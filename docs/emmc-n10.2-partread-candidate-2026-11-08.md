# gta4l N10.2 read-only IOMedia content qualification expansion (2026-11-08)

Status: host-only candidate build of the N10.2 expansion of the N10.1 opt-in
read-only `IOMedia` content qualification (`gta4l-emmc-block=3
gta4l-emmc-partread=1`) from six to fourteen pinned cases through the
archive/prelink/audit/payload/BOOT flow. No flash, boot, device access, commit
or push was performed while producing this record. Build success is not a
hardware result. The N10.1 hardware result and repeatability series remain
unchanged; N10.2 has not run on a device.

## 1. Scope and outcome

The selector, media/partition discovery, page-aligned bounce buffer, digest
comparison, reason codes and `complete=1` guard are unchanged from N10.1. Only
the pinned case table and the two boot-image kernel offset constants grew. The
mode still waits for exactly one whole-media `IOMedia`, validates the
read-only whole-media profile, opens the whole media as a reader, resolves the
boot partition by its pinned base/size, and performs one read per case through
the page-aligned 32 KiB `IOMallocAligned` bounce buffer. It copies bytes only
after a full successful transfer and compares the first 16 digest bytes.

The 14 fixed cases are:

| index | target | range | blocks | SHA-256 (first 8…last 6) |
| --- | --- | --- | --- | --- |
| 0 | whole | LBA 0, 34 blocks (PMBR, primary header, primary array) | 34 | `0c7e9c03…718bef` |
| 1 | whole | last 34 blocks (backup header and backup array) | 34 | `41912e59…0c2020` |
| 2 | whole | LBA 0, 1 block (PMBR only) | 1 | `a483d593…b9b699` |
| 3 | whole | LBA 1, 1 block (primary GPT header only) | 1 | `efd3556b…1823ac` |
| 4 | whole | LBA 2, 32 blocks (full primary array) | 32 | `ecde9f6f…e272aa` |
| 5 | whole | LBA 34, 1 block (first usable LBA) | 1 | `2ddc82b7…2c0eeb` |
| 6 | whole | 32 blocks starting at the last 33-block offset (full backup array) | 32 | `ecde9f6f…e272aa` |
| 7 | whole | last block (backup GPT header) | 1 | `5ea6f4bf…089b48` |
| 8 | boot | 64 blocks at `BOOT_SIZE - 32768` (maximum request) | 64 | `ef313f71…5c2150` |
| 9 | boot | 3 blocks at `BOOT_SIZE - 1536` (odd tail) | 3 | `02acbb3a…fd8bba` |
| 10 | boot | 2 blocks at `BOOT_SIZE - 1024` (even tail) | 2 | `d737414c…608ab0` |
| 11 | boot | 1 block at `BOOT_SIZE - 512` (singleton) | 1 | `400039fd…78a773e` |
| 12 | boot | 64 blocks at offset 4096 (kernel section page 1) | 64 | `979df1ff…0e2093` |
| 13 | boot | 64 blocks at offset 1048576 (kernel section 1 MiB) | 64 | `050f2b0d…2a1202` |

Cases 0-1 and 8-11 are the N10.1 cases. Cases 2-7 split the whole-media
coverage so a primary-copy mismatch localizes to the PMBR, the primary header,
the primary array, the first usable LBA, the backup array or the backup
header. Case 6 and case 4 pin the same digest (`ecde9f6f…e272aa`), so the two
array copies are compared as content; case 7 is the backup header. Cases 12-13
cover the boot-image kernel section at page 1 and 1 MiB. Both lie inside the
1,798,052-byte kernel section that the boot-image gate keeps byte-identical to
the hardware-tested `ce4d76e8…` v5 baseline, so their digests are invariant
across candidates.

The run is `reads=14 blocks=334 bytes=171008` (34+34+1+1+32+1+32+1+64+3+2+1+
64+64 = 334 blocks and 334 x 512 = 171,008 bytes). The last case is index 13,
target boot, 64 blocks at offset 1,048,576, 32,768 actual bytes. The
`complete=1` guard is unchanged: it requires every case to pass with no
cancellation, budget stop, media/partition/open failure, short transfer or
hash mismatch. The per-read 4,000 ms budget is unchanged and covers the whole
case table; a slow device stops with `budget=1` and `complete=0` rather than
extending the 30,000 ms watchdog.

