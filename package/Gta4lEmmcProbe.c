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

struct gta4l_emmc_probe {
	uintptr_t sdhci;
	uintptr_t gcc;
	uintptr_t tlmm;
	uint32_t log_count;
	uint32_t last_event;
	uint32_t last_a;
	uint32_t last_b;
};

static int gta4l_emmc_probe_ran;

static inline uint64_t
gta4l_emmc_probe_counter(void)
{
	uint64_t value;

	__asm__ volatile("mrs %0, cntvct_el0" : "=r" (value));
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
	uint64_t start;
	uint64_t ticks;
	uint64_t spins = 0;
	uint64_t limit;

	(void)context;
	/* The documented SM6115 generic counter is 19.2 MHz: 19.2 ticks/us. */
	ticks = ((uint64_t)microseconds * 192u) / 10u;
	limit = ticks + 1000000u;
	start = gta4l_emmc_probe_counter();
	while ((gta4l_emmc_probe_counter() - start) < ticks) {
		if (++spins > limit) {
			break;
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
};

static int
gta4l_emmc_probe_parse(const char *name, int *value)
{
	return PE_parse_boot_argn(name, value, sizeof(*value)) && (*value == 1);
}

static int
gta4l_emmc_probe_conflict(void)
{
	int value = 0;

	return gta4l_emmc_probe_parse("gta4l-sdhci-audit", &value) ||
	    gta4l_emmc_probe_parse("gta4l-sdhci-snapshot", &value) ||
	    gta4l_emmc_probe_parse("gta4l-sdhci-host-init", &value);
}

void
gta4l_emmc_probe_preflight(void)
{
	int enabled = 0;

	if (!gta4l_emmc_probe_parse("gta4l-emmc-probe", &enabled)) {
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

void
gta4l_emmc_probe_run(void)
{
	static uint8_t block0[GTA4L_EMMC_BLOCK_BYTES];
	static uint8_t block1[GTA4L_EMMC_BLOCK_BYTES];
	struct gta4l_emmc_probe probe;
	struct gta4l_emmc_host host;
	vm_offset_t sdhci_mapping;
	vm_offset_t gcc_mapping;
	vm_offset_t tlmm_mapping;
	uint32_t version;
	uint8_t power;
	uint16_t clock;
	uint32_t pwr_status;
	uint32_t cfg;
	uint32_t cmd;
	int enabled = 0;
	int result;

	if (gta4l_emmc_probe_ran != 0) {
		return;
	}
	if (!gta4l_emmc_probe_parse("gta4l-emmc-probe", &enabled)) {
		return;
	}
	if (gta4l_emmc_probe_conflict()) {
		panic("GTA4L-EMMC PROBE REFUSED: conflicting diagnostic "
		    "arguments");
	}
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

	bzero(&host, sizeof(host));
	host.ops = &gta4l_emmc_probe_ops;
	host.context = &probe;

	result = gta4l_emmc_init(&host);
	if (result != GTA4L_EMMC_OK) {
		version = gta4l_emmc_probe_read32(&probe,
		    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_MCI_VERSION);
		power = gta4l_emmc_probe_read8(&probe,
		    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_POWER_CONTROL);
		clock = gta4l_emmc_probe_read16(&probe,
		    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CLOCK_CONTROL);
		pwr_status = gta4l_emmc_probe_read32(&probe,
		    GTA4L_EMMC_SPACE_SDHCI,
		    GTA4L_EMMC_REG_CORE_PWRCTL_STATUS);
		cfg = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_CFG);
		cmd = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,
		    GTA4L_EMMC_GCC_SDCC1_APPS_CMD);
		panic("GTA4L-EMMC PROBE FAILED: rc=%d mci=%08x pwr=%02x "
		    "clk=%04x pst=%08x cfg=%08x cmd=%08x log=%u/%u/%u",
		    result, version, (unsigned int)power,
		    (unsigned int)clock, pwr_status, cfg, cmd,
		    probe.log_count, probe.last_event, probe.last_a);
	}

	result = gta4l_emmc_read_block(&host, 0, block0);
	if (result != GTA4L_EMMC_OK) {
		panic("GTA4L-EMMC PROBE FAILED: block 0 read rc=%d", result);
	}
	result = gta4l_emmc_read_block(&host, 1, block1);
	if (result != GTA4L_EMMC_OK) {
		panic("GTA4L-EMMC PROBE FAILED: block 1 read rc=%d", result);
	}

	panic("GTA4L-EMMC PROBE REPORT: rc=0 mci=%08x rca=%u cap=%u rev=%u "
	    "bw=%u ocr=%08x ext=%08x log=%u\n"
	    "GTA4L-EMMC PROBE CID %08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE CSD %08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE BLOCK0 sum=%08x words=%08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE BLOCK1 sum=%08x words=%08x %08x %08x %08x",
	    host.mci_version, host.rca, host.capacity_blocks,
	    (unsigned int)host.ext_csd_rev, (unsigned int)host.bus_width,
	    host.ocr, host.ext_csd_sum, probe.log_count,
	    host.cid[0], host.cid[1], host.cid[2], host.cid[3],
	    host.csd[0], host.csd[1], host.csd[2], host.csd[3],
	    gta4l_emmc_probe_sum(block0, GTA4L_EMMC_BLOCK_BYTES),
	    gta4l_emmc_probe_word(block0, 0),
	    gta4l_emmc_probe_word(block0, 446),
	    gta4l_emmc_probe_word(block0, 450),
	    gta4l_emmc_probe_word(block0, 454),
	    gta4l_emmc_probe_sum(block1, GTA4L_EMMC_BLOCK_BYTES),
	    gta4l_emmc_probe_word(block1, 0),
	    gta4l_emmc_probe_word(block1, 4),
	    gta4l_emmc_probe_word(block1, 8),
	    gta4l_emmc_probe_word(block1, 12));
}
