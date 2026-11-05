// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// gta4l eMMC diagnostic probe.
//
// The probe is opt-in with the boot argument "gta4l-emmc-probe=1". It maps the
// SDHCI, GCC SDCC1 and SDC1 TLMM windows, runs the portable core, reads the
// protective MBR and primary GPT header, and reports the result. It does not
// publish a block device, mount a filesystem, write the card or touch the PMIC.
//
// The probe is a bring-up diagnostic: it panics with the report so the serial
// log captures the last completed stage. The working RAM-root path is unchanged.

#include <sys/types.h>
#include <pexpert/pexpert.h>
#include <kern/debug.h>

#include "Gta4lEmmc.h"
#include "Gta4lEmmcPlatform.h"

void gta4l_emmc_probe_preflight(void);
void gta4l_emmc_probe_run(void);

/* Extended content phase (stage A). The LBA list is derived from the pinned
 * recovery GPT header: the primary partition-entry array starts at LBA 2 and
 * holds 84 128-byte entries (21 sectors), the first usable LBA is 34 and the
 * backup header is the last sector. The gate refuses to read the range unless
 * the live header carries exactly these values, so a changed layout aborts
 * the diagnostic instead of reading pinned LBAs that no longer mean the same
 * regions. */
#define GTA4L_EMMC_PROBE_ARRAY_LBA       2u
#define GTA4L_EMMC_PROBE_ARRAY_BLOCKS    21u
#define GTA4L_EMMC_PROBE_ENTRY_COUNT     84u
#define GTA4L_EMMC_PROBE_ENTRY_SIZE      128u
#define GTA4L_EMMC_PROBE_FIRST_USABLE    34u
#define GTA4L_EMMC_PROBE_BACKUP_SECTORS  33u

/* Stage-7 data-timeout audit. SDHCI puts the timeout count in the byte
 * TIMEOUT_CONTROL register at 0x2E, not in CLOCK_CONTROL. Count 0 is the
 * minimum; the exact duration depends on the controller's timeout clock and
 * is not assumed here. E2 repeats the 1-bit CMD19 write wedge under an 8 s
 * software bound so a late controller timeout could still be observed. */
#define GTA4L_EMMC_PROBE_DTO_BLOCKS      8u
#define GTA4L_EMMC_PROBE_DTO_WINDOW_US   8000000u

/* GPT header field offsets used by the gate. */
#define GTA4L_EMMC_PROBE_GPT_BACKUP_LBA   0x20u
#define GTA4L_EMMC_PROBE_GPT_FIRST_USABLE 0x28u
#define GTA4L_EMMC_PROBE_GPT_LAST_USABLE  0x30u
#define GTA4L_EMMC_PROBE_GPT_ENTRIES_LBA  0x48u
#define GTA4L_EMMC_PROBE_GPT_ENTRY_COUNT  0x50u
#define GTA4L_EMMC_PROBE_GPT_ENTRY_SIZE   0x54u

struct gta4l_emmc_probe {
	struct gta4l_emmc_platform platform;
	uint32_t log_count;
	uint32_t last_event;
	uint32_t last_a;
	uint32_t last_b;
};

/* Sampled SDHCI and GCC register values passed to the report formatters. The
 * report functions are separate from the sampling code so the host test can
 * format synthetic field-wise bounds through the same production format
 * strings and argument lists. */
struct gta4l_emmc_probe_regs {
	uint32_t version;
	uint8_t power;
	uint8_t host_control;
	uint16_t clock;
	uint16_t host_control2;
	uint16_t host_version;
	uint32_t vendor_spec;
	uint32_t vendor_spec3;
	uint32_t dll_config;
	uint32_t dll_status;
	uint32_t ddr_config;
	uint32_t pwr_status;
	uint32_t cfg;
	uint32_t gcc_m;
	uint32_t gcc_n;
	uint32_t gcc_d;
	uint32_t cmd;
	uint32_t bcr;
	uint32_t apps_cbcr;
	uint32_t ahb_cbcr;
};

/* Extended content phase values. The sums are per-LBA accumulator values over
 * the whole 512-byte block; the GPT fields are the gated header values that
 * selected the LBA list. */
struct gta4l_emmc_probe_range {
	uint32_t array[GTA4L_EMMC_PROBE_ARRAY_BLOCKS];
	uint32_t entries_lba;
	uint32_t entry_count;
	uint32_t entry_size;
	uint32_t first_usable;
	uint32_t last_usable;
	uint32_t backup_lba;
	uint32_t data_sum;
	uint32_t last_sum;
	/* CMD18 pass: per-LBA sums and the in-device comparison against the
	 * CMD17 pass. mb_same is 1 when every sum agrees (mb_div is then 0)
	 * and 0 on the first differing LBA, reported as mb_div. */
	uint32_t mb[GTA4L_EMMC_PROBE_ARRAY_BLOCKS];
	uint32_t mb_same;
	uint32_t mb_div;
};

/* Stage-B1 bus-width rung evidence. One instance is kept per attempted width
 * plus the 1-bit baseline used for the in-device content comparison. `result`
 * carries the core's CMD6 readback and CMD19/CMD14 words; `same`/`div` report
 * the CMD17 content comparison against the baseline, with `div` the first
 * differing LBA. */
struct gta4l_emmc_probe_bus {
	struct gta4l_emmc_bus_result result;
	struct gta4l_emmc_bus_result test;
	uint32_t attempted;
	uint32_t tested;
	uint32_t b1c_enabled;
	uint32_t continued;
	uint32_t same;
	uint32_t div;
	uint32_t block0_sum;
	uint32_t block1_sum;
	uint32_t blocks[GTA4L_EMMC_PROBE_ARRAY_BLOCKS];
	uint32_t data_sum;
	uint32_t last_sum;
	/* Post-continuation EXT_CSD read at 4-bit: byte 183 and the byte sum.
	 * Both stay zero when the continuation did not reach the re-read. */
	uint32_t reread_width;
	uint32_t reread_sum;
	/* Pad/DLL samples taken immediately before the rung, so the report
	 * can compare them with the post-run sample on its PAD line. */
	uint32_t pad_qdsd;
	uint32_t pad_dll;
	uint32_t pad_dst;
};

/* The bus-width rung stops with a content disagreement that the per-width
 * CMD17 sums exposed; it is not a controller or card-status code. */
#define GTA4L_EMMC_PROBE_ERR_CONTENT     -21

/* CMD7 R1b capture block shared by the failure and success reports. The field
 * order is part of GTA4L_EMMC_CAPTURE_FMT. Every value is an observed sample,
 * not proof of continuous bus visibility. c7p: pre-command state and raw
 * window. c7q: poll OR mask, first/last normal, iterations and ticks. c7c:
 * first observed CC/TC. c7t: terminal state and raw window. c7x: bounded trace
 * (iteration:normal:error) and overflow. */
#define GTA4L_EMMC_PROBE_CMD7_FMT \
	"\nc7p=%u/%04x/%04x/%08x/%04x/%04x/%08x,%08x,%08x,%08x" \
	"\nc7q=%u/%04x/%04x/%04x/%04x/%u/%08x%08x" \
	" c7c=%u/%u/%08x%08x/%u/%u/%08x%08x" \
	"\nc7t=%u/%04x/%04x/%08x/%08x/%08x/%08x,%08x,%08x,%08x" \
	"\nc7x=%u/%u/%u:%04x:%04x,%u:%04x:%04x,%u:%04x:%04x," \
	"%u:%04x:%04x"

#define GTA4L_EMMC_PROBE_CMD7_ARGS(host) \
	(unsigned int)(host).cmd7_pre_capture_valid, \
	(unsigned int)(host).cmd7_pre_normal, \
	(unsigned int)(host).cmd7_pre_error, \
	(host).cmd7_pre_present, \
	(unsigned int)(host).cmd7_pre_normal_enable, \
	(unsigned int)(host).cmd7_pre_error_enable, \
	(host).cmd7_pre_response[0], (host).cmd7_pre_response[1], \
	(host).cmd7_pre_response[2], (host).cmd7_pre_response[3], \
	(unsigned int)(host).cmd7_poll_capture_valid, \
	(unsigned int)(host).cmd7_poll_or_normal, \
	(unsigned int)(host).cmd7_poll_or_error, \
	(unsigned int)(host).cmd7_poll_first_normal, \
	(unsigned int)(host).cmd7_poll_last_normal, \
	(host).cmd7_poll_iterations, \
	(unsigned int)((host).cmd7_poll_ticks >> 32), \
	(unsigned int)(host).cmd7_poll_ticks, \
	(unsigned int)(host).cmd7_cc_valid, (host).cmd7_cc_iteration, \
	(unsigned int)((host).cmd7_cc_ticks >> 32), \
	(unsigned int)(host).cmd7_cc_ticks, \
	(unsigned int)(host).cmd7_tc_valid, (host).cmd7_tc_iteration, \
	(unsigned int)((host).cmd7_tc_ticks >> 32), \
	(unsigned int)(host).cmd7_tc_ticks, \
	(unsigned int)(host).cmd7_term_capture_valid, \
	(unsigned int)(host).cmd7_term_normal, \
	(unsigned int)(host).cmd7_term_error, \
	(host).cmd7_term_present, \
	(unsigned int)(host).cmd7_term_command, \
	(host).cmd7_term_argument, \
	(host).cmd7_term_response[0], (host).cmd7_term_response[1], \
	(host).cmd7_term_response[2], (host).cmd7_term_response[3], \
	(host).cmd7_trace_count, (host).cmd7_trace_overflow, \
	(host).cmd7_trace[0].iteration, \
	(unsigned int)(host).cmd7_trace[0].normal, \
	(unsigned int)(host).cmd7_trace[0].error, \
	(host).cmd7_trace[1].iteration, \
	(unsigned int)(host).cmd7_trace[1].normal, \
	(unsigned int)(host).cmd7_trace[1].error, \
	(host).cmd7_trace[2].iteration, \
	(unsigned int)(host).cmd7_trace[2].normal, \
	(unsigned int)(host).cmd7_trace[2].error, \
	(host).cmd7_trace[3].iteration, \
	(unsigned int)(host).cmd7_trace[3].normal, \
	(unsigned int)(host).cmd7_trace[3].error

static int gta4l_emmc_probe_ran;

static uint8_t
gta4l_emmc_probe_read8(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	return gta4l_emmc_platform_read8(&probe->platform, space, offset);
}

static uint16_t
gta4l_emmc_probe_read16(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	return gta4l_emmc_platform_read16(&probe->platform, space, offset);
}

static uint32_t
gta4l_emmc_probe_read32(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	return gta4l_emmc_platform_read32(&probe->platform, space, offset);
}

static void
gta4l_emmc_probe_write8(void *context, uint32_t space, uint32_t offset,
    uint8_t value)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	gta4l_emmc_platform_write8(&probe->platform, space, offset, value);
}

static void
gta4l_emmc_probe_write16(void *context, uint32_t space, uint32_t offset,
    uint16_t value)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	gta4l_emmc_platform_write16(&probe->platform, space, offset, value);
}

static void
gta4l_emmc_probe_write32(void *context, uint32_t space, uint32_t offset,
    uint32_t value)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	gta4l_emmc_platform_write32(&probe->platform, space, offset, value);
}

static void
gta4l_emmc_probe_barrier(void *context)
{
	(void)context;
	gta4l_emmc_platform_barrier();
}

static void
gta4l_emmc_probe_delay_us(void *context, uint32_t microseconds)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	gta4l_emmc_platform_delay_us(&probe->platform, microseconds);
}

static void
gta4l_emmc_probe_log(void *context, uint32_t event, uint32_t a, uint32_t b)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	probe->log_count++;
	probe->last_event = event;
	probe->last_a = a;
	probe->last_b = b;
}

static uint64_t
gta4l_emmc_probe_counter_op(void *context)
{
	(void)context;
	return gta4l_emmc_platform_counter();
}

static const struct gta4l_emmc_ops gta4l_emmc_probe_ops = {
	.read8 = gta4l_emmc_probe_read8,
	.read16 = gta4l_emmc_probe_read16,
	.read32 = gta4l_emmc_probe_read32,
	.write8 = gta4l_emmc_probe_write8,
	.write16 = gta4l_emmc_probe_write16,
	.write32 = gta4l_emmc_probe_write32,
	.barrier = gta4l_emmc_probe_barrier,
	.delay_us = gta4l_emmc_probe_delay_us,
	.log = gta4l_emmc_probe_log,
	.counter = gta4l_emmc_probe_counter_op,
};

static int
gta4l_emmc_probe_parse(const char *name, int *value)
{
	return PE_parse_boot_argn(name, value, sizeof(*value)) && (*value == 1);
}

/* 1 = full probe, 2 = controller/host init only, 3 = host init plus card
 * identification without sector reads, 4 = stage-B1 bus-width switch and
 * CMD19/CMD14 bus test with a per-width content pass, 5 = stage-B1C, which
 * adds the continue-past-readback continuation on the exact observed 4-bit
 * tuple, 6 = stage-B1C plus a 1-bit CMD19/CMD14 discriminator that runs
 * after the stage-A pass and before any width switch, 7 = the data-timeout
 * audit (readback, armed-minimum healthy multi-block read, recovery read and
 * a repeated 1-bit CMD19 wedge under an extended bound; no CMD6). On stage 6
 * a failed 1-bit test stops the rung (report stop=9) and issues no CMD6; a
 * passing 1-bit test falls through to the stage-5 flow unchanged. Stage 7
 * always reports and stops with the DTO report. Stage 8 is the HS-timing
 * rung: it switches byte 185, ramps to the 50 MHz HS row, repeats the
 * stage-A content pass and returns to legacy before its B2 report. Stage 9
 * is the H9 identification rung: it runs the read-only identification
 * sequence once with one designed duplicate CMD3 after a completion
 * timeout, then always stops with the H9 report. Stage 10 is the H10
 * width-at-HS continuation: after the stage-A pass it enters HS, switches
 * the BUS_WIDTH to 4-bit at HS, runs one CMD19 round trip and the content
 * pass at 4-bit/50 MHz, reverts both and stops with the H10 report. Stage 11
 * is the H11 width-value continuation: it replays the H10 4-bit switch as a
 * record-only sample, attempts the 8-bit BUS_WIDTH value at HS, and runs the
 * CMD19 round trip and content pass at 8-bit/50 MHz only when that switch is
 * honored; it reverts both and stops with the H11 report. Stage 12 is the
 * H12 record-only DDR attempt: after the stage-A pass it enters HS, issues
 * one CMD6 SET byte 183 = 4, reads the value back at the 4-bit SDR DAT
 * width without applying any controller DDR mode, reverts both and stops
 * with the H12 report. Stage 13 is the rung-1 write-phase recovery: after
 * the stage-A pass it runs one 1-bit CMD19 write, one bounded SDHCI
 * data-circuit reset, records the post-reset present state, issues one
 * CMD14 read and the block-0 recovery read, then stops with the H13
 * report. Stage 14 is the rung-2 write-phase recovery: the same stage-A
 * pass and CMD19 write and bounded reset, then one forced CMD12
 * STOP_TRANSMISSION and one CMD13 SEND_STATUS while the post-reset inhibit
 * is clear, then the record-only CMD14 and block-0 reads, and stops with
 * the H14 report. Stage 15 is the corrected verified-width rung: the
 * stage-A pass, a 4-bit SDR switch verified by a read-only EXT_CSD
 * properties comparison with card+host rollback, the corrected
 * CMD19/CMD14 test, a 4-bit content pass, a verified return to 1-bit and
 * the recovery read; it stops with the H15 report. Stage 16 is the
 * content-only isolation rung: the same baseline and verified 4-bit switch,
 * the 4-bit content pass and recovery, with no CMD19 or CMD14 at all; it
 * stops with the H16 report. Stage 17 is the Linux-compatible bus-test rung:
 * the same baseline, verified 4-bit switch and 4-bit content pass, then one
 * CMD19 write whose data timeout is recorded and tolerated, one bounded data
 * reset, one CMD14 read, the verified revert and the recovery read; it stops
 * with the H17 report. All three use the baseline-based recovery that
 * issues CMD6 and a bounded local data reset even when DAT_INHIBIT is
 * latched. Stages 13 and 14 issue no CMD6, CMD18, CMD25 or user-data
 * write. */
static int
gta4l_emmc_probe_stage(void)
{
	int value = 0;

	if (!PE_parse_boot_argn("gta4l-emmc-probe", &value, sizeof(value))) {
		return 0;
	}
	if ((value < 1) || (value > 17)) {
		return 0;
	}
	return value;
}

static int
gta4l_emmc_probe_conflict(void)
{
	int value = 0;

	return gta4l_emmc_probe_parse("gta4l-sdhci-audit", &value) ||
	    gta4l_emmc_probe_parse("gta4l-sdhci-snapshot", &value) ||
	    gta4l_emmc_probe_parse("gta4l-sdhci-host-init", &value);
}

/* Multi-block mode is a modifier of the probe, never a diagnostic on its
 * own. A bare gta4l-emmc-mb argument is refused before any MMIO is mapped. */
static int
gta4l_emmc_probe_mb(void)
{
	int value = 0;

	return gta4l_emmc_probe_parse("gta4l-emmc-mb", &value);
}

static void
gta4l_emmc_probe_refuse_mb(void)
{
	panic("GTA4L-EMMC PROBE REFUSED: conflicting diagnostic arguments "
	    "(gta4l-emmc-mb requires gta4l-emmc-probe)");
}

void
gta4l_emmc_probe_preflight(void)
{
	if (gta4l_emmc_probe_stage() == 0) {
		if (gta4l_emmc_probe_mb() != 0) {
			gta4l_emmc_probe_refuse_mb();
		}
		return;
	}
	if (gta4l_emmc_probe_conflict()) {
		panic("GTA4L-EMMC PROBE REFUSED: conflicting diagnostic "
		    "arguments");
	}
}

static uint32_t
gta4l_emmc_probe_sum(const uint8_t *buffer, uint32_t length)
{
	uint32_t sum = 0;
	uint32_t index;

	for (index = 0; index < length; index++) {
		sum = (sum * 31u) + buffer[index];
	}
	return sum;
}

static uint32_t
gta4l_emmc_probe_word(const uint8_t *buffer, uint32_t offset)
{
	return (uint32_t)buffer[offset] |
	    ((uint32_t)buffer[offset + 1u] << 8) |
	    ((uint32_t)buffer[offset + 2u] << 16) |
	    ((uint32_t)buffer[offset + 3u] << 24);
}

static uint64_t
gta4l_emmc_probe_long(const uint8_t *buffer, uint32_t offset)
{
	return (uint64_t)gta4l_emmc_probe_word(buffer, offset) |
	    ((uint64_t)gta4l_emmc_probe_word(buffer, offset + 4u) << 32);
}

static int
gta4l_emmc_probe_gpt_signature(const uint8_t *buffer)
{
	static const uint8_t signature[8] = {
		'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'
	};
	uint32_t index;

	for (index = 0; index < 8u; index++) {
		if (buffer[index] != signature[index]) {
			return 0;
		}
	}
	return 1;
}

/* Gate the pinned LBA list against the live GPT header. Each failed check
 * sets one bit so the abort report names the mismatched field instead of a
 * generic refusal. */
