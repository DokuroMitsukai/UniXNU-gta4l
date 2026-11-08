# gta4l N9.10 exact-comparator observation (2026-11-08)

Status: BOOT-only flash of the N9.10 page-aligned exact-comparator candidate and
one owner-controlled first cold boot on 2026-11-08. The report selected the real
whole media and found the attached `IOGUIDPartitionScheme`
(`whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0`),
validated both GPT copies, matched all 80 qualifying `IOMedia` tuples against
the 80 used GPT entries, and emitted
`gpt=1 reason=0 media=80 match=1 complete=1`. Every comparator read succeeded
(`reads=25 blocks=45 fails=0`), including the first LBA 0/512-byte read that
failed in N9.8. The owner returned the tablet to TWRP; the one-boot budget is
consumed. No commit, push, retry, reflash or repair was performed. This is a
same-boot partition-completeness result, not an initialization-reliability
result.

## 1. Flash and boot scope

The owner approved the BOOT-only write in this session. The flash record is in
`out/gta4l/ram-root-emmc-n9.10-hardware-evidence/` (`flash-record.json`
`4cbe5b97…`, `evidence-sha256.txt` `89f5cc24…`; the manifest verifies).

- BOOT image `dc79fab2…` (85,688,320 bytes), payload `800d293d…`, prelinked
  kernel `fcc61714…`, packaged kernel `149b1e6c…`, requalified archive
  `0575c413…`, candidate build report `de1ca530…`.
- Candidate source `Gta4lEmmcBlock.cpp` `78144956…`, policy source
  `Gta4lEmmcBlock.c` `f84d1bd2…` and header `Gta4lEmmcBlock.h` `6c51b5b3…`.
  The build report binds those three hashes to the objects and archive.
- Only `/dev/block/mmcblk0p69` was written (20,920 x 4096 records, synced).
  The full readback `004b9d6d…` matched the expected candidate-prefix plus
  unchanged-tail value; prefix `dc79fab2…`, tail `a28b4ea2…`. RECOVERY
  `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` were unchanged, and
  the protected-partition backups were verified before the write.
- Rollback `boot-before-n9.10.img.xz` (`3c7e9e5f…` -> raw `cf13acfc…`) preserves
  the previous N9.8 post-flash image. It was not restored.

The owner supplied the wrapped report and confirmed TWRP return. The raw text,
a bounded interpretation and a separate non-self-hashing manifest are in
`out/gta4l/ram-root-emmc-n9.10-hardware-evidence/observation-1/` (raw
`2fc677de…`, interpretation `cacab157…`, record `138cbeb7…`; the manifest
`2056f59e…` binds the unchanged `evidence-sha256.txt` `89f5cc24…`). The report
is associated with the flashed image through the flash record, not an
independently measured running-image hash.

