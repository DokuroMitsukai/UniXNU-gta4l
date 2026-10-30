# gta4l diagnostic payload and boot-image input inventory

Status: host-only inventory taken on 2026-10-30. No device, flash, boot,
power or storage operation was performed. The SDHC decision remains
**NO-GO** (`sdhc-decision-memo.md`).

This document records the inputs, hashes, availability and host gates for the
diagnostic payload and boot-image flow. The packaging step has been replayed
byte-for-byte for the audit and host-init images. The raw payload build has
since been replayed on the historical host: with the FAT timestamp pinned and
the diagnostic argument ordered as in the historical payload, it reproduces
every payload byte except the 64-byte ADT random seed, and substituting that
seed reproduces the historical payload and audit v2 image
(`payload-build-recipe.md`). This does
**not** claim that the XNU kernel or the boot image's firmware is reproducible
from the RAM-root fixture replay. That replay reproduces the eight fixture
outputs only (`ram-root-fixture-reproducibility.md`); the kernel compile and
link are replayed separately in `kernel-build-recipe.md`.

Tags: `[S]` source- or artifact-established, `[C]` conditional inference,
`[U]` not established.

## 1. Flow

| Stage | Command | Consumes | Produces |
| --- | --- | --- | --- |
| Kernel | `./unixnu build kernel` (UniXNU) | `unixnu.lock`, XNU source and patches, `configs/gta4l_defconfig`, `CONFIG_XNU_EXTRA_LD_ARCHIVES` | `out/gta4l/<profile>/gta4l/kernel/kernel.macho` |
| Raw payload | `scripts/build-gta4l-diagnostic-payload.py` | kernel above, `board.toml`, Mu-Silicium, data ramdisk, trust cache, chip ID, diagnostic argument | `payload.img` and `payload.json` |
| Boot image | `scripts/package-gta4l-diagnostic.py` | pinned v5 baseline, payload image | boot image and package manifest |
| Payload gate | `scripts/check-gta4l-payload.py` | payload image | FAT/ADT/placeholder contract result |
| Boot-image gate | `scripts/check-gta4l-boot-image.py` | v5 baseline, candidate image, optional expected payload hash | component-by-component comparison |

The packager does not rebuild the firmware or the boot DTB. It copies the
baseline header page (recomputing only the image ID), firmware + BootShim,
boot kernel section and appended boot DTB byte-for-byte, and replaces only the
ramdisk. The replacement payload
must be exactly the baseline ramdisk size (83,886,080 bytes) `[S]`
(`scripts/package-gta4l-diagnostic.py`).

The boot image's "kernel section" is **not** the XNU kernel. It is a gzip
stream containing the 192-byte BootShim followed by the UEFI outer firmware
volume, with the boot DTB appended after the gzip stream `[S]`
(`docs/firmware-handoff.md`). The XNU kernel is inside the payload FAT image at
`::/EFI/UniXNU/Payloads/kernel.macho`.

## 2. Pinned v5 baseline components

Baseline: `out/gta4l/boot/boot-ram-root-sdhci-v5.img`, 85,688,320 bytes,
SHA-256 `ce4d76e8024a220890e7f32e2992a7beb094087910b070fc615c6fe1ee86dcde`
`[S]`. The packager defaults to this hash
(`scripts/package-gta4l-diagnostic.py:36-47`); the boot-image gate takes the
baseline path explicitly.

