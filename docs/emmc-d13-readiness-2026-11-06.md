# gta4l D13 readiness: grounded publication seam and proposed CMD18 contract

**Status:** host-only readiness/design record, 2026-11-06. This document
records the pre-implementation D13.1 grounding and D13.2 scenario enumeration.
That readiness batch performed source and interface inspection and
documentation only; it did not implement a driver, formatter or test change,
build an archive, kernel, payload or image, launch QEMU or regenerate
storage-ABI artifacts, access/flash/boot the device, or commit/push. The owner
then selected N2 option 1 and the N3 host implementation followed; it is
recorded in `docs/emmc-driver.md` §5.3.48 and summarized in §2.7 and §9 below.
This document remains the readiness record and is not itself a candidate,
hardware result or reliability disposition.

## 1. Source identity and evidence

- Device HEAD `1ee1f4f9143b` with the existing uncommitted D12.5/D12.6
  closeout batch; shared HEAD `0a1cc71407a3`.
- No tracked source was modified by this batch. The pre-edit hash baseline is
  in the private evidence directory
  `out/gta4l/ram-root-emmc-d13-readiness-evidence/` (`inventory.txt`,
  `repo-status.txt`, `device-working-tree.diff`, `source-hashes-pre-edit.txt`).
- The accepted p5b/F5 and D10.5/F2 verdicts are unchanged; see
  `docs/gta4l-storage-publication-design.md` section 5 and
  `docs/emmc-driver.md` sections 5.3.37 and 5.3.41.

## 2. D13.1 — the current contract as inspected

### 2.1 XNU adapter seam

`package/Gta4lEmmcBlock.cpp`:

- `doAsyncReadWrite` (`:501`) rejects before acceptance, without calling the
  completion: null buffer/completion/action (`:513-516`), zero blocks
  (`:517-519`), out-of-range block (`:520-525`), more than 64 blocks
  (`:526-528`), short descriptor (`:529-532`), write direction (`:533-535`),
  non-read direction (`:536-538`) and an absent gate (`:539-541`). The
  portable `gta4l_emmc_block_request_check` repeats the range, direction and
  offline checks (`:543-554`).
- An accepted request takes `gate` (`:556`), handles an already-offline state
  under the lock with a `kIOReturnNoMedia` completion after unlock
  (`:557-562`), sets `requestBuffer` (`:563`), calls the portable core
  (`:565-566`), clears `requestBuffer` (`:567`), marks `online=false` on
  failure under the lock (`:568-571`), unlocks (`:572`), maps the result,
  invokes exactly one completion (`:574-598`), and only then sends
  `kIOMessageMediaStateHasChanged`/offline (`:599-602`). Every accepted
  request gets exactly one completion; immediate rejections get none.
  Completion and offline notification run outside the gate.
- `readBlockCallback` (`:239-246`) delegates to `gta4l_emmc_read_block`, one
  CMD17 sector into the caller's buffer. `copyOutCallback` (`:248-263`)
  returns 0 when `requestBuffer` is null and otherwise uses
  `requestBuffer->writeBytes(offset, ...)`.
- Advertised limits are 64 blocks / 32,768 bytes / one segment
  (`:427-440`), enforced again at `:526-532`. Capacity is
  `host.capacity_blocks` (`:334`); block size is 512, the maximum valid block
  is `capacity-1` (`:672-680`), and the media is non-writable, non-removable
  and non-ejectable.
- Mode selection (`package/Gta4lEmmcBlock.c:63-78`) accepts only
  `gta4l-emmc-block=1` as report mode; value 2 and malformed values are
  explicit refusals. A probe-family token, another SDHCI diagnostic or a
  duplicate token is refused before MMIO
  (`package/Gta4lEmmcBlock.cpp:354-392`). The report thread and the
  30,000 ms watchdog are at `:442-472` and `:1087-1148`; the report panic is
  `:901-915`.
- There is no CMD18 or `gta4l_emmc_read_multi` call in the publication adapter
  or the portable core. The adapter's only transport calls are the
  baseline/report CMD17 reads (`:245`, `:409`).

### 2.2 Portable core

`package/Gta4lEmmcBlockCore.c:50-148`:

- It validates direction, count, range, the 64-block ceiling, buffer length
  and `online`; rejected requests do not increment counters (`:56-95`).
- An accepted request increments `requests`/`max_request_blocks` (`:97-100`),
  then loops one block at a time: `read_block` into the single 512-byte
  bounce (`:106-107`), `copy_out` at `index * 512` (`:120-122`), and
  `blocks++` only after a full 512-byte copy (`:144`).
