// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Bounded host-local SDHCI initialization for the Samsung gta4l.
//
// The function is called once from bsd_init after the read-only audit and
// snapshot hooks. It does nothing unless the boot argument
// "gta4l-sdhci-host-init=1" is present. It maps the controller and GCC SDCC1
// windows, validates the observed handoff state, suppresses interrupt
// signalling, performs a standard controller reset, enables only the internal
// clock, and reports a power-blocked result. It never enables bus power or the
// card clock, submits a command, touches the data window, programs DMA or
// writes storage.
//
// The host-init preflight is called before the other board diagnostics so a
// conflicting argument cannot be hidden by an earlier probe's panic.

#include <sys/types.h>
#include <pexpert/pexpert.h>
#include <machine/machine_routines.h>
#include <kern/debug.h>

#include "Gta4lSdhciHostInit.h"

void gta4l_sdhci_host_init_preflight(void);
void gta4l_sdhci_host_init_run(void);
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

static int gta4l_sdhci_host_init_ran;

static inline uint32_t
gta4l_sdhci_host_init_read32(uintptr_t base, uint32_t offset)
{
	return *(volatile uint32_t *)(base + offset);
}

static inline uint16_t
gta4l_sdhci_host_init_read16(uintptr_t base, uint32_t offset)
{
	return *(volatile uint16_t *)(base + offset);
}

static inline uint8_t
gta4l_sdhci_host_init_read8(uintptr_t base, uint32_t offset)
{
	return *(volatile uint8_t *)(base + offset);
}

static inline void
gta4l_sdhci_host_init_write32(uintptr_t base, uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(base + offset) = value;
}

static inline void
gta4l_sdhci_host_init_write16(uintptr_t base, uint32_t offset, uint16_t value)
{
	*(volatile uint16_t *)(base + offset) = value;
}

static inline void
gta4l_sdhci_host_init_write8(uintptr_t base, uint32_t offset, uint8_t value)
{
	*(volatile uint8_t *)(base + offset) = value;
}

static inline void
gta4l_sdhci_host_init_barrier(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
}

/*
 * The generic counter is monotonic and does not depend on the XNU timebase
 * conversion path being available at this early point. The documented SM6115
 * timebase is 19.2 MHz, so ten milliseconds is 192,000 ticks. Every wait also
 * has a fixed iteration cap in case the counter is stuck.
 */
static inline uint64_t
gta4l_sdhci_host_init_counter(void)
{
	uint64_t value;

	__asm__ volatile("mrs %0, cntvct_el0" : "=r" (value));
	return value;
}

static int
gta4l_sdhci_host_init_conflict(void)
{
	int audit = 0;
	int snapshot = 0;

	if (PE_parse_boot_argn("gta4l-sdhci-audit", &audit, sizeof(audit)) &&
	    audit == 1) {
		return 1;
	}
	if (PE_parse_boot_argn("gta4l-sdhci-snapshot", &snapshot,
	    sizeof(snapshot)) && snapshot == 1) {
		return 1;
	}
	return 0;
}

void
gta4l_sdhci_host_init_preflight(void)
{
	int enabled = 0;

	if (!PE_parse_boot_argn("gta4l-sdhci-host-init", &enabled,
	    sizeof(enabled)) || enabled != 1) {
		return;
	}
	if (gta4l_sdhci_host_init_conflict()) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: conflicting diagnostic "
		    "arguments");
	}
}

static int
gta4l_sdhci_host_init_poll_clear8(uintptr_t base, uint32_t offset,
    uint32_t *last_value, uint64_t *elapsed_ticks)
{
	uint64_t start = gta4l_sdhci_host_init_counter();
	uint32_t i;

	for (i = 0; i < GTA4L_SDHCI_HOST_INIT_MAX_POLL_ITERATIONS; i++) {
		uint32_t value = gta4l_sdhci_host_init_read8(base, offset);

		*last_value = value;
		if (value == 0) {
			*elapsed_ticks = gta4l_sdhci_host_init_counter() - start;
			return 1;
		}
		if ((gta4l_sdhci_host_init_counter() - start) >=
		    GTA4L_SDHCI_HOST_INIT_TIMEOUT_TICKS) {
			break;
		}
	}
	*elapsed_ticks = gta4l_sdhci_host_init_counter() - start;
	return 0;
}

