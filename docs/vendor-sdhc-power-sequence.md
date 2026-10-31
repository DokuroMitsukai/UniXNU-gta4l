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
| Samsung `T505NDXS7CXG1` source | ZIP `84e8f9934987834da6867d37243bb18fe6fab623019e3d7a4e10fa603f069f8e`; `Kernel.tar.gz` `4e9ba469592a135e9847c9e8cebe51000286f7f0e2c2c11dac0e6832268ff402`; kernel 4.19.157 | Official vendor source for the CXG1 build `[S]` |
| Samsung `T505NDXS5CWI2` source | ZIP `b00cbe4827b62af8ea888b89b000e31368b644e5ec0c07ba6b65a164718081fb`; `Kernel.tar.gz` `49d3bfbca5bf35f8412f9316946fc07e8444e6d976cfe9718503add33e5dc203`; kernel 4.19.157 | Comparison release; relevant files byte-identical `[S]` |
| `LineageOS/android_device_samsung_gta4l` | `ed99b5e1a435a04cc44e51aa0f7331c3a5ee82ea` | Device makefiles and configuration `[S]` |
| `LineageOS/android_device_samsung_gta4l-common` | `52bcebf80079036f90f09daa950af3ad5fb015b6` | Common device configuration `[S]` |
| `arch/arm64/boot/dts/vendor/qcom/P85946-qrd-overlay.dts` | `f03828c2682f4ef509e686ac895d35f978b233200774c4ff321ebc8f3d9d656b` | Board overlay declaring the `sdhc_1` and UFS supplies; applicability is name/ID-level only `[S]/[U]` |
| `drivers/mmc/host/sdhci-msm.c` | `298aeafcd49392bc706ca7573b178da7bcf614294bed26ccd9c21c4f4dbef751` | Vendor regulator, power-IRQ and controller-clock handling `[S]` |
| `drivers/mmc/host/sdhci.c` | `846e5ea0dd264df5b95a6848245f44b42ed86bfa0d9a7b9ee1d486eda9aeca48` | Generic power/clock ordering `[S]` |
| `drivers/mmc/core/core.c` | `d5af6572f5142142dedeeedeaea88b38d1d631992c52e1804fa58bca90c1e340` | `mmc_power_up` ordering `[S]` |
| `drivers/mmc/core/host.c` | `1d0191e6ee642287d94b2aa4ff09f33a8ac589892050fc580b4005e969c305ea` | Default `power_delay_ms` `[S]` |
| `drivers/regulator/rpm-smd-regulator.c` | `61f32e800bb676d291542f772f969afac44369b2c98858e1684fd0a899e7fe8a` | RPM-SMD regulator requests, aggregation and active/sleep sets `[S]` |
| `drivers/regulator/core.c` | `3fefda7fa90dbb856068ee552b43cdf9f9cf5ecca776d65509631dea896cff44` | Framework enable-time and delay handling `[S]` |
| Captured `live.dts` | `81c1865589df632ae76b77edd44eb9326b317f8abef471465504618636a9b629` | Live board wiring and regulator values `[S]` |

These repositories are research inputs and are not vendored here. The trace is
pinned by commit and file hash so it can be re-fetched unchanged `[S]`. Pinning
establishes reproducibility of the research inputs, not that these sources are
the binaries shipped on the captured device `[S]/[U]`.

`[S]` The official CXG1 and CWI2 kernel archives contain Linux 4.19.157 and
`arch/arm64/configs/gta4l_mea_jt_defconfig`. The files below are byte-identical
in both releases:

| File | SHA-256 (CXG1 = CWI2) |
| --- | --- |
| `drivers/mmc/host/sdhci-msm.c` | `347530fd6d700f2fa56825c11e574761ed7aaaaad14788d815108b4219447b5f` |
| `drivers/mmc/host/sdhci.c` | `6efb32596145fa2dfe15c7ad3a10b58076bb354f6da10658422b16f415af3308` |
| `drivers/mmc/core/core.c` | `023c76f941b09673663a3275c5a43721ef9d7b970abdf3903f7b2f071b4649ca` |
| `drivers/mmc/core/host.c` | `4df339ce21382be5b3608b0fa423946481764c9f7fa9c359e37ea72ece2d8faa` |
| `drivers/regulator/rpm-smd-regulator.c` | `f934ab1df958905c11962b2c1eae2226ba9015f77853ba62ea3c1835710fd970` |
| `drivers/regulator/core.c` | `7ccd1a482052da3123ed854caa2f1b44835d5867eb9d03f80beed2aadb1cd7a4` |

