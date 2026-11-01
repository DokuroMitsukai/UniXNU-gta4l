# PixelExperience gta4l storage-stack provenance

Status: host-only source and artifact review `[S]`. It adds no hardware run,
image build, flash, boot, measurement or XNU result. The eMMC bring-up remains
unproven and the historical CMD7 timeout cause remains unresolved `[U]`.

Tags: `[S]` source-, artifact- or source-diff-established; `[C]` conditional
inference (condition stated); `[U]` not established by this review.

The release is a **PixelExperience Plus 11 (Android 11)** build for
`gta4l`/SM-T505. The release notes require the latest official Android 12
firmware first and report that only the SM-T505 PE Plus build was tested on a
device; the owner tablet is an SM-T505N. A device-tested SM-T505 build does not
by itself establish SM-T505N identity or compatibility `[S]/[U]`.

## 1. Retrieval and identity

Retrieved on 2026-11-01 into the host cache `/tmp/opencode/pe-artifact/`. No
installer, script, ROM or extracted binary was executed.

| Item | Value |
| --- | --- |
| Release page | `https://github.com/dolbaras/gta4l-gta4lwifi-roms/releases/tag/2026-10-07` |
| Release tag commit | `21335db246d6f9e5a2b09ff8d8fcc1d8f38a8f4e` |
| Asset | `PixelExperience_Plus_gta4l-11.0-20261007-1420-UNOFFICIAL.zip` |
| Size | 1,420,855,324 bytes (matches the published asset size) `[S]` |
| Published MD5 in the release table | `6aa86a41ecef08ce4157ccc1d379631c` |
| Local MD5 | `6aa86a41ecef08ce4157ccc1d379631c` `[S]` |
| Published SHA-256 (GitHub release asset digest) | `3adbec9c4066b0efe571274346c710162d5692dd184e513d7cb5276379cc7605` |
| Local SHA-256 | `3adbec9c4066b0efe571274346c710162d5692dd184e513d7cb5276379cc7605` `[S]` |

The local file is byte-identical to the release asset: the GitHub release API
lists `sha256:3adbec9c...` for this asset, and the local file has the same
SHA-256, the same size and the release-table MD5. That establishes byte
identity against the published asset and its digests, not source
reproducibility, device compatibility, safety or trustworthiness `[S]/[U]`.

The build manifest is `snippets/gta4l.xml` in `dolbaras/gta4l-pe-plus` at
`7076c856b71803f09cc24a859c4d4132b09eed15`. It selects device trees on
`eleven-plus`, the common/vendor trees and the kernel on `lineage-18.1`, and
LineageOS `hardware/samsung` and PE-archive `external/faceunlock`. It contains
**no commit revisions**; every entry is a branch name `[S]`. The branches are
mutable and were not pinned by the release.

| Repository | Ref | Observed head (2026-11-01) | Use |
| --- | --- | --- | --- |
| `dolbaras/android_kernel_samsung_sm6115` | `lineage-18.1` | `5a3272a3e1de7dcc68ad452ca2d36e6c3bbb594a` | Storage kernel source anchor |
| `dolbaras/android_device_samsung_gta4l` | `eleven-plus` | `3276f5b69e92dd9f609ee29ea7bec31c7db6fc98` | Board overlay/build config |
| `dolbaras/android_device_samsung_gta4l-common` | `lineage-18.1` | `3f21d1579bc3f41afd5687d4d71843b22b528249` | Common build config |
| `dolbaras/android_device_samsung_gta4lwifi` | `eleven-plus` | `24485fa933bc5c9c6db49d98b361897d08c3ba73` | Wi-Fi sibling; not this artifact |

The four cached clones are shallow: `git rev-parse --is-shallow-repository`
reports `true` for each. They cannot establish repository history. The observed
head message `Add README` and timestamp 2026-10-07 19:30 +0300 are reported
clone metadata, not an import history. These heads are inspection anchors,
**not** proven release build revisions `[U]`. The artifact reports a 2026-10-06
build time, one day before the observed heads; that ordering is a provenance
mismatch, but it is not by itself proof that the release used different code
`[C]`.

## 2. Artifact inventory

