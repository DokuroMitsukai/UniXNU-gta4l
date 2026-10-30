# RAM-root fixture reproducibility

Status: host-only reproduction `[S]`. The `fs-timer` fixture artifacts were
rebuilt on Linux and match the recorded bytes exactly. This is not a boot
image, kernel, tablet, authenticated-root or power result.

## 1. Scope

The historical artifacts are the gitignored outputs of
`scripts/build-ram-root-report-fixture.py --diagnostic fs-timer`. The signed
fixture, trust cache and `N4KROOT` image were used by the hardware-passed
fs/timer diagnostic (`out/gta4l/ram-root-fs-timer-evidence/README.md`);
`user.o` and `launchd-static.macho` were retained in
`out/gta4l/ram-root-sdhci-v5-evidence/`.

The builder compiles `scripts/qemu-entry/native-user.S` with
`-DNATIVE_RAM_ROOT_FS_TIMER=1`, links it as a static arm64 Mach-O, applies the
iOS 17.0 build version with `vtool`, signs ad-hoc with `rcodesign`, parses the
SHA-256 CodeDirectory and verifies every signed page, writes the scoped
trust-cache bundle and the `N4KROOT` image with the deterministic
`/probe.bin`, and records `fixture.json`.

Paths in this document are relative to the workspace root
(`taba7unixnu/`). The evidence directories are gitignored and are not
vendored.

## 2. Reproduced artifacts

| Artifact | Historical path | SHA-256 |
| --- | --- | --- |
| Compiled object | `out/gta4l/ram-root-sdhci-v5-evidence/user.o` | `6984333d83feb8e02ee71c98f3ccf90cd75766814f270a597f859ad8c6152490` |
| Static Mach-O | `out/gta4l/ram-root-sdhci-v5-evidence/launchd-static.macho` | `2056577cea4fe278ca47c0b78eb5a536364a9d181e95c6627fafaa40cf0aec6a` |
| Signed Mach-O | `out/gta4l/ram-root-fs-timer-evidence/launchd.macho` | `b316e44f0c2a7a29d6d798763559a0756c4edea233f21a45dd812d7112450469` |
| Trust-cache bundle | `out/gta4l/ram-root-fs-timer-evidence/trustcache.bundle` | `e146717a15fada48fd6ab06b0de9f179e2a6dd9e9a1dc3d86c1e56804bac8a6d` |
| Trust-cache record | `out/gta4l/ram-root-fs-timer-evidence/trustcache.json` | `9e90952db6afcdedbc33c6599e8235c01b21b6cdd236c8d1a8fbd890dec1e721` |
| Root image | `out/gta4l/ram-root-fs-timer-evidence/root.img` | `77d5eba0903a0491118d58150047370aeed2f1cf65636bb7b275f8c23d6f81b4` |
| Symbol addresses | `out/gta4l/ram-root-fs-timer-evidence/addresses.json` | `0dbf7886548975790ee3c739df4acb44ad5f0cd84d53c6e49384fc6a65927215` |
| Fixture record | `out/gta4l/ram-root-fs-timer-evidence/fixture.json` | `37cbdd0bd4eb18baeb602f60bff908a8fcec142e784dc1dcbaa2d58e0ef4dd37` |

`fixture.json` is byte-identical, including the unsigned/signed hashes, the
CDHash `b1a0da729f7704a9cd2dada7a05ebe8229b6f2d4`, the 9 verified pages and
the root/trust-cache hashes.

## 3. Host and tools

| Item | Value |
| --- | --- |
| Host | AlmaLinux 9, x86_64, glibc |
| Python | 3.11.13 (`python3.11 -B`) |
| Compiler | system clang 21.1.8, `llvm-config` 21.1.8 |
| Linker and Mach-O tools | cctools-port `1030.6.3-ld64-956.6` (ld64-956.6, `vtool`, `nm`, `lipo`, `libtool`) |
| Signer | `rcodesign` 0.29.0, `x86_64-unknown-linux-musl` |
| `rcodesign` release tarball | SHA-256 `dbe85cedd8ee4217b64e9a0e4c2aef92ab8bcaaa41f20bde99781ff02e600002` |
| `xcrun` | `scripts/linux/xcrun` shim |

`scripts/linux/setup-toolchain.py` pins the cctools tag
(`CCTOOLS_PORT_REF`) and is the authoritative source. Its full run also builds
a patched LLVM, `mig`, `iig` and `lzfse` for the kernel; the fixture does not
use those and needs only a clang plus cctools' `ld`, `vtool` and `nm`. The
Linux `linux_env()` used by the builder requires every toolchain path to
exist, so the unused tools are symlinked to `/usr/bin/true`.

## 4. Recipe

Run from `src/UniXNU`. First confirm the host dependencies:

```sh
python3.11 scripts/linux/setup-toolchain.py /tmp/opencode/toolchain-linux \
  --package-manager none --check
```

Build the pinned cctools tools. The helper imports the supported installer
and calls only its `cctools()` step:

```python
#!/usr/bin/env python3
import importlib.util
from pathlib import Path

SRC = Path('/path/to/taba7unixnu/src/UniXNU')
PREFIX = Path('/tmp/opencode/toolchain-linux')
WORK = Path('/tmp/opencode/cctools-work')

spec = importlib.util.spec_from_file_location(
    'setup_toolchain', SRC / 'scripts/linux/setup-toolchain.py')
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

clt = PREFIX / 'CommandLineTools/usr'
include = clt.parent / 'SDKs/MacOSX.sdk/usr/include'
WORK.mkdir(parents=True, exist_ok=True)
mod.cctools(clt, include, WORK, 8, mod.host_tools())
```

