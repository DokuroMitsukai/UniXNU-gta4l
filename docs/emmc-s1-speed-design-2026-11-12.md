# gta4l S1 speed design and record (2026-11-12)

Status: design plus host helper, host-only. The combined width/timing
revert helper now exists and is host-tested; the S1 block-driver mode is not
implemented. No candidate has been built, flashed or booted. This document
is the rung record for S1: the design sections below are corrected after the
2026-11-12 audit; the candidate, run and verdict sections are appended after
the implementation batch and the owner boot.

Risk class R1: a new combination of measured modes (HS timing plus a wider
bus) with reference support and a verified pre-report revert. Standing owner
approval covers the read-only preflight, the BOOT-only flash and local
commits.

## 1. Question and scope

Does the block driver read correctly through published BOOT media at HS
timing and the widest verified bus width? The rung qualifies only the
reported 4 KiB read window on the reported boot. It does not qualify
sustained or arbitrary I/O, a megabyte-scale throughput claim, writes, DMA,
interrupts, a filesystem, a mount or initialization reliability. Sustained
throughput moves to S2, which needs a transport or deadline change.

## 2. Evidence basis

- N10.24 (5.3.98) published the whole media and the BOOT partition at 4-bit
  SDR / 400 kHz, read eight tagged two-block cases in two passes and
  reverted to 1-bit with a verified revert. The mode contract, report
  lifecycle and watchdog budget are the base for S1.
- B2 (5.3.20) entered HS timing at 1-bit, matched the full stage-A content
  pass at the 50 MHz clock and reverted cleanly on two warm boots. B2 did
  not set `HOST_CONTROL.HISPD`.
- TWRP runs this eMMC at HS400 Enhanced Strobe in 8-bit, so DATA0-7 are
  wired and the 8-bit card/host path works on this board. The port has not
  used 8-bit.
- Linux `mmc_init_card()` selects timing before bus width, tries 8-bit then
  4-bit, and `sdhci_set_ios()` sets `HISPD` for `MMC_TIMING_MMC_HS`.
- The N10.24 LBA-1 baseline read and the publication discovery are retained.
  No 1-bit reference window is read: content is bound to the host digest of
  the final image, and a 4 KiB 1-bit reference at 400 kHz is not needed for
  a mode-correctness claim.

## 3. Contract

### 3.1 Token and selection

- Token `gta4l-emmc-speed=1`.
- Requires exactly one `gta4l-emmc-block=3` token.
- Refuses duplicate speed tokens, any block mode other than 3, any value
  other than 1, and every other diagnostic instrument: `partobs`,
  `partcmp`, `partread`, `deblock`, `breakup`, `sweep`, `consume`, `width`,
  `widthhold`, `widthpub`, plus the probe and SDHCI snapshot tokens already
  refused by the block-mode selector.
- The selector mirrors `gta4l_emmc_block_widthpub_select()` in
  `package/Gta4lEmmcBlock.c:332` and is unit-tested with the same truth
  table.

### 3.2 Start sequence (pre-watchdog)

1. Enter the report-mode start path and initialize hardware as today.
2. Allocate one 4 KiB page-aligned read buffer (`SPEED_WINDOW_BYTES` =
   8 blocks). The buffer holds one pass over the four two-block offsets.
3. Read the EXT_CSD baseline at 1-bit / 400 kHz.
4. Enter HS timing with `gta4l_emmc_set_hs_timing(&host, 1, &hs)`.
   - Primary configuration keeps `HISPD` clear, because that is the only
     configuration measured in this port (B2).
   - Record `hs_before`/`hs_after`, the RCG configuration, the
     `CLOCK_CONTROL` value, `switch_status` and both results.
