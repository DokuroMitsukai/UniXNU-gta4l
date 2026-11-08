# gta4l N9.1 read-only exact GPT-to-IOMedia partition-range comparison design (host-only, 2026-11-07)

**Status:** N9.1 host-only design. This batch performed no device access, boot,
flash, candidate build, driver change, QEMU run, commit or push. It follows the
N8.5 observation closeout (`docs/emmc-n8.5-hold-fixture-candidate-2026-11-07.md`)
and preserves N8's explicit `complete=0`; it does not upgrade that result. N9.2
implementation requires owner approval and Build mode; N9.3 archive/ABI/kernel/
payload/BOOT production and any N9.4 hardware run are separately gated. The
N8.5 boot budget is consumed, and initialization reliability remains `[U]`.

## 1. Why N8.5's count is not a comparison

N8.5 returned `PARTOBS samples=8 first=39 final=80 final_ms=350 changes=2
stable=1 stable_ms=250 deadline_ms=4000 timed_out=0 none=0 error=0 slots=84
reported=3 complete=0`. That is a bounded count observation, not a partition
measurement:

- `Gta4lEmmcBlock::countPartitions()` (`package/Gta4lEmmcBlock.cpp:1125`)
  enumerates every `IOMedia` service whose provider chain reaches the driver
  (`ancestryContains()`, `:406`), skips only media whose `Whole` property is
  true, and counts the rest. It does not require the media to be a direct child
  of the whole media's GUID scheme, so a nested container's children are also
  counted. It does not inspect `Base`/`Size` types, reject duplicates or
  overlaps, or verify that the range came from the GPT.
- Missing or wrongly typed `Base`, `Size` or `Content` properties are coerced
  to zero/empty in the current reporter (`:1160-1178`); the count still
  succeeds. A malformed publication can therefore look like a valid count.
- The `PART1..3` sample lines are the first three enumerated non-whole media at
  the final sample. Registry order is not a stable GPT-index identity, and
  three ranges are not an all-partition comparison.
- `final=80` agrees with the validated GPT's 80 populated entries among 84
  slots (`docs/emmc-reference.md`), but a count alone also accepts an
  equal-count wrong-range set, a duplicate that replaces a missing range, a
  nested set, or 80 malformed media with zeroed properties.

N9 therefore compares the exact multiset of normalized `(base_bytes,
size_bytes)` for validated GPT entries against only the direct partition media
of the whole media's `IOGUIDPartitionScheme`. Counts remain summary fields, not
the acceptance predicate.

## 2. Decision: opt-in `gta4l-emmc-partcmp=1`

N9.1 chooses a new diagnostic token, `gta4l-emmc-partcmp=1`, orthogonal to the
read/report selector and separate from N8's `gta4l-emmc-partobs=1`:

- `gta4l-emmc-block=1` (CMD17 report) and `gta4l-emmc-block=3` (chunked
  report) keep their existing semantics and report lines.
- `gta4l-emmc-block=2` remains refused.
- `gta4l-emmc-partcmp=1` requires exactly one block token with value 1 or 3.
  An orphan token, a duplicate token, any value other than 1, or a conflicting
  diagnostic token is refused before MMIO, matching the existing selector
  rules (`gta4l_emmc_block_partobs_select()` is the template).
- `gta4l-emmc-partcmp` and `gta4l-emmc-partobs` are mutually exclusive in one
  boot: the combination is refused. One boot yields one partition instrument,
  so the report budget and the 30,000 ms watchdog stay bounded and the N8
  count artifact is never silently replaced by a different instrument.
- When the token is absent, the legacy mode-1/mode-3 reporter, including the
  `PART` line and the N8 `PARTOBS` behavior, is byte-for-byte unchanged. The
  accepted N5, p5b and N8.5 artifacts are not re-interpreted.

