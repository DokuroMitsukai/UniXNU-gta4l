# gta4l N10.11 bounded read-sweep design (host-only, 2026-11-09)

**Status:** N10.11 host-only design. This batch performed no device access,
boot, flash, candidate build, driver change, QEMU run, commit or push. It
proposes a bounded, runtime-derived read-only sweep for owner review. It does
not authorize implementation, candidate production, a device run, a commit or
a push. The implementation batch (working label N10.12), candidate production
(working label N10.13) and any hardware run (working label N10.14) are
separately gated; the plan assigns the final labels. The corrected
post-N10.10 roadmap is the authority for sequencing; this document is its M1
feasibility/design output.

## 1. Why this rung, and what N10.10 does not establish

N10.10 recorded one owner-reported cold boot in which four pinned
boot-partition ranges passed through the real
`IOMedia`/`IOBlockStorageDriver`/`IOBreaker`/`IODeblocker` path
(`cases=4 passed=4 failed=0 reason=0 complete=1`, 49 observed subrequests,
maximum eight blocks, zero foreign traffic). The four cases were fixed at
design time: an aligned direct control, two aligned splits, and one unaligned
deblocker split. That result qualifies those four ranges and the real family
path on that boot. It does not qualify every offset, every length, every
descriptor layout, every allocation, or I/O chosen by another client of the
storage stack.

The next bounded question is:

> Can the driver select a small, deterministic set of read windows at runtime
> from the published boot-partition geometry, read each window through the
> real media path, and obtain byte-for-byte agreement with an independent
> same-boot direct CMD17 reference read, while the observed driver-level
> decomposition matches the declared 8-block / 4,096-byte limit?

This is a consistency test between two paths in the same boot, not proof for
all LBAs. The reference and media paths share controller and addressing code;
their agreement is evidence that the media path delivered what the transport
path read, not independent ground truth. The sweep is also not a mount test,
a write test, a wider/faster-mode test, or an initialization-reliability
measurement.

## 2. Pinned facts and constraints

### 2.1 N10.7/N10.8/N10.9/N10.10 contract

- The breakup mode advertises eight read blocks, 4,096 read bytes and three
  segments (`package/Gta4lEmmcBlock.h:162-170`;
  `package/Gta4lEmmcBlock.cpp:961-981`). The adapter's own accepted maximum
  remains 64 blocks (`package/Gta4lEmmcBlockCore.h:22-24`); the
  `IOBlockStorageDriver` splits before the adapter.
- The breakup report thread builds the caller descriptor and sets its non-zero
  case tag first, then arms a per-case active window under `reportLock`, calls
  `media->read()`, disarms the window, and never holds the controller `gate`
  across the media call (`package/Gta4lEmmcBlock.cpp:3401-3434`). The
  observer is defined at `package/Gta4lEmmcBlock.cpp:2425-2496` and is called
  at `:1095`, before the adapter takes the gate at `:1132`.
- The active window uses absolute block numbers:
  `firstBlock = (mediaBase + start) / 512`
  (`package/Gta4lEmmcBlock.cpp:3408-3410`). The report prints the
  partition-relative `start` in bytes.
- The observer attributes a tagged request to the active case, treats a tag-0
  request inside the active span as a case subrequest only when it matches the
  next expected range, counts a tag-0 request outside the span as `foreign`,
  and records an out-of-order/overlapping/oversized request as `order`
  (`package/Gta4lEmmcBlock.cpp:2442-2494`). The direct aligned path preserves
  the caller tag; the deblocker path does not.
- The breakup complete guard requires `cases != 0`, `passed == cases`,
  `failed == 0`, `reason == 0`, `cancel == 0`, `budget == 0`, `media == 0`,
  `part == 0`, `foreign == 0`, `limit == 8` and `segments == 3`
  (`package/Gta4lEmmcBlock.cpp:3559-3572`).
- The breakup report formatter emits one `BREAKUP` summary line and one
  `BREAKUPREAD` line, each conditionally on the mode
  (`package/Gta4lEmmcBlock.c:1773-1901`). The report cap is 2,048 bytes
  before NUL termination (`package/Gta4lEmmcBlock.h:182`); the formatter
  fails closed and the report path panics if the writer cannot fit
  (`package/Gta4lEmmcBlock.cpp:3893-3898`, `:4428-4434`).
- The breakup worst-case arithmetic is media wait 20,000 ms + read budget
  6,000 ms + one nominal in-flight 1,000 ms read + partition deadline
  2,000 ms = 29,000 ms, below the 30,000 ms watchdog
  (`package/Gta4lEmmcBlock.h:175-178`; `package/Gta4lEmmcBlock.cpp:55-102`).
- The boot partition is resolved by the pinned base
  `GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES` and size
  `GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES`
  (`package/Gta4lEmmcBlock.cpp:3336-3355`; constants in
  `package/Gta4lEmmcBlock.h`). On the current device the base is 2,255,360
  blocks (1,154,744,320 bytes) and the size is 196,608 blocks
  (100,663,296 bytes). The current device capacity is 61,071,360 blocks.

### 2.2 Corrected roadmap constraints

- N10.10 does not qualify every offset, buffer layout or physical segment
  pattern. Smaller segment-derived breaks, descriptor failures, allocation
  failures and cancellation remain relevant. A new mode must explicitly
  implement and test its chosen limits.
