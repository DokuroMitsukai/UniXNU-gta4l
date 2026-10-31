# gta4l read-only SDHCI handoff audit

Status: v2 booted on the tablet and completed the read-only handoff audit. The
v1 image was withdrawn after a BOOT-only packaging failure; its failure and
verified rollback are retained below. The capture is register-level handoff
evidence, not card or storage access.

## Activation

The audit is compiled into the gta4l kernel and called once from `bsd_init`
after `bsd_bufferinit()`. It returns immediately unless the boot argument
contains:

```text
gta4l-sdhci-audit=1
```

Without that argument there is no controller or GCC mapping and no register
read. The audit is separate from `gta4l-sdhci-snapshot=1`; either probe can be
selected independently.

## Why this audit exists

The successful v5 snapshot established the controller identity and the
firmware-left standard register state. It did not establish:

- whether the SM6115 GCC SDCC1 AHB/apps/ICE branches are running;
- which parent and divider the apps clock source is using;
- whether the v5 vendor power-control request bits are pending;
- whether vendor capability or pad-control state is consistent with the
  PM6125 L11 1.8 V IO rail.

Those facts must be observed at the audit point during `bsd_init`, not inferred
from recovery. TWRP's Linux `sdhci_msm` driver has already configured the
controller, and its regulator state is not the XNU handoff state. In particular,
the recovery L24 reading was internally inconsistent (`state=disabled` while
`num_users=1` and `microvolts=2960000`), so it cannot prove that the eMMC VDD
rail is on at the audit point.

The audit also does not treat Qualcomm power-status bits as proof that a
regulator action succeeded. `CORE_PWRCTL_STATUS` is a request/status word for
software; this probe reads it without clearing it or acknowledging success.
The write-to-clear and write-to-acknowledge registers are deliberately not read
or written.

## Allowlist

The offsets and widths are defined in
`package/Gta4lSdhciHandoffAudit.h` and checked by
`scripts/test-gta4l-sdhci-handoff-audit.py`.

### Standard SDHCI window: `0x04744000`, 4 KiB

| Offset | Width | Name | Purpose |
| --- | --- | --- | --- |
| `0x24` | 32 | Present State | command/data inhibit and bus-power state |
| `0x28` | 8 | Host Control | bus-width and LED/power-control bits |
| `0x29` | 8 | Power Control | bus-power enable and voltage selection |
| `0x2C` | 16 | Clock Control | internal/card clock enables and stability |
| `0x30` | 32 | Interrupt Status | combined normal/error state |
| `0x34` | 32 | Interrupt Enable | firmware-left interrupt enables |
| `0x3E` | 16 | Host Control 2 | UHS/timing and 1.8 V signalling state |
| `0x40` | 32 | Capabilities | base clock, voltage, bus-width and DMA capabilities |
| `0x44` | 32 | Capabilities 1 | version-4 and 64-bit fields |
| `0xFE` | 16 | Host Version | standard SDHCI version |

### Qualcomm v5 vendor window: same 4 KiB mapping

| Offset | Width | Name | Purpose |
| --- | --- | --- | --- |
| `0x318` | 32 | MCI Version | v5 core version; major version 1 is required |
| `0x320` | 32 | Core Generics | v5 generic control state |
| `0x240` | 32 | Core Power-Control Status | pending power request/status bits |
| `0x244` | 32 | Core Power-Control Mask | firmware-left interrupt mask |
| `0x20C` | 32 | Core Vendor Spec | vendor control state |
| `0x21C` | 32 | Core Vendor Spec Capabilities 0 | voltage, bus-width and DLL capabilities |
| `0x25C` | 32 | Core DDR Config | DDR/DLL configuration state |
| `0x208` | 32 | Core DLL Status | DLL lock/status state |

### SM6115 GCC SDCC1 page: `0x01438000`, 4 KiB

The page is GCC base `0x1400000` plus the SDCC1 register block at `0x38000`.
Only this page is mapped. Offsets below are relative to the mapping base
(`0x01438000`), so they are the low 12 bits of the GCC block offsets.

| Offset | Width | Name | Purpose |
| --- | --- | --- | --- |
| `0x00` | 32 | SDCC1 BCR | block reset state |
| `0x04` | 32 | SDCC1 Apps CBCR | apps branch enable/halt state |
| `0x08` | 32 | SDCC1 AHB CBCR | AHB branch enable/halt state |
| `0x0C` | 32 | SDCC1 ICE CBCR | ICE branch enable/halt state |
| `0x28` | 32 | SDCC1 Apps CMD RCGR | RCG command/update state |
| `0x2C` | 32 | SDCC1 Apps CFG RCGR | source select, divider and mode |
| `0x30` | 32 | SDCC1 Apps M | RCG M value |
| `0x34` | 32 | SDCC1 Apps N | RCG N value |
| `0x38` | 32 | SDCC1 Apps D | RCG D value |

