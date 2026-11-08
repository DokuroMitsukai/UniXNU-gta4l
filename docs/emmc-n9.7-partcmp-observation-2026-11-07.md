# gta4l N9.7 exact-comparator observation (2026-11-07)

Status: BOOT-only flash of the N9.7 corrected GUID-scheme-lookup exact-comparator
candidate and one owner-controlled first cold boot on 2026-11-07. The report
selected the real whole media and found the attached `IOGUIDPartitionScheme`
(`whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0`), so
the N9.7 service-plane child lookup works on hardware, but the first comparator
read of LBA 0/512 bytes failed (`gpt=0 reason=8 reads=1 blocks=1 fails=1
samples=0 complete=0`). The owner returned the tablet to TWRP; the one-boot
budget is consumed. No commit, push, retry, reflash or repair was performed.
This is not a partition-completeness or reliability result, and the failure
cause remains unresolved because the report discards the raw `IOReturn` and
actual byte count.

## 1. Flash and boot scope

The owner approved the BOOT-only write in this session. The flash record is in
`out/gta4l/ram-root-emmc-n9.7-hardware-evidence/` (`flash-record.json`
`ecf652c9…`, `evidence-sha256.txt` `c35f25d3…`; the manifest verifies).

- BOOT image `74a4c687…` (85,688,320 bytes), payload `79c83d67…`, prelinked
  kernel `06826fc5…`, packaged kernel `61b9cea0…`, requalified archive
  `6a6af596…`, candidate build report `e77ae31d…`.
- Candidate source `Gta4lEmmcBlock.cpp` `0bc83206…`, policy source
  `Gta4lEmmcBlock.c` `417ea291…` and header `Gta4lEmmcBlock.h` `ce30dfff…`.
  The build report binds those three hashes to the objects and archive.
- Only `/dev/block/mmcblk0p69` was written (20,920 x 4096 records, synced).
  The full readback `aaaf8f03…` matched the expected candidate-prefix plus
  unchanged-tail value; prefix `74a4c687…`, tail `a28b4ea2…`. RECOVERY
  `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` were unchanged, and
  the protected-partition backups were verified before the write.
- Rollback `boot-before-n9.7.img.xz` (`f7e5ac3c…` -> raw `9c5bcf2c…`) preserves
  the previous N9.6 post-flash image. It was not restored.

The owner supplied the wrapped report and confirmed TWRP return. The raw text,
a bounded interpretation and a separate non-self-hashing manifest are in
`out/gta4l/ram-root-emmc-n9.7-hardware-evidence/observation-1/` (raw
`bb984f90…`, interpretation `b38255c7…`, record `21a39d86…`; the manifest binds
the unchanged `evidence-sha256.txt` `c35f25d3…`). The report is associated with
the flashed image through the flash record, not an independently measured
running-image hash; the boot receipt time is not the boot timestamp.