Proposed payload personality names for N9.2 are `emmc-block-1-partcmp` and
`emmc-block-3-partcmp`, mapping to the block token plus
`gta4l-emmc-partcmp=1`. The payload builder, checker and packager must refuse
an orphan, duplicate or non-1 `partcmp` token, a personality/token mismatch and
a `partobs`+`partcmp` combination. Renaming these later requires an explicit
documentation update, not a silent change.

## 3. Reference use and invariant matrix

The Plan audit re-read the owner-provided Linux `block/partitions/efi.c` at the
pinned `limonabisi/linux-gta4l` commit `cf5f763f0ef8a5b4d8bb4163833cba1160c51894`
and staged a copy for this batch. This batch read that staged copy (`efi.c`,
SHA-256 `b348acd18893a9839da2f9f0ee9ddaf380be519fb9dbfc62803229ce4be96576`)
and the existing XNU IOStorageFamily snapshot
`out/feeds/IOStorageFamily-67f9003c/`. It did not re-fetch the Arch or pmOS
repositories; their role is limited to the out-of-scope mount rung described in
section 10, and no N9.1 rule depends on them.

Linux acceptance is a behavioral reference, not a parser to copy. The
diagnostic is stricter than both Linux and XNU where the stricter rule makes a
completeness claim defensible; those differences are listed in the last column.

