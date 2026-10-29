# gta4l `sdhc_1` dependency trace (beyond L11/L24)

Status: host-only source and DT research `[S]`. No device connection, build,
flash, boot, power change or commit was made. Network access was limited to
fetching the pinned-revision source files listed under Inputs; no live system was
contacted. This document records what the pinned sources require for `sdhc_1`
card operation **beyond** the PM6125 L11/L24 rails, what the XNU port already
has, and what remains unknown. It does not authorize card commands, DMA, or
hardware writes.

Tags: **[S]** source/DT-established, **[C]** conditional inference (condition
stated), **[U]** unknown.

## Inputs

| Input | Use |
| --- | --- |
| `/tmp/opencode/linux-verify/{sm6115,pm6125,board}.dts*` | Pinned mainline SoC/board wiring `[S]` |
| `sdhci-msm.c`, `opp-core.c`, `rpmpd.c`, `regulator-core.c` | Pinned driver behaviour `[S]` |
| `sdhci.c`, `mmc-core.c`, `pmdomain-core.c`, `icc-core.c`, `icc-rpm.c`, `sm6115.c`, `gcc-sm6115.c` | Pinned-revision copies fetched into `/tmp/opencode/linux-verify/`; hashes in §7.1 `[S]` |
| `device-dump/private/gta4l-dump/live.dts` | Captured vendor DT `[S]` |
| `src/Mu-Silicium/Binaries/gta4l/...` (`SdccDxe.efi`, `uefiplat.cfg`) | Handoff exit path `[S]` |
| `package/Gta4lSdhciHostInit.*`, `docs/sdhci-handoff-audit.md`, `docs/sdhci-host-init.md` | Current XNU state `[S]` |

## Summary table

| # | Dependency | Linux / handoff state `[S]` | XNU has it? | Disposition |
| - | --- | --- | --- | --- |
| 1 | `SM6115_VDDCX` corner | Genpd attach powers the domain; probe requests SVS_PLUS (192); genpd aggregates across attached devices/subdomains and the provider caches that aggregate, which may be higher; a pre-sync aggregate may emit `max_state` `[S]` | No rpmpd/RPM code | Request or prove sufficient; emitted corner and physical voltage are not measurements `[C]` |
| 2 | SDHC interconnect | Probe binds both paths and requests 800000/300000 kBps; both paths attempt RPM master/slave votes and provider bus-clock votes `[S]` | No ICC driver | DMA prerequisite; PIO does not use the DMA master `[S/C]` |
| 3 | GCC SDCC1 branches | Enable bits set at handoff; frequency not measured (`ac=0x4221`, `ahb=0x1`, `ice=0x4229`, `bcr=0`) `[S]` | Read/verify only | Keep as a gate; card-clock output gating is controller-local, a source-rate change needs the RCG `[S/C]` |
| 4 | TLMM SDC1 pads | Configured by UEFI; mainline does not bind pinctrl to `sdhc_1` `[S]` | No TLMM code | Trust handoff for read-only, or program pads before commands `[C]` |
| 5 | SMMU stream `0xc0` | Mainline DT attaches it; vendor DT has no `iommus`; no SDHC attachment found in the inspected SDCC/`uefiplat` scope `[S]` | No SMMU driver | Bypass state is unknown; absence of references is not proof `[U]` |
| 6 | L24 vmmc / L11 vqmmc | Vendor levels are requested values; handoff state and retention are unknown `[U]` | No regulator path | Prior L11/L24 contract; see `power-retention-evidence.md` |
| 7 | HC IRQ 348 / power IRQ 352 | Wired in DT/vendor `[S]` | Not mapped | Needed to handle core power requests `[C]` |

**Supply correction `[S]`:** `pm6125_l5a` is `sdhc_2`'s `vqmmc`, not `sdhc_1`'s
(`board.dts:558-559`). Vendor `sdhc_1` names L24 as `vdd` and L11 as `vdd-io`
(`live.dts:2062-2069`), but those are board requests, not measured levels.
Pinned `&sdhc_1` has no `vmmc-supply`/`vqmmc-supply` (`board.dts:528-532`) `[S]`.

## 1. `SM6115_VDDCX` / CX corner

- `sdhc_1` binds `power-domains = <&rpmpd SM6115_VDDCX>` (`sm6115.dtsi:1118`)
  and an OPP table whose `required-opps` select CX corners:
  100/192 MHz → `rpmpd_opp_low_svs`, 384 MHz → `rpmpd_opp_svs_plus`
  (`sm6115.dtsi:1134-1153`) `[S]`.
