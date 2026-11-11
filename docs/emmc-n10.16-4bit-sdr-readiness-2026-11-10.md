# gta4l N10.16 4-bit SDR storage-publication readiness audit (host-only, 2026-11-10)

Status: host-only readiness audit and design proposal for the first M3 rung:
staged 4-bit SDR storage publication at the existing 400 kHz. No
implementation, candidate build, device access, flash or boot was performed
while producing this record. The audit maps the H15-H17 verified
width-switch/recovery helpers onto the production block driver and proposes
an opt-in N10.16 width mode. Verdict: **GO** for a host-only implementation
and test batch under the contract in §4; **NO-GO** for candidate production
or hardware until the gates in §7 close. F5 remains measured PASS at 1-bit
SDR/400 kHz for one reported boot, F3 remains measured PASS at 4-bit
SDR/400 kHz for one reported boot, initialization reliability remains `[U]`,
and no 4-bit publication result exists yet.

## 1. Scope and outcome

M3's first rung is a readiness audit, not a publication result. The question
is whether the production block driver can switch the card and host to 4-bit
SDR at 400 kHz after identification, keep the production
64-block/32,768-byte/one-segment advertisement, read a bounded public-image
window at 4-bit, verify the content against the same-boot 1-bit reference,
and return the card to the legacy 1-bit state without changing clock,
CMD18 chunk shape, polling/interrupt policy, DMA policy or any write path.

The audit finds that the verified switch and recovery helpers already exist
in the same eMMC core the block driver uses, that the block driver's platform
ops implement every register operation those helpers need, that the safe
insertion point is after `initializeHardware()` succeeds and before the
baseline read/publication, and that the failure, rollback and DAT_INHIBIT
behavior is already proven at the probe level. The remaining work is a
bounded mode contract, host tests, a fresh ABI/packaging chain and explicit
owner approval for the CMD6 configuration writes. CMD19/CMD14 is deliberately
excluded from the normal storage path.

## 2. Evidence basis

### 2.1 Probe width evidence

- H15 (cold boot, `9facb6b`): the verified 4-bit switch passed on the first
  CMD6 attempt (`c6=00000900`, host `00->02`, `vf=1`, read-only EXT_CSD
  `es=000009cb`), so 4-bit data transfer at 400 kHz is observed. The single
  CMD19 then timed out and the old revert's pre-switch EXT_CSD read was
  blocked by a latched `DAT_INHIBIT` (`V rc=-17`, `c6=0`), so the host stayed
  at 4-bit and no content pass or recovery read ran. This is the origin of
  the recovery-helper requirement.
- H16 (warm boot, `20c9f9a`): verified 4-bit switch on the first CMD6
  attempt, the full 4-bit content pass matched the 1-bit baseline
  (`same=1 div=0`, `b0=62084b66 b1=84d0543e data=f418cd33 last=a89b26d5`),
  the baseline-based revert was verified (`02->00`) and the block-0 recovery
  read returned `qb0=62084b66`. No CMD19/CMD14 was attempted.
- H17 (fourth boot, `a198db1`): first three boots stopped pre-identification
  in the CMD3-completion-timeout class; the fourth (warm) returned
  `rc=0 stop=0`. The switch and content pass passed, CMD19 produced the
  expected no-CRC-status timeout (`wrc=-11`, CMD19-bound failure record, CC
  latched), one bounded data reset cleared the circuit (`rst=0`), CMD14
  returned the expected word (`trc=0`, `tw=te=000000a5`), and the verified
  revert and block-0 recovery read followed. **F3 is measured PASS at 4-bit
  SDR, 400 kHz `[C]`**; 8-bit is untried and cold-boot identification
  reliability remains open.
- BUS_WIDTH[183] read back 1, 0 and 1 on three verified live 4-bit paths.
  It is W/E_P and not readable, so it is never used as switch proof. The
  verified switch compares the read-only EXT_CSD properties segment
  (bytes 192..511) against a pre-switch baseline.
- The probe's width sequence is: baseline EXT_CSD read, verified 4-bit
  switch, content pass, optional CMD19/CMD14, baseline-based revert, block-0
  recovery read. H16 omits CMD19/CMD14; H17 tolerates only the CMD19 data
  timeout and lets CMD14 decide.

### 2.2 Production block-driver facts

- `Gta4lEmmcBlock::initializeHardware()` (`package/Gta4lEmmcBlock.cpp:778`)
  zeroes the platform and host, installs
  `gta4l_emmc_block_platform_ops`, maps the platform and calls
  `gta4l_emmc_init(&host)` (`:791`). On success `host.initialized` is set by
  the core (`package/Gta4lEmmc.c:3470`), which is the precondition for every
  width helper. `initializeHardware()` then installs the block callbacks and
  calls `gta4l_emmc_block_init()` (`package/Gta4lEmmcBlock.cpp:817`), so the
  block core is ready when `start()` resumes.