- A callback failure sets `online=0`, increments `failures`, sets
  `last_error=IO_ERROR`, latches `first_fail_*` once and returns `IO_ERROR`
  (`:108-119`). A short or overclaimed copy is capped at one block for
  `completed_bytes` but still sets `online=0`, records `SHORT_COPY` and fails
  (`:123-143`).
- `MAX_BLOCKS=64` and `MAX_BYTES=32768`
  (`package/Gta4lEmmcBlockCore.h:22-24`); the result enum is
  `OK/BAD_ARGUMENT/NOT_WRITABLE/NO_MEDIA/TOO_LARGE/IO_ERROR/SHORT_COPY`
  (`:26-34`).
- The adapter never calls `gta4l_emmc_block_set_online`; after a failure the
  core stays offline until re-initialization, which the adapter does not
  perform.

### 2.3 Diagnostic CMD18/CMD12 machinery

`package/Gta4lEmmc.c`:

- `gta4l_emmc_read_multi` (`:3658-3971`, declared in `Gta4lEmmc.h:683`)
  checks arguments (`:3679-3690`), resets the `multi_*` state only after those
  checks (`:3692-3700`), issues CMD18 with the block count and multi-block
  transfer mode (`:3701-3725`), polls with a 1,000,000 us data bound
  (`:3728`), writes each 512-byte sector **directly into the caller's
  buffer** (`:3761-3796`), and on a bound expiry runs a separate 500,000 us
  drain (`:3825-3944`).
- `gta4l_emmc_send_stop` (`:3528-3655`) is always called after the transfer
  loop (`:3966`). Success requires both CC and TC, no error bits and no R1
  error bits (`:3601-3614`, `:3637-3651`).
- The return combines the first data failure with the stop only when the data
  result is still OK (`:3967-3969`). A failed stop after an earlier data
  failure is preserved only in `host->multi_stop_result`;
  `host->multi_completed`/`multi_block` preserve the completed/failing
  ordinal, and the raw stop command/response fields are in
  `Gta4lEmmc.h:632-649`.
- Consequences for a production path: bytes reach the destination before the
  mandatory stop is known; the API does not return completed and stop
  outcomes as first-class outputs; an early argument error leaves the previous
  request's `multi_*` state stale; and `multi_stop_valid` means "attempted",
  not "succeeded". A production wrapper must therefore use a private bounce
  and qualify transport, exact completed count and clean stop before copying
  any chunk byte to the client. It must not treat the probe's acceptance
  logic as a production checksum.

### 2.4 Probe-only acceptance that must not leak

`package/Gta4lEmmcProbe.c` uses `read_multi` only for the DTO window
(8 blocks, `:46`, `:3100-3102`) and the GPT double-read (2 and 21 blocks,
`:3898-3919`), with static reference arrays (`:3064`, `:3344`) and byte/sum
comparisons against same-boot CMD17 references. Its report fields
(`:1001-1032`, `:3224-3251`) and its `mb_same`/validation gates are
diagnostic acceptance, not user-read validation. The production path must not
transplant the large static arrays, the hidden reference comparison, the probe
report fields or the probe's acceptance shortcuts.

### 2.5 ABI, build and payload state

- The accepted storage-ABI artifacts in `out/gta4l/storage-abi/` are bound to
  the p5b/D9 candidate `2f8238b9...` (190 slots, 26 concrete overrides,
  0 unresolved original imports; provenance `cdfe6950...`).
- The current shared kernel/plan is `b1871d40...` (D12.6).
  `scripts/test-gta4l-storage-abi.py` fails closed with
  `positive audit failed: Derived candidate does not match the plan`; this is
  artifact staleness, not a source regression. Do not relabel the p5b derive
  as current. A D13 integration candidate needs its own qualification
  directory with a matching kernel, plan and live snapshot.
- Payload plumbing: the readiness batch recorded `emmc-block-1` mapping to
  `gta4l-emmc-block=1` in
  `scripts/build-gta4l-diagnostic-payload.py` and
  `scripts/check-gta4l-payload.py`; `BLOCK_PREFIX`/`BLOCK_DIAGNOSTIC` were
  mutually exclusive with the probe family; and
  `scripts/package-gta4l-diagnostic.py` allowlisted `emmc-block-1`. The block
  patch is in the recorded build
  (`docs/diagnostic-payload-input-inventory.md:107`). N3 added
  `emmc-block-3` to the builder, checker, packager, inventory and tests
  together; the block token set is now `emmc-block-1` and `emmc-block-3`, and
  the duplicate/mixed-probe/deferred-`=2` rules are unchanged.

