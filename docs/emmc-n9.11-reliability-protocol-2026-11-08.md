# gta4l N9.11 unchanged-N9.10 cold-boot reliability protocol design (host-only, 2026-11-08)

**Status:** host-only protocol proposal, 2026-11-08. This batch performed no
device access, boot, flash, candidate build, driver change, QEMU run, commit
or push. It follows the N9.10 observation
(`docs/emmc-n9.10-partcmp-observation-2026-11-08.md`) and the N6 reliability
control (`docs/emmc-n6-reliability-observation-2026-11-07.md`). It does not
authorize execution. Any further boot or device-access batch requires fresh
explicit owner approval of the exact scope in section 10 and an
owner-selected finite boot budget. N9.6/N9.7/N9.8 `complete=0` are preserved
and initialization reliability remains `[U]`.

## 1. Bounded question

N9.10 returned one same-boot exact GPT-to-`IOMedia` `complete=1`. The next
question is descriptive, not a new comparator or transport correction: on the
**same unchanged already-flashed image**, do further owner-controlled cold
boots repeat the bounded completeness outcome, and what is observable when
initialization or the comparator fails before a complete report?

This protocol does not estimate a failure rate, invent a statistical
threshold, qualify initialization reliability, or move to mount, writes or
wider/faster modes. It defines a finite observation series and the evidence
that each attempt must produce. A missing or partial capture is never a pass.

## 2. Fixed image identity and preconditions

No reflash, rebuild or rollback is part of this protocol. The image is the
N9.10 candidate already on BOOT/p69:

| Item | SHA-256 / identity |
| --- | --- |
| BOOT image | `dc79fab2b334608859f75ccd00b56d771eb1d1cb4af5b69084fe1f1d4642183f` (85,688,320 bytes) |
| Payload | `800d293dc62676153b35acbe790cb5384ba594ee9be7a46506ead064411dd7f4` (83,886,080 bytes) |
| Prelinked kernel | `fcc61714471e30ea102f3c579be90bc8ec6750f34f726fd8310d645cb6efbd57` |
| Packaged kernel | `149b1e6cdbefb6e7e04428bd5b18220a26c149252597b2b382e1177fef986ef5` |
| Requalified archive | `0575c413e799d6ed47985748937f304824b255945dc9d1d0352b3da361ff6bbc` |
| Candidate source | `78144956a17042db9b9a4fb8ccf6ea5d11469b5d198c83418184f75fa68abf47` |
| Policy source | `f84d1bd25dea5b0457450ecdb3a9a2a6d9a71d46b59a2c9b4a3890ff9894598c` |
| Policy header | `6c51b5b310d0725ea69d3a6d0df8161c520a8455012840052f96c60cadcd7f24` |
| Post-flash p69 | `004b9d6d60d5ceedd7c0bb9f768fc2b120f0918ccb66d654c893073ad2a4fe31` (prefix `dc79fab2…` + unchanged tail `a28b4ea2…`) |
| RECOVERY | `14e11370…` (unchanged at flash time) |
| EFS / sec_efs | `51fa824c…` / `3a265d58…` (unchanged at flash time) |
| Retained rollback | `boot-before-n9.10.img.xz` `3c7e9e5f…` -> raw `cf13acfc…`; restoration is not authorized |

Association with the running image uses the preceding N9.10 flash record and a
pre-series read-only p69 verification. It is not an independently measured
running-image hash, and no boot timestamp, rail-reset state or post-boot
device readback is invented.

Before the first authorized attempt, and again after the last attempt or a
stop, verify read-only through the owner-approved access path:

- the full p69 SHA-256 is still `004b9d6d…`;
- RECOVERY, EFS and sec_efs still match their recorded hashes.

Any mismatch stops the series immediately; do not reflash, restore, repair or
continue. These reads are device access and must be included in the approved
scope; they write nothing.

## 3. Boot budget and attempt accounting

- Proposed default: **three additional owner-controlled cold boots**
  (observations 2-4; four total including N9.10 observation 1). The owner
  selects the actual finite number at approval; this document does not fix it.
- Every authorized attempt counts, including initialization failure, a
  no-report boot, a capture failure and any inconclusive outcome. There is no
  replacement boot and no retry within an attempt.
- A failed or inconclusive attempt consumes its budget slot. Continuing after
  any stop-rule trigger requires a new explicit owner decision, not an
  automatic extension.
