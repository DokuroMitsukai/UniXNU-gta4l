# gta4l N9.9 page-aligned comparator read buffer (2026-11-08)

Status: host-only correction of the exact GPT-to-`IOMedia` comparator read
path. The change removes the caller-layout dependency that produced the N9.8
`kIOReturnDMAError` before the adapter. No candidate build, storage-ABI/QEMU
audit, device access, flash, boot, retry, reset/repair, commit or push was
performed. Host acceptance is not permission to flash or boot, and this is not
a partition-completeness or reliability result.

## 1. Scope and outcome

N9.8 established `READDETAIL state=2 lba=0 blocks=1 bytes=512
status=3758097108 actual=0 desc_len=512 desc_dir=1` on hardware; `3758097108`
is `0xe00002d4` (`kIOReturnDMAError`). The host trace selected
`IOBlockStorageDriver::breakUpRequest()` with `getBreakSize()==0` as the only
pre-adapter DMA-error exit under the advertised one-segment read limit. The
target `partmapPmbr` member sits at `0x0e18` (3,608) in a page-aligned 16 KiB
kalloc class, so a 512-byte read spans a 488-byte first segment and a 24-byte
second segment; the first segment truncates to zero request blocks.

N9.9 corrects the read path without changing the advertised limits: every
comparator read is issued from one page-aligned bounce buffer and copied to
the caller only after a full successful transfer.

## 2. Correction

- `GTA4L_EMMC_BLOCK_PARTMAP_DMA_BYTES` is
  `GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS * GTA4L_EMMC_BLOCK_BYTES` (1,024
  bytes), the largest comparator read (`package/Gta4lEmmcBlock.h:74`).
- Comparator mode allocates the buffer once in `start()` with
  `IOMallocAligned(size, PAGE_SIZE)` (`package/Gta4lEmmcBlock.cpp:653-654`)
  and releases it in `free()` with `IOFreeAligned` (`:775-778`). An allocation
  failure refuses the driver start; the legacy and partobs paths never
  allocate it.
- `readPartmapSectors()` selects the bounce for requests of at most 1,024
  bytes (`:1395-1397`), builds the `IOMemoryDescriptor` from it, and copies
  the bytes to the caller only when `status == kIOReturnSuccess` and
  `actual == requested` (`:1444-1447`). A failed or short transfer leaves the
  destination untouched and still latches the bounded `READDETAIL` outcome.
- The one-segment read limit, the request block size, the chunk size and the
  `GTA4L-EMMC P5 READDETAIL` field set are unchanged.

## 3. Host qualification

The host seam now models the worst-case breakup constraint reconstructed in
`docs/emmc-driver.md` §5.3.69: each virtual page is a separate physical
segment, only the first segment is taken, the accumulated size is truncated to
the 512-byte request block size, and a zero break size returns
`kIOReturnDMAError` before the adapter
(`scripts/sim-gta4l-emmc-block/adapter_harness.cpp:217-245`). The model is
enabled per scenario so the existing scenarios keep their previous meaning.

- `partcmp-page-aligned` runs the full 25-read comparator with the model
  enabled and requires zero rejected requests, zero unaligned descriptors,
  `gpt=1 reason=0 media=80 match=1 complete=1` (`:1505-1519`).
- `partcmp-page-split` calls `readPartmapSectors()` directly with a
  destination at page offset 3,608, mirroring the target `partmapPmbr`
  placement, and requires a successful read with the exact PMBR copy
  (`:1522-1540`).
- The new `partcmp-bounce-skip` adapter mutation restores the caller buffer
  and is rejected by `partcmp-page-split` with the modeled DMA error
  (`scripts/test-gta4l-emmc-block.py:498-501`).

Results: block suite 48 core scenarios/38 mutation controls and 99 adapter
scenarios with 24 adapter and eight policy mutation controls; the
evidence-count guard passes (core 187/115, probe 196/100); `git diff --check`
is clean.

## 4. Limits and gates

- The host seam still calls `doAsyncReadWrite` directly. The breakup model is
  a host reconstruction, not a qualified run of the real context, deblock or
  breakup layers.
- Physical adjacency of the two page segments remains unmeasured. The bounce
  makes the result independent of it, but the hardware outcome is not
  measured by this batch.
- No candidate was built, audited, flashed or booted and no device was
  accessed. The exact 80-entry/84-slot GPT comparison and the direct media
  publication are not measured on hardware.
- N8.5's historical `complete=0` and the N9.6/N9.7/N9.8 verdicts are not
  upgraded. Initialization reliability remains unresolved.
- A fresh explicit BOOT-only approval, verified protected-partition backups
  and readbacks, and one owner-controlled boot are required before any
  hardware run. The previous boot budget is consumed.
- No mount or root switch, no writes, no wider/faster modes, no DMA/CQE/ICE
  and no request-limit changes are part of this correction.
- No commit or push is part of N9.9.

## 5. Evidence

The host test output is reproducible with
`python3.11 -B scripts/test-gta4l-emmc-block.py` and
`python3.11 -B scripts/test-gta4l-emmc-evidence-counts.py`. The repository
records the bounded interpretation; no private evidence directory is added by
this host-only batch.