```sh
python3.11 /tmp/opencode/build-cctools.py
/tmp/opencode/toolchain-linux/CommandLineTools/usr/bin/ld -v
```

The second command reports `PROJECT:ld64-956.6`. Download and verify
`rcodesign` 0.29.0:

```sh
mkdir -p /tmp/opencode/rcodesign-linux && cd /tmp/opencode/rcodesign-linux
curl -fsSL -o rcodesign.tar.gz \
  https://github.com/indygreg/apple-platform-rs/releases/download/apple-codesign%2F0.29.0/apple-codesign-0.29.0-x86_64-unknown-linux-musl.tar.gz
curl -fsSL -o rcodesign.tar.gz.sha256 \
  https://github.com/indygreg/apple-platform-rs/releases/download/apple-codesign%2F0.29.0/apple-codesign-0.29.0-x86_64-unknown-linux-musl.tar.gz.sha256
printf '%s  rcodesign.tar.gz\n' "$(cat rcodesign.tar.gz.sha256)" | sha256sum -c -
tar -xzf rcodesign.tar.gz
chmod +x apple-codesign-0.29.0-x86_64-unknown-linux-musl/rcodesign
```

The release `.sha256` asset is a bare digest, so the filename is supplied to
`sha256sum -c`. Create the shim toolchain:

```sh
TC=/tmp/opencode/fixture-toolchain
mkdir -p "$TC/llvm/bin" "$TC/CommandLineTools/usr/bin" \
  "$TC/CommandLineTools/usr/libexec" "$TC/XcodeDefault.xctoolchain/usr/bin" "$TC/bin"
ln -sf "$(command -v clang)" "$TC/llvm/bin/clang"
ln -sf /tmp/opencode/toolchain-linux/CommandLineTools/usr/bin/ld "$TC/CommandLineTools/usr/bin/ld"
ln -sf /tmp/opencode/toolchain-linux/CommandLineTools/usr/bin/vtool "$TC/CommandLineTools/usr/bin/vtool"
ln -sf /tmp/opencode/toolchain-linux/CommandLineTools/usr/bin/nm "$TC/CommandLineTools/usr/bin/nm"
ln -sf /usr/bin/true "$TC/CommandLineTools/usr/bin/xnu-host-cc"
ln -sf /usr/bin/true "$TC/CommandLineTools/usr/libexec/migcom"
ln -sf /usr/bin/true "$TC/XcodeDefault.xctoolchain/usr/bin/iig"
ln -sf /usr/bin/true "$TC/bin/lzfse"
cp scripts/linux/xcrun "$TC/bin/xcrun"
chmod +x "$TC/bin/xcrun"
```

Build the fixture against the gta4l output tree:

```sh
python3.11 -B scripts/build-ram-root-report-fixture.py \
  --output /tmp/opencode/fixture-fs-timer-linux \
  --tree out/gta4l/ipados-17.7.11-21H461/gta4l/tree \
  --toolchain "$TC" \
  --rcodesign /tmp/opencode/rcodesign-linux/apple-codesign-0.29.0-x86_64-unknown-linux-musl/rcodesign \
  --diagnostic fs-timer
```

Compare every output:

```sh
cd /path/to/taba7unixnu
NEW=/tmp/opencode/fixture-fs-timer-linux
HIST=out/gta4l/ram-root-fs-timer-evidence
cmp "$NEW/user.o" out/gta4l/ram-root-sdhci-v5-evidence/user.o
cmp "$NEW/launchd-static.macho" out/gta4l/ram-root-sdhci-v5-evidence/launchd-static.macho
for f in launchd.macho trustcache.bundle trustcache.json root.img addresses.json fixture.json; do
  cmp "$NEW/$f" "$HIST/$f" || exit 1
done
sha256sum "$NEW"/*
```

The builder prints benign warnings for the unknown Apple-only
`-Wno-unused-but-set-global` option, the missing
`-F/System/Library/Frameworks/` directory and the 4-byte alignment of `_start`
and `_checkpoint`; the recorded bytes include their effect and still match.

## 5. Mac cross-check

The same source was also built on a macOS 27.0.1 arm64 host with the Command
Line Tools clang, `ld` and `vtool`. `user.o` was byte-identical
(`6984333d…`), but the link/`vtool` stage produced unsigned `ae96f406…` and
signed `ffc291f8…`, not the historical `dd772397…`/`b316e44f…`. The current
Apple linker layout therefore does not reproduce the historical fixture; the
pinned cctools-port ld64-956.6 does. The Linux recipe above is the recorded
reproduction path.

## 6. Limits

- Only the `fs-timer` fixture is covered. The `report` diagnostic is a
  different source configuration and was not re-verified.
- The boot image `c12e601e…`, the kernel `dc8184da…` and the hardware run are
  recorded results, not products of this recipe.
- The signature is ad-hoc (`apple_trust_chain_verified=false`); the trust
  cache is scoped to this fixture's CDHash and is not an Apple trust result.
- The compiler is an input. `user.o` matched both the macOS Command Line Tools
  clang and Linux clang 21.1.8; another clang version may emit different
  bytes. The exact versions above are part of the reproduction.
- cctools-port and `rcodesign` are third-party builds pinned by tag and release
  digest; the cctools source is fetched from GitHub at that tag.
- `/tmp/opencode/` artifacts are volatile and are not vendored. Rebuild them
  with this recipe before relying on the comparison.
- The recipe is host-only: it does not build, flash, boot or access a device.
