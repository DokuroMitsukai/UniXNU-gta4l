# gta4l native SDHCI v5 / eMMC core

Status: host-tested implementation `[S]`; stage 2 (controller/host init only)
ran on the tablet and reported success `[C]`; stage 3 (identification) ran and
stopped at a command CRC error with no identification report `[C]`; card
identification, sector reads and the later stages remain `[U]`. The core is
opt-in. Both runs were made under the owner's approval for that exact
procedure, with verified EFS/SEC_EFS backups and a known-good BOOT restoration
image. The
`sdhc-bus-power-activation` NO-GO still applies to any operation not separately
approved.

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
| `scripts/test-gta4l-emmc.py` | Compiles the core against the simulator, runs 56 scenarios and 33 mutation controls |
| `scripts/sim-gta4l-emmc-probe/harness.c` | Simulated SDHCI/GCC windows and kernel stubs for the probe failure-report path |
| `scripts/test-gta4l-emmc-probe.py` | Runs the probe failure report, seven scenarios and four mutation controls |
| `scripts/build-gta4l-emmc-probe.py` | Builds `libGta4lEmmcProbe.a` with the `bsd_init.o` compile record |
| `patches/xnu/gta4l-emmc-probe.patch` | Adds the opt-in preflight/run hooks to `bsd_init` |
| `board.toml` | Links `build-gta4l-emmc-probe.py` into the kernel archive list |

## 2. Sequence

### Controller, status and clock

1. Assert and release the SDCC1 block reset through the GCC BCR, waiting 200 µs
   in each state.
2. Enable the SDCC1 AHB and APPS branches and poll `CLK_OFF` clear.
3. Validate the Qualcomm v5 MCI version (major nibble 1).
4. Suppress normal/error interrupt enables and signal enables with 16-bit
   accesses to `0x34`, `0x36`, `0x38` and `0x3A`.
5. Software-reset the controller through the byte register at `0x2F` and poll
   it with byte reads until it self-clears.
6. Restore `CORE_VENDOR_SPEC` to the documented POR value `0xA9C`.
7. Program the maximum timeout and one-bit host control.
8. Read `HOST_CONTROL2`, clear only the UHS-mode and execute fields, and select
   the 1.8 V signalling bit for the board's fixed 1.8 V VCCQ rail. Preserve the
   other fields instead of clearing the register.
9. Read `CORE_VENDOR_SPEC` and apply the pad-supply policy explicitly. For the
   fixed-1.8 V rail the caller sets both the pad-switch enable and the pad-switch
   bit; for 3.3 V signalling both are cleared. This is a board policy, not a
   reproduction of the mainline gta4l DT state: that DT declares no SDHC1
   `vqmmc` consumer, so `sdhci_msm_set_regulator_caps()` derives `caps == 0` and
   the Linux probe leaves both bits at the `CORE_VENDOR_SPEC_POR_VAL` (`0xa9c`)
   value. The policy follows the sdhci-msm fixed-1.8 V comment instead. See §7.
10. On the low-speed path, before the 400 kHz card clock starts, clear
    `PWRSAVE_DLL` in `CORE_VENDOR_SPEC3` (`0x250`), set `DLL_RST` then
    `DLL_PDN` in `CORE_DLL_CONFIG` (`0x200`), clear the `HC_SELECT_IN` override,
    select the default `MCLK_SEL`, and clear `CLK_PWRSAVE` so the feedback clock
    is used.
11. Enable the polling status bits (`CC`, `TC`, `BRR` and the error mask) while
    leaving both signal-enable registers at zero.
12. Stop the card clock and program the GCC SDCC1 apps RCG to 400 kHz:

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
13. Set `CMD_UPDATE` and poll it clear.
14. Enable the internal clock and poll `INT_STABLE`.

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
5. Track the latest acknowledged IO level, not a one-way "seen" flag. Within a
   status word apply the Linux handler order: `BUS_ON` implies IO-high, an
   IO-low request overrides it, and an IO-high request overrides IO-low.
   Success requires the latest level to equal the level requested by the
   `HOST_CONTROL2` `VDD_180` write (IO-low for 1.8 V, IO-high for 3.3 V). A
   later opposite level, a later standalone `BUS_ON` or contradictory bits
   therefore invalidate an earlier completion and the loop keeps waiting.
   If the `VDD_180` bit did not change, no IO request is due, but success still
   accepts only an unobserved (zero) level or the desired level; a wrong level
   raised later is not excused by the unchanged bit. The acknowledgement
   reports completion and does not clear the request latches: only
   `CORE_PWRCTL_CLEAR` does, so a request raised between clear and ack is still
   observed. Wrong-level requests are acknowledged but do not complete the
   transition. A zero status read alone is not completion; a late request is
   handled before the card clock starts. An IO-only request is
   `GTA4L_EMMC_ERR_POWER`, not a completed bus power-on. A request that remains
   set after the acknowledgement is also a failure.
