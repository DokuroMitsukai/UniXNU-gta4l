# gta4l native SDHCI v5 / eMMC core

Status: host-tested implementation `[S]`; no device run `[U]`. The core is
opt-in and has not been booted on the tablet. The on-device probe is a device
action and requires the owner's written approval plus a verified EFS/CPEFS
backup; the `sdhc-bus-power-activation` NO-GO remains for that operation.

This document records the native eMMC implementation. It is deliberately small:
one-bit bus, legacy timing, polling, single 512-byte PIO reads and no card
writes. It is the host-testable base for later block-device publication, DMA,
interrupts and higher-speed modes.

## 1. Files

| Path | Purpose |
| --- | --- |
| `package/Gta4lEmmc.h` | Register map, command/error/result constants, host struct and ops interface |
| `package/Gta4lEmmc.c` | Portable controller, clock, power, enumeration and PIO-read core |
| `package/Gta4lEmmcProbe.c` | Opt-in staged XNU probe: maps MMIO, runs the core and reports |
| `scripts/sim-gta4l-emmc/harness.c` | Simulated controller and eMMC card with injectable failures and independent register/command checks |
| `scripts/test-gta4l-emmc.py` | Compiles the core against the simulator, runs 40 scenarios and 16 mutation controls |
| `scripts/build-gta4l-emmc-probe.py` | Builds `libGta4lEmmcProbe.a` with the `bsd_init.o` compile record |
| `patches/xnu/gta4l-emmc-probe.patch` | Adds the opt-in preflight/run hooks to `bsd_init` |
| `board.toml` | Links `build-gta4l-emmc-probe.py` into the kernel archive list |

## 2. Sequence

### Controller, status and clock

1. Validate the Qualcomm v5 MCI version (major nibble 1).
2. Suppress normal/error interrupt enables and signal enables with 16-bit
   accesses to `0x34`, `0x36`, `0x38` and `0x3A`.
3. Software-reset the controller through the byte register at `0x2F` and poll
   it with byte reads until it self-clears.
4. Program the maximum timeout and one-bit host control.
5. Read `HOST_CONTROL2`, clear only the UHS-mode and execute fields, and select
   the 1.8 V signalling bit for the board's fixed 1.8 V VCCQ rail. Preserve the
   other fields instead of clearing the register.
6. Read `CORE_VENDOR_SPEC` and apply the pad-supply policy explicitly. For the
   fixed-1.8 V rail the caller sets both the pad-switch enable and the pad-switch
   bit; for 3.3 V signalling both are cleared. This is a board policy, not a
   reproduction of the mainline gta4l DT state: that DT declares no SDHC1
   `vqmmc` consumer, so `sdhci_msm_set_regulator_caps()` derives `caps == 0` and
   the Linux probe leaves both bits at the `CORE_VENDOR_SPEC_POR_VAL` (`0xa9c`)
   value. The policy follows the sdhci-msm fixed-1.8 V comment instead. See §7.
7. Enable the polling status bits (`CC`, `TC`, `BRR` and the error mask) while
   leaving both signal-enable registers at zero.
8. Stop the card clock, enable the SDCC1 AHB and APPS branches and poll
   `CLK_OFF` clear.
9. Program the GCC SDCC1 apps RCG to 400 kHz:

   | Field | Value |
   | --- | --- |
   | Source select | `0` (`BI_TCXO`, 19.2 MHz) |
   | Divider field | `23` (`0x17`) |
   | M | `1` |
   | N | `0xFC` (encoded `~(4-1)`) |
   | D | `0xFB` (encoded `~4`) |
   | CFG | `0x2017` (dual-edge MND mode) |

   The values are derived from the pinned `gcc-sm6115.c` frequency entry
   `F(400000, P_BI_TCXO, 12, 1, 4)` and the `clk-rcg2` encoding, not from a
   guessed divider.
10. Set `CMD_UPDATE` and poll it clear.
11. Enable the internal clock and poll `INT_STABLE`.

### Power and pads

1. Program `HOST_CONTROL2` with the fixed-1.8 V signalling bit and record the
   requested IO transition. The controller raises an IO request only when the
   `VDD_180` bit changes, so an unchanged bit means no IO request is due and the
   level is already correct. This is the same condition Linux uses in
   `sdhci_msm_check_power_status()`.
2. Write `POWER_CONTROL = bus power | 3.3 V`.
3. Poll `CORE_PWRCTL_STATUS` for up to 500 ms. Bus power and IO voltage are
   independent controller requests: the `HOST_CONTROL2` write can raise an IO
   request before, together with, or after the bus request.
