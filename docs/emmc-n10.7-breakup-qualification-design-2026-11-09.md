# gta4l N10.7 deterministic IOBreaker breakup coverage design (host-only, 2026-11-09)

**Status:** N10.7 host-only design. This batch performed no device access, boot,
flash, candidate build, driver change, QEMU run, commit or push. It follows the
N10.6 eleven-case hardware result and preserves N10.1/N10.2, the N10.1 captured
repeatability series, N10.4/N10.5/N10.6 and the earlier N-series records. The
implementation batch (working label N10.8), candidate production (N10.9) and any
hardware run (N10.10) are separately gated; the plan assigns the final labels.
The N10.6 one-boot budget is consumed, and initialization reliability remains
`[U]`.

## 1. Why N10.6 does not deterministically qualify the breakup layer

N10.6 passed all eleven pinned deblock cases and the whole-boot transport
counters showed a largest subrequest of 24 blocks, exactly three 4 KiB physical
segments. That is aggregate, same-boot evidence that the real
`IOBlockStorageDriver::breakUpRequest()` / `IOBreaker` path was entered on that
boot. It is not a per-case trace:

- Every N10.6 aligned span is at most 64 blocks / 32,768 bytes, exactly the
  advertised production read limit. The split observed in N10.6 came from the
  mode-scoped `kIOMaximumSegmentCountReadKey = 3` fold over the allocation's
  incidental physical segmentation, not from a request that necessarily exceeds
  a byte or block limit.
- The 24-block maximum depends on the allocator returning three 4 KiB physical
  segments. A different allocation could return one contiguous segment and the
  same case would take the direct `executeRequest()` short-circuit. N10.3 said
  this explicitly: the table did not deterministically force the real
  `IOBreaker` split, and a deterministic breaker-split case was left to a
  future rung.
- The whole-boot `DRV`/`XFER` counters include every subrequest of every case
  plus the deblocker's scratch and rounding reads. Subtracting one whole-boot
  counter from another, or comparing a boot with a previous boot, is not a
  per-case trace and cannot attribute a particular subrequest to a particular
  case. The plan rejects that method, and this design does not use it.

N10.7 is the next read-only rung: a small fixed table in which a request
necessarily exceeds a mode-scoped per-subrequest limit, so the split size is
determined by the block/byte constraint rather than by physical adjacency, and
the adapter records the exact subrequest decomposition of each case. The mode
is not a general-I/O, mount, write, reliability or production-limit rung.

## 2. Pinned IOStorageFamily survey

All references are the pinned public feed
`out/feeds/IOStorageFamily-67f9003c/` and the pinned xnu feed
`out/feeds/xnu-b7545ced/`. The survey was read-only.

### 2.1 Call path

1. `IOMedia::read()` (`IOMedia.cpp:702`) rejects an inactive, unopened or
   unformatted media, a null or non-`In` buffer, a start beyond `_mediaSize`
   and a buffer that would cross `_mediaSize`. It then adds `_mediaBase`
   (`:760`) and calls `getProvider()->read()`. The partition-media base
   addition is part of the breakup path too.
2. The synchronous `IOStorage::read()` wrapper (`IOStorage.cpp:221-249`)
   builds an `IOStorageCompletion`, issues the asynchronous read and waits on
   `IOStorageSyncer::wait()` (`IOStorage.cpp:114-126`). That wait has no
   timeout; the controller's per-command timeout and the adapter's 30,000 ms
   watchdog are the existing bounds.
3. `IOBlockStorageDriver::prepareRequest()` (`:3424`) records the request and
   calls `deblockRequest()` (`:2536`) at `:3523`.
4. `deblockRequest()` builds an `IODeblocker` when either the start or the
   length is not block-aligned (`:2574`), then calls
   `deblockRequestCompletion(this, deblocker, kIOReturnSuccess, 0)` (`:2610`).
   An aligned request short-circuits directly to `breakUpRequest()`
   (`:2565-2568`).
5. `IODeblocker::initWithBlockSize()` (`:2037-2122`) computes
   `_excessCountStart = requestStart % blockSize` (`:2075`),
   `_excessCountFinal = blockSize - ((requestStart + requestCount) % blockSize)`
   when nonzero (`:2076-2077`), and
   `_requestIsOneBlock = (_excessCountStart + _requestCount <= blockSize)`
   (`:2079`). For a read the scratch buffer is
   `max(_excessCountStart, _excessCountFinal)` bytes (`:2087`), allocated with
   `IOBufferMemoryDescriptor::withCapacity()` (`:2115-2117`).
6. `IODeblocker::getNextStage()` (`:2257`) for `kIODirectionIn` (`:2277-2308`)
   sets `_byteStart = _requestStart - _excessCountStart` (`:2286`) and builds at
   most three chunks: the excess-start scratch (`:2288-2293`), the caller's
   request buffer (`:2296-2299`) and the excess-final scratch (`:2301-2307`).
   Both excess chunks map to scratch offset 0; for a read they are discarded,
   so the aliasing is harmless. The deblocker completion
   (`deblockRequestCompletion()`, `:2613-2692`) obtains or allocates the
   deblocker's thread callback (`:2630-2637`) and uses `thread_call_enter1()`
   for the next stage (`:2685`).
7. `IOBlockStorageDriver::breakUpRequest()` (`:3212-3333`) computes
   `breakSize = IOBreaker::getBreakSize(...)` from the advertised read limits
   (`:3247-3268`). If `breakSize == 0`, it completes with
   `kIOReturnDMAError` (`:3272-3275`) before `executeRequest()`. If
   `buffer->getLength() <= breakSize`, it calls `executeRequest()` directly
   (`:3281-3284`). Otherwise it builds an `IOBreaker` (`:3287-3330`) and starts
   the split at `:3332`.
