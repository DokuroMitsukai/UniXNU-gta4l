# PM6125 L11/L24 retention evidence (host-only)

Status: host-only source and firmware research `[S]`. No device connection,
build, flash, boot or power change was made `[S]`. This document asks one
question: **what do the pinned sources establish about RPM firmware retention of
the PM6125 L11 (`vdd-io`) and L24 (`vdd`) rails across the XNU handoff and the
power transitions?** It does not measure either rail.

Tags: **[S]** source/DT-established, **[C]** conditional inference (condition
stated), **[U]** unknown.

## Inputs

| Input | Use |
| --- | --- |
| `/tmp/opencode/linux-verify/{board,sm6115,pm6125,pmi632}.dts*` | Pinned mainline board and SoC wiring `[S]` |
| `qcom_smd-regulator.c`, `rpmpd.c`, `regulator-core.c` (`core.c`), `sdhci-msm.c`, `opp-core.c`, `icc-core.c`, `icc-rpm.c` | Pinned RPM, power-domain, regulator and interconnect behaviour `[S]` |
| `device-dump/private/gta4l-dump/live.dts` | Captured vendor DT; post-unlock, not authenticated stock `[S]` |
| `device-dump/private/gta4l-dump/partitions/{rpm,xbl}.img` | Firmware images present but not fully analysed `[S]` |
| Mu-Silicium `SPMIDxe.efi`, `PmicDxeLa.efi`, `SdccDxe.efi` | Handoff exit paths; hashes match `firmware-handoff.md` `[S]` |
| `docs/rpm-power-interface.md`, `docs/power-path-decision.md`, `docs/linux-regulator-lifecycle.md`, `docs/firmware-handoff.md` | Prior host-only traces `[S]` |

## Bottom line

- **[S]** The pinned mainline board sends **no RPM vote at all** for L11 or L24
  in the traced lifecycle. Registration sends no packet; the driver only caches
  software state and has no readback.
- **[S]** VDDCX and interconnect are different: the AP sends explicit RPM votes
  for `SM6115_VDDCX` (active and sleep corners), and its OPP interconnect
  requests are converted into RPM master/slave and bus-clock vote attempts on
  both SDHC paths.
- **[S]** The traced Mu-Silicium exit paths do **not directly address** the
  L11/L24 control registers (`0x40/0x41/0x45/0x46/0x48/0x4c/0x61`), but the PMIC
  exit path is
  **not passive** (interrupt latch/enable clears plus a `+0x28` arbiter
  interrupt status/clear).
- **[U]** Nothing in the pinned set proves L11/L24 are on at the XNU handoff, or
  that they survive AP suspend, shutdown or reboot. "Unaddressed by direct
  control writes" is not "retained".
- **[U]** `qcom,set`, `qcom,init-voltage`, `qcom,vdd-io-always-on` and
  `qcom,vdd-io-lpm-sup` are vendor board-description properties. They express
  intent; they are not a latch, a register value or a measurement.

## Evidence by transition

| Transition | Established `[S]` | Conditional `[C]` | Unknown `[U]` |
| --- | --- | --- | --- |
| (a) Bootloader/UEFI → XNU | SDCC exit path touches pads/SDHCI only; PMIC exit path writes SPMI bytes `0x14/0x16/0xd1`; no LDO entries in `uefiplat.cfg` | If XBL/RPM left the rails enabled and no indirect PMIC effect occurs, the traced UEFI path does not directly turn them off | Actual voltage/enable at handoff; runtime `+0x28` bitmap contents; XBL/RPM initial programming |
| (b) AP suspend/resume | No suspend/sleep ops on the RPM regulator; no `regulator-state-mem`; `regulator_suspend()` returns 0 when no suspend state applies; system sleep is `pm_runtime_force_suspend()` → `sdhci_msm_runtime_suspend()`, which attempts to drop the OPP rate (return value ignored), disables the bulk clocks and suspends ICE when crypto is enabled; it does not disable the bus clock or touch L11/L24 | If a suspend state did apply, the core would call the regulator's `set_suspend_*` ops; if RPM has a vendor sleep set for these LDOs, it could change them in sleep; if ICE was enabled, its resume path runs | RPM sleep policy for LDOA 11/24; whether any other agent changes them in sleep |
| (c) Shutdown | `sdhci-msm` registers no `.shutdown`, so system shutdown does not invoke the driver; the separate remove path calls `sdhci_remove_host()`, disables bulk/bus clocks, and calls `regulator_disable()` only on `vqmmc` when the core enabled it; mainline `sdhc_1` has no supply, and `mmc_regulator_get_supply()` uses optional lookups that return `-ENODEV` (not a dummy regulator), so the `vqmmc` disable is skipped; late cleanup also skips disable because cached `is_enabled=0` | If RPM/PON resets the PMIC on AP shutdown, rails may drop; if RPM stays powered, they may persist | Actual PMIC/PON/RPM shutdown behaviour |
| (d) Reboot | No pinned source traces the XNU reboot path or a PMIC/PON reset | If XNU's reboot resets the PMIC, the next-handoff state is set by XBL/RPM, not by prior XNU state | Whether reboot resets the PMIC, what XNU writes, and what XBL/RPM reprogram |

