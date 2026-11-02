/*
 * Host-side simulated-MMIO harness for the gta4l eMMC probe failure report.
 *
 * The test runner copies Gta4lEmmcProbe.c, replaces the generic-counter and
 * barrier instructions and the six MMIO helpers with the hooks below, and
 * links it against this harness and minimal kernel-header stubs. The probe's
 * stage selection, conflict check, failure-report guard, register offsets and
 * panic strings remain the production code. Every hooked access is counted so
 * the tests can reject an SDHCI read while the branch clocks are off.
 *
 * The controller/host path and a minimal card-command engine are modelled: the
 * GCC block reset, the branch enable/CLK_OFF handshake, a configurable MCI
 * version, MMIO-map failure, CMD0/CMD1/CMD2 with a CMD2 CRC error, and the
 * identification sequence through CMD17 block reads. The full-probe failure
 * scenarios fail a single CMD17 with a data CRC or data timeout and check the
 * block-read failure report.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "Gta4lEmmc.h"

void gta4l_emmc_probe_run(void);
void gta4l_emmc_probe_width_fixture(int form);
uint64_t gta4l_emmc_probe_test_counter(void);
uint64_t gta4l_emmc_probe_test_counter_hz(void);
void gta4l_emmc_probe_test_delay_us(uint32_t microseconds);
uint8_t gta4l_emmc_probe_test_read8(uint32_t space, uint32_t offset);
uint16_t gta4l_emmc_probe_test_read16(uint32_t space, uint32_t offset);
uint32_t gta4l_emmc_probe_test_read32(uint32_t space, uint32_t offset);
void gta4l_emmc_probe_test_write8(uint32_t space, uint32_t offset,
    uint8_t value);
void gta4l_emmc_probe_test_write16(uint32_t space, uint32_t offset,
    uint16_t value);
void gta4l_emmc_probe_test_write32(uint32_t space, uint32_t offset,
    uint32_t value);

#define SIM_WINDOW        0x1000u
#define SIM_GCC_WORDS     (SIM_WINDOW / 4u)
#define SIM_SDHCI_BASE    0x04744000u
#define SIM_GCC_BASE      0x01438000u
#define SIM_TLMM_BASE     0x00575000u
#define SIM_COUNTER_HZ    1000000u
#define SIM_CBCR_ENABLE   0x00000001u
#define SIM_CBCR_CLK_OFF  0x80000000u
#define SIM_BCR_RESET     0x00000001u
#define SIM_VERSION_GOOD  0x10000072u
#define SIM_VERSION_BAD   0x20000000u
#define SIM_BRANCH_APPS   1
#define SIM_BRANCH_AHB    2
/* Independent copy of the SDHCI AUTO_CMD12 transfer-mode bit. The port must
 * leave it clear and issue CMD12 explicitly. */
#define SIM_TM_AUTO_CMD12 0x0004u

/* Synthetic GPT model. The production probe gates these values against the
 * live header, so the harness header must carry the same layout. These are
 * not copies of a real disk GUID or partition entry. */
#define SIM_GPT_ARRAY_LBA       2u
#define SIM_GPT_ARRAY_BLOCKS    21u
#define SIM_GPT_ENTRY_COUNT     84u
#define SIM_GPT_ENTRY_SIZE      128u
#define SIM_GPT_FIRST_USABLE    34u
#define SIM_GPT_BACKUP_SECTORS  33u

/* Independent expectations for the offsets the failure report samples. The
 * production constants are also checked here so a wrong constant cannot make
 * a read-count assertion agree with itself. */
_Static_assert(GTA4L_EMMC_REG_ARGUMENT == 0x08u, "ARGUMENT offset");
_Static_assert(GTA4L_EMMC_REG_COMMAND == 0x0Eu, "COMMAND offset");
_Static_assert(GTA4L_EMMC_REG_RESPONSE == 0x10u, "RESPONSE offset");
_Static_assert(GTA4L_EMMC_REG_PRESENT_STATE == 0x24u, "PRESENT_STATE offset");
_Static_assert(GTA4L_EMMC_REG_HOST_CONTROL == 0x28u, "HOST_CONTROL offset");
_Static_assert(GTA4L_EMMC_REG_POWER_CONTROL == 0x29u, "POWER_CONTROL offset");
_Static_assert(GTA4L_EMMC_REG_CLOCK_CONTROL == 0x2Cu, "CLOCK_CONTROL offset");
_Static_assert(GTA4L_EMMC_REG_SOFTWARE_RESET == 0x2Fu,
    "SOFTWARE_RESET offset");
_Static_assert(GTA4L_EMMC_REG_NORMAL_INT_STATUS == 0x30u,
    "NORMAL_INT_STATUS offset");
_Static_assert(GTA4L_EMMC_REG_ERROR_INT_STATUS == 0x32u,
    "ERROR_INT_STATUS offset");
_Static_assert(GTA4L_EMMC_REG_HOST_CONTROL2 == 0x3Eu, "HOST_CONTROL2 offset");
_Static_assert(GTA4L_EMMC_REG_HOST_VERSION == 0xFEu, "HOST_VERSION offset");
_Static_assert(GTA4L_EMMC_REG_CORE_DLL_CONFIG == 0x200u,
    "CORE_DLL_CONFIG offset");
_Static_assert(GTA4L_EMMC_REG_CORE_DLL_STATUS == 0x208u,
    "CORE_DLL_STATUS offset");
_Static_assert(GTA4L_EMMC_REG_CORE_VENDOR_SPEC == 0x20Cu,
    "CORE_VENDOR_SPEC offset");
_Static_assert(GTA4L_EMMC_REG_CORE_VENDOR_SPEC3 == 0x250u,
    "CORE_VENDOR_SPEC3 offset");
_Static_assert(GTA4L_EMMC_REG_CORE_DDR_CONFIG == 0x25Cu,
    "CORE_DDR_CONFIG offset");
_Static_assert(GTA4L_EMMC_REG_CORE_PWRCTL_STATUS == 0x240u,
    "CORE_PWRCTL_STATUS offset");
_Static_assert(GTA4L_EMMC_REG_CORE_PWRCTL_CLEAR == 0x248u,
    "CORE_PWRCTL_CLEAR offset");
_Static_assert(GTA4L_EMMC_REG_CORE_PWRCTL_CTL == 0x24Cu,
    "CORE_PWRCTL_CTL offset");
_Static_assert(GTA4L_EMMC_REG_MCI_VERSION == 0x318u, "MCI_VERSION offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_BCR == 0x00u, "SDCC1 BCR offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_CBCR == 0x04u,
    "SDCC1 APPS CBCR offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_AHB_CBCR == 0x08u,
    "SDCC1 AHB CBCR offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_CMD == 0x28u,
    "SDCC1 APPS CMD offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_CFG == 0x2Cu,
    "SDCC1 APPS CFG offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_M == 0x30u, "SDCC1 APPS M offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_N == 0x34u, "SDCC1 APPS N offset");
_Static_assert(GTA4L_EMMC_GCC_SDCC1_APPS_D == 0x38u, "SDCC1 APPS D offset");

static unsigned char sdhci[SIM_WINDOW];
static unsigned char gcc[SIM_WINDOW];
static unsigned char tlmm[SIM_WINDOW];
static jmp_buf panic_jmp;
static char panic_message[2048];
static int panic_ready;
static int stage;
static int conflict;
static int map_fail;
static int branch_stuck;
static int rcg_stuck;
static int clock_timeout;
static int bad_access;
static unsigned long sdhci_accesses;
static unsigned long sdhci_writes;
static unsigned long gcc_accesses;
static unsigned int sdhci_read_count[SIM_WINDOW];
static unsigned int gcc_read_count[SIM_WINDOW];
static uint64_t counter_value;
static uint16_t normal_int;
static uint16_t error_int;
static uint32_t pwr_status;
static uint16_t last_command;
static uint32_t last_argument;
static uint32_t last_index;
static unsigned int cmd_count;
static int bad_command_format;
static int bad_command_order;
static uint16_t block_size;
static uint16_t block_count;
static uint16_t transfer_mode;
static uint32_t buffer_data[GTA4L_EMMC_BLOCK_BYTES / 4u];
static uint32_t buffer_index;
static unsigned char ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
/* Stage-B1 bus-width modeling. cmd6_ignore leaves EXT_CSD[183] unchanged so
 * the readback disagrees with the requested code; cmd6_ignore_width8 does that
 * only for the 8-bit write. bus_test_bad returns the written pattern instead of
 * its complement at bus_test_bad_width (0 = every width). width_corrupt_lba
 * changes one CMD17 payload at width_corrupt_code so the per-width content
 * comparison differs. */
static unsigned int cmd6_count;
static uint32_t cmd6_argument;
static int cmd6_ignore;
static int cmd6_ignore_width8;
static uint32_t cmd6_status = 0x00000900u;
static int cmd6_late_commit;
static uint32_t cmd6_late_value;
static unsigned int cmd6_late_reads;
static unsigned int cmd8_after_cmd6;
static unsigned int cmd8_fail_on_after_cmd6;
static uint16_t cmd8_fail_error;
static int bus_test_bad;
static uint8_t bus_test_bad_width;
static uint8_t bus_test_written[GTA4L_EMMC_BUS_TEST_8_BYTES];
static unsigned int bus_test_written_count;
static uint16_t bus_test_width;
static uint32_t width_corrupt_lba = 0xFFFFFFFFu;
static uint8_t width_corrupt_code;
static uint8_t sim_host_width_code(void);
/* B1 readback-cause modeling. ext_csd_zero_width makes the CMD8 read at that
 * card width return an all-zero buffer without an error, which models a
 * data-path readback that never filled the buffer. host_width_ignore drops the
 * SDHCI width bits from the HOST_CONTROL write and makes the CMD8 read at a
 * switched width fail with a data CRC, which models a host width that did not
 * take effect. */
static uint8_t ext_csd_zero_width;
static int host_width_ignore;
static int cmd19_error;
static int cmd2_crc_fail;
static int cmd3_no_cc;
static int cmd3_late_cc;
static int cmd3_cc_delay;
static int cmd3_identify_status;
static int cmd9_timeout;
static unsigned int cmd17_fail_on;
static uint16_t cmd17_fail_error;
static unsigned int cmd17_count;
static unsigned int cmd17_index;
static int gpt_bad_field;
static int bad_command_argument;
static const uint32_t *expected_lbas;
static unsigned int expected_lba_count;
static int expected_lba_wrap;
static unsigned int cmd3_normal_reads;
static uint32_t ext_csd_count = 0x01000000u;
/* Multi-block CMD18/CMD12 modeling. mb_fail_done injects a data error after
 * that many fully drained sectors (0 disables it); mb_corrupt_lba changes the
 * CMD18 bytes for one sector so the in-device CMD17/CMD18 comparison can
 * differ; mb_stop_no_tc omits the CMD12 transfer completion. */
static int mb_mode_arg;
static int mb_fail_done;
static uint16_t mb_fail_error;
static uint32_t mb_argument;
static uint32_t mb_count;
static uint32_t mb_done;
static int mb_active;
static int mb_stop_no_tc;
static int mb_r1_error;
static uint32_t mb_corrupt_lba = 0xFFFFFFFFu;
/* Delayed multi-block events. Each delay is measured from the moment the
 * event is scheduled: mb_last_brr_delay_us from the penultimate sector drain,
 * mb_tc_delay_us from the final sector drain, and mb_error_delay_us from the
 * CMD18 issue. A negative delay keeps the immediate model. */
static int mb_tc_delay_us = -1;
static int mb_last_brr_delay_us = -1;
static int mb_error_delay_us = -1;
static int mb_no_tc;
static int mb_tc_pending;
static int mb_last_brr_pending;
static int mb_error_pending;
static uint64_t mb_tc_due;
static uint64_t mb_last_brr_due;
static uint64_t mb_error_due;
static unsigned int mb_stop_count;
static int auto_cmd12_seen;
static int bad_mb_command;
/* CMD7 R1b modeling. Event times are microseconds from command issue against
 * the simulated counter; -1 means the event never occurs. Status-read counts
 * are not the event clock. */
static int cmd7_cc_immediate;
static int cmd7_tc_immediate;
static int cmd7_cc_delay;
static int cmd7_tc_delay;
static int cmd7_error_delay;
static int cmd7_error_status;
static int cmd7_keep_response;
static int cmd7_fresh_response;
static int cmd7_toggle_status;
static int cmd7_access_latency_us;
static int cmd7_cc_due;
static int cmd7_tc_due;
static int cmd7_error_due;
static int cmd7_cc_time_valid;
static int cmd7_tc_time_valid;
static int cmd7_error_time_valid;
static uint64_t cmd7_cc_time;
static uint64_t cmd7_tc_time;
static uint64_t cmd7_error_time;
static uint64_t cmd7_issue_time;
static uint64_t cmd7_release_time;
static uint64_t cmd7_next_issue_time;
static int cmd7_next_index;
static int cmd7_early_command;
static int cmd7_command_readback;
static int response_read_before_cc;
static const uint32_t cmd7_window[4] = {
	0x40400CF6u, 0xDBFFEF0Fu, 0x5903FF00u, 0x00070000u
};
static const uint8_t *expected_order;
static unsigned int expected_order_count;

static const uint32_t sim_cid[4] = {
	0x15010044u, 0x5836384Du, 0x42009005u, 0xF4661900u
};
static const uint32_t sim_csd[4] = {
	0xD0270132u, 0x0F5903FFu, 0xF6DBFFEFu, 0x8E404000u
};

static uint32_t sim_load(const unsigned char *window, uint32_t offset,
    uint32_t width);
static void sim_store(unsigned char *window, uint32_t offset, uint32_t width,
    uint32_t value);
static void check(int condition, const char *message);

static void
sim_set_long_response(const uint32_t words[4])
{
	uint32_t reg[4];
	uint32_t index;

	reg[3] = words[0] >> 8;
	reg[2] = (words[1] >> 8) | ((words[0] & 0xFFu) << 24);
	reg[1] = (words[2] >> 8) | ((words[1] & 0xFFu) << 24);
	reg[0] = (words[3] >> 8) | ((words[2] & 0xFFu) << 24);
	for (index = 0; index < 4u; index++) {
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE + index * 4u, 4u,
		    reg[index]);
	}
}

static void
sim_fill_buffer_words(const uint8_t *bytes)
{
	uint32_t index;

	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index += 4u) {
		buffer_data[index / 4u] =
		    (uint32_t)bytes[index] |
		    ((uint32_t)bytes[index + 1u] << 8) |
		    ((uint32_t)bytes[index + 2u] << 16) |
		    ((uint32_t)bytes[index + 3u] << 24);
	}
}

static void
sim_put32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
	bytes[offset] = (uint8_t)(value & 0xFFu);
	bytes[offset + 1u] = (uint8_t)((value >> 8) & 0xFFu);
	bytes[offset + 2u] = (uint8_t)((value >> 16) & 0xFFu);
	bytes[offset + 3u] = (uint8_t)((value >> 24) & 0xFFu);
}

static void
sim_put64(uint8_t *bytes, uint32_t offset, uint64_t value)
{
	sim_put32(bytes, offset, (uint32_t)value);
	sim_put32(bytes, offset + 4u, (uint32_t)(value >> 32));
}

/* Synthetic protective GPT header for LBA 1. The backup and last-usable
 * values follow the simulated capacity; the array and first-usable values
 * are the pinned layout the production gate accepts. gpt_bad_field mutates
 * the entry count so the gate-refusal scenario has a live mismatch. */
static void
sim_gpt_block(uint8_t *bytes)
{
	uint64_t backup_lba = (uint64_t)ext_csd_count - 1u;
	uint64_t last_usable = backup_lba - SIM_GPT_BACKUP_SECTORS;
	uint32_t index;

	memset(bytes, 0, GTA4L_EMMC_BLOCK_BYTES);
	for (index = 0; index < 8u; index++) {
		bytes[index] = (uint8_t)"EFI PART"[index];
	}
	sim_put32(bytes, 0x08u, 0x00010000u);
	sim_put32(bytes, 0x0Cu, 92u);
	sim_put32(bytes, 0x10u, 0x5A5A5A5Au);
	sim_put64(bytes, 0x18u, 1u);
	sim_put64(bytes, 0x20u, backup_lba);
	sim_put64(bytes, 0x28u, SIM_GPT_FIRST_USABLE);
	sim_put64(bytes, 0x30u, last_usable);
	for (index = 0; index < 16u; index++) {
		bytes[0x38u + index] = (uint8_t)(0x11u * (index + 1u));
	}
	sim_put64(bytes, 0x48u, SIM_GPT_ARRAY_LBA);
	sim_put32(bytes, 0x50u,
	    (gpt_bad_field != 0) ? (SIM_GPT_ENTRY_COUNT - 1u) :
	    SIM_GPT_ENTRY_COUNT);
	sim_put32(bytes, 0x54u, SIM_GPT_ENTRY_SIZE);
	sim_put32(bytes, 0x58u, 0x12345678u);
}

static void
sim_fill_block(uint32_t lba)
{
	uint8_t bytes[GTA4L_EMMC_BLOCK_BYTES];
	uint32_t index;

	if (lba == 1u) {
		sim_gpt_block(bytes);
	} else {
		for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
			bytes[index] = (uint8_t)((lba * 7u) + (index * 13u));
		}
	}
	if ((width_corrupt_code != 0u) &&
	    ((ext_csd[GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF] ==
	    width_corrupt_code) ||
	    (sim_host_width_code() == width_corrupt_code)) &&
	    (lba == width_corrupt_lba)) {
		bytes[0] ^= 0xFFu;
	}
	sim_fill_buffer_words(bytes);
}

/* CMD18 payload. Only the selected sector differs from the CMD17 pass, so a
 * report that substitutes the CMD17 sums for the CMD18 sums is caught. */
static void
sim_fill_mb_block(uint32_t lba)
{
	uint8_t bytes[GTA4L_EMMC_BLOCK_BYTES];
	uint32_t index;

	if (lba == mb_corrupt_lba) {
		for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
			bytes[index] =
			    (uint8_t)((lba * 7u) + (index * 13u) + 1u);
		}
		sim_fill_buffer_words(bytes);
		return;
	}
	sim_fill_block(lba);
}

static uint32_t
sim_block_sum(uint32_t lba)
{
	uint8_t bytes[GTA4L_EMMC_BLOCK_BYTES];
	uint32_t sum = 0;
	uint32_t index;

	if (lba == 1u) {
		sim_gpt_block(bytes);
	} else {
		for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
			bytes[index] = (uint8_t)((lba * 7u) + (index * 13u));
		}
	}
	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
		sum = (sum * 31u) + bytes[index];
	}
	return sum;
}

/* CMD18-pass sum for one LBA, including the optional corruption. */
static uint32_t
sim_mb_block_sum(uint32_t lba)
{
	uint8_t bytes[GTA4L_EMMC_BLOCK_BYTES];
	uint32_t sum = 0;
	uint32_t index;

	if (lba == mb_corrupt_lba) {
		for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
			bytes[index] =
			    (uint8_t)((lba * 7u) + (index * 13u) + 1u);
		}
	} else {
		return sim_block_sum(lba);
	}
	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
		sum = (sum * 31u) + bytes[index];
	}
	return sum;
}

/* Expected substring for the per-LBA content line. */
static void
sim_expected_blocks_label(char *buffer, size_t size, const char *label,
    int cmd18)
{
	size_t used;
	uint32_t index;
	int written;

	written = snprintf(buffer, size, "GTA4L-EMMC PROBE %s", label);
	if ((written < 0) || ((size_t)written >= size)) {
		check(0, "the expected BLOCKS line does not fit its buffer");
		return;
	}
	used = (size_t)written;
	for (index = 0; index < SIM_GPT_ARRAY_BLOCKS; index++) {
		uint32_t lba = SIM_GPT_ARRAY_LBA + index;
		uint32_t sum = (cmd18 != 0) ? sim_mb_block_sum(lba) :
		    sim_block_sum(lba);

		written = snprintf(buffer + used, size - used, " %u=%08x",
		    lba, sum);
		if ((written < 0) || ((size_t)written >= (size - used))) {
			check(0, "the expected BLOCKS line does not fit its buffer");
			return;
		}
		used += (size_t)written;
	}
}

static void
sim_expected_blocks(char *buffer, size_t size)
{
	sim_expected_blocks_label(buffer, size, "BLOCKS", 0);
}

/* Expected Stage-B1 BLOCKS line for one width. */
static void
sim_expected_b1_blocks(char *buffer, size_t size, unsigned int width)
{
	size_t used;
	uint32_t index;
	int written;

	written = snprintf(buffer, size, "GTA4L-EMMC B1 BLOCKS %u", width);
	if ((written < 0) || ((size_t)written >= size)) {
		check(0, "the expected B1 BLOCKS line does not fit its buffer");
		return;
	}
	used = (size_t)written;
	for (index = 0; index < SIM_GPT_ARRAY_BLOCKS; index++) {
		uint32_t lba = SIM_GPT_ARRAY_LBA + index;

		written = snprintf(buffer + used, size - used, " %u=%08x",
		    lba, sim_block_sum(lba));
		if ((written < 0) || ((size_t)written >= (size - used))) {
			check(0, "the expected B1 BLOCKS line does not fit its "
			    "buffer");
			return;
		}
		used += (size_t)written;
	}
}

static uint32_t
sim_load(const unsigned char *window, uint32_t offset, uint32_t width)
{
	uint32_t value = 0;
	uint32_t index;

	if ((offset > SIM_WINDOW) || (width > SIM_WINDOW) ||
	    ((offset + width) > SIM_WINDOW)) {
		bad_access = 1;
		return 0;
	}
	for (index = 0; index < width; index++) {
		value |= (uint32_t)window[offset + index] << (8u * index);
	}
	return value;
}

