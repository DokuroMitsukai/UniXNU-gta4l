# gta4l N10.1 read-only content-qualification captured repeatability protocol design (host-only, 2026-11-08)

**Status:** host-only protocol proposal, 2026-11-08. This batch performed no
device access, boot, flash, candidate build, driver change, QEMU run, commit
or push. It follows the N10.1 candidate and hardware-result records and the
N9.11 reliability protocol and observation. It does not authorize execution.
Any further boot or device-access batch requires fresh explicit owner approval
of the exact scope in section 10 and an owner-selected finite boot budget.
N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10 comparator `complete=1`, the
owner-reported N9.11 series and N10.1 observation 1 are preserved.
Initialization reliability remains `[U]`.

## 1. Bounded question

N10.1 returned one same-boot six-of-six pinned content match. The next
question is descriptive, not a new comparator, transport correction or
candidate change: on the **same unchanged already-flashed N10.1 image**, do
further owner-controlled cold boots repeat the six pinned read matches, and
what is observable when initialization, media/partition resolution, a read,
digest comparison, the read budget or the capture fails before a complete
report?

This protocol does not estimate a failure rate, invent a statistical
threshold, qualify initialization reliability, or move to mount, writes or
wider/faster modes. It defines a finite observation series and the evidence
that each attempt must produce. A missing or partial capture is never a pass.

## 2. Fixed image identity and preconditions

No reflash, rebuild or rollback is part of this protocol. The image is the
N10.1 candidate already on BOOT/p69:

| Item | SHA-256 / identity |
| --- | --- |
| BOOT image | `0cee22c92fa7434de7d115d4141dc0c6519988d91e779c86cedf2a8a25e71bf9` (85,688,320 bytes) |
| Payload | `e2dc899d96fed2801dd5cc0888db950d8ee5ceb16f84781fa19b22459007c1a8` (83,886,080 bytes) |
| Prelinked kernel | `12e00d9fb4f1fbb35a0246b980f657bc9bf68a29fc7d68a9359695af8c836499` |
| Packaged kernel | `05fde6426d45241b2f40c5bf32c92ce7a741b2cce4dc8fd13912c1bef4fba75b` |
| Requalified archive | `5a59d05560e0b28b9f108a2b8ef1b7e6a2a1074db843725dbccfc68f9393b87b` |
| Candidate source | `6a11aa766ff8c6444997ef696e9cfd4da57dd601c53f5a33c93b2e26c1af6c4b` |
| Policy source | `9dad3d79349907042647f91c587023b1fe3edd8fdf8ba608a903d704dbdb4735` |
| Policy header | `d05b982e8062268f85b626414c10a906d2983d5a602b6ecc984e7c7b754d4c9a` |
| Post-flash p69 | `cc3c8492cfeecbab7e5b909e9299774228d8b9a7eb16a1119a5b4501e9d8d974` (prefix `0cee22c9…` + unchanged tail `a28b4ea2…`) |
| RECOVERY | `14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae` (unchanged at flash time) |
| EFS / sec_efs | `51fa824c4bea060f6bf42ee9c1a8c95657a35e3f872a8799f5762963a12f37bd` / `3a265d587700b3e4899f7d13617c3cd3ae9aa3c9972d8d47f04f9b13b9c05970` (unchanged at flash time) |
| Retained rollback | `boot-before-n10.1.img.xz` `de7d4d51…` -> raw `004b9d6d…`; restoration is not authorized |

Association with the running image uses the preceding N10.1 flash record and a
pre-series read-only p69 verification. It is not an independently measured
running-image hash, and no boot timestamp, rail-reset state or post-boot
device readback is invented.

Before the first authorized attempt, and again after the last attempt or a
stop, verify read-only through the owner-approved access path:

- the full p69 SHA-256 is still `cc3c8492…`;
- RECOVERY, EFS and sec_efs still match their recorded hashes.

Any mismatch stops the series immediately; do not reflash, restore, repair or
continue. These reads are device access and must be included in the approved
scope; they write nothing.

## 3. Boot budget and attempt accounting

- Proposed default: **three additional owner-controlled cold boots**
  (observations 2-4; four total including N10.1 observation 1). The owner
  selects the actual finite number at approval; this document does not fix it.
- Every authorized attempt counts, including initialization failure, a
  no-report boot, a capture failure and any inconclusive outcome. There is no
  replacement boot and no retry within an attempt.
- A failed or inconclusive attempt consumes its budget slot. Continuing after
  any stop-rule trigger requires a new explicit owner decision, not an
  automatic extension.
- The series never mixes candidates: only the fixed N10.1 image in section 2.
  N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10 comparator, the N9.11
  owner-reported series and N10.1 observation 1 are historical and are not
  part of this denominator.