## 1. AP votes for L11/L24

- The board declares L11 and L24 with min/max and `regulator-allow-set-load`
  only: no `regulator-boot-on`, no `regulator-always-on`, no consumer
  (`board.dts` L11/L24 nodes). The phandles are defined and never referenced
  `[S]`.
- `&sdhc_1` has no `vmmc-supply` or `vqmmc-supply`; only `&sdhc_2` wires L22/L5
  (`board.dts:558-559`) `[S]`.
- Probe registers each child with `devm_regulator_register()` and issues no RPM
  request (`qcom_smd-regulator.c`) `[S]`.
- The driver caches `is_enabled`, `uV` and `load`; `rpm_reg_is_enabled()` and
  `rpm_reg_get_voltage()` return the cache only `[S]`.
- `rpm_reg_write_active()` emits only changed keys, includes `uv`/`ma` only when
  cached `is_enabled` is set, and always uses the active state; with no pending
  keys it returns 0 without a packet `[S]`.
- Registration calls `set_voltage` because `apply_uV` is true, but that only
  sets `uv_updated`; with `is_enabled=0` and no enable, the write produces zero
  keys `[S]`.
- `always_on`/`boot_on` are false, so registration does not enable; late cleanup
  reads cached `is_enabled=0` and skips disable `[S]`.

**Caveat `[C]`:** "no packet" is a property of this driver plus this DT. A
consumer, a `boot-on` flag or a hardware-read `is_enabled` implementation would
change the outcome.

## 2. Vendor DT properties

- L11 and L24 carry `qcom,set = <0x03>`, `qcom,init-voltage` and a min/max range
  in `live.dts`; neither has `regulator-always-on` or `regulator-boot-on` `[S]`.
- No pinned driver parses `qcom,set`; its exact semantics are `[U]` from pinned
  sources `[S]`.
- `[C]` If the common downstream meaning applies (a bitmask selecting active and
  sleep RPM sets), `0x03` would mean "apply to both sets". Even then it is a
  vote/description consumed by a downstream driver, not proof that RPM keeps the
  rail on, and it says nothing about shutdown or reboot.
- `qcom,init-voltage` is a downstream initial-vote value, not a hardware readback
  and not evidence of what XBL programmed `[S]`.
- `regulator-always-on` prevents the AP from disabling a rail and forces it on at
  registration; `regulator-boot-on` only requests enable at registration and does
  not permanently prohibit a later disable (`core.c:1662-1697`). Neither programs
  an RPM sleep set `[S]`. Both are absent for L11/L24 in the pinned mainline and
  vendor DTs; they appear on other rails `[S]`.
- `qcom,vdd-io-always-on` and `qcom,vdd-io-lpm-sup` sit on the vendor `sdhc_1`
  node, not the regulator node. The pinned `sdhci-msm.c` does not parse them
  `[S]`; their effect is `[U]`. They are downstream hints, not RPM commands and
  not state proof.

## 3. VDDCX and interconnect contrast

- `sdhc_1` binds `power-domains = <&rpmpd SM6115_VDDCX>`, an OPP table and two
  interconnects (`sm6115.dtsi`); the OPP table has `required-opps` and bandwidth
  but no `opp-microvolt` `[S]`.
- `SM6115_VDDCX` maps to `cx_rwcx0_lvl` with `res_type = RPMPD_RWCX` and
  `key = KEY_LEVEL`; `rpmpd_send_corner()` writes both active and sleep states,
  and `rpmpd_send_enable()` writes `swen` on the active state `[S]`.
