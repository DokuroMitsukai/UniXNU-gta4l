# SDHC bus-power activation decision memo (Step 6)

Date: 2026-10-29

Decision: **NO-GO**

Operation: `sdhc-bus-power-activation` (bus power on, card clock off, no card
command)

Board: Samsung Galaxy Tab A7 10.4 (2020), `SM-T505N`, `gta4l`, SM6115

Status: host-only decision record. No device connection, device image build,
flash, boot, power change or commit was made for this memo `[S]`. The decision
applies only to the operation below; it does not authorize card commands, PIO,
DMA, high-speed modes, tuning, CQE, inline encryption or storage access `[S]`.

Tags: `[S]` source- or DT-established, `[C]` conditional inference, `[U]` not
established by the available evidence.

## 1. Operation and exclusions

| Field | Definition |
| --- | --- |
| Starting point | Fresh handoff or the post-`Gta4lSdhciHostInit` state: v5 MCI version valid, software reset self-cleared, internal clock stable, bus power off, card clock off, CMD/DAT inhibit clear, GCC SDCC1 branches enabled/unhalted, block reset deasserted `[S]`. L24/L11, pad and CX state are not established `[U]`. |
| Intended action | Enable SDHCI bus power at the DT-requested voltage; read `PRESENT_STATE` and `CORE_PWRCTL_STATUS`; read `CLOCK_CONTROL` only to confirm the card-clock bit stays clear; report the result. The candidate as a whole is `[C]`; individual register facts remain `[S]`. |
| Explicitly excluded | Card-clock enable, card command/response, PIO, data transfer, DMA, CQE, ICE, tuning, high-speed mode change, GCC branch/RCG/PLL/BCR write, PMIC/SPMI access, RPM vote, storage write and partition change `[S]`. |
| Vendor acknowledgement | Not included. `CORE_PWRCTL_CLEAR`/`CORE_PWRCTL_CTL` would require a separately approved design; the current prohibition remains `[S]`. |
| Required end state | Bus power on with the card clock still off, no command or transfer issued `[C]`. On abort: bus power off, card clock off, reset deasserted, state reported `[C]`. Acceptance criteria are not established `[U]`. |
| Success observation | Bus-power bit set, card-clock enable bit clear, `INT_STABLE` only reflecting the already-established internal clock, no error interrupt, vendor status reported not acknowledged `[C]`. No card response is expected because no command is issued `[S]`. |

The existing controller-local diagnostic is a different, narrower operation. It
is already implemented and was observed on the tablet under its own approval;
this memo does not re-authorize it and does not transfer its result to bus-power
activation `[S]`.

## 2. Evidence packet

| Item | Reference | Status |
| --- | --- | --- |
| Exact operation and exclusions | Section 1 of this memo; `sdhc-electrical-contract.md` §1 | Defined `[S]` |
| Board and controller | `SM-T505N`/`gta4l`; `sdhci@4744000`; `README.md`, `dts/gta4l.dts`, `board.toml` | Identified `[S]`; board revision and UFS population `[U]` |
| Pinned source revision | `limonabisi/linux-gta4l`, branch `gta4l-7.2`, commit `cf5f763f0ef8a5b4d8bb4163833cba1160c51894` | Pinned `[S]` |
| Captured vendor FDT | `device-dump/private/gta4l-dump/live-fdt.dtb` SHA-256 `da1cb76fcf9031de8ee2f1152456994220d3d01a2a2b73b211b684e92a0b6ebc`; decompiled `live.dts` SHA-256 `81c1865589df632ae76b77edd44eb9326b317f8abef471465504618636a9b629` | Captured, post-unlock, not authenticated stock `[S]` |
| Booted host-init image | v2 boot image SHA-256 `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133`; payload `0afaae8d...`; present locally in the gitignored output tree, not tracked by git | Prior controller-local result, not a power result `[S]` |
| Firmware provenance | `SdccDxe` `4084bbc7...`, `PmicDxeLa` `db541f63...`, `SPMIDxe` `50be2e1a...`; `firmware-handoff.md` | The traced driver bytes are the booted bytes `[S]`; the current checkout may not reproduce that exact boot `[S]` |
| Step 3 findings | `rpm-power-interface.md`, `power-retention-evidence.md`, `pm6125-identity-evidence.md`, `pmic-audit.md`, `spmi-0x28-analysis.md`, `firmware-handoff.md` | Bounded source/firmware findings, not measurements `[S]` |
| Supplied stock-firmware analysis | `stock-firmware-analysis-audit.md`; analysis credited to https://github.com/NoNameBuilder | Package manifest matches internally; XBL bytes and DT overlays differ from the captured state; several supplied claims corrected; no measurement `[S]/[U]` |
| Evidence baseline | `evidence-baseline.md` | Current-tree revision, dirty state, file hashes, pinned-source cache provenance and booted-image relationship recorded `[S]` |
| Step 4 contract | `sdhc-electrical-contract.md` | `E1`–`E9` safety-critical areas blocking `[S]` |
| Step 5 software evidence | `sdhci-host-init.md` §Host access-policy verification; `scripts/test-gta4l-sdhci-host-init-sim.py`; `scripts/oracle-gta4l-sdhci-host-init.py` | 33 scenarios pass, ten source mutations and forty-one synthetic trace controls rejected, an initially-zero interrupt baseline accepted, separate-implementation oracle (same author) passes `[S]` |
| Starting-state evidence | `sdhci-handoff-audit.md`, `sdhci-host-init.md`, `host-init-success.txt` | Controller-local only; rails/pads/CX not measured `[U]` |
| Power-only artifact | None | No bus-power implementation, image or access policy exists in this tree `[S]`; existence outside the tree is not established `[U]` |
| Recovery | BOOT-partition backup/rollback and payload gates | Software rollback only; rail recovery absent `[U]` |

