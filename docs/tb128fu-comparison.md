# TB128FU reference comparison

Status: reference inventory and adaptation plan, 2026-10-27. No TB128FU source
has been imported into this repository yet.

## Credit

This comparison is based on the **Lenovo Xiaoxin Pad 2022 (TB128FU, Snapdragon
680 / SM6225)** device repository by **xVanTuring**:

- <https://github.com/xVanTuring/UniXNU-tb128fu>
- pinned at `a9c3863dedbee6f319bff16d3b609ec6cef409bf`
- license: BSD-2-Clause-Patent, Copyright (c) 2026 UniXNU contributors

The TB128FU bring-up, the `ArmCpu` fork, the original-PMGR bridge, the
`make-tb128fu-adt.py` generator and the TB128FU hardware observations are that
project's work. This document only records what may be reusable for gta4l and
what must not be copied. Any future adaptation must keep the upstream license,
copyright notice and commit provenance. References below point to the pinned
revisions, not to a floating branch.

## Pinned references

| Role | Repository | Branch / pin |
| --- | --- | --- |
| TB128FU device tree, board, scripts, PMGR bridge | `xVanTuring/UniXNU-tb128fu` | `main` `a9c3863dedbee6f319bff16d3b609ec6cef409bf` |
| CPU, GIC and timer component | `xVanTuring/ArmCpu` | `tb128fu` `3aba18e46f4277fdf8774d0c06bf71700136995d` |
| Adreno 610 kernel driver (no GMU) | `xVanTuring/Adreno610` | `tb128fu` `43848231e83ab219fe0852437dce2d42d41d0fcf` |
| Adreno Metal user space | `xVanTuring/AdrenoMetal` | `tb128fu` `2ff4be9eabd2b1c1078f2d88e75e84d3d589a465` |
| SM6225 UFS host and verified root | `xVanTuring/Tb128fuUfs` | `tb128fu` `c77662101f58553a197c6b4ded0dec39c07187da` |
| SMMU 4K kit | `xVanTuring/ArmSmmu4K` | `tb128fu` `b464afc93869a239a01c697a912d5d6ff789f27c` |
| GOP framebuffer / legacy display | `xVanTuring/GopFramebuffer` | `tb128fu` `e3e737285e9077dc93c6de4c26a1e8b7f190e81e` |
| HID input | `UniXNU/ShengHidInput` | `main` `5f3a37cec0c3fc4234b10726b25489a55f0d77ef` |
| TB128FU UEFI firmware | `xVanTuring/uefi-tb128fu` | `tb128fu` `4a8b6e124153e4a7efbccfb04b88ac47c87b269a` |

Local comparison pins:

| Repository | Revision |
| --- | --- |
| `UniXNU-gta4l` | `841077a2dda9402a8c2f988d7bf775bc502f5a22` |
| `UniXNU-sm6115-common` | `4137c1c1fbe2b3693e5a61985fd01bbacc8fdabc` |
| `UniXNU` | `456185449295c50365c54353b0a4b385ab50a17e` |
| `Mu-Silicium` | `e14626a3757007a2dbc902d3db323620d5568923` |

The TB128FU `README.md` says `uefi-tb128fu` is "not published yet", but the
repository and the `tb128fu` branch above are reachable as of this inventory.
Treat the README and the first `board.toml` header, which says nothing has been
observed through UniXNU yet, as older prose than the README status table and
the pinned source. Prefer the source and record the inconsistency when
repeating a claim.

## Why this board is relevant

TB128FU and gta4l are both Bengal-family SM6115/SM6225 designs with
Cortex-A73/A53 clusters, the same GICv3 addresses, a 19.2 MHz timebase, the
same timer PPI list and an Adreno 610. TB128FU reports:

- iPadOS 17.7.11 reaching the home screen and accepting touch;
- all eight cores running under XNU;
- GICv3 distributor `0x0f200000`, redistributor `0x0f300000`, stride `0x20000`;
- the EL1 virtual timer on **INTID 19**;
- IPIs delivered as software-pended **SPIs 504..511** (INTIDs 536..543);
- an Adreno 610 kernel path without a GMU, with CPU-rendered QuartzCore;
- the original-PMGR bridge that publishes the original `ApplePMGRNub`.

That is directly useful evidence for gta4l, but the boards differ in SoC
revision, RAM size, storage, display, touch and firmware. The source is a
reference, not a board configuration to import.

## Reuse, adapt, do not copy

