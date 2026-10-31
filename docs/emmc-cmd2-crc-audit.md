# gta4l eMMC CMD2 CRC audit

**Status:** read-only reference audit. No source, build, flash, or partition
changes were made by the audit itself. Comment-only corrections to
`package/Gta4lEmmc.c` and `package/Gta4lEmmc.h` were applied afterwards; the
line anchors below are unchanged. TWRP on `RECOVERY` remains
`14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae`.

**Audit baseline (current working tree after the comment corrections and the
host-side probe instrumentation below):**

| Artifact | SHA-256 |
| --- | --- |
| `src/UniXNU-gta4l/package/Gta4lEmmc.c` | `722110a5c98ca4ac5520f70ce8ca9aa7d8a9f679c861698eda0e568aa7fb01ca` |
| `src/UniXNU-gta4l/package/Gta4lEmmc.h` | `7abc7d878f75f83cdc71cdd55dbcc6036c44b181a48e861c4a791cd24d05ca09` |
| `src/UniXNU-gta4l/package/Gta4lEmmcProbe.c` | `d541514b08176f296e026cf4d2492952eecaa0be319e7757088423e54576326b` |
| `src/UniXNU-gta4l/package/Gta4lSdhciHostInit.c` | `3f8f45316cff8b3ad19266f901dba69475d7dd0816ce9b6040f2bee37eaebafb` |
| `src/UniXNU-gta4l/scripts/sim-gta4l-emmc/harness.c` | `5262362d1cdc86c725109c428ce150ba2901f839b3f0fb5aa574b90aecf95b09` |
| `src/UniXNU-gta4l/scripts/sim-gta4l-emmc-probe/harness.c` | `18f2637a84330b4eaac216a362e2b847596beb90245af5dd3f2dd74e65747690` |
| `src/UniXNU-gta4l/scripts/test-gta4l-emmc-probe.py` | `52c94ccb606de0fbfc9f09d8142fc3700d6d8575b3b15245dc2c7d9520de5cf3` |

The host probe simulator runs a stage-3 CMD0/CMD1/CMD2 scenario that fails CMD2
with a synthetic CRC error and R2 response, and it rejects six mutation controls
for the failure-report reads and snapshot.

**Hardware artifacts:**

- Stage-2/3 hardware-tested kernel:
  `1a4710892e49fa96c053de8f33f1b4b5d89a36fc3d688febd6fa0f7e4a89003a`
  (`src/UniXNU-gta4l/docs/emmc-driver.md:328,360`).
- Flashed failure-record kernel:
  `be30e9f4f5d88a87da2943a8e1c977d8b5c96151ed78051cdae35782eaed186e`
  (`src/UniXNU-gta4l/docs/emmc-driver.md:383`); hardware run is pending
  (`src/UniXNU-gta4l/docs/emmc-driver.md:395`,
  `out/gta4l/ram-root-emmc-probe-failrec-evidence/flash-record.json:25`).
- Current source changed after the failure-record image was built. The current
  failure path now snapshots raw `RESPONSE0..3` (`Gta4lEmmc.c:84-95,97-116`) and
  prints `rsp=` and `cmd1=` (`Gta4lEmmcProbe.c:425-447`). The flashed
  failure-record image predates those fields. Do not attribute `rsp=` or
  `cmd1=` values to the flashed image.

---

## 1. Executive conclusion

The portable CMD2 path is semantically equivalent to mainline Linux and is
covered by the simulator. The recorded stage-3 result is `-8`, which the core
maps from the SDHCI command-CRC error bit `0x0002`. In this command sequence
CMD2 is the only CRC-checked command that can produce that error: CMD0 has no
response, CMD1 is sent without CRC checking, and CMD2 is the next command with
`RESP_LONG | CRC`.

The recorded failure is therefore attributable to CMD2 by code-path inference,
not by a captured hardware index. The hardware-tested build (`1a471089…`)
predates the current POR/vendor/DLL normalization and the current raw-response
snapshot. Its `ven=00318a0c` is the pre-POR handoff value plus pad bits, not the
value current code produces. The current failure-record image (`be30e9f4…`) is
flashed but unrun.

The root cause is **not uniquely identified**. The leading explanation for the
recorded failure is the old build's unnormalized host-init state; if the current
build still fails at CMD2, the leading explanations become CMD-line
electrical/signal integrity, rail/voltage state, or card-side CID/CRC behavior.
No CRC checking may be disabled.

---

## 2. Evidence inventory

### 2.1 Stage-2 hardware result (PASS, controller-local)

Path: `out/gta4l/ram-root-emmc-probe-evidence/host-report-stage2.txt:1-3`

    GTA4L-EMMC HOST REPORT: rc=0 mci=10000072 pwr=0f clk=0007 hc2=0008
    ven=00318a0c pst=00000000 log=4/1/268435570 @Gta4lEmmcProbe.c:394

Establishes: v5 controller initialized; bus power written; no pending power/IO
request; internal clock stable and card clock enabled; 1.8 V signalling
selected; pad bits set. Does **not** establish card identification or any rail
voltage.

### 2.2 Stage-3 hardware result (FAIL, `-8`)

Path: `out/gta4l/ram-root-emmc-probe-stage3-evidence/host-report-stage3.txt:1-3`

    GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967288 mci=10000072 pwr=0f
    clk=0007 hc2=0008 ven=00318a0c pst=00000000 cfg=00002017 cmd=0000
    0000 log=4/1/268435570 @Gta4lEmmcProbe.c:373

`rc=4294967288` is `0xFFFFFFF8` = `-8` = `GTA4L_EMMC_ERR_CMD_CRC`
(`Gta4lEmmc.h:242`). The SDHCI error bit is
`GTA4L_EMMC_INT_ERR_CMD_CRC = 0x0002` (`Gta4lEmmc.h:129`).

The transcript contains no `fail=`, `rsp=`, `cmd1=`, DLL, vendor-spec3, DDR, or
MND fields. It bounds the error to a CRC-checked command but does not itself
name the command. The current source's richer report is not the report that
produced this transcript.

### 2.3 Failure-record image (flashed, not run)

Path: `out/gta4l/ram-root-emmc-probe-failrec-evidence/flash-record.json:1-26`

