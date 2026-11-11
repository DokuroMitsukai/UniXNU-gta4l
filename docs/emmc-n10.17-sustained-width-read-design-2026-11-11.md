# gta4l N10.17 sustained 4-bit read qualification design (host-only, 2026-11-11)

**Status:** N10.17 host-only design. This batch performed no device access,
boot, flash, candidate build, driver change, QEMU run, commit or push. It
proposes a bounded, opt-in read-only qualification for owner review. It does
not authorize implementation, candidate production, a device run, a commit or
a push. The implementation batch (working label N10.18), candidate production
(working label N10.19) and any hardware run (working label N10.20) are
separately gated; the plan assigns the final labels.

This revision is reconciled with the owner-approved four-case scope: four
sequential two-block reads at the N10.16-qualified request shape, covering the
first eight blocks (4,096 bytes) of the pinned BOOT partition. Larger 8-block
and 16-block requests were removed after review: the block core issues one
CMD18/CMD12 chunk per two blocks, each with a conservative 4,500 ms source
bound (inhibit, data, drain and stop windows), so the enforced per-case
overrun for an 8-block case is 18,000 ms and for a 16-block case 36,000 ms.
With the reserved 8,220 ms verified revert, the media/partition waits and the
revert gate, an 8-block or larger reference pushes the report-thread bound
past the 30,000 ms watchdog; a 4,096-byte reference is exactly 18,000 ms and
still fits. Larger requests are a later rung, not part of this one.

## 1. Why this rung, and what N10.16 does not establish

N10.16 recorded one owner-reported cold boot in which the driver:

- read the pinned two-block BOOT window (device LBA 2,255,360, 1,024 bytes)
  at 1-bit through the block core;
- switched the card and host to 4-bit SDR at the existing 400 kHz with the
  verified helper;
- read the same window once at 4-bit through the block core, compared it
  byte-for-byte and matched;
- reverted to 1-bit with a captured baseline and verified the host and card
  state;
- continued through advertisement and the publication attempt. The supplied
  report did not establish media or partition publication (`MEDIA whole=0`,
  `PART count=0`); the new mode requires publication and treats its absence
  as a failure.

The measured report was `cases=1 passed=1 failed=0 reason=0 complete=1`,
with `switch=4 verified=1`, `match=1`, `revert=1 rverified=1`, and no
WIDTHFAIL or watchdog line. The one-boot budget is consumed (1/1).

That result qualifies exactly one short block-core read at 4-bit and one
verified return to 1-bit on that boot. It does **not** establish:

- repeated 4-bit reads, or any read after the first one;
- reads through the published `IOMedia` / `IOBlockStorageDriver` /
  `IOBreaker` / `IODeblocker` path while 4-bit is active;
- requests larger than two blocks, or more than one adapter subrequest;
- 4-bit behavior across repeated CMD18/CMD12 sequences;
- attribution of the qualified requests against unrelated traffic;
- recovery from every exit path, including cancellation;
- initialization reliability, repeatability or any failure rate;
- higher clocks, 8-bit, HS/DDR, DMA/CQE/ICE, writes, mount or arbitrary I/O.

N10.17 combines two separately qualified paths and adds repetition at the
qualified request shape: the N10.16 4-bit block-core transport and the
N10.14/N10.15 1-bit published-media path. It performs **four repeated
two-block reads covering the first eight blocks (4,096 bytes) of the pinned
BOOT partition through published media while 4-bit remains active, then a
verified 1-bit recovery.** The new variables are the 4-bit published-media
data transfer and repeated CMD18/stop sequences; larger requests are
explicitly deferred.

The first 1,024 bytes have a prior 1-bit published-media qualification
(N10.15) and a prior 4-bit block-core read (N10.16); the remaining bytes
(1,024-4,095) are bound by the same-boot 1-bit reference, not by prior
coverage. No other partition, range, write, mount or root switch is added.

**Why not larger requests in this rung.** The core chunks every request into
two-block CMD18/CMD12 units (`package/Gta4lEmmcBlockCore.h:25-27`,
`package/Gta4lEmmcBlockCore.c:135-182`). Each unit's conservative source
bound is the pre-command inhibit wait 1,000 ms + the command/data window
1,000 ms + the secondary data-drain window 500 ms + CMD12 entry 1,000 ms +
CMD12 completion 1,000 ms = **4,500 ms**
(`package/Gta4lEmmc.h:231,234,238`; `package/Gta4lEmmc.c:599-616,3706,
3810-3855,3549-3600`). The N10.16 timing table used 3,000 ms and did
not model the inhibit or drain windows; this design uses the larger
conservative term. The report-thread case loop checks its budget only between
cases, so the enforced worst case is the case-loop budget plus one in-flight
case. A two-block in-flight case is 4,500 ms; an 8-block case would be
18,000 ms and a 16-block case 36,000 ms. The approved 4,096-byte reference
uses four two-block chunks and therefore 18,000 ms, which fits the report
budget; a 16-block reference or any 8-block/16-block media case does not fit
the 30,000 ms watchdog with the 8,220 ms verified revert and the
media/partition/gate reserves. A later rung can add larger requests with a
dedicated per-mode watchdog budget or a different chunk contract.

## 2. Evidence and implementation basis

### 2.1 Measured and host-qualified facts

- N10.16 gate-fix candidate boot image
  `58352cd37f8643ba89c07c4911914fd50b4728c0333602aa4ca7840633c8aba7`
  (85,688,320 bytes); the 4-bit WIDTHREAD digest was
  `a6da82e356863d1ab7e31c21a8fd85fc`, the truncated SHA-256 of the pinned
  1,024-byte window. The digest is image-bound; it is **not** compiled into
  the kernel and must not be reused for a different boot image.
- N10.15 qualified a read-only consumer path over the same two-block BOOT
  window at BOOT offset 0 at 1-bit. N10.14 qualified a six-case sweep,
  including the 8-block A0 anchor at BOOT offset 4,096 (covering
  4,096-8,191) and the 32-block A1 anchor at BOOT offset 4,096 (covering
  4,096-20,479), at 1-bit through published media. Bytes 1,024-4,095 have no
  prior media-path qualification.
- The N10.16 host batch covers selector, switch, pre-read, read, compare,
  revert, cancellation and watchdog interleavings with mutation controls.
  The current adapter harness covers 285 scenarios and 104 adapter/support
  plus 10 policy mutation controls; the block helper covers 48 scenarios and
  38 mutation controls. These are host-model results, not hardware evidence.
- EXT_CSD byte 183 is W/E_P and is not a readable width assertion. N10.16
  reported `rb=0` because the hardware register is not readable; the host
  adapter model's `rb=1` is simulated. The new mode keeps byte 183
  diagnostic only and never uses it as width-success proof.
