# gta4l N10.20 widthhold hardware observation (2026-11-11)

Status: closed hardware observation with a bounded FAIL verdict. This record
adds no device access, flash, boot, mount, write or retry. The N10.20 one-boot
budget is consumed. Raw owner-era evidence stays outside the tracked tree; this
public record carries only privacy-safe normalized fields and hashes. The
N10.19 BOOT image is left as last written and must not be booted again without
a new, separately approved finite budget.

## 1. Provenance and scope

- The result is an owner-supplied, owner-transcribed wrapped screen report
  from the single approved cold boot. Cold/warm classification is not
  established by the supplied exchange and is not claimed here.
- The raw transcription is owner-held outside this repository. The values
  below are the normalized fields recorded in the plan update; they are not a
  byte-for-byte transcription and no wrapping ambiguity is silently repaired.
- The owner confirmed return to TWRP. No fresh device collection, post-boot
  integrity re-read or extra boot is included.
- The BOOT-only flash record and the candidate image are bound by hashes in
  section 2. No serial number, account, host name, SSH target, network address
  or home path is part of this record.

## 2. Candidate and flash binding

- Candidate source commit: `339318b` (N10.19 host-only candidate).
- BOOT image:
  `7f5455d691402e06f4df0c458c22ecf70b6654ea58c37c11a88aabd2d908a84a`
  (85,688,320 bytes; first 1,024 bytes
  `9fcf8c14b9f184abdb66eec0547814faf2314162bcb1ace5a99a6f213ba01eee`).
- BOOT-only pre-flash p69 (100,663,296 bytes):
  `6fa2b7e9890c0e5971e8d0d5e90cf8d96e595b6e2e92cc748f34ec4556269671`;
  first 1,024 bytes `97479ae876ec6fe6f45b38492b9eb9ac…`, last 1,024 bytes
  `d737414c433ea3c74750f380ca18bd06d76753cce4c3a1071e66841aec608ab0`.
- Post-flash p69 full hash
  `f0940421acedba4498cf69631fdac45ffab83abf65e47b56559926e87a0a1e7d`:
  the candidate prefix, candidate-length region and first 1,024 bytes match
  the image; the p69 tail and last 1,024 bytes are unchanged.
- RECOVERY `14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae`,
  EFS `51fa824c4bea060f6bf42ee9c1a8c95657a35e3f872a8799f5762963a12f37bd`
  and sec_efs
  `3a265d587700b3e4899f7d13617c3cd3ae9aa3c9972d8d47f04f9b13b9c05970`
  were unchanged across the write. These are pre-test measurements, not a
  fresh post-boot integrity check.
- The retained pre-flash p69 image
  (`boot-before-n10.20.img.xz`,
  `ae11113ac673340eb266b6535e15c82aed52a935cb4e2b82429fb22d506ad6ea` to
  raw `6fa2b7e9…`) is not automatically restored, and its persistence is
  treated as unverified. No restore is authorized here.

## 3. Owner-observed report

The WIDTHHOLD family reported:

- `cases=4 passed=0 failed=1 reason=5 complete=0` (reason 5 is `PART`);
- `reads=0 bytes=0 blocks=0 subrequests=0 max=0 c18=0 stops=0 stopfail=0`;
  no case read or tagged subrequest was credited;
- `partbase=0 partsize=0 partro=0` in the old schema, which assigned those
  fields only after every BOOT-partition check passed;
- `switch=4 verified=1 hb=0 ha=2 rb=0 cmd6=1`, so the verified 4-bit switch
  completed;
- `refs=1 refbytes=4096 refblocks=8 refstatus=0 refchunks=4 refstops=4
  refstopfail=0` with `refdigest=fa169295a70cc707bcd18d279507df12`;
- `revert=1 rverified=1 rhb=2 rha=0 rcmd6=1 rstatus=0`, so the verified 1-bit
  revert completed;
- one `WIDTHHOLDFAIL` line whose case fields are all zero/status 0 and whose
  digest field is all zeros. `index=0` is an initialized failure field, not
  evidence that case 0 was attempted. The transcription contains an apparent
  extra wrapped zero in that digest field; it is noted as an ambiguity and not
  repaired.

`online=0` and the intentional report-stop panic (`FAIL valid=0`) are
consistent with the fail-closed terminal path; they do not override the
WIDTHHOLD FAIL or make it a success.

## 4. Host recomputation

The first 4,096 bytes of the packaged N10.19 image hash to
`fa169295a70cc707bcd18d279507df1285039020ae98608c628ca03ced5ee085`, whose
16-byte prefix is exactly the reported `refdigest`. This binds the 1-bit
reference content to the candidate. It does not bind the failed BOOT-partition
selection to any particular property value.

## 5. Bounded verdict

**N10.20 sustained-read qualification FAIL at BOOT-partition discovery.** The
run did not enter the case-read set, and no sustained 4-bit read was observed.
The reported verified switch, matching 1-bit reference and verified revert
show that those stages behaved as specified on this one boot; they do not
establish read reliability, initialization reliability, repeatability or
arbitrary I/O.

## 6. Diagnosis boundary

At `339318b`, `widthholdDiscoverMedia()` returns `reason=5` for several
distinct failures: lookup timeout or duplicates, post-selection size/base
re-read, Writable and the exact `Content == "Apple_Boot"` equality. All of
them leave `partbase`/`partsize`/`partro` zero, so the observed zeros cannot
identify which check refused the media. The all-zero partition fields are
therefore consistent with either lookup failure or a post-selection refusal;
the transcript alone does not distinguish them.

The exact Content string equality is a source-backed suspect, not a proven
cause: the linked partition scheme can publish a type-GUID Content string, and
a storage client may replace Content with its Content Mask. The existing host
fixtures used `Apple_Boot`, so they did not model that shape. An eMMC defect,
other post-selection mismatches and lookup absence are not excluded by this
observation.

## 7. Correction carried by N10.21

The N10.21 host-only correction:

- replaces the friendly-name equality with a structural Content check
  (`OSString` present, value not compared);
- adds a widthhold-only u32 `partdiag` first-failure bitmask (not-found,
  duplicate, size, base, Writable, Content) so a later run can identify the
  failing category without raw Content telemetry;
- recomputes the formatter bounds and updates the host verifier and tests.

This correction changes no transport, timing, read range, request count,
recovery rule or reason number. It does not retroactively pass N10.20, does not
qualify the N10.19 image, and does not authorize a new flash or boot. See
`docs/emmc-n10.21-widthhold-partdiag-candidate-2026-11-11.md` for the host
batch result.

## 8. References

- `docs/emmc-n10.17-sustained-width-read-design-2026-11-11.md`
- `docs/emmc-n10.19-widthhold-candidate-2026-11-11.md`
- `docs/emmc-driver.md`
- `docs/evidence-baseline.md`
- `package/Gta4lEmmcBlock.cpp`
- `scripts/check-gta4l-widthhold-evidence.py`
