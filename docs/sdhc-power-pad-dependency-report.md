# gta4l SDHC power and pad dependency report

Date: 2026-10-30

Status: host-only consolidation `[S]`. No device write, image build, flash,
boot, power change, PMIC/SPMI access or RPM vote was made for this report. The
device decision remains **NO-GO** (`sdhc-decision-memo.md`); this report does
not authorize bus-power activation, card-clock activation, card commands, DMA
or storage access.

Tags: `[S]` established by a pinned source, captured DT, traced firmware bytes,
or recorded host evidence; `[C]` conditional inference with the condition
stated; `[U]` not established by the available evidence.

## 1. Purpose and limits

This report consolidates the remaining dependencies for the
`sdhc-bus-power-activation` candidate: the eMMC VCC/VCCQ rails, the SDC1 pads,
the firmware handoff, the controller power-request/acknowledge path, and the
required sequencing, delays and recovery. It also records the CX, clock, fabric
and SMMU dependencies that matter after bus power, because a bus-power result
must not be mistaken for card-readiness.

The detailed derivations remain in `sdhc-electrical-contract.md`,
`sdhc-dependency-trace.md`, `power-path-decision.md`,
`power-retention-evidence.md`, `rpm-power-interface.md`, `firmware-handoff.md`
and `sdhci-host-init.md`. This report adds one host-side decoding: the exact
controller-local power handshake in the booted `SdccDxe` exit handler. It does
not repeat the earlier source traces and does not measure any rail, pad or
current.

## 2. Inputs

| Input | Use |
| --- | --- |
| Pinned Linux `cf5f763f...` (`sdhci-msm.c`, `sdhci.c`, `mmc-core.c`, `core.c`, `rpmpd.c`, `icc-*.c`, `pinctrl-sm6115.c`, `pinctrl-msm.c/.h`, `sm6115.dtsi`) | Power-request sequence, rail APIs, OPP/ICC/CX dependencies and TLMM tile/pad definitions `[S]` |
| Captured vendor `live.dts` and pinned `board.dts`/`sm6115.dtsi` | L24/L11 requests, pad states, consumers, controller wiring `[S]` |
| Captured `rpm.img`, `xbl.img`, `xbl_config.img` | LDOA tables, boot PMIC settings, bounded static trace `[S]` |
| Booted Mu-Silicium `SdccDxe.efi` and `PmicDxeLa.efi` bytes | Exit-handler power/pad/PMIC behavior `[S]` |
| `sdhci-handoff-audit.md`, `sdhci-host-init.md`, both reproduced-kernel evidence packets | Controller-local handoff and host-init results `[S]` |
| Recovery eMMC identity | Samsung `DX68MB`, CID `150100445836384d42009005f4661900`, CSD `d02701320f5903fff6dbffef8e404000`, 61,071,360 logical sectors `[S]` |

The eMMC identity is a read-only recovery observation. The CID/CSD identify the
card; they are not a datasheet and do not establish voltage, current or timing
limits `[S]/[U]`.

## 3. Summary

