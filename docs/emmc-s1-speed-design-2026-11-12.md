# gta4l S1 speed design and record (2026-11-12)

Status: design, host-only. No candidate has been built, flashed or booted.
This document is the rung record for S1: the design sections below are
completed now; the host, candidate, run and verdict sections are appended
after the batch and the owner boot.

Risk class R1: a new combination of measured modes (HS timing plus a wider
bus) with reference support and a verified pre-report revert. Standing owner
approval covers the read-only preflight, the BOOT-only flash and local
commits.

## 1. Question and scope

Does the block driver read correctly through published BOOT media at HS
timing and the widest verified bus width, and what throughput does it
measure? The rung qualifies only the reported read window on the reported
boot. It does not qualify sustained, arbitrary, concurrent or write I/O,
DMA, interrupts, a filesystem, a mount or an initialization-reliability
claim.

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
- The N10.24 LBA-1 baseline read and the 4-bit publication discovery are
  retained; the S1 pre-switch path drops the 1-bit reference window because
  content is bound to the host digest of the final image.

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
2. Allocate one 32 KiB page-aligned read buffer (`SPEED_SLICE_BYTES` =
   64 blocks). No 1 MiB staging buffer is allocated.
3. Read the EXT_CSD baseline at 1-bit / 400 kHz.
4. Enter HS timing with `gta4l_emmc_set_hs_timing(&host, 1, &hs)`.
   - Primary configuration keeps `HISPD` clear, because that is the only
     configuration measured in this port (B2).
   - Record `hs_before`, `hs_after`, `rcg_before`, `rcg_after`, the
     `CLOCK_CONTROL` value, `switch_status` and both results in the report.
5. Switch to 8-bit with `gta4l_emmc_set_bus_width_verified(&host, 8, &w)`.
   - If verification fails, the helper restores 1-bit; try 4-bit next.
   - Record the winning width, the switch result, the host control before
     and after, the read-only EXT_CSD verification result and the CMD6
     attempt count. A 1-bit outcome is a reported refusal, not a panic.
6. Read the inherited LBA-1 baseline through the block core at the final
   mode. A failure is `BASE`; the start path then performs the synchronous
   verified revert and reports.
7. Publish the media and start the report thread.

`HISPD` is a separate one-variable rung (S1b) only if S1 fails at the wide
width with a data CRC or timeout. S1 never mixes both configurations.

### 3.3 Report thread (watchdog-covered)

1. Wait up to 8,000 ms for exactly one whole media.
2. Find the BOOT partition within 2,000 ms; open it read-only.
3. Two passes over the first 1 MiB of BOOT:
   - request 64 blocks (32 KiB) at a time through the partition IOMedia,
     with the advertised `limit=64` blocks and `seg=1`;
   - hash every returned slice into a running SHA-256;
   - check the 4,000 ms shared loop budget between requests;
   - stop at the first request error, short transfer or cancellation.
4. Re-read the same window (pass 2) and hash it independently.
5. Acquire the gate within 2,000 ms.
6. Revert the bus width to 1-bit with `gta4l_emmc_set_bus_width_recover()`
   using the 1-bit EXT_CSD baseline; verify the read-only properties segment.
7. Leave HS timing with `gta4l_emmc_set_hs_timing(&host, 0, &rhs)`; verify
   byte 185 has returned to 0 and the clock to 400 kHz.
8. Emit the `SPEED:` line and the existing `WIDTHPUB`-shaped media fields,
   then take the intentional terminal report stop.

Any post-switch failure runs the same width and timing revert before the
report. A revert failure is terminal; the next boot's CMD0 resets the card.

### 3.4 Report fields

Success line, one logical line:

```
GTA4L-EMMC P5 SPEED: hs=1 hsrb=<u32> rcg=<8hex> hispd=<2hex> wtry=<8|4> wid=<8|4> verified=1
hb=<2hex> ha=<2hex> cmd6=<u32> base=1 bytes1=1048576 ticks1=<u64> dig1=<32hex>
bytes2=1048576 ticks2=<u64> dig2=<32hex> req=<u32> fails=0 cancel=0 budget=0 partial=0
pub=1 partdiag=0 revert=1 rverified=1 rhb=<2hex> rha=<2hex> rcmd6=<u32> rstatus=<dec>
hsrevert=1 hsverified=1 rhsb=<u32> rhs=<u32> rclk=<8hex> limit=64 seg=1 cap=61071360
```

Names are fixed before the implementation batch; the formatter-exact
maximum is derived from the source constants and recorded in the code.

### 3.5 Success predicate

- `hs=1`, `wid` is 8 or 4, `verified=1`, `wtry` records the first attempted
  width, `hb`/`ha` and `cmd6` are present and numerically sane.
- `bytes1 = bytes2 = 1048576`, `partial=0`.
- `dig1 = dig2` and both equal the host SHA-256 of the first 1 MiB of the
  final BOOT image.
