/*
 * Host-side simulated controller and eMMC card for the gta4l SDHCI v5 core.
 *
 * The production core in package/Gta4lEmmc.c is compiled unmodified against
 * the callbacks below. The simulator models the register semantics the core
 * depends on and records the writes so the tests can reject a wrong clock,
 * power, pad or command sequence. It does not execute firmware or touch a
 * device.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Gta4lEmmc.h"

#define SIM_SDHCI_SIZE 0x1000u
#define SIM_GCC_WORDS  0x40u
#define SIM_TLMM_WORDS 0x04u
#define SIM_MAX_LOG    128u
#define SIM_MAX_RESPONSE_READS 64u
#define SIM_MB_TEST_BLOCKS 21u

/* Independent copies of the SDHCI semantics the simulator enforces. These
 * are deliberately not taken from the production header, so a mutated header
 * cannot change what the simulator accepts. */
#define SIM_CMD_DATA            0x0020u
#define SIM_CMD_CRC             0x0008u
#define SIM_CMD_INDEX           0x0010u
#define SIM_CMD_CRC_NONE        0xFFu
#define SIM_TM_BLOCK_COUNT_EN   0x0002u
#define SIM_TM_READ             0x0010u
#define SIM_TM_MULTI            0x0020u
#define SIM_TM_AUTO_CMD12       0x0004u
#define SIM_CTRL_VDD_180        0x0008u
#define SIM_PAD_PWR_SWITCH_EN   0x00008000u
#define SIM_PAD_PWR_SWITCH      0x00010000u
#define SIM_DLL_RST             0x40000000u
#define SIM_DLL_PDN             0x20000000u
#define SIM_VENDOR_MCLK_SEL_MASK   0x00000300u
#define SIM_VENDOR_MCLK_SEL_DFLT   0x00000200u
#define SIM_VENDOR_MCLK_SEL_HS400  0x00000300u
#define SIM_VENDOR_HC_SELECT_IN_EN 0x00040000u
#define SIM_VENDOR_HC_SELECT_IN_MASK 0x00380000u
#define SIM_VENDOR_CLK_PWRSAVE     0x00000002u
#define SIM_VENDOR_SPEC_POR        0x00000A9Cu
#define SIM_VENDOR3_PWRSAVE_DLL    0x00000008u
#define SIM_VENDOR_INITIAL         0x00318A0Cu
#define SIM_POWER_SETTLE_US     10000u
#define SIM_CARD_CLOCK_SETTLE_US 1000u
#define SIM_CMD0_DELAY_US       1000u
#define SIM_CMD0_POST_DELAY_US  2000u

/* Independent GCC SDCC1 offsets and reset/branch/RCG semantics. */
#define SIM_GCC_BCR             0x00u
#define SIM_GCC_APPS_CBCR       0x04u
#define SIM_GCC_AHB_CBCR        0x08u
#define SIM_GCC_ICE_CBCR        0x0Cu
#define SIM_GCC_APPS_CMD        0x28u
#define SIM_GCC_APPS_CFG        0x2Cu
#define SIM_GCC_APPS_M          0x30u
#define SIM_GCC_APPS_N          0x34u
#define SIM_GCC_APPS_D          0x38u
#define SIM_BCR_BLOCK_RESET     0x00000001u
#define SIM_BCR_DELAY_US        200u
#define SIM_CBCR_CLOCK_ENABLE   0x00000001u
#define SIM_CBCR_CLK_OFF        0x80000000u
#define SIM_RCG_CMD_UPDATE      0x00000001u

struct sim {
	uint8_t  sdhci[SIM_SDHCI_SIZE];
	uint32_t gcc[SIM_GCC_WORDS];
	uint32_t tlmm[SIM_TLMM_WORDS];
	uint16_t normal_int;
	uint16_t error_int;
	uint16_t normal_enable;
	uint16_t error_enable;
	uint16_t normal_signal;
	uint16_t error_signal;
	uint16_t block_size;
	uint16_t block_count;
	uint16_t transfer_mode;
	uint32_t pwr_status;
	uint32_t power_ack;
	uint32_t pwr_poll_count;
	uint32_t io_poll_count;
	uint32_t last_clear;
	uint32_t churn_count;
	uint32_t buffer_data[GTA4L_EMMC_BLOCK_BYTES / 4u];
	uint32_t buffer_index;
	uint32_t last_cmd;
	uint32_t last_cmd_argument;
	uint32_t cmd_count;
	uint32_t cmd3_argument;
	uint32_t rca;
	uint32_t sector_count;
	uint32_t partition_config;
	uint8_t  ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t cid[4];
	uint32_t csd[4];
	uint64_t time_us;
	uint64_t card_clock_time_us;
	uint64_t power_ack_time_us;
	uint64_t cmd0_time_us;
	uint64_t cmd1_time_us;
	uint64_t cmd1_ready_time_us;
	uint64_t cmd1_attempt_time_us[4];
	uint64_t cmd2_time_us;
	uint32_t cmd1_attempt_count;
	int cmd1_seen;
	uint32_t log_event[SIM_MAX_LOG];
	uint32_t log_a[SIM_MAX_LOG];
	uint32_t log_b[SIM_MAX_LOG];
	uint32_t log_count;

	int reset_timeout;
	int clock_timeout;
	int power_off;
	int rcg_busy;
	int cmd_timeout;
	uint32_t cmd_crc_index;
	int cmd_crc_with_cc;
	int clear_response_on_w1c;
	int clear_response_on_normal_w1c;
	int preserve_host_control2;
	int cmd_index;
	int data_timeout;
	int data_crc;
	int no_tc;
	int no_brr;
	int card_never_ready;
	int bad_cid;
	int bad_csd;
	int cmd3_error;
	int power_no_request;
	int power_stuck;
	int power_io_only;
	int power_delayed_bus;
	int power_io_delayed;
	int power_wrong_io;
	int power_io_wrong_then_correct;
	int power_late_bus_only;
	int power_io_then_wrong;
	int power_opposite_io;
	int power_io_on_ack;
	int ack_io_injected;
	int power_churn;
	int bus_acked;
	int bus_delayed_emitted;
	int io_delayed_emitted;
	int data_sector_4k;
	int data_sector_size_code;
	int data_late_error_words;
	int data_late_error_emitted;
	/* Multi-block CMD18/CMD12 model. The data phase runs until the
	 * configured block count has been drained, then the stop is required
	 * to release the modeled DAT line. */
	int mb_active;
	int mb_dat_inhibit;
	int mb_done;
	int mb_no_cc;
	int mb_stop_no_tc;
	int mb_late_cc;
	int mb_stop_count;
	int auto_cmd12_seen;
	int mb_response_error;
	uint32_t mb_count;
	uint32_t mb_argument;
	uint32_t mb_issued_block_size;
	uint32_t mb_issued_block_count;
	uint32_t mb_stop_normal_reads;
	/* Delayed CMD18 events for the post-bound drain model. Delays are
	 * microseconds from the event's scheduling point (the issue for an
	 * error, the penultimate drain for the last BRR, the final drain for
	 * TC); -1 keeps the immediate model. mb_no_tc withholds completion so
	 * the drain window can be exhausted. */
	int mb_tc_delay_us;
	int mb_last_brr_delay_us;
	int mb_error_delay_us;
	int mb_error_status;
	int mb_no_tc;
	int mb_tc_pending;
	int mb_last_brr_pending;
	int mb_error_pending;
	uint64_t mb_tc_due_us;
	uint64_t mb_last_brr_due_us;
	uint64_t mb_error_due_us;
	uint32_t cmd_late_error_index;
	int cmd_late_error_emitted;
	int power_written;
	int card_clock_before_power;
	int card_clock_enabled;
	int reset_written;
	int bcr_reset;
	int branch_writes;
	int apps_branch_fail;
	int ahb_branch_fail;
	int trace_armed;
	int bcr_asserted;
	int bcr_deasserted;
	int bcr_assert_pending;
	int bcr_reasserted;
	int bcr_short_pulse;
	int sdhci_before_bcr_deassert;
	int sdhci_before_clocks;
	int apps_branch_enabled;
	int ahb_branch_enabled;
	int dll_rst_written;
	int dll_pdn_written;
	int dll_pdn_before_rst;
	uint64_t bcr_assert_time_us;
	uint64_t bcr_pulse_us;
	uint32_t cmd1_timeouts_remaining;
	uint32_t cmd1_not_ready_remaining;
	/* Hypothetical timing model: after a ready CMD1 the controller's
	 * internal CRC source remains busy for cmd1_crc_retire_us; a CMD2
	 * inside that window latches CMD_CRC without loading a CID. This is
	 * an explicit experiment model, not hardware confirmation. The
	 * pre-CMD2 status clears do not reset the modeled timer. */
	int cmd1_crc_retire_us;
	int cmd1_crc_retire_pending;
	int cmd2_keep_response;
	int cmd2_ocr_like_response;
	int cmd2_normalized_layout;
	int cmd3_no_cc;
	int cmd3_late_cc;
	int cmd3_stale_cc;
	int cmd3_identify_status;
	int enable_readback_wrong;
	uint32_t cmd3_normal_reads;
	/* CMD7 R1b modeling. Delays are microseconds from command issue; -1
	 * means the event never occurs. Events are applied from modeled time,
	 * not from status-read counts. */
	int cmd7_cc_immediate;
	int cmd7_tc_immediate;
	int cmd7_cc_delay;
	int cmd7_tc_delay;
	int cmd7_error_delay;
	int cmd7_error_status;
	int cmd7_keep_response;
	int cmd7_fresh_response;
	int cmd7_toggle_status;
	int cmd7_script;
	const uint32_t *cmd7_script_values;
	uint32_t cmd7_script_length;
	uint32_t cmd7_script_index;
	int cmd7_tc_raised;
	uint32_t cmd7_normal_reads;
	uint32_t cmd7_error_reads;
	uint32_t cmd7_window[4];
	uint64_t cmd7_issue_time_us;
	uint64_t cmd7_cc_time_us;
	uint64_t cmd7_tc_time_us;
	uint64_t cmd7_error_time_us;
	int cmd7_cc_time_valid;
	int cmd7_tc_time_valid;
	int cmd7_error_time_valid;
	int cmd7_cc_due;
	int cmd7_tc_due;
	int cmd7_error_due;
	int cmd7_cc_applied;
	int cmd7_busy;
	int cmd7_cc_observed;
	int cmd7_tc_observed;
	int cmd7_error_observed;
	int cmd7_early_command;
	int cmd7_access_latency_us;
	int cmd7_stale_status;
	int cmd7_stale_injected;
	uint64_t cmd7_release_time_us;
	uint64_t cmd7_next_issue_time_us;
	int cmd7_next_index;
	int cmd7_command_readback;
	int response_read_before_cc;
	int irrelevant_tc_valid;
	uint32_t irrelevant_tc_index;
	uint32_t cmd9_normal_clear_count;
	uint32_t cmd2_normal_clear_count;
	uint32_t error_clear_count;
	uint32_t cmd_clear_count;
	uint32_t cmd_issue_clear[64];
	uint32_t response_read_count;
	uint32_t response_read_cmd[SIM_MAX_RESPONSE_READS];
	uint32_t response_read_clear[SIM_MAX_RESPONSE_READS];
	uint32_t response_read_offset[SIM_MAX_RESPONSE_READS];
	int access_violation;
	int command_error;
};

static int
sim_sdhci_width_ok(uint32_t offset, uint32_t width)
{
	switch (offset) {
	case 0x28u: /* HOST_CONTROL */
	case 0x29u: /* POWER_CONTROL */
	case 0x2Eu: /* TIMEOUT_CONTROL */
	case 0x2Fu: /* SOFTWARE_RESET */
		return width == 1u;
	case 0x04u: /* BLOCK_SIZE */
	case 0x06u: /* BLOCK_COUNT */
	case 0x0Cu: /* TRANSFER_MODE */
	case 0x0Eu: /* COMMAND */
	case 0x2Cu: /* CLOCK_CONTROL */
	case 0x30u: /* NORMAL_INT_STATUS */
	case 0x32u: /* ERROR_INT_STATUS */
	case 0x34u: /* NORMAL_INT_ENABLE */
	case 0x36u: /* ERROR_INT_ENABLE */
	case 0x38u: /* NORMAL_INT_SIGNAL */
	case 0x3Au: /* ERROR_INT_SIGNAL */
	case 0x3Eu: /* HOST_CONTROL2 */
	case 0xFEu: /* HOST_VERSION */
		return width == 2u;
	case 0x08u: /* ARGUMENT */
	case 0x10u: /* RESPONSE */
	case 0x14u:
	case 0x18u:
	case 0x1Cu:
	case 0x20u: /* BUFFER_DATA_PORT */
	case 0x24u: /* PRESENT_STATE */
	case 0x40u: /* CAPABILITIES */
	case 0x200u: /* CORE_DLL_CONFIG */
	case 0x208u: /* CORE_DLL_STATUS */
	case 0x20Cu: /* CORE_VENDOR_SPEC */
	case 0x250u: /* CORE_VENDOR_SPEC3 */
	case 0x240u: /* CORE_PWRCTL_STATUS */
	case 0x244u: /* CORE_PWRCTL_MASK */
	case 0x248u: /* CORE_PWRCTL_CLEAR */
	case 0x24Cu: /* CORE_PWRCTL_CTL */
	case 0x318u: /* MCI_VERSION */
		return width == 4u;
	default:
		return 0;
	}
}

static int
sim_region_ok(uint32_t offset, uint32_t width, uint32_t size)
{
	return ((offset % width) == 0u) && ((offset + width) <= size);
}

static void
sim_note_sdhci_access(struct sim *sim)
{
	if (sim->trace_armed == 0) {
		return;
	}
	if (sim->bcr_deasserted == 0) {
		sim->sdhci_before_bcr_deassert = 1;
	}
	if ((sim->apps_branch_enabled == 0) ||
	    (sim->ahb_branch_enabled == 0)) {
		sim->sdhci_before_clocks = 1;
	}
}

static void
sim_apply_cmd7_events(struct sim *sim)
{
	if (sim->last_cmd != GTA4L_EMMC_CMD7) {
		return;
	}
	if ((sim->cmd7_cc_due == 0) && (sim->cmd7_cc_time_valid != 0) &&
	    (sim->time_us >= sim->cmd7_cc_time_us)) {
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		sim->cmd7_cc_due = 1;
		sim->cmd7_cc_applied = 1;
	}
	if ((sim->cmd7_tc_due == 0) && (sim->cmd7_tc_time_valid != 0) &&
	    (sim->time_us >= sim->cmd7_tc_time_us)) {
		sim->normal_int |= GTA4L_EMMC_INT_TC;
		sim->cmd7_tc_due = 1;
		sim->cmd7_tc_raised = 1;
		sim->cmd7_release_time_us = sim->time_us;
		sim->cmd7_busy = 0;
	}
	if ((sim->cmd7_error_due == 0) && (sim->cmd7_error_time_valid != 0) &&
	    (sim->time_us >= sim->cmd7_error_time_us)) {
		sim->error_int |= (uint16_t)sim->cmd7_error_status;
		sim->cmd7_error_due = 1;
	}
}

static void sim_apply_mb_events(struct sim *sim);

static uint8_t
sim_read8(void *context, uint32_t space, uint32_t offset)
{
	struct sim *sim = (struct sim *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sim_note_sdhci_access(sim);
		if (!sim_sdhci_width_ok(offset, 1u)) {
			sim->access_violation = 1;
			return 0;
		}
		return sim->sdhci[offset];
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		if (!sim_region_ok(offset, 1u, SIM_GCC_WORDS * 4u)) {
			sim->access_violation = 1;
			return 0;
		}
		return (uint8_t)(sim->gcc[offset / 4u] >> ((offset & 3u) * 8u));
	}
	if (space == GTA4L_EMMC_SPACE_TLMM) {
		if (!sim_region_ok(offset, 1u, SIM_TLMM_WORDS * 4u)) {
			sim->access_violation = 1;
			return 0;
		}
		return (uint8_t)(sim->tlmm[offset / 4u] >> ((offset & 3u) * 8u));
	}
	return 0;
}

static uint16_t
sim_read16(void *context, uint32_t space, uint32_t offset)
{
	struct sim *sim = (struct sim *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sim_note_sdhci_access(sim);
		if (!sim_sdhci_width_ok(offset, 2u)) {
			sim->access_violation = 1;
			return 0;
		}
		if (offset == GTA4L_EMMC_REG_NORMAL_INT_STATUS) {
			uint16_t value;

			sim_apply_mb_events(sim);
			if ((sim->cmd3_late_cc != 0) &&
			    (sim->last_cmd == GTA4L_EMMC_CMD3)) {
				sim->cmd3_normal_reads++;
				if (sim->cmd3_normal_reads >
				    (GTA4L_EMMC_CMD_TIMEOUT_US /
				    GTA4L_EMMC_POLL_STEP_US)) {
					sim->normal_int |= GTA4L_EMMC_INT_CC;
				}
			}
			if ((sim->mb_late_cc != 0) &&
			    (sim->last_cmd == GTA4L_EMMC_CMD12)) {
				sim->mb_stop_normal_reads++;
				if (sim->mb_stop_normal_reads >
				    (GTA4L_EMMC_CMD_TIMEOUT_US /
				    GTA4L_EMMC_POLL_STEP_US)) {
					sim->normal_int |= GTA4L_EMMC_INT_CC;
				}
			}
			if (sim->last_cmd == GTA4L_EMMC_CMD7) {
				sim->cmd7_normal_reads++;
				sim_apply_cmd7_events(sim);
				if (sim->cmd7_toggle_status != 0) {
					sim->normal_int ^=
					    GTA4L_EMMC_INT_BRR;
				}
				if ((sim->cmd7_script != 0) &&
				    (sim->cmd7_script_length != 0u)) {
					uint32_t sample;

					if (sim->cmd7_script_index <
					    sim->cmd7_script_length) {
						sample =
						    sim->cmd7_script_values[
						    sim->cmd7_script_index];
					} else {
						sample =
						    sim->cmd7_script_values[
						    sim->cmd7_script_length -
						    1u];
					}
					if ((sample & 1u) != 0) {
						sim->normal_int |=
						    GTA4L_EMMC_INT_BRR;
					} else {
						sim->normal_int &=
						    (uint16_t)
						    ~GTA4L_EMMC_INT_BRR;
					}
					sim->cmd7_script_index++;
				}
			}
			value = sim->normal_int & sim->normal_enable;
			if ((sim->last_cmd == GTA4L_EMMC_CMD7) &&
			    ((value & GTA4L_EMMC_INT_CC) != 0)) {
				sim->cmd7_cc_observed = 1;
			}
			if ((sim->last_cmd == GTA4L_EMMC_CMD7) &&
			    ((value & GTA4L_EMMC_INT_TC) != 0)) {
				sim->cmd7_tc_observed = 1;
			}
			return value;
		}
		if (offset == GTA4L_EMMC_REG_ERROR_INT_STATUS) {
			uint16_t value;

			sim_apply_mb_events(sim);
			if (sim->last_cmd == GTA4L_EMMC_CMD7) {
				sim->cmd7_error_reads++;
				sim_apply_cmd7_events(sim);
			}
			value = sim->error_int & sim->error_enable;
			if ((sim->last_cmd == GTA4L_EMMC_CMD7) &&
			    ((value & GTA4L_EMMC_INT_ERR_MASK) != 0)) {
				sim->cmd7_error_observed = 1;
			}
			return value;
		}
		if (offset == GTA4L_EMMC_REG_NORMAL_INT_ENABLE) {
			return sim->enable_readback_wrong ?
			    0u : sim->normal_enable;
		}
		if (offset == GTA4L_EMMC_REG_ERROR_INT_ENABLE) {
			return sim->enable_readback_wrong ?
			    0u : sim->error_enable;
		}
		if ((offset == GTA4L_EMMC_REG_COMMAND) &&
		    (sim->last_cmd == GTA4L_EMMC_CMD7)) {
			/* Terminal captures read the command readback before
			 * the raw response window; mark them so the
			 * before-CC check covers only in-loop snapshots. */
			sim->cmd7_command_readback = 1;
		}
		return (uint16_t)(sim->sdhci[offset] |
		    ((uint16_t)sim->sdhci[offset + 1u] << 8));
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		if (!sim_region_ok(offset, 2u, SIM_GCC_WORDS * 4u)) {
			sim->access_violation = 1;
			return 0;
		}
		return (uint16_t)(sim->gcc[offset / 4u] >> ((offset & 3u) * 8u));
	}
	if (space == GTA4L_EMMC_SPACE_TLMM) {
		if (!sim_region_ok(offset, 2u, SIM_TLMM_WORDS * 4u)) {
			sim->access_violation = 1;
			return 0;
		}
		return (uint16_t)(sim->tlmm[offset / 4u] >> ((offset & 3u) * 8u));
	}
	return 0;
}

static void
sim_fill_block(struct sim *sim, uint32_t lba);

/* Delayed CMD18 events for the post-bound drain scenarios. A scheduled error
 * wins over pending data and completion events. */
static void
sim_apply_mb_events(struct sim *sim)
{
	if (sim->last_cmd != GTA4L_EMMC_CMD18) {
		return;
	}
	if ((sim->mb_error_pending != 0) &&
	    (sim->time_us >= sim->mb_error_due_us)) {
		sim->mb_error_pending = 0;
		sim->mb_tc_pending = 0;
		sim->mb_last_brr_pending = 0;
		sim->mb_active = 0;
		sim->normal_int &= (uint16_t)~GTA4L_EMMC_INT_BRR;
		sim->error_int |= (uint16_t)sim->mb_error_status;
		return;
	}
	if ((sim->mb_last_brr_pending != 0) &&
	    (sim->time_us >= sim->mb_last_brr_due_us)) {
		sim->mb_last_brr_pending = 0;
		sim_fill_block(sim, sim->mb_argument +
		    (uint32_t)sim->mb_done);
		sim->normal_int |= GTA4L_EMMC_INT_BRR;
	}
	if ((sim->mb_tc_pending != 0) &&
	    (sim->time_us >= sim->mb_tc_due_us)) {
		sim->mb_tc_pending = 0;
		sim->normal_int |= GTA4L_EMMC_INT_TC;
	}
}

