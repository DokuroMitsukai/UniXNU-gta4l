# gta4l N10.15 read-only consumer qualification candidate (2026-11-10)

Status: host-only candidate build of the N10.15 opt-in `gta4l-emmc-consume=1`
read-only BOOT-window consumer through the archive/prelink/audit/payload/BOOT
flow. No flash, boot or device access was performed while producing this
record. Build success is not a hardware result. The external content
reference is rebound to the packaged image. The owner approved candidate
production and one hardware run and accepts the conditional §7.4 timing
margin (25,000 ms against the 30,000 ms watchdog) as the basis for that run;
this is an acceptance, not a timing proof. N10.14 remains the only
sweep-mode hardware result, N10.10 remains the only breakup-layer hardware
result and initialization reliability remains `[U]`.

## 1. Scope and outcome

The selector, production advertisement, distinct `IOService` consumer, one
1,024-byte BOOT window at offset 0, 32 KiB `IOMallocContiguous` buffer,
one-segment descriptor, entry-side tag/range observer, completion-side
private counter deltas, option A digest reporting and `complete=1` guard are
unchanged from the N10.15 host implementation (`c53d70a`).

The single case is BOOT offset 0, length 1,024 bytes (two blocks), read once
through the real media path with the production 64-block/32,768-byte/
one-segment advertisement. The expected full-pass aggregate is
`reads=1 bytes=1024 span=1024 blocks=2 subrequests=1 max=2 foreign=0 fails=0
cancel=0 budget=0 media=0 part=0 open=1 close=1 alloc=0 client=1
mismatches=0 limit=64 seg=1 cap=61071360`; the consumer records the observed
first-16-byte SHA-256 prefix and prints `expected` as all zeros with
`mismatch=0`. The host compares the printed prefix with the external
reference after the run. No expected digest is compiled in.

The window is the first 1,024 bytes of the Android boot v1 header page. The
header SHA-1 at offsets 576-595 covers the kernel section and the ramdisk
(`scripts/package-gta4l-diagnostic.py:209-216`), and the XNU kernel is in the
FAT ramdisk, so the window changes with the candidate payload. The previous
N10.13 reference `5a25ed5e…` therefore does not apply to this image.

The worst case remains the conditional 20,000 + 2,000 + 3,000 = 25,000 ms
argument with a 5,000 ms margin under the 30,000 ms watchdog; the margin
must also cover allocation, gate contention and scheduling. A watchdog fire
is a no-go, not a completed case.

## 2. Digest derivation and external binding

The candidate BOOT image is `777b446c2646e7f4633cb3681bd2a0e9098aae0cb64
2299c7023d1d24182d7e9` (85,688,320 bytes). Its first 1,024 bytes hash to
`97479ae876ec6fe6f45b38492b9eb9ac934b092ee8bcdccd3167cdc4eb502c7a`; the
external prefix is `97479ae876ec6fe6f45b38492b9eb9ac`. The packaged header ID
is `4b15eb2a…` (the v5 baseline is `f212ebc8…`), and the BOOT gate confirms
that the header page excluding the ID, the kernel section, firmware and boot
DTB are unchanged from the hardware-tested `ce4d76e8…` v5 baseline; only the
ramdisk differs.

The on-device `CONSUMEREAD` digest must equal the external prefix
`97479ae876ec6fe6f45b38492b9eb9ac`. A different prefix fails acceptance
host-side only and does not change the on-device `complete` field, because
the comparison happens after the report is transcribed.

## 3. Candidate identity

- prelinked kernel `d943c228…` (51,437,568 bytes; plan `29b7a386…`; original
  `f18b4df8…`)
- kernel-build archive `12d851a8…` (1,339,872 bytes) with
  `Gta4lEmmcBlock.c` `9587c98a…` compiled to `Gta4lEmmcBlock.o`
  `cd224a74…` and `Gta4lEmmcBlock.cpp` `86dab8b1…` compiled to
  `Gta4lEmmcBlockCpp.o` `eff8fd7c…`; the bindings object stays `3e53b66d…`
  because the audited interface is unchanged
