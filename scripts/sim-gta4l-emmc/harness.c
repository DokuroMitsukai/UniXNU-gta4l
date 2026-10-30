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

struct sim {
	uint8_t  sdhci[SIM_SDHCI_SIZE];
	uint32_t gcc[SIM_GCC_WORDS];
	uint32_t tlmm[SIM_TLMM_WORDS];
	uint16_t normal_int;
	uint16_t error_int;
	uint32_t pwr_status;
	uint32_t power_ack;
	uint32_t buffer_data[GTA4L_EMMC_BLOCK_BYTES / 4u];
	uint32_t buffer_index;
	uint32_t last_cmd;
	uint32_t cmd_count;
	uint32_t rca;
	uint8_t  ext_csd[GTA4L_EMMC_EXT_CSD_BYTES];
	uint32_t cid[4];
	uint32_t csd[4];
	uint64_t time_us;
	uint32_t log_event[SIM_MAX_LOG];
	uint32_t log_a[SIM_MAX_LOG];
	uint32_t log_b[SIM_MAX_LOG];
	uint32_t log_count;

	int reset_timeout;
	int clock_timeout;
	int power_off;
	int rcg_busy;
	int cmd_timeout;
	int cmd_crc;
	int cmd_index;
	int data_timeout;
	int data_crc;
	int no_tc;
	int no_brr;
	int card_never_ready;
	int bad_cid;
	int bad_csd;
	int rca_zero;
	int power_written;
	int card_clock_before_power;
	int card_clock_enabled;
	int reset_written;
	int branch_writes;
};

static uint8_t
sim_read8(void *context, uint32_t space, uint32_t offset)
{
	struct sim *sim = (struct sim *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return sim->sdhci[offset];
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return (uint8_t)(sim->gcc[offset / 4u] >> ((offset & 3u) * 8u));
	}
	if (space == GTA4L_EMMC_SPACE_TLMM) {
		return (uint8_t)(sim->tlmm[offset / 4u] >> ((offset & 3u) * 8u));
	}
	return 0;
}

static uint16_t
sim_read16(void *context, uint32_t space, uint32_t offset)
{
	struct sim *sim = (struct sim *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		if (offset == GTA4L_EMMC_REG_NORMAL_INT_STATUS) {
			return sim->normal_int;
		}
		if (offset == GTA4L_EMMC_REG_ERROR_INT_STATUS) {
			return sim->error_int;
		}
		return (uint16_t)(sim->sdhci[offset] |
		    ((uint16_t)sim->sdhci[offset + 1u] << 8));
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return (uint16_t)(sim->gcc[offset / 4u] >> ((offset & 3u) * 8u));
	}
	if (space == GTA4L_EMMC_SPACE_TLMM) {
		return (uint16_t)(sim->tlmm[offset / 4u] >> ((offset & 3u) * 8u));
	}
	return 0;
}

static uint32_t
sim_read32(void *context, uint32_t space, uint32_t offset)
{
	struct sim *sim = (struct sim *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_STATUS) {
			return sim->pwr_status;
		}
		if (offset == GTA4L_EMMC_REG_BUFFER_DATA_PORT) {
			uint32_t value = sim->buffer_data[
			    sim->buffer_index % (GTA4L_EMMC_BLOCK_BYTES / 4u)];
			sim->buffer_index++;
			return value;
		}
		return (uint32_t)sim->sdhci[offset] |
		    ((uint32_t)sim->sdhci[offset + 1u] << 8) |
		    ((uint32_t)sim->sdhci[offset + 2u] << 16) |
		    ((uint32_t)sim->sdhci[offset + 3u] << 24);
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return sim->gcc[offset / 4u];
	}
	if (space == GTA4L_EMMC_SPACE_TLMM) {
		return sim->tlmm[offset / 4u];
	}
	return 0;
}