- Image: `boot-ram-root-emmc-probe-failrec.img`, 85,688,320 bytes, SHA-256
  `76745fe7e81ca68025ee428cc96c88f98a392b125ecd18c5cf5d0bbef57c7488`.
- BOOT-only write; pre-flash BOOT backup
  `ef2fb96f0b7e0925c157613953d8fc0cd694019232f13408239e579abf97e936`.
- `RECOVERY` before and after:
  `14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae`.
- EFS/SEC_EFS backups recorded and verified against `SHA256SUMS.all`.
- `"hardware_run": "pending"`.

This is flash evidence, not run evidence. It must not be cited as a captured
CMD2 failure record.

### 2.4 Handoff snapshot

Path: `src/UniXNU-gta4l/docs/sdhci-handoff-audit.md:130-158`

    ps=01f800f0 clk=0003 pwr=00 hc=00 hc2=0000
    vs=00300a0c vc=742dc8b2 ddr=80040873 dll=00000000

The handoff vendor-spec value is `0x00300a0c`; the DLL config is zero. The
stage-3 `ven=00318a0c` is the handoff value plus pad bits `0x00018000`. The
stage-3 DLL state was not captured.

### 2.5 Electrical contract

Path: `src/UniXNU-gta4l/docs/sdhc-electrical-contract.md:48-52`

- VCC via L24: range 2.704–3.600 V, requested 2.960 V; measured level `[U]`.
- VCCQ/IO via L11: range 1.704–1.952 V, requested 1.800 V; measured level `[U]`.
- SDC1 pads: vendor pinctrl drive/pull values are source facts; actual handoff
  pad state and pad supply are `[U]`.

---

## 3. Failure attribution: why CMD2

### 3.1 Command sequence

`gta4l_emmc_init()` sends commands in this order (`Gta4lEmmc.c:1041-1163`):

1. CMD0, argument 0, flags `GTA4L_EMMC_CMD_RESP_NONE`
   (`Gta4lEmmc.c:1058-1060`). No response, no CRC check.
2. CMD1, argument `GTA4L_EMMC_OCR_VALUE`, flags
   `GTA4L_EMMC_CMD_RESP_SHORT` only (`Gta4lEmmc.c:900-934`, specifically
   `:917-919`). No CRC bit, no index bit.
3. CMD2, argument 0, flags `GTA4L_EMMC_CMD_RESP_LONG | GTA4L_EMMC_CMD_CRC`
   (`Gta4lEmmc.c:1073-1075`). First command with CRC checking.
4. CMD3, CMD9, CMD7, CMD13 follow only if CMD2 succeeds
   (`Gta4lEmmc.c:1088-1133`).

### 3.2 Error mapping

`gta4l_emmc_send_command()` polls `ERROR_INT_STATUS` before accepting `CC`
(`Gta4lEmmc.c:317-338`). On any enabled error bit it records the failure,
clears the latches, and returns `gta4l_emmc_map_error(error, 0)`
(`Gta4lEmmc.c:327-330`). `gta4l_emmc_map_error()` checks `CMD_TIMEOUT` first,
then `CMD_CRC` (`Gta4lEmmc.c:265-284`). Thus `error=0x0002` maps to `-8`.

### 3.3 The inference

Given SDHCI semantics, a controller raises `CMD_CRC` only for a command with
CRC checking enabled. CMD0 has no response and no CRC. CMD1 has `RESP_SHORT`
only and no CRC. The core returns immediately on the first error. Therefore,
for the recorded build, `-8` uniquely implies CMD2.

This is a code-path inference, not a captured hardware index. The stage-3
transcript does not contain `fail_index`, `fail_command`, or `fail_error`. The
failure-record build that would capture them has not been run.

### 3.4 Explicit non-supported statement

The stage-3 transcript does **not** establish:

- the failing command index;
- the exact `ERROR_INT_STATUS` value;
- the raw `RESPONSE0..3` words;
- whether `CC` was also set;
- the DLL/vendor-spec3/DDR/MND state at the failure.

The CMD2 attribution is valid only as an inference from the code path and `-8`.

---

## 4. Reference parity: the CMD2 software path matches mainline

### 4.1 Command encoding

`Gta4lEmmc.h:146-154`:

    RESP_NONE       0x0000
    RESP_LONG       0x0001
    RESP_SHORT      0x0002
    RESP_SHORT_BUSY 0x0003
    RESP_TYPE_MASK  0x0003
    CRC             0x0008
    INDEX           0x0010

These exactly match `/tmp/opencode/linux-pin/sdhci.h:68-71`.

The core encodes the command register as `(index << 8) | flags`
(`Gta4lEmmc.c:299`). CMD2 therefore encodes `0x0209`.

Linux sets `SDHCI_CMD_RESP_LONG` for `MMC_RSP_136`
(`/tmp/opencode/linux-pin/sdhci.c:1711-1712`) and `SDHCI_CMD_CRC` for
`MMC_RSP_CRC` (`/tmp/opencode/linux-pin/sdhci.c:1718-1719`). The core does the
same.

### 4.2 Long-response normalization

Core: `gta4l_emmc_read_long_response()` reads `RESPONSE3..RESPONSE0` in reverse
word order, shifts each word left by 8, and ORs the next word's top byte
(`Gta4lEmmc.c:202-217`).

Linux: `sdhci_read_rsp_136()` performs the identical shift
(`/tmp/opencode/linux-pin/sdhci.c:1798-1816`).

Samsung reference: the same shift
(`/tmp/opencode/samsung-src/CXG1/drivers/mmc/host/sdhci.c:1442-1460`).

Simulator: `sim_set_response()` encodes a normalized 128-bit response into the
raw register layout (`src/UniXNU-gta4l/scripts/sim-gta4l-emmc/harness.c:527-543`),
and the `cmd-crc-snapshot` test verifies that the raw words are snapshotted
before the error latch is cleared (`harness.c:1209-1213,1765-1790`).

The normal SDHCI layout strips the CRC byte; the raw `RESPONSE0..3` words
reconstruct the 128-bit CID but do not directly expose the card's CRC7 byte.
The core's comment at `Gta4lEmmc.c:195-198` now states this layout correctly.
The constants and the shift behavior are correct.

### 4.3 Error mapping

