# gta4l N10.3 read-only unaligned IOMedia (deblock) qualification design (host-only, 2026-11-08)

**Status:** N10.3 host-only design. This batch performed no device access, boot,
flash, candidate build, driver change, QEMU run, commit or push. It follows the
N10.2 fourteen-case hardware result and preserves N10.1/N10.2, the N10.1
captured repeatability series and the earlier N-series records. N10.4
implementation requires owner approval and Build mode; N10.5 archive/ABI/
kernel/payload/BOOT production and any N10.6 hardware run are separately
gated. The N10.2 one-boot budget is consumed, and initialization reliability
remains `[U]`.

## 1. Why N10.2 does not qualify the deblock layer

N10.1/N10.2 issue whole-block reads through `IOMedia::read()`: the requested
byte start and the requested byte length are both multiples of 512. In
`IOBlockStorageDriver::deblockRequest()`
(`out/feeds/IOStorageFamily-67f9003c/IOBlockStorageDriver.cpp:2536`), an
aligned request short-circuits directly to `breakUpRequest()` (`:2565-2568`);
the `IODeblocker` is never constructed. The fourteen N10.2 digests therefore
qualify the aligned media read and the adapter's chunked copy-out, but not the
deblocker's excess handling or its scratch-copy semantics. Every N10.2 request
also fit the 64-block/32 KiB transfer limit, so `breakUpRequest()` took the
direct `executeRequest()` short-circuit; the real `IOBreaker` split was not
entered (the host harness models it).

The host seam has the same blind spot from the other direction: it calls
`Gta4lEmmcBlock::doAsyncReadWrite()` directly, so its breakup model
(`scripts/sim-gta4l-emmc-block/adapter_harness.cpp:217-245`) is a
reconstruction of `IOBreaker::getBreakSize()`, not a run of the real
`IOBlockStorageDriver`/`IODeblocker` code. N9.7 and N10.1/N10.2 records say
this explicitly: the host seam does not qualify the real
context/deblock/breakup layers (`docs/emmc-driver.md` §5.3.66, §5.3.70,
§5.3.78).

N10.3 is the next read-only rung: qualify a fixed, bounded table of
**byte-unaligned** `IOMedia` reads that forces the real deblocker path on the
device, while keeping the transport, width, timing, request limits and write
protection unchanged. It is not a general-I/O, mount, write or reliability
rung.

## 2. Pinned IOStorageFamily survey

All references are the pinned public feed
`out/feeds/IOStorageFamily-67f9003c/`. The survey was read-only.

### 2.1 Call path

1. `IOMedia::read()` (`IOMedia.cpp:702`) rejects an inactive, unopened or
   unformatted media, a null or non-`In` buffer, a start beyond `_mediaSize`
   and a buffer that would cross `_mediaSize`. It then adds `_mediaBase`
   (`:760`) and calls `getProvider()->read()`. The partition-media base
   addition is therefore part of the deblock path too.
2. The synchronous `IOStorage::read()` wrapper (`IOStorage.cpp:221-248`)
   builds an `IOStorageCompletion`, issues the asynchronous read and waits on
   `IOStorageSyncer::wait()` (`IOStorage.cpp:114-126`). That wait has no
   timeout; the controller's per-command timeout and the adapter's 30,000 ms
   watchdog are the existing bounds.
3. `IOBlockStorageDriver::prepareRequest()` (`:3424`) records the request and
   calls `deblockRequest()` (`:2536`) at `:3523`.
4. `deblockRequest()` builds `IODeblocker::withBlockSize(blockSize,
   requestStart, requestBuffer, ...)` (`:2574`) when either the start or the
   length is not block-aligned, then calls
   `deblockRequestCompletion(this, deblocker, kIOReturnSuccess, 0)` (`:2610`).
