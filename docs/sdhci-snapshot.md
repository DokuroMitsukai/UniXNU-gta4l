# gta4l observation-only SDHCI snapshot

Status: corrected v5 diagnostic image, host-built and run once on the tablet.
The v5 core version read is valid. The earlier `0x50` variant reached the
intended panic, but that offset is the legacy separate-MCI layout and produced
an invalid version value.

## Hardware result

The owner manually rebooted the tablet with
`boot-ram-root-sdhci-v5.img` (SHA-256
`ce4d76e8024a220890e7f32e2992a7beb094087910b070fc615c6fe1ee86dcde`). The
probe reached its intended panic and reported:

```text
GTA4L-SDHCI SNAP OK v=10000072 sv=7202 c=742dc8b2 c1=0000808f
ps=01f800f0 clk=0003 pwr=00 hc=00 hc2=0000 is=00000000 ie=00000000
```

`v=10000072` decodes as major version 1, minor `0x72`. The capture therefore
confirms the v5 vendor version offset and the gate, mapping, read and panic
paths on the tablet. It does not confirm card protocol state or storage
access. The next diagnostic is the separate read-only handoff audit described
in `sdhci-handoff-audit.md`; it is host-built and not yet run.

## Activation

The probe is compiled only into the gta4l kernel and is called once from
`bsd_init` after `bsd_bufferinit()`. It returns immediately unless the boot
argument contains:

```text
gta4l-sdhci-snapshot=1
```

Without that argument there is no controller mapping and no register read.
The probe is not a driver, does not install a service, and does not change the
RAM-root path.

## Allowlist

The offsets and widths below are also defined in
`package/Gta4lSdhciSnapshot.h` and checked by
`scripts/test-gta4l-sdhci-snapshot.py`.

| Offset | Width | Name | Access here | Purpose |
| --- | --- | --- | --- | --- |
| `0x24` | 32 | Present State | read-only | command/data inhibit, card state, bus-power bit |
| `0x28` | 8 | Host Control | read-only | bus-width and LED/power-control bits |
| `0x29` | 8 | Power Control | read-only | bus-power enable and voltage selection |
| `0x2C` | 16 | Clock Control | read-only | internal/card clock enables and stability |
| `0x30` | 32 | Interrupt Status | read-only | combined normal and error interrupt state |
| `0x34` | 32 | Interrupt Enable | read-only | firmware-left interrupt enables |
| `0x3E` | 16 | Host Control 2 | read-only | UHS/timing and version-4 fields |
| `0x40` | 32 | Capabilities | read-only | base clock, voltage, bus-width and DMA capabilities |
| `0x44` | 32 | Capabilities 1 | read-only | version-4 and 64-bit fields |
| `0x318` | 32 | MCI Version | read-only | Qualcomm v5 vendor core version |
| `0xFE` | 16 | Host Version | read-only | standard SDHCI version |

The probe reads exactly these eleven values. It does not read the data/FIFO
window, response registers, ADMA descriptors, CQHCI registers, ICE registers,
GCC clock/reset registers, PMIC registers or SMMU registers. It does not write
any register, issue a command, reset the controller, change clock or power, or
touch the card.

The interrupt status register is the standard combined normal/error word, and
the interrupt enable register is included only as handoff state. The SDHCI
programming interface clears status bits by writing ones; a read does not clear
them. The probe never performs that write.

## Sources

The standard offsets and field meanings were checked against the pinned Linux
reference tree:

- Repository: `https://github.com/limonabisi/linux-gta4l`
- Branch: `gta4l-7.2`
- Commit: `cf5f763f0ef8a5b4d8bb4163833cba1160c51894`
- Files:
  - `drivers/mmc/host/sdhci.h`
  - `drivers/mmc/host/sdhci-msm.c`
  - `arch/arm64/boot/dts/qcom/sm6115.dtsi`
  - `arch/arm64/boot/dts/qcom/sm6115-samsung-gta4l.dts`

The Linux board DTS credits V6lhost (2026); the SoC DTSI credits Iskren
Chernev (2021). The files carry `GPL-2.0+ OR BSD-3-Clause` SPDX identifiers.
This probe is an independent implementation: no Linux source is copied or
translated. The v5 vendor register `0x318` is used only as a read-only version
observation; no vendor initialization sequence is imported. The legacy
separate-MCI version offset `0x50` is not used and is explicitly rejected by
the static test.

The tablet's live FDT and recovery inventory establish the 4 KiB host window at
`0x04744000` and the eight-bit bus. See `emmc-reference.md`.

## Integration

- Source: `package/Gta4lSdhciSnapshot.c`
- Header: `package/Gta4lSdhciSnapshot.h`
- Build recipe: `scripts/build-gta4l-sdhci-snapshot.py`
- Kernel hook: `patches/xnu/gta4l-sdhci-snapshot.patch`
- Link input: `CONFIG_XNU_EXTRA_LD_ARCHIVES` in `configs/gta4l_defconfig`
- Separate read-only handoff audit: `docs/sdhci-handoff-audit.md`

The build recipe compiles both probes against the exact `bsd_init.o` compile
record and writes `build/gta4l-sdhci-snapshot/libGta4lSdhciSnapshot.a`. On a
clean tree, where that record does not exist yet, it links a no-op bootstrap
archive defining both symbols so the first kernel can be produced; the next
kernel build links the real probes. The board-only patches call the handoff
audit and then the snapshot probe after the PMGR hook. Both functions are
one-shot and return when their boot argument is absent.

## Report and limits

A completed capture ends with a panic identified as:

```text
GTA4L-SDHCI SNAP OK v=... sv=... c=... c1=... ps=... clk=... pwr=... hc=... hc2=... is=... ie=...
```

`v` is the v5 vendor MCI core version, `sv` the standard host version, `c`/`c1` the
capabilities, `ps` present state, `clk` clock control, `pwr` power control,
`hc`/`hc2` host controls, `is` the combined interrupt status, and `ie` the
interrupt enable. The panic is deliberately separate from the RAM-root `PASS`
message and does not depend on persistent storage.

A fixed number of reads is not a guarantee against a bus hang. The v5
snapshot is hardware-observed, but the controller clock, power domain,
interconnect and PMIC prerequisites have not been proven under XNU. This
diagnostic only observes the handoff state; it does not establish card
protocol state, initialize eMMC, or qualify a storage path. The next step is
the read-only handoff audit, not a write-capable host-init sequence.