/* The data-path width the controller currently drives, derived from
 * HOST_CONTROL. A B1C continuation applies 4-bit at the host while the card's
 * EXT_CSD byte 183 still reads 0, so content faults must key on this value too
 * (1 = 4-bit, 2 = 8-bit, 0 = 1-bit). */
static uint8_t
sim_host_width_code(void)
{
	uint8_t control = (uint8_t)sim_load(sdhci,
	    GTA4L_EMMC_REG_HOST_CONTROL, 1u);

	if ((control & GTA4L_EMMC_HOST_CONTROL_8BIT) != 0u) {
		return GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8;
	}
	if ((control & GTA4L_EMMC_HOST_CONTROL_4BIT) != 0u) {
		return GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
	}
	return GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1;
}
static void
sim_store(unsigned char *window, uint32_t offset, uint32_t width,
    uint32_t value)
{
	uint32_t index;

	if ((offset > SIM_WINDOW) || (width > SIM_WINDOW) ||
	    ((offset + width) > SIM_WINDOW)) {
		bad_access = 1;
		return;
	}
	for (index = 0; index < width; index++) {
		window[offset + index] =
		    (unsigned char)((value >> (8u * index)) & 0xFFu);
	}
}

/* Model the GCC SDCC1 block reset and branch-clock handshake. */
static void
sim_gcc_write32(uint32_t offset, uint32_t value)
{
	if ((offset == GTA4L_EMMC_GCC_SDCC1_APPS_CMD) && (rcg_stuck == 0)) {
		/* The RCG update bit is self-clearing. */
		value &= ~GTA4L_EMMC_RCG_CMD_UPDATE;
	}
	sim_store(gcc, offset, 4u, value);
	if (offset == GTA4L_EMMC_GCC_SDCC1_BCR) {
		if ((value & SIM_BCR_RESET) == 0) {
			return;
		}
		sim_store(gcc, GTA4L_EMMC_GCC_SDCC1_BCR, 4u, 0);
		/* Reset asserts CLK_OFF but preserves the other CBCR fields,
		 * so a seeded handoff value survives the reset. */
		sim_store(gcc, GTA4L_EMMC_GCC_SDCC1_APPS_CBCR, 4u,
		    sim_load(gcc, GTA4L_EMMC_GCC_SDCC1_APPS_CBCR, 4u) |
		    SIM_CBCR_CLK_OFF);
		sim_store(gcc, GTA4L_EMMC_GCC_SDCC1_AHB_CBCR, 4u,
		    sim_load(gcc, GTA4L_EMMC_GCC_SDCC1_AHB_CBCR, 4u) |
		    SIM_CBCR_CLK_OFF);
		return;
	}
	if (((offset == GTA4L_EMMC_GCC_SDCC1_APPS_CBCR) &&
	    (branch_stuck == SIM_BRANCH_APPS)) ||
	    ((offset == GTA4L_EMMC_GCC_SDCC1_AHB_CBCR) &&
	    (branch_stuck == SIM_BRANCH_AHB))) {
		if ((value & SIM_CBCR_ENABLE) != 0) {
			sim_store(gcc, offset, 4u, value | SIM_CBCR_CLK_OFF);
		}
		return;
	}
	if ((value & SIM_CBCR_ENABLE) != 0) {
		sim_store(gcc, offset, 4u, value & ~SIM_CBCR_CLK_OFF);
	}
}

static unsigned char *
sim_window(uint32_t space)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return sdhci;
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return gcc;
	}
	return tlmm;
}

static void
sim_clear_response(void)
{
	uint32_t index;

	for (index = 0; index < 4u; index++) {
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE + index * 4u, 4u, 0);
	}
}

static void
sim_apply_cmd7_events(void)
{
	if (last_index != GTA4L_EMMC_CMD7) {
		return;
	}
	if ((cmd7_cc_due == 0) && (cmd7_cc_time_valid != 0) &&
	    (counter_value >= cmd7_cc_time)) {
		normal_int |= GTA4L_EMMC_INT_CC;
		cmd7_cc_due = 1;
	}
	if ((cmd7_tc_due == 0) && (cmd7_tc_time_valid != 0) &&
	    (counter_value >= cmd7_tc_time)) {
		normal_int |= GTA4L_EMMC_INT_TC;
		cmd7_tc_due = 1;
		cmd7_release_time = counter_value;
	}
	if ((cmd7_error_due == 0) && (cmd7_error_time_valid != 0) &&
	    (counter_value >= cmd7_error_time)) {
		error_int |= (uint16_t)cmd7_error_status;
		cmd7_error_due = 1;
	}
}

/* Delayed CMD18 events used by the post-bound drain scenarios. A scheduled
 * error wins over pending data and completion events. */
static void
sim_apply_mb_events(void)
{
	if (last_index != GTA4L_EMMC_CMD18) {
		return;
	}
	if ((mb_error_pending != 0) &&
	    (counter_value >= mb_error_due)) {
		mb_error_pending = 0;
		mb_tc_pending = 0;
		mb_last_brr_pending = 0;
		mb_active = 0;
		normal_int &= (uint16_t)~GTA4L_EMMC_INT_BRR;
		error_int |= mb_fail_error;
		return;
	}
	if ((mb_last_brr_pending != 0) &&
	    (counter_value >= mb_last_brr_due)) {
		mb_last_brr_pending = 0;
		sim_fill_mb_block(mb_argument + mb_done);
		normal_int |= GTA4L_EMMC_INT_BRR;
	}
	if ((mb_tc_pending != 0) && (counter_value >= mb_tc_due)) {
		mb_tc_pending = 0;
		normal_int |= GTA4L_EMMC_INT_TC;
	}
}

/* Minimal command engine for the stage-3 scenarios. CMD0 and CMD1 complete
 * normally; CMD2 can fail with CMD_CRC and synthetic R2 words, CMD3 can omit
 * or delay its completion, and the identification sequence can complete so the
 * success report path is exercised. */
static void
sim_command(uint16_t command)
{
	uint32_t index = ((uint32_t)command >> 8) & 0x3Fu;
	uint32_t flags = (uint32_t)command & 0xFFu;
	uint32_t words[4];
	uint32_t word;
	uint64_t cc_time;
	uint64_t tc_time;

	sim_apply_cmd7_events();
	if ((last_index == GTA4L_EMMC_CMD7) &&
	    (index != GTA4L_EMMC_CMD7)) {
		if (cmd7_tc_due == 0) {
			cmd7_early_command = 1;
		}
		cmd7_next_issue_time = counter_value;
		cmd7_next_index = (int)index;
	}
	last_command = command;
	last_index = index;
	if (cmd_count < expected_order_count) {
		if (index != expected_order[cmd_count]) {
			bad_command_order = 1;
		}
	} else {
		bad_command_order = 1;
	}
	cmd_count++;
	buffer_index = 0;
	if (index == 0u) {
		if (flags != GTA4L_EMMC_CMD_RESP_NONE) {
			bad_command_format = 1;
		}
		normal_int |= GTA4L_EMMC_INT_CC;
		return;
	}
	if (index == 1u) {
		if (flags != GTA4L_EMMC_CMD_RESP_SHORT) {
			bad_command_format = 1;
		}
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u,
		    GTA4L_EMMC_OCR_VALUE | 0x80000000u);
		normal_int |= GTA4L_EMMC_INT_CC;
		return;
	}
	if (index == 2u) {
		if (flags != (GTA4L_EMMC_CMD_RESP_LONG | GTA4L_EMMC_CMD_CRC)) {
			bad_command_format = 1;
		}
		if (cmd2_crc_fail != 0) {
			/* Synthetic R2 words: this is a formatting/capture
			 * test, not a real CID. */
			words[0] = 0x11111111u;
			words[1] = 0x22222222u;
			words[2] = 0x33333333u;
			words[3] = 0x44444444u;
			for (word = 0; word < 4u; word++) {
				sim_store(sdhci,
				    GTA4L_EMMC_REG_RESPONSE + word * 4u, 4u,
				    words[word]);
			}
			error_int |= GTA4L_EMMC_INT_ERR_CMD_CRC;
			normal_int |= 0x8000u;
			return;
		}
		sim_set_long_response(sim_cid);
		normal_int |= GTA4L_EMMC_INT_CC;
		return;
	}
	if (index == GTA4L_EMMC_CMD3) {
		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX)) {
			bad_command_format = 1;
		}
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u,
		    cmd3_identify_status ? 0x00000500u : 0x00000900u);
		cmd3_normal_reads = 0;
		if ((cmd3_no_cc == 0) && (cmd3_late_cc == 0) &&
		    (cmd3_cc_delay < 0)) {
			normal_int |= GTA4L_EMMC_INT_CC;
		}
		return;
	}
	if (index == GTA4L_EMMC_CMD9) {
		if (flags != (GTA4L_EMMC_CMD_RESP_LONG |
		    GTA4L_EMMC_CMD_CRC)) {
			bad_command_format = 1;
		}
		sim_set_long_response(sim_csd);
		if (cmd9_timeout == 0) {
			normal_int |= GTA4L_EMMC_INT_CC;
		}
		return;
	}
	if (index == GTA4L_EMMC_CMD7) {
		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT_BUSY |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX)) {
			bad_command_format = 1;
		}
		cmd7_issue_time = counter_value;
		cmd7_cc_due = 0;
		cmd7_tc_due = 0;
		cmd7_error_due = 0;
		cmd7_command_readback = 0;
		cmd7_cc_time_valid =
		    ((cmd7_cc_immediate != 0) || (cmd7_cc_delay >= 0)) ? 1 : 0;
		cmd7_tc_time_valid =
		    ((cmd7_tc_immediate != 0) || (cmd7_tc_delay >= 0)) ? 1 : 0;
		cmd7_error_time_valid = (cmd7_error_delay >= 0) ? 1 : 0;
		cc_time = counter_value;
		tc_time = counter_value;
		if ((cmd7_cc_immediate == 0) && (cmd7_cc_delay >= 0)) {
			cc_time += (uint64_t)cmd7_cc_delay;
		}
		if ((cmd7_tc_immediate == 0) && (cmd7_tc_delay >= 0)) {
			tc_time += (uint64_t)cmd7_tc_delay;
		}
		cmd7_cc_time = cc_time;
		cmd7_tc_time = tc_time;
		cmd7_error_time = counter_value +
		    ((cmd7_error_delay >= 0) ?
		    (uint64_t)cmd7_error_delay : 0);
		if (cmd7_keep_response == 0) {
			if (cmd7_fresh_response != 0) {
				for (word = 0; word < 4u; word++) {
					sim_store(sdhci,
					    GTA4L_EMMC_REG_RESPONSE +
					    word * 4u, 4u,
					    cmd7_window[word]);
				}
			} else {
				sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE,
				    4u, 0x00000900u);
			}
		}
		sim_apply_cmd7_events();
		return;
	}
	if (index == GTA4L_EMMC_CMD13) {
		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX)) {
			bad_command_format = 1;
		}
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u, 0x00000900u);
		normal_int |= GTA4L_EMMC_INT_CC;
		return;
	}
	if (index == GTA4L_EMMC_CMD6) {
		uint32_t value;

		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX)) {
			bad_command_format = 1;
		}
		cmd6_count++;
		cmd6_argument = last_argument;
		value = (last_argument >> 8) & 0xFFu;
		if ((cmd6_status & GTA4L_EMMC_R1_ERROR_MASK) == 0u) {
			cmd8_after_cmd6 = 0;
			if (cmd6_late_commit != 0) {
				/* The write is invisible to the verification
				 * readback and appears on a later CMD8, which
				 * models a late BUS_WIDTH commit. */
				cmd6_late_value = value;
				cmd6_late_reads = 0;
			} else if ((cmd6_ignore == 0) &&
			    ((cmd6_ignore_width8 == 0) ||
			    (value != 2u))) {
				ext_csd[GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF] =
				    (uint8_t)value;
			}
		}
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u, cmd6_status);
		normal_int |= GTA4L_EMMC_INT_CC;
		return;
	}
	if (index == GTA4L_EMMC_CMD19) {
		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX |
		    GTA4L_EMMC_CMD_DATA)) {
			bad_command_format = 1;
		}
		if ((block_count != 1u) ||
		    ((transfer_mode & GTA4L_EMMC_TM_READ) != 0) ||
		    ((transfer_mode & GTA4L_EMMC_TM_BLOCK_COUNT_EN) == 0)) {
			bad_command_format = 1;
		}
		bus_test_width = block_size;
		bus_test_written_count = 0;
		if (cmd19_error != 0) {
			error_int |= (uint16_t)cmd19_error;
			return;
		}
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u, 0x00000900u);
		normal_int |= GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_BWR;
		return;
	}
	if (index == GTA4L_EMMC_CMD14) {
		uint8_t payload[GTA4L_EMMC_BUS_TEST_8_BYTES];
		uint32_t index2;

		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX |
		    GTA4L_EMMC_CMD_DATA)) {
			bad_command_format = 1;
		}
		if ((block_count != 1u) ||
		    (block_size != bus_test_width) ||
		    ((transfer_mode & (GTA4L_EMMC_TM_READ |
		    GTA4L_EMMC_TM_BLOCK_COUNT_EN)) !=
		    (GTA4L_EMMC_TM_READ | GTA4L_EMMC_TM_BLOCK_COUNT_EN))) {
			bad_command_format = 1;
		}
		if (bus_test_width > GTA4L_EMMC_BUS_TEST_8_BYTES) {
			bad_command_format = 1;
			bus_test_width = GTA4L_EMMC_BUS_TEST_8_BYTES;
		}
		for (index2 = 0; index2 < bus_test_width; index2++) {
			if ((bus_test_bad != 0) &&
			    ((bus_test_bad_width == 0u) ||
			    (bus_test_bad_width ==
			    (uint8_t)ext_csd[
			    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF]) ||
			    (bus_test_bad_width ==
			    (uint8_t)(bus_test_width / 4u)))) {
				payload[index2] = bus_test_written[index2];
			} else {
				payload[index2] =
				    (uint8_t)~bus_test_written[index2];
			}
		}
		for (index2 = 0; index2 < bus_test_width; index2 += 4u) {
			buffer_data[index2 / 4u] =
			    (uint32_t)payload[index2] |
			    ((uint32_t)payload[index2 + 1u] << 8) |
			    ((uint32_t)payload[index2 + 2u] << 16) |
			    ((uint32_t)payload[index2 + 3u] << 24);
		}
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u, 0x00000900u);
		normal_int |= GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_BRR |
		    GTA4L_EMMC_INT_TC;
		return;
	}
	if (index == GTA4L_EMMC_CMD18) {
		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX |
		    GTA4L_EMMC_CMD_DATA)) {
			bad_command_format = 1;
		}
		if ((block_size != GTA4L_EMMC_BLOCK_BYTES) ||
		    (block_count != SIM_GPT_ARRAY_BLOCKS) ||
		    (last_argument != SIM_GPT_ARRAY_LBA) ||
		    ((transfer_mode & (GTA4L_EMMC_TM_READ |
		    GTA4L_EMMC_TM_BLOCK_COUNT_EN |
		    GTA4L_EMMC_TM_MULTI)) !=
		    (GTA4L_EMMC_TM_READ | GTA4L_EMMC_TM_BLOCK_COUNT_EN |
		    GTA4L_EMMC_TM_MULTI))) {
			bad_mb_command = 1;
		}
		if ((transfer_mode & SIM_TM_AUTO_CMD12) != 0) {
			auto_cmd12_seen = 1;
		}
		mb_argument = last_argument;
		mb_count = block_count;
		mb_done = 0;
		mb_active = 1;
		mb_tc_pending = 0;
		mb_last_brr_pending = 0;
		mb_error_pending = 0;
		if (mb_error_delay_us >= 0) {
			mb_error_pending = 1;
			mb_error_due = counter_value +
			    (uint64_t)mb_error_delay_us;
		}
		buffer_index = 0;
		sim_fill_mb_block(mb_argument);
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u,
		    (mb_r1_error != 0) ? 0x00000080u : 0x00000900u);
		normal_int |= GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_BRR;
		return;
	}
	if (index == GTA4L_EMMC_CMD12) {
		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT_BUSY |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX)) {
			bad_command_format = 1;
		}
		if ((last_argument != 0u) ||
		    (mb_count != SIM_GPT_ARRAY_BLOCKS) ||
		    (mb_argument != SIM_GPT_ARRAY_LBA)) {
			bad_mb_command = 1;
		}
		mb_stop_count++;
		mb_active = 0;
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u, 0x00000900u);
		normal_int |= GTA4L_EMMC_INT_CC;
		if (mb_stop_no_tc == 0) {
			normal_int |= GTA4L_EMMC_INT_TC;
		}
		return;
	}
	if ((index == GTA4L_EMMC_CMD8) || (index == GTA4L_EMMC_CMD17)) {
		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX |
		    GTA4L_EMMC_CMD_DATA)) {
			bad_command_format = 1;
		}
		if ((block_size != GTA4L_EMMC_BLOCK_BYTES) ||
		    (block_count != 1u) ||
		    ((transfer_mode & (GTA4L_EMMC_TM_READ |
		    GTA4L_EMMC_TM_BLOCK_COUNT_EN)) !=
		    (GTA4L_EMMC_TM_READ | GTA4L_EMMC_TM_BLOCK_COUNT_EN))) {
			bad_command_format = 1;
		}
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u, 0x00000900u);
		if (index == GTA4L_EMMC_CMD8) {
			if (cmd6_count != 0u) {
				cmd8_after_cmd6++;
			}
			if ((cmd8_fail_on_after_cmd6 != 0u) &&
			    (cmd8_after_cmd6 == cmd8_fail_on_after_cmd6)) {
				error_int |= cmd8_fail_error;
				return;
			}
			if (cmd6_late_commit != 0) {
				if (cmd6_late_reads == 0u) {
					cmd6_late_reads = 1u;
				} else {
					ext_csd[
					    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF] =
					    (uint8_t)cmd6_late_value;
					cmd6_late_commit = 0;
				}
			}
			if ((host_width_ignore != 0) &&
			    (ext_csd[GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF] != 0u)) {
				error_int |= GTA4L_EMMC_INT_ERR_DATA_CRC;
				return;
			}
			if ((ext_csd_zero_width != 0u) &&
			    (ext_csd[GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF] ==
			    ext_csd_zero_width)) {
				memset(buffer_data, 0, sizeof(buffer_data));
			} else {
				sim_fill_buffer_words(ext_csd);
			}
		} else {
			cmd17_count++;
			if ((expected_lba_count == 0u) ||
			    ((expected_lba_wrap == 0) &&
			    ((cmd17_index >= expected_lba_count) ||
			    (last_argument != expected_lbas[cmd17_index]))) ||
			    ((expected_lba_wrap != 0) &&
			    (last_argument !=
			    expected_lbas[cmd17_index % expected_lba_count]))) {
				bad_command_argument = 1;
			}
			cmd17_index++;
			if (cmd17_count == cmd17_fail_on) {
				error_int |= cmd17_fail_error;
				return;
			}
			sim_fill_block(last_argument);
		}
		normal_int |= GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_BRR |
		    GTA4L_EMMC_INT_TC;
		return;
	}
	/* No later command is expected. Report a CRC error so an unexpected
	 * command fails the scenario instead of hanging. */
	error_int |= GTA4L_EMMC_INT_ERR_CMD_CRC;
	normal_int |= 0x8000u;
}

uint8_t
gta4l_emmc_probe_test_read8(uint32_t space, uint32_t offset)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		if (offset < SIM_WINDOW) {
			sdhci_read_count[offset]++;
		}
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
		if (offset < SIM_WINDOW) {
			gcc_read_count[offset]++;
		}
	}
	return (uint8_t)sim_load(sim_window(space), offset, 1u);
}

uint16_t
gta4l_emmc_probe_test_read16(uint32_t space, uint32_t offset)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		if (offset < SIM_WINDOW) {
			sdhci_read_count[offset]++;
		}
		if (offset == GTA4L_EMMC_REG_NORMAL_INT_STATUS) {
			sim_apply_mb_events();
			if (last_index == GTA4L_EMMC_CMD3) {
				cmd3_normal_reads++;
				if ((cmd3_late_cc != 0) &&
				    (cmd3_normal_reads >
				    (GTA4L_EMMC_CMD_TIMEOUT_US /
				    GTA4L_EMMC_POLL_STEP_US))) {
					normal_int |= GTA4L_EMMC_INT_CC;
				}
				if ((cmd3_cc_delay >= 0) &&
				    (cmd3_normal_reads >
				    (unsigned int)cmd3_cc_delay)) {
					normal_int |= GTA4L_EMMC_INT_CC;
				}
			}
			if (last_index == GTA4L_EMMC_CMD7) {
				sim_apply_cmd7_events();
				if (cmd7_toggle_status != 0) {
					normal_int ^= GTA4L_EMMC_INT_BRR;
				}
			}
			return normal_int;
		}
		if (offset == GTA4L_EMMC_REG_ERROR_INT_STATUS) {
			sim_apply_mb_events();
			if (last_index == GTA4L_EMMC_CMD7) {
				sim_apply_cmd7_events();
			}
			return error_int;
		}
		if ((offset == GTA4L_EMMC_REG_COMMAND) &&
		    (last_index == GTA4L_EMMC_CMD7)) {
			/* Terminal captures read the command readback before
			 * the raw response window; mark them so the
			 * before-CC check covers only in-loop snapshots. */
			cmd7_command_readback = 1;
		}
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
		if (offset < SIM_WINDOW) {
			gcc_read_count[offset]++;
		}
	}
	return (uint16_t)sim_load(sim_window(space), offset, 2u);
}

