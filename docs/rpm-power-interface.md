# gta4l RPM/regulator power interface

Status: source investigation only. No XNU code, build, boot or hardware access
was added. The pinned tablet port's RPM transport exposes no hardware readback
for PM6125 L24/L11 and does not describe either rail as an `sdhc_1` supply. A
separate SPMI register map is recorded in `pm6125-register-map.md`, and
`pmic-audit.md` blocks live access to it. This document is evidence of a
retained-rail design, not proof of the XNU-time rail state.

## Source pin

The reference is the tablet-specific port, not a generic upstream tree:

- Repository: `https://github.com/limonabisi/linux-gta4l`
- Branch: `gta4l-7.2`
- Commit: `cf5f763f0ef8a5b4d8bb4163833cba1160c51894`
- Raw prefix:
  `https://raw.githubusercontent.com/limonabisi/linux-gta4l/cf5f763f0ef8a5b4d8bb4163833cba1160c51894/`

Inspected files:

- `arch/arm64/boot/dts/qcom/sm6115.dtsi`
- `arch/arm64/boot/dts/qcom/sm6115-samsung-gta4l.dts`
- `arch/arm64/boot/dts/qcom/pm6125.dtsi`
- `drivers/regulator/qcom_smd-regulator.c`
- `drivers/soc/qcom/smd-rpm.c`
- `drivers/soc/qcom/rpm-proc.c`
- `drivers/rpmsg/qcom_glink_rpm.c`
- `drivers/rpmsg/qcom_glink_native.c`
- `drivers/mmc/host/sdhci-msm.c`
- `drivers/mmc/core/regulator.c`
- `drivers/regulator/core.c`
- `include/linux/soc/qcom/smd-rpm.h`

## Board wiring

The mainline board enables `sdhc_1` but does not give it regulator supplies:

```dts
&sdhc_1 {
	non-removable;
	status = "okay";
};
```

The SoC DTSI describes the same controller resources as the live FDT, plus the
RPM power-domain and interconnect requirements:

| Resource | Mainline value |
| --- | --- |
| Controller | `mmc@4744000`, `qcom,sm6115-sdhci`, `qcom,sdhci-msm-v5` |
| MMIO | `0x04744000+0x1000`, `0x04745000+0x1000`, `0x04748000+0x8000` |
| IRQs | GIC SPI 348 `hc_irq`, SPI 352 `pwr_irq` |
| Clocks | `GCC_SDCC1_AHB_CLK`, `GCC_SDCC1_APPS_CLK`, `RPM_SMD_XO_CLK_SRC`, `GCC_SDCC1_ICE_CORE_CLK` |
| Reset | `GCC_SDCC1_BCR` |
| Power domain | `SM6115_VDDCX` via `rpmpd` |
| OPPs | 100 MHz low-SVS, 192 MHz low-SVS, 384 MHz SVS-plus |
| SMMU | `apps_smmu` stream 0x00c0 |
| Interconnects | `sdhc-ddr`, `cpu-sdhc` |

The live FDT also reports `tb_trig_irq`; the mainline DTSI lists only `hc_irq`
and `pwr_irq`.

### RPM transport

The mainline RPM node is GLINK-based:

| Resource | Mainline value |
| --- | --- |
| RPM proc | `/remoteproc`, `qcom,sm6115-rpm-proc`, `qcom,rpm-proc` |
| GLINK edge | `qcom,glink-rpm`, IRQ GIC SPI 194 edge rising |
| Mailbox | `apcs_glb` channel 0, `mailbox@f111000`, size `0x1000` |
| RPM service | `qcom,rpm-sm6115`, `qcom,glink-smd-rpm`, channel `rpm_requests` |
| RPM message RAM | `sram@45f0000`, `0x045f0000+0x7000` |
| SMEM | `memory@46000000`, `0x46000000+0x200000`, hwlock `tcsr_mutex 3` |

`qcom,glink-smd-rpm` is the RPM service carried over GLINK. The compatible
name is historical; this board does not use the legacy SMD transport described
by the live FDT's `qcom,rpm-smd` node. The RPM transport uses the message-RAM
SRAM, not the SMEM region. SMEM is used by other edges and by the SMEM state
machines.

## PM6125 L24/L11 in the mainline port

The board DTS defines the two rails but does not reference either one:

```dts
pm6125_l11a: l11 {
	regulator-min-microvolt = <1704000>;
	regulator-max-microvolt = <1952000>;
	regulator-allow-set-load;
};

pm6125_l24a: l24 {
	regulator-min-microvolt = <2704000>;
	regulator-max-microvolt = <3600000>;
	regulator-allow-set-load;
};
```

