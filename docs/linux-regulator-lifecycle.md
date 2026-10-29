# Linux regulator lifecycle for the gta4l eMMC rails

Status: source investigation only. No XNU code, build, boot or hardware access was
added. This records what the pinned Linux board does to the PM6125 L11/L24 rails
and to the SDHCI power path, so a later design can reuse a known sequence instead
of inventing one.

Every substantive statement below is tagged:

| Tag | Meaning |
| --- | --- |
| `[S]` | Established by the pinned source quoted here |
| `[C]` | True only under the stated configuration or runtime condition |
| `[U]` | Not established by this investigation |

## Source pin

- Repository: `https://github.com/limonabisi/linux-gta4l`
- Branch: `gta4l-7.2`
- Commit: `cf5f763f0ef8a5b4d8bb4163833cba1160c51894`

All `file:line` references below are from this commit.

| File | Role |
| --- | --- |
| `drivers/regulator/qcom_smd-regulator.c` | RPM regulator driver and PM6125 table |
| `drivers/regulator/core.c` | registration, supply resolution, constraints, late cleanup, suspend |
| `drivers/regulator/of_regulator.c` | DT constraint parsing |
| `drivers/mmc/core/regulator.c` | optional supply lookup and helpers |
| `drivers/mmc/core/core.c` | power up/off, signal voltage dispatch |
| `drivers/mmc/host/sdhci.c` | generic host setup, power, signal switch, remove |
| `drivers/mmc/host/sdhci-msm.c` | power IRQ, pads, runtime PM, vendor registers |
| `drivers/pinctrl/core.c` | `pinctrl_pm_select_*` behaviour |
| `drivers/opp/core.c` | OPP to performance-state and bandwidth |
| `drivers/pmdomain/core.c`, `drivers/pmdomain/qcom/rpmpd.c` | VDDCX votes |
| `drivers/interconnect/core.c`, `drivers/interconnect/qcom/icc-rpm.c` | bandwidth votes |
| `drivers/soc/qcom/smd-rpm.c` | RPM packet format |
| board and SoC DTS | wiring |

## Two descriptions of the same rails

`[S]` The tree describes L11 and L24 through two unrelated mechanisms:

| Rail | RPM path (instantiated by this board) | SPMI path (not instantiated) |
| --- | --- | --- |
| L11 | `QCOM_SMD_RPM_LDOA`, id 11, `pm660_ht_lvpldo` | peripheral `0x4a`, enable `0x4a46` |
| L24 | `QCOM_SMD_RPM_LDOA`, id 24, `pm660_pldo660` | peripheral `0x57`, enable `0x5746` |

The board instantiates `qcom,rpm-pm6125-regulators`, so the RPM path is the one
that runs. `pm6125-register-map.md` describes the hardware the RPM firmware
programs; it is not a path Linux takes.

## The final device tree

`[S]` The board includes `sm6115.dtsi`, `pm6125.dtsi` and `pmi632.dtsi`. Of these,
only the board file contains a regulators node: `regulators-0` under
`&rpm_requests`, compatible `qcom,rpm-pm6125-regulators`. `pm6125.dtsi` and
`pmi632.dtsi` define no regulator subsystem nodes.

`[S]` `&sdhc_1` carries only `non-removable` and `status = "okay"`. The
`vmmc-supply`/`vqmmc-supply` pair in the board file belongs to `&sdhc_2` (L22 and
L5), the removable SD card, not the eMMC controller.

`[S]` L11/L24 are declared with a minimum, a maximum and `regulator-allow-set-load`
only. Neither has `regulator-boot-on`, `regulator-always-on`, or a label reference
from any consumer node. The phandle labels `pm6125_l11a` and `pm6125_l24a` are
defined and never used.

`[C]` "No consumer in this DTS" is not the same as "no possible consumer".
Consumers can also be created outside DT through regulator lookup mappings, and a
future overlay could add one. What is established is that this final tree contains
none.

### Parent supply resolution

`[S]` The RPM regulator descriptors set `supply_name` to `vdd_l9_l11` (L11) and
`vdd_l23_l24` (L24). No regulator with either name exists in the final tree, and
`regulator_resolve_supply()` reaches the error path.