- The sweep is a finite consistency test, not arbitrary-I/O safety. Roadmap
  §9 decision 4 (the approval must cover arbitrary read-only LBAs) describes
  the scope a real storage stack would need; it does not make this sweep an
  arbitrary-I/O qualification.
- The design must settle bounds, overflow, end boundaries, sensitive-content
  exclusion, reference/media ownership, timing, first-failure diagnostics,
  report budget and host mutation controls before implementation approval.
- The boot partition is the only media read. Whole-media scattered reads can
  encounter private data even without writing; this design excludes them by
  default.

## 3. Scope decision: aligned-only, boot-partition-only, six cases

### 3.1 Why aligned only

The runtime cases are 512-byte aligned and their starts are 8-block aligned.
Every requested length is a multiple of 512 bytes. With a page-aligned bounce
buffer, the aligned descriptor's first physical segment is at least one page,
so the 4,096-byte cap forces the split deterministically
(`docs/emmc-n10.7-breakup-qualification-design-2026-11-09.md` §3.1). The
expected decomposition is therefore the simple, layout-independent formula
`subrequests = ceil(blocks / 8)`, `max = min(8, blocks)`, with contiguous
8-block stages and one partial final stage.

Unaligned cases are excluded. The deblocker path loses the caller tag, adds
head/tail scratch reads and rounding, and its observed decomposition depends
on the physical segment scan; N10.10 already qualified the pinned unaligned
case. A future unaligned sweep must design its own scratch attribution, its
own expected decomposition and a three-segment advertisement. This design
does not claim the three-segment envelope.

### 3.2 Why one segment

Because every case is aligned and the bounce buffer is page-aligned, the
legacy segment count of 1 is sufficient; the N10.7 host-model `seg=1` control
showed the aligned cases pass with one segment (no hardware boot has run a
one-segment advertisement; N10.10 ran `seg=3`). The sweep therefore advertises
`8 / 4,096 / 1` and requires `seg=1` in its complete guard. It does not
reduce the three-segment breakup/deblock modes, and it does not raise any
production limit. A future unaligned extension must advertise and test three
segments separately.

### 3.3 Why the boot partition only

The sweep reads only the boot partition at the pinned base and size. It does
not read the whole media's other partitions, the GPT, user data, or any
unmapped region. It validates the same whole-media profile as N10.7: exactly
one whole media, base 0, size equal to the reported capacity, whole-media
flag set, content `GUID_partition_scheme`, and `writable=0`; it resolves
exactly one boot partition at the pinned base/size and requires its media to
report non-writable. A profile or writability mismatch is reason 1 or 2 and
stops the run before any case read. The boot partition contains the project's
public diagnostic image. The report prints only bounded counters, offsets,
lengths, statuses and truncated digests; it never prints raw sectors, GUIDs,
labels, serials or identifiers. The owner approval for any hardware run must confirm that the
boot partition content is the project's public diagnostic image; if it is
not, the run is not approved. Raw evidence remains in the gitignored
`out/gta4l/...` tree, and tracked documents use truncated hashes only.

## 4. Mode selection and advertisement

The mode is selected by exactly one `gta4l-emmc-sweep=1` token alongside
exactly one `gta4l-emmc-block=1` or `=3` token, following the N10.7 selector
rules (`docs/emmc-n10.7-breakup-qualification-design-2026-11-09.md` §4):

- The token requires exactly one block token; an orphan token without the
  block mode is refused, not inert.
- A duplicate `gta4l-emmc-sweep` token, a non-1 or malformed value, or any
  combination with `gta4l-emmc-partobs`, `gta4l-emmc-partcmp`,
  `gta4l-emmc-partread`, `gta4l-emmc-deblock` or `gta4l-emmc-breakup` is
  refused.
- The absent-token legacy, partobs, partcmp, partread, deblock and breakup
  reports remain byte-for-byte unchanged.
- The transport policy is unchanged: 1-bit SDR, 400 kHz, polling PIO, the
  existing CMD17 / two-sector CMD18 / CMD12 policy, read-only publication, no
  retries and no fallback.

The sweep advertises `kIOMaximumBlockCountReadKey = 8`,
`kIOMaximumByteCountReadKey = 4,096` and
`kIOMaximumSegmentCountReadKey = 1`. The other modes keep their existing
values (legacy/partobs/partcmp/partread 64 / 32,768 / 1; deblock
64 / 32,768 / 3; breakup 8 / 4,096 / 3). The advertisement is mode-scoped and
boot-wide, exactly as N10.7 §2.2 describes; it is the declared mechanism under
test, not a production-limit recommendation.

The adapter's `doAsyncReadWrite()` data path, `copyOutCallback()` and the
core chunk loop are unchanged. The sweep adds one entry-side observer call at
the top of `doAsyncReadWrite()`, before the adapter takes the gate; it
observes only and never alters the data path.

## 5. Case table and runtime derivation

### 5.1 Constants

