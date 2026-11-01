# LinuxForGta4l organization source audit

**Status:** host-only, revision-pinned source comparison `[S]`, 2026-11-01.
This audit adds no build, boot, flash, electrical or device result. It does not
change the SDHC decision, the port's CMD7 R1b policy, the identification-only
diagnostic scope or the partition/backup contract. The historical CMD7
missing-CC cause remains unresolved `[U]`.

Tags: `[S]` established by a pinned source read or byte comparison in this
audit; `[C]` conditional inference, with the condition stated; `[U]` not
established by this audit.

## 1. Scope and method

This is the revision-pinned source comparison of the new
`https://github.com/LinuxForGta4l` organization requested before any code,
bootloader or device-tree decision. It covers the eMMC/MMC path and the board
declarations that feed it. It does not evaluate display, touch, USB, audio,
Wi-Fi, DSP, GPU or battery integration except where a storage conclusion needs
a board fact.

Retrieval and comparison method:

- Only read-only GitHub API content requests at immutable SHAs were used. No
  repository was cloned or fetched as a Git repository, no submodule was
  initialized, no `APKBUILD` was sourced, no inspected tree was built or tested,
  no CI was dispatched, no firmware binary was downloaded and no device was
  accessed.
- Files were retrieved as raw bytes into the host cache
  `/tmp/opencode/lfg4l-audit/files/`; the retrieval inventory with repository,
  SHA, path, blob hash, size, SHA-256, license note, variant and retrieval
  status is committed beside this document as
  `docs/linuxforgta4l-org-audit-manifest.tsv` (69 rows, 8 repositories).
- For every manifest row the computed `git hash-object` value equals the API
  blob SHA. Comparisons below use byte-wise `cmp`/`diff` on those cached bytes,
  so they are file-level identities and deltas, not commit-ancestry claims.
- The retained 2026-10-29 `/tmp/opencode/linux-verify/` snapshot was left
  unchanged. This audit adds a new dated source set instead of rewriting the
  old hashes.

Material claims below cite the pinned repository/path and, where useful, the
function or line in the pinned revision. Line numbers are from the pinned
bytes; they are not stable across revisions.

## 2. Stacks and pins (they are not interchangeable)

| Repository | Ref | Pinned SHA | Role in this audit | What it is not |
| --- | --- | --- | --- | --- |
| `LinuxForGta4l/linux` | `gta4l-7.2` | `c91d0e94385f75a8788cfc9d8a4f532b3994e6da` | Mainline 7.2 board/SoC wiring and MMC reference | Not the vendor kernel, not a boot or hardware result |
| `LinuxForGta4l/u-boot` | `gta4l` | `0824f9a3adfdbe2031bac40f0e5eac0891781c4e` | Independent U-Boot MMC/init reference | Not evidence it ran on the owner's tablet |
| `LinuxForGta4l/android_kernel_samsung_sm6115` | `lineage-23.0` | `63b2b11b0b5cffcc1eca07ac48a80ed31d8b75c4` | Downstream Android kernel MMC/board cross-check | Not mainline and not a reproduced build |
| `LinuxForGta4l/postmarketos-packages-gta4l` | downstream | `35b3893a62ea326fe487b0790827fc1a72717f49` | Downstream package source/config pin | Not the kernel source itself |
| `LinuxForGta4l/postmarketos-packages-gta4l` | `mainline` | `d7fa5a483d488c11a7b4b589b9cc0fda1ed4cf0c` | Mainline 6.10 package/boot metadata | Not Linux 7.2; not a device result |
| `LinuxForGta4l/uniLoader` | `master` | `12a3d83a2334b559ec86208be72964c2770ba35b` | Minimal loader board file, framebuffer only | Not a storage driver or boot proof |
| `LinuxForGta4l/dtbo-lk2nd` | `main` | `8a0fa8a96cc9db19d2968a89d0bd57d1aa7d0bae` | Board-ID overlay convention | Not a complete board description or replacement authorization |
| `LinuxForGta4l/firmware-samsung-gta4l` | `main` | `4e76b4b64b081cdc19cfdd866fa41bd26e84e4e8` | Firmware filename/provenance lead (metadata only) | Binaries were not downloaded; proprietary |
| `limonabisi/linux-gta4l` | `gta4l-7.2` | `cf5f763f0ef8a5b4d8bb4163833cba1160c51894` | Pre-existing, already-used local Linux reference | Not a Git ancestor of the organization tree |
| `LineageOS/android_kernel_samsung_sm6115` | pinned by postmarketOS | `381a5549ee66fa751f632ee0840f5631274209ee` | Downstream kernel version that postmarketOS actually packages | Not the organization's downstream HEAD |
| `LineageOS/android_kernel_samsung_sm6115` | branch tip used for delta | `cbd2ed85b0262bbcafaa9e0cc6a979e1eaa2c1d4` | Same-generation comparison point for 63b2 | Not the postmarketOS pin |

