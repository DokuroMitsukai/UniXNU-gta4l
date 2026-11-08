# gta4l N10.6 unaligned deblocked read qualification hardware result (2026-11-08)

Status: BOOT-only flash of the N10.5 unaligned deblocked read candidate and one
owner-reported first cold boot on 2026-11-08. The report selected the read-only
whole media and the pinned boot partition, performed the eleven fixed
byte-unaligned reads through the real
`IOMedia`/`IOBlockStorageDriver`/`IODeblocker` path, and emitted `cases=11
passed=11 failed=0 reason=0 complete=1 reads=11 bytes=54364 span=56832
blocks=111 fails=0 cancel=0 budget=0 media=0 part=0 seg=3 cap=61071360`. The
last case emitted `DEBLOCKREAD: index=10 target=1 start=4096 length=1024
span=1024 actual=1024 status=0 digest=b50aab9d52e4d1e36078fa5120e3aa1c`, the
first 16 bytes of the pinned case-10 digest. The whole-boot transport counters
show a largest subrequest of 24 blocks, exactly three 4 KiB physical segments,
which matches the mode-scoped three-segment advertisement and shows that the
real `IOBreaker`/`breakUpRequest()` split path was entered rather than the
direct `executeRequest()` short-circuit. The flash's post-write readback
verified the stored BOOT before the boot; no post-boot readback was taken. The
owner returned the tablet to TWRP; the one-boot budget is consumed. No commit,
push, retry, reflash, rollback, mount, repair or further device access was
performed. This is a bounded same-boot qualification of eleven pinned unaligned
ranges and the real deblock/breakup layers on one boot `[C]`, not an
initialization-reliability or general I/O result.

## 1. Flash and boot scope

The owner approved the BOOT-only write in this session and controlled the one
cold boot. The flash record is in
`out/gta4l/ram-root-emmc-n10.6-hardware-evidence/` (`flash-record.json`
`49adc70d…`, flash evidence manifest `evidence-sha256.txt` `360031f6…`).

- BOOT image `bacc933f…` (85,688,320 bytes), payload `924c7bbd…` (83,886,080
  bytes), prelinked kernel `b2837a5b…`, packaged kernel `00615136…`,
  requalified archive `f3789061…`.
- Candidate source `Gta4lEmmcBlock.cpp` `2721a6a7…`, policy source
  `Gta4lEmmcBlock.c` `4cfbc466…` and header `Gta4lEmmcBlock.h` `0aae4cc4…`.
- The pre-flash p69 was the N10.2 post-flash image `4225defb…`; its tail after
  the 85,688,320-byte candidate offset was `a28b4ea2…`.
- Only `/dev/block/mmcblk0p69` was written (20,920 x 4096 records, synced).
  The post-flash full readback `f4882047…` matched the expected
  candidate-prefix plus unchanged-tail value; prefix `bacc933f…`, tail
  `a28b4ea2…`. RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…`
  were unchanged, and the live protected-partition hashes were verified
  against the recorded values before the write. A read-only target-binding
  re-verification after the write (`target-binding.txt`, UTC
  `2026-11-08T21:16:21Z`) re-read the same p69 full/prefix/tail values before
  the owner booted.
- Rollback `boot-before-n10.6.img.xz` (`0a0fe70a…` -> raw `4225defb…`)
  preserves the previous N10.2 post-flash image. It was not restored.

The owner supplied the wrapped report and confirmed the TWRP return. The raw
screen transcription is `owner-transcript-n10.6-2026-11-08.txt` (`034bcce0…`);
it preserves the screen's wrapped-line artifact. The report is associated with
the flashed image through the flash record, not an independently measured
running-image hash. The transcript is a screen transcription, not a captured
kernel log.

## 2. What the report shows

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 DRV: online=1 wp=1 reads=22 blocks=140 fails=0 max=24
GTA4L-EMMC P5 XFER: req=140 c17=12 c18=64 c18b=128 cfail=0 stops=64 sfail=0 last_stop=0
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 DEBLOCK: cases=11 passed=11 failed=0 reason=0 complete=1 reads=11 bytes=54364 span=56832 blocks=111 fails=0 cancel=0 budget=0 media=0 part=0 seg=3 cap=61071360
GTA4L-EMMC P5 DEBLOCKREAD: index=10 target=1 start=4096 length=1024 span=1024 actual=1024 status=0 digest=b50aab9d52e4d1e36078fa5120e3aa1c
GTA4L-EMMC P5 PART: count=0 expected=0 waited_ms=0 error=0 reported=0
GTA4L-EMMC P5 READ: lba=1 attempted=0 ok=0 bytes=0 sum=0 base=2228245566 same=0
GTA4L-EMMC P5 QUAL: ref=0 ref_lba=0 ref_blocks=0 two_att=0 two_ok=0 two_reason=0 two_bytes=0 two_mismatch=0 two_off=0 two_match=0 two_c18=0 two_c17=0 two_stops=0 two_sfail=0
GTA4L-EMMC P5 QUAL3: att=0 ok=0 reason=0 bytes=0 mismatch=0 off=0 match=0 c18=0 c17=0 stops=0 sfail=0
GTA4L-EMMC P5 SNAP: locked=1
GTA4L-EMMC P5 FAIL: valid=0 stage=0 code=0 block=0 error=0
```

