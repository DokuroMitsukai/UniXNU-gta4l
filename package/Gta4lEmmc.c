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

/* SDHCI stores a 128-bit R2 response in RESPONSE3..RESPONSE0, least
 * significant word first, with the CRC byte in the low byte of RESPONSE0.
 * Normalize to the MMC big-endian word order: response[0] is bits 127:96,
 * matching Linux's sdhci_read_rsp_136() shift. */
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
	    GTA4L_EMMC_INT_CC | GTA4L_EMMC_INT_TC | GTA4L_EMMC_INT_BRR);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ERROR_INT_ENABLE, GTA4L_EMMC_INT_ERR_MASK);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_NORMAL_INT_SIGNAL, 0);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ERROR_INT_SIGNAL, 0);
	gta4l_emmc_barrier(host);
}

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
	uint32_t iterations;
	uint32_t step;
	int result;

	result = gta4l_emmc_wait_inhibit(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	gta4l_emmc_clear_interrupts(host);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ARGUMENT, argument);
	gta4l_emmc_barrier(host);
	command = (uint16_t)((index << 8) | flags);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_COMMAND, command);
	gta4l_emmc_barrier(host);

	iterations = gta4l_emmc_poll_iterations(timeout_us);
	for (step = 0; step < iterations; step++) {
		uint16_t error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);

		if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
			gta4l_emmc_clear_interrupts(host);
			return gta4l_emmc_map_error(error, 0);
		}
		normal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_NORMAL_INT_STATUS);
		if ((normal & GTA4L_EMMC_INT_CC) != 0) {
			break;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}
	if ((normal & GTA4L_EMMC_INT_CC) == 0) {
		gta4l_emmc_clear_interrupts(host);
		return GTA4L_EMMC_ERR_CMD_TIMEOUT;
	}

	if (response != 0) {
		if ((flags & GTA4L_EMMC_CMD_RESP_TYPE_MASK) ==
		    GTA4L_EMMC_CMD_RESP_LONG) {
			gta4l_emmc_read_long_response(host, response);
		} else {
			response[0] = gta4l_emmc_read32(host,
			    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_RESPONSE);
		}
	}

	gta4l_emmc_clear_interrupts(host);
	return GTA4L_EMMC_OK;
}

