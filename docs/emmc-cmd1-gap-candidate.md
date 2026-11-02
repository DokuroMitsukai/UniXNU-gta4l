# gta4l eMMC CMD1-to-CMD2 timing candidate

**Status:** specified, host-tested, built, flashed and run on 2026-11-01. The
owner's SM-T505N and two owner-reported SM-T505 units completed full stage-3
identification with `rc=0` on the candidate image. The exact `fmt=4`
fail-closed image remains the baseline it was compared against. This document
does not authorize another build, flash, boot or other device operation.

## Candidate

**Name:** `cmd1-crc-retire-8us`.

**Change:** after `gta4l_emmc_card_ready()` accepts a ready CMD1 response and
records `host->ocr` and `host->cmd1_response_valid`, it calls
`gta4l_emmc_delay(host, GTA4L_EMMC_CMD1_CRC_RETIRE_US)` before returning.
`GTA4L_EMMC_CMD1_CRC_RETIRE_US` is `8u` in `package/Gta4lEmmc.h`.

No other behavior changes: no retry, timeout, clock, rail, command-flag,
MMIO-width, poll-count, CRC-enable, response-promotion or CMD7-policy change.
The delay is not applied after a not-ready CMD1 reply or a failed attempt.

## Reference

Pinned downstream source:
`LinuxForGta4l/android_kernel_samsung_sm6115@63b2b11b0b5cffcc1eca07ac48a80ed31d8b75c4:drivers/mmc/core/mmc_ops.c`,
SHA-256 `ad043ed759d094f891a24a2a1fce1a4ef38295ecaff55d38838533d4c88c436d`.
Lines 210-220 record that the internal CRC status flag takes three MCLK cycles
(7.5 us at 400 kHz) to clear and execute `udelay(8)` after a successful
`mmc_send_op_cond()`.

Mainline Linux and U-Boot do not apply that explicit guard. The downstream
delay is consistent with the observed CMD1-to-CMD2 transition but does not
prove this controller needs it or that the recorded CRC error is spurious.
The actual MMIO overhead in the port is not a measured delay.

## Placement and the two status clears

The delay is after the accepted ready CMD1 response and before
`gta4l_emmc_init()` submits CMD2. CMD2's `send_command()` then performs its own
inhibit wait, normal/error W1C clears and pre-CMD2 raw-window reads before the
`COMMAND` write.

Both clears are after the modeled timer start: CMD1's post-command clear happens
before `gta4l_emmc_card_ready()` returns and calls the delay, while CMD2's
pre-command clear happens after the delay. The simulator model measures the
8-us window from the ready CMD1 response and does not reset it on either status
clear. That is an explicit assumption of this candidate. If the real CRC source
is instead re-armed by the final pre-CMD2 clear, this candidate would not cover
it; that would be a separately named delay-after-final-clear candidate, not a
global delay.

## Host tests

`scripts/test-gta4l-emmc.py` runs 112 scenarios and 62 mutation controls. The
timing candidate adds:

- `cmd1-crc-retire`: the hypothetical controller needs 8 us after ready CMD1;
  the candidate delay lets full identification complete, and the test checks
  that CMD2 was issued at least 8 us after the ready response.
- `cmd1-crc-retire-not-ready`: two completed but not-ready CMD1 replies precede
  the ready reply. The test checks three attempts, exact 1000-us gaps between
  those attempts (the delay is not applied after a not-ready reply), and the
  full 8-us ready-to-CMD2 gap.
- `cmd1-crc-retire-short`: the model needs 9 us while the candidate supplies 8.
  CMD2 must fail with `CMD_CRC`; the test checks that only CMD0, CMD1 and CMD2
  were issued, the failure step is CMD1, the CMD2 delay is exactly 8 us and no
  CID was promoted.
- The `cmd1-crc-retire-delay-removed` mutation removes the delay and must fail
  `cmd1-crc-retire` with `-8`.
- A source-policy check requires the 8-us constant, and requires the delay call
  to follow `host->cmd1_response_valid = 1` and precede `return GTA4L_EMMC_OK`.

The existing success, no-op, conflict, clock-gate, timeout, CMD2 CRC, CMD3,
CMD9 and CMD7 scenarios still pass. The simulator model is a hypothesis, not
silicon evidence; it does not prove the controller has the modeled CRC source.

## Baseline, build and flash (2026-11-01)

- Source baseline: the `fmt=4` fail-closed image, 85,688,320 bytes, SHA-256
  `4d2f7975d3bc2e26a1ab15fb4d60d765190fba2df0e6b4ee7c5226d41a63e5bf`,
  built from source at `aaaae41`: `Gta4lEmmc.c` `e493b3eb…`,
  `Gta4lEmmc.h` `e0a2830c…` and `Gta4lEmmcProbe.c` `932552a2…`. It failed CMD2
  on the owner's SM-T505N and on a reported SM-T505
  (`docs/emmc-cmd2-crc-audit.md` §2.9-2.10).