- The block platform ops implement `read8/16/32`, `write8/16/32`, `barrier`,
  `delay_us`, `log` and `counter` (`package/Gta4lEmmcBlock.cpp:634-645`).
  The width helpers use only those operations plus the existing command and
  data paths, so no new platform callback is needed.
- The core is polled and has no interrupt path; the block driver serializes
  transport with the `gate` `IOLock` in `doAsyncReadWrite()`
  (`package/Gta4lEmmcBlock.cpp:1509`). The report thread and the watchdog
  snapshot also use `gate` (`:1819-1847`). The width change must finish
  before the report/watchdog threads start, or run under `gate` in the
  report thread; it must never overlap a concurrent read at a different
  width.
- `start()` allocates `reportBuffer` (2,048 bytes) and the `gate`/`reportLock`
  locks (`package/Gta4lEmmcBlock.cpp:1269-1289`), calls `initializeHardware()`
  (`:1292`), performs a 1-bit baseline CMD17 read at LBA 1 (`:1297-1305`),
  sets the production advertisement (`:1316-1343`), registers the service
  (`:1344`) and then starts the report and watchdog threads (`:1347-1375`).
  A switch placed after `initializeHardware()` returns and before the
  baseline read is in the pre-watchdog start path; a switch placed in the
  report thread is watchdog-covered.
- The portable block core (`package/Gta4lEmmcBlockCore.c:88-282`) accepts
  only reads, caps a request at 64 blocks, uses two-block CMD18 chunks with a
  mandatory CMD12 stop, and copies bytes only after transport, exact counts
  and stop all qualify. A failed callback clears `online`, preserves the
  first failure and returns `IO_ERROR`/`SHORT_COPY`.
- The report contract is 2,048 bytes (`package/Gta4lEmmcBlock.h:258`), with
  per-line caps and `static_assert`s (`package/Gta4lEmmcBlock.cpp:55-162`).
  The header `wid=` prints the EXT_CSD code (`package/Gta4lEmmcBlock.c:1654`)
  and `snapshotReport` copies `host.bus_width` outside `gate`
  (`package/Gta4lEmmcBlock.cpp:1810`), so the width must not change while a
  coherent snapshot is being taken.
- The mode selectors are chained, exclusive and fail closed
  (`package/Gta4lEmmcBlock.c:67-217`). The consume selector already requires
  block mode 3 and refuses every other mode token (`:199-217`); a width
  selector can follow the same shape.

## 3. Readiness audit

### 3.1 Helper callability and contract

The verified helpers are in the same core the block driver links:

| Helper | Location | Precondition | What it proves |
| --- | --- | --- | --- |
| `gta4l_emmc_read_ext_csd()` | `package/Gta4lEmmc.c:1908` | initialized host | 512-byte EXT_CSD read; used for the baseline and the post-switch comparison |
| `gta4l_emmc_set_bus_width_verified()` | `package/Gta4lEmmc.c:2320` | initialized host | CMD6 BUS_WIDTH write, CMD13 TRAN wait, host width apply/readback, read-only EXT_CSD properties comparison, card+host rollback on failure |
| `gta4l_emmc_set_bus_width_recover()` | `package/Gta4lEmmc.c:2341` | initialized host + healthy baseline | clears a latched `DAT_INHIBIT` with one bounded data reset, CMD6 restore, properties verification, two attempts |
| `gta4l_emmc_bus_test_verified()` | `package/Gta4lEmmc.c:2655` | initialized host + card/host width agreement | CMD19/CMD14 pattern round trip; **not used by this rung** |

The verified switch reads a fresh EXT_CSD baseline before CMD6, rejects an
invalid width, skips CMD6 when the requested code equals the cached code,
issues `CMD6` with `SWITCH_ACCESS_WRITE | (183 << 16) | (code << 8) |
SET_NORMAL`, waits for TRAN/READY_FOR_DATA with up to ten CMD13 attempts,
applies only the 4/8-bit mask in `HOST_CONTROL`, reads the host byte back,
re-reads EXT_CSD at the new width and compares bytes 192..511 with the
baseline. Any failure rolls both sides back; a restore that cannot be proven
returns `GTA4L_EMMC_ERR_WIDTH_RESTORE` (`package/Gta4lEmmc.h:368`), which
must be treated as terminal unknown framing. The switch changes no clock,
PAD, DLL, HS_TIMING, power or DDR state.

