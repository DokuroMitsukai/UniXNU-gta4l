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
#define GTA4L_EMMC_REG_CORE_DLL_CONFIG     0x200u
#define GTA4L_EMMC_REG_CORE_DLL_STATUS     0x208u
#define GTA4L_EMMC_REG_CORE_VENDOR_SPEC    0x20Cu
#define GTA4L_EMMC_REG_CORE_VENDOR_SPEC3   0x250u
/* The v5 offset table uses 0x25c for controllers with minor >= 0x49
 * (updated_ddr_cfg in Linux sdhci-msm). gta4l reports minor 0x72. */
#define GTA4L_EMMC_REG_CORE_DDR_CONFIG     0x25Cu
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

/* Host Control 2 (0x3E). Preserve the reserved and vendor fields and change
 * only the UHS/tuning/1.8 V signalling fields. */
#define GTA4L_EMMC_HOST_CONTROL2_UHS_MASK  0x0007u
#define GTA4L_EMMC_HOST_CONTROL2_VDD_180   0x0008u
#define GTA4L_EMMC_HOST_CONTROL2_TUNING    0x0040u

/* Qualcomm v5 CORE_DLL_CONFIG bits (Linux sdhci-msm). At 100 MHz and below
 * the reference resets and powers down the DLL so the feedback clock is used
 * and tuning can be skipped. */
#define GTA4L_EMMC_DLL_PDN                  0x20000000u
#define GTA4L_EMMC_DLL_RST                  0x40000000u

/* Qualcomm vendor-spec pad power switch (CORE_VENDOR_SPEC, 0x20C). */
#define GTA4L_EMMC_VENDOR_PAD_PWR_SWITCH_EN 0x00008000u
#define GTA4L_EMMC_VENDOR_PAD_PWR_SWITCH    0x00010000u

/* CORE_VENDOR_SPEC power-on value used by Linux and the vendor UEFI. */
#define GTA4L_EMMC_VENDOR_SPEC_POR          0x00000A9Cu
#define GTA4L_EMMC_VENDOR3_PWRSAVE_DLL      0x00000008u

/* CORE_VENDOR_SPEC timing fields. Linux msm_hc_select_default() selects the
 * free-running MCLK, clears the HC_SELECT_IN override so HOST_CONTROL2
 * controls the mode, and keeps the card clock running (no clock power-save)
 * while the feedback clock is in use. */
#define GTA4L_EMMC_VENDOR_HC_MCLK_SEL_MASK  0x00000300u
#define GTA4L_EMMC_VENDOR_HC_MCLK_SEL_DFLT  0x00000200u
#define GTA4L_EMMC_VENDOR_HC_SELECT_IN_EN   0x00040000u
#define GTA4L_EMMC_VENDOR_HC_SELECT_IN_MASK 0x00380000u
#define GTA4L_EMMC_VENDOR_CLK_PWRSAVE       0x00000002u

/* Linux mmc_power_up() waits power_delay_ms (10 ms default) before enabling
 * the card clock; the post-clock requirement is at least 74 clocks or 1 ms,
 * and this core keeps 1 ms. Linux mmc_go_idle() delays 1 ms before CMD0,
 * 1 ms after it and another 1 ms after chip-select release. */
#define GTA4L_EMMC_POWER_SETTLE_US         10000u
#define GTA4L_EMMC_CARD_CLOCK_SETTLE_US    1000u
#define GTA4L_EMMC_CMD0_DELAY_US           1000u
#define GTA4L_EMMC_CMD0_POST_DELAY_US      2000u
#define GTA4L_EMMC_BCR_DELAY_US             200u

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
#define GTA4L_EMMC_CMD_RESP_TYPE_MASK      0x0003u
#define GTA4L_EMMC_CMD_DATA                0x0020u
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

/* 400 kHz: BI_TCXO * 2 / 24 * 1 / 4 (= BI_TCXO / 48). Source select 0,
 * divider field 23, M=1, N=0xFC, D=0xFB (pinned gcc-sm6115.c table). */