| constant | value | meaning |
|---|---:|---|
| `SWEEP_CASES` | 6 | two pinned anchors plus four runtime windows |
| `SWEEP_RUNTIME_CASES` | 4 | runtime-derived windows |
| `SWEEP_MAX_BLOCKS` | 32 | largest case block count |
| `SWEEP_MAX_BYTES` | 16,384 | largest case byte count |
| `SWEEP_READ_BLOCKS` | 8 | mode-scoped read block limit |
| `SWEEP_READ_MAX_BYTES` | 4,096 | mode-scoped read byte limit |
| `SWEEP_SEGMENTS` | 1 | mode-scoped segment count |
| `SWEEP_SECTOR_BYTES` | 512 | media block size |
| `SWEEP_READ_BUDGET_MS` | 4,000 | case-loop elapsed budget, checked between cases |
| `SWEEP_DEADLINE_MS` | 2,000 | boot-partition discovery deadline |
| `SWEEP_STEP_MS` | 50 | discovery poll step |
| `SWEEP_MAX_CASE_MS` | 1,700 | one worst-case reference + media case |
| `SWEEP_WORST_MS` | 27,700 | media wait + discovery deadline + budget + max case |
| `SWEEP_LINE_BYTES` | 384 | summary line cap |
| `SWEEPREAD_LINE_BYTES` | 352 | detail line cap |
| `SWEEP_DERIVE_MULTIPLIER` | 104,729 | runtime start recipe multiplier |

### 5.2 Pinned anchors

The two anchors are fixed boot-partition ranges:

| index | kind | start (bytes) | length (bytes) | span | blocks | subrequests | max | expected content |
|---:|---|---:|---:|---:|---:|---:|---:|---|
| 0 | anchor A0 | 4,096 | 4,096 | 4,096 | 8 | 1 | 8 | pinned SHA-256 prefix `5878bcf8a6afe4dd79a002caa8b6aeea` (N10.7 case 0) |
| 1 | anchor A1 | 4,096 | 16,384 | 16,384 | 32 | 4 | 8 | re-derived at implementation from the gate-pinned v5 BOOT image kernel section; the design does not invent it |

A0 is the known-answer direct control. A1 is the known-answer split anchor;
its range is the first 16,384 bytes of the N10.7 case 1 range. The
implementation batch must re-derive A1's full SHA-256 from the retained
gate-pinned v5 BOOT image, pin the first 16 bytes in the case table, and
verify both anchors on the host before the implementation batch closes. A
candidate batch must re-verify the anchor digests against the exact BOOT image
it builds; a mismatch blocks the candidate, not a hardware run.

### 5.3 Runtime windows

Let `P` be the discovered boot-partition block count. The implementation must
validate `P == GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES / 512` (196,608
blocks on the current device) and refuse with reason 2 if it differs.
Production therefore never derives starts for another geometry; only the
pure derivation function is exercised with a different `P` in the host test.
Let `W` be the case's block count. For runtime case `i` (0-based), compute:

```text
start_block = ((i + 1) * 104729) % ((P - W) / 8) * 8
start_byte  = start_block * 512
```

All arithmetic is unsigned 64-bit. Before the first reference read the
implementation validates the whole six-entry table — both anchors and all
four runtime-derived entries: `W > 0`, `W <= SWEEP_MAX_BLOCKS`, `W % 8 == 0`,
`P % 8 == 0`, `P >= W`, `(P - W) / 8 != 0`, `start_block + W <= P`, and no
64-bit overflow in `start_byte + length` or `boot_base + start_block + W`.
An invalid entry is reason 10 and stops the run before any case I/O. The
per-case step in section 6.1 re-checks the same conditions defensively; a
failure there is also reason 10.

The four runtime windows are:

| index | name | `W` blocks | `W` bytes | expected subrequests | expected max |
|---:|---|---:|---:|---:|---:|
| 2 | R0 | 32 | 16,384 | 4 | 8 |
| 3 | R1 | 16 | 8,192 | 2 | 8 |
| 4 | R2 | 8 | 4,096 | 1 | 8 |
| 5 | R3 | 8 | 4,096 | 1 | 8 |

For the current `P = 196,608` blocks, the recipe yields:

| index | name | start block | start byte | end block |
|---:|---|---:|---:|---:|
| 2 | R0 | 51,528 | 26,382,336 | 51,560 |
| 3 | R1 | 102,928 | 52,699,136 | 102,944 |
| 4 | R2 | 154,296 | 78,999,552 | 154,304 |
| 5 | R3 | 9,128 | 4,673,536 | 9,136 |

These starts are runtime-computed, not a compile-time pinned table. The host
tests pin the expected starts for the current `P` and exercise the pure
derivation with a different `P` to prove the starts change; production still
refuses any `P` other than the pinned boot-partition size (reason 2), so a
different geometry cannot silently produce a different sweep. Runtime windows
may overlap each other or the anchors; each case is independently armed and
observed, and overlap is allowed.

### 5.4 Expected full-pass aggregate

The six cases total:

- requested bytes: `4,096 + 16,384 + 16,384 + 8,192 + 4,096 + 4,096 =
  53,248`;
- aligned span: `53,248` bytes (every case is aligned);
- blocks: `53,248 / 512 = 104`;
- subrequests: `1 + 4 + 4 + 2 + 1 + 1 = 13`;
- maximum observed subrequest: `8`;
- reference bytes: `53,248` (the aligned cases need no padding);
- case reads: `6`; successful reference reads: `6`.

A later table change requires a design update, not a silent counter change.

## 6. Reference/media double-path protocol

### 6.1 Per-case order

Before the case loop, the report thread resolves and validates the whole
media and the boot partition (the N10.7 profile checks) within the media-wait
and `SWEEP_DEADLINE_MS` bounds, validates the whole six-entry table (section
5.3), and starts the case-loop elapsed counter. The budget therefore covers
the case reads, not discovery.