- The verified switch and verified recover helpers both read the 512-byte
  EXT_CSD over the data lines and compare the read-only properties segment
  against the captured baseline (`package/Gta4lEmmc.c:1958-1971,2270-2285,
  2430-2445`). `verified=1` is therefore a data-path check at the new width,
  not a host-register-only snapshot. A card still framed at the other width
  cannot return the matching properties at the host's width. Byte 183
  remains unreadable and is reported only as the diagnostic `rb`/`rcmd6`
  pair.
- The host control width is a mask: `GTA4L_EMMC_HOST_CONTROL_4BIT` is
  `0x02` and `GTA4L_EMMC_HOST_CONTROL_8BIT` is `0x20`
  (`package/Gta4lEmmc.h:103-104`). N10.16 hardware recorded `hb=0 ha=2` for
  the 4-bit switch and `rhb=2 rha=0` for the 1-bit revert. A raw register
  value `4` (`0x04`) is neither mask; only `0x02` means 4-bit and only
  `0x20` means 8-bit. The new mode's checks use the mask constants.

### 2.2 Relevant source paths

| Area | Reference |
| --- | --- |
| N10.16 pre-watchdog sequence | `package/Gta4lEmmcBlock.cpp:1473-1545` |
| width read/capture helper | `package/Gta4lEmmcBlock.cpp:5706-5778` |
| width switch/revert record | `package/Gta4lEmmcBlock.cpp:5781-5858` |
| width recovery attempt | `package/Gta4lEmmcBlock.cpp:5860-5876` |
| bounded gate acquisition | `package/Gta4lEmmcBlock.cpp:5878-5901` |
| width report-thread phase | `package/Gta4lEmmcBlock.cpp:5903-6109` |
| terminal commit and guard | `package/Gta4lEmmcBlock.cpp:6040-6109` |
| sweep observer/attribution | `package/Gta4lEmmcBlock.cpp:3415-3485` |
| sweep read/reference pattern | `package/Gta4lEmmcBlock.cpp:4607-5185` |
| consume read-only client | `package/Gta4lEmmcBlock.cpp:5187-5559` |
| width report emission | `package/Gta4lEmmcBlock.c:2569-2734` |
| width-mode selector | `package/Gta4lEmmcBlock.c:221-242` |
| block core two-block chunk | `package/Gta4lEmmcBlockCore.h:25-27` |
| multi-result record | `package/Gta4lEmmcBlockCore.h:68-74` |
| tagged read callback | `package/Gta4lEmmcBlock.cpp:728-760` |
| verified bus switch | `package/Gta4lEmmc.c:2320-2329` |
| verified bus recover | `package/Gta4lEmmc.c:2341-2456` |
| EXT_CSD properties compare | `package/Gta4lEmmc.c:1958-1971` |
| host width masks | `package/Gta4lEmmc.h:103-104` |
| media wait constant | `package/Gta4lEmmcBlock.h:80` |
| report buffer and width bounds | `package/Gta4lEmmcBlock.h:286-300` |
| watchdog and terminal claim | `package/Gta4lEmmcBlock.cpp:6993-7055` |

The production advertisement is 64 blocks / 32,768 bytes / one segment. The
block core chunk remains two blocks (1,024 bytes per CMD18), so each
two-block request is one adapter subrequest and one CMD18/CMD12 sequence. The
clock remains 400 kHz. No DMA, CQE, ICE or interrupt-policy change is part of
this design.

## 3. Design decisions

### 3.1 A new selector, not a broader N10.16

N10.16's contract, report and measured record are specific to one direct
two-block read. Widening `gta4l-emmc-width=4` to add media reads or repetition
would:

- silently invalidate the recorded N10.16 `reads=2 bytes=2048` semantics;
- make the N10.16 completion predicate mean two different things;
- make a future N10.16 rerun ambiguous against its published evidence.

The design therefore adds a separate token, `gta4l-emmc-widthhold=1`
(working name), requiring exactly one `gta4l-emmc-block=3` token. The token is
mutually exclusive with `gta4l-emmc-width` and every other qualification
token. The absent token leaves all legacy and N10.16 behavior byte-for-byte
unchanged. Section 4.1 defines the exact selection and refusal rules.

### 3.2 Start at the pinned BOOT window

The read set is confined to the first eight blocks (4,096 bytes) of the
pinned BOOT partition, at device LBA 2,255,360. It is covered by four
sequential two-block reads at offsets 0, 1,024, 2,048 and 3,072. That set:

- repeats the exact N10.16 two-block window as case 0, then exercises three
  more slices at the same request shape, so the sustained variable is
  repetition, not a new request size;
- covers bytes 1,024-4,095, which have no prior media-path qualification;
- has every byte independently read at 1-bit in the same boot before the
  switch, so the same-boot reference, not prior coverage, binds the content;
- avoids introducing a second location, runtime-derived offsets, partition
  table content or user input in the first sustained rung.

The content is rebound to the new candidate image. The N10.16 digest is not
assumed to describe the new image even though the first 1,024 bytes are the
same range. The set is a **bounded sample**, not a reliability demonstration
and not an arbitrary-I/O claim. No other partition, range, write, mount or
root switch is in scope.

### 3.3 Same-boot 1-bit reference, host-bound digests

Before the switch, the driver reads the 8-block reference window once at
1-bit through the block core under `gate` and stores all 4,096 bytes. The
switch happens only after that read passes exact status, byte-count, chunk,
stop and stop-failure gates. Each 4-bit case compares its 1,024 bytes to the
reference slice at the case's own offset (case 0 is also the N10.16 window;
case 3 is the last slice). The mode also accumulates the concatenation of the
four observed slices in case order into an observed window digest.

No digest is compiled into the kernel. The report prints the 1-bit reference
window digest, the concatenated 4-bit observed window digest and the last
case's observed/reference slice digests; the host verifier compares the
reference and observed window digests against the exact final packaged BOOT
image's first 4,096 bytes. A host mismatch rejects acceptance even if the
on-device guard reports `complete=1`. The N10.16 1,024-byte digest may be
cross-checked only after confirming that the final packaged image's first
1,024 bytes are byte-identical to the image measured on 2026-11-11; the mode
itself does not assume that equality.

A non-zero-offset case is **not** compared to a reference prefix. The
comparison is always the reference slice at that case's byte offset.

### 3.4 Published-media path and attribution

The 4-bit reads go through the published BOOT partition media, not the direct
block-core helper used by N10.16. The driver opens the partition as a reader
and issues synchronous reads with tagged descriptors, following the sweep and
consume patterns. `doAsyncReadWrite` observes each request at entry.