- candidate storage-ABI audit `out/gta4l/storage-abi-d943c228`:
  `gdb-stop.json` `92ccffbf…`, `interface-audit.json` `fdfae103…`,
  `provenance.json` `a9088e50…`, `run.json` `615b27d2…`; 190 method slots,
  181 kernel bindings (167 inherited + 14 pure virtual), 26 concrete
  overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
  metaclass `0xfffffff006add1d0`, service metaclass
  `0xfffffff007b175b8`, `bsd_init` `0xfffffff0076efa3c`, live original sizes
  0x90/0x88, native 4K, `disk_read_tested=false`,
  `transport_implemented=false`, `hardware_tested=false`, candidate matches
  the audit
- requalified archive `12d851a8…` with byte-identical members to the
  kernel-build report; `build-report.json` records `qualified=true`,
  `candidate_matches_audit=true`, 190 virtual method slots, 26 concrete
  overrides, 0 unresolved original imports and 238 non-bound imports
  checked; the consumer probe has 187 imports split 185/1/1
  (`__TEXT_EXEC,__text` / `__DATA_CONST,__const` / `__DATA,__common`), all
  exported by the candidate
- packaged kernel `e5ba92e3…` (51,437,568 bytes; extracted from the payload
  FAT16 member `::/EFI/UniXNU/Payloads/kernel.macho`)