No image was built for this memo `[S]`. The current tree contains host-only test
and documentation changes; it has not been flashed, and the v2 tablet result is
not re-claimed for the current source `[S]`.

## 3. Gate table

| Gate | ID | Required outcome | Result | Evidence | Status |
| --- | --- | --- | --- | --- | --- |
| Scope / provenance | `S2` | Exact operation, board and artifacts identified | Operation, board, controller and prior boot provenance identified; no bus-power artifact; board revision and UFS population unknown | Sections 1–2; `sdhc-electrical-contract.md` §2 | Partial — fails |
| Electrical contract | `E1`–`E9` | No unresolved safety-critical requirement for this operation | All nine areas (supply voltage, IO compatibility, current/loading, transients, sequencing, back-powering, retention, fault handling, recovery) are blocking | `sdhc-electrical-contract.md` §3–§4 | Fail |
| Starting state | `S2`, `E1`/`E2`/`E7` | Required rail, pad, clock and controller state established | Controller state observed; L24/L11, pad domain, CX and retention are not measured | `sdhci-handoff-audit.md`; `power-retention-evidence.md` | Fail |
| Access policy | `S1` | All intended accesses justified; prohibited paths excluded | Existing policy forbids bus-power enable, card-clock enable and power-status acknowledgement; no approved bus-power policy exists | `sdhci-host-init.md` §5.2; this memo §1 | Fail for candidate |
| Software behavior | `S1` | Applicable deterministic and negative tests pass | Host-local diagnostic passes 33 scenarios, ten mutations and the oracle; no bus-power tests exist | `sdhci-host-init.md` §5; `oracle-gta4l-sdhci-host-init.py` | Pass for capability 1 only — fails for candidate |
| Independent assessment | `S3` | No unresolved safety-critical finding | Oracle is a same-author software cross-check, not an independent electrical review; electrical blockers remain | `sdhci-host-init.md` §5.7; `sdhc-electrical-contract.md` §6 | Fail |
| Recovery | `E9` | Procedure and acceptance criteria established | Software rollback is proven; no rail-level safe-stop, discharge/power-cycle or acceptance criteria | `sdhc-electrical-contract.md` §3–§5 | Fail |
| Authorization | `S4` | Explicit owner approval for the exact procedure | No approval for bus-power activation; the card clock remains off; prior host-init approval does not transfer | This memo; `AGENTS.md` | Fail |

## 4. Unresolved blockers (canonical IDs)

Electrical-contract areas are `E1`–`E9`, matching the row order in
`sdhc-electrical-contract.md` §3. Non-electrical gates are `S1`–`S4`. These IDs
are the stable references for this decision. Assigning an ID does not close a
blocker `[S]`.

