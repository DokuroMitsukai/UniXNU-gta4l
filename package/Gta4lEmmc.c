// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Portable gta4l SDHCI v5 / eMMC core implementation.
//
// This file deliberately uses only the callbacks in struct gta4l_emmc_ops. The
// XNU probe supplies mapped MMIO, a 19.2 MHz generic-counter delay and logging;
// the host simulator supplies a fake controller and card. The operational
// sequence below is therefore the code that runs on both.

#include <stdint.h>

#include "Gta4lEmmc.h"

static uint8_t
gta4l_emmc_read8(struct gta4l_emmc_host *host, uint32_t space, uint32_t offset)
{
	return host->ops->read8(host->context, space, offset);
}

static uint16_t
gta4l_emmc_read16(struct gta4l_emmc_host *host, uint32_t space, uint32_t offset)
{
	return host->ops->read16(host->context, space, offset);
}

static uint32_t
gta4l_emmc_read32(struct gta4l_emmc_host *host, uint32_t space, uint32_t offset)
{
	return host->ops->read32(host->context, space, offset);
}

static void
gta4l_emmc_write8(struct gta4l_emmc_host *host, uint32_t space,
    uint32_t offset, uint8_t value)
{
	host->ops->write8(host->context, space, offset, value);
}

static void
gta4l_emmc_write16(struct gta4l_emmc_host *host, uint32_t space,
    uint32_t offset, uint16_t value)
{
	host->ops->write16(host->context, space, offset, value);
}

static void
gta4l_emmc_write32(struct gta4l_emmc_host *host, uint32_t space,
    uint32_t offset, uint32_t value)
{
	host->ops->write32(host->context, space, offset, value);
}

static void
gta4l_emmc_barrier(struct gta4l_emmc_host *host)
{
	if (host->ops->barrier != 0) {
		host->ops->barrier(host->context);
	}
}

static void
gta4l_emmc_delay(struct gta4l_emmc_host *host, uint32_t microseconds)
{
	if (microseconds != 0) {
		host->ops->delay_us(host->context, microseconds);
	}
}

static void
gta4l_emmc_log(struct gta4l_emmc_host *host, uint32_t event, uint32_t a,
    uint32_t b)
{
	if (host->ops->log != 0) {
		host->ops->log(host->context, event, a, b);
	}
}

static uint64_t
gta4l_emmc_counter(struct gta4l_emmc_host *host)
{
	if (host->ops->counter != 0) {
		return host->ops->counter(host->context);
	}
	return 0;
}

/* Keep the terminal command failure intact before the status latches are
 * cleared. The probe reports these fields, so a hardware panic identifies the
 * command instead of only the final reset log. Recovered CMD1 busy timeouts are
 * retired explicitly so they cannot mask the command that ends initialization.
 * The raw RESPONSE0..3 words are captured as diagnostics only; a CRC-failed R2
 * response is never promoted to a valid CID. */
static void
gta4l_emmc_read_raw_response(struct gta4l_emmc_host *host,
    uint32_t response[4])
{
	uint32_t index;

	for (index = 0; index < 4; index++) {
		response[index] = gta4l_emmc_read32(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_RESPONSE + index * 4u);
	}
}

static void
gta4l_emmc_record_failure(struct gta4l_emmc_host *host, uint32_t index,
    uint32_t argument, uint16_t command, uint16_t error, uint16_t normal,
    uint32_t present)
{
	if (host->fail_valid != 0) {
		return;
	}
	host->fail_index = index;
	host->fail_argument = argument;
	host->fail_command = command;
	host->fail_error = error;
	host->fail_normal = normal;
	host->fail_present = present;
	gta4l_emmc_read_raw_response(host, host->fail_response);
	host->fail_step = host->last_step;
	host->fail_valid = 1;
	gta4l_emmc_log(host, GTA4L_EMMC_LOG_ERROR, index,
	    ((uint32_t)error << 16) | normal);
}

/* Capture the terminal CMD7 state before the status clear. The raw response
 * window is diagnostics only; equality with an earlier window does not prove
 * that the value was retained. */
static void
gta4l_emmc_capture_cmd7_terminal(struct gta4l_emmc_host *host, uint16_t normal,
    uint16_t error, uint32_t present)
{
	host->cmd7_term_normal = normal;
	host->cmd7_term_error = error;
	host->cmd7_term_present = present;
	host->cmd7_term_command = gta4l_emmc_read16(host,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_COMMAND);
	host->cmd7_term_argument = gta4l_emmc_read32(host,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_ARGUMENT);
	gta4l_emmc_read_raw_response(host, host->cmd7_term_response);
	host->cmd7_term_capture_valid = 1;
}

/* Record the first CMD7 poll sample and each observed status change. The
 * previous-observed pair is updated even after the bounded store fills, so the
 * overflow counter counts omitted observed changes rather than comparisons
 * against the last stored entry. A repeated sample is not a transition. When
 * the store is full only the overflow counter advances; the stored entries keep
 * their original order. */
static void
gta4l_emmc_record_cmd7_trace(struct gta4l_emmc_host *host, uint32_t iteration,
    uint16_t normal, uint16_t error)
{
	uint32_t count;

	if ((host->cmd7_trace_prev_valid != 0u) &&
	    (host->cmd7_trace_prev_normal == normal) &&
	    (host->cmd7_trace_prev_error == error)) {
		return;
	}
	host->cmd7_trace_prev_normal = normal;
	host->cmd7_trace_prev_error = error;
	host->cmd7_trace_prev_valid = 1;
	count = host->cmd7_trace_count;
	if (count < GTA4L_EMMC_CMD7_TRACE_ENTRIES) {
		host->cmd7_trace[count].iteration = iteration;
		host->cmd7_trace[count].normal = normal;
		host->cmd7_trace[count].error = error;
		host->cmd7_trace_count = count + 1u;
	} else {
		host->cmd7_trace_overflow++;
	}
}

/* Fold one CMD7 poll sample into the observed-status summary. The samples are
 * sequential reads, so an OR mask proves only which bits were observed, not
 * that every hardware transition was seen. Diagnostics only. */
static void
gta4l_emmc_note_cmd7_poll(struct gta4l_emmc_host *host, uint32_t iteration,
    uint16_t normal, uint16_t error, uint64_t poll_start)
{
	host->cmd7_poll_or_normal |= normal;
	host->cmd7_poll_or_error |= error;
	if (iteration == 0u) {
		host->cmd7_poll_first_normal = normal;
	}
	host->cmd7_poll_last_normal = normal;
	if ((host->cmd7_cc_valid == 0u) &&
	    ((normal & GTA4L_EMMC_INT_CC) != 0)) {
		host->cmd7_cc_valid = 1;
		host->cmd7_cc_iteration = iteration;
		host->cmd7_cc_ticks = gta4l_emmc_counter(host) - poll_start;
	}
	if ((host->cmd7_tc_valid == 0u) &&
	    ((normal & GTA4L_EMMC_INT_TC) != 0)) {
		host->cmd7_tc_valid = 1;
		host->cmd7_tc_iteration = iteration;
		host->cmd7_tc_ticks = gta4l_emmc_counter(host) - poll_start;
	}
	gta4l_emmc_record_cmd7_trace(host, iteration, normal, error);
}

static void
gta4l_emmc_clear_failure(struct gta4l_emmc_host *host)
{
	uint32_t index;

	host->fail_index = 0;
	host->fail_argument = 0;
	host->fail_command = 0;
	host->fail_error = 0;
	host->fail_normal = 0;
	host->fail_present = 0;
	host->fail_response[0] = 0;
	host->fail_response[1] = 0;
	host->fail_response[2] = 0;
	host->fail_response[3] = 0;
	host->fail_step = 0;
	host->cmd1_response[0] = 0;
	host->cmd1_response[1] = 0;
	host->cmd1_response[2] = 0;
	host->cmd1_response[3] = 0;
	host->pre_cmd2_response[0] = 0;
	host->pre_cmd2_response[1] = 0;
	host->pre_cmd2_response[2] = 0;
	host->pre_cmd2_response[3] = 0;
	host->cmd1_response_valid = 0;
	host->pre_cmd2_response_valid = 0;
	host->fail_valid = 0;
	host->cmd2_normal = 0;
	host->cmd2_error = 0;
	host->cmd2_raw_response[0] = 0;
	host->cmd2_raw_response[1] = 0;
	host->cmd2_raw_response[2] = 0;
	host->cmd2_raw_response[3] = 0;
	host->cmd2_capture_valid = 0;
	host->pre_cmd3_normal = 0;
	host->pre_cmd3_error = 0;
	host->pre_cmd3_present = 0;
	host->pre_cmd3_normal_enable = 0;
	host->pre_cmd3_error_enable = 0;
	host->pre_cmd3_capture_valid = 0;
	host->poll_first_normal = 0;
	host->poll_last_normal = 0;
	host->poll_iterations = 0;
	host->poll_ticks = 0;
	host->poll_index = 0;
	host->poll_capture_valid = 0;
	host->timeout_normal = 0;
	host->timeout_error = 0;
	host->timeout_present = 0;
	host->timeout_command = 0;
	host->timeout_argument = 0;
	host->timeout_index = 0;
	host->timeout_response[0] = 0;
	host->timeout_response[1] = 0;
	host->timeout_response[2] = 0;
	host->timeout_response[3] = 0;
	host->timeout_response_stable = 0;
	host->timeout_capture_valid = 0;
	host->cmd7_pre_normal = 0;
	host->cmd7_pre_error = 0;
	host->cmd7_pre_present = 0;
	host->cmd7_pre_normal_enable = 0;
	host->cmd7_pre_error_enable = 0;
	for (index = 0; index < 4u; index++) {
		host->cmd7_pre_response[index] = 0;
		host->cmd7_term_response[index] = 0;
	}
	host->cmd7_pre_capture_valid = 0;
	host->cmd7_poll_or_normal = 0;
	host->cmd7_poll_or_error = 0;
	host->cmd7_poll_first_normal = 0;
	host->cmd7_poll_last_normal = 0;
	host->cmd7_poll_iterations = 0;
	host->cmd7_poll_ticks = 0;
	host->cmd7_cc_iteration = 0;
	host->cmd7_cc_ticks = 0;
	host->cmd7_cc_valid = 0;
	host->cmd7_tc_iteration = 0;
	host->cmd7_tc_ticks = 0;
	host->cmd7_tc_valid = 0;
	host->cmd7_poll_capture_valid = 0;
	host->cmd7_trace_count = 0;
	host->cmd7_trace_overflow = 0;
	host->cmd7_trace_prev_normal = 0;
	host->cmd7_trace_prev_error = 0;
	host->cmd7_trace_prev_valid = 0;
	for (index = 0; index < GTA4L_EMMC_CMD7_TRACE_ENTRIES; index++) {
		host->cmd7_trace[index].iteration = 0;
		host->cmd7_trace[index].normal = 0;
		host->cmd7_trace[index].error = 0;
	}
	host->cmd7_term_normal = 0;
	host->cmd7_term_error = 0;
	host->cmd7_term_present = 0;
	host->cmd7_term_command = 0;
	host->cmd7_term_argument = 0;
	host->cmd7_term_capture_valid = 0;
	host->multi_block = 0;
	host->multi_stop_result = 0;
	host->multi_stop_normal = 0;
	host->multi_stop_error = 0;
	host->multi_stop_present = 0;
	host->multi_stop_command = 0;
	host->multi_stop_argument = 0;
	host->multi_stop_response[0] = 0;
	host->multi_stop_response[1] = 0;
	host->multi_stop_response[2] = 0;
	host->multi_stop_response[3] = 0;
	host->multi_stop_valid = 0;
	host->multi_stop_capture_valid = 0;
}

/* Fold one CMD3 attempt into its H9 capture slot. The fail, pre-command,
 * timeout and poll fields are sequential register samples; the capture is a
 * diagnostic record, not an atomic snapshot. */
static void
gta4l_emmc_cmd3_snapshot(struct gta4l_emmc_host *host,
    struct gta4l_emmc_cmd3_capture *capture, uint32_t number, int result,
    const uint32_t response[4])
{
	uint32_t index;

	capture->number = number;
	capture->result = result;
	capture->r1_error = (result == GTA4L_EMMC_ERR_CARD_STATUS) ? 1u : 0u;
	capture->fail_valid = host->fail_valid;
	capture->fail_index = host->fail_index;
	capture->fail_argument = host->fail_argument;
	capture->fail_command = host->fail_command;
	capture->fail_error = host->fail_error;
	capture->fail_normal = host->fail_normal;
	capture->fail_present = host->fail_present;
	capture->fail_step = host->fail_step;
	capture->pre_valid = host->pre_cmd3_capture_valid;
	capture->pre_normal = host->pre_cmd3_normal;
	capture->pre_error = host->pre_cmd3_error;
	capture->pre_present = host->pre_cmd3_present;
	capture->pre_normal_enable = host->pre_cmd3_normal_enable;
	capture->pre_error_enable = host->pre_cmd3_error_enable;
	capture->timeout_valid = host->timeout_capture_valid;
	capture->timeout_index = host->timeout_index;
	capture->timeout_command = host->timeout_command;
	capture->timeout_argument = host->timeout_argument;
	capture->timeout_response_stable = host->timeout_response_stable;
	capture->poll_valid = host->poll_capture_valid;
	capture->poll_iterations = host->poll_iterations;
	capture->poll_index = host->poll_index;
	capture->poll_first_normal = host->poll_first_normal;
	capture->poll_last_normal = host->poll_last_normal;
	capture->poll_ticks = host->poll_ticks;
	for (index = 0; index < 4u; index++) {
		capture->fail_response[index] = host->fail_response[index];
		capture->timeout_response[index] =
		    host->timeout_response[index];
		if ((result == GTA4L_EMMC_OK) ||
		    (result == GTA4L_EMMC_ERR_CARD_STATUS)) {
			capture->response[index] = response[index];
		} else {
			capture->response[index] =
			    host->fail_response[index];
		}
	}
}

/* Map a completed CMD3 R1 status to the core's status error. */
static int
gta4l_emmc_cmd3_result(int result, const uint32_t response[4])
{
	if ((result == GTA4L_EMMC_OK) &&
	    ((response[0] & GTA4L_EMMC_R1_ERROR_MASK) != 0)) {
		return GTA4L_EMMC_ERR_CARD_STATUS;
	}
	return result;
}

static uint32_t
gta4l_emmc_poll_iterations(uint32_t timeout_us)
{
	uint32_t iterations = timeout_us / GTA4L_EMMC_POLL_STEP_US;

	if (iterations == 0) {
		iterations = 1;
	}
	if (iterations > GTA4L_EMMC_MAX_POLL_ITERATIONS) {
		iterations = GTA4L_EMMC_MAX_POLL_ITERATIONS;
	}
	return iterations;
}

static int
gta4l_emmc_wait8(struct gta4l_emmc_host *host, uint32_t space,
    uint32_t offset, uint8_t mask, uint8_t value, uint32_t timeout_us)
{
	uint32_t iterations = gta4l_emmc_poll_iterations(timeout_us);
	uint32_t index;

	for (index = 0; index < iterations; index++) {
		if ((gta4l_emmc_read8(host, space, offset) & mask) == value) {
			return GTA4L_EMMC_OK;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}
	return GTA4L_EMMC_ERR_IO;
}

static int
gta4l_emmc_wait16(struct gta4l_emmc_host *host, uint32_t space,
    uint32_t offset, uint16_t mask, uint16_t value, uint32_t timeout_us)
{
	uint32_t iterations = gta4l_emmc_poll_iterations(timeout_us);
	uint32_t index;

	for (index = 0; index < iterations; index++) {
		if ((gta4l_emmc_read16(host, space, offset) & mask) == value) {
			return GTA4L_EMMC_OK;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}
	return GTA4L_EMMC_ERR_IO;
}

static int
gta4l_emmc_wait32(struct gta4l_emmc_host *host, uint32_t space,
    uint32_t offset, uint32_t mask, uint32_t value, uint32_t timeout_us)
{
	uint32_t iterations = gta4l_emmc_poll_iterations(timeout_us);
	uint32_t index;

	for (index = 0; index < iterations; index++) {
		if ((gta4l_emmc_read32(host, space, offset) & mask) == value) {
			return GTA4L_EMMC_OK;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}
	return GTA4L_EMMC_ERR_IO;
}

/* SDHCI stores the 120 R2 response bits in RESPONSE3..0, least significant
 * word first; the controller strips CRC7, so RESPONSE0's low byte is not
 * the CRC byte. Normalize to MMC big-endian word order: response[0] is bits
 * 127:96, matching Linux's sdhci_read_rsp_136() shift. */
static void
gta4l_emmc_read_long_response(struct gta4l_emmc_host *host,
    uint32_t response[4])
{
	uint32_t index;
	uint32_t offset;

	for (index = 0; index < 4; index++) {
		offset = GTA4L_EMMC_REG_RESPONSE + (3u - index) * 4u;
		response[index] = gta4l_emmc_read32(host,
		    GTA4L_EMMC_SPACE_SDHCI, offset) << 8;
		if (index != 3u) {
			response[index] |= gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI, offset - 4u) >> 24;
		}
	}
}

static void
gta4l_emmc_clear_interrupts(struct gta4l_emmc_host *host)
{
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_NORMAL_INT_STATUS, 0xFFFFu);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ERROR_INT_STATUS, 0xFFFFu);
	gta4l_emmc_barrier(host);
}