- Probe calls `dev_pm_opp_set_rate(&pdev->dev, INT_MAX)` before any card
  operation (`sdhci-msm.c:2741`), which selects the 384 MHz OPP `[S]`.
- `_set_required_opps()` → `_set_opp_level()` →
  `dev_pm_domain_set_performance_state()` (`opp-core.c:1178-1235`) `[S]`.
- `SM6115_VDDCX` maps to `cx_rwcx0_lvl` (`res_type = RPMPD_RWCX`, `res_id = 0`,
  `key = KEY_LEVEL`) (`rpmpd.c:75-81`) `[S]`.
- `rpmpd_set_performance()` clamps to `max_state`, caches the corner, and
  returns without sending when the domain is not enabled and the key is not a
  floor key (`rpmpd.c:1065-1083`) `[S]`.
- `rpmpd_aggregate_corner()` takes the maximum of this domain and its peer; before
  `state_synced` it substitutes `max_state` for this domain's corner, and it
  writes both active and sleep states (`rpmpd.c:990-1028`) `[S]`.
- `rpmpd_sync_state()` sets `state_synced`, clears the corner if the domain is
  not enabled, then aggregates again (`rpmpd.c:1146-1172`) `[S]`.
- Therefore the probe requests SVS_PLUS, but genpd re-evaluates the domain-wide
  maximum before calling the provider, so the corner cached in `pd->corner` and
  the value emitted are the aggregate across all attached devices and
  subdomains, which may be higher; before synchronization the rpmpd aggregate
  may also emit `max_state` `[S]`.
- `genpd_dev_pm_attach()` passes `power_on=true`, and
  `__genpd_dev_pm_attach()` calls `genpd_power_on()` before the device probes
  (`pmdomain-core.c:3448-3461`, `3411-3415`), which invokes the rpmpd power-on
  path `[S]`.
- Runtime suspend attempts to drop the rate (`dev_pm_opp_set_rate(dev, 0)`);
  resume attempts to restore it (`sdhci-msm.c:2944`, `2973`). Both return values
  are ignored, and a failed bandwidth removal can abort OPP teardown before the
  required-state removal `[S]`.

**What this does and does not establish `[S]/[C]`:** the genpd attach powers the
CX domain on and the probe's request enters the rpmpd send path, unless attach or
power-on fails. The requested corner is SVS_PLUS, but the emitted value is the
domain-wide aggregate and a pre-sync aggregate can emit `max_state`; whether
either value produces the expected physical CX voltage
is not established. `cx_rwcx0_lvl` does not set `active_only`, so its own sleep
contribution defaults to the same corner as its active contribution; the peer's
`active_only` value and synchronization still qualify the aggregate
`[S]/[C]`.

**XNU `[S]`:** no rpmpd/RPM/VDDCX/OPP/genpd handling in `package/`, `scripts/`,
`dts/` or `board.toml`; `dts/gta4l.dts` is a memory/reserved-memory wrapper and
does not describe `sdhc_1` or `rpmpd`. `ShengPlatformPower` is an Apple PMGR stub.

**Conclusion `[C]`:** Linux powers the CX domain at attach and requests SVS_PLUS
at probe; the request path is established, but the physical voltage is not.
Whether the eMMC needs that corner if firmware left CX high is `[U]`. XNU must
either add an RPM-SMD level vote or prove the retained CX state is sufficient.

## 2. SDHC OPP / interconnect

- OPP table (`sm6115.dtsi:1131-1154`) `[S]`:

  | Rate | `required-opps` | peak kBps | avg kBps |
  | --- | --- | --- | --- |
  | 100 MHz | low_svs | 250000 / 133320 | 102400 / 65000 |
  | 192 MHz | low_svs | 800000 / 300000 | 204800 / 200000 |
  | 384 MHz | svs_plus | 800000 / 300000 | 204800 / 200000 |

- Two paths (`sm6115.dtsi:1121-1126`) `[S]`:
  - `sdhc-ddr`: `system_noc MASTER_SDCC_1` → `bimc SLAVE_EBI_CH0`.
  - `cpu-sdhc`: `bimc MASTER_AMPSS_M0` → `config_noc SLAVE_SDCC_1`.
