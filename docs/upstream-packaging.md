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
| `scripts/unixnu/profiles.py` (UniXNU) | `d9fe24b030b4e89337e33e033b41aa35222fd93abdf4b3408ee5fc30cc4e08f1` | Retains the local arm64-only selection and adds the package-contract validation |
| `scripts/unixnu/tree.py` (UniXNU) | `4d06a44b5f195e05335c10ce7a0e01cbf0da1b9f79f49dcafc3581978067e151` | Upstream `a4419c8`: script copies refresh by content, not size and mtime |
| `scripts/unixnu/firmware.py` (UniXNU) | `69c30263d30da155a4f44d747d691847192891b889ff280c1ecc57bca0ce51ab` | Upstream `658f82f`+`66e43f1`: preflight entries carry arguments and `UNIXNU_DEVICE` |
| `docs/porting/packaging.md` (UniXNU) | `6dff4a637664adbe1ed264bde66844497b60d3ed3a26efb0e00df19e12adb147` | Upstream text, "uefi-sheng" generalized |

The script reads only `[package] unixnu` itself; every other value comes from
`board.toml`, the profiles and the local build inputs.

## Later upstream advance (2026-11-01)

Official upstream advanced from the inspected anchor `6790e7ce` to
`a4419c8af72999d97df6b18505326b60b06aeb00` by five commits. The range was
reviewed commit by commit; the two shared-tooling fixes are ported, the
example/docs-only firmware-archive contract is recorded but not adopted.

| Upstream commit | Change | Disposition for gta4l |
| --- | --- | --- |
| `658f82fe0f822707609ace8b0056c93506120af8` | Firmware preflight scripts get the device checkout in `UNIXNU_DEVICE` | Ported into `scripts/unixnu/firmware.py` |
| `66e43f1c168120154680871d26227cedc601ec3c` | Preflight entries carry arguments; `shlex` splits the entry, absolute paths are used as-is | Ported with the previous commit |
| `76a2e1cb6e80d0f5bd6b2e313226cfd192174382` | Example device pins the official fastboot archive in `[firmware.image]` (url, SHA-256, MD5, bytes) and fetches it in preflight | Recorded as reference; **not adopted** |
| `c96b29975645b711dc06bc435c3ad27321ce817c` | Packaging docs describe the official-archive inputs | Recorded as reference; **not adopted** |
| `a4419c8af72999d97df6b18505326b60b06aeb00` | Assembled-tree script copies refresh by content, not size and mtime | Ported into `scripts/unixnu/tree.py` |

The content-refresh port matters beyond packaging: the device build reuses the
assembled tree under `src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l/tree`
across incremental kernel builds. A same-size script edit, or a checkout that
makes the source look older than the tree copy, previously left the stale copy
in place. The gta4l preflight entries in `board.toml` are arg-less and only
read `UNIXNU_UEFI`, so the preflight port is behavior-neutral today; it makes
the board contract match upstream for future scripts that need device-repo
context or arguments.

The `[firmware.image]` deferral is deliberate. gta4l's firmware inputs and
their provenance are recorded in `firmware-dependency-decision.md` and
`boot-dtb-provenance.md`; moving them onto an official-archive download is a
firmware-provenance decision, not a tooling catch-up, and needs its own
authorization. No extracted firmware is added to any repository either way.

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

## Workflow publication gate

`.github/workflows/package-gta4l.yml` is manual-only, read-only and pinned to
the published UniXNU commit and the recorded Mu-Silicium integration commit.
It runs `scripts/test-gta4l-workflow.py`, the packaging contract, the
boot-image self-test and the diagnostic-payload builder test before the
dependency/preflight step.

Image publication is disabled: the workflow has no upload step and no
`if: success()` upload gate. A future run that somehow passes verification
still retains no artifact. Do not restore an upload step, or treat an expected
verify failure as a release, without an owner-approved allowlisted publication
gate.

The workflow has not run on a runner, so the macOS image, Xcode 26.6 and
Homebrew tool versions remain unverified. The pinned UniXNU commit predates
`scripts/unixnu/inspection.py`, so `scripts/test-gta4l-inventory.py` is not in
the runner step yet; it joins when the pin advances. The contract test parses
the YAML with Python `yaml` or Ruby Psych and normalizes their YAML 1.1 `on`
key.

## Host verification

Run from the UniXNU checkout:

```sh
python3.11 -m py_compile scripts/unixnu/*.py
python3.11 -B -m unittest discover -s scripts/unixnu -t .
python3.11 scripts/porting-record.py check --device ../UniXNU-gta4l
```

Run from this repository:

```sh
python3.11 -B scripts/test-gta4l-workflow.py
python3.11 -B scripts/test-gta4l-packaging.py
python3.11 -B scripts/test-gta4l-inventory.py
python3.11 -B scripts/test-gta4l-boot-image.py
python3.11 -B scripts/test-gta4l-diagnostic-package.py
python3.11 -B scripts/test-gta4l-diagnostic-payload.py
python3.11 -B scripts/check-gta4l.py
UNIXNU_UEFI=../Mu-Silicium python3.11 -B scripts/prepare-gta4l-boot-dtb.py
```

