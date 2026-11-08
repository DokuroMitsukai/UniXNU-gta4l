# gta4l N8 bounded partition-publication observation design (host-only, 2026-11-07)

**Status:** N8.1 host-only design, N8.2 host-only implementation and N8.3
host-only candidate production, 2026-11-07. The N8.1 batch performed no device
access, boot, flash, candidate build, driver or transport change, QEMU run,
commit or push; the separately approved N8.2 batch added the opt-in host
implementation and tests described in section 8; the separately approved N8.3
batch built and gated the candidate described in section 10. It follows the N5
mode-3 observation
(`docs/emmc-n5-integration-observation-2026-11-07.md`) and the N6 reliability
control (`docs/emmc-n6-reliability-observation-2026-11-07.md`). The N5 and N6
boot budgets are consumed; no further flash, boot or retry is authorized.
Initialization reliability remains a separate open issue.

## 1. Why the current partition snapshot is not a measurement

N5 reported:

```
GTA4L-EMMC P5 PART: count=22 expected=84 waited_ms=0 error=0 reported=3
```

The reporter behind that line is not a publication-completion barrier:

- `package/Gta4lEmmcBlock.cpp` waits for the whole `IOMedia` for up to
  `GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS` (20,000 ms), reads LBA 1 and derives
  `report.part_expected` from the GPT header's `NumberOfPartitionEntries`
  field through `gta4l_emmc_block_gpt_entries()`. That value is the
  entry-array **slot count** (84 on the validated GPT), not a populated count.
- The partition loop runs for at most `GTA4L_EMMC_BLOCK_PART_WAIT_MS`
  (2,000 ms) and breaks on the **first nonzero** count. It then takes one
  immediate re-sample and overwrites `part_count`, `part_reported` and the
  first three `part[]` entries from that second walk.
- `countPartitions()` is a fresh `IOService` registry walk. It takes no
  controller gate, counts non-whole `IOMedia` children whose provider chain
  reaches the driver, and sets `part_error = 15` if service matching fails.
- `IOGUIDPartitionScheme::start()` publishes its scan result one media at a
  time. A count concurrent with that loop can observe a prefix.

The validated recovery GPT has 84 slots and 80 populated entries. A report of
`count=22` therefore says only that 22 partition media existed at the instant
of the sample. It cannot distinguish:

- publication still in progress;
- a stable partial count (for example a scan that stopped early);
- complete publication of 80 entries;
- no publication yet (a zero count at the deadline is also reported as a
  non-error today);
- a registry-walk failure (that one case does set `error=15`).

`expected=84` makes the line look like a completeness check even though it is
a slot count. N8 replaces the inference with a bounded observation and stops
short of any completeness claim.

## 2. Decision: an opt-in observation token

N8.1 chooses a new diagnostic token, `gta4l-emmc-partobs=1`, that is
orthogonal to the read/report selector:

- `gta4l-emmc-block=1` (CMD17 report) and `gta4l-emmc-block=3` (chunked
  report) keep their existing semantics and report lines.
- `gta4l-emmc-block=2` remains refused.
- `gta4l-emmc-partobs=1` requires exactly one block token with value 1 or 3.
  A `partobs` token without a block token, a duplicate `partobs` token, any
  value other than 1, and any conflicting or duplicate diagnostic token are
  refused before MMIO, matching the existing selector rules.
- When the token is absent, the legacy `GTA4L-EMMC P5 PART:` line and the
  mode-1/mode-3 behavior are byte-for-byte unchanged. When it is present, the
  report emits the bounded `PARTOBS` line described in section 4 instead of
  the legacy `PART` line, and the `PART1..3` sample lines remain.

Why this choice:

- A silent mode-3 reporter revision would change the instrument that produced
  the accepted N5 artifact and make the two mode-3 reports non-comparable.
- A new block-mode value (for example `=4`) would conflate the read/report
  mode with the observation instrument and enlarge the selector surface.
- The orthogonal token keeps the read path fixed and makes the observation
  change explicit. The cost is diagnostic plumbing in the payload builder,
  checker, packager and their tests; it is host-only work and adds no
  transport, width, timing or write capability.

## 3. Bounded observation protocol

The observation window starts only after the whole media is published, opened
read-only, and the existing LBA 1 GPT-header read completes. Publication that
happened before this point is invisible to N8; the report must not imply
otherwise.

Proposed constants (to be fixed during N8.2 host measurement, not changed
silently later):