6. If the loop reaches its iteration limit without a quiet read after both
   completions, return `GTA4L_EMMC_ERR_POWER`; exhaustion is never a success.
   The ack is a controller-local software ack, not a measurement of the rail.
7. Write the UEFI-derived active SDC1 pad value `0x9FE4` to the SDC1 QDSD
   register at TLMM `0x00575000`.
8. Wait 10 ms (`power_delay_ms` default) for power and pads to settle, then
   enable the card clock. The core also keeps a 1 ms post-enable delay, which
   exceeds the 74-clock/1 ms minimum at 400 kHz.

### Card

| Step | Command | Notes |
| --- | --- | --- |
| 1 | `CMD0` | Reset the card; 1 ms before, 2 ms after |
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
- The terminal command failure is recorded before the status latches are
  cleared: command index, argument, encoded command register, error/normal
  status, present state and the last completed protocol step. Later failures do
  not overwrite it. A busy `CMD1` timeout is expected and retried, so each
  retry retires the previous record; a recovered timeout therefore cannot mask
  the command that ends initialization, and a final `CMD1` timeout still leaves
  its own record.
- The host tracks whether the AHB/APPS branches were enabled. The probe reads
  SDHCI registers on the failure path only when they were, so a clock-enable
  failure cannot turn the diagnostic into an access to an unclocked controller;
  the GCC state is always reported.
- Card R1 errors are checked separately from controller status.
- Every wait has a microsecond bound and an iteration cap, and every poll loop
  uses the documented 10 µs step.
- `CMD17` is rejected when the block is at or beyond the discovered capacity.
- The core never submits `CMD12`, `CMD18`, `CMD24`, `CMD25`, DMA, CQE, ICE or
  tuning operations.

## 4. Host tests

```sh
python3.11 -B scripts/test-gta4l-emmc.py
python3.11 -B scripts/test-gta4l-emmc-probe.py
```

The test compiles `package/Gta4lEmmc.c` with `scripts/sim-gta4l-emmc/harness.c`
using `-std=c11 -Wall -Wextra -Werror`. The simulator independently enforces the
SDHCI register widths, command encoding, status-enable gating, RCA assignment,
block bounds, the power-settle interval and the separate bus/IO request
sequence, so the production core cannot pass by sharing a bug with the model.
It also enforces the SDCC1 reset pulse and deassert, that the first SDHCI access
follows verification of both branch clocks, and that `DLL_RST` is written before
`DLL_PDN`; these checks use independent GCC offsets and bit definitions.

It covers:

- successful enumeration, CID/CSD/EXT_CSD parse, capacity and user partition;
- block 0, block 1, repeated, last-block and out-of-range reads;
- block-address overflow and read-before-init;
- command timeout, command CRC at CMD0/CMD1/CMD2 and command index errors;
- a recovered `CMD1` busy timeout followed by a `CMD2` CRC failure, where the
  report must carry the terminal `CMD2` failure rather than the retired timeout;
- a `CMD2` CRC failure reported together with command complete, where the error
  must take precedence and the failed CID must not be promoted, and a `CMD2` CRC
  failure whose latch clear invalidates the response, where the raw
  `RESPONSE0..3` snapshot must already be recorded;
- a recovered `CMD1` busy timeout that completes initialization, which must end
  with no failure record, and an exhausted `CMD1` retry loop, which must keep
  the final timeout record for `CMD1`;
- data timeout, data CRC, missing `TC` and missing `BRR`;
- card-not-ready, bad CID, bad CSD and CMD3 R1 errors;
- reset timeout, clock timeout, RCG busy, power-off, no power request and a
  stuck power-status clear;
- an AHB or APPS branch that never comes up after the block reset, where the
  core must return `ERR_CLOCK`, leave the clock-gate flag clear, record no
  command failure and issue no SDHCI access;