Linux maps `SDHCI_INT_CRC` to `-EILSEQ` (`/tmp/opencode/linux-pin/sdhci.c:3330-3336`).
The core maps `CMD_CRC` to `-8` (`Gta4lEmmc.c:265-284`). The semantics match.

### 4.4 Send/poll sequence

Core (`Gta4lEmmc.c:287-362`):

1. Wait for command/data inhibit clear.
2. Clear normal and error latches.
3. Write `ARGUMENT`.
4. Write `COMMAND`.
5. Poll `ERROR_INT_STATUS`; if any enabled error bit, record and return.
6. Poll `NORMAL_INT_STATUS` for `CC`.
7. On `CC`, read the response (long or short).
8. Clear latches.

Linux performs the same conceptual sequence in its IRQ-driven command path; the
portable core uses polling with interrupt signalling disabled
(`Gta4lEmmc.c:230-243`). No semantic divergence was found.

The simulator covers the error-priority case where the controller reports `CC`
together with `CMD_CRC` (`harness.c:1204-1207,1754-1763`): the error must win,
and the CRC-failed CID must not be promoted to `host.cid`.

### 4.5 Clock

Core constants (`Gta4lEmmc.h:179-184`):

    RCG_400K_CFG = 0x00002017
    RCG_400K_M   = 1
    RCG_400K_N   = 0xFC
    RCG_400K_D   = 0xFB

Programming: `Gta4lEmmc.c:511-566`.

Mainline table: `/tmp/opencode/linux-verify/gcc-sm6115.c:1248-1258` contains
`F(400000, P_BI_TCXO, 12, 1, 4)`; `cmd_rcgr = 0x38028`
(`gcc-sm6115.c:1261`).

The RCG formula is `parent * 2 / (pre_div + 1) * m / n`; with
`pre_div = 2*h - 1 = 23`, `m/n = 1/4`, and `P_BI_TCXO = 19.2 MHz`, the result
is `19.2 MHz * 2/24 * 1/4 = 400 kHz`. The M/N/D encodings are `M=1`,
`N=~ (4-1)=0xFC`, `D=~4=0xFB`. The simulator asserts the same values
(`harness.c:1333-1345`).

The header comment at `Gta4lEmmc.h:179` now states the correct
`BI_TCXO * 2 / 24 * 1 / 4` arithmetic; the constants are correct.

The actual card-clock frequency at the pad is **not measured**. `cfg=00002017`
and `clk=0007` are register states, not a frequency measurement.

### 4.6 Power and IO

Core power sequence: `gta4l_emmc_power_on()` (`Gta4lEmmc.c:568-682`). It writes
`POWER_CONTROL` with bus power and 3.3 V select, then handles each observed
`CORE_PWRCTL_STATUS` request, clears only the observed bits, applies the
latest-level rule (`BUS_ON` implies IO-high; `IO_LOW` overrides; `IO_HIGH`
overrides), and requires an observed `BUS_ON` completion plus completion of the
requested IO transition.

Mainline reference: `/tmp/opencode/linux-pin/sdhci-msm.c:1718-1755` for the
power handler; `:2323-2325` for `HOST_CONTROL2 VDD_180` to
`REQ_IO_LOW`/`REQ_IO_HIGH`; `:2412-2417` for pad switch; `:2452-2480` for
voltage switch.

Core policy: `Gta4lEmmcProbe.c:345-352` sets `fixed_1v8=1` and
`pad_pwr_switch=1`; `Gta4lEmmc.c:825-864` selects `HOST_CONTROL2 VDD_180` and
the pad power switch.

POR divergence: mainline uses `CORE_VENDOR_SPEC_POR_VAL 0xa9c`
(`sdhci-msm.c:76`); Samsung uses `0xA1C`
(`/tmp/opencode/samsung-src/CXG1/drivers/mmc/host/sdhci-msm.c:101`). The core
uses `0xA9C` (`Gta4lEmmc.h:101`, `Gta4lEmmc.c:814-818`). The divergence is
documented but not proven irrelevant.

The power acknowledgement is controller-local. `BUS_SUCCESS` does not prove the
rail voltage. `pwr=0f` and `pst=00000000` are not electrical measurements.

### 4.7 Low-speed vendor/DLL configuration

Core: `gta4l_emmc_configure_low_speed()` (`Gta4lEmmc.c:702-736`) clears
`CORE_VENDOR_SPEC3` `PWRSAVE_DLL`, sets `DLL_RST | DLL_PDN`, selects the default
MCLK, clears `HC_SELECT_IN_EN`/`HC_SELECT_IN_MASK`, and clears
`CORE_CLK_PWRSAVE`.

Mainline: `msm_hc_select_default()` (`/tmp/opencode/linux-pin/sdhci-msm.c:803-840`)
performs the vendor-spec normalization; the <=100 MHz path sets
`DLL_RST | DLL_PDN` (`sdhci-msm.c:1397-1420`).

Simulator: `harness.c:1362-1392` asserts 1.8 V signalling, pad switch, DLL
reset/power-down, MCLK default, `HC_SELECT_IN` cleared, clock power-save
cleared, and the normalized POR value.

The current core therefore matches the mainline low-speed policy. The
hardware-tested build did not produce the normalized value (see section 5).

### 4.8 Vendor-register write comparison (v5, minor 0x72)

The v5 offset table uses `core_ddr_config = 0x25c`
(`/tmp/opencode/linux-pin/sdhci-msm.c:229`), selected because minor `0x72 >= 0x49`
(`:2827-2828`). `0x1b8`/`0x1bc` are the v4/mci offsets (`:259-260`) and do not
apply.

Linux does **not** write `DDR_CONFIG_POR_VAL 0x80040873` before the first
command at 400 kHz. The write occurs only in
`sdhci_msm_cm_dll_sdc4_calibration()` (`:1047-1051`), reached from the HS400
DLL-calibration path (`:1427-1428` → `:1345` → `:1140`) and guarded by
`host->clock > CORE_FREQ_100MHZ && (msm_host->tuning_done ||
ios->enhanced_strobe) && !msm_host->calibration_done` (`:1342-1344`).
`CORE_CMDIN_RCLK_EN` is `BIT(1)` of `core_ddr_200_cfg` (`0x224`,
`:116,225`) and is set only under `if (mmc->ios.enhanced_strobe)` inside the
same HS400 calibration (`:1053-1058`). Neither is required before CMD2 at
400 kHz.

