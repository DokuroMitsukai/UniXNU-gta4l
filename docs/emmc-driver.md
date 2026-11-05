# gta4l native SDHCI v5 / eMMC core

Status: host-tested implementation `[S]`; stage 2 (controller/host init only)
ran on the tablet and reported success `[C]`; stage 3 (identification) ran and
stopped at a command CRC error with no identification report in the first run
`[C]`; the later `cmd2window` run completed CMD2 and stopped at a CMD3
completion timeout, still with no identification report `[C]`; the later
`fmt=2` run completed CMD2, CMD3 and CMD9 and stopped at a CMD7 R1b completion
timeout, still with no identification report `[C]`; the 2026-11-01 `fmt=4`
fail-closed runs (first on an SM-T505, then on the owner's SM-T505N, per the
owner) failed at CMD2 with the same `-8` record as the first current-source
run, so the CMD7 capture was not exercised `[C]`; the 2026-11-01 `cmd1gap`
candidate added only an 8-us post-ready-CMD1 delay and completed full stage-3
identification on the owner's SM-T505N and two owner-reported SM-T505s,
including the CMD7 CC+TC capture `[C]`. The 2026-11-02 stage-1 image then
completed identification and both read-only `CMD17` reads (blocks 0 and 1) on
the owner's SM-T505N and one of those SM-T505s `[C]`; the corrected A2/F2
attempt then reached block ordinal 20 in one open-ended `CMD18` before the
data bound expired `[C]`, and the §5.3.8 bounded drain reproduced the same
stall on a later boot without resolving it `[C]`. Stage B1 (§5.3.9) is
implemented and host-tested; the first B1 image was flashed and booted three
times (§5.3.10), and only one boot reached B1 before stopping at the 4-bit
byte-183 readback. The B1R2 report-completeness re-run (§5.3.12) then booted
the rebuilt image five times: two boots reproduced the 4-bit stop with the
new fields and three stopped before B1, so F3 remains `[U]`. The B1C
continuation and the 1-bit discriminator then timed out in the CMD19 write
phase before any CMD6 (§5.3.14, §5.3.16), and the B1C-2 audit found no
controller data-timeout in either hang class (§5.3.18). The Stage-B2
HS-timing image then booted four times (§5.3.20): two warm boots accepted
`HS_TIMING=1`, matched the full stage-A content pass at the 50 MHz floor
and reverted cleanly, so F4 is measured PASS at 1-bit/50 MHz `[C]`; the
cold boot and one warm boot stopped pre-B2 at the H8 CMD3 timeout. Card
identification,
single-block sector reads and
a partial multi-block data phase are therefore observed; a complete
multi-block read, a filesystem, block-device publication and DMA/CQE/ICE
remain `[U]`, and F3 is measured only at 4-bit SDR (`[C]`, §5.3.34) with
8-bit untried. The 2026-11-04 protocol correction (§1a) withdraws the
byte-183 refusal, H12 DDR, stage-7 timeout and H13 CMD14-CRC causal claims
and fixes the underlying width, bus-test, timeout and CMD18-readiness logic.
The corrected width rung is H15 (`gta4l-emmc-probe=15`). Its first cold boot
(§5.3.31) verified the 4-bit SDR switch (`vf=1`, host `00→02`) and then hit
the CMD19 write-phase timeout at 4-bit, after which the old revert was blocked
by a latched DAT inhibit and no CMD6 was issued. The report and recovery are
fixed and a content-only H16 rung is added; its first boot (§5.3.32) returned
`rc=0 stop=0` with the verified 4-bit switch, the full 4-bit content pass, the
verified revert and the block-0 recovery read, so 4-bit SDR reads are measured
`[C]` and the remaining barrier was the CMD19 completion rule. Stage H17
(`gta4l-emmc-probe=17`, §5.3.33) applies the reference rule — tolerate the
expected CMD19 data timeout, reset the data circuit, let CMD14 decide — after
the 4-bit content pass. Its fourth boot (cold, then three warm; §5.3.34)
returned `rc=0 stop=0` with the recorded CMD19 timeout (`wrc=-11`), a clean
bounded data reset (`rst=0`), the CMD14 pattern match (`trc=0`,
`tw=te=000000a5`), the matching 4-bit content pass, the verified revert and
the recovery read, so **F3 is measured PASS at 4-bit SDR, 400 kHz `[C]`**.
The 8-bit width is untried and stays open separately. H14 remains paused. The
core is
opt-in. The runs were made under the owner's approval for each exact procedure,
with verified EFS/SEC_EFS backups and a known-good BOOT restoration image. The
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
| `package/Gta4lEmmcPlatform.c/.h` | Board MMIO mapping, width-specific register access, barrier and generic-counter timing behind the ops interface |
| `package/Gta4lEmmcBlockCore.c/.h` | Portable read-only block request helper: validation, per-block loop, bounce copy, counters |
| `package/Gta4lEmmcProbe.c` | Opt-in staged XNU probe: maps MMIO through the platform object, runs the core and reports |
| `scripts/sim-gta4l-emmc/harness.c` | Simulated controller and eMMC card with injectable failures and independent register/command checks |
| `scripts/test-gta4l-emmc.py` | Compiles the core against the simulator, runs 179 scenarios and 104 mutation controls |
| `scripts/sim-gta4l-emmc-probe/harness.c` | Simulated SDHCI/GCC windows, eMMC card and kernel stubs for the probe report path |
| `scripts/test-gta4l-emmc-probe.py` | Runs the probe report, 163 scenarios and 77 mutation controls |
| `scripts/sim-gta4l-emmc-block/harness.c` | Mock read/copy transport for the portable block request helper |
| `scripts/test-gta4l-emmc-block.py` | Compiles the helper and covers 23 scenarios plus 18 mutation controls |
| `scripts/build-gta4l-emmc-probe.py` | Builds `libGta4lEmmcCore.a` (core, platform, block helper) and `libGta4lEmmcProbe.a` with the `bsd_init.o` compile record; rejects duplicate globals |
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
  `gta4l-emmc-mb=1` mode additionally submits one open-ended `CMD18` read and
  an explicit `CMD12` stop; the host `AUTO_CMD12` enable bit stays clear. There
  is no `CMD24`/`CMD25`, DMA, CQE, ICE or tuning operation.

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
not tolerated). The probe suite is 163 scenarios and 77 mutation controls
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