8. `IOBreaker::getBreakSize()` (`:2816-2990`) folds the buffer's physical
   segments, constrains the block count at `:2848-2863`, scans at
   `:2866-2985`, caps the byte count at `:2957-2971`, stops at
   `segmentCount == withMaximumSegmentCount` (`:2973-2984`) and truncates the
   accumulated size to the request block size (`:2988`).
9. `IOBreaker::getNextStage()` (`:3104-3142`) advances `_start` by the current
   `_length`, sets `_length = min(_breakSize, _requestCount - _start)`, and
   recomputes `_breakSize` from the same request buffer at the *next* stage
   offset (`:3123-3132`). The first stage's break size is the one computed in
   `breakUpRequest()`.
10. `breakUpRequestExecute()` (`:3398-3422`) calls `executeRequest()` with the
    `IOBreaker` itself as the buffer. `executeRequest()` (`:1127-1187`) asserts
    that `byteStart` and `buffer->getLength()` are block-aligned (`:1146-1147`),
    computes `block = byteStart / blockSize` and
    `nblks = buffer->getLength() / blockSize` (`:1149-1150`), and calls
    `getProvider()->doAsyncReadWrite(buffer, block, nblks, ...)` (`:1178`).
11. `breakUpRequestCompletion()` (`:3335-3396`) checks the stage's status and
    actual byte count, advances to the next stage, and either issues it through
    a thread call or directly (`:3389-3393`). The final completion returns the
    original request's status and the accumulated actual byte count
    (`:3364-3382`). The next subrequest may therefore run on a thread-call
    thread, not the caller's thread.

### 2.2 The read-limit properties

`IOBlockStorageDriver::handleStart()` parses the read limits from its provider:

- `kIOMaximumBlockCountReadKey` -> `_maxReadBlockTransfer` (`:1289`);
- `kIOMaximumByteCountReadKey` -> `_maxReadByteTransfer` (`:1311`);
- `kIOMaximumSegmentCountReadKey` -> `_maxReadSegmentTransfer` (`:1333`);
- `kIOMaximumSegmentByteCountReadKey` -> `_maxReadSegmentByteTransfer`
  (`:1355`).

The adapter already sets the first three keys on itself before
`registerService()` (`package/Gta4lEmmcBlock.cpp:854-867`): block count 64,
byte count 32,768, and segment count 3 for `gta4l-emmc-deblock=1` or 1
otherwise. The driver copies the values at start, so a mode-scoped value
applies to every `IOBlockStorageDriver` instance in that boot, including the
whole-media and partition-media drivers and the partition-scheme scan. The
adapter's own `doAsyncReadWrite()` still enforces its unchanged 64-block
maximum (`package/Gta4lEmmcBlockCore.h:22`,
`package/Gta4lEmmcBlock.cpp:984`); the driver splits before the adapter, so a
128-block `IOMedia` request reaches the adapter only as smaller subrequests.

The mode-scoped advertisement is legal because it uses the existing keys and
the existing driver behavior. It does not raise a production limit, it does not
change the adapter's accepted request maximum, and it is confined to the one
opt-in diagnostic boot. The plan's warning against "lowering limits merely to
manufacture a pass" is respected by making the reduced limit the *declared
mechanism under test*: the mode exists to exercise the production split branch
that a request above the advertised limit necessarily takes, not to relax or
fake an acceptance check. The same table is also run in the host harness at the
production 64-block limit as a control (section 9).

### 2.3 IOBreaker, subranges and tag propagation

`IOBreaker` is declared at `:2723-2810` and derives from
`IOSubMemoryDescriptor`. `initWithBreakSize()` calls
`super::initSubRange(withRequestBuffer, 0, withRequestBuffer->getLength(),
withRequestBuffer->getDirection())` (`:3057-3061`) and retains the request
buffer (`:3080`). `IOSubMemoryDescriptor::initSubRange()` copies the
parent's tag into the subrange (`IOSubMemoryDescriptor.cpp:92`), so a tag set
on the caller's descriptor survives the `IOBreaker` creation. The `IOBreaker`
inherits `IOSubMemoryDescriptor::getPhysicalSegment()`
(`IOSubMemoryDescriptor.cpp:120-141`), which maps an offset through
`_parent->getPhysicalSegment(offset + _start)` and clips the length to the
current stage.

`IODeblocker` derives from `IOMemoryDescriptor` (`:1917`), not from
`IOSubMemoryDescriptor`, and `initWithBlockSize()` calls `super::init()`
(`:2056`), not `initSubRange()`. It therefore does **not** copy the caller's
tag. The `IOBreaker` built over the deblocker copies the deblocker's tag, which
is zero. This is a source-grounded asymmetry: the direct (aligned) path keeps
the caller's tag; the deblocker (unaligned) path loses it. The observability
contract in section 6 uses the tag for the direct path and the active case's
aligned span plus contiguous order for the deblocker path.

`IOMemoryDescriptor::setTag()` / `getTag()` are the public accessors
(`out/feeds/xnu-b7545ced/iokit/IOKit/IOMemoryDescriptor.h:660-672`,
`out/feeds/xnu-b7545ced/iokit/Kernel/IOMemoryDescriptor.cpp:2461-2468`). The
tag is a 32-bit `IOOptionBits` and is not printed.

## 3. Determinism argument

### 3.1 The block-count cap is layout-independent