4. Handle each observed request separately. `BUS_OFF` is a failure. Each
   observed set of bits is cleared with the documented 10-attempt retry;
   exhausting the retries is a failure. `BUS_ON` is acknowledged with both
   `BUS_SUCCESS` and `IO_SUCCESS`, matching Linux, which treats bus-on as an
   implicit IO-high transition. IO-low/high is acknowledged with `IO_SUCCESS`.
5. Track completion explicitly: success requires an observed `BUS_ON` and,
   when the `HOST_CONTROL2` write changed `VDD_180`, an observed IO request at
   the requested level (IO-low for 1.8 V, IO-high for 3.3 V). A wrong-level IO
   request is acknowledged but does not satisfy the transition; the loop keeps
   waiting. A zero status read no longer counts as IO completion on its own,
   and an IO request that arrives late is handled before the card clock starts.
   An IO-only request is `GTA4L_EMMC_ERR_POWER`, not a completed bus power-on.
   A request that remains set after the acknowledgement is also a failure.
6. If the loop reaches its iteration limit without a quiet read after both
   completions, return `GTA4L_EMMC_ERR_POWER`; exhaustion is never a success.
   The ack is a controller-local software ack, not a measurement of the rail.
7. Write the UEFI-derived active SDC1 pad value `0x9FE4` to the SDC1 QDSD
   register at TLMM `0x00575000`.
8. Wait 5 ms for power and pads to settle, then enable the card clock. This is
   the Linux 3.3 V regulator settle bound applied to the retained rail; the core
   also keeps a 1 ms post-enable delay.

### Card

| Step | Command | Notes |
| --- | --- | --- |
| 1 | `CMD0` | Reset the card |
| 2 | `CMD1` | Poll OCR bit 31, 100 ms per attempt, 20 attempts |
| 3 | `CMD2` | Read the 128-bit CID; reject all-zero/all-ones |
| 4 | `CMD3` | Assign RCA `1` in the argument; check the R1 status |
| 5 | `CMD9` | Read the 128-bit CSD with the assigned RCA |
| 6 | `CMD7` | Select the card with the assigned RCA |
| 7 | `CMD13` | Poll until state `TRAN` and `READY_FOR_DATA` |
| 8 | `CMD8` | Read 512-byte EXT_CSD |
| 9 | `CMD17` | Read one 512-byte block |

eMMC `CMD3` is `SET_RELATIVE_ADDR`: the host assigns the RCA through the
command argument and the response is R1 status. The core does not derive an RCA
from the response.

Long (R2) responses are normalized from the SDHCI `RESPONSE3..RESPONSE0`
register layout, least-significant word first with the CRC byte in the low byte
of `RESPONSE0`, into the MMC big-endian word order (`cid[0]`/`csd[0]` is bits
127:96). This is the same shift the Linux `sdhci_read_rsp_136()` path uses. The
response-type test uses a two-bit mask, so `RESP_SHORT_BUSY` (`CMD7`) is not
mistaken for a 128-bit response.

The core parses `EXT_CSD_REV` at byte 192, `BUS_WIDTH` at byte 183,
`PARTITION_CONFIG` at byte 179, `DATA_SECTOR_SIZE` at byte 61 and `SEC_COUNT` at
bytes 212-215. Initialization requires 512-byte data sectors
(`DATA_SECTOR_SIZE == 0`), a sector-addressed card (`SEC_COUNT > 2 GiB / 512`)
and the user-area partition access bits. A card that does not meet any condition
returns `GTA4L_EMMC_ERR_UNSUPPORTED` before any sector read.

## 3. Completion and error rules

- A command succeeds only on `CC`; a data read succeeds only when `CC`, `BRR`
  and `TC` are all observed and exactly the requested bytes were read.
- Draining the expected byte count without `TC` is a failure.
- `BRR` without `TC`, `TC` without `BRR` and stale status are failures.
- Command/data timeout, CRC, index, end-bit and other controller errors map to
  distinct result codes.
- Card R1 errors are checked separately from controller status.
- Every wait has a microsecond bound and an iteration cap, and every poll loop
  uses the documented 10 µs step.
- `CMD17` is rejected when the block is at or beyond the discovered capacity.
- The core never submits `CMD12`, `CMD18`, `CMD24`, `CMD25`, DMA, CQE, ICE or
  tuning operations.

## 4. Host tests

```sh
python3.11 -B scripts/test-gta4l-emmc.py
```

