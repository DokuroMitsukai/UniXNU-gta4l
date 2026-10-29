// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Read-only SDHCI handoff audit for the Samsung gta4l.
//
// The function is called once from bsd_init after bsd_bufferinit(). It does
// nothing unless the boot argument "gta4l-sdhci-audit=1" is present. When
// enabled it maps the controller's 4 KiB host window and the 4 KiB SM6115 GCC
// SDCC1 page, then reads the allowlisted registers in
// Gta4lSdhciHandoffAudit.h. It never writes a register, submits a command,
// touches the data buffer, changes a clock or power state, or accesses the
// card. A completed capture is reported with a clearly identified panic so
// the RAM-only diagnostic needs no persistent storage.

#include <sys/types.h>
#include <pexpert/pexpert.h>
#include <machine/machine_routines.h>
#include <kern/debug.h>

#include "Gta4lSdhciHandoffAudit.h"

void gta4l_sdhci_handoff_audit_run(void);
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

static int gta4l_sdhci_handoff_audit_ran;

static inline uint32_t
gta4l_sdhci_audit_read32(uintptr_t base, uint32_t offset)
{
	return *(volatile uint32_t *)(base + offset);
}

static inline uint16_t
gta4l_sdhci_audit_read16(uintptr_t base, uint32_t offset)
{
	return *(volatile uint16_t *)(base + offset);
}

static inline uint8_t
gta4l_sdhci_audit_read8(uintptr_t base, uint32_t offset)
{
	return *(volatile uint8_t *)(base + offset);
}

