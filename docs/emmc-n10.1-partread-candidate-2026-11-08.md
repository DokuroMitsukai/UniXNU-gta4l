# gta4l N10.1 read-only IOMedia content qualification candidate production (2026-11-08)

Status: host-only candidate build of the N10.1 opt-in read-only `IOMedia`
content qualification (`gta4l-emmc-block=3 gta4l-emmc-partread=1`) through the
archive/prelink/audit/payload/BOOT flow. No flash, boot, device access, commit
or push was performed while producing this record. Build success is not a
hardware result. The separately approved BOOT-only flash and one
owner-reported cold boot were executed later and are recorded in
`docs/emmc-n10.1-partread-hardware-result-2026-11-08.md`; this record remains
the host-only production snapshot.

## 1. Scope and outcome

N10.1 adds an orthogonal read-only qualification mode selected by
`gta4l-emmc-partread=1`. The selector accepts the token only with exactly one
`gta4l-emmc-block=1` or `=3` token, value exactly `1`, no duplicate, and no
`gta4l-emmc-partobs=1` or `gta4l-emmc-partcmp=1` in the same boot. An orphan,
duplicate, wrong value or combination refusal keeps the legacy report and is
not an inert fallback.

The mode waits for exactly one whole-media `IOMedia`, validates the read-only
whole-media profile, opens the whole media as a reader, resolves the boot
partition media by its pinned base/size, and performs six fixed-range reads:

| index | target | range | SHA-256 (32 bytes) |
| --- | --- | --- | --- |
| 0 | whole | LBA 0, 34 blocks (PMBR, primary header, primary array) | `0c7e9c03…718bef` |
| 1 | whole | last 34 blocks (backup header and backup array) | `41912e59…0c2020` |
| 2 | boot | 64 blocks at `BOOT_SIZE - 32768` (maximum request) | `ef313f71…5c2150` |
| 3 | boot | 3 blocks at `BOOT_SIZE - 1536` (odd tail) | `02acbb3a…fd8bba` |
| 4 | boot | 2 blocks at `BOOT_SIZE - 1024` (even tail) | `d737414c…608ab0` |
| 5 | boot | 1 block at `BOOT_SIZE - 512` (singleton) | `400039fd…78a773e` |

Each read uses a page-aligned `IOMallocAligned` bounce buffer sized to the
64-block maximum, copies the bytes only after a full successful transfer, and
compares the first 16 digest bytes in the bounded report. The portable
SHA-256 implementation is shared with the retained evidence tooling. The first
failing case latches the reason; the counters keep every case that ran.
`complete=1` requires all six cases to pass, no cancellation, no budget stop
and no media/partition/open failure.

Host acceptance passes. The six digests were re-verified against the retained
out-of-band evidence before this build:

- the whole-media cases equal the retained recovery readbacks
  `lbas-0-33.bin` and `lbas-61071326-61071359.bin` (which are also the first
  17,408 bytes of the retained `primary-gpt.bin` and `backup-gpt.bin`);
- the boot-tail cases equal the corresponding ranges of the retained full
  BOOT-partition backup `boot-backup-20261027-pmgr.img` (100,663,296 bytes);
- the retained GPT parses as 128 entries of 84 bytes with backup LBA
  61,071,359 and usable bounds 34/61,071,326; the `boot` entry starts at LBA
  2,255,360 with 196,608 sectors, matching the pinned boot geometry.

## 2. Candidate identity

- prelinked kernel `12e00d9f…` (51,277,824 bytes; plan `83a8b81b…`)
- kernel-build archive `b137c2e4…` (members `Gta4lEmmcBlock.o` `2402c895…`,
  `Gta4lEmmcBlockCpp.o` `5f72925e…`, `original-bindings.o` `3e53b66d…`); the
  kernel-step container is recorded in `build-report-kernel-build.json`, and
  the requalified container `5a59d055…` (769,368 bytes) carries byte-identical
  members