| Component | Bytes | SHA-256 | Availability | Provenance |
| --- | --- | --- | --- | --- |
| Boot header page, ID zeroed | 4,096 | `f5f8f1fe76229f60eaae70f8b81e2778d4086f0eb8155d0e54cb8576b588e388` | embedded | Derived from the v5 image `[S]` |
| Boot kernel section (gzip) | 1,798,052 | `a4b054e04d506d118471495116e75ef234d1b45027e3cddcb87f97bc04c718b3` | embedded | Firmware + BootShim; gzip stream `[S]` |
| Firmware, decompressed | 3,145,920 | `1e8a3132f63d82c96284d1b5d2d3bda5a6e4c44a8770fafdf9fb2abb5e1b073d` | embedded | BootShim + outer FV `[S]` |
| BootShim | 192 | `0b28d033de5d7ba5e33e18700a4edde5201383a3ee5185aa87f2b36439987796` | embedded | First 192 bytes of the firmware `[S]` |
| Booted outer FV / raw FD | 3,145,728 | `42f900a38e582026f5daabb70b19b340f0697f2f90b0248c53ecebc07f4186e9` | embedded, no standalone file | Recoverable from the v5 image; see §7 `[S]` |
| Booted inner FV | 8,339,456 | `3bb7847a9f9abeec1bb70d812fe58ad014ae27a3254465e2da2a753cef99a8bd` | embedded | GUIDed LZMA section of the outer FV `[S]` |
| Appended boot DTB | 316,317 | `dc4c719d06e408654680524af26052d609baa14dd57da9082fd9f35c6509de9e` | present-out | `out/gta4l/ram-root-sdhci-audit-evidence/boot-dtb-v5.bin`; patched from `e2ae2d67...` `[S]` |
| Unpatched boot DTB | 316,317 | `e2ae2d671e28d9e295cc98ea3573ee6b41c46f5402e0769a417228d9fd8927fd` | present-sibling | `src/Mu-Silicium/Resources/DTBs/gta4l.dtb`; `board.toml` `[[bootimg.dtb_node]]` applies the framebuffer and touch-controller edits `[S]` |
| Ramdisk (payload) | 83,886,080 | `a390c68ae1d4650a83c49f72631970d829d8edfe1a0239ce347cfc27f646951c` | present-out | v5 payload image `[S]` |

The raw FD and inner FV hashes are recorded again in
`docs/firmware-handoff.md` and `docs/evidence-baseline.md`; this table does not
supersede that provenance work.

## 3. Historical images

All five images below pass the boot-image gate against the v5 baseline when the
expected payload hash is supplied. They therefore share the header page
(except the recomputed image ID), firmware + BootShim, boot kernel section and
appended boot DTB `[S]`. Only the 80 MiB ramdisk differs.

| Image | Boot image SHA-256 | Payload SHA-256 | Embedded payload kernel SHA-256 | Payload contract |
| --- | --- | --- | --- | --- |
| v5 snapshot | `ce4d76e8024a220890e7f32e2992a7beb094087910b070fc615c6fe1ee86dcde` | `a390c68ae1d4650a83c49f72631970d829d8edfe1a0239ce347cfc27f646951c` | `810fd34133bf5abeaf14b40c0a0b2ec24b081e9ef217479f5cb98ef70104bd1f` | pass, 6/6 |
| Audit v2 | `a34519c66820eb5f1f4c4790be5e10233ffd4c8a4ece2df4768e7dca151c8546` | `31b78bec7529706407f682e64dbc9c4c990e86009025174fd3f791606b29ea31` | `6e81598cd143c73f47b8d160433d87d84ca035f599187c334241762d4055854a` | pass, 7/7 |
| Host-init v2 | `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133` | `0afaae8d66251d61c74d80427289da5e79551e52652e5236adaac55c6c8ac8b1` | `72eae6f0c0850ac421d0048cb6cc3697cf70db7df176b34b30703d26132bbc3f` | pass, 7/7 |
| FS timer | `c12e601eceb8e5b5b9cf7a028c0f404653b67dc5ff5577813293673d2dea2b3a` | `3f392030ff98f5bb03aaadaaa1a4af0802e8a1da06f4b5e16ff13e9cf40fe15c` | `20f3facca74d0915b686cacba1da1a74cfe0ff3cf55f296ddaa67a6fd698de4e` | fail only `exactly_one_diagnostic_argument` |
| Report | `45ecc93049757e97a8d71edb776f67e69df5c9c58be13543295c154637273e24` | `06de02dfa2d7ec845bc6e1b2dce4725f4a01db55014e08b5000ab418e1ce9a2b` | `20f3facca74d0915b686cacba1da1a74cfe0ff3cf55f296ddaa67a6fd698de4e` | fail only `exactly_one_diagnostic_argument` |

The FS-timer and report payloads predate the diagnostic-argument requirement;
their other four contract checks pass `[S]`. They are not regressions in the
diagnostic flow.

The v5, audit and host-init payloads use the same fixture data:

| Payload input | SHA-256 | Availability |
| --- | --- | --- |
| Data ramdisk (`root.img`) | `77d5eba0903a0491118d58150047370aeed2f1cf65636bb7b275f8c23d6f81b4` | present-out; matches the fixture replay `[S]` |
| Trust cache (`trustcache.bundle`) | `e146717a15fada48fd6ab06b0de9f179e2a6dd9e9a1dc3d86c1e56804bac8a6d` | present-out; matches the fixture replay `[S]` |
| Host-init `unixnu.toml` | `f14ab52ce5cf9e83063cd7e71c9bf90933ff4fd055c5e33dcd00b98d34ee3671` | present-out `[S]` |

The report payload uses a different fixture: ramdisk
`0c3fbbc1662514bdac3832dd42f80986c907e1fa0988b06d7e9ea2ba439eb329` and trust
cache `17693480cbb8d0ebab55adf4728717ce004957666d6ad47bfb9dc46d4c3716c0`
`[S]`.

## 4. Payload input inventory

| Input | Consumed by | Identity | SHA-256 | Availability | Notes |
| --- | --- | --- | --- | --- | --- |
| XNU source revision | kernel build | `unixnu.lock` `[source."xnu"]` | commit `d8b80295118ef25ac3a784134bcf95cd8e88109f` | tracked | `unixnu.lock` is in the device repo; the feed checkout is under `src/UniXNU/out/feeds/` `[S]` |
| XNU patches | kernel build | `src/UniXNU/out/.../gta4l/xnu-inputs.json` | 59 patches, including device-repo `patches/xnu/gta4l-sdhci-handoff-audit.patch` `2b7668e643d248e5191c6b84dd740e54ad67b9a05c9d7006ad23f7bfc806af9f` | present-out | Current build record; no equivalent record was found in the evidence directories for the v5 or host-init kernels `[S]/[U]` |
| Kernel link archive | kernel build | `libGta4lSdhciSnapshot.a` | audit evidence `ef6646ce0a2deb55d7e63bb403638e00734a13154b1d478cfd695cd447b8f56a`; current build `8db3f178725bfdede748b7dab210883905de981af94a2b097441ea765a3989b8` | present-out | Built from `kits/gta4l/Gta4lSdhciSnapshot.c`, `Gta4lSdhciHandoffAudit.c` and `Gta4lSdhciHostInit.c`; current archive contains three objects, the audit archive two `[S]` |
| Device configuration | payload build | `board.toml` | `505bef50b59b1cd9324ea88244891d91db9b6f157c19702a1a29e8b511fb31ed` | tracked | `[payload.adt]`, `[payload]` kernel args, `[[bootimg.dtb_node]]` `[S]` |
| Kernel configuration | kernel build | `configs/gta4l_defconfig` | `cdc698cb36e26413b61ca30dc2314e700f257f89f563f3e3d5ee3a0440e3b864` | tracked | `CONFIG_XNU_EXTRA_LD_ARCHIVES` force-loads the SDHCI snapshot and eMMC probe archives `[S]` |
| Resolved source set | fetch/build | `unixnu.lock` | `fc1bf57c489ba2809d471fde20a550424e35789a78a95977e917e84255e89550` | tracked | XNU, sm6115, libdispatch and the other pinned sources `[S]` |
| Feed configuration | fetch/build | `feeds.toml` | `3be886c449404f678c0e52034bf5d0b3232f07227be7e15af082c2e377528aca` | tracked | SM6115 source pin `[S]` |
| UniXNU checkout | payload build | `src/UniXNU` | commit `6ee4ada8a82f5bd2d9032b4d7cf033e878e0551f` | present-sibling | Detached; supplies `unixnu`, payload builder and boot-image helper `[S]` |
| Mu-Silicium checkout | firmware and DTB | `src/Mu-Silicium` | commit `e14626a3757007a2dbc902d3db323620d5568923` | present-sibling | Working tree has a modified `Mu_Basecore` submodule `[S]` |
| Boot DTB source | boot image | `src/Mu-Silicium/Resources/DTBs/gta4l.dtb` | `e2ae2d671e28d9e295cc98ea3573ee6b41c46f5402e0769a417228d9fd8927fd` | present-sibling | Patched to `dc4c719d...` by `board.toml`; the v5 `boot.json` records the unpatched hash `[S]` |
| Tested firmware FD | boot image | outer FV of the v5 image | `42f900a38e582026f5daabb70b19b340f0697f2f90b0248c53ecebc07f4186e9` | embedded | Not in this workspace; the historical build host retains the standalone Build FD (same hash), and `boot.json` records `fd_sha256`; see `firmware-build-recipe.md` `[S]` |
| Current Mu-Silicium FD | rebuild only | `src/Mu-Silicium/Build/gta4lPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd` | `b2b5bdce7e3005f04d8883b40adac5bb750736fb3aad54244470b6bc9d80308f` | present-sibling | Local clang 21.1.8 build at a different absolute path; 45 of 86 PE hashes differ from the tested image. The tested toolchain is Arch clang 22.1.8; see `firmware-build-recipe.md` `[S]` |
| Local committed FD | firmware reference | `src/UniXNU-gta4l/SILICIUM_UEFI.fd` | `3d5dbd6ff28093e38a16c02af813fe7d2624d8e9b83ff407045e467e86904f5a` | tracked | Gzip-wrapped; outer FV `4a8ca2cd...`, inner FV `2443e52a...`; not the booted image `[S]` |
| Fixture compiler | fixture replay | host `/usr/bin/clang-21` via the toolchain shim | clang 21.1.8 | present-host | Pinned path and exact version in the fixture recipe `[S]` |
| Fixture signer | fixture replay | `rcodesign` tarball | pinned by the recipe | fetched per replay | Not installed system-wide; the replay prefix is volatile `[S]` |
| cctools-port | fixture replay | recipe pin | commit `904de2a71d4da6a9b30d2efaf912a10ddc7d9ddb` | fetched per replay | `[S]` |
| swift-corelibs-libdispatch | fixture replay | recipe pin | commit `024805abec6daa9cdda569fd736b9c7686dfd735` | fetched per replay | This is the fixture dependency, not the `unixnu.lock` libdispatch `[S]` |