The breakup mode advertises `kIOMaximumBlockCountReadKey = 8` and
`kIOMaximumByteCountReadKey = 4,096` (8 x 512) for itself. In `getBreakSize()`,
the block constraint is converted to bytes at `:2848-2863`:

```
blockCountInBytes = 8 * 512 = 4096;
withMaximumByteCount = min(4096, 4096) = 4096;
```

The scan obtains the first physical segment of the request buffer at
`bufferOffset = 0` (`:2866-2888`). Because the breakup bounce buffer is
page-aligned and at least one page long, the first segment covers at least one
whole page. Every supported page size is at least 4,096 bytes (the pinned
arm64 kernel uses a 16 KiB or 4 KiB granule;
`out/feeds/xnu-b7545ced/osfmk/mach/arm/vm_param.h:60-72`,
`out/feeds/xnu-b7545ced/osfmk/arm64/proc_reg.h:1193-1199`), so the first fold
already has `breakSize >= 4096`. The byte-count constraint at `:2957-2971`
then sets `breakSize = 4096` and breaks. The segment-count constraint at
`:2973-2984` is not consulted on that iteration because the folded segment was
consumed and the byte cap broke first. `IOTrunc(4096, 512) = 4096` (`:2988`).

The split size is therefore exactly 4,096 bytes regardless of whether the
allocation is physically contiguous or fragmented. The design does not assume
that virtual page crossings imply separate physical segments; it relies only
on the first segment being at least one page, which follows from the
page-aligned allocation.

### 3.2 Every stage keeps the same cap

The stage offsets are multiples of 4,096. For a page-aligned buffer:

- with 16 KiB pages, an offset that is a multiple of 4,096 leaves 4,096,
  8,192, 12,288 or 16,384 bytes in the current page, all at least 4,096;
- with 4 KiB pages, an offset that is a multiple of 4,096 is page-aligned, so
  the first segment is at least 4,096.

`getNextStage()` recomputes the break size at the next stage offset
(`:3123-3132`), and the same first-segment argument applies. The first stage's
4,096-byte break size and every later stage's 4,096-byte break size are
therefore deterministic. The final stage is the remainder:
`min(4096, requestCount - start)`.

### 3.3 The unaligned deblocker path needs three segments

For the unaligned case the buffer passed to `breakUpRequest()` is the
`IODeblocker`, whose first chunk is the excess-start scratch. With a 100-byte
head, the parent offset of a stage is shifted by 100 bytes relative to the
caller's page-aligned buffer. At the stage offset 16,384, for example, the
deblocker maps to caller-buffer offset 16,284, whose first physical segment is
the remaining 100 bytes of the caller's page (for a 16 KiB or 4 KiB page).
The first fold therefore gives `breakSize = 100`, below the 4,096-byte cap. The
scan obtains the next page as a new segment; because the segment count is 1 and
the advertised maximum is 3, the segment-count stop at `:2973-2984` does not
fire, the next full page is folded, `breakSize >= 4,196`, and the byte cap sets
it to 4,096. With `kIOMaximumSegmentCountReadKey = 1`, the scan stops at the
100-byte segment and `IOTrunc(100, 512) = 0`, so `breakUpRequest()` returns
`kIOReturnDMAError` before the adapter, provided the system mapper is absent:
`getBreakSize()` folds every segment as contiguous when
`IOMapper::gSystem != NULL` (`:2896`). The pinned sentinels are
`kNoMapper = 0` and `kHasMapper = 2`
(`out/feeds/xnu-b7545ced/iokit/IOKit/IOMapper.h:67-69`), `IOPlatformExpert`
sets the requirement from `IOPlatformMapperPresent`
(`out/feeds/xnu-b7545ced/iokit/Kernel/IOPlatformExpert.cpp:157`), and the
established N9.8 trace records that `IOMapper::gSystem` is null on this
platform (`docs/emmc-driver.md` §5.3.69). The breakup mode therefore advertises
segment count 3, the same mode-scoped value the deblock mode already uses. The
host `seg=1` control in section 9 models the null-mapper case; on a
mapper-present platform the segment-count advertisement would not be the
failure mechanism, while the 4,096-byte split-size argument in section 3.1
still holds because the second fold reaches the byte cap.

### 3.4 What the argument does and does not cover

The argument covers the pinned cases and the page-aligned bounce buffer. It
does not claim that arbitrary descriptors, arbitrary offsets or arbitrary
allocations split identically. The instrumentation in section 6 observes the
actual subrequest decomposition and fails the case closed if it differs from
the expected decomposition, so an unexpected allocation or driver behavior is
detected rather than assumed away. The physical addresses and the driver's
internal segment scan are not observed; the content digest and the block-level
decomposition are.

## 4. Decision: opt-in `gta4l-emmc-breakup=1` with a mode-scoped eight-block limit

The mode is selected by exactly one `gta4l-emmc-breakup=1` token. It requires
exactly one block token 1 or 3, like the deblock mode, and refuses:

- a duplicate `gta4l-emmc-breakup` token;
- an orphan token without `gta4l-emmc-block=1` or `3`;
- a non-1 or malformed value;
- any `gta4l-emmc-partobs`, `gta4l-emmc-partcmp`, `gta4l-emmc-partread` or
  `gta4l-emmc-deblock` combination.

The absent-token legacy, partobs, partcmp, partread and deblock reports remain
byte-for-byte unchanged. The mode-scoped advertisement is:

| property | legacy/partobs/partcmp/partread | deblock | breakup |
| --- | --- | --- | --- |
| `kIOMaximumBlockCountReadKey` | 64 | 64 | 8 |
| `kIOMaximumByteCountReadKey` | 32,768 | 32,768 | 4,096 |
| `kIOMaximumSegmentCountReadKey` | 1 | 3 | 3 |