static uint32_t
gta4l_emmc_probe_gpt(struct gta4l_emmc_probe_range *range,
    const uint8_t *block1, uint32_t capacity_blocks)
{
	uint64_t backup_lba;
	uint64_t first_usable;
	uint64_t last_usable;
	uint64_t entries_lba;
	uint32_t entry_count;
	uint32_t entry_size;
	uint32_t bad = 0;

	if (gta4l_emmc_probe_gpt_signature(block1) == 0) {
		bad |= 0x01u;
	}
	backup_lba = gta4l_emmc_probe_long(block1,
	    GTA4L_EMMC_PROBE_GPT_BACKUP_LBA);
	first_usable = gta4l_emmc_probe_long(block1,
	    GTA4L_EMMC_PROBE_GPT_FIRST_USABLE);
	last_usable = gta4l_emmc_probe_long(block1,
	    GTA4L_EMMC_PROBE_GPT_LAST_USABLE);
	entries_lba = gta4l_emmc_probe_long(block1,
	    GTA4L_EMMC_PROBE_GPT_ENTRIES_LBA);
	entry_count = gta4l_emmc_probe_word(block1,
	    GTA4L_EMMC_PROBE_GPT_ENTRY_COUNT);
	entry_size = gta4l_emmc_probe_word(block1,
	    GTA4L_EMMC_PROBE_GPT_ENTRY_SIZE);

	range->entries_lba = (uint32_t)entries_lba;
	range->entry_count = entry_count;
	range->entry_size = entry_size;
	range->first_usable = (uint32_t)first_usable;
	range->last_usable = (uint32_t)last_usable;
	range->backup_lba = (uint32_t)backup_lba;

	if ((capacity_blocks == 0u) ||
	    (backup_lba != (uint64_t)capacity_blocks - 1u)) {
		bad |= 0x02u;
	}
	if (first_usable != GTA4L_EMMC_PROBE_FIRST_USABLE) {
		bad |= 0x04u;
	}
	if ((last_usable + GTA4L_EMMC_PROBE_BACKUP_SECTORS) != backup_lba) {
		bad |= 0x08u;
	}
	if (entries_lba != GTA4L_EMMC_PROBE_ARRAY_LBA) {
		bad |= 0x10u;
	}
	if (entry_count != GTA4L_EMMC_PROBE_ENTRY_COUNT) {
		bad |= 0x20u;
	}
	if (entry_size != GTA4L_EMMC_PROBE_ENTRY_SIZE) {
		bad |= 0x40u;
	}
	return bad;
}

/* Stage-B1 content pass. Reads the same 25 LBA values as the stage-A probe
 * (block 0/1, the 21-entry primary array, the first usable sector and the
 * last sector) and stores the per-LBA sums. The baseline call also gates the
 * live GPT header and fills `range`; width passes reuse that gated list. A
 * read failure returns the core result and leaves the partial sums in `bus`
 * for the stop report. */
static int
gta4l_emmc_probe_b1_content(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe_range *range,
    struct gta4l_emmc_probe_bus *bus, uint32_t *gate_mask)
{
	static uint8_t block[GTA4L_EMMC_BLOCK_BYTES];
	uint32_t index;
	int result;

	result = gta4l_emmc_read_block(host, 0, block);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	bus->block0_sum = gta4l_emmc_probe_sum(block,
	    GTA4L_EMMC_BLOCK_BYTES);

	result = gta4l_emmc_read_block(host, 1, block);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	bus->block1_sum = gta4l_emmc_probe_sum(block,
	    GTA4L_EMMC_BLOCK_BYTES);
	if (gate_mask != 0) {
		*gate_mask = gta4l_emmc_probe_gpt(range, block,
		    host->capacity_blocks);
		if (*gate_mask != 0u) {
			return GTA4L_EMMC_OK;
		}
	}

	host->last_step = GTA4L_EMMC_STEP_PROBE_ARRAY;
	for (index = 0; index < GTA4L_EMMC_PROBE_ARRAY_BLOCKS; index++) {
		result = gta4l_emmc_read_block(host,
		    (uint64_t)range->entries_lba + index, block);
		if (result != GTA4L_EMMC_OK) {
			return result;
		}
		bus->blocks[index] = gta4l_emmc_probe_sum(block,
		    GTA4L_EMMC_BLOCK_BYTES);
	}

	host->last_step = GTA4L_EMMC_STEP_PROBE_DATA;
	result = gta4l_emmc_read_block(host, range->first_usable, block);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	bus->data_sum = gta4l_emmc_probe_sum(block, GTA4L_EMMC_BLOCK_BYTES);

	host->last_step = GTA4L_EMMC_STEP_PROBE_LAST;
	result = gta4l_emmc_read_block(host, range->backup_lba, block);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	bus->last_sum = gta4l_emmc_probe_sum(block, GTA4L_EMMC_BLOCK_BYTES);
	return GTA4L_EMMC_OK;
}

/* Compare one width's content pass against the 1-bit baseline. `div` is the
 * absolute LBA of the first differing value, or 0 when every value agrees. */
static void
gta4l_emmc_probe_b1_compare(const struct gta4l_emmc_probe_range *range,
    const struct gta4l_emmc_probe_bus *base,
    struct gta4l_emmc_probe_bus *bus)
{
	uint32_t index;

	bus->same = 1;
	bus->div = 0;
	if (bus->block0_sum != base->block0_sum) {
		bus->same = 0;
		return;
	}
	if (bus->block1_sum != base->block1_sum) {
		bus->same = 0;
		bus->div = 1u;
		return;
	}
	for (index = 0; index < GTA4L_EMMC_PROBE_ARRAY_BLOCKS; index++) {
		if (bus->blocks[index] != base->blocks[index]) {
			bus->same = 0;
			bus->div = range->entries_lba + index;
			return;
		}
	}
	if (bus->data_sum != base->data_sum) {
		bus->same = 0;
		bus->div = range->first_usable;
		return;
	}
	if (bus->last_sum != base->last_sum) {
		bus->same = 0;
		bus->div = range->backup_lba;
	}
}

/* B1C continue condition, pinned to exactly the observed 4-bit tuple:
 * a clean CMD6 R1 in TRAN with READY_FOR_DATA, the host width bit written and
 * read back as 0x02, an EXT_CSD read at the attempted width that completed
 * with the 1-bit byte sum, and byte 183 still 0 (readback code 0). Anything
 * else keeps the stage-B1 stop-on-first-failure behavior. */
static int
gta4l_emmc_probe_b1c_enter(const struct gta4l_emmc_host *host,
    const struct gta4l_emmc_probe_bus *bus)
{
	uint32_t status = bus->result.switch_status;
	uint32_t state;

	if (bus->result.requested != 4u) {
		return 0;
	}
	if (bus->result.switch_result != GTA4L_EMMC_ERR_BUS_WIDTH) {
		return 0;
	}
	if (bus->result.switched != GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1) {
		return 0;
	}
	if (bus->result.host_after != GTA4L_EMMC_HOST_CONTROL_4BIT) {
		return 0;
	}
	if (bus->result.host_before != 0u) {
		/* The observed pre-attempt HOST_CONTROL was zero; any other
		 * state is a different rung and must keep the stop behavior. */
		return 0;
	}
	if (host->bus_width != (uint8_t)GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1) {
		return 0;
	}
	if ((status & GTA4L_EMMC_R1_ERROR_MASK) != 0u) {
		return 0;
	}
	state = (status & GTA4L_EMMC_R1_STATE_MASK) >> GTA4L_EMMC_R1_STATE_SHIFT;
	if (state != GTA4L_EMMC_R1_STATE_TRAN) {
		return 0;
	}
	if ((status & GTA4L_EMMC_R1_READY_FOR_DATA) == 0u) {
		return 0;
	}
	if (bus->result.readback_sum != host->ext_csd_sum) {
		return 0;
	}
	return 1;
}

/* Re-apply the 4-bit host width after the failed switch verification. The
 * core reverted HOST_CONTROL on the failed readback, so the continuation
 * applies the attempted host width itself and marks the host state as 4-bit
 * so gta4l_emmc_bus_test() accepts the rung. The card's BUS_WIDTH field is
 * deliberately not rewritten: this rung measures the data path, not the
 * switch. */
static void
gta4l_emmc_probe_b1c_apply_4bit(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe)
{
	uint32_t control;

	control = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	control &= ~(GTA4L_EMMC_HOST_CONTROL_4BIT |
	    GTA4L_EMMC_HOST_CONTROL_8BIT);
	control |= GTA4L_EMMC_HOST_CONTROL_4BIT;
	gta4l_emmc_probe_write8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL, control);
	gta4l_emmc_probe_barrier(probe);
	host->bus_width = (uint8_t)GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
}

/* Re-read EXT_CSD after the continuation content pass: a late BUS_WIDTH
 * commit would appear here even though the verification readback saw 0. */
static int
gta4l_emmc_probe_b1c_reread(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe_bus *bus)
{
	uint8_t ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t sum = 0;
	uint32_t index;
	int result;

	result = gta4l_emmc_read_ext_csd(host, ext_csd);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	bus->reread_width =
	    (uint32_t)ext_csd[GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF] & 0x0Fu;
	/* The `es` field is the core's plain byte sum, not the probe's
	 * rolling content hash, so the two sums stay comparable. */
	for (index = 0; index < GTA4L_EMMC_EXT_CSD_BYTES; index++) {
		sum += ext_csd[index];
	}
	bus->reread_sum = sum;
	return GTA4L_EMMC_OK;
}

/* Restore the host width and the recorded card width after the continuation.
 * `control` is the pre-attempt HOST_CONTROL byte saved by the failed switch. */
static void
gta4l_emmc_probe_b1c_restore(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe, uint32_t control)
{
	gta4l_emmc_probe_write8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL, (uint8_t)control);
	gta4l_emmc_probe_barrier(probe);
	host->bus_width = (uint8_t)GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1;
}

static void
gta4l_emmc_probe_report_failure(const struct gta4l_emmc_host *host,
    const struct gta4l_emmc_probe *probe, int stage, int result,
    const struct gta4l_emmc_probe_regs *regs)
{
	panic("GTA4L-EMMC PROBE FAILED: stage=%d rc=%d "
	    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u "
	    "rsp=%08x,%08x,%08x,%08x "
	    "cmd1rsp=%u/%08x,%08x,%08x,%08x "
	    "pre2rsp=%u/%08x,%08x,%08x,%08x "
	    "c2=%u/%04x/%04x/%08x,%08x,%08x,%08x "
	    "c3p=%u/%04x/%04x/%08x/%04x/%04x "
	    "c3t=%u/%u/%04x/%08x/%u/%08x,%08x,%08x,%08x "
	    "c3l=%u/%u/%04x/%04x/%u/%08x%08x/%u "
	    "cid=%08x,%08x,%08x,%08x fmt=4 hv=%04x "
	    "cmd1=%u/%u/%08x "
	    "clocks=%u mci=%08x pwr=%02x clk=%04x hc=%02x "
	    "hc2=%04x ven=%08x v3=%08x dll=%08x dst=%08x "
	    "ddr=%08x pst=%08x cfg=%08x mnd=%08x/%08x/%08x "
	    "cmd=%08x bcr=%08x apps=%08x ahb=%08x step=%u "
	    "log=%u/%u/%u/%u" GTA4L_EMMC_PROBE_CMD7_FMT,
	    stage, result, (unsigned int)host->fail_valid,
	    host->fail_index, host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step, host->fail_response[0],
	    host->fail_response[1], host->fail_response[2],
	    host->fail_response[3],
	    (unsigned int)host->cmd1_response_valid,
	    host->cmd1_response[0], host->cmd1_response[1],
	    host->cmd1_response[2], host->cmd1_response[3],
	    (unsigned int)host->pre_cmd2_response_valid,
	    host->pre_cmd2_response[0], host->pre_cmd2_response[1],
	    host->pre_cmd2_response[2], host->pre_cmd2_response[3],
	    (unsigned int)host->cmd2_capture_valid,
	    (unsigned int)host->cmd2_normal,
	    (unsigned int)host->cmd2_error,
	    host->cmd2_raw_response[0], host->cmd2_raw_response[1],
	    host->cmd2_raw_response[2], host->cmd2_raw_response[3],
	    (unsigned int)host->pre_cmd3_capture_valid,
	    (unsigned int)host->pre_cmd3_normal,
	    (unsigned int)host->pre_cmd3_error,
	    host->pre_cmd3_present,
	    (unsigned int)host->pre_cmd3_normal_enable,
	    (unsigned int)host->pre_cmd3_error_enable,
	    (unsigned int)host->timeout_capture_valid,
	    (unsigned int)host->timeout_index,
	    (unsigned int)host->timeout_command,
	    host->timeout_argument,
	    (unsigned int)host->timeout_response_stable,
	    host->timeout_response[0], host->timeout_response[1],
	    host->timeout_response[2], host->timeout_response[3],
	    (unsigned int)host->poll_capture_valid,
	    host->poll_index,
	    (unsigned int)host->poll_first_normal,
	    (unsigned int)host->poll_last_normal,
	    host->poll_iterations,
	    (unsigned int)(host->poll_ticks >> 32),
	    (unsigned int)host->poll_ticks,
	    (unsigned int)probe->platform.counter_hz,
	    host->cid[0], host->cid[1], host->cid[2], host->cid[3],
	    (unsigned int)regs->host_version,
	    host->cmd1_attempts,
	    host->cmd1_timeouts, host->ocr,
	    (unsigned int)host->clocks_enabled, regs->version,
	    (unsigned int)regs->power, (unsigned int)regs->clock,
	    (unsigned int)regs->host_control, (unsigned int)regs->host_control2,
	    regs->vendor_spec, regs->vendor_spec3, regs->dll_config,
	    regs->dll_status, regs->ddr_config, regs->pwr_status, regs->cfg,
	    regs->gcc_m, regs->gcc_n, regs->gcc_d, regs->cmd, regs->bcr,
	    regs->apps_cbcr, regs->ahb_cbcr, host->last_step, probe->log_count,
	    probe->last_event, probe->last_a, probe->last_b,
	    GTA4L_EMMC_PROBE_CMD7_ARGS(*host));
}

static void
gta4l_emmc_probe_report_ident(const struct gta4l_emmc_host *ident_host,
    const struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_regs *regs)
{
	panic("GTA4L-EMMC IDENT REPORT: rc=0 mci=%08x rca=%u cap=%u "
	    "rev=%u bw=%u ocr=%08x ext=%08x hc2=%04x ven=%08x "
	    "dll=%08x dst=%08x pst=%08x step=%u log=%u\n"
	    "GTA4L-EMMC IDENT CAPTURE fmt=4 hv=%04x "
	    "c2=%u/%04x/%04x/%08x,%08x,%08x,%08x "
	    "c3p=%u/%04x/%04x/%08x/%04x/%04x "
	    "c3t=%u/%u/%04x/%08x/%u/%08x,%08x,%08x,%08x "
	    "c3l=%u/%u/%04x/%04x/%u/%08x%08x/%u "
	    "cid=%08x,%08x,%08x,%08x" GTA4L_EMMC_PROBE_CMD7_FMT,
	    ident_host->mci_version, ident_host->rca,
	    ident_host->capacity_blocks,
	    (unsigned int)ident_host->ext_csd_rev,
	    (unsigned int)ident_host->bus_width, ident_host->ocr,
	    ident_host->ext_csd_sum, (unsigned int)regs->host_control2,
	    regs->vendor_spec, regs->dll_config, regs->dll_status,
	    regs->pwr_status, ident_host->last_step, probe->log_count,
	    (unsigned int)regs->host_version,
	    (unsigned int)ident_host->cmd2_capture_valid,
	    (unsigned int)ident_host->cmd2_normal,
	    (unsigned int)ident_host->cmd2_error,
	    ident_host->cmd2_raw_response[0],
	    ident_host->cmd2_raw_response[1],
	    ident_host->cmd2_raw_response[2],
	    ident_host->cmd2_raw_response[3],
	    (unsigned int)ident_host->pre_cmd3_capture_valid,
	    (unsigned int)ident_host->pre_cmd3_normal,
	    (unsigned int)ident_host->pre_cmd3_error,
	    ident_host->pre_cmd3_present,
	    (unsigned int)ident_host->pre_cmd3_normal_enable,
	    (unsigned int)ident_host->pre_cmd3_error_enable,
	    (unsigned int)ident_host->timeout_capture_valid,
	    (unsigned int)ident_host->timeout_index,
	    (unsigned int)ident_host->timeout_command,
	    ident_host->timeout_argument,
	    (unsigned int)ident_host->timeout_response_stable,
	    ident_host->timeout_response[0], ident_host->timeout_response[1],
	    ident_host->timeout_response[2], ident_host->timeout_response[3],
	    (unsigned int)ident_host->poll_capture_valid,
	    ident_host->poll_index,
	    (unsigned int)ident_host->poll_first_normal,
	    (unsigned int)ident_host->poll_last_normal,
	    ident_host->poll_iterations,
	    (unsigned int)(ident_host->poll_ticks >> 32),
	    (unsigned int)ident_host->poll_ticks,
	    (unsigned int)probe->platform.counter_hz,
	    ident_host->cid[0], ident_host->cid[1],
	    ident_host->cid[2], ident_host->cid[3],
	    GTA4L_EMMC_PROBE_CMD7_ARGS(*ident_host));
}

