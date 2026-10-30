# gta4l SDHC evidence-gap checklist (Phase 1)

Date: 2026-10-30

Status: host-only inventory `[S]`. No device connection, build, flash, boot,
power change, PMIC/SPMI access, RPM vote or firmware execution was made for this
checklist. The device decision remains **NO-GO**
(`sdhc-decision-memo.md`); this checklist does not authorize bus-power
activation, card-clock activation, card commands, DMA or storage access `[S]`.

Tags: `[S]` established by a pinned source, captured DT, traced firmware bytes,
or recorded host evidence; `[C]` conditional inference with the condition
stated; `[U]` not established by the available evidence.

Revision 2 (2026-10-30): separates staged approvals from gate closure, labels
dependencies as informational, adds claim-level source references, records the
SPMI-arbiter acquisition gaps, narrows unsupported acquisition restrictions,
marks roles as proposed and adds the Phase 2 acquisition register. No gate
disposition changed `[S]`.

Revision 3 (2026-10-30): completes shared-consumer and parent-supply acquisition
tracking, separates USID, runtime-APID and descriptor-base evidence, maps
acquisition items to gates and corrects source citations. No gate disposition
changed `[S]`.

Revision 4 (2026-10-30): aligns `D11` with the `0x8878` descriptor-base override
path and extends `D6` to the `E5`/`E6`/`E7` measurement and recovery records. No
gate disposition changed `[S]`.

Revision 5 (2026-10-30): records the pinned-image descriptor-base scan in
`D11`/`E8`: one read and no direct or bounded page-relative store to `0x8878`;
runtime/external writes remain open. No gate disposition changed `[S]`.

Revision 6 (2026-10-30): corrects revision 5's scan scope after an audit found
incomplete pointer propagation and address-range checks. The bounded scan now
follows newly loaded `.data` pointers, pre/post-index addressing and one level
of direct `BL`, detects overlapping writes, and resolves the caller stores at
`0x38bc`/`0x38f4` to `0x8140`/`0x8868`. It finds no write intersecting
`[0x8878,0x8880)`, but internal indirect/computed/aliased stores and
runtime/external writers remain open. No gate disposition changed `[S]`.

Revision 7 (2026-10-30): strengthens revision 6's scan after a second audit.
The scan now distinguishes XZR from SP, invalidates `.data` pointer slots
written earlier in the path or by modeled calls, stops and reports any
instruction outside its register-only whitelist, and reports unresolved loads
and stores and every revisited-state/step/budget/call-depth/indirect-branch
truncation. Revisited states conservatively invalidate every `.data` pointer
slot. It still finds one read at `0x38b4` and no write intersecting `[0x8878,0x8880)`,
and resolves the caller stores at `0x38bc`/`0x38f4` to `0x8140`/`0x8868`.
Internal indirect/computed/aliased stores and runtime/external writers remain
open. No gate disposition changed `[S]`.

## 1. Purpose and canonical links

This checklist links every canonical gate to its current evidence, missing
input, acquisition route, proposed role, closure criterion and dependencies.
It does not create new gates and does not restate the closure criteria as new
requirements. The canonical definitions remain:

- `docs/sdhc-decision-memo.md` §3 (gate table), §4 (blocker table with closure
  criteria, non-substitutes and dispositions) and §6 (approval conditions) `[S]`.
- `docs/sdhc-electrical-contract.md` §3 (`E1`–`E9` contract matrix), §4
  (evidence gaps and acquisition requirements) and §5 (measurement
  requirements) `[S]`.
- `docs/sdhc-power-pad-dependency-report.md` §3 (dependency summary), §5 (what
  software cannot establish), §6 (measurement-only baseline) and §7 (bus-power
  design gate) `[S]`.

Closure criteria below are quoted from the "Closure criterion" column of
`sdhc-decision-memo.md` §4. The citation is part of the record; the criterion is
not redefined here `[S]`.

Three record-keeping distinctions prevent this inventory from being read as a
closure order:

- **Dependencies are informational cross-links, not prerequisites.** The
  `Informational cross-dependencies` field records which gates share inputs or
  constrain each other. It does not define a closure sequence and does not
  create a cycle. The staged approvals are governed only by
  `sdhc-decision-memo.md` §6.1–§6.4: documentation and host-side closure,
  approval of a measurement-only baseline, approval of a controlled activation
  experiment, and capability approval. A measurement-only experiment may be
  approved before the measurement-dependent gates close (memo §6.2); activation
  requires the memo §6.3 conditions: baseline results, the `S1` design and host
  tests, the documentation closure for `E1`–`E6` and `E8`, defined acceptance
  limits, `E9` recovery, `S3` review and `S4` approval; capability approval
  requires the controlled activation results, the measurement-dependent closure
  of `E1`–`E7`, the `E8` decision and independent review (memo §6.4) `[S]`.
- **Responsibilities are proposed, not accepted assignments.** No assignment
  record exists. Role key: **document custodian** obtains or retains the input
  and records its provenance; **technically qualified author/analyst** produces
  the host-only extraction, design, decision or procedure; **measurement
  operator** executes an approved device procedure; **independent reviewer**
  reviews without authoring; **approving owner** gives separate written
  approval. `S3` and `S4` remain separate roles `[S]`. Every proposed role
  remains unconfirmed until the owner accepts it in writing `[U]`.
- **Evidence references are claim-level where the record supports it.** §3.1
  maps each gate's claim group to the deriving document section, pinned input
  or artifact and observation stage. Historical device observations are
  distinguished from current host-tested source; neither is a measurement
  `[S]/[U]`.

## 2. Status summary

The disposition column is the canonical disposition from
`sdhc-decision-memo.md` §4. "Open" means no closure evidence exists; it is not a
new disposition.

| Gate | Canonical disposition | Checklist state | Primary missing input |
| --- | --- | --- | --- |
| `E1` | Blocking | Open | PM6125 and exact eMMC electrical limits; shared-consumer limits |
| `E2` | Blocking | Open | Actual SDC1 pad domain, pull-up source and unpowered-drive limits |
| `E3` | Blocking | Open | UFS population, shared loading and regulator capability |
| `E4` | Blocking | Open | Ramp, inrush, transient and brownout requirements and measurements |
| `E5` | Blocking | Open | Electrical order and minimum delays; no-ack pending-request safety |
| `E6` | Blocking | Open | Unpowered-drive and back-powering limits |
| `E7` | Blocking | Open | Actual rail retention across handoff and each transition |
| `E8` | Blocking | Open | PM6125 fault semantics and acknowledge-or-not decision |
| `E9` | Blocking | Open | Qualified rail safe-stop, recovery and acceptance criteria |
| `S1` | Blocking | Open | Approved bus-power design, policy, oracle and negative tests |
| `S2` | Narrowed but blocking | Open | Exact PCB revision, population and measured starting state |
| `S3` | Blocking | Open | Independent human review |
| `S4` | Blocking | Open | Separate written owner approval for the exact device procedure |

No gate is closed by this checklist, by the existing host tests, by the captured
vendor DT, or by the prior controller-local result `[S]`.

## 3. Gate checklist

### `E1` — Supply voltage

