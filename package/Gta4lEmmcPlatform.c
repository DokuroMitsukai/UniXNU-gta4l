// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// gta4l eMMC platform access.
//
// The mapping, register accesses, barrier and generic-counter delay preserve
// the probe's original behaviour exactly; the probe keeps only the
// gta4l_emmc_ops adapters and the diagnostic log.

#include <sys/types.h>
#include <machine/machine_routines.h>
#include <kern/debug.h>

#include "Gta4lEmmc.h"
#include "Gta4lEmmcPlatform.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

int
gta4l_emmc_platform_map(struct gta4l_emmc_platform *platform)
{
	platform->sdhci = (uintptr_t)ml_io_map(GTA4L_EMMC_PLATFORM_SDHCI_BASE,
	    GTA4L_EMMC_PLATFORM_WINDOW);
	platform->gcc = (uintptr_t)ml_io_map(GTA4L_EMMC_PLATFORM_GCC_BASE,
	    GTA4L_EMMC_PLATFORM_WINDOW);
	platform->tlmm = (uintptr_t)ml_io_map(GTA4L_EMMC_PLATFORM_TLMM_BASE,
	    GTA4L_EMMC_PLATFORM_WINDOW);
	if ((platform->sdhci == 0) || (platform->gcc == 0) ||
	    (platform->tlmm == 0)) {
		return -1;
	}
	platform->counter_hz = gta4l_emmc_platform_counter_hz();
	return 0;
}

uint64_t
gta4l_emmc_platform_counter(void)
{
	uint64_t value;

	__asm__ volatile("mrs %0, cntvct_el0" : "=r" (value));
	return value;
}

uint64_t
gta4l_emmc_platform_counter_hz(void)
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

uint8_t
gta4l_emmc_platform_read8(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return *(volatile uint8_t *)(platform->sdhci + offset);
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return *(volatile uint8_t *)(platform->gcc + offset);
	}
	return *(volatile uint8_t *)(platform->tlmm + offset);
}

uint16_t
gta4l_emmc_platform_read16(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return *(volatile uint16_t *)(platform->sdhci + offset);
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return *(volatile uint16_t *)(platform->gcc + offset);
	}
	return *(volatile uint16_t *)(platform->tlmm + offset);
}

uint32_t
gta4l_emmc_platform_read32(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		return *(volatile uint32_t *)(platform->sdhci + offset);
	}
	if (space == GTA4L_EMMC_SPACE_GCC) {
		return *(volatile uint32_t *)(platform->gcc + offset);
	}
	return *(volatile uint32_t *)(platform->tlmm + offset);
}

void
gta4l_emmc_platform_write8(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint8_t value)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		*(volatile uint8_t *)(platform->sdhci + offset) = value;
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		*(volatile uint8_t *)(platform->gcc + offset) = value;
	} else {
		*(volatile uint8_t *)(platform->tlmm + offset) = value;
	}
}

void
gta4l_emmc_platform_write16(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint16_t value)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		*(volatile uint16_t *)(platform->sdhci + offset) = value;
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		*(volatile uint16_t *)(platform->gcc + offset) = value;
	} else {
		*(volatile uint16_t *)(platform->tlmm + offset) = value;
	}
}

void
gta4l_emmc_platform_write32(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint32_t value)
{
	if (space == GTA4L_EMMC_SPACE_SDHCI) {
		*(volatile uint32_t *)(platform->sdhci + offset) = value;
	} else if (space == GTA4L_EMMC_SPACE_GCC) {
		*(volatile uint32_t *)(platform->gcc + offset) = value;
	} else {
		*(volatile uint32_t *)(platform->tlmm + offset) = value;
	}
}

void
gta4l_emmc_platform_barrier(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
}

void
gta4l_emmc_platform_delay_us(struct gta4l_emmc_platform *platform,
    uint32_t microseconds)
{
	uint64_t start;
	uint64_t ticks;
	uint64_t spins = 0;
	uint64_t limit;
	uint64_t hz = platform->counter_hz;

	if (hz == 0u) {
		hz = 19200000u;
	}
	ticks = (((uint64_t)microseconds * hz) + 999999u) / 1000000u;
	limit = ticks + 1000000u;
	start = gta4l_emmc_platform_counter();
	while ((gta4l_emmc_platform_counter() - start) < ticks) {
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