| Invariant | Pinned reference | Current XNU behavior | N9.1 decision and test |
| --- | --- | --- | --- |
| Protective MBR has the MSDOS signature and one `0xEE` entry | `efi.c` `pmbr_part_valid()` :140, `is_pmbr_valid()` :172 | `IOGUIDPartitionScheme::scan()` (`IOGUIDPartitionScheme.cpp:329`) requires the `0x55AA` signature and exactly one `0xEE` entry, but does not require its start LBA to be 1 and accepts extra non-`0xEE` entries as long as one `0xEE` exists | Require signature `0x55AA`, exactly one `0xEE` entry at start LBA 1, and no other nonzero entry; reject hybrid MBRs (Linux warns and accepts them). Host tests: valid, missing signature, zero `0xEE`, two `0xEE`, wrong start LBA, hybrid |
| Primary header signature, size, revision and CRC | `efi.c` `is_gpt_valid()` :334 | `scan()` checks `EFI PART`, `92 <= hdr_size <= block size`, and the header CRC over `hdr_size`; it does not check revision or reserved | Require `EFI PART`, `header_size == 92`, revision `0x00010000`, reserved `0`, and the CRC over 92 bytes with the CRC field zeroed. Stricter than both references; fail closed on any mismatch |
| Header identity and reciprocal placement | `efi.c` `is_gpt_valid()` `my_lba` check; `compare_gpts()` :479 | `scan()` never checks `my_lba`, `alternate_lba`, or that the backup header is at the last LBA | Require primary `my_lba == 1`, `alternate_lba == capacity - 1`, backup `my_lba == capacity - 1`, `alternate_lba == 1`. Host tests mutate each field |
| Usable range | `efi.c` `is_gpt_valid()` `first_usable_lba`/`last_usable_lba` checks; `is_pte_valid()` :459 | `scan()` does not check usable bounds; `isPartitionInvalid()` (`:592`) only rejects a range whose end exceeds the container | Require `2 <= first_usable <= last_usable < capacity`, the primary entry array entirely below `first_usable`, and the backup array entirely above `last_usable`. Populated entries must be inside the usable range |
| Entry-array geometry and CRC | `efi.c` `is_gpt_valid()` (pt_size cap and array CRC) | `scan()` requires `entry_size >= 128` and `<= UINT16_MAX`, `entry_count <= UINT16_MAX`, and the array CRC over `count * size` | Require `entry_size == 128`, `1 <= entry_count <= 128`, the array span inside the device, and the CRC. The 128-slot cap covers this device's 84 slots and bounds memory/reads; larger geometry fails closed |
| Inclusive end arithmetic | `efi.c` `efi_partition()` :712 (`size = end - start + 1`) | `instantiateMediaObject()` (`:628`) computes `(end - start + 1) * blockSize` through `partitionSize -= partitionBase - mediaBlockSize` | Use the same inclusive arithmetic with overflow checks; never use an exclusive end or a half-open convention |
| Empty entries | `efi.c` `is_pte_valid()` skips a null type GUID | `isPartitionUsed()` (`:571`) skips a null type GUID | Treat a null type GUID as empty; do not inspect its start/end. Test an empty entry with nonzero stale fields |
| Single-sector entry | Linux accepts it (`end - start + 1 == 1`) | `isPartitionInvalid()` treats `start == end` as a placeholder and skips it, so XNU would not publish that partition | Keep the GPT valid, compare against the actual published set, and report the entry as `missing` with the `xnu_skip` reason. Do not "fix" XNU or silently drop the entry from the oracle |
| Out-of-container entry | `efi.c` `is_pte_valid()` rejects an entry whose end exceeds the last LBA | `isPartitionInvalid()` skips it but keeps the scheme valid | Reject the whole GPT (fail closed), not just the entry, because the oracle must be well formed before a completeness claim |
| Overlap and duplicate ranges | `efi.c` does not reject overlaps; Linux partition numbering would accept them | XNU does not reject overlaps or duplicates; it publishes both | Reject overlapping populated ranges and duplicate normalized ranges in the GPT. In the media set, duplicates are a mismatch (`dup=1`), never a match |
| Primary/backup agreement | `efi.c` `compare_gpts()` :479 compares reciprocal LBAs, usable bounds, disk GUID, entry count/size, array CRC and end-of-disk placement | XNU never reads the backup GPT | Require agreement on disk GUID, usable bounds, entry count, entry size, array CRC and the exact normalized populated set. Any disagreement fails closed (`xcheck=0`) |
| Direct partition media | Not applicable; Linux has no IOMedia ancestry | `IOGUIDPartitionScheme::start()` (`:145`) attaches each published media to the scheme and sets `kIOPartitionScheme_partition_valid`; `scan()` sets `kIOGUIDPartitionSchemeUUIDKey` | Select the scheme whose provider is the whole media and whose `UUID` property is an `OSString`; require each candidate media's provider to be that scheme. Exclude grandchildren and other devices; count excluded ancestry-matching media as `nested=1` |
| Media coordinates and property types | Not applicable | `instantiateMediaObject()` sets `Base` from `start * blockSize`, `Size` from the inclusive end, `Whole=false`, and `Content` from the type GUID | Require `Whole` `OSBoolean`, `Base`/`Size` `OSNumber`, `Content` `OSString`; require nonzero block-aligned sizes and in-container ranges. Wrong or missing types are `prop` errors, never zero-coerced |
| Enumeration order | Not applicable | `OSSet` order is not guaranteed and is not a GPT identity | Sort normalized `(base, size)` tuples before comparison; never compare registry order or `PARTn` order |
| `scripts/verify-gpt.py` as oracle | Existing host validator | Header location is assumed at 512 or 33 sectors; reciprocal placement is not checked; `capacity_sectors` is `last_usable - first_usable + 1` (usable span, not device capacity) | Use it only to re-check private captures after verifying their recorded hashes; do not inherit its header-location assumption, missing reciprocal checks or usable-span field in the runtime validator |

## 4. Independent same-boot GPT validation

The comparator validates the GPT itself. It does not trust the fact that XNU
published a `GUID_partition_scheme`, and it does not reuse the report baseline
buffer; a valid scheme with a corrupt or partial GPT must not become an oracle.

### 4.1 Reads and bounds

All reads go through the existing read-only block path, in 2-sector chunks
(`GTA4L_EMMC_BLOCK_CHUNK_BLOCKS`), with the existing per-command/data timeout.
The comparator stops on the first failure, performs no retry, reset, repair or
write, and checks cancellation and the read budget before every chunk.