uint32_t
gta4l_emmc_probe_test_read32(uint32_t space, uint32_t offset)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		if (offset < SIM_WINDOW) {
			sdhci_read_count[offset]++;
		}
		if ((offset >= GTA4L_EMMC_REG_RESPONSE) &&
		    (offset < (GTA4L_EMMC_REG_RESPONSE + 16u)) &&
		    (last_index == GTA4L_EMMC_CMD7)) {
			if ((cmd7_cc_due == 0) &&
			    (cmd7_command_readback == 0)) {
				response_read_before_cc = 1;
			}
			if (cmd7_access_latency_us > 0) {
				counter_value +=
				    (uint64_t)cmd7_access_latency_us;
				sim_apply_cmd7_events();
			}
		}
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_STATUS) {
			return pwr_status;
		}
		if (offset == GTA4L_EMMC_REG_BUFFER_DATA_PORT) {
			uint32_t word = 0;

			if (buffer_index <
			    (GTA4L_EMMC_BLOCK_BYTES / 4u)) {
				word = buffer_data[buffer_index];
			}
			buffer_index++;
			if ((mb_active != 0) &&
			    (buffer_index >=
			    (GTA4L_EMMC_BLOCK_BYTES / 4u))) {
				/* One full CMD18 sector was drained. Stage the
				 * next sector, inject the configured data
				 * error, or raise the transfer completion
				 * after the last sector. */
				buffer_index = 0;
				mb_done++;
				if ((mb_fail_done != 0) &&
				    (mb_done == (uint32_t)mb_fail_done)) {
					mb_active = 0;
					normal_int &= (uint16_t)
					    ~GTA4L_EMMC_INT_BRR;
					error_int |= mb_fail_error;
				} else if (mb_done < mb_count) {
					if ((mb_done == (mb_count - 1u)) &&
					    (mb_last_brr_delay_us >= 0)) {
						normal_int &= (uint16_t)
						    ~GTA4L_EMMC_INT_BRR;
						mb_last_brr_pending = 1;
						mb_last_brr_due = counter_value +
						    (uint64_t)mb_last_brr_delay_us;
					} else {
						sim_fill_mb_block(mb_argument +
						    mb_done);
					}
				} else {
					mb_active = 0;
					normal_int &= (uint16_t)
					    ~GTA4L_EMMC_INT_BRR;
					if (mb_no_tc == 0) {
						if (mb_tc_delay_us >= 0) {
							mb_tc_pending = 1;
							mb_tc_due = counter_value +
							    (uint64_t)mb_tc_delay_us;
						} else {
							normal_int |=
							    GTA4L_EMMC_INT_TC;
						}
					}
				}
			}
			return word;
		}
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
		if (offset < SIM_WINDOW) {
			gcc_read_count[offset]++;
		}
	}
	return sim_load(sim_window(space), offset, 4u);
}

void
gta4l_emmc_probe_test_write8(uint32_t space, uint32_t offset, uint8_t value)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		sdhci_writes++;
		/* The software-reset bits are self-clearing. Model the
		 * completion so later stages can be exercised. */
		if (offset == GTA4L_EMMC_REG_SOFTWARE_RESET) {
			value = 0;
		}
		if ((offset == GTA4L_EMMC_REG_HOST_CONTROL) &&
		    (host_width_ignore != 0)) {
			value &= (uint8_t)~(GTA4L_EMMC_HOST_CONTROL_4BIT |
			    GTA4L_EMMC_HOST_CONTROL_8BIT);
		}
		if ((offset == GTA4L_EMMC_REG_POWER_CONTROL) &&
		    ((value & GTA4L_EMMC_POWER_BUS_POWER) != 0)) {
			pwr_status |= GTA4L_EMMC_PWRCTL_BUS_ON;
		}
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
	}
	sim_store(sim_window(space), offset, 1u, value);
}

void
gta4l_emmc_probe_test_write16(uint32_t space, uint32_t offset, uint16_t value)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		sdhci_writes++;
		if (offset == GTA4L_EMMC_REG_BLOCK_SIZE) {
			block_size = value;
		}
		if (offset == GTA4L_EMMC_REG_BLOCK_COUNT) {
			block_count = value;
		}
		if (offset == GTA4L_EMMC_REG_TRANSFER_MODE) {
			transfer_mode = value;
		}
		if (offset == GTA4L_EMMC_REG_COMMAND) {
			sim_store(sdhci, offset, 2u, value);
			sim_command(value);
			return;
		}
		if (offset == GTA4L_EMMC_REG_NORMAL_INT_STATUS) {
			normal_int &= (uint16_t)~value;
			sim_store(sdhci, offset, 2u, 0);
			return;
		}
		if (offset == GTA4L_EMMC_REG_ERROR_INT_STATUS) {
			uint16_t pending = (uint16_t)(error_int &
			    GTA4L_EMMC_INT_ERR_CMD_CRC);

			error_int &= (uint16_t)~value;
			if ((value & pending) != 0) {
				sim_clear_response();
			}
			sim_store(sdhci, offset, 2u, 0);
			return;
		}
		if (offset == GTA4L_EMMC_REG_CLOCK_CONTROL) {
			sim_store(sdhci, offset, 2u, value);
			if (((value & GTA4L_EMMC_CLOCK_INT_EN) != 0) &&
			    (clock_timeout == 0)) {
				value |= GTA4L_EMMC_CLOCK_INT_STABLE;
				sim_store(sdhci, offset, 2u, value);
			}
			return;
		}
		if (offset == GTA4L_EMMC_REG_HOST_CONTROL2) {
			uint16_t previous = (uint16_t)sim_load(sdhci, offset,
			    2u);

			sim_store(sdhci, offset, 2u, value);
			if (((value & GTA4L_EMMC_HOST_CONTROL2_VDD_180) != 0) !=
			    ((previous &
			    GTA4L_EMMC_HOST_CONTROL2_VDD_180) != 0)) {
				if ((value &
				    GTA4L_EMMC_HOST_CONTROL2_VDD_180) != 0) {
					pwr_status |=
					    GTA4L_EMMC_PWRCTL_IO_LOW;
				} else {
					pwr_status |=
					    GTA4L_EMMC_PWRCTL_IO_HIGH;
				}
			}
			return;
		}
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
	}
	sim_store(sim_window(space), offset, 2u, value);
}

void
gta4l_emmc_probe_test_write32(uint32_t space, uint32_t offset, uint32_t value)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sdhci_accesses++;
		sdhci_writes++;
		if (offset == GTA4L_EMMC_REG_ARGUMENT) {
			last_argument = value;
		}
		if (offset == GTA4L_EMMC_REG_BUFFER_DATA_PORT) {
			uint32_t base = bus_test_written_count;

			if ((last_index == GTA4L_EMMC_CMD19) &&
			    ((base + 4u) <= sizeof(bus_test_written))) {
				bus_test_written[base] = (uint8_t)value;
				bus_test_written[base + 1u] =
				    (uint8_t)(value >> 8);
				bus_test_written[base + 2u] =
				    (uint8_t)(value >> 16);
				bus_test_written[base + 3u] =
				    (uint8_t)(value >> 24);
				bus_test_written_count += 4u;
				if (bus_test_written_count >=
				    bus_test_width) {
					normal_int &= (uint16_t)
					    ~GTA4L_EMMC_INT_BWR;
					normal_int |= GTA4L_EMMC_INT_TC;
				}
			}
			return;
		}
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_CLEAR) {
			pwr_status &= ~value;
			sim_store(sdhci, offset, 4u, 0);
			return;
		}
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_CTL) {
			sim_store(sdhci, offset, 4u, value);
			return;
		}
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		gcc_accesses++;
		sim_gcc_write32(offset, value);
		return;
	}
	sim_store(sim_window(space), offset, 4u, value);
}

uint64_t
gta4l_emmc_probe_test_counter(void)
{
	return counter_value;
}

void
gta4l_emmc_probe_test_delay_us(uint32_t microseconds)
{
	counter_value += ((uint64_t)microseconds * SIM_COUNTER_HZ +
	    999999u) / 1000000u;
	sim_apply_cmd7_events();
	sim_apply_mb_events();
}

uint64_t
gta4l_emmc_probe_test_counter_hz(void)
{
	return SIM_COUNTER_HZ;
}

int
PE_parse_boot_argn(const char *arg, void *value, int size)
{
	if (size < (int)sizeof(int)) {
		return 0;
	}
	if (strcmp(arg, "gta4l-emmc-probe") == 0) {
		*(int *)value = stage;
		return 1;
	}
	if (strcmp(arg, "gta4l-emmc-mb") == 0) {
		*(int *)value = mb_mode_arg;
		return 1;
	}
	if (conflict != 0) {
		if ((strcmp(arg, "gta4l-sdhci-audit") == 0) ||
		    (strcmp(arg, "gta4l-sdhci-snapshot") == 0) ||
		    (strcmp(arg, "gta4l-sdhci-host-init") == 0)) {
			*(int *)value = 1;
			return 1;
		}
	}
	return 0;
}

vm_offset_t
ml_io_map(vm_offset_t phys, vm_size_t size)
{
	(void)size;
	if (map_fail != 0) {
		return 0;
	}
	switch (phys) {
	case SIM_SDHCI_BASE:
		return (vm_offset_t)(uintptr_t)sdhci;
	case SIM_GCC_BASE:
		return (vm_offset_t)(uintptr_t)gcc;
	case SIM_TLMM_BASE:
		return (vm_offset_t)(uintptr_t)tlmm;
	default:
		return 0;
	}
}

void
panic(const char *format, ...)
{
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(panic_message, sizeof(panic_message), format,
	    arguments);
	va_end(arguments);
	if ((length < 0) || ((size_t)length >= sizeof(panic_message))) {
		fprintf(stderr, "panic report truncated: %d bytes into %zu\n",
		    length, sizeof(panic_message));
		exit(2);
	}
	if (panic_ready == 0) {
		fprintf(stderr, "unexpected panic: %s\n", panic_message);
		exit(2);
	}
	longjmp(panic_jmp, 1);
}

static int failures;
static int panicked;
static int width_form;

static void
check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

/* Report-buffer predicate shared by the synthetic width fixtures and the
 * report-budget negative control. A report of `length` bytes needs one more
 * byte for the NUL terminator. */
static int
report_budget_accepts(size_t length)
{
	return ((length + 1u) <= sizeof(panic_message)) ? 1 : 0;
}

static void
sim_init_ext_csd(void)
{
	uint32_t count = ext_csd_count;

	memset(ext_csd, 0, sizeof(ext_csd));
	ext_csd[GTA4L_EMMC_EXT_CSD_REV_OFF] = 8u;
	ext_csd[GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF] =
	    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1;
	ext_csd[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF + 0] =
	    (uint8_t)(count & 0xFFu);
	ext_csd[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF + 1] =
	    (uint8_t)((count >> 8) & 0xFFu);
	ext_csd[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF + 2] =
	    (uint8_t)((count >> 16) & 0xFFu);
	ext_csd[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF + 3] =
	    (uint8_t)((count >> 24) & 0xFFu);
}

static uint32_t
sim_ext_csd_sum(void)
{
	uint32_t sum = 0;
	uint32_t index;

	for (index = 0; index < GTA4L_EMMC_EXT_CSD_BYTES; index++) {
		sum += ext_csd[index];
	}
	return sum;
}

static void
sim_init(void)
{
	sim_store(sdhci, GTA4L_EMMC_REG_MCI_VERSION, 4u, SIM_VERSION_GOOD);
	sim_store(sdhci, GTA4L_EMMC_REG_HOST_VERSION, 2u, 0x1234u);
	cmd7_cc_immediate = 1;
	cmd7_tc_immediate = 0;
	cmd3_cc_delay = -1;
	cmd7_cc_delay = -1;
	cmd7_tc_delay = -1;
	cmd7_error_delay = -1;
	cmd17_index = 0;
	bad_command_argument = 0;
	sim_init_ext_csd();
}

static const uint8_t order_cmd2[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2
};
static const uint8_t order_cmd3[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3
};
static const uint8_t order_cmd9[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9
};
static const uint8_t order_ident[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8
};
static const uint8_t order_probe_block0[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17
};
static const uint8_t order_probe_block1[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17
};
static const uint8_t order_probe_block2[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17
};
static const uint8_t order_probe_block8[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17
};
static const uint8_t order_probe_full[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17
};
/* The MD pass replaces nothing in the CMD17 sequence: the same 33 commands,
 * then one open-ended CMD18 and one explicit CMD12. */
static const uint8_t order_probe_md[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD18, GTA4L_EMMC_CMD12
};
/* Stage B1 keeps the stage-A 1-bit content pass, then repeats the switch,
 * bus test and content pass at 4-bit and 8-bit. A failing scenario stops
 * early, so the same prefix-checked order also covers the failure forms. */
static const uint8_t order_probe_b1[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD14,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD14,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17
};
/* Stage B1C adds exactly the continuation after the failed 4-bit readback:
 * one CMD19/CMD14 round trip, the 25-value content pass, then a second CMD8
 * re-read. A non-entering or partial run is a prefix of this order. */
static const uint8_t order_probe_b1c[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD14,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD8
};
static const uint8_t order_cmd7[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7
};

/* Expected CMD17 argument sequence for each success or failure path. */
static const uint32_t lbas_block0[] = {
	0u
};
static const uint32_t lbas_block1[] = {
	0u, 1u
};
static const uint32_t lbas_array_first[] = {
	0u, 1u, 2u
};
static const uint32_t lbas_array_mid[] = {
	0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u
};
static const uint32_t lbas_full[] = {
	0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u,
	14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 34u, 61071359u
};
static const uint32_t lbas_full_max[] = {
	0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u,
	14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 34u, 0xFFFFFFFEu
};

static void
set_expected_order(const uint8_t *order, unsigned int count)
{
	expected_order = order;
	expected_order_count = count;
}

static void
set_expected_lbas(const uint32_t *lbas, unsigned int count)
{
	expected_lbas = lbas;
	expected_lba_count = count;
}