The Stage-C/F5 publication design is a separate host-only document:
`docs/gta4l-storage-publication-design.md`. It surveys the prelinked
IOStorageFamily personalities, the in-tree `IOBlockStorageDevice` templates
and the existing CMD17 core, and proposes a read-only publication rung with
no code or device action yet.

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
  `BLOCKS` is replaced by `MBLOCKS 2=… 22=…` carrying the CMD18 pass sums, and
  the `LSTLBA` line gains `mbsame=1` or `mbsame=0 mbdiv=<first differing LBA>`
  followed by `drain=used/resolved/blocks/iterations`; the counters are
  `step=19`/`log=51`. An MD failure line carries the same `drain` field
  between `mb=` and `stop=`.
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

- Stage 2 ran on the tablet `[C]`; stage 3 ran and stopped at a command CRC
  error in the first run `[C]`; the later `cmd2window` run completed CMD2 and
  stopped at a CMD3 completion timeout `[C]`; the later `fmt=2` run completed
  CMD2, CMD3 and CMD9 and stopped at a CMD7 R1b completion timeout `[C]`; the
  2026-11-01 `fmt=4` runs failed at CMD2 with the same `-8` record, so they did
  not exercise the CMD7 capture `[C]`; the 2026-11-01 `cmd1gap` candidate
  completed identification, including CC+TC on CMD7, on three units `[C]`; the
  2026-11-02 stage-1 image completed identification and the read-only block 0
  and block 1 `CMD17` reads on the owner's SM-T505N and one SM-T505 `[C]`; the
  §5.3.6 stage-A image then read the full 84-entry GPT array, the first usable
  sector and the last sector on the owner's SM-T505N, and every sum matched a
  same-session live recovery readback `[C]`; the §5.3.7 attempt 2 then
  stopped at block ordinal 20 in one open-ended `CMD18` (at least 20 of 21
  blocks drained) before the data bound expired, but the transfer and the
  `CMD12` stop did not complete `[C]`; the §5.3.8 attempt 3 was flashed and
  booted and, on later boots of the same image, reached the MD phase, where
  the bounded drain exhausted its window without draining another block or
  seeing `TC` and the `CMD12` stop again timed out `[C]`. A
  complete multi-block transfer, a filesystem, block-device publication and a
  reliability test have not run `[U]`.
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
- No DMA, SMMU programming, CQE, ICE or tuning exists `[U]`. High-speed
  timing is measured PASS at 1-bit/50 MHz (§5.3.20) `[C]`, but no high-speed
  data, multi-bit, DMA or interrupt result exists `[U]`.
  The Stage-B1 4/8-bit bus-width switch and CMD19/CMD14 bus test are
  implemented and host-tested (§5.3.9) and ran on 2026-11-02 (§5.3.10); the
  only boot that reached B1 stopped at the 4-bit byte-183 readback, so F3
  remains `[U]` and no write path exists. The optional A2/F2 multi-block mode is implemented and
  host-tested (5.3.7); its corrected 2026-11-02 attempt reached the MD phase
  and stopped at block ordinal 20 in one `CMD18` before the data bound
  expired, and the `CMD12` stop did not report `CC` or `TC`, so a complete
  multi-block transfer result still does not exist `[C]/[S]/[U]`. Attempt 3
  (5.3.8) adds a bounded post-bound drain and host simulator coverage; it was
  flashed and booted three times. The drain ran on the second boot and did
  not resolve the stall (no additional blocks, no `TC`, timed-out stop), and
  the third boot failed earlier in the stage-A `CMD17` read, so F2 remains
  `[U]`.
