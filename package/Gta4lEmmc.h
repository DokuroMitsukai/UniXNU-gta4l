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
// Scope: one-bit base bus, legacy timing, polling, single-block and open-ended
// multi-block 512-byte PIO reads with an explicit CMD12 stop, plus an opt-in
// 4/8-bit BUS_WIDTH switch (CMD6) and CMD19/CMD14 bus test. No DMA, CQE, ICE,
// tuning, high-speed mode or block-data writes.

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
#define GTA4L_EMMC_REG_HOST_VERSION        0xFEu

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
#define GTA4L_EMMC_RESET_DATA              0x04u
#define GTA4L_EMMC_TIMEOUT_MAX             0x0Eu
#define GTA4L_EMMC_PRESENT_CMD_INHIBIT     0x00000001u
#define GTA4L_EMMC_PRESENT_DAT_INHIBIT     0x00000002u
/* Live FIFO readiness, not the latched interrupt bit (Linux
 * SDHCI_DATA_AVAILABLE, sdhci.h:89). A read must check this bit before
 * draining the buffer port; BRR can stay latched after the FIFO is empty. */
#define GTA4L_EMMC_PRESENT_BUFFER_READ_READY 0x00000800u

/* Host Control 2 (0x3E). Preserve the reserved and vendor fields and change
 * only the UHS/tuning/1.8 V signalling fields. */
#define GTA4L_EMMC_HOST_CONTROL2_UHS_MASK  0x0007u
#define GTA4L_EMMC_HOST_CONTROL2_VDD_180   0x0008u
#define GTA4L_EMMC_HOST_CONTROL2_TUNING    0x0040u

/* Host Control (0x28) bus-width bits (Linux sdhci_set_bus_width()). */
#define GTA4L_EMMC_HOST_CONTROL_4BIT       0x02u
#define GTA4L_EMMC_HOST_CONTROL_8BIT       0x20u

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

/* Named timing candidate: downstream mmc_send_op_cond() waits 8 us after a
 * successful ready response before the next command because the internal CRC
 * status can take three MCLK cycles (7.5 us at 400 kHz) to clear. This is a
 * hypothesis for the CMD2 CRC failure, not a confirmed hardware requirement. */
#define GTA4L_EMMC_CMD1_CRC_RETIRE_US          8u

#define GTA4L_EMMC_INT_CC                  0x0001u
#define GTA4L_EMMC_INT_TC                  0x0002u
/* Buffer Write Ready (SDHCI_SPACE_AVAIL). The bus-test write path polls it
 * before filling the buffer port. */
#define GTA4L_EMMC_INT_BWR                 0x0010u
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
#define GTA4L_EMMC_TM_MULTI                0x0020u

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

/* 50 MHz direct-divided row used for the 52 MHz HS class: pinned
 * `F(50000000, P_GPLL0_OUT_AUX2, 6, 0, 0)` with clk_rcg2_floor_ops, so a
 * 52 MHz request floors to 50 MHz on this clock tree. Source select 2,
 * divider field 11, M/N/D zero (direct mode). The parent rate is a table
 * value, not a measurement. */
#define GTA4L_EMMC_RCG_50M_CFG             0x0000020Bu
#define GTA4L_EMMC_RCG_50M_M               0x00000000u
#define GTA4L_EMMC_RCG_50M_N               0x00000000u
#define GTA4L_EMMC_RCG_50M_D               0x00000000u

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
/* One bounded secondary window after an error-free data-bound expiry: ready
 * blocks are drained and TC is re-checked. It is reported separately so an
 * exhausted window stays distinguishable from the primary bound. */
#define GTA4L_EMMC_DATA_DRAIN_US           500000u
#define GTA4L_EMMC_CMD1_TIMEOUT_US         2000000u
#define GTA4L_EMMC_RCG_TIMEOUT_US          100000u
#define GTA4L_EMMC_POLL_STEP_US            10u
#define GTA4L_EMMC_MAX_POLL_ITERATIONS     100000u
/* The probe's data-timeout audit extends only the CMD19 bus-test write poll
 * to 8 s (800,000 iterations at the 10 us step), so a late controller
 * timeout can still latch. Every other poll keeps the universal cap. */
#define GTA4L_EMMC_MAX_BUS_TEST_ITERATIONS 800000u

