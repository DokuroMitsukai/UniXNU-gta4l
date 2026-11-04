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
/* Models a TIMEOUT_CONTROL write that does not stick (readback 5), used by
 * the DTO arming-guard scenario. */
static int timeout_arm_stuck;
static unsigned int timeout_rung_writes;
static int bad_access;
static unsigned long sdhci_accesses;
static unsigned long sdhci_writes;
static unsigned long gcc_accesses;
static unsigned int sdhci_read_count[SIM_WINDOW];
static unsigned int h12_rung_write_count[SIM_WINDOW];
static unsigned int h13_reset_write_count;
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
 * the readback disagrees with the requested code; cmd6_ignore_width8 and
 * cmd6_ignore_width4 do that only for the named SDR value. bus_test_bad
 * returns the written pattern instead of
 * its complement at bus_test_bad_width (0 = every width). width_corrupt_lba
 * changes one CMD17 payload at width_corrupt_code so the per-width content
 * comparison differs. */
static unsigned int cmd6_count;
static uint32_t cmd6_argument;
static int cmd6_ignore;
static int cmd6_ignore_width8;
static int cmd6_ignore_width4;
static uint32_t cmd6_status = 0x00000900u;
static int cmd6_late_commit;
static uint32_t cmd6_late_value;
static unsigned int cmd6_late_reads;
static unsigned int cmd8_after_cmd6;
static unsigned int cmd8_fail_on_after_cmd6;
static unsigned int cmd8_count;
static unsigned int cmd8_fail_on;
static uint16_t cmd8_fail_error;
/* Stage-B2 HS-timing modeling. hs_timing_ignore_enter leaves byte 185 at 0
 * for the value-1 switch; hs_timing_ignore_exit leaves it at 1 for the
 * value-0 revert. rcg_stuck_after lets the first N RCG updates complete and
 * stalls the next one. hs_content_lba changes one CMD17 payload only while
 * the GCC APPS RCG carries the 50 MHz row, so the HS content comparison sees
 * a divergence that is absent at 400 kHz. */
static int hs_timing_ignore_enter;
static int hs_timing_ignore_exit;
static unsigned int rcg_stuck_after;
static unsigned int rcg_update_count;
static uint32_t hs_content_lba = 0xFFFFFFFFu;
static int bus_test_bad;
static uint8_t bus_test_bad_width;
static int cmd19_1bit_timeout;
/* Stage-H10 width-at-HS modeling. cmd19_4bit_timeout reproduces the observed
 * CMD19 write-phase stall when the host is at 4-bit: CC and BufferWriteReady
 * latch, TC never follows and no error bit is set. cmd6_ignore_revert leaves
 * byte 183 at 1 for the value-0 revert so the width unwind fails. */
static int cmd19_4bit_timeout;
static int cmd6_ignore_revert;
/* Stage-H11 width-value continuation modeling. cmd19_8bit_timeout is the
 * same write-phase stall with the host driving 8-bit. cmd6_width_ddr flags
 * any CMD6 BUS_WIDTH value above the SDR 8-bit code so a scope control can
 * reject a DDR variant. cmd6_fail_bus_width8 makes the value-2 write return
 * an R1 error, and w8_paradox makes the value-2 verification re-read keep
 * byte 183 = 2 while its byte sum stays at the 1-bit-at-HS value. */
static int cmd19_8bit_timeout;
static int cmd6_width_ddr;
static unsigned int cmd6_value4_count;
static int cmd6_fail_bus_width8;
static int cmd6_fail_bus_width_ddr;
static int w8_paradox;
static int w8_paradox_pending;
static int wddr_paradox;
static int wddr_paradox_pending;
static uint8_t bus_test_written[GTA4L_EMMC_BUS_TEST_8_BYTES];
static unsigned int bus_test_written_count;
static uint16_t bus_test_width;
static uint32_t width_corrupt_lba = 0xFFFFFFFFu;
static uint8_t width_corrupt_code;
static uint8_t sim_host_width_code(void);
/* B1 readback-cause modeling. ext_csd_zero_width makes the CMD8 read at that
 * host width code return an all-zero buffer without an error, which models a
 * data-path readback that never filled the buffer. host_width_ignore drops the
 * SDHCI width bits from the HOST_CONTROL write and makes the CMD8 read at a
 * switched width fail with a data CRC, which models a host width that did not
 * take effect. */
static uint8_t ext_csd_zero_width;
static int host_width_ignore;
static int cmd19_error;
/* Stage-H13 rung-1 modeling. cmd19_1bit_timeout supplies the recorded
 * write-phase wedge; h13_reset_stuck keeps the data-reset bit from
 * self-clearing, h13_inhibit_survives leaves DAT_INHIBIT set after the
 * reset, cmd14_error injects a CMD14 transport error and cmd14_status sets
 * the R1 error bit. */
static int h13_reset_stuck;
static int h13_inhibit_survives;
static int cmd14_error;
static int cmd14_status;
/* Stage-H14 rung-2 drain modeling. The drain is recognized after the CMD19
 * write (cmd19_count != 0); h14_cmd12_no_cc withholds the stop completion,
 * h14_cmd12_crc injects a stop transport error and h14_cmd13_crc injects a
 * status-command error. Completion of the drain CMD12 clears the modeled
 * DAT_INHIBIT, mirroring the SDHCI stop-command abort semantics. */