- Candidate build: repository HEAD
  `2a3268c7fa68c384470bb0ee87270ac594ec3fec`, clean tree, the same parent source
  with only the 8-us delay and its constant added. Source hashes `874e6878…`
  (`Gta4lEmmc.c`), `5dd223f7…` (`Gta4lEmmc.h`) and `932552a2…`
  (`Gta4lEmmcProbe.c`); archive `22e6186f…`; payload kernel `ee33719e…`;
  payload `6b6f5212…`, 83,886,080 bytes, `gta4l-emmc-probe=3`, contract 7/7;
  boot image `8a463dae…`, 85,688,320 bytes, gate 18/18 against the pinned v5
  baseline `ce4d76e8…`. Only the ramdisk section differs from that baseline.
- Flash: BOOT only (`/dev/block/mmcblk0p69`). Pre-flash partition `b53f31fe…`
  (the fail-closed image plus its unchanged tail), post-flash `92f4000e…`,
  image-byte readback `8a463dae…`, tail `a28b4ea2…` unchanged, `RECOVERY`
  `14e11370…` unchanged, pulled-image gate 18/18 and pulled payload contract
  7/7. The record is
  `out/gta4l/ram-root-emmc-probe-cmd1gap-evidence/flash-record.json`; it was
  written after the BOOT write and records no reboot or boot at that time.

## Hardware result (2026-11-01)

The owner subsequently rebooted and ran the candidate on three units: the
owner's SM-T505N and two SM-T505s. All three runs reached
`GTA4L-EMMC IDENT REPORT: rc=0` and stopped at the probe's intentional report
panic (`package/Gta4lEmmcProbe.c:497`). The report is emitted only after
`gta4l_emmc_init()` returns `GTA4L_EMMC_OK` and before the stage-3 probe's
sector read. Common report values: `mci=10000072 rca=1 cap=61071360 rev=8 bw=0
ocr=c0ff8080 hc2=0008 ven=00018a9c dll=6000642c dst=00000000 pst=00000000
step=15 log=5`; valid CMD2 capture with normal `0x0001`; Samsung CID with
product name `DX68M`; identical pre-CMD7 raw response window; `c3l` and `c7q`
valid with 38 iterations; CMD7 CC and TC both observed with no error (command
`0x071B`, argument `0x00010000`). `step=15` is `GTA4L_EMMC_STEP_READY`, reached only after
the EXT_CSD read and the sector-size, capacity and partition-access checks
pass. The capacity matches the EXT_CSD `SEC_COUNT` and the recovery inventory.
Per-unit differences are limited to the CID serial/date words, the plain
EXT_CSD byte sum (`0x9CB` versus `0x9C7`), a few poll-counter ticks and the XNU
VM page counters. The full transcription, field comparison and decode are in
`docs/emmc-cmd2-crc-audit.md` §2.11 and `docs/emmc-driver.md` §5.3.4.

The run results are owner-supplied text transcriptions; no photo file or photo
hash from this run is in the tree, and the unit/model attribution is
owner-reported. The owner also reports expert confirmation that SM-T505,
SM-T505N and SM-T500 are identical for eMMC with only LTE/Wi-Fi/regional
differences; that is not an independently verified part-number or
board-revision identity.

## Pre-registered outcomes

These criteria were fixed before the run.

| Result | Interpretation |
| --- | --- |
| Baseline fails CMD2; candidate passes CMD2 | Timing-sensitive behavior is supported for this run; not a root-cause proof or a reliability claim. Seek separate approval before adopting a general fix. |
| Both fail CMD2 identically | The timing candidate did not resolve the observed failure. Do not increase the delay blindly; move to the bounded CMD2 capture in the plan. |
| Both pass CMD2 | The earlier failure did not reproduce under these starting conditions; there is no basis to credit the delay. Continue stage-3 identification. |
| Candidate fails elsewhere | Record the first failure; a changed failure stage is not identification success. |

Observed: the first row. On the owner's unit the fail-closed image failed CMD2
and the candidate completed CMD2 and full identification; the delay is the
only source delta between the two images. That supports a timing-sensitive
contribution for these runs, not a root cause. The earlier `cmd2window` and
`fmt=2` builds also passed CMD2 on the owner's unit without the delay, so the
delay is not proven necessary and no general fix is adopted. No sector read,
block-device publication, bus-width change, DMA, CQE, ICE, write or mount was
issued, and CRC checking remains enabled.
