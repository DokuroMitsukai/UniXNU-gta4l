# gta4l N10.19 widthhold candidate (2026-11-11)

Status: host-only candidate production. No device access, flash, boot, mount,
write or hardware run occurred. N10.16's one-boot budget remains consumed; a
hardware protocol and run (N10.20) are separately gated and are not authorized
by this record. The ABI audit uses the established QEMU-emulated Linux path and
executes only the synthetic firmware/kernel fixture.

## 1. Candidate change

N10.19 packages the N10.18 `gta4l-emmc-widthhold=1` sustained-width mode for a
later hardware run. The driver batch (commit `0c6aebe`) implements:

- block=3-only selection with duplicate, unsupported-value and
  partobs/partcmp/partread/deblock/breakup/sweep/consume/width conflict
  refusals; legacy and N10.16 behavior is unchanged;
- a 1-bit EXT_CSD baseline and an 8-block/4,096-byte 1-bit BOOT reference;
- the verified 4-bit switch at the existing 400 kHz, four tagged two-block
  IOMedia reads at BOOT offsets 0, 1,024, 2,048 and 3,072 bytes (LBAs
  2,255,360, 2,255,362, 2,255,364 and 2,255,366), byte comparison against the
  reference slice, and a verified 1-bit revert on every post-switch exit;
- one terminal `WIDTHHOLD` line and, on success, one `WIDTHHOLDREAD` line for
  the last case (failure instead emits one `WIDTHHOLDFAIL` line), with the base
  `MEDIA` evidence and suppressed legacy READ/QUAL/QUAL3 and PART lines;
- a 27,720 ms report-thread bound (8,000 media wait + 2,000 BOOT deadline +
  3,000 case budget + 4,500 two-block case + 2,000 revert gate + 8,220
  verified revert), leaving 2,280 ms under the 30,000 ms watchdog.

This batch adds only host-side candidate production and verification:

- `scripts/check-gta4l-widthhold-evidence.py` recomputes the 4,096-byte
  window and the last-case slice digests from the final BOOT image, checks the
  complete success-line shape and content agreement, and rejects a
  `WIDTHHOLDFAIL` line or a missing/duplicated report line. It never reads a
  device, firmware or candidate kernel.
- `scripts/check-gta4l-payload.py`, `scripts/build-gta4l-diagnostic-payload.py`
  and `scripts/package-gta4l-diagnostic.py` gain the
  `emmc-block-3-widthhold` personality (`gta4l-emmc-block=3
  gta4l-emmc-widthhold=1`) with the same exclusivity checks as the other
  modifiers. Its payload contract requires the fs-timer-hold data ramdisk and
  rejects the early-panic fs-timer fixture.
- `scripts/test-gta4l-diagnostic-payload.py` and
  `scripts/test-gta4l-diagnostic-package.py` cover the new personality,
  orphan/duplicate/wrong-value/block-mismatch/conflict tokens and the fixture
  requirement.

## 2. Candidate identity

- prelinked kernel
  `23e3b6d90f4af2f1562da0ae2a385bec45c247c6eb6d59b76aa068ef09789d48`
  (51,527,680 bytes; plan
  `144d113f5489f6a10abdefb19893e3cd546a1a76b85789639e53d66ccdd9a0a0`)
- candidate storage-ABI audit `out/gta4l/storage-abi-23e3b6d90f4a/`:
  `gdb-stop.json`
  `275b626a2eb3678fc7f68e076a9096a3f9883d4a15688504ab80905a1463ff61`,
  `interface-audit.json`
  `9963b2395d04f4f7dc0ef3ba6dfdfba9dda4e9a34b186e34b8d51d7f9848776c`,
  `provenance.json`
  `00c51c63cfb1e0bf56aad34957e582017fe157f24fe483e8dafac75b8ae25b52`,
  `run.json`
  `268e34ca8acc2b2004bb246a12a836fb9dfc69dccd5e00d45e69f34b214dac1c`;
  190 method slots, 181 kernel bindings (167 inherited + 14 pure virtual),
  26 concrete overrides, 0 unresolved original imports, service metaclass
  `0xfffffff007b2b5b8`, `bsd_init` `0xfffffff0076efa3c`, QEMU 8.2.0, live
  original sizes 0x90/0x88, native 4K.