| Artifact | Size | SHA-256 | Note |
| --- | --- | --- | --- |
| `boot.img` | 100,663,296 | `8b19ade181f62ca2786881dfbb9d01e19b723e208d00ab310ef7253e03880d22` | Android boot header v2 |
| `kernel.gz` (boot kernel) | 15,144,947 | `eb3cb68f80646ce0d573e3a29e85ed34cb556b232edfdacc410d1d4f1dc558da` | Matches `kernel_size` |
| `Image` (gunzipped) | 33,939,968 | `59b1f22091610aeb3b69eb7a25abac11343dcf1073aa5b8b9fb26e6697d11614` | AArch64 kernel image |
| `ramdisk` | 680,425 | `e3809019bb3eb39ae08e6eede76ec53ba3877d7ecc2bbdf9d8247ac1a03a945f` | Matches `ramdisk_size` |
| `dtb.img` (boot dtb section) | 3,950,482 | `32414e6f43b5a0bd42b1fb3cd20436f1e16095d4390bdaed151f3fe995b4112b` | 12 concatenated FDTs; size matches `dtb_size` |
| `dtbo.img` | 25,165,824 | `430361292528202a0d4ea098d3c5b1b7cca209c4e60057201f49147dc3a69e7b` | dtbo table; 40 entries, 4,474,460-byte table total |
| `kernel.config` (IKCONFIG) | 154,244 | `0dff1387890d6bf08b021865e9612c38a259b209efc7d28f81ed38c982502280` | Extracted from `Image` |
| `dynamic_partitions_op_list` | 843 | — | `super` group maximum 5,750,390,784 bytes |
| `vendor.transfer.list` | 2,444 | — | v4 transfer list |

The `Image` embeds the gzip config between `IKCFG_ST` at offset 18,860,672 and
`IKCFG_ED` at 18,895,478 `[S]`. Key rows are `CONFIG_MMC=y`,
`CONFIG_MMC_SDHCI_MSM=y`, `CONFIG_MMC_CQHCI=y`,
`CONFIG_MMC_CQHCI_CRYPTO=y`, `CONFIG_MMC_CQHCI_CRYPTO_QTI=y`,
`CONFIG_GTA4L_DTB=y`, `CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE=y` and
`CONFIG_CMDLINE="cgroup_disable=pressure"`.

Boot header v2 fields: page size 4096, board name `SRPTC24A004`, OS version
`0x1600015c` (Android 11), kernel address `0x8000`, ramdisk address
`0x20000000`, tags `0x1e00000`, dtb address `0x1f00000` and no second stage or
recovery dtbo `[S]`. The board name matches
`dev-gta4l/BoardConfig.mk` (`TARGET_BOOTIMAGE_BOARD_NAME := SRPTC24A004`,
`BOARD_MKBOOTIMG_ARGS += --board ...`) at the pinned anchor `[S]`.

The boot cmdline is the `BoardConfigCommon.mk` board cmdline
(`console=ttyMSM0,115200n8 earlycon=msm_geni_serial,0x4a90000 ...`) plus
`buildvariant=user`; it is not the kernel `CONFIG_CMDLINE` above `[S]`.

## 3. Source-to-artifact correspondence

The artifact's kernel string is:

```text
Linux version 4.19.300-perf+ (nobody@android-build) ... #3 SMP PREEMPT
Tue Oct 6 16:51:01 EEST 2026
```

The pinned kernel anchor is Linux 4.19.300 (`Makefile` `SUBLEVEL = 300`) and
its `gta4l_eur_open_defconfig` sets `CONFIG_LOCALVERSION="-perf"`,
`CONFIG_CMDLINE="cgroup_disable=pressure"`, the same SDHCI/CQHCI options and
`CONFIG_GTA4L_DTB=y` `[S]`. Those are consistency checks, not proof of a build:
the artifact claims a 2026-10-06 build while every pinned repository head was
committed on 2026-10-07. **Exact source-to-artifact identity is unresolved**
`[U]`. The source anchors are useful and internally consistent, but the release
cannot be presented as reproduced from those commit hashes.

## 4. Storage node provenance

The boot dtb section splits into 12 FDTs (`dtbs/00.dtb`..`11.dtb`). The first
is the Bengal base with `model = "Qualcomm Technologies, Inc. Bengal SoC"`,
`compatible = "qcom,bengal"`, `qcom,board-id = <0 0>`,
`qcom,msm-id = <417 65536 444 65536>` and `sdhci@4744000` `status =
"disabled"` `[S]`.

The dtbo table splits into 40 overlays (`dtbos/00-...dtbo`..`39-...dtbo`).
Entry 00 declares `model = "Qualcomm Technologies, Inc. Bengal QRD"`,
`compatible = "qcom,bengal-qrd\0qcom,bengal\0qcom,qrd"`,
`qcom,board-id = <65547 0>` and no `qcom,msm-id`. Its `fragment@47` (of 367)
targets `sdhc_1` and enables the controller; the overlay source also carries
the `sdhc_1` alias and the L24A/L11A supply mapping `[S]`.

Applying entry 00 to the base FDT with `fdtoverlay` yields an
`sdhci@4744000` node whose property set is **identical** to the captured live
FDT node, with only property ordering different `[S]`. Selected properties:

| Property | Value |
| --- | --- |
| `compatible` | `qcom,sdhci-msm-v5`, `qcom,sdhci-msm-cqe` |
| `reg` / `reg-names` | `0x4744000+0x1000`, `0x4745000+0x1000`, `0x4748000+0x8000` |
| `clocks` / `clock-names` | `iface_clk`, `core_clk`, `ice_core_clk` |
| `resets` / `reset-names` | `core_reset` |
| `qcom,bus-width` / `qcom,nonremovable` | 8 / present |
| `vdd-supply` | L24A (live phandle 503), 2,960,000 uV, 570,000 uA max |
| `vdd-io-supply` | L11A (live phandle 496), 1,800,000 uV, 325,000 uA max |
| `vdd-io` flags | `qcom,vdd-io-always-on`, `qcom,vdd-io-lpm-sup` |
| `qcom,clk-rates` | `400000 20000000 25000000 50000000 100000000 192000000 384000000` |
| `pinctrl-0` / `pinctrl-1` | four active pins, four sleep pins |
| `status` | `ok` after the overlay |

The live FDT was captured from recovery and is recorded as byte-identical to
the pinned stock FDT in `docs/emmc-reference.md`. The storage node therefore
matches the published PE artifact's base+overlay result. This is evidence that
the PE boot payload describes the same SDCC1 node as the captured board, not
that the owner device ran this PE release `[S]/[U]`.

The inspected DTB bytes are:

| Input | SHA-256 |
| --- | --- |
| `extracted/dtbs/00.dtb` (Bengal base) | `5c4c0599ff430bec71de6bb75663eb857c30ae368823d55054fd9a90e8eb77f8` |
| `extracted/dtbos/00-00000000-00000000.dtbo` (QRD overlay 00) | `7781256875f7efab9e0fd2d6d2e04a55a36df92be500d41c74567c4e0c0ebda5` |
| `merged.dtb` (base 00 + overlay 00 via `fdtoverlay`) | `47ba96e5692b4006d28b6a39ef701df1e6b76f7a333eada1b686e186063ee97f` |
| `live-fdt.dtb` (captured recovery FDT) | `da1cb76fcf9031de8ee2f1152456994220d3d01a2a2b73b211b684e92a0b6ebc` |

A property-name sort followed by `fdtget -t bx` value comparison of the merged
`/soc/sdhci@4744000` node against the live FDT covers 41 properties with zero
mismatches. That is node-level raw-value equality, not whole-blob equality,
boot-selection proof or release-source identity `[S]/[C]`.

Two caveats:

- `fdtoverlay` merges fragments only. The merged blob keeps the base root
  `model`/`compatible` (`Qualcomm Technologies, Inc. Bengal SoC`,
  `qcom,bengal`), while the live root carries the overlay's `Bengal QRD`
  strings. The capturing boot path therefore applied overlay root metadata that
  `fdtoverlay` does not copy; compare the storage node, not whole-blob equality
  `[C]`. A matching storage node also does not prove which of the 40 overlays
  the captured boot selected `[C]/[U]`.
- The overlay source at the pinned kernel anchor
  (`arch/arm64/boot/dts/vendor/qcom/P85946-qrd-overlay.dts`) has SHA-256
  `f03828c2682f4ef509e686ac895d35f978b233200774c4ff321ebc8f3d9d656b`, the same
  value recorded for the pinned vendor overlay in
  `docs/vendor-sdhc-power-sequence.md`. The decompiled artifact overlay is a
  separate text (`extracted/bengal-qrd-overlay.dts`, SHA-256
  `703c07802c94217702c1803ffcb4b1f3f3cd1e3756e6f70e199b498e6357c68b`) and is
  not the source file `[S]`.

## 5. Storage-source lineage

The pinned PE kernel files are vendor-lineage 4.19.300 code, not a mainline
port `[S]`. The table counts nonempty added plus deleted lines from `diff -u`,
excluding the two `---`/`+++` file-header lines and blank added/deleted lines.
A replaced line therefore counts twice, once on each side.

| File | vs Samsung CXG1/CWI2 (4.19.157) | vs LineageOS vendor `cbd2ed85` | vs mainline gta4l `cf5f763f` |
| --- | --- | --- | --- |
| `drivers/mmc/host/sdhci.c` | 79 | 4 | 2,565 |
| `drivers/mmc/host/sdhci-msm.c` | 27 | 51 | 6,957 |
| `drivers/mmc/core/core.c` | 20 | 29 | 1,726 |

The mainline comparison uses `linux-verify/mmc-core.c` (SHA-256
`56678ec9c220d952b63f4e2af7072f7d3b815cd867c7809c937b5f38cddd5304`) from
`cf5f763f0ef8a5b4d8bb4163833cba1160c51894` (`gta4l-7.2`). The earlier report's
8,209 figure compared `pe-ref/files/core.c` with `linux-verify/core.c`, whose
bytes are the regulator framework
(`9896430492f89adde15547b72780e8e795dcdf2a7b46c87237fa69941172f8fd`, identical
to `regulator-core.c`), not MMC core. That figure is withdrawn.