| Dependency | Proven `[S]` | Conditional `[C]` | Missing `[U]` | Memo ID |
| --- | --- | --- | --- | --- |
| VCC, PM6125 L24 | Vendor `sdhc_1` requests 2.96 V (range 2.704–3.600 V, 570 mA); shared with `ufshc`; RPM-owned; pinned port sends no L24 vote; captured RPM has LDOA 24 tables | If XBL/RPM left L24 enabled and no indirect exit effect disables it, the rail is present at handoff | Actual voltage, enable and power-good; absolute maxima; load; ramp/inrush; UFS population | `E1`, `E3`, `E4`, `E7` |
| VCCQ, PM6125 L11 | Vendor `sdhc_1` requests 1.8 V (range 1.704–1.952 V, 325 mA); shared with `ufshc`; declared 1.8 V IO domain; RPM-owned; no L11 vote | Pad domain is 1.8 V if L11 is enabled and the pads use that supply | Actual rail and pad domain; pull-up source; unpowered-drive limits; absolute maxima; load | `E1`, `E2`, `E3`, `E4`, `E7` |
| SDC1 pads | Pinned `sm6115.dtsi` TLMM tiles `west=0x500000`, `south=0x900000`; pinned `pinctrl-sm6115.c` places the SDC1 QDSD group at `0x75000` and SDC2 at `0x73000`; UEFI writes `0x9fe4` to `0x575000` and `0x1fe4` to `0x973000`; `0x9fe4` decodes to the vendor `sdc1_*_on` drive/pull fields; no XNU TLMM code | The SDC1 pad supply is 1.8 V if L11 is enabled and the pads use it; the decoded state is the vendor active configuration, not a measured level | Actual pad voltage and state, pull-up source, unpowered-drive and back-power limits | `E2`, `E6` |
| Firmware handoff | `SdccDxe` sets the bus-power bit, resets the controller, clears/acknowledges the resulting power status, and does not re-enable bus power; the later XNU audit reports `pwr=00`; `PmicDxeLa` writes only `0x14`/`0x16`/`0xd1` and the `+0x28` arbiter path; no direct LDO write found | Rails remain as XBL/RPM left them if no indirect PMIC effect occurs | Runtime `+0x28` bitmap, indirect PMIC effects, actual rail state and retention across transitions | `E5`, `E7`, `E8` |
| Power request/ack | Linux order is `POWER_CONTROL` → power IRQ → `set_vmmc` → `set_vqmmc` → `set_pincfg` → acknowledge; UEFI writes the `IO_SUCCESS` result bit when the captured status has neither bus request bit set (including zero), or the `BUS_SUCCESS` result bit when either is, without a status-dependent rail or pad action; XNU maps no power IRQ and acknowledges nothing | A bare XNU `POWER_CONTROL` write may leave a pending request | PM6125 fault semantics; whether no-ack or result-without-rail-action is safe; the documented acknowledge-or-not decision | `E5`, `E8`, `S1` |
| Sequencing, delays, recovery | Linux software order; UEFI reset/power order with bounded status/clear polls (up to 10,001 reads each) and a 10,000-read reset self-clear poll; BOOT-partition backup/rollback | None beyond the software order | Electrical order and minimum delays; rail safe-stop, discharge/power-cycle and re-init; acceptance criteria; independent review | `E5`, `E9`, `S3` |
| CX, clocks, fabric, SMMU (post-power) | The Linux probe requests CX SVS_PLUS and both ICC paths attempt votes; GCC branches enabled and block reset self-cleared; internal clock stable; PIO does not use the SDHC DMA master; `sdhc_1` DT has SMMU stream `0xc0` | Retained CX/fabric state may suffice for PIO if it was left high | Physical CX voltage, ICC/fabric state, SMMU stream state, card-clock source rate | Beyond the bus-power candidate; gates card commands/DMA |

No row is closed. DT ranges and current levels are operating intent, not
absolute-maximum ratings or measurements `[S]`.

## 4. Decoded firmware power handshake

The booted `SdccDxe.efi` (`4084bbc7...`) exit handler at `0x81a8` performs a
controller-local sequence; the reset clears the standard power and clock
registers and the handler does not re-enable bus power `[S]`:

1. `0x69ac` reads `POWER_CONTROL` (`0x29`), clears bit 0, ORs the requested
   value, and writes the byte back. The handler calls it with `1`, so it sets the
   bus-power bit and preserves the existing voltage-select field. It does not
   select a voltage `[S]`.
2. `0x6458` receives the drive in `w0` and the reset value in `w1`; the caller
   passes `1`. It captures the current `POWER_CONTROL` byte, writes the `w1`
   reset argument to `SOFTWARE_RESET` (`0x2f`), and enters the
   status/clear/result path only when `w1 == 1` and the captured
   `POWER_CONTROL` bus-power bit was set. If either condition is false, it
   skips directly to the reset poll and writes no `CORE_PWRCTL_CTL` value `[S]`.