5. `IODeblocker::initWithBlockSize()` (`:2037-2122`) computes
   `_excessCountStart = requestStart % blockSize` (`:2075`),
   `_excessCountFinal = blockSize - ((requestStart + requestCount) %
   blockSize)` when nonzero (`:2076-2077`), and
   `_requestIsOneBlock = (_excessCountStart + _requestCount <= blockSize)`
   (`:2079`). For a read the scratch buffer is
   `max(_excessCountStart, _excessCountFinal)` bytes (`:2087`), allocated with
   `IOBufferMemoryDescriptor::withCapacity()` (`:2115-2117`).
6. `IODeblocker::getNextStage()` (`:2257`) for `kIODirectionIn` (`:2277-2308`)
   sets
   `_byteStart = _requestStart - _excessCountStart` (`:2286`) and builds at
   most three chunks: the excess-start scratch (`:2288-2293`), the caller's
   request buffer (`:2296-2299`) and the excess-final scratch
   (`:2301-2307`). Both excess chunks map to scratch offset 0; for a read
   they are discarded, so the aliasing is harmless.
7. `IOBlockStorageDriver::breakUpRequest()` (`:3212-3333`) computes
   `breakSize = IOBreaker::getBreakSize(...)` from the advertised read limits
   (`:3247-3256`). If `breakSize == 0`, it completes with
   `kIOReturnDMAError` (`:3272-3275`) before `executeRequest()`. If the
   aligned transfer fits, it calls `executeRequest()` directly
   (`:3279-3283`); otherwise it builds an `IOBreaker` and splits the transfer
   (`:3287-3330`).
8. `IOBreaker::getBreakSize()` (`:2816-2990`) scans the buffer's physical
   segments, folds only physically contiguous segments unless
   `IOMapper::gSystem` is set, stops at
   `segmentCount == withMaximumSegmentCount` (`:2975-2980`), and truncates
   the accumulated size to the request block size (`:2988`).
9. `executeRequest()` (`:1127-1187`) computes `block = byteStart /
   blockSize`, `nblks = buffer->getLength() / blockSize` and calls
   `getProvider()->doAsyncReadWrite(buffer, block, nblks, ...)` (`:1176`).
10. `deblockRequestCompletion()` (`:2613-2692`) obtains or allocates the
    deblocker's thread callback (`:2630-2637`; the allocation itself is in
    `setThreadCallback()` at `:2531`) and uses `thread_call_enter1()` for the
    next stage (`:2685`). A deblocked read is therefore internally
    asynchronous even though the adapter's `doAsyncReadWrite()` completes
    synchronously; the synchronous `IOStorage::read()` wrapper is what waits
    for the final completion.
11. `IODeblocker::getRequestCompletion()` (`:2448-2487`) subtracts
    `_excessCountStart` from the aligned actual byte count and clamps it to
    the request length on the last stage. The caller sees only the requested
    bytes.

### 2.2 Adapter surface

- `Gta4lEmmcBlock::doAsyncReadWrite()` (`package/Gta4lEmmcBlock.cpp:872`)
  validates the buffer and completion, `nblks != 0`, the media range,
  `nblks <= GTA4L_EMMC_BLOCK_MAX_BLOCKS` (64), `buffer->getLength() >=
  nblks * 512`, and `kIODirectionIn`; it takes the controller gate, sets
  `requestBuffer` (`:934`), calls `gta4l_emmc_block_read()` (`:959`) and
  completes synchronously (`:998`).
- `copyOutCallback()` (`:420-459`) writes each qualified chunk into
  `requestBuffer` with `writeBytes()`. For a deblocked read, `requestBuffer`
  is the `IOBreaker`/`IODeblocker` descriptor, so the write maps through the
  deblocker's chunk list.
- The adapter advertises `kIOMaximumBlockCountReadKey = 64`,
  `kIOMaximumByteCountReadKey = 32768` and
  `kIOMaximumSegmentCountReadKey = 1` (`:774-788`).