/* Polling needs the status bits enabled; interrupt signalling stays off. */
static void
gta4l_emmc_enable_status(struct gta4l_emmc_host *host)
{
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_NORMAL_INT_ENABLE,
	    GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_TC | GTA4L_EMMC_INT_BWR |
	    GTA4L_EMMC_INT_BRR);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ERROR_INT_ENABLE, GTA4L_EMMC_INT_ERR_MASK);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_NORMAL_INT_SIGNAL, 0);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ERROR_INT_SIGNAL, 0);
	gta4l_emmc_barrier(host);
}

/* Wait for the controller to accept the next command. DAT_INHIBIT is
 * checked as well: after an R1b busy command it is the controller's only
 * observable guard that the card is still driving DAT0. A stale inhibit left
 * by an aborted data transfer is cleared by the recovery path's bounded data
 * reset before it issues CMD6. */
static int
gta4l_emmc_wait_inhibit(struct gta4l_emmc_host *host)
{
	uint32_t state;
	uint32_t iterations = gta4l_emmc_poll_iterations(
	    GTA4L_EMMC_CMD_TIMEOUT_US);
	uint32_t index;

	for (index = 0; index < iterations; index++) {
		state = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		if ((state & (GTA4L_EMMC_PRESENT_CMD_INHIBIT |
		    GTA4L_EMMC_PRESENT_DAT_INHIBIT)) == 0) {
			return GTA4L_EMMC_OK;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}
	return GTA4L_EMMC_ERR_INHIBIT;
}

static int
gta4l_emmc_map_error(uint16_t error, int data)
{
	if ((error & GTA4L_EMMC_INT_ERR_CMD_TIMEOUT) != 0) {
		return GTA4L_EMMC_ERR_CMD_TIMEOUT;
	}
	if ((error & GTA4L_EMMC_INT_ERR_CMD_CRC) != 0) {
		return GTA4L_EMMC_ERR_CMD_CRC;
	}
	if ((error & GTA4L_EMMC_INT_ERR_CMD_INDEX) != 0) {
		return GTA4L_EMMC_ERR_CMD_INDEX;
	}
	if ((error & GTA4L_EMMC_INT_ERR_DATA_TIMEOUT) != 0) {
		return GTA4L_EMMC_ERR_DATA_TIMEOUT;
	}
	if ((error & GTA4L_EMMC_INT_ERR_DATA_CRC) != 0) {
		return GTA4L_EMMC_ERR_DATA_CRC;
	}
	return data ? GTA4L_EMMC_ERR_DATA_OTHER : GTA4L_EMMC_ERR_CMD_OTHER;
}

static int
gta4l_emmc_send_command(struct gta4l_emmc_host *host, uint32_t index,
    uint32_t argument, uint16_t flags, uint32_t timeout_us,
    uint32_t response[4])
{
	uint16_t command;
	uint16_t normal = 0;
	uint16_t error = 0;
	uint32_t present;
	uint32_t iterations;
	uint32_t step;
	uint64_t poll_start;
	uint32_t busy_response;
	int record_poll;
	int record_cmd7;
	int busy;
	int cc_seen;
	int tc_seen;
	int response_snapshot;
	int timed_out;
	int result;

	command = (uint16_t)((index << 8) | flags);
	result = gta4l_emmc_wait_inhibit(host);
	if (result != GTA4L_EMMC_OK) {
		present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		gta4l_emmc_record_failure(host, index, argument, command, 0, 0,
		    present);
		return result;
	}
	gta4l_emmc_clear_interrupts(host);
	if (index == GTA4L_EMMC_CMD2) {
		/* Capture the post-clear window immediately before CMD2 is
		 * submitted, so a W1C side effect is visible separately from
		 * the failed-command snapshot. Diagnostics only. */
		gta4l_emmc_read_raw_response(host, host->pre_cmd2_response);
		host->pre_cmd2_response_valid = 1;
	}
	if (index == GTA4L_EMMC_CMD3) {
		/* Capture the state after the pre-command clear and before
		 * CMD3 is submitted. A stale completion or a lost status
		 * enable is visible here. Diagnostics only. */
		host->pre_cmd3_normal = gta4l_emmc_read16(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		host->pre_cmd3_error = gta4l_emmc_read16(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		host->pre_cmd3_present = gta4l_emmc_read32(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		host->pre_cmd3_normal_enable = gta4l_emmc_read16(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_ENABLE);
		host->pre_cmd3_error_enable = gta4l_emmc_read16(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_ENABLE);
		host->pre_cmd3_capture_valid = 1;
	}
	if (index == GTA4L_EMMC_CMD7) {
		/* Capture the state after the pre-command clear and before
		 * CMD7 is submitted. A stale completion, a lost status
		 * enable or a retained response window is visible here.
		 * These are sequential reads, not an atomic snapshot.
		 * Diagnostics only. */
		host->cmd7_pre_normal = gta4l_emmc_read16(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		host->cmd7_pre_error = gta4l_emmc_read16(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		host->cmd7_pre_present = gta4l_emmc_read32(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		host->cmd7_pre_normal_enable = gta4l_emmc_read16(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_ENABLE);
		host->cmd7_pre_error_enable = gta4l_emmc_read16(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_ENABLE);
		gta4l_emmc_read_raw_response(host, host->cmd7_pre_response);
		host->cmd7_pre_capture_valid = 1;
	}
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ARGUMENT, argument);
	gta4l_emmc_barrier(host);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_COMMAND, command);
	gta4l_emmc_barrier(host);

	iterations = gta4l_emmc_poll_iterations(timeout_us);
	record_poll = (index == GTA4L_EMMC_CMD3);
	record_cmd7 = (index == GTA4L_EMMC_CMD7);
	busy = ((flags & GTA4L_EMMC_CMD_RESP_TYPE_MASK) ==
	    GTA4L_EMMC_CMD_RESP_SHORT_BUSY);
	cc_seen = 0;
	tc_seen = 0;
	response_snapshot = 0;
	busy_response = 0;
	if (record_poll != 0) {
		host->poll_first_normal = 0;
		host->poll_last_normal = 0;
		host->poll_iterations = 0;
		host->poll_ticks = 0;
		host->poll_index = index;
		host->poll_capture_valid = 0;
	}
	if (record_cmd7 != 0) {
		host->cmd7_poll_or_normal = 0;
		host->cmd7_poll_or_error = 0;
		host->cmd7_poll_first_normal = 0;
		host->cmd7_poll_last_normal = 0;
		host->cmd7_poll_iterations = 0;
		host->cmd7_poll_ticks = 0;
		host->cmd7_cc_iteration = 0;
		host->cmd7_cc_ticks = 0;
		host->cmd7_cc_valid = 0;
		host->cmd7_tc_iteration = 0;
		host->cmd7_tc_ticks = 0;
		host->cmd7_tc_valid = 0;
		host->cmd7_poll_capture_valid = 0;
		host->cmd7_trace_count = 0;
		host->cmd7_trace_overflow = 0;
		host->cmd7_trace_prev_normal = 0;
		host->cmd7_trace_prev_error = 0;
		host->cmd7_trace_prev_valid = 0;
	}
	if ((record_poll != 0) || (record_cmd7 != 0)) {
		poll_start = gta4l_emmc_counter(host);
	} else {
		poll_start = 0;
	}
	for (step = 0; step < iterations; step++) {
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);

		if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
			normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
			present = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_PRESENT_STATE);
			if (record_cmd7 != 0) {
				gta4l_emmc_note_cmd7_poll(host, step, normal,
				    error, poll_start);
				host->cmd7_poll_iterations = step + 1u;
				host->cmd7_poll_ticks =
				    gta4l_emmc_counter(host) - poll_start;
				host->cmd7_poll_capture_valid = 1;
				gta4l_emmc_capture_cmd7_terminal(host, normal,
				    error, present);
			}
			gta4l_emmc_record_failure(host, index, argument,
			    command, error, normal, present);
			gta4l_emmc_clear_interrupts(host);
			return gta4l_emmc_map_error(error, 0);
		}
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		if (record_poll != 0) {
			if (step == 0) {
				host->poll_first_normal = normal;
			}
			host->poll_last_normal = normal;
		}
		if (record_cmd7 != 0) {
			gta4l_emmc_note_cmd7_poll(host, step, normal, error,
			    poll_start);
		}
		if (busy != 0) {
			if ((normal & GTA4L_EMMC_INT_CC) != 0) {
				cc_seen = 1;
			}
			if ((normal & GTA4L_EMMC_INT_TC) != 0) {
				tc_seen = 1;
			}
			if ((cc_seen != 0) && (response_snapshot == 0) &&
			    (response != 0)) {
				busy_response = gta4l_emmc_read32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_RESPONSE);
				response_snapshot = 1;
			}
			if ((cc_seen != 0) && (tc_seen != 0)) {
				break;
			}
		} else if ((normal & GTA4L_EMMC_INT_CC) != 0) {
			break;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}
	timed_out = (busy != 0) ?
	    ((cc_seen == 0) || (tc_seen == 0)) :
	    ((normal & GTA4L_EMMC_INT_CC) == 0);
	if (record_poll != 0) {
		host->poll_iterations = timed_out ? step : step + 1u;
		host->poll_ticks = gta4l_emmc_counter(host) - poll_start;
		host->poll_capture_valid = 1;
	}
	if (record_cmd7 != 0) {
		host->cmd7_poll_iterations = timed_out ? step : step + 1u;
		host->cmd7_poll_ticks = gta4l_emmc_counter(host) - poll_start;
		host->cmd7_poll_capture_valid = 1;
	}
	if (timed_out != 0) {
		/* Fresh post-timeout samples. fail_normal is this fresh read,
		 * not the last poll sample; poll_last_normal keeps the loop
		 * value. Diagnostics only. */
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		host->timeout_normal = normal;
		host->timeout_error = error;
		host->timeout_present = present;
		host->timeout_command = gta4l_emmc_read16(host,
		    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_COMMAND);
		host->timeout_argument = gta4l_emmc_read32(host,
		    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_ARGUMENT);
		host->timeout_index = index;
		gta4l_emmc_read_raw_response(host, host->timeout_response);
		host->timeout_capture_valid = 1;
		if (record_cmd7 != 0) {
			gta4l_emmc_capture_cmd7_terminal(host, normal, error,
			    present);
		}
		gta4l_emmc_record_failure(host, index, argument, command,
		    error, normal, present);
		host->timeout_response_stable =
		    (host->timeout_response[0] == host->fail_response[0]) &&
		    (host->timeout_response[1] == host->fail_response[1]) &&
		    (host->timeout_response[2] == host->fail_response[2]) &&
		    (host->timeout_response[3] == host->fail_response[3]);
		gta4l_emmc_clear_interrupts(host);
		return GTA4L_EMMC_ERR_CMD_TIMEOUT;
	}

	if (index == GTA4L_EMMC_CMD2) {
		/* Capture the intact CMD2 window and its status before the
		 * status clear can invalidate either. The normalized CID is
		 * decoded below by the existing read_long_response path.
		 * Diagnostics only. */
		host->cmd2_normal = gta4l_emmc_read16(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		host->cmd2_error = gta4l_emmc_read16(host,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		gta4l_emmc_read_raw_response(host, host->cmd2_raw_response);
		host->cmd2_capture_valid = 1;
	}
	if (busy == 0) {
		if (response != 0) {
			if ((flags & GTA4L_EMMC_CMD_RESP_TYPE_MASK) ==
			    GTA4L_EMMC_CMD_RESP_LONG) {
				gta4l_emmc_read_long_response(host, response);
			} else {
				response[0] = gta4l_emmc_read32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_RESPONSE);
			}
		}
		if (index == GTA4L_EMMC_CMD1) {
			/* Keep the raw window from this CMD1 before the status
			 * clear can invalidate it. card_ready marks it valid only
			 * for the final accepted ready response. Diagnostics
			 * only. */
			gta4l_emmc_read_raw_response(host, host->cmd1_response);
		}
		/* The loop only observed the error latch before the response
		 * reads. Evaluate it once more so an error asserted during
		 * those reads is still recorded before the status clear. An
		 * error after this sample remains unobserved. */
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
			present = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_PRESENT_STATE);
			gta4l_emmc_record_failure(host, index, argument,
			    command, error, normal, present);
			gta4l_emmc_clear_interrupts(host);
			return gta4l_emmc_map_error(error, 0);
		}
	} else {
		if ((response != 0) && (response_snapshot != 0)) {
			response[0] = busy_response;
		}
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		if (record_cmd7 != 0) {
			gta4l_emmc_capture_cmd7_terminal(host, normal, error,
			    present);
		}
		/* The terminal capture reads COMMAND, ARGUMENT and the raw
		 * response window after the first error sample. Re-read the
		 * error status after those reads so an error that becomes
		 * visible during them is still evaluated before the status
		 * clear. This final sample is the observation boundary; an
		 * error asserted after it remains unobserved. */
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
			gta4l_emmc_record_failure(host, index, argument,
			    command, error, normal, present);
			gta4l_emmc_clear_interrupts(host);
			return gta4l_emmc_map_error(error, 0);
		}
	}

	gta4l_emmc_clear_interrupts(host);
	return GTA4L_EMMC_OK;
}

/* JESD84-B51 6.6.4 permits an optional CRC16 in the CMD14 pattern that the
 * host ignores, and Linux sdhci_data_irq() excludes MMC_BUS_TEST_R from its
 * data-CRC failure path. Tolerate exactly that one indication and only while
 * a verified bus-test read is active; every other error bit stays fatal. */
static int
gta4l_emmc_tolerated_bus_test_crc(const struct gta4l_emmc_host *host,
    uint16_t error)
{
	if ((host->bus_test_data_crc_ok == 0) ||
	    ((error & GTA4L_EMMC_INT_ERR_DATA_CRC) == 0)) {
		return 0;
	}
	return (error & (uint16_t)(GTA4L_EMMC_INT_ERR_MASK &
	    ~GTA4L_EMMC_INT_ERR_DATA_CRC)) == 0;
}

static int
gta4l_emmc_read_data(struct gta4l_emmc_host *host, uint32_t index,
    uint32_t argument, uint16_t flags, void *buffer, uint32_t length,
    uint32_t *response)
{
	uint16_t command;
	uint16_t normal = 0;
	uint16_t error = 0;
	uint32_t present;
	uint32_t iterations;
	uint32_t step;
	uint32_t byte;
	uint8_t *bytes = (uint8_t *)buffer;
	int command_done = 0;
	int data_done = 0;

	if ((buffer == 0) || ((length % 4) != 0) ||
	    (length == 0) || (length > 0xFFFFu)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	command = (uint16_t)((index << 8) | flags | GTA4L_EMMC_CMD_DATA);
	if (gta4l_emmc_wait_inhibit(host) != GTA4L_EMMC_OK) {
		present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		gta4l_emmc_record_failure(host, index, argument, command, 0, 0,
		    present);
		return GTA4L_EMMC_ERR_INHIBIT;
	}
	gta4l_emmc_clear_interrupts(host);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_BLOCK_SIZE, (uint16_t)length);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_BLOCK_COUNT, 1);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ARGUMENT, argument);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_TRANSFER_MODE,
	    GTA4L_EMMC_TM_READ | GTA4L_EMMC_TM_BLOCK_COUNT_EN);
	gta4l_emmc_barrier(host);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_COMMAND, command);
	gta4l_emmc_barrier(host);

	iterations = gta4l_emmc_poll_iterations(GTA4L_EMMC_DATA_TIMEOUT_US);
	for (step = 0; step < iterations; step++) {
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);

		if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
			if (gta4l_emmc_tolerated_bus_test_crc(host,
			    error) != 0) {
				host->bus_test_ignored_crc = 1;
				/* Clear only the optional pattern-CRC bit;
				 * clearing both status registers would drop
				 * BRR/TC for data that is already in the FIFO. */
				gta4l_emmc_write16(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_ERROR_INT_STATUS,
				    GTA4L_EMMC_INT_ERR_DATA_CRC);
				gta4l_emmc_barrier(host);
			} else {
				normal = gta4l_emmc_read16(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
				present = gta4l_emmc_read32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_PRESENT_STATE);
				gta4l_emmc_record_failure(host, index,
				    argument, command, error, normal,
				    present);
				gta4l_emmc_clear_interrupts(host);
				return gta4l_emmc_map_error(error, 1);
			}
		}
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		if ((command_done == 0) &&
		    ((normal & GTA4L_EMMC_INT_CC) != 0)) {
			if (response != 0) {
				response[0] = gta4l_emmc_read32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_RESPONSE);
			}
			command_done = 1;
		}
		if ((data_done == 0) && ((normal & GTA4L_EMMC_INT_BRR) != 0)) {
			for (byte = 0; byte < length; byte += 4) {
				uint32_t word = gta4l_emmc_read32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_BUFFER_DATA_PORT);
				bytes[byte + 0] = (uint8_t)(word & 0xFFu);
				bytes[byte + 1] = (uint8_t)((word >> 8) & 0xFFu);
				bytes[byte + 2] = (uint8_t)((word >> 16) & 0xFFu);
				bytes[byte + 3] = (uint8_t)((word >> 24) & 0xFFu);
			}
			data_done = 1;
		}
		if ((command_done != 0) && (data_done != 0) &&
		    ((normal & GTA4L_EMMC_INT_TC) != 0)) {
			/* Re-evaluate the error latch after the response and
			 * FIFO reads so an error that became visible during
			 * them is still recorded before the status clear. An
			 * error after this sample remains unobserved. */
			error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_ERROR_INT_STATUS);
			if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
				if (gta4l_emmc_tolerated_bus_test_crc(host,
				    error) != 0) {
					host->bus_test_ignored_crc = 1;
				} else {
					present = gta4l_emmc_read32(host,
					    GTA4L_EMMC_SPACE_SDHCI,
					    GTA4L_EMMC_REG_PRESENT_STATE);
					gta4l_emmc_record_failure(host,
					    index, argument, command, error,
					    normal, present);
					gta4l_emmc_clear_interrupts(host);
					return gta4l_emmc_map_error(error, 1);
				}
			}
			gta4l_emmc_clear_interrupts(host);
			return GTA4L_EMMC_OK;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}

	error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ERROR_INT_STATUS);
	present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_PRESENT_STATE);
	gta4l_emmc_record_failure(host, index, argument, command, error,
	    normal, present);
	gta4l_emmc_clear_interrupts(host);
	return GTA4L_EMMC_ERR_DATA_TIMEOUT;
}

