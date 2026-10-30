# gta4l porting-approach reassessment, 2026-10-31

Status: host-only decision reassessment `[S]`. No device connection, build,
flash, boot, power change, PMIC/SPMI access, RPM vote or firmware execution was
made for this note. It changes the engineering approach, not the evidence tags:
nothing here marks a missing measurement as established `[S]`.

Owner direction: the device owner directed that the working firmware,
LineageOS/vendor kernel and mainline Linux stack be treated as sufficient
reference material for porting work, and that a full electrical investigation is
not a prerequisite for that work. This note records the direction as an explicit
reassessment rather than a silent bypass of the existing decision record. It
does not claim independent review of this document, and it is not an electrical
measurement.

## 1. What is being reassessed

The earlier record conflated two different questions:

| Question | Earlier framing | Reassessed framing |
| --- | --- | --- |
| Can software porting proceed? | Treated by the working notes as blocked until electrical gates closed | **Yes.** Software porting may proceed using the working stack as the reference; electrical characterization is not a prerequisite for writing, porting or testing host-side software `[S]` |
| Is the on-device bus-power activation operation approved? | `NO-GO` (`sdhc-decision-memo.md`) | **Still `NO-GO` as a device operation.** It requires a separate written owner approval for the exact procedure and a verified EFS/CPEFS backup before any device action `[S]` |

The `NO-GO` in `docs/sdhc-decision-memo.md` is scoped to the operation
`sdhc-bus-power-activation` (bus power on, card clock off, no card command). It
was never a finding that the controller cannot be ported or that Linux is an
invalid reference. The reassessment makes that scope explicit and stops using
the activation gate to block software work `[S]`.

## 2. Reference stack accepted for porting

| Reference | What it establishes | Use |
| --- | --- | --- |
| Mainline Linux `limonabisi/linux-gta4l`, branch `gta4l-7.2`, commit `cf5f763f0ef8a5b4d8bb4163833cba1160c51894` | Board wiring, controller resources, clocks, resets, power domain, interconnect, SMMU stream, SDHCI v5 and regulator/pinctrl behaviour `[S]` | Primary software reference; pinned copies in `/tmp/opencode/linux-verify/` |
| Samsung vendor kernel 4.19.157 source archives (CXG1/CWI2, owner-supplied) | Vendor SDHCI, regulator, RPM and pinctrl behaviour; the source used by downstream Android/LineageOS builds `[S]/[U]` | Behavioural cross-check; not the shipped binary and contains no vendor DTS |
| LineageOS gta4l device tree and kernel configuration | The working Android stack that boots and uses eMMC on this device; microSD support is advertised, but microSD boot is unproven [S]/[U] | Existence proof that the eMMC controller path works under a shipping OS; not an XNU result |
| Arch Linux ARM on gta4l (`limonabisi/archlinux-on-gta4l`) | Public working distribution: U-Boot plus pmOS kernel 7.2, Arch root on eMMC `userdata`; display, touch, GPU, Wi-Fi, Bluetooth, audio, battery, charging and USB reported working `[S]` | Working-stack evidence for the porting approach; not an XNU, microSD-boot or electrical result |
| Captured vendor FDT (`live.dts`, `live-fdt.dtb`) | Actual booted board description, eMMC boot medium, SDC2 wiring, regulator and pad nodes `[S]` | Runtime wiring reference |
| Mu-Silicium UEFI and pinned `SdccDxe.efi` (`4084bbc7...`) | Firmware handoff state and controller-local power/status sequence `[S]` | Handoff contract |
| UniXNU loader source | Read-only `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` access, config path and fallback loader behaviour `[S]` | Boot-path integration |

Accepting these as the reference means the port reproduces documented behaviour
and records where it cannot yet verify it. It does not mean the Linux stack
proves XNU-time rail state, pad levels or retention `[S]/[U]`.

## 3. Dependencies to reproduce

The register below lists what the working stack depends on and what the port
must reproduce. "Retained" means the port relies on firmware-left state and does
not issue a new vote; that is a design choice, not a measurement `[C]`.

