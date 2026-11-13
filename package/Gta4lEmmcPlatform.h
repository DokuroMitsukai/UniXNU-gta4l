// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// gta4l eMMC platform access.
//
// The platform object owns the three MMIO windows the eMMC diagnostic needs,
// the width-specific register accessors, the barrier and the generic-counter
// timing helpers. It is board code, not part of the portable eMMC core: the
// core only sees the gta4l_emmc_ops callbacks.
#ifndef GTA4L_EMMC_PLATFORM_H
#define GTA4L_EMMC_PLATFORM_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GTA4L_EMMC_PLATFORM_SDHCI_BASE 0x04744000u
#define GTA4L_EMMC_PLATFORM_GCC_BASE   0x01438000u
#define GTA4L_EMMC_PLATFORM_TLMM_BASE  0x00575000u
#define GTA4L_EMMC_PLATFORM_SMMU_BASE  0x0c600000u
#define GTA4L_EMMC_PLATFORM_WINDOW     0x00001000u

struct gta4l_emmc_platform {
	uintptr_t sdhci;
	uintptr_t gcc;
	uintptr_t tlmm;
	uintptr_t smmu;
	uint64_t counter_hz;
};

/* Map the SDHCI, GCC and TLMM windows and sample the counter frequency.
 * Returns 0 when all three windows mapped, -1 when any mapping is absent;
 * the address fields are zero for the windows that failed. */
int gta4l_emmc_platform_map(struct gta4l_emmc_platform *platform);

/* Map the SMMU global page separately. The probe and the non-DMA block
 * paths never call this, so an absent or restricted SMMU cannot fail the
 * eMMC bring-up; the bulk gate treats a failure as a skip reason. */
int gta4l_emmc_platform_map_smmu(struct gta4l_emmc_platform *platform);
uint32_t gta4l_emmc_platform_smmu_read32(
    const struct gta4l_emmc_platform *platform, uint32_t offset);

uint64_t gta4l_emmc_platform_counter(void);
uint64_t gta4l_emmc_platform_counter_hz(void);

uint8_t gta4l_emmc_platform_read8(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset);
uint16_t gta4l_emmc_platform_read16(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset);
uint32_t gta4l_emmc_platform_read32(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset);
void gta4l_emmc_platform_write8(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint8_t value);
void gta4l_emmc_platform_write16(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint16_t value);
void gta4l_emmc_platform_write32(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint32_t value);

void gta4l_emmc_platform_barrier(void);
void gta4l_emmc_platform_delay_us(struct gta4l_emmc_platform *platform,
    uint32_t microseconds);

#ifdef __cplusplus
}
#endif

#endif /* GTA4L_EMMC_PLATFORM_H */
