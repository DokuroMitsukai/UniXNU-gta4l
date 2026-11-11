# gta4l N10.24 Track C broader 4-bit coverage disposition (host-only, 2026-11-12)

**Status:** host-only feasibility disposition, 2026-11-12. This batch
performed no device access, flash, boot, restore, mount, write, driver
change, candidate build, QEMU run, commit or push beyond the separately
authorized N10.23 closeout commit. It inventories the delivered 1-bit sweep
and states what broader 4-bit coverage would require. It does not authorize
implementation, a candidate, a device run, a commit or a push. N10.20
remains FAIL and N10.23 remains a bounded PASS; neither is reinterpreted.

## 1. Scope

N10.11/N10.13/N10.14 delivered a bounded six-range BOOT-partition sweep at
**1-bit** SDR/400 kHz. This note does not repeat that work and does not
reinterpret it as 4-bit coverage. It answers only:

> What would broader 4-bit coverage require beyond the N10.23 four-case
> widthhold read, and which parts are feasible under the current watchdog,
> report and observer contracts?

## 2. Delivered 1-bit inventory

The N10.13 candidate and N10.14 run used the `gta4l-emmc-sweep=1` mode with
the default 1-bit path (`wid=0` in the N10.14 `REPORT` line). Six cases read
the pinned BOOT partition:

| index | kind | start byte | length | blocks | expected subrequests | expected max |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 0 | anchor A0 | 4,096 | 4,096 | 8 | 1 | 8 |
| 1 | anchor A1 | 4,096 | 16,384 | 32 | 4 | 8 |
| 2 | R0 | 26,382,336 | 16,384 | 32 | 4 | 8 |
| 3 | R1 | 52,699,136 | 8,192 | 16 | 2 | 8 |
| 4 | R2 | 78,999,552 | 4,096 | 8 | 1 | 8 |
| 5 | R3 | 4,673,536 | 4,096 | 8 | 1 | 8 |

Totals: 53,248 requested bytes, 104 blocks, 13 driver-level case
subrequests, maximum subrequest 8 blocks, 63 two-block CMD18 chunks over the
whole boot, each with one successful CMD12 stop. The mode advertises
8 blocks / 4,096 bytes / one segment, so the family splits the 32-block
cases into four 8-block subrequests. The anchors are content-pinned
(`5878bcf8…` and `fa43aa5d…`); the four runtime windows compare the media
path against a direct CMD17 reference.

N10.14 returned `SWEEP: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6
bytes=53248 span=53248 blocks=104 subrequests=13 max=8 foreign=0 fails=0
cancel=0 budget=0 media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0
limit=8 seg=1 cap=61071360` on one cold boot. Its worst-case design bound is
27,700 ms = media wait 20,000 + partition deadline 2,000 + case-loop budget
4,000 + one overrunning case 1,700, below the 30,000 ms watchdog.

## 3. Current 4-bit evidence

N10.23 exercised, through the real published-partition path:

- four tagged **2-block** media subrequests at offsets 0/1,024/2,048/3,072
  (the first 4,096 bytes of BOOT);
- one 1-bit reference capture of the same 8 blocks in four 2-block chunks;
- one verified 4-bit switch and one verified revert;
- `max=2`, `subrequests=4`, `c18=4`, `stops=4`, no foreign traffic.

The block core always splits a multi-block request into 2-block CMD18 chunks
with a mandatory stop per chunk (`package/Gta4lEmmcBlockCore.h:25`–`:27`,
`package/Gta4lEmmcBlockCore.c:130`–`:200`). The chunk shape is therefore not
the gap. The gaps are:

- driver-level subrequests larger than 2 blocks at 4-bit;
- more than 4,096 bytes or four cases at 4-bit;
- reference and observer attribution across multi-subrequest cases;
- a source-derived 4-bit per-case timing bound (N10.23 recorded no elapsed
  time);
- failure-path 4-bit evidence and a 4-bit equivalent of the 1-bit anchor
  known-answer control.

## 4. Feasibility disposition

### 4.1 Stage C2 — one 8-block 4-bit subrequest (conditionally feasible)