The LineageOS comparison uses the pinned vendor files recorded in
`docs/vendor-sdhc-power-sequence.md`
(`LineageOS/android_kernel_samsung_sm6115` at
`cbd2ed85b0262bbcafaa9e0cc6a979e1eaa2c1d4`; file hashes listed there). The four
nonempty changed `sdhci.c` lines represent two changes: the `sdhci_check_ro()`
spinlock (present in PE) and the removed `start_signal_voltage_switch()` call
in the reset path (absent in PE). The 51-line `sdhci-msm.c` delta is a copyright
year change, the LineageOS `sdhci_msm_gcc_reset()` helper (assert, 200 us,
deassert, 200 us) that PE inlines inside `sdhci_msm_hw_reset()` without the
post-deassert delay, and the helper's probe-time call (present in LineageOS,
absent in PE). The 29-line `core.c` delta is CQE-recovery semantics
(`MMC_CMD_RETRIES` and `mmc_poll_for_busy` in LineageOS; 0 retries and no busy
poll in PE) plus SD-express removal. The diff alone does not establish when
those changes were added relative to the PE branch `[S]/[U]`. None of the
deltas changes the CMD7 response type or the busy-wait design below.

### 5.1 Inspected source files

| Tree | File | SHA-256 |
| --- | --- | --- |
| PE (`lineage-18.1` head `5a3272a3`) | `drivers/mmc/host/sdhci.c` | `997c8a2c284db8efd6eb97ffcd3434ed76c6bdae8d640122d04729eae696ec58` |
| PE | `drivers/mmc/host/sdhci-msm.c` | `12faacccf16acefc4c3ce0251cfa79051f8dd5cf2d1a8159b0b5a2960a26de68` |
| PE | `drivers/mmc/core/core.c` | `d300629059876c49d5e450d5640f40043d52f42ee877005c66a4214bab34072b` |
| PE | `drivers/mmc/core/mmc_ops.c` | `f2bbb1b9d72390ea93d81aa366fce558ebd5ea8eafed09779a898d4201fb54fe` |
| PE | `drivers/mmc/core/mmc.c` | `9a9efe2f1167f1dba859aad4198901add84b4c91569d0bd778a82bff4559e066` |
| LineageOS (`cbd2ed85`) | `drivers/mmc/host/sdhci.c` | `846e5ea0dd264df5b95a6848245f44b42ed86bfa0d9a7b9ee1d486eda9aeca48` |
| LineageOS | `drivers/mmc/host/sdhci-msm.c` | `298aeafcd49392bc706ca7573b178da7bcf614294bed26ccd9c21c4f4dbef751` |
| LineageOS | `drivers/mmc/core/core.c` | `d5af6572f5142142dedeeedeaea88b38d1d631992c52e1804fa58bca90c1e340` |
| LineageOS | `drivers/mmc/core/mmc_ops.c` | `ad043ed759d094f891a24a2a1fce1a4ef38295ecaff55d38838533d4c88c436d` |
| Mainline (`cf5f763f`) | `mmc-core.c` (`drivers/mmc/core/core.c`) | `56678ec9c220d952b63f4e2af7072f7d3b815cd867c7809c937b5f38cddd5304` |
| Mainline | `sdhci.c` | `186305e102bd32df419ff80669e6018d6b3a9e4310c3101193413970355b9857` |
| Mainline | `sdhci-msm.c` | `b668e01dfcb6efac44249c287c6b8583b0a3d584f497e82d33df223cee61f53e` |
| Mainline | `mmc_ops.c` | `21ce493f0638b9b778af03ed01be9780ca5bcff12ec0e365a112af0137ad193f` |
| Mainline | `mmc.c` | `fc69b95ffbc277a7c1f293fb32e182f0230efc825ac07df8364da89e9fed5f54` |
| Samsung CXG1 = CWI2 (4.19.157) | `drivers/mmc/host/sdhci.c` | `6efb32596145fa2dfe15c7ad3a10b58076bb354f6da10658422b16f415af3308` |
| Samsung | `drivers/mmc/host/sdhci-msm.c` | `347530fd6d700f2fa56825c11e574761ed7aaaaad14788d815108b4219447b5f` |
| Samsung | `drivers/mmc/core/core.c` | `023c76f941b09673663a3275c5a43721ef9d7b970abdf3903f7b2f071b4649ca` |

The PE and LineageOS trees are cached copies; the Samsung files are byte-equal
in the CXG1 and CWI2 archives. The related `mmc.c` file is absent from the
LineageOS cache, so its CMD7 call-site trace uses the PE tree. The PE clone is
shallow and its head is not a proven release revision; the inspected-file
hashes identify the compared bytes, not the bytes compiled into the release
artifact `[S]/[U]`.