The audit reads exactly these 27 values. It does not read the data/FIFO
window, response registers, ADMA descriptors, CQHCI registers, PMIC registers
or SMMU registers. It does not write any register, issue a command, reset the
controller, change clock or power, acknowledge a power request or touch the
card. It does not read the v5 power-clear or power-control registers.

## Report

A completed capture ends with a panic identified as:

```text
GTA4L-SDHCI AUDIT OK v=... sv=... c=... c1=... ps=... clk=... pwr=... hc=... hc2=... is=... ie=...
GTA4L-SDHCI AUDIT VENDOR g=... pst=... pm=... vs=... vc=... ddr=... dll=...
GTA4L-SDHCI AUDIT GCC bcr=... ac=... ahb=... ice=... cmd=... cfg=... m=... n=... d=...
```

- `v` is the v5 MCI core version.
- `sv` is the standard host version.
- `c`/`c1` are the standard capabilities.
- `ps`, `clk`, `pwr`, `hc`, `hc2`, `is`, `ie` are the standard handoff state.
- `g`, `pst`, `pm`, `vs`, `vc`, `ddr`, `dll` are the v5 vendor state.
- `bcr`, `ac`, `ahb`, `ice`, `cmd`, `cfg`, `m`, `n`, `d` are the SM6115 GCC
  SDCC1 state.

An unexpected MCI major version stops with
`GTA4L-SDHCI AUDIT REFUSED: unexpected MCI version ...` before the GCC mapping
and the remaining vendor reads.

## Hardware result

The owner manually booted `boot-ram-root-sdhci-audit-v2.img` (SHA-256
`a34519c66820eb5f1f4c4790be5e10233ffd4c8a4ece2df4768e7dca151c8546`) with
`gta4l-sdhci-audit=1`; the snapshot gate was absent. The audit reached its
intentional panic. The values below were transcribed from the owner's
photograph on 2026-10-28. Line wrapping is reconstructed for readability; the
field values are as photographed.

```text
GTA4L-SDHCI AUDIT OK v=10000072 sv=7202 c=742dc8b2 c1=0000808f
ps=01f800f0 clk=0003 pwr=00 hc=00 hc2=0000
is=00000000 ie=00000000

GTA4L-SDHCI AUDIT VENDOR g=00f04003 pst=00000000 pm=00000000
vs=00300a0c vc=742dc8b2 ddr=80040873 dll=00000000

GTA4L-SDHCI AUDIT GCC bcr=00000000 ac=00004221 ahb=00000001 ice=00004229
cmd=00000000 cfg=00000401
m=00000001 n=000000fe d=000000fd
```

The complete capture and parsed fields are retained in
`out/gta4l/ram-root-sdhci-audit-evidence/audit-capture.txt` and
`audit-result.json`.

| Area | Observation | Limit |
| --- | --- | --- |
| GCC reset | `bcr=00000000`; the SDCC1 block reset bit is clear | Does not prove the controller is initialized |
| GCC branches | `ac=00004221`, `ahb=00000001`, `ice=00004229`; the enable bits are set and the standard halt bits are clear | Does not measure frequency or prove the branch is actually clocking hardware |
| Apps RCG | `cfg=00000401`: parent select 4, divider encoding 1, M/N mode disabled; `cmd=00000000`; `m/n/d=1/fe/fd` | Consistent with the reference nominal GPLL6 384 MHz entry; the PLL rate was not measured |
| SDHCI clock | `clk=0003`: internal clock enable and stable bits set; SD/card clock enable clear | No card-clock output or card communication |
| SDHCI power | `pwr=00`: standard bus-power enable clear | External PMIC rail state is unknown |
| Power requests | `pst=00000000`, `pm=00000000`: no observed request or mask bits | Does not prove regulator voltage or retained rail state |
| DLL | `dll=00000000` | No DLL lock or high-speed readiness is established |
| Present state | `ps=01f800f0`: standard CMD/DAT inhibit bits are clear | Other present-state fields are not used as storage proof |
| Interrupts | `is=00000000`, `ie=00000000` | No status or enable is used to infer card state |

