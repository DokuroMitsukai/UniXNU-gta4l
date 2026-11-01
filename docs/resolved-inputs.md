# gta4l resolved packaging inputs

Status: host-only, offline inventory. No fetch, build, image, CI run or device
operation was performed. The SDHC decision remains **NO-GO**
(`sdhc-decision-memo.md`).

This record lists the inputs the release path resolves from the device
repository, the shared UniXNU checkout and the firmware checkout. It was
captured on 2026-11-01 from the working tree; the JSON itself records the
exact device and UniXNU commits and dirty-path counts. Regenerate it for a
later tree.

## Reproduction

```sh
cd src/UniXNU-gta4l
python3.11 -B scripts/audit-gta4l-resolved-inputs.py > /tmp/gta4l-inputs.json
```

The script only reads local files and Git metadata. It does not fetch, write a
build output or verify a remote ref; the JSON paths are absolute. A missing
cached kernelcache or build report is reported as `null`, not as a failure.

## OS profile and IPSW identity

| Field | Value |
| --- | --- |
| Profile | `ipados-17.7.11-21H461` (the only installed `profiles/os` entry) |
| Darwin | `23.6.0` |
| XNU version | `10063.141.1` |
| XNU revision | `d8b80295118ef25ac3a784134bcf95cd8e88109f` |
| DriverKit tag | `xnu-10063.121.3` |
| Trust cache tag | `xnu-8796.101.5` |
| ABI | `IOGPUFamily`, `IOHIDFamily-2031.120.4`, `IOMobileGraphicsFamily` |
| IPSW | `iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw` (Apple 2025SpringFCS) |
| Product / version / build | `iPad7,1` / `17.7.11` / `21H461` |
| Device class / chip / board | `j120ap` / `32785` / `12` |
| Fetched members | `KernelCache`, `DeviceTree`, `StaticTrustCache`, `Cryptex1,SystemTrustCache` |

The profile records these IPSW member hashes; the full archive hash is not
pinned in the profile.

| Member | SHA-256 |
| --- | --- |
| `044-76034-038.dmg` | `b4908cde41d23e6bc05e6af51a2c9eeb00cf0f863a6a18b1600c96a3fd70414f` |
| `044-76040-038.dmg` | `3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d` |

The cached decompressed kernelcache is
`out/ipsw/21H461/kernelcache.decompressed`, 46,415,872 bytes, SHA-256
`f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`, matching
the profile's `[kernel.prelink] kernelcache_sha256`.

## Feeds and lock

The device `feeds.toml` pins the SM6115 tree; `unixnu.lock` pins every resolved
git source.

| Source | Commit |
| --- | --- |
| `sm6115` (device feed, `gta4lunix/UniXNU-sm6115-common`) | `4137c1c1fbe2b3693e5a61985fd01bbacc8fdabc` |
| `xnu` | `d8b80295118ef25ac3a784134bcf95cd8e88109f` |
| `edk2` (`microsoft/mu_basecore`, `release/202608`) | `cfb93724a06ff86ff5ba971413368a7809ee42a5` |
| `libplatform` | `a00a4cc36da2110578bcf3b8eeeeb93dcc7f4e11` |
| `libdispatch` | `30fe6a7d945340ce69cef121132f049e810089a5` |
| `AvailabilityVersions` | `933f9360f1b70dfbda5548386a3d65e0358b4f7d` |
| `libpthread` | `c0e60fb5ecfc54cf379c261d3f9572cc22ded891` |
| `corecrypto-2023` | `11fba65b12d8830630e248dfa8344caf8b065f26` |
| `IOStorageFamily` | `e45fbe30a7879e0b16e26dddb78538f1b00e0bff` |

The SM6115 feed pin matches the lock. There is no `unixnu.local.toml`; no
per-run `--local`/`--local-module` override is recorded.

## Components

The board includes `ArmDTPlatform`, `ArmCpu`, `SoftwareAES`, `XnuPrelink` and
`DarwinBoot`. Each local checkout matches its `components.toml` revision and is
clean:

| Component | Revision |
| --- | --- |
| `ArmCpu` | `c9c4bd87cabc1fe6736e171cd2540313ac4b7883` |
| `ArmDTPlatform` | `417ff95d6a6e8607ddea9309ec45c3bfd4a67c60` |
| `SoftwareAES` | `4a45326da5c2cd6fe6813d79ee43db20805628af` |
| `XnuPrelink` | `f876e3ee62e18fc06653dd75ed7dafcfb3950ac0` |
| `DarwinBoot` | `3f2f304784f8fbfbf2ebfdc8eba3af3577d92967` |

`DarwinBoot` was declared as `2e225117` before the packaging change. The
device record and firmware recipe already pin the `bringup/shared-video` head
`3f2f3047`, so the catalog was moved to the checkout the assembled tree links.

## Firmware

| Input | Value |
| --- | --- |
| Mu-Silicium checkout | `e14626a3757007a2dbc902d3db323620d5568923` |
| `gta4lPkg` | committed in that checkout; clean |
| Local `Mu_Basecore` | `3c9c7b2e211e62d2a954ad760a36fe05adc43aa3`, 10 modified files |
| Build basecore | feed `edk2` = `microsoft/mu_basecore` `release/202608`, `cfb93724` |
| External packages | `UniXNUPkg` and `DarwinBoot` from the UniXNU checkout |

The local `Mu_Basecore` is not the build source. `docs/firmware-dependency-decision.md`
records the dependency decision and the preserved local modifications.

## Boot device tree

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| Mu-Silicium `Resources/DTBs/gta4l.dtb`, full file | 4,619,189 | `e2ae2d671e28d9e295cc98ea3573ee6b41c46f5402e0769a417228d9fd8927fd` |
| First FDT in that file (`totalsize`) | 316,095 | `5c4c0599ff430bec71de6bb75663eb857c30ae368823d55054fd9a90e8eb77f8` |
| Audit evidence `out/gta4l/ram-root-sdhci-audit-evidence/boot-dtb-v5.bin` | 316,317 | `dc4c719d06e408654680524af26052d609baa14dd57da9082fd9f35c6509de9e` |

The evidence DTB is reproduced byte-exactly from the first FDT plus the board
`[[bootimg.dtb_node]]` edits and initrd marks; the PixelExperience Android 11
release boot DTB plus `dtbo.img` overlay does not match. See
`docs/boot-dtb-provenance.md`.

## Kernel inputs

`out/gta4l/ipados-17.7.11-21H461/gta4l/xnu-inputs.json` records the pinned XNU
commit and 60 patch files with their SHA-256 values. The inventory script emits
that list; it is not reproduced here. The file is a gitignored build report, so
its absence means "not cached", not "different inputs".

## Resolution order

The profile merge order is arch profile, `soc.toml`, OS profile,
`board.toml`, then the selected `[os_overrides]` entry. The release command
forces `[package]` from the raw board after the merge, and a per-OS override
cannot change the resolved profile name. `--os` must resolve to a profile that
`[package.defconfig]` maps to an existing `configs/<name>`, and that check runs
before the SoC is resolved. The release command selects the defconfig from
`[package.defconfig]`, so a stale saved `state.json` choice cannot redirect it.

## Limits

- No network access: the IPSW URL, feed refs and remote component heads are
  not re-verified here.
- The kernelcache and `xnu-inputs.json` are retained local evidence; a fresh
  host may not have them.
- This is an input inventory, not a build, CI or device result. The release
  artifact remains blocked by the 100,663,296-byte BOOT partition limit.