- `req = 32` (1 MiB / 32 KiB), `fails=0`, `cancel=0`, `budget=0`.
- `pub=1`, `partdiag=0`.
- `revert=1`, `rverified=1`, `rstatus=0`, `hsrevert=1`, `hsverified=1`,
  final `rclk` is the 400 kHz state and the final card `hs=0`.

A digest mismatch with clean status is treated as a buffer or offset defect,
not accepted.

### 3.6 Failure classes

| Class | Next action |
| --- | --- |
| HS entry refused or readback mismatch | Compare CMD6/CMD13 ordering with Linux `mmc_select_hs()`; one fix, one boot |
| 8-bit verify fail, 4-bit pass | Continue at 4-bit; open an 8-bit/HISPD/pad investigation as S1b or S8 |
| Both widths refused | Report the 1-bit outcome; check the verified helper's rollback and the EXT_CSD compare |
| Data CRC/timeout at speed | S1b sets HISPD on entry and clears it on revert |
| Short/partial read | Budget or transfer-shape defect; report partial bytes and their digest |
| Digest mismatch | Buffer/offset bug first; add read-detail fields and re-run |
| Revert failure | Terminal; fix the recovery order in the simulator before another image |

## 4. Timing

Pre-watchdog start path:

| Step | Bound |
| --- | ---: |
| 1-bit EXT_CSD baseline | 2,000 ms |
| HS entry (CMD6 + CMD13 + readback + clock) | 4,500 ms |
| 8-bit verified switch | 6,010 ms |
| 4-bit verified fallback (only after an 8-bit failure) | 6,010 ms |
| 4-bit LBA-1 baseline | 2,000 ms |
| **Success path (8-bit)** | **14,510 ms** |
| **Success path after 8-bit refusal (4-bit)** | **20,520 ms** |

The 1-bit reference window is deliberately absent; a 1 MiB 1-bit read at
400 kHz would take about 20 s and cannot fit.

Report thread:

| Step | Bound |
| --- | ---: |
| whole-media wait | 8,000 ms |
| BOOT discovery deadline | 2,000 ms |
| shared two-pass loop budget | 4,000 ms |
| one in-flight 32 KiB request at the slowest mode | 1,000 ms |
| gate acquisition | 2,000 ms |
| verified 1-bit revert | 8,220 ms |
| HS leave and verify | 2,000 ms |
| **Total** | **27,220 ms** |

Margin against the 30,000 ms watchdog is 2,780 ms. At 8-bit / 50 MHz the
two 1 MiB passes are expected in tens of milliseconds; the loop records the
actual elapsed ticks. A larger read set needs a new bound and owner
acceptance; the watchdog is not extended.

## 5. Host validation plan

- Selector truth table: required block mode and value, duplicates, each
  conflicting instrument, and the inert/no-token case.
- Simulator scenarios: 8-bit pass; 8-bit verify fail then 4-bit pass; both
  widths fail; HS entry refused; CMD6 readback mismatch; mid-read data CRC
  and timeout with a verified revert; short transfer; budget expiry with a
  partial digest; cancellation before and during a request; revert failure;
  HS leave failure; media discovery and partition deadline expiry.
- Mutation controls: every predicate term, the digest comparison, the
  winning width, `hispd`, both revert flags, `pub`, `partdiag`, `req`,
  `bytes`, `partial`, and selector conflicts.
- Evidence checker: add mode `speed` to
  `scripts/check-gta4l-emmc-evidence.py` with the host digest of the first
  1 MiB and the exact line shape; reject a one-character change and a
  one-byte image change.
- Existing gates: eMMC core, probe, block, payload/package, evidence
  counts, RAM-root fixture, board GPT, TOML parse, `git diff --check` and
  the privacy grep.

## 6. Flash and boot

- Build the native kernel, storage-ABI audit, archive reconciliation,
  diagnostic payload and BOOT image with the established chain.
- BOOT gate 18/18 against the v5 baseline and `only_ramdisk_differs`.
- Flash BOOT only with the S0 helper; preflight and postflight hashes cover
  BOOT, RECOVERY, EFS and sec_efs.
- One owner boot. The owner returns to TWRP; the rung report is normalized
  with `scripts/normalize-gta4l-report.py` and checked with
  `--mode speed`.
- One verdict boot; a pre-identification stop may be retried up to two
  more times on the same image under the existing boot accounting.

## 7. Open items

- `HISPD` remains unset in the primary image; S1b is the fallback.
- The actual 8-bit / 50 MHz timing margin, pad state and DLL state are
  unmeasured until the boot.
- A PASS only qualifies the first 1 MiB of the BOOT image on that boot.

## 8. Host, candidate, run and verdict

To be appended after the implementation batch and the owner boot.