## 2. Digest derivation and the boot-tail boundary

The digests were re-verified before the build against the retained out-of-band
evidence:

- cases 0-7 equal the retained recovery readbacks `lbas-0-33.bin`,
  `lba-34.bin` and `lbas-61071326-61071359.bin` (the 34-block prefix and
  suffix files are also the first 17,408 bytes of the retained
  `primary-gpt.bin` and `backup-gpt.bin`);
- cases 8-11 equal the corresponding ranges of the retained full BOOT
  partition backup `boot-backup-20261027-pmgr.img` (100,663,296 bytes);
- cases 12-13 equal the corresponding ranges of the v5 BOOT image
  `boot-ram-root-sdhci-v5.img` (85,688,320 bytes), whose kernel section hash
  `a4b054e0…` is also the boot-image gate baseline.

The boot-tail boundary matters: cases 8-11 address the end of the 196,608-
sector (100,663,296-byte) boot partition. The 85,688,320-byte v5 BOOT image is
a prefix of that partition and does not reach the final 14,974,976 bytes, so
those four digests come from the full-partition backup, not from the image
file. The image's own last 32 KiB hashes to `c3502047…` and is not a case
digest. The N10.1 hardware run matched the four tail cases on the device,
which is consistent with the packaging writing only the image prefix and
leaving the partition tail intact. Cases 12-13 are inside the image prefix and
therefore use the v5 image values; they were not part of the N10.1 hardware
run.

All 14 pinned values were re-derived successfully before the build; the
new-case digest prefixes were found in the packaged kernel, and case 6 shares
case 4's array digest.

## 3. Candidate identity

- prelinked kernel `e7a1982b…` (51,277,824 bytes; plan `1a84cff2…`)
- kernel-build archive `cb3d6df0…` with `Gta4lEmmcBlock.c` `62d1d706…`
  compiled to `Gta4lEmmcBlock.o` `7abc97d6…` and `Gta4lEmmcBlock.cpp`
  `6a11aa76…` compiled to `Gta4lEmmcBlockCpp.o` `0c5c1b96…`; the bindings
  object stays `3e53b66d…` because the audited interface is unchanged
- candidate storage-ABI audit `out/gta4l/storage-abi-e7a1982b`:
  `gdb-stop.json` `f2bdc155…`, `interface-audit.json` `88ebe8a3…`,
  `provenance.json` `c3a98a7e…`, `run.json` `a26f1d3a…`; 190 method slots,
  181 kernel bindings (167 inherited + 14 pure virtual), 26 concrete
  overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
  metaclass `0xfffffff006add1d0`, service metaclass `0xfffffff007af75b8`,
  live original sizes 0x90/0x88, native 4K, candidate matches the audit,
  `runtime_qualified=false`
- requalified archive `2ea638ef…` (769,840 bytes) with byte-identical members
- packaged kernel `d987c350…` (51,277,824 bytes; extracted from the payload
  FAT16 member `::/EFI/UniXNU/Payloads/kernel.macho`)
