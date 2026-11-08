# gta4l N9.3 exact-comparator candidate production (2026-11-07)

Status: host-only candidate build of the opt-in `gta4l-emmc-partcmp=1` exact
GPT-to-`IOMedia` comparator through the archive/prelink/audit/payload/BOOT
flow. The storage-ABI audit is host/QEMU only. No flash, boot, device access,
commit or push was performed. Build success is not permission to flash or
boot, and this is not a partition-completeness or reliability result.

## 1. Scope and outcome

N9.3 built the approved comparator candidate and stopped at the production
record. The real kernel build rejected the N9.2 driver object at XNU's 16 KiB
kalloc type limit, so N9.3 moved the comparator scratch out of the object
(section 2) before producing the candidate. The prelinked kernel, linked
archive, candidate storage-ABI audit, packaged kernel, payload and BOOT image
form the immutable chain in section 3.

The candidate selects chunked mode 3 plus the exact comparator
(`gta4l-emmc-block=3 gta4l-emmc-partcmp=1`). The legacy mode-1/mode-3 report
and the N8 `gta4l-emmc-partobs=1` path are unchanged when the comparator token
is absent; `partobs` and `partcmp` remain mutually exclusive in one boot.

## 2. N9.2 object-size defect and the correction

The N9.2 implementation embedded the comparator scratch in `Gta4lEmmcBlock`:
a 16,384-byte GPT entry array, two 2,112-byte validated GPT copies and two
2,048-byte direct-media snapshots (24,704 bytes). The object was 38,712 bytes,
so the real kernel build failed at `OSDefineMetaClassAndStructors` with
`sizeof(Gta4lEmmcBlock) <= 16 * 1024` ("type is too large"). The host seam's
`OSDefineMetaClassAndStructors` stub does not carry that kernel assertion, so
the host suite had not seen it.

The correction adds `struct gta4l_emmc_block_partmap_scratch` with the same
members and allocates one instance with `IOMalloc` in `start()` only when the
comparator token selects the mode. The comparator methods take local
references into that block, and `free()` releases it. An allocation failure
refuses `start()` with an `IOLog` and no partial claim. `Gta4lEmmcBlock` is now
14,016 bytes, and a `static_assert(sizeof(Gta4lEmmcBlock) <= 16 * 1024)` runs
in both the host and target builds so this limit is a host-test regression
control. The host block suite still passes 48 core scenarios/38 mutation
controls and 85 adapter scenarios/16 adapter and eight policy mutation
controls.

This is a host build/structural defect, not a card, hardware, partition or
reliability result.

## 3. Candidate identity

- prelinked kernel `fd2c8c65…` (51,257,344 bytes; plan `374a9d74…`)
- kernel-build archive `408c0ce1…` (644,656 bytes; members
  `Gta4lEmmcBlock.o` `fb94f96f…`, `Gta4lEmmcBlockCpp.o` `0cb16c0e…`,
  `original-bindings.o` `3e53b66d…`)
- requalified archive `02cb5563…` (644,632 bytes; the three members are
  byte-identical to the kernel-build archive and the container adds one
  `__.SYMDEF` member)
- candidate storage-ABI audit `out/gta4l/storage-abi-fd2c8c65`:
  `gdb-stop.json` `dd20cf0b…`, `interface-audit.json` `c5cad497…`,
  `provenance.json` `75bfc26f…`, `run.json` `22d4d85c…`; 190 method slots,
  181 kernel bindings (167 inherited + 14 pure virtual), 26 concrete
  overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
  metaclass `0xfffffff006add1d0`, service metaclass `0xfffffff007af35b8`,
  live original sizes 0x90/0x88, native 4K, candidate matches the audit,
  `runtime_qualified=false`
- packaged kernel `d5a98930…` (51,257,344 bytes; the payload builder applies
  the pinned entry-probe/panic-screen patch)
