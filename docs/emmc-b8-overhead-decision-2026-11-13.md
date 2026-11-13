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