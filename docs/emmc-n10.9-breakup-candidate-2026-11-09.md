# gta4l N10.9 deterministic breakup qualification candidate (2026-11-09)

Status: host-only candidate build of the N10.8 opt-in `gta4l-emmc-breakup=1`
deterministic breakup qualification through the archive/prelink/audit/payload/
BOOT flow. No flash, boot, device access, commit or push was performed while
producing this record. Build success is not a hardware result. N10.8's host
implementation and the N10.1/N10.2/N10.6 hardware results remain unchanged;
N10.10 remains separately gated.

## 1. Scope and outcome

The selector, four-case table, page-aligned 65,536-byte bounce buffer, digest
comparison, reason codes, observer and `complete=1` guard are unchanged from
N10.8. The mode requires exactly one block token 1/3 with value 1, refuses
duplicates, orphans, non-1 values and every partobs/partcmp/partread/deblock
combination, advertises `kIOMaximumBlockCountReadKey = 8`,
`kIOMaximumByteCountReadKey = 4,096` and
`kIOMaximumSegmentCountReadKey = 3` only for itself, and performs one
`IOMedia::read()` per case through the real
`IOMedia`/`IOBlockStorageDriver`/`IOBreaker`/`IODeblocker` path.

The four fixed cases are boot-partition reads:

| index | target | start | length | span | blocks | stages | max | SHA-256 (first 8…last 6) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | boot | 4096 | 4096 | 4096 | 8 | 1 | 8 | `5878bcf8…f3b1b2` |
| 1 | boot | 4096 | 65536 | 65536 | 128 | 16 | 8 | `b4c3c643…80fa9c` |
| 2 | boot | 4096 | 65024 | 65024 | 127 | 16 | 8 | `5f970472…61a95c` |
| 3 | boot | 4196 | 65024 | 65536 | 128 | 16 | 8 | `bc0b9bb9…26db17` |

Case 0 is the aligned control at or below the 4,096-byte break size and takes
the direct `executeRequest()` short-circuit (one eight-block subrequest).
Case 1 splits into sixteen exact eight-block stages. Case 2 splits into
fifteen full eight-block stages and a seven-block final stage (16 stages,
127 blocks). Case 3 is unaligned: the deblocker requests the aligned
4,096-69,632 span with a 100-byte head scratch and a 412-byte tail scratch,
and the driver splits the 65,024 requested bytes into sixteen eight-block
subrequests. The run is `reads=4 bytes=199680 span=200192 blocks=391
subrequests=49 max=8` (sums of the requested lengths, aligned spans,
`span / 512`, driver-level stages and the largest stage).

The `complete=1` guard requires every case to pass with `reason=0`, clear
`cancel`/`budget`/`media`/`part`/`foreign` flags, `limit=8`, `seg=3` and
every status/actual/digest/decomposition check. The worst case is
20,000 + 6,000 + 2,000 + 1,000 = 29,000 ms, below the 30,000 ms watchdog.

## 2. Digest derivation

All four pinned digests were re-derived on the host before the build from the
gate-pinned v5 BOOT image `boot-ram-root-sdhci-v5.img` (85,688,320 bytes),
whose kernel section hash `a4b054e0…` is the boot-image baseline. The four
ranges (4,096/4,096, 4,096/65,536, 4,096/65,024 and 4,196/65,024) are inside
the 1,798,052-byte kernel section (offsets 4,096-69,220), so the digests are
invariant across candidates and do not depend on the ramdisk payload. Every
derived value matched the pinned table above; the new-case digest prefixes
were also found in the packaged kernel.

## 3. Candidate identity

- prelinked kernel `71828829…` (51,331,072 bytes; plan `2cee429c…`)
- kernel-build archive `8e636a01…` (988,936 bytes) with `Gta4lEmmcBlock.c`
  `e7cfcf23…` compiled to `Gta4lEmmcBlock.o` `75d4f999…` and
  `Gta4lEmmcBlock.cpp` `684a84db…` compiled to `Gta4lEmmcBlockCpp.o`
  `eea9a94d…`; the bindings object stays `3e53b66d…` because the audited
  interface is unchanged
- candidate storage-ABI audit `out/gta4l/storage-abi-71828829`:
  `gdb-stop.json` `20ac48a4…`, `interface-audit.json` `d4ac2abf…`,
  `provenance.json` `eefbc490…`, `run.json` `3bb349bf…`; 190 method slots,
  181 kernel bindings (167 inherited + 14 pure virtual), 26 concrete
  overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
  metaclass `0xfffffff006add1d0`, service metaclass
  `0xfffffff007b035b8`, live original sizes 0x90/0x88, native 4K,
  `disk_read_tested=false`, `transport_implemented=false`,
  `hardware_tested=false`, candidate matches the audit