| Constant | Value | Purpose |
| --- | --- | --- |
| `GTA4L_EMMC_BLOCK_PART_OBSERVE_MS` | 4000 | overall deadline from the first sample |
| `GTA4L_EMMC_BLOCK_PART_OBSERVE_STEP_MS` | 50 | sleep between samples, report thread only |
| `GTA4L_EMMC_BLOCK_PART_STABLE_MS` | 250 | count-unchanged window required for `stable=1` |

Algorithm:

1. Take the first sample immediately at nominal `t = 0`; record `first`,
   `first_ms = 0`, and the first nonzero count/time (`first_nz`,
   `first_nz_ms`).
2. If the count is nonzero and has been unchanged for at least
   `PART_STABLE_MS` of accumulated nominal time, stop with `stable = 1`.
3. Otherwise, if the deadline is reached, stop with `timed_out = 1` (and
   `none = 1` if the final count is zero).
4. Otherwise check cancellation, sleep `PART_OBSERVE_STEP_MS`, add the
   requested interval to the nominal elapsed time, and sample again.
5. On the first `part_error != 0`, latch the error, stop, and preserve the
   existing `stop = 15`.

Rules:

- Compare **counts**, never media-list order, UUIDs, labels or other
  identifiers. Registry order may differ between samples.
- The final snapshot's first three non-whole media are reported as
  `PART1..3`; they are an enumeration-order sample, not a stable identity
  mapping.
- Sleep only in the report thread and never while holding the controller
  `gate`. `countPartitions()` does not take the gate; the observation must not
  add block I/O, retries, resets or writes.
- Check `reportCancel` before each sample and after each sleep. On cancel,
  return through the existing no-panic cancellation path.
- Publish the observation state in one coherent snapshot under a briefly held
  lock so the watchdog sees consistent fields; never hold that lock across a
  sleep. The existing terminal-ownership rule (`reportLock`) still decides
  whether the report thread or the watchdog emits the final panic.
- The deadline is a nominal accumulated-sleep budget, not a wall-clock
  guarantee. The existing 30,000 ms watchdog remains the hard stop. The
  worst-case report path is 20,000 ms media wait plus 4,000 ms observation,
  leaving about 6,000 ms before the watchdog; N8.2 must assert this bound and
  keep the terminal-race suppression.
- No controlled-request counter is changed. The LBA 1 report read remains the
  existing incidental read; the intentional two-/three-block CMD18/CMD12
  qualification is unchanged.

## 4. Report line and interpretation

When `gta4l-emmc-partobs=1` is present, emit one bounded line before the
`PART1..3` samples:

```
GTA4L-EMMC P5 PARTOBS: samples=N first=C first_ms=T first_nz=C first_nz_ms=T
final=C final_ms=T changes=N stable=B stable_ms=T deadline_ms=T timed_out=B
none=B error=B slots=S reported=3 complete=0
```

Field meanings:

- `samples`: number of registry walks taken.
- `first` / `first_ms`: count and nominal time of the first sample.
- `first_nz` / `first_nz_ms`: first nonzero count and its nominal time
  (`0`/`0` if none was observed).
- `final` / `final_ms`: last count and its nominal time.
- `changes`: number of count transitions between consecutive samples.
- `stable` / `stable_ms`: whether the final count held for the stability
  window, and the time since the last change at the final sample.
- `deadline_ms`: the configured observation deadline.
- `timed_out`: the deadline was reached without `stable = 1`.
- `none`: the final count was zero at the deadline.
- `error`: a registry-walk failure was latched (`stop = 15`).
- `slots`: the GPT header entry-array slot count (84 here), explicitly not an
  expected populated count.
- `reported`: number of `PARTn` sample lines (still 3).
- `complete=0`: an explicit non-claim. This instrument never reports
  completeness.

The legacy `GTA4L-EMMC P5 PART: count= expected= waited_ms= error= reported=`
line is unchanged when the token is absent. In opt-in mode it is replaced by
`PARTOBS`, so `expected=84` is never presented as an expectation.

Interpretation rules:

- `stable=1` means the count did not change over the window. Publication may
  resume after the window; a stable count is **not** proof of completion.
- `count=80` with `stable=1` is consistent with complete publication but is
  not proof. A completeness claim needs an independent validated
  GPT-to-`IOMedia` range/count comparison, which N8.1 does not implement.
- `count<80` with `stable=1` is a stable partial snapshot, not an error and
  not evidence that partitions are missing.
- `timed_out=1` with a nonzero count is a changing or growing publication at
  the deadline, not a failure.