/* One bounded single-block PIO write. Only the CMD19 bus-test pattern uses
 * this path; block-data write commands are outside the core's scope. */
static int
gta4l_emmc_write_data(struct gta4l_emmc_host *host, uint32_t index,
    uint32_t argument, uint16_t flags, const void *buffer, uint32_t length,
    uint32_t *response)
{
	uint16_t command;
	uint16_t normal = 0;
	uint16_t error = 0;
	uint32_t present;
	uint32_t iterations;
	uint32_t step;
	uint32_t byte;
	const uint8_t *bytes = (const uint8_t *)buffer;
	int command_done = 0;
	int data_done = 0;

	if ((buffer == 0) || ((length % 4) != 0) ||
	    (length == 0) || (length > 0xFFFFu)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	command = (uint16_t)((index << 8) | flags | GTA4L_EMMC_CMD_DATA);
	if (gta4l_emmc_wait_inhibit(host) != GTA4L_EMMC_OK) {
		present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		gta4l_emmc_record_failure(host, index, argument, command, 0, 0,
		    present);
		return GTA4L_EMMC_ERR_INHIBIT;
	}
	gta4l_emmc_clear_interrupts(host);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_BLOCK_SIZE, (uint16_t)length);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_BLOCK_COUNT, 1);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ARGUMENT, argument);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_TRANSFER_MODE, GTA4L_EMMC_TM_BLOCK_COUNT_EN);
	gta4l_emmc_barrier(host);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_COMMAND, command);
	gta4l_emmc_barrier(host);

	if (host->bus_test_window_us != 0u) {
		iterations = host->bus_test_window_us /
		    GTA4L_EMMC_POLL_STEP_US;
		if (iterations == 0u) {
			iterations = 1u;
		}
		if (iterations > GTA4L_EMMC_MAX_BUS_TEST_ITERATIONS) {
			iterations = GTA4L_EMMC_MAX_BUS_TEST_ITERATIONS;
		}
	} else {
		iterations =
		    gta4l_emmc_poll_iterations(GTA4L_EMMC_DATA_TIMEOUT_US);
	}
	for (step = 0; step < iterations; step++) {
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
			normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
			present = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_PRESENT_STATE);
			gta4l_emmc_record_failure(host, index, argument,
			    command, error, normal, present);
			gta4l_emmc_clear_interrupts(host);
			return gta4l_emmc_map_error(error, 1);
		}
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		if ((command_done == 0) &&
		    ((normal & GTA4L_EMMC_INT_CC) != 0)) {
			if (response != 0) {
				response[0] = gta4l_emmc_read32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_RESPONSE);
			}
			command_done = 1;
		}
		if ((data_done == 0) && ((normal & GTA4L_EMMC_INT_BWR) != 0)) {
			for (byte = 0; byte < length; byte += 4) {
				uint32_t word = (uint32_t)bytes[byte] |
				    ((uint32_t)bytes[byte + 1u] << 8) |
				    ((uint32_t)bytes[byte + 2u] << 16) |
				    ((uint32_t)bytes[byte + 3u] << 24);
				gta4l_emmc_write32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_BUFFER_DATA_PORT, word);
			}
			data_done = 1;
		}
		if ((command_done != 0) && (data_done != 0) &&
		    ((normal & GTA4L_EMMC_INT_TC) != 0)) {
			error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_ERROR_INT_STATUS);
			if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
				present = gta4l_emmc_read32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_PRESENT_STATE);
				gta4l_emmc_record_failure(host, index, argument,
				    command, error, normal, present);
				gta4l_emmc_clear_interrupts(host);
				return gta4l_emmc_map_error(error, 1);
			}
			gta4l_emmc_clear_interrupts(host);
			return GTA4L_EMMC_OK;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}

	error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ERROR_INT_STATUS);
	present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_PRESENT_STATE);
	gta4l_emmc_record_failure(host, index, argument, command, error,
	    normal, present);
	gta4l_emmc_clear_interrupts(host);
	return GTA4L_EMMC_ERR_DATA_TIMEOUT;
}

static int
gta4l_emmc_enable_branch(struct gta4l_emmc_host *host, uint32_t offset)
{
	uint32_t value = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_GCC, offset);

	if ((value & GTA4L_EMMC_CBCR_CLOCK_ENABLE) == 0) {
		gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC, offset,
		    value | GTA4L_EMMC_CBCR_CLOCK_ENABLE);
		gta4l_emmc_barrier(host);
	}
	return gta4l_emmc_wait32(host, GTA4L_EMMC_SPACE_GCC, offset,
	    GTA4L_EMMC_CBCR_CLK_OFF, 0, GTA4L_EMMC_CLOCK_TIMEOUT_US);
}

static int
gta4l_emmc_reset_block(struct gta4l_emmc_host *host)
{
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_BCR, GTA4L_EMMC_BCR_BLOCK_RESET);
	gta4l_emmc_barrier(host);
	gta4l_emmc_delay(host, GTA4L_EMMC_BCR_DELAY_US);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_BCR, 0);
	gta4l_emmc_barrier(host);
	gta4l_emmc_delay(host, GTA4L_EMMC_BCR_DELAY_US);
	return GTA4L_EMMC_OK;
}

static int
gta4l_emmc_enable_clocks(struct gta4l_emmc_host *host)
{
	int result;

	result = gta4l_emmc_enable_branch(host,
	    GTA4L_EMMC_GCC_SDCC1_AHB_CBCR);
	if (result != GTA4L_EMMC_OK) {
		return GTA4L_EMMC_ERR_CLOCK;
	}
	result = gta4l_emmc_enable_branch(host,
	    GTA4L_EMMC_GCC_SDCC1_APPS_CBCR);
	if (result != GTA4L_EMMC_OK) {
		return GTA4L_EMMC_ERR_CLOCK;
	}
	return GTA4L_EMMC_OK;
}

/* Program the GCC SDCC1 APPS RCG with the card clock stopped. The caller
 * enables the clock and waits for INT_STABLE afterwards. */
static int
gta4l_emmc_program_rcg(struct gta4l_emmc_host *host, uint32_t config_bits,
    uint32_t m, uint32_t n, uint32_t d, uint32_t *config_out)
{
	uint32_t command;
	uint32_t config;

	/* Stop the card clock before touching the RCG. */
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL, 0);
	gta4l_emmc_barrier(host);

	config = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_CFG);
	config &= ~(GTA4L_EMMC_RCG_SRC_SEL_MASK |
	    GTA4L_EMMC_RCG_SRC_DIV_MASK | GTA4L_EMMC_RCG_MODE_MASK);
	config |= config_bits;
	if (config_out != 0) {
		*config_out = config;
	}
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_M, m);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_N, n);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_D, d);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_CFG, config);
	gta4l_emmc_barrier(host);

	command = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_CMD);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_CMD,
	    command | GTA4L_EMMC_RCG_CMD_UPDATE);
	gta4l_emmc_barrier(host);
	return gta4l_emmc_wait32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_CMD, GTA4L_EMMC_RCG_CMD_UPDATE, 0,
	    GTA4L_EMMC_RCG_TIMEOUT_US);
}

/* Switch the running card clock to `config_bits`/M/N/D and leave the card
 * clock enabled. Returns ERR_CLOCK when the RCG update or the INT_STABLE
 * wait fails; `clock_after` receives the final CLOCK_CONTROL value. */
static int
gta4l_emmc_switch_clock(struct gta4l_emmc_host *host, uint32_t config_bits,
    uint32_t m, uint32_t n, uint32_t d, uint32_t *clock_after)
{
	uint32_t clock;
	int result;

	result = gta4l_emmc_program_rcg(host, config_bits, m, n, d, 0);
	if (result != GTA4L_EMMC_OK) {
		return GTA4L_EMMC_ERR_CLOCK;
	}
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL, GTA4L_EMMC_CLOCK_INT_EN);
	gta4l_emmc_barrier(host);
	result = gta4l_emmc_wait16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL,
	    GTA4L_EMMC_CLOCK_INT_EN | GTA4L_EMMC_CLOCK_INT_STABLE,
	    GTA4L_EMMC_CLOCK_INT_EN | GTA4L_EMMC_CLOCK_INT_STABLE,
	    GTA4L_EMMC_CLOCK_TIMEOUT_US);
	if (result != GTA4L_EMMC_OK) {
		return GTA4L_EMMC_ERR_CLOCK;
	}
	clock = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL,
	    (uint16_t)(clock | GTA4L_EMMC_CLOCK_CARD_EN));
	gta4l_emmc_barrier(host);
	gta4l_emmc_delay(host, GTA4L_EMMC_CARD_CLOCK_SETTLE_US);
	if (clock_after != 0) {
		*clock_after = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CLOCK_CONTROL);
	}
	return GTA4L_EMMC_OK;
}

static int
gta4l_emmc_configure_clock(struct gta4l_emmc_host *host)
{
	uint32_t config;
	int result;

	result = gta4l_emmc_program_rcg(host, GTA4L_EMMC_RCG_400K_CFG,
	    GTA4L_EMMC_RCG_400K_M, GTA4L_EMMC_RCG_400K_N,
	    GTA4L_EMMC_RCG_400K_D, &config);
	if (result != GTA4L_EMMC_OK) {
		return GTA4L_EMMC_ERR_CLOCK;
	}

	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL, GTA4L_EMMC_CLOCK_INT_EN);
	gta4l_emmc_barrier(host);
	result = gta4l_emmc_wait16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL,
	    GTA4L_EMMC_CLOCK_INT_EN | GTA4L_EMMC_CLOCK_INT_STABLE,
	    GTA4L_EMMC_CLOCK_INT_EN | GTA4L_EMMC_CLOCK_INT_STABLE,
	    GTA4L_EMMC_CLOCK_TIMEOUT_US);
	if (result != GTA4L_EMMC_OK) {
		return GTA4L_EMMC_ERR_CLOCK;
	}
	gta4l_emmc_log(host, GTA4L_EMMC_LOG_CLOCK, config,
	    gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL));
	return GTA4L_EMMC_OK;
}

static int
gta4l_emmc_power_on(struct gta4l_emmc_host *host, uint32_t io_request,
    uint32_t io_desired)
{
	uint32_t status;
	uint32_t clear;
	uint32_t ack;
	uint32_t iterations;
	uint32_t index;
	uint32_t attempt;
	int bus_on_seen = 0;
	uint32_t io_level = 0;
	int settled = 0;
	int cleared;

	/* The HOST_CONTROL2 write requested this IO level. A zero value means
	 * the signalling bit did not change, so the controller raises no IO
	 * request and the level is already the requested one. */
	gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_POWER_CONTROL,
	    GTA4L_EMMC_POWER_BUS_POWER | GTA4L_EMMC_POWER_3V3);
	gta4l_emmc_barrier(host);

	/* Bus power and IO voltage are independent controller requests. The
	 * HOST_CONTROL2 write above can raise an IO request before, with or
	 * after the bus request, so handle each observed request separately.
	 * Success requires an observed BUS_ON completion and completion of the
	 * IO transition requested through HOST_CONTROL2; two empty status
	 * reads are not treated as proof that the IO transition finished. */
	iterations = gta4l_emmc_poll_iterations(GTA4L_EMMC_POWER_TIMEOUT_US);
	for (index = 0; index < iterations; index++) {
		status = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_PWRCTL_STATUS) & 0xFu;
		if ((status & GTA4L_EMMC_PWRCTL_BUS_OFF) != 0) {
			gta4l_emmc_log(host, GTA4L_EMMC_LOG_POWER, status, 0);
			return GTA4L_EMMC_ERR_POWER;
		}
		if (status == 0) {
			/* A requested transition must reach the desired level.
			 * When the VDD_180 bit did not change, no request is
			 * due, but an observed wrong level still blocks
			 * success; only an untouched (zero) level or the
			 * desired level is acceptable. */
			if ((bus_on_seen != 0) &&
			    ((io_level == io_desired) ||
			    ((io_request == 0) && (io_level == 0)))) {
				settled = 1;
				break;
			}
			gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
			continue;
		}

		/* Clear only the request bits observed in this pass, retrying
		 * the documented lost-pulse case. */
		clear = status;
		cleared = 0;
		for (attempt = 0; attempt < 10; attempt++) {
			gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_PWRCTL_CLEAR, clear);
			gta4l_emmc_barrier(host);
			if ((gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_CORE_PWRCTL_STATUS) & clear) == 0) {
				cleared = 1;
				break;
			}
			gta4l_emmc_delay(host, 10);
		}
		if (cleared == 0) {
			gta4l_emmc_log(host, GTA4L_EMMC_LOG_POWER, status, 0);
			return GTA4L_EMMC_ERR_POWER;
		}

		ack = 0;
		/* Apply the Linux handler order: BUS_ON implies IO-high, an
		 * IO-low request overrides it, and an IO-high request overrides
		 * IO-low. The latest acknowledged level replaces the previous
		 * one, so a later opposite or standalone bus request
		 * invalidates an earlier completion. */
		if ((status & GTA4L_EMMC_PWRCTL_BUS_ON) != 0) {
			/* Linux treats BUS_ON as an implicit IO-high
			 * transition and acknowledges both completions. */
			ack |= GTA4L_EMMC_PWRCTL_ACK_BUS |
			    GTA4L_EMMC_PWRCTL_ACK_IO;
			bus_on_seen = 1;
			io_level = GTA4L_EMMC_PWRCTL_IO_HIGH;
		}
		if ((status & GTA4L_EMMC_PWRCTL_IO_LOW) != 0) {
			ack |= GTA4L_EMMC_PWRCTL_ACK_IO;
			io_level = GTA4L_EMMC_PWRCTL_IO_LOW;
		}
		if ((status & GTA4L_EMMC_PWRCTL_IO_HIGH) != 0) {
			ack |= GTA4L_EMMC_PWRCTL_ACK_IO;
			io_level = GTA4L_EMMC_PWRCTL_IO_HIGH;
		}
		gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_PWRCTL_CTL, ack);
		gta4l_emmc_barrier(host);
		if ((gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_PWRCTL_STATUS) & clear) != 0) {
			gta4l_emmc_log(host, GTA4L_EMMC_LOG_POWER, status, ack);
			return GTA4L_EMMC_ERR_POWER;
		}
		gta4l_emmc_log(host, GTA4L_EMMC_LOG_POWER, status, ack);
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}

	if (settled == 0) {
		/* The loop must end on a quiet window with both completions
		 * observed; exhaustion is a power failure. */
		gta4l_emmc_log(host, GTA4L_EMMC_LOG_POWER, io_desired, io_level);
		return GTA4L_EMMC_ERR_POWER;
	}
	return GTA4L_EMMC_OK;
}

static int
gta4l_emmc_configure_pads(struct gta4l_emmc_host *host)
{
	uint32_t before = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_TLMM,
	    GTA4L_EMMC_TLMM_SDC1_QDSD);

	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_TLMM,
	    GTA4L_EMMC_TLMM_SDC1_QDSD, GTA4L_EMMC_SDC1_PAD_ACTIVE);
	gta4l_emmc_barrier(host);
	gta4l_emmc_log(host, GTA4L_EMMC_LOG_PAD, before,
	    GTA4L_EMMC_SDC1_PAD_ACTIVE);
	return GTA4L_EMMC_OK;
}