`[S]` With a populated DT, `have_full_constraints()` is true, so that path may
substitute `dummy_regulator_rdev` instead of failing or deferring. Registration
does not depend on that substitution: `set_machine_constraints()` runs before
ordinary supply resolution, and the later resolve error is nonfatal
(`regulator-core.c:6162-6185`, `6215-6243`).

`[C]` The substitution affects the early-resolve outcome, not whether the
regulator can register. A build without full constraints can still register with
pending constraints; that path was not exercised here.

## Lifecycle

```text
DT parse ──► constraints: apply_uV, CHANGE_STATUS, CHANGE_DRMS, no boot_on/always_on
                     │
registration ──► set_machine_constraints
                     │   apply_uV ──► set_voltage → cache only
                     │   boot_on/always_on false → no enable
                     │
                     ├──► supply resolve (nonfatal)
                     │
        30 s ──► regulator_late_cleanup ──► cached is_enabled == 0 → skip
                     │
MMC probe ──► supplies are -ENODEV ──► no vqmmc enable; 0xa9c write clears pad bits
                     │
runtime ──► pwr_irq ──► clear status; set_vmmc/vqmmc no-op; pinctrl no-op; ack
                     │
runtime PM ──► OPP ──► genpd performance state ──► rpmpd ──► RPM CX votes
                     └─► icc_set_bw ──► icc-rpm ──► RPM bandwidth votes
```

### Phase 0 — DT constraints

`[S]` `of_get_regulation_constraints()` sets, for both rails:

- `min_uV`/`max_uV`, and because both are present, `apply_uV = true`;
- `REGULATOR_CHANGE_VOLTAGE` (min ≠ max);
- `REGULATOR_CHANGE_STATUS` (`always_on` false);
- `REGULATOR_CHANGE_DRMS` (`regulator-allow-set-load`);
- `boot_on = false`, `always_on = false`.

### Phase 1 — driver probe

`[S]` `rpm_reg_probe()` calls `rpm_regulator_init_vreg()` per child, which calls
`devm_regulator_register()`. The probe itself issues no RPM request.

`[C]` "No request at probe" is specific to this driver and these rails.
Registration in general can resolve supplies, apply constraints and, for
`boot_on`/`always_on` rails, enable hardware.

### Phase 2 — registration and constraints

`[S]` `set_machine_constraints()` runs `machine_constraints_voltage()`. Because
`apply_uV` is true, it reads the current voltage via `rpm_reg_get_voltage()`, which
returns the cached `uV` (initially 0), then calls `_regulator_do_set_voltage()` for
the DT minimum.

`[S]` That reaches `rpm_reg_set_voltage()`, which updates the cache and calls
`rpm_reg_write_active()`. That function includes `uv` only when
`vreg->is_enabled` is set, and `swen` only when `enabled_updated` is set. Neither
is true, so the request has zero keys and no RPM packet is sent.

`[S]` `set_machine_constraints()` enables only when
`constraints->always_on || constraints->boot_on`; both are false, so no enable
occurs.

`[C]` The set-voltage call is made; the packet is suppressed by the cached enable
state. The suppression is a property of this driver, not of the regulator core.

### Phase 3 — late cleanup

`[S]` `regulator_init_complete()` is `late_initcall_sync`. With DT it sets
`has_full_constraints = true` and queues the work after 30 s.

`[S]` `regulator_late_cleanup()` skips when `always_on` is set, when the driver
cannot change status, or when `rdev->use_count` is non-zero. Otherwise it calls
`_regulator_is_enabled()`, which is `rpm_reg_is_enabled()` returning the cached
`is_enabled` (0), and returns without disabling.

`[C]` This is a software-cache outcome. It is consistent with the board working
without a supply consumer; it is not a measurement of the hardware.

`[U]` Whether the RPM firmware would tolerate an AP-initiated disable is not
established, and this code path never attempts one.

### Phase 4 — MMC probe and supply lookup

`[S]` `mmc_regulator_get_supply()` uses `devm_regulator_get_optional()` for
`vmmc`, `vqmmc` and `vqmmc2` and returns 0 for `-ENODEV`; the error pointers stay
in `mmc->supply`.

