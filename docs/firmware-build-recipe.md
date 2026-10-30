# gta4l Mu-Silicium firmware build recipe and module replay

Status: host-only build replay taken on 2026-10-30. No device, flash, boot,
power or storage operation was performed. The SDHC decision remains **NO-GO**
(`sdhc-decision-memo.md`).

This document pins the inputs of the Mu-Silicium UEFI build that produced the
historical `SILICIUM_UEFI.fd` carried by the gta4l SDHC images, and replays the
build on the machine that produced it. The replay reproduces the code and data
of every packaged module. The only observed delta is an 8-byte debug-directory
GUID in `PcdDxe`.

Tags: `[S]` source- or artifact-established, `[C]` conditional inference,
`[U]` not established.

## 1. Result

| Stage | Replay | Result |
| --- | --- | --- |
| Tested artifact identity | standalone FD and the booted image metadata | outer FV `42f900a3...`; the tested boot image's `boot.json` records the same `fd_sha256` `[S]` |
| Source and toolchain | Mu-Silicium `e14626a`, Mu_Basecore feed `cfb9372`, UniXNU `4561854`, Arch clang 22.1.8 | the tested log records the same paths and defines; every checkout, feed and toolchain package is pinned `[S]` |
| Module compile and link | clean build on the historical host at the historical absolute paths | 153 of 156 `.efi` build outputs hash-identical; the three `PcdDxe` copies differ only in the debug GUID `[S]` |
| Module population | build tree and inner FV | 156 `.efi` path entries are three copies of 52 built modules; the inner FV packages 86 PE modules: 45 built, 41 prebuilt `Binaries`, and 7 built modules are not packaged `[S]` |
| Inner FV | clean build | 8,339,456 bytes; exactly 8 bytes differ, at `0x2ed28`, the `PcdDxe` `RSDS` GUID; every FFS offset, size and other byte matches `[S]` |
| Outer FD | clean build | `421af406...` vs tested `42f900a3...`; `FVMAIN_COMPACT` used `0x173158` (replay) vs `0x1733a0` (tested) `[S]` |
| Bit-exact outer FD | stock `build` | not reproduced in this replay; the observed inner-FV delta is in generated debug text, not source code `[C]` |

The clean replay used an empty `Build/` directory in a user and mount
namespace, so the historical build tree was not modified. The replay log and
the module hash lists are archived under
`out/gta4l/firmware-replay/` on the analysis host.

## 2. Pinned inputs