#define GTA4L_EMMC_RCG_400K_CFG            0x00002017u
#define GTA4L_EMMC_RCG_400K_M              0x00000001u
#define GTA4L_EMMC_RCG_400K_N              0x000000FCu
#define GTA4L_EMMC_RCG_400K_D              0x000000FBu

/* The UEFI-derived active SDC1 pad configuration. */
#define GTA4L_EMMC_SDC1_PAD_ACTIVE         0x00009FE4u

/* Poll bounds. The clock bounds follow Linux sdhci_enable_clk() (150 ms) and
 * sdhci_reset() (100 ms); the command and data bounds cover the slowest
 * legacy-timing response plus retries. */
#define GTA4L_EMMC_RESET_TIMEOUT_US        100000u
#define GTA4L_EMMC_CLOCK_TIMEOUT_US        150000u
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
#define GTA4L_EMMC_EXT_CSD_DATA_SECTOR_SIZE_OFF 61u
#define GTA4L_EMMC_EXT_CSD_PARTITION_CONFIG_OFF 179u
#define GTA4L_EMMC_EXT_CSD_SEC_COUNT_OFF   212u
#define GTA4L_EMMC_EXT_CSD_REV_OFF         192u
#define GTA4L_EMMC_EXT_CSD_PARTITION_ACCESS_MASK 0x07u
/* eMMC devices above 2 GiB are sector addressed (JEDEC; Linux sets
 * MMC_STATE_BLOCKADDR when SEC_COUNT > 2 GiB / 512). */
#define GTA4L_EMMC_SECTOR_ADDRESS_MIN_BLOCKS 4194304u
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

/* Protocol progress recorded for diagnostics. The value is the last step
 * that completed successfully, not the step that was in progress. */
#define GTA4L_EMMC_STEP_NONE               0u
#define GTA4L_EMMC_STEP_CONTROLLER_RESET   1u
#define GTA4L_EMMC_STEP_VENDOR_CONFIG      2u
#define GTA4L_EMMC_STEP_CLOCK              3u
#define GTA4L_EMMC_STEP_POWER              4u
#define GTA4L_EMMC_STEP_PADS               5u
#define GTA4L_EMMC_STEP_CARD_CLOCK         6u
#define GTA4L_EMMC_STEP_CMD0               7u
#define GTA4L_EMMC_STEP_CMD1               8u
#define GTA4L_EMMC_STEP_CMD2               9u
#define GTA4L_EMMC_STEP_CMD3              10u
#define GTA4L_EMMC_STEP_CMD9              11u
#define GTA4L_EMMC_STEP_CMD7              12u
#define GTA4L_EMMC_STEP_TRAN              13u
#define GTA4L_EMMC_STEP_EXT_CSD           14u
#define GTA4L_EMMC_STEP_READY             15u

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
	uint8_t  partition_config;
	uint8_t  data_sector_size_4k;
	uint8_t  fixed_1v8;      /* caller: board VCCQ is a fixed 1.8 V rail */
	uint8_t  pad_pwr_switch; /* caller: select the 1.8 V pad supply */
	uint8_t  initialized;
	uint8_t  clocks_enabled; /* AHB/APPS branches enabled after reset */
	uint32_t last_step;      /* last protocol step that completed */
	uint32_t fail_index;     /* terminal failing command, diagnostics only */
	uint32_t fail_argument;
	uint32_t fail_command;   /* encoded SDHCI command register value */
	uint32_t fail_error;     /* raw error interrupt status before clear */
	uint32_t fail_normal;    /* raw normal interrupt status before clear */
	uint32_t fail_present;   /* raw present state before clear */
	uint32_t fail_response[4]; /* raw RESPONSE0..3 before clear */
	uint32_t fail_step;      /* last_step when the failure was recorded */
	uint32_t cmd1_attempts;  /* CMD1 attempts in the last card_ready call */
	uint32_t cmd1_timeouts;  /* CMD1 attempts that returned a timeout */
	uint8_t  fail_valid;
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