## 2. What the report shows

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512
bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320
content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 PARTMAP: gpt=0 reason=8 ... reads=1 blocks=1 fails=1
cancel=0 complete=0
```

- `rc=0 stop=0 mode=3` is header success, not comparator success.
- The MEDIA fields are populated only after exactly one qualifying whole medium
  is selected. `whole=1 base=0 size=31268536320
  content=GUID_partition_scheme writable=0` therefore records a successful
  whole-media selection, and `base=0` is the implied zero base rather than a
  published `Base` property.
- The N9.7 source order (`package/Gta4lEmmcBlock.cpp:1779–1795`) establishes
  that the service-plane child enumeration, the class/name match and the
  reader open all passed before the first `readPartmapSectors()` request. The
  N9.6 reason-10 lookup failure is not repeated.
- `reason=8` is `GTA4L_EMMC_BLOCK_PARTMAP_REASON_READ`
  (`package/Gta4lEmmcBlock.h:276`). `readPartmapSectors():1345–1374` returns it
  only when `media->read` returns a non-success status or an `actual` count
  other than 512. `reads=1 blocks=1` confirms exactly one LBA 0/one-sector
  comparator read was attempted; `fails=1` is the failure reporter, not an
  independent transport measurement.
- GPT validation and the exact range comparison never began. `gpt=0` is not
  evidence of a corrupt GPT, and `media=0` is not a measured partition count.
- `READ`/`QUAL`/`QUAL3` show no controlled qualification attempt in this run.
- Driver aggregate: online=1, wp=1, 9 requests/29 blocks, fails=0, maximum
  request 8 blocks. The transport is consistent (29 = 7 CMD17 + 22 CMD18
  blocks, 11 CMD18 commands and 11 stops, no command or stop failures).
  These are incidental/scan requests, not the comparator read; aggregate
  counters cannot prove whether the comparator request entered
  `doAsyncReadWrite`.
- Capacity remains 61,071,360 x 512 = 31,268,536,320 bytes; `wid=0 tim=0
  clk=400000` retains the 1-bit legacy timing policy. `SNAP locked=1`; FAIL is
  valid=0/all-zero. Frames/pages/free add no storage qualification.
- The supplied panic suffix is `@Gta41EmmcBlock.cpp:2000` (spelling preserved).
  In the candidate source `0bc83206…`, line 2000 is the intentional
  `panic("%s", reportBuffer)` in `panicWithReport()`. Header success and FAIL
  valid=0 are consistent with the intentional report stop, not a newly
  inferred crash.

## 3. Source-grounded analysis (host-only, cause unresolved)

The comparator read is
`IOMemoryDescriptor::withAddress(partmapPmbr, 512, kIODirectionIn)` ->
`IOMedia::read` -> `IOBlockStorageDriver::read` -> `prepareRequest` ->
`deblockRequest` (block-aligned short-circuit) -> `breakUpRequest` ->
`IOBreaker::getBreakSize`.

Pinned facts:

- The flashed object's DWARF gives class `Gta4lEmmcBlock` byte size `0x3658`
  (13,912) with `partmapPmbr` at `0x0e18` (3,608). The archive hash `6a6af596…`
  matches the flashed candidate, so this is the flashed layout.
- The driver advertises `kIOMaximumBlockCountReadKey=64`,
  `kIOMaximumByteCountReadKey=32768` and `kIOMaximumSegmentCountReadKey=1`
  (`package/Gta4lEmmcBlock.cpp:684–697`). The pinned `IOBlockStorageDriver`
  loads the segment key into `_maxReadSegmentTransfer`
  (`IOStorageFamily-67f9003c/IOBlockStorageDriver.cpp:1327–1340`).
- The object is allocated through `OSObject::operator new` ->
  `kheap_alloc(KHEAP_DEFAULT, 13,912, …)` ->
  `kalloc_zone_for_size_with_flags` (`libkern/c++/OSObject.cpp:295`,
  `osfmk/kern/kalloc.c:241–310`). On arm64 `KHEAP_MAX_SIZE` is 32 KiB
  (`osfmk/kern/kalloc.h:364`); 13,912 falls in the 16,384 size class. Because
  16,384 is a page multiple, `zone_get_min_alloc_granule()` uses the element
  size as the allocation granule (`osfmk/kern/zalloc.c:9380–9395`), so the
  element base is page-aligned. KASAN is disabled in this build
  (`-fno-sanitize=address -U KASAN`), so there is no redzone offset.
- With a 4 KB page and a page-aligned base, `partmapPmbr` starts at page
  offset 3,608. `IOGeneralMemoryDescriptor::getPhysicalSegment(…,
  kIOMemoryMapperNone)` is page-limited
  (`iokit/Kernel/IOMemoryDescriptor.cpp:3347–3456`), so the buffer's first
  physical segment is 488 bytes (to the page end) and the second is 24 bytes
  on the next page.
- `IOBreaker::getBreakSize()` counts the first segment, then folds the second
  only when it is physically contiguous or `IOMapper::gSystem != NULL`
  (`IOBlockStorageDriver.cpp:2896–2903`). This platform has no system IOMMU:
  `IOMapper::gSystem` is `kNoMapper` (0) unless the platform expert sets a
  mapper (`iokit/Kernel/IOMapper.cpp:63,143–150`; `iokit/IOKit/IOMapper.h:67–70`),
  so a non-contiguous second page is not folded. When the second segment is
  not foldable, the `segmentCount == withMaximumSegmentCount` check breaks the
  loop (`:2975–2980`). `breakSize` is then 488, and `IOTrunc(488, 512)`
  returns 0 (`:2988`).
- `breakUpRequest()` completes the request with `kIOReturnDMAError` when
  `breakSize == 0` (`:3272–3276`), before `executeRequest()` and before
  `getProvider()->doAsyncReadWrite`. The adapter core helper increments
  `block->counters.requests` only after its validation
  (`package/Gta4lEmmcBlockCore.c:127`), and the adapter calls it only after its
  own validation (`package/Gta4lEmmcBlock.cpp:814–859`). The comparator's
  direction, block count, length, capacity and online state all pass those
  checks, so the request would have reached the helper if the driver pipeline
  had forwarded it. This path explains a reason-8 signature with no adapter
  counter movement.

Residual uncertainty: the host cannot observe the physical adjacency of the
two pages. If those two pages were physically contiguous, the fold would
produce `breakSize=512` and the read would proceed. The observed failure is
consistent with the non-contiguous case and rules out an allocation whose
buffer does not straddle a page; the physical-adjacency input itself is not
measured. Allocation failures in `prepareRequest` or in `IOBreaker` are the
main alternative early exits that would also produce reason 8; no evidence
selects them, while the page-straddle path is deterministic given the flashed
layout, the advertised one-segment constraint and a 4 KB page. This is a
host-grounded hypothesis, not a measured root cause.

The host seam (`scripts/sim-gta4l-emmc-block/adapter_support.cpp:838–922`)
calls `doAsyncReadWrite` directly and does not execute the real
context/deblock/physical-segment/breakup path, so its passes do not qualify
those layers.

## 4. What this does not establish

- No exact GPT-to-`IOMedia` comparison, no GPT validation and no range result.
- No partition-completeness claim. N8/N8.5's historical `complete=0` is not
  upgraded.
- No initialization-reliability result; the one boot is a single bounded
  diagnostic run.
- No statement about wider/faster bus modes, DMA/CQE/ICE, arbitrary I/O,
  mount/root switch or microSD.
- No claim that the media or its partitions were absent; the comparator
  stopped at the first GPT read.

## 5. Next steps and gates

The next host-only batch should add a host control that distinguishes the
page-straddle path from allocation failure and pins the signature on the
adapter seam. Candidate corrections for owner approval, smallest first:

1. Stage the comparator reads through a page-aligned bounce buffer (for
   example `IOBufferMemoryDescriptor::withCapacity(512, kIODirectionIn)` or an
   `IOMallocAligned` scratch) and copy into `partmapPmbr`/`partmapHeader`
   after success. This removes the straddle without changing the class size or
   the advertised limits.
2. Remove or raise `kIOMaximumSegmentCountReadKey` so `getBreakSize()` can
   count both page segments; the adapter copies through
   `IOMemoryDescriptor::writeBytes`, so it does not require physical
   contiguity. This is the smallest code change but weakens the advertised
   request constraint.
3. Page-align the member buffers (`alignas(4096)` or an aligned scratch
   struct). This adds class bytes and must stay within the 16 KiB
   typed-allocator limit that N9.3 already hit.

The diagnostic-first proposal also remains open: preserve reason=8
compatibility and add explicit attempt/validity state, requested LBA,
blocks/bytes, the full 32-bit raw `IOReturn`, `UInt64` actual bytes and
descriptor length/direction so the next observation can resolve the cause
instead of discarding it. No driver correction, candidate build, ABI/QEMU
audit, device access, flash, boot, retry, reset/repair, commit or push is
authorized or performed by this record. The N9.7 boot budget is consumed; any
new boot needs a fresh explicit owner approval, verified protected
backups/readbacks and a new predeclared one-boot budget.

## 6. Evidence

Raw artifacts and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n9.7-hardware-evidence/` directory. The observation
subdirectory binds the original flash manifest by hash and does not hash
itself. The repository records only the sanitized summary and the bounded
interpretation.