| Item | Value |
| --- | --- |
| Build host | the historical Arch build host, x86_64, Linux `7.1.8-arch1-3`, glibc `2.44+r24+g16be1518495f-1`, 12 logical CPUs |
| Mu-Silicium | `src/Mu-Silicium`, commit `e14626a3757007a2dbc902d3db323620d5568923`, clean except the `Mu_Basecore` submodule |
| Mu-Silicium submodules | `Binaries` `a1501e0c2e642d6ac0d8f52c85af2a67b75a7799`; `Common/Mu` `b8d46c71610318e36a661819e1e3c0b8bdda413a`; `Common/Mu_OEM_Sample` `b70b4b7bb02c43bf9db2172dfb3ed0457e2361d6`; `Silicium-ACPI` `1c8bd63a7ffcaecf3c8764a6b01a11e053457543`; openssl `67b5686b4419b4cb8caa502711c41815f5279751` |
| Build edk2 tree | Mu_Basecore feed `src/UniXNU/out/feeds/mu_basecore-8f10084c`, git HEAD `cfb93724a06ff86ff5ba971413368a7809ee42a5`, clean, `https://github.com/microsoft/mu_basecore.git` |
| BaseTools | built from the feed's `BaseTools/Source/C`; `GenFw` mtime `2026-10-27 21:59:45 +0300`; `logs/basetools.log` SHA-256 `36c8190987df7bbb6d46a7a7c6b54b9712b13515e6cf0b2008231070a4e76b7e` |
| UniXNU | `src/UniXNU`, commit `456185449295c50365c54353b0a4b385ab50a17e`; `package/` and `drivers/DarwinBoot` are unchanged from this commit through `6ee4ada8a82f5bd2d9032b4d7cf033e878e0551f` |
| DarwinBoot | `drivers/DarwinBoot` gitlink `3f2f304784f8fbfbf2ebfdc8eba3af3577d92967` |
| Board file | `src/UniXNU-gta4l/board.toml` at `a472a00746f5b394f47438d2d210ebc09f9f9272`; defines `ENABLE_SECUREBOOT=1`, `DEVICE_MODEL=gta4l`, `MEMORY_PROTECTION=TRUE`, `SHIP_MODE=FALSE`, `BUILDID_STRING=gta4l-research` |
| FD geometry | `src/Mu-Silicium/Resources/Configs/gta4l.toml` at `e14626a`: `uefi_fd base=0x5FC00000 size=0x300000`; `FD_BASE=0x5fc00000`, `FD_SIZE=0x300000`, `FD_BLOCKS=768` |
| Platform | `Platforms/Samsung/gta4lPkg/gta4l.dsc` SHA-256 `300e07336f20504fa65aefcf8465f5ab68ed24c844a6cc5a6eb6adb7edaa4d9c`; `gta4l.fdf` SHA-256 `7306a43619145e9e6e667625ca829c6b77a7132b525eb1bc5564fe2c7e4160ba` |
| Compiler | Arch `clang 22.1.8-1`, `lld 22.1.8-1`, `llvm 22.1.8-2`, `llvm-libs 22.1.8-2`; `/usr/bin/clang` resolves to `clang-22` |
| Compiler binaries | `clang-22` SHA-256 `67ad17f2e61ed725abd3cefdd3fa1c24dc5c09ec3ed064dc03c0798e66fe8955`; `lld` `4b6e6ba199c88dce66c622124c264281e53ef8884921730d48269c8069091094`; `llvm-lib` `a6d906986e7840289870d48f29a69f0178c352c299eb5c72802b256482943c60`; `llvm-rc` `2ce21cce397e4a6294460733ec267c51da2ea8434c860e9191a0d11ea47215ee` |
| Toolchain shim | `toolchain/` contains `clang`, `lld-link`, `llvm-lib`, `llvm-rc` symlinks to the `/usr/bin` tools; `firmware.py` recreates them from `clang_bin()` |
| EDK II config | `edk2-conf/tools_def.txt` `ee0a064fe6f4bb3001b418edc4bb1a4cb87015cc16eecbae30b2b29a13c7d4e3`; `target.txt` `d602450f5d3f252d577ba08a9c8da9e23d04c97bf68523102045c51af3e25365`; `build_rule.txt` `da7847023c368a3789190a7597941f4e48f6ccea6af15e35f0f92fff5d63958c` |
| Tested log | `src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l/logs/firmware.log` SHA-256 `c0ce015888a3e9c6484d3566d2a30fc245b7c0eb72af5267d2bb2db18fdebd9f`; end `2026-10-27 22:04:25 +0300`; total `00:04:14`; 440 `ENABLE_SECUREBOOT=1` and no `ENABLE_SECUREBOOT=0` |
| Tested outer FD | `Build/gta4lPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd`, 3,145,728 bytes, SHA-256 `42f900a38e582026f5daabb70b19b340f0697f2f90b0248c53ecebc07f4186e9` |
| Tested inner FV | `FV/FVMAIN.Fv`, 8,339,456 bytes, SHA-256 `3bb7847a9f9abeec1bb70d812fe58ad014ae27a3254465e2da2a753cef99a8bd` |
| Tested FV map | `FV/FVMAIN.Fv.txt` SHA-256 `01847b8343b8755db811cca47c4b51a9e21791701d56239079c364bd68a7f2c5` |
| Tested BuildOptions | `BuildOptions` SHA-256 `4cdbbc41df3b55a8777e27eaf64f52e5bc79b2f7d397a53ab4aed9c4ad414870` |
| Tested module list | 156 `AARCH64` `.efi` path entries, SHA-256 `033ee33c0bcd5293c38e6be55f6b2af5fc14af45548ad05d3745bd9df4b8b5c4` |
| Boot image metadata | `out/gta4l/boot/boot.json` records `fd_sha256` `42f900a3...`; the host-init v2 image `boot-ram-root-sdhci-host-init-v2.img` is 85,688,320 bytes, SHA-256 `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133` (`firmware-handoff.md`) |

