# UniXNU for the Galaxy Tab A7 10.4 (gta4l)

Samsung Galaxy Tab A7 10.4 (2020), SM-T505N, Qualcomm SM6115 (Bengal).
This repository owns the board DTS, peripheral wiring, firmware inputs,
partition layout, payload policy and packaging.

## Status

- Format 2 configuration: `schema_version = 2`, pinned `sm6115` SoC source,
  iPadOS 17.7.11 OS profile, layered ARM64/SoC/OS/device inputs and board DTS.
- The shared SM6115 tree describes eight CPUs, PSCI 1.0 SMC, GICv3 at
  `0xf200000` and the 19.2 MHz timer. UFS is described but disabled.
- The firmware builds with `ENABLE_SECUREBOOT=1`.
- The kernel build is not yet validated. It currently stops at the corecrypto
  archive step because the Linux XNU bootstrap object set is incomplete.
- Display, input, storage, power, GPU and network drivers remain unimplemented
  or unvalidated. A firmware build is not a hardware boot.
- QEMU AppleImage4/AMFI findings are in `docs/qemu.md` and are cross-port
  input requirements, not gta4l hardware evidence.

## Inputs

| Item | Value |
| --- | --- |
| SoC | Qualcomm SM6115 (Bengal) |
| SoC source | `DokuroMitsukai/UniXNU-sm6115-common` |
| OS profile | `ipados-17.7.11-21H461` |
| DRAM | `0x40000000` + `0xC0000000` |
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
