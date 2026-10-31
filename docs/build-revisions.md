# Tab A7 build revisions

Primary checkout: `src/UniXNU-gta4l` (fork of `ilovemykedi/UniXNU-gta4l`).
Reference checkout: `src/UniXNU-gta4l-noname-reference` (NoNameBuilder; not
primary).

These are development integration pins, not hardware-qualified releases.

| Component | Repository | Branch | Commit |
| --- | --- | --- | --- |
| UniXNU | `DokuroMitsukai/UniXNU` | `—` | `456185449295c50365c54353b0a4b385ab50a17e` |
| Mu-Silicium | `DokuroMitsukai/Mu-Silicium` | `—` | `e14626a3757007a2dbc902d3db323620d5568923` |
| DarwinBoot | `DokuroMitsukai/DarwinBoot` | `bringup/shared-video` | `3f2f304784f8fbfbf2ebfdc8eba3af3577d92967` |
| ArmCpu | `DokuroMitsukai/ArmCpu` | `main` | `c9c4bd87cabc1fe6736e171cd2540313ac4b7883` |
| SM6115 SoC tree | `DokuroMitsukai/UniXNU-sm6115-common` | `main` | `4137c1c1fbe2b3693e5a61985fd01bbacc8fdabc` |
| Tab A7 device | `DokuroMitsukai/UniXNU-gta4l` | `bringup/tab-a7-qemu-fixture` | this repository; use the branch head |

The UniXNU and Mu-Silicium integration commits are pinned by SHA and are not on
the shared `main` branches.

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

The NoNameBuilder working-copy test settings (`ENABLE_SECUREBOOT=0`, the
smaller payload, the reset probes and the `cs_enforcement_disable` argument)
were **not** ported. Keep those in a separate diagnostic branch if they are
still needed. Its `CONFIG_XNU_NATIVE_PAN=n` is the exception: that is a
hardware requirement for the ARMv8.0 cores, not a diagnostic setting, and is
now part of the board defconfig.

The board previously carried `amfi_get_out_of_my_way=1` and
`cs_enforcement_disable=1` in `[payload] kernel_args` even though
`ENABLE_SECUREBOOT=1`. Those pre-existing test arguments have been removed.
The 2026-10-28 RAM-root fixture booted without either argument using a
test-scoped trust bundle; this is still a bounded diagnostic result, not an
authenticated-root or production security result.

## Format 2 migration

- `feeds.toml` pins `UniXNU-sm6115-common` at `4137c1c1`.
- The resolver reports `schema_version 2` with arch/SM6115/OS/device profiles,
  canonical components and the output path
  `out/gta4l/ipados-17.7.11-21H461/gta4l`.
- `dts/gta4l.dts` compiles with `dts/sm6115.dtsi`. The ADT uses one 3 GiB
  range at `0x40000000` because the common generator requires one range. The
  live FDT reports three banks (`0x40000000+0x3e580000`, `0x80000000+0x20000000`,
  `0xa0000000+0x60000000`) with a `0x7e580000..0x80000000` hole. The largest
  contiguous free run after the live and Mu reservations is 2 GiB at
  `0x80000000`, so [memory-map.md](memory-map.md) sets the DarwinBoot window to
  `0x80000000`. This is a host derivation, not a hardware result.
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
| `./unixnu status --json -D ../UniXNU-gta4l` | PASS; `schema_version` 2, Format 2 output path and the configured arch/SoC/OS/device layers |
| `python3 scripts/porting-record.py check --device ../UniXNU-gta4l` | PASS; 13 structured areas |
| `dtc -I dts -O dtb -i ../UniXNU-sm6115-common/dts ...` | PASS |
| `python3 scripts/make-adt.py --dtb ... --os-profile ... --board ../UniXNU-gta4l/board.toml ...` | PASS; 13,552-byte ADT with `/options`, NVRAM, crypto hash and AMFI platform code |
| `python3 scripts/test_prepare_xnu_source.py` | PASS; transient timeout retried, HTTP 404 not retried |
| `./unixnu build kernel -D ../UniXNU-gta4l -j "$(nproc)"` | PASS; 49 MiB `kernel.macho`; no-bridge `ef85af54...`, PMGR `dc8184da...`; 1006/1006 link objects; PAN=0 compile records |
| `python3 scripts/audit-amfi-pmgr-contract.py --candidate .../kernel.macho` | PASS; original AMFI read/OR/write and PMGR dispatch; target slots read `0xffffffff` and write no MMIO |
| `qemu-smoke.py` with `sheng-pmgr=2`, no AMFI bypass | REACHED `_bsd_init`, `_sheng_pmgr_run`, `_IOKitBSDInit`; no `_panic`; timed out at `_ml_get_timebase` before `_vfs_mountroot` |