5. Switch to 8-bit with `gta4l_emmc_set_bus_width_verified(&host, 8, &w)`.
   - If verification fails, the helper restores 1-bit. A 4-bit attempt is
     allowed **only** when the 8-bit failure reports a verified rollback
     (`rollback_result == GTA4L_EMMC_OK`) and the read-only EXT_CSD
     properties still match the baseline. An `ERR_WIDTH_RESTORE` result is
     terminal and forbids any fallback.
   - Record the winning width, `wfall`, `wroll`, the switch result, the
     host control before and after, the read-only EXT_CSD verification
     result and the CMD6 attempt count. `wroll=1` is set only after the
     failed 8-bit attempt reports a verified rollback.
6. Read the inherited LBA-1 baseline through the block core at the final
   mode. A failure is `BASE`; the start path then performs the combined
   synchronous revert and reports.
7. Publish the media and start the report thread.

`HISPD` is a separate one-variable rung (S1b) only if S1 fails at the wide
width with a data CRC or timeout. S1 never mixes both configurations.

### 3.3 Report thread (watchdog-covered)

1. Wait up to 8,000 ms for exactly one whole media.
2. Find the BOOT partition within 2,000 ms; open it read-only.
3. Two passes over the first 4 KiB of BOOT:
   - request two blocks (1 KiB) at a time through the partition IOMedia,
     with the advertised `limit=64` blocks and `seg=1`;
   - four requests per pass, eight requests total;
   - hash the 4 KiB pass after the fourth request;
   - check the 3,000 ms shared loop budget between requests;
   - stop at the first request error, short transfer or cancellation.
4. Re-read the same window (pass 2) and hash it independently.
5. Acquire the gate within 2,000 ms.
6. Revert with one **combined width and timing helper**, not two independent
   helpers. The helper must:
   - clear a stuck data inhibit;
   - drop the clock to 400 kHz;
   - write CMD6 byte 185 = 0 and wait for TRAN;
   - write CMD6 BUS_WIDTH = 1 and wait for TRAN;
   - clear the host width bits and verify the readback;
   - read EXT_CSD once and require byte 185 = 0 plus unchanged read-only
     properties;
   - record both CMD6 attempts and the final host control.
   The combined helper is `gta4l_emmc_recover_width_and_timing()` in
   `package/Gta4lEmmc.c`, implemented and host-tested in the 2026-11-12
   helper batch.
7. Emit the `SPEED:` line and the existing media fields, then take the
   intentional terminal report stop.

Any post-switch failure runs the same combined revert before the report. A
revert failure is terminal; the next boot's CMD0 resets the card. The two
separate helpers are not used together here because their combined
worst-case bound does not fit the watchdog (see §4).

### 3.4 Report fields

Success line, one logical line:

```
GTA4L-EMMC P5 SPEED: hs=1 hsrb=0 hsa=1 rcg=0000020b hispd=<2hex>
wtry=8 wfall=0 wroll=0 wid=<8|4> verified=1 hb=<2hex> ha=<2hex> cmd6=<u32> base=1
bytes1=4096 ticks1=<u64> hz=<u32> dig1=<32hex> bytes2=4096 ticks2=<u64>
dig2=<32hex> req=8 fails=0 cancel=0 budget=0 partial=0 pub=1 partdiag=0
revert=1 rverified=1 rhb=<2hex> rha=<2hex> rcmd6=<u32> rstatus=0
hsrevert=1 hsverified=1 rhsb=1 rhs=0 rclk=400000 limit=64 seg=1 cap=61071360
```

`wfall=1` records that 8-bit verification failed and 4-bit won. `ticks` are
the platform counter deltas and `hz` is `gta4l_emmc_platform_counter_hz()`.
The formatter-exact maximum is derived from the source constants and
recorded in the code.

### 3.5 Success predicate

- `hs=1`, `hsrb=0`, `hsa=1`, `rcg=0000020b`, `hispd` bit 2 clear,
  `wtry=8`, `wid` is 8 or 4, `verified=1`.
- `wfall=0` with `wid=8`, or `wfall=1` with `wid=4`.
- `wroll=0` with `wid=8`, or `wroll=1` with `wid=4`; `wroll=1` is accepted
  only because the failed 8-bit attempt reported a verified rollback.