- **Established evidence:** Vendor `sdhc_1` requests L24 at 2.960 V (range
  2.704–3.600 V, init 2.704 V, declared 0–570,000 µA) and L11 at 1.800 V (range
  1.704–1.952 V, init 1.704 V, declared 0–325,000 µA); vendor `ufshc` shares
  both rails and declares 600,000 µA on each `[S]`. The PM6125 source table
  gives L11 base `0x4a00` and L24 base `0x5700`; the enable register is `0x46`
  bit 7 `[S]`. The captured bootargs report `androidboot.hwversion=REV1.0`,
  `androidboot.revision=0` and `androidboot.board_id=P85946KA1`; these are
  bootloader-provided identifiers, not a schematic revision or population
  record `[S]/[U]`.
- **Missing documentation or measurement:** PM6125 absolute maxima and LDO
  electrical limits; the exact Samsung eMMC ordering part and its VCC/VCCQ2
  operating and absolute-maximum limits; per-consumer limits for UFS (if
  populated) and every other shared L24/L11 consumer; the board-revision-matched
  net values `[U]`.
- **Acquisition route:** Owner-provided PM6125 datasheet or vendor register
  description, exact eMMC datasheet, and board schematic/netlist. Host-only
  qualification if the documents are supplied; no live PMIC read substitutes for
  them `[S]`.
- **Proposed responsible:** Device owner (document custodian and applicability);
  build agent (technically qualified analyst for host-only extraction and
  citation once supplied).
- **Acceptance criterion (source):** "Datasheets and schematic/netlist establish
  operating ranges and absolute maxima for VCC, VCCQ2 and every shared
  consumer" (`sdhc-decision-memo.md` §4, `E1`) `[S]`.
- **Non-substitutes (source):** DT ranges, PMIC readback and controller bits
  (`sdhc-decision-memo.md` §4, `E1`) `[S]`.
- **Informational cross-dependencies:** `E2`, `E3`, `E7`; the measurement-dependent part also depends
  on an approved measurement procedure `[C]`.
- **Status and unresolved contradictions:** Open. The vendor init level
  (2.704 V) and requested level (2.960 V) differ; which value applies at the XNU
  handoff is not established `[U]`. "DX68MB" is a captured product string, not
  proof of the exact ordering part or its datasheet `[U]`. A declared UFS
  consumer does not prove UFS population `[S]/[U]`.

### `E2` — IO compatibility

- **Established evidence:** Vendor and pinned `sdc1_clk/cmd/data/rclk`
  active/sleep states agree; the pinned TLMM tiles and QDSD offsets place SDC1
  at `west+0x75000`; UEFI writes `0x9fe4` to `0x00575000` and `0x1fe4` to
  `0x00973000`; `0x9fe4` decodes to the vendor `sdc1_*_on` drive/pull fields
  `[S]`. The write-to-pad mapping part of `E2` is resolved `[S]`.
- **Missing documentation or measurement:** Actual SDC1 pad supply domain and
  levels at handoff; pull-up source; permitted powered/unpowered combinations;
  unpowered-drive and back-power limits; measured pad state `[U]`.
- **Acquisition route:** PM6125/eMMC/SoC pad documentation and a verified
  schematic, then a pad measurement under a separately approved procedure
  (`sdhc-decision-memo.md` §6.2) `[S]`.
- **Proposed responsible:** Device owner (document custodian and approving owner); qualified measurement
  operator (pad measurement).
- **Acceptance criterion (source):** "Pad domain, pull-up source and
  unpowered-drive limits established, and the handoff state measured"
  (`sdhc-decision-memo.md` §4, `E2`) `[S]`.
- **Non-substitutes (source):** DT pinctrl values, UEFI write values and
  `INT_STABLE` (`sdhc-decision-memo.md` §4, `E2`) `[S]`.
- **Informational cross-dependencies:** `E1`, `E3`, `E6` `[C]`.
- **Status and unresolved contradictions:** Open. The relationship between the
  `uefiplat.cfg` `Sdc1GpioConfigOn = 0x1E92` / `Off = 0xA00` entries and the
  `0x9fe4` register write remains unresolved `[U]`. The SDC2 decode gives DATA
  drive 10 mA while the captured `sdc2_data_on` state is 16 mA; SDC2 is outside
  this eMMC candidate, but the discrepancy is a caution against treating a
  field decode as a measured pad state `[S]/[U]`.

### `E3` — Current / loading

- **Established evidence:** Vendor declared currents for `sdhc_1` (570 mA VCC,
  325 mA VDD-IO) and `ufshc` (600 mA each); parent inputs `vdd_l23_l24` and
  `vdd_l9_l11` are shared with L23 and L9; recovery observes the eMMC
  `mmcblk0` and no UFS storage `[S]`. These are declarations and observations,
  not regulator capability or measured load `[S]`.
- **Missing documentation or measurement:** Physical UFS population; actual
  shared loading and decoupling; L24/L11 regulator capability and parent
  loading; current measurements at handoff and during any approved transition
  `[U]`.
- **Acquisition route:** Schematic/netlist/population evidence and physical
  inspection, then current measurement under separate device approval
  (`sdhc-decision-memo.md` §4, `E3`) `[S]`.
- **Proposed responsible:** Device owner (document custodian and approving
  owner); qualified measurement operator (measurement operator for current
  measurement).
- **Acceptance criterion (source):** "Population and shared loading
  established; regulator capability and measured current within limits"
  (`sdhc-decision-memo.md` §4, `E3`) `[S]`.
- **Non-substitutes (source):** DT declared current and recovery eMMC inventory
  (`sdhc-decision-memo.md` §4, `E3`) `[S]`.
- **Informational cross-dependencies:** `E1`, `E4` `[C]`.
- **Status and unresolved contradictions:** Open. The vendor DT declares
  `ufshc` `status = "ok"` and shares both rails, while mainline disables it and
  recovery sees only eMMC; physical UFS population is unresolved `[S]/[U]`.
  Declared current is neither regulator capability nor measured load `[S]`.

### `E4` — Transients

- **Established evidence:** The SPMI common `SOFT_START` register `0x4c` exists
  in the register map; the regulator core has a `soft_start` hook; no board ramp
  property or value is present `[S]`. The RPM regulator ops expose no
  `set_soft_start`, `enable_time` or `off_on_delay` in the traced path `[S]`.
  That absence is bounded to the pinned regulator-core/RPM source and the board
  DT; no vendor datasheet or silicon documentation was available `[S]/[U]`.
- **Missing documentation or measurement:** Required slew rate, inrush,
  overshoot/undershoot, settling time, brownout and load-step response;
  measured ramp and transient behavior on this board `[U]`.
- **Acquisition route:** PM6125/eMMC datasheet transient data plus
  oscilloscope/current-probe capture under a separately approved procedure
  (`sdhc-decision-memo.md` §4, `E4`) `[S]`.
- **Proposed responsible:** Device owner (document custodian and approving owner); qualified measurement
  operator (capture).
- **Acceptance criterion (source):** "Ramp, inrush, overshoot/undershoot,
  settling and brownout requirements established and measured"
  (`sdhc-decision-memo.md` §4, `E4`) `[S]`.
- **Non-substitutes (source):** Soft-start register and regulator-core hook
  (`sdhc-decision-memo.md` §4, `E4`) `[S]`.