Linux vendor writes before the first command, and the core's coverage:

| Offset | Value / action | Context | Core coverage |
| --- | --- | --- | --- |
| `0x20C` | `0x00000A9C` | probe POR (`:2779-2780`) | performed (`Gta4lEmmc.c:794-796`) |
| `0x21C` | capabilities advertisement | probe (`:2820-2824`) | **missing** |
| `0x244` | `0x0000000F` power-IRQ mask | probe (`:2861-2862`) | **missing** |
| `0x20C` | pad-switch bits 15/16 | regulator caps (`:2384-2421`) | performed (`Gta4lEmmc.c:831-841`) |
| `0x248`/`0x24C` | power status/ack | probe/power IRQ (`:1693-1694,1776-1777`) | performed in the handshake (`Gta4lEmmc.c:605-606,642-643`); probe-time zero writes skipped |
| `0x250` | clear `BIT(3)` PWRSAVE_DLL | `msm_hc_select_default()` (`:811-816`) | performed (`Gta4lEmmc.c:686-691`) |
| `0x20C` | clear `0x300`, set `0x200`, clear `BIT(18)`/`0x380000` | `msm_hc_select_default()` (`:819-834`) | performed (`Gta4lEmmc.c:705-714`) |
| `0x200` | set `BIT(30)` DLL_RST, then `BIT(29)` DLL_PDN | `<=100 MHz` (`:1404-1414`) | performed (`Gta4lEmmc.c:693-703`) |
| `0x224`/`0x25C`/`0x254`/`0x258` | not written at 400 kHz | HS400 only | not written |

The only omissions are `0x21C` (vendor capabilities advertisement) and `0x244`
(power-IRQ mask). Neither participates in command issue, response sampling, or
CRC checking. The HS400-only `0x25C` and `0x224 BIT(1)` writes are not performed
by Linux at 400 kHz either. The legacy vendor state that could affect sampling
(`0x20C` MCLK_SELECT/HC_SELECT_IN/pad bits, `0x250` PWRSAVE_DLL, and `0x200`
DLL_RST|PDN) is matched. No missing vendor write is a credible cause of a CMD2
R2 `CMD_CRC` at 400 kHz.

---

## 5. Evidence/build mismatch

### 5.1 The hardware-tested build is not current source

The stage-2/3 hardware images used kernel
`1a4710892e49fa96c053de8f33f1b4b5d89a36fc3d688febd6fa0f7e4a89003a`
(`src/UniXNU-gta4l/docs/emmc-driver.md:328,360`). Current source is
`Gta4lEmmc.c 722110a5…`, `Gta4lEmmc.h 7abc7d87…`, `Gta4lEmmcProbe.c d541514b…`.

The current source adds:

- raw `RESPONSE0..3` snapshot before clearing the error latch
  (`Gta4lEmmc.c:84-95,97-116`);
- `cmd1_attempts` / `cmd1_timeouts` accounting (`Gta4lEmmc.h:326-327`,
  `Gta4lEmmc.c:900-934`);
- a richer failure panic with `rsp=`, `cmd1=`, `v3=`, `ddr=`, `mnd=`, and
  `hc=` fields (`Gta4lEmmcProbe.c:425-447`).

The flashed failure-record image `be30e9f4…` was built before these additions.
Its hardware run is pending (`docs/emmc-driver.md:395`, `flash-record.json:25`).
The current source has not been hardware-tested.

### 5.2 The `ven` value proves the tested build lacked current normalization

Stage-3 reports `ven=00318a0c`
(`out/gta4l/ram-root-emmc-probe-stage3-evidence/host-report-stage3.txt:1-3`).
The handoff snapshot reports `vs=00300a0c` and `dll=00000000`
(`docs/sdhci-handoff-audit.md:130-158`). The stage-3 value is the handoff value
plus pad bits `0x00018000`.

Current code writes `CORE_VENDOR_SPEC_POR = 0x00000A9C` after reset
(`Gta4lEmmc.c:814-818`) and then applies the pad policy and low-speed fields
(`Gta4lEmmc.c:850-866`). The expected current value is `0x00018A9C`
(`0xA9C | 0x18000`), with `HC_SELECT_IN` cleared. The simulator requires exactly
`SIM_VENDOR_SPEC_POR | SIM_PAD_PWR_SWITCH_EN | SIM_PAD_PWR_SWITCH`
(`harness.c:1390-1392`).

Therefore the recorded `ven=00318a0c` is not the current core's expected
host-init state. The stage-3 transcript does not validate the current
POR/vendor/DLL code. This is an inference from the register values and the
documented build hashes, not a build-log correlation from the same run.

### 5.3 Documentation was stale in places

All items below were corrected in the working tree after the audit; the line
numbers refer to the audit baseline.

- `src/UniXNU-gta4l/docs/emmc-driver.md:456` cites
  `package/Gta4lEmmc.h:208` for `GTA4L_EMMC_ERR_CMD_CRC`; current line is
  `Gta4lEmmc.h:242`.
- `docs/emmc-driver.md:457-461` says `ven=00318a0c` repeats the stage-2
  host-init state and that the 1.8 V signalling and pad-switch policy were
  applied. The pad bits are set, but the value is not the current normalized
  value. The bullet is stale for current code.
- `docs/emmc-driver.md:395` correctly says the failure-report run is pending.
- `docs/emmc-driver.md:502-507,542` correctly says the recorded stage-3
  transcript predates the failure record and DLL fields.
- `docs/emmc-driver.md:370-376` says the failure-record build matches the
  current source hashes. That was true when the image was built; the source has
  since changed. Re-check before citing the flashed image as current.
- `Gta4lEmmc.h:179` comment arithmetic was wrong; it is corrected in the current
  tree.
- `Gta4lEmmc.c:195-198` comment described the CRC byte layout incorrectly; it is
  corrected in the current tree.

### 5.4 Explicit non-supported statement

The following are **not** established:

- that current source reproduces the stage-3 failure;
- that current source fixes the stage-3 failure;
- that the old build's missing POR/vendor/DLL normalization caused the CRC
  error;