- `none=1` means no non-whole media was observed by the deadline; it does not
  prove the GPT has no partitions.
- `error=1` / `stop=15` is a measurement failure (registry walk), not a card,
  GPT or transport failure.
- The observation cannot see publication before its window starts, and it
  cannot see growth after a stable stop.

## 5. Host-test plan (N8.2 acceptance requirements)

Scenarios:

1. Incremental publication: counts grow across samples; assert `first`,
   `final`, `changes`, `first_nz_ms` and `stable`.
2. Delayed first partition: several zero samples, then a nonzero count;
   assert `first_nz`/`first_nz_ms` and that the deadline is respected.
3. Stable partial count: count reaches 22 and holds; assert `stable=1`,
   `final=22` and `complete=0`. A later growth injection after the window is
   outside the observation and must not be reported as complete.
4. Late growth inside the window: a change at about 200 ms resets the
   stability window; the final count captures the growth.
5. No partitions: zero to the deadline; assert `none=1`, `timed_out=1`,
   `stable=0`, `stop=0`.
6. Changing count to the deadline: assert `timed_out=1`, `stable=0`, bounded
   sample count.
7. Iterator failure: a failing registry walk sets `error=1` and `stop=15`,
   preserving the first failure.
8. Cancellation mid-observation: the loop exits promptly, does not panic and
   does not double-complete.
9. Watchdog race: a watchdog terminal snapshot taken during observation is
   coherent, and the later report-thread panic is suppressed.
10. Legacy path: with the token absent, mode 1 and mode 3 emit the unchanged
    legacy `PART` line and no `PARTOBS` line.
11. Width bound: a widest-field `PARTOBS` form (maximum counts, six-digit
    milliseconds, 81 samples, `slots=1024`) plus the `PART1..3` lines stays
    below the real 2,048-byte report buffer with margin; an overflow control
    must fail.
12. No-gate-across-sleep: the host seam detects the controller gate being held
    during `IOSleep`; a mutation that holds it must fail.

Mutation controls (each must fail for the intended reason at runtime, not at
compile time):

- the old first-nonzero break without a stability window;
- treating `stable=1` as `complete=1`;
- substituting the 84 slot count for a populated count or an expectation;
- skipping the cancellation check;
- skipping the first-error latch;
- ignoring the deadline;
- using media-list order as the stability criterion;
- holding the gate across a sleep;
- a report field that exceeds its bound.

The existing core, probe, block and adapter suites must remain unchanged when
the token is absent. N8.2 reports actual scenario and mutation counts; it does
not re-claim historical suite counts as freshly executed.

## 6. Implementation surface and approval gates

N8.2 implementation would touch, at most:

- `package/Gta4lEmmcBlock.h` (observation constants and report fields);
- `package/Gta4lEmmcBlock.cpp` (token parsing, observation loop, coherent
  snapshot, cancellation and terminal-race handling);
- `package/Gta4lEmmcBlock.c` (the `PARTOBS` formatter; the legacy formatter
  unchanged);
- `scripts/sim-gta4l-emmc-block/adapter_support.cpp` and
  `adapter_harness.cpp` (publication, cancellation and watchdog hooks);
- `scripts/test-gta4l-emmc-block.py` (scenarios and mutations);
- the diagnostic payload builder/checker/packager and their tests for the new
  token;
- the documentation and porting record.

No new block mode, transport API, width/speed change, reset/retry, write,
mount, DMA/CQE/ICE path or advertised-request-limit change is part of N8. The
report struct grows, so any candidate build still needs a fresh storage-ABI
audit, provenance, payload contract and BOOT gate before linking.

Gates:

1. **N8.1 design (this document):** no code, build, QEMU, device access,
   flash, boot, commit or push.
2. **N8.2 host implementation and tests:** separate Build approval.
3. **N8.3 candidate production:** separate approval; fresh source/ABI
   provenance, packaging gates, token contract, report-width check and
   protected-partition/prefix/tail readback plan.
4. **N8.4 hardware observation:** separate BOOT-only approval, verified
   backups (including EFS/CPEFS as required by the device guide), one
   explicitly budgeted owner-controlled boot, and the standing stop budget.
   No reflash or retry to chase initialization reliability.

## 7. Limits and non-goals

- N8 is observation-only. It is not a completeness proof, a mount/filesystem
  rung, a transport or timing change, a reliability fix, or a write path.
- The 4,000 ms deadline, 50 ms interval and 250 ms window are design
  proposals; N8.2 must fix them after host measurement and re-assert the
  watchdog bound.
