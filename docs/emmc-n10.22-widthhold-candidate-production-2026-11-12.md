# gta4l N10.22 widthhold candidate production (2026-11-12)

Status: host-only candidate production from the N10.21 source correction at
`6b7dd95`. No device access, flash, boot, restore, mount or storage write
occurred. This record does not qualify the N10.19 image, does not reinterpret
the N10.20 failure and does not authorize a hardware run. The N10.21 record
describes a source and report-schema correction only; it did not build an
image. N10.20 remains **FAIL** at BOOT-partition discovery.

## 1. Source and build inputs

- Device working tree: `6b7dd95`, clean at build start. The candidate uses the
  corrected structural Content check and the widthhold-only `partdiag` field.
- `package/Gta4lEmmcBlock.c`
  `c4feb2d4662e0819ae9b646b78c9364b54bb66ae3c260225ab80b62b33425904`
- `package/Gta4lEmmcBlock.cpp`
  `6cc17c2a9a55fd9eb67a448461245acb5ec7e3ac469e1504b596745f652c555e`
- `package/Gta4lEmmcBlock.h`
  `16edd86ebb4c0a6bb0e53a7eb192ec6217aaf6c81dfb807a1c96b908071e4c13`
- Shared `../UniXNU` checkout at `588ff8c` with the two established fixture
  modifications: `scripts/build-ram-root-report-fixture.py`
  `1df33865b50e9e2b5e9cc587e03cb1609525aea2e399cc709690e1f95414fd91`
  and `scripts/qemu-entry/native-ram-root-fs-timer.inc`
  `b05e88ca57e0009624f896aa4859a3827ea13f0cda957761c3c2aea2e331163e`.
- XNU patch list `bc6c44e330aef35368fefd131db3d7aef5731390a2d674fc929d39a5690015c5`;
  configuration `f4c64617b7bf711dc339d2220480241f51867b6e7bff670b9dc23b26fa5725e3`.
- Toolchain: prebuilt Linux toolchain, clang 24.0.0git at llvm-project
  `58c46bae21181fb4240cf964461bea738d96d50e`, ld64-956.6.
- The build tree's `kits/gta4l` link resolved to this device `package/`; the
  `Gta4lEmmcBlock.cpp` hash in the tree matched the source hash above. The
  archive linked into the kernel contains the literal `partdiag` key and the
  `widthholdLatchPartDiag` symbol, so the corrected source is in the linked
  kernel, not only in the archive.

## 2. Candidate kernel and storage ABI

- Prelinked kernel
  `04ae57f6f5659b1a4b4236b29931d0711b3d210fefee3cb19fbf95bfd861b097`
  (51,527,680 bytes). Kernel plan JSON
  `5b02fcf9776cb6fe263cafa4b979737cda772550f00f2d08da12c4972d520dbd`.
- The `gta4l-emmc-block/libGta4lEmmcBlock.a` archive is named in the captured
  final-link archive list, and the prelink input is the linked kernel above.
- Candidate storage-ABI audit `out/gta4l/storage-abi-04ae57f6f565/`:
  `gdb-stop.json`
  `d30ad4607986cfbd5f8fc520e7dcb881d687f14467711bad269944424fcf771f`,
  `interface-audit.json`
  `f56e3f9e88a70fe195ab8762d9cac749143ee714d8b46b1b4df3dad79de635e3`,
  `provenance.json`
  `50f697f7beba3ddc9fdfedd297457e982c9eedb3bfb62f392d1fa657264122de`,
  `run.json`
  `91c9ac965f52401ce2049fa4305fa9cc764d37a476361d4307151e30fe404804`.
- Audit result: 190 method slots, 181 kernel bindings (167 inherited + 14
  pure virtual), 26 concrete overrides, 0 unresolved original imports;
  live original sizes 0x90/0x88 and native 4K. `bsd_init` stop
  `0xfffffff0076efa3c`; service metaclass `0xfffffff007b2b5b8`; vtable
  `0xfffffff005cb44c0`; metaclass `0xfffffff006add1d0`.