- that the flashed failure-record image contains the current raw-response and
  CMD1 diagnostics;
- that `ven=00318a0c` is equivalent to the current code's host-init state.

---

## 6. Ranked hypotheses

Ranking is for the **recorded failure** first, then for a failure that persists
on current source. The ranking is not a conclusion; each hypothesis has a
discriminating diagnostic.

### H1 — Rank 1: the recorded failure is an artifact of the old build's unnormalized host-init state

**Statement:** The hardware-tested build `1a471089…` did not apply the current
POR/vendor/DLL low-speed normalization. The resulting controller state may have
been unsuitable for the CRC-checked 136-bit R2 response.

**Why:**

- Stage-3 `ven=00318a0c` equals handoff `vs=00300a0c` plus pad bits; current
  code writes POR `0xA9C` and normalizes to `0x18A9C`
  (`Gta4lEmmc.c:814-818,850-866,702-736`; `harness.c:1079,1362-1392`).
- The handoff DLL config was zero (`docs/sdhci-handoff-audit.md:130-158`), but
  the stage-3 DLL state was not captured.
- The stage-3 build predates the current raw-response snapshot and CMD1 counters
  (`docs/emmc-driver.md:328,360`; current hashes in the baseline table).

**Discriminating diagnostic:**

1. Boot the already-flashed failure-record image (`be30e9f4…`) with owner
   approval. It captures `fail=` but not `rsp=`/`cmd1=`.
2. Build current source (`722110a5…`, `d541514b…`, `5262362d…`) into a new
   failure-record image; with separate owner approval, write it to BOOT only and
   boot it.
3. Compare `ven`, `v3`, `dll`, `dst`, `ddr`, and `mnd` at failure. If
   `ven=0x18A9C` and the DLL bits are `DLL_RST|DLL_PDN`, current normalization
   was applied. If CMD2 then passes, H1 is confirmed for the old failure. If
   CMD2 still fails, H1 is rejected as the sole cause.

**Not supported:** The old build's `ven` value does not prove that the missing
normalization caused the CRC error. At 400 kHz the DLL is not required, the
handoff DLL was already zero, and `HC_SELECT_IN_EN` was clear in the handoff
value. The functional impact of the missing POR bits is unproven.

### H2 — Rank 2: CMD-line electrical/signal-integrity or pad/pull-up issue corrupts the R2 response

**Statement:** The card drives a 136-bit R2 response on CMD. A pad direction,
pull-up, drive-strength, pad-supply, or signal-integrity problem can corrupt one
or more response bits and make the controller's CRC check fail.

**Why:**

- The software path matches mainline (section 4), so a persistent failure points
  away from encoding/decoding.
- CMD1 uses the same CMD line and succeeds with a short R3 response; R2 is
  longer and card-driven. CMD1 success does not prove R2 signal integrity.
- The core writes the UEFI-derived pad value `0x9FE4` (`Gta4lEmmc.h:187`,
  `Gta4lEmmc.c:684-700`), but the actual pad state, pad supply, and CMD pull-up
  are not measured (`docs/sdhc-electrical-contract.md:48-52`).

**Discriminating diagnostic:**

1. Capture raw `RESPONSE0..3` with the current build and reconstruct the 128-bit
   CID using the `read_long_response` shift (`Gta4lEmmc.c:202-217`):
   - `cid[0] = rsp[3] << 8 | rsp[2] >> 24`
   - `cid[1] = rsp[2] << 8 | rsp[1] >> 24`
   - `cid[2] = rsp[1] << 8 | rsp[0] >> 24`
   - `cid[3] = rsp[0] << 8`
2. Compare the reconstructed CID with a known-good CID from TWRP/Linux or
   another host. If the reconstructed CID differs, the response bits were
   corrupted → signal/electrical.
3. Scope CMD and CLK at the pad during CMD2 at 400 kHz, if equipment is
   available. Measure L11/pad supply and the CMD pull-up rail.
4. Check the pad configuration actually read back at failure (`hc=`, `ven=`)
   against the intended `0x9FE4` and pad-switch policy.

**Not supported:** No CMD/CLK scope, pad voltage, pull-up, drive-strength, or
signal-integrity measurement exists. CMD1 success does not prove R2 integrity.

### H3 — Rank 3: rail/voltage/power state is wrong or droops during CMD2

**Statement:** VCC (L24) or VCCQ/IO (L11) is not at the intended level, or the
card is underpowered during the R2 response, causing a CRC error.

**Why:**

- The core selects 1.8 V signalling and the pad switch based on board policy
  (`Gta4lEmmcProbe.c:345-352`, `Gta4lEmmc.c:825-864`), but the actual L11 level
  is unmeasured.
- The power acknowledgement is controller-local; `BUS_SUCCESS` does not prove
  the rail (`Gta4lEmmc.c:568-682`; `docs/sdhc-electrical-contract.md:48-52`).
- Stage-3 `pwr=0f` and `pst=00000000` are register states, not voltage
  measurements.

**Discriminating diagnostic:**

1. Measure L24 and L11 at the eMMC/pads before and during the probe. Check PMIC
   state and any shared consumers.
2. Compare with TWRP/Linux boot state on the same tablet.
3. Capture `pwr`, `pst`, and the power log event with the current build.
4. If a rail is wrong or drooping, correct the power path only with owner
   approval; do not change CRC checking.

**Not supported:** No rail measurement exists. The `pwr=0f`/`pst=0` values do not
prove voltage or current. No droop evidence exists.

### H4 — Rank 4: card-side CID/R2 CRC error or card state

**Statement:** The eMMC responds to CMD1 but transmits an invalid CID CRC, or is
in a state where CMD2's R2 response is not reliable.

**Why:**

- CMD1 returns a valid R3 response with bit 31 set, proving the card is present
  and responding (`Gta4lEmmc.c:900-934`).
- CMD2's CID is fixed; a damaged, refurbished, or nonconforming card could have
  a bad CID CRC.
- The board's populated eMMC is identified as `DX68MB`
  (`docs/sdhc-electrical-contract.md:48-52`), but the CID/CSD contents and CRC
  are not recorded.
- TWRP boots from eMMC, which suggests the card works under another software
  stack, but that access was not re-verified in this audit.

