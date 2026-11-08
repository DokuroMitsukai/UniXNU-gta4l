# gta4l N10.5 unaligned deblocked read qualification candidate (2026-11-08)

Status: host-only candidate build of the N10.4 opt-in `gta4l-emmc-deblock=1`
unaligned read qualification through the archive/prelink/audit/payload/BOOT
flow. No flash, boot, device access, commit or push was performed while
producing this record. Build success is not a hardware result. N10.4's host
implementation and the N10.1/N10.2 hardware results remain unchanged; N10.6
remains separately gated.

## 1. Scope and outcome

The selector, media/partition discovery, page-aligned 33,792-byte bounce
buffer, digest comparison, reason codes and `complete=1` guard are unchanged
from N10.4. The mode requires exactly one block token 1/3 with value 1,
refuses duplicates, orphans, non-1 values and every partobs/partcmp/partread
combination, advertises `kIOMaximumSegmentCountReadKey = 3` only for itself,
and performs one `IOMedia::read()` per case through the real
`IOMedia`/`IOBlockStorageDriver`/`IODeblocker` path. Each case compares the
first 16 bytes of a portable SHA-256 over the exact requested bytes to a
pinned 32-byte digest.

The eleven fixed cases are:

| index | target | start | length | span | blocks | SHA-256 (first 8…last 6) |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | whole | 100 | 924 | 1024 | 2 | `5f4f363c…301d3719` |
| 1 | whole | 0 | 600 | 1024 | 2 | `e3ef996d…07d59d01` |
| 2 | whole | 100 | 200 | 512 | 1 | `6d9c54de…e0c3a33a` |
| 3 | whole | 100 | 600 | 1024 | 2 | `062e84ac…d90dae55` |
| 4 | whole | 100 | 16000 | 16384 | 32 | `4f499bf0…19cd0fe2` |
| 5 | whole | 512 | 1024 | 1024 | 2 | `4e3575fe…474d1f70` |
| 6 | boot | 4196 | 924 | 1024 | 2 | `ff346b8d…3e36d7d1` |
| 7 | boot | 1048676 | 200 | 512 | 1 | `cdb2d0c4…f6b7bf53` |
| 8 | boot | 100630528 | 32668 | 32768 | 64 | `095a6573…5bf4cc63` |
| 9 | boot | 100662884 | 200 | 512 | 1 | `6d9c54de…e0c3a33a` |
| 10 | boot | 4096 | 1024 | 1024 | 2 | `b50aab9d…aa959774` |

Cases 0-4 exercise head-only, tail-only, both ends inside one block, both
ends across two blocks and both ends across a multi-page 32-block span; cases
6-9 exercise the boot base with head-only, same-block, 64-block tail-only and
same-block-at-media-end reads; cases 5 and 10 are aligned controls that
construct no deblocker. The run is `reads=11 bytes=54364 span=56832
blocks=111` (sum of the requested lengths, aligned spans and `span / 512`).
Case 9 shares case 2's digest because both 200-byte ranges are zero-filled;
case 8 is also a zero-filled boot-partition tail.

The `complete=1` guard requires every case to pass with `reason=0`, clear
`cancel`/`budget`/`media`/`part` flags, `seg=3` and every status/actual/digest
check. The per-read 4,000 ms budget covers the whole case table; a slow device
stops with `budget=1` and `complete=0` rather than extending the 30,000 ms
watchdog.

## 2. Digest derivation

All eleven pinned digests were re-derived on the host before the build from
the retained out-of-band evidence:

- cases 0-5 equal the corresponding ranges of the retained whole-media
  recovery readback `lbas-0-33.bin` (17,408 bytes; LBAs 0-33);
- cases 6, 7 and 10 equal the corresponding ranges of the gate-pinned v5 BOOT
  image `boot-ram-root-sdhci-v5.img` (85,688,320 bytes), whose kernel section
  hash `a4b054e0…` is the boot-image baseline;
- cases 8 and 9 equal the corresponding ranges of the retained full BOOT
  partition backup `boot-backup-20261027-pmgr.img` (100,663,296 bytes).

The boot-tail boundary matters: cases 8 and 9 address offsets
100,630,528-100,663,196 and 100,662,884-100,663,084 at the end of the
196,608-sector boot partition. The 85,688,320-byte v5 image is a prefix of
that partition and does not reach those bytes, so those two digests come from
the full-partition backup, not from the image file. Cases 6, 7 and 10 are
inside the image prefix and therefore use the v5 image values. Every derived
value matched the pinned table above; the new-case digest prefixes were also
found in the packaged kernel.

## 3. Candidate identity