The mode allocates one page-aligned 65,536-byte `IOMallocAligned` bounce buffer
(128 blocks), frees it with `IOFreeAligned()`, and uses it only for the
breakup cases. The existing deblock 33,792-byte buffer and the partread and
partmap buffers are unchanged. The adapter's `doAsyncReadWrite()`,
`copyOutCallback()` and core chunk loop are unchanged.

The transport policy is unchanged: 1-bit SDR, 400 kHz, polling PIO, the
existing CMD17 / two-sector CMD18 / CMD12 policy, read-only publication, no
retries and no fallback. The block token 1 or 3 continues to select the
existing report backend; the breakup flag selects only the new report form.

## 5. Case table and reference digests

The mode waits for exactly one whole-media `IOMedia`, validates the read-only
whole-media profile (absent `Base` accepted as zero, nonzero or wrong-typed
`Base` refused), opens the whole media as a reader, resolves the boot partition
by the pinned base `2,255,360 x 512` and size `196,608 x 512` on demand, and
performs the fixed boot-partition table below. `start` is the byte offset
relative to the boot partition; `length` is the requested byte count; `span` is
the deblocker-aligned byte count
`ceil((start + length) / 512) * 512 - floor(start / 512) * 512`; `blocks` is
`span / 512`; `subrequests` is `ceil(blocks / 8)`; `max` is `min(8, blocks)`.

| index | target | start | length | span | blocks | subrequests | max | branch |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | boot | 4096 | 4096 | 4096 | 8 | 1 | 8 | aligned control: `length <= breakSize` -> direct `executeRequest()` |
| 1 | boot | 4096 | 65536 | 65536 | 128 | 16 | 8 | aligned split: exact multiple of 8 blocks |
| 2 | boot | 4096 | 65024 | 65024 | 127 | 16 | 8 | aligned split: 15 full stages + 7-block final stage |
| 3 | boot | 4196 | 65024 | 65536 | 128 | 16 | 8 | unaligned split through the `IODeblocker` (head 100, tail 412) |

The four cases cover the three `breakUpRequest()` branches: the direct
short-circuit (case 0), the `IOBreaker` split with an exact final stage (case
1), the split with a partial final stage (case 2), and the split over the
deblocker's three-chunk descriptor (case 3). Cases 0-2 are aligned and
construct no deblocker; case 3 is the separate justified gap: it is the only
case that exercises `breakUpRequest()` with the `IODeblocker` as the parent
descriptor, the three-chunk physical mapping and the 100-byte-shifted page
boundary.

Each case carries a pinned 32-byte SHA-256 of the exact requested bytes:

| index | full SHA-256 |
| --- | --- |
| 0 | `5878bcf8a6afe4dd79a002caa8b6aeeae16125b890ab750b67d6ab3c25f3b1b2` |
| 1 | `b4c3c643e5a43d82a52ec8c101bacfa51210244d7bf40e97ef1436447880fa9c` |
| 2 | `5f970472402e70b54bb0f8b4945f5f020d54798b42ca32a21742a4570f61a95c` |
| 3 | `bc0b9bb9c6dd11c3aea665d01dc5f10814a460d41eb77e0a7a01bb2be726db17` |

The derivation was host-only and used the gate-pinned v5 BOOT image
`out/gta4l/boot/boot-ram-root-sdhci-v5.img` (85,688,320 bytes), whose kernel
section `4096..1,802,148` is the boot-image baseline with hash `a4b054e0…`:

- case 0 is `SHA-256(image[4096..8192])`;
- case 1 is `SHA-256(image[4096..69632])`;
- case 2 is `SHA-256(image[4096..69120])`;
- case 3 is `SHA-256(image[4196..69220])`, whose aligned span is
  `4096..69632`.

The design batch recomputed all four values from this image with
`hashlib.sha256` on 2026-11-09 and all four matched the table. The
concatenation property also held:
`case 1 = case 2 || image[69120..69632]`, and case 3 is not a shifted
duplicate of case 2. The ranges are inside the 1,798,052-byte kernel section
and inside the 85,688,320-byte image, so the "shorter candidate prefix versus
full BOOT tail" boundary does not apply to them. The N10.6 hardware run
already read `4096/1024` and `4196/924` on the device and matched the v5
kernel-section digests `b50aab9d…` and `ff346b8d…`; the new ranges extend
those proven anchors through the same gate-pinned section. The N10.5 candidate
`bacc933f…` differs from v5 only in the ramdisk; its kernel section, firmware
and boot DTB are unchanged.

The retained full-partition backup
`out/gta4l/boot/boot-backup-20261027-pmgr.img` (100,663,296 bytes) is the
pre-flash image. Its kernel section does not match the candidate and is not
used for these digests. Its preserved tail beyond the 85,688,320-byte BOOT
image was used by N10.6 cases 8-9, but N10.7 does not use it: that tail is
zero-filled (the N10.6 case 8/9 digests are zero-range digests), so it cannot
bind a split read to the requested offsets. N10.7 deliberately uses the
non-zero, gate-pinned kernel section instead.

The implementation batch re-derives all four digests from the retained image,
pins them in the case table, and verifies each range on the host. The design
does not invent digest values. The report prints only the first 16 digest
bytes, the target, the bounded offset/length and counters; no raw sectors,
GUIDs, labels, serials or identifiers are printed.

## 6. Per-case observability contract

### 6.1 Case ownership and the active window

