# Vendor SDHC power-on sequence (host-only)

Status: host-only static trace of pinned LineageOS vendor sources and the
captured live device tree. It adds no measurement, device action or activation
code, and closes no gate; the decision remains **NO-GO**
(`sdhc-decision-memo.md`) `[S]`.

Tags: **[S]** source/DT-established, **[C]** conditional inference (condition
stated), **[U]** unknown.

## 1. Inputs and provenance

| Input | Revision / hash | Use |
| --- | --- | --- |
| `LineageOS/android_kernel_samsung_sm6115` | `cbd2ed85b0262bbcafaa9e0cc6a979e1eaa2c1d4` (lineage-24.0 at trace time) | Vendor SDHCI, MMC core and RPM regulator sources `[S]` |
| `LineageOS/android_device_samsung_gta4l` | `ed99b5e1a435a04cc44e51aa0f7331c3a5ee82ea` | Device makefiles and configuration `[S]` |
| `LineageOS/android_device_samsung_gta4l-common` | `52bcebf80079036f90f09daa950af3ad5fb015b6` | Common device configuration `[S]` |
| `arch/arm64/boot/dts/vendor/qcom/P85946-qrd-overlay.dts` | `f03828c2682f4ef509e686ac895d35f978b233200774c4ff321ebc8f3d9d656b` | Board overlay applied to `bengal.dtb` `[S]` |
| `drivers/mmc/host/sdhci-msm.c` | `298aeafcd49392bc706ca7573b178da7bcf614294bed26ccd9c21c4f4dbef751` | Vendor regulator and power-IRQ handling `[S]` |
| `drivers/mmc/host/sdhci.c` | `846e5ea0dd264df5b95a6848245f44b42ed86bfa0d9a7b9ee1d486eda9aeca48` | Generic power/clock ordering `[S]` |
| `drivers/mmc/core/core.c`, `host.c` | pinned at the same kernel revision | `mmc_power_up` ordering and delay default `[S]` |
| `drivers/regulator/qcom_smd-regulator.c` | `b7875db7d509220ebc958c1c600fb8c568cf6f98dfd3b6786d788a9f4b67bd47` | RPM-SMD enable/voltage requests `[S]` |
| Captured `live.dts` | `81c1865589df632ae76b77edd44eb9326b317f8abef471465504618636a9b629` | Live board wiring and regulator values `[S]` |

These repositories are research inputs and are not vendored here. The trace is
pinned by commit and file hash so it can be re-fetched unchanged `[S]`.

## 2. Board wiring

`[S]` The captured live device tree wires `sdhc_1` (`sdhci@4744000`) as
follows:

| Property | Value | Decoded |
| --- | --- | --- |
| `vdd-supply` | phandle `0x1f7` | `pm6125_l24` (`rpm-regulator-ldoa24/regulator-l24`) |
| `qcom,vdd-voltage-level` | `<0x2d2a80 0x2d2a80>` | 2,960,000 µV low/high |
| `qcom,vdd-current-level` | `<0x0 0x8b290>` | 0 / 570,000 µA |
| `vdd-io-supply` | phandle `0x1f0` | `pm6125_l11` (`rpm-regulator-ldoa11/regulator-l11`) |
| `qcom,vdd-io-voltage-level` | `<0x1b7740 0x1b7740>` | 1,800,000 µV low/high |
| `qcom,vdd-io-current-level` | `<0x0 0x4f588>` | 0 / 325,000 µA |
| `qcom,vdd-io-always-on` | present | L11 is never disabled by this driver |
| `qcom,vdd-io-lpm-sup` | present | L11 is put in LPM (load 0) on bus-off |

`[S]` The L24 node declares min/max 2,704,000–3,600,000 µV with init
2,704,000 µV; L11 declares 1,704,000–1,952,000 µV with init 1,704,000 µV.
Both nodes carry `qcom,set = <0x03>`. Those are software constraints and
initial requests, not measurements `[S]/[U]`.

`[S]` The rails are shared. The live `ufshc@4804000` node declares
`vcc-supply = <0x1f7>` (L24) and `vccq2-supply = <0x1f0>` (L11), with
`vcc-max-microamp = 600,000` and `vccq2-max-microamp = 600,000`, and the board
overlay applies those supplies in its UFS fragment. The physical UFS
population is unresolved (`sdhc-evidence-gap-checklist.md`, `E1`/`E3`)
`[S]/[U]`.

`[S]` The board overlay (`P85946-qrd-overlay.dts`, `fragment@47`) applies
exactly these properties to the `sdhc_1` target, including the two
`always-on`/`lpm-sup` booleans and the active/sleep pinctrl states. The overlay
is applied over `bengal.dtb`, and its board identifier matches the captured
`P85946KA1` `[S]`.

## 3. Power-up ordering in the vendor kernel

`[S]` `mmc_power_up()` (vendor `core.c`) sets `MMC_POWER_UP`, calls
`mmc_set_ios()` with `ios.clock` still 0, waits
`ios.power_delay_ms`, then sets `ios.clock = host->f_init`, sets
`MMC_POWER_ON` and calls `mmc_set_ios()` again. The default
`power_delay_ms` is 10 ms (`host.c`), and the board overlay adds no
`post-power-on-delay-ms` override `[S]`.

`[S]` With `ios.clock == 0`, the clock branch in `sdhci_set_ios()` is
skipped, so no card clock is programmed during the power-up call `[S]`.