The recovery helper is the DAT_INHIBIT path: it takes a caller-supplied
healthy-path EXT_CSD baseline, clears a latched inhibit with one bounded
local data reset (100 ms reset timeout), issues CMD6 while the host still
drives the previous width, applies the host width, verifies the read-only
properties and retries once. It does not read EXT_CSD before CMD6, so it can
recover when a normal pre-switch read is blocked. It returns
`GTA4L_EMMC_ERR_WIDTH_RESTORE` if the clear or the restore cannot be proven.

### 3.2 Insertion point and lifecycle

The production goal is to publish the media at 4-bit, so the switch belongs
after `initializeHardware()` returns and before the baseline read and
`registerService()`. `initializeHardware()` has already run
`gta4l_emmc_init()` and `gta4l_emmc_block_init()` (`package/Gta4lEmmcBlock.cpp:791`,
`:817`), so `host.initialized == 1`, the block core is ready for the 1-bit
reference read, and no report/watchdog thread exists yet. The exact call site
is between the successful `initializeHardware()` at
`package/Gta4lEmmcBlock.cpp:1292` and the baseline read at `:1297`. That
placement:

- has `host.initialized == 1`, which every helper requires;
- has the platform mapped, the host struct live and the block core
  initialized;
- precedes the report/watchdog threads, so no concurrent snapshot can copy a
  half-changed `host.bus_width`;
- precedes publication, so the advertised media operates at the new width;
- keeps the 1-bit reference read and the switch in the pre-watchdog start
  path, bounded by the core's command/data timeouts rather than the
  30,000 ms report watchdog.

The pre-watchdog placement is the same class as the H15-H17 probe: a hang
beyond the core timeouts would stall the boot instead of producing a
watchdog report. The audit records that explicitly; it is not a new risk
introduced by the width mode, but it is a limitation the owner must accept
for the first rung. The report-thread case read is watchdog-covered.

The mode must hold `gate` for the 4-bit case read and for the revert, so a
concurrent storage request cannot issue a CMD17/CMD18 at the wrong width.
The mode must release `gate` before `panicWithReport()`, because
`fillDriverReport()`/`snapshotReport()` takes the same lock.

### 3.3 Validation

The only accepted proof is the read-only EXT_CSD properties comparison plus
the host `HOST_CONTROL` readback, exactly as H16/H17 did. BUS_WIDTH[183] is
recorded only as a diagnostic (`switched` in `struct gta4l_emmc_bus_result`)
and must never gate completion. The host width bits must read back as the
requested 4-bit mask. A missing bit, a properties mismatch or a short
EXT_CSD read is a switch failure, not a warning.

The content proof is a same-boot comparison: a 1-bit two-block read of the
public window before the switch, a 4-bit two-block read of the same window
after the switch, and a byte-for-byte comparison. The device prints both
SHA-256 prefixes; the host compares them with the packaged image's first
1,024 bytes after the run (option A). No expected digest is compiled in. A
host mismatch rejects acceptance even if the on-device comparison passed.

### 3.4 Failure, rollback and DAT_INHIBIT

- A switch failure returns the original error after a proven card+host
  rollback, or `ERR_WIDTH_RESTORE` if the rollback cannot be proven. The mode
  stops with a terminal failure and no further reads. On
  `ERR_WIDTH_RESTORE` it does not attempt another recovery: the helper has
  already made two restore attempts, the framing is unknown, and an
  unqualified third sequence is outside the contract. Failure is terminal
  unknown framing.
- A 4-bit read failure clears the block core's `online` flag, preserves the
  first failure and fails closed. The mode does not retry the read and does
  not substitute a smaller window.
- A revert failure after a successful 4-bit read is a terminal failure even
  though the width read passed; the report preserves both facts. The card
  may remain at 4-bit, the media is offline and the run stops.
- The success path reverts to 1-bit with the captured baseline before the
  terminal report. This keeps the card in the legacy state and proves the
  production path can unwind. A production 4-bit mode (a later rung) would
  need its own stop/shutdown revert or a proven CMD0/controller-reset width
  reset; neither is claimed here.
- No CMD19/CMD14 is issued. The H17 tolerated-timeout rule is not part of
  the normal storage path, so the data-write wedge and its failure-record
  carry-forwards are not inherited.

### 3.5 Timing

The width rung adds a bounded, finite sequence before the existing case
read. The terms below are the core's own timeouts, not measured timings:

| Step | Terms | Worst case |
| --- | --- | --- |
| 1-bit EXT_CSD baseline | CMD8 command 1,000 ms + data 1,000 ms | 2,000 ms |
| Pre-switch 1-bit window read | two-block CMD18: data poll 1,000 + CMD12 entry 1,000 + CMD12 completion 1,000 | 3,000 ms |
| Verified 4-bit switch | pre-switch EXT_CSD 2,000 + CMD6 1,000 + TRAN wait 10 x (100 + 1) + post-switch EXT_CSD 2,000 | 6,010 ms |
| Existing baseline LBA 1 read | CMD17 command 1,000 + data 1,000 | 2,000 ms |
| Bounded gate acquisition | try-lock loop, 10 ms step, 2,000 ms deadline | 2,000 ms |
| 4-bit window read | two-block CMD18: 1,000 + 1,000 + 1,000 | 3,000 ms |
| Verified 1-bit revert | two attempts x (reset 100 + CMD6 1,000 + TRAN 1,010 + EXT_CSD 2,000) | 8,220 ms |
| **Total** | | **26,230 ms** |

The pre-watchdog start path is the baseline, pre-read, switch and existing
baseline read: 2,000 + 3,000 + 6,010 + 2,000 = **13,010 ms**. The
watchdog-covered report path is the bounded gate, the 4-bit read and the
revert: 2,000 + 3,000 + 8,220 = **13,220 ms**. The success total is
26,230 ms, leaving 3,770 ms below the 30,000 ms
`GTA4L_EMMC_BLOCK_WATCHDOG_MS`.

A failed switch has a larger bound than a successful one because the
verified helper's rollback can make two CMD6/TRAN/EXT_CSD passes:
2,000 + 3,000 + (2,000 + 1,000 + 1,010 + 2,000 + 8,020) = **21,030 ms**.
The mode fails closed there, so it does not run the existing baseline read,
publication, the 4-bit case read, the success revert or an extra recovery
sequence. The success path's 26,230 ms is the overall bound.

This is a conditional margin, not a path-condition proof. The 3,770 ms must
also cover scheduling and pre-watchdog boot-path variance. The report gate
wait is now explicitly bounded at 2,000 ms; a timeout is a terminal gate
failure with media offline, no read, no revert claim and no watchdog wait.
The pre-watchdog path remains a design budget rather than a source-derived
sum. The design does not extend the watchdog, the CMD/data timeouts or the
chunk size. A watchdog fire is a no-go, not a pass.

### 3.6 Report and terminal ownership

The width mode follows the consume-mode precedent: suppress the legacy
`READ`/`QUAL`/`QUAL3` lines (which would otherwise overflow the 2,048-byte
buffer), emit one `WIDTH` summary and one `WIDTHREAD` detail line on success,
and one `WIDTH` plus one `WIDTHFAIL` first-failure line on failure. Every
other legacy line is unchanged, and every non-width mode stays byte-for-byte
identical. The summary carries the switch, revert, count and advertisement
fields; the detail carries the observed and pre-switch digests and the
on-device match. The formatter needs new `WIDTH_LINE_BYTES`,
`WIDTHREAD_LINE_BYTES`, `WIDTHFAIL_LINE_BYTES` and a
`WIDTH_REPORT_WORST_BYTES` `static_assert`, all bounded by the existing
2,048-byte cap. The exact maxima must be recomputed from the formatter, not
estimated.

Terminal ownership stays with `claimTerminal()`/`panicWithReport()`. The
width mode releases `gate` before reporting; the watchdog's try-lock
snapshot is allowed to be incoherent while the mode holds `gate`, and a
watchdog fire is a no-go. The success `complete=1` guard is a source
constant conjunction, not a host-side inference.

### 3.7 ABI and packaging

A new mode changes the kernel image, so the existing chain applies unchanged:
a fresh storage-ABI audit (190 method slots, 181 kernel bindings, 26
concrete overrides, zero unresolved original imports, vtable/metaclass and
export-subset checks, `runtime_qualified=false`), a fresh prelink/archive/
packaged-kernel/payload/BOOT production, the 18/18 boot-image gate, the
payload contract (exactly one block token 3, exactly one width token 4, no
other mode token) and the post-flash readback. The candidate-specific audit
must not reuse the N10.15 audit. The payload builder/checker need a new
`emmc-block-3-width4` mapping and refusal cases for orphan, duplicate,
wrong-value and conflicting tokens.

## 4. Proposed N10.16 mode contract

### 4.1 Token and selection

- New boot argument: `gta4l-emmc-width=4`, requiring exactly one
  `gta4l-emmc-block=3` token. The mode refuses a missing block token,
  block values other than 3, duplicate width tokens, width values other
  than 4, and every combination with `partobs`, `partcmp`, `partread`,
  `deblock`, `breakup`, `sweep` or `consume`. The selector follows
  `gta4l_emmc_block_consume_select()` (`package/Gta4lEmmcBlock.c:199-217`).