The two source ZIPs use the same displayed filename but differ by SHA-256;
keep them keyed by build ID and archive hash. The companion `Platform.tar.gz`
contains Android platform modules, not kernel DTS. The Samsung listing also
offers the older `T505NDXU4CWD1` source, which was not inspected.

`[S]` The official source confirms the normal power-up ordering, the 5,000 ms
wait, the BUS_FAIL acknowledgement, the register-restore gate and the RPM-SMD
aggregation described below. `rpm-smd-regulator.c` differs from the pinned
LineageOS file only by one `EXPORT_SYMBOL` line. The SDHCI power-up block and
`mmc_power_up()` with its 10 ms default are equivalent. The official source
omits the LineageOS card-detect early-fail and no-card cleanup blocks; those
remain specific to the pinned LineageOS revision in this trace. The archive
contains no vendor board DTS.

`[S]/[U]` The exact CXB1 source is unavailable, so correspondence between these
source releases and the shipped CXB1 binary remains `[U]`.

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
Both nodes carry `qcom,set = <0x03>`, which selects both the active set (bit 0)
and the sleep set (bit 1) in `rpm-smd-regulator.c`. Those are software
constraints and initial requests, not measurements `[S]/[U]`.

`[S]` The rails are shared. The live `ufshc@4804000` node declares
`vcc-supply = <0x1f7>` (L24) and `vccq2-supply = <0x1f0>` (L11), with
`vcc-max-microamp = 600,000` and `vccq2-max-microamp = 600,000`, and the board
overlay applies those supplies in its UFS fragment. The physical UFS
population is unresolved (`sdhc-evidence-gap-checklist.md`, `E1`/`E3`)
`[S]/[U]`.

`[S]` The board overlay (`P85946-qrd-overlay.dts`, `fragment@47`) declares the
same properties for the `sdhc_1` target, including the two
`always-on`/`lpm-sup` booleans and the active/sleep pinctrl states. Its model and
`qcom,board-id` match the captured live DT, and the captured boot arguments
report a `P85946KA1` board identifier whose `P85946` prefix matches the overlay
filename; that is an identifier correspondence, not a PCB revision check `[S]`.

`[S]/[U]` That is a source-hash and name/ID correspondence. It does not prove
that this overlay was the exact fragment compiled into the shipped kernel, that
the pinned LineageOS source matches the shipped kernel binary, or that the
overlay applies to the exact PCB population. Those remain `[U]`.

`[S]` The captured L24/L11 regulator nodes declare no `qcom,enable-time` and no
`regulator-enable-ramp-delay`, and the captured `sdhc_1` node declares no
`post-power-on-delay-ms`; the overlay adds none of them either. Their effect on
the modelled delays is traced in §3 `[S]`.

## 3. Power-up ordering in the vendor kernel

`[S]` `mmc_power_up()` (vendor `core.c`) sets `MMC_POWER_UP`, calls
`mmc_set_ios()` with `ios.clock` still 0, waits
`ios.power_delay_ms`, then sets `ios.clock = host->f_init`, sets
`MMC_POWER_ON` and calls `mmc_set_ios()` again. The default
`power_delay_ms` is 10 ms (`host.c`), and the board overlay adds no
`post-power-on-delay-ms` override `[S]`.

`[S]` With `ios.clock == 0`, the clock branch in `sdhci_set_ios()` is
skipped, so no card clock is programmed during the first power-up call `[S]`.

`[S]` For `MMC_POWER_UP`, `sdhci_set_ios()` calls the driver's
`enable_controller_clock` hook; if that succeeds, it calls `sdhci_set_power()`,
which reaches `sdhci_set_power_noreg()`. The vendor driver sets
`SDHCI_QUIRK_SINGLE_POWER_WRITE`, so that function writes
`SDHCI_POWER_CONTROL` once and then calls `check_power_status(REQ_BUS_ON)`,
which the vendor driver implements as `sdhci_msm_check_power_status()` `[S]`.

`[S]` `sdhci_msm_check_power_status()` waits on
`msm_host->pwr_irq_completion` for up to 5,000 ms
(`MSM_PWR_IRQ_TIMEOUT_MS`). If the requested state is already recorded, it
re-initialises the completion and does not wait. If the wait times out, it
warns, dumps the power-control registers and continues. It does not abort the
power-up sequence. In the pinned LineageOS revision only, after the wait, if
card detect reports no card while `REQ_BUS_ON` was requested, it writes
`SDHCI_POWER_CONTROL = 0` and requests `REQ_BUS_OFF`; the official CXG1/CWI2
source omits that block `[S]/[U]`.

