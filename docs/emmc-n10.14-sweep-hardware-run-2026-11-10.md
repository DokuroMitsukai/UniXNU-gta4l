# gta4l N10.14 bounded read-sweep hardware run (2026-11-10)

Status: BOOT-only flash of the N10.13 bounded aligned read-sweep candidate and
one owner-reported first cold boot on 2026-11-10. The report selected the
read-only whole media and the pinned boot partition and completed all six
sweep cases through the real `IOMedia`/`IOBlockStorageDriver`/`IOBreaker`
path, each with a direct controller-gate CMD17 reference and a byte-for-byte
comparison: `SWEEP: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6
bytes=53248 span=53248 blocks=104 subrequests=13 max=8 foreign=0 fails=0
cancel=0 budget=0 media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0
limit=8 seg=1 cap=61071360`. The last case printed `SWEEPREAD: index=5
target=1 start=4673536 length=4096 span=4096 actual=4096 status=0
subrequests=1 blocks=8 max=8 ref=1 refbytes=4096 refstatus=0 mvalid=0
mismatch=0 digest=ccaa1c08dd34df3ca8d4eb3a2b530fdb
refdigest=ccaa1c08dd34df3ca8d4eb3a2b530fdb`: a same-boot media-path SHA-256
prefix equal to its direct reference. The whole-boot aggregates show a
largest subrequest of eight blocks, 13 driver-level case subrequests and 63
two-block CMD18 chunks with one successful CMD12 stop each. The flash's
post-write readback verified the stored BOOT before the boot; no post-boot
readback was taken. The owner returned the tablet to TWRP; the one-boot
budget is consumed. No push, retry, reflash, rollback, mount, repair or
further device access was performed. This is a bounded same-boot
qualification of the six case ranges (two invariant anchors plus four
runtime windows) and the real driver/reference agreement on one boot `[C]`,
not an initialization-reliability or general I/O result.

## 1. Flash and boot scope

The owner directed the BOOT-only flash in this session and controlled the one
cold boot. The flash record is in
`out/gta4l/ram-root-emmc-n10.14-hardware-evidence/` (`flash-record.json`
`d10fad8b…`, evidence manifest `evidence-sha256.txt` `d2833db6…`).