| Area | TB128FU evidence | gta4l decision |
| --- | --- | --- |
| Timer INTID | `armcpu-timer-intid=19`; the SM6225 DT timer node lists PPIs 1, 2, 3, 0 | **Adapt.** Add or port a per-device timer selection while keeping INTID 27 as the default. Our `UniXNU-sm6115-common` timer node has the same `<1 1>, <1 2>, <1 3>, <1 0>` list, so 19 is the first candidate to test. Do not change the shared default for other ports. |
| SGI/IPI delivery | SGIs did not arrive; `armcpu-spi-ipi=1` with `armcpu-spi-ipi-base=536`; TB's GIC has 544 INTIDs and its DT uses SPIs through 429 | **Adapt after measurement.** Do not enable SPI IPIs on gta4l until the boot CPU takes an SGI and we know our GIC capacity and unused lines. Port the mechanism as an opt-in boot argument, never as the shared default. |
| PAN | `CONFIG_XNU_NATIVE_PAN=n` with a comment that the Cortex-A73/Kryo Silver cores are ARMv8.0 | **Applied.** gta4l now sets `CONFIG_XNU_NATIVE_PAN=n`; `msr PAN, #1` is undefined on these cores. |
| PMGR / AMFI | `CONFIG_XNU_SHENG_PMGR=y`, `sheng-pmgr=2`; the bridge publishes the original nub through an ABI-audited provider and touches no hardware | **Adapt behind gates.** The bridge is the best candidate for the AMFI "No PMGR" dependency, but it must pass its contract audit against our kernelcache and prelink. Do not use it to bypass AMFI; unsupported power operations must keep failing explicitly. |
| `ArmCpu` changes | The TB128FU fork adds timer/SPI-IPI arguments and simplifies the GIC path; it does not retain our GICv2 support | **Do not merge wholesale.** Port the timer and IPI mechanisms with per-device defaults. Preserve gta4l's GICv3 path and the shared GICv2 path. |
| Graphics | Adreno 610 without GMU; SMMU context, zap shader, CP, interrupts and `NativeAdreno`; Metal user space not complete | **Adapt later.** Verify our GPU revision, firmware and SMMU topology. Do not adapt the Adreno 740 path as a substitute. Console/timer stability comes first. |
| Display / touch | GOP framebuffer plus a legacy display probe; Himax HX83102E on QUP SE0 SPI | **Compare before reuse.** The Tab A7 panel and touch controller are not established in the current record. Reuse only after matching the hardware IDs and firmware protocol. |
| Storage | SM6225 UFS link, `Tb128fuUfs` and the verified-root policy | **Do not reuse.** gta4l boots from eMMC (`4744000.sdhci`, `fstab_suffix=emmc`). UFS is a different controller and root policy. |
| Memory | `dram_size=0x180000000` (span across the hole), `owned_memory_size=0x100000000` (4 GiB) | **Do not copy.** Our SM-T505N is 3 GiB in three banks; the ADT span is `0x40000000+0xc0000000`, and the largest free run is 2 GiB at `0x80000000`. See [memory-map.md](memory-map.md). |
| Firmware | Mu-only `tb128fuPkg`; no vendor DXE; splash framebuffer; UEFI memory map in `MemoryMapLib` | **Compare, do not import.** Both boards use Mu-Silicium, but the reservation tables and boot image limits are board-specific. |
| Board layout | Format 1-style `board.toml`, TB128FU boot arguments and flashing flow | **Do not import.** Our tree is Format 2. Use only the pinned component sources and record every adaptation. |

## Detailed findings

### 1. The timer is INTID 19, not 27

The TB128FU DT timer specifiers are `<1 1>, <1 2>, <1 3>, <1 0>`: secure
physical, physical, virtual and hypervisor PPIs. XNU's EL1 virtual timer is
therefore PPI 3, which is **INTID 19** on that GIC. TB128FU booted with
`armcpu-timer-intid=19` after INTID 27 produced an interrupt storm: INTID 27
was another source that stayed asserted, and XNU's timer ran far ahead.

Our `UniXNU-sm6115-common` DTSI uses the same four specifiers. Our current
`ArmCpu` defaults to INTID 27. The correct order is:

1. port the timer selection with the existing 27 default;
2. pass `armcpu-timer-intid=19` for gta4l;
3. confirm timer ticks and no storm on the boot CPU;
4. only then enable secondary CPUs.

Do not change the shared default to 19; other devices are a different GIC/timer
population.

### 2. SGIs may not be the IPI mechanism

TB128FU observed that writes to `ICC_SGI1R_EL1` did not deliver IPIs. Its
workaround is one software-pended SPI per logical core, `armcpu-spi-ipi=1`
with `armcpu-spi-ipi-base=536` (SPIs 504..511). Its GIC has 544 INTIDs and the
DT leaves those lines unused; the source comment records that reasoning.

That does not prove gta4l has the same defect or that 536..543 are free here.
Before enabling it:

- test SGI delivery on the boot CPU with `armcpu-self-ipi=1` and the existing
  diagnostic counters;
