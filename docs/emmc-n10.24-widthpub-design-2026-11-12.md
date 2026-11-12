# gta4l N10.24 bounded 4-bit published-media read design (host-only, corrected 2026-11-12)

**Status:** host-only design draft, corrected on 2026-11-12 after owner review.
This batch performed no device access, flash, boot, restore, mount, write,
driver change, candidate build, QEMU run, commit or push beyond the separately
authorized N10.23 closeout commit. It proposes a bounded published-media read
qualification at the existing 400 kHz for owner review. It does **not**
propose or authorize a long-lived 4-bit publication mode. It does not
authorize implementation, candidate production, a device run, a commit or a
push. Implementation, candidate production, hardware protocol and any boot
are separately gated.

**N10.24 implementation amendment (2026-11-12):** the owner accepted this
corrected scope, including the two-pass read set under one shared 3,000 ms
case-loop budget, the `gta4l-emmc-widthpub=1` token and report names, the
`pub` publication marker and the report-thread offline-notification delta.
The host implementation batch is recorded in `docs/emmc-driver.md` §5.3.98
and adds `scripts/check-gta4l-widthpub-evidence.py`. No candidate build,
ABI/packaging gate, device access, flash or boot is authorized by this
amendment; candidate production and any hardware run remain separately
gated.

## 1. Scope and question

N10.23 already observed, on one bounded boot, the production driver publishing
the whole media and the BOOT partition while the card and host were at 4-bit
SDR/400 kHz, serving four tagged two-block reads through the opened published
partition, matching a same-boot 1-bit reference byte-for-byte, and completing
a verified CMD6 revert to 1-bit before the terminal report. Publication at
4-bit during that boot therefore happened; it is not the missing result.

What remains unqualified is **sustained or general operation at 4-bit and its
lifecycle**, not the act of publishing the media. N10.23 covered four
two-block requests over one 4,096-byte window, one boot, no failure path and
no time beyond the report window.

The question for this rung is therefore:

> Can an opt-in mode publish the media and partitions at 4-bit SDR/400 kHz
> after initialization, serve a bounded repeated read set through the
> published partition at 4-bit, match a same-boot 1-bit reference, and then
> complete a verified revert to 1-bit before its terminal report, without
> changing the read-only contract, clock, request limits or failure
> semantics?

This rung deliberately keeps the proven N10.23 shape: **verified pre-report
revert**. It makes no claim that leaving the volatile card width at 4-bit
after the terminal report is safe, and it does not treat a TWRP return as a
power-cycle or width-reset proof. A genuine long-lived publication mode
requires a separate, source-grounded stop/shutdown/error/quiesce contract;
that contract does not exist today (see §3.7).

Clock (400 kHz), CMD18 chunk shape for the existing read path, DMA/interrupt
policy, request limits, the read-only contract and the absence of
writes/mount/root switching are unchanged.

## 2. Evidence basis

### 2.1 Measured facts this rung builds on

- H15–H17 (`docs/emmc-driver.md` §5.3.31–§5.3.34): verified 4-bit switch,
  4-bit content pass, verified revert and block-0 recovery read at 400 kHz.
  BUS_WIDTH[183] is W/E_P and is never used as switch proof.
- N10.16 readiness audit `docs/emmc-n10.16-4bit-sdr-readiness-2026-11-10.md`:
  the helpers exist in the linked core; the safe insertion point is after
  `initializeHardware()` succeeds and before the baseline read and
  `registerService()`; failure/rollback/DAT_INHIBIT behavior is proven at the
  probe level; CMD19/CMD14 is excluded from the normal storage path.
- N10.16 width4 hardware observation
  `docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md`: the
  `gta4l-emmc-width=4` mode passed its complete=1 guard for a verified
  switch, byte-exact two-read comparison and verified revert on one cold
  boot. Its `MEDIA whole=0`/`PART count=0` fields are not failure flags and do
  not establish publication.
