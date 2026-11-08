# gta4l N9.5 corrected-comparator candidate production (2026-11-07)

Status: host-only candidate build of the corrected `gta4l-emmc-partcmp=1`
exact GPT-to-`IOMedia` comparator through the archive/prelink/audit/payload/
BOOT flow. The candidate carries the owner-approved N9.4 whole-media `Base`
correction. The storage-ABI audit is host/QEMU only. No flash, boot, device
access, commit or push was performed. Build success is not permission to flash
or boot, and this is not a partition-completeness or reliability result.

## 1. Scope and outcome

N9.4 reached the bounded report but failed at whole-media discovery because
the real whole media publishes no `Base`. The owner approved a host-only
correction: accept an absent whole-media `Base` as the implied zero, reject a
present nonzero or wrong-typed `Base`, and keep the partition `Base`
requirement and every other guard. N9.5 built that correction through the
established production chain and stopped at the production record.

The candidate selects chunked mode 3 plus the exact comparator
(`gta4l-emmc-block=3 gta4l-emmc-partcmp=1`). The legacy mode-1/mode-3 report
and the N8 `gta4l-emmc-partobs=1` path are unchanged when the comparator token
is absent; `partobs` and `partcmp` remain mutually exclusive in one boot.

## 2. Candidate identity

- prelinked kernel `a610e9d1…` (51,257,344 bytes; plan `737305c9…`)
- kernel-build archive `b9b7ecc0…` (644,792 bytes; members
  `Gta4lEmmcBlock.o` `fb94f96f…`, `Gta4lEmmcBlockCpp.o` `f9d2e3ed…`,
  `original-bindings.o` `3e53b66d…`)
- requalified archive `bd394f12…` (644,768 bytes; the three members are
  byte-identical to the kernel-build archive and the container differs only
  in its index)
- candidate storage-ABI audit `out/gta4l/storage-abi-a610e9d1`:
  `gdb-stop.json` `862bc0ff…`, `interface-audit.json` `5ad1bfb1…`,
  `provenance.json` `c92ece5f…`, `run.json` `09ab30ec…`; 190 method slots,
  181 kernel bindings (167 inherited + 14 pure virtual), 26 concrete
  overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
  metaclass `0xfffffff006add1d0`, service metaclass `0xfffffff007af35b8`,
  live original sizes 0x90/0x88, native 4K, candidate matches the audit,
  `runtime_qualified=false`
- packaged kernel `a17c71dd…` (51,257,344 bytes; the payload builder applies
  the pinned entry-probe/panic-screen patch)
- payload `79ac3a10…` (83,886,080 bytes; manifest `68bf9a22…`; 14/14
  contract; diagnostic `emmc-block-3-partcmp` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-partcmp=1`; hold root `327ac826…`, hold
  `fixture.json` `00363681…` with `diagnostic=fs-timer-hold` and
  `hold_after_success=true`, trust cache `30a72f78…`)
- BOOT image `45f3bb81…` (85,688,320 bytes; manifest `732e2d59…`; gate
  `299c689f…`, 18/18, only the ramdisk differs from the hardware-tested
  `ce4d76e8…` v5 baseline; baseline kernel section `a4b054e0…`, firmware
  `1e8a3132…` and boot DTB `dc4c719d…` are unchanged)
- source hashes: `Gta4lEmmcBlock.c` `417ea291…`, `Gta4lEmmcBlock.cpp`
  `b7aeff0a…`, `Gta4lEmmcBlock.h` `ce30dfff…`

The kernel-build archive records `Gta4lEmmcBlock.cpp` `b7aeff0a…` compiled to
`Gta4lEmmcBlockCpp.o` `f9d2e3ed…`; the policy object is unchanged at
`fb94f96f…`. The candidate kernel hash changed from the N9.3 `fd2c8c65…` as
expected for the corrected adapter object.

## 3. Build chain and records

The host environment prepends a private `python3.11` shim because the host
`python3` is 3.9 and lacks `tomllib`; `UNIXNU_TOOLCHAIN` points at the
prebuilt toolchain. The device tree was built from HEAD `31e719f` with the
uncommitted N9.4/N9.5 working tree; the shared tree was HEAD `588ff8c` with
the uncommitted `fs-timer-hold` fixture edits. The stale `kits/gta4l` link
was refreshed with `unixnu tree` before the build so the compile read the
device package (`Gta4lEmmcBlock.cpp` `b7aeff0a…`).

1. Corrected kernel build with the prior candidate-matched audit
   `out/gta4l/storage-abi-fd2c8c65` for the archive step: `kernel-build.log`.
   The archive step validated against the then-current `fd2c8c65…` kernel
   and recorded `build-report-kernel-build.json`.
2. Candidate-specific storage-ABI audit against `a610e9d1…`:
   `audit-a610e9d1.log` and `out/gta4l/storage-abi-a610e9d1`;
   `test-gta4l-storage-abi.py --artifacts out/gta4l/storage-abi-a610e9d1`
   passes.
3. Archive requalification against the new audit: `archive-a610e9d1.log`,
   `build-report.json` (archive `bd394f12…`, `candidate_matches_audit=true`),
   `archive-requalified.a`.
4. Payload build: `payload-build.log` -> `79ac3a10…`; the contract check is
   `payload-contract.json` (14/14 checks including exactly one partcmp token,
   partobs+partcmp exclusion and the hold-fixture requirement).
5. BOOT packaging: `package.log` -> `45f3bb81…`; `boot-image-gate.json`
   passes 18/18.
6. Host suites: `block-suite.log` (48 core scenarios/38 mutation controls;
   87 adapter scenarios/16 adapter mutation controls/eight policy mutation
   controls), `payload-suite.log` (28 checks), `package-suite.log` (11
   checks), `storage-abi-test.log`, `evidence-counts.log` and `recipe.log`.

The packaged kernel carries one `GTA4L-EMMC P5 PARTMAP` string, three
`REPORT` strings, one legacy `PART:` string, five `gta4l-emmc-partcmp`
strings and five `gta4l-emmc-partobs` strings, matching the N9.3 packaged
kernel. The binary contains both opt-in paths; the boot argument selects
`partcmp`. There is no literal `complete=`, so the PARTMAP completion value
remains runtime-formatted.

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

## 5. Limits and gates

- No candidate was flashed or booted and no device was accessed. The exact
  80-entry/84-slot GPT comparison and the direct media publication are not
  measured on hardware.
- N8.5's historical `complete=0` is not upgraded. A future `complete=1`
  would be a separate same-boot claim with the scope in
  `docs/emmc-n9.1-gpt-iomedia-comparison-design-2026-11-07.md` section 5.4.
- A fresh explicit BOOT-only approval, verified protected-partition backups
  and readbacks, and one owner-controlled boot are required before any
  hardware run. The previous boot budget is consumed.
- No mount or root switch, no writes, no wider/faster modes, no DMA/CQE/ICE
  and no request-limit changes are part of this candidate.
- Initialization reliability remains unresolved and is not measured by N9.
- No commit or push is part of N9.5.

## 6. Evidence

The host build artifacts, logs, manifests and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n9.5-candidate-evidence/` directory. The repository
records the hashes and the bounded interpretation only.
