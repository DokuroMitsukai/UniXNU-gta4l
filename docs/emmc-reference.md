# eMMC reference inputs

Status: inventory and design reference only. No XNU eMMC driver exists yet.

## Local tablet evidence

The live recovery FDT was captured read-only and is byte-identical to the
pinned stock FDT:

- `live-fdt.dtb` SHA-256:
  `da1cb76fcf9031de8ee2f1152456994220d3d01a2a2b73b211b684e92a0b6ebc`
- Primary GPT SHA-256:
  `98bcfd9a5cb89e4d2d01311573986a0330f462a4aba29e5ee3d221e44160aeac`
- Backup GPT SHA-256:
  `12f803503905977e29e0220974c482b88da020946088f0407365717953e81202`

Both GPTs validate: protective MBR, header CRC, entry-array CRC, 84 entries,
80 nonempty partitions, no overlaps, identical primary/backup arrays.

| Resource | Tablet value |
| --- | --- |
| Host MMIO | `0x04744000`, size `0x1000` |
| CQHCI MMIO | `0x04745000`, size `0x1000` |
| CQHCI ICE MMIO | `0x04748000`, size `0x8000` |
| Compatible | `qcom,sdhci-msm-v5`, `qcom,sdhci-msm-cqe` |
| Interrupts | `hc_irq`, `pwr_irq`, `tb_trig_irq` |
| GCC clocks | `iface_clk`, `core_clk`, `ice_core_clk` |
| GCC reset | `core_reset` |
| Supplies | PM6125 L24 (`vdd`), L11 (`vdd-io`) |
| Bus width | 8-bit |
| Card | DX68MB, 61,071,360 × 512-byte sectors |
| Boot partition | GPT index 69, LBA 2,255,360–2,451,967 |
| Userdata | GPT index 80, LBA 15,203,328–61,071,326 |

The GPT partition list matches recovery's `mmcblk0p1` through `mmcblk0p80`.
The earlier shifted 76-entry map is invalid and must not be used.

## Linux reference

Repository:
`https://github.com/limonabisi/linux-gta4l`

Branch: `gta4l-7.2`

Pinned commit:
`cf5f763f0ef8a5b4d8bb4163833cba1160c51894`

Files inspected:

- `arch/arm64/boot/dts/qcom/sm6115.dtsi`
- `arch/arm64/boot/dts/qcom/sm6115-samsung-gta4l.dts`

Credits from those files:

- SM6115 SoC DTSI: Copyright (c) 2021 Iskren Chernev.
- gta4l board DTS: Copyright (c) 2026 V6lhost.
- The files carry `GPL-2.0+ OR BSD-3-Clause` SPDX identifiers.

The Linux tree independently corroborates the tablet's controller MMIO
ranges, eight-bit bus, controller/power interrupts, GCC SDCC1 clocks/reset,
and adds questions we must verify before using them:

- `VDDCX` power-domain and SDHC OPP requirements.
- XO clock input.
- system/config interconnect bandwidth paths.
- SMMU stream ID `0x00c0`.

These are reference findings, not imported code or approved XNU settings. Any
later source reuse requires a separate licensing and integration decision.

The pinned port enables `sdhc_1` without `vmmc-supply` or `vqmmc-supply`. It
defines `pm6125_l24a` and `pm6125_l11a`, but neither phandle is used anywhere
in the board DTS; the only `sdhc_2` supplies are L22 and L5 for the removable
SD card. The port's RPM path is GLINK over `sram@45f0000`, not the live FDT's
legacy `qcom,rpm-smd` node, and its regulator driver reports only cached
software state. The port therefore relies on the eMMC/L11/L24 rail enable and
voltage state left by firmware; it does not give XNU a regulator readback. The
SDHCI `core_vendor_spec` pad-switch bits are not inherited: the probe's `0xa9c`
write clears them and `caps == 0` prevents re-enable. See
`rpm-power-interface.md` and, for the pinned `drivers/mmc/host/sdhci-msm.c`
line ranges at commit `cf5f763f0ef8a5b4d8bb4163833cba1160c51894`,
`linux-regulator-lifecycle.md`.

## guamp reference

Repository:
`https://github.com/itsChris122/UniXNU-guamp`

Pinned commit:
`6be83ac10b4eee84d504266bb8d12ef64a083acc`