static int
gta4l_emmc_read_data(struct gta4l_emmc_host *host, uint32_t index,
    uint32_t argument, uint16_t flags, void *buffer, uint32_t length,
    uint32_t *response)
{
	uint16_t command;
	uint16_t normal;
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
	if (gta4l_emmc_wait_inhibit(host) != GTA4L_EMMC_OK) {
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
	command = (uint16_t)((index << 8) | flags | GTA4L_EMMC_CMD_DATA);
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_COMMAND, command);
	gta4l_emmc_barrier(host);

	iterations = gta4l_emmc_poll_iterations(GTA4L_EMMC_DATA_TIMEOUT_US);
	for (step = 0; step < iterations; step++) {
		uint16_t error = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_ERROR_INT_STATUS);

		if ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {
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
			gta4l_emmc_clear_interrupts(host);
			return GTA4L_EMMC_OK;
		}
		gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);
	}

	gta4l_emmc_clear_interrupts(host);
	(void)normal;
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
gta4l_emmc_configure_clock(struct gta4l_emmc_host *host)
{
	uint32_t command;
	uint32_t config;
	int result;

	/* Stop the card clock before touching the RCG. */
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL, 0);
	gta4l_emmc_barrier(host);

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

	config = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_CFG);
	config &= ~(GTA4L_EMMC_RCG_SRC_SEL_MASK |
	    GTA4L_EMMC_RCG_SRC_DIV_MASK | GTA4L_EMMC_RCG_MODE_MASK);
	config |= GTA4L_EMMC_RCG_400K_CFG;
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_M, GTA4L_EMMC_RCG_400K_M);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_N, GTA4L_EMMC_RCG_400K_N);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_D, GTA4L_EMMC_RCG_400K_D);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_CFG, config);
	gta4l_emmc_barrier(host);

	command = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_CMD);
	gta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_CMD,
	    command | GTA4L_EMMC_RCG_CMD_UPDATE);
	gta4l_emmc_barrier(host);
	result = gta4l_emmc_wait32(host, GTA4L_EMMC_SPACE_GCC,
	    GTA4L_EMMC_GCC_SDCC1_APPS_CMD, GTA4L_EMMC_RCG_CMD_UPDATE, 0,
	    GTA4L_EMMC_RCG_TIMEOUT_US);
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
gta4l_emmc_power_on(struct gta4l_emmc_host *host, uint32_t io_request)
{
	uint32_t status;
	uint32_t clear;
	uint32_t ack;
	uint32_t iterations;
	uint32_t index;
	uint32_t attempt;
	int bus_on_seen = 0;
	int io_done;
	int settled = 0;
	int cleared;

	/* The HOST_CONTROL2 write requested this IO level. A zero value means
	 * the signalling bit did not change, so the controller raises no IO
	 * request and the level is already the requested one. */
	io_done = (io_request == 0) ? 1 : 0;

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
			if ((bus_on_seen != 0) && (io_done != 0)) {
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
		if ((status & GTA4L_EMMC_PWRCTL_BUS_ON) != 0) {
			/* Linux treats BUS_ON as an implicit IO-high
			 * transition and acknowledges both completions. */
			ack |= GTA4L_EMMC_PWRCTL_ACK_BUS |
			    GTA4L_EMMC_PWRCTL_ACK_IO;
			bus_on_seen = 1;
			if (io_request == GTA4L_EMMC_PWRCTL_IO_HIGH) {
				io_done = 1;
			}
		}
		if ((status & (GTA4L_EMMC_PWRCTL_IO_HIGH |
		    GTA4L_EMMC_PWRCTL_IO_LOW)) != 0) {
			ack |= GTA4L_EMMC_PWRCTL_ACK_IO;
			io_done = 1;
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
		gta4l_emmc_log(host, GTA4L_EMMC_LOG_POWER, io_request, io_done);
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

static int
gta4l_emmc_enable_card_clock(struct gta4l_emmc_host *host)
{
	uint16_t clock = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL);

	clock |= GTA4L_EMMC_CLOCK_INT_EN | GTA4L_EMMC_CLOCK_CARD_EN;
	gta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL, clock);
	gta4l_emmc_barrier(host);
	gta4l_emmc_delay(host, 1000);
	return GTA4L_EMMC_OK;
}

int
gta4l_emmc_controller_init(struct gta4l_emmc_host *host)
{
	uint32_t version;
	uint32_t vendor;
	uint32_t io_request;
	uint16_t control2;
	int result;

	if ((host == 0) || (host->ops == 0)) {
		return GTA4L_EMMC_ERR_ARG;
	}

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

	version = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_MCI_VERSION);
	if ((version >> 28) != 1u) {
		return GTA4L_EMMC_ERR_VERSION;
	}

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
	io_request = 0;
	if (((control2 & GTA4L_EMMC_HOST_CONTROL2_VDD_180) != 0) !=
	    (host->fixed_1v8 != 0)) {
		io_request = (host->fixed_1v8 != 0) ?
		    GTA4L_EMMC_PWRCTL_IO_LOW : GTA4L_EMMC_PWRCTL_IO_HIGH;
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

	gta4l_emmc_clear_interrupts(host);
	gta4l_emmc_enable_status(host);

	result = gta4l_emmc_configure_clock(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	result = gta4l_emmc_power_on(host, io_request);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	gta4l_emmc_configure_pads(host);

	/* Let power and pads settle before the card sees a clock. */
	gta4l_emmc_delay(host, GTA4L_EMMC_POWER_SETTLE_US);
	result = gta4l_emmc_enable_card_clock(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}

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

	for (index = 0; index < iterations; index++) {
		result = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD1,
		    GTA4L_EMMC_OCR_VALUE, GTA4L_EMMC_CMD_RESP_SHORT,
		    GTA4L_EMMC_CMD1_ATTEMPT_US, response);
		if (result == GTA4L_EMMC_OK) {
			if ((response[0] & 0x80000000u) != 0) {
				host->ocr = response[0];
				return GTA4L_EMMC_OK;
			}
		} else if (result != GTA4L_EMMC_ERR_CMD_TIMEOUT) {
			return result;
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

static void
gta4l_emmc_parse_ext_csd(struct gta4l_emmc_host *host, const uint8_t *ext)
{
	uint32_t index;
	uint32_t sum = 0;
	uint32_t count;

	for (index = 0; index < GTA4L_EMMC_EXT_CSD_BYTES; index++) {
		sum += ext[index];
	}
	host->ext_csd_sum = sum;
	host->ext_csd_rev = ext[GTA4L_EMMC_EXT_CSD_REV_OFF];
	host->bus_width = (uint8_t)(ext[183] & 0x0Fu);
	host->partition_config = ext[GTA4L_EMMC_EXT_CSD_PARTITION_CONFIG_OFF];
	host->data_sector_size_4k =
	    (ext[GTA4L_EMMC_EXT_CSD_DATA_SECTOR_SIZE_OFF] == 1u) ? 1u : 0u;

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
	result = gta4l_emmc_controller_init(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}

	result = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD0, 0,
	    GTA4L_EMMC_CMD_RESP_NONE, GTA4L_EMMC_CMD_TIMEOUT_US, 0);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	result = gta4l_emmc_card_ready(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}

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

	result = gta4l_emmc_send_command(host, GTA4L_EMMC_CMD3,
	    GTA4L_EMMC_DEFAULT_RCA << 16, GTA4L_EMMC_CMD_RESP_SHORT |
	    GTA4L_EMMC_CMD_CRC | GTA4L_EMMC_CMD_INDEX,
	    GTA4L_EMMC_CMD_TIMEOUT_US, response);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	if ((response[0] & GTA4L_EMMC_R1_ERROR_MASK) != 0) {
		return GTA4L_EMMC_ERR_CARD_STATUS;
	}
	/* eMMC CMD3 is SET_RELATIVE_ADDR: the host assigns the RCA in the
	 * argument and the response is R1 status, not a returned RCA. */
	host->rca = GTA4L_EMMC_DEFAULT_RCA;

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
	result = gta4l_emmc_wait_tran(host);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}

	result = gta4l_emmc_read_ext_csd(host, ext_csd);
	if (result != GTA4L_EMMC_OK) {
		return result;
	}
	gta4l_emmc_parse_ext_csd(host, ext_csd);
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
	host->initialized = 1;
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