- `dev_pm_opp_of_find_icc_paths()` binds them at probe (`sdhci-msm.c:2725`);
  `_set_opp_bw()` calls `icc_set_bw()` (`opp-core.c:1150-1174`) `[S]`.
- The probe max-rate vote therefore also attempts to request the 384 MHz bandwidth;
  a failed call is only warned about (`sdhci-msm.c:2741-2743`) `[S]`.
- Vendor uses legacy `qcom,msm-bus` vectors instead (`live.dts:2043-2047`) `[S]`.

**Complete path accounting `[S]`.** `apply_constraints()` calls a provider's
`set()` only for consecutive nodes that share that provider; cross-provider
edges are skipped because no SM6115 provider sets `inter_set`
(`icc-core.c:304-327`, `icc-rpm.c:554-559`) `[S]`. `qcom_icc_set()` sends an RPM
request for every non-`-1` master/slave id and an aggregate bus-clock rate for
the provider (`icc-rpm.c:207-245`, `327-374`, `376-445`) `[S]`.

| Path | Same-provider segments | RPM ids sent | Provider bus clocks |
| --- | --- | --- | --- |
| `sdhc-ddr` | `xm_sdc1`→`slv_anoc_snoc`→`mas_anoc_snoc`→`slv_snoc_bimc` (system_noc); `mas_snoc_bimc`→`ebi` (bimc) | master 33 (`xm_sdc1`); slave 24 (`slv_snoc_bimc`); master 3 (`mas_snoc_bimc`); slave 0 (`ebi`) | `bus_2_clk` (system_noc); `bimc_clk` (bimc) |
| `cpu-sdhc` | `apps_proc`→`slv_bimc_snoc` (bimc); `mas_bimc_snoc`→`slv_snoc_cnoc` (system_noc); `mas_snoc_cnoc`→`qhs_sdc1` (config_noc) | master 0 (`apps_proc`); slave 2 (`slv_bimc_snoc`); master 21 (`mas_bimc_snoc`); slave 25 (`slv_snoc_cnoc`) | `bimc_clk`; `bus_2_clk`; `bus_1_clk` |

- Ids come from the node definitions in `sm6115.c`: `apps_proc` 126-139,
  `mas_snoc_bimc` 175-188, `mas_snoc_cnoc` 303-312, `mas_anoc_snoc` 445-454,
  `mas_bimc_snoc` 465-474, `xm_sdc1` 562-574, `ebi` 604-611,
  `slv_bimc_snoc` 617-626, `qhs_sdc1` 961-968, `slv_snoc_cnoc` 1109-1118,
  `slv_snoc_bimc` 1142-1151, `slv_anoc_snoc` 1184-1193 `[S]`.
- The destination `qhs_sdc1` has both ids `-1`, but the path is **not**
  local-only: its source and several intermediate nodes attempt RPM votes, and all
  three providers vote bus clocks `[S]`. The earlier "local config/QoS node"
  conclusion was wrong and is withdrawn.
- Each RPM state bucket (active and sleep) is sent separately for every
  non-`-1` id (`icc-rpm.c:207-245`) and for the provider bus clock
  (`icc-rpm.c:399-416`) `[S]`.
- No-op cases: `qos.ap_owned` nodes and `-1` ids are skipped; a provider with
  no bus clock returns early; a bus-clock rate equal to the cached value is not
  re-sent (`icc-rpm.c:207-214`, `376-416`) `[S]`.
- A failed RPM send propagates back to `icc_set_bw()`; `-ENXIO` is suppressed
  only when the provider sets `ignore_enxio`, which the SM6115 descriptors do
  not (`icc-rpm.c:219-240`, `366-372`, `514`) `[S]`.
- The three providers on these paths (`bimc`, `system_noc`, `config_noc`) set
  `keep_alive = true`, which floors the active bus clock at
  `ICC_BUS_CLK_MIN_RATE` (`icc-rpm.c:382-384`, `sm6115.c:1214-1221`,
  `1285-1293`, `1328-1338`) `[S]`.
- Registration also calls `provider->set(node, node)` once per node because
  SM6115 supplies no `get_bw`; `icc_node_add()` initialises the bandwidth to
  `INT_MAX` before that call (`icc-core.c:1094-1099`, `icc-rpm.c:559`) `[S]`.
  The accounting above is for the later `icc_set_bw()` from the OPP.