- OPP `_set_opp()` calls `_set_required_opps()` and `_set_opp_bw()`; the latter
  calls `icc_set_bw()` `[S]`. `sdhci-msm` never calls
  `dev_pm_opp_set_regulators()`, and the `sdhc1` OPP table has no supply `[S]`.
- `sdhci-msm` runtime suspend attempts to drop the OPP rate (return value
  ignored) and disables the bulk clocks (not `bus_clk`); system sleep uses
  `pm_runtime_force_suspend/resume` `[S]`.
- **Contrast:** VDDCX is an explicit RPM resource with active+sleep votes; L11/L24
  are `QCOM_SMD_RPM_LDOA` ids 11/24 and receive no vote here `[S]`.
- `[S]` Both SDHC interconnect paths attempt RPM master/slave votes and provider
  bus-clock votes through `icc-rpm.c`; see
  `docs/sdhc-dependency-trace.md` §2. The destination `qhs_sdc1` alone is
  id-less, so neither path is local-only.

## 4. Mu-Silicium handoff

- `SdccDxe` has no PMIC/SPMI/LDO/regulator strings or protocol GUIDs. Its exit
  handler configures SDC pads, sets SDHCI bus power, runs the v5 reset/power
  handshake and writes vendor registers; the reset clears the standard power
  and clock registers and the handler does not re-enable bus power. It does not
  write PMIC/SPMI/RPM/LDO `[S]`.
- `PmicDxeLa` writes SPMI register bytes `0x14`, `0x16` and `0xd1` `[S]`.
- The L11/L24 control registers are `0x40/0x41/0x45/0x46/0x48/0x4c/0x61` at
  peripherals
  `0x4a`/`0x57`, so those writes cannot directly address them `[S]`.
- On peripherals with the PMIC interrupt layout, `0x14`/`0x16` are
  `QPNPINT_REG_LATCHED_CLR`/`EN_CLR`; the `+0x28` operation is the SPMI-arbiter
  v5 `irq_status`/`irq_clear` pair at base `0x01C00000`, with the runtime bitmap
  contents still unknown `[S]/[U]`.
- `uefiplat.cfg` has no LDO/VREG entry `[S]`.
- **Net:** the traced path leaves the rails **unaddressed by direct control
  writes**, not proven on, and does not demonstrate an off/reprogram either
  `[S]`.

## 5. What is not established

- No pinned source, DT or firmware trace reads or measures L11/L24 at the XNU
  handoff `[U]`.
- No traced AP path disables or reprograms them; the absence of a traced write is
  not proof that RPM or another agent did not act `[U]`.
- The only XNU-time hardware observations are controller-local (`pwr=00`,
  `clk=0003`, ...); `SDHCI_POWER_CONTROL == 0` says nothing about L11/L24 `[S]`.
- Positive evidence is intent only: the vendor `sdhc_1`/`ufshc` consume L24/L11
  and the vendor DT sets `qcom,init-voltage` `[S]`.

## 6. Missing evidence

### Host-side answerable

1. Static analysis of `rpm.img` and `xbl.img` for LDOA 11/24 resource tables,
   sleep sets, PON/shutdown and reboot re-init. A bounded trace is recorded in
   §7: it finds LDOA resource tables for both rails and no direct L11/L24
   voltage, mode or enable write in the traced paths, but not the runtime
   policy or a retention result `[S]/[U]`.
2. Downstream vendor `qcom_smd-regulator`/`sdhci-msm` source to define
   `qcom,set`, `qcom,vdd-io-always-on` and `qcom,vdd-io-lpm-sup` exactly `[U]`.
3. Full Mu-Silicium exit-handler audit for any PMIC/RPM/LDO write beyond the
   three traced modules `[U]`.
4. Consumer inventory for L11/L24 across the vendor tree; this does not
   establish loading or state `[U]`.

### Requires hardware or vendor documentation

1. Direct rail measurement at the XNU handoff: L11/L24 voltage, enable/power-good,
   pad state and clock state `[U]`.
2. Measurements across AP suspend/resume, shutdown and reboot, each transition
   separately `[U]`.
3. PM6125/PMI632 datasheet plus schematic/netlist: absolute maxima, transients,
   back-powering, sequencing and recovery `[U]`.