/* Linux sdhci-msm at 100 MHz and below uses the feedback clock: the DLL is
 * reset and powered down, the CDCLP533 power-save bit is cleared, and
 * CORE_VENDOR_SPEC selects the free-running MCLK without the HC_SELECT_IN
 * override. The vendor UEFI writes the same DLL_RST|DLL_PDN pair. */
static void
gta4l_emmc_configure_low_speed(struct gta4l_emmc_host *host)
{
	uint32_t config;

	config = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_VENDOR_SPEC3);
	config &= ~GTA4L_EMMC_VENDOR3_PWRSAVE_DLL;
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_VENDOR_SPEC3, config);
	gta4l_emmc_barrier(host);

	config = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	config |= GTA4L_EMMC_DLL_RST;
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_DLL_CONFIG, config);
	config = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	config |= GTA4L_EMMC_DLL_PDN;
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_DLL_CONFIG, config);
	gta4l_emmc_barrier(host);

	config = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_VENDOR_SPEC);
	config &= ~(GTA4L_EMMC_VENDOR_HC_MCLK_SEL_MASK |
	    GTA4L_EMMC_VENDOR_HC_SELECT_IN_EN |
	    GTA4L_EMMC_VENDOR_HC_SELECT_IN_MASK |
	    GTA4L_EMMC_VENDOR_CLK_PWRSAVE);
	config |= GTA4L_EMMC_VENDOR_HC_MCLK_SEL_DFLT;
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_VENDOR_SPEC, config);
	gta4l_emmc_barrier(host);
}

static int
gta4l_emmc_enable_card_clock(struct gta4l_emmc_host *host)
{
	uint16_t clock = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL);

	clock |= GTA4L_EMMC_CLOCK_INT_EN | GTA4L_EMMC_CLOCK_CARD_EN;
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL, clock);
	gta4l_emmc_barrier(host);
	gta4l_emmc_delay(host, GTA4L_EMMC_CARD_CLOCK_SETTLE_US);
	return GTA4L_EMMC_OK;
}

int
gta4l_emmc_controller_init(struct gta4l_emmc_host *host)
{
	uint32_t version;
	uint32_t vendor;
	uint32_t io_desired;
	uint32_t io_request;
	uint16_t control2;
	int result;

	if ((host == 0) || (host->ops == 0)) {
		return GTA4L_EMMC_ERR_ARG;
	}

	host->last_step = GTA4L_EMMC_STEP_NONE;
	gta4l_emmc_clear_failure(host);
	host->cmd1_attempts = 0;
	host->cmd1_timeouts = 0;
	host->clocks_enabled = 0;
	result = gta4l_emmc_reset_block(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	result = gta4l_emmc_enable_clocks(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	host->clocks_enabled = 1;
	version = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_MCI_VERSION);
	if ((version >> 28) != 1u) {
		gta4l_emmc_log(host, GTA4L_EMMC_LOG_ERROR, 1, version);
		return GTA4L_EMMC_ERR_VERSION;
	}
	host->mci_version = version;

	/* Suppress interrupt signalling and status before resetting. */
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_NORMAL_INT_ENABLE, 0);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ERROR_INT_ENABLE, 0);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_NORMAL_INT_SIGNAL, 0);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ERROR_INT_SIGNAL, 0);
	gta4l_emmc_barrier(host);

	gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_SOFTWARE_RESET, GTA4L_EMMC_RESET_ALL);
	gta4l_emmc_barrier(host);
	if (gta4l_emmc_wait8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_SOFTWARE_RESET, 0xFFu, 0,
	    GTA4L_EMMC_RESET_TIMEOUT_US) != GTA4L_EMMC_OK) {
		return GTA4L_EMMC_ERR_RESET;
	}
	host->last_step = GTA4L_EMMC_STEP_CONTROLLER_RESET;

	version = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_MCI_VERSION);
	if ((version >> 28) != 1u) {
		return GTA4L_EMMC_ERR_VERSION;
	}

	/* Linux and the vendor UEFI restore the documented vendor POR value
	 * after reset, then apply the pad policy and low-speed fields. */
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_VENDOR_SPEC, GTA4L_EMMC_VENDOR_SPEC_POR);
	gta4l_emmc_barrier(host);

	gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_TIMEOUT_CONTROL, GTA4L_EMMC_TIMEOUT_MAX);
	gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL, 0);

	/* Legacy timing: clear the UHS mode and execute field, preserve the
	 * rest. The board's VCCQ rail is fixed at 1.8 V, so select 1.8 V
	 * signalling. The controller raises an IO request only when the
	 * VDD_180 bit changes; record that requested transition so the power
	 * loop can wait for its completion instead of inferring it from a
	 * quiet status window. */
	control2 = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL2);
	io_desired = (host->fixed_1v8 != 0) ?
	    GTA4L_EMMC_PWRCTL_IO_LOW : GTA4L_EMMC_PWRCTL_IO_HIGH;
	io_request = 0;
	if (((control2 & GTA4L_EMMC_HOST_CONTROL2_VDD_180) != 0) !=
	    (host->fixed_1v8 != 0)) {
		io_request = io_desired;
	}
	control2 &= ~(GTA4L_EMMC_HOST_CONTROL2_UHS_MASK |
	    GTA4L_EMMC_HOST_CONTROL2_TUNING);
	if (host->fixed_1v8 != 0) {
		control2 |= GTA4L_EMMC_HOST_CONTROL2_VDD_180;
	} else {
		control2 &= ~GTA4L_EMMC_HOST_CONTROL2_VDD_180;
	}
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL2, control2);

	/* The pad power switch is a separate board policy. A fixed-1.8 V
	 * VCCQ target keeps it set so the pads follow the actual rail; a
	 * 3.3 V-capable target leaves both vendor bits clear. */
	vendor = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_VENDOR_SPEC);
	if (host->pad_pwr_switch != 0) {
		vendor |= GTA4L_EMMC_VENDOR_PAD_PWR_SWITCH_EN |
		    GTA4L_EMMC_VENDOR_PAD_PWR_SWITCH;
	} else {
		vendor &= ~(GTA4L_EMMC_VENDOR_PAD_PWR_SWITCH_EN |
		    GTA4L_EMMC_VENDOR_PAD_PWR_SWITCH);
	}
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CORE_VENDOR_SPEC, vendor);
	gta4l_emmc_barrier(host);

	gta4l_emmc_configure_low_speed(host);
	host->last_step = GTA4L_EMMC_STEP_VENDOR_CONFIG;

	gta4l_emmc_clear_interrupts(host);
	gta4l_emmc_enable_status(host);

	result = gta4l_emmc_configure_clock(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	host->last_step = GTA4L_EMMC_STEP_CLOCK;
	result = gta4l_emmc_power_on(host, io_request, io_desired);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	host->last_step = GTA4L_EMMC_STEP_POWER;
	gta4l_emmc_configure_pads(host);
	host->last_step = GTA4L_EMMC_STEP_PADS;

	/* Let power and pads settle before the card sees a clock. */
	gta4l_emmc_delay(host, GTA4L_EMMC_POWER_SETTLE_US);
	result = gta4l_emmc_enable_card_clock(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	host->last_step = GTA4L_EMMC_STEP_CARD_CLOCK;

	gta4l_emmc_log(host, GTA4L_EMMC_LOG_RESET, version,
	    gta4l_emmc_read8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_POWER_CONTROL));
	return GTA4L_EMMC_OK;
}

static int
gta4l_emmc_card_ready(struct gta4l_emmc_host *host)
{
	uint32_t response[4];
	uint32_t iterations = GTA4L_EMMC_CMD1_TIMEOUT_US /
	    GTA4L_EMMC_CMD1_ATTEMPT_US;
	uint32_t index;
	int result;

	host->cmd1_attempts = 0;
	host->cmd1_timeouts = 0;
	for (index = 0; index < iterations; index++) {
		/* A busy CMD1 timeout is expected and retried. Retire it before
		 * the next attempt so it cannot mask the command that finally
		 * ends initialization. If every attempt times out, the last
		 * timeout remains recorded. */
		gta4l_emmc_clear_failure(host);
		host->cmd1_attempts++;
		result = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD1,
		    GTA4L_EMMC_OCR_VALUE, GTA4L_EMMC_CMD_RESP_SHORT,
		    GTA4L_EMMC_CMD1_ATTEMPT_US, response);
		if (result == GTA4L_EMMC_OK) {
			if ((response[0] & 0x80000000u) != 0) {
				host->ocr = response[0];
				host->cmd1_response_valid = 1;
				/* Timing candidate only: give the controller's
				 * internal CRC source the downstream 8 us
				 * retirement window after a successful CMD1,
				 * before the next command. No retry, timeout,
				 * clock, rail, command-flag or MMIO-width
				 * change is part of this candidate. */
				gta4l_emmc_delay(host,
				    GTA4L_EMMC_CMD1_CRC_RETIRE_US);
				return GTA4L_EMMC_OK;
			}
		} else if (result != GTA4L_EMMC_ERR_CMD_TIMEOUT) {
			return result;
		} else {
			host->cmd1_timeouts++;
		}
		gta4l_emmc_delay(host, 1000);
	}
	return GTA4L_EMMC_ERR_CARD_NOT_READY;
}

static int
gta4l_emmc_validate_response(const uint32_t response[4])
{
	uint32_t index;
	int all_zero = 1;
	int all_ones = 1;

	for (index = 0; index < 4; index++) {
		if (response[index] != 0) {
			all_zero = 0;
		}
		/* The low byte of the last word is the stripped CRC, not
		 * response data, so mask it for the all-ones test. */
		if (index == 3u) {
			if ((response[index] | 0xFFu) != 0xFFFFFFFFu) {
				all_ones = 0;
			}
		} else if (response[index] != 0xFFFFFFFFu) {
			all_ones = 0;
		}
	}
	if (all_zero || all_ones) {
		return GTA4L_EMMC_ERR_RESPONSE;
	}
	return GTA4L_EMMC_OK;
}

static int
gta4l_emmc_wait_tran(struct gta4l_emmc_host *host)
{
	uint32_t response[4];
	uint32_t iterations = GTA4L_EMMC_CMD_TIMEOUT_US /
	    GTA4L_EMMC_CMD13_ATTEMPT_US;
	uint32_t index;
	int result;

	for (index = 0; index < iterations; index++) {
		result = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD13,
		    host->rca << 16, GTA4L_EMMC_CMD_RESP_SHORT |
		    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX,
		    GTA4L_EMMC_CMD13_ATTEMPT_US, response);
		if (result != GTA4L_EMMC_OK) {
			return result;
		}
		if ((response[0] & GTA4L_EMMC_R1_ERROR_MASK) != 0) {
			host->card_status = response[0];
			return GTA4L_EMMC_ERR_CARD_STATUS;
		}
		host->card_status = response[0];
		if ((((response[0] >> GTA4L_EMMC_R1_STATE_SHIFT) & 0xFu) ==
		    GTA4L_EMMC_R1_STATE_TRAN) &&
		    ((response[0] & GTA4L_EMMC_R1_READY_FOR_DATA) != 0)) {
			return GTA4L_EMMC_OK;
		}
		gta4l_emmc_delay(host, 1000);
	}
	return GTA4L_EMMC_ERR_CARD_NOT_READY;
}

int
gta4l_emmc_read_ext_csd(struct gta4l_emmc_host *host, void *buffer)
{
	uint32_t response = 0;
	int result;

	if ((host == 0) || (buffer == 0)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	result = gta4l_emmc_read_data(host, GTA4L_EMMC_CMD8, 0,
	    GTA4L_EMMC_CMD_RESP_SHORT | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX, buffer, GTA4L_EMMC_EXT_CSD_BYTES,
	    &response);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	if ((response & GTA4L_EMMC_R1_ERROR_MASK) != 0) {
		return GTA4L_EMMC_ERR_CARD_STATUS;
	}
	return GTA4L_EMMC_OK;
}

static uint32_t
gta4l_emmc_bus_width_code(uint32_t bits)
{
	if (bits == 1u) {
		return GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1;
	}
	if (bits == 4u) {
		return GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4;
	}
	if (bits == 8u) {
		return GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8;
	}
	return 0xFFFFFFFFu;
}

static uint32_t
gta4l_emmc_ext_csd_sum(const uint8_t *ext)
{
	uint32_t sum = 0;
	uint32_t index;

	for (index = 0; index < GTA4L_EMMC_EXT_CSD_BYTES; index++) {
		sum += ext[index];
	}
	return sum;
}

/* Read-only EXT_CSD properties segment comparison. BUS_WIDTH itself is
 * W/E_P and not readable, so this is the portable positive check that the
 * data path still carries the card's identity at the attempted width. */
static int
gta4l_emmc_ext_csd_properties_same(const uint8_t *a, const uint8_t *b)
{
	uint32_t index;

	for (index = GTA4L_EMMC_EXT_CSD_PROPERTIES_OFF;
	    index < GTA4L_EMMC_EXT_CSD_BYTES; index++) {
		if (a[index] != b[index]) {
			return 0;
		}
	}
	return 1;
}

/* One CMD6 BUS_WIDTH write and TRAN wait, without touching the host width.
 * The command/response path is independent of the DAT width, so this can
 * restore the card while the host still drives the attempted width. */
static int
gta4l_emmc_width_cmd6(struct gta4l_emmc_host *host, uint32_t code,
    uint32_t *switch_status)
{
	uint32_t response[4];
	uint32_t argument;
	int rc;

	argument = GTA4L_EMMC_SWITCH_ACCESS_WRITE |
	    (GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF << 16) |
	    (code << 8) | GTA4L_EMMC_SWITCH_CMD_SET_NORMAL;
	rc = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD6, argument,
	    GTA4L_EMMC_CMD_RESP_SHORT | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX, GTA4L_EMMC_CMD_TIMEOUT_US, response);
	if ((rc == GTA4L_EMMC_OK) && (switch_status != 0)) {
		*switch_status = response[0];
	}
	if (rc != GTA4L_EMMC_OK) {
		return rc;
	}
	if ((response[0] & GTA4L_EMMC_R1_ERROR_MASK) != 0) {
		host->card_status = response[0];
		return GTA4L_EMMC_ERR_CARD_STATUS;
	}
	return gta4l_emmc_wait_tran(host);
}

/* Restore the card and host after a failed width verification. The card may
 * or may not have accepted the CMD6, so try the restore command while the
 * host still drives the attempted width, then fall back to the previous host
 * width. Success is proven only by a coherent EXT_CSD read whose read-only
 * properties match the baseline; otherwise the framing is unknown. */
static int
gta4l_emmc_width_rollback(struct gta4l_emmc_host *host, uint32_t previous_code,
    uint32_t attempted_code, uint8_t previous_control, const uint8_t *baseline,
    struct gta4l_emmc_bus_result *result)
{
	uint8_t ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t status;
	int rc;

	if (result != 0) {
		result->rollback_result = GTA4L_EMMC_ERR_WIDTH_RESTORE;
	}
	if (attempted_code != previous_code) {
		if (result != 0) {
			result->cmd6_attempts++;
		}
		rc = gta4l_emmc_width_cmd6(host, previous_code, &status);
		if (rc == GTA4L_EMMC_OK) {
			gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_HOST_CONTROL, previous_control);
			gta4l_emmc_barrier(host);
			rc = gta4l_emmc_read_ext_csd(host, ext_csd);
			if ((rc == GTA4L_EMMC_OK) &&
			    (gta4l_emmc_ext_csd_properties_same(baseline,
			    ext_csd) != 0)) {
				host->bus_width = (uint8_t)previous_code;
				if (result != 0) {
					result->rollback_result =
					    GTA4L_EMMC_OK;
					result->host_control =
					    previous_control;
				}
				return GTA4L_EMMC_OK;
			}
		}
	}
	/* The first restore path did not prove the framing. Return the host to
	 * the previous width and try the same command there. */
	gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL, previous_control);
	gta4l_emmc_barrier(host);
	if (result != 0) {
		result->cmd6_attempts++;
	}
	rc = gta4l_emmc_width_cmd6(host, previous_code, &status);
	if (rc != GTA4L_EMMC_OK) {
		return GTA4L_EMMC_ERR_WIDTH_RESTORE;
	}
	rc = gta4l_emmc_read_ext_csd(host, ext_csd);
	if ((rc != GTA4L_EMMC_OK) ||
	    (gta4l_emmc_ext_csd_properties_same(baseline, ext_csd) == 0)) {
		return GTA4L_EMMC_ERR_WIDTH_RESTORE;
	}
	host->bus_width = (uint8_t)previous_code;
	if (result != 0) {
		result->rollback_result = GTA4L_EMMC_OK;
		result->host_control = previous_control;
	}
	return GTA4L_EMMC_OK;
}

/* Reset one bus-result record to the values observed before a width step.
 * `control` is the HOST_CONTROL byte read before the step. */
