# gta4l SDHC board-level electrical-contract assessment (Step 4)

Status: host-only assessment `[S]`. No device connection, build, flash, boot,
power change or commit was made for this document `[S]`. It uses the pinned
Linux source at `cf5f763f0ef8a5b4d8bb4163833cba1160c51894`, the captured vendor
device tree, the existing firmware traces and the existing controller-local
observations `[S]`. It does not measure a rail, pad or current `[U]`.

Tags: `[S]` source- or DT-established, `[C]` conditional inference with the
condition stated, `[U]` not established by the available evidence.

## 1. Operation being assessed

The capabilities are separate. Approval or evidence for one is not approval for
another `[S]`.

| # | Capability | Current disposition |
| - | --- | --- |
| 1 | Existing controller-local diagnostics (`Gta4lSdhciHostInit`) | Implemented, observed on the tablet, and covered by the host access policy `[S]` |
| 2 | Bus-power activation, with no card command and the card clock off | Selected candidate for this assessment; blocked `[U]` |
| 3 | Card commands / PIO | Not assessed; depends on 2 `[U]` |
| 4 | DMA | Not assessed; depends on 3 and SMMU/ICC state `[U]` |
| 5 | High-speed modes and tuning | Not assessed; depends on 3 and clock-rate control `[U]` |
| 6 | CQE / inline encryption | Not assessed; depends on 3/4 and the CQE/ICE/SCM path `[U]` |

### Selected candidate

| Field | Candidate definition |
| --- | --- |
| Identifier | `sdhc-bus-power-activation` (bus-power-only probe) |
| Starting state | Fresh handoff or the post-`Gta4lSdhciHostInit` state: v5 MCI version valid, software reset self-cleared, internal clock stable, bus power off, card clock off, CMD/DAT inhibit clear, GCC SDCC1 branches enabled and unhalted, SDCC1 block reset deasserted `[S]`. L24/L11 state, pad state and CX state are not established `[U]`. |
| Intended accesses | Read `PRESENT_STATE` (`0x24`) and `CORE_PWRCTL_STATUS` (`0x240`); read/write `POWER_CONTROL` (`0x29`, 8-bit) to enable bus power at the DT-requested voltage; read `CLOCK_CONTROL` (`0x2C`, 16-bit) only to confirm the card-clock bit stays clear; report only `[C]`. A vendor power-request acknowledgement (`CORE_PWRCTL_CLEAR`/`CORE_PWRCTL_CTL`) is **not** in this candidate unless a separately approved design changes the scope `[S]`. |
| Excluded operations | No card-clock enable, card command, response, data, PIO, DMA, CQE, ICE, tuning, high-speed mode change, GCC branch/RCG/PLL/BCR write, PMIC/SPMI access, RPM vote, storage write or partition change `[S]`. |
| Required end state | Bus power on with the card clock still off, no command issued, no DMA and no storage write `[C]`. On abort: bus power off, card clock off, controller reset deasserted, and a report of the observed state `[C]`. The abort and recovery acceptance criteria are not established `[U]`. |
| Success observation | `POWER_CONTROL` bus-power bit set and the selected voltage field as requested; `CLOCK_CONTROL` card-clock enable bit clear; `INT_STABLE`, if set, reflects only the already-established internal clock and is not a card-clock output measurement; no error interrupt; vendor power status reported but not acknowledged `[C]`. No card response is expected because no command is issued `[S]`. |
| Key limit | Powering the interface does not establish that the card, its internal state or the data path is ready `[C]`. This candidate deliberately stops before any command `[S]`. |

The existing host-local diagnostic (capability 1) remains the baseline. It
performs no power, card-clock, pad, regulator, PMIC, RPM or SPMI action `[S]`.
This document does not re-authorize it and does not transfer its software result
to candidate 2 `[S]`.

## 2. Identity and topology inventory