`[S]` The handler `sdhci_msm_pwr_irq()` handles `CORE_PWRCTL_BUS_ON` in this
order:

1. `sdhci_msm_setup_vreg(pdata, true, false)`;
2. `sdhci_msm_setup_pins(pdata, true)`;
3. `sdhci_msm_set_vdd_io_vol(pdata, VDD_IO_HIGH, 0)`;
4. write `CORE_PWRCTL_BUS_SUCCESS` in `CORE_PWRCTL_CTL`, or
   `CORE_PWRCTL_BUS_FAIL` if any of steps 1–3 failed.

`[S]` The handler completes `pwr_irq_completion` after either acknowledgement,
and `sdhci_msm_check_power_status()` does not inspect whether the
acknowledgement was success or failure. A failed BUS_ON acknowledgement
therefore unblocks the caller just as a success does `[S]`.

`[S]` `sdhci_msm_setup_vreg()` enables `vdd` first and `vdd_io` second, in that
array order, and stops on the first error `[S]`.

`[S]` `sdhci_msm_vreg_enable()` sets the optimum load (HPM), sets the voltage
range only while its cached `is_enabled` is false, then calls
`regulator_enable()`; it marks the regulator enabled only after success `[S]`.

`[S]` The captured nodes bind to `qcom,rpm-smd-regulator` in
`rpm-smd-regulator.c`, not to `qcom_smd-regulator.c`.
`rpm_vreg_enable()` sets the RPM `swen` parameter to 1 and calls
`rpm_vreg_aggregate_requests()`. That function combines every consumer request
for the same RPM resource. For the parameters used here, enable, voltage and
the LDO/SMPS mode take the maximum and current is summed; other parameters have
their own rules (for example pin-control bits are ORed and frequency takes the
minimum). It sends only modified parameters to the RPM active set. It sends a
sleep-set request only when the sleep values differ from the active values or a
sleep request was already sent; otherwise the active-set values also apply
during RPM-assisted power collapse `[S]`.

`[S]` With `qcom,set = <0x03>`, both sets are configured, but the captured node
declares one set of min/max/init values and no separate sleep values, so the
driver's active and sleep requests start identical. Only the active-set request
is sent unless a sleep request was sent earlier. When the regulator is not
already enabled, the voltage set is staged and included in the same aggregated
request as the enable; if another consumer has already enabled it, the voltage
update is sent immediately `[S]`.

`[S]` `rpm_vreg_send_request()` can wait for an RPM acknowledgement
(`msm_rpm_wait_for_ack()`) when the request requires one; an enable request
generally does. This is an RPM transport/aggregation acknowledgement, not the
SDHCI power-IRQ acknowledgement. The AP cannot observe the effective
aggregated rail state: `rpm_vreg_is_enabled()` returns the driver's cached
consumer request, not the RPM aggregate `[S]`.

`[S]` On `CORE_PWRCTL_BUS_OFF`, the handler calls
`sdhci_msm_setup_vreg(pdata, false, false)`, puts the pins in sleep state and
sets the IO level low before acknowledging `BUS_SUCCESS` (or `BUS_FAIL` on
error) `[S]`.

`[S]` `sdhci_msm_vreg_disable()` disables a regulator only when it is enabled
and **not** `is_always_on`. L24 is disabled and its minimum voltage is set to
0. L11 is `is_always_on`, so it is not disabled; because it is also
`lpm_sup`, the driver only sets its load to the LPM value (0 µA) `[S]`.

`[C]` So the stock software leaves L11 enabled across a bus-off request and
only reduces its load demand; whether the RPM aggregate or PMIC actually keeps
the rail at 1.8 V under that request is not established by this trace `[C]`.

`[S]` In the normal cold-start path, the card clock is programmed in the second
`mmc_set_ios()` call, after the first 10 ms `power_delay_ms` wait and after
`sdhci_msm_check_power_status()` returns. That return can follow a BUS_SUCCESS
acknowledgement, a BUS_FAIL acknowledgement, a 5,000 ms timeout, or an
already-recorded state; the generic comment in `sdhci_set_ios()` states the
intent that the controller clock and regulators are turned on first and the
core delay covers the supply reaching its minimum voltage before the card
clock `[S]`.

`[S]` Two other paths weaken a universal "clock only after acknowledgement"
statement. First, the timeout and failure paths above proceed without a
successful acknowledgement. Second, on a restore path, when
`qcom,restore-after-cx-collapse` is declared or `reg_store` is set during a
hardware reset, `sdhci_msm_enable_controller_clock()` calls
`sdhci_msm_registers_restore()` after enabling the controller clocks; that
function writes the saved `SDHCI_CLOCK_CONTROL` value, which can contain
card-clock enable bits. The captured `sdhc_1` node does not declare
`qcom,restore-after-cx-collapse`, so this path is not established for the
captured board's normal cold start, but it exists in the helper `[S]/[U]`. The
cold-start path and the restore path must therefore be described separately
`[S]`.

