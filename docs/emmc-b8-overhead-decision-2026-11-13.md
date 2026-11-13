# gta4l eMMC B8 per-request overhead and S2 shape decision (2026-11-13)

Status: **host-only**. This batch adds the per-request breakdown to the S1
diagnostic, runs every host gate, and decides the S2 transport shape from the
S1 aggregate. It builds no candidate, does not touch a device and adds no
boot. The S1 image `805a5b66…` is not booted again. The per-phase split itself
is read on the next speed-window boot, because the only S1 boot is consumed;
the shape decision below uses the measured S1 pass times and the source
timeouts, and the next boot re-checks it.

## 1. Question

The S1 pass moved 4,096 bytes in 2.414–2.912 ms at 8-bit / 50 MHz, where the
wire time is 81.92 µs. Which per-request phases hold the remaining 96.6%,
and what chunk and advertised-limit shape should S2 use so the
85,688,320-byte BOOT region can be streamed without raising the 30,000 ms
watchdog?

## 2. S1 input and the overhead model

S1 (`docs/emmc-s1-speed-design-2026-11-12.md` §9): `hz=19200000`, two passes
of four two-block requests, `bytes1=bytes2=4096`, `req=8`, every digest
matching, `fails=0 cancel=0 budget=0 partial=0`.

| Pass | Ticks | Time | Wire | Overhead | Per 1 KiB request | Fixed per request |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 55,912 | 2.912 ms | 81.92 µs | 2.830 ms (97.2%) | 728.0 µs | 707.5 µs |
| 2 | 46,348 | 2.414 ms | 81.92 µs | 2.332 ms (96.6%) | 603.5 µs | 583.0 µs |

The two passes bound a per-request fixed cost `F` of 583–708 µs and a wire
cost of `bytes / 50 MB/s`. The table projects that model at the four candidate
request sizes; the last two are the S2 options.

| Request | Wire | Projected total | Rate | BOOT region |
| --- | ---: | ---: | ---: | ---: |
| 1 KiB (2 blocks) | 20.48 µs | 0.60–0.73 ms | 1.41–1.70 MB/s | ≈ 50 s |
| 8 KiB (16 blocks) | 163.84 µs | 0.75–0.87 ms | 9.4–11.0 MB/s | 7.8–9.1 s |
| **32 KiB (64 blocks)** | 655.36 µs | 1.24–1.36 ms | 24.0–26.5 MB/s | 3.2–3.6 s |
| 128 KiB (256 blocks) | 2.621 ms | 3.20–3.33 ms | 39.4–40.9 MB/s | 2.1–2.2 s |

The region is exactly 2,615 × 32 KiB requests, so the 64-block row has no
tail.

Decision rule for the next boot: the containment
`inh + cmd + dat + stop ≤ core ≤ bus ≤ io ≤ win` must hold, and `dat` should
be close to the wire time. If `dat` is much larger than the wire and scales
with blocks, the real per-request cost is higher than the model and the
measured rate becomes the S2 input. If `io`, `inh`, `cmd` or `stop` dominates,
the model's `F` is confirmed and the amortization below is the right lever.
No outcome makes 256 blocks the safer first choice.

## 3. Instrumentation

`timing_capture` is a caller opt-in flag in `struct gta4l_emmc_host`
(`package/Gta4lEmmc.h`). With it clear every diagnostic hook is a no-op and
production pays neither a counter read nor a sum. `gta4l_emmc_timing_close()`
(`package/Gta4lEmmc.c`) moves the stamp and adds the delta; every phase is
closed before the CMD12 stop, so a failed command or data wait cannot be
charged to the stop.

| Field | Source | Meaning |
| --- | --- | --- |
| `req` / `blk` | core | matched CMD18 request and block counts |
| `inh` | core | command-inhibit entry |
| `cmd` | core | CMD18 register write to command complete |
| `dat` | core | command complete to all data blocks consumed |
| `stop` | core | the complete CMD12 stop |
| `core` | adapter | `gta4l_emmc_read_multi_result()` wall time |
| `bus` | adapter | `gta4l_emmc_block_read()` wall time |
| `io` | adapter | one tagged `widthholdReadCase()` round trip |
| `win` | adapter | `ticks1 + ticks2` |
| `hz` | adapter | counter timebase |

