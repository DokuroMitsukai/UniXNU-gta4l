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

The build uses the builder defaults `--identifier org.blu-sharky.ram-root-report`
and `--test-size 12288`; only `--diagnostic fs-timer` is passed explicitly.

## 3. Host, tools and pinned inputs

| Item | Value |
| --- | --- |
| Host | AlmaLinux 9, x86_64, glibc |
| Python | 3.11.13 (`python3.11 -B`) |
| Compiler | `/usr/bin/clang-21`, clang 21.1.8; `llvm-config` 21.1.8 |
| Linker and Mach-O tools | cctools-port tag `1030.6.3-ld64-956.6`, commit `904de2a71d4da6a9b30d2efaf912a10ddc7d9ddb`; ld64-956.6, `vtool`, `nm`, `lipo`, `libtool` |
| libdispatch | `swift-6.4.0-RELEASE`, commit `024805abec6daa9cdda569fd736b9c7686dfd735` |
| Signer | `rcodesign` 0.29.0, `x86_64-unknown-linux-musl` |
| `rcodesign` release tarball | SHA-256 `dbe85cedd8ee4217b64e9a0e4c2aef92ab8bcaaa41f20bde99781ff02e600002` |
| `xcrun` | `scripts/linux/xcrun` shim |

The shared UniXNU checkout used for the reproduction is commit
`6ee4ada8a82f5bd2d9032b4d7cf033e878e0551f` (detached HEAD, clean tree). The
device repository's `docs/build-revisions.md` retains a separate development
integration pin; the hashes below identify the exact source bytes used here.

| Input | SHA-256 |
| --- | --- |
| `scripts/build-ram-root-report-fixture.py` | `3dd4fa08576a20021569b0e1afc66e264f422827e33a25fe36d5e85cbd1c3c64` |
| `scripts/qemu-entry/native-user.S` | `1e29e9405c670a7a65a222cf48a590497b10ba7f106b202909c2b99e179fe76f` |
| `scripts/unixnu/kernel.py` | `b9b6e572f3ff9d7f62d7c10db9913b8993287ea41dbbb50011d13ee17f0f3271` |
| `scripts/linux/setup-toolchain.py` | `9334e7994a64a0a5f59b40b8f36e08fd9eac08684c7ae8bce8d4afedd279c963` |
| `scripts/linux/xcrun` | `8da8a494abd47a6d484c217ac074c1759792732f048ffce3f65c114c5d641576` |

`scripts/linux/setup-toolchain.py` pins the cctools and libdispatch tags
(`CCTOOLS_PORT_REF`, `LIBDISPATCH_REF`) and is the authoritative source. Its
full run also builds a patched LLVM, `mig`, `iig` and `lzfse` for the kernel;
the fixture does not use those and needs only a clang plus cctools' `ld`,
`vtool` and `nm`. The Linux `linux_env()` used by the builder requires every
toolchain path to exist, so the unused tools are symlinked to `/usr/bin/true`.
That stub toolchain is **fixture-only**; it is not a kernel toolchain.

## 4. Recipe

The recipe creates a new empty replay directory and never reuses or deletes an
existing one. Run the blocks below in one shell; the variables persist and
every failure aborts:

```sh
set -eu
SRC=/path/to/taba7unixnu/src/UniXNU
DOC="$SRC/../UniXNU-gta4l/docs/ram-root-fixture-reproducibility.md"
REPLAY=$(mktemp -d /tmp/opencode/repro-fresh.XXXXXX)
test -z "$(ls -A "$REPLAY")"
cd "$SRC"
python3.11 - "$DOC" "$SRC" <<'PY'
import hashlib, pathlib, re, sys
doc = pathlib.Path(sys.argv[1])
root = pathlib.Path(sys.argv[2])
rows = re.findall(r'^\| `(scripts/[^`]+)` \| `([0-9a-f]{64})` \|$', doc.read_text(), re.M)
if len(rows) != 5:
    raise SystemExit(f'expected 5 pinned inputs, found {len(rows)}')
if len({path for path, _ in rows}) != len(rows):
    raise SystemExit('duplicate pinned input paths')
bad = []
for path, want in rows:
    got = hashlib.sha256((root / path).read_bytes()).hexdigest()
    if got != want:
        bad.append((path, got, want))
if bad:
    for path, got, want in bad:
        print(f'{path}: {got} != {want}')
    raise SystemExit('source inputs do not match the pins')
print(f'source inputs: {len(rows)} match')
PY
python3.11 scripts/linux/setup-toolchain.py "$REPLAY/toolchain-linux" \
  --package-manager none --check
```

The source check stops before any build when a pinned input differs.

Build the pinned cctools tools. Save this helper as
`$REPLAY/build-cctools.py`; it takes the replay root and the shared checkout as
arguments:

```python
#!/usr/bin/env python3
import importlib.util
import sys
from pathlib import Path

REPLAY = Path(sys.argv[1])
SRC = Path(sys.argv[2])
PREFIX = REPLAY / 'toolchain-linux'
WORK = REPLAY / 'cctools-work'

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
python3.11 "$REPLAY/build-cctools.py" "$REPLAY" "$SRC"
"$REPLAY/toolchain-linux/CommandLineTools/usr/bin/ld" -v
test "$(git -C "$REPLAY/cctools-work/cctools-port" rev-parse HEAD)" = \
  904de2a71d4da6a9b30d2efaf912a10ddc7d9ddb
test "$(git -C "$REPLAY/cctools-work/swift-corelibs-libdispatch" rev-parse HEAD)" = \
  024805abec6daa9cdda569fd736b9c7686dfd735
```

The linker reports `PROJECT:ld64-956.6`; the two commit checks stop the
recipe if the tags resolve elsewhere.

Download and verify `rcodesign` 0.29.0 in a subshell so the working directory
does not change:

```sh
mkdir -p "$REPLAY/rcodesign-linux"
(
  cd "$REPLAY/rcodesign-linux"
  curl -fsSL -o rcodesign.tar.gz \
    https://github.com/indygreg/apple-platform-rs/releases/download/apple-codesign%2F0.29.0/apple-codesign-0.29.0-x86_64-unknown-linux-musl.tar.gz
  curl -fsSL -o rcodesign.tar.gz.sha256 \
    https://github.com/indygreg/apple-platform-rs/releases/download/apple-codesign%2F0.29.0/apple-codesign-0.29.0-x86_64-unknown-linux-musl.tar.gz.sha256
  printf '%s  rcodesign.tar.gz\n' "$(cat rcodesign.tar.gz.sha256)" | sha256sum -c -
  printf '%s  rcodesign.tar.gz\n' dbe85cedd8ee4217b64e9a0e4c2aef92ab8bcaaa41f20bde99781ff02e600002 | sha256sum -c -
  tar -xzf rcodesign.tar.gz
  chmod +x apple-codesign-0.29.0-x86_64-unknown-linux-musl/rcodesign
)
```

The first check uses the release asset; the second enforces the pinned digest
from §3, so a changed release asset also stops the recipe. Create the shim
toolchain. The `clang` symlink goes in `CommandLineTools/usr/bin` because
`xcrun` resolves tools from `XNU_TOOLCHAIN_BIN`; the `llvm/bin/clang` entry
only satisfies the `linux_env()` existence check.

```sh
TC="$REPLAY/fixture-toolchain"
mkdir -p "$TC/llvm/bin" "$TC/CommandLineTools/usr/bin" \
  "$TC/CommandLineTools/usr/libexec" "$TC/XcodeDefault.xctoolchain/usr/bin" "$TC/bin"
ln -sf /usr/bin/clang-21 "$TC/llvm/bin/clang"
ln -sf /usr/bin/clang-21 "$TC/CommandLineTools/usr/bin/clang"
ln -sf "$REPLAY/toolchain-linux/CommandLineTools/usr/bin/ld" "$TC/CommandLineTools/usr/bin/ld"
ln -sf "$REPLAY/toolchain-linux/CommandLineTools/usr/bin/vtool" "$TC/CommandLineTools/usr/bin/vtool"
ln -sf "$REPLAY/toolchain-linux/CommandLineTools/usr/bin/nm" "$TC/CommandLineTools/usr/bin/nm"
ln -sf /usr/bin/true "$TC/CommandLineTools/usr/bin/xnu-host-cc"
ln -sf /usr/bin/true "$TC/CommandLineTools/usr/libexec/migcom"
ln -sf /usr/bin/true "$TC/XcodeDefault.xctoolchain/usr/bin/iig"
ln -sf /usr/bin/true "$TC/bin/lzfse"
cp scripts/linux/xcrun "$TC/bin/xcrun"
chmod +x "$TC/bin/xcrun"
```

Confirm that `xcrun` resolves the pinned compiler and version:

```sh
(
  export PATH="$TC/CommandLineTools/usr/bin:$TC/bin:$PATH"
  export XNU_TOOLCHAIN_BIN="$TC/CommandLineTools/usr/bin $TC/XcodeDefault.xctoolchain/usr/bin"
  test "$(xcrun --find clang)" = "$TC/CommandLineTools/usr/bin/clang"
  xcrun clang --version | head -1 | grep -F 'clang version 21.1.8'
)
```

The path check rejects a `PATH` fallback; the version check rejects a
different compiler. Build the fixture against the gta4l output tree; the
defaults supply the identifier and the 12,288-byte test file:

```sh
python3.11 -B scripts/build-ram-root-report-fixture.py \
  --output "$REPLAY/fixture-out" \
  --tree "$SRC/out/gta4l/ipados-17.7.11-21H461/gta4l/tree" \
  --toolchain "$TC" \
  --rcodesign "$REPLAY/rcodesign-linux/apple-codesign-0.29.0-x86_64-unknown-linux-musl/rcodesign" \
  --diagnostic fs-timer
```

Compare every output with `set -e` still active:

```sh
ROOT=/path/to/taba7unixnu
NEW="$REPLAY/fixture-out"
HIST="$ROOT/out/gta4l/ram-root-fs-timer-evidence"
cmp "$NEW/user.o" "$ROOT/out/gta4l/ram-root-sdhci-v5-evidence/user.o"
cmp "$NEW/launchd-static.macho" "$ROOT/out/gta4l/ram-root-sdhci-v5-evidence/launchd-static.macho"
for f in launchd.macho trustcache.bundle trustcache.json root.img addresses.json fixture.json; do
  cmp "$NEW/$f" "$HIST/$f"
done
sha256sum "$NEW"/*
```

Any missing file or `cmp` mismatch returns nonzero and stops the recipe.

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
- The stub toolchain is fixture-only. It is not a kernel toolchain; do not use
  it for `./unixnu build kernel`, which needs the real `mig`, `iig`, `lzfse`,
  `xnu-host-cc` and the patched LLVM.
- The source pins describe the shared checkout at the recorded revision. A
  different revision may change the builder or fixture source and therefore
  the bytes.
- `/tmp/opencode/` artifacts are volatile and are not vendored. Rebuild them
  with this recipe before relying on the comparison.
- The recipe is host-only: it does not build, flash, boot or access a device.

## 7. Replay record

The corrected recipe was replayed on 2026-10-30 in a fresh `mktemp` replay
directory. The five source pins matched, the cctools and libdispatch commits
matched §3, the release tarball matched both its published and pinned digests,
`xcrun` resolved the shim clang at version 21.1.8, and all eight outputs were
byte-identical to the recorded artifacts. The comparison block stopped with a
nonzero status when `user.o` was corrupted.

The enforcement paths were also exercised with the recipe's own blocks: a
wrong source hash, a missing or duplicated input row, a wrong compiler version,
a `PATH` fallback, a different dependency commit, a non-empty replay directory
and empty, missing or duplicated baseline inventories all failed closed.