static void
gta4l_emmc_bus_result_init(struct gta4l_emmc_bus_result *result,
    uint32_t bits, uint32_t control)
{
	if (result == 0) {
		return;
	}
	result->requested = bits;
	result->switched = 0;
	result->switch_result = GTA4L_EMMC_OK;
	result->test_result = GTA4L_EMMC_OK;
	result->test_word = 0;
	result->test_expected = 0;
	result->host_control = control;
	result->switch_status = 0;
	result->host_before = (uint8_t)control;
	result->host_after = (uint8_t)control;
	result->readback_sum = 0;
	result->verified = 0;
	result->rollback_result = GTA4L_EMMC_OK;
	result->cmd6_attempts = 0;
	result->write_result = GTA4L_EMMC_OK;
	result->reset_result = GTA4L_EMMC_OK;
}

/* Clear a latched DAT inhibit with one bounded local data-circuit reset.
 * Returns GTA4L_EMMC_OK only when the reset self-cleared and PRESENT_STATE
 * reports no DAT inhibit; the card-side framing is still proven separately by
 * the read-only EXT_CSD comparison. */
static int
gta4l_emmc_clear_dat_inhibit(struct gta4l_emmc_host *host)
{
	uint32_t present;
	uint32_t readback;
	uint32_t iterations;
	uint32_t index;

	present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_PRESENT_STATE);
	if ((present & GTA4L_EMMC_PRESENT_DAT_INHIBIT) == 0u) {
		return GTA4L_EMMC_OK;
	}
	gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_SOFTWARE_RESET, GTA4L_EMMC_RESET_DATA);
	gta4l_emmc_barrier(host);
	iterations = gta4l_emmc_poll_iterations(GTA4L_EMMC_RESET_TIMEOUT_US);
	readback = gta4l_emmc_read8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_SOFTWARE_RESET);
	for (index = 1; (readback != 0u) && (index < iterations); index++) {
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
		readback = gta4l_emmc_read8(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_SOFTWARE_RESET);
	}
	if (readback != 0u) {
		return GTA4L_EMMC_ERR_RESET;
	}
	present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_PRESENT_STATE);
	if ((present & GTA4L_EMMC_PRESENT_DAT_INHIBIT) != 0u) {
		return GTA4L_EMMC_ERR_INHIBIT;
	}
	return GTA4L_EMMC_OK;
}

/* CMD6 write-byte switch plus the matching SDHCI HOST_CONTROL width. The
 * legacy mode (verify_readonly == 0) keeps the historical byte-183 equality
 * check used by the already-flashed B1/H10/H11 stages. BUS_WIDTH is W/E_P and
 * not readable, so that check cannot prove a switch; new work must use the
 * verified mode, which compares read-only properties and rolls both sides
 * back on failure. */
static int
gta4l_emmc_set_bus_width_value(struct gta4l_emmc_host *host, uint32_t bits,
    uint32_t code, struct gta4l_emmc_bus_result *result, int verify_readonly)
{
	uint8_t ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint8_t baseline[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t readback;
	uint32_t control;
	uint32_t previous_control;
	uint32_t previous_code;
	uint32_t expected_mask;
	int rc;
	int restore;

	if ((host == 0) || (host->initialized == 0)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	previous_code = (uint32_t)host->bus_width;
	previous_control = gta4l_emmc_read8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	gta4l_emmc_bus_result_init(result, bits, previous_control);
	if (verify_readonly != 0) {
		rc = gta4l_emmc_read_ext_csd(host, baseline);
		if (rc != GTA4L_EMMC_OK) {
			if (result != 0) {
				result->switch_result = rc;
			}
			return rc;
		}
	}
	if ((verify_readonly != 0) && (code == previous_code)) {
		/* The verified path knows the card and host are coherent at
		 * this width; no CMD6 is required. The legacy path must still
		 * issue the revert even when its cached width is unchanged,
		 * because a failed legacy switch may have reached the card. */
		if (result != 0) {
			result->verified = 1;
		}
		return GTA4L_EMMC_OK;
	}

	if (result != 0) {
		result->cmd6_attempts++;
	}
	rc = gta4l_emmc_width_cmd6(host, code,
	    (result != 0) ? &result->switch_status : 0);
	if (rc != GTA4L_EMMC_OK) {
		if (result != 0) {
			result->switch_result = rc;
		}
		if (verify_readonly != 0) {
			/* The response failed, but the card may have applied
			 * the switch. Restore and verify both sides before
			 * returning; otherwise the framing is unknown. */
			restore = gta4l_emmc_width_rollback(host, previous_code,
			    code, (uint8_t)previous_control, baseline,
			    result);
			return (restore == GTA4L_EMMC_OK) ? rc :
			    GTA4L_EMMC_ERR_WIDTH_RESTORE;
		}
		return rc;
	}

	control = previous_control;
	control &= ~(GTA4L_EMMC_HOST_CONTROL_4BIT |
	    GTA4L_EMMC_HOST_CONTROL_8BIT);
	if (bits == 4u) {
		control |= GTA4L_EMMC_HOST_CONTROL_4BIT;
	} else if (bits == 8u) {
		control |= GTA4L_EMMC_HOST_CONTROL_8BIT;
	}
	gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL, control);
	gta4l_emmc_barrier(host);
	readback = gta4l_emmc_read8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	if (result != 0) {
		result->host_after = (uint8_t)readback;
		result->host_control = control;
	}
	expected_mask = (bits == 4u) ? GTA4L_EMMC_HOST_CONTROL_4BIT :
	    ((bits == 8u) ? GTA4L_EMMC_HOST_CONTROL_8BIT : 0u);
	if ((readback & (GTA4L_EMMC_HOST_CONTROL_4BIT |
	    GTA4L_EMMC_HOST_CONTROL_8BIT)) != expected_mask) {
		if (result != 0) {
			result->switch_result = GTA4L_EMMC_ERR_BUS_WIDTH;
		}
		if (verify_readonly != 0) {
			restore = gta4l_emmc_width_rollback(host, previous_code,
			    code, (uint8_t)previous_control, baseline,
			    result);
			return (restore == GTA4L_EMMC_OK) ?
			    GTA4L_EMMC_ERR_BUS_WIDTH :
			    GTA4L_EMMC_ERR_WIDTH_RESTORE;
		}
		gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_HOST_CONTROL,
		    (uint8_t)previous_control);
		gta4l_emmc_barrier(host);
		return GTA4L_EMMC_ERR_BUS_WIDTH;
	}

	/* Re-read EXT_CSD at the new width: this exercises the data path and,
	 * in verified mode, compares read-only properties. Byte 183 itself is
	 * W/E_P and not readable; its value is recorded only as a diagnostic. */
	rc = gta4l_emmc_read_ext_csd(host, ext_csd);
	if (rc != GTA4L_EMMC_OK) {
		if (result != 0) {
			result->switch_result = rc;
		}
		if (verify_readonly != 0) {
			restore = gta4l_emmc_width_rollback(host, previous_code,
			    code, (uint8_t)previous_control, baseline,
			    result);
			return (restore == GTA4L_EMMC_OK) ? rc :
			    GTA4L_EMMC_ERR_WIDTH_RESTORE;
		}
		gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_HOST_CONTROL,
		    (uint8_t)previous_control);
		gta4l_emmc_barrier(host);
		return rc;
	}
	readback = (uint32_t)ext_csd[GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF] & 0x0Fu;
	if (result != 0) {
		result->readback_sum = gta4l_emmc_ext_csd_sum(ext_csd);
		result->switched = readback;
	}
	if (verify_readonly != 0) {
		if (gta4l_emmc_ext_csd_properties_same(baseline,
		    ext_csd) == 0) {
			if (result != 0) {
				result->switch_result =
				    GTA4L_EMMC_ERR_BUS_WIDTH;
			}
			restore = gta4l_emmc_width_rollback(host, previous_code,
			    code, (uint8_t)previous_control, baseline,
			    result);
			return (restore == GTA4L_EMMC_OK) ?
			    GTA4L_EMMC_ERR_BUS_WIDTH :
			    GTA4L_EMMC_ERR_WIDTH_RESTORE;
		}
		if (result != 0) {
			result->verified = 1;
		}
	} else if (readback != code) {
		/* Historical legacy check retained for old diagnostic stages. */
		gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_HOST_CONTROL,
		    (uint8_t)previous_control);
		gta4l_emmc_barrier(host);
		if (result != 0) {
			result->switch_result = GTA4L_EMMC_ERR_BUS_WIDTH;
			result->host_control = previous_control;
		}
		return GTA4L_EMMC_ERR_BUS_WIDTH;
	}

	host->bus_width = (uint8_t)code;
	host->last_step = GTA4L_EMMC_STEP_BUS_WIDTH;
	gta4l_emmc_log(host, GTA4L_EMMC_LOG_BUS, bits,
	    (code << 8) | readback);
	return GTA4L_EMMC_OK;
}

int
gta4l_emmc_set_bus_width(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_result *result)
{
	uint32_t code;

	code = gta4l_emmc_bus_width_code(bits);
	if (code == 0xFFFFFFFFu) {
		return GTA4L_EMMC_ERR_ARG;
	}
	return gta4l_emmc_set_bus_width_value(host, bits, code, result, 0);
}

int
gta4l_emmc_set_bus_width_verified(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_result *result)
{
	uint32_t code;

	code = gta4l_emmc_bus_width_code(bits);
	if (code == 0xFFFFFFFFu) {
		return GTA4L_EMMC_ERR_ARG;
	}
	return gta4l_emmc_set_bus_width_value(host, bits, code, result, 1);
}

int
gta4l_emmc_set_bus_width_reserved4(struct gta4l_emmc_host *host,
    struct gta4l_emmc_bus_result *result)
{
	return gta4l_emmc_set_bus_width_value(host, 4u,
	    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_RESERVED_4, result, 0);
}

int
gta4l_emmc_set_bus_width_recover(struct gta4l_emmc_host *host, uint32_t bits,
    const uint8_t *baseline, struct gta4l_emmc_bus_result *result)
{
	uint8_t ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t code;
	uint32_t control;
	uint32_t previous_control;
	uint32_t expected_mask;
	uint32_t readback;
	uint32_t status;
	int rc;
	int attempt;

	if ((host == 0) || (host->initialized == 0) || (baseline == 0)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	code = gta4l_emmc_bus_width_code(bits);
	if (code == 0xFFFFFFFFu) {
		return GTA4L_EMMC_ERR_ARG;
	}
	previous_control = gta4l_emmc_read8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	gta4l_emmc_bus_result_init(result, bits, previous_control);

	control = previous_control;
	control &= ~(GTA4L_EMMC_HOST_CONTROL_4BIT |
	    GTA4L_EMMC_HOST_CONTROL_8BIT);
	if (bits == 4u) {
		control |= GTA4L_EMMC_HOST_CONTROL_4BIT;
	} else if (bits == 8u) {
		control |= GTA4L_EMMC_HOST_CONTROL_8BIT;
	}
	expected_mask = (bits == 4u) ? GTA4L_EMMC_HOST_CONTROL_4BIT :
	    ((bits == 8u) ? GTA4L_EMMC_HOST_CONTROL_8BIT : 0u);

	/* Two attempts cover a lost CMD6 write and a data reset that had not
	 * yet cleared the inhibit. The command path is width-independent, so
	 * the card can be restored even when the DAT path is still wedged. */
	for (attempt = 0; attempt < 2; attempt++) {
		/* A failed data transfer can leave DAT_INHIBIT latched. The
		 * bounded local reset is the only way to regain command
		 * admission, so it runs before CMD6; the card command itself
		 * is width-independent. */
		rc = gta4l_emmc_clear_dat_inhibit(host);
		if (rc != GTA4L_EMMC_OK) {
			if (result != 0) {
				result->switch_result = rc;
				result->rollback_result =
				    GTA4L_EMMC_ERR_WIDTH_RESTORE;
			}
			return GTA4L_EMMC_ERR_WIDTH_RESTORE;
		}
		if (result != 0) {
			result->cmd6_attempts++;
		}
		rc = gta4l_emmc_width_cmd6(host, code, &status);
		if (result != 0) {
			result->switch_status = status;
		}
		if (rc != GTA4L_EMMC_OK) {
			/* The card command failed; put the host back at the
			 * width it was driving and report unknown framing. */
			gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_HOST_CONTROL,
			    (uint8_t)previous_control);
			gta4l_emmc_barrier(host);
			if (result != 0) {
				result->switch_result = rc;
				result->rollback_result =
				    GTA4L_EMMC_ERR_WIDTH_RESTORE;
			}
			return GTA4L_EMMC_ERR_WIDTH_RESTORE;
		}

		gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_HOST_CONTROL, control);
		gta4l_emmc_barrier(host);
		readback = gta4l_emmc_read8(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_HOST_CONTROL);
		if (result != 0) {
			result->host_after = (uint8_t)readback;
			result->host_control = control;
		}
		if ((readback & (GTA4L_EMMC_HOST_CONTROL_4BIT |
		    GTA4L_EMMC_HOST_CONTROL_8BIT)) != expected_mask) {
			rc = GTA4L_EMMC_ERR_BUS_WIDTH;
		} else {
			rc = gta4l_emmc_read_ext_csd(host, ext_csd);
			if ((rc == GTA4L_EMMC_OK) &&
			    (gta4l_emmc_ext_csd_properties_same(
			    baseline, ext_csd) != 0)) {
				host->bus_width = (uint8_t)code;
				if (result != 0) {
					result->verified = 1;
					result->rollback_result =
					    GTA4L_EMMC_OK;
					result->readback_sum =
					    gta4l_emmc_ext_csd_sum(
					    ext_csd);
				}
				return GTA4L_EMMC_OK;
			}
		}

		/* Put the host back where the failed step left it before the
		 * retry, so both attempts present the same width to the card. */
		gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_HOST_CONTROL,
		    (uint8_t)previous_control);
		gta4l_emmc_barrier(host);
	}
	if (result != 0) {
		result->switch_result = rc;
		result->rollback_result = GTA4L_EMMC_ERR_WIDTH_RESTORE;
	}
	return GTA4L_EMMC_ERR_WIDTH_RESTORE;
}

/* CMD19/CMD14 bus test. 4-bit and 8-bit patterns/comparison follow Linux
 * mmc_send_bus_test(). The 1-bit form uses the JESD84-B51 0x80 transmit
 * pattern and checks only the first two returned bits (0b01), because only
 * those bits are defined for DAT0. `verified` additionally tolerates the
 * optional pattern CRC16 indication on CMD14. */