`[S]` The SDHCI helper inserts no explicit delay between L24 and L11, between
the rails and the pads, or before the BUS_ON acknowledgement. The regulator
core can still delay: `_regulator_do_enable()` queries `enable_time` from the
constraints (`regulator-enable-ramp-delay`), the descriptor, or the driver
callback, then calls `_regulator_enable_delay()`. `rpm_vreg_enable_time()`
returns `qcom,enable-time`, which the captured L24/L11 nodes do not declare, so
the captured DT contributes a zero modelled framework delay. An RPM request can
also wait for its acknowledgement, whose latency is not measured `[S]/[U]`.

## 4. What the trace establishes

- `[S]` In the normal cold-start path, the bus-power request is handled with
  the card clock at zero: controller clock, BUS_ON write, then L24, then L11,
  then pads, then the IO level, then a BUS_SUCCESS/BUS_FAIL acknowledgement,
  then the first 10 ms wait, then the card clock. A timeout or failure
  acknowledgement does not stop the sequence, and a supported, valid restore
  path can write clock control without a fresh BUS_ON request.
- `[S]` The stock stack sends explicit RPM enable and voltage votes for L24 and
  L11 on BUS_ON. The RPM aggregates those votes with every other consumer and
  master; the AP cannot read the effective aggregate, so this is not proof
  that either rail is on or retained.
- `[S]` L11 is declared always-on and low-power-capable in DT, and the driver
  implements that by never disabling it and reducing its load to 0 µA on
  bus-off.
- `[S]` The SDHCI helper performs no explicit delay between the rails, the pads
  or the acknowledgement. The captured DT declares no regulator `enable-time`
  or ramp delay, but RPM acknowledgement latency and any firmware sequencing
  remain unmeasured `[U]`.
- `[C]` If the RPM/PMIC honours these votes and the acknowledgement path
  completes, this sequence is a software precedent for the candidate operation
  "bus power on with card clock off and no card command". It is not a
  measurement and does not establish that the rails reach or hold their
  requested values, nor that the acknowledgement was successful.

## 5. What it does not establish

- `[U]` Actual L24/L11 voltage, ramp, inrush, overshoot, settling or
  brownout behaviour at any point in the sequence.
- `[U]` The effective RPM aggregate, other masters' votes, and retention across
  handoff, suspend, shutdown or reboot.
- `[U]` The electrical order required by the PM6125, the eMMC or the board;
  the 10 ms default is not a sourced minimum.
- `[U]` Back-powering, unpowered-drive limits, pad-domain behaviour or leakage
  during the BUS_ON window.
- `[U]` PM6125 fault semantics and the acknowledge/pending-request decision
  (`E8`), recovery and safe-stop (`E9`), and current/loading (`E3`, `E4`).
- `[U]` RPM acknowledgement latency and any firmware soft-start or sequencing
  that the AP cannot observe.
- `[U]` Whether the pinned LineageOS sources, the overlay and the captured live
  DT correspond exactly to the shipped kernel binary and the exact PCB
  population; the match is at source-hash and name/ID level, not a binary or
  board-level authentication.
- `[U]` The timeout and failure-acknowledgement paths' electrical consequences,
  and the clock state written by the register-restore path.
- `[U]` Whether the shipped CXB1 binary contains the LineageOS card-detect
  early-fail and no-card cleanup blocks, which are absent from the official
  CXG1/CWI2 source.

## 6. Relationship to the pinned port

`[S]` The pinned mainline port used for the XNU work is a different software
path: `sdhc_1` has no `vmmc-supply` or `vqmmc-supply`, the AP sends no L11/L24
RPM vote, and the mainline `sdhci-msm` power-IRQ path returns success without a
regulator action (`linux-regulator-lifecycle.md`). The stock vendor trace above
therefore describes the vendor design, not the port's current behaviour `[S]`.

`[C]` Adopting the vendor order in a future candidate would mean adding an
explicit RPM enable/voltage vote for L24 and L11 before any clock or card
command, while accounting for RPM aggregation and the existing UFS consumers.
That is a design change, not an existing capability, and it still requires the
electrical contract, measurement and independent review `[C]`.

No gate is closed, no measurement was made, and no device state was changed by
this trace `[S]`.