/* Diagnostic capture format reported by the probe. Bump the value when the
 * meaning or order of the capture fields changes. */
#define GTA4L_EMMC_CAPTURE_FMT             4u
/* Bounded CMD7 status-transition trace. Each entry records an observed
 * change; further changes increment the overflow counter. */
#define GTA4L_EMMC_CMD7_TRACE_ENTRIES      4u
/* The H9 diagnostic allows exactly one duplicate CMD3 after a completion
 * timeout. Two attempt slots keep the first record intact for the report. */
#define GTA4L_EMMC_CMD3_ATTEMPTS           2u

/* MMC command indexes. */
#define GTA4L_EMMC_CMD0                    0u
#define GTA4L_EMMC_CMD1                    1u
#define GTA4L_EMMC_CMD2                    2u
#define GTA4L_EMMC_CMD3                    3u
#define GTA4L_EMMC_CMD6                    6u
#define GTA4L_EMMC_CMD7                    7u
#define GTA4L_EMMC_CMD8                    8u
#define GTA4L_EMMC_CMD9                    9u
#define GTA4L_EMMC_CMD12                   12u
#define GTA4L_EMMC_CMD13                   13u
#define GTA4L_EMMC_CMD14                   14u
#define GTA4L_EMMC_CMD17                   17u
#define GTA4L_EMMC_CMD18                   18u
#define GTA4L_EMMC_CMD19                   19u

#define GTA4L_EMMC_BLOCK_BYTES             512u
#define GTA4L_EMMC_OCR_VALUE               0x40FF8080u
#define GTA4L_EMMC_DEFAULT_RCA             1u
#define GTA4L_EMMC_EXT_CSD_BYTES           512u

/* EXT_CSD BUS_WIDTH (byte 183; JEDEC 6.6.94, Linux EXT_CSD_BUS_WIDTH).
 * CMD6 writes it with Access=0x03 (write byte) and Cmd Set=1 (normal). */
#define GTA4L_EMMC_EXT_CSD_BUS_WIDTH_OFF   183u
#define GTA4L_EMMC_EXT_CSD_BUS_WIDTH_1     0u
#define GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4     1u
#define GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8     2u
/* JESD84-B51 7.4.67 Table 145: values 5 and 6 are 4-bit and 8-bit DDR;
 * values 3 and 4 are reserved. BUS_WIDTH is W/E_P and is NOT readable, so
 * a readback of the field cannot verify a switch. Linux instead verifies
 * with a bus test or by comparing read-only EXT_CSD fields
 * (mmc_select_bus_width()). */
#define GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4_DDR 5u
#define GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8_DDR 6u
#define GTA4L_EMMC_EXT_CSD_BUS_WIDTH_RESERVED_4 4u
/* The properties segment (bytes 192..511) is read-only and is the portable
 * substitute for a byte-183 readback. */
#define GTA4L_EMMC_EXT_CSD_PROPERTIES_OFF  192u
#define GTA4L_EMMC_SWITCH_ACCESS_WRITE     0x03000000u
#define GTA4L_EMMC_SWITCH_CMD_SET_NORMAL   0x00000001u

/* CMD19/CMD14 bus-test data lengths and patterns. Linux mmc_bus_test()
 * skips 1-bit; JESD84-B51 6.6.4/Annex A.6.3 define the 1-bit transmit
 * pattern as 0x80 and check only the first two returned bits. The 4-bit
 * and 8-bit forms follow Linux mmc_send_bus_test() and compare the full
 * complement byte for each lane group. */
#define GTA4L_EMMC_BUS_TEST_1_BYTES        4u
#define GTA4L_EMMC_BUS_TEST_4_BYTES        4u
#define GTA4L_EMMC_BUS_TEST_8_BYTES        8u
#define GTA4L_EMMC_BUS_TEST_PATTERN_1      0x80u
#define GTA4L_EMMC_BUS_TEST_1_EXPECT_MASK  0xC0u
#define GTA4L_EMMC_BUS_TEST_1_EXPECT       0x40u
#define GTA4L_EMMC_BUS_TEST_PATTERN_4      0x5Au
#define GTA4L_EMMC_BUS_TEST_PATTERN_8_0    0x55u
#define GTA4L_EMMC_BUS_TEST_PATTERN_8_1    0xAAu
#define GTA4L_EMMC_EXT_CSD_DATA_SECTOR_SIZE_OFF 61u
#define GTA4L_EMMC_EXT_CSD_PARTITION_CONFIG_OFF 179u
#define GTA4L_EMMC_EXT_CSD_HS_TIMING_OFF   185u
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
#define GTA4L_EMMC_ERR_BUS_WIDTH         -19
#define GTA4L_EMMC_ERR_BUS_TEST          -20
#define GTA4L_EMMC_ERR_TIMING            -22
/* A width switch failed and the card/host framing could not be proven back
 * at the previous width. The caller must stop: the active bus width is
 * unknown. */