The report adds one `GTA4L-EMMC P5 SPEEDIO:` line after the `SPEED` line. The
line is at most 267 bytes (23-byte label, eleven fields, ten separators); the
combined S1 report is 1,854 bytes inside the 2,560-byte buffer, enforced by a
compile-time assert. The checker extracts the single `SPEED` line by exact
label and is unaffected by the extra line.

The watchdog's second report copy moves from the instance to an explicit
allocation (`watchdogReport`), so the enlarged report does not double the
adapter instance and an allocation failure is a pre-watchdog start failure.
The watchdog still formats the same struct under the same bound.

## 4. Chunk and limit decision

**S2 uses `GTA4L_EMMC_BLOCK_CHUNK_BLOCKS = 64` and keeps the advertised
`limit = 64` blocks.** One request becomes one 32 KiB CMD18 plus CMD12
instead of 32 sequential two-block chunks, so `F` is paid once per 32 KiB.
The projected region time falls from ≈ 50 s to 3.2–3.6 s.

Rejected: advertising 256 blocks for the first S2 batch. `MAX_BLOCKS` sets
`MAX_BYTES` = 128 KiB and ripples through the partread, deblock, breakup,
sweep, consume, partmap and adapter bounds, the bounce buffers and the host
matrix. The gain over 64 blocks is only 1.6× (2.1 s instead of 3.4 s), and
the full region still does not fit the current additive worst-case loop
allowance (see §5). It is a later measurement-driven change, not an S2
prerequisite.

Consequences the S2 implementation already owes:

1. One request is one chunk, so the 4,500 ms in-flight case stays exactly
   4,500 ms (one command-inhibit wait, one data wait, one drain, one CMD12
   entry and one CMD12 completion). It is not multiplied by chunks per
   request. A larger advertised limit without the matching chunk would
   multiply the bound (32 two-block chunks for a 64-block request) and break
   the watchdog; the two constants must move together.
2. Move the 32 KiB chunk bounce out of the adapter instance. The instance
   already carries a 2,672-byte report, a 1,024-byte chunk and 1.5 KiB of
   qualification buffers; a 64-block inline buffer would push it past the
   small-allocation range. The chunk should be allocated and freed with a
   checked failure path like the report buffer.
3. Decouple the partmap chunk from the core chunk. The current
   `GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS` equals the core chunk by assert;
   at 64 blocks its bounce would be 32 KiB while its scratch array is 16 KiB,
   so the GPT path would need a resize for no benefit. Keep the proven
   partmap two-block shape and drop the equality assert.
4. Update the hardcoded chunk-shape expectations in the host suites (for
   example a 21-block read becomes one CMD18 instead of ten CMD18s plus a
   CMD17 tail). Symbolic expectations that use `CHUNK_BLOCKS` stay valid.

## 5. Report-thread arithmetic

S1's report-thread sum is unchanged by this batch:
8,000 media + 2,000 BOOT + 2,000 loop + 4,500 in-flight + 2,000 gate +
9,500 combined revert = 28,000 ms, leaving the fixed 2,000 ms under the
30,000 ms watchdog. The existing static asserts tie the 4,500 ms term to the
transport timeouts and the total to the watchdog.

The chunk change does not change that sum because one request is one chunk.
It does expose the S2 budget choice: inserting a 6,000 ms bulk loop into the
same sum would give 32,000 ms and violate the watchdog. S2 must therefore
either keep the ≤ 2,000 ms loop (a verified prefix of about 50 MB at the
projected rate) or make the bulk deadline inclusive by checking the armed
deadline inside the CMD18 data and CMD12 polls before raising the loop
budget. That choice, its static assert and its scenarios belong to the S2
design; B8 changes no budget constant.

## 6. Host validation

| Gate | Result |
| --- | --- |
| `scripts/test-gta4l-emmc.py` | PASS: 211 scenarios, 128 mutation controls |
| `scripts/test-gta4l-emmc-probe.py` | PASS: 196 scenarios, 100 mutation controls, 13 width-bound materializations, 1 oversized-report control |
| `scripts/test-gta4l-emmc-block.py` | PASS: 48 core scenarios, 38 mutation controls; 419 adapter scenarios, 158 adapter, 3 support and 37 policy mutation controls |
| `scripts/test-gta4l-emmc-evidence.py` | PASS: dispatcher, normalizer and `--mode speed` round trip |
| `scripts/test-gta4l-emmc-evidence-counts.py` | PASS: documented counts match sources |
| `scripts/test-gta4l-ram-root-fixture-recipe.py` | PASS after the evidence-baseline refresh |
| TOML parse and `git diff --check` | PASS |

