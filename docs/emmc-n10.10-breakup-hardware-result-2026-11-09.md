# gta4l N10.10 deterministic breakup hardware result (2026-11-09)

Status: BOOT-only flash of the N10.9 deterministic breakup candidate and one
owner-reported first cold boot on 2026-11-09. The report selected the read-only
whole media and the pinned boot partition, performed the four fixed
boot-partition reads through the real
`IOMedia`/`IOBlockStorageDriver`/`IOBreaker`/`IODeblocker` path, and emitted
`cases=4 passed=4 failed=0 reason=0 complete=1 reads=4 bytes=199680
span=200192 blocks=391 subrequests=49 max=8 foreign=0 fails=0 cancel=0
budget=0 media=0 part=0 limit=8 seg=3 cap=61071360`. The last case emitted
`BREAKUPREAD: index=3 target=1 start=4196 length=65024 span=65536
actual=65024 status=0 subrequests=16 blocks=128 max=8
digest=bc0b9bb9c6dd11c3aea665d01dc5f108`, the first 16 bytes of the pinned
case-3 digest. The whole-boot transport counters show a largest subrequest of
eight blocks and 206 two-block CMD18 chunks, matching the mode-scoped
eight-block/4,096-byte/three-segment advertisement and the real
`IOBreaker`/`breakUpRequest()` split path. The flash's post-write readback
verified the stored BOOT before the boot; no post-boot readback was taken. The
owner returned the tablet to TWRP; the one-boot budget is consumed. No commit,
push, retry, reflash, rollback, mount, repair or further device access was
performed. This is a bounded same-boot qualification of four pinned
boot-partition ranges and the real breakup path on one boot `[C]`, not an
initialization-reliability or general I/O result.

## 1. Flash and boot scope

The owner approved the BOOT-only write in this session and controlled the one
cold boot. The flash record is in
`out/gta4l/ram-root-emmc-n10.9-hardware-evidence/` (`flash-record.json`
`26949764…`, flash evidence manifest `evidence-sha256.txt` `35cb4a3a…`).

- BOOT image `a8f4a54b…` (85,688,320 bytes), payload `706cfd53…` (83,886,080
  bytes), prelinked kernel `71828829…` (51,331,072 bytes; plan `2cee429c…`),
  packaged kernel `a8a5a609…`, requalified archive `8e636a01…`.
- Candidate source `Gta4lEmmcBlock.cpp` `684a84db…`, policy source
  `Gta4lEmmcBlock.c` `e7cfcf23…` and header `Gta4lEmmcBlock.h` `3ab6d170…`.
- The pre-flash p69 was the N10.6 post-flash image `f4882047…`; its prefix was
  `bacc933f…` and its tail after the 85,688,320-byte candidate offset was
  `a28b4ea2…`.