The mode does **not** hold the controller gate across an `IOMedia` read. It
takes the gate only for the pre-switch reference read and for the post-read
revert. Each media read takes and releases the gate inside the adapter as it
normally does. Attribution is per request window, not per gate hold.

While a request window is active, the observer checks:

- the descriptor tag equals the current case tag;
- the first block equals the requested first block;
- subrequests are contiguous and bounded by the requested end;
- the expected adapter subrequest count is one;
- the largest adapter subrequest is two blocks;
- no foreign or untagged request is seen.

Any foreign request inside an instrumented window is an attribution failure
(`FOREIGN`), not a silent success. Traffic outside the instrumented windows is
outside the claim: the mode does not assert that no other client was active
before or after its own reads. The start-path 4-bit baseline read of device
LBA 1 happens before publication and is not counted as a sustained case.

Per-case CMD18, stop and stop-failure deltas come from the tagged
`readMultiCallback` execution record (`package/Gta4lEmmcBlock.cpp:728-760`),
which runs inside the adapter's own gate hold for that request. The mode does
not sample global block-core counters under its own gate, because it does not
hold the gate across the `IOMedia` read.

### 3.5 Recovery on every recoverable exit

Once the switch succeeds, the card is at 4-bit. Every recoverable exit then
attempts `gta4l_emmc_set_bus_width_recover(&host, 1, baseline, &revert)` under
a bounded gate acquisition and requires the verified 1-bit state. This
includes the post-switch device LBA 1 baseline failure, an advertisement
property failure, a report-thread or watchdog-thread start failure, media
discovery failure, partition or geometry failure, open failure, descriptor
failure, any read failure, content mismatch, order or foreign failure, budget
exhaustion and cancellation. The start-path failures perform the recovery
synchronously before returning or emitting the terminal report; the thread
start failures use the same synchronous helper, and `claimTerminal()` prevents
a late second report.

Buffer allocation for the reference and observed windows happens before the
switch. Allocation failure refuses the mode with a logged startup failure and
no card change, matching the existing buffer-allocation paths. It is not a
recoverable post-switch exit.

The recover helper's acceptance checks are the recovery proof:

- `verified=1` from a 512-byte EXT_CSD data read whose read-only properties
  segment matches the pre-switch 1-bit baseline;
- host after with neither the `0x02` 4-bit nor the `0x20` 8-bit mask set;
- card `bus_width` code back to 1-bit;
- `rcmd6` in `[1, 2]`.

The EXT_CSD read is a data transfer at the 1-bit host width; a card still
framed at 4-bit cannot return the matching properties. The mode does not use
byte 183 as proof and does not claim a separate media-level read after the
revert. If the helper reports `ERR_WIDTH_RESTORE`, framing is unknown and the
mode stops without further card commands.

The only post-switch exits that do not attempt a revert are:

- the controller gate cannot be acquired within the bound, in which case
  another request still owns the card and the mode fails closed with the
  media offline and no further card command;
- the bus helper reports the unknown-framing restore error, in which case the
  mode stops without further card commands;
- the watchdog has already claimed terminal ownership.

First failure is preserved. A later revert failure does not overwrite an
earlier read/match/transport failure; it is recorded in the revert fields and
forces `complete=0`.

### 3.6 Cancellation and watchdog

Cancellation is recorded but does not skip recovery. If the driver is
stopping while 4-bit is active, the report thread abandons the remaining
cases and still attempts the bounded verified revert. This is stricter than
the N10.16 report-thread entry path, which returns immediately on a cancel
observed before its read. The new mode's recovery rule is part of its
contract.

- Cancel before the switch: no width change; quiet cancel, no normal report.
  This is the only quiet path.
- Cancel after the switch, including during the synchronous switch
  completion, media discovery, between cases, after the last read but before
  the revert, an in-flight `IOMedia` read, or the revert: record `cancel=1`,
  skip the remaining cases, attempt the verified revert if it has not
  already run, and emit exactly one terminal `WIDTHHOLD` report with
  `cancel=1`, `complete=0` and the revert fields. A successful revert does
  not suppress the report; a failed revert fails closed with the first
  failure preserved.
- Cancel during the synchronous verified switch: the switch completes or
  fails. If it succeeds and the cancel is latched, the mode treats it as a
  post-switch cancel and runs recovery/report. If the switch fails, the
  switch failure is the first failure.
- Cancel during the revert: the revert is the recovery. On success, emit the
  terminal report with `cancel=1` and the revert fields; on failure, fail
  closed with `cancel=1` and the revert failure.
- Cancel during the bounded revert-gate wait: the gate attempt continues to
  its 2,000 ms bound. On success the revert runs; on timeout the
  gate-timeout fail-closed path applies. The cancel is recorded as `cancel=1`
  in the terminal report.
- The start path checks `reportCancel` after the reference read and before
  the switch. A cancel latched during the reference read skips the switch and
  takes the quiet pre-switch path; no width change has occurred.
- Cancel while a synchronous `IOMedia::read()` is in flight: the read is not
  interruptible. The cancel is latched, the read completes or the block core
  times out, then the thread skips the remaining cases and still attempts
  the verified revert. The attribution window is armed and disarmed under
  `reportLock`, so a cancel cannot split the window state.
- Watchdog: at 30,000 ms the watchdog claims terminal ownership and emits
  the source-defined `fail_stage=16` snapshot
  (`fillWatchdogReport`, `package/Gta4lEmmcBlock.cpp:6993-7004`) unless
  `reportDone` was already set. A report-thread terminal attempt after the
  watchdog claim is a no-op. Exactly one panic report is emitted across
  watchdog and report thread on every non-quiet path. The watchdog never
  sets `complete=1`, so a watchdog report cannot be a false completion.

### 3.7 Bounded report and distinct results

The mode reports transport shape, content comparison, reference binding,
switch/revert state, cancellation and budget as distinct fields. It does not
collapse them into one boolean. `complete=1` is a structural predicate over
all of them; a digest-only or byte-183-only result can never satisfy it.

## 4. Proposed N10.17 mode contract

### 4.1 Token and selection

- Boot argument: `gta4l-emmc-widthhold=1`.
- Requires exactly one `gta4l-emmc-block=3` token. Refuse a missing block
  token, any block value other than 3, duplicate block tokens, duplicate
  widthhold tokens, widthhold values other than 1, and every combination with
  `gta4l-emmc-partobs`, `gta4l-emmc-partcmp`, `gta4l-emmc-partread`,
  `gta4l-emmc-deblock`, `gta4l-emmc-breakup`, `gta4l-emmc-sweep`,
  `gta4l-emmc-consume` or `gta4l-emmc-width`.