static void
sim_write8(void *context, uint32_t space, uint32_t offset, uint8_t value)
{
	struct sim *sim = (struct sim *)context;

	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		sim->sdhci[offset] = value;
		if (offset == GTA4L_EMMC_REG_SOFTWARE_RESET) {
			sim->reset_written = 1;
			if (!sim->reset_timeout) {
				sim->sdhci[offset] = 0;
			}
		}
		if (offset == GTA4L_EMMC_REG_POWER_CONTROL) {
			sim->power_written = 1;
			if ((value & GTA4L_EMMC_POWER_BUS_POWER) != 0) {
				sim->pwr_status = sim->power_off ?
				    GTA4L_EMMC_PWRCTL_BUS_OFF :
				    (GTA4L_EMMC_PWRCTL_BUS_ON |
				    GTA4L_EMMC_PWRCTL_IO_HIGH);
			}
		}
		if (offset == GTA4L_EMMC_REG_CLOCK_CONTROL) {
			if (((value & GTA4L_EMMC_CLOCK_CARD_EN) != 0) &&
			    (sim->power_written == 0)) {
				sim->card_clock_before_power = 1;
			}
			if ((value & GTA4L_EMMC_CLOCK_CARD_EN) != 0) {
				sim->card_clock_enabled = 1;
			}
			if ((value & GTA4L_EMMC_CLOCK_INT_EN) != 0) {
				if (!sim->clock_timeout) {
					sim->sdhci[offset] |= GTA4L_EMMC_CLOCK_INT_STABLE;
				}
			}
		}
		return;
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		uint32_t shift = (offset & 3u) * 8u;
		uint32_t mask = 0xFFu << shift;

		sim->gcc[offset / 4u] = (sim->gcc[offset / 4u] & ~mask) |
		    ((uint32_t)value << shift);
		return;
	}
	if (space == GTA4L_EMMC_SPACE_TLMM) {
		uint32_t shift = (offset & 3u) * 8u;
		uint32_t mask = 0xFFu << shift;

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
	uint32_t count = 61071360u;

	memset(sim->ext_csd, 0, sizeof(sim->ext_csd));
	sim->ext_csd[GTA4L_EMMC_EXT_CSD_REV_OFF] = 8;
	sim->ext_csd[183] = 2;
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

	for (index = 0; index < count; index++) {
		sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_RESPONSE + index * 4u, words[index]);
	}
}