The adapter suite adds one mutation, `speed-report-io-dropped`, which zeroes
`speedio_req` and is rejected by `speed-match-8bit`. That scenario also
asserts `req=8`, `blk=16`, the phase containment and
`win = ticks1 + ticks2`. `speed-format-max` asserts the 267-byte line and the
combined buffer bound.

## 7. Limits and follow-up

This batch adds no hardware claim. The phase split, the projected rate and
the 64-block shape are host-derived until the next boot reads the `SPEEDIO`
line; the S1 image stays grounded. No sustained I/O, arbitrary region,
write, DMA, interrupt, CQE or lifetime mode is qualified here.

Next: the S2 `bulk` design (token, sequence, report line, success predicate,
failure classes, budget with the inclusive-deadline option or the additive
prefix, and the host scenario/mutation list), then its implementation. S2
keeps the S1 two-pass window, the verified pre-report revert and every S1
success field so the S1 coverage is re-asserted on each boot.

## 8. S2 implementation status (2026-11-13)

The S2 design above is implemented as the opt-in `gta4l-emmc-bulk=1`
diagnostic and the `emmc-block-3-bulk` payload personality. It streams the
85,688,320-byte BOOT region in 2,615 64-block requests under a 4,000 ms
stream loop and an inclusive 4,500 ms transport deadline, checked inside the
CMD18 data and drain polls. The stop transmission keeps its own 2,000 ms
CMD12 bound and is still issued after an expiry; the report-thread sum is
the 28,000 ms computed in §5 and the report bounds are compile-time
asserted. It hashes exactly the bytes read, reads 64 aligned plus 16
unaligned two-block windows, and replaces the S1 two-pass window with that
80-window set while retaining the S1 start-path fields. Host validation now
passes 213 core scenarios and 130 mutation controls, 47 block-core scenarios
and 35 mutation controls, 443 adapter scenarios and 208
adapter/support/policy mutations; the block suite adds 25 bulk scenarios and
10 new bulk mutations. The payload builder, packager and contract tests pass
with the bulk personality and the `fs-timer-hold` fixture, and the evidence
dispatcher has a `--mode bulk` verifier. See `docs/emmc-driver.md` §5.3.105.
No candidate, flash or boot was produced; the S2 rate, sustained windows and
the hardware revert path remain `[U]`.

## 9. S2 candidate (2026-11-13)

Built from the uncommitted working tree after `de7d0d1`; the S2 host batch
was not committed or pushed at packaging time.

| Input | Value |
| --- | --- |
| Source | working tree after `de7d0d1` |
| Candidate kernel | `053b5c577cf553a9c21d782539db750425b00a87db5265c87b32d2f23efe76f0` |
| Storage-ABI artifacts | `storage-abi-053b5c577cf5` |
| Block archive | `1613fb52d4175f671b559d39a14c4d6ba222ae7a0228fcc6015f658db106a0b3`, requalified against the final kernel |
| Diagnostic | `emmc-block-3-bulk`, `gta4l-emmc-block=3 gta4l-emmc-bulk=1` |
| Data ramdisk | `fs-timer-hold` fixture, owner trust cache |
| Payload | `182c3ad4079399f0d2aa8dd54b53a7dde1b22812cf29cf246a0bfd3e52c13d4b` (83,886,080 bytes) |
| BOOT image | `c6e23141ba0c7ff475ae85118955cc43b17522b96a29797c5d30606ad4913fab` (85,688,320 bytes) |
| BOOT gate | PASS, 18/18 checks against the v5 baseline `ce4d76e8…` |
| Region digest source | first 9,142,272 bytes of the final image, `5710fc38b12378dc5786da8d22b4a173…` |

## 10. Run (2026-11-13)

Flash. BOOT only, `mmcblk0p69` on a 100,663,296-byte partition. TWRP
3.7.1, battery 100%, adb state `recovery`. The read-only preflight matched
the previous post-flash state: BOOT full `5342e715…`, candidate prefix
`805a5b66…`, tail `a28b4ea2…`, RECOVERY `14e11370…`, EFS `51fa824c…`,
sec_efs `3a265d58…`. The write covered 20,920 blocks; the postflight read
`513a0e38…` for the full partition with the image prefix `c6e23141…`, the
same tail and unchanged protected partitions. Record: `flash-record.json`
(`result=pass`) in the gitignored evidence directory.