- **Informational cross-dependencies:** `E1`, `E3` `[C]`.
- **Status and unresolved contradictions:** Open. The existence of a soft-start
  hook does not establish a configured ramp, and no board value or capture
  exists `[S]/[U]`.

### `E5` — Sequencing

- **Established evidence:** The pinned Linux power-IRQ order is `set_vmmc` →
  `set_vqmmc` → `set_pincfg` → acknowledge; controller reset and internal-clock
  stages are separate; the candidate keeps the card clock off `[S]`. The booted
  `SdccDxe` handler performs a controller-local bus-power bit set, reset,
  bounded status/clear polls and a result-only `CORE_PWRCTL_CTL` write, with no
  regulator action `[S]`.
- **Missing documentation or measurement:** Required electrical order and
  minimum delays between L24, L11, pads, reset and clock; whether the candidate's
  no-ack scope leaves a safe pending request; measured sequencing `[U]`.
- **Acquisition route:** Datasheet and board documentation, then controlled
  measurement under separate approval (`sdhc-decision-memo.md` §4, `E5`) `[S]`.
- **Proposed responsible:** Device owner (document custodian and approving owner); qualified measurement
  operator (sequencing capture).
- **Acceptance criterion (source):** "Electrical order and minimum delays
  established and verified" (`sdhc-decision-memo.md` §4, `E5`) `[S]`.
- **Non-substitutes (source):** Linux software order (`sdhc-decision-memo.md`
  §4, `E5`) `[S]`.
- **Informational cross-dependencies:** `E2`, `E7`, `E8` `[C]`.
- **Status and unresolved contradictions:** Open. Linux software order is not an
  electrical order `[S]`. The UEFI handler writes a success result after a
  controller reset; it is not a bus-power-on regulator sequence and does not
  justify copying its acknowledge choice `[S]/[C]`.

### `E6` — Back-powering

- **Established evidence:** Vendor `ufshc` declares
  `vccq2-pwr-collapse-sup`; vendor `sdhc_1` declares
  `qcom,vdd-io-lpm-sup`; no pad-leakage data or unpowered-drive limit is present
  `[S]`.
- **Missing documentation or measurement:** Which pins may be driven while VCC
  or VCCQ2 is absent; whether the card or SoC can back-power a rail; pad leakage
  limits; measured leakage/back-power behavior `[U]`.
- **Acquisition route:** Datasheet/schematic limits plus pad-leakage measurement
  under separate approval (`sdhc-decision-memo.md` §4, `E6`) `[S]`.
- **Proposed responsible:** Device owner (document custodian and approving owner); qualified measurement
  operator (leakage/back-power measurement).
- **Acceptance criterion (source):** "Unpowered-drive restrictions and
  back-powering limits established" (`sdhc-decision-memo.md` §4, `E6`) `[S]`.
- **Non-substitutes (source):** Vendor properties and software reset state
  (`sdhc-decision-memo.md` §4, `E6`) `[S]`.
- **Informational cross-dependencies:** `E2`, `E5` `[C]`.
- **Status and unresolved contradictions:** Open. The two vendor properties are
  hints, not limits; their electrical meaning is unresolved `[S]/[U]`.

### `E7` — Retention

- **Established evidence:** The pinned mainline port sends no L11/L24 vote and
  has no readback; the Mu-Silicium exit path is not passive (PMIC writes to
  `0x14`/`0x16`/`0xd1` and the arbiter `+0x28` path); the bounded RPM/XBL trace
  finds LDOA resource tables for both rails, no referenced static table or
  stored pointer/resolvable PC-relative literal that selects either rail in the
  bounded scan, and no direct L11/L24 voltage, mode or enable write in the
  traced paths; both XBL RSBP images are downloaded to PMIC PBS RAM and touch
  only L11/L24 `STEP_CTRL` `[S]`.
- **Missing documentation or measurement:** Actual rail state at handoff and
  across suspend, shutdown and reboot; runtime LDOA request path
  (parser/aggregator), latched per-id state and runtime-built descriptors; PBS
  instruction semantics; indirect UEFI/transport effects (`GlinkDxe`/`NpaDxe`)
  on L11/L24 requests; transition measurements `[U]`.
- **Acquisition route:** RPM firmware documentation or reversing plus
  transition measurements under separate approval (`sdhc-decision-memo.md` §4,
  `E7`) `[S]`.
- **Proposed responsible:** Device owner (document custodian and approving
  owner); build agent (technically qualified analyst for a bounded host-only
  trace); qualified measurement operator (measurement operator for transition
  measurements).
- **Acceptance criterion (source):** "Initial/active/sleep/reset policy and
  retention across each transition established" (`sdhc-decision-memo.md` §4,
  `E7`) `[S]`.
- **Non-substitutes (source):** No AP vote, `pwr=00`, DT `always-on` and
  firmware trace (`sdhc-decision-memo.md` §4, `E7`) `[S]`.
- **Informational cross-dependencies:** `E1`, `E5` `[C]`.
- **Status and unresolved contradictions:** Open. The retained-rail hypothesis
  is conditional; an absent AP vote is not retention, and the bounded static
  trace is not a runtime policy result `[S]/[U]`. The class object and per-id
  array are built at runtime, so the scan does not prove that any L11/L24
  request exists or its runtime bounds `[C]/[U]`.

### `E8` — Fault handling

- **Established evidence:** SDHCI `CORE_PWRCTL_STATUS`/`MASK`/`CLEAR`/`CTL`
  semantics are source-established; the booted `SdccDxe` handler polls status,
  writes `CORE_PWRCTL_CLEAR`, and writes a result-only `CORE_PWRCTL_CTL` value;
  `PmicDxeLa` writes PMIC bytes `0x14`/`0x16`/`0xd1`; the `+0x28` entry resolves
  to the SPMI-arbiter v5 `irq_status`/`irq_clear` pair under the recorded
  interpretation `[S]/[C]`. Whether PM6125 uses the `0x14`/`0x16` `QPNPINT`
  layout is unresolved `[U]`. A bounded static scan of the pinned `SPMIDxe`
  image finds one read and no write intersecting `[0x8878,0x8880)`; it resolves
  the caller stores at `0x38bc`/`0x38f4` to `0x8140`/`0x8868`, stops at
  instructions outside its register-only whitelist, invalidates overwritten
  `.data` pointer slots, and conservatively invalidates all such slots after
  indirect or truncated calls and on revisited states, so the descriptor-base
  override is zero in the image's initial state `[S]`.
- **Missing documentation or measurement:** PM6125 fault semantics, USID/subtype
  and `QPNPINT` applicability; SPMI-arbiter clear-register read/write semantics
  (what bits 31:8 read as and whether writing them back is safe); runtime
  APID/owner-status evidence for the `+0x28` path; internal
  indirect/computed/aliased or runtime/external descriptor-base override
  population (the bounded static scan finds no intersecting write, stops at
  instructions outside its whitelist and reports 150 unresolved non-stack loads
  and 36 unresolved non-stack stores, but does not cover every addressing
  mode); safe pending-request behavior; a documented acknowledge-or-not
  decision `[U]`.