- read `GICD_TYPER` and the live DT to find the real SPI capacity and used
  lines;
- pick unused INTIDs and record the evidence;
- keep the feature off by default and preserve the GICv2 path.

### 3. PAN must be off

TB128FU's defconfig sets `CONFIG_XNU_NATIVE_PAN=n` and explains that the
Cortex-A73 and Kryo Silver cores are ARMv8.0, so `msr PAN, #1` is an undefined
instruction. gta4l has the same core generation. The shared ARM64 profile
enables PAN, so the board override is a hardware requirement, not a test
setting. This is now applied in `configs/gta4l_defconfig`.

### 4. The PMGR bridge is a real candidate

Without a PMGR provider, the original AMFI code panics with "No PMGR" (the
TB128FU defconfig comment says exactly this). TB128FU's
`build-platform-power.py` and `package/ShengPlatformPower.cpp` publish the
original `ApplePMGRNub` through an ABI-audited provider. The source states
that it reads and writes no hardware and that unsupported power operations
fail explicitly. TB128FU enables it with `sheng-pmgr=2`.

The bridge is built in two passes: without a previous `kernel/kernel.macho`
it archives a no-op `sheng_pmgr_run`; the next kernel build links the real
bridge. Its audit pins the original kernelcache SHA-256:

```text
f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7
```

Our extracted kernelcache at `out/ipsw/21H461/kernelcache.decompressed` now
hashes to exactly `f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`,
the value pinned by the TB128FU audit. The bridge's static contract audit
passes against our prelinked kernel, and the linked image contains
`ShengPlatformPower`, `sheng_pmgr_run` and the `bsd_init` hook. QEMU TCG with
`sheng-pmgr=2` and no AMFI bypass reaches `_bsd_init`, `_sheng_pmgr_run` and
`_IOKitBSDInit` without the "No PMGR" panic, then stalls at
`_ml_get_timebase` before `_vfs_mountroot`. The tablet RAM-root fixture later
booted through the PMGR-linked kernel without either AMFI bypass argument; the
PMGR publication was still not independently read back.

### 5. The ArmCpu fork is not a drop-in

The TB128FU `ArmCpu` fork contains the timer and SPI-IPI work, but it removed
the GICv2 code that our shared component still carries for other devices. A
wholesale merge would regress the shared GICv2 path. Port the mechanisms with
per-device configuration and run the shared GICv2 host tests before updating
the shared pin.

### 6. Graphics, storage and memory are separate projects

- **GPU:** the Adreno610 fork is the relevant no-GMU path. TB128FU reports the
  kernel driver and GPU self-test working, but Metal still renders on the CPU.
  Our GPU revision, firmware and SMMU context are unverified.
- **Storage:** TB128FU's UFS driver and verified-root policy do not apply to
  gta4l's eMMC.
- **Memory:** TB128FU's 6 GiB span and 4 GiB owned window do not describe our
  3 GiB SM-T505N. The gta4l ADT range is a generator constraint, not a
  contiguous map, and the largest free DarwinBoot run is 2 GiB at
  `0x80000000`.

## What changed in gta4l from this comparison

- `configs/gta4l_defconfig`: `CONFIG_XNU_NATIVE_PAN=n`, with the ARMv8.0
  reason.
- `dts/gta4l.dts` and `docs/build-revisions.md`: the single 3 GiB ADT range is
  documented as a generator constraint spanning three live banks and a hole,
  not as contiguous RAM.
- `package/ShengPlatformPower.cpp`, `scripts/build-platform-power.py` and
  `patches/xnu/sheng-platform-power.patch` adapt the TB128FU bridge with the
  upstream MIT/BSD notices and the pinned commit credit. The static audit
  passes; the PMGR-linked kernel SHA-256 is
  `dc8184da757b5323d2f4fb6c7ac1d588790b3d44d583c6d6fd24897cdbbcfe60`.
- The PMGR QEMU run reaches `_bsd_init` → `_sheng_pmgr_run` →
  `_IOKitBSDInit` with no AMFI bypass, then stalls at `_ml_get_timebase`.
  No tablet test has run.

The PMGR bridge is the first TB128FU code adapted into this repository, with
credit and its original license notices. Timer, IPI, GPU and touch code remain
uncopied and gated on local evidence.

## Next actions

1. Build the tablet payload/boot image from the PMGR kernel; request owner
   approval for a bounded test without the AMFI bypass.
2. Add the timer selection with the 27 default; test INTID 19 on the boot CPU.
3. Measure SGI delivery and GIC capacity before any SPI-IPI change.
4. Resolve the QEMU `_ml_get_timebase` stall if it blocks shared-kernel testing.
5. Keep GPU, touch and eMMC work separate from the console/timer milestone.