- N10.17 design `docs/emmc-n10.17-sustained-width-read-design-2026-11-11.md`,
  N10.21 correction
  `docs/emmc-n10.21-widthhold-partdiag-candidate-2026-11-11.md` and N10.22
  candidate
  `docs/emmc-n10.22-widthhold-candidate-production-2026-11-12.md`: the
  `gta4l-emmc-widthhold=1` mode captures a 1-bit EXT_CSD baseline and an
  8-block/4,096-byte 1-bit BOOT reference in four two-block chunks, switches
  to 4-bit before the inherited baseline read and advertisement, discovers
  the whole media and the pinned BOOT partition, opens the partition as a
  read-only client, runs four tagged two-block reads at offsets
  0/1,024/2,048/3,072, compares every byte against the reference and reverts
  with a verified CMD6 before the terminal report. Its success guard already
  requires `revert=1 rverified=1`.
- N10.23 hardware run
  `docs/emmc-n10.23-widthhold-hardware-run-2026-11-12.md`: one bounded
  **PASS** — `cases=4 passed=4 failed=0 complete=1 match=1 mismatches=0
  fails=0 foreign=0 cancel=0 budget=0 partdiag=0`, `refdigest=obsdigest=
  417cdef0…`, `WIDTHHOLDREAD digest=predigest=5f70bf18…`,
  `partbase=1154744320 partsize=100663296 partro=1`, verified switch and
  verified revert. The whole media (`whole=1 base=0 size=31268536320
  content=GUID_partition_scheme writable=0`) and the BOOT partition existed
  and served reads at 4-bit in that boot. Post-boot integrity was unchanged.
  N10.20 remains **FAIL** and is not reinterpreted.

The N10.23 report contains no elapsed-time or media-ready-latency field, so
no measured timing is available from it.

### 2.2 Current source paths

| Item | Current location |
| --- | --- |
| `gta4l_emmc_read_ext_csd()` | `package/Gta4lEmmc.c:1908` |
| `gta4l_emmc_set_bus_width_verified()` | `package/Gta4lEmmc.c:2320` |
| `gta4l_emmc_set_bus_width_recover()` | `package/Gta4lEmmc.c:2341` |
| `gta4l_emmc_bus_test_verified()` (not used) | `package/Gta4lEmmc.c:2655` |
| Widthhold start sequence (baseline, reference, switch) | `package/Gta4lEmmcBlock.cpp:1807`–`:1886` |
| Inherited LBA-1 baseline read | `package/Gta4lEmmcBlock.cpp:1888` |
| Advertisement properties | `package/Gta4lEmmcBlock.cpp:1936`–`:1968` |
| `registerService()` (publication) | `package/Gta4lEmmcBlock.cpp:1971` |
| Whole/partition discovery | `package/Gta4lEmmcBlock.cpp:6804` |
| Two-block tagged case read | `package/Gta4lEmmcBlock.cpp:6958` |
| Async-read failure offline notification | `package/Gta4lEmmcBlock.cpp:2313`–`:2316` |
| Report-thread failure state and revert | `package/Gta4lEmmcBlock.cpp:7482`–`:7551` |
| Verified revert path | `package/Gta4lEmmcBlock.cpp:7171`, `:7506` |
| Report assembly and success guard | `package/Gta4lEmmcBlock.cpp:7553`–`:7700` |

### 2.3 What is still not established

- Sustained or repeated reads at 4-bit beyond the single N10.23 four-case
  pass, and any 4-bit request larger than two blocks.
- Publication lifecycle beyond the bounded report window: stop, shutdown,
  panic, in-flight quiescence and terminal ownership for a mode that would
  leave 4-bit active. No source-grounded contract exists.
- A physically observed power-on reset from a 4-bit-held state on this
  device. JEDEC's volatile BUS_WIDTH default is a specification fact, not a
  measured device result.
- Offline decommissioning for report-thread-detected failures. The async read
  path already notifies clients (see §3.4); the report-thread path only
  clears `online`, and no test establishes client-visible decommissioning
  there.
- Failure-path 4-bit evidence: no 4-bit read, short-transfer, stop or digest
  failure has been injected on hardware.
- Repeatability of the N10.23 result across boots (separate protocol,
  `docs/emmc-n10.24-widthhold-repeatability-protocol-2026-11-12.md`).
- Initialization reliability, arbitrary I/O, writes, mount/root and any
  higher clock or wider bus; all remain `[U]` or deferred.

## 3. Proposed bounded mode contract (working token)