- The partread buffer is one page-aligned 32 KiB `IOMallocAligned`
  allocation (`:700-704`); the comparator bounce is page-aligned for the same
  reason (`:719-730`). The code comment already records the rule: a sub-block
  first segment is truncated to zero by the storage driver before the adapter
  is called.

## 3. The one-segment blocker

For a head-unaligned read (`requestStart % 512 != 0`), the deblocker's first
chunk is the excess-start scratch, whose length is `requestStart % 512` and is
therefore strictly less than 512. With `kIOMaximumSegmentCountReadKey = 1`,
`IOBreaker::getBreakSize()` folds only that first segment, `IOTrunc(head, 512)`
is zero, and `breakUpRequest()` completes with `kIOReturnDMAError`
(`0xe00002d4`, printed as `3758097108`) before `executeRequest()` or
`doAsyncReadWrite()`. A page-aligned caller buffer does not help: the
deblocker's scratch is a separate allocation and remains the first segment.

This is the same pre-adapter return site that N9.8 observed for the
comparator's 488-byte class-member first segment (`docs/emmc-driver.md`
§5.3.69), but it is now a structural property of the deblocker rather than a
caller-layout accident. N9.9's page-aligned bounce removed the N9.8 instance;
it cannot remove this one.

A tail-only unaligned read (`requestStart` aligned, length not a multiple of
512) has the caller's request buffer as its first segment. With a page-aligned
request buffer that segment is a full page, so `breakSize` is nonzero and the
read proceeds. A same-block read with both excesses is head-unaligned and is
blocked for the same reason as the head-only case.

Three details bound the advertisement. `IOBreaker::getBreakSize()` counts
folded physical-segment groups, not the deblocker's logical chunks: each
non-contiguous segment starts a new group, and the loop stops before starting
a group beyond the advertised count. The read scratch is a sub-512-byte
`IOBufferMemoryDescriptor::withCapacity()` allocation (`max(head, tail)`), so
it is a single sub-page physical segment. The caller's request buffer is
page-aligned, so its first physical segment is a full page (or the whole
request when shorter). With a three-group limit the folded bytes are at least
the head scratch plus the request's first segment; the tightest table case is
the one-block request `100 + 200 + 212 = 512`, which folds the head scratch,
the request and the tail scratch. A larger request folds the head scratch and
its first two page segments. Every table case reaches at least one 512-byte
block, so `breakSize` is nonzero.

The N10.3 design therefore cannot qualify the full deblock branch by only
choosing read ranges; it must change the advertised read segment count for
the opt-in mode.

## 4. Decision: opt-in `gta4l-emmc-deblock=1` with a mode-scoped three-segment read advertisement

N10.3 chooses a new read-only diagnostic token, `gta4l-emmc-deblock=1`,
orthogonal to the existing block mode and mutually exclusive with the other
partition/read instruments:

- `gta4l-emmc-block=1` (CMD17 report) and `gta4l-emmc-block=3` (chunked
  report) keep their existing semantics and report lines.
- `gta4l-emmc-block=2` remains refused.
- `gta4l-emmc-deblock=1` requires exactly one block token with value 1 or 3.
  An orphan token, a duplicate token, any value other than 1, or a
  combination with `gta4l-emmc-partobs=1`, `gta4l-emmc-partcmp=1` or
  `gta4l-emmc-partread=1` in the same boot is refused before MMIO, matching
  the existing selector rules (`gta4l_emmc_block_partread_select()` is the
  template).
- When the token is absent, the legacy mode-1/mode-3 reporter, including the
  `PART`, `QUAL`, `PARTMAP`, `READDETAIL`, `PARTOBS` and `PARTREAD` behavior,
  is byte-for-byte unchanged. The accepted N5, p5b/F5, N8.5, N9.10,
  N10.1 and N10.2 artifacts are not re-interpreted.

The adapter's read segment advertisement becomes mode-scoped:

| Mode | `kIOMaximumSegmentCountReadKey` | Reason |
| --- | --- | --- |
| legacy mode 1/3, partobs, partcmp, partread | 1 | unchanged; keeps the accepted images' driver behavior |
| deblock | 3 | the read descriptor has at most three chunks and the scratch is one sub-page segment; three folded groups guarantee a nonzero block-aligned break size |

Three, not zero and not unlimited: the read descriptor is
`[excess-start scratch, request buffer, excess-final scratch]`, and the
scratch is one sub-page physical segment. Three folded groups cover the head
scratch and the request's first two physical segments, or, for a one-block
request, the head scratch, the request and the tail scratch; section 3 shows
that the tightest table case folds exactly 512 bytes. Zero would remove the
segment bound entirely; a smaller value fails the one-block cases. The
existing 64-block/32 KiB transfer limits still bound each subrequest, and the
adapter's `doAsyncReadWrite()` argument checks and the core chunk loop are
unchanged. The write segment advertisement is not set (read-only device), and
no write path is added. The property is set after the mode is parsed and
before `registerService()`, so `IOBlockStorageDriver::handleStart()` reads the
mode's value; the legacy modes keep the current `1` value.

The design does not change the request block size, the 64-block request limit,
the 32 KiB byte limit, the 2-sector CMD18 chunk, the controller clock, the bus
width, the per-command timeout, the report watchdog or the read-only
`doAsyncReadWrite()` contract.

## 5. Case table and reference digests

The deblock mode waits for exactly one whole-media `IOMedia`, validates the
read-only whole-media profile (absent `Base` accepted as zero, nonzero or
wrong-typed `Base` refused), opens the whole media as a reader, resolves the
boot partition by the pinned base `2,255,360 x 512` and size `196,608 x 512`
on demand, and performs the fixed case table below. It reuses the N10.1/N10.2
media discovery, profile checks, boot geometry constants, cancellation checks
and report-thread ownership; it does not reuse the partread case table or its
`blocks`/`from_end_blocks` representation.

`start` is the byte offset relative to the target media (`whole` or `boot`);
`length` is the requested byte count. The deblocker's aligned span is
`ceil((start + length) / 512) * 512 - floor(start / 512) * 512`.

| index | target | start | length | end | head | tail | span | blocks | branch |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | whole | 100 | 924 | 1024 | 100 | 0 | 1024 | 2 | head-only |
| 1 | whole | 0 | 600 | 600 | 0 | 424 | 1024 | 2 | tail-only |
| 2 | whole | 100 | 200 | 300 | 100 | 212 | 512 | 1 | both ends, same block (`_requestIsOneBlock`) |
| 3 | whole | 100 | 600 | 700 | 100 | 324 | 1024 | 2 | both ends, multi-block |
| 4 | whole | 100 | 16000 | 16100 | 100 | 284 | 16384 | 32 | both ends, multi-page span |
| 5 | whole | 512 | 1024 | 1536 | 0 | 0 | 1024 | 2 | aligned control (no deblocker) |
| 6 | boot | 4196 | 924 | 5120 | 100 | 0 | 1024 | 2 | boot base + head-only |
| 7 | boot | 1048676 | 200 | 1048876 | 100 | 212 | 512 | 1 | boot base + same-block |
| 8 | boot | 100630528 | 32668 | 100663196 | 0 | 100 | 32768 | 64 | boot base + tail-only + 64-block span |
| 9 | boot | 100662884 | 200 | 100663084 | 100 | 212 | 512 | 1 | boot base + same-block at media end |
| 10 | boot | 4096 | 1024 | 5120 | 0 | 0 | 1024 | 2 | boot base + aligned control |