- Absent token: every legacy mode and the 1-bit path stay byte-for-byte
  unchanged. `width=1` is refused rather than treated as a no-op; the
  legacy path is the absence of the token.

### 4.2 Run sequence

1. After `initializeHardware()` returns (host and block core initialized)
   and before the start-path baseline read, read the 1-bit EXT_CSD baseline
   into a 512-byte stack buffer.
2. Allocate/initialize the 1,024-byte reference buffer before the switch.
   Read the pinned BOOT-partition window at device LBA
   `GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES / 512` = 2,255,360 (two
   blocks) at 1-bit through the block core, require `status=0`,
   `actual=1024`, exact two-block counts and a clean CMD12, and compute the
   reference digest. Any failure stops before publication.
3. Call `gta4l_emmc_set_bus_width_verified(&host, 4, &bus)`. Require
   `verified=1`, the host 4-bit readback and `switch_result=OK`. On failure
   fail closed with the recorded fields; on `ERR_WIDTH_RESTORE`, stop without
   further reads and without an extra recovery sequence.
4. Continue with the existing baseline LBA 1 read and the production
   advertisement/publication. The advertisement stays 64 blocks /
   32,768 bytes / one segment; the clock stays 400 kHz and the chunk stays
   two blocks.
5. In the report thread, take `gate` and read the same pinned BOOT window at
   4-bit through the block core. Require the same exact-count/stop gates,
   compare all 1,024 bytes with the reference buffer, and compute the
   observed digest.
6. Revert to 1-bit with `gta4l_emmc_set_bus_width_recover(&host, 1,
   baseline, &revert)`. Require `verified=1`, the host 1-bit readback and a
   successful properties comparison.
7. Release `gate`, fill the report under `reportLock`, and call
   `panicWithReport()`.

The mode does not open the media, does not use the consumer client, does not
run discovery, and does not issue CMD19/CMD14.

### 4.3 Read scope and content binding

- Exactly one public window: the pinned BOOT-partition base (device LBA
  2,255,360), two 512-byte blocks, 1,024 bytes, BOOT image offset 0. The
  same window was qualified at 1-bit by N10.15; this rung re-reads it at
  1-bit and 4-bit in the same boot.
- On-device check: the 1-bit reference and the 4-bit read must be equal
  byte-for-byte (`match=1`, `mismatch=0`).
- Host check (option A): both printed SHA-256 prefixes must equal the first
  1,024 bytes of the final packaged image. The reference is rebound to the
  final candidate image after production; no digest is compiled into the
  kernel. A host mismatch rejects acceptance even if `complete=1`.
- No other range, partition, write, mount or root switch is in scope.

### 4.4 Counters and completion guard

The report carries the switch/revert result fields, two reads, 2,048 bytes,
four blocks, two driver-level subrequests, a largest subrequest of two
blocks, the production limit/segment values, the capacity, both digests and
the on-device match. `complete=1` requires, at minimum:

- `cases=1`, `passed=1`, `failed=0`, `reason=NONE`, `complete` computed in
  the source;
- `switch=4`, `verified=1`, `host_before` 1-bit, `host_after` with the
  4-bit bit, `cmd6=1`;
- reference read: `status=0`, `actual=1024`, `blocks=2`, `subrequests=1`,
  clean stop;
- 4-bit read: `status=0`, `actual=1024`, `blocks=2`, `subrequests=1`,
  `max=2`, clean stop;
- `match=1`, `mismatch=0`, `fails=0`, `cancel=0`, `budget=0`;
- revert: `revert=1`, `rverified=1`, host back to 1-bit, `rcmd6` in 1..2;
- `limit=64`, `seg=1`, `cap=61071360`.

A missing/duplicate/extra case, a wrong advertisement, a foreign request, a
short transfer, a stop failure, a properties mismatch, a byte-183-only
"proof" or a host-side digest mismatch cannot produce `complete=1`.

### 4.5 Report lines

Success adds exactly two lines:

```
GTA4L-EMMC P5 WIDTH: cases=1 passed=1 failed=0 reason=0 complete=1
switch=4 verified=1 hb=0 ha=2 rb=0 cmd6=1 revert=1 rverified=1 rhb=2
rha=0 rcmd6=1 reads=2 bytes=2048 blocks=4 subrequests=2 max=2 match=1
mismatches=0 fails=0 cancel=0 budget=0 limit=64 seg=1 cap=61071360
GTA4L-EMMC P5 WIDTHREAD: index=0 lba=2255360 start=0 length=1024 span=1024
actual=1024 status=0 subrequests=1 blocks=2 max=2 digest=<32 hex>
predigest=<32 hex> match=1
```