Boot accounting. One owner-triggered boot; the S2 one-boot budget is
consumed. The cold/warm class and the boot ordinal were not recorded. The
image must not be booted again.

Report (recorded fields):

| Line | Values |
| --- | --- |
| `BULK` | `region=85688320 chunks=279 bytes=9142272 ticks=77071679 hz=19200000 seed=47544134 windows=80 wpass=0 digest=5710fc38b12378dc5786da8d22b4a173` |
| `BULKFAIL` | `stage=6 status=0 cancel=0 budget=1` |
| `SPEEDIO` | `req=279 blk=17856 inh=13447 cmd=18710 dat=63288896 stop=139839 core=63510393 bus=63798706 io=63884420` |
| Revert | `rverified=1 hsverified=1 rclk=400000`; `cfail=0 sfail=0 fails=0` |

The `BULK` and `BULKFAIL` rows quote the fields captured in the run record;
the formatter also always emits `wfail` and `woffset`, which are zero here
because the window phase never started, and the failure line repeats the
stream `bytes` and `chunks`. `wpass=0` is not a window failure.

Host verification. The SHA-256 of the first 9,142,272 bytes of the final
image is `5710fc38b12378dc5786da8d22b4a17339b3a0e76bebc539d5305ba3c12b5106`,
so the prefix digest matches exactly. The `check-gta4l-bulk-evidence.py`
self-test passes; its full-run verifier is expected to reject this report
because the success shape (2,615 chunks, 85,688,320 bytes, 80 windows) was
not reached.

Measured breakdown. `hz=19200000` is the 19.2 MHz XO counter.

| Item | Ticks | Time | Share of stream |
| --- | ---: | ---: | ---: |
| Stream loop | 77,071,679 | 4.014 s | 100% |
| CMD18 data phase | 63,288,896 | 3.296 s | 82.1% |
| Data per 512-byte block | 3,544.4 | 184.6 µs | — |
| Wire time per block at 8-bit / 50 MHz | 196.6 | 10.24 µs | — |
| Data per 32 KiB request | 226,842 | 11.81 ms | — |
| Inhibit + command + stop, all 279 requests | 171,996 | 8.96 ms | 0.22% |
| `core − dat` | 221,497 | 11.54 ms | 0.29% |
| `io − dat` (includes the IOKit round trip) | 595,524 | 31.0 ms | 0.77% |
| Stream minus `io` (hash and report path) | 13,187,259 | 0.687 s | 17.1% |

Stream throughput was 2.28 MB/s. The data phase is **18.0×** the 8-bit /
50 MHz wire time. The hash path alone runs at about 13.3 MB/s. Projecting
the measured rates to the whole region gives 30.90 s of data phase plus
6.44 s of hashing, about **37.3 s**, which cannot fit the 30,000 ms
watchdog.

Corrections:

- The §2 fixed-cost model projected 1.24–1.36 ms per 64-block request and
  24.0–26.5 MB/s. Run 1 measured 14.39 ms per request (11.81 ms of it data
  phase) and 2.28 MB/s. The model's wire floor is not the limiting term at
  this chunk size.
- The S1 §9 attribution of 96.6% to per-request overhead is refuted at
  64 blocks: inhibit, command, stop and the IOKit path together are under
  1% of the stream. S1's per-request numbers remain valid for 1 KiB
  requests; the conclusion drawn from them does not. See the S1 erratum.
- The actual budgets in the source are 4,000 ms for the stream loop and
  4,500 ms for the inclusive transport deadline
  (`Gta4lEmmcBlock.h:486-487`), not 5,500 / 6,500 ms.

Open cause. Two hypotheses remain and run 1 cannot separate them:

- H1: the effective card clock or width is below the configured 8-bit /
  50 MHz. 184.6 µs per 512-byte block is consistent with 8-bit at about
  2.78 MHz or 1-bit at about 25 MHz. The SDHCI Clock Control (0x2C) divider
  and Host Control 1 (0x28) width were never captured.
- H2: the device-memory FIFO drain costs about 1.44 µs per 32-bit word
  (128 reads per block). The unconditional 10 µs poll delay is at most
  5.4% of the per-block time and cannot explain the gap.

The random-window phase never started, so it contributes no evidence.