- PIO fallback: `sdhci_prepare_dma()` clears `SDHCI_REQ_USE_DMA` when DMA is
  unusable and moves data with `sg_miter` plus `SDHCI_BUFFER` (`sdhci.c:1125`,
  `1158-1163`, `1191-1201`, `550-610`) `[S]`. PIO data movement is CPU MMIO and
  does not call `dma_map_sg` or use the SDHC DMA master `[S]`.

**XNU `[S]`:** no interconnect/ICC or OPP consumption. PIO does not need the
`sdhc-ddr` data-path vote, the SDHC DMA master or an SMMU mapping. Its
config-space accesses still traverse the same fabric, so whether the retained
NoC/config state and bus clocks suffice is not established `[U]`. A first
command without any RPM bandwidth vote is also not demonstrated by the pinned
driver, which votes on both paths at probe `[U]`.

## 3. GCC SDCC1 clocks and reset

- Clocks: `iface` (`GCC_SDCC1_AHB_CLK`), `core` (`GCC_SDCC1_APPS_CLK`), `xo`
  (`RPM_SMD_XO_CLK_SRC`), `ice` (`GCC_SDCC1_ICE_CORE_CLK`); reset
  `GCC_SDCC1_BCR` (`sm6115.dtsi:1110-1116`) `[S]`.
- `sdhci-msm` prepares/enables the bulk clocks at probe; ICE is fetched through
  `devm_of_qcom_ice_get()` and is only needed for inline encryption
  (`sdhci-msm.c:2695-2765`, `1920`) `[S]`.
- Observed handoff (`docs/sdhci-handoff-audit.md:143-145`) `[S]`:
  `bcr=0`, `ac=0x4221`, `ahb=0x1`, `ice=0x4229`, `cfg=0x401`, `m/n/d` set.
  The branch enable bits are set and the block reset is self-cleared; the audit
  explicitly does **not** measure frequency or prove the branch is clocking.
- `cfg=0x401` decodes to a nominal GPLL6-based rate; the clock provider table has
  100 MHz (GPLL0 aux), 192 MHz and 384 MHz (both GPLL6 main) entries
  (`gcc-sm6115.c:1248-1257`, `1260-1272`). This is a register/table decode, not a
  measured clock `[S/C]`.

**XNU `[S]`:** `Gta4lSdhciHostInit` reads the same GCC page and refuses if the
block reset is not self-cleared or the ICE branch is not running. It does not
program the RCG. **Conclusion `[S]/[C]`:** the branches need no first-probe
action to keep their current configuration. Enabling the card-clock output is a
controller clock-control operation; selecting a different source rate requires
the GCC RCG (`sdhci-msm.c:1858-1892`, `378-415`; `gcc-sm6115.c:1248-1272`,
`2692-2734`).

## 4. TLMM SDC1 pads

- Pinned pad states exist (`sdc1_clk/cmd/data/rclk`, active/sleep) in
  `sm6115.dtsi:731-779` `[S]`.
- Pinned `&sdhc_1` does **not** bind pinctrl (`board.dts:528-532`); mainline
  relies on the bootloader. `sdhci_msm_set_pincfg()` applies states only from the
  power IRQ handler (`sdhci-msm.c:1431-1443`, `1739-1745`) `[S]`.
- Vendor `sdhc_1` binds `sdc1_*_on/off` (`live.dts:2070-2072`) `[S]`.
- UEFI `uefiplat.cfg` sets `Sdc1GpioConfigOn = 0x1E92`, `...Off = 0xA00`;
  `SdccDxe` builds `0x00575000` and writes `0x9fe4` for controller 0, or adds
  `0x3fe000` and writes `0x1fe4` at `0x00973000` for controller 1
  (`/tmp/opencode/sdccdxe.dis:3020-3033`) `[S]`. **Correction:** an earlier
  version of this document used `0x0093e000` from a secondary summary; the
  disassembly arithmetic yields `0x00973000`. The exact mapping between those
  writes and the config values remains `[U]`.

### Decoded vendor settings versus handoff state