### 2.6 Pre-existing nuances the design must account for

These are not changed by this batch and are not D13 defects:

- `gta4l_emmc_block_mode_select` returns inert for value 0 before evaluating
  duplicate or conflict tokens (`package/Gta4lEmmcBlock.c:66-72`), so
  `gta4l-emmc-block=0` with a conflicting token is inert rather than a logged
  refusal.
- An offline request is not an immediate rejection; it takes the gate and
  completes with `kIOReturnNoMedia` (`package/Gta4lEmmcBlock.cpp:557-562`).
- The adapter and the portable core each keep an `online` flag, and the
  adapter has no recovery path.
- `fillWatchdogReport` reads counters without the gate
  (`package/Gta4lEmmcBlock.cpp:1087-1110`), unlike `fillDriverReport`
  (`:774-795`); a new report field must not assume a locked watchdog snapshot.
- `GTA4L_EMMC_BLOCK_READ_WAIT_MS` (`package/Gta4lEmmcBlock.h:36`) is defined
  but unused.

### 2.7 N3 implementation update (2026-11-06)

The follow-up N3 host batch implements the proposed seam without changing the
measured p5b/F5 rung. `gta4l-emmc-block=3` selects report mode with two-sector
CMD18 chunks; `=1` keeps the CMD17 singleton backend and `=2` remains refused.
The portable helper now has an optional multi-read callback whose result
carries transport, requested/completed sectors and stop attempted/result; it
uses a caller-supplied chunk bounce and copies a chunk only after the callback
return code, exact count, mandatory stop attempt and clean stop result all
qualify. A singleton and an odd tail use CMD17. The adapter owns a 1024-byte
chunk buffer, wires `gta4l_emmc_read_multi_result()` to the diagnostic
transport without changing `gta4l_emmc_read_multi()` semantics, and reports
`mode=3` plus the `req/c17/c18/c18b/cfail/stops/sfail/last_stop` counters. The
implementation, test totals and remaining limits are in
`docs/emmc-driver.md` §5.3.48.

## 3. D13.2 — contract and N3 status

| Topic | Proposed first contract | Status |
| --- | --- | --- |
| External request | Keep the accepted 64-sector / 32-KiB / one-segment limit and the existing p5b mode. | Preserved (N3) |
| Transfer shape | CMD17 for one sector; 2-sector CMD18 chunks; one CMD17 tail for an odd remainder. | Implemented host-only (N3); the 2-sector choice is the plan's conservative recommendation, not a qualification of long chunk sequences. |
| Multi-read callback | Optional callback returning transport result, requested sectors, completed sectors and stop outcome explicitly (or a small struct). No success until exact count and clean mandatory CMD12. | Implemented host-only (N3); the collapsed `read_multi` return is not the only signal. |
| Byte delivery | Read each chunk into a private bounded bounce; copy to the request descriptor only after qualification. A failed chunk contributes no bytes; earlier qualified chunks may contribute partial bytes. | Implemented host-only (N3); the diagnostic `read_multi` still writes directly to its caller buffer, so the adapter supplies the bounce. |
| Bounce | One owned buffer sized to the chunk (2 x 512 = 1024 bytes for the proposed shape), not a VLA or large kernel-stack allocation; no direct DMA into descriptors; no stale reuse. | Implemented host-only (N3) |
| Failure policy | Preserve the first transport/data failure separately from the stop result; on failure set offline, stop issuing suffix chunks, notify clients outside the gate, and never retry, fall back or recover in-request. | Implemented host-only (N3) |
| Completion | Exactly one completion per accepted request; none for immediate rejection; gate released and request buffer cleared before the callback; no callback under the lock; no double completion. | Preserved (N3) |
| Validation | Production reads do not compare against hidden CMD17 references; exact reference comparison belongs to the qualification reporter. | Implemented host-only (N3) |
| Selector | New explicit report-mode selector, distinct from `gta4l-emmc-block=1`; the exact token/value is fixed at the implementation decision. Parser, conflicts, payload, packager, inventory and tests change together. | Implemented as `gta4l-emmc-block=3`; `=1` is unchanged and `=2` remains refused. |
| Mount/write | No publish-only mode, filesystem mount, root switch, write path, DMA/CQE/ICE or width/speed change. | Still excluded |

### 3.1 Time-bound derivation (N3 disposition)

