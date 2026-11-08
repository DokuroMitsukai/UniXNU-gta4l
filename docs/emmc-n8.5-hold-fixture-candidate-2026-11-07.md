# gta4l N8.5 hold-fixture candidate and flash (2026-11-07)

Status: host-only candidate build, BOOT-only flash and one owner-controlled
first cold boot of the corrected `fs-timer-hold` observation image on
2026-11-07. The boot returned the clean mode-3 report and TWRP; the boot budget
is consumed. No commit, push or host-issued reboot was performed. This is not a
partition-completeness or reliability result.

## 1. Why N8.5

N8.4's first cold boot returned the RAM-root `fs-timer` success panic before
the eMMC report thread, so the observation was inconclusive (see
`docs/emmc-n8.4-partobs-observation-2026-11-07.md`). The host-side correction
adds an additive `fs-timer-hold` variant that runs the same checks and holds in
a one-second `poll(NULL, 0, 1000)` loop instead of panicking, leaving
termination to the eMMC report thread or its watchdog. Failure paths still
panic. The original `fs-timer` fixture is byte-identical (`user.o`
`6984333d…`, signed `b316e44f…`, trust cache `e146717a…`, root `77d5eba0…`,
`fixture.json` `37cbdd0b…`).

The payload builder now requires `--data-ramdisk-manifest` naming the
`fs-timer-hold` fixture for both `*-partobs` personalities and rejects the old
early-panic fixture.

## 2. Candidate identity

The kernel and block archive are unchanged from N8.3; only the RAM-root data
ramdisk and trust cache differ.

- prelinked kernel `4dcbf24d…` (51,224,576 bytes; reused N8.3 candidate)
- block archive `f9f83526…` (512,896 bytes; 190 slots, 181 bindings,
  `runtime_qualified=false`)
- storage-ABI audit `out/gta4l/storage-abi-4dcbf24d` (`a30cfcde…`, provenance
  `a806c6cf…`, snapshot `222f6322…`, run `85307464…`)
- packaged kernel `023c8ff8…` (51,224,576 bytes; same as N8.3)
- hold root image `327ac826…` (57,344 bytes)
- hold trust cache `30a72f78…` (56 bytes; signed fixture `e6546715…`, nine
  verified pages)
- hold `fixture.json` `00363681…` (`diagnostic=fs-timer-hold`,
  `hold_after_success=true`)
- payload `28eafd24…` (83,886,080 bytes; 11/11 contract; diagnostic
  `emmc-block-3-partobs` mapping to `gta4l-emmc-block=3 gta4l-emmc-partobs=1`)
- BOOT image `b4f4f1ea…` (85,688,320 bytes; 18/18 gate; only the ramdisk
  differs from the `ce4d76e8…` v5 baseline)

The payload manifest records the hold fixture path/hash, diagnostic and
`hold_after_success=true`. The extracted payload kernel is `023c8ff8…`; the
token scan still finds one `PARTOBS` string, three `REPORT` strings, one
legacy `PART:` string, four `gta4l-emmc-partobs` strings and no literal
`complete=`.

## 3. BOOT-only flash

The candidate was flashed to `/dev/block/mmcblk0p69` only on 2026-11-07. The
pre-flash p69 `7c22e499…` (the N8.3 candidate) was backed up and verified as
`boot-before-n8.5.img.xz` (`e5b43228…`). The full p69 readback `9d51fe80…`
matches the candidate prefix `b4f4f1ea…` plus the unchanged tail `a28b4ea2…`;
RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` are unchanged.
The device disconnected once after the backup and before any write; adb was
restarted, p69 was re-read unchanged, and the candidate was then written once.
The flash record is `flash-record.json` in
`out/gta4l/ram-root-emmc-n8.5-hardware-evidence/`.

## 4. Boot observation

The owner performed one owner-classified first cold boot of the flashed image,
captured the panic report and returned to TWRP. No timestamp or photograph
bytes were supplied, and the report is associated with the image through the
flash record in section 3, not an independently measured running-image hash.
The boot budget is consumed; no retry or reflash follows.

The supplied report, reconstructed across OCR line wraps:

```text
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 DRV: online=1 wp=1 reads=12 blocks=35 fails=0 max=8
GTA4L-EMMC P5 XFER: req=35 c17=9 c18=13 c18b=26 cfail=0 stops=13 sfail=0 last_stop=0
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 PARTOBS: samples=8 first=39 first_ms=0 first_nz=39 first_nz_ms=0 final=80 final_ms=350 changes=2 stable=1 stable_ms=250 deadline_ms=4000 timed_out=0 none=0 error=0 slots=84 reported=3 complete=0
GTA4L-EMMC P5 PART1: base=4194304 size=4194304 content=other
GTA4L-EMMC P5 PART2: base=8388608 size=4194304 content=other
GTA4L-EMMC P5 PART3: base=12582912 size=4194304 content=other
GTA4L-EMMC P5 READ: lba=1 attempted=1 ok=1 bytes=512 sum=2228245566 base=2228245566 same=1
GTA4L-EMMC P5 QUAL: ref=1 ref_lba=2 ref_blocks=3 two_att=1 two_ok=1 two_reason=0 two_bytes=1024 two_mismatch=0 two_off=0 two_match=1024 two_c18=1 two_c17=0 two_stops=1 two_sfail=0
GTA4L-EMMC P5 QUAL3: att=1 ok=1 reason=0 bytes=1536 mismatch=0 off=0 match=1536 c18=1 c17=1 stops=1 sfail=0
GTA4L-EMMC P5 SNAP: locked=1
GTA4L-EMMC P5 FAIL: valid=0 stage=0 code=0 block=0 error=0
 @Gta41EmmcBlock.cpp:1205