Its `drivers/NativeEmmc` describes polled PIO and read-only storage, but the
same source also contains persistent `kpan` logging, writable windows, panic
flush hooks, and optional automatic resets. It is useful for identifying
questions and failure stages only; no code is copied or translated. The
separately audited `Dracen-W/NativeEmmc` revision below is the primary
NativeEmmc reference.

## NativeEmmc reference (Redmi Note 9)

Repository:
`https://github.com/Dracen-W/NativeEmmc`

Maintainer: [Dracen](https://github.com/Dracen-W)

Pinned commit:
`4980f1070b0c822dedb9eb350df710a57df28590` (2026-10-25)

Catalog entry:
`https://unixnu.com/drivers/community-native-emmc`

The source declares `BSD-2-Clause-Patent`; its `LICENSE` file contains the
ordinary BSD-2-Clause text. The discrepancy is a reuse blocker, and no code is
imported here.

This driver targets the MediaTek MT6768 MSDC controller for the Redmi Note 9
(merlinx), not the Qualcomm SDHCI v5 controller used by gta4l. Its register
map, command encoding, FIFO path and hardcoded userdata LBA are not
transferable. It is useful as a design reference for a future XNU read-only
block device: staged boot-argument enablement, `IOBlockStorageDevice`
publication, write rejection, a global controller lock, fail-closed manifest
parsing and exact-one-match `IOMedia` lookup. It assumes the firmware left the
controller and card initialized, which the gta4l handoff does not satisfy.

The static audit in [native-emmc-reference-audit.md](native-emmc-reference-audit.md)
records the reusable patterns, the MediaTek-specific parts, the
completion/error-path gaps and the host-only test plan. It adds no hardware
result and does not change any gta4l blocker.

## Open questions before implementation

1. What state does Mu-Silicium leave the controller, clocks and card in after
   `ExitBootServices`?
2. Which v5/CQE/ICE shutdown or reset sequence is mandatory before PIO?
3. Which clock, reset, regulator and interconnect operations are safe and
   sufficient for conservative reads?
4. How does XNU map the controller MMIO and publish a read-only block device?
5. Which sector is stable and non-sensitive enough for the first read check?

The first question is answered for the controller and pads by
`firmware-handoff.md`: the Mu-Silicium SDCC DXE `ExitBootServices` handler
configures the SDC1/SDC2 TLMM pads, enables SDHCI bus power, runs the v5
reset/power handshake and writes vendor registers; the reset clears the standard
power and clock registers and the handler does not re-enable bus power. It has
no PMIC, SPMI, LDO
or RPM-request operation. The PMIC exit handler does issue SPMI writes, but only
at register bytes `0x14`, `0x16` and `0xd1`, which cannot directly reach the LDO
control registers `0x40`/`0x41`/`0x45`/`0x46`/`0x48`/`0x4c`/`0x61`; `0x14`/`0x16` are the PMIC interrupt latch-clear
and enable-clear registers on peripherals that implement that layout, and the
`+0x28` entry is the SPMI-arbiter interrupt status/clear path, not a regulator
access. The UEFI platform config has no LDO entries. This
is consistent with, but does not establish, a retained-rail hypothesis; it does
not measure the rails or establish the card state. See `pmic-audit.md`.

The first hardware implementation must be observation-only: map verified
registers, read only safe status/version fields, report them through the
working RAM-root diagnostic, and perform no command submission or storage
writes. The first opt-in probe and its register allowlist are documented in
`docs/sdhci-snapshot.md`. That probe ran on the tablet and reported a valid v5
core version (`v=10000072`). The next step is the separate read-only handoff
audit in `docs/sdhci-handoff-audit.md`; it observes the GCC and v5 vendor
state at the audit point in `bsd_init` without writing or acknowledging power
requests. Its first boot image was withdrawn after a packaging failure; the
corrected image preserves the working v5 boot header, firmware, boot DTB and
boot kernel section, and then booted and completed the read-only capture. The
capture reports `bcr=00000000`, `ac=00004221`, `ahb=00000001`, `ice=00004229`,
`cmd=00000000`, `cfg=00000401`, `clk=0003`, `pwr=00`, `pst=00000000` and
`dll=00000000`. This is register-level handoff evidence, not proof that the
eMMC rails are powered or that the card can communicate. The live FDT resolves
`vdd`/`vdd-io` to RPM-SMD PM6125 L24/L11, but this tree has no XNU
RPM/SPMI/regulator path. The bounded host-local probe in `sdhci-host-init.md`
therefore keeps bus power and the card clock off and treats the power stage as
blocked.