The report thread issues one `IOMedia::read()` at a time and blocks in the
synchronous `IOStorage::read()` wrapper until the deblocker or breaker
completes. Before the read it records, under `reportLock`, the active case
index, the expected tag, the case's first block and block count, the expected
subrequest count and maximum, and the expected final block. It clears the
active flag after the read returns. It never holds `reportLock` across the
media call.

The adapter's `doAsyncReadWrite()` calls a new mode-scoped observer after the
null/zero argument checks and before it takes the controller `gate`. The
observer takes `reportLock` briefly, reads the active state, classifies the
request, updates the observation counters and releases the lock. It does not
take `gate`, does not retain the buffer and does not perform I/O. Because the
report thread holds no lock while blocked, and the observer never takes `gate`,
there is no lock inversion and no lock held across a media call.

### 6.2 Request classification

The observer classifies each request that arrives while a case is active:

- **Direct path (aligned cases 0-2):** the caller's descriptor is tagged with
  a non-zero per-case tag before `media->read()`. `IOBreaker::initWithBreakSize()`
  copies the tag through `IOSubMemoryDescriptor::initSubRange()`
  (`IOSubMemoryDescriptor.cpp:92`), so every subrequest delivered to the
  adapter carries that tag. A request with the active tag is a case subrequest;
  a request with any other tag is counted as `foreign`.
- **Deblocker path (unaligned case 3):** `IODeblocker` derives from
  `IOMemoryDescriptor` and calls `super::init()`, so it does not copy the
  caller's tag; the `IOBreaker` over it has tag 0. A tag-0 request is a case
  subrequest only if its block range is exactly the next expected range inside
  the active aligned span. A tag-0 request outside that span is counted as
  `foreign`; a tag-0 request inside the span but out of order or overlapping is
  recorded as an order error.
- A request whose block range starts inside the active span but ends outside
  it, or whose `nblks` exceeds the mode's 8-block limit, is recorded as an
  order error and its observed `nblks` is still recorded so the report shows
  the oversized subrequest.
- A request that arrives while no case is active is ignored by the observer;
  the existing whole-boot counters continue to count it.

For each attributed subrequest the observer increments the observed subrequest
count, adds `nblks` to the observed block total, raises the observed maximum
and advances the expected next block. Counters saturate at their unsigned
maximum rather than wrapping.

### 6.3 Expected decomposition and the case verdict

After the read returns, the report thread compares the observed values with the
case table's expected values:

- observed subrequest count equals `ceil(blocks / 8)`;
- observed block total equals `blocks`;
- observed maximum equals `min(8, blocks)`;
- the observed next block equals the case's first block plus `blocks`;
- no order error and no oversized subrequest were recorded;
- the status is `kIOReturnSuccess`, `actual == length`, and the SHA-256 of the
  requested bytes equals the pinned digest.

A mismatch of the first five is the new `SPLIT` (wrong count/total/max) or
`ORDER` (out-of-order/overlapping/oversized) reason; a status, actual or digest
failure uses the existing `READ`, `SHORT` or `HASH` reasons. The first failing
case latches the failure index, reason, status, actual, observed
subrequests/blocks/max and the computed digest. Later cases continue after a
read/short/hash/descriptor/split/order failure so the counters attribute every
case; a media, partition, open, budget or cancellation failure stops the run.

A request during an active case that is classified as `foreign` increments the
global `foreign` counter and is not attributed to the case. If `foreign` is
nonzero after the case loop, the run latches `reason=13` (`FOREIGN`) and
`complete=0`; the final `complete=1` guard requires `foreign == 0`, so an
unattributable request fails the run closed instead of being silently ignored.
The report still prints `foreign` so the owner can distinguish an
incidental-traffic stop from a read or digest failure.

### 6.4 What the instrumentation proves and does not prove

It proves, per case:

- the number, block total, maximum size and contiguous order of the
  driver-level subrequests delivered to the adapter;
- that the aligned direct-path subrequests carry the case tag and that the
  deblocker-path subrequests are the expected untagged sequence;
- that the requested bytes have the pinned content;
- that the first failure, if any, is identified by case, reason, status,
  actual, observed decomposition and digest prefix.

It does not prove:

- the physical addresses or the number of physical segments inside the
  driver's `getBreakSize()` scan; the split-size argument in section 3 is a
  source argument, not an observation of the scan;
- the exact thread on which the driver issues a continuation;
- that a foreign request which exactly mimics the next expected subrequest
  (same tag or same tag-0 block range and length) did not occur; the
  contiguous-order check and the `foreign` guard make this the only remaining
  ambiguity;
- any request outside the four pinned cases.

### 6.5 Rejection of whole-boot counter subtraction

The existing `DRV`/`XFER` counters remain aggregate whole-boot counters. N10.7
does not compute a per-case split by subtracting `DRV` values before and after
a case, does not compare a breakup boot with N10.6, and does not use the N10.6
`max=24` as a pass criterion. The N10.7 verdict is the per-case observed
decomposition plus the content digest. The whole-boot counters remain useful
context, not evidence for the split.

## 7. Read protocol, bounds, cancellation and terminal ownership

Algorithm:

1. Wait up to `GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS` (20,000 ms) for exactly one
   whole media, checking `reportCancel` before every poll and sleeping only in
   the report thread. Zero or more than one candidate is a media failure
   (`reason=1`); a profile/geometry mismatch is also a media failure.
