# gta4l native SDHCI v5 / eMMC core

Status: host-tested core with hardware-verified read-only stages `[S]/[C]`.
Section 5.3 is the chronological hardware record. This summary replaces the
older status prose; a capability is `[C]` only for the ranges and modes the
cited run measured.

Measured on the tablet:
- Controller and host initialization, and identification `[C]` (5.3.4).
- F1 single-block content `[C]` (5.3.6); F2 CMD18 with an explicit CMD12 stop
  `[C]` on one reported cold boot (5.3.41).
- 4-bit SDR at 400 kHz `[C]` (5.3.32, 5.3.34, 5.3.91 to 5.3.98).
- HS_TIMING at 1-bit and the 50 MHz clock in the probe path `[C]` (5.3.20);
  high-speed timing in the block driver `[U]`.
- Whole-media publication `[C]` (5.3.37), and published-partition reads with
  a verified 4-bit width switch and revert `[C]` (5.3.91 to 5.3.98).

Not measured: 8-bit, DDR, HS200, HS400, DMA, interrupts, CQE, ICE, tuning, a
filesystem, a mount, a root switch and card writes are all `[U]`.

The implementation is no longer limited to the original 1-bit / 400 kHz PIO
path. The core now includes the verified width switch, the probe high-speed
path, and the block driver's read-only publication and partition adapter.
Section 5.3 keeps the full history, including the withdrawn byte-183 width
criterion and every failed or partial rung.

## 1. Files

| Path | Purpose |
| --- | --- |
| `package/Gta4lEmmc.h` | Register map, command/error/result constants, host struct and ops interface |
| `package/Gta4lEmmc.c` | Portable controller, clock, power, enumeration and PIO-read core |
| `package/Gta4lEmmcPlatform.c/.h` | Board MMIO mapping, width-specific register access, barrier and generic-counter timing behind the ops interface |
| `package/Gta4lEmmcBlockCore.c/.h` | Portable read-only block request helper: validation, per-block loop, bounce copy, counters |
| `package/Gta4lEmmcProbe.c` | Opt-in staged XNU probe: maps MMIO through the platform object, runs the core and reports |
| `scripts/sim-gta4l-emmc/harness.c` | Simulated controller and eMMC card with injectable failures and independent register/command checks |
| `scripts/test-gta4l-emmc.py` | Compiles the core against the simulator, runs 189 scenarios and 117 mutation controls |
| `scripts/sim-gta4l-emmc-probe/harness.c` | Simulated SDHCI/GCC windows, eMMC card and kernel stubs for the probe report path |
| `scripts/test-gta4l-emmc-probe.py` | Runs the probe report, 196 scenarios and 100 mutation controls |
| `scripts/sim-gta4l-emmc-block/harness.c` | Mock read/copy transport for the portable block request helper |
| `scripts/test-gta4l-emmc-block.py` | Compiles the helper and covers 48 scenarios plus 38 mutation controls, with 285 adapter scenarios and 104 adapter/support plus 10 policy mutation controls against the host IOKit seam |
| `scripts/build-gta4l-emmc-probe.py` | Builds `libGta4lEmmcCore.a` (core, platform, block helper) and `libGta4lEmmcProbe.a` with the `bsd_init.o` compile record; rejects duplicate globals |
| `scripts/check-gta4l-emmc-evidence.py` | Selects the per-mode evidence predicate (`width4`, `widthhold`, `widthpub`, `speed`) and passes every argument through |
| `scripts/normalize-gta4l-report.py` | Joins screen-wrapped report lines, logs each join, and never changes a value |
| `scripts/test-gta4l-board-userdata.py` | Compares `board.toml [userdata]` with the captured GPT and runs mutation controls |
| `scripts/copy-gta4l-reference-sources.py` | Copies the pinned reference sources into gitignored `out/` with a SHA-256 manifest |
| `scripts/test-gta4l-emmc-evidence.py` | Runs the normalizer and dispatcher self-tests, then re-runs the historical width4, widthhold and widthpub verdicts when their private evidence is present |
| `docs/emmc-rung-record-template.md` | Record template for one rung |
| `patches/xnu/gta4l-emmc-probe.patch` | Adds the opt-in preflight/run hooks to `bsd_init` |
| `configs/gta4l_defconfig` | Force-loads the SDHCI snapshot and both eMMC archives into the kernel |

## 1a. Protocol corrections (2026-11-04)

These corrections supersede the causal claims of the historical rungs; the
raw reports and images remain unchanged evidence.

- **`BUS_WIDTH[183]` is `W/E_P` and not readable** (JESD84-B51 7.4, Table 93;
  Linux `mmc_select_bus_width()` verifies with a bus test or
  `mmc_compare_ext_csds()`). A zero byte-183 readback is not proof that the
  card refused a width switch. `gta4l_emmc_set_bus_width_verified()` reads the
  512-byte EXT_CSD before the CMD6, switches, reads again at the new width and
  requires the read-only properties segment (bytes 192..511) to match. On
  failure it restores the host and the card and verifies the restore; if that
  cannot be proven it returns `GTA4L_EMMC_ERR_WIDTH_RESTORE` so the caller
  stops with unknown framing. `switched` remains the raw, non-authoritative
  byte-183 observation. The legacy `gta4l_emmc_set_bus_width()` path is kept
  only to decode the already-flashed B1/H10/H11 stages and must not be used
  for new work.
- **CMD14 data CRC is not a read failure by itself.** JESD84-B51 6.6.4 allows
  an optional pattern CRC16 that the host ignores, and Linux
  `sdhci_data_irq()` exempts `MMC_BUS_TEST_R` from its data-CRC path. The
  verified bus test tolerates exactly one `DATA_CRC` indication (no other
  error bit) and still requires the returned pattern; command CRC, normal
  sector-read CRC and every other error remain fatal. The H13 `trc=-12` row
  therefore does not establish a card-side data-conversation failure.
- **The 1-bit bus test uses the JEDEC pattern.** The transmit byte is `0x80`
  and only the first two returned bits (`0b01`) are checked. The previous
  `0x5A`/full-byte-complement check was not a valid 1-bit criterion. The
  4-bit and 8-bit forms continue to follow Linux `mmc_send_bus_test()`.
- **The data-timeout audit writes the right register.** SDHCI timeout count
  is the byte at `TIMEOUT_CONTROL` `0x2E`, not bits of `CLOCK_CONTROL` `0x2C`.
  The stage-7 report now carries `toc`/`arm`/`restored` and rung-reaching
  writes are bounded to the audit; the earlier "minimum armed but no DTO"
  conclusion is withdrawn. The legacy `0x2E = 0x0E` init write is unchanged.
- **CMD18 uses live FIFO readiness and acknowledges BRR.** The read path
  checks `PRESENT_STATE.BUFFER_READ_READY` before draining the port and
  writes the BRR bit back after each sector, so a latched status bit cannot
  be mistaken for a ready FIFO, and one sector is consumed once. The core
  simulator models live readiness separately from BRR and flags a premature
  FIFO read. The earlier `mb=N` values are software drain ordinals, not an
  independently validated sector count.
- **H12 used a reserved BUS_WIDTH value.** JESD84-B51 Table 145 defines 5/6
  as 4/8-bit DDR and 3/4 as reserved. The historical stage issued value 4,
  so it did not test DDR; the helper is now named
  `gta4l_emmc_set_bus_width_reserved4()`, and DDR remains deferred.
- **H15 is the corrected width rung.** `gta4l-emmc-probe=15` runs the
  stage-A baseline, a verified 4-bit SDR switch, the corrected CMD19/CMD14
  test, a 4-bit content pass, a verified return to 1-bit and the recovery
  read. Its first cold boot (§5.3.31) passed the switch and verified 4-bit
  data transfer, then reproduced the CMD19 write timeout at 4-bit. The
  original revert's pre-switch EXT_CSD read was blocked by a latched
  `DAT_INHIBIT`, so no CMD6 was issued. The report now prints an explicit
  attempted flag on B/T/V/Q and a result code on T, and the new
  `gta4l_emmc_set_bus_width_recover()` clears the data circuit before CMD6
  and verifies against a baseline captured while the path was healthy.
  `gta4l-emmc-probe=16` is the content-only isolation rung: the same
  baseline and verified 4-bit switch, the 4-bit content pass and the
  baseline-based recovery, with no CMD19 or CMD14 at all. Its first boot
  (§5.3.32) returned `rc=0 stop=0`: the verified 4-bit content pass, the
  verified revert and the block-0 recovery read all completed. The 4-bit SDR
  DAT path is therefore measured for reads. `gta4l-emmc-probe=17` is the
  Linux-compatible completion rung (§5.3.33): the same baseline, verified
  switch and 4-bit content pass, then one CMD19 write whose expected
  data timeout is recorded and tolerated, one bounded data reset, one CMD14
  read-back, the verified revert and the recovery read. Its fourth boot
  (§5.3.34) returned `rc=0 stop=0` with the CMD19 timeout recorded
  (`wrc=-11`), a clean reset (`rst=0`) and the CMD14 pattern match
  (`trc=0`, `tw=te=000000a5`), so **F3 is measured PASS at 4-bit SDR,
  400 kHz**. The 8-bit width is untried and stays open separately. H14 is
  paused rather than advanced: CMD12 is not appropriate for the single-block
  CMD19 write, and its stop-code mapping conflated distinct failures. The
  recorded CMD19 timeout is now read as a completion-rule defect, not a
  data-path defect: Linux `mmc_bus_test()` ignores every `BUS_TEST_W` error
  and lets `BUS_TEST_R` decide, and TI erratum SPRZ436H i836 records that
  the card generates no CRC status token for the tuning pattern, so the
  controller's data timeout is expected and the next rung must reset the
  data circuit and continue to CMD14 rather than stop at the write.

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
| 2 | `CMD1` | Poll OCR bit 31; up to 20 attempts, each with a nominal 100-ms per-command poll budget and a 1-ms gap after an unsuccessful attempt |
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
and the user-area partition access bits. The defined `DATA_SECTOR_SIZE`
encodings are only `0` and `1`; a reserved value (2-255) is not treated as the
512-byte default and is rejected. A card that does not meet any condition
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
  its own record. On the CMD7 busy-completion success path, the error status is
  re-read after the terminal command/argument/response capture, so an error
  asserted during those diagnostic reads is still evaluated before the status
  clear; that final sample is the observation boundary for that path. The
  in-loop error path returns the error it sampled, and the exhausted-poll path
  returns the software timeout from its pre-capture error sample without such a
  re-read.
- The error status is also re-read once before a non-busy command's or a data
  read's success clear, so an error that becomes visible while the response or
  FIFO is being read is still recorded. An error asserted after that final sample
  remains unobserved; the rule does not make the path race-free.
- The host tracks whether the AHB/APPS branches were enabled. The probe reads
  SDHCI registers on the failure path only when they were, so a clock-enable
  failure cannot turn the diagnostic into an access to an unclocked controller;
  the GCC state is always reported.
- Card R1 errors are checked separately from controller status.
- Every wait has a microsecond bound and an iteration cap, and every poll loop
  uses the documented 10 µs step. The microsecond value is a nominal delay
  budget: `poll_iterations()` divides it by the 10 µs step and caps the result
  at 100,000 iterations (at least one). Counter reads record observed ticks for
  diagnostics only; they do not enforce an exact wall-clock deadline, because
  each iteration also performs MMIO and optional diagnostic work.
- `CMD17` is rejected when the block is at or beyond the discovered capacity.
- The default path submits only `CMD17` sector reads. The optional
  `gta4l-emmc-mb=1` mode runs the D10 F2 sequence: a two-sector open-ended
  `CMD18` read at LBA 2, then one 21-sector `CMD18` over LBAs 2..22, each
  stopped explicitly with `CMD12` (the host `AUTO_CMD12` enable bit stays
  clear), each byte-compared against the same boot's `CMD17` reference, then
  bounded `CMD17` recovery reads of block 0 and LBA 2. The report carries
  per-request requested/completed/validated sectors, the first divergence,
  drain, stop issue/result/status and the recovery outcome. A transfer
  failure preserves its first record before cleanup and stops without a
  retry. There is no `CMD24`/`CMD25`, DMA, CQE, ICE or tuning operation.

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
- not-ready `CMD1` replies followed by a ready reply and then a `CMD2` CRC
  failure (the recorded hardware sequence), a normal-status latch clear that
  invalidates the response after the CMD1 window was captured, and a
  reinitialization that clears both response windows;
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
- a reserved `DATA_SECTOR_SIZE` encoding that must not be read as 512-byte mode;
- small-card and non-user-partition rejection;
- 1.8 V and 3.3 V signalling, plus the explicit pad-switch-on and pad-switch-off
  policies;
- vendor normalization from an HS400 MCLK selection or an enabled
  `HC_SELECT_IN` override, and the `PWRSAVE_DLL` clear;
- EXT_CSD data read;
- a data error that becomes visible on the last FIFO word read and a command
  error that becomes visible while a non-busy response is read, both of which
  must be caught by the final pre-clear error sample;
- the CMD7 R1b busy lifecycle: CC-then-TC, TC-then-CC, simultaneous
  completion, CC only, TC only, neither completion, a delayed busy release, a
  late completion, stale pre-command status, busy-phase and late errors, a
  completion on the last permitted poll, trace plateau/return/overflow, and, on
  the busy-completion success path, an error that becomes visible during the
  terminal diagnostic reads and is caught by the final error re-read. Success
  requires both CC and TC inside one shared poll budget, the short response is
  snapshotted only after CC, and a missing completion is not rescued by
  terminal sampling; the final error sample after terminal capture bounds the
  success path, while the exhausted-poll path returns the timeout from its
  pre-capture sample;
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
  recovered `CMD1` timeout, a missing raw-response snapshot, a retimed `CMD1`
  response snapshot, a missing response-window clear on reinitialization, a
  clock-gate flag set before the branch clocks are verified, a missing 4 KiB
  sector check, an accepted reserved sector-size encoding, restored CC-only
  success, an accepted TC-only completion,
  cleared sticky completion evidence, a removed final terminal-read error
  sample, a removed data/response final error sample, an ignored final checked
  error, a response read before CC, a timeout
  budget rearmed after CC, a loose response-type match and an early downstream
  command.

The test also checks the source policy: only the injected ops are used, the core
contains no DMA, CQE, ICE, tuning or card-write command, the poll loops use the
10 µs step, and the probe uses width-correct MMIO and aborts on a stalled timing
source.

`scripts/test-gta4l-emmc-probe.py` executes the probe report path. It
copies `package/Gta4lEmmcProbe.c` and replaces only the generic-counter and
barrier instructions and the six MMIO helpers with recording hooks; the stage
selection, conflict check, clock guard, register offsets and panic strings are
the production code. The scenarios cover a disabled probe, an MMIO-map
failure, a conflicting diagnostic argument, a stuck AHB branch, a stuck APPS
branch, a verified-clock version failure, a stuck RCG update, a missing clock
stability bit, a stage-3 CMD2 CRC failure, CMD3 without a completion interrupt,
a late CMD3 completion that appears only after the poll loop expires, a CMD9
timeout, a successful identification through EXT_CSD, and the CMD7 cases: the
transcribed TC-only timeout, a changed and an unchanged post-timeout response
window, CC-then-TC, TC-then-CC, simultaneous completion, CC without a busy
completion (a bounded timeout, because CC and TC are both required), a
busy-phase error, a late
busy-phase error, an error asserted during the terminal diagnostic reads after
both completions, and a bounded-trace overflow. Two full-probe scenarios fail
the first or second CMD17 with a data CRC or a data timeout and check the
block-read failure report and command stop. Three `report-max-width-*`
end-to-end runs execute the failure, identification and full-probe success
report forms with wide simulated values (a 10-digit capacity, both poll budgets
at their limit, 16-hex-digit tick fields, a full four-entry CMD7 trace and its
overflow count) and assert the report stays below the 2048-byte panic-text
budget; they are execution samples, not a field-wise width bound. The three
report-width scenarios use the host-only
`scripts/sim-gta4l-emmc-probe/report-width-fixture.c` to call the production
formatters with field-wise extremes, assert each field and require the printed
length plus margin to fit the panic buffer; a width-budget control checks the
accept boundary at 2047 and 2048 bytes, and two fixture mutations narrow a
stored trace index and must fail the corresponding trace assertion. A separate
oversized-report control
requires the harness to reject a 2099-byte message with the explicit truncation
error. A
stuck branch must panic with `clocks=0` and the GCC state while issuing no SDHCI
read or write;
the version-failure control must report `clocks=1` and sample each diagnostic
SDHCI register in the report (MCI version, power, clock, `HOST_CONTROL`,
`HOST_CONTROL2`, vendor, vendor spec 3, DLL config/status, DDR config and power
status), with MCI version read twice in total (init plus report) and the others
once, which proves the access counter detects real reads. The clock-timeout run
must panic with `rc=-5`, `clocks=1` and the formatted `cfg=00002017`,
`mnd=00000001/000000fc/000000fb`, `cmd=00000001` and branch/reset values, and
must read the diagnostic-only GCC M/N/D/BCR offsets exactly once each. The
clock-stability run keeps the RCG update released and must report
`cmd=00000000` and `clk=0001`. The CMD2 CRC run drives CMD0 and CMD1 to
completion, fails CMD2 with a synthetic R2 response, and checks the `fail=`,
`rsp=`, `cmd1rsp=`, `pre2rsp=`, `c2=`, `c3p=`, `c3t=`, `c3l=`, `cid=`, `fmt=4`
and `hv=` fields, the exact CMD0/CMD1/CMD2 order and arguments, the report read
counts (including one HOST_VERSION read) and the exact CMD2 encoding. The CMD3
timeout runs check the failed command index and encoding, the fresh post-timeout
status/command/response, the CMD3-only poll history, the distinct last-poll
sample and that identification stops at the timed-out command. The CMD9 timeout
run also checks that the timeout capture is labelled CMD9 while the poll history
remains CMD3's. The CMD7 runs check the pre-command state and response window
(`c7p=`), the poll OR mask, first/last normal sample, iteration and tick counts
(`c7q=`), the first observed CC/TC iteration and timestamp (`c7c=`), the
terminal state and response window (`c7t=`) and the bounded transition trace
with its overflow count (`c7x=`), including exact report strings for TC-then-CC
and simultaneous completion. The successful identification run checks that the
success report carries the CMD2, pre-CMD3, poll-history, CMD7, counter-frequency
and CID fields and that no timeout capture is invented. The mutations
insert an unguarded SDHCI read before the guard, replace the guard with an
unconditional branch, suppress a GCC report read, suppress an SDHCI report read,
suppress the raw R2 snapshot, suppress the final-CMD1 window, suppress the
pre-CMD2 window, suppress the captured CMD2 error, change the capture-format
identifier, remove the HOST_VERSION sample, suppress the success-report CID,
suppress the success-report poll history, suppress the timeout command index,
suppress the pre-CMD7 validity, poll OR mask, TC validity or CC validity,
suppress the trace overflow count, suppress the terminal CMD7 response
window or suppress the block-read failure record; each must be rejected for the
expected reported reason, not merely by a nonzero exit.

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
kernel arguments include `gta4l-emmc-probe=2`. The build was not marked
hardware-tested; the image was subsequently flashed to BOOT only for the
stage-2 run in §5.2. TWRP on `RECOVERY` was not touched.

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
payload's kernel arguments include `gta4l-emmc-probe=3`, and the build was not
marked hardware-tested. The image was subsequently written to BOOT only for the
stage-3 run in §5.3; TWRP on `RECOVERY` was not touched.

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
differs from the pinned v5 baseline. The build was not marked hardware-tested.

The image was then written to BOOT only. The pre-flash BOOT partition was saved
and verified (`ef2fb96f...`, 100,663,296 bytes); the image bytes read back
identical, the remainder of the BOOT partition stayed byte-identical, and
RECOVERY stayed byte-identical to TWRP
(`14e11370...`). No other partition was written and the host did not reboot the
tablet. This older failure-record image has not been booted; the later
current-source diagnostic run is recorded in §5.3.

The host-only reference audit and its hypotheses are recorded in
[`emmc-cmd2-crc-audit.md`](emmc-cmd2-crc-audit.md). No software correction is
supported by the references; the next discriminator is the current failure
report (`fail=`, `rsp=`, `cmd1=`, `v3=`, `ddr=`, `mnd=`), not a CRC disable.

### 5.1.2 CMD2 CRC diagnostic rebuild (2026-10-31)

The stage-3 candidate was rebuilt from the current source after the CMD2 CRC
audit added the raw `RESPONSE0..3` snapshot, the CMD1 attempt/timeout counters
and the extended failure report. It was built through UniXNU with the Linux
toolchain prefix and the Python 3.11 shim:

```sh
# The /tmp/opencode/py311 shim used at the time is gone; the host
# /usr/bin/python3.11 provides the same interpreter.
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
The payload contract is 7/7 and the boot-image gate is 18/18. The build record keeps
`hardware_tested=false`; the image has not been flashed, written or booted. See
[`emmc-cmd2-crc-audit.md`](emmc-cmd2-crc-audit.md) for the hypotheses and
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
- `hc2=0008` and `ven=00318a0c`: the 1.8 V signalling and pad-switch bits are
  set. This is the pre-POR handoff vendor value plus pad bits, not the current
  normalized value (`0x18A9C`), so it does not prove the current POR/vendor/DLL
  policy was applied.
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

That record attributes the failure to CMD2: `fail_argument=0`,
`fail_command=0x0209` is CMD2 (`0x0200`) with `RESP_LONG | CRC`,
`fail_error=0x0002` is `CMD_CRC`, and `last_step=8` is CMD1 readiness
completed. The older transcript above remains un-attributed and
is not treated as an independent run. The later `cmd2window` run (5.3.1) did
not reproduce this CMD2 failure; CMD2 completed and CMD3 timed out.

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
absent, unsupported or faulty `[U]`. The older transcript's build does not
record the failing command index or the SDHCI interrupt/error status, so for
that build the CRC fault can only be bounded to "a CRC-checked command after
`CMD0`" `[U]`. Code-path inference narrows it to `CMD2`: `CMD0` has no response
and no CRC check, `CMD1` is sent with `RESP_SHORT` only, and `CMD2` is the next
command with `RESP_LONG | CRC`. The later current-source record above captures
the index and error directly (`fail_index=2`, `fail_error=0x0002`). In this
path the later CRC-checked commands are `CMD3`, `CMD7`, `CMD8`, `CMD9` and
`CMD13`; they are reachable only after `CMD2` succeeds. The older transcript
records no RCA, capacity, EXT_CSD revision, bus width, OCR, EXT_CSD checksum,
CID, CSD or sector data; the later current-source reports do record the CMD1
OCR value. Stage 3 performs no sector read by design, so no card content was
touched `[C]`.

Photo: `out/gta4l/ram-root-emmc-probe-stage3-evidence/host-report-stage3.jpg`
(sha256 `0014db219d5d3dd1b1db572845d81c8a6fb966d0912c0c23aae02c9d70a082c9`);
transcript: `host-report-stage3.txt` in the same directory (sha256
`69096eca388c1497c7acc59261ce29ca29ae4a771ec613f79928c4188b429794`). The
transcript keeps the on-screen wrap; the source file named by the panic is
`package/Gta4lEmmcProbe.c`.

### 5.3.1 cmd2window stage-3 result (2026-10-31)

A diagnostics-only rebuild added raw `RESPONSE0..3` captures after the final
accepted CMD1 (`cmd1rsp=`) and immediately before CMD2 (`pre2rsp=`). It made no
command-semantics change, added no retry, wrote no response register and did
not disable CRC checking. Image:
`out/gta4l/ram-root-emmc-probe-cmd2window-evidence/boot-ram-root-emmc-probe-cmd2window.img`,
85,688,320 bytes, sha256
`b119f78010bb8e50f319d871f05d94c29b57010757b2a32176c0d9ff778ff19b`;
kernel `4c1cd5e1…`. Flash record: `flash-record.json` in the same directory.
BOOT-only write; pre-flash BOOT backup `99a6cab4…` (the 5.3 image); `recovery`
unchanged at `14e11370…`.

The tablet panicked with:

```text
GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967289 fail=1/3/65536/0000
031a/0000/0000/03f800f0/9 rsp=00000500,5836384d,42009005,f4661978
cmd1rsp=1/c0ff8080,00000000,00000000,00000000 pre2rsp=1/c0ff8080,0
0000000,00000000,00000000 cmd1=3/0/c0ff8080 clocks=1 mci=10000072
pwr=0f clk=0007 hc=00 hc2=0008 ven=00018a9c v3=02626040 dll=600064
2c dst=00000000 ddr=80040873 pst=00000000 cfg=00002017 mnd=00000000
1/000000fc/000000fb cmd=00000000 bcr=00000000 apps=00004221 ahb=00
000001 step=9 log=5/8/3/0 @Gta41EmmcProbe.c:456
```

Photo: `stage3-cmd2window-photo-2026-10-31.jpg` in the same directory (sha256
`a9656fc934f1fec9417633054441e28d192d93be3864ee8bf83a377560634305`);
transcript: `stage3-cmd2window-report.txt` (sha256 `6c7738ef…`).

- `rc=4294967289` is `-7`, `GTA4L_EMMC_ERR_CMD_TIMEOUT`
  (`package/Gta4lEmmc.h:241`).
- `fail=1/3/65536/0x031A/0x0000/0x0000/0x03F800F0/9`: CMD3, argument
  `0x00010000` (RCA 1), command `0x031A` (`RESP_SHORT | CRC | INDEX`), error
  status `0x0000`, normal status `0x0000`, present state `0x03F800F0`,
  failure step 9.
- `step=9` is `GTA4L_EMMC_STEP_CMD2`, assigned only after CMD2 returns OK and
  its response passes validation, so CMD2 completed.
- CMD3 was issued with `GTA4L_EMMC_DEFAULT_RCA << 16` and
  `RESP_SHORT | CRC | INDEX`; the completion poll expired without `CC` and
  without an error bit. The driver recorded the timeout rather than a CRC,
  index or timeout error bit.
- `cmd1rsp=` and `pre2rsp=` both show the CMD1 OCR `c0ff8080` in `RESPONSE0`
  with zeros in `RESPONSE1..3`. The pre-command clear did not empty the
  window.
- `rsp=00000500,5836384d,42009005,f4661978`: `RESPONSE1..3` match known CID
  words 2..4 (`5836384d`, `42009005`, `f4661978`; the last byte is the CRC7
  trailer `0x3c << 1`), and `RESPONSE0=0x00000500` is a plausible CMD3 R1
  status (CURRENT_STATE=2, Identify; READY_FOR_DATA), not CID word 1
  (`15010044`). The window contains real CMD2 data, not the stale CMD1 OCR.
- The known CID is `150100445836384d42009005f4661900`; CRC7 over the 15-byte
  prefix is `0x3c`, encoded as `0x79`. The observed `0x78` is `CRC7 << 1`
  with the end bit clear. The "known" CID is itself from Linux sysfs and is
  already normalized by the same long-response path, so this is consistency
  evidence, not an independent raw dump.

This run `[C]` establishes that CMD2 passed and that the terminal failure is a
CMD3 completion timeout, not a CMD2 CRC error. It does not establish why CMD2
failed in the 5.3 image or why the host missed CMD3's completion `[U]`. The
card appears to have answered CMD3 (the R1 status is present), but the
`cmd2window` image has no post-timeout `NORMAL_INT_STATUS`/`COMMAND` capture.
The working tree added that expanded D7 capture (`fmt=2`: `c2=`, `c3p=`,
`c3t=`, `c3l=`, `cid=` and `hv=`) to the failure report and to successful
identification, including the counter frequency. It ran on hardware in 5.3.2;
that run passed CMD3 and CMD9 and stopped at a CMD7 R1b completion timeout. The
working tree now emits `fmt=4` and adds the CMD7-specific fields `c7p=`, `c7q=`,
`c7c=`, `c7t=` and `c7x=` (pre-command state and response window, sampled poll
history, first observed CC/TC, terminal state and bounded transition trace). The
poll history spans the busy-completion wait rather than stopping at CC, so the
format version is 4; earlier CC-terminated CMD7 history must not be decoded with
these semantics. The next owner-approved run would supply the evidence the
`fmt=2` run lacked. The
failed window is not a validated CID: `RESPONSE0` was overwritten by CMD3 and
the first CID word is absent. No card content was read.
The audit's section 2.7, section 2.8, section 10 and H8/D7 contain the full
analysis; `docs/emmc-cmd7-timeout-finding.md` holds the focused CMD7 decode and
the open questions.

### 5.3.2 fmt=2 stage-3 result (2026-10-31)

The `fmt=2` diagnostics-only rebuild added the expanded D7 capture to the
failure report and to successful identification. Image:
`out/gta4l/ram-root-emmc-probe-fmt2-evidence/boot-ram-root-emmc-probe-fmt2.img`,
85,688,320 bytes, sha256
`7bf3e71991f949f2da6fcff2ef388268904a512b1876062d04e9bb86b6f411b6`;
payload `ba4e88ba…`; kernel `f53860af…`; source hashes `b4461abc…`,
`c50b6467…`, `60cff259…`. Flash record: `flash-record.json` in the same
directory. BOOT-only write; pre-flash BOOT `d42ce6db…`, post-flash
`92e80609…`; `recovery` unchanged at `14e11370…`. The flashing procedure did
not reboot; the owner booted the image.

The owner supplied this panic transcription (normalized to one field per line;
the focused finding preserves the extraction verbatim):

```text
GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967289
fail=1/7/65536/0000071b/0000/0002/03f800f0/11
rsp=00000700,0f5903ff,f6dbffef,8e40400c
cmd1rsp=1/c0ff8080,00000000,00000000,00000000
pre2rsp=1/c0ff8080,00000000,00000000,00000000
c2=1/0001/0000/05f46619,4d420090,44583638,00150100
c3p=1/0000/0000/03f800f0/0023/07ff
c3t=1/7/071b/00010000/1/00000700,0f5903ff,f6dbffef,8e40400c
c3l=1/3/0000/0001/26/0000000000001557/19200000
cid=15010044,5836384d,42009005,f4661900 fmt=2 hv=7202
cmd1=3/0/c0ff8080
clocks=1 mci=10000072 pwr=0f clk=0007 hc=00 hc2=0008 ven=00018a9c
v3=02626040 dll=6000642c dst=00000000 ddr=80040873 pst=00000000
cfg=00002017 mnd=00000001/000000fc/000000fb cmd=00000000 bcr=00000000
apps=00004221 ahb=00000001 step=11 log=5/8/7/2 @Gta4lEmmcProbe.c:503
```

- `rc=4294967289` is `-7`, `GTA4L_EMMC_ERR_CMD_TIMEOUT`
  (`package/Gta4lEmmc.h:242`).
- `fail=1/7/65536/0x071B/0x0000/0x0002/0x03F800F0/11`: CMD7, argument
  `0x00010000` (RCA 1), command `0x071B` (`RESP_SHORT_BUSY | CRC | INDEX`),
  error `0x0000`, normal `0x0002` (TC), present `0x03F800F0`, failure step 11.
- `step=11` is `GTA4L_EMMC_STEP_CMD9`; CMD9 completed and CMD7 success would
  have set step 12. CMD2, CMD3 and CMD9 all completed in this run.
- The completion poll's recorded samples show no error bit and no `CC`; the
  fresh post-timeout sample shows `TC` but not `CC`. The report has no in-loop
  CMD7 history, so it does not prove `CC` was never asserted earlier. The
  returned `-7` is the driver's software timeout, not a latched `CMD_TIMEOUT`
  bit.
- `c3t` is the CMD7 timeout capture despite its legacy name: command readback
  `0x071B`, argument `0x00010000`, response stable across two reads. `c3l` is
  the CMD3-only poll capture (26 iterations, last normal `CC`, 5,463 ticks at
  19.2 MHz, about 284.5 microseconds); it is not CMD7's timeout duration.
- `rsp=0x00000700,0x0F5903FF,0xF6DBFFEF,0x8E40400C`: applying the driver's
  32-bit long-response transformation gives
  `40400CF6,DBFFEF0F,5903FF00,00070000`. That arithmetic does not recover a
  coherent CMD9 CSD. The raw upper words match the recovery CSD apart from one
  nibble, which is consistent with a retained CMD9 window with `RESPONSE0`
  overwritten, but it is consistency only, not proof. `RESPONSE0` remains
  ambiguous between a plausible CMD7 R1 (STBY, ready, no errors) and a retained
  value; the window is not a validated CMD7 response.
- `log=5/8/7/2` is the last log event: count 5, `ERROR` (8), index 7,
  `(error << 16) | normal = 0x0002`. It is the failure record, not a reset log.
  The live `cmd=0`/`bcr=0` fields are GCC registers, not SDHCI command state;
  the SDHCI command submission evidence is the `c3t` readback.

This run `[C]` moves the terminal failure from CMD3 to CMD7 and establishes a
completion-status mismatch on the first R1b command: TC is set while CC is
absent. It does not establish why, whether the card accepted CMD7, or that the
response window is fresh `[U]`. The focused decode, candidate interpretations
and open questions are in `docs/emmc-cmd7-timeout-finding.md`. No EXT_CSD or
sector data was read; CRC checking remains enabled. Separately, the retained
pre-instrumentation source accepted an R1b command on CC alone and returned
without waiting for the busy completion; the lost `fmt=2` build's exact source
is not retained. The working tree now accumulates CC and TC across one shared
budget and requires both; that corrected implementation gap does not explain the
absent CC in this run.

The working tree extends this capture to `fmt=4` with CMD7-specific fields:
`c7p=` (pre-command normal/error status, status enables, present state and raw
response window), `c7q=` (poll OR mask, first/last normal sample, iterations and
ticks), `c7c=` (first observed CC/TC iteration and timestamp), `c7t=` (terminal
state and response window) and `c7x=` (a four-entry bounded transition trace
plus overflow). These are sequential samples, not an atomic snapshot, and an OR
mask proves only which bits were observed, not every hardware transition. The
core's R1b decision path now requires both CC and TC inside one shared poll
budget and snapshots the short response only after CC. Both simulators cover the
TC-only failure, changed and unchanged response windows, CC-then-TC, TC-then-CC,
simultaneous completion, CC without a busy completion (a bounded timeout, not a
premature success), busy-phase errors, late errors, an error asserted during
the terminal diagnostic reads on the busy-completion success path and trace
overflow. The later 2026-11-01 owner-approved `fmt=4` run and its verified
rerun are recorded in
§5.3.3.

### 5.3.3 fmt=4 fail-closed stage-3 result (2026-11-01)

The `fmt=4` fail-closed image was built from the current tree on 2026-11-01
(11:15Z):
`out/gta4l/ram-root-emmc-probe-failclosed-evidence/boot-ram-root-emmc-probe-failclosed.img`,
85,688,320 bytes, sha256
`4d2f7975d3bc2e26a1ab15fb4d60d765190fba2df0e6b4ee7c5226d41a63e5bf`;
payload `954cc0dc…`; kernel `27cb1b54…`; source hashes `e493b3eb…`,
`e0a2830c…`, `932552a2…`; archive `e9578564…`. The payload carries
`gta4l-emmc-probe=3`, the payload contract is 7/7 and the boot-image gate is
18/18. The build manifest does not record a hardware-tested flag.

The owner reports two runs of the same image on two different units: an SM-T505
first, then the owner's SM-T505N. The first run's owner-supplied text recorded
the CMD2 failure below; it has no flash record or run ID in this tree. The later
BOOT-only run on the SM-T505N reproduced the same eMMC failure record and
response window, and the owner reports the two results are identical 1:1. The
rerun flash record is
`out/gta4l/ram-root-emmc-probe-failclosed-rerun-evidence/flash-record.json`:
pre-flash BOOT partition `92e80609…`, post-flash full partition `b53f31fe…`,
image-byte readback `4d2f7975…`, unchanged tail `a28b4ea2…`, unchanged
`RECOVERY` `14e11370…`, readback gate 18/18 and payload contract 7/7. The
photo is
`out/gta4l/ram-root-emmc-probe-failclosed-rerun-evidence/stage3-fmt4-rerun-photo-2026-11-01.jpg`
(2560x1920, sha256
`c079952510039d236a705985a91d9e4a8b042a125afb81ef66bf1a4e6ddc8c76`).

The owner-supplied panic (normalized; `docs/emmc-cmd2-crc-audit.md` §2.9
records the extraction notes and the normalized block):

```text
GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967288
fail=1/2/0/00000209/0002/8000/03f800f0/8
rsp=00000000,00000000,80000000,00c0ff80
cmd1rsp=1/c0ff8080,00000000,00000000,00000000
pre2rsp=1/c0ff8080,00000000,00000000,00000000
c2=0/0000/0000/00000000,00000000,00000000,00000000
c3p=0/0000/0000/00000000/0000/0000
c3t=0/0/0000/00000000/0/00000000,00000000,00000000,00000000
c3l=0/0/0000/0000/0/0000000000000000/19200000
cid=00000000,00000000,00000000,00000000 fmt=4 hv=7202
cmd1=3/0/c0ff8080
clocks=1 mci=10000072 pwr=0f clk=0007 hc=00 hc2=0008 ven=00018a9c
v3=02626040 dll=6000642c dst=00000000 ddr=80040873 pst=00000000
cfg=00002017 mnd=00000001/000000fc/000000fb cmd=00000000 bcr=00000000
apps=00004221 ahb=00000001 step=8 log=5/8/2/163840
c7p=0/0000/0000/00000000/0000/0000/00000000,00000000,00000000,00000000
c7q=0/0000/0000/0000/0000/0/0000000000000000
c7c=0/0/0000000000000000/0/0/0000000000000000
c7t=0/0000/0000/00000000/00000000/00000000/00000000,00000000,00000000,00000000
c7x=0/0/0:0000:0000,0:0000:0000,0:0000:0000,0:0000:0000 @Gta4lEmmcProbe.c:442
```

- `rc=4294967288` is `-8`, `GTA4L_EMMC_ERR_CMD_CRC` (`Gta4lEmmc.h:242`).
- `fail=1/2/0/0x0209/0x0002/0x8000/0x03F800F0/8`: CMD2, argument 0, command
  `0x0209` (CMD2 index `0x0200`, `RESP_LONG` `0x0001`, CRC `0x0008`), error
  `0x0002` (`CMD_CRC`), normal `0x8000` (error-interrupt aggregate; `CC`
  clear), present `0x03F800F0`, step 8.
- `step=8` is `GTA4L_EMMC_STEP_CMD1`; CMD1 was the last completed step and
  CMD3/CMD9/CMD7 were never submitted. `cmd1=3/0/0xC0FF8080`.
- `c2`, `c3p`, `c3t`, `c3l` and `c7p`–`c7x` are invalid/zero: the CMD2
  post-success capture and the later command captures were not reached. The
  report prints the fields unconditionally.
- `rsp=0x00000000,0x00000000,0x80000000,0x00C0FF80` is not CID-shaped. The
  two nonzero words contain the bytes `80 00 00 00 00 C0 FF 80`, a one-byte
  rotation of `00 00 00 00 C0 FF 80 80`, whose low 32 bits are the recorded
  CMD1 word `0xC0FF8080`. It is consistent with a retained or rotated CMD1
  window, not a plain logical shift, and it is not a CID.
- `ven=0x00018A9C` and the rest of the register state repeat §5.3 and §5.3.2
  (`mci=0x10000072`, `pwr=0x0F`, `clk=0x0007`, `v3=0x02626040`, DLL
  `0x6000642C`, `ddr=0x80040873`, `cfg=0x2017`, MND `1/252/251`). This is a
  register state, not a measured clock.
- `log=5/8/2/163840` is the failure record: count 5, `ERROR` (8), index 2,
  `(error << 16) | normal = 0x00028000`.

The failure record, response window and register state are identical to the
current-source CMD2 CRC record in §5.3 and to the earlier `fmt=4` run. The two
`fmt=4` observations are on different units (SM-T505 and SM-T505N) per the
owner and agree 1:1; they are not a same-unit repeatability test. This `[C]`
records the CMD2 CRC outcome on the 2026-11-01 tree; it does not establish a
new failure mode. `cmd2window` (§5.3.1) and `fmt=2` (§5.3.2) had passed CMD2 on
the owner's SM-T505N, so the outcome varies across diagnostic builds on that
unit. No cause is established; the runs do not select among H2/H4/H5/H6/H7 in
`docs/emmc-cmd2-crc-audit.md` §6. CMD1 OCR `0xC0FF8080` was observed; no
validated CID, CSD, EXT_CSD, capacity, partition or sector result was obtained,
no sector read was issued and CRC checking remains enabled. The provenance note
and pulled-prefix gate rerun are in `docs/emmc-cmd2-crc-audit.md` §2.10.

### 5.3.4 cmd1gap candidate stage-3 result (2026-11-01)

The `cmd1-crc-retire-8us` candidate is the `fmt=4` fail-closed source plus one
8-us delay after an accepted ready CMD1 (`package/Gta4lEmmc.h` constant,
`package/Gta4lEmmc.c:1357`); no other source changed. It was built from commit
`2a3268c7` with core/header/probe hashes `874e6878…`, `5dd223f7…` and
`932552a2…`, archive `22e6186f…`, payload kernel `ee33719e…`, payload
`6b6f5212…` (`gta4l-emmc-probe=3`, contract 7/7), and boot image
`8a463dae…` (85,688,320 bytes, gate 18/18 against the v5 baseline
`ce4d76e8…`). The BOOT-only flash record is
`out/gta4l/ram-root-emmc-probe-cmd1gap-evidence/flash-record.json`:
pre-flash `b53f31fe…` (the §5.3.3 image plus its tail), post-flash
`92f4000e…`, image-byte readback `8a463dae…`, tail `a28b4ea2…` unchanged,
`RECOVERY` `14e11370…` unchanged, pulled gate 18/18 and pulled payload contract
7/7. The record was written after the BOOT write and records no boot at that
time.

The owner then rebooted and supplied text transcriptions for three runs: the
owner's SM-T505N and two SM-T505 units (owner-reported). All three reached
`GTA4L-EMMC IDENT REPORT: rc=0` and stopped at the intentional report panic.
The report is emitted only after `gta4l_emmc_init()` returns `GTA4L_EMMC_OK`
and before the stage-3 probe's sector read. On all three units:
`mci=10000072 rca=1 cap=61071360 rev=8 bw=0 ocr=c0ff8080 hc2=0008
ven=00018a9c dll=6000642c dst=00000000 pst=00000000 step=15 log=5`; CMD2 is
valid with normal `0x0001`; the normalized CID is Samsung with product name
`DX68M`; the `c7p` raw CSD window is identical; `c3l` and `c7q` are valid with
38 iterations; `c7q`/`c7t` show CC and TC both observed with no error (command
`0x071B`, argument `0x00010000`), so the committed CMD7 R1b CC+TC lifecycle
completed. `step=15` is `GTA4L_EMMC_STEP_READY`, reached only after the EXT_CSD
read, 512-byte sector-size check, capacity floor and partition-access check
pass. Per-unit differences are limited to the CID serial/date, the plain
EXT_CSD byte sum (`0x9CB`/`0x9C7`), a few poll-counter ticks and the XNU VM
page counters; the `ext=` field is a plain byte sum, not a checksum. The full
transcription and field comparison are in `docs/emmc-cmd2-crc-audit.md` §2.11.

On the owner's unit the §5.3.3 fail-closed image had failed CMD2, and this
candidate completed identification; the delay is the only source delta. That
supports a timing-sensitive contribution for these runs. It does not establish
the CMD2 cause or prove the delay necessary: `cmd2window` (§5.3.1) and `fmt=2`
(§5.3.2) passed CMD2 on the same unit without it. It is not a reliability
result, and no general fix is adopted. No photo file or hash from these three
runs is in the tree; the transcriptions and the unit/model attribution are
owner-supplied. Identification only: no sector read, bus-width change,
high-speed mode, DMA, CQE, ICE, write, block-device publication or mount was
issued, and CRC checking remains enabled.

### 5.3.5 Stage-1 full-probe result (2026-11-02)

The stage-1 image is the full probe (`gta4l-emmc-probe=1`) built from commit
`417acc4d` (clean) with the committed `cmd1gap` source (`Gta4lEmmc.c
874e6878…`, `Gta4lEmmc.h 5dd223f7…`, `Gta4lEmmcProbe.c 932552a2…`). Its
archive is `e2a013e2…`, payload kernel `194c909f…`, payload `42e7a518…`
(83,886,080 bytes, contract 7/7) and boot image `bf73a39b…` (85,688,320
bytes, gate 18/18 against the v5 baseline `ce4d76e8…`). The BOOT-only flash
record is
`out/gta4l/ram-root-emmc-probe-stage1-evidence/flash-record.json`: pre-flash
`92f4000e…` (the §5.3.4 image plus its unchanged tail), post-flash
`421e8848…`, image-byte readback `bf73a39b…`, tail `a28b4ea2…` unchanged,
`RECOVERY` `14e11370…` unchanged, pulled gate 18/18 and pulled payload contract
7/7. The record was written before the boot. The same image was shared for a
second unit; that unit has no flash record in this tree.

The owner booted it on the owner's SM-T505N and on the SM-T505 whose CID is the
first SM-T505 column in `docs/emmc-cmd2-crc-audit.md` §2.11. Both runs reached
`GTA4L-EMMC PROBE REPORT: rc=0` and stopped at the intentional report panic
after both `CMD17` reads, with `mci=10000072 rca=1 cap=61071360 rev=8 bw=0
ocr=c0ff8080 hc2=0008 ven=00018a9c dll=6000642c dst=00000000 pst=00000000
step=15 log=7`. The common report also carries the recovery-known CSD
`d0270132 0f5903ff f6dbffef 8e404000`; on the owner's unit the CID matches the
recovery-known CID. The two block reports are identical in their selected
words: `BLOCK0 sum=62084b66 words=00000000 00000000 000000ee 00000001` (a
protective-MBR type `0xEE` entry with start LBA 1) and `BLOCK1
words=20494645 54524150 00010000 0000005c` (`EFI PART`, revision 1.0, 92-byte
GPT header). The `BLOCK1` sum differs between units (`84d0543e`/`425cd1ab`)
and the `BLOCK0` sum does not (`62084b66`); the sum is the probe's
`sum*31 + byte` content fingerprint, so the per-device GPT header (disk GUID,
header CRC and related fields) explains the difference without implying
corruption. The selected words do not include the `0x55AA` MBR signature or
the GPT header CRC, so those fields are covered by the full-sector fingerprint
but are not printed by the probe report; the recovery-side readback in
`docs/emmc-cmd2-crc-audit.md` §2.12 reads both directly.

The reports are owner-supplied screen photographs without a photo file or hash
in the tree; the SM-T505 unit has no flash record, and its attribution and
image identity are owner-reported/shared. The run is read-only: no write,
bus-width change, high-speed mode, DMA, CQE, ICE, mount, root switch or
block-device publication. It establishes the first sector-read and MBR/GPT
content evidence for blocks 0 and 1, not a filesystem or broader media
reliability; the separate recovery-side readback in
`docs/emmc-cmd2-crc-audit.md` §2.12 validates the MBR signature and GPT header
CRC. The full transcription and field comparison are in
`docs/emmc-cmd2-crc-audit.md` §2.12.

### 5.3.6 Stage-A extended content-read result (2026-11-02)

The stage-A image rebuilds the same `emmc-probe-1` diagnostic on top of
`53753df` with the extended read phase. The build provenance is
`out/gta4l/ram-root-emmc-probe-stage2-evidence/build-provenance.json`:
`Gta4lEmmc.c 874e6878…`, `Gta4lEmmc.h 7e063c08…`, `Gta4lEmmcProbe.c
034571f1…`, probe archive `57ed9995…`, payload `fdd85d25…` (83,886,080 bytes,
`gta4l-emmc-probe=1`, contract 7/7) and boot image `721e5c87…` (85,688,320
bytes, gate 18/18 against the v5 baseline `ce4d76e8…`). The BOOT-only flash
record is
`out/gta4l/ram-root-emmc-probe-stage2-evidence/flash-record.json`: pre-flash
`421e8848…` (the §5.3.5 image plus its unchanged tail), post-flash
`d55c09bf…`, image-byte readback `721e5c87…`, tail `a28b4ea2…` unchanged,
`RECOVERY` `14e11370…` unchanged, EFS and sec_efs unchanged, and no other
partition written.

The owner booted it on the same SM-T505N. The report reached
`GTA4L-EMMC PROBE REPORT: rc=0` with `step=18 log=30`, the §5.3.5
identification fields and the new content lines: `BLOCKS 2=793877e4 …
21=89457318 22=00000000`, `DATA 34=f418cd33` and `LSTLBA=61071359
sum=a89b26d5`. The GPT gate reported `elba=2 n=84 sz=128 first=34
last=61071326 backup=61071359`; those are exactly the live and pinned header
values. `step=18` is `GTA4L_EMMC_STEP_PROBE_LAST`, and `log=30` is the stage-1
`log=7` plus 23 reads (21 array + first usable + last sector).

The 84 × 128-byte primary array is 10,752 bytes = 21 sectors, so the probe
reads LBAs 2..22. `BLOCKS 22=00000000` is expected because only entries 1..80
are populated and the last four 128-byte entry slots are zero; the backup
array has the same 20 nonzero blocks. The live recovery readback in
`out/gta4l/ram-root-emmc-probe-stage2-evidence/recovery-readback/` read LBAs
0..33, LBA 34 and the last 34 sectors from `/dev/block/mmcblk0` in TWRP and
matched every report sum: all 21 array sums, `DATA 34` and `LSTLBA`. The live
0..33 image is byte-identical to the pinned recovery GPT dump
(`0c7e9c03…`), the live backup tail is byte-identical to the pinned backup
dump (`41912e59…`), both GPT header CRCs are valid, the backup array equals
the primary array, and LBA 0/1 match the §5.3.5 readback. The full
transcription and decoding are in `docs/emmc-cmd2-crc-audit.md` §2.13.

This closes the Stage-A / F1 content claim: repeated single-block CMD17 reads
at the fixed identification clock (1-bit, CRC enabled) returned the GPT
header, the full primary array, the first usable sector and the last sector,
and every content sum is bound to live recovery bytes. It is not an F2
multi-block transfer, an F3/F4 width or speed result, an F5 publication, an F6
write, a filesystem result or a media-reliability test.

### 5.3.7 Optional A2/F2 multi-block mode and attempts 1–2 (2026-11-02)

The same diagnostic build adds an opt-in multi-block mode: `gta4l-emmc-mb=1`
alongside `gta4l-emmc-probe=1`. Without the modifier the image still reports
the stage-A row (`step=18 log=30`, `BLOCKS`) byte for byte. With both
arguments the probe runs the same stage-A pass, then issues one open-ended
`CMD18` over LBAs 2..22 (21 sectors, 512-byte blocks, block count 21), polls
for command and data completion, and ends the transfer with an explicit
`CMD12` (`R1b`, CRC and index checks, CC and TC required, bounded busy poll,
terminal capture before the clear). The transfer-mode word is `TM_READ |
TM_BLOCK_COUNT_EN | TM_MULTI`; the Multi/Single Block Select bit is required
and matches the pinned Samsung multi-block encoding
(`SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_MULTI`,
`drivers/mmc/host/sdhci.c:1172-1175`). `AUTO_CMD12` stays clear. The MD report
is `step=19 log=51 mb=1`, replaces `BLOCKS` with `MBLOCKS 2=… 22=…`, and adds
`mbsame=1` or `mbsame=0 mbdiv=<first differing LBA>` to the `LSTLBA` line.

The first hardware attempt (2026-11-02, the pre-correction build:
`Gta4lEmmc.c 009f0eb9…`, `Gta4lEmmc.h 6f3ff335…`, `Gta4lEmmcProbe.c
e67ad2bc…`, payload `198dc393…`, boot image `cac4356b…`) did not reach the MD
phase. It stopped during identification with `GTA4L-EMMC PROBE FAILED:
stage=1 rc=4294967289` (`-7`, `ERR_CMD_TIMEOUT`), `step=9` (`STEP_CMD2`
completed) and `c3l` at the 1,000,000 µs / 10 µs poll bound. The response
window held `00000500,5836384d,42009005,f4661978`: RESPONSE0 is a plausible
CMD3 R1 status and words 1..3 are the CID words, so the command was accepted
but its completion was not observed. That is the documented H8 CMD3-completion
signature (`docs/emmc-cmd2-crc-audit.md` §6 H8), not an MD/F2 verdict: no
CMD18, no CMD12 and no sector read occurred. The transcription and build
record are in `out/gta4l/ram-root-emmc-probe-stage2b-evidence/`.

The audit after that attempt found that the transfer-mode word omitted
`TM_MULTI`; the controller would have treated the open-ended CMD18 as a
single-block transfer and the helper would have timed out after one sector.
The working tree now sets the bit, both simulators reject a CMD18 whose mode
word lacks it, and both suites carry a mutation control that removes it (core
112/62, probe 44/34 at that attempt). The attempt-1 image never exercised the
defect because it stopped before the MD phase.

Attempt 2 (2026-11-02) used the corrected build from clean `209766c4` with
`Gta4lEmmc.c 942117eb…`, `Gta4lEmmc.h dce8a428…`, `Gta4lEmmcProbe.c
e67ad2bc…`, archive `7c08c6fb…`, kernel `5132f235…`, payload `a5d9e63f…`
(contract 7/7) and boot image `115e5c63…` (gate 18/18). The BOOT-only flash
record is in `out/gta4l/ram-root-emmc-probe-stage2b-attempt2-evidence/`:
pre-flash partition `64fa7190…` (the attempt-1 image `cac4356b…` plus the
unchanged tail `a28b4ea2…`), post-flash `4ca40951…`, image-byte readback
`115e5c63…`, tail and `RECOVERY` `14e11370…` unchanged, and EFS/sec_efs
unchanged. A re-read after the boot confirmed the same image was still on
BOOT.

The image entered the MD phase and failed with the data phase stopped at
block ordinal 20 of the 21-block request:

```text
GTA4L-EMMC PROBE FAILED: mb read lba=2 n=21 rc=4294967285 step=19
mb=20 stop=4294967289/1/1/0000/0000/03f80af6/00000c1b/00000000
fail=1/18/2/0000123a/0000/0021/03f80af6/19
```

`step=19` is `STEP_PROBE_MB` and `rc` is `-11`
(`GTA4L_EMMC_ERR_DATA_TIMEOUT`). The CMD18 record (index 18, argument 2,
command `0x123a`, `normal=0x0021` = `CC | BRR`, `error=0`, present
`0x03f80af6`, step 19) shows the command was accepted and the buffer-read path
was active with no error latched and no `TC`. `mb=20` is the
failing/completed block ordinal (0-based; `Gta4lEmmc.h:438`), not a block
count: the helper sets it to `done` while blocks are pending and to
`count - 1` when all blocks were drained but `TC` was missing. The ordinal
therefore shows at least 20 of the 21 blocks were drained and cannot
distinguish "20 drained with the ordinal-20 block pending" from "all 21
drained with only `TC` missing". The helper's 1,000,000 µs / 10 µs
data-completion poll expired; the terminal sample still showed `CC | BRR`, so
a buffer-read-ready condition was present when polling stopped, but the record
cannot tell whether draining it would have completed the transfer.

The stop record `stop=-7/1/1/0000/0000/03f80af6/00000c1b/00000000` is CMD12
(`0x0c1b`, `R1b`, CRC and index checks, argument 0) with stop-valid 1 and
terminal-capture-valid 1, `normal=0`, `error=0` and the same present state.
The capture flags do not mean completion: neither `CC` nor `TC` was observed in
the stop's 1,000,000 µs bound. `gta4l_emmc_read_multi()` keeps the first
failing result, so the reported `rc` remains the CMD18 data timeout.

This is the first hardware evidence of a multi-block data phase beyond one
block: at least 20 consecutive 512-byte blocks were consumed under one
open-ended CMD18, consistent with `TM_MULTI` taking effect. F2 is not
established because the report does not establish 21/21 data completion or
`TC`, the probe panicked before printing the `MBLOCKS` sums, the stop did not
complete, and no content comparison exists. The transcript is
`out/gta4l/ram-root-emmc-probe-stage2b-attempt2-evidence/stage2b-attempt2-failure.txt`;
the owner photo file is not in this tree and has no hash here. A further
attempt needed a fresh owner approval; the bounded-drain follow-up and its
hardware runs are recorded in §5.3.8.

### 5.3.8 Attempt 3: bounded post-bound drain and hardware runs (2026-11-02)

Attempt 3 keeps the 1,000,000 µs primary data bound unchanged and adds one
bounded secondary window, `GTA4L_EMMC_DATA_DRAIN_US` (500,000 µs), in
`gta4l_emmc_read_multi()`. The window is entered only when the primary poll
expires without either a latched error or `TC`; it drains every ready `BRR`
sector into the caller's buffer and re-checks `TC`. An error that appears in
the window is mapped exactly like an in-loop error and the first failing
result is kept. The explicit `CMD12` stop still runs after either outcome, and
the secondary window is capped by the same
`GTA4L_EMMC_MAX_POLL_ITERATIONS` helper as every other poll, so it cannot
extend the transfer without bound.

Four diagnostics in `struct gta4l_emmc_host` make the outcome visible:
`multi_drain_used`, `multi_drain_resolved`, `multi_drain_blocks` and
`multi_drain_iterations`. The probe's MD failure line gains
`drain=used/resolved/blocks/iterations` between `mb=` and `stop=`; the MD
success `LSTLBA` line appends the same field after `mbsame`/`mbdiv`. The
synthetic width fixture pins the field-wise maxima (1/1/65535/100000).

Simulator coverage now includes completion arriving only inside the drain
window, a last-sector `BRR` that arrives only inside the window, a data error
that appears inside the window, and an attempt-2 replay where every sector
drains but `TC` never arrives (the window is exhausted and the CMD18 result
remains `-11`). The core suite is 136 scenarios with 80 mutation controls and
the probe suite is 55 scenarios with 34 mutation controls, including drain
mutation controls for the window entry, `TC` re-check, drained-block count,
error mapping and resolution flag.

Attempt 3 was committed as `9ed12bd` and built from it: kernel `f034dda2…`,
payload `0048962c…` (contract 7/7) and boot image `f85c18d9…` (gate 18/18
against the v5 baseline `ce4d76e8…`). The owner approved a BOOT-only flash;
the pre-flash partition `4ca40951…` (the attempt-2 image `115e5c63…` plus the
unchanged tail `a28b4ea2…`) was backed up and hash-verified, and the post-flash
partition is `d652a8b5…` with image prefix `f85c18d9…`, tail `a28b4ea2…`, and
`RECOVERY` `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` unchanged.
The pulled boot-image gate is 18/18 and the pulled payload contract is 7/7.
The record is
`out/gta4l/ram-root-emmc-probe-stage2b-attempt3-evidence/flash-record.json`.

The owner booted the image three times. The image was not reflashed between
those boots, and the BOOT partition was not re-read after the first boot, so
all three records are bound by the verified flash/readback above and the
owner's statement that they are boots of this image.

**Boot 1.** The full-probe stage stopped during identification with the same
CMD3-completion signature as attempt 1. The report's key fields decode as
follows; the raw CID and response words are kept only in the untracked
transcript and are not repeated here.

| Field | Report value | Decode |
| --- | --- | --- |
| `stage` | `1` | full-probe stage |
| `rc` | `4294967289` | `-7`, `GTA4L_EMMC_ERR_CMD_TIMEOUT` |
| `fail` | `1/3/65536/0000031a/0000/0000/03f800f0/9` | valid 1, index 3 (CMD3), argument `0x00010000`, command `0x031a`, error 0, normal 0, present `0x03f800f0`, step 9 |
| `rsp` | `00000500,<CID words>` | `RESPONSE0` is a plausible CMD3 R1 status; `RESPONSE1..3` are the CID words |
| `c3p` | `1/0000/0000/03f800f0/0023/07ff` | valid pre-CMD3 sample, no error |
| `c3t` | `1/3/031a/00010000/1/00000500,<CID words>` | valid, stable CMD3 timeout capture |
| `c3l` | `1/3/0000/0000/100000/0000000014c343d/19200000` | 100,000 iterations, `0x14c343d` ticks at 19.2 MHz (about 1.134 s), first/last normal 0 |
| `step`/`log` | `9`/`5/8/3/0` | `STEP_CMD2` last completed |
| `c7p`–`c7x` | all invalid/zero | CMD7 never attempted |
| `drain` | absent | MD phase never reached |

`step=9` is `STEP_CMD2`, so CMD2 completed and CMD3 did not; `fail` names CMD3
(index 3, argument `0x00010000`, command `0x031a`) with no error or normal bits
latched. `c3p` is a valid pre-CMD3 sample with no error; `c3t` is valid,
stable, and holds a plausible CMD3 R1 status in `RESPONSE0` with the CID words
in `RESPONSE1..3`; `c3l` exhausted its 100,000-iteration, 1,000,000 µs / 10 µs
poll bound at 19.2 MHz with neither `CC` nor an error observed. The `c7` block
is invalid/zero, so CMD7 never ran and no CMD18, CMD12 or sector read occurred.
This is the documented H8 CMD3-completion signature, field-for-field the same
as the attempt-1 stage-1 failure except for the panic frame/page/free context
and the `c3l` tick count (a fresh run). The transcription is
`out/gta4l/ram-root-emmc-probe-stage2b-attempt3-evidence/stage2b-attempt3-failure.txt`.

**Boot 2: the drain ran.** The stage-A pass and its GPT gate completed and the
optional `CMD18` ran; the primary 1,000,000 µs bound expired at block ordinal
20, as in attempt 2. The bounded drain then ran its full 500,000 µs / 10 µs
window:

```text
GTA4L-EMMC PROBE FAILED: mb read lba=2 n=21 rc=4294967285 step=19
mb=20 drain=1/0/0/50000 stop=4294967289/1/1/0000/0000/03f80af6/00000c1b/00000000 fail=1/18/2/0000123a/0000/0021/03f80af6/19
```

| Field | Report value | Decode |
| --- | --- | --- |
| `rc` | `4294967285` | `-11`, `GTA4L_EMMC_ERR_DATA_TIMEOUT` |
| `step` | `19` | `STEP_PROBE_MB`; stage-A completed and the MD pass ran |
| `mb` | `20` | 20 drained with the ordinal-20 block pending, or all 21 drained with only `TC` missing |
| `drain` | `1/0/0/50000` | used 1, resolved 0, 0 additional blocks, 50,000 iterations (the whole 500,000 µs / 10 µs window) |
| `stop` | `4294967289/1/1/0000/0000/03f80af6/00000c1b/00000000` | result -7, valid 1, capture valid 1, normal 0, error 0, present `0x03f80af6`, command `0x0c1b` (`CMD12`), argument 0 |
| `fail` | `1/18/2/0000123a/0000/0021/03f80af6/19` | valid 1, index 18 (CMD18), argument 2 (start LBA), command `0x123a`, error 0, normal `0x0021` (CC \| BRR), present `0x03f80af6`, step 19 |

The drain therefore does not resolve the stall: no completion was observed, no
additional block was drained in the window, and `TC` never appeared; the
post-drain sample still shows `CC | BRR` and no `TC`. The explicit `CMD12`
stop again produced a valid terminal capture with no `CC`/`TC` and returned
`-7`; the stop record is field-for-field identical to attempt 2's. Because the
helper returns `-11`, no MB sums are printed and no content comparison exists;
`mb=20` still cannot distinguish 20-of-21 from 21-of-21 drained blocks. If all
21 blocks were already in the buffer (`done == count`), the zero drain-block
count means there was nothing further to read, not that a late block was
missed; the record cannot tell the two cases apart. The
record does not support a marginally longer data bound as the explanation,
though it does not formally exclude a block arriving just after the drain
window. The transcription is
`out/gta4l/ram-root-emmc-probe-stage2b-attempt3-evidence/stage2b-attempt3-boot2-drain-failure.txt`.

**Boot 3: the array read failed first.** The stage-A extended content read
failed at its first sector (LBA 2), after the GPT gate, so the probe stopped
before the data/last-sector reads and the MD phase:

```text
GTA4L-EMMC PROBE FAILED: array read lba=2 rc=4294967285 step=16
fail=1/17/2/0000113a/0000/0002/03f800f0/16
```

`rc` is `-11`, `step=16` is `STEP_PROBE_ARRAY`, and `fail` is valid 1, index 17
(CMD17), argument 2, command `0x113a`, error 0, normal `0x0002` (`TC` only, no
`CC` or `BRR`), present `0x03f800f0`, step 16. The single-block helper requires
`CC`, `BRR` and `TC`; the terminal in-loop sample shows `TC` with no
buffer-ready condition and no `CC`, so the 1,000,000 µs data bound expired and
the helper returned `DATA_TIMEOUT`. This is a new single-block data-phase
failure signature. The same image completed the stage-A pass on boot 2, so it
is intermittent across boots; the report does not show whether the previous
boot's failed `CMD18`/`CMD12` data phase carried over (whether the owner's
reboot power-cycled the card is not recorded). The transcription is
`out/gta4l/ram-root-emmc-probe-stage2b-attempt3-evidence/stage2b-attempt3-boot3-array-failure.txt`.

Taken together: H8 (the pre-MD CMD3-completion timeout) is intermittent rather
than a deterministic property of the build: boot 1 failed there while boots 2
and 3 completed identification. The multi-block stall at block ordinal 20 is
reproducible (attempt 2 and boot 2), and the drain demonstrably does not
resolve it. F2 remains unestablished: no 21/21 completion, no `TC`, no
completed stop and no MB sums or content comparison exist. The attempt-3
image was not re-read after these boots, so all three runs are bound by the
verified flash/readback record, not by a post-boot partition re-read. The
`@Gta4lEmmcProbe.c` panic markers differ by failure path and match each
report function's closing argument line (`:588` full-probe, `:656` single-read,
`:717` multi-read), which is a useful OCR audit check.

### 5.3.9 Stage-B1 bus-width switch and bus test (host-tested; hardware run in §5.3.10) (2026-11-02)

Stage B1 is a new opt-in probe stage, `gta4l-emmc-probe=4`. It keeps the
stage-A identification and 1-bit content pass unchanged, then performs two
width rungs:

- **4-bit.** CMD6 (switch, write-byte, access 3) writes `BUS_WIDTH` (EXT_CSD
  byte 183) value 1, `gta4l_emmc_wait_tran()` confirms the card left the
  switch busy state and reports no R1 error, the host sets `HOST_CONTROL`
  bit 0x02, and EXT_CSD is read back at the new width. The masked readback
  must equal 1. The CMD19/CMD14 bus test writes four bytes (`5a 00 00 00`) and
  compares the first returned word against the complement (`000000a5`).
- **8-bit.** The same sequence with `BUS_WIDTH` value 2, `HOST_CONTROL`
  bit 0x20 and an eight-byte CMD19 pattern (`55 aa 00 00 00 00 00 00`);
  the expected first word is `000055aa`.
- After each successful switch and bus test, the probe repeats the stage-A
  25-value content pass (blocks 0 and 1, LBAs 2..22, first usable LBA 34,
  last sector 61,071,359) and compares every sum with the 1-bit baseline.
  The GPT gate is not re-run; the gated list is reused so a width cannot
  silently change which sectors are compared.

The ordering follows Linux `mmc_select_bus_width()`
(`drivers/mmc/core/mmc.c:1022`): switch the card, change the host width,
then test the bus. `sdhci_set_bus_width()`
(`drivers/mmc/host/sdhci.c:2277`) supplies the host bits. The test pattern,
length and complement comparison follow `mmc_send_bus_test()` and
`mmc_bus_test()` (`drivers/mmc/core/mmc_ops.c:771,847`); the 4-bit length is
4 and the 8-bit length is 8, and only the first length/4 returned bytes are
compared. The production path treats a CMD19 error as a failure and reports
it; Linux ignores `BUS_TEST_W` errors and relies on `BUS_TEST_R`. That is a
diagnostic choice, not a parity claim. The probe sets no separate EXT_CSD
bus-test flag: the pinned reference has no such field and CMD19/CMD14 are the
test. CRC stays enabled; the clock and bus-power state are unchanged.

Report fields (`GTA4L-EMMC B1 REPORT`, one panic):

- header `rc stop mci rca cap rev hci step log`;
- `BW=1` baseline and `BW=4`/`BW=8` lines: `att` attempted, `req` requested
  bits, `sw` the masked EXT_CSD BUS_WIDTH value read back at that width
  (0/1/2), `swrc` the switch result, `tt` whether the bus test ran, `tw` the
  returned test word, `te` the expected word, `trc` the test result, `hc`
  the `HOST_CONTROL` byte after the switch (the restored value after a failed
  readback), `hb`/`ha` the `HOST_CONTROL` byte before and after the width
  write, `c6` the CMD6 R1 status word, `es` the byte sum of the EXT_CSD read
  at that width, `b0`/`b1` the block 0/1 sums, and `same`/`div` the content
  comparison against the 1-bit baseline;
- `BLOCKS 4`/`BLOCKS 8` (21 array sums), `DATA 4`/`DATA 8`, and
  `LSTLBA 4`/`LSTLBA 8`;
- `PAD pqdsd=… pdll=… pdst=… qdsd=… dll=… dst=… bif=none` reads the TLMM
  SDC1 QDSD register at offset 0x00, `CORE_DLL_CONFIG` at SDHCI 0x200 and
  `CORE_DLL_STATUS` at 0x208 immediately before the rung (`p*`) and at the
  end of the run. There is no `BUS_INTF` timing node in the live DT or the
  vendor tree; `grep` finds no `bus-intf`/`bus_intf` property. The closest
  vendor property is `qcom,bus-speed-mode` on `/soc/sdhci@4744000`
  (`device-dump/private/gta4l-dump/merged.dts:3030,3050`), which the probe
  does not parse. `bif=none` is therefore an explicit "not read" marker,
  not a measured mode;
- `FAIL fail=valid/index/argument/command/error/normal/present/step` carries
  the terminal command record on any stop.

`stop` is 0 for the success report, 1-4 for a 4-bit switch, bus-test, content
read or content-mismatch stop, and 5-8 for the corresponding 8-bit stops. A
switch stop binds to `swrc`/`sw`: `sw` is the readback, so an unchanged
4-bit card read after an 8-bit CMD6 shows `req=8 sw=1 swrc=-19` and the host
byte is reverted to `hc=02`. A bus-test stop binds to `trc` and the
`tw`/`te` pair; a content stop binds to `div`, the first differing LBA. The
first failure stops the rung, so no assertion or content read runs on a
width that failed. `rc=-19` is `GTA4L_EMMC_ERR_BUS_WIDTH`, `-20` is
`GTA4L_EMMC_ERR_BUS_TEST`, and `-21` is the probe-only content-mismatch
code. When the rung stops before a width's content pass, that width's
`b0`/`b1`/`same`/`div` fields are zero-initialized; `same=0` with `div=0` is
not a content-disagreement result. A zero `FAIL` record means no terminal
command/data failure was recorded; the stop reason is the `rc`/`swrc`/`trc`
field for that rung. The `c6`/`hb`/`ha`/`es` fields separate the readback
causes: a card that ignored the switch has a clean `c6`, the requested width
bits in `ha`, an `es` equal to the 1-bit sum and an unchanged `sw`; an empty
readback has `es=00000000`; a host width that did not take effect has `ha`
without the requested width bits. §5.3.11 lists the models.

Bounds: the report must fit the 2048-byte panic buffer. The width fixture
drives every B1 field at its widest production form and the probe suite
requires the widest B1 form to fit including the NUL; the success form is
the same layout with ordinary values. The probe suite is 57 scenarios and
34 mutation controls; the core suite is 136 scenarios and 80 mutation
controls. The B1 cases cover 4-bit and 8-bit success, a 1-bit unwind, CMD6
errors, a stuck readback, an empty readback, a lost host width write, switch
busy, an invalid width, a pre-init call, CMD19/CMD14 timeouts, CRC and
status errors, 4-bit and 8-bit pattern mismatches, a width mismatch and the
1-bit refusal, plus probe success, stuck-width, zero-readback,
host-width-stuck, bus-test-failure, 8-bit-stuck, 8-bit-bus-test-failure and
8-bit-content-divergence reports. The only card content written is the
`BUS_WIDTH` configuration byte and the CMD19 test pattern; no partition
sector or user data is written, and the core gains no user-data write path.

Hardware status: the B1 image was built, flashed to BOOT only and booted
three times on 2026-11-02 (§5.3.10). The only boot that reached B1 stopped at
the 4-bit byte-183 readback (`sw=0`, `swrc=-19`, stop=1); no CMD19/CMD14 or
per-width content pass ran. The other two boots stopped before B1. The rung is
host-tested `[S]`; the B1 hardware result is a failing stop, so F3 remains
`[U]`. On hardware, a 4-bit CMD19/CMD14 success plus a matching 4-bit content
pass is the plan's F3 PASS; an 8-bit failure is a separate, exact binding
(`swrc`, `trc`/`tw`/`te`, or `div`) and does not invalidate the 4-bit result.
The B1R2 re-run of this rung (five boots, two B1 stops with the
report-completeness fields) is recorded in §5.3.12.

### 5.3.10 Stage-B1 hardware run: three boots (2026-11-02)

The B1 image was built from `09b8f30` plus the `emmc-probe-4` payload mapping
in `5cf8565`: kernel `f25d3cee…`, payload `cf08c1b2…` (contract 7/7) and boot
image `327fe8c9…` (gate 18/18 against the `ce4d76e8…` v5 baseline). The write
was BOOT-only: pre-flash p69 was `d652a8b5…`, post-flash p69 is `1c64333a…`,
the image prefix read back as `327fe8c9…`, the unchanged tail is `a28b4ea2…`,
and `RECOVERY` `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` were
unchanged. The pulled boot-image gate is 18/18 and the pulled payload contract
is 7/7. The flash record is
`out/gta4l/ram-root-emmc-probe-b1-evidence/flash-record.json`; its `status`
field predates the boot and still says "owner boot pending". The boot results
are in `out/gta4l/ram-root-emmc-probe-b1-evidence/hardware-run-record.json` and
the three owner-supplied transcripts.

The owner booted the same flashed image three times without reflashing. The
BOOT partition was not re-read after the boots, so the reports are bound by the
verified flash/readback record and the owner's statement that they are boots of
this image. No photo file is in the tree. Whether each reboot power-cycled the
eMMC is not recorded; attempt 2 followed the attempt-1 H8 stop.

| Attempt | Report | B1 reached? | Result |
| --- | --- | --- | --- |
| 1 | generic `GTA4L-EMMC PROBE FAILED` | no | `stage=4`, `rc=-7`, `step=9` (`STEP_CMD2`); CMD3 completion timeout, no `c7`, no BUS_WIDTH/CMD19/CMD14/read |
| 2 | `GTA4L-EMMC B1 REPORT` | yes | `rc=-19`, `stop=1`; 4-bit CMD6 post-switch byte-183 readback was 0 (`sw=0`), host width reverted; no CMD19/CMD14 or content pass |
| 3 | generic `GTA4L-EMMC PROBE FAILED` | no | block 1 CMD17 `rc=-11`, `step=15` (`STEP_READY`), `normal=0x0002` (TC only); identification and block 0 succeeded, failure before the GPT gate |

Attempt 1 is the §6 H8 CMD3-completion signature, not a B1 result. Its
`stage=4` is the boot argument, and the `c7` capture fields are all
invalid/zero, so no width command ran. Attempt 3 is the same intermittent
single-block TC-only/no-error data-phase signature recorded for the earlier
attempt-3 boot 3, now at LBA 1 instead of array LBA 2; it also stopped before
B1.

Attempt 2 is the only B1 result. The stage-A 1-bit pass completed
(`step=18`, `log=30`) and its `b0=62084b66`/`b1=84d0543e` match the stage-A
sums. The 4-bit switch then stopped:

```text
GTA4L-EMMC B1 REPORT: rc=4294967277 stop=1 mci=10000072 rca=1 cap=61071360 rev=8 hci=00 step=18 log=30
GTA4L-EMMC B1 BW=1 att=1 req=1 sw=0 swrc=0 tt=0 tw=00000000 te=00000000 trc=0 hc=00 b0=62084b66 b1=84d0543e same=1 div=0
GTA4L-EMMC B1 BW=4 att=1 req=4 sw=0 swrc=4294967277 tt=0 tw=00000000 te=00000000 trc=0 hc=00 b0=00000000 b1=00000000 same=0 div=0
GTA4L-EMMC B1 BW=8 att=0 req=0 sw=0 swrc=0 tt=0 tw=00000000 te=00000000 trc=0 hc=00 b0=00000000 b1=00000000 same=0 div=0
GTA4L-EMMC B1 PAD qdsd=00009fe4 dll=6000642c dst=00000000 bif=none
GTA4L-EMMC B1 FAIL fail=0/0/0/00000000/0000/0000/00000000/0
```

`rc` is `-19` (`GTA4L_EMMC_ERR_BUS_WIDTH`), `stop=1` is the 4-bit switch stop,
and `sw=0` is the post-switch byte-183 readback (still the 1-bit code). A CMD6
or CMD13 failure would have returned before the readback; `swrc=-19` is the
`readback != code` branch after a successful EXT_CSD read at the new width. The
host width was reverted to the previous `hc=00`; the report does not retain the
0x02 value driven during the attempted 4-bit read. `tt=0`/`trc=0` mean no
CMD19/CMD14 ran. `b0`/`b1`/`same`/`div` on the 4-bit and 8-bit lines are
zero-initialized because those content passes never ran; `same=0` is not a
content mismatch. The zero `FAIL` record means no terminal command/data
failure was recorded. The whole per-width `BLOCKS`/`DATA`/`LSTLBA` block is
zero for the same reason.

The failure fields match the simulator's `probe-b1-stuck-width` case, so the
report is covered by the host model. The report cannot distinguish a card that
ignored the CMD6 BUS_WIDTH write from a 4-bit readback that returned the old
value or zero, nor from a host-width write that did not take effect before the
read; it keeps neither the CMD6 response nor the pre-revert `HOST_CONTROL`
value. Attempt 2 is one observation and does not establish that the failure is
repeatable.

Audit verdict: one B1 result and two pre-B1 failures, not three contradictory
B1 verdicts. No attempt produced `rc=0 stop=0`, a 4-bit CMD19/CMD14 success, an
8-bit attempt or a per-width content comparison. F3 remains `[U]`. The full
field decode, simulator comparison and the attempt-3 model gap are in
`docs/emmc-cmd2-crc-audit.md` §2.16. No code change follows from this record;
the planning model must decide the next diagnostic before any further build,
flash or boot. Attempt 3's TC-only/no-error sample has no field-for-field
simulator case: the simulator's `probe-read-timeout` expects a latched
`DATA_TIMEOUT` (`error=0x0010`, `normal=0`), while the hardware report has
`error=0` and `normal=0x0002`.

### 5.3.11 Stage-B1 report-completeness audit (host-only) (2026-11-02)

The §5.3.10 attempt-2 report stopped at the 4-bit byte-183 readback but could
not say why: it kept neither the CMD6 response, nor any `HOST_CONTROL` value
driven during the attempted width, nor evidence that the readback returned
data at all. This batch extends the B1 evidence without changing the command
sequence or any register semantic:

- `struct gta4l_emmc_bus_result` gains `switch_status` (CMD6 R1),
  `host_before`/`host_after` (`HOST_CONTROL` before and after the width write)
  and `readback_sum` (byte sum of the EXT_CSD read at the attempted width).
  `gta4l_emmc_ext_csd_sum()` is shared with `gta4l_emmc_parse_ext_csd()`.
- Every `BW=4`/`BW=8` line gains `hb=`, `ha=`, `c6=` and `es=`. The `BW=1`
  baseline gains only `es=` because that rung issues no CMD6 or host width
  write.
- The `PAD` line now carries the pre-rung (`pqdsd`/`pdll`/`pdst`) and final
  (`qdsd`/`dll`/`dst`) samples. The three pre-rung samples are taken once,
  immediately before the 4-bit rung.
- `host_after` is captured immediately after the `HOST_CONTROL` write. On a
  failed EXT_CSD readback the host width is still restored, and `hc` reports
  the restored value; `ha` keeps the value actually read back during the
  attempt.

The fields separate four causes that produce the same old `sw=0 swrc=-19`
line:

| Cause | `c6` | `ha` | `es` | `sw` |
| --- | --- | --- | --- | --- |
| card ignored the CMD6 write | clean (no R1 error) | has the requested bits | equals the 1-bit sum | unchanged |
| readback returned no data | clean | has the requested bits | `00000000` | 0 |
| host width write did not take effect | clean | lacks the requested bits | 0 (read fails) | 0 |
| pad/DLL disturbed by the rung | clean | has the requested bits | expected sum | expected or not |

The last row is not a simulated fault; it is an observation the `p*`/`q*`
pair makes. The ordering and bits are pinned: `SDHCI_HOST_CONTROL` is 0x28,
`SDHCI_CTRL_4BITBUS` is 0x02 and `SDHCI_CTRL_8BITBUS` is 0x20
(`drivers/mmc/host/sdhci.h:110,112,120`); `mmc_select_bus_width()`
(`drivers/mmc/core/mmc.c:1022`) switches the card, changes the host width and
then checks the card, and `mmc_compare_ext_csds()` (`:729`) re-reads EXT_CSD
at the new width. The new fields observe that sequence; they do not change it.

Host model and controls:

- `probe-b1-zero-readback` makes the CMD8 read at the 4-bit card width return
  an all-zero buffer with no error, so `es=00000000` while `ha=02`.
- `probe-b1-host-width-stuck` drops the width bits from the `HOST_CONTROL`
  write and makes the CMD8 read at the switched card width fail with a data
  CRC, so `ha=00` and `es=00000000` with `swrc=-12`.
- `probe-b1-stuck-width` keeps the existing card-ignored model: byte 183 stays
  0, `ha=02`, `es` equals the 1-bit sum.
- The three readback-cause scenarios assert the exact tuples above. The
  success scenario asserts each width's readback sum against
  `sim_ext_csd_sum()`; `probe-b1-success`, the stuck-width case and the
  synthetic `report-width-b1` form were updated to the widened layout.
- The field-wise fixture fills `switch_status`, `host_before`, `host_after`
  and `readback_sum` at their 32-bit maxima, and the report-budget check still
  requires the widened maximum to fit the 2048-byte panic buffer.

Host checks: `scripts/test-gta4l-emmc.py` passes 136 scenarios and 80 mutation
controls (unchanged); `scripts/test-gta4l-emmc-probe.py` passes 57 scenarios
and 34 mutation controls (55/34 before this batch). No build, flash or boot was
performed. The old attempt-2 transcript cannot be retroactively decoded: it
predates these fields. A future boot of a rebuilt B1 image would separate the
three readback causes directly. F3 remains `[U]`, and the next device step
still requires explicit owner approval.

### 5.3.12 Stage-B1R2 diagnostic run: five boots (2026-11-02)

The `24f8c9d` report-completeness build was packaged as kernel `346ff1e0…`,
payload `37ff0a62…` (contract 7/7) and boot image `e6d9d402…` (gate 18/18
against the `ce4d76e8…` v5 baseline), then flashed BOOT-only: pre-flash p69
`1c64333a…`, post-flash p69 `ac356ef1…`, the image prefix read back as
`e6d9d402…`, the unchanged tail `a28b4ea2…`, and `RECOVERY` `14e11370…`,
EFS `51fa824c…` and sec_efs `3a265d58…` unchanged. The pulled gates pass
18/18 and 7/7. The owner booted the same image five times without
reflashing and supplied the reports as text (no photo file or hash in the
tree). Every reboot was a power + volume-down warm reset; no full shutdown or
power cycle was performed. The BOOT partition was not re-read after the
boots at record time; the reports are bound by the verified flash/readback
record and the owner statement. Evidence:
`out/gta4l/ram-root-emmc-probe-b1r2-evidence/` (`flash-record.json`,
`hardware-run-record.json`, five transcripts).

| Boot | B1? | Result |
| --- | --- | --- |
| 1 | yes | `B1 REPORT rc=-19 stop=1 step=18 log=30`; `BW=1` pass (`es=000009cb`, `b0=62084b66`, `b1=84d0543e`, `same=1`); `BW=4` stop `sw=0 swrc=-19 tt=0 hc=00 hb=00 ha=02 c6=00000900 es=000009cb`; `BW=8 att=0`; `FAIL` all zero. |
| 2 | yes | Identical tuple to boot 1 (stack `pages`/`free` differ only). |
| 3 | no | Block-1 CMD17 `-11`, `step=15`, `fail=1/17/1/0000113a/0000/0002/03f800f0/15` (`TC` only, `error=0`), before the GPT gate. |
| 4 | no | Array LBA 2 CMD17 `-11`, `step=16`, `fail=1/17/2/0000113a/0000/0002/03f800f0/16`, after the GPT gate. |
| 5 | no | H8 CMD3 timeout `-7`, `step=9`, `fail=1/3/65536/0000031a/0000/0000/03f800f0/9`; `c3l` 100,000 iterations (ticks `0x14c2bdf`); no CMD7 or width command. |

Wire semantics of the two B1 tuples:

- `c6=00000900` is a clean R1 (TRAN + READY_FOR_DATA, error mask clear) and
  the CMD13 TRAN wait passed, so CMD6 was accepted.
- `hb=00`/`ha=02`: `HOST_CONTROL` read 0 before the width write and the
  4-bit bit `0x02` after it, so the host register write took effect; `hc=00`
  is the restored pre-attempt value after the failed readback.
- `es=000009cb` equals the 1-bit baseline sum on both lines: the EXT_CSD
  read at the attempted width succeeded and returned the same 512 bytes.
  Byte 183 was 0 (`sw=0`), so the card-visible BUS_WIDTH stayed 1-bit; a
  successful switch would read `000009cc`.
- The failed width's content fields and its `BLOCKS`/`DATA`/`LSTLBA` lines
  are zero-initialized (no content pass ran); the `BW=8` line shows `att=0`.
- The `PAD` pre-rung and end-of-run samples are identical
  (`00009fe4`/`6000642c`/`00000000`), so no register-level pad/DLL drift was
  observed.

Status: F3 remains `[U]`. The 4-bit stop is repeatable (2/2 B1-reaching
boots) and its report-level cause is narrowed to the card-visible BUS_WIDTH
no-op; the empty-readback and lost-host-width causes are excluded for these
boots, and no pad/DLL register drift accompanied the stop. The report cannot
observe the pad-level width, so a card-side no-op and a data path that did
not apply the new width stay indistinguishable; the warm-only reboot method
also leaves a retained card state open. Three of five boots stopped before
B1 (two TC-only/no-error CMD17 timeouts, one H8 CMD3 timeout); the TC-only
signature still has no field-for-field simulator case. Host checks are
unchanged (core 136/80, probe 57/34). No code change follows from this
record; any further device step needs an explicit owner approval.

### 5.3.13 Stage-B1R2 cold session and report binding (2026-11-02)

A cold-boot session booted the same flashed `emmc-probe-4` image three times
without reflashing, each boot a full power-down then power-on (no
force-reboot key combo). Cold boot 1 stopped before B1 at the block-1 CMD17
`-11`/`step=15` TC-only tuple
(`1/17/1/0000113a/0000/0002/03f800f0/15`); cold boots 2 and 3 reached B1 and
reproduced the 4-bit stop byte-for-byte (`c6=00000900`, `hb=00`, `ha=02`,
`es=000009cb`, `sw=0`, `swrc=-19`, `BW=8 att=0`, `FAIL` all zero), differing
only in the stack `free` counter (`pages` is identical). The same session's read-only
`adb shell sha256sum /dev/block/mmcblk0p69` returned `ac356ef1…`, matching the
flash record's post-flash hash, so `partition_verified_again_after_boot` is
true and all eight boots (five warm, three cold) are bound to the flashed
bytes. Evidence:
`out/gta4l/ram-root-emmc-probe-b1r2-evidence/` (`hardware-run-record.json`,
`flash-record.json`, eight transcripts and `b1r2-p69-reread.txt`).

Status: the 4-bit stop is now 4/4 across warm and cold boots, which excludes a
card state retained across warm resets as its cause; the pre-B1
TC-only/no-error CMD17 signature is cold-vulnerable too, so the two
signatures are independent. The report still cannot observe the pad-level
width, so a card-side no-op and a data path that did not apply the new width
remain indistinguishable. F3 remains `[U]`. The plan of record (§3.1.3) is
the owner-approved B1C rung (`gta4l-emmc-probe=5`): on exactly this tuple it
continues to the CMD19/CMD14 bus test and the stage-A content pass at 4-bit,
re-reads byte 183, restores the host width and emits one report, with no
8-bit attempt; the outcome does not auto-advance F3. Host checks are
unchanged (core 136/80, probe 57/34). No code change follows from this
record; the separately approved B1C batch implements the plan's continuation.

### 5.3.13.1 Stage-B1C continue-past-readback host implementation (2026-11-02)

`gta4l-emmc-probe=5` implements the plan's B1C rung on top of the stage-B1
image. Nothing in the command order, register sequence, clock or power
policy changes. The only behavior addition is the continue branch: when
`gta4l_emmc_set_bus_width()` fails at 4-bit with exactly the observed tuple
(`swrc=-19`, `sw=0`, `hb=00`, `ha=02`, `c6=00000900` clean R1 in TRAN with
READY_FOR_DATA, and `es` equal to the 1-bit baseline), the probe re-applies
the 4-bit host width (the core reverted it), runs one CMD19/CMD14 bus test,
repeats the stage-A content pass, re-reads EXT_CSD byte 183 and its byte
sum, restores the host width and emits one `GTA4L-EMMC B1 REPORT` with
`stop=1`. `rc` is the continuation error when one stops the continuation
(for example `-20` for a bus-test mismatch or `-21` for a content
mismatch); a completed continuation keeps the verification result `-19`.
Every other failed tuple and every 4-bit success path keeps the stage-B1
behavior, and the 8-bit rung is not attempted after a continuation.

The `BW=4` line gains `bc=` (1 when the continuation ran, 0 otherwise) and,
for stage 5 only, `e183a=`/`esa=` (post-continuation byte 183 and byte sum).
`sw`/`swrc` keep their stage-B1 meanings and are never folded with the
continuation result; the continuation result rides in `tt`/`tw`/`te`/`trc`,
the content fields and the re-read fields. `hc` is the end-of-rung
HOST_CONTROL sample, re-read after the restore. A continuation hard error
stops at that step and the report carries the completed fields. Stage-4
reports are byte-identical to the pre-batch output; stage-5 non-continue
reports are the stage-4 fields plus `bc=0 e183a=00000000 esa=00000000`.

Host checks: the probe suite is 67 scenarios and 39 mutation controls (57/34
before), adding `probe-b1c-success`, `probe-b1c-pass`,
`probe-b1c-late-commit`, `probe-b1c-bustest-fail`,
`probe-b1c-content-div`, `probe-b1c-content-error`,
`probe-b1c-reread-error`, `probe-b1c-c6-error`,
`probe-b1c-zero-readback` and `probe-b1c-host-width-stuck`, plus five
mutation controls (continue entered on a non-entering tuple, a continuation
that does not stop, a wrong content baseline, a skipped host-width restore
and a wrong continue marker). The core suite is unchanged at 136/80. The
`report-width-b1` fixture materializes `bc=1`, `e183a=ffffffff` and
`esa=ffffffff`, and the report still fits the 2048-byte panic buffer. The device run followed
the approval and is recorded in §5.3.14; F3 remains `[U]`.

### 5.3.14 Stage-B1C hardware run: CMD19 write-phase timeout (2026-11-03)

The B1C image was built from clean `5ad1dd6`: kernel `2879f5e3…`, payload
`18f15dd0…` (contract 7/7) and boot image `370b962f…` (gate 18/18 against the
`ce4d76e8…` v5 baseline). The write was BOOT-only: pre-flash p69 was
`ac356ef1…`, post-flash p69 is `c6a9f8e0…`, the image prefix read back as
`370b962f…`, the unchanged tail is `a28b4ea2…`, and `RECOVERY`
`14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` were unchanged. The
flash record is
`out/gta4l/ram-root-emmc-probe-b1r3-evidence/flash-record.json`; the results
are in `out/gta4l/ram-root-emmc-probe-b1c-evidence/`
(`hardware-run-record.json` and eight transcripts).

The owner booted the same flashed image seven times without reflashing: boots
1 and 4 were cold boots and the other five were warm force reboots. Boots 3
and 6 reached the B1C continuation. Both entered it on exactly the pinned
tuple (`bc=1`, `c6=00000900`, `hb=00`→`ha=02`, `es=000009cb`, `sw=0`,
`swrc=-19`), then timed out in the CMD19 write phase:

    tt=1 tw=00000000 te=00000000 trc=-11
    fail=1/19/0/0000133a/0000/0011/03f801f6/18 @Gta4lEmmcProbe.c:1085

CMD19 was accepted and both `CC` and `BufferWriteReady` were latched
(`normal=0x0011`), but the block never completed: no `TC`, no error bit. The
present state `0x03f801f6` is the project's first data-write-direction
terminal sample; every read sample is `0x03f800f0`. The two reports are
identical field-for-field except for the stack `pages`/`free` counters, and a
full cold power cycle occurred between them, so the timeout is deterministic
conditional on reaching the rung and is not caused by card state retained
from an earlier interrupted CMD19. The cold boot itself did not reach the
continuation. The `PAD` samples are unchanged
(`0x00009fe4`/`0x6000642c`/`0x00000000`).

On the hard error the rung skipped the content pass and the post-continuation
EXT_CSD re-read, so `b0`/`b1`/`same`/`div` and `e183a`/`esa` are
placeholders, not observed values; `hc=00` is the restored host width and
`BW=8 att=0` is untouched. Both reports again read byte 183 = 0 and the
byte-exact 1-bit sum at the attempted 4-bit width.

The other five boots stopped before B1: four byte-identical block-1 CMD17
TC-only/no-error timeouts (boots 1, 4, 5 and 7) and one H8 CMD3-completion
timeout (boot 2, `stage=5`). The same-session read-only p69 re-read after
boot 3 matched the post-flash hash, binding boots 1–3; boots 4–7 ran on the
same un-reflashed image by the owner's statement, and the partition was not
re-read after them.

Status: the CMD19 write-phase timeout is a new, deterministic terminal class
conditional on reaching the rung. It does not separate a width-caused timeout
from an independent first-write property: the "wire never left 1-bit" and
"the card switched but rejects the 4-byte write" models both remain alive,
and the `trc=-20` read-back-mismatch class remains unobserved. F3 remains
`[U]`; no per-width content pass or 8-bit attempt ran. Host checks are
unchanged (core 136/80, probe 67/39). No code change follows from this
record. The next rung candidate is a 1-bit CMD19/CMD14 bus test before any
width switch (`gta4l-emmc-probe=6`), which issues no CMD6 and keeps the same
non-persistent bus-test-pattern write class; it needs a fresh explicit
BOOT-only flash approval.

### 5.3.15 Stage-B1C 1-bit CMD19/CMD14 discriminator (2026-11-03)

The `gta4l-emmc-probe=6` host rung adds one CMD19/CMD14 round trip at the
identification width (1-bit) after the stage-A content pass and before the
first CMD6. Its purpose is to separate the two live models for the CMD19
write-phase timeout: a passing 1-bit test shows the write path completes at
1-bit, so the 4-bit timeout is width-caused; a 1-bit timeout or pattern
mismatch shows the write path is broken independently of width.

The core now accepts `bits == 1` in `gta4l_emmc_bus_test()`. Because the
SDHCI PIO buffer is 32-bit, the 1-bit form writes a 4-byte block with the
first byte `0x5A` and compares the first returned byte (`0xA5`); that byte
carries the first two pattern bits on the single line. The 1-bit test issues no CMD6 and keeps the same
non-persistent bus-test-pattern write class already approved for B1C.

On stage 6, a failed 1-bit test stops the rung with `stop=9` and the report
carries the completed 1-bit fields plus the terminal `FAIL` record; no width
switch or content pass follows. A passing 1-bit test falls through to the
stage-5 flow unchanged, so every stage-6 boot that reaches the rung also
records the 1-bit `tt`/`tw`/`te`/`trc` on the `BW=1` line. For stages 4 and
5 the `BW=1` line stays byte-identical (`tt=0 tw=00000000 te=00000000
trc=0`) because the discriminator fields are zero.

Host checks: the core suite is 138 scenarios and 83 mutation controls (136/80
before), adding the 1-bit success, pattern-mismatch and wrong-width cases and
three mutation controls (the 1-bit guard, pattern and length). The probe
suite is 71 scenarios and 42 mutation controls (67/39 before), adding
`probe-b1-1bit-pass`, `probe-b1-1bit-timeout`,
`probe-b1-1bit-mismatch` and `probe-b1-1bit-continue` plus three mutation
controls (skipped discriminator, failure that continues to the switch, wrong
stop code). The `report-width-b1` fixture now materializes the 1-bit fields
and still fits the 2048-byte panic buffer. The build, BOOT-only flash and
thirteen-boot device run are recorded in §5.3.16.

### 5.3.16 Stage-B1C-1 hardware run: write phase times out at 1-bit (2026-11-03)

The stage-6 image was built from clean `5f57dcb`: kernel `a20d6269…`,
payload `fdaa507d…` (contract 7/7) and boot image `90cf074c…` (gate 18/18
against the `ce4d76e8…` v5 baseline). The write was BOOT-only: pre-flash p69
was `c6a9f8e0…` (the B1C post-flash hash), post-flash p69 is `1ea626f5…`,
the image prefix read back as `90cf074c…`, the unchanged tail is
`a28b4ea2…`, and `RECOVERY` `14e11370…`, EFS `51fa824c…` and sec_efs
`3a265d58…` were unchanged. The flash record is
`out/gta4l/ram-root-emmc-probe-b1c1-evidence/flash-record.json`; the boot
record is `hardware-run-record.json` in the same directory.

The owner booted the same image thirteen times without reflashing (nine
owner-stated resets — cold, warm, warm, cold, warm, warm, cold, warm, warm —
then four cold sampling boots) and re-read BOOT read-only after boot 9 and
after boot 13; both reads matched `1ea626f5…`, so all boots are bound to the
flashed bytes.

Boots 1 and 12, both cold, reached the 1-bit discriminator. Both timed out in
the CMD19 write phase with the same terminal class as the 4-bit continuation
(§5.3.14):

    rc=-11 stop=9 step=18 log=31
    BW=1 att=1 req=1 sw=0 swrc=0 tt=1 tw=00000000 te=00000000 trc=-11
    BW=4 att=0 ... c6=00000000   BW=8 att=0
    FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/18 @Gta4lEmmcProbe.c:1090

`es=000009cb`, `b0=62084b66`, `b1=84d0543e`, the `PAD` line
(`qdsd=00009fe4`, `dll=6000642c`, `dst=00000000`, `bif=none`) and the whole
`FAIL` tuple are identical between the two reports; only the stack
`pages`/`free` counters and the second stack frame differ. No CMD6 was
issued on either boot (`BW=4 att=0`, `bc=0`), so the failing path is a
data-write hang at the identification width with no card-state write
involved. CMD19's command phase completed (`CC`) and `BufferWriteReady`
latched; the data phase never produced `TC` and no error bit appeared before
the 1 s bounded poll expired. This is the §3.1.4 decision-table
"write-phase timeout" row: the write path is broken independent of bus
width, and the width-caused model (with both prior width-dependent models)
is excluded.

The other eleven boots stopped pre-B1: eight H8 CMD3-completion timeouts
(`fail=1/3/65536/0000031a/0000/0000/03f800f0/9`, `stage=6`, at `:838`) and
three CMD17 TC-only/no-error timeouts — block 1 at `step=15`
(`fail=1/17/1/…`) and, for the first time, the GPT array's first sector at
LBA 2, `step=16` (`fail=1/17/2/…`). The `trc=-20` read-back-mismatch class
remains unobserved, and the byte-183 readback paradox is unchanged
(`es=000009cb` again at the 1-bit baseline).

Status: F3 remains `[U]` — no bus-test pass, no per-width content pass and
no 8-bit attempt. The write-failure question is answered (width-independent),
but the cause (card-side write wedging versus the host PIO path) is not. The
next rung, chosen by the owner, is a host data-timeout audit: both hang
classes (CMD17 TC-only and the CMD19 wedge) record no data-timeout interrupt
even though the core writes the DTO bit in `ERROR_INT_ENABLE`; the rung reads
back the timeout/enable state, arms the minimum timeout for a healthy
multi-block read longer than the counter, and repeats the 1-bit CMD19 wedge
with an extended window. The host batch is implemented and host-tested in
§5.3.17 and the twelve-boot device run is recorded in §5.3.18: the
controller never raised data-timeout, so F3 remains `[U]` and the next rung
is B2 (plan §3.2), pending a fresh owner approval.

### 5.3.17 Stage-B1C-2 data-timeout audit rung (gta4l-emmc-probe=7, 2026-11-03)

The `gta4l-emmc-probe=7` stage runs after the stage-A pass and before any
CMD6. It issues no card-state write; E2 repeats the already-approved
non-persistent 1-bit CMD19 bus-test write. The stage always reports
(`GTA4L-EMMC DTO REPORT`) and stops; there is no on-device retry.

Why: both observed hang classes — the pre-B1 CMD17 TC-only/no-error stop and
the CMD19 write wedge — record no data-timeout status although the core
writes `ERROR_INT_ENABLE = INT_ERR_MASK` (DTO bit included) and the software
windows are 1 s. The 400 kHz setup writes `CLOCK_CONTROL = 0x0001` as a full
write, leaving TOUTVAL `[7:3] = 0`, the 2^13-cycle minimum (about 20.5 ms at
400 kHz). The rung separates "the counter never decrements in these states"
from "the timeout status never reaches the register".

- R1: read back `CLOCK_CONTROL` (0x2C), `NORMAL_INT_ENABLE` (0x34),
  `ERROR_INT_ENABLE` (0x36), `NORMAL_INT_SIGNAL` (0x38),
  `ERROR_INT_SIGNAL` (0x3A) and `ERROR_INT_STATUS` (0x32) at rest. The
  expected device values are `clk=0007 ne=0033 ee=07ff ns=0000 se=0000
  es=0000`; the report records the observed values, not the expectation.
- E1: force TOUTVAL=0 (preserving the clock-enable bits), read the value back
  (`arm`), then read 8 blocks (about 83 ms at 1-bit/400 kHz, well past the
  minimum) through the existing CMD18+CMD12 reader. A latched
  `ERROR_INT_STATUS` DTO bit (`0x0010`) is the "counter works" result; a clean
  completion or a software-bound timeout with `error=0` is the "counter did
  not fire" result. The E1 fail tuple, block ordinal, drain and explicit-stop
  fields are reported.
- Q: re-read block 0 to prove the bus recovered; a failure records stop=11
  and skips E2.
- E2: repeat the 1-bit CMD19/CMD14 bus test with the optional per-host
  `bus_test_window_us` override at 8 s (800,000 iterations of the 10 us
  step, a dedicated `GTA4L_EMMC_MAX_BUS_TEST_ITERATIONS` cap; every other
  poll keeps the universal 100,000-iteration cap). The extended bound lets a
  late controller timeout latch; the report carries `att/win/rc/tt/tw/te/trc
  /hc` and the terminal `FAIL`.

Stop codes: 0 = E1 latched a DTO and E2 ran; 10 = E1 never latched a DTO and
E2 ran; 11 = the recovery read failed (E2 skipped); 12 = the forced-TOUTVAL
write did not read back; 13 = E2 passed (write path completed on this boot);
14 = E2 returned an error class other than the timeout or pattern mismatch.
Every row reports and stops; an uncovered row is an Appendix-B stop.

Host checks: the probe suite is 75 scenarios and 46 mutation controls
(71/42 before), adding `probe-dto-fired`, `probe-dto-silent`,
`probe-dto-q-fail` and `probe-dto-e2-pass` plus four mutation controls
(ignored extended window, silent E1 accepted, skipped recovery read and a
wrong E1 block count). The `probe-dto-fired` row also proves the extended
window is used by counting `ERROR_INT_STATUS` reads (>= 700,000); the
ignored-window core mutation drops that below the bound and is rejected. The
DTO report is 518 bytes at device-shaped values, far under the 2048-byte
budget; no width fixture is involved. The diagnostic wrapper accepts
`emmc-probe-7`. The build, BOOT-only flash and twelve-boot device run are
recorded in §5.3.18; F3 remains `[U]` and the ladder does not auto-advance.

### 5.3.18 Stage-B1C-2 hardware run: the data-timeout counter is silent (2026-11-03)

The stage-7 image was built from clean `d158378`: kernel `9d7ae51e…`,
payload `b4171d96…` (contract 7/7) and boot image `3cc5f9f1…` (gate 18/18
against the `ce4d76e8…` v5 baseline). The write was BOOT-only: pre-flash p69
was `1ea626f5…` (the B1C-1 post-flash hash, backed up and verified),
post-flash p69 is `13f88342…`, the image prefix read back as `3cc5f9f1…`,
the unchanged tail is `a28b4ea2…`, and `RECOVERY` `14e11370…`, EFS
`51fa824c…` and sec_efs `3a265d58…` were unchanged. The pre-flash battery
was 100% and the pulled gates pass 18/18 and 7/7. The flash record is
`out/gta4l/ram-root-emmc-probe-dto-evidence/flash-record.json`; the build
provenance is `build-provenance.json` in the same directory.

The owner booted the same image twelve times without reflashing (boot 1
cold, boots 2–12 warm) and re-read BOOT read-only after the batch:

    $ adb shell sha256sum /dev/block/mmcblk0p69
    13f883420f9514844b84446d87f099ed5c88c49827cf3e54ed5fa89dc5327465  /dev/block/mmcblk0p69

The read matches the post-flash hash, so all twelve boots are bound to the
flashed bytes. Five boots (1, 3, 8, 9 and 10) reached the DTO stage and
reported `rc=0 stop=12` with field-for-field identical values; only the
stack `pages`/`free` counters differ. Six boots (2, 5, 6, 7, 11 and 12)
stopped pre-B1 at the block-1 CMD17 TC-only/no-error timeout
(`rc=-11 fail=1/17/1/0000113a/0000/0002/03f800f0/15`), and boot 4 stopped at
the post-CMD7 CMD13 completion timeout
(`stage=7 rc=-7 fail=1/13/65536/00000d1a/0000/0000/03f800f0/12`). Every stop
is in a known class; no new failure surface appeared.

The five reports carry:

    mci=10000072 rca=1 cap=61071360 rev=8 hci=00 step=22 log=40
    R1 clk=0007 ne=0033 ee=07ff ns=0000 se=0000 es=0000 arm=0005
    E1 n=8 rc=-11 fail=1/18/2/0000123a/0000/0021/03f80af6/22
    E1S mb=7 sr=-7/1/1 dr=1/0/0/50000
    Q rc=-17
    E2 att=0 win=0 rc=0 tt=0 tw=00000000 te=00000000 trc=0 hc=00
    PAD qdsd=00009fe4 dll=6000642c dst=00000000
    FAIL fail=1/17/0/0000113a/0000/0000/03f80af6/22 @Gta41EmmcProbe.c:1199

`R1 clk=0007` shows `CLOCK_CONTROL` already at TOUTVAL `[7:3] = 0` at rest:
the 2^13-cycle minimum (about 20.5 ms) was already armed and the E1 forced
write was a no-op. `arm=0005` differs from the expected `0007` only in the
read-only `INT_STABLE` bit (0x0002), which drops after a full
`CLOCK_CONTROL` write; the stage compares the whole register
(`Gta4lEmmcProbe.c:1238` and `:1296`), so `stop=12` is a probe-gate artifact,
not a device datapoint. E1 and Q run before that gate (`:1246`/`:1281`), so
the E1 evidence stands; only E2 was skipped.

E1 returned `-11` from the 1 s software window, not from a latched
controller timeout: `fail_error=0x0000` means no error bit of any kind
fired, DTO included. `fail_normal=0x0021` is CC plus BufferReadReady and
`mb=7` records seven blocks streamed before the stop — about 3.5 KiB at
400 kHz/1-bit, roughly 3× the minimum curtain — while the 5 s drain
(`dr=1/0/0/50000`) expired unresolved. The card then stopped answering:
CMD12 got no R1b (`E1S sr=-7/1/1`), and the block-0 recovery read ended in
ERR_INHIBIT (`Q rc=-17`) with the terminal FAIL carrying
`normal=0x0000`. The controller therefore never raises data-timeout in this
configuration, even on an active transfer that was still progressing — the
row-level fact that also covers the pre-B1 CMD17 TC-only class. E2 is moot
in the silent branch and was not run, so this batch adds no bus-test
datapoint. F3 remains `[U]`; the `trc=-20` class remains unobserved and the
byte-183 readback paradox is unchanged.

Future guard: if a later batch touches the probe, the arming gate should
compare only the TOUTVAL bits (`dto.arm & TOUTVAL_MASK == expected &
TOUTVAL_MASK`) so a live RO status bit cannot block the report. The
`INT_STABLE` drop after a full `CLOCK_CONTROL` write is a recorded hardware
quirk. No code change is made in this records batch. The next rung is B2
(high-speed timing, plan §3.2), which needs a fresh owner approval.

### 5.3.19 Stage-B2 HS-timing rung (gta4l-emmc-probe=8, host batch 2026-11-03)

The `gta4l-emmc-probe=8` stage runs after the stage-A pass and before any
width CMD6. It measures one thing: whether the card accepts the 52 MHz HS
class and whether the data path preserves content at the raised clock on
the only proven-good path (1-bit single-block CMD17 reads). It issues no
CMD19/CMD14, no width switch and no user-data write; the only card write is
the EXT_CSD `HS_TIMING` byte (185). The rung always emits a
`GTA4L-EMMC B2 REPORT` and stops; there is no on-device retry and no
fall-through.

Steps:

| Step | Action |
| --- | --- |
| R | Read byte 185 and `CLOCK_CONTROL`; sample the pad/DLL state at 400 kHz. |
| S | CMD6 SET index 185 value 1, wait TRAN/ready and read byte 185 back. Only a confirmed `hs=1` raises the GCC SDCC1 APPS RCG to the pinned 50 MHz floor row (`0x20B`, M/N/D zero) and re-enables/verifies the card clock. |
| T | Repeat the full stage-A content pass (block 0/1, the 21 GPT-array sectors, the first usable sector and the last sector) at 50 MHz and compare every sum with the held 400 kHz baseline. |
| V | Drop the clock to 400 kHz first, then CMD6 byte 185 = 0, read both back and re-read block 0 at 400 kHz. |

The 52 MHz class is floored to 50 MHz on this clock tree: the pinned
`gcc-sm6115.c` table has no 52 MHz entry and `clk_rcg2_floor_ops` selects
`F(50000000, P_GPLL0_OUT_AUX2, 6, 0, 0)`, which encodes source select 2
and divider field 11 (`0x20B`) with the MND block in direct mode. The
parent rate is a table value, not a measurement. `CLOCK_CONTROL` carries
only the enable/stable bits, so the frequency verdict comes from the RCG
readback.

Stop codes: 0 pass with a clean V and recovery read; 10 switch step failed
(CMD6/TRAN error, the byte-185-ignored class, or the pre-switch EXT_CSD
read failure); 11 clock step failed (RCG update, `INT_STABLE` wait or RCG
readback); 12 content read failure or first divergence at 50 MHz; 13 revert
failure; 14 recovery-read failure or block-0 content mismatch.

The report carries an `R` line (byte 185, `CLOCK_CONTROL`, EXT_CSD sum), an
`S` line (invocation rc, switch result, CMD6 R1, byte-185 readback, clock,
RCG config, EXT_CSD sum), a `T` line (content rc, attempt marker, `same=`,
`div=`, block sums), a `V` line (revert rc/result, byte 185, clock, RCG,
recovery rc/sum), the 21 HS content sums, before/after pad/DLL samples and
the terminal FAIL record. The `report-width-b2` fixture fills every field
at its bound and stays inside the 2048-byte panic buffer.

Host checks: core 143 scenarios and 86 mutation controls (was 138/83),
adding HS enter/leave, an ignored byte-185 readback, CMD6 SWITCH_ERROR and
a busy-RCG case plus three mutation controls; probe 85 scenarios and 51
mutation controls (was 75/46), adding the eight stage-8 scenarios (pass,
ignored switch, busy RCG, content divergence, content read error, failed
revert, failed recovery read and failed pre-read) and four stage-8
mutations, plus the TOUTVAL-only arm-artifact case and its mutation; the
`report-width-b2` materialization is the sixth width fixture. The diagnostic
wrapper and payload gate accept `emmc-probe-8`. The stage-7 arming gate now
compares only the TOUTVAL bits, so the B1C-2 `INT_STABLE` artifact cannot
block a later DTO report.

No build, flash or boot had been performed when this host batch was
written. The build, BOOT-only flash and four-boot device run are recorded in
§5.3.20: F4 is measured PASS at 1-bit/50 MHz `[C]`, and F3 remains `[U]`.

### 5.3.20 Stage-B2 hardware run: HS timing holds at 50 MHz (2026-11-03)

The stage-8 image (`gta4l-emmc-probe=8`) was built from `9a84215` plus the
packager-gate fix `9d6fd1f`: kernel `fc3d5ee3…`, payload `9d17d0a1…`
(contract 7/7) and boot image `e5bc5024…` (gate 18/18 against the
`ce4d76e8…` v5 baseline). The write was BOOT-only: pre-flash p69 was
`13f88342…` (the B1C-2 post-flash hash, backed up and verified), post-flash
p69 is `2cd82e0f…`, the image prefix read back as `e5bc5024…`, the unchanged
tail is `a28b4ea2…`, and `RECOVERY` `14e11370…`, EFS `51fa824c…` and
sec_efs `3a265d58…` were unchanged. The pre-flash battery was 100%, the
post-batch read was 98%, and the pulled gates pass 18/18 and 7/7. The flash
record is `out/gta4l/ram-root-emmc-probe-b2-evidence/flash-record.json`; the
build provenance is `build-provenance.json` in the same directory.

The owner booted the same image four times without reflashing (boot 1 cold,
boots 2–4 warm) and re-read BOOT read-only after the batch:

    $ adb shell sha256sum /dev/block/mmcblk0p69
    2cd82e0f5a5734eb35d015c24629093afa6c9c3d47ef9f111fe911b98d4f46d7  /dev/block/mmcblk0p69

The read matches the post-flash hash, so all four boots are bound to the
flashed bytes. Boots 2 and 4 reached B2 and produced field-for-field
identical reports apart from the stack counters, with boot 4's
owner-supplied PAD `post` field carrying an extra leading zero (an OCR
artifact of `00009fe4`). Boots 1 and 3 stopped pre-B2 at the H8
CMD3-completion timeout
(`stage=8 rc=-7 fail=1/3/65536/0000031a/0000/0000/03f800f0/9`, `step=9`,
`log=5/8/3/0`); they are the known flake class and not B2 verdicts.

The two B2 reports carry:

    mci=10000072 rca=1 cap=61071360 rev=8 hci=00 step=23 log=58
    R clk=0007 hs=00 es=000009cb
    S rc=0 sw=0 c6=00000900 hs=01 clk=0007 rcg=0000020b sum=000009cc
    T rc=0 att=1 same=1 div=0 b0=62084b66 b1=84d0543e data=f418cd33 last=a89b26d5
    V rc=0 sw=0 hs=00 clk=0007 rcg=00002017 q=0 qb0=62084b66
    BLOCKS 2=793877e4 3=01fcf835 4=393b7c13 5=0bfbefd5 6=57ac4bf5
      7=82d4c9c9 8=2e16c705 9=adbc9085 10=2d26b02c 11=69ab5982
      12=604856e0 13=7f3f196c 14=7e056798 15=d6926617 16=3d035910
      17=5076cbb8 18=d40dca26 19=37f4be4f 20=057d3881 21=89457318
      22=00000000
    PAD pre=00009fe4/6000642c/00000000 post=00009fe4/6000642c/00000000
    FAIL fail=0/0/0/00000000/0000/0000/00000000/0 @Gta41EmmcProbe.c:1286

`R` records byte 185 = 0 and the EXT_CSD byte sum `0x9cb` at 400 kHz. `S`
records a clean CMD6 (`sw=0`, R1 `00000900` = TRAN + READY_FOR_DATA), byte
185 reading 1, and the GCC SDCC1 APPS RCG at `0x20b` — the pinned 50 MHz
floor row (source select 2, divider 11, M/N/D zero) for the 52 MHz class.
The post-switch EXT_CSD sum `0x9cc` is exactly the baseline plus one for
the changed byte. `T` reports `att=1`, `same=1`, `div=0` and the stage-A
sums at 50 MHz: b0 `62084b66`, b1 `84d0543e`, DATA 34 `f418cd33`, LSTLBA
`a89b26d5` and all 21 GPT-array BLOCKS sums, identical to the 400 kHz
baseline. `V` drops the clock first, clears byte 185, reads back `hs=00`
with RCG `0x2017` (400 kHz) and re-reads block 0 at 400 kHz (`q=0`,
`qb0=62084b66`). The pad/DLL samples are identical before and after and the
terminal FAIL record is all zero.

Verdict: the decision table's S-pass + T-pass row is taken. F4 is measured
PASS at 1-bit/50 MHz: the card accepted `HS_TIMING=1`, the controller
programmed and read back the 50 MHz floor row, the full stage-A content
pass matched at that clock, and the revert to legacy timing plus the
recovery read were clean. The two matching reports satisfy the
two-matching rule. This is content-level evidence only: it is not a width,
write, DMA, CQE, ICE, HS200 or frequency-measurement result. The cold boot
did not reach B2, so the cold-start path is not part of the F4 evidence;
both passes are warm boots that followed a pre-B2 stop, and each report's
`V` line returns the card to legacy timing, so no retained HS state is
implied. The H8 class remains an independent pre-B2 flake.

F4 moves from `[U]` to `[C]` at the stated 1-bit/50 MHz content scope. F3
remains `[U]`; `trc=-20` remains unobserved and the byte-183 readback
paradox is unchanged.

### 5.3.21 Stage-H9 CMD3-completion retry discriminator (gta4l-emmc-probe=9, host batch 2026-11-03)

The `gta4l-emmc-probe=9` stage runs the standard read-only identification
sequence exactly once, preserving the pinned 8-us post-ready CMD1 delay, and
adds one designed hook: if CMD3 ends in the H8 completion timeout
(`GTA4L_EMMC_ERR_CMD_TIMEOUT`), the core snapshots attempt 1 and re-issues
exactly one CMD3 with the identical argument (`0x00010000`) and timing path.
There is never a third attempt, never a silent retry and no fall-through.
The stage stops after the TRAN wait, before any EXT_CSD read and before the
stage-A pass; it issues no CMD6, no width switch and no user-data access.
The only new card-facing action relative to the identification sequence is
the duplicate CMD3.

The portable core keeps the hook opt-in through `cmd3_retry_enabled`; the
probe sets it only for stage 9. A second opt-in, `identify_only`, stops
`gta4l_emmc_init()` after `wait_tran()` so the rung touches no EXT_CSD data.
Each attempt is folded into a two-slot capture (result, R1 error flag, the
live fail record, the pre-CMD3 sample, the timeout sample and the poll
history), and the live failure state is cleared before the duplicate so the
attempt-1 record cannot be overwritten.

The stage always emits `GTA4L-EMMC H9 REPORT`: a header with `rc`, `stop`,
the identification fields, `retry=engaged/ok`, `att`, `step` and `log`; one
`A1`/`A2` attempt line each with `n`, `rc`, `r1`, the fail record and the
response words; `A1P`/`A2P` with the pre-command `c3p` and timeout `c3t`
samples; `A1L`/`A2L` with the CMD3 poll history `c3l`; a `C` line with the
CID and CMD1 attempts/timeouts; and a terminal `H9 FAIL` line. A slot that
did not run prints `n=0` and zero fields.

Stop codes: 0 clean identification with no retry; 10 retry engaged, the
duplicate clean and identification clean; 11 duplicate CMD3 transport
failure; 12 duplicate clean but identification failed later; 13 duplicate
R1 status error; 14 no retry engaged and identification did not complete
cleanly (the first CMD3 either failed at a non-timeout code or never ran);
15 attempt-1 clean but identification failed later. The report does not add
a new MMIO sample: `regs` is the caller's existing clock-guarded capture.

Host checks: core 148 scenarios and 90 mutation controls (was 143/86),
adding `cmd3-retry-clean`, `cmd3-retry-pass`, `cmd3-retry-fail`,
`cmd3-retry-status` and `cmd3-retry-later-fail` plus four mutations (silent
retry, retry without a first timeout, retry with a changed argument and an
unbounded retry loop). The probe suite is 91 scenarios and 51 mutation
controls (was 85/51), adding `probe-h9-clean-no-fire`, `probe-h9-fire-pass`,
`probe-h9-fire-fail`, `probe-h9-fire-second-ident`, `probe-h9-mismatch` and
the `report-width-h9` width materialization (the seventh). The diagnostic
wrapper and payload gate accept `emmc-probe-9`.

The run procedure required a fresh explicit BOOT-only approval that names
the duplicate CMD3; the as-run result is in §5.3.22. F3 remains `[U]`.

### 5.3.22 Stage-H9 hardware run: the duplicate CMD3 also times out (2026-11-03)

The stage-9 image was built from clean `a952a09` (kernel `75e529a3…`,
payload `e1b474f7…` with a 7/7 contract, boot image `92d9471c…` with an
18/18 gate against the `ce4d76e8…` v5 baseline; the probe archive
`7022e326…` matches the committed sources), flashed BOOT only (pre p69
`2cd82e0f…`, post p69 `25385fd7…`, image prefix `92d9471c…`, unchanged
tail `a28b4ea2…`, RECOVERY/EFS/sec_efs unchanged, pulled gates 18/18 and
7/7) and booted four times without reflashing (boot 1 cold, boots 2–4
warm). The post-batch p69 re-read matched `25385fd7…`, binding all four
boots. Every boot reached stage 9 and stopped before any EXT_CSD read
(`cap=0 rev=0 bw=0 ext=00000000`).

Boots 1/3/4 produced field-identical `rc=-7 stop=11 retry=1/0 att=2`
reports: attempt 1 reproduced the historical H8 tuple
(`fail=1/3/65536/0000031a/0000/0000/03f800f0/9`) and exhausted the full
100,000-iteration poll budget (≈1.14 s at 19.2 MHz) with the normal/error
samples zero at every step and a zero fresh post-timeout sample — the
wait is completion-silent, and the H8-era "65536 = latched CTO bit"
reading is corrected: `65536` is the FAIL-record argument (`0x00010000`).
The duplicate CMD3 (`0x00010000`, same timing path, never a third
attempt) also timed out, exiting through the latched-error branch with
`fail=1/3/65536/0000031a/0001/8000/03f800f1/9` (ERROR `0x0001` command
timeout, NORMAL `0x8000` error summary, present bit 0). Boot 2 was the
clean control: `rc=0 stop=0 retry=0/0 att=1 step=13`, a clean CMD3
completing within 26 poll iterations (≈0.29 ms), `rca=1`, and an empty
attempt-2 slot.

Verdict: the sticky row of the H9 decision table is taken with three
field-identical samples (the pair (1, 3) already satisfies the
two-matching rule); the majority row was exercised once, and the
"attempt-1 timeout, attempt-2 clean" row never occurred. The immediate
duplicate CMD3 is not a recovery mechanism (0/3), so no per-boot retry
policy is adopted; the flake remains per-boot random (cold boot 1 timed
out, warm boot 2 was clean, warm boots 3/4 timed out; 3/4 this batch).
The `A2L` poll zeros are the latched-error exit's expected shape because
that exit never validates the poll capture. F3 stays `[U]`; no width,
write, DMA, frequency or EXT_CSD work was done.

### 5.3.23 Stage-H10 width-at-HS continuation (gta4l-emmc-probe=10, host batch 2026-11-03)

The `gta4l-emmc-probe=10` stage runs the standard read-only identification
sequence once (no retry hook), the unchanged stage-A content pass at
400 kHz/1-bit as the baseline, then HS entry (byte 185 = 1, RCG `0x20B`,
readbacks), a 4-bit BUS_WIDTH switch at HS (byte 183 = 1, CMD13 TRAN wait,
`HOST_CONTROL` write/readback and the EXT_CSD re-read at the attempted
width), one CMD19/CMD14 round trip at 4-bit, the full stage-A content pass
at 4-bit/HS, the width revert (byte 183 = 0), the HS revert (clock to
400 kHz first, then byte 185 = 0) and a block-0 recovery read. It always
emits `GTA4L-EMMC H10 REPORT` and stops; there is no 8-bit step, no
on-device retry and no fall-through, and the only card-state writes are
bytes 185 and 183 (the only data write is the 4-byte CMD19 pattern).

The stage reuses the opt-in core calls (`gta4l_emmc_set_hs_timing()`,
`gta4l_emmc_set_bus_width()`, `gta4l_emmc_bus_test()`) and the shared
content/compare helpers; the stage parser accepts 1..10. The report carries
a header (`rc`, `stop`, the identification fields, `hci`, `step`, `log`,
`bw`), an `R` identity line (byte 185, byte 183, `CLOCK_CONTROL`,
`HOST_CONTROL` and the EXT_CSD sum at 400 kHz), an `S` line (HS entry), a
`W` line (width switch: `sw`, `swrc`, `c6`, `hb`/`ha`, `es`), a `B` line
(bus test: `tt`, `trc`, `tw`, `te`), a `T` line (`same`, `div`, the content
sums), a `V` line (width revert), an `X` line (HS revert), a `Q` line
(recovery read), the shared `BLOCKS`/`PAD` lines and the terminal `FAIL`
record.

Stop codes: 0 pass; 10 byte-185 readback mismatch or HS switch error; 11 HS
clock failure; 12 width switch failure (CMD6/R1 error, empty readback or
byte-183 readback mismatch); 13 bus-test failure at 4-bit/HS (a CMD19
write-phase wedge leaves the controller inhibited, so the reverts are
recorded as not-run and the rung stops immediately, like the B1C stop);
14 content read failure or first divergence at 4-bit/HS; 15 width-revert
failure; 16 HS-revert failure; 17 recovery-read failure or sum mismatch.

Host checks: the probe suite is 102 scenarios and 56 mutation controls (was
91/51), adding `probe-h10-pass`, `probe-h10-hs-ignored`,
`probe-h10-clock-busy`, `probe-h10-width-ignored`,
`probe-h10-bustest-timeout`, `probe-h10-bustest-mismatch`,
`probe-h10-content-div`, `probe-h10-content-read`,
`probe-h10-width-revert-fail`, `probe-h10-hs-revert-fail` and
`probe-h10-q-fail` plus five mutations (a skipped width step, a wrong stop
code, a silent width revert, a skipped content pass and a bus-test failure
that continues into the inhibited controller). The core suite is unchanged
at 148/90. The `report-width-h10` fixture is the eighth width
materialization, and the diagnostic wrapper and payload gate accept
`emmc-probe-10`.

The device run followed in the same session; the as-run result is in
§5.3.24.

### 5.3.24 Stage-H10 hardware run: the width is refused at HS too (2026-11-03)

The stage-10 image was built from the `912e257` host batch with the
payload-gate fix (`076a548`) in the working tree (kernel `87be83e6…`,
payload `b046acf5…` with a 7/7 contract, boot image `26bab7b2…` with an
18/18 gate against the `ce4d76e8…` v5 baseline; the probe archive
`906188e0…` matches the committed sources), flashed BOOT only (pre p69
`25385fd7…`, post p69 `38c74815…`, image prefix `26bab7b2…`, unchanged
tail `a28b4ea2…`, RECOVERY/EFS/sec_efs unchanged, pulled gates 18/18 and
7/7) and booted fifteen times without reflashing: the 12-boot batch
(boot 1 cold, boot 2 warm and the only clean identification, boots 3–5
warm), a cold boot 6 after an accidental power-off, then boots 7–12 and
the owner-approved extension boots 13–15. The post-batch p69 re-read
matched `38c74815…`, binding every boot.

Boot 2 was the only H10 verdict: `rc=-19 stop=12`. HS entry was clean
(`S rc=0 sw=0 c6=00000900 hs=01 clk=0007 rcg=0000020b sum=000009cc`
against the `R es=000009cb` baseline), then the 4-bit switch at HS
returned the legacy tuple — clean CMD6 R1 (`c6=00000900`), the host width
set and read back (`hb=00` → `ha=02`), the byte-183 readback still 0
(`sw=0`, `swrc=-19`), and the EXT_CSD re-read equal to the 1-bit-at-HS
sum (`es=000009cc`, baseline+1, not a 4-bit sum). The width revert V and
the HS revert X were clean (1-bit at HS, then byte 185 = 0, legacy RCG
`0x2017`, sum back to `0x9cb`); `B`/`T`/`Q` carry the zero-initialized
not-run fields because the designed stop precedes them, and `log=33`
(7 identification commands + 1 pre-rung read + 25 stage-A CMD17s)
matches the committed failure path and the simulator's
`probe-h10-width-ignored` fingerprint field-for-field.

The other fourteen boots stopped pre-HS with the H8-class CMD3
completion-silent stop (`PROBE FAILED stage=10 rc=-7`, `step=9`,
`fail=1/3/65536/0000031a/0000/0000/03f800f0/9`, the CMD2-tail response
window `00000500,5836384d,42009005,f4661978`, and the full
100,000-iteration ≈1.13–1.14 s poll window; the `c31` ticks differ per
boot). They are facts only, not H10 verdicts: a completion-timeout boot
is re-run, never retried (the H9 verdict). Both cold boots (1 and the
accidental boot 6) flaked, so unlike the B2 batch's warm-only pairing the
cold path is covered; this batch's flake ran 14/15 (vs 3/4 in H9), facts
that do not measure a rate.

Verdict: the §3.2.3 decision-table row "W fails with the legacy tuple" is
taken — the card ignores the BUS_WIDTH write at HS exactly as at
400 kHz, so the timing-class model for the legacy refusal is excluded.
The byte-185 writes are honored in the same CMD6 shape (content `0x9cb`
↔ `0x9cc`, readbacks 1/0), so the write mechanism works and the refusal
is targeted to the byte-183 field. The verdict carried the single-sample
caveat (`N=1`: the two-matching rule was not met and the 15-boot cap was
reached; the verdict sample's fields match the committed fingerprint).
The H11 W4 replay (§5.3.25) produced the matching second sample, so that
caveat is closed: the byte-183 refusal is sticky and covers both nonzero
SDR values tried. The CMD19-at-4-bit/HS question stays unmeasured because
the designed stop precedes the bus test. F3 stays `[U]`; no 8-bit step,
user-data write, CMD18 or CMD25 was performed.

### 5.3.25 Stage-H11 width-value continuation (gta4l-emmc-probe=11, host batch and hardware run 2026-11-03)

The `gta4l-emmc-probe=11` stage runs the standard read-only identification
sequence once (no retry hook), the unchanged stage-A content pass at
400 kHz/1-bit as the baseline, then HS entry exactly as H10 (byte 185 = 1,
RCG `0x20B`, readbacks), the record-only W4 replay of the H10 4-bit switch
at HS (byte 183 = 1, CMD13 TRAN wait, `HOST_CONTROL` write/readback and
the EXT_CSD re-read at the attempted width), the W8 8-bit attempt at HS
(byte 183 = 2, still SDR; no bit-4/DDR variant), one CMD19/CMD14 round
trip and the full stage-A content pass at 8-bit/HS only when W8 is
honored, the width revert (byte 183 = 0), the HS revert (clock to 400 kHz
first, then byte 185 = 0) and a block-0 recovery read. It always emits
`GTA4L-EMMC H11 REPORT` and stops; there is no on-device retry and no
fall-through, and the only card-state writes are bytes 185 and 183 (the
only data write is the 8-byte CMD19 pattern at the honored 8-bit width).
No CMD18/CMD25, no user-data write and no DDR byte-183 value is issued.

The stage reuses the opt-in core calls (`gta4l_emmc_set_hs_timing()`,
`gta4l_emmc_set_bus_width()`, `gta4l_emmc_bus_test()`) and the shared
content/compare helpers; the stage parser accepts 1..11. The report
carries a header (`rc`, `stop`, the identification fields, `hci`, `step`,
`log`, `bw`), an `R` identity line, an `S` line (HS entry), a `W4` line
(record-only 4-bit replay), a `W8` line (8-bit attempt), a `B` line (bus
test), a `T` line (content), a `V` line (width revert), an `X` line (HS
revert), a `Q` line (recovery read), the shared `BLOCKS`/`PAD` lines and
the terminal `FAIL` record.

W4 is record-only: a clean-shape tuple match closes the H10 `N=1` caveat
and a clean-shape mismatch re-opens H10 as per-boot random, but neither
stops the rung; only a true anomaly (a CMD6/R1 error, an empty readback
`es=00000000` or a transport rc) stops before W8. A W8 readback that
disagrees with its own re-read sum is the byte-183 paradox row and stops
after the reverts with no `B`/`T` on the unproven framing.

Stop codes: 0 pass; 10 byte-185 readback mismatch or HS switch error; 11
HS clock failure; 12 W8 switch failure (CMD6/R1 error, empty readback or
byte-183 readback mismatch); 13 bus-test failure at 8-bit/HS (a CMD19
write-phase wedge leaves the controller inhibited, so the reverts are
recorded as not-run and the rung stops immediately); 14 content read
failure or first divergence at 8-bit/HS; 15 width-revert failure; 16
HS-revert failure; 17 recovery-read failure or sum mismatch; 18 W4
anomaly (stops before W8).

Host checks: the probe suite is 116 scenarios and 64 mutation controls
(was 102/56), adding `probe-h11-honored-pass`, `probe-h11-w4-replay`,
`probe-h11-w4-mismatch`, `probe-h11-w8-div`, `probe-h11-w8-err`,
`probe-h11-w4-anomaly`, `probe-h11-clock-busy`,
`probe-h11-bustest-timeout`, `probe-h11-bustest-mismatch`,
`probe-h11-content-read`, `probe-h11-content-div`,
`probe-h11-width-revert-fail`, `probe-h11-hs-revert-fail` and
`probe-h11-q-fail` plus eight mutations (a skipped W4 step, a skipped W8
step, a bus test at the wrong width, a silent width revert, a wrong W8
stop code, a skipped content pass, a host width not restored and a
must-refuse byte-183 DDR scope control). The core suite is unchanged at
148/90. The `report-width-h11` fixture is the ninth width materialization,
and the diagnostic wrapper and payload gate accept `emmc-probe-11`.

No build, flash or boot had been performed when the host batch landed.
The device run followed on 2026-11-03. The stage-11 image was built from
the clean `6b0b8e5` base (kernel `ac69000a…`, payload `e78a7f9e…` with a
7/7 contract, boot image `3dc561cd…` with an 18/18 gate against the
`ce4d76e8…` v5 baseline; the probe archive `18c3532e…` matches the
committed sources), flashed BOOT only (pre p69 `38c74815…`, post p69
`2f6deba3…`, image prefix `3dc561cd…`, unchanged tail `a28b4ea2…`,
RECOVERY/EFS/sec_efs unchanged, battery 100%) and booted five times
without reflashing by the owner. The per-boot cold/warm method and the
reboot methods were not captured. The post-boot p69 re-read matched
`2f6deba3…` (battery 100% Full), binding every boot to the flashed
bytes.

Boots 1–4 stopped pre-identification with the H8 CMD3 completion-silent
class (`PROBE FAILED stage=11 rc=-7`, `step=9`,
`fail=1/3/65536/0000031a/0000/0000/03f800f0/9`, the CMD2-tail response
window `00000500,5836384d,42009005,f4661978`, the full 100,000-iteration
≈1.14 s poll window; only the `c31` ticks and the stack pages/free
differ). They are facts only, not H11 verdicts: a completion-timeout boot
is re-run, never retried (the H9 verdict).

Boot 5 was the only H11 verdict: `rc=-19 stop=12`, matching the
committed `probe-h11-w4-replay` simulator fingerprint field-for-field.
HS entry was clean (`S rc=0 sw=0 c6=00000900 hs=01 clk=0007 rcg=0000020b
sum=000009cc` against the `R es=000009cb` baseline). W4 replayed the
H10 4-bit switch at HS and returned the legacy tuple field-for-field
(`rc=-19 sw=0 swrc=-19 c6=00000900 hb=00 ha=02 es=000009cc`), the
matching second sample that closes the H10 `N=1` caveat. W8 then
attempted byte 183 = 2 at HS: the host applied and read back the 8-bit
code (`ha=20`) but the card again returned the legacy tuple (`sw=0`,
`swrc=-19`, clean `c6=00000900`, `es=000009cc`), so the 8-bit value is
refused exactly as value 1 was. The width revert V and the HS revert X
were clean (host back at 1-bit, `hs=00`, legacy RCG `0x2017`, sum
`0x9cb`), `PAD` pre equals post, and `B`/`T`/`Q` carry the
zero-initialized not-run records because the designed stop precedes the
bus test, content pass and recovery read (`log=33` = 7 identification
commands + 1 pre-rung read + 25 stage-A CMD17s, the same failure-path
count class as H10).

Verdict: the §3.2.4 decision-table row 1 is closed. The byte-183 refusal
is sticky per boot, timing-class-independent (400 kHz and HS) and covers
both nonzero SDR values tried (1 and 2), so the card refuses the whole
SDR field rather than only the 4-bit value. F3 stays `[U]`; the open
owner questions are the DDR bit-plane (byte-183 bit 4) and the CMD19
write-phase wedge, with no new rung scheduled. No CMD18/CMD25,
user-data write, DDR value, HS200 or second 8-bit transition was
performed. Evidence:
`out/gta4l/ram-root-emmc-probe-h11-evidence/` (`flash-record.json`,
`device-run-record.json`, `device-run-transcript.txt`,
`postflash-hashes.txt`, `postboot-reread.txt`).

### 5.3.26 Stage-H12 record-only DDR attempt (gta4l-emmc-probe=12, host batch and hardware run 2026-11-03)

The `gta4l-emmc-probe=12` stage runs the standard read-only identification
sequence once (no retry hook), the unchanged stage-A content pass at
400 kHz/1-bit as the baseline, then HS entry exactly as H10/H11 (byte 185 = 1,
RCG `0x20B`, readbacks), one record-only CMD6 SET of EXT_CSD byte 183 to 4
(the JEDEC 4-bit DDR code, bit 2 set over the SDR 4-bit value 1), its CMD13
TRAN wait, the matching 4-bit SDR `HOST_CONTROL` DAT width for the
verification read, the byte-183 readback and a fresh EXT_CSD re-read with its
byte sum, the width revert (byte 183 = 0 at HS), the HS revert (clock to
400 kHz first, then byte 185 = 0) and a block-0 recovery read. It always
emits `GTA4L-EMMC H12 REPORT` and stops; there is no on-device retry and no
fall-through. The controller DDR configuration (`CORE_DDR_CONFIG` and the
`HOST_CONTROL2` MCLK_SEL/HC_SELECT_IN fields) is never touched, and no
CMD19/CMD14, CMD18/CMD25 or user-data write is issued. The only card-state
writes are bytes 185 and 183, and the only byte-183 values issued are the
record-only 4 and its revert 0, so there is no 8-bit DDR value 5, HS200 or
HS400 step.

The stage reuses `gta4l_emmc_set_hs_timing()`, `gta4l_emmc_set_bus_width()`
and the shared content/sum helpers, and adds
`gta4l_emmc_set_bus_width_ddr()` in the portable core: a raw-value wrapper
around the existing switch path that passes the 4-bit DDR code 4 while
applying only the 4-bit SDR host DAT width for the verification read.
`gta4l_emmc_set_bus_width()` keeps accepting only 1/4/8 bits, and the DDR
helper is used only by stage 12. The stage parser accepts 1..12. The report
carries a header (`rc`, `stop`, the identification fields, `hci`, `step`,
`log`, `bw`), an `R` identity line, an `S` line (HS entry), a `WDDR` line
(the record-only DDR attempt and its verification read), a `VR` line (width
revert), an `X` line (HS revert), a `Q` line (recovery read), the shared
`PAD` line and the terminal `FAIL` record.

The outcome classes are decided from the WDDR tuple and the pre-rung sum:
a clean refusal is `wddr_rc = -19` (`GTA4L_EMMC_ERR_BUS_WIDTH`), byte-183
readback 0 and the re-read sum equal to the 1-bit-at-HS sum (the pre-rung sum
plus the byte-185 change), and stops with 0; an honored row is `wddr_rc = 0`,
byte-183 readback 4 and the re-read sum equal to the 1-bit-at-HS sum plus 4,
and stops with 13 before any controller DDR work; any other shape (a
transport/R1 error, an empty read or a readback that disagrees with its own
sum) is the DDR-verification failure and stops with 12 after both reverts.

Stop codes: 0 clean refusal; 10 pre-rung EXT_CSD read failure or HS switch
failure; 11 HS clock failure; 12 DDR-verification failure; 13 honored; 14
width-revert failure; 15 HS-revert failure, recovery-read failure or block-0
content mismatch. A refused or honored row always runs both reverts and the
recovery read; a stop-12 row reverts both before reporting.

Host checks: the probe suite is 128 scenarios and 69 mutation controls
(was 116/64), adding `probe-h12-refused-pass`, `probe-h12-honored`,
`probe-h12-wddr-err`, `probe-h12-wddr-paradox`, `probe-h12-wddr-empty`,
`probe-h12-hs-ignored`, `probe-h12-clock-busy`,
`probe-h12-width-revert-fail`, `probe-h12-hs-revert-fail`,
`probe-h12-q-fail` and `probe-h12-pre-read-fail`, plus five mutations (a
skipped WDDR step, a wrong stop code, a silent width revert, a skipped
recovery read and a wrong DDR code that issues value 5 instead of 4). The
existing `h11-width-ddr` core scope control now rejects any byte-183 DDR
value from stages 1..11, so the stage-12 raw-value helper is the only path
that can issue value 4. The core suite is unchanged at 148/90. The
`report-width-h12` fixture is the tenth width materialization, and the
diagnostic wrapper and payload gate accept `emmc-probe-12`.

No build, flash or boot had been performed when the host batch landed. The
device run followed on 2026-11-03. The stage-12 image was built from the
clean `dc1a85a` base (kernel `75a4661a…`, payload `27d8228f…` with a 7/7
contract, boot image `e5744884…` with an 18/18 gate against the
`ce4d76e8…` v5 baseline; the probe archive `2c63e623…` matches the
committed sources), flashed BOOT only (pre p69 `2f6deba3…`, post p69
`0f7ec22c…`, image prefix `e5744884…`, unchanged tail `a28b4ea2…`,
RECOVERY/EFS/sec_efs unchanged) and booted five times without reflashing
by the owner. The per-boot cold/warm method was not captured. The
post-boot p69 re-read matched `0f7ec22c…` (battery 97%), binding every
boot to the flashed bytes.

Boots 1, 2 and 4 stopped pre-identification with the H8 CMD3
completion-silent class (`PROBE FAILED stage=12 rc=-7`, `step=9`,
`fail=1/3/65536/0000031a/0000/0000/03f800f0/9`, the full
100,000-iteration ≈1.14 s poll window): facts only, re-run not retried.
Boot 3 stopped during identification at the block-1 CMD17 with `rc=-11`
(`GTA4L_EMMC_ERR_DATA_TIMEOUT`), `step=15` (`STEP_READY`) and
`fail=1/17/1/0000113a/0000/0002/03f800f0/15` (TC only, no error bit), the
known TC-only/no-error CMD17 class: facts only.

Boot 5 was the only H12 verdict: `rc=-15 stop=12`. HS entry was clean
(`S rc=0 sw=0 c6=00000900 hs=01 clk=0007 rcg=0000020b sum=000009cc`
against `R es=000009cb`). The byte-183 = 4 CMD6 answered clean
(`c6=00000900`, the legacy tuple; not the `wddr-err` R1-error shape), so
the card accepted the command, but the verification EXT_CSD re-read at
the 4-bit SDR DAT width failed with `GTA4L_EMMC_ERR_CARD_STATUS`
(`swrc=-15`). The helper restored the previous host width, so `sw=0` and
`es=00000000` are the unset readback fields and no byte-183 = 4 readback
was obtained. The VR width revert and X HS revert were clean (1-bit,
`hs=00`, legacy RCG `0x2017`, sum `0x9cb`), the Q recovery read returned
block 0, `PAD` pre equals post, and `log=33` matches the H10/H11
failure-path count class.

The tuple is not enumerated by any committed H12 scenario: the clean
refusal expects `swrc=-19`/`es=0x9cc` and the honored row expects
`sw=4`/`es=0x9cd`. With the only written value differing (4 versus the
refused 1 and 2) and the host configuration otherwise identical, the
post-switch re-read changed from a clean EXT_CSD read to a card-status
error, coherent with the card toggling into 4-bit DDR for data. The
byte-183 paradox row is measured but unverified, the whole-field-refusal
row is not closed, and F3 remains `[U]`. The open owner questions remain
the DDR bit-plane and the CMD19 write-phase wedge; the next step is the
host-only write-phase wedge audit. Evidence:
`out/gta4l/ram-root-emmc-probe-h12-evidence/` (`flash-record.json`,
`device-run-record.json`, `device-run-transcript.txt`,
`postflash-hashes.txt`, `postboot-reread.txt`).

### 5.3.27 Write-phase wedge audit: the CMD19 write has no completion token (host-only, 2026-11-03)

The host-only audit is `docs/emmc-write-wedge-audit.md`. It decodes the
recorded CMD19 write sequence against the pinned `sdhci.c` PIO/transfer-mode
path, the `sdhci-msm` timeout formula, JEDEC B1.51 §6.6.4/Table 7 and
§A.6.3/Table A.223, and the Linux bus-test handling. No build, flash, boot or
device access was performed, and no code changed.

The two-phase finding: the command phase completes (`CC` latches, the R1
response is read), the controller asks for the block (`BufferWriteReady`
latches, the 4-byte or 8-byte PIO block is written), and the data phase never
produces `TC`. The terminal sample is `error=0x0000`,
`normal=0x0011` (`CC | BWR`), present `0x03f801f6` (`CMD_INHIBIT=0`,
`DAT_INHIBIT=1`, `DAT_LINE_ACTIVE=1`), at both widths, with no CMD6 in the
1-bit case. The write path's required completion signal is not the one CMD19
produces: JEDEC defines a host-to-device pattern (the device ignores all but
the first two bits of each line and the optional CRC16), not a normal data
write, and the TI SPRZ436H i836 erratum records controllers waiting for a CRC
status token that the card does not send. Linux ignores `BUS_TEST_W` errors and
lets `BUS_TEST_R` decide (`mmc_ops-pinned.c:847-866`), and excludes
`MMC_BUS_TEST_R` from the data-CRC path (`sdhci.c:3471-3477`). That is the
leading cause: no CRC status token, so the controller stays data-active and
`TC` never comes.

The window arithmetic is consistent with the silence. Every observed write
wedge used the universal 1 s poll window; the 8 s E2 repeat never ran because
the stage-7 E1 recovery read failed with `ERR_INHIBIT` (`Q rc=-17`). The port
arms `TIMEOUT_CONTROL = 0x0E` (`Gta4lEmmc.c:1502`); the MSM formula
(`sdhci-msm.c:2213-2227`) makes that about 22 minutes at 400 kHz and about
10.7 s at 50 MHz, so a write waiting for a CRC status has no reason to raise
`DTO` inside 1 s or 8 s. The stage-7 E1 forced `TOUTVAL=0` row saw no `DTO` in
a 5 s read drain, but its `mb=7` field cannot distinguish a pending eighth
block from a completed transfer whose `TC` never latched; the write-wait state
itself has never been tested with `TOUTVAL=0`.

The audit also records a host recovery gap: the core has no per-request data
reset (only `RESET_ALL` at init, `Gta4lEmmc.c:1480`), so the failed write
leaves `DAT_INHIBIT` set and the next command fails the inhibit wait. Linux
resets the data circuit on a data error (`sdhci.c:1566-1600`). The gap is not
the cause of the first wedge, but it is a confound and a prerequisite for the
next rung.

The next rung menu is in the audit §6. The recommended rung is a data-circuit
reset followed by CMD14: treat the CMD19 write phase as Linux does, reset the
data circuit, then let the read pattern comparison decide. The second rung is
the forced-`TOUTVAL=0` 1-bit write decoupled from the recovery read, which
distinguishes a working-but-long counter from a counter that is silent in the
write-wait state. Both are owner-gated BOOT-only runs; neither is authorized
by this record, and no write-side goal (CMD19 at width, B1C continuation,
F5/F6) is claimed. Evidence rows remain
`out/gta4l/ram-root-emmc-probe-b1c-evidence/`,
`out/gta4l/ram-root-emmc-probe-b1c1-evidence/` and
`out/gta4l/ram-root-emmc-probe-dto-evidence/`.

### 5.3.28 Stage-H13 rung-1 write-phase recovery (gta4l-emmc-probe=13, host batch 2026-11-03)

The `gta4l-emmc-probe=13` stage is the first rung of the write-phase wedge
ladder recommended by the host-only audit. It runs the standard read-only
identification once (no CMD3 retry), the unchanged stage-A content pass at
400 kHz/1-bit as the baseline, then one record-only 1-bit CMD19 bus-test
write (the 4-byte pattern block, first byte `0x5A`, compared as `0xA5`),
one bounded SDHCI data-circuit reset (`SOFTWARE_RESET` ← `0x04`, the data
bit, with the self-clear poll using the 100,000 us reset bound and the
10 us poll step), the post-reset `PRESENT_STATE`/`DAT_INHIBIT` sample, one
CMD14 read with the existing pattern comparison, and the block-0 recovery
read. It always emits `GTA4L-EMMC H13 REPORT` and stops; there is no
on-device retry and no fall-through. The only card-facing write is the
non-persistent CMD19 pattern; no CMD6, CMD18, CMD25 or user-data write is
issued, and no card width or timing is changed. The stage parser
accepts 1..13.

The reset is local to `gta4l_emmc_bus_test_reset()`; no other timeout path
changes. The helper accepts 1/4/8 bits, requires the host width and
`HOST_CONTROL` DAT bits to match, and records the terminal write sample,
the reset write/readback/poll count, the post-reset `DAT_INHIBIT`, and the
CMD14 result with its first pattern words in
`struct gta4l_emmc_bus_reset_result`. Stop codes: 0 clean wedge recovery
(the recorded `CC | BufferWriteReady`/no-`TC` write, reset clear, CMD14
pattern expected); 10 reset did not self-clear; 11 `DAT_INHIBIT` survived
the reset (CMD14 not issued); 12 the width/argument precondition refused
the helper; 13 CMD14 pattern mismatch; 14 CMD14 transport or card-status
failure; 15 block-0 recovery read failed or disagreed; 16 the write
completed normally and the rest passed; 17 the write failed with a
non-timeout error and the rest passed.

The report carries the header (`rc`, `stop`, `mci`, `rca`, `cap`, `rev`,
`hci`, `step`, `log`, `bw`), an `R` pre-rung line (`clk`, `hc`, `bw`,
`es`), a `W` write line (`wrc`, `err`, `nrm`, `pst`), a `RST` reset line
(`rc`, `wr`, `rb`, `it`, `pst`, `dinh`), a `T` CMD14 line (`trc`, `tw`,
`te`), a `Q` recovery line (`rc`, `qb0`), the shared `PAD` line and the
terminal `FAIL` record.

Host checks: the probe suite is 139 scenarios and 69 mutation controls
(was 128/69), adding `probe-h13-pass`, `probe-h13-reset-stuck`,
`probe-h13-inhibit`, `probe-h13-mismatch`, `probe-h13-read-timeout`,
`probe-h13-read-crc`, `probe-h13-read-status`, `probe-h13-write-crc`,
`probe-h13-normal` and `probe-h13-q-fail` plus the eleventh width
materialization `report-width-h13`. The core suite is 160 scenarios and
98 mutation controls (was 148/90), adding the reset-clear, stuck-reset,
surviving-inhibit, CMD14 mismatch/read-timeout/read-CRC/read-status,
non-wedged write and non-timeout write cases with mutation controls for
the reset write, the DAT_INHIBIT gate and the stop-code mapping. The
diagnostic wrapper and payload gate accept `emmc-probe-13`. No build,
flash or boot was performed when the host batch landed.

### 5.3.29 Stage-H13 hardware run: the bounded data reset does not recover the card (2026-11-03)

The stage-13 image was built from the clean `5c79aa9` base (kernel
`3e02d187…`, payload `1fb04a4d…` with a 7/7 contract, boot image
`bc8175e9…` with an 18/18 gate against the `ce4d76e8…` v5 baseline; the
probe archive `4ffb6d2c…` matches the committed sources), flashed BOOT
only (pre p69 `0f7ec22c…`, post p69 `18d2a83d…`, image prefix
`bc8175e9…`, unchanged tail `a28b4ea2…`; RECOVERY/EFS/sec_efs unchanged)
and booted repeatedly without reflashing by the owner. The exact boot
count was lost; the verdict arrived on an estimated ~18th boot. The first
three boots were not method-captured, the next eight were explicitly cold
(power-off, ~10 s, power on), and the remaining boots were not
individually recorded. The post-boot p69 re-read matched `18d2a83d…`
(battery 92%), binding every boot to the flashed bytes.

About seventeen boots before the verdict stopped pre-identification with
the H8 CMD3 completion-silent class (`PROBE FAILED stage=13 rc=-7`,
`step=9`, `fail=1/3/65536/0000031a/0000/0000/03f800f0/9`, the full
100,000-iteration ≈1.14 s poll window; two were captured field-identically
and only the `c31` ticks and the stack pages/free differ): facts only,
re-run not retried, and the H13 experiment never ran in those boots.

The verdict boot emitted the single `GTA4L-EMMC H13 REPORT`:
`rc=-12 stop=14` (`GTA4L_EMMC_ERR_DATA_CRC`; CMD14 transport or
card-status failure). The pre-rung `R` line was clean
(`clk=0007 hc=00 bw=00 es=000009cb`). The `W` line reproduced the wedge
(`wrc=-11` = `GTA4L_EMMC_ERR_DATA_TIMEOUT`, `err=0000`, `nrm=0011` =
`CC | BWR`, `pst=03f801f6` = `DAT_INHIBIT=1`, `DAT_LINE_ACTIVE=1`); the
terminal `FAIL` tuple `1/19/0/0000133a/0000/0011/03f801f6/18` is
field-identical to the historical B1C 4-bit and B1C-1 1-bit rows (only
the source location differs, `:2403` here versus `:1085` there). The
bounded reset self-cleared (`RST rc=0 wr=04 rb=00 it=6 pst=03f800f0
dinh=0`), about 60 us after six status reads, so stops 10/11 did not
occur and the controller-side latch is fully recovered. CMD14 then failed
with `trc=-12`; `tw`/`te` stayed zero because the pattern words are only
filled after a successful data read. The block-0 recovery read returned
`Q rc=-17` (`GTA4L_EMMC_ERR_INHIBIT`) with `qb0=00000000` unset, so the
rung's own data conversation was still not inhibit-clean. `PAD` pre
equals post (`00009fe4/6000642c/00000000`) and `log=31` (6 identification
events plus 25 stage-A CMD17 reads; H13 does not re-read EXT_CSD before
the rung).

The result is a single verdict sample (the two-matching rule is unmet):
the host data-circuit reset clears the controller's latched data state
but does not restore the card to a working data conversation. CMD14 fails
at the CRC layer and the next block read is inhibit-blocked, consistent
with a card still waiting for the write data phase it never received.
The card remained readable at the partition level in TWRP (the post-batch
p69 read matched the post-flash hash), so a fresh TWRP boot reinitialized
it. F3 remains `[U]`; the device record names the forced post-write
CMD12/CMD13 drain sample as the next step, to be drafted only after this
record lands. Evidence:
`out/gta4l/ram-root-emmc-probe-h13-evidence/` (`flash-record.json`,
`device-run-record.json`, `device-run-transcript.txt`,
`postflash-hashes.txt`, `postboot-reread.txt`).

### 5.3.30 Stage-H14 rung-2 forced post-write drain (gta4l-emmc-probe=14, host batch 2026-11-04, paused)

**Paused by the 2026-11-04 protocol correction (§1a).** The write-wedge audit
itself says CMD12 is not appropriate for the single-block CMD19 write, and
the stop-code mapping below conflated silent, CRC, inhibit and missing-busy
cases. The already-flashed H14 image is historical evidence; it is not a
prerequisite for H15. A new build of this stage uses the corrected CMD14 CRC
and 1-bit pattern rules, so its report is not byte-compatible with the old
image.

The `gta4l-emmc-probe=14` stage is the second rung of the write-phase
wedge ladder and implements the forced post-write CMD12/CMD13 drain
sample named by the H13 record. It keeps the H13 form: the standard
read-only identification once (no CMD3 retry), the unchanged stage-A
content pass at 400 kHz/1-bit as the baseline, one record-only 1-bit
CMD19 bus-test write (the 4-byte pattern block, first byte `0x5A`,
compared as `0xA5`), one bounded SDHCI data-circuit reset
(`SOFTWARE_RESET` ← `0x04` with the 100,000 us self-clear bound and the
10 us poll step) and the post-reset `PRESENT_STATE`/`DAT_INHIBIT`
sample. It then inserts the drain between the reset and the CMD14 read:
one forced CMD12 `STOP_TRANSMISSION` (R1b: `RESP_SHORT_BUSY | CRC |
INDEX`, argument 0) through the normal command engine with no ACMD12
transfer-mode bit, followed, when the stop completed, by one CMD13
`SEND_STATUS` (R1: `RESP_SHORT | CRC | INDEX`, RCA argument). One CMD14
read with the existing pattern comparison and the block-0 recovery read
follow, both record-only. The stage always emits `GTA4L-EMMC H14 REPORT`
and stops; there is no on-device retry and no fall-through. The only
card-facing write is the non-persistent CMD19 pattern; no CMD6, CMD18,
CMD25 or user-data write is issued, and no card width or timing is
changed. The stage parser accepts 1..14.

The drain is local to `gta4l_emmc_post_write_drain()`; no other timeout
path changes and `TIMEOUT_CONTROL` is untouched. The CMD12 entry wait
checks only `COMMAND_INHIBIT`, the explicit SDHCI stop-command
exemption, because an aborted data transfer may still hold
`DAT_INHIBIT` while the command line is free; CMD13 keeps the normal
both-inhibit wait. Each command has its own bounded window and captures
its terminal tuple before the status clear, and a card-status bit in
the R1 reply is recorded rather than promoted to a transport error. The
rung-2 form is reached through
`gta4l_emmc_bus_test_reset_drain()`, which shares the rung-1 body
`gta4l_emmc_bus_test_reset()` keeps drain-free. The drain result lives in
`struct gta4l_emmc_post_write_drain_result`; `status_result` is
`GTA4L_EMMC_ERR_UNSUPPORTED` when CMD12 failed and CMD13 was not issued.
The probe presets both drain results to that code when a reset or inhibit
stop returns before the drain, so the report never prints a zeroed
"success" for a command that did not run.

Stop codes: 0 the recorded wedge, a clean reset and drain, and CMD14/Q
passing; 10 the reset did not self-clear; 11 `DAT_INHIBIT` survived the
reset; 12 the width/argument precondition refused the helper; 13 CMD12
was silent (no R1 within the window); 14 CMD12 answered but CMD13
failed; 15 the drain completed but CMD14 or the recovery read failed;
16 the write completed normally and the rest passed; 17 the write failed
with a non-timeout error. Reset and inhibit verdicts precede the drain;
a non-timeout write verdict precedes the drain and CMD14 verdicts,
matching the rung-1 precedence.

The report carries the header (`rc`, `stop`, `mci`, `rca`, `cap`, `rev`,
`hci`, `step`, `log`, `bw`), an `R` pre-rung line (`clk`, `hc`, `bw`,
`es`), a `W` write line (`wrc`, `err`, `nrm`, `pst`), a `RST` reset line
(`rc`, `wr`, `rb`, `it`, `pst`, `dinh`), a `C12` stop line (`rc`, `r1`,
`err`, `nrm`, `pst`), a `C13` status line (`at`, `rc`, `r1`, `err`,
`nrm`, `pst`), a `T` CMD14 line (`trc`, `tw`, `te`), a `Q` recovery line
(`rc`, `qb0`), the shared `PAD` line and the terminal `FAIL` record.

Host checks: the probe suite is 151 scenarios and 77 mutation controls
(was 139/69), adding `probe-h14-pass`, `probe-h14-reset-stuck`,
`probe-h14-inhibit`, `probe-h14-cmd12-silent`, `probe-h14-cmd12-crc`,
`probe-h14-cmd13-fail`, `probe-h14-mismatch`, `probe-h14-read-crc`,
`probe-h14-q-fail`, `probe-h14-write-crc` and `probe-h14-normal` plus the
twelfth width materialization `report-width-h14`. The core suite is 168
scenarios and 104 mutation controls (was 160/98), adding the clean,
silent, CMD13-CRC, data-inhibit-exempt and uninitialized post-write
drain cases plus the rung-2 reset-plus-drain pass, record-only silent
drain and surviving-inhibit cases, with mutation controls for a skipped
helper drain, a removed CMD12 DAT_INHIBIT exemption, a wrong stop-code
mapping, a skipped CMD13, a lost R1 record and a wrong stop response
type. The diagnostic wrapper and payload gate accept `emmc-probe-14`.
No build, flash or boot was performed when the host batch landed.

### 5.3.31 Stage-H15 device run (first cold boot, 2026-11-05)

The H15 image from commit `9facb6b` was flashed to BOOT only and booted once.
Identification completed normally (no pre-identification CMD3 stop). The
owner-supplied report is preserved at
`out/gta4l/ram-root-emmc-probe-h15-evidence/device-run-transcript.txt`:

```
GTA4L-EMMC H15 REPORT: rc=4294967279 stop=15 mci=10000072 rca=1 cap=61071360 rev=8 hci=02 step=20 log=32 bw=01
GTA4L-EMMC H15 R clk=0007 hc=00 bw=00 es=000009cb
GTA4L-EMMC H15 W rc=0 req=4 sw=1 vf=1 c6=00000900 hb=00 ha=02 es=000009cb rb=0
GTA4L-EMMC H15 B trc=4294967285 tw=00000000 te=00000000
GTA4L-EMMC H15 T att=1 same=0 div=0 b0=00000000 b1=00000000 data=00000000 last=00000000
GTA4L-EMMC H15 V rc=4294967279 sw=0 vf=0 c6=00000000 hb=02 ha=02 es=00000000 rb=0
GTA4L-EMMC H15 Q rc=0 qb0=00000000
GTA4L-EMMC H15 PAD pre=00009fe4/6000642c/00000000 post=00009fe4/6000642c/00000000
GTA4L-EMMC H15 FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/20 @Gta41EmmcProbe.c:2748
```

Interpretation:

- The verified switch passed: CMD6 returned `0x00000900`, the host width
  moved `00→02`, and the read-only EXT_CSD comparison passed (`vf=1`). This
  is the first positive evidence that the card and host carried a 4-bit
  DAT path at 400 kHz. It also ends the byte-183 "refusal" reading: the
  switch is not observable in byte 183, but the data path demonstrably
  switched.
- The single CMD19 write failed with `trc=-11` (data timeout). The terminal
  tuple `err=0000 nrm=0011 pst=03f801f6` shows command completion and
  BufferWriteReady with `DAT_INHIBIT` still set, the same write-phase wedge
  previously seen at 1-bit. It is therefore not a 1-bit-only effect and not
  evidence that the 4-bit switch failed.
- The H15 revert used `gta4l_emmc_set_bus_width_verified()`, which reads
  EXT_CSD before issuing CMD6. That read was refused by the latched
  `DAT_INHIBIT` (`rc=-17`), so no CMD6 was issued (`c6=00000000`) and the
  host stayed at 4-bit. This is a recovery-order defect in the stage, not a
  card verdict.
- The 4-bit content pass and the block-0 recovery read did not run. The old
  report format printed `T att=1` unconditionally and left `Q rc=0` as a
  default; neither line is a result. They are not evidence of a successful
  content pass or recovery read.
- The PAD samples are identical before and after (`qdsd=00009fe4`,
  `dll=6000642c`, `dst=00000000`), so this run changed no pad or DLL
  configuration.

Corrective host batch:

- `GTA4L_EMMC_ERR_WIDTH_RESTORE` is now the only "framing unknown" result,
  and the H15/H16 `V` line distinguishes an attempted revert from a clean
  one.
- The new `gta4l_emmc_set_bus_width_recover()` takes a read-only EXT_CSD
  baseline captured while the path was healthy, clears a latched
  `DAT_INHIBIT` with one bounded local data reset, then issues CMD6 and
  verifies with the properties comparison. It never pre-reads the possibly
  wedged data path.
- B/T/V/Q now print explicit `att` flags and T prints its result code.
- Stage 16 (`gta4l-emmc-probe=16`) is the content-only isolation rung: the
  stage-A baseline, the verified 4-bit switch, the 4-bit content pass, the
  baseline-based recovery and the block-0 recovery read, with no CMD19 or
  CMD14 at all. It answers whether 4-bit sector reads work without the
  write-phase wedge.
- Stop codes for H15/H16: 0 all clean; 10 the pre-switch baseline EXT_CSD
  read failed; 11 the switch or its verification failed with framing
  restored; 12 unknown framing after a failed restore; 13 the 4-bit bus test
  failed; 14 the 4-bit content pass diverged or its read failed; 15 the
  width recovery could not prove the framing; 16 the recovery read failed.
  H16 omits 13 because it never issues CMD19/CMD14.

Host checks after the batch: core 179 scenarios and 104 mutation controls,
probe 158 scenarios and 77 mutation controls, including the reproduced 4-bit
write wedge with a clean baseline-based recovery and the surviving-inhibit
case that stops before CMD6. No new image was built or flashed for this
record. A rebuilt H15 is therefore not byte-identical to the flashed `9facb6b`
image: the report prints the new `att`/`c6at` fields and the revert uses the
baseline-based recovery.

### 5.3.32 Stage-H16 device run: the 4-bit content pass succeeds (2026-11-05)

The H16 image from commit `20c9f9a` (payload
`7404870a35f794bb50b9e02faf5eb083ac9dbd96096d6a59068468a3d332a512`, 7/7;
boot image
`4f1bfb9094c2951ed3f02b08f828d9d905d4c3412c86d6dd43710a7f9ec9fcc0`,
18/18) was written to BOOT only after a verified backup. The owner
warm-rebooted from TWRP into the flashed image and the first attempt produced
a verdict, so no retry was needed. The owner-supplied report is preserved at
`out/gta4l/ram-root-emmc-probe-h16-evidence/device-run-transcript.txt`
(line wraps rejoined) and decoded at `device-run-record.json`:

```
GTA4L-EMMC H16 REPORT: rc=0 stop=0 mci=10000072 rca=1 cap=61071360 rev=8 hci=00 step=18 log=57 bw=00
GTA4L-EMMC H16 R clk=0007 hc=00 bw=00 es=000009cb
GTA4L-EMMC H16 W rc=0 req=4 sw=0 vf=1 c6at=1 c6=00000900 hb=00 ha=02 es=000009cb rb=0
GTA4L-EMMC H16 B att=0 trc=0 tw=00000000 te=00000000
GTA4L-EMMC H16 T att=1 rc=0 same=1 div=0 b0=62084b66 b1=84d0543e data=f418cd33 last=a89b26d5
GTA4L-EMMC H16 V att=1 rc=0 sw=0 vf=1 c6at=1 c6=00000900 hb=02 ha=00 es=000009cb rb=0
GTA4L-EMMC H16 Q att=1 rc=0 qb0=62084b66
GTA4L-EMMC H16 PAD pre=00009fe4/6000642c/00000000 post=00009fe4/6000642c/00000000
GTA4L-EMMC H16 FAIL fail=0/0/0/00000000/0000/0000/00000000/0 @Gta41EmmcProbe.c:2764
```

Interpretation:

- The verified 4-bit SDR switch completed on the first CMD6 attempt
  (`c6at=1`, `c6=00000900`), the host width moved `00→02`, and the read-only
  EXT_CSD properties comparison passed (`vf=1`, `es=000009cb`). This is the
  second such switch and the first one on a warm boot.
- The 4-bit content pass is the first **successful** per-LBA content read at
  4-bit: `T att=1 rc=0 same=1 div=0` with `b0=62084b66`, `b1=84d0543e`,
  `data=f418cd33` and `last=a89b26d5` equal to the 1-bit stage-A baseline.
  Twenty-one primary-array blocks, the DATA LBA and the last LBA therefore
  read correctly over four DAT lines at 400 kHz.
- The baseline-based recovery worked on hardware instead of being blocked:
  `V att=1 rc=0`, `c6at=1`, CMD6 `00000900`, host `02→00`, `vf=1`. The
  latched-inhibit failure mode of §5.3.31 is closed.
- The block-0 recovery read passed at 1-bit and returned `qb0=62084b66`,
  identical to `T b0`, so the post-revert read path is intact.
- `B att=0` is a not-attempted marker, not a pass. H16 issues no CMD19 and
  no CMD14, so this run says nothing about the write phase.
- `fail=0` and unchanged `PAD` samples: no failure was latched and no pad,
  DLL or pad-status bit changed.
- The `W` line reads back `BUS_WIDTH=0` while the verified 4-bit path was
  live, where H15 read back `1`. Byte 183 is therefore not a usable switch
  indicator on this part in either direction; the read-only properties
  comparison and the 21-block content pass are the width evidence. The `V`
  line's `0` is the genuinely correct post-revert value.
- The report ends in an intentional panic: the probe has no exit path, so
  the panic is the delivery mechanism, not an H16 failure.

What this establishes, and what it does not:

- **4-bit SDR sector reads are measured `[C]`** at 400 kHz for this part.
  Together with §5.3.31 this also confirms the write wedge is not a
  width-caused data-path failure.
- **F3 stays `[U]`.** Its bus-test clause needs a CMD19/CMD14 round trip,
  and H16 deliberately does not attempt one. The content clause is now met
  at 4-bit, so only the round trip is outstanding.
- The rung is a warm boot, so the cold-path caveat recorded for the H8/H9
  identification class does not extend to the content pass.

The recorded CMD19 timeout now has a reference-backed explanation, which
`docs/emmc-write-wedge-audit.md` already ranked first: JEDEC B1.51 §6.6.4
defines CMD19 as a host-to-device *pattern*, the device ignores all but the
first two bits per line, and no CRC status token is defined for it. TI erratum
SPRZ436H i836 records controllers that wait for that token and signal Data
Timeout, with the guideline to ignore the timeout, clear it and continue to
CMD14. Linux goes further: `mmc_bus_test()` (`mmc_ops.c`) discards every
error from `BUS_TEST_W` and lets `BUS_TEST_R` decide, `sdhci_data_irq()`
excludes `MMC_BUS_TEST_R` from the data-CRC error path, and `sdhci` resets the
data circuit on the data error before the next command. The port instead
treated the CMD19 timeout as a headline failure, so it never issued CMD14 and
left `DAT_INHIBIT` latched. The next rung therefore changes the completion
rule, not the width or the timing.

Next rung (host batch, then a fresh owner approval): a stage that runs the
same baseline, verified 4-bit switch and 4-bit content pass, then one CMD19
write whose data timeout is recorded and tolerated, one bounded data-circuit
reset, one CMD14 read with the existing pattern comparison (and the existing
tolerated-CRC rule), the baseline-based revert and the block-0 recovery read.
That rung is the F3-at-4-bit candidate.

### 5.3.33 Stage-H17: the Linux-compatible bus-test completion rule (host batch, 2026-11-05)

The H16 run leaves exactly one F3 clause open, and the pinned references say
the previous rungs were asking the wrong question. Linux `mmc_bus_test()`
(`drivers/mmc/core/mmc_ops.c`) calls `mmc_send_bus_test(MMC_BUS_TEST_W)` and
**discards its return value**, then lets `MMC_BUS_TEST_R` decide:

```c
	/*
	 * Ignore errors from BUS_TEST_W.  BUS_TEST_R will fail if there
	 * is a problem.  This improves chances that the test will work.
	 */
	mmc_send_bus_test(card, card->host, MMC_BUS_TEST_W, width);
	return mmc_send_bus_test(card, card->host, MMC_BUS_TEST_R, width);
```

The same function returns success immediately for a 1-bit bus
(`if (bus_width == MMC_BUS_WIDTH_1) return 0; /* no need for test */`), which
is another reason the historical 1-bit discriminator was never a valid
comparison. `sdhci_data_irq()` excludes `MMC_BUS_TEST_R` from the data-CRC
error path, and `sdhci` resets the data circuit on the data error before the
next command. JEDEC 6.6.4 defines CMD19 as a pattern with no CRC status token,
and TI erratum SPRZ436H i836 records the resulting controller data timeout with
the guideline to ignore it, clear it and continue to CMD14.

The port instead returned on the CMD19 write result, so no rung ever reached
CMD14 under the 4-bit switch. The host batch changes the completion rule:

- `struct gta4l_emmc_host` gains
  `bus_test_tolerate_write_timeout`. Only a stage that opts in gets the new
  rule, so H15 keeps its recorded strict behaviour.
- `struct gta4l_emmc_bus_result` gains `write_result` and `reset_result`;
  every initializer sets both to `GTA4L_EMMC_OK`.
- `gta4l_emmc_bus_test_common()` records the CMD19 result. When it is
  `GTA4L_EMMC_ERR_DATA_TIMEOUT` and the caller opted in, it issues
  `gta4l_emmc_clear_dat_inhibit()` (one bounded local data-circuit reset,
  self-clear polled, post-reset `DAT_INHIBIT` checked) and continues to the
  CMD14 read. Any other write error, and a reset that cannot prove the
  circuit idle, still stops as before.
- The CMD14 read keeps the existing single-tolerated-CRC rule, and the
  returned pattern still decides.

Stage 17 (`gta4l-emmc-probe=17`) is the resulting rung: the standard read-only
identification once (no CMD3 retry), the stage-A content pass at 400 kHz/1-bit
as the baseline, the verified 4-bit SDR switch, **the full 4-bit content
pass**, one CMD19 4-byte pattern write with the tolerated timeout, the bounded
data reset, one CMD14 read, the verified return to 1-bit and the block-0
recovery read. It always emits `GTA4L-EMMC H17 REPORT` and stops with
0 (all clean), 10 (baseline read failed), 11 (switch/verification failed with
framing restored), 12 (unknown framing after a failed restore), 13 (bus test
failed: a non-timeout write error, a reset that could not prove the circuit,
a CMD14 transport error or a pattern mismatch), 14 (content read failed or
diverged), 15 (width recovery unproven) or 16 (recovery read failed). It
issues no CMD18, CMD25 or user-data write, and the only card writes are the
CMD6 width values and the non-persistent CMD19 pattern.

The `B` line gains the write-phase record and is now shared by H15/H16/H17:

```
GTA4L-EMMC H17 B att=1 wrc=-11 rst=0 ci=0 trc=0 tw=000000a5 te=000000a5
```

`wrc` is the CMD19 write result, `rst` the data-reset result, `ci` the
tolerated CMD14 CRC flag, and `trc`/`tw`/`te` keep their existing meaning.
H15 therefore reports `wrc=-11 trc=-11` on its recorded wedge (a strict
refusal), while H17 reports `wrc=-11 trc=0` when the CMD14 read-back passes.

Host checks: the core suite is 179 scenarios and 104 mutation controls (was
176/104), adding `bus-test-tolerate-timeout` (tolerated timeout, clean reset,
pattern accepted), `bus-test-tolerate-inhibit` (surviving `DAT_INHIBIT` stops
before CMD14) and `bus-test-tolerate-write-crc` (a non-timeout write error is
not tolerated). The probe suite is 168 scenarios and 77 mutation controls
(was 158/77), adding `probe-h17-pass`, `probe-h17-content-div`,
`probe-h17-pattern-mismatch`, `probe-h17-write-crc` and `probe-h17-inhibit`;
the existing H15/H16 expectations are updated for the shared `B` line.

Build and flash (2026-11-05): the kernel was rebuilt from the committed H17
sources (`ca983f7d…`), the probe archive is `b99cdbd3…`, the payload
`72f6e2d4…` passes 7/7 with `emmc-probe-17` selected and the boot image
`13938e49…` passes the 18/18 gate against the `ce4d76e8…` v5 baseline. The
first payload build was refused by the contract because the checker's
`DIAGNOSTICS` tuple still ended at stage 16; the tuple is now derived from
`DIAGNOSTIC_NAMES`, so the two lists cannot drift again, and the rebuilt
payload passes. The image was compressed with `xz -6 -T0` (`b87f131b…`,
8,056,988 bytes), transferred and written to BOOT only
(`/dev/block/mmcblk0p69`) after a verified backup of the H16 image
(`3adb7401…` → `70d2ed37…` xz, decompressing exactly). Post-flash p69 is
`60d3d4bc…`; the image prefix matches, the unchanged tail is `a28b4ea2…`,
RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` are unchanged
and the battery read 100% after the write. No reboot was issued. Evidence:
`out/gta4l/ram-root-emmc-probe-h17-evidence/`.

The rung was the F3-at-4-bit candidate: a `stop=0` with
`wrc=-11 rst=0 trc=0 tw=000000a5` and a matching 4-bit content pass satisfies
F3's bus-test clause under the reference completion rule, with the tolerated
timeout recorded rather than hidden. A pattern mismatch would have kept F3
`[U]` and bound the failure to the returned word, and a failed reset would
have stopped with unknown framing instead of pretending the bus is idle. The
device run returned exactly that pass row (§5.3.34).

### 5.3.34 Stage-H17 device run: the CMD19/CMD14 round trip passes at 4-bit (2026-11-05)

The flashed H17 image from commit `a198db1` (payload `72f6e2d4…` 7/7, boot
image `13938e49…` 18/18) was booted four times without reflashing: cold, then
three warm. The first three attempts stopped at the known pre-identification
CMD3-completion timeout class seen on earlier rungs and produced no report; the
fourth (warm) attempt produced the complete report below. A pre-identification
stop is a fact, not an H17 verdict, and it is recorded as an identification
reliability observation rather than an H17 failure. The owner-supplied report
is preserved at
`out/gta4l/ram-root-emmc-probe-h17-evidence/device-run-transcript.txt`
(line wraps rejoined) and decoded at `device-run-record.json`:

```
GTA4L-EMMC H17 REPORT: rc=0 stop=0 mci=10000072 rca=1 cap=61071360 rev=8 hci=00 step=21 log=59 bw=00
GTA4L-EMMC H17 R clk=0007 hc=00 bw=00 es=000009cb
GTA4L-EMMC H17 W rc=0 req=4 sw=1 vf=1 c6at=1 c6=00000900 hb=00 ha=02 es=000009cb rb=0
GTA4L-EMMC H17 B att=1 wrc=4294967285 rst=0 ci=0 trc=0 tw=000000a5 te=000000a5
GTA4L-EMMC H17 T att=1 rc=0 same=1 div=0 b0=62084b66 b1=84d0543e data=f418cd33 last=a89b26d5
GTA4L-EMMC H17 V att=1 rc=0 sw=0 vf=1 c6at=1 c6=00000900 hb=02 ha=00 es=000009cb rb=0
GTA4L-EMMC H17 Q att=1 rc=0 qb0=62084b66
GTA4L-EMMC H17 PAD pre=00009fe4/6000642c/00000000 post=00009fe4/6000642c/00000000
GTA4L-EMMC H17 FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/18 @Gta41EmmcProbe.c:2775
```

Interpretation:

- The verified 4-bit SDR switch completed on the first CMD6 attempt
  (`c6at=1`, `c6=00000900`), the host moved `00→02`, and the read-only
  EXT_CSD properties comparison passed (`vf=1`, `es=000009cb`). This is the
  third such verified switch on hardware and the second that then carried
  traffic over four DAT lines.
- The 4-bit content pass ran before the bus test and matched the 1-bit
  baseline: `T att=1 rc=0 same=1 div=0` with `b0=62084b66`, `b1=84d0543e`,
  `data=f418cd33` and `last=a89b26d5`.
- The CMD19 write produced exactly the expected no-CRC-status terminal state:
  `wrc=4294967285` is the unsigned panic rendering of `-11`
  (`GTA4L_EMMC_ERR_DATA_TIMEOUT`), and the failure record is bound to the
  command (`fail_index=19`, `fail_command=0x0000133a`) with `CC` latched
  (`normal=0x0011`), no error bit (`error=0x0000`) and the recorded
  write-direction present state `0x03f801f6`. The strict path would have
  stopped here, as H15 did.
- The opt-in completion rule tolerated only that timeout, issued one bounded
  data-circuit reset (`rst=0`) and continued to CMD14, which returned the
  expected word (`trc=0`, `tw=te=000000a5`, `ci=0`). This is the first
  successful CMD19/CMD14 round trip on this device at any width, and it is
  the §1 F3 bus-test clause.
- The baseline-based revert was verified on hardware (`V att=1 rc=0 sw=0
  vf=1 c6at=1 c6=00000900 hb=02 ha=00 es=000009cb rb=0`) and the block-0
  recovery read matched `T b0` (`Q att=1 rc=0 qb0=62084b66`).
- The `FAIL` line still reports `fail=1` because the tolerated timeout keeps
  its failure record; it is the preserved record of the expected write-phase
  timeout, not an overall failure. `rc=0 stop=0` is the run verdict, and the
  later stages prove the record did not wedge the host.
- `byte183` (`sw`) read back `1` here, `0` in H16 and `1` in H15, all on
  verified live 4-bit paths. Three samples across both directions confirm it
  is not a usable switch indicator; the read-only properties comparison and
  the content pass remain the width evidence. The `V` line's `0` is the
  correct post-revert value.
- The `PAD` samples are unchanged, so this run changed no pad, DLL or
  pad-status configuration.
- The report ends in an intentional panic. The reported location
  `@Gta41EmmcProbe.c:2775` is the last argument line of the shared report
  panic in `a198db1` (the H15 and H16 reports bind to their commits the same
  way at 2748 and 2764), so the report is tied to the flashed source as well
  as to the image hash.

What this establishes, and what it does not:

- **F3 is measured PASS at 4-bit SDR, 400 kHz `[C]`.** The §1 definition
  asks for the pad/bus to be switched to 4-bit or 8-bit with a CMD19 pattern
  round-trip in the report; both are present, and the tolerated write timeout
  is the JEDEC/TI/Linux-expected terminal state for a pattern with no CRC
  status token, recorded rather than hidden.
- **The 8-bit width is untried.** A 4-bit pass closes the §3.1 width rung
  under its own definition, but it says nothing about 8-bit wiring; an 8-bit
  attempt is the next width question and a failure there would be a separate,
  exact binding.
- **Cold-boot identification reliability is still open.** The verdict came
  from the fourth attempt; the first three stopped pre-identification at the
  same CMD3-completion class already recorded for H8/H9. That class is
  unchanged by H17 and is not an F3 property.
- Two diagnostic carry-forwards are recorded for the next rung, neither of
  which affects this verdict: `host->last_step` is only advanced to
  `GTA4L_EMMC_STEP_BUS_TEST` after the CMD14 read, so the tolerated timeout's
  failure record reads step 18 (`PROBE_LAST`) where the bus test is step 21;
  and the tolerated timeout leaves `fail_valid=1`, so `record_failure()` would
  ignore a later failure in the same boot. H17's later steps passed, but a
  rung with two bus tests must snapshot or clear the tolerated record (or add
  a second slot) before continuing.

Raw H17 evidence stays outside the tracked tree at
`out/gta4l/ram-root-emmc-probe-h17-evidence/` (`device-run-transcript.txt`,
`device-run-record.json`, `flash-record.json`, build provenance and hashes).

### 5.3.35 P5-1 read-only block adapter host batch (2026-11-05)

The Stage-C/F5 host implementation is recorded in
`docs/gta4l-storage-publication-design.md` and adds no device result. The
read-only `IOBlockStorageDevice` path now consists of:

- `package/Gta4lEmmcBlock.cpp` — the thin adapter. It owns one
  platform/host/helper state and one controller lock, maps and initializes
  the platform once, registers only when report mode
  `gta4l-emmc-block=1` is selected, implements the audited override set
  (512-byte geometry, capacity `- 1` maximum block, non-removable and
  non-ejectable, write protection, no write cache, refused destructive/
  discard operations and the Generic device type), advertises 64 blocks /
  32768 bytes and performs only read-direction requests through the
  existing `gta4l_emmc_read_block` helper. A bounded report thread waits for
  the whole `IOMedia`, checks its geometry and compares a same-boot baseline
  sum with a media-path read; a watchdog bounds backend activity. The
  adapter rejects mode 2 (publish-only) rather than enabling it silently and
  refuses a probe-family argument, another SDHCI diagnostic or a duplicate
  block token before MMIO.
- `package/Gta4lEmmcBlock.c/.h` — the portable policy: mode selection,
  request pre-validation with distinct refusal classes, the same-boot sum
  (`sum * 31 + byte`) and the bounded `GTA4L-EMMC P5 READ:` / `P5 FAIL:`
  report formatter.
- `scripts/build-gta4l-emmc-block.py` — compiles the policy object with the
  `bsd_init.o` record and the adapter with the `IOPlatformExpert.cpo` record
  against the qualified storage headers, binds the original storage
  base-class symbols from the interface audit and archives
  `libGta4lEmmcBlock.a`. It refuses a missing, stale or wrong-candidate
  storage-ABI audit and never marks the archive `runtime_qualified`.
- `patches/xnu/gta4l-emmc-block.patch` — the guarded
  `Gta4lEmmcBlock` personality appended to `KernelConfigTables.cpp` and
  added to `patches/xnu/series`. At the D7 batch
  `configs/gta4l_defconfig` set
  `CONFIG_XNU_NATIVE_GTA4L_EMMC_BLOCK=n` and force-loaded the new archive,
  so it was linked but inert: the shared build script did not yet translate
  the personality key, and the adapter additionally requires report mode 1.
  The D8 batch (section 5.3.36) enables the personality.
- `emmc-block-1` in the diagnostic name map and payload contract, mapping to
  `gta4l-emmc-block=1`. The contract refuses duplicates, a mixed probe/block
  selection, a malformed probe token in block mode and the deferred mode 2.

Build and host results:

- The P5-1 builder produced `build/gta4l-emmc-block/libGta4lEmmcBlock.a`
  (`fedd4bc3…`) from the recorded sources (`Gta4lEmmcBlock.c`
  `a2689508…`, `.h` `1c13ce98…`, `.cpp` `021dd555…`), against candidate
  `ca983f7d…` and interface audit `695c7bbe…` (190 virtual methods, 26
  concrete overrides, 0 unresolved original imports). Qualification is
  `true` under `out/gta4l/storage-abi/` (provenance `cbf3a80b…`); the
  archive is explicitly not runtime-qualified. The D8 re-run in §5.3.36
  supersedes the candidate and archive hashes; this pre-D8 output remains
  recorded here.
- `scripts/test-gta4l-emmc-block.py` passes 23 core scenarios with 18
  mutation controls and 28 adapter scenarios against the host IOKit seam.
  The adapter scenarios cover disabled/conflicting/duplicate/deferred
  arguments, init/lock/thread failures, properties and limit advertisement,
  write and invalid-request refusal, one/multiple/maximum reads, short and
  overlong copies, first-error offline transition, media absence and wrong
  geometry, media-read failure/short/divergence/match, GPT absence, the
  watchdog and the policy mapping.
- The existing core/probe suites pass 179/104 and 168/77; the storage-ABI
  positive/negative suite, the diagnostic payload contract, the inventory
  and the board checks pass. The P5-1 device-repo core/probe archives are
  `4c977fe1…`/`ae10304c…`; the D8 in-tree rebuild is `5e5ee937…`/
  `19ac22d0…` (§5.3.36).

The P5-1 batch built no kernel or payload image; the D8 batch then built a
host kernel (§5.3.36). Nothing was flashed or booted, and no `IOMedia`
object has been observed; F5 remains `[U]`. The device candidate and the
P5-2 BOOT-only approval are a separate gate.

### 5.3.36 D8 enabled-personality host candidate (2026-11-05)

The D8 batch enables `CONFIG_XNU_NATIVE_GTA4L_EMMC_BLOCK=y` in
`configs/gta4l_defconfig`, force-loads `libGta4lEmmcBlock.a` and adds the
shared configuration-to-flag translation. The sibling UniXNU working tree
adds `scripts/xnu-native-personality.sh` (sourced by
`scripts/build-xnu-4k-baseline.sh`), which maps the value `0` or an absent
key to no flag, `1` to `-DXNU_NATIVE_GTA4L_EMMC_BLOCK=1` and any other value
to a hard error. The host kernel build exited 0 and produced the prelinked
candidate `b509c511…` (51,187,712 bytes; `kernel.json` `candidate_sha256`
matches); the `KernelConfigTables.cpo` compile record carries
`-DXNU_NATIVE_GTA4L_EMMC_BLOCK=1`, so the personality is compiled in and the
archive is linked. This is a host build only: no payload image was built,
nothing was flashed or booted, no `IOMedia` object was observed, and F5
remains `[U]`.

A relink changes the candidate hash through the version timestamp, so the
storage-ABI audit was re-run against the final kernel rather than the earlier
`66977822…` build. The re-audit passes: `interface-audit.json`
(`6908349252b920c…`) reports 190 method slots, 181 kernel bindings (167
inherited + 14 pure virtual), live native 4K, the same `_bsd_init` stop
(`0xfffffff0076e7a3c`) and 0 binding-target diffs against the earlier audit;
`provenance.json` is `eace94306cd4…`. The audit's `gdb_stop_sha256` field
names the `gdb-stop.py` harness script (`3ba447bc…`), while the builder's
`gdb_stop_sha256` field names the `gdb-stop.json` snapshot (`d328190b…`);
both are recorded so the two hashes are not confused.

`scripts/build-gta4l-emmc-block.py` (`ae6fc34a…`, identical in the repository
and the build tree) was then re-run in the tree without relinking the kernel.
It produced a byte-identical `libGta4lEmmcBlock.a` (`ff447a48…`) from the
policy object, the C++ object and the generated original bindings, with 190
virtual methods, 26 concrete overrides and 0 unresolved original imports.
The qualification records `qualified=true`, `candidate_matches_audit=true`,
`personality_enabled=true` and `runtime_qualified=false`; the in-tree core
and probe archives are `5e5ee937…` and `19ac22d0…`. The host suites pass
(block 23/18 plus 28 adapter scenarios, core 179/104, probe 168/77), and the
storage-ABI, payload-contract, inventory, package and boot-image suites pass.
The earlier P5-1 output (`fedd4bc3…` against `ca983f7d…`) remains recorded in
§5.3.35 as the pre-D8 artifact.

### 5.3.37 P5-2 device run and corrected report-client lifecycle (2026-11-05)

The first approved P5-2 BOOT-only flash used the D8 candidate `b509c511…`
(block archive `ff447a48…`, payload `7b2ac8c0…`, boot image `4a635cae…`).
Its first cold boot published the whole `IOMedia` but the report client did
not hold a valid open, so the media-path read failed. The audit attributed
the failure to the adapter's report-client lifecycle, not to the transport
or the card; the initial candidate and its failure remain recorded in
`out/gta4l/ram-root-emmc-block-p5-evidence/` and are not relabeled.

The D9.4 correction opens the media before the report read with
`media->open(this, 0, kIOStorageAccessReader)` and closes exactly once on
every success, failure and cancellation exit, recording the access mode; the
same client is used for read and close. The host IOKit seam now starts
closed, returns `kIOReturnNotOpen` on an unopened read, models open refusal
and descriptor failure, and checks access mode, client identity and close
accounting. The block suite passes 23 core scenarios with 18 mutation
controls and 32 adapter scenarios (was 28); the new adapter scenarios include
the unopened-read regression and the balanced-open check.

The corrected p5b candidate was rebuilt from the same working tree with the
new source binding: kernel `2f8238b9…` (51,187,712 bytes; `kernel.json`
candidate matches), qualified block archive `6d36826a…` (350,248 bytes; 190
virtual methods, 26 concrete overrides, 0 unresolved original imports,
requalified against the new candidate with interface audit `1f456fec…` and
provenance `cdfe6950…`), payload `47b9593d…` (9/9 contract, exactly one
`gta4l-emmc-block=1`, no probe token) and boot image `e256b27d…` (18/18 gate,
only the ramdisk differs from the `ce4d76e8…` baseline). The packaged payload
kernel is `26af8293…`; `kernel-string-verification.txt` records the class,
personality and report strings and the payload token counts.

The owner approved a fresh BOOT-only flash and the image was written to
`/dev/block/mmcblk0p69` only: pre-flash p69 `21f3b5f2…` (the initial P5-2
image), post-flash prefix `e256b27d…`, unchanged tail `a28b4ea2…`, full p69
`f1954fd4…`; RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…`
unchanged; battery 100%; the host issued no reboot. Evidence is in
`out/gta4l/ram-root-emmc-block-p5b-evidence/` (`flash-record.json`,
`owner-report.txt`, `device-run-record.json`, `build-provenance.json`,
`kernel-string-verification.txt`).

The owner-supplied report is `rc=0 stop=0 blocks=61071360 bs=512
bytes=31268536320 wid=0 tim=0 clk=400000` (`wid=0` is the EXT_CSD 1-bit
encoding). The driver reports `online=1 wp=1 reads=10 blocks=30 fails=0
max=8`; the whole media is `whole=1 base=0 size=31268536320
content=GUID_partition_scheme writable=0`; the media-path read is `lba=1
attempted=1 ok=1 bytes=512 sum=2228245566 base=2228245566 same=1`; the
failure record is all zero. The terminal panic is the intentional report-mode
panic (`Gta4lEmmcBlock.cpp:914`, the shared report panic).

F5 is therefore measured PASS for this reported boot at 1-bit SDR, 400 kHz:
whole-device publication plus a full-length 512-byte media-path read matching
the same-boot baseline. The boot ordinal and cold/warm class were not
supplied and are not inferred; the owner confirmed on 2026-11-05 that the
tablet returned to TWRP. Partition discovery is a separate observation: the
report shows `count=80 expected=84 waited_ms=0 error=0 reported=3`, but
`gta4l_emmc_block_gpt_entries()` derives `expected` from the GPT header
entry-array length, not the number of populated entries, and the reporter
stops waiting on any nonzero count. Section 5.3.38 audits the count against
the validated GPT evidence; it finds no missing-partition or counting defect,
but partition completeness is not claimed from the report alone. F2, 8-bit,
4-bit/HS publication, mount/root switch and F6 remain separate gates.

### 5.3.38 P5-2 partition accounting audit (host-only, 2026-11-05)

The owner supplied no new device evidence for this audit; the tablet was back
in TWRP. The audit re-used the validated recovery GPT capture and the
reporter's registry walk. `docs/emmc-reference.md` records the capture
(`primary-gpt.bin` SHA-256 `98bcfd9a…`, backup `12f80350…`): 84 entries of
128 bytes, 80 nonempty partitions, no overlaps and identical primary/backup
arrays. The `gpt-report-verified.json` record agrees (`partition_count=80`,
`partitions_equal=true`).

- `IOGUIDPartitionScheme::scan()` reads the GPT header, validates the
  entry-array CRC, iterates all 84 slots, skips entries with a null type GUID
  (`isPartitionUsed`) and creates one non-whole `IOMedia` per used entry with
  base and size taken from the entry. The 80 populated entries therefore
  produce 80 child media.
- `Gta4lEmmcBlock::countPartitions()` enumerates `IOMedia` services whose
  provider chain reaches the driver within four levels, skips the whole
  media and counts the rest. The owner report's `count=80` matches the 80
  populated entries, and its first three children (bases 4,194,304,
  8,388,608 and 12,582,912 bytes, each 4,194,304 bytes) match GPT entries
  1-3 exactly. `waited_ms=0` means the first snapshot already saw 80; the
  loop breaks on any nonzero count and takes a final snapshot, which also
  returned 80.
- `expected=84` is the entry-array slot count from the header, not a
  populated-entry expectation. The label remains misleading to a reader but
  the counted value is correct, and the audit found no missing-partition or
  counting defect. Partition completeness is not re-qualified on-device by
  this audit; it rests on the validated primary/backup capture and the
  matching count, and the accepted p5b report and artifacts are unchanged.
  A future reporter that parses the entry array to print a populated count
  would need a new image and a fresh BOOT-only approval; this audit does not
  propose or implement that change.

### 5.3.39 D10 F2 host batch: short/long CMD18 qualification (host-only, 2026-11-05)

The D10 F2 batch replaces the single open-ended `CMD18` experiment in
`gta4l-emmc-mb=1` with the bounded sequence the plan requires. Stage A still
reads the 21 primary GPT array sectors (LBAs 2..22) with `CMD17` and keeps the
exact bytes as the same-boot reference, in addition to the existing per-sector
sums. The MD pass then:

1. issues a two-sector `CMD18` request at LBA 2, stops it explicitly with
   `CMD12`, and byte-compares both sectors against the reference;
2. issues one 21-sector `CMD18` request over LBAs 2..22, stops it explicitly,
   and byte-compares every completed sector;
3. after clean termination, reads block 0 and LBA 2 again with `CMD17` and
   compares them with the pre-transfer reference, proving the card and
   controller still serve single-block reads.

The core keeps a completed-sector count (`multi_completed`) separate from the
software drain ordinal (`multi_block`) and from the validated count. A
transfer failure preserves the first failure record before any stop or reset
can overwrite it, emits the failure report, and does not retry or advance to
another CMD18 request. The probe report adds the `MB` line with per-request
`result/requested/completed/validated/first-divergence/stop-result/
stop-normal/stop-error/stop-response0` groups and the `rec=` recovery group;
the success counters are `step=19`/`log=55`.

The probe simulator now models live FIFO readiness separately from a latched
BRR. It flags any data-port read while a CMD18 transfer is active and the
FIFO is not live-ready (`mb_ready_violations`), and it can hold BRR latched
after a sector while withholding readiness for a configured delay. New
scenarios cover a short-request content divergence, a mid-transfer readiness
stall, an R1-error transfer, a failed recovery read and a diverging recovery
read; the existing mid-transfer data-error, stop-timeout and drain scenarios
were re-based on the short+long order.

Host results: core 179 scenarios/104 mutation controls and probe 168
scenarios/77 mutation controls pass, together with the report-width,
oversized-report, storage-ABI, payload, package, boot-image, inventory and
workflow gates. This is host-only evidence. No candidate image was built,
flashed or booted for this batch, and F2 remains `[U]` until a device boot
shows both requests byte-exact with clean completion and stop handling and
successful post-transfer `CMD17` reads.

### 5.3.40 D10.4a corrective host gate: enforced CMD18 acceptance (host-only, 2026-11-05)

The D10.4a correction closes the acceptance gap in §5.3.39: that batch
snapshotted and byte-validated each CMD18 request but advanced on the
transport return code alone. A short-request content mismatch could therefore
issue the long CMD18, and a long-request or recovery mismatch could reach
`gta4l_emmc_probe_report_probe()` and its hardcoded `rc=0`. The initial host
batch remains recorded above as its own evidence; this section records the
enforced gate.

The probe now treats each request as an explicit barrier:

- `gta4l_emmc_probe_mb_accept()` returns a diagnostic verdict from the
  preserved transport result, the explicit `CMD12` result, the requested
  length, the completed count and the exact-byte validated count, in that
  order. A content-only failure returns `GTA4L_EMMC_PROBE_ERR_CONTENT`
  (`-21`) without overwriting the successful transport result or inventing a
  hardware failure record; a length/accounting failure returns
  `GTA4L_EMMC_PROBE_ERR_LENGTH`, now `-24` so it cannot collide with a
  controller `GTA4L_EMMC_ERR_TIMING` code.
- A failed short request records `mb_failure`/`mb_stage=1` and issues no long
  CMD18 or recovery `CMD17`. A failed long request records stage 2 and issues
  no recovery read. A failed or diverging block-0 recovery read records stage
  3 and issues no LBA-2 read; a failed or diverging LBA-2 read records stage
  4. There is no retry and no cleanup command beyond the existing bounded
  stop.
- The recovery state is initialized before any transfer
  (`result`/`attempted`/`compared`/`same` clear, `same` set only after both
  requests qualify), so a not-run recovery can never read as a successful
  one. LBA 0 is disambiguated from "no divergence" by
  `recovery_div_valid`; a later read cannot overwrite the first recovery
  failure.
- The hardcoded `rc=0` success report is reachable only through one overall
  gate that requires both request verdicts to be clean, both requests to have
  been attempted, `recovery_same=1`, `recovery_result=OK` and both recovery
  comparisons to have run (the attempted/compared bitmask must be 3 for the
  block-0 and LBA-2 reads). Any other state emits the failure report
  below.

The failure report is bounded and keeps the request context:

```
GTA4L-EMMC PROBE FAILED: mb rc=<r> stage=<s> step=<n> mb=<n> mbc=<n>
    att=<short_attempted><long_attempted>
    s=<result/requested/completed/validated/div/stop_result/stop_normal/stop_error/stop_response0>
    l=<same long-request group>
    rec=<result/block0_result/lba2_result/attempted/compared/same/div/div_valid>
    fail=<valid/index/argument/command/error/normal/present/step>
```

The `s=`/`l=` groups report `requested/completed/validated` separately from
the first divergent LBA and keep the stop capture, and `rec=` carries the
transport results and the attempted/compared/not-run flags. The terminal
`fail=` record is the preserved hardware failure when one exists, not a
synthesized content failure.

Coverage added by this correction:

- Scenarios: `probe-md-final-mismatch` (last array sector), `probe-md-swap`
  (reordered sectors), `probe-md-sum-preserve` (bytes changed with an
  unchanged aggregate checksum), `probe-md-short-second-mismatch`,
  `probe-md-recovery-block0-mismatch`, `probe-md-recovery-lba2-fail`,
  `probe-md-accept-seam` (a controlled host seam that calls the production
  acceptance helper with synthetic snapshots, because an otherwise-correct
  simulator cannot produce a successful transport with incomplete sector
  accounting) and `report-width-md-failure` (width form 13).
- Mutation controls: short and long barrier removal, exact-byte comparison
  removal, completed and validated count-gate removal, recovery transport and
  divergence early-stop removal, and an inverted overall gate. The
  first-divergence mutation now targets the validation assignment, and the
  former checksum-substitution mutation is replaced because
  `probe-md-sum-preserve` plus the byte-comparison mutation proves that a
  matching checksum cannot substitute for exact comparison.
- The widest failure form materializes both request groups, the recovery
  group and the terminal record at their field bounds against the real
  2048-byte panic buffer; it is the thirteenth width materialization.

Host results after the correction: core 179 scenarios/104 mutation controls
(unchanged) and probe 176 scenarios/84 mutation controls/13 width-bound
materializations, plus the oversized-report control. This is host-only
evidence. No candidate image was built, flashed or booted for this
correction, no device access occurred, and F2 remains `[U]` until a device
boot shows both requests byte-exact with clean completion and stop handling
and successful post-transfer `CMD17` reads.

### 5.3.41 D10.5 F2 device run: first cold boot passes both CMD18 requests (2026-11-05)

The D10.5 candidate, which is the D10.4a acceptance gate of §5.3.40, was
built, flashed BOOT-only to `p69` and booted by the owner. The owner supplied
the terminal report and clarified that it came from the first cold boot of the
candidate. The raw report, that clarification and the closeout record are in
`out/gta4l/ram-root-emmc-mb-d10.5-closeout-evidence/`
(`owner-report.txt`, `owner-clarification.txt`, `device-run-record.json` and
`evidence-sha256.txt`); the original build and flash artifacts remain in the
immutable `out/gta4l/ram-root-emmc-mb-d10.5-evidence/` tree (94-file
manifest, verified 94/94). The candidate is the recorded dirty-tree state on
device HEAD `1ee1f4f9…` and shared HEAD `0a1cc714…`: prelinked kernel
`bb22f6ca…` (51,187,712 bytes), entry-patched kernel `2d87a4be…`, payload
`93110d8c…` (8/8 contract, exactly one `gta4l-emmc-probe=1` and one
`gta4l-emmc-mb=1`) and BOOT image `bcc93a47…` (18/18 gate).

The report is reproduced here with the identifying CID/CSD and per-sector
content lines omitted; the complete text stays in the ignored evidence tree:

```
GTA4L-EMMC PROBE REPORT: rc=0 mci=10000072 rca=1 cap=61071360 rev=8
  bw=0 ocr=c0ff8080 ext=000009cb hc2=0008 ven=00018a9c dll=6000642c
  dst=00000000 pst=00000000 step=19 log=55 mb=1
GTA4L-EMMC PROBE LSTLBA=61071359 sum=a89b26d5 mbsame=1 drain=0/0/0/0
GTA4L-EMMC PROBE MB clk=0007 s=0/2/2/2/0/0/0003/0000/00000b00
  l=0/21/21/21/0/0/0003/0000/00000b00 rec=0/1/0
```

`step=19`, `log=55` and `mb=1` identify the D10.4a success gate rather than a
stage-A-only pass. Each `s=`/`l=` group is
`result/requested/completed/validated/div/stop_result/stop_normal/stop_error/stop_response0`:
the short request is a clean transport with 2 requested, 2 completed and 2
byte-validated sectors and no divergence, and the long request is a clean
transport with 21/21/21 over LBAs 2..22 and no divergence. Both explicit
`CMD12` stops report result 0, `NORMAL=0x0003` (CC and TC) and
`ERROR=0x0000`, with response word `0x00000b00`. The compact success `rec=`
tuple is `result/same/div` only, so the printed `rec=0/1/0` says the recovery
result was clean, the contents matched and no divergence was seen; the
attempted/compared bitmask is not printed there, but the `rc=0` gate requires
both post-transfer `CMD17` recovery reads (block 0, then LBA 2) to have been
attempted and compared. `mbsame=1` is the aggregate agreement of the
multi-block bytes with the same-boot CMD17 reference. `drain=0/0/0/0` records
that the optional post-bound drain was not used and earns no credit for this
result. `bw=0` and `clk=0007` are the unchanged 1-bit legacy identification
configuration, not a new width or speed claim.

This is one owner-reported cold boot, so **F2 is measured PASS for that boot
at 1-bit SDR, 400 kHz `[C]`**. It is not repeatability, a cold-start
reliability result, general performance or multi-block publication; those
remain `[U]`. The owner confirmed the tablet was on TWRP after the boot, so
the recovery disposition is confirmed and no host reboot was issued. The
original build provenance keeps `owner_boot_pending=true` as its pre-run
snapshot; the closeout record supersedes it without rewriting the original
evidence.

### 5.3.42 D12 offline initialization/reliability audit (host-only, 2026-11-06)

The D12 audit asked whether the existing H8/H9, A2, H15-H17, p5/p5b and D10.5
records demonstrate a host-correctable initialization defect, or whether the
cause remains unknown and needs one discriminating observation. It used only
ignored evidence and the current source; no device access, build, flash,
boot, reboot, commit or push was performed. The matrix, per-hypothesis
reasoning and instrumentation proposal are outside tracked trees at
`out/gta4l/ram-root-emmc-d12-reliability-audit/`
(`failure-matrix.json`, `hypothesis-analysis.md`,
`discriminating-observation.md`, `audit-summary.md`).

The dominant class is CMD3 completion-silent: a plausible R1 status word is
present at the timeout while `CC` is never observed over the full
100,000-iteration (~1.14 s) window, yet the same image can complete CMD3 in
~0.3 ms on another boot. Pre-CMD3 status, enables and `PRESENT_STATE` are
identical in clean and failing H9 boots; the failing sample has
`CMD_INHIBIT` clear, `ERROR=0`, the correct command and argument. A2 attempt 3
boot 1 captures both the post-CMD2 response window and the timeout window, and
`RESPONSE0` changed after CMD2, so the timeout word is not simply the stale
CMD2 window; that makes a fresh card response plausible but does not show
whether `CC` was ever asserted. No command-construction, inhibit, CRC/index,
CMD1-readiness or W1C defect is demonstrated. The CMD17 TC-only and CMD18
data-timeout classes are separate and are not assumed to share the CMD3 cause;
p5's report-client-not-open is a corrected adapter lifecycle defect, not a
transport failure. Cold/warm correlation and any probability model remain
unestablished, and the cross-image counts are not a controlled comparison.

**Disposition: no defect demonstrated; missing discriminating observation.**
The proposed next step is a bounded diagnostic-only extension of the existing
H9 rung: capture `RESPONSE0..3` immediately before CMD3 and a log-spaced
`(iteration, NORMAL, RESPONSE0)` trace through the existing poll, with the
command count, arguments, wait bound, poll step and W1C ordering unchanged.
It allocates no new probe number and adds about 24 bounded MMIO reads over the
~1.14 s window (not claimed observationally free). The audit stops before
implementation, build or flash for an owner decision. F2 remains measured PASS
for the one D10.5 cold boot and F5 remains measured PASS for p5b; neither
verdict is changed by this audit.

### 5.3.43 D12.5 stage-9 no-retry CMD3 timeline (host-only, 2026-11-06)

Stage 9 keeps the `gta4l-emmc-probe=9` selector but now means a **versioned
no-retry identification capture**, not the historical duplicate-CMD3 retry
experiment. Historical H9 images, stop codes 10-13 and their transcripts are
unchanged; this section documents the new meaning and does not reinterpret
them. The duplicate-CMD3 retry seam remains in the portable core for its direct
regression tests but stage 9 sets `cmd3_retry_enabled = 0`, enables
`cmd3_timeline_enabled`, and keeps `identify_only` so a clean run stops after
the TRAN wait, before any EXT_CSD or sector command.

Capture contract and storage. Fixed-capacity state lives in
`struct gta4l_emmc_host`, not a VLA, heap allocation or formatter temporary:
`GTA4L_EMMC_CMD3_TIMELINE_VERSION` 1, 19 scheduled slots plus one reserved
terminal slot (`GTA4L_EMMC_CMD3_TIMELINE_ENTRIES` 20), and entry kinds
scheduled / terminal-success / terminal-timeout / terminal-error. The
zero-based scheduled RESPONSE0 samples are at
`0,1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768,65536,99999`;
a point beyond the actual poll limit is never reached and the poll is never
extended for one. Reinitialization clears the trace fields, validity, indices
and counters without clearing caller configuration.

Sequential order. At the existing post-clear, pre-ARGUMENT/COMMAND point the
code reads `RESPONSE0..3` in register order after the existing status, enable
and present samples; these are four sequential observations, not an atomic
snapshot. At a scheduled iteration the existing `ERROR`/`NORMAL` values are
reused and one `RESPONSE0` read is added; the completion decision still uses
the original status values, never response content. The counter delta is
sampled after that read and is a one-sided bound, not an atomic
status/response timestamp. The reserved terminal slot reuses the existing
success response read, the fresh timeout status/raw-response capture or the
latched failure capture, and it is only marked valid when those source fields
exist. The CMD3 transport result is stored independently of R1 validation and
the later identification result, so a timeout stays a timeout even if the
fresh terminal NORMAL sample contains late CC.

Bounds. One diagnostic CMD3 adds at most 4 pre-command response words plus 19
scheduled RESPONSE0 reads (23 32-bit response reads) and at most 20 counter
reads (19 scheduled plus one terminal). It adds zero status reads, no new
reads on disabled paths or unrelated commands, and no extra sleep, reset,
retry, power operation or command. The added cost is not claimed to be
observationally free and no percentage or microsecond overhead is asserted.

Report and stops. `GTA4L-EMMC H9T REPORT ver=1` is one bounded panic: header
(`rc stop sub att tr r1 retry step log hz`), `C3P` (pre-command window and
response), `C3L` (poll history), `C3T` (timeout capture), `TRC` (count,
overflow) and a fixed 20-line `T00..T19` trace, then the terminal `FAIL`
record. Unused slots print kind `?` and valid 0 so the length is bounded and
nothing is invented. The clean materialization is 1658 bytes and the widest
field-wise synthetic form stays below the 2048-byte panic budget including
the terminator. Stop codes are 0 (clean identification), 14 (CMD3 not clean or
never submitted) and 15 (CMD3 clean, later identification failed); historical
retry stops 10-13 are unreachable here. The formatter performs no MMIO and
does not touch an unverified clock window.

Interpretation limits. A RESPONSE0 change after submission bounds a register
update to an interval and does not prove a fresh card response or a lost
hardware CC. An unchanged sampled window is inconclusive: it cannot separate
no response, an identical new response, an intermediate change missed between
samples and a retained/controller-produced value. A first change only in the
terminal capture is bounded between the last scheduled sample and the
terminal read, not an arrival time. A late terminal CC after an exhausted poll
remains a timeout and is not promoted to success or replayed. NORMAL is
sampled every ordinary iteration, so a CC pulse between reads is unobservable.
Full pre/terminal windows are also sequential. These limits supersede the
older D12 sketch's duplicate-CMD3 retry, overhead-percentage and stronger
causal readings; the D12 audit artifacts remain immutable historical evidence.

Host-only status. Host suites pass core 187 scenarios/115 mutation controls
and probe 196 scenarios/100 mutation controls with 13 width-bound
materializations and the oversized-report control. Stage-9 scenarios are
`probe-h9-map-fail`, `probe-h9-clock-fail`, `probe-h9-cmd1-fail`,
`probe-h9-cmd2-fail`, `probe-h9-clean`, `probe-h9-timeout`,
`probe-h9-status`, `probe-h9-error`, `probe-h9-later-fail`,
`probe-h9-cc-between`, `probe-h9-cc-at-sample`, `probe-h9-update-no-cc`,
`probe-h9-no-update`, `probe-h9-preexisting-r1`, `probe-h9-identical`,
`probe-h9-multi-change`, `probe-h9-last-sample`, `probe-h9-terminal-only`,
`probe-h9-late-cc`, `probe-h9-index-error`, `probe-h9-simultaneous`,
`probe-h9-error-in-sample`, `probe-h9-error-after-read`,
`probe-h9-inhibit` and `probe-h9-counter-stall`; the core keeps the
historical direct retry scenarios and adds `cmd3-timeline-clean`,
`cmd3-timeline-timeout`, `cmd3-timeline-stale-cc`, `cmd3-timeline-mixed`,
`cmd3-timeline-reinit`, `cmd3-timeline-short-budget`,
`cmd3-timeline-store-boundary` and `cmd3-timeline-saturation`. The probe suite
also carries the stage-9 mutation gates for a scheduled response read, the
last schedule point, a scheduled store, the terminal slot, R1 content, the
timeout terminal entry, the final error store, the report version, the
pre-CMD3 response, an extra response read, an extra status read, a
later-command trace clear, the counter frequency, an invented valid unused
slot, the overflow field and a late-CC promotion; the core adds gates for
retry-coupled capture, an unguarded capture-disabled read, an extended poll,
a forced capacity overflow, a capacity-guard removal and a pre-capture clear
removal. No kernel/archive
candidate was built, no image was packaged, flashed or booted, no device access
occurred, and no commit or push was made. F2 and p5b F5 keep their measured
verdicts; reliability remains unresolved pending a separately approved
observation.

### 5.3.44 D12.5 stage-9 observation closeout and H9T kind-column finding (host-only, 2026-11-06)

The single authorized D12.5 stage-9 observation was consumed on the BOOT-only
candidate `9e0317909530f7d59eeb7d8d80dfa9f0a8ed354d2fd0bb5165c646c6b17f6bc2`
(85,688,320 bytes, 18/18 gate, p69 only), built from device HEAD `1ee1f4f9`
and shared HEAD `0a1cc714` with the dirty-tree diff bound by
`device-unstaged-diff.sha256`. The owner classified the boot as cold, supplied
the report and a photograph, and confirmed return to TWRP. No host-issued
reboot, reflash between boots or further attempt occurred; the authorization is
exhausted. This is a host-only closeout and does not reinterpret the D12.5 host
batch or the D10.5/p5b verdicts. The dated analysis is
`docs/emmc-d12.5-closeout-2026-11-06.md`.

Report decode. Header `ver=1 rc=0 stop=0 sub=1 att=1 tr=0 r1=0 retry=0/0
step=13 log=4 hz=19200000`: CMD3 was submitted once with the retry seam
disabled. The `C3L` history is valid with 25 iterations, first NORMAL `0000`,
last NORMAL `0001` and zero-based terminal iteration 24. The reserved terminal
slot 19 records kind SUCCESS, NORMAL `0001`, ERROR `0000`, RESPONSE0
`00000500`, valid 1 and ticks `0x14f0`; `C3T v=0` means no timeout capture.
`TRC n=6 ov=0` counts scheduled entries at iterations 0, 1, 2, 4, 8 and 16;
slots 6-18 are unused (`?`, valid 0) and slot 19 is reserved. The zero-validity
`FAIL` record is the intentional report stop, not a recorded hardware failure.

Limits. The six scheduled samples are the retained long-response window's
first word (the CMD2 CID prefix) read while polling before completion; they are
not CMD3 responses. The terminal sample is a later sequential read. Comparing
it with the last scheduled sample bounds a register change to an interval; it
does not prove a fresh card response, an exact arrival time or a card-state
transition. The driver applies its R1 error mask to the terminal RESPONSE0 and
reported no masked error (`r1=0`); it assigns the RCA in the CMD3 argument and
does not parse an RCA from the response. No timing estimate is derived from the
counter ticks.

Disposition. Clean instrumented identification / CMD3 completion-silent
non-reproduction. Initialization reliability remains unresolved: one boot does
not qualify repeatability, cold-start reliability or a failure rate. F2 remains
measured PASS for the one D10.5 cold boot and p5b F5 keeps its measured verdict.

Missing kind column. `package/Gta4lEmmcProbe.c:1488` emits each trace line with
`GTA4L-EMMC H9T T%02u %c %u %04x %04x %08x %u %08x%08x\n`. The on-screen report
and the typed transcript omit the `%c` kind (`S` scheduled, `?` unused, `C`
terminal success, `T` terminal timeout, `E` terminal error); `Txx` is the slot
index. The photograph confirms the omission is not a transcription artifact.
The panic is rendered by the freestanding formatter in the sibling UniXNU
checkout `scripts/xnu-panic-screen.c` (SHA-256
`54b950a7d83c30f7720d30551c6d5f10cea5e40dcccc5adb2b4c70a98240a68b`, identical
to the build-tree copy). Its `format()` supports `%s`, `%x`/`%X`, `%u`/`%d`
and `%p` but has no `%c` branch; it still consumes one variadic slot at
`u64 v = args[used++]`, so the kind is dropped while the following fields stay
aligned. The probe harness's `panic()` calls libc `vsnprintf`
(`scripts/sim-gta4l-emmc-probe/harness.c:1966`), which handles `%c`, so the
existing host suite cannot observe the device behavior. The fetched XNU kernel
source handles `%c` (`out/feeds/xnu-b7545ced/osfmk/kern/printf.c:459`),
confirming the divergence is in the panic-screen path rather than in the report
format or the probe.

`scripts/test-gta4l-panic-format.py` extracts the pinned `put`, `number` and
`format` bodies, compiles them with a memory sink and runs the exact H9T format
with synthetic values. It checks the production output (double space, no kind),
the libc reference (kind present), an unaffected control without `%c` and a
textual mutation that adds a `%c` branch and restores the kind. It pins the
formatter SHA-256 and the probe format string, uses no private response word
and does not read device state. The test passes.

This is a finding, not a fix. A formatter fix needs an exact invariant (one
`%c` consumes one argument and emits that byte), a minimal seam, the
baseline-failing reproducer above and a mutation that restores failure, then a
separate implementation decision. The missing kind column limits trace-format
acceptance; it does not erase the independently visible clean header,
completion and trace-field evidence. No formatter or driver change, candidate
build, flash, boot, device access, commit or push was made in this closeout.

Validation (host-only, from `src/UniXNU-gta4l/`, `LC_ALL=C`,
`/usr/bin/python3.11 -B`). The 24-script `scripts/test-*.py` battery passed 23
scripts, including the new panic-format test, with these exact counts: core 187
scenarios/115 mutation controls, probe 196/100 with 13 width-bound
materializations and one oversized-report control, block core 23/18.
`scripts/test-gta4l-storage-abi.py` failed with `Derived candidate does not
match the plan`; the shared `kernel.macho` is the D12.5 prelinked
`b1871d40ec0c6b5021ca6dd14a83951650255b40824399483679116cf6b4eef3`, while the
storage-ABI derive and provenance remain pinned to the p5b kernel `2f8238b9`.
That is a pre-existing artifact-staleness condition, not a regression from this
batch; the storage-ABI artifacts were not regenerated. `scripts/check-gta4l.py`
passes, `porting.toml`/`board.toml`/`feeds.toml` parse with `tomllib` (13
porting areas), and `git diff --check` is clean.

Evidence. Private raw evidence is under the ignored
`out/gta4l/ram-root-emmc-d12.5-cmd3-timeline-evidence/observation-1/` (exact
owner transcript, original photograph bytes, structured record and
interpretation), covered by a new closeout SHA-256 manifest. The original
40-file sealed manifest is unchanged. Raw response windows and CID fragments
remain private and are not repeated here.

### 5.3.45 H9T kind-column panic formatter fix (host-only, 2026-11-06)

Follow-up to §5.3.44. The H9T trace format at `package/Gta4lEmmcProbe.c:1488`
includes `%c`, but the shared panic formatter in the sibling UniXNU checkout
(`scripts/xnu-panic-screen.c`) consumed one variadic slot for every conversion
and had no `%c` branch, so the kind byte was dropped while the following fields
stayed aligned. The fix adds one branch after the slot is consumed:

```c
    if (c == 'c') {
      put (g, (u8)v);
    } else if (c == 's') {
```

The invariant is that a plain `%c` consumes exactly one slot and emits its low
byte; no other conversion, argument order or report length bound changes. The
file comment now lists `%c`. The old formatter hash was
`54b950a7d83c30f7720d30551c6d5f10cea5e40dcccc5adb2b4c70a98240a68b`. The
`%c` branch alone produced a 2615-byte (`0xa37`) linked image at the D12.5
panic cave, 7 bytes past `PANIC_CAVE_END`; the `--panic-screen --no-reset`
recipe has only the 2608-byte (`0xa30`) stage-2-to-end padding. The
`PANIC_TEXT` log path is now compiled only when `PANIC_TEXT` is nonzero, which
is dead code on the D12.5 kernel (it has no `_boot_progress_panic_text`). The
guarded formatter is 2519 bytes (`0x9d7`) with 89 bytes spare, and the file
hash is
`8b71ed5a1fadbf3bea5839ce4b614f5b6e0c998e37131e60c1d37b3f2686ba95`.
Kernels that define `PANIC_TEXT` keep the previous log behavior.

`scripts/test-gta4l-panic-format.py` was rewritten from a defect reproducer
into a regression test. It pins the fixed hash and the probe format, compares
exact output with libc for kinds `S`, `?`, `C`, `T` and `E`, an unaffected
control and an adjacent `%c`/`%%` format, and requires a branch-removal
mutation to reproduce the old `T19  7` and `AB7C%DE1234` output. It also
compiles the full formatter for `aarch64-none-elf` with the production flags
and the D12.5 cave/symbol addresses, rejects writable data, requires
`panic_screen` first in `.text`, and requires at least 32 bytes spare. The
pre-fix source fails with `the %c branch is missing; the pre-fix formatter
drops the H9T kind`; the unguarded `%c` source (hash `3915f34f...`) passes the
behavior phase but fails the fit phase with `panic screen is 0xa37 bytes ...
only -7 bytes before ...`; the guarded source prints `FIT: panic screen 0x9d7
bytes ... 89 bytes spare`.

Validation (host-only, from `src/UniXNU-gta4l/`, `LC_ALL=C`,
`/usr/bin/python3.11 -B`). The 24-script battery passed 23 scripts, including
the fixed panic-format test, with core 187/115, probe 196/100 and block 23/18.
`scripts/test-gta4l-storage-abi.py` still fails with `Derived candidate does
not match the plan` because the shared `kernel.macho` is the D12.5 prelinked
`b1871d40` while the storage-ABI derive and provenance remain pinned to the
p5b kernel `2f8238b9`; the artifacts were not regenerated. The porting-record
schema check (`scripts/porting-record.py check --device ../UniXNU-gta4l`),
`scripts/check-gta4l.py`, the TOML parse (13 areas), the baseline fixture
recipe and `git diff --check` in both repositories pass.

Evidence is under the ignored `out/gta4l/ram-root-emmc-panic-format-fix/`
(pre-fix/fixed formatter copies, diff, pre-fix failure log, full test output,
host checks, source hashes and a new manifest). The D12.5 closeout manifest
remains an unchanged historical snapshot. No candidate was built, flashed or
booted, no device access occurred, and the D12.5 image still carries the old
formatter. A new candidate and on-device observation need fresh explicit
approval; reliability remains unresolved, F2 and p5b F5 are unchanged and D13
is separate. No commit or push was made.

### 5.3.46 D12.6 panic-format candidate observation (host-only closeout, 2026-11-06)

Follow-up to §5.3.44 and §5.3.45. The owner supplied one H9T report from a
D12.6 candidate boot and confirmed a cold start, no intervening attempts and a
return to TWRP. The candidate was BOOT image `7c76c7eb` (18/18 gate, payload
`e384cc06`, packaged kernel `12385bc7`), flashed BOOT/p69 only with the recorded
prefix match, protected partitions matching the pre-flash values and a saved
rollback. No exact timestamp or photo bytes were supplied.

The header is `ver=1 rc=0 stop=0 sub=1 att=1 tr=0 r1=0 retry=0/0 step=13 log=4
hz=19200000`. CMD3 was submitted once with no retry; the 25-iteration poll
ended at zero-based iteration 24 with terminal kind `C`, NORMAL `0001`, ERROR
`0000`, RESPONSE0 `00000500`, valid 1 and ticks `0x14f2`; no timeout capture
exists. `TRC n=6 ov=0` has scheduled entries at 0/1/2/4/8/16, unused slots
6-18 and reserved slot 19; the six scheduled words are private. FAIL validity
is zero, so the panic is the intentional report stop.

The kind column renders as expected (`S`, `?`, `C`), accepting the §5.3.45
formatter correction for this report; it does not qualify the framebuffer path,
other conversions or future candidates. The scheduled samples remain
interval-bounded polling reads, not CMD3 responses or exact arrival times, and
the terminal ticks are not a command-latency measurement. Disposition: clean
instrumented identification / CMD3 non-reproduction; initialization reliability
remains unresolved and F2/p5b F5 are unchanged. Private evidence is under
`out/gta4l/ram-root-emmc-d12.6-panic-format-evidence/observation-1/`; see
`docs/emmc-d12.6-panic-format-observation-2026-11-06.md`. No device access,
build, flash, boot, commit or push in this batch.

### 5.3.47 D13 readiness: grounded adapter/CMD18 contract (host-only, 2026-11-06)

Follow-up to §5.3.42–§5.3.46. This batch performs the plan's host-only D13.1
grounding and D13.2 scenario enumeration; it does not implement the proposed
backend, build a candidate, launch QEMU, access the device or commit. Source
identity is device HEAD `1ee1f4f9143b` with the existing closeout working tree
and shared HEAD `0a1cc71407a3`. The dated record is
`docs/emmc-d13-readiness-2026-11-06.md`; it is design and inspection only.

The inspected seam is the current one-sector adapter, not the probe:
`doAsyncReadWrite` rejects before acceptance and without a completion (null
arguments, zero/out-of-range blocks, the 64-block ceiling, a short descriptor,
write or non-read direction, an absent gate), takes the gate for an accepted
request, clears the request buffer, completes once after unlocking, and sends
the media-state notification last (`package/Gta4lEmmcBlock.cpp:501-604`). The
portable core loops one CMD17 sector through a 512-byte bounce, counts a block
only after a full copy and marks the media offline on the first read or copy
failure (`package/Gta4lEmmcBlockCore.c:50-148`). The diagnostic
`gta4l_emmc_read_multi` writes data directly into its caller's buffer before
the mandatory CMD12 outcome is known, returns a collapsed result and preserves
the completed/failing ordinal and stop result only in separate `multi_*`
fields (`package/Gta4lEmmc.c:3658-3971`); the probe's static references and
acceptance gates are diagnostic-only and must not leak into production reads.

The proposed first contract keeps the accepted 64-sector/32-KiB/one-segment
request shape and the `gta4l-emmc-block=1` singleton backend, adds CMD17 for a
singleton and two-sector CMD18 chunks plus one CMD17 tail, requires an optional
multi-read callback that exposes transport, requested/completed sectors and the
stop outcome separately, and delivers a chunk only after exact count and a
clean mandatory CMD12. It uses a private bounded bounce (not a VLA or large
kernel-stack allocation), preserves the first failure, fails closed/offline,
stops issuing suffix chunks, does not retry or fall back, and keeps one
completion per accepted request with no callback under the gate. The new
selector value remains open and must not silently change `=1`. No mount, root
switch, write path, DMA, CQE, ICE or width/speed change is part of it.

The timing derivation is a named open decision: with 1 s command and data
bounds, a 0.5 s drain and up to 2 s of stop waits, a pathologically slow chunk
can hold the gate for roughly 4–4.5 s; 32 two-sector chunks can therefore
exceed any interactive expectation, while a failed chunk is bounded before the
request stops. The implementation decision must choose explicitly between
accepting/documenting the 64-sector synchronous bound, using a larger bounded
chunk, or changing the external ceiling. The first hardware discriminator stays
small: a two-sector GPT-array read plus an odd-length split/tail, not a
64-sector sweep.

The host scenario inventory to preserve and extend is core 23 scenarios/18
mutation controls, adapter 32 scenarios, and diagnostic 18 `multi-*`/12
multi-block mutations. The readiness record enumerates the required transfer,
rejection, failure/partial-delivery, lifetime/completion and runtime-negative
cases, plus the reporting and counter constraints. The batch re-ran the TOML
parse, `porting-record.py check`, `check-gta4l.py`, the eMMC evidence-count
test, `git diff --check` and the privacy scan; `scripts/test-gta4l-storage-abi.py`
still fails closed on the stale p5b candidate/provenance and was not
regenerated. The N2 owner decision selected host implementation, and the
follow-up N3 batch is recorded in §5.3.48.

### 5.3.48 D13 N3 chunked report-mode host implementation (host-only, 2026-11-06)

Follow-up to §5.3.47. This batch implements the D13 contract on the host only;
it does not build a candidate or archive, launch QEMU, access the device,
flash, boot, commit or push. The measured p5b/F5 rung and the F2 verdict are
unchanged. Source identity remains device HEAD `1ee1f4f9143b` with the
existing dirty tree and shared HEAD `0a1cc71407a3`; raw host evidence is under
`out/gta4l/ram-root-emmc-d13-n3-chunk-evidence/`.

**Selector and dispatch.** `gta4l-emmc-block=3` is the new report-chunked
mode (`GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED`,
`GTA4L_EMMC_BLOCK_SELECT_REPORT_CHUNKED`). `=1` remains the CMD17 report mode,
`=2` remains refused and a probe token, another SDHCI diagnostic or a duplicate
block token is still refused before MMIO. The adapter sets `chunkedMode` from
the selection and passes `read_multi`, `chunk_buffer` and `chunk_buffer_bytes`
only in mode 3; `initializeHardware()` runs after that flag is set.

**Portable contract.** The helper has an optional
`read_multi(context, block, count, buffer, result)` callback. Its
`gta4l_emmc_block_multi_result` carries `transport_result`,
`requested_blocks`, `completed_blocks`, `stop_attempted` and `stop_result`.
A singleton and an odd tail use CMD17; multi-block requests use two-sector
CMD18 chunks (`GTA4L_EMMC_BLOCK_CHUNK_BLOCKS=2`,
`GTA4L_EMMC_BLOCK_CHUNK_BYTES=1024`) when the callback is present, otherwise
the old one-block loop. A chunk is copied out only when the callback return
code is zero, `transport_result` is zero, the requested and completed counts
both equal the chunk size, the mandatory stop was attempted and
`stop_result` is zero. Earlier qualified chunks may contribute partial bytes;
a failed chunk contributes none, clears `online`, latches the first failure,
counts `chunk_failures` (and `stop_failures` only when a stop was attempted and
failed), and stops suffix I/O. There is no retry, CMD17 fallback or recovery
in-request.

**Adapter and transport.** The adapter owns a 1024-byte `chunk` member and
maps `readMultiCallback()` to `gta4l_emmc_read_multi_result()`. That wrapper
snapshots the existing `multi_completed`, `multi_stop_valid` and
`multi_stop_result` fields and returns the unchanged collapsed
`gta4l_emmc_read_multi()` result; diagnostic callers and the probe are
unchanged. The adapter still takes the gate for an accepted request, clears
the request buffer before the callback, completes exactly once outside the
lock, and sends the media-state notification last.

**Report.** `fillDriverReport()` (under the gate) and
`fillWatchdogReport()` (without it) copy `mode=3` for chunked mode and the
existing counters plus `requested_blocks`, `cmd17_reads`, `cmd18_chunks`,
`cmd18_blocks`, `chunk_failures`, `stop_attempts`, `stop_failures` and
`last_stop_result`. The formatter emits `mode=` on the header line and a
`GTA4L-EMMC P5 XFER:` line with `req`, `c17`, `c18`, `c18b`, `cfail`, `stops`,
`sfail` and `last_stop` inside the 2048-byte buffer; an overflow fails closed
rather than truncating. The watchdog snapshot still does not take the gate, so
new fields do not assume a locked read.

**Host results.** `scripts/test-gta4l-emmc-block.py` passes 48 core scenarios
with 38 mutation controls, 55 adapter scenarios against the host IOKit seam
and 3 adapter-source mutations (double completion, callback under lock and a
stale request buffer). The chunk scenarios cover singleton, pair, odd split,
exact boundary, 21-sector and 64-sector requests, first/middle/last failure,
short count, data-phase result, stop failure, missing stop, short copy,
overlong copy, missing/small bounce, stale bounce and offline state; the
adapter scenarios add mode-3 selection, conflict, read/tail/max, failure,
partial delivery, stop failure, short-count, watchdog snapshot/terminal-race
and the controlled qualification reporter cases in §5.3.49. The diagnostic
payload suite passes with `emmc-block-3` -> `gta4l-emmc-block=3`; the builder,
checker, packager and inventory accept exactly one block token, refuse
duplicates and mixed probe/block tokens, and still refuse the deferred `=2`.
The unchanged core and probe suites remain 187/115 and 196/100.

**Limits and next gate.** The known synchronous gate-hold bound is unchanged:
a pathologically slow two-sector chunk can consume roughly 4–4.5 s, so a
64-sector request can hold the gate for roughly 128–144 s; the 30,000 ms
report watchdog covers the report thread, not general I/O, and
`GTA4L_EMMC_BLOCK_READ_WAIT_MS` is still unused. There is no offline recovery
path. This batch is not a hardware result or a reliability disposition; the
first hardware discriminator remains a small two-sector GPT-array read plus an
odd-length split/tail. N4 requires separate candidate-build and ABI/QEMU
approval, and the stale storage-ABI candidate/provenance still needs its own
requalification path.

### 5.3.49 N3 corrective host batch (host-only, 2026-11-06)

**Stop-failure accounting.** The portable multi-result callback contract now
distinguishes refusal from execution: a nonzero return means the callback
refused the chunk and no execution record exists; zero means `result` is
always filled, including a failed data phase or stop. The helper increments
`stop_attempts` and `stop_failures` before any return branch, so a stop
failure is visible even when the collapsed transport result is also nonzero,
and it never copies chunk bytes before the transport result qualifies.

**Qualification contract.** `readMultiCallback()` maps the diagnostic
`gta4l_emmc_multi_result` transport/requested/completed/stop fields into
`gta4l_emmc_block_multi_result`; only a pre-command argument refusal keeps a
nonzero return. `copyOutCallback()` compares every delivered byte against the
reference capture while a qualification window is active.

**Mode-3 controlled reporter.** In chunked mode `runReport()` reads a
three-block reference at LBA 2 before opening the media, then performs one
intentional two-block and one three-block `IOMedia` read at LBA 2 through the
adapter. The two-block read exercises one CMD18/CMD12 chunk; the three-block
read exercises one CMD18 chunk plus the CMD17 odd tail. A coherent capture
records exact byte matches/mismatch offset, CMD17/CMD18 counts, requested
chunk blocks, stop attempts/failures and the last stop result. Reason codes:
1 reference unavailable, 2 transport/count/copy/lifetime failure, 3 byte
mismatch, 4 command-shape mismatch, 5 ambiguous attribution. A second request
with the controlled block/count/length identity marks the window ambiguous, so
an incidental scan cannot be credited to the intentional read; ordinary
requests are otherwise ignored by the capture. The reporter never holds the
controller gate across `media->read()`, which re-enters the adapter.

**Terminal ownership and watchdog.** `claimTerminal()` gives exactly one
terminal owner. `panicWithReport()` claims first, takes a coherent snapshot,
then sets `reportDone` and formats; the watchdog uses a bounded try-lock
snapshot (`P5 SNAP: locked=0/1`) with `rc=stop=16` and defers while the
terminal owner is active. A watchdog thread-start failure now reports
`stop=16 stage=17` instead of returning success.

**Host results.** The block suite passes 48 core scenarios with 38 mutation
controls, 55 adapter scenarios and 3 adapter-source mutations. New adapter
controls cover reporter success, incidental attribution, a two-block mismatch,
a sum-preserving byte mismatch (`+1`/`-31` at adjacent bytes preserves the
rolling 31-based sum, so only an exact byte comparison detects it), an odd-tail
mismatch, wrong command shape, an unavailable reference, ambiguous attribution
and a stop failure. `read-one` also requires the request buffer to be cleared;
the adapter mutations must fail on double completion, callback-under-lock and
a stale request buffer. The watchdog controls cover start failure, a blocked
try-lock snapshot and terminal-race suppression of a second panic. Host
acceptance is complete for the intentional mode-3 path; it is not a hardware,
initialization-reliability or general-I/O qualification. Evidence:
`out/gta4l/ram-root-emmc-n3-corrective-evidence/`.

### 5.3.50 D13 N4 candidate and N5 chunked hardware observation (2026-11-07)

Follow-up to §5.3.48–§5.3.49. N4 built the approved `gta4l-emmc-block=3`
integration candidate through the archive/prelink/payload/BOOT flow into
`out/gta4l/ram-root-emmc-n4-candidate-evidence/`: prelinked kernel
`f37f8404…` (51,224,576 bytes), packaged kernel `2e6d91a0…`, linked archive
`e0f022ac…` (460,352 bytes, 190 method slots, 181 kernel bindings,
`runtime_qualified=false`), payload `4a400ca8…` (83,886,080 bytes, 9/9
contract), BOOT image `54cdbfb6…` (85,688,320 bytes, 18/18 gate, only the
ramdisk differs from the `ce4d76e8…` baseline) and a candidate-specific
storage-ABI audit (`72d4df59…` interface audit, `672aa25b…` snapshot). N4 did
not flash, boot or authorize a boot; its closeout is `n4-closeout.txt` and its
read-only proposal is `n5-hardware-proposal.txt` in that directory.

N5 flashed that BOOT image to `/dev/block/mmcblk0p69` only, under the standing
backup/gate policy. Pre-flash p69 `b19c59b8…` was saved as
`boot-before-n5.img.xz` (`8b37f41c…`, decompressing to `b19c59b8…`); the
post-flash p69 is `22bea393…`, the image-length prefix is `54cdbfb6…`, the
tail is `a28b4ea2…` and RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs
`3a265d58…` are unchanged. No host-issued reboot, retry or reflash occurred.
The one owner-controlled cold boot returned to TWRP and produced one
`GTA4L-EMMC P5 REPORT` with `rc=0 stop=0 mode=3 blocks=61071360 bs=512
bytes=31268536320 wid=0 tim=0 clk=400000`, `online=1 wp=1 reads=12 blocks=35
fails=0 max=8`, `req=35 c17=9 c18=13 c18b=26 cfail=0 stops=13 sfail=0
last_stop=0`, a read-only whole media (`whole=1 base=0 size=31268536320
content=GUID_partition_scheme writable=0`) and an LBA 1 read matching the
same-boot baseline (`sum=2228245566 same=1`).

The two controlled IOMedia requests both pass for this one cold boot at
1-bit SDR, 400 kHz. The two-block LBA 2 read delivered 1,024 exact bytes
through one two-sector CMD18 chunk and one clean CMD12 (`two_att=1 two_ok=1
two_reason=0 two_bytes=1024 two_mismatch=0 two_c18=1 two_c17=0 two_stops=1
two_sfail=0`). The three-block LBA 2 read delivered 1,536 exact bytes through
one two-sector CMD18 chunk, one clean CMD12 and one CMD17 odd tail (`att=1
ok=1 reason=0 bytes=1536 mismatch=0 c18=1 c17=1 stops=1 sfail=0`). The raw
baseline and three-block reference reads are direct transport reads outside
the helper counters; the per-request QUAL fields bind the intentional reads.
The final all-zero failure record and the report panic at
`Gta4lEmmcBlock.cpp:1148` are the intentional report stop.

Partition discovery is a separate observation:
`count=22 expected=84 waited_ms=0 error=0 reported=3`. `expected` is the
same-boot GPT header entry-array slot count, not a populated count; the
validated recovery GPT has 80 populated entries. `countPartitions()` stops
waiting at the first nonzero count and then takes one immediate snapshot, and
`IOGUIDPartitionScheme::start()` publishes its scan result one `IOMedia` at a
time. A count concurrent with that loop can observe a prefix; a failed scan
would publish nothing. The presence of 22 non-whole media proves the scan
succeeded and publication had begun, but the report cannot prove the final
count is 22. The most consistent host-side explanation is a mid-publication
snapshot, not a lost-partition or read failure; partition completeness is not
claimed. The earlier p5b boot reported 80 with the same counting logic, so the
snapshot is timing-dependent. No source or instrument change is made in this
batch.

N5 acceptance is limited to the two actual IOMedia requests measured. It does
not qualify arbitrary LBAs or sizes, the 64-sector ceiling, repeatability,
writable media, a mount/root switch, DMA/CQE/ICE, wider/faster modes or
initialization reliability. The one-boot budget is consumed; any further
observation or partition-snapshot instrument needs a fresh owner decision.
Evidence: `out/gta4l/ram-root-emmc-n5-hardware-evidence/` (flash board and
`observation-1/`) and `docs/emmc-n5-integration-observation-2026-11-07.md`.

### 5.3.51 N6 reliability observation: second clean CMD3 control (2026-11-07)

Follow-up to §5.3.46 and §5.3.50. N6 re-flashed the unchanged D12.6 stage-9
candidate BOOT-only to `/dev/block/mmcblk0p69`: image `7c76c7eb…` (85,688,320
bytes, 18/18 gate, payload `e384cc06…`, packaged kernel `12385bc7…`,
`gta4l-emmc-probe=9`, arguments `rd=md0 sheng-pmgr=2`). The pre-flash p69
(the N5 image `22bea393…`) was saved as `boot-before-n6.img.xz` (`5eaaa423…`,
decompressing to `22bea393…`). The post-flash full p69 is `b19c59b8…`, the
image-length prefix matches `7c76c7eb…`, the tail is `a28b4ea2…`, and
RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` are unchanged.
Battery was 100%; no host-issued reboot or write outside p69 occurred. No
source, archive, kernel or payload was rebuilt.

One owner-controlled cold boot returned to TWRP and produced one
`GTA4L-EMMC H9T REPORT ver=1` with `rc=0 stop=0 sub=1 att=1 tr=0 r1=0
retry=0/0 step=13 log=4 hz=19200000`. CMD3 was submitted once with retry
disabled; the 25-iteration poll ended at zero-based iteration 24 with terminal
kind `C`, NORMAL `0001`, ERROR `0000`, RESPONSE0 `00000500`, valid 1 and ticks
`0x14f2` (about 279.27 us at 19.2 MHz). No timeout capture exists (`C3T v=0`).
`TRC n=6 ov=0` records scheduled entries at iterations 0/1/2/4/8/16, unused
slots 6-18 and the reserved terminal slot 19. The all-zero `FAIL` record and
the panic at the report path are the intentional report stop. The typed C3L
poll ticks show 15 hex digits (`0000000000014e4`); the source format is two
eight-digit halves and the prior D12.6 report of the same candidate shows
`00000000000014e4`, so the missing leading zero is a transcription omission.
Normalized, the poll ticks are `0x14e4` (about 278.54 us); poll and terminal
timings are distinct sequential measurements, not a card-response latency.

The prior D12.6 cold boot of the same candidate reported the same C3L value
and the same terminal ticks; the six scheduled samples here are each one tick
higher. This is deterministic-looking timing within a tick, not a rate or a
latency bound. **Classification: clean control / non-reproduction.** N6 is a
second clean cold-boot control of the same candidate, separated by other
ladder work and a reflash; it is not a continuous two-boot trial, a
failure-rate estimate, a fix or proof of reliable initialization. Scheduled
samples remain interval-bounded and are not CMD3 responses; the terminal read
does not prove a fresh response, an exact arrival time or a card-state
transition. F2, p5b/F5 and N5 verdicts are unchanged; initialization
reliability remains unresolved. The N6 boot budget is consumed; any further
observation or instrument change needs a fresh owner decision. Evidence:
`out/gta4l/ram-root-emmc-n6-reliability-evidence/` and
`docs/emmc-n6-reliability-observation-2026-11-07.md`.

### 5.3.52 N8 bounded partition-observation design (host-only, 2026-11-07)

Follow-up to §5.3.50–§5.3.51. N8.1 designs a bounded partition-publication
observation for the existing reporter; it adds no code, build, QEMU, device
access, flash, boot, commit or push. The current reporter's `countPartitions()`
stops at the first nonzero count and then takes one immediate re-sample, and
`expected` is the GPT header entry-array slot count (84), not a populated
count. N5's `count=22 expected=84 waited_ms=0` is therefore an unsynchronized
mid-publication snapshot, not a completeness verdict.

The design chooses an opt-in `gta4l-emmc-partobs=1` token orthogonal to
`gta4l-emmc-block=1|3`; when the token is absent, the mode-1/mode-3 reporter
is unchanged. It proposes a 4,000 ms observation deadline, a 50 ms sampling
interval and a 250 ms count-unchanged window, all in the report thread and
never under the controller gate. The `PARTOBS` line reports the sample count,
first/final counts and nominal times, first-nonzero, changes, stability,
deadline, timeout, none, error, the 84 slot count and an explicit `complete=0`.
A stable count is not a completion barrier, and no completeness claim is made
without an independent validated GPT-to-`IOMedia` range/count comparison.
Iterator failure keeps `stop=15`; a deadline or a zero count is not a failure.
The worst-case report path is 24 s against the 30 s watchdog. Host scenarios
cover incremental, delayed, stable-partial, late-growth, no-partition,
changing, deadline, iterator-error, cancellation, watchdog, legacy, width and
no-gate-sleep cases, with mutations for the old first-nonzero behavior, false
completeness, slot-count substitution, skipped cancellation/error/deadline
checks, list-order stability, gate-held sleep and field overflow.

No transport, width, timing, write path, mount, DMA/CQE/ICE or request-limit
change is part of N8; candidate production and any hardware observation need
separate approvals. F2, p5b/F5, N5 and N6 verdicts are unchanged; initialization
reliability remains unresolved. Evidence:
`docs/emmc-n8-partition-observation-design-2026-11-07.md`.

N8.2 then implemented the observation on the host. The selector accepts
`gta4l-emmc-partobs=1` only with exactly one block token and refuses an
orphan, duplicate or non-1 value; the legacy `PART` path is byte-for-byte
unchanged when the token is absent. The `PARTOBS` line is bounded and keeps
`complete=0`. The diagnostic payload builder now maps
`emmc-block-1-partobs` and `emmc-block-3-partobs` to two kernel arguments, the
payload checker refuses a partobs token without a block token (or with a
mismatched personality), and the packager accepts both names. Effective host
results: core 48 scenarios/38 mutation controls (unchanged), adapter 64
scenarios (nine partobs) with 12 adapter mutation controls (nine partobs) and
three policy mutation controls, all rejected for the intended reason; the
diagnostic payload suite passes 22 checks including both partobs mappings. No
candidate build, QEMU run, device access, flash or boot was performed.

### 5.3.53 N8.3 partition-observation candidate production (host-only, 2026-11-07)

Follow-up to §5.3.52. N8.3 built the approved opt-in `gta4l-emmc-partobs=1`
observation candidate through the archive/prelink/payload/BOOT flow. No flash,
boot, device access, QEMU device run, commit or push was performed; the
storage-ABI audit is host/QEMU only.

The immutable chain is prelinked kernel `4dcbf24d…` (51,224,576 bytes;
`kernel.json` `0481cbab…`), linked block archive `f9f83526…` (512,896 bytes,
190 method slots, 181 kernel bindings, 26 concrete overrides, 0 unresolved
original imports, `runtime_qualified=false`, `personality_enabled=true`),
candidate storage-ABI audit `out/gta4l/storage-abi-4dcbf24d` (interface audit
`a30cfcde…`, provenance `a806c6cf…`, snapshot `222f6322…`, run `85307464…`,
candidate plan `0481cbab…`), packaged kernel `023c8ff8…` (51,224,576 bytes),
payload `5f6759f2…` (83,886,080 bytes, 11/11 contract, diagnostic
`emmc-block-3-partobs` mapping to `gta4l-emmc-block=3 gta4l-emmc-partobs=1`)
and BOOT image `3d7e71af…` (85,688,320 bytes, 18/18 gate, only the ramdisk
differs from the `ce4d76e8…` v5 baseline). The kernel-build archive step
produced the `3e76771d…` container variant; the requalified `f9f83526…`
archive has byte-identical members. The block suite passes 48 core
scenarios/38 mutation controls and 64 adapter scenarios with 12 adapter
mutation controls plus three policy mutation controls; the storage-ABI
positive and negative checks pass. A token scan on the extracted payload
kernel finds one `GTA4L-EMMC P5 PARTOBS` string, three `REPORT` strings, one
legacy `PART:` string, four `gta4l-emmc-partobs` strings and no literal
`complete=`; the PARTOBS line is formatted with `complete=0` at runtime, so
the count-only non-claim is preserved.

The N8.3 record is `n8-closeout.txt` and the read-only hardware proposal is
`n8-hardware-proposal.txt` in
`out/gta4l/ram-root-emmc-n8-candidate-evidence/`. N8.4 is a separate
BOOT-only owner decision for one boot with verified EFS/CPEFS backups; it is
not authorized by this record. Initialization reliability remains unresolved
`[U]`, and F2, p5b/F5, N5 and N6 verdicts are unchanged. A stable `PARTOBS`
count is not a completeness claim.

### 5.3.54 N8.4 first boot and the hold-fixture correction (2026-11-07)

Follow-up to §5.3.53. N8.4 was approved separately and the N8.3 candidate was
flashed BOOT-only to p69. The pre-flash p69 `b19c59b8…` was backed up and
verified as `boot-before-n8.img.xz` (`8b37f41c…`); the write copied 85,688,320
bytes and the full p69 readback `7c22e499…` matched the candidate prefix
`3d7e71af…` plus the unchanged tail `a28b4ea2…`. RECOVERY `14e11370…`, EFS
`51fa824c…` and sec_efs `3a265d58…` were unchanged; the host did not reboot.

The one owner-controlled first cold boot returned the userspace
`RAMROOT-FST PASS: pid=1 page=4096 fs=read+seek+eof timer=poll` panic and no
eMMC report. The RAM-root fixture deliberately panics after its checks and a
50 ms poll, while the eMMC reporter independently waits for media, performs
the controlled reads and observes the partition count for up to 4,000 ms. The
fixture therefore won the terminal before the eMMC report existed. The result
is **inconclusive**: no `PARTOBS`, controlled-read or count line was captured,
and the transcript cannot distinguish a reporter that had not started, one
still waiting for media, and one that lost the terminal race. The boot budget
is consumed; no retry or reflash was performed.

The host-only correction adds an additive `fs-timer-hold` fixture variant. It
runs the same fs/timer checks, records stage 7 and blocks in a one-second
`poll(NULL, 0, 1000)` loop instead of panicking, so the eMMC report thread or
its watchdog owns termination; every failure path keeps its immediate panic.
The original `fs-timer` fixture is byte-identical (`user.o` `6984333d…`, signed
`b316e44f…`, trust cache `e146717a…`, root `77d5eba0…`). The hold variant
builds and signs as `e6546715…` (root `327ac826…`, trust cache `30a72f78…`,
nine verified pages). The diagnostic payload builder now refuses
`emmc-block-1-partobs`/`emmc-block-3-partobs` unless
`--data-ramdisk-manifest` names the `fs-timer-hold` fixture and any declared
`hold_after_success` agrees; the old early-panic fixture is rejected instead
of being silently repackaged. Host tests cover the original object hash, the
hold loop, the retained failure panic, the missing-manifest rejection, the
early-panic rejection and the hold/diagnostic mismatch. The N8.4 closeout is
`docs/emmc-n8.4-partobs-observation-2026-11-07.md`. A new candidate build, a
new BOOT-only flash and one new owner-controlled boot remain separately gated;
no eMMC, partition-completeness or reliability result follows.

### 5.3.55 N8.5 hold-fixture candidate and BOOT-only flash (2026-11-07)

Follow-up to §5.3.54. N8.5 rebuilt the observation payload and BOOT image with
the additive `fs-timer-hold` fixture, reusing the unchanged N8.3 kernel
`4dcbf24d…` and archive `f9f83526…` (190 slots, 181 bindings,
`runtime_qualified=false`; storage-ABI audit `out/gta4l/storage-abi-4dcbf24d`).
The hold root image is `327ac826…`, the hold trust cache `30a72f78…`, the
signed fixture `e6546715…` (nine verified pages) and `fixture.json`
`00363681…` (`diagnostic=fs-timer-hold`, `hold_after_success=true`). The
original `fs-timer` fixture remains byte-identical (`user.o` `6984333d…`,
signed `b316e44f…`, root `77d5eba0…`, `fixture.json` `37cbdd0b…`).

The new payload is `28eafd24…` (83,886,080 bytes; 11/11 contract; diagnostic
`emmc-block-3-partobs` mapping to `gta4l-emmc-block=3 gta4l-emmc-partobs=1`)
and the packaged kernel is `023c8ff8…`. The payload manifest records the hold
fixture path/hash, diagnostic and hold state. The BOOT image is
`b4f4f1ea…` (85,688,320 bytes; 18/18 gate; only the ramdisk differs from the
`ce4d76e8…` v5 baseline). The token scan still finds one `PARTOBS` string,
three `REPORT` strings, one legacy `PART:` string, four
`gta4l-emmc-partobs` strings and no literal `complete=`.

The image was flashed to p69 only. The pre-flash p69 `7c22e499…` (the N8.3
candidate) was backed up and verified as `boot-before-n8.5.img.xz`
(`e5b43228…`); the full p69 readback `9d51fe80…` matches the candidate prefix
`b4f4f1ea…` plus the unchanged tail `a28b4ea2…`, and RECOVERY/EFS/sec_efs are
unchanged. The device disconnected once after the backup and before any
write; adb was restarted, p69 was re-read unchanged, and the candidate was
then written once. The host did not reboot; the subsequent boot observation is
§5.3.56. A stable count is not a completeness claim, a watchdog report is an
observer result rather than a card verdict, and initialization reliability
remains `[U]`. The N8.5 record is
`docs/emmc-n8.5-hold-fixture-candidate-2026-11-07.md`; the flash record is
`out/gta4l/ram-root-emmc-n8.5-hardware-evidence/`.

### 5.3.56 N8.5 first cold-boot observation (2026-11-07)

Follow-up to §5.3.55. One owner-classified first cold boot of the N8.5
hold-fixture image returned the clean mode-3 report and TWRP. The report
carried `rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320 wid=0
tim=0 clk=400000`; the read-only whole `IOMedia` (base 0, size 31,268,536,320,
`GUID_partition_scheme`, `writable=0`) and the LBA 1 baseline match
(`sum=2228245566 same=1`). The two-block and three-block controlled reads
delivered 1,024 and 1,536 exact bytes through one two-sector CMD18 chunk each,
clean CMD12 stops and a CMD17 odd tail for the three-block request. The
aggregate `XFER` counters (`req=35 c17=9 c18=13 c18b=26 cfail=0 stops=13
sfail=0`) include incidental reads and are not attributed entirely to those
requests.

`PARTOBS` reported `samples=8 first=39 final=80 final_ms=350 changes=2 stable=1
stable_ms=250 deadline_ms=4000 timed_out=0 none=0 error=0 slots=84 reported=3
complete=0`. The first sample was already 39, so the initial transition was not
observed; `changes=2` and `stable_ms=250` imply the last observed change at
nominal t = 100 ms, and the intermediate count is unknown. `stable=1` means the
count held for the window, not that publication is complete. `final=80` is
consistent with the validated GPT's 80 populated entries among 84 slots, but a
count is not an exact GPT-to-`IOMedia` comparison; `complete=0` remains
explicit. `PART1..3` are the final enumeration sample at 4 MiB bases and sizes,
not a stable GPT-index identity. Timing fields accumulate requested sleeps, not
measured wall time. `SNAP locked=1`; `FAIL valid=0` with an all-zero record.

The bounded observation succeeded on this one cold boot and no driver fix is
indicated. Partition completeness and initialization reliability remain `[U]`;
F2, p5b/F5, N5 and N6 are unchanged. The raw transcript and observation record
are in `out/gta4l/ram-root-emmc-n8.5-hardware-evidence/observation-1/` (raw
`240ab0dd…`, interpretation `0bea53a6…`, record `92852a40…`), with a separate
non-self-hashing manifest; the original flash record and manifest are
unchanged. The N9.1 host-only exact-comparison design is
`docs/emmc-n9.1-gpt-iomedia-comparison-design-2026-11-07.md`.

### 5.3.57 N9.2 exact GPT-to-IOMedia comparator implementation (host-only, 2026-11-07)

Follow-up to §5.3.56 and the N9.1 design. N9.2 implemented the opt-in
`gta4l-emmc-partcmp=1` comparator on the host. The selector accepts the token
only with exactly one block token 1/3, value exactly 1, no duplicate and no
`gta4l-emmc-partobs=1` in the same boot; an orphan, duplicate, wrong value or
partobs combination is a refusal with the existing `IOLog` wording rather than
an inert fallback. The legacy mode-1/mode-3 report is unchanged when the token
is absent.

The comparator validates the protective MBR, primary header and entry array,
then the backup header and array, including CRC, geometry, reciprocal
locations, usable bounds, populated-entry semantics, overlap and duplicate
rules, and cross-copy agreement. It normalizes populated entries to sorted
`(base_bytes, size_bytes)` tuples and compares the exact multiset with only the
direct children of the selected whole-media `IOGUIDPartitionScheme`, excluding
nested containers. Sampling is bounded by the 2,000 ms deadline, 50 ms step,
250 ms unchanged window, 4,000 ms read budget and a 25-read/45-block
worst-case; cancellation, terminal ownership and the no-gate sleep rule are
preserved. The `PARTMAP` line emits only bounded counts, stage flags and
reason codes (`complete=1` only for the exact same-boot equality); it prints
no ranges, GUIDs or labels. The worst-case device-bounded line is 288 bytes
against the 384-byte cap and the full report is 1,088 bytes against 2,048;
the writer still fails closed and returns 0 rather than truncating. The
absence of a token leaves the pre-existing report byte-identical, including
when the new fields are populated.

The diagnostic payload builder now exposes `emmc-block-1-partcmp` and
`emmc-block-3-partcmp` (block token plus `gta4l-emmc-partcmp=1`), requires the
same `fs-timer-hold` manifest as the partobs personalities, and refuses a
partobs+partcmp combination; the payload checker enforces the token rules and
the packager accepts both names. Effective host results: core 48 scenarios/38
mutation controls (unchanged), adapter 85 scenarios (21 partcmp) with 16
adapter mutation controls (four partcmp) and eight policy mutation controls
(five partcmp), all rejected for the intended reason; the diagnostic payload
suite passes 28 checks including both partcmp mappings, and the packager
self-test passes 11 checks including the partcmp forwarding refusal. No
candidate build, QEMU run, device access, flash or boot was performed. N9.3
archive/ABI/kernel/payload/BOOT production and any N9.4 hardware run remain
separately gated; N8.5's historical `complete=0` is not upgraded. N9.3 later
found that the embedded comparator scratch made the driver object exceed
XNU's 16 KiB kalloc type limit; §5.3.58 moves it to one bounded allocation.

### 5.3.58 N9.3 exact-comparator candidate production (host-only, 2026-11-07)

Follow-up to §5.3.57 and the N9.1 design. N9.3 built the approved
`gta4l-emmc-partcmp=1` candidate through the archive/prelink/audit/payload/
BOOT flow. The storage-ABI audit is host/QEMU only. No flash, boot, device
access, commit or push was performed; the host build is not permission to
flash or boot. The detailed record is
`docs/emmc-n9.3-partcmp-candidate-2026-11-07.md`.

The real kernel build rejected the N9.2 object at XNU's 16 KiB kalloc type
limit: the embedded comparator scratch is a 16,384-byte GPT array plus two
2,112-byte validated GPT copies and two 2,048-byte media snapshots (24,704
bytes), making `sizeof(Gta4lEmmcBlock)` 38,712 bytes. N9.3 moved that state to
`struct gta4l_emmc_block_partmap_scratch`, allocated once with `IOMalloc` in
`start()` when the comparator token selects the mode and released in `free()`;
an allocation failure refuses `start()` with an `IOLog` and no partial claim.
The object is now 14,016 bytes, and a
`static_assert(sizeof(Gta4lEmmcBlock) <= 16 * 1024)` runs in both the host and
target builds as a regression control. The host block suite still passes 48
core scenarios/38 mutation controls and 85 adapter scenarios/16 adapter and
eight policy mutation controls.

The immutable chain is prelinked kernel `fd2c8c65…` (51,257,344 bytes; plan
`374a9d74…`), kernel-build archive `408c0ce1…` (644,656 bytes), requalified
archive `02cb5563…` (644,632 bytes; three byte-identical members plus one
`__.SYMDEF` container member), candidate storage-ABI audit
`out/gta4l/storage-abi-fd2c8c65` (`gdb-stop.json` `dd20cf0b…`,
`interface-audit.json` `c5cad497…`, `provenance.json` `75bfc26f…`,
`run.json` `22d4d85c…`; 190 slots, 181 bindings, 26 concrete overrides, 0
unresolved original imports, `runtime_qualified=false`), packaged kernel
`d5a98930…`, payload `fd73b37f…` (83,886,080 bytes, 14/14 contract,
`emmc-block-3-partcmp` mapping to `gta4l-emmc-block=3 gta4l-emmc-partcmp=1`,
hold root `327ac826…` and fixture `00363681…`) and BOOT image `fa55ef61…`
(85,688,320 bytes, 18/18 gate, only the ramdisk differs from the
`ce4d76e8…` v5 baseline). A token scan on the packaged kernel finds one
`GTA4L-EMMC P5 PARTMAP` string, three `REPORT` strings, one legacy `PART:`
string, five `gta4l-emmc-partcmp` strings and five `gta4l-emmc-partobs`
strings; the binary carries both opt-in paths and the boot argument selects
`partcmp`. There is no literal `complete=`, so the PARTMAP completion value
stays runtime-formatted. N8.5's historical `complete=0` is not upgraded and
N9.4 remains separately gated.

### 5.3.59 N9.4 exact-comparator observation and whole-media Base root cause (2026-11-07)

N9.4 flashed the N9.3 `emmc-block-3-partcmp` BOOT image to p69 only and
consumed its one owner-controlled cold boot. The report reached the bounded
diagnostic stop but failed closed at whole-media discovery, before any GPT
validation or partition comparison. The owner returned the tablet to TWRP; no
retry, reflash, rollback, reset/repair, commit or push was performed. The
flash record is `flash-commands.log` `3b3f9065…` and `flash-record.json`
`89c90441…`; the candidate chain is BOOT `fa55ef61…`, payload `fd73b37f…`,
prelinked kernel `fd2c8c65…`, packaged kernel `d5a98930…` and source
`Gta4lEmmcBlock.cpp` `062ee8fa…`, with the post-flash p69 readback
`8d5042d3…` (candidate prefix plus unchanged tail `a28b4ea2…`). Raw evidence
is in `out/gta4l/ram-root-emmc-n9-hardware-evidence/observation-1/` (raw
`97cc0002…`, interpretation `75f7bb79…`, record `ac7dcfb3…`); the sanitized
record is `docs/emmc-n9.4-partcmp-observation-2026-11-07.md`.

The report was `rc=0 stop=0 mode=3 blocks=61071360 bs=512
bytes=31268536320 wid=0 tim=0 clk=400000`, `PARTMAP: gpt=0 reason=10
slots=0 used=0 … prop=0 samples=0 changes=0 stable=0 stable_ms=0
deadline_ms=2000 timed_out=0 budget=0 reads=0 blocks=0 fails=0 cancel=0
complete=0`, and `MEDIA: whole=0 base=0 size=0 content=other writable=0`.
`reason=10` is `GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA`. The MEDIA fields are
set only after exactly one qualifying whole medium is selected, so their zero
values are consistent with a failure before selection; they do not prove that
no whole media existed and the report does not distinguish zero from multiple
matches. `gpt=0` is not evidence of a corrupt GPT, `media=0` is not a
partition count, and `deadline_ms=2000`/`timed_out=0` belong to the later
snapshot phase. The driver aggregate (`online=1 wp=1 reads=9 blocks=29
fails=0 max=8`) and the consistent `XFER` counters describe incidental scan
reads, not comparator reads. `SNAP locked=1` and the all-zero FAIL record are
consistent with the intentional report stop. The owner's observation that the
log took "a litte longer than usual … a couple of seconds more instead of
instant" is preserved as subjective; it is consistent with the discovery
loop's nominal 20,000 ms wait (`GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS`, 100 ms
steps), which the report does not timestamp.

The cause is a property-predicate mismatch, not a transport or GPT failure.
`findExactWholeMedia()` requires `Whole` true, `Base` present as an `OSNumber`
equal to 0, `Size` equal to `capacityBlocks * 512`, and `Content` equal to
`GUID_partition_scheme`, all within the ancestry cap. In the pinned
IOStorageFamily `67f9003c`, the whole media is created by
`IOBlockStorageDriver::instantiateMediaObject()`, which calls `IOMedia::init()`
with an empty content hint; `init()` publishes `Content`, `Content Hint`,
`Preferred Block Size`, `Size`, `Whole` and `Writable`, but not `Base`.
`kIOMediaBaseKey` is defined in `IOPartitionScheme.h` and is set by partition
schemes only on partition media (`IOGUIDPartitionScheme.cpp`,
`IOFDiskPartitionScheme.cpp`, `IOApplePartitionScheme.cpp`). The GUID scheme's
personality supplies the `Content Mask` `GUID_partition_scheme`, which
`IOMedia::attachToChild()` copies into the whole media's `Content` when the
scheme attaches. The real whole media therefore has no `Base` property;
`OSDynamicCast` yields null and the finder rejects it on every poll, so the
discovery loop expires and reports reason 10 before any GPT read. The legacy
`findWholeMedia()` already tests only `Whole` and ancestry and needs no change;
partition media still require `Base`.

The host seam masked the defect. `gta4l_test_media_configure()` sets `Base` on
every media, including whole media, and the `partcmp-property` `BASE_MISSING`
control faults a partition media, so the suite always exercised `Base=0` on
the whole media. A new adapter scenario, `partcmp-whole-no-base`, applies the
missing-Base fault to the whole media and pins the observed hardware signature
(`gpt=0 reason=10`, `prop=0`, `samples=0`, `reads=0 blocks=0 fails=0 cancel=0
complete=0`). At the observation the block suite passed 48 core
scenarios/38 mutation controls and 86 adapter scenarios with 16 adapter and
eight policy mutation controls. That scenario then pinned the defect; §5.3.60
inverts it and treats the corrected behavior as desired.

The owner-approved correction is recorded in §5.3.60. It accepts an absent
`Base` on a whole media as the implied zero base, rejects a present nonzero
`Base`, and keeps the `Whole`/`Size`/`Content`/ancestry checks and the
exactly-one guard. This follows the family's construction: the whole media is
created at base 0 and partition schemes publish `Base` explicitly on
partitions. Candidate rebuild, ABI/QEMU audit, payload/BOOT production, flash
and boot remain separate gates. No partition-completeness claim is made;
N8/N8.5's historical `complete=0` is not upgraded, and no
initialization-reliability, wider/faster mode, write, mount, DMA/CQE/ICE or
arbitrary-I/O result is added.

### 5.3.60 N9.4 whole-media Base correction (host-only, 2026-11-07)

Follow-up to §5.3.59. The owner approved the host-only correction and this
section records it. No candidate was rebuilt, no ABI/QEMU audit was run, and
no device access, flash, boot, retry, reset/repair, commit or push was
performed.

`findExactWholeMedia()` now fetches the raw `Base` property before casting. An
absent `Base` on a whole media is accepted as the implied zero; a present
property that is not an `OSNumber` or that is nonzero is rejected. The
`Whole`/`Size`/`Content`/ancestry predicates and the exactly-one guard are
unchanged. `snapshotPartitionMedia()` still requires `Base` on partition
media, and the legacy `findWholeMedia()` still tests only `Whole` and ancestry.
The corrected adapter source is `Gta4lEmmcBlock.cpp` `b7aeff0a…`.

The host seam now models both sides. `partcmp-whole-no-base` is inverted: a
whole media without `Base` must be accepted and produce the full comparison
(`whole=1 base=0`, `gpt=1 reason=0`, `media=80 match=1`, `complete=1`). The
new `partcmp-whole-base-invalid` control rejects a present nonzero `Base` and
a present wrong-type `Base` with `reason=10` before any read. The existing
`partcmp-property` control still faults a partition media, so the
partition-side `Base` requirement remains covered. The discovery wait,
missing/ambiguous media, content timing, cancellation, exact GPT/range
comparison, duplicate/missing/extra rejection, read-only behavior and report
semantics are unchanged.

The block suite passes 48 core scenarios/38 mutation controls and 87 adapter
scenarios with 16 adapter and eight policy mutation controls. This is
host-only evidence. N9.5 archive/prelink/ABI-QEMU/payload/BOOT production is
recorded in §5.3.61; a fresh explicit BOOT-only approval, verified protected
backups/readbacks and a new predeclared owner-controlled one-boot budget
remain separately gated. No retry, reflash, rollback, reset/repair,
mount/root switch, partition write, wider/faster mode, DMA/CQE/ICE or
request-limit increase is authorized. No partition-completeness claim is
made; N8/N8.5's historical `complete=0` is not upgraded, and no
initialization-reliability, wider/faster mode, write, mount, DMA/CQE/ICE or
arbitrary-I/O result is added.

### 5.3.61 N9.5 corrected candidate production (host-only, 2026-11-07)

Follow-up to §5.3.60. N9.5 built the corrected whole-media `Base` candidate
through the archive/prelink/audit/payload/BOOT flow and stopped at the
production record. No flash, boot, device access, retry, reset/repair, commit
or push was performed.

The prelinked kernel is `a610e9d1…` (51,257,344 bytes; plan `737305c9…`). The
kernel-build archive `b9b7ecc0…` records `Gta4lEmmcBlock.cpp` `b7aeff0a…`
compiled to `Gta4lEmmcBlockCpp.o` `f9d2e3ed…`; the policy object is unchanged
at `fb94f96f…`. A fresh candidate-matched Linux/QEMU audit in
`out/gta4l/storage-abi-a610e9d1` (`gdb-stop.json` `862bc0ff…`,
`interface-audit.json` `5ad1bfb1…`, `provenance.json` `c92ece5f…`,
`run.json` `09ab30ec…`) binds 190 slots, 181 kernel bindings (167 inherited
+ 14 pure virtual), 26 concrete overrides and 0 unresolved original imports,
with live sizes 0x90/0x88, native 4K and `runtime_qualified=false`.
Requalifying the archive against that audit produced `bd394f12…`; all three
members are byte-identical to the kernel-build archive and the container
differs only in its index.

The packaged kernel is `a17c71dd…`. The payload is `79ac3a10…` (83,886,080
bytes; manifest `68bf9a22…`; 14/14 contract; diagnostic
`emmc-block-3-partcmp`; hold root `327ac826…`, fixture `00363681…`, trust
cache `30a72f78…`). The BOOT image is `45f3bb81…` (85,688,320 bytes;
manifest `732e2d59…`; gate `299c689f…`, 18/18, only the ramdisk differs from
the hardware-tested `ce4d76e8…` baseline; baseline kernel section
`a4b054e0…`, firmware `1e8a3132…` and DTB `dc4c719d…` unchanged). The
packaged kernel carries one `GTA4L-EMMC P5 PARTMAP` string, three `REPORT`
strings, one legacy `PART:` string, five `gta4l-emmc-partcmp` and five
`gta4l-emmc-partobs` strings, and no literal `complete=`.

The block suite passes 48 core scenarios/38 mutation controls and 87 adapter
scenarios with 16 adapter and eight policy mutation controls; the payload
suite passes 28 checks, the packager self-test 11 checks, and the storage-ABI
positive/negative checks, evidence counts, fixture recipe and board check
pass. This is host-only production. N8/N8.5's historical `complete=0` is not
upgraded and no partition-completeness or initialization-reliability claim is
made. A fresh explicit BOOT-only approval, verified protected
backups/readbacks and a new predeclared owner-controlled one-boot budget
remain separately gated. No retry, reflash, rollback, reset/repair,
mount/root switch, partition write, wider/faster mode, DMA/CQE/ICE or
request-limit increase is authorized by this record. No commit or push. The
full record is `docs/emmc-n9.5-partcmp-candidate-2026-11-07.md`.

### 5.3.62 N9.6 exact-comparator observation and GUID-scheme lookup (2026-11-07)

Follow-up to §5.3.61. N9.6 flashed the N9.5 `emmc-block-3-partcmp` BOOT image
to p69 only and consumed its one owner-controlled cold boot. The report
selected the real whole media, so the N9.5 absent-`Base` correction works on
hardware, but the comparator stopped at the GUID-scheme lookup before any GPT
read. The owner returned the tablet to TWRP; no retry, reflash, rollback,
reset/repair, commit or push was performed. The flash record is
`flash-commands.log` `f7e32544…` and `flash-record.json` `3ccc9d9d…`; the
candidate chain is BOOT `45f3bb81…`, payload `79ac3a10…`, prelinked kernel
`a610e9d1…`, packaged kernel `a17c71dd…` and source `Gta4lEmmcBlock.cpp`
`b7aeff0a…`, with the post-flash p69 readback `9c5bcf2c…` (candidate prefix
plus unchanged tail `a28b4ea2…`) and unchanged RECOVERY/EFS/sec_efs. The
rollback `boot-before-n9.6.img.xz` (`baddb107…` -> raw `8d5042d3…`) was not
restored. Raw evidence is in
`out/gta4l/ram-root-emmc-n9.6-hardware-evidence/observation-1/` (raw
`440b0d44…`, interpretation `57221ca8…`, record `87c45795…`, manifest binding
`evidence-sha256.txt` `423923e7…`); the sanitized record is
`docs/emmc-n9.6-partcmp-observation-2026-11-07.md`.

The report was `rc=0 stop=0 mode=3 blocks=61071360 bs=512
bytes=31268536320 wid=0 tim=0 clk=400000`, `MEDIA: whole=1 base=0
size=31268536320 content=GUID_partition_scheme writable=0`, `PARTMAP: gpt=0
reason=10 slots=0 used=0 pmbr=0 hdr=0 arr=0 bak=0 xcheck=0 media=0 match=0
missing=0 extra=0 dup=0 nested=0 prop=0 samples=0 changes=0 stable=0
stable_ms=0 deadline_ms=2000 timed_out=0 budget=0 reads=0 blocks=0 fails=0
cancel=0 complete=0`, `PART: count=0 expected=0 waited_ms=0 error=0
reported=0`, and zero `READ`/`QUAL`/`QUAL3` counters. The driver aggregate was
online=1, wp=1, 10 reads/29 blocks, fails=0, max=8; the transport aggregate
was req=29, c17=7, c18=11, c18b=22, cfail=0, stops=11, sfail=0, last_stop=0.
These are incidental/scan requests, not comparator reads. The supplied panic
suffix `@Gta41EmmcBlock.cpp:1991` preserves its spelling; line 1991 in the
candidate source is the intentional `panic("%s", reportBuffer)` in
`panicWithReport()`, so header success and FAIL valid=0 are consistent with
the report stop.

`whole=1` and the populated MEDIA fields establish a successful whole-media
selection and confirm the N9.5 correction on this boot. `reason=10` is
`GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA`; `reads=0 blocks=0` and
`samples=0 complete=0` establish that no GPT read, GPT validation or exact
partition comparison occurred. They do not establish a corrupt GPT, missing
partitions or initialization reliability. The owner classified the run as
exactly one cold boot with a return to TWRP and described the delay as
noticeably shorter; that wording is preserved as a subjective observation, not
a measured duration. N8/N8.5's historical `complete=0` is not upgraded, and no
partition-completeness or reliability claim is made. A later correction,
candidate production and hardware run remain separately gated.

### 5.3.63 N9.7 GUID-scheme seam investigation (host-only, 2026-11-07)

Follow-up to §5.3.62. This section records the read-only source/seam
investigation of the two remaining pre-read `reason=10` guards. No driver
correction was implemented, no candidate was built, no ABI/QEMU audit was run,
and no device access, flash, boot, commit or push was performed.

`findGuidScheme()` in `Gta4lEmmcBlock.cpp` enumerates
`IOService::getMatchingServices(IOService::serviceMatching("IOGUIDPartitionScheme"))`
and keeps the direct-provider, `UUID`-string and exactly-one predicates. In the
pinned XNU, `getMatchingServices()` calls
`copyExistingServices(matching, kIOServiceMatchedState)`, and `instanceMatch()`
requires the service state to contain `kIOServiceMatchedState` and to be
non-inactive. The state bit is set by `copyNotifiers(gIOMatchedNotification,
…)` inside `doServiceMatch()`, reached from `IOService::registerService()` ->
`startMatching()`.

In the pinned IOStorageFamily `67f9003c`, `IOGUIDPartitionScheme::start()`
attaches and registers every partition media (`IOPartitionScheme.cpp:582,716`)
but never registers the scheme object itself; `IOPartitionScheme` has no
`start()`/`probe()` override and `IOStorage` does not register it either. The
scheme is nevertheless a direct service-plane child of the whole media:
`IOService::startCandidate()` calls `service->attach(this)` before
`probe()`/`start()`, and `IOMedia::attachToChild()` copies the scheme's
`Content Mask` `GUID_partition_scheme` into the whole media's `Content`.
`findExactWholeMedia()` requires that `Content`, so the accepted whole media
proves the scheme attached before selection; `IOGUIDPartitionScheme::scan()`
sets the `UUID` property during probe, before attach. The scheme is therefore
attached and carries its `UUID`, but it is invisible to matched-state matching
because it never registered. That is the source-grounded cause of the N9.6
`reason=10`.

The whole-media reader-open guard is eliminated as the cause.
`IOGUIDPartitionScheme::scan()` opens the provider media with
`open(this, 0, kIOStorageAccessReader)` and closes it before returning; the
pinned `IOMedia::handleOpen()` sums the existing reader levels instead of
excluding a second reader, and `IOMedia::read()` fails only when
`_openLevel == kIOStorageAccessNone`. Read-only access and fail-closed
behavior are unchanged, and no access-mode change is proposed.

The host seam previously returned every configured scheme from
`getMatchingServices()` and modeled no registration state, so the comparator
scenarios could not see the failure. It now models registration visibility:
`getMatchingServices()` returns only schemes whose `registerService()` was
called, and the fixture separates attach-only
(`partcmp_add_scheme_unregistered()`) from attached-and-registered
(`partcmp_add_scheme()`). New scenarios pin the hardware-identical `gpt=0
reason=10 slots=0 used=0 … samples=0 changes=0 stable=0 stable_ms=0
deadline_ms=2000 timed_out=0 budget=0 reads=0 blocks=0 fails=0 cancel=0
complete=0` signature for an attached-but-unregistered scheme
(`partcmp-scheme-unregistered`) and for a missing scheme
(`partcmp-scheme-absent`), plus a missing `UUID` (`partcmp-scheme-no-uuid`), a
wrong provider (`partcmp-scheme-wrong-provider`), duplicate matching schemes
(`partcmp-scheme-duplicate`) and reader-open refusal
(`partcmp-open-refused`, using the fixture's open-refusal flag). The
registered-scheme scenarios are unchanged and still reach the full comparison.

The block suite passes 48 core scenarios/38 mutation controls and 93 adapter
scenarios with 16 adapter and eight policy mutation controls. This is host-only
evidence and does not qualify the driver on hardware.

The smallest justified correction, presented for owner approval only, is to
replace the `getMatchingServices()` enumeration with the selected whole
media's direct service-plane children (for example
`whole->getChildIterator(gIOServicePlane)`), keeping the class, direct-provider,
`UUID`-string and exactly-one guards and the existing discovery wait. The
accepted whole media already proves the scheme attached, so no new wait or
timing change is proposed. An approved host-only correction must add
service-plane child tracking to the seam, invert
`partcmp-scheme-unregistered` to require the full successful comparison, and
preserve the absent/missing-`UUID`/wrong-provider/duplicate and
reader-open-refusal controls. No identity, ancestry, exactly-one, read-only,
GPT-validation or exact-comparison guard may be weakened. Candidate production
and any new hardware run remain later, separately approved gates.

### 5.3.64 N9.7 GUID-scheme lookup correction (host-only, 2026-11-07)

Owner-approved follow-up to §5.3.63. This section records the host-only
correction and its seam extension; the candidate production that followed is
recorded in §5.3.65. No device access, flash, boot, commit or push was
performed.

`findGuidScheme()` now enumerates the selected whole media's direct
service-plane children with `whole->getChildIterator(gIOServicePlane)`. The
lookup no longer calls `IOService::getMatchingServices()`; the four remaining
matching walks in the adapter are the whole-media and partition-media `IOMedia`
walks. The direct-provider guard, the `UUID`-string guard, the exactly-one
guard and the existing media discovery wait are unchanged. In the pinned
headers `getChildIterator()` returns `OSPtr<OSIterator>`; the build does not
define `IOKIT_ENABLE_SHARED_PTR`, so `OSPtr<T>` is `T *` and the iterator is
released exactly once. A failed iterator allocation still returns `nullptr` and
reports the existing media reason without a read.

The class guard had to change. A direct
`OSDynamicCast(IOGUIDPartitionScheme, object)` compiles but fails the kernel
link with an undefined `__ZN21IOGUIDPartitionScheme9metaClassE`: the pinned
class lives in the prelinked storage kext and its metaclass is not exported to
the kernel link. The guard is now the name-based
`object->metaCast("IOGUIDPartitionScheme")`, following the existing
`ShengPlatformPower.cpp` precedent (`provider->metaCast("ApplePMGR")`). The
direct-provider, `UUID` and exactly-one guards still constrain the result, so a
same-named foreign object is rejected by them.

The host seam now models direct service-plane children. `IOService` carries a
bounded child list and `getChildIterator(gIOServicePlane)` returns only those
children; `gta4l_test_add_child()` attaches a child. The seam defines an
`IOGUIDPartitionScheme` class and a `metaCast` test double keyed on the class
name, so the production name-based class guard is exercised by the same
expression used on target. `partcmp_setup()` gained a registration flag. The
unregistered fixture attaches the scheme as a child of the whole media and
configures its provider and `UUID` but never calls `registerService()`; the
registered fixture does both. `getMatchingServices()` still hides unregistered
schemes, so the two visibility models remain distinct.

`partcmp-scheme-unregistered` is inverted: it now requires the full successful
comparison (`gpt=1 reason=0 slots=84 used=80 pmbr=1 hdr=1 arr=1 bak=1
xcheck=1 media=80 match=1 … stable=1 stable_ms=250 timed_out=0 budget=0
reads=25 blocks=45 fails=0 cancel=0 complete=1`) and asserts that the scheme
was never registered (only the adapter's own registration is counted). The new
`partcmp-scheme-wrong-class` control attaches a plain `IOService` child with a
`UUID` and requires the class guard to reject it with the pre-read
`gpt=0 reason=10 … reads=0 complete=0` signature. The
`partcmp-scheme-absent`, `partcmp-scheme-no-uuid`,
`partcmp-scheme-wrong-provider`, `partcmp-scheme-duplicate` and
`partcmp-open-refused` controls are retained.

The adapter mutation set grows to 18: `partcmp-scheme-class-guard` replaces the
name check with `if (false)` and must fail `partcmp-scheme-wrong-class`;
`partcmp-scheme-exactly-one` accepts zero or many children and must fail
`partcmp-scheme-duplicate`. The policy mutation set is unchanged at eight.

The block suite passes 48 core scenarios/38 mutation controls and 94 adapter
scenarios with 18 adapter and eight policy mutation controls. The diagnostic
payload (28), packager (11), evidence-count (core 187/115, probe 196/100),
RAM-root fixture recipe and board checks pass unchanged. This is host-only
evidence: the correction is not hardware-verified, N9.6's `complete=0` is not
upgraded, and no partition-completeness or initialization-reliability claim is
made. The corrected candidate production is recorded in §5.3.65; a BOOT-only
flash and any new boot remain later, separately approved gates.

### 5.3.65 N9.7 corrected candidate production (host-only, 2026-11-07)

Follow-up to §5.3.64. This section records the host-only production chain for
the corrected GUID-scheme lookup and stops at the production record. No flash,
boot, device access, retry, reset/repair, commit or push was performed.

The prelinked kernel is `06826fc5…` (51,257,344 bytes; plan `c9a11f43…`). The
kernel-build archive `d8234e69…` records `Gta4lEmmcBlock.cpp` `0bc83206…`
compiled to `Gta4lEmmcBlockCpp.o` `19f0ba8b…`; the policy object is unchanged
at `fb94f96f…`. A fresh candidate-matched Linux/QEMU audit in
`out/gta4l/storage-abi-06826fc5` (`gdb-stop.json` `0c1f6e34…`,
`interface-audit.json` `e70c0503…`, `provenance.json` `1ad75463…`,
`run.json` `56e469d4…`) binds 190 slots, 181 kernel bindings (167 inherited
+ 14 pure virtual), 26 concrete overrides and 0 unresolved original imports,
with live sizes 0x90/0x88, native 4K and `runtime_qualified=false`.
Requalifying the archive in the build tree against that audit produced
`6a6af596…`; all three members are byte-identical to the kernel-build archive
and the container differs only in its index.

The packaged kernel is `61b9cea0…`. The payload is `79c83d67…` (83,886,080
bytes; manifest `1bdd7051…`; 14/14 contract; diagnostic
`emmc-block-3-partcmp`; kernel arguments ending
`gta4l-emmc-block=3 gta4l-emmc-partcmp=1`; hold root `327ac826…`, fixture
`00363681…`, trust cache `30a72f78…`). The BOOT image is `74a4c687…`
(85,688,320 bytes; manifest `788d5008…`; gate `bc2c0281…`, 18/18, only the
ramdisk differs from the hardware-tested `ce4d76e8…` baseline; baseline
kernel section `a4b054e0…`, firmware `1e8a3132…` and DTB `dc4c719d…`
unchanged).

The block suite passes 48 core scenarios/38 mutation controls and 94 adapter
scenarios with 18 adapter and eight policy mutation controls; the payload
suite passes 28 checks, the packager self-test 11 checks, and the storage-ABI
positive/negative checks, evidence counts, fixture recipe and board check
pass. This is host-only production. N9.6's `complete=0` is not upgraded and no
partition-completeness or initialization-reliability claim is made. A fresh
explicit BOOT-only approval, verified protected backups/readbacks and a new
predeclared owner-controlled one-boot budget remain separately gated. No
retry, reflash, rollback, reset/repair, mount/root switch, partition write,
wider/faster mode, DMA/CQE/ICE or request-limit increase is authorized by this
record. No commit or push. The private evidence directory is
`out/gta4l/ram-root-emmc-n9.7-candidate-evidence/`.

### 5.3.66 N9.7 corrected-candidate observation and page-straddle analysis (2026-11-07)

Follow-up to §5.3.65. N9.7 flashed the corrected image BOOT-only to p69 and
consumed one owner-controlled cold boot. The flash record is
`ecf652c9…` with the unchanged manifest `c35f25d3…`; BOOT `74a4c687…`
(85,688,320 bytes), payload `79c83d67…`, prelinked kernel `06826fc5…` and
packaged kernel `61b9cea0…` match §5.3.65. The full p69 readback is
`aaaf8f03…` (candidate prefix `74a4c687…` plus unchanged tail `a28b4ea2…`);
RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` were unchanged,
and rollback `boot-before-n9.7.img.xz` `f7e5ac3c…` preserves raw `9c5bcf2c…`.

The report selected the real whole media (`whole=1 base=0 size=31268536320
content=GUID_partition_scheme writable=0`), so the §5.3.64 correction works on
hardware: the service-plane child lookup, class match and reader open passed.
The first comparator LBA 0/512-byte read then failed with `gpt=0 reason=8
reads=1 blocks=1 fails=1 samples=0 complete=0`. Reason 8 is
`GTA4L_EMMC_BLOCK_PARTMAP_REASON_READ`; `readPartmapSectors()` returns it when
`media->read` is non-success or `actual != 512`, and the report discards both
details. `reads=1 blocks=1` confirms one attempted sector; `fails=1` is the
reporter, not a transport measurement. DRV online=1/wp=1/9 requests/29
blocks/fails=0/max=8 and XFER req=29/c17=7/c18=11/c18b=22/cfail=0/stops=11/
sfail=0 are incidental scan requests. GPT validation and comparison did not
begin. The supplied panic suffix `@Gta41EmmcBlock.cpp:2000` is the intentional
report stop in the `0bc83206…` source.

Host-only analysis points to the driver breakup stage, not the adapter. The
flashed DWARF gives `Gta4lEmmcBlock` byte size `0x3658` with `partmapPmbr` at
`0x0e18`; the object's 13,912 bytes land in the page-multiple 16 KiB kalloc
class, whose element base is page-aligned, and KASAN is disabled. The buffer
therefore starts at page offset 3,608; `getPhysicalSegment(…,
kIOMemoryMapperNone)` yields a 488-byte first segment and a 24-byte second
segment. The advertised `kIOMaximumSegmentCountReadKey=1` stops
`IOBreaker::getBreakSize()` before folding the second page unless it is
physically contiguous or a system mapper exists; this platform has none, so
`breakSize=488`, `IOTrunc(488, 512)=0`, and `breakUpRequest()` completes with
`kIOReturnDMAError` before `executeRequest()`/`doAsyncReadWrite`. That path
leaves the adapter counters untouched, matching the report. Physical
adjacency of the two pages is not host-observable, and
`prepareRequest`/`IOBreaker` allocation failure remains the main unselected
alternative early exit. The host seam calls `doAsyncReadWrite` directly and
does not qualify the real context/deblock/breakup layers.

This is a bounded observation, not a root-cause proof. N9.6's `complete=0` is
not upgraded and no partition-completeness or initialization-reliability claim
is made. The next host-only batch should distinguish the page-straddle path
from allocation failure and preserve raw `IOReturn`/actual bytes in the
report. Candidate corrections for owner approval, smallest first: stage the
reads through a page-aligned bounce buffer; raise/remove the segment-count
limit; or page-align the member buffers within the 16 KiB typed-allocator
limit. No driver correction, candidate build, ABI/QEMU audit, device access,
flash, boot, retry, reset/repair, commit or push is authorized or performed by
this record. Private evidence is in
`out/gta4l/ram-root-emmc-n9.7-hardware-evidence/`.

### 5.3.67 N9.7 comparator read-detail host batch (2026-11-07)

Follow-up to §5.3.66. This section records a host-only diagnostic capture; no
driver correction, candidate build, ABI/QEMU audit, device access, flash, boot,
retry, reset/repair, commit or push was performed. The N9.7 boot budget stays
consumed and the N9.6/N9.7 `complete=0` verdicts are not upgraded.

The N9.7 report could not distinguish a media read that returned an error from
one that returned fewer bytes, and it discarded the raw `IOReturn` and actual
count. `struct gta4l_emmc_block_report` now carries a bounded read-detail
record: `partmap_read_state` (`GTA4L_EMMC_BLOCK_PARTMAP_READ_NONE` 0 not
attempted, `READ_FLIGHT` 1 in flight, `READ_ERROR` 2 returned error,
`READ_SHORT` 3 short transfer, `READ_FULL` 4 full completion),
`partmap_read_lba`, `partmap_read_blocks`, `partmap_read_bytes`, the raw
signed 32-bit `partmap_read_status`, the returned `UInt64`
`partmap_read_actual`, and the `IOMemoryDescriptor` `getLength()`/
`getDirection()` pair in `partmap_read_desc_len`/`partmap_read_desc_dir`.
`readPartmapSectors()` publishes the request under `reportLock` before
`media->read`, records the outcome only when the state is still `FLIGHT` for
the same LBA/block pair, and preserves the first `ERROR` or `SHORT` state
across later reads. A descriptor-allocation refusal or a refused media open
leaves the record at `NONE`; neither path starts a read.

The new `GTA4L-EMMC P5 READDETAIL:` line follows every `PARTMAP` line and
emits `state`, `lba`, `blocks`, `bytes`, `status`, `actual`, `desc_len` and
`desc_dir`. It is bounded by
`GTA4L_EMMC_BLOCK_PARTMAP_DETAIL_LINE_BYTES` (256) and keeps the whole report
inside the 2048-byte buffer. The fields are all zero in a report that never
attempted a comparator read.

The host seam covers the capture: `partcmp-read-failure` injects a
`kIOReturnDMAError` (`0xe00002ca`, printed as `3758097098`) at each of the 25
read positions and asserts `state=2`, `actual=0`, the LBA/block/byte and
descriptor fields, `reason=8`, `fails=1`, `samples=0` and `complete=0`;
`partcmp-read-short` covers `actual=0` and `actual=511` for a 512-byte request
and a 512-of-1024-byte short transfer after earlier successes (`state=3`);
`partcmp-read-inflight` pins the watchdog `state=1` snapshot; the success path
asserts `state=4`; and `partcmp-open-refused` plus the new
`partcmp-descriptor-failure` control pin the all-zero `state=0` line with
`reason=10`. Five adapter mutation controls reject a discarded status, actual,
descriptor length, short state or in-flight state. The formatter fixture also
checks the default and maximum field widths and the 256-byte detail-line cap.
The block suite passes 48 core scenarios/38 mutation controls and 97 adapter
scenarios with 23 adapter and eight policy mutation controls.

Constant note (2026-11-08): this batch originally injected and expected
`0xe00002ca`/`3758097098` under the `kIOReturnDMAError` name. That value is
actually `kIOReturnIOError`; `kIOReturnDMAError` is `0xe00002d4`/`3758097108`
(`IOReturn.h:167`). The N9.8 closeout corrected the fixture and the current
records (§5.3.69); this historical paragraph is retained as recorded.

This is a diagnostic capture, not a root-cause proof. The host seam calls
`doAsyncReadWrite` directly and does not qualify the real context, deblock or
breakup layers; physical adjacency, `IOBreaker` allocation failure and the
page-straddle hypothesis all remain unselected inputs. The next gated boot must
read the `READDETAIL` fields before any candidate correction is selected, and
no buffer, segment-limit or request-limit change is authorized by this record.

### 5.3.68 N9.8 read-detail candidate production (host-only, 2026-11-08)

Follow-up to §5.3.67. N9.8 built the read-detail-instrumented
`emmc-block-3-partcmp` candidate through the archive/prelink/audit/payload/
BOOT flow and stopped at the production record. No flash, boot, device access,
retry, reset/repair, commit or push was performed; the N9.7 boot budget stays
consumed and the N9.6/N9.7 `complete=0` verdicts are not upgraded.

The prelinked kernel is `2777bfce…` (51,257,344 bytes; plan `c85c3abd…`). The
kernel-build archive `176b533b…` records `Gta4lEmmcBlock.c` `f84d1bd2…`
compiled to `Gta4lEmmcBlock.o` `0c76b09e…` and `Gta4lEmmcBlock.cpp`
`440279b2…` compiled to `Gta4lEmmcBlockCpp.o` `db6d2b74…`; the bindings
object is unchanged at `3e53b66d…`. A fresh candidate-matched Linux/QEMU
audit in `out/gta4l/storage-abi-2777bfce` (`gdb-stop.json` `8a3feec1…`,
`interface-audit.json` `9a7845cf…`, `provenance.json` `c89275ad…`,
`run.json` `f4430cf1…`) binds 190 slots, 181 kernel bindings (167 inherited
+ 14 pure virtual), 26 concrete overrides and 0 unresolved original imports,
with live sizes 0x90/0x88, native 4K and `runtime_qualified=false`.
Requalifying the archive against that audit produced `4a811585…`; all three
members are byte-identical to the kernel-build archive and the container
differs only in its index.

The packaged kernel is `525fdaa4…`. The payload is `8d57eb3d…` (83,886,080
bytes; manifest `86e6c447…`; 14/14 contract; diagnostic
`emmc-block-3-partcmp` mapping to `gta4l-emmc-block=3 gta4l-emmc-partcmp=1`;
hold root `327ac826…`, hold `fixture.json` `00363681…` with
`diagnostic=fs-timer-hold` and `hold_after_success=true`, trust cache
`30a72f78…`). The BOOT image is `1e767155…` (85,688,320 bytes; manifest
`969683cd…`; `boot-image-gate.json` `cfa4227f…`, 18/18, only the ramdisk
differs from the hardware-tested `ce4d76e8…` v5 baseline; the baseline kernel
section `a4b054e0…`, firmware `1e8a3132…` and boot DTB `dc4c719d…` are
unchanged).

The packaged kernel carries one `GTA4L-EMMC P5 PARTMAP` string, three
`GTA4L-EMMC P5 REPORT` strings, one `GTA4L-EMMC P5 READDETAIL` string, one
`GTA4L-EMMC P5 PARTOBS` string, one legacy `PART:` string, five
`gta4l-emmc-partcmp` strings and five `gta4l-emmc-partobs` strings, matching
the N9.7 packaged kernel plus the new detail line; there is no literal
`complete=`. The block suite passes 48 core scenarios/38 mutation controls
and 97 adapter scenarios with 23 adapter and eight policy mutation controls;
the payload suite passes 28 checks, the packager 11, and the boot-image,
storage-ABI, evidence-count, fixture-recipe and board checks pass.

The hardware `reason=8` cause remains unresolved; the read-detail capture is
host-only until a separately gated boot, and no buffer, segment-limit or
request-limit change is authorized. Findings:
`docs/emmc-n9.8-partcmp-readdetail-candidate-2026-11-08.md`.

### 5.3.69 N9.8 read-detail observation and DMA-error exit trace (2026-11-08)

Follow-up to §5.3.68. The owner approved the BOOT-only write of the N9.8
read-detail candidate and supplied the single authorized cold-boot report; the
owner confirms return to TWRP. The boot budget is consumed; no retry, reflash,
rollback, reset/repair, host reboot, commit or push was performed, and no
buffer, segment-limit or request-limit change is authorized.

Flash association: BOOT `1e767155…` (85,688,320 bytes), payload `8d57eb3d…`,
prelinked kernel `2777bfce…`, packaged kernel `525fdaa4…`, source
`Gta4lEmmcBlock.cpp` `440279b2…`, policy `Gta4lEmmcBlock.c` `f84d1bd2…`,
header `6cb1f7cd…` and requalified archive `4a811585…`. The post-flash p69
readback is `cf13acfc…` (candidate prefix `1e767155…` plus unchanged tail
`a28b4ea2…`); the pre-flash p69 `aaaf8f03…` is saved as
`boot-before-n9.8.img.xz` (`19ab50d7…`, decompressed `aaaf8f03…`). RECOVERY
`14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` are unchanged. The flash
manifest verifies 5/5.

The report returned `rc=0 stop=0 mode=3` with the real whole media selected
(`whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0`)
and the attached GUID scheme found. `PARTMAP` remains `gpt=0 reason=8 reads=1
blocks=1 fails=1 samples=0 cancel=0 complete=0`, and the new line reports
`READDETAIL: state=2 lba=0 blocks=1 bytes=512 status=3758097108 actual=0
desc_len=512 desc_dir=1`. `3758097108` is `0xe00002d4`, the actual
`kIOReturnDMAError` (`IOReturn.h:167`), not the `0xe00002ca`/`3758097098`
(`kIOReturnIOError`) used earlier under that name. DRV `online=1 wp=1 reads=10
blocks=29 fails=0 max=8` and XFER `req=29 c17=7 c18=11 c18b=22 cfail=0
stops=11 sfail=0 last_stop=0` are incidental scan requests; the comparator
request never reached the adapter; the adapter maps its own failures to
`kIOReturnIOError` and the request-check results to other codes, and never
returns `kIOReturnDMAError` (`package/Gta4lEmmcBlock.cpp:814-825,891-892`).
The driver `fails=0` is the adapter/core counter, not the
`IOBlockStorageDriver` error statistic.

Exit trace (host-only, pinned IOStorageFamily `67f9003c`): the only
`kIOReturnDMAError` completion of an `IOMedia` read before provider dispatch is
`IOBlockStorageDriver::breakUpRequest()` at `IOBlockStorageDriver.cpp:3272-3275`
when `IOBreaker::getBreakSize()` returns 0; `executeRequest()`/`doAsyncReadWrite`
are only reached later (`:3283`, `:3421`, `:1178`). `getBreakSize()` truncates
the accumulated size to the request block size (`:2988`). With
`kIOMaximumSegmentCountReadKey=1` (`package/Gta4lEmmcBlock.cpp:694`), a 512-byte
buffer whose physical segments are not foldable stops at the segment-count
check (`:2975-2980`) after counting only the first partial segment; a 488-byte
first segment then truncates to 0. `IOMapper::gSystem` is null on this
platform, so the second segment is not folded. The DWARF of the flashed object
`db6d2b74…` gives `sizeof(Gta4lEmmcBlock)=0x36a8` (13,992) with `partmapPmbr`
at `0x0e18` (3,608) in the page-multiple 16 KiB kalloc class, so the two
segments are 488 and 24 bytes. Allocation failures in `prepareRequest` or
`IOBreaker` return `kIOReturnNoMemory` (`:2584`, `:3326`, `:3474`), and the
pinned `IOMemoryDescriptor`/`IOMapper` paths produce no `kIOReturnDMAError`, so
the observed status selects the `breakSize==0` path rather than allocation
failure. `actual=0` is the default completion count and the adapter counters do
not move because dispatch is never reached.

Residual uncertainty: the host cannot observe whether the two page segments are
physically adjacent. If they were contiguous, the fold would produce 512 bytes
and the read would proceed; the trace selects the DMA-error exit but does not
measure that input. No driver correction is selected or authorized.

Host correction: `partcmp-read-failure` now injects `0xe00002d4` and expects
`status=3758097108`; the historical §5.3.67 paragraph and the N9.7 baseline row
keep their recorded `0xe00002ca` with an explicit correction note. The block
suite passes 48 core scenarios/38 mutation controls and 97 adapter scenarios
with 23 adapter and eight policy mutation controls. Findings:
`out/gta4l/ram-root-emmc-n9.8-hardware-evidence/observation-1/` (private,
gitignored). N9.6/N9.7/N9.8 `complete=0` are preserved and initialization
reliability remains `[U]`.

### 5.3.70 N9.9 page-aligned comparator read buffer (host-only, 2026-11-08)

Follow-up to §5.3.69. The owner approved the host-only correction selected by
the DMA-error trace: the comparator's read descriptors now always start on a
kernel page boundary. The advertised one-segment read limit, the request block
size and the `READDETAIL` line are unchanged; no segment-limit or
request-limit change is part of this batch.

Correction: `GTA4L_EMMC_BLOCK_PARTMAP_DMA_BYTES` is two blocks (1,024 bytes,
`package/Gta4lEmmcBlock.h:74`). In comparator mode `start()` allocates one
page-aligned buffer with `IOMallocAligned(size, PAGE_SIZE)`
(`package/Gta4lEmmcBlock.cpp:653-654`) and `free()` releases it with
`IOFreeAligned` (`:775-778`). `readPartmapSectors()` issues every comparator
read of at most 1,024 bytes from that buffer (`:1395-1397`) and copies the
bytes to the caller only after a full successful transfer (`:1444-1447`); a
short or failed transfer leaves the destination untouched. The bounce removes
the caller-layout dependency: the class members are not page-contained, and
the target `partmapPmbr` at `0x0e18` (3,608) previously produced the 488+24
page split. The bounce is allocated only for `gta4l-emmc-partcmp=1`; the
legacy and partobs paths are unchanged.

Host qualification: the seam now models the worst-case
`IOBlockStorageDriver::breakUpRequest()` constraint reconstructed in §5.3.69:
each virtual page is a separate physical segment, only the first segment is
taken, the break size is truncated to the 512-byte request block size, and a
zero break size returns `kIOReturnDMAError` before the adapter
(`scripts/sim-gta4l-emmc-block/adapter_harness.cpp:217-245`). The model is
opt-in per scenario. `partcmp-page-aligned` runs the full 25-read comparator
with the model enabled and requires zero rejected requests, zero unaligned
descriptors, `gpt=1 reason=0 media=80 match=1 complete=1` (`:1505-1519`).
`partcmp-page-split` calls `readPartmapSectors()` directly with a destination
at page offset 3,608 and requires the exact PMBR copy (`:1522-1540`); the new
`partcmp-bounce-skip` mutation restores the caller buffer and is rejected by
that scenario (`scripts/test-gta4l-emmc-block.py:498-501`). The block suite
passes 48 core scenarios/38 mutation controls and 99 adapter scenarios with
24 adapter and eight policy mutation controls; the evidence-count guard
passes (core 187/115, probe 196/100).

Limits: the host seam still calls `doAsyncReadWrite` directly, so the breakup
model is a host reconstruction rather than a qualified run of the real
context/deblock/breakup layers. Physical adjacency of the two page segments
remains unmeasured, and the correction is not hardware-verified. No candidate
build, storage-ABI/QEMU audit, device access, flash, boot, retry, reset/repair,
commit or push; N9.6/N9.7/N9.8 `complete=0` are preserved and initialization
reliability remains `[U]`. Findings:
`docs/emmc-n9.9-page-aligned-comparator-buffer-2026-11-08.md`.

### 5.3.71 N9.10 page-aligned comparator candidate production (host-only, 2026-11-08)

Follow-up to §5.3.70. N9.10 carried the page-aligned comparator correction
through the archive/prelink/audit/payload/BOOT flow and stopped at the
production record. The owner separately approved a BOOT-only flash and one
cold boot; the flash and observation are recorded in §5.3.72. No commit or
push was performed, and the N9.6/N9.7/N9.8 `complete=0` verdicts are not
upgraded.

The prelinked kernel is `fcc61714…` (51,257,344 bytes; plan `240ea822…`). The
kernel-build archive `b8557d3e…` records `Gta4lEmmcBlock.c` `f84d1bd2…`
compiled to `Gta4lEmmcBlock.o` `bf2c80ce…` and `Gta4lEmmcBlock.cpp`
`78144956…` compiled to `Gta4lEmmcBlockCpp.o` `44a5686d…`; the bindings
object is unchanged at `3e53b66d…`. A fresh candidate-matched Linux/QEMU
audit in `out/gta4l/storage-abi-fcc61714` (`gdb-stop.json` `e9e01873…`,
`interface-audit.json` `4f66b116…`, `provenance.json` `64e46c60…`,
`run.json` `94746dec…`) binds 190 slots, 181 kernel bindings (167 inherited
+ 14 pure virtual), 26 concrete overrides and 0 unresolved original imports,
with live sizes 0x90/0x88, native 4K and `runtime_qualified=false`.
Requalifying the archive against that audit produced `0575c413…`; all three
members are byte-identical to the kernel-build archive and the container
differs only in its index.

The packaged kernel is `149b1e6c…`. The payload is `800d293d…` (83,886,080
bytes; manifest `c600a8fd…`; 14/14 contract; diagnostic
`emmc-block-3-partcmp` mapping to `gta4l-emmc-block=3 gta4l-emmc-partcmp=1`;
hold root `327ac826…`, hold `fixture.json` `00363681…` with
`diagnostic=fs-timer-hold` and `hold_after_success=true`, trust cache
`30a72f78…`). The BOOT image is `dc79fab2…` (85,688,320 bytes; manifest
`057efbb5…`; `boot-image-gate.json` `3290d179…`, 18/18, only the ramdisk
differs from the hardware-tested `ce4d76e8…` v5 baseline; the baseline kernel
section `a4b054e0…`, firmware `1e8a3132…` and boot DTB `dc4c719d…` are
unchanged). The packaged kernel carries the same bounded report-string set as
N9.8 (one `PARTMAP`, three `REPORT`, one `READDETAIL`, one `PARTOBS`, one
legacy `PART:`, five each of the `partcmp`/`partobs` tokens, no literal
`complete=`).

The block suite passes 48 core scenarios/38 mutation controls and 99 adapter
scenarios with 24 adapter and eight policy mutation controls; the payload,
packager, boot-image, storage-ABI, evidence-count, fixture-recipe and board
checks pass, and `git diff --check` is clean. The host seam still calls
`doAsyncReadWrite` directly, so the breakup model and physical adjacency
remain host reconstructions. Findings:
`docs/emmc-n9.10-partcmp-page-aligned-candidate-2026-11-08.md`.

### 5.3.72 N9.10 exact-comparator hardware observation (2026-11-08)

Follow-up to §5.3.71. The owner-approved BOOT-only flash of `dc79fab2…`
(85,688,320 bytes) and one owner-controlled cold boot returned the first
hardware `complete=1` for the exact GPT-to-`IOMedia` comparator. The report
selected the real whole media (`whole=1 base=0 size=31268536320
content=GUID_partition_scheme writable=0`), found the attached GUID scheme and
validated both GPT copies (`pmbr=1 hdr=1 arr=1 bak=1 xcheck=1 slots=84
used=80`). All 80 qualifying media tuples matched the 80 used entries
(`media=80 match=1 missing=0 extra=0 dup=0 nested=0 prop=0`) and the set was
stable for 250 ms (`samples=9 changes=3 stable=1 timed_out=0 deadline_ms=2000
budget=0 cancel=0`). The comparator performed exactly 25 reads/45 blocks with
no failure: PMBR LBA 0, primary header LBA 1, 11 primary-array reads LBA 2-22,
backup header LBA 61,071,359 and 11 backup-array reads LBA
61,071,327-61,071,347 in two-block chunks. `READDETAIL state=4 lba=61071347
blocks=1 bytes=512 status=0 actual=512 desc_len=512 desc_dir=1` is the final
backup-array tail read, not an aggregate. `complete=1` is the guard at
`package/Gta4lEmmcBlock.cpp:2034-2039` and cannot coexist with a mismatch,
timeout, budget refusal, cancellation or read failure.

The N9.8 first-read `kIOReturnDMAError` (`status=3758097108`, `actual=0`) did
not recur on any N9.10 comparator read, which is direct hardware support that
the N9.9 page-aligned bounce buffer removed the observed pre-adapter
rejection. The N9.8 return site and the physical page adjacency remain host
reconstructions. This is a same-boot completeness observation on one cold
boot: it does not establish initialization reliability, a second matching
boot, filesystem mount, arbitrary I/O, writes or wider/faster modes.
N9.6/N9.7/N9.8 `complete=0` are preserved and initialization reliability
remains `[U]`. The flash readback `004b9d6d…` matched the candidate prefix
`dc79fab2…` plus the unchanged tail `a28b4ea2…`; RECOVERY/EFS/sec_efs were
unchanged and the rollback `3c7e9e5f…` restores the N9.8 image `cf13acfc…`.
No retry, reflash, rollback, reset/repair, commit or push. Findings:
`docs/emmc-n9.10-partcmp-observation-2026-11-08.md`.

### 5.3.73 N9.11 owner-reported unchanged-image repeatability (2026-11-08)

Follow-up to §5.3.72. After the captured observation-1 cold boot, the owner
reports three additional cold boots and three warm boots on the unchanged
image, every one showing `complete=1`, with no photos or full reports, no
intervening flash, and a return to TWRP. Totals are 4 cold and 3 warm boots
since the N9.10 flash; the three warm boots are outside the written N9.11
cold-boot protocol. Only observation-1 is captured: the six additional
outcomes are owner-reported, and no pre-series or post-series p69 or
protected-partition verification was performed. This supports descriptive
repeatability of the owner-observed same-boot completeness outcome on this
image; it does not verify the other report fields for the uncaptured boots,
does not establish initialization reliability, a failure rate or a statistical
bound, and does not qualify mount, arbitrary I/O, writes or wider/faster
modes. N9.6/N9.7/N9.8 `complete=0` are preserved and initialization reliability
remains `[U]`. No further boot is needed; any stronger claim needs a separately
approved read-only re-verification and/or captured-boot protocol. Findings:
`docs/emmc-n9.11-reliability-observation-2026-11-08.md`.

### 5.3.74 N10.1 read-only IOMedia content qualification candidate production (host-only, 2026-11-08)

Follow-up to §5.3.71. N10.1 adds an orthogonal read-only content
qualification selected by `gta4l-emmc-partread=1`. The selector accepts the
token only with exactly one `gta4l-emmc-block=1` or `=3` token, value exactly
`1`, no duplicate, and no `gta4l-emmc-partobs=1` or `gta4l-emmc-partcmp=1` in
the same boot; an orphan, duplicate, wrong value or combination is a refusal
with the existing `IOLog` wording rather than an inert fallback, and the
legacy report is unchanged when the token is absent.

The mode waits for exactly one whole-media `IOMedia`, validates the read-only
whole-media profile (absent `Base` accepted as zero, nonzero or wrong-typed
`Base` refused), opens it as a reader, resolves the boot partition by the
pinned base `2,255,360 × 512` and size `196,608 × 512`, and performs six
fixed-range reads: whole LBA 0/34 blocks, whole last 34 blocks, boot 64 blocks
at `BOOT_SIZE - 32768`, and boot 3/2/1-block tails. Each read goes through one
page-aligned 32 KiB `IOMallocAligned` bounce buffer, copies only after a full
successful transfer, and compares the first 16 bytes of a portable SHA-256
against a digest pinned from retained evidence. The first failing case latches
the reason; the counters keep every case that ran; `complete=1` requires all
six cases to pass with no cancellation, budget stop, media/partition/open
failure, short transfer or hash mismatch. The worst case
`MEDIA_WAIT + READ_BUDGET + 1000 + DEADLINE` stays below the 30,000 ms
watchdog, and the object stays below the 16 KiB `static_assert`.

The candidate chain: prelinked kernel `12e00d9f…` (51,277,824 bytes; plan
`83a8b81b…`); kernel-build archive `b137c2e4…` with `Gta4lEmmcBlock.c`
`9dad3d79…` compiled to `Gta4lEmmcBlock.o` `2402c895…` and
`Gta4lEmmcBlock.cpp` `6a11aa76…` compiled to `Gta4lEmmcBlockCpp.o`
`5f72925e…`; candidate storage-ABI audit `out/gta4l/storage-abi-12e00d9f`
(`gdb-stop.json` `275d693e…`, `interface-audit.json` `48d8d4a7…`,
`provenance.json` `99c15186…`, `run.json` `b9888e50…`; 190 slots, 181 kernel
bindings, 26 concrete overrides, 0 unresolved original imports, vtable
`0xfffffff005cb44c0`, service metaclass `0xfffffff007af75b8`,
`runtime_qualified=false`); requalified archive `5a59d055…` with
byte-identical members; packaged kernel `05fde642…`; payload `e2dc899d…`
(83,886,080 bytes, manifest `08ff16bf…`, 18/18 contract,
`emmc-block-3-partread` mapping to `gta4l-emmc-block=3
gta4l-emmc-partread=1` with the `fs-timer-hold` fixture) and BOOT image
`0cee22c9…` (85,688,320 bytes, manifest `31ce0993…`, 18/18 gate, only the
ramdisk differs from the `ce4d76e8…` v5 baseline).

The six pinned digests were re-verified before the build against
`lbas-0-33.bin`/`lbas-61071326-61071359.bin`, the retained `primary-gpt.bin`
and `backup-gpt.bin` prefixes and the boot-tail ranges of
`boot-backup-20261027-pmgr.img`; the retained GPT parses as 128 entries of
84 bytes with backup LBA 61,071,359, usable bounds 34/61,071,326 and the boot
entry at LBA 2,255,360 with 196,608 sectors. The packaged kernel carries one
`GTA4L-EMMC P5 PARTREAD`, one `READTEST`, one `READFAIL`, one `PARTMAP`, three
`REPORT`, one `READDETAIL`, one `PARTOBS`, one legacy `PART:` and six
`gta4l-emmc-partread` strings and no literal `complete=`. Host suites pass:
block 48 core scenarios/38 mutation controls and 114 adapter scenarios with
33 adapter and eight policy mutation controls; payload 18 contract checks;
packager, boot-image, storage-ABI, evidence-count (core 187/115, probe
196/100), fixture recipe and board checks. No flash, boot, device access,
commit or push was part of the production step; the separately approved
BOOT-only flash and one owner-reported cold boot were executed later and are
recorded in §5.3.75. They qualify only the six pinned ranges on that boot, not
arbitrary I/O, mount, writes, wider/faster modes or initialization
reliability. Findings:
`docs/emmc-n10.1-partread-candidate-2026-11-08.md`.

### 5.3.75 N10.1 read-only IOMedia content qualification hardware result (2026-11-08)

Follow-up to §5.3.74. The owner-approved BOOT-only flash wrote the N10.1 BOOT
image `0cee22c9…` (85,688,320 bytes) to p69 only; the post-flash full readback
`cc3c8492…` matched the candidate prefix `0cee22c9…` plus the preserved tail
`a28b4ea2…`, and RECOVERY/EFS/sec_efs were unchanged. One owner-reported cold
boot then returned the read-only whole-media profile (`whole=1 base=0
size=31268536320 content=GUID_partition_scheme writable=0`) and:

```
GTA4L-EMMC P5 PARTREAD: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6 blocks=138 bytes=70656 fails=0 cancel=0 budget=0 media=0 part=0 cap=61071360
```

The six pinned cases are whole LBA 0/34 blocks, whole last 34 blocks, and boot
64/3/2/1-block tails. `passed=6 failed=0 reason=0 complete=1` means all six
digests matched under the guard at `package/Gta4lEmmcBlock.cpp:2297-2299`;
`reads=6 blocks=138 bytes=70656` is exactly 34+34+64+3+2+1 blocks and
138 x 512 bytes. `READTEST index=5 … off=100662784 … digest=400039fd…` is the
final singleton boot-tail case, not an aggregate. `media=0 part=0` are failure
flags: the source sets `partread_media=1` only for a media reason and
`partread_part=1` only for a partition reason
(`package/Gta4lEmmcBlock.cpp:1969-1974`), so both zero means neither failure
occurred. `DRV reads=31 blocks=167` and `XFER req=167 c17=9 c18=79 c18b=158
stops=79` are whole-boot aggregates that include the six partread reads and
are not additive with the partread counters. The zero `READFAIL`, `PART`,
`READ`, `QUAL` and `QUAL3` lines are inactive legacy paths, and `SNAP
locked=1`/`FAIL valid=0` are unchanged. The supplied suffix
`@Gta41EmmcBlock.cpp:2642` preserves the supplied spelling; current source
line 2642 is the intentional terminal `panic("%s", reportBuffer)` in
`panicWithReport()`, not a fault.

An independent read-only post-boot collection then matched the stored BOOT
full/prefix/tail hashes, found RECOVERY/EFS/sec_efs unchanged, and passed
18/18 boot-image and 18/18 payload checks on the pulled prefix and extracted
payload. This confirms the stored content and protected-partition
preservation; it is not a second boot. The result is one same-boot
qualification of six pinned ranges and does not establish arbitrary I/O,
mount, writes, wider/faster modes, DMA/CQE/ICE or initialization reliability.
N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10 comparator result and the N9.11
owner-reported repeatability series are preserved. The owner returned to
TWRP; the one-boot budget is consumed and no retry, reflash, rollback,
reset/repair, commit or push was performed. Findings:
`docs/emmc-n10.1-partread-hardware-result-2026-11-08.md`.

### 5.3.76 N10.1 captured repeatability series (2026-11-08)

Follow-up to §5.3.74-§5.3.75. The owner approved the captured repeatability
protocol (`docs/emmc-n10.1-partread-repeatability-protocol-2026-11-08.md`)
section 10 with three additional owner-controlled cold boots. No rebuild,
reflash, rollback, reset/repair or host-issued reboot occurred; the unchanged
image stayed on p69. Observations 2-4 were supplied as screen transcriptions
(fully cold, about 10-20 seconds off, in boot order, TWRP return after each)
and all three were class D:

```
GTA4L-EMMC P5 PARTREAD: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6 blocks=138 bytes=70656 fails=0 cancel=0 budget=0 media=0 part=0 cap=61071360
```

`READTEST` remained the final pinned case (`index=5 … off=100662784 …
digest=400039fd…`), `READFAIL` was zero-filled and `FAIL valid=0`. Together
with observation 1, four captured cold-boot observations returned the
identical six-of-six qualification. Incidental variation was limited to the
whole-boot `DRV reads` aggregate (31 in observation 1, 30 in observations
2-4) and the panic `pages`/`free` counters; no qualification field changed.
The approved read-only pre-series and post-series checks matched p69
full/prefix/tail and RECOVERY/EFS/sec_efs, and the tablet returned to TWRP
after each boot. The series supports descriptive repeatability only: it does
not establish initialization reliability, a failure rate, arbitrary I/O,
filesystem mount, writes, wider/faster modes, DMA/CQE/ICE or the real
context/deblock/breakup layers. N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10
comparator result and N10.1 observation 1 are preserved; initialization
reliability remains `[U]`. The three-boot budget is consumed and no further
boot is authorized. Findings:
`docs/emmc-n10.1-partread-repeatability-observation-2026-11-08.md`.

### 5.3.77 N10.2 read-only IOMedia content qualification expansion (host-only, 2026-11-08)

Follow-up to §5.3.74 and §5.3.76. N10.2 expands the pinned partread case table
from six to fourteen without changing the selector, media/partition discovery,
page-aligned bounce buffer, digest comparison, reason codes or `complete=1`
guard. Cases 2-7 split the whole-media coverage into the PMBR only (LBA 0, one
block), the primary GPT header only (LBA 1), the full primary array (LBA 2, 32
blocks), the first usable LBA (LBA 34), the full backup array (32 blocks at
`capacity - 33`) and the backup GPT header (the last block); cases 12-13 add
64-block reads at boot offsets 4,096 and 1,048,576 inside the 1,798,052-byte
kernel section. The boot-tail cases 8-11 are unchanged. The run is
`reads=14 blocks=334 bytes=171008`; the last case is index 13, target boot,
64 blocks at offset 1,048,576, 32,768 actual bytes.

The digests were re-verified before the build: cases 0-7 against
`lbas-0-33.bin`, `lba-34.bin` and `lbas-61071326-61071359.bin`; cases 8-11
against the retained full BOOT partition backup `boot-backup-20261027-pmgr.img`
(100,663,296 bytes); cases 12-13 against the v5 BOOT image (85,688,320 bytes),
whose kernel section `a4b054e0…` is the boot-image gate baseline. Cases 8-11
address the end of the full partition, which the shorter image does not reach;
the image's own last 32 KiB (`c3502047…`) is not a case digest. Case 6 shares
case 4's array digest (`ecde9f6f…e272aa`).

The candidate chain: prelinked kernel `e7a1982b…` (51,277,824 bytes; plan
`1a84cff2…`); kernel-build archive `cb3d6df0…` with `Gta4lEmmcBlock.c`
`62d1d706…` compiled to `Gta4lEmmcBlock.o` `7abc97d6…` and
`Gta4lEmmcBlock.cpp` `6a11aa76…` compiled to `Gta4lEmmcBlockCpp.o`
`0c5c1b96…`; candidate storage-ABI audit `out/gta4l/storage-abi-e7a1982b`
(`gdb-stop.json` `f2bdc155…`, `interface-audit.json` `88ebe8a3…`,
`provenance.json` `c3a98a7e…`, `run.json` `a26f1d3a…`; 190 slots, 181 kernel
bindings, 26 concrete overrides, 0 unresolved original imports, vtable
`0xfffffff005cb44c0`, service metaclass `0xfffffff007af75b8`,
`runtime_qualified=false`); requalified archive `2ea638ef…` with
byte-identical members; packaged kernel `d987c350…`; payload `462870cd…`
(83,886,080 bytes, manifest `b6439f2a…`, 18/18 contract,
`emmc-block-3-partread` mapping to `gta4l-emmc-block=3
gta4l-emmc-partread=1` with the `fs-timer-hold` fixture) and BOOT image
`ca1789f0…` (85,688,320 bytes, manifest `c3272820…`, 18/18 gate, only the
ramdisk differs from the `ce4d76e8…` v5 baseline).

The packaged kernel token scan is unchanged from N10.1 (one `PARTREAD`, one
`READTEST`, one `READFAIL`, one `PARTMAP`, three `REPORT`, one `READDETAIL`,
one `PARTOBS`, one legacy `PART:` and six `gta4l-emmc-partread` strings, no
literal `complete=`). Host suites pass: block 48 core scenarios/38 mutation
controls and 114 adapter scenarios with 33 adapter and eight policy mutation
controls; core 187/115, probe 196/100, payload, packager, boot-image,
storage-ABI, evidence-count, fixture recipe and board checks. The adapter
expectations pin `cases=14 passed=14 reads=14 blocks=334 bytes=171008` and
`READTEST index=13 target=1 blocks=64 off=1048576 actual=32768`, plus the
expanded failure shapes (hash mismatch `passed=13 failed=1 reason=7`; boot
partition absent after eight whole-media cases `passed=8 reads=8 blocks=136
bytes=69632 part=1`; descriptor failure `passed=0 failed=14 reason=4`). No
flash, boot, device access, commit or push was part of N10.2; a BOOT-only
flash and cold boot need fresh explicit owner approval. N10.1's hardware and
repeatability results are preserved and initialization reliability remains
`[U]`. Findings: `docs/emmc-n10.2-partread-candidate-2026-11-08.md`.

### 5.3.78 N10.2 read-only IOMedia content qualification hardware result (2026-11-08)

Follow-up to §5.3.77. The owner-approved BOOT-only flash wrote the N10.2 BOOT
image `ca1789f0…` (85,688,320 bytes) to p69 only; the post-flash full readback
`4225defb…` matched the candidate prefix `ca1789f0…` plus the preserved tail
`a28b4ea2…`, and RECOVERY/EFS/sec_efs were unchanged. One owner-reported cold
boot then returned the read-only whole-media profile (`whole=1 base=0
size=31268536320 content=GUID_partition_scheme writable=0`) and:

```
GTA4L-EMMC P5 PARTREAD: cases=14 passed=14 failed=0 reason=0 complete=1 reads=14 blocks=334 bytes=171008 fails=0 cancel=0 budget=0 media=0 part=0 cap=61071360
```

The fourteen pinned cases are the N10.1 whole-media and boot-tail cases plus
PMBR-only, primary GPT header, full primary array, LBA 34, full backup array,
backup GPT header and two 64-block kernel-section ranges at 4,096 and
1,048,576. `passed=14 failed=0 reason=0 complete=1` means all fourteen digests
matched under the guard at `package/Gta4lEmmcBlock.cpp:2297-2299`;
`reads=14 blocks=334 bytes=171008` is exactly the fourteen cases and
334 x 512 bytes. `READTEST index=13 … off=1048576 … actual=32768
digest=050f2b0d…` is the final kernel-section case, not an aggregate.
`media=0 part=0` are failure flags: the source sets `partread_media=1` only
for a media reason and `partread_part=1` only for a partition reason
(`package/Gta4lEmmcBlock.cpp:1970-1973`), so both zero means neither failure
occurred. `DRV reads=58 blocks=363` and `XFER req=363 c17=13 c18=175 c18b=350
stops=175` are whole-boot aggregates that include the fourteen partread reads
and are not additive with the partread counters. The zero `READFAIL`, `PART`,
`READ`, `QUAL` and `QUAL3` lines are inactive legacy paths, and `SNAP
locked=1`/`FAIL valid=0` are unchanged. The supplied suffix
`@Gta41EmmcBlock.cpp:2642` preserves the supplied spelling; current source
line 2642 is the intentional terminal `panic("%s", reportBuffer)` in
`panicWithReport()`, not a fault.

An independent read-only post-boot collection then matched the stored BOOT
full/prefix/tail hashes, found RECOVERY/EFS/sec_efs unchanged, and passed
18/18 boot-image and 18/18 payload checks on the pulled prefix and extracted
payload. This confirms the stored content and protected-partition
preservation; it is not a second boot. The result is one same-boot
qualification of fourteen pinned ranges and does not establish arbitrary I/O,
mount, writes, wider/faster modes, DMA/CQE/ICE or initialization reliability.
N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10 comparator result, the N10.1
hardware result and its captured repeatability series are preserved. The
owner returned to TWRP; the one-boot budget is consumed and no retry, reflash,
rollback, reset/repair, commit or push was performed. Findings:
`docs/emmc-n10.2-partread-hardware-result-2026-11-08.md`.

### 5.3.79 N10.4 unaligned deblocked read qualification (host-only, 2026-11-08)

Follow-up to §5.3.74-§5.3.78 and the owner-approved
`docs/emmc-n10.3-deblock-qualification-design-2026-11-08.md`. N10.4 implements
the opt-in `gta4l-emmc-deblock=1` mode: the token requires exactly one block
token 1/3 and refuses a duplicate, orphan, non-1 value and every
partobs/partcmp/partread combination, while the absent-token legacy, partobs,
partcmp and partread reports stay byte-for-byte unchanged. The mode sets
`kIOMaximumSegmentCountReadKey` to 3 only for itself (the other modes keep 1)
and allocates one page-aligned 33,792-byte `IOMallocAligned` bounce buffer
(66 blocks), freed with `IOFreeAligned()`.

The eleven pinned cases read whole-media offsets 100/924, 0/600, 100/200,
100/600, 100/16000 and 512/1024 (the aligned control), and boot offsets
4196/924, 1048676/200, 100630528/32668, 100662884/200 and 4096/1024. Cases
0-4 exercise head-only, tail-only, both ends inside one block, both ends
across two blocks and both ends across a multi-page 32-block span; cases 6-9
exercise the boot base with head-only, same-block, 64-block tail-only and
same-block-at-media-end reads; cases 5 and 10 are aligned controls that
construct no deblocker. Each case is one `IOMedia::read()` with a descriptor
of exactly the requested length. The existing `doAsyncReadWrite()` issues the
per-chunk read and `copyOutCallback()` writes the qualified bytes into the
`IOBreaker`/`IODeblocker` descriptor, and the host seam models the head/tail
chunks, the break-size computation, the three-segment descriptor and the
deblocker's excess discard. The digest is the first 16 bytes of a portable
SHA-256 over the exact requested bytes, compared to a pinned 32-byte digest
derived from the retained GPT, BOOT-backup and v5 kernel-section evidence.

The full-pass line is `GTA4L-EMMC P5 DEBLOCK: cases=11 passed=11 failed=0
reason=0 complete=1 reads=11 bytes=54364 span=56832 blocks=111 fails=0
cancel=0 budget=0 media=0 part=0 seg=3 cap=61071360`; the last case also emits
`GTA4L-EMMC P5 DEBLOCKREAD: index=10 target=1 start=4096 length=1024
span=1024 actual=1024 status=0 digest=...`. `reads`, `bytes`, `span` and
`blocks` are the attempted cases' requested bytes, aligned deblocker bytes and
`span / 512`; `seg=3` is the mode's advertised read segment count and
`cap=61071360` is the capacity. `complete=1` requires the eleven-case table,
all passes, `reason=0`, clear `cancel`/`budget`/`media`/`part` flags, `seg=3`
and every status/actual/digest check. The first failure latches `reason` (1
media, 2 partition, 3 open, 4 descriptor, 5 read, 6 short, 7 hash, 8 budget,
9 cancel, 10 argument), and the remaining cases continue after
read/short/hash/descriptor/argument failures. Cancellation sets `reason=9`
and `cancel=1` and returns through the existing no-panic path. The host tests
bound the `DEBLOCK` line at 384 bytes and `DEBLOCKREAD` at 256 bytes and
assert the full report stays below 2,048 bytes; the formatter keeps its
fail-closed behavior, and the report prints only bounded counts, offsets,
lengths, statuses and a 16-byte digest prefix.

The diagnostic payload builder, checker and packager carry the
`emmc-block-1-deblock`/`emmc-block-3-deblock` personalities, mapping to
`gta4l-emmc-block=1|3 gta4l-emmc-deblock=1`. The checker refuses an orphan,
duplicate or non-1 deblock token and every partobs/partcmp/partread
combination, and the builder requires the `fs-timer-hold` data-ramdisk
fixture for the new personalities. Host suites pass: block 48 core
scenarios/38 mutation controls and 143 adapter scenarios with 45 adapter,
three support and eight policy mutation controls; diagnostic payload 34
checks; packager self-test 13 checks; payload-checker self-test; and
evidence-count core 187/115, probe 196/100. The host deblock model is a
reconstruction: it does not run the real `IOBlockStorageDriver`/`IODeblocker`
code, so the hardware run is the only qualification of those layers. N10.4
performed no candidate build, ABI/QEMU audit, device access, flash or boot;
N10.5 produced the candidate and N10.6 then completed on one cold boot
(§5.3.80, §5.3.81), the N10.1/N10.2 hardware results are preserved, and
initialization reliability remains `[U]`. Findings:
`docs/emmc-n10.3-deblock-qualification-design-2026-11-08.md`.

### 5.3.80 N10.5 unaligned deblocked read candidate production (host-only, 2026-11-08)

Follow-up to §5.3.79. N10.5 builds and gates the N10.4 deblock mode without
changing the selector, case table, bounce buffer, digests, report lines or
guards. Candidate: prelinked kernel `b2837a5b…` (51,294,208 bytes; plan
`a834fb1d…`), kernel-build archive `284323e6…` with `Gta4lEmmcBlock.c`
`4cfbc466…` compiled to `Gta4lEmmcBlock.o` `dddd7761…` and
`Gta4lEmmcBlock.cpp` `2721a6a7…` compiled to `Gta4lEmmcBlockCpp.o`
`c1d9335c…`; candidate storage-ABI audit `out/gta4l/storage-abi-b2837a5b`
(`gdb-stop.json` `7ca3d434…`, `interface-audit.json` `ab4113dd…`,
`provenance.json` `943f8bdf…`, `run.json` `a3a1010b…`; 190 method slots, 181
kernel bindings, 26 concrete overrides, 0 unresolved original imports,
vtable `0xfffffff005cb44c0`, metaclass `0xfffffff006add1d0`, service
metaclass `0xfffffff007afb5b8`, `runtime_qualified=false`); requalified
archive `f3789061…` (865,448 bytes) with byte-identical members; packaged
kernel `00615136…`; payload `924c7bbd…` (83,886,080 bytes, manifest
`ddbb77f1…`, gate `fbfdf923…`, 23/23 contract, `emmc-block-3-deblock`
mapping to `gta4l-emmc-block=3 gta4l-emmc-deblock=1`, hold root `327ac826…`
and fixture `00363681…`); and BOOT image `bacc933f…` (85,688,320 bytes,
manifest `95670b8e…`, gate `9c3d0558…`, 18/18, only the ramdisk differs from
the `ce4d76e8…` v5 baseline; kernel section `a4b054e0…`, firmware
`1e8a3132…` and DTB `dc4c719d…` unchanged; header `f369571b…`). The eleven
pinned digests were re-derived from `lbas-0-33.bin` (cases 0-5), the v5 image
(cases 6, 7 and 10) and the full boot-partition backup (cases 8-9); case 9
shares case 2's zero-filled digest. The packaged kernel carries three
`GTA4L-EMMC P5 REPORT` strings, one `PARTMAP`, one `READDETAIL`, one
`PARTOBS`, one `PARTREAD`, one `READTEST`, one `READFAIL`, two `DEBLOCK`, one
`DEBLOCKREAD`, one legacy `PART:`, seven each
`gta4l-emmc-partread`/`partcmp`/`partobs`/`deblock` strings and no literal
`complete=`. Host suites pass: block 48 core scenarios/38 mutation controls
and 143 adapter scenarios with 48 adapter/support and eight policy mutation
controls; core 187/115, probe 196/100, payload 34, packager 13, boot-image
self-test, storage-ABI positive/negative, evidence counts, fixture recipe,
board and TOML checks. No flash, boot, device access, commit or push while
producing the candidate; N10.6 then completed on one owner-reported cold boot
(§5.3.81) and initialization reliability remains `[U]`. Findings:
`docs/emmc-n10.5-deblock-candidate-2026-11-08.md`.

### 5.3.81 N10.6 unaligned deblocked read hardware result (2026-11-08)

Follow-up to §5.3.80. The owner approved a BOOT-only write of the N10.5 BOOT
image `bacc933f…` (85,688,320 bytes; payload `924c7bbd…`, prelinked kernel
`b2837a5b…`, packaged kernel `00615136…`, requalified archive `f3789061…`) to
p69 and controlled one cold boot. Only `/dev/block/mmcblk0p69` was written
(20,920 x 4096 records, synced). The pre-flash p69 was the N10.2 post-flash
image `4225defb…` with tail `a28b4ea2…`; the post-flash full readback
`f4882047…` matched the candidate prefix `bacc933f…` plus the unchanged tail,
and RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` were
unchanged. The rollback image `boot-before-n10.6.img.xz` (`0a0fe70a…` -> raw
`4225defb…`) was not restored.

The owner report returned `rc=0 stop=0 mode=3` with the read-only whole media
and `DEBLOCK: cases=11 passed=11 failed=0 reason=0 complete=1 reads=11
bytes=54364 span=56832 blocks=111 fails=0 cancel=0 budget=0 media=0 part=0
seg=3 cap=61071360`; the last case emitted `DEBLOCKREAD: index=10 target=1
start=4096 length=1024 span=1024 actual=1024 status=0
digest=b50aab9d52e4d1e36078fa5120e3aa1c`, the first 16 bytes of the pinned
case-10 digest `b50aab9d…aa959774` at `package/Gta4lEmmcBlock.c:258-265`.
Every pinned digest matched. `complete=1` is the guard at
`package/Gta4lEmmcBlock.cpp:2835-2845`; `media=0 part=0` are clear failure
flags; `seg=3` is the mode-scoped advertisement at
`package/Gta4lEmmcBlock.cpp:864-867`.

The whole-boot counters are `DRV: online=1 wp=1 reads=22 blocks=140 fails=0
max=24` and `XFER: req=140 c17=12 c18=64 c18b=128 cfail=0 stops=64 sfail=0
last_stop=0`. `max=24` is exactly three 4 KiB physical segments (3 x 8
blocks), matching the three-segment advertisement; the one-segment
partread/comparator modes fold only the first segment and their reports show
`max=8`. Because case 8's aligned span is 64 blocks, a direct
`executeRequest()` short-circuit would have passed 64 blocks; the observed
maximum therefore shows the real `IOBreaker::getBreakSize()` /
`breakUpRequest()` split path was entered. This is the first recorded N-series
hardware observation of the three-segment fold and of a transport request
larger than the eight-block one-segment break. The transport issued 12 CMD17
single-block reads and 64 two-block CMD18 chunks with one successful CMD12
stop per chunk; the 29-block difference from the 111-block case-table span is
the deblocker's head/tail scratch reads and block rounding.

No post-boot readback is recorded: the owner returned the tablet to TWRP after
the one cold boot and no further device access is authorized, so the stored
image after the boot is not independently re-verified in this record. The
report is bound to the candidate by the pre-boot post-flash readback, the
read-only target-binding re-verification and the pinned digest match. This is
a bounded same-boot qualification of eleven pinned unaligned ranges and the
real deblock/breakup layers on one boot, not an initialization-reliability or
general I/O result; N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10 comparator
result, the N10.1 hardware/repeatability results and the N10.2 result are
preserved. The one-boot budget is consumed; no retry, reflash, rollback,
reset/repair, mount, commit or push was performed. Findings:
`docs/emmc-n10.6-deblock-hardware-result-2026-11-08.md`.

### 5.3.82 N10.8 deterministic breakup qualification implementation (host-only, 2026-11-09)

Follow-up to §5.3.79-§5.3.81 and the owner-approved
`docs/emmc-n10.7-breakup-qualification-design-2026-11-09.md`. N10.8 implements
the opt-in `gta4l-emmc-breakup=1` mode in `package/Gta4lEmmcBlock.h`/`.c`/
`.cpp` without a candidate build, ABI/QEMU audit, device access, flash or
boot. The token requires exactly one block token 1/3 and refuses a duplicate,
an orphan, a non-1 or malformed value and every partobs/partcmp/partread/
deblock combination; the absent-token legacy, partobs, partcmp, partread and
deblock reports stay byte-for-byte unchanged. The mode advertises
`kIOMaximumBlockCountReadKey=8` and `kIOMaximumByteCountReadKey=4,096` only
for itself (the other modes keep 64/32,768) and
`kIOMaximumSegmentCountReadKey=3`; it allocates one page-aligned 65,536-byte
`IOMallocAligned` bounce buffer covering the largest requested length and the
largest aligned span, freed with `IOFreeAligned()`.

The four pinned cases are boot-partition reads at `4096/4096`, `4096/65536`,
`4096/65024` and `4196/65024`. They cover the direct `executeRequest()`
short-circuit (case 0), the exact 16-stage eight-block split (case 1), the
15-stage split with a 7-block final stage (case 2) and the unaligned split
through the deblocker's three-chunk descriptor (case 3). Each case carries a
full 32-byte SHA-256 derived from the gate-pinned v5 BOOT image kernel
section; the report prints the first 16 bytes. The expected full-pass line is
`BREAKUP: cases=4 passed=4 failed=0 reason=0 complete=1 reads=4 bytes=199680
span=200192 blocks=391 subrequests=49 max=8 foreign=0 fails=0 cancel=0
budget=0 media=0 part=0 limit=8 seg=3 cap=61071360`, and the first failing
case otherwise supplies `BREAKUPREAD`.

The report thread records the active case under `reportLock` before each
`media->read()` and clears it after the call; it never holds the lock across
the media call. The adapter's `doAsyncReadWrite()` calls the mode-scoped
`breakupObserve()` after the null/zero checks and before taking the
controller `gate`. The observer takes `reportLock` briefly, classifies the
request by case tag (direct path) or tag-0 block range (deblocker path),
records subrequest count/block total/maximum/order, counts unattributable
requests as `foreign`, does not retain the buffer and performs no I/O; there
is no lock inversion. The first failure latches its case index, reason,
status, actual, observed decomposition and digest prefix; later
read/short/hash/descriptor/split/order failures continue, while media,
partition, open, budget and cancellation stop the run. `complete=1` requires
all four cases to pass with `reason=0`, `cancel=0`, `budget=0`, `media=0`,
`part=0`, `foreign=0`, `limit=8` and `seg=3`. The worst case is
20,000 + 6,000 + 2,000 + 1,000 = 29,000 ms, below the 30,000 ms watchdog, and
the formatter keeps the `BREAKUP` line at or below 448 bytes, the
`BREAKUPREAD` line at or below 320 bytes and the whole report below 2,048
bytes before NUL termination with fail-closed behavior unchanged.

Host results: the block suite passes 48 core scenarios/38 mutation controls
and 178 adapter scenarios with 58 adapter, three support and eight policy
mutation controls. The adapter coverage includes the four-case full pass, the
production 64-block control (case 0 is one 8-block subrequest; cases 1-3
split at 64 blocks and fail with `SPLIT`/`ORDER`), the `seg=1` anti-weakening
control (case 3 fails with `kIOReturnDMAError` before the adapter), wrong
decomposition (missing/extra/reordered/overlapping/oversized/single
subrequest), foreign traffic, read/short/hash/descriptor/range/zero-length
failures, budget, cancellation, media/partition/open failures, the watchdog
race and the selector/property/report controls. Diagnostic payload tests pass
37 checks and the packager self-test 14 checks, including both
`emmc-block-1-breakup` and `emmc-block-3-breakup` personalities, the
`fs-timer-hold` fixture requirement and the exclusivity refusals. The host
model reconstructs the `IOBreaker`/`IODeblocker` split; it does not execute
the real family code, so N10.9 candidate production and N10.10 hardware
remain separately gated, N10.6 remains the only breakup-layer hardware
observation and initialization reliability remains `[U]`. Findings:
`docs/emmc-n10.7-breakup-qualification-design-2026-11-09.md`.

### 5.3.83 N10.9 deterministic breakup qualification candidate (host-only, 2026-11-09)

Follow-up to §5.3.82 and the N10.7 design. N10.9 builds and gates the N10.8
opt-in `gta4l-emmc-breakup=1` mode through the archive/prelink/audit/payload/
BOOT flow without changing the selector, four-case table, page-aligned
65,536-byte bounce buffer, pinned digests, `BREAKUP`/`BREAKUPREAD` report
lines or `complete=1` guard. The candidate chain is prelinked kernel
`71828829…` (51,331,072 bytes; plan `2cee429c…`), kernel-build archive
`8e636a01…`, candidate storage-ABI audit `out/gta4l/storage-abi-71828829`
(`gdb-stop.json` `20ac48a4…`, `interface-audit.json` `d4ac2abf…`,
`provenance.json` `eefbc490…`, `run.json` `3bb349bf…`; 190 method slots,
181 kernel bindings, 26 concrete overrides, 0 unresolved original imports,
vtable `0xfffffff005cb44c0`, service metaclass `0xfffffff007b035b8`,
`runtime_qualified=false`), requalified archive `8e636a01…` with
byte-identical members, packaged kernel `a8a5a609…`, payload `706cfd53…`
(83,886,080 bytes; manifest `e9fdba1c…`; gate `84bd1ab7…`; 29/29 contract;
`emmc-block-3-breakup` mapping to `gta4l-emmc-block=3
gta4l-emmc-breakup=1`; hold root `327ac826…`, fixture `00363681…`) and BOOT
image `a8f4a54b…` (85,688,320 bytes; manifest `ee878222…`; gate `39571235…`;
18/18, only the ramdisk differs from the `ce4d76e8…` v5 baseline; kernel
section `a4b054e0…`, firmware `1e8a3132…` and DTB `dc4c719d…` unchanged).
The four pinned digests were re-derived from the gate-pinned v5 image kernel
section; the ranges are inside the 1,798,052-byte kernel section and are
invariant across candidates. The packaged kernel carries the two
`BREAKUP`/`BREAKUPREAD` lines and eight `gta4l-emmc-breakup` strings with no
literal `complete=`.

Host suites re-passed: block 48 core scenarios/38 mutation controls and 178
adapter scenarios with 58 adapter, three support and eight policy mutation
controls (including the thirteen breakup mutations), core 187/115, probe
196/100, payload 37, packager 14, boot-image self-test, storage-ABI
positive/negative, evidence counts, fixture recipe, board and TOML checks.
No flash, boot, device access, commit or push was performed while producing
the candidate; N10.8's host implementation is unchanged, N10.10 remains
separately gated, N10.6 remains the only breakup-layer hardware observation
and initialization reliability remains `[U]`. Findings:
`docs/emmc-n10.9-breakup-candidate-2026-11-09.md`.

### 5.3.84 N10.10 deterministic breakup hardware result (2026-11-09)

Follow-up to §5.3.83. The owner approved a BOOT-only write of the N10.9 BOOT
image `a8f4a54b…` (85,688,320 bytes; payload `706cfd53…`, prelinked kernel
`71828829…`, packaged kernel `a8a5a609…`, requalified archive `8e636a01…`) to
p69 and controlled one cold boot. Only `/dev/block/mmcblk0p69` was written
(20,920 x 4096 records, synced). The pre-flash p69 was the N10.6 post-flash
image `f4882047…` with prefix `bacc933f…` and tail `a28b4ea2…`; the post-flash
full readback `4b51ff21…` matched the candidate prefix `a8f4a54b…` plus the
unchanged tail, and RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs
`3a265d58…` were unchanged. The rollback image `boot-before-n10.10.img.xz`
(`70312a39…` -> raw `f4882047…`) was not restored.

The owner report returned `rc=0 stop=0 mode=3` with the read-only whole media
and `BREAKUP: cases=4 passed=4 failed=0 reason=0 complete=1 reads=4
bytes=199680 span=200192 blocks=391 subrequests=49 max=8 foreign=0 fails=0
cancel=0 budget=0 media=0 part=0 limit=8 seg=3 cap=61071360`; the last case
emitted `BREAKUPREAD: index=3 target=1 start=4196 length=65024 span=65536
actual=65024 status=0 subrequests=16 blocks=128 max=8
digest=bc0b9bb9c6dd11c3aea665d01dc5f108`, the first 16 bytes of the pinned
case-3 digest `bc0b9bb9…26db17` at `package/Gta4lEmmcBlock.c:322-331`.
Every pinned digest matched. The aggregate is the four-case table: 4,096 +
65,536 + 65,024 + 65,024 = 199,680 requested bytes, 200,192 aligned bytes,
391 blocks and 1 + 16 + 16 + 16 = 49 driver-level subrequests. `complete=1` is
the guard at `package/Gta4lEmmcBlock.cpp:3559-3572`; `media=0 part=0` are
clear failure flags; `limit=8` and `seg=3` are the mode-scoped advertisements
at `package/Gta4lEmmcBlock.cpp:961-980`.

The whole-boot counters are `DRV: online=1 wp=1 reads=58 blocks=420 fails=0
max=8` and `XFER: req=420 c17=8 c18=206 c18b=412 cfail=0 stops=206 sfail=0
last_stop=0`. `max=8` is the mode-scoped eight-block/4,096-byte cap; the N10.6
deblock-mode 24-block three-segment fold remains its own observation and is
not expected in breakup mode. Because case 1 and case 3 span 128 blocks, a
direct `executeRequest()` short-circuit would have passed 128 blocks; the
observed maximum and the 49 subrequests show the real
`IOBreaker::getBreakSize()` / `breakUpRequest()` split path was entered. The
transport issued 8 CMD17 single-block reads and 206 two-block CMD18 chunks
with one successful CMD12 stop per chunk; the 29-block difference from the
391-block case-table span is the deblocker's head/tail scratch reads and block
rounding. The `@Gta41EmmcBlock.cpp:3899` suffix (spelling preserved) is the
intentional `panic("%s", reportBuffer)` terminal report stop, the same class
as N10.6's `:3172`.

No post-boot readback is recorded: the owner returned the tablet to TWRP after
the one cold boot and no further device access is authorized, so the stored
image after the boot is not independently re-verified in this record. The
report is bound to the candidate by the pre-boot post-flash readback, the
read-only target-binding re-verification and the pinned digest match. This is
a bounded same-boot qualification of four pinned boot-partition ranges and the
real breakup path on one boot, not an initialization-reliability or general
I/O result; N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10 comparator result, the
N9.11 owner-reported series, the N10.1/N10.2 partread results and the N10.6
deblock result are preserved, and initialization reliability remains `[U]`.
The one-boot budget is consumed; no retry, reflash, rollback, reset/repair,
mount, commit or push was performed. Findings:
`docs/emmc-n10.10-breakup-hardware-result-2026-11-09.md`.

### 5.3.85 N10.12 bounded aligned read sweep implementation (host-only, 2026-11-09)

Follow-up to §5.3.82-§5.3.84 and the owner-approved
`docs/emmc-n10.11-bounded-read-sweep-design-2026-11-09.md`. N10.12 implements
the opt-in `gta4l-emmc-sweep=1` mode in `package/Gta4lEmmcBlock.h`/`.c`/
`.cpp` without a candidate build, ABI/QEMU audit, device access, flash or
boot. The token requires exactly one block token 1/3 and refuses a duplicate,
an orphan, a non-1 or malformed value and every partobs/partcmp/partread/
deblock/breakup combination; the absent-token legacy, partobs, partcmp,
partread, deblock and breakup reports stay byte-for-byte unchanged. The mode
advertises `kIOMaximumBlockCountReadKey=8`,
`kIOMaximumByteCountReadKey=4,096` and
`kIOMaximumSegmentCountReadKey=1` only for itself (the other modes keep their
limits); it allocates two page-aligned 16,384-byte `IOMallocAligned` buffers,
one for the media read and one for the direct CMD17 reference, both freed with
`IOFreeAligned()`.

The six cases are two pinned boot-partition anchors, A0 at `4096/4096`
(digest prefix `5878bcf8…`) and A1 at `4096/16384` (digest prefix `fa43…`),
and four runtime windows derived from the discovered boot-partition block
count `P = 196,608`: R0 32 blocks at start block 51,528, R1 16 blocks at
102,928, R2 8 blocks at 154,296 and R3 8 blocks at 9,128. Each runtime start
is `((i + 1) * 104729) % ((P - W) / 8) * 8` blocks, and the whole table is
validated before any case I/O; production refuses a `P` other than the pinned
boot-partition size. Every case is read once through the real media path into
the media buffer and once by direct per-block CMD17 under the controller gate
into the reference buffer; the two are compared byte-for-byte, and the anchors
additionally check the pinned SHA-256 prefix. The expected full-pass line is
`SWEEP: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6 bytes=53248
span=53248 blocks=104 subrequests=13 max=8 foreign=0 fails=0 cancel=0 budget=0
media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 limit=8 seg=1
cap=61071360`, and the first failing case otherwise supplies `SWEEPREAD`.

The report thread arms the active case under `reportLock` before each
`media->read()` and clears it after the call; it never holds the lock across
the media call. The adapter's `doAsyncReadWrite()` calls the mode-scoped
`sweepObserve()` after the null/zero checks and before taking the controller
`gate`. The observer takes `reportLock` briefly, classifies the request by
case tag (direct path) or tag-0 block range, records subrequest
count/block total/maximum/order, counts unattributable requests as `foreign`,
does not retain the buffer and performs no I/O. The first failure latches its
case index, reason, status, actual, observed decomposition, reference
status/bytes, mismatch offset and digest prefixes; later read/short/mismatch/
descriptor/split/order failures continue, while media, partition, open,
budget, cancellation, reference-transport and anchor-mismatch failures stop
the run. `complete=1` requires all six cases with `reason=0`, `cancel=0`,
`budget=0`, `media=0`, `part=0`, `foreign=0`, `anchor=1`, `mismatches=0`,
`limit=8` and `seg=1`; reason 14 is the reference/media mismatch and 15 a
reference failure. The worst case is 20,000 + 2,000 + 4,000 + 1,700 =
27,700 ms, below the 30,000 ms watchdog, and the formatter keeps `SWEEP` at or
below 384 bytes, `SWEEPREAD` at or below 352 bytes and the whole report below
2,048 bytes before NUL termination with fail-closed behavior unchanged.

Host results: the block suite passes 48 core scenarios/38 mutation controls
and 218 adapter scenarios with 71 adapter, three support and eight policy
mutation controls. The adapter coverage includes the six-case full pass, the
production 64-block control (the oversized requests fail with `ORDER`), the
one-segment advertisement control, short reference/media, hash/anchor skips,
budget, cancellation, complete-claim, start-hardcode, bounce, decomposition
and order skips, a shifted reference LBA, media/partition/open failures, the
watchdog race and the selector/property/report controls. Diagnostic payload
tests pass 40 checks and the packager self-test 15 checks, including both
`emmc-block-1-sweep` and `emmc-block-3-sweep` personalities, the
`fs-timer-hold` fixture requirement and the exclusivity refusals. The host
model reconstructs the media/breaker path; it does not execute the real
family code, so N10.13 candidate production and N10.14 hardware remain
separately gated, the sweep has no hardware observation, N10.10 remains the
only breakup-layer hardware result and initialization reliability remains
`[U]`. Findings:
`docs/emmc-n10.11-bounded-read-sweep-design-2026-11-09.md`.

### 5.3.86 N10.13 bounded read-sweep qualification candidate (host-only, 2026-11-09)

Follow-up to §5.3.85 and the N10.11 design. N10.13 builds and gates the N10.12
opt-in `gta4l-emmc-sweep=1` mode through the archive/prelink/audit/payload/
BOOT flow without changing the selector, six-case table, two page-aligned
16,384-byte buffers, direct CMD17 reference, pinned anchor digests,
`SWEEP`/`SWEEPREAD` report lines or `complete=1` guard. The candidate chain is
prelinked kernel `64bab406…` (51,363,840 bytes; plan `cd402f0e…`; original
`f18b4df8…`), kernel-build archive `14f40fa8…`, candidate storage-ABI audit
`out/gta4l/storage-abi-64bab406` (`gdb-stop.json` `227fec6d…`,
`interface-audit.json` `bbd49581…`, `provenance.json` `50d546bf…`,
`run.json` `e8f1b9d2…`; 190 method slots, 181 kernel bindings, 26 concrete
overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
metaclass `0xfffffff006add1d0`, service metaclass `0xfffffff007b0b5b8`,
`bsd_init` `0xfffffff0076eba3c`, `runtime_qualified=false`), requalified
archive `14f40fa8…` with byte-identical members, packaged kernel `f1ab2b1d…`,
payload `62371bfb…` (83,886,080 bytes; manifest `e39debae…`; gate
`20e23cee…`; 36/36 contract; `emmc-block-3-sweep` mapping to
`gta4l-emmc-block=3 gta4l-emmc-sweep=1`; hold root `327ac826…`, fixture
`00363681…`) and BOOT image `b626a1fa…` (85,688,320 bytes; manifest
`a43d16e3…`; gate `1b43740a…`; 18/18, only the ramdisk differs from the
`ce4d76e8…` v5 baseline; kernel section `a4b054e0…`, firmware `1e8a3132…`
and DTB `dc4c719d…` unchanged). Both anchor digests were re-verified against
the gate-pinned v5 image (`5878bcf8…` at 4,096/4,096 and `fa43aa5d…` at
4,096/16,384); the anchors are inside the 1,798,052-byte kernel section and
are invariant across candidates, while the four runtime windows are compared
in the same boot against their direct CMD17 references. No audited
method-target changed from the N10.9 audit; the bindings object stays
`3e53b66d…`. The packaged kernel carries the two `SWEEP`/`SWEEPREAD` lines,
nine `gta4l-emmc-sweep` strings and no literal `complete=`.

Host suites re-passed: block 48 core scenarios/38 mutation controls and 218
adapter scenarios with 71 adapter, three support and eight policy mutation
controls (including the sweep mutations), core 187/115, probe 196/100,
payload 40, packager 15, boot-image self-test, storage-ABI positive/negative,
evidence counts, fixture recipe, board and TOML checks. No flash, boot, device
access, commit or push was performed while producing the candidate; N10.12's
host implementation is unchanged, N10.14 remains separately gated, the sweep
has no hardware observation, N10.10 remains the only breakup-layer hardware
result and initialization reliability remains `[U]`. Findings:
`docs/emmc-n10.13-sweep-candidate-2026-11-09.md`.

### 5.3.87 N10.14 bounded read-sweep hardware run (2026-11-10)

Follow-up to §5.3.86. The owner directed a BOOT-only write of the N10.13 BOOT
image `b626a1fa…` (85,688,320 bytes; payload `62371bfb…`, prelinked kernel
`64bab406…`, packaged kernel `f1ab2b1d…`, requalified archive `14f40fa8…`) to
p69 and controlled one cold boot. Only `/dev/block/mmcblk0p69` was written
(20,920 x 4096 records, synced). The pre-flash p69 was the N10.10 post-flash
image `4b51ff21…` with prefix `a8f4a54b…` and tail `a28b4ea2…`; the post-flash
full readback `a2b5a62d…` matched the candidate prefix `b626a1fa…` plus the
unchanged tail, and RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs
`3a265d58…` were unchanged before and after the write. The rollback image
`boot-before-n10.14.img.xz` (`d4fce9c9…` -> raw `4b51ff21…`) was not restored.

The owner report returned `rc=0 stop=0 mode=3` with the read-only whole media
and `SWEEP: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6 bytes=53248
span=53248 blocks=104 subrequests=13 max=8 foreign=0 fails=0 cancel=0
budget=0 media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 limit=8
seg=1 cap=61071360`; the last case emitted `SWEEPREAD: index=5 target=1
start=4673536 length=4096 span=4096 actual=4096 status=0 subrequests=1
blocks=8 max=8 ref=1 refbytes=4096 refstatus=0 mvalid=0 mismatch=0
digest=ccaa1c08dd34df3ca8d4eb3a2b530fdb
refdigest=ccaa1c08dd34df3ca8d4eb3a2b530fdb`, a same-boot media-path digest
prefix equal to its direct reference. The six-case aggregate is exactly the
`package/Gta4lEmmcBlock.c` table: 4,096 + 16,384 + 16,384 + 8,192 + 4,096 +
4,096 = 53,248 bytes; 8 + 32 + 32 + 16 + 8 + 8 = 104 blocks; and 1 + 4 + 4 +
2 + 1 + 1 = 13 driver-level subrequests, all aligned so `span=bytes`. Both
pinned anchors re-hashed to their invariant gate-pinned values `5878bcf8…
f3b1b2` and `fa43aa5d…0782da` (`anchor=1`), every case's direct
controller-gate CMD17 reference agreed byte-for-byte with the media-path
read (`refs=6 refbytes=53248 mismatches=0`, `mvalid=0` is the equal-content
form), and `complete=1` is the guard at `package/Gta4lEmmcBlock.cpp:4529-4554`.
The whole-boot counters are `DRV: online=1 wp=1 reads=22 blocks=133 fails=0
max=8` and `XFER: req=133 c17=7 c18=63 c18b=126 cfail=0 stops=63 sfail=0
last_stop=0`: `XFER req` equals `DRV blocks`, the transport issued 7 CMD17
reads plus 63 two-block CMD18 chunks with one successful CMD12 stop per
chunk, the case column accounts for 52 of the chunks (13 x 4), and the
remaining 11 chunks and 7 CMD17 reads form the same 29-block incidental
overlay observed by the N9.6/N9.7 partcmp boots and the N10.10 breakup boot.
The direct CMD17 reference reads bypass the driver path and are not visible
in the `DRV`/`XFER` aggregates. The `@Gta41EmmcBlock.cpp:4882` suffix
(spelling preserved) is the intentional `panic("%s", reportBuffer)` terminal
report stop in the N10.13 candidate source `80c817a0…`, the same class as
N10.10's `:3899`, and no watchdog line appeared.

No post-boot readback is recorded: the owner returned the tablet to TWRP
after the one cold boot and no further device access is authorized, so the
stored image after the boot is not independently re-verified in this record.
The report is bound to the candidate by the pre-boot post-flash readback,
the read-only target-binding re-verification, the two pinned anchor digests
and the same-boot media/reference digest equality. This is a bounded
same-boot qualification of the two invariant anchors and the four
P-derived runtime windows and the real driver/reference agreement on one
boot, not an initialization-reliability or general I/O result; N8.5/N9.6/
N9.7/N9.8 `complete=0`, the N9.10 comparator result, the N9.11 owner-reported
series, the N10.1/N10.2 partread results, the N10.6 deblock result and the
N10.10 breakup result are preserved, and initialization reliability remains
`[U]`. N10.14 is the first sweep-mode hardware observation. The one-boot
budget is consumed; no retry, reflash, rollback, reset/repair, mount, commit
or push was performed. Findings:
`docs/emmc-n10.14-sweep-hardware-run-2026-11-10.md`.

### 5.3.88 N10.15 read-only consumer implementation (host-only, 2026-11-10)

Follow-up to §5.3.87 and the owner-approved
`docs/emmc-n10.15-read-only-consumer-design-2026-11-10.md`. N10.15 implements
the opt-in `gta4l-emmc-consume=1` mode in `package/Gta4lEmmcBlock.h`/`.c`/
`.cpp` without a candidate build, ABI/QEMU audit, device access, flash, boot
or payload rebuild. The token requires exactly one `gta4l-emmc-block=3` token
and refuses block 1, a duplicate, an orphan, a non-1 or malformed value and
every partobs/partcmp/partread/deblock/breakup/sweep combination; the
absent-token legacy, partobs, partcmp, partread, deblock, breakup and sweep
reports stay byte-for-byte unchanged.

The mode uses the production advertisement (`kIOMaximumBlockCountReadKey=64`,
`kIOMaximumByteCountReadKey=32,768`, `kIOMaximumSegmentCountReadKey=1`),
pinned by a host scenario and reported as `limit=64 seg=1`. A distinct
minimal `Gta4lEmmcConsumer : public IOService` client is created with `new`
and `init()`, opens the pinned BOOT partition read-only with its own client
identity, issues one `partition->read(consumer, 0, descriptor, nullptr,
&actual)` and is released; the whole media is opened and closed with the
provider `this` and balanced separately. The single window is BOOT offset 0,
1,024 bytes (two blocks). The consumer allocates one
`IOMallocContiguous(32768, PAGE_SIZE, nullptr)` buffer, builds the descriptor
with `withAddress(buffer, 1024, kIODirectionIn)` and `kIOMemoryAutoPrepare`,
requires `getPhysicalSegment(0, &segmentLength, kIOMemoryMapperNone)` to
return a nonzero address and a segment of at least 1,024 bytes (`DESC`
otherwise), sets the distinct nonzero case tag and frees the buffer with
`IOFreeContiguous(buffer, 32768)` on every path. Allocation, descriptor,
open, read, short-read, cancellation and budget failures latch their first
reason and do not retry or substitute a fragmented buffer.

The entry-side observer at the top of `doAsyncReadWrite()` runs before the
controller gate only while the single case is in flight and requires the
descriptor tag, exact whole-media first block, block count and range to match;
a tag-0 or otherwise different request is `foreign` even when it overlaps the
window, a second matching request while in flight or a missing/extra case is
`ORDER`, and the completion side credits the private `block.counters` deltas
under the gate only for the exact tagged request (one two-block CMD18 chunk,
two blocks and one stop). The mode adds no write, mount, wider/faster mode,
other partition read or fallback. Under option A no expected digest is
compiled in: the report prints the observed first-16-byte SHA-256 prefix with
`expected` all zeros and `mismatch=0`, and the host compares it with the
external reference; the host `consume_expected_digest()` recomputes the
two-block fixture through `gta4l_test_fill_sector()`, so a mismatch fails
acceptance host-side without changing the on-device `complete` field.

The expected success lines are
`CONSUME: cases=1 passed=1 failed=0 reason=0 complete=1 reads=1 bytes=1024
span=1024 blocks=2 subrequests=1 max=2 foreign=0 fails=0 cancel=0 budget=0
media=0 part=0 open=1 close=1 alloc=0 client=1 mismatches=0 limit=64 seg=1
cap=61071360` and `CONSUMEREAD: index=0 target=1 start=0 length=1024
span=1024 actual=1024 status=0 subrequests=1 blocks=2 max=2
digest=<observed> expected=00000000000000000000000000000000 mismatch=0`;
the failure form is `CONSUMEFAIL` with reason, case, target, start, length,
span, requested, completed, actual, status, digest, expected and mismatch.
Consume mode suppresses `READ`/`QUAL`/`QUAL3` and every other line is
unchanged. The formatter caps are 320/352/384 bytes for the three lines and
the pinned worst report is 1,959 content bytes inside the 2,048-byte report;
the static asserts at `package/Gta4lEmmcBlock.cpp:129-162` pin the timing,
window, advertisement, tag and width bounds. The worst case is the conditional
20,000 + 2,000 + 3,000 = 25,000 ms against the 30,000 ms watchdog; the 5,000 ms
margin must also cover allocation, gate contention and scheduling, so this is
not a complete path-condition proof and remains a candidate-production gate.

Host results: the block suite passes 48 core scenarios/38 mutation controls
and 248 adapter scenarios with 95 adapter/support and 10 policy mutation
controls, including the full pass, the production 64/32,768/1 advertisement,
the one-segment descriptor, the digest host comparison, media/partition/open/
allocation/descriptor failures, short/read/cancel/budget/foreign/order
controls, the suppression and width controls, and the legacy-mode equality
checks. Diagnostic payload tests pass 42 checks and the packager 16 checks,
including the `emmc-block-3-consume` personality, the `fs-timer-hold` fixture
requirement, the block-1 refusal and the six-mode exclusivity refusals. The
ABI gate compiles the new compile-only
`scripts/sim-gta4l-emmc-block/consumer_lifecycle_probe.cpp` with the pinned
adapter record and both include paths: it has 187 undefined symbols, all
defined in the unchanged N10.13 candidate kernel `64bab406…` (185
`__TEXT_EXEC,__text`, one `__DATA_CONST,__const` for `PAGE_SHIFT_CONST` and one
`__DATA,__common` for `IOService::gMetaClass`), and the extended build gate
checks 238 non-bound imports against that kernel and reports `qualified=True`
(probe source `984ab609…`, probe object `629735dc…`, archive `10228710…`).
The host model reconstructs the IOKit client/provider path; it does not
execute the real family code, so N10.15 has no hardware observation, N10.14
remains the only sweep-mode hardware result and N10.10 the only breakup-layer
result, and initialization reliability remains `[U]`. Candidate production
and any hardware run remain separately gated: the conditional timing margin
must be proved or explicitly accepted, and the external window digest must be
rebound to the final packaged image. No payload was rebuilt. Findings:
`docs/emmc-n10.15-read-only-consumer-design-2026-11-10.md`.

### 5.3.89 N10.15 read-only consumer qualification candidate (host-only, 2026-11-10)

Follow-up to §5.3.88 and the N10.15 design. N10.15 candidate production
builds and gates the opt-in `gta4l-emmc-consume=1` mode through the
archive/prelink/audit/payload/BOOT flow without changing the selector,
production 64-block/32,768-byte/one-segment advertisement, distinct
`Gta4lEmmcConsumer` client, one 1,024-byte BOOT window at offset 0, 32 KiB
`IOMallocContiguous` buffer, one-segment descriptor, tag/range observer,
option A digest reporting or `complete=1` guard. The candidate chain is
prelinked kernel `d943c228…` (51,437,568 bytes; plan `29b7a386…`; original
`f18b4df8…`), kernel-build archive `12d851a8…`, candidate storage-ABI audit
`out/gta4l/storage-abi-d943c228` (`gdb-stop.json` `92ccffbf…`,
`interface-audit.json` `fdfae103…`, `provenance.json` `a9088e50…`,
`run.json` `615b27d2…`; 190 method slots, 181 kernel bindings, 26 concrete
overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
metaclass `0xfffffff006add1d0`, service metaclass `0xfffffff007b175b8`,
`bsd_init` `0xfffffff0076efa3c`, `runtime_qualified=false`), requalified
archive `12d851a8…` with byte-identical members, packaged kernel
`e5ba92e3…`, payload `e05eb1cb…` (83,886,080 bytes; manifest `c23aaa9b…`;
45/45 contract; `emmc-block-3-consume` mapping to `gta4l-emmc-block=3
gta4l-emmc-consume=1`; hold root `327ac826…`, fixture `00363681…`) and BOOT
image `777b446c…` (85,688,320 bytes; manifest `09dfe4ea…`; gate
`22587d59…`; 18/18, only the ramdisk differs from the `ce4d76e8…` v5
baseline; kernel section `a4b054e0…`, firmware `1e8a3132…` and DTB
`dc4c719d…` unchanged; header ID `4b15eb2a…`). The external window reference
was rebound from the N10.13 image's `5a25ed5e…` to the packaged image's
first 1,024 bytes
`97479ae876ec6fe6f45b38492b9eb9ac934b092ee8bcdccd3167cdc4eb502c7a` (prefix
`97479ae8…`); the on-device `CONSUMEREAD` digest must equal that prefix, and
a mismatch fails acceptance host-side only. The packaged kernel carries one
`CONSUME`, one `CONSUMEREAD` and one `CONSUMEFAIL` string, ten
`gta4l-emmc-consume` strings and no literal `complete=`.

Host suites re-passed: block 48 core scenarios/38 mutation controls and 248
adapter scenarios with 95 adapter/support and 10 policy mutation controls
(including the consume mutations), core 187/115, probe 196/100, payload 42,
packager 16, boot-image self-test, storage-ABI positive/negative, evidence
counts, fixture recipe, board and TOML checks. The owner accepted the
conditional 25,000 ms timing margin for the separately approved BOOT-only
flash and one cold boot; N10.15 has no hardware observation yet, the one-boot
budget is not consumed, N10.14 remains the only sweep-mode hardware result,
N10.10 the only breakup-layer result and initialization reliability remains
`[U]`. Findings: `docs/emmc-n10.15-consume-candidate-2026-11-10.md`.

### 5.3.90 N10.15 read-only consumer hardware run (2026-11-10)

Follow-up to §5.3.89. The owner directed a BOOT-only write of the N10.15 BOOT
image `777b446c…` (85,688,320 bytes; payload `e05eb1cb…`, prelinked kernel
`d943c228…`, packaged kernel `e5ba92e3…`, requalified archive `12d851a8…`) to
p69 and controlled one cold boot. Only `/dev/block/mmcblk0p69` was written
(20,920 x 4096 records, synced). The pre-flash p69 was the N10.14 post-flash
image `a2b5a62d…` with prefix `b626a1fa…` and tail `a28b4ea2…`; the post-flash
full readback `6fa2b7e9…` matched the candidate prefix `777b446c…` plus the
unchanged tail, and RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs
`3a265d58…` were unchanged before and after the write. The rollback image
`boot-before-n10.15.img.xz` (`4a90e599…` -> raw `a2b5a62d…`) was not restored.

The owner report returned `rc=0 stop=0 mode=3` with the read-only whole media
and `CONSUME: cases=1 passed=1 failed=0 reason=0 complete=1 reads=1 bytes=1024
span=1024 blocks=2 subrequests=1 max=2 foreign=0 fails=0 cancel=0 budget=0
media=0 part=0 open=1 close=1 alloc=0 client=1 mismatches=0 limit=64 seg=1
cap=61071360`; the single case emitted `CONSUMEREAD: index=0 target=1 start=0
length=1024 span=1024 actual=1024 status=0 subrequests=1 blocks=2 max=2
digest=97479ae876ec6fe6f45b38492b9eb9ac
expected=00000000000000000000000000000000 mismatch=0`. The digest equals the
option A external reference
`97479ae876ec6fe6f45b38492b9eb9ac934b092ee8bcdccd3167cdc4eb502c7a` (the first
1,024 bytes of the packaged image); `expected` is all zeros by design and the
comparison is host-side only. `complete=1` is the guard at
`package/Gta4lEmmcBlock.cpp:5209-5243`, which additionally requires the
completion-side private deltas `creditedC18 == 1`, `creditedC18b == 2` and
`creditedStops == 1`. The single case is BOOT target 1, offset 0, length
1,024 bytes, two blocks, one driver-level subrequest with a largest
subrequest of two blocks, under the production 64-block/one-segment
advertisement. `media=0 part=0 foreign=0` are clear failure flags,
`alloc=0` records the accepted 32 KiB `IOMallocContiguous` buffer and
one-segment descriptor, and `open=1 close=1 client=1` record the consumer
lifecycle.

The whole-boot counters are `DRV: online=1 wp=1 reads=10 blocks=31 fails=0
max=8` and `XFER: req=31 c17=7 c18=12 c18b=24 cfail=0 stops=12 sfail=0
last_stop=0`: `XFER req` equals `DRV blocks`, the transport issued 7 CMD17
reads plus 12 two-block CMD18 chunks with one successful CMD12 stop per
chunk, the case column accounts for one of the 12 chunks (two blocks, one
stop), and the remaining 11 chunks and 7 CMD17 reads form the same 29-block
incidental overlay observed by the N9.6/N9.7 partcmp, N10.10 breakup and
N10.14 sweep boots. `DRV max=8` therefore comes from that overlay, not from
the case, whose largest subrequest is the reported `max=2`. Consume mode
suppresses `READ`/`QUAL`/`QUAL3` through the `report->consume == 0` guard at
`package/Gta4lEmmcBlock.c:2648`; the zero `PART` line is the unchanged legacy
line. The `@Gta41EmmcBlock.cpp:5719` suffix (spelling preserved) is the
intentional `panic("%s", reportBuffer)` terminal report stop in the N10.15
candidate source `86dab8b1…`, and no watchdog line appeared. The owner's
wrapped `expected=` field carries 35 zeros instead of the designed 32; that
is a transcription artifact and does not change the field semantics.

No post-boot readback is recorded: the owner returned the tablet to TWRP
after the one cold boot and no further device access is authorized, so the
stored image after the boot is not independently re-verified in this record.
The report is bound to the candidate by the pre-boot post-flash readback, the
read-only target-binding re-verification and the option A digest comparison
against the packaged image. This is a bounded same-boot qualification of one
1,024-byte BOOT window, the consumer lifecycle, the production advertisement
and the driver/attribution path on one boot, not an initialization-reliability
or general I/O result. The 25,000 ms worst case remains conditional and
accepted, not a timing measurement; the absence of a watchdog line does not
prove the margin. N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10 comparator
result, the N9.11 owner-reported series, the N10.1/N10.2 partread results,
the N10.6 deblock result, the N10.10 breakup result and the N10.14 sweep
result are preserved, and initialization reliability remains `[U]`. N10.14
remains the only sweep-mode hardware observation, N10.10 the only
breakup-layer hardware observation, and N10.15 is its own first hardware
observation.
The one-boot budget is consumed; no retry, reflash, rollback, reset/repair,
mount, commit or push was performed. Findings:
`docs/emmc-n10.15-consume-hardware-run-2026-11-10.md`.

### 5.3.91 N10.16 4-bit SDR width-mode host batch (2026-11-10)

Follow-up to the N10.16 readiness audit. This section records the host-only
implementation of the opt-in `gta4l-emmc-width=4` mode and its test batch; no
candidate build, device access, flash or boot was performed.

Selector and scope. The mode requires exactly one `gta4l-emmc-block=3` token
and refuses an orphan, duplicate, malformed or non-4 width token, block
tokens other than 3, and every combination with `partobs`, `partcmp`,
`partread`, `deblock`, `breakup`, `sweep` or `consume`. With the token absent,
the legacy 1-bit path and the production 64-block/32,768-byte/one-segment
advertisement are unchanged.

Sequence. After `initializeHardware()` returns and before the baseline read,
the mode reads the 1-bit EXT_CSD baseline into a 512-byte buffer, reads the
pinned BOOT window (LBA
`GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES / 512` = 2,255,360, two blocks,
1,024 bytes) at 1-bit through the block core and computes its SHA-256, then
performs the verified 4-bit switch: CMD6 `BUS_WIDTH`, CMD13
TRAN/READY_FOR_DATA wait, `HOST_CONTROL` apply/readback and the read-only
EXT_CSD properties comparison against the baseline. The switch proof never
compares byte 183 and issues no CMD19/CMD14. The baseline LBA 1 read and
publication then run at 4-bit, and the report thread re-reads the same window
through the block core, requires the same exact count and clean CMD12,
compares all 1,024 bytes with the reference and computes the observed digest
before reverting to 1-bit with `gta4l_emmc_set_bus_width_recover()` and the
captured baseline.

Failure semantics. A baseline, pre-read or switch failure stops before
publication. A 4-bit read failure clears `online`, preserves the first
failure through the core's `first_fail_valid`/`first_fail_error` record and
fails closed without a retry or a smaller window. A comparison mismatch is a
terminal `reason=5`; a revert failure after a passing 4-bit read is a
terminal `reason=6` that retains both facts. The report uses `WIDTH` for the
switch and revert fields, `WIDTHREAD` for the passing window with both digest
prefixes, and `WIDTHFAIL` for the first terminal failure. Width mode
suppresses the legacy `READ`/`QUAL`/`QUAL3` lines, and every non-width mode
keeps its previous report form byte-for-byte. The three width lines are
bounded at 513, 329 and 127 bytes and the whole width-mode report at 1,772
bytes inside the 2,048-byte buffer. The success bound is 26,230 ms and the
failed-switch bound 21,030 ms against the 30,000 ms watchdog; the report
gate wait is explicitly bounded at 2,000 ms and a timeout is a terminal
gate failure. The margin remains conditional and unmeasured.

Host results. The block suite passes 48 core scenarios/38 mutation controls
and 285 adapter scenarios with 104 adapter/support and 10 policy mutation
controls, covering the absent token, all refusals, the full match,
baseline/pre-read/switch/read/short/stop/mismatch/revert failures, the
`ERR_WIDTH_RESTORE` terminal framing, budget, cancellation during the
bounded gate wait and after the 4-bit read, the terminal watchdog
interleaving, and the host-side packaged-digest rejection. The adapter mutations remove the
switch guard, claim completion, skip the byte compare, skip the revert,
credit a failed read, advertise the wrong limit/segments, truncate the
reference digest and shift the pinned LBA. The core suite passes 189
scenarios/117 mutation controls, the probe suite 196/100, and the
evidence-count test re-derives and checks the documented counts. The
diagnostic payload builder and packager suites also pass with the new
`emmc-block-3-width4` personality, which requires the `fs-timer-hold`
fixture and maps to exactly `gta4l-emmc-block=3 gta4l-emmc-width=4`; the
checker refuses orphan, duplicate, malformed, non-4, block-1, duplicate
block, probe and modifier-conflict tokens, and no other personality can
smuggle the width token into an accepted mapping. That host batch built no
payload or candidate image. Candidate, owner and hardware gates are tracked in
`docs/emmc-n10.16-4bit-sdr-readiness-2026-11-10.md` §10.

Corrective candidate. The 2026-11-11 pre-hardware review found that a slow
64-block in-flight request could hold the controller gate past the
30,000 ms watchdog before the width case started, and corrected the digest
field to 16 bytes/32 hex characters. The host-only gate-fix batch then
produced prelinked kernel `2be2eb06…` (51,470,336 bytes), a
candidate-matched 190-slot/181-binding storage-ABI audit with 26 overrides
and 0 unresolved original imports, a requalified block archive whose members
match the archive linked into the kernel, payload `10ff7ee8…` (55/55
contract, `emmc-block-3-width4`) and BOOT image `58352cd3…` (18/18 gate, only
the ramdisk differs). The report gate is bounded at 2,000 ms; a timeout is
terminal reason 8 with media offline and no revert claim. The width report is
bound to the final image's first 1,024 bytes
(`a6da82e356863d1ab7e31c21a8fd85fc…`), and
`scripts/check-gta4l-width4-evidence.py` covers matching, changed, missing,
malformed, truncated and wrong-shape host cases. This closes the gate finding
at source and host-test level; the 13,010 ms pre-watchdog path remains a
conditional design budget. Hardware result: the owner-approved one-boot run
executed on 2026-11-11. The terminal report returned `rc=0 stop=0` with
`WIDTH cases=1 passed=1 failed=0 reason=0 complete=1`,
`switch=4 verified=1 hb=0 ha=2 rb=0 cmd6=1`,
`reads=2 bytes=2048 blocks=4 subrequests=2 max=2 match=1`,
`revert=1 rverified=1 rhb=2 rha=0 rcmd6=1` and
`limit=64 seg=1 cap=61071360`; the WIDTHREAD digest and predigest both
matched the image prefix `a6da82e3…`, and no `WIDTHFAIL` or watchdog report
appeared. `rb=0` is the diagnostic byte-183 readback (W/E_P, not a completion
condition) and `rcmd6=1` is first-attempt revert success; the terminal guard
attests both at `package/Gta4lEmmcBlock.cpp:6052-6091`. The pre-flash BOOT
image was restored and verified, with RECOVERY/EFS/sec_efs unchanged. Result:
`docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md`. Findings:
`docs/emmc-n10.16-prehardware-review-2026-11-11.md`,
`docs/emmc-n10.16-4bit-sdr-readiness-2026-11-10.md` §10 and
`docs/emmc-n10.16-width4-gatefix-candidate-2026-11-11.md`.

Status. F5 remains measured PASS at 1-bit SDR/400 kHz for one reported boot
and F3 at 4-bit SDR/400 kHz for one reported boot; the width4 mode now has one
measured cold-boot PASS of its own switch/read/compare/revert path. This is
not a publication, repeatability, failure-rate or initialization-reliability
result: no 4-bit publication exists and initialization reliability remains
`[U]`. The one-boot budget is consumed and the result authorizes no retry,
reflash, mount, write or extra boot. Deferred: CMD19/CMD14, 8-bit, HS200/HS400,
DDR, higher clocks, DMA/CQE/ICE, mount/root, storage writes and any user-data
path.

### 5.3.92 N10.18 sustained-width widthhold batch and N10.19 candidate (2026-11-11)

Follow-up to the reconciled N10.17 design. The N10.18 host batch implemented
the opt-in sustained-width mode; the N10.19 host-only batch produced and
qualified the prelinked kernel, payload and BOOT candidate. No device access,
flash, boot, mount or write was performed.

Selector and scope. The mode requires exactly one `gta4l-emmc-block=3` token
and refuses a duplicate, an orphan, a malformed or non-1 widthhold token,
block tokens other than 3, and every combination with `partobs`, `partcmp`,
`partread`, `deblock`, `breakup`, `sweep`, `consume` or `width`. With the
token absent, the legacy path and the N10.16 width4 behavior are unchanged.

Sequence. After the baseline EXT_CSD capture, the mode reads an 8-block/
4,096-byte BOOT reference at 1-bit, performs the same verified 4-bit switch
(CMD6 `BUS_WIDTH`, CMD13 TRAN wait, `HOST_CONTROL` apply/readback, read-only
EXT_CSD properties comparison; byte 183 is never a switch condition and
CMD19/CMD14 is excluded), issues four tagged two-block reads at BOOT offsets
0, 1,024, 2,048 and 3,072 bytes (LBAs 2,255,360, 2,255,362, 2,255,364 and
2,255,366), compares every byte against the reference slice and reverts to
1-bit with the captured baseline on every post-switch exit. Each case is
attributed through the tagged subrequest observer, so foreign traffic is
counted rather than silently folded into a case.

Report and bounds. The report family is one terminal `WIDTHHOLD` summary
plus one `WIDTHHOLDREAD` last-case line on success, or one `WIDTHHOLDFAIL`
line on failure; success and failure lines are mutually exclusive. Widthhold
mode suppresses the legacy `READ`/`QUAL`/`QUAL3` and `PART` lines. The three
line bounds are 905, 335 and 227 bytes and the worst report is 2,078 bytes
inside the 2,560-byte buffer. The report-thread bound is 27,720 ms (8,000
media wait + 2,000 BOOT deadline + 3,000 case budget + 4,500 two-block case
+ 2,000 revert gate + 8,220 verified revert), leaving 2,280 ms under the
30,000 ms watchdog; the pre-watchdog path remains conditional.

Candidate. The N10.19 batch produced prelinked kernel `23e3b6d90f4a…`
(51,527,680 bytes), a candidate-matched 190-slot storage-ABI audit with 0
unresolved methods and native 4K, a kernel-build archive byte-identical to
the requalified-tree archive (`6dcdfe9a…`) with matching members, payload
`bd112387…` (83,886,080 bytes; 66/66 contract; `emmc-block-3-widthhold`
mapping to `gta4l-emmc-block=3 gta4l-emmc-widthhold=1`) and BOOT image
`7f5455d6…` (85,688,320 bytes; 18/18 gate; only the ramdisk differs from the
v5 baseline). `scripts/check-gta4l-widthhold-evidence.py` binds
`refdigest`/`obsdigest` to the final image's first 4,096 bytes and
`digest`/`predigest` to the last-case 1,024-byte slice, checks the complete
success shape and rejects both a missing/duplicated line and a
`WIDTHHOLDFAIL` line. The host block suite passes 48 core scenarios/38
mutation controls and 344 adapter scenarios with 120 adapter/support and 14
policy mutation controls, including the widthhold cancel-quiet, budget
success, case-skip, revert-skip, advertise-mismatch and terminal-claim
mutations; core 189/117, probe 196/100, payload and packager suites,
storage-ABI, evidence-count, fixture, inventory, packaging and boot-image
checks pass.

Status. The sustained-width mode has no hardware observation: F5 remains
measured PASS at 1-bit and F3 at 4-bit for one reported boot each,
repeatability and failure rates remain `[U]`, and the N10.16 one-boot budget
stays consumed. A hardware protocol and run (N10.20) are separately gated and
not authorized by this batch. Findings:
`docs/emmc-n10.17-sustained-width-read-design-2026-11-11.md` and
`docs/emmc-n10.19-widthhold-candidate-2026-11-11.md`.

### 5.3.93 N10.20 widthhold failure and N10.21 PART diagnostics (2026-11-11)

Follow-up to N10.19. The N10.20 BOOT-only flash and one approved boot were
executed; the run failed at BOOT-partition discovery with `reason=5` (`PART`)
before any case read, after a verified 4-bit switch, a matching 1-bit
reference and a verified revert. All partition fields were zero in the old
schema, so the failing category could not be identified. No new device
action, flash, boot, mount or write occurred in this host-only batch.

Correction. The BOOT Content check is now structural: the property must be
present as an `OSString`, and its value is deliberately not compared. The
stock partition scheme publishes a type-GUID string and an attached storage
client may install its Content Mask, so the previous exact `Apple_Boot`
equality was not a valid acceptance rule. Actual bytes remain protected by
the same-boot reference comparison and the final-image host digest binding.

Diagnostic. A widthhold-only u32 `partdiag` captures the first failing PART
category as fixed bits: not-found, duplicate, size, base, Writable, Content.
It is reset per run, latched once (never accumulated), copied through report
copy/snapshot and printed after `partro`; zero means no PART failure. It is
not a reason code and does not claim every check ran. `partbase`, `partsize`
and `partro` stay zero unless all checks pass. The formatter-exact bounds are
922/913 bytes for the summary failure/success shape, 303 for `WIDTHHOLDREAD`
and 133 for `WIDTHHOLDFAIL`, with a 2,066-byte read-path and 1,896-byte
fail-path worst report inside 2,560 bytes.

Host results. The block suite passes 48 core scenarios/38 mutation controls
and 351 adapter scenarios with 122 adapter, 3 support and 15 policy mutation
controls, including the new PART category, late base/size, structural Content
acceptance, lost-latch, lost-reset, reintroduced-name and omitted-field
controls. `scripts/check-gta4l-widthhold-evidence.py` requires `partdiag=0`
on a success line and rejects nonzero, missing, duplicated, malformed or
unknown fields. Core 189/117, probe 196/100 and the evidence-count check pass.

Status. The N10.20 run stays FAIL and is not reinterpreted. The N10.19 image
remains as last written and must not be booted again without a new finite
budget. A new source-bound candidate build, storage-ABI audit, rebound image,
hardware protocol and owner approval are separate gates; no hardware
continuation is authorized. Findings:
`docs/emmc-n10.20-widthhold-observation-2026-11-11.md` and
`docs/emmc-n10.21-widthhold-partdiag-candidate-2026-11-11.md`.

### 5.3.94 N10.22 widthhold candidate production (host-only, 2026-11-12)

Follow-up to N10.21. A fresh host-only candidate was built from the corrected
source at `6b7dd95`; this record does not qualify the N10.19 image and does
not reinterpret N10.20, which stays **FAIL**.

Source and build. The linked kernel `04ae57f6f565` (51,527,680 bytes) was
built from `Gta4lEmmcBlock.c` `c4feb2d4...`, `Gta4lEmmcBlock.cpp` `6cc17c2a...`
and `Gta4lEmmcBlock.h` `16edd86e...`; the build tree's `kits/gta4l` link
resolved to the corrected device package, and the linked kernel contains the
`partdiag` key and `widthholdLatchPartDiag` symbol. The candidate storage-ABI
audit is `out/gta4l/storage-abi-04ae57f6f565/` (gdb-stop `d30ad460`,
interface-audit `f56e3f9e`, provenance `50f697f7`, run `91c9ac96`; 190 slots,
181 bindings, 26 overrides, 0 unresolved original imports, vtable
`fffffff005cb44c0`, service metaclass `fffffff007b2b5b8`). The block archive
`0c7e2df2...` is byte-identical between the linked and requalified runs;
members are `Gta4lEmmcBlock.o` `7662aa44...`, `Gta4lEmmcBlockCpp.o`
`3f40c53d...` and `original-bindings.o` `3e53b66d...`. The at-link report
retains the bootstrap audit binding; the requalified report binds
`candidate_sha256` `04ae57f6...` and the candidate interface audit, with no
linked-byte change.

Image. Payload `64914089...` (83,886,080 bytes; 66/66 contract,
`emmc-block-3-widthhold`, fs-timer-hold fixture, packaged kernel
`50168139...`) and BOOT image `b4d63be7...` (85,688,320 bytes; 18/18 gate,
only the ramdisk differs from the `ce4d76e8...` v5 baseline; header ID
`00eaac92...`). Final-image bindings: first-4,096-byte SHA-256
`417cdef0f05bceafea13119a3b9f1ff5dab52be86ab0173dfe6d563c57368400`
(prefix `417cdef0f05bceafea13119a3b9f1ff5`), first-1,024-byte
`338ebbe6ab3da764f0ad87e30c2785742ea250e321b1a78bb63f914490b506f1`, and
last-case slice at offset 3,072
`5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef`.

Host results. Block 48 core scenarios/38 mutation controls and 351 adapter
scenarios with 125 adapter/support and 15 policy mutation controls; core
189/117; probe 196/100; payload and package regressions, builder/checker
self-tests, storage ABI, evidence counts, fixture recipe, inventory and
packaging checks pass; the widthhold checker passes on the final image and a
labeled synthetic `partdiag=0` fixture and rejects changed content, nonzero,
missing, duplicated and malformed `partdiag`, and `WIDTHHOLDFAIL`.

Status. Host-qualified candidate only; no N10.22 hardware result exists,
initialization reliability remains `[U]`, and the N10.19 image must not be
booted again without a new finite budget. A hardware protocol, current
p69/protected-partition checks and owner approval remain separate gates.
Findings:
`docs/emmc-n10.22-widthhold-candidate-production-2026-11-12.md`.

### 5.3.95 N10.23 widthhold hardware run (2026-11-12)

First hardware run of the corrected candidate. One owner-approved BOOT-only
write and one owner-triggered diagnostic boot; the one-boot budget is consumed
(1/1). **PASS** on the bounded width-hold read qualification.

Preflight and write. TWRP 3.7.1 (`gta4l`), battery Full, verified boot state
`orange`, no slot suffix; `boot`=`p69` (100,663,296 bytes), `recovery`=`p70`,
`efs`=`p64`, `sec_efs`=`p63`. Pre-flash p69
`f0940421acedba4498cf69631fdac45ffab83abf65e47b56559926e87a0a1e7d` with
prefix `7f5455d6…` (N10.19 image still installed) matched the N10.20
post-flash records; RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs
`3a265d58…` were unchanged. Verified full backups of BOOT/RECOVERY/EFS/sec_efs
were taken to the owner's private store and matched the device reads. The
N10.22 image `b4d63be7…` (85,688,320 bytes) was hash-verified before an exact
20,920 × 4,096-byte write to p69; no wipe, repartition, tail zeroing or other
partition write. Post-write prefix `b4d63be7…`, full p69
`7260ad75caf7522b1e6ff7ccb63979b2096dd7d68fd580b1844574e3419a34cb`
(new, expected), tail `a28b4ea2…`, last 1 KiB `a4f065b4…` and the three
protected partitions unchanged.

Report. `REPORT: rc=0 stop=0 mode=3 … clk=400000`; `DRV: online=1 wp=1
reads=14 blocks=45 fails=0 max=8`; `XFER: req=45 c17=7 c18=19 c18b=38
cfail=0 stops=19 sfail=0 last_stop=0`; `MEDIA: whole=1 base=0
size=31268536320 content=GUID_partition_scheme writable=0`; `WIDTHHOLD:
cases=4 passed=4 failed=0 reason=0 complete=1 switch=4 verified=1 hb=0 ha=2
rb=0 cmd6=1 refs=1 refbytes=4096 refblocks=8 refstatus=0 refchunks=4
refstops=4 refstopfail=0 refdigest=417cdef0f05bceafea13119a3b9f1ff5 reads=4
bytes=4096 blocks=8 subrequests=4 max=2 c18=4 stops=4 stopfail=0 match=1
mismatches=0 fails=0 foreign=0 cancel=0 budget=0
obsdigest=417cdef0f05bceafea13119a3b9f1ff5 partbase=1154744320
partsize=100663296 partro=1 partdiag=0 revert=1 rverified=1 rhb=2 rha=0
rcmd6=1 rstatus=0 limit=64 seg=1 cap=61071360`; `WIDTHHOLDREAD: index=3
lba=2255366 start=3072 length=1024 span=1024 actual=1024 status=0
subrequests=1 blocks=2 max=2 digest=5f70bf18a086007016e948b04aed3b82
predigest=5f70bf18a086007016e948b04aed3b82 match=1`; `SNAP: locked=1`;
terminal `FAIL: valid=0 stage=0 code=0` (intentional report stop). Owner
transcription wrapping was normalized separately; `@Gta41EmmcBlock.cpp:8024`
keeps its `l`/`1` ambiguity. `wid=0` is the post-revert width.

Verification and integrity. `scripts/check-gta4l-widthhold-evidence.py`
passes against the exact final image and the normalized lines, including
`partdiag=0` and the amended success predicates; its self-test passes. The
device `refdigest`/`obsdigest` match the host-recomputed first-4,096-byte
prefix and the `WIDTHHOLDREAD` digest matches the 1,024-byte slice at offset
3,072 (`338ebbe6…` is the 1 KiB image hash). Post-boot read-only re-measure
matched all post-flash values, so no unintended BOOT, tail or protected
change exists.

Status. N10.20 stays FAIL and is not reinterpreted; the only discriminator is
the N10.21 structural Content check plus `partdiag`, and this run latched no
PART category. One PASS establishes only this bounded boot: repeatability,
initialization reliability, arbitrary I/O and general 4-bit publication
remain `[U]`. BOOT stays as written (N10.22) and must not be booted again
without a new finite budget; restore is a separate owner decision. Findings:
`docs/emmc-n10.23-widthhold-hardware-run-2026-11-12.md`.

### 5.3.96 N10.24 host-only publication design corrections and Track C disposition (2026-11-12)

Host-only documentation batch after the N10.23 bounded PASS. No device
access, flash, boot, restore, mount, write, driver change, candidate build,
QEMU run or additional commit occurred. The N10.24 documents are corrected
drafts, not accepted implementation contracts.

Publication-path design. `docs/emmc-n10.24-widthpub-design-2026-11-12.md`
now records that N10.23 already observed whole-media and BOOT-partition
publication and four tagged reads while at 4-bit, followed by a verified
revert; what remains unqualified is sustained/general operation and its
lifecycle, not the act of publication. The prior success-path
no-revert/panic-held proposal is withdrawn: a TWRP return demonstrates
recovery access, not a physical power cycle, a reset command or a proven
width reset, and no source-grounded stop/shutdown/error/quiesce contract
exists for leaving the card at 4-bit. The proposed bounded mode (working
token `gta4l-emmc-widthpub=1`, provisional) keeps the N10.23 sequence —
1-bit EXT_CSD baseline, 8-block/4,096-byte 1-bit reference, verified 4-bit
switch, inherited LBA-1 read, advertisement and `registerService()` — and
serves either one four-case pass or two passes of the four two-block
windows, with per-case digest comparison, strict foreign rejection and a
verified pre-report 1-bit revert. The start-path timing is the N10.17
source-derived pre-watchdog budget (28,010 ms success, 36,030 ms failed
switch, 38,230 ms post-switch failure), not the obsolete 13,010 ms
two-block estimate; the report thread is 27,720 ms only because one
3,000 ms total case-loop budget is shared by both passes (a per-pass reset
would reach 30,720 ms and is excluded). The decommission audit records
that the async read path already sends
`kIOMessageMediaStateHasChanged(kIOMediaStateOffline)` after completion and
outside `gate` (`package/Gta4lEmmcBlock.cpp:2313`–`:2316`), while the
report-thread path only clears `online` and does not message clients; a
report-thread notification delta is an open owner decision and is not
claimed as tested. Verdict: **NO-GO for implementation** until the owner
accepts the corrected scope, read set, names, decommission behavior and a
recomputed formatter/timing bound. A long-lived hold mode is NO-GO pending
a source-grounded lifecycle contract and an observed physical power-cycle
reset.

Repeatability protocol.
`docs/emmc-n10.24-widthhold-repeatability-protocol-2026-11-12.md` defines a
finite owner-controlled series on the unchanged installed N10.22 image
(`b4d63be7…`, full p69 `7260ad75…`, tail `a28b4ea2…`, protected partitions
`14e11370…`/`51fa824c…`/`3a265d58…`). Three additional cold boots are a
proposal only, not an approved budget; the owner must select the finite
budget, classify each attempt and approve the read-only device access. Every
launch attempt counts, including pre-identification stops and unusable
captures. The series records planned/attempted/passed/failed/insufficient
counts and stops on the **first non-pass** of any class (pre-identification,
widthhold, watchdog, integrity, failed recovery, ambiguous capture or owner
abort). A TWRP start is recovery-access evidence only, not a physical power
cycle or width-reset proof. No reflash, restore, reset, mount, write, host
reboot, retry or replacement boot is included.

Track C disposition.
`docs/emmc-n10.24-trackc-4bit-coverage-disposition-2026-11-12.md` records
that N10.11/N10.13/N10.14 delivered a six-range **1-bit** sweep (53,248
bytes, 13 subrequests, max 8 blocks, 63 two-block CMD18 chunks) and that the
4-bit evidence covers only four 2-block reads over the first 4,096 bytes.
The block core always chunks at 2 blocks, so the gap is larger driver
subrequests and more bytes, not the CMD18 chunk shape. An 8-block 4-bit
case is conditionally feasible only after a source-derived 4-bit per-case
timing bound (the conservative 4,500 ms per 2-block chunk would put it at
41,220 ms, above the watchdog); 16-block, 32-block and full six-range 4-bit
coverage are NO-GO under the current 30,000 ms watchdog and report contract.
No C2 design, candidate or hardware run is authorized.

Status. N10.23 remains the only widthhold hardware result and N10.20 remains
FAIL. All three N10.24 documents are host-only; no candidate, flash, boot,
mount, write or commit is authorized. Findings:
`docs/emmc-n10.24-widthpub-design-2026-11-12.md`,
`docs/emmc-n10.24-widthhold-repeatability-protocol-2026-11-12.md` and
`docs/emmc-n10.24-trackc-4bit-coverage-disposition-2026-11-12.md`.

### 5.3.97 N10.24 widthhold repeatability owner observation (uncaptured, 2026-11-12)

Owner-reported observation after the approved repeatability series on the
unchanged installed N10.22 image. The owner executed the boots and reports
that every boot repeated the N10.23 `WIDTHHOLD` success shape, including the
matching `WIDTHHOLDREAD` digest and a successful manual return to TWRP; no
non-pass was reported. The agent performed no device access, flash, boot,
restore, mount, write, candidate build, driver change, QEMU run, commit or
push while producing this record.

The observation supplies no raw capture, normalized report lines, verifier
log, cold/warm classification, per-attempt record or pre/post integrity hash,
so it does not satisfy the protocol's PASS predicate and does not upgrade the
evidence state. It is descriptive corroboration only: repeatability in the
SHA-256-bound sense remains `[U]`, N10.23 remains the only captured widthhold
hardware PASS, and N10.20 remains FAIL. The image hashes quoted in the record
are inherited from the N10.23 flash record, not re-measured. No failure rate,
initialization reliability, cold/warm correlation, arbitrary I/O, write,
mount or publication-readiness claim is made. Findings:
`docs/emmc-n10.24-widthhold-repeatability-observation-2026-11-12.md`.

### 5.3.98 N10.24 widthpub host implementation (host-only, 2026-11-12)

Follow-up to §5.3.96 and the owner-accepted corrected design. The host batch
implements the opt-in bounded published-media read mode in
`package/Gta4lEmmcBlock.h`/`.c`/`.cpp` without a candidate build,
ABI/packaging gate, device access, flash, boot, mount or write.

Selector and scope. `gta4l-emmc-widthpub=1` requires exactly one
`gta4l-emmc-block=3` token and refuses an orphan or duplicate block token,
block values other than 3, a malformed, duplicate or non-1 widthpub value,
and every combination with `partobs`, `partcmp`, `partread`, `deblock`,
`breakup`, `sweep`, `consume`, `width` or `widthhold`. An absent token and
the legacy `gta4l-emmc-width=4`/`gta4l-emmc-widthhold=1` paths are unchanged.

Sequence and read set. The mode reuses the widthhold start path: 1-bit
EXT_CSD baseline, 8-block/4,096-byte 1-bit BOOT reference in four two-block
chunks, the verified 4-bit switch (CMD6 `BUS_WIDTH`, CMD13 TRAN wait,
`HOST_CONTROL` apply/readback, read-only EXT_CSD properties comparison; byte
183 is never a switch condition and CMD19/CMD14 is excluded), the inherited
LBA-1 baseline read, the production 64-block/32,768-byte/one-segment
advertisement and `registerService()`. The report thread discovers exactly
one whole media and one pinned BOOT partition with the structural Content
check and first-failure `partdiag`, opens the partition as a read-only client
once and runs two passes over the four two-block offsets 0, 1,024, 2,048 and
3,072 bytes (LBAs 2,255,360/2,255,362/2,255,364/2,255,366), eight tagged
cases total. Both passes share one 3,000 ms case-loop budget checked between
cases; the second pass adds cases, not budget. Each case requires exact
completed counts, a clean stop and a per-case digest equal to the reference
slice; foreign traffic, missing/duplicate/out-of-order cases, short
transfers, read failures, budget expiry and cancellation are strict
failures. On every post-switch exit the thread acquires `gate` with the
bounded timeout and attempts the verified 1-bit revert before the terminal
report; there is no `held` state and no success path without a verified
revert.

Publication marker. The `pub` report field is set to 1 only inside
`runWidthhold()` after `widthholdDiscoverMedia()` has proved exactly one whole
media and one BOOT partition were published while the card and host were at
4-bit. The source comment records that the marker records publication, not
qualification success. A discovery failure leaves `pub=0`; success requires
`pub=1` together with the existing `complete=1`, `partdiag=0`, `revert=1` and
`rverified=1` guards.

Decommission. The batch adds the owner-accepted report-thread offline
notification delta. `notifyOfflineOnce()` latches the first failure under
`reportLock` and sends
`kIOMessageMediaStateHasChanged(kIOMediaStateOffline)` exactly once per
terminal widthpub failure and never on the success path. The report thread
calls it after it has released the controller gate and the media client and
quiesced the in-flight request; a failing case completion also calls it, and
in widthpub mode the async-read failure path routes through the same latch
instead of messaging directly, so a failing case read does not notify clients
twice. The helper is scoped to widthpub mode, so the absent-token 1-bit and
widthhold paths keep their prior behavior; the model is the existing
async-read notification at `package/Gta4lEmmcBlock.cpp:2313`–`:2316`.

Report and bounds. The report family is one terminal `WIDTHPUB` summary plus
one `WIDTHPUBREAD` last-case line on success, or one `WIDTHPUBFAIL` line on
failure, reusing the widthhold field block and adding `pub=` after
`complete=`; widthpub mode suppresses the legacy `READ`/`QUAL`/`QUAL3` and
`PART` lines. Formatter-exact line bounds are 936 bytes for `WIDTHPUB` (the
922-byte widthhold summary, a one-character-shorter prefix and the widest
` pub=<u32>` field), 302 for `WIDTHPUBREAD` and 132 for `WIDTHPUBFAIL`; the
worst report is 2,079 bytes inside the 2,560-byte buffer. The pre-watchdog
start path keeps the N10.17 source-derived bounds: 28,010 ms success, 36,030
ms on a failed switch and 38,230 ms on another post-switch failure. The
report thread is 27,720 ms (8,000 media wait + 2,000 BOOT deadline + 3,000
shared case budget + 4,500 in-flight two-block case + 2,000 revert gate +
8,220 verified revert), leaving 2,280 ms under the 30,000 ms watchdog; the
bounds are conditional and exclude scheduler stalls.

Host results and checker. The block suite passes 48 core scenarios with 38
mutation controls and 370 adapter scenarios (371 PASS lines) with the
widthpub full-pass, publication, budget, read/short/match/foreign/cancel/
gate/revert/baseline/switch failures, media-absent and selector-refusal
cases, plus adapter/support controls for single-pass, window-collapse,
pub-cleared, pub-before-discovery, offline-missing and offline-latch and
policy controls for value-accepted, duplicate-accepted and
widthhold-conflict-accepted; core 189/117, probe 196/100 and the documented
evidence counts pass. The new host-only `scripts/check-gta4l-widthpub-evidence.py`
recomputes the window and last-case slice prefixes from the final packaged
BOOT image, requires the success shape, `pub=1`, `partdiag=0` and a verified
revert, and rejects a digest mismatch, a one-byte-changed image, a
missing/duplicate/extra field or line, a nonzero mismatch/foreign/cancel/
budget, and a `WIDTHHOLD`-labelled summary; `--self-test` passes. The checker
does not read a device, firmware or candidate kernel.

Status. The host batch itself performed no device access, flash, boot, mount
or write. A host-only candidate was later produced from this batch (prelinked
kernel `e65d0625…`, audit `out/gta4l/storage-abi-e65d06259ae4`, payload
`c852dc80…`, BOOT image `72759594…`); the production record is
`docs/emmc-n10.24-widthpub-design-2026-11-12.md` §8. One owner-approved
BOOT-only flash and one owner-triggered cold boot then returned the widthpub
success shape with `pub=1`, eight passes, `partdiag=0`, a verified revert and
the expected window digests; the run record is
`docs/emmc-n10.24-widthpub-hardware-run-2026-11-12.md`, and the one-boot
budget is consumed. N10.23 remains the only captured widthhold hardware PASS
and N10.20 remains FAIL; general 4-bit publication, sustained or arbitrary
I/O, repeatability and initialization reliability remain `[U]`. Findings:
`docs/emmc-n10.24-widthpub-design-2026-11-12.md` and
`docs/emmc-n10.24-widthpub-hardware-run-2026-11-12.md`.

## 6. Expected report

On a full success the probe panics with a report containing:

- MCI version, RCA, capacity, EXT_CSD revision, bus width, OCR, EXT_CSD sum,
  `HOST_CONTROL2`, `CORE_VENDOR_SPEC`, `CORE_DLL_CONFIG`, `CORE_DLL_STATUS` and
  power status;
- CID and CSD words;
- block 0 and block 1 sums and selected words, including the protective MBR
  signature area and the GPT header fields;
- the gated GPT values `elba=2 n=84 sz=128 first=34 last=61071326
  backup=61071359`, the per-LBA probes for the 21 primary-array blocks
  (LBAs 2..22), the first usable LBA (`DATA 34`) and the last sector
  (`LSTLBA=61071359`), and the `step=18`/`log=30` stage-A counters.
- In the optional MD mode (`gta4l-emmc-mb=1`) the header line gains ` mb=1`,
  `BLOCKS` is replaced by `MBLOCKS 2=… 22=…` carrying the long CMD18 request
  sums, and the `LSTLBA` line gains `mbsame=1` or `mbsame=0
  mbdiv=<first differing LBA>` followed by
  `drain=used/resolved/blocks/iterations`. The counters are `step=19`/`log=55`.
  A following `GTA4L-EMMC PROBE MB clk=<hex>` line reports the short request
  (`s=`), the long request (`l=`) and the recovery reads (`rec=`). Each request
  group is `result/requested/completed/validated/first-divergence/stop-result/
  stop-normal/stop-error/stop-response0`; `rec=` is
  `result/same/first-divergence`. An MD failure emits
  `GTA4L-EMMC PROBE FAILED: mb rc=<verdict> stage=<barrier> step=19
  mb=<block> mbc=<completed> att=<short><long>`, the same `s=` and `l=`
  request groups, an eight-field
  `rec=result/block0-result/lba2-result/attempted/compared/same/div/div-valid`
  group and the terminal `fail=` record. A barrier that did not run keeps its
  groups clear (`att=0`, zero results) and the success report is unreachable.
- In the Stage-B1C mode (`gta4l-emmc-probe=5`) the same B1 form adds `bc=`,
  `e183a=` and `esa=` to the `BW=4` line: `bc=1` marks a continuation that
  ran, and the two re-read fields are zero when the continuation did not
  reach the re-read. Stage-4 reports keep the earlier form unchanged.
- In the Stage-B1 mode (`gta4l-emmc-probe=4`) the probe emits the
  `GTA4L-EMMC B1 REPORT` form instead: the header plus one `BW=1` baseline
  line and one `BW=4`/`BW=8` line per rung (`att`, `req`, `sw`, `swrc`, `tt`,
  `tw`, `te`, `trc`, `hc`, `hb`, `ha`, `c6`, `es`, `b0`, `b1`, `same`, `div`),
  the per-width `BLOCKS`/`DATA`/`LSTLBA` content lines, the `PAD` register line
  with before/after samples, and the terminal `FAIL` record. §5.3.9 lists the
  field bounds and stop codes; §5.3.11 lists the readback-cause fields. The
  report fits the 2048-byte buffer at the field-wise maximum.

Stage 2 stops with `GTA4L-EMMC HOST REPORT` and stage 3 with
`GTA4L-EMMC IDENT REPORT`. The §5.3.4 candidate completed the stage-3
identification report on three units; the §5.3.5 stage-1 image then completed
the full report, including the block 0/block 1 reads, on the owner's SM-T505N
and one SM-T505; the §5.3.6 stage-A image completed the extended content
phase on the owner's SM-T505N and matched the live recovery readback; the
first §5.3.7 MD-mode attempt then stopped in identification before the MD
phase, the corrected second attempt reached the MD phase but timed out at
block ordinal 20 with an incomplete `CMD12` stop, the first §5.3.8 attempt
again stopped in identification at CMD3, and two later boots of the same
§5.3.8 image completed identification: one exercised the drain (which did not
resolve the stall) and one failed the stage-A array read, so an on-device
`drain=` value exists but no multi-block report exists yet. The §5.3.9 B1
form is host-tested; its 2026-11-02 hardware run stopped at the 4-bit readback
(§5.3.10), so there is no B1 success report; the §5.3.11 readback-cause fields
are host-tested only and have not run on a device. On
failure the probe reports the
stage, result code,
the `clocks` flag, MCI version, power/clock/`HOST_CONTROL2`/vendor/DLL registers,
power-control status, GCC CFG/CMD/BCR/APPS-CBCR/AHB-CBCR, the current or last
completed protocol step (the stage-A and MD phases set it on entry, so for
those failures it names the active phase), the terminal command-failure record
(valid flag, index, argument,
encoded command, error/normal status, present state and failure step) and the
last log event. SDHCI registers are read only when the AHB/APPS branches were
enabled; otherwise `clocks=0` marks them as not sampled and the GCC state is
still reported. The report is a bring-up checkpoint, not a storage or filesystem
result. The older stage-3 transcript predates the failure record and DLL
fields, so it identifies the failing command only as a CRC-checked command after
`CMD0`. The 5.3.1 `cmd2window` report contains the failure record and the
response windows; it identifies CMD3 as the terminal command. The 5.3.2
`fmt=2` report identifies CMD7 as the terminal command and shows TC without CC
at the timeout; see `docs/emmc-cmd7-timeout-finding.md`. The working tree's
stage-3 capture block is `fmt=4`; it appends the CMD7 fields described in
§5.3.2 to both the failure and success reports.

## 7. Limits and residual risks

- **Every hardware run is owner-gated.** Each run was made under the owner's
  approval for that exact procedure, with verified EFS/sec_efs backups and a
  known-good BOOT restoration image. The `sdhc-bus-power-activation` NO-GO
  still applies to any operation that has not been separately approved.
- **Initialization reliability is not established.** Pre-identification
  stops (the H8 CMD3 class) occurred in many probe boots and remain
  unexplained; the post-2026-11-05 N-series images have not shown one in a
  captured record, but the sample is small. A rung's verdict counts a boot
  only when the image reaches its report path.
- **Most results are single-boot observations.** N10.1 repeated its read set
  on four cold boots; other repeatability claims are owner-reported and
  uncaptured. Sustained, arbitrary and concurrent I/O are `[U]`.
- **The measured width is 4-bit SDR at 400 kHz.** 8-bit is untried in the
  port even though TWRP runs this eMMC at HS400 Enhanced Strobe; DDR, HS200,
  HS400 and tuning do not exist.
- **HS_TIMING is probe-only and 1-bit.** The 50 MHz floor passed a content
  comparison and reverted cleanly in the probe (5.3.20). The block driver
  still runs at 400 kHz with no high-speed entry, and the controller's HISPD
  bit is not set anywhere in the port.
- **No DMA, SMMU programming, CQE, ICE or interrupt completion exists.**
  Every transfer is polled PIO. The DMA/SMMU ownership question is open.
- **Publication is bounded.** Only the tested GPT and partition windows are
  qualified; the driver does not establish a filesystem, a root volume, an
  APFS container or a mount.
- **No card writes exist.** CMD24/CMD25, erase, cache and reliable write are
  absent, so no persistence claim can be made.
- **Power and pads are policy, not measurement.** The port uses retained
  rails and does not measure L22/L5/L24/L11, the pad levels, or retention
  across suspend and shutdown. 1.8 V signalling follows the documented board
  policy derived from the L11 range. A pad-related failure is a policy
  question first, not proof that the rail is wrong. The power acknowledgement
  is controller-local: `BUS_SUCCESS` does not prove the rail voltage, and a
  new request raised after both completions and after the card clock starts
  still has no interrupt path.
- **The diagnostic window is short.** The report path is bounded by a 30,000
  ms watchdog. At 400 kHz a megabyte-scale read cannot fit, and an iPadOS
  root cannot mount in practice, so speed is on the critical path.
- **microSD/SDHC2 is out of scope.** Only SDHC1/eMMC is implemented.
- **Vendor correspondence is open.** The exact vendor source/binary
  correspondence and the firmware runtime slot table remain `[U]`.
