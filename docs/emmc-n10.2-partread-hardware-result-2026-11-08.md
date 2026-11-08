# gta4l N10.2 read-only IOMedia content qualification hardware result (2026-11-08)

Status: BOOT-only flash of the N10.2 read-only content-qualification candidate
and one owner-reported first cold boot on 2026-11-08. The report selected the
read-only whole media and the pinned boot partition, performed the fourteen
fixed range reads, and emitted `cases=14 passed=14 failed=0 reason=0 complete=1
reads=14 blocks=334 bytes=171008 fails=0 cancel=0 budget=0 media=0 part=0
cap=61071360`. Every pinned digest matched. An independent read-only post-boot
comparison then confirmed that the stored BOOT is exactly the flashed
candidate plus the preserved pre-flash tail, that RECOVERY/EFS/sec_efs are
unchanged, and that the pulled prefix and extracted payload pass the host
gates. The owner returned the tablet to TWRP; the one-boot budget is consumed.
No commit, push, retry, reflash, rollback, mount or repair was performed. This
is a bounded same-boot content qualification of fourteen pinned ranges, not an
initialization-reliability or general I/O result.

## 1. Flash and boot scope

The owner approved the BOOT-only write in this session and controlled the one
cold boot. The flash record is in
`out/gta4l/ram-root-emmc-n10.2-hardware-evidence/` (`flash-record.json`
`e14b9fb2…`, flash evidence manifest `evidence-sha256.txt` `5b725de6…`).

- BOOT image `ca1789f0…` (85,688,320 bytes), payload `462870cd…` (83,886,080
  bytes), prelinked kernel `e7a1982b…`, packaged kernel `d987c350…`,
  requalified archive `2ea638ef…`, candidate build report `8beb7190…`.
- Candidate source `Gta4lEmmcBlock.cpp` `6a11aa76…`, policy source
  `Gta4lEmmcBlock.c` `62d1d706…` and header `Gta4lEmmcBlock.h` `b3bb2c95…`.
  The build report binds those hashes to the objects and archive.
- The pre-flash p69 was the N10.1 post-flash image `cc3c8492…`; its tail after
  the 85,688,320-byte candidate offset was `a28b4ea2…`.
- Only `/dev/block/mmcblk0p69` was written (20,920 x 4096 records, synced).
  The post-flash full readback `4225defb…` matched the expected
  candidate-prefix plus unchanged-tail value; prefix `ca1789f0…`, tail
  `a28b4ea2…`. RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…`
  were unchanged, and the live protected-partition hashes were verified
  against the recorded values before the write. The N10.1-era `gta4l-dump`
  SHA256SUMS sets were no longer present on the flash host; the write touched
  p69 only.
- Rollback `boot-before-n10.2.img.xz` (`f140f1e5…` -> raw `cc3c8492…`)
  preserves the previous N10.1 post-flash image. It was not restored.

The owner supplied the wrapped report and confirmed the TWRP return. The raw
screen transcription is in the parent directory (`d2dff203…`); the bounded
interpretation, structured observation record and pulled gate reports are in
`out/gta4l/ram-root-emmc-n10.2-hardware-evidence/observation-1/`
(interpretation `c5ff550f…`, record `25da15a6…`; the observation manifest
`observation-sha256.txt` `840b2721…` binds the extended `evidence-sha256.txt`
`5b725de6…`). The report is associated with the flashed image through the
flash record, not an independently measured running-image hash. The transcript
is a screen transcription, not a captured kernel log.

## 2. What the report shows

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 DRV: online=1 wp=1 reads=58 blocks=363 fails=0 max=8
GTA4L-EMMC P5 XFER: req=363 c17=13 c18=175 c18b=350 cfail=0 stops=175 sfail=0 last_stop=0
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 PARTREAD: cases=14 passed=14 failed=0 reason=0 complete=1 reads=14 blocks=334 bytes=171008 fails=0 cancel=0 budget=0 media=0 part=0 cap=61071360
GTA4L-EMMC P5 READTEST: index=13 target=1 blocks=64 off=1048576 ok=1 status=0 actual=32768 digest=050f2b0d7b20ccb58cc4299d18dde572
GTA4L-EMMC P5 READFAIL: index=0 reason=0 status=0 actual=0 digest=00000000000000000000000000000000
GTA4L-EMMC P5 PART: count=0 expected=0 waited_ms=0 error=0 reported=0
GTA4L-EMMC P5 READ: lba=1 attempted=0 ok=0 bytes=0 sum=0 base=2228245566 same=0
GTA4L-EMMC P5 QUAL: ref=0 ref_lba=0 ref_blocks=0 two_att=0 two_ok=0 two_reason=0 two_bytes=0 two_mismatch=0 two_off=0 two_match=0 two_c18=0 two_c17=0 two_stops=0 two_sfail=0
GTA4L-EMMC P5 QUAL3: att=0 ok=0 reason=0 bytes=0 mismatch=0 off=0 match=0 c18=0 c17=0 stops=0 sfail=0
GTA4L-EMMC P5 SNAP: locked=1
GTA4L-EMMC P5 FAIL: valid=0 stage=0 code=0 block=0 error=0
```