| Read | LBA / byte range | Bound |
| --- | --- | --- |
| Protective MBR | LBA 0, 512 bytes | 1 sector |
| Primary header | LBA 1, 512 bytes | 1 sector |
| Primary entry array | `entries_lba`, `slots * 128` bytes | `slots <= 128`, at most 32 sectors |
| Backup header | `capacity_blocks - 1`, 512 bytes | 1 sector |
| Backup entry array | backup `entries_lba`, `slots * 128` bytes | at most 32 sectors |

Geometry caps (fail closed above them): block size 512, capacity 1 ..
`0xFFFFFFFF` sectors, `slots` 1..128, `entry_size == 128`, `header_size == 92`.
The device's 61,071,360 sectors and 84 slots are inside the caps. The
comparator's reads are counted separately as `reads` (requests), `blocks`
(sectors) and `fails` in the `PARTMAP` line. They are not part of the controlled
`QUAL`/`QUAL3` qualification counters; because the adapter counts every request,
the aggregate `XFER` line will include them when `partcmp` is selected, and the
`PARTMAP` counters attribute them explicitly.

### 4.2 Protective MBR

Require `data[510..511] == 0x55 0xAA`; exactly one partition record with
`os_type == 0xEE`; that record's start LBA is 1; every other record has
`os_type == 0`; and the protective size is nonzero and does not exceed the
device capacity or `0xFFFFFFFF` sectors. A hybrid MBR is refused. This is
stricter than Linux's warning-only hybrid handling and stricter than XNU's
"one `0xEE` anywhere" rule.

### 4.3 Header

Require the signature `EFI PART`, `header_size == 92`, revision
`0x00010000`, `reserved == 0`, and the CRC32 over the 92 header bytes with the
CRC field zeroed. Require `my_lba` to equal the LBA the header was read from
and `alternate_lba` to equal the opposite copy's LBA (`1` for the backup,
`capacity - 1` for the primary). Require `first_usable <= last_usable`,
`first_usable >= 2`, `last_usable <= capacity - 2`, and the entry-array span
inside the device. The primary array must end below `first_usable`; the backup
array must start above `last_usable` and end below `capacity - 1`.

### 4.4 Entry array and entries

Require the array CRC32 over exactly `slots * 128` bytes to match the header.
For each slot, a null type GUID is empty and is ignored. For a populated slot,
require `first <= last`, `first >= first_usable`, `last <= last_usable`,
inclusive size `last - first + 1`, and no overlap with any earlier populated
range. Reject exact duplicate ranges. Entries whose type is non-null but whose
range is invalid make the whole GPT invalid; they are not skipped one by one as
Linux's `is_pte_valid()` and XNU's `isPartitionInvalid()` do, because a
skipped entry would silently shrink the oracle.

### 4.5 Backup copy and cross-check

Validate the backup header and array with the same rules, using
`capacity - 1` as its expected `my_lba` and `1` as its expected
`alternate_lba`. Require the backup header's `disk_guid`, `first_usable_lba`,
`last_usable_lba`, `num_partition_entries`, `sizeof_partition_entry` and
`partition_entry_array_crc32` to equal the primary's, and require the exact
normalized populated set to be identical. This mirrors `compare_gpts()` while
also checking the array bytes themselves. Unlike Linux's `force_gpt` fallback,
N9 does not accept a backup-only GPT: both copies must be valid and agree.

### 4.6 Reason codes and fail-closed rule

`gpt=1` only when every check above passes. On the first failure, set `gpt=0`,
the reason code below, `complete=0`, and stop validation; do not sample or
compare media. No repair, fallback, reset, retry or write follows.

| Reason | Meaning |
| --- | --- |
| 0 | none |
| 1 | protective MBR invalid |
| 2 | primary header invalid |
| 3 | primary entry array invalid |
| 4 | backup header invalid |
| 5 | backup entry array invalid |
| 6 | primary/backup disagreement |
| 7 | entry range, overlap or duplicate invalid |
| 8 | read failure |
| 9 | read budget exhausted |
| 10 | media, scheme, property or ambiguity failure |
| 11 | stable media set does not match the GPT |
| 12 | media set still changing at the deadline |
| 13 | observation cancelled |

