# gta4l N9.6 exact-comparator observation (2026-11-07)

Status: BOOT-only flash of the N9.5 corrected whole-media `Base` candidate and
one owner-controlled first cold boot on 2026-11-07. The report selected the
real whole media (`whole=1 base=0 size=31268536320
content=GUID_partition_scheme writable=0`), so the N9.5 correction works on
hardware, but the comparator stopped at the GUID-scheme lookup before its first
GPT read (`gpt=0 reason=10 reads=0 samples=0 complete=0`). The owner returned
the tablet to TWRP; the one-boot budget is consumed. No commit, push, retry,
reflash or repair was performed. This is not a partition-completeness or
reliability result.

## 1. Flash and boot scope

The owner approved the BOOT-only write in this session. The flash record is in
`out/gta4l/ram-root-emmc-n9.6-hardware-evidence/` (`flash-commands.log`
`f7e32544…`, `flash-record.json` `3ccc9d9d…`).

- BOOT image `45f3bb81…` (85,688,320 bytes), payload `79ac3a10…`, prelinked
  kernel `a610e9d1…`, packaged kernel `a17c71dd…`.
- Candidate source `Gta4lEmmcBlock.cpp` `b7aeff0a…`, policy source
  `Gta4lEmmcBlock.c` `417ea291…` and header `Gta4lEmmcBlock.h` `ce30dfff…`.
- Only `/dev/block/mmcblk0p69` was written (20,920 x 4096 records, synced).
  The full readback `9c5bcf2c…` matched the expected candidate-prefix plus
  unchanged-tail value; prefix `45f3bb81…`, tail `a28b4ea2…`. RECOVERY
  `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` were unchanged, and
  the protected-partition backups were verified before the write.
- Rollback `boot-before-n9.6.img.xz` (`baddb107…` -> raw `8d5042d3…`) preserves
  the previous N9.4 candidate image. It was not restored.

The owner supplied the wrapped report and confirmed TWRP return. The raw text,
a bounded interpretation and a separate non-self-hashing manifest are in
`out/gta4l/ram-root-emmc-n9.6-hardware-evidence/observation-1/` (raw
`440b0d44…`, interpretation `57221ca8…`, record `87c45795…`; the manifest binds
the unchanged `evidence-sha256.txt` `423923e7…`). The report is associated with
the flashed image through the flash record, not an independently measured
running-image hash.