Whole cases 0-5 lie inside the retained primary-GPT range (LBAs 0-33,
17,408 bytes). Boot cases 6-10 lie inside the 1,798,052-byte kernel section
(`4096..1,802,148`) or the retained 64-block boot tail; case 9 ends at
`BOOT_SIZE - 212`. Each case is one `IOMedia::read()` call with a descriptor
whose length is exactly `length`; the deblocker's excess bytes go to its own
scratch and are never part of the comparison.

Each case carries a pinned 32-byte SHA-256 of the exact requested bytes. The
implementation batch derives the digests from the retained out-of-band
evidence already used by N10.1/N10.2 (the GPT readbacks, the full
BOOT-partition backup and the gate-pinned v5 BOOT image kernel section),
verifies each range and digest on the host, and records the derivation in the
N10.5 candidate document. The design
does not invent digest values and does not print raw ranges, GUIDs, labels or
identifiers in the report; the report prints only the first 16 digest bytes,
the target, the bounded offset/length and counters.

## 6. Read protocol, bounds, cancellation and terminal ownership

Algorithm:

1. Wait up to `GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS` (20,000 ms) for exactly one
   whole media, checking `reportCancel` before every poll and sleeping only in
   the report thread. Zero or more than one candidate is a media failure
   (`reason=1`); a profile/geometry mismatch is also a media failure.
2. Open the whole media as a reader. A refused open is `reason=3`.
3. For each case in table order:
   - Check `reportCancel` and the read budget
     (`GTA4L_EMMC_BLOCK_DEBLOCK_READ_BUDGET_MS`, 4,000 ms) before the read. A
     cancellation latches `reason=9` and `cancel=1`; a budget stop latches
     `reason=8` and `budget=1`, matching the existing partread codes.
   - Resolve the boot partition on first use with the N10.1 deadline
     (`GTA4L_EMMC_BLOCK_PARTREAD_DEADLINE_MS`, 2,000 ms) and open it as a
     reader; an absent/ambiguous partition is `reason=2`, a refused open is
     `reason=3`.
   - Build a page-aligned `IOMemoryDescriptor::withAddress()` over the
     deblock bounce buffer with exactly `length` bytes and call
     `media->read(this, start, descriptor, nullptr, &actual)`. The
     synchronous `IOStorage::read()` wrapper waits for the deblocker's final
     completion; the report thread must not hold the controller gate across
     the call.
   - On a non-success status, latch `reason=5` and the raw status/actual.
     On `actual != length`, latch `reason=6`. Otherwise compute the SHA-256
     over the requested bytes and compare it to the pinned digest; a mismatch
     is `reason=7`. A pass increments `passed`; the first failure latches the
     failure index, reason, status, actual and digest.
   - A descriptor-allocation refusal is `reason=4`; an invalid table entry
     (zero length, out-of-range, unaligned arithmetic, span above the bound)
     is `reason=10`.
4. Continue the remaining cases after a read/short/hash/descriptor/argument
   failure so the counters attribute every case, exactly as N10.1/N10.2 do.
   Stop immediately on a media, partition, open or budget failure. No retry,
   fallback, reset, repair or write follows any failure.
5. On cancellation, set `reason=9`, `cancel=1`, `complete=0`, release the
   media and return through the existing no-panic cancellation path. The
   terminal-ownership rule (`reportLock`, `claimTerminal()`) is unchanged; the
   report thread or the watchdog emits the single final panic.
6. Release the boot and whole media on every exit path. The report thread
   never sleeps while holding the controller gate and never holds a lock
   across a media call.

The deblock bounce buffer is one page-aligned `IOMallocAligned` allocation
sized to `GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BYTES` (66 blocks, 33,792 bytes), which
covers the largest requested length (32,668) and the table's aligned spans.
The buffer is allocated only for `gta4l-emmc-deblock=1` and freed with
`IOFreeAligned()`; the legacy, partobs, partcmp and partread allocations are
unchanged.