## 5. Exact IOMedia comparison

### 5.1 Whole media and direct children

Find the whole media exactly as N8 does: `ancestryContains(media)`, `Whole`
true, `Base == 0`, `Size == capacity * 512`, `Content ==
"GUID_partition_scheme"`. If no such media exists, or more than one exists,
fail closed (`reason=10`). Then find the GUID scheme: an `IOService` matching
`IOGUIDPartitionScheme` whose provider is the selected whole media and whose
`UUID` property (`kIOGUIDPartitionSchemeUUIDKey`) is an `OSString`. If none or
more than one matches, fail closed.

Enumerate `IOMedia` services with `ancestryContains`. For each non-whole
candidate, require `candidate->getProvider() == scheme`; otherwise exclude it
and set `nested=1`. This excludes grandchildren published by a nested scheme
and any unrelated device, which the current count cannot distinguish.

The selected whole media and scheme are retained for the comparison and
released on every exit path. Each candidate's `Base`, `Size`, `Content` and
`Whole` values are copied into the fixed snapshot array while the reference is
held; raw `IOService` pointers are never kept across a sleep, a media call or
the terminal-ownership handoff.

### 5.2 Normalization and multiset comparison

For each direct partition media, read `Whole` (`OSBoolean`, must be false),
`Base` (`OSNumber`), `Size` (`OSNumber`) and `Content` (`OSString`). Missing or
wrongly typed properties set `prop` and make the comparison fail; they are not
coerced to zero. Normalize to `(base_bytes, size_bytes)` and require
`size_bytes != 0`, `base_bytes % 512 == 0`, `size_bytes % 512 == 0`,
`base_bytes <= whole_size` and `size_bytes <= whole_size - base_bytes`.

For each validated populated GPT entry, normalize to
`(first_lba * 512, (last_lba - first_lba + 1) * 512)`. Sort both arrays by
`(base, size)` and compare them as exact multisets: equal length, pairwise
equality, no duplicates. Count `missing` (GPT tuples without a media match),
`extra` (media tuples without a GPT match) and `dup` (duplicate tuples in
either set). The acceptance predicate is the sorted-array equality, not
`media == used`; the counts are explanatory fields. Registry order, names,
UUIDs, type-GUID strings, `Partition ID` and `PARTn` order are never compared.

### 5.3 Bounded coherent snapshots

The GPT is read and validated once before sampling. Each media snapshot is one
registry walk into a fixed array of at most 128 tuples; no lock is held across
the walk, and no media call is made under the controller gate. Take the first
snapshot at nominal `t = 0`, then every 50 ms until the 2,000 ms deadline:

1. If the snapshot is unchanged for 250 ms and equals the GPT set, stop with
   `stable=1`, `match=1`.
2. If it is unchanged for 250 ms but differs, keep sampling until the deadline
   in case publication is still growing; a late growth is then observed.
3. At the deadline, if the final 250 ms were unchanged, stop with `stable=1`,
   `timed_out=0` and `match` reflecting the set. If not, stop with
   `stable=0`, `timed_out=1`, `match=0`, `reason=12`.
4. On cancellation, set `cancel=1`, `complete=0` and return through the
   no-panic cancellation path.
5. On a property/overflow/ambiguity error, set `prop` or the reason, stop, and
   do not claim a match.

A stable zero set is a valid snapshot: it compares as `missing=used` and is a
mismatch, not a timeout. A stable partial set is likewise a mismatch, not a
failure of the card. The comparator cannot see publication before its first
sample or after the deadline; `complete=1` is a bounded same-boot claim.

### 5.4 Outcomes and `complete`

`match=1` only when the stable sorted media set equals the validated GPT set.
`complete=1` only when `gpt=1`, `match=1`, `stable=1`, `timed_out=0`,
`budget=0`, `cancel=0`, `fails=0`, `prop=0`, `dup=0`, `missing=0`, `extra=0`
and `media == used`. Otherwise `complete=0`. This is a new, exact same-boot
comparison claim; it does not rewrite N8.5's historical `complete=0` and does
not promise that publication will not change later.

