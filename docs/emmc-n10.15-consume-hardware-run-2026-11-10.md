# gta4l N10.15 read-only consumer hardware run (2026-11-10)

Status: BOOT-only flash of the N10.15 read-only BOOT-window consumer candidate
and one owner-reported first cold boot on 2026-11-10. The report selected the
read-only whole media, opened the pinned boot partition and read the single
1,024-byte window at offset 0 through the real
`IOMedia`/`IOBlockStorageDriver`/`IOBreaker` path with the production
64-block/32,768-byte/one-segment advertisement:
`CONSUME: cases=1 passed=1 failed=0 reason=0 complete=1 reads=1 bytes=1024
span=1024 blocks=2 subrequests=1 max=2 foreign=0 fails=0 cancel=0 budget=0
media=0 part=0 open=1 close=1 alloc=0 client=1 mismatches=0 limit=64 seg=1
cap=61071360`. The single case printed `CONSUMEREAD: index=0 target=1 start=0
length=1024 span=1024 actual=1024 status=0 subrequests=1 blocks=2 max=2
digest=97479ae876ec6fe6f45b38492b9eb9ac
expected=00000000000000000000000000000000 mismatch=0`: the observed SHA-256
prefix equals the option A external reference for the packaged image, and the
all-zero `expected` is the option A form with the comparison performed
host-side. No `CONSUMEFAIL` line appeared and consume mode suppressed
`READ`/`QUAL`/`QUAL3`. The flash's post-write readback verified the stored
BOOT before the boot; no post-boot readback was taken. The owner returned the
tablet to TWRP; the one-boot budget is consumed. No retry, reflash, rollback,
mount, repair or further device access was performed. This is a bounded
same-boot qualification of one 1,024-byte BOOT window, the consumer
lifecycle, the production advertisement and the driver/attribution path on one
boot `[C]`, not an initialization-reliability or general I/O result.

## 1. Flash and boot scope

The owner directed the BOOT-only flash in this session and controlled the one
cold boot. The flash record is in
`out/gta4l/ram-root-emmc-n10.15-hardware-evidence/` (`flash-record.json`
`ca26ebee…`, evidence manifest `evidence-sha256.txt` `f09b111d…`).