Working token: `gta4l-emmc-widthpub=1`. The final token and report names are
an owner decision; `widthpub` is used here only for clarity. The new mode
must not alter the absent-token legacy path, `gta4l-emmc-width=4` or
`gta4l-emmc-widthhold=1`. The token is provisional and the mode must not be
implemented until the owner accepts the corrected scope in §6.

### 3.1 Token and selection

- Requires exactly one `gta4l-emmc-block=3` token. Refuses an orphan or
  duplicate block token, block values other than 3, duplicate or malformed
  widthpub tokens, widthpub values other than 1, and every combination with
  `partobs`, `partcmp`, `partread`, `deblock`, `breakup`, `sweep`, `consume`,
  `width` and `widthhold`. Selection and refusal follow the existing
  widthhold selector and diagnostic-payload exclusivity checks.
- Absent token: byte-for-byte unchanged legacy behavior.

### 3.2 Start path (pre-watchdog)

1. `initializeHardware()` has succeeded; `host.initialized == 1` and the
   block core is ready. No report/watchdog thread exists yet.
2. Under `gate`: read the 1-bit EXT_CSD baseline (512 bytes); read the pinned
   8-block/4,096-byte BOOT window at 1-bit through the block core; require
   exact counts and a clean stop; compute the reference digest.
3. Under `gate`: `gta4l_emmc_set_bus_width_verified(&host, 4, &bus)`.
   Require `verified=1`, the host 4-bit readback, the read-only EXT_CSD
   properties comparison and `switch_result=OK`. On failure, fail closed with
   the recorded fields; on `ERR_WIDTH_RESTORE`, stop without further reads
   and without an extra recovery sequence.
4. Run the inherited LBA-1 baseline read at 4-bit and the advertisement
   properties, then set `online` and call `registerService()`. The media and
   partitions are published at 4-bit; the advertisement stays 64 blocks /
   32,768 bytes / one segment and the clock stays 400 kHz.

### 3.3 Report path (bounded, watchdog-covered)

1. Discover exactly one whole media and exactly one pinned BOOT partition
   with the N10.23 checks (size, base, Writable, structural Content,
   exactly-one), recording `whole`, `partbase`, `partsize`, `partro` and the
   first-failure `partdiag` bitmask.
2. Open the BOOT partition as a read-only client exactly once.
3. Serve the proposed read set: **two passes** of the four-case set, eight
   tagged two-block reads at offsets 0, 1,024, 2,048, 3,072 (device LBAs
   2,255,360/2,255,362/2,255,364/2,255,366). Each read requires exact
   completed counts, a mandatory clean stop and a per-case digest equal to
   the reference slice. Foreign (untagged) storage traffic is a strict
   failure. The read set is provisional: a single four-case pass is the
   conservative fallback if the owner does not want the second pass.
4. The case loop uses **one total elapsed budget** covering both passes, not
   one budget per pass. The budget is checked between cases and stops issuing
   new cases when it expires. This is required for the §3.6 bound; a
   per-pass reset would exceed the watchdog and is not part of this design.
5. After the read set, whether it passed or failed, acquire `gate` with the
   bounded timeout and attempt the verified 1-bit revert. If the revert is
   proven: release the gate; on a read-set failure keep `online=false` and
   report the first failure, and on a read-set pass keep `online=1` and
   report success with `revert=1 rverified=1`. If the gate times out or the
   revert cannot be proven, mark `online=false`, retain the first failure,
   report terminal unknown framing, and do not retry or add another recovery
   sequence.
6. Release the media client and references on every exit. Report under the
   existing lock/terminal ownership; `panicWithReport()` remains the only
   terminal. There is no `held` state and no success path without a verified
   revert.

### 3.4 Decommission-on-error audit and contract

The existing async read path already has a per-request offline notification:
`doAsyncReadWrite()` sets `online=false` when the block-core read returns a
non-OK result, invokes the completion action, and then calls
`messageClients(kIOMessageMediaStateHasChanged,
(void *)(uintptr_t)kIOMediaStateOffline)` after releasing `gate`
(`package/Gta4lEmmcBlock.cpp:2313`–`:2316`). That path is the model for
client-visible decommissioning; it is not evidence that every failure class
is covered.