The transport budgets are `CMD_TIMEOUT_US=1,000,000`,
`DATA_TIMEOUT_US=1,000,000`, `DATA_DRAIN_US=500,000` and
`POLL_STEP_US=10` (`package/Gta4lEmmc.h:231-241`). A chunk can spend up to 1 s
in the pre-CMD18 inhibit wait, 1 s in the CMD18/data bound, 0.5 s in the drain
and up to 2 s in the two CMD12 waits: roughly 4 to 4.5 s per pathologically
slow chunk. A pathological but successful 64-sector request at 2-sector
chunks is 32 chunks, so the derived gate-hold bound is roughly 128 to 144 s,
far above any interactive expectation. A single failed chunk is bounded at
roughly 4.5 s before the request fails offline and stops issuing suffix
chunks. The report watchdog is 30,000 ms and applies to the report thread, not
to general I/O; `GTA4L_EMMC_BLOCK_READ_WAIT_MS=5000` is defined but unused.

This record did not silently change the ceiling or raise a timeout. N3 chose
option (a): it keeps the two-sector shape and the 64-sector ceiling, records
the synchronous gate-hold bound as a known host-development risk, and changes
no timeout or advertised limit. The first hardware discriminator remains
small: a 2-sector GPT-array read plus an odd-length split/tail, not a
64-sector sweep.

## 4. File-by-file implementation map (N3 implemented host-only)

| File | Proposed change |
| --- | --- |
| `package/Gta4lEmmcBlockCore.c/.h` | Add an optional multi-read callback and a chunk loop that qualifies transport/completed/stop before copy-out; add bounded counters for CMD17/CMD18 chunks, requested/completed/delivered bytes and stop status; keep the existing one-sector path unchanged for `=1`. |
| `package/Gta4lEmmcBlock.c/.h` | Add the new selector and its conflicts; extend the report struct/format within the 2048-byte budget; keep the existing report fields and mode-1 semantics. |
| `package/Gta4lEmmcBlock.cpp` | Implement the callback through a production wrapper; own the chunk bounce; copy out only qualified chunks; preserve the gate, single-completion and offline-notification order. |
| `package/Gta4lEmmc.c/.h` | Add a wrapper that exposes completed and stop outcomes explicitly (or safely captures them) without changing the diagnostic `read_multi` semantics; keep probe callers unchanged. |
| `package/Gta4lEmmcPlatform.c/.h` | No change expected. |
| `patches/xnu/gta4l-emmc-block.patch`, `scripts/build-gta4l-emmc-block.py` | Update only if new symbols or the personality flag require it. |
| `scripts/sim-gta4l-emmc-block/*`, `scripts/test-gta4l-emmc-block.py` | Add chunk/split/failure scenarios and runtime-negative mutations. The initial N3 suite was 42 core scenarios / 29 mutation controls / 42 adapter scenarios; the corrective batch raised it to 48 core / 38 mutation / 55 adapter plus 3 adapter-source mutations; the readiness inventory was 23/18/32. |
| `scripts/sim-gta4l-emmc/*`, `scripts/test-gta4l-emmc.py` | Keep the 18 multi-* scenarios and 12 multi-block mutations; the N3 wrapper adds its own focused checks. |
| `scripts/build-gta4l-diagnostic-payload.py`, `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py`, `docs/diagnostic-payload-input-inventory.md` | Add the new selector token/value, mutual-exclusion rule, allowlist and inventory row. Implemented as `emmc-block-3` -> `gta4l-emmc-block=3`. |
| `docs/emmc-driver.md`, `docs/gta4l-storage-publication-design.md`, `porting.toml`, `README.md`, `docs/evidence-baseline.md` | Update findings/evidence with the implementation; updated by the N3 batch. |

## 5. D13.2 — required host scenarios and negative controls

The implementation must test the portable helper and the real C++ adapter seam
with actual command selection, callback order and completion assertions, not
counters alone. The N3 implementation and its corrective batch run 48 core
scenarios / 38 mutation controls, 55 adapter scenarios and 3 adapter-source
mutations; the diagnostic 18 multi-* scenarios / 12
multi-block mutations are unchanged. The readiness baseline was core 23 / 18,
adapter 32; it is history, not the current total.

### 5.1 Transfer shape and byte correctness

- Singleton CMD17; 2-sector CMD18; 3-sector split (2+1); exact chunk boundary;
  21-sector request; 64-sector maximum; one over-limit request.
- Nonzero and end-of-media LBAs; overflow-safe range arithmetic; callback
  count bounds; contiguous LBAs; no duplicate sectors; final singleton; exact
  byte contents and offsets; no stale bounce bytes between chunks.

### 5.2 Rejections

