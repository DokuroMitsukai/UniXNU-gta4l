# PM6125 USID assignment research (L11 / L24)

Status: host-only source and device-tree research. No device connection, network
fetch, build, flash, commit or power change was made. This document asks a single
question and answers it from pinned evidence: **can the PM6125 L11 and L24 LDO
peripherals be assigned to SPMI USID 0 or USID 1?** It also re-checks the L11/L24
SPMI base addresses and the voltage-getter assignment recorded in
`pm6125-register-map.md`.

Tags used throughout: **[S]** source/DT-established (file and line named),
**[C]** conditional inference (condition stated), **[U]** unknown.

## Source pin

| Input | Reference |
| --- | --- |
| Mainline port | `https://github.com/limonabisi/linux-gta4l`, branch `gta4l-7.2`, commit `cf5f763f0ef8a5b4d8bb4163833cba1160c51894` |
| Vendor device tree | `device-dump/private/gta4l-dump/live.dts` (decompiled capture; not authenticated stock) |
| Local pinned copies | `/tmp/opencode/linux-verify/` (`pm6125.dtsi`, `pmi632.dtsi`, `sm6115.dtsi`, `board.dts`, `qcom_smd-regulator.c`) |
| Pinned SPMI regulator | `/tmp/opencode/linux-verify/qcom_spmi-regulator.c` (SHA-256 `09436b87…`) |
| Pinned SPMI PMIC | `/tmp/opencode/linux-verify/qcom-spmi-pmic.c` (SHA-256 `cba25036…`) |
| Register map | `pm6125-register-map.md` (derived from `drivers/regulator/qcom_spmi-regulator.c` at the pin) |

The two SPMI driver files were fetched at the pin and verified byte-for-byte
during this pass, so the table entries below are cited from the files directly,
not only through `pm6125-register-map.md`.

## Evidence table

| Fact | Evidence | Tag |
| --- | --- | --- |
| Vendor SPMI bus is `qcom,spmi@1c40000`, `qcom,ee = <0x00>`, `qcom,channel = <0x00>` | `live.dts:4305`, `:4311-4312` | [S] |
| Vendor PM6125 USID 0 node `qcom,pm6125@0`, `reg = <0x00 0x00>` | `live.dts:4320`, `:4322` | [S] |
| Vendor PM6125 USID 1 node `qcom,pm6125@1`, `reg = <0x01 0x00>` | `live.dts:4573`, `:4575` | [S] |
| USID 1 PM6125 contains only `qcom,pwms@b300` (PWM/LPG), no regulators | `live.dts:4573-4588` | [S] |
| Vendor PMI632 sits at USID 2 and USID 3 | `live.dts:4590`, `:4592`; `:4976`, `:4978` | [S] |
| Mainline `pm6125.dtsi` declares `pmic@0` at `SPMI_USID` (USID 0) and `pmic@1` at USID 1 | `pm6125.dtsi:40,42`; `:154,156` | [S] |
| Mainline `pmic@1` body is empty (no child peripherals) | `pm6125.dtsi:154-157` | [S] |
| Mainline `pmi632.dtsi` declares `pmic@2` (USID 2) and `pmic@3` (USID 3) | `pmi632.dtsi:41,43`; `:192,193` | [S] |
| Mainline SPMI bus uses `qcom,ee = <0>`, `qcom,channel = <0>` | `sm6115.dtsi:1054`, `:1064-1065` | [S] |
| No `qcom,pm6125-regulators` node exists in the board DT | `board.dts` (only `qcom,rpm-pm6125-regulators` at `:383`) | [S] |
| No regulator subnode exists under either vendor PM6125 USID | `live.dts:4320-4588` | [S] |
| Vendor wires L11/L24 through RPM `ldoa` resources, not SPMI | `live.dts:1455-1470` (id `0x0b`), `:1730-1745` (id `0x18`) | [S] |
| RPM table maps L11→`pm660_ht_lvpldo`, L24→`pm660_pldo660` | `qcom_smd-regulator.c:851-864` (`rpm_pm6125_regulators[]`) | [S] |
| L11 SPMI base `0x4a00`, L24 `0x5700` | `qcom_spmi-regulator.c:2038,2051` (verified locally) | [S] |
| SPMI address splits as `peripheral = addr >> 8`, `ppid = (sid << 8) \| (addr >> 8)` | `pm6125-register-map.md:77-98` | [S] via doc |
| Which USID carries peripheral `0x4a`/`0x57` | no pinned source selects it | [U] |
| Which SPMI ops set / voltage getter applies to L11/L24 | depends on hardware subtype read | [U] |
| PM6125 regulator variant field in the table | `pm6125_regulators[]` has no subtype field; `force_type=0` | [S] |
| PM6125/PMI632 entry in `qcom-spmi-pmic.c` | absent; only the generic `qcom,spmi-pmic` matches | [S] |