- `gta4l-emmc-widthhold=0` is refused. Legacy behavior is the absence of the
  token, never a zero value.
- Selector function: add `gta4l_emmc_block_widthhold_select()` beside
  `gta4l_emmc_block_width_select()` in `package/Gta4lEmmcBlock.c`. It takes
  the same mode-state arguments plus the already-selected width state, so a
  combined `width=4 widthhold=1` boot is refused rather than resolved by
  ordering.
- Refusal messages name the duplicate, the missing/wrong block token, or the
  conflicting mode token, matching the existing style.

### 4.2 Run sequence

1. `start()` parses and selects the mode. `initializeHardware()` runs as
   today. No buffer is allocated unless the mode is selected.
2. Allocate two page-aligned 4,096-byte buffers (`reference`, `observed`) and
   initialize the widthhold report scalars before any early failure can
   panic.
3. Under `gate`, read the 1-bit EXT_CSD baseline into a persistent 512-byte
   widthhold member. The baseline must survive to the report-thread revert;
   the existing width mode already keeps it in driver state
   (`package/Gta4lEmmcBlock.cpp:567,5866`). Require `GTA4L_EMMC_OK`;
   otherwise fail before the switch.
4. Under `gate`, read the 8-block reference window at 1-bit through the
   block core: device LBA 2,255,360, 4,096 bytes, four two-block CMD18
   chunks, four stops, zero stop failures, last stop result zero,
   `status=0`, `actual=4096`. Store all bytes and compute the reference
   window digest. Any failure fails before the switch and before publication.
   The core aborts the read on the first failed chunk, so the failure path is
   shorter than the strict four-chunk bound.
5. Call `gta4l_emmc_set_bus_width_verified(&host, 4, &switch)`. Require
   `verified=1`, `switch_result=OK`, host 4-bit mask `0x02` and the card at
   4-bit. Record the switch fields. On `ERR_WIDTH_RESTORE`, stop without
   further reads or recovery.
6. Release `gate`. Run the existing device LBA 1 baseline read at the new
   width. A failure here is a 4-bit read failure; latch `BASE4`, recover and
   fail closed.
7. Continue with the existing production advertisement (64 blocks / 32,768
   bytes / one segment), publication and report/watchdog thread start. An
   advertisement/geometry failure latches `ADVERT`; a thread start failure
   latches `THREAD`. Both perform the synchronous verified revert before the
   terminal report.
8. In the report thread, wait up to 8,000 ms for exactly one whole media and
   up to 2,000 ms for exactly one BOOT partition at the pinned base/size.
   Verify whole geometry, boot size/base, boot read-only property and the
   media content check. Any failure skips the read set and goes to step 11.
9. Open the BOOT partition as a reader. Run the fixed read set in table
   order. For each case: arm the attribution window, issue one synchronous
   tagged `IOMedia::read()`, disarm, verify tag/order/block/subrequest shape,
   require success and exact length, compare bytes against the reference
   slice at the case offset, and accumulate counters and the observed window
   digest. Stop the set on the first failure, or when the case-loop budget
   (3,000 ms, checked before each case) is exhausted.
10. On success, the four slices cover the whole 8-block reference window;
    the concatenated observed window digest and the reference window digest
    are the host-checkable pair. The last case's slice digest is also printed
    for a direct image comparison.
11. Acquire `gate` within 2,000 ms, attempt the verified 1-bit revert, and
    require `verified=1`, host 1-bit mask (both `0x02` and `0x20` clear), card
    1-bit and the captured-baseline properties comparison. If the gate
    cannot be acquired, fail closed without a revert. If restore is unknown,
    stop without further commands.
12. Release `gate`, close the media, release discovered objects, fill the
    report under `reportLock`, compute `complete`, and call
    `panicWithReport()` (or `panicWithReportNoGate()` for the gate-timeout
    path). A post-switch cancel still reaches this terminal report with
    `cancel=1`.

### 4.3 Read set

Reference window: BOOT partition offset 0, 8 blocks, 4,096 bytes, device LBA
2,255,360. The set is four two-block reads covering the reference window one
slice at a time. All offsets are byte offsets within the BOOT partition.

| Index | Offset | LBA | Blocks | Bytes | Span | Expected subrequests | Purpose |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 0 | 2,255,360 | 2 | 1,024 | 1,024 | 1 | N10.16 window, repeat 1 |
| 1 | 1,024 | 2,255,362 | 2 | 1,024 | 1,024 | 1 | repeat 2 |
| 2 | 2,048 | 2,255,364 | 2 | 1,024 | 1,024 | 1 | repeat 3 |
| 3 | 3,072 | 2,255,366 | 2 | 1,024 | 1,024 | 1 | repeat 4, last slice |

Totals: 4 requests, 8 blocks, 4,096 bytes, largest request 2 blocks /
1,024 bytes, expected 4 adapter subrequests, 4 CMD18 chunks and 4 stops. The
production 64-block advertisement covers every request, so each is a single
adapter subrequest; the two-block core chunk produces exactly one CMD18/CMD12
sequence per request.

Rationale:

- Four two-block reads at the already-qualified request shape exercise
  repeated 4-bit transfer, stop and comparison without adding a new request
  size.
- The slices cover offsets 0-4,095 contiguously. Case 0 repeats the N10.16
  window; cases 1-3 add the 1,024-4,095 bytes with no prior media-path
  qualification.
- No case is unaligned, so no IODeblocker head/tail split is introduced.
  The 1-bit deblocker and breakup paths are not re-qualified here.
- No case needs more than one segment, so the production one-segment
  advertisement is unchanged.
- The 4,096-byte reference is four two-block chunks (18,000 ms conservative
  bound) and fits the report-thread budget. 8-block and 16-block media
  requests and the withdrawn 16-block reference remain a separate rung with
  a dedicated budget or a different chunk contract.

The set, order, tags, offsets and lengths are fixed at build time. No user
input selects a range, and no runtime derivation extends the scope.

### 4.4 Reference capture

The N10.16 capture callback is clamped to `WIDTH_CASE_BYTES` (1,024 bytes).
The implementation batch must add a separate bounded widthhold capture path
(or a bound parameter) that copies up to 4,096 bytes, without changing the
N10.16 path. The reference helper must report:

- `status`, `actual`, block delta, CMD18 chunk delta, stop delta and stop
  failure delta;
- the first failing block and raw error when the core's first-failure record
  falls inside the reference window.

The reference read is one `gta4l_emmc_block_read()` call with 8 blocks and
4,096 bytes. The exact-shape gate requires four chunks, four stops, zero
stop failures and the last stop result zero, in addition to
`status=0`/`actual=4096`.