#define GTA4L_EMMC_ERR_WIDTH_RESTORE     -23

/* Diagnostic event codes passed to the log callback. */
#define GTA4L_EMMC_LOG_RESET              1u
#define GTA4L_EMMC_LOG_CLOCK              2u
#define GTA4L_EMMC_LOG_POWER              3u
#define GTA4L_EMMC_LOG_PAD                4u
#define GTA4L_EMMC_LOG_CMD                5u
#define GTA4L_EMMC_LOG_READ               6u
#define GTA4L_EMMC_LOG_CARD               7u
#define GTA4L_EMMC_LOG_ERROR              8u
#define GTA4L_EMMC_LOG_BUS                9u

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
/* Probe-only content phases. The portable core never sets these; the
 * diagnostic probe assigns them before each read so a failure record names
 * the phase that faulted. */
#define GTA4L_EMMC_STEP_PROBE_ARRAY       16u
#define GTA4L_EMMC_STEP_PROBE_DATA        17u
#define GTA4L_EMMC_STEP_PROBE_LAST        18u
#define GTA4L_EMMC_STEP_PROBE_MB          19u
/* Bus-width rung. The portable core sets these only when the caller invokes
 * the opt-in switch/test API. */
#define GTA4L_EMMC_STEP_BUS_WIDTH         20u
#define GTA4L_EMMC_STEP_BUS_TEST          21u
/* Data-timeout audit: the probe-only timeout-configuration readback and
 * experiments. The portable core never sets it. */
#define GTA4L_EMMC_STEP_DTO               22u
/* HS-timing rung: the byte-185 switch and the clock ramp. The portable core
 * sets these only when the caller invokes the opt-in HS-timing API. */
#define GTA4L_EMMC_STEP_HS_TIMING         23u
#define GTA4L_EMMC_STEP_HS_CLOCK          24u

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
	/* Optional free-running counter used only to measure poll duration.
	 * The caller reports its frequency separately. */
	uint64_t (*counter)(void *context);
};

struct gta4l_emmc_cmd7_trace {
	uint32_t iteration; /* poll iteration that observed the change */
	uint32_t normal;    /* normal interrupt status at that sample */
	uint32_t error;     /* error interrupt status at that sample */
};

/* One CMD3 attempt record for the H9 retry discriminator. Every field is a
 * sequential register sample, not an atomic snapshot, and is diagnostics
 * only. The first slot is copied before the failure state is cleared for the
 * duplicate command, so it keeps the attempt-1 record the historical H8
 * reports show. `number` is zero when the slot was not used. */