static void
sim_command(struct sim *sim, uint16_t command)
{
	uint32_t index = (command >> 8) & 0x3Fu;
	uint32_t argument = sim_read32(sim, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_ARGUMENT);
	uint32_t response = 0x00000900u; /* READY_FOR_DATA | TRAN */
	uint32_t words[4];

	sim->last_cmd = index;
	sim->cmd_count++;
	sim->buffer_index = 0;

	if ((sim->cmd_timeout != 0) && (index == GTA4L_EMMC_CMD0)) {
		return;
	}
	if ((sim->cmd_crc != 0) && (index == GTA4L_EMMC_CMD2)) {
		sim->error_int |= GTA4L_EMMC_INT_ERR_CMD_CRC;
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
		if (sim->card_never_ready == 0) {
			response |= 0x80000000u;
		}
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		sim_set_response(sim, &response, 1);
		break;
	case GTA4L_EMMC_CMD2:
		words[0] = sim->bad_cid ? 0 : sim->cid[0];
		words[1] = sim->bad_cid ? 0 : sim->cid[1];
		words[2] = sim->bad_cid ? 0 : sim->cid[2];
		words[3] = sim->bad_cid ? 0 : sim->cid[3];
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		sim_set_response(sim, words, 4);
		break;
	case GTA4L_EMMC_CMD3:
		sim->rca = sim->rca_zero ? 0u : 1u;
		response = sim->rca << 16;
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		sim_set_response(sim, &response, 1);
		break;
	case GTA4L_EMMC_CMD7:
		sim->normal_int |= GTA4L_EMMC_INT_CC;
		sim_set_response(sim, &response, 1);
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
}

static void
sim_write16(void *context, uint32_t space, uint32_t offset, uint16_t value)
{
	struct sim *sim = (struct sim *)context;

	if (space != GTA4L_EMMC_SPACE_SDHCI) {
		sim_write8(context, space, offset, (uint8_t)(value & 0xFFu));
		sim_write8(context, space, offset + 1u, (uint8_t)(value >> 8));
		return;
	}
	if (offset == GTA4L_EMMC_REG_COMMAND) {
		sim->sdhci[offset] = (uint8_t)(value & 0xFFu);
		sim->sdhci[offset + 1u] = (uint8_t)(value >> 8);
		sim_command(sim, value);
		return;
	}
	if (offset == GTA4L_EMMC_REG_NORMAL_INT_STATUS) {
		sim->normal_int &= (uint16_t)~value;
		return;
	}
	if (offset == GTA4L_EMMC_REG_ERROR_INT_STATUS) {
		sim->error_int &= (uint16_t)~value;
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
		}
		if (((value & GTA4L_EMMC_CLOCK_INT_EN) != 0) &&
		    (sim->clock_timeout == 0)) {
			sim->sdhci[offset] |= GTA4L_EMMC_CLOCK_INT_STABLE;
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
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_CLEAR) {
			sim->pwr_status &= ~value;
			return;
		}
		if (offset == GTA4L_EMMC_REG_CORE_PWRCTL_CTL) {
			sim->power_ack = value;
			if ((value & GTA4L_EMMC_PWRCTL_ACK_BUS) != 0) {
				sim->pwr_status &= ~GTA4L_EMMC_PWRCTL_BUS_ON;
			}
			if ((value & GTA4L_EMMC_PWRCTL_ACK_IO) != 0) {
				sim->pwr_status &= ~(GTA4L_EMMC_PWRCTL_IO_LOW |
				    GTA4L_EMMC_PWRCTL_IO_HIGH);
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
		sim->gcc[offset / 4u] = value;
		if (offset == GTA4L_EMMC_GCC_SDCC1_BCR) {
			if ((value & GTA4L_EMMC_BCR_BLOCK_RESET) != 0) {
				sim->gcc[offset / 4u] &= ~GTA4L_EMMC_BCR_BLOCK_RESET;
			}
		}
		if ((offset == GTA4L_EMMC_GCC_SDCC1_APPS_CBCR) ||
		    (offset == GTA4L_EMMC_GCC_SDCC1_AHB_CBCR)) {
			sim->branch_writes++;
			if ((value & GTA4L_EMMC_CBCR_CLOCK_ENABLE) != 0) {
				sim->gcc[offset / 4u] &= ~GTA4L_EMMC_CBCR_CLK_OFF;
			} else {
				sim->gcc[offset / 4u] |= GTA4L_EMMC_CBCR_CLK_OFF;
			}
		}
		if (offset == GTA4L_EMMC_GCC_SDCC1_APPS_CMD) {
			if ((value & GTA4L_EMMC_RCG_CMD_UPDATE) != 0) {
				if (sim->rcg_busy == 0) {
					sim->gcc[offset / 4u] &=
					    ~GTA4L_EMMC_RCG_CMD_UPDATE;
				}
			}
		}
		return;
	}
	if (space == GTA4L_EMMC_SPACE_TLMM) {
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
};

static void
sim_init(struct sim *sim)
{
	memset(sim, 0, sizeof(*sim));
	sim_write32(sim, GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_MCI_VERSION,
	    0x10000072u);
	sim->gcc[GTA4L_EMMC_GCC_SDCC1_APPS_CBCR / 4u] =
	    GTA4L_EMMC_CBCR_CLOCK_ENABLE;
	sim->gcc[GTA4L_EMMC_GCC_SDCC1_AHB_CBCR / 4u] =
	    GTA4L_EMMC_CBCR_CLOCK_ENABLE;
	sim->gcc[GTA4L_EMMC_GCC_SDCC1_ICE_CBCR / 4u] =
	    GTA4L_EMMC_CBCR_CLOCK_ENABLE;
	sim->gcc[GTA4L_EMMC_GCC_SDCC1_APPS_CFG / 4u] = 0x00000401u;
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

static int
scenario_is(const char *name, const char *expected)
{
	return strcmp(name, expected) == 0;
}

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
	int result;
	int expected;

	sim_init(&sim);
	memset(&host, 0, sizeof(host));
	host.ops = &sim_ops;
	host.context = &sim;

	if (scenario_is(scenario, "reset-timeout")) {
		sim.reset_timeout = 1;
	} else if (scenario_is(scenario, "clock-timeout")) {
		sim.clock_timeout = 1;
	} else if (scenario_is(scenario, "power-off")) {
		sim.power_off = 1;
	} else if (scenario_is(scenario, "rcg-busy")) {
		sim.rcg_busy = 1;
	} else if (scenario_is(scenario, "cmd-timeout")) {
		sim.cmd_timeout = 1;
	} else if (scenario_is(scenario, "cmd-crc")) {
		sim.cmd_crc = 1;
	} else if (scenario_is(scenario, "cmd-index")) {
		sim.cmd_index = 1;
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
	} else if (scenario_is(scenario, "rca-zero")) {
		sim.rca_zero = 1;
	} else if (scenario_is(scenario, "version")) {
		sim_write32(&sim, GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_MCI_VERSION, 0x20000000u);
	}

	result = gta4l_emmc_init(&host);

	if (scenario_is(scenario, "success") ||
	    scenario_is(scenario, "read-arg") ||
	    scenario_is(scenario, "read-without-init") ||
	    scenario_is(scenario, "read-data-timeout") ||
	    scenario_is(scenario, "read-data-crc") ||
	    scenario_is(scenario, "read-no-tc") ||
	    scenario_is(scenario, "read-no-brr") ||
	    scenario_is(scenario, "read-repeat") ||
	    scenario_is(scenario, "read-ext-csd")) {
		check(result == GTA4L_EMMC_OK, "initialization failed");
		if (result != GTA4L_EMMC_OK) {
			fprintf(stderr, "init result %d\n", result);
			return 1;
		}
	}

	if (scenario_is(scenario, "success")) {
		check(host.rca == 1u, "RCA is not 1");
		check(host.capacity_blocks == 61071360u, "capacity mismatch");
		check(host.ext_csd_rev == 8u, "EXT_CSD revision mismatch");
		check(host.bus_width == 2u, "bus width mismatch");
		check(host.cid[0] != 0u, "CID is empty");
		check(host.csd[0] != 0u, "CSD is empty");
		check(sim.gcc[GTA4L_EMMC_GCC_SDCC1_APPS_CFG / 4u] ==
		    0x00002017u, "RCG CFG is not 400 kHz");
		check(sim.gcc[GTA4L_EMMC_GCC_SDCC1_APPS_M / 4u] == 0x00000001u,
		    "RCG M mismatch");
		check(sim.gcc[GTA4L_EMMC_GCC_SDCC1_APPS_N / 4u] == 0x000000FCu,
		    "RCG N mismatch");
		check(sim.gcc[GTA4L_EMMC_GCC_SDCC1_APPS_D / 4u] == 0x000000FBu,
		    "RCG D mismatch");
		check(sim.tlmm[0] == 0x00009FE4u,
		    "SDC1 pad config mismatch");
		check(sim.power_ack == (GTA4L_EMMC_PWRCTL_ACK_BUS |
		    GTA4L_EMMC_PWRCTL_ACK_IO), "power ack mismatch");
		check(sim.card_clock_enabled == 1, "card clock not enabled");
		check(sim.card_clock_before_power == 0,
		    "card clock enabled before power");
		check(sim.last_cmd == GTA4L_EMMC_CMD8,
		    "initialization did not end with CMD8");
		check(sim.log_count > 0, "no log events");

		result = gta4l_emmc_read_block(&host, 0, block0);
		check(result == GTA4L_EMMC_OK, "block 0 read failed");
		if (result == GTA4L_EMMC_OK) {
			check_block(&sim, 0, block0, "block 0");
		}
		result = gta4l_emmc_read_block(&host, 1, block1);
		check(result == GTA4L_EMMC_OK, "block 1 read failed");
		if (result == GTA4L_EMMC_OK) {
			check_block(&sim, 1, block1, "block 1");
		}
		check(sim.last_cmd == GTA4L_EMMC_CMD17, "last command is not CMD17");
	} else if (scenario_is(scenario, "read-arg")) {
		result = gta4l_emmc_read_block(&host, 0x100000000ULL, block0);
		check(result == GTA4L_EMMC_ERR_ARG, "overflow block was accepted");
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
	} else if (scenario_is(scenario, "read-data-crc")) {
		sim.data_crc = 1;
		result = gta4l_emmc_read_block(&host, 0, block0);
		check(result == GTA4L_EMMC_ERR_DATA_CRC,
		    "data CRC not reported");
	} else if (scenario_is(scenario, "read-no-tc")) {
		sim.no_tc = 1;
		result = gta4l_emmc_read_block(&host, 0, block0);
		check(result == GTA4L_EMMC_ERR_DATA_TIMEOUT,
		    "missing TC not reported");
	} else if (scenario_is(scenario, "read-no-brr")) {
		sim.no_brr = 1;
		result = gta4l_emmc_read_block(&host, 0, block0);
		check(result == GTA4L_EMMC_ERR_DATA_TIMEOUT,
		    "missing BRR not reported");
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
	} else if (scenario_is(scenario, "read-ext-csd")) {
		result = gta4l_emmc_read_ext_csd(&host, ext);
		check(result == GTA4L_EMMC_OK, "EXT_CSD read failed");
		check(ext[GTA4L_EMMC_EXT_CSD_REV_OFF] == 8u,
		    "EXT_CSD revision mismatch");
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
			    scenario_is(scenario, "rcg-busy");
			break;
		case GTA4L_EMMC_ERR_POWER:
			expected = scenario_is(scenario, "power-off");
			break;
		case GTA4L_EMMC_ERR_CMD_TIMEOUT:
			expected = scenario_is(scenario, "cmd-timeout");
			break;
		case GTA4L_EMMC_ERR_CMD_CRC:
			expected = scenario_is(scenario, "cmd-crc");
			break;
		case GTA4L_EMMC_ERR_CMD_INDEX:
			expected = scenario_is(scenario, "cmd-index");
			break;
		case GTA4L_EMMC_ERR_DATA_TIMEOUT:
			expected = scenario_is(scenario, "data-timeout-init") ||
			    scenario_is(scenario, "no-tc-init") ||
			    scenario_is(scenario, "no-brr-init");
			break;
		case GTA4L_EMMC_ERR_DATA_CRC:
			expected = scenario_is(scenario, "data-crc-init");
			break;
		case GTA4L_EMMC_ERR_CARD_NOT_READY:
			expected = scenario_is(scenario, "card-not-ready");
			break;
		case GTA4L_EMMC_ERR_RESPONSE:
			expected = scenario_is(scenario, "bad-cid") ||
			    scenario_is(scenario, "bad-csd") ||
			    scenario_is(scenario, "rca-zero");
			break;
		default:
			expected = 0;
			break;
		}
		if (!expected) {
			fprintf(stderr, "FAIL: %s returned %d\n", scenario, result);
			failures++;
		}
	}

	if (failures != 0) {
		return 1;
	}
	printf("PASS %s\n", scenario);
	return 0;
}