The test compiles `package/Gta4lEmmc.c` with `scripts/sim-gta4l-emmc/harness.c`
using `-std=c11 -Wall -Wextra -Werror`. The simulator independently enforces the
SDHCI register widths, command encoding, status-enable gating, RCA assignment,
block bounds, the power-settle interval and the separate bus/IO request
sequence, so the production core cannot pass by sharing a bug with the model.

It covers:

- successful enumeration, CID/CSD/EXT_CSD parse, capacity and user partition;
- block 0, block 1, repeated, last-block and out-of-range reads;
- block-address overflow and read-before-init;
- command timeout, command CRC and command index errors;
- data timeout, data CRC, missing `TC` and missing `BRR`;
- card-not-ready, bad CID, bad CSD and CMD3 R1 errors;
- reset timeout, clock timeout, RCG busy, power-off, no power request and a
  stuck power-status clear;
- an IO-only power request, a delayed bus request, a bus-then-IO sequence whose
  late IO request arrives after the old quiet window, a wrong-level IO request
  that never becomes correct, a wrong-level request followed by the correct
  level, and a churn case where requests never stop so the loop must fail on
  exhaustion;
- a bus-only request (no `VDD_180` change) whose acknowledgement must still be
  `BUS_SUCCESS | IO_SUCCESS`, matching Linux;
- a 4 KiB `DATA_SECTOR_SIZE` card;
- small-card and non-user-partition rejection;
- 1.8 V and 3.3 V signalling, plus the explicit pad-switch-on and pad-switch-off
  policies;
- EXT_CSD data read;
- mutation controls for the 400 kHz CFG, M and pad values, the command data bit,
  the pad-switch and 1.8 V bits, the settle time, an unaligned reset read, a
  disabled polling status, the raw long-response layout, a missing bus-on
  requirement, a bus-only acknowledgement that omits `IO_SUCCESS`, a missing
  IO-completion requirement, a wrong-level IO request accepted as completion,
  a power loop that treats exhaustion as success and a missing 4 KiB sector
  check.

The test also checks the source policy: only the injected ops are used, the core
contains no DMA, CQE, ICE, tuning or card-write command, the poll loops use the
10 µs step, and the probe uses width-correct MMIO and aborts on a stalled timing
source.

## 5. XNU integration

The probe is disabled unless the boot argument `gta4l-emmc-probe=<stage>` is
present:

| Stage | Behavior |
| --- | --- |
| `1` | Full: controller/host init, identification and block 0/1 reads |
| `2` | Controller/host init only; no card command, clock-gated card or sector read |
| `3` | Host init plus identification; no sector read |

It conflicts with `gta4l-sdhci-audit`, `gta4l-sdhci-snapshot` and
`gta4l-sdhci-host-init`; the preflight panics before mapping if more than one is
requested.

The probe reads `CNTFRQ_EL0` for its delay loop and keeps a 19.2 MHz fallback.
A delay loop that exhausts its spin bound panics instead of silently skipping
the settle interval.

The probe sets the fixed-1.8 V signalling policy (`fixed_1v8 = 1`) and the
1.8 V pad-supply policy (`pad_pwr_switch = 1`) explicitly because the board's
VCCQ rail (L11) is 1.8 V. The pad policy is a deliberate departure from the
mainline gta4l DT-derived register state; see §2 and §7.

The archive is built with the same compile record as `bsd_init.o`, so the core
and probe use the selected XNU headers and ABI:

```sh
./unixnu build kernel -D ../UniXNU-gta4l
./unixnu build payload -D ../UniXNU-gta4l
./unixnu bootimg -D ../UniXNU-gta4l
```

The build report at `build/gta4l-emmc-probe/build-report.json` records the
source/header/object hashes and the archive hash. A bootstrap build with no
compile record links a no-op stub and must be rebuilt before use.

### 5.1 Current diagnostic image

The stage-2 image was built on 2026-10-31 from this tree:

