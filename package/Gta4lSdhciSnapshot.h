// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// gta4l observation-only SDHCI snapshot.
//
// This header is the single source of the MMIO allowlist used by the probe and
// by scripts/test-gta4l-sdhci-snapshot.py. Standard offsets and widths come
// from the SDHCI register definitions; the vendor core-version register uses
// the v5 sdhci-msm layout. The legacy separate-MCI version offset (0x50) is
// deliberately not used. See docs/sdhci-snapshot.md for sources and
// side-effect classification.

#ifndef GTA4L_SDHCI_SNAPSHOT_H
#define GTA4L_SDHCI_SNAPSHOT_H

#define GTA4L_SDHCI_SNAPSHOT_BASE       0x04744000u
#define GTA4L_SDHCI_SNAPSHOT_SIZE       0x00001000u

#define GTA4L_SDHCI_REG_PRESENT_STATE   0x24u
#define GTA4L_SDHCI_REG_HOST_CONTROL    0x28u
#define GTA4L_SDHCI_REG_POWER_CONTROL   0x29u
#define GTA4L_SDHCI_REG_CLOCK_CONTROL   0x2Cu
#define GTA4L_SDHCI_REG_INT_STATUS      0x30u
#define GTA4L_SDHCI_REG_INT_ENABLE      0x34u
#define GTA4L_SDHCI_REG_HOST_CONTROL2   0x3Eu
#define GTA4L_SDHCI_REG_CAPABILITIES    0x40u
#define GTA4L_SDHCI_REG_CAPABILITIES_1  0x44u
#define GTA4L_SDHCI_REG_MCI_VERSION     0x318u
#define GTA4L_SDHCI_REG_HOST_VERSION    0xFEu

#define GTA4L_SDHCI_WIDTH_PRESENT_STATE  32u
#define GTA4L_SDHCI_WIDTH_HOST_CONTROL   8u
#define GTA4L_SDHCI_WIDTH_POWER_CONTROL  8u
#define GTA4L_SDHCI_WIDTH_CLOCK_CONTROL  16u
#define GTA4L_SDHCI_WIDTH_INT_STATUS     32u
#define GTA4L_SDHCI_WIDTH_INT_ENABLE     32u
#define GTA4L_SDHCI_WIDTH_HOST_CONTROL2  16u
#define GTA4L_SDHCI_WIDTH_CAPABILITIES   32u
#define GTA4L_SDHCI_WIDTH_CAPABILITIES_1 32u
#define GTA4L_SDHCI_WIDTH_MCI_VERSION    32u
#define GTA4L_SDHCI_WIDTH_HOST_VERSION   16u

#endif /* GTA4L_SDHCI_SNAPSHOT_H */
