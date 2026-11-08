# gta4l N9.10 page-aligned comparator candidate production (2026-11-08)

Status: host-only candidate build of the N9.9 page-aligned exact
GPT-to-`IOMedia` comparator (`gta4l-emmc-block=3 gta4l-emmc-partcmp=1`)
through the archive/prelink/audit/payload/BOOT flow. The candidate keeps the
advertised one-segment and request limits and the bounded `READDETAIL` line.
The storage-ABI audit is host/QEMU only. No flash, boot or device access was
performed while producing this record; the separately approved BOOT-only flash
and one owner-controlled cold boot are recorded in the hardware observation.
Build success is not a partition-completeness or reliability result.

## 1. Scope and outcome

N9.8 measured `READDETAIL state=2 lba=0 blocks=1 bytes=512 status=3758097108
actual=0 desc_len=512 desc_dir=1` on hardware; `3758097108` is `0xe00002d4`
(`kIOReturnDMAError`). The host trace selected
`IOBlockStorageDriver::breakUpRequest()` with `getBreakSize()==0` as the only
pre-adapter DMA-error exit under the advertised one-segment read limit, and
N9.9 corrected the comparator to read through one page-aligned bounce buffer
without changing the advertised limits. N9.10 carries that correction through
the established production chain.

The candidate selects chunked mode 3 plus the exact comparator. The legacy
mode-1/mode-3 report and the N8 `gta4l-emmc-partobs=1` path are unchanged when
the comparator token is absent; `partobs` and `partcmp` remain mutually
exclusive in one boot.

## 2. Candidate identity

- prelinked kernel `fcc61714…` (51,257,344 bytes; plan `240ea822…`)
- kernel-build archive `b8557d3e…` (668,904 bytes; members
  `Gta4lEmmcBlock.o` `bf2c80ce…`, `Gta4lEmmcBlockCpp.o` `44a5686d…`,
  `original-bindings.o` `3e53b66d…`)
- requalified archive `0575c413…` (668,880 bytes; the three members are
  byte-identical to the kernel-build archive and the container differs only
  in its index)
- candidate storage-ABI audit `out/gta4l/storage-abi-fcc61714`:
  `gdb-stop.json` `e9e01873…`, `interface-audit.json` `4f66b116…`,
  `provenance.json` `64e46c60…`, `run.json` `94746dec…`; 190 method slots,
  181 kernel bindings (167 inherited + 14 pure virtual), 26 concrete
  overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
  metaclass `0xfffffff006add1d0`, service metaclass `0xfffffff007af35b8`,
  live original sizes 0x90/0x88, native 4K, candidate matches the audit,
  `runtime_qualified=false`
- packaged kernel `149b1e6c…` (51,257,344 bytes; the payload builder applies
  the pinned entry-probe/panic-screen patch)