- **Acquisition route:** PM6125 register description/datasheet and non-scanning
  identity evidence; SoC SPMI-arbiter register documentation; runtime APID
  bitmap or transport trace; a whole-program alias analysis or runtime
  writer/trace for the descriptor base (the bounded static write analysis is
  recorded in `docs/spmi-0x28-analysis.md` and
  `scripts/test-gta4l-spmidxe-descriptor-base.py`); live PMIC access remains
  blocked (`sdhc-decision-memo.md` §4, `E8`) `[S]`.
- **Proposed responsible:** Device owner (document custodian); build agent
  (technically qualified analyst and proposed decision author); independent
  reviewer (reviewer).
- **Acceptance criterion (source):** "Fault semantics established and a
  documented acknowledge-or-not decision with pending-request behaviour"
  (`sdhc-decision-memo.md` §4, `E8`) `[S]`.
- **Non-substitutes (source):** Generic arbiter layout, PMI632 analogy and
  SDHCI semantics (`sdhc-decision-memo.md` §4, `E8`) `[S]`.
- **Informational cross-dependencies:** `E5`, `E7` `[C]`.
- **Status and unresolved contradictions:** Open. A generic `QPNPINT` layout is
  not proof for PM6125 `[S]/[U]`. The UEFI handler's result-only write follows a
  controller reset and is not a response to a bus-on request, so it does not
  decide the candidate's acknowledge choice `[S]/[C]`. The descriptor-base
  override is zero and the bounded scan finds no write intersecting it, but
  the scan stops at instructions outside its whitelist and leaves internal
  indirect/computed and runtime/external writers possible `[S]/[U]`.

### `E9` — Recovery

- **Established evidence:** BOOT-partition backup/rollback and payload gates
  provide software rollback `[S]`. No rail-level safe-stop, discharge/power-cycle
  procedure, re-initialization order, verification measurement or acceptance
  criteria exists `[U]`.
- **Missing documentation or measurement:** Safe-stop and discharge/power-cycle
  procedure; re-initialization order; verification measurement; acceptance
  criteria; independent review `[U]`.
- **Acquisition route:** Written procedure with safe stop, discharge/power-cycle
  and re-init order, verification measurement, and independent review
  (`sdhc-decision-memo.md` §4, `E9`) `[S]`.
- **Proposed responsible:** Build agent (proposed procedure author, host-only);
  qualified measurement operator (measurement operator for verification);
  independent reviewer (reviewer); device owner (approving owner).
- **Acceptance criterion (source):** "Qualified recovery procedure and
  acceptance criteria established" (`sdhc-decision-memo.md` §4, `E9`) `[S]`.
- **Non-substitutes (source):** Software rollback (`sdhc-decision-memo.md` §4,
  `E9`) `[S]`.
- **Informational cross-dependencies:** `E1`–`E8` `[C]`.
- **Status and unresolved contradictions:** Open. Software rollback repairs
  software only; it cannot repair electrical damage `[S]`.

### `S1` — Bus-power implementation and host controls

- **Established evidence:** The host-local diagnostic and its access policy,
  simulator, oracle and negative tests exist and pass; no bus-power
  implementation, image or approved bus-power access policy exists in this tree
  `[S]`. The existing policy forbids bus-power enable, card-clock enable and
  power-status acknowledgement `[S]`.
- **Missing documentation or measurement:** Approved bus-power design with an
  exact register allowlist, widths and permitted values; preconditions and
  sequencing; bounded waits and failure handling; the `E8` acknowledge-or-not
  decision and pending-request behavior; safe end states; trace oracle and
  negative tests for the candidate `[U]`.
- **Acquisition route:** Host-only design after the applicable `E5`/`E8`
  documentation is available (`sdhc-decision-memo.md` §4, `S1`) `[S]`.
- **Proposed responsible:** Build agent (technically qualified design author);
  independent reviewer (reviewer).
- **Acceptance criterion (source):** "Approved design passes its policy, oracle
  and negative tests" (`sdhc-decision-memo.md` §4, `S1`) `[S]`.
- **Non-substitutes (source):** Host-local diagnostic result
  (`sdhc-decision-memo.md` §4, `S1`) `[S]`.
- **Informational cross-dependencies:** `E5`, `E8` `[C]`.
- **Status and unresolved contradictions:** Open. Passing host tests do not
  change the electrical decision, and the existing controller-local policy is
  not a bus-power policy `[S]`.

### `S2` — Board revision, population and starting state

- **Established evidence:** The operation, board, controller, firmware and prior
  boot provenance are identified; the controller-local starting state is
  observed (`pwr=00`, `clk=0003`, `bcr=00000000`, `ac=00004221`,
  `ahb=00000001`, `ice=00004229`) `[S]`. The captured bootargs report
  `androidboot.hwversion=REV1.0`, `androidboot.revision=0`,
  `androidboot.board_id=P85946KA1` and a device serial; these are
  bootloader-provided identifiers, not a PCB revision or population record
  `[S]/[U]`.
- **Missing documentation or measurement:** Exact PCB revision and population
  variant; UFS population; measured L24/L11 voltage/enable/power-good; SDC1 pad
  state; CX state; verified safe measurement points `[U]`.
- **Acquisition route:** Documentation plus measurement under separate approval
  (`sdhc-decision-memo.md` §4, `S2`) `[S]`.
- **Proposed responsible:** Device owner (document custodian and approving
  owner); build agent (proposed record author); qualified measurement operator
  (measurement operator for starting-state measurement).
- **Acceptance criterion (source):** "Complete starting-state record, with the
  measurement-dependent parts of `E1`/`E2`/`E7` closed"
  (`sdhc-decision-memo.md` §4, `S2`) `[S]`.
- **Non-substitutes (source):** Prior controller-local observation
  (`sdhc-decision-memo.md` §4, `S2`) `[S]`.
- **Informational cross-dependencies:** `E1`, `E2`, `E7` `[C]`.
- **Status and unresolved contradictions:** Open and narrowed. The memo records
  the board revision as unknown; the captured `hwversion`/`revision`/`board_id`
  strings narrow the identifier but do not establish the PCB revision,
  population or schematic match `[S]/[U]`.

### `S3` — Independent review

- **Established evidence:** A same-author, separate-implementation software
  oracle exists and passes `[S]`. It is not an independent electrical review
  `[S]`.
- **Missing documentation or measurement:** Independent human review of the
  electrical contract, measurement procedure, activation procedure and recovery
  `[U]`.
- **Acquisition route:** A reviewer who is not the author and does not rely on
  the same-author oracle (`sdhc-decision-memo.md` §4, `S3`) `[S]`.
- **Proposed responsible:** Independent human reviewer (reviewer; not yet
  assigned).
- **Acceptance criterion (source):** "Independent review finds no unresolved
  safety-critical finding" (`sdhc-decision-memo.md` §4, `S3`) `[S]`.
- **Non-substitutes (source):** Same-author oracle (`sdhc-decision-memo.md` §4,
  `S3`) `[S]`.
- **Informational cross-dependencies:** `E1`–`E9`, `S1`, `S2` `[C]`.
- **Status and unresolved contradictions:** Open. No independent review exists
  `[U]`.

### `S4` — Owner approval

- **Established evidence:** None; prior host-init approval does not transfer to
  bus-power activation `[S]`.
- **Missing documentation or measurement:** Separate written owner approval for
  the exact device procedure, after the technical prerequisites are met `[U]`.
- **Acquisition route:** Written owner approval for the exact procedure
  (`sdhc-decision-memo.md` §4, `S4`) `[S]`.
