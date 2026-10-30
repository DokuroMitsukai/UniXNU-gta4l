// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// Portable gta4l SDHCI v5 / eMMC core.
//
// The core performs controller reset, GCC identification-clock setup, the
// retained-rail power handshake, eMMC enumeration and single-block PIO reads.
// It has no XNU or libc dependency: the caller supplies MMIO, delay and
// diagnostic callbacks. The host simulator and the XNU probe compile the same
// source.
//
// Scope: one-bit bus, legacy timing, polling, single 512-byte block reads. No
// DMA, CQE, ICE, tuning, high-speed mode or card writes.

#ifndef GTA4L_EMMC_H
#define GTA4L_EMMC_H

/* MMIO spaces. The caller maps each base and implements the offset. */
#define GTA4L_EMMC_SPACE_SDHCI 0u
#define GTA4L_EMMC_SPACE_GCC   1u
#define GTA4L_EMMC_SPACE_TLMM  2u

/* SDHCI standard registers. */
#define GTA4L_EMMC_REG_BLOCK_SIZE          0x04u
#define GTA4L_EMMC_REG_BLOCK_COUNT         0x06u
#define GTA4L_EMMC_REG_ARGUMENT            0x08u
#define GTA4L_EMMC_REG_TRANSFER_MODE       0x0Cu
#define GTA4L_EMMC_REG_COMMAND             0x0Eu
#define GTA4L_EMMC_REG_RESPONSE            0x10u
#define GTA4L_EMMC_REG_BUFFER_DATA_PORT    0x20u
#define GTA4L_EMMC_REG_PRESENT_STATE       0x24u
#define GTA4L_EMMC_REG_HOST_CONTROL        0x28u
#define GTA4L_EMMC_REG_POWER_CONTROL       0x29u
#define GTA4L_EMMC_REG_CLOCK_CONTROL       0x2Cu
#define GTA4L_EMMC_REG_TIMEOUT_CONTROL     0x2Eu
#define GTA4L_EMMC_REG_SOFTWARE_RESET      0x2Fu
#define GTA4L_EMMC_REG_NORMAL_INT_STATUS   0x30u
#define GTA4L_EMMC_REG_ERROR_INT_STATUS    0x32u
#define GTA4L_EMMC_REG_NORMAL_INT_ENABLE   0x34u
#define GTA4L_EMMC_REG_ERROR_INT_ENABLE    0x36u
#define GTA4L_EMMC_REG_NORMAL_INT_SIGNAL   0x38u
#define GTA4L_EMMC_REG_ERROR_INT_SIGNAL    0x3Au
#define GTA4L_EMMC_REG_HOST_CONTROL2       0x3Eu
#define GTA4L_EMMC_REG_CAPABILITIES        0x40u

/* Qualcomm v5 vendor registers. */
#define GTA4L_EMMC_REG_MCI_VERSION         0x318u
#define GTA4L_EMMC_REG_CORE_VENDOR_SPEC    0x20Cu
#define GTA4L_EMMC_REG_CORE_PWRCTL_STATUS  0x240u
#define GTA4L_EMMC_REG_CORE_PWRCTL_MASK    0x244u
#define GTA4L_EMMC_REG_CORE_PWRCTL_CLEAR   0x248u
#define GTA4L_EMMC_REG_CORE_PWRCTL_CTL     0x24Cu

/* SM6115 GCC SDCC1 page, relative to 0x01438000. */
#define GTA4L_EMMC_GCC_SDCC1_BCR           0x00u
#define GTA4L_EMMC_GCC_SDCC1_APPS_CBCR     0x04u
#define GTA4L_EMMC_GCC_SDCC1_AHB_CBCR      0x08u
#define GTA4L_EMMC_GCC_SDCC1_ICE_CBCR      0x0Cu
#define GTA4L_EMMC_GCC_SDCC1_APPS_CMD      0x28u
#define GTA4L_EMMC_GCC_SDCC1_APPS_CFG      0x2Cu
#define GTA4L_EMMC_GCC_SDCC1_APPS_M        0x30u
#define GTA4L_EMMC_GCC_SDCC1_APPS_N        0x34u
#define GTA4L_EMMC_GCC_SDCC1_APPS_D        0x38u