The organization holds at least five distinct stacks: downstream Android,
mainline 6.10 packaging, a Linux 7.2 tree, U-Boot adaptations and a minimal
loader. They do not share a kernel version, a board description or a build
system `[S]`. A fact from one stack is not transferable to another without the
per-file comparison recorded here. In particular:

- Mainline 7.2, U-Boot and the downstream Android kernel are separate code
  bases. Only same-name file deltas are recorded; no cross-version equivalence
  is claimed.
- `limonabisi/linux-gta4l` at `cf5f763f` declares Linux 7.2.0 and its MMC stack
  is byte-identical to `LinuxForGta4l/linux` at `c91d0e94` `[S]`. That is file
  identity, not established Git ancestry; the previous planning review's
  compare request returned 404 `[U]`.
- The postmarketOS downstream package pins LineageOS `381a5549`, not the
  organization's `63b2b11b`, and its mainline package pins a separate 6.10 tree
  and a local DTS patch `[S]`.

## 3. Linux 7.2 reference (`c91d0e94`)

### 3.1 Identity and retained snapshot

`Makefile` declares `VERSION=7`, `PATCHLEVEL=2`, `SUBLEVEL=0` `[S]`. The board
DTS and DTSI carry `Copyright (c) 2026 V6lhost <v6lhost@proton.me>` under
`GPL-2.0+ OR BSD-3-Clause` `[S]`.

The retained `/tmp/opencode/linux-verify/` board DTS is byte-identical to the
`limonabisi/linux-gta4l` `cf5f763f` board DTS `[S]`. Against the organization
tree, the six inspected MMC files and `sm6115.dtsi` are byte-identical `[S]`:

| File | Result at `c91d0e94` vs `cf5f763f` |
| --- | --- |
| `drivers/mmc/core/host.c` | identical |
| `drivers/mmc/core/core.c` | identical |
| `drivers/mmc/core/mmc.c` | identical |
| `drivers/mmc/core/mmc_ops.c` | identical |
| `drivers/mmc/host/sdhci.c` | identical |
| `drivers/mmc/host/sdhci-msm.c` | identical |
| `arch/arm64/boot/dts/qcom/sm6115.dtsi` | identical |

The board DTS differs only in these items `[S]`:

- the organization tree removes the `lpasscc`, `q6afe` and `q6asm` includes
  used by the retained sound nodes;
- `qcom,msm-id` is only `<0x1bc 0x10000>` (SM6115), dropping the retained
  `<0x1a1 0x10000>` SM4250 entry; `qcom,board-id` stays `<0x1000b 0x00>`;
- the retained `battery` node is removed.

Those differences are outside the storage path. They are recorded so the two
7.2 trees are not described as identical `[S]`.

### 3.2 Board storage declarations

- `&sdhc_1` is `non-removable; status = "okay";` with no `vmmc-supply` or
  `vqmmc-supply` (`sm6115-samsung-gta4l.dts:367`) `[S]`. This is the eMMC
  controller. The absence of supplies is not evidence that L24/L11 are absent
  from the board; it changes what the Linux driver derives at probe.
- `&sdhc_2` keeps `vmmc-supply = <&pm6125_l22a>`,
  `vqmmc-supply = <&pm6125_l5a>` and `cd-gpios = <&tlmm 88
  GPIO_ACTIVE_HIGH>` `[S]`. These are removable-card rails and must not be
  conflated with the eMMC L24/L11 rails `[S]`.
- The SoC node is `compatible = "qcom,sm6115-sdhci", "qcom,sdhci-msm-v5"`
  (`sm6115.dtsi:1099-1103`) `[S]`, matching the port's v5 register layout.

