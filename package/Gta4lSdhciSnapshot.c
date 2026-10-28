// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Observation-only SDHCI snapshot for the Samsung gta4l.
//
// The function is called once from bsd_init after bsd_bufferinit(). It does
// nothing unless the boot argument "gta4l-sdhci-snapshot=1" is present. When
// enabled it maps only the controller's 4 KiB host register window and reads
// the allowlisted registers in Gta4lSdhciSnapshot.h. It never writes a
// register, submits a command, touches the data buffer, changes a clock or
// power state, or accesses the card. A completed capture is reported with a
// clearly identified panic so the RAM-only diagnostic needs no persistent
// storage.

#include <sys/types.h>
#include <pexpert/pexpert.h>
#include <machine/machine_routines.h>
#include <kern/debug.h>

#include "Gta4lSdhciSnapshot.h"

void gta4l_sdhci_snapshot_run(void);
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

static int gta4l_sdhci_snapshot_ran;

static inline uint32_t
gta4l_sdhci_read32(uintptr_t base, uint32_t offset)
{
	return *(volatile uint32_t *)(base + offset);
}

static inline uint16_t
gta4l_sdhci_read16(uintptr_t base, uint32_t offset)
{
	return *(volatile uint16_t *)(base + offset);
}

static inline uint8_t
gta4l_sdhci_read8(uintptr_t base, uint32_t offset)
{
	return *(volatile uint8_t *)(base + offset);
}

void
gta4l_sdhci_snapshot_run(void)
{
	int enabled = 0;
	vm_offset_t mapping;
	uintptr_t base;
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

	if (gta4l_sdhci_snapshot_ran) {
		return;
	}
	if (!PE_parse_boot_argn("gta4l-sdhci-snapshot", &enabled,
	    sizeof(enabled)) || enabled != 1) {
		return;
	}
	gta4l_sdhci_snapshot_ran = 1;

	mapping = ml_io_map(GTA4L_SDHCI_SNAPSHOT_BASE,
	    GTA4L_SDHCI_SNAPSHOT_SIZE);
	if (mapping == 0) {
		panic("GTA4L-SDHCI SNAP REFUSED: map failed");
	}
	base = (uintptr_t)mapping;

	/* Read-only snapshot: every access below is an allowlisted load. */
	host_version = gta4l_sdhci_read16(base, GTA4L_SDHCI_REG_HOST_VERSION);
	mci_version = gta4l_sdhci_read32(base, GTA4L_SDHCI_REG_MCI_VERSION);
	capabilities = gta4l_sdhci_read32(base, GTA4L_SDHCI_REG_CAPABILITIES);
	capabilities_1 = gta4l_sdhci_read32(base, GTA4L_SDHCI_REG_CAPABILITIES_1);
	present_state = gta4l_sdhci_read32(base, GTA4L_SDHCI_REG_PRESENT_STATE);
	clock_control = gta4l_sdhci_read16(base, GTA4L_SDHCI_REG_CLOCK_CONTROL);
	power_control = gta4l_sdhci_read8(base, GTA4L_SDHCI_REG_POWER_CONTROL);
	host_control = gta4l_sdhci_read8(base, GTA4L_SDHCI_REG_HOST_CONTROL);
	host_control2 = gta4l_sdhci_read16(base, GTA4L_SDHCI_REG_HOST_CONTROL2);
	int_status = gta4l_sdhci_read32(base, GTA4L_SDHCI_REG_INT_STATUS);
	int_enable = gta4l_sdhci_read32(base, GTA4L_SDHCI_REG_INT_ENABLE);

	panic("GTA4L-SDHCI SNAP OK v=%08x sv=%04x c=%08x c1=%08x "
	    "ps=%08x clk=%04x pwr=%02x hc=%02x hc2=%04x is=%08x ie=%08x",
	    mci_version, (unsigned int)host_version, capabilities, capabilities_1,
	    present_state, (unsigned int)clock_control,
	    (unsigned int)power_control, (unsigned int)host_control,
	    (unsigned int)host_control2, int_status, int_enable);
}