`grep` over the pinned DTS/DTSI shows only these definitions: no `vmmc-supply`,
no `vqmmc-supply`, and no other phandle consumer for L24 or L11. The only
`sdhc_2` supplies are L22 (`vmmc`) and L5 (`vqmmc`); those are the removable SD
card rails, not the eMMC rails.

The regulator table maps both to the same RPM resource type and different IDs:

| Rail | RPM type | RPM ID | Mainline range | Mainline consumer |
| --- | --- | --- | --- | --- |
| L24 | `QCOM_SMD_RPM_LDOA` (`0x616f646c`) | 24 | 2.704–3.600 V | none |
| L11 | `QCOM_SMD_RPM_LDOA` (`0x616f646c`) | 11 | 1.704–1.952 V | none |
| L22 | `QCOM_SMD_RPM_LDOA` | 22 | 2.952–2.960 V | `sdhc_2` vmmc |
| L5 | `QCOM_SMD_RPM_LDOA` | 5 | 1.648–2.960 V | `sdhc_2` vqmmc |

The live stock FDT differs. It resolves the controller's `vdd`/`vdd-io` to
`/soc/qcom,rpm-smd/rpm-regulator-ldoa24/regulator-l24` and
`.../rpm-regulator-ldoa11/regulator-l11`, reports `qcom,init-voltage`
2.704 V/1.704 V, `qcom,vdd-voltage-level` 2.96 V, `qcom,vdd-io-voltage-level`
1.8 V, and carries `qcom,vdd-io-always-on` and `qcom,vdd-io-lpm-sup`. Those are
vendor properties. The mainline `sdhci-msm` driver does not parse them.

## RPM request format

The RPM service is not a register interface. `qcom_rpm_smd_write()` sends a
packet over the `rpm_requests` rpmsg channel:

```text
qcom_rpm_header  { service_type = "req\0", length }
qcom_rpm_request { msg_id, flags, type, id, data_len }
payload[]        { key, nbytes, value } ...
```

All fields are little-endian. `flags` is `0` for the active state and `1` for
sleep. The regulator driver writes the active state only. The packet is limited
to less than 256 bytes, and completion is a response containing `msg#` or
`err`, with a 5-second timeout.

The regulator keys are:

| Key | Value | Meaning |
| --- | --- | --- |
| `RPM_KEY_SWEN` | `0x6e657773` (`swen`) | software enable |
| `RPM_KEY_UV` | `0x00007675` (`uv`) | voltage in microvolts |
| `RPM_KEY_MA` | `0x0000616d` (`ma`) | load in milliamps |

`rpm_reg_write_active()` only includes changed keys. It includes `uv` and `ma`
only when the cached `is_enabled` flag is set. Enabling a rail with no voltage
update therefore sends only `swen`; disabling sends only `swen=0`. There is no
read/query request in this path.

## Firmware resource tables (bounded trace)

A bounded, host-only trace of the captured `rpm.img` (SHA-256
`272bebd3ddd7ef223d6e95e9b4cd30385a3df0eb4fa7beae6b622f457b4937c6`) found the
RPM firmware's own LDOA resource tables `[S]`. The packaged
`analysis/disassembly/rpm.asm.zst` is a different build and was used only as a
layout hint; the findings below come from capstone disassembly of the captured
bytes `[S]`.

- 24 `ldoa` key records at VA `0x4696994`–`0x4696a54`, 8 bytes each (`ldoa` plus
  id), in id order 1–24. L11 is at `0x46969e4` and L24 at `0x4696a4c` `[S]`.
  The L11 record has exactly one stored pointer in the whole image
  (`0x46966a8`, the selector slot); L24 has none `[S]`.
- A 24-entry voltage table at VA `0x4696a54`, 12 bytes per entry, aligned with
  the key records `[C]`. L11's entry (`0x4696acc`) carries 1704–1952 mV and
  L24's (`0x4696b68`) carries 2704–3600 mV, matching the board DTS ranges above
  `[C]`. The other fields are not decoded `[U]`.
- A NULL-terminated selector array at VA `0x469667c` whose 16 stored words,
  under the link-base-relative interpretation `[C]`, select ids 6, 8, 1, 2, 3,
  4, 7, 17, 18, 9, 10, 11, 12, 13, 14 and 16: L11 is present, L24 is absent
  `[S]`. The array's only stored pointer is the unreferenced three-entry table
  at `0x46966fc`–`0x4696714`; no linked path found by the whole-image scan
  selects it `[S]`. Computed addressing, runtime relocation or aliasing remains
  `[U]`.