/* SDC1 QDSD pad group, relative to the TLMM base 0x00575000. */
#define GTA4L_EMMC_TLMM_SDC1_QDSD          0x00u

/* Standard SDHCI fields. */
#define GTA4L_EMMC_POWER_BUS_POWER         0x01u
#define GTA4L_EMMC_POWER_3V3               0x0Eu
#define GTA4L_EMMC_CLOCK_INT_EN            0x0001u
#define GTA4L_EMMC_CLOCK_INT_STABLE        0x0002u
#define GTA4L_EMMC_CLOCK_CARD_EN           0x0004u
#define GTA4L_EMMC_RESET_ALL               0x01u
#define GTA4L_EMMC_TIMEOUT_MAX             0x0Eu
#define GTA4L_EMMC_PRESENT_CMD_INHIBIT     0x00000001u
#define GTA4L_EMMC_PRESENT_DAT_INHIBIT     0x00000002u

#define GTA4L_EMMC_INT_CC                  0x0001u
#define GTA4L_EMMC_INT_TC                  0x0002u
#define GTA4L_EMMC_INT_BRR                 0x0020u

#define GTA4L_EMMC_INT_ERR_CMD_TIMEOUT     0x0001u
#define GTA4L_EMMC_INT_ERR_CMD_CRC         0x0002u
#define GTA4L_EMMC_INT_ERR_CMD_END         0x0004u
#define GTA4L_EMMC_INT_ERR_CMD_INDEX       0x0008u
#define GTA4L_EMMC_INT_ERR_DATA_TIMEOUT    0x0010u
#define GTA4L_EMMC_INT_ERR_DATA_CRC        0x0020u
#define GTA4L_EMMC_INT_ERR_DATA_END        0x0040u
#define GTA4L_EMMC_INT_ERR_CURRENT_LIMIT   0x0080u
#define GTA4L_EMMC_INT_ERR_AUTO_CMD12      0x0100u
#define GTA4L_EMMC_INT_ERR_ADMA            0x0200u
#define GTA4L_EMMC_INT_ERR_TUNING          0x0400u
#define GTA4L_EMMC_INT_ERR_MASK            (GTA4L_EMMC_INT_ERR_CMD_TIMEOUT | \
    GTA4L_EMMC_INT_ERR_CMD_CRC | GTA4L_EMMC_INT_ERR_CMD_END | \
    GTA4L_EMMC_INT_ERR_CMD_INDEX | GTA4L_EMMC_INT_ERR_DATA_TIMEOUT | \
    GTA4L_EMMC_INT_ERR_DATA_CRC | GTA4L_EMMC_INT_ERR_DATA_END | \
    GTA4L_EMMC_INT_ERR_CURRENT_LIMIT | GTA4L_EMMC_INT_ERR_AUTO_CMD12 | \
    GTA4L_EMMC_INT_ERR_ADMA | GTA4L_EMMC_INT_ERR_TUNING)

/* Command-register encoding. */
#define GTA4L_EMMC_CMD_RESP_NONE           0x0000u
#define GTA4L_EMMC_CMD_RESP_LONG           0x0001u
#define GTA4L_EMMC_CMD_RESP_SHORT          0x0002u
#define GTA4L_EMMC_CMD_RESP_SHORT_BUSY     0x0003u
#define GTA4L_EMMC_CMD_DATA                0x0004u
#define GTA4L_EMMC_CMD_CRC                 0x0008u
#define GTA4L_EMMC_CMD_INDEX               0x0010u

/* Transfer-mode fields. */
#define GTA4L_EMMC_TM_BLOCK_COUNT_EN       0x0002u
#define GTA4L_EMMC_TM_READ                 0x0010u