**Discriminating diagnostic:**

1. Read the CID/CSD via TWRP/Linux (`/sys/block/mmcblk0/device/cid`, `csd`, or
   `mmc-utils`) if accessible. Record the exact command and output.
2. Compare the known CID with the reconstructed CID from the current build's
   `rsp=` snapshot.
3. If the reconstructed CID matches the known CID but the controller still
   reports CRC, the data bits are intact; the fault is in the CRC byte/check path
   (card or controller).
4. Try another known-good eMMC/board if available. Do not alter or wipe the card.

**Not supported:** The CID/CSD contents and the card's CRC7 are unknown. CMD1
success does not prove the CID CRC is valid. TWRP eMMC access is not re-verified
here.

### H5 — Rank 5: controller R2 CRC-engine/quirk/silicon behavior

**Statement:** The SDHCI v5 controller's CRC engine or R2 handling has a
vendor-specific behavior that makes CMD2 fail even with a valid card response.

**Why:**

- The controller is the same IP family as Linux's `sdhci-msm`; mainline does not
  set a generic R2 CRC-check quirk for this path.
- `SDHCI_QUIRK2_RSP_136_HAS_CRC` affects response layout, not the hardware CRC
  check; the core always applies the non-quirk shift (`Gta4lEmmc.c:202-217`).
- No direct observation of the controller's CRC computation exists.
- If Linux/TWRP reads the CID on the same device, the controller/card path is
  capable under Linux's configuration, which weakens H5 and strengthens H1/H2.

**Discriminating diagnostic:**

1. Check the mainline `sdhci-msm` quirks and DT compatible for this SoC/board.
   Record whether `SDHCI_QUIRK2_RSP_136_HAS_CRC` or a vendor CRC quirk applies.
2. Compare Linux/TWRP CID reads on the same tablet with the core's CMD2 result.
3. Capture `ERROR_INT_STATUS`, `NORMAL_INT_STATUS`, and raw `RESPONSE0..3` with
   the current build.
4. Do not add a quirk or change CRC handling without evidence and owner
   approval.

**Not supported:** No direct controller CRC observation exists. No quirk check
was performed in this audit. The effect of any quirk on the CRC error bit is not
established.

### H6 — Rank 6: host software bug in the CMD2 path

**Statement:** The core's CMD2 encoding, response handling, or error mapping is
wrong.

**Why this is ranked low:**

- Encoding matches mainline (`Gta4lEmmc.c:299`; `sdhci.h:68-71`).
- Long-response normalization matches Linux and Samsung (`Gta4lEmmc.c:202-217`;
  `sdhci.c:1798-1816`; Samsung `sdhci.c:1442-1460`).
- Error mapping matches Linux (`Gta4lEmmc.c:265-284`; `sdhci.c:3330-3336`).
- The simulator covers CMD2 CRC, CC+CRC priority, and raw-response snapshot
  (`harness.c:1202-1213,1742-1790`).

**Discriminating diagnostic:**

- Confirm the captured failure fields are `fail_index=2`, `fail_command=0x0209`,
  `fail_error=0x0002`, and `fail_step=GTA4L_EMMC_STEP_CMD1`. If they are not,
  revisit the software path.
- The simulator is not silicon; it cannot prove the hardware's exact behavior,
  but it excludes the obvious software defects.

**Not supported:** The simulator is a model. It does not prove the silicon
behaves identically. The software path is not the leading explanation, but it is
not proven perfect.

### H7 — Rank 7: clock frequency/programming is not actually 400 kHz

**Statement:** The RCG programming or parent rate is wrong, so CMD2 runs above
the intended identification clock and fails CRC.

**Why this is ranked low:**

- The RCG values match the mainline table (`Gta4lEmmc.h:179-184`;
  `gcc-sm6115.c:1248-1258,1261`).
- The formula yields 400 kHz from 19.2 MHz (`pre_div=23`, dual-edge, `m/n=1/4`).
- The simulator asserts `0x2017/1/0xFC/0xFB` (`harness.c:1333-1345`).
- Stage-3 `cfg=00002017` matches the programmed value.

**Discriminating diagnostic:**

1. Measure CLK at the pad or read the actual parent rate; verify the RCG update
   completed.
2. If the actual frequency is 400 kHz, reject H7.
3. If it is 800 kHz or 1.6 MHz, investigate the RCG update/parent.

**Not supported:** The actual card-clock frequency is not measured.
`cfg=00002017` and `clk=0007` are register states, not a measurement. The parent
rate is assumed from the mainline table.

---

## 7. Discriminating diagnostic plan (owner approval required)

### D0 — Preconditions

- Preserve TWRP on `RECOVERY`; verify the recorded hash `14e11370…` before and
  after any BOOT write.
- Verify EFS/SEC_EFS backups before any flash.
- Do not disable CRC checking.
- Do not write any partition without explicit owner approval.

### D1 — Boot the already-flashed failure-record image

Image: `out/gta4l/ram-root-emmc-probe-failrec-evidence/boot-ram-root-emmc-probe-failrec.img`
(`be30e9f4…`).

This requires no new flash. It captures the failure-record fields present in that
build (`fail_valid/index/argument/command/error/normal/present/step`) but not the
current `rsp=`/`cmd1=` fields.

Expected if the failure persists:

    fail=1/2/0/0x0209/0x0002/<normal>/<present>/<fail_step>

This confirms CMD2 and the CRC error bit for that build.

### D2 — Build and run the current failure-record image

Build from current source (`Gta4lEmmc.c 722110a5…`, `Gta4lEmmcProbe.c d541514b…`).
The current failure path captures:

- `fail=` valid/index/argument/command/error/normal/present/step;
- `rsp=` raw `RESPONSE0..3` before clear;
- `cmd1=` attempts/timeouts/OCR;
- `v3=`, `dll=`, `dst=`, `ddr=`, `mnd=`, `hc=`.

With separate owner approval, write to BOOT only and boot. Compare `ven`, `v3`,
`dll`, `dst`, `ddr`, and `mnd` with the expected current values
(`ven=0x18A9C`, `DLL_RST|DLL_PDN`, `PWRSAVE_DLL` clear).

### D3 — Analyze the raw response