- candidate storage-ABI audit `out/gta4l/storage-abi-12e00d9f`:
  `gdb-stop.json` `275d693e…`, `interface-audit.json` `48d8d4a7…`,
  `provenance.json` `99c15186…`, `run.json` `b9888e50…`; 190 method slots,
  181 kernel bindings (167 inherited + 14 pure virtual), 26 concrete
  overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`,
  metaclass `0xfffffff006add1d0`, service metaclass `0xfffffff007af75b8`,
  live original sizes 0x90/0x88, native 4K, candidate matches the audit,
  `runtime_qualified=false`
- packaged kernel `05fde642…` (extracted from the payload FAT16 member
  `::/EFI/UniXNU/Payloads/kernel.macho`)
- payload `e2dc899d…` (83,886,080 bytes; manifest `08ff16bf…`; 18/18
  contract; diagnostic `emmc-block-3-partread` mapping to
  `gta4l-emmc-block=3 gta4l-emmc-partread=1`; hold root `327ac826…`, hold
  `fixture.json` `00363681…` with `diagnostic=fs-timer-hold` and
  `hold_after_success=true`, trust cache `30a72f78…`)
- BOOT image `0cee22c9…` (85,688,320 bytes; manifest `31ce0993…`; gate 18/18,
  only the ramdisk differs from the hardware-tested `ce4d76e8…` v5 baseline;
  baseline kernel section `a4b054e0…`, firmware `1e8a3132…` and boot DTB
  `dc4c719d…` are unchanged; header ID `b31f4c51…`)
- source hashes: `Gta4lEmmcBlock.c` `9dad3d79…`, `Gta4lEmmcBlock.cpp`
  `6a11aa76…`, `Gta4lEmmcBlock.h` `d05b982e…`; payload builder
  `1ef3d1d6…`, payload checker `16e17b0a…`, packager `652cb746…`

The kernel-build archive records `Gta4lEmmcBlock.c` `9dad3d79…` compiled to
`Gta4lEmmcBlock.o` `2402c895…` and `Gta4lEmmcBlock.cpp` `6a11aa76…` compiled
to `Gta4lEmmcBlockCpp.o` `5f72925e…`; the bindings object stays `3e53b66d…`
because the audited interface is unchanged. The candidate kernel hash changed
from the N9.10 `fcc61714…` as expected for the new partread code.

## 3. Build chain and records

The host environment prepends a private `python3.11` shim because the host
`python3` is 3.9 and lacks `tomllib`; `UNIXNU_TOOLCHAIN` points at the
prebuilt toolchain. The device tree was built from HEAD `31e719f` with the
uncommitted N10.1 working tree; `unixnu tree` refreshed the shared tree before
the build so the compile read the device package.

1. Kernel build with the N9.10 candidate-matched audit
   `out/gta4l/storage-abi-fcc61714` for the archive step:
   `kernel-build.log` -> prelinked kernel `12e00d9f…`;
   `build-report-kernel-build.json` records the two source/object pairs and
   the kernel-build archive `b137c2e4…`.
2. Candidate-specific storage-ABI audit against `12e00d9f…`:
   `audit-12e00d9f.log` and `out/gta4l/storage-abi-12e00d9f`;
   `test-gta4l-storage-abi.py --artifacts out/gta4l/storage-abi-12e00d9f`
   passes.
3. Archive requalification against the new audit from the shared tree:
   `archive-12e00d9f.log`, `build-report.json` (archive `5a59d055…`,
   `candidate_matches_audit=true`), `archive-requalified.a`; the three members
   hash to the kernel-build report's object hashes.
4. Payload build: `payload-build.log` -> `e2dc899d…`; the contract check is
   recorded in `payload.img.manifest.json` (18/18 checks including exactly one
   partread token, partobs+partread and partcmp+partread exclusion, and the
   hold-fixture requirement).
5. BOOT packaging: `package.log` -> `0cee22c9…`; `boot-image-gate.json`
   passes 18/18.
6. Host suites: `block-suite.log` (48 core scenarios/38 mutation controls;
   114 adapter scenarios with 33 adapter and eight policy mutation controls,
   including the partread mutations), `payload-suite.log`, `package-suite.log`,
   `boot-image-suite.log`, `storage-abi-test.log`, `evidence-counts.log`,
   `recipe.log`, `board-check.log` and `kernel-token-scan.txt`.

The packaged kernel carries one `GTA4L-EMMC P5 PARTREAD` string, one
`GTA4L-EMMC P5 READTEST` string, one `GTA4L-EMMC P5 READFAIL` string, one
`GTA4L-EMMC P5 PARTMAP` string, three `GTA4L-EMMC P5 REPORT` strings, one
`GTA4L-EMMC P5 READDETAIL` string, one `GTA4L-EMMC P5 PARTOBS` string, one
legacy `PART:` string, six `gta4l-emmc-partread`, six `gta4l-emmc-partcmp` and
six `gta4l-emmc-partobs` strings and no literal `complete=`. The binary
contains every opt-in path; the boot argument selects `partread`.

## 4. Qualification results

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

The host partread coverage pins the six cases and rejects mutations that would
weaken the contract: a skipped hash comparison, a short-transfer acceptance, a
read-error acceptance, a budget skip, a cancellation skip, a premature
`complete=1`, a wrong case pair and a wrong pinned digest. The adapter suite
also models the real whole-media profile (absent `Base`) and the boot-partition
lookup.

The report line forms are bounded:

```
GTA4L-EMMC P5 PARTREAD: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6 blocks=138 bytes=70656 fails=0 cancel=0 budget=0 media=0 part=0 cap=61071360
GTA4L-EMMC P5 READTEST: index=5 target=1 blocks=1 off=100662784 ok=1 status=0 actual=512 digest=<32 hex>
GTA4L-EMMC P5 READFAIL: index=… reason=… status=… actual=… digest=<32 hex>
```

`media` and `part` are failure flags: `media=1` means a whole-media
selection/profile failure and `part=1` means a boot-partition lookup failure.
A successful run therefore reports `media=0 part=0`; an earlier draft of this
illustrative line incorrectly showed `1` for both.

The reason codes are 1 media, 2 partition, 3 open, 4 descriptor, 5 read,
6 short, 7 hash, 8 budget, 9 cancel and 10 argument; 0 is no failure. The
first failing case latches the reason and `complete` stays 0.

## 5. Limits and gates

- This production record covers the host build only. No flash, boot or device
  access was performed while producing it; the separately approved BOOT-only
  flash and one owner-reported cold boot were executed later and are recorded
  in `docs/emmc-n10.1-partread-hardware-result-2026-11-08.md`.
- The pinned digests qualify the retained historical evidence, not the current
  device content. The later hardware `complete=1` is a same-boot content claim
  for those ranges only; it would not qualify arbitrary I/O, mount, writes,
  wider/faster modes, DMA/CQE/ICE or initialization reliability.
- The host seam calls `doAsyncReadWrite` directly and does not qualify the
  real context, deblock or breakup layers.
- The partread scratch is one page-aligned 32 KiB `IOMallocAligned` buffer
  freed with `IOFreeAligned`; the object stays below the 16 KiB
  `static_assert`, and `PARTREAD_WORST_MS` stays below the 30,000 ms watchdog.
- N8.5's historical `complete=0`, the N9.6/N9.7/N9.8 verdicts and the N9.10
  comparator result are not upgraded. Initialization reliability remains
  unresolved.
- No commit or push is part of N10.1.

## 6. Evidence

The host build artifacts, logs, manifests and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n10.1-partread-candidate-evidence/` directory. The
repository records the hashes and the bounded interpretation only. The
retained out-of-band digests live in
`out/gta4l/ram-root-emmc-probe-stage2-evidence/recovery-readback/`,
`out/gta4l/ram-root-fs-timer-evidence/` and `out/gta4l/boot/`.