4. RPM firmware source or vendor documentation of LDO sleep/shutdown/reboot
   policy `[U]`.
5. PMIC register readback of `0x4a46`/`0x5746` and voltage selectors; currently
   blocked by `pmic-audit.md` `[U]`.
6. PMIC USID and subtype for L11/L24; currently blocked by
   `pm6125-usid-research.md` `[U]`.

## 7. Bounded RPM and XBL static trace

This section records a bounded, host-only trace of the captured `rpm.img` and
`xbl.img` bytes. It corrects an earlier table-only reading: the generic `ldoa`
setter and request handler were identified, and the later trace also identifies
the per-instance LDOA parser/aggregator and its apply path. The bounded scan
finds no referenced static table or stored pointer/resolvable PC-relative
literal that selects L11 or L24, and the XBL images touch only `STEP_CTRL`.
None of it is a retention result `[S]`.

### 7.1 RPM image identity and limits

`rpm.img` is a stripped ELF32 ARM executable with two `LOAD` segments (text at
`0x4600000` size `0x27e04` at file offset `0x3000`; RWE data at `0x4690000`
size `0xc4d0` at file offset `0x2ae10`), two `PT_NULL` segments and no section
headers; its SHA-256 is
`272bebd3ddd7ef223d6e95e9b4cd30385a3df0eb4fa7beae6b622f457b4937c6` `[S]`. The
entry is `0x4623e51` (Thumb). The packaged
`analysis/disassembly/rpm.asm.zst` is a different build (T505XXS8CXG1,
2024-07-03) from the captured image (T505NDXS6CXB1, 2024-03-05): its entry is
`0x4623e41`, code is displaced by 8/16 bytes in many regions, and it decodes
everything in ARM mode although the captured entry code is Thumb `[S]`. It is
only a rough layout map; every byte-level claim below comes from capstone
disassembly of the captured bytes `[S]`.

### 7.2 LDOA resource tables

The captured data segment contains `[S]`:

- 24 `ldoa` key records at VA `0x4696994`–`0x4696a54` (file `0x317a4`), 8 bytes
  each: the fourcc `ldoa` plus a little-endian u32 id, in id order 1–24. L11 is
  at `0x46969e4` (id 11) and L24 at `0x4696a4c` (id 24).
- A 24-entry voltage table immediately after them at VA `0x4696a54`, 12 bytes
  per entry (`u32 head; u16 min; u16 max; u32 tail`). Entry *i* structurally
  aligns with `ldoa` id *i+1* `[C]`. L11's entry at `0x4696acc` has head
  `0x62018000`, min 1704, max 1952 and tail `0x00020000`; L24's entry at
  `0x4696b68` has head `0x62156000`, min 2704, max 3600 and tail `0x00020000`.
  The min/max values match the pinned DTS ranges in `rpm-power-interface.md`
  (1.704–1.952 V and 2.704–3.600 V) `[C]`. The `head`/`tail` fields have no
  established semantics `[U]`; the `0x62`/`0x66` bytes are data fields, not
  command IDs `[S]`.
- A NULL-terminated selector array at VA `0x469667c` of 16 stored words that,
  under the link-base-relative interpretation `[C]`, select the `ldoa` key
  records for ids 6, 8, 1, 2, 3, 4, 7, 17, 18, 9, 10, 11, 12, 13, 14 and 16.
  The array contents are `[S]`; it includes L11 (index 11) and excludes L24.
  A whole-image scan finds exactly one stored pointer to the array (the word at
  `0x46966fc`, in a three-entry table at `0x46966fc`–`0x4696720`), and that
  table has no stored or PC-relative referrer, so no referenced stored-pointer
  or resolvable PC-relative-literal chain found by the scan reaches the array
  `[S]`. Computed addressing, runtime relocation or aliasing remains `[U]`.
- A descriptor-shaped record at VA `0x46965c8` and an adjacent small table at
  `0x469661c`; the latter has `ldoa` ids 9, 6, 4 and 18 and `smpa` ids 3 and 5,
  with no L11 or L24 entry `[S]`. The earlier reading of these records stands;
  their semantics are `[U]`.
