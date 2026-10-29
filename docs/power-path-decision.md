# eMMC power-path decision: L11/L24 electrical contract and RPM-vs-SPMI

Status: host-only research `[S]`. No device connection, build, flash, network
fetch, power change or commit was made `[S]`. Existing documents are unchanged
`[S]`. The Step 4 contract assessment is in
`docs/sdhc-electrical-contract.md`; the Step 6 decision is in
`docs/sdhc-decision-memo.md` `[S]`.

Tags used throughout:

| Tag | Meaning |
| --- | --- |
| `[S]` | Established by a pinned source, the captured DT, or a repository search quoted here |
| `[C]` | Conditional or inference; true only under the stated condition |
| `[U]` | Not established by the available evidence |

## Inputs and limits

| Input | Use |
| --- | --- |
| `/tmp/opencode/linux-verify/{pm6125,pmi632,sm6115}.dtsi`, `board.dts` | Pinned mainline board and SoC wiring `[S]` |
| `qcom_smd-regulator.c`, `sdhci-msm.c`, `rpmpd.c`, `opp-core.c`, `core.c` | Pinned RPM regulator, SDHCI, power-domain and regulator-core behaviour `[S]` |
| `device-dump/private/gta4l-dump/live.dts` | Captured vendor DT; post-unlock state, not authenticated stock `[S]` |
| Existing `docs/*.md` | Prior host-only traces and their stated limits `[S]` |
| `docs/power-retention-evidence.md` | Transition-by-transition retention evidence and its limits `[S]` |
| `docs/sdhc-dependency-trace.md` | Remaining `sdhc_1` dependencies beyond L11/L24 and what XNU lacks `[S]` |

`core.c` and `regulator-core.c` in the input set are byte-identical
(SHA-256 `9896430492f89add…f8fd`); both are the regulator core, not
`drivers/mmc/core/core.c` `[S]`. The later dependency trace fetched the pinned
`sdhci.c` and `mmc-core.c` into `/tmp/opencode/linux-verify` and records their
hashes in `sdhc-dependency-trace.md` §7.1 `[S]`. MMC-core sequence claims below
come from `linux-regulator-lifecycle.md` and that pinned copy, not from an
unidentified tree `[C]`.

## Part 1 — Board-level electrical-contract gap inventory

The repository contains software descriptions of rail *operating intent* `[S]`.
It does not contain an electrical contract `[U]`. Device-tree ranges are
operating constraints, not absolute-maximum ratings `[C]`. The Step 4
assessment in `docs/sdhc-electrical-contract.md` turns this inventory into a
candidate-specific matrix and records nine blocking safety-critical areas
(`E1`–`E9`) `[S]`.

### Known rail description

| Rail | Mainline DT range | Vendor DT range / init | `sdhc_1` current level (vendor DT) | Parent input supply | Vendor output consumers |
| --- | --- | --- | --- | --- | --- |
| L11 | 1.704–1.952 V `[S]` | 1.704–1.952 V, init 1.704 V `[S]` | `vdd-io` 0–325,000 µA `[S]` | `vdd_l9_l11`, shared with L9 `[S]` | `sdhc_1` vdd-io; `ufshc` vccq2 `[S]` |
| L24 | 2.704–3.600 V `[S]` | 2.704–3.600 V, init 2.704 V `[S]` | `vdd` 0–570,000 µA `[S]` | `vdd_l23_l24`, shared with L23 `[S]` | `sdhc_1` vdd; `ufshc` vcc `[S]` |

The current levels are board-description values, not measurements `[C]`. The
mainline board declares both rails but gives them no consumer `[S]`. The vendor
`ufshc` node is `status = "ok"` and shares both output rails `[S]`; it also
declares `vcc-max-microamp` and `vccq2-max-microamp` of 600,000 µA each `[S]`,
so the `sdhc_1` levels above are not the only declared load `[C]`.

### Contract items: present versus absent