static int
gta4l_emmc_bus_test_common(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_result *result, int verified)
{
	uint8_t pattern[GTA4L_EMMC_BUS_TEST_8_BYTES];
	uint8_t actual[GTA4L_EMMC_BUS_TEST_8_BYTES];
	uint32_t response[4] = { 0 };
	uint32_t length;
	uint32_t code;
	uint32_t control;
	uint32_t index;
	uint32_t test_word = 0;
	uint32_t expected_word = 0;
	int rc;

	if ((host == 0) || (host->initialized == 0)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	code = gta4l_emmc_bus_width_code(bits);
	if (code == 0xFFFFFFFFu) {
		return GTA4L_EMMC_ERR_ARG;
	}
	/* The card field and the SDHCI HOST_CONTROL width must both already
	 * describe the requested width; the test must not run on a mismatched
	 * bus. */
	control = gta4l_emmc_read8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	if (host->bus_width != (uint8_t)code) {
		return GTA4L_EMMC_ERR_BUS_WIDTH;
	}
	if (bits == 1u) {
		if ((control & (GTA4L_EMMC_HOST_CONTROL_4BIT |
		    GTA4L_EMMC_HOST_CONTROL_8BIT)) != 0u) {
			return GTA4L_EMMC_ERR_BUS_WIDTH;
		}
	} else if (bits == 4u) {
		if ((control & (GTA4L_EMMC_HOST_CONTROL_4BIT |
		    GTA4L_EMMC_HOST_CONTROL_8BIT)) !=
		    GTA4L_EMMC_HOST_CONTROL_4BIT) {
			return GTA4L_EMMC_ERR_BUS_WIDTH;
		}
	} else if ((control & (GTA4L_EMMC_HOST_CONTROL_4BIT |
	    GTA4L_EMMC_HOST_CONTROL_8BIT)) != GTA4L_EMMC_HOST_CONTROL_8BIT) {
		return GTA4L_EMMC_ERR_BUS_WIDTH;
	}
	if (result != 0) {
		result->requested = bits;
		result->switched = code;
		result->switch_result = GTA4L_EMMC_OK;
		result->test_result = GTA4L_EMMC_OK;
		result->test_word = 0;
		result->test_expected = 0;
		result->host_control = control;
	}

	length = (bits == 8u) ? GTA4L_EMMC_BUS_TEST_8_BYTES :
	    (bits == 1u) ? GTA4L_EMMC_BUS_TEST_1_BYTES :
	    GTA4L_EMMC_BUS_TEST_4_BYTES;
	for (index = 0; index < GTA4L_EMMC_BUS_TEST_8_BYTES; index++) {
		pattern[index] = 0;
		actual[index] = 0;
	}
	if (bits == 1u) {
		pattern[0] = GTA4L_EMMC_BUS_TEST_PATTERN_1;
	} else if (bits == 4u) {
		pattern[0] = GTA4L_EMMC_BUS_TEST_PATTERN_4;
	} else {
		pattern[0] = GTA4L_EMMC_BUS_TEST_PATTERN_8_0;
		pattern[1] = GTA4L_EMMC_BUS_TEST_PATTERN_8_1;
	}

	rc = gta4l_emmc_write_data(host, GTA4L_EMMC_CMD19, 0,
	    GTA4L_EMMC_CMD_RESP_SHORT | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX, pattern, length, response);
	if (result != 0) {
		result->write_result = rc;
	}
	if (rc != GTA4L_EMMC_OK) {
		if ((rc != GTA4L_EMMC_ERR_DATA_TIMEOUT) ||
		    (host->bus_test_tolerate_write_timeout == 0u) ||
		    (host->fail_valid == 0u) ||
		    (host->fail_index != GTA4L_EMMC_CMD19) ||
		    ((host->fail_normal & GTA4L_EMMC_INT_CC) == 0u)) {
			if (result != 0) {
				result->test_result = rc;
			}
			return rc;
		}
		/* JEDEC 6.6.4 defines CMD19 as a host-to-device pattern, not a
		 * data write: the device ignores all but the first two bits per
		 * line and returns no CRC status token, so the controller waits
		 * for a completion that never arrives (TI SPRZ436H i836). Linux
		 * `mmc_bus_test()` discards every BUS_TEST_W error and lets
		 * BUS_TEST_R decide. Only a command that completed (CC latched)
		 * is eligible: clear the data circuit and continue to CMD14, and
		 * let the read-back decide. */
		rc = gta4l_emmc_clear_dat_inhibit(host);
		if (result != 0) {
			result->reset_result = rc;
		}
		if (rc != GTA4L_EMMC_OK) {
			if (result != 0) {
				result->test_result = rc;
			}
			return rc;
		}
	}
	if ((response[0] & GTA4L_EMMC_R1_ERROR_MASK) != 0) {
		host->card_status = response[0];
		if (result != 0) {
			result->test_result = GTA4L_EMMC_ERR_CARD_STATUS;
		}
		return GTA4L_EMMC_ERR_CARD_STATUS;
	}
	host->bus_test_data_crc_ok = (verified != 0) ? 1u : 0u;
	host->bus_test_ignored_crc = 0;
	rc = gta4l_emmc_read_data(host, GTA4L_EMMC_CMD14, 0,
	    GTA4L_EMMC_CMD_RESP_SHORT | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX, actual, length, response);
	host->bus_test_data_crc_ok = 0;
	if (rc != GTA4L_EMMC_OK) {
		if (result != 0) {
			result->test_result = rc;
		}
		return rc;
	}
	if ((response[0] & GTA4L_EMMC_R1_ERROR_MASK) != 0) {
		host->card_status = response[0];
		if (result != 0) {
			result->test_result = GTA4L_EMMC_ERR_CARD_STATUS;
		}
		return GTA4L_EMMC_ERR_CARD_STATUS;
	}

	if (bits == 1u) {
		/* Only the first two bits of the reversed DAT0 pattern are
		 * defined: the 0x80 transmit pattern returns 0b01 in bits
		 * 7:6. The lower bits are not part of the check. */
		test_word = (uint32_t)actual[0] &
		    GTA4L_EMMC_BUS_TEST_1_EXPECT_MASK;
		expected_word = GTA4L_EMMC_BUS_TEST_1_EXPECT;
	} else {
		for (index = 0; index < length / 4u; index++) {
			test_word |= (uint32_t)actual[index] <<
			    (index * 8u);
			expected_word |=
			    (uint32_t)(uint8_t)(~pattern[index]) <<
			    (index * 8u);
		}
	}
	if (result != 0) {
		result->test_word = test_word;
		result->test_expected = expected_word;
	}
	host->last_step = GTA4L_EMMC_STEP_BUS_TEST;
	if (bits == 1u) {
		if (test_word != expected_word) {
			if (result != 0) {
				result->test_result =
				    GTA4L_EMMC_ERR_BUS_TEST;
			}
			gta4l_emmc_log(host, GTA4L_EMMC_LOG_BUS,
			    0x80000000u | bits, test_word);
			return GTA4L_EMMC_ERR_BUS_TEST;
		}
	} else {
		for (index = 0; index < length / 4u; index++) {
			if ((uint8_t)(pattern[index] ^ actual[index]) !=
			    0xFFu) {
				if (result != 0) {
					result->test_result =
					    GTA4L_EMMC_ERR_BUS_TEST;
				}
				gta4l_emmc_log(host, GTA4L_EMMC_LOG_BUS,
				    0x80000000u | bits, test_word);
				return GTA4L_EMMC_ERR_BUS_TEST;
			}
		}
	}
	gta4l_emmc_log(host, GTA4L_EMMC_LOG_BUS, bits, test_word);
	return GTA4L_EMMC_OK;
}

int
gta4l_emmc_bus_test(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_result *result)
{
	return gta4l_emmc_bus_test_common(host, bits, result, 0);
}

int
gta4l_emmc_bus_test_verified(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_result *result)
{
	return gta4l_emmc_bus_test_common(host, bits, result, 1);
}

struct gta4l_emmc_drain_sample {
	int      result;
	uint32_t response;
	uint16_t error;
	uint16_t normal;
	uint32_t present;
};

/* One bounded drain command. `skip_dat_inhibit` is the SDHCI stop-command
 * exemption: CMD12 may be issued while a data transfer still holds
 * DAT_INHIBIT, because the command line itself is free. CMD13 keeps the
 * normal both-inhibit entry wait. The terminal tuple is captured before the
 * status clear; a card-status bit is left in the response and is not
 * promoted to a transport error. */
static int
gta4l_emmc_drain_command(struct gta4l_emmc_host *host, uint32_t index,
    uint32_t argument, uint16_t flags, int skip_dat_inhibit,
    struct gta4l_emmc_drain_sample *sample)
{
	uint16_t command;
	uint16_t normal = 0;
	uint16_t error = 0;
	uint32_t present = 0;
	uint32_t response = 0;
	uint32_t iterations;
	uint32_t step;
	int busy;
	int cc_seen;
	int tc_seen;
	int rc;

	command = (uint16_t)((index << 8) | flags);
	busy = ((flags & GTA4L_EMMC_CMD_RESP_TYPE_MASK) ==
	    GTA4L_EMMC_CMD_RESP_SHORT_BUSY);
	cc_seen = 0;
	tc_seen = 0;
	if (sample != 0) {
		sample->result = GTA4L_EMMC_OK;
		sample->response = 0;
		sample->error = 0;
		sample->normal = 0;
		sample->present = 0;
	}

	iterations = gta4l_emmc_poll_iterations(GTA4L_EMMC_CMD_TIMEOUT_US);
	for (step = 0; step < iterations; step++) {
		present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		if ((present & GTA4L_EMMC_PRESENT_CMD_INHIBIT) == 0) {
			if ((skip_dat_inhibit != 0) ||
			    ((present & GTA4L_EMMC_PRESENT_DAT_INHIBIT) == 0)) {
				break;
			}
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}
	if (step == iterations) {
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		gta4l_emmc_record_failure(host, index, argument, command, error,
		    normal, present);
		if (sample != 0) {
			sample->result = GTA4L_EMMC_ERR_INHIBIT;
			sample->error = error;
			sample->normal = normal;
			sample->present = present;
		}
		return GTA4L_EMMC_ERR_INHIBIT;
	}

	/* A stale data error may still be latched. It belongs to the write
	 * phase and is already in the first-failure record, so clear before
	 * the drain command and observe only this command below. */
	gta4l_emmc_clear_interrupts(host);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ARGUMENT, argument);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_COMMAND, command);
	gta4l_emmc_barrier(host);

	iterations = gta4l_emmc_poll_iterations(GTA4L_EMMC_CMD_TIMEOUT_US);
	for (step = 0; step < iterations; step++) {
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
			normal = gta4l_emmc_read16(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
			present = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_PRESENT_STATE);
			gta4l_emmc_record_failure(host, index, argument,
			    command, error, normal, present);
			gta4l_emmc_clear_interrupts(host);
			rc = gta4l_emmc_map_error(error, 0);
			if (sample != 0) {
				sample->result = rc;
				sample->response = response;
				sample->error = error;
				sample->normal = normal;
				sample->present = present;
			}
			return rc;
		}
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		if ((cc_seen == 0) && ((normal & GTA4L_EMMC_INT_CC) != 0)) {
			response = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE);
			cc_seen = 1;
		}
		if ((normal & GTA4L_EMMC_INT_TC) != 0) {
			tc_seen = 1;
		}
		if ((cc_seen != 0) && ((busy == 0) || (tc_seen != 0))) {
			break;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}

	error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ERROR_INT_STATUS);
	normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
	present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_PRESENT_STATE);
	if ((cc_seen == 0) || ((busy != 0) && (tc_seen == 0))) {
		gta4l_emmc_record_failure(host, index, argument, command, error,
		    normal, present);
		gta4l_emmc_clear_interrupts(host);
		if (sample != 0) {
			sample->result = GTA4L_EMMC_ERR_CMD_TIMEOUT;
			sample->response = response;
			sample->error = error;
			sample->normal = normal;
			sample->present = present;
		}
		return GTA4L_EMMC_ERR_CMD_TIMEOUT;
	}
	if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
		gta4l_emmc_record_failure(host, index, argument, command, error,
		    normal, present);
		gta4l_emmc_clear_interrupts(host);
		rc = gta4l_emmc_map_error(error, 0);
		if (sample != 0) {
			sample->result = rc;
			sample->response = response;
			sample->error = error;
			sample->normal = normal;
			sample->present = present;
		}
		return rc;
	}
	gta4l_emmc_clear_interrupts(host);
	if (sample != 0) {
		sample->result = GTA4L_EMMC_OK;
		sample->response = response;
		sample->error = error;
		sample->normal = normal;
		sample->present = present;
	}
	return GTA4L_EMMC_OK;
}

int
gta4l_emmc_post_write_drain(struct gta4l_emmc_host *host,
    struct gta4l_emmc_post_write_drain_result *result)
{
	struct gta4l_emmc_drain_sample stop;
	struct gta4l_emmc_drain_sample status;
	const struct gta4l_emmc_post_write_drain_result zero = { 0 };
	int rc;

	if ((host == 0) || (host->initialized == 0)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	if (result != 0) {
		*result = zero;
		result->status_result = GTA4L_EMMC_ERR_UNSUPPORTED;
	}

	rc = gta4l_emmc_drain_command(host, GTA4L_EMMC_CMD12, 0,
	    GTA4L_EMMC_CMD_RESP_SHORT_BUSY | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX, 1, &stop);
	if (result != 0) {
		result->stop_result = stop.result;
		result->stop_response = stop.response;
		result->stop_error = stop.error;
		result->stop_normal = stop.normal;
		result->stop_present = stop.present;
	}
	if (rc != GTA4L_EMMC_OK) {
		return rc;
	}

	if (result != 0) {
		result->status_attempted = 1;
	}
	rc = gta4l_emmc_drain_command(host, GTA4L_EMMC_CMD13,
	    host->rca << 16, GTA4L_EMMC_CMD_RESP_SHORT | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX, 0, &status);
	if (result != 0) {
		result->status_result = status.result;
		result->status_response = status.response;
		result->status_error = status.error;
		result->status_normal = status.normal;
		result->status_present = status.present;
	}
	if (status.result == GTA4L_EMMC_OK) {
		host->card_status = status.response;
	}
	return rc;
}

/* CMD19 write, bounded data-circuit reset and CMD14 read. This is the
 * rung-1 recovery form of the bus test: the write phase may end in the
 * recorded CC + BufferWriteReady timeout with no TC and no error bit, so
 * the data circuit is reset before the read half is attempted. The reset
 * is local to this diagnostic helper; no other timeout path changes. When
 * `drain` is supplied, the rung-2 CMD12/CMD13 drain runs after the
 * post-reset sample and before CMD14; its verdict is the caller's. */
static int
gta4l_emmc_bus_test_reset_common(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_reset_result *result,
    struct gta4l_emmc_post_write_drain_result *drain)
{
	uint8_t pattern[GTA4L_EMMC_BUS_TEST_8_BYTES];
	uint8_t actual[GTA4L_EMMC_BUS_TEST_8_BYTES];
	uint32_t response[4];
	uint32_t length;
	uint32_t code;
	uint32_t control;
	uint32_t present;
	uint32_t index;
	uint32_t iterations;
	uint32_t test_word = 0;
	uint32_t expected_word = 0;
	uint8_t readback;
	const struct gta4l_emmc_bus_reset_result zero = { 0 };
	int rc;

	if ((host == 0) || (host->initialized == 0)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	code = gta4l_emmc_bus_width_code(bits);
	if (code == 0xFFFFFFFFu) {
		return GTA4L_EMMC_ERR_ARG;
	}
	control = gta4l_emmc_read8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_HOST_CONTROL);
	if (host->bus_width != (uint8_t)code) {
		return GTA4L_EMMC_ERR_BUS_WIDTH;
	}
	if (bits == 1u) {
		if ((control & (GTA4L_EMMC_HOST_CONTROL_4BIT |
		    GTA4L_EMMC_HOST_CONTROL_8BIT)) != 0u) {
			return GTA4L_EMMC_ERR_BUS_WIDTH;
		}
	} else if (bits == 4u) {
		if ((control & (GTA4L_EMMC_HOST_CONTROL_4BIT |
		    GTA4L_EMMC_HOST_CONTROL_8BIT)) !=
		    GTA4L_EMMC_HOST_CONTROL_4BIT) {
			return GTA4L_EMMC_ERR_BUS_WIDTH;
		}
	} else if ((control & (GTA4L_EMMC_HOST_CONTROL_4BIT |
	    GTA4L_EMMC_HOST_CONTROL_8BIT)) != GTA4L_EMMC_HOST_CONTROL_8BIT) {
		return GTA4L_EMMC_ERR_BUS_WIDTH;
	}
	if (result != 0) {
		*result = zero;
		result->requested = bits;
		result->host_control = control;
	}

	length = (bits == 8u) ? GTA4L_EMMC_BUS_TEST_8_BYTES :
	    (bits == 1u) ? GTA4L_EMMC_BUS_TEST_1_BYTES :
	    GTA4L_EMMC_BUS_TEST_4_BYTES;
	for (index = 0; index < GTA4L_EMMC_BUS_TEST_8_BYTES; index++) {
		pattern[index] = 0;
		actual[index] = 0;
	}
	if (bits == 1u) {
		pattern[0] = GTA4L_EMMC_BUS_TEST_PATTERN_1;
	} else if (bits == 4u) {
		pattern[0] = GTA4L_EMMC_BUS_TEST_PATTERN_4;
	} else {
		pattern[0] = GTA4L_EMMC_BUS_TEST_PATTERN_8_0;
		pattern[1] = GTA4L_EMMC_BUS_TEST_PATTERN_8_1;
	}

	rc = gta4l_emmc_write_data(host, GTA4L_EMMC_CMD19, 0,
	    GTA4L_EMMC_CMD_RESP_SHORT | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX, pattern, length, response);
	if (result != 0) {
		result->write_result = rc;
		/* The terminal sample belongs to the first failure, which the
		 * probe's stage-A gate makes this CMD19 write. Do not copy an
		 * older tuple if a previous failure is still latched. */
		if ((rc != GTA4L_EMMC_OK) && (host->fail_valid != 0) &&
		    (host->fail_index == GTA4L_EMMC_CMD19)) {
			result->write_error = host->fail_error;
			result->write_normal = host->fail_normal;
			result->write_present = host->fail_present;
		}
	}

	/* One bounded data-circuit reset. The bit is self-clearing; the poll
	 * records how many status reads it took and leaves the last value for
	 * the report. */
	if (result != 0) {
		result->reset_written = GTA4L_EMMC_RESET_DATA;
	}
	gta4l_emmc_write8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_SOFTWARE_RESET, GTA4L_EMMC_RESET_DATA);
	gta4l_emmc_barrier(host);
	iterations = gta4l_emmc_poll_iterations(GTA4L_EMMC_RESET_TIMEOUT_US);
	readback = gta4l_emmc_read8(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_SOFTWARE_RESET);
	for (index = 1; (readback != 0u) && (index < iterations); index++) {
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
		readback = gta4l_emmc_read8(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_SOFTWARE_RESET);
	}
	if (result != 0) {
		result->reset_readback = readback;
		result->reset_iterations = index;
		result->reset_result = (readback == 0u) ? GTA4L_EMMC_OK :
		    GTA4L_EMMC_ERR_RESET;
	}
	if (readback != 0u) {
		return GTA4L_EMMC_ERR_RESET;
	}