For each case, the report thread performs these steps in order:

1. Check `reportCancel` and the case-loop elapsed budget
   (`SWEEP_READ_BUDGET_MS`, checked between cases with the inherited
   elapsed-counter mechanism, `package/Gta4lEmmcBlock.cpp:2354-2367`).
   Cancellation latches reason 9 and stops; budget exhaustion latches reason
   8 and stops. Neither is an in-flight cancellation mechanism.
2. Re-validate the case geometry (section 5.3; the whole table was validated
   before the first reference read). An invalid entry is reason 10 and stops
   the run before any I/O for that case.
3. Take the controller `gate`, read the case's `blocks` blocks from absolute
   LBA `boot_base_block + start_block` with the direct CMD17 helper into the
   reference buffer, and release the gate. The loop stops at the first
   transport failure; a failure latches reason 15 and stops the run, with no
   media read for that case or any later case. The gate is never held across
   `media->read()`.
4. Build a page-aligned `IOMemoryDescriptor::withAddress()` over the sweep
   bounce buffer with exactly `length` bytes and set the case tag. If the
   descriptor cannot be built, latch reason 4 and continue with the next case;
   nothing is armed yet, so no stale window can survive the failure.
5. Arm the sweep observer under `reportLock`: active case index, non-zero
   case tag (`index + 1`), absolute first block, next block, end block,
   expected subrequest count, expected maximum, and zeroed observed counters.
   Then call `media->read(this, start_byte, descriptor, nullptr, &actual)`;
   under `reportLock`, disarm the observer and snapshot the observed
   counters; release the descriptor. The active window must never remain
   armed past the case.
6. Compare in this order: status (`kIOReturnSuccess`), `actual == length`,
   reference/media byte equality (`memcmp`), anchor digest equality for A0/A1,
   and the observed decomposition. The first failure latches the case's
   reason, status, actual, observed counters, reference status/bytes,
   mismatch offset and digests. Later cases continue after a read, short,
   mismatch (reason 14), descriptor, split or order failure; a media
   discovery/profile, partition, open, budget, cancel, reference-transport or
   anchor-mismatch failure stops the run.

### 6.2 Reference buffer and media buffer ownership

- One page-aligned reference buffer of `SWEEP_MAX_BYTES` (16,384 bytes) and
  one page-aligned sweep buffer of `SWEEP_MAX_BYTES` (16,384 bytes) are
  allocated only in sweep mode and freed on every exit path with
  `IOFreeAligned()`. If either `IOMallocAligned()` fails, sweep-mode driver
  start is refused through the existing allocation-failure path
  (`package/Gta4lEmmcBlock.cpp:861-893`) before any report thread runs; the
  mode never operates with one buffer or a smaller buffer. This startup
  refusal is not a case reason code; reason 4 is reserved for the per-case
  descriptor.
- The reference buffer is filled only by the direct CMD17 helper under the
  controller gate. The media buffer is filled only by the media-path read
  through the adapter's `copyOutCallback()`. The two buffers never alias.
- The reference helper reads blocks from the absolute boot-partition LBA; the
  media read uses the partition-relative start. Both addresses derive from
  the same validated geometry.
- The reference read is a direct transport call and does not enter
  `doAsyncReadWrite()`, so the observer never sees it. It is performed before
  the case window is armed. The media read is inside the active window and is
  attributed by tag and range.

### 6.3 Content comparison and diagnostics

- The verdict comparison is `memcmp(reference, media, length)`. On mismatch,
  the first differing byte offset is recorded and the case latches reason 14.
  The report prints `mvalid=1` and the offset; on success it prints `mvalid=0
  mismatch=0`.
- The report computes the SHA-256 of the media bytes and the reference bytes
  for the `SWEEPREAD` line. The two digests are printed as 16-byte prefixes.
  A mismatch is diagnosed by the offset and the two prefixes, not by raw
  bytes.
- The anchor cases additionally compare the media digest to the pinned anchor
  digest. A mismatch latches reason 7 and stops the run, because the boot
  image is not the expected candidate. The reference/media comparison is
  performed first; if both fail, the first latched reason is 14.
- The comparison is a same-boot consistency check. The reference and media
  paths share controller, addressing and CMD17/CMD18 code; agreement is not
  independent ground truth. The pinned anchors are the known-answer positive
  controls.

## 7. Per-case observability contract

### 7.1 Ownership and classification

- The report thread issues one `IOMedia::read()` at a time and blocks in the
  synchronous `IOStorage::read()` wrapper. It builds and tags the descriptor
  first, arms the active state under `reportLock` immediately before the
  call, disarms it immediately after the call returns, and releases the
  descriptor. It never holds `reportLock` across the media call, and the
  descriptor failure path runs before the arm, so no window is left active.
- The observer runs at the top of `doAsyncReadWrite()` after the null/zero
  argument checks and before the adapter takes the controller `gate`. It
  takes `reportLock` briefly, classifies the request, updates the observed
  counters and releases the lock. It does not take `gate`, retain the buffer
  or perform I/O.
- A request with the active tag is a case subrequest. A tag-0 request inside
  the active span is a case subrequest only if it matches the next expected
  range; otherwise it is an order error. A tag-0 request outside the active
  span is `foreign`. A request while no case is active is ignored by the
  observer; the whole-boot counters continue to count it.
