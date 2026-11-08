# gta4l N9.4 exact-comparator observation (2026-11-07)

Status: BOOT-only flash of the N9.3 `gta4l-emmc-partcmp=1` candidate and one
owner-controlled first cold boot on 2026-11-07. The boot reached the bounded
diagnostic report but failed closed at whole-media discovery with
`reason=10`, before any GPT validation or partition comparison. The owner
returned the tablet to TWRP; the one-boot budget is consumed. No commit, push,
retry, reflash or repair was performed. This is not a partition-completeness or
reliability result.

## 1. Flash and boot scope

The owner approved the BOOT-only write in this session. The flash record is in
`out/gta4l/ram-root-emmc-n9-hardware-evidence/` (`flash-commands.log`
`3b3f9065…`, `flash-record.json` `89c90441…`).

- BOOT image `fa55ef61…` (85,688,320 bytes), payload `fd73b37f…`,
  prelinked kernel `fd2c8c65…`, packaged kernel `d5a98930…`.
- Candidate source `Gta4lEmmcBlock.cpp` `062ee8fa…`.
- Only `/dev/block/mmcblk0p69` was written (20,920 x 4096 records, synced).
  The full readback `8d5042d3…` matched the expected candidate-prefix plus
  unchanged-tail value; prefix `fa55ef61…`, tail `a28b4ea2…`. RECOVERY
  `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` were unchanged, and
  the protected-partition backups were verified before the write.
- Rollback `boot-before-n9.img.xz` (`0e8c61fd…` -> raw `9d51fe80…`) preserves
  the previous N8.5 image. It was not restored.

The owner supplied the wrapped report and confirmed TWRP return. The raw text
and a bounded interpretation are in
`out/gta4l/ram-root-emmc-n9-hardware-evidence/observation-1/`
(raw `97cc0002…`, interpretation `75f7bb79…`, record `ac7dcfb3…`). The report
is associated with the flashed image through the flash record, not an
independently measured running-image hash.