## 11. Verdict

Result: **INCOMPLETE (controlled stream-budget exit)**.

Qualified claim. On one owner boot of `c6e23141…`, the bulk diagnostic
streamed 9,142,272 of the 85,688,320 BOOT bytes through the partition
IOMedia in 279 64-block requests, with no I/O error, cancellation or short
transfer, reproduced the host SHA-256 prefix of exactly those bytes, and
reverted to 1-bit / legacy timing / 400 kHz with the card and host verified.

Not qualified. The full-region digest, the 80-window set, any sustained or
arbitrary I/O, a throughput claim, writes, persistence, DMA, interrupts,
CQE, HS200/HS400, a filesystem, a mount, repeatability, initialization
reliability, the electrical timing margin, pad or DLL state, and lifetime
fast mode. The S2 stage predicate is not satisfied.

Failure class: budget expiry before the window phase, not an I/O failure.

## 12. Follow-up

1. S2.1 is instrumentation only: split the CMD18 loop into BRR wait and
   FIFO drain, count poll iterations and 10 µs delays, capture SDHCI 0x28 /
   0x2C / 0x24 and the capabilities registers while in HS/8-bit, read back
   the GCC SDCC1 RCG configuration, and time the SHA-256 update separately.
   No transport change, so cause and effect stay separable.
2. From the evidence: `brr_wait` ≫ `drain` selects H1; `drain` ≫ `brr_wait`
   selects H2; the register snapshot can confirm or refute the divider and
   width directly.
3. Fix the selected cause under the host gates, then run S2 again with the
   full region and the 80 windows. Raise the stream/total budgets only from
   the measured rate with margin, under the 30,000 ms watchdog
   `static_assert`; shrink the region only if the measured rate cannot fit
   at all.
4. Do not boot `c6e23141…` again. Refresh the protected baseline before the
   next flash.

## 13. S2.1 host instrumentation (2026-11-13)

Instrumentation-only follow-up to §9–§12. It changes no transport behaviour
and adds no candidate, flash or boot; the same `emmc-block-3-bulk`
personality and the same budgets are used.

Core. The CMD18 PIO loop now charges the wait for a live BRR sample, from
command completion or the previous drain end, to `timing_brr_wait_ticks`,
the 128-word FIFO read to `timing_drain_ticks`, and counts primary poll
iterations and the 10 µs delay steps in `timing_polls`/`timing_delays`.
Every counter is opt-in through `timing_capture`, so production callers pay
nothing. The new `gta4l_emmc_capture_diag_regs()` takes a one-shot,
sequential (not atomic) snapshot in HS/8-bit: Host Control 1, Clock Control,
Present State, Capabilities 0/1, `CORE_DLL_CONFIG/STATUS`,
`CORE_VENDOR_SPEC`/`SPEC3`, and GCC SDCC1 APPS RCG CFG/M/N/D.

Report. Bulk mode emits one `GTA4L-EMMC P5 BULKIO:` line after the BULK
line: `brr`, `drain`, `polls`, `delays` and `sha` in ticks, then
`hc`/`cc`/`ps` and the packed `cap`/`dll`/`ven`/`rcg`/`rcg2` pairs. The
packing is high:low: `cap = caps1:caps0`, `dll = status:config`,
`ven = spec3:spec`, `rcg = m:cfg`, `rcg2 = d:n`. The formatter bound is
235 bytes and the combined bulk worst case is 2,508 bytes inside the
2,560-byte buffer. The SHA-256 update is timed separately from the read,
so the hash cost no longer hides inside the non-I/O term.

Host results. The core suite now passes 214 scenarios and 132 mutation
controls, including `multi-timing-s21` and two mutations that zero the BRR
wait or the poll count. The block suite passes 47 core scenarios, 35 core
mutations, 443 adapter scenarios and 170 adapter, 3 support and 37 policy
mutations; the success and maximum-format scenarios assert the BULKIO
fields and line bound, and two new mutations drop the BRR or the Clock
Control copy. The payload contract, report bounds and evidence suites are
re-run before the candidate build. The S2.1 rate, the register snapshot
values and the fix decision remain `[U]` until the next owner boot.

## 14. S2.1 candidate and flash (2026-11-13)