This confirms the boot path, both MMIO read paths, the v5 version gate and the
report path on hardware. It does **not** establish that the eMMC VDD/VDD-IO
rails are powered, that VDDCX or interconnect votes are present, that the
external card clock is running, or that the card can accept a command. The
nominal 384 MHz GPLL6 interpretation is a decoded configuration, not a measured
rate.

## Withdrawn v1 artifact

- Image: `out/gta4l/boot/boot-ram-root-sdhci-audit.img`
- SHA-256 `57b15d67ecdf450047ee8f1ea9efef657e8156fbd23dc7954fcd7df80e956652`
- 85,377,024 bytes; 80 MiB payload.
- Payload `31b78bec7529706407f682e64dbc9c4c990e86009025174fd3f791606b29ea31`;
  payload kernel `6e81598cd143c73f47b8d160433d87d84ca035f599187c334241762d4055854a`.
- Prelinked kernel `172951570ac0ccbdd8fac87dc089001adc9971cd476122ec8f1a9f685e102e3a`;
  ADT `2c0c4f737dc43abbc5c8b86a441bff172126a50b74b33288945032e8d9a5ec2a`.
- The archive contains both real probe objects. `_gta4l_sdhci_handoff_audit_run`
  is at `fffffff0079cf138`.
- Host disassembly confirms the GCC reads use mapping offsets `0x00`–`0x38`
  (base `0x01438000`) and that no mapped-MMIO store is emitted.
- The static self-test covers the mapped-window bound and alignment, and
  rejects a wrong GCC offset.

This v1 boot image is withdrawn. It was flashed and read back byte-for-byte,
but the tablet entered download mode instead of starting UEFI/XNU. The full
pre-flash BOOT partition was restored and verified. The defect was the appended
DTB: 2,598 bytes of minimal generated DTB instead of the 316,317-byte working
v5 boot DTB, which lacks `qcom,msm-id` and `qcom,board-id`.

## Corrected v2 artifact

The v2 image preserves the working v5 boot header, firmware + BootShim, boot
kernel section and 316,317-byte appended DTB exactly; only the 80 MiB payload
changes to the audit.

- Image: `out/gta4l/boot/boot-ram-root-sdhci-audit-v2.img`
- SHA-256 `a34519c66820eb5f1f4c4790be5e10233ffd4c8a4ece2df4768e7dca151c8546`
- 85,688,320 bytes.
- Appended DTB `dc4c719d06e408654680524af26052d609baa14dd57da9082fd9f35c6509de9e`.
- Boot kernel section `a4b054e04d506d118471495116e75ef234d1b45027e3cddcb87f97bc04c718b3`.
- Ramdisk / audit payload `31b78bec7529706407f682e64dbc9c4c990e86009025174fd3f791606b29ea31`.
- Component report: `out/gta4l/ram-root-sdhci-audit-evidence/v2-verification.json`.

v2 was flashed and read back byte-for-byte. The owner confirmed the restored v5
BOOT partition worked before this flash, then manually booted v2 and captured
the completed audit. The agent issued no reboot. The hardware result is recorded
above; the flash and readback are recorded in `flash-report-v2.json`.

## Packaging failure and rollback

- Flash/boot failure evidence:
  `out/gta4l/ram-root-sdhci-audit-evidence/flash-report.json`.
- Rollback evidence:
  `out/gta4l/ram-root-sdhci-audit-evidence/rollback-report.json`.
- Extracted working v5 boot DTB:
  `out/gta4l/ram-root-sdhci-audit-evidence/boot-dtb-v5.bin`.
- The test payload remains the 80 MiB, non-packed RAM-root configuration with
  the existing test-scoped trust cache. The audit argument is
  `gta4l-sdhci-audit=1`; `gta4l-sdhci-snapshot=1` is absent.

The payload-side ADT DTB and the bootloader-facing boot DTB are separate inputs.
The bootloader DTB must be copied byte-for-byte from the working image and must
not be regenerated from the minimal DTS.

## Sources

- Linux reference repository: `https://github.com/limonabisi/linux-gta4l`
- Branch: `gta4l-7.2`
- Commit: `cf5f763f0ef8a5b4d8bb4163833cba1160c51894`
- Files:
  - `drivers/mmc/host/sdhci-msm.c` (`sdhci_msm_v5_offset`)
  - `drivers/clk/qcom/gcc-sm6115.c`
  - `drivers/clk/qcom/clk-rcg2.c`
  - `arch/arm64/boot/dts/qcom/sm6115.dtsi`
  - `arch/arm64/boot/dts/qcom/sm6115-samsung-gta4l.dts`