- payload `800d293d…` (83,886,080 bytes; manifest `c600a8fd…`; 14/14
  contract; diagnostic `emmc-block-3-partcmp` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-partcmp=1`; hold root `327ac826…`, hold
  `fixture.json` `00363681…` with `diagnostic=fs-timer-hold` and
  `hold_after_success=true`, trust cache `30a72f78…`)
- BOOT image `dc79fab2…` (85,688,320 bytes; manifest `057efbb5…`; gate
  `3290d179…`, 18/18, only the ramdisk differs from the hardware-tested
  `ce4d76e8…` v5 baseline; baseline kernel section `a4b054e0…`, firmware
  `1e8a3132…` and boot DTB `dc4c719d…` are unchanged)
- source hashes: `Gta4lEmmcBlock.c` `f84d1bd2…`, `Gta4lEmmcBlock.cpp`
  `78144956…`, `Gta4lEmmcBlock.h` `6c51b5b3…`

The kernel-build archive records `Gta4lEmmcBlock.c` `f84d1bd2…` compiled to
`Gta4lEmmcBlock.o` `bf2c80ce…` and `Gta4lEmmcBlock.cpp` `78144956…` compiled
to `Gta4lEmmcBlockCpp.o` `44a5686d…`; the bindings object is unchanged at
`3e53b66d…`. The candidate kernel hash changed from the N9.8 `2777bfce…` as
expected for the page-aligned bounce allocation, descriptor source and copy.

## 3. Build chain and records

The host environment prepends a private `python3.11` shim because the host
`python3` is 3.9 and lacks `tomllib`; `UNIXNU_TOOLCHAIN` points at the
prebuilt toolchain. The device tree was built from HEAD `31e719f` with the
uncommitted N9.9 working tree; `unixnu tree` refreshed the shared tree before
the build so the compile read the device package.

1. Kernel build with the N9.8 candidate-matched audit
   `out/gta4l/storage-abi-2777bfce` for the archive step: `kernel-build.log`
   -> prelinked kernel `fcc61714…`; the archive step recorded
   `build-report-kernel-build.json` against the then-current N9.8 kernel.
2. Candidate-specific storage-ABI audit against `fcc61714…`:
   `audit-fcc61714.log` and `out/gta4l/storage-abi-fcc61714`;
   `test-gta4l-storage-abi.py --artifacts out/gta4l/storage-abi-fcc61714`
   passes.
3. Archive requalification against the new audit from the shared tree:
   `archive-fcc61714.log`, `build-report.json` (archive `0575c413…`,
   `candidate_matches_audit=true`), `archive-requalified.a`.
4. Payload build: `payload-build.log` -> `800d293d…`; the contract check is
   recorded in `payload.img.manifest.json` (14/14 checks including exactly one
   partcmp token, partobs+partcmp exclusion and the hold-fixture requirement).
5. BOOT packaging: `package.log` -> `dc79fab2…`; `boot-image-gate.json`
   passes 18/18.
6. Host suites: `block-suite.log` (48 core scenarios/38 mutation controls;
   99 adapter scenarios with 24 adapter and eight policy mutation controls),
   `payload-suite.log`, `package-suite.log`, `boot-image-suite.log`,
   `storage-abi-test.log`, `evidence-counts.log`, `recipe.log`,
   `board-check.log` and `diff-check.log`.

The packaged kernel carries one `GTA4L-EMMC P5 PARTMAP` string, three
`GTA4L-EMMC P5 REPORT` strings, one `GTA4L-EMMC P5 READDETAIL` string, one
`GTA4L-EMMC P5 PARTOBS` string, one legacy `PART:` string, five
`gta4l-emmc-partcmp` strings and five `gta4l-emmc-partobs` strings, matching
the N9.8 packaged kernel. The binary contains both opt-in paths; the boot
argument selects `partcmp`. There is no literal `complete=`, so the PARTMAP
completion value remains runtime-formatted.

## 4. Qualification results

The candidate-matched audit binds every one of the 190 audited storage slots
against the actual prelinked kernel, finds 26 concrete overrides and no
unresolved original import, and reports the live original block/service
sizes and native 4K translation. It explicitly records
`disk_read_tested=false`, `transport_implemented=false` and
`hardware_tested=false`; no driver is linked into the audit.

The requalified archive members are byte-identical to the archive used to
link the candidate kernel, so the audit qualifies the bytes that produced the
candidate. The BOOT gate confirms the header page excluding the ID, the
kernel section, firmware and DTB match the hardware-tested `ce4d76e8…`
baseline; only the payload ramdisk differs.

The host comparator coverage keeps the N9.9 page-aligned model: the seam
models the worst-case `breakUpRequest()` constraint (one segment per page,
truncation to the 512-byte request block size, zero break size returns
`kIOReturnDMAError`), `partcmp-page-aligned` runs the full 25-read comparator
with zero rejected requests and zero unaligned descriptors, and
`partcmp-page-split` reads into a destination at page offset 3,608 and
requires the exact PMBR copy. The `partcmp-bounce-skip` mutation restores the
caller buffer and is rejected by that scenario.

## 5. Limits and gates

- This production record covers the host build only. The separately approved
  BOOT-only flash and one owner-controlled cold boot are recorded in the N9.10
  hardware observation.
- The host seam calls `doAsyncReadWrite` directly and does not qualify the
  real context, deblock or breakup layers; physical adjacency and `IOBreaker`
  allocation behavior remain host reconstructions. The N9.8 hardware
  `reason=8` cause is still unproven beyond the trace-selected page-straddle
  path.
- N8.5's historical `complete=0` and the N9.6/N9.7/N9.8 verdicts are not
  upgraded. A future `complete=1` would be a separate same-boot claim with
  the scope in `docs/emmc-n9.1-gpt-iomedia-comparison-design-2026-11-07.md`
  section 5.4.
- No mount or root switch, no writes, no wider/faster modes, no DMA/CQE/ICE
  and no request-limit changes are part of this candidate.
- Initialization reliability remains unresolved and is not measured by N9.
- No commit or push is part of N9.10.

## 6. Evidence

The host build artifacts, logs, manifests and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n9.10-candidate-evidence/` directory. The repository
records the hashes and the bounded interpretation only.