- an IO-only power request, a delayed bus request, a bus-then-IO sequence whose
  late IO request arrives after the old quiet window, a wrong-level IO request
  that never becomes correct, a wrong-level request followed by the correct
  level, a correct level followed by a later wrong level, a later standalone
  `BUS_ON` after a completed IO-low transition, a status word with both IO
  levels, an unchanged `VDD_180` followed by a standalone `BUS_ON`, a
  wrong-level request injected on the ack write, and a churn case where
  requests never stop so the loop must fail on exhaustion;
- a bus-only request (no `VDD_180` change) whose acknowledgement must still be
  `BUS_SUCCESS | IO_SUCCESS`, matching Linux;
- a 4 KiB `DATA_SECTOR_SIZE` card;
- small-card and non-user-partition rejection;
- 1.8 V and 3.3 V signalling, plus the explicit pad-switch-on and pad-switch-off
  policies;
- vendor normalization from an HS400 MCLK selection or an enabled
  `HC_SELECT_IN` override, and the `PWRSAVE_DLL` clear;
- EXT_CSD data read;
- mutation controls for the SDCC1 block reset, the reset pulse duration, a
  missing reset deassert, an SDHCI access before the branch clocks are verified,
  `DLL_PDN` before `DLL_RST`, the vendor POR value and MCLK default,
  `PWRSAVE_DLL`, the 400 kHz CFG, M and pad values, the command data bit, the
  pad-switch and 1.8 V bits, the power/card-clock/CMD0 settle times, a wrong DLL
  access width, an unaligned reset read, a disabled polling status, the raw
  long-response layout, a missing bus-on requirement, a bus-only
  acknowledgement that omits `IO_SUCCESS`, a missing IO-completion requirement,
  a wrong-level IO request accepted as completion, a first-level-wins sticky
  level, opposite IO bits accepted as completion, an unchanged-bit level
  shortcut, a power loop that treats exhaustion as success, a retained
  recovered `CMD1` timeout, a missing raw-response snapshot, a clock-gate flag
  set before the branch clocks are verified and a missing 4 KiB sector check.

The test also checks the source policy: only the injected ops are used, the core
contains no DMA, CQE, ICE, tuning or card-write command, the poll loops use the
10 µs step, and the probe uses width-correct MMIO and aborts on a stalled timing
source.

`scripts/test-gta4l-emmc-probe.py` executes the probe failure-report path. It
copies `package/Gta4lEmmcProbe.c` and replaces only the generic-counter and
barrier instructions and the six MMIO helpers with recording hooks; the stage
selection, conflict check, clock guard, register offsets and panic strings are
the production code. Seven scenarios cover a disabled probe, an MMIO-map
failure, a conflicting diagnostic argument, a stuck AHB branch, a stuck APPS
branch, a verified-clock version failure and a verified-clock RCG/clock timeout.
A stuck branch must panic with `clocks=0` and the GCC state while issuing no
SDHCI read or write; the version-failure control must report `clocks=1` and read
each diagnostic SDHCI register (MCI version, power, clock, `HOST_CONTROL2`,
vendor, DLL config/status and power status) exactly once, which proves the
access counter detects real reads. The clock-timeout run must panic with
`rc=-5`, `clocks=1` and the formatted `cfg=00002017`,
`mnd=00000001/000000fc/000000fb`, `cmd=00000001` and branch/reset values, and
must read the diagnostic-only GCC M/N/D/BCR offsets exactly once each. Four
mutations insert an unguarded SDHCI read before the guard, replace the guard
with an unconditional branch, suppress a GCC report read and suppress an SDHCI
report read; each must be rejected for the expected reported reason, not merely
by a nonzero exit.

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
  --payload-sha256 2953e90220f1e2d9341fc154f3f2eb6523ab2539be68404a3beffa8ee4702633 \
  --expect-diagnostic emmc-probe-2 \
  --output ../../out/gta4l/ram-root-emmc-probe-evidence/boot-ram-root-emmc-probe-stage2.img \
  --manifest ../../out/gta4l/ram-root-emmc-probe-evidence/boot-ram-root-emmc-probe-stage2.json
