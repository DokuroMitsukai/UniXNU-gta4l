# gta4l N6 reliability observation: second clean CMD3 control on one cold boot

**Status:** focused finding, 2026-11-07. Host-only closeout of one
owner-supplied hardware report after the N6 BOOT-only flash; this batch
performed no device access, boot, flash, candidate build, driver or transport
change, commit or push. It follows the N5 closeout in
`docs/emmc-n5-integration-observation-2026-11-07.md` and the D12.6
panic-format observation in
`docs/emmc-d12.6-panic-format-observation-2026-11-06.md`; it does not rewrite
either record or any earlier verdict.

## 1. Observation and provenance

The owner supplied one `GTA4L-EMMC H9T REPORT` after the N6 flash and
confirmed:

- the boot was a **cold** start (power off then on); no stronger physical
  rail-reset claim is made;
- no intervening attempts were reported between the flash and this report;
- the tablet was returned to **TWRP**.

No exact boot timestamp was recorded and none is invented. No original
photograph bytes were supplied in this turn, so no photo hash is claimed. The
report is ordinal 1 of the supplied observation and the N6 one-boot budget is
consumed by this closeout.

The candidate is the unchanged D12.6 stage-9 evidence set, reused rather than
rebuilt:

- BOOT image `boot-ram-root-emmc-d12.6-probe9.img`, 85,688,320 bytes, SHA-256
  `7c76c7ebd3a14c343bbf0556f4dd1aa943ee2ee10f80a274a3962d54d2b2d44e`,
  18/18 boot gate; staged xz `20ccec34…`.
- Payload `e384cc06…`, 83,886,080 bytes; prelinked kernel `b1871d40…`,
  packaged kernel `12385bc7…`.
- Diagnostic `gta4l-emmc-probe=9` with kernel arguments `rd=md0
  sheng-pmgr=2`; identification only, one CMD3 attempt, no retry, then the
  intentional report stop.