Worst-case arithmetic: 20,000 ms media wait + 4,000 ms read budget + 2,000 ms
partition deadline + one nominal 1,000 ms in-flight read = 27,000 ms, below
the 30,000 ms watchdog, matching the N9.1/N10.1 bound. The deblocker's
internal thread calls add scheduling latency but no new unbounded wait; the
synchronous wrapper has no timeout of its own, so the recorded N3
pathological 64-sector gate-hold risk still applies to the 64-block case and
is not changed by this design.

## 7. Report lines and budget

When `gta4l-emmc-deblock=1` is present, emit two bounded lines in the existing
report:

```
GTA4L-EMMC P5 DEBLOCK: cases=11 passed=11 failed=0 reason=0 complete=1 reads=11 bytes=54364 span=56832 blocks=111 fails=0 cancel=0 budget=0 media=0 part=0 seg=3 cap=61071360
GTA4L-EMMC P5 DEBLOCKREAD: index=10 target=1 start=4096 length=1024 span=1024 actual=1024 status=0 digest=...
```

Field meanings:

- `cases`, `passed`, `failed`: the fixed table size and the case outcomes.
- `reason`: the first failure code (section 6); `0` on a full pass.
- `complete`: `1` only when `cases != 0`, `passed == cases`, `failed == 0`,
  `reason == 0`, `cancel == 0`, `budget == 0`, `media == 0`, `part == 0`,
  `seg == 3`, and every case's status/actual/digest passed. Otherwise `0`.
- `reads`: attempted case reads; `bytes`: requested bytes across attempted
  cases; `span`: aligned deblocker bytes across attempted cases; `blocks`:
  `span / 512`.
- `fails`: transport/copy failures; `cancel`, `budget`, `media`, `part`:
  the N10.1 failure flags.
- `seg`: the mode's advertised read segment count (3).
- `cap`: `capacityBlocks` (`61071360` on this device), as in N10.2.
- `DEBLOCKREAD`: the last case's index, target, requested start/length,
  aligned span, actual bytes, raw signed 32-bit status and the first 16 digest
  bytes. On a failure it identifies the first failing case through the
  `DEBLOCK` failure fields; the line remains bounded.

Budget and privacy rules:

- The host tests bound the `DEBLOCK` line at 384 bytes and `DEBLOCKREAD` at
  256 bytes, leaving the rest of the 2,048-byte report for the existing
  lines.
- N10.4 must format the complete report with the maximum field widths this
  device can produce and assert `length < 2048` before NUL termination. If
  the assertion fails, drop optional explanatory fields; never truncate a
  line or emit a partial report.
- The existing formatter fail-closed behavior is unchanged: if the writer
  cannot fit the report, the buffer becomes empty and the report path panics
  with the existing format-failure message.
- The line contains only bounded counts, offsets, lengths, statuses and a
  16-byte digest prefix; no GUIDs, labels, identifiers, serials or raw ranges
  are printed.

The expected full-pass line for the table above is `cases=11 passed=11
failed=0 reason=0 complete=1 reads=11 bytes=54364 span=56832 blocks=111
fails=0 cancel=0 budget=0 media=0 part=0 seg=3 cap=61071360`. The
implementation batch pins these values in the host tests; a later table change
requires a design update, not a silent counter change.

## 8. Host-test plan (N10.4 acceptance)

The existing host seam (`scripts/sim-gta4l-emmc-block/`) calls
`doAsyncReadWrite()` directly. N10.4 must extend it with a deblock model that
reconstructs the real path closely enough to exercise the adapter's new mode:

- The fake media/driver computes `head`, `tail`, `_requestIsOneBlock` and the
  aligned span from the requested start/length, builds the three-chunk read
  descriptor, reads the adapter's `kIOMaximumSegmentCountReadKey` property,
  applies the existing `getBreakSize()` model, and either returns
  `kIOReturnDMAError` for a zero break size or splits the aligned transfer
  into subrequests and calls the adapter with a fake `IOBreaker`-style
  descriptor.