- the only audited method-target change from the N10.5 audit is the
  `___cxa_pure_virtual` slot (`0xfffffff007a11118` -> `0xfffffff007a1a570`);
  the vtable and metaclass addresses are unchanged and the service metaclass
  moved with the larger kernel image
- requalified archive `8e636a01…` (988,936 bytes) with byte-identical members
- packaged kernel `a8a5a609…` (51,331,072 bytes; extracted from the payload
  FAT16 member `::/EFI/UniXNU/Payloads/kernel.macho`)
- payload `706cfd53…` (83,886,080 bytes; manifest `e9fdba1c…`; gate
  `84bd1ab7…`; 29/29 contract; diagnostic `emmc-block-3-breakup` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-breakup=1`; hold root `327ac826…`, fixture
  `00363681…` with `diagnostic=fs-timer-hold` and
  `hold_after_success=true`, owner trust cache `30a72f78…`)
- BOOT image `a8f4a54b…` (85,688,320 bytes; manifest `ee878222…`; gate
  `39571235…`; 18/18 component checks, only the ramdisk differs from the
  hardware-tested `ce4d76e8…` v5 baseline; baseline kernel section
  `a4b054e0…`, firmware `1e8a3132…` and boot DTB `dc4c719d…` are unchanged;
  header ID `733d6643…`)
- source hashes: `Gta4lEmmcBlock.c` `e7cfcf23…`, `Gta4lEmmcBlock.cpp`
  `684a84db…`, `Gta4lEmmcBlock.h` `3ab6d170…`; adapter harness
  `66b56ee9…`, adapter support `e39e1582…`, test support `29730fb6…`;
  payload builder `1d866672…`, payload checker `03cc78bf…`, packager
  `b58f7321…`

The kernel hash changed from the N10.5 `b2837a5b…` as expected for the new
breakup code path. The kernel-build report records the two source/object
pairs and the kernel-build archive `8e636a01…`; its archive step used the
N10.5 candidate-matched audit `out/gta4l/storage-abi-b2837a5b`, while the
requalification uses the N10.9 candidate audit
`out/gta4l/storage-abi-71828829`.

## 4. Build chain and records

The host environment prepends a private `python3.11` shim because the host
`python3` is 3.9 and lacks `tomllib`; `UNIXNU_TOOLCHAIN` points at the
prebuilt toolchain. The device tree was built from HEAD `c02a349` (the N10.8
implementation) with a clean working tree; `unixnu tree` refreshed the shared
tree before the build so the compile read the device package.

1. Kernel build with the N10.5 candidate-matched audit
   `out/gta4l/storage-abi-b2837a5b` for the archive step:
   `kernel-build.log` -> prelinked kernel `71828829…`;
   `build-report-kernel-build.json` records the two source/object pairs and
   the kernel-build archive `8e636a01…`.
2. Candidate-specific storage-ABI audit against `71828829…`:
   `audit-71828829.log` and `out/gta4l/storage-abi-71828829`;
   `test-gta4l-storage-abi.py --artifacts out/gta4l/storage-abi-71828829`
   passes.
3. Archive requalification against the new audit from the shared tree:
   `archive-71828829.log`, `build-report.json` (archive `8e636a01…`,
   `candidate_matches_audit=true`, `qualified=true`, no missing
   qualification), `archive-requalified.a`; the three members hash to the
   kernel-build report's object hashes.
4. Payload build: `payload-build.log` -> `706cfd53…`; the contract check is
   recorded in `payload.img.manifest.json` (29/29 checks including exactly
   one breakup token, the breakup exclusions and the hold-fixture
   requirement).
5. BOOT packaging: `package.log` -> `a8f4a54b…`; `boot-image-gate.json`
   passes 18/18 and `only_ramdisk_differs=true`.
6. Host suites: `block-suite.log` (48 core scenarios/38 mutation controls;
   178 adapter scenarios with 58 adapter, three support and eight policy
   mutation controls, including the thirteen breakup mutations),
   `emmc-suite.log` (187/115), `emmc-probe-suite.log` (196/100, 13
   width-bound materializations and one oversized-report control),
   `payload-suite.log` (37 checks), `package-suite.log` (14 checks),
   `boot-image-suite.log`, `storage-abi-test.log`,
   `emmc-evidence-counts.log`, `sdhci-evidence-counts.log` (33/10/41),
   `recipe.log`, `board-check.log`, `toml-check.log` and
   `git-diff-check.log`; every suite exited 0.

The packaged kernel carries three `GTA4L-EMMC P5 REPORT` strings, one
`PARTMAP`, one `READDETAIL`, one `PARTOBS`, one `PARTREAD`, one `READTEST`,
one `READFAIL`, two `DEBLOCK` (the line and the `DEBLOCKREAD` prefix), one
`DEBLOCKREAD`, two `BREAKUP` (the line and the `BREAKUPREAD` prefix), one
`BREAKUPREAD`, one legacy `PART:`, eight `gta4l-emmc-partread`, eight
`gta4l-emmc-partcmp`, eight `gta4l-emmc-partobs`, eight
`gta4l-emmc-deblock` and eight `gta4l-emmc-breakup` strings and no literal
`complete=`. The binary contains every opt-in path; the boot argument selects
`breakup`.

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

The host breakup coverage pins the four cases and the expected full-pass
counters, and rejects mutations that would weaken the contract: a refused
selector value accepted, a skipped hash comparison, a short-transfer
acceptance, a read-error acceptance, a budget skip, a cancellation skip, a
premature `complete=1`, a one-segment advertisement, a skipped bounce buffer,
a missing or extra split, a reordered subrequest, a wrong split count and
skipped whole-media or boot-partition close. The adapter expectations pin
`cases=4 passed=4 failed=0 reason=0 complete=1 reads=4 bytes=199680
span=200192 blocks=391 subrequests=49 max=8 foreign=0 fails=0 cancel=0
budget=0 media=0 part=0 limit=8 seg=3 cap=61071360`; the last-case line is
`BREAKUPREAD index=3 target=1 start=4196 length=65024 span=65536
actual=65024 status=0 subrequests=16 blocks=128 max=8
digest=bc0b9bb9c6dd11c3aea665d01dc5f108`.

The report line forms are bounded:

```
GTA4L-EMMC P5 BREAKUP: cases=4 passed=4 failed=0 reason=0 complete=1 reads=4 bytes=199680 span=200192 blocks=391 subrequests=49 max=8 foreign=0 fails=0 cancel=0 budget=0 media=0 part=0 limit=8 seg=3 cap=61071360
GTA4L-EMMC P5 BREAKUPREAD: index=3 target=1 start=4196 length=65024 span=65536 actual=65024 status=0 subrequests=16 blocks=128 max=8 digest=<32 hex>
```

`media`, `part` and `foreign` are failure flags: `media=1` means a whole-media
selection/profile failure, `part=1` means a boot-partition lookup failure and
`foreign=1` means a driver-level request that could not be attributed to the
active case. A successful run therefore reports `media=0 part=0 foreign=0`.
The reason codes are 1 media, 2 partition, 3 open, 4 descriptor, 5 read,
6 short, 7 hash, 8 budget, 9 cancel, 10 argument, 11 split, 12 order and
13 foreign; 0 is no failure. The first failing case latches the reason and
`complete` stays 0; later read/short/hash/descriptor/split/order failures
continue, foreign traffic is counted without stopping the run, while media,
partition, open, budget and cancellation stop it. The breakup layer counts
four reads but 49 driver-level subrequests, so the whole-boot `DRV`/`XFER`
aggregates will be larger than `reads=4`.

## 6. Limits and gates

- This record covers the host build only. No flash, boot or device access was
  performed while producing it. N10.9 has no hardware result; a BOOT-only
  flash and cold boot (N10.10) would need fresh explicit owner approval and a
  finite boot budget.
- The pinned digests qualify the retained v5 image content, not the current
  device content. A future hardware `complete=1` would be a same-boot
  decomposition and content claim for those four ranges only; it would not
  qualify arbitrary I/O, mount, writes, wider/faster modes, DMA/CQE/ICE or
  initialization reliability.
- The host seam models the limit-folded `IOBreaker` split and the
  `IODeblocker` three-chunk descriptor; it does not run the real
  `IOBlockStorageDriver`/`IOBreaker`/`IODeblocker` code. The hardware run is
  the only qualification of those layers.
- The four cases are inside the gate-pinned 1,798,052-byte v5 kernel section,
  so they are invariant across candidate payloads. The mode's advertised
  limits are 8 blocks, 4,096 bytes and three segments; the deblocker path
  exercises the same three-segment fold.
- The breakup scratch is one page-aligned 65,536-byte `IOMallocAligned`
  buffer freed with `IOFreeAligned`; the object stays below the 16 KiB
  `static_assert`, the report stays below 2,048 bytes, and the 29,000 ms
  worst case stays below the 30,000 ms watchdog.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 verdicts, the N9.10
  comparator result, the N9.11 owner-reported series, the N10.1
  hardware/repeatability results, the N10.2 hardware result and the N10.6
  deblock result are not upgraded. N10.6 remains the only breakup-layer
  hardware observation. Initialization reliability remains `[U]`.
- No commit or push is part of N10.9.

## 7. Evidence

The host build artifacts, logs, manifests, gates and hashes are in the
gitignored `out/gta4l/ram-root-emmc-n10.9-breakup-candidate-evidence/`
directory. The repository records the hashes and the bounded interpretation
only. The retained digest source is the gate-pinned
`out/gta4l/boot/boot-ram-root-sdhci-v5.img`; the storage-ABI audit artifacts
are in `out/gta4l/storage-abi-71828829/`.