### 3.3 Initialization behavior used by the port

- `mmc_go_idle()` sets chip-select high, delays 1 ms, sends CMD0 (no response),
  releases chip-select and delays 1 ms; `__mmc_go_idle()` also delays 1 ms after
  the command (`drivers/mmc/core/mmc_ops.c:147-190`) `[S]`. The port's
  `CMD0_DELAY_US = 1000` and `CMD0_POST_DELAY_US = 2000` preserve the combined
  pre-command and post-command bounds (`package/Gta4lEmmc.c:1440-1457`,
  `package/Gta4lEmmc.h:121-122`).
- CMD1 polling in 7.2 uses `__mmc_poll_for_busy()` with a 4 ms period and a
  1 s timeout (`mmc_ops.c:24-25`, `:224-247`); acceptance is OCR bit 31 set
  (`MMC_CARD_BUSY`) `[S]`.
- CMD2 and CMD9 use R2 long responses with CRC; CMD3 uses `RSP_R1 | AC` with
  `card->rca = 1`; CMD7 uses `RSP_R1 | AC` with `card->rca << 16`
  (`mmc_ops.c:98-114`, `:257-265`; `mmc.c:1684`, `:1698-1742`) `[S]`. The
  initialized order is CMD2, CMD3, CMD9, CMD7, then EXT_CSD `[S]`.
- `mmc_select_card()` is the selected-card transition; the port's `DEFAULT_RCA`
  of 1 and its CMD3/R1 parsing match the mainline assignment `[S]`.

### 3.4 Pad, POR and low-speed controller policy

- `sdhci_msm_probe()` writes `CORE_VENDOR_SPEC_POR_VAL` (`0xa9c`) to
  `CORE_VENDOR_SPEC` (`drivers/mmc/host/sdhci-msm.c:76`, `:2779`) `[S]`.
- `sdhci_msm_set_regulator_caps()` changes the two pad-power bits only when
  `caps != 0`, which requires a usable `vqmmc` supply and a supported 1.8 V or
  3.0 V range (`sdhci-msm.c:2384-2427`) `[S]`. The board declares no `vqmmc`
  for `sdhc_1`, so `caps == 0`; the pad bits remain as the probe wrote them,
  i.e. bit 15 (`CORE_IO_PAD_PWR_SWITCH_EN`) and bit 16
  (`CORE_IO_PAD_PWR_SWITCH`) both clear `[S]`. This corroborates
  `docs/linux-regulator-lifecycle.md:182-191`.
- The power-IRQ pad update is likewise gated on `caps_0 & CORE_VOLT_SUPPORT`
  (`sdhci-msm.c:1780-1812`) `[S]`. With no `vqmmc`, neither path re-enables a
  pad bit.
- At `clock <= 100 MHz`, `sdhci_msm_set_uhs_signaling()` sets `CORE_DLL_RST`
  and `CORE_DLL_PDN` because the feedback clock is used and tuning is skipped
  (`sdhci-msm.c:1393-1412`) `[S]`. `msm_hc_select_default()` clears
  `CORE_PWRSAVE_DLL`, selects the default MCLK and clears the `HC_SELECT_IN`
  override (`sdhci-msm.c:803-840`) `[S]`. `msm_init_cm_dll()` clears
  `CORE_CLK_PWRSAVE` while keeping the clock alive (`:648-671`) `[S]`.
- `CORE_PWRSAVE_DLL` is set again only for controllers that are not
  `use_14lpp_dll_reset` (`:1078-1090`) `[S]`; the port's low-speed path clears
  the corresponding `VENDOR3_PWRSAVE_DLL` bit instead
  (`package/Gta4lEmmc.c:1098-1130`), matching the 7.2 low-speed state `[S]`.

## 4. Downstream Android reference (`63b2b11b`) and its neighbours

### 4.1 File-level deltas

`63b2b11b` was compared with the same-generation LineageOS tip
`cbd2ed85b0262bbcafaa9e0cc6a979e1eaa2c1d4` `[S]`:

| File | Comparison | Result |
| --- | --- | --- |
| `drivers/mmc/host/sdhci-msm.c` | byte | identical (blob `4ca1b5c4…`, 189,739 bytes) |
| `drivers/mmc/core/mmc_ops.c` | byte | identical (blob `1c9382e6…`, 24,496 bytes) |
| `drivers/mmc/core/host.c` | byte | identical (blob `b1a09fb5…`, 19,764 bytes) |
| `drivers/mmc/core/mmc.c` | diff | 4 changed lines (`mmc_set_driver_strength`: 63b2 drops the `fixed_drv_type` branch) |
| `drivers/mmc/core/core.c` | diff | 14 changed lines (63b2 has no SD-Express path; uses `mmc_send_if_cond` instead of `mmc_send_if_cond_pcie`) |
| `drivers/mmc/host/sdhci.c` | diff | 1 changed line (63b2 drops the redundant `start_signal_voltage_switch()` call in `sdhci_reinit`) |

The postmarketOS pin `381a5549` differs from `63b2b11b` in `mmc_ops.c` `[S]`:
63b2 makes `mmc_poll_for_busy()` non-static and exports it with
`EXPORT_SYMBOL_GPL`, and it relaxes `mmc_flush_cache()` to test
`mmc_cache_enabled(host)` without the MMC-card and cache-disable-quirk guards.
Those are downstream API/behaviour changes; neither is part of the port's
identification path `[S]`.

The organization's downstream `sdhci-msm.c` and `mmc_ops.c` are therefore the
same bytes as the LineageOS tip in the MMC area, and the differences from the
postmarketOS-pinned tree are small and enumerated `[S]`. This does not
establish that the two kernels share a Git ancestor or that a build of either
has run on this tablet `[U]`.

### 4.2 Downstream MMC semantics

- CMD3 is `RSP_R1 | AC` with the host-assigned RCA; CMD7 is `RSP_R1 | AC` with
  `card->rca << 16` (`drivers/mmc/core/mmc_ops.c:90-105`, `:225-234`) `[S]`.
  `card->rca = 1` (`drivers/mmc/core/mmc.c:2135`) `[S]`.
- CMD1 is a 100-iteration loop with a 10 ms delay and a 1 s class budget; after
  a successful CMD1 the downstream code delays 8 µs before the next command
  (`mmc_ops.c:175-222`) `[S]`. The 8 µs is present in both `381a5549` and
  `63b2b11b` and absent from mainline 7.2 `[S]`. Its comment says the internal
  CRC error flag needs about 3 MCLK (about 7.5 µs at 400 kHz) after a clear
  before CRC checking is re-enabled; enabling CRC check earlier can raise the
  CRC interrupt on the following command `[S]`. This is recorded as a
  downstream-specific timing guard. It is not established as the cause of the
  historical CMD2 CRC, and the current-source `cmd2window` run passed CMD2
  (`docs/emmc-cmd2-crc-audit.md` §2.7) `[S]/[U]`.
- Downstream `CORE_VENDOR_SPEC_POR_VAL` is `0xA1C`, not mainline/U-Boot
  `0xa9c` (`sdhci-msm.c:102`) `[S]`. At probe it writes `0xA1C`
  (`:5726-5733`), then sets `CORE_VENDOR_SPEC` bit 7 (ADMA error-size enable)
  (`:5734-5738`) and sets `CORE_IO_PAD_PWR_SWITCH_EN` (bit 15) while leaving
  bit 16 under later control (`:5761-5767`) `[S]`. A card-detect-driven
  block toggles bit 16 for removable slots (`sdhci_msm_vbias_bypass_wa()`,
  `:3138-3172`) `[S]`. Both POR values clear bits 15 and 16; the difference is
  bit 7, which downstream sets explicitly `[S]`.
- `sdhci_msm_clear_pwrctl_status()` retries the status clear up to 10 times
  with a 10 µs delay and dumps the power registers on exhaustion
  (`sdhci-msm.c:3092-3136`) `[S]`. The port has its own bounded clear/ack loop
  and does not copy the dump `[S]`.

### 4.3 Configuration scope

The fetched `arch/arm64/configs/vendor/gta4l-common.config` is a fragment and
contains no `CONFIG_MMC*` lines `[S]`. The effective `63b2b11b` defconfig was
not retrieved, so its MMC/CQ/ICE selection is `[U]`. The LineageOS
`381a5549` `gta4l_eur_open_defconfig` enables `CONFIG_MMC`,
`CONFIG_MMC_SDHCI`, `CONFIG_MMC_SDHCI_PLTFM`, `CONFIG_MMC_SDHCI_MSM` and the
CQHCI crypto options `[S]`; that is the postmarketOS-pinned configuration, not
63b2's.

