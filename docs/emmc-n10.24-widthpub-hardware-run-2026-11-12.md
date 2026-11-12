# gta4l N10.24 widthpub hardware run (2026-11-12)

Status: closed hardware run with a bounded **PASS**. One owner-approved BOOT-only
flash and one owner-triggered diagnostic cold boot were executed. The one-boot
budget is consumed (1/1). No retry, restore, mount, repair or storage write was
performed. Raw owner evidence (screens and the verbatim wrapped transcription)
stays outside the tracked tree; this public record carries only privacy-safe
normalized fields and hashes. The BOOT partition now contains the N10.24 image
and must not be booted again without a new, separately approved finite budget.

## 1. Provenance and scope

- The result is an owner-supplied, owner-transcribed wrapped screen report from
  the single approved boot. The owner returned the tablet to TWRP manually; no
  host-issued reboot occurred, and no force-reboot key combination is part of
  the procedure.
- The raw transcription is owner-held outside this repository. The fields below
  are a separately derived, normalized join of the wrapped lines; wrapping
  artifacts are noted and not silently repaired.
- The run exercised only the N10.24 candidate's widthpub mode: 400 kHz SDR,
  1-bit EXT_CSD baseline, an 8-block/4,096-byte 1-bit BOOT reference in four
  chunks, a verified 4-bit CMD6 switch, two passes over the four two-block
  offsets 0, 1,024, 2,048 and 3,072 bytes (eight tagged cases under one shared
  3,000 ms case-loop budget), the publication marker, a verified 1-bit revert
  and the intentional terminal report. CMD6 changes volatile card
  configuration and is reverted; no persistent card, partition or filesystem
  state was intentionally changed.
- No serial number, account, host name, SSH target, network address or home
  path is part of this record.

## 2. Candidate, preflight and flash binding

- Candidate:
  `docs/emmc-n10.24-widthpub-design-2026-11-12.md` §8. Image
  `out/gta4l/ram-root-emmc-n10.24-widthpub-candidate-evidence/boot-ram-root-emmc-n10.24-widthpub.img`,
  SHA-256
  `727595944324f5a439e4fe9ec1b3fddc2da979d731a4f9589d7d12013e78d922`,
  85,688,320 bytes. Kernel `e65d0625…`, payload `c852dc80…`.
- Read-only preflight: TWRP 3.7.1 (`gta4l`), battery 100% (Full), verified boot
  state `orange`, no slot suffix. `boot` is `p69` (196,608 sectors =
  100,663,296 bytes), `recovery` is `p70` (103,546,880 bytes), `efs` is `p64`
  and `sec_efs` is `p63` (16,777,216 bytes each).
- Pre-flash BOOT full hash
  `7260ad75caf7522b1e6ff7ccb63979b2096dd7d68fd580b1844574e3419a34cb`
  matched the N10.23 post-write record exactly, confirming that the N10.22
  image (`b4d63be7…`) was still installed. Tail
  `a28b4ea28a157a4d55e8574fd5ea32b72f261cde679feeedd11b714584d1e27f`,
  partition-end last 1 KiB
  `d737414c433ea3c74750f380ca18bd06d76753cce4c3a1071e66841aec608ab0`,
  RECOVERY `14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae`,
  EFS `51fa824c4bea060f6bf42ee9c1a8c95657a35e3f872a8799f5762963a12f37bd`
  and sec_efs
  `3a265d587700b3e4899f7d13617c3cd3ae9aa3c9972d8d47f04f9b13b9c05970`
  were consistent with the prior closeout.
- The pre-flash BOOT content is the N10.22 image plus the unchanged tail; the
  N10.22 image is retained in the evidence tree, and the previously verified
  BOOT/RECOVERY/EFS/sec_efs backups remain in the owner's private store. No
  new backup was taken for this write.
- The image was staged on the owner-controlled jump host and its hash verified
  before the write. The flash wrote exactly 20,920 × 4,096 bytes to `p69`; no
  wipe, repartition, tail zeroing or other partition write was performed.