| Input | Value |
| --- | --- |
| Source | working tree after `de7d0d1` |
| Candidate kernel | `ada70559771e4ec4df05746c2a70ec85c50070ce6d6c5ae435b5885c052a0fce` |
| Storage-ABI | `out/gta4l/storage-abi-ada70559771e`, 190 slots, native 4K |
| Block archive | linked `afad7d2f…`, requalified `971687be…`; the member objects are byte-identical (`Gta4lEmmcBlock.o` `25cc9adf…`, `Gta4lEmmcBlockCpp.o` `1e257c02…`, bindings `3e53b66d…`) |
| Payload | `2eb96c0c7ccde00baed6d413892d5a3e7bd9832927dd3e96163e7c6416dfbab3` (83,886,080 bytes) |
| BOOT image | `fec0f921f9288df0935c6299c97a255f9fdcaa9fca796f53b6814edaa2491e10` (85,688,320 bytes) |
| BOOT gate | PASS, 18/18 against v5 `ce4d76e8…`; only the ramdisk differs |
| Compressed image | `3ae4cb461006f1eef1543f5b7d1db29ff2b265d3fbdc0fc323353c764e0d5d08` (8,038,524 bytes) |

Build note. The storage-ABI loop was run against the bootstrap kernel
`6dd464a2…`, then `a917d854…`, `997d8c93…`, `3811be54…` and finally
`ada70559…`. The candidate was linked with the archive built against the
`3811be54` audit; the archive requalified against the `ada70559` audit has
the same member objects byte-for-byte after `llvm-objcopy --strip-debug`,
so only the archive container metadata differs. The candidate audit
matches the final kernel.

Flash. BOOT only, `mmcblk0p69`, 20,920 × 4,096 bytes. The read-only
preflight matched the protected baseline (full p69 `513a0e38…`, prefix
`c6e23141…`, RECOVERY/EFS/sec_efs `14e11370…`/`51fa824c…`/`3a265d58…`).
The postflight full p69 is `2c04b63b…` with the image prefix `fec0f921…`
and the three protected partitions unchanged. The exact `tail` field was
not separately re-read; the write covered exactly 20,920 blocks and the
full partition hash is recorded. `flash-record.json` in the gitignored
evidence directory is `result=pass`.

Boot. One owner-controlled boot is pending; the image must not be rebooted
from adb. The report to capture is the `BULK`, `BULKIO`, `SPEEDIO` and
`BULKFAIL` set. The S2.1 verdict, the brr/drain split and the fix decision
remain `[U]` until that boot.

## 15. S2.1 run (2026-11-13)

One owner boot of `fec0f921…`; the boot budget is consumed. The run ended
in the controlled 4,000 ms stream-budget exit, not an I/O failure:

| Line | Values |
| --- | --- |
| `BULK` | `region=85688320 chunks=280 bytes=9175040 ticks=77015710 hz=19200000 seed=47544134 windows=80 wpass=0 wfail=0 woffset=0 digest=1ec4d0cebc23eac4e64411f98ba9dfc8` |
| `BULKIO` | `brr=6003260 drain=57138691 polls=22839 delays=22559 sha=13203697 hc=20 cc=0007 ps=03f800f0 cap=0000808f3029c8b2 dll=000000006000642c ven=0262604000018a9c rcg=000000000000020b rcg2=0000000000000000` |
| `SPEEDIO` | `req=280 blk=17920 inh=13819 cmd=17799 dat=63217789 stop=136769 core=63435519 bus=63724784 io=63809003 win=0 hz=19200000` |
| `BULKFAIL` | `stage=6 status=0 bytes=9175040 chunks=280 window=0 woffset=0 cancel=0 budget=1` |

The host SHA-256 of the first 9,175,040 bytes of the final image is
`1ec4d0cebc23eac4e64411f98ba9dfc81ee7c9fd3cbf383ce1800a8d0357d881`,
matching the report prefix exactly. The width/HS revert is verified
(`rverified=1 hsverified=1 rclk=400000`); `cfail=0`, `sfail=0`, `fails=0`.
The window phase never started.

Measured split:

| Item | Ticks | Time | Per 512-byte block |
| --- | ---: | ---: | ---: |
| CMD18 data phase | 63,217,789 | 3.293 s | 183.7 µs |
| BRR wait | 6,003,260 | 0.313 s | 17.4 µs |
| FIFO drain | 57,138,691 | 2.976 s | 166.1 µs |
| SHA-256 | 13,203,697 | 0.688 s | — |
| Stream loop | 77,015,710 | 4.011 s | — |