- A pair array at VA `0x4696c40` whose entries point at the 24-entry voltage
  table, a single-entry table at `0x4696b74`, an 8-entry table at `0x4696b80`,
  a 13-word list at `0x4696c0c` and a header at `0x4696be0` `[S]`. The
  82-word pointer table at `0x4697e20` (NULL at `0x4697f68`) contains these
  pointers. Four aligned words in the image equal its runtime-offset address
  (`0x46006a4`, `0x46221d4`, `0x462227c`, `0x4623ef8`); the two loaded by
  traced code (`0x46221b2`, `0x4622244`) treat it as a memory-region boundary,
  and the other two have no resolved PC-relative loader `[S]`. The voltage
  table's only stored pointer is the descriptor at `0x4696c40`; that
  descriptor's only stored pointer is the word `0x4697f10` inside the same
  boundary table, and no stored word equals `0x4697f10` `[S]`. No referenced
  stored-pointer or resolvable PC-relative-literal chain found by the scan
  therefore reaches the table; computed addressing, runtime relocation or
  aliasing remains `[U]`.

### 7.3 Consumer trace

The trace found the following `[S]`:

- The function at `0x460d024` (literals `0xd0f8`, `0x93de8`, `0x93ec8`,
  `0x93ed0`) is an 8-client, 14-resource, banked 64-bit high-water-mark update
  capped by a per-client limit. It does not reference the LDOA selector array
  or the voltage table, and it is not a regulator/voltage resolver `[S]`.
- The word `0x00092331` at VA `0x46980b4` is loaded by the boot-time function
  at `0x4698074` and used directly as a byte address (no base register added)
  `[S]`. The function is a one-shot init guard, not a selector or policy path.
  No relocation entry for that word was found; whether the runtime loader
  aliases it to the linked data remains `[U]`, so the link-base-relative
  interpretation is still `[C]`.
- The generic key/value handler at `0x4615da4` parses the keys `uv`, `swen`,
  `bobm`, `pcv1`, `pcv2` and `pcv3`. `uv` stores 32-bit microvolts directly in
  a state object; `swen` sets a software-enable bit in that object, not the
  PMIC `ENABLE` register `0x46`; `bobm` is a 3-bit field inserted by the `bfi`
  at `0x4615e4c` `[S]`. There is no selector-to-microvolt conversion and no
  PMIC command ID in this handler `[S]`.
- The class/ID path at `0x4615d08` builds the name `ldoa` plus the class byte
  (0–6) and accepts an 8-bit id. The generic setter at `0x4617b48` compares the
  id against the runtime object's `+8` field and the mode against 4
  (`0x4617b60`, `0x4617b74`) `[S]`. The class table at `0x4695774` is zero in
  the linked image and the object is allocated and filled at runtime by the
  resource database constructor at `0x46996c4` (table store at `0x4699694`), so
  the runtime count equalling the registered 25 is `[C]`, and ids 11 and 24
  passing those bounds is `[C]`. The request path at `0x4616e98` indexes a
  table with the descriptor byte at `+3` plus `0x9f`; in the LDOA aggregator
  the same byte is a domain index checked against 7, so mapping L11 and L24 to
  fixed indices is not established `[U]`.
- The class registration at `0x461746c` records `smpa` with count 9 and type
  `0xc`, `ldoa` with count 25 (`0x19`) and type `0x10`, and `clka` with count
  15 and type `0xc`; the callbacks it installs are the trivial `bx lr` stubs at
  `0x4617468` and `0x461746a` `[S]`. The per-instance `ldoa` builder at
  `0x4698944` replaces those defaults with the parser at `0x46151f8` and the
  aggregator at `0x46153b8` (call site `0x4698ade`, type `0x10`, literals
  `0x151f9` and `0x153b9`) `[S]`. The parser accepts `ocmd`, `tv`, `uv`, `ma`,
  `hr`, `bypa`, `ocen`, `pcmd`, `lsmd`, `pcen` and `swen` and writes them into
  the resource request descriptor `[S]`. The aggregator reads the per-id
  latched state at `[resource+0x14]` (24-byte entries, index `id-1`), compares
  the new aggregate with that state, and calls the apply routine at `0x4615906`
  only when the state changes; `0x4615906` commits through `0x4614920` `[S]`.
  For L11 and L24 the table indices are 10 and 23 `[C]`. The separately traced
  parser/aggregator pair at `0x46166c8`/`0x46168dc` belongs to the `smpa`
  class (type `0xc`), not to `ldoa`; its keys and merge behavior stand as
  observed, and its aggregator reaches the LDOA setter at `0x4615d08` through
  `0x4616e10` for a pending request whose class byte is `0xb` `[S]`, but
  whether that request class is LDOA is `[U]`. The Linux driver writes only the
  active state and no traced path writes an L11/L24 request `[S]`. Whether a
  boot-time or non-AP LDOA request exists, and what the RPM does with its state
  across suspend and shutdown, remains `[U]`.