The payload builder copies the device repo to a temporary directory, rewrites
`[payload] size_mib` and forces `[payload.adt] packed_data_ramdisk = false`,
and invokes `unixnu build payload` with the data ramdisk, trust cache and
diagnostic argument. It then runs the payload contract check before publishing
the image `[S]` (`scripts/build-gta4l-diagnostic-payload.py:100-199`).

## 5. Kernel artifacts

| Image | Prelinked/raw kernel | Packaged payload kernel | Availability |
| --- | --- | --- | --- |
| v5 snapshot | `10a2b5b9d5239d90a848a9c88dcebc96118b309f0342d0d69447685a81622224` | `810fd34133bf5abeaf14b40c0a0b2ec24b081e9ef217479f5cb98ef70104bd1f` | raw absent; packaged embedded in `payload.img` `[S]` |
| Audit v2 | `172951570ac0ccbdd8fac87dc089001adc9971cd476122ec8f1a9f685e102e3a` | `6e81598cd143c73f47b8d160433d87d84ca035f599187c334241762d4055854a` | both present in `ram-root-sdhci-audit-evidence/` `[S]` |
| Host-init v2 | `ba52b6500ec3d064d173fa06fba23cb42aa4850c0bddd0df9a4178666c084646` | `72eae6f0c0850ac421d0048cb6cc3697cf70db7df176b34b30703d26132bbc3f` | raw absent; packaged present as `payload-v2-kernel.macho` `[S]` |
| FS timer / report | `dc8184da757b5323d2f4fb6c7ac1d588790b3d44d583c6d6fd24897cdbbcfe60` | `20f3facca74d0915b686cacba1da1a74cfe0ff3cf55f296ddaa67a6fd698de4e` | raw present in `pmgr-test2-evidence/` and `pmgr-test3-evidence/`; packaged embedded `[S]` |
| Current build tree | `452026b2d8bbfaaa5b6ebc97ce235f396a8929b50d3957eff4c655eddd30ca86` | `ed19095452f1e2aa1b56edd1746cbbf25063dd01e6d34d97bc2982faa5718a89` | prelink output present; mutable payload output now holds the host-init reproduced-kernel diagnostic `a256b6d7...`; the audit `0eadb4d4...` and host-init `a256b6d7...` images were both flashed and booted `[S]` |