	present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_PRESENT_STATE);
	if (result != 0) {
		result->post_present = present;
		result->dat_inhibit = present & GTA4L_EMMC_PRESENT_DAT_INHIBIT;
	}
	if ((present & GTA4L_EMMC_PRESENT_DAT_INHIBIT) != 0u) {
		if (result != 0) {
			result->test_result = GTA4L_EMMC_ERR_INHIBIT;
		}
		return GTA4L_EMMC_ERR_INHIBIT;
	}

	if (drain != 0) {
		/* Record-only here: the rung-2 caller owns the drain verdict,
		 * and CMD14 still runs so the report carries both samples. */
		(void)gta4l_emmc_post_write_drain(host, drain);
	}

	host->bus_test_data_crc_ok = 1;
	host->bus_test_ignored_crc = 0;
	rc = gta4l_emmc_read_data(host, GTA4L_EMMC_CMD14, 0,
	    GTA4L_EMMC_CMD_RESP_SHORT | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX, actual, length, response);
	host->bus_test_data_crc_ok = 0;
	if (rc != GTA4L_EMMC_OK) {
		if (result != 0) {
			result->test_result = rc;
		}
		return rc;
	}
	if ((response[0] & GTA4L_EMMC_R1_ERROR_MASK) != 0) {
		host->card_status = response[0];
		if (result != 0) {
			result->test_result = GTA4L_EMMC_ERR_CARD_STATUS;
		}
		return GTA4L_EMMC_ERR_CARD_STATUS;
	}

	if (bits == 1u) {
		test_word = (uint32_t)actual[0] &
		    GTA4L_EMMC_BUS_TEST_1_EXPECT_MASK;
		expected_word = GTA4L_EMMC_BUS_TEST_1_EXPECT;
	} else {
		for (index = 0; index < length / 4u; index++) {
			test_word |= (uint32_t)actual[index] <<
			    (index * 8u);
			expected_word |=
			    (uint32_t)(uint8_t)(~pattern[index]) <<
			    (index * 8u);
		}
	}
	if (result != 0) {
		result->test_word = test_word;
		result->test_expected = expected_word;
	}
	host->last_step = GTA4L_EMMC_STEP_BUS_TEST;
	/* The reset path applies the same JEDEC 1-bit two-bit rule. */
	if (bits == 1u) {
		if (test_word != expected_word) {
			if (result != 0) {
				result->test_result = GTA4L_EMMC_ERR_BUS_TEST;
			}
			gta4l_emmc_log(host, GTA4L_EMMC_LOG_BUS,
			    0x80000000u | bits, test_word);
			return GTA4L_EMMC_ERR_BUS_TEST;
		}
	} else {
		for (index = 0; index < length / 4u; index++) {
			if (((uint32_t)pattern[index] ^
			    (uint32_t)actual[index]) != 0xFFu) {
				if (result != 0) {
					result->test_result =
					    GTA4L_EMMC_ERR_BUS_TEST;
				}
				gta4l_emmc_log(host, GTA4L_EMMC_LOG_BUS,
				    0x80000000u | bits, test_word);
				return GTA4L_EMMC_ERR_BUS_TEST;
			}
		}
	}
	gta4l_emmc_log(host, GTA4L_EMMC_LOG_BUS, bits, test_word);
	return GTA4L_EMMC_OK;
}

int
gta4l_emmc_bus_test_reset(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_reset_result *result)
{
	return gta4l_emmc_bus_test_reset_common(host, bits, result, 0);
}

int
gta4l_emmc_bus_test_reset_drain(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_reset_result *result,
    struct gta4l_emmc_post_write_drain_result *drain)
{
	return gta4l_emmc_bus_test_reset_common(host, bits, result, drain);
}

int
gta4l_emmc_set_hs_timing(struct gta4l_emmc_host *host, uint32_t timing,
    struct gta4l_emmc_hs_result *result)
{
	uint8_t ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t response[4];
	uint32_t argument;
	uint32_t readback;
	uint32_t rcg_expected;
	uint32_t rcg_actual;
	uint32_t clock = 0;
	uint32_t sum = 0;
	uint32_t index;
	const struct gta4l_emmc_hs_result zero = { 0 };
	int rc;

	if ((host == 0) || (host->initialized == 0) || (timing > 1u)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	if (result != 0) {
		*result = zero;
		result->timing = timing;
		result->switch_result = GTA4L_EMMC_OK;
		result->clock_result = GTA4L_EMMC_OK;
	}
	if (result != 0) {
		result->rcg_before = gta4l_emmc_read32(host,
		    GTA4L_EMMC_SPACE_GCC, GTA4L_EMMC_GCC_SDCC1_APPS_CFG);
	}

	/* Leaving HS: drop the clock to 400 kHz before the card is asked to
	 * return to legacy timing, because legacy timing is not valid at the
	 * HS rate. Entering HS keeps 400 kHz until the byte-185 readback
	 * confirms the switch, matching Linux mmc_select_timing(). */
	if (timing == 0u) {
		rc = gta4l_emmc_switch_clock(host, GTA4L_EMMC_RCG_400K_CFG,
		    GTA4L_EMMC_RCG_400K_M, GTA4L_EMMC_RCG_400K_N,
		    GTA4L_EMMC_RCG_400K_D, &clock);
		if (rc != GTA4L_EMMC_OK) {
			if (result != 0) {
				result->clock_result = rc;
				result->clock = clock;
			}
			return rc;
		}
		if (result != 0) {
			result->clock = clock;
			result->rcg_after = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_GCC,
			    GTA4L_EMMC_GCC_SDCC1_APPS_CFG);
		}
		host->last_step = GTA4L_EMMC_STEP_HS_CLOCK;
	}

	rc = gta4l_emmc_read_ext_csd(host, ext_csd);
	if (rc != GTA4L_EMMC_OK) {
		if (result != 0) {
			result->switch_result = rc;
		}
		return rc;
	}
	if (result != 0) {
		result->hs_before =
		    ext_csd[GTA4L_EMMC_EXT_CSD_HS_TIMING_OFF];
	}

	argument = GTA4L_EMMC_SWITCH_ACCESS_WRITE |
	    (GTA4L_EMMC_EXT_CSD_HS_TIMING_OFF << 16) | (timing << 8) |
	    GTA4L_EMMC_SWITCH_CMD_SET_NORMAL;
	rc = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD6, argument,
	    GTA4L_EMMC_CMD_RESP_SHORT | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX, GTA4L_EMMC_CMD_TIMEOUT_US, response);
	if (rc != GTA4L_EMMC_OK) {
		if (result != 0) {
			result->switch_result = rc;
		}
		return rc;
	}
	if (result != 0) {
		result->switch_status = response[0];
	}
	if ((response[0] & GTA4L_EMMC_R1_ERROR_MASK) != 0) {
		host->card_status = response[0];
		if (result != 0) {
			result->switch_result = GTA4L_EMMC_ERR_CARD_STATUS;
		}
		return GTA4L_EMMC_ERR_CARD_STATUS;
	}
	rc = gta4l_emmc_wait_tran(host);
	if (rc != GTA4L_EMMC_OK) {
		if (result != 0) {
			result->switch_result = rc;
		}
		return rc;
	}

	rc = gta4l_emmc_read_ext_csd(host, ext_csd);
	if (rc != GTA4L_EMMC_OK) {
		if (result != 0) {
			result->switch_result = rc;
		}
		return rc;
	}
	readback = ext_csd[GTA4L_EMMC_EXT_CSD_HS_TIMING_OFF];
	for (index = 0; index < GTA4L_EMMC_EXT_CSD_BYTES; index++) {
		sum += ext_csd[index];
	}
	if (result != 0) {
		result->hs_after = readback;
		result->readback_sum = sum;
	}
	if (readback != timing) {
		if (result != 0) {
			result->switch_result = GTA4L_EMMC_ERR_TIMING;
		}
		return GTA4L_EMMC_ERR_TIMING;
	}
	host->last_step = GTA4L_EMMC_STEP_HS_TIMING;

	if (timing != 0u) {
		rc = gta4l_emmc_switch_clock(host, GTA4L_EMMC_RCG_50M_CFG,
		    GTA4L_EMMC_RCG_50M_M, GTA4L_EMMC_RCG_50M_N,
		    GTA4L_EMMC_RCG_50M_D, &clock);
		if (rc != GTA4L_EMMC_OK) {
			if (result != 0) {
				result->clock_result = rc;
				result->clock = clock;
			}
			return rc;
		}
		if (result != 0) {
			result->clock = clock;
		}
		host->last_step = GTA4L_EMMC_STEP_HS_CLOCK;
	}

	if (result != 0) {
		rcg_actual = result->rcg_after;
	} else {
		rcg_actual = 0u;
	}
	if (rcg_actual == 0u) {
		rcg_actual = gta4l_emmc_read32(host,
		    GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_CFG);
		if (result != 0) {
			result->rcg_after = rcg_actual;
		}
	}
	rcg_expected = (timing != 0u) ? GTA4L_EMMC_RCG_50M_CFG :
	    GTA4L_EMMC_RCG_400K_CFG;
	if ((rcg_actual & (GTA4L_EMMC_RCG_SRC_SEL_MASK |
	    GTA4L_EMMC_RCG_SRC_DIV_MASK |
	    GTA4L_EMMC_RCG_MODE_MASK)) !=
	    (rcg_expected & (GTA4L_EMMC_RCG_SRC_SEL_MASK |
	    GTA4L_EMMC_RCG_SRC_DIV_MASK |
	    GTA4L_EMMC_RCG_MODE_MASK))) {
		if (result != 0) {
			result->clock_result = GTA4L_EMMC_ERR_CLOCK;
		}
		return GTA4L_EMMC_ERR_CLOCK;
	}
	gta4l_emmc_log(host, GTA4L_EMMC_LOG_CLOCK, timing,
	    gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL));
	return GTA4L_EMMC_OK;
}

static void
gta4l_emmc_parse_ext_csd(struct gta4l_emmc_host *host, const uint8_t *ext)
{
	uint32_t count;

	host->ext_csd_sum = gta4l_emmc_ext_csd_sum(ext);
	host->ext_csd_rev = ext[GTA4L_EMMC_EXT_CSD_REV_OFF];
	host->bus_width = (uint8_t)(ext[183] & 0x0Fu);
	host->partition_config = ext[GTA4L_EMMC_EXT_CSD_PARTITION_CONFIG_OFF];
	host->data_sector_size_code =
	    ext[GTA4L_EMMC_EXT_CSD_DATA_SECTOR_SIZE_OFF];
	host->data_sector_size_4k =
	    (host->data_sector_size_code == 1u) ? 1u : 0u;

	count = (uint32_t)ext[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF] |
	    ((uint32_t)ext[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF + 1] << 8) |
	    ((uint32_t)ext[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF + 2] << 16) |
	    ((uint32_t)ext[GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF + 3] << 24);
	host->capacity_blocks = count;
}

int
gta4l_emmc_init(struct gta4l_emmc_host *host)
{
	uint8_t ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t response[4];
	int result;

	if (host == 0) {
		return GTA4L_EMMC_ERR_ARG;
	}
	host->cmd3_retry_engaged = 0;
	host->cmd3_retry_ok = 0;
	host->cmd3_attempt_count = 0;
	host->cmd3_attempt[0].number = 0;
	host->cmd3_attempt[1].number = 0;
	result = gta4l_emmc_controller_init(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}

	/* Linux mmc_go_idle(): chip-select high, 1 ms, CMD0, 1 ms, then
	 * chip-select release and 1 ms. eMMC has no chip-select line, so the
	 * pre-delay and the combined post-delay preserve the reference timing. */
	gta4l_emmc_delay(host, GTA4L_EMMC_CMD0_DELAY_US);
	result = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD0, 0,
	    GTA4L_EMMC_CMD_RESP_NONE, GTA4L_EMMC_CMD_TIMEOUT_US, 0);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	gta4l_emmc_delay(host, GTA4L_EMMC_CMD0_POST_DELAY_US);
	host->last_step = GTA4L_EMMC_STEP_CMD0;

	result = gta4l_emmc_card_ready(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	host->last_step = GTA4L_EMMC_STEP_CMD1;

	result = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD2, 0,
	    GTA4L_EMMC_CMD_RESP_LONG | GTA4L_EMMC_CMD_CRC,
	    GTA4L_EMMC_CMD_TIMEOUT_US, response);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	if (gta4l_emmc_validate_response(response) != GTA4L_EMMC_OK) {
		return GTA4L_EMMC_ERR_RESPONSE;
	}
	host->cid[0] = response[0];
	host->cid[1] = response[1];
	host->cid[2] = response[2];
	host->cid[3] = response[3];
	host->last_step = GTA4L_EMMC_STEP_CMD2;

	result = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD3,
	    GTA4L_EMMC_DEFAULT_RCA << 16, GTA4L_EMMC_CMD_RESP_SHORT |
	    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX,
	    GTA4L_EMMC_CMD_TIMEOUT_US, response);
	result = gta4l_emmc_cmd3_result(result, response);
	if (host->cmd3_retry_enabled != 0) {
		host->cmd3_attempt_count = 1;
		gta4l_emmc_cmd3_snapshot(host, &host->cmd3_attempt[0], 1u,
		    result, response);
		if (result == GTA4L_EMMC_ERR_CMD_TIMEOUT) {
			/* H9: one designed duplicate CMD3 with the identical
			 * argument and timing path. The attempt-1 record was
			 * copied above, so clearing the live failure state
			 * cannot lose it. Never a third attempt. */
			host->cmd3_retry_engaged = 1;
			gta4l_emmc_clear_failure(host);
			result = gta4l_emmc_send_command(host,
			    GTA4L_EMMC_CMD3,
			    GTA4L_EMMC_DEFAULT_RCA << 16,
			    GTA4L_EMMC_CMD_RESP_SHORT |
			    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX,
			    GTA4L_EMMC_CMD_TIMEOUT_US, response);
			result = gta4l_emmc_cmd3_result(result, response);
			host->cmd3_attempt_count = 2;
			gta4l_emmc_cmd3_snapshot(host, &host->cmd3_attempt[1],
			    2u, result, response);
			if (result == GTA4L_EMMC_OK) {
				host->cmd3_retry_ok = 1;
			}
		}
	}
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	/* eMMC CMD3 is SET_RELATIVE_ADDR: the host assigns the RCA in the
	 * argument and the response is R1 status, not a returned RCA. */
	host->rca = GTA4L_EMMC_DEFAULT_RCA;
	host->last_step = GTA4L_EMMC_STEP_CMD3;

	result = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD9,
	    host->rca << 16, GTA4L_EMMC_CMD_RESP_LONG | GTA4L_EMMC_CMD_CRC,
	    GTA4L_EMMC_CMD_TIMEOUT_US, response);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	if (gta4l_emmc_validate_response(response) != GTA4L_EMMC_OK) {
		return GTA4L_EMMC_ERR_RESPONSE;
	}
	host->csd[0] = response[0];
	host->csd[1] = response[1];
	host->csd[2] = response[2];
	host->csd[3] = response[3];
	host->last_step = GTA4L_EMMC_STEP_CMD9;

	result = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD7,
	    host->rca << 16, GTA4L_EMMC_CMD_RESP_SHORT_BUSY |
	    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX,
	    GTA4L_EMMC_CMD_TIMEOUT_US, response);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	if ((response[0] & GTA4L_EMMC_R1_ERROR_MASK) != 0) {
		return GTA4L_EMMC_ERR_CARD_STATUS;
	}
	host->last_step = GTA4L_EMMC_STEP_CMD7;
	result = gta4l_emmc_wait_tran(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	host->last_step = GTA4L_EMMC_STEP_TRAN;

	if (host->identify_only != 0) {
		/* H9 stops with the card in TRAN, before any EXT_CSD access. */
		return GTA4L_EMMC_OK;
	}

	result = gta4l_emmc_read_ext_csd(host, ext_csd);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	gta4l_emmc_parse_ext_csd(host, ext_csd);
	if (host->data_sector_size_code > 1u) {
		/* Only 0 (512-byte) and 1 (4 KiB) are defined. A reserved
		 * encoding must not be read as 512-byte mode. */
		gta4l_emmc_log(host, GTA4L_EMMC_LOG_ERROR, 5,
		    host->data_sector_size_code);
		return GTA4L_EMMC_ERR_UNSUPPORTED;
	}
	if (host->data_sector_size_4k != 0) {
		/* The read path is fixed at 512-byte sectors. */
		gta4l_emmc_log(host, GTA4L_EMMC_LOG_ERROR, 5,
		    host->data_sector_size_4k);
		return GTA4L_EMMC_ERR_UNSUPPORTED;
	}
	if (host->capacity_blocks <= GTA4L_EMMC_SECTOR_ADDRESS_MIN_BLOCKS) {
		gta4l_emmc_log(host, GTA4L_EMMC_LOG_ERROR, 3,
		    host->capacity_blocks);
		return GTA4L_EMMC_ERR_UNSUPPORTED;
	}
	if ((host->partition_config &
	    GTA4L_EMMC_EXT_CSD_PARTITION_ACCESS_MASK) != 0) {
		gta4l_emmc_log(host, GTA4L_EMMC_LOG_ERROR, 4,
		    host->partition_config);
		return GTA4L_EMMC_ERR_UNSUPPORTED;
	}
	host->last_step = GTA4L_EMMC_STEP_EXT_CSD;
	host->initialized = 1;
	host->last_step = GTA4L_EMMC_STEP_READY;
	gta4l_emmc_log(host, GTA4L_EMMC_LOG_CARD, host->capacity_blocks,
	    ((uint32_t)host->ext_csd_rev << 8) | host->bus_width);
	return GTA4L_EMMC_OK;
}

