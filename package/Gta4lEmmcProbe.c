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
#include <machine/machine_routines.h>
#include <kern/debug.h>

#include "Gta4lEmmc.h"

void gta4l_emmc_probe_preflight(void);
void gta4l_emmc_probe_run(void);
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

#define GTA4L_EMMC_PROBE_SDHCI_BASE 0x04744000u
#define GTA4L_EMMC_PROBE_GCC_BASE   0x01438000u
#define GTA4L_EMMC_PROBE_TLMM_BASE  0x00575000u
#define GTA4L_EMMC_PROBE_WINDOW     0x00001000u

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

/* Stage-7 data-timeout audit. With TOUTVAL [7:3] of CLOCK_CONTROL zero the
 * controller's data timeout is 2^13 timeout clocks. At the fixed 400 kHz
 * identification clock that is about 20.5 ms; an 8-block 1-bit read moves
 * 4 KiB at about 50 kB/s, roughly 83 ms, so an armed minimum must fire
 * mid-transfer if the counter decrements during a healthy transfer. E2
 * repeats the 1-bit CMD19 write wedge under an 8 s software bound (about
 * 390x the minimum) so a late controller timeout could still be observed. */
#define GTA4L_EMMC_PROBE_DTO_BLOCKS      8u
#define GTA4L_EMMC_PROBE_DTO_WINDOW_US   8000000u
#define GTA4L_EMMC_PROBE_TOUTVAL_MASK    0x00F8u

/* GPT header field offsets used by the gate. */
#define GTA4L_EMMC_PROBE_GPT_BACKUP_LBA   0x20u
#define GTA4L_EMMC_PROBE_GPT_FIRST_USABLE 0x28u
#define GTA4L_EMMC_PROBE_GPT_LAST_USABLE  0x30u
#define GTA4L_EMMC_PROBE_GPT_ENTRIES_LBA  0x48u
#define GTA4L_EMMC_PROBE_GPT_ENTRY_COUNT  0x50u
#define GTA4L_EMMC_PROBE_GPT_ENTRY_SIZE   0x54u

struct gta4l_emmc_probe {
	uintptr_t sdhci;
	uintptr_t gcc;
	uintptr_t tlmm;
	uint64_t counter_hz;
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

static inline uint64_t
gta4l_emmc_probe_counter(void)
{
	uint64_t value;

	__asm__ volatile("mrs %0, cntvct_el0" : "=r" (value));
	return value;
}

static inline uint64_t
gta4l_emmc_probe_counter_hz(void)
{
	uint64_t value;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r" (value));
	/* The architectural counter frequency is the primary source. Keep a
	 * documented 19.2 MHz fallback if firmware left it unprogrammed. */
	if ((value < 1000000u) || (value > 1000000000u)) {
		value = 19200000u;
	}
	return value;
}

static uint8_t
gta4l_emmc_probe_read8(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return *(volatile uint8_t *)(probe->sdhci + offset);
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return *(volatile uint8_t *)(probe->gcc + offset);
	}
	return *(volatile uint8_t *)(probe->tlmm + offset);
}

static uint16_t
gta4l_emmc_probe_read16(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return *(volatile uint16_t *)(probe->sdhci + offset);
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return *(volatile uint16_t *)(probe->gcc + offset);
	}
	return *(volatile uint16_t *)(probe->tlmm + offset);
}

static uint32_t
gta4l_emmc_probe_read32(void *context, uint32_t space, uint32_t offset)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return *(volatile uint32_t *)(probe->sdhci + offset);
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return *(volatile uint32_t *)(probe->gcc + offset);
	}
	return *(volatile uint32_t *)(probe->tlmm + offset);
}

static void
gta4l_emmc_probe_write8(void *context, uint32_t space, uint32_t offset,
    uint8_t value)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		*(volatile uint8_t *)(probe->sdhci + offset) = value;
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		*(volatile uint8_t *)(probe->gcc + offset) = value;
	} else {
		*(volatile uint8_t *)(probe->tlmm + offset) = value;
	}
}

static void
gta4l_emmc_probe_write16(void *context, uint32_t space, uint32_t offset,
    uint16_t value)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		*(volatile uint16_t *)(probe->sdhci + offset) = value;
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		*(volatile uint16_t *)(probe->gcc + offset) = value;
	} else {
		*(volatile uint16_t *)(probe->tlmm + offset) = value;
	}
}

static void
gta4l_emmc_probe_write32(void *context, uint32_t space, uint32_t offset,
    uint32_t value)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		*(volatile uint32_t *)(probe->sdhci + offset) = value;
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		*(volatile uint32_t *)(probe->gcc + offset) = value;
	} else {
		*(volatile uint32_t *)(probe->tlmm + offset) = value;
	}
}

static void
gta4l_emmc_probe_barrier(void *context)
{
	(void)context;
	__asm__ volatile("dsb sy" ::: "memory");
}