## 2. What the report shows

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512
bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320
content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 PARTMAP: gpt=1 reason=0 slots=84 used=80 pmbr=1 hdr=1
arr=1 bak=1 xcheck=1 media=80 match=1 missing=0 extra=0 dup=0 nested=0
prop=0 samples=9 changes=3 stable=1 stable_ms=250 deadline_ms=2000
timed_out=0 budget=0 reads=25 blocks=45 fails=0 cancel=0 complete=1
GTA4L-EMMC P5 READDETAIL: state=4 lba=61071347 blocks=1 bytes=512
status=0 actual=512 desc_len=512 desc_dir=1
```

- `rc=0 stop=0 mode=3` is header success, not comparator success.
- The MEDIA fields are populated only after exactly one qualifying whole medium
  is selected. `whole=1 base=0 size=31268536320
  content=GUID_partition_scheme writable=0` records a successful whole-media
  selection and GUID-scheme lookup, and `base=0` is the implied zero base
  rather than a published `Base` property.
- `gpt=1` is set only after the protective MBR, primary header and primary
  entry array validate, and the backup header and backup entry array validate
  and agree with the primary on the disk GUID, entry size and CRC, first/last
  usable LBA, slot and used counts and every used `(base, size)` entry
  (`package/Gta4lEmmcBlock.c:500-582`). The five flags `pmbr/hdr/arr/bak/xcheck`
  all read 1. `slots=84` is the GPT entry-slot count and `used=80` the
  populated count.
- `media=80` is the number of qualifying direct partition media and `match=1`
  is exact multiset equality between the 80 GPT `(base, size)` tuples and the
  80 media tuples (`package/Gta4lEmmcBlock.c:635-700`). `missing`, `extra`,
  `dup`, `nested` and `prop` are all zero.
- `samples=9 changes=3 stable=1 stable_ms=250 deadline_ms=2000 timed_out=0`
  means nine snapshots were taken and the media set changed three times before
  the final window; the final 250 ms window was unchanged and equal. The
  comparator stopped on stable-and-equal, not on the 2000 ms deadline.
  `budget=0 cancel=0 fails=0` mean no comparator read was refused by the
  4000 ms budget, no cancellation was observed and no comparator read failed.
- `reads=25 blocks=45` has an exact source-derived accounting. With
  `slots=84`, the entry array is `84 * 128 = 10,752` bytes = 21 blocks and is
  read in two-block chunks (11 reads: ten two-block reads and one one-block
  tail). The comparator issues one PMBR read (LBA 0, 1 block), one primary
  header read (LBA 1, 1 block), 11 primary-array reads (LBA 2-22, 21 blocks),
  one backup header read (LBA 61,071,359, 1 block) and 11 backup-array reads
  (LBA 61,071,327-61,071,347, 21 blocks). That is 25 reads and 45 blocks,
  matching the report.
- `READDETAIL: state=4 lba=61071347 blocks=1 bytes=512 status=0 actual=512
  desc_len=512 desc_dir=1`. State 4 is `READ_FULL`; the final comparator read
  is the one-block tail of the backup entry array at LBA 61,071,347 and it
  completed with `kIOReturnSuccess` and 512 actual bytes. `READDETAIL` records
  the most recent comparator read, not an aggregate; the aggregate is
  `reads=25 blocks=45`. The backup header read at LBA 61,071,359 necessarily
  succeeded earlier, because the backup array was read from the LBA derived
  from it.
- `complete=1` is exactly the source guard at
  `package/Gta4lEmmcBlock.cpp:2034-2039`: `gpt != 0`, `match != 0`,
  `stable != 0`, `timedOut == 0`, `budget == 0`, `cancel == 0`, `fails == 0`,
  `propSeen == 0`, `dup == 0`, `missing == 0`, `extra == 0` and
  `count == used`. It cannot coexist with a mismatch, timeout, budget,
  cancellation or read failure under that guard.
- Driver aggregate: online=1, wp=1, 34 reads/74 blocks, fails=0, maximum
  request 8 blocks. The transport is consistent (`74 = 12` CMD17 blocks `+ 62`
  CMD18 blocks; 31 CMD18 commands and 31 stops, no command or stop failures).
  These include the 25 comparator reads and incidental/scan requests; the
  aggregate cannot attribute a request to a path.
- Capacity remains 61,071,360 x 512 = 31,268,536,320 bytes; `wid=0 tim=0
  clk=400000` retains the 1-bit legacy timing policy. `SNAP locked=1`; FAIL is
  valid=0/all-zero. The supplied panic suffix is `@Gta41EmmcBlock.cpp:2078`
  (spelling preserved). In the candidate source `78144956…`, line 2078 is the
  intentional `panic("%s", reportBuffer)` in `panicWithReport()`.

## 3. Source-grounded reading

N9.8 measured the first comparator LBA 0/512-byte read returning
`status=3758097108` (`0xe00002d4`, `kIOReturnDMAError`) with `actual=0`. The
host trace selected `IOBlockStorageDriver::breakUpRequest()` with a zero break
size under the advertised one-segment read limit, and N9.9 moved every
comparator read of at most 1,024 bytes to one page-aligned
`IOMallocAligned(size, PAGE_SIZE)` bounce buffer with the caller copy only
after a full successful transfer. On N9.10 every comparator read, including
the LBA 0 PMBR read and the final backup-array tail, completed with `status=0`
and exact actual bytes.

This is direct hardware support that the page-aligned read path removed the
observed pre-adapter DMA rejection. It does not independently prove the N9.8
return site, the physical adjacency of the two page segments or the
`IOBreaker` allocation behavior; those remain host reconstructions. The host
seam still calls `doAsyncReadWrite` directly and does not qualify the real
context/deblock/breakup layers.

## 4. What this does not establish

- One owner-supplied cold boot. There is no independently measured
  running-image hash, boot timestamp, rail-reset state or post-boot device
  readback. The report is bound through the flash record.
- The result is same-boot exact GPT-to-`IOMedia` completeness under the
  comparator's range, property and 250 ms stability contract. It does not
  prove filesystem mount, root switch, arbitrary I/O, writes, wider or faster
  modes, DMA/CQE/ICE, or initialization reliability.
- N9.6/N9.7/N9.8 `complete=0` are historical facts and are not upgraded.
  Initialization reliability remains `[U]`.
- No partition was proven readable beyond the comparator's bounded GPT reads;
  no partition content, filesystem or data-integrity claim is made.

## 5. Next steps and gates

The N9.10 candidate has one same-boot `complete=1` observation. The next step
is a separately gated reliability proposal: repeated owner-controlled cold
boots of the same unchanged N9.10 image (no reflash) to observe whether the
same-boot completeness result repeats. Any such run requires fresh explicit
owner approval, a new predeclared boot budget, verified protected backups and a
rollback path; no further boot, flash, retry, rollback, reset/repair, commit or
push is authorized or performed by this record.

A repeat run would still be a bounded observation series, not a failure-rate
estimate, and it would not qualify mount, writes or wider/faster modes.

## 6. Evidence

Raw artifacts and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n9.10-hardware-evidence/` directory. The observation
subdirectory binds the original flash manifest by hash and does not hash
itself. The repository records only the sanitized summary and the bounded
interpretation.