- An oversized subrequest (`nblks > 8`), an out-of-order or overlapping
  subrequest, or a subrequest ending beyond the active span is an order
  error. The observed `nblks` is still recorded so the report shows the
  oversized request.

### 7.2 Case verdict

After the media read returns, the report thread compares observed against
expected:

- observed subrequest count equals `ceil(blocks / 8)`;
- observed block total equals `blocks`;
- observed maximum equals `min(8, blocks)`;
- observed next block equals the case's first block plus `blocks`;
- no order error and no oversized subrequest were recorded;
- the status is `kIOReturnSuccess`, `actual == length`, the reference/media
  bytes are equal, and the anchor digest matches for A0/A1.

A count/total/max mismatch is reason 11 (`SPLIT`); an out-of-order,
overlapping or oversized request is reason 12 (`ORDER`). A status, actual or
content failure uses reason 5, 6, 7, 14 or 15 as defined in section 8.3.

### 7.3 First-failure latch, continue and stop

- The first failing case latches its index, reason, status, actual, observed
  subrequests/blocks/max, reference status/bytes, mismatch offset and media/
  reference digests. Later cases continue after a read, short, mismatch
  (reason 14), descriptor, split or order failure so the counters attribute
  every case.
- A media discovery/profile, partition, open, budget, cancel,
  reference-transport or anchor-mismatch failure stops the run immediately;
  no retry, fallback, reset, repair or write follows any failure.
- `foreign` traffic is counted without stopping the run; the complete guard
  fails closed if it is nonzero.
- Counters saturate at their unsigned maximum rather than wrapping.

### 7.4 Complete guard

`complete = 1` only when all of these hold:

```text
cases == 6
passed == cases
failed == 0
reason == 0
reads == cases
refs == cases
refbytes == bytes
span == bytes
blocks == bytes / 512
subrequests == 13
max == 8
foreign == 0
fails == 0
cancel == 0
budget == 0
media == 0
part == 0
anchor == 1
mismatches == 0
limit == 8
seg == 1
```

`anchor == 1` means both pinned anchors matched. `mismatches == 0` means no
reference/media mismatch occurred. `subrequests == 13` is the expected total
for the current six-case table; the per-case decomposition checks enforce the
same contract.

### 7.5 What the sweep proves and does not prove

It proves, for the six selected ranges on one boot: the driver computed the
runtime starts from the published boot-partition geometry; the media path
delivered the exact requested bytes; the direct CMD17 reference path delivered
the same bytes; the observed driver-level decomposition matched the declared
8-block / 4,096-byte / one-segment limit; the pinned anchors matched their
known-answer digests; and the first failure, if any, is identified by case,
reason, status, actual, reference status, observed decomposition and digests.

It does not prove: all LBAs, all lengths, arbitrary offsets or descriptors,
the unaligned/deblocker path, physical segment patterns inside the driver's
scan, I/O chosen by another storage-stack client, mount, root integration,
writes, wider/faster modes, DMA/CQE/ICE, or initialization reliability. It
does not upgrade the host model's status. The hardware run is the only
qualification of the real layers.

## 8. Report lines and budget

### 8.1 Lines

The sweep mode adds exactly two conditional lines to the existing report,
following the breakup formatter pattern:

```text
GTA4L-EMMC P5 SWEEP: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6 bytes=53248 span=53248 blocks=104 subrequests=13 max=8 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 limit=8 seg=1 cap=61071360
GTA4L-EMMC P5 SWEEPREAD: index=5 target=1 start=4673536 length=4096 span=4096 actual=4096 status=0 subrequests=1 blocks=8 max=8 ref=1 refbytes=4096 refstatus=0 mvalid=0 mismatch=0 digest=<32 hex> refdigest=<32 hex>
```

`SWEEP` fields:

- `cases`, `passed`, `failed`: fixed table size and case outcomes.
- `reason`: first failure code; 0 on full pass.
- `complete`: the fail-closed guard above.
- `reads`: attempted case reads; `bytes`: requested bytes across attempted
  cases; `span`: aligned bytes across attempted cases; `blocks`: `span / 512`.
- `subrequests`: observed driver-level subrequests across attempted cases;
  `max`: maximum observed subrequest block count; `foreign`: requests during
  an active case that were not attributed to it.
- `fails`: transport/copy failures; `cancel`, `budget`, `media`, `part`:
  stop flags for cancellation, the case-loop budget, media discovery/profile
  and partition discovery.
- `refs`: successful reference reads; `refbytes`: reference bytes read.
- `anchor`: 1 only when both pinned anchors matched; `mismatches`: cases with
  a reference/media content mismatch.
- `limit`: advertised read block limit (8); `seg`: advertised segment count
  (1); `cap`: `capacityBlocks`.

`SWEEPREAD` prints the first failing case when `failed != 0`, otherwise the
last attempted case. It prints the case index, target (1 = boot), requested
start/length, aligned span, actual bytes, raw unsigned 32-bit status (the
existing formatter convention, `package/Gta4lEmmcBlock.c:1885-1887`),
observed subrequest count/blocks/max, reference success/bytes/status, mismatch
validity and offset, and the media/reference digest prefixes. A failed case's digests
are zero when the read or reference failed before hashing. `SWEEPREAD` is
always emitted in sweep mode, zero-filled when no case was attempted, so the
capture has a fixed line set.

### 8.2 Budget