- BOOT image `b626a1fa…` (85,688,320 bytes), payload `62371bfb…` (83,886,080
  bytes; 36/36 contract; `emmc-block-3-sweep` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-sweep=1`), prelinked kernel `64bab406…`
  (51,363,840 bytes; plan `cd402f0e…`), packaged kernel `f1ab2b1d…`, archive
  `14f40fa8…`(requalified with byte-identical members).
- Candidate sources `Gta4lEmmcBlock.c` `907ac8ad…` and `Gta4lEmmcBlock.cpp`
  `80c817a0…` (header `d718148c…`), from the N10.13 candidate production.
- The pre-flash p69 was the N10.10 post-flash image `4b51ff21…`; its prefix
  was the N10.9 breakup image `a8f4a54b…` and its tail after the
  85,688,320-byte candidate offset was `a28b4ea2…`.
- Only `/dev/block/mmcblk0p69` was written (20,920 x 4096 records, synced).
  The post-flash full readback `a2b5a62d…` matched the expected
  candidate-prefix plus unchanged-tail value; prefix `b626a1fa…`, tail
  `a28b4ea2…`. RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…`
  were unchanged, and the live protected-partition hashes were verified
  against the recorded values before the write and after it. A read-only
  target-binding re-verification after the write (`target-binding.txt`, UTC
  `2026-11-10T00:28:44Z`) re-read the same p69 full/prefix/tail values before
  the owner booted.
- Rollback `boot-before-n10.14.img.xz` (`d4fce9c9…` -> raw `4b51ff21…`)
  preserves the previous N10.9-breakup post-flash image. It was not restored.

The owner supplied the wrapped report and confirmed the TWRP return
(`owner-transcript-n10.14-2026-11-10.txt`, `e7199549…`); it preserves the
screen's wrapped-line artifact, including the ` @Gta41` continuation, the
`FAIL`/frames/pages/free lines and the owner's `first cold boot, bak on twrp
now` note. The report is associated with the flashed image through the flash
record, not an independently measured running-image hash. The transcript is a
screen transcription, not a captured kernel log, and it carries no boot
timestamp.

## 2. What the report shows

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 by
tes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 DRV: online=1 wp=1 reads=22 blocks=133 fails=0 max=8
GTA4L-EMMC P5 XFER: req=133 c17=7 c18=63 c18b=126 cfail=0 stops=63
 sfail=0 last_stop=0
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 content=GUID_
partition_scheme writable=0
GTA4L-EMMC P5 SWEEP: cases=6 passed=6 failed=0 reason=0 complete=1
 reads=6 bytes=53248 span=53248 blocks=104 subrequests=13 max=8 fo
reign=0 fails=0 cancel=0 budget=0 media=0 part=0 refs=6 refbytes=5
3248 anchor=1 mismatches=0 limit=8 seg=1 cap=61071360
GTA4L-EMMC P5 SWEEPREAD: index=5 target=1 start=4673536 length=409
6 span=4096 actual=4096 status=0 subrequests=1 blocks=8 max=8 ref=
1 refbytes=4096 refstatus=0 mvalid=0 mismatch=0 digest=ccaa1c08dd3
4df3ca8d4eb3a2b530fdb refdigest=ccaa1c08dd34df3ca8d4eb3a2b530fdb
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
 @Gta41EmmcBlock.cpp:4882
frames ffffffff007a0948c ffffffff007a0f218 ffffffff007a094a4 ffffffff0
0741e4a4
pages=0x00000000000079e66
free=0x00000000000078e8a
```

- `rc=0 stop=0 mode=3` is header success, not sweep success. The MEDIA fields
  repeat the N10.1/N10.2/N10.6/N10.10 read-only whole-media profile: `whole=1
  base=0 size=31268536320 content=GUID_partition_scheme writable=0`.
- `cases=6` is the fixed case count of the N10.13 candidate
  (`package/Gta4lEmmcBlock.c`, six-case table). `passed=6 failed=0 reason=0`
  means every case completed, agreed with its direct reference and, for the
  two anchors, matched the pinned SHA-256 prefix. The observed aggregate is
  exactly the table:

| index | kind | start | length | span | blocks | subrequests | max | content |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | anchor A0 | 4096 | 4096 | 4096 | 8 | 1 | 8 | pinned `5878bcf8…f3b1b2` |
| 1 | anchor A1 | 4096 | 16384 | 16384 | 32 | 4 | 8 | pinned `fa43aa5d…0782da` |
| 2 | R0 | 26382336 | 16384 | 16384 | 32 | 4 | 8 | runtime reference |
| 3 | R1 | 52699136 | 8192 | 8192 | 16 | 2 | 8 | runtime reference |
| 4 | R2 | 78999552 | 4096 | 4096 | 8 | 1 | 8 | runtime reference |
| 5 | R3 | 4673536 | 4096 | 4096 | 8 | 1 | 8 | runtime reference |

- `reads=6 bytes=53248 span=53248 blocks=104 subrequests=13` is exactly the
  table: 4,096 + 16,384 + 16,384 + 8,192 + 4,096 + 4,096 = 53,248 bytes;
  8 + 32 + 32 + 16 + 8 + 8 = 104 blocks; 1 + 4 + 4 + 2 + 1 + 1 = 13
  driver-level subrequests with a largest subrequest of eight blocks. All six
  cases are section-aligned so `span=bytes`; unlike N10.10 there is no
  unaligned case and no deblocker span padding inside the table.
- `refs=6 refbytes=53248 anchor=1 mismatches=0` is the reference column: every
  case performed its direct per-controller-gate CMD17 reference read of the
  same bytes (`refs=6`, 53,248 bytes) and every media/reference pair compared
  byte-for-byte without a located mismatch. `anchor=1` is the guard
  `sweep_anchor == (anchorMatched == GTA4L_EMMC_BLOCK_SWEEP_ANCHOR_CASES)`
  form: both pinned anchors re-hashed to their invariant gate-pinned values
  `5878bcf8…f3b1b2` (4,096/4,096) and `fa43aa5d…0782da` (4,096/16,384), the
  same v5-image kernel-section digests recorded by N10.13.
- `complete=1` is the source guard at `package/Gta4lEmmcBlock.cpp:4529-4554`:
  `cases == 6`, `passed == cases`, `failed == 0`, the latched reason is
  `NONE`, `reads == cases`, `refs == cases`, `refbytes == bytes`,
  `span == bytes`, `blocks == bytes / 512`, `subrequests == 13`, `max == 8`,
  `foreign == 0`, `fails == 0`, `cancel == 0`, `budget == 0`, `media == 0`,
  `part == 0`, `anchor == 1`, `mismatches == 0`, `limit == 8` and
  `segments == 1`. It cannot coexist with a read failure, short transfer,
  reference failure, hash mismatch, split/order failure, cancellation, budget
  refusal, media/partition/open failure, foreign traffic or a wrong
  advertisement.
- `media=0 part=0 foreign=0` are clear failure flags: a whole-media selection
  or profile failure, a boot-partition lookup failure or a driver-level
  request that cannot be attributed to the active case would set the
  corresponding flag and stop or latch `complete=0`. `refbytes=53248
  refstatus=0` on every case means the reference transport returned
  `kIOReturnSuccess` each time.
- `limit=8` is the sweep-mode `kIOMaximumBlockCountReadKey` advertisement and
  `seg=1` the sweep-mode `kIOMaximumSegmentCountReadKey` advertisement;
  `cap=61071360` is the whole-media capacity in 512-byte blocks
  (31,268,536,320 bytes), matching the `MEDIA size`.
- `DRV reads=22 blocks=133 fails=0 max=8` and `XFER req=133 c17=7 c18=63
  c18b=126 cfail=0 stops=63 sfail=0 last_stop=0` are whole-boot driver-path
  aggregates. They are not additive with the sweep counters: `XFER req`
  equals `DRV blocks=133`, and the transport issued 7 single-block CMD17
  reads plus 63 two-block CMD18 chunks (126 blocks) with one successful CMD12
  stop per chunk, no chunk failure, no stop failure and no failed final stop
  (`last_stop=0`). The sweep case column accounts for 52 of the chunks (13
  eight-block subrequests, four two-block chunks each, 104 blocks); the
  remaining 11 chunks and 7 CMD17 reads form a 29-block incidental overlay
  outside the case observation windows, the same 29-block overlay size the
  N9.6/N9.7 partcmp boots and the N10.10 breakup boot observed as whole-media
  scan/rounding activity. The direct controller-gate CMD17 reference reads
  (6 cases, 53,248 bytes) bypass the driver path and are deliberately not
  visible in the `DRV`/`XFER` aggregates. `max=8` confirms no subrequest
  exceeded the sweep-mode cap.
- `mvalid=0 mismatch=0` in `SWEEPREAD` is the equal-content form of the
  mismatch diagnostic: `caseMvalid` flags a located first-difference offset,
  and it stays zero when the media-path and reference buffers compared equal.
- The aggregate arithmetic and the last-case line are field-for-field
  identical to the N10.13 host expectation for this table, including the
  untouched incidental overlay shape.
- The zero `PART`/`READ`/`QUAL`/`QUAL3` lines are inactive legacy mode-1/mode-3
  and N8 `PARTOBS` paths in this sweep boot, not failures or new passes.
  `SNAP locked=1` and `FAIL valid=0` are the unchanged snapshot and failure
  latch.
- The supplied panic suffix is `@Gta41EmmcBlock.cpp:4882` (spelling
  preserved). In the N10.13 candidate source `80c817a0…`, line 4882 is the
  intentional `panic("%s", reportBuffer)` in `panicWithReport()`; it is the
  normal terminal report stop, not a fault. No watchdog line is present; the
  eMMC report owned the terminal, and the userspace fs-timer-hold fixture
  kept the screen visible long enough for the owner to transcribe it.

## 3. Stored-image verification and post-boot boundary

The flash record's post-write readback, and the read-only target-binding
re-verification after the write, confirmed that p69 was exactly the flashed
candidate prefix `b626a1fa…` plus the preserved pre-flash tail `a28b4ea2…`,
that the full partition was `a2b5a62d…`, and that RECOVERY `14e11370…`, EFS
`51fa824c…` and sec_efs `3a265d58…` were unchanged. That verification happened
before the owner's cold boot.

No post-boot readback is recorded in the evidence directory. The owner
returned the tablet to TWRP after the one cold boot, and this batch authorizes
no further device access, so no independent post-boot content or
protected-partition comparison was performed. As in N10.10, the stored image
after the boot is not independently re-verified in this record; the report is
bound to the candidate by the pre-boot post-flash readback, the target-binding
re-verification, the two pinned anchor digests and the same-boot
media/reference digest equality, not by a post-boot pull.

## 4. What this does not establish

- One owner-reported cold boot, transcribed from the screen. There is no
  captured kernel log, boot timestamp, rail-reset state or independently
  measured running-image hash; the report is bound through the flash record.
- The result qualifies only the two invariant anchors and the four
  P-derived runtime windows (starts `26,382,336`, `52,699,136`, `78,999,552`
  and `4,673,536` bytes) and the real driver/reference agreement they
  exercised on this one boot. It does not qualify arbitrary byte offsets or
  lengths, arbitrary I/O, filesystem mount, root integration, writes, wider
  or faster modes, DMA/CQE/ICE or initialization reliability.
- The anchor ranges are inside the gate-pinned 1,798,052-byte v5 kernel
  section and are invariant across candidates; the four runtime windows are
  derived from the discovered 196,608-block boot partition and were compared
  only within this boot against their direct CMD17 references. The
  `digest`/`refdigest` prefixes have no pinned content and bind nothing
  across boots.
- The host sweep model is a reconstruction of the media/breaker path and the
  direct reference transport. This boot is the hardware qualification of the
  real family code and transport for the six-case table; it does not upgrade
  the host model's status.
- The `DRV`/`XFER` aggregates and the 29-block incidental overlay are
  descriptive whole-boot counters, not a sweep-mode transport characterization
  beyond the observed eight-block cap; the sweep budget/worst-case margin is
  not a timing measurement.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 `complete=0` records, the
  N9.10 comparator `complete=1`, the N9.11 owner-reported series, the
  N10.1/N10.2 partread results, the N10.6 deblock result and the N10.10
  breakup result are preserved unchanged. Initialization reliability remains
  `[U]`. N10.10 remains the only breakup-layer hardware result; N10.14 is the
  first sweep-mode hardware observation.

## 5. Next steps and gates

This closeout does not authorize another boot, flash, retry, reflash,
rollback, reset/repair or mount. The flash and boot consumed the N10.14
budget: one owner-controlled cold boot, then return to TWRP and stop
regardless of result, with no automatic retry, reflash or rollback. The
tablet is in TWRP; the next milestone is a separate proposal from the
storage-publication roadmap and must not be implemented, built or flashed in
this batch. A captured repeatability series for this unchanged sweep image
would be a separate proposal with fresh explicit owner approval and an
explicit boot budget. Any later hardware attempt requires the same.

## 6. Evidence

Raw artifacts and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n10.14-hardware-evidence/` directory. The manifest
`evidence-sha256.txt` (`d2833db6…`) binds `backup-local.log` (`29b1a92a…`),
`backup.log` (`7c97894f…`), `boot-before-n10.14.img` (`4b51ff21…`),
`boot-before-n10.14.img.xz` (`d4fce9c9…`),
`boot-ram-root-emmc-n10.13-sweep.img.xz` (`464b26a8…`), `cleanup.log`
(`e89d4429…`), `expected-postflash-p69.txt` (`add74259…`),
`flash-commands.log` (`2f120ab3…`), `flash-record.json` (`d10fad8b…`),
`next-step.txt` (`78b506f2…`), `owner-transcript-n10.14-2026-11-10.txt`
(`e7199549…`), `preflight.log` (`b9eb7fdb…`), `preflight-hashes.log`
(`6a7c7707…`), `stage-local.log` (`99205813…`), `stage.log` (`229678c2…`),
`target-binding.txt` (`cd7acfde…`) and `write-readback.log` (`c2a463b3…`).
The raw evidence stays outside the tracked tree and records no private
identifier in this document.