- `rc=0 stop=0 mode=3` is header success, not partread success. The MEDIA
  fields repeat the N9.10 and N10.1 read-only whole-media profile: `whole=1
  base=0 size=31268536320 content=GUID_partition_scheme writable=0`. `base=0`
  is the implied zero base, not a published `Base` property.
- `cases=14` is the fixed case count in `package/Gta4lEmmcBlock.c:135-286`.
  `passed=14 failed=0 reason=0` means every pinned case completed and matched
  its retained digest. The fourteen ranges and their expected first 16 digest
  bytes are:

| index | target | range | blocks | expected SHA-256 (first 16 bytes) |
| --- | --- | --- | --- | --- |
| 0 | whole | LBA 0 (PMBR, primary header and primary array) | 34 | `0c7e9c03ce25232f6ac1e588268dcda1` |
| 1 | whole | last 34 blocks (backup header and array) | 34 | `41912e59e66ee2c9a435fe17250d20c8` |
| 2 | whole | LBA 0 (PMBR only) | 1 | `a483d593b074c9e7da9d7c434c90038b` |
| 3 | whole | LBA 1 (primary GPT header only) | 1 | `efd3556b761bf5027d5cc6b0c4b7d3e6` |
| 4 | whole | LBA 2, 32 blocks (full primary array) | 32 | `ecde9f6fc8d58bb6c749bf1d7fe4a6de` |
| 5 | whole | LBA 34 (first usable LBA) | 1 | `2ddc82b7b3fb5b8b90c0e610d2b6eae0` |
| 6 | whole | 32 blocks at `capacity - 33` (full backup array) | 32 | `ecde9f6fc8d58bb6c749bf1d7fe4a6de` |
| 7 | whole | last block (backup GPT header) | 1 | `5ea6f4bf1518790b25d659ceefb56fe5` |
| 8 | boot | `BOOT_SIZE - 32768` (maximum 64-block request) | 64 | `ef313f712a8f436e989ce73766b49cc6` |
| 9 | boot | `BOOT_SIZE - 1536` (odd three-block tail) | 3 | `02acbb3a7b14ae9929eb10cd1ad7ba59` |
| 10 | boot | `BOOT_SIZE - 1024` (even two-block tail) | 2 | `d737414c433ea3c74750f380ca18bd06` |
| 11 | boot | `BOOT_SIZE - 512` (singleton) | 1 | `400039fd62daf3499b377ced75428918` |
| 12 | boot | offset 4096 (kernel section page 1) | 64 | `979df1ff230241b43862f8907c3f42d9` |
| 13 | boot | offset 1048576 (kernel section 1 MiB) | 64 | `050f2b0d7b20ccb58cc4299d18dde572` |

- `reads=14 blocks=334 bytes=171008` is exactly the fourteen cases:
  34 + 34 + 1 + 1 + 32 + 1 + 32 + 1 + 64 + 3 + 2 + 1 + 64 + 64 = 334 blocks
  and 334 x 512 = 171,008 bytes. Each case issues one media read through the
  page-aligned 32 KiB `IOMallocAligned` bounce buffer; the driver's eight-block
  request bound splits the 32-, 34- and 64-block cases into multiple transport
  requests.
- `complete=1` is the source guard at `package/Gta4lEmmcBlock.cpp:2297-2299`:
  `budget == 0`, the latched reason is `NONE`, `failed == 0`, `passed == cases`
  and `cases != 0`. It cannot coexist with a read failure, short transfer,
  hash mismatch, cancellation, budget refusal or media/partition/open failure.
- `media=0 part=0` are clear failure flags, not success flags. In
  `package/Gta4lEmmcBlock.cpp:1970-1973`, `partread_media` is set to `1` only
  when the latched reason is `GTA4L_EMMC_BLOCK_PARTREAD_REASON_MEDIA` (1), and
  `partread_part` is set to `1` only when it is
  `GTA4L_EMMC_BLOCK_PARTREAD_REASON_PART` (2). Both zero therefore means
  neither failure occurred.
- `cap=61071360` is the whole-media capacity in 512-byte blocks
  (31,268,536,320 bytes), matching the `MEDIA size`.
- `READTEST: index=13 target=1 blocks=64 off=1048576 ok=1 status=0
  actual=32768 digest=050f2b0d7b20ccb58cc4299d18dde572` is the final
  kernel-section case: boot offset 1,048,576, 64 blocks, `kIOReturnSuccess`,
  32,768 actual bytes, and the first 16 digest bytes of pinned case 13.
  READTEST records the most recent case, not an aggregate; the aggregate is
  `reads=14 blocks=334`.
