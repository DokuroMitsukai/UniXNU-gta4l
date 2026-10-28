// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// gta4l read-only SDHCI handoff audit.
//
// This header is the single source of the MMIO allowlist used by the audit
// probe and by scripts/test-gta4l-sdhci-handoff-audit.py. It records the
// standard SDHCI registers plus the v5 Qualcomm vendor registers and the
// SM6115 GCC SDCC1 clock/reset page. Every access is a load; there is no
// write, command, DMA, clock or power operation in this audit.
//
// The v5 vendor offsets are from the pinned Linux reference tree
// (drivers/mmc/host/sdhci-msm.c, sdhci_msm_v5_offset). The GCC offsets are
// from drivers/clk/qcom/gcc-sm6115.c. See docs/sdhci-handoff-audit.md.

#ifndef GTA4L_SDHCI_HANDOFF_AUDIT_H
#define GTA4L_SDHCI_HANDOFF_AUDIT_H

#define GTA4L_SDHCI_AUDIT_SDHCI_BASE       0x04744000u
#define GTA4L_SDHCI_AUDIT_SDHCI_SIZE       0x00001000u
#define GTA4L_SDHCI_AUDIT_GCC_BASE         0x01438000u
#define GTA4L_SDHCI_AUDIT_GCC_SIZE         0x00001000u

/* Standard SDHCI registers. */
#define GTA4L_SDHCI_AUDIT_REG_PRESENT_STATE          0x24u
#define GTA4L_SDHCI_AUDIT_REG_HOST_CONTROL           0x28u
#define GTA4L_SDHCI_AUDIT_REG_POWER_CONTROL          0x29u
#define GTA4L_SDHCI_AUDIT_REG_CLOCK_CONTROL          0x2Cu
#define GTA4L_SDHCI_AUDIT_REG_INT_STATUS             0x30u
#define GTA4L_SDHCI_AUDIT_REG_INT_ENABLE             0x34u
#define GTA4L_SDHCI_AUDIT_REG_HOST_CONTROL2          0x3Eu
#define GTA4L_SDHCI_AUDIT_REG_CAPABILITIES           0x40u
#define GTA4L_SDHCI_AUDIT_REG_CAPABILITIES_1         0x44u
#define GTA4L_SDHCI_AUDIT_REG_HOST_VERSION           0xFEu

/* Qualcomm v5 vendor registers. */
#define GTA4L_SDHCI_AUDIT_REG_MCI_VERSION            0x318u
#define GTA4L_SDHCI_AUDIT_REG_CORE_GENERICS          0x320u
#define GTA4L_SDHCI_AUDIT_REG_CORE_PWRCTL_STATUS     0x240u
#define GTA4L_SDHCI_AUDIT_REG_CORE_PWRCTL_MASK       0x244u
#define GTA4L_SDHCI_AUDIT_REG_CORE_VENDOR_SPEC       0x20Cu
#define GTA4L_SDHCI_AUDIT_REG_CORE_VENDOR_SPEC_CAPS0 0x21Cu
#define GTA4L_SDHCI_AUDIT_REG_CORE_DDR_CONFIG        0x25Cu
#define GTA4L_SDHCI_AUDIT_REG_CORE_DLL_STATUS        0x208u

/* SM6115 GCC SDCC1 page at 0x1400000 + 0x38000. The mapping starts at the
 * SDCC1 block, so these are page-relative offsets, not GCC-block offsets. */
#define GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_BCR          0x00u
#define GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_CBCR    0x04u
#define GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_AHB_CBCR     0x08u
#define GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_ICE_CBCR     0x0Cu
#define GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_CMD     0x28u
#define GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_CFG     0x2Cu
#define GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_M       0x30u
#define GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_N       0x34u
#define GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_APPS_D       0x38u

/* Widths: all vendor and GCC registers are 32-bit; these are the standard
 * SDHCI widths used by the snapshot probe. */
#define GTA4L_SDHCI_AUDIT_WIDTH_PRESENT_STATE          32u
#define GTA4L_SDHCI_AUDIT_WIDTH_HOST_CONTROL           8u
#define GTA4L_SDHCI_AUDIT_WIDTH_POWER_CONTROL          8u
#define GTA4L_SDHCI_AUDIT_WIDTH_CLOCK_CONTROL          16u
#define GTA4L_SDHCI_AUDIT_WIDTH_INT_STATUS             32u
#define GTA4L_SDHCI_AUDIT_WIDTH_INT_ENABLE             32u
#define GTA4L_SDHCI_AUDIT_WIDTH_HOST_CONTROL2          16u
#define GTA4L_SDHCI_AUDIT_WIDTH_CAPABILITIES           32u
#define GTA4L_SDHCI_AUDIT_WIDTH_CAPABILITIES_1         32u
#define GTA4L_SDHCI_AUDIT_WIDTH_HOST_VERSION           16u
#define GTA4L_SDHCI_AUDIT_WIDTH_MCI_VERSION            32u
#define GTA4L_SDHCI_AUDIT_WIDTH_CORE_GENERICS          32u
#define GTA4L_SDHCI_AUDIT_WIDTH_CORE_PWRCTL_STATUS     32u
#define GTA4L_SDHCI_AUDIT_WIDTH_CORE_PWRCTL_MASK       32u
#define GTA4L_SDHCI_AUDIT_WIDTH_CORE_VENDOR_SPEC       32u
#define GTA4L_SDHCI_AUDIT_WIDTH_CORE_VENDOR_SPEC_CAPS0 32u
#define GTA4L_SDHCI_AUDIT_WIDTH_CORE_DDR_CONFIG        32u
#define GTA4L_SDHCI_AUDIT_WIDTH_CORE_DLL_STATUS        32u
#define GTA4L_SDHCI_AUDIT_WIDTH_GCC_SDCC1_BCR          32u
#define GTA4L_SDHCI_AUDIT_WIDTH_GCC_SDCC1_APPS_CBCR    32u
#define GTA4L_SDHCI_AUDIT_WIDTH_GCC_SDCC1_AHB_CBCR     32u
#define GTA4L_SDHCI_AUDIT_WIDTH_GCC_SDCC1_ICE_CBCR     32u
#define GTA4L_SDHCI_AUDIT_WIDTH_GCC_SDCC1_APPS_CMD     32u
#define GTA4L_SDHCI_AUDIT_WIDTH_GCC_SDCC1_APPS_CFG     32u
#define GTA4L_SDHCI_AUDIT_WIDTH_GCC_SDCC1_APPS_M       32u
#define GTA4L_SDHCI_AUDIT_WIDTH_GCC_SDCC1_APPS_N       32u
#define GTA4L_SDHCI_AUDIT_WIDTH_GCC_SDCC1_APPS_D       32u

#endif /* GTA4L_SDHCI_HANDOFF_AUDIT_H */