- `hb` has no wide width bit; `ha` and `rhb` carry exactly the reported
  width; `rha` has neither bit.
- `cmd6>=1`, `rcmd6>=2`.
- `bytes1=bytes2=4096`, `req=8`, `partial=0`.
- `dig1=dig2` and both equal the host SHA-256 prefix of the first 4 KiB of
  the final BOOT image.
- `ticks1>0`, `ticks2>0`, `hz>0`.
- `fails=0`, `cancel=0`, `budget=0`.
- `pub=1`, `partdiag=0`.
- `revert=1`, `rverified=1`, `rstatus=0`, `hsrevert=1`, `hsverified=1`,
  `rhs=0`, `rclk=400000`.

A digest mismatch with clean status is treated as a buffer or offset defect,
not accepted.

### 3.6 Failure classes

| Class | Next action |
| --- | --- |
| HS entry refused or readback mismatch | Compare CMD6/CMD13 ordering with Linux `mmc_select_hs()`; one fix, one boot |
| 8-bit verify fail with a verified rollback | Try 4-bit; record `wfall=1` |
| 8-bit verify fail with `ERR_WIDTH_RESTORE` | Terminal; fix rollback before another image |
| Both widths refused | Report the 1-bit outcome; check the verified helper's rollback and the EXT_CSD compare |
| Data CRC/timeout at speed | S1b sets HISPD on entry and clears it on revert |
| Short/partial read | Budget or transfer-shape defect; report partial bytes |
| Digest mismatch | Buffer/offset bug first; add read-detail fields and re-run |
| Combined revert failure | Terminal; fix the helper in the simulator before another image |

## 4. Timing

Pre-watchdog start path:

| Step | Bound |
| --- | ---: |
| 1-bit EXT_CSD baseline (inhibit + CMD + data) | 3,000 ms |
| HS entry (2 EXT_CSD reads + CMD6 + TRAN + clock) | 9,261 ms |
| 8-bit verified switch (success path) | 6,010 ms |
| 8-bit failure and verified rollback (replaces the switch row) | 16,030 ms |
| 4-bit verified fallback (only after a verified 8-bit rollback) | 6,010 ms |
| 4-bit LBA-1 baseline | 3,000 ms |
| **Success path (8-bit)** | **21,271 ms** |
| **Success path after verified 8-bit refusal (4-bit)** | **31,291 ms** |

The pre-watchdog rows above still use the older per-TRAN estimate (one
1,010 ms wait). B2 re-derives them against the enforced deadline before the
start path is implemented; they gate nothing yet.

The start path has no 1-bit reference read and no megabyte-scale transfer.
The 4-bit fallback replaces the 6,010 ms 8-bit switch with the 16,030 ms
failed-switch bound, so the worst start path is 31,291 ms. Both paths are
pre-watchdog; N10.24 accepted 38,230 ms there.

Report thread:

| Step | Bound |
| --- | ---: |
| whole-media wait | 8,000 ms |
| BOOT discovery deadline | 2,000 ms |
| shared two-pass loop budget | 2,000 ms |
| one in-flight two-block request | 4,500 ms |
| gate acquisition | 2,000 ms |
| combined width/timing revert | 9,500 ms |
| **Total** | **28,000 ms** |

The revert budget is **enforced**, not estimated: the helper arms one
shared elapsed-time deadline before the clock switch and checks it in every
wait, including the command-inhibit wait inside each TRAN poll. The S1
budget is **9,500 ms**, so the report thread is at most **28,000 ms** and
the margin against the 30,000 ms watchdog is **2,000 ms**. Expiry is a
terminal revert failure (`hsrevert=0`, `rverified=0`), reported and not
retried.

The earlier 1 MiB / 27,220 ms draft is withdrawn. It assumed one in-flight
bound for a 32 KiB request, but `GTA4L_EMMC_BLOCK_CHUNK_BLOCKS` is two, so
that request is 32 sequential chunks. It also allowed only 2,000 ms for
leaving HS even though `gta4l_emmc_set_hs_timing(0)` performs two EXT_CSD
reads and two clock/CMD operations. Neither bound was supportable.