## USID analysis

The two paths must be kept separate, because they answer different questions.

**AP-side SPMI path (`qcom,pm6125-regulators`).** This is the only path that
would bind an LDO peripheral to a USID. `of_spmi_register_devices()` stores the
node's `reg[0]` as the device USID and uses it for every transaction
(`pm6125-register-map.md:122-125`). Both mainline and vendor trees declare a
PM6125 at USID 0 and USID 1, so the candidate set is exactly {0, 1}. However:

- Mainline instantiates no `qcom,pm6125-regulators` node on this board; the
  board only uses `qcom,rpm-pm6125-regulators` (`board.dts:383`). **[S]**
- The vendor USID 1 PM6125 node contains only a PWM block
  (`live.dts:4573-4588`). It carries no regulator children. **[S]**
- The vendor LDO peripherals `0x4a`/`0x57` are never named anywhere in the DT;
  the rails are addressed by RPM resource id, not SPMI address. **[S]**

Therefore the pinned evidence **bounds** the LDO USID to {0, 1} but does **not
choose** between them. No pinned source selects USID 0 or USID 1 for the L11 or
L24 LDO peripherals. **[U]**

**RPM-owned path (`qcom,rpm-pm6125-regulators`).** This path does not use a
USID at all. It submits `QCOM_SMD_RPM_LDOA` resource votes (id 11 and id 24) over
the RPM transport (`qcom_smd-regulator.c:851-864`, match at `:1412`). The RPM
firmware owns the rail; the AP never issues an SPMI transaction and gets no
register readback (`rpm-power-interface.md:4-6`, `:164-166`). So for the board's
actual configuration, the USID question is **not exercised** and cannot be
answered from the board DT. **[C]**

## L11/L24 base and getter re-check

- Bases: L11 = `0x4a00` (peripheral `0x4a`), L24 = `0x5700` (peripheral `0x57`),
  from `pm6125-register-map.md:59-62`, derived from `pm6125_regulators[]` at the
  pin. The `0x4000 + (n-1)*0x100` window rule agrees (L11 = 11th, L24 = 24th).
  **[S] via doc.**
- Enable register: `base + 0x46`, software-enable bit 7 (`0x80`), for every
  mapping (`pm6125-register-map.md:211-214`). **[S] via doc.**
- Voltage getter: **not resolvable to one getter from the pinned evidence.** The
  ops set is selected by the `(TYPE, SUBTYPE, DIG_MAJOR_REV)` triple read from
  the part. `ldo`/`smps` use `spmi_regulator_common_get_voltage()` (range at
  `0x40` + selector at `0x41`); `ftsmps426`/`hfs430`/`hfsmps` use
  `spmi_regulator_ftsmps426_get_voltage()` (two-byte millivolt at `0x40`)
  (`pm6125-register-map.md:175-205`). The LDO subtypes in `supported_regulators[]`
  span both families, so L11/L24 cannot be assigned a getter without the live
  subtype. **[U]**
- The RPM descriptors (`pm660_ht_lvpldo`, `pm660_pldo660`) are RPM-path data and
  do not tell us which SPMI ops set the same silicon would match. They must not
  be substituted for the SPMI getter. **[C]**

