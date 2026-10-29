# PM6125 identity evidence inventory

Status: host-only source and device-tree inventory. No device connection, hardware
access, compilation, flashing or power change was made. This document records what
the pinned sources and the captured vendor device tree do and do not establish
about the PM6125, the eMMC rails and the board electrical contract. It does not
measure either rail.

## Inputs

| Input | Reference |
| --- | --- |
| Mainline port | `https://github.com/limonabisi/linux-gta4l`, branch `gta4l-7.2`, commit `cf5f763f0ef8a5b4d8bb4163833cba1160c51894` |
| Vendor device tree | `device-dump/private/gta4l-dump/live.dts` (decompiled capture; current post-unlock state, not authenticated stock) |
| Live FDT copy | `out/gta4l/ram-root-fs-timer-evidence/live-fdt.dtb` |

Inspected pinned files: `arch/arm64/boot/dts/qcom/pm6125.dtsi`,
`arch/arm64/boot/dts/qcom/pmi632.dtsi`,
`arch/arm64/boot/dts/qcom/sm6115.dtsi`,
`arch/arm64/boot/dts/qcom/sm6115-samsung-gta4l.dts`,
`drivers/regulator/qcom_smd-regulator.c`.

All statements below are source- or DT-established description unless explicitly
labelled otherwise. The vendor FDT is a description of intended hardware, not a
measurement of the silicon on this tablet.

## Identity model

The board is modelled as a PM6125 + PMI632 pair. Both mainline and vendor trees
declare the PM6125 twice; the vendor tree also declares the PMI632 twice
(USID 2 and 3):

| Node | `reg` | Mainline `pm6125.dtsi` / `pmi632.dtsi` | Vendor `live.dts` |
| --- | --- | --- | --- |
| `qcom,pm6125@0` | `<0x00 0x00>` | yes (USID 0) | yes |
| `qcom,pm6125@1` | `<0x01 0x00>` | yes (USID 1) | yes |
| `qcom,pmi632@2` | `<0x02 0x00>` | yes (USID 2) | yes |
| `qcom,pmi632@3` | `<0x03 0x00>` | yes (USID 3) | yes (USID 3) |

This is a device-tree topology description. It does not establish the silicon
revision, the die variant, or which of the two PM6125 USIDs carries a given LDO
peripheral.

## Rails: numbering is established, USID is not

The board uses `qcom,rpm-pm6125-regulators` under `&rpm_requests`, not the AP-side
`qcom,pm6125-regulators` SPMI driver. The RPM regulator table in
`qcom_smd-regulator.c` (`rpm_pm6125_regulators[]`) defines the two eMMC rails:

| Rail | RPM resource | Descriptor | Parent supply name |
| --- | --- | --- | --- |
| L11 | `QCOM_SMD_RPM_LDOA`, id 11 | `pm660_ht_lvpldo` | `vdd_l9_l11` |
| L24 | `QCOM_SMD_RPM_LDOA`, id 24 | `pm660_pldo660` | `vdd_l23_l24` |

The vendor tree agrees on the numbering: `rpm-regulator-ldoa11` has
`qcom,resource-id = <0x0b>` and `rpm-regulator-ldoa24` has
`qcom,resource-id = <0x18>`, both `qcom,regulator-hw-type = "pmic5-ldo"`. It
assigns the rails through the RPM resource path, not through an SPMI address, so
the numbering, not the USID, is what the pinned evidence supports.

The descriptor names are shared with PM660, which is why the register map is
source-backed but the subtype and voltage encoding still depend on a hardware
read. See `pm6125-register-map.md` for the base addresses and getter split.

## Consumer assignment

The mainline `sdhc_1` node in `sm6115.dtsi` has **no** `vdd-supply` or
`vdd-io-supply`; `board.dts` only sets `non-removable` and `status = "okay"`. The
L11/L24-to-eMMC assignment is therefore **vendor-FDT description**, not a
mainline-port fact:

| Consumer | Property | Phandle target | Init / level | Range |
| --- | --- | --- | --- | --- |
| `sdhci@4744000` | `vdd-supply` | `regulator-l24` (`pm6125_l24`) | `0x294280` = 2,704,000 µV init; `qcom,vdd-voltage-level` `0x2d2a80` = 2,960,000 µV | `0x294280`–`0x36ee80` (2.704–3.600 V) |
| `sdhci@4744000` | `vdd-io-supply` | `regulator-l11` (`pm6125_l11`) | `0x1a0040` = 1,704,000 µV init; `qcom,vdd-io-voltage-level` `0x1b7740` = 1,800,000 µV | `0x1a0040`–`0x1dc900` (1.704–1.952 V) |

The same consumer node declares `qcom,vdd-io-always-on` and
`qcom,vdd-io-lpm-sup`. The regulator nodes themselves declare no `always-on` or
`boot-on`; those booleans are on the consumer.

A second consumer shares both rails in the vendor tree: `ufshc@4804000` has
`vcc-supply = <0x1f7>` (L24) and `vccq2-supply = <0x1f0>` (L11), with
`vccq2-pwr-collapse-sup`. The mainline tree does not enable this UFS node, so the
shared-consumer picture is vendor description only.

The mainline board file separately marks L5 and L22 `regulator-always-on` with
"broken hw" comments, but those are the `sdhc_2` SD-card rails, not the eMMC
L11/L24 rails. They must not be conflated with the eMMC supplies.

## What is not established

- **USID for the LDO peripherals.** Bounded to PM6125 USID 0 or 1; not chosen.
  Neither mainline `pm6125.dtsi` node instantiates `qcom,pm6125-regulators`, and
  the vendor tree has no regulator subnode under either USID.
- **SPMI subtype and voltage encoding.** Depends on a hardware read of
  `0x04`/`0x05`; the descriptor name alone does not decide the getter family.
- **Silicon revision and die variant.** No revid value is recorded here.
- **Board electrical contract.** No datasheet, schematic or measurement is
  present: absolute-maximum ratings, transient behaviour, back-powering, safe
  sequencing between supply, I/O rail, pads and clocks, and a qualified recovery
  procedure are all unresolved.
- **Retention across reset, suspend and shutdown.** The RPM firmware owns the
  rails; its retention behaviour is not established by these files.
- **Pad / I/O domain at handoff.** Whether the eMMC I/O is in the 1.8 V or 3.3 V
  domain at the XNU handoff is not measured.
- **PM6125 interrupt-layout applicability.** Whether the LDO peripherals expose
  the `0x14`/`0x16` `QPNPINT` layout is unresolved; see `pmic-audit.md`.
- **Actual rail voltage or readiness.** No power-good or ADC measurement exists.

## Consequence

The pinned evidence establishes the identity model, the rail numbering and the
vendor consumer assignment. It does not establish the SPMI slave assignment, the
variant, or the electrical contract. That is exactly the blocker recorded in
`pmic-audit.md`: live PMIC/SPMI access stays blocked, and candidate-slave scanning
is not a substitute for the missing evidence.

The next legitimate inputs are a pinned PM6125 register description or schematic
and a qualified physical measurement. Neither is present in this repository. The
itemized electrical-contract gap and the RPM-vs-SPMI recommendation are in
`power-path-decision.md`.