`rb` is the diagnostic EXT_CSD byte-183 readback. Byte 183 is W/E_P and not
readable, so hardware reports `0`; the host adapter model's `1` is a simulated
value. `rcmd6` is `1` when the first revert attempt succeeds (the source
accepts one or two attempts). The `complete` guard does not test `rb`, but it
does require `rcmd6` in `[1,2]`. The 2026-11-11 hardware run measured these
values; see `docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md`.

Failure adds `WIDTH` with `complete=0` and one `WIDTHFAIL` line carrying the
first-failure stage (switch, pre-read, 4-bit read or revert), the raw
error/status, the actual byte count and the observed digest when one exists.
The `WIDTH`/`WIDTHREAD`/`WIDTHFAIL` caps and the worst-report
`static_assert` are part of the implementation batch. The formatter passes
`16u` to `gta4l_emmc_block_put_digest()` as a **byte count**, so each digest
field is 16 bytes/32 hex characters, not 16 characters.

## 5. Host test and mutation plan

The implementation batch must add, and pass, at least:

- Selector: token absent (legacy unchanged), orphan, duplicate, values
  0/1/2/3/5/8, block=1, block=3 accepted, conflicts with every existing
  mode token.
- Switch: clean success; CMD6 failure with proven rollback; rollback
  failure (`ERR_WIDTH_RESTORE`); host readback mismatch; EXT_CSD properties
  mismatch; baseline read failure; byte-183 mismatch ignored when the
  properties match.
- Recovery: clean DAT_INHIBIT clear; reset timeout; CMD6 failure; property
  mismatch; second-attempt success; two-attempt exhaustion.
- Content: reference read failure; 4-bit read failure; short transfer;
  stop failure; byte mismatch; digest mismatch (host-side); match; revert
  failure after a successful 4-bit read.
- Report: success/failure line caps, worst-report size, `complete=1` guard
  fields, legacy report byte identity when the token is absent, terminal
  ownership and watchdog interaction.
- Mutation controls: remove the width guard, skip the properties check, use
  byte 183, skip the revert, credit a failed read, advertise the wrong
  limit/segments, and accept a host digest mismatch.

The existing simulator already carries a `bus_width` field
(`scripts/sim-gta4l-emmc-block/adapter_support.cpp:475`) and the probe
harness already models the H15-H17 switch/recovery cases; the new block
scenarios should reuse those seams rather than invent a second model. The
evidence-count test must be refreshed with the new core/adapter scenario and
mutation counts, and the historical count rows must stay valid.

## 6. Candidate, packaging and hardware gates

Candidate production requires its own approval and a fresh chain: kernel
build, storage-ABI audit, archive, prelink, packaged kernel, payload, BOOT
image, 18/18 gate, payload contract and post-flash readback. The payload
builder/checker need the `emmc-block-3-width4` mapping and the refusal
cases. The BOOT-only flash and any boot remain separately approved.

A hardware run must explicitly name the CMD6 BUS_WIDTH configuration writes,
the exact read scope, the finite boot budget and the TWRP return. It must
use a transcription/host-comparison protocol, stop on the first non-pass,
and not retry, reflash, rollback, mount, repair or access another partition.
The cold-boot identification reliability is `[U]`; a pre-identification stop
is an observation, not a width failure, and the boot budget must account
for it.

## 7. Go/no-go verdict and open items

Superseding status (2026-11-11): conditions 1-6 below were satisfied for the
gate-fix candidate, the owner approved the one-boot scope, and the executed
boot passed the driver's `complete=1` guard. BOOT was restored and verified.
The verdict text below is the historical 2026-11-10 assessment; the measured
result and its bounded scope are in
`docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md` and §10.4.
Initialization reliability and failure rates remain `[U]`.

**GO for the host-only implementation and test batch (batch B)** under the
contract in §4. The helpers, insertion point, validation, rollback, recovery
and report shape are all established from the current source and the
H15-H17 evidence; no new transport mechanism is required.

**NO-GO for candidate production or hardware** until:

1. batch B passes the selector, switch, recovery, content, report and
   mutation tests;
2. the owner explicitly approves the CMD6 configuration writes and the exact
   read scope;
3. the owner accepts the conditional timing margin in §3.5 or a stronger
   bound is established;
4. the content reference is rebound to the final packaged image;
5. the candidate-specific ABI/packaging gates pass; and
6. the hardware run has a finite boot budget, a transcription protocol and
   a TWRP return.