- The fake descriptor implements `getLength()`, `getDirection()`,
  `getPhysicalSegment()` and `writeBytes()` over the chunk list, including
  the same-block scratch aliasing. The test verifies the caller buffer
  contains exactly the requested bytes and that the scratch contents are
  irrelevant.
- The host test asserts the advertised segment property is `1` for legacy,
  partobs, partcmp and partread modes and `3` for deblock mode.

Required scenarios:

1. Each of the eleven cases with the model at `seg=3`; exact requested bytes,
   expected `passed=11 failed=0 reason=0 complete=1 reads=11 bytes=54364
   span=56832 blocks=111 fails=0`.
2. The same table at `seg=1`: every head-unaligned case (0, 2, 3, 4, 6, 7, 9)
   must return `kIOReturnDMAError` before the adapter; the tail-only and
   aligned cases still pass. This is the anti-weakening control that proves
   the advertisement change is necessary.
3. The same table with the page-aligned bounce removed: the head-unaligned
   cases must fail through the model, and the aligned controls must still
   pass.
4. Wrong head/tail discard, a shifted copy-out (write the aligned bytes at
   the wrong offset), a tail written into the request buffer, and a
   same-block aliasing mutation; each is rejected by the exact-byte compare.
5. Read failure at each case position (`kIOReturnIOError`,
   `kIOReturnDMAError`, `kIOReturnNoMedia`), short transfer (`actual=0`,
   `actual=length-1`, `actual=length+1`), digest mismatch and descriptor
   allocation failure; the first reason latches, later cases continue for
   read/short/hash/descriptor failures, and `complete=0`.
6. Budget exhaustion before a case; cancellation before the media wait,
   between cases and after a read; media absent, two whole media, wrong
   profile, boot partition absent/ambiguous, and open refused; each produces
   the expected flag and no panic claim.
7. Watchdog race: the watchdog claims the terminal while the report thread is
   blocked in a deblock read; the report thread must not panic twice or
   overwrite the watchdog report.
8. Selector controls: orphan, duplicate, wrong value, and each
   partobs/partcmp/partread combination refused; the absent-token legacy
   report is byte-identical; the accepted `emmc-block-1`/`emmc-block-3`
   payloads are unchanged.
9. Report controls: worst-case field widths fit 2,048 bytes; a deliberately
   small buffer returns 0 and takes the existing format-failure path; the
   `DEBLOCK` and `DEBLOCKREAD` line caps hold; no raw identifier appears.
10. Mutation controls beyond the `seg=1` case: accept a short transfer,
    accept a non-success status, skip the digest compare, accept a wrong
    target/range, skip the budget or cancellation check, force
    `complete=1`, revert the segment advertisement to 1, use the caller's
    class-member buffer instead of the page-aligned bounce, and skip the
    completion wait. Each must be rejected by the suite.

The host model is a reconstruction; it does not run the real
`IOBlockStorageDriver`/`IODeblocker` code. The hardware run is the only
qualification of the real deblock/breakup layers, and the design does not
claim otherwise.

## 9. Implementation surface and approval gates

N10.4, if approved, changes:

- `package/Gta4lEmmcBlock.h`/`.c`: the new token, selector, case table,
  reason codes, report fields, formatter and the static asserts for the
  2,048-byte budget and the 33,792-byte bounce bound; no transport, width,
  timing, DMA/CQE/ICE, write or request-limit change.
- `package/Gta4lEmmcBlock.cpp`: the opt-in mode parse, the mode-scoped
  `kIOMaximumSegmentCountReadKey` value, the page-aligned deblock bounce,
  the case loop, media/partition resolution, cancellation and terminal
  handling. `doAsyncReadWrite()`, `copyOutCallback()` and the core chunk loop
  are unchanged.
- `scripts/build-gta4l-diagnostic-payload.py`,
  `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py`:
  the `emmc-block-1-deblock`/`emmc-block-3-deblock` personalities, token
  validation and the partobs/partcmp/partread refusal.