/* Qualcomm power-control bits. */
#define GTA4L_EMMC_PWRCTL_BUS_OFF          0x01u
#define GTA4L_EMMC_PWRCTL_BUS_ON           0x02u
#define GTA4L_EMMC_PWRCTL_IO_LOW           0x04u
#define GTA4L_EMMC_PWRCTL_IO_HIGH          0x08u
#define GTA4L_EMMC_PWRCTL_ACK_BUS          0x01u
#define GTA4L_EMMC_PWRCTL_ACK_IO           0x04u

/* GCC fields. */
#define GTA4L_EMMC_CBCR_CLK_OFF            0x80000000u
#define GTA4L_EMMC_CBCR_CLOCK_ENABLE       0x00000001u
#define GTA4L_EMMC_BCR_BLOCK_RESET         0x00000001u
#define GTA4L_EMMC_RCG_CMD_UPDATE          0x00000001u
#define GTA4L_EMMC_RCG_CMD_ROOT_OFF        0x80000000u
#define GTA4L_EMMC_RCG_SRC_SEL_MASK        0x00000700u
#define GTA4L_EMMC_RCG_SRC_DIV_MASK        0x000000FFu
#define GTA4L_EMMC_RCG_MODE_MASK           0x00003000u
#define GTA4L_EMMC_RCG_MODE_DUAL_EDGE      0x00002000u

/* 400 kHz: BI_TCXO / 24 * 1 / 4. The source select is 0, the divider field
 * 23, M=1, N=0xFC and D=0xFB (see the pinned gcc-sm6115.c frequency table). */
#define GTA4L_EMMC_RCG_400K_CFG            0x00002017u
#define GTA4L_EMMC_RCG_400K_M              0x00000001u
#define GTA4L_EMMC_RCG_400K_N              0x000000FCu
#define GTA4L_EMMC_RCG_400K_D              0x000000FBu

/* The UEFI-derived active SDC1 pad configuration. */
#define GTA4L_EMMC_SDC1_PAD_ACTIVE         0x00009FE4u

/* Poll bounds. */
#define GTA4L_EMMC_RESET_TIMEOUT_US        100000u
#define GTA4L_EMMC_CLOCK_TIMEOUT_US        100000u
#define GTA4L_EMMC_POWER_TIMEOUT_US        500000u
#define GTA4L_EMMC_CMD_TIMEOUT_US          1000000u
#define GTA4L_EMMC_CMD1_ATTEMPT_US         100000u
#define GTA4L_EMMC_CMD13_ATTEMPT_US        100000u
#define GTA4L_EMMC_DATA_TIMEOUT_US         1000000u
#define GTA4L_EMMC_CMD1_TIMEOUT_US         2000000u
#define GTA4L_EMMC_RCG_TIMEOUT_US          100000u
#define GTA4L_EMMC_POLL_STEP_US            10u
#define GTA4L_EMMC_MAX_POLL_ITERATIONS     100000u

/* MMC command indexes. */
#define GTA4L_EMMC_CMD0                    0u
#define GTA4L_EMMC_CMD1                    1u
#define GTA4L_EMMC_CMD2                    2u
#define GTA4L_EMMC_CMD3                    3u
#define GTA4L_EMMC_CMD7                    7u
#define GTA4L_EMMC_CMD8                    8u
#define GTA4L_EMMC_CMD9                    9u
#define GTA4L_EMMC_CMD13                   13u
#define GTA4L_EMMC_CMD17                   17u

#define GTA4L_EMMC_BLOCK_BYTES             512u
#define GTA4L_EMMC_OCR_VALUE               0x40FF8080u
#define GTA4L_EMMC_DEFAULT_RCA             1u
#define GTA4L_EMMC_EXT_CSD_BYTES           512u
#define GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF   212u
#define GTA4L_EMMC_EXT_CSD_REV_OFF         192u
#define GTA4L_EMMC_R1_STATE_MASK           0x00001E00u
#define GTA4L_EMMC_R1_STATE_SHIFT          9u
#define GTA4L_EMMC_R1_STATE_TRAN           4u
#define GTA4L_EMMC_R1_READY_FOR_DATA       0x00000100u
#define GTA4L_EMMC_R1_ERROR_MASK           0xFDF9A080u

