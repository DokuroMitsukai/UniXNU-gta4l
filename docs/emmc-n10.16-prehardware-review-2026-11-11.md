# gta4l N10.16 pre-hardware readiness review (2026-11-11)

Status: host-only source and evidence review. No device access, flash, boot,
mount or physical MMIO was performed. Verdict: **NO-GO for the N10.16
hardware run as currently designed**. The C2 candidate remains host-qualified,
but its timing contract does not yet bound the report-thread gate wait.

## 1. Inputs

- Candidate record: `docs/emmc-n10.16-width4-candidate-2026-11-10.md`
- Candidate kernel `4a7d254e…`, payload `fafca83a…`, BOOT `f18694b7…`
- Readiness checklist: `docs/emmc-n10.16-4bit-sdr-readiness-2026-11-10.md` §10
- Driver: `package/Gta4lEmmcBlock.cpp`, policy/formatter:
  `package/Gta4lEmmcBlock.c`, core: `package/Gta4lEmmcBlockCore.c`,
  transfer: `package/Gta4lEmmc.c`

## 2. Finding: digest field is 16 bytes, not 16 hex characters

`gta4l_emmc_block_put_digest()` (`Gta4lEmmcBlock.c:1587-1598`) takes a byte
count and emits two hex characters per byte. The width formatter passes
`16u` (`Gta4lEmmcBlock.c:2723`), so `WIDTHREAD` prints **16 bytes = 32 hex
characters** for each of `digest` and `predigest`, not 16 hex characters.

The correct final-image prefix is:

`b29e23fda6d6a1dfc969c652a17f7ae6`

from the full first-1,024-byte SHA-256
`b29e23fda6d6a1dfc969c652a17f7ae64d55eef8706bdf6f3ccba4fc170beb66`.

The initial verifier, candidate record and checklist used the shorter
16-character value. They were corrected in this review. The candidate image
is unchanged; the formatter always emitted 32 characters. The verifier
self-test and final-image fixture were rerun with 32 characters and pass.
This finding is evidence that the review, not the candidate build, was
previously incomplete.

## 3. Timing ownership

### 3.1 Pre-watchdog width path

`start()` runs the width sequence at `Gta4lEmmcBlock.cpp:1470-1532` before the
report and watchdog threads start at `:1592-1622`:

1. 1-bit EXT_CSD baseline.
2. 1-bit two-block BOOT reference read.
3. verified 4-bit switch.
4. existing baseline LBA 1 read at 4-bit.

The 24,230 ms design figure is split as
`GTA4L_EMMC_BLOCK_WIDTH_REPORT_MS` (11,220) plus a 13,010 ms pre-watchdog
budget (`Gta4lEmmcBlock.h:283-284`, static asserts
`Gta4lEmmcBlock.cpp:163-171`). The 13,010 ms is a conditional design budget,
not a source-derived sum of every command timeout and retry on those paths.
The watchdog does not cover this prefix. If the prefix overruns, total time
is prefix plus report/watchdog time; the 30,000 ms watchdog cannot bound it.

### 3.2 Report budget and watchdog origin

`runWidth()` records `startTicks` before the blocking `IOLockLock(gate)`
(`Gta4lEmmcBlock.cpp:5908-5920`). The 11,220 ms report budget therefore
includes time spent waiting for the controller gate, the 4-bit read, the
comparison and the revert. `width_budget` is set when that elapsed time
reaches 11,220 ms (`:5965`).

The watchdog starts only after `start()` returns, after service registration
and after both the report and watchdog threads are launched (`:1586-1622`).
Its 30,000 ms interval is measured from that point, not from the beginning of
the width sequence. A no-contention success could finish at 24,230 ms from
the width start, but the watchdog's expiry is at roughly 13,010 + 30,000 =
43,010 ms from that same origin. The candidate's "24,230 against 30,000"
comparison is therefore not an apples-to-apples end-to-end bound.

### 3.3 Unbounded report gate wait

The report thread blocks on `IOLockLock(gate)` with no timeout. The adapter
advertises a 64-block read limit (`Gta4lEmmcBlockCore.h:22`), and the core
splits it into 32 two-block CMD18 chunks (`:25-27`,
`Gta4lEmmcBlockCore.c:132-...`). Each chunk can consume up to:

- `GTA4L_EMMC_CMD_TIMEOUT_US` = 1,000 ms in `gta4l_emmc_wait_inhibit()`
  (`Gta4lEmmc.c:599-616`), and
- `GTA4L_EMMC_DATA_TIMEOUT_US` = 1,000 ms in `gta4l_emmc_read_multi()`
  (`Gta4lEmmc.c:3659` and following).

A slow but successful 64-block in-flight request can therefore hold the gate
for up to roughly 32 x 2,000 ms = 64,000 ms before the report thread even
starts its own read. The 30,000 ms watchdog can fire first and emit a
watchdog report instead of `WIDTH`. That is a no-go observation, and it is
not covered by the 11,220 ms report budget.