- payload `fd73b37f…` (83,886,080 bytes; manifest `1de264b3…`; 14/14 contract;
  diagnostic `emmc-block-3-partcmp` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-partcmp=1`; hold root `327ac826…`, hold
  `fixture.json` `00363681…` with `diagnostic=fs-timer-hold` and
  `hold_after_success=true`, trust cache `30a72f78…`)
- BOOT image `fa55ef61…` (85,688,320 bytes; manifest `d867ff25…`; gate
  `7e29b13c…`, 18/18, only the ramdisk differs from the hardware-tested
  `ce4d76e8…` v5 baseline; baseline kernel section `a4b054e0…`, firmware
  `1e8a3132…` and boot DTB `dc4c719d…` are unchanged)
- source hashes: `Gta4lEmmcBlock.c` `417ea291…`, `Gta4lEmmcBlock.cpp`
  `062ee8fa…`, `Gta4lEmmcBlock.h` `ce30dfff…`

## 4. Build chain and records

The host environment prepends a private `python3.11` shim because the host
`python3` is 3.9 and lacks `tomllib`; `UNIXNU_TOOLCHAIN` points at the
prebuilt toolchain. The device tree was built from HEAD `31e719f` with the
uncommitted N9.2/N9.3 working tree; the shared tree was HEAD `588ff8c` with
the uncommitted `fs-timer-hold` fixture edits. The shared tree's stale
`kits/gta4l` link to a removed payload temp copy was refreshed with
`unixnu tree` before the build.

1. Candidate kernel build with the N8.3 storage-ABI directory for the archive
   step: `kernel-build.log`; the first attempt is preserved as
   `kernel-build-attempt1.log` and ends at the object-size assertion.
2. Candidate-specific storage-ABI audit: `audit-fd2c8c65.log` and
   `out/gta4l/storage-abi-fd2c8c65`; `test-gta4l-storage-abi.py` passes.
3. Archive compile/qualification against the new audit:
   `archive-fd2c8c65.log`, `build-report.json` (archive `02cb5563…`,
   `candidate_matches_audit=true`), `archive-requalified.a`.
4. Payload build: `payload-build.log` -> `fd73b37f…`; the contract check is
   `payload-contract.json` (14/14 checks including exactly one partcmp token,
   partobs+partcmp exclusion and the hold-fixture requirement).
5. BOOT packaging: `package.log` -> `fa55ef61…`; `boot-image-gate.json`
   passes 18/18.
6. Host suites: `block-suite.log` (48 core scenarios/38 mutation controls;
   85 adapter scenarios/16 adapter mutation controls/eight policy mutation
   controls), `payload-suite.log` (28 checks), `package-suite.log` (11
   checks), `storage-abi-test.log`, `evidence-counts.log` and `recipe.log`.

The packaged kernel carries one `GTA4L-EMMC P5 PARTMAP` string, three
`REPORT` strings, one legacy `PART:` string, five `gta4l-emmc-partcmp`
strings and five `gta4l-emmc-partobs` strings. The binary contains both
opt-in paths; the boot argument selects `partcmp`. There is no literal
`complete=`, so the PARTMAP completion value remains runtime-formatted.

## 5. Limits and gates

- No candidate was flashed or booted and no device was accessed. The exact
  80-entry/84-slot GPT comparison and the direct media publication are not
  measured on hardware.
- N8.5's historical `complete=0` is not upgraded. A future N9.4
  `complete=1` would be a separate same-boot claim with the scope in
  `docs/emmc-n9.1-gpt-iomedia-comparison-design-2026-11-07.md` section 5.4.
- N9.4 requires a fresh BOOT-only approval, verified protected-partition
  backups and readbacks, and one owner-controlled boot. No mount or root
  switch, no writes, no wider/faster modes, no DMA/CQE/ICE and no
  request-limit changes are part of this candidate.
- Initialization reliability remains unresolved and is not measured by N9.

## 6. Evidence

The host build artifacts, logs, manifests and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n9-candidate-evidence/` directory. The repository
records the hashes and the bounded interpretation only.