- The window starts after the whole media and cannot observe earlier
  publication; a stable stop cannot observe later growth.
- `PART1..3` are enumeration-order samples, not identities; no UUIDs, labels,
  serials or other private identifiers are added.
- Initialization reliability remains unresolved and separate. N8 does not
  authorize another boot, and a clean N8 observation would not qualify
  reliability.
- The N5 and N6 boot budgets remain consumed. Any device action needs a fresh
  explicit owner decision.

## 8. N8.2 host implementation record (2026-11-07)

N8.2 was approved separately and is complete on the host. It implements the
opt-in observation without changing the legacy path when the token is absent:

- `package/Gta4lEmmcBlock.h` adds `GTA4L_EMMC_BLOCK_PARTOBS_ARG`, the
  OFF/ON/REFUSED selector values, the 4,000 ms / 50 ms / 250 ms constants and
  the bounded `part_obs_*` report fields.
- `package/Gta4lEmmcBlock.c` adds `gta4l_emmc_block_partobs_select()` and the
  `PARTOBS` formatter. `part_obs_complete` is always 0.
- `package/Gta4lEmmcBlock.cpp` parses the token only after exactly one
  block-mode 1/3 token is accepted, samples `countPartitions()` at the nominal
  deadline in the report thread, publishes each sample under the report lock,
  latches the first registry-walk failure as `stop=15`, keeps the cancellation
  and terminal-ownership paths, and reuses `part_expected` only for the
  separate `slots` field.
- The diagnostic payload builder exposes `emmc-block-1-partobs` and
  `emmc-block-3-partobs`, which add two `--kernel-arg` tokens. The payload
  checker refuses an orphan, duplicate or non-1 partobs token and requires the
  named block personality for an exact expectation; the packager accepts both
  names.
- The adapter harness adds publication, cancellation and watchdog hooks and
  nine scenarios: `partobs-incremental`, `partobs-delayed`,
  `partobs-stable-partial`, `partobs-late-growth`, `partobs-none`,
  `partobs-changing`, `partobs-error`, `partobs-cancel` and
  `partobs-watchdog`.

Effective host results for this working tree:

- core suite: 48 scenarios and 38 mutation controls (unchanged);
- adapter suite: 64 scenarios (65 PASS lines including the suite banner), nine
  of them partobs;
- adapter mutation controls: 12, of which nine are the new partobs controls;
- policy mutation controls: three new partobs controls, all rejected;
- diagnostic payload suite: 22 PASS lines including both partobs personality
  mappings and the new contract cases; the packager self-test and the payload
  checker self-test also pass.

The budgets are fixed at 4,000 ms observation, 50 ms sample step and 250 ms
stability window. The 30,000 ms watchdog remains above the 20,000 ms media
wait plus the observation (24,000 ms worst case, about 6,000 ms margin).

The `partobs-changing` scenario alternates the published media count between
one and two for 81 samples. The final count is one because the profile's last
entry is one; `changes=80`, `stable=0` and `timed_out=1` are the expected
changing-publication result. The `partobs-stable-partial` scenario holds 22
for the stability window and stops with `stable=1`, `final=22`, `slots=84`
and `complete=0`, so a stable partial count is not presented as complete.

No candidate build, QEMU run, device access, flash or boot was performed.
N8.3 candidate production and N8.4 hardware observation remain separately
gated.

## 9. Disposition

N8.2 is complete on the host. N8.3 candidate production was then approved
separately and is complete on the host (section 10). The next decision is the
owner's: approve an N8.4 BOOT-only hardware observation, request a different
measurement, or defer the publication rung. No device action, flash or boot
follows from this document.

## 10. N8.3 candidate production record (2026-11-07)