The report-thread path is different. The widthhold report thread clears
`online` for its own failure classes — discovery, partition, budget,
cancellation, digest mismatch after a **successful** read, gate timeout and
revert failure — but it does not itself send `messageClients`
(`package/Gta4lEmmcBlock.cpp:7482`–`:7551`). A digest mismatch is the
clearest gap: the underlying read succeeded, so the async path did not
notify, yet the driver stops serving the media. Whether the family
decommissions the media in that case is not established.

The bounded mode must therefore specify, before implementation:

- the first-failure record is latched before `online` is cleared and before
  any notification;
- any report-thread offline notification is sent after the controller gate
  is released and after the in-flight request is quiesced, never while
  holding `reportLock` or `gate`;
- the notification is emitted exactly once per terminal failure and not on
  the success path;
- the legacy 1-bit path remains unchanged.

This record does **not** claim the existing host suite covers the
report-thread notification path; the implementation batch must add that
coverage if the owner accepts a report-thread notification change.

### 3.5 Report fields and success predicate

Working success line (final names open; it reuses the established widthhold
schema and adds only a provisional publication marker):

```
GTA4L-EMMC P5 WIDTHPUB: cases=8 passed=8 failed=0 reason=0 complete=1
pub=1 switch=4 verified=1 hb=0 ha=2 rb=0 cmd6=1 refs=1 refbytes=4096
refblocks=8 refstatus=0 refchunks=4 refstops=4 refstopfail=0
refdigest=<32 hex> reads=8 bytes=8192 blocks=16 subrequests=8 max=2 c18=8
stops=8 stopfail=0 match=1 mismatches=0 fails=0 foreign=0 cancel=0 budget=0
obsdigest=<32 hex> partbase=1154744320 partsize=100663296 partro=1 partdiag=0
revert=1 rverified=1 rhb=2 rha=0 rcmd6=1 rstatus=0 limit=64 seg=1 cap=61071360
GTA4L-EMMC P5 WIDTHPUBREAD: index=7 lba=2255366 start=3072 length=1024
span=1024 actual=1024 status=0 subrequests=1 blocks=2 max=2
digest=<32 hex> predigest=<32 hex> match=1
```

`complete=1` requires, at minimum: both passes and all eight cases (or the
accepted reduced set); `obsdigest` equal to `refdigest`;
`match=1 mismatches=0 fails=0 foreign=0 cancel=0 budget=0`; `pub=1` with
exactly one whole media and one BOOT partition and a zero `partdiag`;
`switch=4 verified=1`; and `revert=1 rverified=1 rstatus=0` on the success
path. A host-side digest mismatch rejects acceptance even if the on-device
guard passed. Digest fields are 16-byte/32-hex prefixes of SHA-256,
recomputed from the final image by the host checker. The formatter-exact
line and total maxima must be recomputed in the implementation batch; the
existing widthhold report bound is 2,066 bytes and does not include a second
pass or a `pub` field.

### 3.6 Timing

All values are conditional bounds derived from the existing source
constants, not proofs. The successful path is expected to be far shorter.
The N10.24 start path is the same sequence as N10.17's, so the N10.17
pre-watchdog derivation is applicable; the obsolete N10.16 13,010 ms
two-block estimate is not used.

Pre-watchdog start path (`start()`, before the report thread and watchdog):

| Step | Bound |
| --- | ---: |
| 1-bit EXT_CSD baseline (CMD/data timeout pair) | 2,000 ms |
| 1-bit 8-block reference (4 chunks x 4,500) | 18,000 ms |
| verified 4-bit switch | 6,010 ms |
| 4-bit LBA 1 baseline read | 2,000 ms |
| **Success path before the report thread** | **28,010 ms** |
| post-switch failure recovery (gate + verified revert) | +10,220 ms |
| **Failure path before the report thread** | **38,230 ms** |

The 18,000 ms reference term is four two-block CMD18/CMD12 chunks, each
bounded by the pre-command inhibit 1,000 ms + command/data 1,000 ms +
secondary drain 500 ms + CMD12 entry 1,000 ms + CMD12 completion 1,000 ms =
4,500 ms. The 2,000 ms baseline term is the CMD/data timeout pair
(`GTA4L_EMMC_CMD_TIMEOUT_US` and `GTA4L_EMMC_DATA_TIMEOUT_US` are 1,000,000
us each); the detailed derivations are in N10.17 §5.1 and are applicable
because the N10.24 start path has the same four steps. A switch failure can
make two helper passes and reaches
2,000 + 18,000 + 16,030 = **36,030 ms**; that path fails closed without the
LBA 1 baseline, publication, case read or extra recovery sequence. These are
pre-watchdog budgets, not watchdog claims. The successful path is expected
to complete well below them; N10.23 recorded no elapsed time, so this is not
a measurement.