- A generic key/value handler at `0x4615da4` that consumes `uv` (32-bit
  microvolts), `swen` (a software-enable bit in its state object), `bobm`,
  `pcv1`–`pcv3`; it has no selector conversion and no PMIC command ID `[S]`.
  The class/ID path at `0x4615d08` could address any `ldoa` id, including 11 or
  24, if a runtime descriptor existed. The static registration at `0x461749e`
  supplies a count of 25 `[S]`; the setter at `0x4617b48` compares the id
  against a runtime object field and the mode against 4 `[S]`. The class table
  at `0x4695774` is zero in the linked image and the object is built at runtime
  by the resource-database constructor at `0x46996c4`, so the runtime count and
  ids 11/24 passing the bounds are `[C]`. The bounded scan finds no static
  table or policy path that selects either rail `[S]`; whether a runtime
  request exists is `[U]`.

No linked code path found by the scan reads the voltage table or selector
array `[S]`, and no traced path writes an L11/L24 voltage, mode or enable
register `[U]`. The only path identified by the scan that can program either
rail is the generic setter, driven by a runtime request whose existence is not
established `[U]`. The tables are therefore evidence that the firmware carries
LDOA resource definitions for both rails, not evidence that it enables, votes
or retains them. They do not add a readback path to the mainline port `[S]`.
The stored-word counts are reproducible with
`scripts/analyze-gta4l-rpm-ldoa.py`; computed addressing, runtime relocation
and aliasing remain outside its scope `[U]`.

## What the mainline port can and cannot report

The regulator driver's read operations are software-cache reads:

- `rpm_reg_is_enabled()` returns `vreg->is_enabled`.
- `rpm_reg_get_voltage()` returns `vreg->uV`.
- Both fields start at zero and are changed only by the driver's own
  enable/disable/set-voltage calls.

The driver has no RPM query command, no PMIC register read, and no completion
value that reports the hardware voltage or enable state. The mainline
`pm6125.dtsi` exposes PON, thermal, ADC, RTC and GPIO through SPMI, but it does
not instantiate an SPMI regulator node, because the board configures the rails
through `qcom,rpm-pm6125-regulators`.

The RPM transport is therefore not a readback path, but it is not the only one.
`qcom_spmi-regulator.c` does carry a PM6125 regulator table, and the L11/L24
register addresses it defines are the hardware state the RPM firmware programs.
That map and its read-safety analysis are recorded in
`pm6125-register-map.md`. It is a separate access path with its own cost: it
needs the PMIC arb, not the RPM channel.

The GLINK transport is also not read-only. `qcom_glink_rpm_probe()` writes zero
to the TX head and RX tail, then `qcom_glink_native_probe()` sends a GLINK
version command. Opening the `rpm_requests` channel sends an open request and
intent handshake. A read-only auditor may inspect the TOC and FIFO pointers
without writing, but it cannot send an RPM request without becoming a writer.

## Why the mainline port works without supply phandles

`sdhci-msm` calls `mmc_regulator_get_supply()`. Missing supplies are optional
and remain error pointers. `sdhci_msm_set_vmmc()` and
`sdhci_msm_set_vqmmc()` return success without a regulator action when the
supply is absent. `sdhci_msm_set_regulator_caps()` likewise does nothing
without `vqmmc`; the probe's earlier `0xa9c` write has already cleared the
`core_vendor_spec` pad-switch bits, and with `caps == 0` nothing re-enables them.

On a bus-power request, the driver can acknowledge `CORE_PWRCTL_BUS_SUCCESS`
after pin configuration even when no regulator action was performed. The
mainline port therefore depends on the eMMC/L11/L24 rail enable and voltage
state that firmware left behind, which it does not read back. The SDHCI
`core_vendor_spec` pad-switch bits are not inherited: the probe's `0xa9c` write
clears them and `caps == 0` prevents re-enable. The retained rail state is the
retained-rail contract. See `linux-regulator-lifecycle.md` for the pinned
`drivers/mmc/host/sdhci-msm.c` line ranges at commit
`cf5f763f0ef8a5b4d8bb4163833cba1160c51894`.

The regulator core does not necessarily undo that state. The mainline RPM
regulator's `is_enabled` field starts at zero and is only changed by an
enable/disable call. Registration does call the set-voltage path because the DT
gives both a minimum and a maximum, but `rpm_reg_write_active()` only includes the
`uv` key when the cached `is_enabled` is set, so the update stays in the cache.
The late cleanup then reads `_regulator_is_enabled()`, sees the cached zero for an
unused L24/L11, and skips the disable. This is a software-cache effect, not
evidence that the hardware rail is off.