```

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| `kernel.macho` | 51,081,216 | `1a4710892e49fa96c053de8f33f1b4b5d89a36fc3d688febd6fa0f7e4a89003a` |
| `payload.img` (80 MiB, stage 2) | 83,886,080 | `2953e90220f1e2d9341fc154f3f2eb6523ab2539be68404a3beffa8ee4702633` |
| `boot-ram-root-emmc-probe-stage2.img` | 85,688,320 | `4f2252df6bbd5d9c7f05ef97c0041da9acdc47212debf31a10334aefa45a12e5` |

The payload contract is 7/7 and the boot-image gate is 18/18. The payload's
kernel arguments include `gta4l-emmc-probe=2`. The build manifest keeps
`hardware_tested=false` as built; the image was subsequently flashed to BOOT
only for the stage-2 run in §5.2. TWRP on `RECOVERY` was not touched.

The stage-3 image was built from the same kernel on 2026-10-31:

```sh
python3.11 -B scripts/build-gta4l-diagnostic-payload.py \
  --device . --unixnu ../UniXNU --uefi ../Mu-Silicium \
  --kernel ../UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l/kernel/kernel.macho \
  --data-ramdisk ../../out/gta4l/ram-root-sdhci-audit-evidence/root.img \
  --trust-cache ../../out/gta4l/ram-root-sdhci-audit-evidence/trustcache.bundle \
  --owner-trust-cache --unique-chip-id 0x1122334455667788 \
  --diagnostic emmc-probe-3 --kernel-arg rd=md0 --kernel-arg sheng-pmgr=2 \
  --output ../../out/gta4l/ram-root-emmc-probe-stage3-evidence/payload.img \
  --manifest ../../out/gta4l/ram-root-emmc-probe-stage3-evidence/payload.manifest.json
python3.11 -B scripts/package-gta4l-diagnostic.py \
  --baseline ../../out/gta4l/boot/boot-ram-root-sdhci-v5.img \
  --payload ../../out/gta4l/ram-root-emmc-probe-stage3-evidence/payload.img \
  --payload-sha256 4028e432f200dc1069a50f1baa6786d2f4861b187f2c5a0bd126bb20f5a9cd41 \
  --expect-diagnostic emmc-probe-3 \
  --output ../../out/gta4l/ram-root-emmc-probe-stage3-evidence/boot-ram-root-emmc-probe-stage3.img \
  --manifest ../../out/gta4l/ram-root-emmc-probe-stage3-evidence/boot-ram-root-emmc-probe-stage3.json
