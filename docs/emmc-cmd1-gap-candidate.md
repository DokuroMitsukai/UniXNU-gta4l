# gta4l eMMC CMD1-to-CMD2 timing candidate

**Status:** host-only, unbuilt, unflashed candidate. The exact existing `fmt=4`
image remains the unchanged baseline. This document does not authorize a build,
flash, boot or any other device operation.

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

`scripts/test-gta4l-emmc.py` runs 98 scenarios and 55 mutation controls. The
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

## Baseline and build specification (not executed)

- Unchanged baseline image:
  `out/gta4l/ram-root-emmc-probe-failclosed-evidence/boot-ram-root-emmc-probe-failclosed.img`,
  85,688,320 bytes, SHA-256
  `4d2f7975d3bc2e26a1ab15fb4d60d765190fba2df0e6b4ee7c5226d41a63e5bf`.
- A candidate build must use the same 80-MiB diagnostic payload path, the same
  v5 boot-facing components and `gta4l-emmc-probe=3`. It must record its own
  source/archive/kernel/payload/image hashes, pass the payload contract 7/7 and
  the boot-image gate 18/18, and be built from exactly the candidate sources.
- The candidate image has not been built, flashed or run. Any on-tablet run
  requires separate owner approval for the exact image and bounded procedure, a
  verified EFS/SEC_EFS backup, a BOOT-only write, a pulled readback check and a
  recovery verification. No run is authorized by this document.

## Expected outcomes

| Result | Interpretation |
| --- | --- |
| Baseline fails CMD2; candidate passes CMD2 | Timing-sensitive behavior is supported for this run; not a root-cause proof or a reliability claim. Seek separate approval before adopting a general fix. |
| Both fail CMD2 identically | The timing candidate did not resolve the observed failure. Do not increase the delay blindly; move to the bounded CMD2 capture in the plan. |
| Both pass CMD2 | The earlier failure did not reproduce under these starting conditions; there is no basis to credit the delay. Continue stage-3 identification. |
| Candidate fails elsewhere | Record the first failure; a changed failure stage is not identification success. |