void
gta4l_sdhci_handoff_audit_run(void)
{
	int enabled = 0;
	vm_offset_t sdhci_mapping;
	vm_offset_t gcc_mapping;
	uintptr_t sdhci;
	uintptr_t gcc;
	uint32_t mci_version;
	uint16_t host_version;
	uint32_t capabilities;
	uint32_t capabilities_1;
	uint32_t present_state;
	uint16_t clock_control;
	uint8_t power_control;
	uint8_t host_control;
	uint16_t host_control2;
	uint32_t int_status;
	uint32_t int_enable;
	uint32_t core_generics;
	uint32_t pwrctl_status;
	uint32_t pwrctl_mask;
	uint32_t vendor_spec;
	uint32_t vendor_spec_caps0;
	uint32_t ddr_config;
	uint32_t dll_status;
	uint32_t gcc_bcr;
	uint32_t gcc_apps_cbcr;
	uint32_t gcc_ahb_cbcr;
	uint32_t gcc_ice_cbcr;
	uint32_t gcc_apps_cmd;
	uint32_t gcc_apps_cfg;
	uint32_t gcc_apps_m;
	uint32_t gcc_apps_n;
	uint32_t gcc_apps_d;

	if (gta4l_sdhci_handoff_audit_ran) {
		return;
	}
	if (!PE_parse_boot_argn("gta4l-sdhci-audit", &enabled,
	    sizeof(enabled)) || enabled != 1) {
		return;
	}
	gta4l_sdhci_handoff_audit_ran = 1;

	sdhci_mapping = ml_io_map(GTA4L_SDHCI_AUDIT_SDHCI_BASE,
	    GTA4L_SDHCI_AUDIT_SDHCI_SIZE);
	if (sdhci_mapping == 0) {
		panic("GTA4L-SDHCI AUDIT REFUSED: SDHCI map failed");
	}
	sdhci = (uintptr_t)sdhci_mapping;

	/*
	 * Read the version first. The v5 controller is the only layout this
	 * audit describes; an unexpected major version stops before the vendor
	 * register reads.
	 */
	mci_version = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_MCI_VERSION);
	if ((mci_version >> 28) != 1) {
		panic("GTA4L-SDHCI AUDIT REFUSED: unexpected MCI version %08x",
		    mci_version);
	}

	gcc_mapping = ml_io_map(GTA4L_SDHCI_AUDIT_GCC_BASE,
	    GTA4L_SDHCI_AUDIT_GCC_SIZE);
	if (gcc_mapping == 0) {
		panic("GTA4L-SDHCI AUDIT REFUSED: GCC map failed");
	}
	gcc = (uintptr_t)gcc_mapping;

	/* Read-only audit: every access below is an allowlisted load. */
	host_version = gta4l_sdhci_audit_read16(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_HOST_VERSION);
	capabilities = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_CAPABILITIES);
	capabilities_1 = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_CAPABILITIES_1);
	present_state = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_PRESENT_STATE);
	clock_control = gta4l_sdhci_audit_read16(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_CLOCK_CONTROL);
	power_control = gta4l_sdhci_audit_read8(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_POWER_CONTROL);
	host_control = gta4l_sdhci_audit_read8(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_HOST_CONTROL);
	host_control2 = gta4l_sdhci_audit_read16(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_HOST_CONTROL2);
	int_status = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_INT_STATUS);
	int_enable = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_INT_ENABLE);
	core_generics = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_CORE_GENERICS);
	pwrctl_status = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_CORE_PWRCTL_STATUS);
	pwrctl_mask = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_CORE_PWRCTL_MASK);
	vendor_spec = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_CORE_VENDOR_SPEC);
	vendor_spec_caps0 = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_CORE_VENDOR_SPEC_CAPS0);
	ddr_config = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_CORE_DDR_CONFIG);
	dll_status = gta4l_sdhci_audit_read32(sdhci,
	    GTA4L_SDHCI_AUDIT_REG_CORE_DLL_STATUS);
	gcc_bcr = gta4l_sdhci_audit_read32(gcc,
	    GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_BCR);
	gcc_apps_cbcr = gta4l_sdhci_audit_read32(gcc,
	    GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_CBCR);
	gcc_ahb_cbcr = gta4l_sdhci_audit_read32(gcc,
	    GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_AHB_CBCR);
	gcc_ice_cbcr = gta4l_sdhci_audit_read32(gcc,
	    GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_ICE_CBCR);
	gcc_apps_cmd = gta4l_sdhci_audit_read32(gcc,
	    GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_CMD);
	gcc_apps_cfg = gta4l_sdhci_audit_read32(gcc,
	    GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_CFG);
	gcc_apps_m = gta4l_sdhci_audit_read32(gcc,
	    GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_M);
	gcc_apps_n = gta4l_sdhci_audit_read32(gcc,
	    GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_N);
	gcc_apps_d = gta4l_sdhci_audit_read32(gcc,
	    GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_D);

	panic("GTA4L-SDHCI AUDIT OK v=%08x sv=%04x c=%08x c1=%08x "
	    "ps=%08x clk=%04x pwr=%02x hc=%02x hc2=%04x is=%08x ie=%08x\n"
	    "GTA4L-SDHCI AUDIT VENDOR g=%08x pst=%08x pm=%08x "
	    "vs=%08x vc=%08x ddr=%08x dll=%08x\n"
	    "GTA4L-SDHCI AUDIT GCC bcr=%08x ac=%08x ahb=%08x ice=%08x "
	    "cmd=%08x cfg=%08x m=%08x n=%08x d=%08x",
	    mci_version, (unsigned int)host_version, capabilities, capabilities_1,
	    present_state, (unsigned int)clock_control,
	    (unsigned int)power_control, (unsigned int)host_control,
	    (unsigned int)host_control2, int_status, int_enable,
	    core_generics, pwrctl_status, pwrctl_mask,
	    vendor_spec, vendor_spec_caps0, ddr_config, dll_status,
	    gcc_bcr, gcc_apps_cbcr, gcc_ahb_cbcr, gcc_ice_cbcr,
	    gcc_apps_cmd, gcc_apps_cfg, gcc_apps_m, gcc_apps_n, gcc_apps_d);
}