`linux-regulator-lifecycle.md` traces the whole sequence, including the absent
supplies, the probe's `0xa9c` write that clears the pad-switch bits (not
re-enabled because `caps` stays 0) and the power-IRQ acknowledgement.

This explains why the port can work while its DT has no eMMC supply consumer.
It does not prove that the rails are on at the XNU handoff, and it does not give
XNU a way to read them.

## Safety conclusion

There is no supported read-only RPM regulator query in the pinned port. The
only read-only observations available from the transport are TOC/FIFO
metadata, and those do not encode L24/L11 voltage or enable state.

Consequently:

- Do not register L24/L11 in an XNU regulator driver just to read them. A
  registration can create cached state and shutdown/cleanup behavior that the
  mainline port avoids.
- Do not send RPM `swen`, `uv` or `ma` votes to learn the state. A vote is a
  write and can change shared-consumer aggregation.
- Do not add an SPMI write path. Reading L11/L24 through SPMI is a different
  question: `pm6125-register-map.md` records source-backed addresses for the
  enable and voltage selectors, but the read path also writes an arbiter command
  register, its mapping walk is outside the transaction timeout, and the slave
  assignment is unresolved. `pmic-audit.md` blocks live access.
- Do not set `SDHCI_POWER_CONTROL` bus power or the card clock, and do not
  acknowledge `CORE_PWRCTL_STATUS`, until the retained state is proven or a
  corresponding regulator action exists.
- Keep the working RAM-root path unchanged.

The host-local SDHCI milestone remains the safe stopping point.

## Optional transport-only audit

A bounded audit could validate the RPM message-RAM layout without touching
power:

- Opt-in boot argument; no mapping or read when absent.
- Map `0x045f0000+0x7000` read-only.
- Read the 256-byte TOC at `0x045f0000+0x6f00`.
- Require magic `0x67727430` (`grt0`) and `count <= 20`.
- Validate each entry's `offset` and `size`; require `offset+8 <= 0x7000` and
  cap the readable FIFO span at `min(size, 0x7000 - offset - 8)`. The upstream
  check covers only `offset+size`, not the FIFO's 8-byte header.
- Find `ap2r` (`0x61703272`) and `r2ap` (`0x72326170`); read the tail/head
  words at `offset` and `offset+4`.
- Report TOC count, entry IDs, offsets, sizes, head/tail and occupancy.
- Do not write head/tail, do not ring the APCS mailbox, do not enable the GLINK
  IRQ, do not send a version/open/intent/RPM message, and do not touch SPMI or
  SDHCI.

This audit would prove only that the transport map is readable and that the
rings have the expected shape. It would not establish L24/L11 state and would
not unblock the power stage. It is not implemented here because it does not
answer the power question and would add another reserved-memory access to the
diagnostic path.

## Firmware handoff trace

The Mu-Silicium DXE exit path is recorded separately in
`firmware-handoff.md`. The SDCC exit handler configures the SDC1/SDC2 TLMM
pads, enables SDHCI bus power, runs the v5 reset/power handshake and writes
vendor registers; the reset clears the standard power and clock registers and
the handler does not re-enable bus power. It has no PMIC, SPMI, LDO or
RPM-request path. The PMIC exit
handler does issue SPMI writes, but only at register offsets `0x14`, `0x16` and
`0xd1`, which cannot address the LDO control offsets
`0x40`/`0x41`/`0x45`/`0x46`/`0x48`/`0x4c`/`0x61`. The UEFI
platform config has no LDO entries. This is consistent with, but does not
establish, the retained-rail reading, and it does not measure L24/L11.

## Next decision

The source trace is complete, and a register map is recorded in
`pm6125-register-map.md`. That document is not a working read path: the audit in
`pmic-audit.md` blocks live access because the slave assignment and the
board-level electrical contract are unresolved. The `+0x28` instruction sequence
and its v5 register interpretation are recorded as SPMI-arbiter interrupt
status/clear; the runtime bitmap and the clear-register read-back value remain
unresolved, and none of that authorises live access.

The mainline port does supply the base addresses, the match table, both voltage
getters, the arbiter offsets, the board constraints and the RPM resource ids. It
does not supply the PM6125 slave assignment, the actual subtype on this tablet,
the live arbiter configuration at handoff, the actual voltage or readiness, or
the electrical limits. It also uses RPM-owned regulation, not AP-side SPMI
access. `pm6125-register-map.md` tabulates the split.

Until that evidence exists, keep the power stage blocked. Do not implement an RPM
vote, an SPMI regulator write, an AP-side SPMI read, a bus-power enable or a
card-clock enable. If a bus-power-only probe is later justified, it must
allowlist every operation, bound every wait, report failures, and not
acknowledge a vendor power request; that path needs its own approved design.