int
gta4l_emmc_read_block(struct gta4l_emmc_host *host, uint64_t block,
    void *buffer)
{
	uint32_t response = 0;
	int result;

	if ((host == 0) || (buffer == 0) || (host->initialized == 0)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	if ((host->capacity_blocks == 0) ||
	    (block >= (uint64_t)host->capacity_blocks) ||
	    (block > 0xFFFFFFFFu)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	result = gta4l_emmc_read_data(host, GTA4L_EMMC_CMD17,
	    (uint32_t)block, GTA4L_EMMC_CMD_RESP_SHORT | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX, buffer, GTA4L_EMMC_BLOCK_BYTES, &response);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	if ((response & GTA4L_EMMC_R1_ERROR_MASK) != 0) {
		gta4l_emmc_log(host, GTA4L_EMMC_LOG_ERROR, 2, response);
		return GTA4L_EMMC_ERR_CARD_STATUS;
	}
	gta4l_emmc_log(host, GTA4L_EMMC_LOG_READ, (uint32_t)block,
	    response);
	return GTA4L_EMMC_OK;
}

static void
gta4l_emmc_capture_stop_terminal(struct gta4l_emmc_host *host,
    uint16_t normal, uint16_t error, uint32_t present)
{
	host->multi_stop_normal = normal;
	host->multi_stop_error = error;
	host->multi_stop_present = present;
	host->multi_stop_command = gta4l_emmc_read16(host,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_COMMAND);
	host->multi_stop_argument = gta4l_emmc_read32(host,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_ARGUMENT);
	gta4l_emmc_read_raw_response(host, host->multi_stop_response);
	host->multi_stop_capture_valid = 1;
}

/* End an open-ended CMD18 transfer. The stop reuses the CMD7 R1b policy:
 * CC and TC are both required, the poll is bounded and the terminal state is
 * captured before the status clear. The data-inhibit bit is deliberately not
 * part of the entry wait because an aborted data transfer can retain it while
 * the command line is free; Linux sdhci likewise skips DATA_INHIBIT for the
 * stop command. */
static int
gta4l_emmc_send_stop(struct gta4l_emmc_host *host)
{
	uint16_t command;
	uint16_t normal = 0;
	uint16_t error = 0;
	uint32_t present = 0;
	uint32_t response = 0;
	uint32_t iterations;
	uint32_t step;
	int cc_seen = 0;
	int tc_seen = 0;
	int result;

	command = (uint16_t)((GTA4L_EMMC_CMD12 << 8) |
	    GTA4L_EMMC_CMD_RESP_SHORT_BUSY | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX);
	host->multi_stop_command = command;
	host->multi_stop_argument = 0;
	host->multi_stop_valid = 1;

	iterations = gta4l_emmc_poll_iterations(GTA4L_EMMC_CMD_TIMEOUT_US);
	for (step = 0; step < iterations; step++) {
		present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		if ((present & GTA4L_EMMC_PRESENT_CMD_INHIBIT) == 0) {
			break;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}
	if (step == iterations) {
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		gta4l_emmc_capture_stop_terminal(host, normal, error, present);
		gta4l_emmc_record_failure(host, GTA4L_EMMC_CMD12, 0, command,
		    error, normal, present);
		host->multi_stop_result = GTA4L_EMMC_ERR_INHIBIT;
		return GTA4L_EMMC_ERR_INHIBIT;
	}

	/* A data error may still be latched. It is already in the failure
	 * record, so clear the latches before the stop is submitted and only
	 * the CMD12 completion is observed below. */
	gta4l_emmc_clear_interrupts(host);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ARGUMENT, 0);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_COMMAND, command);
	gta4l_emmc_barrier(host);

	iterations = gta4l_emmc_poll_iterations(GTA4L_EMMC_CMD_TIMEOUT_US);
	for (step = 0; step < iterations; step++) {
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
			normal = gta4l_emmc_read16(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
			present = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_PRESENT_STATE);
			gta4l_emmc_capture_stop_terminal(host, normal, error,
			    present);
			gta4l_emmc_record_failure(host, GTA4L_EMMC_CMD12, 0,
			    command, error, normal, present);
			gta4l_emmc_clear_interrupts(host);
			result = gta4l_emmc_map_error(error, 0);
			host->multi_stop_result = result;
			return result;
		}
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		if ((cc_seen == 0) && ((normal & GTA4L_EMMC_INT_CC) != 0)) {
			cc_seen = 1;
			response = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE);
		}
		if ((normal & GTA4L_EMMC_INT_TC) != 0) {
			tc_seen = 1;
		}
		if ((cc_seen != 0) && (tc_seen != 0)) {
			break;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}
	if ((cc_seen == 0) || (tc_seen == 0)) {
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		gta4l_emmc_capture_stop_terminal(host, normal, error, present);
		gta4l_emmc_record_failure(host, GTA4L_EMMC_CMD12, 0, command,
		    error, normal, present);
		gta4l_emmc_clear_interrupts(host);
		host->multi_stop_result = GTA4L_EMMC_ERR_CMD_TIMEOUT;
		return GTA4L_EMMC_ERR_CMD_TIMEOUT;
	}

	error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ERROR_INT_STATUS);
	normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
	present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_PRESENT_STATE);
	gta4l_emmc_capture_stop_terminal(host, normal, error, present);
	if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
		gta4l_emmc_record_failure(host, GTA4L_EMMC_CMD12, 0, command,
		    error, normal, present);
		gta4l_emmc_clear_interrupts(host);
		result = gta4l_emmc_map_error(error, 0);
		host->multi_stop_result = result;
		return result;
	}
	if ((response & GTA4L_EMMC_R1_ERROR_MASK) != 0) {
		gta4l_emmc_record_failure(host, GTA4L_EMMC_CMD12, 0, command,
		    error, normal, present);
		gta4l_emmc_clear_interrupts(host);
		host->multi_stop_result = GTA4L_EMMC_ERR_CARD_STATUS;
		return GTA4L_EMMC_ERR_CARD_STATUS;
	}
	gta4l_emmc_clear_interrupts(host);
	host->multi_stop_result = GTA4L_EMMC_OK;
	return GTA4L_EMMC_OK;
}

int
gta4l_emmc_read_multi(struct gta4l_emmc_host *host, uint64_t start_lba,
    uint32_t count, void *buffer)
{
	uint8_t *bytes = (uint8_t *)buffer;
	uint16_t command;
	uint16_t normal = 0;
	uint16_t error = 0;
	uint32_t present;
	uint32_t iterations;
	uint32_t step;
	uint32_t drain_iterations;
	uint32_t drain_step;
	uint32_t done = 0;
	uint32_t response = 0;
	uint32_t byte;
	uint32_t word;
	int command_done = 0;
	int completed = 0;
	int result;
	int stop_result;

	if ((host == 0) || (buffer == 0) || (host->initialized == 0)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	if ((count == 0u) || (count > 0xFFFFu)) {
		return GTA4L_EMMC_ERR_ARG;
	}
	if ((host->capacity_blocks == 0u) ||
	    (start_lba >= (uint64_t)host->capacity_blocks) ||
	    (start_lba > 0xFFFFFFFFu) ||
	    ((uint64_t)count >
	    ((uint64_t)host->capacity_blocks - start_lba))) {
		return GTA4L_EMMC_ERR_ARG;
	}
	host->multi_block = 0;
	host->multi_stop_result = 0;
	host->multi_stop_valid = 0;
	host->multi_stop_capture_valid = 0;
	host->multi_drain_blocks = 0;
	host->multi_drain_iterations = 0;
	host->multi_drain_used = 0;
	host->multi_drain_resolved = 0;
	command = (uint16_t)((GTA4L_EMMC_CMD18 << 8) |
	    GTA4L_EMMC_CMD_RESP_SHORT | GTA4L_EMMC_CMD_CRC |
	    GTA4L_EMMC_CMD_INDEX | GTA4L_EMMC_CMD_DATA);
	if (gta4l_emmc_wait_inhibit(host) != GTA4L_EMMC_OK) {
		present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		gta4l_emmc_record_failure(host, GTA4L_EMMC_CMD18,
		    (uint32_t)start_lba, command, 0, 0, present);
		return GTA4L_EMMC_ERR_INHIBIT;
	}
	gta4l_emmc_clear_interrupts(host);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_BLOCK_SIZE, GTA4L_EMMC_BLOCK_BYTES);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_BLOCK_COUNT, (uint16_t)count);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ARGUMENT, (uint32_t)start_lba);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_TRANSFER_MODE,
	    GTA4L_EMMC_TM_READ | GTA4L_EMMC_TM_BLOCK_COUNT_EN |
	    GTA4L_EMMC_TM_MULTI);
	gta4l_emmc_barrier(host);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_COMMAND, command);
	gta4l_emmc_barrier(host);

	result = GTA4L_EMMC_OK;
	iterations = gta4l_emmc_poll_iterations(GTA4L_EMMC_DATA_TIMEOUT_US);
	for (step = 0; step < iterations; step++) {
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
			normal = gta4l_emmc_read16(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
			present = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_PRESENT_STATE);
			host->multi_block = (done < count) ? done : count - 1u;
			gta4l_emmc_record_failure(host, GTA4L_EMMC_CMD18,
			    (uint32_t)start_lba, command, error, normal,
			    present);
			result = gta4l_emmc_map_error(error, 1);
			break;
		}
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		if ((command_done == 0) &&
		    ((normal & GTA4L_EMMC_INT_CC) != 0)) {
			response = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_RESPONSE);
			command_done = 1;
		}
		if ((command_done != 0) && (done < count)) {
			/* BRR is latched until written back; the FIFO can be
			 * empty while it is still set. Live buffer readiness
			 * is the transfer condition; BRR is acknowledged so
			 * one sector is consumed exactly once. */
			present = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_PRESENT_STATE);
			if ((present &
			    GTA4L_EMMC_PRESENT_BUFFER_READ_READY) != 0u) {
				for (byte = 0;
				    byte < GTA4L_EMMC_BLOCK_BYTES;
				    byte += 4u) {
					word = gta4l_emmc_read32(host,
					    GTA4L_EMMC_SPACE_SDHCI,
					    GTA4L_EMMC_REG_BUFFER_DATA_PORT);
					bytes[(done *
					    GTA4L_EMMC_BLOCK_BYTES) + byte] =
					    (uint8_t)(word & 0xFFu);
					bytes[(done *
					    GTA4L_EMMC_BLOCK_BYTES) + byte +
					    1u] = (uint8_t)((word >> 8) &
					    0xFFu);
					bytes[(done *
					    GTA4L_EMMC_BLOCK_BYTES) + byte +
					    2u] = (uint8_t)((word >> 16) &
					    0xFFu);
					bytes[(done *
					    GTA4L_EMMC_BLOCK_BYTES) + byte +
					    3u] = (uint8_t)((word >> 24) &
					    0xFFu);
				}
				gta4l_emmc_log(host, GTA4L_EMMC_LOG_READ,
				    (uint32_t)(start_lba + done), response);
				gta4l_emmc_write16(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_NORMAL_INT_STATUS,
				    GTA4L_EMMC_INT_BRR);
				done++;
			}
		}
		if ((command_done != 0) && (done == count) &&
		    ((normal & GTA4L_EMMC_INT_TC) != 0)) {
			error = gta4l_emmc_read16(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_ERROR_INT_STATUS);
			if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
				normal = gta4l_emmc_read16(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
				present = gta4l_emmc_read32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_PRESENT_STATE);
				host->multi_block = count - 1u;
				gta4l_emmc_record_failure(host,
				    GTA4L_EMMC_CMD18, (uint32_t)start_lba,
				    command, error, normal, present);
				result = gta4l_emmc_map_error(error, 1);
				break;
			}
			host->multi_block = count - 1u;
			completed = 1;
			break;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}
	if ((completed == 0) && (result == GTA4L_EMMC_OK)) {
		/* The primary bound expired without an error or TC. Attempt 2
		 * ended here with the data path still active and BRR pending,
		 * so drain the remaining ready blocks and re-check TC in one
		 * bounded, separately-reported window. An error in this window
		 * is mapped exactly like an in-loop error and keeps the first
		 * failing result. */
		host->multi_drain_used = 1;
		drain_iterations =
		    gta4l_emmc_poll_iterations(GTA4L_EMMC_DATA_DRAIN_US);
		for (drain_step = 0; drain_step < drain_iterations;
		    drain_step++) {
			error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_ERROR_INT_STATUS);
			if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
				normal = gta4l_emmc_read16(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
				present = gta4l_emmc_read32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_PRESENT_STATE);
				host->multi_block = (done < count) ?
				    done : count - 1u;
				gta4l_emmc_record_failure(host,
				    GTA4L_EMMC_CMD18, (uint32_t)start_lba,
				    command, error, normal, present);
				result = gta4l_emmc_map_error(error, 1);
				break;
			}
			normal = gta4l_emmc_read16(host,
			    GTA4L_EMMC_SPACE_SDHCI,
			    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
			if ((command_done == 0) &&
			    ((normal & GTA4L_EMMC_INT_CC) != 0)) {
				response = gta4l_emmc_read32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_RESPONSE);
				command_done = 1;
			}
			if ((command_done != 0) && (done < count)) {
				present = gta4l_emmc_read32(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_PRESENT_STATE);
				if ((present &
				    GTA4L_EMMC_PRESENT_BUFFER_READ_READY) !=
				    0u) {
					for (byte = 0;
					    byte < GTA4L_EMMC_BLOCK_BYTES;
					    byte += 4u) {
						word = gta4l_emmc_read32(host,
						    GTA4L_EMMC_SPACE_SDHCI,
						    GTA4L_EMMC_REG_BUFFER_DATA_PORT);
						bytes[(done *
						    GTA4L_EMMC_BLOCK_BYTES) +
						    byte] = (uint8_t)(word &
						    0xFFu);
						bytes[(done *
						    GTA4L_EMMC_BLOCK_BYTES) +
						    byte + 1u] =
						    (uint8_t)((word >> 8) &
						    0xFFu);
						bytes[(done *
						    GTA4L_EMMC_BLOCK_BYTES) +
						    byte + 2u] =
						    (uint8_t)((word >> 16) &
						    0xFFu);
						bytes[(done *
						    GTA4L_EMMC_BLOCK_BYTES) +
						    byte + 3u] =
						    (uint8_t)((word >> 24) &
						    0xFFu);
					}
					gta4l_emmc_log(host,
					    GTA4L_EMMC_LOG_READ,
					    (uint32_t)(start_lba + done),
					    response);
					gta4l_emmc_write16(host,
					    GTA4L_EMMC_SPACE_SDHCI,
					    GTA4L_EMMC_REG_NORMAL_INT_STATUS,
					    GTA4L_EMMC_INT_BRR);
					done++;
					host->multi_drain_blocks++;
				}
			}
			if ((command_done != 0) && (done == count) &&
			    ((normal & GTA4L_EMMC_INT_TC) != 0)) {
				error = gta4l_emmc_read16(host,
				    GTA4L_EMMC_SPACE_SDHCI,
				    GTA4L_EMMC_REG_ERROR_INT_STATUS);
				if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
					normal = gta4l_emmc_read16(host,
					    GTA4L_EMMC_SPACE_SDHCI,
					    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
					present = gta4l_emmc_read32(host,
					    GTA4L_EMMC_SPACE_SDHCI,
					    GTA4L_EMMC_REG_PRESENT_STATE);
					host->multi_block = count - 1u;
					gta4l_emmc_record_failure(host,
					    GTA4L_EMMC_CMD18,
					    (uint32_t)start_lba, command,
					    error, normal, present);
					result =
					    gta4l_emmc_map_error(error, 1);
					break;
				}
				host->multi_block = count - 1u;
				completed = 1;
				break;
			}
			gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
		}
		host->multi_drain_iterations =
		    (drain_step < drain_iterations) ? (drain_step + 1u) :
		    drain_iterations;
		host->multi_drain_resolved = (completed != 0) ? 1 : 0;
	}
	if ((completed == 0) && (result == GTA4L_EMMC_OK)) {
		error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		present = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_PRESENT_STATE);
		host->multi_block = (done < count) ? done : count - 1u;
		gta4l_emmc_record_failure(host, GTA4L_EMMC_CMD18,
		    (uint32_t)start_lba, command, error, normal, present);
		result = GTA4L_EMMC_ERR_DATA_TIMEOUT;
	}
	if ((result == GTA4L_EMMC_OK) &&
	    ((response & GTA4L_EMMC_R1_ERROR_MASK) != 0)) {
		/* The R1 status is a card-level result; the open-ended
		 * transfer still has to be stopped below. */
		gta4l_emmc_log(host, GTA4L_EMMC_LOG_ERROR, 3, response);
		result = GTA4L_EMMC_ERR_CARD_STATUS;
	}

	stop_result = gta4l_emmc_send_stop(host);
	if ((result == GTA4L_EMMC_OK) && (stop_result != GTA4L_EMMC_OK)) {
		result = stop_result;
	}
	return result;
}