Reconstruct the CID with the shift from `Gta4lEmmc.c:202-217`:

    cid[0] = rsp[3] << 8 | rsp[2] >> 24
    cid[1] = rsp[2] << 8 | rsp[1] >> 24
    cid[2] = rsp[1] << 8 | rsp[0] >> 24
    cid[3] = rsp[0] << 8

Compare with a known-good CID from TWRP/Linux. Classification:

| Observation | Interpretation | Next |
| --- | --- | --- |
| Reconstructed CID matches known CID, controller reports CRC | Response data bits intact; fault is in the CRC byte/check path (card or controller) | H4/H5 |
| Reconstructed CID differs from known CID | Response bits corrupted | H2 (signal/electrical) |
| Reconstructed CID matches, controller does not report CRC | Failure not reproduced on current build | H1 |
| `fail_index != 2` or `fail_command != 0x0209` | CMD2 attribution wrong for this run | Revisit H6 |

The raw snapshot does not directly expose the card's CRC7 byte in the normal
stripped-CRC layout. Do not claim a CRC7 recomputation from `rsp=` alone.

### D4 — Electrical measurements

Measure and record:

- L24 (VCC) at the eMMC/pads, before and during CMD2.
- L11 (VCCQ/IO) at the eMMC/pads, before and during CMD2.
- CMD pull-up rail and pad supply.
- CMD and CLK at the pad, if a scope is available; record frequency, amplitude,
  edges, and any contention.
- PMIC state and shared consumers (L24/L11 are shared with other declarations).

### D5 — Cross-check with TWRP/Linux

If TWRP/Linux can read the eMMC CID/CSD, record the exact command and output. If
Linux reads the CID successfully on the same tablet, the card/rail path is
capable under Linux's configuration; focus on the portable core's host-init
state (H1) or a subtle timing/pad difference. If Linux also fails, focus on
card/electrical (H2/H3/H4).

### D6 — Only then consider code changes

No code change should be made until D1–D5 are recorded. Any code change requires
owner approval and must not disable CRC checking.

---

## 8. Explicit non-supported statements

The following statements are **not supported** by the current evidence and must
not be presented as established:

1. The stage-3 transcript does not contain the failing command index, error
   status, or response words. CMD2 is an inference from the code path and `-8`.
2. The failure-record image is flashed but not run. Its fields are not
   hardware-confirmed.
3. The hardware-tested build `1a471089…` is not current source. The current
   source has not been hardware-tested.
4. `ven=00318a0c` does not prove the current host-init policy was applied. It is
   the pre-POR handoff value plus pad bits.
5. The old build's missing POR/vendor/DLL normalization is not proven to be the
   cause of the CRC error. H1 is a hypothesis.
6. No rail voltage, pad voltage, pull-up, drive-strength, or signal-integrity
   measurement exists. `pwr=0f`, `clk=0007`, and `cfg=00002017` are register
   states, not measurements.
7. The actual card-clock frequency is not measured. The 400 kHz value is derived
   from the RCG constants and the mainline parent rate.
8. The CID/CSD/EXT_CSD contents and the card's CRC7 are unknown. CMD1 success
   proves the card responded to R3, not that its CID CRC is valid.
9. The controller's internal CRC computation and the card's transmitted CRC byte
   are not observed. The raw `RESPONSE0..3` snapshot reconstructs the CID but
   does not directly expose the CRC byte in the normal stripped-CRC layout.
10. The card is not proven absent, unsupported, faulty, counterfeit, or damaged.
11. The failure is not proven to be electrical, card-side, or controller-side.
    The ranked hypotheses are not conclusions.
12. The stage-2 `rc=0` proves controller-local host init, not card communication
    or identification.
13. TWRP/Linux eMMC access is not re-verified in this audit. If used as a
    comparator, record the exact command and output.
14. No evidence supports disabling CRC checking. CRC checking must remain
    enabled.
15. No partition, flash, or wipe action is authorized by this audit. TWRP on
    `RECOVERY` must remain preserved.

---

## 9. Documentation corrections

The corrections below were applied in the working tree after the audit.

| Location | Current text/issue | Correction |
| --- | --- | --- |
| `src/UniXNU-gta4l/docs/emmc-driver.md:456` | Cites `package/Gta4lEmmc.h:208` for `GTA4L_EMMC_ERR_CMD_CRC` | Current line is `Gta4lEmmc.h:242` |
| `src/UniXNU-gta4l/docs/emmc-driver.md:457-461` | Says `ven=00318a0c` repeats the stage-2 host-init state and the policy was applied | The pad bits are set, but `ven` is not the current normalized value (`0x18A9C`); the bullet is stale for current code |
| `src/UniXNU-gta4l/docs/emmc-driver.md:370-376` | Says the failure-record build matches the current source hashes | True at build time; current source has changed. Re-check before citing the flashed image as current |
| `src/UniXNU-gta4l/docs/emmc-driver.md:469-471` | Says the transcript does not establish which command failed | Add: code-path inference narrows it to CMD2 because CMD0 has no response and CMD1 has no CRC; the hardware index is still uncaptured |
| `src/UniXNU-gta4l/package/Gta4lEmmc.h:179` | Comment says "BI_TCXO / 24 * 1 / 4" | Corrected to the actual "BI_TCXO * 2 / 24 * 1 / 4" (= /48) in the current tree |
| `src/UniXNU-gta4l/package/Gta4lEmmc.c:195-198` | Comment says the CRC byte is in the low byte of `RESPONSE0` | Corrected: the normal stripped-CRC layout does not expose the CRC byte there |

The following documentation statements are correct and should be retained:

- `src/UniXNU-gta4l/docs/emmc-driver.md:395`: the hardware failure-report run is
  pending.
- `src/UniXNU-gta4l/docs/emmc-driver.md:502-507,542`: the recorded stage-3
  transcript predates the failure record and DLL fields.
- `src/UniXNU-gta4l/docs/emmc-driver.md:469-471`: the transcript does not
  establish the failing command by itself.

---

## 10. Anchor index

### 10.1 Current source