- payload `e05eb1cb…` (83,886,080 bytes; manifest `c23aaa9b…`; 45/45
  contract; diagnostic `emmc-block-3-consume` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-consume=1`; hold root `327ac826…`, fixture
  `00363681…` with `diagnostic=fs-timer-hold` and
  `hold_after_success=true`, owner trust cache `30a72f78…`; the payload
  record binds kernel `d943c228…`, packaged kernel `e5ba92e3…`,
  `panic_screen=true` and `hardware_tested=false`)
- BOOT image `777b446c…` (85,688,320 bytes; manifest `09dfe4ea…`; gate
  `22587d59…`; 18/18 component checks, only the ramdisk differs from the
  `ce4d76e8…` v5 baseline; baseline kernel section `a4b054e0…`, firmware
  `1e8a3132…` and boot DTB `dc4c719d…` are unchanged; header ID
  `4b15eb2a…`; first-1,024-byte digest `97479ae8…`)
- source hashes: `Gta4lEmmcBlock.c` `9587c98a…`, `Gta4lEmmcBlock.cpp`
  `86dab8b1…`, `Gta4lEmmcBlock.h` `59c8ff45…`; adapter harness
  `c479bc62…`, adapter support `68ae59cb…`, test support `be734e97…`,
  consumer probe `984ab609…`; payload builder `b47e0072…`, payload checker
  `fa9983db…`, packager `d1cd7e19…`, block suite `b8b78c85…`

The kernel hash changed from the N10.13 candidate `64bab406…` as expected
for the new consumer code path. The kernel-build report records the two
source/object pairs and the kernel-build archive `12d851a8…`; its archive
step used the N10.13 candidate-matched audit
`out/gta4l/storage-abi-64bab406`, while the requalification uses the N10.15
candidate audit `out/gta4l/storage-abi-d943c228`.

## 4. Build chain and records

The host environment prepends a private `python3.11` shim because the host
`python3` is 3.9 and lacks `tomllib`; `UNIXNU_TOOLCHAIN` points at the
prebuilt toolchain. The device tree was built from HEAD `c53d70a` with a
clean working tree; `unixnu tree` refreshed the shared tree before the build
so the compile read the device package. The shared `UniXNU` checkout was at
HEAD `588ff8c` with the two established fs-timer-hold fixture modifications
(`scripts/build-ram-root-report-fixture.py` and
`scripts/qemu-entry/native-ram-root-fs-timer.inc`).

1. Kernel build with the N10.13 candidate-matched audit
   `out/gta4l/storage-abi-64bab406` for the archive step:
   `kernel-build.log` -> prelinked kernel `d943c228…`;
   `build-report-kernel-build.json` records the two source/object pairs and
   the kernel-build archive `12d851a8…`.
2. Candidate-specific storage-ABI audit against `d943c228…`:
   `audit-d943c228.log` and `out/gta4l/storage-abi-d943c228`;
   `test-gta4l-storage-abi.py --artifacts out/gta4l/storage-abi-d943c228`
   passes.
3. Archive requalification against the new audit from the shared tree:
   `archive-d943c228.log`, `build-report.json` (archive `12d851a8…`,
   `candidate_matches_audit=true`, `qualified=true`, no missing
   qualification), `archive-requalified.a`; the three members hash to the
   kernel-build report's object hashes.
4. Payload build: `payload-build.log` -> `e05eb1cb…`; the contract check is
   recorded in `payload.img.manifest.json` (45/45 checks including exactly
   one consume token, the consume exclusions, the block-3 requirement and
   the hold-fixture requirement).
5. BOOT packaging: `package.log` -> `777b446c…`; `boot-image-gate.json`
   passes 18/18 and `only_ramdisk_differs=true`.
6. Host suites: `block-suite.log` (48 core scenarios/38 mutation controls;
   248 adapter scenarios with 95 adapter/support and 10 policy mutation
   controls, including the consume mutations), `emmc-suite.log` (187/115),
   `emmc-probe-suite.log` (196/100, 13 width-bound materializations and one
   oversized-report control), `payload-suite.log` (42 checks),
   `package-suite.log` (16 checks), `boot-image-suite.log`,
   `storage-abi-test.log`, `emmc-evidence-counts.log`,
   `sdhci-evidence-counts.log` (33/10/41), `recipe.log`, `board-check.log`,
   `toml-check.log` and `git-diff-check.log`; every suite exited 0.

The packaged kernel carries one `CONSUME` line string, one `CONSUMEREAD`
string, one `CONSUMEFAIL` string, ten `gta4l-emmc-consume` strings and no
literal `complete=`. The binary contains every opt-in path; the boot argument
selects `consume`.

## 5. Qualification results

The candidate-matched audit binds every one of the 190 audited storage slots
against the actual prelinked kernel, finds 26 concrete overrides and no
unresolved original import, and reports the live original block/service
sizes and native 4K translation. It explicitly records
`disk_read_tested=false`, `transport_implemented=false` and
`hardware_tested=false`; no driver is linked into the audit.

The requalified archive members are byte-identical to the objects recorded by
the kernel-build step, so the audit qualifies the bytes that produced the
candidate. The extended archive gate compiles the one-read consumer probe and
confirms that all 187 imports are candidate-kernel exports; the automated
subset check is additional to, not a substitute for, the layout/vtable/
metaclass qualification. The BOOT gate confirms the header page excluding the
ID, the kernel section, firmware and DTB match the hardware-tested
`ce4d76e8…` baseline; only the payload ramdisk differs.

The host consume coverage pins the one-case table and the expected full-pass
counters, and rejects mutations that would weaken the contract: a refused
selector value accepted, block 1, a duplicate, an orphan, a non-1 or
malformed value, and every conflicting mode; a skipped open, close or client
release; a skipped contiguous-allocation check or a fragmented buffer; a
skipped `getPhysicalSegment` one-segment check or a short segment; a missing,
zero, wrong or duplicate tag; a skipped descriptor release; a missing or
swapped digest; a short read accepted; a changed fixed offset or length; a
skipped partition base/size/writable check; skipped cancellation, budget or
foreign attribution; skipped `complete` guard terms (`reads`, `bytes`,
`blocks`, `subrequests`, `max`, `limit`, `seg`, `open`, `close`, `alloc`,
`client`); the sweep 8-block or deblock three-segment advertisement; retained
`READ`/`QUAL`/`QUAL3` in consume mode or consume lines in legacy mode; and a
per-line or total report width overflow. The adapter expectations pin
`cases=1 passed=1 failed=0 reason=0 complete=1 reads=1 bytes=1024 span=1024
blocks=2 subrequests=1 max=2 foreign=0 fails=0 cancel=0 budget=0 media=0
part=0 open=1 close=1 alloc=0 client=1 mismatches=0 limit=64 seg=1
cap=61071360`.

The report line forms are bounded:

```
GTA4L-EMMC P5 CONSUME: cases=1 passed=1 failed=0 reason=0 complete=1 reads=1 bytes=1024 span=1024 blocks=2 subrequests=1 max=2 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 open=1 close=1 alloc=0 client=1 mismatches=0 limit=64 seg=1 cap=61071360
GTA4L-EMMC P5 CONSUMEREAD: index=0 target=1 start=0 length=1024 span=1024 actual=1024 status=0 subrequests=1 blocks=2 max=2 digest=<32 hex> expected=00000000000000000000000000000000 mismatch=0
```

`complete=1` requires `cases == 1`, `passed == 1`, `failed == 0`, the latched
reason `NONE`, `reads == 1`, `bytes == 1024`, `span == 1024`, `blocks == 2`,
`subrequests == 1`, `max == 2`, `foreign == 0`, `fails == 0`, `cancel == 0`,
`budget == 0`, `media == 0`, `part == 0`, `open == 1`, `close == 1`,
`alloc == 0`, `client == 1`, `limit == 64` and `segments == 1`. It cannot
coexist with a read failure, short transfer, wrong offset, wrong partition,
open/close mismatch, allocation failure, cancellation, budget refusal,
foreign traffic or a wrong advertisement. Under option A the guard does not
include content; the host comparison covers the digest.

The failure line is `CONSUMEFAIL` with reason, case, target, start, length,
span, requested, completed, actual, status, digest, expected and mismatch.
The reason codes are 0 none, 1 media, 2 partition, 3 open, 4 allocation,
5 descriptor, 6 read, 7 short, 8 hash (option B only), 9 budget,
10 cancellation, 11 argument, 12 order, 13 foreign, 14 client and
15 close. Consume mode suppresses `READ`/`QUAL`/`QUAL3`; every other legacy
line is unchanged, and every non-consume mode stays byte-for-byte identical.
The three line caps are 320/352/384 bytes and the pinned worst report is
1,959 content bytes inside the 2,048-byte report.

## 6. Limits and gates

- This record covers the host build only. No flash, boot or device access was
  performed while producing it. N10.15 has no hardware result; the separately
  approved BOOT-only flash and one owner-controlled cold boot have a finite
  one-boot budget.
- The external digest qualifies the first 1,024 bytes of the packaged image
  `777b446c…` only. It is a pre-run host binding plus a post-run comparison,
  not an on-device content check; a host-side mismatch fails acceptance
  without changing `complete`.
- The window is the boot header page and changes with the ramdisk; the
  previous `5a25ed5e…` reference does not apply. No compiled-in expected
  digest exists.
- The host seam models the media/breaker path and the consumer lifecycle; it
  does not run the real `IOBlockStorageDriver`/`IOBreaker` code or the
  controller transport. The hardware run is the only qualification of those
  layers for this window.
- The 25,000 ms worst case is conditional and accepted by the owner for the
  approved run; it is not a complete path-condition proof. A watchdog fire
  is a no-go and must not be reported as a completed case.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 verdicts, the N9.10
  comparator result, the N9.11 owner-reported series, the N10.1/N10.2
  partread results, the N10.6 deblock result, the N10.10 breakup result and
  the N10.14 sweep result are not upgraded. N10.14 remains the only
  sweep-mode hardware observation; N10.10 remains the only breakup-layer
  hardware observation; initialization reliability remains `[U]`.
- No flash, boot or device access is part of this candidate record; the
  owner-approved hardware run is a separate batch.

## 7. Evidence

The host build artifacts, logs, manifests, gates and hashes are in the
gitignored `out/gta4l/ram-root-emmc-n10.15-candidate-evidence/` directory.
The repository records the hashes and the bounded interpretation only. The
storage-ABI audit artifacts are in `out/gta4l/storage-abi-d943c228/`; the
retained digest source is the packaged BOOT image
`out/gta4l/ram-root-emmc-n10.15-candidate-evidence/boot-ram-root-emmc-n10.15-consume.img`
(`777b446c…`), whose first 1,024 bytes hash to `97479ae8…`.