- `rc=0 stop=0 mode=3` is header success, not deblock success. The MEDIA
  fields repeat the N10.1/N10.2 read-only whole-media profile: `whole=1 base=0
  size=31268536320 content=GUID_partition_scheme writable=0`. `base=0` is the
  implied zero base, not a published `Base` property.
- `cases=11` is the fixed case count in `package/Gta4lEmmcBlock.c:154-267`.
  `passed=11 failed=0 reason=0` means every pinned case completed and matched
  its retained digest. The eleven ranges and their expected digest prefixes
  are:

| index | target | start | length | span | blocks | expected SHA-256 (first 8…last 6) |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | whole | 100 | 924 | 1024 | 2 | `5f4f363c…301d3719` |
| 1 | whole | 0 | 600 | 1024 | 2 | `e3ef996d…07d59d01` |
| 2 | whole | 100 | 200 | 512 | 1 | `6d9c54de…e0c3a33a` |
| 3 | whole | 100 | 600 | 1024 | 2 | `062e84ac…d90dae55` |
| 4 | whole | 100 | 16000 | 16384 | 32 | `4f499bf0…19cd0fe2` |
| 5 | whole | 512 | 1024 | 1024 | 2 | `4e3575fe…474d1f70` |
| 6 | boot | 4196 | 924 | 1024 | 2 | `ff346b8d…3e36d7d1` |
| 7 | boot | 1048676 | 200 | 512 | 1 | `cdb2d0c4…f6b7bf53` |
| 8 | boot | 100630528 | 32668 | 32768 | 64 | `095a6573…5bf4cc63` |
| 9 | boot | 100662884 | 200 | 512 | 1 | `6d9c54de…e0c3a33a` |
| 10 | boot | 4096 | 1024 | 1024 | 2 | `b50aab9d…aa959774` |

- The aggregate `reads=11 bytes=54364 span=56832 blocks=111` is exactly the
  table: 924 + 600 + 200 + 600 + 16000 + 1024 + 924 + 200 + 32668 + 200 + 1024
  = 54,364 requested bytes; 56,832 aligned bytes; 56,832 / 512 = 111 blocks.
  Cases 0-4 exercise head-only, tail-only, both ends inside one block, both
  ends across two blocks and both ends across a multi-page 32-block span;
  cases 6-9 exercise the boot base with head-only, same-block, 64-block
  tail-only and same-block-at-media-end reads; cases 5 and 10 are aligned
  controls that construct no deblocker. Case 9 shares case 2's digest because
  both 200-byte ranges are zero-filled.
- `complete=1` is the source guard at `package/Gta4lEmmcBlock.cpp:2835-2845`:
  `cases != 0`, `passed == cases`, `failed == 0`, the latched reason is `NONE`,
  `cancel == 0`, `budget == 0`, `media == 0`, `part == 0` and `segments == 3`.
  It cannot coexist with a read failure, short transfer, hash mismatch,
  cancellation, budget refusal, media/partition/open failure or a wrong
  segment advertisement.
- `media=0 part=0` are clear failure flags, not success flags. In
  `package/Gta4lEmmcBlock.cpp:2156-2168`, `deblock_media` is set to `1` only
  when the latched reason is `GTA4L_EMMC_BLOCK_DEBLOCK_REASON_MEDIA` (1), and
  `deblock_part` is set to `1` only when it is
  `GTA4L_EMMC_BLOCK_DEBLOCK_REASON_PART` (2). Both zero therefore means
  neither failure occurred.
- `seg=3` is the mode-scoped `kIOMaximumSegmentCountReadKey` advertisement
  (`package/Gta4lEmmcBlock.cpp:864-867`); the legacy, partobs, partcmp and
  partread modes keep `1`. `cap=61071360` is the whole-media capacity in
  512-byte blocks (31,268,536,320 bytes), matching the `MEDIA size`.
- `DEBLOCKREAD: index=10 target=1 start=4096 length=1024 span=1024
  actual=1024 status=0 digest=b50aab9d52e4d1e36078fa5120e3aa1c` is the final
  aligned control: boot offset 4,096, 1,024 requested bytes, 1,024 aligned
  bytes, `kIOReturnSuccess`, 1,024 actual bytes. The printed 16 digest bytes
  are exactly the first 16 bytes of the pinned 32-byte case-10 digest at
  `package/Gta4lEmmcBlock.c:258-265`
  (`b50aab9d52e4d1e36078fa5120e3aa1c…aa959774`), and they match the N10.5
  candidate table. `DEBLOCKREAD` records the most recent case, not an
  aggregate; the aggregate is `reads=11`.