static uint32_t
sim_read32(void *context, uint32_t space, uint32_t offset)
{
	struct sim *sim = (struct sim *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sim_note_sdhci_access(sim);
		if (!sim_sdhci_width_ok(offset, 4u)) {
			sim->access_violation = 1;
			return 0;
		}
		sim_apply_cmd7_events(sim);
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_STATUS) {
			if (((sim->power_delayed_bus != 0) ||
			    (sim->power_late_bus_only != 0)) &&
			    (sim->power_written != 0) &&
			    (sim->bus_delayed_emitted == 0)) {
				sim->pwr_poll_count++;
				if (sim->pwr_poll_count >= 5u) {
					sim->pwr_status |=
					    GTA4L_EMMC_PWRCTL_BUS_ON;
					if (sim->power_delayed_bus != 0) {
						sim->pwr_status |=
						    GTA4L_EMMC_PWRCTL_IO_LOW;
					}
					sim->bus_delayed_emitted = 1;
				}
			}
			if (((sim->power_io_delayed != 0) ||
			    (sim->power_io_wrong_then_correct != 0) ||
			    (sim->power_io_then_wrong != 0)) &&
			    (sim->bus_acked != 0) &&
			    (sim->io_delayed_emitted == 0)) {
				sim->io_poll_count++;
				if (sim->io_poll_count >=
				    ((sim->power_io_then_wrong != 0) ? 1u : 5u)) {
					sim->pwr_status |=
					    (sim->power_io_then_wrong != 0) ?
					    GTA4L_EMMC_PWRCTL_IO_HIGH :
					    GTA4L_EMMC_PWRCTL_IO_LOW;
					sim->io_delayed_emitted = 1;
				}
			}
			if ((sim->power_churn != 0) &&
			    (sim->bus_acked != 0) &&
			    ((sim->pwr_status &
			    (GTA4L_EMMC_PWRCTL_IO_LOW |
			    GTA4L_EMMC_PWRCTL_IO_HIGH)) == 0)) {
				/* Raise the opposite of the last cleared bit
				 * so the core's post-ack read-back stays
				 * clean and the loop can only end by
				 * exhaustion, not by a quiet window. */
				sim->pwr_status |= (sim->last_clear &
				    GTA4L_EMMC_PWRCTL_IO_LOW) ?
				    GTA4L_EMMC_PWRCTL_IO_HIGH :
				    GTA4L_EMMC_PWRCTL_IO_LOW;
				sim->churn_count++;
			}
			return sim->pwr_status;
		}
		if (offset == GTA4L_EMMC_REG_BUFFER_DATA_PORT) {
			uint32_t value = sim->buffer_data[
			    sim->buffer_index % (GTA4L_EMMC_BLOCK_BYTES / 4u)];
			sim->buffer_index++;
			if ((sim->data_late_error_words > 0) &&
			    (sim->data_late_error_emitted == 0) &&
			    (sim->buffer_index >=
			    (uint32_t)sim->data_late_error_words)) {
				/* Model a data error that only becomes visible
				 * after the FIFO drain has already started. */
				sim->error_int |=
				    GTA4L_EMMC_INT_ERR_DATA_CRC;
				sim->data_late_error_emitted = 1;
			}
			if ((sim->mb_active != 0) &&
			    ((sim->buffer_index %
			    (GTA4L_EMMC_BLOCK_BYTES / 4u)) == 0u)) {
				/* One full multi-block sector was drained.
				 * Stage the next sector or raise the data
				 * transfer completion after the last one. */
				sim->mb_done++;
				if (sim->mb_done < (int)sim->mb_count) {
					if ((sim->mb_done ==
					    ((int)sim->mb_count - 1)) &&
					    (sim->mb_last_brr_delay_us >= 0)) {
						/* Withhold the last sector
						 * until the drain window. */
						sim->normal_int &=
						    (uint16_t)
						    ~GTA4L_EMMC_INT_BRR;
						sim->mb_last_brr_pending = 1;
						sim->mb_last_brr_due_us =
						    sim->time_us +
						    (uint64_t)sim->
						    mb_last_brr_delay_us;
					} else {
						sim_fill_block(sim,
						    sim->mb_argument +
						    (uint32_t)sim->mb_done);
					}
				} else {
					sim->mb_active = 0;
					sim->normal_int &=
					    (uint16_t)~GTA4L_EMMC_INT_BRR;
					if (sim->mb_no_tc == 0) {
						if (sim->mb_tc_delay_us >= 0) {
							sim->mb_tc_pending = 1;
							sim->mb_tc_due_us =
							    sim->time_us +
							    (uint64_t)sim->
							    mb_tc_delay_us;
						} else {
							sim->normal_int |=
							    GTA4L_EMMC_INT_TC;
						}
					}
				}
			}
			return value;
		}
		if ((offset >= GTA4L_EMMC_REG_RESPONSE) &&
		    (offset < (GTA4L_EMMC_REG_RESPONSE + 16u))) {
			if ((sim->last_cmd == GTA4L_EMMC_CMD7) &&
			    (sim->cmd7_cc_observed == 0) &&
			    (sim->cmd7_command_readback == 0)) {
				sim->response_read_before_cc = 1;
			}
			if ((sim->last_cmd == GTA4L_EMMC_CMD7) &&
			    (sim->cmd7_access_latency_us > 0)) {
				/* Explicit per-access latency for the final
				 * sample race only. */
				sim->time_us +=
				    (uint64_t)sim->cmd7_access_latency_us;
				sim_apply_cmd7_events(sim);
			}
			if (sim->response_read_count <
			    SIM_MAX_RESPONSE_READS) {
				sim->response_read_cmd[
				    sim->response_read_count] =
				    sim->last_cmd;
				sim->response_read_clear[
				    sim->response_read_count] =
				    sim->error_clear_count;
				sim->response_read_offset[
				    sim->response_read_count] = offset;
				sim->response_read_count++;
			}
			if ((sim->cmd_late_error_index != SIM_CMD_CRC_NONE) &&
			    (sim->last_cmd == sim->cmd_late_error_index) &&
			    (sim->cmd_late_error_emitted == 0)) {
				/* An error asserted while the response is
				 * being read must still be evaluated before
				 * the status clear. */
				sim->error_int |= GTA4L_EMMC_INT_ERR_CMD_CRC;
				sim->cmd_late_error_emitted = 1;
			}
		}
		if (offset == GTA4L_EMMC_REG_PRESENT_STATE) {
			uint32_t value = (uint32_t)sim->sdhci[offset] |
			    ((uint32_t)sim->sdhci[offset + 1u] << 8) |
			    ((uint32_t)sim->sdhci[offset + 2u] << 16) |
			    ((uint32_t)sim->sdhci[offset + 3u] << 24);

			if ((sim->cmd7_busy != 0) ||
			    (sim->mb_dat_inhibit != 0)) {
				value |= GTA4L_EMMC_PRESENT_DAT_INHIBIT;
			}
			return value;
		}
		return (uint32_t)sim->sdhci[offset] |
		    ((uint32_t)sim->sdhci[offset + 1u] << 8) |
		    ((uint32_t)sim->sdhci[offset + 2u] << 16) |
		    ((uint32_t)sim->sdhci[offset + 3u] << 24);
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		uint32_t value;

		if (!sim_region_ok(offset, 4u, SIM_GCC_WORDS * 4u)) {
			sim->access_violation = 1;
			return 0;
		}
		value = sim->gcc[offset / 4u];
		/* The core establishes that a branch clock is running by
		 * reading its CBCR after the block reset. Count only a
		 * post-reset read that observes enable set and CLK_OFF
		 * clear; a write alone is not a verification. */
		if ((sim->trace_armed != 0) &&
		    (sim->bcr_deasserted != 0) &&
		    ((offset == SIM_GCC_APPS_CBCR) ||
		    (offset == SIM_GCC_AHB_CBCR)) &&
		    ((value & SIM_CBCR_CLOCK_ENABLE) != 0) &&
		    ((value & SIM_CBCR_CLK_OFF) == 0)) {
			if (offset == SIM_GCC_APPS_CBCR) {
				sim->apps_branch_enabled = 1;
			} else {
				sim->ahb_branch_enabled = 1;
			}
		}
		return value;
	}
	if (space == GTA4L_EMMC_SPACE_TLMM) {
		if (!sim_region_ok(offset, 4u, SIM_TLMM_WORDS * 4u)) {
			sim->access_violation = 1;
			return 0;
		}
		return sim->tlmm[offset / 4u];
	}
	return 0;
}

static void
sim_write8(void *context, uint32_t space, uint32_t offset, uint8_t value)
{
	struct sim *sim = (struct sim *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sim_note_sdhci_access(sim);
		if (!sim_sdhci_width_ok(offset, 1u)) {
			sim->access_violation = 1;
			return;
		}
		sim->sdhci[offset] = value;
		if (offset == GTA4L_EMMC_REG_SOFTWARE_RESET) {
			sim->reset_written = 1;
			if (!sim->reset_timeout) {
				sim->sdhci[offset] = 0;
				/* RESET_ALL resets HOST_CONTROL2 in hardware,
				 * so a later init sees VDD_180 clear and raises
				 * the IO request again. A controller that keeps
				 * the bit across reset is modelled separately. */
				if (sim->preserve_host_control2 == 0) {
					sim->sdhci[
					    GTA4L_EMMC_REG_HOST_CONTROL2] = 0;
					sim->sdhci[
					    GTA4L_EMMC_REG_HOST_CONTROL2 +
					    1u] = 0;
				}
			}
		}
		if (offset == GTA4L_EMMC_REG_POWER_CONTROL) {
			sim->power_written = 1;
			if ((value & GTA4L_EMMC_POWER_BUS_POWER) != 0) {
				if (sim->power_no_request != 0) {
					sim->pwr_status = 0;
				} else if (sim->power_off != 0) {
					sim->pwr_status |=
					    GTA4L_EMMC_PWRCTL_BUS_OFF;
				} else if ((sim->power_io_only == 0) &&
				    (sim->power_delayed_bus == 0) &&
				    (sim->power_late_bus_only == 0)) {
					sim->pwr_status |=
					    GTA4L_EMMC_PWRCTL_BUS_ON;
				}
			}
		}
		return;
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		uint32_t shift = (offset & 3u) * 8u;
		uint32_t mask = 0xFFu << shift;

		if (!sim_region_ok(offset, 1u, SIM_GCC_WORDS * 4u)) {
			sim->access_violation = 1;
			return;
		}
		sim->gcc[offset / 4u] = (sim->gcc[offset / 4u] & ~mask) |
		    ((uint32_t)value << shift);
		return;
	}
	if (space == GTA4L_EMMC_SPACE_TLMM) {
		uint32_t shift = (offset & 3u) * 8u;
		uint32_t mask = 0xFFu << shift;

		if (!sim_region_ok(offset, 1u, SIM_TLMM_WORDS * 4u)) {
			sim->access_violation = 1;
			return;
		}
		sim->tlmm[offset / 4u] = (sim->tlmm[offset / 4u] & ~mask) |
		    ((uint32_t)value << shift);
		return;
	}
}

static void
sim_write32(void *context, uint32_t space, uint32_t offset, uint32_t value);

static void
sim_fill_ext_csd(struct sim *sim)
{
	uint32_t count = sim->sector_count;

	memset(sim->ext_csd, 0, sizeof(sim->ext_csd));
	sim->ext_csd[GTA4L_EMMC_EXT_CSD_REV_OFF] = 8;
	sim->ext_csd[183] = 2;
	sim->ext_csd[GTA4L_EMMC_EXT_CSD_DATA_SECTOR_SIZE_OFF] =
	    (uint8_t)(sim->data_sector_4k ? 1 : sim->data_sector_size_code);
	sim->ext_csd[GTA4L_EMMC_EXT_CSD_PARTITION_CONFIG_OFF] =
	    (uint8_t)sim->partition_config;
	sim->ext_csd[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF + 0] =
	    (uint8_t)(count & 0xFFu);
	sim->ext_csd[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF + 1] =
	    (uint8_t)((count >> 8) & 0xFFu);
	sim->ext_csd[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF + 2] =
	    (uint8_t)((count >> 16) & 0xFFu);
	sim->ext_csd[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF + 3] =
	    (uint8_t)((count >> 24) & 0xFFu);
}

static uint8_t
sim_block_byte(uint32_t lba, uint32_t index)
{
	return (uint8_t)((lba * 7u) + (index * 13u));
}

static void
sim_fill_block(struct sim *sim, uint32_t lba)
{
	uint32_t index;
	uint8_t *bytes = (uint8_t *)sim->buffer_data;

	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
		bytes[index] = sim_block_byte(lba, index);
	}
}

static void
sim_fill_words(struct sim *sim, const uint8_t *bytes)
{
	uint32_t index;

	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index += 4u) {
		sim->buffer_data[index / 4u] =
		    (uint32_t)bytes[index] |
		    ((uint32_t)bytes[index + 1u] << 8) |
		    ((uint32_t)bytes[index + 2u] << 16) |
		    ((uint32_t)bytes[index + 3u] << 24);
	}
}

static void
sim_set_response(struct sim *sim, const uint32_t *words, uint32_t count)
{
	uint32_t index;
	uint32_t reg[4];

	if (count == 4u) {
		/* Encode a normalized 128-bit response into the SDHCI
		 * RESPONSE3..RESPONSE0 register order with a zero CRC byte. */
		reg[3] = words[0] >> 8;
		reg[2] = (words[1] >> 8) | ((words[0] & 0xFFu) << 24);
		reg[1] = (words[2] >> 8) | ((words[1] & 0xFFu) << 24);
		reg[0] = (words[3] >> 8) | ((words[2] & 0xFFu) << 24);
		for (index = 0; index < 4u; index++) {
			sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE + index * 4u, reg[index]);
		}
		return;
	}
	for (index = 0; index < count; index++) {
		sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_RESPONSE + index * 4u, words[index]);
	}
}

/* Store a raw RESPONSE0..3 window exactly as given. */
static void
sim_set_raw_response(struct sim *sim, const uint32_t words[4])
{
	uint32_t index;
	uint32_t offset;

	for (index = 0; index < 4u; index++) {
		uint32_t value = words[index];

		offset = GTA4L_EMMC_REG_RESPONSE + index * 4u;
		sim->sdhci[offset] = (uint8_t)(value & 0xFFu);
		sim->sdhci[offset + 1u] = (uint8_t)((value >> 8) & 0xFFu);
		sim->sdhci[offset + 2u] = (uint8_t)((value >> 16) & 0xFFu);
		sim->sdhci[offset + 3u] = (uint8_t)((value >> 24) & 0xFFu);
	}
}

static void
sim_load_cmd2_response(struct sim *sim)
{
	uint32_t words[4];
	uint32_t raw[4];

	if (sim->cmd2_keep_response != 0) {
		/* Model a controller that does not update the response
		 * registers for this failed command. */
		return;
	}
	if (sim->cmd2_ocr_like_response != 0) {
		/* The 2026-10-31 raw window: OCR-like, origin unconfirmed. */
		raw[0] = 0x00000000u;
		raw[1] = 0x00000000u;
		raw[2] = 0x80000000u;
		raw[3] = 0x00C0FF80u;
		sim_set_raw_response(sim, raw);
		return;
	}
	if (sim->cmd2_normalized_layout != 0) {
		/* Model the cmd2window observation: the controller stores the
		 * CID words in normalized order, with the CRC trailer in the
		 * low byte of the last word. CMD3 later overwrites RESPONSE0,
		 * leaving words 1..3 as the photographed window. */
		raw[0] = sim->cid[0];
		raw[1] = sim->cid[1];
		raw[2] = sim->cid[2];
		raw[3] = (sim->cid[3] & 0xFFFFFF00u) | 0x78u;
		sim_set_raw_response(sim, raw);
		return;
	}
	words[0] = sim->bad_cid ? 0 : sim->cid[0];
	words[1] = sim->bad_cid ? 0 : sim->cid[1];
	words[2] = sim->bad_cid ? 0 : sim->cid[2];
	words[3] = sim->bad_cid ? 0 : sim->cid[3];
	sim_set_response(sim, words, 4);
}

static int
sim_command_ok(struct sim *sim, uint32_t index, uint32_t argument,
    uint16_t flags)
{
	uint32_t response_type = flags & 0x3u;
	uint32_t checks = flags & (SIM_CMD_CRC | SIM_CMD_INDEX);
	int data = (flags & SIM_CMD_DATA) != 0;
	int read = (sim->transfer_mode & SIM_TM_READ) != 0;
	int block_count_en =
	    (sim->transfer_mode & SIM_TM_BLOCK_COUNT_EN) != 0;
	int multi = (sim->transfer_mode & SIM_TM_MULTI) != 0;

	switch (index) {
	case GTA4L_EMMC_CMD0:
		return response_type == 0u && data == 0 && checks == 0u;
	case GTA4L_EMMC_CMD1:
		/* R3 responses are not CRC checked. */
		return response_type == 2u && data == 0 && checks == 0u;
	case GTA4L_EMMC_CMD2:
		return response_type == 1u && data == 0 &&
		    checks == SIM_CMD_CRC;
	case GTA4L_EMMC_CMD3:
		return response_type == 2u && data == 0 &&
		    checks == (SIM_CMD_CRC | SIM_CMD_INDEX) &&
		    ((argument >> 16) != 0u);
	case GTA4L_EMMC_CMD7:
		return response_type == 3u && data == 0 &&
		    checks == (SIM_CMD_CRC | SIM_CMD_INDEX) &&
		    ((argument >> 16) == sim->rca);
	case GTA4L_EMMC_CMD9:
		return response_type == 1u && data == 0 &&
		    checks == SIM_CMD_CRC &&
		    ((argument >> 16) == sim->rca);
	case GTA4L_EMMC_CMD13:
		return response_type == 2u && data == 0 &&
		    checks == (SIM_CMD_CRC | SIM_CMD_INDEX) &&
		    ((argument >> 16) == sim->rca);
	case GTA4L_EMMC_CMD8:
		return response_type == 2u && data != 0 && read &&
		    checks == (SIM_CMD_CRC | SIM_CMD_INDEX) &&
		    block_count_en && sim->block_size == 512u &&
		    sim->block_count == 1u;
	case GTA4L_EMMC_CMD17:
		return response_type == 2u && data != 0 && read &&
		    checks == (SIM_CMD_CRC | SIM_CMD_INDEX) &&
		    block_count_en && sim->block_size == 512u &&
		    sim->block_count == 1u &&
		    argument < sim->sector_count;
	case GTA4L_EMMC_CMD18:
		return response_type == 2u && data != 0 && read && multi &&
		    checks == (SIM_CMD_CRC | SIM_CMD_INDEX) &&
		    block_count_en && sim->block_size == 512u &&
		    sim->block_count > 1u &&
		    argument < sim->sector_count &&
		    ((uint64_t)argument + sim->block_count) <=
		    (uint64_t)sim->sector_count;
	case GTA4L_EMMC_CMD12:
		return response_type == 3u && data == 0 &&
		    checks == (SIM_CMD_CRC | SIM_CMD_INDEX) &&
		    argument == 0u;
	default:
		return 0;
	}
}

static void
sim_command(struct sim *sim, uint16_t command)
{
	uint32_t index = (command >> 8) & 0x3Fu;
	uint32_t flags = command & 0xFFu;
	uint32_t argument = sim_read32(sim, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ARGUMENT);
	uint32_t response = 0x00000900u; /* READY_FOR_DATA | TRAN */
	uint32_t words[4];
	uint64_t cc_time = 0;
	uint64_t tc_time = 0;

	sim_apply_cmd7_events(sim);
	if ((sim->last_cmd == GTA4L_EMMC_CMD7) &&
	    (index != GTA4L_EMMC_CMD7)) {
		if ((sim->cmd7_busy != 0) ||
		    ((sim->cmd7_tc_due != 0) &&
		    (sim->cmd7_tc_observed == 0))) {
			sim->cmd7_early_command = 1;
		}
		sim->cmd7_next_issue_time_us = sim->time_us;
		sim->cmd7_next_index = (int)index;
	}
	sim->last_cmd = index;
	sim->last_cmd_argument = argument;
	sim->cmd_count++;
	sim->cmd_clear_count = sim->error_clear_count;
	sim->cmd_issue_clear[index] = sim->error_clear_count;
	sim->buffer_index = 0;
	if (index == GTA4L_EMMC_CMD0) {
		sim->cmd0_time_us = sim->time_us;
	}
	if ((index == GTA4L_EMMC_CMD1) && (sim->cmd1_seen == 0)) {
		sim->cmd1_time_us = sim->time_us;
		sim->cmd1_seen = 1;
	}
	if (index == GTA4L_EMMC_CMD1) {
		if (sim->cmd1_attempt_count < 4u) {
			sim->cmd1_attempt_time_us[sim->cmd1_attempt_count] =
			    sim->time_us;
		}
		sim->cmd1_attempt_count++;
	}
	if (index == GTA4L_EMMC_CMD2) {
		sim->cmd2_time_us = sim->time_us;
	}
	if (index == GTA4L_EMMC_CMD12) {
		/* Count the stop attempt even when the encoding or an injected
		 * error rejects it, and end the modeled data phase. */
		sim->mb_stop_count++;
		sim->mb_stop_normal_reads = 0;
		sim->mb_active = 0;
		sim->mb_dat_inhibit = 0;
	}

	if (!sim_command_ok(sim, index, argument, (uint16_t)flags)) {
		sim->command_error = 1;
		return;
	}

	if ((sim->cmd_timeout != 0) && (index == GTA4L_EMMC_CMD0)) {
		return;
	}
	if ((index == GTA4L_EMMC_CMD1) &&
	    (sim->cmd1_timeouts_remaining != 0)) {
		sim->cmd1_timeouts_remaining--;
		return;
	}
	if (index == GTA4L_EMMC_CMD2) {
		/* Hypothetical timing model: if the configured CRC-retire
		 * interval has not elapsed since the ready CMD1 response,
		 * latch CMD_CRC without loading a CID. The interval is
		 * measured from that response and is not reset by the
		 * pre-command status clears. */
		if ((sim->cmd1_crc_retire_us != 0) &&
		    (sim->cmd1_crc_retire_pending != 0)) {
			if (sim->time_us < sim->cmd1_ready_time_us +
			    (uint64_t)sim->cmd1_crc_retire_us) {
				sim->error_int |= GTA4L_EMMC_INT_ERR_CMD_CRC;
				return;
			}
			sim->cmd1_crc_retire_pending = 0;
		}
		/* Load the response before an injected CRC error so the
		 * snapshot ordering tests have words to capture. A real
		 * controller may or may not update a bad response; the core
		 * must still treat it as diagnostics only. */
		sim_load_cmd2_response(sim);
	}
	if ((sim->cmd_crc_index != SIM_CMD_CRC_NONE) &&
	    (index == sim->cmd_crc_index)) {
		sim->error_int |= GTA4L_EMMC_INT_ERR_CMD_CRC;
		if (sim->cmd_crc_with_cc != 0) {
			sim->normal_int |= GTA4L_EMMC_INT_CC;
		}
		return;
	}
	if ((sim->cmd_index != 0) && (index == GTA4L_EMMC_CMD3)) {
		sim->error_int |= GTA4L_EMMC_INT_ERR_CMD_INDEX;
		return;
	}

	switch (index) {
	case GTA4L_EMMC_CMD0:
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		break;
	case GTA4L_EMMC_CMD1:
		response = GTA4L_EMMC_OCR_VALUE;
		if (sim->cmd1_not_ready_remaining != 0) {
			/* A completed but not-ready R3 reply. */
			sim->cmd1_not_ready_remaining--;
		} else if (sim->card_never_ready == 0) {
			response |= 0x80000000u;
		}
		if ((response & 0x80000000u) != 0) {
			/* Start the hypothetical CRC-retire timer only for
			 * the ready reply; a not-ready reply never arms it. */
			sim->cmd1_ready_time_us = sim->time_us;
			sim->cmd1_crc_retire_pending = 1;
		}
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		sim_set_response(sim, &response, 1);
		break;
	case GTA4L_EMMC_CMD2:
		sim_load_cmd2_response(sim);
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		break;
	case GTA4L_EMMC_CMD3:
		/* eMMC SET_RELATIVE_ADDR: the host assigns the RCA in the
		 * argument; the response is R1 status. */
		sim->cmd3_argument = argument;
		sim->rca = argument >> 16;
		response = sim->cmd3_identify_status ? 0x00000500u :
		    0x00000900u;
		if (sim->cmd3_error != 0) {
			response |= 0x00000080u;
		}
		sim->cmd3_normal_reads = 0;
		if ((sim->cmd3_no_cc == 0) && (sim->cmd3_late_cc == 0)) {
			sim->normal_int |= GTA4L_EMMC_INT_CC;
		}
		sim_set_response(sim, &response, 1);
		break;
	case GTA4L_EMMC_CMD7:
		sim->cmd7_normal_reads = 0;
		sim->cmd7_error_reads = 0;
		sim->cmd7_script_index = 0;
		sim->cmd7_tc_raised = 0;
		sim->cmd7_cc_observed = 0;
		sim->cmd7_tc_observed = 0;
		sim->cmd7_error_observed = 0;
		sim->cmd7_early_command = 0;
		sim->cmd7_release_time_us = 0;
		sim->cmd7_next_issue_time_us = 0;
		sim->cmd7_next_index = -1;
		sim->cmd7_command_readback = 0;
		sim->response_read_before_cc = 0;
		sim->cmd7_issue_time_us = sim->time_us;
		sim->cmd7_cc_time_valid =
		    ((sim->cmd7_cc_immediate != 0) ||
		    (sim->cmd7_cc_delay >= 0)) ? 1 : 0;
		sim->cmd7_tc_time_valid =
		    ((sim->cmd7_tc_immediate != 0) ||
		    (sim->cmd7_tc_delay >= 0)) ? 1 : 0;
		sim->cmd7_error_time_valid =
		    (sim->cmd7_error_delay >= 0) ? 1 : 0;
		cc_time = sim->time_us;
		tc_time = sim->time_us;
		if ((sim->cmd7_cc_immediate == 0) &&
		    (sim->cmd7_cc_delay >= 0)) {
			cc_time += (uint64_t)sim->cmd7_cc_delay;
		}
		if ((sim->cmd7_tc_immediate == 0) &&
		    (sim->cmd7_tc_delay >= 0)) {
			tc_time += (uint64_t)sim->cmd7_tc_delay;
		}
		sim->cmd7_cc_time_us = cc_time;
		sim->cmd7_tc_time_us = tc_time;
		sim->cmd7_error_time_us = sim->time_us +
		    ((sim->cmd7_error_delay >= 0) ?
		    (uint64_t)sim->cmd7_error_delay : 0);
		sim->cmd7_cc_due = 0;
		sim->cmd7_tc_due = 0;
		sim->cmd7_error_due = 0;
		sim->cmd7_cc_applied = 0;
		if (sim->cmd7_keep_response == 0) {
			if (sim->cmd7_fresh_response != 0) {
				sim_set_raw_response(sim, sim->cmd7_window);
			} else {
				sim_set_response(sim, &response, 1);
			}
		}
		sim_apply_cmd7_events(sim);
		sim->cmd7_busy = (sim->cmd7_tc_due == 0) ? 1 : 0;
		break;
	case GTA4L_EMMC_CMD9:
		words[0] = sim->bad_csd ? 0xFFFFFFFFu : sim->csd[0];
		words[1] = sim->bad_csd ? 0xFFFFFFFFu : sim->csd[1];
		words[2] = sim->bad_csd ? 0xFFFFFFFFu : sim->csd[2];
		words[3] = sim->bad_csd ? 0xFFFFFFFFu : sim->csd[3];
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		sim_set_response(sim, words, 4);
		break;
	case GTA4L_EMMC_CMD13:
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		sim_set_response(sim, &response, 1);
		break;
	case GTA4L_EMMC_CMD17:
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		sim_set_response(sim, &response, 1);
		if (sim->data_crc != 0) {
			sim->error_int |= GTA4L_EMMC_INT_ERR_DATA_CRC;
			break;
		}
		if (sim->data_timeout == 0) {
			if (sim->no_brr == 0) {
				sim_fill_block(sim, argument);
				sim->normal_int |= GTA4L_EMMC_INT_BRR;
			}
			if (sim->no_tc == 0) {
				sim->normal_int |= GTA4L_EMMC_INT_TC;
			}
		}
		break;
	case GTA4L_EMMC_CMD18:
		sim->mb_argument = argument;
		sim->mb_issued_block_size = sim->block_size;
		sim->mb_issued_block_count = sim->block_count;
		sim->mb_count = sim->block_count;
		sim->mb_done = 0;
		sim->mb_dat_inhibit = 1;
		sim->mb_active = 0;
		sim->mb_tc_pending = 0;
		sim->mb_last_brr_pending = 0;
		sim->mb_error_pending = 0;
		if (sim->mb_error_delay_us >= 0) {
			sim->mb_error_pending = 1;
			sim->mb_error_due_us = sim->time_us +
			    (uint64_t)sim->mb_error_delay_us;
		}
		sim->normal_int &= (uint16_t)~GTA4L_EMMC_INT_BRR;
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		if (sim->mb_response_error != 0) {
			response |= 0x00000080u;
		}
		sim_set_response(sim, &response, 1);
		if (sim->data_crc != 0) {
			sim->error_int |= GTA4L_EMMC_INT_ERR_DATA_CRC;
			break;
		}
		if (sim->data_timeout == 0) {
			sim->mb_active = 1;
			if (sim->no_brr == 0) {
				sim_fill_block(sim, argument);
				sim->normal_int |= GTA4L_EMMC_INT_BRR;
			}
		}
		break;
	case GTA4L_EMMC_CMD12:
		if ((sim->mb_no_cc == 0) && (sim->mb_late_cc == 0)) {
			sim->normal_int |= GTA4L_EMMC_INT_CC;
		}
		if (sim->mb_stop_no_tc == 0) {
			sim->normal_int |= GTA4L_EMMC_INT_TC;
		}
		sim_set_response(sim, &response, 1);
		break;
	case GTA4L_EMMC_CMD8:
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		sim_set_response(sim, &response, 1);
		if (sim->data_crc != 0) {
			sim->error_int |= GTA4L_EMMC_INT_ERR_DATA_CRC;
			break;
		}
		if (sim->data_timeout == 0) {
			if (sim->no_brr == 0) {
				sim_fill_words(sim, sim->ext_csd);
				sim->normal_int |= GTA4L_EMMC_INT_BRR;
			}
			if (sim->no_tc == 0) {
				sim->normal_int |= GTA4L_EMMC_INT_TC;
			}
		}
		break;
	default:
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		sim_set_response(sim, &response, 1);
		break;
	}
	if ((sim->irrelevant_tc_valid != 0) &&
	    (index == sim->irrelevant_tc_index)) {
		/* Non-busy response types must ignore TC as a completion. */
		sim->normal_int |= GTA4L_EMMC_INT_TC;
	}
}