- prelinked kernel `b2837a5b…` (51,294,208 bytes; plan `a834fb1d…`)
- kernel-build archive `284323e6…` with `Gta4lEmmcBlock.c` `4cfbc466…`
  compiled to `Gta4lEmmcBlock.o` `dddd7761…` and `Gta4lEmmcBlock.cpp`
  `2721a6a7…` compiled to `Gta4lEmmcBlockCpp.o` `c1d9335c…`; the bindings
  object stays `3e53b66d…` because the audited interface is unchanged
- candidate storage-ABI audit `out/gta4l/storage-abi-b2837a5b`:
  `gdb-stop.json` `7ca3d434…`, `interface-audit.json` `ab4113dd…`,
  `provenance.json` `943f8bdf…`, `run.json` `a3a1010b…`; 190 method slots,
  181 kernel bindings (167 inherited + 14 pure virtual), 26 concrete
  overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
  metaclass `0xfffffff006add1d0`, service metaclass
  `0xfffffff007afb5b8`, live original sizes 0x90/0x88, native 4K,
  `disk_read_tested=false`, `transport_implemented=false`,
  `hardware_tested=false`, candidate matches the audit
- the only audited method-target change from the N10.2 audit is the
  `___cxa_pure_virtual` slot (`0xfffffff007a0d00c` -> `0xfffffff007a11118`);
  the vtable and metaclass addresses are unchanged and the service metaclass
  moved with the larger kernel image
- requalified archive `f3789061…` (865,448 bytes) with byte-identical members
- packaged kernel `00615136…` (51,294,208 bytes; extracted from the payload
  FAT16 member `::/EFI/UniXNU/Payloads/kernel.macho`)