- kernel-build archive and requalified archive are byte-identical:
  `6dcdfe9afe8014edd5fc02e24c8863c0d50da1e1f2cd9513baa5981be04d26ba`;
  members match:
  `Gta4lEmmcBlock.o`
  `bc58e01ce7ff908de74af12905960e276a16d26418d95957288ec3b45edea2d9`,
  `Gta4lEmmcBlockCpp.o`
  `0db90c9d080599893f0090e89d766c0b336838d128283439da235dc61e2f009f`,
  `original-bindings.o`
  `3e53b66dd177598ce54175d82e4d35b3fe6bf0572dc7ff1c78c75a6c6a461bd7`.
  The source hashes are `Gta4lEmmcBlock.c` `8fb24016022a…` and
  `Gta4lEmmcBlock.cpp` `8acb9da0c4f1…`.
- payload `bd112387ae65a676a5cbbce0cecf828e851af04c338f1c19c0e5230b21a67f15`
  (83,886,080 bytes; 66/66 contract; diagnostic `emmc-block-3-widthhold`;
  packaged kernel
  `82ad663d12f86fb924bafafc5931c57c2d8fb5eedb9377c184d3996d0ffb8c6e`)
- BOOT image
  `7f5455d691402e06f4df0c458c22ecf70b6654ea58c37c11a88aabd2d908a84a`
  (85,688,320 bytes; component gate 18/18; only the ramdisk differs; header ID
  `adb8232af56105a2228e4b75820d19270e40d437…`; first-4,096-byte SHA-256
  `fa169295a70cc707bcd18d279507df1285039020ae98608c628ca03ced5ee085`,
  prefix `fa169295a70cc707bcd18d279507df12`; first-1,024-byte SHA-256
  `9fcf8c14b9f184abdb66eec0547814faf2314162bcb1ace5a99a6f213ba01eee`,
  prefix `9fcf8c14b9f184abdb66eec0547814fa`)
- last-case slice (BOOT offset 3,072, 1,024 bytes) SHA-256
  `5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef`,
  prefix `5f70bf18a086007016e948b04aed3b82`
- fs-timer-hold fixture: `root.img`
  `327ac826176c2fef55b28731f5839f1949ae49b0514c22443575f560d19f8c72`
  (57,344 bytes), `trustcache.bundle`
  `30a72f785252533c33e763bb30a7a823c85a3468ecd22d977147d7d8704fa721`,
  `fixture.json`
  `0036368153fe361df9fbdc8d5386b414cc7177ae3340b39cd412da91c07b525a`

The widthhold host checker binds `refdigest`/`obsdigest` to the first
4,096 bytes of this exact BOOT image and `digest`/`predigest` to the last-case
slice; it does not reuse the N10.16 digest.

## 3. Build chain and reproduction

Host `python3` is 3.9 and lacks `tomllib`, so every step runs with a 3.11
interpreter shim on `PATH`. `UNIXNU_TOOLCHAIN` points at the prebuilt Linux
toolchain; the shared `UniXNU` checkout was at `588ff8c…` with the two
established fixture modifications. The device working tree was at `0c6aebe`
plus this batch's host-tooling changes. Logs, manifests and captured archives
are in `out/gta4l/ram-root-emmc-n10.19-widthhold-candidate-evidence/`.

1. Refresh the shared tree and build the kernel with the previous
   candidate-matched audit as bootstrap:

   ```sh
   cd ../UniXNU
   PATH=<py311-shim>:$PATH UNIXNU_TOOLCHAIN=<prefix> \
     python3.11 ./unixnu tree -D ../UniXNU-gta4l -j 8
   PATH=<py311-shim>:$PATH UNIXNU_TOOLCHAIN=<prefix> \
     GTA4L_STORAGE_ABI_DIR=$PWD/../UniXNU-gta4l/out/gta4l/storage-abi-2be2eb0605b2 \
     python3.11 ./unixnu build kernel -D ../UniXNU-gta4l -j 8
   ```

   The build produced `23e3b6d90f4a…` and the tree's `kits/gta4l` symlink
   pointed at the device `package/` (driver source-hash parity confirmed).