Report thread (watchdog-covered):

| Step | Bound |
| --- | ---: |
| whole-media wait | 8,000 ms |
| BOOT partition discovery deadline | 2,000 ms |
| one total case-loop budget for both passes (checked between cases) | 3,000 ms |
| one in-flight two-block case overrun | 4,500 ms |
| bounded gate acquisition for revert | 2,000 ms |
| verified 1-bit revert | 8,220 ms |
| **Total** | **27,720 ms** |

Margin against the 30,000 ms watchdog is 2,280 ms. The total is unchanged
from N10.23 **only because the 3,000 ms budget is shared by both passes**.
If the budget were reset for each pass, the bound becomes 30,720 ms, which
exceeds the watchdog; that variant is not part of this design. Because the
success path now includes the gate and the verified revert, the success and
failure report-thread bounds are the same 27,720 ms; the earlier 17,500 ms
success figure is withdrawn. Eight cases are expected to complete far inside
3,000 ms at 400 kHz, but the actual loop elapsed and media-ready latency are
unmeasured; the candidate must record both. The loop checks the budget
before issuing each case, and the last issued case may overrun by its own
4,500 ms bound. Cancellation does not shorten an in-flight case or the
revert, the budget is not an in-flight cancellation mechanism, and the
bounds exclude scheduler stalls; these are conditional bounds, not proofs.
A larger or slower read set needs a new bound and owner acceptance; the
watchdog is not extended.

### 3.7 Why there is no hold in this rung

A prior draft proposed leaving the volatile card BUS_WIDTH at 4-bit after
the terminal report and treating the following TWRP start as reset evidence.
That proposal is withdrawn. It does not establish a safe lifecycle:

- A TWRP start demonstrates recovery access after a new boot; it does not
  prove a physical power cycle, a particular reset command or a width reset.
  The bounded mode reverts before the terminal, so the next boot is not
  evidence about a 4-bit-held state at all.
- JEDEC's volatile BUS_WIDTH default is a specification fact; no observed
  power-on reset from a 4-bit-held state exists on this device.
- There is no source-grounded stop/shutdown/quiesce contract for a mode that
  ends with the card at 4-bit. In-flight requests, the controller gate,
  panic ordering, client notification and the next boot's initialization are
  not covered by the N10.23 evidence.
- The terminal report is on-screen only; no storage write follows, but that
  does not make the lifecycle safe.

A future long-lived publication mode is **NO-GO** until, at minimum: a
source-grounded stop/shutdown/error/quiesce contract is written and tested;
in-flight quiescence and terminal ordering are proven; a physically verified
power cycle from a 4-bit-held state demonstrates a clean 1-bit
reinitialization; report-thread decommissioning is covered; and the owner
accepts the residual risk. This rung keeps the proven pre-report revert
instead.

## 4. Host test and mutation plan

The implementation batch must add, and pass, at least:

- selector: absent-token legacy equality; valid widthpub; orphan/duplicate
  block; block 1; malformed/duplicate/other widthpub values; every existing
  mode combination refused;
- start path: baseline failure, reference failure, switch failure,
  `ERR_WIDTH_RESTORE`, host-readback mismatch, properties mismatch, gate
  timeout and pre-switch cancellation;
- publication: whole-media missing/duplicate/wrong size/writable-unknown;
  BOOT partition missing/duplicate/wrong size/base/Writable/Content absent;
  `partdiag` exact bit per refusal; exactly-one semantics;
- reads: eight-case pass with a shared total budget, per-pass digest
  mismatch, short transfer, stop failure, foreign traffic,
  order/duplicate/missing case, budget expiry, cancellation, gate timeout
  and post-switch failure with verified revert;
- success/failure contract: `revert=1 rverified=1` on both the success and
  the failure paths; no success without a verified revert; failure-path
  `online=false` and first-failure preservation;
- decommission: async-read failure notification remains exactly once and
  outside the gate; if the report-thread notification is added, prove its
  ordering, exactly-once property, quiescence and absence on the success
  path, and prove the legacy 1-bit path is unchanged;