The drain is **90.4% of the data phase** and **1.30 µs per 32-bit FIFO
read**. The 10 µs delays account for 0.226 s (22,559 steps); the
`polls=22,839` for 17,920 blocks leaves about 4,639 extra iterations. The
BRR wait is small relative to the drain, which points at the host MMIO read
as the bottleneck rather than the card: a 50 MHz card would be ready well
before each 166 µs drain completes. The stream rate is 3.08 MB/s.

Register snapshot. `hc=20` is the 8-bit host width, `cc=0007` has the
internal clock enable/stable and card clock enable bits, and `rcg=0x20B` is
the selected 50 MHz floor row (source select 2, divider field 11, M/N/D
zero). These confirm the programmed state but do not measure the parent
rate; the 50 MHz parent remains a table value `[C]`. The 3.08 MB/s stream
rate is therefore not evidence of a low card clock while the host drain is
the limiting term.

Verdict: **INCOMPLETE (controlled stream-budget exit)** with a verified
9,175,040-byte prefix. The S2 success predicate is not met, and the next
fix targets the FIFO drain, not the clock/width path.

## 16. S2.2 fix plan from the S2.1 split

The evidence selects H2 (per-word MMIO drain cost). In order:

1. Drain every block that is already ready before taking the 10 µs poll
   delay. The S2.1 delay term is 0.226 s; this is a small but certain win.
2. Store each 32-bit FIFO word directly into the destination and inline the
   SDHCI data-port read, so the drain loop carries no byte-wise store or
   generic-space branch per word.
3. Add a direct drain calibration before the stream: read the data port
   128 times in a tight loop with the FIFO idle and report the ticks in a
   new `DRAINCAL` line. This separates the host MMIO cost from the card
   pacing without another boot shape.
4. If the calibrated drain is still near 166 µs per block, implement ADMA2
   or SDMA as its own rung; DMA is unqualified and needs its own host gates
   and a separate owner approval.
5. Only then re-size the stream/total budgets from the measured rate with
   margin under the 30,000 ms watchdog, or shrink the region if it cannot
   fit at all.

The full region remains the S2 claim. No budget change is made before the
S2.2 measurement.

S2.2 host status (2026-11-13). The CMD18 loop now drains every block that
is already ready before taking the 10 µs poll delay, stores each FIFO word
directly as a 32-bit value, and re-checks the error status before each
additional block. `gta4l_emmc_calibrate_drain()` times 128 data-port reads
with the FIFO idle after a clean or budget-exit stop; the result is the new
`cal` field in the `BULKIO` line. The line is 250 bytes and the combined
bulk worst case is 2,523 bytes inside the 2,560-byte buffer. Host results:
core 214 scenarios/132 mutations, block 47 core scenarios/35 mutations,
443 adapter scenarios and 171 adapter + 3 support + 37 policy mutations.
No candidate or boot is produced by this host status; the candidate build
and one owner boot are next. If `cal` is still near the S2.1 166 µs per
block, the drain is MMIO-bound and ADMA2/SDMA becomes its own rung; if
`cal` is small, the earlier 3.08 MB/s figure is card pacing and the
clock/width path is re-opened.

S2.2 candidate and flash (2026-11-13). Kernel
`56ec4c1c3dd810ac0452b90a91d35dea2e06adf22b05f1a012c5d92fc722512b`,
storage-ABI `out/gta4l/storage-abi-56ec4c1c3dd8`, linked archive
`bea6c8c4…` and requalified archive `e3d9bb9e…` with byte-identical member
objects (`db51cdb4…`, `4ccd51cb…`, bindings `3e53b66d…`). Payload
`ba6875e8…`, BOOT image `cbc0ee1e…` (18/18 against v5 `ce4d76e8…`, only
the ramdisk differs), xz `af518f36…`. BOOT-only p69 write: preflight full
p69 `2c04b63b…` matched the S2.1 post state; postflight full p69
`5e7028cf…`, image prefix `cbc0ee1e…`, RECOVERY/EFS/sec_efs unchanged.
The tail field was not separately re-read; the write covered exactly
20,920 blocks. Flash record `result=pass`; one owner boot is pending and
the image must not be rebooted from adb. The `cal` value and the S2.2
verdict remain `[U]` until the report.

## 17. S2.2 run and verdict (2026-11-13)