- Only `/dev/block/mmcblk0p69` was written (20,920 x 4096 records, synced).
  The post-flash full readback `4b51ff21…` matched the expected
  candidate-prefix plus unchanged-tail value; prefix `a8f4a54b…`, tail
  `a28b4ea2…`. RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…`
  were unchanged, and the live protected-partition hashes were verified
  against the recorded values before the write. A read-only target-binding
  re-verification after the write (`target-binding.txt`, UTC
  `2026-11-09T11:29:51Z`) re-read the same p69 full/prefix/tail values before
  the owner booted.
- Rollback `boot-before-n10.10.img.xz` (`70312a39…` -> raw `f4882047…`)
  preserves the previous N10.6 post-flash image. It was not restored.

The owner supplied the wrapped report and confirmed the TWRP return
(`owner-transcript-n10.10-2026-11-09.txt`, `51482570…`); it preserves the
screen's wrapped-line artifact, including the ` @Gta41` continuation, the
`FAIL`/frames/pages/free lines and the owner's `onecold boot, back on twp`
note. The report is associated with the flashed image through the flash
record, not an independently measured running-image hash. The transcript is a
screen transcription, not a captured kernel log.

## 2. What the report shows

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 by
tes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 DRV: online=1 wp=1 reads=58 blocks=420 fails=0 max=8
GTA4L-EMMC P5 XFER: req=420 c17=8 c18=206 c18b=412 cfail=0 stops=2
06 sfail=0 last_stop=0
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 content=GUID_
partition_scheme writable=0
GTA4L-EMMC P5 BREAKUP: cases=4 passed=4 failed=0 reason=0 complete
=1 reads=4 bytes=199680 span=200192 blocks=391 subrequests=49 max=
8 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 limit=8 seg=3
 cap=61071360
GTA4L-EMMC P5 BREAKUPREAD: index=3 target=1 start=4196 length=6502
4 span=65536 actual=65024 status=0 subrequests=16 blocks=128 max=8
 digest=bc0b9bb9c6dd11c3aea665d01dc5f108
GTA4L-EMMC P5 PART: count=0 expected=0 waited_ms=0 error=0 reporte
d=0
GTA4L-EMMC P5 READ: lba=1 attempted=0 ok=0 bytes=0 sum=0 base=2228
245566 same=0
GTA4L-EMMC P5 QUAL: ref=0 ref_lba=0 ref_blocks=0 two_att=0 two_ok=
0 two_reason=0 two_bytes=0 two_mismatch=0 two_off=0 two_match=0 tw
o_c18=0 two_c17=0 two_stops=0 two_sfail=0
GTA4L-EMMC P5 QUAL3: att=0 ok=0 reason=0 bytes=0 mismatch=0 off=0
match=0 c18=0 c17=0 stops=0 sfail=0
GTA4L-EMMC P5 SNAP: locked=1
GTA4L-EMMC P5 FAIL: valid=0 stage=0 code=0 block=0 error=0
 @Gta41EmmcBlock.cpp:3899
frames ffffffff007a03808 ffffffff007a07ea4 ffffffff007a03820 ffffffff0
0741e4a4
pages=0x00000000000079e6d
free=0x00000000000078b53
```

- `rc=0 stop=0 mode=3` is header success, not breakup success. The MEDIA
  fields repeat the N10.1/N10.2/N10.6 read-only whole-media profile: `whole=1
  base=0 size=31268536320 content=GUID_partition_scheme writable=0`. `base=0`
  is the implied zero base, not a published `Base` property.
- `cases=4` is the fixed case count in `package/Gta4lEmmcBlock.c:288-333`.
  `passed=4 failed=0 reason=0` means every pinned case completed and matched
  its retained digest. The four ranges and their expected digest prefixes
  are:

| index | target | start | length | span | blocks | stages | max | expected SHA-256 (first 8…last 6) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | boot | 4096 | 4096 | 4096 | 8 | 1 | 8 | `5878bcf8…f3b1b2` |
| 1 | boot | 4096 | 65536 | 65536 | 128 | 16 | 8 | `b4c3c643…80fa9c` |
| 2 | boot | 4096 | 65024 | 65024 | 127 | 16 | 8 | `5f970472…61a95c` |
| 3 | boot | 4196 | 65024 | 65536 | 128 | 16 | 8 | `bc0b9bb9…26db17` |

- Case 0 is the aligned control at the 4,096-byte break size and takes the
  direct `executeRequest()` short-circuit (one eight-block subrequest). Case 1
  splits into sixteen exact eight-block stages. Case 2 splits into fifteen
  full eight-block stages and a seven-block final stage (16 stages, 127
  blocks). Case 3 is unaligned: the deblocker requests the aligned
  4,096-69,632 span with a 100-byte head scratch and a 412-byte tail scratch,
  and the driver splits the 65,024 requested bytes into sixteen eight-block
  subrequests. The four ranges are inside the gate-pinned 1,798,052-byte
  kernel section and are invariant across candidates.
- The aggregate `reads=4 bytes=199680 span=200192 blocks=391 subrequests=49`
  is exactly the table: 4,096 + 65,536 + 65,024 + 65,024 = 199,680 requested
  bytes; 4,096 + 65,536 + 65,024 + 65,536 = 200,192 aligned bytes; 8 + 128 +
  127 + 128 = 391 blocks; 1 + 16 + 16 + 16 = 49 driver-level subrequests; the
  largest observed subrequest is eight blocks.