3. The status poll is a do-while shape: at most 10,001
   `CORE_PWRCTL_STATUS` (`0x240`) reads and 10,001 one-unit delay calls, with
   early exit on a nonzero value. On exhaustion it forces the captured status
   to zero and continues. It writes the captured value to `CORE_PWRCTL_CLEAR`
   (`0x248`) and polls `0x240` to zero for at most 10,001 reads and 10,001
   delays; on exhaustion it continues with the captured value unchanged. It
   then writes `CORE_PWRCTL_CTL` (`0x24c`) with `4` when the captured status
   satisfies `captured_status & 0x3 == 0`, or `1` otherwise. The selection
   therefore uses the initially captured status, not the final clear-poll value:
   zero status selects `IO_SUCCESS`; an IO-only status (bits 2-3) also selects
   `IO_SUCCESS`; bus-only (bits 0-1) and mixed bus/IO status select
   `BUS_SUCCESS` only. Under the pinned Linux definitions, `CORE_PWRCTL_CTL`
   carries result bits, not request bits: bit 0 is `BUS_SUCCESS`, bit 1
   `BUS_FAIL`, bit 2 `IO_SUCCESS`, bit 3 `IO_FAIL`. The request names
   `BUS_OFF`/`BUS_ON`/`IO_LOW`/`IO_HIGH` (bits 0-3) belong to
   `CORE_PWRCTL_STATUS`. The handler writes no failure bit and performs no
   action that depends on the status `[S]`.
4. It waits for `(0x2f & w1)` to become zero, reading at most 10,000 times with
   no delay. On exhaustion it returns normally; the caller then writes vendor
   registers `0x21c`/`0x220` for each processed drive and clears bit 18 of
   `0x20c` only when the drive equals the separately selected `w19` drive
   (`0x6d28` is gated by `cmp w19, w20`) `[S]`.

The three bounded loops do not abort the sequence. If no status arrives, the
handler still writes zero to `CORE_PWRCTL_CLEAR` and `IO_SUCCESS`; if the
status never clears, it still writes the result selected from the captured
value; if the reset bit never self-clears, it still returns and the caller
proceeds to the vendor writes. A success-bit write therefore records the branch
the handler took, not that the controller completed the handshake `[S]`.

This decoding pins the instruction bytes and models the loop behaviour; it does
not execute the firmware `[S]`. The static regression check is
`scripts/test-gta4l-sdccdxe-decode.py`.

The pad write is separate from this handshake. `0x3f04(drive)` is called
earlier for each processed drive and writes `0x9fe4`/`0x1fe4` unconditionally;
it is not a response to the power status. No rail write exists in the handler,
and no pad write is conditioned on the status `[S]`.

The handler has no PMIC, SPMI, LDO or RPM request. `PmicDxeLa` separately clears
PMIC interrupt latches/enables and touches the SPMI-arbiter `+0x28` path; those
writes cannot directly reach the L24/L11 voltage, mode or enable registers
`[S]`. The UEFI sequence is therefore a controller-local reset followed by a
result-only power-status write, not a bus-power-on regulator sequence. It must
not be copied as a template for enabling bus power `[S]/[C]`.

This narrows `E5` and `E8` by documenting the firmware's exact handshake and ack
choice. It does not establish that the rails are on, that the pads are in the
required domain, or that a result-only write without a rail action is safe
`[U]`.

### 4.1 Decoded SDC1 pad write

The same exit handler calls `0x3f04(drive)` before the power/reset sequence. The
pinned mainline `sm6115.dtsi` defines three TLMM tiles: `west` at `0x00500000`,
`south` at `0x00900000`, and `east` at `0x00d00000`. The pinned
`drivers/pinctrl/qcom/pinctrl-sm6115.c` defines the SDC1 QDSD pingroup at
`ctl_reg = 0x75000` on the west tile and the SDC2 group at `ctl_reg = 0x73000`
on the south tile, with the pull and drive fields at the bit positions below.
The UEFI writes therefore land on `0x00575000` (SDC1) and `0x00973000` (SDC2),
not on a PMIC or GCC page `[S]`.

