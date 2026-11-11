# gta4l N10.24 widthhold captured repeatability protocol (host-only, corrected 2026-11-12)

**Status:** host-only protocol proposal, corrected on 2026-11-12 after owner
review. This batch performed no device access, flash, boot, restore, mount,
write, candidate build or driver change. It follows the N10.23 bounded PASS
and proposes a finite, owner-controlled repeatability series on the
**unchanged N10.22 image already installed on BOOT/p69**. It does not
authorize execution. Running it requires fresh explicit owner approval of the
exact scope in §9, including read-only device access, and an owner-selected
finite boot budget. N10.20 remains FAIL and is not reinterpreted.

## 1. Bounded question

N10.23 returned one bounded width-hold PASS. The descriptive question is:

> On the same unchanged already-flashed N10.22 image, do further
> owner-controlled boots repeat the `WIDTHHOLD` success shape and the
> unchanged post-boot integrity, and what is observable when initialization,
> switching, media/partition discovery, a tagged read, a digest comparison,
> the budget, the gate, the revert or the capture fails before a complete
> report?

This protocol does not estimate a failure rate, invent a statistical
threshold, qualify initialization reliability or move to publication-hold,
mount, writes or wider/faster modes. It defines a finite observation series
and the evidence each attempt must produce. A missing or partial capture is
never a pass.

## 2. Fixed image identity and preconditions

No reflash, rebuild or rollback is part of this protocol. The image is the
N10.22 widthhold candidate already on BOOT/p69 from the N10.23 run:

| Item | SHA-256 / identity |
| --- | --- |
| BOOT image | `b4d63be7ddaf70f88139b000e31a3e9e26e0cc849757db4f49696e3b9ce0877d` (85,688,320 bytes) |
| Prelinked kernel | `04ae57f6f5659b1a4b4236b29931d0711b3d210fefee3cb19fbf95bfd861b097` |
| Payload | `6491408907c6da896be9a6bf81be6b5a00d7153c656c04803f152f1b283b9ef1` |
| Post-N10.23 full p69 | `7260ad75caf7522b1e6ff7ccb63979b2096dd7d68fd580b1844574e3419a34cb` |
| p69 tail beyond the image | `a28b4ea28a157a4d55e8574fd5ea32b72f261cde679feeedd11b714584d1e27f` |
| p69 last 1 KiB | `a4f065b4f14f4911b49f8bd68fef979862d19e1611146a28ee62c3f80df0746f` |
| RECOVERY | `14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae` |
| EFS / sec_efs | `51fa824c4bea060f6bf42ee9c1a8c95657a35e3f872a8799f5762963a12f37bd` / `3a265d587700b3e4899f7d13617c3cd3ae9aa3c9972d8d47f04f9b13b9c05970` |
| Retained rollback | verified full BOOT backup of the pre-test N10.19 image `f0940421…`, held in the owner's private store; restoration is not authorized |

Association with the running image uses the N10.23 flash record and a
read-only pre-series p69 verification. It is not an independently measured
running-image hash, and the cold/warm class is supplied by the owner, not
inferred.

## 3. Boot budget and attempt accounting

- Three additional cold boots are a **proposal only**, not an approved
  budget. The owner selects a finite number of boots in advance; any change
  is a new approval.
- Every launch attempt counts toward the budget, including
  pre-identification stops, hangs, missing output, owner interruptions and
  boots that produce an unusable capture. There is no replacement, retry or
  automatic reboot inside the budget.
- Cold boots only unless the owner separately scopes warm boots. The owner
  classifies each attempt; an unclassified attempt is recorded as unknown
  and is not aggregated as cold.
- Each attempt ends at the terminal report or the owner-observation deadline
  (about two minutes after power-on, as established in the N10.23
  procedure), whichever comes first, followed by a manual return to TWRP.
  No force-reboot key combination, host-issued reboot or second diagnostic
  boot is part of this protocol.
- No reflash, restore, wipe, repair, mount, root switch or storage write is
  part of any attempt.
- The series records planned, attempted, passed, failed and insufficient
  counts plus the unused budget. It reports a bounded descriptive series,
  not a rate.

## 4. Per-attempt procedure

1. **Pre-attempt (read-only, separately approved):** confirm the device is
   in TWRP and re-measure p69 prefix/full/tail/last-1KiB and
   RECOVERY/EFS/sec_efs. Require prefix `b4d63be7…`, full p69 `7260ad75…`,
   tail `a28b4ea2…`, last 1 KiB `a4f065b4…` and the three protected
   partitions above. Any mismatch stops the series before a boot.
2. **Boot:** the owner triggers the one approved diagnostic boot.
3. **Capture:** record the entire terminal screen, including the `REPORT`,
   `DRV`, `XFER`, `MEDIA`, `WIDTHHOLD`, `WIDTHHOLDREAD`, `SNAP` and terminal
   `FAIL` lines, plus any pre-report message, watchdog report or hang.
   Preserve the raw capture verbatim outside the tracked tree.
4. **Return:** the owner returns the tablet to TWRP manually and confirms
   that TWRP started. This demonstrates recovery access after the run; it is
   **not** evidence of a physical power cycle, a reset command or a card-width
   reset. The mode reverted to 1-bit before its terminal, and no held-width
   state is claimed.
5. **Post-attempt (read-only):** re-measure the same p69 and protected
   ranges. On the success path they must equal the pre-attempt values.
6. **Verify:** normalize the wrapped `WIDTHHOLD`/`WIDTHHOLDREAD` lines in a
   separate labeled file without repairing values, then run
   `scripts/check-gta4l-widthhold-evidence.py` against the exact N10.22
   image. Record the command and exit status. The raw capture and the
   normalized file both stay in private evidence.