One owner boot of `cbc0ee1e…`; the boot budget is consumed. The run ended
in the controlled 4,000 ms stream-budget exit, not an I/O failure:

| Line | Values |
| --- | --- |
| `BULK` | `region=85688320 chunks=292 bytes=9568256 ticks=76808842 hz=19200000 seed=47544134 windows=80 wpass=0 wfail=0 woffset=0 digest=34912093eeaa32334791b2fad6f7c757` |
| `BULKIO` | `brr=2486257 drain=59813569 cal=3154 polls=5399 delays=5107 sha=13800789 hc=20 cc=0007 ps=03f800f0 cap=0000808f3029c8b2 dll=000000006000642c ven=0262604000018a9c rcg=000000000000020b rcg2=0000000000000000` |
| `SPEEDIO` | `req=292 blk=18688 inh=14616 cmd=18943 dat=62382301 stop=142745 core=62610149 bus=62914925 io=63004658 win=0 hz=19200000` |
| `BULKFAIL` | `stage=6 status=0 bytes=9568256 chunks=292 window=0 woffset=0 cancel=0 budget=1` |

The host SHA-256 of the first 9,568,256 bytes of the final image is
`34912093eeaa32334791b2fad6f7c757373b5b871cff893aa728550deb2bc42b`,
matching the report prefix exactly. The width/HS revert is verified
(`rverified=1 hsverified=1 rclk=400000`); `cfail=0`, `sfail=0`, `fails=0`.
The window phase never started.

Measured split:

| Item | Ticks | Time | Per 512-byte block |
| --- | ---: | ---: | ---: |
| CMD18 data phase | 62,382,301 | 3.249 s | 173.9 µs |
| BRR wait | 2,486,257 | 0.129 s | 6.9 µs |
| FIFO drain | 59,813,569 | 3.115 s | 166.7 µs |
| SHA-256 | 13,800,789 | 0.719 s | — |
| Stream loop | 76,808,842 | 4.000 s | — |

The drain is **95.9% of the data phase** and **1.30 µs per 32-bit FIFO
read**. The new `cal` field times 128 idle data-port reads in 3,154 ticks,
or **1.28 µs per read**, about the same as the in-stream drain cost per
block. The host MMIO read latency is therefore the bottleneck, not card
pacing. The loop fix worked as intended: `polls` fell from 22,839 to
5,399, `delays` from 22,559 to 5,107, the BRR wait from 0.313 s to
0.129 s, and bytes per budget rose 4.3% (9,175,040 → 9,568,256). The
stream rate is 2.39 MB/s end to end.

Projection. The full region by PIO takes 85,688,320 / 2.39 MB/s ≈ 35.8 s,
already above the 30,000 ms watchdog before the 80 windows; the drain
alone is ≈ 27.9 s (167,360 blocks × 166.7 µs). Budget changes cannot make
S2 pass on PIO.

Corrections to earlier entries:

- The §15 (and `docs/emmc-driver.md` §5.3.109) "3.08 MB/s stream rate" is
  the drain-only rate. The S2.1 end-to-end rate is 2.29 MB/s
  (9,175,040 bytes / 4.011 s).
- `DRV online=0` is expected: bulk mode clears `online` on any non-clean
  stage, and the budget exit is stage 6 (`Gta4lEmmcBlock.cpp:9716`); a
  failed revert also clears it (`:9721`). Not a pre-condition for DMA.
- The `cal` path shows the `ops->read32` call overhead is not the cost;
  the MMIO transaction itself is. A Normal-NC/write-combine mapping is
  rejected because the CPU could read speculatively or merge FIFO reads,
  and every read pops data.

Baseline. Post-flash BOOT full p69 is `5e7028cf…` with image prefix
`cbc0ee1e…`; RECOVERY/EFS/sec_efs remain
`14e11370…`/`51fa824c…`/`3a265d58…`. The partition tail and last 1 KiB
were not separately re-read, so the saved baseline is refreshed only
after a read-only TWRP re-read before the next flash.

Verdict: **INCOMPLETE (controlled stream-budget exit)** with a verified
9,568,256-byte prefix, no I/O error and a verified revert. As a
measurement it is conclusive: PIO is MMIO-bound and cannot meet the S2
full-region claim; polled 32-bit ADMA2 behind a read-only SMMU gate is the
next rung. Do not boot `cbc0ee1e…` again. See `docs/emmc-driver.md`
§5.3.111.
