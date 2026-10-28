# gta4l read-only SDHCI handoff audit

Status: host-built; not flashed and not yet run on the tablet. This probe is the
prerequisite audit before any write-capable host initialization. It preserves
the working RAM-backed root and does not access the card.

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

## Built artifact

- Image: `out/gta4l/boot/boot-ram-root-sdhci-audit.img`
- SHA-256 `57b15d67ecdf450047ee8f1ea9efef657e8156fbd23dc7954fcd7df80e956652`
- 85,377,024 bytes; 80 MiB payload.
- Payload `31b78bec7529706407f682e64dbc9c4c990e86009025174fd3f791606b29ea31`;
  payload kernel `6e81598cd143c73f47b8d160433d87d84ca035f599187c334241762d4055854a`.
- Prelinked kernel `172951570ac0ccbdd8fac87dc089001adc9971cd476122ec8f1a9f685e102e3a`;
  payload kernel `6e81598cd143c73f47b8d160433d87d84ca035f599187c334241762d4055854a`;
  ADT `2c0c4f737dc43abbc5c8b86a441bff172126a50b74b33288945032e8d9a5ec2a`.
- The archive contains both real probe objects. `_gta4l_sdhci_handoff_audit_run`
  is at `fffffff0079cf138`.
- Host disassembly confirms the GCC reads use mapping offsets `0x00`–`0x38`
  (base `0x01438000`) and that no mapped-MMIO store is emitted.
- The static self-test covers the mapped-window bound and alignment, and
  rejects a wrong GCC offset.

The test image uses the 80 MiB, non-packed RAM-root payload configuration and
the existing test-scoped trust cache. It does not enable
`gta4l-sdhci-snapshot=1`. The boot-image configuration omits the stale
`/soc/spi@4a80000/...` touch-node patches because the current minimal DTS has no
such nodes. See `out/gta4l/ram-root-sdhci-audit-evidence/README.md` for the full
record.

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
- Build recipe: `scripts/build-gta4l-sdhci-snapshot.py` (compiles both probes
  into `libGta4lSdhciSnapshot.a`)
- Kernel hook: `patches/xnu/gta4l-sdhci-handoff-audit.patch`
- Static test: `scripts/test-gta4l-sdhci-handoff-audit.py`
- Link input: `CONFIG_XNU_EXTRA_LD_ARCHIVES` in `configs/gta4l_defconfig`

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