- Flashed to BOOT/p69 only. The current p69 (the N5 image `22bea393…`) was
  saved as `boot-before-n6.img.xz` (`5eaaa423…`, decompressing to
  `22bea393…`). The post-flash full p69 is `b19c59b8…`, the image-length
  prefix matches `7c76c7eb…`, the unchanged tail is `a28b4ea2…`, and
  RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` match the
  pre-flash values. Battery was 100%; the host issued no reboot and performed
  no write outside p69.

The report does not embed the BOOT hash. Association with this candidate uses
the N6 flash record and the conversational sequence; it is not an independent
cryptographic measurement of the running image.

## 2. Report decode

The header is:

```
GTA4L-EMMC H9T REPORT ver=1 rc=0 stop=0 sub=1 att=1 tr=0 r1=0 retry=0/0 step=13 log=4 hz=19200000
```

CMD3 was submitted and attempted once (`sub=1`, `att=1`) with retry disabled
and not engaged (`retry=0/0`). Transport and the masked R1 error flag are
zero. The poll history is valid: 25 iterations, zero-based terminal iteration
24, first NORMAL `0000`, last NORMAL `0001`. The reserved terminal slot 19 is
kind `C` (SUCCESS), NORMAL `0001` (command complete), ERROR `0000`,
RESPONSE0 `00000500`, valid 1 and ticks `0x14f2` (5,362 ticks, about 279.27 us
at 19.2 MHz). No timeout capture exists (`C3T v=0`).

The supplied C3L field is:

```
GTA4L-EMMC H9T C3L v=1/3/0000/0001/25/0000000000014e4
```

The source prints the poll ticks as two eight-digit halves. The typed report
shows 15 hex digits; the prior D12.6 report of the same candidate shows
`00000000000014e4`, so the missing leading zero is read as a transcription
omission, not a different value. Normalized, the poll ticks are `0x14e4`
(5,348 ticks, about 278.54 us at 19.2 MHz). The raw text is preserved
unchanged in the private evidence tree.

`TRC n=6 ov=0` reports six scheduled entries at iterations 0, 1, 2, 4, 8 and
16. All six sampled the same private RESPONSE0 word with valid 1. Slots 6-18
are unused (`?`, valid 0); slot 19 is the reserved terminal entry. The `FAIL`
line is `fail=0/0/0/00000000/0000/0000/00000000/0`: the panic at the rendered
suffix `@Gta41EmmcProbe.c:1579` is the intentional report stop, not a recorded
hardware failure. The rendered suffix uses the digit `1` in place of the
source letter `l`; the source file is `package/Gta4lEmmcProbe.c`.

The pre-command response words and the scheduled RESPONSE0 words can contain
CID material. They are omitted from this public record; the full supplied
text remains in the private transcript.

## 3. Comparison with D12.6 and classification

The prior D12.6 cold boot of the same unchanged candidate reported the same
C3L value `00000000000014e4` and the same terminal ticks
`00000000000014f2`. The six scheduled samples here are each one tick higher
than that report (`0x31/0x119/0x201/0x3c2/0x738/0xe14` versus
`0x30/0x118/0x200/0x3c1/0x737/0xe13`). This supports the 0x14e4 normalization
and shows deterministic-looking timing within a tick; it is not a rate, a
latency bound or a card-response claim.

The kind column renders `S`, `?` and `C`, matching the guarded formatter and
the D12.6 result.

**Classification: clean control / non-reproduction.** CMD3 completed without
retry and without a recorded timeout on this cold boot. Together with D12.6,
this is a second clean cold-boot control of the same candidate, separated by
other ladder work and a reflash. It is not a continuous two-boot trial, a
controlled failure-rate estimate, a fix, or proof of reliable initialization.

## 4. Interpretation limits

- The six scheduled entries sample the retained long-response window during
  polling; they are not CMD3 responses.
- The terminal sample is a later sequential register read. It bounds a change
  to an interval but does not prove a fresh card response, an exact arrival
  time or a card-state transition.
- The poll and terminal timings are distinct sequential measurements; neither
  is a card-response latency.
- The transcript was typed by the owner; line breaks inside the report are
  terminal wrapping, not protocol line breaks. The C3L field is preserved as
  supplied, with the normalization documented above.
- One boot cannot qualify repeatability, initialization reliability or a
  failure rate. Initialization reliability remains unresolved.
- No driver, transport, timing or candidate change is made or implied. F2,
  p5b/F5 and N5 verdicts are unchanged.

## 5. Evidence and validation

Raw evidence is in the ignored
`out/gta4l/ram-root-emmc-n6-reliability-evidence/` tree: the N6 flash record
and command log, pre/post hashes, backup and staged xz files, and
`observation-1/` with `owner-transcript-raw.txt` (SHA-256 `b7702fce…`),
`observation-record.json` and `interpretation.md`. `observation-sha256.txt`
covers the observation files, this document and the validation log and does
not hash itself; `supplemental-manifest.txt` covers the flash-evidence files.
The flash hashes and claims are unchanged, and the flash record's path fields
are repo-root-relative.

Host checks run for this closeout are recorded in
`observation-validation.txt`; they cover TOML parsing, the porting record,
board checks, evidence counts, the RAM-root fixture recipe, whitespace, the
baseline hash table and a privacy scan. The earlier 24-script battery results
remain historical and are not re-claimed as a current full-suite acceptance.

## 6. Disposition and next step

N6 is accepted only as one additional clean control for the unchanged D12.6
stage-9 candidate on one owner-classified cold boot. The N6 boot budget is
consumed; no further flash, boot or retry is authorized. Initialization
reliability remains a separate open issue.

The recommended next rung is host-only design work for a bounded
partition-publication observation, because the N5 reporter stops at the first
nonzero partition count and does not observe a stability window or a
completion barrier. That design, any implementation, candidate production and
any later hardware observation each need separate approval.