static void
gta4l_emmc_probe_delay_us(void *context, uint32_t microseconds)
{
	struct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;
	uint64_t start;
	uint64_t ticks;
	uint64_t spins = 0;
	uint64_t limit;
	uint64_t hz = probe->counter_hz;

	if (hz == 0u) {
		hz = 19200000u;
	}
	ticks = (((uint64_t)microseconds * hz) + 999999u) / 1000000u;
	limit = ticks + 1000000u;
	start = gta4l_emmc_probe_counter();
	while ((gta4l_emmc_probe_counter() - start) < ticks) {
		if (++spins > limit) {
			/* A stalled counter would silently skip the settle
			 * interval. Stop the diagnostic instead. */
			panic("GTA4L-EMMC PROBE FAILED: generic counter "
			    "stalled (hz=%u us=%u)",
			    (unsigned int)hz,
			    (unsigned int)microseconds);
		}
		__asm__ volatile("yield");
	}
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
	return gta4l_emmc_probe_counter();
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
 * stage-A content pass and returns to legacy before its B2 report. */
static int
gta4l_emmc_probe_stage(void)
{
	int value = 0;

	if (!PE_parse_boot_argn("gta4l-emmc-probe", &value, sizeof(value))) {
		return 0;
	}
	if ((value < 1) || (value > 8)) {
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
	    (unsigned int)probe->counter_hz,
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
	    (unsigned int)probe->counter_hz,
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
	uint16_t arm;
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
	    "es=%04x arm=%04x\n"
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
	    (unsigned int)dto->arm,
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
	uint16_t expected;
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

	/* Force the minimum data timeout (2^13 timeout clocks) while keeping
	 * the clock-enable bits, then read the value back. */
	expected = (uint16_t)(dto.clk & ~GTA4L_EMMC_PROBE_TOUTVAL_MASK);
	gta4l_emmc_probe_write16(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL, expected);
	gta4l_emmc_probe_barrier(probe);
	dto.arm = gta4l_emmc_probe_read16(probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL);

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

	if (((dto.arm & GTA4L_EMMC_PROBE_TOUTVAL_MASK) ==
	    (expected & GTA4L_EMMC_PROBE_TOUTVAL_MASK)) &&
	    (dto.q_result == GTA4L_EMMC_OK)) {
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

	if ((dto.arm & GTA4L_EMMC_PROBE_TOUTVAL_MASK) !=
	    (expected & GTA4L_EMMC_PROBE_TOUTVAL_MASK)) {
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
	    (unsigned int)probe->counter_hz,
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
	vm_offset_t sdhci_mapping;
	vm_offset_t gcc_mapping;
	vm_offset_t tlmm_mapping;
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

	sdhci_mapping = ml_io_map(GTA4L_EMMC_PROBE_SDHCI_BASE,
	    GTA4L_EMMC_PROBE_WINDOW);
	gcc_mapping = ml_io_map(GTA4L_EMMC_PROBE_GCC_BASE,
	    GTA4L_EMMC_PROBE_WINDOW);
	tlmm_mapping = ml_io_map(GTA4L_EMMC_PROBE_TLMM_BASE,
	    GTA4L_EMMC_PROBE_WINDOW);
	if ((sdhci_mapping == 0) || (gcc_mapping == 0) ||
	    (tlmm_mapping == 0)) {
		panic("GTA4L-EMMC PROBE REFUSED: MMIO map failed "
		    "(sdhci=%u gcc=%u tlmm=%u)",
		    (unsigned int)(sdhci_mapping != 0),
		    (unsigned int)(gcc_mapping != 0),
		    (unsigned int)(tlmm_mapping != 0));
	}

	bzero(&probe, sizeof(probe));
	probe.sdhci = (uintptr_t)sdhci_mapping;
	probe.gcc = (uintptr_t)gcc_mapping;
	probe.tlmm = (uintptr_t)tlmm_mapping;
	probe.counter_hz = gta4l_emmc_probe_counter_hz();

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
		gta4l_emmc_probe_report_failure(&host, &probe, stage, result,
		    &regs);
	}

	version = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_MCI_VERSION);
	power = gta4l_emmc_probe_read8(&probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_POWER_CONTROL);
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
	regs.clock = clock;
	regs.host_control2 = host_control2;
	regs.host_version = host_version;
	regs.vendor_spec = vendor_spec;
	regs.dll_config = dll_config;
	regs.dll_status = dll_status;
	regs.pwr_status = pwr_status;

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

	/* Stage B1: switch the card/host to 4-bit then 8-bit, run the
	 * CMD19/CMD14 bus-test round trip at each width, and repeat the
	 * stage-A content reads so a ramp that disturbs data is visible. A
	 * failed step stops at that width: the report never issues further
	 * assertions on a bus that just failed. The 1-bit pass above is the
	 * baseline for the per-width comparison. */
	if (stage >= 4) {
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