- `DRV reads=58 blocks=363 fails=0 max=8` and `XFER req=363 c17=13 c18=175
  c18b=350 cfail=0 stops=175 sfail=0 last_stop=0` are whole-boot aggregate
  counters that include the fourteen partread reads. They are not additive
  with `PARTREAD reads=14 blocks=334`: the partread counters count only the
  pinned cases. `XFER req` is a requested-block count, not a request count; it
  equals `DRV blocks=363`. `max=8` is the driver's eight-block request bound.
- The zero `READFAIL` and `PART`/`READ`/`QUAL`/`QUAL3` lines are inactive
  legacy mode-1/mode-3 and N8 `PARTOBS` paths in this partread boot, not
  failures or new passes. `SNAP locked=1` and `FAIL valid=0` are the unchanged
  snapshot and failure latch.
- The supplied panic suffix is `@Gta41EmmcBlock.cpp:2642` (spelling preserved).
  In the candidate source `6a11aa76…`, line 2642 is the intentional
  `panic("%s", reportBuffer)` in `panicWithReport()`; it is the normal
  terminal report stop, not a fault.

## 3. Independent post-boot integrity (read-only)

The boot budget was already consumed when the owner returned the tablet to
TWRP. A read-only host collection through the established private flash-host
connection then compared the stored state with the flash record. It did not
write, mount or reboot. Private identifiers are not recorded.

- BOOT p69 full `4225defb…` matched the post-flash record. The first
  85,688,320 bytes matched the candidate `ca1789f0…`, and the remaining
  14,974,976 bytes matched the preserved pre-flash tail `a28b4ea2…`.
- RECOVERY p70 `14e11370…`, EFS p64 `51fa824c…` and sec_efs p63
  `3a265d58…` were unchanged.
- The pulled 85,688,320-byte prefix re-hashed to `ca1789f0…`; the payload
  extracted at the boot-header ramdisk offset (1,802,240) re-hashed to
  `462870cd…`.
- `scripts/check-gta4l-boot-image.py` passed 18/18 checks on the pulled
  prefix against the hardware-tested `ce4d76e8…` v5 baseline: header page
  excluding the ID, header fields, firmware, boot DTB and kernel section
  match, the ramdisk matches the expected payload, kernel padding is zero and
  there is no unexpected trailing data.
- `scripts/check-gta4l-payload.py` passed 18/18 checks on the extracted
  payload for `emmc-block-3-partread`: exactly one partread token with value
  1, no partobs/partcmp combination, one block token, raw ramdisk layout and
  trust-cache placeholder match.

These gates verify the pulled bytes independently of simple source-image
equality. The sanitized command/result record is
`out/gta4l/ram-root-emmc-n10.2-hardware-evidence/postboot-integrity-2026-11-08.txt`
(`8e092ae4…`); the gate reports are
`observation-1/pulled-boot-gate.json` (`43b47bf7…`) and
`observation-1/pulled-payload-check.json` (`37bdeb24…`).

## 4. What this does not establish

- One owner-reported cold boot, transcribed from the screen. There is no
  captured kernel log, boot timestamp, rail-reset state or independently
  measured running-image hash; the report is bound through the flash record.
- The result qualifies only the fourteen pinned ranges on this one boot. It
  does not qualify arbitrary I/O, filesystem mount, root integration, writes,
  wider or faster modes, DMA/CQE/ICE or initialization reliability.
- The boot-tail cases 8-11 address the end of the full 100,663,296-byte boot
  partition, which the 85,688,320-byte BOOT image does not reach. Cases 12-13
  are inside the gate-pinned 1,798,052-byte kernel section and are invariant
  across candidates.
- The independent readback confirms stored content and protected-partition
  preservation after the boot; it does not observe the driver's in-boot read
  path or add a second boot.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 `complete=0` records, the
  N9.10 comparator `complete=1`, the N10.1 hardware result and its captured
  repeatability series are preserved unchanged. Initialization reliability
  remains `[U]`.
- The host seam still calls `doAsyncReadWrite` directly; it does not qualify
  the real context, deblock or breakup layers.

## 5. Next steps and gates

This closeout does not authorize another boot, flash, retry, reflash,
rollback, reset/repair, commit or push. The next milestone is a separate
proposal from the storage-publication roadmap; it must not be implemented,
built or flashed in this batch. A captured repeatability series for this
unchanged fourteen-case image would be a separate proposal with a fresh
explicit owner approval and an explicit boot budget. Any later hardware
attempt requires the same.

## 6. Evidence

Raw artifacts and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n10.2-hardware-evidence/` directory. The
`observation-1/` subdirectory binds the original flash manifest by hash and
contains the owner transcript reference, the interpretation, the structured
observation record, the pulled-prefix gate reports and the observation
manifest `observation-sha256.txt` (`840b2721…`).