`[S]` `sdhci_msm_probe()` calls `sdhci_msm_register_vreg()` before the add-host
step; `sdhci_add_host()` and `sdhci_msm_cqe_add_host()` both call
`sdhci_setup_host()`, which fetches supplies only when `mmc->supply.vqmmc` is
still NULL and enables `vqmmc` only inside
`if (!IS_ERR(mmc->supply.vqmmc))`. By then the supply is already an error
pointer, so neither the lookup nor the enable runs there.

`[S]` `host->sdhci_core_to_disable_vqmmc` is set only when that enable succeeded,
so the matching `regulator_disable()` calls in `sdhci_setup_host()` error paths,
`sdhci_cleanup_host()` and `sdhci_remove_host()` are skipped.

`[S]` `sdhci_msm_set_regulator_caps()` derives capabilities only when `vqmmc`
exists. With an error pointer `caps` stays 0 and that function's `core_vendor_spec`
pad-switch update is skipped.

`[S]` That skip does not leave the pad bits as firmware set them. Earlier in the
same probe, `sdhci_msm_probe()` writes the full value `CORE_VENDOR_SPEC_POR_VAL`
(`0xa9c`) to `core_vendor_spec` (`drivers/mmc/host/sdhci-msm.c:2779`). In `0xa9c`
both `CORE_IO_PAD_PWR_SWITCH_EN`
(bit 15, `0x8000`) and `CORE_IO_PAD_PWR_SWITCH` (bit 16, `0x10000`) are clear, so
the write drives both bits to 0. The later read-modify-write paths preserve
whatever those bits hold. With `caps == 0` nothing re-enables them, so Linux
leaves both bits cleared, not inherited (`sdhci_msm_set_regulator_caps()`,
`drivers/mmc/host/sdhci-msm.c:2384-2424`).

### Phase 5 — runtime power requests

`[S]` `sdhci_msm_handle_pwr_irq()` reads `CORE_PWRCTL_STATUS`, writes the status
bits to `CORE_PWRCTL_CLEAR` with up to 10 retries, then on `BUS_ON`/`BUS_OFF` calls
`sdhci_msm_set_vmmc()`, `sdhci_msm_set_vqmmc()` and `sdhci_msm_set_pincfg()`,
acknowledging `CORE_PWRCTL_BUS_SUCCESS` or `_FAIL` through `CORE_PWRCTL_CTL`.

`[S]` `sdhci_msm_set_vmmc()` and `sdhci_msm_set_vqmmc()` return 0 immediately when
their supply is an error pointer, so the acknowledgement can follow with no
regulator action.

`[S]` `sdhci_msm_set_pincfg()` calls `pinctrl_pm_select_default_state()` or
`_sleep_state()`. Those return 0 when the device has no pinctrl states
(`!dev->pins`) or when the named state is absent (`IS_ERR(state)`), without
applying a pin configuration. `sdhc_1` declares no pinctrl states.

`[C]` So the acknowledgement can follow a pinctrl call that changes no pins. The
handler reports success; it does not prove that pads were reprogrammed.

`[S]` The `IO_SUCCESS` bits are also acknowledged for IO level changes, and
`mmc_regulator_set_vqmmc()` is called only when a real `vqmmc` exists and no power
state change is in flight.

### Phase 6 — signal voltage switching

`[S]` `sdhci-msm` overrides `mmc_host_ops.start_signal_voltage_switch` with
`sdhci_msm_start_signal_voltage_switch()`, which writes only
`SDHCI_HOST_CONTROL2`. It does not call the regulator API.

`[C]` The generic `sdhci_do_voltage_switch()` does call `mmc_regulator_set_vqmmc()`
inside `if (!IS_ERR(mmc->supply.vqmmc))`, but this driver does not use it.

### Phase 7 — runtime PM, OPP and VDDCX

`[S]` `sdhci_msm_runtime_suspend()` calls `dev_pm_opp_set_rate(dev, 0)`,
disables the bulk clocks (`clk_bulk_disable_unprepare()`) and suspends ICE;
`runtime_resume()` re-enables the bulk clocks, restores DLL state and the OPP
rate. Both OPP calls ignore their return values. Neither hook touches `bus_clk`,
which is only disabled on remove/error paths.

`[S]` Inside the OPP core, `_set_opp()` calls `_set_required_opps()` and
`_set_opp_level()`, which reach `dev_pm_domain_set_performance_state()`, and
`_set_opp_bw()`, which reaches `icc_set_bw()`.