- report: formatter-exact maximum widths, `static_assert`s and the
  worst-report budget with any added fields; legacy reports byte-for-byte
  unchanged;
- host checker: a publication-path evidence checker (or a widthhold checker
  extension) that recomputes both window digests and the last-case slice from
  the final image and rejects changed content, missing/duplicate fields,
  nonzero mismatch/foreign, and a success report without a verified revert.

## 5. Candidate, packaging and hardware gates

Candidate production needs its own approval and a fresh chain: kernel build,
candidate-specific storage-ABI audit, requalified archive, prelink, packaged
kernel, payload, BOOT image, 18/18 boot-image gate, payload contract with a
new `emmc-block-3-widthpub` personality and refusal cases, and post-flash
readback. The candidate-specific audit must not reuse an earlier audit. The
BOOT-only flash and any boot remain separately approved with an explicitly
named image, a finite boot budget, a transcription protocol, a TWRP return
and a stop-on-first-non-pass rule. Initialization reliability is `[U]`; a
pre-identification stop is an observation, not a mode failure, and the
budget must account for it.

## 6. Verdict and open items

**NO-GO for implementation.** The corrected scope is a bounded
published-media read qualification with the proven pre-report revert, but
the read set, token/report names, decommission delta and timing recomputation
are not accepted. The prior `held=1` hold mode is **NO-GO** for the missing
evidence in §3.7.

**NO-GO for candidate production or hardware** until a host-only
implementation batch passes, the owner accepts the corrected scope, the
final token and report names, the exact read set (one pass or two), the CMD6
writes and the finite boot budget, the timing bound is recomputed from the
implemented source, and the ABI/packaging gates close.

Open items for the owner:

1. Accept the bounded non-hold scope (verified pre-report revert, no
   `held` claim) as the next rung, or defer 4-bit publication work.
2. Choose one four-case pass or two passes, with the understanding that the
   two-pass variant requires one shared 3,000 ms total loop budget to stay
   under the watchdog.
3. Decide whether the report-thread failure path should emit the offline
   notification (new behavior and new tests) or whether clearing `online`
   only is accepted; document the residual client-visible uncertainty.
4. Confirm the provisional token/report names and the timing convention.
5. Repeatability is not established by this rung; the separate N10.24
   widthhold protocol covers the unchanged N10.22 image first, and any
   publication-path protocol must add its own repeated-boot acceptance later.

## 7. References

- `docs/emmc-n10.16-4bit-sdr-readiness-2026-11-10.md`
- `docs/emmc-n10.16-width4-hardware-observation-2026-11-11.md`
- `docs/emmc-n10.17-sustained-width-read-design-2026-11-11.md`
- `docs/emmc-n10.21-widthhold-partdiag-candidate-2026-11-11.md`
- `docs/emmc-n10.22-widthhold-candidate-production-2026-11-12.md`
- `docs/emmc-n10.23-widthhold-hardware-run-2026-11-12.md`
- `docs/emmc-n10.24-widthhold-repeatability-protocol-2026-11-12.md`
- `docs/emmc-n10.24-trackc-4bit-coverage-disposition-2026-11-12.md`
- `docs/emmc-driver.md`
- `package/Gta4lEmmc.c`, `package/Gta4lEmmcBlock.c`,
  `package/Gta4lEmmcBlock.cpp`, `package/Gta4lEmmcBlock.h`
- `scripts/check-gta4l-widthhold-evidence.py`
- `scripts/check-gta4l-widthpub-evidence.py`
- `porting.toml`

## 8. Host candidate production record (2026-11-12)

This record covers the host-only candidate chain. The one-boot device run is
recorded separately in `docs/emmc-n10.24-widthpub-hardware-run-2026-11-12.md`.
The production batch and this record were committed as `29a4fcd`.