- payload `462870cd…` (83,886,080 bytes; manifest `b6439f2a…`; 18/18
  contract; diagnostic `emmc-block-3-partread` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-partread=1`; hold root `327ac826…`, hold
  `fixture.json` `00363681…` with `diagnostic=fs-timer-hold` and
  `hold_after_success=true`, trust cache `30a72f78…`)
- BOOT image `ca1789f0…` (85,688,320 bytes; manifest `c3272820…`; gate 18/18,
  only the ramdisk differs from the hardware-tested `ce4d76e8…` v5 baseline;
  baseline kernel section `a4b054e0…`, firmware `1e8a3132…` and boot DTB
  `dc4c719d…` are unchanged; header ID `026fa7e0…`)
- source hashes: `Gta4lEmmcBlock.c` `62d1d706…`, `Gta4lEmmcBlock.cpp`
  `6a11aa76…`, `Gta4lEmmcBlock.h` `b3bb2c95…`; adapter harness
  `27e9e99d…`; payload builder `1ef3d1d6…`, payload checker `16e17b0a…`,
  packager `652cb746…`, boot-image checker `879ee0ed…`

The kernel-build archive records `Gta4lEmmcBlock.c` `62d1d706…` compiled to
`Gta4lEmmcBlock.o` `7abc97d6…` and `Gta4lEmmcBlock.cpp` `6a11aa76…` compiled
to `Gta4lEmmcBlockCpp.o` `0c5c1b96…`; the bindings object stays `3e53b66d…`
because the audited interface is unchanged. The candidate kernel hash changed
from the N10.1 `12e00d9f…` as expected for the expanded case table.

## 4. Build chain and records

The host environment prepends a private `python3.11` shim because the host
`python3` is 3.9 and lacks `tomllib`; `UNIXNU_TOOLCHAIN` points at the
prebuilt toolchain. The device tree was built from HEAD `acb3a96` with the
uncommitted N10.2 working tree; `unixnu tree` refreshed the shared tree before
the build so the compile read the device package.

1. Kernel build with the N10.1 candidate-matched audit
   `out/gta4l/storage-abi-12e00d9f` for the archive step:
   `kernel-build.log` -> prelinked kernel `e7a1982b…`;
   `build-report-kernel-build.json` records the two source/object pairs and
   the kernel-build archive `cb3d6df0…`.
2. Candidate-specific storage-ABI audit against `e7a1982b…`:
   `audit-e7a1982b.log` and `out/gta4l/storage-abi-e7a1982b`;
   `test-gta4l-storage-abi.py --artifacts out/gta4l/storage-abi-e7a1982b`
   passes.
3. Archive requalification against the new audit from the shared tree:
   `archive-e7a1982b.log`, `build-report.json` (archive `2ea638ef…`,
   `candidate_matches_audit=true`), `archive-requalified.a`; the three members
   hash to the kernel-build report's object hashes.
4. Payload build: `payload-build.log` -> `462870cd…`; the contract check is
   recorded in `payload.img.manifest.json` (18/18 checks including exactly one
   partread token, partobs+partread and partcmp+partread exclusion, and the
   hold-fixture requirement).
5. BOOT packaging: `package.log` -> `ca1789f0…`; `boot-image-gate.json`
   passes 18/18.
6. Host suites: `block-suite.log` (48 core scenarios/38 mutation controls;
   114 adapter scenarios with 33 adapter and eight policy mutation controls,
   including the expanded partread expectations), `emmc-suite.log` (187/115),
   `emmc-probe-suite.log` (196/100), `payload-suite.log`, `package-suite.log`,
   `boot-image-suite.log`, `storage-abi-test.log`, `emmc-evidence-counts.log`,
   `sdhci-evidence-counts.log`, `recipe.log`, `board-check.log`, `toml-check`
   and `git-diff-check.log`; every suite exited 0.

The packaged kernel carries one `GTA4L-EMMC P5 PARTREAD` string, one
`GTA4L-EMMC P5 READTEST` string, one `GTA4L-EMMC P5 READFAIL` string, one
`GTA4L-EMMC P5 PARTMAP` string, three `GTA4L-EMMC P5 REPORT` strings, one
`GTA4L-EMMC P5 READDETAIL` string, one `GTA4L-EMMC P5 PARTOBS` string, one
legacy `PART:` string, six `gta4l-emmc-partread`, six `gta4l-emmc-partcmp` and
six `gta4l-emmc-partobs` strings and no literal `complete=`. The binary
contains every opt-in path; the boot argument selects `partread`.

## 5. Qualification results

The candidate-matched audit binds every one of the 190 audited storage slots
against the actual prelinked kernel, finds 26 concrete overrides and no
unresolved original import, and reports the live original block/service sizes
and native 4K translation. It explicitly records `disk_read_tested=false`,
`transport_implemented=false` and `hardware_tested=false`; no driver is linked
into the audit.

The requalified archive members are byte-identical to the objects recorded by
the kernel-build step, so the audit qualifies the bytes that produced the
candidate. The BOOT gate confirms the header page excluding the ID, the kernel
section, firmware and DTB match the hardware-tested `ce4d76e8…` baseline; only
the payload ramdisk differs.

The host partread coverage pins the 14 cases and rejects mutations that would
weaken the contract: a skipped hash comparison, a short-transfer acceptance, a
read-error acceptance, a budget skip, a cancellation skip, a premature
`complete=1`, a wrong case pair and a wrong pinned digest. The adapter
expectations now pin `cases=14 passed=14 reads=14 blocks=334 bytes=171008`;
the last-case line is `READTEST index=13 target=1 blocks=64 off=1048576
actual=32768`. The failure scenarios pin the expanded counts: hash mismatch
`passed=13 failed=1 reason=7 reads=14 blocks=334 bytes=171008`; read failure
`bytes=153600 fails=1`; short read `bytes=170496 fails=1`; boot partition
absent after the eight whole-media cases `passed=8 reads=8 blocks=136
bytes=69632 part=1`; descriptor failure `passed=0 failed=14 reason=4`; media
absent `reason=1 reads=0`; open refused `reason=3 reads=0`; budget stop after
the first case `reads=1 blocks=34 bytes=17408 budget=1`. The adapter suite
also models the real whole-media profile (absent `Base`) and the
boot-partition lookup.

The report line forms are bounded:

```
GTA4L-EMMC P5 PARTREAD: cases=14 passed=14 failed=0 reason=0 complete=1 reads=14 blocks=334 bytes=171008 fails=0 cancel=0 budget=0 media=0 part=0 cap=61071360
GTA4L-EMMC P5 READTEST: index=13 target=1 blocks=64 off=1048576 ok=1 status=0 actual=32768 digest=<32 hex>
GTA4L-EMMC P5 READFAIL: index=… reason=… status=… actual=… digest=<32 hex>
```

`media` and `part` are failure flags: `media=1` means a whole-media
selection/profile failure and `part=1` means a boot-partition lookup failure.
A successful run therefore reports `media=0 part=0`.

The reason codes are unchanged: 1 media, 2 partition, 3 open, 4 descriptor,
5 read, 6 short, 7 hash, 8 budget, 9 cancel and 10 argument; 0 is no failure.
The first failing case latches the reason and `complete` stays 0. The partread
layer counts one read per case; the driver may split the 32/34/64-block cases
into multiple transport requests because of its eight-block request bound, so
the whole-boot `DRV`/`XFER` aggregates will be larger than `reads=14`.

## 6. Limits and gates

- This record covers the host build only. No flash, boot or device access was
  performed while producing it. N10.2 has no hardware result; a BOOT-only
  flash and cold boot would need fresh explicit owner approval and a finite
  boot budget.
- The pinned digests qualify the retained historical evidence, not the current
  device content. A future hardware `complete=1` would be a same-boot content
  claim for those 14 ranges only; it would not qualify arbitrary I/O, mount,
  writes, wider/faster modes, DMA/CQE/ICE or initialization reliability.
- The boot-tail cases 8-11 address the end of the full boot partition, not the
  end of the shorter BOOT image file. The kernel-section cases 12-13 are
  inside the gate-pinned v5 kernel section and are invariant across
  candidates.
- The host seam calls `doAsyncReadWrite` directly and does not qualify the
  real context, deblock or breakup layers.
- The partread scratch is one page-aligned 32 KiB `IOMallocAligned` buffer
  freed with `IOFreeAligned`; the object stays below the 16 KiB
  `static_assert`, and `PARTREAD_WORST_MS` stays below the 30,000 ms watchdog.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 verdicts, the N9.10
  comparator result and the N10.1 hardware/repeatability results are not
  upgraded. Initialization reliability remains unresolved.
- No commit or push is part of N10.2.

## 7. Evidence

The host build artifacts, logs, manifests and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n10.2-partread-candidate-evidence/` directory. The
repository records the hashes and the bounded interpretation only. The
retained out-of-band digests live in
`out/gta4l/ram-root-emmc-probe-stage2-evidence/recovery-readback/`,
`out/gta4l/ram-root-fs-timer-evidence/` and `out/gta4l/boot/`.