- **Proposed responsible:** Device owner (approving owner; no approval
  recorded).
- **Acceptance criterion (source):** "Separate written owner approval for the
  exact device procedure" (`sdhc-decision-memo.md` §4, `S4`) `[S]`.
- **Non-substitutes (source):** Prior host-init approval
  (`sdhc-decision-memo.md` §4, `S4`) `[S]`.
- **Informational cross-dependencies:** `S1`, `S3`, `E9` `[C]`.
- **Status and unresolved contradictions:** Open. Requesting approval while the
  technical gates fail would imply the prerequisites are met; they are not
  `[S]`.

### 3.1 Gate source references

These are claim-level pointers to the existing derivations, not a new evidence
claim. Stage labels distinguish vendor-DT declarations, current host-tested
source and historical device observations from electrical measurements. None of
these rows is a measurement `[S]/[U]`.

| Gate | Claim group | Deriving record and section | Pinned input or artifact | Stage |
| --- | --- | --- | --- | --- |
| `E1` | Rail request levels and declared currents; PMIC bases and enable bit | `docs/power-path-decision.md` §Known rail description; `docs/pm6125-identity-evidence.md` §Consumer assignment; `docs/pm6125-register-map.md` §Regulator bases, §Common register layout | Captured `device-dump/private/gta4l-dump/live.dts`; pinned Linux `cf5f763f...` | Vendor DT and host source |
| `E2` | SDC1 pad active/sleep states; TLMM offset; UEFI write decode | `docs/sdhc-power-pad-dependency-report.md` §4.1; `docs/sdhc-decision-update-2026-10-30.md` §2; `docs/sdhc-dependency-trace.md` §4, §7.1 | Pinned DTS/pinctrl; `SdccDxe.efi` `4084bbc7...`; `scripts/test-gta4l-sdccdxe-decode.py` | Host decode of booted image |
| `E3` | Declared currents; shared parent inputs; recovery inventory | `docs/power-path-decision.md` §Known rail description; `docs/pm6125-identity-evidence.md` §Consumer assignment, §Rails: numbering is established, USID is not; `docs/sdhc-power-pad-dependency-report.md` §3; `docs/emmc-reference.md` §Local tablet evidence | Vendor DT; recovery observation | DT declaration and historical device observation |
| `E4` | Soft-start register and hook; absent board ramp configuration | `docs/power-path-decision.md` §Contract items: present versus absent; `docs/pm6125-register-map.md` §Common register layout; `docs/sdhc-electrical-contract.md` §3 | Pinned `qcom_smd-regulator.c` and regulator core | Host source |
| `E5` | Linux power-IRQ order; UEFI handler sequence | `docs/sdhc-dependency-trace.md` §6; `docs/sdhc-decision-update-2026-10-30.md` §1; `docs/sdhc-power-pad-dependency-report.md` §4 | Pinned `sdhci-msm.c`; `SdccDxe.efi` | Host source and booted-image decode |
| `E6` | Vendor collapse and low-power-mode properties | `docs/pm6125-identity-evidence.md` §Consumer assignment; `docs/sdhc-power-pad-dependency-report.md` §3 | Vendor DT | DT declaration |
| `E7` | No AP vote; non-passive UEFI path; bounded RPM/XBL trace | `docs/power-retention-evidence.md` §1, §4, §5, §7.2, §7.4, §7.6; `docs/pmic-audit.md` §Consolidated blocker ledger | Pinned `qcom_smd-regulator.c`; `rpm.img` `272bebd3...`; `xbl.img`/`xbl_config.img` | Host source and bounded static firmware analysis |
| `E8` | SDHCI status/clear/CTL; UEFI writes; arbiter `+0x28`; PMIC bytes | `docs/pmic-audit.md` §Findings 2, §Consolidated blocker ledger; `docs/sdhc-decision-update-2026-10-30.md` §1; `docs/pm6125-register-map.md` §Interrupt registers are a separate block | `SdccDxe.efi`, `PmicDxeLa.efi`, `SPMIDxe.efi`; pinned SDHCI source | Booted-image decode and host source |
| `E9` | BOOT rollback and payload gates; absence of rail recovery | `docs/sdhci-handoff-audit.md` §Packaging failure and rollback; `docs/evidence-baseline.md` §1, §4; `docs/sdhc-decision-memo.md` §4 | `out/gta4l/ram-root-sdhci-audit-evidence/rollback-report.json` (historical, gitignored) and payload-gate records | Historical host and device record |
| `S1` | Host diagnostic, policy, oracle and tests; no bus-power policy | `docs/sdhci-host-init.md` §Host access-policy verification (Step 5); `docs/evidence-baseline.md` §2 | `package/Gta4lSdhciHostInit.c`, simulator, oracle and tests | Current host-tested source |
| `S2` | Controller-local start state; bootargs identifiers | `docs/sdhci-handoff-audit.md` §Report, §Hardware result; `docs/sdhci-host-init.md` §Implementation status; `device-dump/private/gta4l-dump/CAPTURE-INFO.md`; `docs/evidence-baseline.md` §4 | Captured `live.dts`; host-init result recorded in `docs/sdhci-host-init.md` §Implementation status | Historical device observation |
| `S3` | Same-author software oracle only | `docs/sdhci-host-init.md` §5.5, §5.7 | Oracle script | Current host test |
| `S4` | Prior host-init approval does not transfer | `docs/sdhc-decision-memo.md` §6.3–§6.4 | Approval records | Decision record |

## 4. Local input inventory

The inventory covers the repository and the adjacent workspace
(`/home/sara/slopcode/taba7unixnu`), excluding `.git`, generated `out/` trees
and the Mu-Silicium dependency tree. No PM6125, eMMC or board schematic, CAD,
boardview, netlist or datasheet file was found `[S]`. The only PDFs found are
unrelated Mu-Silicium dependency documentation; they are not board inputs `[S]`.

