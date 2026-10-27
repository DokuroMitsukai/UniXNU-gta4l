# Tab A7 build revisions

Primary checkout: `src/UniXNU-gta4l` (fork of `ilovemykedi/UniXNU-gta4l`).
Reference checkout: `src/UniXNU-gta4l-noname-reference` (NoNameBuilder; not
primary).

These are development integration pins, not hardware-qualified releases.

| Component | Repository | Branch | Commit |
| --- | --- | --- | --- |
| UniXNU | `DokuroMitsukai/UniXNU` | `integration/hero2lte-gta4l` | `456185449295c50365c54353b0a4b385ab50a17e` |
| Mu-Silicium | `DokuroMitsukai/Mu-Silicium` | `integration/hero2lte-gta4l` | `e14626a3757007a2dbc902d3db323620d5568923` |
| DarwinBoot | `DokuroMitsukai/DarwinBoot` | `bringup/shared-video` | `3f2f304784f8fbfbf2ebfdc8eba3af3577d92967` |
| ArmCpu | `DokuroMitsukai/ArmCpu` | `main` | `c9c4bd87cabc1fe6736e171cd2540313ac4b7883` |
| SM6115 SoC tree | `DokuroMitsukai/UniXNU-sm6115-common` | `main` | `4137c1c1fbe2b3693e5a61985fd01bbacc8fdabc` |
| UniXNU-exynos8890-common | `DokuroMitsukai/UniXNU-exynos8890-common` | `main` | `0258950adf615983aec3c87ca65cb52b2310ec95` |
| Tab A7 device | `DokuroMitsukai/UniXNU-gta4l` | `bringup/tab-a7-qemu-fixture` | this repository; use the branch head |

## Board policy

The primary `board.toml` is Format 2:

- `schema_version = 2`
- `soc_source = "sm6115"` and `os_profile = "ipados-17.7.11-21H461"`
- ARM64, SM6115, OS and device configuration/patch layers
- board DTS compiled with the shared `sm6115.dtsi`

It keeps:

- `ENABLE_SECUREBOOT = "1"`
- the existing `gta4l_defconfig` PAN setting
- payload size 192 MiB
- `panic_screen = true`
- `debug=0x14e`

The S7 Edge `board.toml` currently sets `ENABLE_SECUREBOOT = "0"`; its firmware
build is a development image, not a security-preserving result.

The NoNameBuilder working-copy test settings (`ENABLE_SECUREBOOT=0`,
`CONFIG_XNU_NATIVE_PAN=n`, the smaller payload, the reset probes and the
`cs_enforcement_disable` argument) were **not** ported. Keep those in a
separate diagnostic branch if they are still needed.

## Format 2 migration

- `feeds.toml` pins `UniXNU-sm6115-common` at `4137c1c1`.
- The resolver reports `schema_version 2` with arch/SM6115/OS/device profiles,
  canonical components and the output path
  `out/gta4l/ipados-17.7.11-21H461/gta4l`.
- `dts/gta4l.dts` compiles with `dts/sm6115.dtsi`; the hardware tree has one
  contiguous 3 GiB memory range at `0x40000000`.
- The common ADT generator produced a 13,552-byte ADT with `/chosen`,
  `/cpus`, `/psci`, `/arm-io`, `/defaults`, `/options` and `/product`, plus the
  CHRP NVRAM proxy, `crypto-hash-method=sha2-384` and
  `amfi-only-platform-code`.
- `unixnu.lock` is committed. The shared xnu/edk2 pins remain in the lock.
- `scripts/make-gta4l-adt.py` is a legacy format-1 generator and is not used by
  the Format 2 payload path.

## Host checks

Run from `src/UniXNU` unless noted.

| Check | Result |
| --- | --- |
| `python3 package/Scripts/test-menu-input.py` | PASS |
| `PYTHONPATH=scripts python3 -m unittest unixnu.test_kernel -v` | PASS |
| `python3 src/Mu-Silicium/Resources/Scripts/test_bootshim.py` | PASS |
| `python3 scripts/check-gta4l.py` (device repo) | PASS |
| `python3 scripts/qemu-smoke.py --help` (device repo) | PASS |
| `./unixnu status --json -D ../UniXNU-gta4l` | PASS; base `4561854`, Format 2 output path, device branch `bringup/tab-a7-qemu-fixture` |
| `python3 scripts/porting-record.py check --device ../UniXNU-gta4l` | PASS; 13 structured areas |
| `dtc -I dts -O dtb -i ../UniXNU-sm6115-common/dts ...` | PASS |
| `python3 scripts/make-adt.py --dtb ... --os-profile ... --board ../UniXNU-gta4l/board.toml ...` | PASS; 13,552-byte ADT with `/options`, NVRAM, crypto hash and AMFI platform code |

## Firmware builds

### Tab A7

PASS on this host:

```sh
./unixnu build firmware --uefi ../Mu-Silicium -D ../UniXNU-gta4l -j "$(nproc)"
```

Output: `src/Mu-Silicium/Build/gta4lPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd`.
Built with `ENABLE_SECUREBOOT=1`.

### S7 Edge

PASS on this host. Run from the S7 Edge workspace `src/UniXNU`:

```sh
./unixnu build firmware --uefi ../Mu-Silicium -D ../UniXNU-hero2lte -j "$(nproc)"
```

Output: `src/Mu-Silicium/Build/hero2ltePkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd`.
Built with `ENABLE_SECUREBOOT=0` from
`UniXNU-hero2lte/board.toml`; this pin does not change that setting. Treat the
S7 Edge image as a development build, not a security-preserving result.

## Kernel build

Attempted for Tab A7 on this Linux host with the S7 Edge workspace toolchain
plus locally built `tcsh` and `unifdef`. The build stopped at the corecrypto
archive step because the XNU bootstrap object set was incomplete
(`vm_kern.o`, `prng_random.o` missing). This is a host/build-order blocker, not
a code failure. The kernel build remains unvalidated for both devices.

## Device notes

- `Mu_Basecore` has pre-existing modified content in the shared Mu-Silicium
  checkout; it is not part of these pins.
- The NoNameBuilder checkout remains only as a reference for the uncommitted
  test configuration.
- The old format-1 `out/gta4l` tree is superseded by
  `out/gta4l/ipados-17.7.11-21H461/gta4l`.
- `scripts/make-gta4l-adt.py` is kept only as a historical format-1 reference;
  the Format 2 payload uses `dts/gta4l.dts` plus the common ADT generator.
- Do not treat a QEMU checkpoint or a firmware build as a hardware boot result.