### 4.5 Media discovery and checks

- Wait up to `WIDTHHOLD_MEDIA_WAIT_MS` (8,000 ms) for exactly one whole media
  of `capacityBlocks` blocks, with `whole=1`, `base=0`, `size=capacityBlocks *
  512`, a writable property present and false, and the existing
  content/geometry check passing. The property must be present; a missing
  writable property is a `MEDIA` failure, not an implicit read-only success
  (this is stricter than the partread path, which treats absence as
  read-only).
- Then wait up to `WIDTHHOLD_DEADLINE_MS` (2,000 ms) for exactly one BOOT
  partition media with base `GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES` and
  size `GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES`.
- Require the BOOT media size property to equal the pinned size, the base to
  equal the pinned base, the writable property to be present and false, and
  the content/geometry check to pass. The existing `findPartitionMedia`
  helper matches whole/base/size only and ignores writability
  (`package/Gta4lEmmcBlock.cpp:2850-2907`); the implementation batch must
  extend the BOOT lookup (or add a side check on the returned media) so
  `partro=1` is derived from a real property read, and the host harness must
  cover both a writable-true and a writable-absent refusal.
- A missing, duplicate or malformed media is a `MEDIA`/`PART` failure. The
  read set is skipped and recovery still runs.
- The whole media is discovered but not opened. Only the BOOT partition is
  opened as `kIOStorageAccessReader`. This deliberately diverges from the
  sweep/consume modes, which also open the whole media as a reader; the
  host harness must cover the no-open path so the deviation is intentional
  and tested.

The 8,000 ms wait is 40% of the existing `GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS`
(20,000 ms) because the 20,000 ms wait plus the required revert does not fit
the 30,000 ms watchdog under the conservative per-chunk term. Publication is
requested before the report thread starts, so the shorter wait is a bounded
assumption, not a measured latency. A 10,000 ms wait leaves only 280 ms of
watchdog margin; 8,000 ms leaves 2,280 ms. The candidate must record media
discovery latency; section 8 lists both values.

### 4.6 Per-case evidence and attribution

For each case the driver records:

- index, tag, requested offset, length, span, blocks;
- adapter subrequest count, block count, largest subrequest and order flag;
- foreign count for the instrumented window;
- `IOReturn` status, actual byte count, CMD18 chunk count, stop count and
  stop-failure count from the tagged `readMultiCallback` execution record;
- comparison result and, on mismatch, the first mismatching byte offset;
- observed digest for the case slice;
- reference status/length for the same slice.

The adapter-level observe must not perturb request behavior: the request,
gate and block-core call remain unchanged; only bookkeeping is added. The
tagged callback runs inside the adapter's own gate hold for the request, so
its CMD18/stop/stopfail deltas are attributable without the mode holding the
gate across the `IOMedia` read. A foreign request inside the window
invalidates the attribution and fails the run.

### 4.7 Revert and fail-closed states

The revert uses the same helper and acceptance checks as N10.16:

- `widthhold_revert=1`, `widthhold_rverified=1`;
- host before with the `0x02` 4-bit mask, host after with neither `0x02` nor
  `0x20`;
- card `bus_width` back to 1-bit;
- `rcmd6` in `[1, 2]`.

Fail-closed conditions:

| Condition | Card state | Media | Further commands | Report |
| --- | --- | --- | --- | --- |
| Gate timeout before revert | 4-bit | offline | none | `GATE`, `complete=0`, no-gate snapshot |
| Restore unknown (`ERR_WIDTH_RESTORE`) | unknown | offline | none | `REVERT`, `complete=0` |
| Revert verified failure | unknown/4-bit | offline | none | `REVERT`, `complete=0` |
| Earlier failure, revert success | 1-bit | offline | none | first failure preserved, `complete=0` |
| Earlier failure, revert failure | unknown | offline | none | first failure preserved plus revert fields, `complete=0` |
| Post-switch cancel, revert success | 1-bit | offline | none | `CANCEL`, `cancel=1`, `complete=0` |
| Post-switch cancel, revert failure | unknown | offline | none | `CANCEL`/`REVERT` first failure, `cancel=1`, `complete=0` |
| Full success | 1-bit | online | none | `complete=1` |

Any earlier read, comparison, order, foreign or budget failure takes the
media offline before the terminal report, and a later successful revert does
not bring it back online. Only a fully successful run leaves the media online
at 1-bit. This matches the existing read-callback failure contract.

### 4.8 Report fields and lines

New report scalar `widthhold` selects the family. The implementation batch
adds a bounded `widthhold_*` field set, including:

- mode/selection: `widthhold=1`, `cases=4`;
- progress: `passed`, `failed`, `reason`, `complete`;
- switch: `switch`, `verified`, `hb`, `ha`, `rb`, `cmd6`;
- reference: `refs=1`, `refbytes=4096`, `refblocks=8`, `refstatus`,
  `refchunks=4`, `refstops=4`, `refstopfail=0`, `refdigest`;
- media: the base `MEDIA` line carries the whole-media geometry and
  writability; the `WIDTHHOLD` summary carries `partbase`, `partsize` and
  `partro` (1 when the BOOT media reports writable false). The widthhold
  family suppresses the base `PART` summary and the three `PART1`-`PART3`
  detail lines: the summary's `part_count`/`part_expected` are populated only
  by the default `countPartitions()`/GPT path, are zero in this mode, and the
  detail lines have base/size/content but no writable field. It also extends
  the existing `(consume == 0) && (width == 0)` guard so the legacy READ,
  QUAL and QUAL3 lines are suppressed; those describe a different contract
  and would otherwise print zero fields. The PART summary `else` branch and
  the PART1-PART3 loop are currently ungated, so dedicated widthhold guards
  are required on both; extending only the READ/QUAL/QUAL3 guard is
  insufficient. This keeps the BOOT read-only evidence in the bounded new
  line instead of expanding shared base-line formats;
- reads: `reads`, `bytes`, `blocks`, `subrequests`, `max`, `c18`, `stops`,
  `stopfail`;
- content: `match`, `mismatches`, `fails`, `foreign`, `obsdigest`;
- control: `cancel`, `budget`, `limit=64`, `seg=1`, `cap`;
- last case (success line): `index`, `lba`, `start`, `length`, `span`,
  `actual`, `status`, `subrequests`, `blocks`, `max`, `digest`, `predigest`,
  `match`;
- first failure line: `index`, `status`, `actual`, `start`, `length`, `span`,
  `blocks`, `max`, `digest`; the summary's `refdigest`/`refstatus`/`refbytes`
  bind the 1-bit reference on a failed run;