- Warm boots are outside this protocol. Do not add or exclude them silently;
  if the owner wants a warm-boot series, scope it explicitly in the approval.

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
`MEDIA`, `PARTREAD`, `READTEST`, `READFAIL`, `PART`, `READ`, `QUAL`, `QUAL3`,
`SNAP` and `FAIL` lines, and the rendered panic suffix. `READFAIL` is emitted
unconditionally inside the partread block and is zero-filled when no case
failed (`package/Gta4lEmmcBlock.c:1266-1288`); an omitted line is a capture
gap, not a pass or a failure by itself. Preserve the owner's wrapping and
spelling; terminal wrapping is not protocol line breaks. If the screen shows
a non-P5 panic or no text, preserve that too.

The owner also reports: cold/warm classification, off interval, return to
TWRP, any deviation from the procedure, and any subjective delay (not a
measured duration). No boot timestamp is invented; if useful, record the
receipt time separately from any inferred boot time.

Raw evidence stays under the gitignored
`out/gta4l/ram-root-emmc-n10.1-hardware-evidence/` tree as `observation-2/`
through `observation-N+1/`, each following the existing `observation-1/`
shape: `owner-transcript-raw.txt`, `interpretation.md`,
`observation-record.json` and a separate non-self-hashing
`observation-sha256.txt`. Each observation manifest binds the unchanged flash
`evidence-sha256.txt` (`d2b23ecf…`) and the preceding observation manifest,
forming a hash chain. A series-level summary and non-self-hashing series
manifest are added at closeout. The tracked record contains only the sanitized
summary.

## 6. Outcome classes and the expected repeat

Classify each attempt before starting the next one:

| Class | Observable signature | Meaning | Action |
| --- | --- | --- | --- |
| A | No P5 report; generic panic, hang or blank screen | Pre-report failure or capture loss; not a pass | Stop |
| B | P5 report with `FAIL valid=1` (`stop=11` or `stop=16`; stage/code set; `online=0` likely) | Initialization, baseline, thread or watchdog failure with recorded fields | Stop |
| C | P5 report with `PARTREAD complete=0` (`cases=0`, `failed>0`, `reason!=0`, `passed!=cases`, or `fails`/`budget`/`cancel`/`media`/`part` set) | Read, short-transfer, digest, budget, cancel, media/partition/open or argument failure, or incomplete observation | Stop |
| D | P5 report with the expected deterministic `PARTREAD complete=1` signature below | Six pinned content matches repeated | Continue within budget |
| E | P5 report present but truncated, internally inconsistent or unreadable | Inconclusive evidence | Stop |

Source-grounded report paths behind classes A/B/C:

- A malformed, duplicate, orphan or combined token is refused before the
  partread path; the report then has no `PARTREAD` line. The boot arguments are
  fixed for this series and must not be changed
  (`package/Gta4lEmmcBlock.c:117-133`,
  `package/Gta4lEmmcBlock.cpp:575-619,673-694`).
- Whole-media wait/profile/open failure sets reason 1/3, `media=1` for reason
  1, `complete=0` and panics (`package/Gta4lEmmcBlock.cpp:2029-2085`,
  `:1963-1977`).
- Boot-partition lookup failure sets reason 2 and `part=1`
  (`package/Gta4lEmmcBlock.cpp:2157-2195`, `:1963-1977`).
- Descriptor, read, short-transfer and digest failures set reasons 4-7; read
  and short failures increment `fails`, a digest mismatch increments `failed`
  but not `fails` (`package/Gta4lEmmcBlock.cpp:2216-2270`).
- The 4,000 ms read budget sets reason 8 and `budget=1`; cancellation sets
  reason 9 and `cancel=1` without a panic
  (`package/Gta4lEmmcBlock.cpp:2108-2112,1963-1992`).
- `complete=1` is the source guard at
  `package/Gta4lEmmcBlock.cpp:2297-2299`: `budget == 0`, the latched reason is
  `NONE`, `failed == 0`, `passed == cases` and `cases != 0`.
- Hardware-initialization or baseline failure sets `rc`, `stop=11`,
  `fail_valid=1`, `fail_stage`/`fail_code` and panics
  (`package/Gta4lEmmcBlock.cpp:750-772`). Report- or watchdog-thread start
  failure sets `stop=16` with stage 1/17
  (`package/Gta4lEmmcBlock.cpp:795-823`). The 30,000 ms watchdog emits its own
  report with `rc=16 stop=16 fail_valid=1 fail_stage=16 fail_code=16`
  (`package/Gta4lEmmcBlock.cpp:3113-3162`).
- The terminal partread report ends in the intentional
  `panic("%s", reportBuffer)` at `package/Gta4lEmmcBlock.cpp:2642`; this is
  the normal report stop, not a fault.