static void
gta4l_emmc_probe_report_read_failure(const struct gta4l_emmc_host *host,
    const char *phase, uint32_t lba, int result)
{
	panic("GTA4L-EMMC PROBE FAILED: %s read lba=%u rc=%d step=%u "
	    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u", phase, lba, result,
	    host->last_step, (unsigned int)host->fail_valid,
	    host->fail_index, host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

/* Small fixed-buffer decimal writer for the mbdiv field. The probe links no
 * formatter and the field is the only decimal value that is not a direct
 * format argument. */
static uint32_t
gta4l_emmc_probe_append_decimal(char *buffer, uint32_t offset, uint32_t value)
{
	char digits[10];
	uint32_t count = 0;
	uint32_t index;

	do {
		digits[count] = (char)('0' + (value % 10u));
		count++;
		value /= 10u;
	} while ((value != 0u) && (count < sizeof(digits)));
	for (index = 0; index < count; index++) {
		buffer[offset + index] = digits[count - 1u - index];
	}
	return offset + count;
}

static uint32_t
gta4l_emmc_probe_append_text(char *buffer, uint32_t offset, const char *text)
{
	uint32_t index = 0;

	while (text[index] != '\0') {
		buffer[offset + index] = text[index];
		index++;
	}
	return offset + index;
}

/* Fixed eight-digit lower-case hex writer for the B1C continuation fields. */
static uint32_t
gta4l_emmc_probe_append_hex8(char *buffer, uint32_t offset, uint32_t value)
{
	static const char digits[] = "0123456789abcdef";
	uint32_t index;

	for (index = 0; index < 8u; index++) {
		buffer[offset + index] =
		    digits[(value >> (28u - (index * 4u))) & 0x0Fu];
	}
	return offset + 8u;
}

static void
gta4l_emmc_probe_report_multi_failure(const struct gta4l_emmc_host *host,
    const struct gta4l_emmc_probe_range *range, int result)
{
	panic("GTA4L-EMMC PROBE FAILED: mb read lba=%u n=%u rc=%d step=%u "
	    "mb=%u drain=%u/%u/%u/%u "
	    "stop=%d/%u/%u/%04x/%04x/%08x/%08x/%08x "
	    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u",
	    range->entries_lba, GTA4L_EMMC_PROBE_ARRAY_BLOCKS, result,
	    host->last_step, host->multi_block,
	    (unsigned int)host->multi_drain_used,
	    (unsigned int)host->multi_drain_resolved,
	    host->multi_drain_blocks, host->multi_drain_iterations,
	    (int)host->multi_stop_result,
	    (unsigned int)host->multi_stop_valid,
	    (unsigned int)host->multi_stop_capture_valid,
	    (unsigned int)host->multi_stop_normal,
	    (unsigned int)host->multi_stop_error,
	    host->multi_stop_present,
	    (unsigned int)host->multi_stop_command,
	    host->multi_stop_argument,
	    (unsigned int)host->fail_valid, host->fail_index,
	    host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

static void
gta4l_emmc_probe_report_b1(const struct gta4l_emmc_host *host,
    const struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_range *range,
    const struct gta4l_emmc_probe_bus *base,
    const struct gta4l_emmc_probe_bus *w4,
    const struct gta4l_emmc_probe_bus *w8, int rc, int stop)
{
	uint32_t qdsd = gta4l_emmc_probe_read32((void *)probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	uint32_t dll = gta4l_emmc_probe_read32((void *)probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	uint32_t dst = gta4l_emmc_probe_read32((void *)probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);
	char b1c[48];
	uint32_t b1c_offset = 0;

	if (w4->b1c_enabled != 0u) {
		b1c_offset = gta4l_emmc_probe_append_text(b1c, b1c_offset,
		    " bc=");
		b1c_offset = gta4l_emmc_probe_append_decimal(b1c, b1c_offset,
		    w4->continued);
		b1c_offset = gta4l_emmc_probe_append_text(b1c, b1c_offset,
		    " e183a=");
		b1c_offset = gta4l_emmc_probe_append_hex8(b1c, b1c_offset,
		    w4->reread_width);
		b1c_offset = gta4l_emmc_probe_append_text(b1c, b1c_offset,
		    " esa=");
		b1c_offset = gta4l_emmc_probe_append_hex8(b1c, b1c_offset,
		    w4->reread_sum);
	}
	b1c[b1c_offset] = '\0';

	panic("GTA4L-EMMC B1 REPORT: rc=%d stop=%u mci=%08x rca=%u cap=%u "
	    "rev=%u hci=%02x step=%u log=%u\n"
	    "GTA4L-EMMC B1 BW=1 att=%u req=1 sw=%u swrc=0 tt=%u "
	    "tw=%08x te=%08x trc=%d hc=%02x es=%08x b0=%08x b1=%08x "
	    "same=1 div=0\n"
	    "GTA4L-EMMC B1 BW=4 att=%u req=%u sw=%u swrc=%d tt=%u "
	    "tw=%08x te=%08x trc=%d hc=%02x hb=%02x ha=%02x c6=%08x "
	    "es=%08x b0=%08x b1=%08x same=%u div=%u%s\n"
	    "GTA4L-EMMC B1 BW=8 att=%u req=%u sw=%u swrc=%d tt=%u "
	    "tw=%08x te=%08x trc=%d hc=%02x hb=%02x ha=%02x c6=%08x "
	    "es=%08x b0=%08x b1=%08x same=%u div=%u\n"
	    "GTA4L-EMMC B1 BLOCKS 4"
	    " 2=%08x 3=%08x 4=%08x 5=%08x 6=%08x 7=%08x 8=%08x 9=%08x"
	    " 10=%08x 11=%08x 12=%08x 13=%08x 14=%08x 15=%08x 16=%08x"
	    " 17=%08x 18=%08x 19=%08x 20=%08x 21=%08x 22=%08x\n"
	    "GTA4L-EMMC B1 DATA 4 %u=%08x\n"
	    "GTA4L-EMMC B1 LSTLBA 4 %u=%08x\n"
	    "GTA4L-EMMC B1 BLOCKS 8"
	    " 2=%08x 3=%08x 4=%08x 5=%08x 6=%08x 7=%08x 8=%08x 9=%08x"
	    " 10=%08x 11=%08x 12=%08x 13=%08x 14=%08x 15=%08x 16=%08x"
	    " 17=%08x 18=%08x 19=%08x 20=%08x 21=%08x 22=%08x\n"
	    "GTA4L-EMMC B1 DATA 8 %u=%08x\n"
	    "GTA4L-EMMC B1 LSTLBA 8 %u=%08x\n"
	    "GTA4L-EMMC B1 PAD pqdsd=%08x pdll=%08x pdst=%08x "
	    "qdsd=%08x dll=%08x dst=%08x bif=none\n"
	    "GTA4L-EMMC B1 FAIL fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u",
	    rc, stop, host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev,
	    (unsigned int)base->result.host_control,
	    host->last_step, probe->log_count,
	    base->attempted, base->result.switched, base->tested,
	    base->test.test_word, base->test.test_expected,
	    base->test.test_result,
	    (unsigned int)base->result.host_control, host->ext_csd_sum,
	    base->block0_sum, base->block1_sum,
	    w4->attempted, w4->result.requested, w4->result.switched,
	    w4->result.switch_result, w4->tested, w4->test.test_word,
	    w4->test.test_expected, w4->test.test_result,
	    (unsigned int)w4->result.host_control,
	    (unsigned int)w4->result.host_before,
	    (unsigned int)w4->result.host_after, w4->result.switch_status,
	    w4->result.readback_sum, w4->block0_sum,
	    w4->block1_sum, w4->same, w4->div, b1c,
	    w8->attempted, w8->result.requested, w8->result.switched,
	    w8->result.switch_result, w8->tested, w8->test.test_word,
	    w8->test.test_expected, w8->test.test_result,
	    (unsigned int)w8->result.host_control,
	    (unsigned int)w8->result.host_before,
	    (unsigned int)w8->result.host_after, w8->result.switch_status,
	    w8->result.readback_sum, w8->block0_sum,
	    w8->block1_sum, w8->same, w8->div,
	    w4->blocks[0], w4->blocks[1], w4->blocks[2], w4->blocks[3],
	    w4->blocks[4], w4->blocks[5], w4->blocks[6], w4->blocks[7],
	    w4->blocks[8], w4->blocks[9], w4->blocks[10], w4->blocks[11],
	    w4->blocks[12], w4->blocks[13], w4->blocks[14], w4->blocks[15],
	    w4->blocks[16], w4->blocks[17], w4->blocks[18], w4->blocks[19],
	    w4->blocks[20],
	    range->first_usable, w4->data_sum,
	    range->backup_lba, w4->last_sum,
	    w8->blocks[0], w8->blocks[1], w8->blocks[2], w8->blocks[3],
	    w8->blocks[4], w8->blocks[5], w8->blocks[6], w8->blocks[7],
	    w8->blocks[8], w8->blocks[9], w8->blocks[10], w8->blocks[11],
	    w8->blocks[12], w8->blocks[13], w8->blocks[14], w8->blocks[15],
	    w8->blocks[16], w8->blocks[17], w8->blocks[18], w8->blocks[19],
	    w8->blocks[20],
	    range->first_usable, w8->data_sum,
	    range->backup_lba, w8->last_sum,
	    base->pad_qdsd, base->pad_dll, base->pad_dst,
	    qdsd, dll, dst,
	    (unsigned int)host->fail_valid, host->fail_index,
	    host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

/* Stage-7 data-timeout audit capture. Every field is an observed value: R1 is
 * the timeout/enable readback, E1 the healthy 8-block read under the armed
 * minimum, Q the recovery read and E2 the repeated 1-bit CMD19 wedge under
 * the extended software bound. */
struct gta4l_emmc_probe_dto {
	uint16_t clk;
	uint16_t normal_enable;
	uint16_t error_enable;
	uint16_t normal_signal;
	uint16_t error_signal;
	uint16_t error_status;
	uint8_t  toc;
	uint8_t  arm;
	uint8_t  restored;
	uint32_t e1_blocks;
	int      e1_result;
	uint32_t e1_fail_valid;
	uint32_t e1_fail_index;
	uint32_t e1_fail_argument;
	uint32_t e1_fail_command;
	uint16_t e1_fail_error;
	uint16_t e1_fail_normal;
	uint32_t e1_fail_present;
	uint32_t e1_fail_step;
	uint32_t e1_multi_block;
	uint32_t e1_drain_used;
	uint32_t e1_drain_resolved;
	uint32_t e1_drain_blocks;
	uint32_t e1_drain_iterations;
	int      e1_stop_result;
	uint32_t e1_stop_valid;
	uint32_t e1_stop_capture_valid;
	int      q_result;
	uint32_t e2_attempted;
	uint32_t e2_window;
	int      e2_result;
	uint32_t e2_tested;
	uint32_t e2_word;
	uint32_t e2_expected;
	int      e2_test_result;
	uint8_t  e2_host_control;
};

static void
gta4l_emmc_probe_report_dto(const struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_dto *dto, int rc, int stop)
{
	uint8_t hci = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	uint32_t qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	uint32_t dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	uint32_t dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

	panic("GTA4L-EMMC DTO REPORT: rc=%d stop=%u mci=%08x rca=%u cap=%u "
	    "rev=%u hci=%02x step=%u log=%u\n"
	    "GTA4L-EMMC DTO R1 clk=%04x ne=%04x ee=%04x ns=%04x se=%04x "
	    "es=%04x toc=%02x arm=%02x restored=%02x\n"
	    "GTA4L-EMMC DTO E1 n=%u rc=%d "
	    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u\n"
	    "GTA4L-EMMC DTO E1S mb=%u sr=%d/%u/%u dr=%u/%u/%u/%u\n"
	    "GTA4L-EMMC DTO Q rc=%d\n"
	    "GTA4L-EMMC DTO E2 att=%u win=%u rc=%d tt=%u tw=%08x te=%08x "
	    "trc=%d hc=%02x\n"
	    "GTA4L-EMMC DTO PAD qdsd=%08x dll=%08x dst=%08x\n"
	    "GTA4L-EMMC DTO FAIL fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u",
	    rc, stop, host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev, (unsigned int)hci,
	    host->last_step, probe->log_count,
	    (unsigned int)dto->clk, (unsigned int)dto->normal_enable,
	    (unsigned int)dto->error_enable, (unsigned int)dto->normal_signal,
	    (unsigned int)dto->error_signal, (unsigned int)dto->error_status,
	    (unsigned int)dto->toc, (unsigned int)dto->arm,
	    (unsigned int)dto->restored,
	    dto->e1_blocks, dto->e1_result, dto->e1_fail_valid,
	    dto->e1_fail_index, dto->e1_fail_argument, dto->e1_fail_command,
	    (unsigned int)dto->e1_fail_error,
	    (unsigned int)dto->e1_fail_normal, dto->e1_fail_present,
	    dto->e1_fail_step,
	    dto->e1_multi_block, dto->e1_stop_result, dto->e1_stop_valid,
	    dto->e1_stop_capture_valid, dto->e1_drain_used,
	    dto->e1_drain_resolved, dto->e1_drain_blocks,
	    dto->e1_drain_iterations,
	    dto->q_result,
	    dto->e2_attempted, dto->e2_window, dto->e2_result,
	    dto->e2_tested, dto->e2_word, dto->e2_expected,
	    dto->e2_test_result, (unsigned int)dto->e2_host_control,
	    qdsd, dll, dst,
	    (unsigned int)host->fail_valid, host->fail_index,
	    host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

/* Stage-8 HS-timing rung capture. `hs` is the byte-185/clock enter step and
 * `revert` the return to legacy; `b2` holds the 50 MHz content pass and its
 * comparison against the 400 kHz stage-A baseline. */
struct gta4l_emmc_probe_b2 {
	struct gta4l_emmc_hs_result hs;
	struct gta4l_emmc_hs_result revert;
	struct gta4l_emmc_probe_bus content;
	uint32_t pre_hs;
	uint32_t pre_clock;
	uint32_t pre_sum;
	uint32_t pre_qdsd;
	uint32_t pre_dll;
	uint32_t pre_dst;
	uint32_t content_attempted;
	int      switch_rc;
	int      content_rc;
	int      revert_rc;
	int      q_result;
	uint32_t q_block0_sum;
};

static void
gta4l_emmc_probe_report_b2(const struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_b2 *b2, int rc, int stop)
{
	uint8_t hci = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	uint32_t qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	uint32_t dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	uint32_t dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

	panic("GTA4L-EMMC B2 REPORT: rc=%d stop=%u mci=%08x rca=%u cap=%u "
	    "rev=%u hci=%02x step=%u log=%u\n"
	    "GTA4L-EMMC B2 R clk=%04x hs=%02x es=%08x\n"
	    "GTA4L-EMMC B2 S rc=%d sw=%d c6=%08x hs=%02x clk=%04x "
	    "rcg=%08x sum=%08x\n"
	    "GTA4L-EMMC B2 T rc=%d att=%u same=%u div=%u b0=%08x b1=%08x "
	    "data=%08x last=%08x\n"
	    "GTA4L-EMMC B2 V rc=%d sw=%d hs=%02x clk=%04x rcg=%08x q=%d "
	    "qb0=%08x\n"
	    "GTA4L-EMMC B2 BLOCKS"
	    " 2=%08x 3=%08x 4=%08x 5=%08x 6=%08x 7=%08x 8=%08x 9=%08x"
	    " 10=%08x 11=%08x 12=%08x 13=%08x 14=%08x 15=%08x 16=%08x"
	    " 17=%08x 18=%08x 19=%08x 20=%08x 21=%08x 22=%08x\n"
	    "GTA4L-EMMC B2 PAD pre=%08x/%08x/%08x post=%08x/%08x/%08x\n"
	    "GTA4L-EMMC B2 FAIL fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u",
	    rc, stop, host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev, (unsigned int)hci,
	    host->last_step, probe->log_count,
	    (unsigned int)b2->pre_clock, (unsigned int)b2->pre_hs,
	    b2->pre_sum,
	    b2->switch_rc, b2->hs.switch_result, b2->hs.switch_status,
	    (unsigned int)b2->hs.hs_after, (unsigned int)b2->hs.clock,
	    b2->hs.rcg_after, b2->hs.readback_sum,
	    b2->content_rc, b2->content_attempted,
	    b2->content.same, b2->content.div,
	    b2->content.block0_sum, b2->content.block1_sum,
	    b2->content.data_sum, b2->content.last_sum,
	    b2->revert_rc, b2->revert.switch_result,
	    (unsigned int)b2->revert.hs_after,
	    (unsigned int)b2->revert.clock, b2->revert.rcg_after,
	    b2->q_result, b2->q_block0_sum,
	    b2->content.blocks[0], b2->content.blocks[1],
	    b2->content.blocks[2], b2->content.blocks[3],
	    b2->content.blocks[4], b2->content.blocks[5],
	    b2->content.blocks[6], b2->content.blocks[7],
	    b2->content.blocks[8], b2->content.blocks[9],
	    b2->content.blocks[10], b2->content.blocks[11],
	    b2->content.blocks[12], b2->content.blocks[13],
	    b2->content.blocks[14], b2->content.blocks[15],
	    b2->content.blocks[16], b2->content.blocks[17],
	    b2->content.blocks[18], b2->content.blocks[19],
	    b2->content.blocks[20],
	    b2->pre_qdsd, b2->pre_dll, b2->pre_dst, qdsd, dll, dst,
	    (unsigned int)host->fail_valid, host->fail_index,
	    host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

/* H9 stop codes: 0 clean identification with no retry, 10 retry engaged and
 * the duplicate CMD3 clean before a clean identification, 11 duplicate CMD3
 * transport failure, 12 duplicate CMD3 clean but identification failed later,
 * 13 duplicate CMD3 R1 status error, 14 no retry engaged and identification
 * did not complete cleanly (the first CMD3 either failed at a non-timeout
 * code or never ran), 15 attempt-1 CMD3 clean but identification failed
 * later. */
static int
gta4l_emmc_probe_h9_stop(const struct gta4l_emmc_host *host, int result)
{
	if (host->cmd3_attempt_count == 0u) {
		return (result == GTA4L_EMMC_OK) ? 0 : 14;
	}
	if (host->cmd3_attempt_count == 1u) {
		if (host->cmd3_attempt[0].result != GTA4L_EMMC_OK) {
			return 14;
		}
		return (result == GTA4L_EMMC_OK) ? 0 : 15;
	}
	if (host->cmd3_attempt[1].result != GTA4L_EMMC_OK) {
		return (host->cmd3_attempt[1].result ==
		    GTA4L_EMMC_ERR_CARD_STATUS) ? 13 : 11;
	}
	return (result == GTA4L_EMMC_OK) ? 10 : 12;
}

/* Report both CMD3 attempt records and the identification fields. The report
 * never samples a new MMIO register; `regs` was captured by the caller behind
 * the existing clock guard. A slot that did not run is printed with `n=0`
 * and zero fields. */
static void
gta4l_emmc_probe_report_h9(const struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_regs *regs, int rc, int stop)
{
	struct gta4l_emmc_cmd3_capture none;
	const struct gta4l_emmc_cmd3_capture *a1;
	const struct gta4l_emmc_cmd3_capture *a2;

	bzero(&none, sizeof(none));
	a1 = (host->cmd3_attempt_count >= 1u) ?
	    &host->cmd3_attempt[0] : &none;
	a2 = (host->cmd3_attempt_count >= 2u) ?
	    &host->cmd3_attempt[1] : &none;
	panic("GTA4L-EMMC H9 REPORT: rc=%d stop=%u mci=%08x rca=%u cap=%u "
	    "rev=%u bw=%u ocr=%08x ext=%08x hci=%02x retry=%u/%u att=%u "
	    "step=%u log=%u\n"
	    "GTA4L-EMMC H9 A1 n=%u rc=%d r1=%u "
	    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u "
	    "rsp=%08x,%08x,%08x,%08x\n"
	    "GTA4L-EMMC H9 A1P c3p=%u/%04x/%04x/%08x/%04x/%04x "
	    "c3t=%u/%u/%04x/%08x/%u/%08x,%08x,%08x,%08x\n"
	    "GTA4L-EMMC H9 A1L c3l=%u/%u/%04x/%04x/%u/%08x%08x/%u\n"
	    "GTA4L-EMMC H9 A2 n=%u rc=%d r1=%u "
	    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u "
	    "rsp=%08x,%08x,%08x,%08x\n"
	    "GTA4L-EMMC H9 A2P c3p=%u/%04x/%04x/%08x/%04x/%04x "
	    "c3t=%u/%u/%04x/%08x/%u/%08x,%08x,%08x,%08x\n"
	    "GTA4L-EMMC H9 A2L c3l=%u/%u/%04x/%04x/%u/%08x%08x/%u\n"
	    "GTA4L-EMMC H9 C cid=%08x,%08x,%08x,%08x cmd1=%u/%u\n"
	    "GTA4L-EMMC H9 FAIL fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u",
	    rc, stop, host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev, (unsigned int)host->bus_width,
	    host->ocr, host->ext_csd_sum, (unsigned int)regs->host_control,
	    (unsigned int)host->cmd3_retry_engaged,
	    (unsigned int)host->cmd3_retry_ok,
	    (unsigned int)host->cmd3_attempt_count,
	    host->last_step, probe->log_count,
	    a1->number, a1->result, a1->r1_error,
	    a1->fail_valid, a1->fail_index, a1->fail_argument,
	    a1->fail_command, (unsigned int)a1->fail_error,
	    (unsigned int)a1->fail_normal, a1->fail_present,
	    a1->fail_step, a1->fail_response[0], a1->fail_response[1],
	    a1->fail_response[2], a1->fail_response[3],
	    a1->pre_valid, (unsigned int)a1->pre_normal,
	    (unsigned int)a1->pre_error, a1->pre_present,
	    (unsigned int)a1->pre_normal_enable,
	    (unsigned int)a1->pre_error_enable,
	    a1->timeout_valid, a1->timeout_index,
	    (unsigned int)a1->timeout_command, a1->timeout_argument,
	    a1->timeout_response_stable, a1->timeout_response[0],
	    a1->timeout_response[1], a1->timeout_response[2],
	    a1->timeout_response[3],
	    a1->poll_valid, a1->poll_index,
	    (unsigned int)a1->poll_first_normal,
	    (unsigned int)a1->poll_last_normal,
	    a1->poll_iterations,
	    (unsigned int)(a1->poll_ticks >> 32),
	    (unsigned int)a1->poll_ticks,
	    (unsigned int)probe->platform.counter_hz,
	    a2->number, a2->result, a2->r1_error,
	    a2->fail_valid, a2->fail_index, a2->fail_argument,
	    a2->fail_command, (unsigned int)a2->fail_error,
	    (unsigned int)a2->fail_normal, a2->fail_present,
	    a2->fail_step, a2->fail_response[0], a2->fail_response[1],
	    a2->fail_response[2], a2->fail_response[3],
	    a2->pre_valid, (unsigned int)a2->pre_normal,
	    (unsigned int)a2->pre_error, a2->pre_present,
	    (unsigned int)a2->pre_normal_enable,
	    (unsigned int)a2->pre_error_enable,
	    a2->timeout_valid, a2->timeout_index,
	    (unsigned int)a2->timeout_command, a2->timeout_argument,
	    a2->timeout_response_stable, a2->timeout_response[0],
	    a2->timeout_response[1], a2->timeout_response[2],
	    a2->timeout_response[3],
	    a2->poll_valid, a2->poll_index,
	    (unsigned int)a2->poll_first_normal,
	    (unsigned int)a2->poll_last_normal,
	    a2->poll_iterations,
	    (unsigned int)(a2->poll_ticks >> 32),
	    (unsigned int)a2->poll_ticks,
	    (unsigned int)probe->platform.counter_hz,
	    host->cid[0], host->cid[1], host->cid[2], host->cid[3],
	    host->cmd1_attempts, host->cmd1_timeouts,
	    (unsigned int)host->fail_valid, host->fail_index,
	    host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

/* Run the stage-8 HS-timing rung: sample the 400 kHz state (R), switch
 * byte 185 to high speed and ramp to the 50 MHz HS row (S), repeat the full
 * stage-A content pass at HS and compare it with the 400 kHz baseline (T),
 * then drop the clock and clear byte 185 before a block-0 recovery read (V).
 * Report and stop in every case; no retry on device. Stop codes: 0 pass,
 * 10 switch/byte-185 failure, 11 clock failure, 12 content read failure or
 * divergence, 13 revert failure, 14 recovery-read failure. */
static void
gta4l_emmc_probe_b2_run(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe, struct gta4l_emmc_probe_range *range,
    const uint8_t *block0, const uint8_t *block1)
{
	static uint8_t qblock[GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_probe_bus base;
	struct gta4l_emmc_probe_b2 b2;
	uint8_t ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t index;
	uint32_t sum;
	int rc;

	bzero(&base, sizeof(base));
	bzero(&b2, sizeof(b2));
	base.block0_sum = gta4l_emmc_probe_sum(block0,
	    GTA4L_EMMC_BLOCK_BYTES);
	base.block1_sum = gta4l_emmc_probe_sum(block1,
	    GTA4L_EMMC_BLOCK_BYTES);
	for (index = 0; index < GTA4L_EMMC_PROBE_ARRAY_BLOCKS; index++) {
		base.blocks[index] = range->array[index];
	}
	base.data_sum = range->data_sum;
	base.last_sum = range->last_sum;

	b2.pre_clock = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CLOCK_CONTROL);
	b2.pre_qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	b2.pre_dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	b2.pre_dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);
	rc = gta4l_emmc_read_ext_csd(host, ext_csd);
	if (rc != GTA4L_EMMC_OK) {
		b2.switch_rc = rc;
		gta4l_emmc_probe_report_b2(host, probe, &b2,
		    rc, 10);
	}
	b2.pre_hs = ext_csd[GTA4L_EMMC_EXT_CSD_HS_TIMING_OFF];
	for (index = 0, sum = 0; index < GTA4L_EMMC_EXT_CSD_BYTES; index++) {
		sum += ext_csd[index];
	}
	b2.pre_sum = sum;

	b2.switch_rc = gta4l_emmc_set_hs_timing(host, 1u, &b2.hs);
	if (b2.switch_rc != GTA4L_EMMC_OK) {
		b2.revert_rc = gta4l_emmc_set_hs_timing(host, 0u,
		    &b2.revert);
		gta4l_emmc_probe_report_b2(host, probe, &b2,
		    b2.switch_rc,
		    (b2.hs.clock_result != GTA4L_EMMC_OK) ? 11 : 10);
	}

	b2.content_attempted = 1u;
	b2.content_rc = gta4l_emmc_probe_b1_content(host, range,
	    &b2.content, 0);
	if (b2.content_rc == GTA4L_EMMC_OK) {
		gta4l_emmc_probe_b1_compare(range, &base, &b2.content);
	}
	if ((b2.content_rc != GTA4L_EMMC_OK) ||
	    (b2.content.same == 0u)) {
		b2.revert_rc = gta4l_emmc_set_hs_timing(host, 0u,
		    &b2.revert);
		gta4l_emmc_probe_report_b2(host, probe, &b2,
		    (b2.content_rc != GTA4L_EMMC_OK) ? b2.content_rc :
		    GTA4L_EMMC_PROBE_ERR_CONTENT,
		    (b2.revert_rc != GTA4L_EMMC_OK) ? 13 : 12);
	}

	b2.revert_rc = gta4l_emmc_set_hs_timing(host, 0u, &b2.revert);
	if (b2.revert_rc != GTA4L_EMMC_OK) {
		gta4l_emmc_probe_report_b2(host, probe, &b2,
		    b2.revert_rc, 13);
	}

	b2.q_result = gta4l_emmc_read_block(host, 0u, qblock);
	if (b2.q_result != GTA4L_EMMC_OK) {
		gta4l_emmc_probe_report_b2(host, probe, &b2,
		    b2.q_result, 14);
	}
	b2.q_block0_sum = gta4l_emmc_probe_sum(qblock,
	    GTA4L_EMMC_BLOCK_BYTES);
	if (b2.q_block0_sum != base.block0_sum) {
		gta4l_emmc_probe_report_b2(host, probe, &b2,
		    GTA4L_EMMC_PROBE_ERR_CONTENT, 14);
	}
	gta4l_emmc_probe_report_b2(host, probe, &b2,
	    GTA4L_EMMC_OK, 0);
}

/* H10 width-at-HS continuation capture. R holds the 400 kHz identity
 * samples, S the HS entry, W the 4-bit BUS_WIDTH switch at HS, B the
 * CMD19/CMD14 round trip at 4-bit, T the content pass at 4-bit/HS, V the
 * width revert, X the HS revert and Q the recovery read. Every value is an
 * observed sample. */
struct gta4l_emmc_probe_h10 {
	struct gta4l_emmc_hs_result hs;
	struct gta4l_emmc_hs_result hs_revert;
	struct gta4l_emmc_bus_result width;
	struct gta4l_emmc_bus_result width_revert;
	struct gta4l_emmc_bus_result bus_test;
	struct gta4l_emmc_probe_bus content;
	uint32_t pre_hs;
	uint32_t pre_width;
	uint32_t pre_clock;
	uint32_t pre_hc;
	uint32_t pre_sum;
	uint32_t pre_qdsd;
	uint32_t pre_dll;
	uint32_t pre_dst;
	uint32_t content_attempted;
	uint32_t tested;
	int      switch_rc;
	int      width_rc;
	int      content_rc;
	int      width_revert_rc;
	int      hs_revert_rc;
	int      q_result;
	uint32_t q_block0_sum;
};

static void
gta4l_emmc_probe_report_h10(const struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_h10 *h10, int rc, int stop)
{
	uint8_t hci = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	uint32_t qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	uint32_t dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	uint32_t dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

	panic("GTA4L-EMMC H10 REPORT: rc=%d stop=%u mci=%08x rca=%u cap=%u "
	    "rev=%u hci=%02x step=%u log=%u bw=%02x\n"
	    "GTA4L-EMMC H10 R clk=%04x hs=%02x bw=%02x hc=%02x es=%08x\n"
	    "GTA4L-EMMC H10 S rc=%d sw=%d c6=%08x hs=%02x clk=%04x "
	    "rcg=%08x sum=%08x\n"
	    "GTA4L-EMMC H10 W rc=%d sw=%u swrc=%d c6=%08x hb=%02x "
	    "ha=%02x es=%08x\n"
	    "GTA4L-EMMC H10 B rc=%d tt=%u trc=%d tw=%08x te=%08x\n"
	    "GTA4L-EMMC H10 T rc=%d att=%u same=%u div=%u b0=%08x "
	    "b1=%08x data=%08x last=%08x\n"
	    "GTA4L-EMMC H10 V rc=%d sw=%u swrc=%d c6=%08x hb=%02x "
	    "ha=%02x es=%08x\n"
	    "GTA4L-EMMC H10 X rc=%d sw=%d c6=%08x hs=%02x clk=%04x "
	    "rcg=%08x sum=%08x\n"
	    "GTA4L-EMMC H10 Q rc=%d qb0=%08x\n"
	    "GTA4L-EMMC H10 BLOCKS"
	    " 2=%08x 3=%08x 4=%08x 5=%08x 6=%08x 7=%08x 8=%08x 9=%08x"
	    " 10=%08x 11=%08x 12=%08x 13=%08x 14=%08x 15=%08x 16=%08x"
	    " 17=%08x 18=%08x 19=%08x 20=%08x 21=%08x 22=%08x\n"
	    "GTA4L-EMMC H10 PAD pre=%08x/%08x/%08x post=%08x/%08x/%08x\n"
	    "GTA4L-EMMC H10 FAIL fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u",
	    rc, stop, host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev, (unsigned int)hci,
	    host->last_step, probe->log_count,
	    (unsigned int)host->bus_width,
	    (unsigned int)h10->pre_clock, (unsigned int)h10->pre_hs,
	    (unsigned int)h10->pre_width, (unsigned int)h10->pre_hc,
	    h10->pre_sum,
	    h10->switch_rc, h10->hs.switch_result,
	    h10->hs.switch_status,
	    (unsigned int)h10->hs.hs_after, (unsigned int)h10->hs.clock,
	    h10->hs.rcg_after, h10->hs.readback_sum,
	    h10->width_rc, h10->width.switched, h10->width.switch_result,
	    h10->width.switch_status,
	    (unsigned int)h10->width.host_before,
	    (unsigned int)h10->width.host_after,
	    h10->width.readback_sum,
	    h10->bus_test.test_result, h10->tested,
	    h10->bus_test.test_result, h10->bus_test.test_word,
	    h10->bus_test.test_expected,
	    h10->content_rc, h10->content_attempted,
	    h10->content.same, h10->content.div,
	    h10->content.block0_sum, h10->content.block1_sum,
	    h10->content.data_sum, h10->content.last_sum,
	    h10->width_revert_rc, h10->width_revert.switched,
	    h10->width_revert.switch_result,
	    h10->width_revert.switch_status,
	    (unsigned int)h10->width_revert.host_before,
	    (unsigned int)h10->width_revert.host_after,
	    h10->width_revert.readback_sum,
	    h10->hs_revert_rc, h10->hs_revert.switch_result,
	    h10->hs_revert.switch_status,
	    (unsigned int)h10->hs_revert.hs_after,
	    (unsigned int)h10->hs_revert.clock,
	    h10->hs_revert.rcg_after, h10->hs_revert.readback_sum,
	    h10->q_result, h10->q_block0_sum,
	    h10->content.blocks[0], h10->content.blocks[1],
	    h10->content.blocks[2], h10->content.blocks[3],
	    h10->content.blocks[4], h10->content.blocks[5],
	    h10->content.blocks[6], h10->content.blocks[7],
	    h10->content.blocks[8], h10->content.blocks[9],
	    h10->content.blocks[10], h10->content.blocks[11],
	    h10->content.blocks[12], h10->content.blocks[13],
	    h10->content.blocks[14], h10->content.blocks[15],
	    h10->content.blocks[16], h10->content.blocks[17],
	    h10->content.blocks[18], h10->content.blocks[19],
	    h10->content.blocks[20],
	    h10->pre_qdsd, h10->pre_dll, h10->pre_dst, qdsd, dll, dst,
	    (unsigned int)host->fail_valid, host->fail_index,
	    host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

/* Revert the width at HS and then the timing class. Best effort: the HS
 * revert runs even when the width revert fails. Returns 0, 15 (width
 * revert) or 16 (HS revert). */
static int
gta4l_emmc_probe_h10_unwind(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe_h10 *h10)
{
	h10->width_revert_rc = gta4l_emmc_set_bus_width(host, 1u,
	    &h10->width_revert);
	h10->hs_revert_rc = gta4l_emmc_set_hs_timing(host, 0u,
	    &h10->hs_revert);
	if (h10->width_revert_rc != GTA4L_EMMC_OK) {
		return 15;
	}
	if (h10->hs_revert_rc != GTA4L_EMMC_OK) {
		return 16;
	}
	return 0;
}

/* Run the stage-10 width-at-HS continuation: stage-A at 400 kHz/1-bit is
 * the baseline, then HS entry, the 4-bit switch at HS, one CMD19/CMD14
 * round trip, the stage-A content pass at 4-bit/HS, the reverts and the
 * recovery read. Report and stop in every case; no 8-bit step, no retry. */
static void
gta4l_emmc_probe_h10_run(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe, struct gta4l_emmc_probe_range *range,
    const uint8_t *block0, const uint8_t *block1)
{
	static uint8_t qblock[GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_probe_bus base;
	struct gta4l_emmc_probe_h10 h10;
	uint8_t ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t index;
	uint32_t sum;
	int unwind;
	int rc;

	bzero(&base, sizeof(base));
	bzero(&h10, sizeof(h10));
	base.block0_sum = gta4l_emmc_probe_sum(block0,
	    GTA4L_EMMC_BLOCK_BYTES);
	base.block1_sum = gta4l_emmc_probe_sum(block1,
	    GTA4L_EMMC_BLOCK_BYTES);
	for (index = 0; index < GTA4L_EMMC_PROBE_ARRAY_BLOCKS; index++) {
		base.blocks[index] = range->array[index];
	}
	base.data_sum = range->data_sum;
	base.last_sum = range->last_sum;

	h10.pre_clock = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CLOCK_CONTROL);
	h10.pre_hc = gta4l_emmc_probe_read8(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_CONTROL);
	h10.pre_qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	h10.pre_dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	h10.pre_dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);
	rc = gta4l_emmc_read_ext_csd(host, ext_csd);
	if (rc != GTA4L_EMMC_OK) {
		h10.switch_rc = rc;
		gta4l_emmc_probe_report_h10(host, probe, &h10, rc, 10);
	}
	h10.pre_hs = ext_csd[GTA4L_EMMC_EXT_CSD_HS_TIMING_OFF];
	h10.pre_width = ext_csd[GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF];
	for (index = 0, sum = 0; index < GTA4L_EMMC_EXT_CSD_BYTES;
	    index++) {
		sum += ext_csd[index];
	}
	h10.pre_sum = sum;

	h10.switch_rc = gta4l_emmc_set_hs_timing(host, 1u, &h10.hs);
	if (h10.switch_rc != GTA4L_EMMC_OK) {
		h10.hs_revert_rc = gta4l_emmc_set_hs_timing(host, 0u,
		    &h10.hs_revert);
		gta4l_emmc_probe_report_h10(host, probe, &h10,
		    h10.switch_rc,
		    (h10.hs.clock_result != GTA4L_EMMC_OK) ? 11 : 10);
	}

	h10.width_rc = gta4l_emmc_set_bus_width(host, 4u, &h10.width);
	if (h10.width_rc != GTA4L_EMMC_OK) {
		unwind = gta4l_emmc_probe_h10_unwind(host, &h10);
		gta4l_emmc_probe_report_h10(host, probe, &h10,
		    h10.width_rc, (unwind != 0) ? unwind : 12);
	}

	h10.tested = 1u;
	rc = gta4l_emmc_bus_test(host, 4u, &h10.bus_test);
	if ((rc != GTA4L_EMMC_OK) &&
	    (h10.bus_test.test_result == GTA4L_EMMC_OK)) {
		h10.bus_test.test_result = rc;
	}
	if (rc != GTA4L_EMMC_OK) {
		/* The CMD19 wedge leaves the controller inhibited; the B1C
		 * evidence stopped at the same point without issuing further
		 * commands, so the width/timing reverts are recorded as
		 * not-run and the panic reboot resets the card. */
		gta4l_emmc_probe_report_h10(host, probe, &h10, rc, 13);
	}

	h10.content_attempted = 1u;
	h10.content_rc = gta4l_emmc_probe_b1_content(host, range,
	    &h10.content, 0);
	if (h10.content_rc == GTA4L_EMMC_OK) {
		gta4l_emmc_probe_b1_compare(range, &base, &h10.content);
	}
	unwind = gta4l_emmc_probe_h10_unwind(host, &h10);
	if (h10.content_rc != GTA4L_EMMC_OK) {
		gta4l_emmc_probe_report_h10(host, probe, &h10,
		    h10.content_rc, (unwind != 0) ? unwind : 14);
	}
	if (h10.content.same == 0u) {
		gta4l_emmc_probe_report_h10(host, probe, &h10,
		    GTA4L_EMMC_PROBE_ERR_CONTENT,
		    (unwind != 0) ? unwind : 14);
	}
	if (unwind != 0) {
		gta4l_emmc_probe_report_h10(host, probe, &h10,
		    (unwind == 15) ? h10.width_revert_rc :
		    h10.hs_revert_rc, unwind);
	}

	h10.q_result = gta4l_emmc_read_block(host, 0u, qblock);
	if (h10.q_result != GTA4L_EMMC_OK) {
		gta4l_emmc_probe_report_h10(host, probe, &h10,
		    h10.q_result, 17);
	}
	h10.q_block0_sum = gta4l_emmc_probe_sum(qblock,
	    GTA4L_EMMC_BLOCK_BYTES);
	if (h10.q_block0_sum != base.block0_sum) {
		gta4l_emmc_probe_report_h10(host, probe, &h10,
		    GTA4L_EMMC_PROBE_ERR_CONTENT, 17);
	}
	gta4l_emmc_probe_report_h10(host, probe, &h10,
	    GTA4L_EMMC_OK, 0);
}

/* H11 width-value continuation capture. R holds the 400 kHz identity
 * samples, S the HS entry, W4 the record-only 4-bit replay at HS, W8 the
 * 8-bit attempt at HS, B the CMD19/CMD14 round trip at the width W8
 * produced, T the content pass at 8-bit/HS, V the width revert, X the HS
 * revert and Q the recovery read. Every value is an observed sample. */
struct gta4l_emmc_probe_h11 {
	struct gta4l_emmc_hs_result hs;
	struct gta4l_emmc_hs_result hs_revert;
	struct gta4l_emmc_bus_result w4;
	struct gta4l_emmc_bus_result w8;
	struct gta4l_emmc_bus_result width_revert;
	struct gta4l_emmc_bus_result bus_test;
	struct gta4l_emmc_probe_bus content;
	uint32_t pre_hs;
	uint32_t pre_width;
	uint32_t pre_clock;
	uint32_t pre_hc;
	uint32_t pre_sum;
	uint32_t pre_qdsd;
	uint32_t pre_dll;
	uint32_t pre_dst;
	uint32_t content_attempted;
	uint32_t tested;
	int      switch_rc;
	int      w4_rc;
	int      w8_rc;
	int      content_rc;
	int      width_revert_rc;
	int      hs_revert_rc;
	int      q_result;
	uint32_t q_block0_sum;
};

static void
gta4l_emmc_probe_report_h11(const struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_h11 *h11, int rc, int stop)
{
	uint8_t hci = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	uint32_t qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	uint32_t dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	uint32_t dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

	panic("GTA4L-EMMC H11 REPORT: rc=%d stop=%u mci=%08x rca=%u cap=%u "
	    "rev=%u hci=%02x step=%u log=%u bw=%02x\n"
	    "GTA4L-EMMC H11 R clk=%04x hs=%02x bw=%02x hc=%02x es=%08x\n"
	    "GTA4L-EMMC H11 S rc=%d sw=%d c6=%08x hs=%02x clk=%04x "
	    "rcg=%08x sum=%08x\n"
	    "GTA4L-EMMC H11 W4 rc=%d sw=%u swrc=%d c6=%08x hb=%02x "
	    "ha=%02x es=%08x\n"
	    "GTA4L-EMMC H11 W8 rc=%d sw=%u swrc=%d c6=%08x hb=%02x "
	    "ha=%02x es=%08x\n"
	    "GTA4L-EMMC H11 B rc=%d tt=%u trc=%d tw=%08x te=%08x\n"
	    "GTA4L-EMMC H11 T rc=%d att=%u same=%u div=%u b0=%08x "
	    "b1=%08x data=%08x last=%08x\n"
	    "GTA4L-EMMC H11 V rc=%d sw=%u swrc=%d c6=%08x hb=%02x "
	    "ha=%02x es=%08x\n"
	    "GTA4L-EMMC H11 X rc=%d sw=%d c6=%08x hs=%02x clk=%04x "
	    "rcg=%08x sum=%08x\n"
	    "GTA4L-EMMC H11 Q rc=%d qb0=%08x\n"
	    "GTA4L-EMMC H11 BLOCKS"
	    " 2=%08x 3=%08x 4=%08x 5=%08x 6=%08x 7=%08x 8=%08x 9=%08x"
	    " 10=%08x 11=%08x 12=%08x 13=%08x 14=%08x 15=%08x 16=%08x"
	    " 17=%08x 18=%08x 19=%08x 20=%08x 21=%08x 22=%08x\n"
	    "GTA4L-EMMC H11 PAD pre=%08x/%08x/%08x post=%08x/%08x/%08x\n"
	    "GTA4L-EMMC H11 FAIL fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u",
	    rc, stop, host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev, (unsigned int)hci,
	    host->last_step, probe->log_count,
	    (unsigned int)host->bus_width,
	    (unsigned int)h11->pre_clock, (unsigned int)h11->pre_hs,
	    (unsigned int)h11->pre_width, (unsigned int)h11->pre_hc,
	    h11->pre_sum,
	    h11->switch_rc, h11->hs.switch_result,
	    h11->hs.switch_status,
	    (unsigned int)h11->hs.hs_after, (unsigned int)h11->hs.clock,
	    h11->hs.rcg_after, h11->hs.readback_sum,
	    h11->w4_rc, h11->w4.switched, h11->w4.switch_result,
	    h11->w4.switch_status,
	    (unsigned int)h11->w4.host_before,
	    (unsigned int)h11->w4.host_after,
	    h11->w4.readback_sum,
	    h11->w8_rc, h11->w8.switched, h11->w8.switch_result,
	    h11->w8.switch_status,
	    (unsigned int)h11->w8.host_before,
	    (unsigned int)h11->w8.host_after,
	    h11->w8.readback_sum,
	    h11->bus_test.test_result, h11->tested,
	    h11->bus_test.test_result, h11->bus_test.test_word,
	    h11->bus_test.test_expected,
	    h11->content_rc, h11->content_attempted,
	    h11->content.same, h11->content.div,
	    h11->content.block0_sum, h11->content.block1_sum,
	    h11->content.data_sum, h11->content.last_sum,
	    h11->width_revert_rc, h11->width_revert.switched,
	    h11->width_revert.switch_result,
	    h11->width_revert.switch_status,
	    (unsigned int)h11->width_revert.host_before,
	    (unsigned int)h11->width_revert.host_after,
	    h11->width_revert.readback_sum,
	    h11->hs_revert_rc, h11->hs_revert.switch_result,
	    h11->hs_revert.switch_status,
	    (unsigned int)h11->hs_revert.hs_after,
	    (unsigned int)h11->hs_revert.clock,
	    h11->hs_revert.rcg_after, h11->hs_revert.readback_sum,
	    h11->q_result, h11->q_block0_sum,
	    h11->content.blocks[0], h11->content.blocks[1],
	    h11->content.blocks[2], h11->content.blocks[3],
	    h11->content.blocks[4], h11->content.blocks[5],
	    h11->content.blocks[6], h11->content.blocks[7],
	    h11->content.blocks[8], h11->content.blocks[9],
	    h11->content.blocks[10], h11->content.blocks[11],
	    h11->content.blocks[12], h11->content.blocks[13],
	    h11->content.blocks[14], h11->content.blocks[15],
	    h11->content.blocks[16], h11->content.blocks[17],
	    h11->content.blocks[18], h11->content.blocks[19],
	    h11->content.blocks[20],
	    h11->pre_qdsd, h11->pre_dll, h11->pre_dst, qdsd, dll, dst,
	    (unsigned int)host->fail_valid, host->fail_index,
	    host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

/* H11 clean-shape predicate: the width step's EXT_CSD re-read must be
 * non-empty and its byte sum must agree with the byte-183 code the same read
 * returned. `pre_sum` is the 400 kHz/1-bit sum read before the HS entry, and
 * the HS entry adds exactly one (byte 185 = 1). */
static int
gta4l_emmc_probe_h11_coherent(const struct gta4l_emmc_bus_result *width,
    uint32_t pre_sum)
{
	if (width->readback_sum == 0u) {
		return 0;
	}
	return (width->readback_sum == pre_sum + 1u + width->switched) ? 1 : 0;
}

/* Revert the width at HS and then the timing class. Best effort: the HS
 * revert runs even when the width revert fails. Returns 0, 15 (width
 * revert) or 16 (HS revert). */
static int
gta4l_emmc_probe_h11_unwind(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe_h11 *h11)
{
	h11->width_revert_rc = gta4l_emmc_set_bus_width(host, 1u,
	    &h11->width_revert);
	h11->hs_revert_rc = gta4l_emmc_set_hs_timing(host, 0u,
	    &h11->hs_revert);
	if (h11->width_revert_rc != GTA4L_EMMC_OK) {
		return 15;
	}
	if (h11->hs_revert_rc != GTA4L_EMMC_OK) {
		return 16;
	}
	return 0;
}

/* Run the stage-11 width-value continuation: stage-A at 400 kHz/1-bit is
 * the baseline, then HS entry, the record-only 4-bit replay, the 8-bit
 * attempt, one CMD19/CMD14 round trip and the stage-A content pass at
 * 8-bit/HS only when the 8-bit switch is honored, the reverts and the
 * recovery read. Report and stop in every case; no retry. */
static void
gta4l_emmc_probe_h11_run(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe, struct gta4l_emmc_probe_range *range,
    const uint8_t *block0, const uint8_t *block1)
{
	static uint8_t qblock[GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_probe_bus base;
	struct gta4l_emmc_probe_h11 h11;
	uint8_t ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t index;
	uint32_t sum;
	int unwind;
	int rc;

	bzero(&base, sizeof(base));
	bzero(&h11, sizeof(h11));
	base.block0_sum = gta4l_emmc_probe_sum(block0,
	    GTA4L_EMMC_BLOCK_BYTES);
	base.block1_sum = gta4l_emmc_probe_sum(block1,
	    GTA4L_EMMC_BLOCK_BYTES);
	for (index = 0; index < GTA4L_EMMC_PROBE_ARRAY_BLOCKS; index++) {
		base.blocks[index] = range->array[index];
	}
	base.data_sum = range->data_sum;
	base.last_sum = range->last_sum;

	h11.pre_clock = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CLOCK_CONTROL);
	h11.pre_hc = gta4l_emmc_probe_read8(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_CONTROL);
	h11.pre_qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	h11.pre_dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	h11.pre_dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);
	rc = gta4l_emmc_read_ext_csd(host, ext_csd);
	if (rc != GTA4L_EMMC_OK) {
		h11.switch_rc = rc;
		gta4l_emmc_probe_report_h11(host, probe, &h11, rc, 10);
	}
	h11.pre_hs = ext_csd[GTA4L_EMMC_EXT_CSD_HS_TIMING_OFF];
	h11.pre_width = ext_csd[GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF];
	for (index = 0, sum = 0; index < GTA4L_EMMC_EXT_CSD_BYTES;
	    index++) {
		sum += ext_csd[index];
	}
	h11.pre_sum = sum;

	h11.switch_rc = gta4l_emmc_set_hs_timing(host, 1u, &h11.hs);
	if (h11.switch_rc != GTA4L_EMMC_OK) {
		h11.hs_revert_rc = gta4l_emmc_set_hs_timing(host, 0u,
		    &h11.hs_revert);
		gta4l_emmc_probe_report_h11(host, probe, &h11,
		    h11.switch_rc,
		    (h11.hs.clock_result != GTA4L_EMMC_OK) ? 11 : 10);
	}

	/* W4: replay the H10 4-bit switch as a record-only sample. A
	 * clean-shape tuple match or mismatch is only a verdict input; only
	 * a true anomaly (a CMD6/R1 or transport error, or an empty
	 * EXT_CSD re-read) stops before W8. */
	h11.w4_rc = gta4l_emmc_set_bus_width(host, 4u, &h11.w4);
	if (((h11.w4_rc != GTA4L_EMMC_OK) &&
	    (h11.w4_rc != GTA4L_EMMC_ERR_BUS_WIDTH)) ||
	    ((h11.w4.switch_result != GTA4L_EMMC_OK) &&
	    (h11.w4.switch_result != GTA4L_EMMC_ERR_BUS_WIDTH)) ||
	    (h11.w4.readback_sum == 0u)) {
		if ((h11.w4_rc == GTA4L_EMMC_OK) &&
		    (h11.w4.readback_sum == 0u)) {
			h11.w4_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
		}
		unwind = gta4l_emmc_probe_h11_unwind(host, &h11);
		gta4l_emmc_probe_report_h11(host, probe, &h11,
		    h11.w4_rc, (unwind != 0) ? unwind : 18);
	}

	/* W8: the 8-bit attempt at HS. B/T/(V/X/Q) run only when the switch
	 * was honored and the re-read agrees with it; a readback that
	 * disagrees with its own sum is the byte-183 paradox row, recorded
	 * and stopped after the reverts. */
	h11.w8_rc = gta4l_emmc_set_bus_width(host, 8u, &h11.w8);
	if ((h11.w8_rc == GTA4L_EMMC_OK) &&
	    (gta4l_emmc_probe_h11_coherent(&h11.w8, h11.pre_sum) != 0)) {
		h11.tested = 1u;
		rc = gta4l_emmc_bus_test(host, 8u, &h11.bus_test);
		if ((rc != GTA4L_EMMC_OK) &&
		    (h11.bus_test.test_result == GTA4L_EMMC_OK)) {
			h11.bus_test.test_result = rc;
		}
		if (rc != GTA4L_EMMC_OK) {
			/* The CMD19 wedge leaves the controller inhibited; as
			 * in H10 the width/timing reverts are recorded as
			 * not-run and the panic reboot resets the card. */
			gta4l_emmc_probe_report_h11(host, probe, &h11, rc, 13);
		}

		h11.content_attempted = 1u;
		h11.content_rc = gta4l_emmc_probe_b1_content(host, range,
		    &h11.content, 0);
		if (h11.content_rc == GTA4L_EMMC_OK) {
			gta4l_emmc_probe_b1_compare(range, &base,
			    &h11.content);
		}
		unwind = gta4l_emmc_probe_h11_unwind(host, &h11);
		if (h11.content_rc != GTA4L_EMMC_OK) {
			gta4l_emmc_probe_report_h11(host, probe, &h11,
			    h11.content_rc, (unwind != 0) ? unwind : 14);
		}
		if (h11.content.same == 0u) {
			gta4l_emmc_probe_report_h11(host, probe, &h11,
			    GTA4L_EMMC_PROBE_ERR_CONTENT,
			    (unwind != 0) ? unwind : 14);
		}
		if (unwind != 0) {
			gta4l_emmc_probe_report_h11(host, probe, &h11,
			    (unwind == 15) ? h11.width_revert_rc :
			    h11.hs_revert_rc, unwind);
		}

		h11.q_result = gta4l_emmc_read_block(host, 0u, qblock);
		if (h11.q_result != GTA4L_EMMC_OK) {
			gta4l_emmc_probe_report_h11(host, probe, &h11,
			    h11.q_result, 17);
		}
		h11.q_block0_sum = gta4l_emmc_probe_sum(qblock,
		    GTA4L_EMMC_BLOCK_BYTES);
		if (h11.q_block0_sum != base.block0_sum) {
			gta4l_emmc_probe_report_h11(host, probe, &h11,
			    GTA4L_EMMC_PROBE_ERR_CONTENT, 17);
		}
		gta4l_emmc_probe_report_h11(host, probe, &h11,
		    GTA4L_EMMC_OK, 0);
	}

	/* W8 ignored or anomalous: no B/T on an unproven framing. Revert
	 * both and stop in the H10 failure-path shape. A readback word that
	 * disagrees with its own re-read sum is the byte-183 paradox row and
	 * is reported with the width-mismatch result. */
	if ((h11.w8_rc == GTA4L_EMMC_OK) &&
	    (gta4l_emmc_probe_h11_coherent(&h11.w8, h11.pre_sum) == 0)) {
		h11.w8_rc = GTA4L_EMMC_ERR_BUS_WIDTH;
	}
	unwind = gta4l_emmc_probe_h11_unwind(host, &h11);
	gta4l_emmc_probe_report_h11(host, probe, &h11,
	    h11.w8_rc, (unwind != 0) ? unwind : 12);
}

/* H12 record-only DDR-width capture. R holds the 400 kHz identity samples,
 * S the HS entry, WDDR the record-only CMD6 SET byte 183 = 4 attempt and
 * its verification re-read, VR the width revert, X the HS revert and Q the
 * recovery read. The controller DDR configuration is never touched. */
struct gta4l_emmc_probe_h12 {
	struct gta4l_emmc_hs_result hs;
	struct gta4l_emmc_hs_result hs_revert;
	struct gta4l_emmc_bus_result wddr;
	struct gta4l_emmc_bus_result width_revert;
	uint32_t pre_hs;
	uint32_t pre_width;
	uint32_t pre_clock;
	uint32_t pre_hc;
	uint32_t pre_sum;
	uint32_t pre_qdsd;
	uint32_t pre_dll;
	uint32_t pre_dst;
	int      switch_rc;
	int      wddr_rc;
	int      width_revert_rc;
	int      hs_revert_rc;
	int      q_result;
	uint32_t q_block0_sum;
};

static void
gta4l_emmc_probe_report_h12(const struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_h12 *h12, int rc, int stop)
{
	uint8_t hci = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	uint32_t qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	uint32_t dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	uint32_t dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

	panic("GTA4L-EMMC H12 REPORT: rc=%d stop=%u mci=%08x rca=%u cap=%u "
	    "rev=%u hci=%02x step=%u log=%u bw=%02x\n"
	    "GTA4L-EMMC H12 R clk=%04x hs=%02x bw=%02x hc=%02x es=%08x\n"
	    "GTA4L-EMMC H12 S rc=%d sw=%d c6=%08x hs=%02x clk=%04x "
	    "rcg=%08x sum=%08x\n"
	    "GTA4L-EMMC H12 WDDR rc=%d sw=%u swrc=%d c6=%08x es=%08x\n"
	    "GTA4L-EMMC H12 VR rc=%d sw=%u swrc=%d c6=%08x hb=%02x "
	    "ha=%02x es=%08x\n"
	    "GTA4L-EMMC H12 X rc=%d sw=%d c6=%08x hs=%02x clk=%04x "
	    "rcg=%08x sum=%08x\n"
	    "GTA4L-EMMC H12 Q rc=%d qb0=%08x\n"
	    "GTA4L-EMMC H12 PAD pre=%08x/%08x/%08x post=%08x/%08x/%08x\n"
	    "GTA4L-EMMC H12 FAIL fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u",
	    rc, stop, host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev, (unsigned int)hci,
	    host->last_step, probe->log_count,
	    (unsigned int)host->bus_width,
	    (unsigned int)h12->pre_clock, (unsigned int)h12->pre_hs,
	    (unsigned int)h12->pre_width, (unsigned int)h12->pre_hc,
	    h12->pre_sum,
	    h12->switch_rc, h12->hs.switch_result,
	    h12->hs.switch_status,
	    (unsigned int)h12->hs.hs_after, (unsigned int)h12->hs.clock,
	    h12->hs.rcg_after, h12->hs.readback_sum,
	    h12->wddr_rc, h12->wddr.switched, h12->wddr.switch_result,
	    h12->wddr.switch_status, h12->wddr.readback_sum,
	    h12->width_revert_rc, h12->width_revert.switched,
	    h12->width_revert.switch_result,
	    h12->width_revert.switch_status,
	    (unsigned int)h12->width_revert.host_before,
	    (unsigned int)h12->width_revert.host_after,
	    h12->width_revert.readback_sum,
	    h12->hs_revert_rc, h12->hs_revert.switch_result,
	    h12->hs_revert.switch_status,
	    (unsigned int)h12->hs_revert.hs_after,
	    (unsigned int)h12->hs_revert.clock,
	    h12->hs_revert.rcg_after, h12->hs_revert.readback_sum,
	    h12->q_result, h12->q_block0_sum,
	    h12->pre_qdsd, h12->pre_dll, h12->pre_dst, qdsd, dll, dst,
	    (unsigned int)host->fail_valid, host->fail_index,
	    host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

/* H12 outcome classes: 0 = clean refusal (byte 183 stays 0 and the sum is
 * the 1-bit-at-HS sum), 1 = honored (byte 183 = 4 and the sum grew by 4),
 * 2 = a DDR-verification failure (transport/R1 error, empty read, or a
 * readback that disagrees with its own sum). */
static int
gta4l_emmc_probe_h12_class(const struct gta4l_emmc_bus_result *wddr,
    int rc, uint32_t pre_sum)
{
	if ((rc == GTA4L_EMMC_OK) && (wddr->switched == 4u) &&
	    (wddr->readback_sum == pre_sum + 1u + 4u)) {
		return 1;
	}
	if ((rc == GTA4L_EMMC_ERR_BUS_WIDTH) && (wddr->switched == 0u) &&
	    (wddr->readback_sum == pre_sum + 1u)) {
		return 0;
	}
	return 2;
}

/* Revert the width at HS and then the timing class. Best effort: the HS
 * revert runs even when the width revert fails. Returns 0, 14 (width
 * revert) or 15 (HS revert). */
static int
gta4l_emmc_probe_h12_unwind(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe_h12 *h12)
{
	h12->width_revert_rc = gta4l_emmc_set_bus_width(host, 1u,
	    &h12->width_revert);
	h12->hs_revert_rc = gta4l_emmc_set_hs_timing(host, 0u,
	    &h12->hs_revert);
	if (h12->width_revert_rc != GTA4L_EMMC_OK) {
		return 14;
	}
	if (h12->hs_revert_rc != GTA4L_EMMC_OK) {
		return 15;
	}
	return 0;
}

/* Unwind, prove block 0 is intact and report. `stop` is 0 for the clean
 * refusal row or 13 for the honored row. */
static void
gta4l_emmc_probe_h12_finish(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe, struct gta4l_emmc_probe_h12 *h12,
    uint32_t block0_sum, int stop)
{
	static uint8_t qblock[GTA4L_EMMC_BLOCK_BYTES];
	int unwind;

	unwind = gta4l_emmc_probe_h12_unwind(host, h12);
	if (unwind != 0) {
		gta4l_emmc_probe_report_h12(host, probe, h12,
		    (unwind == 14) ? h12->width_revert_rc :
		    h12->hs_revert_rc, unwind);
	}
	h12->q_result = gta4l_emmc_read_block(host, 0u, qblock);
	if (h12->q_result != GTA4L_EMMC_OK) {
		gta4l_emmc_probe_report_h12(host, probe, h12, h12->q_result,
		    15);
	}
	h12->q_block0_sum = gta4l_emmc_probe_sum(qblock,
	    GTA4L_EMMC_BLOCK_BYTES);
	if (h12->q_block0_sum != block0_sum) {
		gta4l_emmc_probe_report_h12(host, probe, h12,
		    GTA4L_EMMC_PROBE_ERR_CONTENT, 15);
	}
	gta4l_emmc_probe_report_h12(host, probe, h12, GTA4L_EMMC_OK, stop);
}

/* Run the stage-12 record-only DDR attempt: stage-A at 400 kHz/1-bit is the
 * baseline, then HS entry, the CMD6 SET byte 183 = 4 attempt and its
 * verification re-read, the width and timing reverts and the recovery read.
 * The controller DDR configuration is never touched and no data moves in
 * DDR framing. Report and stop in every case; no retry. */
static void
gta4l_emmc_probe_h12_run(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe, struct gta4l_emmc_probe_range *range,
    const uint8_t *block0, const uint8_t *block1)
{
	struct gta4l_emmc_probe_h12 h12;
	uint8_t ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t index;
	uint32_t sum;
	uint32_t block0_sum;
	int klass;
	int unwind;
	int rc;

	bzero(&h12, sizeof(h12));
	block0_sum = gta4l_emmc_probe_sum(block0, GTA4L_EMMC_BLOCK_BYTES);
	/* The stage-A pass already ran; block1 is unused by this rung. */
	(void)range;
	(void)block1;

	h12.pre_clock = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CLOCK_CONTROL);
	h12.pre_hc = gta4l_emmc_probe_read8(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_CONTROL);
	h12.pre_qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	h12.pre_dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	h12.pre_dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);
	rc = gta4l_emmc_read_ext_csd(host, ext_csd);
	if (rc != GTA4L_EMMC_OK) {
		h12.switch_rc = rc;
		gta4l_emmc_probe_report_h12(host, probe, &h12, rc, 10);
	}
	h12.pre_hs = ext_csd[GTA4L_EMMC_EXT_CSD_HS_TIMING_OFF];
	h12.pre_width = ext_csd[GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF];
	for (index = 0, sum = 0; index < GTA4L_EMMC_EXT_CSD_BYTES;
	    index++) {
		sum += ext_csd[index];
	}
	h12.pre_sum = sum;

	h12.switch_rc = gta4l_emmc_set_hs_timing(host, 1u, &h12.hs);
	if (h12.switch_rc != GTA4L_EMMC_OK) {
		h12.hs_revert_rc = gta4l_emmc_set_hs_timing(host, 0u,
		    &h12.hs_revert);
		gta4l_emmc_probe_report_h12(host, probe, &h12,
		    h12.switch_rc,
		    (h12.hs.clock_result != GTA4L_EMMC_OK) ? 11 : 10);
	}

	/* WDDR: the record-only 4-bit DDR value. The helper follows the
	 * 4-bit DAT width for the verification read but never applies a
	 * controller DDR mode. */
	h12.wddr_rc = gta4l_emmc_set_bus_width_reserved4(host, &h12.wddr);
	klass = gta4l_emmc_probe_h12_class(&h12.wddr, h12.wddr_rc,
	    h12.pre_sum);
	if (klass == 0) {
		gta4l_emmc_probe_h12_finish(host, probe, &h12, block0_sum,
		    0);
	}
	if (klass == 1) {
		gta4l_emmc_probe_h12_finish(host, probe, &h12, block0_sum,
		    13);
	}

	/* DDR-verification failure: revert both and stop with the failure.
	 * A transport/R1 error keeps its code; a refused or disagreeing
	 * readback reports the width-mismatch result. */
	unwind = gta4l_emmc_probe_h12_unwind(host, &h12);
	if (unwind != 0) {
		gta4l_emmc_probe_report_h12(host, probe, &h12,
		    (unwind == 14) ? h12.width_revert_rc :
		    h12.hs_revert_rc, unwind);
	}
	if ((h12.wddr_rc == GTA4L_EMMC_OK) ||
	    (h12.wddr_rc == GTA4L_EMMC_ERR_BUS_WIDTH)) {
		rc = GTA4L_EMMC_ERR_BUS_WIDTH;
	} else {
		rc = h12.wddr_rc;
	}
	gta4l_emmc_probe_report_h12(host, probe, &h12, rc, 12);
}

struct gta4l_emmc_probe_h13 {
	struct gta4l_emmc_bus_reset_result bus;
	uint32_t pre_clock;
	uint32_t pre_hc;
	uint32_t pre_bw;
	uint32_t pre_sum;
	uint32_t pre_qdsd;
	uint32_t pre_dll;
	uint32_t pre_dst;
	int      q_result;
	uint32_t q_block0_sum;
};

static void
gta4l_emmc_probe_report_h13(const struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_h13 *h13, int rc, int stop)
{
	uint8_t hci = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	uint32_t qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	uint32_t dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	uint32_t dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

	panic("GTA4L-EMMC H13 REPORT: rc=%d stop=%u mci=%08x rca=%u cap=%u "
	    "rev=%u hci=%02x step=%u log=%u bw=%02x\n"
	    "GTA4L-EMMC H13 R clk=%04x hc=%02x bw=%02x es=%08x\n"
	    "GTA4L-EMMC H13 W wrc=%d err=%04x nrm=%04x pst=%08x\n"
	    "GTA4L-EMMC H13 RST rc=%d wr=%02x rb=%02x it=%u pst=%08x "
	    "dinh=%u\n"
	    "GTA4L-EMMC H13 T trc=%d tw=%08x te=%08x\n"
	    "GTA4L-EMMC H13 Q rc=%d qb0=%08x\n"
	    "GTA4L-EMMC H13 PAD pre=%08x/%08x/%08x post=%08x/%08x/%08x\n"
	    "GTA4L-EMMC H13 FAIL fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u",
	    rc, stop, host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev, (unsigned int)hci,
	    host->last_step, probe->log_count,
	    (unsigned int)host->bus_width,
	    h13->pre_clock, (unsigned int)h13->pre_hc,
	    (unsigned int)h13->pre_bw, h13->pre_sum,
	    h13->bus.write_result, (unsigned int)h13->bus.write_error,
	    (unsigned int)h13->bus.write_normal, h13->bus.write_present,
	    h13->bus.reset_result, (unsigned int)h13->bus.reset_written,
	    (unsigned int)h13->bus.reset_readback,
	    h13->bus.reset_iterations, h13->bus.post_present,
	    h13->bus.dat_inhibit,
	    h13->bus.test_result, h13->bus.test_word,
	    h13->bus.test_expected,
	    h13->q_result, h13->q_block0_sum,
	    h13->pre_qdsd, h13->pre_dll, h13->pre_dst, qdsd, dll, dst,
	    (unsigned int)host->fail_valid, host->fail_index,
	    host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

/* Run the rung-1 write-phase recovery: the stage-A pass at 400 kHz/1-bit is
 * the baseline, then one CMD19 write at 1-bit, one bounded data-circuit
 * reset, the post-reset present state, one CMD14 read and the block-0
 * recovery read. The reset is local to this rung; no other timeout path is
 * changed. Report and stop in every case; no retry.
 *
 * Stop codes: 0 = the recorded CC + BufferWriteReady/no-TC write, with the
 * reset clear and CMD14 returning the expected pattern; 10 = the reset did
 * not self-clear; 11 = DAT_INHIBIT survived the reset (CMD14 not issued);
 * 12 = the width/argument precondition refused the helper; 13 = CMD14
 * pattern mismatch; 14 = CMD14 transport or card-status failure; 15 = the
 * block-0 recovery read failed or disagreed; 16 = the write completed
 * normally (no wedge) and the rest passed; 17 = the write failed with a
 * non-timeout error and the rest passed. */
static void
gta4l_emmc_probe_h13_run(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_range *range, const uint8_t *block0)
{
	static uint8_t qblock[GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_probe_h13 h13;
	uint32_t block0_sum;
	int rc;
	int stop;

	bzero(&h13, sizeof(h13));
	(void)range;
	block0_sum = gta4l_emmc_probe_sum(block0, GTA4L_EMMC_BLOCK_BYTES);
	h13.pre_clock = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CLOCK_CONTROL);
	h13.pre_hc = gta4l_emmc_probe_read8(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_CONTROL);
	h13.pre_bw = host->bus_width;
	h13.pre_sum = host->ext_csd_sum;
	h13.pre_qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	h13.pre_dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	h13.pre_dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

	rc = gta4l_emmc_bus_test_reset(host, 1u, &h13.bus);
	if ((rc == GTA4L_EMMC_ERR_ARG) ||
	    (rc == GTA4L_EMMC_ERR_BUS_WIDTH)) {
		gta4l_emmc_probe_report_h13(host, probe, &h13, rc, 12);
	}

	h13.q_result = gta4l_emmc_read_block(host, 0u, qblock);
	if (h13.q_result == GTA4L_EMMC_OK) {
		h13.q_block0_sum = gta4l_emmc_probe_sum(qblock,
		    GTA4L_EMMC_BLOCK_BYTES);
	}

	if (h13.bus.reset_result != GTA4L_EMMC_OK) {
		rc = h13.bus.reset_result;
		stop = 10;
	} else if (h13.bus.dat_inhibit != 0u) {
		rc = GTA4L_EMMC_ERR_INHIBIT;
		stop = 11;
	} else if ((h13.bus.write_result != GTA4L_EMMC_OK) &&
	    (h13.bus.write_result != GTA4L_EMMC_ERR_DATA_TIMEOUT)) {
		/* A write that failed before the observed completion class is
		 * the headline; CMD14 can only be inconclusive after it. */
		rc = h13.bus.write_result;
		stop = 17;
	} else if (h13.bus.test_result == GTA4L_EMMC_ERR_BUS_TEST) {
		rc = GTA4L_EMMC_ERR_BUS_TEST;
		stop = 13;
	} else if (h13.bus.test_result != GTA4L_EMMC_OK) {
		rc = h13.bus.test_result;
		stop = 14;
	} else if ((h13.q_result != GTA4L_EMMC_OK) ||
	    (h13.q_block0_sum != block0_sum)) {
		rc = (h13.q_result != GTA4L_EMMC_OK) ? h13.q_result :
		    GTA4L_EMMC_PROBE_ERR_CONTENT;
		stop = 15;
	} else if (h13.bus.write_result == GTA4L_EMMC_OK) {
		rc = GTA4L_EMMC_OK;
		stop = 16;
	} else {
		rc = GTA4L_EMMC_OK;
		stop = 0;
	}
	gta4l_emmc_probe_report_h13(host, probe, &h13, rc, stop);
}

struct gta4l_emmc_probe_h14 {
	struct gta4l_emmc_bus_reset_result bus;
	struct gta4l_emmc_post_write_drain_result drain;
	uint32_t pre_clock;
	uint32_t pre_hc;
	uint32_t pre_bw;
	uint32_t pre_sum;
	uint32_t pre_qdsd;
	uint32_t pre_dll;
	uint32_t pre_dst;
	int      q_result;
	uint32_t q_block0_sum;
};

static void
gta4l_emmc_probe_report_h14(const struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_h14 *h14, int rc, int stop)
{
	uint8_t hci = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	uint32_t qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	uint32_t dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	uint32_t dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

	panic("GTA4L-EMMC H14 REPORT: rc=%d stop=%u mci=%08x rca=%u cap=%u "
	    "rev=%u hci=%02x step=%u log=%u bw=%02x\n"
	    "GTA4L-EMMC H14 R clk=%04x hc=%02x bw=%02x es=%08x\n"
	    "GTA4L-EMMC H14 W wrc=%d err=%04x nrm=%04x pst=%08x\n"
	    "GTA4L-EMMC H14 RST rc=%d wr=%02x rb=%02x it=%u pst=%08x "
	    "dinh=%u\n"
	    "GTA4L-EMMC H14 C12 rc=%d r1=%08x err=%04x nrm=%04x "
	    "pst=%08x\n"
	    "GTA4L-EMMC H14 C13 at=%u rc=%d r1=%08x err=%04x nrm=%04x "
	    "pst=%08x\n"
	    "GTA4L-EMMC H14 T trc=%d tw=%08x te=%08x\n"
	    "GTA4L-EMMC H14 Q rc=%d qb0=%08x\n"
	    "GTA4L-EMMC H14 PAD pre=%08x/%08x/%08x post=%08x/%08x/%08x\n"
	    "GTA4L-EMMC H14 FAIL fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u",
	    rc, stop, host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev, (unsigned int)hci,
	    host->last_step, probe->log_count,
	    (unsigned int)host->bus_width,
	    h14->pre_clock, (unsigned int)h14->pre_hc,
	    (unsigned int)h14->pre_bw, h14->pre_sum,
	    h14->bus.write_result, (unsigned int)h14->bus.write_error,
	    (unsigned int)h14->bus.write_normal, h14->bus.write_present,
	    h14->bus.reset_result, (unsigned int)h14->bus.reset_written,
	    (unsigned int)h14->bus.reset_readback,
	    h14->bus.reset_iterations, h14->bus.post_present,
	    h14->bus.dat_inhibit,
	    h14->drain.stop_result, h14->drain.stop_response,
	    (unsigned int)h14->drain.stop_error,
	    (unsigned int)h14->drain.stop_normal,
	    h14->drain.stop_present,
	    (unsigned int)h14->drain.status_attempted,
	    h14->drain.status_result, h14->drain.status_response,
	    (unsigned int)h14->drain.status_error,
	    (unsigned int)h14->drain.status_normal,
	    h14->drain.status_present,
	    h14->bus.test_result, h14->bus.test_word,
	    h14->bus.test_expected,
	    h14->q_result, h14->q_block0_sum,
	    h14->pre_qdsd, h14->pre_dll, h14->pre_dst, qdsd, dll, dst,
	    (unsigned int)host->fail_valid, host->fail_index,
	    host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

/* Run the rung-2 write-phase recovery: the stage-A pass at 400 kHz/1-bit is
 * the baseline, then one CMD19 write at 1-bit, one bounded data-circuit
 * reset, the post-reset present state, the forced CMD12/CMD13 drain, one
 * CMD14 read and the block-0 recovery read. The drain runs after the reset
 * while the post-reset inhibit is clear and before CMD14, because a failed
 * CMD14 can re-latch DAT_INHIBIT. The drain is local to this rung; no other
 * timeout path is changed. Report and stop in every case; no retry.
 *
 * Stop codes: 0 = the recorded wedge, a clean reset and drain, and CMD14/Q
 * passing; 10 = the reset did not self-clear; 11 = DAT_INHIBIT survived the
 * reset (drain and CMD14 not issued); 12 = the width/argument precondition
 * refused the helper; 13 = CMD12 silent (no R1 within the window); 14 =
 * CMD12 answered but CMD13 failed; 15 = the drain completed but the CMD14/Q
 * reads failed; 16 = the write completed normally and the rest passed; 17 =
 * the write failed with a non-timeout error and the rest passed. Reset and
 * inhibit verdicts precede the drain; a non-timeout write verdict precedes
 * the drain and CMD14 verdicts, matching the rung-1 precedence. */
static void
gta4l_emmc_probe_h14_run(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_range *range, const uint8_t *block0)
{
	static uint8_t qblock[GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_probe_h14 h14;
	uint32_t block0_sum;
	int rc;
	int stop;

	bzero(&h14, sizeof(h14));
	/* A reset or inhibit stop returns before the drain, so name both
	 * commands as not issued instead of printing zeroed success fields. */
	h14.drain.stop_result = GTA4L_EMMC_ERR_UNSUPPORTED;
	h14.drain.status_result = GTA4L_EMMC_ERR_UNSUPPORTED;
	(void)range;
	block0_sum = gta4l_emmc_probe_sum(block0, GTA4L_EMMC_BLOCK_BYTES);
	h14.pre_clock = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CLOCK_CONTROL);
	h14.pre_hc = gta4l_emmc_probe_read8(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_CONTROL);
	h14.pre_bw = host->bus_width;
	h14.pre_sum = host->ext_csd_sum;
	h14.pre_qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	h14.pre_dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	h14.pre_dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

	rc = gta4l_emmc_bus_test_reset_drain(host, 1u, &h14.bus,
	    &h14.drain);
	if ((rc == GTA4L_EMMC_ERR_ARG) ||
	    (rc == GTA4L_EMMC_ERR_BUS_WIDTH)) {
		gta4l_emmc_probe_report_h14(host, probe, &h14, rc, 12);
	}

	h14.q_result = gta4l_emmc_read_block(host, 0u, qblock);
	if (h14.q_result == GTA4L_EMMC_OK) {
		h14.q_block0_sum = gta4l_emmc_probe_sum(qblock,
		    GTA4L_EMMC_BLOCK_BYTES);
	}

	if (h14.bus.reset_result != GTA4L_EMMC_OK) {
		rc = h14.bus.reset_result;
		stop = 10;
	} else if (h14.bus.dat_inhibit != 0u) {
		rc = GTA4L_EMMC_ERR_INHIBIT;
		stop = 11;
	} else if ((h14.bus.write_result != GTA4L_EMMC_OK) &&
	    (h14.bus.write_result != GTA4L_EMMC_ERR_DATA_TIMEOUT)) {
		/* The non-timeout write class is the headline; the drain and
		 * CMD14 samples stay record-only after it. */
		rc = h14.bus.write_result;
		stop = 17;
	} else if (h14.drain.stop_result != GTA4L_EMMC_OK) {
		rc = h14.drain.stop_result;
		stop = 13;
	} else if (h14.drain.status_result != GTA4L_EMMC_OK) {
		rc = h14.drain.status_result;
		stop = 14;
	} else if ((h14.bus.test_result != GTA4L_EMMC_OK) ||
	    (h14.q_result != GTA4L_EMMC_OK) ||
	    (h14.q_block0_sum != block0_sum)) {
		rc = (h14.bus.test_result != GTA4L_EMMC_OK) ?
		    h14.bus.test_result :
		    ((h14.q_result != GTA4L_EMMC_OK) ? h14.q_result :
		    GTA4L_EMMC_PROBE_ERR_CONTENT);
		stop = 15;
	} else if (h14.bus.write_result == GTA4L_EMMC_OK) {
		rc = GTA4L_EMMC_OK;
		stop = 16;
	} else {
		rc = GTA4L_EMMC_OK;
		stop = 0;
	}
	gta4l_emmc_probe_report_h14(host, probe, &h14, rc, stop);
}

/* H15/H16/H17: verified 4-bit SDR width rung. This is the corrected replacement
 * for the byte-183 readback rungs: the switch is verified by an EXT_CSD read
 * whose read-only properties must match the 1-bit baseline, and a failed
 * switch or a wedged data path is restored with CMD6 and a bounded local data
 * reset. `mode` selects the rung: 1 is H15 (the strict CMD19/CMD14 bus test
 * before the content pass), 0 is H16 (content-only, no CMD19/CMD14 at all)
 * and 2 is H17 (the content pass first, then the Linux-compatible bus test
 * whose CMD19 data timeout is tolerated and followed by a data reset plus
 * CMD14). No HS, DDR or 8-bit transition is part of any mode. */
struct gta4l_emmc_probe_h15 {
	struct gta4l_emmc_bus_result w4;
	struct gta4l_emmc_bus_result revert;
	struct gta4l_emmc_probe_bus content;
	uint32_t pre_clock;
	uint32_t pre_hc;
	uint32_t pre_bw;
	uint32_t pre_sum;
	uint32_t pre_qdsd;
	uint32_t pre_dll;
	uint32_t pre_dst;
	uint8_t  bus_test_attempted;
	uint8_t  content_attempted;
	uint8_t  revert_attempted;
	uint8_t  q_attempted;
	uint8_t  crc_ignored;
	int      write_result;
	int      reset_result;
	int      content_rc;
	int      q_result;
	uint32_t q_block0_sum;
};

static void
gta4l_emmc_probe_report_h15(const struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_h15 *h15, const char *tag, int rc, int stop)
{
	uint8_t hci = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	uint32_t qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	uint32_t dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	uint32_t dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

	panic("GTA4L-EMMC %s REPORT: rc=%d stop=%u mci=%08x rca=%u cap=%u "
	    "rev=%u hci=%02x step=%u log=%u bw=%02x\n"
	    "GTA4L-EMMC %s R clk=%04x hc=%02x bw=%02x es=%08x\n"
	    "GTA4L-EMMC %s W rc=%d req=%u sw=%u vf=%u c6at=%u c6=%08x "
	    "hb=%02x ha=%02x es=%08x rb=%d\n"
	    "GTA4L-EMMC %s B att=%u wrc=%d rst=%d ci=%u trc=%d tw=%08x te=%08x\n"
	    "GTA4L-EMMC %s T att=%u rc=%d same=%u div=%u b0=%08x b1=%08x "
	    "data=%08x last=%08x\n"
	    "GTA4L-EMMC %s V att=%u rc=%d sw=%u vf=%u c6at=%u c6=%08x hb=%02x "
	    "ha=%02x es=%08x rb=%d\n"
	    "GTA4L-EMMC %s Q att=%u rc=%d qb0=%08x\n"
	    "GTA4L-EMMC %s PAD pre=%08x/%08x/%08x post=%08x/%08x/%08x\n"
	    "GTA4L-EMMC %s FAIL fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u",
	    tag, rc, stop, host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev, (unsigned int)hci,
	    host->last_step, probe->log_count,
	    (unsigned int)host->bus_width,
	    tag, h15->pre_clock, (unsigned int)h15->pre_hc,
	    (unsigned int)h15->pre_bw, h15->pre_sum,
	    tag, h15->w4.switch_result, h15->w4.requested,
	    h15->w4.switched, h15->w4.verified, h15->w4.cmd6_attempts,
	    h15->w4.switch_status,
	    (unsigned int)h15->w4.host_before,
	    (unsigned int)h15->w4.host_after, h15->w4.readback_sum,
	    h15->w4.rollback_result,
	    tag, (unsigned int)h15->bus_test_attempted,
	    h15->write_result, h15->reset_result,
	    (unsigned int)h15->crc_ignored,
	    h15->w4.test_result, h15->w4.test_word,
	    h15->w4.test_expected,
	    tag, (unsigned int)h15->content_attempted, h15->content_rc,
	    h15->content.same, h15->content.div,
	    h15->content.block0_sum, h15->content.block1_sum,
	    h15->content.data_sum, h15->content.last_sum,
	    tag, (unsigned int)h15->revert_attempted,
	    h15->revert.switch_result, h15->revert.switched,
	    h15->revert.verified, h15->revert.cmd6_attempts,
	    h15->revert.switch_status,
	    (unsigned int)h15->revert.host_before,
	    (unsigned int)h15->revert.host_after,
	    h15->revert.readback_sum, h15->revert.rollback_result,
	    tag, (unsigned int)h15->q_attempted, h15->q_result,
	    h15->q_block0_sum,
	    tag, h15->pre_qdsd, h15->pre_dll, h15->pre_dst, qdsd, dll, dst,
	    tag, (unsigned int)host->fail_valid, host->fail_index,
	    host->fail_argument, host->fail_command,
	    (unsigned int)host->fail_error,
	    (unsigned int)host->fail_normal, host->fail_present,
	    host->fail_step);
}

static void
gta4l_emmc_probe_width_run(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe, struct gta4l_emmc_probe_range *range,
    const uint8_t *block0, const uint8_t *block1, int mode)
{
	static uint8_t qblock[GTA4L_EMMC_BLOCK_BYTES];
	uint8_t baseline[GTA4L_EMMC_EXT_CSD_BYTES];
	struct gta4l_emmc_probe_h15 h15;
	struct gta4l_emmc_probe_bus base;
	const char *tag = (mode == 1) ? "H15" : ((mode == 2) ? "H17" : "H16");
	uint32_t index;
	int rc;
	int restore;

	bzero(&h15, sizeof(h15));
	bzero(&base, sizeof(base));
	bzero(baseline, sizeof(baseline));
	base.block0_sum = gta4l_emmc_probe_sum(block0,
	    GTA4L_EMMC_BLOCK_BYTES);
	base.block1_sum = gta4l_emmc_probe_sum(block1,
	    GTA4L_EMMC_BLOCK_BYTES);
	for (index = 0; index < GTA4L_EMMC_PROBE_ARRAY_BLOCKS; index++) {
		base.blocks[index] = range->array[index];
	}
	base.data_sum = range->data_sum;
	base.last_sum = range->last_sum;

	h15.pre_clock = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CLOCK_CONTROL);
	h15.pre_hc = gta4l_emmc_probe_read8(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_CONTROL);
	h15.pre_bw = host->bus_width;
	h15.pre_sum = host->ext_csd_sum;
	h15.pre_qdsd = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
	h15.pre_dll = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	h15.pre_dst = gta4l_emmc_probe_read32(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

	/* Capture the 1-bit read-only properties before any CMD6. This is the
	 * only baseline the recovery path needs; unlike the verified switch it
	 * must not re-read a possibly wedged data path later. */
	rc = gta4l_emmc_read_ext_csd(host, baseline);
	if (rc != GTA4L_EMMC_OK) {
		gta4l_emmc_probe_report_h15(host, probe, &h15, tag, rc, 10);
	}

	rc = gta4l_emmc_set_bus_width_verified(host, 4u, &h15.w4);
	if (rc == GTA4L_EMMC_ERR_WIDTH_RESTORE) {
		gta4l_emmc_probe_report_h15(host, probe, &h15, tag, rc, 12);
	}
	if (rc != GTA4L_EMMC_OK) {
		gta4l_emmc_probe_report_h15(host, probe, &h15, tag, rc, 11);
	}

	if (mode == 1) {
		h15.bus_test_attempted = 1;
		rc = gta4l_emmc_bus_test_verified(host, 4u, &h15.w4);
		h15.write_result = h15.w4.write_result;
		h15.reset_result = h15.w4.reset_result;
		h15.crc_ignored = host->bus_test_ignored_crc;
		if (rc != GTA4L_EMMC_OK) {
			/* The write phase can leave DAT_INHIBIT latched. The
			 * CMD6 revert must still be issued, so use the
			 * baseline-based recovery rather than another
			 * verified switch that would pre-read EXT_CSD. */
			h15.revert_attempted = 1;
			restore = gta4l_emmc_set_bus_width_recover(host, 1u,
			    baseline, &h15.revert);
			if (restore != GTA4L_EMMC_OK) {
				gta4l_emmc_probe_report_h15(host, probe,
				    &h15, tag, restore, 15);
			}
			gta4l_emmc_probe_report_h15(host, probe, &h15,
			    tag, rc, 13);
		}
	}

	h15.content_attempted = 1;
	h15.content_rc = gta4l_emmc_probe_b1_content(host, range,
	    &h15.content, 0);
	rc = h15.content_rc;
	if (rc == GTA4L_EMMC_OK) {
		gta4l_emmc_probe_b1_compare(range, &base, &h15.content);
		if (h15.content.same == 0u) {
			rc = GTA4L_EMMC_PROBE_ERR_CONTENT;
		}
	}
	if ((rc == GTA4L_EMMC_OK) && (mode == 2)) {
		/* Linux-compatible bus test: run it on the 4-bit path the
		 * content pass just proved, tolerate the expected CMD19
		 * write-phase data timeout, clear the data circuit and let
		 * the CMD14 read-back decide. */
		h15.bus_test_attempted = 1;
		host->bus_test_tolerate_write_timeout = 1u;
		rc = gta4l_emmc_bus_test_verified(host, 4u, &h15.w4);
		host->bus_test_tolerate_write_timeout = 0u;
		h15.write_result = h15.w4.write_result;
		h15.reset_result = h15.w4.reset_result;
		h15.crc_ignored = host->bus_test_ignored_crc;
		if (rc != GTA4L_EMMC_OK) {
			h15.revert_attempted = 1;
			restore = gta4l_emmc_set_bus_width_recover(host, 1u,
			    baseline, &h15.revert);
			if (restore != GTA4L_EMMC_OK) {
				gta4l_emmc_probe_report_h15(host, probe,
				    &h15, tag, restore, 15);
			}
			gta4l_emmc_probe_report_h15(host, probe, &h15,
			    tag, rc, 13);
		}
	}
	h15.revert_attempted = 1;
	restore = gta4l_emmc_set_bus_width_recover(host, 1u, baseline,
	    &h15.revert);
	if (restore != GTA4L_EMMC_OK) {
		gta4l_emmc_probe_report_h15(host, probe, &h15, tag,
		    restore, 15);
	}
	if (rc != GTA4L_EMMC_OK) {
		gta4l_emmc_probe_report_h15(host, probe, &h15, tag, rc, 14);
	}

	h15.q_attempted = 1;
	h15.q_result = gta4l_emmc_read_block(host, 0u, qblock);
	if (h15.q_result == GTA4L_EMMC_OK) {
		h15.q_block0_sum = gta4l_emmc_probe_sum(qblock,
		    GTA4L_EMMC_BLOCK_BYTES);
	}
	if ((h15.q_result != GTA4L_EMMC_OK) ||
	    (h15.q_block0_sum != base.block0_sum)) {
		gta4l_emmc_probe_report_h15(host, probe, &h15, tag,
		    (h15.q_result != GTA4L_EMMC_OK) ? h15.q_result :
		    GTA4L_EMMC_PROBE_ERR_CONTENT, 16);
	}
	gta4l_emmc_probe_report_h15(host, probe, &h15, tag, GTA4L_EMMC_OK, 0);
}

/* Run the stage-7 audit: read back the timeout configuration (R1), arm the
 * minimum data timeout and read 8 blocks (E1), re-read block 0 to prove the bus
 * recovered (Q), then repeat the 1-bit CMD19 write test under the extended
 * software bound (E2). Report and stop in every case; no retry on device. */
static void
gta4l_emmc_probe_dto_run(struct gta4l_emmc_host *host,
    struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_range *range, uint8_t *block0)
{
	static uint8_t blocks[GTA4L_EMMC_PROBE_DTO_BLOCKS *
	    GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_probe_dto dto;
	struct gta4l_emmc_bus_result bus;
	int e1_dto;
	int stop;
	int rc;

	bzero(&dto, sizeof(dto));
	bzero(&bus, sizeof(bus));
	dto.e1_blocks = GTA4L_EMMC_PROBE_DTO_BLOCKS;
	dto.clk = gta4l_emmc_probe_read16(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL);
	dto.normal_enable = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_NORMAL_INT_ENABLE);
	dto.error_enable = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_ERROR_INT_ENABLE);
	dto.normal_signal = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_NORMAL_INT_SIGNAL);
	dto.error_signal = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_ERROR_INT_SIGNAL);
	dto.error_status = gta4l_emmc_probe_read16(probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_ERROR_INT_STATUS);

	/* Force the minimum timeout count in TIMEOUT_CONTROL (0x2E), the byte
	 * register Linux writes, and read it back. The previous value is
	 * restored before the report. */
	dto.toc = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_TIMEOUT_CONTROL);
	gta4l_emmc_probe_write8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_TIMEOUT_CONTROL, 0);
	gta4l_emmc_probe_barrier(probe);
	dto.arm = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_TIMEOUT_CONTROL);

	host->last_step = GTA4L_EMMC_STEP_DTO;
	dto.e1_result = gta4l_emmc_read_multi(host,
	    (uint64_t)range->entries_lba, GTA4L_EMMC_PROBE_DTO_BLOCKS,
	    blocks);
	dto.e1_fail_valid = host->fail_valid;
	dto.e1_fail_index = host->fail_index;
	dto.e1_fail_argument = host->fail_argument;
	dto.e1_fail_command = host->fail_command;
	dto.e1_fail_error = host->fail_error;
	dto.e1_fail_normal = host->fail_normal;
	dto.e1_fail_present = host->fail_present;
	dto.e1_fail_step = host->fail_step;
	dto.e1_multi_block = host->multi_block;
	dto.e1_drain_used = host->multi_drain_used;
	dto.e1_drain_resolved = host->multi_drain_resolved;
	dto.e1_drain_blocks = host->multi_drain_blocks;
	dto.e1_drain_iterations = host->multi_drain_iterations;
	dto.e1_stop_result = host->multi_stop_result;
	dto.e1_stop_valid = host->multi_stop_valid;
	dto.e1_stop_capture_valid = host->multi_stop_capture_valid;
	e1_dto = (dto.e1_result == GTA4L_EMMC_ERR_DATA_TIMEOUT) &&
	    (dto.e1_fail_valid != 0u) &&
	    ((dto.e1_fail_error & GTA4L_EMMC_INT_ERR_DATA_TIMEOUT) != 0u);

	/* The core keeps only the first failure record. It is snapshotted above,
	 * so clear it now and let the report's terminal FAIL name the last
	 * experiment (the recovery read or E2) instead of repeating E1. */
	host->fail_valid = 0;
	host->fail_index = 0;
	host->fail_argument = 0;
	host->fail_command = 0;
	host->fail_error = 0;
	host->fail_normal = 0;
	host->fail_present = 0;
	host->fail_step = 0;

	dto.q_result = gta4l_emmc_read_block(host, 0u, block0);

	if ((dto.arm == 0u) && (dto.q_result == GTA4L_EMMC_OK)) {
		dto.e2_attempted = 1u;
		dto.e2_window = GTA4L_EMMC_PROBE_DTO_WINDOW_US;
		host->bus_test_window_us = GTA4L_EMMC_PROBE_DTO_WINDOW_US;
		dto.e2_result = gta4l_emmc_bus_test(host, 1u, &bus);
		host->bus_test_window_us = 0u;
		dto.e2_tested = 1u;
		dto.e2_word = bus.test_word;
		dto.e2_expected = bus.test_expected;
		dto.e2_test_result = bus.test_result;
		dto.e2_host_control = bus.host_control;
	}

	/* Restore the timeout count before the report in every path. */
	gta4l_emmc_probe_write8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_TIMEOUT_CONTROL, dto.toc);
	gta4l_emmc_probe_barrier(probe);
	dto.restored = gta4l_emmc_probe_read8(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_TIMEOUT_CONTROL);

	if (dto.arm != 0u) {
		stop = 12;
		rc = 0;
	} else if (dto.q_result != GTA4L_EMMC_OK) {
		stop = 11;
		rc = dto.q_result;
	} else if (dto.e2_result == GTA4L_EMMC_OK) {
		stop = 13;
		rc = 0;
	} else if ((dto.e2_result != GTA4L_EMMC_ERR_DATA_TIMEOUT) &&
	    (dto.e2_result != GTA4L_EMMC_ERR_BUS_TEST)) {
		stop = 14;
		rc = dto.e2_result;
	} else if (e1_dto != 0) {
		stop = 0;
		rc = dto.e2_result;
	} else {
		stop = 10;
		rc = dto.e2_result;
	}
	gta4l_emmc_probe_report_dto(host, probe, &dto, rc, stop);
}

static void
gta4l_emmc_probe_report_probe(const struct gta4l_emmc_host *host,
    const struct gta4l_emmc_probe *probe,
    const struct gta4l_emmc_probe_regs *regs,
    const uint8_t *block0, const uint8_t *block1,
    const struct gta4l_emmc_probe_range *range, int mb_mode)
{
	const uint32_t *content = (mb_mode != 0) ? range->mb : range->array;
	char mb_suffix[64];
	uint32_t mb_suffix_length = 0;

	mb_suffix[0] = '\0';
	if (mb_mode != 0) {
		if (range->mb_same != 0) {
			mb_suffix_length = gta4l_emmc_probe_append_text(mb_suffix,
			    mb_suffix_length, " mbsame=1");
		} else {
			mb_suffix_length = gta4l_emmc_probe_append_text(mb_suffix,
			    mb_suffix_length, " mbsame=0 mbdiv=");
			mb_suffix_length = gta4l_emmc_probe_append_decimal(
			    mb_suffix, mb_suffix_length, range->mb_div);
		}
		mb_suffix_length = gta4l_emmc_probe_append_text(mb_suffix,
		    mb_suffix_length, " drain=");
		mb_suffix_length = gta4l_emmc_probe_append_decimal(mb_suffix,
		    mb_suffix_length, (uint32_t)host->multi_drain_used);
		mb_suffix_length = gta4l_emmc_probe_append_text(mb_suffix,
		    mb_suffix_length, "/");
		mb_suffix_length = gta4l_emmc_probe_append_decimal(mb_suffix,
		    mb_suffix_length, (uint32_t)host->multi_drain_resolved);
		mb_suffix_length = gta4l_emmc_probe_append_text(mb_suffix,
		    mb_suffix_length, "/");
		mb_suffix_length = gta4l_emmc_probe_append_decimal(mb_suffix,
		    mb_suffix_length, host->multi_drain_blocks);
		mb_suffix_length = gta4l_emmc_probe_append_text(mb_suffix,
		    mb_suffix_length, "/");
		mb_suffix_length = gta4l_emmc_probe_append_decimal(mb_suffix,
		    mb_suffix_length, host->multi_drain_iterations);
	}
	mb_suffix[mb_suffix_length] = '\0';

	panic("GTA4L-EMMC PROBE REPORT: rc=0 mci=%08x rca=%u cap=%u rev=%u "
	    "bw=%u ocr=%08x ext=%08x hc2=%04x ven=%08x dll=%08x "
	    "dst=%08x pst=%08x step=%u log=%u%s\n"
	    "GTA4L-EMMC PROBE CID %08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE CSD %08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE BLOCK0 sum=%08x words=%08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE BLOCK1 sum=%08x words=%08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE GPT elba=%u n=%u sz=%u first=%u last=%u "
	    "backup=%u\n"
	    "GTA4L-EMMC PROBE %s"
	    " 2=%08x 3=%08x 4=%08x 5=%08x 6=%08x 7=%08x 8=%08x 9=%08x"
	    " 10=%08x 11=%08x 12=%08x 13=%08x 14=%08x 15=%08x 16=%08x"
	    " 17=%08x 18=%08x 19=%08x 20=%08x 21=%08x 22=%08x\n"
	    "GTA4L-EMMC PROBE DATA %u=%08x\n"
	    "GTA4L-EMMC PROBE LSTLBA=%u sum=%08x%s\n"
	    "GTA4L-EMMC PROBE CAPTURE fmt=4 hv=%04x "
	    "c2=%u/%04x/%04x/%08x,%08x,%08x,%08x "
	    "c3p=%u/%04x/%04x/%08x/%04x/%04x "
	    "c3t=%u/%u/%04x/%08x/%u/%08x,%08x,%08x,%08x "
	    "c3l=%u/%u/%04x/%04x/%u/%08x%08x/%u "
	    "cid=%08x,%08x,%08x,%08x" GTA4L_EMMC_PROBE_CMD7_FMT,
	    host->mci_version, host->rca, host->capacity_blocks,
	    (unsigned int)host->ext_csd_rev, (unsigned int)host->bus_width,
	    host->ocr, host->ext_csd_sum, (unsigned int)regs->host_control2,
	    regs->vendor_spec, regs->dll_config, regs->dll_status,
	    regs->pwr_status, host->last_step, probe->log_count,
	    (mb_mode != 0) ? " mb=1" : "",
	    host->cid[0], host->cid[1], host->cid[2], host->cid[3],
	    host->csd[0], host->csd[1], host->csd[2], host->csd[3],
	    gta4l_emmc_probe_sum(block0, GTA4L_EMMC_BLOCK_BYTES),
	    gta4l_emmc_probe_word(block0, 0),
	    gta4l_emmc_probe_word(block0, 446),
	    gta4l_emmc_probe_word(block0, 450),
	    gta4l_emmc_probe_word(block0, 454),
	    gta4l_emmc_probe_sum(block1, GTA4L_EMMC_BLOCK_BYTES),
	    gta4l_emmc_probe_word(block1, 0),
	    gta4l_emmc_probe_word(block1, 4),
	    gta4l_emmc_probe_word(block1, 8),
	    gta4l_emmc_probe_word(block1, 12),
	    range->entries_lba, range->entry_count, range->entry_size,
	    range->first_usable, range->last_usable, range->backup_lba,
	    (mb_mode != 0) ? "MBLOCKS" : "BLOCKS",
	    content[0], content[1], content[2],
	    content[3], content[4], content[5],
	    content[6], content[7], content[8],
	    content[9], content[10], content[11],
	    content[12], content[13], content[14],
	    content[15], content[16], content[17],
	    content[18], content[19], content[20],
	    range->first_usable, range->data_sum,
	    range->backup_lba, range->last_sum, mb_suffix,
	    (unsigned int)regs->host_version,
	    (unsigned int)host->cmd2_capture_valid,
	    (unsigned int)host->cmd2_normal,
	    (unsigned int)host->cmd2_error,
	    host->cmd2_raw_response[0], host->cmd2_raw_response[1],
	    host->cmd2_raw_response[2], host->cmd2_raw_response[3],
	    (unsigned int)host->pre_cmd3_capture_valid,
	    (unsigned int)host->pre_cmd3_normal,
	    (unsigned int)host->pre_cmd3_error,
	    host->pre_cmd3_present,
	    (unsigned int)host->pre_cmd3_normal_enable,
	    (unsigned int)host->pre_cmd3_error_enable,
	    (unsigned int)host->timeout_capture_valid,
	    (unsigned int)host->timeout_index,
	    (unsigned int)host->timeout_command,
	    host->timeout_argument,
	    (unsigned int)host->timeout_response_stable,
	    host->timeout_response[0], host->timeout_response[1],
	    host->timeout_response[2], host->timeout_response[3],
	    (unsigned int)host->poll_capture_valid,
	    host->poll_index,
	    (unsigned int)host->poll_first_normal,
	    (unsigned int)host->poll_last_normal,
	    host->poll_iterations,
	    (unsigned int)(host->poll_ticks >> 32),
	    (unsigned int)host->poll_ticks,
	    (unsigned int)probe->platform.counter_hz,
	    host->cid[0], host->cid[1], host->cid[2], host->cid[3],
	    GTA4L_EMMC_PROBE_CMD7_ARGS(*host));
}

void
gta4l_emmc_probe_run(void)
{
	static uint8_t block0[GTA4L_EMMC_BLOCK_BYTES];
	static uint8_t block1[GTA4L_EMMC_BLOCK_BYTES];
	static uint8_t range_block[GTA4L_EMMC_BLOCK_BYTES];
	static uint8_t mb_blocks[GTA4L_EMMC_PROBE_ARRAY_BLOCKS *
	    GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_probe probe;
	struct gta4l_emmc_probe_regs regs;
	struct gta4l_emmc_probe_range range;
	struct gta4l_emmc_probe_bus base;
	struct gta4l_emmc_probe_bus w4;
	struct gta4l_emmc_probe_bus w8;
	struct gta4l_emmc_host host;
	uint32_t version;
	uint8_t power;
	uint8_t host_control;
	uint16_t clock;
	uint16_t host_control2;
	uint16_t host_version;
	uint32_t vendor_spec;
	uint32_t vendor_spec3;
	uint32_t dll_config;
	uint32_t dll_status;
	uint32_t ddr_config;
	uint32_t pwr_status;
	uint32_t cfg;
	uint32_t cmd;
	uint32_t bcr;
	uint32_t apps_cbcr;
	uint32_t ahb_cbcr;
	uint32_t gcc_m;
	uint32_t gcc_n;
	uint32_t gcc_d;
	uint32_t index;
	uint32_t gate_mask;
	int stage;
	int mb_mode;
	int result;
	int set_result;

	if (gta4l_emmc_probe_ran != 0) {
		return;
	}
	stage = gta4l_emmc_probe_stage();
	if (stage == 0) {
		if (gta4l_emmc_probe_mb() != 0) {
			gta4l_emmc_probe_refuse_mb();
		}
		return;
	}
	if (gta4l_emmc_probe_conflict()) {
		panic("GTA4L-EMMC PROBE REFUSED: conflicting diagnostic "
		    "arguments");
	}
	mb_mode = gta4l_emmc_probe_mb();
	gta4l_emmc_probe_ran = 1;

	bzero(&probe, sizeof(probe));
	if (gta4l_emmc_platform_map(&probe.platform) != 0) {
		panic("GTA4L-EMMC PROBE REFUSED: MMIO map failed "
		    "(sdhci=%u gcc=%u tlmm=%u)",
		    (unsigned int)(probe.platform.sdhci != 0),
		    (unsigned int)(probe.platform.gcc != 0),
		    (unsigned int)(probe.platform.tlmm != 0));
	}

	bzero(&regs, sizeof(regs));
	bzero(&host, sizeof(host));
	host.ops = &gta4l_emmc_probe_ops;
	host.context = &probe;
	/* Board policy: L11/VCCQ is a fixed 1.8 V rail, so select 1.8 V
	 * signalling and keep the pad power switch in the 1.8 V position.
	 * The mainline gta4l DT declares no SDHC1 vqmmc consumer, so the
	 * Linux probe leaves both vendor pad bits at the POR value; this
	 * explicit policy follows the sdhci-msm fixed-1.8 V comment instead
	 * of that DT-derived state. See docs/emmc-driver.md. */
	host.fixed_1v8 = 1;
	host.pad_pwr_switch = 1;
	/* H9 is the only stage that enables the duplicate CMD3 hook. */
	host.cmd3_retry_enabled = (stage == 9) ? 1u : 0u;
	/* H9 also stops after the identification sequence, before EXT_CSD. */
	host.identify_only = (stage == 9) ? 1u : 0u;

	if (stage == 2) {
		result = gta4l_emmc_controller_init(&host);
	} else {
		result = gta4l_emmc_init(&host);
	}
	if (result != GTA4L_EMMC_OK) {
		if (host.clocks_enabled != 0) {
			version = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_MCI_VERSION);
			power = gta4l_emmc_probe_read8(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_POWER_CONTROL);
			host_control = gta4l_emmc_probe_read8(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_HOST_CONTROL);
			clock = gta4l_emmc_probe_read16(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CLOCK_CONTROL);
			host_control2 = gta4l_emmc_probe_read16(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_HOST_CONTROL2);
			host_version = gta4l_emmc_probe_read16(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_HOST_VERSION);
			vendor_spec = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_VENDOR_SPEC);
			vendor_spec3 = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_VENDOR_SPEC3);
			dll_config = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_DLL_CONFIG);
			dll_status = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_DLL_STATUS);
			ddr_config = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_DDR_CONFIG);
			pwr_status = gta4l_emmc_probe_read32(&probe,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_PWRCTL_STATUS);
		} else {
			/* Without the AHB/APPS branches the SDHCI window may not
			 * respond; report the cached result and GCC state only. */
			version = 0;
			power = 0;
			host_control = 0;
			clock = 0;
			host_control2 = 0;
			host_version = 0;
			vendor_spec = 0;
			vendor_spec3 = 0;
			dll_config = 0;
			dll_status = 0;
			ddr_config = 0;
			pwr_status = 0;
		}
		cfg = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_CFG);
		gcc_m = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_M);
		gcc_n = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_N);
		gcc_d = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_D);
		cmd = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_CMD);
		bcr = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_BCR);
		apps_cbcr = gta4l_emmc_probe_read32(&probe,
		    GTA4L_EMMC_SPACE_GCC, GTA4L_EMMC_GCC_SDCC1_APPS_CBCR);
		ahb_cbcr = gta4l_emmc_probe_read32(&probe,
		    GTA4L_EMMC_SPACE_GCC, GTA4L_EMMC_GCC_SDCC1_AHB_CBCR);
		regs.version = version;
		regs.power = power;
		regs.host_control = host_control;
		regs.clock = clock;
		regs.host_control2 = host_control2;
		regs.host_version = host_version;
		regs.vendor_spec = vendor_spec;
		regs.vendor_spec3 = vendor_spec3;
		regs.dll_config = dll_config;
		regs.dll_status = dll_status;
		regs.ddr_config = ddr_config;
		regs.pwr_status = pwr_status;
		regs.cfg = cfg;
		regs.gcc_m = gcc_m;
		regs.gcc_n = gcc_n;
		regs.gcc_d = gcc_d;
		regs.cmd = cmd;
		regs.bcr = bcr;
		regs.apps_cbcr = apps_cbcr;
		regs.ahb_cbcr = ahb_cbcr;
		if (stage == 9) {
			gta4l_emmc_probe_report_h9(&host, &probe, &regs,
			    result,
			    gta4l_emmc_probe_h9_stop(&host, result));
		} else {
			gta4l_emmc_probe_report_failure(&host, &probe, stage,
			    result, &regs);
		}
	}

	version = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_MCI_VERSION);
	power = gta4l_emmc_probe_read8(&probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_POWER_CONTROL);
	host_control = gta4l_emmc_probe_read8(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_CONTROL);
	clock = gta4l_emmc_probe_read16(&probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL);
	host_control2 = gta4l_emmc_probe_read16(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_CONTROL2);
	host_version = gta4l_emmc_probe_read16(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_VERSION);
	vendor_spec = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_VENDOR_SPEC);
	dll_config = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	dll_status = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);
	pwr_status = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_PWRCTL_STATUS);
	regs.version = version;
	regs.power = power;
	regs.host_control = host_control;
	regs.clock = clock;
	regs.host_control2 = host_control2;
	regs.host_version = host_version;
	regs.vendor_spec = vendor_spec;
	regs.dll_config = dll_config;
	regs.dll_status = dll_status;
	regs.pwr_status = pwr_status;

	if (stage == 9) {
		gta4l_emmc_probe_report_h9(&host, &probe, &regs,
		    GTA4L_EMMC_OK,
		    gta4l_emmc_probe_h9_stop(&host, GTA4L_EMMC_OK));
	}
	if (stage == 2) {
		panic("GTA4L-EMMC HOST REPORT: rc=0 mci=%08x pwr=%02x "
		    "clk=%04x hc2=%04x ven=%08x dll=%08x dst=%08x "
		    "pst=%08x step=%u log=%u/%u/%u/%u",
		    version, (unsigned int)power, (unsigned int)clock,
		    (unsigned int)host_control2, vendor_spec, dll_config,
		    dll_status, pwr_status, host.last_step, probe.log_count,
		    probe.last_event, probe.last_a, probe.last_b);
	}
	if (stage == 3) {
		gta4l_emmc_probe_report_ident(&host, &probe, &regs);
	}

	result = gta4l_emmc_read_block(&host, 0, block0);
	if (result != GTA4L_EMMC_OK) {
		panic("GTA4L-EMMC PROBE FAILED: block 0 read rc=%d step=%u "
		    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u", result,
		    host.last_step, (unsigned int)host.fail_valid,
		    host.fail_index, host.fail_argument, host.fail_command,
		    (unsigned int)host.fail_error,
		    (unsigned int)host.fail_normal, host.fail_present,
		    host.fail_step);
	}
	result = gta4l_emmc_read_block(&host, 1, block1);
	if (result != GTA4L_EMMC_OK) {
		panic("GTA4L-EMMC PROBE FAILED: block 1 read rc=%d step=%u "
		    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u", result,
		    host.last_step, (unsigned int)host.fail_valid,
		    host.fail_index, host.fail_argument, host.fail_command,
		    (unsigned int)host.fail_error,
		    (unsigned int)host.fail_normal, host.fail_present,
		    host.fail_step);
	}

	/* Stage-A content phase: gate the pinned LBA list on the live header
	 * and then read the array, first usable sector and last sector. */
	bzero(&range, sizeof(range));
	gate_mask = gta4l_emmc_probe_gpt(&range, block1,
	    host.capacity_blocks);
	if (gate_mask != 0u) {
		panic("GTA4L-EMMC PROBE FAILED: gpt gate bad=%08x cap=%u "
		    "sig=%08x elba=%u n=%u sz=%u first=%u last=%u "
		    "backup=%u sum=%08x step=%u", gate_mask,
		    host.capacity_blocks, gta4l_emmc_probe_word(block1, 0),
		    range.entries_lba, range.entry_count, range.entry_size,
		    range.first_usable, range.last_usable, range.backup_lba,
		    gta4l_emmc_probe_sum(block1, GTA4L_EMMC_BLOCK_BYTES),
		    host.last_step);
	}

	host.last_step = GTA4L_EMMC_STEP_PROBE_ARRAY;
	for (index = 0; index < GTA4L_EMMC_PROBE_ARRAY_BLOCKS; index++) {
		result = gta4l_emmc_read_block(&host,
		    (uint64_t)range.entries_lba + index, range_block);
		if (result != GTA4L_EMMC_OK) {
			gta4l_emmc_probe_report_read_failure(&host, "array",
			    range.entries_lba + index, result);
		}
		range.array[index] = gta4l_emmc_probe_sum(range_block,
		    GTA4L_EMMC_BLOCK_BYTES);
	}

	host.last_step = GTA4L_EMMC_STEP_PROBE_DATA;
	result = gta4l_emmc_read_block(&host, range.first_usable,
	    range_block);
	if (result != GTA4L_EMMC_OK) {
		gta4l_emmc_probe_report_read_failure(&host, "data",
		    range.first_usable, result);
	}
	range.data_sum = gta4l_emmc_probe_sum(range_block,
	    GTA4L_EMMC_BLOCK_BYTES);

	host.last_step = GTA4L_EMMC_STEP_PROBE_LAST;
	result = gta4l_emmc_read_block(&host, range.backup_lba, range_block);
	if (result != GTA4L_EMMC_OK) {
		gta4l_emmc_probe_report_read_failure(&host, "last",
		    range.backup_lba, result);
	}
	range.last_sum = gta4l_emmc_probe_sum(range_block,
	    GTA4L_EMMC_BLOCK_BYTES);

	/* Stage 7: data-timeout audit. Both observed hang classes (the pre-B1
	 * CMD17 TC-only stop and the CMD19 write wedge) record no data-timeout
	 * error even though the core enables the DTO status bit, so this rung
	 * reads back the timeout configuration, arms the minimum timeout for a
	 * healthy multi-block read longer than the counter, and then repeats
	 * the 1-bit CMD19 wedge under a longer software bound. It issues no
	 * CMD6 and no card-state write; the CMD19 pattern write is the same
	 * non-persistent class already approved for B1C. */
	if (stage == 7) {
		gta4l_emmc_probe_dto_run(&host, &probe, &range, block0);
	}

	if (stage == 8) {
		gta4l_emmc_probe_b2_run(&host, &probe, &range, block0, block1);
	}

	if (stage == 10) {
		gta4l_emmc_probe_h10_run(&host, &probe, &range, block0,
		    block1);
	}

	if (stage == 11) {
		gta4l_emmc_probe_h11_run(&host, &probe, &range, block0,
		    block1);
	}

	if (stage == 12) {
		gta4l_emmc_probe_h12_run(&host, &probe, &range, block0,
		    block1);
	}

	if (stage == 13) {
		gta4l_emmc_probe_h13_run(&host, &probe, &range, block0);
	}

	if (stage == 14) {
		gta4l_emmc_probe_h14_run(&host, &probe, &range, block0);
	}

	if (stage == 15) {
		gta4l_emmc_probe_width_run(&host, &probe, &range, block0,
		    block1, 1);
	}

	if (stage == 16) {
		gta4l_emmc_probe_width_run(&host, &probe, &range, block0,
		    block1, 0);
	}

	if (stage == 17) {
		gta4l_emmc_probe_width_run(&host, &probe, &range, block0,
		    block1, 2);
	}

	/* Stage B1: switch the card/host to 4-bit then 8-bit, run the
	 * CMD19/CMD14 bus-test round trip at each width, and repeat the
	 * stage-A content reads so a ramp that disturbs data is visible. A
	 * failed step stops at that width: the report never issues further
	 * assertions on a bus that just failed. The 1-bit pass above is the
	 * baseline for the per-width comparison. */
	if ((stage >= 4) && (stage <= 14)) {
		bzero(&base, sizeof(base));
		bzero(&w4, sizeof(w4));
		bzero(&w8, sizeof(w8));
		w4.b1c_enabled = (stage >= 5) ? 1u : 0u;
		base.result.requested = 1u;
		base.result.switched = host.bus_width;
		base.result.switch_result = GTA4L_EMMC_OK;
		base.result.test_result = GTA4L_EMMC_OK;
		base.result.host_control = gta4l_emmc_probe_read8(&probe,
		    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_CONTROL);
		base.attempted = 1u;
		base.block0_sum = gta4l_emmc_probe_sum(block0,
		    GTA4L_EMMC_BLOCK_BYTES);
		base.block1_sum = gta4l_emmc_probe_sum(block1,
		    GTA4L_EMMC_BLOCK_BYTES);
		for (index = 0; index < GTA4L_EMMC_PROBE_ARRAY_BLOCKS;
		    index++) {
			base.blocks[index] = range.array[index];
		}
		base.data_sum = range.data_sum;
		base.last_sum = range.last_sum;
		base.pad_qdsd = gta4l_emmc_probe_read32(&probe,
		    GTA4L_EMMC_SPACE_TLMM, GTA4L_EMMC_TLMM_SDC1_QDSD);
		base.pad_dll = gta4l_emmc_probe_read32(&probe,
		    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
		base.pad_dst = gta4l_emmc_probe_read32(&probe,
		    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);

		/* Stage-B1C 1-bit discriminator: before any width switch,
		 * exercise the same CMD19 write / CMD14 read direction at the
		 * identification width. A pass means the write path completes
		 * at 1-bit, so a later 4-bit write timeout is width-caused; a
		 * timeout or mismatch here means the write path is broken
		 * independently of width. The failure stops the rung before
		 * any CMD6 (no persistent card-state write) with stop=9. */
		if (stage == 6) {
			base.tested = 1u;
			result = gta4l_emmc_bus_test(&host, 1u,
			    &base.test);
			if ((result != GTA4L_EMMC_OK) &&
			    (base.test.test_result == GTA4L_EMMC_OK)) {
				base.test.test_result = result;
			}
			if (result != GTA4L_EMMC_OK) {
				gta4l_emmc_probe_report_b1(&host, &probe,
				    &range, &base, &w4, &w8, result, 9);
			}
		}

		w4.attempted = 1u;
		set_result = gta4l_emmc_set_bus_width(&host, 4u, &w4.result);
		if (set_result != GTA4L_EMMC_OK) {
			result = set_result;
			if ((stage >= 5) &&
			    (gta4l_emmc_probe_b1c_enter(&host, &w4) != 0)) {
				/* B1C: the verification failed with exactly the
				 * observed tuple. Continue read-only at 4-bit
				 * instead of stopping, then report once. The
				 * failed-verification tuple stays in w4.result;
				 * the continuation result lives in w4.test,
				 * w4.same and the reread fields. */
				gta4l_emmc_probe_b1c_apply_4bit(&host,
				    &probe);
				w4.continued = 1u;
				w4.tested = 1u;
				result = gta4l_emmc_bus_test(&host, 4u,
				    &w4.test);
				if ((result != GTA4L_EMMC_OK) &&
				    (w4.test.test_result == GTA4L_EMMC_OK)) {
					/* A precondition rejection returns
					 * before filling the test result;
					 * keep `trc` naming the failure. */
					w4.test.test_result = result;
				}
				if (result == GTA4L_EMMC_OK) {
					result = gta4l_emmc_probe_b1_content(
					    &host, &range, &w4, 0);
				}
				if (result == GTA4L_EMMC_OK) {
					gta4l_emmc_probe_b1_compare(&range,
					    &base, &w4);
					result = gta4l_emmc_probe_b1c_reread(
					    &host, &w4);
				}
				gta4l_emmc_probe_b1c_restore(&host, &probe,
				    w4.result.host_control);
				w4.result.host_control =
				    gta4l_emmc_probe_read8(&probe,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_HOST_CONTROL);
				if ((result == GTA4L_EMMC_OK) &&
				    (w4.same == 0u)) {
					result =
					    GTA4L_EMMC_PROBE_ERR_CONTENT;
				}
				if (result == GTA4L_EMMC_OK) {
					result = set_result;
				}
			}
			gta4l_emmc_probe_report_b1(&host, &probe, &range,
			    &base, &w4, &w8, result, 1);
		}
		w4.tested = 1u;
		result = gta4l_emmc_bus_test(&host, 4u, &w4.test);
		if (result != GTA4L_EMMC_OK) {
			gta4l_emmc_probe_report_b1(&host, &probe, &range,
			    &base, &w4, &w8, result, 2);
		}
		result = gta4l_emmc_probe_b1_content(&host, &range, &w4, 0);
		if (result != GTA4L_EMMC_OK) {
			gta4l_emmc_probe_report_b1(&host, &probe, &range,
			    &base, &w4, &w8, result, 3);
		}
		gta4l_emmc_probe_b1_compare(&range, &base, &w4);
		if (w4.same == 0u) {
			gta4l_emmc_probe_report_b1(&host, &probe, &range,
			    &base, &w4, &w8,
			    GTA4L_EMMC_PROBE_ERR_CONTENT, 4);
		}

		w8.attempted = 1u;
		result = gta4l_emmc_set_bus_width(&host, 8u, &w8.result);
		if (result != GTA4L_EMMC_OK) {
			gta4l_emmc_probe_report_b1(&host, &probe, &range,
			    &base, &w4, &w8, result, 5);
		}
		w8.tested = 1u;
		result = gta4l_emmc_bus_test(&host, 8u, &w8.test);
		if (result != GTA4L_EMMC_OK) {
			gta4l_emmc_probe_report_b1(&host, &probe, &range,
			    &base, &w4, &w8, result, 6);
		}
		result = gta4l_emmc_probe_b1_content(&host, &range, &w8, 0);
		if (result != GTA4L_EMMC_OK) {
			gta4l_emmc_probe_report_b1(&host, &probe, &range,
			    &base, &w4, &w8, result, 7);
		}
		gta4l_emmc_probe_b1_compare(&range, &base, &w8);
		if (w8.same == 0u) {
			gta4l_emmc_probe_report_b1(&host, &probe, &range,
			    &base, &w4, &w8,
			    GTA4L_EMMC_PROBE_ERR_CONTENT, 8);
		}
		gta4l_emmc_probe_report_b1(&host, &probe, &range, &base,
		    &w4, &w8, GTA4L_EMMC_OK, 0);
	}

	/* Optional MD pass: re-read the same 21 sectors as one open-ended
	 * CMD18 transfer and stop it explicitly. A read failure stops here
	 * with the terminal failure record, the failing block ordinal and
	 * the CMD12 attempt result. A content mismatch is not a hardware
	 * failure: it is reported as mbsame=0 and the offline comparison
	 * decides. There is no on-device retry. */
	if (mb_mode != 0) {
		host.last_step = GTA4L_EMMC_STEP_PROBE_MB;
		result = gta4l_emmc_read_multi(&host, range.entries_lba,
		    GTA4L_EMMC_PROBE_ARRAY_BLOCKS, mb_blocks);
		if (result != GTA4L_EMMC_OK) {
			gta4l_emmc_probe_report_multi_failure(&host, &range,
			    result);
		}
		range.mb_same = 1;
		range.mb_div = 0;
		for (index = 0;
		    index < GTA4L_EMMC_PROBE_ARRAY_BLOCKS; index++) {
			range.mb[index] = gta4l_emmc_probe_sum(
			    mb_blocks +
			    (index * GTA4L_EMMC_BLOCK_BYTES),
			    GTA4L_EMMC_BLOCK_BYTES);
			if ((range.mb[index] != range.array[index]) &&
			    (range.mb_same != 0)) {
				range.mb_same = 0;
				range.mb_div = range.entries_lba + index;
			}
		}
	}

	gta4l_emmc_probe_report_probe(&host, &probe, &regs, block0, block1,
	    &range, mb_mode);
}
