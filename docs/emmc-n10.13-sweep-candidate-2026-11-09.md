# gta4l N10.13 bounded read-sweep qualification candidate (2026-11-09)

Status: host-only candidate build of the N10.12 opt-in `gta4l-emmc-sweep=1`
bounded aligned read sweep through the archive/prelink/audit/payload/BOOT
flow. No flash, boot, device access, commit or push was performed while
producing this record. Build success is not a hardware result. N10.12's host
implementation and the N10.10 breakup hardware result remain unchanged;
N10.14 remains separately gated.

## 1. Scope and outcome

The selector, six-case table, two page-aligned 16,384-byte buffers, direct
CMD17 reference, first-16-byte SHA-256 anchor check, observer and `complete=1`
guard are unchanged from N10.12. The mode requires exactly one block token 1/3
with value 1, refuses duplicates, orphans, non-1 values and every
partobs/partcmp/partread/deblock/breakup combination, advertises
`kIOMaximumBlockCountReadKey = 8`, `kIOMaximumByteCountReadKey = 4,096` and
`kIOMaximumSegmentCountReadKey = 1` only for itself, and performs one
`IOMedia::read()` plus one direct per-block CMD17 reference read per case.

The six cases are boot-partition reads:

| index | kind | start | length | span | blocks | subrequests | max | content |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | anchor A0 | 4096 | 4096 | 4096 | 8 | 1 | 8 | pinned `5878bcf8…b1b2` |
| 1 | anchor A1 | 4096 | 16384 | 16384 | 32 | 4 | 8 | pinned `fa43aa5d…82da` |
| 2 | R0 | 26382336 | 16384 | 16384 | 32 | 4 | 8 | runtime reference |
| 3 | R1 | 52699136 | 8192 | 8192 | 16 | 2 | 8 | runtime reference |
| 4 | R2 | 78999552 | 4096 | 4096 | 8 | 1 | 8 | runtime reference |
| 5 | R3 | 4673536 | 4096 | 4096 | 8 | 1 | 8 | runtime reference |

The two anchors are fixed boot-partition ranges. The four runtime starts are
`((i + 1) * 104729) % ((P - W) / 8) * 8` blocks for the discovered
boot-partition block count `P = 196,608` and the case block count `W`: R0 32
blocks at start block 51,528, R1 16 blocks at 102,928, R2 8 blocks at 154,296
and R3 8 blocks at 9,128. The whole table is validated before any case I/O;
production refuses a `P` other than the pinned boot-partition size.

Each case reads once through the real media path into the media buffer and
once by direct per-block CMD17 under the controller gate into the reference
buffer; the two are compared byte-for-byte, and the anchors additionally check
the pinned SHA-256 prefix. The run is `reads=6 bytes=53248 span=53248
blocks=104 subrequests=13 max=8 refs=6 refbytes=53248`.

The `complete=1` guard requires every case to pass with `reason=0`, clear
`cancel`/`budget`/`media`/`part`/`foreign` flags, `anchor=1`, `mismatches=0`,
`limit=8` and `seg=1`. The worst case is 20,000 + 2,000 + 4,000 + 1,700 =
27,700 ms, below the 30,000 ms watchdog.

## 2. Digest derivation

Both pinned anchor digests were re-verified on the host before the build from
the gate-pinned v5 BOOT image `boot-ram-root-sdhci-v5.img` (85,688,320 bytes),
whose kernel section hash `a4b054e0…` is the boot-image baseline. Offset
4,096 length 4,096 hashes to
`5878bcf8a6afe4dd79a002caa8b6aeeae16125b890ab750b67d6ab3c25f3b1b2`, and
offset 4,096 length 16,384 hashes to
`fa43aa5d40591e9b619c99c4e490054feac85ac545f5c84a38f8aa04820782da`. Both
ranges are inside the 1,798,052-byte kernel section, so the anchors are
invariant across candidates and do not depend on the ramdisk payload. The
four runtime windows have no pinned content: each is compared in the same
boot against its own direct CMD17 reference.

## 3. Candidate identity

