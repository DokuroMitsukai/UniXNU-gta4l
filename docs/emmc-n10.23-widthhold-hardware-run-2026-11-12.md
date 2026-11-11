# gta4l N10.23 widthhold hardware run (2026-11-12)

Status: closed hardware run with a bounded **PASS**. One owner-approved BOOT-only
flash and one owner-triggered diagnostic boot were executed. The one-boot
budget is consumed (1/1). No retry, restore, mount, repair or storage write was
performed. Raw owner evidence (screens and the verbatim wrapped transcription)
stays outside the tracked tree; this public record carries only privacy-safe
normalized fields and hashes. The BOOT partition now contains the N10.22 image
and must not be booted again without a new, separately approved finite budget.

## 1. Provenance and scope

- The result is an owner-supplied, owner-transcribed wrapped screen report from
  the single approved boot. The owner returned the tablet to TWRP manually; no
  host-issued reboot occurred, and no force-reboot key combination is part of
  the procedure.
- The raw transcription is owner-held outside this repository. The fields below
  are a separately derived, normalized join of the wrapped lines; wrapping
  artifacts are noted and not silently repaired.
- The run exercised only the N10.22 candidate's widthhold mode: 400 kHz SDR,
  1-bit EXT_CSD baseline, an 8-block/4,096-byte 1-bit BOOT reference, a
  verified 4-bit CMD6 switch, four tagged two-block reads, same-boot
  comparison, a verified 1-bit revert and the intentional terminal report. CMD6
  changes volatile card configuration and is reverted; no persistent card,
  partition or filesystem state was intentionally changed.
- No serial number, account, host name, SSH target, network address or home
  path is part of this record.

## 2. Candidate, preflight and flash binding

- Candidate: `docs/emmc-n10.22-widthhold-candidate-production-2026-11-12.md`.
  Image
  `out/gta4l/ram-root-emmc-n10.22-widthhold-candidate-evidence/boot-ram-root-emmc-n10.22-widthhold.img`,
  SHA-256
  `b4d63be7ddaf70f88139b000e31a3e9e26e0cc849757db4f49696e3b9ce0877d`,
  85,688,320 bytes. Kernel `04ae57f6…`, payload `64914089…`.
- Read-only preflight: TWRP 3.7.1 (`gta4l`), battery 100% (Full), verified boot
  state `orange`, no slot suffix. `boot` is `p69` (196,608 sectors =
  100,663,296 bytes), `recovery` is `p70`, `efs` is `p64`, `sec_efs` is `p63`.
- Pre-flash BOOT full hash
  `f0940421acedba4498cf69631fdac45ffab83abf65e47b56559926e87a0a1e7d` and
  prefix
  `7f5455d691402e06f4df0c458c22ecf70b6654ea58c37c11a88aabd2d908a84a`
  matched the N10.20 post-flash records exactly, confirming that the N10.19
  image was still installed. Tail
  `a28b4ea28a157a4d55e8574fd5ea32b72f261cde679feeedd11b714584d1e27f`, last
  1 KiB `a4f065b4f14f4911b49f8bd68fef979862d19e1611146a28ee62c3f80df0746f`,
  RECOVERY `14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae`,
  EFS `51fa824c4bea060f6bf42ee9c1a8c95657a35e3f872a8799f5762963a12f37bd`
  and sec_efs
  `3a265d587700b3e4899f7d13617c3cd3ae9aa3c9972d8d47f04f9b13b9c05970`
  were records consistent with the prior closeout.
- Verified full backups of BOOT, RECOVERY, EFS and sec_efs were taken to the
  owner's private store before any write; their hashes matched the device-side
  reads exactly.
- The image was staged on the owner-controlled jump host and its hash verified
  before the write. The flash wrote exactly 20,920 × 4,096 bytes to `p69`; no
  wipe, repartition, tail zeroing or other partition write was performed.
- Post-write verification: candidate prefix
  `b4d63be7ddaf70f88139b000e31a3e9e26e0cc849757db4f49696e3b9ce0877d`;
  full p69
  `7260ad75caf7522b1e6ff7ccb63979b2096dd7d68fd580b1844574e3419a34cb`
  (new, expected, because the unchanged tail is included); tail, last 1 KiB and
  the three protected partitions identical to pre-flight.

## 3. Owner-observed report

Normalized from the wrapped transcription:

- `REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320 wid=0
  tim=0 clk=400000`.
- `DRV: online=1 wp=1 reads=14 blocks=45 fails=0 max=8`.
- `XFER: req=45 c17=7 c18=19 c18b=38 cfail=0 stops=19 sfail=0 last_stop=0`.
- `MEDIA: whole=1 base=0 size=31268536320 content=GUID_partition_scheme
  writable=0` (whole-media content of the partition scheme; not a partition
  label).
- `WIDTHHOLD`: `cases=4 passed=4 failed=0 reason=0 complete=1 switch=4
  verified=1 hb=0 ha=2 rb=0 cmd6=1 refs=1 refbytes=4096 refblocks=8 refstatus=0
  refchunks=4 refstops=4 refstopfail=0
  refdigest=417cdef0f05bceafea13119a3b9f1ff5 reads=4 bytes=4096 blocks=8
  subrequests=4 max=2 c18=4 stops=4 stopfail=0 match=1 mismatches=0 fails=0
  foreign=0 cancel=0 budget=0
  obsdigest=417cdef0f05bceafea13119a3b9f1ff5 partbase=1154744320
  partsize=100663296 partro=1 partdiag=0 revert=1 rverified=1 rhb=2 rha=0
  rcmd6=1 rstatus=0 limit=64 seg=1 cap=61071360`.
