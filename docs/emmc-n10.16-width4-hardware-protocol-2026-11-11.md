# gta4l N10.16 width4 hardware protocol (2026-11-11)

Status: executed. The owner approved one controlled boot and the run completed
on 2026-11-11; the result is in
`docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md`. Flash, boot and
restore ran from the owner-controlled TWRP host; this repository host has no
`adb`/`fastboot`. One boot was consumed, no retry was made, and the pre-flash
BOOT image was restored and verified. The protocol text below is retained as
the executed procedure, with the §5 acceptance fields corrected to the source
semantics noted there.

## 1. Candidate and scope

- Candidate record: `docs/emmc-n10.16-width4-gatefix-candidate-2026-11-11.md`
- BOOT image
  `58352cd37f8643ba89c07c4911914fd50b4728c0333602aa4ca7840633c8aba7`
  (85,688,320 bytes)
- payload
  `10ff7ee86d82c7d7838b4d37c8ed426f2b56b9c23b926a51e0f0102bfbade79f`
- prelinked kernel
  `2be2eb0605b25758c57dcc3a80e631a76225dc975ffc4ac15aa8320e71cd7005`
- final-image first-1,024-byte prefix
  `a6da82e356863d1ab7e31c21a8fd85fc`

Approved scope: BOOT-only write of the image to p69, one controlled boot, the
opt-in `gta4l-emmc-block=3 gta4l-emmc-width=4` mode, CMD6 `BUS_WIDTH`
configuration writes and host width changes, the pinned two-block BOOT window
at device LBA 2,255,360 read once at 1-bit and once at 4-bit, and the existing
baseline/publication reads. Clock stays 400 kHz SDR, chunks stay two blocks,
the advertisement stays 64 blocks/32,768 bytes/one segment. No CMD19/CMD14,
other width, HS, DDR, DMA/CQE/ICE, mount, root switch, arbitrary/user-data I/O
or storage-data write is in scope.

## 2. Budget and stop rule

One owner-controlled boot attempt. Any attempt consumes the budget, including
a pre-identification stop. Stop at the first non-pass, identity mismatch or
TWRP-return deviation. No retry, reflash, rollback, reset, repair or extra boot
without fresh owner approval. A rollback image is retained, not automatically
restored.

## 3. Preflight (read-only)

1. Verify the local BOOT file size and SHA-256 match the values above.
2. In TWRP, save the current p69 full image as
   `boot-before-n10.16-gatefix.img.xz`; record its size, full SHA-256, candidate
   prefix and unchanged tail. Do not flash yet.
3. Record read-only hashes of RECOVERY, EFS and sec_efs before the write.
4. Confirm the BOOT partition is p69 and no other partition is in scope.

## 4. BOOT-only write

Write only p69, using the established 4,096-byte record path (20,920 records
for 85,688,320 bytes), then sync. Do not write any other partition. Read back
the full p69 and require:

- the candidate prefix `58352cd3…`;
- the unchanged tail expected from the retained backup;
- unchanged RECOVERY/EFS/sec_efs hashes.

A readback mismatch stops before boot.

## 5. One controlled boot and report

Boot once and capture the complete P5 report. Acceptance requires:

- `WIDTH` with `cases=1 passed=1 failed=0 reason=0 complete=1`;
- `switch=4 verified=1 hb=0 ha=2 cmd6=1` with the 1-bit→4-bit host
  `HOST_CONTROL` transition; `rb` is a diagnostic byte-183 readback and reads
  `0` on hardware (W/E_P field, not a completion condition);
- `reads=2 bytes=2048 blocks=4 subrequests=2 max=2 match=1 mismatches=0
  fails=0 cancel=0 budget=0`;
- `revert=1 rverified=1 rhb=2 rha=0` with `rcmd6` of `1` or `2` (the first
  successful attempt counts as one; the second is consumed only by a retry);
- `limit=64 seg=1 cap=61071360`;
- `WIDTHREAD` with both `digest` and `predigest` equal to
  `a6da82e356863d1ab7e31c21a8fd85fc` and `match=1`;
- no `WIDTHFAIL` and no watchdog report.

The driver's own terminal guard (`package/Gta4lEmmcBlock.cpp:6052-6091`) is the
authoritative acceptance check; it already encodes these conditions, including
`rcmd6` in `[1,2]` and no byte-183 equality. The earlier fixed `rb=1` and
`rcmd6=2` values in this section were simulated-model values and are
corrected here after the executed run.

A watchdog report (`rc=16 stop=16 fail_stage=16`), missing/truncated report,
pre-identification stop, or omitted digest is a non-pass and is not diagnosed
as a width failure. A width gate timeout (reason 8) is a terminal non-pass.

## 6. Return and evidence

Return to TWRP and confirm the boot return. The one-boot budget is then
consumed. Do not retry or restore the rollback unless the owner separately
approves. Capture the full report, the p69 readback, protected-partition
hashes and TWRP-return confirmation in the private evidence directory. Public
records use hashes and relative paths only.

The host-side WIDTHREAD comparison is
`scripts/check-gta4l-width4-evidence.py --image <final-boot.img> --widthread
<captured-line>`; it does not accept the summary or switch/revert fields by
itself.

## 7. Result (2026-11-11)

The executed boot passed the driver's terminal guard (`complete=1`) with the
expected read, match, revert, limit, capacity and digest values. The
transcript, host verifier result and restore record are in
`docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md`. The pre-flash
BOOT image was restored and verified byte-for-byte; RECOVERY, EFS and sec_efs
were unchanged. The one-boot budget is consumed. No retry, extra boot, write
scope extension or further hardware qualification is authorized by this
result.