| Field (SDC1 QDSD, `0x9fe4`) | Bits | Decoded value | Vendor `live.dts` `_on` state |
| --- | --- | --- | --- |
| Data drive | 0-2 | `4` → 10 mA | `sdc1_data_on`: 10 mA |
| CMD drive | 3-5 | `4` → 10 mA | `sdc1_cmd_on`: 10 mA |
| CLK drive | 6-8 | `7` → 16 mA | `sdc1_clk_on`: 16 mA |
| Data pull | 9-10 | `3` → pull-up | `sdc1_data_on`: pull-up |
| CMD pull | 11-12 | `3` → pull-up | `sdc1_cmd_on`: pull-up |
| CLK pull | 13-14 | `0` → no pull | `sdc1_clk_on`: bias-disable |
| RCLK pull | 15-16 | `1` → pull-down | `sdc1_rclk_on`: pull-down |

The SDC1 value is therefore the vendor active pad configuration, not an
arbitrary write. The pinned SDC2 group at `0x73000` defines only CLK, CMD and
DATA fields (pull starts at bits 14, 11 and 9; drive at bits 6, 3 and 0); it has
no RCLK entry. In those SDC2 fields, `0x1fe4` decodes to CLK drive 16 mA with no
pull, CMD drive 10 mA with pull-up, and DATA drive 10 mA with pull-up. The raw
bits 15-16 are zero; bit 15 is the upper CLK-pull bit and bit 16 is outside the
pinned SDC2 fields. The data-drive field is 10 mA while the captured
`sdc2_data_on` state is 16 mA; SDC2 is the removable-card controller and is
outside this eMMC candidate `[S]`.
This closes the write-to-pad mapping part of `E2`. It does not establish the pad
supply voltage, the actual pad levels at handoff, the pull-up source, or the
unpowered-drive/back-power limits `[U]`. The `uefiplat.cfg`
`Sdc1GpioConfigOn = 0x1E92` / `Off = 0xA00` values are a separate configuration
map entry; their relationship to the `0x9fe4` register write remains `[U]`.

## 5. What software cannot establish

| Missing input | Why it is required | Available evidence is not a substitute |
| --- | --- | --- |
| PM6125 datasheet or vendor register description | Absolute maxima, LDO electrical limits, soft-start, fault semantics, `QPNPINT` applicability and the `+0x28` bitmap meaning | DT ranges, register readback and the bounded RPM/XBL trace report commands or structure, not electrical limits or runtime state `[S]` |
| eMMC datasheet for Samsung `DX68MB` | VCC/VCCQ2 operating and absolute ranges, power-up sequence, inrush and initial-state requirements | CID, CSD, sector count and recovery operation identify the card; they do not state its limits `[S]` |
| Board schematic, netlist and population evidence | L24/L11 test points, decoupling, UFS population, shared loading, SDC1 pad supply and pull-up source | Vendor DT declares consumers and parents; it does not prove population, loading or net topology `[S]` |
| Calibrated physical measurement | Actual L24/L11 voltage, enable/power-good, pad levels, current, ramp and transients at handoff | `pwr=00` is a controller-local bit; PMIC readback is commanded state; neither is a rail measurement `[S]` |
| RPM runtime policy | L11/L24 retention across handoff, suspend, shutdown and reboot; whether any runtime LDOA request exists | The bounded static trace finds no referenced static selector and no direct LDO write; it does not establish runtime policy `[S]/[U]` |
| Qualified recovery and independent review | Safe stop, discharge/power-cycle, re-init order, acceptance criteria and a non-author review | BOOT-partition rollback repairs software only; it cannot repair electrical damage `[S]` |

A software register read or a DT request cannot close these items. No speculative
PMIC scan, SPMI transaction, RPM vote or pad write is authorized to try to close
them `[S]`.

## 6. Next actionable experiment: measurement-only baseline

The first justified device experiment is the baseline measurement described in
`sdhc-decision-memo.md` §6.2. It does not enable bus power or the card clock and
issues no card command. It is still a separate device procedure with its own
approval and recovery requirements.