- `DRV reads=22 blocks=140 fails=0 max=24` and `XFER req=140 c17=12 c18=64
  c18b=128 cfail=0 stops=64 sfail=0 last_stop=0` are whole-boot aggregate
  counters that include the eleven deblocked reads. They are not additive with
  `DEBLOCK reads=11 blocks=111`: the deblock counters count only the pinned
  case-table requested bytes, aligned spans and `span / 512`. `XFER req` is a
  requested-block count, not a request count; it equals `DRV blocks=140`. The
  transport issued 12 single-block CMD17 reads and 64 two-block CMD18 chunks
  (128 blocks) with one successful CMD12 stop per chunk and no stop failure.
  The 29-block difference from the 111-block case-table span is the
  deblocker's head/tail scratch reads and block rounding.
- `max=24` is the largest single storage-driver subrequest: 24 blocks,
  12,288 bytes. It is exactly three 4 KiB physical segments (3 x 8 blocks).
  The one-segment partread/comparator modes fold only the first 4 KiB segment
  and the N10.1/N10.2 reports show `max=8`; the deblock mode folds three
  segments and shows `max=24`. Because case 8's aligned span is 64 blocks, a
  direct `executeRequest()` short-circuit would have passed 64 blocks; the
  observed maximum therefore shows the real `IOBreaker::getBreakSize()` /
  `breakUpRequest()` split path was entered for that case. This is the first
  recorded N-series hardware observation of the three-segment fold and of a
  transport request larger than the eight-block one-segment break.
- The zero `PART`/`READ`/`QUAL`/`QUAL3` lines are inactive legacy mode-1/mode-3
  and N8 `PARTOBS` paths in this deblock boot, not failures or new passes.
  `SNAP locked=1` and `FAIL valid=0` are the unchanged snapshot and failure
  latch.
- The supplied panic suffix is `@Gta41EmmcBlock.cpp:3172` (spelling
  preserved). In the candidate source `2721a6a7…`, lines 3165-3172 are the
  intentional `panic("%s", reportBuffer)` in `panicWithReport()`; it is the
  normal terminal report stop, not a fault.

## 3. Stored-image verification and post-boot boundary

The flash record's post-write readback, and the read-only target-binding
re-verification after the write, confirmed that p69 was exactly the flashed
candidate prefix `bacc933f…` plus the preserved pre-flash tail `a28b4ea2…`,
that the full partition was `f4882047…`, and that RECOVERY `14e11370…`, EFS
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
- The result qualifies only the eleven pinned unaligned ranges and the real
  deblock/breakup path they exercised on this one boot. It does not qualify
  arbitrary byte offsets or lengths, arbitrary I/O, filesystem mount, root
  integration, writes, wider or faster modes, DMA/CQE/ICE or initialization
  reliability.
- The boot-tail cases 8 and 9 address the end of the full 100,663,296-byte
  boot partition, which the 85,688,320-byte BOOT image does not reach. Cases
  6, 7 and 10 are inside the gate-pinned 1,798,052-byte kernel section and are
  invariant across candidates.
- The three-segment advertisement is mode-scoped and unchanged from N10.4.
  This boot observed the resulting 24-block fold, but it does not prove that
  every later allocation produces the same physical segmentation; a
  deterministic breaker-split case remains a future rung.
- The host deblock model is a reconstruction. This boot is the hardware
  qualification of the real family code for the pinned table; it does not
  upgrade the host model's status.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 `complete=0` records, the
  N9.10 comparator `complete=1`, the N10.1 hardware result and its captured
  repeatability series, and the N10.2 hardware result are preserved unchanged.
  Initialization reliability remains `[U]`.

## 5. Next steps and gates

This closeout does not authorize another boot, flash, retry, reflash,
rollback, reset/repair, mount, commit or push. The next milestone is a separate
proposal from the storage-publication roadmap; it must not be implemented,
built or flashed in this batch. A captured repeatability series for this
unchanged eleven-case image would be a separate proposal with a fresh explicit
owner approval and an explicit boot budget. Any later hardware attempt
requires the same.

## 6. Evidence

Raw artifacts and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n10.6-hardware-evidence/` directory. The manifest
`evidence-sha256.txt` (`360031f6…`) binds `preflight.log` (`0c16ecab…`),
`backup.log` (`f8964a04…`), `stage-local.log` (`fae67dfd…`), `stage.log`
(`758db687…`), `write-readback.log` (`cfb5f614…`), `cleanup.log`
(`66199539…`), `target-binding.txt` (`ea1ae944…`), `flash-record.json`
(`49adc70d…`), `flash-commands.log` (`26772639…`), `next-step.txt`
(`c29ff743…`), `owner-transcript-n10.6-2026-11-08.txt` (`034bcce0…`),
`boot-before-n10.6.img.xz` (`0a0fe70a…`) and
`boot-ram-root-emmc-n10.5-deblock.img.xz` (`15202a04…`). The raw evidence
stays outside the tracked tree and records no private identifier in this
document.