- Short descriptor, invalid direction, every write direction, zero length,
  missing callback, offline media, invalid completion, malformed/conflicting
  selector: reject without transport or copy calls, with the correct
  immediate error and no completion (or the documented accepted-offline
  completion where the current adapter does that).

### 5.3 Failures and partial delivery

- First/middle/last chunk command or data failure; simultaneous
  completion/error; count-short success seam; CMD12 refusal/timeout/card
  error/busy failure; descriptor short/overclaimed copy.
- Assert nonzero result, exact partial-byte count, offline state, preserved
  first failure and separate stop evidence, and no suffix I/O after the first
  failure.

### 5.4 Lifetime and completion

- Exactly one completion on every accepted request; none after immediate
  rejection; gate released and request-buffer pointer cleared before the
  callback; no deadlock on callback re-entry; no use-after-free; preserve the
  p5b open/close and media-path regression coverage; no writes or mount path.

### 5.5 Runtime-negative mutations

- CMD17-only substitution for a chunk; skipped or dirty stop accepted; wrong
  split/LBA/offset; overcounted completed bytes; reused stale bounce; suffix
  after error; overwritten first failure; double completion; callback under
  the lock.
- Each mutation must fail for the intended reason at runtime; a compile
  failure does not count.

### 5.6 Reporting and counters

- Add only the counters needed to distinguish CMD17, CMD18, chunks,
  requested, drained/delivered bytes and stop status.
- Bound every success/failure panic against the real 2048-byte buffer; keep
  overflow controls.
- The qualification reporter must bind the intentional IOMedia read to the
  command/stop counters under the gate; incidental partition scans cannot
  satisfy it. Keep same-boot CMD17 references diagnostic-only and compare
  exact bytes, not an additive checksum alone.

## 6. Validation performed in this readiness batch

From `src/UniXNU-gta4l/` with `LC_ALL=C` and `/usr/bin/python3.11 -B`:

| Check | Result |
| --- | --- |
| `tomllib` parse of `porting.toml`, `board.toml`, `feeds.toml` | PASS: 3/10/1 top-level keys; `porting.toml` has 13 areas |
| `../UniXNU/scripts/porting-record.py check --device .` | PASS: `gta4l: 13 structured areas checked` |
| `scripts/check-gta4l.py` | PASS: `SM6115 (gta4l) board layout verified` |
| `scripts/test-gta4l-emmc-evidence-counts.py` | PASS: core 187 scenarios/115 mutation controls; probe 196 scenarios/100 mutation controls |
| `scripts/test-gta4l-ram-root-fixture-recipe.py` | PASS: input and failure checks, including the §2 hash-table acceptance after the `README.md`, `porting.toml` and `docs/emmc-driver.md` hash-row refresh |
| `scripts/test-gta4l-storage-abi.py` | exited 1: `positive audit failed: Derived candidate does not match the plan` (stale p5b artifacts versus the current D12.6 kernel/plan) |
| `git diff --check` in the device repository | PASS: tracked diffs clean; the new untracked document has no whitespace errors (`git diff --no-index --check` emits no check output; its exit 1 is the normal difference result) |
| Privacy scan over the changed docs and added lines | No private value found in the new document or added lines. The email pattern matches the pre-existing DT node names `sdhci@4744000` and `sram@45f0000` in `porting.toml` and this row's own `sdhci@4744000` mention; the `serial=` pattern matches the QEMU `-v serial=3` argument in pre-existing text and this row's own `serial=` label, not a device identifier; the legacy `adb`/IP fields at `docs/emmc-driver.md:1762,1782,2045,2175,4095` are pre-existing private-evidence command lines outside the added lines and are not repeated here. |

The earlier 24-script battery (23 passed, with the same storage-ABI staleness
failure) remains historical and is not re-claimed as a current full-suite
acceptance. The N3 host batch re-ran the affected suites after the
implementation; its totals and results are in `docs/emmc-driver.md` §5.3.48
and the N3 evidence row in `docs/evidence-baseline.md`.

## 7. Evidence and manifest

Private raw inventory, diffs and hashes stay under the ignored
`out/gta4l/ram-root-emmc-d13-readiness-evidence/`. A supplemental
relative-path manifest covers this document and the readiness evidence;
existing D12, p5b and F2 manifests are not rewritten.

## 8. Limits and next decision

This is not a candidate, a hardware result or a reliability disposition. N2
selected option 1 and the N3 host implementation is complete; initialization
reliability remains explicitly deferred, so CMD3 can still prevent a later
device observation. The next step is an N4 candidate proposal, which requires
separate candidate-build and ABI/QEMU approval. No candidate hash, timeout
change or hardware acceptance is fixed here.