static void
sim_write16(void *context, uint32_t space, uint32_t offset, uint16_t value)
{
	struct sim *sim = (struct sim *)context;

	if (space != GTA4L_EMMC_SPACE_SDHCI) {
		uint32_t size = (space == GTA4L_EMMC_SPACE_GCC) ?
		    SIM_GCC_WORDS * 4u : SIM_TLMM_WORDS * 4u;

		if (!sim_region_ok(offset, 2u, size)) {
			sim->access_violation = 1;
			return;
		}
		sim_write8(context, space, offset, (uint8_t)(value & 0xFFu));
		sim_write8(context, space, offset + 1u,
		    (uint8_t)(value >> 8));
		return;
	}
	sim_note_sdhci_access(sim);
	if (!sim_sdhci_width_ok(offset, 2u)) {
		sim->access_violation = 1;
		return;
	}
	if (offset == GTA4L_EMMC_REG_BLOCK_SIZE) {
		sim->block_size = value;
	}
	if (offset == GTA4L_EMMC_REG_BLOCK_COUNT) {
		sim->block_count = value;
	}
	if (offset == GTA4L_EMMC_REG_TRANSFER_MODE) {
		sim->transfer_mode = value;
		if ((value & SIM_TM_AUTO_CMD12) != 0) {
			sim->auto_cmd12_seen = 1;
		}
	}
	if (offset == GTA4L_EMMC_REG_COMMAND) {
		sim->sdhci[offset] = (uint8_t)(value & 0xFFu);
		sim->sdhci[offset + 1u] = (uint8_t)(value >> 8);
		sim_command(sim, value);
		return;
	}
	if (offset == GTA4L_EMMC_REG_NORMAL_INT_STATUS) {
		sim->normal_int &= (uint16_t)~value;
		if ((sim->cmd3_stale_cc != 0) &&
		    (sim->last_cmd == GTA4L_EMMC_CMD2)) {
			/* The post-CMD2 clear is the first one; the
			 * pre-CMD3 clear is the second. Inject a stale
			 * completion that survives the second clear. */
			sim->cmd2_normal_clear_count++;
			if (sim->cmd2_normal_clear_count == 2u) {
				sim->normal_int |= GTA4L_EMMC_INT_CC;
			}
		}
		if ((sim->cmd7_stale_status != 0) &&
		    (sim->last_cmd == GTA4L_EMMC_CMD9)) {
			/* Inject after the CMD9 tail clear; the CMD7
			 * pre-command clear must remove it. */
			sim->cmd9_normal_clear_count++;
			if (sim->cmd9_normal_clear_count == 1u) {
				sim->normal_int |= GTA4L_EMMC_INT_CC |
				    GTA4L_EMMC_INT_TC;
			}
		}
		if (sim->clear_response_on_normal_w1c != 0) {
			/* Model a controller that drops the response window
			 * when the normal-status latch is cleared. */
			sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE + 0u, 0u);
			sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE + 4u, 0u);
			sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE + 8u, 0u);
			sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE + 12u, 0u);
		}
		return;
	}
	if (offset == GTA4L_EMMC_REG_ERROR_INT_STATUS) {
		uint16_t pending = (uint16_t)(sim->error_int &
		    GTA4L_EMMC_INT_ERR_CMD_CRC);

		sim->error_int &= (uint16_t)~value;
		sim->error_clear_count++;
		if ((sim->cmd7_stale_status != 0) &&
		    (sim->last_cmd == GTA4L_EMMC_CMD9) &&
		    (sim->cmd9_normal_clear_count == 1u)) {
			/* Stale error latched after the CMD9 tail clear. */
			sim->error_int |= GTA4L_EMMC_INT_ERR_CMD_CRC;
		}
		if ((sim->clear_response_on_w1c != 0) &&
		    ((value & pending) != 0)) {
			/* A real controller may invalidate the response with
			 * the latch clear. The snapshot must already exist. */
			sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE + 0u, 0u);
			sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE + 4u, 0u);
			sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE + 8u, 0u);
			sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE + 12u, 0u);
		}
		return;
	}
	if (offset == GTA4L_EMMC_REG_NORMAL_INT_ENABLE) {
		sim->normal_enable = value;
		return;
	}
	if (offset == GTA4L_EMMC_REG_ERROR_INT_ENABLE) {
		sim->error_enable = value;
		return;
	}
	if (offset == GTA4L_EMMC_REG_NORMAL_INT_SIGNAL) {
		sim->normal_signal = value;
		return;
	}
	if (offset == GTA4L_EMMC_REG_ERROR_INT_SIGNAL) {
		sim->error_signal = value;
		return;
	}
	if (offset == GTA4L_EMMC_REG_CLOCK_CONTROL) {
		sim->sdhci[offset] = (uint8_t)(value & 0xFFu);
		sim->sdhci[offset + 1u] = (uint8_t)(value >> 8);
		if (((value & GTA4L_EMMC_CLOCK_CARD_EN) != 0) &&
		    (sim->power_written == 0)) {
			sim->card_clock_before_power = 1;
		}
		if ((value & GTA4L_EMMC_CLOCK_CARD_EN) != 0) {
			sim->card_clock_enabled = 1;
			sim->card_clock_time_us = sim->time_us;
		}
		if (((value & GTA4L_EMMC_CLOCK_INT_EN) != 0) &&
		    (sim->clock_timeout == 0)) {
			sim->sdhci[offset] |= GTA4L_EMMC_CLOCK_INT_STABLE;
		}
		return;
	}
	if (offset == GTA4L_EMMC_REG_HOST_CONTROL2) {
		uint16_t previous = (uint16_t)(sim->sdhci[offset] |
		    ((uint16_t)sim->sdhci[offset + 1u] << 8));

		sim->sdhci[offset] = (uint8_t)(value & 0xFFu);
		sim->sdhci[offset + 1u] = (uint8_t)(value >> 8);
		/* The controller raises an IO request only when the 1.8 V
		 * signalling bit changes, matching the Linux
		 * sdhci_msm_check_power_status() contract. The wrong-io
		 * scenarios deliberately raise the opposite level. */
		if ((sim->power_io_delayed == 0) &&
		    ((value & SIM_CTRL_VDD_180) !=
		    (previous & SIM_CTRL_VDD_180))) {
			if (sim->power_opposite_io != 0) {
				sim->pwr_status |= GTA4L_EMMC_PWRCTL_IO_LOW |
				    GTA4L_EMMC_PWRCTL_IO_HIGH;
			} else if ((sim->power_wrong_io != 0) ||
			    (sim->power_io_wrong_then_correct != 0)) {
				if ((value & SIM_CTRL_VDD_180) != 0) {
					sim->pwr_status |=
					    GTA4L_EMMC_PWRCTL_IO_HIGH;
				} else {
					sim->pwr_status |=
					    GTA4L_EMMC_PWRCTL_IO_LOW;
				}
			} else if ((value & SIM_CTRL_VDD_180) != 0) {
				sim->pwr_status |= GTA4L_EMMC_PWRCTL_IO_LOW;
			} else {
				sim->pwr_status |= GTA4L_EMMC_PWRCTL_IO_HIGH;
			}
		}
		return;
	}
	sim->sdhci[offset] = (uint8_t)(value & 0xFFu);
	sim->sdhci[offset + 1u] = (uint8_t)(value >> 8);
}

static void
sim_write32(void *context, uint32_t space, uint32_t offset, uint32_t value)
{
	struct sim *sim = (struct sim *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sim_note_sdhci_access(sim);
		if (!sim_sdhci_width_ok(offset, 4u)) {
			sim->access_violation = 1;
			return;
		}
		if (offset == GTA4L_EMMC_REG_CORE_DLL_CONFIG) {
			uint32_t previous = (uint32_t)sim->sdhci[offset] |
			    ((uint32_t)sim->sdhci[offset + 1u] << 8) |
			    ((uint32_t)sim->sdhci[offset + 2u] << 16) |
			    ((uint32_t)sim->sdhci[offset + 3u] << 24);

			/* The reset must be requested before the DLL is powered
			 * down; a combined or reordered write would hide a
			 * sequence regression that the final bits cannot. */
			if (((value & SIM_DLL_PDN) != 0) &&
			    ((previous & SIM_DLL_PDN) == 0) &&
			    (sim->dll_rst_written == 0)) {
				sim->dll_pdn_before_rst = 1;
			}
			if ((value & SIM_DLL_RST) != 0) {
				sim->dll_rst_written = 1;
			}
			if ((value & SIM_DLL_PDN) != 0) {
				sim->dll_pdn_written = 1;
			}
		}
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_CLEAR) {
			sim->last_clear = value;
			if (sim->power_stuck == 0) {
				sim->pwr_status &= ~value;
			}
			return;
		}
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_CTL) {
			sim->power_ack = value;
			sim->power_ack_time_us = sim->time_us;
			if ((value & GTA4L_EMMC_PWRCTL_ACK_BUS) != 0) {
				sim->bus_acked = 1;
			}
			/* The acknowledgement reports completion; it does not
			 * clear the request latches. Only CORE_PWRCTL_CLEAR
			 * does that. Inject a request here to prove the ack
			 * cannot erase an uncaptured request. */
			if ((sim->power_io_on_ack != 0) &&
			    (sim->ack_io_injected == 0) &&
			    ((value & GTA4L_EMMC_PWRCTL_ACK_BUS) != 0)) {
				sim->pwr_status |= GTA4L_EMMC_PWRCTL_IO_HIGH;
				sim->ack_io_injected = 1;
			}
			return;
		}
		sim->sdhci[offset] = (uint8_t)(value & 0xFFu);
		sim->sdhci[offset + 1u] = (uint8_t)((value >> 8) & 0xFFu);
		sim->sdhci[offset + 2u] = (uint8_t)((value >> 16) & 0xFFu);
		sim->sdhci[offset + 3u] = (uint8_t)((value >> 24) & 0xFFu);
		return;
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		if (!sim_region_ok(offset, 4u, SIM_GCC_WORDS * 4u)) {
			sim->access_violation = 1;
			return;
		}
		sim->gcc[offset / 4u] = value;
		if (offset == SIM_GCC_BCR) {
			if ((value & SIM_BCR_BLOCK_RESET) != 0) {
				if (sim->bcr_assert_pending != 0) {
					sim->bcr_reasserted = 1;
				}
				if (sim->bcr_asserted == 0) {
					sim->bcr_assert_time_us =
					    sim->time_us;
				}
				sim->bcr_asserted = 1;
				sim->bcr_assert_pending = 1;
				sim->bcr_reset = 1;
				/* The block is in reset until the core
				 * re-enables both branches; the reset also
				 * clears the branch enables in hardware. */
				sim->apps_branch_enabled = 0;
				sim->ahb_branch_enabled = 0;
				sim->gcc[SIM_GCC_APPS_CBCR / 4u] =
				    SIM_CBCR_CLK_OFF;
				sim->gcc[SIM_GCC_AHB_CBCR / 4u] =
				    SIM_CBCR_CLK_OFF;
				sim->gcc[offset / 4u] &= ~SIM_BCR_BLOCK_RESET;
			} else if (sim->bcr_assert_pending != 0) {
				sim->bcr_assert_pending = 0;
				sim->bcr_deasserted = 1;
				sim->bcr_pulse_us =
				    sim->time_us - sim->bcr_assert_time_us;
				if (sim->bcr_pulse_us < SIM_BCR_DELAY_US) {
					sim->bcr_short_pulse = 1;
				}
			}
		}
		if ((offset == SIM_GCC_APPS_CBCR) ||
		    (offset == SIM_GCC_AHB_CBCR)) {
			sim->branch_writes++;
			if ((value & SIM_CBCR_CLOCK_ENABLE) != 0) {
				/* An injected branch failure keeps the clock
				 * off no matter how often the core writes the
				 * enable bit, so the bounded wait must expire
				 * and the core must report ERR_CLOCK. */
				if (((offset == SIM_GCC_APPS_CBCR) &&
				    (sim->apps_branch_fail != 0)) ||
				    ((offset == SIM_GCC_AHB_CBCR) &&
				    (sim->ahb_branch_fail != 0))) {
					sim->gcc[offset / 4u] |=
					    SIM_CBCR_CLK_OFF;
				} else {
					sim->gcc[offset / 4u] &=
					    ~SIM_CBCR_CLK_OFF;
				}
			} else {
				sim->gcc[offset / 4u] |= SIM_CBCR_CLK_OFF;
				if (offset == SIM_GCC_APPS_CBCR) {
					sim->apps_branch_enabled = 0;
				} else {
					sim->ahb_branch_enabled = 0;
				}
			}
		}
		if (offset == SIM_GCC_APPS_CMD) {
			if ((value & SIM_RCG_CMD_UPDATE) != 0) {
				if (sim->rcg_busy == 0) {
					sim->gcc[offset / 4u] &=
					    ~SIM_RCG_CMD_UPDATE;
				}
			}
		}
		return;
	}
	if (space == GTA4L_EMMC_SPACE_TLMM) {
		if (!sim_region_ok(offset, 4u, SIM_TLMM_WORDS * 4u)) {
			sim->access_violation = 1;
			return;
		}
		sim->tlmm[offset / 4u] = value;
	}
}

static void
sim_barrier(void *context)
{
	(void)context;
}

static void
sim_delay_us(void *context, uint32_t microseconds)
{
	struct sim *sim = (struct sim *)context;

	sim->time_us += microseconds;
	sim_apply_mb_events(sim);
}

static uint64_t
sim_counter(void *context)
{
	struct sim *sim = (struct sim *)context;

	return sim->time_us;
}

static void
sim_log(void *context, uint32_t event, uint32_t a, uint32_t b)
{
	struct sim *sim = (struct sim *)context;

	if (sim->log_count < SIM_MAX_LOG) {
		sim->log_event[sim->log_count] = event;
		sim->log_a[sim->log_count] = a;
		sim->log_b[sim->log_count] = b;
		sim->log_count++;
	}
}

static const struct gta4l_emmc_ops sim_ops = {
	.read8 = sim_read8,
	.read16 = sim_read16,
	.read32 = sim_read32,
	.write8 = sim_write8,
	.write16 = sim_write16,
	.write32 = sim_write32,
	.barrier = sim_barrier,
	.delay_us = sim_delay_us,
	.log = sim_log,
	.counter = sim_counter,
};

static void
sim_init(struct sim *sim)
{
	memset(sim, 0, sizeof(*sim));
	sim->sector_count = 61071360u;
	sim->partition_config = 0u;
	sim->cmd_crc_index = SIM_CMD_CRC_NONE;
	sim->cmd_late_error_index = SIM_CMD_CRC_NONE;
	sim->cmd7_cc_immediate = 1;
	sim->cmd7_tc_immediate = 1;
	sim->cmd7_cc_delay = -1;
	sim->cmd7_tc_delay = -1;
	sim->cmd7_error_delay = -1;
	sim->mb_tc_delay_us = -1;
	sim->mb_last_brr_delay_us = -1;
	sim->mb_error_delay_us = -1;
	sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_MCI_VERSION,
	    0x10000072u);
	/* The observed stage-3 vendor value: MCLK default, HC_SELECT_IN=6
	 * with the enable clear. The core must normalize the override bits. */
	sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_VENDOR_SPEC, SIM_VENDOR_INITIAL);
	sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_VENDOR_SPEC3, SIM_VENDOR3_PWRSAVE_DLL);
	sim->gcc[SIM_GCC_APPS_CBCR / 4u] = SIM_CBCR_CLOCK_ENABLE;
	sim->gcc[SIM_GCC_AHB_CBCR / 4u] = SIM_CBCR_CLOCK_ENABLE;
	sim->gcc[SIM_GCC_ICE_CBCR / 4u] = SIM_CBCR_CLOCK_ENABLE;
	sim->gcc[SIM_GCC_APPS_CFG / 4u] = 0x00000401u;
	sim->cid[0] = 0x15010044u;
	sim->cid[1] = 0x5836384Du;
	sim->cid[2] = 0x42009005u;
	sim->cid[3] = 0xF4661900u;
	sim->csd[0] = 0xD0270132u;
	sim->csd[1] = 0x0F5903FFu;
	sim->csd[2] = 0xF6DBFFEFu;
	sim->csd[3] = 0x8E404000u;
	sim_fill_ext_csd(sim);
}

static int failures;

static void
check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

static void
check_block(const struct sim *sim, uint32_t lba, const uint8_t *buffer,
    const char *label)
{
	uint32_t index;
	int bad = 0;

	(void)sim;

	for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
		if (buffer[index] != sim_block_byte(lba, index)) {
			bad++;
		}
	}
	if (bad != 0) {
		fprintf(stderr, "FAIL: %s has %u wrong bytes\n", label, bad);
		failures++;
	}
}

static void
check_multi_block(const uint8_t *buffer, uint32_t start, uint32_t count,
    const char *label)
{
	uint32_t block;
	uint32_t index;

	for (block = 0; block < count; block++) {
		for (index = 0; index < GTA4L_EMMC_BLOCK_BYTES; index++) {
			if (buffer[(block * GTA4L_EMMC_BLOCK_BYTES) + index] !=
			    sim_block_byte(start + block, index)) {
				fprintf(stderr,
				    "FAIL: %s block %u byte %u mismatch\n",
				    label, block, index);
				failures++;
				return;
			}
		}
	}
}

static int
scenario_is(const char *name, const char *expected)
{
	return strcmp(name, expected) == 0;
}

/* True when the four logged reads starting at start are one complete
 * RESPONSE0..3 raw window captured while cmd was the last command. */
static int
response_window_at(const struct sim *sim, uint32_t start, uint32_t cmd)
{
	uint32_t index;

	if (start + 4u > sim->response_read_count) {
		return 0;
	}
	for (index = 0; index < 4u; index++) {
		if (sim->response_read_cmd[start + index] != cmd) {
			return 0;
		}
		if (sim->response_read_offset[start + index] !=
		    GTA4L_EMMC_REG_RESPONSE + index * 4u) {
			return 0;
		}
	}
	return 1;
}

/* Find the n-th complete raw window captured while cmd was last issued.
 * Returns the start index or 0xFFFFFFFFu when absent. */
static uint32_t
response_window_find(const struct sim *sim, uint32_t cmd, uint32_t nth)
{
	uint32_t index;
	uint32_t seen = 0;

	for (index = 0; index + 4u <= sim->response_read_count; index++) {
		if (!response_window_at(sim, index, cmd)) {
			continue;
		}
		if (seen == nth) {
			return index;
		}
		seen++;
	}
	return 0xFFFFFFFFu;
}

/* CMD7 normal-status sequences for the bounded-trace semantics regressions.
 * A non-zero value raises BRR; zero clears it. Values past the end repeat the
 * last element, so a sequence can settle into a plateau. */
static const uint32_t cmd7_trace_plateau_script[] = {
	0u, 1u, 0u, 1u, 0u, 1u, 0u, 1u, 1u
};
static const uint32_t cmd7_trace_return_script[] = {
	1u, 0u, 1u, 0u, 1u, 0u, 1u, 0u, 0u
};
static const uint32_t cmd7_terminal_change_script[] = {
	0u, 1u
};