- Post-write verification: candidate prefix
  `727595944324f5a439e4fe9ec1b3fddc2da979d731a4f9589d7d12013e78d922`;
  full p69
  `7e209188a324fa2e6b163c5c1a7cc98cbcb1b7d48c6e24a4ffc832cbb89704fa`
  (new, expected, because the unchanged tail is included); image-region last
  1 KiB `c869228a8c986e6caf437772e9511988d6a16d4057f0675a2061b0471b45f328`,
  partition-end last 1 KiB `d737414c…`, tail `a28b4ea2…` and the three
  protected partitions identical to pre-flight.

## 3. Owner-observed report

Normalized from the wrapped transcription:

- `REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320
  wid=0 tim=0 clk=400000`.
- `DRV: online=1 wp=1 reads=18 blocks=53 fails=0 max=8`.
- `XFER: req=53 c17=7 c18=23 c18b=46 cfail=0 stops=23 sfail=0 last_stop=0`.
- `MEDIA: whole=1 base=0 size=31268536320 content=GUID_partition_scheme
  writable=0` (whole-media content of the partition scheme; not a partition
  label).
- `WIDTHPUB`: `cases=8 passed=8 failed=0 reason=0 complete=1 pub=1 switch=4
  verified=1 hb=0 ha=2 rb=0 cmd6=1 refs=1 refbytes=4096 refblocks=8 refstatus=0
  refchunks=4 refstops=4 refstopfail=0
  refdigest=2469ee44e806df84cd1464ddc959d3ea reads=8 bytes=8192 blocks=16
  subrequests=8 max=2 c18=8 stops=8 stopfail=0 match=1 mismatches=0 fails=0
  foreign=0 cancel=0 budget=0
  obsdigest=2469ee44e806df84cd1464ddc959d3ea partbase=1154744320
  partsize=100663296 partro=1 partdiag=0 revert=1 rverified=1 rhb=2 rha=0
  rcmd6=1 rstatus=0 limit=64 seg=1 cap=61071360`.
- `WIDTHPUBREAD`: `index=7 lba=2255366 start=3072 length=1024 span=1024
  actual=1024 status=0 subrequests=1 blocks=2 max=2
  digest=5f70bf18a086007016e948b04aed3b82
  predigest=5f70bf18a086007016e948b04aed3b82 match=1`.
- `SNAP: locked=1`.
- Terminal `FAIL: valid=0 stage=0 code=0 block=0 error=0
  @Gta41EmmcBlock.cpp:8167`, with four frame values, `pages` and `free`
  values. `valid=0` is the intentional report-stop path; it does not override
  the WIDTHPUB success. The source file name is transcribed with an `l`/`1`
  ambiguity (`Gta41` vs `Gta4l`) and is not repaired; the four frame values
  are kept as transcribed, where all four show 17 hex digits rather than the
  usual 16-digit width.
- Wrapping artifacts normalized without changing values: `by tes` → `bytes`,
  `s` + `fail` → `sfail`, `complet` + `e`, `refbyte` + `s`, `misma` + `tches`,
  `l` + `imit`, `span=` + `1024` and digest continuations. `wid=0` is the
  post-revert bus width, as designed.
- The whole-boot counters `DRV reads=18 blocks=53` and `XFER req=53 c18=23
  c18b=46 stops=23` are eight case CMD18 chunks higher than the N10.23
  four-case values, as expected for the two-pass eight-case set; they include
  the same incidental overlay observed by earlier boots.

## 4. Host verification

- `scripts/check-gta4l-widthpub-evidence.py` was executed against the exact
  N10.24 image with the normalized WIDTHPUB/WIDTHPUBREAD lines: **PASS**.
  All predicates hold, including `pub=1`, `partdiag=0`, eight passes and zero
  failures, verified switch/revert, zero cancellation/budget/foreign traffic,
  and matching observed/reference digests. The verifier self-test also passes.
