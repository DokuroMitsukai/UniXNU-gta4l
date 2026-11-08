# gta4l N10.1 read-only IOMedia content qualification hardware result (2026-11-08)

Status: BOOT-only flash of the N10.1 read-only content-qualification candidate
and one owner-reported first cold boot on 2026-11-08. The report selected the
read-only whole media and the pinned boot partition, performed the six fixed
range reads, and emitted `cases=6 passed=6 failed=0 reason=0 complete=1
reads=6 blocks=138 bytes=70656 fails=0 cancel=0 budget=0 media=0 part=0
cap=61071360`. Every pinned digest matched. An independent read-only
post-boot comparison then confirmed that the stored BOOT is exactly the
flashed candidate plus the preserved pre-flash tail, that RECOVERY/EFS/sec_efs
are unchanged, and that the pulled prefix and extracted payload pass the host
gates. The owner returned the tablet to TWRP; the one-boot budget is consumed.
No commit, push, retry, reflash, rollback, mount or repair was performed. This
is a bounded same-boot content qualification of six pinned ranges, not an
initialization-reliability or general I/O result.

## 1. Flash and boot scope

The owner approved the BOOT-only write in this session and controlled the one
cold boot. The flash record is in
`out/gta4l/ram-root-emmc-n10.1-hardware-evidence/` (`flash-record.json`
`aed3dfe9…`, flash evidence manifest `evidence-sha256.txt` `d2b23ecf…`).

- BOOT image `0cee22c9…` (85,688,320 bytes), payload `e2dc899d…` (83,886,080
  bytes), prelinked kernel `12e00d9f…`, packaged kernel `05fde642…`,
  requalified archive `5a59d055…`, candidate build report `0c6b9607…`.
- Candidate source `Gta4lEmmcBlock.cpp` `6a11aa76…`, policy source
  `Gta4lEmmcBlock.c` `9dad3d79…` and header `Gta4lEmmcBlock.h` `d05b982e…`.
  The build report binds those hashes to the objects and archive.
- Only `/dev/block/mmcblk0p69` was written (20,920 x 4096 records, synced).
  The post-flash full readback `cc3c8492…` matched the expected
  candidate-prefix plus unchanged-tail value; prefix `0cee22c9…`, tail
  `a28b4ea2…`. RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs
  `3a265d58…` were unchanged, and the protected-partition backups were
  verified before the write.
- Rollback `boot-before-n10.1.img.xz` (`de7d4d51…` -> raw `004b9d6d…`)
  preserves the previous N9.10 post-flash image. It was not restored.

The owner supplied the wrapped report and confirmed the TWRP return. The raw
screen transcription is in the parent directory (`2717eb74…`); the bounded
interpretation, structured observation record and pulled gate reports are in
`out/gta4l/ram-root-emmc-n10.1-hardware-evidence/observation-1/`
(interpretation `a6a431c6…`, record `a158f74b…`; the separate non-self-hashing
manifest `898bbda0…` binds the extended `evidence-sha256.txt` `d2b23ecf…`).
The report is associated with the flashed image through the flash record, not
an independently measured running-image hash. The transcript is a screen
transcription, not a captured kernel log.