| Input | Local status | Evidence or path | Authority and limits |
| --- | --- | --- | --- |
| Pinned Linux source cache | Present, outside the repository | `/tmp/opencode/linux-verify/`, 26 flat files, manifest digest `35b55f07119534f899da80ba1bae933b1bba67f91ec776553f21a91a150987e2`; revision `cf5f763f0ef8a5b4d8bb4163833cba1160c51894` | Source behaviour, register map, pad definitions and lifecycle `[S]`; not electrical limits or measurements; cache is ephemeral and has no `.git` `[S]/[U]` |
| Captured vendor FDT | Present, private, not tracked | `device-dump/private/gta4l-dump/live-fdt.dtb` SHA-256 `da1cb76f...a0b6ebc`; `live.dts` SHA-256 `81c18655...6a9b629` | Board description and bootargs identifiers `[S]`; post-unlock current-state, not authenticated stock, not population or measurement `[S]/[U]` |
| RPM image | Present, private, not tracked | `device-dump/private/gta4l-dump/partitions/rpm.img` SHA-256 `272bebd3...b4937c6` | Bounded static LDOA analysis `[S]`; not runtime policy or retention `[U]` |
| XBL and XBL config images | Present, private, not tracked | `xbl.img` SHA-256 `a09c7b62...9e073e`; `xbl_config.img` SHA-256 `6dbaad63...20d99e`; embedded `pmic_settings.bin` SHA-256 `410e45ac...f4e97597` | Bounded static boot-PMIC analysis `[S]`; PBS semantics and runtime effect unresolved `[U]` |
| Mu-Silicium handoff drivers and platform config | Present | `SdccDxe.efi` SHA-256 `4084bbc7...d5674b`; `PmicDxeLa.efi` `db541f63...64e32ce`; `SPMIDxe.efi` `50be2e1a...d3e254`; `uefiplat.cfg` `1b8c0b80...f08f9c3d` | Booted-image provenance for the three drivers in the recorded v2 image `[S]`; controller-local handshake, not a regulator sequence or rail measurement `[S]` |
| Recovery eMMC identity | Present as a read-only observation | `DX68MB`, CID `150100445836384d42009005f4661900`, CSD `d02701320f5903fff6dbffef8e404000`, 61,071,360 logical 512-byte sectors | Identifies the card `[S]`; not a datasheet, not voltage/current/timing limits `[S]/[U]` |
| Controller-local observations | Present | `docs/sdhci-handoff-audit.md`, `docs/sdhci-host-init.md`, recorded host-init result | Controller and GCC state `[S]`; not rail, pad, CX or retention measurement `[S]/[U]` |
| Host-only analysis and tests | Present | `docs/evidence-baseline.md`, `docs/sdhc-electrical-contract.md`, `docs/sdhc-power-pad-dependency-report.md`, `docs/power-retention-evidence.md`, `docs/pmic-audit.md`, `docs/pm6125-*`, `scripts/test-gta4l-sdccdxe-decode.py` and the host-init suite | Bounded source, firmware and host evidence `[S]`; no hardware or electrical result `[S]` |
| Board identifiers | Present in captured bootargs | `androidboot.hwversion=REV1.0`, `androidboot.revision=0`, `androidboot.board_id=P85946KA1`, serial `R9KT601KVHW` | Bootloader-provided identifiers `[S]`; not PCB revision, population, schematic match or safe test-point record `[U]` |
| PM6125 datasheet or vendor register description | Absent | Only the source-derived map in `docs/pm6125-register-map.md` and identity inventory in `docs/pm6125-identity-evidence.md` | The map supplies bases, offsets and enable bit `[S]`; it does not supply absolute maxima, transient limits, fault semantics or a vendor read-safety statement `[U]` |
| Exact Samsung eMMC datasheet | Absent | Only CID/CSD/product string and sector count | Card identity `[S]`; no VCC/VCCQ2 limits, power-up sequence or inrush data `[U]` |
| Shared-consumer and parent-supply limits | Absent | Only vendor DT declarations for `sdhc_1`/`ufshc` and the parent net names | Declared current `[S]`; no per-consumer datasheet, UFS population or L9/L23 loading and capability data `[U]` |
| Board schematic, netlist, CAD or boardview | Absent | No PDF/schematic/CAD/boardview/netlist found in the repository or workspace search | DT describes intended wiring `[S]`; it does not prove population, net topology, decoupling or safe test points `[U]` |
| SM6115 pad and SDHC power-control documentation | Absent | Pinned DTS/pinctrl source only | Pad field mapping `[S]`; no absolute pad limits or vendor power-control semantics `[U]` |
| Vendor power-policy or runtime RPM documentation | Absent | Bounded static RPM/XBL trace only | Narrowed `E7` `[S]`; runtime policy, PBS semantics and retention unresolved `[U]` |
| Verified safe measurement points | Absent | No schematic or board inspection record | Cannot connect a measurement to a known net without speculation `[U]` |
| Rail, pad, current and transient measurements | Absent | No DMM, oscilloscope or current-probe record | No physical voltage, power-good, current, ramp or transient result `[U]` |
| Qualified rail-recovery procedure | Absent | BOOT-partition backup/rollback only | Software rollback `[S]`; no rail safe-stop, discharge/power-cycle, re-init or acceptance criteria `[U]` |
| Independent review | Absent | Same-author software oracle only | Software cross-check `[S]`; not an independent electrical review `[U]` |
| Owner approval for bus-power activation | Absent | Prior host-init approval only | Prior approval does not transfer `[S]` |

The workspace image files referenced by the existing records are screen or
panic-screen captures used for controller-local results; they are not board
photographs, schematics or component-marking records `[S]`.

## 5. Acquisition routes and stop conditions

The preferred route is owner-provided, revision-matched vendor documentation.
Host-only work can qualify and cite a supplied document, but it cannot create an
electrical limit `[S]`.

| Input | Preferred acquisition | Acceptable fallback | Stop condition |
| --- | --- | --- | --- |
| PM6125 limits and fault semantics | PM6125 datasheet or vendor register description applicable to the installed silicon | None for absolute maxima, transients, back-powering or fault semantics | Absent documentation leaves `E1`, `E4`, `E6`, `E8` open `[S]` |
| SPMI-arbiter clear/read-back and runtime APID evidence | SoC SPMI-arbiter register documentation; runtime APID bitmap or transport trace | Bounded static trace narrows but does not close `E8` | Absent evidence leaves the arbiter and runtime-APID parts of `E8` open `[S]` |
| Exact eMMC limits | Samsung datasheet for the exact `DX68MB` ordering part, resolved from package marking or vendor documentation | None; a generic eMMC specification is not the exact part's datasheet | Absent documentation leaves `E1`, `E4`, `E5` open `[S]` |
| Shared-consumer and parent-supply limits | Datasheets for UFS and every other populated L11/L24 consumer plus PM6125 parent-rail capability | None for absolute maxima or capability; population evidence only establishes which parts to obtain | Absent evidence leaves `E1`, `E3`, `E4`, `E6` open `[S]` |
| Board topology and population | SM-T505N service schematic/netlist/boardview matched to the captured board identifiers and revision | Verified revision-matched PCB/CAD or population record for the facts it establishes; the vendor DT alone is not a schematic. Schematic-dependent limits and safe connection points still require a schematic/netlist or qualified inspection | Absent board evidence leaves `E2`, `E3`, `E6`, `S2` open; unverified points keep measurement blocked `[S]` |
| SoC pad and SDHC power-control semantics | SM6115 documentation for the installed SoC revision | Pinned DTS/pinctrl source only for field mapping, not limits | Absent documentation leaves `E2`, `E5`, `E6` open `[S]` |
| Runtime power policy | Vendor RPM documentation or a qualified runtime trace | Bounded static RPM/XBL analysis narrows but does not close `E7` | Absent evidence leaves `E7` open `[S]` |
| Physical starting state | Measurement-only procedure under `sdhc-decision-memo.md` §6.2, with independent review and separate approval | None; PMIC readback reports commanded state, not measured voltage | Unverified test points or absent recovery leave the measurement blocked `[S]` |
| Recovery | Written safe-stop, discharge/power-cycle and re-init procedure with verification and independent review | None; software rollback is not electrical recovery | Absent procedure leaves `E9` open `[S]` |
| Independent review and owner approval | Independent human review, then separate written owner approval for the exact procedure | None | Prior approvals and same-author checks do not transfer `[S]` |