- `SWEEP_LINE_BYTES = 384` and `SWEEPREAD_LINE_BYTES = 352`.
- The existing formatter's 2,048-byte cap and fail-closed behavior are
  unchanged. The base report plus the two new lines must stay below 2,048
  bytes before NUL termination with the maximum field widths this device can
  produce (`cap=61071360`, `start` up to the boot-partition byte size,
  `length` up to 16,384, two 16-byte digest prefixes).
- The host tests render the worst-case field widths, assert each line cap,
  assert the whole report is below 2,048, and assert that a deliberately
  small buffer returns 0 and takes the existing format-failure path.
- No raw sectors, GUIDs, labels, serials or identifiers are printed.

### 8.3 Reason codes

The sweep reuses the N10.7/N10.9 mapping for 0–13 and adds two mode-scoped
codes:

| code | meaning |
|---:|---|
| 0 | no failure |
| 1 | media (whole-media discovery/profile) |
| 2 | partition (boot-partition discovery/geometry) |
| 3 | open (media open refused) |
| 4 | descriptor (descriptor allocation failure) |
| 5 | read (media-path read returned non-success) |
| 6 | short (media-path actual != length) |
| 7 | hash (pinned anchor digest mismatch) |
| 8 | budget (case-loop elapsed budget exceeded) |
| 9 | cancel (`reportCancel`) |
| 10 | argument (invalid case entry/geometry/overflow) |
| 11 | split (observed subrequest count/blocks/max mismatch) |
| 12 | order (out-of-order/overlapping/oversized subrequest) |
| 13 | foreign (unattributed request during an active case) |
| 14 | mismatch (reference/media content mismatch) |
| 15 | ref (reference transport failure or short reference read) |

The existing mode reports are unchanged; codes 14 and 15 appear only in
sweep mode.

## 9. Timing, watchdog, cancellation and terminal ownership

- `SWEEP_WORST_MS = 20,000 + 2,000 + 4,000 + 1,700 = 27,700 ms`, below the
  30,000 ms watchdog (media wait + boot-partition discovery deadline +
  case-loop budget + one overrunning case). The static assert must be added
  next to the existing breakup/partread/deblock asserts
  (`package/Gta4lEmmcBlock.cpp:55-102`).
- The 1,700 ms one-case bound is the larger of two paths: a reference
  failure (up to about 1,330 ms for 31 successful CMD17 reads plus one
  1,000 ms timeout, with no media read), or a successful reference read
  (about 340 ms) followed by a media read with one timed-out subrequest
  (about 1,250 ms), plus scheduling overhead. The reference failure stops
  before the media read; the two worst cases are not additive. The
  implementation batch must re-derive these components from the actual
  command overhead (each 8-block subrequest is 81.92 ms of data at 400 kHz,
  plus command overhead; `GTA4L_EMMC_DATA_TIMEOUT_US` is 1,000,000,
  `package/Gta4lEmmc.h:234`) and record the arithmetic; if the derived bound
  exceeds 1,700 ms, adjust the table or the bound so `SWEEP_WORST_MS` stays
  below the watchdog.
- The total data volume is 53,248 requested bytes, 53,248 reference bytes and
  53,248 media-path bytes. At 400 kHz 1-bit SDR this is about 2.2 s plus
  command overhead. `SWEEP_READ_BUDGET_MS` is a monotonic case-loop budget
  checked before each case with the inherited elapsed-counter mechanism
  (`package/Gta4lEmmcBlock.cpp:2354-2367`), not a per-case or per-gap
  deadline. The sweep starts the counter after whole-media and boot-partition
  discovery, so the 4,000 ms budget covers the 2.2 s transfer plus command
  overhead and has about 1.8 s of headroom. The hardware run must report
  `budget=0` on a full pass.
- The reference read holds the controller gate for its bounded loop; the
  report thread never holds the gate across `media->read()` and never sleeps
  while holding it. The media read's adapter calls hold the gate for at most
  one 8-block subrequest or one command timeout.
- The synchronous family read has no timeout of its own; the per-command
  transport timeout and the 30,000 ms watchdog remain the terminal bounds.
  The case-loop budget check is not an in-flight cancellation mechanism.
  No timeout is extended silently; if the bounds do not fit, reduce the case
  table or stop the design.
- On cancellation, set reason 9, `cancel=1`, `complete=0`, clear the active
  case, release the media, and return through the existing no-panic
  cancellation path. The terminal-ownership rule (`reportLock`,
  `claimTerminal()`) is unchanged; the report thread or the watchdog emits the
  single final panic. A late subrequest after the watchdog claim cannot alter
  the emitted report.
- Release the boot and whole media on every exit path.

## 10. Host-test plan (N10.12 acceptance)

The existing host seam (`scripts/sim-gta4l-emmc-block/`) already calls
`doAsyncReadWrite()` directly and reconstructs the deblocker and the
`getBreakSize()` split from the advertised properties. The implementation
batch extends it with the sweep table, the runtime derivation, the reference/
media comparison and the two new lines. The suite must cover at least:

1. **Full pass at 8 / 4,096 / 1:** exact `cases=6 passed=6 failed=0 reason=0
   complete=1 reads=6 bytes=53248 span=53248 blocks=104 subrequests=13 max=8
   foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 refs=6 refbytes=53248
   anchor=1 mismatches=0 limit=8 seg=1`; the expected starts from section 5.3;
   the A0/A1 anchor digests; reference/media equality; the last-case
   `SWEEPREAD` line.
