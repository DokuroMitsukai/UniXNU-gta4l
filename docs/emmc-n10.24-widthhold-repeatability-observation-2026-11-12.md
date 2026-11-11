# gta4l N10.24 widthhold repeatability owner observation (uncaptured, 2026-11-12)

**Status:** owner-reported observation, 2026-11-12. The owner executed the
approved repeatability series on the unchanged installed N10.22 image and
reports that every boot repeated the N10.23 `WIDTHHOLD` success output. No
raw capture, normalized report lines, verifier log, pre/post integrity hash,
cold/warm classification or per-attempt record was supplied. This record
therefore documents an **owner observation**, not a captured protocol PASS.
The agent performed no device access, flash, boot, restore, mount, write,
candidate build or driver change while producing this record. N10.20 remains
FAIL and N10.23 remains the only captured widthhold hardware result.

## 1. Bounded question and approved scope

The N10.24 repeatability protocol
(`docs/emmc-n10.24-widthhold-repeatability-protocol-2026-11-12.md`) proposed
a finite owner-controlled series on the unchanged N10.22 image already on
BOOT/p69. The approved scope was three additional cold boots, with the series
stopping on the first non-pass, read-only pre/post verification, full-screen
capture, labeled normalization, a per-attempt widthhold verifier run and
per-attempt records.

The owner accepted that scope and reports that the series completed with the
same successful output on every boot. The owner did not supply the capture,
normalization, verifier or hash artifacts the protocol requires for a PASS.

## 2. Fixed image identity

No reflash, rebuild, rollback or restore was performed by the agent, and the
owner did not report one. The intended image remains the N10.22 widthhold
candidate from the N10.23 record:

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

These are the N10.23-recorded values. They were not re-measured before or
after this observation. The association between the reported boots and this
exact image is therefore inherited from the N10.23 flash record, not
independently bound by a pre-series hash in this observation.

## 3. Owner report

The owner reports that every approved boot:

- reached the same `WIDTHHOLD` success shape as N10.23 — `cases=4 passed=4
  failed=0 reason=0 complete=1`, `match=1`, `mismatches=0`, `fails=0`,
  `foreign=0`, `cancel=0`, `budget=0`, `partdiag=0`, verified switch and
  `revert=1 rverified=1 rstatus=0`;
- showed the same `WIDTHHOLDREAD` last-case result with `digest` equal to
  `predigest`;
- showed no `WIDTHHOLDFAIL`, watchdog report or hang; and
- was followed by a successful manual return to TWRP.

The owner personally confirmed the output. No non-pass was reported, so the
first-non-pass stop rule was not triggered as far as this report goes.

## 4. What this observation does not establish

The protocol's PASS predicate requires a verifier exit 0, the full success
shape, pre/post integrity equality and a confirmed TWRP return. This
observation supplies only the owner's screen report, so it does not
establish:

- the exact number of attempts actually executed (the approved scope was
  three cold boots; no per-attempt count or cold/warm classification was
  supplied);
- SHA-256 binding of the observed output to the N10.22 image or to the
  `WIDTHHOLD`/`WIDTHHOLDREAD` lines;
- post-attempt p69, tail, last-1KiB, RECOVERY, EFS or sec_efs integrity;
- a host verifier result for any attempt;
- a failure rate, initialization reliability, cold/warm correlation or
  statistical repeatability;
- arbitrary I/O, wider/faster modes, writes, mount/root or publication
  readiness.

Under the protocol's outcome classes this is **not a PASS**. It is best
classified as an uncaptured owner-observed success shape. A missing or
partial capture would be INSUFFICIENT under §6; the owner's direct
observation is recorded here as descriptive corroboration, not as a
captured result.

## 5. Bounded verdict

**Owner-reported: the approved series repeated the N10.23 widthhold success
shape on every boot.** This is consistent with N10.23 and does not
contradict any prior result. It does **not** upgrade the evidence state:
N10.23 remains the only captured widthhold hardware PASS, repeatability
remains `[U]` in the SHA-256-bound evidence sense, and no failure-rate or
initialization-reliability claim is made. N10.20 remains FAIL and is not
reinterpreted.

If the owner later supplies the raw captures, normalized lines, verifier
logs and pre/post hashes, a captured closeout can replace or supplement this
observation. No reflash, restore, reset/repair, retry or replacement boot is
part of this record.

## 6. Privacy

This record contains only public image hashes, partition identifiers already
present in the tracked evidence, and a de-identified owner report. It
contains no serial number, unique device identifier, account name, hostname,
SSH target, e-mail address, IP address, VPN name or absolute home path. Raw
captures, if any exist, remain outside the tracked tree.

## 7. References

- `docs/emmc-n10.23-widthhold-hardware-run-2026-11-12.md`
- `docs/emmc-n10.24-widthhold-repeatability-protocol-2026-11-12.md`
- `docs/emmc-n10.22-widthhold-candidate-production-2026-11-12.md`
- `docs/emmc-n10.24-widthpub-design-2026-11-12.md`
- `docs/emmc-driver.md`
- `scripts/check-gta4l-widthhold-evidence.py`
- `porting.toml`