- `scripts/test-gta4l-storage-abi.py --artifacts
  out/gta4l/storage-abi-04ae57f6f565` passes. This is a synthetic
  QEMU-emulated interface audit, not firmware/device execution and not runtime
  transport qualification.

## 3. Archive requalification

- Archive linked into the kernel:
  `0c7e2df2a2a8c1cc9a18cc8a4e0dbe818bbf22b3f3d22cb2c5dcb67414bad517`.
- Requalified in the same build-tree environment against the candidate audit:
  the same SHA-256. No container or member difference exists.
- Members:
  `Gta4lEmmcBlock.o`
  `7662aa44e385bf6c010897a77b9556e2245a2d4a341fb73a7ef16f0aebf5a0df`,
  `Gta4lEmmcBlockCpp.o`
  `3f40c53d7a71dc7fca0518461e64837cc44d6b28474440f3836e0dcc87ff0ce3`,
  `original-bindings.o`
  `3e53b66dd177598ce54175d82e4d35b3fe6bf0572dc7ff1c78c75a6c6a461bd7`.
- The at-link build report (`aae05fec39840d915ab78a5cbeb1b5cf57391c89505c1a8d550cef3f5f707192`)
  still records the previous audit as its bootstrap binding. The requalified
  report (`032057bf216ef2142e8a39879a27815c37cc78bf0bb44fe02c97f865e84cb1a1`)
  binds `candidate_sha256`
  `04ae57f6f5659b1a4b4236b29931d0711b3d210fefee3cb19fbf95bfd861b097`,
  interface audit
  `f56e3f9e88a70fe195ab8762d9cac749143ee714d8b46b1b4df3dad79de635e3`,
  190 virtual methods, 26 overrides and 0 unresolved original imports. The
  byte-identical archive is the link qualification; the report difference is
  metadata provenance only.

## 4. Payload and BOOT image

- Payload `6491408907c6da896be9a6bf81be6b5a00d7153c656c04803f152f1b283b9ef1`
  (83,886,080 bytes); manifest
  `2678eb3fdad6e3d69c433039083db64d96e8c9b5f2926905e3f41b9dc5ac8801`.
- Payload contract: 66/66 checks true. Diagnostic personality
  `emmc-block-3-widthhold`, mapped to
  `gta4l-emmc-block=3 gta4l-emmc-widthhold=1`, with the established
  fs-timer-hold fixture (`root.img`
  `327ac826176c2fef55b28731f5839f1949ae49b0514c22443575f560d19f8c72`,
  trust cache
  `30a72f785252533c33e763bb30a7a823c85a3468ecd22d977147d7d8704fa721`,
  fixture
  `0036368153fe361df9fbdc8d5386b414cc7177ae3340b39cd412da91c07b525a`).
- Packaged payload kernel (panic-screen patched):
  `5016813923bc1c9ca803e7a02bb912823b7bd684181580a2a9f516325aaf8cbf`
  (51,527,680 bytes). The UNiXNU payload record
  (`payload-unixnu.json`
  `8ed9cdbf5e5ebb68f7ce7ee9c6984bcb4e92bc8064341e2cc16d49804a3eb7a9`)
  also records the pre-patch kernel `04ae57f6...`, the exact kernel
  command line, `panic_screen=true` and `hardware_tested=false`.
- BOOT image
  `b4d63be7ddaf70f88139b000e31a3e9e26e0cc849757db4f49696e3b9ce0877d`
  (85,688,320 bytes); manifest
  `c8569d9e90abb31d79785c3dfcba759265b0be58a3a9cc617ff0ede9d9de466f`.
