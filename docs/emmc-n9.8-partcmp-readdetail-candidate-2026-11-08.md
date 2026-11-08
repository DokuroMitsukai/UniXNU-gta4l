# gta4l N9.8 read-detail comparator candidate production (2026-11-08)

Status: host-only candidate build of the read-detail-instrumented
`gta4l-emmc-partcmp=1` exact GPT-to-`IOMedia` comparator through the
archive/prelink/audit/payload/BOOT flow. The candidate carries the N9.7
read-detail host batch without changing transport policy. The storage-ABI
audit is host/QEMU only. No flash, boot, device access, commit or push was
performed. Build success is not permission to flash or boot, and this is not
a partition-completeness or reliability result.

## 1. Scope and outcome

N9.7 reached the real whole media, found the attached GUID scheme and failed
the first comparator LBA 0/512-byte read with `gpt=0 reason=8 reads=1
blocks=1 fails=1 samples=0 complete=0`, but the report discarded the raw
`IOReturn` and actual byte count. The owner approved a host-only diagnostic
capture that adds a bounded read-detail record to every `PARTMAP` report; the
N9.7 boot budget stays consumed and the N9.6/N9.7 `complete=0` verdicts are
not upgraded.

N9.8 built the candidate that carries that instrumentation through the
established production chain and stopped at the production record. The
candidate selects chunked mode 3 plus the exact comparator
(`gta4l-emmc-block=3 gta4l-emmc-partcmp=1`). The legacy mode-1/mode-3 report
and the N8 `gta4l-emmc-partobs=1` path are unchanged when the comparator token
is absent; `partobs` and `partcmp` remain mutually exclusive in one boot.

## 2. Candidate identity

- prelinked kernel `2777bfce…` (51,257,344 bytes; plan `c85c3abd…`)
- kernel-build archive `176b533b…` (668,024 bytes; members
  `Gta4lEmmcBlock.o` `0c76b09e…`, `Gta4lEmmcBlockCpp.o` `db6d2b74…`,
  `original-bindings.o` `3e53b66d…`)
- requalified archive `4a811585…` (668,000 bytes; the three members are
  byte-identical to the kernel-build archive and the container differs only
  in its index)
- candidate storage-ABI audit `out/gta4l/storage-abi-2777bfce`:
  `gdb-stop.json` `8a3feec1…`, `interface-audit.json` `9a7845cf…`,
  `provenance.json` `c89275ad…`, `run.json` `f4430cf1…`; 190 method slots,
  181 kernel bindings (167 inherited + 14 pure virtual), 26 concrete
  overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
  metaclass `0xfffffff006add1d0`, service metaclass `0xfffffff007af35b8`,
  live original sizes 0x90/0x88, native 4K, candidate matches the audit,
  `runtime_qualified=false`
- packaged kernel `525fdaa4…` (51,257,344 bytes; the payload builder applies
  the pinned entry-probe/panic-screen patch)