The `Mu_Basecore` submodule at `3c9c7b2e211e62d2a954ad760a36fe05adc43aa3`
remains in the Mu-Silicium tree, but this build does **not** use it: the log's
`EDK_TOOLS_PATH` and the last `PACKAGES_PATH` entry are the
`mu_basecore-8f10084c` feed. Do not attribute the tested FD to the submodule.
The feed directory name is UniXNU's feed identifier, not a git object; the
checked-out commit is `cfb9372...`.

The `Binaries` submodule is a required build input even though `board.toml`
does not list it in `[firmware].submodules`: `gta4lPkg/DeviceBuild.py` marks it
required and `Include/APRIORI.inc` resolves `Binaries/gta4l/...` INF files into
the volume. The inner-FV comparison below covers those prebuilt modules.

## 3. Build environment and command

`src/UniXNU/scripts/unixnu/firmware.py` at `4561854...` runs:

```sh
# <user>/<volume> are the historical path components recorded in the tested
# firmware.log; the PDB-path comparison needs the exact values.
HIST=/run/media/<user>/<volume>/unixnutaba72020test
cd "$HIST/src/Mu-Silicium"
export WORKSPACE="$PWD"
export EDK_TOOLS_PATH="$HIST/src/UniXNU/out/feeds/mu_basecore-8f10084c/BaseTools"
export CONF_PATH="$HIST/src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l/edk2-conf"
export PACKAGES_PATH="$PWD:$PWD/Silicon/Qualcomm:$PWD/Silicon/Silicium:$PWD/Platforms/Samsung:$PWD/Common/Mu:$PWD/Common/Mu_OEM_Sample:$PWD/Silicium-ACPI:$HIST/src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l/tree/package:$HIST/src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l/tree/drivers/DarwinBoot:$HIST/src/UniXNU/out/feeds/mu_basecore-8f10084c"
export PYTHON_COMMAND=python3
export PATH="$HIST/src/UniXNU/out/feeds/mu_basecore-8f10084c/BaseTools/BinWrappers/PosixLike:$HIST/src/UniXNU/out/feeds/mu_basecore-8f10084c/BaseTools/Source/C/bin:$PATH"
export CLANG_BIN="$HIST/src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l/toolchain/"
export SOURCE_DATE_EPOCH=1793123768
build -p Platforms/Samsung/gta4lPkg/gta4l.dsc -a AARCH64 -b DEBUG -t CLANGPDB -n 12 \
  -D FD_BASE=0x5fc00000 -D FD_SIZE=0x300000 -D FD_BLOCKS=768 \
  -D ENABLE_SECUREBOOT=1 -D DEVICE_MODEL=gta4l -D MEMORY_PROTECTION=TRUE \
  -D SHIP_MODE=FALSE -D BUILDID_STRING=gta4l-research -D ARCH=AARCH64
```

The `<user>` and `<volume>` components are personal path data and are elided
from this document. The tested log records them. The PE debug directory embeds
the absolute path, so the historical path is part of the build input; this
replay used it verbatim. A build elsewhere is expected to change the embedded
path and therefore the PE hashes.

`SOURCE_DATE_EPOCH` is the commit time of Mu-Silicium `e14626a`; `firmware.py`
pins it so the same sources embed the same build time. `-n 12` is parallelism
only. The tested log's `Processing meta-data` line and the full command list
use the same paths and defines; `BuildOptions` records the exact define set.

## 4. Replay

The module PE files embed the absolute PDB path in their debug directory.
Rebuilding the same sources and compiler at a different path changes those
bytes, so the replay uses the historical absolute paths. On the historical
host, an empty scratch `Build/` and a copy of `edk2-conf` are bind-mounted over
the historical locations in a user and mount namespace:

```sh
W=/run/media/<user>/<volume>/unixnutaba72020test
G=$W/src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l
SCRATCH=$W/.tmp/fw-replay        # contains an empty Build/ and a copy of edk2-conf/
unshare -rm bash -c '
  mount --bind "$1/Build"     "$2/src/Mu-Silicium/Build"
  mount --bind "$1/edk2-conf" "$3/edk2-conf"
  cd "$2/src/Mu-Silicium"
  # run the section 3 command
' _ "$SCRATCH" "$W" "$G"
```