| Contract item | Present in the repository | Absent | Tag |
| --- | --- | --- | --- |
| Absolute-maximum ratings | Operating min/max ranges above; enable mask `0x80` at register `0x46`; vendor current ceilings | Absmax for VCC, VCCQ2, SDC pads and CMD/DAT; any distinction between operating range and damage threshold | ranges `[S]`; absmax `[U]` |
| Transient behaviour | SPMI common `SOFT_START` register `0x4c` exists in the map; regulator core has a `soft_start` hook; RPM ops expose no `set_soft_start`, `enable_time` or `off_on_delay`; board DT sets no ramp property | Slew rate, inrush, overshoot/undershoot, brownout, settling time, load-step response, power-up/down ramp | `[S]` for the hooks; `[U]` for the behaviour |
| Back-powering | Vendor `ufshc` has `vccq2-pwr-collapse-sup`; `sdhc_1` has `qcom,vdd-io-lpm-sup` | Which pins may be driven while VCC/VCCQ2 is off; pad-to-rail leakage limits; whether card or SoC IO can back-power a rail | properties `[S]`; requirement `[U]` |
| Supply / IO / pad / clock sequencing | Linux order in the power IRQ: `set_vmmc` → `set_vqmmc` → `set_pincfg` → acknowledge; probe writes `0xa9c`, clearing the pad-switch bits; power control writes `SDHCI_POWER_CONTROL` only; signal switch writes `SDHCI_HOST_CONTROL2` | Required order and timing between L24, L11, pads, card clock and controller reset; whether IO may be present before VCC; minimum delays | software order `[S]`; electrical order `[U]` |
| Fault-interrupt consequences | SDHCI IRQ reads `CORE_PWRCTL_STATUS`, writes `CLEAR` with 10 retries, then acks via `CTL`; PMIC `0x14`/`0x16` are `QPNPINT` latched-clear / enable-clear on that layout | PM6125 applicability; the actual fault sources; whether clearing or masking disables protection; what a `BUS_SUCCESS` ack with no rail action means electrically | SDHCI/arbiter `[S]`; PM6125 `[U]` |
| Shared consumers | Vendor table above; shared parent inputs L9/L23; mainline has no consumer | Whether UFS is populated on this SKU; real load sharing; decoupling; net topology; whether L23/L9 loading couples into L24/L11 through the shared input | topology `[S]`; loading `[U]` |
| Qualified recovery procedure | BOOT-partition backup/rollback and payload gates | Rail-level recovery (power cycle, discharge time, re-init order), acceptance criteria, and a measurement that confirms recovery | software rollback `[S]`; rail recovery `[U]` |

Two notes limit the table. First, two outputs sharing an input supply is not the
same as sharing an output net `[C]`. Second, the vendor rail nodes declare
`qcom,set = <0x03>` while the mainline RPM driver writes only the active state
`[S]`; retention across suspend and shutdown is therefore not established `[U]`.

### Repository search result

| Sought | Result |
| --- | --- |
| Schematic, netlist, PCB or CAD source | None found in the checkout `[S]` |
| PM6125 / PMI632 datasheet or vendor register description | None found `[S]` |
| Downstream Android / vendor kernel source tree | None found; only pinned mainline DTS and driver copies `[S]` |
| Board files | Mainline DTS, vendor `live.dts`, and `UniXNU-sm6115-common/dts/sm6115.dtsi` `[S]` |
| Electrical measurement records | None; only controller-local register captures and screen photographs `[S]` |
| Software / register maps | `pm6125-register-map.md`, `linux-regulator-lifecycle.md`, `firmware-handoff.md` `[S]` |

The recorded `host-init-success.txt` reports SDHCI registers, not a rail
voltage, current or pad measurement `[S]`. No power-good, ADC or oscilloscope
record for L11/L24 exists in the repository `[U]`.

## Part 2 — Decision memo: RPM-owned regulation vs AP-side SPMI

### Option (a): RPM-owned regulation matching the pinned port

The board uses `qcom,rpm-pm6125-regulators`, so the rails are programmed by RPM
firmware, not by an AP-side SPMI regulator `[S]`. In the pinned port, L11 and
L24 have no consumer and receive no `swen`, `uv` or `ma` vote; the getters
return only cached software state `[S]`. "RPM-owned" therefore has two forms:

| Form | Description | Status |
| --- | --- | --- |
| (a1) Retained rail | Firmware owns L11/L24; the AP sends no vote and cannot read them back | This is what the pinned board does `[S]` |
| (a2) AP RPM votes | The AP sends `swen`/`uv`/`ma` over the GLINK `rpm_requests` service | Not done for L11/L24 by the pinned port; a new control path `[S]` |

(a1) matches the proven design and avoids AP-side SPMI ownership `[S]`. It does
not provide rail readback, and it depends on firmware retention that the AP does
not verify `[U]`. (a2) is a write path that can change shared-consumer
aggregation, and XNU has no RPM/GLINK regulator component in this tree `[S]`.

Evidence required before any live test of (a):