- The reported `refdigest`/`obsdigest` prefix
  `2469ee44e806df84cd1464ddc959d3ea` equals the SHA-256 prefix of the final
  image's first 4,096 bytes as recomputed on the host; the reported
  `WIDTHPUBREAD` prefix `5f70bf18a086007016e948b04aed3b82` equals the host
  digest of the 1,024-byte slice at offset 3,072. Both device digests therefore
  bind the observed and reference windows to the exact image that was flashed.
- The first/last 1 KiB host hashes of the final image are
  `e7e2aab4d9ffe90d81b2258d12b0a61f521a468fd986780e1054955938a52567`
  and
  `c869228a8c986e6caf437772e9511988d6a16d4057f0675a2061b0471b45f328`.

## 5. Bounded verdict

**N10.24 published-media read qualification PASS on one bounded boot.** The
run entered the eight-case set through the published BOOT partition client,
completed both passes over the four two-block offsets, matched every observed
window against the 1-bit reference, set `pub=1` only after whole-media and BOOT
publication at 4-bit, performed a verified switch and a verified revert, and
latched no PART diagnostic. No `WIDTHPUBFAIL` line was emitted.

This PASS establishes only this single cold boot and this bounded operation. It
does not establish repeatability, initialization reliability, failure rates,
arbitrary I/O safety, general 4-bit publication readiness, mount or write
safety, or any post-publication durability. The `pub=1` marker records
publication, not success by itself; success also requires `complete=1`,
`partdiag=0`, `revert=1` and `rverified=1`, all of which hold here.

## 6. Post-run integrity

A read-only re-measurement after the boot returned to TWRP produced the same
values as the post-flash check: full p69
`7e209188a324fa2e6b163c5c1a7cc98cbcb1b7d48c6e24a4ffc832cbb89704fa`,
candidate prefix `72759594…`, tail `a28b4ea2…`, image-region last 1 KiB
`c869228a…`, partition-end last 1 KiB `d737414c…`, RECOVERY `14e11370…`,
EFS `51fa824c…` and sec_efs `3a265d58…`. No unintended BOOT, tail or
protected-partition change is present.

## 7. Diagnosis boundary

N10.20 remains **FAIL** at BOOT-partition discovery; N10.23 remains the captured
widthhold PASS, and this N10.24 run does not retroactively explain N10.20's
failure. The N10.24 `partdiag=0` shows the structural Content check did not
refuse the BOOT partition on this run and that the published client worked,
which is consistent with the N10.21 correction while not excluding other
differences or run-to-run variability. No eMMC defect is inferred or excluded.

## 8. Gates and next steps

- Budget consumed 1/1. No second boot was issued; BOOT stays as written
  (N10.24) and must not be booted again without a new finite budget.
- Restoring a pre-test image is a separate owner decision and requires a
  specifically identified, hash-verified backup; no restore is implied.
- Further qualification (repeat runs, faster clocks or wider buses, other
  partitions, sustained or arbitrary I/O, general publication readiness)
  requires separate designs, protocols and approvals.
- Host records and references are updated with this run; the documentation
  commit is the only repository action authorized by the owner's instruction.

## 9. References

- `docs/emmc-n10.24-widthpub-design-2026-11-12.md`
- `docs/emmc-n10.23-widthhold-hardware-run-2026-11-12.md`
- `docs/emmc-n10.22-widthhold-candidate-production-2026-11-12.md`
- `docs/emmc-n10.21-widthhold-partdiag-candidate-2026-11-11.md`
- `docs/emmc-n10.20-widthhold-observation-2026-11-11.md`
- `docs/emmc-n10.17-sustained-width-read-design-2026-11-11.md`
- `docs/emmc-driver.md`
- `scripts/check-gta4l-widthpub-evidence.py`
- `package/Gta4lEmmcBlock.cpp`