```

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| `kernel.macho` (unchanged) | 51,081,216 | `1a4710892e49fa96c053de8f33f1b4b5d89a36fc3d688febd6fa0f7e4a89003a` |
| `payload.img` (80 MiB, stage 3) | 83,886,080 | `4028e432f200dc1069a50f1baa6786d2f4861b187f2c5a0bd126bb20f5a9cd41` |
| `boot-ram-root-emmc-probe-stage3.img` | 85,688,320 | `88b4d28607b371fe008922131c6ee51f27f73da30249fd4a07969f770fa90d87` |

The stage-3 payload contract is 7/7 and its boot-image gate is 18/18. The
payload's kernel arguments include `gta4l-emmc-probe=3`, and the manifest keeps
`hardware_tested=false` as built. The image was subsequently written to BOOT
only for the stage-3 run in §5.3; TWRP on `RECOVERY` was not touched.

### 5.1.1 Failure-record rebuild (2026-10-31)

After the audit rounds the kernel was rebuilt with the Linux toolchain prefix
from `scripts/linux/setup-toolchain.py` and a `python3` 3.11 shim for the
archive helpers, because the host `python3` is 3.9 and has no `tomllib`.
The probe archive is `4becbf69...` and its build report matched the
`Gta4lEmmc.c`, `Gta4lEmmcProbe.c` and header hashes at build time, so it is not
a bootstrap no-op. The source has since gained the raw `RESPONSE0..3`
snapshot, the CMD1 attempt/timeout counters and the richer failure report; do
not cite the flashed image as matching current source. The prelinked `kernel.macho` is `be30e9f4...`.

The stage-3 image was rebuilt with the same payload and packaging commands, with
the failure-record kernel and a new output directory:

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| `kernel.macho` (failure-record build) | 51,081,216 | `be30e9f4f5d88a87da2943a8e1c977d8b5c96151ed78051cdae35782eaed186e` |
| `payload.img` (80 MiB, stage 3) | 83,886,080 | `5a5133a6615afdf61dbb1a50fa36e200d0fb612aaafda058bb0e04d0e6d91042` |
| `boot-ram-root-emmc-probe-failrec.img` | 85,688,320 | `76745fe7e81ca68025ee428cc96c88f98a392b125ecd18c5cf5d0bbef57c7488` |

The payload contract is 7/7 and the boot-image gate is 18/18; only the ramdisk
differs from the pinned v5 baseline. The manifest keeps `hardware_tested=false`.

The image was then written to BOOT only. The pre-flash BOOT partition was saved
and verified (`ef2fb96f...`, 100,663,296 bytes); the image bytes read back
identical, the remainder of the BOOT partition stayed byte-identical, and
RECOVERY stayed byte-identical to TWRP
(`14e11370...`). No other partition was written and the host did not reboot the
tablet. The hardware failure-report run is pending.

The host-only reference audit and its ranked hypotheses are recorded in
[`emmc-cmd2-crc-audit.md`](emmc-cmd2-crc-audit.md). No software correction is
supported by the references; the next discriminator is the current failure
report (`fail=`, `rsp=`, `cmd1=`, `v3=`, `ddr=`, `mnd=`), not a CRC disable.

### 5.1.2 CMD2 CRC diagnostic rebuild (2026-10-31)

The stage-3 candidate was rebuilt from the current source after the CMD2 CRC
audit added the raw `RESPONSE0..3` snapshot, the CMD1 attempt/timeout counters
and the extended failure report. It was built through UniXNU with the Linux
toolchain prefix and the Python 3.11 shim:

```sh
export PATH=/tmp/opencode/py311/bin:$PATH
export UNIXNU_TOOLCHAIN=<prefix from scripts/linux/setup-toolchain.py>
cd ../UniXNU
python3.11 ./unixnu build kernel -D ../UniXNU-gta4l -j 8
cd ../UniXNU-gta4l
python3.11 -B scripts/build-gta4l-diagnostic-payload.py \
  --device . --unixnu ../UniXNU --uefi ../Mu-Silicium \
  --kernel ../UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l/kernel/kernel.macho \
  --data-ramdisk ../../out/gta4l/ram-root-sdhci-audit-evidence/root.img \
  --trust-cache ../../out/gta4l/ram-root-sdhci-audit-evidence/trustcache.bundle \
  --owner-trust-cache --unique-chip-id 0x1122334455667788 \
  --diagnostic emmc-probe-3 --kernel-arg rd=md0 --kernel-arg sheng-pmgr=2 \
  --output ../../out/gta4l/ram-root-emmc-probe-cmd2crc-evidence/payload.img \
  --manifest ../../out/gta4l/ram-root-emmc-probe-cmd2crc-evidence/payload.manifest.json
python3.11 -B scripts/package-gta4l-diagnostic.py \
  --baseline ../../out/gta4l/boot/boot-ram-root-sdhci-v5.img \
  --payload ../../out/gta4l/ram-root-emmc-probe-cmd2crc-evidence/payload.img \
  --payload-sha256 0b8e8cf93f1f872bb9958a9ec7b9d6556c54f14958bb8502fbc1c51ceab7fd60 \
  --expect-diagnostic emmc-probe-3 \
  --output ../../out/gta4l/ram-root-emmc-probe-cmd2crc-evidence/boot-ram-root-emmc-probe-cmd2crc.img \
  --manifest ../../out/gta4l/ram-root-emmc-probe-cmd2crc-evidence/boot-ram-root-emmc-probe-cmd2crc.json
