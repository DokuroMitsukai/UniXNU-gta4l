# UniXNU for the Galaxy Tab A7 10.4 (gta4l)

Samsung Galaxy Tab A7 10.4 (2020), SM-T505N, Qualcomm SM6115 (Bengal).
This repository owns the board DTS, peripheral wiring, firmware inputs,
partition layout, payload policy and packaging.

## Status

- Format 2 configuration: `schema_version = 2`, pinned `sm6115` SoC source,
  iPadOS 17.7.11 OS profile, layered ARM64/SoC/OS/device inputs and board DTS.
- The shared SM6115 tree describes eight CPUs, PSCI 1.0 SMC, GICv3 at
  `0xf200000` and the 19.2 MHz timer. UFS is described but disabled.
- The kernel host build and prelink pass from the regenerated Format 2
  `.config` (`CONFIG_XNU_NATIVE_PAN=n`): `out/gta4l/ipados-17.7.11-21H461/gta4l/kernel/kernel.macho`
  is a 49 MiB Mach-O arm64 image with SHA-256
  `ef85af5417ace24e7b96f6c5a3224d2e7c40a163c1532bf2586f23fb5dd5f8bd`.
  See [docs/build-revisions.md](docs/build-revisions.md#kernel-build) for the
  two earlier failures and their fixes.
- The firmware builds with `ENABLE_SECUREBOOT=1`. A host kernel build is not a
  hardware boot, and no boot image or payload has been produced from this
  kernel yet.
- Display, input, storage, power, GPU and network drivers remain unimplemented
  or unvalidated.
- QEMU AppleImage4/AMFI findings are in `docs/qemu.md` and are cross-port
  input requirements, not gta4l hardware evidence.

## TB128FU reference

The Lenovo Xiaoxin Pad 2022 (TB128FU, Snapdragon 680 / SM6225) port by
[xVanTuring](https://github.com/xVanTuring/UniXNU-tb128fu) is the closest
proven reference: same Bengal family, A53/A73 cores, GICv3 addresses, timer
PPI list, PAN constraint and Adreno 610. Its timer, IPI and original-PMGR
findings are compared at pinned revisions in
[docs/tb128fu-comparison.md](docs/tb128fu-comparison.md), with the upstream
BSD-2-Clause-Patent credit. No TB128FU source is imported here.

## Inputs

| Item | Value |
| --- | --- |
| SoC | Qualcomm SM6115 (Bengal) |
| SoC source | `DokuroMitsukai/UniXNU-sm6115-common` |
| OS profile | `ipados-17.7.11-21H461` |
| DRAM | `0x40000000` + `0xC0000000` (single ADT range; the live FDT reports three banks with a hole) |
| Framebuffer | `0x5C000000`, 1200x2000, stride 4800 |
| Boot image | v0, base `0x40000000`, kernel `0x40008000`, tags `0x40000100`, page 4096 |
| Firmware | Mu-Silicium `gta4lPkg`, `SILICIUM_UEFI.fd` |

The SoC source is pinned in `feeds.toml`. The board DTS includes
`sm6115.dtsi`; the common ADT generator supplies `/chosen`, `/cpus`,
`/arm-io`, `/defaults`, `/options`, `/product`, NVRAM and payload handoff.

## Build

From the shared UniXNU checkout:

```sh
./unixnu defconfig gta4l_defconfig -D ../UniXNU-gta4l
./unixnu fetch -D ../UniXNU-gta4l
./unixnu status --json -D ../UniXNU-gta4l
./unixnu tree -D ../UniXNU-gta4l
./unixnu build kernel -D ../UniXNU-gta4l
./unixnu build package -D ../UniXNU-gta4l
./unixnu build firmware --uefi ../Mu-Silicium -D ../UniXNU-gta4l
./unixnu build payload --unique-chip-id <hex> -D ../UniXNU-gta4l
./unixnu bootimg -D ../UniXNU-gta4l
```

Read [Port a device](https://unixnu.com/porting/) and
[Device tree](https://unixnu.com/porting/device-tree) before changing inputs.
Record host, QEMU and hardware results separately. Do not treat the panic screen
or a QEMU checkpoint as a gta4l hardware boot.