2. Open the whole media as a reader. A refused open is `reason=3`.
3. For each case in table order:
   - Check `reportCancel` and the read budget
     (`GTA4L_EMMC_BLOCK_BREAKUP_READ_BUDGET_MS`, 6,000 ms) before the read. A
     cancellation latches `reason=9` and `cancel=1`; a budget stop latches
     `reason=8` and `budget=1`.
   - Resolve the boot partition on first use with the N10.1 deadline
     (`GTA4L_EMMC_BLOCK_PARTREAD_DEADLINE_MS`, 2,000 ms) and open it as a
     reader; an absent/ambiguous partition is `reason=2`, a refused open is
     `reason=3`.
   - Validate the case: target boot, nonzero length, range inside the boot
     partition, aligned span at most 65,536 bytes, span a multiple of 512,
     blocks at most 128 and subrequests at most 16. An invalid table entry is
     `reason=10`.
   - Set the active case state under `reportLock`, build a page-aligned
     `IOMemoryDescriptor::withAddress()` over the breakup bounce buffer with
     exactly `length` bytes, set the non-zero case tag, call
     `media->read(this, start, descriptor, nullptr, &actual)`, release the
     descriptor and clear the active state. A null descriptor latches
     `reason=4` (`DESC`) and continues with the next case. The report thread
     must not hold the controller gate across the call.
   - On a non-success status, latch `reason=5` and the raw status/actual. On
     `actual != length`, latch `reason=6`. Otherwise compute the SHA-256 over
     the requested bytes and compare it to the pinned digest; a mismatch is
     `reason=7`.
   - Compare the observed decomposition as in section 6.3; a mismatch is
     `reason=11` (`SPLIT`) or `reason=12` (`ORDER`).
4. Continue the remaining cases after a read/short/hash/descriptor/split/order
   failure so the counters attribute every case. Stop immediately on a media,
   partition, open or budget failure. No retry, fallback, reset, repair or
   write follows any failure.
5. On cancellation, set `reason=9`, `cancel=1`, `complete=0`, clear the active
   case, release the media and return through the existing no-panic
   cancellation path. The terminal-ownership rule (`reportLock`,
   `claimTerminal()`) is unchanged; the report thread or the watchdog emits
   the single final panic. A late subrequest after the watchdog claim cannot
   alter the emitted report.
6. Release the boot and whole media on every exit path. The report thread
   never sleeps while holding the controller gate and never holds a lock
   across a media call.

The breakup bounce buffer is one page-aligned `IOMallocAligned` allocation
sized to `GTA4L_EMMC_BLOCK_BREAKUP_MAX_BYTES` (128 blocks, 65,536 bytes),
which covers the largest requested length and the largest aligned span. It is
allocated only for `gta4l-emmc-breakup=1` and freed with `IOFreeAligned()`;
the legacy, partobs, partcmp, partread and deblock allocations are unchanged.

Worst-case arithmetic: 20,000 ms media wait + 6,000 ms read budget + 2,000 ms
partition deadline + one nominal 1,000 ms in-flight read = 29,000 ms, below
the 30,000 ms watchdog. The 6,000 ms budget covers the four cases' 199,680
requested bytes (about 4.0 s at 400 kHz 1-bit SDR) with headroom. Each adapter
call handles at most 8 blocks, so the adapter gate is held for 4 KiB per
subrequest, below the historical 64-block pathological bound. The driver's
thread-call continuations add scheduling latency but no new unbounded wait; the
synchronous wrapper has no timeout of its own, so the watchdog remains the
terminal bound.

## 8. Report lines and budget

When `gta4l-emmc-breakup=1` is present, emit two bounded lines in the existing
report:

```
GTA4L-EMMC P5 BREAKUP: cases=4 passed=4 failed=0 reason=0 complete=1 reads=4 bytes=199680 span=200192 blocks=391 subrequests=49 max=8 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 limit=8 seg=3 cap=61071360
GTA4L-EMMC P5 BREAKUPREAD: index=3 target=1 start=4196 length=65024 span=65536 actual=65024 status=0 subrequests=16 blocks=128 max=8 digest=bc0b9bb9c6dd11c3aea665d01dc5f108
```

Field meanings:

- `cases`, `passed`, `failed`: the fixed table size and the case outcomes.
- `reason`: the first failure code (section 7); `0` on a full pass.
- `complete`: `1` only when `cases != 0`, `passed == cases`, `failed == 0`,
  `reason == 0`, `cancel == 0`, `budget == 0`, `media == 0`, `part == 0`,
  `foreign == 0`, `limit == 8`, `seg == 3`, and every case's
  status/actual/digest/decomposition passed. Otherwise `0`.
- `reads`: attempted case reads; `bytes`: requested bytes across attempted
  cases; `span`: aligned bytes across attempted cases; `blocks`: `span / 512`.
- `subrequests`: observed driver-level subrequests across the attempted cases;
  `max`: maximum observed subrequest block count; `foreign`: requests observed
  during an active case that were not attributed to it.
- `fails`: transport/copy failures; `cancel`, `budget`, `media`, `part`: the
  N10.1 failure flags.
- `limit`: the mode's advertised read block limit (8); `seg`: the mode's
  advertised read segment count (3); `cap`: `capacityBlocks` (`61071360` on
  this device).
- `BREAKUPREAD`: the first failing case when `failed != 0`, otherwise the last
  attempted case. It prints the case index, target, requested start/length,
  aligned span, actual bytes, raw signed 32-bit status, observed subrequest
  count/blocks/max and the first 16 digest bytes. A failed case's digest is
  zero when the read failed before hashing.

The expected full-pass aggregate is exactly the table: `4,096 + 65,536 +
65,024 + 65,024 = 199,680` requested bytes; `4,096 + 65,536 + 65,024 + 65,536
= 200,192` aligned bytes; `200,192 / 512 = 391` blocks; `1 + 16 + 16 + 16 =
49` observed subrequests; maximum 8. The implementation batch pins these
values in the host tests; a later table change requires a design update, not a
silent counter change.