At the replay the current build tree also recorded a 192 MiB payload
(`f1054a26...`), a 203,132,928-byte boot image (`08d2af9f...`), firmware FD
`b2b5bdce...`, and a different boot DTB. The mutable `OUT/payload/` output now
holds the 80 MiB host-init reproduced-kernel diagnostic payload `a256b6d7...`;
the audit payload was `0eadb4d4...`; no copy of the historical 192 MiB payload
was found in the output trees `[S]`. Both reproduced-kernel diagnostic images
were flashed and booted on the tablet
(`out/gta4l/ram-root-sdhci-repro-kernel-evidence/` and
`out/gta4l/ram-root-sdhci-repro-kernel-host-init-evidence/`). The build tree is
a later build, not a reproduction of the v5/audit/host-init evidence `[S]`.

## 6. Host gate results

Run from the device repository on 2026-10-30. All commands are read-only.

```sh
python3.11 -B scripts/check-gta4l-boot-image.py \
  --baseline ../../out/gta4l/boot/boot-ram-root-sdhci-v5.img \
  --candidate ../../out/gta4l/ram-root-sdhci-audit-evidence/boot-ram-root-sdhci-audit-v2.img \
  --expected-ramdisk-sha256 31b78bec7529706407f682e64dbc9c4c990e86009025174fd3f791606b29ea31
```

| Candidate | Gate result | Payload gate |
| --- | --- | --- |
| v5 vs v5 | pass, 18/18 with the v5 payload hash | pass, 6/6 |
| Audit v2 | pass, 18/18 with `31b78bec...` | pass, 7/7 |
| Host-init v2 | pass, 18/18 with `0afaae8d...` | pass, 7/7 |
| FS timer | pass, 18/18 with `3f392030...` | fail only `exactly_one_diagnostic_argument` |
| Report | pass, 18/18 with `06de02df...` | fail only `exactly_one_diagnostic_argument` |

The boot-image gate checks the baseline header page (excluding the
deterministic image ID), header fields, firmware, boot DTB, boot kernel
section, zero kernel padding, no trailing data, FDT validity and Qualcomm IDs,
ramdisk size, and the optional expected payload hash `[S]`
(`scripts/check-gta4l-boot-image.py:118-170`). It does not inspect the FAT
payload contents. The payload gate checks the ADT placeholders, packed/raw
ramdisk layout, trust-cache and addon files, and the diagnostic argument
`[S]` (`scripts/check-gta4l-payload.py`).

Self-tests and firmware extraction:

```sh
python3.11 -B scripts/test-gta4l-boot-image.py            # pass
python3.11 -B scripts/test-gta4l-diagnostic-package.py    # pass
python3.11 -B scripts/check-gta4l-payload.py --self-test  # pass
python3.11 -B scripts/extract-gta4l-firmware.py \
  ../../out/gta4l/boot/boot-ram-root-sdhci-v5.img --json --validate  # 86 PE modules
python3.11 -B scripts/test-gta4l-ram-root-fixture-recipe.py          # pass
```

The current Mu-Silicium FD also validates as 86 PE modules, but only 41 of its
86 module hashes match the tested image `[S]`.

### Packaging replay

On 2026-10-30 the audit and host-init images were repackaged from the pinned
v5 baseline and the existing payloads into a fresh `mktemp` directory under
`/tmp/opencode/`. Both outputs are byte-identical to the historical images:

| Replay | Output SHA-256 | Comparison | Boot gate | Payload gate |
| --- | --- | --- | --- | --- |
| Audit v2 | `a34519c66820eb5f1f4c4790be5e10233ffd4c8a4ece2df4768e7dca151c8546` | `cmp` identical | 18/18 | 7/7 |
| Host-init v2 | `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133` | `cmp` identical | 18/18 | 7/7 |

Commands, run from the device repository:

```sh
REPLAY=$(mktemp -d /tmp/opencode/package-replay.XXXXXX)
python3.11 -B scripts/package-gta4l-diagnostic.py \
  --baseline ../../out/gta4l/boot/boot-ram-root-sdhci-v5.img \
  --payload ../../out/gta4l/ram-root-sdhci-audit-evidence/payload.img \
  --payload-sha256 31b78bec7529706407f682e64dbc9c4c990e86009025174fd3f791606b29ea31 \
  --expect-diagnostic audit \
  --output "$REPLAY/boot-ram-root-sdhci-audit-v2.img" \
  --manifest "$REPLAY/audit-v2.json"
python3.11 -B scripts/package-gta4l-diagnostic.py \
  --baseline ../../out/gta4l/boot/boot-ram-root-sdhci-v5.img \
  --payload ../../out/gta4l/ram-root-sdhci-host-init-evidence/payload-v2.img \
  --payload-sha256 0afaae8d66251d61c74d80427289da5e79551e52652e5236adaac55c6c8ac8b1 \
  --expect-diagnostic host-init \
  --output "$REPLAY/boot-ram-root-sdhci-host-init-v2.img" \
  --manifest "$REPLAY/host-init-v2.json"
cmp "$REPLAY/boot-ram-root-sdhci-audit-v2.img" \
  ../../out/gta4l/ram-root-sdhci-audit-evidence/boot-ram-root-sdhci-audit-v2.img
cmp "$REPLAY/boot-ram-root-sdhci-host-init-v2.img" \
  ../../out/gta4l/ram-root-sdhci-host-init-evidence/boot-ram-root-sdhci-host-init-v2.img
```

The packager runs the payload contract and component gate before publishing
each output; the explicit gate runs above repeat those checks with the expected
payload hash. Replay inputs:

| Script | SHA-256 |
| --- | --- |
| `scripts/build-gta4l-diagnostic-payload.py` | `3f412a7f257dab87b7b23120b2131aa609ab7add92306f00caca5959d74e029e` |
| `scripts/package-gta4l-diagnostic.py` | `77b7f38b55b3a47d865c9180b6c9cdcbf0e4c3039c5bd1a471ac21492440ae83` |
| `scripts/check-gta4l-boot-image.py` | `879ee0ede5ddbebe4e24915c8720fa40a7b1e253642f6fd2a4d4dff937586bf4` |
| `scripts/check-gta4l-payload.py` | `993429b25455041a0cddfec454bb23caa417289e5a3d359950c42db2e5802e49` |

The diagnostic wrapper also accepts `emmc-probe-1`, `emmc-probe-2` and
`emmc-probe-3`, mapping to `gta4l-emmc-probe=1/2/3`. The stage-2 image built on
2026-10-31 is
`out/gta4l/ram-root-emmc-probe-evidence/boot-ram-root-emmc-probe-stage2.img`
(85,688,320 bytes, SHA-256
`278651bdde382949aedae9b508789f8ef393279d77b2a54bac20d9387d7919a6`). Its payload
is SHA-256 `e3c49c4bdcfb34523c37bdab26f1810c26f343650aa651dcbd98d42cd868e77a`;
the payload contract is 7/7 and the boot-image gate is 18/18. It is a host-built
artifact with `hardware_tested=false` and has not been flashed.

This establishes the packaging step only. The inputs are existing artifacts;
the replay does not reproduce the kernel or firmware.

The raw payload build itself was replayed on the historical host on 2026-10-30
(`payload-build-recipe.md`). With the historical prelinked kernel, clang
22.1.8, the audit board snapshot, the historical diagnostic-argument order and
`SOURCE_DATE_EPOCH=1793180892`, the clean payload `1d15ea68...` differs from
the historical `31b78bec...` in exactly the 64-byte ADT `random-seed`; the
patched kernel, trust cache, data ramdisk, `unixnu.toml` and every other ADT
property match. Substituting the historical seed yields a byte-identical
payload, and packaging it against the v5 baseline
reproduces the historical audit v2 image `a34519c6...` byte-for-byte. The clean
payload packages to `037ba650...`, which differs from the historical v2 image
in 84 bytes (the derived image ID and the seed); both gates pass. A second
controlled clean build (`c697903b...`, clean boot `60ad9d05...`) passed both
gates and normalized again to `31b78bec...` / `a34519c6...`. The kernel
compile and link are replayed separately in `kernel-build-recipe.md`.

