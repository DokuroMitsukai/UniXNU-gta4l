# gta4l N10.21 widthhold PART diagnostic candidate (2026-11-11)

Status: host-only corrective batch. No device access, flash, boot, mount,
write or hardware run occurred. The N10.20 one-boot budget is consumed and its
run failed at BOOT-partition discovery; this batch corrects the source and the
report schema. No new candidate kernel, payload or BOOT image was built here.
A source-bound candidate build plus storage-ABI audit, a new hardware protocol
and a new finite boot budget are separate gates. The N10.19 BOOT image is left
as last written and must not be booted again without such approval.

## 1. Trigger

N10.20 failed with `reason=5` (`PART`), `reads=0`, `failed=1` at BOOT-partition
discovery, after a verified 4-bit switch, a matching 1-bit reference
(`refdigest=fa169295a70cc707bcd18d279507df12`) and a verified 1-bit revert.
At `339318b` that reason covers lookup timeout/duplicates, post-selection
size/base rereads, Writable and an exact `Content == "Apple_Boot"` equality.
All of them left `partbase`/`partsize`/`partro` zero, so the report could not
identify the failing category. The closeout is
`docs/emmc-n10.20-widthhold-observation-2026-11-11.md`.

The exact string equality is a source-backed suspect, not a proven cause: the
linked partition scheme publishes a type-GUID Content string and an attached
storage client can replace Content with its Content Mask, while the prior host
fixtures only used `Apple_Boot`.

## 2. Correction

- Content qualification is now structural. The BOOT media must publish a
  Content property that casts to `OSString`; the string value, GUID format and
  length are not compared. The rationale is recorded in
  `package/Gta4lEmmcBlock.cpp` next to the check. Actual bytes remain protected
  by the same-boot 1-bit reference comparison and the final-image host binding,
  not by a media label.
- A widthhold-only u32 `partdiag` field latches the first failing PART
  category. It is not a reason code and never accumulates:
  bit 0 not-found, bit 1 duplicate, bit 2 size, bit 3 base, bit 4 Writable,
  bit 5 Content. Missing/wrong-typed and wrong-valued properties share their
  category. Zero means no PART failure was latched.
- The field is reset to zero at the start of every widthhold run, survives
  cancellation, recovery, report copy and snapshot, and is zero on success.
  `partbase`, `partsize` and `partro` remain zero unless every check passed.
- The formatter emits `partdiag=` after `partro` in the `WIDTHHOLD` summary.
  The field has a compile-time u32 assertion in both the policy and the
  formatter translation units.
- The design amendment is in
  `docs/emmc-n10.17-sustained-width-read-design-2026-11-11.md` (sections 4.5,
  4.8, 4.9, 4.10, 6.3, 6.6 and 8). Transport, timing, read ranges, request
  counts, recovery rules and reason numbers are unchanged.

## 3. Report bounds

The formatter-exact N10.21 maxima, with every numeric field at its largest u32
value and 16-byte digests filled, are:

| Line | Failure shape | Success shape |
| --- | ---: | ---: |
| `WIDTHHOLD` | 922 bytes | 913 bytes |
| `WIDTHHOLDREAD` | 303 bytes | 303 bytes |
| `WIDTHHOLDFAIL` | 133 bytes | not emitted |

The `WIDTHHOLD` summary cap stays 922 bytes so one constant covers both
shapes. With the established 838-byte base bound and line separators, the
worst report is 2,066 bytes on the read path and 1,896 bytes on the fail path,
inside the 2,560-byte `GTA4L_EMMC_BLOCK_REPORT_BYTES`. These values supersede
the pre-N10.21 905/335/227 and 2,078/1,970 figures, which were not
formatter-exact.

## 4. Host verification

- Block suite: 48 core scenarios and 38 core mutation controls; 351 adapter
  scenarios with 122 adapter, 3 support and 15 policy mutation controls;
  `scripts/test-gta4l-emmc.py` 189/117 and
  `scripts/test-gta4l-emmc-probe.py` 196/100.
- New PART scenarios cover lookup absent/duplicate, post-selection base and
  size refusals reachable through a property that changes after lookup,
  Writable true/absent/wrong-typed, Content absent/wrong-typed, and both a
  friendly name and a synthetic non-identifying type-GUID Content value
  accepted when correctly typed. Every refusal asserts its exact `partdiag`
  bit, zero `partbase`/`partsize`/`partro`, no case read, and verified 1-bit
  recovery; the success shape asserts `partdiag=0`, verified recovery and the
  unchanged switch/read accounting.
- Mutation controls reject a lost `partdiag` latch, a lost per-run reset, a
  removed Content-presence guard, a reintroduced friendly-name equality, a
  removed Writable guard and an omitted `partdiag` report field, in addition
  to the existing widthhold completion, revert, budget, attribution and
  cancellation controls.
- `scripts/check-gta4l-widthhold-evidence.py` now requires `partdiag=0` on the
  success line and rejects nonzero, missing, duplicated, malformed, reordered
  or unknown `partdiag` fields. Its self-test covers each rejection. N10.19
  artifacts and their schema stay historical evidence and are not
  reinterpreted as N10.21 passes.
- The documented evidence counts, diagnostic payload and packager suites, the
  payload builder/checker self-tests and the widthhold evidence-checker
  self-test pass; `git diff --check` is clean.

## 5. Status and gates

This is a **host-qualified correction only**. It does not identify the N10.20
failing check, does not retroactively pass N10.20 and does not qualify the
N10.19 image. The report schema can identify the first failing category on a
future run, but no run exists.

Any hardware continuation requires, in order: a new source-bound candidate
build and storage-ABI audit, a fresh payload/BOOT image with rebound host
digests, a separately approved finite one-boot protocol, and a current
p69/protected-partition state check. None of that is authorized by this batch.

## 6. References

- `docs/emmc-n10.20-widthhold-observation-2026-11-11.md`
- `docs/emmc-n10.17-sustained-width-read-design-2026-11-11.md`
- `docs/emmc-n10.19-widthhold-candidate-2026-11-11.md`
- `docs/emmc-driver.md`
- `package/Gta4lEmmcBlock.h`
- `package/Gta4lEmmcBlock.c`
- `package/Gta4lEmmcBlock.cpp`
- `scripts/test-gta4l-emmc-block.py`
- `scripts/check-gta4l-widthhold-evidence.py`