## 2. What the report shows

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 mode=3 blocks=61071360 bs=512
bytes=31268536320 wid=0 tim=0 clk=400000
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320
content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 PARTMAP: gpt=0 reason=10 ... samples=0 reads=0 blocks=0
fails=0 cancel=0 complete=0
```

- `rc=0 stop=0 mode=3` is header success, not comparator success.
- The MEDIA fields are populated only after exactly one qualifying whole medium
  is selected. `whole=1 base=0 size=31268536320
  content=GUID_partition_scheme writable=0` therefore records a successful
  whole-media selection, and `base=0` is the implied zero base rather than a
  published `Base` property. The N9.5 absent-`Base` correction works on the
  real whole media.
- `reason=10` is `GTA4L_EMMC_BLOCK_PARTMAP_REASON_MEDIA`. In
  `runPartitionComparison()` the same reason is reported by `findGuidScheme()`
  and by the following whole-media reader-open refusal, and both occur before
  the first `readPartmapSectors()` call. `reads=0 blocks=0` confirms that no
  GPT read was attempted, and `samples=0 complete=0` confirms that the
  partition snapshot never started.
- GPT validation and the exact range comparison never began. `gpt=0` is not
  evidence of a corrupt GPT, and `media=0` is not a measured partition count.
- `READ`/`QUAL`/`QUAL3` show no controlled qualification attempt in this run.
- Driver aggregate: online=1, wp=1, 10 reads/29 blocks, fails=0, maximum
  request 8 blocks. The transport is consistent (29 = 7 CMD17 + 22 CMD18
  blocks, 11 CMD18 commands and 11 stops, no command or stop failures).
  These are incidental/scan requests, not comparator reads.
- Capacity remains 61,071,360 x 512 = 31,268,536,320 bytes; `wid=0 tim=0
  clk=400000` retains the 1-bit legacy timing policy. `SNAP locked=1`; FAIL is
  valid=0/all-zero. Frames/pages/free add no storage qualification.
- The supplied panic suffix is `@Gta41EmmcBlock.cpp:1991` (spelling preserved).
  In the candidate source `b7aeff0a…`, line 1991 is the intentional
  `panic("%s", reportBuffer)` in `panicWithReport()`. Header success and FAIL
  valid=0 are consistent with the intentional report stop, not a newly
  inferred crash.

## 3. Source-grounded cause (N9.7 host-only analysis)

`findGuidScheme()` enumerates
`IOService::getMatchingServices(IOService::serviceMatching("IOGUIDPartitionScheme"))`
and requires exactly one service whose `getProvider()` is the selected whole
media and whose `kIOGUIDPartitionSchemeUUIDKey` property is an `OSString`.

In the pinned XNU, `getMatchingServices()` calls
`copyExistingServices(matching, kIOServiceMatchedState)`; `instanceMatch()`
requires the service state to contain `kIOServiceMatchedState` and to be
non-inactive. That state bit is set by `copyNotifiers(gIOMatchedNotification,
…)` inside `doServiceMatch()`, which is reached from
`IOService::registerService()` -> `startMatching()`. A service that never calls
`registerService()` is therefore invisible to `getMatchingServices()`.

In the pinned IOStorageFamily `67f9003c`, `IOGUIDPartitionScheme::start()`
attaches and registers every partition media, but never registers the scheme
object itself; `IOPartitionScheme` and `IOStorage` do not register it either.
The scheme is nevertheless present in the service plane as a direct child of
the whole media: `IOService::startCandidate()` calls `service->attach(this)`
before `probe()`/`start()`, and `IOMedia::attachToChild()` copies the scheme's
`Content Mask` (`GUID_partition_scheme`, personality
`IOMatchCategory=IOStorage`) into the whole media's `Content` property.
`findExactWholeMedia()` requires that `Content` value, so the accepted whole
media proves the scheme attached before selection.
`IOGUIDPartitionScheme::scan()` also sets the `UUID` property during probe,
before attach. The lookup therefore fails because the attached scheme is
unregistered, not because it is absent.

The whole-media reader-open guard is not reached. `IOGUIDPartitionScheme::scan()`
opens the media with `open(this, 0, kIOStorageAccessReader)` and closes it
before returning, and the pinned `IOMedia::handleOpen()` sums the existing
reader levels rather than excluding a second reader. No evidence supports open
refusal as the cause.

The host seam previously returned every configured scheme from
`getMatchingServices()` and modeled no registration state, so the comparator
tests could not see this failure. It now models registration visibility:
`getMatchingServices()` returns only schemes whose `registerService()` was
called, and the adapter scenarios pin the hardware-identical
`gpt=0 reason=10 … samples=0 complete=0` signature for an
attached-but-unregistered scheme (`partcmp-scheme-unregistered`), a missing
scheme (`partcmp-scheme-absent`), a missing `UUID`
(`partcmp-scheme-no-uuid`), a wrong provider
(`partcmp-scheme-wrong-provider`), duplicate matching schemes
(`partcmp-scheme-duplicate`) and reader-open refusal
(`partcmp-open-refused`). The block suite passes 48 core scenarios/38 mutation
controls and 93 adapter scenarios with 16 adapter and eight policy mutation
controls.

## 4. What this does not establish

- No exact GPT-to-`IOMedia` comparison, no GPT validation and no range result.
- No partition-completeness claim. N8/N8.5's historical `complete=0` is not
  upgraded.
- No initialization-reliability result; the one boot is a single bounded
  diagnostic run.
- No statement about wider/faster bus modes, DMA/CQE/ICE, arbitrary I/O,
  mount/root switch or microSD.
- No claim that the whole media or its partitions were absent; the comparator
  stopped before any partition snapshot.

## 5. Next steps and gates

The host-only investigation narrows the failure to the GUID-scheme lookup and
proposes the smallest correction for owner approval only: replace the
`getMatchingServices()` enumeration with the selected whole media's
service-plane children (for example `whole->getChildIterator(gIOServicePlane)`
or `getChildEntry(gIOServicePlane)`), keeping the class, direct-provider,
`UUID` and exactly-one guards and the existing discovery wait. The accepted
whole media already proves the scheme attached, so no new wait or timing change
is proposed.

The host seam models registration visibility first. An approved host-only
correction must add service-plane child tracking, invert
`partcmp-scheme-unregistered` to require the full successful comparison, and
preserve the absent/missing-`UUID`/wrong-provider/duplicate controls plus the
reader-open-refusal control. No driver correction, candidate build,
ABI/QEMU audit, device access, flash, boot, retry, reset/repair, commit or push
is authorized or performed by this record.

The owner's comment that "the delay was way less now :)" is preserved as a
subjective observation, not a measured duration. It is consistent with
whole-media discovery now succeeding instead of consuming the nominal 20,000 ms
wait, but the report does not timestamp it and no hardware-speed claim is
made.

## 6. Evidence

Raw artifacts and hashes are in the gitignored
`out/gta4l/ram-root-emmc-n9.6-hardware-evidence/` directory. The observation
subdirectory binds the original flash manifest by hash and does not hash
itself. The repository records only the sanitized summary and the bounded
interpretation.