static int
gta4l_sdhci_host_init_poll_clock(uintptr_t base, uint32_t offset,
    uint32_t *last_value, uint64_t *elapsed_ticks)
{
	uint64_t start = gta4l_sdhci_host_init_counter();
	uint32_t i;

	for (i = 0; i < GTA4L_SDHCI_HOST_INIT_MAX_POLL_ITERATIONS; i++) {
		uint32_t value = gta4l_sdhci_host_init_read16(base, offset);

		*last_value = value;
		if ((value & (GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN |
		    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_STABLE)) ==
		    (GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN |
		    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_STABLE)) {
			*elapsed_ticks = gta4l_sdhci_host_init_counter() - start;
			return 1;
		}
		if ((gta4l_sdhci_host_init_counter() - start) >=
		    GTA4L_SDHCI_HOST_INIT_TIMEOUT_TICKS) {
			break;
		}
	}
	*elapsed_ticks = gta4l_sdhci_host_init_counter() - start;
	return 0;
}

void
gta4l_sdhci_host_init_run(void)
{
	int enabled = 0;
	vm_offset_t sdhci_mapping;
	vm_offset_t gcc_mapping;
	uintptr_t sdhci;
	uintptr_t gcc;
	uint32_t mci_version;
	uint32_t present_before;
	uint16_t clock_before;
	uint8_t power_before;
	uint32_t int_enable_before;
	uint32_t signal_enable_before;
	uint32_t core_before;
	uint32_t pwr_status_before;
	uint32_t pwr_mask_before;
	uint32_t vendor_before;
	uint32_t ddr_before;
	uint32_t dll_before;
	uint32_t bcr;
	uint32_t apps_cbcr;
	uint32_t ahb_cbcr;
	uint32_t ice_cbcr;
	uint32_t apps_cmd;
	uint32_t apps_cfg;
	uint32_t apps_m;
	uint32_t apps_n;
	uint32_t apps_d;
	uint32_t reset_last = 0;
	uint32_t clock_last = 0;
	uint64_t reset_ticks = 0;
	uint64_t clock_ticks = 0;
	uint32_t mci_after_reset;
	uint16_t clock_after_reset;
	uint8_t power_after_reset;
	uint32_t int_enable_after_reset;
	uint32_t signal_enable_after_reset;
	uint16_t clock_write;
	uint16_t clock_final;
	uint32_t present_final;
	uint8_t power_final;
	uint32_t int_enable_final;
	uint32_t signal_enable_final;
	uint8_t reset_final;
	uint32_t core_final;
	uint32_t pwr_status_final;
	uint32_t pwr_mask_final;
	uint32_t vendor_final;
	uint32_t ddr_final;
	uint32_t dll_final;

	if (gta4l_sdhci_host_init_ran) {
		return;
	}
	if (!PE_parse_boot_argn("gta4l-sdhci-host-init", &enabled,
	    sizeof(enabled)) || enabled != 1) {
		return;
	}
	if (gta4l_sdhci_host_init_conflict()) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: conflicting diagnostic "
		    "arguments");
	}
	gta4l_sdhci_host_init_ran = 1;

	sdhci_mapping = ml_io_map(GTA4L_SDHCI_HOST_INIT_SDHCI_BASE,
	    GTA4L_SDHCI_HOST_INIT_SDHCI_SIZE);
	if (sdhci_mapping == 0) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: SDHCI map failed");
	}
	sdhci = (uintptr_t)sdhci_mapping;

	mci_version = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_MCI_VERSION);
	if ((mci_version >> 28) != 1) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: unexpected MCI version "
		    "%08x", mci_version);
	}

	/* Capture the handoff state before any write. */
	present_before = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_PRESENT_STATE);
	clock_before = gta4l_sdhci_host_init_read16(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL);
	power_before = gta4l_sdhci_host_init_read8(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL);
	int_enable_before = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE);
	signal_enable_before = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE);
	core_before = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_GENERICS);
	pwr_status_before = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS);
	pwr_mask_before = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_MASK);
	vendor_before = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_VENDOR_SPEC);
	ddr_before = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_DDR_CONFIG);
	dll_before = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_DLL_STATUS);

	gcc_mapping = ml_io_map(GTA4L_SDHCI_HOST_INIT_GCC_BASE,
	    GTA4L_SDHCI_HOST_INIT_GCC_SIZE);
	if (gcc_mapping == 0) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: GCC map failed");
	}
	gcc = (uintptr_t)gcc_mapping;
	bcr = gta4l_sdhci_host_init_read32(gcc,
	    GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_BCR);
	apps_cbcr = gta4l_sdhci_host_init_read32(gcc,
	    GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CBCR);
	ahb_cbcr = gta4l_sdhci_host_init_read32(gcc,
	    GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_AHB_CBCR);
	ice_cbcr = gta4l_sdhci_host_init_read32(gcc,
	    GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_ICE_CBCR);
	apps_cmd = gta4l_sdhci_host_init_read32(gcc,
	    GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CMD);
	apps_cfg = gta4l_sdhci_host_init_read32(gcc,
	    GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_CFG);
	apps_m = gta4l_sdhci_host_init_read32(gcc,
	    GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_M);
	apps_n = gta4l_sdhci_host_init_read32(gcc,
	    GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_N);
	apps_d = gta4l_sdhci_host_init_read32(gcc,
	    GTA4L_SDHCI_HOST_INIT_REG_GCC_SDCC1_APPS_D);

	/* Stage 0: refuse an unexpected handoff instead of overwriting it. */
	if (power_before & GTA4L_SDHCI_HOST_INIT_POWER_BUS_POWER) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: bus power on before "
		    "writes (pwr=%02x clk=%04x)",
		    (unsigned int)power_before, (unsigned int)clock_before);
	}
	if (clock_before & GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: card clock on before "
		    "writes (pwr=%02x clk=%04x)",
		    (unsigned int)power_before, (unsigned int)clock_before);
	}
	if (present_before & (GTA4L_SDHCI_HOST_INIT_PRESENT_CMD_INHIBIT |
	    GTA4L_SDHCI_HOST_INIT_PRESENT_DAT_INHIBIT)) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: command/data inhibit set "
		    "(ps=%08x)", present_before);
	}
	if (bcr != 0) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: SDCC1 block reset "
		    "asserted (bcr=%08x)", bcr);
	}
	if ((apps_cbcr & (GTA4L_SDHCI_HOST_INIT_CBCR_CLK_OFF |
	    GTA4L_SDHCI_HOST_INIT_CBCR_CLOCK_ENABLE)) !=
	    GTA4L_SDHCI_HOST_INIT_CBCR_CLOCK_ENABLE) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: apps branch not running "
		    "(ac=%08x)", apps_cbcr);
	}
	if ((ahb_cbcr & (GTA4L_SDHCI_HOST_INIT_CBCR_CLK_OFF |
	    GTA4L_SDHCI_HOST_INIT_CBCR_CLOCK_ENABLE)) !=
	    GTA4L_SDHCI_HOST_INIT_CBCR_CLOCK_ENABLE) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: AHB branch not running "
		    "(ahb=%08x)", ahb_cbcr);
	}
	if ((ice_cbcr & (GTA4L_SDHCI_HOST_INIT_CBCR_CLK_OFF |
	    GTA4L_SDHCI_HOST_INIT_CBCR_CLOCK_ENABLE)) !=
	    GTA4L_SDHCI_HOST_INIT_CBCR_CLOCK_ENABLE) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: ICE branch not running "
		    "(ice=%08x)", ice_cbcr);
	}
	if (apps_cmd & (GTA4L_SDHCI_HOST_INIT_RCG_CMD_UPDATE |
	    GTA4L_SDHCI_HOST_INIT_RCG_CMD_ROOT_OFF)) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: apps RCG busy or off "
		    "(cmd=%08x)", apps_cmd);
	}
	if ((apps_cfg & GTA4L_SDHCI_HOST_INIT_RCG_SRC_SEL_MASK) !=
	    GTA4L_SDHCI_HOST_INIT_RCG_SRC_SEL_GPLL6 ||
	    (apps_cfg & GTA4L_SDHCI_HOST_INIT_RCG_SRC_DIV_MASK) !=
	    GTA4L_SDHCI_HOST_INIT_RCG_SRC_DIV_EXPECTED ||
	    (apps_cfg & GTA4L_SDHCI_HOST_INIT_RCG_MODE_MASK) != 0) {
		panic("GTA4L-SDHCI HOST INIT REFUSED: unexpected apps RCG "
		    "configuration (cfg=%08x)", apps_cfg);
	}

	/* Stage 2: suppress signalling, then reset the host controller. */
	if (int_enable_before != 0) {
		gta4l_sdhci_host_init_write32(sdhci,
		    GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE, 0);
		gta4l_sdhci_host_init_barrier();
	}
	if (signal_enable_before != 0) {
		gta4l_sdhci_host_init_write32(sdhci,
		    GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE, 0);
		gta4l_sdhci_host_init_barrier();
	}
	if (gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE) != 0 ||
	    gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE) != 0) {
		panic("GTA4L-SDHCI HOST INIT FAILED: interrupt suppression did "
		    "not clear (ie=%08x se=%08x)",
		    gta4l_sdhci_host_init_read32(sdhci,
		    GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE),
		    gta4l_sdhci_host_init_read32(sdhci,
		    GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE));
	}

	gta4l_sdhci_host_init_write8(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET,
	    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL);
	gta4l_sdhci_host_init_barrier();
	if (!gta4l_sdhci_host_init_poll_clear8(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET, &reset_last,
	    &reset_ticks)) {
		panic("GTA4L-SDHCI HOST INIT FAILED: reset timeout "
		    "(last=%02x counter=%u)", reset_last,
		    (unsigned int)(reset_ticks != 0));
	}

	mci_after_reset = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_MCI_VERSION);
	if ((mci_after_reset >> 28) != 1) {
		panic("GTA4L-SDHCI HOST INIT FAILED: MCI version changed after "
		    "reset (%08x)", mci_after_reset);
	}
	clock_after_reset = gta4l_sdhci_host_init_read16(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL);
	power_after_reset = gta4l_sdhci_host_init_read8(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL);
	int_enable_after_reset = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE);
	signal_enable_after_reset = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE);
	if (power_after_reset & GTA4L_SDHCI_HOST_INIT_POWER_BUS_POWER) {
		panic("GTA4L-SDHCI HOST INIT FAILED: bus power on after reset "
		    "(pwr=%02x)", (unsigned int)power_after_reset);
	}
	if (clock_after_reset & GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) {
		panic("GTA4L-SDHCI HOST INIT FAILED: card clock on after reset "
		    "(clk=%04x)", (unsigned int)clock_after_reset);
	}
	if (int_enable_after_reset != 0 || signal_enable_after_reset != 0) {
		panic("GTA4L-SDHCI HOST INIT FAILED: interrupts re-enabled after "
		    "reset (ie=%08x se=%08x)", int_enable_after_reset,
		    signal_enable_after_reset);
	}

	/* Stage 3: enable only the internal clock; the card clock stays off. */
	clock_write = (uint16_t)((clock_after_reset &
	    ~GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) |
	    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN);
	gta4l_sdhci_host_init_write16(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, clock_write);
	gta4l_sdhci_host_init_barrier();
	if (!gta4l_sdhci_host_init_poll_clock(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, &clock_last,
	    &clock_ticks)) {
		panic("GTA4L-SDHCI HOST INIT FAILED: internal clock timeout "
		    "(last=%04x counter=%u)", clock_last,
		    (unsigned int)(clock_ticks != 0));
	}
	clock_final = gta4l_sdhci_host_init_read16(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL);
	if (clock_final & GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) {
		panic("GTA4L-SDHCI HOST INIT FAILED: card clock enabled after "
		    "internal-clock stage (clk=%04x)",
		    (unsigned int)clock_final);
	}
	if ((clock_final & (GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN |
	    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_STABLE)) !=
	    (GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN |
	    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_STABLE)) {
		panic("GTA4L-SDHCI HOST INIT FAILED: internal clock not stable "
		    "(clk=%04x)", (unsigned int)clock_final);
	}

	/* Stage 4 is blocked: no power, regulator or vendor power write. */
	present_final = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_PRESENT_STATE);
	power_final = gta4l_sdhci_host_init_read8(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL);
	int_enable_final = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE);
	signal_enable_final = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_SIGNAL_ENABLE);
	reset_final = gta4l_sdhci_host_init_read8(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET);
	core_final = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_GENERICS);
	pwr_status_final = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_STATUS);
	pwr_mask_final = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_PWRCTL_MASK);
	vendor_final = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_VENDOR_SPEC);
	ddr_final = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_DDR_CONFIG);
	dll_final = gta4l_sdhci_host_init_read32(sdhci,
	    GTA4L_SDHCI_HOST_INIT_REG_CORE_DLL_STATUS);

	panic("GTA4L-SDHCI HOST INIT REPORT: host-local stages complete; power "
	    "stage blocked; vendor state reported, not enforced\n"
	    "GTA4L-SDHCI HOST INIT BEFORE ps=%08x clk=%04x pwr=%02x "
	    "ie=%08x se=%08x\n"
	    "GTA4L-SDHCI HOST INIT VENDOR g=%08x pst=%08x pm=%08x vs=%08x "
	    "ddr=%08x dll=%08x\n"
	    "GTA4L-SDHCI HOST INIT GCC bcr=%08x ac=%08x ahb=%08x ice=%08x "
	    "cmd=%08x cfg=%08x m=%08x n=%08x d=%08x\n"
	    "GTA4L-SDHCI HOST INIT AFTER ps=%08x clk=%04x pwr=%02x ie=%08x "
	    "se=%08x reset=%02x\n"
	    "GTA4L-SDHCI HOST INIT VENDOR AFTER g=%08x pst=%08x pm=%08x "
	    "vs=%08x ddr=%08x dll=%08x",
	    present_before, (unsigned int)clock_before,
	    (unsigned int)power_before, int_enable_before,
	    signal_enable_before,
	    core_before, pwr_status_before, pwr_mask_before, vendor_before,
	    ddr_before, dll_before,
	    bcr, apps_cbcr, ahb_cbcr, ice_cbcr, apps_cmd, apps_cfg,
	    apps_m, apps_n, apps_d,
	    present_final, (unsigned int)clock_final,
	    (unsigned int)power_final, int_enable_final,
	    signal_enable_final, (unsigned int)reset_final,
	    core_final, pwr_status_final, pwr_mask_final, vendor_final,
	    ddr_final, dll_final);
}