| Item | Evidence | Tag | Limit |
| --- | --- | --- | --- |
| Board / SKU | Samsung Galaxy Tab A7 10.4 (2020), `SM-T505N`, `gta4l`; `README.md`, `dts/gta4l.dts` and `board.toml` | `[S]` | Hardware revision, PCB revision and population variant are not recorded `[U]`. |
| Populated boot storage | eMMC `DX68MB`, 61,071,360 × 512-byte sectors, `mmcblk0` at `sdhci@4744000`; recovery read-only inventory | `[S]` | The card's electrical limits and current state are not measured `[U]`. |
| UFS declaration | Vendor `ufshc@4804000` is `status = "ok"` and consumes L24/L11; mainline does not enable it; recovery boots from eMMC | `[S]` | Physical UFS population, loading and whether it is powered are not established `[U]`. DT declaration is not population `[S]`. |
| L24 / VCC routing | Vendor `sdhc_1` `vdd-supply` → `pm6125_l24`; range 2.704–3.600 V, init 2.704 V, requested level 2.960 V, declared current 0–570,000 µA | `[S]` | These are board-description requests `[S]`; measured levels and regulator capability are not established `[U]`. |
| L11 / IO routing | Vendor `sdhc_1` `vdd-io-supply` → `pm6125_l11`; range 1.704–1.952 V, init 1.704 V, requested level 1.800 V, declared current 0–325,000 µA | `[S]` | Same limit; the actual pad IO domain at handoff is not measured `[U]`. |
| Shared output consumers | Vendor DT declares both `sdhc_1` and `ufshc` on L24 and L11 | `[S]` | Whether both are populated and what they draw is not established `[U]`. |
| Shared parent inputs | L24 parent `vdd_l23_l24` is shared with L23; L11 parent `vdd_l9_l11` is shared with L9 | `[S]` | Parent loading, decoupling and coupling into the eMMC rails are not established `[U]`. |
| SDC1 pads | Vendor pinctrl states: `sdc1_clk` bias-disable/drive 16; `sdc1_cmd` and `sdc1_data` bias-pull-up/drive 10; `sdc1_rclk` bias-pull-down; UEFI builds the TLMM page `0x00575000` for controller 0 and `0x00973000` for controller 1 and writes `0x9fe4`/`0x1fe4` respectively | `[S]` | XNU has no TLMM code; the exact mapping of the UEFI writes to the vendor config values and the handoff pad state are not established `[U]`. |
| Reset | GCC SDCC1 `core_reset`; handoff audit observed `bcr=00000000` | `[S]` | Deasserted GCC block reset is controller-local; the card-side reset pin level and timing are not measured `[U]`. |
| Clock | GCC SDCC1 apps RCG source select 4 (GPLL6), divider 1, M/N off; branches `ac=00004221`, `ahb=00000001`, `ice=00004229`; internal clock stable, card clock off; nominal 384 MHz source | register state `[S]`; 384 MHz nominal `[C]` | `INT_STABLE` is the internal clock, not the card-clock output; source frequency and card-clock output are not measured; changing the source rate needs the RCG `[S/C]`. |
| Firmware / boot provenance | Booted v2 image `d4aee57a...`; live FDT `da1cb76f...`; `SdccDxe`, `PmicDxeLa`, `SPMIDxe` hashes in `firmware-handoff.md` | `[S]` | The traced driver bytes are the booted bytes `[S]`; the current checkout may not reproduce that exact boot, and the image is controller-local evidence, not a rail measurement `[S]`. |

The vendor tree is a description of intended hardware. It distinguishes
declared consumers from physically populated consumers only where independent
evidence exists `[S]`. The only storage observed populated by recovery software
is the eMMC `[S]`.

## 3. Contract matrix

The matrix assesses candidate `sdhc-bus-power-activation`. A requirement is
`blocking` when the available evidence does not establish it for this board and
operation. DT voltage/current requests are software constraints, not proof of
measured levels or damage thresholds `[S]`.