7. **Record:** write one per-attempt record with the owner classification,
   the normalized report, verifier result, pre/post integrity hashes and the
   outcome class.

## 5. Per-attempt capture requirements

- The summary line must carry the complete N10.21 schema, including the
  `WIDTHHOLD` success fields `cases passed failed reason complete switch
  verified hb ha rb cmd6 refs refbytes refblocks refstatus refchunks
  refstops refstopfail refdigest reads bytes blocks subrequests max c18
  stops stopfail match mismatches fails foreign cancel budget obsdigest
  partbase partsize partro partdiag revert rverified rhb rha rcmd6 rstatus
  limit seg cap`.
- `WIDTHHOLDREAD` must be the last-case line (`index=3`, `lba=2255366`,
  `start=3072`, `length/span/actual=1024`) with `digest` equal to
  `predigest`.
- Expected success values on the unchanged image: `refdigest=obsdigest`
  prefix `417cdef0f05bceafea13119a3b9f1ff5`; `WIDTHHOLDREAD` prefix
  `5f70bf18a086007016e948b04aed3b82`; `partdiag=0`; `partbase=1154744320`,
  `partsize=100663296`, `partro=1`; verified switch `4` and verified revert
  `1`; `revert=1 rverified=1 rstatus=0`; no `WIDTHHOLDFAIL`.
- The terminal `FAIL: valid=0 stage=0 code=0` line is the intentional report
  stop, not a failure, and does not override the driver's success guard.
- Wrapping artifacts are joined in the labeled normalized file only
  (`by tes`→`bytes`, `GUID_`+`partition_scheme`, `comple`+`te`,
  `s`+`fail`, `par`+`tdiag`, digest continuations); values are never
  repaired or guessed. `@Gta41EmmcBlock.cpp` keeps its `l`/`1` ambiguity.

## 6. Outcome classes

- **PASS:** verifier exit 0; the full success shape above; post-attempt
  integrity equal to pre-attempt; TWRP return confirmed.
- **FAIL (pre-identification):** the report or screen stops before
  identification/switch. This is an observation, not a widthhold failure,
  but it is **not a PASS**; the attempt still counts.
- **FAIL (widthhold):** a `WIDTHHOLDFAIL` line or a nonzero `reason`,
  `fails`, `mismatches`, `foreign`, `budget` or `cancel`, an unverified
  switch or revert, a watchdog report, or a digest mismatch (host-side
  mismatch always rejects, even if the on-device guard passed).
- **FAIL (integrity):** any pre/post p69, tail, last-1KiB or protected
  partition change, or a TWRP return that cannot read the card.
- **INSUFFICIENT:** missing, ambiguous or partial capture that cannot be
  normalized without guessing. Not a pass and not a widthhold failure; the
  attempt counts and the cause is recorded.

A run that passes the driver guard but fails the host verifier is a
**FAIL**, never a partial pass.

## 7. Stop rules

Stop the series immediately on the **first non-pass**. Do not start another
attempt after any of:

- a pre-identification stop or any widthhold-phase failure;
- a watchdog report;
- any integrity discrepancy (pre or post);
- a failed recovery or a TWRP return that cannot read the card;
- an insufficient or ambiguous capture that cannot be normalized without
  guessing;
- the owner aborts; or
- the finite budget is exhausted.

There is no replacement boot, automatic retry, reflash, restore,
reset/repair or substitute image after a stop. The tablet stays as last
written and a new proposal is required before any further device access.

## 8. What the series can and cannot establish

- It can show descriptively whether the N10.23 bounded success shape repeats
  on this unchanged image across the approved boots, and what failure modes
  appear when it does not. It reports planned/attempted/passed/failed/
  insufficient counts, not a failure rate.
- It cannot establish a failure rate, initialization reliability, a
  cold/warm correlation, publication readiness, sustained arbitrary I/O,
  wider/faster modes, writes, mount/root or durability.
- It does not authorize the bounded publication-path rung or any candidate
  change; those remain separate designs and gates
  (`docs/emmc-n10.24-widthpub-design-2026-11-12.md`).

## 9. Exact approval scope and gates

Execution requires fresh explicit owner approval of all of:

1. read-only device access for the pre-series and per-attempt p69,
   RECOVERY/EFS/sec_efs verification in §2 and §4, with no backup, restore
   or mount;
2. a finite owner-selected number of owner-controlled boots of the unchanged
   N10.22 image (three additional cold boots proposed; cold only unless
   separately scoped), an owner-classified cold/warm condition for each
   attempt and a finite observation deadline;
3. the known manual TWRP-return procedure and the per-attempt capture,
   normalization, verification and private-evidence layout in §4–§5; and
4. no reflash, rebuild, rollback, restore, reset/repair, partition write,
   host-issued reboot, retry, extra boot, commit or push.

If the owner does not approve, hardware stays paused. If approved, Build
executes only inside that scope and stops at the first non-pass. A new
candidate, instrumentation, image or boot class needs its own gates.
Device mapping and any backup access require separate read-only approval.

## 10. Evidence and privacy

Raw captures, screenshots, transcripts and normalized files stay in a
gitignored private evidence tree outside the tracked record. Each attempt
directory contains the raw screen capture, the labeled normalized lines, the
verifier log with exit status, the pre/post integrity log and an observation
record; manifests do not hash themselves. The tracked closeout contains only
the sanitized summary and hashes. Do not put serial numbers, unique device
identifiers, account names, hostnames, SSH targets, e-mail addresses, IP
addresses, VPN names or absolute home paths in the tracked summary or any
manifest.

This proposal is host-only and not executed. N10.23 remains the only
widthhold hardware result. The next step is owner review of §9; no device
access, flash, boot, commit or push is authorized by this document.