| ID | Requirement | Existing evidence | Classification | Remaining gap |
| --- | --- | --- | --- | --- |
| `E1` | Recommended operating ranges and absolute maxima for eMMC VCC, VCCQ2 and every shared L24/L11 consumer | Vendor DT ranges and requested levels; no PM6125 or eMMC datasheet | Ranges `[S]`; absolute maxima and per-component limits `[U]` | Exact PM6125/eMMC absolute maxima, shared-consumer limits and the values that apply to this board revision |
| `E2` | SDC1 pad domain, signalling levels, pull-up source and permitted powered/unpowered combinations | Vendor and pinned `sdc1_*` states; L11 1.8 V request; UEFI TLMM writes | Declared settings `[S]`; actual pad domain/state and UEFI encoding `[U]` | Actual pad domain at handoff, pull-up source, unpowered-drive limits and the UEFI-write-to-pad mapping |
| `E3` | Shared loads, regulator capability and operating conditions for L24/L11 and their parents | Vendor declared currents (570 mA VCC, 325 mA VDD-IO; UFS 600 mA each); recovery sees eMMC only | Declarations `[S]`; capability, load and UFS population `[U]` | Physical UFS population, actual shared loading, regulator capability and parent loading |
| `E4` | Ramp, settling, inrush, overshoot/undershoot and brownout requirements | SPMI `SOFT_START` register and regulator-core `soft_start` hook exist; no board values | Hooks `[S]`; behaviour and limits `[U]` | Required slew, inrush, overshoot/undershoot and brownout limits; measured behaviour |
| `E5` | Electrical supply, pad, reset and clock ordering, including minimum delays | Linux power-IRQ software order `set_vmmc` → `set_vqmmc` → `set_pincfg` → acknowledge; controller reset and internal-clock stages are separate | Software order `[S]`; electrical order and delays `[U]` | Required electrical order and minimum delays; whether the no-ack scope leaves a safe pending request |
| `E6` | Restrictions on driving CMD/DAT/CLK/reset while a supply is absent; pad leakage | Vendor `vccq2-pwr-collapse-sup` and `qcom,vdd-io-lpm-sup`; no pad leakage data | Properties `[S]`; requirement `[U]` | Which pins may be driven unpowered and whether the card or SoC can back-power a rail |
| `E7` | L24/L11 and pad state across handoff, suspend, shutdown and reboot | Mainline sends no L11/L24 vote; UEFI exit path is not passive; `pwr=00` is controller-local; a bounded RPM/XBL trace finds LDOA tables for both rails but the bounded scan finds no referenced static RPM table or stored pointer/resolvable PC-relative literal that selects either rail, and no traced L11/L24 voltage/mode/enable write; runtime policy `[S]/[U]` | No vote and exit trace `[S]`; firmware tables identified `[S]`; static reference not found `[S]`; retention `[U]` | Actual rail state across each transition; runtime LDOA request path (parser/aggregator), latched per-id state, and runtime-built LDOA descriptors; PBS instruction semantics; indirect UEFI effects |
| `E8` | Relevant PM6125/controller interrupt and status semantics; consequence of clearing or masking; documented acknowledge-or-not decision | SDHCI power-status semantics `[S]`; arbiter `+0x28` interpretation `[S]/[C]`; PM6125 `QPNPINT` applicability `[U]` | SDHCI `[S]`; PM6125 applicability and decision `[U]` | Whether PM6125 uses the `0x14`/`0x16` `QPNPINT` layout, its USID/subtype, and safe pending-request behaviour |
| `E9` | Qualified rail-recovery procedure and acceptance criteria | BOOT-partition backup/rollback and payload gates | Software rollback `[S]`; rail recovery `[U]` | Safe-stop, discharge/power-cycle procedure, verification measurement and independent review |
| `S1` | Bus-power implementation, explicit access policy, bounded waits, trace oracle and negative tests | Host-local diagnostic policy and tests; no bus-power artifact in this tree | Tree-bounded `[S]`; candidate design `[U]` | Approved design and tests for the bus-power candidate |
| `S2` | Exact board revision, population and starting-state record (rails, pads, CX) | Operation, board, controller, firmware and prior boot provenance identified; controller state observed | Provenance `[S]`; board revision, population and rails/pads/CX `[U]` | Board revision, UFS population and measured starting state |
| `S3` | Independent review of the electrical contract, procedure and recovery | Same-author separate-implementation oracle | Limitation `[S]`; review `[U]` | Independent human assessment |
| `S4` | Separate written owner approval for the exact device procedure | None; prior host-init approval does not transfer | `[S]` | Owner approval after the technical prerequisites are met |

