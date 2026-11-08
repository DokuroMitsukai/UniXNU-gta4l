# gta4l N9.11 unchanged-N9.10 owner-reported repeatability observation (2026-11-08)

Status: owner-reported repeatability series on the unchanged N9.10
page-aligned exact-comparator image, 2026-11-08. After the captured
observation-1 cold boot, the owner states that they ran three additional cold
boots and three warm boots, that every one showed `complete=1`, that no photos
or full reports were captured, that the image was not changed (no intervening
flash), and that the tablet is back in TWRP. This record separates the one
fully captured report from the six owner-reported outcomes. It is not an
initialization-reliability qualification, a failure-rate estimate or a new
candidate. No device access, flash, boot, pre/post p69 read, commit or push was
performed while recording it. N9.6/N9.7/N9.8 `complete=0` remain historical
facts and initialization reliability remains `[U]`.

## 1. Attempt accounting

| Attempt group | Type | Outcome | Evidence |
| --- | --- | --- | --- |
| observation 1 | cold | `complete=1` with the full P5 report | captured raw transcript, interpretation and record |
| 3 additional boots | cold | owner-reported `complete=1` | owner statement only |
| 3 additional boots | warm | owner-reported `complete=1` | owner statement only |

Totals: 4 cold boots (1 captured + 3 reported) and 3 warm boots (all
reported), seven boots since the N9.10 flash. The three warm boots are outside
the written N9.11 protocol, which scoped cold boots only; they are recorded
separately and are not part of the cold-boot repeatability sample.

The exact owner messages are preserved verbatim in the private evidence tree:
no pictures were taken, the six additional boots were reported to show
`complete=1`, the tablet was returned to TWRP, and the image was not changed.

## 2. What is verified and what is not

Verified: the observation-1 cold boot and its full P5 report (`gpt=1 reason=0
slots=84 used=80 media=80 match=1 complete=1`, `reads=25 blocks=45 fails=0`),
bound to the flash record and the post-flash p69 `004b9d6d…`.

Owner-reported only: the three additional cold boots and three warm boots.
Their report fields, screen text, timestamps and boot conditions were not
captured. No photograph exists, no running-image hash was measured during the
series, and no pre-series or post-series read-only p69 or protected-partition
verification was performed. The image association for the series rests on the
flash record, observation 1 and the owner's statement that the image was
unchanged.

## 3. Deviations from the N9.11 protocol

The written protocol (`docs/emmc-n9.11-reliability-protocol-2026-11-08.md`)
proposed per-attempt raw capture, hash-chained observation directories, and
read-only pre-series and post-series p69/protected-partition verification. The
owner-executed series did not provide those captures or checks, and it added
three warm boots outside the cold-boot scope. The outcome classes A-E
therefore cannot be independently applied to the six reported attempts beyond
the owner's `complete=1` statement. The series is recorded as owner-reported
evidence, not as a completed protocol execution.

## 4. Bounded interpretation

The owner-reported outcome is consistent with the captured observation-1
same-boot completeness result repeating on the same image: 4 cold boots and 3
warm boots were reported to show `complete=1`. This supports descriptive
repeatability of the owner-observed outcome on this image. It does not verify
the detailed report fields for the six uncaptured boots, and a missing capture
is not a pass.

Because the series has no captures and no failure was reported, it is a
success-only owner report. It cannot estimate a failure rate, bound
initialization reliability, or distinguish an initialization success from a
comparator success on the uncaptured attempts.

## 5. What this does not establish

- No initialization-reliability qualification, failure probability,
  mean-time-to-failure or statistical bound.
- No verification of the other P5 fields, the deterministic final comparator
  read, or the absence of a transient error on the six uncaptured boots.
- No mount, arbitrary I/O, write, wider/faster-mode, DMA/CQE/ICE or
  later-boot result.
- No upgrade of N9.6/N9.7/N9.8 `complete=0`; initialization reliability
  remains `[U]`.
- No measured running-image hash, boot timestamp or rail-reset claim.

## 6. Evidence and next gates

Raw owner messages, the bounded interpretation and the structured series
record are in the gitignored
`out/gta4l/ram-root-emmc-n9.10-hardware-evidence/reliability-series/` directory
(raw `141021e2…`, interpretation `6552f2a8…`, record `e0fbe0c8…`; the separate
non-self-hashing manifest `76c45042…` binds the unchanged flash manifest
`89f5cc24…` and the observation-1 manifest `2056f59e…`). The repository
records only this sanitized summary.

No further boot is needed for this series. If a stronger repeatability or
reliability claim is wanted, the next step is a separately owner-approved,
read-only p69/protected-partition re-verification (no boot) and, only if the
owner wants it, a newly scoped captured-boot protocol; the current result does
not require either. Mount, writes and wider/faster modes remain separate,
unapproved rungs.