2. Audit the candidate and run the storage-ABI qualification:

   ```sh
   cd ../UniXNU-gta4l
   PATH=<py311-shim>:$PATH python3.11 -B \
     scripts/audit-gta4l-storage-interface.py \
     --kernel out/gta4l/ram-root-emmc-n10.19-widthhold-candidate-evidence/candidate-kernel-23e3b6d90f4a.macho \
     --out out/gta4l/storage-abi-23e3b6d90f4a
   PATH=<py311-shim>:$PATH python3.11 -B \
     scripts/test-gta4l-storage-abi.py --artifacts out/gta4l/storage-abi-23e3b6d90f4a
   ```

3. Requalify the block archive against that audit from inside the shared
   tree, with the full kernel-step environment and
   `GTA4L_STORAGE_ABI_DIR` naming the candidate audit. The resulting archive
   is byte-identical to the archive linked into the kernel; all three members
   match (`archive-member-compare.txt`). A second requalification run from the
   device repository produced a different container and different
   `Gta4lEmmcBlock.o`/`Gta4lEmmcBlockCpp.o` member hashes because the compile
   records embed different object/compile-record directories; the tree run is
   the controlled reproduction and the device-repo difference is a documented
   path-dependence observation, not a source difference.

4. Build the payload and the BOOT image from the candidate kernel:

   ```sh
   PATH=<py311-shim>:$PATH UNIXNU_TOOLCHAIN=<prefix> python3.11 -B \
     scripts/build-gta4l-diagnostic-payload.py \
     --device . --unixnu ../UniXNU --uefi ../Mu-Silicium \
     --kernel out/gta4l/ram-root-emmc-n10.19-widthhold-candidate-evidence/candidate-kernel-23e3b6d90f4a.macho \
     --data-ramdisk out/gta4l/ram-root-fixture-hold-evidence/root.img \
     --trust-cache out/gta4l/ram-root-fixture-hold-evidence/trustcache.bundle \
     --owner-trust-cache --unique-chip-id 0x1122334455667788 \
     --data-ramdisk-manifest out/gta4l/ram-root-fixture-hold-evidence/fixture.json \
     --diagnostic emmc-block-3-widthhold \
     --output out/gta4l/ram-root-emmc-n10.19-widthhold-candidate-evidence/payload.img \
     --manifest out/gta4l/ram-root-emmc-n10.19-widthhold-candidate-evidence/payload.img.manifest.json

   PATH=<py311-shim>:$PATH python3.11 -B \
     scripts/package-gta4l-diagnostic.py \
     --baseline ../../out/gta4l/boot/boot-ram-root-sdhci-v5.img \
     --payload out/gta4l/ram-root-emmc-n10.19-widthhold-candidate-evidence/payload.img \
     --payload-sha256 bd112387ae65a676a5cbbce0cecf828e851af04c338f1c19c0e5230b21a67f15 \
     --expect-diagnostic emmc-block-3-widthhold \
     --output out/gta4l/ram-root-emmc-n10.19-widthhold-candidate-evidence/boot-ram-root-emmc-n10.19-widthhold.img \
     --manifest out/gta4l/ram-root-emmc-n10.19-widthhold-candidate-evidence/boot-ram-root-emmc-n10.19-widthhold.img.manifest.json
   ```

   The payload builder runs `check-gta4l-payload.py` and the packager runs
   both the payload contract and `check-gta4l-boot-image.py`; both gates pass.

## 4. Host qualification and status

Host checks: block 48 core scenarios/38 core mutation controls and 344 adapter
scenarios with 120 adapter/support and 14 policy mutation controls; core
189/117; probe 196/100; payload builder, packager, storage-ABI, fixture,
inventory/board, packaging and evidence-count checks pass; the payload contract
passes 66/66, the BOOT component gate 18/18, and `git diff --check` is clean.
The widthhold checker self-test covers digest agreement, wrong/truncated
digests, uppercase/short fields, missing/duplicate fields, mismatched
`complete`/`mismatches`/`rcmd6`, one-byte-changed images and rejected
`WIDTHHOLDFAIL`/missing-line reports.

This is a **host-qualified candidate only**. No widthhold hardware result
exists; F5/F3 remain their earlier bounded observations and initialization
reliability remains `[U]`. The 27,720 ms report bound still contains a
conditional pre-watchdog path, and media-ready latency is unmeasured. Nothing
here authorizes flash, boot, mount, writes, retries or extra boots; a hardware
run requires a separately approved protocol and record.