| Requirement | Status |
| --- | --- |
| Pinned RPM resource IDs and board constraints | Present: LDOA 11 and 24 `[S]` |
| RPM transport actually used at handoff (GLINK vs the live FDT's legacy SMD node) | Not established `[U]` |
| RPM firmware behaviour for L11/L24 across AP suspend, shutdown and reboot | Not established `[U]` |
| Shared-consumer aggregation and whether an AP vote would perturb UFS | Not established `[U]` |
| For (a2): a host-tested, allowlisted, bounded XNU RPM client | Absent `[U]` |
| A rail measurement at the XNU handoff | Absent `[U]` |
| The Part 1 electrical contract | Absent `[U]` |

### Option (b): AP-side SPMI access

`qcom_spmi-regulator.c` maps L11 to peripheral `0x4a` and L24 to peripheral
`0x57`, with enable at `0x4a46`/`0x5746` `[S]`. The path needs the PMIC arbiter,
not the RPM channel `[S]`. A "read" issues one observer-window command write per
transaction, and the 1000 µs poll does not bound the APID mapping walk `[S]`.
The slave USID is bounded to 0 or 1 but not chosen, and voltage decoding depends
on a subtype read `[S]`. That identity gap blocks an AP-side SPMI transaction
`[S]`; it does not block a direct rail measurement. Approval of a safe
measurement procedure is a separate gate from bus-power activation and does not
require the measurement record it is intended to produce; the record is still
absent `[U]`. Option (b) would report commanded enable and voltage state, not
measured voltage or power-good `[C]`.

Evidence required before any live test of (b):

| Requirement | Status |
| --- | --- |
| Pinned USID assignment and L11/L24 variant | Absent `[U]` |
| Subtype / revision from a non-scanning source | Absent `[U]` |
| Per-address PMIC read-safety statement | Absent `[U]` |
| Arbiter version and APID ownership at handoff | Absent `[U]` |
| Proof that AP access does not conflict with RPM ownership | Absent `[U]` |
| Host-testable arbiter client with an independent event trace | Absent `[U]` |
| A rail measurement and the Part 1 electrical contract | Absent `[U]` |

### Comparison

| Criterion | (a) RPM-owned | (b) AP-side SPMI |
| --- | --- | --- |
| Matches the pinned board design | Yes `[S]` | No; the board does not instantiate the SPMI regulator `[S]` |
| Rail owner | RPM firmware `[S]` | Observer reads take no ownership; a write path would require write ownership and could conflict with RPM `[C]` |
| Hardware-state readback | None `[S]` | Commanded state only `[C]` |
| Identity blocker | None for (a1) `[C]` | USID and subtype unresolved `[S]` |
| Transport work | None for (a1); GLINK client for (a2) `[U]` | PMIC-arbiter client `[S]` |
| Electrical contract still required | Yes `[C]` | Yes `[C]` |
| Live test authorized now | No `[S]` | No `[S]` |

The USID/subtype gap is an option-(b) transaction blocker. It is not a
precondition for a direct rail measurement. A measurement procedure still needs
its own safety approval, and the electrical contract and measurement record are
required before bus-power activation, not before approving that procedure `[S]`.

### Recommendation

Adopt (a), specifically the retained-rail form (a1), as the only legitimate
architecture for this project now `[C]`. It is the design the pinned board
actually uses, and it avoids inventing an AP-side SPMI write owner while the
slave assignment, subtype and transport bounds are unresolved `[S]`. The Step 4
contract assessment does not change this recommendation; it records the
bus-power-only candidate as blocked, and the Step 6 memo records **NO-GO**.

Do not treat this as authorization for a live test `[S]`. The immediate next
step is host-only: turn Part 1 into a written requirements checklist and build a
host-side model plus negative tests for whatever RPM path is later proposed `[C]`.
Option (b) is not legitimate now because it has unresolved identity, a
write-bearing transport, and cannot establish electrical safety; observer reads
alone would not take ownership, but the option as scoped includes the transport
bring-up `[C]`. If AP control is later required, (a2) is a new RPM-vote design
that needs its own evidence, not a rename of (a1) `[C]`.

### Missing evidence, highest priority first

1. Board schematic/netlist or PM6125/PMI632 datasheet covering absmax, transients, back-powering and sequencing `[U]`.
2. A rail measurement at the XNU handoff: L24 and L11 voltage, current, pad state and clock state `[U]`.
3. Pinned USID plus subtype/revision for the L11/L24 peripherals, required
   before any AP-side SPMI transaction and separate from measurement approval
   `[U]`.
4. RPM firmware retention behaviour across AP suspend, shutdown and reboot `[U]`.
5. Shared-consumer population and loading, including UFS and the L9/L23 input supplies `[U]`.
6. PM6125 interrupt-layout applicability and the consequence of clearing or masking faults `[U]`.
7. A qualified rail-recovery procedure with acceptance criteria `[U]`.
8. Independent review that does not rely on these documents `[U]`.

## Safety disposition

No live test is authorized by this memo `[S]`. Keep SDHCI bus power and the card
clock off, do not acknowledge `CORE_PWRCTL_STATUS`, do not access PMIC/SPMI or
send RPM votes, and do not scan candidate slaves `[S]`. A software rollback
cannot repair electrical damage `[S]`. The working RAM-root path stays unchanged
`[S]`. The Step 4 contract assessment and Step 6 decision memo confirm the
device decision remains **NO-GO** for SDHC bus-power activation (decision memo,
dated 2026-10-29).