- prelinked kernel `64bab406…` (51,363,840 bytes; plan `cd402f0e…`; original
  `f18b4df8…`)
- kernel-build archive `14f40fa8…` (1,153,488 bytes) with `Gta4lEmmcBlock.c`
  `907ac8ad…` compiled to `Gta4lEmmcBlock.o` `0c547e2f…` and
  `Gta4lEmmcBlock.cpp` `80c817a0…` compiled to `Gta4lEmmcBlockCpp.o`
  `0a0202f5…`; the bindings object stays `3e53b66d…` because the audited
  interface is unchanged
- candidate storage-ABI audit `out/gta4l/storage-abi-64bab406`:
  `gdb-stop.json` `227fec6d…`, `interface-audit.json` `bbd49581…`,
  `provenance.json` `50d546bf…`, `run.json` `e8f1b9d2…`; 190 method slots,
  181 kernel bindings (167 inherited + 14 pure virtual), 26 concrete
  overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
  metaclass `0xfffffff006add1d0`, service metaclass
  `0xfffffff007b0b5b8`, `bsd_init` `0xfffffff0076eba3c`, live original sizes
  0x90/0x88, native 4K, `disk_read_tested=false`,
  `transport_implemented=false`, `hardware_tested=false`, candidate matches
  the audit
- no audited method-target changed from the N10.9 audit; the vtable and
  metaclass addresses are unchanged and the service metaclass and `bsd_init`
  moved with the larger kernel image
- requalified archive `14f40fa8…` (1,153,488 bytes) with byte-identical
  members to the kernel-build report
- packaged kernel `f1ab2b1d…` (51,363,840 bytes; extracted from the payload
  FAT16 member `::/EFI/UniXNU/Payloads/kernel.macho`)