The referenced Linux files declare `GPL-2.0-only`. No Linux source is copied
or translated here; the audit is an independent read-only implementation of
the recorded register layout.

## Integration

- Source: `package/Gta4lSdhciHandoffAudit.c`
- Header: `package/Gta4lSdhciHandoffAudit.h`
- Build recipe: `scripts/build-gta4l-sdhci-snapshot.py` (compiles the snapshot,
  audit and host-init probes into `libGta4lSdhciSnapshot.a`)
- Kernel hook: `patches/xnu/gta4l-sdhci-handoff-audit.patch`
- Static test: `scripts/test-gta4l-sdhci-handoff-audit.py`
- Diagnostic packager: `scripts/package-gta4l-diagnostic.py`
- Packager self-test: `scripts/test-gta4l-diagnostic-package.py`
- Boot-image gate: `scripts/check-gta4l-boot-image.py`
- Gate self-test: `scripts/test-gta4l-boot-image.py`
- Host-local init source: `package/Gta4lSdhciHostInit.c` and `.h`
- Host-local init static test: `scripts/test-gta4l-sdhci-host-init.py`
- Host-local init simulated-register test:
  `scripts/test-gta4l-sdhci-host-init-sim.py`
- Raw diagnostic payload builder:
  `scripts/build-gta4l-diagnostic-payload.py`
- Payload/ADT contract check: `scripts/check-gta4l-payload.py`
- Link input: `CONFIG_XNU_EXTRA_LD_ARCHIVES` in `configs/gta4l_defconfig`

The boot-image gate must pass against the hardware-tested v5 image before any
flash. It verifies the header page except the deterministic boot-image ID, the
recomputed ID, firmware + BootShim, boot kernel section, appended boot DTB,
zero kernel padding, absence of trailing data, the appended FDT total size and
the Qualcomm identification properties. It can also require an exact expected
ramdisk/payload hash. The withdrawn v1 image fails this gate; v2 passes it.

The diagnostic packager copies the pinned v5 header page, kernel section and
padding byte-for-byte, replaces only the ramdisk, recomputes the Android
boot-image ID, runs the payload/ADT contract and the image gate, and publishes
an output plus manifest. It does not read or modify `board.toml`, so no
temporary configuration substitution is needed. Rebuilding v2 from the v5
baseline and audit payload is byte-identical:
`a34519c66820eb5f1f4c4790be5e10233ffd4c8a4ece2df4768e7dca151c8546`.

The raw diagnostic payload builder makes the former temporary board explicit:
it copies the device tree to a work directory, sets `payload.size_mib` and
forces `payload.adt.packed_data_ramdisk = false`, runs the normal payload step,
and checks the result before writing its manifest. The tracked `board.toml`
keeps its normal 192 MiB packed configuration.

An independent ABL/LinuxLoader-oriented check with `bootimg_tool.py`, provided
by **https://github.com/NoNameBuilder**, was used as a second opinion and is recorded in
`out/gta4l/ram-root-sdhci-audit-evidence/independent-bootimg-verification.txt`.
It confirms the v2 header, appended FDT and Qualcomm ID properties; it rejects
the v1 DTB as expected. The script is not vendored because the supplied copy has
no license header.

The patch calls the audit before the snapshot probe. Both functions are
one-shot and return when their boot argument is absent. On a clean tree the
build recipe links a no-op bootstrap archive defining both symbols; the next
kernel build links the real probes.

## Limits

A completed audit is register-level handoff evidence, not proof that the card
is powered or that the controller can communicate. It does not establish the
PMIC L24/L11 state, the VDDCX power domain, interconnect bandwidth, SMMU
stream ID or card protocol state. The next write-capable host-init probe must
be justified by this capture, must not acknowledge a power request without the
corresponding regulator action or proven retained state, and must keep the
RAM-root path unchanged.

The next probe's design is in `sdhci-host-init.md`. The live FDT resolves the
supplies to RPM-SMD PM6125 L24/L11, but this tree has no XNU RPM/SPMI/regulator
path, so the first host-init milestone keeps bus power and the card clock off
and reports the power stage as blocked.

The pinned mainline gta4l port differs from the live FDT: it enables `sdhc_1`
without `vmmc-supply` or `vqmmc-supply`, leaves L24/L11 unreferenced, uses RPM
GLINK over `sram@45f0000` rather than the live legacy SMD node, and exposes
only cached regulator state. It therefore relies on retained firmware rails and
provides no hardware readback. See `rpm-power-interface.md`.