Open items:

- Cold-boot identification reliability remains `[U]` (H17 needed four boots;
  the first three stopped pre-identification).
- The pre-watchdog start path (baseline, pre-read, switch, existing baseline
  read) is bounded by core timeouts but not by the report watchdog.
- Gate acquisition is now explicitly bounded at 2,000 ms with a terminal
  reason-8 timeout; a held gate leaves the media offline with no read or
  revert claim. The 13,010 ms pre-watchdog path remains a conditional design
  budget.
- The success-path revert leaves the card at 1-bit; a production 4-bit mode
  needs its own stop/shutdown revert or a proven reset path.
- The 4-bit content evidence is one warm boot (H16/H17), not a reliability
  result. Initialization reliability, repeatability and failure rates remain
  `[U]`.

Deferred: CMD19/CMD14 in the normal storage path, 8-bit, HS200/HS400, DDR,
higher clocks, DMA/CQE/ICE, a CMD18 IOMedia publication rung, mount/root
integration, storage writes and any user-data path.

## 8. References

- `docs/emmc-driver.md` §5.3.31-§5.3.34 (H15/H16/H17), §5.3.35-§5.3.37
  (P5-1/P5-2) and §7 (limits and residual risks).
- `docs/evidence-baseline.md` Stage-H15/H16/H17 rows and the P5-2 row.
- `docs/gta4l-storage-publication-design.md` (F5/F6, width/timing
  exclusions, mount boundary, fail-closed and no-retry rules).
- `docs/emmc-n10.15-read-only-consumer-design-2026-11-10.md` §7.4 (the
  conditional timing method this audit reuses) and §13 (gating).
- `package/Gta4lEmmc.c` (width helpers, timeouts, recovery),
  `package/Gta4lEmmc.h` (result/error/step definitions),
  `package/Gta4lEmmcBlock.cpp`/`.h`/`.c` (lifecycle, selector, report),
  `package/Gta4lEmmcBlockCore.c`/`.h` (portable read path).
- `scripts/test-gta4l-emmc.py`, `scripts/test-gta4l-emmc-probe.py`,
  `scripts/test-gta4l-emmc-block.py`,
  `scripts/sim-gta4l-emmc-block/` (host seams and mutation controls).

## 9. Batch B host evidence (2026-11-10)

Host-only implementation and test batch, no device access, candidate,
flash or boot.

- `scripts/test-gta4l-emmc-block.py`: 48 core scenarios/38 mutation
  controls and 285 adapter scenarios with 104 adapter/support and 10
  policy mutation controls. The width scenarios cover the absent token,
  orphan/duplicate/value/block/conflict refusals, the full 4-bit match,
  baseline/preread/switch/read/short/stop/mismatch/revert failures,
  `ERR_WIDTH_RESTORE` terminal framing, budget, cancellation during the
  bounded gate wait and after the 4-bit read, the terminal watchdog
  interleaving, and the host-side digest rejection. The width mutation
  controls remove the switch guard, claim completion, skip the byte
  compare, skip the revert, credit a failed read, advertise the wrong
  limit/segments, truncate the reference digest and shift the pinned LBA.
- `scripts/test-gta4l-emmc.py`: 189 scenarios/117 mutation controls,
  including the verified switch, rollback, byte-183 independence, the
  recovery retry/second-attempt success, CMD6 failure and exhaustion paths,
  and the properties-check and byte-183-proof mutation controls.
- `scripts/test-gta4l-emmc-probe.py`: 196 scenarios/100 mutation
  controls, 13 width-bound materializations and one oversized-report
  control.
- `scripts/test-gta4l-emmc-evidence-counts.py` re-derives the core,
  probe and adapter counts from the sources and checks the live
  summaries.
- `scripts/test-gta4l-diagnostic-payload.py` and
  `scripts/test-gta4l-diagnostic-package.py` pass with the new
  `emmc-block-3-width4` personality. It maps to exactly
  `gta4l-emmc-block=3 gta4l-emmc-width=4`, requires the
  `fs-timer-hold` fixture, rejects the early-panic fixture, refuses an
  orphan, duplicate, malformed or non-4 width token, a block-1 or
  duplicate block token, a probe token, and every combination with
  partobs, partcmp, partread, deblock, breakup, sweep or consume, and
  cannot be smuggled into another personality's mapping. The builder and
  packager self-tests pass. No payload or candidate image was built by this
  host batch; the separately approved candidates are recorded in §10.2,
  `docs/emmc-n10.16-width4-candidate-2026-11-10.md` and
  `docs/emmc-n10.16-width4-gatefix-candidate-2026-11-11.md`.