## 7. Missing, embedded and not-yet-reproduced inputs

**Missing as standalone files, recoverable from a present artifact:**

- Tested outer FV `42f900a3...` (extract from the v5 boot image).
- Packaged payload kernels for v5 (`810fd341...`), FS timer/report
  (`20f3facc...`), and host-init (`72eae6f0...`, also present as a file).
- Embedded payload ADTs for all five images (extract from each payload image).
  Sidecars exist for v5 (`adt/adt`), audit (`kernel.macho.adt`), host-init
  (`payload-v2.adt`) and report (`adt/adt`); the FS-timer sidecar is stale
  (see below).

**Missing and not recoverable from present artifacts:**

- v5 prelinked kernel `10a2b5b9...`.
- Host-init raw kernel `ba52b650...` (only its packaged form is present).
- A standalone FD file and a build record tying the tested firmware to a
  source revision. The FD bytes themselves are recoverable from the v5 image.

**Present but reproduction not established from the current checkout:**

- The tested firmware FD. The current Mu-Silicium build (`b2b5bdce...`) has the
  same module count but 45 of 86 PE hashes differ. `docs/firmware-handoff.md`
  already records this gap for `DxeCore` and `UniXNURamDiskDxe`; this inventory
  confirms it across the full module set `[S]`.
- The v5 and host-init kernel artifacts. The v5 prelinked kernel
  `10a2b5b9...` and host-init raw kernel `ba52b650...` have not been rebuilt
  from the current checkout `[U]`. The current build's prelinked kernel
  `452026b2...` is the reproduced prelink output; the audit v2 prelinked kernel
  `17295157...` is a different build and remains not replayed `[U]`.
- The v5 and host-init payload images embed kernels whose reproduction is not
  established; the audit v2 payload kernel `6e81598c...` is reproduced from the
  historical prelink output with the historical clang 22.1.8
  (`payload-build-recipe.md` §4.1), but the audit v2 prelinked kernel
  `17295157...` itself remains not replayed `[U]`. The v5 and host-init
  baselines also embed firmware whose reproduction is not established `[U]`.

**Recorded inconsistency:**

- The FS-timer evidence directory contains an `adt/adt` sidecar with SHA-256
  `40fec3dfbc183f75e66e0dbb07db723d7ff2a365e5598fc88eebf08beb0cff2b`, but
  the FS-timer payload embeds ADT `8783ee66d269d9230f66625049561064b6341c08e8f526b8624316c94b74c9b2`,
  which matches its `payload.json`. The sidecar is stale; the embedded ADT is
  authoritative `[S]`.

**Wording to read precisely:**

- The booted host-init image is present locally in the gitignored `out/` tree
  at the recorded hash; it is not tracked by git `[S]`.

## 8. Reproduction boundary

The fixture replay in `ram-root-fixture-reproducibility.md` reproduces the
eight fixture outputs with pinned hashes: `user.o`, `launchd-static.macho`,
`launchd.macho`, `trustcache.bundle`, `trustcache.json`, `root.img`,
`addresses.json` and `fixture.json` `[S]`. Two of those are payload inputs:
`root.img` `77d5eba0...` and `trustcache.bundle` `e146717a...`, matching the
v5, audit and host-init `payload.json` records `[S]`.

The replay does not produce:

- the XNU kernel or any of its per-image variants;
- the UEFI firmware or BootShim;
- the payload ADT or the FAT payload image;
- the boot image or its header, firmware section or appended DTB.

Reproducing the boot image therefore requires three separate steps: rebuild
the kernel from the pinned XNU source and patches, rebuild the firmware from
the pinned Mu-Silicium revision, then rebuild and repackage the payload. The
packaging step is replayable from the existing baseline and payloads, the
fixture data inputs have a pinned recipe, and the kernel source/patch,
compile, link and prelink stages are replayed in `kernel-build-recipe.md`.
Firmware build provenance and the
module replay are recorded in `firmware-build-recipe.md`: the tested outer FD
`42f900a3...` is built from Mu-Silicium `e14626a...`, the `mu_basecore-8f10084c`
feed at `cfb9372...`, UniXNU `4561854...`, `Binaries` `a1501e0c...` and Arch
clang 22.1.8. The inner FV differs in exactly 8 bytes (the `PcdDxe` PDB GUID).