`[S]` For `MMC_POWER_UP`, `sdhci_set_ios()` first calls the driver's
`enable_controller_clock` hook, then `sdhci_set_power()`, which reaches
`sdhci_set_power_noreg()`. That function writes `SDHCI_POWER_CONTROL` and then
calls `check_power_status(REQ_BUS_ON)`, which the vendor driver implements as
`sdhci_msm_check_power_status()` `[S]`.

`[S]` `sdhci_msm_check_power_status()` waits on
`msm_host->pwr_irq_completion` for up to 5,000 ms
(`MSM_PWR_IRQ_TIMEOUT_MS`) and proceeds after the handler completes or the wait
times out `[S]`.

`[S]` The handler `sdhci_msm_pwr_irq()` handles `CORE_PWRCTL_BUS_ON` in this
order:

1. `sdhci_msm_setup_vreg(pdata, true, false)`;
2. `sdhci_msm_setup_pins(pdata, true)`;
3. `sdhci_msm_set_vdd_io_vol(pdata, VDD_IO_HIGH, 0)`;
4. acknowledge `CORE_PWRCTL_BUS_SUCCESS` in `CORE_PWRCTL_CTL`.

`[S]` `sdhci_msm_setup_vreg()` enables `vdd` first and `vdd_io` second, in that
array order, and stops on the first error `[S]`.

`[S]` `sdhci_msm_vreg_enable()` sets the optimum load (HPM), sets the voltage
range only while its cached `is_enabled` is false, then calls
`regulator_enable()`; it marks the regulator enabled only after success `[S]`.

`[S]` `qcom_smd-regulator.c` implements `regulator_enable()` as an RPM request:
`rpm_reg_enable()` sends `RPM_KEY_SWEN = 1` through `rpm_reg_write_active()`,
and `rpm_reg_set_voltage()` sends the voltage key the same way `[S]`. The stock
kernel therefore asks RPM to enable L24 and L11; it does not rely on a retained
rail at this point `[S]`.

`[S]` On `CORE_PWRCTL_BUS_OFF`, the handler calls
`sdhci_msm_setup_vreg(pdata, false, false)`, puts the pins in sleep state and
sets the IO level low before acknowledging `BUS_SUCCESS` `[S]`.

`[S]` `sdhci_msm_vreg_disable()` disables a regulator only when it is enabled
and **not** `is_always_on`. L24 is disabled and its minimum voltage is set to
0. L11 is `is_always_on`, so it is not disabled; because it is also
`lpm_sup`, the driver only sets its load to the LPM value (0 µA) `[S]`.

`[C]` So the stock software leaves L11 enabled across a bus-off request and
only reduces its load demand; whether the RPM firmware or PMIC actually keeps
the rail at 1.8 V under that request is not established by this trace `[C]`.

`[S]` The card clock is programmed in the second `mmc_set_ios()` call, after
the 10 ms `power_delay_ms` wait and after the BUS_ON acknowledgement. The
generic comment in `sdhci_set_ios()` states the intent: during power-up the
controller clock and regulators are turned on first, and the core delay is
meant to cover the supply reaching its minimum voltage before the card clock is
turned on `[S]`.

## 4. What the trace establishes

- `[S]` In the stock vendor stack, the bus-power request is handled with the
  card clock at zero: L24, then L11, then pads, then the IO level, then the
  BUS_ON acknowledgement, then a 10 ms wait, then the card clock.
- `[S]` The stock stack sends explicit RPM enable and voltage requests for L24
  and L11 on BUS_ON; it does not depend on a firmware-retained rail for this
  transition.
- `[S]` L11 is declared always-on and low-power-capable in DT, and the driver
  implements that by never disabling it and reducing its load to 0 µA on
  bus-off.
- `[S]` The driver performs no delay between L24 and L11, between the rails and
  the pads, or before the BUS_ON acknowledgement. The only modelled delay is
  the 10 ms core `power_delay_ms` before the card clock, and that value is a
  Linux default rather than a datasheet minimum.
- `[C]` If the RPM/PMIC honours these requests, this sequence is a software
  precedent for the candidate operation "bus power on with card clock off and
  no card command". It is not a measurement and does not establish that the
  rails reach or hold their requested values.

## 5. What it does not establish

- `[U]` Actual L24/L11 voltage, ramp, inrush, overshoot, settling or
  brownout behaviour at any point in the sequence.
- `[U]` The electrical order required by the PM6125, the eMMC or the board;
  the 10 ms default is not a sourced minimum.
- `[U]` Back-powering, unpowered-drive limits, pad-domain behaviour or leakage
  during the BUS_ON window.
- `[U]` PM6125 fault semantics and the acknowledge/pending-request decision
  (`E8`), recovery and safe-stop (`E9`), and current/loading (`E3`, `E4`).
- `[U]` Whether the RPM firmware applies additional soft-start or sequencing
  that the AP cannot observe.

## 6. Relationship to the pinned port

`[S]` The pinned mainline port used for the XNU work is a different software
path: `sdhc_1` has no `vmmc-supply` or `vqmmc-supply`, the AP sends no L11/L24
RPM vote, and the mainline `sdhci-msm` power-IRQ path returns success without a
regulator action (`linux-regulator-lifecycle.md`). The stock vendor trace above
therefore describes the vendor design, not the port's current behaviour `[S]`.

`[C]` Adopting the vendor order in a future candidate would mean adding an
explicit RPM enable/voltage request for L24 and L11 before any clock or card
command. That is a design change, not an existing capability, and it still
requires the electrical contract, measurement and independent review `[C]`.

No gate is closed, no measurement was made, and no device state was changed by
this trace `[S]`.