- An 8-us CMD1-to-CMD2 timing candidate is specified in
  `docs/emmc-cmd1-gap-candidate.md` and was built, flashed and run on
  2026-11-01; it completed identification on three units (5.3.4) `[C]`. The
  8-us delay is not proven necessary, because earlier builds passed CMD2
  without it, and it is not adopted as a general fix `[S]/[U]`.
- No block-device publication, mount, root switch or persistent Data path
  exists `[U]`.
- microSD/SDHC2 is not implemented; only SDHC1/eMMC is in scope `[U]`.
- The exact vendor source/binary correspondence and the firmware runtime slot
  table remain `[U]`.

Stage 2 is complete. Stage 3 now completes identification on the §5.3.4
candidate; the historical stage-3 transcript still bounds the older CMD2
`CMD_CRC` error only to a CRC-checked command after `CMD0`, and the cause of
that earlier failure remains open. The current code records the terminal
command index, argument, encoded command register, interrupt/error status,
present state and last completed step before clearing the latches, and gates
its SDHCI diagnostics on the branch clocks. The follow-up audits added
retirement of recovered `CMD1` timeouts, simulator coverage for failed
branch-clock enables, recovered and exhausted `CMD1` timeouts, the reset pulse,
branch-clock ordering and the DLL reset-before-power-down sequence, and a host
test that executes the probe failure report with simulated MMIO and requires
zero SDHCI accesses when the clocks are off. The `fmt=2` missing-CC CMD7
timeout did not reproduce on the candidate: the CMD7 poll/response capture
completed with CC and TC on three units, so the capture gap is closed but the
historical timeout is still unexplained. The stage-A extended content read is
complete: repeated CMD17 reads covered the GPT header and the full 84-entry
primary array, and the live recovery readback bound every sum. The A2/F2
multi-block rung remains partial after the attempt-3 runs (§5.3.8): the
open-ended CMD18 is proven beyond one block, but the ordinal-20 stall
reproduced and the bounded drain did not resolve it, so F2 is not established.
The ladder's next rung is Stage B (bus width and speed); the Stage-B1
bus-width rung is implemented and host-tested (§5.3.9) and ran on 2026-11-02
(§5.3.10). The B1R2 report-completeness re-run (§5.3.12) then booted the
rebuilt image five times: two boots reproduced the 4-bit stop with a clean
CMD6 R1, the host width bit set and an EXT_CSD readback still at 1-bit, and
three stopped before B1 (two TC-only CMD17 timeouts, one CMD3 timeout), so
F3 remains unestablished. The B1C continuation (§5.3.13.1) was then flashed
BOOT-only and booted seven times (§5.3.14): both boots that reached the
continuation timed out in the CMD19 write phase (CC + BufferWriteReady
latched, no TC, no error), so the data-write direction is now the blocking
observation and F3 remains `[U]`. The stage-6 1-bit CMD19/CMD14 discriminator
(§5.3.15) was flashed and booted thirteen times (§5.3.16); both boots that
reached the rung timed out in the 1-bit CMD19 write phase before any CMD6, so
the write failure is width-independent and F3 remains `[U]`. The Stage-B1C-2
data-timeout audit (§5.3.17–5.3.18) then found that the controller never
raises data-timeout in either hang class: five DTO boots were silent while
data streamed past the armed minimum. The Stage-B2 HS-timing rung (§5.3.19)
was built, flashed BOOT-only and booted four times (§5.3.20); two warm boots
accepted `HS_TIMING=1`, matched the full stage-A content pass at the 50 MHz
floor and reverted cleanly, so F4 is measured PASS at 1-bit/50 MHz `[C]`
(content-level only), while the cold boot and one warm boot stopped pre-B2
at the H8 CMD3 timeout. F3 remains `[U]`, the pre-B1 H8/CMD17 stops remain
open, and any further ladder step is owner-gated. No filesystem,
block-device publication, mount, storage write, DMA, CQE or ICE result exists
yet.