N8.3 built the approved candidate through the archive/prelink/payload/BOOT
flow with no device access, flash, boot, QEMU device run, commit or push. The
immutable chain is prelinked kernel `4dcbf24d…` (51,224,576 bytes), linked
archive `f9f83526…` (512,896 bytes, 190 method slots, 181 kernel bindings,
26 concrete overrides, 0 unresolved original imports, `runtime_qualified=false`),
candidate storage-ABI audit `out/gta4l/storage-abi-4dcbf24d` (interface audit
`a30cfcde…`, provenance `a806c6cf…`, snapshot `222f6322…`, run `85307464…`),
packaged kernel `023c8ff8…`, payload `5f6759f2…` (83,886,080 bytes, 11/11
contract, `emmc-block-3-partobs` mapping to `gta4l-emmc-block=3
gta4l-emmc-partobs=1`) and BOOT image `3d7e71af…` (85,688,320 bytes, 18/18
gate, only the ramdisk differs from the `ce4d76e8…` v5 baseline). The
kernel-build archive step produced the `3e76771d…` container variant; the
requalified `f9f83526…` archive has byte-identical members. The block suite
passes 48 core scenarios/38 mutation controls and 64 adapter scenarios with
12 adapter mutation controls plus three policy mutation controls; the
storage-ABI positive and negative checks pass. A token scan on the extracted
payload kernel preserves the count-only non-claim: one `PARTOBS` string, three
`REPORT` strings, one legacy `PART:` string, four `gta4l-emmc-partobs` strings
and no literal `complete=`. The N8.3 record is `n8-closeout.txt` and the
read-only proposal is `n8-hardware-proposal.txt` in
`out/gta4l/ram-root-emmc-n8-candidate-evidence/`. N8.4 remains a separate
BOOT-only owner decision for one boot with verified EFS/CPEFS backups;
initialization reliability remains `[U]`.

## 11. N8.4 first boot and hold-fixture correction (2026-11-07)

N8.4 was approved separately and the N8.3 candidate was flashed BOOT-only to
p69. The pre-flash p69 `b19c59b8…` was backed up and verified, the full p69
readback `7c22e499…` matched the candidate prefix `3d7e71af…` plus the
unchanged tail `a28b4ea2…`, and RECOVERY/EFS/sec_efs stayed unchanged. The
host did not reboot.

The one owner-controlled first cold boot returned the userspace
`RAMROOT-FST PASS` panic and no eMMC report. The fixture deliberately panics
after its checks and a 50 ms poll, while the eMMC reporter independently waits
for media, performs the controlled reads and then observes the partition count
for up to 4,000 ms. The fixture therefore won the terminal before the eMMC
report existed. The observation is inconclusive: no `PARTOBS` line, controlled
read or count exists, and the transcript cannot distinguish a reporter that
had not started, one still waiting, and one that lost the terminal race. The
boot budget is consumed; no retry or reflash was performed.

The host-only correction adds an additive `fs-timer-hold` fixture variant that
runs the same checks and then blocks in a one-second poll loop instead of
panicking, leaving termination to the eMMC report thread or its watchdog.
Failure paths still panic immediately, and the original `fs-timer` fixture is
byte-identical (`user.o` `6984333d…`, signed `b316e44f…`, root `77d5eba0…`,
`fixture.json` `37cbdd0b…`).
The hold fixture builds and signs as `e6546715…` with root `327ac826…` and
nine verified pages. The gta4l payload builder now refuses a `*-partobs`
payload unless `--data-ramdisk-manifest` names the hold fixture, and host
tests cover the original hash, the hold loop, the retained failure panic, the
early-panic rejection and the manifest mismatch. The staged hold artifacts are
in `out/gta4l/ram-root-fixture-hold-evidence/`.

## 12. N8.5 hold-fixture candidate and flash (2026-11-07)

N8.5 rebuilt the payload and BOOT image with the `fs-timer-hold` fixture while
reusing the unchanged N8.3 kernel `4dcbf24d…` and archive `f9f83526…`. The hold
root is `327ac826…`, the hold trust cache `30a72f78…` and `fixture.json`
`00363681…`; the original `fs-timer` stays byte-identical. The new payload is
`28eafd24…` (11/11 contract) and the BOOT image is `b4f4f1ea…` (18/18 gate,
only the ramdisk differs from the `ce4d76e8…` baseline). The image was flashed
BOOT-only to p69 after backing up the N8.3 p69 `7c22e499…` as
`boot-before-n8.5.img.xz` (`e5b43228…`); the full p69 readback `9d51fe80…`
matches the candidate prefix plus the unchanged tail `a28b4ea2…`, and
RECOVERY/EFS/sec_efs are unchanged. The device disconnected once after the
backup and before any write; adb was restarted, p69 was re-read unchanged, and
the candidate was written once. The host did not reboot; one owner-controlled
boot remains, and no completeness or reliability claim follows. The N8.5
record is `docs/emmc-n8.5-hold-fixture-candidate-2026-11-07.md`. The N8.4 closeout is
`docs/emmc-n8.4-partobs-observation-2026-11-07.md`; a new candidate build and
a new BOOT-only flash/one-boot observation remain separately gated.