- The series never mixes candidates: only the fixed N9.10 image in section 2.
  N9.4-N9.8 observations are historical and are not part of this denominator.

## 4. Cold-start procedure

- Each attempt is an owner-initiated **full power-off then power-on**, not a
  warm reboot. The host never issues a reboot or power command.
- The owner documents the off interval and confirms the boot was cold. This is
  an owner classification, not an independent rail measurement.
- Exactly one boot per authorized attempt; no intervening boots.
- The owner confirms adequate charge before the series; no battery measurement
  or threshold claim is made.
- After the boot, the owner captures the screen (section 5) and returns the
  tablet to TWRP. If the expected screen does not appear, or the tablet does
  not return to TWRP, stop the series and report.
- No reflash, rollback, reset/repair, partition write or host-issued reboot
  occurs between attempts.

## 5. Per-attempt capture requirements

For every attempt, preserve the complete screen output the owner supplies:
the full `GTA4L-EMMC P5 REPORT` block, including the header, `DRV`, `XFER`,
`MEDIA`, `PARTMAP`, `READDETAIL`, `PART`, `READ`, `QUAL`, `QUAL3`, `SNAP` and
`FAIL` lines, and the rendered panic suffix. Preserve the owner's wrapping and
spelling; terminal wrapping is not protocol line breaks. If the screen shows a
non-P5 panic or no text, preserve that too.

The owner also reports: cold/warm classification, off interval, return to
TWRP, any deviation from the procedure, and any subjective delay (not a
measured duration). No boot timestamp is invented; if useful, record the
receipt time separately from any inferred boot time.

Raw evidence stays under the gitignored
`out/gta4l/ram-root-emmc-n9.10-hardware-evidence/` tree as
`observation-2/` through `observation-N+1/`, each following the existing
`observation-1/` shape: `owner-transcript-raw.txt`, `interpretation.md`,
`observation-record.json` and a separate non-self-hashing
`observation-sha256.txt`. Each observation manifest binds the unchanged flash
`evidence-sha256.txt` and the preceding observation manifest, forming a hash
chain. A series-level summary and non-self-hashing series manifest are added
at closeout. The tracked record contains only the sanitized summary.

## 6. Outcome classes and the expected repeat

Classify each attempt before starting the next one:

| Class | Observable signature | Meaning | Action |
| --- | --- | --- | --- |
| A | No P5 report; generic panic, hang or blank screen | Pre-report failure or capture loss; not a pass | Stop |
| B | P5 report with `FAIL valid=1` (`stop=11` or `stop=16`; stage/code set; `online=0` likely) | Initialization, baseline, thread or watchdog failure with recorded fields | Stop |
| C | P5 report with `PARTMAP complete=0` (`gpt=0`, `reason=8/10/12/13`, `fails`/`budget`/`cancel` set, or `match=0`) | Comparator failure or incomplete observation | Stop |
| D | P5 report with the expected deterministic `PARTMAP complete=1` signature below | Same-boot completeness repeated | Continue within budget |
| E | P5 report present but truncated, internally inconsistent or unreadable | Inconclusive evidence | Stop |

Source-grounded report paths behind classes A/B/C:

- `initializeHardware()` failure sets `rc`, `stop=11`, `fail_valid=1`,
  `fail_stage=host.last_step` and `fail_code`, and `start()` calls
  `panicWithReport()` (`package/Gta4lEmmcBlock.cpp:480-529,679-681`).
- Baseline LBA 1 failure sets the same `stop=11`/FAIL fields and panics
  (`:683-699`). Report or watchdog thread-start failure sets `stop=16`,
  `fail_valid=1`, stage 1/17, code 16 (`:724-750`).
- Lock-allocation and property failures return false with only an
  `IOLog("GTA4L-EMMC P5 FAILED: …")` line (`:665-678,702-717`); with no panic
  screen that is class A, not a pass.
- The 30,000 ms watchdog emits its own report with `rc=16 stop=16
  fail_valid=1 fail_stage=16 fail_code=16` (`:53,2555-2585`).
- The comparator failure paths set `partmap_complete=0` and panic through
  `partmapReportFailure()`/`panicWithReport()` (`:1494-1510,2061-2079`);
  `complete=1` is only the guard at `:2034-2039`.

Expected class-D signature (from N9.10 observation 1):