A failure usually stops earlier, but the success-side gate hold is not
bounded by an aggregate deadline. Publication also produces incidental
traffic; requiring an externally "quiet" media is not a proof.

## 4. Report acceptance

A hardware acceptance decision must require all of:

- `WIDTH complete=1` with `cases=1`, `passed=1`, `failed=0`, `reason=0`,
  switch/revert fields, `reads=2`, `bytes=2048`, `blocks=4`,
  `subrequests=2`, `max=2`, `match=1`, `mismatches=0`, `fails=0`,
  `cancel=0`, `budget=0`, clean stops and the production
  `limit=64 seg=1 cap=61071360`;
- `WIDTHREAD` with `digest` and `predigest` each equal to the 32-hex prefix
  above, `match=1`, and the fixed geometry;
- no `WIDTHFAIL`;
- verified 4-bit switch and verified 1-bit revert;
- a complete, coherent report with no truncated or missing fields.

`scripts/check-gta4l-width4-evidence.py` checks the `WIDTHREAD` content line
and its geometry. It does not accept the summary, switch/revert or completion
guards by itself. A watchdog report (`rc=16 stop=16 fail_stage=16`) or a
pre-identification stop is a non-pass, not a width failure diagnosis and not
an automatic retry trigger.

## 5. Verdict and recommended next batch

**NO-GO for hardware.** The pre-watchdog prefix is conditional rather than
derived, and the report gate wait can exceed the watchdog before the width
case starts. The 24,230 ms figure does not cover the 64-block in-flight
request case. A hardware run based on it would risk a watchdog report and a
misclassified result.

Recommended host-only corrective batch before any execution proposal:

1. Add a bounded gate acquisition in `runWidth()` (or an equivalent proven
   exclusion), with a deterministic terminal path that leaves media offline,
   preserves the first failure and does not claim a revert that did not run.
2. Derive the pre-watchdog budget from the actual command timeouts/retry
   counts, or add an explicit bound with tests, instead of treating 13,010 ms
   as established.
3. Extend host tests/mutations for gate-timeout, watchdog-race, no-revert
   terminal framing and the 64-block in-flight case.
4. Rebuild and rebind the candidate if code changes; rerun the ABI, payload,
   BOOT and host suites.
5. Then return to the owner for CMD6/read-scope, timing and finite-budget
   approval.

Until then, the candidate is host-qualified only. F5/F3 remain their earlier
bounded observations; initialization reliability remains `[U]`. No device
action is authorized.

## 6. Corrective update (2026-11-11)

The bounded-gate corrective batch was implemented and host-tested:

- `runWidth()` now acquires the gate with a 2,000 ms try-lock loop
  (`GTA4L_EMMC_BLOCK_WIDTH_GATE_MS`); a timeout sets reason 8
  (`GTA4L_EMMC_BLOCK_WIDTH_REASON_GATE`), takes media offline, skips the read
  and does not claim a revert.
- `panicWithReportNoGate()` uses a try-lock snapshot so the terminal report
  cannot block on the held gate.
- The report budget is 13,220 ms (2,000 gate + 3,000 read + 8,220 revert) plus
  the conditional 13,010 ms pre-watchdog path, for 26,230 ms.
- `width-gate-timeout` simulates a held gate and checks reason/stage 8, no
  `WIDTHREAD`, media offline, no read/revert counters and the card left at
  4-bit; `width-gate-release` releases the gate during the bounded wait and
  requires the full success path and verified revert. Deadline tests release
  exactly at 2,000 ms (success) and 10 ms later (terminal timeout), and a
  watchdog pass after the timeout must not produce a second terminal report.
- The block suite passes 48 core scenarios/38 core mutation controls and 285
  adapter scenarios with 104 adapter/support and 10 policy mutation controls;
  core 189/117, probe 196/100, and the packaging/ABI/fixture/evidence checks
  pass.

The corrective candidate is
`docs/emmc-n10.16-width4-gatefix-candidate-2026-11-11.md`: kernel
`2be2eb06…`, payload `10ff7ee8…`, BOOT `58352cd3…`, first-1,024-byte prefix
`a6da82e356863d1ab7e31c21a8fd85fc`. The unbounded report-gate finding is
closed at the source and host-test level. The pre-watchdog 13,010 ms path
remains a conditional design budget. The owner approved one controlled boot on
2026-11-11 under
`docs/emmc-n10.16-width4-hardware-protocol-2026-11-11.md`; the run completed
with the driver's `complete=1` guard and the pre-flash BOOT image was restored
and verified. This host lacks `adb`/`fastboot`, so the device actions were
performed from the owner-controlled host. The measured result is in
`docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md`; this review
itself remains a pre-run assessment, not a hardware result.