int
main(int argc, char **argv)
{
	const char *scenario = (argc > 1) ? argv[1] : "success";
	struct sim sim;
	struct gta4l_emmc_host host;
	uint8_t block0[GTA4L_EMMC_BLOCK_BYTES];
	uint8_t block1[GTA4L_EMMC_BLOCK_BYTES];
	uint8_t block5[GTA4L_EMMC_BLOCK_BYTES];
	uint8_t ext[GTA4L_EMMC_EXT_CSD_BYTES];
	static uint8_t mb_blocks[SIM_MB_TEST_BLOCKS * GTA4L_EMMC_BLOCK_BYTES];
	int result;
	int expected;

	sim_init(&sim);
	memset(&host, 0, sizeof(host));
	host.ops = &sim_ops;
	host.context = &sim;
	host.fixed_1v8 = 1;
	host.pad_pwr_switch = 1;

	if (scenario_is(scenario, "reset-timeout")) {
		sim.reset_timeout = 1;
	} else if (scenario_is(scenario, "clock-timeout")) {
		sim.clock_timeout = 1;
	} else if (scenario_is(scenario, "clock-branch-apps-fail")) {
		sim.apps_branch_fail = 1;
	} else if (scenario_is(scenario, "clock-branch-ahb-fail")) {
		sim.ahb_branch_fail = 1;
	} else if (scenario_is(scenario, "power-off")) {
		sim.power_off = 1;
	} else if (scenario_is(scenario, "power-no-request")) {
		sim.power_no_request = 1;
	} else if (scenario_is(scenario, "power-stuck")) {
		sim.power_stuck = 1;
	} else if (scenario_is(scenario, "power-io-only")) {
		sim.power_io_only = 1;
	} else if (scenario_is(scenario, "power-delayed-bus")) {
		sim.power_delayed_bus = 1;
	} else if (scenario_is(scenario, "power-bus-then-io")) {
		sim.power_io_delayed = 1;
	} else if (scenario_is(scenario, "power-wrong-io")) {
		sim.power_wrong_io = 1;
	} else if (scenario_is(scenario, "power-wrong-io-then-correct")) {
		sim.power_io_wrong_then_correct = 1;
	} else if (scenario_is(scenario, "power-late-bus-only")) {
		sim.power_late_bus_only = 1;
	} else if (scenario_is(scenario, "power-io-then-wrong")) {
		sim.power_io_then_wrong = 1;
	} else if (scenario_is(scenario, "power-opposite-io")) {
		sim.power_opposite_io = 1;
	} else if (scenario_is(scenario, "power-unchanged-1v8-bus-only")) {
		/* Model a controller that preserved VDD_180 across reset: the
		 * write changes nothing, so no IO request is due, but a
		 * standalone BUS_ON then implies the wrong level. */
		sim.preserve_host_control2 = 1;
		sim.sdhci[GTA4L_EMMC_REG_HOST_CONTROL2] =
		    (uint8_t)(SIM_CTRL_VDD_180 & 0xFFu);
		sim.sdhci[GTA4L_EMMC_REG_HOST_CONTROL2 + 1u] = 0;
	} else if (scenario_is(scenario, "power-wrong-io-on-ack")) {
		sim.power_io_on_ack = 1;
	} else if (scenario_is(scenario, "power-churn")) {
		sim.power_churn = 1;
	} else if (scenario_is(scenario, "data-sector-4k")) {
		sim.data_sector_4k = 1;
		sim_fill_ext_csd(&sim);
	} else if (scenario_is(scenario, "data-sector-reserved")) {
		/* A reserved DATA_SECTOR_SIZE encoding must not be treated
		 * as the 512-byte default. */
		sim.data_sector_size_code = 2;
		sim_fill_ext_csd(&sim);
	} else if (scenario_is(scenario, "rcg-busy")) {
		sim.rcg_busy = 1;
	} else if (scenario_is(scenario, "cmd-timeout")) {
		sim.cmd_timeout = 1;
	} else if (scenario_is(scenario, "cmd-crc")) {
		sim.cmd_crc_index = GTA4L_EMMC_CMD2;
	} else if (scenario_is(scenario, "cmd-crc-with-cc")) {
		/* A controller may report command complete together with the
		 * CRC error. The error must win and must not be masked. */
		sim.cmd_crc_index = GTA4L_EMMC_CMD2;
		sim.cmd_crc_with_cc = 1;
	} else if (scenario_is(scenario, "cmd-crc-snapshot")) {
		/* Clearing the error latch also invalidates RESPONSE0..3.
		 * The failure record must already hold the raw words. */
		sim.cmd_crc_index = GTA4L_EMMC_CMD2;
		sim.clear_response_on_w1c = 1;
	} else if (scenario_is(scenario, "cmd0-crc")) {
		sim.cmd_crc_index = GTA4L_EMMC_CMD0;
	} else if (scenario_is(scenario, "cmd1-crc")) {
		sim.cmd_crc_index = GTA4L_EMMC_CMD1;
	} else if (scenario_is(scenario, "cmd1-timeout-recovery-crc")) {
		/* The first CMD1 attempt times out while the card is busy;
		 * the retry succeeds, then CMD2 fails its CRC check. */
		sim.cmd1_timeouts_remaining = 1;
		sim.cmd_crc_index = GTA4L_EMMC_CMD2;
	} else if (scenario_is(scenario, "cmd1-not-ready-then-ready")) {
		/* Two completed but not-ready CMD1 replies, then a ready
		 * reply: three attempts, no timeouts, matching the
		 * 2026-10-31 hardware counters. */
		sim.cmd1_not_ready_remaining = 2;
	} else if (scenario_is(scenario, "cmd1-not-ready-then-ready-crc")) {
		/* The recorded hardware sequence: two not-ready CMD1 replies,
		 * a ready CMD1, then a CMD2 CRC failure. */
		sim.cmd1_not_ready_remaining = 2;
		sim.cmd_crc_index = GTA4L_EMMC_CMD2;
	} else if (scenario_is(scenario, "cmd1-crc-retire")) {
		/* Hypothetical controller model: the internal CRC source
		 * needs the full 8 us after the ready CMD1. The candidate
		 * delay must make this run pass. */
		sim.cmd1_crc_retire_us = (int)GTA4L_EMMC_CMD1_CRC_RETIRE_US;
	} else if (scenario_is(scenario, "cmd1-crc-retire-not-ready")) {
		/* Same model after two not-ready replies: only the final
		 * ready attempt must be followed by the 8 us delay. */
		sim.cmd1_not_ready_remaining = 2;
		sim.cmd1_crc_retire_us = (int)GTA4L_EMMC_CMD1_CRC_RETIRE_US;
	} else if (scenario_is(scenario, "cmd1-crc-retire-short")) {
		/* Negative control: the model needs one microsecond more
		 * than the candidate delay, so CMD2 must fail CMD_CRC. */
		sim.cmd1_crc_retire_us =
		    (int)(GTA4L_EMMC_CMD1_CRC_RETIRE_US + 1u);
	} else if (scenario_is(scenario, "cmd1-clear-invalidates-window")) {
		/* A controller may drop RESPONSE0..3 when the normal-status
		 * latch is cleared. The CMD1 window must already be captured;
		 * the pre-CMD2 window then reflects the cleared registers. */
		sim.clear_response_on_normal_w1c = 1;
	} else if (scenario_is(scenario, "cmd2-crc-distinct")) {
		/* Distinct post-CMD1 and failed-CMD2 windows. Seed stale
		 * words beyond RESPONSE0 so every captured word is checked. */
		sim_write32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_RESPONSE + 4u, 0x01020304u);
		sim_write32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_RESPONSE + 8u, 0x05060708u);
		sim_write32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_RESPONSE + 12u, 0x090A0B0Cu);
		sim.cmd_crc_index = GTA4L_EMMC_CMD2;
	} else if (scenario_is(scenario, "cmd2-crc-unchanged")) {
		/* CMD2 fails without updating the response registers: the
		 * pre-CMD2 and failed windows are identical. */
		sim.cmd_crc_index = GTA4L_EMMC_CMD2;
		sim.cmd2_keep_response = 1;
	} else if (scenario_is(scenario, "cmd2-crc-ocr-like")) {
		/* CMD2 fails after loading the hardware's OCR-like raw
		 * window (origin unconfirmed). */
		sim.cmd_crc_index = GTA4L_EMMC_CMD2;
		sim.cmd2_ocr_like_response = 1;
	} else if (scenario_is(scenario, "cmd1-timeout-recover")) {
		/* A single busy timeout is recovered by the retry; the run must
		 * finish with no failure record at all. */
		sim.cmd1_timeouts_remaining = 1;
	} else if (scenario_is(scenario, "cmd1-timeout-exhausted")) {
		/* Every CMD1 attempt times out: the final timeout must stay
		 * recorded instead of being retired with the earlier ones. */
		sim.cmd1_timeouts_remaining = 1000;
	} else if (scenario_is(scenario, "cmd-index")) {
		sim.cmd_index = 1;
	} else if (scenario_is(scenario, "vendor-mclk-hs400")) {
		uint32_t vendor = sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC);

		vendor = (vendor & ~SIM_VENDOR_MCLK_SEL_MASK) |
		    SIM_VENDOR_MCLK_SEL_HS400;
		sim_write32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC, vendor);
	} else if (scenario_is(scenario, "vendor-hc-select-en")) {
		uint32_t vendor = sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC);

		vendor |= SIM_VENDOR_HC_SELECT_IN_EN |
		    SIM_VENDOR_HC_SELECT_IN_MASK | SIM_VENDOR_CLK_PWRSAVE;
		sim_write32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC, vendor);
	} else if (scenario_is(scenario, "data-timeout-init")) {
		sim.data_timeout = 1;
	} else if (scenario_is(scenario, "data-crc-init")) {
		sim.data_crc = 1;
	} else if (scenario_is(scenario, "no-tc-init")) {
		sim.no_tc = 1;
	} else if (scenario_is(scenario, "no-brr-init")) {
		sim.no_brr = 1;
	} else if (scenario_is(scenario, "card-not-ready")) {
		sim.card_never_ready = 1;
	} else if (scenario_is(scenario, "bad-cid")) {
		sim.bad_cid = 1;
	} else if (scenario_is(scenario, "bad-csd")) {
		sim.bad_csd = 1;
	} else if (scenario_is(scenario, "cmd3-error")) {
		sim.cmd3_error = 1;
	} else if (scenario_is(scenario, "cmd2-normalized")) {
		/* Model the cmd2window observation: the controller stores the
		 * CID words in normalized order with the CRC trailer in the
		 * low byte of RESPONSE3. The raw capture must preserve that
		 * layout while the decoded CID is kept separate. */
		sim.cmd2_normalized_layout = 1;
	} else if (scenario_is(scenario, "cmd3-no-cc")) {
		/* CMD3 loads its R1 response but never raises CC. */
		sim.cmd3_no_cc = 1;
	} else if (scenario_is(scenario, "cmd3-late-cc")) {
		/* CC appears only after the final in-loop normal-status
		 * sample, i.e. in the fresh post-timeout read. */
		sim.cmd3_late_cc = 1;
	} else if (scenario_is(scenario, "cmd3-stale-cc")) {
		/* A stale CC survives the pre-CMD3 status clear. */
		sim.cmd3_stale_cc = 1;
	} else if (scenario_is(scenario, "cmd3-identify")) {
		/* CMD3 returns the Identify-state R1 status seen on
		 * hardware. */
		sim.cmd3_identify_status = 1;
	} else if (scenario_is(scenario, "cmd3-enable-readback")) {
		/* The status-enable readback does not reflect the value the
		 * core programmed, without changing the actual enables. */
		sim.enable_readback_wrong = 1;
	} else if (scenario_is(scenario, "cmd7-tc-only")) {
		/* Owner-transcribed failure: TC is set, CC is absent, and the
		 * retained response window looks like the CMD9 CSD tail. */
		sim.cmd7_cc_immediate = 0;
		sim.cmd7_tc_immediate = 1;
		sim.cmd7_keep_response = 1;
	} else if (scenario_is(scenario, "cmd7-fresh-window")) {
		/* Same TC-only timeout, but the response window changes at
		 * CMD7 submission. Equality of the pre and terminal windows
		 * is not required for, and does not prove, staleness. */
		sim.cmd7_cc_immediate = 0;
		sim.cmd7_tc_immediate = 1;
		sim.cmd7_fresh_response = 1;
		sim.cmd7_window[0] = 0x40400CF6u;
		sim.cmd7_window[1] = 0xDBFFEF0Fu;
		sim.cmd7_window[2] = 0x5903FF00u;
		sim.cmd7_window[3] = 0x00070000u;
	} else if (scenario_is(scenario, "cmd7-cc-then-tc")) {
		/* CC first; the busy release arrives 50 us later. Success
		 * requires both events; CC stays sticky over repeated polls. */
		sim.cmd7_cc_immediate = 1;
		sim.cmd7_tc_immediate = 0;
		sim.cmd7_tc_delay = 50;
	} else if (scenario_is(scenario, "cmd7-tc-then-cc")) {
		/* TC is visible first; CC appears on the second poll sample. */
		sim.cmd7_cc_immediate = 0;
		sim.cmd7_cc_delay = 10;
		sim.cmd7_tc_immediate = 1;
	} else if (scenario_is(scenario, "cmd7-simultaneous")) {
		/* Zero-length busy interval: CC and TC on the first sample. */
		sim.cmd7_cc_immediate = 1;
		sim.cmd7_tc_immediate = 1;
	} else if (scenario_is(scenario, "cmd7-terminal-change")) {
		/* The ignored status bit changes after the final poll sample
		 * so a reused poll sample is distinguishable from a fresh
		 * terminal sample. */
		sim.cmd7_cc_immediate = 1;
		sim.cmd7_tc_immediate = 1;
		sim.cmd7_script = 1;
		sim.cmd7_script_values = cmd7_terminal_change_script;
		sim.cmd7_script_length =
		    (uint32_t)(sizeof(cmd7_terminal_change_script) /
		    sizeof(cmd7_terminal_change_script[0]));
	} else if (scenario_is(scenario, "cmd7-last-poll")) {
		/* Both events become due on the last permitted poll. */
		sim.cmd7_cc_immediate = 0;
		sim.cmd7_cc_delay = (int)(GTA4L_EMMC_CMD_TIMEOUT_US -
		    GTA4L_EMMC_POLL_STEP_US);
		sim.cmd7_tc_immediate = 0;
		sim.cmd7_tc_delay = (int)(GTA4L_EMMC_CMD_TIMEOUT_US -
		    GTA4L_EMMC_POLL_STEP_US);
	} else if (scenario_is(scenario, "cmd7-delayed-release")) {
		/* A 200 us busy interval must block the next command. */
		sim.cmd7_cc_immediate = 1;
		sim.cmd7_tc_immediate = 0;
		sim.cmd7_tc_delay = 200;
	} else if (scenario_is(scenario, "cmd7-late-tc")) {
		/* TC first becomes due after the full in-budget window. */
		sim.cmd7_cc_immediate = 1;
		sim.cmd7_tc_immediate = 0;
		sim.cmd7_tc_delay = (int)GTA4L_EMMC_CMD_TIMEOUT_US;
	} else if (scenario_is(scenario, "cmd7-cc-only")) {
		/* CC is accepted but the busy release never arrives. */
		sim.cmd7_cc_immediate = 1;
		sim.cmd7_tc_immediate = 0;
	} else if (scenario_is(scenario, "cmd7-neither")) {
		/* Neither completion ever arrives. */
		sim.cmd7_cc_immediate = 0;
		sim.cmd7_tc_immediate = 0;
	} else if (scenario_is(scenario, "cmd7-stale-status")) {
		/* Stale CC/TC/error latched after the CMD9 tail clear must be
		 * removed by the CMD7 pre-command clear. */
		sim.cmd7_stale_status = 1;
	} else if (scenario_is(scenario, "cmd7-busy-error")) {
		/* Error latched together with both completions: error
		 * precedence must win. */
		sim.cmd7_cc_immediate = 1;
		sim.cmd7_tc_immediate = 1;
		sim.cmd7_error_delay = 0;
		sim.cmd7_error_status = GTA4L_EMMC_INT_ERR_CMD_TIMEOUT;
	} else if (scenario_is(scenario, "cmd7-late-error")) {
		/* Both completions arrive first; the error becomes due during
		 * the explicit response-read latency and is evaluated in the
		 * terminal sample. */
		sim.cmd7_cc_immediate = 1;
		sim.cmd7_tc_immediate = 1;
		sim.cmd7_access_latency_us = 1;
		sim.cmd7_error_delay = 1;
		sim.cmd7_error_status = GTA4L_EMMC_INT_ERR_CMD_TIMEOUT;
	} else if (scenario_is(scenario, "cmd7-terminal-error")) {
		/* Both completions arrive first. The error is not due at the
		 * first terminal error sample; it becomes due during the
		 * terminal command/argument/response reads and must be caught
		 * by the final error sample before the status clear. */
		sim.cmd7_cc_immediate = 1;
		sim.cmd7_tc_immediate = 1;
		sim.cmd7_access_latency_us = 1;
		sim.cmd7_error_delay = 2;
		sim.cmd7_error_status = GTA4L_EMMC_INT_ERR_CMD_TIMEOUT;
	} else if (scenario_is(scenario, "cmd7-data-timeout-after-cc")) {
		/* DATA_TIMEOUT after CC and before TC maps to a failure. */
		sim.cmd7_cc_immediate = 1;
		sim.cmd7_tc_immediate = 0;
		sim.cmd7_tc_delay = 100;
		sim.cmd7_error_delay = 50;
		sim.cmd7_error_status = GTA4L_EMMC_INT_ERR_DATA_TIMEOUT;
	} else if (scenario_is(scenario, "cmd7-data-crc-after-cc")) {
		/* DATA_CRC after CC and before TC maps to a failure. */
		sim.cmd7_cc_immediate = 1;
		sim.cmd7_tc_immediate = 0;
		sim.cmd7_tc_delay = 100;
		sim.cmd7_error_delay = 50;
		sim.cmd7_error_status = GTA4L_EMMC_INT_ERR_DATA_CRC;
	} else if (scenario_is(scenario, "cmd0-irrelevant-tc") ||
	    scenario_is(scenario, "cmd2-irrelevant-tc") ||
	    scenario_is(scenario, "cmd3-irrelevant-tc")) {
		/* A non-busy response type must not treat TC as completion. */
		sim.irrelevant_tc_valid = 1;
		sim.irrelevant_tc_index =
		    scenario_is(scenario, "cmd0-irrelevant-tc") ?
		    GTA4L_EMMC_CMD0 :
		    (scenario_is(scenario, "cmd2-irrelevant-tc") ?
		    GTA4L_EMMC_CMD2 : GTA4L_EMMC_CMD3);
	} else if (scenario_is(scenario, "cmd7-trace-overflow")) {
		/* Neither completion appears and an ignored normal-status bit
		 * changes on every sample, so the bounded trace overflows. */
		sim.cmd7_cc_immediate = 0;
		sim.cmd7_tc_immediate = 0;
		sim.cmd7_toggle_status = 1;
	} else if (scenario_is(scenario, "cmd7-trace-plateau")) {
		/* Alternate past the bounded store, then stop changing. Only
		 * observed changes may advance the overflow counter. */
		sim.cmd7_cc_immediate = 0;
		sim.cmd7_tc_immediate = 0;
		sim.cmd7_script = 1;
		sim.cmd7_script_values = cmd7_trace_plateau_script;
		sim.cmd7_script_length =
		    (uint32_t)(sizeof(cmd7_trace_plateau_script) /
		    sizeof(cmd7_trace_plateau_script[0]));
	} else if (scenario_is(scenario, "cmd7-trace-return")) {
		/* After the store fills, a sample that differs from the
		 * previous observation but equals the last stored entry is
		 * still an observed change, then the sequence plateaus. */
		sim.cmd7_cc_immediate = 0;
		sim.cmd7_tc_immediate = 0;
		sim.cmd7_script = 1;
		sim.cmd7_script_values = cmd7_trace_return_script;
		sim.cmd7_script_length =
		    (uint32_t)(sizeof(cmd7_trace_return_script) /
		    sizeof(cmd7_trace_return_script[0]));
	} else if (scenario_is(scenario, "small-card")) {
		sim.sector_count = 4096u;
		sim_fill_ext_csd(&sim);
	} else if (scenario_is(scenario, "partition-access")) {
		sim.partition_config = 1u;
		sim_fill_ext_csd(&sim);
	} else if (scenario_is(scenario, "cmd9-late-error")) {
		/* CMD9's response is intact, but an error becomes visible
		 * while it is being read. The non-busy path must evaluate the
		 * latch again before the status clear. */
		sim.cmd_late_error_index = GTA4L_EMMC_CMD9;
	} else if (scenario_is(scenario, "signaling-3v3")) {
		host.fixed_1v8 = 0;
		host.pad_pwr_switch = 0;
	} else if (scenario_is(scenario, "pad-switch-off")) {
		host.pad_pwr_switch = 0;
	} else if (scenario_is(scenario, "version")) {
		sim_write32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_MCI_VERSION, 0x20000000u);
	} else if (scenario_is(scenario, "multi-late-error")) {
		/* The error becomes visible during the fourth block's drain,
		 * so four complete blocks are observed before it is sampled. */
		sim.data_late_error_words =
		    (int)((GTA4L_EMMC_BLOCK_BYTES / 4u) * 4u);
	} else if (scenario_is(scenario, "multi-drain-tc")) {
		/* Every sector drains, but completion arrives only inside the
		 * post-bound window. */
		sim.mb_tc_delay_us = (int)GTA4L_EMMC_DATA_TIMEOUT_US + 1000;
	} else if (scenario_is(scenario, "multi-drain-brr")) {
		/* The last sector becomes buffer-read ready only inside the
		 * post-bound window; completion follows its drain. */
		sim.mb_last_brr_delay_us =
		    (int)GTA4L_EMMC_DATA_TIMEOUT_US + 1000;
	} else if (scenario_is(scenario, "multi-drain-error")) {
		/* Completion never arrives and a data CRC error becomes
		 * visible inside the post-bound window. */
		sim.mb_no_tc = 1;
		sim.mb_error_delay_us = (int)GTA4L_EMMC_DATA_TIMEOUT_US + 1000;
		sim.mb_error_status = GTA4L_EMMC_INT_ERR_DATA_CRC;
	} else if (scenario_is(scenario, "multi-drain-stall")) {
		/* Attempt-2 replay: every sector drains and completion never
		 * arrives, so the secondary window is exhausted. */
		sim.mb_no_tc = 1;
	} else if (scenario_is(scenario, "multi-stop-tc-only")) {
		sim.mb_no_cc = 1;
	} else if (scenario_is(scenario, "multi-stop-late-cc")) {
		sim.mb_late_cc = 1;
	} else if (scenario_is(scenario, "multi-stop-busy-timeout")) {
		sim.mb_stop_no_tc = 1;
	} else if (scenario_is(scenario, "multi-stop-crc")) {
		sim.cmd_crc_index = GTA4L_EMMC_CMD12;
	} else if (scenario_is(scenario, "multi-stop-late-error")) {
		sim.cmd_late_error_index = GTA4L_EMMC_CMD12;
	} else if (scenario_is(scenario, "multi-card-status")) {
		sim.mb_response_error = 1;
	}

	sim.trace_armed = 1;
	result = gta4l_emmc_init(&host);

	if (scenario_is(scenario, "success") ||
	    scenario_is(scenario, "read-arg") ||
	    scenario_is(scenario, "read-without-init") ||
	    scenario_is(scenario, "read-data-timeout") ||
	    scenario_is(scenario, "read-data-crc") ||
	    scenario_is(scenario, "read-late-fifo-error") ||
	    scenario_is(scenario, "read-no-tc") ||
	    scenario_is(scenario, "read-no-brr") ||
	    scenario_is(scenario, "read-repeat") ||
	    scenario_is(scenario, "read-ext-csd") ||
	    scenario_is(scenario, "read-out-of-range") ||
	    scenario_is(scenario, "read-last-block") ||
	    scenario_is(scenario, "multi-args") ||
	    scenario_is(scenario, "multi-without-init") ||
	    scenario_is(scenario, "multi-last-block") ||
	    scenario_is(scenario, "multi-success") ||
	    scenario_is(scenario, "multi-repeat") ||
	    scenario_is(scenario, "multi-data-timeout") ||
	    scenario_is(scenario, "multi-data-crc") ||
	    scenario_is(scenario, "multi-late-error") ||
	    scenario_is(scenario, "multi-drain-tc") ||
	    scenario_is(scenario, "multi-drain-brr") ||
	    scenario_is(scenario, "multi-drain-error") ||
	    scenario_is(scenario, "multi-drain-stall") ||
	    scenario_is(scenario, "multi-stop-tc-only") ||
	    scenario_is(scenario, "multi-stop-late-cc") ||
	    scenario_is(scenario, "multi-stop-busy-timeout") ||
	    scenario_is(scenario, "multi-stop-crc") ||
	    scenario_is(scenario, "multi-stop-late-error") ||
	    scenario_is(scenario, "multi-card-status") ||
	    scenario_is(scenario, "power-delayed-bus") ||
	    scenario_is(scenario, "power-bus-then-io") ||
	    scenario_is(scenario, "power-wrong-io-then-correct") ||
	    scenario_is(scenario, "pad-switch-off") ||
	    scenario_is(scenario, "signaling-3v3") ||
	    scenario_is(scenario, "vendor-mclk-hs400") ||
	    scenario_is(scenario, "vendor-hc-select-en") ||
	    scenario_is(scenario, "cmd1-not-ready-then-ready") ||
	    scenario_is(scenario, "cmd1-crc-retire") ||
	    scenario_is(scenario, "cmd1-crc-retire-not-ready") ||
	    scenario_is(scenario, "cmd1-clear-invalidates-window") ||
	    scenario_is(scenario, "reinit-clears-windows") ||
	    scenario_is(scenario, "cmd2-normalized") ||
	    scenario_is(scenario, "cmd3-identify") ||
	    scenario_is(scenario, "cmd3-stale-cc") ||
	    scenario_is(scenario, "cmd3-enable-readback") ||
	    scenario_is(scenario, "cmd7-cc-then-tc") ||
	    scenario_is(scenario, "cmd7-tc-then-cc") ||
	    scenario_is(scenario, "cmd7-simultaneous") ||
	    scenario_is(scenario, "cmd7-terminal-change") ||
	    scenario_is(scenario, "cmd7-last-poll") ||
	    scenario_is(scenario, "cmd7-delayed-release") ||
	    scenario_is(scenario, "cmd7-stale-status") ||
	    scenario_is(scenario, "cmd0-irrelevant-tc") ||
	    scenario_is(scenario, "cmd2-irrelevant-tc") ||
	    scenario_is(scenario, "cmd3-irrelevant-tc") ||
	    scenario_is(scenario, "cmd1-timeout-recover")) {
		check(result == GTA4L_EMMC_OK, "initialization failed");
		if (result != GTA4L_EMMC_OK) {
			fprintf(stderr, "init result %d\n", result);
			return 1;
		}
	}

	if (scenario_is(scenario, "success")) {
		check(host.rca == GTA4L_EMMC_DEFAULT_RCA,
		    "RCA is not the assigned value");
		check(sim.cmd3_argument == (GTA4L_EMMC_DEFAULT_RCA << 16),
		    "CMD3 argument did not carry the RCA");
		check(host.capacity_blocks == 61071360u, "capacity mismatch");
		check(host.ext_csd_rev == 8u, "EXT_CSD revision mismatch");
		check(host.bus_width == 2u, "bus width mismatch");
		check(host.partition_config == 0u,
		    "partition access is not the user area");
		check(host.data_sector_size_4k == 0u,
		    "unexpected 4 KiB data sector size");
		check(host.cid[0] == 0x15010044u &&
		    host.cid[1] == 0x5836384Du &&
		    host.cid[2] == 0x42009005u &&
		    host.cid[3] == 0xF4661900u,
		    "CID long-response normalization mismatch");
		check(host.cmd2_capture_valid == 1u,
		    "successful init did not capture the CMD2 window");
		check(host.cmd2_raw_response[0] == 0x05F46619u &&
		    host.cmd2_raw_response[1] == 0x4D420090u &&
		    host.cmd2_raw_response[2] == 0x44583638u &&
		    host.cmd2_raw_response[3] == 0x00150100u,
		    "CMD2 raw capture is not the standard SDHCI layout");
		check(host.pre_cmd3_capture_valid == 1u,
		    "successful init did not capture the pre-CMD3 state");
		check(host.poll_capture_valid == 1u,
		    "successful init did not record the CMD3 poll");
		check(host.poll_index == GTA4L_EMMC_CMD3,
		    "recorded poll is not for CMD3");
		check(host.poll_iterations == 1u,
		    "immediately-complete CMD3 poll did not stop on the first "
		    "sample");
		check((host.poll_first_normal & GTA4L_EMMC_INT_CC) != 0,
		    "CMD3 poll did not observe the completion");
		check(host.timeout_capture_valid == 0u,
		    "successful CMD3 recorded a timeout capture");
		check(host.csd[0] == 0xD0270132u &&
		    host.csd[1] == 0x0F5903FFu &&
		    host.csd[2] == 0xF6DBFFEFu &&
		    host.csd[3] == 0x8E404000u,
		    "CSD long-response normalization mismatch");
		check(sim.gcc[SIM_GCC_APPS_CFG / 4u] ==
		    0x00002017u, "RCG CFG is not 400 kHz");
		check(sim.gcc[SIM_GCC_APPS_M / 4u] == 0x00000001u,
		    "RCG M mismatch");
		check(sim.gcc[SIM_GCC_APPS_N / 4u] == 0x000000FCu,
		    "RCG N mismatch");
		check(sim.gcc[SIM_GCC_APPS_D / 4u] == 0x000000FBu,
		    "RCG D mismatch");
		check(sim.tlmm[0] == 0x00009FE4u,
		    "SDC1 pad config mismatch");
		check(sim.power_ack == (GTA4L_EMMC_PWRCTL_ACK_BUS |
		    GTA4L_EMMC_PWRCTL_ACK_IO), "power ack mismatch");
		check(sim.card_clock_enabled == 1, "card clock not enabled");
		check(sim.card_clock_before_power == 0,
		    "card clock enabled before power");
		check(sim.card_clock_time_us >=
		    sim.power_ack_time_us + SIM_POWER_SETTLE_US,
		    "card clock enabled before the power settle delay");
		check(sim.access_violation == 0,
		    "core used a wrong register width or unaligned access");
		check(sim.command_error == 0,
		    "core sent a command with wrong encoding or state");
		check(sim.normal_enable == (GTA4L_EMMC_INT_CC |
		    GTA4L_EMMC_INT_TC | GTA4L_EMMC_INT_BRR),
		    "normal status enables are not set for polling");
		check(sim.error_enable == GTA4L_EMMC_INT_ERR_MASK,
		    "error status enables are not set for polling");
		check(sim.normal_signal == 0u && sim.error_signal == 0u,
		    "interrupt signalling is not suppressed");
		check((sim_read16(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_HOST_CONTROL2) & SIM_CTRL_VDD_180) != 0,
		    "1.8 V signalling is not selected");
		check((sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) &
		    (SIM_PAD_PWR_SWITCH_EN | SIM_PAD_PWR_SWITCH)) ==
		    (SIM_PAD_PWR_SWITCH_EN | SIM_PAD_PWR_SWITCH),
		    "fixed-1.8 V pad power switch is not selected");
		check((sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_DLL_CONFIG) &
		    (SIM_DLL_RST | SIM_DLL_PDN)) ==
		    (SIM_DLL_RST | SIM_DLL_PDN),
		    "low-speed DLL is not reset and powered down");
		check((sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) &
		    SIM_VENDOR_MCLK_SEL_MASK) ==
		    SIM_VENDOR_MCLK_SEL_DFLT,
		    "vendor MCLK is not the low-speed default");
		check((sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) &
		    (SIM_VENDOR_HC_SELECT_IN_EN |
		    SIM_VENDOR_HC_SELECT_IN_MASK)) == 0,
		    "HC_SELECT_IN override was not cleared");
		check((sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) &
		    SIM_VENDOR_CLK_PWRSAVE) == 0,
		    "clock power-save is still set");
		check(sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) ==
		    (SIM_VENDOR_SPEC_POR | SIM_PAD_PWR_SWITCH_EN |
		    SIM_PAD_PWR_SWITCH),
		    "vendor register is not the normalized POR value");
		check((sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC3) &
		    SIM_VENDOR3_PWRSAVE_DLL) == 0,
		    "CDCLP533 DLL power-save bit is still set");
		check(sim.bcr_reset == 1,
		    "GCC SDCC1 block reset was not asserted");
		check(sim.bcr_deasserted == 1,
		    "GCC SDCC1 block reset was not deasserted");
		check(sim.bcr_assert_pending == 0,
		    "GCC SDCC1 block reset was left asserted");
		check(sim.bcr_reasserted == 0,
		    "GCC SDCC1 block reset was asserted twice");
		check((sim.bcr_short_pulse == 0) &&
		    (sim.bcr_pulse_us >= SIM_BCR_DELAY_US),
		    "GCC SDCC1 reset pulse is shorter than 200 us");
		check(sim.sdhci_before_bcr_deassert == 0,
		    "SDHCI was accessed before the block reset deassert");
		check(sim.sdhci_before_clocks == 0,
		    "SDHCI was accessed before both branch clocks were verified");
		check(sim.dll_rst_written == 1,
		    "DLL reset was never requested");
		check(sim.dll_pdn_written == 1,
		    "DLL power-down was never requested");
		check(sim.dll_pdn_before_rst == 0,
		    "DLL power-down was requested before the DLL reset");
		check(host.last_step == GTA4L_EMMC_STEP_READY,
		    "protocol step did not reach READY");
		check(host.fail_valid == 0,
		    "successful init recorded a command failure");
		check(host.cmd1_attempts == 1u,
		    "successful init did not report one CMD1 attempt");
		check(host.cmd1_timeouts == 0u,
		    "successful init reported a CMD1 timeout");
		check(host.ocr == (GTA4L_EMMC_OCR_VALUE | 0x80000000u),
		    "successful init did not cache the ready OCR");
		check(sim.cmd0_time_us >=
		    sim.card_clock_time_us + SIM_CARD_CLOCK_SETTLE_US +
		    SIM_CMD0_DELAY_US,
		    "CMD0 was sent before the card-clock/CMD0 delays");
		check(sim.cmd1_time_us >=
		    sim.cmd0_time_us + SIM_CMD0_POST_DELAY_US,
		    "CMD1 was sent before the CMD0 post delay");
		check(sim.last_cmd == GTA4L_EMMC_CMD8,
		    "initialization did not end with CMD8");
		check(sim.last_cmd_argument == 0u,
		    "CMD8 argument is not zero");
		check(sim.log_count > 0, "no log events");

		result = gta4l_emmc_read_block(&host, 0, block0);
		check(result == GTA4L_EMMC_OK, "block 0 read failed");
		if (result == GTA4L_EMMC_OK) {
			check_block(&sim, 0, block0, "block 0");
			check(sim.last_cmd_argument == 0u,
			    "CMD17 block 0 argument mismatch");
		}
		result = gta4l_emmc_read_block(&host, 1, block1);
		check(result == GTA4L_EMMC_OK, "block 1 read failed");
		if (result == GTA4L_EMMC_OK) {
			check_block(&sim, 1, block1, "block 1");
			check(sim.last_cmd_argument == 1u,
			    "CMD17 block 1 argument mismatch");
		}
		check(sim.last_cmd == GTA4L_EMMC_CMD17,
		    "last command is not CMD17");
	} else if (scenario_is(scenario, "signaling-3v3")) {
		check((sim_read16(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_HOST_CONTROL2) & SIM_CTRL_VDD_180) == 0,
		    "3.3 V signalling unexpectedly has VDD_180 set");
		check((sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) &
		    (SIM_PAD_PWR_SWITCH_EN | SIM_PAD_PWR_SWITCH)) == 0,
		    "pad power switch is set for 3.3 V signalling");
		/* With no VDD_180 transition the controller raises only
		 * BUS_ON; Linux still acknowledges BUS_SUCCESS and
		 * IO_SUCCESS for that implicit IO-high completion. */
		check(sim.power_ack == (GTA4L_EMMC_PWRCTL_ACK_BUS |
		    GTA4L_EMMC_PWRCTL_ACK_IO),
		    "bus-only power request was not acknowledged as BUS+IO");
		check(sim.pwr_status == 0,
		    "a power/IO request was left pending");
	} else if (scenario_is(scenario, "pad-switch-off")) {
		check((sim_read16(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_HOST_CONTROL2) & SIM_CTRL_VDD_180) != 0,
		    "1.8 V signalling is not selected");
		check((sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) &
		    (SIM_PAD_PWR_SWITCH_EN | SIM_PAD_PWR_SWITCH)) == 0,
		    "pad power switch is set with the policy disabled");
	} else if (scenario_is(scenario, "vendor-mclk-hs400")) {
		check((sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) &
		    SIM_VENDOR_MCLK_SEL_MASK) ==
		    SIM_VENDOR_MCLK_SEL_DFLT,
		    "HS400 MCLK selection was not restored to the default");
		check(sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) ==
		    (SIM_VENDOR_SPEC_POR | SIM_PAD_PWR_SWITCH_EN |
		    SIM_PAD_PWR_SWITCH),
		    "vendor register is not the normalized POR value");
	} else if (scenario_is(scenario, "vendor-hc-select-en")) {
		check((sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) &
		    (SIM_VENDOR_HC_SELECT_IN_EN |
		    SIM_VENDOR_HC_SELECT_IN_MASK)) == 0,
		    "enabled HC_SELECT_IN override was not cleared");
		check((sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) &
		    SIM_VENDOR_CLK_PWRSAVE) == 0,
		    "clock power-save was not cleared");
		check(sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_VENDOR_SPEC) ==
		    (SIM_VENDOR_SPEC_POR | SIM_PAD_PWR_SWITCH_EN |
		    SIM_PAD_PWR_SWITCH),
		    "vendor register is not the normalized POR value");
	} else if (scenario_is(scenario, "cmd1-timeout-recover")) {
		check(sim.cmd1_timeouts_remaining == 0u,
		    "the injected CMD1 timeout was never consumed");
		check(host.fail_valid == 0,
		    "a recovered CMD1 timeout left a failure record");
		check(host.cmd1_attempts == 2u,
		    "recovered CMD1 run did not report two attempts");
		check(host.cmd1_timeouts == 1u,
		    "recovered CMD1 run did not report one timeout");
		check(host.ocr == (GTA4L_EMMC_OCR_VALUE | 0x80000000u),
		    "recovered CMD1 run did not cache the ready OCR");
		check(host.last_step == GTA4L_EMMC_STEP_READY,
		    "recovered CMD1 run did not reach READY");
		check(host.capacity_blocks == 61071360u,
		    "recovered CMD1 run did not enumerate the card");
	} else if (scenario_is(scenario, "cmd1-not-ready-then-ready")) {
		check(sim.cmd1_not_ready_remaining == 0u,
		    "the injected not-ready CMD1 replies were not consumed");
		check(host.cmd1_attempts == 3u,
		    "not-ready CMD1 run did not report three attempts");
		check(host.cmd1_timeouts == 0u,
		    "not-ready CMD1 run reported a timeout");
		check(host.ocr == (GTA4L_EMMC_OCR_VALUE | 0x80000000u),
		    "not-ready CMD1 run did not cache the ready OCR");
		check(host.cmd1_response_valid == 1u,
		    "ready CMD1 raw window was not marked valid");
		check(host.pre_cmd2_response_valid == 1u,
		    "pre-CMD2 raw window was not marked valid");
		check(host.cmd1_response[0] ==
		    (GTA4L_EMMC_OCR_VALUE | 0x80000000u),
		    "ready CMD1 raw window does not hold the OCR");
		check(host.last_step == GTA4L_EMMC_STEP_READY,
		    "not-ready CMD1 run did not reach READY");
		check(host.capacity_blocks == 61071360u,
		    "not-ready CMD1 run did not enumerate the card");
	} else if (scenario_is(scenario, "cmd1-not-ready-then-ready-crc")) {
		check(sim.cmd1_not_ready_remaining == 0u,
		    "not-ready+CRC run did not consume the injected replies");
		check(host.cmd1_attempts == 3u,
		    "not-ready+CRC run did not report three attempts");
		check(host.cmd1_timeouts == 0u,
		    "not-ready+CRC run reported a timeout");
		check(host.cmd1_response_valid == 1u,
		    "not-ready+CRC run did not mark the final CMD1 window "
		    "valid");
		check(host.pre_cmd2_response_valid == 1u,
		    "not-ready+CRC run did not capture the pre-CMD2 window");
		check(host.cmd1_response[0] ==
		    (GTA4L_EMMC_OCR_VALUE | 0x80000000u),
		    "not-ready+CRC run lost the ready CMD1 OCR");
		check(host.fail_index == GTA4L_EMMC_CMD2,
		    "not-ready+CRC run did not capture CMD2");
		check(host.fail_command == 0x0209u,
		    "not-ready+CRC CMD2 encoding mismatch");
		check(host.fail_step == GTA4L_EMMC_STEP_CMD1,
		    "not-ready+CRC failure step is not CMD1");
		check(host.fail_response[0] !=
		    host.pre_cmd2_response[0] ||
		    host.fail_response[1] !=
		    host.pre_cmd2_response[1] ||
		    host.fail_response[2] !=
		    host.pre_cmd2_response[2] ||
		    host.fail_response[3] !=
		    host.pre_cmd2_response[3],
		    "not-ready+CRC failed window equals the pre-CMD2 window");
	} else if (scenario_is(scenario, "cmd1-crc-retire")) {
		check(sim.cmd1_crc_retire_us ==
		    (int)GTA4L_EMMC_CMD1_CRC_RETIRE_US,
		    "CRC-retire timing model was not active");
		check(host.cmd1_attempts == 1u,
		    "CRC-retire run did not use one CMD1 attempt");
		check(host.cmd1_timeouts == 0u,
		    "CRC-retire run reported a CMD1 timeout");
		check(host.ocr == (GTA4L_EMMC_OCR_VALUE | 0x80000000u),
		    "CRC-retire run did not cache the ready OCR");
		check(host.last_step == GTA4L_EMMC_STEP_READY,
		    "CRC-retire run did not reach READY");
		check(host.capacity_blocks == 61071360u,
		    "CRC-retire run did not enumerate the card");
		check(sim.cmd1_crc_retire_pending == 0,
		    "CRC-retire model did not arm and retire");
		check(sim.cmd2_time_us >= sim.cmd1_ready_time_us +
		    GTA4L_EMMC_CMD1_CRC_RETIRE_US,
		    "CMD2 was issued before the CRC-retire interval");
	} else if (scenario_is(scenario, "cmd1-crc-retire-not-ready")) {
		check(sim.cmd1_crc_retire_us ==
		    (int)GTA4L_EMMC_CMD1_CRC_RETIRE_US,
		    "not-ready CRC-retire timing model was not active");
		check(sim.cmd1_not_ready_remaining == 0u,
		    "not-ready CRC-retire run did not consume the replies");
		check(host.cmd1_attempts == 3u,
		    "not-ready CRC-retire run did not use three attempts");
		check(sim.cmd1_attempt_count == 3u,
		    "not-ready CRC-retire command count is not three");
		check(sim.cmd1_attempt_time_us[1] -
		    sim.cmd1_attempt_time_us[0] == 1000u,
		    "first not-ready CMD1 gap was not 1000 us");
		check(sim.cmd1_attempt_time_us[2] -
		    sim.cmd1_attempt_time_us[1] == 1000u,
		    "second not-ready CMD1 gap was not 1000 us");
		check(sim.cmd2_time_us >= sim.cmd1_ready_time_us +
		    GTA4L_EMMC_CMD1_CRC_RETIRE_US,
		    "CMD2 was issued before the CRC-retire interval");
		check(host.capacity_blocks == 61071360u,
		    "not-ready CRC-retire run did not enumerate the card");
		check(sim.cmd1_crc_retire_pending == 0,
		    "not-ready CRC-retire model did not arm and retire");
	} else if (scenario_is(scenario, "cmd1-clear-invalidates-window")) {
		check(host.cmd1_response_valid == 1u,
		    "CMD1 window was not marked valid before the clear");
		check(host.cmd1_response[0] ==
		    (GTA4L_EMMC_OCR_VALUE | 0x80000000u),
		    "CMD1 window did not survive the later status clear");
		check(host.pre_cmd2_response_valid == 1u,
		    "pre-CMD2 window was not marked valid");
		check(host.pre_cmd2_response[0] == 0u &&
		    host.pre_cmd2_response[1] == 0u &&
		    host.pre_cmd2_response[2] == 0u &&
		    host.pre_cmd2_response[3] == 0u,
		    "pre-CMD2 window was not cleared by the normal-status "
		    "write");
		check(host.last_step == GTA4L_EMMC_STEP_READY,
		    "cleared-window run did not reach READY");
	} else if (scenario_is(scenario, "reinit-clears-windows")) {
		check(host.cmd1_response_valid == 1u &&
		    host.pre_cmd2_response_valid == 1u,
		    "initial run did not produce both response windows");
		check(host.cmd7_pre_capture_valid == 1u &&
		    host.cmd7_poll_capture_valid == 1u &&
		    host.cmd7_term_capture_valid == 1u,
		    "initial run did not produce the CMD7 captures");
		check(host.cmd7_trace_count != 0u,
		    "initial run did not record a CMD7 trace");
		result = gta4l_emmc_controller_init(&host);
		check(result == GTA4L_EMMC_OK, "reinitialization failed");
		check(host.cmd1_response_valid == 0u,
		    "reinit did not clear the CMD1 window validity");
		check(host.pre_cmd2_response_valid == 0u,
		    "reinit did not clear the pre-CMD2 window validity");
		check(host.cmd7_pre_capture_valid == 0u &&
		    host.cmd7_poll_capture_valid == 0u &&
		    host.cmd7_term_capture_valid == 0u,
		    "reinit did not clear the CMD7 capture validity");
		check(host.cmd7_cc_valid == 0u && host.cmd7_tc_valid == 0u,
		    "reinit left the CMD7 completion flags");
		check(host.cmd7_trace_count == 0u &&
		    host.cmd7_trace_overflow == 0u,
		    "reinit left the CMD7 trace");
		check(host.cmd1_response[0] == 0u &&
		    host.cmd1_response[1] == 0u &&
		    host.cmd1_response[2] == 0u &&
		    host.cmd1_response[3] == 0u,
		    "reinit left CMD1 window data");
		check(host.pre_cmd2_response[0] == 0u &&
		    host.pre_cmd2_response[1] == 0u &&
		    host.pre_cmd2_response[2] == 0u &&
		    host.pre_cmd2_response[3] == 0u,
		    "reinit left pre-CMD2 window data");
		check(host.cmd7_pre_response[0] == 0u &&
		    host.cmd7_pre_response[1] == 0u &&
		    host.cmd7_pre_response[2] == 0u &&
		    host.cmd7_pre_response[3] == 0u &&
		    host.cmd7_term_response[0] == 0u &&
		    host.cmd7_term_response[1] == 0u &&
		    host.cmd7_term_response[2] == 0u &&
		    host.cmd7_term_response[3] == 0u,
		    "reinit left CMD7 response windows");
	} else if (scenario_is(scenario, "power-delayed-bus") ||
	    scenario_is(scenario, "power-bus-then-io")) {
		check(sim.bus_acked == 1,
		    "bus power-on completion was not acknowledged");
		check(sim.pwr_status == 0,
		    "a power/IO request was left pending");
		check(sim.card_clock_enabled == 1, "card clock not enabled");
		if (scenario_is(scenario, "power-bus-then-io")) {
			/* The IO request arrives after the old two-read
			 * quiet window; the core must still handle it. */
			check(sim.io_delayed_emitted == 1,
			    "late IO request was never handled");
			check(sim.io_poll_count >= 5u,
			    "late IO request did not arrive after the quiet window");
		}
	} else if (scenario_is(scenario, "power-wrong-io-then-correct")) {
		check(sim.bus_acked == 1,
		    "bus power-on completion was not acknowledged");
		check(sim.io_delayed_emitted == 1,
		    "correct-level IO request was never handled");
		check(sim.pwr_status == 0,
		    "a power/IO request was left pending");
		check(sim.card_clock_enabled == 1, "card clock not enabled");
		check(sim.power_ack == GTA4L_EMMC_PWRCTL_ACK_IO,
		    "last ack was not the correct-level IO acknowledgement");
	} else if (scenario_is(scenario, "power-wrong-io")) {
		check(result == GTA4L_EMMC_ERR_POWER,
		    "wrong-level IO request was accepted");
		check(sim.card_clock_enabled == 0,
		    "card clock enabled after a wrong-level IO request");
	} else if (scenario_is(scenario, "power-late-bus-only") ||
	    scenario_is(scenario, "power-io-then-wrong") ||
	    scenario_is(scenario, "power-opposite-io")) {
		check(result == GTA4L_EMMC_ERR_POWER,
		    "a later or contradictory IO level was accepted");
		check(sim.card_clock_enabled == 0,
		    "card clock enabled after a later/contradictory IO level");
		if (scenario_is(scenario, "power-late-bus-only")) {
			check(sim.bus_delayed_emitted == 1,
			    "late standalone bus request was never emitted");
		}
		if (scenario_is(scenario, "power-io-then-wrong")) {
			check(sim.io_delayed_emitted == 1,
			    "late wrong-level IO request was never emitted");
		}
	} else if (scenario_is(scenario, "power-unchanged-1v8-bus-only")) {
		check(result == GTA4L_EMMC_ERR_POWER,
		    "unchanged VDD_180 accepted a wrong IO level");
		check(sim.card_clock_enabled == 0,
		    "card clock enabled after an unchanged-bit wrong level");
	} else if (scenario_is(scenario, "power-wrong-io-on-ack")) {
		check(result == GTA4L_EMMC_ERR_POWER,
		    "wrong-level IO request raised on ack was accepted");
		check(sim.ack_io_injected == 1,
		    "ack-time IO request was never injected");
		check(sim.card_clock_enabled == 0,
		    "card clock enabled after an ack-time wrong level");
	} else if (scenario_is(scenario, "power-churn")) {
		check(result == GTA4L_EMMC_ERR_POWER,
		    "churned power loop did not fail");
		check(sim.churn_count > 100u,
		    "churn did not keep requests pending");
	} else if (scenario_is(scenario, "read-arg")) {
		result = gta4l_emmc_read_block(&host, 0x100000000ULL, block0);
		check(result == GTA4L_EMMC_ERR_ARG, "overflow block was accepted");
	} else if (scenario_is(scenario, "read-out-of-range")) {
		result = gta4l_emmc_read_block(&host, host.capacity_blocks,
		    block0);
		check(result == GTA4L_EMMC_ERR_ARG,
		    "block at capacity was accepted");
	} else if (scenario_is(scenario, "read-last-block")) {
		result = gta4l_emmc_read_block(&host,
		    (uint64_t)host.capacity_blocks - 1u, block0);
		check(result == GTA4L_EMMC_OK, "last block read failed");
		if (result == GTA4L_EMMC_OK) {
			check_block(&sim, host.capacity_blocks - 1u, block0,
			    "last block");
		}
	} else if (scenario_is(scenario, "read-without-init")) {
		struct gta4l_emmc_host empty;

		memset(&empty, 0, sizeof(empty));
		empty.ops = &sim_ops;
		empty.context = &sim;
		result = gta4l_emmc_read_block(&empty, 0, block0);
		check(result == GTA4L_EMMC_ERR_ARG,
		    "read without init was accepted");
	} else if (scenario_is(scenario, "read-data-timeout")) {
		sim.data_timeout = 1;
		result = gta4l_emmc_read_block(&host, 0, block0);
		check(result == GTA4L_EMMC_ERR_DATA_TIMEOUT,
		    "data timeout not reported");
		check(host.fail_valid == 1, "read timeout was not captured");
		check(host.fail_index == GTA4L_EMMC_CMD17,
		    "read timeout did not capture CMD17");
		check(host.fail_command == 0x113Au,
		    "timed-out CMD17 encoding mismatch");
		check(host.fail_step == GTA4L_EMMC_STEP_READY,
		    "CMD17 timeout step is not READY");
	} else if (scenario_is(scenario, "read-data-crc")) {
		sim.data_crc = 1;
		result = gta4l_emmc_read_block(&host, 0, block0);
		check(result == GTA4L_EMMC_ERR_DATA_CRC,
		    "data CRC not reported");
		check(host.fail_valid == 1,
		    "read data CRC failure was not captured");
		check(host.fail_index == GTA4L_EMMC_CMD17,
		    "read data CRC failure did not capture CMD17");
		check(host.fail_command == 0x113Au,
		    "CMD17 encoded command mismatch");
		check((host.fail_error &
		    GTA4L_EMMC_INT_ERR_DATA_CRC) != 0,
		    "captured error is not DATA_CRC");
		check(host.fail_step == GTA4L_EMMC_STEP_READY,
		    "CMD17 failure step is not READY");
	} else if (scenario_is(scenario, "read-late-fifo-error")) {
		/* The error becomes visible on the last FIFO word read, after
		 * the in-loop error sample. It must still be reported. */
		sim.data_late_error_words =
		    (int)(GTA4L_EMMC_BLOCK_BYTES / 4u);
		result = gta4l_emmc_read_block(&host, 0, block0);
		check(result == GTA4L_EMMC_ERR_DATA_CRC,
		    "late FIFO error not reported");
		check(host.fail_valid == 1,
		    "late FIFO error was not captured");
		check(host.fail_index == GTA4L_EMMC_CMD17,
		    "late FIFO error did not capture CMD17");
		check((host.fail_error &
		    GTA4L_EMMC_INT_ERR_DATA_CRC) != 0,
		    "captured late FIFO error is not DATA_CRC");
	} else if (scenario_is(scenario, "read-no-tc")) {
		sim.no_tc = 1;
		result = gta4l_emmc_read_block(&host, 0, block0);
		check(result == GTA4L_EMMC_ERR_DATA_TIMEOUT,
		    "missing TC not reported");
		check(host.fail_valid == 1, "missing TC was not captured");
		check(host.fail_index == GTA4L_EMMC_CMD17,
		    "missing TC did not capture CMD17");
		check(host.fail_command == 0x113Au,
		    "missing-TC CMD17 encoding mismatch");
		check(host.fail_step == GTA4L_EMMC_STEP_READY,
		    "CMD17 missing-TC step is not READY");
	} else if (scenario_is(scenario, "read-no-brr")) {
		sim.no_brr = 1;
		result = gta4l_emmc_read_block(&host, 0, block0);
		check(result == GTA4L_EMMC_ERR_DATA_TIMEOUT,
		    "missing BRR not reported");
		check(host.fail_valid == 1, "missing BRR was not captured");
		check(host.fail_index == GTA4L_EMMC_CMD17,
		    "missing BRR did not capture CMD17");
		check(host.fail_command == 0x113Au,
		    "missing-BRR CMD17 encoding mismatch");
		check(host.fail_step == GTA4L_EMMC_STEP_READY,
		    "CMD17 missing-BRR step is not READY");
	} else if (scenario_is(scenario, "read-repeat")) {
		result = gta4l_emmc_read_block(&host, 5, block5);
		check(result == GTA4L_EMMC_OK, "first read failed");
		if (result == GTA4L_EMMC_OK) {
			check_block(&sim, 5, block5, "block 5 first");
		}
		memset(block5, 0, sizeof(block5));
		result = gta4l_emmc_read_block(&host, 5, block5);
		check(result == GTA4L_EMMC_OK, "second read failed");
		if (result == GTA4L_EMMC_OK) {
			check_block(&sim, 5, block5, "block 5 second");
		}
	} else if (scenario_is(scenario, "multi-args")) {
		uint32_t before = sim.cmd_count;

		result = gta4l_emmc_read_multi(&host, 0x100000000ULL, 1u,
		    mb_blocks);
		check(result == GTA4L_EMMC_ERR_ARG,
		    "multi-block start overflow was accepted");
		result = gta4l_emmc_read_multi(&host, host.capacity_blocks, 1u,
		    mb_blocks);
		check(result == GTA4L_EMMC_ERR_ARG,
		    "multi-block start at capacity was accepted");
		result = gta4l_emmc_read_multi(&host,
		    (uint64_t)host.capacity_blocks - 1u, 2u, mb_blocks);
		check(result == GTA4L_EMMC_ERR_ARG,
		    "multi-block count past capacity was accepted");
		result = gta4l_emmc_read_multi(&host, 2u, 0u, mb_blocks);
		check(result == GTA4L_EMMC_ERR_ARG,
		    "zero-block multi-block read was accepted");
		result = gta4l_emmc_read_multi(&host, 2u, 0x10000u, mb_blocks);
		check(result == GTA4L_EMMC_ERR_ARG,
		    "oversized multi-block count was accepted");
		check(sim.cmd_count == before,
		    "a rejected multi-block argument issued a command");
	} else if (scenario_is(scenario, "multi-without-init")) {
		struct gta4l_emmc_host empty;

		memset(&empty, 0, sizeof(empty));
		empty.ops = &sim_ops;
		empty.context = &sim;
		result = gta4l_emmc_read_multi(&empty, 0, 1u, mb_blocks);
		check(result == GTA4L_EMMC_ERR_ARG,
		    "multi-block read without init was accepted");
	} else if (scenario_is(scenario, "multi-last-block")) {
		result = gta4l_emmc_read_multi(&host,
		    (uint64_t)host.capacity_blocks - 3u, 3u, mb_blocks);
		check(result == GTA4L_EMMC_OK, "multi-block tail read failed");
		if (result == GTA4L_EMMC_OK) {
			check_multi_block(mb_blocks,
			    host.capacity_blocks - 3u, 3u,
			    "multi-block tail");
			check(host.multi_block == 2u,
			    "tail multi-block ordinal mismatch");
			check(sim.mb_issued_block_count == 3u,
			    "tail block count register mismatch");
		}
	} else if (scenario_is(scenario, "multi-success") ||
	    scenario_is(scenario, "multi-repeat")) {
		result = gta4l_emmc_read_multi(&host, 2u,
		    SIM_MB_TEST_BLOCKS, mb_blocks);
		check(result == GTA4L_EMMC_OK, "multi-block read failed");
		if (result == GTA4L_EMMC_OK) {
			check_multi_block(mb_blocks, 2u, SIM_MB_TEST_BLOCKS,
			    "multi-block");
			check(host.multi_block == SIM_MB_TEST_BLOCKS - 1u,
			    "completed multi-block ordinal mismatch");
			check(host.multi_stop_result == GTA4L_EMMC_OK,
			    "clean CMD12 stop was not recorded as success");
			check(host.multi_stop_valid == 1u,
			    "CMD12 stop was not marked attempted");
			check(host.multi_stop_capture_valid == 1u,
			    "CMD12 terminal state was not captured");
			check(host.multi_stop_command == 0x0C1Bu,
			    "CMD12 encoding mismatch");
			check(host.multi_stop_argument == 0u,
			    "CMD12 argument is not zero");
			check((host.multi_stop_normal &
			    (GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_TC)) ==
			    (GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_TC),
			    "CMD12 terminal state lost CC or TC");
			check(sim.mb_stop_count == 1,
			    "the explicit CMD12 stop was not issued");
			check(sim.mb_issued_block_size == 512u,
			    "multi-block sector size register mismatch");
			check(sim.mb_issued_block_count ==
			    SIM_MB_TEST_BLOCKS,
			    "multi-block count register mismatch");
			check(sim.mb_argument == 2u,
			    "multi-block start LBA mismatch");
			check(sim.auto_cmd12_seen == 0,
			    "AUTO_CMD12 was enabled");
			check(sim.mb_dat_inhibit == 0,
			    "DAT inhibit survived the CMD12 stop");
		}
		if (scenario_is(scenario, "multi-repeat")) {
			uint32_t before = (uint32_t)sim.mb_stop_count;

			memset(mb_blocks, 0, sizeof(mb_blocks));
			result = gta4l_emmc_read_multi(&host, 5u, 4u,
			    mb_blocks);
			check(result == GTA4L_EMMC_OK,
			    "second multi-block read failed");
			if (result == GTA4L_EMMC_OK) {
				check_multi_block(mb_blocks, 5u, 4u,
				    "multi-block second");
				check(sim.mb_stop_count == (int)(before + 1u),
				    "second multi-block read did not stop");
				check(host.multi_block == 3u,
				    "second multi-block ordinal mismatch");
			}
		}
	} else if (scenario_is(scenario, "multi-data-timeout") ||
	    scenario_is(scenario, "multi-data-crc") ||
	    scenario_is(scenario, "multi-late-error")) {
		if (scenario_is(scenario, "multi-data-timeout")) {
			sim.data_timeout = 1;
		}
		if (scenario_is(scenario, "multi-data-crc")) {
			sim.data_crc = 1;
		}
		result = gta4l_emmc_read_multi(&host, 2u,
		    SIM_MB_TEST_BLOCKS, mb_blocks);
		if (scenario_is(scenario, "multi-data-timeout")) {
			check(result == GTA4L_EMMC_ERR_DATA_TIMEOUT,
			    "multi-block data timeout not reported");
		} else {
			check(result == GTA4L_EMMC_ERR_DATA_CRC,
			    "multi-block data CRC not reported");
		}
		check(host.fail_valid == 1,
		    "multi-block data failure was not captured");
		check(host.fail_index == GTA4L_EMMC_CMD18,
		    "multi-block data failure did not capture CMD18");
		check(host.fail_command == 0x123Au,
		    "CMD18 encoding mismatch");
		check(host.fail_step == GTA4L_EMMC_STEP_READY,
		    "multi-block data failure step is not READY");
		check(host.multi_block ==
		    (scenario_is(scenario, "multi-late-error") ? 4u : 0u),
		    "multi-block failing ordinal mismatch");
		check(host.multi_stop_result == GTA4L_EMMC_OK,
		    "CMD12 attempt result was not recorded after data error");
		check(sim.mb_stop_count == 1,
		    "no stop attempt after the multi-block data error");
	} else if (scenario_is(scenario, "multi-drain-tc") ||
	    scenario_is(scenario, "multi-drain-brr") ||
	    scenario_is(scenario, "multi-drain-error") ||
	    scenario_is(scenario, "multi-drain-stall")) {
		uint32_t expected_blocks = 0u;

		if (scenario_is(scenario, "multi-drain-brr")) {
			expected_blocks = 1u;
		}
		result = gta4l_emmc_read_multi(&host, 2u,
		    SIM_MB_TEST_BLOCKS, mb_blocks);
		if (scenario_is(scenario, "multi-drain-error")) {
			check(result == GTA4L_EMMC_ERR_DATA_CRC,
			    "drain-window data error was not reported");
			check(host.fail_valid == 1,
			    "drain-window data error was not captured");
			check(host.fail_index == GTA4L_EMMC_CMD18,
			    "drain-window data error did not capture CMD18");
			check((host.fail_error &
			    GTA4L_EMMC_INT_ERR_DATA_CRC) != 0,
			    "drain-window captured error is not DATA_CRC");
		} else if (scenario_is(scenario, "multi-drain-stall")) {
			check(result == GTA4L_EMMC_ERR_DATA_TIMEOUT,
			    "an exhausted drain window was not reported as a "
			    "data timeout");
			check(host.fail_valid == 1,
			    "an exhausted drain window was not captured");
			check(host.fail_index == GTA4L_EMMC_CMD18,
			    "drain exhaustion did not capture CMD18");
		} else {
			check(result == GTA4L_EMMC_OK,
			    "drained multi-block read failed");
			if (result == GTA4L_EMMC_OK) {
				check_multi_block(mb_blocks, 2u,
				    SIM_MB_TEST_BLOCKS,
				    "drained multi-block");
				check(host.multi_block ==
				    SIM_MB_TEST_BLOCKS - 1u,
				    "drained multi-block ordinal mismatch");
			}
		}
		check(host.multi_drain_used == 1u,
		    "post-bound drain window was not entered");
		check(host.multi_drain_resolved ==
		    ((scenario_is(scenario, "multi-drain-error") ||
		    scenario_is(scenario, "multi-drain-stall")) ? 0u : 1u),
		    "drain-resolved transfer did not record its resolution");
		check(host.multi_drain_blocks == expected_blocks,
		    "late-BRR drain did not count the drained block");
		if (scenario_is(scenario, "multi-drain-stall")) {
			check(host.multi_drain_iterations ==
			    GTA4L_EMMC_DATA_DRAIN_US /
			    GTA4L_EMMC_POLL_STEP_US,
			    "exhausted drain iteration count mismatch");
		} else {
			check(host.multi_drain_iterations > 0u &&
			    host.multi_drain_iterations <=
			    GTA4L_EMMC_DATA_DRAIN_US /
			    GTA4L_EMMC_POLL_STEP_US,
			    "drain iteration count is out of bounds");
		}
		check(host.multi_stop_result == GTA4L_EMMC_OK,
		    "CMD12 result was not recorded after the drain");
		check(sim.mb_stop_count == 1,
		    "the drain path did not issue exactly one stop");
		check(host.multi_stop_command == 0x0C1Bu,
		    "drain-path CMD12 encoding mismatch");
		check(sim.mb_dat_inhibit == 0,
		    "DAT inhibit survived the drain-path stop");
	} else if (scenario_is(scenario, "multi-card-status")) {
		result = gta4l_emmc_read_multi(&host, 2u,
		    SIM_MB_TEST_BLOCKS, mb_blocks);
		check(result == GTA4L_EMMC_ERR_CARD_STATUS,
		    "multi-block R1 card status was not reported");
		check(host.multi_block == SIM_MB_TEST_BLOCKS - 1u,
		    "card-status run did not complete the data phase");
		check(host.multi_stop_result == GTA4L_EMMC_OK,
		    "card-status run did not stop cleanly");
		check(sim.mb_stop_count == 1,
		    "card-status run did not issue the explicit stop");
		check(host.fail_valid == 0u,
		    "a card status error was recorded as a controller failure");
	} else if (scenario_is(scenario, "multi-stop-tc-only") ||
	    scenario_is(scenario, "multi-stop-late-cc") ||
	    scenario_is(scenario, "multi-stop-busy-timeout") ||
	    scenario_is(scenario, "multi-stop-crc") ||
	    scenario_is(scenario, "multi-stop-late-error")) {
		result = gta4l_emmc_read_multi(&host, 2u,
		    SIM_MB_TEST_BLOCKS, mb_blocks);
		if (scenario_is(scenario, "multi-stop-crc") ||
		    scenario_is(scenario, "multi-stop-late-error")) {
			check(result == GTA4L_EMMC_ERR_CMD_CRC,
			    "CMD12 CRC not reported");
			check((host.multi_stop_error &
			    GTA4L_EMMC_INT_ERR_CMD_CRC) != 0,
			    "CMD12 terminal capture lost the CRC error");
		} else {
			check(result == GTA4L_EMMC_ERR_CMD_TIMEOUT,
			    "CMD12 completion timeout not reported");
		}
		check(host.fail_valid == 1,
		    "CMD12 stop failure was not captured");
		check(host.fail_index == GTA4L_EMMC_CMD12,
		    "stop failure did not capture CMD12");
		check(host.fail_command == 0x0C1Bu,
		    "failed CMD12 encoding mismatch");
		check(host.multi_block == SIM_MB_TEST_BLOCKS - 1u,
		    "data completed but the ordinal was not the last block");
		check(host.multi_stop_valid == 1u,
		    "failed CMD12 was not marked attempted");
		check(host.multi_stop_capture_valid == 1u,
		    "failed CMD12 terminal state was not captured");
		check(sim.mb_stop_count == 1,
		    "failed stop was not attempted exactly once");
		if (scenario_is(scenario, "multi-stop-tc-only")) {
			check((host.multi_stop_normal &
			    GTA4L_EMMC_INT_CC) == 0,
			    "TC-only stop captured a command completion");
			check((host.multi_stop_normal &
			    GTA4L_EMMC_INT_TC) != 0,
			    "TC-only stop lost the transfer completion");
			check(host.multi_stop_result ==
			    GTA4L_EMMC_ERR_CMD_TIMEOUT,
			    "TC-only stop result mismatch");
		}
		if (scenario_is(scenario, "multi-stop-busy-timeout")) {
			check((host.multi_stop_normal &
			    GTA4L_EMMC_INT_CC) != 0,
			    "busy-timeout stop lost the command completion");
			check((host.multi_stop_normal &
			    GTA4L_EMMC_INT_TC) == 0,
			    "busy-timeout stop captured a transfer completion");
			check(host.multi_stop_result ==
			    GTA4L_EMMC_ERR_CMD_TIMEOUT,
			    "busy-timeout stop result mismatch");
		}
		if (scenario_is(scenario, "multi-stop-late-cc")) {
			check(host.multi_stop_result ==
			    GTA4L_EMMC_ERR_CMD_TIMEOUT,
			    "late-CC stop result mismatch");
		}
		if (scenario_is(scenario, "multi-stop-crc") ||
		    scenario_is(scenario, "multi-stop-late-error")) {
			check(host.multi_stop_result ==
			    GTA4L_EMMC_ERR_CMD_CRC,
			    "CRC stop result mismatch");
		}
	} else if (scenario_is(scenario, "read-ext-csd")) {
		result = gta4l_emmc_read_ext_csd(&host, ext);
		check(result == GTA4L_EMMC_OK, "EXT_CSD read failed");
		check(ext[GTA4L_EMMC_EXT_CSD_REV_OFF] == 8u,
		    "EXT_CSD revision mismatch");
	} else if (scenario_is(scenario, "cmd2-normalized") ||
	    scenario_is(scenario, "cmd3-identify") ||
	    scenario_is(scenario, "cmd3-stale-cc") ||
	    scenario_is(scenario, "cmd3-enable-readback")) {
		check(result == GTA4L_EMMC_OK, "initialization failed");
		if (result != GTA4L_EMMC_OK) {
			fprintf(stderr, "init result %d\n", result);
			return 1;
		}
		if (scenario_is(scenario, "cmd2-normalized")) {
			uint32_t start;

			check(host.cmd2_capture_valid == 1u,
			    "normalized-layout run did not capture CMD2");
			check(host.cmd2_raw_response[0] == 0x15010044u &&
			    host.cmd2_raw_response[1] == 0x5836384Du &&
			    host.cmd2_raw_response[2] == 0x42009005u &&
			    host.cmd2_raw_response[3] == 0xF4661978u,
			    "CMD2 raw capture lost the photographed layout");
			check(host.cid[0] == 0x66197842u &&
			    host.cid[1] == 0x00900558u &&
			    host.cid[2] == 0x36384D15u &&
			    host.cid[3] == 0x01004400u,
			    "decoded CID did not follow the standard SDHCI "
			    "normalization");
			check(host.cid[0] != host.cmd2_raw_response[0] ||
			    host.cid[1] != host.cmd2_raw_response[1] ||
			    host.cid[2] != host.cmd2_raw_response[2] ||
			    host.cid[3] != host.cmd2_raw_response[3],
			    "raw layout and decoded CID were not kept "
			    "distinct");
			start = response_window_find(&sim,
			    GTA4L_EMMC_CMD2, 0);
			check(start != 0xFFFFFFFFu,
			    "CMD2 window was not read as a group");
			if (start != 0xFFFFFFFFu) {
				check(sim.response_read_clear[start] ==
				    sim.cmd_issue_clear[GTA4L_EMMC_CMD2],
				    "CMD2 window was captured after the "
				    "post-command clear");
			}
		}
		if (scenario_is(scenario, "cmd3-identify")) {
			check(host.pre_cmd3_capture_valid == 1u,
			    "identify-status run did not capture pre-CMD3 "
			    "state");
			check((host.pre_cmd3_normal &
			    GTA4L_EMMC_INT_CC) == 0,
			    "pre-CMD3 status already held a completion");
			check(host.pre_cmd3_normal_enable ==
			    (GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_TC |
			    GTA4L_EMMC_INT_BRR),
			    "pre-CMD3 normal enables do not match the core");
			check(host.pre_cmd3_error_enable ==
			    GTA4L_EMMC_INT_ERR_MASK,
			    "pre-CMD3 error enables do not match the core");
			check(host.poll_capture_valid == 1u,
			    "identify-status run did not record the CMD3 "
			    "poll");
			check(host.poll_index == GTA4L_EMMC_CMD3,
			    "identify-status poll is not for CMD3");
			check(host.timeout_capture_valid == 0u,
			    "identify-status run recorded a timeout capture");
		}
		if (scenario_is(scenario, "cmd3-stale-cc")) {
			check(host.pre_cmd3_capture_valid == 1u,
			    "stale-CC run did not capture pre-CMD3 state");
			check((host.pre_cmd3_normal &
			    GTA4L_EMMC_INT_CC) != 0,
			    "stale completion did not survive the pre-CMD3 "
			    "clear");
			check(host.poll_capture_valid == 1u,
			    "stale-CC run did not record the CMD3 poll");
			check((host.poll_first_normal &
			    GTA4L_EMMC_INT_CC) != 0,
			    "stale-CC poll did not observe a completion");
		}
		if (scenario_is(scenario, "cmd3-enable-readback")) {
			check(host.pre_cmd3_capture_valid == 1u,
			    "enable-readback run did not capture pre-CMD3 "
			    "state");
			check(host.pre_cmd3_normal_enable == 0u,
			    "normal enable readback mismatch was not "
			    "captured");
			check(host.pre_cmd3_error_enable == 0u,
			    "error enable readback mismatch was not "
			    "captured");
			check(host.poll_capture_valid == 1u,
			    "enable-readback run did not record the CMD3 "
			    "poll");
		}
	} else if (scenario_is(scenario, "cmd7-cc-then-tc") ||
	    scenario_is(scenario, "cmd7-tc-then-cc") ||
	    scenario_is(scenario, "cmd7-simultaneous") ||
	    scenario_is(scenario, "cmd7-terminal-change") ||
	    scenario_is(scenario, "cmd7-last-poll") ||
	    scenario_is(scenario, "cmd7-delayed-release") ||
	    scenario_is(scenario, "cmd7-stale-status") ||
	    scenario_is(scenario, "cmd0-irrelevant-tc") ||
	    scenario_is(scenario, "cmd2-irrelevant-tc") ||
	    scenario_is(scenario, "cmd3-irrelevant-tc")) {
		check(host.cmd7_pre_capture_valid == 1u,
		    "CMD7 success did not capture the pre-command state");
		check(host.cmd7_poll_capture_valid == 1u,
		    "CMD7 success did not record the poll");
		check(host.cmd7_term_capture_valid == 1u,
		    "CMD7 success did not capture the terminal state");
		check(host.cmd7_cc_valid == 1u,
		    "CMD7 success did not observe CC");
		check(host.cmd7_tc_valid == 1u,
		    "CMD7 success did not observe TC");
		check((host.cmd7_poll_or_normal & GTA4L_EMMC_INT_CC) != 0,
		    "CMD7 poll mask lost the observed CC");
		check((host.cmd7_poll_or_normal & GTA4L_EMMC_INT_TC) != 0,
		    "CMD7 poll mask lost the observed TC");
		if (scenario_is(scenario, "cmd7-terminal-change")) {
			check(host.cmd7_poll_last_normal ==
			    (GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_TC),
			    "terminal-change poll last sample is not CC|TC");
			check(host.cmd7_term_normal ==
			    (GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_TC |
			    GTA4L_EMMC_INT_BRR),
			    "CMD7 terminal sample lost the post-poll status "
			    "change");
		} else {
			check(host.cmd7_term_normal ==
			    (GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_TC),
			    "CMD7 terminal sample does not hold both "
			    "completions");
		}
		check(host.last_step == GTA4L_EMMC_STEP_READY,
		    "CMD7 success did not reach READY");
		check(sim.cmd_count == 8u,
		    "CMD7 success did not complete identification");
		check(sim.cmd7_early_command == 0,
		    "a command was submitted before the busy release");
		check(sim.response_read_before_cc == 0,
		    "the response window was read before CC");
		if (scenario_is(scenario, "cmd7-cc-then-tc")) {
			check(host.cmd7_cc_iteration == 0u,
			    "CC-then-TC run did not observe CC first");
			check(host.cmd7_tc_iteration == 5u,
			    "CC-then-TC run did not observe TC after five poll "
			    "intervals");
			check(host.cmd7_poll_iterations == 6u,
			    "CC-then-TC run polled for the wrong number of "
			    "iterations");
			check(host.cmd7_poll_ticks == 50u,
			    "CC-then-TC poll ticks do not match the modeled "
			    "busy interval");
			check(host.cmd7_trace_count == 2u,
			    "CC-then-TC trace does not hold both samples");
			check(host.cmd7_trace[0].normal ==
			    GTA4L_EMMC_INT_CC &&
			    host.cmd7_trace[1].normal ==
			    (GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_TC),
			    "CC-then-TC trace samples changed");
		}
		if (scenario_is(scenario, "cmd7-tc-then-cc")) {
			check(host.cmd7_tc_valid == 1u &&
			    host.cmd7_tc_iteration == 0u,
			    "TC-then-CC run did not observe TC first");
			check(host.cmd7_cc_iteration == 1u,
			    "TC-then-CC run did not observe CC on the second "
			    "sample");
			check(host.cmd7_trace_count == 2u,
			    "TC-then-CC trace does not hold both samples");
			check(host.cmd7_trace[0].normal ==
			    GTA4L_EMMC_INT_TC,
			    "TC-then-CC trace first sample is not TC");
			check(host.cmd7_trace[1].normal ==
			    (GTA4L_EMMC_INT_TC | GTA4L_EMMC_INT_CC),
			    "TC-then-CC trace second sample is not TC|CC");
		}
		if (scenario_is(scenario, "cmd7-simultaneous")) {
			check(host.cmd7_cc_iteration == 0u &&
			    host.cmd7_tc_iteration == 0u,
			    "simultaneous completion was not seen on the first "
			    "sample");
			check(host.cmd7_poll_iterations == 1u,
			    "zero-length busy interval polled more than once");
			check(host.cmd7_trace_count == 1u,
			    "simultaneous completion produced a transition "
			    "trace");
		}
		if (scenario_is(scenario, "cmd7-last-poll")) {
			check(host.cmd7_cc_iteration ==
			    (GTA4L_EMMC_CMD_TIMEOUT_US /
			    GTA4L_EMMC_POLL_STEP_US) - 1u &&
			    host.cmd7_tc_iteration ==
			    (GTA4L_EMMC_CMD_TIMEOUT_US /
			    GTA4L_EMMC_POLL_STEP_US) - 1u,
			    "last-poll completion was not seen on the final "
			    "permitted sample");
			check(host.cmd7_poll_iterations ==
			    GTA4L_EMMC_CMD_TIMEOUT_US /
			    GTA4L_EMMC_POLL_STEP_US,
			    "last-poll run did not consume the full budget");
			check(host.cmd7_poll_ticks ==
			    (uint64_t)(host.cmd7_poll_iterations - 1u) *
			    GTA4L_EMMC_POLL_STEP_US,
			    "last-poll tick count does not match the modeled "
			    "delays");
		}
		if (scenario_is(scenario, "cmd7-delayed-release")) {
			check(host.cmd7_tc_iteration == 20u,
			    "delayed release was not observed at 200 us");
			check(host.cmd7_poll_ticks == 200u,
			    "delayed-release poll ticks are not 200 us");
			check(sim.cmd7_release_time_us ==
			    sim.cmd7_issue_time_us + 200u,
			    "busy release time is not issue + 200 us");
			check(sim.cmd7_next_issue_time_us >=
			    sim.cmd7_release_time_us,
			    "the next command was issued before the busy "
			    "release");
			check(sim.cmd7_next_index == GTA4L_EMMC_CMD13,
			    "the command after CMD7 is not CMD13");
		}
		if (scenario_is(scenario, "cmd7-stale-status")) {
			check(host.cmd7_pre_normal == 0u,
			    "stale completion survived the pre-CMD7 clear");
			check(host.cmd7_pre_error == 0u,
			    "stale error survived the pre-CMD7 clear");
		}
		if (scenario_is(scenario, "cmd3-irrelevant-tc")) {
			check(host.poll_capture_valid == 1u,
			    "irrelevant-TC CMD3 poll was not recorded");
			check(host.poll_iterations == 1u,
			    "irrelevant TC did not stop the CMD3 poll at CC");
			check((host.poll_first_normal &
			    GTA4L_EMMC_INT_CC) != 0 &&
			    (host.poll_first_normal &
			    GTA4L_EMMC_INT_TC) != 0,
			    "irrelevant-TC CMD3 poll did not observe CC|TC");
		}
		if (scenario_is(scenario, "cmd0-irrelevant-tc") ||
		    scenario_is(scenario, "cmd2-irrelevant-tc") ||
		    scenario_is(scenario, "cmd3-irrelevant-tc")) {
			check(host.timeout_capture_valid == 0u,
			    "an irrelevant TC caused a command timeout");
		}
	} else {
		switch (result) {
		case GTA4L_EMMC_ERR_VERSION:
			expected = scenario_is(scenario, "version");
			break;
		case GTA4L_EMMC_ERR_RESET:
			expected = scenario_is(scenario, "reset-timeout");
			break;
		case GTA4L_EMMC_ERR_CLOCK:
			expected = scenario_is(scenario, "clock-timeout") ||
			    scenario_is(scenario, "rcg-busy") ||
			    scenario_is(scenario, "clock-branch-apps-fail") ||
			    scenario_is(scenario, "clock-branch-ahb-fail");
			break;
		case GTA4L_EMMC_ERR_POWER:
			expected = scenario_is(scenario, "power-off") ||
			    scenario_is(scenario, "power-no-request") ||
			    scenario_is(scenario, "power-stuck") ||
			    scenario_is(scenario, "power-io-only") ||
			    scenario_is(scenario, "power-churn");
			break;
		case GTA4L_EMMC_ERR_CMD_TIMEOUT:
			expected = scenario_is(scenario, "cmd-timeout") ||
			    scenario_is(scenario, "cmd3-no-cc") ||
			    scenario_is(scenario, "cmd3-late-cc") ||
			    scenario_is(scenario, "cmd7-tc-only") ||
			    scenario_is(scenario, "cmd7-fresh-window") ||
			    scenario_is(scenario, "cmd7-busy-error") ||
			    scenario_is(scenario, "cmd7-late-error") ||
			    scenario_is(scenario, "cmd7-terminal-error") ||
			    scenario_is(scenario, "cmd7-cc-only") ||
			    scenario_is(scenario, "cmd7-neither") ||
			    scenario_is(scenario, "cmd7-late-tc") ||
			    scenario_is(scenario, "cmd7-trace-overflow") ||
			    scenario_is(scenario, "cmd7-trace-plateau") ||
			    scenario_is(scenario, "cmd7-trace-return");
			break;
		case GTA4L_EMMC_ERR_CMD_CRC:
			expected = scenario_is(scenario, "cmd-crc") ||
			    scenario_is(scenario, "cmd-crc-with-cc") ||
			    scenario_is(scenario, "cmd-crc-snapshot") ||
			    scenario_is(scenario, "cmd0-crc") ||
			    scenario_is(scenario, "cmd1-crc") ||
			    scenario_is(scenario, "cmd2-crc-distinct") ||
			    scenario_is(scenario, "cmd2-crc-unchanged") ||
			    scenario_is(scenario, "cmd2-crc-ocr-like") ||
			    scenario_is(scenario, "cmd1-not-ready-then-ready-crc") ||
			    scenario_is(scenario,
			    "cmd1-timeout-recovery-crc") ||
			    scenario_is(scenario, "cmd1-crc-retire-short") ||
			    scenario_is(scenario, "cmd9-late-error");
			break;
		case GTA4L_EMMC_ERR_CMD_INDEX:
			expected = scenario_is(scenario, "cmd-index");
			break;
		case GTA4L_EMMC_ERR_DATA_TIMEOUT:
			expected = scenario_is(scenario, "data-timeout-init") ||
			    scenario_is(scenario, "no-tc-init") ||
			    scenario_is(scenario, "no-brr-init") ||
			    scenario_is(scenario, "cmd7-data-timeout-after-cc");
			break;
		case GTA4L_EMMC_ERR_DATA_CRC:
			expected = scenario_is(scenario, "data-crc-init") ||
			    scenario_is(scenario, "cmd7-data-crc-after-cc");
			break;
		case GTA4L_EMMC_ERR_CARD_NOT_READY:
			expected = scenario_is(scenario, "card-not-ready") ||
			    scenario_is(scenario, "cmd1-timeout-exhausted");
			break;
		case GTA4L_EMMC_ERR_CARD_STATUS:
			expected = scenario_is(scenario, "cmd3-error");
			break;
		case GTA4L_EMMC_ERR_UNSUPPORTED:
			expected = scenario_is(scenario, "small-card") ||
			    scenario_is(scenario, "partition-access") ||
			    scenario_is(scenario, "data-sector-4k") ||
			    scenario_is(scenario, "data-sector-reserved");
			break;
		case GTA4L_EMMC_ERR_RESPONSE:
			expected = scenario_is(scenario, "bad-cid") ||
			    scenario_is(scenario, "bad-csd");
			break;
		default:
			expected = 0;
			break;
		}
		if (!expected) {
			fprintf(stderr, "FAIL: %s returned %d\n", scenario, result);
			failures++;
		}
		if (scenario_is(scenario, "cmd-crc") ||
		    scenario_is(scenario, "cmd-crc-with-cc") ||
		    scenario_is(scenario, "cmd-crc-snapshot") ||
		    scenario_is(scenario, "cmd0-crc") ||
		    scenario_is(scenario, "cmd1-crc") ||
		    scenario_is(scenario, "cmd2-crc-distinct") ||
		    scenario_is(scenario, "cmd2-crc-unchanged") ||
		    scenario_is(scenario, "cmd2-crc-ocr-like") ||
		    scenario_is(scenario, "cmd1-not-ready-then-ready-crc") ||
		    scenario_is(scenario, "cmd1-timeout-recovery-crc") ||
		    scenario_is(scenario, "cmd1-crc-retire-short") ||
		    scenario_is(scenario, "cmd9-late-error")) {
			check(host.fail_valid == 1,
			    "CRC failure was not captured");
			check((host.fail_error &
			    GTA4L_EMMC_INT_ERR_CMD_CRC) != 0,
			    "captured error is not CMD_CRC");
		}
		if (scenario_is(scenario, "cmd-crc") ||
		    scenario_is(scenario, "cmd-crc-with-cc") ||
		    scenario_is(scenario, "cmd-crc-snapshot") ||
		    scenario_is(scenario, "cmd2-crc-distinct") ||
		    scenario_is(scenario, "cmd2-crc-unchanged") ||
		    scenario_is(scenario, "cmd2-crc-ocr-like") ||
		    scenario_is(scenario, "cmd1-not-ready-then-ready-crc") ||
		    scenario_is(scenario, "cmd1-timeout-recovery-crc")) {
			check(sim.last_cmd == GTA4L_EMMC_CMD2,
			    "a later command was sent after the CMD2 CRC failure");
		}
		if (scenario_is(scenario, "cmd9-late-error")) {
			check(host.fail_index == GTA4L_EMMC_CMD9,
			    "late CMD9 error did not capture CMD9");
		}
		if (scenario_is(scenario, "cmd1-crc-retire-short")) {
			check(sim.cmd1_crc_retire_us ==
			    (int)(GTA4L_EMMC_CMD1_CRC_RETIRE_US + 1u),
			    "short CRC-retire model is not one microsecond "
			    "longer than the candidate");
			check(sim.cmd1_crc_retire_pending == 1,
			    "short CRC-retire model was not armed");
			check(host.fail_index == GTA4L_EMMC_CMD2,
			    "short CRC-retire failure did not capture CMD2");
			check(host.fail_step == GTA4L_EMMC_STEP_CMD1,
			    "short CRC-retire failure step is not CMD1");
			check(sim.last_cmd == GTA4L_EMMC_CMD2,
			    "a later command followed the short CRC-retire "
			    "failure");
			check(sim.cmd_count == 3u,
			    "short CRC-retire run issued extra commands");
			check(sim.cmd2_time_us - sim.cmd1_ready_time_us ==
			    GTA4L_EMMC_CMD1_CRC_RETIRE_US,
			    "short CRC-retire CMD2 delay is not the candidate "
			    "8 us");
			check(host.cid[0] == 0u && host.cid[1] == 0u &&
			    host.cid[2] == 0u && host.cid[3] == 0u,
			    "short CRC-retire failure promoted a CID");
		}
		if (scenario_is(scenario, "cmd-crc-with-cc")) {
			check(host.fail_index == GTA4L_EMMC_CMD2,
			    "CC+CRC failure did not capture CMD2");
			check(host.fail_command == 0x0209u,
			    "CC+CRC CMD2 encoding mismatch");
			check((host.fail_normal & GTA4L_EMMC_INT_CC) != 0,
			    "CC bit was not captured with the CRC error");
			check(host.cid[0] == 0u && host.cid[1] == 0u &&
			    host.cid[2] == 0u && host.cid[3] == 0u,
			    "CRC-failed CID was promoted to the host CID");
		}
		if (scenario_is(scenario, "cmd-crc-snapshot")) {
			uint32_t expected[4];

			/* Mirror sim_set_response(): raw RESPONSE0..3 carry
			 * the normalized CID shifted left by the stripped CRC
			 * byte, so compare the snapshot in that layout. */
			expected[3] = sim.cid[0] >> 8;
			expected[2] = (sim.cid[1] >> 8) |
			    ((sim.cid[0] & 0xFFu) << 24);
			expected[1] = (sim.cid[2] >> 8) |
			    ((sim.cid[1] & 0xFFu) << 24);
			expected[0] = (sim.cid[3] >> 8) |
			    ((sim.cid[2] & 0xFFu) << 24);
			check(host.fail_index == GTA4L_EMMC_CMD2,
			    "snapshot failure did not capture CMD2");
			check(host.fail_response[0] == expected[0] &&
			    host.fail_response[1] == expected[1] &&
			    host.fail_response[2] == expected[2] &&
			    host.fail_response[3] == expected[3],
			    "raw CMD2 response was not snapshotted before clear");
			check(sim_read32(&sim, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE) == 0u,
			    "simulator did not invalidate the response");
			check(host.cid[0] == 0u && host.cid[1] == 0u &&
			    host.cid[2] == 0u && host.cid[3] == 0u,
			    "snapshotted CRC-failed CID became the host CID");
		}
		if (scenario_is(scenario, "cmd1-timeout-recovery-crc")) {
			check(host.fail_index == GTA4L_EMMC_CMD2,
			    "recovered CMD1 timeout masked the CMD2 failure");
			check(host.fail_command == 0x0209u,
			    "CMD2 encoded command mismatch after CMD1 retry");
			check(host.fail_step == GTA4L_EMMC_STEP_CMD1,
			    "terminal failure step is not CMD1");
			check(host.cmd1_attempts == 2u,
			    "CMD2 failure run did not report two CMD1 attempts");
			check(host.cmd1_timeouts == 1u,
			    "CMD2 failure run did not report one CMD1 timeout");
			check(host.ocr ==
			    (GTA4L_EMMC_OCR_VALUE | 0x80000000u),
			    "CMD2 failure run did not cache the ready OCR");
			check(host.cmd1_response_valid == 1u,
			    "recovered CMD1 run did not mark its window valid");
			check(host.pre_cmd2_response_valid == 1u,
			    "recovered CMD1 run did not capture the pre-CMD2 "
			    "window");
			check(host.cmd1_response[0] ==
			    (GTA4L_EMMC_OCR_VALUE | 0x80000000u),
			    "recovered CMD1 run did not capture the ready OCR");
			check(host.fail_response[0] !=
			    host.pre_cmd2_response[0] ||
			    host.fail_response[1] !=
			    host.pre_cmd2_response[1] ||
			    host.fail_response[2] !=
			    host.pre_cmd2_response[2] ||
			    host.fail_response[3] !=
			    host.pre_cmd2_response[3],
			    "failed CMD2 window equals the pre-CMD2 window");
		}
		if (scenario_is(scenario, "cmd2-crc-distinct")) {
			uint32_t expected[4];
			uint32_t cmd1_start;
			uint32_t pre_start;
			uint32_t fail_start;

			expected[3] = sim.cid[0] >> 8;
			expected[2] = (sim.cid[1] >> 8) |
			    ((sim.cid[0] & 0xFFu) << 24);
			expected[1] = (sim.cid[2] >> 8) |
			    ((sim.cid[1] & 0xFFu) << 24);
			expected[0] = (sim.cid[3] >> 8) |
			    ((sim.cid[2] & 0xFFu) << 24);

			check(host.cmd1_response_valid == 1u,
			    "final CMD1 raw window was not marked valid");
			check(host.pre_cmd2_response_valid == 1u,
			    "pre-CMD2 raw window was not marked valid");
			check(host.cmd1_response[0] ==
			    (GTA4L_EMMC_OCR_VALUE | 0x80000000u),
			    "final CMD1 raw word 0 is not the ready OCR");
			check(host.cmd1_response[1] == 0x01020304u,
			    "final CMD1 raw word 1 was not captured");
			check(host.cmd1_response[2] == 0x05060708u,
			    "final CMD1 raw word 2 was not captured");
			check(host.cmd1_response[3] == 0x090A0B0Cu,
			    "final CMD1 raw word 3 was not captured");
			check(host.pre_cmd2_response[0] ==
			    host.cmd1_response[0] &&
			    host.pre_cmd2_response[1] ==
			    host.cmd1_response[1] &&
			    host.pre_cmd2_response[2] ==
			    host.cmd1_response[2] &&
			    host.pre_cmd2_response[3] ==
			    host.cmd1_response[3],
			    "pre-CMD2 window is not the post-CMD1 window");
			check(host.fail_response[0] == expected[0] &&
			    host.fail_response[1] == expected[1] &&
			    host.fail_response[2] == expected[2] &&
			    host.fail_response[3] == expected[3],
			    "failed-CMD2 window does not hold the raw CID "
			    "layout");
			check(host.fail_response[0] !=
			    host.pre_cmd2_response[0] &&
			    host.fail_response[1] !=
			    host.pre_cmd2_response[1] &&
			    host.fail_response[2] !=
			    host.pre_cmd2_response[2] &&
			    host.fail_response[3] !=
			    host.pre_cmd2_response[3],
			    "distinct CMD1/CMD2 windows share a word");

			cmd1_start = response_window_find(&sim,
			    GTA4L_EMMC_CMD1, 0);
			pre_start = response_window_find(&sim,
			    GTA4L_EMMC_CMD1, 1);
			fail_start = response_window_find(&sim,
			    GTA4L_EMMC_CMD2, 0);
			check(cmd1_start != 0xFFFFFFFFu,
			    "post-CMD1 window was not read as a group");
			check(pre_start != 0xFFFFFFFFu,
			    "pre-CMD2 window was not read as a group");
			check(fail_start != 0xFFFFFFFFu,
			    "failed-CMD2 window was not read as a group");
			if ((cmd1_start != 0xFFFFFFFFu) &&
			    (pre_start != 0xFFFFFFFFu) &&
			    (fail_start != 0xFFFFFFFFu)) {
				check(sim.response_read_clear[pre_start] >
				    sim.response_read_clear[cmd1_start],
				    "pre-CMD2 window was not captured after a "
				    "later status clear");
				check(sim.response_read_clear[pre_start] ==
				    sim.cmd_clear_count,
				    "pre-CMD2 window was not captured after "
				    "the last status clear");
				check(sim.response_read_clear[fail_start] ==
				    sim.cmd_clear_count,
				    "failed-CMD2 window was not captured "
				    "before the post-failure clear");
			}
		}
		if (scenario_is(scenario, "cmd2-crc-unchanged")) {
			check(host.cmd1_response_valid == 1u,
			    "unchanged-window run lost the final CMD1 window");
			check(host.pre_cmd2_response_valid == 1u,
			    "unchanged-window run did not capture pre-CMD2");
			check(host.cmd1_response[0] ==
			    (GTA4L_EMMC_OCR_VALUE | 0x80000000u),
			    "unchanged-window CMD1 word 0 is not the OCR");
			check(host.cmd1_response[1] == 0u &&
			    host.cmd1_response[2] == 0u &&
			    host.cmd1_response[3] == 0u,
			    "unchanged-window CMD1 words 1..3 are not zero");
			check(host.pre_cmd2_response[0] ==
			    host.cmd1_response[0] &&
			    host.pre_cmd2_response[1] ==
			    host.cmd1_response[1] &&
			    host.pre_cmd2_response[2] ==
			    host.cmd1_response[2] &&
			    host.pre_cmd2_response[3] ==
			    host.cmd1_response[3],
			    "pre-CMD2 window changed without a CMD2 update");
			check(host.fail_response[0] ==
			    host.pre_cmd2_response[0] &&
			    host.fail_response[1] ==
			    host.pre_cmd2_response[1] &&
			    host.fail_response[2] ==
			    host.pre_cmd2_response[2] &&
			    host.fail_response[3] ==
			    host.pre_cmd2_response[3],
			    "failed window changed though the controller kept "
			    "the response");
		}
		if (scenario_is(scenario, "cmd2-crc-ocr-like")) {
			check(host.cmd1_response_valid == 1u,
			    "OCR-like run lost the final CMD1 window");
			check(host.pre_cmd2_response_valid == 1u,
			    "OCR-like run did not capture pre-CMD2");
			check(host.pre_cmd2_response[0] ==
			    (GTA4L_EMMC_OCR_VALUE | 0x80000000u) &&
			    host.pre_cmd2_response[1] == 0u &&
			    host.pre_cmd2_response[2] == 0u &&
			    host.pre_cmd2_response[3] == 0u,
			    "OCR-like pre-CMD2 window is not the CMD1 OCR");
			check(host.fail_response[0] == 0u &&
			    host.fail_response[1] == 0u &&
			    host.fail_response[2] == 0x80000000u &&
			    host.fail_response[3] == 0x00C0FF80u,
			    "OCR-like failed-command window was not captured");
			check(host.fail_response[2] !=
			    host.pre_cmd2_response[2] ||
			    host.fail_response[3] !=
			    host.pre_cmd2_response[3],
			    "rearranged window was not distinguished");
		}
		if (scenario_is(scenario, "cmd1-timeout-exhausted")) {
			check(host.fail_valid == 1,
			    "the final CMD1 timeout was not recorded");
			check(host.fail_index == GTA4L_EMMC_CMD1,
			    "exhausted CMD1 run did not capture CMD1");
			check(host.fail_command == 0x0102u,
			    "CMD1 encoded command mismatch");
			check(host.fail_error == 0u,
			    "timed-out CMD1 has a spurious error status");
			check(host.fail_step == GTA4L_EMMC_STEP_CMD0,
			    "CMD1 timeout step is not CMD0");
			check(host.cmd1_attempts ==
			    GTA4L_EMMC_CMD1_TIMEOUT_US /
			    GTA4L_EMMC_CMD1_ATTEMPT_US,
			    "exhausted CMD1 run did not use every attempt");
			check(host.cmd1_timeouts == host.cmd1_attempts,
			    "exhausted CMD1 run did not record every timeout");
			check(host.ocr == 0u,
			    "an unready card cached an OCR value");
			check(host.cmd1_response_valid == 0u,
			    "unreached CMD1 baseline was marked valid");
			check(host.pre_cmd2_response_valid == 0u,
			    "unreached CMD2 window was marked valid");
		}
		if (scenario_is(scenario, "clock-branch-apps-fail") ||
		    scenario_is(scenario, "clock-branch-ahb-fail")) {
			check(host.clocks_enabled == 0,
			    "a failed branch enable was reported as enabled");
			check(host.fail_valid == 0,
			    "a clock failure recorded a command failure");
			check(sim.card_clock_enabled == 0,
			    "card clock enabled after a failed branch enable");
			check(sim.sdhci_before_clocks == 0,
			    "SDHCI was accessed after a failed branch enable");
			if (scenario_is(scenario, "clock-branch-apps-fail")) {
				check(sim.apps_branch_enabled == 0,
				    "failed APPS branch was marked running");
			} else {
				check(sim.ahb_branch_enabled == 0,
				    "failed AHB branch was marked running");
			}
		}
		if (scenario_is(scenario, "cmd-crc")) {
			check(host.fail_index == GTA4L_EMMC_CMD2,
			    "CRC failure did not capture CMD2");
			check(host.fail_command == 0x0209u,
			    "CMD2 encoded command mismatch");
			check(host.fail_step == GTA4L_EMMC_STEP_CMD1,
			    "CMD2 failure step is not CMD1");
		}
		if (scenario_is(scenario, "cmd0-crc")) {
			check(host.fail_index == GTA4L_EMMC_CMD0,
			    "CRC failure did not capture CMD0");
			check(host.fail_command == 0x0000u,
			    "CMD0 encoded command mismatch");
			check(host.fail_step == GTA4L_EMMC_STEP_CARD_CLOCK,
			    "CMD0 failure step is not CARD_CLOCK");
		}
		if (scenario_is(scenario, "cmd1-crc")) {
			check(host.fail_index == GTA4L_EMMC_CMD1,
			    "CRC failure did not capture CMD1");
			check(host.fail_command == 0x0102u,
			    "CMD1 encoded command mismatch");
			check(host.fail_step == GTA4L_EMMC_STEP_CMD0,
			    "CMD1 failure step is not CMD0");
		}
		if (scenario_is(scenario, "cmd-index")) {
			check(host.fail_valid == 1,
			    "command-index failure was not captured");
			check(host.fail_index == GTA4L_EMMC_CMD3,
			    "command-index failure did not capture CMD3");
			check(host.fail_command == 0x031Au,
			    "CMD3 encoded command mismatch");
			check((host.fail_error &
			    GTA4L_EMMC_INT_ERR_CMD_INDEX) != 0,
			    "captured error is not CMD_INDEX");
			check(host.fail_step == GTA4L_EMMC_STEP_CMD2,
			    "CMD3 failure step is not CMD2");
		}
		if (scenario_is(scenario, "cmd-timeout")) {
			check(host.fail_valid == 1,
			    "command timeout was not captured");
			check(host.fail_index == GTA4L_EMMC_CMD0,
			    "timeout failure did not capture CMD0");
			check(host.fail_command == 0x0000u,
			    "timed-out CMD0 encoding mismatch");
			check(host.fail_error == 0u,
			    "timed-out CMD0 has a spurious error status");
			check(host.fail_step == GTA4L_EMMC_STEP_CARD_CLOCK,
			    "CMD0 timeout step is not CARD_CLOCK");
		}
		if (scenario_is(scenario, "data-crc-init")) {
			check(host.fail_valid == 1,
			    "data CRC failure was not captured");
			check(host.fail_index == GTA4L_EMMC_CMD8,
			    "data CRC failure did not capture CMD8");
			check(host.fail_command == 0x083Au,
			    "CMD8 encoded command mismatch");
			check((host.fail_error &
			    GTA4L_EMMC_INT_ERR_DATA_CRC) != 0,
			    "captured error is not DATA_CRC");
			check(host.fail_step == GTA4L_EMMC_STEP_TRAN,
			    "CMD8 failure step is not TRAN");
		}
		if (scenario_is(scenario, "data-timeout-init") ||
		    scenario_is(scenario, "no-tc-init") ||
		    scenario_is(scenario, "no-brr-init")) {
			check(host.fail_valid == 1,
			    "data timeout was not captured");
			check(host.fail_index == GTA4L_EMMC_CMD8,
			    "data timeout did not capture CMD8");
			check(host.fail_command == 0x083Au,
			    "timed-out CMD8 encoding mismatch");
			check(host.fail_error == 0u,
			    "timed-out CMD8 has a spurious error status");
			check(host.fail_step == GTA4L_EMMC_STEP_TRAN,
			    "CMD8 timeout step is not TRAN");
		}
		if (scenario_is(scenario, "card-not-ready") ||
		    scenario_is(scenario, "cmd3-error")) {
			check(host.fail_valid == 0,
			    "non-command failure recorded a command failure");
		}
		if (scenario_is(scenario, "card-not-ready")) {
			check(host.cmd1_response_valid == 0u,
			    "never-ready CMD1 baseline was marked valid");
			check(host.pre_cmd2_response_valid == 0u,
			    "never-ready run captured a pre-CMD2 window");
		}
		if (scenario_is(scenario, "cmd3-no-cc") ||
		    scenario_is(scenario, "cmd3-late-cc")) {
			check(host.fail_valid == 1,
			    "CMD3 timeout was not captured");
			check(host.fail_index == GTA4L_EMMC_CMD3,
			    "CMD3 timeout did not capture CMD3");
			check(host.fail_command == 0x031Au,
			    "timed-out CMD3 encoding mismatch");
			check(host.fail_error == 0u,
			    "timed-out CMD3 has a spurious error status");
			check(host.fail_step == GTA4L_EMMC_STEP_CMD2,
			    "CMD3 timeout step is not CMD2");
			check(host.cmd2_capture_valid == 1u,
			    "CMD3 timeout lost the CMD2 capture");
			check(host.pre_cmd3_capture_valid == 1u,
			    "CMD3 timeout did not capture pre-CMD3 state");
			check(host.poll_capture_valid == 1u,
			    "CMD3 timeout did not record the poll");
			check(host.poll_index == GTA4L_EMMC_CMD3,
			    "recorded poll is not for CMD3");
			check(host.poll_iterations ==
			    GTA4L_EMMC_CMD_TIMEOUT_US /
			    GTA4L_EMMC_POLL_STEP_US,
			    "CMD3 timeout poll did not run to exhaustion");
			check(host.poll_ticks ==
			    (uint64_t)host.poll_iterations *
			    GTA4L_EMMC_POLL_STEP_US,
			    "CMD3 poll tick count does not match the "
			    "simulated delays");
			check(host.timeout_capture_valid == 1u,
			    "CMD3 timeout did not capture fresh state");
			check(host.timeout_command == 0x031Au,
			    "timed-out CMD3 readback mismatch");
			check(host.timeout_argument ==
			    (GTA4L_EMMC_DEFAULT_RCA << 16),
			    "timed-out CMD3 argument readback mismatch");
			check(host.timeout_response_stable == 1u,
			    "post-timeout response window changed before the "
			    "clear");
		}
		if (scenario_is(scenario, "cmd3-no-cc")) {
			check((host.poll_first_normal &
			    GTA4L_EMMC_INT_CC) == 0,
			    "no-CC run saw a completion in the first sample");
			check((host.poll_last_normal &
			    GTA4L_EMMC_INT_CC) == 0,
			    "no-CC run saw a completion in the last sample");
			check((host.timeout_normal &
			    GTA4L_EMMC_INT_CC) == 0,
			    "no-CC run saw a completion after the timeout");
		}
		if (scenario_is(scenario, "cmd3-late-cc")) {
			check((host.poll_first_normal &
			    GTA4L_EMMC_INT_CC) == 0,
			    "late-CC run saw a completion in the first "
			    "sample");
			check((host.poll_last_normal &
			    GTA4L_EMMC_INT_CC) == 0,
			    "late-CC run saw a completion in the last "
			    "sample");
			check((host.timeout_normal &
			    GTA4L_EMMC_INT_CC) != 0,
			    "late-CC run did not see the post-timeout "
			    "completion");
		}
		if (scenario_is(scenario, "cmd7-tc-only") ||
		    scenario_is(scenario, "cmd7-fresh-window") ||
		    scenario_is(scenario, "cmd7-busy-error") ||
		    scenario_is(scenario, "cmd7-late-error") ||
		    scenario_is(scenario, "cmd7-terminal-error") ||
		    scenario_is(scenario, "cmd7-cc-only") ||
		    scenario_is(scenario, "cmd7-neither") ||
		    scenario_is(scenario, "cmd7-late-tc") ||
		    scenario_is(scenario, "cmd7-data-timeout-after-cc") ||
		    scenario_is(scenario, "cmd7-data-crc-after-cc") ||
		    scenario_is(scenario, "cmd7-trace-overflow") ||
		    scenario_is(scenario, "cmd7-trace-plateau") ||
		    scenario_is(scenario, "cmd7-trace-return")) {
			check(host.fail_valid == 1,
			    "CMD7 timeout was not captured");
			check(host.fail_index == GTA4L_EMMC_CMD7,
			    "CMD7 timeout did not capture CMD7");
			check(host.fail_command == 0x071Bu,
			    "timed-out CMD7 encoding mismatch");
			check(host.fail_argument ==
			    (GTA4L_EMMC_DEFAULT_RCA << 16),
			    "timed-out CMD7 argument mismatch");
			check(host.fail_step == GTA4L_EMMC_STEP_CMD9,
			    "CMD7 timeout step is not CMD9");
			check(host.cmd7_pre_capture_valid == 1u,
			    "CMD7 timeout did not capture pre-command state");
			check(host.cmd7_poll_capture_valid == 1u,
			    "CMD7 timeout did not record the poll");
			check(host.cmd7_term_capture_valid == 1u,
			    "CMD7 timeout did not capture terminal state");
			check(host.cmd7_term_command == 0x071Bu,
			    "terminal CMD7 readback mismatch");
			check(host.cmd7_term_argument ==
			    (GTA4L_EMMC_DEFAULT_RCA << 16),
			    "terminal CMD7 argument readback mismatch");
			check(sim.last_cmd == GTA4L_EMMC_CMD7,
			    "a later command followed the CMD7 failure");
			check(sim.cmd_count == 6u,
			    "CMD7 failure did not stop identification");
		}
		if (scenario_is(scenario, "cmd7-tc-only") ||
		    scenario_is(scenario, "cmd7-fresh-window")) {
			check(host.timeout_capture_valid == 1u,
			    "CMD7 timeout did not capture fresh state");
			check(host.timeout_index == GTA4L_EMMC_CMD7,
			    "CMD7 timeout capture names another command");
			check(host.cmd7_cc_valid == 0u,
			    "TC-only timeout observed a command completion");
			check(host.cmd7_tc_valid == 1u &&
			    host.cmd7_tc_iteration == 0u,
			    "TC-only timeout did not observe TC first");
			check((host.cmd7_poll_or_normal &
			    GTA4L_EMMC_INT_TC) != 0,
			    "TC-only poll mask lost the observed TC");
			check((host.cmd7_poll_or_normal &
			    GTA4L_EMMC_INT_CC) == 0,
			    "TC-only poll mask claims an observed CC");
			check(host.cmd7_poll_iterations ==
			    GTA4L_EMMC_CMD_TIMEOUT_US /
			    GTA4L_EMMC_POLL_STEP_US,
			    "TC-only poll did not run to exhaustion");
			check(host.cmd7_poll_ticks ==
			    (uint64_t)host.cmd7_poll_iterations *
			    GTA4L_EMMC_POLL_STEP_US,
			    "CMD7 poll tick count does not match the "
			    "simulated delays");
			check(host.cmd7_trace_count == 1u,
			    "TC-only run recorded a transition");
			check(host.cmd7_trace_overflow == 0u,
			    "TC-only trace overflowed");
			check(host.fail_error == 0u,
			    "TC-only timeout has a spurious error status");
			check(host.fail_normal == GTA4L_EMMC_INT_TC,
			    "TC-only failure status is not TC");
			check(sim.response_read_before_cc == 0,
			    "TC-only run read the response before CC");
		}
		if (scenario_is(scenario, "cmd7-tc-only")) {
			/* The retained window is equal before and after
			 * CMD7. That is an observation, not proof that the
			 * controller failed to update it. */
			check(host.cmd7_pre_response[0] ==
			    host.cmd7_term_response[0] &&
			    host.cmd7_pre_response[1] ==
			    host.cmd7_term_response[1] &&
			    host.cmd7_pre_response[2] ==
			    host.cmd7_term_response[2] &&
			    host.cmd7_pre_response[3] ==
			    host.cmd7_term_response[3],
			    "retained CMD7 window changed");
			check(host.timeout_response_stable == 1u,
			    "two post-timeout samples disagreed");
		}
		if (scenario_is(scenario, "cmd7-fresh-window")) {
			check(host.cmd7_term_response[0] == 0x40400CF6u &&
			    host.cmd7_term_response[1] == 0xDBFFEF0Fu &&
			    host.cmd7_term_response[2] == 0x5903FF00u &&
			    host.cmd7_term_response[3] == 0x00070000u,
			    "fresh CMD7 window was not captured");
			check(host.cmd7_pre_response[0] !=
			    host.cmd7_term_response[0] ||
			    host.cmd7_pre_response[1] !=
			    host.cmd7_term_response[1] ||
			    host.cmd7_pre_response[2] !=
			    host.cmd7_term_response[2] ||
			    host.cmd7_pre_response[3] !=
			    host.cmd7_term_response[3],
			    "fresh CMD7 window equals the retained window");
			/* Two agreeing post-timeout samples do not make the
			 * window stale; it differs from the pre-CMD7 window. */
			check(host.timeout_response_stable == 1u,
			    "two post-timeout samples disagreed");
		}
		if (scenario_is(scenario, "cmd7-busy-error")) {
			check((host.fail_error &
			    GTA4L_EMMC_INT_ERR_CMD_TIMEOUT) != 0,
			    "busy-phase error was not captured");
			check(host.cmd7_poll_iterations == 1u,
			    "busy-phase error was not seen on the first poll");
			check(host.cmd7_poll_ticks == 0u,
			    "busy-phase error advanced the poll clock");
			check(host.cmd7_cc_valid == 1u,
			    "busy-phase error run did not observe CC");
			check(host.cmd7_term_error ==
			    GTA4L_EMMC_INT_ERR_CMD_TIMEOUT,
			    "terminal sample lost the busy-phase error");
		}
		if (scenario_is(scenario, "cmd7-cc-only")) {
			check(host.cmd7_cc_valid == 1u,
			    "CC-only run did not observe CC");
			check(host.cmd7_tc_valid == 0u,
			    "CC-only run observed a busy completion");
			check(host.cmd7_poll_iterations ==
			    GTA4L_EMMC_CMD_TIMEOUT_US /
			    GTA4L_EMMC_POLL_STEP_US,
			    "CC-only run did not exhaust the budget");
			check(host.cmd7_poll_ticks ==
			    (uint64_t)host.cmd7_poll_iterations *
			    GTA4L_EMMC_POLL_STEP_US,
			    "CC-only tick count does not match the modeled "
			    "busy interval");
			check(host.fail_normal == GTA4L_EMMC_INT_CC,
			    "CC-only failure status is not CC");
			check(host.fail_error == 0u,
			    "CC-only timeout has a spurious error status");
			check(sim.cmd7_early_command == 0,
			    "CC-only run submitted a command before release");
		}
		if (scenario_is(scenario, "cmd7-neither")) {
			check(host.cmd7_cc_valid == 0u &&
			    host.cmd7_tc_valid == 0u,
			    "no-completion run observed an event");
			check(host.cmd7_poll_iterations ==
			    GTA4L_EMMC_CMD_TIMEOUT_US /
			    GTA4L_EMMC_POLL_STEP_US,
			    "no-completion run did not exhaust the budget");
			check(host.fail_normal == 0u,
			    "no-completion failure status is not clear");
		}
		if (scenario_is(scenario, "cmd7-late-tc")) {
			check(host.cmd7_cc_valid == 1u,
			    "late-TC run did not observe CC");
			check(host.cmd7_tc_valid == 0u,
			    "late-TC run observed TC in-budget");
			check((host.timeout_normal &
			    GTA4L_EMMC_INT_TC) != 0,
			    "late-TC run did not expose TC after the budget");
			check(host.cmd7_poll_iterations ==
			    GTA4L_EMMC_CMD_TIMEOUT_US /
			    GTA4L_EMMC_POLL_STEP_US,
			    "late-TC run did not exhaust the budget");
		}
		if (scenario_is(scenario, "cmd7-late-error")) {
			check((host.fail_error &
			    GTA4L_EMMC_INT_ERR_CMD_TIMEOUT) != 0,
			    "final-sample error was not mapped");
			check(host.cmd7_cc_valid == 1u &&
			    host.cmd7_tc_valid == 1u,
			    "late-error run lost an in-budget completion");
			check(host.cmd7_poll_iterations == 1u,
			    "late-error run polled past the first sample");
			check(host.cmd7_poll_ticks == 1u,
			    "late-error run did not advance the explicit "
			    "response-read latency");
			check((host.cmd7_term_error &
			    GTA4L_EMMC_INT_ERR_CMD_TIMEOUT) != 0,
			    "terminal sample lost the final error");
			check(sim.response_read_before_cc == 0,
			    "late-error run read the response before CC");
		}
		if (scenario_is(scenario, "cmd7-terminal-error")) {
			check((host.fail_error &
			    GTA4L_EMMC_INT_ERR_CMD_TIMEOUT) != 0,
			    "terminal-read error was not mapped");
			check(host.cmd7_cc_valid == 1u &&
			    host.cmd7_tc_valid == 1u,
			    "terminal-error run lost an in-budget completion");
			check(host.cmd7_poll_iterations == 1u,
			    "terminal-error run polled past the first sample");
			check(host.cmd7_poll_ticks == 1u,
			    "terminal-error run did not advance the explicit "
			    "response-read latency");
			check(host.cmd7_term_error == 0u,
			    "terminal capture saw an error before the "
			    "terminal reads");
			check(sim.cmd7_error_due == 1,
			    "terminal-read error never became due");
			check(sim.response_read_before_cc == 0,
			    "terminal-error run read the response before CC");
		}
		if (scenario_is(scenario, "cmd7-data-timeout-after-cc") ||
		    scenario_is(scenario, "cmd7-data-crc-after-cc")) {
			uint16_t expected_error =
			    scenario_is(scenario,
			    "cmd7-data-timeout-after-cc") ?
			    GTA4L_EMMC_INT_ERR_DATA_TIMEOUT :
			    GTA4L_EMMC_INT_ERR_DATA_CRC;

			check((host.fail_error & expected_error) != 0,
			    "post-CC data error was not preserved");
			check(host.cmd7_cc_valid == 1u,
			    "post-CC data error run lost CC");
			check(host.cmd7_tc_valid == 0u,
			    "post-CC data error run observed TC first");
			check(host.cmd7_poll_iterations == 6u,
			    "post-CC data error was not seen at 50 us");
			check(host.cmd7_poll_ticks == 50u,
			    "post-CC data error poll ticks are not 50 us");
			check((host.cmd7_term_error & expected_error) != 0,
			    "terminal sample lost the post-CC data error");
			check(sim.response_read_before_cc == 0,
			    "response read before CC in a data-error run");
			check(sim.cmd7_early_command == 0,
			    "a command was submitted after the data error");
		}
		if (scenario_is(scenario, "cmd7-trace-overflow")) {
			check(host.cmd7_trace_count ==
			    GTA4L_EMMC_CMD7_TRACE_ENTRIES,
			    "bounded trace did not fill");
			check(host.cmd7_trace_overflow ==
			    (GTA4L_EMMC_CMD_TIMEOUT_US /
			    GTA4L_EMMC_POLL_STEP_US) - 4u,
			    "overflow count is not the number of observed "
			    "changes after the first four entries");
			check(host.cmd7_trace[0].normal ==
			    GTA4L_EMMC_INT_BRR &&
			    host.cmd7_trace[1].normal == 0u &&
			    host.cmd7_trace[2].normal ==
			    GTA4L_EMMC_INT_BRR &&
			    host.cmd7_trace[3].normal == 0u,
			    "alternating trace first four entries changed");
			check(host.cmd7_cc_valid == 0u,
			    "overflow run observed a command completion");
		}
		if (scenario_is(scenario, "cmd7-trace-plateau")) {
			check(host.cmd7_trace_count ==
			    GTA4L_EMMC_CMD7_TRACE_ENTRIES,
			    "plateau trace did not fill");
			check(host.cmd7_trace_overflow == 4u,
			    "plateau trace did not count exactly the four "
			    "observed changes after the store filled");
			check(host.cmd7_trace[0].normal == 0u &&
			    host.cmd7_trace[1].normal ==
			    GTA4L_EMMC_INT_BRR &&
			    host.cmd7_trace[2].normal == 0u &&
			    host.cmd7_trace[3].normal ==
			    GTA4L_EMMC_INT_BRR,
			    "plateau trace first four entries changed");
			check(host.cmd7_trace[0].iteration == 0u &&
			    host.cmd7_trace[1].iteration == 1u &&
			    host.cmd7_trace[2].iteration == 2u &&
			    host.cmd7_trace[3].iteration == 3u,
			    "plateau trace iterations changed");
			check(host.fail_normal == GTA4L_EMMC_INT_BRR,
			    "plateau timeout did not end at the scripted "
			    "plateau");
		}
		if (scenario_is(scenario, "cmd7-trace-return")) {
			check(host.cmd7_trace_count ==
			    GTA4L_EMMC_CMD7_TRACE_ENTRIES,
			    "return trace did not fill");
			check(host.cmd7_trace_overflow == 4u,
			    "return trace did not count the transition back "
			    "to the last stored state");
			check(host.cmd7_trace[0].normal ==
			    GTA4L_EMMC_INT_BRR &&
			    host.cmd7_trace[1].normal == 0u &&
			    host.cmd7_trace[2].normal ==
			    GTA4L_EMMC_INT_BRR &&
			    host.cmd7_trace[3].normal == 0u,
			    "return trace first four entries changed");
			check(host.cmd7_trace[0].iteration == 0u &&
			    host.cmd7_trace[1].iteration == 1u &&
			    host.cmd7_trace[2].iteration == 2u &&
			    host.cmd7_trace[3].iteration == 3u,
			    "return trace iterations changed");
			check(host.fail_normal == 0u,
			    "return timeout did not end at the scripted "
			    "plateau");
		}
	}

	if (failures != 0) {
		return 1;
	}
	printf("PASS %s\n", scenario);
	return 0;
}