## 2. What the report shows

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512
bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 MEDIA: whole=0 base=0 size=0 content=other writable=0
GTA4L-EMMC P5 PARTMAP: gpt=0 reason=10 ... reads=0 blocks=0 fails=0
cancel=0 complete=0
```

- `rc=0 stop=0 mode=3` is header success, not comparator success.
- `reason=10` is `GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA`.
- The MEDIA fields are set only after exactly one qualifying whole medium is
  selected. Their zero values are consistent with failure before that point.
  They do not prove that no whole media existed in the registry, and the report
  does not distinguish zero matches from multiple matches.
- GPT validation and the exact range comparison never began. `gpt=0` is not
  evidence of a corrupt GPT; `media=0` is not a measured partition count.
- `READ`/`QUAL`/`QUAL3` show no controlled qualification attempt in this run.
- The driver reports online=1, wp=1, 9 reads/29 blocks, fails=0, max=8. The
  transport is consistent (29 = 7 CMD17 + 22 CMD18 blocks, 11 CMD18 commands
  and 11 stops, no command or stop failures). These are incidental/scan
  requests, not comparator reads. They do not qualify arbitrary I/O, partition
  completeness or initialization reliability.
- Capacity remains 61,071,360 x 512 = 31,268,536,320 bytes; `wid=0 tim=0
  clk=400000` retains the 1-bit legacy timing policy. `SNAP locked=1`; FAIL is
  valid=0/all-zero. Frames/pages/free add no storage qualification.
- The supplied panic suffix is `@Gta41EmmcBlock.cpp:1982` (spelling preserved).
  In the candidate source `062ee8fa…`, line 1982 is the intentional
  `panic("%s", reportBuffer)` in `panicWithReport()`. Header success and
  FAIL valid=0 are consistent with the intentional report stop.

## 3. Source-grounded cause

`findExactWholeMedia()` requires all of:

- `Whole` present as an `OSBoolean` and true;
- `Base` present as an `OSNumber` and equal to 0;
- `Size` present as an `OSNumber` and equal to `capacityBlocks * 512`;
- `Content` present as an `OSString` and equal to `GUID_partition_scheme`;
- ancestry within the four-level cap.

In the pinned IOStorageFamily `67f9003c`, the whole media is created by
`IOBlockStorageDriver::instantiateMediaObject()`, which passes an empty content
hint and calls `IOMedia::init()`. `IOMedia::init()` publishes `Content`,
`Content Hint`, `Preferred Block Size`, `Size`, `Whole` and `Writable`, but it
does not publish `Base`. `kIOMediaBaseKey` is defined in
`IOPartitionScheme.h` and is set only by partition schemes on partition media
(`IOGUIDPartitionScheme.cpp`, `IOFDiskPartitionScheme.cpp`,
`IOApplePartitionScheme.cpp`). The GUID scheme's personality supplies the
`Content Mask` `GUID_partition_scheme`, which `IOMedia::attachToChild()` copies
into the whole media's `Content` when the scheme attaches.

The real whole media therefore has no `Base` property. `OSDynamicCast` of the
absent property yields null, so the exact finder rejects the media on every
poll. With no match, the discovery loop consumes its nominal 20,000 ms bound
(`GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS`, 100 ms steps) and reports reason 10.
`timed_out=0` and `deadline_ms=2000` belong to the later partition-snapshot
phase, which was never reached.

The host seam hid this. `gta4l_test_media_configure()` sets `Base` on every
media, including whole media, so the comparator tests always saw `Base=0`. The
`partcmp-property` `BASE_MISSING` control faults a partition media (fixture
index 1), not the whole media. The test suite therefore mirrored the driver's
assumption rather than the pinned registry behavior.

The owner observed that the log took "a litte longer than usual … a couple of
seconds more instead of instant". That wording is preserved as a subjective
observation, not a measured duration. The discovery loop is the only whole-media
wait in this path and can consume up to 20,000 nominal ms; the report does not
timestamp it. The failure is a deterministic property mismatch, not a timing
race, and no hardware slowdown is inferred.

## 4. What this does not establish

- No exact GPT-to-`IOMedia` comparison, no GPT validation and no range result.
- No partition-completeness claim. N8/N8.5's historical `complete=0` is not
  upgraded.
- No initialization-reliability result; the one boot is a single bounded
  diagnostic run.
- No statement about wider/faster bus modes, DMA/CQE/ICE, arbitrary I/O,
  mount/root switch or microSD.
- No claim that the whole media or its partitions were absent; discovery
  rejected the media on a property predicate before recording it.

## 5. Next steps and gates

The host seam first modelled the real whole-media profile without changing the
driver: `partcmp-whole-no-base` faulted the whole media's `Base` and pinned the
observed hardware signature. The owner then approved the host-only driver
correction, which is now implemented:

- `findExactWholeMedia()` fetches the raw `Base` property. An absent `Base` on
  a whole media is accepted as the implied zero base; a present property that
  is not an `OSNumber` or is nonzero is rejected. The `Whole`, `Size`,
  `Content`, ancestry and exactly-one guards are unchanged.
- Partition media still require `Base` in `snapshotPartitionMedia()`; the
  legacy `findWholeMedia()` still tests only `Whole` and ancestry.
- `partcmp-whole-no-base` is inverted to require acceptance and the full
  successful comparison (`whole=1 base=0`, `gpt=1 reason=0`, `media=80
  match=1`, `complete=1`).
- The new `partcmp-whole-base-invalid` control rejects a present nonzero
  `Base` and a present wrong-type `Base` with `reason=10` before any read.
- `partcmp-property` still faults a partition media, so the partition-side
  `Base` requirement remains covered.

The block suite passes 48 core scenarios/38 mutation controls and 87 adapter
scenarios with 16 adapter and eight policy mutation controls. This is host-only
evidence: no driver candidate was rebuilt, no ABI/QEMU audit was run, and no
device access, flash, boot, retry, reset/repair, commit or push was performed.
N8/N8.5's historical `complete=0` is not upgraded and no completeness or
reliability claim is made.

N9.5 archive/prelink/ABI-QEMU/payload/BOOT production remains separately
gated, followed by fresh explicit BOOT-only approval, verified protected
backups/readbacks and a new predeclared owner-controlled one-boot budget. No
retry, reflash, rollback, reset/repair, mount/root switch, partition write,
wider/faster mode, DMA/CQE/ICE or request-limit increase is authorized by this
record. No commit or push.

## 6. Evidence

Raw artifacts and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n9-hardware-evidence/` directory. The observation
subdirectory binds the original flash manifest by hash and does not hash
itself. The repository records only the sanitized summary and the bounded
interpretation.
