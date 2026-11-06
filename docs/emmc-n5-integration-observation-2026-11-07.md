# gta4l N4/N5 chunked-read integration observation: exact two- and three-sector IOMedia reads pass on one cold boot

**Status:** focused finding, 2026-11-07. Host-only closeout of one
owner-supplied hardware report after the N4 candidate was flashed BOOT-only;
this batch performed no device access, boot, flash, candidate build, driver or
transport change, commit or push. It follows
`docs/emmc-d13-readiness-2026-11-06.md`, the N3 host batches recorded in
`docs/emmc-driver.md` §5.3.48–§5.3.49 and the D12.6 observation; it does not
rewrite any earlier record or verdict.

## 1. Observation and provenance

The owner supplied one `GTA4L-EMMC P5 REPORT` after the N5 BOOT-only flash and
confirmed:

- the boot was a **cold** start (power off then on); no stronger physical
  rail-reset claim is made;
- the tablet was returned to **TWRP** immediately after the report.

No exact boot timestamp was recorded and none is invented. No original
photograph bytes were supplied in this turn, so no photo hash is claimed. The
report is ordinal 1 of the supplied observation and the one-boot budget for
this candidate is consumed by it.

The candidate is the existing N4 evidence set under the ignored
`out/gta4l/ram-root-emmc-n4-candidate-evidence/`:

- prelinked kernel `f37f840462b16a64c8a2d327b2c467dd9c5156fa4cc9c66c76f67ee0364538e0`,
  51,224,576 bytes; packaged kernel
  `2e6d91a0546d517eb400589935ef58a5c5dd509d943b01a0b03dcfc4be0fb180`,
  51,224,576 bytes.
- payload `4a400ca836470defbc86a461aa2b331074cf810d90f87285164e022ffa0dc987`,
  83,886,080 bytes, 9/9 contract, diagnostic `gta4l-emmc-block=3` with kernel
  arguments `rd=md0 sheng-pmgr=2`.
- BOOT image `54cdbfb6115a04597fb6de33954fba925a2bfb010a61f3c4c771f66c36d231da`,
  85,688,320 bytes, 18/18 gate, only the ramdisk differs from the pinned v5
  baseline `ce4d76e8…`.
- Storage-ABI qualification is host/QEMU PASS with `runtime_qualified=false`
  and is not a runtime ABI claim.
- Checkout anchors: device HEAD `1ee1f4f9` and shared HEAD `0a1cc714`, both
  uncommitted working trees; the N4 closeout binds the dirty deltas and
  source hashes.

The flash record is `out/gta4l/ram-root-emmc-n5-hardware-evidence/`:

- BOOT only to `/dev/block/mmcblk0p69`; 20,920 x 4,096-byte records,
  85,688,320 bytes, `conv=fsync` and `sync`; no host-issued reboot.
- Pre-flash full p69 `b19c59b8…`; post-flash full p69 `22bea393…`, matching
  the expected candidate-plus-unchanged-tail value; image-length prefix
  `54cdbfb6…`; tail `a28b4ea2…` unchanged.
- RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` are unchanged;
  battery 95%.
- Rollback `boot-before-n5.img.xz`
  `8b37f41cda282847a7229a39fedf23fc930212548df908c272225feaf2d83cc8`,
  decompressing to the pre-flash p69 hash.

The report does not embed the BOOT hash. Association with this candidate uses
the conversational sequence and the flash record; it is not an independent
cryptographic measurement of the running image.

## 2. Report decode

The header and counters are:

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 DRV: online=1 wp=1 reads=12 blocks=35 fails=0 max=8
GTA4L-EMMC P5 XFER: req=35 c17=9 c18=13 c18b=26 cfail=0 stops=13 sfail=0 last_stop=0
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 READ: lba=1 attempted=1 ok=1 bytes=512 sum=2228245566 base=2228245566 same=1
GTA4L-EMMC P5 SNAP: locked=1
```

The whole media is read-only and has the expected 61,071,360 x 512-byte
geometry. The media-path LBA 1 read matched the same-boot baseline. No driver,
chunk or stop failure was recorded. The final panic is the intentional report
stop:

```
GTA4L-EMMC P5 FAIL: valid=0 stage=0 code=0 block=0 error=0
  @Gta41EmmcBlock.cpp:1148
```

The rendered suffix uses the digit `1` in place of the source letter `l`; the
source file is `package/Gta4lEmmcBlock.cpp` and the panic is the report
terminal, not a hardware failure.

The two controlled requests are:

| Request | Delivered | Match | CMD18 chunks | CMD17 tail | CMD12 stops | Stop failures | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| LBA 2, 2 blocks | 1,024 bytes | 1,024 | 1 | 0 | 1 | 0 | `two_att=1 two_ok=1 two_reason=0` |
| LBA 2, 3 blocks | 1,536 bytes | 1,536 | 1 | 1 | 1 | 0 | `att=1 ok=1 reason=0` |

The reference was a raw transport read of LBA 2..4 before the media was
opened; it is not an IOMedia request. The reporter's counted requests are the
two controlled IOMedia reads plus the LBA 1 report read (3 requests,
6 blocks: 2 CMD18 chunks = 4 blocks and 2 CMD17 = 2 blocks, 2 stops). The
remaining 9 requests / 29 blocks / 11 CMD18 chunks / 7 CMD17 / 11 stops in the
aggregate are incidental partition-scheme and media requests. The raw
baseline and reference reads are direct transport reads outside those
counters. The per-request QUAL fields, not the aggregate, bind the intentional
reads; the qualification capture rejects incidental requests and marks a
second matching request ambiguous.

