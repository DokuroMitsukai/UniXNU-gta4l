# gta4l SDHC evidence baseline

Status: host-only snapshot of the working tree used for the software-evidence
closure `[S]`. It is not a hardware result, an electrical measurement or
permission to activate bus power or the card clock; the decision remains
**NO-GO** (`sdhc-decision-memo.md`) `[S]`.

Hashes were first taken at `2026-10-30T21:06:39Z` and refreshed on
`2026-10-31` after the porting-approach reassessment, the native eMMC
implementation and the eMMC audit fixes. This document's own hash is not
listed. Hashes describe the working tree at the snapshot and change with later
edits; re-run the commands in §6 before reusing them. The snapshot is intended
to be committed without further content changes; if the committed content
differs, re-run §6 and update the table.

## 1. Repository state

| Item | Value |
| --- | --- |
| Repository | `src/UniXNU-gta4l` (device repository) |
| Branch | `bringup/sdhc-evidence` |
| HEAD at the hash snapshot | `904b8eb` (eMMC audit-fix commit) |
| Parent of the fourteenth evidence batch | `95ff0a1293194b2108f4041068c77f18e708c1fd` (same commit) |
| Correction commit paths | 8: 7 modified (`docs/evidence-baseline.md`, `docs/firmware-handoff.md`, `docs/sdhc-decision-memo.md`, `docs/sdhc-dependency-trace.md`, `docs/sdhc-electrical-contract.md`, `docs/sdhc-power-pad-dependency-report.md`, `porting.toml`) and 1 added (`docs/sdhc-decision-update-2026-10-30.md`) |
| First follow-up commit paths | 6: 5 modified (`docs/evidence-baseline.md`, `docs/firmware-handoff.md`, `docs/sdhc-decision-update-2026-10-30.md`, `docs/sdhc-power-pad-dependency-report.md`, `porting.toml`) and 1 added (`scripts/test-gta4l-sdccdxe-decode.py`) |
| Second follow-up commit paths | 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-power-pad-dependency-report.md`, `scripts/test-gta4l-sdccdxe-decode.py`) |
| Third follow-up commit paths | 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-power-pad-dependency-report.md`, `scripts/test-gta4l-sdccdxe-decode.py`) |
| Fourth follow-up paths | 4: 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-decision-memo.md`, `porting.toml`) and 1 added (`docs/sdhc-evidence-gap-checklist.md`) |
| Fifth correction paths | 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `porting.toml`) |
| Sixth correction paths | 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `porting.toml`) |
| Seventh correction paths | 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `porting.toml`) |
| Eighth correction paths | 6: 5 modified (`docs/evidence-baseline.md`, `docs/pmic-audit.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/spmi-0x28-analysis.md`, `porting.toml`) and 1 added (`scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Ninth correction paths | 6 modified (`docs/evidence-baseline.md`, `docs/pmic-audit.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/spmi-0x28-analysis.md`, `porting.toml`, `scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Tenth correction paths | 6 modified (`docs/evidence-baseline.md`, `docs/pmic-audit.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/spmi-0x28-analysis.md`, `porting.toml`, `scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Checker-fix follow-up paths | 4 modified (`docs/evidence-baseline.md`, `docs/pmic-audit.md`, `docs/sdhc-evidence-gap-checklist.md`, `porting.toml`) |
| Eleventh correction paths | 5 modified (`docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/spmi-0x28-analysis.md`, `porting.toml`, `scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Twelfth evidence-batch paths (committed as `34761fc`) | 5: 4 modified (`docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/stock-firmware-analysis-audit.md`, `porting.toml`) and 1 added (`docs/vendor-sdhc-power-sequence.md`) |
| Thirteenth correction paths (committed as `88aa689`) | 10 modified (`.circleci/config.yml`, `.github/workflows/build-xnu-kernel-gta4l.yaml`, `.github/workflows/workflowtest.yaml`, `.github/workflows/workingworkflow.yaml`, `docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/stock-firmware-analysis-audit.md`, `docs/vendor-sdhc-power-sequence.md`, `porting.toml`, `scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Checker-fix follow-up paths (committed as `95ff0a1`) | 2 modified (`docs/evidence-baseline.md`, `scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Fourteenth evidence-batch paths at this snapshot | 5 modified (`docs/emmc-reference.md`, `docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/vendor-sdhc-power-sequence.md`, `porting.toml`) |
| Fifteenth reassessment paths at this snapshot | 3: 2 modified (`docs/sdhc-decision-memo.md`, `porting.toml`) and 1 added (`docs/porting-approach-reassessment-2026-10-31.md`) |
| Sixteenth implementation paths at this snapshot | 15: 6 modified (`README.md`, `board.toml`, `docs/evidence-baseline.md`, `docs/porting-approach-reassessment-2026-10-31.md`, `patches/xnu/series`, `porting.toml`) and 9 added (`docs/archlinux-gta4l-evidence.md`, `docs/emmc-driver.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `patches/xnu/gta4l-emmc-probe.patch`, `scripts/build-gta4l-emmc-probe.py`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`) |
| Seventeenth audit-fix paths at this snapshot | 8 modified (`docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `porting.toml`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`) |
| Eighteenth audit-fix paths at this snapshot | 13 modified (`configs/gta4l_defconfig`, `docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `porting.toml`, `scripts/build-gta4l-diagnostic-payload.py`, `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`) |
| Dirty entries at the hash snapshot | 13, exactly the eighteenth audit-fix paths above |
| Staging and commits | None at the snapshot; this eighteenth batch lands as one commit on top of `904b8eb` |

The hash table describes the fourteenth evidence-batch working tree: the
checker-fix commit `95ff0a1` plus the then-uncommitted official-source,
board-manual and eMMC-candidate records listed above. The eleventh
correction commit `4c447b9` contains exactly the five paths in its row. The
tenth correction commit `07a36e6`
contains exactly the six paths in its row. The ninth correction commit
`3e47264`
contains exactly the six paths in its row. The eighth correction commit
`e21d7da` contains exactly the six paths in its row. The seventh
correction commit `4148a61` contains exactly the three paths in its row. The
sixth correction commit `9699ded` contains
exactly the three paths in its row. The fifth correction commit `339a7e2`
contains exactly the three paths in its row. The fourth follow-up commit
`769d1bc` contains
exactly the four paths in its row. The third and second follow-up commits each
contain exactly the three paths in their rows. The first follow-up commit
contains exactly the six paths in its row: five documentation/porting edits and
the new host-only decode test. The correction commit `a958e26` contains exactly
the eight paths in its row and is not the earlier report batch.

The fourth batch added the Phase 1 evidence-gap checklist, linked it from the
decision memo and recorded it in the storage and power porting areas. The fifth
correction batch separated staged approvals from gate closure, labeled
dependencies as informational, added claim-level source references, recorded the
SPMI-arbiter acquisition gaps, narrowed unsupported acquisition restrictions,
marked roles as proposed and prepared the Phase 2 acquisition register. The sixth
correction batch completed shared-consumer and parent-supply acquisition
tracking, separated USID, runtime-APID and descriptor-base evidence, mapped
acquisition items to gates and corrected the E1/E3/E5/E9 source citations. The
seventh correction batch aligns `D11` with the `0x8878` descriptor-base override
path and extends `D6` to the `E5`/`E6`/`E7` measurement and recovery records. The
eighth correction batch added the initial pinned-image descriptor-base scan and
its reproducible host test. The ninth correction batch corrected the scan's
pointer propagation, address-range and index handling and the resulting claim
scope: it follows newly loaded `.data` pointers, pre/post-index addressing and
bounded direct `BL` recursion, resolves the known caller stores, and finds no
write intersecting `[0x8878,0x8880)`. The tenth correction batch makes the scan
distinguish XZR from SP, invalidate `.data` pointer slots overwritten in the
path or by modeled calls, stop and report instructions outside its
register-only whitelist, and report unresolved non-stack loads and stores and
every revisited-state/step/budget/call-depth/indirect-branch truncation,
conservatively invalidating all `.data` pointer slots on a revisit. Internal
indirect/computed and runtime/external writers remain unresolved. A checker-fix
follow-up corrected the revision-7 and scan-scope wording to state the
revisited-state invalidation explicitly; it changes no gate disposition. The
eleventh correction batch keeps conditional-branch paths independent,
propagates their invalidation union at function return, and stops seeding XZR
roots; it adds regressions for all three and updates the revision-8 scan scope.
The twelfth evidence batch adds two host-only inputs: an exact-build SM-T505N
package whose four BL images and both AP images match the capture in full while
six BL images match only on their decompressed prefixes (the TWRP recovery
differs as expected and the vbmeta mismatch is unresolved), and a pinned
vendor-kernel trace of the stock BUS_ON path (controller clock, BUS_ON, L24,
L11, pads, acknowledgement, the 10 ms core delay, then the card clock, with
explicit RPM enable and voltage votes). The thirteenth correction batch
narrows and corrects both inputs after the broad host-only audit: it separates
the unexplained nonzero partition tails from padding and full-partition
equality; retraces the vendor path against the captured DT's
`rpm-smd-regulator` backend, including aggregation, active/sleep request rules,
cached-state limits, RPM acknowledgement, the BUS_FAIL and 5,000 ms timeout
paths, the conditional register-restore path, and the framework/DT delay
limits; and records the overlay correspondence as name/ID-level only, not
binary provenance. It also scopes CI GitHub credentials to trusted HTTPS GitHub
endpoints with cross-host redirect stripping, and applies 64-bit register and
effective-address semantics, paired access/writeback and boundary-crossing
handling to the descriptor-base scanner, with unchanged result sets against the
audited `4c447b9` baseline. The vendor path is a software precedent and a
different design from the pinned mainline port; it closes no electrical or
measurement requirement. None of these batches adds a measurement, device
action or activation code, and none closes a gate.

The checker-fix follow-up adds single and paired boundary-crossing store/load
controls to the scanner test, states the site-level set comparison used for the
six result sets, and records why the hash table now has 36 rows; it changes no
gate disposition.

The fourteenth evidence batch records the official Samsung CXG1/CWI2 kernel
source comparison: both releases carry Linux 4.19.157 and the same relevant
power/regulator files; the source corroborates the vendor trace but omits the
LineageOS card-detect early-fail/cleanup blocks, and the exact CXB1 source
remains unavailable. It also records that the inspected SM-T505 family service
manual is not a verified T505N board record (PM4250 versus the captured PM6125; board
photo 85943_1_04 versus the title 85943_1_21), and that KMDX60018M-B425
(32 GB eMMC 5.1 + 24 Gb LPDDR4X, 254FBGA) is the leading eMMC candidate while
the exact installed part and electrical limits remain unresolved. No gate
disposition changed.

The fifteenth batch is the owner-directed porting-approach reassessment. It
narrows the existing `NO-GO` to the on-device bus-power activation operation,
states that software porting may proceed with the mainline Linux,
vendor/LineageOS and captured-firmware stack as the reference, and records the
dependencies to reproduce plus the residual electrical, retention, UEFI-volume
and boot-medium risks. It closes no electrical gate, adds no device action and
does not mark any missing measurement as established.

The sixteenth batch is the native eMMC implementation. It adds the portable
SDHCI v5/eMMC core, the simulated controller and card, the 25-scenario host
test with 3 mutation controls, the opt-in XNU probe, the archive builder and
board patch wiring, the `docs/emmc-driver.md` record and the
`docs/archlinux-gta4l-evidence.md` working-stack note. It updates the storage
porting area, README and reassessment. The probe has not been booted on the
tablet; no block device, filesystem, DMA, CQE, ICE, write or microSD path
exists, and no electrical gate is closed.

The seventeenth batch corrects the eMMC audit findings: the command
data-present bit is `0x20`; status bits are enabled for polling with signalling
left off; the reset is polled with byte accesses; CMD3 assigns the RCA through
the argument and checks R1; a missing or stuck power request and exhausted
status-clear retries are failures; `HOST_CONTROL2` is updated in place and the
fixed-1.8 V pad switch is selected; a 5 ms settle precedes the card clock; and
sector reads are bounded by the discovered capacity and the user-partition
selection. The simulator now enforces register widths, command encoding,
status-enable gating and RCA/block bounds independently, and the host test has
32 scenarios and 9 mutation controls. The probe adds stage 2 (host only) and
stage 3 (identification only) and reads `CNTFRQ_EL0` for its delay loop. The
probe is still not booted on the tablet and no electrical gate is closed.

The eighteenth batch closes the second eMMC audit. Bus and IO power requests
are now handled separately and success requires an observed `BUS_ON`
completion; an IO-only request, a delayed bus request and a bus-then-IO
sequence have simulator scenarios. The pad-supply policy is an explicit host
flag, documented as a deliberate departure from the DT-derived Linux state.
The probe aborts on a stalled generic counter instead of skipping settle.
SDHCI long responses are normalized with the same word shift as Linux, and the
response-type test now uses a two-bit mask so `RESP_SHORT_BUSY` is not parsed as
R2. A 4 KiB `DATA_SECTOR_SIZE` is rejected before any sector read, and all poll
loops use the documented 10 µs step. The host test now has 37 scenarios and 12
mutation controls. The probe is still not booted on the tablet and no
electrical gate is closed.

The second batch derived the status/clear/reset loop bounds from the pinned
MOVZ instructions, added last-read and delayed-completion boundary cases and
wrong-bound negative controls, and corrected the reset-argument and SDC2 field
wording. This third batch adds synthetic SDC2 CLK-pull field-position vectors,
corrects the raw-bit wording, and labels the earlier `porting.toml` change as
historical.

The earlier documentation batch contained the eleven paths listed in the
original evidence-closure provenance below: six modified and five added.

### Historical provenance

The original evidence-closure snapshot was taken on
`bringup/tab-a7-qemu-fixture` at
`e2d0cfc08c0c0119f3c55d1bcb6f9b8323893961` with 27 dirty entries: 8 modified
and 19 untracked, including this document. The tree was already dirty. This
task edited only paths already in that set and
added this document and `docs/stock-firmware-analysis-audit.md`; it did not
discard, stage, commit or revert existing work.
No pre-task snapshot exists, so the exact line-level split between earlier work
and this task is not independently recorded. The task-touched paths are:

- `docs/evidence-baseline.md` (new)
- `docs/stock-firmware-analysis-audit.md` (new)
- `docs/firmware-handoff.md`
- `docs/power-retention-evidence.md`
- `docs/rpm-power-interface.md`
- `docs/pm6125-register-map.md`
- `docs/sdhc-decision-memo.md`
- `docs/sdhc-dependency-trace.md`
- `docs/sdhc-electrical-contract.md`
- `docs/sdhci-host-init.md`
- `porting.toml`
- `scripts/oracle-gta4l-sdhci-host-init.py`
- `scripts/sim-gta4l-sdhci-host-init/harness.c`
- `scripts/test-gta4l-sdhci-evidence-counts.py`
- `scripts/test-gta4l-sdhci-host-init-sim.py`

The later bounded RPM/XBL trace updated `power-retention-evidence.md`,
`rpm-power-interface.md`, `sdhc-decision-memo.md`, `sdhc-electrical-contract.md`,
`stock-firmware-analysis-audit.md`, `pm6125-register-map.md` and `porting.toml`.

A later report batch was recorded with HEAD
`3c3d586dd1feb1d63ade4452800a4ccc7cbcee31` and six dirty paths: five modified
(`docs/evidence-baseline.md`, `docs/kernel-build-recipe.md`,
`docs/sdhc-decision-memo.md`, `docs/sdhc-dependency-trace.md`,
`porting.toml`) and one added (`docs/sdhc-power-pad-dependency-report.md`).
That inventory is a historical pre-commit observation for that batch, not the
current snapshot.

### New paths in this task

- `docs/evidence-baseline.md`
- `docs/stock-firmware-analysis-audit.md`

The pre-existing untracked entries are earlier work in the same SDHC evidence
effort. They are listed so the baseline is reproducible and so unrelated work is
not mistaken for this task's output.

### Modified paths in the pre-existing tree

- `docs/emmc-reference.md`
- `docs/sdhci-handoff-audit.md`
- `docs/sdhci-host-init.md`
- `package/Gta4lSdhciHostInit.c`
- `porting.toml`
- `scripts/sim-gta4l-sdhci-host-init/harness.c`
- `scripts/test-gta4l-sdhci-host-init-sim.py`
- `scripts/test-gta4l-sdhci-host-init.py`

### Untracked paths in the pre-existing tree

- `docs/firmware-handoff.md`
- `docs/linux-regulator-lifecycle.md`
- `docs/pm6125-identity-evidence.md`
- `docs/pm6125-register-map.md`
- `docs/pm6125-usid-research.md`
- `docs/pmic-audit.md`
- `docs/power-path-decision.md`
- `docs/power-retention-evidence.md`
- `docs/rpm-power-interface.md`
- `docs/sdhc-decision-memo.md`
- `docs/sdhc-dependency-trace.md`
- `docs/sdhc-electrical-contract.md`
- `docs/spmi-0x28-analysis.md`
- `scripts/extract-gta4l-firmware.py`
- `scripts/oracle-gta4l-sdhci-host-init.py`
- `scripts/test-extract-gta4l-firmware.py`
- `scripts/test-gta4l-sdhci-evidence-counts.py`

## 2. Current host-tested source hashes

These are the bytes the host checks exercise at the snapshot; they are not the
bytes of the previously booted image `[S]`.

| Artifact | Path | SHA-256 |
| --- | --- | --- |
| Host-init probe source | `package/Gta4lSdhciHostInit.c` | `3f8f45316cff8b3ad19266f901dba69475d7dd0816ce9b6040f2bee37eaebafb` |
| Probe header | `package/Gta4lSdhciHostInit.h` | `a91f76e6c0405315e6df3bca79d067ae798f688dac7b80f665d70f569482f0d5` |
| Simulator harness | `scripts/sim-gta4l-sdhci-host-init/harness.c` | `dd8c92092a6c7a609a81f6f202f5aa4470fdf6375cebd50ec49a828041066cdc` |
| Static host test | `scripts/test-gta4l-sdhci-host-init.py` | `6dd2c1d8ca64527c70b8410797a0361341d6b593b127b8a71a44f624f9580f6f` |
| Simulator test | `scripts/test-gta4l-sdhci-host-init-sim.py` | `fe696332841d247b58d81d2a2c1cd32cf59474eccaa5de64c1ab2a059d3de9b9` |
| Access-policy oracle | `scripts/oracle-gta4l-sdhci-host-init.py` | `488e2d0150692569fe20303facd4b7be3e5afa79e680f6c3785cad35bc570990` |
| Count-consistency test | `scripts/test-gta4l-sdhci-evidence-counts.py` | `f219e3cd281040e3d0c5731f626275ab8a943d119c4bfd141d8660079fed7f59` |
| Host-init policy document | `docs/sdhci-host-init.md` | `a50d093c379c10604291bcb135399878a96e83b24141d58ec6171411f7b206af` |
| Decision memo | `docs/sdhc-decision-memo.md` | `9dd6b5fcaf6eaa4f543d3140fc946ccd9d9ff27f33919b5e3e9000e704369bba` |
| Evidence-gap checklist | `docs/sdhc-evidence-gap-checklist.md` | `4299b4f7e03e56ed5927642edf231b214172d9e6fe6ea653ef9ae7b100067c17` |
| Electrical contract | `docs/sdhc-electrical-contract.md` | `88cd9d6a4ac15ea5e9d738f947db0127d4b929a0c24a3713873e927c29907593` |
| Retention evidence | `docs/power-retention-evidence.md` | `eb91725bd6a63acded9e63ca0673670f36b43a3253da15d8ec17cb67f0387e07` |
| RPM interface | `docs/rpm-power-interface.md` | `4f3fe2e8527a9c334de0478f4a250a49d443b6e552eb4845f75e0cfc44a35e49` |
| PM6125 register map | `docs/pm6125-register-map.md` | `501672b50a86319b7bd095a92b674c162ccc485963e510a708649aa985486c22` |
| PMIC audit | `docs/pmic-audit.md` | `6fe005441dbc987313e6f9d2b810286158a23000cbd604fe30d51e86c50d3dae` |
| SPMI `+0x28` analysis | `docs/spmi-0x28-analysis.md` | `3b3b6e7485ac5939df400bbba1cb8146288961a7cd1df2f220e0dc2627bf3933` |
| RPM LDOA analyzer | `scripts/analyze-gta4l-rpm-ldoa.py` | `d967e640ca8f6f7eb0607d0a51aea9b541b21901ed4bf5a0a3e9be090f2fa4a8` |
| RPM LDOA analyzer self-test | `scripts/test-gta4l-rpm-ldoa-analysis.py` | `c670e2788044bd8b5054b19452dde8080ee9dc93d794ffe8abf3e2890e202afc` |
| Dependency trace | `docs/sdhc-dependency-trace.md` | `7399f421fd11db96fcea80e9c3f5bafc178e2d99099391e293188eb0dd8045b9` |
| Power/pad dependency report | `docs/sdhc-power-pad-dependency-report.md` | `35005a995918eaaa0da69d3f50238609c32d202fa0eca1945a4624dd170eecc2` |
| Decision update 2026-10-30 | `docs/sdhc-decision-update-2026-10-30.md` | `09cfa0d63131124374804ce5bb2b0bffa04de0a6d2ac0f70cd6e6174f1b81cb7` |
| Firmware handoff | `docs/firmware-handoff.md` | `3007de28ad245ae3a268f42fe4ffa9fda289e55473135b6e8a89207ee751cf19` |
| SdccDxe decode test | `scripts/test-gta4l-sdccdxe-decode.py` | `a346959cc424c46be49dccc4c6e6392f776a5297d938f72796e1b34bc2bffd78` |
| SPMIDxe descriptor-base test | `scripts/test-gta4l-spmidxe-descriptor-base.py` | `c835ee091399eba8c236e0cf5c268bf5d5b418ed77438b41297bb274dace436d` |
| Supplied-analysis audit | `docs/stock-firmware-analysis-audit.md` | `1185e1a702efa047a3e5b3e195ec126f1d62070a82d49397efa31ae0dd6f2dbf` |
| Vendor SDHC power sequence | `docs/vendor-sdhc-power-sequence.md` | `73028c8c605d44245f9c57b339c22ddefc8ca41c35b45bfc234a916c2563c995` |
| Repository README | `README.md` | `0f124426c44734d5669d626d87b99c16f0653870860e8f40c0200aa51c083d9c` |
| eMMC reference | `docs/emmc-reference.md` | `0d605ec482d5b77ae428dead608543cb157e331d90ec0a68a31457dcf0ec44e9` |
| NativeEmmc audit | `docs/native-emmc-reference-audit.md` | `c123577953c3c8625e2119967a112826448dd0a95c861023a9593bca0dc1a215` |
| Porting record | `porting.toml` | `700f710ad0ec38fc3961dadedae9691262c6fedd59cafaa9a79d41b798d9d1f0` |
| Porting-approach reassessment | `docs/porting-approach-reassessment-2026-10-31.md` | `ad77a51fbe36ab78333ba7c62e3f606ebbcede1549bfebc0bc5a08f777e8fa71` |
| Arch Linux gta4l evidence | `docs/archlinux-gta4l-evidence.md` | `9704e402411a31cbffe6dd1c1318c678e9275f96506728e18fe6c761116b13a7` |
| eMMC driver record | `docs/emmc-driver.md` | `63771a6a5939eace7804b99da4657b226bc33440ed78c7a7907ef1e85720da09` |
| eMMC core header | `package/Gta4lEmmc.h` | `09b58ad583aeca6a7adaec940412fdaab0e084936cefbaf6862fc7616105d1ff` |
| eMMC core source | `package/Gta4lEmmc.c` | `1bafbf94117e41e75c306436c6968920e7aa02c522144839f1a29cb3e3b2b326` |
| eMMC XNU probe | `package/Gta4lEmmcProbe.c` | `9593c75855a7349d4b33cd0d36486c515ec68388e775bd485577e5d7e66daaec` |
| eMMC simulator | `scripts/sim-gta4l-emmc/harness.c` | `4d29854adf71f03e17bc53de18f3302a49c989915fc97a953bd9c386e8a40cee` |
| eMMC host test | `scripts/test-gta4l-emmc.py` | `e842f600364044c3827bc702b4ed365ca939a83e126bc1e58072302ee4ae2c66` |
| eMMC archive builder | `scripts/build-gta4l-emmc-probe.py` | `c0d742df56cbf69c499537e077be6b590ea1e89e33f656b21c3215561dae0157` |
| RAM-root fixture reproduction | `docs/ram-root-fixture-reproducibility.md` | `1ed6e08627978c130e3810145cfd6c3f579b251514a3c03d024aa5cf9018a3b7` |
| RAM-root fixture recipe test | `scripts/test-gta4l-ram-root-fixture-recipe.py` | `4366ea84438e84ee70d2c0ee9b911d0b5d58817352d27e0076829eedffdc1f8b` |
| Diagnostic payload/boot-image inventory | `docs/diagnostic-payload-input-inventory.md` | `35a3070a264470b9f444b6b4ba66ca6eed28b68b91ab4fccf9dd1b70057521a2` |
| Diagnostic payload build recipe | `docs/payload-build-recipe.md` | `4c7ddf9f62fd8921481ec0a2304fc0ce845a9ba46f7c3d2a6648e7c650002ab5` |
| ADT seed normalizer | `scripts/normalize-gta4l-adt-seed.py` | `cf80caafdd3b56fa590fed208e17e2eb8f5c27be1451dbaf1c57b0bbb7f76cf5` |
| Diagnostic payload builder test | `scripts/test-gta4l-diagnostic-payload.py` | `f293bee964bdd3bebbf40e4c8099a426780679065f3d65abceb9999255480558` |

The table has 45 rows: the sixteenth batch added the eMMC core, probe,
simulator, test, builder and documentation rows, plus the Arch Linux evidence
row. The seventeenth batch refreshed the eight changed-file hashes without
adding a row. The §6 self-check expected 35 until an earlier correction, then
36, then 37, and now expects 45.

The active tree carries the committed `bf2e7d4` source (`3f8f4531...`, the
qualified `HOST INIT REPORT` wording). The historical `bc228be7...` source
(`HOST INIT OK` wording) was used only for the isolated kernel replay and the
reproduced-kernel evidence packets; see `kernel-build-recipe.md` §5 and
`out/gta4l/ram-root-sdhci-repro-kernel-host-init-evidence/README.md`.

The HostInit source correction was a working-tree restoration to the existing
HEAD blob `3f8f4531...`; it created no content diff and is therefore absent
from both commits. The regression it removed was an uncommitted local revert to
`bc228be7...`. In the first follow-up snapshot, `porting.toml` changed only the
diagnostics finding that summarizes the power/pad report and names the new
decode regression test; it is not a build-input change.

## 3. Pinned Linux source cache

| Item | Value |
| --- | --- |
| Repository | `https://github.com/limonabisi/linux-gta4l` |
| Branch and revision | `gta4l-7.2`, commit `cf5f763f0ef8a5b4d8bb4163833cba1160c51894` |
| Cache path | `/tmp/opencode/linux-verify/` |
| Cache shape | 26 flat files; **no `.git` directory** |
| Sorted `sha256sum *` manifest digest | `35b55f07119534f899da80ba1bae933b1bba67f91ec776553f21a91a150987e2` |

```text
98192e8a5bd656dd68fe624d03e4a48a6964b4066a3453268d32b05440e2ea90  board.dts
9896430492f89adde15547b72780e8e795dcdf2a7b46c87237fa69941172f8fd  core.c
9207beba399bf366902fcebf6a57fbb890c9f0289c37a301a3513b4d9d232f97  gcc-sm6115.c
45a71ac0cfb9b9d07bcb15eb93133ba0aa847aea1428ef072f75ba6e971b7bf5  icc-core.c
7e125d9308913c32803b0d482c070fb68356a00d19e0e9a687ac75fd5208fec0  icc-rpm.c
76792d3702e438e44a44ca93d86827940baa764833391319a8f887829e21b83c  icc-rpm.h
56678ec9c220d952b63f4e2af7072f7d3b815cd867c7809c937b5f38cddd5304  mmc-core.c
3eee0021701861692f738d359e0773af8eb046633b33988edb03c0b90c488777  opp-core.c
5f181cb21628ed3043fbf5329875fc56b74564770b3755ad8799502811f8e95f  pm6125.dtsi
32e2f6b0988eba52a2f662fdc2d7132022fbe980f281b2a82babcf3d7f46e18a  pmdomain-core.c
d6638c03c09f5c0b24d9e343e19fb04ad7d483ec4f1c5fdabde4efc3570d59f8  pinctrl-msm.c
68e7845e7f785a84c476b03b347b09fbf6789de991f4d0684b81d2af6354d814  pinctrl-msm.h
c993ffc565c2bb3816ea808d45e23b160c5aa4b77c45a0a176d932307faeaa87  pinctrl-sm6115.c
2da9e5b25ed33eaff3ebd0c8e497c0fc045c019156e686a8d5675e21e6fc9c57  pmi632.dtsi
cba250364921bfff3e8cfe10372906631cf2cba70365b2d75eabb8822084b9bd  qcom-spmi-pmic.c
4148ae42b825fecf124e964e585f775597a77e99df83e274f7236d1640c32370  qcom_smd-regulator.c
09436b8793dc69bf0805f941512b796b0831507d0c7d9d566ce186b6031c0dbc  qcom_spmi-regulator.c
9896430492f89adde15547b72780e8e795dcdf2a7b46c87237fa69941172f8fd  regulator-core.c
6c6a0b277d001297d11cddeeb87e9f4367387a622dcfc6ca42c000754e0a722a  regulator.c
4aba9e20b5e68105fb68a378011248ad016b439f064b22fb6e56281c38c459e3  rpmpd.c
3ba6c4baf1adeaa90043f46db08dfc9df2809209b4e29386e9148d26da827a9a  runtime.c
b668e01dfcb6efac44249c287c6b8583b0a3d584f497e82d33df223cee61f53e  sdhci-msm.c
186305e102bd32df419ff80669e6018d6b3a9e4310c3101193413970355b9857  sdhci.c
d4cff8991af9bde1820f872b75bcb8259b8a0e4c87cf976582b61674707d7efb  sm6115.c
5b91a04c3119f2c846b620e1dc9fd50ce6ed61f07cf1ef2674c640c23a432d9c  sm6115.dtsi
320e71b64915eb4e32be90b49fd65f12da2241e877bf4f68f0c0c1eb4ab3e28d  spmi-pmic-arb.c
```

The cache has no git metadata, so the revision is a documented fetch provenance,
not a property that can be independently verified from the cache alone. If the
cache is lost, re-fetch the listed files from the pinned commit and compare the
manifest digest `[S]/[U]`.

## 4. Firmware and boot artifacts

| Artifact | Path or identity | SHA-256 | Status and relationship |
| --- | --- | --- | --- |
| Booted corrected v2 image | `out/gta4l/ram-root-sdhci-host-init-evidence/boot-ram-root-sdhci-host-init-v2.img` | `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133` | Present in the gitignored output tree; not tracked by git `[S]` |
| Local firmware volume | `SILICIUM_UEFI.fd` | `3d5dbd6ff28093e38a16c02af813fe7d2624d8e9b83ff407045e467e86904f5a` | Present; gzip-wrapped `[S]` |
| Local decompressed source | inner extraction of the above | `7c20a5b9165100a2f07edc58d2b348e576b372e368af0ebd6ecdeceac50ba10e` | 3,145,840 bytes; 112-byte prefix plus outer FV `[S]` |
| Local outer FV | inner extraction of the above | `4a8ca2cddcd578391dfd846ae994dafc7590b68e1e7ebde8f86ceca0c904b676` | 3,145,728 bytes; differs from booted outer FV `42f900a3...` `[S]` |
| Local inner FV | inner extraction of the above | `2443e52a31a3208c70d10e5a6a733c285de48dc1b48358f626cc1557f7d5f6b5` | 6,991,872 bytes; 90 PE modules; differs from booted inner FV `3bb7847a...` `[S]` |
| Booted outer FV | recorded in `firmware-handoff.md` | `42f900a3...` | 3,145,728 bytes `[S]` |
| Booted inner FV | recorded in `firmware-handoff.md` | `3bb7847a...` | 8,339,456 bytes `[S]` |
| SdccDxe PE | `../Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/SdccDxe/SdccDxe.efi` | `4084bbc745d66fd81eae0caf7eacb19f7204076d4ee88adc7cda45c949d5674b` | Same bytes in the local volume and the booted image `[S]` |
| PmicDxeLa PE | `../Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/PmicDxe/PmicDxeLa.efi` | `db541f6302efee1546beea9b4dee9362fca5f50bcec2f494e560c150464e32ce` | Same bytes in the local volume and the booted image `[S]` |
| SPMIDxe PE | `../Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/SPMIDxe/SPMIDxe.efi` | `50be2e1a0972843af5de67d4d0b80aba68634259849459897922716a25d3e254` | Same bytes in the local volume and the booted image `[S]` |
| UEFI platform config | `../Mu-Silicium/Binaries/gta4l/RawFiles/uefiplat.cfg` | `1b8c0b8035d8b858dc8c7a763ab3b874dd16f8221295cbab116879bcf08f9c3d` | Present `[S]` |
| RPM image | `../../device-dump/private/gta4l-dump/partitions/rpm.img` | `272bebd3ddd7ef223d6e95e9b4cd30385a3df0eb4fa7beae6b622f457b4937c6` | Private device dump; static analysis only `[S]` |
| XBL image | `../../device-dump/private/gta4l-dump/partitions/xbl.img` | `a09c7b62e639a0565c30b7a3b61e2d68f2a07bfe9c3199d38f9af17bdf9e073e` | Private device dump; ELF64 AArch64 `ET_EXEC`, 17 program headers, no sections; 15 `LOAD` segments including `0x5fc00000`; policy not analyzed `[S]/[U]` |
| XBL config image | `../../device-dump/private/gta4l-dump/partitions/xbl_config.img` | `6dbaad63204b761e7d8fb4dba7c861d97b25dc5094aabc0a9fe45f19fb20d99e` | Private device dump; embeds `/pmic_settings.bin` at `0x3054`, size `0x7240`, SHA-256 `410e45ac...f4e97597`; static analysis only `[S]` |
| Supplied stock-firmware analysis | archive from https://github.com/NoNameBuilder; not vendored | `6d750114c4465e894a043ce190ea3a776d25a3bf862d5b50df3d95d73f2368e9` | 214/214 manifest-listed files match; supplied XBL bytes and DT overlays differ from the captured state; audit in `stock-firmware-analysis-audit.md` `[S]/[U]` |
| Samsung CXG1 source archive | owner-supplied `sos/` archive; not vendored | `84e8f9934987834da6867d37243bb18fe6fab623019e3d7a4e10fa603f069f8e` | Kernel 4.19.157, `gta4l_mea_jt_defconfig`; official vendor source; not the shipped binary `[S]/[U]` |
| Samsung CWI2 source archive | owner-supplied `sos/` archive; not vendored | `b00cbe4827b62af8ea888b89b000e31368b644e5ec0c07ba6b65a164718081fb` | Comparison release; relevant files byte-identical to CXG1 `[S]` |
| Live FDT blob | `../../device-dump/private/gta4l-dump/live-fdt.dtb` | `da1cb76fcf9031de8ee2f1152456994220d3d01a2a2b73b211b684e92a0b6ebc` | Captured vendor FDT `[S]` |
| Decompiled live DTS | `../../device-dump/private/gta4l-dump/live.dts` | `81c1865589df632ae76b77edd44eb9326b317f8abef471465504618636a9b629` | Text used for DT citations; not the booted blob `[S]` |

The local `SILICIUM_UEFI.fd` is **not** the booted image. It packages the same
three driver PEs, but its outer and inner firmware volumes differ and its
`DxeCore` is `f37f7d2b...` rather than the booted `f4aefa94...`
(`firmware-handoff.md`). The booted artifact is present locally in the
gitignored output tree, not tracked by git; its hash is a recorded provenance
value, not a reproduction. Do not present the current tree or the local
firmware volume as the previously booted image `[S]`.

Private firmware and device dumps are referenced by path and hash only; their
contents are not duplicated in this repository `[S]`.

Build provenance note: the Mu-Silicium parent is at `e14626a3...`; its
`Mu_Basecore` submodule carries 10 uncommitted platform edits at this snapshot.
The payload and boot images tested here preserve the v5 baseline firmware bytes
and do not rebuild firmware from that subtree. A future firmware rebuild must
pin or discard those edits `[S]`.

## 5. Evidence categories

| Category | What it is | What it is not |
| --- | --- | --- |
| Current host-tested source | Probe, harness, simulator, oracle, counts and policy documents at the hashes above | Not a hardware, electrical or retention result `[S]` |
| Official vendor kernel source | Samsung CXG1/CWI2 source archives, pinned by ZIP/tarball hash | Not the shipped binary, not a board schematic; the archive contains no vendor DTS `[S]/[U]` |
| Previously booted controller-local image | Recorded v2 image hash and its controller-local result | Not reproduced by this tree and not a power result `[S]` |
| Captured vendor DT | `live-fdt.dtb`/`live.dts` board description | Not proof of the XNU-time rail, pad or regulator state `[S]` |
| Static firmware analysis | RPM LDOA resource tables, XBL boot PMIC settings and Mu-Silicium exit traces | Not runtime behavior; the RPM consumer/policy, PBS semantics, indirect effects and untraced callbacks remain `[U]` |
| Supplied third-party analysis | stock-firmware archive from https://github.com/NoNameBuilder, manifest-checked and audited | Not authenticated, not the captured XBL or live FDT, and not a measurement `[S]/[U]` |
| Physical measurements | None | No voltage, current, transient, pad or retention measurement exists `[U]` |

## 6. Reproduction

Run from `src/UniXNU-gta4l/`:

```sh
git rev-parse HEAD
git status --porcelain
sha256sum package/Gta4lSdhciHostInit.c package/Gta4lSdhciHostInit.h \
  scripts/sim-gta4l-sdhci-host-init/harness.c \
  scripts/test-gta4l-sdhci-host-init.py \
  scripts/test-gta4l-sdhci-host-init-sim.py \
  scripts/oracle-gta4l-sdhci-host-init.py \
  scripts/test-gta4l-sdhci-evidence-counts.py \
  scripts/test-gta4l-sdccdxe-decode.py \
  docs/sdhci-host-init.md docs/sdhc-decision-memo.md \
  docs/sdhc-evidence-gap-checklist.md \
  docs/sdhc-electrical-contract.md docs/power-retention-evidence.md \
  docs/rpm-power-interface.md docs/pm6125-register-map.md \
  docs/sdhc-dependency-trace.md docs/firmware-handoff.md \
  docs/stock-firmware-analysis-audit.md \
  docs/vendor-sdhc-power-sequence.md README.md \
  docs/emmc-reference.md docs/native-emmc-reference-audit.md \
  docs/sdhc-power-pad-dependency-report.md \
  docs/sdhc-decision-update-2026-10-30.md \
  docs/ram-root-fixture-reproducibility.md porting.toml \
  docs/pmic-audit.md docs/spmi-0x28-analysis.md \
  scripts/analyze-gta4l-rpm-ldoa.py scripts/test-gta4l-rpm-ldoa-analysis.py \
  scripts/test-gta4l-ram-root-fixture-recipe.py \
  scripts/test-gta4l-spmidxe-descriptor-base.py
( cd /tmp/opencode/linux-verify && sha256sum * | sort -k2 | sha256sum )
python3.11 -B scripts/test-gta4l-sdhci-host-init.py
python3.11 -B scripts/test-gta4l-sdhci-host-init-sim.py
python3.11 -B scripts/oracle-gta4l-sdhci-host-init.py
python3.11 -B scripts/test-gta4l-sdhci-evidence-counts.py
python3.11 -B scripts/test-gta4l-sdccdxe-decode.py
python3.11 -B scripts/test-gta4l-emmc.py
python3.11 -B scripts/test-gta4l-spmidxe-descriptor-base.py
python3.11 -B scripts/test-gta4l-ram-root-fixture-recipe.py
python3.11 -B scripts/test-gta4l-rpm-ldoa-analysis.py
python3.11 -B scripts/analyze-gta4l-rpm-ldoa.py \
  ../../device-dump/private/gta4l-dump/partitions/rpm.img --code
```

The analyzer command needs the private `rpm.img`; it refuses a different image
hash unless `--expect-sha256` is given explicitly. `--code` checks the
documented Thumb call sites, literals, destination registers, registration
setup and class registration. The self-test uses only a synthetic image `[S]`.

`scripts/test-gta4l-sdccdxe-decode.py` reads the sibling Mu-Silicium
`SdccDxe.efi` when present and otherwise extracts the same pinned PE from the
committed `SILICIUM_UEFI.fd`. It pins instruction bytes and models the loop
behaviour; it does not execute firmware `[S]`.

The `fs-timer` fixture reproduction is recorded in
`docs/ram-root-fixture-reproducibility.md`; it rebuilds the signed fixture,
trust cache and root image byte-for-byte with the pinned cctools-port and
`rcodesign` versions. The hash table above can be checked mechanically:

```sh
python3.11 - <<'PY'
import hashlib, pathlib, re
text = pathlib.Path('docs/evidence-baseline.md').read_text()
rows = re.findall(r'^\| [^|]+ \| `([^`]+)` \| `([0-9a-f]{64})` \|$', text, re.M)
expected = 45
paths = [path for path, _ in rows]
assert len(rows) == expected, f'expected {expected} rows, found {len(rows)}'
assert len(set(paths)) == len(paths), 'duplicate paths in the table'
bad = []
for path, want in rows:
    got = hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
    if got != want:
        bad.append((path, got, want))
print(f'rows={len(rows)} mismatches={len(bad)}')
for path, got, want in bad:
    print(f'{path}: {got} != {want}')
raise SystemExit(1 if bad else 0)
PY
```

### 6.1 Validation record

After the final edits, the host checks were run from `src/UniXNU-gta4l/` with
`PATH=/tmp/opencode/py311/bin:$PATH` and `LC_ALL=C`:

| Check | Result |
| --- | --- |
| `scripts/test-gta4l-diagnostic-payload.py`, `test-gta4l-diagnostic-package.py`, `test-gta4l-boot-image.py`, `test-gta4l-sdhci-handoff-audit.py`, `test-gta4l-sdhci-evidence-counts.py`, `test-gta4l-xnu-mtimes.py`, `test-gta4l-ram-root-fixture-recipe.py`, `test-gta4l-rpm-ldoa-analysis.py`, `test-gta4l-sdhci-host-init.py`, `test-gta4l-sdhci-host-init-sim.py`, `oracle-gta4l-sdhci-host-init.py`, `check-gta4l.py` | All exited 0 |
| `scripts/test-gta4l-sdccdxe-decode.py` (static PE/model check, separate from the diagnostic tests) | PASS: pinned SdccDxe SHA-256, handshake and pad instruction words, MOVZ-derived loop bounds, last-read and delayed-completion boundary cases, wrong-bound negative controls, captured-status selection, three non-aborting timeout paths, TLMM arithmetic, SDC1 pad decode and SDC2 CLK-pull field positions |
| `scripts/test-gta4l-spmidxe-descriptor-base.py` (static PE/scan check, separate from the diagnostic tests) | PASS: pinned SPMIDxe SHA-256, `0x38b0` read/override words, `0x38c8` caller and `.data` layout, one `0x8878` read, no write intersecting `[0x8878,0x8880)`, resolved caller stores at `0x38bc`/`0x38f4`, 150 unresolved non-stack loads and 36 unresolved non-stack stores, explicit revisited-state/step/unsupported/call-depth/indirect-branch truncations, 64-bit ADD/SUB and effective-address wrapping with the demonstrated overflow regression, paired access/writeback and single/paired address-space-boundary controls, a boundary-crossing load that invalidates its derived store, ADR-to-XZR, and pointer-chain, overlap, partial-store, pre/post-index, call-clobber, XZR/SP, XZR-root, branch-separation, callee-conditional, revisit, stale-pointer, cross-call, truncation and decoder controls; all six result sets are unchanged from the audited `4c447b9` baseline in a site-level set comparison with zero added and zero removed records in accesses, stores, calls, boundaries, unresolved and clobbers |
| Workflow token scoping (offline harness) | PASS: all 5 extracted patch strings attach the dummy token only to HTTPS GitHub hosts and strip it on cross-host redirects; no real token was used |
| Official Samsung source comparison | PASS: CXG1/CWI2 ZIP and tarball hashes recorded; kernel 4.19.157; six relevant driver files byte-identical; no vendor DTS |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| Native eMMC host test | PASS: 37 simulator scenarios and 12 mutation controls; the production core compiles with `-std=c11 -Wall -Wextra -Werror` and with the real `bsd_init.o` compile record |
| eMMC kernel link | PASS: `./unixnu build kernel -D ../UniXNU-gta4l` links the probe through `CONFIG_XNU_EXTRA_LD_ARCHIVES`; `kernel.macho` sha256 `152bde801bc7f97f470e46713314e9daa62cb3b8a59f46a0f48ce183e0529957` |
| eMMC stage-2 payload | PASS: 80 MiB raw payload sha256 `baad6342c950d847912ede0ed06f3e14ecc012e022101049f37f8f4a69dd7b39`, kernel args include `gta4l-emmc-probe=2`, payload contract 7/7 |
| eMMC stage-2 boot image | PASS: 85,688,320-byte image sha256 `c7011274991b8afd83d5965392941c78d800dbe7b3b8715b939e7f27cf678e05`, boot-image gate 18/18; `hardware_tested=false`, not flashed |
| XNU probe compile | PASS: `Gta4lEmmc.c` and `Gta4lEmmcProbe.c` compile against the pinned XNU headers with `-Wall -Wextra`; the archive exports `gta4l_emmc_probe_preflight`, `gta4l_emmc_probe_run`, `gta4l_emmc_init` and `gta4l_emmc_read_block` |
| Patch-series apply | PASS: `sheng-platform-power`, `gta4l-sdhci-snapshot`, `gta4l-sdhci-handoff-audit` and `gta4l-emmc-probe` apply in order to the clean `bsd_init.c` |
| Reassessment re-run 2026-10-31 | PASS: `test-gta4l-sdhci-host-init.py`, `test-gta4l-sdhci-host-init-sim.py`, `oracle-gta4l-sdhci-host-init.py`, `test-gta4l-sdhci-evidence-counts.py`, `test-gta4l-sdccdxe-decode.py`, `test-gta4l-spmidxe-descriptor-base.py`, `test-gta4l-ram-root-fixture-recipe.py`, `test-gta4l-rpm-ldoa-analysis.py` and `check-gta4l.py` all exited 0; `git diff --check` clean |
| `porting.toml` parse with `tomllib` | 13 areas |
| `git diff --check` | Clean |

The host checks are read-only and do not build, flash, boot, power or access a
device `[S]`.

## 7. Limits

- The hashes are a snapshot of a dirty tree; any later edit invalidates them.
- The pinned Linux cache has no `.git`; its revision is documented provenance,
  not a verifiable git checkout `[U]`.
- The booted image is not tracked; the copy in the gitignored output tree
  matches the recorded hash `[S]`, but reproduction of its firmware and kernel
  inputs from the current checkout is not established `[U]`.
- The local firmware volume is a different build; only the three driver PEs are
  byte-identical to the booted drivers `[S]`.
- No physical measurement exists; the measurement-dependent parts of `E1`–`E9`
  remain `[U]`.
- The official vendor source is CXG1/CWI2, not the captured CXB1; source/binary
  correspondence and the LineageOS-only card-detect blocks remain `[U]`.
