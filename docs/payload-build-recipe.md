# gta4l diagnostic payload build recipe and replay

Status: host-only payload build replay taken on 2026-10-30 on the historical
build host. No device, flash, boot, power or storage operation was performed.
The SDHC decision remains **NO-GO** (`sdhc-decision-memo.md`).

This document pins the inputs of the raw 80 MiB diagnostic payload carried by
the audit image and replays the payload build on the host that produced it.
The replay starts from the historical prelinked kernel; the kernel compile and
link are replayed separately in `kernel-build-recipe.md`.

Tags: `[S]` source- or artifact-established, `[C]` conditional inference,
`[U]` not established.

## 1. Result

| Stage | Replay | Result |
| --- | --- | --- |
| Inputs | audit board snapshot, historical prelinked kernel, RAM-root fixture, trust cache, chip ID | input hashes match `[S]` |
| Kernel patch | historical host, clang 22.1.8 | patched kernel `6e81598c...` byte-identical `[S]` |
| ADT | same DTB, board and chip ID | 106 of 107 properties match; the 64-byte `random-seed` differs `[S]` |
| FAT volume | mformat 4.0.49 with `SOURCE_DATE_EPOCH=1793180892` | volume-label timestamp pinned; every other FAT byte matches `[S]` |
| Clean payload | full wrapper build | `1d15ea68...`; exactly 64 bytes differ from the historical `31b78bec...` `[S]` |
| Seed-normalized payload | clean build with the historical seed substituted | byte-identical to `31b78bec...` `[S]` |
| Packaging | seed-normalized payload against the v5 baseline | byte-identical to the historical audit v2 `a34519c6...` `[S]` |
| Clean packaging | clean payload against the v5 baseline | `037ba650...`; 84 bytes differ (20-byte image ID and 64-byte seed); gates pass `[S]` |
| Controlled test build | second fresh clean build, both gates, separate normalized copy | clean payload `c697903b...`, clean boot `60ad9d05...`; normalized payload `31b78bec...` and boot `a34519c6...` byte-identical `[S]` |

The clean build is not bit-exact: the ADT contains a fresh 64-byte
`random-seed`, and the pinned UniXNU has no seed input. The seed-normalized
comparison substitutes the historical seed after the build; it proves that
every other payload byte reproduces. A supported seed pin is still missing for
a clean bit-exact build.

## 2. Pinned inputs

| Item | Value |
| --- | --- |
| Host | Arch Linux, Linux 7.1.8-arch1-3, x86_64; Python 3.11.13 |
| Device repository | `src/UniXNU-gta4l`, HEAD `e2d0cfc08c0c0119f3c55d1bcb6f9b8323893961` on the historical host |
| Board snapshot | `../../out/gta4l/ram-root-sdhci-audit-evidence/board.payload.toml`, SHA-256 `87ba8d28d8a23fd55e90f2230477b05f7750a81e5af3226d058c8d0978f9fdd3`; `size_mib=80`, `packed_data_ramdisk=false`, volume `GTA4LESP` |
| Wrapper | `scripts/build-gta4l-diagnostic-payload.py`, SHA-256 `6ec96d4aa444bf1dd031b009da30ecc944269984669c1b21ac44a4a7104dbda4` (this revision; adds `--kernel` and appends the diagnostic argument last) |
| Payload contract | `scripts/check-gta4l-payload.py`, SHA-256 `3c5a5723ea1cc85946dbd48bb916b2aa2e2fcb9ca584dc49caee598a6bd62930` |
| UniXNU | `../UniXNU`, HEAD `6ee4ada8a82f5bd2d9032b4d7cf033e878e0551f`; tree HEAD the same |
| Mu-Silicium | `../Mu-Silicium`, HEAD `e14626a3757007a2dbc902d3db323620d5568923` |
| Device tree | `dts/gta4l.dts`, SHA-256 `298dbed9f8b3a1e5941def1c29118e361f80007a2fc50564d676c220a0694a22` |
| Prelinked kernel | `../../out/gta4l/ram-root-sdhci-audit-evidence/kernel.prelinked.macho`, 51,077,120 bytes, SHA-256 `172951570ac0ccbdd8fac87dc089001adc9971cd476122ec8f1a9f685e102e3a` |
| Data ramdisk | `../../out/gta4l/ram-root-sdhci-audit-evidence/root.img`, 57,344 bytes, SHA-256 `77d5eba0903a0491118d58150047370aeed2f1cf65636bb7b275f8c23d6f81b4` |
| Trust cache | `../../out/gta4l/ram-root-sdhci-audit-evidence/trustcache.bundle`, 56 bytes, SHA-256 `e146717a15fada48fd6ab06b0de9f179e2a6dd9e9a1dc3d86c1e56804bac8a6d` |
| Chip ID | `0x1122334455667788`, the historical placeholder, not a device secret |
| Diagnostic | `audit`, argument `gta4l-sdhci-audit=1` |
| Extra arguments | `rd=md0`, `sheng-pmgr=2` |
| Payload size | 80 MiB; raw `N4KROOT` data ramdisk |
| FAT timestamp | `SOURCE_DATE_EPOCH=1793180892` (2026-10-28 09:48:12 UTC, the historical mformat time) |
| Kernel-patch compiler | clang 22.1.8 |
| Device-tree compiler | dtc 1.8.1 |
| FAT tools | GNU mtools 4.0.49 |

