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
	uint64_t counter_hz;
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

/* 1 = full probe, 2 = controller/host init only, 3 = host init plus card
 * identification without sector reads. */
static int
gta4l_emmc_probe_stage(void)
{
	int value = 0;

	if (!PE_parse_boot_argn("gta4l-emmc-probe", &value, sizeof(value))) {
		return 0;
	}
	if ((value < 1) || (value > 3)) {
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

void
gta4l_emmc_probe_preflight(void)
{
	if (gta4l_emmc_probe_stage() == 0) {
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
	uint8_t host_control;
	uint16_t clock;
	uint16_t host_control2;
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
	int stage;
	int result;

	if (gta4l_emmc_probe_ran != 0) {
		return;
	}
	stage = gta4l_emmc_probe_stage();
	if (stage == 0) {
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
	probe.counter_hz = gta4l_emmc_probe_counter_hz();

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
		panic("GTA4L-EMMC PROBE FAILED: stage=%d rc=%d "
		    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u "
		    "rsp=%08x,%08x,%08x,%08x cmd1=%u/%u/%08x "
		    "clocks=%u mci=%08x pwr=%02x clk=%04x hc=%02x "
		    "hc2=%04x ven=%08x v3=%08x dll=%08x dst=%08x "
		    "ddr=%08x pst=%08x cfg=%08x mnd=%08x/%08x/%08x "
		    "cmd=%08x bcr=%08x apps=%08x ahb=%08x step=%u "
		    "log=%u/%u/%u/%u",
		    stage, result, (unsigned int)host.fail_valid,
		    host.fail_index, host.fail_argument, host.fail_command,
		    (unsigned int)host.fail_error,
		    (unsigned int)host.fail_normal, host.fail_present,
		    host.fail_step, host.fail_response[0],
		    host.fail_response[1], host.fail_response[2],
		    host.fail_response[3], host.cmd1_attempts,
		    host.cmd1_timeouts, host.ocr,
		    (unsigned int)host.clocks_enabled, version,
		    (unsigned int)power, (unsigned int)clock,
		    (unsigned int)host_control, (unsigned int)host_control2,
		    vendor_spec, vendor_spec3, dll_config, dll_status,
		    ddr_config, pwr_status, cfg, gcc_m, gcc_n, gcc_d, cmd,
		    bcr, apps_cbcr, ahb_cbcr, host.last_step, probe.log_count,
		    probe.last_event, probe.last_a, probe.last_b);
	}

	version = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_MCI_VERSION);
	power = gta4l_emmc_probe_read8(&probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_POWER_CONTROL);
	clock = gta4l_emmc_probe_read16(&probe, GTA4L_EMMC_SPACE_SDHCI,
	    GTA4L_EMMC_REG_CLOCK_CONTROL);
	host_control2 = gta4l_emmc_probe_read16(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_HOST_CONTROL2);
	vendor_spec = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_VENDOR_SPEC);
	dll_config = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_CONFIG);
	dll_status = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_DLL_STATUS);
	pwr_status = gta4l_emmc_probe_read32(&probe,
	    GTA4L_EMMC_SPACE_SDHCI, GTA4L_EMMC_REG_CORE_PWRCTL_STATUS);

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
		panic("GTA4L-EMMC IDENT REPORT: rc=0 mci=%08x rca=%u cap=%u "
		    "rev=%u bw=%u ocr=%08x ext=%08x hc2=%04x ven=%08x "
		    "dll=%08x dst=%08x pst=%08x step=%u log=%u",
		    host.mci_version, host.rca, host.capacity_blocks,
		    (unsigned int)host.ext_csd_rev,
		    (unsigned int)host.bus_width, host.ocr,
		    host.ext_csd_sum, (unsigned int)host_control2,
		    vendor_spec, dll_config, dll_status, pwr_status,
		    host.last_step, probe.log_count);
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

	panic("GTA4L-EMMC PROBE REPORT: rc=0 mci=%08x rca=%u cap=%u rev=%u "
	    "bw=%u ocr=%08x ext=%08x hc2=%04x ven=%08x dll=%08x "
	    "dst=%08x pst=%08x step=%u log=%u\n"
	    "GTA4L-EMMC PROBE CID %08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE CSD %08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE BLOCK0 sum=%08x words=%08x %08x %08x %08x\n"
	    "GTA4L-EMMC PROBE BLOCK1 sum=%08x words=%08x %08x %08x %08x",
	    host.mci_version, host.rca, host.capacity_blocks,
	    (unsigned int)host.ext_csd_rev, (unsigned int)host.bus_width,
	    host.ocr, host.ext_csd_sum, (unsigned int)host_control2,
	    vendor_spec, dll_config, dll_status, pwr_status,
	    host.last_step, probe.log_count,
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