static int h14_cmd12_no_cc;
static int h14_cmd12_crc;
static int h14_cmd13_crc;
static unsigned int h14_cmd12_count;
static unsigned int h14_cmd13_count;
static int cmd2_crc_fail;
static int cmd3_no_cc;
static int cmd3_late_cc;
static int cmd3_cc_delay;
static int cmd3_identify_status;
static int cmd3_timeouts_remaining;
static int cmd3_r1_error_after;
static unsigned int cmd3_count;
static uint32_t cmd3_arguments[4];
static int cmd9_timeout;
static unsigned int cmd17_fail_on;
static uint16_t cmd17_fail_error;
static unsigned int cmd17_count;
static unsigned int cmd17_index;
static unsigned int cmd19_count;
static unsigned int cmd14_count;
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
/* Live PIO readiness, separate from the latched BRR bit. */
static int buffer_ready;
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
	buffer_ready = 1;
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
	if ((hs_content_lba != 0xFFFFFFFFu) &&
	    (sim_load(gcc, GTA4L_EMMC_GCC_SDCC1_APPS_CFG, 4u) ==
	    GTA4L_EMMC_RCG_50M_CFG) &&
	    (lba == hs_content_lba)) {
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
	if ((offset == GTA4L_EMMC_GCC_SDCC1_APPS_CMD) &&
	    ((value & GTA4L_EMMC_RCG_CMD_UPDATE) != 0) &&
	    (rcg_stuck == 0) &&
	    ((rcg_stuck_after == 0) ||
	    (rcg_update_count < rcg_stuck_after))) {
		/* The RCG update bit is self-clearing until the arm count. */
		value &= ~GTA4L_EMMC_RCG_CMD_UPDATE;
		rcg_update_count++;
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
		uint32_t response = cmd3_identify_status ?
		    0x00000500u : 0x00000900u;

		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX)) {
			bad_command_format = 1;
		}
		if (cmd3_count < 4u) {
			cmd3_arguments[cmd3_count] = last_argument;
		}
		cmd3_count++;
		if ((cmd3_r1_error_after > 0) &&
		    (cmd3_count >= (unsigned int)cmd3_r1_error_after)) {
			response |= 0x00000080u;
		}
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u, response);
		cmd3_normal_reads = 0;
		if (cmd3_timeouts_remaining > 0) {
			cmd3_timeouts_remaining--;
		} else if ((cmd3_no_cc == 0) && (cmd3_late_cc == 0) &&
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
		if (cmd19_count > 0u) {
			/* Stage-H14 drain status command; the identification
			 * CMD13 above the write is not counted. */
			h14_cmd13_count++;
			if (h14_cmd13_crc != 0) {
				error_int |= GTA4L_EMMC_INT_ERR_CMD_CRC;
				return;
			}
		}
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u, 0x00000900u);
		normal_int |= GTA4L_EMMC_INT_CC;
		return;
	}
	if (index == GTA4L_EMMC_CMD6) {
		uint32_t value;
		uint32_t ext_index;

		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX)) {
			bad_command_format = 1;
		}
		cmd6_count++;
		cmd6_argument = last_argument;
		value = (last_argument >> 8) & 0xFFu;
		ext_index = (last_argument >> 16) & 0xFFu;
		if ((ext_index == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF) &&
		    (value == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_RESERVED_4)) {
			/* Count every value-4 attempt, including an R1 refusal,
			 * so the H12 scope check can bind the DDR request. */
			cmd6_value4_count++;
			cmd6_width_ddr = 1;
		}
		if ((cmd6_fail_bus_width_ddr != 0) &&
		    (ext_index == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF) &&
		    (value == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_RESERVED_4)) {
			/* The H12 DDR value is refused with an R1 error before
			 * any commit or host-width change. */
			sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u,
			    0x0000A000u);
			normal_int |= GTA4L_EMMC_INT_CC;
			return;
		}
		if ((cmd6_fail_bus_width8 != 0) &&
		    (ext_index == GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF) &&
		    (value == 2u)) {
			/* The H11 8-bit attempt is refused with an R1 error
			 * before any commit or host-width change. */
			sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u,
			    0x0000A000u);
			normal_int |= GTA4L_EMMC_INT_CC;
			return;
		}
		if ((cmd6_status & GTA4L_EMMC_R1_ERROR_MASK) == 0u) {
			cmd8_after_cmd6 = 0;
			if ((ext_index ==
			    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF) &&
			    (value > GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8)) {
				/* DDR (or any non-SDR) BUS_WIDTH value: the
				 * H11 rung must never issue one. */
				cmd6_width_ddr = 1;
			}
			if (ext_index ==
			    GTA4L_EMMC_EXT_CSD_HS_TIMING_OFF) {
				if (((value == 1u) &&
				    (hs_timing_ignore_enter == 0)) ||
				    ((value == 0u) &&
				    (hs_timing_ignore_exit == 0))) {
					ext_csd[
					    GTA4L_EMMC_EXT_CSD_HS_TIMING_OFF] =
					    (uint8_t)value;
				}
			} else if (cmd6_late_commit != 0) {
				/* The write is invisible to the verification
				 * readback and appears on a later CMD8, which
				 * models a late BUS_WIDTH commit. */
				cmd6_late_value = value;
				cmd6_late_reads = 0;
			} else if ((cmd6_ignore == 0) &&
			    ((cmd6_ignore_width8 == 0) ||
			    (value != 2u)) &&
			    ((cmd6_ignore_width4 == 0) ||
			    (value != 1u)) &&
			    ((cmd6_ignore_revert == 0) ||
			    (value != 0u))) {
				ext_csd[GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF] =
				    (uint8_t)value;
				if ((w8_paradox != 0) && (value == 2u)) {
					w8_paradox_pending = 1;
				}
				if ((wddr_paradox != 0) &&
				    (value ==
				    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_RESERVED_4)) {
					wddr_paradox_pending = 1;
				}
			}
		}
		sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u, cmd6_status);
		normal_int |= GTA4L_EMMC_INT_CC;
		return;
	}
	if (index == GTA4L_EMMC_CMD19) {
		cmd19_count++;
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
		if ((cmd19_1bit_timeout != 0) && (cmd6_count == 0u)) {
			/* Model the observed 1-bit write-phase timeout: the
			 * command is accepted, CC and BufferWriteReady latch,
			 * but TC never follows and no error bit is set. The
			 * present state is the observed write-direction
			 * sample, stored at the terminal point only. */
			sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u,
			    0x00000900u);
			sim_store(sdhci, GTA4L_EMMC_REG_PRESENT_STATE, 4u,
			    0x03f801f6u);
			normal_int |= GTA4L_EMMC_INT_CC |
			    GTA4L_EMMC_INT_BWR;
			return;
		}
		if ((cmd19_4bit_timeout != 0) &&
		    (sim_host_width_code() ==
		    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4)) {
			/* Same terminal class as the 1-bit wedge, now with the
			 * host driving 4-bit: the write phase ACKs but never
			 * completes. */
			sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u,
			    0x00000900u);
			sim_store(sdhci, GTA4L_EMMC_REG_PRESENT_STATE, 4u,
			    0x03f801f6u);
			normal_int |= GTA4L_EMMC_INT_CC |
			    GTA4L_EMMC_INT_BWR;
			return;
		}
		if ((cmd19_8bit_timeout != 0) &&
		    (sim_host_width_code() ==
		    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8)) {
			/* The H11 8-bit/HS form of the same write-phase
			 * wedge. */
			sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u,
			    0x00000900u);
			sim_store(sdhci, GTA4L_EMMC_REG_PRESENT_STATE, 4u,
			    0x03f801f6u);
			normal_int |= GTA4L_EMMC_INT_CC |
			    GTA4L_EMMC_INT_BWR;
			return;
		}
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
		uint32_t width_code;

		cmd14_count++;
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
		/* The width code follows the host width the controller
		 * currently drives, not the CMD6 count: the H10 rung's first
		 * CMD6 is the byte-185 switch, so a count-based code would
		 * misname its 4-bit test as 8-bit. */
		width_code = sim_host_width_code();
		for (index2 = 0; index2 < bus_test_width; index2++) {
			if ((bus_test_bad != 0) &&
			    ((bus_test_bad_width == 0u) ||
			    (bus_test_bad_width == width_code))) {
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
		if (cmd14_status != 0) {
			sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u,
			    0x00000980u);
		}
		if (cmd14_error != 0) {
			error_int |= (uint16_t)cmd14_error;
			/* The optional pattern CRC16 is ignored by the host;
			 * deliver the data so the verified path can compare it.
			 * Every other injected error stays terminal. */
			if (cmd14_error != GTA4L_EMMC_INT_ERR_DATA_CRC) {
				return;
			}
		}
		buffer_ready = 1;
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
		    ((block_count != SIM_GPT_ARRAY_BLOCKS) &&
		    (block_count != 8u)) ||
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
		if (cmd19_count > 0u) {
			/* Stage-H14 forced post-write drain. The stop is not
			 * tied to an open CMD18 transfer, so it is accepted
			 * on its own encoding and the modeled data inhibit is
			 * released when the command completes. This is the
			 * explicit SDHCI stop-command DAT_INHIBIT exemption. */
			if (flags != (GTA4L_EMMC_CMD_RESP_SHORT_BUSY |
			    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX)) {
				bad_command_format = 1;
			}
			if (last_argument != 0u) {
				bad_command_format = 1;
			}
			h14_cmd12_count++;
			sim_store(sdhci, GTA4L_EMMC_REG_PRESENT_STATE, 4u,
			    sim_load(sdhci, GTA4L_EMMC_REG_PRESENT_STATE,
			    4u) & ~GTA4L_EMMC_PRESENT_DAT_INHIBIT);
			if (h14_cmd12_crc != 0) {
				error_int |= GTA4L_EMMC_INT_ERR_CMD_CRC;
				return;
			}
			if (h14_cmd12_no_cc != 0) {
				return;
			}
			sim_store(sdhci, GTA4L_EMMC_REG_RESPONSE, 4u,
			    0x00000900u);
			normal_int |= GTA4L_EMMC_INT_CC |
			    GTA4L_EMMC_INT_TC;
			return;
		}
		if (flags != (GTA4L_EMMC_CMD_RESP_SHORT_BUSY |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX)) {
			bad_command_format = 1;
		}
		if ((last_argument != 0u) ||
		    ((mb_count != SIM_GPT_ARRAY_BLOCKS) &&
		    (mb_count != 8u)) ||
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
			cmd8_count++;
			if ((cmd8_fail_on != 0u) &&
			    (cmd8_count == cmd8_fail_on)) {
				error_int |= cmd8_fail_error;
				return;
			}
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
			    (sim_host_width_code() == ext_csd_zero_width)) {
				memset(buffer_data, 0, sizeof(buffer_data));
			} else {
				sim_fill_buffer_words(ext_csd);
				if ((w8_paradox_pending != 0) &&
				    (ext_csd[
				    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF] ==
				    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8)) {
					/* Keep byte 183 = 2 but drop the
					 * sum by 2, so the readback word
					 * and the re-read sum disagree
					 * (the H11 W8 paradox row). */
					w8_paradox_pending = 0;
					buffer_data[
					    GTA4L_EMMC_EXT_CSD_REV_OFF / 4u] -=
					    2u << (8u *
					    (GTA4L_EMMC_EXT_CSD_REV_OFF % 4u));
				}
				if ((wddr_paradox_pending != 0) &&
				    (ext_csd[
				    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF] ==
				    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_RESERVED_4)) {
					/* Keep byte 183 = 4 but drop the
					 * sum by 4, so the readback word
					 * and the re-read sum disagree
					 * (the H12 DDR paradox row). */
					wddr_paradox_pending = 0;
					buffer_data[
					    GTA4L_EMMC_EXT_CSD_REV_OFF / 4u] -=
					    4u << (8u *
					    (GTA4L_EMMC_EXT_CSD_REV_OFF % 4u));
				}
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
		if (offset == GTA4L_EMMC_REG_PRESENT_STATE) {
			uint32_t value = sim_load(sdhci,
			    GTA4L_EMMC_REG_PRESENT_STATE, 4u);

			if (buffer_ready != 0) {
				value |=
				    GTA4L_EMMC_PRESENT_BUFFER_READ_READY;
			}
			return value;
		}
		if (offset == GTA4L_EMMC_REG_BUFFER_DATA_PORT) {
			uint32_t word = 0;

			if (buffer_index <
			    (GTA4L_EMMC_BLOCK_BYTES / 4u)) {
				word = buffer_data[buffer_index];
			}
			buffer_index++;
			if (buffer_index >=
			    (GTA4L_EMMC_BLOCK_BYTES / 4u)) {
				buffer_ready = 0;
			}
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
		if ((offset < SIM_WINDOW) && (stage == 12) &&
		    (cmd17_count >= 25u)) {
			h12_rung_write_count[offset]++;
		}
		if (((stage == 13) || (stage == 14) || (stage == 17)) &&
		    (offset == GTA4L_EMMC_REG_SOFTWARE_RESET) &&
		    ((value & GTA4L_EMMC_RESET_DATA) != 0)) {
			h13_reset_write_count++;
		}
		/* The software-reset bits are self-clearing. Model the
		 * completion so later stages can be exercised. The rung-1
		 * data reset also clears the recorded data inhibit unless a
		 * fault keeps the bit set. */
		if (offset == GTA4L_EMMC_REG_SOFTWARE_RESET) {
			if ((value & GTA4L_EMMC_RESET_DATA) != 0) {
				if (h13_reset_stuck == 0) {
					value = 0;
					if (h13_inhibit_survives == 0) {
						uint32_t present =
						    sim_load(sdhci,
						    GTA4L_EMMC_REG_PRESENT_STATE,
						    4u);

						present &= (uint32_t)
						    ~GTA4L_EMMC_PRESENT_DAT_INHIBIT;
						sim_store(sdhci,
						    GTA4L_EMMC_REG_PRESENT_STATE,
						    4u, present);
					}
				}
			} else {
				value = 0;
			}
		}
		if (offset == GTA4L_EMMC_REG_TIMEOUT_CONTROL) {
			/* Only the stage-7 rung write is faulted; the init
			 * write must still program the real value. */
			if ((stage == 7) && (cmd17_count >= 25u)) {
				timeout_rung_writes++;
				if ((timeout_arm_stuck != 0) &&
				    (timeout_rung_writes == 1u)) {
					value = 5u;
				}
			}
			sim_store(sdhci, offset, 1u, value);
			return;
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
		if ((offset < SIM_WINDOW) && (stage == 12) &&
		    (cmd17_count >= 25u)) {
			h12_rung_write_count[offset]++;
		}
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
		if ((offset < SIM_WINDOW) && (stage == 12) &&
		    (cmd17_count >= 25u)) {
			h12_rung_write_count[offset]++;
		}
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
					if (((cmd19_1bit_timeout != 0) &&
					    (cmd6_count == 0u)) ||
					    ((cmd19_4bit_timeout != 0) &&
					    (sim_host_width_code() ==
					    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4)) ||
					    ((cmd19_8bit_timeout != 0) &&
					    (sim_host_width_code() ==
					    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8))) {
						/* Write-phase timeout: BufferWriteReady
						 * stays latched and TC never
						 * follows. */
					} else {
						normal_int &= (uint16_t)
						    ~GTA4L_EMMC_INT_BWR;
						normal_int |=
						    GTA4L_EMMC_INT_TC;
					}
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
static const uint8_t order_h9_clean[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7, GTA4L_EMMC_CMD13
};
static const uint8_t order_h9_retry[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD3, GTA4L_EMMC_CMD9, GTA4L_EMMC_CMD7,
	GTA4L_EMMC_CMD13
};
static const uint8_t order_h9_retry_fail[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD3
};
static const uint8_t order_h9_retry_later[] = {
	GTA4L_EMMC_CMD0, GTA4L_EMMC_CMD1, GTA4L_EMMC_CMD2, GTA4L_EMMC_CMD3,
	GTA4L_EMMC_CMD3, GTA4L_EMMC_CMD9
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
/* Stage B1C with the stage-6 1-bit discriminator: one extra CMD19/CMD14
 * round trip after the stage-A pass and before the first CMD6. */
static const uint8_t order_probe_b1_1bit[] = {
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
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD14,
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
static const uint8_t order_probe_b1c_1bit[] = {
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
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD14,
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
/* Stage 7: the stage-A pass, then the 8-block CMD18 with its explicit stop,
 * a block-0 recovery read and the repeated 1-bit CMD19/CMD14 round trip. */
static const uint8_t order_probe_dto[] = {
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
	GTA4L_EMMC_CMD18, GTA4L_EMMC_CMD12,
	GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD14
};
/* Stage 8: the stage-A pass, then the B2 sequence - a pre-switch EXT_CSD
 * read, the core's own before-read + CMD6 + CMD13 + after-read, the 25-value
 * content pass at HS, the clock-down + CMD6 + CMD13 + after-read revert and
 * one block-0 recovery read. A failure is a prefix of this order. */
static const uint8_t order_probe_b2[] = {
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
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17, GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17
};
/* Stage 10: the stage-A pass, then the H10 sequence - the pre-rung EXT_CSD
 * read, the HS enter, the 4-bit switch at HS, one CMD19/CMD14 round trip,
 * the 25-value content pass at 4-bit/HS, the width revert, the HS revert and
 * one block-0 recovery read. A failure is a prefix of this order. */
static const uint8_t order_probe_h10[] = {
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
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
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
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17
};
#define ORDER_H10_BASE 33u
static uint8_t h10_order_buffer[160];
static const uint8_t h10_suffix_hs_ignored[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h10_suffix_width_ignored[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h10_suffix_bustest_timeout[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD19,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h10_suffix_bustest_mismatch[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD14,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h10_suffix_content_read[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD14,
	GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
/* Stage 11: the stage-A pass, then the H11 sequence - the pre-rung EXT_CSD
 * read, the HS enter, the record-only 4-bit replay, the 8-bit attempt, one
 * CMD19/CMD14 round trip, the 25-value content pass at 8-bit/HS, the width
 * revert, the HS revert and one block-0 recovery read. A failure is a prefix
 * of this order. */
static const uint8_t order_probe_h11[] = {
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
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
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
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17
};
#define ORDER_H11_BASE 33u
static uint8_t h11_order_buffer[180];
static const uint8_t h11_suffix_clock_busy[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h11_suffix_w4_anomaly[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h11_suffix_w8_close[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h11_suffix_w8_err[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h11_suffix_bustest_timeout[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD19
};
static const uint8_t h11_suffix_bustest_mismatch[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD14
};
static const uint8_t h11_suffix_content_read[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD14,
	GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h11_suffix_no_q[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
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
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
/* Stage 12: the stage-A pass, then the H12 sequence - the pre-rung EXT_CSD
 * read, the HS enter, the record-only byte-183 value-4 DDR attempt and its
 * verification read, the width revert, the HS revert and one block-0
 * recovery read. A failure is a prefix of this order. */
static const uint8_t order_probe_h12[] = {
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
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17
};
#define ORDER_H12_BASE 33u
static uint8_t h12_order_buffer[64];
static const uint8_t h12_suffix_pre_read[] = {
	GTA4L_EMMC_CMD8
};
static const uint8_t h12_suffix_hs_ignored[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h12_suffix_clock_busy[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h12_suffix_wddr_err[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t h12_suffix_no_q[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13, GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
/* Stage 13: the stage-A pass, then the rung-1 sequence - one 1-bit CMD19
 * write, one bounded data reset, one CMD14 read and one block-0 recovery
 * read. A reset that does not self-clear or a surviving DAT_INHIBIT stops
 * before CMD14, so its order is a prefix with no CMD14 entry. */
static const uint8_t order_probe_h13[] = {
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
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD14,
	GTA4L_EMMC_CMD17
};
#define ORDER_H13_BASE 33u
static uint8_t h13_order_buffer[64];
static const uint8_t h13_suffix_no_cmd14[] = {
	GTA4L_EMMC_CMD19
};
/* Stage 14: the stage-A pass, then one 1-bit CMD19 write, one bounded data
 * reset, one forced CMD12 and, when the stop completed, one CMD13, one CMD14
 * read and one block-0 recovery read. A reset/inhibit stop keeps the Q read
 * but drops the drain and CMD14; a silent or failed CMD12 drops CMD13 and
 * keeps the record-only CMD14/Q tail. */
static const uint8_t order_probe_h14[] = {
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
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD12, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD14, GTA4L_EMMC_CMD17
};
#define ORDER_H14_BASE 33u
static uint8_t h14_order_buffer[64];
static const uint8_t h14_suffix_no_drain[] = {
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD17
};
static const uint8_t h14_suffix_no_cmd13[] = {
	GTA4L_EMMC_CMD19, GTA4L_EMMC_CMD12, GTA4L_EMMC_CMD14,
	GTA4L_EMMC_CMD17
};
/* The B2 failure paths deviate from the full order only after the switch
 * step. Each suffix below is spliced after the shared 33-command init plus
 * stage-A prefix. */
#define ORDER_B2_BASE 33u
static uint8_t b2_order_buffer[128];
static const uint8_t b2_suffix_ignored[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t b2_suffix_clock_busy[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t b2_suffix_content_read[] = {
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8,
	GTA4L_EMMC_CMD17,
	GTA4L_EMMC_CMD8, GTA4L_EMMC_CMD6, GTA4L_EMMC_CMD13,
	GTA4L_EMMC_CMD8
};
static const uint8_t b2_suffix_pre_read[] = {
	GTA4L_EMMC_CMD8
};

static void
set_b2_order(const uint8_t *suffix, unsigned int count);
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

static void
set_b2_order(const uint8_t *suffix, unsigned int count)
{
	unsigned int index;

	for (index = 0; index < ORDER_B2_BASE; index++) {
		b2_order_buffer[index] = order_probe_b2[index];
	}
	for (index = 0; index < count; index++) {
		b2_order_buffer[ORDER_B2_BASE + index] = suffix[index];
	}
	set_expected_order(b2_order_buffer, ORDER_B2_BASE + count);
}

static void
set_h10_order(const uint8_t *suffix, unsigned int count)
{
	unsigned int index;

	for (index = 0; index < ORDER_H10_BASE; index++) {
		h10_order_buffer[index] = order_probe_h10[index];
	}
	for (index = 0; index < count; index++) {
		h10_order_buffer[ORDER_H10_BASE + index] = suffix[index];
	}
	set_expected_order(h10_order_buffer, ORDER_H10_BASE + count);
}

static void
set_h11_order(const uint8_t *suffix, unsigned int count)
{
	unsigned int index;

	for (index = 0; index < ORDER_H11_BASE; index++) {
		h11_order_buffer[index] = order_probe_h11[index];
	}
	for (index = 0; index < count; index++) {
		h11_order_buffer[ORDER_H11_BASE + index] = suffix[index];
	}
	set_expected_order(h11_order_buffer, ORDER_H11_BASE + count);
}

static void
set_h12_order(const uint8_t *suffix, unsigned int count)
{
	unsigned int index;

	for (index = 0; index < ORDER_H12_BASE; index++) {
		h12_order_buffer[index] = order_probe_h12[index];
	}
	for (index = 0; index < count; index++) {
		h12_order_buffer[ORDER_H12_BASE + index] = suffix[index];
	}
	set_expected_order(h12_order_buffer, ORDER_H12_BASE + count);
}

static void
set_h13_order(const uint8_t *suffix, unsigned int count)
{
	unsigned int index;

	for (index = 0; index < ORDER_H13_BASE; index++) {
		h13_order_buffer[index] = order_probe_h13[index];
	}
	for (index = 0; index < count; index++) {
		h13_order_buffer[ORDER_H13_BASE + index] = suffix[index];
	}
	set_expected_order(h13_order_buffer, ORDER_H13_BASE + count);
}

static void
set_h14_order(const uint8_t *suffix, unsigned int count)
{
	unsigned int index;

	for (index = 0; index < ORDER_H14_BASE; index++) {
		h14_order_buffer[index] = order_probe_h14[index];
	}
	for (index = 0; index < count; index++) {
		h14_order_buffer[ORDER_H14_BASE + index] = suffix[index];
	}
	set_expected_order(h14_order_buffer, ORDER_H14_BASE + count);
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
	} else if (strcmp(scenario, "probe-h9-clean-no-fire") == 0) {
		stage = 9;
		cmd7_tc_immediate = 1;
		set_expected_order(order_h9_clean,
		    (unsigned int)(sizeof(order_h9_clean) /
		    sizeof(order_h9_clean[0])));
	} else if (strcmp(scenario, "probe-h9-fire-pass") == 0) {
		stage = 9;
		cmd3_timeouts_remaining = 1;
		cmd7_tc_immediate = 1;
		set_expected_order(order_h9_retry,
		    (unsigned int)(sizeof(order_h9_retry) /
		    sizeof(order_h9_retry[0])));
	} else if (strcmp(scenario, "probe-h9-fire-fail") == 0) {
		stage = 9;
		cmd3_timeouts_remaining = 2;
		set_expected_order(order_h9_retry_fail,
		    (unsigned int)(sizeof(order_h9_retry_fail) /
		    sizeof(order_h9_retry_fail[0])));
	} else if (strcmp(scenario, "probe-h9-fire-second-ident") == 0) {
		stage = 9;
		cmd3_timeouts_remaining = 1;
		cmd9_timeout = 1;
		set_expected_order(order_h9_retry_later,
		    (unsigned int)(sizeof(order_h9_retry_later) /
		    sizeof(order_h9_retry_later[0])));
	} else if (strcmp(scenario, "probe-h9-mismatch") == 0) {
		stage = 9;
		cmd3_timeouts_remaining = 1;
		cmd3_r1_error_after = 2;
		set_expected_order(order_h9_retry_fail,
		    (unsigned int)(sizeof(order_h9_retry_fail) /
		    sizeof(order_h9_retry_fail[0])));
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
	} else if ((strcmp(scenario, "probe-b1-1bit-pass") == 0) ||
	    (strcmp(scenario, "probe-b1-1bit-timeout") == 0) ||
	    (strcmp(scenario, "probe-b1-1bit-mismatch") == 0) ||
	    (strcmp(scenario, "probe-b1-1bit-continue") == 0)) {
		/* Stage 6: the stage-B1C flow plus one 1-bit CMD19/CMD14
		 * round trip after the stage-A pass and before the first
		 * CMD6. A failed 1-bit test stops with stop=9; a passing one
		 * falls through to the stage-5 flow. */
		stage = 6;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		if (strcmp(scenario, "probe-b1-1bit-continue") == 0) {
			set_expected_order(order_probe_b1c_1bit,
			    (unsigned int)(sizeof(order_probe_b1c_1bit) /
			    sizeof(order_probe_b1c_1bit[0])));
		} else {
			set_expected_order(order_probe_b1_1bit,
			    (unsigned int)(sizeof(order_probe_b1_1bit) /
			    sizeof(order_probe_b1_1bit[0])));
		}
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if (strcmp(scenario, "probe-b1-1bit-timeout") == 0) {
			cmd19_1bit_timeout = 1;
		} else if (strcmp(scenario,
		    "probe-b1-1bit-mismatch") == 0) {
			bus_test_bad = 1;
			bus_test_bad_width = 0u;
		} else if (strcmp(scenario,
		    "probe-b1-1bit-continue") == 0) {
			cmd6_ignore = 1;
		}
	} else if ((strcmp(scenario, "probe-b2-pass") == 0) ||
	    (strcmp(scenario, "probe-b2-ignored") == 0) ||
	    (strcmp(scenario, "probe-b2-clock-busy") == 0) ||
	    (strcmp(scenario, "probe-b2-content-div") == 0) ||
	    (strcmp(scenario, "probe-b2-content-read") == 0) ||
	    (strcmp(scenario, "probe-b2-revert-fail") == 0) ||
	    (strcmp(scenario, "probe-b2-q-fail") == 0) ||
	    (strcmp(scenario, "probe-b2-pre-read-fail") == 0)) {
		/* Stage 8: HS-timing rung. The stage-A pass runs at 400 kHz,
		 * then the byte-185 switch, the 50 MHz content pass, the
		 * revert and the recovery read. One fault per failure case. */
		stage = 8;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		if (strcmp(scenario, "probe-b2-ignored") == 0) {
			set_b2_order(b2_suffix_ignored,
			    (unsigned int)(sizeof(b2_suffix_ignored) /
			    sizeof(b2_suffix_ignored[0])));
		} else if (strcmp(scenario,
		    "probe-b2-clock-busy") == 0) {
			set_b2_order(b2_suffix_clock_busy,
			    (unsigned int)(sizeof(b2_suffix_clock_busy) /
			    sizeof(b2_suffix_clock_busy[0])));
		} else if (strcmp(scenario,
		    "probe-b2-content-read") == 0) {
			set_b2_order(b2_suffix_content_read,
			    (unsigned int)(sizeof(b2_suffix_content_read) /
			    sizeof(b2_suffix_content_read[0])));
		} else if (strcmp(scenario,
		    "probe-b2-pre-read-fail") == 0) {
			set_b2_order(b2_suffix_pre_read,
			    (unsigned int)(sizeof(b2_suffix_pre_read) /
			    sizeof(b2_suffix_pre_read[0])));
		} else {
			set_expected_order(order_probe_b2,
			    (unsigned int)(sizeof(order_probe_b2) /
			    sizeof(order_probe_b2[0])));
		}
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if (strcmp(scenario, "probe-b2-ignored") == 0) {
			hs_timing_ignore_enter = 1;
		} else if (strcmp(scenario,
		    "probe-b2-clock-busy") == 0) {
			rcg_stuck_after = 1u;
		} else if (strcmp(scenario,
		    "probe-b2-content-div") == 0) {
			hs_content_lba = SIM_GPT_FIRST_USABLE;
		} else if (strcmp(scenario,
		    "probe-b2-content-read") == 0) {
			cmd17_fail_on = 26u;
			cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario,
		    "probe-b2-revert-fail") == 0) {
			hs_timing_ignore_exit = 1;
		} else if (strcmp(scenario, "probe-b2-q-fail") == 0) {
			cmd17_fail_on = 51u;
			cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario,
		    "probe-b2-pre-read-fail") == 0) {
			cmd8_fail_on = 2u;
			cmd8_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		}
	} else if ((strcmp(scenario, "probe-h10-pass") == 0) ||
	    (strcmp(scenario, "probe-h10-hs-ignored") == 0) ||
	    (strcmp(scenario, "probe-h10-clock-busy") == 0) ||
	    (strcmp(scenario, "probe-h10-width-ignored") == 0) ||
	    (strcmp(scenario, "probe-h10-bustest-timeout") == 0) ||
	    (strcmp(scenario, "probe-h10-bustest-mismatch") == 0) ||
	    (strcmp(scenario, "probe-h10-content-div") == 0) ||
	    (strcmp(scenario, "probe-h10-content-read") == 0) ||
	    (strcmp(scenario, "probe-h10-width-revert-fail") == 0) ||
	    (strcmp(scenario, "probe-h10-hs-revert-fail") == 0) ||
	    (strcmp(scenario, "probe-h10-q-fail") == 0)) {
		/* Stage 10: width-at-HS continuation. The stage-A pass runs at
		 * 400 kHz/1-bit, then the HS enter, the 4-bit switch at HS,
		 * one CMD19/CMD14 round trip, the content pass at 4-bit/HS,
		 * the width and HS reverts and the recovery read. One fault
		 * per failure case; the non-prefix failure orders splice
		 * their own suffix after the shared base. */
		stage = 10;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		if (strcmp(scenario, "probe-h10-hs-ignored") == 0) {
			set_h10_order(h10_suffix_hs_ignored,
			    (unsigned int)(sizeof(h10_suffix_hs_ignored) /
			    sizeof(h10_suffix_hs_ignored[0])));
		} else if (strcmp(scenario,
		    "probe-h10-width-ignored") == 0) {
			set_h10_order(h10_suffix_width_ignored,
			    (unsigned int)(sizeof(h10_suffix_width_ignored) /
			    sizeof(h10_suffix_width_ignored[0])));
		} else if (strcmp(scenario,
		    "probe-h10-bustest-timeout") == 0) {
			set_h10_order(h10_suffix_bustest_timeout,
			    (unsigned int)(sizeof(h10_suffix_bustest_timeout) /
			    sizeof(h10_suffix_bustest_timeout[0])));
		} else if (strcmp(scenario,
		    "probe-h10-bustest-mismatch") == 0) {
			set_h10_order(h10_suffix_bustest_mismatch,
			    (unsigned int)(sizeof(h10_suffix_bustest_mismatch) /
			    sizeof(h10_suffix_bustest_mismatch[0])));
		} else if (strcmp(scenario,
		    "probe-h10-content-read") == 0) {
			set_h10_order(h10_suffix_content_read,
			    (unsigned int)(sizeof(h10_suffix_content_read) /
			    sizeof(h10_suffix_content_read[0])));
		} else {
			set_expected_order(order_probe_h10,
			    (unsigned int)(sizeof(order_probe_h10) /
			    sizeof(order_probe_h10[0])));
		}
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if (strcmp(scenario, "probe-h10-hs-ignored") == 0) {
			hs_timing_ignore_enter = 1;
		} else if (strcmp(scenario,
		    "probe-h10-clock-busy") == 0) {
			rcg_stuck_after = 1u;
		} else if (strcmp(scenario,
		    "probe-h10-width-ignored") == 0) {
			cmd6_ignore = 1;
		} else if (strcmp(scenario,
		    "probe-h10-bustest-timeout") == 0) {
			cmd19_4bit_timeout = 1;
		} else if (strcmp(scenario,
		    "probe-h10-bustest-mismatch") == 0) {
			bus_test_bad = 1;
			bus_test_bad_width = GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		} else if (strcmp(scenario,
		    "probe-h10-content-div") == 0) {
			hs_content_lba = SIM_GPT_FIRST_USABLE;
		} else if (strcmp(scenario,
		    "probe-h10-content-read") == 0) {
			cmd17_fail_on = 26u;
			cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario,
		    "probe-h10-width-revert-fail") == 0) {
			cmd6_ignore_revert = 1;
		} else if (strcmp(scenario,
		    "probe-h10-hs-revert-fail") == 0) {
			hs_timing_ignore_exit = 1;
		} else if (strcmp(scenario, "probe-h10-q-fail") == 0) {
			cmd17_fail_on = 51u;
			cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		}
	} else if ((strcmp(scenario, "probe-h11-honored-pass") == 0) ||
	    (strcmp(scenario, "probe-h11-w4-replay") == 0) ||
	    (strcmp(scenario, "probe-h11-w4-mismatch") == 0) ||
	    (strcmp(scenario, "probe-h11-w8-div") == 0) ||
	    (strcmp(scenario, "probe-h11-w8-err") == 0) ||
	    (strcmp(scenario, "probe-h11-w4-anomaly") == 0) ||
	    (strcmp(scenario, "probe-h11-clock-busy") == 0) ||
	    (strcmp(scenario, "probe-h11-bustest-timeout") == 0) ||
	    (strcmp(scenario, "probe-h11-bustest-mismatch") == 0) ||
	    (strcmp(scenario, "probe-h11-content-read") == 0) ||
	    (strcmp(scenario, "probe-h11-content-div") == 0) ||
	    (strcmp(scenario, "probe-h11-width-revert-fail") == 0) ||
	    (strcmp(scenario, "probe-h11-hs-revert-fail") == 0) ||
	    (strcmp(scenario, "probe-h11-q-fail") == 0)) {
		/* Stage 11: width-value continuation. The stage-A pass runs at
		 * 400 kHz/1-bit, then the HS enter, the record-only 4-bit
		 * replay, the 8-bit attempt, one CMD19/CMD14 round trip, the
		 * content pass at 8-bit/HS, the width and HS reverts and the
		 * recovery read. One fault per failure case; the non-prefix
		 * failure orders splice their own suffix after the shared
		 * base. */
		stage = 11;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		if ((strcmp(scenario, "probe-h11-w4-replay") == 0) ||
		    (strcmp(scenario, "probe-h11-w4-mismatch") == 0) ||
		    (strcmp(scenario, "probe-h11-w8-div") == 0)) {
			set_h11_order(h11_suffix_w8_close,
			    (unsigned int)(sizeof(h11_suffix_w8_close) /
			    sizeof(h11_suffix_w8_close[0])));
		} else if (strcmp(scenario, "probe-h11-w8-err") == 0) {
			set_h11_order(h11_suffix_w8_err,
			    (unsigned int)(sizeof(h11_suffix_w8_err) /
			    sizeof(h11_suffix_w8_err[0])));
		} else if (strcmp(scenario,
		    "probe-h11-w4-anomaly") == 0) {
			set_h11_order(h11_suffix_w4_anomaly,
			    (unsigned int)(sizeof(h11_suffix_w4_anomaly) /
			    sizeof(h11_suffix_w4_anomaly[0])));
		} else if (strcmp(scenario,
		    "probe-h11-clock-busy") == 0) {
			set_h11_order(h11_suffix_clock_busy,
			    (unsigned int)(sizeof(h11_suffix_clock_busy) /
			    sizeof(h11_suffix_clock_busy[0])));
		} else if (strcmp(scenario,
		    "probe-h11-bustest-timeout") == 0) {
			set_h11_order(h11_suffix_bustest_timeout,
			    (unsigned int)(sizeof(h11_suffix_bustest_timeout) /
			    sizeof(h11_suffix_bustest_timeout[0])));
		} else if (strcmp(scenario,
		    "probe-h11-bustest-mismatch") == 0) {
			set_h11_order(h11_suffix_bustest_mismatch,
			    (unsigned int)(sizeof(h11_suffix_bustest_mismatch) /
			    sizeof(h11_suffix_bustest_mismatch[0])));
		} else if (strcmp(scenario,
		    "probe-h11-content-read") == 0) {
			set_h11_order(h11_suffix_content_read,
			    (unsigned int)(sizeof(h11_suffix_content_read) /
			    sizeof(h11_suffix_content_read[0])));
		} else if ((strcmp(scenario,
		    "probe-h11-content-div") == 0) ||
		    (strcmp(scenario,
		    "probe-h11-width-revert-fail") == 0) ||
		    (strcmp(scenario,
		    "probe-h11-hs-revert-fail") == 0)) {
			set_h11_order(h11_suffix_no_q,
			    (unsigned int)(sizeof(h11_suffix_no_q) /
			    sizeof(h11_suffix_no_q[0])));
		} else {
			set_expected_order(order_probe_h11,
			    (unsigned int)(sizeof(order_probe_h11) /
			    sizeof(order_probe_h11[0])));
		}
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if (strcmp(scenario, "probe-h11-w4-replay") == 0) {
			cmd6_ignore = 1;
		} else if (strcmp(scenario,
		    "probe-h11-w4-mismatch") == 0) {
			cmd6_ignore_width8 = 1;
		} else if (strcmp(scenario, "probe-h11-w8-div") == 0) {
			cmd6_ignore_width4 = 1;
			w8_paradox = 1;
		} else if (strcmp(scenario, "probe-h11-w8-err") == 0) {
			cmd6_ignore_width4 = 1;
			cmd6_fail_bus_width8 = 1;
		} else if (strcmp(scenario,
		    "probe-h11-w4-anomaly") == 0) {
			ext_csd_zero_width = GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		} else if (strcmp(scenario,
		    "probe-h11-clock-busy") == 0) {
			rcg_stuck_after = 1u;
		} else if (strcmp(scenario,
		    "probe-h11-bustest-timeout") == 0) {
			cmd6_ignore_width4 = 1;
			cmd19_8bit_timeout = 1;
		} else if (strcmp(scenario,
		    "probe-h11-bustest-mismatch") == 0) {
			cmd6_ignore_width4 = 1;
			bus_test_bad = 1;
			bus_test_bad_width = GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8;
		} else if (strcmp(scenario,
		    "probe-h11-content-read") == 0) {
			cmd6_ignore_width4 = 1;
			cmd17_fail_on = 26u;
			cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario,
		    "probe-h11-content-div") == 0) {
			cmd6_ignore_width4 = 1;
			hs_content_lba = SIM_GPT_FIRST_USABLE;
		} else if (strcmp(scenario,
		    "probe-h11-width-revert-fail") == 0) {
			cmd6_ignore_width4 = 1;
			cmd6_ignore_revert = 1;
		} else if (strcmp(scenario,
		    "probe-h11-hs-revert-fail") == 0) {
			cmd6_ignore_width4 = 1;
			hs_timing_ignore_exit = 1;
		} else if (strcmp(scenario, "probe-h11-q-fail") == 0) {
			cmd6_ignore_width4 = 1;
			cmd17_fail_on = 51u;
			cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario,
		    "probe-h11-honored-pass") == 0) {
			cmd6_ignore_width4 = 1;
		}
	} else if ((strcmp(scenario, "probe-h12-refused-pass") == 0) ||
	    (strcmp(scenario, "probe-h12-honored") == 0) ||
	    (strcmp(scenario, "probe-h12-hs-ignored") == 0) ||
	    (strcmp(scenario, "probe-h12-clock-busy") == 0) ||
	    (strcmp(scenario, "probe-h12-wddr-err") == 0) ||
	    (strcmp(scenario, "probe-h12-wddr-empty") == 0) ||
	    (strcmp(scenario, "probe-h12-wddr-paradox") == 0) ||
	    (strcmp(scenario, "probe-h12-width-revert-fail") == 0) ||
	    (strcmp(scenario, "probe-h12-hs-revert-fail") == 0) ||
	    (strcmp(scenario, "probe-h12-q-fail") == 0) ||
	    (strcmp(scenario, "probe-h12-pre-read-fail") == 0)) {
		/* Stage 12: record-only DDR-width attempt. The stage-A pass
		 * runs at 400 kHz/1-bit, then the HS enter, the byte-183
		 * value-4 attempt and its verification read, the width and
		 * HS reverts and the recovery read. One fault per failure
		 * case; the non-prefix failure orders splice their own
		 * suffix after the shared base. */
		stage = 12;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		if (strcmp(scenario, "probe-h12-pre-read-fail") == 0) {
			set_h12_order(h12_suffix_pre_read,
			    (unsigned int)(sizeof(h12_suffix_pre_read) /
			    sizeof(h12_suffix_pre_read[0])));
		} else if (strcmp(scenario, "probe-h12-hs-ignored") == 0) {
			set_h12_order(h12_suffix_hs_ignored,
			    (unsigned int)(sizeof(h12_suffix_hs_ignored) /
			    sizeof(h12_suffix_hs_ignored[0])));
		} else if (strcmp(scenario, "probe-h12-clock-busy") == 0) {
			set_h12_order(h12_suffix_clock_busy,
			    (unsigned int)(sizeof(h12_suffix_clock_busy) /
			    sizeof(h12_suffix_clock_busy[0])));
		} else if (strcmp(scenario, "probe-h12-wddr-err") == 0) {
			set_h12_order(h12_suffix_wddr_err,
			    (unsigned int)(sizeof(h12_suffix_wddr_err) /
			    sizeof(h12_suffix_wddr_err[0])));
		} else if ((strcmp(scenario,
		    "probe-h12-wddr-empty") == 0) ||
		    (strcmp(scenario,
		    "probe-h12-wddr-paradox") == 0) ||
		    (strcmp(scenario,
		    "probe-h12-width-revert-fail") == 0) ||
		    (strcmp(scenario,
		    "probe-h12-hs-revert-fail") == 0)) {
			set_h12_order(h12_suffix_no_q,
			    (unsigned int)(sizeof(h12_suffix_no_q) /
			    sizeof(h12_suffix_no_q[0])));
		} else {
			set_expected_order(order_probe_h12,
			    (unsigned int)(sizeof(order_probe_h12) /
			    sizeof(order_probe_h12[0])));
		}
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if (strcmp(scenario, "probe-h12-refused-pass") == 0) {
			cmd6_ignore = 1;
		} else if (strcmp(scenario,
		    "probe-h12-hs-ignored") == 0) {
			hs_timing_ignore_enter = 1;
		} else if (strcmp(scenario,
		    "probe-h12-clock-busy") == 0) {
			rcg_stuck_after = 1u;
		} else if (strcmp(scenario,
		    "probe-h12-wddr-err") == 0) {
			cmd6_fail_bus_width_ddr = 1;
		} else if (strcmp(scenario,
		    "probe-h12-wddr-empty") == 0) {
			ext_csd_zero_width = GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		} else if (strcmp(scenario,
		    "probe-h12-wddr-paradox") == 0) {
			wddr_paradox = 1;
		} else if (strcmp(scenario,
		    "probe-h12-width-revert-fail") == 0) {
			cmd6_ignore_revert = 1;
		} else if (strcmp(scenario,
		    "probe-h12-hs-revert-fail") == 0) {
			hs_timing_ignore_exit = 1;
		} else if (strcmp(scenario,
		    "probe-h12-q-fail") == 0) {
			cmd17_fail_on = 26u;
			cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario,
		    "probe-h12-pre-read-fail") == 0) {
			cmd8_fail_on = 2u;
			cmd8_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		}
	} else if ((strcmp(scenario, "probe-h13-pass") == 0) ||
	    (strcmp(scenario, "probe-h13-reset-stuck") == 0) ||
	    (strcmp(scenario, "probe-h13-inhibit") == 0) ||
	    (strcmp(scenario, "probe-h13-mismatch") == 0) ||
	    (strcmp(scenario, "probe-h13-read-timeout") == 0) ||
	    (strcmp(scenario, "probe-h13-read-crc") == 0) ||
	    (strcmp(scenario, "probe-h13-read-status") == 0) ||
	    (strcmp(scenario, "probe-h13-write-crc") == 0) ||
	    (strcmp(scenario, "probe-h13-normal") == 0) ||
	    (strcmp(scenario, "probe-h13-q-fail") == 0)) {
		/* Stage 13: rung-1 write-phase recovery. The stage-A pass
		 * runs at 400 kHz/1-bit, then one CMD19 write, one bounded
		 * data reset, one CMD14 read and the block-0 recovery read.
		 * A reset that does not clear stops before CMD14. */
		stage = 13;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if ((strcmp(scenario, "probe-h13-reset-stuck") == 0) ||
		    (strcmp(scenario, "probe-h13-inhibit") == 0)) {
			set_h13_order(h13_suffix_no_cmd14,
			    (unsigned int)(sizeof(h13_suffix_no_cmd14) /
			    sizeof(h13_suffix_no_cmd14[0])));
		} else {
			set_expected_order(order_probe_h13,
			    (unsigned int)(sizeof(order_probe_h13) /
			    sizeof(order_probe_h13[0])));
		}
		if ((strcmp(scenario, "probe-h13-normal") != 0) &&
		    (strcmp(scenario, "probe-h13-write-crc") != 0)) {
			cmd19_1bit_timeout = 1;
		}
		if (strcmp(scenario, "probe-h13-reset-stuck") == 0) {
			h13_reset_stuck = 1;
		} else if (strcmp(scenario, "probe-h13-inhibit") == 0) {
			h13_inhibit_survives = 1;
		} else if (strcmp(scenario, "probe-h13-mismatch") == 0) {
			bus_test_bad = 1;
			bus_test_bad_width = 0u;
		} else if (strcmp(scenario,
		    "probe-h13-read-timeout") == 0) {
			cmd14_error = GTA4L_EMMC_INT_ERR_DATA_TIMEOUT;
		} else if (strcmp(scenario, "probe-h13-read-crc") == 0) {
			cmd14_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario,
		    "probe-h13-read-status") == 0) {
			cmd14_status = 1;
		} else if (strcmp(scenario, "probe-h13-write-crc") == 0) {
			cmd19_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario, "probe-h13-q-fail") == 0) {
			cmd17_fail_on = 26u;
			cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		}
	} else if ((strcmp(scenario, "probe-h14-pass") == 0) ||
	    (strcmp(scenario, "probe-h14-reset-stuck") == 0) ||
	    (strcmp(scenario, "probe-h14-inhibit") == 0) ||
	    (strcmp(scenario, "probe-h14-cmd12-silent") == 0) ||
	    (strcmp(scenario, "probe-h14-cmd12-crc") == 0) ||
	    (strcmp(scenario, "probe-h14-cmd13-fail") == 0) ||
	    (strcmp(scenario, "probe-h14-mismatch") == 0) ||
	    (strcmp(scenario, "probe-h14-read-crc") == 0) ||
	    (strcmp(scenario, "probe-h14-q-fail") == 0) ||
	    (strcmp(scenario, "probe-h14-write-crc") == 0) ||
	    (strcmp(scenario, "probe-h14-normal") == 0)) {
		/* Stage 14: rung-2 write-phase recovery. The stage-A pass
		 * runs at 400 kHz/1-bit, then one CMD19 write, one bounded
		 * data reset, the forced CMD12/CMD13 drain, one CMD14 read
		 * and the block-0 recovery read. */
		stage = 14;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if ((strcmp(scenario, "probe-h14-reset-stuck") == 0) ||
		    (strcmp(scenario, "probe-h14-inhibit") == 0)) {
			set_h14_order(h14_suffix_no_drain,
			    (unsigned int)(sizeof(h14_suffix_no_drain) /
			    sizeof(h14_suffix_no_drain[0])));
		} else if ((strcmp(scenario,
		    "probe-h14-cmd12-silent") == 0) ||
		    (strcmp(scenario, "probe-h14-cmd12-crc") == 0)) {
			set_h14_order(h14_suffix_no_cmd13,
			    (unsigned int)(sizeof(h14_suffix_no_cmd13) /
			    sizeof(h14_suffix_no_cmd13[0])));
		} else {
			set_expected_order(order_probe_h14,
			    (unsigned int)(sizeof(order_probe_h14) /
			    sizeof(order_probe_h14[0])));
		}
		if ((strcmp(scenario, "probe-h14-normal") != 0) &&
		    (strcmp(scenario, "probe-h14-write-crc") != 0)) {
			cmd19_1bit_timeout = 1;
		}
		if (strcmp(scenario, "probe-h14-reset-stuck") == 0) {
			h13_reset_stuck = 1;
		} else if (strcmp(scenario, "probe-h14-inhibit") == 0) {
			h13_inhibit_survives = 1;
		} else if (strcmp(scenario,
		    "probe-h14-cmd12-silent") == 0) {
			h14_cmd12_no_cc = 1;
		} else if (strcmp(scenario, "probe-h14-cmd12-crc") == 0) {
			h14_cmd12_crc = 1;
		} else if (strcmp(scenario, "probe-h14-cmd13-fail") == 0) {
			h14_cmd13_crc = 1;
		} else if (strcmp(scenario, "probe-h14-mismatch") == 0) {
			bus_test_bad = 1;
			bus_test_bad_width = 0u;
		} else if (strcmp(scenario, "probe-h14-read-crc") == 0) {
			cmd14_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario, "probe-h14-q-fail") == 0) {
			cmd17_fail_on = 26u;
			cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario, "probe-h14-write-crc") == 0) {
			cmd19_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		}
	} else if ((strcmp(scenario, "probe-h15-pass") == 0) ||
	    (strcmp(scenario, "probe-h15-crc") == 0) ||
	    (strcmp(scenario, "probe-h15-content-div") == 0) ||
	    (strcmp(scenario, "probe-h15-bustest-wedge") == 0) ||
	    (strcmp(scenario,
	    "probe-h15-bustest-wedge-unproven") == 0)) {
		/* Stage 15: verified 4-bit SDR switch, corrected CMD19/CMD14
		 * test, content pass, verified revert and recovery read. The
		 * wedge scenarios reproduce the observed 4-bit write-phase
		 * timeout, which latches DAT_INHIBIT and blocks a pre-read
		 * revert; the second keeps the inhibit latched so the
		 * recovery cannot prove the framing. */
		stage = 15;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if (strcmp(scenario, "probe-h15-crc") == 0) {
			cmd14_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario,
		    "probe-h15-content-div") == 0) {
			width_corrupt_lba = SIM_GPT_FIRST_USABLE;
			width_corrupt_code =
			    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		} else if (strcmp(scenario,
		    "probe-h15-bustest-wedge") == 0) {
			cmd19_4bit_timeout = 1;
		} else if (strcmp(scenario,
		    "probe-h15-bustest-wedge-unproven") == 0) {
			cmd19_4bit_timeout = 1;
			h13_inhibit_survives = 1;
		}
	} else if ((strcmp(scenario, "probe-h16-pass") == 0) ||
	    (strcmp(scenario, "probe-h16-content-div") == 0)) {
		/* Stage 16: the content-only isolation rung. No CMD19 or
		 * CMD14 is issued at all. */
		stage = 16;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if (strcmp(scenario, "probe-h16-content-div") == 0) {
			width_corrupt_lba = SIM_GPT_FIRST_USABLE;
			width_corrupt_code =
			    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		}
	} else if ((strcmp(scenario, "probe-h17-pass") == 0) ||
	    (strcmp(scenario, "probe-h17-content-div") == 0) ||
	    (strcmp(scenario, "probe-h17-pattern-mismatch") == 0) ||
	    (strcmp(scenario, "probe-h17-write-crc") == 0) ||
	    (strcmp(scenario, "probe-h17-inhibit") == 0)) {
		/* Stage 17: the content pass runs first, then the
		 * Linux-compatible CMD19/CMD14 test. The expected write-phase
		 * data timeout is tolerated, the data circuit is reset and the
		 * CMD14 read-back decides. */
		stage = 17;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if (strcmp(scenario, "probe-h17-content-div") == 0) {
			width_corrupt_lba = SIM_GPT_FIRST_USABLE;
			width_corrupt_code =
			    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		} else if (strcmp(scenario,
		    "probe-h17-pattern-mismatch") == 0) {
			cmd19_4bit_timeout = 1;
			bus_test_bad = 1;
			bus_test_bad_width =
			    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
		} else if (strcmp(scenario,
		    "probe-h17-write-crc") == 0) {
			cmd19_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		} else if (strcmp(scenario, "probe-h17-inhibit") == 0) {
			cmd19_4bit_timeout = 1;
			h13_inhibit_survives = 1;
		} else {
			cmd19_4bit_timeout = 1;
		}
	} else if ((strcmp(scenario, "probe-dto-fired") == 0) ||
	    (strcmp(scenario, "probe-dto-silent") == 0) ||
	    (strcmp(scenario, "probe-dto-q-fail") == 0) ||
	    (strcmp(scenario, "probe-dto-e2-pass") == 0) ||
	    (strcmp(scenario, "probe-dto-arm-ro") == 0)) {
		/* Stage 7: data-timeout audit. E1 reads 8 blocks with the
		 * minimum timeout armed; probe-dto-fired and probe-dto-q-fail
		 * inject a data-timeout error mid-transfer, probe-dto-silent
		 * lets the read complete. probe-dto-q-fail also fails the
		 * block-0 recovery read. E2 repeats the 1-bit CMD19 wall.
		 * probe-dto-arm-ro drops the RO INT_STABLE bit on the arm
		 * readback (0x0005) to prove the TOUTVAL-only gate. */
		stage = 7;
		ext_csd_count = 61071360u;
		sim_init_ext_csd();
		cmd7_tc_immediate = 1;
		set_expected_order(order_probe_dto,
		    (unsigned int)(sizeof(order_probe_dto) /
		    sizeof(order_probe_dto[0])));
		set_expected_lbas(lbas_full,
		    (unsigned int)(sizeof(lbas_full) /
		    sizeof(lbas_full[0])));
		expected_lba_wrap = 1;
		if ((strcmp(scenario, "probe-dto-fired") == 0) ||
		    (strcmp(scenario, "probe-dto-q-fail") == 0)) {
			mb_fail_done = 3;
			mb_fail_error = GTA4L_EMMC_INT_ERR_DATA_TIMEOUT;
		}
		if (strcmp(scenario, "probe-dto-q-fail") == 0) {
			cmd17_fail_on = 26u;
			cmd17_fail_error = GTA4L_EMMC_INT_ERR_DATA_CRC;
		}
		if (strcmp(scenario, "probe-dto-arm-ro") == 0) {
			timeout_arm_stuck = 1;
		}
		if (strcmp(scenario, "probe-dto-e2-pass") != 0) {
			cmd19_1bit_timeout = 1;
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
	} else if (strcmp(scenario, "report-width-b2") == 0) {
		/* Synthetic B2 field-wise bound: the header and pad lines read
		 * the live SDHCI/TLMM samples. */
		width_form = 6;
		sim_store(sdhci, GTA4L_EMMC_REG_HOST_CONTROL, 1u, 0xFFu);
		sim_store(tlmm, GTA4L_EMMC_TLMM_SDC1_QDSD, 4u, 0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_CONFIG, 4u,
		    0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_STATUS, 4u,
		    0xFFFFFFFFu);
	} else if (strcmp(scenario, "report-width-h9") == 0) {
		/* Synthetic H9 field-wise bound: the fixture fills both
		 * attempt slots and the report formatter is called directly. */
		width_form = 7;
	} else if (strcmp(scenario, "report-width-h10") == 0) {
		/* Synthetic H10 field-wise bound: the fixture fills every
		 * switch/test/content/revert field and the report formatter
		 * samples the live header and pad registers. */
		width_form = 8;
		sim_store(sdhci, GTA4L_EMMC_REG_HOST_CONTROL, 1u, 0xFFu);
		sim_store(tlmm, GTA4L_EMMC_TLMM_SDC1_QDSD, 4u, 0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_CONFIG, 4u,
		    0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_STATUS, 4u,
		    0xFFFFFFFFu);
	} else if (strcmp(scenario, "report-width-h11") == 0) {
		/* Synthetic H11 field-wise bound: the fixture fills both
		 * width attempts and every switch/test/content/revert field,
		 * and the report formatter samples the live header and pad
		 * registers. */
		width_form = 9;
		sim_store(sdhci, GTA4L_EMMC_REG_HOST_CONTROL, 1u, 0xFFu);
		sim_store(tlmm, GTA4L_EMMC_TLMM_SDC1_QDSD, 4u, 0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_CONFIG, 4u,
		    0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_STATUS, 4u,
		    0xFFFFFFFFu);
	} else if (strcmp(scenario, "report-width-h12") == 0) {
		/* Synthetic H12 field-wise bound: the fixture fills the
		 * record-only DDR attempt and every switch/revert field, and
		 * the report formatter samples the live header and pad
		 * registers. */
		width_form = 10;
		sim_store(sdhci, GTA4L_EMMC_REG_HOST_CONTROL, 1u, 0xFFu);
		sim_store(tlmm, GTA4L_EMMC_TLMM_SDC1_QDSD, 4u, 0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_CONFIG, 4u,
		    0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_STATUS, 4u,
		    0xFFFFFFFFu);
	} else if (strcmp(scenario, "report-width-h13") == 0) {
		/* Synthetic H13 field-wise bound: the fixture fills the
		 * rung-1 write/reset/read fields, and the report formatter
		 * samples the live header and pad registers. */
		width_form = 11;
		sim_store(sdhci, GTA4L_EMMC_REG_HOST_CONTROL, 1u, 0xFFu);
		sim_store(tlmm, GTA4L_EMMC_TLMM_SDC1_QDSD, 4u, 0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_CONFIG, 4u,
		    0xFFFFFFFFu);
		sim_store(sdhci, GTA4L_EMMC_REG_CORE_DLL_STATUS, 4u,
		    0xFFFFFFFFu);
	} else if (strcmp(scenario, "report-width-h14") == 0) {
		/* Synthetic H14 field-wise bound: the fixture fills the
		 * rung-2 write/reset/drain/read fields, and the report
		 * formatter samples the live header and pad registers. */
		width_form = 12;
		sim_store(sdhci, GTA4L_EMMC_REG_HOST_CONTROL, 1u, 0xFFu);
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
	} else if ((strcmp(scenario, "probe-h9-clean-no-fire") == 0) ||
	    (strcmp(scenario, "probe-h9-fire-pass") == 0) ||
	    (strcmp(scenario, "probe-h9-fire-fail") == 0) ||
	    (strcmp(scenario, "probe-h9-fire-second-ident") == 0) ||
	    (strcmp(scenario, "probe-h9-mismatch") == 0)) {
		check(panicked == 1, "the H9 rung did not report");
		check(bad_command_format == 0,
		    "an H9 command used the wrong flags");
		check(bad_command_order == 0,
		    "the H9 run sent commands in the wrong order");
		if (strcmp(scenario, "probe-h9-clean-no-fire") == 0) {
			check(strstr(panic_message,
			    "GTA4L-EMMC H9 REPORT: rc=0 stop=0") != NULL,
			    "the clean H9 run does not name stop 0");
			check(strstr(panic_message,
			    "retry=0/0 att=1") != NULL,
			    "the clean H9 run invented a retry");
			check(strstr(panic_message,
			    "A1 n=1 rc=0 r1=0 "
			    "fail=0/0/0/00000000/0000/0000/00000000/0")
			    != NULL,
			    "the clean H9 attempt-1 record is wrong");
			check(strstr(panic_message,
			    "A1P c3p=1/0000/0000/00000000/0033/07ff")
			    != NULL,
			    "the clean H9 run lost the pre-CMD3 state");
			check(strstr(panic_message,
			    "A1L c3l=1/3/0001/0001/1/"
			    "0000000000000000/1000000") != NULL,
			    "the clean H9 run lost the CMD3 poll history");
			check(strstr(panic_message, "A2 n=0 rc=0") != NULL,
			    "the clean H9 run invented a second attempt");
			check(strstr(panic_message,
			    "C cid=15010044,5836384d,42009005,f4661900 "
			    "cmd1=1/0") != NULL,
			    "the clean H9 report lost the CID");
			check(strstr(panic_message,
			    "H9 FAIL fail=0/0/0/00000000/0000/0000/"
			    "00000000/0") != NULL,
			    "the clean H9 report invented a failure");
			check(cmd_count == 7u,
			    "the clean H9 run did not stop after TRAN");
			check(last_index == GTA4L_EMMC_CMD13,
			    "the clean H9 run did not stop at CMD13");
		}
		if (strcmp(scenario, "probe-h9-fire-pass") == 0) {
			check(strstr(panic_message,
			    "GTA4L-EMMC H9 REPORT: rc=0 stop=10") != NULL,
			    "the H9 retry pass does not name stop 10");
			check(strstr(panic_message,
			    "retry=1/1 att=2") != NULL,
			    "the H9 retry pass lost the retry state");
			check(strstr(panic_message,
			    "A1 n=1 rc=-7 r1=0 "
			    "fail=1/3/65536/0000031a/0000/0000/00000000/9")
			    != NULL,
			    "the H9 attempt-1 timeout record is wrong");
			check(strstr(panic_message,
			    "A1L c3l=1/3/0000/0000/100000/"
			    "00000000000f4240/1000000") != NULL,
			    "the H9 attempt-1 poll history is wrong");
			check(strstr(panic_message,
			    "A2 n=2 rc=0 r1=0 "
			    "fail=0/0/0/00000000/0000/0000/00000000/0")
			    != NULL,
			    "the H9 duplicate CMD3 record is wrong");
			check(strstr(panic_message,
			    "A2P c3p=1/0000/0000/00000000/0033/07ff")
			    != NULL,
			    "the duplicate CMD3 lost its pre-command state");
			check(strstr(panic_message,
			    "A2L c3l=1/3/0001/0001/1/"
			    "0000000000000000/1000000") != NULL,
			    "the duplicate CMD3 poll history is wrong");
			check(strstr(panic_message,
			    "C cid=15010044,5836384d,42009005,f4661900")
			    != NULL,
			    "the H9 retry pass lost the CID");
			check(cmd_count == 8u,
			    "the H9 retry pass did not issue exactly one "
			    "duplicate");
			check(last_index == GTA4L_EMMC_CMD13,
			    "the H9 retry pass did not stop after TRAN");
		}
		if (strcmp(scenario, "probe-h9-fire-fail") == 0) {
			check(strstr(panic_message,
			    "GTA4L-EMMC H9 REPORT: rc=-7 stop=11") != NULL,
			    "the H9 double timeout does not name stop 11");
			check(strstr(panic_message,
			    "retry=1/0 att=2") != NULL,
			    "the H9 double timeout lost the retry state");
			check((strstr(panic_message, "A1 n=1 rc=-7") != NULL) &&
			    (strstr(panic_message, "A2 n=2 rc=-7") != NULL),
			    "the H9 double timeout lost an attempt record");
			check(strstr(panic_message,
			    "A2 n=2 rc=-7 r1=0 fail=1/3/65536/0000031a/")
			    != NULL,
			    "the duplicate CMD3 timeout record is wrong");
			check(cmd_count == 5u,
			    "the H9 double timeout issued more than two CMD3 "
			    "attempts or a later command");
			check(last_index == GTA4L_EMMC_CMD3,
			    "the H9 double timeout stopped after an unexpected "
			    "command");
		}
		if (strcmp(scenario, "probe-h9-fire-second-ident") == 0) {
			check(strstr(panic_message,
			    "GTA4L-EMMC H9 REPORT: rc=-7 stop=12") != NULL,
			    "the later H9 failure does not name stop 12");
			check(strstr(panic_message,
			    "retry=1/1 att=2") != NULL,
			    "the later H9 failure lost the retry state");
			check(strstr(panic_message,
			    "A2 n=2 rc=0 r1=0") != NULL,
			    "the later H9 failure lost the clean duplicate");
			check(strstr(panic_message,
			    "H9 FAIL fail=1/9/65536/00000909/") != NULL,
			    "the later H9 failure did not name the CMD9 "
			    "timeout");
			check(cmd_count == 6u,
			    "the later H9 failure did not stop at CMD9");
			check(last_index == GTA4L_EMMC_CMD9,
			    "the later H9 failure stopped after an unexpected "
			    "command");
		}
		if (strcmp(scenario, "probe-h9-mismatch") == 0) {
			check(strstr(panic_message,
			    "GTA4L-EMMC H9 REPORT: rc=-15 stop=13") != NULL,
			    "the H9 R1 mismatch does not name stop 13");
			check(strstr(panic_message,
			    "retry=1/0 att=2") != NULL,
			    "the H9 R1 mismatch lost the retry state");
			check(strstr(panic_message,
			    "A2 n=2 rc=-15 r1=1") != NULL,
			    "the H9 R1 mismatch lost the status record");
			check(strstr(panic_message,
			    "H9 FAIL fail=0/0/0/00000000/0000/0000/"
			    "00000000/0") != NULL,
			    "the H9 R1 mismatch invented a transport failure");
			check(cmd_count == 5u,
			    "the H9 R1 mismatch issued more than two CMD3 "
			    "attempts");
			check(last_index == GTA4L_EMMC_CMD3,
			    "the H9 R1 mismatch stopped after an unexpected "
			    "command");
		}
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
		    "GTA4L-EMMC B1 REPORT: rc=-19 stop=1") != NULL,
		    "the host-width-stuck report does not name the lost "
		    "HOST_CONTROL width");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=0 tw=00000000 "
		    "te=00000000 trc=0 hc=02 hb=00 ha=00 c6=00000900 "
		    "es=00000000") != NULL,
		    "the host-width-stuck report does not show the lost "
		    "HOST_CONTROL width");
		check(cmd_count == 35u,
		    "the host-width-stuck run issued a read on a width the "
		    "host did not apply");
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
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=0 tw=00000000 "
		    "te=00000000 trc=0 hc=02 hb=00 ha=00 c6=00000900 "
		    "es=00000000 b0=00000000 b1=00000000 same=0 div=0 "
		    "bc=0 e183a=00000000 esa=00000000") != NULL,
		    "the lost-host-width row is not the non-entering stage-B1 "
		    "report plus zero B1C fields");
		check(cmd_count == 35u,
		    "the lost-host-width row issued a read on a width the "
		    "host did not apply");
	} else if (strcmp(scenario, "probe-b1-1bit-pass") == 0) {
		char expected[512];

		check(panicked == 1, "the 1-bit pass run did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=0 stop=0") != NULL,
		    "the 1-bit pass report does not carry the result");
		snprintf(expected, sizeof(expected),
		    "BW=1 att=1 req=1 sw=0 swrc=0 tt=1 "
		    "tw=00000040 te=00000040 trc=0 hc=00 es=%08x",
		    sim_ext_csd_sum() - 2u);
		check(strstr(panic_message, expected) != NULL,
		    "the 1-bit discriminator did not record its passing words");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=1 swrc=0 tt=1 "
		    "tw=000000a5 te=000000a5 trc=0 hc=02") != NULL,
		    "the 1-bit pass report lost the 4-bit escalation");
		check(strstr(panic_message,
		    "BW=8 att=1 req=8 sw=2 swrc=0 tt=1") != NULL,
		    "the 1-bit pass report lost the 8-bit escalation");
		check(strstr(panic_message,
		    "same=1 div=0 bc=0 e183a=00000000 esa=00000000") != NULL,
		    "the 1-bit pass report invented a continuation");
		check(cmd6_count == 2u,
		    "the 1-bit pass run did not issue one CMD6 per width");
		check(cmd_count == 95u,
		    "the 1-bit pass run did not run the discriminator and both "
		    "content passes");
	} else if (strcmp(scenario, "probe-b1-1bit-continue") == 0) {
		char expected[512];

		check(panicked == 1, "the 1-bit continuation did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-19 stop=1") != NULL,
		    "the 1-bit continuation does not keep the verification "
		    "stop");
		snprintf(expected, sizeof(expected),
		    "BW=1 att=1 req=1 sw=0 swrc=0 tt=1 "
		    "tw=00000040 te=00000040 trc=0 hc=00 es=%08x",
		    sim_ext_csd_sum());
		check(strstr(panic_message, expected) != NULL,
		    "the 1-bit continuation lost the discriminator pass");
		check(strstr(panic_message,
		    "BW=4 att=1 req=4 sw=0 swrc=-19 tt=1 "
		    "tw=000000a5 te=000000a5 trc=0 hc=00 hb=00 ha=02 "
		    "c6=00000900") != NULL,
		    "the 1-bit continuation lost the failed-verification "
		    "tuple or the 4-bit bus test");
		snprintf(expected, sizeof(expected),
		    "same=1 div=0 bc=1 e183a=00000000 esa=%08x",
		    sim_ext_csd_sum());
		check(strstr(panic_message, expected) != NULL,
		    "the 1-bit continuation lost the marker or invented a "
		    "byte-183 value");
		check(strstr(panic_message,
		    "BW=8 att=0 req=0 sw=0 swrc=0 tt=0") != NULL,
		    "the 1-bit continuation attempted 8-bit");
		check(cmd6_count == 1u,
		    "the 1-bit continuation issued more than one CMD6");
		check(cmd_count == 66u,
		    "the 1-bit continuation did not run the discriminator and "
		    "exactly one continuation");
	} else if (strcmp(scenario, "probe-b1-1bit-timeout") == 0) {
		check(panicked == 1, "the 1-bit timeout did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-11 stop=9") != NULL,
		    "the 1-bit timeout does not name the data timeout and the "
		    "discriminator stop");
		check(strstr(panic_message,
		    "BW=1 att=1 req=1 sw=0 swrc=0 tt=1 "
		    "tw=00000000 te=00000000 trc=-11 hc=00") != NULL,
		    "the 1-bit timeout did not record the write-phase result");
		check(strstr(panic_message,
		    "BW=4 att=0 req=0 sw=0 swrc=0 tt=0") != NULL,
		    "the 1-bit timeout continued to the width switch");
		check(strstr(panic_message,
		    "FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/18") != NULL,
		    "the 1-bit timeout lost the terminal write-phase record");
		check(cmd6_count == 0u,
		    "the 1-bit timeout issued a CMD6");
		check(cmd_count == 34u,
		    "the 1-bit timeout did not stop at CMD19");
	} else if (strcmp(scenario, "probe-b1-1bit-mismatch") == 0) {
		check(panicked == 1, "the 1-bit mismatch did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B1 REPORT: rc=-20 stop=9") != NULL,
		    "the 1-bit mismatch does not name the pattern error and the "
		    "discriminator stop");
		check(strstr(panic_message,
		    "BW=1 att=1 req=1 sw=0 swrc=0 tt=1 "
		    "tw=00000080 te=00000040 trc=-20 hc=00") != NULL,
		    "the 1-bit mismatch did not bind the failing pattern word");
		check(strstr(panic_message,
		    "BW=4 att=0 req=0 sw=0 swrc=0 tt=0") != NULL,
		    "the 1-bit mismatch continued to the width switch");
		check(cmd6_count == 0u,
		    "the 1-bit mismatch issued a CMD6");
		check(cmd_count == 35u,
		    "the 1-bit mismatch did not stop at CMD14");
	} else if (strcmp(scenario, "probe-b2-pass") == 0) {
		check(panicked == 1, "the B2 pass did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 REPORT: rc=0 stop=0") != NULL,
		    "the B2 pass does not name its stop");
		check(strstr(panic_message,
		    "B2 R clk=0007 hs=00") != NULL,
		    "the B2 R line lost the 400 kHz state");
		check(strstr(panic_message,
		    "B2 S rc=0 sw=0 c6=00000900 hs=01 clk=0007 "
		    "rcg=0000020b") != NULL,
		    "the B2 S line lost the HS switch or the 50 MHz RCG");
		check(strstr(panic_message,
		    "B2 T rc=0 att=1 same=1 div=0") != NULL,
		    "the B2 content pass did not match");
		check(strstr(panic_message,
		    "B2 V rc=0 sw=0 hs=00 clk=0007 rcg=00002017 q=0")
		    != NULL,
		    "the B2 revert did not restore legacy timing");
		check(strstr(panic_message,
		    "B2 PAD pre=00009fe4/60000000/00000000 "
		    "post=00009fe4/60000000/00000000") != NULL,
		    "the B2 pad samples changed");
		check(strstr(panic_message,
		    "B2 FAIL fail=0/0/0/00000000/0000/0000/00000000/0")
		    != NULL,
		    "the B2 pass invented a failure record");
		check(cmd6_count == 2u,
		    "the B2 pass did not issue the enter and leave CMD6");
		check(cmd17_count == 51u,
		    "the B2 pass did not run both content passes and Q");
	} else if (strcmp(scenario, "probe-b2-ignored") == 0) {
		check(panicked == 1, "the ignored HS switch did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 REPORT: rc=-22 stop=10") != NULL,
		    "the ignored byte-185 switch does not keep stop 10");
		check(strstr(panic_message,
		    "B2 S rc=-22 sw=-22 c6=00000900 hs=00") != NULL,
		    "the ignored switch did not record the readback mismatch");
		check(strstr(panic_message,
		    "B2 V rc=0 sw=0 hs=00 clk=0007 rcg=00002017") != NULL,
		    "the ignored switch did not revert cleanly");
		check(cmd17_count == 25u,
		    "the ignored switch still ran the HS content pass");
		check(sim_load(gcc, GTA4L_EMMC_GCC_SDCC1_APPS_CFG, 4u) ==
		    GTA4L_EMMC_RCG_400K_CFG,
		    "the ignored switch left the 50 MHz RCG");
	} else if (strcmp(scenario, "probe-b2-clock-busy") == 0) {
		check(panicked == 1, "the busy RCG did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 REPORT: rc=-5 stop=11") != NULL,
		    "the busy RCG does not keep stop 11");
		check(strstr(panic_message,
		    "B2 S rc=-5 sw=0 c6=00000900 hs=01 clk=0000") != NULL,
		    "the busy RCG lost the completed switch or the failed "
		    "clock");
		check(strstr(panic_message,
		    "B2 V rc=-5 sw=0 hs=00") != NULL,
		    "the busy RCG revert failure was not recorded");
		check(cmd17_count == 25u,
		    "the busy RCG still ran the HS content pass");
		check((sim_load(sdhci, GTA4L_EMMC_REG_CLOCK_CONTROL, 2u) &
		    GTA4L_EMMC_CLOCK_CARD_EN) == 0u,
		    "the busy RCG left the card clock enabled");
	} else if (strcmp(scenario, "probe-b2-content-div") == 0) {
		check(panicked == 1, "the HS content divergence did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 REPORT: rc=-21 stop=12") != NULL,
		    "the HS divergence does not keep stop 12");
		check(strstr(panic_message,
		    "B2 T rc=0 att=1 same=0 div=34") != NULL,
		    "the HS divergence did not name the first differing LBA");
		check(strstr(panic_message,
		    "B2 V rc=0 sw=0 hs=00 clk=0007 rcg=00002017") != NULL,
		    "the HS divergence run did not revert");
	} else if (strcmp(scenario, "probe-b2-content-read") == 0) {
		check(panicked == 1, "the HS content read error did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 REPORT: rc=-12 stop=12") != NULL,
		    "the HS content read error does not keep stop 12");
		check(strstr(panic_message,
		    "B2 T rc=-12 att=1") != NULL,
		    "the HS content read error lost its core code");
		check(strstr(panic_message,
		    "B2 V rc=0 sw=0 hs=00 clk=0007 rcg=00002017") != NULL,
		    "the HS content read error did not revert");
		check(cmd17_count == 26u,
		    "the HS content read error did not stop at the first "
		    "HS read");
	} else if (strcmp(scenario, "probe-b2-revert-fail") == 0) {
		check(panicked == 1, "the failed revert did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 REPORT: rc=-22 stop=13") != NULL,
		    "the failed revert does not keep stop 13");
		check(strstr(panic_message,
		    "B2 T rc=0 att=1 same=1 div=0") != NULL,
		    "the failed revert lost the passing HS content pass");
		check(strstr(panic_message,
		    "B2 V rc=-22 sw=-22 hs=01") != NULL,
		    "the failed revert did not record the byte-185 mismatch");
		check(sim_load(gcc, GTA4L_EMMC_GCC_SDCC1_APPS_CFG, 4u) ==
		    GTA4L_EMMC_RCG_400K_CFG,
		    "the failed revert left the 50 MHz RCG");
	} else if (strcmp(scenario, "probe-b2-q-fail") == 0) {
		check(panicked == 1, "the failed B2 recovery read did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 REPORT: rc=-12 stop=14") != NULL,
		    "the failed B2 recovery read does not keep stop 14");
		check(strstr(panic_message,
		    "B2 V rc=0 sw=0 hs=00 clk=0007 rcg=00002017 q=-12")
		    != NULL,
		    "the failed B2 recovery read lost its core code");
		check(cmd17_count == 51u,
		    "the failed B2 recovery read did not stop at Q");
	} else if (strcmp(scenario, "probe-b2-pre-read-fail") == 0) {
		check(panicked == 1, "the failed B2 pre-read did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 REPORT: rc=-12 stop=10") != NULL,
		    "the failed B2 pre-read does not keep stop 10");
		check(strstr(panic_message,
		    "B2 S rc=-12 sw=0 c6=00000000 hs=00") != NULL,
		    "the failed B2 pre-read still issued a CMD6");
		check(cmd6_count == 0u,
		    "the failed B2 pre-read issued a CMD6");
		check(cmd17_count == 25u,
		    "the failed B2 pre-read ran the HS content pass");
	} else if (strcmp(scenario, "probe-h10-pass") == 0) {
		check(panicked == 1, "the H10 pass did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=0 stop=0") != NULL,
		    "the H10 pass does not name its stop");
		check(strstr(panic_message,
		    "H10 R clk=0007 hs=00 bw=00 hc=00 es=0000018e") != NULL,
		    "the H10 R line lost the 400 kHz identity");
		check(strstr(panic_message,
		    "H10 S rc=0 sw=0 c6=00000900 hs=01 clk=0007 "
		    "rcg=0000020b sum=0000018f") != NULL,
		    "the H10 S line lost the HS switch or the 50 MHz RCG");
		check(strstr(panic_message,
		    "H10 W rc=0 sw=1 swrc=0 c6=00000900 hb=00 ha=02 "
		    "es=00000190") != NULL,
		    "the H10 W line lost the 4-bit switch at HS");
		check(strstr(panic_message,
		    "H10 B rc=0 tt=1 trc=0 tw=000000a5 te=000000a5")
		    != NULL,
		    "the H10 bus test did not pass at 4-bit/HS");
		check(strstr(panic_message,
		    "H10 T rc=0 att=1 same=1 div=0") != NULL,
		    "the H10 content pass did not match");
		check(strstr(panic_message,
		    "H10 V rc=0 sw=0 swrc=0 c6=00000900 hb=02 ha=00 "
		    "es=0000018f") != NULL,
		    "the H10 width revert did not restore 1-bit");
		check(strstr(panic_message,
		    "H10 X rc=0 sw=0 c6=00000900 hs=00 clk=0007 "
		    "rcg=00002017 sum=0000018e") != NULL,
		    "the H10 HS revert did not restore legacy timing");
		check(strstr(panic_message,
		    "H10 Q rc=0 qb0=5c510100") != NULL,
		    "the H10 recovery read did not match block 0");
		check(strstr(panic_message,
		    "H10 PAD pre=00009fe4/60000000/00000000 "
		    "post=00009fe4/60000000/00000000") != NULL,
		    "the H10 pad samples changed");
		check(strstr(panic_message,
		    "H10 FAIL fail=0/0/0/00000000/0000/0000/00000000/0")
		    != NULL,
		    "the H10 pass invented a failure record");
		check(cmd6_count == 4u,
		    "the H10 pass did not issue both switches and reverts");
		check(cmd17_count == 51u,
		    "the H10 pass did not run both content passes and Q");
	} else if (strcmp(scenario, "probe-h10-hs-ignored") == 0) {
		check(panicked == 1, "the ignored H10 HS switch did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=-22 stop=10") != NULL,
		    "the ignored H10 HS switch does not keep stop 10");
		check(strstr(panic_message,
		    "H10 S rc=-22 sw=-22 c6=00000900 hs=00") != NULL,
		    "the ignored H10 HS switch did not record the readback "
		    "mismatch");
		check(strstr(panic_message,
		    "H10 W rc=0 sw=0 swrc=0 c6=00000000") != NULL,
		    "the ignored H10 HS switch continued to the width step");
		check(strstr(panic_message,
		    "H10 T rc=0 att=0") != NULL,
		    "the ignored H10 HS switch ran the content pass");
		check(strstr(panic_message,
		    "H10 X rc=0 sw=0 c6=00000900 hs=00 clk=0007 "
		    "rcg=00002017") != NULL,
		    "the ignored H10 HS switch did not revert cleanly");
		check(cmd17_count == 25u,
		    "the ignored H10 HS switch still ran the content pass");
		check(cmd6_count == 2u,
		    "the ignored H10 HS switch did not issue both byte-185 "
		    "commands");
	} else if (strcmp(scenario, "probe-h10-clock-busy") == 0) {
		check(panicked == 1, "the busy H10 RCG did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=-5 stop=11") != NULL,
		    "the busy H10 RCG does not keep stop 11");
		check(strstr(panic_message,
		    "H10 S rc=-5 sw=0 c6=00000900 hs=01 clk=0000") != NULL,
		    "the busy H10 RCG lost the completed switch or the "
		    "failed clock");
		check(strstr(panic_message,
		    "H10 X rc=-5 sw=0 c6=00000000 hs=00") != NULL,
		    "the busy H10 RCG revert failure was not recorded");
		check(cmd17_count == 25u,
		    "the busy H10 RCG still ran the content pass");
		check(cmd6_count == 1u,
		    "the busy H10 RCG issued a width command");
		check((sim_load(sdhci, GTA4L_EMMC_REG_CLOCK_CONTROL, 2u) &
		    GTA4L_EMMC_CLOCK_CARD_EN) == 0u,
		    "the busy H10 RCG left the card clock enabled");
	} else if (strcmp(scenario, "probe-h10-width-ignored") == 0) {
		check(panicked == 1, "the ignored H10 width switch did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=-19 stop=12") != NULL,
		    "the ignored H10 width switch does not keep stop 12");
		check(strstr(panic_message,
		    "H10 W rc=-19 sw=0 swrc=-19 c6=00000900 hb=00 ha=02 "
		    "es=0000018f") != NULL,
		    "the ignored H10 width switch did not record the legacy "
		    "tuple");
		check(strstr(panic_message,
		    "H10 V rc=0 sw=0 swrc=0 c6=00000900 hb=00 ha=00 "
		    "es=0000018f") != NULL,
		    "the ignored H10 width switch did not unwind to 1-bit");
		check(strstr(panic_message,
		    "H10 X rc=0 sw=0 c6=00000900 hs=00 clk=0007 "
		    "rcg=00002017") != NULL,
		    "the ignored H10 width switch did not revert HS");
		check(cmd17_count == 25u,
		    "the ignored H10 width switch ran the content pass");
		check(cmd6_count == 4u,
		    "the ignored H10 width switch did not issue both "
		    "switches and reverts");
	} else if (strcmp(scenario, "probe-h10-bustest-timeout") == 0) {
		check(panicked == 1, "the H10 bus-test timeout did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=-11 stop=13") != NULL,
		    "the H10 bus-test timeout does not keep stop 13");
		check(strstr(panic_message,
		    "H10 B rc=-11 tt=1 trc=-11 tw=00000000 te=00000000")
		    != NULL,
		    "the H10 bus-test timeout lost the write-phase class");
		check(strstr(panic_message,
		    "H10 V rc=0 sw=0 swrc=0") != NULL,
		    "the H10 bus-test timeout did not restore 1-bit");
		check(strstr(panic_message,
		    "H10 X rc=0 sw=0 c6=00000000 hs=00 clk=0000 "
		    "rcg=00000000 sum=00000000") != NULL,
		    "the H10 bus-test failure walked back into the "
		    "inhibited controller");
		check(cmd17_count == 25u,
		    "the H10 bus-test timeout ran the content pass");
	} else if (strcmp(scenario, "probe-h10-bustest-mismatch") == 0) {
		check(panicked == 1, "the H10 bus-test mismatch did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=-20 stop=13") != NULL,
		    "the H10 bus-test mismatch does not keep stop 13");
		check(strstr(panic_message,
		    "H10 B rc=-20 tt=1 trc=-20 tw=0000005a te=000000a5")
		    != NULL,
		    "the H10 bus-test mismatch did not bind the pattern "
		    "words");
		check(cmd17_count == 25u,
		    "the H10 bus-test mismatch ran the content pass");
	} else if (strcmp(scenario, "probe-h10-content-div") == 0) {
		check(panicked == 1, "the H10 content divergence did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=-21 stop=14") != NULL,
		    "the H10 divergence does not keep stop 14");
		check(strstr(panic_message,
		    "H10 T rc=0 att=1 same=0 div=34") != NULL,
		    "the H10 divergence did not name the first differing "
		    "LBA");
		check(strstr(panic_message,
		    "H10 V rc=0 sw=0 swrc=0 c6=00000900") != NULL,
		    "the H10 divergence run did not revert the width");
		check(strstr(panic_message,
		    "H10 X rc=0 sw=0 c6=00000900 hs=00") != NULL,
		    "the H10 divergence run did not revert HS");
		check(cmd17_count == 50u,
		    "the H10 divergence did not stop at Q");
	} else if (strcmp(scenario, "probe-h10-content-read") == 0) {
		check(panicked == 1, "the H10 content read error did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=-12 stop=14") != NULL,
		    "the H10 content read error does not keep stop 14");
		check(strstr(panic_message,
		    "H10 T rc=-12 att=1") != NULL,
		    "the H10 content read error lost its core code");
		check(cmd17_count == 26u,
		    "the H10 content read error did not stop at the first "
		    "4-bit read");
	} else if (strcmp(scenario, "probe-h10-width-revert-fail") == 0) {
		check(panicked == 1, "the H10 width-revert failure did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=-19 stop=15") != NULL,
		    "the H10 width-revert failure does not keep stop 15");
		check(strstr(panic_message,
		    "H10 T rc=0 att=1 same=1 div=0") != NULL,
		    "the H10 width-revert failure lost the passing content "
		    "pass");
		check(strstr(panic_message,
		    "H10 V rc=-19 sw=1 swrc=-19 c6=00000900 hb=02 ha=00 "
		    "es=00000190") != NULL,
		    "the H10 width-revert failure did not record the "
		    "byte-183 mismatch");
		check(strstr(panic_message,
		    "H10 X rc=0 sw=0 c6=00000900 hs=00") != NULL,
		    "the H10 width-revert failure did not revert HS");
		check(cmd17_count == 50u,
		    "the H10 width-revert failure ran Q");
	} else if (strcmp(scenario, "probe-h10-hs-revert-fail") == 0) {
		check(panicked == 1, "the H10 HS-revert failure did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=-22 stop=16") != NULL,
		    "the H10 HS-revert failure does not keep stop 16");
		check(strstr(panic_message,
		    "H10 V rc=0 sw=0 swrc=0") != NULL,
		    "the H10 HS-revert failure lost the clean width revert");
		check(strstr(panic_message,
		    "H10 X rc=-22 sw=-22 c6=00000900 hs=01") != NULL,
		    "the H10 HS-revert failure did not record the byte-185 "
		    "mismatch");
		check(cmd17_count == 50u,
		    "the H10 HS-revert failure ran Q");
	} else if (strcmp(scenario, "probe-h10-q-fail") == 0) {
		check(panicked == 1, "the failed H10 recovery read did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=-12 stop=17") != NULL,
		    "the failed H10 recovery read does not keep stop 17");
		check(strstr(panic_message,
		    "H10 Q rc=-12 qb0=00000000") != NULL,
		    "the failed H10 recovery read lost its core code");
		check(cmd17_count == 51u,
		    "the failed H10 recovery read did not stop at Q");
	} else if (strcmp(scenario, "probe-h11-honored-pass") == 0) {
		check(panicked == 1, "the H11 honored pass did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=0 stop=0") != NULL,
		    "the H11 honored pass does not name its stop");
		check(strstr(panic_message,
		    "H11 R clk=0007 hs=00 bw=00 hc=00 es=0000018e") != NULL,
		    "the H11 R line lost the 400 kHz identity");
		check(strstr(panic_message,
		    "H11 S rc=0 sw=0 c6=00000900 hs=01 clk=0007 "
		    "rcg=0000020b sum=0000018f") != NULL,
		    "the H11 S line lost the HS switch or the 50 MHz RCG");
		check(strstr(panic_message,
		    "H11 W4 rc=-19 sw=0 swrc=-19 c6=00000900 hb=00 ha=02 "
		    "es=0000018f") != NULL,
		    "the H11 W4 line did not replay the H10 legacy tuple");
		check(strstr(panic_message,
		    "H11 W8 rc=0 sw=2 swrc=0 c6=00000900 hb=00 ha=20 "
		    "es=00000191") != NULL,
		    "the H11 W8 line lost the honored 8-bit switch");
		check(strstr(panic_message,
		    "H11 B rc=0 tt=1 trc=0 tw=000055aa te=000055aa")
		    != NULL,
		    "the H11 bus test did not pass at 8-bit/HS");
		check(strstr(panic_message,
		    "H11 T rc=0 att=1 same=1 div=0") != NULL,
		    "the H11 content pass did not match");
		check(strstr(panic_message,
		    "H11 V rc=0 sw=0 swrc=0 c6=00000900 hb=20 ha=00 "
		    "es=0000018f") != NULL,
		    "the H11 width revert did not restore 1-bit");
		check(strstr(panic_message,
		    "H11 X rc=0 sw=0 c6=00000900 hs=00 clk=0007 "
		    "rcg=00002017 sum=0000018e") != NULL,
		    "the H11 HS revert did not restore legacy timing");
		check(strstr(panic_message,
		    "H11 Q rc=0 qb0=5c510100") != NULL,
		    "the H11 recovery read did not match block 0");
		check(strstr(panic_message,
		    "H11 PAD pre=00009fe4/60000000/00000000 "
		    "post=00009fe4/60000000/00000000") != NULL,
		    "the H11 pad samples changed");
		check(strstr(panic_message,
		    "H11 FAIL fail=0/0/0/00000000/0000/0000/00000000/0")
		    != NULL,
		    "the H11 pass invented a failure record");
		check(cmd6_count == 5u,
		    "the H11 pass did not issue both width attempts and "
		    "both reverts");
		check(cmd17_count == 51u,
		    "the H11 pass did not run both content passes and Q");
	} else if (strcmp(scenario, "probe-h11-w4-replay") == 0) {
		check(panicked == 1, "the H11 W4 replay did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-19 stop=12") != NULL,
		    "the H11 W4 replay does not keep stop 12");
		check(strstr(panic_message,
		    "H11 W4 rc=-19 sw=0 swrc=-19 c6=00000900 hb=00 ha=02 "
		    "es=0000018f") != NULL,
		    "the H11 W4 replay did not reproduce the H10 tuple");
		check(strstr(panic_message,
		    "H11 W8 rc=-19 sw=0 swrc=-19 c6=00000900 hb=00 ha=20 "
		    "es=0000018f") != NULL,
		    "the H11 W4 replay did not record the ignored 8-bit "
		    "attempt");
		check(strstr(panic_message,
		    "H11 B rc=0 tt=0 trc=0 tw=00000000 te=00000000")
		    != NULL,
		    "the H11 W4 replay ran the bus test on an unproven "
		    "framing");
		check(strstr(panic_message,
		    "H11 T rc=0 att=0") != NULL,
		    "the H11 W4 replay ran the content pass on an unproven "
		    "framing");
		check(strstr(panic_message,
		    "H11 V rc=0 sw=0 swrc=0 c6=00000900 hb=00 ha=00 "
		    "es=0000018f") != NULL,
		    "the H11 W4 replay did not unwind to 1-bit");
		check(strstr(panic_message,
		    "H11 X rc=0 sw=0 c6=00000900 hs=00 clk=0007 "
		    "rcg=00002017 sum=0000018e") != NULL,
		    "the H11 W4 replay did not revert HS");
		check(cmd17_count == 25u,
		    "the H11 W4 replay ran the content pass");
		check(cmd6_count == 5u,
		    "the H11 W4 replay did not issue both attempts and both "
		    "reverts");
	} else if (strcmp(scenario, "probe-h11-w4-mismatch") == 0) {
		check(panicked == 1, "the H11 W4 mismatch did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-19 stop=12") != NULL,
		    "the H11 W4 mismatch does not keep stop 12");
		check(strstr(panic_message,
		    "H11 W4 rc=0 sw=1 swrc=0 c6=00000900 hb=00 ha=02 "
		    "es=00000190") != NULL,
		    "the H11 W4 mismatch did not record the honored 4-bit "
		    "replay");
		check(strstr(panic_message,
		    "H11 W8 rc=-19 sw=1 swrc=-19 c6=00000900 hb=02 ha=20 "
		    "es=00000190") != NULL,
		    "the H11 W4 mismatch did not record the ignored 8-bit "
		    "attempt from the 4-bit host state");
		check(strstr(panic_message,
		    "H11 V rc=0 sw=0 swrc=0 c6=00000900 hb=02 ha=00 "
		    "es=0000018f") != NULL,
		    "the H11 W4 mismatch did not unwind to 1-bit");
		check(cmd17_count == 25u,
		    "the H11 W4 mismatch ran the content pass");
		check(cmd6_count == 5u,
		    "the H11 W4 mismatch did not issue both attempts and "
		    "both reverts");
	} else if (strcmp(scenario, "probe-h11-w8-div") == 0) {
		check(panicked == 1, "the H11 W8 paradox did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-19 stop=12") != NULL,
		    "the H11 W8 paradox does not keep stop 12");
		check(strstr(panic_message,
		    "H11 W4 rc=-19 sw=0 swrc=-19 c6=00000900 hb=00 ha=02 "
		    "es=0000018f") != NULL,
		    "the H11 W8 paradox lost the record-only 4-bit replay");
		check(strstr(panic_message,
		    "H11 W8 rc=-19 sw=2 swrc=0 c6=00000900 hb=00 ha=20 "
		    "es=0000018f") != NULL,
		    "the H11 W8 paradox did not record the disagreeing "
		    "readback and re-read");
		check(strstr(panic_message,
		    "H11 B rc=0 tt=0") != NULL,
		    "the H11 W8 paradox ran the bus test on an unproven "
		    "framing");
		check(strstr(panic_message,
		    "H11 T rc=0 att=0") != NULL,
		    "the H11 W8 paradox ran the content pass on an unproven "
		    "framing");
		check(strstr(panic_message,
		    "H11 V rc=0 sw=0 swrc=0 c6=00000900 hb=20 ha=00 "
		    "es=0000018f") != NULL,
		    "the H11 W8 paradox did not revert the width");
		check(cmd17_count == 25u,
		    "the H11 W8 paradox ran the content pass");
	} else if (strcmp(scenario, "probe-h11-w8-err") == 0) {
		check(panicked == 1, "the H11 W8 error did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-15 stop=12") != NULL,
		    "the H11 W8 error does not keep stop 12");
		check(strstr(panic_message,
		    "H11 W8 rc=-15 sw=0 swrc=-15 c6=0000a000 hb=00 ha=00 "
		    "es=00000000") != NULL,
		    "the H11 W8 error did not record the R1 refusal");
		check(strstr(panic_message,
		    "H11 V rc=0 sw=0 swrc=0 c6=00000900 hb=00 ha=00 "
		    "es=0000018f") != NULL,
		    "the H11 W8 error did not revert the width");
		check(cmd17_count == 25u,
		    "the H11 W8 error ran the content pass");
		check(cmd6_count == 5u,
		    "the H11 W8 error did not issue both attempts and both "
		    "reverts");
	} else if (strcmp(scenario, "probe-h11-w4-anomaly") == 0) {
		check(panicked == 1, "the H11 W4 anomaly did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-19 stop=18") != NULL,
		    "the H11 W4 anomaly does not keep stop 18");
		check(strstr(panic_message,
		    "H11 W4 rc=-19 sw=0 swrc=-19 c6=00000900 hb=00 ha=02 "
		    "es=00000000") != NULL,
		    "the H11 W4 anomaly did not record the empty re-read");
		check(strstr(panic_message,
		    "H11 W8 rc=0 sw=0 swrc=0 c6=00000000 hb=00 ha=00 "
		    "es=00000000") != NULL,
		    "the H11 W4 anomaly continued to the 8-bit attempt");
		check(strstr(panic_message,
		    "H11 T rc=0 att=0") != NULL,
		    "the H11 W4 anomaly ran the content pass");
		check(strstr(panic_message,
		    "H11 V rc=0 sw=0 swrc=0 c6=00000900 hb=00 ha=00 "
		    "es=0000018f") != NULL,
		    "the H11 W4 anomaly did not unwind the width");
		check(strstr(panic_message,
		    "H11 X rc=0 sw=0 c6=00000900 hs=00 clk=0007 "
		    "rcg=00002017 sum=0000018e") != NULL,
		    "the H11 W4 anomaly did not revert HS");
		check(cmd17_count == 25u,
		    "the H11 W4 anomaly ran the content pass");
		check(cmd6_count == 4u,
		    "the H11 W4 anomaly issued the 8-bit attempt");
	} else if (strcmp(scenario, "probe-h11-clock-busy") == 0) {
		check(panicked == 1, "the busy H11 RCG did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-5 stop=11") != NULL,
		    "the busy H11 RCG does not keep stop 11");
		check(strstr(panic_message,
		    "H11 S rc=-5 sw=0 c6=00000900 hs=01 clk=0000") != NULL,
		    "the busy H11 RCG lost the completed switch or the "
		    "failed clock");
		check(strstr(panic_message,
		    "H11 W4 rc=0 sw=0 swrc=0 c6=00000000") != NULL,
		    "the busy H11 RCG ran the width work");
		check(strstr(panic_message,
		    "H11 X rc=-5 sw=0 c6=00000000 hs=00") != NULL,
		    "the busy H11 RCG revert failure was not recorded");
		check(cmd17_count == 25u,
		    "the busy H11 RCG still ran the content pass");
		check(cmd6_count == 1u,
		    "the busy H11 RCG issued a width command");
		check((sim_load(sdhci, GTA4L_EMMC_REG_CLOCK_CONTROL, 2u) &
		    GTA4L_EMMC_CLOCK_CARD_EN) == 0u,
		    "the busy H11 RCG left the card clock enabled");
	} else if (strcmp(scenario, "probe-h11-bustest-timeout") == 0) {
		check(panicked == 1, "the H11 bus-test timeout did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-11 stop=13") != NULL,
		    "the H11 bus-test timeout does not keep stop 13");
		check(strstr(panic_message,
		    "H11 W8 rc=0 sw=2 swrc=0 c6=00000900 hb=00 ha=20 "
		    "es=00000191") != NULL,
		    "the H11 bus-test timeout lost the honored 8-bit switch");
		check(strstr(panic_message,
		    "H11 B rc=-11 tt=1 trc=-11 tw=00000000 te=00000000")
		    != NULL,
		    "the H11 bus-test timeout lost the 8-bit write-phase "
		    "class");
		check(strstr(panic_message,
		    "H11 V rc=0 sw=0 swrc=0") != NULL,
		    "the H11 bus-test timeout did not restore 1-bit");
		check(strstr(panic_message,
		    "H11 X rc=0 sw=0 c6=00000000 hs=00 clk=0000 "
		    "rcg=00000000 sum=00000000") != NULL,
		    "the H11 bus-test failure walked back into the inhibited "
		    "controller");
		check(cmd17_count == 25u,
		    "the H11 bus-test timeout ran the content pass");
	} else if (strcmp(scenario, "probe-h11-bustest-mismatch") == 0) {
		check(panicked == 1, "the H11 bus-test mismatch did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-20 stop=13") != NULL,
		    "the H11 bus-test mismatch does not keep stop 13");
		check(strstr(panic_message,
		    "H11 B rc=-20 tt=1 trc=-20 tw=0000aa55 te=000055aa")
		    != NULL,
		    "the H11 bus-test mismatch did not bind the pattern "
		    "words");
		check(cmd17_count == 25u,
		    "the H11 bus-test mismatch ran the content pass");
	} else if (strcmp(scenario, "probe-h11-content-div") == 0) {
		check(panicked == 1, "the H11 content divergence did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-21 stop=14") != NULL,
		    "the H11 divergence does not keep stop 14");
		check(strstr(panic_message,
		    "H11 T rc=0 att=1 same=0 div=34") != NULL,
		    "the H11 divergence did not name the first differing "
		    "LBA");
		check(strstr(panic_message,
		    "H11 V rc=0 sw=0 swrc=0 c6=00000900") != NULL,
		    "the H11 divergence run did not revert the width");
		check(strstr(panic_message,
		    "H11 X rc=0 sw=0 c6=00000900 hs=00") != NULL,
		    "the H11 divergence run did not revert HS");
		check(cmd17_count == 50u,
		    "the H11 divergence did not stop at Q");
	} else if (strcmp(scenario, "probe-h11-content-read") == 0) {
		check(panicked == 1, "the H11 content read error did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-12 stop=14") != NULL,
		    "the H11 content read error does not keep stop 14");
		check(strstr(panic_message,
		    "H11 T rc=-12 att=1") != NULL,
		    "the H11 content read error lost its core code");
		check(cmd17_count == 26u,
		    "the H11 content read error did not stop at the first "
		    "8-bit read");
	} else if (strcmp(scenario, "probe-h11-width-revert-fail") == 0) {
		check(panicked == 1, "the H11 width-revert failure did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-19 stop=15") != NULL,
		    "the H11 width-revert failure does not keep stop 15");
		check(strstr(panic_message,
		    "H11 T rc=0 att=1 same=1 div=0") != NULL,
		    "the H11 width-revert failure lost the passing content "
		    "pass");
		check(strstr(panic_message,
		    "H11 V rc=-19 sw=2 swrc=-19 c6=00000900 hb=20 ha=00 "
		    "es=00000191") != NULL,
		    "the H11 width-revert failure did not record the "
		    "byte-183 mismatch");
		check(strstr(panic_message,
		    "H11 X rc=0 sw=0 c6=00000900 hs=00") != NULL,
		    "the H11 width-revert failure did not revert HS");
		check(sim_host_width_code() ==
		    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8,
		    "the H11 width-revert failure did not leave the host "
		    "width at the failed value");
		check(cmd17_count == 50u,
		    "the H11 width-revert failure ran Q");
	} else if (strcmp(scenario, "probe-h11-hs-revert-fail") == 0) {
		check(panicked == 1, "the H11 HS-revert failure did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-22 stop=16") != NULL,
		    "the H11 HS-revert failure does not keep stop 16");
		check(strstr(panic_message,
		    "H11 V rc=0 sw=0 swrc=0") != NULL,
		    "the H11 HS-revert failure lost the clean width revert");
		check(strstr(panic_message,
		    "H11 X rc=-22 sw=-22 c6=00000900 hs=01") != NULL,
		    "the H11 HS-revert failure did not record the byte-185 "
		    "mismatch");
		check(cmd17_count == 50u,
		    "the H11 HS-revert failure ran Q");
	} else if (strcmp(scenario, "probe-h11-q-fail") == 0) {
		check(panicked == 1, "the failed H11 recovery read did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H11 REPORT: rc=-12 stop=17") != NULL,
		    "the failed H11 recovery read does not keep stop 17");
		check(strstr(panic_message,
		    "H11 Q rc=-12 qb0=00000000") != NULL,
		    "the failed H11 recovery read lost its core code");
		check(cmd17_count == 51u,
		    "the failed H11 recovery read did not stop at Q");
	} else if (strcmp(scenario, "probe-h12-refused-pass") == 0) {
		check(panicked == 1, "the H12 pass did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=0 stop=0") != NULL,
		    "the H12 pass does not name its stop");
		check(strstr(panic_message,
		    "H12 R clk=0007 hs=00 bw=00 hc=00 es=0000018e") != NULL,
		    "the H12 R line lost the 400 kHz identity");
		check(strstr(panic_message,
		    "H12 S rc=0 sw=0 c6=00000900 hs=01 clk=0007 "
		    "rcg=0000020b sum=0000018f") != NULL,
		    "the H12 S line lost the HS switch or the 50 MHz RCG");
		check(strstr(panic_message,
		    "H12 WDDR rc=-19 sw=0 swrc=-19 c6=00000900 "
		    "es=0000018f") != NULL,
		    "the H12 clean refusal did not record the ignored "
		    "DDR value");
		check(strstr(panic_message,
		    "H12 VR rc=0 sw=0 swrc=0 c6=00000900 hb=00 ha=00 "
		    "es=0000018f") != NULL,
		    "the H12 clean refusal did not revert the width");
		check(strstr(panic_message,
		    "H12 X rc=0 sw=0 c6=00000900 hs=00 clk=0007 "
		    "rcg=00002017 sum=0000018e") != NULL,
		    "the H12 clean refusal did not revert HS");
		check(strstr(panic_message,
		    "H12 Q rc=0 qb0=5c510100") != NULL,
		    "the H12 recovery read did not match block 0");
		check(strstr(panic_message,
		    "H12 PAD pre=00009fe4/60000000/00000000 "
		    "post=00009fe4/60000000/00000000") != NULL,
		    "the H12 pad samples changed");
		check(strstr(panic_message,
		    "H12 FAIL fail=0/0/0/00000000/0000/0000/00000000/0")
		    != NULL,
		    "the H12 pass invented a failure record");
		check(cmd6_count == 4u,
		    "the H12 pass did not issue the DDR attempt and both "
		    "reverts");
		check(cmd17_count == 26u,
		    "the H12 pass did not run the stage-A reads and Q");
		check(cmd6_value4_count == 1u,
		    "the H12 pass did not issue exactly one byte-183 "
		    "value-4 attempt");
	} else if (strcmp(scenario, "probe-h12-honored") == 0) {
		check(panicked == 1, "the honored H12 pass did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=0 stop=13") != NULL,
		    "the honored H12 pass does not name its stop");
		check(strstr(panic_message,
		    "H12 WDDR rc=0 sw=4 swrc=0 c6=00000900 "
		    "es=00000193") != NULL,
		    "the honored H12 pass lost the DDR readback");
		check(strstr(panic_message,
		    "H12 VR rc=0 sw=0 swrc=0 c6=00000900 hb=02 ha=00 "
		    "es=0000018f") != NULL,
		    "the honored H12 pass did not revert the width");
		check(strstr(panic_message,
		    "H12 X rc=0 sw=0 c6=00000900 hs=00 clk=0007 "
		    "rcg=00002017 sum=0000018e") != NULL,
		    "the honored H12 pass did not revert HS");
		check(strstr(panic_message,
		    "H12 Q rc=0 qb0=5c510100") != NULL,
		    "the honored H12 recovery read did not match block 0");
		check(cmd6_count == 4u,
		    "the honored H12 pass did not issue the DDR attempt and "
		    "both reverts");
		check(cmd17_count == 26u,
		    "the honored H12 pass did not run the stage-A reads "
		    "and Q");
		check(cmd6_value4_count == 1u,
		    "the honored H12 pass did not issue exactly one byte-183 "
		    "value-4 attempt");
	} else if (strcmp(scenario, "probe-h12-hs-ignored") == 0) {
		check(panicked == 1, "the ignored H12 HS switch did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=-22 stop=10") != NULL,
		    "the ignored H12 HS switch does not keep stop 10");
		check(strstr(panic_message,
		    "H12 S rc=-22 sw=-22 c6=00000900 hs=00 clk=0000 "
		    "rcg=00000000 sum=0000018e") != NULL,
		    "the ignored H12 HS switch did not record the readback "
		    "mismatch");
		check(strstr(panic_message,
		    "H12 WDDR rc=0 sw=0 swrc=0 c6=00000000") != NULL,
		    "the ignored H12 HS switch continued to the DDR step");
		check(strstr(panic_message,
		    "H12 VR rc=0 sw=0 swrc=0 c6=00000000") != NULL,
		    "the ignored H12 HS switch ran the width revert");
		check(strstr(panic_message,
		    "H12 X rc=0 sw=0 c6=00000900 hs=00 clk=0007 "
		    "rcg=00002017 sum=0000018e") != NULL,
		    "the ignored H12 HS switch did not revert cleanly");
		check(cmd17_count == 25u,
		    "the ignored H12 HS switch ran Q");
		check(cmd6_count == 2u,
		    "the ignored H12 HS switch did not issue both byte-185 "
		    "commands");
		check(cmd6_value4_count == 0u,
		    "the ignored H12 HS switch issued a byte-183 DDR "
		    "attempt");
	} else if (strcmp(scenario, "probe-h12-clock-busy") == 0) {
		check(panicked == 1, "the busy H12 RCG did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=-5 stop=11") != NULL,
		    "the busy H12 RCG does not keep stop 11");
		check(strstr(panic_message,
		    "H12 S rc=-5 sw=0 c6=00000900 hs=01 clk=0000") != NULL,
		    "the busy H12 RCG lost the completed switch or the "
		    "failed clock");
		check(strstr(panic_message,
		    "H12 WDDR rc=0 sw=0 swrc=0 c6=00000000") != NULL,
		    "the busy H12 RCG ran the DDR work");
		check(strstr(panic_message,
		    "H12 X rc=-5 sw=0 c6=00000000 hs=00") != NULL,
		    "the busy H12 RCG revert failure was not recorded");
		check(cmd17_count == 25u,
		    "the busy H12 RCG ran Q");
		check(cmd6_count == 1u,
		    "the busy H12 RCG issued a width command");
		check(cmd6_value4_count == 0u,
		    "the busy H12 RCG issued a byte-183 DDR attempt");
		check((sim_load(sdhci, GTA4L_EMMC_REG_CLOCK_CONTROL, 2u) &
		    GTA4L_EMMC_CLOCK_CARD_EN) == 0u,
		    "the busy H12 RCG left the card clock enabled");
	} else if (strcmp(scenario, "probe-h12-wddr-err") == 0) {
		check(panicked == 1, "the refused H12 DDR value did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=-15 stop=12") != NULL,
		    "the refused H12 DDR value does not keep stop 12");
		check(strstr(panic_message,
		    "H12 WDDR rc=-15 sw=0 swrc=-15 c6=0000a000 "
		    "es=00000000") != NULL,
		    "the refused H12 DDR value did not record the R1 "
		    "refusal");
		check(strstr(panic_message,
		    "H12 VR rc=0 sw=0 swrc=0 c6=00000900 hb=00 ha=00 "
		    "es=0000018f") != NULL,
		    "the refused H12 DDR value did not revert the width");
		check(strstr(panic_message,
		    "H12 X rc=0 sw=0 c6=00000900 hs=00 clk=0007 "
		    "rcg=00002017 sum=0000018e") != NULL,
		    "the refused H12 DDR value did not revert HS");
		check(cmd17_count == 25u,
		    "the refused H12 DDR value ran Q");
		check(cmd6_count == 4u,
		    "the refused H12 DDR value did not issue the attempt "
		    "and both reverts");
		check(cmd6_value4_count == 1u,
		    "the refused H12 DDR value did not issue exactly one "
		    "byte-183 value-4 attempt");
	} else if (strcmp(scenario, "probe-h12-wddr-empty") == 0) {
		check(panicked == 1, "the empty H12 DDR readback did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=-19 stop=12") != NULL,
		    "the empty H12 DDR readback does not keep stop 12");
		check(strstr(panic_message,
		    "H12 WDDR rc=-19 sw=0 swrc=-19 c6=00000900 "
		    "es=00000000") != NULL,
		    "the empty H12 DDR readback did not record the empty "
		    "verification read");
		check(strstr(panic_message,
		    "H12 VR rc=0 sw=0 swrc=0 c6=00000900 hb=00 ha=00 "
		    "es=0000018f") != NULL,
		    "the empty H12 DDR readback did not revert the width");
		check(cmd17_count == 25u,
		    "the empty H12 DDR readback ran Q");
		check(cmd6_count == 4u,
		    "the empty H12 DDR readback did not issue the attempt "
		    "and both reverts");
		check(cmd6_value4_count == 1u,
		    "the empty H12 DDR readback did not issue exactly one "
		    "byte-183 value-4 attempt");
	} else if (strcmp(scenario, "probe-h12-wddr-paradox") == 0) {
		check(panicked == 1, "the H12 DDR paradox did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=-19 stop=12") != NULL,
		    "the H12 DDR paradox does not keep stop 12");
		check(strstr(panic_message,
		    "H12 WDDR rc=0 sw=4 swrc=0 c6=00000900 "
		    "es=0000018f") != NULL,
		    "the H12 DDR paradox did not record the disagreeing "
		    "readback and re-read");
		check(strstr(panic_message,
		    "H12 VR rc=0 sw=0 swrc=0 c6=00000900 hb=02 ha=00 "
		    "es=0000018f") != NULL,
		    "the H12 DDR paradox did not revert the width");
		check(cmd17_count == 25u,
		    "the H12 DDR paradox ran Q");
		check(cmd6_count == 4u,
		    "the H12 DDR paradox did not issue the attempt and both "
		    "reverts");
	} else if (strcmp(scenario, "probe-h12-width-revert-fail") == 0) {
		check(panicked == 1, "the H12 width-revert failure did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=-19 stop=14") != NULL,
		    "the H12 width-revert failure does not keep stop 14");
		check(strstr(panic_message,
		    "H12 WDDR rc=0 sw=4 swrc=0 c6=00000900 "
		    "es=00000193") != NULL,
		    "the H12 width-revert failure lost the honored DDR "
		    "readback");
		check(strstr(panic_message,
		    "H12 VR rc=-19 sw=4 swrc=-19 c6=00000900 hb=02 ha=00 "
		    "es=00000193") != NULL,
		    "the H12 width-revert failure did not record the "
		    "byte-183 mismatch");
		check(strstr(panic_message,
		    "H12 X rc=0 sw=0 c6=00000900 hs=00") != NULL,
		    "the H12 width-revert failure did not revert HS");
		check(sim_host_width_code() ==
		    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4,
		    "the H12 width-revert failure did not leave the host "
		    "width at the failed value");
		check(cmd17_count == 25u,
		    "the H12 width-revert failure ran Q");
	} else if (strcmp(scenario, "probe-h12-hs-revert-fail") == 0) {
		check(panicked == 1, "the H12 HS-revert failure did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=-22 stop=15") != NULL,
		    "the H12 HS-revert failure does not keep stop 15");
		check(strstr(panic_message,
		    "H12 VR rc=0 sw=0 swrc=0 c6=00000900 hb=02 ha=00 "
		    "es=0000018f") != NULL,
		    "the H12 HS-revert failure lost the clean width revert");
		check(strstr(panic_message,
		    "H12 X rc=-22 sw=-22 c6=00000900 hs=01 clk=0007 "
		    "rcg=00002017 sum=0000018f") != NULL,
		    "the H12 HS-revert failure did not record the byte-185 "
		    "mismatch");
		check(cmd17_count == 25u,
		    "the H12 HS-revert failure ran Q");
	} else if (strcmp(scenario, "probe-h12-q-fail") == 0) {
		check(panicked == 1, "the failed H12 recovery read did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=-12 stop=15") != NULL,
		    "the failed H12 recovery read does not keep stop 15");
		check(strstr(panic_message,
		    "H12 Q rc=-12 qb0=00000000") != NULL,
		    "the failed H12 recovery read lost its core code");
		check(cmd17_count == 26u,
		    "the failed H12 recovery read did not stop at Q");
	} else if (strcmp(scenario, "probe-h12-pre-read-fail") == 0) {
		check(panicked == 1, "the failed H12 pre-read did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=-12 stop=10") != NULL,
		    "the failed H12 pre-read does not keep stop 10");
		check(strstr(panic_message,
		    "H12 R clk=0007 hs=00 bw=00 hc=00 es=00000000") != NULL,
		    "the failed H12 pre-read lost the 400 kHz identity");
		check(strstr(panic_message,
		    "H12 S rc=-12 sw=0 c6=00000000 hs=00") != NULL,
		    "the failed H12 pre-read still issued a CMD6");
		check(strstr(panic_message,
		    "H12 WDDR rc=0 sw=0 swrc=0 c6=00000000") != NULL,
		    "the failed H12 pre-read continued to the DDR step");
		check(cmd6_count == 0u,
		    "the failed H12 pre-read issued a CMD6");
		check(cmd17_count == 25u,
		    "the failed H12 pre-read ran Q");
		check(cmd6_value4_count == 0u,
		    "the failed H12 pre-read issued a byte-183 DDR "
		    "attempt");
	} else if (strcmp(scenario, "probe-h13-pass") == 0) {
		check(panicked == 1, "the H13 rung-1 pass did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 REPORT: rc=0 stop=0") != NULL,
		    "the H13 rung-1 pass does not keep stop 0");
		check(strstr(panic_message,
		    "H13 R clk=0007 hc=00 bw=00 es=0000018e") != NULL,
		    "the H13 rung-1 pass lost the 400 kHz identity");
		check(strstr(panic_message,
		    "H13 W wrc=-11 err=0000 nrm=0011 pst=03f801f6") != NULL,
		    "the H13 rung-1 pass lost the write-phase wedge tuple");
		check(strstr(panic_message,
		    "H13 RST rc=0 wr=04 rb=00 it=1 pst=03f801f4 dinh=0")
		    != NULL,
		    "the H13 rung-1 pass lost the self-clearing data reset");
		check(strstr(panic_message,
		    "H13 T trc=0 tw=00000040 te=00000040") != NULL,
		    "the H13 rung-1 pass lost the matching CMD14 word");
		check(strstr(panic_message,
		    "H13 Q rc=0 qb0=5c510100") != NULL,
		    "the H13 recovery read did not match block 0");
		check(strstr(panic_message,
		    "H13 PAD pre=00009fe4/60000000/00000000 "
		    "post=00009fe4/60000000/00000000") != NULL,
		    "the H13 pad samples changed");
		check(strstr(panic_message,
		    "H13 FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/18")
		    != NULL,
		    "the H13 rung-1 pass lost the first CMD19 failure");
		check(cmd19_count == 1u && cmd14_count == 1u,
		    "the H13 rung-1 pass command counts mismatch");
		check(cmd17_count == 26u,
		    "the H13 rung-1 pass did not run the stage-A reads and Q");
		check(sim_host_width_code() ==
		    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1,
		    "the H13 rung-1 pass changed the host width");
	} else if (strcmp(scenario, "probe-h13-reset-stuck") == 0) {
		check(panicked == 1, "the stuck H13 data reset did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 REPORT: rc=-4 stop=10") != NULL,
		    "the stuck H13 data reset does not keep stop 10");
		check(strstr(panic_message,
		    "H13 RST rc=-4 wr=04 rb=04 it=10000 pst=00000000 "
		    "dinh=0") != NULL,
		    "the stuck H13 data reset lost its poll evidence");
		check(strstr(panic_message,
		    "H13 T trc=0 tw=00000000 te=00000000") != NULL,
		    "the stuck H13 data reset still compared a CMD14 word");
		check(strstr(panic_message,
		    "H13 Q rc=-17 qb0=00000000") != NULL,
		    "the stuck H13 data reset still read block 0");
		check(cmd19_count == 1u && cmd14_count == 0u,
		    "the stuck H13 data reset still issued CMD14");
		check(cmd17_count == 25u,
		    "the stuck H13 data reset walked into the inhibited "
		    "controller");
	} else if (strcmp(scenario, "probe-h13-inhibit") == 0) {
		check(panicked == 1, "the surviving H13 data inhibit did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 REPORT: rc=-17 stop=11") != NULL,
		    "the surviving H13 data inhibit does not keep stop 11");
		check(strstr(panic_message,
		    "H13 RST rc=0 wr=04 rb=00 it=1 pst=03f801f6 dinh=2")
		    != NULL,
		    "the surviving H13 data inhibit lost its post-reset "
		    "evidence");
		check(strstr(panic_message,
		    "H13 T trc=-17 tw=00000000 te=00000000") != NULL,
		    "the surviving H13 data inhibit did not record its "
		    "refusal");
		check(cmd19_count == 1u && cmd14_count == 0u,
		    "the surviving H13 data inhibit still issued CMD14");
		check(cmd17_count == 25u,
		    "the surviving H13 data inhibit walked into the "
		    "inhibited controller");
	} else if (strcmp(scenario, "probe-h13-mismatch") == 0) {
		check(panicked == 1, "the H13 CMD14 mismatch did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 REPORT: rc=-20 stop=13") != NULL,
		    "the H13 CMD14 mismatch does not keep stop 13");
		check(strstr(panic_message,
		    "H13 T trc=-20 tw=00000080 te=00000040") != NULL,
		    "the H13 CMD14 mismatch did not bind the pattern words");
		check(strstr(panic_message,
		    "H13 Q rc=0 qb0=5c510100") != NULL,
		    "the H13 CMD14 mismatch lost the recovery read");
		check(cmd19_count == 1u && cmd14_count == 1u,
		    "the H13 CMD14 mismatch command counts mismatch");
	} else if (strcmp(scenario, "probe-h13-read-timeout") == 0) {
		check(panicked == 1, "the H13 CMD14 timeout did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 REPORT: rc=-11 stop=14") != NULL,
		    "the H13 CMD14 timeout does not keep stop 14");
		check(strstr(panic_message,
		    "H13 T trc=-11 tw=00000000 te=00000000") != NULL,
		    "the H13 CMD14 timeout lost its core code");
		check(cmd19_count == 1u && cmd14_count == 1u,
		    "the H13 CMD14 timeout command counts mismatch");
	} else if (strcmp(scenario, "probe-h13-read-crc") == 0) {
		check(panicked == 1, "the H13 CMD14 CRC did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 REPORT: rc=0 stop=0") != NULL,
		    "the optional CMD14 pattern CRC was not ignored");
		check(strstr(panic_message,
		    "H13 T trc=0 tw=00000040 te=00000040") != NULL,
		    "the ignored CMD14 CRC lost the pattern result");
		check(cmd19_count == 1u && cmd14_count == 1u,
		    "the H13 CMD14 CRC command counts mismatch");
	} else if (strcmp(scenario, "probe-h13-read-status") == 0) {
		check(panicked == 1, "the H13 CMD14 status error did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 REPORT: rc=-15 stop=14") != NULL,
		    "the H13 CMD14 status error does not keep stop 14");
		check(strstr(panic_message,
		    "H13 T trc=-15 tw=00000000 te=00000000") != NULL,
		    "the H13 CMD14 status error lost its core code");
		check(cmd19_count == 1u && cmd14_count == 1u,
		    "the H13 CMD14 status error command counts mismatch");
	} else if (strcmp(scenario, "probe-h13-write-crc") == 0) {
		check(panicked == 1, "the H13 write CRC did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 REPORT: rc=-12 stop=17") != NULL,
		    "the H13 write CRC does not keep stop 17");
		check(strstr(panic_message,
		    "H13 W wrc=-12 err=0020 nrm=0000 pst=00000000") != NULL,
		    "the H13 write CRC lost the terminal write tuple");
		check(strstr(panic_message,
		    "H13 RST rc=0 wr=04 rb=00 it=1") != NULL,
		    "the H13 write CRC did not run the clean reset");
		check(cmd19_count == 1u && cmd14_count == 1u,
		    "the H13 write CRC command counts mismatch");
	} else if (strcmp(scenario, "probe-h13-normal") == 0) {
		check(panicked == 1, "the H13 non-wedge write did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 REPORT: rc=0 stop=16") != NULL,
		    "the H13 non-wedge write does not keep stop 16");
		check(strstr(panic_message,
		    "H13 W wrc=0 err=0000 nrm=0000 pst=00000000") != NULL,
		    "the H13 non-wedge write lost the clean completion");
		check(strstr(panic_message,
		    "H13 RST rc=0 wr=04 rb=00 it=1 pst=00000000 dinh=0")
		    != NULL,
		    "the H13 non-wedge write lost the clean reset");
		check(strstr(panic_message,
		    "H13 T trc=0 tw=00000040 te=00000040") != NULL,
		    "the H13 non-wedge write lost the matching CMD14 word");
		check(strstr(panic_message,
		    "H13 FAIL fail=0/0/0/00000000/0000/0000/00000000/0")
		    != NULL,
		    "the H13 non-wedge write invented a failure record");
		check(cmd19_count == 1u && cmd14_count == 1u,
		    "the H13 non-wedge write command counts mismatch");
		check(cmd17_count == 26u,
		    "the H13 non-wedge write did not run the stage-A reads "
		    "and Q");
	} else if (strcmp(scenario, "probe-h13-q-fail") == 0) {
		check(panicked == 1, "the failed H13 recovery read did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 REPORT: rc=-12 stop=15") != NULL,
		    "the failed H13 recovery read does not keep stop 15");
		check(strstr(panic_message,
		    "H13 T trc=0 tw=00000040 te=00000040") != NULL,
		    "the failed H13 recovery read lost the passing CMD14 "
		    "word");
		check(strstr(panic_message,
		    "H13 Q rc=-12 qb0=00000000") != NULL,
		    "the failed H13 recovery read lost its core code");
		check(strstr(panic_message,
		    "H13 FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/18")
		    != NULL,
		    "the failed H13 recovery read replaced the first "
		    "failure");
		check(cmd17_count == 26u,
		    "the failed H13 recovery read did not stop at Q");
	} else if (strcmp(scenario, "probe-h14-pass") == 0) {
		check(panicked == 1, "the H14 rung-2 pass did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=0 stop=0") != NULL,
		    "the H14 rung-2 pass does not keep stop 0");
		check(strstr(panic_message,
		    "H14 R clk=0007 hc=00 bw=00 es=0000018e") != NULL,
		    "the H14 rung-2 pass lost the 400 kHz identity");
		check(strstr(panic_message,
		    "H14 W wrc=-11 err=0000 nrm=0011 pst=03f801f6") != NULL,
		    "the H14 rung-2 pass lost the write-phase wedge tuple");
		check(strstr(panic_message,
		    "H14 RST rc=0 wr=04 rb=00 it=1 pst=03f801f4 dinh=0")
		    != NULL,
		    "the H14 rung-2 pass lost the self-clearing data reset");
		check(strstr(panic_message,
		    "H14 C12 rc=0 r1=00000900 err=0000 nrm=0003 "
		    "pst=03f801f4") != NULL,
		    "the H14 rung-2 pass lost the completed CMD12 drain");
		check(strstr(panic_message,
		    "H14 C13 at=1 rc=0 r1=00000900 err=0000 nrm=0001 "
		    "pst=03f801f4") != NULL,
		    "the H14 rung-2 pass lost the completed CMD13 drain");
		check(strstr(panic_message,
		    "H14 T trc=0 tw=00000040 te=00000040") != NULL,
		    "the H14 rung-2 pass lost the matching CMD14 word");
		check(strstr(panic_message,
		    "H14 Q rc=0 qb0=5c510100") != NULL,
		    "the H14 recovery read did not match block 0");
		check(strstr(panic_message,
		    "H14 PAD pre=00009fe4/60000000/00000000 "
		    "post=00009fe4/60000000/00000000") != NULL,
		    "the H14 pad samples changed");
		check(strstr(panic_message,
		    "H14 FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/18")
		    != NULL,
		    "the H14 rung-2 pass lost the first CMD19 failure");
		check(cmd19_count == 1u && cmd14_count == 1u &&
		    h14_cmd12_count == 1u && h14_cmd13_count == 1u,
		    "the H14 rung-2 pass command counts mismatch");
		check(cmd17_count == 26u,
		    "the H14 rung-2 pass did not run the stage-A reads and Q");
		check(sim_host_width_code() ==
		    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1,
		    "the H14 rung-2 pass changed the host width");
	} else if (strcmp(scenario, "probe-h14-reset-stuck") == 0) {
		check(panicked == 1, "the stuck H14 data reset did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=-4 stop=10") != NULL,
		    "the stuck H14 data reset does not keep stop 10");
		check(strstr(panic_message,
		    "H14 RST rc=-4 wr=04 rb=04 it=10000 pst=00000000 "
		    "dinh=0") != NULL,
		    "the stuck H14 data reset lost its poll evidence");
		check(strstr(panic_message,
		    "H14 C12 rc=-18 r1=00000000 err=0000 nrm=0000 "
		    "pst=00000000") != NULL,
		    "the stuck H14 data reset invented a CMD12 attempt");
		check(strstr(panic_message,
		    "H14 T trc=0 tw=00000000 te=00000000") != NULL,
		    "the stuck H14 data reset still compared a CMD14 word");
		check(strstr(panic_message,
		    "H14 Q rc=-17 qb0=00000000") != NULL,
		    "the stuck H14 data reset still read block 0");
		check(cmd19_count == 1u && cmd14_count == 0u &&
		    h14_cmd12_count == 0u && h14_cmd13_count == 0u,
		    "the stuck H14 data reset still issued a drain or CMD14");
		check(cmd17_count == 25u,
		    "the stuck H14 data reset walked into the inhibited "
		    "controller");
	} else if (strcmp(scenario, "probe-h14-inhibit") == 0) {
		check(panicked == 1, "the surviving H14 data inhibit did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=-17 stop=11") != NULL,
		    "the surviving H14 data inhibit does not keep stop 11");
		check(strstr(panic_message,
		    "H14 RST rc=0 wr=04 rb=00 it=1 pst=03f801f6 dinh=2")
		    != NULL,
		    "the surviving H14 data inhibit lost its post-reset "
		    "evidence");
		check(strstr(panic_message,
		    "H14 C12 rc=-18 r1=00000000 err=0000 nrm=0000 "
		    "pst=00000000") != NULL,
		    "the surviving H14 data inhibit invented a CMD12 "
		    "attempt");
		check(strstr(panic_message,
		    "H14 T trc=-17 tw=00000000 te=00000000") != NULL,
		    "the surviving H14 data inhibit did not record its "
		    "refusal");
		check(cmd19_count == 1u && cmd14_count == 0u &&
		    h14_cmd12_count == 0u && h14_cmd13_count == 0u,
		    "the surviving H14 data inhibit still issued a drain or "
		    "CMD14");
		check(cmd17_count == 25u,
		    "the surviving H14 data inhibit walked into the "
		    "inhibited controller");
	} else if (strcmp(scenario, "probe-h14-cmd12-silent") == 0) {
		check(panicked == 1, "the silent H14 CMD12 did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=-7 stop=13") != NULL,
		    "the silent H14 CMD12 does not keep stop 13");
		check(strstr(panic_message,
		    "H14 C12 rc=-7 r1=00000000 err=0000 nrm=0000 "
		    "pst=03f801f4") != NULL,
		    "the silent H14 CMD12 lost its terminal tuple");
		check(strstr(panic_message,
		    "H14 C13 at=0 rc=-18 r1=00000000 err=0000 nrm=0000 "
		    "pst=00000000") != NULL,
		    "the silent H14 CMD12 still issued CMD13");
		check(strstr(panic_message,
		    "H14 T trc=0 tw=00000040 te=00000040") != NULL,
		    "the silent H14 CMD12 lost the record-only CMD14 read");
		check(cmd19_count == 1u && cmd14_count == 1u &&
		    h14_cmd12_count == 1u && h14_cmd13_count == 0u,
		    "the silent H14 CMD12 command counts mismatch");
	} else if (strcmp(scenario, "probe-h14-cmd12-crc") == 0) {
		check(panicked == 1, "the H14 CMD12 CRC did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=-8 stop=13") != NULL,
		    "the H14 CMD12 CRC does not keep stop 13");
		check(strstr(panic_message,
		    "H14 C12 rc=-8 r1=00000000 err=0002 nrm=0000 "
		    "pst=03f801f4") != NULL,
		    "the H14 CMD12 CRC lost its terminal tuple");
		check(strstr(panic_message,
		    "H14 C13 at=0 rc=-18") != NULL,
		    "the H14 CMD12 CRC still issued CMD13");
		check(cmd19_count == 1u && cmd14_count == 1u &&
		    h14_cmd12_count == 1u && h14_cmd13_count == 0u,
		    "the H14 CMD12 CRC command counts mismatch");
	} else if (strcmp(scenario, "probe-h14-cmd13-fail") == 0) {
		check(panicked == 1, "the H14 CMD13 failure did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=-8 stop=14") != NULL,
		    "the H14 CMD13 failure does not keep stop 14");
		check(strstr(panic_message,
		    "H14 C12 rc=0 r1=00000900 err=0000 nrm=0003") != NULL,
		    "the H14 CMD13 failure lost the completed CMD12");
		check(strstr(panic_message,
		    "H14 C13 at=1 rc=-8 r1=00000000 err=0002 nrm=0000 "
		    "pst=03f801f4") != NULL,
		    "the H14 CMD13 failure lost its terminal tuple");
		check(strstr(panic_message,
		    "H14 T trc=0 tw=00000040 te=00000040") != NULL,
		    "the H14 CMD13 failure lost the record-only CMD14 read");
		check(cmd19_count == 1u && cmd14_count == 1u &&
		    h14_cmd12_count == 1u && h14_cmd13_count == 1u,
		    "the H14 CMD13 failure command counts mismatch");
	} else if (strcmp(scenario, "probe-h14-mismatch") == 0) {
		check(panicked == 1, "the H14 CMD14 mismatch did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=-20 stop=15") != NULL,
		    "the H14 CMD14 mismatch does not keep stop 15");
		check(strstr(panic_message,
		    "H14 C12 rc=0 r1=00000900 err=0000 nrm=0003") != NULL,
		    "the H14 CMD14 mismatch lost the clean drain");
		check(strstr(panic_message,
		    "H14 T trc=-20 tw=00000080 te=00000040") != NULL,
		    "the H14 CMD14 mismatch did not bind the pattern words");
		check(strstr(panic_message,
		    "H14 Q rc=0 qb0=5c510100") != NULL,
		    "the H14 CMD14 mismatch lost the recovery read");
		check(cmd19_count == 1u && cmd14_count == 1u,
		    "the H14 CMD14 mismatch command counts mismatch");
	} else if (strcmp(scenario, "probe-h14-read-crc") == 0) {
		check(panicked == 1, "the H14 CMD14 CRC did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=0 stop=0") != NULL,
		    "the optional CMD14 pattern CRC was not ignored");
		check(strstr(panic_message,
		    "H14 T trc=0 tw=00000040 te=00000040") != NULL,
		    "the ignored CMD14 CRC lost the pattern result");
		check(cmd19_count == 1u && cmd14_count == 1u &&
		    h14_cmd12_count == 1u && h14_cmd13_count == 1u,
		    "the H14 CMD14 CRC command counts mismatch");
	} else if (strcmp(scenario, "probe-h14-q-fail") == 0) {
		check(panicked == 1, "the failed H14 recovery read did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=-12 stop=15") != NULL,
		    "the failed H14 recovery read does not keep stop 15");
		check(strstr(panic_message,
		    "H14 T trc=0 tw=00000040 te=00000040") != NULL,
		    "the failed H14 recovery read lost the passing CMD14 "
		    "word");
		check(strstr(panic_message,
		    "H14 Q rc=-12 qb0=00000000") != NULL,
		    "the failed H14 recovery read lost its core code");
		check(strstr(panic_message,
		    "H14 FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/18")
		    != NULL,
		    "the failed H14 recovery read replaced the first "
		    "failure");
		check(cmd17_count == 26u,
		    "the failed H14 recovery read did not stop at Q");
	} else if (strcmp(scenario, "probe-h14-write-crc") == 0) {
		check(panicked == 1, "the H14 write CRC did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=-12 stop=17") != NULL,
		    "the H14 write CRC does not keep stop 17");
		check(strstr(panic_message,
		    "H14 W wrc=-12 err=0020 nrm=0000 pst=00000000") != NULL,
		    "the H14 write CRC lost the terminal write tuple");
		check(strstr(panic_message,
		    "H14 RST rc=0 wr=04 rb=00 it=1") != NULL,
		    "the H14 write CRC did not run the clean reset");
		check(strstr(panic_message,
		    "H14 C12 rc=0 r1=00000900 err=0000 nrm=0003") != NULL,
		    "the H14 write CRC lost the record-only drain");
		check(cmd19_count == 1u && cmd14_count == 1u &&
		    h14_cmd12_count == 1u && h14_cmd13_count == 1u,
		    "the H14 write CRC command counts mismatch");
	} else if (strcmp(scenario, "probe-h14-normal") == 0) {
		check(panicked == 1, "the H14 non-wedge write did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=0 stop=16") != NULL,
		    "the H14 non-wedge write does not keep stop 16");
		check(strstr(panic_message,
		    "H14 W wrc=0 err=0000 nrm=0000 pst=00000000") != NULL,
		    "the H14 non-wedge write lost the clean completion");
		check(strstr(panic_message,
		    "H14 RST rc=0 wr=04 rb=00 it=1 pst=00000000 dinh=0")
		    != NULL,
		    "the H14 non-wedge write lost the clean reset");
		check(strstr(panic_message,
		    "H14 C12 rc=0 r1=00000900 err=0000 nrm=0003 "
		    "pst=00000000") != NULL,
		    "the H14 non-wedge write lost the clean CMD12");
		check(strstr(panic_message,
		    "H14 T trc=0 tw=00000040 te=00000040") != NULL,
		    "the H14 non-wedge write lost the matching CMD14 word");
		check(strstr(panic_message,
		    "H14 FAIL fail=0/0/0/00000000/0000/0000/00000000/0")
		    != NULL,
		    "the H14 non-wedge write invented a failure record");
		check(cmd19_count == 1u && cmd14_count == 1u &&
		    h14_cmd12_count == 1u && h14_cmd13_count == 1u,
		    "the H14 non-wedge write command counts mismatch");
		check(cmd17_count == 26u,
		    "the H14 non-wedge write did not run the stage-A reads "
		    "and Q");
	} else if ((strcmp(scenario, "probe-h15-pass") == 0) ||
	    (strcmp(scenario, "probe-h15-crc") == 0)) {
		check(panicked == 1, "the H15 verified switch did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H15 REPORT: rc=0 stop=0") != NULL,
		    "the H15 verified switch did not pass");
		check(strstr(panic_message,
		    "H15 W rc=0 req=4 sw=") != NULL &&
		    strstr(panic_message, " vf=1 c6at=1 ") != NULL,
		    "the H15 W line does not show the read-only verification");
		check(strstr(panic_message,
		    (strcmp(scenario, "probe-h15-crc") == 0) ?
		    "H15 B att=1 wrc=0 rst=0 ci=1 trc=0 tw=000000a5 "
		    "te=000000a5" :
		    "H15 B att=1 wrc=0 rst=0 ci=0 trc=0 tw=000000a5 "
		    "te=000000a5") != NULL,
		    "the H15 4-bit bus test lost its pattern or CRC record");
		check(strstr(panic_message,
		    "H15 T att=1 rc=0 same=1 div=0") != NULL,
		    "the H15 4-bit content pass diverged");
		check(strstr(panic_message,
		    "H15 V att=1 rc=0 sw=0 vf=1 c6at=1 ") != NULL &&
		    strstr(panic_message, "H15 Q att=1 rc=0 qb0=5c510100")
		    != NULL,
		    "the H15 verified revert or recovery read failed");
		check(cmd6_count == 2u,
		    "the H15 run did not issue the switch and its verified "
		    "revert");
		check(cmd17_count == 51u,
		    "the H15 run did not run the baseline and 4-bit content "
		    "passes plus Q");
		check(strlen(panic_message) < 2048u,
		    "the H15 report exceeds the panic buffer");
	} else if (strcmp(scenario, "probe-h15-content-div") == 0) {
		check(panicked == 1, "the H15 divergent content pass did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H15 REPORT: rc=-21 stop=14") != NULL,
		    "the H15 content divergence did not keep stop 14");
		check(strstr(panic_message,
		    "H15 T att=1 rc=0 same=0 div=34") != NULL,
		    "the H15 content divergence did not name LBA 34");
	} else if (strcmp(scenario, "probe-h15-bustest-wedge") == 0) {
		check(panicked == 1, "the H15 4-bit write wedge did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H15 REPORT: rc=-11 stop=13") != NULL,
		    "the H15 4-bit write wedge did not keep stop 13");
		check(strstr(panic_message,
		    "H15 B att=1 wrc=-11 rst=0 ci=0 trc=-11 tw=00000000 "
		    "te=00000000") != NULL,
		    "the H15 4-bit write wedge lost the CMD19 timeout");
		check(strstr(panic_message,
		    "H15 T att=0 rc=0 same=0") != NULL,
		    "the H15 4-bit write wedge reported a content pass it "
		    "did not run");
		check(strstr(panic_message,
		    "H15 V att=1 rc=0 sw=0 vf=1 c6at=1 ") != NULL &&
		    strstr(panic_message, "H15 Q att=0 rc=0 qb0=00000000")
		    != NULL,
		    "the H15 4-bit write wedge did not recover the width");
		check(cmd6_count == 2u && cmd14_count == 0u,
		    "the H15 4-bit write wedge command counts mismatch");
	} else if (strcmp(scenario,
	    "probe-h15-bustest-wedge-unproven") == 0) {
		check(panicked == 1, "the unproven H15 recovery did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H15 REPORT: rc=-23 stop=15") != NULL,
		    "a surviving DAT inhibit did not keep stop 15");
		check(strstr(panic_message,
		    "H15 V att=1 rc=-17 sw=0 vf=0 c6at=0 ") != NULL,
		    "the unproven H15 recovery did not report the surviving "
		    "inhibit before CMD6");
		check(cmd6_count == 1u && cmd14_count == 0u,
		    "the unproven H15 recovery command counts mismatch");
	} else if (strcmp(scenario, "probe-h16-pass") == 0) {
		check(panicked == 1, "the H16 content-only rung did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H16 REPORT: rc=0 stop=0") != NULL,
		    "the H16 content-only rung did not pass");
		check(strstr(panic_message,
		    "H16 B att=0 wrc=0 rst=0 ci=0 trc=0 tw=00000000 "
		    "te=00000000") != NULL,
		    "the H16 rung issued a bus test");
		check(strstr(panic_message,
		    "H16 T att=1 rc=0 same=1 div=0") != NULL,
		    "the H16 4-bit content pass diverged");
		check(strstr(panic_message,
		    "H16 V att=1 rc=0 sw=0 vf=1 c6at=1 ") != NULL &&
		    strstr(panic_message, "H16 Q att=1 rc=0 qb0=5c510100")
		    != NULL,
		    "the H16 verified revert or recovery read failed");
		check(cmd19_count == 0u && cmd14_count == 0u &&
		    cmd6_count == 2u,
		    "the H16 rung issued CMD19/CMD14 or missed a CMD6");
		check(cmd17_count == 51u,
		    "the H16 rung did not run both content passes plus Q");
	} else if (strcmp(scenario, "probe-h16-content-div") == 0) {
		check(panicked == 1, "the H16 divergent content pass did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H16 REPORT: rc=-21 stop=14") != NULL,
		    "the H16 content divergence did not keep stop 14");
		check(strstr(panic_message,
		    "H16 T att=1 rc=0 same=0 div=34") != NULL,
		    "the H16 content divergence did not name LBA 34");
		check(strstr(panic_message,
		    "H16 V att=1 rc=0 sw=0 vf=1 c6at=1 ") != NULL,
		    "the H16 divergence did not restore 1-bit framing");
		check(cmd19_count == 0u && cmd14_count == 0u,
		    "the H16 divergence issued CMD19/CMD14");
	} else if (strcmp(scenario, "probe-h17-pass") == 0) {
		check(panicked == 1, "the H17 bus-test rung did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H17 REPORT: rc=0 stop=0") != NULL,
		    "the H17 bus-test rung did not pass");
		check(strstr(panic_message,
		    "H17 T att=1 rc=0 same=1 div=0") != NULL,
		    "the H17 4-bit content pass diverged");
		check(strstr(panic_message,
		    "H17 B att=1 wrc=-11 rst=0 ci=0 trc=0 tw=000000a5 "
		    "te=000000a5") != NULL,
		    "the H17 rung did not tolerate the CMD19 write timeout "
		    "or lost the CMD14 pattern");
		check(h13_reset_write_count == 1u,
		    "the H17 rung did not issue exactly one data reset");
		check(strstr(panic_message,
		    "H17 V att=1 rc=0 sw=0 vf=1 c6at=1 ") != NULL &&
		    strstr(panic_message, "H17 Q att=1 rc=0 qb0=5c510100")
		    != NULL,
		    "the H17 verified revert or recovery read failed");
		check(cmd19_count == 1u && cmd14_count == 1u &&
		    cmd6_count == 2u,
		    "the H17 rung command counts mismatch");
		check(cmd17_count == 51u,
		    "the H17 rung did not run both content passes plus Q");
		check(strlen(panic_message) < 2048u,
		    "the H17 report exceeds the panic buffer");
	} else if (strcmp(scenario, "probe-h17-content-div") == 0) {
		check(panicked == 1, "the H17 divergent content pass did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H17 REPORT: rc=-21 stop=14") != NULL,
		    "the H17 content divergence did not keep stop 14");
		check(strstr(panic_message,
		    "H17 B att=0 wrc=0 rst=0 ci=0 trc=0") != NULL,
		    "the H17 divergent run issued a bus test");
		check(strstr(panic_message,
		    "H17 T att=1 rc=0 same=0 div=34") != NULL,
		    "the H17 content divergence did not name LBA 34");
		check(strstr(panic_message,
		    "H17 V att=1 rc=0 sw=0 vf=1 c6at=1 ") != NULL,
		    "the H17 divergence did not restore 1-bit framing");
		check(cmd19_count == 0u && cmd14_count == 0u,
		    "the H17 divergence issued CMD19/CMD14");
	} else if (strcmp(scenario, "probe-h17-pattern-mismatch") == 0) {
		check(panicked == 1, "the H17 pattern mismatch did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H17 REPORT: rc=-20 stop=13") != NULL,
		    "the H17 pattern mismatch did not keep stop 13");
		check(strstr(panic_message,
		    "H17 B att=1 wrc=-11 rst=0 ci=0 trc=-20 tw=0000005a "
		    "te=000000a5") != NULL,
		    "the H17 mismatch did not keep the tolerated write and "
		    "the observed word");
		check(strstr(panic_message,
		    "H17 V att=1 rc=0 sw=0 vf=1 c6at=1 ") != NULL,
		    "the H17 mismatch did not restore 1-bit framing");
		check(cmd19_count == 1u && cmd14_count == 1u,
		    "the H17 mismatch command counts mismatch");
	} else if (strcmp(scenario, "probe-h17-write-crc") == 0) {
		check(panicked == 1, "the H17 non-timeout write error did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H17 REPORT: rc=-12 stop=13") != NULL,
		    "a non-timeout CMD19 error was tolerated");
		check(strstr(panic_message,
		    "H17 B att=1 wrc=-12 rst=0 ci=0 trc=-12 ") != NULL,
		    "the H17 non-timeout write lost its error");
		check(h13_reset_write_count == 0u && cmd14_count == 0u,
		    "a non-timeout CMD19 error still reset the data circuit "
		    "or issued CMD14");
	} else if (strcmp(scenario, "probe-h17-inhibit") == 0) {
		check(panicked == 1, "the surviving-inhibit H17 run did not "
		    "report");
		check(strstr(panic_message,
		    "GTA4L-EMMC H17 REPORT: rc=-23 stop=15") != NULL,
		    "a surviving DAT inhibit did not keep stop 15");
		check(strstr(panic_message,
		    "H17 B att=1 wrc=-11 rst=-17 ci=0 trc=-17") != NULL,
		    "the H17 run did not name the failed data reset");
		check(strstr(panic_message,
		    "H17 V att=1 rc=-17 sw=0 vf=0 c6at=0 ") != NULL,
		    "the unproven H17 recovery did not report the surviving "
		    "inhibit before CMD6");
		check(cmd14_count == 0u,
		    "the H17 run issued CMD14 after a failed reset");
	} else if (strcmp(scenario, "probe-dto-fired") == 0) {
		check(panicked == 1, "the fired DTO audit did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC DTO REPORT: rc=-11 stop=0") != NULL,
		    "the fired row does not name the E1 timeout and stop 0");
		check(strstr(panic_message,
		    "DTO R1 clk=0007 ne=0033 ee=07ff ns=0000 se=0000 "
		    "es=0000 toc=0e arm=00 restored=0e") != NULL,
		    "the R1 line lost the timeout or enable readback");
		check(strstr(panic_message,
		    "DTO E1 n=8 rc=-11 "
		    "fail=1/18/2/0000123a/0010/0001/00000000/22") != NULL,
		    "the E1 line lost the latched data-timeout record");
		check(strstr(panic_message,
		    "DTO E1S mb=3 sr=0/1/1 dr=0/0/0/0") != NULL,
		    "the E1 line lost the block ordinal or the stop result");
		check(strstr(panic_message, "DTO Q rc=0") != NULL,
		    "the recovery read did not complete");
		check(strstr(panic_message,
		    "DTO E2 att=1 win=8000000 rc=-11 tt=1 tw=00000000 "
		    "te=00000000 trc=-11 hc=00") != NULL,
		    "the E2 line lost the repeated wedged write-phase result");
		check(strstr(panic_message,
		    "DTO FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/22")
		    != NULL,
		    "the terminal record lost the wedged CMD19 tuple");
		check(sdhci_read_count[GTA4L_EMMC_REG_ERROR_INT_STATUS] >=
		    700000u,
		    "the extended data-timeout window was not used");
		check(mb_stop_count == 1u,
		    "the E1 transfer was not stopped explicitly");
		check(cmd_count == 37u,
		    "the fired run did not issue the expected command count");
	} else if (strcmp(scenario, "probe-dto-silent") == 0) {
		check(panicked == 1, "the silent DTO audit did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC DTO REPORT: rc=-11 stop=10") != NULL,
		    "the silent row does not keep stop 10");
		check(strstr(panic_message,
		    "DTO E1 n=8 rc=0 "
		    "fail=0/0/0/00000000/0000/0000/00000000/0") != NULL,
		    "the silent E1 row invented a failure record");
		check(strstr(panic_message,
		    "DTO E1S mb=7 sr=0/1/1 dr=0/0/0/0") != NULL,
		    "the silent E1 row lost the completed ordinal or the stop");
		check(strstr(panic_message, "DTO Q rc=0") != NULL,
		    "the silent row lost the recovery read");
		check(strstr(panic_message,
		    "DTO E2 att=1 win=8000000 rc=-11 tt=1") != NULL,
		    "the silent row lost the repeated wedged write phase");
		check(strstr(panic_message,
		    "DTO FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/22")
		    != NULL,
		    "the silent row lost the terminal wedged CMD19 tuple");
		check(mb_stop_count == 1u,
		    "the silent E1 transfer was not stopped explicitly");
		check(cmd_count == 37u,
		    "the silent run did not issue the expected command count");
	} else if (strcmp(scenario, "probe-dto-q-fail") == 0) {
		check(panicked == 1, "the failed recovery read did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC DTO REPORT: rc=-12 stop=11") != NULL,
		    "the failed recovery row does not name the read error");
		check(strstr(panic_message,
		    "DTO E1 n=8 rc=-11 "
		    "fail=1/18/2/0000123a/0010/0001/00000000/22") != NULL,
		    "the failed recovery row lost the E1 data-timeout record");
		check(strstr(panic_message, "DTO Q rc=-12") != NULL,
		    "the failed recovery read was not recorded");
		check(strstr(panic_message,
		    "DTO E2 att=0 win=0 rc=0 tt=0 tw=00000000 te=00000000 "
		    "trc=0 hc=00") != NULL,
		    "the failed recovery row still ran the CMD19 experiment");
		check(strstr(panic_message,
		    "DTO FAIL fail=1/17/0/0000113a/0020/0000/00000000/22")
		    != NULL,
		    "the terminal record lost the recovery-read tuple");
		check(cmd_count == 36u,
		    "the failed recovery run did not stop before CMD19");
	} else if (strcmp(scenario, "probe-dto-e2-pass") == 0) {
		check(panicked == 1, "the passing E2 audit did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC DTO REPORT: rc=0 stop=13") != NULL,
		    "the passed write phase does not name its stop");
		check(strstr(panic_message,
		    "DTO E2 att=1 win=8000000 rc=0 tt=1 tw=00000040 "
		    "te=00000040 trc=0 hc=00") != NULL,
		    "the E2 line lost the passing 1-bit words");
		check(cmd_count == 38u,
		    "the passing E2 run did not complete CMD19/CMD14");
	} else if (strcmp(scenario, "probe-dto-arm-ro") == 0) {
		check(panicked == 1, "the arm-artifact audit did not report");
		check(strstr(panic_message,
		    "GTA4L-EMMC DTO REPORT: rc=0 stop=12") != NULL,
		    "a TIMEOUT_CONTROL readback that did not arm is not "
		    "stop 12");
		check(strstr(panic_message,
		    "DTO R1 clk=0007 ne=0033 ee=07ff ns=0000 se=0000 "
		    "es=0000 toc=0e arm=05 restored=0e") != NULL,
		    "the arm line did not record the stuck timeout byte and "
		    "its restore");
		check(strstr(panic_message,
		    "DTO E1 n=8 rc=0 "
		    "fail=0/0/0/00000000/0000/0000/00000000/0") != NULL,
		    "the arm artifact lost the silent E1 row");
		check(strstr(panic_message,
		    "DTO Q rc=0") != NULL,
		    "the arm artifact skipped the recovery read");
		check(strstr(panic_message,
		    "DTO E2 att=0 win=0 rc=0 tt=0") != NULL,
		    "the failed arm gate still ran E2");
		check(cmd_count == 36u,
		    "the arm artifact did not run the full DTO sequence");
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
		    "GTA4L-EMMC B1 BW=1 att=1 req=1 sw=15 swrc=0 tt=1 "
		    "tw=ffffffff te=ffffffff trc=0 hc=ff es=ffffffff "
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
	} else if (strcmp(scenario, "report-width-b2") == 0) {
		check(panicked == 1,
		    "the synthetic width B2 report did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 REPORT: rc=-5 stop=13 mci=ffffffff rca=1 "
		    "cap=4294967295 rev=255 hci=ff step=24 log=99999") != NULL,
		    "the synthetic width B2 report lost the widest header");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 R clk=ffff hs=ff es=ffffffff") != NULL,
		    "the synthetic width B2 report lost the 400 kHz state");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 S rc=-22 sw=-22 c6=ffffffff hs=ff "
		    "clk=ffff rcg=ffffffff sum=ffffffff") != NULL,
		    "the synthetic width B2 report lost the widest switch "
		    "fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 T rc=-21 att=1 same=0 div=4294967295 "
		    "b0=ffffffff b1=ffffffff data=ffffffff last=ffffffff")
		    != NULL,
		    "the synthetic width B2 report lost the widest content "
		    "fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 V rc=-5 sw=-22 hs=ff clk=ffff "
		    "rcg=ffffffff q=-17 qb0=ffffffff") != NULL,
		    "the synthetic width B2 report lost the failed revert and "
		    "recovery read");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 BLOCKS 2=ffffffff 3=ffffffff "
		    "4=ffffffff 5=ffffffff 6=ffffffff 7=ffffffff "
		    "8=ffffffff 9=ffffffff 10=ffffffff 11=ffffffff "
		    "12=ffffffff 13=ffffffff 14=ffffffff 15=ffffffff "
		    "16=ffffffff 17=ffffffff 18=ffffffff 19=ffffffff "
		    "20=ffffffff 21=ffffffff 22=ffffffff") != NULL,
		    "the synthetic width B2 report lost the content sums");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 PAD pre=ffffffff/ffffffff/ffffffff "
		    "post=ffffffff/ffffffff/ffffffff") != NULL,
		    "the synthetic width B2 report lost the before/after "
		    "pad/DLL state");
		check(strstr(panic_message,
		    "GTA4L-EMMC B2 FAIL fail=1/4294967295/4294967295/"
		    "ffffffff/ffff/ffff/ffffffff/24") != NULL,
		    "the synthetic width B2 report lost the widest failure "
		    "record");
		check(report_budget_accepts(strlen(panic_message)),
		    "the synthetic width B2 report does not fit the "
		    "2048-byte panic buffer including the NUL");
	} else if (strcmp(scenario, "report-width-h9") == 0) {
		check(panicked == 1, "the synthetic width H9 report did not "
		    "panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC H9 REPORT: rc=-18 stop=13 mci=ffffffff "
		    "rca=1 cap=4294967295 rev=255 bw=15 ocr=ffffffff "
		    "ext=ffffffff hci=ff retry=1/0 att=2 step=24 log=99999")
		    != NULL,
		    "the synthetic width H9 report lost the widest header");
		check(strstr(panic_message,
		    "GTA4L-EMMC H9 A1 n=1 rc=-18 r1=1 "
		    "fail=1/63/4294967295/0000ffff/ffff/ffff/ffffffff/24 "
		    "rsp=ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width H9 report lost the widest first "
		    "attempt");
		check(strstr(panic_message,
		    "GTA4L-EMMC H9 A1P c3p=1/ffff/ffff/ffffffff/ffff/ffff "
		    "c3t=1/63/ffff/ffffffff/1/"
		    "ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width H9 report lost the widest first "
		    "pre-command and timeout capture");
		check(strstr(panic_message,
		    "GTA4L-EMMC H9 A1L c3l=1/63/ffff/ffff/100000/"
		    "ffffffffffffffff/1000000000") != NULL,
		    "the synthetic width H9 report lost the widest first "
		    "poll history");
		check(strstr(panic_message,
		    "GTA4L-EMMC H9 A2 n=2 rc=-18 r1=1 "
		    "fail=1/63/4294967295/0000ffff/ffff/ffff/ffffffff/24 "
		    "rsp=ffffffff,ffffffff,ffffffff,ffffffff") != NULL,
		    "the synthetic width H9 report lost the widest second "
		    "attempt");
		check(strstr(panic_message,
		    "GTA4L-EMMC H9 C cid=ffffffff,ffffffff,ffffffff,ffffffff "
		    "cmd1=20/20") != NULL,
		    "the synthetic width H9 report lost the widest card data");
		check(strstr(panic_message,
		    "GTA4L-EMMC H9 FAIL fail=1/63/4294967295/0000ffff/"
		    "ffff/ffff/ffffffff/24") != NULL,
		    "the synthetic width H9 report lost the widest failure "
		    "record");
		check(report_budget_accepts(strlen(panic_message)),
		    "the synthetic width H9 report does not fit the "
		    "2048-byte panic buffer including the NUL");
	} else if (strcmp(scenario, "report-width-h10") == 0) {
		check(panicked == 1,
		    "the synthetic width H10 report did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 REPORT: rc=-18 stop=13 mci=ffffffff "
		    "rca=1 cap=4294967295 rev=255 hci=ff step=24 log=99999 "
		    "bw=0f") != NULL,
		    "the synthetic width H10 report lost the widest header");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 R clk=ffff hs=ff bw=ff hc=ff "
		    "es=ffffffff") != NULL,
		    "the synthetic width H10 report lost the 400 kHz "
		    "identity line");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 S rc=-22 sw=-22 c6=ffffffff hs=ff "
		    "clk=ffff rcg=ffffffff sum=ffffffff") != NULL,
		    "the synthetic width H10 report lost the widest HS "
		    "switch fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 W rc=-19 sw=255 swrc=-19 c6=ffffffff "
		    "hb=ff ha=ff es=ffffffff") != NULL,
		    "the synthetic width H10 report lost the widest width "
		    "switch fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 B rc=-20 tt=1 trc=-20 tw=ffffffff "
		    "te=ffffffff") != NULL,
		    "the synthetic width H10 report lost the widest bus-test "
		    "fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 T rc=-12 att=1 same=0 div=4294967295 "
		    "b0=ffffffff b1=ffffffff data=ffffffff last=ffffffff")
		    != NULL,
		    "the synthetic width H10 report lost the widest content "
		    "fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 V rc=-19 sw=255 swrc=-19 c6=ffffffff "
		    "hb=ff ha=ff es=ffffffff") != NULL,
		    "the synthetic width H10 report lost the widest width "
		    "revert fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 X rc=-22 sw=-22 c6=ffffffff hs=ff "
		    "clk=ffff rcg=ffffffff sum=ffffffff") != NULL,
		    "the synthetic width H10 report lost the widest HS "
		    "revert fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 Q rc=-12 qb0=ffffffff") != NULL,
		    "the synthetic width H10 report lost the recovery read");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 BLOCKS 2=ffffffff 3=ffffffff "
		    "4=ffffffff 5=ffffffff 6=ffffffff 7=ffffffff "
		    "8=ffffffff 9=ffffffff 10=ffffffff 11=ffffffff "
		    "12=ffffffff 13=ffffffff 14=ffffffff 15=ffffffff "
		    "16=ffffffff 17=ffffffff 18=ffffffff 19=ffffffff "
		    "20=ffffffff 21=ffffffff 22=ffffffff") != NULL,
		    "the synthetic width H10 report lost the content sums");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 PAD pre=ffffffff/ffffffff/ffffffff "
		    "post=ffffffff/ffffffff/ffffffff") != NULL,
		    "the synthetic width H10 report lost the before/after "
		    "pad/DLL state");
		check(strstr(panic_message,
		    "GTA4L-EMMC H10 FAIL fail=1/4294967295/4294967295/"
		    "ffffffff/ffff/ffff/ffffffff/24") != NULL,
		    "the synthetic width H10 report lost the widest failure "
		    "record");
		check(report_budget_accepts(strlen(panic_message)),
		    "the synthetic width H10 report does not fit the "
		    "2048-byte panic buffer including the NUL");
	} else if (strcmp(scenario, "report-width-h12") == 0) {
		check(panicked == 1,
		    "the synthetic width H12 report did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 REPORT: rc=-18 stop=13 mci=ffffffff "
		    "rca=1 cap=4294967295 rev=255 hci=ff step=24 log=99999 "
		    "bw=0f") != NULL,
		    "the synthetic width H12 report lost the widest header");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 R clk=ffff hs=ff bw=ff hc=ff "
		    "es=ffffffff") != NULL,
		    "the synthetic width H12 report lost the 400 kHz "
		    "identity line");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 S rc=-22 sw=-22 c6=ffffffff hs=ff "
		    "clk=ffff rcg=ffffffff sum=ffffffff") != NULL,
		    "the synthetic width H12 report lost the widest HS "
		    "switch fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 WDDR rc=-19 sw=255 swrc=-19 c6=ffffffff "
		    "es=ffffffff") != NULL,
		    "the synthetic width H12 report lost the widest DDR "
		    "switch fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 VR rc=-19 sw=255 swrc=-19 c6=ffffffff "
		    "hb=ff ha=ff es=ffffffff") != NULL,
		    "the synthetic width H12 report lost the widest width "
		    "revert fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 X rc=-22 sw=-22 c6=ffffffff hs=ff "
		    "clk=ffff rcg=ffffffff sum=ffffffff") != NULL,
		    "the synthetic width H12 report lost the widest HS "
		    "revert fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 Q rc=-12 qb0=ffffffff") != NULL,
		    "the synthetic width H12 report lost the recovery read");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 PAD pre=ffffffff/ffffffff/ffffffff "
		    "post=ffffffff/ffffffff/ffffffff") != NULL,
		    "the synthetic width H12 report lost the before/after "
		    "pad/DLL state");
		check(strstr(panic_message,
		    "GTA4L-EMMC H12 FAIL fail=1/4294967295/4294967295/"
		    "ffffffff/ffff/ffff/ffffffff/24") != NULL,
		    "the synthetic width H12 report lost the widest failure "
		    "record");
		check(report_budget_accepts(strlen(panic_message)),
		    "the synthetic width H12 report does not fit the "
		    "2048-byte panic buffer including the NUL");
	} else if (strcmp(scenario, "report-width-h13") == 0) {
		check(panicked == 1,
		    "the synthetic width H13 report did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 REPORT: rc=-18 stop=13 mci=ffffffff "
		    "rca=1 cap=4294967295 rev=255 hci=ff step=24 log=99999 "
		    "bw=0f") != NULL,
		    "the synthetic width H13 report lost the widest header");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 R clk=ffff hc=ff bw=ff es=ffffffff")
		    != NULL,
		    "the synthetic width H13 report lost the 400 kHz "
		    "identity line");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 W wrc=-18 err=ffff nrm=ffff pst=ffffffff")
		    != NULL,
		    "the synthetic width H13 report lost the widest "
		    "write-phase fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 RST rc=-4 wr=ff rb=ff it=4294967295 "
		    "pst=ffffffff dinh=4294967295") != NULL,
		    "the synthetic width H13 report lost the widest reset "
		    "fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 T trc=-20 tw=ffffffff te=ffffffff")
		    != NULL,
		    "the synthetic width H13 report lost the widest CMD14 "
		    "fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 Q rc=-12 qb0=ffffffff") != NULL,
		    "the synthetic width H13 report lost the recovery read");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 PAD pre=ffffffff/ffffffff/ffffffff "
		    "post=ffffffff/ffffffff/ffffffff") != NULL,
		    "the synthetic width H13 report lost the before/after "
		    "pad/DLL state");
		check(strstr(panic_message,
		    "GTA4L-EMMC H13 FAIL fail=1/4294967295/4294967295/"
		    "ffffffff/ffff/ffff/ffffffff/24") != NULL,
		    "the synthetic width H13 report lost the widest failure "
		    "record");
		check(report_budget_accepts(strlen(panic_message)),
		    "the synthetic width H13 report does not fit the "
		    "2048-byte panic buffer including the NUL");
	} else if (strcmp(scenario, "report-width-h14") == 0) {
		check(panicked == 1,
		    "the synthetic width H14 report did not panic");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 REPORT: rc=-18 stop=13 mci=ffffffff "
		    "rca=1 cap=4294967295 rev=255 hci=ff step=24 log=99999 "
		    "bw=0f") != NULL,
		    "the synthetic width H14 report lost the widest header");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 R clk=ffff hc=ff bw=ff es=ffffffff")
		    != NULL,
		    "the synthetic width H14 report lost the 400 kHz "
		    "identity line");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 W wrc=-18 err=ffff nrm=ffff pst=ffffffff")
		    != NULL,
		    "the synthetic width H14 report lost the widest "
		    "write-phase fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 RST rc=-4 wr=ff rb=ff it=4294967295 "
		    "pst=ffffffff dinh=4294967295") != NULL,
		    "the synthetic width H14 report lost the widest reset "
		    "fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 C12 rc=-18 r1=ffffffff err=ffff "
		    "nrm=ffff pst=ffffffff") != NULL,
		    "the synthetic width H14 report lost the widest CMD12 "
		    "fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 C13 at=1 rc=-18 r1=ffffffff err=ffff "
		    "nrm=ffff pst=ffffffff") != NULL,
		    "the synthetic width H14 report lost the widest CMD13 "
		    "fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 T trc=-20 tw=ffffffff te=ffffffff")
		    != NULL,
		    "the synthetic width H14 report lost the widest CMD14 "
		    "fields");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 Q rc=-12 qb0=ffffffff") != NULL,
		    "the synthetic width H14 report lost the recovery read");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 PAD pre=ffffffff/ffffffff/ffffffff "
		    "post=ffffffff/ffffffff/ffffffff") != NULL,
		    "the synthetic width H14 report lost the before/after "
		    "pad/DLL state");
		check(strstr(panic_message,
		    "GTA4L-EMMC H14 FAIL fail=1/4294967295/4294967295/"
		    "ffffffff/ffff/ffff/ffffffff/24") != NULL,
		    "the synthetic width H14 report lost the widest failure "
		    "record");
		check(report_budget_accepts(strlen(panic_message)),
		    "the synthetic width H14 report does not fit the "
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

	if ((strncmp(scenario, "probe-b1c-", 10u) == 0) ||
	    (strncmp(scenario, "probe-b1-1bit-", 15u) == 0) ||
	    (strncmp(scenario, "probe-dto-", 10u) == 0) ||
	    (strncmp(scenario, "probe-b2-", 9u) == 0) ||
	    (strncmp(scenario, "probe-h9-", 9u) == 0) ||
	    (strncmp(scenario, "probe-h11-", 10u) == 0) ||
	    (strncmp(scenario, "probe-h12-", 10u) == 0) ||
	    (strncmp(scenario, "probe-h13-", 10u) == 0)) {
		check(bad_command_format == 0,
		    "a B1C command used the wrong flags");
		check(bad_command_order == 0,
		    "the B1C run sent commands in the wrong order");
		check(bad_command_argument == 0,
		    "the B1C content pass read the wrong LBA list");
	}
	if ((strncmp(scenario, "probe-", 6u) == 0) &&
	    (strncmp(scenario, "probe-h12-", 10u) != 0)) {
		check(cmd6_width_ddr == 0,
		    "a stage 1..11 rung issued a byte-183 DDR variant");
	}
	if (strncmp(scenario, "probe-h11-", 10u) == 0) {
		if ((strcmp(scenario, "probe-h11-width-revert-fail") != 0) &&
		    (strcmp(scenario, "probe-h11-bustest-timeout") != 0) &&
		    (strcmp(scenario, "probe-h11-bustest-mismatch") != 0)) {
			check(sim_host_width_code() ==
			    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1,
			    "the H11 run did not restore the 1-bit host "
			    "width");
		}
	}
	if (strncmp(scenario, "probe-h12-", 10u) == 0) {
		check(h12_rung_write_count[GTA4L_EMMC_REG_CORE_DDR_CONFIG] ==
		    0u,
		    "the H12 rung wrote CORE_DDR_CONFIG");
		check(h12_rung_write_count[GTA4L_EMMC_REG_HOST_CONTROL2] ==
		    0u,
		    "the H12 rung wrote HOST_CONTROL2");
		if ((strcmp(scenario, "probe-h12-hs-ignored") == 0) ||
		    (strcmp(scenario, "probe-h12-clock-busy") == 0) ||
		    (strcmp(scenario, "probe-h12-pre-read-fail") == 0)) {
			check(cmd6_value4_count == 0u,
			    "the H12 early failure issued a byte-183 DDR "
			    "attempt");
		} else {
			check(cmd6_value4_count == 1u,
			    "the H12 rung did not issue exactly one "
			    "byte-183 value-4 attempt");
		}
	}
	if (strncmp(scenario, "probe-h13-", 10u) == 0) {
		check(cmd6_count == 0u,
		    "the H13 rung issued a CMD6");
		check(cmd6_width_ddr == 0,
		    "the H13 rung issued a byte-183 DDR variant");
		check(h13_reset_write_count == 1u,
		    "the H13 rung did not issue exactly one data reset");
	}
	if (strncmp(scenario, "probe-h14-", 10u) == 0) {
		check(cmd6_count == 0u,
		    "the H14 rung issued a CMD6");
		check(cmd6_width_ddr == 0,
		    "the H14 rung issued a byte-183 DDR variant");
		check(h13_reset_write_count == 1u,
		    "the H14 rung did not issue exactly one data reset");
		check(auto_cmd12_seen == 0,
		    "the H14 drain left AUTO_CMD12 enabled");
	}
	if (strncmp(scenario, "probe-dto-", 10u) == 0) {
		check(bad_mb_command == 0,
		    "the DTO run used the wrong CMD18/CMD12 form");
		check(auto_cmd12_seen == 0,
		    "the DTO run left AUTO_CMD12 enabled");
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