## 2. What the report shows

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512
bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 DRV: online=1 wp=1 reads=31 blocks=167 fails=0 max=8
GTA4L-EMMC P5 XFER: req=167 c17=9 c18=79 c18b=158 cfail=0 stops=79
sfail=0 last_stop=0
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320
content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 PARTREAD: cases=6 passed=6 failed=0 reason=0 complete=1
reads=6 blocks=138 bytes=70656 fails=0 cancel=0 budget=0 media=0 part=0
cap=61071360
GTA4L-EMMC P5 READTEST: index=5 target=1 blocks=1 off=100662784 ok=1
status=0 actual=512 digest=400039fd62daf3499b377ced75428918
```

- `rc=0 stop=0 mode=3` is header success, not partread success. The MEDIA
  fields repeat the N9.10 read-only whole-media profile: `whole=1 base=0
  size=31268536320 content=GUID_partition_scheme writable=0`. `base=0` is the
  implied zero base, not a published `Base` property.
- `cases=6` is the fixed case count in
  `package/Gta4lEmmcBlock.c:135-200`. `passed=6 failed=0 reason=0` means every
  pinned case completed and matched its retained digest. The six ranges and
  their expected first 16 digest bytes are:

| index | target | range | blocks | expected SHA-256 (first 16 bytes) |
| --- | --- | --- | --- | --- |
| 0 | whole | LBA 0 (PMBR, primary header and primary array) | 34 | `0c7e9c03ce25232f6ac1e588268dcda1` |
| 1 | whole | last 34 blocks (backup header and backup array) | 34 | `41912e59e66ee2c9a435fe17250d20c8` |
| 2 | boot | `BOOT_SIZE - 32768` (maximum 64-block request) | 64 | `ef313f712a8f436e989ce73766b49cc6` |
| 3 | boot | `BOOT_SIZE - 1536` (odd three-block tail) | 3 | `02acbb3a7b14ae9929eb10cd1ad7ba59` |
| 4 | boot | `BOOT_SIZE - 1024` (even two-block tail) | 2 | `d737414c433ea3c74750f380ca18bd06` |
| 5 | boot | `BOOT_SIZE - 512` (singleton) | 1 | `400039fd62daf3499b377ced75428918` |

- `reads=6 blocks=138 bytes=70656` is exactly the six cases: 34 + 34 + 64 + 3
  + 2 + 1 = 138 blocks and 138 x 512 = 70,656 bytes. Each case issues one
  media read through the page-aligned 32 KiB `IOMallocAligned` bounce buffer;
  the driver's eight-block request bound splits the 34- and 64-block cases
  into multiple transport requests.
- `complete=1` is the source guard at
  `package/Gta4lEmmcBlock.cpp:2297-2299`: `budget == 0`, the latched reason is
  `NONE`, `failed == 0`, `passed == cases` and `cases != 0`. It cannot coexist
  with a read failure, short transfer, hash mismatch, cancellation, budget
  refusal or media/partition/open failure.
- `media=0 part=0` are clear failure flags, not success flags. In
  `package/Gta4lEmmcBlock.cpp:1969-1974`, `partread_media` is set to `1` only
  when the latched reason is `GTA4L_EMMC_BLOCK_PARTREAD_REASON_MEDIA` (1), and
  `partread_part` is set to `1` only when it is
  `GTA4L_EMMC_BLOCK_PARTREAD_REASON_PART` (2). Both zero therefore means
  neither failure occurred. The candidate production record's illustrative
  success line previously showed `1` for both; that line is corrected to
  `media=0 part=0`.
- `READTEST: index=5 target=1 blocks=1 off=100662784 ok=1 status=0 actual=512
  digest=400039fd62daf3499b377ced75428918` is the final singleton case: boot
  offset `100,663,296 - 512 = 100,662,784`, one block, `kIOReturnSuccess`,
  512 actual bytes, and the first 16 digest bytes of pinned case 5. READTEST
  records the most recent case, not an aggregate; the aggregate is
  `reads=6 blocks=138`.
- `DRV reads=31 blocks=167 fails=0 max=8` and `XFER req=167 c17=9 c18=79
  c18b=158 cfail=0 stops=79 sfail=0 last_stop=0` are whole-boot aggregate
  counters that include the six partread reads. They are not additive with
  `PARTREAD reads=6 blocks=138`: the partread counters count only the pinned
  cases. `XFER req` is a requested-block count, not a request count; it equals
  `DRV blocks=167`. `max=8` is the driver's eight-block request bound.
- The zero `READFAIL` and `PART`/`READ`/`QUAL`/`QUAL3` lines are inactive
  legacy mode-1/mode-3 and N8 `PARTOBS` paths in this partread boot, not
  failures or new passes. `SNAP locked=1` and `FAIL valid=0` are the unchanged
  snapshot and failure latch.
- The supplied panic suffix is `@Gta41EmmcBlock.cpp:2642` (spelling
  preserved). In the candidate source `6a11aa76…`, line 2642 is the
  intentional `panic("%s", reportBuffer)` in `panicWithReport()`; it is the
  normal terminal report stop, not a fault.

## 3. Independent post-boot integrity (read-only)

The boot budget was already consumed when the owner returned the tablet to
TWRP. A read-only host collection through the established private flash-host
connection then compared the stored state with the flash record. It did not
write, mount or reboot. Private identifiers are not recorded.

- BOOT p69 full `cc3c8492…` matched the post-flash record. The first
  85,688,320 bytes matched the candidate `0cee22c9…`, and the remaining
  14,974,976 bytes matched the preserved pre-flash tail `a28b4ea2…`.
- RECOVERY p70 `14e11370…`, EFS p64 `51fa824c…` and sec_efs p63
  `3a265d58…` were unchanged.
- The pulled 85,688,320-byte prefix re-hashed to `0cee22c9…`; its xz copy
  re-hashed to `bd519731…`; the payload extracted at the boot-header ramdisk
  offset (1,802,240) re-hashed to `e2dc899d…`.
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
`out/gta4l/ram-root-emmc-n10.1-hardware-evidence/postboot-integrity-2026-11-08.txt`
(`620d20aa…`); the gate reports are
`observation-1/pulled-boot-gate.json` (`3c8db987…`) and
`observation-1/pulled-payload-check.json` (`770c4016…`).

## 4. What this does not establish

- One owner-reported cold boot, transcribed from the screen. There is no
  captured kernel log, boot timestamp, rail-reset state or independently
  measured running-image hash; the report is bound through the flash record.
- The result qualifies only the six pinned ranges on this one boot. It does
  not qualify arbitrary I/O, filesystem mount, root integration, writes,
  wider or faster modes, DMA/CQE/ICE or initialization reliability.
- The independent readback confirms stored content and protected-partition
  preservation after the boot; it does not observe the driver's in-boot read
  path or add a second boot.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 `complete=0` records, the
  N9.10 comparator `complete=1` and the owner-reported N9.11 repeatability
  series are preserved unchanged. Initialization reliability remains `[U]`.
- The host seam still calls `doAsyncReadWrite` directly; it does not qualify
  the real context, deblock or breakup layers.

## 5. Next steps and gates

This closeout does not authorize another boot, flash, retry, reflash,
rollback, reset/repair, commit or push. The next milestone is a separate
proposal from the storage-publication roadmap; it must not be implemented,
built or flashed in this batch. Any later hardware attempt requires fresh
explicit owner approval and an explicit boot budget.

## 6. Evidence

Raw artifacts and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n10.1-hardware-evidence/` directory. The
`observation-1/` subdirectory binds the original flash manifest by hash and
does not hash itself. The repository records only the sanitized summary and
the bounded interpretation.