- `complete=1` is the source guard at `package/Gta4lEmmcBlock.cpp:3559-3572`:
  `cases != 0`, `passed == cases`, `failed == 0`, the latched reason is
  `NONE`, `cancel == 0`, `budget == 0`, `media == 0`, `part == 0`, `foreign
  == 0`, `limit == 8` and `segments == 3`. It cannot coexist with a read
  failure, short transfer, hash mismatch, decomposition failure, cancellation,
  budget refusal, media/partition/open failure, foreign traffic or a wrong
  advertisement.
- `media=0 part=0` are clear failure flags, not success flags. In
  `package/Gta4lEmmcBlock.cpp:3263-3378`, the media and partition failure
  flags are set only when the corresponding latched reason is
  `GTA4L_EMMC_BLOCK_BREAKUP_REASON_MEDIA` or
  `GTA4L_EMMC_BLOCK_BREAKUP_REASON_PART`. Both zero therefore means neither
  failure occurred.
- `limit=8` is the mode-scoped `kIOMaximumBlockCountReadKey` advertisement
  and `seg=3` the `kIOMaximumSegmentCountReadKey` advertisement
  (`package/Gta4lEmmcBlock.cpp:961-980`); the byte limit is 4,096. The
  legacy, partobs, partcmp, partread and deblock modes keep their own
  values. `cap=61071360` is the whole-media capacity in 512-byte blocks
  (31,268,536,320 bytes), matching the `MEDIA size`.
- `BREAKUPREAD: index=3 target=1 start=4196 length=65024 span=65536
  actual=65024 status=0 subrequests=16 blocks=128 max=8
  digest=bc0b9bb9c6dd11c3aea665d01dc5f108` is the final unaligned case: boot
  offset 4,196, 65,024 requested bytes, 65,536 aligned bytes, `kIOReturnSuccess`,
  65,024 actual bytes and sixteen eight-block subrequests. The printed 16
  digest bytes are exactly the first 16 bytes of the pinned 32-byte case-3
  digest at `package/Gta4lEmmcBlock.c:322-331`
  (`bc0b9bb9c6dd11c3aea665d01dc5f108…26db17`), and they match the N10.9
  candidate table. `BREAKUPREAD` records the most recent case, not an
  aggregate; the aggregate is `reads=4`.
- `DRV reads=58 blocks=420 fails=0 max=8` and `XFER req=420 c17=8 c18=206
  c18b=412 cfail=0 stops=206 sfail=0 last_stop=0` are whole-boot aggregate
  counters that include the four breakup reads. They are not additive with
  `BREAKUP reads=4 blocks=391`: the breakup counters count only the pinned
  case-table requested bytes, aligned spans and `span / 512`. `XFER req` is a
  requested-block count, not a request count; it equals `DRV blocks=420`. The
  transport issued 8 single-block CMD17 reads and 206 two-block CMD18 chunks
  (412 blocks) with one successful CMD12 stop per chunk and no stop failure.
  The 29-block difference from the 391-block case-table span is the
  deblocker's head/tail scratch reads and block rounding.
- `max=8` is the largest single storage-driver subrequest in this breakup
  boot: eight blocks, 4,096 bytes, exactly the mode-scoped block and byte
  limits. Because case 1's aligned span is 128 blocks and case 3's is 128
  blocks, a direct single request would have passed 128 blocks; the observed
  maximum therefore shows the real `IOBreaker::getBreakSize()` /
  `breakUpRequest()` split path was entered for those cases. The N10.6
  deblock-mode boot advertised three segments with a 64-block/32,768-byte
  limit and observed a 24-block three-segment fold; that is a distinct
  mode-scoped fact, not an expectation for breakup mode.
- The zero `PART`/`READ`/`QUAL`/`QUAL3` lines are inactive legacy mode-1/mode-3
  and N8 `PARTOBS` paths in this breakup boot, not failures or new passes.
  `SNAP locked=1` and `FAIL valid=0` are the unchanged snapshot and failure
  latch.