Firmware traces and static analysis cannot replace electrical ratings or
physical measurements, and repeating an unchanged analysis supplies no new
evidence `[S]`. Qualified host-only analysis remains an acquisition route where
it can answer a specific open question, for example extracting limits from a
supplied document or tracing a newly identified runtime path; it does not close
a measurement-dependent gate by itself `[S]/[C]`. A DT range, PMIC readback,
controller bit or software rollback likewise does not substitute for the
corresponding measurement or electrical limit `[S]`.

## 6. Phase 2 acquisition register

Status: template and proposed tracking record `[U]`. No document in this
register has been acquired; every row remains an open blocker. When an input
arrives, record the provenance fields before using it, keep proprietary material
outside the public repository unless redistribution is permitted, and extract
limits with page or section references `[S]`.

| ID | Gates | Required evidence | Applicability to establish | Proposed acquisition route and custodian | Provenance to record | Extraction required | Current conflicts or blockers |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `D1` | `E1`, `E4`, `E6`, `E8` | PM6125 datasheet or vendor register description | Installed PM6125 silicon and package variant; USID/subtype; L11/L24 and common-block applicability | Owner-supplied vendor document; custodian: device owner; analysis: build agent `[U]` | Origin, revision/date, checksum, redistribution terms, page count, applicability statement | Absolute maxima, LDO operating limits, soft-start and transient data, fault/`QPNPINT` semantics, SPMI read/write caveats with page/section | PM6125 identity and USID unresolved (`pmic-audit.md`); the source-derived map is not a datasheet `[U]` |
| `D2` | `E1`, `E3`, `E4`, `E5` | Samsung eMMC datasheet for the exact ordering part | Exact part behind `DX68MB` and the CID; package marking and ordering code | Owner or vendor service documentation; custodian: device owner; analysis: build agent `[U]` | Origin, revision/date, checksum, redistribution terms, exact ordering part and marking match | VCC/VCCQ2 operating and absolute-maximum ranges, power-up sequence, inrush, back-power and unpowered-drive restrictions with page/section | Product string and CID do not prove the ordering part or datasheet applicability `[U]` |
| `D3` | `E1`, `E2`, `E3`, `E6`, `S2` | SM-T505N service schematic, netlist, PCB/CAD or boardview matched to `REV1.0`/`P85946KA1`/`T505NDXS6CXB1` | Board revision, population variant, UFS population, L11/L24 nets, decoupling, SDC1 pad supply and pull-ups, safe test points | Owner or service documentation; custodian: device owner; inspection: qualified operator under separate approval `[U]` | Origin, revision, checksum, redistribution terms, board/revision match, sheet index | Net topology, population, decoupling, pad domains and test points; mark which facts each source establishes with sheet/page | Bootloader identifiers are not a PCB revision; no board document found locally `[U]` |
| `D4` | `E2`, `E5`, `E6`, `E8` | SM6115 pad and SDHC power-control documentation; SPMI-arbiter register documentation | Installed SoC revision; SDC1 TLMM pad limits; SDHCI power-control and arbiter clear/read-back semantics | Owner-supplied SoC or vendor documentation; custodian: device owner; analysis: build agent `[U]` | Origin, revision/date, checksum, redistribution terms, SoC revision applicability | Pad absolute limits, drive/pull constraints, power-control semantics, arbiter bits 31:8 read-back and write rules with page/section | Pinned DTS gives field mapping only; arbiter semantics unresolved (`pmic-audit.md`) `[U]` |
| `D5` | `E7` | RPM/NPA power-policy documentation or qualified runtime evidence | RPM firmware revision in `rpm.img`; L11/L24 runtime requests and retention across transitions; `GlinkDxe`/`NpaDxe` effects | Owner-supplied vendor documentation, or a separately approved runtime trace; custodian: device owner; analysis: build agent `[U]` | Origin, revision/date, checksum, redistribution terms, firmware-image match, trace method and calibration | Initial/active/sleep/reset policy, request and aggregation rules and PBS semantics with page/section or trace evidence; transition-retention measurements are tracked in `D6` | Bounded static trace narrows but does not close `E7`; no runtime policy evidence `[U]` |
| `D6` | `E1`–`E7`, `E9`, `S2` | Qualified measurement and recovery records | Rails, pads, current and transients; power-up/down order and minimum delays; pad leakage and back-power; transition retention; safe-stop, discharge/power-cycle and re-initialization | Separate approved device procedures under `sdhc-decision-memo.md` §6.2–§6.3; operator: qualified measurement operator; approval: device owner `[U]` | Procedure revision, instrument calibration/resolution/bandwidth/uncertainty, board and firmware hashes, raw traces, timestamps | Measured values against sourced limits; sequencing order and delays; leakage/back-power; retention and recovery acceptance criteria; anomalies and repetitions | No measurement or rail-recovery procedure exists; device action is not authorized; `D5` supplies the policy inputs `[U]` |
| `D7` | `E1`, `E3`, `E4`, `E6` | Component datasheets or vendor limits for UFS and every other populated L11/L24 consumer, conditional on verified population | Per-consumer VCC/VCCQ2/VDD-IO operating ranges, absolute maxima, inrush, low-power and back-power restrictions | Owner-supplied component datasheets triggered by verified `D3` population; custodian: device owner; analysis: build agent `[U]` | Origin, revision/date, checksum, redistribution terms, exact ordering part, population match | Consumer limits, inrush, load-step and back-power data with page/section; map each consumer to the L11/L24 net | UFS population unknown; DT `status = "ok"` and 600 mA declarations do not establish population or limits `[U]` |
| `D8` | `E3`, `E4` | PM6125 parent-rail capability and combined-load accounting for `vdd_l9_l11` and `vdd_l23_l24` | Parent input current capability, shared L9/L23 loads and combined loading against L11/L24 demand | PM6125 datasheet (`D1`) plus board population/topology (`D3`) and measurement (`D6`); custodian: device owner; analysis: build agent `[U]` | Document revisions, rail-limit pages, load inventory and measurement provenance | Parent-rail current limits, L9/L23 consumer loads, combined-load margin and load-step limits with page/section | Declared `sdhc_1`/`ufshc` currents are not regulator capability; other L9/L23 loads are not enumerated `[U]` |
| `D9` | `E1`, `E3`, `E8` | Non-scanning PM6125 USID/subtype identity evidence | Installed PMIC identity and subtype needed for register and rail applicability | Vendor documentation, package marking or a non-scanning boot log; custodian: device owner; analysis: build agent. A live PMIC scan remains blocked `[U]` | Source type, capture method, firmware revision, raw record and hash | USID/subtype value and mapping to the applicable datasheet revision with page/section | Rail numbering is established but identity is not; a scan is not authorized `[U]` |
| `D10` | `E8` | Runtime SPMI-arbiter APID/owner-bitmap evidence for the `+0x28` clear path | Which master owns or may clear the pending interrupt and what the owner bitmap reads at runtime | SoC arbiter documentation or a separately approved runtime transport trace; custodian: device owner; collection requires separate approval `[U]` | Trace procedure, firmware and image hashes, APID bitmap values, timestamps, calibration | Owner/APID bits, access permissions and read side effects with page/section or trace evidence | Only bounded static inference exists; runtime collection is not authorized `[U]` |
| `D11` | `E8` | Descriptor-base override write analysis for indirect and computed stores | Whether anything populates global `0x8878` at run time and can therefore change the arbiter descriptor base | Host-only binary/data-flow analysis of the pinned drivers plus `D4` arbiter documentation; the bounded scan is reproducible, stops at instructions outside its register-only whitelist, invalidates overwritten `.data` pointer slots and all such slots on revisited states, and leaves internal indirect/computed and runtime/external stores open `[S]/[U]` | Firmware hashes, tool revision, analysis scope and unresolved indirect targets | Enumerated stores to `0x8878` and aliases, computed addresses, reachability and worst-case descriptor-base value with evidence | The bounded scan finds one read at `0x38b4` and no write intersecting `[0x8878,0x8880)`; it resolves the caller stores at `0x38bc`/`0x38f4` to `0x8140`/`0x8868`, stops at instructions outside its register-only whitelist, invalidates overwritten `.data` pointer slots and all such slots on revisited states, and reports 150 unresolved non-stack loads and 36 unresolved non-stack stores, so it is not a proof of absence and the analysis is not a datasheet `[U]` |