The PE tree is consequently a sibling of the Samsung/LineageOS vendor copies,
not independent corroboration. Its storage stack says nothing by itself
about the mainline-derived XNU core.

## 6. Board-selected command and clock path

### 6.1 CMD7

- `drivers/mmc/core/mmc_ops.c:90-116` constructs CMD7 as
  `MMC_RSP_R1 | MMC_CMD_AC` with argument `rca << 16`. The inspected `mmc.c`
  calls it after CMD3 and CMD9 and before the first EXT_CSD read
  (`mmc.c:2149-2205`). The host advertises
  `MMC_CAP_WAIT_WHILE_BUSY | MMC_CAP_NEED_RSP_BUSY` at probe, but CMD7 is built
  directly and that capability is not consulted for it. CMD7 is therefore a
  plain R1 command with no in-command busy wait in the inspected source `[S]`.
  Exact source-to-artifact correspondence is unresolved, so statements about
  the released binary are conditional `[C]`.
- R1B is constructed only where a function explicitly sets it: erase
  (`drivers/mmc/core/core.c:3053-3059`), switch
  (`drivers/mmc/core/mmc_ops.c:559-571`), sleep/awake
  (`drivers/mmc/core/mmc.c:2495-2505`), HPI stop
  (`drivers/mmc/core/mmc_ops.c:824-827`) and CQE recovery
  (`drivers/mmc/core/core.c:1458-1468`). CMD7 is not one of them `[S]`.
- `sdhci_msm_probe()` sets `host->mmc->max_busy_timeout = 0`
  (`drivers/mmc/host/sdhci-msm.c:5998`) and
  `MMC_CAP_WAIT_WHILE_BUSY | MMC_CAP_NEED_RSP_BUSY` (`:6002`). The comment
  states the intent: keep explicit R1B commands as R1B and let the host handle
  the busy timeout instead of converting them to R1. The capability does not
  apply to CMD7, which is already R1 `[S]`.
- `sdhci_send_command()` polls `SDHCI_CMD_INHIBIT` (and `SDHCI_DATA_INHIBIT`
  for data-line commands) for at most 10 ms and skips the data inhibit for
  stop commands (`drivers/mmc/host/sdhci.c:1341-1370`). Response processing
  returns early for an R1B command while busy is pending
  (`sdhci.c:1498-1516`); busy end is handled in `sdhci_data_irq()` on
  `SDHCI_INT_DATA_END` (`sdhci.c:3347-3388`). A TC-only state is not treated as
  a completed R1B command `[S]`.
- `SDHCI_QUIRK2_IGNORE_DATATOUT_FOR_R1BCMD` is set unconditionally
  (`drivers/mmc/host/sdhci-msm.c:5789`) and suppresses data-timeout handling
  for R1B commands (`drivers/mmc/host/sdhci.c:3369-3378`). It neither applies
  to a plain R1 CMD7 nor supplies a missing CC, so it is not evidence for the
  port's R1B timeout `[S]/[U]`.
- `sdhci_prepare_data()` calls `sdhci_set_timeout()` only when
  `sdhci_data_line_cmd(cmd)` is true, i.e. for a command with data or
  `MMC_RSP_BUSY` (`drivers/mmc/host/sdhci.c:1002-1003`, `:166-169`).
  `sdhci_set_timeout()` is the only writer of `SDHCI_TIMEOUT_CONTROL`
  (`:961-982`). A plain-R1 CMD7 therefore never programs that register; its
  completion is bounded by the 10 s software `host->timer` path
  (`:3133-3143`). `sdhci_calc_timeout()`'s `0xE` return for an unspecified busy
  timeout (`:884`) applies only to a data/R1B command that reaches it, not to a
  plain-R1 CMD7 `[S]`.
- The artifact's `qcom,sdhci-msm-cqe` compatible and `CONFIG_MMC_CQHCI=y`
  establish CQE support/eligibility, and the inspected probe calls
  `sdhci_msm_cqe_add_host()`/`cqhci_init()`
  (`drivers/mmc/host/sdhci-msm.c:5982-5984`, `:2599-2645`). They do not by
  themselves prove that runtime command queuing was enabled on the device. The
  post-initialization I/O path is therefore not established as comparable to
  the port's single-command PIO path `[S]/[U]`.

### 6.2 Clock, reset, supply and pad

- The base DT node's `qcom,clk-rates` starts at 400 kHz and reaches 384 MHz;
  the `sdhc_1` overlay selects four active and four sleep pin states. The PE
  host enables the core clock through `clk_set_rate()`, programs the vendor
  MCLK selector and `CORE_VENDOR_SPEC3` DLL policy, and waits up to 20 ms for
  `SDHCI_CLOCK_INT_STABLE` (`drivers/mmc/host/sdhci.c:1656-1685`) `[S]`.