## 5. U-Boot reference (`0824f9a3`)

- `drivers/mmc/msm_sdhci.c` defines `CORE_VENDOR_SPEC_POR_VAL 0xa9c` and
  writes it at init (`:36`, `:123-124`) `[S]`. The 363-line driver contains no
  pad-power-bit or power-status-request code `[S]`; it depends on the POR
  value and controller defaults for that state.
- `mmc_go_idle()` delays 1 ms before CMD0 and 2 ms after it
  (`drivers/mmc/mmc.c:563-580`) `[S]`. This is the only timing difference from
  Linux's two 1 ms windows and the port's 1 ms + 2 ms bounds `[S]`.
- CMD1 is retried until OCR bit 31 is set with a 100 µs poll delay and a 1 s
  timeout (`mmc.c:762-788`) `[S]`. CMD2 uses R2 with its
  `MMC_QUIRK_RETRY_SEND_CID` path (4 attempts) `[S]`. CMD3 uses the generic
  `SD_CMD_SEND_RELATIVE_ADDR` with `MMC_RSP_R6` for both SD and MMC
  (`mmc.c:2580-2599`) `[S]`; that response mask is a U-Boot generic-path
  choice, not a different eMMC CMD3 protocol. CMD7 is `MMC_RSP_R1`
  (`mmc.c:2687-2694`) `[S]`.
- The U-Boot DTS enables HS200 on SDHC1 and carries SD-root bootargs
  `[S]`. Neither belongs in the current identification-only diagnostic, and
  the plan explicitly excludes copying them `[S]`.
- `mmc_send_init_stream` exists in this tree `[S]`; it is not part of the
  port's CMD0/1/2/3/9/7 identification sequence and no use is proposed.

## 6. Command/response matrix

The port's behavior is from `package/Gta4lEmmc.c`; references are as pinned
above. "Port" means the current identification path, not a future data path.

| Step | Linux 7.2 `c91d0e94` | Downstream `63b2b11b` | U-Boot `0824f9a3` | Port | Assessment |
| --- | --- | --- | --- | --- | --- |
| CMD0 | BC, no response; 1 ms pre, 1 ms post (`mmc_ops.c:147-190`) | same | 1 ms pre, 2 ms post (`mmc.c:563-580`) | 1 ms pre, CMD0, 2 ms post (`Gta4lEmmc.c:1440-1457`) | Equivalent bounds; no defect `[S]` |
| CMD1 | R3; 4 ms poll, 1 s budget, accept OCR bit 31 (`mmc_ops.c:24-25`, `:224-247`) | R3; 10 ms poll, 100 tries, accept bit 31, 8 µs post (`mmc_ops.c:175-222`) | R3; 100 µs poll, 1 s budget, accept bit 31 (`mmc.c:762-788`) | R3; 20 attempts × 100 ms, accept bit 31, timeouts retired per attempt (`Gta4lEmmc.c:1296-1333`) | Same acceptance condition; port budget is wider (2 s) `[S]`; no change proposed |
| CMD2 | R2 + CRC, retries 3 (`mmc_ops.c:269-283`, `:370-376`) | R2 + CRC, retries 3 | R2 + CRC, quirk retry 4 (`mmc.c:2563-2575`) | R2 + CRC, one attempt, response validated before use (`Gta4lEmmc.c:1462-1483`) | Equivalent encoding/validation; port is fail-stop `[S]` |
| CMD3 | R1 + CRC/AC, RCA 1, retries 3 (`mmc_ops.c:257-265`) | same (`mmc_ops.c:225-234`) | R6 mask, RCA from caller (`mmc.c:2578-2601`) | R1 + CRC/index, RCA 1, status errors checked (`Gta4lEmmc.c:1485-1498`) | Port matches Linux/downstream semantics `[S]`; U-Boot mask noted, no change |
| CMD9 | R2 + CRC, retries 3 | same | R2 + CRC | R2 + CRC, validated (`Gta4lEmmc.c:1500-1513`) | Equivalent `[S]` |
| CMD7 | R1 + AC, retries 3 (`mmc_ops.c:98-114`) | R1 + AC, retries 3 | R1 (`mmc.c:2685-2694`) | R1b (short-busy), CC + TC in one shared budget, response snapshot after CC (`Gta4lEmmc.c:1515-1518`) | **Intentional port divergence** `[S]`; see §6.1 |
| CMD13 | R1 + CRC, status poll | R1 + CRC, status poll | status poll | R1 + CRC/index, require TRAN + ready-for-data (`Gta4lEmmc.c:1360-1389`) | Port uses a stricter state acceptance after CMD7 `[S]`; no reference contradiction |