- revert: `revert`, `rverified`, `rhb`, `rha`, `rcmd6`, `rstatus`.

`digest` is the observed bytes for the line's scope; `predigest` is the same
scope from the 1-bit reference. The summary's `obsdigest` is the concatenated
4-bit window and `refdigest` the 1-bit reference window. Digests are 16
bytes, printed as 32 hex characters.

Proposed lines:

```
GTA4L-EMMC P5 WIDTHHOLD: cases=4 passed=4 failed=0 reason=0 complete=1
switch=4 verified=1 hb=0 ha=2 rb=0 cmd6=1 refs=1 refbytes=4096 refblocks=8
refstatus=0 refchunks=4 refstops=4 refstopfail=0 refdigest=<32 hex>
reads=4 bytes=4096 blocks=8 subrequests=4 max=2 c18=4 stops=4 stopfail=0
match=1 mismatches=0 fails=0 foreign=0 cancel=0 budget=0 obsdigest=<32 hex>
partbase=1154744320 partsize=100663296 partro=1
revert=1 rverified=1 rhb=2 rha=0 rcmd6=1 limit=64 seg=1 cap=61071360
GTA4L-EMMC P5 WIDTHHOLDREAD: index=3 lba=2255366 start=3072 length=1024
span=1024 actual=1024 status=0 subrequests=1 blocks=2 max=2
digest=<32 hex> predigest=<32 hex> match=1
```

Failure adds `WIDTHHOLD` with `complete=0`, `reason` set and one
`WIDTHHOLDFAIL` line with the first-failure fields and the observed slice
digest when one exists. `WIDTHHOLDREAD` and `WIDTHHOLDFAIL` are mutually
exclusive: the success path emits the read line, the failure path emits the
fail line, never both. The base `MEDIA` line carries the whole-media
geometry and writability; the `WIDTHHOLD` summary's `partbase`/`partsize`/
`partro` carry the BOOT partition evidence.

`rb` remains the diagnostic byte-183 readback and is never part of the
completion guard; `rcmd6` is accepted in `[1, 2]`. Host tests must not pin
`rb`: the adapter model emits `rb=1`, while hardware carries `rb=0`.

Report-buffer sizing: the current buffer is
`GTA4L_EMMC_BLOCK_REPORT_BYTES` 2,048 bytes
(`package/Gta4lEmmcBlock.h:300`). The formatter-exact widthhold worst case,
recomputed from the per-line shape with new numeric fields capped at u32, is
**2,078 bytes** on the success path (838 base + 905 summary + 335
WIDTHHOLDREAD) and **1,970 bytes** on the failure path (838 base + 905 + 227
WIDTHHOLDFAIL). Four versus eight cases is byte-identical because only the
last-case fields are printed. The widthhold family suppresses the base
`PART` summary, `PART1`-`PART3`, and legacy READ/QUAL/QUAL3, so none enters
the bound. 2,078 exceeds the current 2,048, so the implementation batch
raises `GTA4L_EMMC_BLOCK_REPORT_BYTES` to 2,560 and adds
`WIDTHHOLD_REPORT_WORST_BYTES <= GTA4L_EMMC_BLOCK_REPORT_BYTES` plus
per-line static asserts. New numeric fields must be capped at u32; an
all-u64 variant with a signed status field recomputes to 2,599 and would not
fit 2,560. The existing WIDTH caps (513/329/127, worst 1,772) are not
literal type-max exact (the WIDTH summary text recomputes to 522), so the
widthhold caps must be derived fresh rather than copied. The existing WIDTH,
CONSUME and other worst-case asserts remain valid at the larger buffer.

### 4.9 Success predicate

`complete=1` requires all of:

- `widthhold=1`, `cases=4`, `passed=4`, `failed=0`, `reason=NONE`;
- `switch=4`, `verified=1`, `(hb & 0x22) == 0`, `(ha & 0x22) == 0x02`,
  `cmd6=1`;
- `refs=1`, `refbytes=4096`, `refblocks=8`, `refstatus=0`, `refchunks=4`,
  `refstops=4`, `refstopfail=0`, `refdigest` present;
- `reads=4`, `bytes=4096`, `blocks=8`, `subrequests=4`, `max=2`, `c18=4`,
  `stops=4`, `stopfail=0`, `obsdigest == refdigest`;
- `match=1`, `mismatches=0`, `fails=0`, `foreign=0`, `cancel=0`, `budget=0`;
- last case index 3, LBA 2,255,366, start 3,072, length 1,024, span 1,024,
  actual 1,024, status 0, subrequests 1, blocks 2, max 2, observed slice
  digest equal to the reference slice digest;
- `revert=1`, `rverified=1`, `(rhb & 0x22) == 0x02`, `(rha & 0x22) == 0`,
  `rcmd6` in `[1, 2]`;
- `limit=64`, `seg=1`, `cap=capacityBlocks`;
- whole media present with the expected geometry and `writable=0` (base
  `MEDIA` line); exactly one BOOT media discovered (the one opened) with
  `partbase == GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES`,
  `partsize == GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES` and `partro=1`,
  where `partro` is read from that media's writable property;
- no watchdog report.

A missing/extra case, wrong advertisement, foreign request, short transfer,
stop failure, properties mismatch, digest-only or byte-183-only claim, or
host-side digest mismatch cannot produce `complete=1`.

### 4.10 Failure reasons

Proposed `widthhold_reason` codes. These live in a separate namespace and
field from the N10.16 `width_reason` values, so numeric overlap is not a
conflict and no widthhold code is ever interpreted as a width reason (or
vice versa):

| Value | Name | Meaning |
| ---: | --- | --- |
| 0 | NONE | success |
| 1 | BASELINE | 1-bit EXT_CSD baseline failed |
| 2 | PREREAD | 1-bit reference window failed |
| 3 | SWITCH | verified 4-bit switch failed |
| 4 | MEDIA | whole media missing/duplicate/malformed |
| 5 | PART | BOOT partition missing/duplicate/malformed |
| 6 | OPEN | BOOT partition open failed |
| 7 | ARG | invalid case table or selector mismatch |
| 8 | DESC | descriptor allocation failed |
| 9 | READ | media read returned an error |
| 10 | SHORT | media read returned fewer bytes |
| 11 | MATCH | byte comparison failed |
| 12 | ORDER | tag/order/block/subrequest shape failed |
| 13 | FOREIGN | unrelated request inside an instrumented window |
| 14 | BUDGET | case-loop budget exhausted |
| 15 | CANCEL | canceled after the switch |
| 16 | GATE | revert gate unavailable |
| 17 | REVERT | verified 1-bit revert failed |
| 18 | BASE4 | 4-bit device LBA 1 baseline failed after the switch |
| 19 | ADVERT | production advertisement/geometry property failed |
| 20 | THREAD | report or watchdog thread start failed |
| 21 | ALLOC | widthhold buffer allocation failed (pre-switch) |