- The supplied panic suffix is `@Gta41EmmcBlock.cpp:3899` (spelling
  preserved). In the candidate source `684a84db…`, line 3899 is the
  intentional `panic("%s", reportBuffer)` in `panicWithReport()`; it is the
  normal terminal report stop, not a fault. No watchdog line is present; the
  eMMC report owned the terminal.

## 3. Stored-image verification and post-boot boundary

The flash record's post-write readback, and the read-only target-binding
re-verification after the write, confirmed that p69 was exactly the flashed
candidate prefix `a8f4a54b…` plus the preserved pre-flash tail `a28b4ea2…`,
that the full partition was `4b51ff21…`, and that RECOVERY `14e11370…`, EFS
`51fa824c…` and sec_efs `3a265d58…` were unchanged. That verification happened
before the owner's cold boot.

No post-boot readback is recorded in the evidence directory. The owner
returned the tablet to TWRP after the one cold boot, and this batch authorizes
no further device access, so no independent post-boot content or
protected-partition comparison was performed. Unlike N10.2, the stored image
after the boot is therefore not independently re-verified in this record; the
report is bound to the candidate by the pre-boot post-flash readback, the
target-binding re-verification and the pinned digest match, not by a
post-boot pull.

## 4. What this does not establish

- One owner-reported cold boot, transcribed from the screen. There is no
  captured kernel log, boot timestamp, rail-reset state or independently
  measured running-image hash; the report is bound through the flash record.
- The result qualifies only the four pinned boot-partition ranges and the real
  breakup path they exercised on this one boot. It does not qualify arbitrary
  byte offsets or lengths, arbitrary I/O, filesystem mount, root integration,
  writes, wider or faster modes, DMA/CQE/ICE or initialization reliability.
- All four ranges address offsets 4,096-69,220 inside the gate-pinned
  1,798,052-byte kernel section, which the 85,688,320-byte BOOT image covers
  and which is invariant across candidates. Unlike N10.6, this table does not
  address the boot-partition tail.
- The mode-scoped eight-block/4,096-byte/three-segment advertisement is
  unchanged from N10.8/N10.9. This boot observed the resulting 49-subrequest
  decomposition, but it does not prove that every later allocation produces
  the same physical segmentation; the deterministic four-case table is the
  qualification, not a general break model.
- The host breakup model is a reconstruction. This boot is the hardware
  qualification of the real family code for the pinned table; it does not
  upgrade the host model's status.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 `complete=0` records, the
  N9.10 comparator `complete=1`, the N9.11 owner-reported series, the
  N10.1/N10.2 partread results and the N10.6 eleven-case deblock result are
  preserved unchanged. Initialization reliability remains `[U]`. The N10.6
  deblock-mode 24-block three-segment fold remains its own observation and is
  not a breakup-mode result.

## 5. Next steps and gates

This closeout does not authorize another boot, flash, retry, reflash,
rollback, reset/repair, mount, commit or push. The next milestone is a separate
proposal from the storage-publication roadmap; it must not be implemented,
built or flashed in this batch. A captured repeatability series for this
unchanged four-case image would be a separate proposal with a fresh explicit
owner approval and an explicit boot budget. Any later hardware attempt
requires the same.

## 6. Evidence

Raw artifacts and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n10.9-hardware-evidence/` directory. The manifest
`evidence-sha256.txt` (`35cb4a3a…`) binds `backup.log` (`63138548…`),
`boot-before-n10.10.img` (`f4882047…`), `boot-before-n10.10.img.xz`
(`70312a39…`), `boot-ram-root-emmc-n10.9-breakup.img.xz` (`02fa8415…`),
`cleanup.log` (`43024ecf…`), `flash-commands.log` (`ba5bebe5…`),
`flash-record.json` (`26949764…`), `next-step.txt` (`12fa74b7…`),
`owner-transcript-n10.10-2026-11-09.txt` (`51482570…`), `preflight.log`
(`06168b25…`), `stage-local.log` (`c4b3106c…`), `stage.log` (`ae0d1b34…`),
`target-binding.txt` (`1c5eb555…`) and `write-readback.log` (`98b85800…`).
The raw evidence stays outside the tracked tree and records no private
identifier in this document.