### 5.5 What is not compared

Not compared: partition names, type GUIDs, unique GUIDs, attributes, `Content`
strings, `Partition ID`, registry order, filesystem contents, mountability,
writability, or the GPT's physical location relative to the boot/recovery
partitions. `Content` is type-checked only. No raw GPT bytes, GUIDs, labels or
ranges are emitted in the report.

## 6. Report line and budget

When `gta4l-emmc-partcmp=1` is present, emit one bounded line before the
`PART1..3` samples:

```
GTA4L-EMMC P5 PARTMAP: gpt=1 reason=0 slots=84 used=80 pmbr=1 hdr=1 arr=1 bak=1 xcheck=1 media=80 match=1 missing=0 extra=0 dup=0 nested=0 prop=0 samples=6 changes=1 stable=1 stable_ms=250 deadline_ms=2000 timed_out=0 budget=0 reads=25 blocks=45 fails=0 cancel=0 complete=1
```

Field meanings:

- `gpt`: same-boot GPT validation passed; `reason` is the code in section 4.6.
- `slots` / `used`: entry-array slots and populated valid entries.
- `pmbr`, `hdr`, `arr`, `bak`, `xcheck`: stage results (1 pass, 0 fail).
- `media`: direct partition media under the selected scheme.
- `match`: exact sorted-multiset equality.
- `missing` / `extra` / `dup`: explanatory multiset differences.
- `nested`: at least one ancestry-matching media was excluded because it is not
  a direct child of the scheme (informational; it does not by itself fail a
  match).
- `prop`: number of direct candidates with missing or wrongly typed properties
  (capped at 255).
- `samples`, `changes`, `stable`, `stable_ms`, `deadline_ms`, `timed_out`: the
  same bounded-snapshot semantics as N8, with a 2,000 ms deadline.
- `budget`: the comparator's read budget was exhausted.
- `reads` / `blocks` / `fails`: the comparator's own request, sector and failure
  counts.
- `cancel`: cancellation was observed.
- `complete`: the exact same-boot claim described in section 5.4.

Budget and privacy rules:

- The `PARTMAP` line is capped at 384 bytes; all values are small bounded
  counts or booleans. No ranges, GUIDs, labels or identifiers are printed.
- N9.2 must format the complete report with the maximum values these fields can
  take on this device and assert `length < 2048` before NUL termination. If the
  assertion fails, shorten the `PARTMAP` line by dropping optional fields; do
  not truncate a line or emit a partial report.
- The existing formatter's fail-closed behavior is unchanged: if the writer
  cannot fit the report, the buffer becomes empty and the report path panics
  with the existing format-failure message rather than emitting a partial
  claim.

## 7. Bounded protocol, cancellation and watchdog

Proposed constants (fixed in N9.2 and not changed silently later):

| Constant | Value | Purpose |
| --- | --- | --- |
| `GTA4L_EMMC_BLOCK_PARTMAP_DEADLINE_MS` | 2000 | stable-snapshot deadline |
| `GTA4L_EMMC_BLOCK_PARTMAP_STEP_MS` | 50 | sleep between snapshots |
| `GTA4L_EMMC_BLOCK_PARTMAP_STABLE_MS` | 250 | unchanged-set window |
| `GTA4L_EMMC_BLOCK_PARTMAP_READ_BUDGET_MS` | 4000 | all GPT reads |
| `GTA4L_EMMC_BLOCK_PARTMAP_MAX_SLOTS` | 128 | entry-array cap |
| `GTA4L_EMMC_BLOCK_PARTMAP_MAX_MEDIA` | 128 | direct-media cap |
| `GTA4L_EMMC_BLOCK_PARTMAP_CHUNK_BLOCKS` | 2 | read chunk, matches the core |