- The PE `sdhci_msm_hw_reset()` callback asserts `core_reset`, waits 200 us,
  deasserts it and then restores registers, with no post-deassert delay
  (`drivers/mmc/host/sdhci-msm.c:5052-5095`). The later LineageOS tree factors
  the reset into `sdhci_msm_gcc_reset()` with a second 200 us delay and also
  calls it once during probe (`sdhci-msm.c:5052-5089`, call at `:5593`); PE has
  no probe-time GCC reset. The comparison therefore concerns PE's reset
  callback, not an established PE initial-probe reset sequence `[S]`. The
  port's GCC SDCC1 BCR assert/delay/release/delay pulse has the LineageOS
  helper's shape; whether it reaches the same reset line as the DT
  `core_reset` is not proven here `[C]/[U]`.
- The PE POR sequence writes `CORE_VENDOR_SPEC_POR_VAL 0xA1C`
  (`drivers/mmc/host/sdhci-msm.c:102`, applied at `:5709`) and immediately ORs
  `CORE_VNDR_SPEC_ADMA_ERR_SIZE_EN` (bit 7) into `CORE_VENDOR_SPEC`
  (`:5712-5716`), producing `0xA9C`; a later write sets
  `CORE_IO_PAD_PWR_SWITCH_EN` (`:5740-5746`). The port writes `0xA9C` directly
  at POR and later applies its pad-switch policy. The initial write and the
  post-update value must be recorded separately: the earlier claim that the
  port deliberately differs in `CORE_SW_RST` is wrong. `CORE_SW_RST` is bit 7
  of `CORE_POWER` at offset 0 (`sdhci-msm.c:46`, referenced in the power-IRQ
  comment at `:5747`), not a bit of `CORE_VENDOR_SPEC` at offset `0x20C`. In
  `CORE_VENDOR_SPEC`, bit 7 is `CORE_VNDR_SPEC_ADMA_ERR_SIZE_EN`. Remaining
  differences are the later pad-switch policy writes, not the POR constant
  `[S]`.
- The DT supplies (L24A 2.96 V, L11A 1.8 V, both with `always-on`/`lpm-sup`
  on VDD-IO) match the port's fixed 1.8 V VDD_180 signalling and retained-rail
  assumption. The port does not control either rail or select the sdc1 pin
  states; it relies on firmware state `[S]/[U]`.
- The PE artifact's `qcom,sdhci-msm-cqe` compatible and
  `CONFIG_MMC_CQHCI=y` establish CQE support/eligibility and a probe-time
  `sdhci_msm_cqe_add_host()` initialization. They do not by themselves prove
  that runtime command queuing was enabled. The port implements
  single-command PIO only, so the PE post-initialization I/O path is not
  established as a comparison for the port's PIO path `[S]/[U]`.

## 7. Comparison matrix

| Area | PE 4.19.300 artifact/source | XNU port | Meaning for this port |
| --- | --- | --- | --- |
| SDCC1 register windows, clocks, reset, 8-bit bus | Base DTB + QRD overlay; identical node property set to the live FDT | Same controller windows and GCC SDCC1 block; fixed probe map | Matching description; strengthens the board map `[S]` |
| Supplies | L24A 2.96 V, L11A 1.8 V, VDD-IO always-on/lpm-sup | Fixed 1.8 V signalling, retained rails, no regulator path | Intent matches; rail state still unmeasured `[S]/[U]` |
| Init clock | 400 kHz minimum, vendor MCLK/DLL selection, 20 ms stable wait | 400 kHz GCC RCG, DLL_RST/PDN and PWRSAVE_DLL handling, 150 ms nominal stable budget | Matching stage; no new clock-rate requirement `[S]` |
| Reset | `core_reset` callback: assert, 200 us, deassert; no post-deassert delay; no probe-time call | GCC BCR assert, 200 us, release, 200 us | Shape differs in post-release delay and probe-time reset; functional equivalence not proven `[S]/[C]` |
| Vendor POR | Initial `0xA1C`, then OR bit 7 → `0xA9C`; pad bits later | `0xA9C`; pad bits later | Same POR value after the vendor update; earlier `CORE_SW_RST` claim withdrawn `[S]` |
| CMD7 response | Inspected source: R1, no in-command busy wait | R1B with CRC/index | **Meaningful difference**: the inspected PE path never exercises the port's R1B completion; release-binary correspondence conditional `[S]/[C]` |
| CMD7 completion | CC finishes the command; TC not required; no timeout-register programming | CC+TC (busy end) required in software; polled | The device-tested PE stack does not corroborate or refute the port's CMD7 R1B behavior `[S]/[U]` |
| R1B busy commands | Explicit erase/switch/sleep/HPI/CQE-recovery; interrupt-driven busy end | CMD7 is the only R1B command in the identification path; polled | Different command set and completion mechanism `[S]` |
| Datatout quirk | `IGNORE_DATATOUT_FOR_R1BCMD` set unconditionally | No equivalent | Not applicable to PE CMD7 R1; not a fix for the port `[S]/[U]` |
| Command queue | CQE/CQHCI support configured; probe-time init; runtime enablement not established | PIO only | PE post-init I/O path is out of scope for the current port `[S]/[U]` |
| Pads/pinctrl | Overlay selects four active and four sleep sdc1 pins | No pinctrl driver; firmware pad state | Unresolved difference; not a basis for a speculative change `[S]/[U]` |
| Clock rates | Up to 384 MHz | 400 kHz in the current probe | Intentional bring-up stage `[S]` |