- `scripts/sim-gta4l-emmc-block/*` and `scripts/test-gta4l-emmc-block.py`:
  the deblock model, the scenarios and the mutation controls above.
- Documentation refresh after implementation (`docs/emmc-driver.md` next
  section, `README.md`, `porting.toml`, `docs/evidence-baseline.md`) with the
  evidence-count checks and the hash table kept consistent.

The guarded KernelConfigTables personality patch, the archive builder's
symbol set and the storage-ABI bindings are expected to need no change: the
new token and report fields are data, and the override set is unchanged. The
N10.5 builder must still confirm zero new unbound symbols and a candidate-
matched audit.

Gates:

- N10.4 implementation: owner approval and Build mode. Host tests only; no
  candidate build, ABI/QEMU audit, device access, flash or boot.
- N10.5 archive/ABI/kernel/payload/BOOT production: separately gated,
  host-only, with the storage-ABI, binding, payload, BOOT-gate and
  evidence-count checks. No device.
- N10.6 hardware: fresh explicit BOOT-only approval, verified protected
  backups and readbacks, and one owner-controlled cold boot. No retry,
  reflash, rollback, reset/repair, mount, root switch, write, wider/faster
  mode, DMA/CQE/ICE or request-limit change. The one-boot budget is consumed
  by the run.
- Initialization reliability remains a separate finite, predeclared
  investigation; N10.3 does not measure it.

## 10. Limits and non-goals

- N10.3 qualifies a fixed, bounded same-boot table of unaligned reads on one
  cold boot. It does not qualify arbitrary byte offsets or lengths, arbitrary
  I/O, filesystem mount, root integration, writes, partition semantics
  beyond the pinned bytes, wider/faster modes, DMA/CQE/ICE or initialization
  reliability.
- The three-segment advertisement is mode-scoped. It is not a general
  request-limit increase, and it does not change the accepted N10.1/N10.2
  images or their counters. The legacy/partread/partcmp/partobs modes keep
  the one-segment advertisement.
- The host deblock model is a reconstruction. Physical adjacency and the
  real `IODeblocker` thread-call timing remain host reconstructions. The table
  does not deterministically force the real `IOBreaker` split: every aligned
  span is at most 32,768 bytes, so a physically contiguous request buffer
  takes the direct `executeRequest()` short-circuit, while a fragmented one
  may enter the breaker. The N10.6 run qualifies whichever path the allocation
  produces; a deterministic breaker-split case is left to a future rung.
- The N10.2 `complete=1`, the N10.1 hardware result and its captured
  repeatability series, the N9.10 comparator result, and the historical
  N8.5/N9.6/N9.7/N9.8 `complete=0` records are preserved unchanged. A future
  N10.6 `complete=1` is a new, exact same-boot claim with the scope in
  section 7; it does not upgrade any earlier result.
- A read error, short transfer or digest mismatch is a measurement failure of
  that case, not a card verdict. A cancellation, budget stop or absent media
  is a bounded observation limit, not a failure-rate estimate.
- No commit or push is part of the N10.3 design batch. The design document
  itself is committed only as part of a separately approved batch.

## 11. Decision requested

The owner is asked to approve the N10.4 implementation scope:

1. the opt-in `gta4l-emmc-deblock=1` token with the strict conflict rules;
2. the mode-scoped `kIOMaximumSegmentCountReadKey = 3` advertisement, with
   the legacy/partobs/partcmp/partread modes kept at `1`;
3. the eleven-case table, the pinned-digest derivation from the retained
   evidence and the `DEBLOCK`/`DEBLOCKREAD` report lines;
4. the host deblock model, the `seg=1` anti-weakening control and the
   mutation controls in section 8.

Approval of this design does not authorize the N10.5 candidate build or the
N10.6 hardware run; those remain separately gated.