The smallest expansion is to keep the existing 4,096-byte reference window
and read it as **one 8-block driver subrequest** at 4-bit (four 2-block
CMD18 chunks, four stops) instead of four 2-block subrequests. This
exercises the 8-block subrequest shape the 1-bit sweep used and matches the
existing reference bytes.

It is not ready for implementation. The current conservative report bound
charges 4,500 ms for one **2-block** in-flight chunk. An 8-block case is four
chunks, so a worst-case charge of 18,000 ms would give 8,000 + 2,000 + 3,000
+ 18,000 + 2,000 + 8,220 = 41,220 ms, above the 30,000 ms watchdog. A C2
design must therefore derive a tighter per-case bound from the source
command overhead and measured 4-bit timing (the 1-bit sweep used a derived
1,700 ms per case rather than the timeout edge), or reduce the report set.
No measurement exists yet; the N10.23 report has no elapsed field. C2 is
feasible only with that new derivation, and it remains a separate
design/candidate/hardware gate.

### 4.2 Stage C3 — 16-block and 32-block 4-bit subrequests (NO-GO today)

C3 would add the R1 (16-block) and R0 (32-block) windows at 4-bit with new
1-bit references and new observer expectations (two and four 8-block
subrequests under an 8-block advertisement, or single larger subrequests
under the widthhold 64-block advertisement). Under the current conservative
per-chunk charge, even one 32-block case is 16 chunks × 4,500 ms = 72,000 ms
of worst-case in-flight time, far above the watchdog. The 1-bit sweep fit
because it used a derived 1,700 ms case bound and a 4,000 ms total budget
for 53,248 bytes; no 4-bit equivalent has been derived and no 4-bit timing
has been measured. C3 is **NO-GO** until C2 supplies a defensible 4-bit
per-chunk/ per-case timing model and the owner accepts a new report bound or
a multi-boot staging.

### 4.3 Stage C4 — full six-range parity at 4-bit (NO-GO today)

C4 would repeat all six 1-bit ranges at 4-bit. It multiplies the C3 timing
and observer problem by the full 53,248-byte set and would need new
references for R0–R3, new anchor handling, and either a much larger report
bound or several separately budgeted boots. It is **NO-GO** under the
current watchdog and report contract.

### 4.4 Constraints that apply to every stage

- Reads remain within BOOT; no whole-media scattered read, no other
  partition, no write, mount or root switch. Larger BOOT ranges still
  contain public boot-image content only, and tracked evidence stays
  digest-only.
- The observer must be extended with exact expected subrequest sequences for
  any new case shape; a tag-0 request that does not match the next expected
  range remains foreign and fails the run.
- The 4-bit path keeps the verified pre-report revert and the read-only
  contract; a 4-bit failure must stop on the first non-pass with the
  decommission/notification questions in
  `docs/emmc-n10.24-widthpub-design-2026-11-12.md` §3.4.
- The watchdog is not extended. A stage that cannot fit 30,000 ms with a
  source-derived bound is not feasible as a single run.

## 5. Verdict

**NO-GO for implementation of broader 4-bit coverage.** The delivered
six-range sweep is 1-bit evidence only. C2 is a conditionally feasible next
design, but only after a source-derived 4-bit timing bound and measured
elapsed data exist; C3 and C4 are NO-GO under the current watchdog/report
contract. This note does not authorize a C2 design or candidate; the next
step is owner review, and any C2 batch needs its own host-only design gate.

## 6. References

- `docs/emmc-n10.11-bounded-read-sweep-design-2026-11-09.md`
- `docs/emmc-n10.13-sweep-candidate-2026-11-09.md`
- `docs/emmc-n10.14-sweep-hardware-run-2026-11-10.md`
- `docs/emmc-n10.17-sustained-width-read-design-2026-11-11.md`
- `docs/emmc-n10.22-widthhold-candidate-production-2026-11-12.md`
- `docs/emmc-n10.23-widthhold-hardware-run-2026-11-12.md`
- `docs/emmc-n10.24-widthpub-design-2026-11-12.md`
- `docs/emmc-driver.md`
- `package/Gta4lEmmcBlockCore.c`, `package/Gta4lEmmcBlockCore.h`,
  `package/Gta4lEmmcBlock.cpp`, `package/Gta4lEmmcBlock.h`
- `porting.toml`