Register rules:

- An input is usable only after its origin, revision, applicability and checksum
  are recorded, and a conflicting specification is logged as a blocker `[S]`.
- Authoritative specifications, analogous-part data and third-party analysis are
  recorded separately; an analogous part does not close a gate `[S]`.
- Private or proprietary material stays outside the public repository unless
  redistribution is permitted; cite its location and hash rather than copying
  it `[S]`.
- This register does not assign work or authorize acquisition involving device
  access; proposed custodians remain unconfirmed until the owner accepts them
  `[U]`.
- `D7` is conditional on verified population from `D3`; if population cannot be
  verified, the row remains an untracked-consumer blocker rather than being
  dropped `[C]`.
- `D10` runtime collection requires separate written approval under
  `sdhc-decision-memo.md` §6; until then only host-only analysis is permitted
  `[S]`.
- `D11` is host-only and may be revised as the arbiter documentation arrives; it
  cannot close `E8` without the `D4` register semantics `[S]/[C]`.

## 7. Unresolved contradictions and decisions

1. **L24 init versus requested voltage.** The vendor DT declares init 2.704 V
   and a requested level of 2.960 V; which value applies at the XNU handoff is
   not established `[S]/[U]`.
2. **eMMC exact part.** `DX68MB` is a captured product string; the exact Samsung
   ordering part and datasheet applicability remain unresolved `[S]/[U]`.
3. **UFS population.** The vendor DT declares `ufshc` `status = "ok"` and
   600 mA on each shared rail, while mainline disables it and recovery observes
   only eMMC; physical population is unknown `[S]/[U]`.
4. **Board revision.** The memo records the board revision as unknown; the
   captured bootargs report `REV1.0`, `revision=0` and `board_id=P85946KA1`.
   These identifiers do not establish the PCB revision, population variant or
   schematic match `[S]/[U]`.
5. **Pad configuration map.** The relationship between
   `uefiplat.cfg` `Sdc1GpioConfigOn = 0x1E92` / `Off = 0xA00` and the `0x9fe4`
   TLMM write remains unresolved `[U]`.
6. **SDC2 field decode.** The SDC2 DATA drive field decodes to 10 mA while the
   captured `sdc2_data_on` state is 16 mA; SDC2 is outside the eMMC candidate,
   but the discrepancy shows that a field decode is not a measured pad state
   `[S]/[U]`.
7. **Retention versus no vote.** The pinned port sends no L11/L24 vote and the
   bounded static trace finds no referenced static selector, but the retained-
   rail hypothesis is conditional and the runtime policy is unresolved
   `[S]/[C]/[U]`.
8. **Acknowledge choice.** The UEFI handler writes a result-only status after a
   controller reset; it is not a bus-on request response. Whether the candidate
   should leave the request pending, acknowledge without a rail action, or
   acknowledge after rail/pad actions is unresolved `[S]/[U]`.
9. **Electrical order.** Linux software order is not an electrical order, and
   the UEFI reset/power order is not a regulator sequence; required delays and
   ordering remain unestablished `[S]/[U]`.
10. **Recovery sufficiency.** Software rollback is proven for the BOOT path but
    cannot repair electrical damage; rail safe-stop and recovery acceptance
    criteria are absent `[S]/[U]`.

## 8. Phase 1 outcome

- The canonical `E1`–`E9` and `S1`–`S4` gates are linked to their current
  evidence, missing inputs, acquisition routes, proposed roles, closure criteria,
  informational cross-dependencies and contradictions `[S]`.
- No gate is closed, no disposition improves, and **NO-GO remains** `[S]`.
- The exact missing inputs are the PM6125 datasheet/register description, the
  exact Samsung eMMC datasheet, the revision-matched board schematic/netlist and
  population record, datasheets for every populated shared L11/L24 consumer plus
  parent-rail capability, SM6115 pad/power-control and SPMI-arbiter
  documentation, non-scanning USID and runtime APID/descriptor-base evidence,
  verified safe test points, a qualified rail-recovery procedure, independent
  review and separate owner approval `[U]`.
- Phase 2 tracking is prepared as a template in §6. It records provenance,
  applicability, extraction and conflict fields, but no document has been
  acquired `[U]`.
- No device state, firmware, image, access policy or activation code was changed
  by this checklist `[S]`.
- The next legitimate work is host-only: qualify and cite missing documents when
  supplied, and run targeted analysis that answers a specific open question. Do
  not run another controller-local probe, do not scan PMIC slaves, do not repeat
  an unchanged static trace of the same pinned bytes as if it were new evidence,
  and do not treat this checklist as approval for any device action `[S]`.

If the documents are unavailable, this blocker list is the outcome; a new
controller-local image or an unchanged static trace of the same pinned bytes is
not a substitute `[S]`.

## 9. References

| Reference | Use |
| --- | --- |
| `docs/sdhc-decision-memo.md` | Canonical `E1`–`E9`/`S1`–`S4` gates, dispositions, closure criteria and approval conditions |
| `docs/sdhc-electrical-contract.md` | Candidate definition, contract matrix, evidence gaps and measurement requirements |
| `docs/sdhc-power-pad-dependency-report.md` | Consolidated power/pad dependencies, measurement-only baseline and bus-power design gate |
| `docs/power-retention-evidence.md` | Transition-by-transition retention evidence and bounded RPM/XBL trace |
| `docs/pmic-audit.md` | PMIC/SPMI audit corrections, identity and live-access blockers |
| `docs/pm6125-identity-evidence.md` | PM6125 identity model, rail numbering and vendor consumer assignment |
| `docs/pm6125-register-map.md` | L11/L24 SPMI bases, common offsets and voltage-decoding caveats |
| `docs/emmc-reference.md` | eMMC, controller and GPT inventory |
| `docs/sdhc-dependency-trace.md` | CX, ICC, GCC, pads, SMMU and IRQ prerequisites beyond L11/L24 |
| `docs/evidence-baseline.md` | Current-tree hashes, pinned-source cache provenance and artifact relationships |
| `docs/sdhc-decision-update-2026-10-30.md` | Short corrected decision update |

No device action is authorized by this checklist. Keep SDHCI bus power and the
card clock off, do not acknowledge `CORE_PWRCTL_STATUS`, do not access PMIC/SPMI
or send RPM votes, and do not scan candidate slaves `[S]`.