| Item | Requirement |
| --- | --- |
| Observation point | A read-only post-handoff state that does not enable bus power or the card clock; the existing host-init panic screen leaves both off and issues no rail write `[S]` |
| Quantities | L24 DC voltage, L11 DC voltage, SDC1 CLK/CMD/DAT DC levels and pull behavior, with a common ground reference; current only with a calibrated probe or known shunt `[C]` |
| Instruments | Calibrated DMM for DC; optional differential probe and oscilloscope for transients; recorded calibration, resolution and uncertainty. Required bandwidth and acceptance limits are not established until the datasheets are available `[U]` |
| Connection points | Test points or component pads identified from a verified schematic; do not probe PMIC balls directly; use short ground returns and avoid shorting adjacent pads `[C]` |
| Procedure | Verify instruments and ground; take at least two stable samples at the observation point; record rail, pad and controller state together; do not write `POWER_CONTROL`, `CORE_PWRCTL_CLEAR`/`CTL`, PMIC/SPMI/RPM or TLMM `[C]` |
| Acceptance | Record values and compare only against limits established by the applicable datasheets. DT ranges are operating intent, not absolute maxima; do not invent pass/fail thresholds `[S]` |
| Stop conditions | Any rail outside a documented limit, unexpected current, brownout/reset, unexpected card-clock activity, thermal event or unexpected power request: stop and follow the separately reviewed recovery method. `E9` is not yet qualified, so this procedure cannot be approved without one `[U]` |

The measurement can narrow `E1`, `E2`, `E7` and `S2`. It cannot close `E3`–`E6`,
`E8`, `E9`, `S1`, `S3` or `S4`. If no schematic and safe test points are
available, those documents and connection points are the exact blockers; a new
controller image would not substitute for them `[S]`.

## 7. Bus-power-only design gate

A host-only bus-power design may be prepared only as a design, not as an
activation path, and must first resolve the acknowledge choice:

- no acknowledge: the controller may leave a pending request, and its behaviour
  is not established `[U]`;
- acknowledge without a rail action: the controller may treat the request as
  satisfied even though no regulator or pad action occurred; the UEFI handler
  writes a success result after a reset, not a bus-on request, so it does not
  justify this choice `[S]/[U]`;
- acknowledge after rail/pad actions: this requires the PMIC/RPM/pad ownership
  and the `E1`–`E9` evidence that is currently absent `[U]`.

Any future design must also define the exact register allowlist and widths,
preconditions, bounded waits, abort end state, simulated success/timeout/failure
cases, a trace oracle and negative tests. It must keep the card clock off, issue
no command, and avoid DMA, CQE, ICE and storage writes. `S1` remains open until
that design exists and passes its host checks `[S]`.

## 8. Disposition

**NO-GO remains.** The corrected UEFI handshake decoding narrows `E5`/`E8` and
the pad decoding closes the write-to-pad mapping part of `E2`, but no electrical
`E*` or `S*` gate is closed. The next legitimate step is the measurement-only
baseline above, or obtaining the missing datasheets, schematic and recovery
procedure. Another controller-local probe adds no evidence for the electrical
contract and is not justified by this report `[S]`.

## 9. References

| Reference | Use |
| --- | --- |
| `docs/sdhc-decision-memo.md` | Canonical `E1`–`E9`/`S1`–`S4` blockers and NO-GO decision |
| `docs/sdhc-electrical-contract.md` | Candidate definition, contract matrix and measurement requirements |
| `docs/sdhc-dependency-trace.md` | CX, ICC, GCC, pads, SMMU and IRQ prerequisites beyond L11/L24 |
| `docs/power-path-decision.md` | Rail ranges, consumers and RPM-vs-SPMI decision |
| `docs/power-retention-evidence.md` | Transition-by-transition retention evidence and bounded RPM/XBL trace |
| `docs/rpm-power-interface.md` | RPM resource IDs, LDOA tables and transport limits |
| `docs/firmware-handoff.md` | Mu-Silicium exit-handler trace and booted-driver provenance |
| `docs/sdhci-host-init.md` | Existing controller-local probe, policy and host test results |
| `docs/sdhci-handoff-audit.md` | Read-only GCC/vendor handoff observations |
| `docs/emmc-reference.md` | eMMC, controller and GPT inventory |
| `docs/sdhc-decision-update-2026-10-30.md` | Short corrected decision update |
| Pinned `pinctrl-sm6115.c` and `sm6115.dtsi` at `cf5f763f...` | TLMM tile bases, SDC QDSD group offsets and field positions |
| `out/gta4l/ram-root-sdhci-repro-kernel-host-init-evidence/README.md` | Reproduced-kernel host-init result and its limits |