- payload `62371bfb…` (83,886,080 bytes; manifest `e39debae…`; gate
  `20e23cee…`; 36/36 contract; diagnostic `emmc-block-3-sweep` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-sweep=1`; hold root `327ac826…`, fixture
  `00363681…` with `diagnostic=fs-timer-hold` and
  `hold_after_success=true`, owner trust cache `30a72f78…`)
- BOOT image `b626a1fa…` (85,688,320 bytes; manifest `a43d16e3…`; gate
  `1b43740a…`; 18/18 component checks, only the ramdisk differs from the
  hardware-tested `ce4d76e8…` v5 baseline; baseline kernel section
  `a4b054e0…`, firmware `1e8a3132…` and boot DTB `dc4c719d…` are unchanged;
  header ID `68ade6a8…`)
- source hashes: `Gta4lEmmcBlock.c` `907ac8ad…`, `Gta4lEmmcBlock.cpp`
  `80c817a0…`, `Gta4lEmmcBlock.h` `d718148c…`; adapter harness
  `dc32e3a1…`, adapter support `5e644f9b…`, test support `3821cd01…`;
  payload builder `e9258120…`, payload checker `2dc4922e…`, packager
  `65fbb6f0…`, block suite `af94aa00…`

The kernel hash changed from the N10.9 candidate `71828829…` as expected for
the new sweep code path. The kernel-build report records the two source/object pairs
and the kernel-build archive `14f40fa8…`; its archive step used the N10.9
candidate-matched audit `out/gta4l/storage-abi-71828829`, while the
requalification uses the N10.13 candidate audit
`out/gta4l/storage-abi-64bab406`.

## 4. Build chain and records

The host environment prepends a private `python3.11` shim because the host
`python3` is 3.9 and lacks `tomllib`; `UNIXNU_TOOLCHAIN` points at the
prebuilt toolchain. The device tree was built from HEAD `f68682e3` (the
N10.12 implementation) with a clean working tree; `unixnu tree` refreshed the
shared tree before the build so the compile read the device package.

1. Kernel build with the N10.9 candidate-matched audit
   `out/gta4l/storage-abi-71828829` for the archive step:
   `kernel-build.log` -> prelinked kernel `64bab406…`;
   `build-report-kernel-build.json` records the two source/object pairs and
   the kernel-build archive `14f40fa8…`.
2. Candidate-specific storage-ABI audit against `64bab406…`:
   `audit-64bab406.log` and `out/gta4l/storage-abi-64bab406`;
   `test-gta4l-storage-abi.py --artifacts out/gta4l/storage-abi-64bab406`
   passes.
3. Archive requalification against the new audit from the shared tree:
   `archive-64bab406.log`, `build-report.json` (archive `14f40fa8…`,
   `candidate_matches_audit=true`, `qualified=true`, no missing
   qualification), `archive-requalified.a`; the three members hash to the
   kernel-build report's object hashes.
4. Payload build: `payload-build.log` -> `62371bfb…`; the contract check is
   recorded in `payload.img.manifest.json` (36/36 checks including exactly
   one sweep token, the sweep exclusions and the hold-fixture requirement).
5. BOOT packaging: `package.log` -> `b626a1fa…`; `boot-image-gate.json`
   passes 18/18 and `only_ramdisk_differs=true`.
6. Host suites: `block-suite.log` (48 core scenarios/38 mutation controls;
   218 adapter scenarios with 71 adapter, three support and eight policy
   mutation controls, including the sweep mutations),
   `emmc-suite.log` (187/115), `emmc-probe-suite.log` (196/100, 13
   width-bound materializations and one oversized-report control),
   `payload-suite.log` (40 checks), `package-suite.log` (15 checks),
   `boot-image-suite.log`, `storage-abi-test.log`,
   `emmc-evidence-counts.log`, `sdhci-evidence-counts.log` (33/10/41),
   `recipe.log`, `board-check.log`, `toml-check.log` and
   `git-diff-check.log`; every suite exited 0.

The packaged kernel carries three `GTA4L-EMMC P5 REPORT` strings, one
`PARTMAP`, one `READDETAIL`, one `PARTOBS`, one `PARTREAD`, one `READTEST`,
one `READFAIL`, two `DEBLOCK` (the line and the `DEBLOCKREAD` prefix), one
`DEBLOCKREAD`, two `BREAKUP` (the line and the `BREAKUPREAD` prefix), one
`BREAKUPREAD`, two `SWEEP` (the line and the `SWEEPREAD` prefix), one
`SWEEPREAD`, one legacy `PART:`, nine `gta4l-emmc-partread`, nine
`gta4l-emmc-partcmp`, nine `gta4l-emmc-partobs`, nine
`gta4l-emmc-deblock`, nine `gta4l-emmc-breakup` and nine
`gta4l-emmc-sweep` strings and no literal `complete=`. The binary contains
every opt-in path; the boot argument selects `sweep`.

## 5. Qualification results

The candidate-matched audit binds every one of the 190 audited storage slots
against the actual prelinked kernel, finds 26 concrete overrides and no
unresolved original import, and reports the live original block/service sizes
and native 4K translation. It explicitly records `disk_read_tested=false`,
`transport_implemented=false` and `hardware_tested=false`; no driver is
linked into the audit.

The requalified archive members are byte-identical to the objects recorded by
the kernel-build step, so the audit qualifies the bytes that produced the
candidate. The BOOT gate confirms the header page excluding the ID, the
kernel section, firmware and DTB match the hardware-tested `ce4d76e8…`
baseline; only the payload ramdisk differs.

The host sweep coverage pins the six cases and the expected full-pass
counters, and rejects mutations that would weaken the contract: a refused
selector value accepted, a skipped anchor comparison, a short media or
reference acceptance, a shifted reference LBA, a media/reference mismatch
accepted, a budget skip, a cancellation skip, a premature `complete=1`, a
one-segment advertisement, a skipped bounce buffer, a hardcoded runtime
start, a missing or extra split, a reordered subrequest, a wrong split count,
a skipped whole-media or boot-partition close and the selector/property/
report controls. The adapter expectations pin `cases=6 passed=6 failed=0
reason=0 complete=1 reads=6 bytes=53248 span=53248 blocks=104 subrequests=13
max=8 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 refs=6 refbytes=53248
anchor=1 mismatches=0 limit=8 seg=1 cap=61071360`; the last-case line is
`SWEEPREAD index=5 target=1 start=4673536 length=4096 span=4096 actual=4096
status=0 subrequests=1 blocks=8 max=8 ref=1 refbytes=4096 refstatus=0 mvalid=0
mismatch=0 digest=<32 hex> refdigest=<the same 32 hex>`.

The report line forms are bounded:

```
GTA4L-EMMC P5 SWEEP: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6 bytes=53248 span=53248 blocks=104 subrequests=13 max=8 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0 limit=8 seg=1 cap=61071360
GTA4L-EMMC P5 SWEEPREAD: index=5 target=1 start=4673536 length=4096 span=4096 actual=4096 status=0 subrequests=1 blocks=8 max=8 ref=1 refbytes=4096 refstatus=0 mvalid=0 mismatch=0 digest=<32 hex> refdigest=<32 hex>
```

`media`, `part` and `foreign` are failure flags: `media=1` means a whole-media
selection/profile failure, `part=1` means a boot-partition lookup failure and
`foreign=1` means a driver-level request that could not be attributed to the
active case. A successful run therefore reports `media=0 part=0 foreign=0`.
The reason codes are 1 media, 2 partition, 3 open, 4 descriptor, 5 read,
6 short, 7 hash, 8 budget, 9 cancel, 10 argument, 11 split, 12 order,
13 foreign, 14 reference/media mismatch and 15 reference failure; 0 is no
failure. The first failing case latches the reason and `complete` stays 0;
later read/short/mismatch/descriptor/split/order failures continue, foreign
traffic is counted without stopping the run, while media, partition, open,
budget, cancellation, reference-transport and anchor-mismatch failures stop
it. The sweep layer counts six media reads but 13 driver-level subrequests,
so the whole-boot `DRV`/`XFER` aggregates will be larger than `reads=6`.

## 6. Limits and gates

- This record covers the host build only. No flash, boot or device access was
  performed while producing it. N10.13 has no hardware result; a BOOT-only
  flash and cold boot (N10.14) would need fresh explicit owner approval and a
  finite boot budget.
- The pinned anchor digests qualify the retained v5 image content, not the
  current device content. A future hardware `complete=1` would be a same-boot
  consistency and content claim for those six ranges only; it would not
  qualify arbitrary I/O, mount, writes, wider/faster modes, DMA/CQE/ICE or
  initialization reliability.
- The host seam models the media/breaker path and the direct per-block CMD17
  reference; it does not run the real `IOBlockStorageDriver`/`IOBreaker` code
  or the controller transport. The hardware run is the only qualification of
  those layers.
- The two anchors are inside the gate-pinned 1,798,052-byte v5 kernel section
  and are invariant across candidate payloads; the four runtime windows
  depend on the discovered `P = 196,608`-block boot partition. The mode's
  advertised limits are eight blocks, 4,096 bytes and one segment.
- The sweep scratch is two page-aligned 16,384-byte `IOMallocAligned` buffers
  freed with `IOFreeAligned`; the object stays below the 16 KiB
  `static_assert`, the report stays below 2,048 bytes, and the 27,700 ms
  worst case stays below the 30,000 ms watchdog.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 verdicts, the N9.10
  comparator result, the N9.11 owner-reported series, the N10.1
  hardware/repeatability results, the N10.2 hardware result, the N10.6
  deblock result and the N10.10 breakup result are not upgraded. N10.10
  remains the only breakup-layer hardware observation. Initialization
  reliability remains `[U]`.
- No commit or push is part of N10.13.

## 7. Evidence

The host build artifacts, logs, manifests, gates and hashes are in the
gitignored `out/gta4l/ram-root-emmc-n10.13-sweep-candidate-evidence/`
directory. The repository records the hashes and the bounded interpretation
only. The retained digest source is the gate-pinned
`out/gta4l/boot/boot-ram-root-sdhci-v5.img`; the storage-ABI audit artifacts
are in `out/gta4l/storage-abi-64bab406/`.