## 8. What this comparison establishes

- The PE release artifact is byte-identical to its published digest and carries
  a Bengal base DTB plus a QRD overlay whose SDCC1 node matches the captured
  live FDT `[S]`.
- Its storage source is vendor-lineage 4.19.300 code close to the pinned
  LineageOS vendor copy, not mainline `[S]`.
- The inspected vendor source sends CMD7 as R1 and does not wait for the R1b
  busy end; if the release used that path, it does not exercise the port's
  R1B completion. The XNU port sends CMD7 as R1B and requires CC+TC. The
  recorded `fmt=2` failure (TC set, CC absent) belongs to the port's path only
  `[S]/[C]`.
- A device-tested PE build therefore does **not** prove the port's CMD7 R1B
  completion handling is wrong, and it does not supply a TC-only workaround,
  timeout increase, quirk import or card-status shortcut `[S]/[U]`.

It does not establish:

- that the release was built from the pinned kernel/device commits `[U]`;
- that the release ran on the owner's SM-T505N (only SM-T505 was
  device-tested) `[U]`;
- that the owner device's rails, pads, card state or controller state match
  the release boot `[U]`;
- that the eMMC works under XNU, or that the historical CMD7 timeout cause is
  known `[U]`.

### 8.1 CMD7 response-policy decision

| Question | Current port | Inspected Linux/PE | Evidence | Unproven assumption | Discriminating host test |
| --- | --- | --- | --- | --- | --- |
| Response type | R1B (`RESP_SHORT_BUSY`, CRC+index) | R1 (`MMC_RSP_R1`, `MMC_CMD_AC`) | Port call site `Gta4lEmmc.c:1515-1518`; PE `mmc_ops.c:90-116` | The release binary executes the inspected `mmc_ops.c` path | `cmd7-tc-only` and `cmd7-cc-only` |
| Completion rule | CC and TC both inside one shared 1 s poll budget | CC finishes the command; no in-command busy wait | Port loop `Gta4lEmmc.c:624-648`; PE `sdhci_finish_command()` `sdhci.c:1498-1516` | A TC-only sample means the card completed; it may instead be a host-visible status artifact | `cmd7-cc-only` succeeds under R1 and times out under R1B; `cmd7-tc-only` is the historical observation |
| Timeout | 1 s nominal poll budget, no retry | 10 s software timer for a non-responding R1 command; 3 `mmc_wait_for_cmd` retries | Port `Gta4lEmmc.h:196-203`; PE `sdhci.c:3133-3143`, `core.c:1287-1328` | The port's budget is shorter than the reference's outer bound | Existing bounded-poll and late-completion scenarios pin the stop |
| Effect of a policy change | — | — | The recorded `fmt=2` state is TC-only with CC absent | Changing R1B to R1 would not create CC and would not explain a missing CC | Any proposed policy must re-run the CC-only/TC-only scenarios |

The audit recommendation is **no production change**: keep the current R1B
CC+TC contract, CRC/index checks, response-after-CC snapshot, bounded polling
and failure-stop semantics. Both the port and the inspected Linux path require
CC; switching to R1 would bypass TC but would not supply a missing CC, so it
does not explain or repair the historical observation. No fallback, retry,
TC-only acceptance, timeout increase, quirk import or card-status shortcut is
justified by this comparison. A policy change would need a separately explicit
change plan and acceptance tests `[S]/[U]`.

The "R1b" label in `docs/emmc-cmd7-timeout-finding.md` describes the port's
chosen R1B completion lifecycle, not a protocol requirement that every host
must use R1B for CMD7. The inspected Linux stacks use R1; the card's protocol
response and the host's busy-detection choice are separate `[S]`.

## 9. Broader reference inventory (non-blocking)