int
main(int argc, char **argv)
{
	const char *scenario = (argc > 1) ? argv[1] : "branch-apps";

	sim_init();
	if (strcmp(scenario, "stage-off") == 0) {
		stage = 0;
	} else if (strcmp(scenario, "map-fail") == 0) {
		stage = 2;
		map_fail = 1;
	} else if (strcmp(scenario, "conflict") == 0) {
		stage = 1;
		conflict = 1;
	} else if (strcmp(scenario, "branch-apps") == 0) {
		stage = 2;
		branch_stuck = SIM_BRANCH_APPS;
	} else if (strcmp(scenario, "branch-ahb") == 0) {
		stage = 2;
		branch_stuck = SIM_BRANCH_AHB;
	} else if (strcmp(scenario, "version-fail") == 0) {
		stage = 2;
		sim_store(sdhci, GTA4L_EMMC_REG_MCI_VERSION, 4u,
		    SIM_VERSION_BAD);
	} else if (strcmp(scenario, "clock-timeout") == 0) {
		/* Keep the RCG update bit set so configure_clock() fails after
		 * programming the 400 kHz RCG. */
		stage = 2;
		rcg_stuck = 1;
	} else if (strcmp(scenario, "clock-stable-timeout") == 0) {
		/* Leave CLOCK_CONTROL without INT_STABLE so configure_clock()
		 * fails after the RCG update completed. */
		stage = 2;
		clock_timeout = 1;
	} else if (strcmp(scenario, "cmd2-crc") == 0) {
		/* Run the full identification sequence and fail the first
		 * CRC-checked command with a synthetic R2 response. */
		stage = 3;
		cmd2_crc_fail = 1;
		set_expected_order(order_cmd2,
		    (unsigned int)(sizeof(order_cmd2) / sizeof(order_cmd2[0])));
	} else if (strcmp(scenario, "cmd3-no-cc") == 0) {
		/* CMD3 never raises the completion interrupt. */
		stage = 3;
		cmd3_no_cc = 1;
		set_expected_order(order_cmd3,
		    (unsigned int)(sizeof(order_cmd3) / sizeof(order_cmd3[0])));
	} else if (strcmp(scenario, "cmd3-late-cc") == 0) {
		/* CMD3 raises the completion interrupt only after the polling
		 * loop has already timed out and taken its fresh sample. */
		stage = 3;
		cmd3_late_cc = 1;
		set_expected_order(order_cmd3,
		    (unsigned int)(sizeof(order_cmd3) / sizeof(order_cmd3[0])));
	} else if (strcmp(scenario, "cmd3-identify") == 0) {
		/* Complete identification through EXT_CSD so the success
		 * report carries the populated captures. */
		stage = 3;
		cmd7_tc_immediate = 1;
		set_expected_order(order_ident,
		    (unsigned int)(sizeof(order_ident) / sizeof(order_ident[0])));
	} else if (strcmp(scenario, "cmd9-timeout") == 0) {
		/* CMD3 completes but CMD9 times out, so the timeout capture is
		 * for a different command than the CMD3 poll history. */
		stage = 3;
		cmd9_timeout = 1;
		set_expected_order(order_cmd9,
		    (unsigned int)(sizeof(order_cmd9) / sizeof(order_cmd9[0])));
	} else if (strcmp(scenario, "cmd7-tc-only") == 0) {
		/* Owner-transcribed failure: TC is set, CC is absent, and the
		 * retained response window is the CMD9 tail. */
		stage = 3;
		cmd7_cc_immediate = 0;
		cmd7_tc_immediate = 1;
		cmd7_keep_response = 1;
		set_expected_order(order_cmd7,
		    (unsigned int)(sizeof(order_cmd7) / sizeof(order_cmd7[0])));
	} else if (strcmp(scenario, "cmd7-fresh-window") == 0) {
		/* Same TC-only timeout with a response window that changes at
		 * CMD7 submission. */
		stage = 3;
		cmd7_cc_immediate = 0;
		cmd7_tc_immediate = 1;
		cmd7_fresh_response = 1;
		set_expected_order(order_cmd7,
		    (unsigned int)(sizeof(order_cmd7) / sizeof(order_cmd7[0])));
	} else if (strcmp(scenario, "cmd7-cc-then-tc") == 0) {
		/* CC arrives immediately; the busy release arrives 50 us
		 * later. Both are required for success. */
		stage = 3;
		cmd7_cc_immediate = 1;
		cmd7_tc_immediate = 0;
		cmd7_tc_delay = 50;
		set_expected_order(order_ident,
		    (unsigned int)(sizeof(order_ident) / sizeof(order_ident[0])));
	} else if (strcmp(scenario, "cmd7-tc-then-cc") == 0) {
		stage = 3;
		cmd7_cc_immediate = 0;
		cmd7_tc_immediate = 1;
		cmd7_cc_delay = 10;
		set_expected_order(order_ident,
		    (unsigned int)(sizeof(order_ident) / sizeof(order_ident[0])));
	} else if (strcmp(scenario, "cmd7-simultaneous") == 0) {
		stage = 3;
		cmd7_cc_immediate = 1;
		cmd7_tc_immediate = 1;
		set_expected_order(order_ident,
		    (unsigned int)(sizeof(order_ident) / sizeof(order_ident[0])));
	} else if (strcmp(scenario, "cmd7-last-poll") == 0) {
		/* Both completions become due on the last permitted poll. */
		stage = 3;
		cmd7_cc_immediate = 0;
		cmd7_cc_delay = (int)(GTA4L_EMMC_CMD_TIMEOUT_US -
		    GTA4L_EMMC_POLL_STEP_US);
		cmd7_tc_immediate = 0;
		cmd7_tc_delay = (int)(GTA4L_EMMC_CMD_TIMEOUT_US -
		    GTA4L_EMMC_POLL_STEP_US);
		set_expected_order(order_ident,
		    (unsigned int)(sizeof(order_ident) / sizeof(order_ident[0])));
	} else if (strcmp(scenario, "cmd7-cc-only") == 0) {
		/* CC arrives and the busy release never does, so the
		 * identification fails without a downstream command. */
		stage = 3;
		cmd7_cc_immediate = 1;
		cmd7_tc_immediate = 0;
		set_expected_order(order_cmd7,
		    (unsigned int)(sizeof(order_cmd7) / sizeof(order_cmd7[0])));
	} else if (strcmp(scenario, "cmd7-busy-error") == 0) {
		stage = 3;
		cmd7_cc_immediate = 1;
		cmd7_tc_immediate = 1;
		cmd7_error_delay = 0;
		cmd7_error_status = GTA4L_EMMC_INT_ERR_CMD_TIMEOUT;
		set_expected_order(order_cmd7,
		    (unsigned int)(sizeof(order_cmd7) / sizeof(order_cmd7[0])));
	} else if (strcmp(scenario, "cmd7-late-error") == 0) {
		/* Both completions arrive first; the busy-phase error becomes
		 * due during the explicit response-read latency and is
		 * evaluated at the terminal sample. */
		stage = 3;
		cmd7_cc_immediate = 1;
		cmd7_tc_immediate = 1;
		cmd7_access_latency_us = 1;
		cmd7_error_delay = 1;
		cmd7_error_status = GTA4L_EMMC_INT_ERR_CMD_TIMEOUT;
		set_expected_order(order_cmd7,
		    (unsigned int)(sizeof(order_cmd7) / sizeof(order_cmd7[0])));
	} else if (strcmp(scenario, "cmd7-terminal-error") == 0) {
		/* Both completions arrive first. The error is not due at the
		 * first terminal error sample; it becomes due during the
		 * terminal command/argument/response reads and must be caught
		 * by the final error sample before the status clear. */
		stage = 3;
		cmd7_cc_immediate = 1;
		cmd7_tc_immediate = 1;
		cmd7_access_latency_us = 1;
		cmd7_error_delay = 2;
		cmd7_error_status = GTA4L_EMMC_INT_ERR_CMD_TIMEOUT;
		set_expected_order(order_cmd7,
		    (unsigned int)(sizeof(order_cmd7) / sizeof(order_cmd7[0])));
	} else if (strcmp(scenario, "cmd7-trace-overflow") == 0) {
		stage = 3;
		cmd7_cc_immediate = 0;
		cmd7_toggle_status = 1;
		set_expected_order(order_cmd7,
		    (unsigned int)(sizeof(order_cmd7) / sizeof(order_cmd7[0])));
	} else if (strcmp(scenario, "report-max-width-failure") == 0) {
		/* Longest failure form: both poll budgets run to their limit and
		 * the CMD7 trace fills its four entries and overflows. */
		stage = 3;
		cmd3_cc_delay = 99999;
		cmd7_cc_immediate = 0;
		cmd7_toggle_status = 1;
		set_expected_order(order_cmd7,
		    (unsigned int)(sizeof(order_cmd7) / sizeof(order_cmd7[0])));
	} else if (strcmp(scenario, "report-max-width-success") == 0) {
		/* Maximum-width identification success form: capacity, both
		 * poll budgets at their limit and a full CMD7 trace. */
		stage = 3;
		ext_csd_count = 0xFFFFFFFFu;
		sim_init_ext_csd();
		cmd3_cc_delay = 99999;
		cmd7_cc_immediate = 0;
		cmd7_cc_delay = (int)(GTA4L_EMMC_CMD_TIMEOUT_US -
		    GTA4L_EMMC_POLL_STEP_US);
		cmd7_tc_immediate = 0;
		cmd7_tc_delay = (int)(GTA4L_EMMC_CMD_TIMEOUT_US -
		    GTA4L_EMMC_POLL_STEP_US);
		cmd7_toggle_status = 1;
		set_expected_order(order_ident,
		    (unsigned int)(sizeof(order_ident) / sizeof(order_ident[0])));
	} else if (strcmp(scenario, "report-max-width-probe") == 0) {
		/* Longest success form: the full probe with sector reads,
		 * maximum-width capacity, both poll budgets at their limit and
		 * a full CMD7 trace. */
		stage = 1;
		ext_csd_count = 0xFFFFFFFFu;
		sim_init_ext_csd();
		cmd3_cc_delay = 99999;
		cmd7_cc_immediate = 0;
		cmd7_cc_delay = (int)(GTA4L_EMMC_CMD_TIMEOUT_US -
		    GTA4L_EMMC_POLL_STEP_US);
		cmd7_tc_immediate = 0;
		cmd7_tc_delay = (int)(GTA4L_EMMC_CMD_TIMEOUT_US -
		    GTA4L_EMMC_POLL_STEP_US);
		cmd7_toggle_status = 1;
		set_expected_order(order_probe_full,
		    (unsigned int)(sizeof(order_probe_full) /
		    sizeof(order_probe_full[0])));
		set_expected_lbas(lbas_full_max,
		    (unsigned int)(sizeof(lbas_full_max) /
		    sizeof(lbas_full_max[0])));
	} else if (strcmp(scenario, "probe-full") == 0) {
		/* Device-capacity success form: block 0/1, the 21-sector
		 * primary GPT array, the first usable sector and the last
		 * sector. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		set_expected_order(order_probe_full,
		    (unsigned int)(sizeof(order_probe_full) /
		    sizeof(order_probe_full[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
	} else if (strcmp(scenario, "probe-gpt-mismatch") == 0) {
		/* Live header entry count differs from the pinned list. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		gpt_bad_field = 1;
		set_expected_order(order_probe_block1,
		    (unsigned int)(sizeof(order_probe_block1) /
		    sizeof(order_probe_block1[0])));
		set_expected_lbas(lbas_block1,
		    (unsigned int)(sizeof(lbas_block1) /
		    sizeof(lbas_block1[0])));
	} else if (strcmp(scenario, "probe-read-crc") == 0) {
		/* Identification succeeds; the first sector read fails its
		 * data CRC check. */
		stage = 1;
		cmd7_tc_immediate = 1;
		cmd17_fail_on = 1;
		cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		set_expected_order(order_probe_block0,
		    (unsigned int)(sizeof(order_probe_block0) /
		    sizeof(order_probe_block0[0])));
		set_expected_lbas(lbas_block0,
		    (unsigned int)(sizeof(lbas_block0) /
		    sizeof(lbas_block0[0])));
	} else if (strcmp(scenario, "probe-read-timeout") == 0) {
		/* Block 0 is read; block 1 fails its data timeout. */
		stage = 1;
		cmd7_tc_immediate = 1;
		cmd17_fail_on = 2;
		cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_TIMEOUT;
		set_expected_order(order_probe_block1,
		    (unsigned int)(sizeof(order_probe_block1) /
		    sizeof(order_probe_block1[0])));
		set_expected_lbas(lbas_block1,
		    (unsigned int)(sizeof(lbas_block1) /
		    sizeof(lbas_block1[0])));
	} else if (strcmp(scenario, "probe-array-crc") == 0) {
		/* Block 0/1 pass; the first array block fails its data CRC. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		cmd17_fail_on = 3;
		cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		set_expected_order(order_probe_block2,
		    (unsigned int)(sizeof(order_probe_block2) /
		    sizeof(order_probe_block2[0])));
		set_expected_lbas(lbas_array_first,
		    (unsigned int)(sizeof(lbas_array_first) /
		    sizeof(lbas_array_first[0])));
	} else if (strcmp(scenario, "probe-mid-loop-error") == 0) {
		/* The sixth array block (LBA 7) fails its data timeout. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		cmd17_fail_on = 8;
		cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_TIMEOUT;
		set_expected_order(order_probe_block8,
		    (unsigned int)(sizeof(order_probe_block8) /
		    sizeof(order_probe_block8[0])));
		set_expected_lbas(lbas_array_mid,
		    (unsigned int)(sizeof(lbas_array_mid) /
		    sizeof(lbas_array_mid[0])));
	} else if (strcmp(scenario, "probe-last-error") == 0) {
		/* Every read passes except the last sector. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		cmd17_fail_on = 25;
		cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		set_expected_order(order_probe_full,
		    (unsigned int)(sizeof(order_probe_full) /
		    sizeof(order_probe_full[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
	} else if (strcmp(scenario, "probe-md-success") == 0) {
		/* Full probe plus the optional MD pass: the same 21 sectors
		 * read as one CMD18 transfer and stopped explicitly. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		mb_mode_arg = 1;
		set_expected_order(order_probe_md,
		    (unsigned int)(sizeof(order_probe_md) /
		    sizeof(order_probe_md[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
	} else if (strcmp(scenario, "probe-md-mismatch") == 0) {
		/* One CMD18 sector differs from its CMD17 counterpart. This
		 * is a content mismatch, not a hardware failure: the report
		 * keeps rc=0 and names the first differing LBA. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		mb_mode_arg = 1;
		mb_corrupt_lba = 7u;
		set_expected_order(order_probe_md,
		    (unsigned int)(sizeof(order_probe_md) /
		    sizeof(order_probe_md[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
	} else if (strcmp(scenario, "probe-md-mid-fail") == 0) {
		/* Three CMD18 sectors drain, then the data timeout fires. The
		 * CMD18 stop is still attempted and succeeds. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		mb_mode_arg = 1;
		mb_fail_done = 3;
		mb_fail_error = GTA4L_EMMC_INT_ERR_DATA_TIMEOUT;
		set_expected_order(order_probe_md,
		    (unsigned int)(sizeof(order_probe_md) /
		    sizeof(order_probe_md[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
	} else if (strcmp(scenario, "probe-md-stop-no-tc") == 0) {
		/* Every sector drains, but CMD12 returns CC without TC, so
		 * the stop times out and the report names the stop result. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		mb_mode_arg = 1;
		mb_stop_no_tc = 1;
		set_expected_order(order_probe_md,
		    (unsigned int)(sizeof(order_probe_md) /
		    sizeof(order_probe_md[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
	} else if (strcmp(scenario, "probe-md-drain-tc") == 0) {
		/* Every sector drains, but TC arrives only inside the
		 * post-bound window; the pass succeeds and reports the drain
		 * resolution. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		mb_mode_arg = 1;
		mb_tc_delay_us = (int)GTA4L_EMMC_DATA_TIMEOUT_US + 1000;
		set_expected_order(order_probe_md,
		    (unsigned int)(sizeof(order_probe_md) /
		    sizeof(order_probe_md[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
	} else if (strcmp(scenario, "probe-md-drain-brr") == 0) {
		/* The last sector becomes buffer-read ready only inside the
		 * post-bound window; the drain reads it and TC follows. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		mb_mode_arg = 1;
		mb_last_brr_delay_us = (int)GTA4L_EMMC_DATA_TIMEOUT_US + 1000;
		set_expected_order(order_probe_md,
		    (unsigned int)(sizeof(order_probe_md) /
		    sizeof(order_probe_md[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
	} else if (strcmp(scenario, "probe-md-drain-error") == 0) {
		/* Every sector drains but TC is withheld; a data CRC error
		 * becomes visible inside the post-bound window and keeps its
		 * own result code. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		mb_mode_arg = 1;
		mb_no_tc = 1;
		mb_error_delay_us = (int)GTA4L_EMMC_DATA_TIMEOUT_US + 1000;
		mb_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		set_expected_order(order_probe_md,
		    (unsigned int)(sizeof(order_probe_md) /
		    sizeof(order_probe_md[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
	} else if (strcmp(scenario, "probe-md-stall-tc") == 0) {
		/* Attempt-2 replay: every sector drains and TC never comes;
		 * the pass reports an exhausted post-bound window. */
		stage = 1;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		mb_mode_arg = 1;
		mb_no_tc = 1;
		set_expected_order(order_probe_md,
		    (unsigned int)(sizeof(order_probe_md) /
		    sizeof(order_probe_md[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
	} else if (strcmp(scenario, "probe-mb-bare-refuse") == 0) {
		/* The modifier cannot stand alone: no probe argument means no
		 * MMIO mapping and an explicit refusal. */
		stage = 0;
		mb_mode_arg = 1;
	} else if ((strcmp(scenario, "probe-b1-success") == 0) ||
	    (strcmp(scenario, "probe-b1-stuck-width") == 0) ||
	    (strcmp(scenario, "probe-b1-zero-readback") == 0) ||
	    (strcmp(scenario, "probe-b1-host-width-stuck") == 0) ||
	    (strcmp(scenario, "probe-b1-bustest-fail") == 0) ||
	    (strcmp(scenario, "probe-b1-switch8-stuck") == 0) ||
	    (strcmp(scenario, "probe-b1-bustest8-fail") == 0) ||
	    (strcmp(scenario, "probe-b1-content8-div") == 0)) {
		/* Stage B1 starts from the same device-capacity 1-bit pass,
		 * then switches to 4-bit and 8-bit. Each failure scenario
		 * injects one model fault and stops at that width. */
		stage = 4;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		set_expected_order(order_probe_b1,
		    (unsigned int)(sizeof(order_probe_b1) /
		    sizeof(order_probe_b1[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if (strcmp(scenario, "probe-b1-stuck-width") == 0) {
			cmd6_ignore = 1;
		} else if (strcmp(scenario, "probe-b1-zero-readback") == 0) {
			ext_csd_zero_width = GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		} else if (strcmp(scenario,
		    "probe-b1-host-width-stuck") == 0) {
			host_width_ignore = 1;
		} else if (strcmp(scenario, "probe-b1-bustest-fail") == 0) {
			bus_test_bad = 1;
			bus_test_bad_width = GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		} else if (strcmp(scenario, "probe-b1-switch8-stuck") == 0) {
			cmd6_ignore_width8 = 1;
		} else if (strcmp(scenario, "probe-b1-bustest8-fail") == 0) {
			bus_test_bad = 1;
			bus_test_bad_width = GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8;
		} else if (strcmp(scenario,
		    "probe-b1-content8-div") == 0) {
			width_corrupt_lba = SIM_GPT_FIRST_USABLE;
			width_corrupt_code = GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8;
		}
	} else if ((strcmp(scenario, "probe-b1c-success") == 0) ||
	    (strcmp(scenario, "probe-b1c-pass") == 0) ||
	    (strcmp(scenario, "probe-b1c-late-commit") == 0) ||
	    (strcmp(scenario, "probe-b1c-bustest-fail") == 0) ||
	    (strcmp(scenario, "probe-b1c-content-div") == 0) ||
	    (strcmp(scenario, "probe-b1c-content-error") == 0) ||
	    (strcmp(scenario, "probe-b1c-reread-error") == 0) ||
	    (strcmp(scenario, "probe-b1c-c6-error") == 0) ||
	    (strcmp(scenario, "probe-b1c-zero-readback") == 0) ||
	    (strcmp(scenario, "probe-b1c-host-width-stuck") == 0)) {
		/* Stage B1C: the same 1-bit baseline and 4-bit switch.
		 * probe-b1c-success runs the normal B1 escalation (no
		 * continuation). The other scenarios model the observed
		 * readback tuple and one continuation outcome each. */
		stage = 5;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		if (strcmp(scenario, "probe-b1c-success") == 0) {
			/* No continuation: the normal two-rung escalation. */
			set_expected_order(order_probe_b1,
			    (unsigned int)(sizeof(order_probe_b1) /
			    sizeof(order_probe_b1[0])));
		} else {
			set_expected_order(order_probe_b1c,
			    (unsigned int)(sizeof(order_probe_b1c) /
			    sizeof(order_probe_b1c[0])));
		}
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if (strcmp(scenario, "probe-b1c-pass") == 0) {
			cmd6_ignore = 1;
		} else if (strcmp(scenario,
		    "probe-b1c-late-commit") == 0) {
			cmd6_late_commit = 1;
		} else if (strcmp(scenario,
		    "probe-b1c-bustest-fail") == 0) {
			cmd6_ignore = 1;
			bus_test_bad = 1;
			bus_test_bad_width = GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		} else if (strcmp(scenario,
		    "probe-b1c-content-div") == 0) {
			cmd6_ignore = 1;
			width_corrupt_lba = SIM_GPT_FIRST_USABLE;
			width_corrupt_code = GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		} else if (strcmp(scenario,
		    "probe-b1c-content-error") == 0) {
			cmd6_ignore = 1;
			cmd17_fail_on = 26u;
			cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario,
		    "probe-b1c-reread-error") == 0) {
			cmd6_ignore = 1;
			cmd8_fail_on_after_cmd6 = 2u;
			cmd8_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario, "probe-b1c-c6-error") == 0) {
			cmd6_status = 0x0000A000u;
		} else if (strcmp(scenario,
		    "probe-b1c-zero-readback") == 0) {
			ext_csd_zero_width = GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		} else if (strcmp(scenario,
		    "probe-b1c-host-width-stuck") == 0) {
			host_width_ignore = 1;
		}
	} else if (strcmp(scenario, "report-width-failure") == 0) {
		/* Synthetic field-wise width bound, not a controller state. */
		width_form = 1;
	} else if (strcmp(scenario, "report-width-ident") == 0) {
		width_form = 2;
	} else if (strcmp(scenario, "report-width-probe") == 0) {
		width_form = 3;
	} else if (strcmp(scenario, "report-width-md") == 0) {
		width_form = 4;
	} else if (strcmp(scenario, "report-width-b1") == 0) {
		/* Synthetic B1 field-wise bound: the report samples the pad and
		 * DLL registers live, so put them at their 32-bit maximum. */
		width_form = 5;
		sim_store(tlmm, GTA4L_EMMC_TLMM_SDC1_QDSD, 4u, 0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_CONFIG, 4u,
		    0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_STATUS, 4u,
		    0xFFFFFFFFu);
	} else if (strcmp(scenario, "report-width-budget-control") == 0) {
		/* Negative control for the report-budget predicate: the last
		 * byte that still leaves room for the NUL must be accepted and
		 * the first byte past the buffer must be rejected. */
		check(report_budget_accepts(sizeof(panic_message) - 1u),
		    "the report-budget predicate rejected a fitting report");
		check(!report_budget_accepts(sizeof(panic_message)),
		    "the report-budget predicate accepted an oversized report");
		if (failures != 0) {
			return 1;
		}
		printf("PASS %s\n", scenario);
		return 0;
	} else if (strcmp(scenario, "oversized-report") == 0) {
		/* Negative control for the report guard: a message that cannot
		 * fit the 2048-byte buffer must be rejected explicitly, not
		 * truncated and treated as a report. */
		char oversized[2100];

		memset(oversized, 'A', sizeof(oversized) - 1u);
		oversized[sizeof(oversized) - 1u] = '\0';
		panic_ready = 1;
		panic("%s", oversized);
		return 2;
	} else {
		fprintf(stderr, "unknown scenario %s\n", scenario);
		return 2;
	}

	if (setjmp(panic_jmp) == 0) {
		panic_ready = 1;
		if (width_form != 0) {
			gta4l_emmc_probe_width_fixture(width_form - 1);
		} else {
			gta4l_emmc_probe_run();
		}
	} else {
		panicked = 1;
	}
	panic_ready = 0;

	check(bad_access == 0, "a simulated register access left its window");

	if (strcmp(scenario, "stage-off") == 0) {
		check(panicked == 0, "a disabled probe still panicked");
		check(sdhci_accesses == 0ul,
		    "a disabled probe touched SDHCI");
		check(gcc_accesses == 0ul, "a disabled probe touched GCC");
	} else if (strcmp(scenario, "map-fail") == 0) {
		check(panicked == 1, "an MMIO map failure did not panic");
		check(strstr(panic_message, "MMIO map failed") != NULL,
		    "panic does not name the MMIO map failure");
		check(sdhci_accesses == 0ul,
		    "an MMIO map failure touched SDHCI");
		check(gcc_accesses == 0ul, "an MMIO map failure touched GCC");
	} else if (strcmp(scenario, "conflict") == 0) {
		check(panicked == 1, "a conflicting argument was not refused");
		check(strstr(panic_message, "REFUSED") != NULL,
		    "panic does not name the argument conflict");
		check(sdhci_accesses == 0ul,
		    "a refused probe touched SDHCI");
		check(gcc_accesses == 0ul, "a refused probe touched GCC");
	} else if (strcmp(scenario, "version-fail") == 0) {
		check(panicked == 1, "a bad MCI version did not panic");
		check(strstr(panic_message, "clocks=1") != NULL,
		    "the report does not show the verified branch clocks");
		/* The core reads the version once before failing, so the
		 * report's own sample makes this count two; every other
		 * diagnostic register is read exactly once by the report. */
		check(sdhci_read_count[GTA4L_EMMC_REG_MCI_VERSION] == 2u,
		    "the report did not sample the MCI version");
		check(sdhci_read_count[GTA4L_EMMC_REG_POWER_CONTROL] == 1u,
		    "the report did not sample the power control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL] == 1u,
		    "the report did not sample HOST_CONTROL");
		check(sdhci_read_count[GTA4L_EMMC_REG_CLOCK_CONTROL] == 1u,
		    "the report did not sample the clock control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL2] == 1u,
		    "the report did not sample HOST_CONTROL2");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC] == 1u,
		    "the report did not sample the vendor spec");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC3] == 1u,
		    "the report did not sample the vendor spec 3");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_CONFIG] == 1u,
		    "the report did not sample the DLL config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_STATUS] == 1u,
		    "the report did not sample the DLL status");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DDR_CONFIG] == 1u,
		    "the report did not sample the DDR config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_PWRCTL_STATUS] == 1u,
		    "the report did not sample the power status");
	} else if (strcmp(scenario, "clock-timeout") == 0) {
		check(panicked == 1, "a clock timeout did not panic");
		check(strstr(panic_message, "stage=2") != NULL,
		    "the clock-timeout report does not name the stage");
		check(strstr(panic_message, "rc=-5") != NULL,
		    "the clock-timeout report does not name the clock error");
		check(strstr(panic_message, "clocks=1") != NULL,
		    "the report does not show the verified branch clocks");
		check(strstr(panic_message, "cfg=00002017") != NULL,
		    "the report does not show the 400 kHz RCG config");
		check(strstr(panic_message,
		    "mnd=00000001/000000fc/000000fb") != NULL,
		    "the report does not show the 400 kHz RCG M/N/D values");
		check(strstr(panic_message, "cmd=00000001") != NULL,
		    "the report does not show the stuck RCG update bit");
		check(strstr(panic_message, "bcr=00000000") != NULL,
		    "the report does not show the released block reset");
		check(strstr(panic_message, "apps=00000001") != NULL,
		    "the report does not show the running APPS branch");
		check(strstr(panic_message, "ahb=00000001") != NULL,
		    "the report does not show the running AHB branch");
		check(sdhci_read_count[GTA4L_EMMC_REG_MCI_VERSION] == 3u,
		    "the report did not sample the MCI version");
		check(sdhci_read_count[GTA4L_EMMC_REG_POWER_CONTROL] == 1u,
		    "the report did not sample the power control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL] == 1u,
		    "the report did not sample HOST_CONTROL");
		check(sdhci_read_count[GTA4L_EMMC_REG_CLOCK_CONTROL] == 1u,
		    "the report did not sample the clock control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL2] == 2u,
		    "the report did not sample HOST_CONTROL2");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC] == 3u,
		    "the report did not sample the vendor spec");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC3] == 2u,
		    "the report did not sample the vendor spec 3");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_CONFIG] == 3u,
		    "the report did not sample the DLL config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_STATUS] == 1u,
		    "the report did not sample the DLL status");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DDR_CONFIG] == 1u,
		    "the report did not sample the DDR config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_PWRCTL_STATUS] == 1u,
		    "the report did not sample the power status");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CFG] == 2u,
		    "the report did not sample the RCG config");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_M] == 1u,
		    "the report did not sample the RCG M value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_N] == 1u,
		    "the report did not sample the RCG N value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_D] == 1u,
		    "the report did not sample the RCG D value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_BCR] == 1u,
		    "the report did not sample the block reset");
	} else if (strcmp(scenario, "clock-stable-timeout") == 0) {
		check(panicked == 1, "a clock-stability timeout did not panic");
		check(strstr(panic_message, "stage=2") != NULL,
		    "the clock-stability report does not name the stage");
		check(strstr(panic_message, "rc=-5") != NULL,
		    "the clock-stability report does not name the clock error");
		check(strstr(panic_message, "cfg=00002017") != NULL,
		    "the report does not show the 400 kHz RCG config");
		check(strstr(panic_message,
		    "mnd=00000001/000000fc/000000fb") != NULL,
		    "the report does not show the 400 kHz RCG M/N/D values");
		check(strstr(panic_message, "cmd=00000000") != NULL,
		    "the report does not show the released RCG update bit");
		check(strstr(panic_message, "clk=0001") != NULL,
		    "the report does not show the missing clock stability");
		check(strstr(panic_message, "bcr=00000000") != NULL,
		    "the report does not show the released block reset");
		check(sdhci_read_count[GTA4L_EMMC_REG_MCI_VERSION] == 3u,
		    "the report did not sample the MCI version");
		check(sdhci_read_count[GTA4L_EMMC_REG_POWER_CONTROL] == 1u,
		    "the report did not sample the power control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL] == 1u,
		    "the report did not sample HOST_CONTROL");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL2] == 2u,
		    "the report did not sample HOST_CONTROL2");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC] == 3u,
		    "the report did not sample the vendor spec");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC3] == 2u,
		    "the report did not sample the vendor spec 3");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_CONFIG] == 3u,
		    "the report did not sample the DLL config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_STATUS] == 1u,
		    "the report did not sample the DLL status");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DDR_CONFIG] == 1u,
		    "the report did not sample the DDR config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_PWRCTL_STATUS] == 1u,
		    "the report did not sample the power status");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CFG] == 2u,
		    "the report did not sample the RCG config");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_M] == 1u,
		    "the report did not sample the RCG M value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_N] == 1u,
		    "the report did not sample the RCG N value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_D] == 1u,
		    "the report did not sample the RCG D value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CMD] == 3u,
		    "the report did not sample the RCG command");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_BCR] == 1u,
		    "the report did not sample the block reset");
	} else if (strcmp(scenario, "cmd2-crc") == 0) {
		check(panicked == 1, "a CMD2 CRC error did not panic");
		check(strstr(panic_message, "stage=3") != NULL,
		    "the CMD2 CRC report does not name the stage");
		check(strstr(panic_message, "rc=-8") != NULL,
		    "the CMD2 CRC report does not name the CRC error");
		check(strstr(panic_message,
		    "fail=1/2/0/00000209/0002/8000/00000000/8") != NULL,
		    "the report does not name the failed CMD2");
		check(strstr(panic_message,
		    "rsp=11111111,22222222,33333333,44444444") != NULL,
		    "the report did not capture the raw R2 response");
		check(strstr(panic_message,
		    "cmd1rsp=1/c0ff8080,00000000,00000000,00000000") != NULL,
		    "the report did not capture the final CMD1 window");
		check(strstr(panic_message,
		    "pre2rsp=1/c0ff8080,00000000,00000000,00000000") != NULL,
		    "the report did not capture the pre-CMD2 window");
		check(strstr(panic_message,
		    "c2=0/0000/0000/00000000,00000000,00000000,00000000") != NULL,
		    "the report invented a CMD2 capture");
		check(strstr(panic_message,
		    "c3p=0/0000/0000/00000000/0000/0000") != NULL,
		    "the report invented a pre-CMD3 capture");
		check(strstr(panic_message,
		    "c3t=0/0/0000/00000000/0/"
		    "00000000,00000000,00000000,00000000") != NULL,
		    "the report invented a timeout capture");
		check(strstr(panic_message,
		    "cid=00000000,00000000,00000000,00000000 fmt=4 hv=1234")
		    != NULL,
		    "the report does not carry the capture format and "
		    "HOST_VERSION");
		check(strstr(panic_message, "cmd1=1/0/c0ff8080") != NULL,
		    "the report does not show the accepted CMD1 OCR");
		check(strstr(panic_message, "clocks=1") != NULL,
		    "the report does not show the verified branch clocks");
		check(bad_command_format == 0,
		    "the init sent a command with the wrong flags");
		check(bad_command_order == 0,
		    "the init sent commands in the wrong order or with the "
		    "wrong argument");
		check(cmd_count == 3u,
		    "the init did not send exactly CMD0/CMD1/CMD2");
		check(last_command == (uint16_t)((GTA4L_EMMC_CMD2 << 8) |
		    GTA4L_EMMC_CMD_RESP_LONG | GTA4L_EMMC_CMD_CRC),
		    "the failing command was not CMD2 with CRC checking");
		check(strstr(panic_message, "pwr=0f") != NULL,
		    "the report does not show the powered bus");
		check(strstr(panic_message, "clk=0007") != NULL,
		    "the report does not show the enabled card clock");
		check(strstr(panic_message, "hc2=0008") != NULL,
		    "the report does not show the 1.8 V signalling bit");
		/* MCI_VERSION: two core reads plus the report; POWER_CONTROL:
		 * the completed-init log plus the report; HOST_CONTROL: report
		 * only; CLOCK_CONTROL: two configure-clock reads, the card-clock
		 * enable read and the report; HOST_CONTROL2 and VENDOR_SPEC3:
		 * one core read plus the report; VENDOR_SPEC and DLL_CONFIG:
		 * two core reads plus the report; DLL_STATUS and DDR_CONFIG:
		 * report only; PWRCTL_STATUS: the status read, the clear
		 * verify, the acknowledge verify, the settling read and the
		 * report. */
		check(sdhci_read_count[GTA4L_EMMC_REG_MCI_VERSION] == 3u,
		    "the report did not sample the MCI version");
		check(sdhci_read_count[GTA4L_EMMC_REG_POWER_CONTROL] == 2u,
		    "the report did not sample the power control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL] == 1u,
		    "the report did not sample HOST_CONTROL");
		check(sdhci_read_count[GTA4L_EMMC_REG_CLOCK_CONTROL] == 4u,
		    "the report did not sample the clock control");
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_CONTROL2] == 2u,
		    "the report did not sample HOST_CONTROL2");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC] == 3u,
		    "the report did not sample the vendor spec");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_VENDOR_SPEC3] == 2u,
		    "the report did not sample the vendor spec 3");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_CONFIG] == 3u,
		    "the report did not sample the DLL config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DLL_STATUS] == 1u,
		    "the report did not sample the DLL status");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_DDR_CONFIG] == 1u,
		    "the report did not sample the DDR config");
		check(sdhci_read_count[GTA4L_EMMC_REG_CORE_PWRCTL_STATUS] == 5u,
		    "the report did not sample the power status");
		/* The RCG config is read once by configure_clock(); M/N/D are
		 * report-only; APPS_CMD is read twice by the update handshake
		 * and once by the report; each branch CBCR is read by the
		 * enable handshake and once by the report. */
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CFG] == 2u,
		    "the report did not sample the RCG config");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_M] == 1u,
		    "the report did not sample the RCG M value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_N] == 1u,
		    "the report did not sample the RCG N value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_D] == 1u,
		    "the report did not sample the RCG D value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CMD] == 3u,
		    "the report did not sample the RCG command");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_BCR] == 1u,
		    "the report did not sample the block reset");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CBCR] == 3u,
		    "the report did not sample the APPS branch");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_AHB_CBCR] == 3u,
		    "the report did not sample the AHB branch");
	} else if ((strcmp(scenario, "cmd3-no-cc") == 0) ||
	    (strcmp(scenario, "cmd3-late-cc") == 0)) {
		check(panicked == 1, "a CMD3 timeout did not panic");
		check(strstr(panic_message, "stage=3") != NULL,
		    "the CMD3 timeout report does not name the stage");
		check(strstr(panic_message, "rc=-7") != NULL,
		    "the CMD3 timeout report does not name the timeout");
		check(strstr(panic_message,
		    "fail=1/3/65536/0000031a/0000/") != NULL,
		    "the report does not name the timed-out CMD3");
		check(strstr(panic_message,
		    "c3t=1/3/031a/00010000/1/"
		    "00000900,4d420090,44583638,00150100") != NULL,
		    "the timeout capture does not carry the CMD3 index and "
		    "raw response");
		check(strstr(panic_message,
		    "c3p=1/0000/0000/00000000/0033/07ff") != NULL,
		    "the report did not capture the pre-CMD3 state");
		check(strstr(panic_message,
		    "c2=1/0001/0000/05f46619,4d420090,44583638,00150100")
		    != NULL,
		    "the report did not capture the successful CMD2");
		check(bad_command_format == 0,
		    "the init sent a command with the wrong flags");
		check(bad_command_order == 0,
		    "the init sent commands in the wrong order");
		check(cmd_count == 4u, "the init did not stop after CMD3");
		check(strstr(panic_message,
		    "c3l=1/3/0000/0000/100000/00000000000f4240/1000000")
		    != NULL,
		    "the poll history does not show the completion arrived "
		    "after the loop");
		if (strcmp(scenario, "cmd3-no-cc") == 0) {
			check(strstr(panic_message,
			    "fail=1/3/65536/0000031a/0000/0000/00000000/9")
			    != NULL,
			    "the timed-out CMD3 report has a completion bit");
		} else {
			check(strstr(panic_message,
			    "fail=1/3/65536/0000031a/0000/0001/00000000/9")
			    != NULL,
			    "the fresh post-timeout sample lost the late "
			    "completion");
		}
		check(strstr(panic_message,
		    "cid=15010044,5836384d,42009005,f4661900") != NULL,
		    "the report lost the CID decoded from the valid CMD2");
	} else if (strcmp(scenario, "cmd9-timeout") == 0) {
		check(panicked == 1, "a CMD9 timeout did not panic");
		check(strstr(panic_message, "rc=-7") != NULL,
		    "the CMD9 timeout report does not name the timeout");
		check(strstr(panic_message,
		    "fail=1/9/65536/00000909/0000/0000/00000000/10") != NULL,
		    "the report does not name the timed-out CMD9");
		check(strstr(panic_message,
		    "c3t=1/9/0909/00010000/1/"
		    "ef8e4040,fff6dbff,320f5903,00d02701") != NULL,
		    "the timeout capture does not label CMD9");
		check(strstr(panic_message,
		    "c3l=1/3/0001/0001/1/0000000000000000/1000000") != NULL,
		    "the report lost the successful CMD3 poll history");
		check(strstr(panic_message,
		    "c3p=1/0000/0000/00000000/0033/07ff") != NULL,
		    "the report did not capture the pre-CMD3 state");
		check(strstr(panic_message,
		    "c2=1/0001/0000/05f46619,4d420090,44583638,00150100")
		    != NULL,
		    "the report did not capture the successful CMD2");
		check(strstr(panic_message,
		    "cid=15010044,5836384d,42009005,f4661900") != NULL,
		    "the report lost the CID decoded from the valid CMD2");
		check(bad_command_format == 0,
		    "the init sent a command with the wrong flags");
		check(bad_command_order == 0,
		    "the init sent commands in the wrong order");
		check(cmd_count == 5u, "the init did not stop after CMD9");
	} else if (strcmp(scenario, "probe-read-crc") == 0) {
		check(panicked == 1, "a block 0 data CRC did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE FAILED: block 0 read rc=-12 step=15 "
		    "fail=1/17/0/0000113a/0020/0000/00000000/15") != NULL,
		    "the block 0 data-CRC report lost the failure record");
		check(strstr(panic_message, "GTA4L-EMMC PROBE REPORT") == NULL,
		    "a failed block 0 read still emitted the success report");
		check(bad_command_format == 0,
		    "the probe sent a command with the wrong flags");
		check(bad_command_order == 0,
		    "the probe sent commands in the wrong order");
		check(cmd_count == 9u,
		    "the probe did not stop after the failed block 0 read");
		check(last_index == GTA4L_EMMC_CMD17,
		    "the failed command was not the block read");
	} else if (strcmp(scenario, "probe-read-timeout") == 0) {
		check(panicked == 1, "a block 1 data timeout did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE FAILED: block 1 read rc=-11 step=15 "
		    "fail=1/17/1/0000113a/0010/0000/00000000/15") != NULL,
		    "the block 1 data-timeout report lost the failure record");
		check(strstr(panic_message, "GTA4L-EMMC PROBE REPORT") == NULL,
		    "a failed block 1 read still emitted the success report");
		check(bad_command_format == 0,
		    "the probe sent a command with the wrong flags");
		check(bad_command_order == 0,
		    "the probe sent commands in the wrong order");
		check(cmd_count == 10u,
		    "the probe did not stop after the failed block 1 read");
		check(last_index == GTA4L_EMMC_CMD17,
		    "the failed command was not the block read");
	} else if (strcmp(scenario, "report-max-width-failure") == 0) {
		check(panicked == 1, "the maximum-width failure did not panic");
		check(strstr(panic_message, "stage=3") != NULL,
		    "the maximum-width failure report does not name the stage");
		check(strstr(panic_message, "rc=-7") != NULL,
		    "the maximum-width failure report does not name the "
		    "timeout");
		check(strstr(panic_message,
		    "fail=1/7/65536/0000071b/0000/0020/00000000/11")
		    != NULL,
		    "the maximum-width failure report does not name the "
		    "overflow timeout");
		check(strstr(panic_message,
		    "c3l=1/3/0000/0001/100000/"
		    "00000000000f4236/1000000") != NULL,
		    "the maximum-width failure report lost the full CMD3 poll "
		    "budget");
		check(strstr(panic_message,
		    "c7q=1/0020/0000/0020/0000/100000/"
		    "00000000000f4240") != NULL,
		    "the maximum-width failure report lost the full CMD7 poll "
		    "budget");
		check(strstr(panic_message,
		    "c7c=0/0/0000000000000000/0/0/"
		    "0000000000000000") != NULL,
		    "the maximum-width failure report claims a completion");
		check(strstr(panic_message,
		    "c7t=1/0020/0000/00000000/0000071b/00010000/"
		    "00000900,fff6dbff,320f5903,00d02701") != NULL,
		    "the maximum-width failure report lost the terminal state");
		check(strstr(panic_message,
		    "c7x=4/99996/0:0020:0000,1:0000:0000,"
		    "2:0020:0000,3:0000:0000") != NULL,
		    "the maximum-width failure report lost the four trace "
		    "entries or the overflow count");
		check(strlen(panic_message) < 2048u,
		    "the maximum-width failure report exceeds the 2048-byte "
		    "panic-text budget");
	} else if (strcmp(scenario, "cmd3-identify") == 0) {
		check(panicked == 1, "the identification success did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC IDENT REPORT: rc=0 mci=10000072 rca=1 "
		    "cap=16777216 rev=8 bw=0 ocr=c0ff8080 ext=00000009")
		    != NULL,
		    "the success report does not carry the decoded card data");
		check(strstr(panic_message, "step=15") != NULL,
		    "the success report does not show the ready step");
		check(strstr(panic_message,
		    "GTA4L-EMMC IDENT CAPTURE fmt=4 hv=1234") != NULL,
		    "the success report does not carry the capture format and "
		    "HOST_VERSION");
		check(strstr(panic_message,
		    "c2=1/0001/0000/05f46619,4d420090,44583638,00150100")
		    != NULL,
		    "the success report lost the CMD2 capture");
		check(strstr(panic_message,
		    "c3p=1/0000/0000/00000000/0033/07ff") != NULL,
		    "the success report lost the pre-CMD3 capture");
		check(strstr(panic_message,
		    "c3t=0/0/0000/00000000/0/"
		    "00000000,00000000,00000000,00000000") != NULL,
		    "the success report invented a timeout capture");
		check(strstr(panic_message,
		    "c3l=1/3/0001/0001/1/0000000000000000/1000000") != NULL,
		    "the success report lost the CMD3 poll history");
		check(strstr(panic_message,
		    "cid=15010044,5836384d,42009005,f4661900") != NULL,
		    "the success report does not carry the decoded CID");
		check(bad_command_format == 0,
		    "the init sent a command with the wrong flags");
		check(bad_command_order == 0,
		    "the init sent commands in the wrong order");
		check(cmd_count == 8u,
		    "the init did not complete the identification sequence");
		check(last_index == GTA4L_EMMC_CMD8,
		    "the last command was not the EXT_CSD read");
	} else if (strcmp(scenario, "report-max-width-success") == 0) {
		check(panicked == 1, "the maximum-width success did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC IDENT REPORT: rc=0 mci=10000072 rca=1 "
		    "cap=4294967295 rev=8 bw=0") != NULL,
		    "the maximum-width success report does not carry the "
		    "maximum-width capacity");
		check(strstr(panic_message,
		    "c3l=1/3/0000/0001/100000/"
		    "00000000000f4236/1000000") != NULL,
		    "the maximum-width success report lost the full CMD3 poll "
		    "budget");
		check(strstr(panic_message,
		    "c7q=1/0023/0000/0020/0003/100000/"
		    "00000000000f4236") != NULL,
		    "the maximum-width success report lost the full CMD7 poll "
		    "budget");
		check(strstr(panic_message,
		    "c7c=1/99999/00000000000f4236/1/99999/"
		    "00000000000f4236") != NULL,
		    "the maximum-width success report lost the last-iteration "
		    "completion");
		check(strstr(panic_message,
		    "c7t=1/0023/0000/00000000/0000071b/00010000/"
		    "00000900,fff6dbff,320f5903,00d02701") != NULL,
		    "the maximum-width success report lost the terminal state");
		check(strstr(panic_message,
		    "c7x=4/99996/0:0020:0000,1:0000:0000,"
		    "2:0020:0000,3:0000:0000") != NULL,
		    "the maximum-width success report lost the four trace "
		    "entries or the overflow count");
		check(strlen(panic_message) < 2048u,
		    "the maximum-width success report exceeds the 2048-byte "
		    "panic-text budget");
	} else if (strcmp(scenario, "report-max-width-probe") == 0) {
		char expected[512];

		check(panicked == 1, "the maximum-width probe did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE REPORT: rc=0 mci=10000072 rca=1 "
		    "cap=4294967295 rev=8 bw=0") != NULL,
		    "the maximum-width probe report does not carry the "
		    "maximum-width capacity");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE CID 15010044 5836384d 42009005 "
		    "f4661900") != NULL,
		    "the maximum-width probe report lost the CID");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE CSD d0270132 0f5903ff f6dbffef "
		    "8e404000") != NULL,
		    "the maximum-width probe report lost the CSD");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE BLOCK0 sum=5c510100 words=271a0d00 "
		    "cdc0b3a6 01f4e7da 35281b0e") != NULL,
		    "the maximum-width probe report lost block 0");
		snprintf(expected, sizeof(expected),
		    "GTA4L-EMMC PROBE BLOCK1 sum=%08x words=20494645 "
		    "54524150 00010000 0000005c", sim_block_sum(1));
		check(strstr(panic_message, expected) != NULL,
		    "the maximum-width probe report lost block 1");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE GPT elba=2 n=84 sz=128 first=34 "
		    "last=4294967261 backup=4294967294") != NULL,
		    "the maximum-width probe report lost the GPT gate values");
		sim_expected_blocks(expected, sizeof(expected));
		check(strstr(panic_message, expected) != NULL,
		    "the maximum-width probe report lost the array sums");
		snprintf(expected, sizeof(expected),
		    "GTA4L-EMMC PROBE DATA 34=%08x", sim_block_sum(34));
		check(strstr(panic_message, expected) != NULL,
		    "the maximum-width probe report lost the first-usable "
		    "sector");
		snprintf(expected, sizeof(expected),
		    "GTA4L-EMMC PROBE LSTLBA=4294967294 sum=%08x",
		    sim_block_sum(0xFFFFFFFEu));
		check(strstr(panic_message, expected) != NULL,
		    "the maximum-width probe report lost the last sector");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE CAPTURE fmt=4 hv=1234") != NULL,
		    "the maximum-width probe report does not carry the "
		    "capture format and HOST_VERSION");
		check(strstr(panic_message,
		    "c3l=1/3/0000/0001/100000/"
		    "00000000000f4236/1000000") != NULL,
		    "the maximum-width probe report lost the full CMD3 poll "
		    "budget");
		check(strstr(panic_message,
		    "c7q=1/0023/0000/0020/0003/100000/"
		    "00000000000f4236") != NULL,
		    "the maximum-width probe report lost the full CMD7 poll "
		    "budget");
		check(strstr(panic_message,
		    "c7c=1/99999/00000000000f4236/1/99999/"
		    "00000000000f4236") != NULL,
		    "the maximum-width probe report lost the last-iteration "
		    "completion");
		check(strstr(panic_message,
		    "c7t=1/0023/0000/00000000/0000071b/00010000/"
		    "00000900,fff6dbff,320f5903,00d02701") != NULL,
		    "the maximum-width probe report lost the terminal state");
		check(strstr(panic_message,
		    "c7x=4/99996/0:0020:0000,1:0000:0000,"
		    "2:0020:0000,3:0000:0000") != NULL,
		    "the maximum-width probe report lost the four trace "
		    "entries or the overflow count");
		check(strstr(panic_message, "step=18 log=") != NULL,
		    "the maximum-width probe report does not show the last "
		    "content step");
		check(bad_command_format == 0,
		    "the maximum-width probe sent a command with the wrong "
		    "flags");
		check(bad_command_order == 0,
		    "the maximum-width probe sent commands in the wrong order");
		check(bad_command_argument == 0,
		    "the maximum-width probe read the wrong LBA list");
		check(cmd_count == 33u,
		    "the maximum-width probe did not complete the content "
		    "reads");
		check(strlen(panic_message) < 2048u,
		    "the maximum-width probe report exceeds the 2048-byte "
		    "panic-text budget");
	} else if (strcmp(scenario, "probe-full") == 0) {
		char expected[512];

		check(panicked == 1, "the full probe did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE REPORT: rc=0 mci=10000072 rca=1 "
		    "cap=61071360 rev=8 bw=0") != NULL,
		    "the full probe report does not carry the device capacity");
		check(strstr(panic_message, "step=18 log=") != NULL,
		    "the full probe report does not show the last content "
		    "step");
		check(strstr(panic_message, "mb=1") == NULL,
		    "the default probe report invented the MD mode marker");
		check(strstr(panic_message, "MBLOCKS") == NULL,
		    "the default probe report replaced the CMD17 sums");
		check(strstr(panic_message, "mbsame") == NULL,
		    "the default probe report invented the MD comparison");
		check(strstr(panic_message, "step=18 log=30") != NULL,
		    "the default probe report does not match the stage-A "
		    "step and log counts");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE BLOCK0 sum=5c510100 words=271a0d00 "
		    "cdc0b3a6 01f4e7da 35281b0e") != NULL,
		    "the full probe report lost block 0");
		snprintf(expected, sizeof(expected),
		    "GTA4L-EMMC PROBE BLOCK1 sum=%08x words=20494645 "
		    "54524150 00010000 0000005c", sim_block_sum(1));
		check(strstr(panic_message, expected) != NULL,
		    "the full probe report lost block 1");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE GPT elba=2 n=84 sz=128 first=34 "
		    "last=61071326 backup=61071359") != NULL,
		    "the full probe report lost the GPT gate values");
		sim_expected_blocks(expected, sizeof(expected));
		check(strstr(panic_message, expected) != NULL,
		    "the full probe report lost the array sums");
		snprintf(expected, sizeof(expected),
		    "GTA4L-EMMC PROBE DATA 34=%08x", sim_block_sum(34));
		check(strstr(panic_message, expected) != NULL,
		    "the full probe report lost the first-usable sector");
		snprintf(expected, sizeof(expected),
		    "GTA4L-EMMC PROBE LSTLBA=61071359 sum=%08x",
		    sim_block_sum(61071359u));
		check(strstr(panic_message, expected) != NULL,
		    "the full probe report lost the last sector");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE CAPTURE fmt=4 hv=1234") != NULL,
		    "the full probe report does not carry the capture format");
		check(bad_command_format == 0,
		    "the full probe sent a command with the wrong flags");
		check(bad_command_order == 0,
		    "the full probe sent commands in the wrong order");
		check(bad_command_argument == 0,
		    "the full probe read the wrong LBA list");
		check(cmd_count == 33u,
		    "the full probe did not complete the content reads");
		check(strlen(panic_message) < 2048u,
		    "the full probe report exceeds the 2048-byte panic "
		    "budget");
	} else if (strcmp(scenario, "probe-gpt-mismatch") == 0) {
		check(panicked == 1, "a GPT gate mismatch did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE FAILED: gpt gate bad=00000020 "
		    "cap=61071360 sig=20494645 elba=2 n=83 sz=128 first=34 "
		    "last=61071326 backup=61071359") != NULL,
		    "the gate refusal does not name the mismatched field");
		check(strstr(panic_message, "GTA4L-EMMC PROBE REPORT") == NULL,
		    "the gate refusal still emitted the success report");
		check(bad_command_argument == 0,
		    "the gate probe read the wrong LBA list");
		check(cmd_count == 10u,
		    "the gate let the probe read past the header");
	} else if (strcmp(scenario, "probe-array-crc") == 0) {
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE FAILED: array read lba=2 rc=-12 "
		    "step=16 fail=1/17/2/0000113a/0020/0000/00000000/16")
		    != NULL,
		    "the array data-CRC report lost the failure record");
		check(strstr(panic_message, "GTA4L-EMMC PROBE REPORT") == NULL,
		    "a failed array read still emitted the success report");
		check(cmd_count == 11u,
		    "the probe did not stop at the first array failure");
	} else if (strcmp(scenario, "probe-mid-loop-error") == 0) {
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE FAILED: array read lba=7 rc=-11 "
		    "step=16 fail=1/17/7/0000113a/0010/0000/00000000/16")
		    != NULL,
		    "the mid-loop timeout report lost the failure record");
		check(cmd_count == 16u,
		    "the probe did not stop at the mid-loop failure");
	} else if (strcmp(scenario, "probe-last-error") == 0) {
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE FAILED: last read lba=61071359 rc=-12 "
		    "step=18 fail=1/17/61071359/0000113a/0020/0000/00000000/18")
		    != NULL,
		    "the last-sector report lost the failure record");
		check(cmd_count == 33u,
		    "the probe did not issue the full content sequence");
	} else if (strcmp(scenario, "probe-md-success") == 0) {
		char expected[512];

		check(panicked == 1, "the MD probe did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE REPORT: rc=0 mci=10000072 rca=1 "
		    "cap=61071360 rev=8 bw=0") != NULL,
		    "the MD probe report does not carry the device capacity");
		check(strstr(panic_message, "step=19 log=51 mb=1") != NULL,
		    "the MD probe report does not name the MD step and log "
		    "counts");
		sim_expected_blocks_label(expected, sizeof(expected),
		    "MBLOCKS", 1);
		check(strstr(panic_message, expected) != NULL,
		    "the MD probe report lost the CMD18 sums");
		check(strstr(panic_message, "mbsame=1") != NULL,
		    "the MD probe report lost the matching comparison");
		check(strstr(panic_message, "mbsame=0") == NULL,
		    "the MD probe report reported a mismatch for equal sums");
		check(strstr(panic_message, "GTA4L-EMMC PROBE BLOCKS") == NULL,
		    "the MD probe report kept the CMD17 sum line");
		check(bad_command_format == 0,
		    "the MD probe sent a command with the wrong flags");
		check(bad_command_order == 0,
		    "the MD probe sent commands in the wrong order");
		check(bad_command_argument == 0,
		    "the MD probe read the wrong CMD17 LBA list");
		check(bad_mb_command == 0,
		    "the MD probe used the wrong CMD18/CMD12 form");
		check(auto_cmd12_seen == 0,
		    "the MD probe left AUTO_CMD12 enabled");
		check(mb_stop_count == 1u,
		    "the MD probe did not issue exactly one explicit stop");
		check(cmd_count == 35u,
		    "the MD probe did not issue CMD18 and one CMD12");
		check(strlen(panic_message) < 2048u,
		    "the MD probe report exceeds the 2048-byte panic budget");
	} else if (strcmp(scenario, "probe-md-mismatch") == 0) {
		char expected[512];

		check(panicked == 1, "the MD mismatch probe did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE REPORT: rc=0") != NULL,
		    "a content mismatch was reported as a hardware failure");
		check(strstr(panic_message, "mbsame=0 mbdiv=7") != NULL,
		    "the MD mismatch report does not name the first differing "
		    "LBA");
		sim_expected_blocks_label(expected, sizeof(expected),
		    "MBLOCKS", 1);
		check(strstr(panic_message, expected) != NULL,
		    "the MD mismatch report lost the CMD18 sums");
		check(strstr(panic_message, "step=19 log=51 mb=1") != NULL,
		    "the MD mismatch report does not name the MD step and log "
		    "counts");
		check(mb_stop_count == 1u,
		    "the MD mismatch probe did not issue exactly one stop");
		check(cmd_count == 35u,
		    "the MD mismatch probe did not issue CMD18 and one CMD12");
	} else if (strcmp(scenario, "probe-md-mid-fail") == 0) {
		check(panicked == 1, "a mid-CMD18 data error did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE FAILED: mb read lba=2 n=21 rc=-11 "
		    "step=19 mb=3 drain=0/0/0/0 "
		    "stop=0/1/1/0003/0000/00000000/00000c1b/"
		    "00000000 fail=1/18/2/0000123a/0010/0001/00000000/19")
		    != NULL,
		    "the mid-CMD18 failure report lost the block ordinal, the "
		    "stop result or the terminal record");
		check(strstr(panic_message, "GTA4L-EMMC PROBE REPORT") == NULL,
		    "a failed CMD18 still emitted the success report");
		check(mb_stop_count == 1u,
		    "the failed CMD18 transfer was not stopped explicitly");
		check(cmd_count == 35u,
		    "the mid-CMD18 failure did not reach the explicit stop");
	} else if (strcmp(scenario, "probe-md-stop-no-tc") == 0) {
		check(panicked == 1, "a CMD12 timeout did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE FAILED: mb read lba=2 n=21 rc=-7 "
		    "step=19 mb=20 drain=0/0/0/0 "
		    "stop=-7/1/1/0001/0000/00000000/00000c1b/"
		    "00000000 fail=1/12/0/00000c1b/0000/0001/00000000/19")
		    != NULL,
		    "the CMD12 timeout report lost the stop capture or the "
		    "terminal record");
		check(strstr(panic_message, "GTA4L-EMMC PROBE REPORT") == NULL,
		    "a failed stop still emitted the success report");
		check(mb_stop_count == 1u,
		    "the probe did not issue exactly one explicit stop");
		check(cmd_count == 35u,
		    "the stop-timeout sequence did not finish");
	} else if (strcmp(scenario, "probe-md-drain-tc") == 0) {
		char expected[512];

		check(panicked == 1, "the drained MD probe did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE REPORT: rc=0") != NULL,
		    "a drain-resolved transfer was reported as a failure");
		check(strstr(panic_message, "step=19 log=51 mb=1") != NULL,
		    "the drained MD report does not name the MD step and log "
		    "counts");
		sim_expected_blocks_label(expected, sizeof(expected),
		    "MBLOCKS", 1);
		check(strstr(panic_message, expected) != NULL,
		    "the drained MD report lost the CMD18 sums");
		check(strstr(panic_message, "mbsame=1 drain=1/1/0/121") != NULL,
		    "the drained MD report lost the completion-only drain "
		    "resolution");
		check(mb_stop_count == 1u,
		    "the drained MD probe did not issue exactly one stop");
		check(cmd_count == 35u,
		    "the drained MD probe did not issue CMD18 and one CMD12");
	} else if (strcmp(scenario, "probe-md-drain-brr") == 0) {
		char expected[512];

		check(panicked == 1, "the late-BRR MD probe did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE REPORT: rc=0") != NULL,
		    "a pending-BRR drain was reported as a failure");
		check(strstr(panic_message, "step=19 log=51 mb=1") != NULL,
		    "the late-BRR MD report does not name the MD step and log "
		    "counts");
		sim_expected_blocks_label(expected, sizeof(expected),
		    "MBLOCKS", 1);
		check(strstr(panic_message, expected) != NULL,
		    "the late-BRR MD report lost the CMD18 sums");
		check(strstr(panic_message, "mbsame=1 drain=1/1/1/121") != NULL,
		    "the late-BRR MD report lost the block-drain resolution");
		check(mb_stop_count == 1u,
		    "the late-BRR MD probe did not issue exactly one stop");
		check(cmd_count == 35u,
		    "the late-BRR MD probe did not issue CMD18 and one CMD12");
	} else if (strcmp(scenario, "probe-md-drain-error") == 0) {
		check(panicked == 1, "a drain-window data error did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE FAILED: mb read lba=2 n=21 rc=-12 "
		    "step=19 mb=20 drain=1/0/0/101 "
		    "stop=0/1/1/0003/0000/00000000/00000c1b/"
		    "00000000 fail=1/18/2/0000123a/0020/0001/00000000/19")
		    != NULL,
		    "the drain-window error report lost the drain outcome, the "
		    "error result or the terminal record");
		check(strstr(panic_message, "GTA4L-EMMC PROBE REPORT") == NULL,
		    "a drain-window error still emitted the success report");
		check(mb_stop_count == 1u,
		    "the drain-window error transfer was not stopped");
		check(cmd_count == 35u,
		    "the drain-window error did not reach the explicit stop");
	} else if (strcmp(scenario, "probe-md-stall-tc") == 0) {
		check(panicked == 1, "an exhausted drain window did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE FAILED: mb read lba=2 n=21 rc=-11 "
		    "step=19 mb=20 drain=1/0/0/50000 "
		    "stop=0/1/1/0003/0000/00000000/00000c1b/"
		    "00000000 fail=1/18/2/0000123a/0000/0001/00000000/19")
		    != NULL,
		    "the exhausted drain report lost the drain outcome or the "
		    "terminal record");
		check(strstr(panic_message, "GTA4L-EMMC PROBE REPORT") == NULL,
		    "an exhausted drain still emitted the success report");
		check(mb_stop_count == 1u,
		    "the exhausted drain transfer was not stopped");
		check(cmd_count == 35u,
		    "the exhausted drain did not reach the explicit stop");
	} else if (strcmp(scenario, "probe-mb-bare-refuse") == 0) {
		check(panicked == 1, "a bare MD argument was not refused");
		check(strstr(panic_message, "REFUSED") != NULL,
		    "the bare MD refusal does not name the conflict");
		check(strstr(panic_message, "gta4l-emmc-mb") != NULL,
		    "the bare MD refusal does not name the modifier");
		check(sdhci_accesses == 0ul,
		    "a bare MD argument touched SDHCI");
		check(gcc_accesses == 0ul,
		    "a bare MD argument touched GCC");
	} else if (strcmp(scenario, "probe-b1-success") == 0) {
		char expected[512];

		check(panicked == 1, "the B1 success run did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=0 stop=0") != NULL,
		    "the B1 success report does not carry the result");
		check(strstr(panic_message,
		    "BW=1 att=1 req=1 sw=0 swrc=0 tt=0") != NULL,
		    "the B1 report lost the 1-bit baseline width");
		snprintf(expected, sizeof(expected),
		    "BW=1 att=1 req=1 sw=0 swrc=0 tt=0 tw=00000000 "
		    "te=00000000 trc=0 hc=00 es=%08x",
		    sim_ext_csd_sum() - 2u);
		check(strstr(panic_message, expected) != NULL,
		    "the B1 report lost the 1-bit EXT_CSD sum");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=1 swrc=0 tt=1 "
		    "tw=000000a5 te=000000a5 trc=0 hc=02") != NULL,
		    "the B1 report lost the 4-bit switch or bus-test words");
		snprintf(expected, sizeof(expected),
		    "BW=4 att=1 req=4 sw=1 swrc=0 tt=1 "
		    "tw=000000a5 te=000000a5 trc=0 hc=02 hb=00 ha=02 "
		    "c6=00000900 es=%08x", sim_ext_csd_sum() - 1u);
		check(strstr(panic_message, expected) != NULL,
		    "the B1 report lost the 4-bit CMD6 status or host-width "
		    "transition");
		check(strstr(panic_message,
		    "BW=8 att=1 req=8 sw=2 swrc=0 tt=1 "
		    "tw=000055aa te=000055aa trc=0 hc=20") != NULL,
		    "the B1 report lost the 8-bit switch or bus-test words");
		snprintf(expected, sizeof(expected),
		    "BW=8 att=1 req=8 sw=2 swrc=0 tt=1 "
		    "tw=000055aa te=000055aa trc=0 hc=20 hb=02 ha=20 "
		    "c6=00000900 es=%08x", sim_ext_csd_sum());
		check(strstr(panic_message, expected) != NULL,
		    "the B1 report lost the 8-bit CMD6 status or host-width "
		    "transition");
		check(strstr(panic_message, "same=1 div=0") != NULL,
		    "the B1 report does not show the per-width content pass");
		sim_expected_b1_blocks(expected, sizeof(expected), 4u);
		check(strstr(panic_message, expected) != NULL,
		    "the 4-bit content pass lost the stage-A sums");
		sim_expected_b1_blocks(expected, sizeof(expected), 8u);
		check(strstr(panic_message, expected) != NULL,
		    "the 8-bit content pass lost the stage-A sums");
		check(strstr(panic_message,
		    "PAD pqdsd=") != NULL,
		    "the B1 report lost the before/after pad/DLL state");
		check(cmd6_count == 2u,
		    "the B1 run did not issue one CMD6 per width");
		check(cmd6_argument == 0x03B70201u,
		    "the B1 run did not write the 8-bit EXT_CSD value last");
		check(bad_command_format == 0,
		    "a B1 command used the wrong flags");
		check(bad_command_order == 0,
		    "the B1 run sent commands in the wrong order");
		check(bad_command_argument == 0,
		    "the B1 content pass read the wrong LBA list");
		check(cmd_count == 93u,
		    "the B1 run did not complete both content passes");
		check(strlen(panic_message) < 2048u,
		    "the B1 report exceeds the 2048-byte panic-text budget");
	} else if (strcmp(scenario, "probe-b1-stuck-width") == 0) {
		char expected[512];

		check(panicked == 1, "a stuck 4-bit switch did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-19 stop=1") != NULL,
		    "the stuck-width report does not name the readback error");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=0") != NULL,
		    "the stuck-width report does not show the unchanged "
		    "EXT_CSD value");
		snprintf(expected, sizeof(expected),
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=0 tw=00000000 "
		    "te=00000000 trc=0 hc=00 hb=00 ha=02 c6=00000900 "
		    "es=%08x", sim_ext_csd_sum());
		check(strstr(panic_message, expected) != NULL,
		    "the stuck-width report does not separate a card-ignored "
		    "switch from a failed or empty readback");
		check(strstr(panic_message,
		    "BW=8 att=0 req=0 sw=0 swrc=0 tt=0") != NULL,
		    "the stuck-width report kept asserting on 8-bit");
		check(cmd_count == 36u,
		    "the stuck-width run did not stop at the readback");
	} else if (strcmp(scenario, "probe-b1-zero-readback") == 0) {
		check(panicked == 1, "a zero-filled readback did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-19 stop=1") != NULL,
		    "the zero-readback report does not name the readback "
		    "error");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=0 tw=00000000 "
		    "te=00000000 trc=0 hc=00 hb=00 ha=02 c6=00000900 "
		    "es=00000000") != NULL,
		    "the zero-readback report does not show the empty EXT_CSD "
		    "read after a taken host-width write");
		check(cmd_count == 36u,
		    "the zero-readback run did not stop at the readback");
	} else if (strcmp(scenario,
	    "probe-b1-host-width-stuck") == 0) {
		check(panicked == 1, "a lost host width write did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-12 stop=1") != NULL,
		    "the host-width-stuck report does not name the failed "
		    "read");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=0 swrc=-12 tt=0 tw=00000000 "
		    "te=00000000 trc=0 hc=00 hb=00 ha=00 c6=00000900 "
		    "es=00000000") != NULL,
		    "the host-width-stuck report does not show the lost "
		    "HOST_CONTROL width and the failed read");
		check(cmd_count == 36u,
		    "the host-width-stuck run did not stop at the read");
	} else if (strcmp(scenario, "probe-b1-bustest-fail") == 0) {
		check(panicked == 1, "a 4-bit bus-test mismatch did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-20 stop=2") != NULL,
		    "the 4-bit bus-test report does not name the mismatch");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=1 swrc=0 tt=1 "
		    "tw=0000005a te=000000a5 trc=-20") != NULL,
		    "the 4-bit bus-test report lost the failing pattern word");
		check(strstr(panic_message,
		    "BW=8 att=0 req=0 sw=0 swrc=0 tt=0") != NULL,
		    "the 4-bit bus-test failure continued to 8-bit");
		check(cmd_count == 38u,
		    "the 4-bit bus-test failure did not stop at CMD14");
	} else if (strcmp(scenario, "probe-b1-switch8-stuck") == 0) {
		check(panicked == 1, "a stuck 8-bit switch did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-19 stop=5") != NULL,
		    "the stuck 8-bit switch report does not name the readback "
		    "error");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=1 swrc=0 tt=1") != NULL,
		    "the stuck 8-bit switch report lost the 4-bit pass");
		check(strstr(panic_message,
		    "BW=8 att=1 req=8 sw=1 swrc=-19 tt=0") != NULL,
		    "the stuck 8-bit switch report does not show the unchanged "
		    "EXT_CSD value");
		check(cmd_count == 66u,
		    "the stuck 8-bit switch run did not stop at the readback");
	} else if (strcmp(scenario, "probe-b1-bustest8-fail") == 0) {
		check(panicked == 1, "an 8-bit bus-test mismatch did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-20 stop=6") != NULL,
		    "the 8-bit bus-test report does not name the mismatch");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=1 swrc=0 tt=1") != NULL,
		    "the 8-bit bus-test report lost the 4-bit pass");
		check(strstr(panic_message,
		    "BW=8 att=1 req=8 sw=2 swrc=0 tt=1 "
		    "tw=0000aa55 te=000055aa trc=-20") != NULL,
		    "the 8-bit bus-test report lost the failing pattern word");
		check(cmd_count == 68u,
		    "the 8-bit bus-test failure did not stop at CMD14");
	} else if (strcmp(scenario, "probe-b1-content8-div") == 0) {
		check(panicked == 1, "an 8-bit content mismatch did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-21 stop=8") != NULL,
		    "the content-mismatch report does not name the probe error");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=1 swrc=0 tt=1") != NULL,
		    "the content-mismatch report lost the 4-bit pass");
		check(strstr(panic_message,
		    "same=0 div=34") != NULL,
		    "the content-mismatch report does not name the first "
		    "differing LBA");
		check(cmd_count == 93u,
		    "the content-mismatch run did not finish the 8-bit read "
		    "pass");
	} else if (strcmp(scenario, "probe-b1c-success") == 0) {
		char expected[512];

		check(panicked == 1, "the B1C success run did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=0 stop=0") != NULL,
		    "the B1C success report does not carry the result");
		snprintf(expected, sizeof(expected),
		    "BW=4 att=1 req=4 sw=1 swrc=0 tt=1 "
		    "tw=000000a5 te=000000a5 trc=0 hc=02 hb=00 ha=02 "
		    "c6=00000900 es=%08x", sim_ext_csd_sum() - 1u);
		check(strstr(panic_message, expected) != NULL,
		    "the B1C success report lost the 4-bit verification "
		    "fields");
		check(strstr(panic_message,
		    "same=1 div=0 bc=0 e183a=00000000 esa=00000000") != NULL,
		    "the B1C success report did not disable the continuation "
		    "fields");
		check(strstr(panic_message,
		    "BW=8 att=1 req=8 sw=2 swrc=0 tt=1") != NULL,
		    "the B1C success report lost the 8-bit escalation");
		check(cmd6_count == 2u,
		    "the B1C success run did not issue one CMD6 per width");
		check(cmd_count == 93u,
		    "the B1C success run did not complete both content "
		    "passes");
	} else if (strcmp(scenario, "probe-b1c-pass") == 0) {
		char expected[512];

		check(panicked == 1, "the B1C continuation did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-19 stop=1") != NULL,
		    "the B1C pass report does not keep the verification stop");
		snprintf(expected, sizeof(expected),
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=1 "
		    "tw=000000a5 te=000000a5 trc=0 hc=00 hb=00 ha=02 "
		    "c6=00000900 es=%08x", sim_ext_csd_sum());
		check(strstr(panic_message, expected) != NULL,
		    "the B1C pass report lost the failed-verification tuple or "
		    "the bus test");
		snprintf(expected, sizeof(expected),
		    "same=1 div=0 bc=1 e183a=00000000 esa=%08x",
		    sim_ext_csd_sum());
		check(strstr(panic_message, expected) != NULL,
		    "the B1C pass report lost the continuation marker or the "
		    "re-read fields");
		sim_expected_b1_blocks(expected, sizeof(expected), 4u);
		check(strstr(panic_message, expected) != NULL,
		    "the B1C pass content pass lost the stage-A sums");
		check(strstr(panic_message,
		    "BW=8 att=0 req=0 sw=0 swrc=0 tt=0") != NULL,
		    "the B1C continuation attempted 8-bit");
		check(cmd_count == 64u,
		    "the B1C pass run did not run exactly one continuation");
	} else if (strcmp(scenario, "probe-b1c-late-commit") == 0) {
		char expected[512];

		check(panicked == 1, "the late-commit continuation did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-19 stop=1") != NULL,
		    "the late-commit report does not keep the verification "
		    "stop");
		snprintf(expected, sizeof(expected),
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=1 "
		    "tw=000000a5 te=000000a5 trc=0 hc=00 hb=00 ha=02 "
		    "c6=00000900 es=%08x", sim_ext_csd_sum() - 1u);
		check(strstr(panic_message, expected) != NULL,
		    "the late-commit report lost the original verification "
		    "tuple");
		snprintf(expected, sizeof(expected),
		    "same=1 div=0 bc=1 e183a=00000001 esa=%08x",
		    sim_ext_csd_sum());
		check(strstr(panic_message, expected) != NULL,
		    "the late-commit report did not record the later byte-183 "
		    "commit and its sum");
		check(cmd_count == 64u,
		    "the late-commit run did not run exactly one continuation");
	} else if (strcmp(scenario, "probe-b1c-bustest-fail") == 0) {
		check(panicked == 1, "the continuation bus-test mismatch did "
		    "not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-20 stop=1") != NULL,
		    "the continuation bus-test report does not name the "
		    "mismatch");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=1 "
		    "tw=0000005a te=000000a5 trc=-20 hc=00 hb=00 ha=02 "
		    "c6=00000900") != NULL,
		    "the continuation bus-test report lost the failing "
		    "pattern word");
		check(strstr(panic_message,
		    "same=0 div=0 bc=1 e183a=00000000 esa=00000000") != NULL,
		    "the continuation bus-test report ran the content pass or "
		    "the re-read");
		check(cmd_count == 38u,
		    "the continuation bus-test failure did not stop at CMD14");
	} else if (strcmp(scenario, "probe-b1c-content-div") == 0) {
		check(panicked == 1, "the continuation content mismatch did "
		    "not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-21 stop=1") != NULL,
		    "the continuation content-mismatch report does not name "
		    "the probe error");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=1 "
		    "tw=000000a5 te=000000a5 trc=0 hc=00 hb=00 ha=02 "
		    "c6=00000900") != NULL,
		    "the continuation content-mismatch report lost the bus "
		    "test pass");
		check(strstr(panic_message,
		    "same=0 div=34 bc=1 e183a=00000000 esa=0000018e")
		    != NULL,
		    "the continuation content-mismatch report does not name "
		    "the first differing LBA or the re-read result");
		check(cmd_count == 64u,
		    "the continuation content mismatch did not run the "
		    "post-pass re-read");
	} else if (strcmp(scenario, "probe-b1c-content-error") == 0) {
		check(panicked == 1, "the continuation content read error did "
		    "not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-12 stop=1") != NULL,
		    "the continuation content-read error does not name the "
		    "read result");
		check(strstr(panic_message,
		    "step=21 log=") != NULL,
		    "the continuation content-read error falsified the last "
		    "completed step");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=1 "
		    "tw=000000a5 te=000000a5 trc=0 hc=00 hb=00 ha=02 "
		    "c6=00000900") != NULL,
		    "the continuation content-read error lost the bus-test "
		    "pass");
		check(strstr(panic_message,
		    "b0=00000000 b1=00000000 same=0 div=0 bc=1 "
		    "e183a=00000000 esa=00000000") != NULL,
		    "the continuation content-read error ran the re-read or "
		    "invented content");
		check(cmd_count == 39u,
		    "the continuation content-read error did not stop at the "
		    "first continuation read");
	} else if (strcmp(scenario, "probe-b1c-reread-error") == 0) {
		char expected[512];

		check(panicked == 1, "the continuation re-read error did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-12 stop=1") != NULL,
		    "the continuation re-read error does not name the read "
		    "result");
		check(strstr(panic_message,
		    "step=18 log=") != NULL,
		    "the continuation re-read error falsified the last "
		    "completed step");
		check(strstr(panic_message,
		    "same=1 div=0 bc=1 e183a=00000000 esa=00000000") != NULL,
		    "the continuation re-read error lost the content pass or "
		    "invented a byte-183 value");
		sim_expected_b1_blocks(expected, sizeof(expected), 4u);
		check(strstr(panic_message, expected) != NULL,
		    "the continuation re-read error lost the content sums");
		check(cmd_count == 64u,
		    "the continuation re-read error did not issue the "
		    "post-pass read");
	} else if (strcmp(scenario, "probe-b1c-c6-error") == 0) {
		check(panicked == 1, "the CMD6-error row did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-15 stop=1") != NULL,
		    "the CMD6-error row does not name the card status");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=0 swrc=-15 tt=0 tw=00000000 "
		    "te=00000000 trc=0 hc=00 hb=00 ha=00 c6=0000a000 "
		    "es=00000000 b0=00000000 b1=00000000 same=0 div=0 "
		    "bc=0 e183a=00000000 esa=00000000") != NULL,
		    "the CMD6-error row is not the non-entering stage-B1 "
		    "report plus zero B1C fields");
		check(cmd_count == 34u,
		    "the CMD6-error row did not stop at the failed switch");
	} else if (strcmp(scenario, "probe-b1c-zero-readback") == 0) {
		check(panicked == 1, "the B1C empty readback did not report");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=0 tw=00000000 "
		    "te=00000000 trc=0 hc=00 hb=00 ha=02 c6=00000900 "
		    "es=00000000 b0=00000000 b1=00000000 same=0 div=0 "
		    "bc=0 e183a=00000000 esa=00000000") != NULL,
		    "the empty-readback row is not the non-entering stage-B1 "
		    "report plus zero B1C fields");
		check(cmd_count == 36u,
		    "the empty-readback row did not stop at the readback");
	} else if (strcmp(scenario, "probe-b1c-host-width-stuck") == 0) {
		check(panicked == 1, "the B1C lost host width did not report");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=0 swrc=-12 tt=0 tw=00000000 "
		    "te=00000000 trc=0 hc=00 hb=00 ha=00 c6=00000900 "
		    "es=00000000 b0=00000000 b1=00000000 same=0 div=0 "
		    "bc=0 e183a=00000000 esa=00000000") != NULL,
		    "the lost-host-width row is not the non-entering stage-B1 "
		    "report plus zero B1C fields");
		check(cmd_count == 36u,
		    "the lost-host-width row did not stop at the read");
	} else if (strcmp(scenario, "report-width-failure") == 0) {
		check(panicked == 1, "the synthetic width failure did not panic");
		check(strstr(panic_message, "stage=3") != NULL,
		    "the synthetic width failure report does not name the "
		    "stage");
		check(strstr(panic_message, "rc=-18") != NULL,
		    "the synthetic width failure report does not carry the "
		    "widest host-side negative result code");
		check(strstr(panic_message,
		    "fail=1/63/4294967295/0000ffff/ffff/ffff/ffffffff/15")
		    != NULL,
		    "the synthetic width failure report does not carry the "
		    "field-wise capture bounds");
		check(strstr(panic_message,
		    "rsp=ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width failure report lost the raw window");
		check(strstr(panic_message,
		    "cmd1rsp=1/ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width failure report lost the CMD1 window");
		check(strstr(panic_message,
		    "pre2rsp=1/ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width failure report lost the pre-CMD2 "
		    "window");
		check(strstr(panic_message,
		    "c2=1/ffff/ffff/ffffffff,ffffffff,ffffffff,ffffffff")
		    != NULL,
		    "the synthetic width failure report lost the CMD2 "
		    "capture");
		check(strstr(panic_message,
		    "c3p=1/ffff/ffff/ffffffff/ffff/ffff") != NULL,
		    "the synthetic width failure report lost the pre-CMD3 "
		    "state");
		check(strstr(panic_message,
		    "c3t=1/63/ffff/ffffffff/1/"
		    "ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width failure report lost the timeout "
		    "capture");
		check(strstr(panic_message,
		    "c3l=1/63/ffff/ffff/100000/"
		    "ffffffffffffffff/1000000000 cid=") != NULL,
		    "the synthetic width failure report lost the six-digit "
		    "poll-iteration and ten-digit counter fields");
		check(strstr(panic_message,
		    "cid=ffffffff,ffffffff,ffffffff,ffffffff fmt=4 hv=ffff "
		    "cmd1=20/20/ffffffff") != NULL,
		    "the synthetic width failure report lost the CID, capture "
		    "format, CMD1 count or host version");
		check(strstr(panic_message,
		    "clocks=1 mci=ffffffff pwr=ff clk=ffff hc=ff "
		    "hc2=ffff ven=ffffffff v3=ffffffff dll=ffffffff "
		    "dst=ffffffff ddr=ffffffff pst=ffffffff cfg=ffffffff "
		    "mnd=ffffffff/ffffffff/ffffffff cmd=ffffffff "
		    "bcr=ffffffff apps=ffffffff ahb=ffffffff step=15") != NULL,
		    "the synthetic width failure report lost the sampler "
		    "state");
		check(strstr(panic_message,
		    "log=99999/8/4294967295/4294967295") != NULL,
		    "the synthetic width failure report lost the log fields");
		check(strstr(panic_message,
		    "c7p=1/ffff/ffff/ffffffff/ffff/ffff/"
		    "ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width failure report lost the pre-CMD7 "
		    "state");
		check(strstr(panic_message,
		    "c7q=1/ffff/ffff/ffff/ffff/100000/"
		    "ffffffffffffffff") != NULL,
		    "the synthetic width failure report lost the CMD7 poll "
		    "summary");
		check(strstr(panic_message,
		    "c7c=1/99999/ffffffffffffffff/1/99999/"
		    "ffffffffffffffff") != NULL,
		    "the synthetic width failure report lost the valid late "
		    "CC and TC");
		check(strstr(panic_message,
		    "c7t=1/ffff/ffff/ffffffff/0000ffff/ffffffff/"
		    "ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width failure report lost the terminal "
		    "CMD7 state");
		check(strstr(panic_message,
		    "c7x=4/99996/0:ffff:ffff,99997:0000:0000,"
		    "99998:ffff:0000,99999:0000:ffff") != NULL,
		    "the synthetic width failure report lost the late stored "
		    "trace index or the overflow bound");
		check(report_budget_accepts(strlen(panic_message)),
		    "the synthetic width failure report does not fit the "
		    "2048-byte panic buffer including the NUL");
	} else if (strcmp(scenario, "report-width-ident") == 0) {
		check(panicked == 1, "the synthetic width ident did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC IDENT REPORT: rc=0 mci=ffffffff rca=1 "
		    "cap=4294967295 rev=255 bw=15 ocr=ffffffff ext=ffffffff "
		    "hc2=ffff ven=ffffffff dll=ffffffff dst=ffffffff "
		    "pst=ffffffff step=15 log=99999") != NULL,
		    "the synthetic width ident report lost the field-wise "
		    "header bounds");
		check(strstr(panic_message,
		    "GTA4L-EMMC IDENT CAPTURE fmt=4 hv=ffff "
		    "c2=1/ffff/ffff/ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width ident report lost the capture header "
		    "or CMD2 capture");
		check(strstr(panic_message,
		    "c3p=1/ffff/ffff/ffffffff/ffff/ffff "
		    "c3t=1/63/ffff/ffffffff/1/"
		    "ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width ident report lost the pre-CMD3 or "
		    "timeout capture");
		check(strstr(panic_message,
		    "c7p=1/ffff/ffff/ffffffff/ffff/ffff/"
		    "ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width ident report lost the pre-CMD7 "
		    "state");
		check(strstr(panic_message,
		    "c7q=1/ffff/ffff/ffff/ffff/100000/"
		    "ffffffffffffffff") != NULL,
		    "the synthetic width ident report lost the CMD7 poll "
		    "summary");
		check(strstr(panic_message,
		    "c3l=1/63/ffff/ffff/100000/"
		    "ffffffffffffffff/1000000000 cid=") != NULL,
		    "the synthetic width ident report lost the six-digit "
		    "poll-iteration and ten-digit counter fields");
		check(strstr(panic_message,
		    "c7c=1/99999/ffffffffffffffff/1/99999/"
		    "ffffffffffffffff") != NULL,
		    "the synthetic width ident report lost the valid late CC "
		    "and TC");
		check(strstr(panic_message,
		    "c7t=1/ffff/ffff/ffffffff/0000ffff/ffffffff/"
		    "ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width ident report lost the terminal "
		    "CMD7 state");
		check(strstr(panic_message,
		    "c7x=4/99996/0:ffff:ffff,99997:0000:0000,"
		    "99998:ffff:0000,99999:0000:ffff") != NULL,
		    "the synthetic width ident report lost the late stored "
		    "trace index or the overflow bound");
		check(strstr(panic_message,
		    "cid=ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width ident report lost the CID");
		check(report_budget_accepts(strlen(panic_message)),
		    "the synthetic width ident report does not fit the "
		    "2048-byte panic buffer including the NUL");
	} else if (strcmp(scenario, "report-width-probe") == 0) {
		check(panicked == 1, "the synthetic width probe did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE REPORT: rc=0 mci=ffffffff rca=1 "
		    "cap=4294967295 rev=255 bw=15 ocr=ffffffff ext=ffffffff "
		    "hc2=ffff ven=ffffffff dll=ffffffff dst=ffffffff "
		    "pst=ffffffff step=18 log=99999") != NULL,
		    "the synthetic width probe report lost the field-wise "
		    "header bounds");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE CID ffffffff ffffffff ffffffff "
		    "ffffffff") != NULL,
		    "the synthetic width probe report lost the CID");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE CSD ffffffff ffffffff ffffffff "
		    "ffffffff") != NULL,
		    "the synthetic width probe report lost the CSD");
		check(strstr(panic_message,
		    "words=ffffffff ffffffff ffffffff ffffffff") != NULL,
		    "the synthetic width probe report lost the block words");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE GPT elba=2 n=84 sz=128 first=34 "
		    "last=4294967261 backup=4294967294") != NULL,
		    "the synthetic width probe report lost the GPT gate "
		    "bounds");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE BLOCKS 2=ffffffff 3=ffffffff "
		    "4=ffffffff 5=ffffffff 6=ffffffff 7=ffffffff "
		    "8=ffffffff 9=ffffffff 10=ffffffff 11=ffffffff "
		    "12=ffffffff 13=ffffffff 14=ffffffff 15=ffffffff "
		    "16=ffffffff 17=ffffffff 18=ffffffff 19=ffffffff "
		    "20=ffffffff 21=ffffffff 22=ffffffff") != NULL,
		    "the synthetic width probe report lost the array sums");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE DATA 34=ffffffff\n"
		    "GTA4L-EMMC PROBE LSTLBA=4294967294 sum=ffffffff\n")
		    != NULL,
		    "the synthetic width probe report lost the first-usable "
		    "or last-sector bound");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE CAPTURE fmt=4 hv=ffff "
		    "c2=1/ffff/ffff/ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width probe report lost the capture header "
		    "or CMD2 capture");
		check(strstr(panic_message,
		    "c3p=1/ffff/ffff/ffffffff/ffff/ffff "
		    "c3t=1/63/ffff/ffffffff/1/"
		    "ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width probe report lost the pre-CMD3 or "
		    "timeout capture");
		check(strstr(panic_message,
		    "c7p=1/ffff/ffff/ffffffff/ffff/ffff/"
		    "ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width probe report lost the pre-CMD7 "
		    "state");
		check(strstr(panic_message,
		    "c7q=1/ffff/ffff/ffff/ffff/100000/"
		    "ffffffffffffffff") != NULL,
		    "the synthetic width probe report lost the CMD7 poll "
		    "summary");
		check(strstr(panic_message,
		    "c3l=1/63/ffff/ffff/100000/"
		    "ffffffffffffffff/1000000000 cid=") != NULL,
		    "the synthetic width probe report lost the six-digit "
		    "poll-iteration and ten-digit counter fields");
		check(strstr(panic_message,
		    "c7c=1/99999/ffffffffffffffff/1/99999/"
		    "ffffffffffffffff") != NULL,
		    "the synthetic width probe report lost the valid late CC "
		    "and TC");
		check(strstr(panic_message,
		    "c7t=1/ffff/ffff/ffffffff/0000ffff/ffffffff/"
		    "ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width probe report lost the terminal "
		    "CMD7 state");
		check(strstr(panic_message,
		    "c7x=4/99996/0:ffff:ffff,99997:0000:0000,"
		    "99998:ffff:0000,99999:0000:ffff") != NULL,
		    "the synthetic width probe report lost the late stored "
		    "trace index or the overflow bound");
		check(report_budget_accepts(strlen(panic_message)),
		    "the synthetic width probe report does not fit the "
		    "2048-byte panic buffer including the NUL");
	} else if (strcmp(scenario, "report-width-md") == 0) {
		check(panicked == 1, "the synthetic width MD report did not "
		    "panic");
		check(strstr(panic_message,
		    "pst=ffffffff step=19 log=99999 mb=1") != NULL,
		    "the synthetic width MD report lost the MD step, log count "
		    "or mode marker");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE MBLOCKS 2=ffffffff 3=ffffffff "
		    "4=ffffffff 5=ffffffff 6=ffffffff 7=ffffffff "
		    "8=ffffffff 9=ffffffff 10=ffffffff 11=ffffffff "
		    "12=ffffffff 13=ffffffff 14=ffffffff 15=ffffffff "
		    "16=ffffffff 17=ffffffff 18=ffffffff 19=ffffffff "
		    "20=ffffffff 21=ffffffff 22=ffffffff") != NULL,
		    "the synthetic width MD report lost the CMD18 sums");
		check(strstr(panic_message,
		    "GTA4L-EMMC PROBE LSTLBA=4294967294 sum=ffffffff "
		    "mbsame=0 mbdiv=4294967295 "
		    "drain=1/1/65535/100000\n") != NULL,
		    "the synthetic width MD report lost the widest mismatch "
		    "and drain suffix");
		check(report_budget_accepts(strlen(panic_message)),
		    "the synthetic width MD report does not fit the "
		    "2048-byte panic buffer including the NUL");
	} else if (strcmp(scenario, "report-width-b1") == 0) {
		check(panicked == 1,
		    "the synthetic width B1 report did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-21 stop=8 mci=ffffffff rca=1 "
		    "cap=4294967295 rev=255 hci=ff step=21 log=99999") != NULL,
		    "the synthetic width B1 report lost the widest header");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 BW=1 att=1 req=1 sw=15 swrc=0 tt=0 "
		    "tw=00000000 te=00000000 trc=0 hc=ff es=ffffffff "
		    "b0=ffffffff b1=ffffffff same=1 div=0") != NULL,
		    "the synthetic width B1 report lost the 1-bit baseline");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 BW=4 att=1 req=4 sw=15 swrc=-20 tt=1 "
		    "tw=ffffffff te=ffffffff trc=-20 hc=ff hb=ff ha=ff "
		    "c6=ffffffff es=ffffffff b0=ffffffff b1=ffffffff "
		    "same=0 div=4294967295 bc=1 e183a=ffffffff "
		    "esa=ffffffff") != NULL,
		    "the synthetic width B1 report lost the widest 4-bit "
		    "switch/test fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 BW=8 att=1 req=8 sw=15 swrc=-20 tt=1 "
		    "tw=ffffffff te=ffffffff trc=-20 hc=ff hb=ff ha=ff "
		    "c6=ffffffff es=ffffffff b0=ffffffff b1=ffffffff "
		    "same=0 div=4294967294") != NULL,
		    "the synthetic width B1 report lost the widest 8-bit "
		    "switch/test fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 BLOCKS 4 2=ffffffff 3=ffffffff "
		    "4=ffffffff 5=ffffffff 6=ffffffff 7=ffffffff "
		    "8=ffffffff 9=ffffffff 10=ffffffff 11=ffffffff "
		    "12=ffffffff 13=ffffffff 14=ffffffff 15=ffffffff "
		    "16=ffffffff 17=ffffffff 18=ffffffff 19=ffffffff "
		    "20=ffffffff 21=ffffffff 22=ffffffff") != NULL,
		    "the synthetic width B1 report lost the 4-bit sums");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 BLOCKS 8 2=ffffffff 3=ffffffff "
		    "4=ffffffff 5=ffffffff 6=ffffffff 7=ffffffff "
		    "8=ffffffff 9=ffffffff 10=ffffffff 11=ffffffff "
		    "12=ffffffff 13=ffffffff 14=ffffffff 15=ffffffff "
		    "16=ffffffff 17=ffffffff 18=ffffffff 19=ffffffff "
		    "20=ffffffff 21=ffffffff 22=ffffffff") != NULL,
		    "the synthetic width B1 report lost the 8-bit sums");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 DATA 8 34=ffffffff") != NULL,
		    "the synthetic width B1 report lost the widest data sum");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 LSTLBA 8 4294967294=ffffffff") != NULL,
		    "the synthetic width B1 report lost the widest last "
		    "sector");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 PAD pqdsd=ffffffff pdll=ffffffff "
		    "pdst=ffffffff qdsd=ffffffff dll=ffffffff "
		    "dst=ffffffff bif=none") != NULL,
		    "the synthetic width B1 report lost the before/after "
		    "pad/DLL state");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 FAIL fail=1/63/4294967295/0000ffff/ffff/"
		    "ffff/ffffffff/21") != NULL,
		    "the synthetic width B1 report lost the widest failure "
		    "record");
		check(report_budget_accepts(strlen(panic_message)),
		    "the synthetic width B1 report does not fit the "
		    "2048-byte panic buffer including the NUL");
	} else if ((strcmp(scenario, "cmd7-tc-only") == 0) ||
	    (strcmp(scenario, "cmd7-fresh-window") == 0) ||
	    (strcmp(scenario, "cmd7-busy-error") == 0) ||
	    (strcmp(scenario, "cmd7-cc-only") == 0) ||
	    (strcmp(scenario, "cmd7-late-error") == 0) ||
	    (strcmp(scenario, "cmd7-terminal-error") == 0) ||
	    (strcmp(scenario, "cmd7-trace-overflow") == 0)) {
		check(panicked == 1, "a CMD7 timeout did not panic");
		check(strstr(panic_message, "stage=3") != NULL,
		    "the CMD7 timeout report does not name the stage");
		check(strstr(panic_message, "rc=-7") != NULL,
		    "the CMD7 timeout report does not name the timeout");
		check(strstr(panic_message,
		    "c3l=1/3/0001/0001/1/0000000000000000/1000000")
		    != NULL,
		    "the report lost the successful CMD3 poll history");
		check(strstr(panic_message,
		    "c3p=1/0000/0000/00000000/0033/07ff") != NULL,
		    "the report did not capture the pre-CMD3 state");
		check(strstr(panic_message,
		    "c2=1/0001/0000/05f46619,4d420090,44583638,00150100")
		    != NULL,
		    "the report did not capture the successful CMD2");
		check(strstr(panic_message,
		    "cid=15010044,5836384d,42009005,f4661900 fmt=4 hv=1234")
		    != NULL,
		    "the report does not carry the capture format and "
		    "HOST_VERSION");
		check(strstr(panic_message,
		    "c7p=1/0000/0000/00000000/0033/07ff/"
		    "ef8e4040,fff6dbff,320f5903,00d02701") != NULL,
		    "the report did not capture the pre-CMD7 state");
		check(bad_command_format == 0,
		    "the init sent a command with the wrong flags");
		check(bad_command_order == 0,
		    "the init sent commands in the wrong order or with the "
		    "wrong argument");
		check(cmd_count == 6u,
		    "the init did not stop after CMD7");
		check(last_index == GTA4L_EMMC_CMD7,
		    "the failing command was not CMD7");
		if (strcmp(scenario, "cmd7-busy-error") == 0) {
			check(strstr(panic_message,
			    "fail=1/7/65536/0000071b/0001/0003/00000000/11")
			    != NULL,
			    "the report does not name the busy-phase error");
			check(strstr(panic_message,
			    "c7q=1/0003/0001/0003/0003/1/"
			    "0000000000000000") != NULL,
			    "the busy-phase error poll history mismatch");
			check(strstr(panic_message,
			    "c7c=1/0/0000000000000000/1/0/"
			    "0000000000000000") != NULL,
			    "the busy-phase error lost the observed CC");
			check(strstr(panic_message,
			    "c7t=1/0003/0001/00000000/0000071b/00010000/"
			    "00000900,fff6dbff,320f5903,00d02701") != NULL,
			    "the terminal sample lost the busy-phase error");
			check(strstr(panic_message,
			    "c7x=1/0/0:0003:0001,0:0000:0000,"
			    "0:0000:0000,0:0000:0000") != NULL,
			    "the trace lost the busy-phase error");
		} else if (strcmp(scenario, "cmd7-trace-overflow") == 0) {
			check(strstr(panic_message,
			    "fail=1/7/65536/0000071b/0000/0020/00000000/11")
			    != NULL,
			    "the report does not name the overflow timeout");
			check(strstr(panic_message,
			    "c7q=1/0020/0000/0020/0000/100000/"
			    "00000000000f4240") != NULL,
			    "the overflow poll history mismatch");
			check(strstr(panic_message,
			    "c7c=0/0/0000000000000000/0/0/"
			    "0000000000000000") != NULL,
			    "the overflow run claims a completion");
			check(strstr(panic_message,
			    "c7t=1/0020/0000/00000000/0000071b/00010000/"
			    "00000900,fff6dbff,320f5903,00d02701") != NULL,
			    "the overflow terminal state mismatch");
			check(strstr(panic_message,
			    "c7x=4/99996/0:0020:0000,1:0000:0000,"
			    "2:0020:0000,3:0000:0000") != NULL,
			    "the bounded trace or overflow count mismatch");
			check(strlen(panic_message) < 2048u,
			    "the bounded-trace report exceeds the 2048-byte "
			    "panic-text budget");
		} else if ((strcmp(scenario, "cmd7-tc-only") == 0) ||
		    (strcmp(scenario, "cmd7-fresh-window") == 0)) {
			check(strstr(panic_message,
			    "fail=1/7/65536/0000071b/0000/0002/00000000/11")
			    != NULL,
			    "the report does not name the TC-only timeout");
			check(strstr(panic_message,
			    "c7q=1/0002/0000/0002/0002/100000/"
			    "00000000000f4240") != NULL,
			    "the TC-only poll history mismatch");
			check(strstr(panic_message,
			    "c7c=0/0/0000000000000000/1/0/"
			    "0000000000000000") != NULL,
			    "the TC-only capture lost the observed TC");
			check(strstr(panic_message,
			    "c7x=1/0/0:0002:0000,0:0000:0000,"
			    "0:0000:0000,0:0000:0000") != NULL,
			    "the TC-only trace mismatch");
			check(response_read_before_cc == 0,
			    "the TC-only run read the response before CC");
			if (strcmp(scenario, "cmd7-fresh-window") == 0) {
				check(strstr(panic_message,
				    "c7t=1/0002/0000/00000000/0000071b/"
				    "00010000/40400cf6,dbffef0f,5903ff00,"
				    "00070000") != NULL,
				    "the report did not capture the fresh "
				    "CMD7 window");
				check(strstr(panic_message,
				    "rsp=40400cf6,dbffef0f,5903ff00,00070000")
				    != NULL,
				    "the failure snapshot lost the fresh CMD7 "
				    "window");
			} else {
				check(strstr(panic_message,
				    "c7t=1/0002/0000/00000000/0000071b/"
				    "00010000/ef8e4040,fff6dbff,320f5903,"
				    "00d02701") != NULL,
				    "the report did not retain the unchanged "
				    "CMD7 window");
			}
		} else if (strcmp(scenario, "cmd7-cc-only") == 0) {
			check(strstr(panic_message,
			    "fail=1/7/65536/0000071b/0000/0001/00000000/11")
			    != NULL,
			    "the report does not name the CC-only timeout");
			check(strstr(panic_message,
			    "c7q=1/0001/0000/0001/0001/100000/"
			    "00000000000f4240") != NULL,
			    "the CC-only poll history mismatch");
			check(strstr(panic_message,
			    "c7c=1/0/0000000000000000/0/0/"
			    "0000000000000000") != NULL,
			    "the CC-only capture lost the observed CC");
			check(strstr(panic_message,
			    "c7t=1/0001/0000/00000000/0000071b/00010000/"
			    "00000900,fff6dbff,320f5903,00d02701") != NULL,
			    "the CC-only terminal state mismatch");
			check(strstr(panic_message,
			    "c7x=1/0/0:0001:0000,0:0000:0000,"
			    "0:0000:0000,0:0000:0000") != NULL,
			    "the CC-only trace mismatch");
		} else if (strcmp(scenario, "cmd7-terminal-error") == 0) {
			check(strstr(panic_message,
			    "fail=1/7/65536/0000071b/0001/0003/00000000/11")
			    != NULL,
			    "the report does not name the terminal-read "
			    "error");
			check(strstr(panic_message,
			    "c7q=1/0003/0000/0003/0003/1/"
			    "0000000000000001") != NULL,
			    "the terminal-read error poll history mismatch");
			check(strstr(panic_message,
			    "c7c=1/0/0000000000000000/1/0/"
			    "0000000000000000") != NULL,
			    "the terminal-read error completion history "
			    "mismatch");
			check(strstr(panic_message,
			    "c7t=1/0003/0000/00000000/0000071b/00010000/"
			    "00000900,fff6dbff,320f5903,00d02701") != NULL,
			    "the terminal capture saw the error too early");
			check(strstr(panic_message,
			    "c7x=1/0/0:0003:0000,0:0000:0000,"
			    "0:0000:0000,0:0000:0000") != NULL,
			    "the terminal-read error trace mismatch");
			check(cmd7_error_due == 1,
			    "the terminal-read error never became due");
		} else {
			check(strstr(panic_message,
			    "fail=1/7/65536/0000071b/0001/0003/00000000/11")
			    != NULL,
			    "the report does not name the late busy-phase "
			    "error");
			check(strstr(panic_message,
			    "c7q=1/0003/0000/0003/0003/1/"
			    "0000000000000001") != NULL,
			    "the late-error poll history mismatch");
			check(strstr(panic_message,
			    "c7c=1/0/0000000000000000/1/0/"
			    "0000000000000000") != NULL,
			    "the late-error completion history mismatch");
			check(strstr(panic_message,
			    "c7t=1/0003/0001/00000000/0000071b/00010000/"
			    "00000900,fff6dbff,320f5903,00d02701") != NULL,
			    "the success report lost the late busy-phase "
			    "error");
			check(strstr(panic_message,
			    "c7x=1/0/0:0003:0000,0:0000:0000,"
			    "0:0000:0000,0:0000:0000") != NULL,
			    "the trace lost the late busy-phase error");
		}
	} else if ((strcmp(scenario, "cmd7-cc-then-tc") == 0) ||
	    (strcmp(scenario, "cmd7-tc-then-cc") == 0) ||
	    (strcmp(scenario, "cmd7-simultaneous") == 0) ||
	    (strcmp(scenario, "cmd7-last-poll") == 0) ||
	    (strcmp(scenario, "cmd3-identify") == 0)) {
		check(panicked == 1, "the CMD7 success did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC IDENT REPORT: rc=0") != NULL,
		    "the CMD7 success report does not carry rc=0");
		check(strstr(panic_message,
		    "GTA4L-EMMC IDENT CAPTURE fmt=4 hv=1234") != NULL,
		    "the success report does not carry the capture format");
		check(strstr(panic_message,
		    "c3l=1/3/0001/0001/1/0000000000000000/1000000")
		    != NULL,
		    "the success report lost the CMD3 poll history");
		check(strstr(panic_message,
		    "c3t=0/0/0000/00000000/0/"
		    "00000000,00000000,00000000,00000000") != NULL,
		    "the success report invented a timeout capture");
		check(strstr(panic_message,
		    "c7p=1/0000/0000/00000000/0033/07ff/"
		    "ef8e4040,fff6dbff,320f5903,00d02701") != NULL,
		    "the success report did not capture the pre-CMD7 state");
		check(bad_command_format == 0,
		    "the init sent a command with the wrong flags");
		check(bad_command_order == 0,
		    "the init sent commands in the wrong order or with the "
		    "wrong argument");
		check(cmd_count == 8u,
		    "the init did not complete the identification sequence");
		check(last_index == GTA4L_EMMC_CMD8,
		    "the last command was not the EXT_CSD read");
		if (strcmp(scenario, "cmd7-tc-then-cc") == 0) {
			check(strstr(panic_message,
			    "c7q=1/0003/0000/0002/0003/2/"
			    "000000000000000a") != NULL,
			    "the TC-then-CC poll history mismatch");
			check(strstr(panic_message,
			    "c7c=1/1/000000000000000a/1/0/"
			    "0000000000000000") != NULL,
			    "the TC-then-CC completion history mismatch");
			check(strstr(panic_message,
			    "c7x=2/0/0:0002:0000,1:0003:0000,"
			    "0:0000:0000,0:0000:0000") != NULL,
			    "the TC-then-CC trace mismatch");
			check(strstr(panic_message,
			    "c7t=1/0003/0000/00000000/0000071b/00010000/"
			    "00000900,fff6dbff,320f5903,00d02701") != NULL,
			    "the TC-then-CC terminal state mismatch");
		} else if (strcmp(scenario, "cmd7-simultaneous") == 0) {
			check(strstr(panic_message,
			    "c7q=1/0003/0000/0003/0003/1/"
			    "0000000000000000") != NULL,
			    "the simultaneous poll history mismatch");
			check(strstr(panic_message,
			    "c7c=1/0/0000000000000000/1/0/"
			    "0000000000000000") != NULL,
			    "the simultaneous completion history mismatch");
			check(strstr(panic_message,
			    "c7x=1/0/0:0003:0000,0:0000:0000,"
			    "0:0000:0000,0:0000:0000") != NULL,
			    "the simultaneous trace mismatch");
			check(strstr(panic_message,
			    "c7t=1/0003/0000/00000000/0000071b/00010000/"
			    "00000900,fff6dbff,320f5903,00d02701") != NULL,
			    "the simultaneous terminal state mismatch");
		} else if (strcmp(scenario, "cmd7-cc-then-tc") == 0) {
			check(strstr(panic_message,
			    "c7q=1/0003/0000/0001/0003/6/"
			    "0000000000000032") != NULL,
			    "the CC-then-TC poll history mismatch");
			check(strstr(panic_message,
			    "c7c=1/0/0000000000000000/1/5/"
			    "0000000000000032") != NULL,
			    "the CC-then-TC completion history mismatch");
			check(strstr(panic_message,
			    "c7x=2/0/0:0001:0000,5:0003:0000,"
			    "0:0000:0000,0:0000:0000") != NULL,
			    "the CC-then-TC trace mismatch");
			check(strstr(panic_message,
			    "c7t=1/0003/0000/00000000/0000071b/00010000/"
			    "00000900,fff6dbff,320f5903,00d02701") != NULL,
			    "the CC-then-TC terminal state mismatch");
		} else if (strcmp(scenario, "cmd7-last-poll") == 0) {
			check(strstr(panic_message,
			    "c7q=1/0003/0000/0000/0003/100000/"
			    "00000000000f4236") != NULL,
			    "the last-poll poll history mismatch");
			check(strstr(panic_message,
			    "c7c=1/99999/00000000000f4236/1/99999/"
			    "00000000000f4236") != NULL,
			    "the last-poll completion history mismatch");
			check(strstr(panic_message,
			    "c7x=2/0/0:0000:0000,99999:0003:0000,"
			    "0:0000:0000,0:0000:0000") != NULL,
			    "the last-poll trace mismatch");
			check(strstr(panic_message,
			    "c7t=1/0003/0000/00000000/0000071b/00010000/"
			    "00000900,fff6dbff,320f5903,00d02701") != NULL,
			    "the last-poll terminal state mismatch");
		} else {
			check(strstr(panic_message,
			    "c7q=1/0003/0000/0003/0003/1/"
			    "0000000000000000") != NULL,
			    "the success poll history mismatch");
			check(strstr(panic_message,
			    "c7c=1/0/0000000000000000/1/0/"
			    "0000000000000000") != NULL,
			    "the success completion history mismatch");
			check(strstr(panic_message,
			    "c7x=1/0/0:0003:0000,0:0000:0000,"
			    "0:0000:0000,0:0000:0000") != NULL,
			    "the success trace mismatch");
			check(strstr(panic_message,
			    "c7t=1/0003/0000/00000000/0000071b/00010000/"
			    "00000900,fff6dbff,320f5903,00d02701") != NULL,
			    "the success report lost the terminal CMD7 state");
		}
		check(strlen(panic_message) < 2048u,
		    "the CMD7 success report exceeds the 2048-byte "
		    "panic-text budget");
	} else {
		check(panicked == 1, "a branch-clock failure did not panic");
		check(strstr(panic_message, "GTA4L-EMMC PROBE FAILED") != NULL,
		    "panic is not the probe failure report");
		check(strstr(panic_message, "stage=2") != NULL,
		    "failure report does not name the stage");
		check(strstr(panic_message, "clocks=0") != NULL,
		    "failure report claims the branch clocks were enabled");
		check(sdhci_accesses == 0ul,
		    "SDHCI was read while the branch clocks were off");
		check(sdhci_writes == 0ul,
		    "SDHCI was written while the branch clocks were off");
		check(gcc_accesses > 0ul,
		    "the GCC failure state was not reported");
		/* The report's GCC reads are the only reads of these offsets
		 * in the branch-failure path (reset writes BCR; enable_branch
		 * reads and polls CBCR). */
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CFG] == 1u,
		    "the report did not sample the GCC apps config");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_M] == 1u,
		    "the report did not sample the RCG M value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_N] == 1u,
		    "the report did not sample the RCG N value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_D] == 1u,
		    "the report did not sample the RCG D value");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_APPS_CMD] == 1u,
		    "the report did not sample the RCG command");
		check(gcc_read_count[GTA4L_EMMC_GCC_SDCC1_BCR] == 1u,
		    "the report did not sample the block reset");
		check(strstr(panic_message, "cfg=00000000") != NULL,
		    "the report does not show the untouched RCG config");
		check(strstr(panic_message,
		    "mnd=00000000/00000000/00000000") != NULL,
		    "the report does not show the untouched RCG M/N/D values");
		check(strstr(panic_message, "cmd=00000000") != NULL,
		    "the report does not show the untouched RCG command");
		check(strstr(panic_message, "bcr=00000000") != NULL,
		    "the report does not show the released block reset");
		if (strcmp(scenario, "branch-apps") == 0) {
			check(strstr(panic_message, "apps=80000001") != NULL,
			    "the report does not show the stuck APPS branch");
			check(strstr(panic_message, "ahb=00000001") != NULL,
			    "the report does not show the running AHB branch");
		} else {
			check(strstr(panic_message, "apps=80000000") != NULL,
			    "the report does not show the idle APPS branch");
			check(strstr(panic_message, "ahb=80000001") != NULL,
			    "the report does not show the stuck AHB branch");
		}
	}

	if ((width_form == 0) &&
	    (strcmp(scenario, "stage-off") != 0) &&
	    (strcmp(scenario, "map-fail") != 0) &&
	    (strcmp(scenario, "conflict") != 0) &&
	    (strcmp(scenario, "probe-mb-bare-refuse") != 0) &&
	    (strcmp(scenario, "branch-apps") != 0) &&
	    (strcmp(scenario, "branch-ahb") != 0)) {
		check(sdhci_read_count[GTA4L_EMMC_REG_HOST_VERSION] == 1u,
		    "the report did not sample HOST_VERSION");
	}

	if (strncmp(scenario, "probe-b1c-", 10u) == 0) {
		check(bad_command_format == 0,
		    "a B1C command used the wrong flags");
		check(bad_command_order == 0,
		    "the B1C run sent commands in the wrong order");
		check(bad_command_argument == 0,
		    "the B1C content pass read the wrong LBA list");
	}

	if (failures != 0) {
		fprintf(stderr, "REPORT len=%zu: %s\n", strlen(panic_message),
		    panic_message);
		return 1;
	}
	if (width_form != 0) {
		printf("PASS %s bytes=%zu margin=%zu\n", scenario,
		    strlen(panic_message),
		    sizeof(panic_message) - (strlen(panic_message) + 1u));
	} else {
		printf("PASS %s\n", scenario);
	}
	return 0;
}