### Kernel build bootstrap caveat

`scripts/build-gta4l-sdhci-snapshot.py` compiles the SDHCI probe archive using
the `bsd_init.o` compile record. On a clean tree that record does not exist
yet: the script writes `FirstPassStub.c` with no-op probe symbols, links
`libGta4lSdhciSnapshot.a`, records `"runtime_qualified": false` in
`build-report.json` and exits (`scripts/build-gta4l-sdhci-snapshot.py:48-72`).
The kernel built in that pass contains no-op probes; only the next kernel
build links the real probes. Any kernel compared against a historical image
must record whether it came from the bootstrap pass or from a later pass with
a compile record `[S]`.

## 9. Next host-only step

1. Kernel build provenance is recorded in `kernel-build-recipe.md`: the
   source, patch, compiler, linker, archive and prelink inputs are pinned, and
   the source/patch, compile, link and prelink stages are replayed. The linked
   kernel `3103e067575d94d878485de464ee0173b07ab2f02215d3abae6fb962c1c59064`
   is reproduced byte-for-byte with the historical timestamps and
   `RC_ProjectSourceVersion=10063.141.1`; the evidence-image kernels remain not
   established.
2. Firmware build provenance is recorded in `firmware-build-recipe.md`. A
   clean replay at the historical absolute path reproduces 153 of 156 `.efi`
   build outputs byte-for-byte; the three `PcdDxe` copies differ only in the
   8-byte debug-directory GUID. Two generated-text differences (stack-cookie
   value and PCD flag order) are the observed candidate inputs; this replay
   did not isolate the GUID cause. The inner FV differs in exactly those 8
   bytes. A bit-exact outer FD was not achieved; `--skip-autogen` failed in
   this BaseTools feed, and other pinning approaches were not attempted. The
   earlier 45-of-86 PE difference was the laptop's clang 21.1.8 build at a
   different path, not the tested toolchain.
3. The raw payload build is recorded in `payload-build-recipe.md`. On the
   historical host with clang 22.1.8, the audit board snapshot, the pinned FAT
   timestamp and the historical diagnostic-argument order, the kernel patch,
   trust cache, data ramdisk, `unixnu.toml` and 106 of 107 ADT properties
   match. The clean payload differs from the historical
   `31b78bec...` only in the 64-byte ADT random seed; substituting the
   historical seed yields a byte-identical payload, and packaging it against
   the v5 baseline reproduces the historical audit v2 image `a34519c6...`
   byte-for-byte. A clean bit-exact build needs a supported random-seed pin,
   which the pinned UniXNU does not provide.
4. Remaining host-only work: a supported ADT random-seed pin for a clean
   bit-exact payload build, and a bit-exact Mu-Silicium outer FD
   (`firmware-build-recipe.md`). Do not flash; the SDHC decision remains
   NO-GO.

## 10. References

- `docs/kernel-build-recipe.md` — kernel inputs and the source/patch, compile,
  link and prelink stage replays
- `docs/payload-build-recipe.md` — raw payload inputs, the historical-host
  replay and the seed-normalized comparison
- `docs/ram-root-fixture-reproducibility.md` — fixture inputs, pins and replay
- `docs/firmware-handoff.md` — tested firmware provenance and the current-build
  difference
- `docs/firmware-build-recipe.md` — firmware inputs, the clean replay, and the
  `PcdDxe` debug-GUID delta
- `docs/sdhci-snapshot.md` — v5 hardware result and activation argument
- `docs/sdhci-handoff-audit.md` — audit v2 image and script inventory
- `docs/sdhci-host-init.md` — host-init v2 image and bounded writes
- `docs/evidence-baseline.md` — tracked-file hashes and limits
- `scripts/build-gta4l-diagnostic-payload.py`,
  `scripts/package-gta4l-diagnostic.py`,
  `scripts/check-gta4l-payload.py`,
  `scripts/check-gta4l-boot-image.py`,
  `scripts/extract-gta4l-firmware.py`