The clean replay produced:

| Item | Value |
| --- | --- |
| Replay log | SHA-256 `0012a0e7a0da26a860ea8c09143ee38d5e52feed4a43c2501317605d7cab4929` |
| Replay module list | 156 entries, SHA-256 `763de186fd58eb0601b5f9bff961f6c65fa810370fbe5e9e8dbd81d39ea6deea` |
| Module-set comparison | 153 of 156 path entries match; the only differing entries are the three `PcdDxe` copies |
| Replay outer FD | SHA-256 `421af40630ae048b39bfee98d9780c7d7b3c1c1d60dab57cf4fd49e743cfdf24` |
| Replay inner FV | SHA-256 `7d6faa796081759f9e178033139e74c8eb7c70fbb8ba28cdfefc46f9f9e1caa4` |

Key module hashes, identical in the tested build and the replay:

| Module | SHA-256 |
| --- | --- |
| `DxeCore.efi` | `f4aefa943027a890f6fbc4381d0b7d6f5f9b1c4adb5584f2f71e0002ab3e35ca` |
| `UniXNU.efi` | `67e5de3f8d46e396cc0ebed9d19f467aa74cecb9642ecb0ebf381232cf9fdb7d` |
| `DarwinBoot.efi` | `87b0854cd7145b246c445ca62fe18aee06376a8848c32be59294596372773369` |
| `ArmTimerDxe.efi` | `64551071a51777b181f3e73adcd8d887df8841f68dbc75db3446997b82aa0cc3` |
| `PcdDxe.efi` | tested `60404587970c0abaa69a31ec1ee5d5a4f0ac49aad62ca11ca1135a702ccdaf27`; replay `41045e212e8262e2a804f784a672d6f399d298fb07ea36be88fc9026a55975d8` |

The 156 path entries are three copies (top-level, `DEBUG/`, `OUTPUT/`) of 52
unique EDK II-built module files. They are not the 86 PE modules packaged in
the inner FV: 45 of the 52 build outputs are packaged, and the other 41
packaged PE modules are prebuilt `Binaries/gta4l` modules; the remaining seven
build outputs are not packaged. The 8-byte inner-FV comparison covers all 86
packaged modules, built and prebuilt.

## 5. The `PcdDxe` delta

`PcdDxe.efi` is the only differing module. Section comparison:

| Section | Tested vs replay |
| --- | --- |
| `.text` (22,016 bytes) | identical |
| `.data` (5,120 bytes) | identical |
| `.pdata` (1,024 bytes) | identical |
| `.reloc` (512 bytes) | identical |
| `.rdata` (7,168 bytes) | 8 bytes differ at the debug directory |

The 8 bytes are the `RSDS` PDB GUID. The rest of the GUID
(`4c4c44205044422e`, the `LLDPDB.` marker), the age and the PDB path are
identical. In the inner FV the same 8 bytes are the only difference at offset
`0x2ed28`, inside the `PcdDxe` FFS (`80CF7257-87AB-47F9-A3FE-D50B76D89541`,
starting at `0x27b68`):

| Build | GUID bytes at `0x2ed28` |
| --- | --- |
| tested | `fc 05 d3 b8 16 34 d4 50` |
| replay | `d0 ea 73 0c ef c7 6b a5` |

Two differences in the generated `PcdDxe` text were observed between the tested
build and the replay. They are candidate inputs to the GUID delta; this replay
did not isolate which one changes the GUID, and it did not exclude other
generated or debug inputs:

- `PcdDxe`'s generated `AutoGen.h` embeds `STACK_COOKIE_VALUE`. EDK II writes a
  fresh 100-value pool to `StackCookieValues64.json` at each build and draws
  the module value from it. The tested value is `0x1F6060D040E44C5C`; the clean
  replay's is `0x3CD517D51BC8365F`. A seeded replay that copied the tested
  `StackCookieValues64.json` into the scratch tree still drew a different
  value: the build rewrote the pool. That does not show the value cannot be
  pinned by another method.
