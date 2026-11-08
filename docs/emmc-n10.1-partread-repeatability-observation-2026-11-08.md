# gta4l N10.1 unchanged-image captured repeatability observation (2026-11-08)

Status: completed captured repeatability series on the unchanged N10.1
read-only content-qualification image, 2026-11-08. The owner approved the
protocol's section 10 scope with a finite budget of three additional
owner-controlled cold boots. Three new fully cold boots (observations 2-4)
were supplied as screen transcriptions and all three were class D: the six
pinned ranges matched, with the expected deterministic PARTREAD/READTEST
signature and no failure, cancellation, budget, media or partition flag.
Together with the earlier observation 1, four captured cold-boot observations
on the same image show the same six-of-six result. This is descriptive
repeatability only; it is not an initialization-reliability qualification, a
failure-rate estimate or a new candidate. N8.5/N9.6/N9.7/N9.8 `complete=0`,
the N9.10 comparator result and N10.1 observation 1 are preserved, and
initialization reliability remains `[U]`.

## 1. Approval and attempt accounting

The owner approved
`docs/emmc-n10.1-partread-repeatability-protocol-2026-11-08.md` section 10
with three additional owner-controlled cold boots. No warm boots were scoped
or performed. Each attempt counted; there was no retry, replacement or budget
extension.

| Observation | Boot | Outcome | Evidence |
| --- | --- | --- | --- |
| 1 | cold (earlier N10.1 result) | class D, six of six | captured transcript, interpretation and record |
| 2 | fully cold, about 10-20 s off | class D, six of six | captured transcript, interpretation and record |
| 3 | fully cold, about 10-20 s off | class D, six of six | captured transcript, interpretation and record |
| 4 | fully cold, about 10-20 s off | class D, six of six | captured transcript, interpretation and record |

The owner reports the new boots in order, fully cold each time, and a return
to TWRP after each. The three-boot budget is consumed and no further boot is
authorized.

## 2. Fixed image identity

No reflash, rebuild or rollback occurred. The series ran on the already
flashed N10.1 image: BOOT `0cee22c9…` (85,688,320 bytes), payload
`e2dc899d…`, prelinked kernel `12e00d9f…`, packaged kernel `05fde642…`,
archive `5a59d055…`, and post-flash p69 `cc3c8492…` (prefix `0cee22c9…` plus
unchanged tail `a28b4ea2…`). The full identity table is in the protocol's
section 2. Association rests on the preceding flash record and the read-only
pre-series check, not on an independently measured running-image hash; no
boot timestamp or rail-reset state is claimed.

## 3. Results

All four captured observations returned the same qualification fields:

```text
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 PARTREAD: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6 blocks=138 bytes=70656 fails=0 cancel=0 budget=0 media=0 part=0 cap=61071360
GTA4L-EMMC P5 READTEST: index=5 target=1 blocks=1 off=100662784 ok=1 status=0 actual=512 digest=400039fd62daf3499b377ced75428918
GTA4L-EMMC P5 READFAIL: index=0 reason=0 status=0 actual=0 digest=00000000000000000000000000000000
GTA4L-EMMC P5 SNAP: locked=1
GTA4L-EMMC P5 FAIL: valid=0 stage=0 code=0 block=0 error=0
```

`passed=6 failed=0 reason=0 complete=1` means all six pinned digests matched
under the guard at `package/Gta4lEmmcBlock.cpp:2297-2299`;
`reads=6 blocks=138 bytes=70656` is exactly 34+34+64+3+2+1 blocks and
138 x 512 bytes. `READTEST index=5 … digest=400039fd…` is the final pinned
boot-tail case, not an aggregate. `media=0 part=0` are clear failure flags.
The zero `READFAIL` and `PART`/`READ`/`QUAL`/`QUAL3` lines are inactive legacy
paths; `SNAP locked=1` and `FAIL valid=0` are unchanged. The supplied panic
suffix `@Gta41EmmcBlock.cpp:2642` preserves the supplied spelling and is the
intentional terminal report stop (source line 2642).

Incidental variation is limited to the whole-boot `DRV reads` aggregate
(31 in observation 1, 30 in observations 2-4) and the panic `pages`/`free`
counters. No qualification field changed.

## 4. Pre-series and post-series integrity

The approved read-only checks bracketed the series. The pre-series check
found p69 full `cc3c8492…`, RECOVERY `14e11370…`, EFS `51fa824c…` and
sec_efs `3a265d58…` all matching the recorded identities; the post-series
check found the same values plus p69 prefix `0cee22c9…` and tail `a28b4ea2…`
still matching. The tablet is in TWRP with battery 100. These reads wrote
nothing; no per-attempt p69 read was performed.

## 5. Capture limits and deviations

- The three new reports are owner-supplied screen transcriptions, not
  captured kernel logs. There are no photographs, boot timestamps,
  independently measured running-image hashes or rail-reset measurements.
- The cold classification and the roughly 10-20 second off interval are owner
  reports, not independent measurements. The owner confirms the reports are
  in boot order and the tablet returned to TWRP each time.
- No stop-rule event, failure, cancellation or budget deviation occurred.
- The screen's line wrapping is preserved in the raw transcripts; terminal
  wrapping is not treated as protocol line breaks.

## 6. What the series establishes and does not establish

Four captured cold-boot observations on the unchanged image repeated the six
pinned content matches with the expected deterministic signature. That
supports descriptive repeatability of the six pinned read-only ranges on this
image across the captured boots.

It does not establish initialization reliability, a failure rate,
statistical bound or mean-time-to-failure; it does not qualify arbitrary I/O,
filesystem mount, root integration, writes, wider/faster bus modes,
DMA/CQE/ICE or the real context/deblock/breakup layers. It does not verify
fields on uncaptured attempts, and a missing capture is not a pass. It does
not upgrade N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10 comparator result or
N10.1 observation 1. Initialization reliability remains `[U]`.

## 7. Evidence

Raw evidence is in the gitignored
`out/gta4l/ram-root-emmc-n10.1-hardware-evidence/` tree:
`series-precheck-2026-11-08.txt` (`b18eba3e…`), `postseries-integrity-2026-11-08.txt`
(`61f68510…`), `observation-2/` through `observation-4/` (each with
`owner-transcript-raw.txt`, `interpretation.md`, `observation-record.json`
and a non-self-hashing `observation-sha256.txt`) and
`repeatability-series/` (summary, record and non-self-hashing
`series-sha256.txt` `38736d52…`). The observation manifests bind the
unchanged flash manifest `d2b23ecf…` and the preceding observation manifest,
forming a hash chain; no manifest hashes itself. The repository records only
this sanitized summary.

## 8. Next gates

No further boot is needed or authorized. Committing or pushing this closeout
needs separate owner approval. Any stronger read-only I/O or reliability
proposal returns to Plan; mounts, writes, bus-width/speed changes and
DMA/CQE/ICE remain separate milestones.