- payload `8d57eb3d…` (83,886,080 bytes; manifest `86e6c447…`; 14/14
  contract; diagnostic `emmc-block-3-partcmp` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-partcmp=1`; hold root `327ac826…`, hold
  `fixture.json` `00363681…` with `diagnostic=fs-timer-hold` and
  `hold_after_success=true`, trust cache `30a72f78…`)
- BOOT image `1e767155…` (85,688,320 bytes; manifest `969683cd…`; gate
  `cfa4227f…`, 18/18, only the ramdisk differs from the hardware-tested
  `ce4d76e8…` v5 baseline; baseline kernel section `a4b054e0…`, firmware
  `1e8a3132…` and boot DTB `dc4c719d…` are unchanged)
- source hashes: `Gta4lEmmcBlock.c` `f84d1bd2…`, `Gta4lEmmcBlock.cpp`
  `440279b2…`, `Gta4lEmmcBlock.h` `6cb1f7cd…`

The kernel-build archive records `Gta4lEmmcBlock.c` `f84d1bd2…` compiled to
`Gta4lEmmcBlock.o` `0c76b09e…` and `Gta4lEmmcBlock.cpp` `440279b2…` compiled
to `Gta4lEmmcBlockCpp.o` `db6d2b74…`; the bindings object is unchanged at
`3e53b66d…`. The candidate kernel hash changed from the N9.7 `06826fc5…` as
expected for the read-detail report fields and formatter.

## 3. Build chain and records

The host environment prepends a private `python3.11` shim because the host
`python3` is 3.9 and lacks `tomllib`; `UNIXNU_TOOLCHAIN` points at the
prebuilt toolchain. The device tree was built from HEAD `31e719f` with the
uncommitted N9.7 read-detail working tree; the shared tree was refreshed with
`unixnu tree` before the build so the compile read the device package
(`Gta4lEmmcBlock.cpp` `440279b2…`, header `6cb1f7cd…`).

1. Kernel build with the prior candidate-matched audit
   `out/gta4l/storage-abi-06826fc5` for the archive step: `kernel-build.log`
   -> prelinked kernel `2777bfce…`; the archive step recorded
   `build-report-kernel-build.json` against the then-current N9.7 kernel.
2. Candidate-specific storage-ABI audit against `2777bfce…`:
   `audit-2777bfce.log` and `out/gta4l/storage-abi-2777bfce`;
   `test-gta4l-storage-abi.py --artifacts out/gta4l/storage-abi-2777bfce`
   passes.
3. Archive requalification against the new audit from the shared tree:
   `archive-2777bfce.log`, `build-report.json` (archive `4a811585…`,
   `candidate_matches_audit=true`), `archive-requalified.a`.
4. Payload build: `payload-build.log` -> `8d57eb3d…`; the contract check is
   `payload-contract.json` (14/14 checks including exactly one partcmp token,
   partobs+partcmp exclusion and the hold-fixture requirement).
5. BOOT packaging: `package.log` -> `1e767155…`; `boot-image-gate.json`
   passes 18/18.
6. Host suites: `block-suite.log` (48 core scenarios/38 mutation controls;
   97 adapter scenarios with 23 adapter and eight policy mutation controls),
   `payload-suite.log` (28 checks), `package-suite.log` (11 checks),
   `boot-image-suite.log`, `storage-abi-test.log`, `evidence-counts.log`,
   `recipe.log`, `board-check.log` and `diff-check.log`.

The packaged kernel carries one `GTA4L-EMMC P5 PARTMAP` string, three
`GTA4L-EMMC P5 REPORT` strings, one `GTA4L-EMMC P5 READDETAIL` string, one
`GTA4L-EMMC P5 PARTOBS` string, one legacy `PART:` string, five
`gta4l-emmc-partcmp` strings and five `gta4l-emmc-partobs` strings, matching
the N9.7 packaged kernel plus the new detail line. The binary contains both
opt-in paths; the boot argument selects `partcmp`. There is no literal
`complete=`, so the PARTMAP completion value remains runtime-formatted.

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

The host read-detail coverage pins descriptor-allocation and open refusal
(`state=0`, `reason=10`), `kIOReturnDMAError` (`state=2`, `status=3758097108`,
`actual=0`), `actual=0`/`511` and a 512-of-1024-byte short transfer
(`state=3`), the in-flight watchdog snapshot (`state=1`) and full completion
(`state=4`); five adapter mutation controls reject a discarded status, actual,
descriptor length, short state or in-flight state.

## 5. Limits and gates

- No candidate was flashed or booted and no device was accessed. The exact
  80-entry/84-slot GPT comparison and the direct media publication are not
  measured on hardware.
- The hardware `reason=8` cause remains unresolved. The host seam calls
  `doAsyncReadWrite` directly and does not qualify the real context, deblock
  or breakup layers; physical adjacency, `IOBreaker` allocation failure and
  the page-straddle hypothesis all remain unselected inputs. No buffer,
  segment-limit or request-limit change is authorized by this record.
- N8.5's historical `complete=0` and the N9.6/N9.7 verdicts are not
  upgraded. A future `complete=1` would be a separate same-boot claim with
  the scope in `docs/emmc-n9.1-gpt-iomedia-comparison-design-2026-11-07.md`
  section 5.4.
- A fresh explicit BOOT-only approval, verified protected-partition backups
  and readbacks, and one owner-controlled boot are required before any
  hardware run. The previous boot budget is consumed.
- No mount or root switch, no writes, no wider/faster modes, no DMA/CQE/ICE
  and no request-limit changes are part of this candidate.
- Initialization reliability remains unresolved and is not measured by N9.
- No commit or push is part of N9.8.

## 6. Evidence

The host build artifacts, logs, manifests and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n9.8-candidate-evidence/` directory. The repository
records the hashes and the bounded interpretation only.

## 7. N9.9 follow-through (2026-11-08)

The N9.8 trace selected the page-straddle path. The owner approved a host-only
correction recorded in
`docs/emmc-n9.9-page-aligned-comparator-buffer-2026-11-08.md` and
`docs/emmc-driver.md` §5.3.70: comparator reads now use one page-aligned
bounce buffer, with the one-segment and request limits and the `READDETAIL`
line unchanged. The host suites pass (48 core scenarios/38 mutation controls,
99 adapter scenarios with 24 adapter and eight policy mutation controls). The
correction is not hardware-verified; the N9.8 `complete=0` verdict and the
consumed boot budget are unchanged, and a new candidate build, flash or boot
requires separate owner approval.