## 5. Host validation plan

- Selector truth table: required block mode and value, duplicates, each
  conflicting instrument, and the inert/no-token case.
- Combined revert helper: normal path, clock-switch failure, first CMD6
  failure, TRAN failure, second CMD6 failure, EXT_CSD read failure, byte-185
  mismatch, properties mismatch, and no fallback after `ERR_WIDTH_RESTORE`.
- Deadline enforcement: arm/clear on every exit, a no-budget regression,
  and expiry inside the clock wait, data reset, command-inhibit wait, TRAN
  poll and final read. Mutation controls drop or misreport each check.
- Simulator scenarios: 8-bit pass; verified 8-bit rollback then 4-bit pass;
  8-bit `ERR_WIDTH_RESTORE`; both widths fail; HS entry refused; CMD6
  readback mismatch; mid-read data CRC and timeout with the combined
  revert; short transfer; budget expiry with partial bytes; cancellation
  before and during a request; media discovery and partition deadline
  expiry.
- Mutation controls: every predicate term, the digest comparison, the
  winning width, `wfall`, `wroll`, `hispd`, both revert flags, `pub`,
  `partdiag`, `req`, `bytes`, `partial`, `rcmd6`, and selector conflicts.
- Evidence checker: `--mode speed` recomputes the first 4 KiB digest,
  requires the exact line shape, rejects well-formed wrong values (clock,
  RCG, CMD6 counts, width bits, HS states) and rejects a one-character
  digest mutation or a one-byte image change.
- Existing gates: eMMC core, probe, block, payload/package, evidence
  counts, RAM-root fixture, board GPT, TOML parse, `git diff --check` and
  the privacy grep.

## 6. Flash and boot

- Build the native kernel, storage-ABI audit, archive reconciliation,
  diagnostic payload and BOOT image with the established chain.
- BOOT gate 18/18 against the v5 baseline and `only_ramdisk_differs`.
- Flash BOOT only with the S0 helper; the required baseline covers BOOT,
  RECOVERY, EFS and sec_efs.
- One owner boot. The owner returns to TWRP; the rung report is normalized
  with `scripts/normalize-gta4l-report.py` and checked with `--mode speed`.
- One verdict boot; a pre-identification stop may be retried up to two
  more times on the same image under the existing boot accounting.

## 7. Open items

- The combined width/timing revert helper exists and passes the core
  scenarios. Its S1 budget is now enforced by a shared deadline. The S1
  block-driver mode and adapter scenarios are the next batch; do not flash
  until they pass.
- `HISPD` remains unset in the primary image; S1b is the fallback.
- The 8-bit / 50 MHz timing margin, pad state and DLL state are unmeasured
  until the boot.
- A PASS only qualifies the first 4 KiB of the BOOT image on that boot.
- Sustained throughput is S2 and needs a transport or deadline change; it
  is not part of S1.

## 8. Host helper batch

The 2026-11-12 helper batch adds
`gta4l_emmc_recover_width_and_timing()` and eighteen core scenarios:
`recover-both-clean`, `recover-both-8bit`, `recover-both-clock-busy`,
`recover-both-hs-stuck`, `recover-both-cmd6-error`,
`recover-both-width-error`, `recover-both-properties`,
`recover-both-inhibit-stuck`, `recover-both-inhibit-clear`,
`recover-both-final-read-timeout`, `recover-both-no-budget`,
`recover-deadline-inhibit`, `recover-deadline-tran`,
`recover-deadline-read`, `recover-deadline-clock`,
`recover-deadline-reset`, `recover-deadline-cmd6` and `recover-both-arg`.
The core suite passes 207 scenarios and 126 mutation controls. The S1 mode,
adapter scenarios, candidate, run and verdict are still to come.