- The L11 key record at `0x46969e4` has exactly one stored pointer in the whole
  image (`0x46966a8`, the dead selector-array slot); the L24 key record at
  `0x4696a4c` has none `[S]`. No referenced stored-pointer or resolvable
  PC-relative-literal chain found by the scan reaches the 24-entry voltage
  table or the selector array `[S]`, and no traced path writes an L11/L24
  voltage, mode or enable register `[U]`. Under the bounded scan, the paths
  identified that can address either rail are the LDOA apply path
  (`0x46153b8` → `0x4615906` → `0x4614920`) and the class/ID setter path
  (`0x4615d08` → `0x4617b48`), which the `smpa`-class aggregator reaches
  through `0x4616e10` for class byte `0xb` `[S]`. Whether either path is
  driven by a runtime L11/L24 request on this device is not established `[U]`.

The callback site at `0x460cb08` is an indirect vtable call in a descriptor
aggregation loop, not a command ID `[S]`.

### 7.4 XBL boot PMIC settings

The captured `xbl_config.img` (SHA-256
`6dbaad63204b761e7d8fb4dba7c861d97b25dc5094aabc0a9fe45f19fb20d99e`) embeds
`/pmic_settings.bin` at file offset `0x3054`, size `0x7240`, SHA-256
`410e45ac65a7efde3171d819a2fc52d5c9a958fe3240cfd0c0b90229f4e97597` `[S]`. Its
PSI2 record table (16-byte records from `0x120`) contains exactly nine records
that target the L11 peripheral `0x4a` or the L24 peripheral `0x57` `[S]`:

| Record offset | Address | Register |
| --- | --- | --- |
| `0x1cc0` | `0x4a88` | `0x88` |
| `0x1d40` | `0x5788` | `0x88` |
| `0x1e70` | `0x4a60` | `0x60` |
| `0x20f0` | `0x4a39` | `0x39` |
| `0x2100` | `0x4a3a` | `0x3a` |
| `0x2290` | `0x5739` | `0x39` |
| `0x22a0` | `0x573a` | `0x3a` |
| `0x3ce0` | `0x5788` | `0x88` |
| `0x3cf0` | `0x57e6` | `0xe6` |