```sh
./unixnu build kernel -D ../UniXNU-gta4l
python3.11 -B scripts/build-gta4l-diagnostic-payload.py \
  --device . --unixnu ../UniXNU --uefi ../Mu-Silicium \
  --kernel ../UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l/kernel/kernel.macho \
  --data-ramdisk ../../out/gta4l/ram-root-sdhci-audit-evidence/root.img \
  --trust-cache ../../out/gta4l/ram-root-sdhci-audit-evidence/trustcache.bundle \
  --owner-trust-cache --unique-chip-id 0x1122334455667788 \
  --diagnostic emmc-probe-2 --kernel-arg rd=md0 --kernel-arg sheng-pmgr=2 \
  --output ../../out/gta4l/ram-root-emmc-probe-evidence/payload.img \
  --manifest ../../out/gta4l/ram-root-emmc-probe-evidence/payload.manifest.json
python3.11 -B scripts/package-gta4l-diagnostic.py \
  --baseline ../../out/gta4l/boot/boot-ram-root-sdhci-v5.img \
  --payload ../../out/gta4l/ram-root-emmc-probe-evidence/payload.img \
  --payload-sha256 e3c49c4bdcfb34523c37bdab26f1810c26f343650aa651dcbd98d42cd868e77a \
  --expect-diagnostic emmc-probe-2 \
  --output ../../out/gta4l/ram-root-emmc-probe-evidence/boot-ram-root-emmc-probe-stage2.img \
  --manifest ../../out/gta4l/ram-root-emmc-probe-evidence/boot-ram-root-emmc-probe-stage2.json
```

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| `kernel.macho` | 51,081,216 | `43bb89f235955dde708801353399d51ae71f95ba1054c2abe8bb92fa2d9a08a7` |
| `payload.img` (80 MiB, stage 2) | 83,886,080 | `e3c49c4bdcfb34523c37bdab26f1810c26f343650aa651dcbd98d42cd868e77a` |
| `boot-ram-root-emmc-probe-stage2.img` | 85,688,320 | `278651bdde382949aedae9b508789f8ef393279d77b2a54bac20d9387d7919a6` |

The payload contract is 7/7 and the boot-image gate is 18/18. The payload's
kernel arguments include `gta4l-emmc-probe=2`. The image has not been flashed;
`hardware_tested` is false. Stage 2 still enables bus power and the card clock,
so a run requires the owner's written approval, a verified EFS/CPEFS backup, a
known-good BOOT restoration image and the exact procedure; TWRP on `RECOVERY`
stays untouched.

## 6. Expected report

On a full success the probe panics with a report containing:

- MCI version, RCA, capacity, EXT_CSD revision, bus width, OCR, EXT_CSD sum,
  `HOST_CONTROL2`, `CORE_VENDOR_SPEC` and power status;
- CID and CSD words;
- block 0 and block 1 sums and selected words, including the protective MBR
  signature area and the GPT header fields.

Stage 2 stops with `GTA4L-EMMC HOST REPORT` and stage 3 with
`GTA4L-EMMC IDENT REPORT`. On failure the probe reports the stage, result code,
MCI version, power/clock/`HOST_CONTROL2`/vendor registers, power-control status,
GCC CFG/CMD and the last log event. The report is a bring-up checkpoint, not a
storage or filesystem result.

## 7. Limits and residual risks

- No device run exists; the probe has not been booted on the tablet `[U]`.
- The port uses retained rails. It does not measure L22/L5/L24/L11 or the pad
  levels, and it does not prove retention across suspend or shutdown `[U]`.
- The 1.8 V signalling and pad-switch selection is a documented board policy
  derived from the L11 rail range; it is not a measurement `[S]/[U]`. It is a
  deliberate departure from the mainline gta4l DT-derived state, where no SDHC1
  `vqmmc` consumer exists, `caps == 0`, and `sdhci_msm_set_regulator_caps()`
  leaves both vendor pad bits at the `0xa9c` POR value. The policy follows the
  sdhci-msm fixed-1.8 V comment (keep the pad switch set so the pads reflect the
  actual rail) rather than that DT-derived state. A first test should treat a
  pad-related failure as a policy question, not as proof that the rail is wrong
  `[S]/[U]`.
- The power acknowledgement is a controller-local software ack; `BUS_SUCCESS`
  does not prove the rail voltage. The core now requires an observed `BUS_ON`
  completion and completion of the IO transition requested through
  `HOST_CONTROL2`, and it fails if the power loop is exhausted. A *new* power
  request raised after both completions and after the card clock starts still
  has no interrupt path `[U]`.
- No DMA, SMMU programming, CQE, ICE, tuning, high-speed mode, multi-block read
  or write path exists `[U]`.
- No block-device publication, mount, root switch or persistent Data path
  exists `[U]`.
- microSD/SDHC2 is not implemented; only SDHC1/eMMC is in scope `[U]`.
- The exact vendor source/binary correspondence and the firmware runtime slot
  table remain `[U]`.

The next device step is the approved staged probe run: stage 2 first, then
stage 3, then the full read. The next software step is a read-only
`IOBlockStorageDevice` publication over the same core, followed by multi-block
reads and a documented stop/recovery policy.