Algorithm: validate the GPT (bounded reads, stop on first failure), then run
the snapshot loop. Check `reportCancel` before every read chunk, before every
registry walk and after every sleep. Sleep only in the report thread and never
while holding the controller `gate`. Publish the observation fields in one
coherent snapshot under a briefly held lock; never hold that lock across a
sleep or a media call. The existing terminal-ownership rule (`reportLock`,
`claimTerminal()`) still decides whether the report thread or the watchdog
emits the final panic.

Worst-case arithmetic: 20,000 ms media wait + 4,000 ms read budget + at most
one in-flight 1,000 ms read + 2,000 ms snapshot deadline = 27,000 ms, below the
30,000 ms watchdog. The pre-existing LBA 1 baseline read and qualification
reads remain part of the earlier report path; N8's 24,000 ms bound already
treated them as inside the watchdog margin. N9.2 must assert the 27,000 ms
bound in a host test and keep the terminal-race suppression.

## 8. Host-test plan (N9.2 acceptance)

The existing host simulator already injects `IOMedia` objects
(`gta4l_test_add_media()`, `gta4l_test_media_configure()`,
`gta4l_test_media_chain()` in
`scripts/sim-gta4l-emmc-block/Gta4lEmmcBlockTestSupport.h`) and can vary the
visible partition count over time. N9.2 must extend it with a synthetic GUID
scheme node and a 2-sector read hook so the direct-child rule, the GPT bytes
and the comparator's own read counters are exercised without a kernel build.

Required scenarios:

1. Valid 84-slot/80-entry synthetic GPT and 80 direct children; shuffled
   registry order; `match=1`, `complete=1`, `nested=0`.
2. Equal-count wrong range: 80 media, one range replaced; `match=0`,
   `missing=1`, `extra=1`, `complete=0`. This is the count-only mutation
   control.
3. Duplicate replacing a missing range with an unchanged count; `dup=1`,
   `match=0`, `complete=0`.
4. Nested container: a partition media with its own child media; the child is
   excluded, `nested=1`, and the direct set still matches.
5. Zero publication; partial publication stable at 39; late growth inside the
   window; growth after the window; `stable`, `timed_out`, `changes`, `match`
   and `complete` as specified.
6. Malformed properties: missing `Whole`/`Base`/`Size`/`Content`, wrong types,
   zero size, unaligned base or size, base+size overflow, range beyond the
   whole media; `prop` and `complete=0`.
7. Invalid GPT: PMBR signature, zero/two `0xEE`, wrong start LBA, hybrid MBR;
   header signature/size/revision/reserved/CRC; `my_lba`/`alternate_lba`;
   usable bounds; array span; array CRC; backup header/array; cross-copy
   disagreement; each sets `gpt=0` with the expected reason.
8. Geometry: `slots` 0, 1, 84, 128, 129 and 4096; `entry_size` 127/128/129;
   `header_size` 91/92/93; capacity 0/1; entries span beyond capacity; primary
   array overlapping the usable range; backup array overlapping the usable
   range; all fail closed without overflow.
9. Entry semantics: reversed range, out-of-usable range, overlap, exact
   duplicate, empty entry with stale start/end, single-sector entry (GPT valid,
   media missing, `xnu_skip`), and a populated entry with a null range.
10. I/O failures at each read (PMBR, header, each array chunk, backup header,
    backup array); first failure stops, `fails=1`, `reason=8`, no retry.
11. Cancellation before validation, between read chunks, between snapshots and
    after a sleep; `cancel=1`, no panic, no partial claim.
12. Watchdog race: the watchdog claims the terminal while the comparator is
    blocked; the comparator must not panic twice and must not overwrite the
    watchdog report.
13. Report budget: worst-case values fit 2,048 bytes; a deliberately small
    buffer returns 0 and takes the existing format-failure path; the
    `PARTMAP` line is at most 384 bytes; no raw GUID, label or range appears.
14. Negative/mutation controls: count-only acceptance must fail when a range
    differs; `match` must be 0 for an equal-count duplicate/missing set;
    `complete` must be 0 when unstable, cancelled, budget-exhausted or
    property-invalid; the absent-token legacy report must be byte-identical;
    `partobs`+`partcmp` must be refused.