| ID | Acquisition route | Closure criterion | Non-substitute | Disposition |
| --- | --- | --- | --- | --- |
| `E1` | Vendor documentation (PM6125 and eMMC datasheets, schematic/netlist); host-only if available | Datasheets and schematic/netlist establish operating ranges and absolute maxima for VCC, VCCQ2 and every shared consumer | DT ranges, PMIC readback, controller bits | Blocking |
| `E2` | Datasheet/schematic pad limits plus a measured pad state under separate approval | Pad domain, pull-up source and unpowered-drive limits established, and the handoff state measured | DT pinctrl values, UEFI write values, `INT_STABLE` | Blocking |
| `E3` | Schematic/netlist/population evidence; physical inspection and current measurement under separate device approval | Population and shared loading established; regulator capability and measured current within limits | DT declared current, recovery eMMC inventory | Blocking |
| `E4` | Datasheet transient data plus oscilloscope/current-probe capture under separate approval | Ramp, inrush, overshoot/undershoot, settling and brownout requirements established and measured | Soft-start register, regulator-core hook | Blocking |
| `E5` | Datasheet/board documentation plus controlled measurement under separate approval | Electrical order and minimum delays established and verified | Linux software order | Blocking |
| `E6` | Datasheet/schematic limits plus pad-leakage measurement under separate approval | Unpowered-drive restrictions and back-powering limits established | Vendor properties, software reset state | Blocking |
| `E7` | RPM/XBL code trace or vendor documentation plus transition measurements | Initial/active/sleep/reset policy and retention across each transition established | No AP vote, `pwr=00`, DT `always-on`, firmware trace | Blocking |
| `E8` | PM6125 register description/datasheet and non-scanning identity evidence; live PMIC access remains blocked | Fault semantics established and a documented acknowledge-or-not decision with pending-request behaviour | Generic arbiter layout, PMI632 analogy, SDHCI semantics | Blocking |
| `E9` | Written procedure with safe stop, discharge/power-cycle and re-init order; verification measurement; independent review | Qualified recovery procedure and acceptance criteria established | Software rollback | Blocking |
| `S1` | Host-only design, access policy, oracle and negative tests | Approved design passes its policy, oracle and negative tests | Host-local diagnostic result | Blocking |
| `S2` | Documentation plus measurement under separate approval | Complete starting-state record, with the measurement-dependent parts of `E1`/`E2`/`E7` closed | Prior controller-local observation | Narrowed but blocking |
| `S3` | Independent human review | Independent review finds no unresolved safety-critical finding | Same-author oracle | Blocking |
| `S4` | Owner approval | Separate written owner approval for the exact device procedure | Prior host-init approval | Blocking |

These distinctions are part of the disposition, not wording `[S]`: a DT current
request is not measured load or regulator capability; a controller bus-power bit
is not a rail-voltage measurement; an absent AP vote does not prove retention; a
declared UFS consumer does not prove physical population; and software rollback
does not establish electrical recovery. No blocker above is resolved, and no
disposition improves because its wording changed `[S]`.

The bounded trace in `power-retention-evidence.md` §7 narrows `E7` by
identifying the RPM LDOA tables for both rails, finding no referenced static
table or stored pointer/resolvable PC-relative literal that selects either rail
in the bounded scan, and finding no direct L11/L24 voltage, mode or enable
write in the traced paths. The LDOA parser and aggregator at
`0x46151f8`/`0x46153b8` are registered per instance at runtime, and the
aggregator keeps a latched per-id state and applies through `0x4615906` →
`0x4614920`; the separately traced `0x46166c8`/`0x46168dc` pair is the `smpa`
class, and its bridge at `0x4616e10` reaches the class/ID setter at
`0x4615d08` → `0x4617b48` for class byte `0xb`, but whether that class is LDOA
is `[U]`. The class object and per-id array are built at runtime, so the scan
does not prove that any request exists or the runtime bounds `[C]/[U]`. It does
not establish a retention result, so `E7` stays blocking `[S]/[U]`.

