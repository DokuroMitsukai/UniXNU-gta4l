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
  `.config` (`CONFIG_XNU_NATIVE_PAN=n`): the PMGR-linked
  `out/gta4l/ipados-17.7.11-21H461/gta4l/kernel/kernel.macho`
  is a 49 MiB Mach-O arm64 image with SHA-256
  `dc8184da757b5323d2f4fb6c7ac1d588790b3d44d583c6d6fd24897cdbbcfe60`.
  See [docs/build-revisions.md](docs/build-revisions.md#kernel-build) for the
  earlier failures and the no-bridge hash.
- The original-PMGR bridge is adapted with credit from TB128FU; its
  AMFI/PMGR contract audit passes, and the kernel contains
  `ShengPlatformPower`, `sheng_pmgr_run` and the `bsd_init` hook. A QEMU run
  with `sheng-pmgr=2` and no AMFI bypass reaches `_bsd_init`,
  `_sheng_pmgr_run` and `_IOKitBSDInit`, then stalls at `_ml_get_timebase`
  before `_vfs_mountroot`. This is host evidence, not a tablet boot.
- The firmware builds with `ENABLE_SECUREBOOT=1`. The PMGR-linked kernel was
  packaged into the RAM-root fs/timer diagnostic image
  (`c12e601e…`, 85,688,320 bytes), flashed and passed on the tablet as a
  bounded fixture; this is not a normal-userspace boot. The fixture artifacts
  reproduce byte-for-byte on Linux; see
  [docs/ram-root-fixture-reproducibility.md](docs/ram-root-fixture-reproducibility.md).
- Display, input, power, GPU and network drivers remain unimplemented or
  unvalidated.
- A native SDHCI v5/eMMC core is implemented and host-verified in
  `package/Gta4lEmmc.c`: controller reset, GCC 400 kHz, retained-rail power
  handshake, enumeration and single-block PIO reads. The opt-in
  `gta4l-emmc-probe=1` XNU probe is built into the kernel archive list but has
  not been booted on the tablet. See
  [docs/emmc-driver.md](docs/emmc-driver.md).
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

## NativeEmmc reference

The Redmi Note 9 (merlinx) NativeEmmc driver by
[Dracen](https://github.com/Dracen-W) is a read-only XNU block-device
reference, pinned at `4980f1070b0c822dedb9eb350df710a57df28590`
([catalog](https://unixnu.com/drivers/community-native-emmc)). It targets the
MediaTek MT6768 MSDC controller, not the SM6115 SDHCI v5 controller, so its
register and command code is not transferable. The host-only static audit in
[docs/native-emmc-reference-audit.md](docs/native-emmc-reference-audit.md)
records the reusable publication patterns, the MediaTek-specific parts and the
completion/error-path findings. No code is imported.

## Inputs

| Item | Value |
| --- | --- |
| SoC | Qualcomm SM6115 (Bengal) |
| SoC source | `DokuroMitsukai/UniXNU-sm6115-common` |
| OS profile | `ipados-17.7.11-21H461` |
| DRAM | 3 GiB in three live banks (`0x40000000+0x3e580000`, `0x80000000+0x20000000`, `0xa0000000+0x60000000`); ADT span `0x40000000+0xC0000000`; 2 GiB DarwinBoot window at `0x80000000` |
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

For a raw RAM-root diagnostic payload, use the explicit builder instead of
editing `board.toml`:

```sh
python3 scripts/build-gta4l-diagnostic-payload.py \
  --data-ramdisk <root.img> --trust-cache <trustcache.bundle> \
  --owner-trust-cache --unique-chip-id <hex> --diagnostic host-init \
  --kernel-arg rd=md0 --kernel-arg sheng-pmgr=2 \
  --output out/gta4l/diagnostic-payload.img
```

It forces `payload.size_mib=80` and `packed_data_ramdisk=false` in a temporary
device copy and requires the payload/ADT contract check. Package the result
with `scripts/package-gta4l-diagnostic.py`; that packager also enforces the
contract before publishing an image.

Read [Port a device](https://unixnu.com/porting/) and
[Device tree](https://unixnu.com/porting/device-tree) before changing inputs.
Record host, QEMU and hardware results separately. Do not treat the panic screen
or a QEMU checkpoint as a gta4l hardware boot.