Host oracle: after verifying the recorded private capture hashes
(`docs/emmc-reference.md`: primary `98bcfd9a…`, backup `12f80350…`; 84 entries
of 128 bytes, 80 populated, no overlaps, identical arrays), the test may use
those captures to derive a synthetic fixture with the same public geometry
(capacity 61,071,360 sectors, first usable 34, last usable 61,071,326, array at
LBA 2) and fake GUIDs/labels. The raw captures and any real GUIDs or labels
must stay in the ignored output tree and must never be committed. The runtime
validator must not inherit `scripts/verify-gpt.py`'s header-location
assumptions, missing reciprocal checks or usable-span `capacity_sectors`
field; the host test must include cases where those assumptions would give the
wrong answer.

## 9. Implementation surface and approval gates

N9.2, if approved, changes:

- `package/Gta4lEmmcBlock.h`/`.c`: the new token, selector, GPT validator,
  CRC32, normalized comparison, report fields and formatter; no transport,
  width, timing, DMA/CQE/ICE, write or request-limit change.
- `package/Gta4lEmmcBlock.cpp`: the opt-in comparator entry point, bounded
  reads, snapshot loop, cancellation and terminal handling.
- `scripts/build-gta4l-diagnostic-payload.py`,
  `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py`: the
  `emmc-block-1-partcmp`/`emmc-block-3-partcmp` personalities, token
  validation and the `partobs`+`partcmp` refusal.
- `scripts/sim-gta4l-emmc-block/*` and the host tests: the synthetic scheme,
  read hook and scenarios above.
- Documentation refreshes after implementation, with the hash table kept at
  45 rows.

Gates:

- N9.2 implementation: owner approval and Build mode. No candidate build.
- N9.3 archive/ABI/kernel/payload/BOOT production: completed host-only on
  2026-11-07; the record is
  `docs/emmc-n9.3-partcmp-candidate-2026-11-07.md`. The storage-ABI, binding
  and payload checks pass; no flash or boot.
- N9.4 hardware: fresh BOOT-only approval, verified protected backups and
  readbacks, and one owner-controlled boot. No mount or root switch, no writes,
  no wider/faster modes, no DMA/CQE/ICE and no request-limit changes.
- Initialization reliability remains a separate finite, predeclared
  investigation; N9 does not measure it.

## 10. Limits and non-goals

- N9 compares one bounded same-boot snapshot of direct GUID-scheme children to
  one validated primary/backup GPT. It does not prove that no later publication
  occurs, that nested containers are absent, that any partition is mountable,
  or that the card's write-protect state is physical.
- It does not repair, rewrite, format, mount, switch root, or import Arch's
  `userdata` layout. The Arch/pmOS references (`docs/archlinux-gta4l-evidence.md`,
  `docs/linuxforgta4l-org-audit.md`) describe a working eMMC-root stack through
  U-Boot and a pmOS kernel; they are integration context for a later, separately
  approved mount experiment, not inputs to this comparator. This batch did not
  re-fetch those repositories, and no N9.1 rule depends on their partition
  layout or package/kernel pins. Any later mount proposal must pin the actual
  Arch/pmOS kernel and package revisions (including the `gta4l-7.2-clean`
  branch distinct from the older `gta4l-7.2` pin), trace the package, kernel and
  filesystem requirements, and keep SM-T500 versus SM-T505N differences
  explicit before making a claim.
- It does not change the accepted N5, p5b/F5, N6 or N8.5 artifacts, and it does
  not upgrade N8.5's `complete=0`. A future N9.4 `complete=1` is a separate,
  exact same-boot claim with the scope stated in section 5.4.
- A stable partial set is a mismatch, not evidence that the card lost
  partitions. A changing set at the deadline is an observation limit, not a
  failure. A registry or read error is a measurement failure, not a card
  verdict.
