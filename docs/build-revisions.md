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
- `CONFIG_XNU_NATIVE_PAN=n` in `gta4l_defconfig`: the SM6115 Cortex-A73/A53
  cores are ARMv8.0 and do not implement PAN
- payload size 192 MiB
- `panic_screen = true`
- `debug=0x14e`

The S7 Edge `board.toml` currently sets `ENABLE_SECUREBOOT = "0"`; its firmware
build is a development image, not a security-preserving result.

The NoNameBuilder working-copy test settings (`ENABLE_SECUREBOOT=0`, the
smaller payload, the reset probes and the `cs_enforcement_disable` argument)
were **not** ported. Keep those in a separate diagnostic branch if they are
still needed. Its `CONFIG_XNU_NATIVE_PAN=n` is the exception: that is a
hardware requirement for the ARMv8.0 cores, not a diagnostic setting, and is
now part of the board defconfig.

The board still carries `amfi_get_out_of_my_way=1` and
`cs_enforcement_disable=1` in `[payload] kernel_args` even though
`ENABLE_SECUREBOOT=1`. These are pre-existing test arguments. The Format 2 OS
profile supplies `amfi-only-platform-code` and `crypto-hash-method`; a clean
run should remove `cs_enforcement_disable=1` and decide the AMFI policy before
claiming a security-preserving boot.

## Format 2 migration

- `feeds.toml` pins `UniXNU-sm6115-common` at `4137c1c1`.
- The resolver reports `schema_version 2` with arch/SM6115/OS/device profiles,
  canonical components and the output path
  `out/gta4l/ipados-17.7.11-21H461/gta4l`.
- `dts/gta4l.dts` compiles with `dts/sm6115.dtsi`. The ADT uses one 3 GiB
  range at `0x40000000` because the common generator requires one range; the
  live FDT reports three banks with a `0x7cc00000..0x80000000` hole. This is
  not a measured contiguous map; revalidate the DarwinBoot window against the
  firmware memory map before hardware use.
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
| `python3 scripts/test_prepare_xnu_source.py` | PASS; transient timeout retried, HTTP 404 not retried |
| `./unixnu build kernel -D ../UniXNU-gta4l -j "$(nproc)"` | PASS; 49 MiB `kernel.macho`, SHA-256 `ef85af54...`, 1006/1006 link objects, PAN=0 compile records |

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
plus locally built `tcsh` and `unifdef`.

The old Format 1 tree stopped at the corecrypto archive step, but the XNU
bootstrap object set was not generally incomplete: `link.filelist` listed 1006
objects and only `osfmk/DEVELOPMENT/prng_random.o` was absent, while
`startup.o.json` existed. The script deletes `prng_random.o` when
`XNU_CRYPTO_BOOTSTRAP` changes; an earlier pass had left the object absent. The
driver skips the bootstrap pass when `startup.o.json` exists, so the archive
builder ran before any make could recreate the object. A clean `build/xnu`
avoids that stale state. This was a stale-object ordering failure, not a
compiler failure.

The fresh Format 2 tree stopped even earlier, in `prepare-xnu-source.py`: a
GitHub raw fetch timed out while restoring omitted DriverKit headers. The
shared script now retries transient TLS failures; a retry restored 26 headers
and reached the XNU bootstrap pass. The local build uses the shared topic
branch `fix/xnu-header-fetch-retry` (`09b2877`, test `b3d8515`); the gta4l pin
stays `4561854` until that branch is merged and the result is retested.

The clean Format 2 run completed on 2026-10-27:

- `out/gta4l/ipados-17.7.11-21H461/gta4l/kernel/kernel.macho` is a 49 MiB
  Mach-O arm64 image with SHA-256
  `ef85af5417ace24e7b96f6c5a3224d2e7c40a163c1532bf2586f23fb5dd5f8bd`.
- `link.filelist` has 1006 entries and none are missing.
- The `copyio.o`, `pcb.o` and `prng_random.o` compile records contain
  `XNU_NATIVE_PAN=0`.
- The original kernelcache at `out/ipsw/21H461/kernelcache.decompressed` has
  SHA-256 `f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`,
  exactly the value pinned by TB128FU's PMGR audit.

This is a host build and prelink result, not a hardware or QEMU boot. No
payload or boot image has been produced from this kernel yet.

The kernel step consumes the generated `out/gta4l/ipados-17.7.11-21H461/gta4l/.config`.
Editing `configs/gta4l_defconfig` alone does not change it: rerun
`./unixnu defconfig gta4l_defconfig -D ../UniXNU-gta4l` after a defconfig
change. A first clean attempt retained `XNU_NATIVE_PAN=1` for this reason and
was discarded; the successful run used the regenerated `.config` with
`XNU_NATIVE_PAN=n`.

## Device notes

- `Mu_Basecore` has pre-existing modified content in the shared Mu-Silicium
  checkout; it is not part of these pins.
- The NoNameBuilder checkout remains only as a reference for the uncommitted
  test configuration.
- The old format-1 `out/gta4l` tree is superseded by
  `out/gta4l/ipados-17.7.11-21H461/gta4l`.
- [TB128FU comparison](tb128fu-comparison.md) pins xVanTuring's Lenovo
  Xiaoxin Pad 2022 / SM6225 port and records the timer, IPI, PAN, memory and
  PMGR findings. It is a reference with upstream credit, not imported code.
- `scripts/make-gta4l-adt.py` is kept only as a historical format-1 reference;
  the Format 2 payload uses `dts/gta4l.dts` plus the common ADT generator.
- Do not treat a QEMU checkpoint or a firmware build as a hardware boot result.