- BOOT image `777b446c…` (85,688,320 bytes), payload `e05eb1cb…` (83,886,080
  bytes; 45/45 contract; `emmc-block-3-consume` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-consume=1`; hold root `327ac826…`, fixture
  `00363681…`), prelinked kernel `d943c228…` (51,437,568 bytes; plan
  `29b7a386…`), packaged kernel `e5ba92e3…` and archive `12d851a8…`.
- Candidate sources `Gta4lEmmcBlock.c` `9587c98a…`, `Gta4lEmmcBlock.cpp`
  `86dab8b1…` and `Gta4lEmmcBlock.h` `59c8ff45…`, from the N10.15 candidate
  production. The BOOT header ID is `4b15eb2a…`; the gate passed 18/18 with
  only the ramdisk differing from the `ce4d76e8…` v5 baseline.
- The pre-flash p69 was the N10.14 post-flash image `a2b5a62d…`; its prefix
  was the N10.13 sweep image `b626a1fa…` and its tail after the
  85,688,320-byte candidate offset was `a28b4ea2…`.
- Only `/dev/block/mmcblk0p69` was written (20,920 x 4096 records, synced).
  The post-flash full readback `6fa2b7e9…` matched the expected
  candidate-prefix plus unchanged-tail value; prefix `777b446c…`, tail
  `a28b4ea2…`. RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…`
  were unchanged, and the live protected-partition hashes were verified
  against the recorded values before the write and after it. A read-only
  target-binding re-verification after the write (`target-binding.txt`, UTC
  `2026-11-10T11:46:39Z`) re-read the same p69 full/prefix/tail values before
  the owner booted.
- Rollback `boot-before-n10.15.img.xz` (`4a90e599…` -> raw `a2b5a62d…`)
  preserves the previous N10.14 post-flash image. It was not restored.

The owner supplied the wrapped report and confirmed the TWRP return
(`owner-transcript-n10.15-2026-11-10.txt`, `b0711fcd…`); it preserves the
screen's wrapped-line artifact, including the owner's `one cold boot done,
back to twrp` note, the ` @Gta41` continuation, the `FAIL`/frames/pages/free
lines and the transcribed 35-zero `expected=` field. The report is associated
with the flashed image through the flash record, not an independently
measured running-image hash. The transcript is a screen transcription, not a
captured kernel log, and it carries no boot timestamp.

## 2. What the report shows

```
one cold boot done, back to twrp
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 by
tes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 DRV: online=1 wp=1 reads=10 blocks=31 fails=0 max=8
GTA4L-EMMC P5 XFER: req=31 c17=7 c18=12 c18b=24 cfail=0 stops=12 s
fail=0 last_stop=0
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 content=GUID_
partition_scheme writable=0
GTA4L-EMMC P5 CONSUME: cases=1 passed=1 failed=0 reason=0 complete
=1 reads=1 bytes=1024 span=1024 blocks=2 subrequests=1 max=2 forei
gn=0 fails=0 cancel=0 budget=0 media=0 part=0 open=1 close=1 alloc
=0 client=1 mismatches=0 limit=64 seg=1 cap=61071360
GTA4L-EMMC P5 CONSUMEREAD: index=0 target=1 start=0 length=1024 sp
an=1024 actual=1024 status=0 subrequests=1 blocks=2 max=2 digest=9
7479ae876ec6fe6f45b38492b9eb9ac expected=0000000000000000000000000000
0000000 mismatch=0
GTA4L-EMMC P5 PART: count=0 expected=0 waited_ms=0 error=0 reporte
d=0
GTA4L-EMMC P5 SNAP: locked=1
GTA4L-EMMC P5 FAIL: valid=0 stage=0 code=0 block=0 error=0
 @Gta41EmmcBlock.cpp:5719
frames ffffffff007a148e0 ffffffff007a1ae00 ffffffff007a148f8 ffffffff0
074224a4
pages=0x00000000000079e55
free=0x00000000000078eb9
```

- `rc=0 stop=0 mode=3` is header success, not consume success. The MEDIA
  fields repeat the N10.1/N10.2/N10.6/N10.10/N10.14 read-only whole-media
  profile: `whole=1 base=0 size=31268536320 content=GUID_partition_scheme
  writable=0`.
- `CONSUME` is exactly the one-case table of the N10.15 candidate
  (`package/Gta4lEmmcBlock.c`): one case, BOOT target 1, offset 0, length
  1,024 bytes, span 1,024, two blocks, one driver-level subrequest, a largest
  subrequest of two blocks, and the production 64-block/one-segment
  advertisement (`limit=64 seg=1`). `cap=61071360` is the whole-media
  capacity in 512-byte blocks (31,268,536,320 bytes), matching `MEDIA size`.
- `complete=1` is the source guard at
  `package/Gta4lEmmcBlock.cpp:5209-5243`: `cases == 1`, `passed == cases`,
  `failed == 0`, the latched reason is `NONE`, `reads == 1`, `bytes == 1024`,
  `span == 1024`, `blocks == 2`, `subrequests == 1`, `max == 2`,
  `foreign == 0`, `fails == 0`, `cancel == 0`, `budget == 0`, `media == 0`,
  `part == 0`, `open == 1`, `close == 1`, `alloc == 0`, `client == 1`,
  `mismatches == 0`, `limit == 64`, `segments == 1`, and additionally
  `creditedC18 == 1`, `creditedC18b == 2` and `creditedStops == 1` from the
  completion-side private counter deltas. It cannot coexist with a read
  failure, short transfer, wrong offset, wrong partition, open/close
  mismatch, allocation failure, descriptor failure, cancellation, budget
  refusal, foreign traffic, a missing/duplicate/extra case or a wrong
  advertisement.
- `CONSUMEREAD` reports `target=1` (the pinned boot partition), `start=0`,
  `length=1024`, `span=1024`, `actual=1024` and `status=0`, so the requested
  and completed window agree exactly. `digest=97479ae876ec6fe6f45b38492b9eb9ac`
  is the observed first-16-byte SHA-256 of the window and equals the option A
  external reference
  `97479ae876ec6fe6f45b38492b9eb9ac934b092ee8bcdccd3167cdc4eb502c7a`, the
  first 1,024 bytes of the packaged image `777b446c…`. `expected` is all
  zeros by design (no expected digest is compiled in) and `mismatch=0` is
  the option A form; the content comparison is host-side only and a mismatch
  would fail acceptance without changing the on-device `complete` field.
- `media=0 part=0 foreign=0` are clear failure flags: a whole-media selection
  or profile failure, a boot-partition lookup failure or a driver-level
  request that cannot be attributed to the active case would set the
  corresponding flag and stop or latch `complete=0`. `alloc=0` records that
  the 32 KiB `IOMallocContiguous` buffer and the one-segment
  `withAddress(buffer, 1024, kIODirectionIn)+kIOMemoryAutoPrepare` descriptor
  were accepted, and `open=1 close=1 client=1` record the consumer's open,
  close and client-release lifecycle.
- `DRV reads=10 blocks=31 fails=0 max=8` and `XFER req=31 c17=7 c18=12
  c18b=24 cfail=0 stops=12 sfail=0 last_stop=0` are whole-boot driver-path
  aggregates. They are not additive with the consume counters: `XFER req`
  equals `DRV blocks=31`, and the transport issued 7 single-block CMD17 reads
  plus 12 two-block CMD18 chunks (24 blocks) with one successful CMD12 stop
  per chunk, no chunk failure, no stop failure and no failed final stop
  (`last_stop=0`). The consume case accounts for one of the 12 chunks (two
  blocks, one stop); the remaining 11 chunks and 7 CMD17 reads form the same
  29-block incidental overlay outside the case observation window that the
  N9.6/N9.7 partcmp, N10.10 breakup and N10.14 sweep boots observed as
  whole-media scan/rounding activity. `DRV max=8` therefore comes from that
  overlay, not from the case, whose largest subrequest is the reported
  `max=2`.
- Consume mode suppresses `READ`/`QUAL`/`QUAL3` through the
  `report->consume == 0` guard at `package/Gta4lEmmcBlock.c:2648`; the zero
  `PART` line is the unchanged legacy line and not a failure or a new pass.
  `SNAP locked=1` and `FAIL valid=0` are the unchanged snapshot and failure
  latch.
- The supplied panic suffix is `@Gta41EmmcBlock.cpp:5719` (spelling
  preserved). In the N10.15 candidate source `86dab8b1…`, line 5719 is the
  intentional `panic("%s", reportBuffer)` in `panicWithReport()`; it is the
  normal terminal report stop, not a fault. No watchdog line is present; the
  eMMC report owned the terminal, and the userspace fs-timer-hold fixture
  kept the screen visible long enough for the owner to transcribe it.
- Transcription artifacts: the owner's wrapped `expected=` field carries 35
  zeros rather than the designed 32, and the panic suffix spells the digit
  `1` in `@Gta41` where the source file is `Gta4lEmmcBlock.cpp`. Both are
  screen-transcription artifacts and do not change the field semantics.

## 3. Stored-image verification and post-boot boundary

The flash record's post-write readback, and the read-only target-binding
re-verification after the write, confirmed that p69 was exactly the flashed
candidate prefix `777b446c…` plus the preserved pre-flash tail `a28b4ea2…`,
that the full partition was `6fa2b7e9…`, and that RECOVERY `14e11370…`, EFS
`51fa824c…` and sec_efs `3a265d58…` were unchanged. That verification happened
before the owner's cold boot.

No post-boot readback is recorded in the evidence directory. The owner
returned the tablet to TWRP after the one cold boot, and this batch authorizes
no further device access, so no independent post-boot content or
protected-partition comparison was performed. As in N10.10 and N10.14, the
stored image after the boot is not independently re-verified in this record;
the report is bound to the candidate by the pre-boot post-flash readback, the
target-binding re-verification and the option A digest comparison against the
packaged image.

## 4. What this does not establish

- One owner-reported cold boot, transcribed from the screen. There is no
  captured kernel log, boot timestamp, rail-reset state or independently
  measured running-image hash; the report is bound through the flash record.
- The result qualifies only the single 1,024-byte window at BOOT offset 0 and
  the consumer lifecycle, production advertisement, attribution and
  driver/reference path it exercised on this one boot. It does not qualify
  arbitrary byte offsets or lengths, arbitrary I/O, filesystem mount, root
  integration, writes, wider or faster modes, DMA/CQE/ICE or initialization
  reliability.
- The digest is an option A external comparison: the device prints the
  observed prefix and the host compares it with the packaged image after the
  run. It is not an on-device content check and no expected digest is
  compiled in.
- The 25,000 ms worst case (20,000 + 2,000 + 3,000 ms) remains conditional
  and accepted by the owner as the basis for this run; it is not a
  path-condition proof or a timing measurement. The absence of a watchdog
  line is consistent with the acceptance but does not prove the margin.
- The host consume model reconstructs the client/provider path and the
  media/breaker seam. This boot is the hardware qualification of the real
  family code and transport for this one window; it does not upgrade the host
  model's status.
- The `DRV`/`XFER` aggregates and the 29-block incidental overlay are
  descriptive whole-boot counters, not a consume-mode transport
  characterization beyond the observed two-block case subrequest.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 `complete=0` records, the
  N9.10 comparator `complete=1`, the N9.11 owner-reported series, the
  N10.1/N10.2 partread results, the N10.6 deblock result, the N10.10 breakup
  result and the N10.14 sweep result are preserved unchanged. N10.14 remains
  the only sweep-mode hardware observation, N10.10 remains the only
  breakup-layer hardware observation and initialization reliability remains
  `[U]`. N10.15 is its own first hardware observation.

## 5. Next steps and gates

This closeout does not authorize another boot, flash, retry, reflash,
rollback, reset/repair or mount. The flash and boot consumed the N10.15
budget: one owner-controlled cold boot, then return to TWRP and stop
regardless of result, with no automatic retry, reflash or rollback. The
tablet is in TWRP; the next milestone is a separate proposal from the
storage-publication roadmap and must not be implemented, built or flashed in
this batch. A captured repeatability series for this unchanged consume image
would be a separate proposal with fresh explicit owner approval and an
explicit boot budget. Any later hardware attempt requires the same.

## 6. Evidence

Raw artifacts and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n10.15-hardware-evidence/` directory. The manifest
`evidence-sha256.txt` (`f09b111d…`) binds `backup-local.log` (`39c75461…`),
`backup.log` (`206db544…`), `boot-before-n10.15.img.xz` (`4a90e599…`),
`boot-ram-root-emmc-n10.15-consume.img.xz` (`2375978e…`), `cleanup.log`
(`1ca8880f…`), `expected-postflash-p69.txt` (`5a72e42c…`),
`flash-commands.log` (`e56ace35…`), `flash-record.json` (`ca26ebee…`),
`next-step.txt` (`f833ad23…`), `owner-transcript-n10.15-2026-11-10.txt`
(`b0711fcd…`), `preflight-hashes.log` (`af03f890…`), `preflight.log`
(`e259988e…`), `stage-local.log` (`0b24c1ba…`), `stage.log` (`61bd5e5e…`),
`target-binding.txt` (`30226596…`) and `write-readback.log` (`500eb9ac…`).
The raw evidence stays outside the tracked tree and records no private
identifier in this document.