struct gta4l_emmc_cmd3_capture {
	uint32_t number;       /* 1 or 2; 0 when the attempt did not run */
	int      result;       /* mapped command result */
	uint32_t r1_error;     /* R1 error-mask bits were present */
	uint32_t fail_valid;
	uint32_t fail_index;
	uint32_t fail_argument;
	uint32_t fail_command;
	uint32_t fail_error;
	uint32_t fail_normal;
	uint32_t fail_present;
	uint32_t fail_step;
	uint32_t fail_response[4];
	uint32_t pre_valid;
	uint32_t pre_normal;
	uint32_t pre_error;
	uint32_t pre_present;
	uint32_t pre_normal_enable;
	uint32_t pre_error_enable;
	uint32_t timeout_valid;
	uint32_t timeout_index;
	uint32_t timeout_command;
	uint32_t timeout_argument;
	uint32_t timeout_response_stable;
	uint32_t timeout_response[4];
	uint32_t poll_valid;
	uint32_t poll_iterations;
	uint32_t poll_index;
	uint32_t poll_first_normal;
	uint32_t poll_last_normal;
	uint64_t poll_ticks;
	uint32_t response[4];  /* R1 after success; terminal words otherwise */
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
	uint8_t  data_sector_size_code;
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
	uint32_t cmd1_response[4]; /* raw RESPONSE0..3 after a ready CMD1 */
	uint32_t pre_cmd2_response[4]; /* raw RESPONSE0..3 before CMD2 issue */
	uint8_t  cmd1_response_valid;
	uint8_t  pre_cmd2_response_valid;
	uint8_t  fail_valid;
	/* Expanded CMD2-to-CMD3 capture (diagnostics only). These samples are
	 * sequential register reads, not an atomic snapshot. */
	uint32_t cmd2_normal;    /* normal status when CMD2 completed */
	uint32_t cmd2_error;     /* error status when CMD2 completed */
	uint32_t cmd2_raw_response[4]; /* raw RESPONSE0..3 after CMD2 */
	uint8_t  cmd2_capture_valid;
	uint32_t pre_cmd3_normal;   /* normal status after the pre-command clear */
	uint32_t pre_cmd3_error;
	uint32_t pre_cmd3_present;
	uint32_t pre_cmd3_normal_enable;
	uint32_t pre_cmd3_error_enable;
	uint8_t  pre_cmd3_capture_valid;
	/* H9 CMD3 retry discriminator. `cmd3_retry_enabled` is caller opt-in;
	 * the portable core never sets it. When enabled, a CMD3 completion
	 * timeout triggers exactly one duplicate CMD3 with the same argument
	 * and records both attempts. Every other result keeps the previous
	 * fail-closed behavior. */
	uint8_t  cmd3_retry_enabled;
	uint8_t  cmd3_retry_engaged;
	uint8_t  cmd3_retry_ok;
	uint8_t  cmd3_attempt_count;
	struct gta4l_emmc_cmd3_capture cmd3_attempt[GTA4L_EMMC_CMD3_ATTEMPTS];
	/* Caller opt-in: stop the identification sequence after the TRAN
	 * wait and before the EXT_CSD read. The H9 rung uses it so the rung
	 * touches no EXT_CSD data; the portable core never sets it. */
	uint8_t  identify_only;
	uint32_t poll_first_normal; /* first normal status in the poll */
	uint32_t poll_last_normal;  /* last normal status in the poll */
	uint32_t poll_iterations;   /* poll iterations completed */
	uint64_t poll_ticks;        /* counter delta across the poll */
	uint32_t poll_index;        /* command whose poll was recorded */
	uint8_t  poll_capture_valid;
	uint32_t timeout_normal;    /* fresh post-timeout normal status */
	uint32_t timeout_error;     /* fresh post-timeout error status */
	uint32_t timeout_present;   /* fresh post-timeout present state */
	uint32_t timeout_command;   /* SDHCI COMMAND readback after timeout */
	uint32_t timeout_argument;  /* SDHCI ARGUMENT readback after timeout */
	uint32_t timeout_index;     /* command whose timeout was captured */
	uint32_t timeout_response[4]; /* fresh raw RESPONSE0..3 after timeout */
	uint8_t  timeout_response_stable; /* matches fail_response */
	uint8_t  timeout_capture_valid;
	/* CMD7 R1b capture (diagnostics only). These fields are separate from
	 * the CMD3 fields so a later CMD7 cannot overwrite the CMD3 evidence.
	 * Every sample is a sequential register read, not an atomic snapshot;
	 * the extra reads can perturb timing and cannot prove continuous bus
	 * visibility. */
	uint32_t cmd7_pre_normal;   /* normal status after the pre-command clear */
	uint32_t cmd7_pre_error;    /* error status after the pre-command clear */
	uint32_t cmd7_pre_present;  /* present state before submission */
	uint32_t cmd7_pre_normal_enable;
	uint32_t cmd7_pre_error_enable;
	uint32_t cmd7_pre_response[4]; /* raw RESPONSE0..3 before submission */
	uint8_t  cmd7_pre_capture_valid;
	uint32_t cmd7_poll_or_normal; /* OR of normal samples observed in the poll */
	uint32_t cmd7_poll_or_error;  /* OR of error samples observed in the poll */
	uint32_t cmd7_poll_first_normal;
	uint32_t cmd7_poll_last_normal;
	uint32_t cmd7_poll_iterations;
	uint64_t cmd7_poll_ticks;
	uint32_t cmd7_cc_iteration; /* first in-poll CC observation */
	uint64_t cmd7_cc_ticks;
	uint8_t  cmd7_cc_valid;
	uint32_t cmd7_tc_iteration; /* first in-poll TC observation */
	uint64_t cmd7_tc_ticks;
	uint8_t  cmd7_tc_valid;
	uint8_t  cmd7_poll_capture_valid;
	uint32_t cmd7_trace_count;    /* stored trace entries */
	uint32_t cmd7_trace_overflow; /* observed changes not stored */
	uint32_t cmd7_trace_prev_normal; /* previous observed normal sample */
	uint32_t cmd7_trace_prev_error;  /* previous observed error sample */
	uint8_t  cmd7_trace_prev_valid;  /* a previous sample was observed */
	struct gta4l_emmc_cmd7_trace cmd7_trace[GTA4L_EMMC_CMD7_TRACE_ENTRIES];
	uint32_t cmd7_term_normal;  /* terminal normal status before clear */
	uint32_t cmd7_term_error;   /* terminal error status before clear */
	uint32_t cmd7_term_present; /* terminal present state before clear */
	uint32_t cmd7_term_command; /* SDHCI COMMAND readback before clear */
	uint32_t cmd7_term_argument;
	uint32_t cmd7_term_response[4]; /* terminal raw RESPONSE0..3 */
	uint8_t  cmd7_term_capture_valid;
	/* Multi-block CMD18/CMD12 capture (diagnostics only). The ordinal is
	 * the number of fully transferred 512-byte blocks observed when the
	 * transfer ended, clamped to count - 1 for the failure report. The
	 * stop samples use the same sequential-read caveat as the CMD7
	 * capture. */
	uint32_t multi_block;       /* failing/completed block ordinal */
	int32_t  multi_stop_result; /* CMD12 result; 0 after a clean stop */
	uint32_t multi_stop_normal;
	uint32_t multi_stop_error;
	uint32_t multi_stop_present;
	uint32_t multi_stop_command;
	uint32_t multi_stop_argument;
	uint32_t multi_stop_response[4];
	uint8_t  multi_stop_valid;
	uint8_t  multi_stop_capture_valid;
	/* Post-bound drain (diagnostics only). The secondary window is entered
	 * only when the primary data poll bound expires without an error or
	 * TC; it drains ready blocks and re-checks TC. */
	uint32_t multi_drain_blocks;     /* blocks drained in that window */
	uint32_t multi_drain_iterations; /* poll iterations it consumed */
	uint8_t  multi_drain_used;
	uint8_t  multi_drain_resolved;   /* transfer completion observed */
	/* Optional override for the bus-test write-phase software bound in
	 * microseconds. Zero keeps GTA4L_EMMC_DATA_TIMEOUT_US. The diagnostic
	 * probe sets a longer bound for the data-timeout audit so a late
	 * controller timeout can still be observed; the portable core never
	 * sets it. Only the CMD19 write path reads it. */
	uint32_t bus_test_window_us;
	/* Set only around a CMD14 bus-test read. JESD84-B51 6.6.4 and Linux
	 * sdhci_data_irq() exclude BUS_TEST_R from the data-CRC failure path:
	 * the pattern may carry an optional CRC16 that the host ignores. The
	 * condition is one DATA_CRC indication with no other error bit. */
	uint8_t  bus_test_data_crc_ok;
	uint8_t  bus_test_ignored_crc;
	/* Set only by a rung that follows the Linux `mmc_bus_test()` rule:
	 * CMD19 is a host-to-device pattern for which the card sends no CRC
	 * status token, so the controller's data timeout is the expected
	 * terminal state (TI SPRZ436H i836). A tolerated timeout clears the
	 * data circuit and lets the CMD14 read decide. Zero keeps the strict
	 * historical behaviour. */
	uint8_t  bus_test_tolerate_write_timeout;
};