Paths in this table and the commands are relative to the device repository
unless noted; `../../out` is the workspace output tree.

## 3. Build command

Run from the device repository. The wrapper copies the device directory to a
temporary tree, forces `payload.size_mib=80` and
`payload.adt.packed_data_ramdisk=false`, invokes `unixnu build payload`, runs
the payload contract and writes the manifest.

```sh
REPLAY=$(mktemp -d /tmp/opencode/payload-replay.XXXXXX)
mkdir -p "$REPLAY/device"
git archive e2d0cfc08c0c0119f3c55d1bcb6f9b8323893961 |
  tar -x -C "$REPLAY/device"
cp ../../out/gta4l/ram-root-sdhci-audit-evidence/board.payload.toml \
  "$REPLAY/device/board.toml"

SOURCE_DATE_EPOCH=1793180892 python3.11 -B \
  scripts/build-gta4l-diagnostic-payload.py \
  --device "$REPLAY/device" \
  --unixnu ../UniXNU \
  --uefi ../Mu-Silicium \
  --kernel ../../out/gta4l/ram-root-sdhci-audit-evidence/kernel.prelinked.macho \
  --data-ramdisk ../../out/gta4l/ram-root-sdhci-audit-evidence/root.img \
  --trust-cache ../../out/gta4l/ram-root-sdhci-audit-evidence/trustcache.bundle \
  --owner-trust-cache \
  --unique-chip-id 0x1122334455667788 \
  --diagnostic audit \
  --kernel-arg rd=md0 \
  --kernel-arg sheng-pmgr=2 \
  --output "$REPLAY/payload.img" \
  --manifest "$REPLAY/payload.manifest.json"

PAYLOAD_SHA=$(python3.11 -c \
  'import json,sys; print(json.load(open(sys.argv[1]))["output"]["sha256"])' \
  "$REPLAY/payload.manifest.json")
```

The extraction directory must exist before `tar` writes into it. The archived
revision is the historical device-tree commit; the board snapshot is a
gitignored evidence input and is copied over the extracted `board.toml`.
`PAYLOAD_SHA` is the hash of this build. A clean build draws a fresh ADT seed,
so `1d15ea68...` identifies the recorded replay only; later builds produce a
different payload hash.

The resulting `unixnu build payload` command is:

```text
unixnu build payload -D <temp-device> --uefi <Mu-Silicium>
  --kernel <kernel.prelinked.macho> --unique-chip-id 0x1122334455667788
  --data-ramdisk <root.img> --owner-trust-cache <trustcache.bundle>
  --kernel-arg rd=md0 --kernel-arg sheng-pmgr=2
  --kernel-arg gta4l-sdhci-audit=1
```

The diagnostic argument is appended after the extra arguments. The historical
`unixnu.toml` uses the same order; the earlier wrapper order (diagnostic first)
produced a 34-byte difference in `unixnu.toml` and the payload.

## 4. Replay observations

### 4.1 Kernel patch