- `src/UniXNU-gta4l/package/Gta4lEmmc.c`
  - `84-95`: raw response snapshot
  - `97-116`: failure record (snapshot before clear)
  - `118-133`: failure clear
  - `202-217`: long-response normalization
  - `230-243`: status enables / interrupt signalling off
  - `265-284`: error mapping
  - `287-362`: command send/poll/response
  - `511-566`: 400 kHz RCG and clock enable
  - `568-682`: power-on / power acknowledgement
  - `684-700`: pad configuration
  - `702-736`: low-speed vendor/DLL normalization
  - `738-750`: card clock enable
  - `752-897`: controller init
  - `814-818`: POR write
  - `825-864`: HOST_CONTROL2 and pad switch
  - `866`: low-speed normalization call
  - `872-891`: clock, power, pads, card clock
  - `900-934`: CMD1 / card ready
  - `936-961`: response validation
  - `1041-1163`: init sequence
  - `1058-1060`: CMD0
  - `1073-1075`: CMD2
  - `1082-1085`: CID capture
- `src/UniXNU-gta4l/package/Gta4lEmmc.h`
  - `101`: vendor POR `0xA9C`
  - `128-144`: error bits/mask
  - `146-154`: command flags
  - `160-166`: power-control bits
  - `179-184`: 400 kHz constants
  - `187`: SDC1 pad value
  - `207`: CMD2
  - `242`: `GTA4L_EMMC_ERR_CMD_CRC -8`
  - `324-328`: failure response/CMD1 fields
- `src/UniXNU-gta4l/package/Gta4lEmmcProbe.c`
  - `24-26`: SDHCI/GCC/TLMM bases
  - `345-352`: fixed 1.8 V / pad policy
  - `354-358`: stage dispatch
  - `425-447`: failure panic with `fail=`, `rsp=`, `cmd1=`
  - `468`: HOST REPORT
  - `477`: IDENT REPORT

### 10.2 Simulator

- `src/UniXNU-gta4l/scripts/sim-gta4l-emmc/harness.c`
  - `44`: initial vendor `0x00318A0C`
  - `527-543`: raw response encoding
  - `650-651`: CRC injection
  - `1073-1081`: simulator init
  - `1202-1213`: `cmd-crc`, `cmd-crc-with-cc`, `cmd-crc-snapshot`
  - `1333-1345`: RCG CFG/M/N/D checks
  - `1362-1392`: 1.8 V, pad, DLL, vendor normalization checks
  - `1699-1701`: expected `CMD_CRC` scenarios
  - `1742-1763`: CRC capture and CC+CRC priority
  - `1765-1790`: raw response snapshot
  - `1795-1801`: CMD1 retry counters
- `src/UniXNU-gta4l/scripts/test-gta4l-emmc.py`
  - scenario list, source policy, and mutation controls cover the same rules.

### 10.3 Linux/Samsung references

- `/tmp/opencode/linux-pin/sdhci.h:68-71`: response-type constants
- `/tmp/opencode/linux-pin/sdhci.c:1711-1712`: `RESP_LONG` for `MMC_RSP_136`
- `/tmp/opencode/linux-pin/sdhci.c:1718-1719`: `CMD_CRC`
- `/tmp/opencode/linux-pin/sdhci.c:1798-1816`: `sdhci_read_rsp_136()`
- `/tmp/opencode/linux-pin/sdhci.c:3330-3336`: CRC → `-EILSEQ`
- `/tmp/opencode/linux-pin/sdhci-msm.c:76`: `CORE_VENDOR_SPEC_POR_VAL 0xa9c`
- `/tmp/opencode/linux-pin/sdhci-msm.c:203-232`: v5 offset table, `core_ddr_config = 0x25c`
- `/tmp/opencode/linux-pin/sdhci-msm.c:803-840`: `msm_hc_select_default()`
- `/tmp/opencode/linux-pin/sdhci-msm.c:1047-1058`: DDR config and `CMDIN_RCLK_EN` under HS400
- `/tmp/opencode/linux-pin/sdhci-msm.c:1397-1420`: <=100 MHz DLL reset/power-down
- `/tmp/opencode/linux-pin/sdhci-msm.c:1718-1755`: power handler
- `/tmp/opencode/linux-pin/sdhci-msm.c:2323-2325`: HOST_CONTROL2 → REQ_IO
- `/tmp/opencode/linux-pin/sdhci-msm.c:2412-2417`: pad switch
- `/tmp/opencode/linux-pin/sdhci-msm.c:2452-2480`: voltage switch
- `/tmp/opencode/linux-pin/sdhci-msm.c:2596-2598`: `DDR_CONFIG_POR_VAL`
- `/tmp/opencode/linux-pin/sdhci-msm.c:2827-2828`: `updated_ddr_cfg` for minor `>= 0x49`
- `/tmp/opencode/samsung-src/CXG1/drivers/mmc/host/sdhci.c:1442-1460`: R2 shift
- `/tmp/opencode/samsung-src/CXG1/drivers/mmc/host/sdhci-msm.c:101`: Samsung POR `0xA1C`
- `/tmp/opencode/linux-verify/gcc-sm6115.c:1248-1258`: SDCC1 apps table
- `/tmp/opencode/linux-verify/gcc-sm6115.c:1261`: `cmd_rcgr = 0x38028`

### 10.4 Hardware evidence and project docs

- `out/gta4l/ram-root-emmc-probe-evidence/host-report-stage2.txt:1-3`: stage-2 PASS
- `out/gta4l/ram-root-emmc-probe-stage3-evidence/host-report-stage3.txt:1-3`: stage-3 FAIL
- `out/gta4l/ram-root-emmc-probe-failrec-evidence/flash-record.json:1-26`: failure-record flash, run pending
- `src/UniXNU-gta4l/docs/emmc-driver.md:328,360`: stage-2/3 kernel hash
- `src/UniXNU-gta4l/docs/emmc-driver.md:383`: failure-record kernel hash
- `src/UniXNU-gta4l/docs/emmc-driver.md:395`: hardware run pending
- `src/UniXNU-gta4l/docs/emmc-driver.md:450-471`: stage-3 interpretation
- `src/UniXNU-gta4l/docs/emmc-driver.md:502-507,542`: transcript predates failure record
- `src/UniXNU-gta4l/docs/sdhci-handoff-audit.md:130-158`: handoff snapshot
- `src/UniXNU-gta4l/docs/sdhc-electrical-contract.md:48-52`: L24/L11/pad contract