```

The suffix `@Gta41EmmcBlock.cpp:1205` is preserved as supplied (OCR reads
`Gta41`; the source file is `Gta4lEmmcBlock.cpp`). The source's
`panicWithReport()` ends at `Gta4lEmmcBlock.cpp:1205` with the intentional
report panic. Frames, `pages` and `free` are not reproduced here and add no
storage qualification.

### 4.1 Bounded interpretation

- `rc=0 stop=0 mode=3` is a clean diagnostic completion; the panic is the
  intentional report terminal.
- The whole read-only `IOMedia` was published with the expected capacity,
  base 0, `GUID_partition_scheme` content and `writable=0`; this does not
  measure the card's physical write-protect state.
- The LBA 1 read matched its same-boot baseline checksum (`2228245566`); this
  is not full GPT CRC validation.
- The two-block and three-block reads delivered 1,024 and 1,536 exact bytes
  with one two-sector CMD18 chunk each, clean CMD12 stops and a CMD17 odd
  tail for the three-block request. The aggregate `XFER` counters include
  incidental reads and are not attributed entirely to the controlled
  requests.
- `PARTOBS` took eight samples at nominal `t = 0..350 ms`. The first sample
  was already 39, so the 0-to-39 transition was not observed. `changes=2`
  and `stable_ms=250` imply the last observed change was at nominal
  `t = 100 ms`; the intermediate count is unknown and is not reconstructed.
- `stable=1` means the count held for the stability window, not that
  publication is complete. `final=80` is consistent with the validated GPT's
  80 populated entries among 84 slots, but a count is not an exact set
  comparison. `complete=0` remains the explicit non-claim.
- `PART1..3` are the final enumeration sample at 4 MiB bases and 4 MiB sizes;
  enumeration order is not a stable GPT-index identity and these three
  entries are not an all-partition comparison.
- Timing fields accumulate requested 50 ms sleeps, not measured wall time,
  and sampling starts after prior report work, not at boot. The window cannot
  observe publication before its first sample.
- `SNAP locked=1`; `FAIL valid=0` with an all-zero record means no failure
  record was published.

The N8.5 bounded observation succeeded on this one owner-classified cold boot,
and the existing mode-3 controlled reads succeeded. The hold fixture no longer
preempted the report. Partition completeness and initialization reliability
remain unresolved `[U]`, and no driver fix is indicated by this report. The
N9.1 host-only design for the exact follow-up comparison is
`docs/emmc-n9.1-gpt-iomedia-comparison-design-2026-11-07.md`; it preserves this
report's `complete=0` and does not upgrade it. The raw transcript, interpretation
and observation record are in
`out/gta4l/ram-root-emmc-n8.5-hardware-evidence/observation-1/`: raw
transcript `240ab0dd…`, interpretation `0bea53a6…`, observation record
`92852a40…`, and the non-self-hashing observation manifest binds those files
and the preserved flash manifest. The original flash record and
`evidence-sha256.txt` are unchanged.

A stable `PARTOBS` count is not a completeness claim; `count=80` with
`stable=1` is consistent with complete publication but is not proof, and a
watchdog report means the observer did not complete, not that the card failed.
Initialization reliability remains `[U]`, and F2, p5b/F5, N5 and N6 verdicts
are unchanged. The N8.3 rollback remains available as
`boot-before-n8.5.img.xz`; the older N6 image rollback is in the N8.4 evidence
directory.