/* Controller reset, GCC 400 kHz setup, retained-rail power handshake, pads. */
int gta4l_emmc_controller_init(struct gta4l_emmc_host *host);

/* Full controller plus card initialization (CMD0/1/2/3/9/7/13/8). */
int gta4l_emmc_init(struct gta4l_emmc_host *host);

/* One 512-byte PIO read. `block` is a logical 512-byte block address. */
int gta4l_emmc_read_block(struct gta4l_emmc_host *host, uint64_t block,
    void *buffer);

/* Open-ended multi-block PIO read with an explicit CMD12 stop. `count` is
 * the number of 512-byte blocks; `buffer` must hold count * 512 bytes. */
int gta4l_emmc_read_multi(struct gta4l_emmc_host *host, uint64_t start_lba,
    uint32_t count, void *buffer);

/* Read the 512-byte EXT_CSD into `buffer`. */
int gta4l_emmc_read_ext_csd(struct gta4l_emmc_host *host, void *buffer);

/* Evidence for one bus-width rung step. Every field is an observed value;
 * `switched` is the EXT_CSD BUS_WIDTH code read back after the CMD6 switch,
 * and `test_word`/`test_expected` are the first 32 bits of the CMD14 response
 * and of the complement the card must return. The diagnostic fields separate
 * a card that ignored the CMD6 write (`switch_status` clean, `readback_sum`
 * matching the 1-bit read), a readback that returned no data
 * (`readback_sum` zero) and a host width write that did not take effect
 * (`host_after` without the requested width bits). */