| ID | Area | Required contract for the candidate | Evidence and source | Applies to | Limits / conditions | Tag | Disposition |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `E1` | Supply voltage | Recommended operating ranges and absolute maxima for the eMMC VCC, VCCQ2 and every shared consumer | Vendor DT ranges and requested levels; no PM6125 or eMMC datasheet in the checkout | L24, L11, eMMC, UFS (if populated) | DT ranges are operating intent; absolute maxima and per-component limits are absent | ranges `[S]`; absmax `[U]` | Blocking |
| `E2` | IO compatibility | Pad voltage selection, signalling levels and permitted powered/unpowered combinations | Vendor `bus-speed-mode = HS400_1p8v/HS200_1p8v/DDR_1p8v`; L11 1.8 V request; SDHCI `vdd-io-voltage-level` 1.8 V | SDC1 pads, eMMC VCCQ2, host controller | Actual pad domain, pull-up source and unpowered-drive behaviour are not measured | DT `[S]`; actual `[U]` | Blocking |
| `E3` | Current / loading | Shared loads, regulator capability and operating conditions | Vendor `sdhc_1` 570 mA (VCC) and 325 mA (VDD-IO); vendor `ufshc` 600 mA each; parent inputs `vdd_l23_l24`/`vdd_l9_l11` shared with L23/L9 | L24, L11, eMMC, UFS, parents | Declared current levels are not regulator capability or measured load; UFS population is unknown | declarations `[S]`; capability/load `[U]` | Blocking |
| `E4` | Transients | Ramp, settling, inrush, overshoot, undershoot and brownout requirements | SPMI common `SOFT_START` register exists; regulator core has a `soft_start` hook; no board ramp values | L24, L11, pads, clock lines | No slew rate, inrush, overshoot/undershoot, brownout or load-step data | hooks `[S]`; behaviour `[U]` | Blocking |
| `E5` | Sequencing | Supply, pad, reset and clock ordering, including required delays | Linux power-IRQ order: `set_vmmc` → `set_vqmmc` → `set_pincfg` → acknowledge; controller reset and internal-clock stages are separate; the candidate keeps the card clock off | L24, L11, pads, reset, internal clock | Electrical order and minimum delays are not established; the candidate's no-ack scope leaves the vendor request pending | software order `[S]`; electrical order `[U]` | Blocking |
| `E6` | Back-powering | Restrictions on driving CMD/DAT/CLK/reset while either supply is absent | Vendor `ufshc` `vccq2-pwr-collapse-sup`; vendor `sdhc_1` `qcom,vdd-io-lpm-sup`; no pad leakage data | SDC1 pads, eMMC, host | Which pins may be driven while VCC/VCCQ2 is off, and whether the card or SoC can back-power a rail, are unknown | properties `[S]`; requirement `[U]` | Blocking |
| `E7` | Retention | Rail and pad assumptions across handoff and every transition in the candidate | Pinned mainline port sends no L11/L24 vote and has no readback; Mu-Silicium exit path is not passive; a bounded RPM/XBL trace finds LDOA tables for both rails but no traced L11/L24 voltage/mode/enable write; both XBL RSBP images download to PMIC PBS RAM and reference only L11/L24 `STEP_CTRL`; no measurement | L24, L11, pads, CX | Retention across handoff, suspend, shutdown and reboot is not established; a `pwr=00` controller bit is not rail state; the firmware trace is not a runtime policy result | no vote `[S]`; tables `[S]`; retention `[U]` | Blocking |
| `E8` | Fault handling | Relevant interrupt/status semantics and the consequence of clearing or masking them | SDHCI `CORE_PWRCTL_STATUS`/`MASK`/`CLEAR`/`CTL` semantics; whether the PM6125 uses the `0x14`/`0x16` `QPNPINT` layout is unresolved | Controller power IRQ, PMIC | The candidate does not acknowledge the request; whether that is safe, and what clearing/masking would disable, is unknown | SDHCI `[S]`; PM6125 `[U]` | Blocking |
| `E9` | Recovery | Safe stop, discharge/power-cycle requirements and recovery acceptance criteria | BOOT-partition backup/rollback and payload gates; no rail-level recovery procedure | Controller, rails, card | Software rollback cannot repair electrical damage; rail recovery and acceptance criteria are absent | software `[S]`; rail `[U]` | Blocking |

No row is supported for candidate 2. The only positive evidence is DT intent and
controller-local software state `[S]`. That is not an electrical contract `[S]`.

## 4. Evidence gaps and acquisition requirements

| ID | Gap | Evidence that could resolve it | Why existing evidence is insufficient | Acquisition mode | Blocks candidate? |
| --- | --- | --- | --- | --- | --- |
| `E1` | Absolute maxima and operating limits for L24, L11, eMMC VCC/VCCQ2 and SDC pads | PM6125 and eMMC datasheets; board schematic | DT ranges and requested levels are not absolute-maximum ratings or measured limits | Host-only if documents are available; otherwise vendor documentation | Yes |
| `E3` | Board population, net topology, decoupling and shared loading | Schematic, netlist, PCB/CAD source, population variant | Vendor DT is a description; UFS population and actual load sharing are not established | Host-only if files are available; physical inspection needs separate device approval | Yes |
| `E1`, `E2`, `E3`, `E7` | L24/L11 voltage, current, power-good and pad state at handoff and during activation | Calibrated DMM/oscilloscope measurement at safe test points; PMIC readback is not a substitute | No rail or pad measurement exists; register readback reports commanded state only | Device measurement under a separately approved procedure | Yes |
| `E4` | Ramp, inrush, overshoot/undershoot, brownout and settling | Datasheet transient data plus oscilloscope/current-probe capture | No board ramp values or transient capture exists | Device measurement under separate approval | Yes |
| `E5` | Electrical sequencing and minimum delays | Datasheet and board documentation; controlled measurement | Linux software order is not an electrical order | Host-only documentation; device measurement under separate approval | Yes |
| `E6` | Back-powering and unpowered-drive restrictions | Datasheet, schematic and pad leakage data | The two vendor properties are hints, not limits | Host-only documentation; device measurement under separate approval | Yes |
| `E7` | Retention across handoff, suspend, shutdown and reboot | RPM firmware documentation/reversing plus transition measurements | No pinned source proves retention; the exit path is not passive; a bounded static trace narrowed the policy gap but did not establish a runtime vote or retention | Host-only reversing narrowed it; device measurement needs separate approval | Yes |
| `E8` | PM6125 fault semantics and applicability | PM6125 register description/datasheet; non-scanning identity evidence | USID/subtype and the `QPNPINT` layout applicability are unresolved | Host-only documents; live PMIC access remains blocked by `pmic-audit.md` | Yes |
| `E9` | Qualified rail recovery and acceptance criteria | Written procedure with safe stop, discharge/power-cycle and re-init order; verification measurement | Only BOOT-partition software rollback exists | Device procedure under separate approval | Yes |