`[S]` The `rpmpd` domain implements `set_performance_state` and `power_on/off` by
sending RPM requests (`qcom_rpm_smd_write`) with the CX level key. The SM6115 map
binds `SM6115_VDDCX` to `cx_rwcx0_lvl`.

`[S]` The `sm6115` interconnect providers are `icc-rpm`, whose `qcom_icc_rpm_set()`
sends RPM master/slave bandwidth requests, skipping nodes marked `ap_owned`.

`[S]` `sdhci-msm` never calls `dev_pm_opp_set_regulators()` (grep count 0 in
`drivers/mmc/host/sdhci-msm.c`), and `sdhc_1`'s OPP table in `sm6115.dtsi`
(`1131-1154`) carries no `opp-microvolt`. `dev_pm_opp_set_regulators()` would
assign `opp_table->config_regulators = _opp_config_regulator_single` directly
for a single regulator (`drivers/opp/core.c:2273-2275`), but this driver never
takes that path and the table declares no supply.

`[C]` For `sdhc_1`, no regulator is attached to this device's OPP table by this
driver, so OPP cannot set a regulator voltage for it. That is a statement about
`sdhc_1` only, not about every OPP table in the system: another driver could
attach a regulator to its own table, and a power domain's performance state
could in principle have regulator side effects. Here the required OPP is
`rpmpd`, whose `set_performance_state` sends RPM traffic rather than calling the
regulator API.

`[S]` Linux therefore does send RPM traffic for VDDCX levels and bandwidth. Those
are different RPM resources from LDOA 11/24.

### Phase 8 — power off, remove, shutdown

`[S]` `sdhci-msm` uses `.set_power = sdhci_set_power_noreg`, which writes only
`SDHCI_POWER_CONTROL`. The `sdhci_set_power_reg()` path that calls
`mmc_regulator_set_ocr()` is not used by this driver.

`[S]` `mmc_power_off()` sets the I/O state and calls `mmc_set_ios()`; it does not
call the regulator API.

`[S]` `sdhci_msm_remove()` calls `sdhci_remove_host()`, runtime PM and clock
teardown (`drivers/mmc/host/sdhci-msm.c:2912-2930`); the only regulator call in
that path is the `sdhci_core_to_disable_vqmmc` disable, which is skipped here.
This is driver removal, not system shutdown: the driver registers no
`.shutdown` callback.

`[S]` System sleep is wired to runtime PM:
`SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)`
(`drivers/mmc/host/sdhci-msm.c:2986-2987`). `sdhci_msm_runtime_suspend()`
(`drivers/mmc/host/sdhci-msm.c:2932-2949`) calls `dev_pm_opp_set_rate(dev, 0)`
without checking its return value, then disables the bulk clocks and suspends
ICE. It does not disable `bus_clk` (remove/error paths do) and does not program a
regulator suspend state. A failed rate call is not detected, so "the vote is
dropped" is an attempt, not an established outcome.

`[S]` The RPM SMPS/LDO ops `rpm_smps_ldo_ops`
(`drivers/regulator/qcom_smd-regulator.c:162-172`) have no `.suspend`,
`.resume`, `.set_suspend_voltage`, `.set_suspend_mode` or `.set_suspend_enable`
callbacks, and no `regulator-state-mem` node exists in the board file,
`sm6115.dtsi`, `pm6125.dtsi` or `pmi632.dtsi`.

`[S]` `regulator_get_suspend_state_check()`
(`drivers/regulator/core.c:583-605`) returns NULL when `rstate->enabled` is
neither `ENABLE_IN_SUSPEND` nor `DISABLE_IN_SUSPEND`; `regulator_suspend()`
(`drivers/regulator/core.c:6330-6346`) then returns 0. The regulator class
therefore has nothing to apply to L11/L24.

`[C]` That covers the regulator framework only. It is not proof that system-wide
suspend or shutdown leaves these rails unchanged, because the RPM firmware and
other subsystems are outside this path. A missing local callback or suspend
state does not establish whole-system rail behaviour.

`[U]` What the RPM firmware itself does to L11/L24 across AP suspend, shutdown and
reboot is not covered by these paths and was not traced.