The first failure is latched and never overwritten. The report always carries
the first failure's index, status, actual length and digest where available.

## 5. Timing

All values are conditional bounds derived from the existing source constants,
not proofs. The successful path is expected to be far shorter.

### 5.1 Pre-watchdog path in `start()`

| Step | Bound |
| --- | ---: |
| 1-bit EXT_CSD baseline | 2,000 ms |
| 1-bit 8-block reference read (4 chunks x 4,500) | 18,000 ms |
| verified 4-bit switch | 6,010 ms |
| 4-bit LBA 1 baseline read | 2,000 ms |
| **Success path before the report thread** | **28,010 ms** |
| post-switch failure recovery (gate + verified revert) | +10,220 ms |
| **Failure path before the report thread** | **38,230 ms** |

The 18,000 ms reference term is four two-block CMD18/CMD12 chunks, each
bounded by the pre-command inhibit 1,000 ms + command/data 1,000 ms +
secondary drain 500 ms + CMD12 entry 1,000 ms + CMD12 completion 1,000 ms =
4,500 ms. The N10.16 table used 3,000 ms; this design uses the larger
conservative term. The block core aborts the read on the first failed chunk,
so a failure path is shorter; 18,000 ms is the strict success bound where
every chunk succeeds near its timeout edge. A `BASE4`, `ADVERT` or `THREAD`
failure after the switch adds the bounded gate acquisition (2,000 ms) and the
verified revert (8,220 ms), so the un-watchdogged failure path can reach
38,230 ms. The watchdog does not cover either path because it starts with the
report thread. These are separate pre-watchdog budgets, not watchdog claims.
A switch failure has a larger bound than a successful switch because the
verified helper's rollback can make two CMD6/TRAN/EXT_CSD passes:
2,000 + 18,000 + 16,030 = **36,030 ms**. That exceeds the 28,010 ms success
budget but stays inside the 38,230 ms overall failure bound; the mode fails
closed there without the LBA 1 baseline, publication, case read, success
revert or an extra recovery sequence.

### 5.2 Report thread

| Step | Bound |
| --- | ---: |
| whole-media wait | 8,000 ms |
| BOOT partition discovery | 2,000 ms |
| case-loop budget (checked between cases) | 3,000 ms |
| one in-flight two-block case | 4,500 ms |
| bounded gate acquisition for revert | 2,000 ms |
| verified 1-bit revert | 8,220 ms |
| **Total** | **27,720 ms** |

Margin against the 30,000 ms watchdog is 2,280 ms.

The case-loop bound is the N10.15 consume read budget
(`package/Gta4lEmmcBlock.h:253`), not the N10.11 sweep budget (4,000 ms,
`package/Gta4lEmmcBlock.h:218`), and not the sum of all four chunks. The
loop checks elapsed time before issuing each case and stops issuing new cases
at 3,000 ms; the last issued case can overrun by its own two-block bound of
4,500 ms. If the budget were absent, four slow chunks would sum to
18,000 ms, but the budget stops the loop after the first slow case. A chunk
that hits its timeout is also a read failure, which also stops the set. The
success path is expected to complete all four reads well inside 3,000 ms at
400 kHz.

The 8,000 ms media wait is the largest round value that keeps a meaningful
margin under the conservative per-chunk term; 10,000 ms would give 29,720 ms
and a 280 ms margin. The media wait is the least-measured assumption:
publication is requested before the report thread starts, but no hardware run
has recorded media-ready latency. The candidate must record it so the
assumption can be tightened or rejected.

### 5.3 Conditional notes

A single hardware PASS is not timing proof. The candidate protocol should
record the report's elapsed/budget fields and any media discovery latency so
the conditional assumptions can be tightened or rejected. The strict
pathological sum for a four-chunk 4-bit set, if the case budget were absent,
would be 18,000 ms; the watchdog preempts at 30,000 ms and reports a watchdog
failure, never `complete=1`. The pre-watchdog path is not covered by the
watchdog and is bounded only by the source timeouts above.

## 6. Host test and mutation plan

The implementation batch must add, and pass, at least:

### 6.1 Selector and legacy

- Token absent: every legacy mode and N10.16 output unchanged.
- Orphan widthhold, duplicate widthhold, values 0 and 2, missing block token,
  block values 1/2/4, and conflicts with each of `partobs`, `partcmp`,
  `partread`, `deblock`, `breakup`, `sweep`, `consume` and `width`.
- `block=3 widthhold=1` accepted; `block=3 width=4 widthhold=1` refused.

### 6.2 Reference and switch

- Reference success with exact four-chunk/four-stop shape.
- Reference status failure, short actual, missing/extra chunk, missing stop,
  stop failure, and non-zero last stop.
- Switch success; switch failure with proven rollback; restore-unknown
  stop-without-recovery.
- 4-bit baseline failure after the switch; `BASE4` reason, fail-closed state
  and report.
- Advertisement/geometry failure (`ADVERT`) and report/watchdog thread start
  failure (`THREAD`), each with synchronous verified recovery.

### 6.3 Media and per-case reads

- Whole media missing, duplicate, wrong geometry, wrong writable/content.
- BOOT partition missing, duplicate, wrong base, wrong size, writable.
- Open failure; descriptor allocation failure.
- Each of the four cases: success; read error; short actual; content
  mismatch; mismatch offset; wrong tag; wrong first block; non-contiguous
  subrequests; extra subrequest; wrong largest subrequest; foreign request
  inside the window; comparison against the wrong reference slice.
- Budget boundary at `elapsed == budget` and `elapsed == budget - 1`.
- Media wait and partition deadline boundary values.

### 6.4 Recovery and terminal ownership

- Revert first-attempt success; second-attempt success; both-attempt failure;
  host state mismatch; card state mismatch; restore-unknown.
- Gate acquisition success at the deadline boundary; gate timeout with the
  card still at 4-bit; no-gate snapshot.
- Cancel before switch, during media wait, between cases, during an in-flight
  `IOMedia` read, during the synchronous switch, during revert, and after
  revert before terminal.
- Post-switch cancel emits exactly one report with `cancel=1`, the revert
  fields and `complete=0`; it is never quiet.
- Watchdog before the first case, during a case, after the last case and
  after revert; exactly one terminal report and no false completion.
- First-failure preservation when cancel or revert fails after a read
  failure.

### 6.5 Mutations

Mutation controls must reject:

- forcing `complete=1` with a skipped case, skipped comparison, skipped
  revert, ignored foreign request, ignored short transfer or wrong counts;