None is the voltage-range (`0x40`), voltage-set (`0x41`), mode (`0x45`) or
enable (`0x46`) register from `pm6125-register-map.md` `[S]`. An independent
capstone scan of the eight non-blob file-backed `xbl.img` segments decoded
250,865 instructions (289,103 disassembly entries including SKIPDATA padding)
and examined 58,915 memory operands; it found no L11/L24 voltage/enable
immediate, memory displacement or aligned literal `[S]`. The scan used
`AARCH64_OP_IMM` and `AARCH64_OP_MEM` with `detail=True`; an earlier pass
counted SKIPDATA entries as instructions and tested memory operands against the
wrong type constant, so its count is superseded `[S]`. The boot address-split
code at `0xc248144`/`0xc248148` is
`and w22, w21, #0xff` / `ubfx w19, w21, #8, #8`, consistent with the
peripheral/register encoding `[S]`. Two `RSBP` images at `0x4040` (`0x1000`
bytes) and `0x5840` (`0x800` bytes) follow the record stream `[S]`. Type-2
download records at `0x1a0` and `0x3d0` (type field `+8` = 2; word counts
`0x400`/`0x200`, exactly the two images' sizes) call the downloader at
`0xc26379c`, which writes each 4-byte word to SPMI register `0x7048`; the
companion verifier reads back `0x704c`, compares every word, and logs mismatches
as word number/read value/slave id (strings at `0xc22e5fc`/`0xc22e65e`) `[S]`.
Both images are therefore downloaded in full to PMIC PBS RAM and executed by the
PMIC PBS engine, not by the AP `[S]`. The words at `0x4200`/`0x4204` and
`0x4980`/`0x4984` lie inside the first image and encode L24 (`0x57`) and L11
(`0x4a`) accesses to register `0x61` (`STEP_CTRL`); a scan of both images for
the same encoding finds no L11/L24 access to the voltage-range (`0x40`),
voltage-set (`0x41`), mode (`0x45`) or enable (`0x46`) registers, so they are
not evidence of a voltage or enable write `[S]`. The PBS instruction semantics
of the `STEP_CTRL` accesses remain `[U]`. The https://github.com/NoNameBuilder package contains no
`xbl_config` or `pmic_settings` member `[S]`.

### 7.5 Reproduction and limits

The RPM findings come from capstone 6.0.0 on the captured bytes: Thumb mode for
the traced functions (the captured entry is Thumb), with no recorded ARM-mode
decode (the scratch scripts carried an unused ARM fallback; manual fallback use,
if any, is unrecorded `[U]`), `detail=True` for operand inspection, PC-relative
literal loads resolved over both `LOAD` segments, and searches of the text
(`0x4600000`–`0x4627e04`), data (`0x4690000`–`0x469c4d0`) and `PT_NULL` region
(file `0x1000`). The trace did not resolve `movw`/`movt` address construction,
base-plus-offset arithmetic, runtime loader relocation or aliasing, dynamic
resource objects, SPMI/PMIC bus writes, or code outside the two `LOAD`
segments; those remain `[U]`. The scratch scripts were host-only and are not
vendored.

The RPM table, reference and callback-attribution findings are reproducible
with the committed host-only analyzer:

```sh
python3 scripts/analyze-gta4l-rpm-ldoa.py \
    ../../device-dump/private/gta4l-dump/partitions/rpm.img --code
```

It verifies the image SHA-256, the ELF header and the captured `LOAD`/`PT_NULL`
layout, checks the 24 name records, all 16 selector ids and the terminator, the
group table, the L11/L24 voltage rows, the descriptor pair, the 82-word boundary
table and its terminator, the linked-zero class table, and prints every aligned
stored word in a `PT_LOAD` segment that equals an anchor's virtual address,
runtime-offset form or Thumb-tagged form. `PT_NULL` words are reported but are
not scanned. A `reference_count` is a stored-word count, not a control-flow
reachability result.

`--code` decodes only the documented Thumb sites and direct call instructions:
the `bx lr` stubs at `0x4617468`/`0x461746a`; the class registration at
`0x461746c` (`smpa` 9/`0xc`, `ldoa` 25/`0x10`, `clka` 15/`0xc`, Thumb callback
literals `0x17469`/`0x1746b`); the builder literals `0x153b9`/`0x151f9` at
`0x4698af8`/`0x4698afc`; the builder call at `0x4698ade` to `0x460ae14`; and
the direct calls at `0x4615502` (aggregator → apply), `0x4615970` (apply →
commit), `0x4616e8a` (`smpa` bridge → class/ID setter) and `0x4615d7c`
(class/ID setter → generic setter). These are mechanical instruction checks at
documented sites, not proof that a callback executes or that a request exists
at runtime. The self-test covers a wrong hash, a changed name record, a wrong
mapping, a matching word in `PT_NULL`, a Thumb-tagged reference, an altered
callback literal, a class misattribution, an altered call edge, a changed
selector, a changed voltage row and a changed boundary terminator. The captured
image is private and is not vendored; the analyzer's self-test uses a synthetic
image only. The two boundary-table words with no resolved PC-relative loader
are `0x46006a4` and `0x4623ef8` `[S]`.

The XBL scan covered the eight non-blob file-backed `PT_LOAD` segments
(`0xc119000`, `0xc11c000`, `0xc225000`, `0xc27b000`, `0xc29c000`, `0xc350000`,
`0x45e35000`, `0x45ec3000`), excluding the `0x5fc00000` data blob. It decoded
AArch64 with `detail=True` and `skipdata=True`, examined immediate operands and
memory displacements for the L11/L24 register windows, and scanned every
aligned 4-byte literal in those segments. It did not follow indirect calls or
runtime-loaded code, and it does not decode PBS instruction semantics; the PBS
transport is established in §7.4, the instruction semantics remain `[U]`.

```sh
python3 - <<'PY'
import capstone, struct
from capstone.aarch64_const import AARCH64_OP_IMM, AARCH64_OP_MEM
img = open('../../device-dump/private/gta4l-dump/partitions/xbl.img', 'rb').read()
phoff = struct.unpack_from('<Q', img, 32)[0]
entsz, num = struct.unpack_from('<HH', img, 54)
md = capstone.Cs(capstone.CS_ARCH_AARCH64, capstone.CS_MODE_ARM)
md.detail = True
md.skipdata = True
want = set(range(0x4a40, 0x4a47)) | set(range(0x5740, 0x5747))
decoded = mem = hits = lits = 0
for j in range(num):
    typ, flags, off, va, pa, fs, ms, al = struct.unpack_from(
        '<IIQQQQQQ', img, phoff + j * entsz)
    if typ != 1 or not fs or va == 0x5fc00000:
        continue
    code = img[off:off + fs]
    for ins in md.disasm(code, va):
        if ins.id == 0:
            continue
        decoded += 1
        for op in ins.operands:
            if op.type == AARCH64_OP_MEM:
                mem += 1
            if (op.type == AARCH64_OP_IMM and op.imm in want) or (
                    op.type == AARCH64_OP_MEM and op.mem.disp in want):
                hits += 1
    for k in range(0, len(code) - 3, 4):
        if struct.unpack_from('<I', code, k)[0] in want:
            lits += 1
print(decoded, mem, hits, lits)
PY
```

The RPM whole-image pointer and PC-relative literal scans in §7.2–§7.3 have the
same limits: "no referrer" means no stored 4-byte pointer or resolvable
PC-relative literal in the linked image, not that a computed address or a
runtime relocation cannot reach the data `[U]`.

The `/pmic_settings.bin` extraction is reproducible with
`dd if=xbl_config.img of=pmic_settings.bin bs=1 skip=$((0x3054)) count=$((0x7240))`;
the result hashes to `410e45ac...f4e97597`.

### 7.6 What this establishes

The defensible statement is: *no direct write to the L11/L24 voltage, mode or
enable registers was found in the traced RPM paths or the scanned XBL paths; in
the linked RPM image the bounded scan finds no referenced static table or
stored pointer/resolvable PC-relative literal that selects either rail (L11 has
one stored pointer, into an unreferenced selector list, and L24 has none); the
RPM image contains LDOA resource tables that include both rails, but their only
stored reference chain ends in an unreferenced word of a pointer array used as
a heap boundary; and both XBL RSBP images are downloaded in full to PMIC PBS
RAM where they touch only L11/L24 `STEP_CTRL`.* This narrows `E7` ("the exit
path is not passive") but does not establish retention: the RPM paths
identified by the scan are the LDOA apply path and the class/ID setter path,
whose class object and per-id array are built at runtime from the resource
database; the aggregator keeps a latched per-id state, not an enable register,
and whether any L11/L24 request exists is not established; computed
addressing, runtime relocation and aliasing are not excluded; the PBS
instruction semantics and their effect, the effect of the runtime loader, and
the actual rail state across handoff, suspend, shutdown and reboot remain
unresolved `[U]`. A name match, an uninterpreted table or an absent reference
is not a retention result `[S]`.

## 8. Step 4 contract cross-reference

The Step 4 assessment in `docs/sdhc-electrical-contract.md` treats retention as
one of the nine safety-critical contract areas (`E1`–`E9`) for the proposed bus-power-only
candidate. It does not add new retention evidence `[S]`. The status remains:

- no pinned source proves L11/L24 are on at the XNU handoff `[U]`;
- no pinned source proves retention across AP suspend, shutdown or reboot `[U]`;
- no measurement of either rail or the SDC1 pads exists `[U]`.

The Step 6 decision memo records **NO-GO** for SDHC bus-power activation
because this and the other electrical-contract areas remain unresolved. This
document does not promote a hardware completion status `[S]`. The bounded
handoff inventory in `firmware-handoff.md` adds the untraced `ExitBootServices`
callbacks and keeps `E7` open; it does not establish retention. The current-tree
evidence baseline is in `evidence-baseline.md`.

## Safety disposition

No live test is authorized by this document `[S]`. Keep SDHCI bus power and the
card clock off, do not acknowledge `CORE_PWRCTL_STATUS`, do not access PMIC/SPMI
or send RPM votes, and do not scan candidate slaves `[S]`. A software rollback
cannot repair electrical damage `[S]`.