struct gta4l_emmc_bus_result {
	uint32_t requested;      /* requested width in bits (1, 4 or 8) */
	uint32_t switched;       /* BUS_WIDTH code read back (0, 1 or 2) */
	int      switch_result;  /* GTA4L_EMMC_OK or the failing code */
	int      test_result;    /* GTA4L_EMMC_OK or the failing code */
	uint32_t test_word;      /* raw CMD14 response, little endian */
	uint32_t test_expected;  /* expected CMD14 word, little endian */
	uint32_t host_control;   /* HOST_CONTROL byte after the host switch */
	uint32_t switch_status;  /* CMD6 R1 status before the readback */
	uint8_t  host_before;    /* HOST_CONTROL before the width write */
	uint8_t  host_after;     /* HOST_CONTROL read back after the write */
	uint32_t readback_sum;   /* byte sum of the EXT_CSD read at the width */
	/* CMD19 write outcome and the bounded data reset that a tolerated
	 * write-phase data timeout needs before CMD14. `write_result` is
	 * GTA4L_EMMC_OK when the write completed normally and
	 * GTA4L_EMMC_ERR_DATA_TIMEOUT for the expected no-CRC-status class;
	 * `reset_result` is only meaningful when the timeout was tolerated. */
	int      write_result;   /* CMD19 write result */
	int      reset_result;   /* data-circuit reset result after a timeout */
	/* The verified switch replaces the invalid byte-183 equality gate with
	 * a read-only properties-segment comparison. `switched` remains the raw
	 * byte-183 observation (diagnostic only, normally 0). */
	uint32_t verified;       /* read-only EXT_CSD comparison passed */
	int      rollback_result; /* card+host restore result after a failure */
	uint32_t cmd6_attempts;  /* CMD6 writes this step issued, including
	                          * any restore retries */
};

/* Switch the card BUS_WIDTH (CMD6) and the SDHCI host width. `bits` may be 1,
 * 4 or 8; 1 is the identification width used to unwind a failed ramp. The
 * caller's `initialized` flag must already be set. */
int gta4l_emmc_set_bus_width(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_result *result);

/* Historical H12 probe of the RESERVED BUS_WIDTH value 4 (not DDR; DDR is
 * 5/6). Kept only to decode the already-flashed H12 image class; new work
 * must not use it. The host applies the matching 4-bit SDR DAT width for
 * the diagnostic read, but no controller DDR configuration is touched. */
int gta4l_emmc_set_bus_width_reserved4(struct gta4l_emmc_host *host,
    struct gta4l_emmc_bus_result *result);

/* Verified width switch: CMD6, CMD13 TRAN wait, host width apply/readback,
 * then an EXT_CSD read at the new width whose read-only properties segment
 * must equal the pre-switch baseline. Byte 183 is not readable and is never
 * used as proof. On any failure both the host and the card are restored to
 * the previous width and verified; a failed restore returns
 * GTA4L_EMMC_ERR_WIDTH_RESTORE so the caller stops with unknown framing. */
int gta4l_emmc_set_bus_width_verified(struct gta4l_emmc_host *host,
    uint32_t bits, struct gta4l_emmc_bus_result *result);