- changing `c18`/`stops`/`stopfail` expectations;
- accepting `rb=1` as width proof;
- accepting a digest-only result without byte comparison;
- accepting a media read with a wrong tag or overlapping range;
- accepting a non-zero-offset case compared to a reference prefix instead of
  its reference slice;
- suppressing the post-switch cancel report;
- accepting `limit`, `seg` or capacity mismatches;
- accepting a cancel or budget result as success;
- emitting more than one terminal report.

### 6.6 Report

- Line caps and exact worst-case report bytes derived from the formatter with
  new numeric fields capped at u32, against the 2,560-byte buffer. Include a
  mutation that widens a new field to u64 and must fail the static assert or
  cap test.
- Digest byte-count semantics: `16u` bytes means 32 hex characters.
- Mutually exclusive WIDTH and WIDTHHOLD families; neither can leak fields
  into the other. `WIDTHHOLDREAD` and `WIDTHHOLDFAIL` are mutually exclusive.
- A widthhold run emits no base `PART` summary, no `PART1`-`PART3` detail
  lines and no legacy READ/QUAL/QUAL3 lines; a mutation that leaves any of
  them emitted fails the report-shape test.
- Missing/duplicate lines cannot format a successful report.
- Host tests must not pin `rb`; hardware carries `rb=0`, the adapter model
  `rb=1`.
- A host verifier test (for example `scripts/check-gta4l-widthhold-evidence.py`)
  binds `refdigest` and `obsdigest` to the packaged image's first 4,096 bytes
  and the last-case `digest`/`predigest` to the last 1,024-byte slice, with a
  one-byte-changed negative fixture and a digest-truncation self-test. A host
  mismatch is rejection even when `complete=1`.

## 7. Candidate, packaging and hardware gates

None of this is authorized by the design.

1. **Implementation (N10.18, working label).** Selector, driver changes,
   report fields/lines, the 2,560-byte report buffer with static asserts,
   harness scenarios, mutations, privacy checks and host evidence.
2. **Candidate (N10.19, working label).** Produce the prelinked kernel,
   kernel-build archive, storage-ABI audit and packaged boot image. Bind the
   reference and observed window digests to that exact image; verify the
   packaged image's first 4,096 bytes with an independent host hash. No
   digest is compiled into the kernel.
3. **Hardware protocol (N10.20, working label).** A new protocol document
   must fix the selectors, BOOT-only write, verified backups, restore
   procedure, stop conditions and a finite boot budget. The N10.16 budget is
   consumed; a fresh explicit owner approval is required. Initialization
   failure also consumes an attempt.
4. **Execution.** One cold boot with
   `gta4l-emmc-block=3 gta4l-emmc-widthhold=1`, terminal transcript capture,
   return to TWRP, BOOT restore under separate approval, and host digest
   verification. No CMD19/CMD14, no writes, no mount, no extra boot and no
   retry without a new protocol.

The candidate must not enable widthhold by default. It must not include
CMD19/CMD14, 8-bit, HS/DDR, a faster clock, DMA/CQE/ICE, filesystem
mount/repair, user-data reads or storage writes.

## 8. Owner-approved decisions and remaining open items

The owner approved the reduced four-case design on 2026-11-11. The following
are fixed inputs to the N10.18 implementation:

1. Read set: four two-block requests / 8 blocks / 4,096 bytes at offsets
   0, 1,024, 2,048 and 3,072; last case index 3, LBA 2,255,366, start 3,072.
2. Pre-watchdog budget: 28,010 ms success / 38,230 ms failure, with a
   36,030 ms failed-switch bound. The report-thread bound is unchanged at
   27,720 ms, leaving a 2,280 ms watchdog margin.
3. Media wait: 8,000 ms for the 2,280 ms margin. No measured media-ready
   latency exists; the candidate must record it.
4. Report buffer: 2,560 bytes, with the formatter-exact worst case 2,078
   (read path) / 1,970 (fail path), u32 field caps and static asserts.
5. Foreign-traffic policy: strict failure.
6. Whole-media discovery is required for geometry and writability, but only
   the BOOT partition is opened.
7. Gate timeout fails closed without a revert, because another request still
   owns the card.
8. The start-path LBA 1 4-bit baseline read is not reported in the widthhold
   counters; it remains an inherited start-path read.
9. Post-switch cancellation always reports; only the pre-switch cancel is
   quiet.
10. Final token and report names remain open (`widthhold` is a working name).
11. Any later expansion to unaligned/deblocker, larger requests, other
    partitions or repeated boots is a separate rung with its own design and
    approval.

## 9. Go/no-go verdict

**GO for implementation design:** the mode is a bounded, opt-in, read-only
extension that combines the N10.16-qualified 4-bit transport with the
N10.14/N10.15-qualified 1-bit published-media path, adds four repeated
two-block reads at the qualified request shape, keeps the production clock and
advertisement, preserves N10.16 semantics, requires same-boot 1-bit
references, defines failure and recovery paths including post-switch cancel,
and fits the report-thread watchdog budget under the stated
one-overrunning-two-block-case convention (27,720 ms < 30,000 ms, 2,280 ms
margin). The pre-watchdog reference path has a separate conditional bound:
28,010 ms on the success path and 38,230 ms when a post-switch failure adds
the synchronous gate and verified revert; it is not watchdog-covered.

**NO-GO for hardware now:** no candidate exists for this mode, no protocol or
boot budget is approved, and N10.16's initialization reliability and
repeatability remain unknown. The first sustained rung, even if it passes,
would show only that a finite repeated media-path sequence completed once with
verified recovery, not a failure rate or arbitrary-I/O readiness.

## 10. References

- `docs/emmc-n10.16-4bit-sdr-readiness-2026-11-10.md`
- `docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md`
- `docs/emmc-n10.16-width4-gatefix-candidate-2026-11-11.md`
- `docs/emmc-n10.14-sweep-hardware-run-2026-11-10.md`
- `docs/emmc-n10.15-read-only-consumer-design-2026-11-10.md`
- `docs/emmc-n10.11-bounded-read-sweep-design-2026-11-09.md`
- `docs/emmc-driver.md`
- `docs/evidence-baseline.md`
- `package/Gta4lEmmcBlock.c`
- `package/Gta4lEmmcBlock.cpp`
- `package/Gta4lEmmcBlock.h`
- `package/Gta4lEmmcBlockCore.h`
- `package/Gta4lEmmc.c`
- `package/Gta4lEmmc.h`
- `scripts/test-gta4l-emmc-block.py`
- `scripts/sim-gta4l-emmc-block/`
- `porting.toml`