| Dependency | Reference value | Port action | Status |
| --- | --- | --- | --- |
| Controller MMIO | `mmc@4744000`: `0x4744000+0x1000`, `0x4745000+0x1000`, `0x4748000+0x8000` | Map the three windows in the SDHCI driver | `[S]` values; driver pending |
| Controller IRQs | GIC SPI 348 `hc_irq`, SPI 352 `pwr_irq`; live FDT also reports `tb_trig_irq` | Wire the two documented IRQs; treat `tb_trig_irq` as `[U]` | `[S]/[U]` |
| Clocks and reset | `GCC_SDCC1_AHB_CLK`, `GCC_SDCC1_APPS_CLK`, `GCC_SDCC1_ICE_CORE_CLK`, `RPM_SMD_XO_CLK_SRC`, `GCC_SDCC1_BCR` | Enable/unhalt and reset through GCC; host-init probe already models the sequence | `[S]` |
| Power domain and OPPs | `SM6115_VDDCX` via `rpmpd`; 100/192/384 MHz OPPs | Retained-state assumption for the first port; add an rpmpd vote only with a documented interface | `[C]` retained; runtime policy `[U]` |
| Interconnect | `sdhc-ddr`, `cpu-sdhc` paths | Retained-state assumption; no ICC vote in the first port | `[C]` |
| SMMU | `apps_smmu` stream `0x00c0` | Program the stream ID for DMA or use a documented bypass only if the firmware contract requires it | `[S]` ID; driver pending |
| eMMC supplies | Vendor DT `vdd`/`vdd-io`; mainline `sdhc_1` declares no supplies; recovery reports `vdd`/`vdd-io` | Use retained rails; do not add a PMIC write to discover state | `[S]/[C]` |
| microSD supplies | `vmmc = pm6125_l22a`, `vqmmc = pm6125_l5a`, both `regulator-always-on` in the mainline board | Use retained rails; no XNU vote required for the first port | `[S]/[C]` |
| Card detect | `cd-gpios = <&tlmm 88 GPIO_ACTIVE_HIGH>`; `sdc2_cd_on/off` | Poll or interrupt GPIO 88; do not assume a card is present | `[S]` |
| Pad states | `sdc2_clk/cmd/data_on/off`; UEFI writes `0x1fe4` to `0x00973000` and `0x9fe4` to `0x00575000` | Reproduce the documented drive/pull configuration; keep unpowered-drive limits open | `[S]` mapping; electrical limits `[U]` |
| SDHCI v5 init | Software reset, internal clock, `POWER_CONTROL`, `CORE_PWRCTL_STATUS/CLEAR/CTL` | Reuse the pinned host-init sequence; do not acknowledge power requests until approved | `[S]` |
| RPM/GLINK transport | `qcom,sm6115-rpm-proc`, GLINK; L11/L24 LDOA resources | Retained firmware rails; no RPM vote in the first port | `[S]` transport; policy `[U]` |
| Boot medium | eMMC `4744000.sdhci`, `fstab_suffix=emmc`; microSD boot untested | Keep eMMC as the documented boot target; treat microSD boot as an experiment | `[S]/[U]` |
| UEFI block stack | `SdccDxe.efi` contains BlockIo, BlockIo2 and SimpleFileSystem GUIDs; DiskIo is absent from the PE; Fat/Partition/DiskIo modules are in the boot chain | Verify at runtime whether SDHC2 publishes a block handle and a FAT volume before relying on it | `[S]` GUIDs; multi-slot/volume `[U]` |
| Loader volume access | UniXNU reads `\EFI\UniXNU` through SimpleFileSystem and adds fallback loaders for other volumes | Keep the RAM-root path as the working default; use the SD volume only if it enumerates | `[S]` |

## 4. Residual risks

These risks stay open. They are documented so the port can proceed without
pretending they are closed `[S]/[U]`:

- No calibrated measurement of L22/L5/L24/L11, SDC1/SDC2 pad levels or current
  exists at XNU handoff `[U]`.
- Back-powering and unpowered-drive limits for the SDC2 pads are not
  established `[U]`.
- PM6125 fault, over-current and soft-start semantics are not established `[U]`.
- RPM retention policy for L11/L24 and the VDDCX/interconnect state is not
  established `[U]`.
- Whether `SdccDxe` publishes BlockIo/BlockIo2 for SDHC2, and whether
  DiskIo/Partition/Fat then exposes a card volume, is not proven `[U]`.
- microSD boot is untested; the documented boot medium is eMMC `[S]/[U]`.
- Vendor source/binary correspondence and the LineageOS-only card-detect blocks
  remain `[U]`.
- No thermal, inrush or sustained-load data exists `[U]`.

These risks bound what may be claimed. They do not block host-side driver work,
host tests or software integration `[S]`.

## 5. Decision

1. **Software porting: proceed.** Use the mainline Linux, vendor/LineageOS and
   captured-firmware evidence as the reference. Electrical characterization is
   not a prerequisite for porting software `[S]`.
2. **On-device bus-power activation: remain `NO-GO` as an operation.** Any
   device action still requires the owner's separate written approval for the
   exact procedure and a verified EFS/CPEFS backup, per `AGENTS.md` `[S]`.
3. **No electrical gate is marked closed.** `E1`-`E9` and `S1`-`S4` remain
   evidence gaps for the activation operation. They no longer gate software
   work `[S]`.
4. **Claims stay tagged.** A working Linux/LineageOS stack is evidence that the
   hardware path works; it is not a measurement of this unit at XNU handoff
   `[S]/[U]`.

The canonical activation decision in `docs/sdhc-decision-memo.md` is unchanged
for the operation it names. Its §11 addendum records this narrower reading and
points here `[S]`.

## 6. Consequences for the existing records

| Record | Consequence |
| --- | --- |
| `docs/sdhc-decision-memo.md` | §11 addendum: `NO-GO` applies to the activation operation, not to software porting |
| `docs/sdhc-evidence-gap-checklist.md` | Remains the activation-evidence inventory; its gates are not porting prerequisites |
| `docs/sdhc-electrical-contract.md` | Remains the electrical contract for activation; not a porting gate |
| `porting.toml` | Storage finding/evidence records the reassessment and the dependency register |
| `docs/evidence-baseline.md` | Reassessment doc added to the hash table; changed files re-hashed |

## References

| Reference | Use |
| --- | --- |
| `docs/sdhc-decision-memo.md` | Canonical activation decision and gate table |
| `docs/sdhc-evidence-gap-checklist.md` | Activation-evidence inventory |
| `docs/sdhc-dependency-trace.md` | Controller, firmware and power dependency trace |
| `docs/sdhc-electrical-contract.md` | Electrical contract for the activation operation |
| `docs/emmc-reference.md` | eMMC identity and mainline reference provenance |
| `docs/firmware-handoff.md` | UEFI module inventory and `SdccDxe` trace |
| `docs/rpm-power-interface.md` | RPM/regulator reference and retained-rail assumptions |
| `docs/archlinux-gta4l-evidence.md` | Public Arch Linux working-stack evidence for gta4l |
| `/tmp/opencode/linux-verify/` | Pinned mainline source cache |