| Evidence | Programmed/declared setting | Tag |
| --- | --- | --- |
| Vendor `sdc1_clk_on` / `sdc1_clk_off` (`live.dts:9270-9288`) | bias-disable, drive 16 / bias-disable, drive 2 | `[S]` |
| Vendor `sdc1_cmd_on` / `sdc1_cmd_off` (`live.dts:9290-9308`) | bias-pull-up, drive 10 / bias-pull-up, drive 2 | `[S]` |
| Vendor `sdc1_data_on` / `sdc1_data_off` (`live.dts:9310-9328`) | bias-pull-up, drive 10 / bias-pull-up, drive 2 | `[S]` |
| Vendor `sdc1_rclk_on` / `sdc1_rclk_off` (`live.dts:9330-9346`) | bias-pull-down / bias-pull-down | `[S]` |
| Pinned `sm6115.dtsi` active/sleep states (`sm6115.dtsi:731-779`) | same settings as the vendor states above | `[S]` |
| Vendor `sdhc_1` pinctrl binding (`live.dts:2070-2072`) | active = `0x289`/`0x28b`/`0x28d`/`0x28f`; sleep = `0x28a`/`0x28c`/`0x28e`/`0x290` | `[S]` |
| `uefiplat.cfg` | `Sdc1GpioConfigOn = 0x1E92`, `Sdc1GpioConfigOff = 0xA00` | `[S]` |
| `SdccDxe` controller-1 write | `0x1fe4` at TLMM page `0x00973000` | `[S]` |
| Mapping between the config values, the `SdccDxe` write and the vendor pad fields | not established | `[U]` |
| Actual SDC1 pad state at XNU handoff | not measured | `[U]` |

The vendor and pinned device trees agree on the active/sleep pad settings, so
the decoded intent is consistent `[S]`. That is not a handoff observation: the
UEFI write is established, but its field encoding and the resulting pad state
are not. The board also declares both `sdhc_1` and `ufshc` on L24 and L11, with
the parents `vdd_l23_l24` shared with L23 and `vdd_l9_l11` shared with L9; the
declaration does not establish physical population or loading (`sdhc-electrical-
contract.md` §2) `[S]/[U]`.

**XNU `[S]`:** no TLMM/pinctrl code. `[C]` For a read-only probe the pads are
expected to remain as UEFI left them, but that expectation is not a measurement.
Before commands that depend on pull-ups or drive strength, XNU should either
trust the handoff explicitly or program the `sdc1_*_on` values; the exact
programming sequence is not established `[U]`.

## 5. SMMU / IOMMU

- Pinned `sdhc_1` has `iommus = <&apps_smmu 0x00c0 0x0>` (`sm6115.dtsi:1120`);
  `sdhc_2` uses `0x00a0` `[S]`.
- `sdhci-msm.c` has no explicit IOMMU calls; attachment is implicit through the
  generic DMA/IOMMU core for a node with `iommus` `[C]`.
- Vendor `sdhc_1` has **no `iommus`** (`live.dts:2031-2072`) `[S]`.
- `SdccDxe` has no SMMU/IOMMU references in the inspected disassembly; the
  inspected `uefiplat.cfg` SMMU entries are for UFS `[S]`.

**XNU `[S]`:** no SMMU/IOMMU driver. **Conclusion `[S]/[U]`:** PIO does not use
the SDHC DMA master, so it does not depend on stream `0xc0`; DMA does. Missing DT
and firmware references do not prove that the stream is bypassed, and an identity
mapping is not a fallback because it still requires configured translation
tables. Before any DMA test XNU must establish the actual state of stream `0xc0`,
or use a path that does not depend on it.

## 6. Interrupts

- Pinned `hc_irq` = SPI 348, `pwr_irq` = SPI 352 (`sm6115.dtsi:1106-1108`) `[S]`.
- The power IRQ handler reads `CORE_PWRCTL_STATUS`, clears it, and responds to
  `CORE_PWRCTL_BUS_ON/OFF` and `IO_HIGH/LOW` by applying vmmc, vqmmc and pinctrl
  and acknowledging through `CORE_PWRCTL_CTL` (`sdhci-msm.c:1686-1829`) `[S]`.
- The local request chain is: `mmc_power_up()` sets the power mode and calls
  `mmc_set_ios()` (`mmc-core.c:1338-1363`); `sdhci_set_ios()` calls
  `host->ops->set_power` when present, otherwise `sdhci_set_power()`
  (`sdhci.c:2413-2416`). `sdhci-msm` sets `.set_power = sdhci_set_power_noreg`
  (`sdhci-msm.c:2564-2576`), so this path uses `sdhci_set_power_noreg()`, which
  writes `SDHCI_POWER_CONTROL` (`sdhci.c:2121-2173`) `[S]`.