## State table

| Item | Value | Tag |
| --- | --- | --- |
| Hardware enable of L11/L24 | not measured; whatever firmware left | `[U]` |
| Cached `is_enabled` | stays 0 | `[S]` |
| Cached `uV` | DT minimum after registration | `[S]` |
| RPM packet for L11/L24 | none in the traced paths | `[C]` |
| VDDCX level/enable packets | sent by `rpmpd` | `[S]` |
| Bandwidth packets | sent by `icc-rpm` | `[S]` |
| `core_vendor_spec` pad-switch bits | cleared by the probe's `0xa9c` write; not re-enabled because `caps == 0` | `[S]` |
| Other `core_vendor_spec` bits | reset and reprogrammed by the driver | `[S]` |

## What Linux changes, inherits, and leaves unknown

| Category | Items |
| --- | --- |
| Changes | SDHCI interrupt/clock/reset registers; `CORE_PWRCTL_CLEAR`/`CTL`; `core_vendor_spec` (including clearing the pad-switch bits) and vendor DLL/mode fields; GCC SDCC clocks; RPM votes for VDDCX and bandwidth |
| Inherits | The L11/L24 hardware enable state and voltage |
| Does not change | L11/L24 through the regulator API in the traced paths; no `swen`, `uv` or `ma` for either rail |
| Unknown | actual rail voltage and enable state; RPM firmware behaviour across suspend/shutdown; behaviour under configurations this board does not use |

`[S]` The probe writes `CORE_VENDOR_SPEC_POR_VAL` (`0xa9c`) to `core_vendor_spec`
and later rewrites that register in `_init_cm_dll()`, `_hc_select_default()`,
`_hc_select_hs400()`, `_msm_execute_tuning()` and `_msm_handle_pwr_irq()`.
An earlier revision said vendor configuration is left as firmware set it. That
was wrong for the register as a whole. It was also wrong for the pad bits: the
`0xa9c` write clears `CORE_IO_PAD_PWR_SWITCH_EN`/`CORE_IO_PAD_PWR_SWITCH`, and
only their later *re-enable* is confined to `_msm_set_regulator_caps()` and
`_msm_handle_pwr_irq()`, both gated on derived capabilities. With `caps == 0`
the bits stay cleared.

## Why this matters for a port

`[S]` Two conditions keep this board from changing L11/L24 through the regulator
framework:

1. `always_on` and `boot_on` are false, so registration does not enable them.
2. The driver's `is_enabled` is a cache that stays 0, so late cleanup does not
   disable them.

`[C]` Both conditions are properties of this DT and this driver. A read on its own
would not change either: `_regulator_is_enabled()` is only consulted by cleanup
and by a few other decisions. Replacing the cached implementation with a hardware
read would, however, change what cleanup decides, because cleanup acts on the
value that function returns.

`[U]` The retained-rail contract therefore rests on firmware behaviour that the AP
does not verify. A port that copies this design inherits that property: it cannot
report the rail state, and cannot detect a rail that firmware left off.

## Open questions

- What does the RPM firmware do to L11/L24 across AP suspend, shutdown and reboot?
- Is the handoff state guaranteed by firmware configuration, or incidental?
- Which other consumers share the `vdd_l9_l11` and `vdd_l23_l24` input supplies?
- Are the pad-switch bits at handoff consistent with the 1.8 V IO domain the rail
  ranges imply?
- What does the board-level electrical contract require beyond these software
  observations?
- Does any configuration this board does not use (for example a supply consumer, a
  suspend state, or a `boot-on` flag) change the conclusions?

## Audit note

This revision revises seven statements from the first version: the scope of "no
RPM write at probe", the pad/DLL inheritance statement, the pinctrl behaviour,
the signal-voltage path, the suspend/remove coverage, the "never reads hardware"
explanation, and the pad-switch claim that survived the first correction. The
`0xa9c` probe write clears the pad-switch bits, so they are not inherited. The
revision also adds the parent-supply resolution and qualifies the no-consumer
claim. It additionally corrects the OPP regulator-attachment explanation for
`sdhc_1`: no regulator is attached through this driver, so OPP cannot set its
voltage. Remaining unverified statements are tagged `[U]` rather than asserted.