`dev-common` also contains Android userspace and HAL configuration that may be
useful for later, separately scoped subsystems: `rootdir/bin/init.qcom*.sh`
(post-boot, early-boot, sensors, USB, DCVS, charger policy),
`rootdir/etc/fstab.*` and `init.samsung*.rc`, `audio/configs/` (mixer, I/O
policy, effects, platform info), `dolby/`, `wifi/`, `power/`, `recovery/`,
`sepolicy/` and the RRO overlays. These are Android userspace/SELinux and
vendor tuning inputs, not portable XNU operations. No file from them is
imported or proposed here `[S]`.

## 10. Reproduction

Read-only commands; no device access and no download. The bounded extractor
`scripts/extract-gta4l-pe-artifact.py` reads only the release members needed
for this record. It validates ZIP member names, sizes, the Android boot v2
header, independent section alignment, recovery-DTBO fields, the FDT sequence
and the DTBO table, then writes the selected outputs to a new directory. It
refuses an existing output and never executes archive content.

```sh
cd $HOME/slopcode/taba7unixnu/src/UniXNU-gta4l
rm -rf /tmp/opencode/pe-extract-run
python3.11 -B scripts/extract-gta4l-pe-artifact.py \
  --archive /tmp/opencode/pe-artifact/PixelExperience_Plus_gta4l-11.0-20261007-1420-UNOFFICIAL.zip \
  --output /tmp/opencode/pe-extract-run --pinned
cat /tmp/opencode/pe-extract-run/extraction.json
```

`--pinned` checks the archive SHA-256 and the selected output hashes recorded
above. Without it the extractor still performs every structural check. The
output contains `boot.img`, `dtbo.img`, `kernel.gz`, `Image`, `ramdisk`,
`dtb.img`, `kernel.config`, the 12 `dtbs/NN.dtb` images, the 40
`dtbos/NN-xxxxxxxx-xxxxxxxx.dtbo` images, the two partition lists, the OTA
metadata and `extraction.json`. DTBO names come from the table's ID/revision
fields, not from names inside the ZIP.

The test `scripts/test-gta4l-pe-extract.py` covers the success path and rejects
bad boot magic, truncated sections, unaligned pages, a recovery-DTBO offset
without a recovery DTBO, FDT block and trailing-byte errors, DTBO table and
entry overflows, overlapping entries, truncated gzip, missing IKCONFIG
markers, a wrong device name, duplicate members, symlinks, traversal names and
expectation mismatches. It runs the pinned release only when the archive is
already in the host cache.

The historical inline snippets that started from already-extracted files are
superseded by the extractor. The merge and node comparison remain separate:

```sh
cd /tmp/opencode/pe-extract-run
fdtoverlay -i dtbs/00.dtb -o merged.dtb dtbos/00-00000000-00000000.dtbo
sha256sum dtbs/00.dtb dtbos/00-00000000-00000000.dtbo merged.dtb
cmp merged.dtb /tmp/opencode/pe-artifact/merged.dtb

LIVE=$HOME/slopcode/taba7unixnu/device-dump/private/gta4l-dump/live-fdt.dtb
fdtget -p merged.dtb /soc/sdhci@4744000 | sort > merged.props
fdtget -p "$LIVE" /soc/sdhci@4744000 | sort > live.props
diff merged.props live.props
for p in $(fdtget -p merged.dtb /soc/sdhci@4744000 | sort); do
  test "$(fdtget -t bx merged.dtb /soc/sdhci@4744000 "$p")" = \
       "$(fdtget -t bx "$LIVE" /soc/sdhci@4744000 "$p")" || echo "diff: $p"
done
```

The merge is a comparison aid, not a bootable image. The caches are not part of
the repository. If `/tmp/opencode/pe-artifact` or `/tmp/opencode/pe-ref` is
lost, stop and record the result as unavailable: do not silently re-fetch the
1.4 GB release or the shallow source clones. A new download needs an explicit
scope, and a partial extraction must not be used `[S]/[U]`.

## Limits

- This review executed no ROM, installer, boot image or extracted binary.
- The release's exact build revisions, its build environment and its kernel
  config generation are unresolved.
- The release's required Android 12 firmware and vendor blobs were not
  downloaded or inspected; the comparison covers the boot payload and pinned
  source only.
- The artifact's SM-T505 scope is not the owner's SM-T505N.
- No rail, pad, clock-tree, bus or card state was measured; the eMMC remains
  unconfirmed in XNU.
- The PE stack is a device-tested reference only. Its CMD7 R1 design is not
  evidence that an R1B design is defective, and its CQE/vendor path is not a
  template for the current PIO core.
- The PE clones are shallow. Repository history and the exact release
  revisions are not established, and the inspected-file hashes identify cached
  bytes, not the release binary `[U]`.
- The 41-property comparison is node-level raw-value equality. It is not
  whole-FDT equality, proof of overlay selection or proof that the owner device
  ran the release `[C]/[U]`.