```text
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 PARTMAP: gpt=1 reason=0 slots=84 used=80 pmbr=1 hdr=1 arr=1 bak=1 xcheck=1 media=80 match=1 missing=0 extra=0 dup=0 nested=0 prop=0 stable=1 stable_ms=250 deadline_ms=2000 timed_out=0 budget=0 reads=25 blocks=45 fails=0 cancel=0 complete=1
GTA4L-EMMC P5 READDETAIL: state=4 lba=61071347 blocks=1 bytes=512 status=0 actual=512 desc_len=512 desc_dir=1
GTA4L-EMMC P5 SNAP: locked=1
GTA4L-EMMC P5 FAIL: valid=0 stage=0 code=0 block=0 error=0
```

Allowed variation within class D: `samples`, `changes`, `stable_ms`, the
incidental `DRV`/`XFER` aggregates and timing. The `PART`, `READ`, `QUAL` and
`QUAL3` lines should remain zero/inactive in comparator mode. Not allowed
within class D: any change to `gpt`, `used`, `media`, `match`, `fails`,
`complete`, the five GPT flags or the deterministic final comparator read.
A different `READDETAIL` is an anomaly to classify, not a silent pass.
`rc=0 stop=0` in the header is not comparator success by itself.

## 7. Stop rules

Stop the series and return the evidence for analysis at the first:

- class A, B, C or E outcome;
- p69 or protected-partition identity mismatch before or after an attempt;
- failure to return to TWRP or to capture the expected screen;
- unexpected panic text, hang or state change;
- any unauthorized write, reflash, rollback, reset/repair or host reboot.

Because the series stops at the first unexpected result, it is a bounded
success series unless a failure occurs; it is not a success-only batch run
past a failure and it cannot estimate a failure rate.

## 8. What the series can and cannot establish

It can establish, descriptively and only within the captured attempts:

- whether the exact same-boot completeness signature repeats on this image;
- whether an initialization or comparator failure occurs and what the report
  path actually displays when it does;
- that the per-attempt evidence is capturable and hash-chained.

It cannot establish initialization reliability, a failure probability, a
mean-time-to-failure, mountability, arbitrary I/O, writes, wider/faster modes,
DMA/CQE/ICE, or later-boot state. It does not upgrade N9.6/N9.7/N9.8
`complete=0`, and initialization reliability remains `[U]` unless a later
separately approved qualification design defines and meets acceptance.

## 9. No new instrumentation in this protocol

The flashed image carries the existing P5 report paths only. If a pre-report
failure occurs, the series can preserve what the screen shows but cannot
resolve the internal cause beyond that capture; the P5 FAIL stage/code is
available only when the driver reaches a report path. Adding pre-comparator
instrumentation would be a separate candidate, build, flash and boot gate, and
is not proposed or authorized here. A missing report is not evidence that
initialization succeeded or that the comparator failed.

## 10. Exact approval scope and gates

Execution requires the owner to approve all of the following explicitly:

1. read-only pre-series and post-series p69/protected-partition verification
   through the existing owner-approved access path;
2. a finite owner-selected number of owner-controlled cold boots of the
   unchanged N9.10 image (three additional boots proposed by default);
3. per-attempt capture and the private evidence layout in section 5;
4. no reflash, rebuild, rollback, reset/repair, partition write, host-issued
   reboot, retry, extra boot, commit or push.

If the owner does not approve, hardware stays paused. If approved, Build
executes only within that scope and stops at the first stop-rule trigger. A
new candidate, instrumentation or a different image requires its own gates.

## 11. Evidence and privacy

Raw transcripts, photographs, records and manifests stay in the gitignored
`out/` tree and are referenced by relative path and hash only. The tracked
summary contains no serial number, unique identifier, account name, hostname,
SSH target, e-mail address, IP address, VPN name or absolute home path. No
manifest hashes itself; each binds the immutable flash manifest and the
preceding observation manifest.

## Postscript — owner-executed series (2026-11-08)

After this proposal was written, the owner ran three additional cold boots and
three warm boots on the unchanged image without per-attempt captures and
without the read-only pre/post checks above; every one was reported to show
`complete=1`. That execution is recorded in
`docs/emmc-n9.11-reliability-observation-2026-11-08.md`. It deviates from this
protocol's capture and verification requirements, and the three warm boots are
outside its cold-boot scope. It supports descriptive repeatability only;
initialization reliability remains `[U]`, and no further boot is needed.