Budget and privacy rules:

- The N10.6 twelve-line deblock report is 1,182 bytes including newlines; the
  two example new lines are 211 and 172 bytes. Even if both are added to the
  full existing report, the total stays below 1,600 bytes, leaving more than
  450 bytes of the 2,048-byte report. The host tests bound the `BREAKUP` line
  at 448 bytes and `BREAKUPREAD` at 320 bytes, and must keep the whole report
  below 2,048 bytes before NUL termination with the maximum field widths this
  device can produce.
- The existing formatter fail-closed behavior is unchanged: if the writer
  cannot fit the report, the buffer becomes empty and the report path panics
  with the existing format-failure message.
- The line contains only bounded counts, offsets, lengths, statuses and a
  16-byte digest prefix; no GUIDs, labels, identifiers, serials or raw ranges
  are printed.

## 9. Host-test plan (N10.8 acceptance)

The existing host seam (`scripts/sim-gta4l-emmc-block/`) calls
`doAsyncReadWrite()` directly and already reconstructs the deblocker and the
`getBreakSize()` split from the adapter's advertised properties
(`adapter_support.cpp`, `adapter_harness.cpp`). N10.8 extends it with:

- `setTag()`/`getTag()` on the test `IOMemoryDescriptor`, and tag copying in
  the model's `IOSubMemoryDescriptor`-style subrange. The model's direct-path
  subrequests carry the case tag; the model's deblocker-path subrequests carry
  tag 0, matching `IODeblocker`'s `IOMemoryDescriptor` base.
- a test-only block-count and byte-count override in the model, so the same
  table can be run at the production 64-block/32 KiB limit without changing
  the adapter's advertised values.
- the breakup case table and the adapter observer; the model continues to
  split the aligned span with its `getBreakSize()` reconstruction.

Required scenarios:

1. Full pass: all four cases with the model at the breakup limit; exact
   requested bytes, expected `passed=4 failed=0 reason=0 complete=1 reads=4
   bytes=199680 span=200192 blocks=391 subrequests=49 max=8 foreign=0`.
2. Production-limit control: the same table with the model at 64 blocks. Case
   0 is one 8-block subrequest; cases 1-3 are two subrequests (64+64, 64+63,
   64+64) and exceed the observer's 8-block expectation, so the run must fail
   with `SPLIT`/`ORDER` and `complete=0`. This proves the reduced limit is the
   split mechanism and that the expected decomposition is enforced.
3. `seg=1` anti-weakening control (null-mapper model, as on this platform):
   cases 0-2 still pass; case 3 must fail with `kIOReturnDMAError` before the
   adapter because the 100-byte head segment is truncated to zero. This proves
   the three-segment advertisement is necessary for the unaligned split under
   the pinned null `IOMapper::gSystem`; a mapper-present model would instead
   fold the next segment and keep the 4,096-byte split.
4. Wrong decomposition controls: 15 subrequests (missing), 17 subrequests
   (extra), a reordered pair, an overlapping range, a 9-block subrequest and a
   single 128-block subrequest. Each is rejected with `SPLIT` or `ORDER` and
   the observed values are visible in the report.
5. Foreign-traffic control: a tag-0 or differently-tagged request inside or
   outside the active case span. The in-span out-of-order request is `ORDER`;
   the out-of-span request increments `foreign` and makes `complete=0` with
   `reason=13` (`FOREIGN`). The case's own subrequests remain attributed.
6. Read failure at each case position (`kIOReturnIOError`,
   `kIOReturnDMAError`, `kIOReturnNoMedia`), short transfer (`actual=0`,
   `actual=length-1`, `actual=length+1`), digest mismatch, descriptor
   allocation failure, wrong target/range and a zero-length table entry; the
   first reason latches, later cases continue for read/short/hash/descriptor/
   split/order failures, and `complete=0`.
7. Budget exhaustion before a case; cancellation before the media wait,
   between cases and after a read; media absent, two whole media, wrong
   profile, boot partition absent/ambiguous, and open refused; each produces
   the expected flag and no panic claim.
8. Watchdog race: the watchdog claims the terminal while the report thread is
   blocked in a case read; the report thread must not panic twice or overwrite
   the watchdog report. A late subrequest after the claim must not alter the
   emitted report.
9. Selector controls: orphan, duplicate, wrong value, and each
   partobs/partcmp/partread/deblock combination refused; the absent-token
   legacy report is byte-identical; the accepted `emmc-block-1`/`emmc-block-3`
   payloads are unchanged.
10. Property controls: legacy/partobs/partcmp/partread advertise 64/32,768/1;
    deblock advertises 64/32,768/3; breakup advertises 8/4,096/3. The
    non-breakup modes' properties and reports are unchanged.
11. Report controls: worst-case field widths fit 448/320 bytes and the whole
    report fits 2,048 bytes; a deliberately small buffer returns 0 and takes
    the existing format-failure path; the expected full-pass lines match; no
    raw identifier appears.
12. Mutation controls beyond the above: accept a short transfer, accept a
    non-success status, skip the digest compare, accept a wrong target/range,
    skip the budget or cancellation check, force `complete=1`, revert the
    block or segment advertisement, use the caller's class-member buffer
    instead of the page-aligned bounce, skip the completion wait, and skip the
    decomposition check. Each must be rejected by the suite.

The host model is a reconstruction; it does not run the real
`IOBlockStorageDriver`/`IOBreaker`/`IODeblocker` code. The hardware run is the
only qualification of the real breakup layers, and the design does not claim
otherwise.