The payload replay uses the historical prelink output as its kernel input. The
kernel compile and link are replayed separately in `kernel-build-recipe.md`,
and the prelink output is regenerated from the reproduced kernel. The payload
step copies the input and runs
`scripts/patch-xnu-entry-probe.py --panic-screen --no-reset`.

| Host | clang | Patched kernel SHA-256 | Comparison |
| --- | --- | --- | --- |
| Historical build host | 22.1.8 | `6e81598cd143c73f47b8d160433d87d84ca035f599187c334241762d4055854a` | byte-identical to the historical payload kernel `[S]` |
| Analysis host | 21.1.8 | `63e37bff799cf19282695061e3a3a6ebda46d771f0bd892b44e44180eadec337` | 73 bytes differ `[S]` |

The patch script and input hashes are the same on both hosts. The difference is
in the generated panic-screen code, so the compiler is an input to this stage.
The historical host's clang 22.1.8 reproduced the historical patched kernel;
clang 21.1.8 did not. Other compiler versions were not tested, so the exact
compiler requirement is not fully bounded.

### 4.2 ADT

The ADT is 13,344 bytes and 107 properties. The clean build's ADT SHA-256 is
`941669fd50a242761039926c92e1a85bce35ed4c2792319a7bb88ffe39f586d2`; the
historical ADT is
`2c0c4f737dc43abbc5c8b86a441bff172126a50b74b33288945032e8d9a5ec2a`. The only
property difference is `[0]/random-seed`:

- historical: `81ba59a42867c32676a453d7b53090b45dbed9e52de48b2ade4e7c3f41e88a9ecaa2bc983574099020c8379892bc125e766050573768e3582d88761dcace44b9`
- clean replay: `02cef6e08985ab06ccfbc46ffe19ce2ced4f49b6c016106d194b94e82378b3005062b7abe4e807a780d9cdd2f6dffc4f7fe458767700aaeae726cc51673c7183`

`scripts/unixnu/devicetree.py:300` generates the seed with `os.urandom(64)`.
Every other property, including `unique-chip-id` `8877665544332211`, `chip-id`
`44040000`, the memory ranges and the ramdisk/trust-cache placeholders, matches.

### 4.3 FAT volume

`mformat` sets the volume-label directory entry's create, access and write
timestamps to the current time. The historical entry is 2026-10-28 09:48:12
UTC. mtools 4.0.49 honours `SOURCE_DATE_EPOCH`, and UniXNU's `write_volume`
sets `TZ=UTC` and inherits the environment, so
`SOURCE_DATE_EPOCH=1793180892` pins the entry to the historical value.

Without the pin the clean build used 2026-10-30 11:16:14 UTC and the payload
differed in 7 bytes at the volume-label entry. All file entries use the fixed
1980-01-01 epoch set by `scripts/unixnu/payload.py`.

### 4.4 Payload comparison

| Artifact | SHA-256 | Difference |
| --- | --- | --- |
| Historical payload | `31b78bec7529706407f682e64dbc9c4c990e86009025174fd3f791606b29ea31` | reference |
| Clean replay payload | `1d15ea68cd570382ed4ba7b426aa4fb29cf5c303124a289af16451248a8975ff` | exactly 64 bytes at `0x30e3b78..0x30e3bb7`, the ADT seed |
| Controlled clean payload | `c697903b74279b6cadf364b94871a6125b42229cbdedf250bfcb62331b8d3d4c` | exactly 64 bytes at `0x30e3b78..0x30e3bb7`, the ADT seed |
| Seed-normalized payload | `31b78bec7529706407f682e64dbc9c4c990e86009025174fd3f791606b29ea31` | `cmp` identical to the historical payload |

Two independent clean builds drew different seeds (`02cef6e0...` and
`c50534aa...`) and both differ from the historical payload only in those 64
bytes. Normalizing either build reproduces `31b78bec...`.

The extracted `kernel.macho`, `kernel.macho.trustcache`, `scratch-data.img` and
`unixnu.toml` hashes match the historical payload. The seed-normalized payload
is produced by `scripts/normalize-gta4l-adt-seed.py`, which writes it only
after requiring `random-seed` to be the only differing ADT property and the
result to be byte-identical to the historical payload; it is not the output of
a clean build.

### 4.5 Packaging comparison