## 5. Decision record

**NO-GO.** Blocking requirements remain in the electrical contract, starting
state, access policy, software behavior for this candidate, independent
assessment and recovery. `READY FOR APPROVAL` is not selected because the
technical prerequisites are not satisfied. `APPROVED` is not selected because
no owner authorization for this operation exists.

No approval request is made. Requesting approval while the gates fail would
imply that the technical prerequisites are met, which they are not.

The consolidated dispositions are `E1`–`E9` blocking, `S1` blocking, `S2`
narrowed but blocking, `S3` blocking and `S4` blocking. No gate is resolved.
These dispositions reflect the evidence, not a change in wording `[S]`.

## 6. Conditions that would change the decision

The decision can be revisited only with new evidence, not with general
encouragement or a prior approval `[S]`. Three approvals are separate; evidence
or approval for one does not authorize the next `[S]`.

### 6.1 Documentation and host-side closure

PM6125 and eMMC datasheets, a board schematic/netlist and population evidence
close the documentation-dependent parts of `E1`–`E6` and `E8` `[U]`. Documents
alone cannot close the measurement-dependent parts of `E1`–`E7` `[S]`. A
complete starting-state record closes `S2` `[U]`. A bus-power design with an
explicit access policy, bounded waits, a documented vendor-request decision, a
host trace oracle and negative tests closes `S1` `[U]`. A documented
acknowledge-or-do-not-acknowledge decision with defined pending-request
behaviour closes the decision part of `E8` `[U]`.

### 6.2 Approval of a first safely designed measurement experiment

A baseline measurement experiment that does not enable bus power or the card
clock and issues no card command is a separate device procedure. It may be
approved before activation evidence exists only when all of the following are
specified:

- the quantities and transitions to be observed;
- connection points identified from verified board documentation;
- instrument resolution, bandwidth, calibration and uncertainty;
- acceptance limits derived from applicable documentation;
- stop conditions and a qualified recovery method;
- confirmation that the procedure does not enable bus power, the card clock,
  PMIC/SPMI access, RPM votes or vendor acknowledgement.

Approval of this experiment does not approve bus-power activation and does not
close any `E*` requirement by itself `[S]`. A measurement-only procedure may
require its own earlier approval; it does not authorize SDHC activation `[S]`.

### 6.3 Approval of a controlled activation experiment

A controlled bus-power activation experiment requires, at minimum:

- the baseline results from §6.2;
- the `S1` design and host tests;
- the documentation closure in §6.1 for `E1`–`E6` and `E8`;
- defined activation acceptance limits and stop conditions;
- the `E9` recovery procedure and `S3` independent review;
- a separate written owner approval for the exact activation procedure (`S4`).

Activation measurements are not a prerequisite for approving the first
measurement experiment, but they are required before capability approval `[S]`.

### 6.4 Approval of the bus-power capability

Capability approval requires the controlled activation results, closure of the
measurement-dependent parts of `E1`–`E7`, the `E8` decision, and independent
review `[U]`. No residual-risk exception is defined for the electrical
requirements, and passing tests or partial measurements cannot substitute for
closure `[S]`. Until the gates close the decision remains **NO-GO** `[S]`.

Any materially changed procedure requires renewed approval `[S]`.

## 7. Authorization record

No device action is authorized by this memo `[S]`. Keep SDHCI bus power and the
card clock off, do not acknowledge `CORE_PWRCTL_STATUS`, do not access PMIC/SPMI
or send RPM votes, and do not scan candidate slaves `[S]`. The working RAM-root
path stays unchanged `[S]`.

## 8. Addendum 2026-10-30: consolidated power/pad report

`docs/sdhc-power-pad-dependency-report.md` consolidates the remaining VCC/VCCQ,
SDC1 pad, firmware-handoff, power-request/ack and sequencing/recovery
dependencies. It records the booted `SdccDxe` controller-local handshake: the
handler sets the bus-power bit, resets the controller, and acknowledges
`IO_LOW` or `BUS_OFF | IO_LOW` after bounded polling, with no PMIC/LDO/RPM
action. That is not a bus-power-on regulator sequence and does not close `E5`
or `E8`. No `E*` or `S*` gate is closed; the decision remains **NO-GO** `[S]`.