### 6.1 The CMD7 policy difference

All three inspected Linux/U-Boot stacks select CMD7 as an R1 command; the port
deliberately uses R1b, requires both CC and TC in one completion budget and
snapshots the response only after CC `[S]`. The focused decode is
`docs/emmc-cmd7-timeout-finding.md`; the Linux-side selection is also recorded
in `docs/pe-storage-stack-provenance.md` §8.1 `[S]`.

This audit found no reference source that contradicts the port's separate
CC/TC requirement as a diagnostic policy, and no reference source that
explains the historical missing-CC timeout `[U]`. Because the plan preserves
the R1b policy in this phase, no change is proposed. In particular, switching
to R1, accepting TC-only completion, suppressing the index/CRC error or
inflating the timeout would hide the unanswered question instead of answering
it `[S]`.

## 7. Board-level corroboration for the port

- **POR value.** Mainline 7.2 and U-Boot both write `0xa9c`; downstream writes
  `0xA1C` and then sets bit 7. All three clear the pad bits 15/16 at that write
  `[S]`. The port writes `GTA4L_EMMC_VENDOR_SPEC_POR = 0x00000A9C`
  (`package/Gta4lEmmc.c:1210-1214`, `package/Gta4lEmmc.h:97-99`), matching
  mainline and U-Boot `[S]`.
- **Pad policy.** The port then sets both pad bits because the board's VCCQ is
  a fixed 1.8 V rail (`package/Gta4lEmmcProbe.c:639-646`,
  `package/Gta4lEmmc.c:1246-1260`) `[S]`. This is a deliberate board policy,
  documented as a departure from the DT-derived Linux state in
  `docs/emmc-driver.md` and `docs/linux-regulator-lifecycle.md`. It is
  corroborated by the stage-2 hardware report `ven=00318a0c`, which has bits 15
  and 16 set `[S]` (the report itself is hardware evidence in
  `docs/emmc-driver.md`; this audit adds no run).
- **DLL/clock.** The port's low-speed setup clears `VENDOR3_PWRSAVE_DLL`, sets
  `DLL_RST`, then `DLL_PDN`, selects the default MCLK, clears the
  `HC_SELECT_IN` override and clears `CLK_PWRSAVE`
  (`package/Gta4lEmmc.c:1098-1130`) `[S]`. That matches the mainline 7.2
  low-speed state described in §3.4 `[S]`. The vendor UEFI's `DLL_RST|DLL_PDN`
  pair is separate captured-firmware evidence, not a claim about this U-Boot
  tree `[S]/[U]`.
- **Power-status handshake.** The port handles BUS_ON/BUS_OFF and IO_LOW/IO_HIGH
  as independent requests, requires an observed BUS_ON and the latest
  acknowledged IO level, and uses a 500 ms `POWER_TIMEOUT_US`
  (`package/Gta4lEmmc.c:963-1089`, `package/Gta4lEmmc.h:195`) `[S]`. Downstream
  retries the PWRCTL clear 10 times; Linux services the same requests through
  its power IRQ `[S]`. The port's separate-request and last-level rules are its
  own stricter implementation, already audited in `docs/emmc-driver.md`; this
  audit found no reference-level contradiction `[S]`.
- **CMD1-to-CMD2 gap.** The only reference timing guard not present in the port
  is the downstream 8 µs post-CMD1 delay described in §4.2 `[S]`. The port
  returns from `gta4l_emmc_card_ready()` and issues CMD2 immediately
  (`package/Gta4lEmmc.c:1296-1333`, `:1462-1483`). This is recorded for the
  capture proposal as a question, not as a patch; mainline 7.2 and U-Boot do
  not have it either `[S]`.