PMIC register readback must not be substituted for physical voltage or
power-good evidence `[S]`. A readback reports commanded state, not the measured
rail `[S]`.

## 5. Proposed measurement requirements (not executed)

Any future measurement is a separate device procedure and requires its own
approval. It does not authorize SDHC activation `[S]`.

| Item | Requirement |
| --- | --- |
| Instruments | Calibrated DMM for DC levels; differential probe and oscilloscope with bandwidth and sample rate derived from the fastest documented ramp/transient (the required value is not established `[U]`; 100 MHz is not an acceptance limit); current probe or calibrated shunt with known burden for inrush/steady current; recorded calibration and uncertainty |
| Safe connection points | Test points or component pads on the L24/L11 outputs and ground, identified from a schematic; do not probe PMIC balls directly; use short ground returns; no probing that can short adjacent pads |
| Uncertainty | Record DMM accuracy and resolution, probe bandwidth/attenuation derating, shunt tolerance and temperature coefficient, and common-mode limits; state whether the result resolves the required limit |
| Capture timing | Before the power write, at the write, during ramp and settling, at steady state and at abort; trigger on the register write or power-enable edge; sample fast enough to resolve the specified transient |
| Stop conditions | Any rail outside its applicable documented operating limit, an overshoot above a known limit, unexpected current, brownout/reset, unexpected card-clock activity, thermal event or any unexpected power request; stop the experiment, follow the separately reviewed qualified safe-stop/recovery method required by `E9`, and record the state. Do not assume that removing power is safe or sufficient |
| Non-substitutes | PMIC register readback, DT values, `SDHCI_POWER_CONTROL` bits and firmware traces are not physical voltage or power-good evidence `[S]` |

The bandwidth, sample rate and acceptance limits are not established until the
applicable PM6125/eMMC documentation is available; they are not guessed
thresholds `[U]`. A baseline measurement that does not enable bus power or the
card clock is a separate experiment and may be approved before activation
evidence exists; that approval does not approve activation. A controlled
activation experiment requires its own design, baseline results, stop
conditions, recovery procedure and owner approval. See
`sdhc-decision-memo.md` §6.2–§6.4 `[S]`.

## 6. Exit assessment

Candidate `sdhc-bus-power-activation` has blocking gaps in every
safety-critical area (`E1`–`E9`): supply voltage, IO compatibility,
current/loading, transients, sequencing, back-powering, retention, fault
handling and recovery.
No electrical measurement exists for L24, L11, the SDC1 pads or the card `[U]`.
Step 4 therefore exits as a documented gap assessment, not as a safety case.

The device decision remains **NO-GO** for SDHC bus-power activation; the card
clock remains off. The existing controller-local diagnostic remains the only
observed operation, and its own access policy is assessed separately in
`sdhci-host-init.md` `[S]`. The non-electrical gates `S1`–`S4` and the
consolidated dispositions for `E1`–`E9` are in `sdhc-decision-memo.md` §4; this
document does not close them `[S]`.

## References

| Reference | Use |
| --- | --- |
| `docs/power-path-decision.md` | Rail ranges, consumers and the RPM-vs-SPMI decision |
| `docs/power-retention-evidence.md` | Transition-by-transition retention evidence and gaps |
| `docs/pm6125-identity-evidence.md` | PM6125 identity, rail numbering and vendor consumer assignment |
| `docs/sdhc-dependency-trace.md` | `sdhc_1` prerequisites beyond L11/L24 |
| `docs/firmware-handoff.md` | Mu-Silicium exit trace and booted-image provenance |
| `docs/sdhci-host-init.md` | Existing controller-local probe and its host access policy |
| `docs/sdhci-handoff-audit.md` | Read-only GCC/vendor handoff observations |
| `docs/emmc-reference.md` | eMMC, controller and GPT inventory |
| `docs/sdhc-decision-memo.md` | NO-GO decision, `E*`/`S*` dispositions and approval gates |
| `docs/stock-firmware-analysis-audit.md` | Audited https://github.com/NoNameBuilder stock-firmware analysis; leads, corrections and limits |
| `docs/evidence-baseline.md` | Current-tree revision, hashes and cache provenance |