No `complete=1` device claim, timing-margin proof or hardware result is
added by this batch.

## 10. Candidate acceptance checklist (2026-11-10)

This checklist separates host preparation from candidate, owner and
hardware gates. It records status; it does not grant approval. Only the
host-integration rows below are complete.

### 10.1 Host integration

| Check | Status |
| --- | --- |
| Opt-in selector and legacy path | DONE, host tests pass (48/38 core, 285 adapter/104 adapter-support + 10 policy, core 189/117, probe 196/100) |
| `emmc-block-3-width4` mapping | DONE: `gta4l-emmc-block=3 gta4l-emmc-width=4` |
| Fixture requirement | DONE: `fs-timer-hold` required; `fs-timer` rejected |
| Token/refusal contract | DONE: orphan, duplicate, malformed, non-4, block-1, duplicate block, probe and all modifier conflicts rejected |
| Personality isolation | DONE: width token cannot be smuggled into another accepted personality |
| Payload builder/packager tests | DONE: host tests pass; candidate image recorded in §10.2 |
| Report bounds | DONE host-side: 513/329/127 bytes, 1,772-byte report inside 2,048 |
| Evidence-count reconciliation | DONE |

### 10.2 Candidate qualification — complete for the gate-fix candidate

| Gate | Status |
| --- | --- |
| Owner approval for a host-only candidate build batch | DONE: owner directed C2/C3; no hardware approval |
| Kernel build and source/artifact hashes | DONE: `2be2eb06…`, 51,470,336 bytes |
| Candidate-specific storage-ABI and import audit | DONE: 190 slots/181 bindings/26 overrides/0 unresolved |
| Archive, prelink, packaged kernel and payload contract | DONE: requalified members match; payload `10ff7ee8…`, 55/55 |
| BOOT-image 18/18 gate and provenance | DONE: `58352cd3…`, 18/18, only ramdisk differs |
| Digest rebinding to the final packaged image first 1,024 bytes | DONE: full `a6da82e3…`, 16-byte prefix `a6da82e356863d1ab7e31c21a8fd85fc` |
| Host content mismatch rejection against the rebound digest | DONE: `scripts/check-gta4l-width4-evidence.py` self-test and fixture |
| Candidate report/line-bound confirmation | DONE host-side: 513/329/127 bytes, 1,772-byte report |
| Bounded report gate | DONE: 2,000 ms try-lock; timeout is reason 8, media offline, no read/revert claim; held-gate, mid-wait release and deadline-boundary scenarios pass |

Evidence: `docs/emmc-n10.16-width4-gatefix-candidate-2026-11-11.md`.

### 10.3 Owner approvals — approved and exercised

| Approval | Status |
| --- | --- |
| CMD6 `BUS_WIDTH` configuration writes and host width changes | APPROVED by owner 2026-11-11; exercised once |
| Exact read scope: pinned two-block BOOT window at LBA 2,255,360 plus the existing baseline/publication reads | APPROVED by owner 2026-11-11; exercised once |
| Conditional timing margin (26,230 ms success / 21,030 ms failed switch against 30,000 ms, including the 2,000 ms bounded gate) or a stronger bound | APPROVED as the conditional basis for one boot; not a timing proof |
| Finite boot budget, transcription protocol and TWRP return | CONSUMED: one owner-controlled boot, TWRP return confirmed |

### 10.4 Hardware execution — executed 2026-11-11

The one approved controlled boot ran and the driver's terminal guard computed
`complete=1`: verified 4-bit switch, two matching 1,024-byte reads of the
pinned window, expected digest, verified return to 1-bit, no width failure and
no watchdog report. The result, transcript provenance, protocol deviations
(`rb`/`rcmd6`) and bounded scope are in
`docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md`. The pre-flash
BOOT image was restored and verified; RECOVERY, EFS and sec_efs are unchanged.
The boot budget is consumed and no retry, reflash, extension or additional
boot is authorized.

The startup path is bounded by core timeouts but not by the report watchdog.
Report gate acquisition is explicitly bounded at 2,000 ms; a timeout is a
terminal reason-8 failure with media offline and no revert claim. The
2026-11-11 review found and the gate-fix candidate closed the unbounded gate
wait at source and host-test level; the 13,010 ms pre-watchdog path remains a
conditional design budget. F5 remains measured at 1-bit and F3 at 4-bit for
one boot each; initialization reliability remains `[U]`.

The host-preparation sequence is recorded in the planning notes as Batch C1;
the gate-fix candidate is C3. The executed procedure, preflight, stop rules and
TWRP return are in
`docs/emmc-n10.16-width4-hardware-protocol-2026-11-11.md`.