**Result:** the two intentional IOMedia requests pass for this one cold boot
at 1-bit SDR, 400 kHz. The two-sector request exercises one CMD18 chunk with a
mandatory clean CMD12; the three-sector request exercises one CMD18 chunk,
one clean CMD12 and one CMD17 odd tail. Every delivered byte matched the
same-boot reference. This is the first device result for the N3 mode-3 path.

## 3. Partition-discovery observation

The report also contains:

```
GTA4L-EMMC P5 PART: count=22 expected=84 waited_ms=0 error=0 reported=3
GTA4L-EMMC P5 PART1: base=8388608 size=4194304 content=other
GTA4L-EMMC P5 PART2: base=856162304 size=35127296 content=other
GTA4L-EMMC P5 PART3: base=891289600 size=4096 content=other
```

This is an observation, not a completeness verdict:

- `expected=84` is read from the same boot's LBA 1 sector by
  `gta4l_emmc_block_gpt_entries()`. It is the GPT header's
  NumberOfPartitionEntries field at offset `0x50`, with 128-byte entries; it
  is not a populated-partition count and is not a stale prior-boot value.
  The validated recovery GPT capture has 84 slots and 80 populated entries.
- `count` is the number of non-whole `IOMedia` objects whose provider chain
  reaches the driver. `reported=3` is the fixed sample cap, not the count.
- `countPartitions()` stops waiting as soon as the first count is nonzero and
  then takes one more immediate snapshot. `waited_ms=0` means the first count
  was already nonzero.
- `IOGUIDPartitionScheme::scan()` validates the full GPT array checksum and
  builds the media set in `probe()` before `start()` publishes the set one
  `IOMedia` at a time. A count concurrent with that publication loop can see
  a prefix. A failed scan publishes nothing, not a partial set.
- The presence of 22 non-whole media therefore proves the scan succeeded and
  publication had begun; it does not prove the final count is 22. The most
  consistent host-side explanation is a mid-publication snapshot, not a
  lost-partition or read failure. The report alone cannot prove that, so
  partition completeness is not claimed.
- The aggregate counters are consistent with the partition-scheme scan
  (protective MBR, GPT header and the 21-sector primary array) having
  completed with no failures, but incidental reads are not individually
  tagged.
- The three sampled bases are inside the media. They are not a stable
  GPT-ordinal mapping because the scheme publishes an `OSSet` in iteration
  order; they need not match the p5b report's first three entries.

The earlier p5b boot reported `count=80` with the same counting logic and the
same GPT. This boot reported `count=22`, so the snapshot can land much
earlier. This is a reporting/timing limitation, not a demonstrated storage
defect. A future instrument could require a stable count across several
bounded samples (or observe the scheme's partition-valid state), but no source
change is made in this batch; such a change needs its own host tests and
approval.

## 4. Limits and next decisions

- Acceptance is limited to the two actual IOMedia requests measured. It does
  not qualify arbitrary LBAs or request sizes, the 64-sector ceiling,
  repeatability, writable media, a mount or root switch, DMA/CQE/ICE, wider or
  faster modes, or initialization reliability.
- Initialization reliability remains unresolved. This report is one clean
  identification and does not measure a failure rate; CMD3 can still stop a
  later boot.
- Partition completeness is not claimed; `count=22` is an unsynchronized
  snapshot.
- The N5 one-boot budget is consumed. No further boot, flash or retry is
  authorized by this closeout.
- The next decision is the owner's: an N6 unchanged-candidate reliability
  observation, a partition-snapshot instrument candidate with its own host
  tests, or an N7 closeout with no device action. Each requires a separate
  approval and budget.

## 5. Evidence and validation

Private raw evidence is preserved under the ignored
`out/gta4l/ram-root-emmc-n5-hardware-evidence/observation-1/`:

- `owner-transcript-raw.txt` — exact owner text, 1,277 bytes, SHA-256
  `60700f362f37bb01f95b093f99e536943a79171ae2b735e8fbdeeebc083e6798`;
- `observation-record.json` — structured decode and owner-confirmed facts;
- `interpretation.md` — host-side decode, counter accounting and limits.

The flash evidence board (`flash-record.json`, `flash-commands.log`,
`preflash-hashes.txt`, `postflash-hashes.txt`, the candidate and rollback xz
files, `staging.txt`) is unchanged. `observation-sha256.txt` at the evidence
root covers the observation files and this document; the earlier
`supplemental-manifest.txt` is not rewritten.

Host validation was run from `src/UniXNU-gta4l/` with `LC_ALL=C` and
`/usr/bin/python3.11 -B`:

| Check | Result |
| --- | --- |
| `tomllib` parse of `porting.toml`, `board.toml`, `feeds.toml` | exited 0 |
| `../UniXNU/scripts/porting-record.py check --device .` | exited 0 |
| `scripts/check-gta4l.py` | exited 0 |
| `scripts/test-gta4l-emmc-evidence-counts.py` | exited 0 |
| `scripts/test-gta4l-ram-root-fixture-recipe.py` | exited 0 |
| `git diff --check` in the device repository | exited 0 |
| Privacy grep over the changed tracked files and this document | no unexplained hits |

The command transcript and exit codes are in `observation-validation.txt`; the
only privacy-pattern hits are device-tree node names such as `sdhci@4744000`,
section numbers such as `5.3.13.1` and the `-v serial=3` boot argument, with no
private identifier. This is a docs/evidence-only change; no fixture, test or
source changed. The earlier 24-script battery results remain historical and are
not re-claimed as a current full-suite acceptance.