## Firmware builds

### Tab A7

PASS on this host:

```sh
./unixnu build firmware --uefi ../Mu-Silicium -D ../UniXNU-gta4l -j "$(nproc)"
```

Output: `src/Mu-Silicium/Build/gta4lPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd`.
Built with `ENABLE_SECUREBOOT=1`.

## Kernel build

Attempted for Tab A7 on this Linux host with the shared workspace toolchain
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

- The clean no-bridge `kernel.macho` was a 49 MiB Mach-O arm64 image with
  SHA-256
  `ef85af5417ace24e7b96f6c5a3224d2e7c40a163c1532bf2586f23fb5dd5f8bd`.
- `link.filelist` has 1006 entries and none are missing.
- The `copyio.o`, `pcb.o` and `prng_random.o` compile records contain
  `XNU_NATIVE_PAN=0`.
- The original kernelcache at `out/ipsw/21H461/kernelcache.decompressed` has
  SHA-256 `f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`,
  exactly the value pinned by TB128FU's PMGR audit.

### PMGR bridge

The opt-in bridge is adapted with credit from xVanTuring's
`UniXNU-tb128fu`:

- `package/ShengPlatformPower.cpp` (MIT; copied from TB128FU commit
  `a9c3863dedbee6f319bff16d3b609ec6cef409bf`)
- `scripts/build-platform-power.py` (BSD-2-Clause-Patent; adapted for the
  Format 2 tree and IPSW cache)
- `patches/xnu/sheng-platform-power.patch` (the shared `bsd_init` hook)

`board.toml` keeps the full ordered archive plan and appends
`build-platform-power.py`; `CONFIG_XNU_SHENG_PMGR=y` enables the hook. The
bridge audit passes against the original kernelcache and the prelinked
kernel. The resulting kernel contains `ShengPlatformPower`, `sheng_pmgr_run`
and the `bsd_init` call; its SHA-256 is
`dc8184da757b5323d2f4fb6c7ac1d588790b3d44d583c6d6fd24897cdbbcfe60`.

QEMU TCG with `sheng-pmgr=2` and **no** `amfi_get_out_of_my_way` /
`cs_enforcement_disable` reaches `_bsd_init`, `_sheng_pmgr_run` and
`_IOKitBSDInit`, then stalls at `_ml_get_timebase` before `_vfs_mountroot`.
There is no AMFI "No PMGR" panic and no `_panic` checkpoint. The bridge's
publication was not independently read back; this is a bounded host result,
not a tablet result. That QEMU checkpoint pass did not produce a payload or
boot image; the later tablet images are recorded below.

### Hardware RAM-root result

The PMGR-linked kernel was packaged with an 80 MiB payload, a scoped ad-hoc
signed static EL0 fixture and a `N4KROOT` mockfs image. It booted on the tablet
without `amfi_get_out_of_my_way` or `cs_enforcement_disable`. The fixture
reported PID 1 and a 4096-byte page size through a deliberate userspace panic.
Image SHA-256:
`45ecc93049757e97a8d71edb776f67e69df5c9c58be13543295c154637273e24`.
This is a bounded fixture result, not an authenticated root.

A second image adds `/probe.bin` reads with distinct 4 KiB page contents, a
seek/reopen/EOF test, and a blocking `poll()` timeout measured with
`cntvct_el0`. It passed on the tablet with:

```text
RAMROOT-FST PASS: pid=1 page=4096 fs=read+seek+eof timer=poll
```

Image SHA-256:
`c12e601eceb8e5b5b9cf7a028c0f404653b67dc5ff5577813293673d2dea2b3a`.
This is still a bounded fixture result, not sustained timer or userspace
qualification.

Recovery inventory identifies `mmcblk0` at `soc/sdhci@4744000`, compatible
`qcom,sdhci-msm-v5`/`qcom,sdhci-msm-cqe`, with HS400 Enhanced Strobe, CQHCI and
64-bit ADMA. The card is a 29.1 GiB DX68MB. The live FDT matches the pinned
stock FDT (`da1cb76f...`); the primary and backup GPTs validate and agree, with
80 partitions including `boot` and `userdata`. No XNU eMMC driver exists yet;
the inventory is not a driver result. See [eMMC reference inputs](emmc-reference.md)
for the decoded resources, credits and reference-only boundaries.

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