Build chain. The device tree was the `b6dbe59` base with the uncommitted
N10.24 implementation batch. The first kernel build stopped at the block
archive step because it bootstrapped against a stale default storage-ABI
audit (`storage-ABI audit candidate does not match ... kernel.macho`). The
rebuilt kernel used the previous candidate-matched audit
`out/gta4l/storage-abi-04ae57f6f565` as the bootstrap and produced prelinked
candidate `e65d06259ae4f3846cab567356e6f50699acca73d4982b77c87982357c8c8b14`
(51,527,680 bytes, slide `-0x1000000`; record
`out/gta4l/ram-root-emmc-n10.24-widthpub-candidate-evidence/kernel-build-2.log`).
The candidate carries the WIDTHPUB summary/read/fail format strings, the
`gta4l-emmc-widthpub` boot argument and
`_gta4l_emmc_block_widthpub_select`.

Storage-ABI audit. `scripts/audit-gta4l-storage-interface.py` against the
candidate passed and wrote `out/gta4l/storage-abi-e65d06259ae4/`, with the
unchanged vtable/metaclass/service addresses `0xfffffff005cb44c0`,
`0xfffffff006add1d0` and `0xfffffff007b2b5b8`, 190 ordinary storage slots and
181 kernel bindings; `scripts/test-gta4l-storage-abi.py --artifacts` passes.

Archive requalification. The tree requalification against the final audit
produced `archive-requalified-tree-e65d.a`; its three members are
byte-identical to the archive linked into the kernel
(`archive-member-compare.txt`: `be2c76cd…` `Gta4lEmmcBlock.o`,
`59acca9e…` `Gta4lEmmcBlockCpp.o`, `3e53b66d…` `original-bindings.o`). The
container hashes differ (`0b8b99e0…` linked vs `9eff92b8…` requalified)
only in `ar` member metadata (uid/gid and symbol-table form), the documented
`ar` path dependence; the requalified report binds the current candidate and
audit (`archive-requalified-tree-report-e65d.json`).

Payload and BOOT. The `emmc-block-3-widthpub` payload
(`gta4l-emmc-block=3 gta4l-emmc-widthpub=1`) built from the candidate and the
fs-timer-hold raw ramdisk is
`c852dc808020a8fabea06ffe12aad117d0ee20559dd08725e35d88e821e1d22a`
(83,886,080 bytes); its contract passes 78/78, including the widthpub
value-must-be-1, block-3 requirement and every widthpub/width/widthhold
exclusivity check. The
packaged BOOT image is
`727595944324f5a439e4fe9ec1b3fddc2da979d731a4f9589d7d12013e78d922`
(85,688,320 bytes); the component gate passes 18/18 with
`only_ramdisk_differs=true`, and the explicit `check-gta4l-boot-image.py` run
against the expected payload digest also passes.

Digest binding and checker. The final BOOT window prefix (offset 0, 4,096
bytes) is `2469ee44e806df84cd1464ddc959d3ea` and the last-case slice prefix
(offset 3,072, 1,024 bytes) is `5f70bf18a086007016e948b04aed3b82`.
`scripts/check-gta4l-widthpub-evidence.py --self-test` passes, and the
checker accepts a clearly labelled synthetic success report generated from
the final packaged image (`widthpub-synthetic-success-report.txt`) and rejects
the checker's changed-image/malformed cases. That fixture is a host
verification aid, not a device observation.

Host suites. The diagnostic payload builder (48 PASS), packager (19 PASS),
packaging, inventory, evidence-count and RAM-root recipe checks pass; the
block suite passes 48 core scenarios with 38 mutation controls and 370
adapter scenarios, and the storage-ABI test and the widthpub checker log
into the same evidence directory, which also carries the refreshed
`docs/evidence-baseline.md` row hashes for the changed inputs. Raw logs,
manifests and the private raw evidence are in
`out/gta4l/ram-root-emmc-n10.24-widthpub-candidate-evidence/`
and `out/gta4l/storage-abi-e65d06259ae4/`.

Status. Host-qualified candidate with a bounded hardware PASS. The one
owner-approved BOOT-only flash of image `72759594…` and the single
owner-triggered cold boot returned the success shape: `cases=8 passed=8
complete=1 pub=1`, matching window digests `2469ee44…`/`5f70bf18…`,
`partdiag=0`, `revert=1`, `rverified=1` and no `WIDTHPUBFAIL`. The one-boot
budget is consumed. The bounded publication read passed on this boot; general
4-bit publication, sustained/arbitrary I/O, mount/write, repeatability and
initialization reliability remain `[U]`. Any further boot requires a new
explicit owner approval and a fresh finite budget.