Host results for the adopted tree (2026-11-01):

- `scripts/check-gta4l.py` passes.
- `scripts/test-gta4l-workflow.py` passes. It statically checks the workflow
  trigger, read-only permissions, immutable action and source pins, the exact
  profile, host tests before release construction, checkout paths, tool and
  interpreter assertions, seed redaction, shell syntax and disabled
  publication.
- `scripts/test-gta4l-packaging.py` passes. It checks the board package
  values, the resolved iPadOS 17 profile and IPSW identity, the cached
  kernelcache, the local component pins, the canonical script hash and
  template equality, the preflight's success and failure controls, stale
  output truncation, and the launcher's argument and interpreter propagation.
- `scripts/test-gta4l-inventory.py` passes over synthetic device, UniXNU and
  firmware Git repositories. It covers the declared/effective/cached split,
  module-in-parent-checkout, revision mismatch, redaction of tokens, URL
  credentials, signed parameters and the platform seed, malformed or
  reordered XNU reports, missing caches and deterministic output.
- `scripts/test-gta4l-boot-image.py`, `scripts/test-gta4l-diagnostic-package.py`
  and `scripts/test-gta4l-diagnostic-payload.py` pass.
- The UniXNU unit suite passes (80 tests, 1 skipped; 73 before the
  tree-refresh and preflight fixtures). It covers the production
  context, argument parsing, profile validation, feed precedence, saved-state
  and environment handling, the offline inspection resolver, redaction of
  tokens, signed URLs and the platform seed, IPSW cleanup, content-based
  script refresh in the assembled tree, and preflight argument splitting with
  `UNIXNU_UEFI`/`UNIXNU_DEVICE`. Profile
  resolution accepts `17` and the full name and rejects
  `26`, `27`, an absent profile, a missing defconfig mapping and an `arm64e`
  profile. The release command's step order, `--check`, `--no-deps` and
  failure-stop behavior at every step are covered with mocks. Per-OS overrides
  cannot change the package contract or the resolved profile identity.
- The preflight stages 316,095 bytes with SHA-256
  `5c4c0599ff430bec71de6bb75663eb857c30ae368823d55054fd9a90e8eb77f8` from the
  4,619,189-byte Mu-Silicium source in an isolated tree, rejects a missing
  `UNIXNU_UEFI`, a missing source, bad FDT magic and invalid totalsizes, and
  overwrites a stale output without keeping its tail.
- `scripts/porting-record.py check --device ../UniXNU-gta4l` reports 13
  structured areas; `git diff --check` is clean in both repositories.

The release flow itself is exercised with unit fixtures, not by fetching
sources or building an image in this record. The script copies stay
byte-identical to the template.

## GitHub Actions

`.github/workflows/package-gta4l.yml` is the manual packaging check. It is
static-validated only and has not run on a runner. It is manual-dispatch only,
pins the actions and all three source checkouts, keeps `contents: read`,
uses a bounded concurrency group and timeout, installs and asserts the
toolchain, validates the platform seed, runs the host contract tests, and then
runs the profile check and the full release for `ipados-17.7.11-21H461` only.
The profile check uses `--check`, which runs the dependency/IPSW preparation;
it is not an offline lint command. Because the 192 MiB payload exceeds the BOOT
partition, the release verify step is expected to fail. Image publication is
disabled: there is no upload step and no `if: success()` upload gate.

The UniXNU pin is `f50d5bd5093ede81f8c1897b8ce5c93473c0fb7b` on
`DokuroMitsukai/UniXNU`, the tip of the published
`gta4l-release-pipeline` branch, so the checkout resolves. The local branch has
later unpublished commits, including the platform-seed redaction: until the pin
advances to a published commit, the runner relies on GitHub Actions secret
masking for the seed. A dispatch
still requires this workflow file on the device default branch, which this
branch does not change. The firmware pin is the recorded Mu-Silicium
integration commit, checked out without submodules: the build initializes only
the `[firmware]` submodules that `board.toml` lists, and `Mu_Basecore` comes
from the edk2 feed.

Do not dispatch the workflow until the artifact limit is resolved and the run
is separately authorized. A missing Xcode 26.6 or runner image change fails
fast in the toolchain step; that has not been exercised.

The three legacy kernel workflows (`build-xnu-kernel-gta4l.yaml`,
`workflowtest.yaml`, `workingworkflow.yaml`) and `.circleci/config.yml` are not
the canonical packaging action: they pin a different UniXNU fork, use mutable
action tags or a connected CI service, contain local Xcode/Nabu/token patches
and do not call `scripts/package.sh`. They are left unchanged; do not dispatch
them from this record. `porting-record.yml` runs on push and pull_request and
calls the reusable UniXNU check at `UniXNU/UniXNU@main`; that moving upstream
ref is an accepted, non-publishing record dependency, not a source pin for a
release build.