2. **Production-limit control at 64 / 32,768 / 1:** the same table at the
   production limit. The observer expects 8-block subrequests but sees
   64-block requests; the run must fail with `SPLIT`/`ORDER` and
   `complete=0`. This proves the reduced limit is the split mechanism.
3. **Segment-count control:** the mode advertises 1; a mutation to 3 must fail
   the complete guard (`seg=3` != 1) even if the reads pass. This proves the
   declared advertisement is part of the contract.
4. **Runtime derivation controls:** for `P=196,608`, the expected starts are
   pinned; a different `P` changes them; a hard-coded start table that ignores
   `P` must be rejected. Also reject `P` not a multiple of 8, `W=0`,
   `W > SWEEP_MAX_BLOCKS`, `W` not a multiple of 8, `P < W`, zero modulus,
   `start + W > P`, and 64-bit overflow in the byte/LBA arithmetic.
5. **Bounds and target controls:** start outside the boot partition, length
   not a multiple of 512, unaligned start, wrong target, zero length, and an
   absolute LBA beyond the device capacity. Each is reason 10 (or reason 2 for
   a partition mismatch) and fails closed.
6. **Reference controls:** reference transport failure at each case position
   (reason 15, stop), short reference read (reason 15, stop), reference from
   the wrong LBA or base, reference shifted by one byte, and a stale
   reference buffer. The wrong/shifted/stale cases must produce reason 14
   with the first mismatch offset; the transport/short cases must stop with
   reason 15 and no media read.
7. **Media controls:** media-path read error (reason 5, continue), short
   actual (reason 6, continue), descriptor allocation failure (reason 4,
   continue), wrong actual length, stale media buffer and shifted media copy
   (reason 14). The observed counters must remain attributable.
8. **Decomposition controls:** missing, extra, reordered, overlapping and
   oversized subrequests; each rejected with reason 11 or 12; the observed
   values are visible.
9. **Foreign traffic:** tag-0 request inside and outside the active span;
   in-span out-of-order is `ORDER`; out-of-span increments `foreign` and
   fails the guard; the case's own subrequests remain attributed.
10. **Anchor controls:** A0 digest mismatch and A1 digest mismatch (reason 7,
    stop); missing or placeholder A1 digest rejected by the host suite; a
    candidate image whose kernel section does not match the pinned anchor
    blocks the candidate.
11. **Budget and cancellation:** budget exhaustion before a case (reason 8,
    stop); cancellation before media wait, between cases and after a read
    (reason 9, stop); no panic claim.
12. **Media/partition controls:** media absent, two whole media, wrong
    profile, boot partition absent/ambiguous, wrong base/size, open refused;
    each expected flag and no panic.
13. **Watchdog race:** the watchdog claims terminal while the report thread is
    blocked in a reference or media read; the report thread must not panic
    twice or overwrite the watchdog report; a late subrequest after the claim
    must not alter the emitted report.
14. **Concurrency and cleanup:** a second client issues a read during an
    active case (tagged or tag-0); the observer attributes or counts it
    without deadlock; a second request with the same active tag is an order or
    foreign case; the report thread and the second client do not corrupt the
    counters. Every failure path releases the whole media, the boot
    partition, the descriptors and both buffers exactly once; no double
    release, no leak, no panic after the terminal claim.
15. **Selector controls:** orphan, duplicate, wrong value, and each
    partobs/partcmp/partread/deblock/breakup combination refused; the
    absent-token legacy report is byte-identical; the accepted
    `emmc-block-1`/`emmc-block-3` payloads are unchanged.
16. **Property controls:** legacy/partobs/partcmp/partread 64 / 32,768 / 1;
    deblock 64 / 32,768 / 3; breakup 8 / 4,096 / 3; sweep 8 / 4,096 / 1; the
    non-sweep reports are unchanged.
17. **Report controls:** worst-case field widths fit 384/352; the whole
    report is below 2,048; a small buffer returns 0 and takes the existing
    format-failure path; the expected full-pass lines match; no raw identifier
    appears.
18. **Mutation controls:** accept a short reference, accept a short media
    read, skip the content compare, skip the anchor compare, skip the
    budget/cancel check, force `complete=1`, hard-code the starts, use the
    caller's buffer instead of the page-aligned bounce, skip the completion
    wait, skip the decomposition check, advertise three segments, accept a
    wrong LBA/base, accept stale/shifted data. Each must be rejected.

The implementation batch must keep the existing suites green:
`scripts/test-gta4l-emmc-block.py`, the evidence-count scripts, the probe
suite, the payload/package/boot-image/storage-ABI/sdhci evidence counts, the
RAM-root fixture recipe, the board check, the TOML check, the porting-record
check and `git diff --check`. The design batch itself adds no code and does
not change those counts.

## 11. Implementation surface and approval gates

### 11.1 Implementation surface (N10.12, separately approved)

- `package/Gta4lEmmcBlock.h`: sweep token/selector constants, case/table
  constants, reason codes 14/15, report fields, line caps, static asserts for
  the 2,048-byte report, the 16,384-byte buffers, the 27,700 ms worst case and
  the object size.
- `package/Gta4lEmmcBlock.c`: selector parse/conflict rules, the six-case
  table, the runtime derivation and validation, the `SWEEP`/`SWEEPREAD`
  formatter, and the anchor digest constants.