Expected class-D signature (from N10.1 observation 1):

```text
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 PARTREAD: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6 blocks=138 bytes=70656 fails=0 cancel=0 budget=0 media=0 part=0 cap=61071360
GTA4L-EMMC P5 READTEST: index=5 target=1 blocks=1 off=100662784 ok=1 status=0 actual=512 digest=400039fd62daf3499b377ced75428918
GTA4L-EMMC P5 READFAIL: index=0 reason=0 status=0 actual=0 digest=00000000000000000000000000000000
GTA4L-EMMC P5 SNAP: locked=1
GTA4L-EMMC P5 FAIL: valid=0 stage=0 code=0 block=0 error=0
```

Allowed variation within class D: the incidental `DRV`/`XFER` aggregates and
timing, and the owner's line wrapping. Not allowed within class D: any change
to `cases=6`, `passed=6`, `failed=0`, `reason=0`, `complete=1`, `reads=6`,
`blocks=138`, `bytes=70656`, `fails=0`, `cancel=0`, `budget=0`, `media=0`,
`part=0`, `cap=61071360`, the six pinned digest values, or the final
`READTEST` case. A different final `READTEST`
index/target/blocks/offset/status/actual/digest is an anomaly to classify, not
a silent pass. `rc=0 stop=0` in the header is not partread success by itself.
`media=0 part=0` are clear failure flags, not success flags.

## 7. Stop rules

Stop the series at the first class A/B/C/E result; at any p69 or
protected-partition identity mismatch before or after an attempt; if the owner
cannot capture the expected screen or the tablet does not return to TWRP; on
unexpected panic text, a hang or a state change; or on any unauthorized
write, reflash, rollback, reset/repair or host-issued reboot. This is a
bounded success series unless a failure occurs, not a success-only batch past
a failure. It cannot estimate a failure rate.

## 8. What the series can and cannot establish

If every authorized attempt is class D, the series supports descriptive
repeatability of the six pinned content matches on the unchanged N10.1 image
across the approved cold boots. It does not establish initialization
reliability, a failure rate, a statistical bound, arbitrary I/O, filesystem
mount, root integration, writes, wider/faster modes, DMA/CQE/ICE, or the real
context/deblock/breakup layers. It does not verify report fields on an
uncaptured attempt; a missing or partial capture is not a pass. It does not
upgrade N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10 comparator `complete=1`,
the N9.11 owner-reported series or N10.1 observation 1. No running-image hash,
boot timestamp or rail-reset claim is added. Initialization reliability
remains `[U]`.

## 9. No new instrumentation in this protocol

The flashed image carries the existing `PARTREAD`, `READTEST`, `READFAIL`,
`MEDIA`, `SNAP` and `FAIL` report paths. This protocol adds no driver change,
rebuild, reflash, boot-argument change, new probe, QEMU run or added logging.
It relies on the existing report and the owner's capture. If the existing
report is insufficient to classify an attempt, stop and propose a separately
gated candidate; do not modify the image in this series.

## 10. Exact approval scope and gates

Execution requires fresh explicit owner approval of all of:

1. the read-only pre-series and post-series p69 and RECOVERY/EFS/sec_efs
   verification in section 2;
2. a finite owner-selected number of owner-controlled cold boots of the
   unchanged N10.1 image in section 2 (three additional proposed; no warm
   boots unless separately scoped);
3. the per-attempt capture and private evidence layout in section 5; and
4. no reflash, rebuild, rollback, reset/repair, partition write, host-issued
   reboot, retry, extra boot, commit or push.

If the owner does not approve, hardware stays paused. If approved, Build
executes only within that scope and stops at the first stop-rule trigger. A
new candidate, instrumentation or different image requires its own gates.

## 11. Evidence and privacy

Raw evidence stays in the gitignored
`out/gta4l/ram-root-emmc-n10.1-hardware-evidence/` tree. Each observation
directory contains `owner-transcript-raw.txt`, `interpretation.md`,
`observation-record.json` and a separate non-self-hashing
`observation-sha256.txt`; the manifest binds the immutable flash
`evidence-sha256.txt` (`d2b23ecf…`) and the preceding observation manifest.
The series closeout adds a summary and a non-self-hashing series manifest. The
tracked record contains only the sanitized summary. Do not put serial numbers,
unique device identifiers, account names, hostnames, SSH targets, e-mail
addresses, IP addresses, VPN names or absolute home paths in the tracked
summary or any manifest; raw private evidence stays outside the tracked tree
and is referred to by relative path and hash only. No manifest hashes itself.

This proposal is host-only and not executed. N10.1 observation 1 remains the
only N10.1 hardware result. The next step is owner review of section 10; no
device access, flash, boot, commit or push is authorized by this document.