- The msm write wrapper maps that `SDHCI_POWER_CONTROL` write to
  `REQ_BUS_ON/OFF` and calls `sdhci_msm_check_power_status()`
  (`sdhci-msm.c:2316-2381`); that function waits for the power IRQ unless the
  request is already satisfied or cannot produce one, and
  `sdhci_msm_handle_pwr_irq()` applies vmmc/vqmmc/pinctrl and acknowledges
  (`sdhci-msm.c:1592-1676`, `1686-1829`) `[S]`.
- `sdhci_set_power_noreg()` returns without a write when the cached `host->pwr`
  already equals the requested value (`sdhci.c:2131-2136`), so a repeated
  identical mode emits no new request `[S]`.
- Signal-voltage writes to `SDHCI_HOST_CONTROL2` map to `REQ_IO_LOW/HIGH`
  through the same wrapper (`sdhci.c:2636-2710`, `sdhci-msm.c:2323-2326`) `[S]`.
- The handler **responds to** those requests; the decision to enter a power mode
  comes from the MMC core sequence above `[S]`.
- XNU has not mapped these IRQs `[S]`.

## 7. Host-side tracing results

| Question | Result |
| --- | --- |
| OPP → genpd path | `_set_opp_level()` requests a performance state; genpd attach powers the domain on; `rpmpd_set_performance()` then sends unless disabled, and a pre-sync `rpmpd_aggregate_corner()` substitutes `max_state` before maxing peer votes `[S]` |
| CX active/sleep encoding | `to_active_sleep()` sets sleep to zero only when `active_only`; `cx_rwcx0_lvl` does not set it, so its own sleep value equals its active value `[S]` |
| ICC provider | Both paths attempt RPM votes: `sdhc-ddr` master 33/3 + slave 24/0; `cpu-sdhc` master 0/21 + slave 2/25, plus provider bus clocks; only the destination `qhs_sdc1` is id-less `[S]` |
| Power-request generation | `mmc_power_up()` → `sdhci_set_ios()` → `host->ops->set_power` = `sdhci_set_power_noreg()` → `SDHCI_POWER_CONTROL` → msm wrapper → wait for IRQ → handler `[S]` |
| PIO vs DMA | PIO uses `sg_miter` and `SDHCI_BUFFER` and clears `SDHCI_REQ_USE_DMA`; it does not use `dma_map_sg` or the SDHC DMA master `[S]` |
| TLMM SDC2 base | `0x00973000` from the `0x00575000 + 0x3fe000` arithmetic in `sdccdxe.dis:3020-3033` `[S]` |
| Card-clock enable vs rate | Separate: `__sdhci_msm_set_clock()` gates `SDHCI_CLOCK_CONTROL`, while `msm_set_clock_rate_for_bus_mode()` selects the GCC rate `[S]` |

### 7.1 Fetched source provenance

Files fetched from `limonabisi/linux-gta4l` at commit
`cf5f763f0ef8a5b4d8bb4163833cba1160c51894` into `/tmp/opencode/linux-verify/`:

| File | SHA-256 |
| --- | --- |
| `sdhci.c` | `186305e102bd32df419ff80669e6018d6b3a9e4310c3101193413970355b9857` |
| `mmc-core.c` | `56678ec9c220d952b63f4e2af7072f7d3b815cd867c7809c937b5f38cddd5304` |
| `pmdomain-core.c` | `32e2f6b0988eba52a2f662fdc2d7132022fbe980f281b2a82babcf3d7f46e18a` |
| `icc-core.c` | `45a71ac0cfb9b9d07bcb15eb93133ba0aa847aea1428ef072f75ba6e971b7bf5` |
| `icc-rpm.c` | `7e125d9308913c32803b0d482c070fb68356a00d19e0e9a687ac75fd5208fec0` |
| `sm6115.c` (ICC provider) | `d4cff8991af9bde1820f872b75bcb8259b8a0e4c87cf976582b61674707d7efb` |
| `gcc-sm6115.c` | `9207beba399bf366902fcebf6a57fbb890c9f0289c37a301a3513b4d9d232f97` |
| `icc-rpm.h` | `76792d3702e438e44a44ca93d86827940baa764833391319a8f887829e21b83c` |

## 8. Prerequisite checklist before card operation