## 10. Implementation surface and approval gates

N10.8, if approved, changes:

- `package/Gta4lEmmcBlock.h`/`.c`: the new token and selector, the four-case
  table and full digests, the reason codes, the report fields, the formatter
  and the static asserts for the 2,048-byte report, the 65,536-byte bounce and
  the 29,000 ms worst case; no transport, width, timing, DMA/CQE/ICE, write or
  production request-limit change.
- `package/Gta4lEmmcBlock.cpp`: the opt-in mode parse, the mode-scoped
  block/byte/segment advertisement, the page-aligned breakup bounce, the case
  loop, the boot-partition resolution, the adapter observer, the cancellation
  and terminal handling, and the report copy. `doAsyncReadWrite()`'s data
  path, `copyOutCallback()` and the core chunk loop are unchanged; the
  observer is a bounded entry-side observation only.
- `scripts/build-gta4l-diagnostic-payload.py`,
  `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py`:
  the `emmc-block-1-breakup`/`emmc-block-3-breakup` personalities, token
  validation and the partobs/partcmp/partread/deblock refusal.
- `scripts/sim-gta4l-emmc-block/*` and `scripts/test-gta4l-emmc-block.py`:
  the tag model, the limit overrides, the scenarios and the mutation controls
  above.
- Documentation refresh after implementation (`docs/emmc-driver.md` next
  section, `README.md`, `porting.toml`, `docs/evidence-baseline.md` with the
  baseline kept at 45 rows and changed hashes refreshed,
  `docs/diagnostic-payload-input-inventory.md`) with the evidence-count checks
  and the hash table kept consistent.

The guarded KernelConfigTables personality patch, the archive builder's symbol
set and the storage-ABI bindings are expected to need no change: the new token
and report fields are data, and the override set is unchanged. The candidate
builder must still confirm zero new unbound symbols and a candidate-matched
audit.

Gates:

- N10.8 implementation: owner approval and Build mode. Host tests only; no
  candidate build, ABI/QEMU audit, device access, flash or boot.
- N10.9 archive/ABI/kernel/payload/BOOT production: separately gated,
  host-only, with the storage-ABI, binding, payload, BOOT-gate and
  evidence-count checks. No device.
- N10.10 hardware: fresh explicit BOOT-only approval, verified protected
  backups and readbacks, and one owner-controlled cold boot. No retry,
  reflash, rollback, reset/repair, mount, root switch, write, wider/faster
  mode, DMA/CQE/ICE or production request-limit change. The one-boot budget is
  consumed by the run.
- Initialization reliability remains a separate finite, predeclared
  investigation; N10.7 does not measure it.

## 11. Limits and non-goals

- N10.7 qualifies a fixed, bounded same-boot table of four reads on one cold
  boot. It does not qualify arbitrary byte offsets or lengths, arbitrary I/O,
  filesystem mount, root integration, writes, partition semantics beyond the
  pinned bytes, wider/faster modes, DMA/CQE/ICE or initialization reliability.
- The eight-block / 4,096-byte read limit and the three-segment advertisement
  are mode-scoped. They are not production-limit recommendations, not a
  general request-limit increase, and not a change to the accepted
  N10.1/N10.2/N10.6 images or counters. The legacy/partread/partcmp/partobs
  modes keep 64/32,768/1 and the deblock mode keeps 64/32,768/3.
- The split is forced by the block-count cap, not by incidental physical
  fragmentation. The design explicitly rejects whole-boot counter subtraction
  and the N10.6 `max=24` observation as the deterministic trace.
- The host model is a reconstruction. Physical adjacency, the driver's
  internal segment scan and the real thread-call timing remain host
  reconstructions. The hardware run is the only qualification of the real
  `IOBlockStorageDriver`/`IOBreaker`/`IODeblocker` layers.
- The zero-filled boot tail is not used. The expected bytes come from the
  gate-pinned v5 kernel section, not from the retained pre-flash full-partition
  backup's kernel section and not from the tail beyond the 85,688,320-byte
  BOOT image.
- The N10.1 hardware result and its captured repeatability series, the N10.2
  hardware result, the N10.6 eleven-case result, the N9.10 comparator result
  and the historical N8.5/N9.6/N9.7/N9.8 `complete=0` records are preserved
  unchanged. A future N10.10 `complete=1` is a new, exact same-boot claim with
  the scope in section 8; it does not upgrade any earlier result.
- A read error, short transfer, digest mismatch or decomposition mismatch is a
  measurement failure of that case, not a card verdict. A cancellation, budget
  stop, foreign-traffic stop or absent media is a bounded observation limit,
  not a failure-rate estimate.
- No commit or push is part of the N10.7 design batch. The design document
  itself is committed only as part of a separately approved batch.

## 12. Decision requested

The owner is asked to approve the N10.8 implementation scope:

1. the opt-in `gta4l-emmc-breakup=1` token with the strict conflict rules;
2. the mode-scoped 8-block / 4,096-byte read advertisement and the
   three-segment advertisement, with every other mode kept at its existing
   values;
3. the four-case table, the pinned v5 kernel-section digests and the
   `BREAKUP`/`BREAKUPREAD` report lines;
4. the per-case tag/span/order observation, the `foreign` guard and the
   explicit rejection of whole-boot counter subtraction;
5. the host tag/limit model, the production-limit control, the `seg=1`
   anti-weakening control and the mutation controls in section 9.

Approval of this design does not authorize the N10.9 candidate build or the
N10.10 hardware run; those remain separately gated.