## Pinned SPMI driver re-check

`[S]` `pm6125_regulators[]` (`qcom_spmi-regulator.c:2019-2052`) maps L11 to
`0x4a00` and L24 to `0x5700`. `struct spmi_regulator_data` (`:437-443`) is
`{name, base, supply, ocp, force_type}`: there is **no subtype/variant field**,
and both L11/L24 leave `ocp` and `force_type` zero. The variant is therefore
chosen at run time by `spmi_regulator_match()` (`:1688-1741`), which reads
`DIG_MAJOR_REV`/`TYPE`/`SUBTYPE` (`0x01`/`0x04`/`0x05`) from the hardware and
selects the first matching `supported_regulators[]` row.

`[S]` The LDO rows in `supported_regulators[]` (`:1586-1678`) span both getter
families: `ldo` (`spmi_regulator_common_get_voltage()`, range at `0x40` +
selector at `0x41`) and `ftsmps426`/`hfs430`/`hfsmps`
(`spmi_regulator_ftsmps426_get_voltage()`, two-byte millivolt at `0x40`). The
candidate LDO subtypes are `0x03-0x0c`, `0x10` (LN), `0x28-0x2c`, `0x30-0x4f`
and `0x6a-0x7d`; the table does not select one.

`[S]` `qcom-spmi-pmic.c` has **no** `qcom,pm6125` or `qcom,pmi632` entry in
`pmic_spmi_id_table[]` (`:39-72`). Both match only the generic
`"qcom,spmi-pmic"` → `N_USIDS(1)` (`:70`), which is a USID count, not a USID
assignment or a regulator variant. The `PMIC_SUBTYPE` read at `0x105` is
compared only against PMI8998/PM660/PM8941/PM8226/PM8110 constants; there is no
PM6125 branch.

`[S]` No pinned DT instantiates `qcom,pm6125-regulators`: the compatible exists
only as a driver match (`:2474`), `pm6125.dtsi` has no `regulators` node under
either `pmic@0`/`pmic@1`, and the vendor `live.dts` has no regulator child under
`qcom,pm6125@0`/`@1` (USID 1 contains only `qcom,pwms@b300`).

**Verdict:** the USID remains unresolved (`[U]`) and the variant remains
unresolved (`[U]`). The new files narrow the candidate subtypes and confirm the
runtime-selection mechanism, but they do not choose a USID or an ops set.

## What would resolve this

1. **A pinned board or SoC DT that instantiates `qcom,pm6125-regulators` on a
   PM6125 node**, showing which USID node contains `regulators { l11 … l24 … }`.
   Source-only and safe. **[preferred]**
2. **The `pm6125_regulators[]` and `supported_regulators[]` tables read directly
   from the pinned `qcom_spmi-regulator.c`** — done in this pass: they confirm
   `0x4a00`/`0x5700` and the candidate subtypes, and show `force_type=0`. They
   still do not pick the USID by itself.
3. **A vendor `spmi-pmic-arb` / peripheral-owner dump** (the `cnfg`/`obsrvr`
   windows) showing which USID owns peripherals `0x4a` and `0x57`. This requires
   device access, which `pmic-audit.md` blocks; it must remain opt-in and is not
   proposed here.
4. **A hardware subtype read** of `0x01..0x05` at the candidate address, which
   would also settle the voltage getter. Also device access; also blocked.

Until (1) or (2) plus (3) are available, the honest statement is: **L11/L24 are
addressed by peripheral `0x4a`/`0x57`, the board reaches them through the RPM
path, and the SPMI USID (0 vs 1) and the SPMI voltage getter are unresolved from
pinned evidence.** No SPMI assignment should be written into board code or used
to justify a live read on that basis.

## Related documents

- `pm6125-register-map.md` — bases, offsets, getter families, slave-identity gap.
- `pm6125-identity-evidence.md` — full identity inventory and RPM rail table.
- `pmic-audit.md` — live-access prohibition and withdrawn USID-scan method.
- `rpm-power-interface.md` — RPM transport exposes no PM6125 readback.