- `PcdDxe`'s generated `AutoGen.c` orders the commutative PCD flags as
  `PCD_TYPE_DATA | PCD_DATUM_TYPE_UINT8_BOOLEAN` in the tested build and the
  reverse in the replay. The compiled datum is identical; only the source text
  changes.

The `RSDS` GUID is derived from the PDB contents, so generated-text differences
are a plausible input to it. The PE code and data do not change. In this replay
the stock `build` driver did not provide a working pin for the generated text:
`--skip-autogen` failed in this BaseTools feed with
`AttributeError: 'WorkSpaceInfo' object has no attribute 'Platform'`. Other
pinning approaches were not attempted, so this does not establish that no
supported pin exists. A bit-exact outer FD was not achieved; it would require
controlling the generated `PcdDxe` text or the resulting PDB GUID outside the
path used here.

The two other build-directory inputs that could be order-dependent matched
exactly: `PcdTokenNumber` and the `AutoGen` ID file have identical hashes in
the tested build and the replay, and every FFS offset and size in
`FVMAIN.Fv.txt` matches.

## 6. What this establishes

- `[S]` The tested outer FD `42f900a3...` is the artifact built on the
  historical build host at `2026-10-27 22:04:25 +0300` from Mu-Silicium
  `e14626a`, the
  `mu_basecore-8f10084c` feed at `cfb9372`, UniXNU `4561854`, the `Binaries`
  `a1501e0c` submodule and Arch clang 22.1.8.
- `[S]` Every packaged module's code and data in the tested inner FV is
  reproduced: 153 of 156 `.efi` build outputs are byte-identical, and the
  inner FV differs in exactly 8 bytes in `PcdDxe`'s debug directory.
- `[C]` A bit-exact outer FD was not reproduced. The observed inner-FV delta is
  an 8-byte PDB GUID in `PcdDxe`, and the observed generated-text differences
  are candidate inputs. Pinning that text, or otherwise controlling the GUID,
  would be needed; this replay did not achieve it, and `--skip-autogen` failed
  in this BaseTools feed.
- `[S]` The earlier local rebuild at `b2b5bdce...` (45 of 86 packaged PE
  hashes differing) used clang 21.1.8 at a different absolute path, not the
  tested clang 22.1.8 at the historical path. The local tree's feed and
  submodule state were not re-verified against the tested build, and the local
  build was not re-run to separate the compiler, path and tree contributions.
  The historical replay is the controlled comparison: with the pinned feed and
  clang 22.1.8 at the historical path, all 86 packaged PE modules match in
  code and data, and only `PcdDxe`'s 8-byte debug GUID differs.
- `[S]` The booted evidence image carries this FD: `boot.json` records
  `fd_sha256` `42f900a3...`; the host-init v2 image's gzip kernel decompresses
  through the 192-byte BootShim to the outer FV `42f900a3...`
  (`firmware-handoff.md`).

## 7. Reproduction recipe

On an Arch host with the section 2 packages:

```sh
# 1. Pin the trees.
git -C src/Mu-Silicium checkout e14626a3757007a2dbc902d3db323620d5568923
git -C src/Mu-Silicium submodule update --init -- \
  Binaries Common/Mu Common/Mu_OEM_Sample Silicium-ACPI \
  Silicon/Silicium/OpensslPkg/Library/OpensslLib/openssl
git -C src/UniXNU checkout 456185449295c50365c54353b0a4b385ab50a17e
git -C src/UniXNU submodule update --init drivers/DarwinBoot
# If the feed is absent, run `(cd src/UniXNU && python3.11 ./unixnu fetch -D ../UniXNU-gta4l)`
# or clone https://github.com/microsoft/mu_basecore.git at cfb9372 to the path.
git -C src/UniXNU/out/feeds/mu_basecore-8f10084c checkout cfb93724a06ff86ff5ba971413368a7809ee42a5
git -C src/UniXNU-gta4l checkout a472a00746f5b394f47438d2d210ebc09f9f9272
# The build's {tree} is src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l/tree.
# If it is absent, run `(cd src/UniXNU && python3.11 ./unixnu tree -D ../UniXNU-gta4l)`.

# 2. Recreate the toolchain shims.
mkdir -p out/gta4l/ipados-17.7.11-21H461/gta4l/toolchain
for t in clang lld-link llvm-lib llvm-rc; do
  ln -sf "/usr/bin/$t" "out/gta4l/ipados-17.7.11-21H461/gta4l/toolchain/$t"
done

# 3. Build BaseTools from the feed if GenFw is absent.
make -C src/UniXNU/out/feeds/mu_basecore-8f10084c/BaseTools/Source/C -j"$(nproc)"

# 4. Run the section 3 command from src/Mu-Silicium.
```