```

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| `kernel.macho` | 51,081,216 | `3a2ffc5ef59932df2414c574d753cdaa6ea058017b808a15159791ed4876a1a2` |
| `libGta4lEmmcProbe.a` | 84,392 | `f1fa6d50aa0f7cc05ee46d604a6e938ce21dc85f40bcf99dee4d4f52074b83d6` |
| `payload.img` (80 MiB, stage 3) | 83,886,080 | `0b8e8cf93f1f872bb9958a9ec7b9d6556c54f14958bb8502fbc1c51ceab7fd60` |
| `boot-ram-root-emmc-probe-cmd2crc.img` | 85,688,320 | `663c98921a5a99c802156be03a3e11d50214c30de42feb0d8eafac3d06e63084` |

The build report's source/header hashes match the checkout, the archive
contains the real `Gta4lEmmc.o`/`Gta4lEmmcProbe.o` members, and the prelinked
kernel contains `_gta4l_emmc_probe_run` and the `rsp=`/`cmd1=` failure format.
The payload contract is 7/7 and the boot-image gate is 18/18. The manifest keeps
`hardware_tested=false`; the image has not been flashed, written or booted. See
[`emmc-cmd2-crc-audit.md`](emmc-cmd2-crc-audit.md) for the ranked hypotheses and
the device-experiment preconditions.

### 5.2 Hardware stage-2 result (2026-10-31)

The owner connected the tablet (SM-T505N, TWRP 3.7.1) and
approved the BOOT-only stage-2 procedure. The image was written to `boot` only.
Readback of the first 85,688,320 bytes matched the image, and `recovery`
remained byte-identical to the recorded TWRP partition
(`14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae`). The
previous boot content was saved as
`dcfe6852035edbb4109231cd41dc5e618e1b8755f47188c1a01bf22e36fcce7b` before the
write.

The tablet booted the image and panicked with:

```text
GTA4L-EMMC HOST REPORT: rc=0 mci=10000072 pwr=0f clk=0007 hc2=0008
ven=00318a0c pst=00000000 log=4/1/268435570 @Gta4lEmmcProbe.c:394
```

- `rc=0` and `mci=10000072`: the v5 controller initialized.
- `pwr=0f` and `pst=00000000`: the bus-power write left no pending power/IO
  request.
- `clk=0007`: the internal clock is stable and the card clock is enabled.
- `hc2=0008` and `ven=00318a0c`: the 1.8 V signalling and pad-switch policy
  were applied.
- `log=4/1/268435570` (`0x10000072`): four log events; the last is RESET with
  the controller version.

This image contains the strict latest-level completion rule, so `rc=0` also
means the controller raised the requested IO-low level; a missing or wrong
level would have returned `GTA4L_EMMC_ERR_POWER`.

This is controller-local host-init evidence. It does not establish card
identification, CID/CSD/EXT_CSD, sector reads, rail voltages or retention.
Photo: `out/gta4l/ram-root-emmc-probe-evidence/host-report-stage2.jpg`
(sha256 `d049547e8fd6135aafe6fb8aff13c9e8295cb9e2012b3944cdb66ac364882a2c`);
transcript: `host-report-stage2.txt` in the same directory.

### 5.3 Hardware stage-3 result (2026-10-31)

The owner approved the BOOT-only stage-3 procedure and the tablet was in TWRP
when the image was written. The current stage-2 boot content was saved first
as partition sha256
`545b57ebc3d451cae0ab87b8020fa28619c144023300833892481b529abf8475`; the same
sha256 was confirmed on the pulled copy. The image was written to `boot` only,
the first 85,688,320 bytes read back identical to the image, and `recovery`
remained byte-identical to the recorded TWRP partition
(`14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae`).

The tablet booted the image and panicked before any identification report (the
panic also printed the frame addresses and the page/free counts; the
transcript keeps those lines):

```text
GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967288 mci=10000072 pwr=0f
clk=0007 hc2=0008 ven=00318a0c pst=00000000 cfg=00002017 cmd=0000
0000 log=4/1/268435570 @Gta4lEmmcProbe.c:373
```

A later owner photograph of the same stage, with the corrected DLL value
`6000642c`, adds the captured failure fields:

```text
stage=3 rc=4294967288 clocks=1 mci=10000072
pwr=0f clk=0007 hc2=0008 ven=00018a9c dll=6000642c dst=00000000
pst=00000000 cfg=00002017 cmd=00000000 bcr=00000000
apps=00004221 ahb=00000001 step=8
fail=1/2/0/00000209/0002/8000/03f800f0/8
log=5/8/2/163840
```

That record attributes the failure to CMD2: `fail_command=0x0209` is argument 0
with `RESP_LONG | CRC`, `fail_error=0x0002` is `CMD_CRC`, and `last_step=8` is
CMD1 readiness completed. The older transcript above remains un-attributed and
is not treated as an independent run.

- `rc=4294967288` is `0xFFFFFFF8`, i.e. `-8` = `GTA4L_EMMC_ERR_CMD_CRC`
  (`package/Gta4lEmmc.h:242`): a command completed with a response CRC error.
- `mci=10000072`, `pwr=0f`, `clk=0007`, `hc2=0008`, `ven=00318a0c` and
  `pst=00000000` repeat the stage-2 host-init state: the v5 controller
  initialized, bus power is on, no power/IO request is pending, the internal
  clock is stable and the card clock is enabled, and 1.8 V signalling is
  selected. The `ven` value is the pre-POR handoff value plus pad bits; it is
  not the normalized `0x00018A9C` that current code writes, so this build does
  not validate the current vendor/DLL normalization.
- `log=4/1/268435570` is four log events; the last is `RESET` with
  `0x10000072`. That is the final log written by
  `gta4l_emmc_controller_init()`, so host init completed and the failure came
  from the card-command sequence that follows `CMD0`.
- `cfg=00002017` and `cmd=00000000` are the GCC SDCC1 apps config/command
  registers at the failure, not SDHCI command state.

This is a failing state `[C]` and is not labeled established. It establishes
that the stage-2 controller state repeats and that identification did not
complete; it does not establish which command failed, why, or that the card is
absent, unsupported or faulty `[U]`. This build does not record the failing
command index or the SDHCI interrupt/error status, so the CRC fault can only
be bounded to "a CRC-checked command after `CMD0`" `[U]`. Code-path inference
narrows it to `CMD2`: `CMD0` has no response and no CRC check, `CMD1` is sent
with `RESP_SHORT` only, and `CMD2` is the next command with
`RESP_LONG | CRC`. The hardware index is still uncaptured. In this path the
later CRC-checked commands are `CMD3`, `CMD7`, `CMD8`, `CMD9` and `CMD13`;
they are reachable only after `CMD2` succeeds. No RCA, capacity, EXT_CSD
revision, bus width, OCR, EXT_CSD checksum, CID, CSD or sector data was
obtained. Stage 3 performs no sector read by design, so no card content was
touched `[C]`.

Photo: `out/gta4l/ram-root-emmc-probe-stage3-evidence/host-report-stage3.jpg`
(sha256 `0014db219d5d3dd1b1db572845d81c8a6fb966d0912c0c23aae02c9d70a082c9`);
transcript: `host-report-stage3.txt` in the same directory (sha256
`69096eca388c1497c7acc59261ce29ca29ae4a771ec613f79928c4188b429794`). The
transcript keeps the on-screen wrap; the source file named by the panic is
`package/Gta4lEmmcProbe.c`.

## 6. Expected report

On a full success the probe panics with a report containing:

- MCI version, RCA, capacity, EXT_CSD revision, bus width, OCR, EXT_CSD sum,
  `HOST_CONTROL2`, `CORE_VENDOR_SPEC`, `CORE_DLL_CONFIG`, `CORE_DLL_STATUS` and
  power status;
- CID and CSD words;
- block 0 and block 1 sums and selected words, including the protective MBR
  signature area and the GPT header fields.

Stage 2 stops with `GTA4L-EMMC HOST REPORT` and stage 3 with
`GTA4L-EMMC IDENT REPORT`. On failure the probe reports the stage, result code,
the `clocks` flag, MCI version, power/clock/`HOST_CONTROL2`/vendor/DLL registers,
power-control status, GCC CFG/CMD/BCR/APPS-CBCR/AHB-CBCR, the last completed
protocol step, the terminal command-failure record (valid flag, index, argument,
encoded command, error/normal status, present state and failure step) and the
last log event. SDHCI registers are read only when the AHB/APPS branches were
enabled; otherwise `clocks=0` marks them as not sampled and the GCC state is
still reported. The report is a bring-up checkpoint, not a storage or filesystem
result. The recorded stage-3 transcript predates the failure record and DLL
fields, so it identifies the failing command only as a CRC-checked command after
`CMD0`.

## 7. Limits and residual risks

- Stage 2 ran on the tablet `[C]`; stage 3 ran and stopped at a command CRC
  error `[C]`; a successful identification and the full sector read have not
  run `[U]`.
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

Stage 2 is complete. Stage 3 did not complete identification: the recorded
transcript predates the failure record, so it bounds the command CRC error only
to a CRC-checked command after `CMD0`. The current code already records the
terminal command index, argument, encoded command register, interrupt/error
status, present state and last completed step before clearing the latches, and
gates its SDHCI diagnostics on the branch clocks. The follow-up audits added
retirement of recovered `CMD1` timeouts, simulator coverage for failed
branch-clock enables, recovered and exhausted `CMD1` timeouts, the reset pulse,
branch-clock ordering and the DLL reset-before-power-down sequence, and a host
test that executes the probe failure report with simulated MMIO and requires
zero SDHCI accesses when the clocks are off. Identifying
the failing command therefore needs a rebuilt probe and a separately approved
BOOT-only run, not another diagnostic-revision design. Read-only
`IOBlockStorageDevice` publication over the same core, multi-block reads and a
documented stop/recovery policy follow after that diagnosis.