/* Restore a width after a failed data operation, using a read-only EXT_CSD
 * baseline captured while the data path was healthy. Unlike the verified
 * switch this never reads EXT_CSD before issuing CMD6: the command path is
 * independent of the DAT state, while a wedged DAT path would block the
 * pre-read and leave the card and host at the failed width. The function
 * issues the CMD6, applies the host width, clears a latched DAT inhibit with
 * one bounded local data-circuit reset and proves the restore with an EXT_CSD
 * properties comparison. A card command failure, an inhibit that survives the
 * reset, or a failed comparison returns GTA4L_EMMC_ERR_WIDTH_RESTORE. */
int gta4l_emmc_set_bus_width_recover(struct gta4l_emmc_host *host,
    uint32_t bits, const uint8_t *baseline,
    struct gta4l_emmc_bus_result *result);

/* Corrected CMD19/CMD14 bus test: ignores a lone CMD14 data-CRC indication
 * (optional pattern CRC16), uses the JEDEC 1-bit 0x80 pattern with the
 * two-bit check, and still requires the returned pattern. */
int gta4l_emmc_bus_test_verified(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_result *result);

/* CMD19/CMD14 bus-test round trip at the active width. `bits` may be 1, 4
 * or 8. The host must already be in the same width as the card. A pattern
 * disagreement returns GTA4L_EMMC_ERR_BUS_TEST and still fills the observed
 * words; a data-phase timeout returns the mapped terminal error and leaves
 * the observed words untouched. The 1-bit form is the stage-B1C
 * discriminator: a passing 1-bit test means the CMD19 write path works at
 * the identification width. */
int gta4l_emmc_bus_test(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_result *result);

/* Evidence for the rung-1 write-phase recovery: one CMD19 write, one
 * bounded SDHCI data-circuit reset, the post-reset present state and one
 * CMD14 read. The write fields are the terminal sample the core latched for
 * a failing CMD19 data phase (zero on a clean write); the reset fields are
 * the SOFTWARE_RESET value written, the value read back, the number of
 * status polls and the post-reset PRESENT_STATE (with its DAT_INHIBIT bit);
 * the test fields are the CMD14 result and the first 32 bits of the
 * returned and expected pattern words. */
struct gta4l_emmc_bus_reset_result {
	uint32_t requested;      /* requested width in bits (1, 4 or 8) */
	uint32_t host_control;   /* HOST_CONTROL before the write */
	int      write_result;   /* CMD19 write result */
	uint16_t write_error;    /* latched ERROR_INT_STATUS after the write */
	uint16_t write_normal;   /* latched NORMAL_INT_STATUS after the write */
	uint32_t write_present;  /* PRESENT_STATE after the write */
	int      reset_result;   /* GTA4L_EMMC_OK or GTA4L_EMMC_ERR_RESET */
	uint8_t  reset_written;  /* value written to SOFTWARE_RESET */
	uint8_t  reset_readback; /* last SOFTWARE_RESET value read */
	uint32_t reset_iterations; /* SOFTWARE_RESET status reads */
	uint32_t post_present;   /* PRESENT_STATE after the reset */
	uint32_t dat_inhibit;    /* DAT_INHIBIT bit of post_present */
	int      test_result;    /* GTA4L_EMMC_OK or the failing code */
	uint32_t test_word;      /* raw CMD14 response, little endian */
	uint32_t test_expected;  /* expected CMD14 word, little endian */
};

/* CMD19 write, bounded data-circuit reset and CMD14 read at the active
 * width. `bits` may be 1, 4 or 8; the host must already be in the same
 * width as the card. The reset is local to this diagnostic helper and
 * changes no other timeout path. A reset that does not self-clear returns
 * GTA4L_EMMC_ERR_RESET; a post-reset DAT_INHIBIT returns
 * GTA4L_EMMC_ERR_INHIBIT before CMD14 is issued; a pattern disagreement
 * returns GTA4L_EMMC_ERR_BUS_TEST and still fills the observed words. */
int gta4l_emmc_bus_test_reset(struct gta4l_emmc_host *host, uint32_t bits,
    struct gta4l_emmc_bus_reset_result *result);