To keep the historical `Build/` untouched, bind-mount a scratch directory over
it (section 4). The module hashes in this replay match at the historical
absolute path (the `<user>/<volume>` path recorded in the tested log) because
the PDB path is embedded in every PE. A build at another path is expected to
produce identical code and data but different PE debug paths, so compare
`objcopy --strip-debug` images or the section contents, not the raw PE hashes.
The local build at another path also used a different compiler, so it did not
isolate the path contribution.

Verification after the build:

```sh
sha256sum Build/gta4lPkg/DEBUG_CLANGPDB/AARCH64/DxeCore.efi
# f4aefa943027a890f6fbc4381d0b7d6f5f9b1c4adb5584f2f71e0002ab3e35ca

# TESTED_LIST is out/gta4l/firmware-replay/tested-mods.sha256 from the
# historical host (or the archived copy on the analysis host).
TESTED_LIST=${TESTED_LIST:-/path/to/tested-mods.sha256}
diff <(sort "$TESTED_LIST") \
     <(cd Build/gta4lPkg/DEBUG_CLANGPDB/AARCH64 && \
       find . -name '*.efi' -print0 | sort -z | xargs -0 sha256sum | sed 's|  \./|  |')
# only the PcdDxe DEBUG/OUTPUT/top-level entries differ
```

The expected `PcdDxe` GUID is the only permitted difference. If any other
module differs, the source, feed, toolchain or path is wrong.

## 8. Notes and limits

- The tested log's `Build start time` line reads `20:56:08`, the same value as
  the earlier intermediate log; the `Build end time` line and the FD mtime both
  read `22:04:25 +0300`, and the total is `00:04:14`. The end time and the
  artifact comparison are the evidence; the start line is not used.
- The earlier logs are not this build. `out/gta4l/logs/firmware.log`
  (end `16:40:13`) was built with `ENABLE_SECUREBOOT=0`; the
  `src/UniXNU/out/gta4l/logs/firmware.log` (end `21:19:19`) used the older
  `src/UniXNU/out/gta4l/tree` layout (no `ipados-17.7.11-21H461` level) and the
  `mu_basecore-89034cbb` feed. The tested FD is the
  `ipados-17.7.11-21H461/gta4l` log ending `22:04:25`.
- The `Mu_Basecore` submodule `3c9c7b2e` and its dirty files are not the
  build's edk2; the clean `cfb9372` feed is. The submodule's dirty diff differs
  between the local checkout and the historical host, and neither state is an
  input to this build.
- `Binaries` is a required submodule and contributes prebuilt DXE modules to
  the volume. The inner-FV comparison covers those bytes; the source pins table
  records the gitlink.
- The evidence image's gzip kernel decompresses to a 192-byte BootShim followed
  by the outer FV; `tail -c 0x300000` on the boot image is not the FD. Use
  `scripts/extract-gta4l-firmware.py` or `boot.json`'s `fd_sha256`.
- No device operation was performed. The SDHC decision remains NO-GO.

## 9. References

- `docs/firmware-handoff.md` — booted-image FV chain and the three prebuilt
  driver traces
- `docs/diagnostic-payload-input-inventory.md` — host input inventory and the
  corrected firmware next step
- `docs/kernel-build-recipe.md` — kernel inputs and stage replays
- `docs/sdhc-decision-memo.md` — the NO-GO decision
- `out/gta4l/firmware-replay/` — tested and replay logs, module hash lists,
  `BuildOptions.tested` and `FVMAIN.Fv.txt.tested` (host artifacts, not
  committed)