- `package/Gta4lEmmcBlock.cpp`: opt-in parse, the mode-scoped 8/4,096/1
  advertisement, the two page-aligned buffers, boot-partition resolution, the
  direct CMD17 reference helper, the case loop, the sweep observer, the
  reference/media comparison, the first-failure/continue/stop rules, the
  cancellation/terminal handling and the report copy.
- `scripts/sim-gta4l-emmc-block/*`, `scripts/test-gta4l-emmc-block.py`,
  `scripts/build-gta4l-diagnostic-payload.py`,
  `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py`:
  the host model, the mutation controls and the report/payload string
  expectations.
- Documentation refresh in the implementation/candidate batches:
  `docs/emmc-driver.md` §5.3.85, `README.md`, `porting.toml`,
  `docs/evidence-baseline.md` (changed hash rows refreshed; the table stays
  at 45 rows) and `docs/diagnostic-payload-input-inventory.md`.
- No transport, width, timing, DMA/CQE/ICE, write, mount/root or production
  request-limit change. `doAsyncReadWrite()`'s data path, `copyOutCallback()`
  and the core chunk loop are unchanged; the sweep observer is a bounded
  entry-side observation only.

### 11.2 Gates

- **Design (this batch):** host-only. No implementation, candidate build,
  ABI/QEMU audit, device access, flash or boot. No commit or push.
- **Implementation (working label N10.12):** owner approval and Build mode.
  Host tests only; no candidate build, device access, flash or boot.
- **Candidate production (working label N10.13):** separately gated,
  host-only. Archive/ABI/kernel/payload/BOOT production, the storage-ABI,
  binding, payload, BOOT-gate and evidence-count checks, the A0/A1 anchor
  re-derivation against the exact BOOT image, and the privacy scan. No
  device.
- **Hardware run (working label N10.14):** fresh explicit BOOT-only
  approval. The approval must name: the boot-partition read-only ranges; the
  four runtime windows; the direct CMD17 reference reads; the one-boot
  budget; the pre-flash backup and post-flash readback; and the fact that the
  owner confirms the boot partition is the project's public diagnostic image.
  It must exclude retry, reflash, rollback, reset/repair, mount, root switch,
  write, wider/faster mode, DMA/CQE/ICE, other partitions and any production
  request-limit change. The one-boot budget is consumed by the run.
- **Closeout:** follows the N10.10 pattern: one owner-reported cold boot, the
  full P5 report captured, the report bound through the flash record, no
  post-boot readback unless separately approved, and a tracked hardware-result
  record. The closeout does not authorize another boot.

## 12. Limits and non-goals

- The sweep is a finite same-boot consistency test of six selected
  boot-partition ranges on one cold boot. It does not qualify arbitrary byte
  offsets or lengths, arbitrary I/O, filesystem mount, root integration,
  writes, partition semantics beyond the selected bytes, wider/faster modes,
  DMA/CQE/ICE or initialization reliability.
- The 8-block / 4,096-byte / one-segment advertisement is mode-scoped. It is
  not a production-limit recommendation, not a general request-limit increase,
  and not a change to the accepted N10.1/N10.2/N10.6/N10.10 images or
  counters. The legacy/partread/partcmp/partobs modes keep 64 / 32,768 / 1,
  the deblock mode keeps 64 / 32,768 / 3, and the breakup mode keeps
  8 / 4,096 / 3.
- The split is forced by the block-count and byte caps, not by incidental
  physical fragmentation. The design rejects whole-boot counter subtraction
  and does not use the N10.6 `max=24` observation or the N10.10 whole-boot
  counters as a per-case trace.
- The reference and media paths share controller/addressing code; agreement
  is consistency evidence, not independent ground truth. The host model is a
  reconstruction; the hardware run is the only qualification of the real
  `IOBlockStorageDriver`/`IOBreaker`/`IODeblocker` layers.
- The runtime windows may land in any part of the boot partition, including
  zero-filled or non-kernel content. The pinned anchors provide the
  known-answer content; the runtime windows test path consistency, not content
  diversity.
- A read error, short transfer, content mismatch, anchor mismatch or
  decomposition mismatch is a measurement failure of that case, not a card
  verdict. A cancellation, budget stop, foreign-traffic guard failure or
  absent media is a bounded observation limit, not a failure-rate estimate.
- No commit or push is part of this design batch.

## 13. Decision requested

The owner is asked to approve the following design direction, not to authorize
execution:

1. A boot-partition-only, aligned-only, six-case bounded sweep with two pinned
   anchors and four runtime-derived windows, under the 8-block / 4,096-byte /
   one-segment advertisement.
2. The runtime recipe in section 5.3 and the current-geometry expected starts,
   with `P` validated against the pinned boot-partition size.
3. The direct CMD17 reference / media-path double-path comparison, the
   first-mismatch diagnostics, the anchor known-answer checks and the
   fail-closed complete guard.
4. The `SWEEP`/`SWEEPREAD` report lines, reason codes 14/15 and the
   4,000 ms budget / 27,700 ms worst case.
5. The host-test plan in section 10 and the implementation surface in section
   11, with implementation, candidate production and hardware as separate
   gates.
6. The privacy scope: boot partition only; no other partition; truncated
   digests only; the owner confirms the boot partition is the project's public
   diagnostic image before any hardware approval.

If the owner wants unaligned coverage, a three-segment advertisement, a
larger sweep, a different media or a different budget, that is a design change
and must be resolved before implementation approval. This document authorizes
nothing by itself.