1. GCC SDCC1 branches — enable bits set at handoff `[S]`; keep the HostInit gate.
2. Block reset — `bcr=0` at handoff `[S]`; keep the self-clear gate.
3. Card clock / RCG — `clk=0003` (internal clock stable, card clock off) `[S]`;
   enabling the card-clock output is a controller clock-control operation, and
   selecting a new source rate requires the `apps` RCG `[S/C]`.
4. L24 / L11 — handoff state and retention unknown `[U]`; see
   `power-retention-evidence.md`. Vendor levels are requests, not measurements.
5. TLMM SDC1 pads — configured by UEFI `[S]`; trust for read-only, program before
   pull-up-dependent commands `[C]`.
6. CX corner — Linux powers the domain at attach and requests SVS_PLUS at probe
   `[S]`; genpd aggregates across devices and the provider caches that aggregate,
   and a pre-sync aggregate may emit `max_state` `[S]`. XNU cannot vote `[S]`;
   the corner must be requested or proven sufficient `[C]`.
7. Interconnect bandwidth — both paths attempt RPM master/slave and bus-clock
   votes `[S]`; required for DMA at speed. PIO does not use the SDHC DMA master,
   but its config-space dependence on retained fabric state is not established
   `[C/U]`.
8. SMMU stream `0xc0` — actual state unknown `[U]`; establish before DMA. PIO
   does not depend on it `[S]`.
9. Power IRQ 352 / HC IRQ 348 — map before relying on the power-request
   handshake `[C]`.
10. ICE branch — enable bits set `[S]`; needed only for inline encryption.
    Initialization is reached through the CQE add-host path and needs the
    CQHCI/ICE/SCM infrastructure, but active CQE command operation is not
    required because non-CQE encrypted requests are supported
    (`sdhci-msm.c:2058-2117`) `[C]`.

## 9. Open items

1. Whether CX SVS_PLUS is physically required if firmware left CX high `[U]`.
2. Whether the retained NoC/ICC state satisfies the 384 MHz OPP bandwidth `[U]`.
3. Whether PIO needs any ICC config-path state beyond what firmware retained `[U]`.
4. Exact SMMU state of stream `0xc0` at XNU handoff `[U]`.
5. Exact semantics of the `SdccDxe` TLMM writes vs `uefiplat.cfg` values `[U]`.
6. Whether the HostInit ICE-branch refusal should remain a hard gate `[C]`.

## 10. Readiness prerequisites by capability

These are separate capabilities, not one mandatory progression `[S]`. A
capability may be exercised only when its own prerequisites are established.

| Capability | Prerequisites | Current status |
| --- | --- | --- |
| Controller-local diagnostics | MMIO reads, version/reset gates, no power or card action | Observed `[S]` |
| Power and clock activation | Rail state and retention, CX sufficiency, pads, card-clock output and source rate | Blocked `[U]` |
| Card commands / PIO | Power/clock activation, command path, mapped IRQs or an explicit polling design; no SMMU or DMA-master dependency `[S]` | Blocked `[U]` |
| DMA | Card commands plus SMMU stream state or a verified non-SMMU path, and ICC bandwidth | Blocked `[U]` |
| High-speed modes / tuning | Card commands, clock rate selection and DLL/tuning; DMA is not inherently required | Blocked `[U]` |
| CQE | Card commands plus CQHCI register block (`hc+0x1000`), the `supports-cqe` DT property that gates `sdhci_msm_cqe_add_host()`, `cqhci_init(cq_host, mmc, dma64)` with the host DMA width, and the ADMA/DMA path (so the DMA row's SMMU/ICC prerequisites apply); uses the same `hc_irq`. Once that path is taken the driver sets `MMC_CAP2_CQE`/`_DCMD` unconditionally; a separate CQE capability probe is not performed `[S]`. XNU-side prerequisites beyond this list are not established `[U]` | Blocked `[U]`; deferred, list incomplete |
| Inline crypto (ICE) | `sdhci_msm_ice_init()` (called before `cqhci_init()`) requires `CQHCI_CAP & CQHCI_CAP_CS`, the ICE device from `devm_of_qcom_ice_get()`, `CQHCI_CCAP` crypto-capability registers, and the SCM keyslot/derive path; the ICE clock branch is necessary but not sufficient. XNU-side prerequisites beyond this list are not established `[U]` | Blocked `[U]`; deferred, SCM path untraced and list incomplete |

## Safety disposition

No card command, DMA, storage write, bus-power enable, card-clock enable, PMIC
access or RPM vote is authorized by this document `[S]`. It records prerequisites
only.