## 8. What this audit does not establish

- **Ancestry.** File identity between the organization tree and
  `limonabisi/linux-gta4l`, or between `63b2b11b` and `cbd2ed85`, is not a
  merge-base or fork relationship `[U]`. The earlier compare request failed
  with 404; no new ancestry claim is made.
- **Variant identity.** Every storage source inspected is for the SM6115/gta4l
  family; none establishes an SM-T505N-specific difference from the SM-T505
  family. The owner unit's identity remains the locally captured evidence, not
  these sources `[U]`.
- **Effective build configuration.** The `63b2b11b` effective defconfig and any
  Samsung overlay include roots were not resolved `[U]`. The U-Boot
  `qcom_defconfig`/`debug-sm6115.config` fragments were retrieved but their
  final selection was not reproduced `[U]`.
- **Build or boot.** None of the inspected trees was built, linked or booted
  here `[U]`. No claim that any of them works on the owner's tablet is made.
- **Firmware and proprietary blobs.** `firmware-samsung-gta4l` is recorded as a
  metadata lead only. No binary was downloaded, and public hosting does not
  establish redistribution permission `[S]/[U]`.
- **License reuse.** The manifest records repository- or file-header-level
  license notes. It is not a per-file compatibility review; DTS files declare
  `GPL-2.0+ OR BSD-3-Clause`, kernel and bootloader code is GPL-family, and
  firmware is proprietary. No code is copied by this audit `[S]`.
- **Non-storage subsystems.** Display/touch panel variants, USB PHY, PMIC
  GPIO keys, audio/DSP, Wi-Fi and GPU differences seen in the organization
  trees are outside this audit and remain later, separately reviewed work
  `[U]`.
- **Historical failure cause.** Nothing here explains the `fmt=2` CMD7
  missing-CC timeout. The nearest source is a reconstruction, not the exact
  historical build `[U]`.

## 9. Conclusions and permitted next step

Per the source-review outcome categories:

1. **Already equivalent:** CMD0 timing bounds, CMD1 acceptance condition, CMD2
   encoding/validation, CMD3 R1 semantics and RCA assignment, CMD9, the
   CMD2→CMD3→CMD9→CMD7 order, response capture after completion, the POR value
   used by mainline/U-Boot, and the low-speed DLL/MCLK state `[S]`.
2. **Real host-code defect:** none established by this audit `[U]`.
3. **Additional instrumentation needed:** yes — the current `fmt=4` stage-3
   report is the right vehicle for the remaining CMD7 CC/TC question. The
   bounded proposal belongs in a separate lane (the plan's Priority 2) and
   needs separate written owner approval before any image construction,
   flashing or boot `[S]`.
4. **Board-variant mismatch:** none established for the storage path `[U]`.
5. **Unsupported inference:** none adopted. In particular, the port's
   pre-existing R1b policy, 2 s CMD1 budget, single-attempt fail-stop behavior
   and fixed-1.8 V pad policy are recorded as differences, not defects `[S]`.

No code change, bootloader adoption, board-DTS replacement or peripheral
enablement follows from this audit. The organization's trees are reference
material only; the port keeps its own board description, packaging contract and
diagnostic scope `[S]`.

## Appendix A. Retrieval manifest

`docs/linuxforgta4l-org-audit-manifest.tsv` lists all 69 retrieved files with:

| Column | Meaning |
| --- | --- |
| `repo` | GitHub `owner/name` |
| `ref` | Immutable commit SHA actually requested |
| `path` | Repository-relative path |
| `git_blob_sha` | Computed blob hash; equals the API blob SHA on every row |
| `size` | Byte size |
| `sha256` | SHA-256 of the retrieved bytes |
| `license` | Repository/file-header note, not a per-file reuse review |
| `variant` | Applicable stack/variant, as far as the source establishes |
| `retrieval` | Read-only GitHub raw API read on 2026-11-01 |

The host cache path is `/tmp/opencode/lfg4l-audit/`; it is transient. The TSV
is the durable inventory. Counts: 69 files, 8 repositories. The files support
the claims above; files retrieved only to resolve include roots or packaging
metadata are included without being treated as hardware evidence `[S]`.