| Payload | Boot image SHA-256 | Comparison | Boot gate | Payload gate |
| --- | --- | --- | --- | --- |
| Seed-normalized | `a34519c66820eb5f1f4c4790be5e10233ffd4c8a4ece2df4768e7dca151c8546` | `cmp` identical to historical audit v2 | 18/18 | 7/7 |
| Clean | `037ba650847caa0cb32ac14a99c13736ae2ae6196700344531a224590c6694eb` | 84 bytes differ: 20-byte image ID at `0x240..0x253` and 64-byte seed at `0x329bb78..0x329bbb7` | 18/18 | 7/7 |
| Controlled clean | `60ad9d056b439a2c53f82dddcefc6ac9ec0df0a5b323ff9cdcebc3d76e15e8f8` | 84 bytes differ: derived image ID and the fresh seed | 18/18 | 7/7 |

The packager copies the v5 header page (recomputing only the image ID),
firmware + BootShim, boot kernel section and appended boot DTB, and replaces
only the ramdisk. The clean image's header page excluding the ID still matches
the v5 baseline, and the boot gate passes with the clean payload hash as the
expected ramdisk hash.

## 5. What this establishes

- `[S]` On the historical host, with the historical prelinked kernel, clang
  22.1.8, the audit board snapshot, the RAM-root fixture, the trust cache and
  the chip ID, the payload build reproduces every payload byte except the
  64-byte ADT `random-seed`. The clean payload is `1d15ea68...`; the historical
  payload is `31b78bec...`.
- `[S]` Substituting the historical seed yields a byte-identical payload, and
  packaging it against the v5 baseline reproduces the historical audit v2 image
  `a34519c6...` byte-for-byte. Both gates pass.
- `[S]` A second controlled clean build (`c697903b...`, seed `c50534aa...`)
  passed the payload and boot gates and produced clean boot `60ad9d05...`.
  Normalizing that independent build again reproduced `31b78bec...` and
  `a34519c6...` byte-for-byte, so the seed was the only payload variable across
  two clean builds.
- `[S]` The kernel patch stage is compiler-sensitive: clang 22.1.8 reproduces
  `6e81598c...`; clang 21.1.8 produces `63e37bff...` with 73 differing bytes.
- `[S]` The FAT volume-label timestamp was the only FAT non-determinism
  observed in this replay; `SOURCE_DATE_EPOCH=1793180892` pins it to the
  historical value.
- `[C]` A clean bit-exact payload build requires a supported ADT random-seed
  input. The pinned UniXNU has none; this replay substituted the seed after the
  build and labels the result seed-normalized.
- `[U]` A bit-exact Mu-Silicium outer FD remains not reproduced (`firmware-build-recipe.md`).

## 6. Gates

The packager and the boot-image gate use the hash of the current build:

```sh
python3.11 -B scripts/check-gta4l-payload.py \
  --payload "$REPLAY/payload.img" --expect-diagnostic audit --json

python3.11 -B scripts/package-gta4l-diagnostic.py \
  --baseline ../../out/gta4l/boot/boot-ram-root-sdhci-v5.img \
  --payload "$REPLAY/payload.img" \
  --payload-sha256 "$PAYLOAD_SHA" \
  --expect-diagnostic audit \
  --output "$REPLAY/boot.img" \
  --manifest "$REPLAY/boot.json"

python3.11 -B scripts/check-gta4l-boot-image.py \
  --baseline ../../out/gta4l/boot/boot-ram-root-sdhci-v5.img \
  --candidate "$REPLAY/boot.img" \
  --expected-ramdisk-sha256 "$PAYLOAD_SHA" \
  --json
```

For the recorded seed-normalized comparison, use the fail-closed normalizer.
It extracts both ADTs, requires `random-seed` to be the only differing
property and exactly 64 bytes, substitutes the reference seed at its structural
offset, and writes the output only if the result is byte-identical to the
reference. It also refuses an existing `--output` and candidate/reference
aliases, and creates the output exclusively so a concurrent writer cannot be
clobbered; the normalized copy must use a new path:

```sh
python3.11 -B scripts/normalize-gta4l-adt-seed.py \
  --candidate "$REPLAY/payload.img" \
  --reference ../../out/gta4l/ram-root-sdhci-audit-evidence/payload.img \
  --output "$REPLAY/payload.seed-normalized.img" \
  --json

python3.11 -B scripts/package-gta4l-diagnostic.py \
  --baseline ../../out/gta4l/boot/boot-ram-root-sdhci-v5.img \
  --payload "$REPLAY/payload.seed-normalized.img" \
  --payload-sha256 31b78bec7529706407f682e64dbc9c4c990e86009025174fd3f791606b29ea31 \
  --expect-diagnostic audit \
  --output "$REPLAY/boot.seed-normalized.img" \
  --manifest "$REPLAY/boot.seed-normalized.json"
```

The normalized boot image must `cmp` equal the historical audit v2 image
`a34519c6...`. This is a post-build comparison, not a clean build and not a
payload input.

The host-only checks for this recipe are:

```sh
python3.11 -B scripts/test-gta4l-diagnostic-payload.py
python3.11 -B scripts/normalize-gta4l-adt-seed.py --self-test
```

## 7. Notes and limits

- The replay starts from the historical prelinked kernel. The kernel compile
  and link are replayed separately (`kernel-build-recipe.md`).
- The clean build is not bit-exact. The seed-normalized result is a post-build
  substitution, not a clean build, and is labelled as such.
- The wrapper's `--kernel` option and the diagnostic-argument order are part
  of this revision. The historical `unixnu.toml` is the reference for the
  argument order.
- UniXNU's `write_volume` does not set `SOURCE_DATE_EPOCH` by default. In this
  replay, a build without the pin differed in 7 bytes at the volume-label
  entry.
- The replay overwrites the shared output tree's `payload/` and
  `device-tree/hardware.dtb`; preserve and restore them if the historical host's
  output tree must remain unchanged.
- Retained replay evidence on the historical host:
  `/tmp/payload-replay-audit-pinned.img` (clean, `1d15ea68...`),
  `/tmp/payload-replay-audit-pinned.manifest.json`,
  `/tmp/payload-replay-audit-seedpinned.img` (normalized, `31b78bec...`),
  `/tmp/boot-replay-audit-seedpinned.img` (audit v2, `a34519c6...`) and
  `/tmp/boot-replay-audit-clean.img` (`037ba650...`). The preserved pre-replay
  output tree is `/tmp/payload-replay-preserve` and
  `/tmp/payload-replay-preserve2`.
- The controlled test build is preserved in
  `../../out/gta4l/payload-testbuild-evidence/` on the historical host: the
  clean payload and boot image, the normalized payload and boot image, all
  manifests, `build.log`, `README.md`, `provenance.json`, `tools/` and the
  `SHA256SUMS` inventory. The inventory is checked with
  `sha256sum -c SHA256SUMS`; its own SHA-256 is
  `feaca77d8b28848e30d3a2c815ecb0285210b7543440572b9d1620f8f0f76246`, so the
  host-state directory can be checked against this recipe. The build log was
  captured at `/tmp/opencode/host-testbuild.log` and copied there; the scratch
  build tree is `/tmp/payload-testbuild`. These paths are host state, not
  tracked artifacts.
- The current normalizer revision is
  `cf80caafdd3b56fa590fed208e17e2eb8f5c27be1451dbaf1c57b0bbb7f76cf5`. It adds
  exclusive output creation to the preserved `3e54b3c2...` guards without
  changing the normalization logic; re-running it against the preserved
  artifacts again produced `31b78bec...`, and an existing output was rejected
  without modification. The evidence bundle keeps the tool revision that was
  current when the bundle was sealed.
- No device, flash, boot, power or storage operation was performed.

## 8. References

- `docs/diagnostic-payload-input-inventory.md` — payload and boot-image inputs
  and gates
- `docs/kernel-build-recipe.md` — prelinked-kernel inputs and stage replays
- `docs/firmware-build-recipe.md` — Mu-Silicium build and module replay
- `docs/ram-root-fixture-reproducibility.md` — RAM-root fixture inputs and
  replay
- `docs/sdhci-handoff-audit.md` — audit image provenance and hardware result
- `scripts/normalize-gta4l-adt-seed.py` — fail-closed seed-normalized
  comparison
- `scripts/test-gta4l-diagnostic-payload.py` — host-only wrapper tests
