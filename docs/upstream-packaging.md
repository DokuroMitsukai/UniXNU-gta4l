# Upstream packaging tooling on gta4l

Status: host-only adoption. No Actions run, image build, flash, boot or device
operation is part of this record. The SDHC decision remains **NO-GO**
(`sdhc-decision-memo.md`).

## Adopted revision

The release flow and the canonical device script were taken from official
UniXNU at `6790e7ce6e77f67a9174fba1255e64eacf1c41ad`, including
`2ced2d7` ("fix(release): pass the board's firmware device tree to the pack
step and the firmware path to preflight scripts").

| File | SHA-256 | Note |
| --- | --- | --- |
| `templates/package.sh` (UniXNU) | `f7b8e3ea043058b9c61d5b45c32b8e7669a85737c89d55d404957c3b43ec4aab` | Byte-identical to upstream |
| `scripts/package.sh` (this repository) | `f7b8e3ea043058b9c61d5b45c32b8e7669a85737c89d55d404957c3b43ec4aab` | Verbatim copy; no board edits |
| `scripts/unixnu/pipeline.py` (UniXNU) | `88a3c93a2e71fc2f3e5c296e5ba5661defc4833ab2a337b6eff3de97e14f5233` | Byte-identical to upstream |
| `scripts/unixnu/profiles.py` (UniXNU) | `6b782336c255a69ab2d786924a8db4156b7f3c646cbf718d397d31824f39a080` | Retains the local arm64-only selection |
| `docs/porting/packaging.md` (UniXNU) | `6dff4a637664adbe1ed264bde66844497b60d3ed3a26efb0e00df19e12adb147` | Upstream text, "uefi-sheng" generalized |

The script reads only `[package] unixnu` itself; every other value comes from
`board.toml`, the profiles and the local build inputs.

## Board values

```toml
[bootimg]
dtb = "build/gta4l/boot.dtb"

[package]
unixnu = "../UniXNU"
boot_partition_bytes = 100663296

[package.defconfig]
"ipados-17.7.11-21H461" = "gta4l_defconfig"
```

`boot_partition_bytes` is the captured SM-T505N BOOT partition size; both
partition snapshots and every diagnostic image in
`out/gta4l/ram-root-sdhci-audit-evidence/` are exactly 100,663,296 bytes. It
is not copied from the example.

`[bootimg] dtb` is staged by the firmware preflight
`scripts/prepare-gta4l-boot-dtb.py` from the Mu-Silicium checkout named by
`UNIXNU_UEFI`. The provenance and byte-exact reproduction are in
`docs/boot-dtb-provenance.md`.

## OS selection contract

- The only installed OS profile is `ipados-17.7.11-21H461`. `--os 17` and the
  full name resolve to it; `--os 26`, `--os 27` and any profile name not
  installed are rejected.
- A profile that declares `[os] arch` other than `arm64` is rejected before
  the build context is assembled. This tree never selects ARM64e.
- No `ipados-26`/`ipados-27` defconfig, override, patch, identity or IPSW is
  added for this board.

## Release artifact limit

The board keeps its 192 MiB packed payload (`[payload] size_mib = 192`). A
previously built image at
`out/gta4l/ipados-17.7.11-21H461/gta4l/boot/boot.img` measured 203,132,928
bytes; that is a historical measurement, not a fresh release build. The release
flow has not been run end to end on this working tree. Because the packed
payload alone exceeds the 100,663,296-byte BOOT partition, a complete
`unixnu release` is **expected** to fail at the verify step with the partition
limit; that failure was not observed in this record.

This is deliberate: raising the limit would misstate the partition evidence.
The standard release recipe is therefore **not** the safe diagnostic image
and is **not** a flash candidate. The established raw RAM-root flow
(`scripts/build-gta4l-diagnostic-payload.py` with an 80 MiB payload, then
`scripts/package-gta4l-diagnostic.py` against the pinned v5 baseline) remains
the packaging path for on-device diagnostics. A future release artifact needs
either a documented payload size that fits BOOT or an upstream interface for
the diagnostic payload contract; neither is invented here.

## Host verification

Run from the UniXNU checkout:

```sh
python3.11 -m py_compile scripts/unixnu/*.py
python3.11 -m unittest scripts.unixnu.test_profiles scripts.unixnu.test_pipeline scripts.unixnu.test_kernel
python3.11 scripts/porting-record.py check --device ../UniXNU-gta4l
```

Run from this repository:

```sh
python3.11 -B scripts/test-gta4l-packaging.py
python3.11 -B scripts/test-gta4l-boot-image.py
python3.11 -B scripts/test-gta4l-diagnostic-package.py
python3.11 -B scripts/test-gta4l-diagnostic-payload.py
python3.11 -B scripts/check-gta4l.py
UNIXNU_UEFI=../Mu-Silicium python3.11 -B scripts/prepare-gta4l-boot-dtb.py
```

Host results on the uncommitted working tree:

- `scripts/check-gta4l.py` passes.
- `scripts/test-gta4l-packaging.py` passes: it checks the package values, the
  canonical script hash and template equality, and the DTB preflight
  truncation in an isolated tree.
- `scripts/test-gta4l-boot-image.py`, `scripts/test-gta4l-diagnostic-package.py`
  and `scripts/test-gta4l-diagnostic-payload.py` pass.
- The UniXNU profile, pipeline and kernel unit tests pass (19 tests); profile
  resolution accepts `17` and the full name and rejects `26`, `27`, an absent
  profile and an `arm64e` profile. The release command's step order, `--check`,
  `--no-deps` and failure-stop behavior are covered with mocks.
- The preflight stages 316,095 bytes with SHA-256
  `5c4c0599ff430bec71de6bb75663eb857c30ae368823d55054fd9a90e8eb77f8` from the
  4,619,189-byte Mu-Silicium source in an isolated tree.
- `scripts/porting-record.py check --device ../UniXNU-gta4l` reports 13
  structured areas; `git diff --check` is clean in both repositories.

The release flow itself is exercised with unit fixtures, not by fetching
sources or building an image in this record. The script copies stay
byte-identical to the template.

## GitHub Actions

The existing gta4l kernel workflow is not yet the canonical packaging action:
it pins a different UniXNU fork, contains local Xcode/Nabu/token patches and
does not call `scripts/package.sh`. It is left unchanged until the reviewed
UniXNU and firmware revisions are committed and a runner/toolchain check is
possible. Do not dispatch it from this record.