- payload `924c7bbd…` (83,886,080 bytes; manifest `ddbb77f1…`; gate
  `fbfdf923…`; 23/23 contract; diagnostic `emmc-block-3-deblock` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-deblock=1`; hold root `327ac826…`, fixture
  `00363681…` with `diagnostic=fs-timer-hold` and
  `hold_after_success=true`, owner trust cache `30a72f78…`)
- BOOT image `bacc933f…` (85,688,320 bytes; manifest `95670b8e…`; gate
  `9c3d0558…`; 18/18 component checks, only the ramdisk differs from the
  hardware-tested `ce4d76e8…` v5 baseline; baseline kernel section
  `a4b054e0…`, firmware `1e8a3132…` and boot DTB `dc4c719d…` are unchanged;
  header ID `f369571b…`)
- source hashes: `Gta4lEmmcBlock.c` `4cfbc466…`, `Gta4lEmmcBlock.cpp`
  `2721a6a7…`, `Gta4lEmmcBlock.h` `0aae4cc4…`; adapter harness
  `fb5a4e23…`, adapter support `d95eab19…`, test support `fe811046…`;
  payload builder `6957a169…`, payload checker `d812835b…`, packager
  `1086fbad…`

The kernel hash changed from the N10.2 `e7a1982b…` as expected for the new
deblock code path. The kernel-build report records the two source/object
pairs and the kernel-build archive `284323e6…`.

## 4. Build chain and records

The host environment prepends a private `python3.11` shim because the host
`python3` is 3.9 and lacks `tomllib`; `UNIXNU_TOOLCHAIN` points at the
prebuilt toolchain. The device tree was built from HEAD `190d12e` (the N10.4
implementation) with a clean working tree; `unixnu tree` refreshed the shared
tree before the build so the compile read the device package.

1. Kernel build with the N10.2 candidate-matched audit
   `out/gta4l/storage-abi-e7a1982b` for the archive step:
   `kernel-build.log` -> prelinked kernel `b2837a5b…`;
   `build-report-kernel-build.json` records the two source/object pairs and
   the kernel-build archive `284323e6…`.
2. Candidate-specific storage-ABI audit against `b2837a5b…`:
   `audit-b2837a5b.log` and `out/gta4l/storage-abi-b2837a5b`;
   `test-gta4l-storage-abi.py --artifacts out/gta4l/storage-abi-b2837a5b`
   passes.
3. Archive requalification against the new audit from the shared tree:
   `archive-b2837a5b.log`, `build-report.json` (archive `f3789061…`,
   `candidate_matches_audit=true`, `qualified=true`, no missing
   qualification), `archive-requalified.a`; the three members hash to the
   kernel-build report's object hashes.
4. Payload build: `payload-build.log` -> `924c7bbd…`; the contract check is
   recorded in `payload.img.manifest.json` (23/23 checks including exactly
   one deblock token, the partobs/partcmp/partread exclusions and the
   hold-fixture requirement).
5. BOOT packaging: `package.log` -> `bacc933f…`; `boot-image-gate.json`
   passes 18/18 and `only_ramdisk_differs=true`.
6. Host suites: `block-suite.log` (48 core scenarios/38 mutation controls;
   143 adapter scenarios with 48 adapter/support and eight policy mutation
   controls, including the sixteen deblock mutations), `emmc-suite.log`
   (187/115), `emmc-probe-suite.log` (196/100, 13 width-bound
   materializations and one oversized-report control), `payload-suite.log`
   (34 checks), `package-suite.log` (13 checks), `boot-image-suite.log`,
   `storage-abi-test.log`, `emmc-evidence-counts.log`,
   `sdhci-evidence-counts.log` (33/10/41), `recipe.log`, `board-check.log`,
   `toml-check.log` and `git-diff-check.log`; every suite exited 0.

The packaged kernel carries three `GTA4L-EMMC P5 REPORT` strings, one
`PARTMAP`, one `READDETAIL`, one `PARTOBS`, one `PARTREAD`, one `READTEST`,
one `READFAIL`, two `DEBLOCK`, one `DEBLOCKREAD`, one legacy `PART:`, seven
`gta4l-emmc-partread`, seven `gta4l-emmc-partcmp`, seven
`gta4l-emmc-partobs` and seven `gta4l-emmc-deblock` strings and no literal
`complete=`. The binary contains every opt-in path; the boot argument selects
`deblock`.

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

The host deblock coverage pins the eleven cases and the expected full-pass
counters, and rejects mutations that would weaken the contract: a refused
selector value accepted, a skipped hash comparison, a short-transfer
acceptance, a read-error acceptance, a budget skip, a cancellation skip, a
premature `complete=1`, a one-segment advertisement, a skipped bounce buffer,
a wrong range, skipped whole-media or boot-partition close, a wrong head
discard, a shifted copy-out and a tail written into the request buffer. The
adapter expectations pin `cases=11 passed=11 failed=0 reason=0 complete=1
reads=11 bytes=54364 span=56832 blocks=111 fails=0 cancel=0 budget=0 media=0
part=0 seg=3 cap=61071360`; the last-case line is `DEBLOCKREAD index=10
target=1 start=4096 length=1024 span=1024 actual=1024 status=0 digest=…`.

The report line forms are bounded:

```
GTA4L-EMMC P5 DEBLOCK: cases=11 passed=11 failed=0 reason=0 complete=1 reads=11 bytes=54364 span=56832 blocks=111 fails=0 cancel=0 budget=0 media=0 part=0 seg=3 cap=61071360
GTA4L-EMMC P5 DEBLOCKREAD: index=10 target=1 start=4096 length=1024 span=1024 actual=1024 status=0 digest=<32 hex>
```

`media` and `part` are failure flags: `media=1` means a whole-media
selection/profile failure and `part=1` means a boot-partition lookup failure.
A successful run therefore reports `media=0 part=0`. The reason codes are
unchanged: 1 media, 2 partition, 3 open, 4 descriptor, 5 read, 6 short,
7 hash, 8 budget, 9 cancel and 10 argument; 0 is no failure. The first
failing case latches the reason and `complete` stays 0. The deblock layer
counts one read per case; the driver may split the 64-block case into
multiple transport requests because of its eight-block request bound, so the
whole-boot `DRV`/`XFER` aggregates will be larger than `reads=11`.

## 6. Limits and gates

- This record covers the host build only. No flash, boot or device access was
  performed while producing it. N10.5 has no hardware result; a BOOT-only
  flash and cold boot would need fresh explicit owner approval and a finite
  boot budget.
- The pinned digests qualify the retained historical evidence, not the
  current device content. A future hardware `complete=1` would be a same-boot
  content claim for those eleven ranges only; it would not qualify arbitrary
  I/O, mount, writes, wider/faster modes, DMA/CQE/ICE or initialization
  reliability.
- The host seam models the head/tail chunks, the break-size computation, the
  three-segment descriptor and the deblocker's excess discard; it does not
  run the real `IOBlockStorageDriver`/`IODeblocker` code. The hardware run is
  the only qualification of those layers.
- The boot-tail cases 8-9 address the end of the full boot partition, not the
  end of the shorter BOOT image file. Cases 6, 7 and 10 are inside the
  gate-pinned v5 kernel section and are invariant across candidates.
- The deblock scratch is one page-aligned 33,792-byte `IOMallocAligned`
  buffer freed with `IOFreeAligned`; the object stays below the 16 KiB
  `static_assert`, and the worst case stays below the 30,000 ms watchdog.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 verdicts, the N9.10
  comparator result, the N10.1 hardware/repeatability results and the N10.2
  hardware result are not upgraded. Initialization reliability remains
  unresolved.
- No commit or push is part of N10.5.

## 7. Evidence

The host build artifacts, logs, manifests and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n10.5-deblock-candidate-evidence/` directory. The
repository records the hashes and the bounded interpretation only. The
retained out-of-band digests live in
`out/gta4l/ram-root-emmc-probe-stage2-evidence/recovery-readback/` and
`out/gta4l/boot/`; the storage-ABI audit artifacts are in
`out/gta4l/storage-abi-b2837a5b/`.