- BOOT component gate: 18/18 checks true. Only the ramdisk differs from the
  `ce4d76e8024a220890e7f32e2992a7beb094087910b070fc615c6fe1ee86dcde`
  v5 baseline; the boot kernel section and non-ramdisk components are
  unchanged. Header ID
  `00eaac92ff3a4ca8a044ebc037b569e688837e61000000000000000000000000`.

## 5. Final-image digest bindings

All values were recomputed from the final BOOT image above; no historical
image digest is used as an acceptance constant.

- First 4,096 bytes:
  `417cdef0f05bceafea13119a3b9f1ff5dab52be86ab0173dfe6d563c57368400`,
  report prefix `417cdef0f05bceafea13119a3b9f1ff5`.
- First 1,024 bytes:
  `338ebbe6ab3da764f0ad87e30c2785742ea250e321b1a78bb63f914490b506f1`,
  prefix `338ebbe6ab3da764f0ad87e30c278574`.
- Last-case slice, offset 3,072, 1,024 bytes:
  `5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef`,
  prefix `5f70bf18a086007016e948b04aed3b82`. This slice digest coincides with
  the historical N10.19 value; it was still recomputed from this image and is
  not inherited.

## 6. Host qualification

Actual commands and results for this candidate:

- `scripts/test-gta4l-emmc-block.py`: rc 0; 48 core scenarios and 38 core
  mutation controls, 351 adapter scenarios and 125 adapter/support mutation
  controls (122 adapter + 3 support), plus 15 policy mutation controls. The
  widthhold PART diagnostics, structural Content cases, lost-latch/reset,
  removed-guard, reintroduced-name and omitted-field controls pass.
- `scripts/test-gta4l-emmc.py`: rc 0; 189 scenarios, 117 mutation controls.
- `scripts/test-gta4l-emmc-probe.py`: rc 0; 196 scenarios, 100 mutation
  controls, 13 width-bound materializations and 1 oversized-report control.
- `scripts/check-gta4l-payload.py --payload <payload>`: rc 0; 66/66 contract
  checks true. `scripts/check-gta4l-boot-image.py`: rc 0; 18/18 component
  checks true.
- `scripts/test-gta4l-diagnostic-payload.py`, `scripts/test-gta4l-diagnostic-package.py`,
  the payload builder/checker self-tests, `scripts/test-gta4l-storage-abi.py`,
  `scripts/test-gta4l-emmc-evidence-counts.py`, `scripts/test-gta4l-inventory.py`,
  `scripts/test-gta4l-packaging.py` and the fixture-recipe check pass.
- `scripts/check-gta4l-widthhold-evidence.py` passes against the final image
  and a clearly labeled synthetic success fixture with `partdiag=0`; it
  rejects a one-byte-changed image, nonzero, missing, duplicated and malformed
  `partdiag`, and a `WIDTHHOLDFAIL` line. Its self-test passes. The synthetic
  report is a host fixture, not a hardware observation.

## 7. Status and gates

This is a host-qualified, source-bound candidate only. N10.20 remains FAIL;
no N10.22 hardware result exists; initialization reliability, repeatability,
failure rates and 4-bit publication remain `[U]`. The currently written
N10.19 image must not be booted again without a fresh finite budget. Any
hardware continuation requires a separately approved protocol, current
p69/protected-partition state checks, explicit CMD6/read/recovery/timing
scope and a new finite boot budget. No flash, boot, mount, repair, restore or
storage write is authorized by this record.

## 8. References

- `docs/emmc-n10.20-widthhold-observation-2026-11-11.md`
- `docs/emmc-n10.21-widthhold-partdiag-candidate-2026-11-11.md`
- `docs/emmc-n10.19-widthhold-candidate-2026-11-11.md`
- `docs/emmc-n10.17-sustained-width-read-design-2026-11-11.md`
- `docs/emmc-driver.md`
- `out/gta4l/ram-root-emmc-n10.22-widthhold-candidate-evidence/` (private raw
  logs and artifacts; not tracked)