/* Evidence for the forced post-write drain: one CMD12 STOP_TRANSMISSION
 * (R1b) followed, when the stop completed, by one CMD13 SEND_STATUS (R1).
 * The CMD12 entry wait checks only COMMAND_INHIBIT; SDHCI exempts the stop
 * command from DAT_INHIBIT because an aborted data transfer can retain it
 * while the command line is free. The terminal tuple for each command is
 * captured before the status clear. `status_result` is
 * GTA4L_EMMC_ERR_UNSUPPORTED when CMD12 failed and CMD13 was not issued. */
struct gta4l_emmc_post_write_drain_result {
	int      stop_result;      /* CMD12 result */
	uint32_t stop_response;    /* CMD12 R1 response */
	uint16_t stop_error;       /* terminal ERROR_INT_STATUS */
	uint16_t stop_normal;      /* terminal NORMAL_INT_STATUS */
	uint32_t stop_present;     /* terminal PRESENT_STATE */
	int      status_attempted; /* CMD13 was issued */
	int      status_result;    /* CMD13 result */
	uint32_t status_response;  /* CMD13 R1 response */
	uint16_t status_error;     /* terminal ERROR_INT_STATUS */
	uint16_t status_normal;    /* terminal NORMAL_INT_STATUS */
	uint32_t status_present;   /* terminal PRESENT_STATE */
};

/* Force the post-write drain: one CMD12 and, when it completed, one CMD13.
 * The drain is local to this diagnostic helper, changes no timeout path and
 * records card-status bits instead of promoting them to a transport error.
 * Returns OK only when both commands completed. */
int gta4l_emmc_post_write_drain(struct gta4l_emmc_host *host,
    struct gta4l_emmc_post_write_drain_result *result);

/* The rung-2 form of the write-phase recovery: the rung-1 CMD19 write and
 * bounded data-circuit reset, the post-reset present-state sample, the
 * forced CMD12/CMD13 drain, then the rung-1 CMD14 read. The drain runs
 * after the reset while the post-reset inhibit is clear and before CMD14,
 * because a failed CMD14 can re-latch DAT_INHIBIT. A reset or inhibit
 * failure returns before the drain; a drain failure is recorded in
 * `drain` but does not suppress the record-only CMD14 read. */
int gta4l_emmc_bus_test_reset_drain(struct gta4l_emmc_host *host,
    uint32_t bits, struct gta4l_emmc_bus_reset_result *result,
    struct gta4l_emmc_post_write_drain_result *drain);

/* Evidence for one HS_TIMING rung step. `hs_before`/`hs_after` are EXT_CSD
 * byte 185 read before and after the CMD6 switch; the RCG fields are the GCC
 * SDCC1 APPS source configuration, because CLOCK_CONTROL carries only the
 * enable/stable bits and cannot name the card clock. `timing` is the value
 * requested (0 legacy, 1 high speed). */
struct gta4l_emmc_hs_result {
	uint32_t timing;        /* requested byte-185 value (0 or 1) */
	uint32_t hs_before;     /* byte 185 read before the switch */
	uint32_t hs_after;      /* byte 185 read after the switch */
	uint32_t rcg_before;    /* GCC APPS CFG before the clock change */
	uint32_t rcg_after;     /* GCC APPS CFG after the clock change */
	uint32_t clock;         /* CLOCK_CONTROL after the clock change */
	uint32_t switch_status; /* CMD6 R1 status */
	uint32_t readback_sum;  /* byte sum of the post-switch EXT_CSD read */
	int      switch_result; /* OK, a command/status error or ERR_TIMING */
	int      clock_result;  /* OK or ERR_CLOCK */
};

/* Enter (timing=1, byte-185 high speed, 50 MHz HS clock) or leave
 * (timing=0, clock first back to 400 kHz, then byte-185 legacy) the HS
 * timing class. The switch is ordered as Linux: for timing=1 the CMD6 and
 * its byte-185 readback complete before the clock rises; for timing=0 the
 * clock drops before the card is asked to leave HS timing. A byte-185
 * readback mismatch returns GTA4L_EMMC_ERR_TIMING and, for timing=1, leaves
 * the clock at 400 kHz. The caller's `initialized` flag must already be
 * set. */
int gta4l_emmc_set_hs_timing(struct gta4l_emmc_host *host, uint32_t timing,
    struct gta4l_emmc_hs_result *result);

#endif /* GTA4L_EMMC_H */