- `WIDTHHOLDREAD`: `index=3 lba=2255366 start=3072 length=1024 span=1024
  actual=1024 status=0 subrequests=1 blocks=2 max=2
  digest=5f70bf18a086007016e948b04aed3b82
  predigest=5f70bf18a086007016e948b04aed3b82 match=1`.
- `SNAP: locked=1`.
- Terminal `FAIL: valid=0 stage=0 code=0 block=0 error=0
  @Gta41EmmcBlock.cpp:8024`, with four wrapped frame values, `pages` and
  `free` values. `valid=0` is the intentional report-stop path; it does not
  override the WIDTHHOLD success. The source file name is transcribed with an
  `l`/`1` ambiguity (`Gta41` vs `Gta4l`) and is not repaired; the wrapped
  `pages`/`free` digit counts are likewise kept as transcribed.
- Wrapping artifacts normalized without changing values: `by tes` → `bytes`,
  `GUID_` + `partition_scheme` → `GUID_partition_scheme`, `comple` + `te`,
  `s` + `fail`, `par` + `tdiag`, and digest continuations. `wid=0` is the
  post-revert bus width, as designed.

## 4. Host verification

- `scripts/check-gta4l-widthhold-evidence.py` was executed against the exact
  N10.22 image with the normalized WIDTHHOLD/WIDTHHOLDREAD lines: **PASS**.
  All amended predicates hold, including `partdiag=0`, four passes and zero
  failures, verified switch/revert, zero cancellation/budget/foreign traffic,
  and matching observed/reference digests. The verifier self-test also passes.
- The reported `refdigest`/`obsdigest` prefix
  `417cdef0f05bceafea13119a3b9f1ff5` equals the SHA-256 prefix of the final
  image's first 4,096 bytes as recomputed on the host; the reported
  `WIDTHHOLDREAD` prefix `5f70bf18a086007016e948b04aed3b82` equals the host
  digest of the 1,024-byte slice at offset 3,072. Both device digests therefore
  bind the observed and reference windows to the exact image that was flashed.
- The 1 KiB host hash of the final image is
  `338ebbe6ab3da764f0ad87e30c2785742ea250e321b1a78bb63f914490b506f1`.

## 5. Bounded verdict

**N10.23 sustained 4-bit width-hold read qualification PASS on one bounded
boot.** The run entered the case set, completed all four two-block reads,
matched every observed window against the 1-bit reference, performed a
verified switch and a verified revert, and latched no PART diagnostic. The
BOOT partition was discovered this time (`partbase=1154744320`,
`partsize=100663296`, `partro=1`), so the structural Content qualification did
not refuse it. No `WIDTHHOLDFAIL` line was emitted.

This PASS establishes only this single cold/warm-unspecified run and this
bounded operation. It does not establish repeatability, initialization
reliability, failure rates, arbitrary I/O safety, general 4-bit publication
readiness or any post-publication durability. The cold/warm classification is
not established by the supplied report.

## 6. Post-run integrity

A read-only re-measurement after the boot returned to TWRP produced the same
values as the post-flash check: full p69
`7260ad75caf7522b1e6ff7ccb63979b2096dd7d68fd580b1844574e3419a34cb`,
candidate prefix `b4d63be7…`, tail `a28b4ea2…`, last 1 KiB `a4f065b4…`,
RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…`. No unintended
BOOT, tail or protected-partition change is present.

## 7. Diagnosis boundary

N10.20 remains **FAIL** at BOOT-partition discovery at `339318b`; N10.23 does
not retroactively prove its cause. The only source change between the two
candidates is the N10.21 correction (structural Content check plus the
`partdiag` field), and N10.23's `partdiag=0` shows no PART category was latched
on this run. That is consistent with the structural check resolving the
refusal while not excluding other differences or run-to-run variability. No
eMMC defect is inferred or excluded.

## 8. Gates and next steps

- Budget consumed 1/1. No second boot was issued; BOOT stays as written
  (N10.22) and must not be booted again without a new finite budget.
- Restoring a pre-test image is a separate owner decision and requires a
  specifically identified, hash-verified backup; no restore is implied.
- Further qualification (repeat runs, faster clocks or wider buses, other
  partitions, publication readiness) requires separate designs, protocols and
  approvals.
- Host records and references are updated with this run; no commit or push is
  part of this record's authorization.

## 9. References

- `docs/emmc-n10.22-widthhold-candidate-production-2026-11-12.md`
- `docs/emmc-n10.21-widthhold-partdiag-candidate-2026-11-11.md`
- `docs/emmc-n10.20-widthhold-observation-2026-11-11.md`
- `docs/emmc-n10.17-sustained-width-read-design-2026-11-11.md`
- `docs/emmc-driver.md`
- `scripts/check-gta4l-widthhold-evidence.py`
- `package/Gta4lEmmcBlock.cpp`