/* Result codes. */
#define GTA4L_EMMC_OK                      0
#define GTA4L_EMMC_ERR_ARG                -1
#define GTA4L_EMMC_ERR_IO                 -2
#define GTA4L_EMMC_ERR_VERSION            -3
#define GTA4L_EMMC_ERR_RESET              -4
#define GTA4L_EMMC_ERR_CLOCK              -5
#define GTA4L_EMMC_ERR_POWER              -6
#define GTA4L_EMMC_ERR_CMD_TIMEOUT        -7
#define GTA4L_EMMC_ERR_CMD_CRC            -8
#define GTA4L_EMMC_ERR_CMD_INDEX          -9
#define GTA4L_EMMC_ERR_CMD_OTHER         -10
#define GTA4L_EMMC_ERR_DATA_TIMEOUT      -11
#define GTA4L_EMMC_ERR_DATA_CRC          -12
#define GTA4L_EMMC_ERR_DATA_OTHER        -13
#define GTA4L_EMMC_ERR_CARD_NOT_READY    -14
#define GTA4L_EMMC_ERR_CARD_STATUS       -15
#define GTA4L_EMMC_ERR_RESPONSE          -16
#define GTA4L_EMMC_ERR_INHIBIT           -17
#define GTA4L_EMMC_ERR_UNSUPPORTED       -18

/* Diagnostic event codes passed to the log callback. */
#define GTA4L_EMMC_LOG_RESET              1u
#define GTA4L_EMMC_LOG_CLOCK              2u
#define GTA4L_EMMC_LOG_POWER              3u
#define GTA4L_EMMC_LOG_PAD                4u
#define GTA4L_EMMC_LOG_CMD                5u
#define GTA4L_EMMC_LOG_READ               6u
#define GTA4L_EMMC_LOG_CARD               7u
#define GTA4L_EMMC_LOG_ERROR              8u

struct gta4l_emmc_ops {
	uint8_t  (*read8)(void *context, uint32_t space, uint32_t offset);
	uint16_t (*read16)(void *context, uint32_t space, uint32_t offset);
	uint32_t (*read32)(void *context, uint32_t space, uint32_t offset);
	void     (*write8)(void *context, uint32_t space, uint32_t offset,
	    uint8_t value);
	void     (*write16)(void *context, uint32_t space, uint32_t offset,
	    uint16_t value);
	void     (*write32)(void *context, uint32_t space, uint32_t offset,
	    uint32_t value);
	void     (*barrier)(void *context);
	void     (*delay_us)(void *context, uint32_t microseconds);
	void     (*log)(void *context, uint32_t event, uint32_t a, uint32_t b);
};

struct gta4l_emmc_host {
	const struct gta4l_emmc_ops *ops;
	void *context;
	uint32_t rca;
	uint32_t ocr;
	uint32_t cid[4];
	uint32_t csd[4];
	uint32_t card_status;
	uint32_t capacity_blocks;
	uint32_t ext_csd_sum;
	uint32_t mci_version;
	uint8_t  ext_csd_rev;
	uint8_t  bus_width;
	uint8_t  initialized;
};

/* Controller reset, GCC 400 kHz setup, retained-rail power handshake, pads. */
int gta4l_emmc_controller_init(struct gta4l_emmc_host *host);

/* Full controller plus card initialization (CMD0/1/2/3/9/7/13/8). */
int gta4l_emmc_init(struct gta4l_emmc_host *host);

/* One 512-byte PIO read. `block` is a logical 512-byte block address. */
int gta4l_emmc_read_block(struct gta4l_emmc_host *host, uint64_t block,
    void *buffer);

/* Read the 512-byte EXT_CSD into `buffer`. */
int gta4l_emmc_read_ext_csd(struct gta4l_emmc_host *host, void *buffer);

#endif /* GTA4L_EMMC_H */
