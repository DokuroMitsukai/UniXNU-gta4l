# gta4l N10.15 read-only consumer design (host-only, 2026-11-10)

Status: host-only revised design for testing, 2026-11-10. This revision
performed no device access, boot, flash, candidate build, driver change, QEMU
run, commit or push. The owner approved host-only testing of a reduced scope:
one two-block (1,024-byte) BOOT read by a distinct `IOService` client, with
external host-side content comparison. The earlier two-window 64-block design
is retained only as history; its timing no-go and content-binding problem are
not active requirements of this revision. Gate A remains open for candidate
production and hardware because the timing argument is conditional. This
document does not authorize a candidate build, a hardware run, a commit or a
push. The corrected post-N10.10 roadmap remains the sequencing authority; this
proposal does not replace M3, the optional repeatability series or any
mount/root decision.

## 1. Why this rung, and what N10.14 does not establish

N10.14 recorded one owner-reported first cold boot of the unchanged N10.13
bounded read-sweep candidate. The six-case sweep completed through the real
`IOMedia`/`IOBlockStorageDriver`/`IOBreaker` path with two pinned anchors and
four runtime-derived boot-partition windows:

```
SWEEP: cases=6 passed=6 failed=0 reason=0 complete=1 reads=6 bytes=53248
span=53248 blocks=104 subrequests=13 max=8 foreign=0 fails=0 cancel=0
budget=0 media=0 part=0 refs=6 refbytes=53248 anchor=1 mismatches=0
limit=8 seg=1 cap=61071360
```

Both anchors re-hashed to their invariant v5 digests, every direct CMD17
reference agreed byte-for-byte, and the last case printed an equal
`digest`/`refdigest` prefix. That is bounded same-boot consistency evidence
for six ranges. It is not arbitrary-I/O safety, not a mount or root result,
not a write result, not a wider/faster-mode result, not a DMA/CQE/ICE result
and not an initialization-reliability result. Initialization reliability
remains `[U]`.

All diagnostic modes to date use the driver itself as the `IOMedia` client:
the report thread calls `media->read(this, ...)` where `this` is the
`Gta4lEmmcBlock` provider. The provider therefore opens, reads and closes its
own published media. N10.2 already established successful 64-block BOOT media
reads through that provider-as-client path at the 64-block production
advertisement, although its page-aligned `IOMallocAligned` scratch was not
guaranteed physically contiguous and the observed `DRV max=8` is consistent
with a split at the first 4 KiB physical segment. The pinned storage-family
source confirms the mechanism: `IOBreaker::getBreakSize()` with a one-segment
advertisement stops at the first physical segment and truncates the break
size to the request block size, so a 4 KiB first segment yields an 8-block
break. N10.2's actual physical segment geometry was not recorded, so a 4 KiB
first segment remains a hypothesis, not a proven cause. This design does not
rely on that explanation: the contiguous allocation is required independently
to make the decomposition deterministic. This rung does not claim a larger
media read or a new transport mode. The new elements are the distinct client
identity and the physically contiguous 32 KiB allocation behind a
1,024-byte descriptor, which should produce one two-block driver request.
This is a report-thread-driven test instrument, not a production filesystem
or BSD consumer, and it does not establish mount readiness. The single
window is not a content-coverage upgrade over N10.14's six cases; it is a
minimal known-answer contract for the distinct client. The new evidence is
client-path integration and the exact single-request decomposition, not
broader range coverage. The next question is narrower: can a **distinct**
client object open the published BOOT partition through the normal client
interface and satisfy a minimal externally compared content contract, rather
than the driver's larger diagnostic case tables? This design answers that
question with a read-only experiment and a fail-closed no-go if the consumer
cannot be built and tested safely.

## 2. What "consumer" means here

In IOKit terms the provider is the `IOBlockStorageDevice`/`IOBlockStorageDriver`
side that publishes `IOMedia`; a consumer is an `IOService` that opens a media
object and issues reads. The current diagnostic modes are provider-driven: the
provider is also the client. The proposed consumer is a minimal, separate
`IOService` object that exists only for the duration of the run and uses its
own client identity for `open()` and `read()`.

Candidate consumers considered for this rung:

| Candidate | Consumer | Read scope | Verdict |
| --- | --- | --- | --- |
| Independent IOMedia client | New minimal `IOService` client owned by the report thread | One fixed BOOT-partition window (two blocks) | **Selected** |
| Production-stack observation | `IOGUIDPartitionScheme`, `IOMediaBSDClient`, APFS | Stack-chosen GPT/superblock reads | Fallback/next; traffic is asynchronous and not exactly attributable |
| BSD/userland client | `/dev/disk*` node opened by a RAM-root fixture | BOOT slice | Later; the current fixture uses mockfs, not devfs, and node naming is not yet established |
| APFS probe | Prelinked `com.apple.filesystems.apfs` | Container superblocks | Later; the probe is not instrumentable from this driver and may read partition content outside this scope |
| Mount/root | Filesystem client | Volume | Separately gated; not this rung |

The independent client is selected because it gives exact request bounds, a
distinct client identity, the production advertisement, external host-side
content verification and a deterministic fail-closed acceptance, without
mounting, writing or reading any partition other than the public diagnostic
BOOT image. It is still a test instrument, not a filesystem. It does not by
itself make the storage stack usable for mount or root.

## 3. Scope

### 3.1 One partition, one fixed window

The consumer reads only the BOOT partition at the retained layout base and
size:

| constant | value | meaning |
| --- | ---: | --- |
| `GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES` | 1,154,744,320 | 2,255,360 blocks x 512 |
| `GTA4L_EMMC_BLOCK_PARTREAD_BOOT_SIZE_BYTES` | 100,663,296 | 196,608 blocks x 512 |
| candidate BOOT image size | 85,688,320 | unchanged N10.13 BOOT image |
| candidate BOOT image SHA-256 | `b626a1fa52a559e274d7291243c9bb959947365c168b6b728265629b3f752aeb` | unchanged image |

The single window is:

| index | target | offset in BOOT | length | blocks | external SHA-256 prefix (first 16 bytes) |
| ---: | --- | ---: | ---: | ---: | --- |
| 0 | BOOT | 0 | 1,024 | 2 | `5a25ed5ef1d90de9163f2f1be3bed9b2` |

The digest above is the external reference for the unchanged N10.13 image
`b626a1fa…`; it is not a compiled-in candidate constant. The window is the
first 1,024 bytes of the Android boot v1 image: the boot header page. The XNU
kernel Mach-O is in the FAT ramdisk, and the header SHA-1 at offsets 576-595
covers the kernel and ramdisk (`scripts/package-gta4l-diagnostic.py:209-216`).
Changing the XNU kernel changes the header ID inside this window, so a
compiled-in expected digest would be self-referential. This revision therefore
uses option A: the consumer prints the observed digest and the host compares it
with the external reference. The 64-block request, the 32 KiB window and the
tail window are deferred; they are not part of this revision. The consumer
still exercises the production 64-block / 32 KiB / one-segment advertisement,
but its single request is two blocks. No other partition, no whole-media
scattered range, no GPT range, no free space and no user-data range is read.
The driver still publishes the whole media and the partition scheme still scans
the GPT as it does on every boot; those incidental stack reads are not
initiated by this consumer and are reported separately, not credited to it.

### 3.2 What the consumer is not

The consumer does not mount a filesystem, does not switch the root device,
does not write, does not repair, does not reset the card and does not retry.
It does not implement arbitrary stack-selected reads: the single window and
its length are fixed and pinned. It does not change the production
advertisement, the CMD17/CMD18 transport, the 400 kHz clock, the 1-bit SDR
width or any other mode. It does not read a partition whose content is not
already the project's public diagnostic image.

## 4. Mode selection and advertisement

The mode is selected by exactly one `gta4l-emmc-consume=1` token alongside
exactly one `gta4l-emmc-block=3` token:

- The token requires exactly one block token. An orphan token without the
  block mode is refused, not inert.
- `gta4l-emmc-block=1` is refused for this mode. The consumer exists to
  exercise the multi-block production path; the single-block CMD17 path is
  already covered by N10.1/N10.2.
- A duplicate `gta4l-emmc-consume` token, a non-1 or malformed value, or any
  combination with `gta4l-emmc-partobs`, `gta4l-emmc-partcmp`,
  `gta4l-emmc-partread`, `gta4l-emmc-deblock`, `gta4l-emmc-breakup` or
  `gta4l-emmc-sweep` is refused.
- The absent-token legacy, partobs, partcmp, partread, deblock, breakup and
  sweep reports remain byte-for-byte unchanged.
- The transport policy is unchanged: 1-bit SDR, 400 kHz, polling PIO, the
  existing two-sector CMD18 / CMD12 policy, read-only publication, no retries
  and no fallback.

The consume mode uses the **production** advertisement, not a mode-scoped
diagnostic reduction:

- `kIOMaximumBlockCountReadKey = 64`
- `kIOMaximumByteCountReadKey = 32,768`
- `kIOMaximumSegmentCountReadKey = 1`

The existing `setProperty` fall-through already advertises these values for
modes that are not sweep, breakup or deblock. Consume mode must not set any of
those mode flags; its advertisement is that production fall-through, pinned by
a host scenario that asserts 64 / 32,768 / 1 and by `limit=64 seg=1` in the
report. A wrong advertisement fails the guard.

The adapter's `doAsyncReadWrite()` data path, `copyOutCallback()` and core
chunk loop are unchanged. The consume mode adds one entry-side observer at the
top of `doAsyncReadWrite()`, before the adapter takes the gate, and one
completion-side accounting step inside the gate; both observe only and never
alter the data path. The entry observer is active only while a consumer case
is in flight and requires the descriptor tag to equal the active case tag. A
tag-0 descriptor or any other tag is `foreign`, even when its block range
overlaps the active window; consume mode does not inherit the sweep observer's
tag-0 in-window exception. The active window is expressed in whole-media
blocks (`GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES / 512 + case offset / 512`),
because `IOMedia::read()` adds the partition base before the provider sees
the request; the report's `start` remains the BOOT-relative byte offset. The
consumer's reads are whole-block aligned, so `deblockRequest()`
short-circuits, and they fit inside the 32 KiB break size, so
`breakUpRequest()` short-circuits to `executeRequest()` with the original
descriptor; the observer therefore sees the consumer's tag, not a
family-created copy. If the real family path loses the tag, the run fails
closed with `foreign=1`; it does not fall back to range attribution. N10.14's
`foreign=0` is the precedent for tag propagation, but the host seam must
assert the tag explicitly rather than rely on that observation.

The report thread sets the active case identity under `reportLock` before the
read and clears it after the read, exactly as the sweep sets `sweepActive`,
`sweepActiveTag`, `sweepActiveFirst`, `sweepActiveNext` and `sweepActiveEnd`
under `reportLock` (`Gta4lEmmcBlock.cpp:4314-4327`). The active identity is
the case tag, the exact whole-media first block, the exact block count, the
exclusive end block and an `inFlight` flag. Before each read the consumer
calls `descriptor->setTag(caseTag)` with a distinct nonzero tag; the single
case uses one tag. A fresh descriptor has tag 0, which is `foreign`,
so omitting `setTag` fails the guard. The entry observer takes only
`reportLock` and checks, in order: an active case exists; `inFlight` is false;
the tag equals the active tag; the block equals the active first block; the
block count equals the active block count; and the request is entirely inside
the active range. Any tag, first-block, block-count or range mismatch latches
`FOREIGN` and increments `foreign`. A second matching request while
`inFlight` is true is a duplicate or concurrent attribution failure and
latches `ORDER`. A read before the case, a second read after it, or a missing
case also latches `ORDER`; `ORDER` is a
reason, not a separate report counter. The entry observer sets `inFlight`
only after all checks pass; the
report thread clears the active identity, including `inFlight`, after the
read returns on every path.

Successful per-case transport completion is captured inside
`doAsyncReadWrite()` while `IOLockLock(gate)` is held. The source of truth is
the adapter-private `block.counters` (`Gta4lEmmcBlockCore.h:45-62`), not the
similarly named report fields copied by `snapshotReport()`. `snapshotReport()`
takes the gate separately and is a coherent whole-boot snapshot, not an exact
per-request delta. Immediately before `gta4l_emmc_block_read()` the adapter
copies `requests`, `blocks`, `cmd17_reads`, `cmd18_chunks`, `cmd18_blocks`,
`chunk_failures`, `stop_attempts` and `stop_failures`; immediately after it
returns it reads the same fields and credits the deltas only when the entry
observer recorded the exact tagged case request and the request succeeded.
The entry observer returns a per-request local attributed flag; the
completion step does not re-read shared observer state. The gate serializes
requests, so no other request can change those counters inside the snapshot
pair. The completion accounting uses the active identity captured at entry;
it does not acquire `reportLock` while holding `gate`. The
lock order is `gate` before `reportLock`; no path holds `reportLock` while
acquiring `gate`. The existing sweep observer is mode-scoped and must not run
in consume mode. The host seam must model the tag, the exact block range, the
in-flight flag and the private counter deltas, and the mutation controls must
remove the tag check, the range check and the delta check separately.

## 5. Consumer object and lifecycle

### 5.1 A minimal distinct client

The consumer is a minimal `IOService` subclass with no overrides:

```
class Gta4lEmmcConsumer : public IOService
{
    OSDeclareDefaultStructors(Gta4lEmmcConsumer)
public:
    bool run(...);          /* non-virtual, called by the report thread */
};
OSDefineMetaClassAndStructors(Gta4lEmmcConsumer, IOService)
```

It is created by the report thread with `new`, initialized with `init()`
(which resolves to `IOService::init(OSDictionary *dictionary = NULL)` at
vtable slot 23 with a null dictionary; `IOService::init` calls
`OSObject::init()` at slot 15), used only for the run, and released with
`release()`. It has no personality, no registration and no properties. Its
only purpose is to provide a distinct `IOService *` identity for
`IOMedia::open()` and `IOMedia::read()`, so the provider sees a client that
is not the provider.

The base `IOService` constructor, destructor, vtable and metaclass symbols
must resolve against the pinned kernel. Gate A compiled a corrected
full-lifecycle probe (`new`, `init()`, `open`, one synchronous read with
`setTag` and `getPhysicalSegment`, `close`, `release`,
`IOMallocContiguous`, `IOFreeContiguous`) with the adapter's pinned
`IOPlatformExpert.cpo.json` compile record plus `-I build/storage-abi/include`
and `-I package`, both required. The record alone does not resolve the
qualified storage headers; the probe must be compiled from the repository
root with those include paths. It compiles with rc 0 (warnings are
non-fatal). The resulting object has 187 undefined symbols, and every one
matches a defined global symbol in the unchanged N10.13 candidate kernel
(`out/gta4l/ram-root-emmc-n10.13-sweep-candidate-evidence/candidate-kernel.macho`)
after stripping the Mach-O leading underscore. Of the 187, 185 resolve to
`__TEXT_EXEC,__text`; `PAGE_SHIFT_CONST` resolves to `__DATA_CONST,__const`
and `__ZN9IOService10gMetaClassE` to `__DATA,__common`. `llvm-nm` labels all
187 `S`; `S` here means a defined symbol in a non-standard section (the
kernel's executable text is `__TEXT_EXEC`, not `__TEXT,__text`), so `S`
alone does not distinguish text from data; the section split above is the
qualification.
The checked set includes `__ZN9IOServiceC1Ev`, `C2Ev`, `D0Ev`, `D1Ev`,
`D2Ev`, `__ZTV9IOService`, `__ZN9IOService9metaClassE`,
`__ZN9IOService10gMetaClassE`, the `OSMetaClass` constructor/destructor,
`OSObject::init`, `IOService::init(OSDictionary*)`, the typed `OSObject`
new/delete operators, `IOMemoryDescriptor::withAddress`,
`IOMallocContiguous` and `IOFreeContiguous`. No address invention and no audit
bypass is required. The `setTag` and `getPhysicalSegment` calls are virtual
dispatches and add no undefined symbols; their slots are qualified by the
layout/vtable evidence below.

The compiler vtable dump for `Gta4lEmmcConsumer` has 171 entries, the same
count as `IOService`: slots 2/3 are the complete/deleting destructor, slot 9
is the macro-generated `Gta4lEmmcConsumer::getMetaClass()`, slot 15 is
`OSObject::init()`, slot 16 is `IOService::free()`, slot 23 is
`IOService::init(OSDictionary*)`, and all other slots resolve to the pinned
`IOService`/`IORegistryEntry`/`OSObject` implementations. The class layout is
a single-inheritance `IOService` subclass with no data members; `OSObject`
has sizeof 16 and the pinned headers supply the class size and alignment.
`OSDefineMetaClassAndStructors` emits the static metaclass and its
registration; the consumer has no service personality or service
registration, but its metaclass registration, constructors and destructors
are required and are covered by the probe's symbol set. The probe is
compile-only; it does not link or run against the kernel. The implementation
batch must repeat the corrected-lifecycle probe against the corrected run
sequence, and a new candidate kernel must repeat both the export-subset check
and the layout/vtable/metaclass qualification against its own headers and
compile record.

The implementation batch must still automate the export check:
`generate_bindings()` (or an equivalent prelink gate) must compile the
consumer translation unit, enumerate its undefined symbols, and refuse the
archive if any is absent from the candidate kernel's export surface. The
generator's existing `IOMedia` import rejection remains valid because the
consumer calls `open()`/`read()` through the `IOMedia` vtable and imports no
`IOMedia` symbol. The 187-symbol subset is an additional gate, not a
substitute for the layout/vtable/metaclass qualification. No unaudited ABI
workaround is needed or permitted. `IOMallocContiguous`/`IOFreeContiguous`
are marked deprecated in the pinned headers; the pinned compile records do
not use `-Werror`, so the warnings are non-fatal. The design pins them and
does not use the non-deprecated `IOBufferMemoryDescriptor` alternative.

The sweep already opens the BOOT partition with `this` as the client even
though `this` is not attached to that media, so an unattached client is the
established pattern; the consumer changes only the client object's identity,
not the open mechanics.

### 5.2 Run sequence

The report thread performs:

1. Wait for exactly one whole media and exactly one BOOT partition at the
   pinned base/size, using the existing bounded discovery helpers and the
   existing media/partition deadlines. Open the whole media with the provider
   `this` (`whole->open(this, 0, kIOStorageAccessReader)`), as the sweep
   does, before the partition is discovered; the provider chain's
   `IOMedia::read()` checks the whole media's open level. A missing,
   duplicate, wrong-base, wrong-size or writable partition fails closed
   before any consumer read.
2. Create the consumer object with `new`, then `init()`. If allocation or
   `init()` fails, `reason=CLIENT`, no open and no read.
3. Open the BOOT partition media with
   `partition->open(consumer, 0, kIOStorageAccessReader)`. If it fails,
   `reason=OPEN`; the consumer object is released and no read occurs.
4. Allocate one 32 KiB physically contiguous buffer with
   `IOMallocContiguous(32768, PAGE_SIZE, nullptr)`; the matching release is
   `IOFreeContiguous(buffer, 32768)`. The allocation remains 32 KiB to
   preserve the pinned allocation contract; the descriptor and request are
   1,024 bytes. Do not use `IOBufferMemoryDescriptor`, a page-aligned
   `IOMallocAligned` buffer, a fragmented buffer or a retry. If allocation
   fails, `reason=ALLOC`; the partition is closed exactly once, the consumer
   is released and no read occurs.
5. Case: create a descriptor with
   `IOMemoryDescriptor::withAddress(buffer, 1024, kIODirectionIn)`.
   `withAddress()` requests `kIOMemoryAutoPrepare`, and the pinned
   `IOGeneralMemoryDescriptor::initWithAddress` calls `prepare()` when that
   flag is set; `release()` completes the preparation in `free()`. Do not
   call `prepare()` or `complete()` separately on this descriptor. Before the
   read, validate the physical run:
   `address = descriptor->getPhysicalSegment(0, &segmentLength,
   kIOMemoryMapperNone)` must return a nonzero address and
   `segmentLength >= 1,024`. A null descriptor, a zero address or a shorter
   first segment is `reason=DESC`; release the descriptor if it is non-null
   and fail closed. Before the read, the report thread publishes the active
   identity (case tag, whole-media first block, block count and exclusive end)
   under `reportLock` with `inFlight` clear; the entry observer sets
   `inFlight` only after its checks pass, and the report thread clears the
   identity after the read returns. Call `descriptor->setTag(caseTag)` with
   the distinct nonzero tag, then call
   `partition->read(consumer, 0, descriptor, nullptr, &actual)`, release the
   descriptor, then check `status == kIOReturnSuccess` and `actual ==
   1,024`, and record the 16-byte SHA-256 prefix in `digest`. Under option A,
   do not latch `HASH`; the host compares `digest` after the run. A read
   failure is `reason=READ`; a short read is `reason=SHORT`.
6. Close the BOOT partition exactly once if and only if the consumer open
   succeeded.
7. Close the whole media exactly once if it was opened, free the buffer if it
   was allocated, release the consumer object and release the partition and
   whole-media references on every path.
8. Record the counters and report.

The consumer holds the only client identity for the one BOOT read. The
provider must see the consumer's BOOT open/close pair and the request group;
the report records `open=1 close=1 client=1` on success. The
whole-media open/close uses the provider `this` and is balanced separately; it
is not counted in the report's `open`/`close`. A missed close, a double close,
a missing client release or a leaked buffer is a host-test failure and a
mutation-control target.

### 5.3 Ownership and cleanup

| Object | Owner | Created | Released | Failure paths |
| --- | --- | --- | --- | --- |
| consumer `IOService` | report thread | before open | after close/report | release on every path after creation |
| 32 KiB contiguous buffer | consumer | after open, before the case | after the read/close | free on every path after allocation |
| case descriptor | consumer | per read | immediately after read | release before any failure return |
| whole media open (provider `this`) | report thread | before partition discovery | after BOOT close/report | close on every path after open |
| BOOT partition media reference | report thread | discovery | after close/report | release on every path |
| whole media reference | report thread | discovery | after report | release on every path |

The consumer does not free the buffer in `free()`; `run()` owns the buffer
lifetime so every early return is visible in one function. The consumer object
is released only after `run()` returns.

## 6. Request table and expected decomposition

### 6.1 Why the buffer is physically contiguous

The production advertisement is 64 blocks / 32 KiB / one segment. The
consumer retains the pinned 32 KiB contiguous allocation even though the
single request is 1,024 bytes: this preserves the allocation contract already
qualified in Gate A and guarantees that the requested two-block run is
physically contiguous. The pinned storage-family source
(`IOBlockStorageDriver.cpp`, `IOBreaker::getBreakSize()`) shows the exact
mechanism: with `withMaximumSegmentCount == 1`, the scan commits the first
physical segment (folding adjacent segments whose physical addresses are
contiguous), stops at the next segment, and truncates the break size to the
request block size. A physically fragmented 32 KiB buffer therefore breaks at
its first physical run, which is at least one page but may be more than 8
blocks; a 4 KiB first segment yields an 8-block break. The consumer requires
one physically contiguous 32 KiB allocation so the first run is the whole
request. With one segment and the 32 KiB byte cap, `getBreakSize()` returns
32 KiB and `breakUpRequest()` short-circuits to `executeRequest()` with the
original descriptor; the 1,024-byte request is entirely inside that break. If
the contiguous allocation fails, the mode fails closed with `reason=ALLOC`; it
does not fall back to a fragmented buffer, a smaller cap or a retry.

The driver's `parseAdvertisement()` reads all six constraint keys from the
provider plane. Only three are advertised in consume mode; the segment byte
count and segment width default to 0, which skips those trim steps, and the
minimum segment alignment defaults to 4 bytes, which a page-aligned allocation
satisfies. The 64-block count folds into the 32 KiB byte cap, so the break
size is exactly the request length and `breakUpRequest()` takes its
`buffer->getLength() <= breakSize` short-circuit.

The consumer's read is whole-block aligned (offset 0, length 1,024), so
`deblockRequest()` also short-circuits and no `IODeblocker` is created.
Because the request is within the break size, no `IOBreaker` sub-descriptor is
created either. The `doAsyncReadWrite()` observer therefore sees the
consumer's original tagged descriptor, not a family-created copy. This is a
stronger attribution than the sweep's split subrequests and is the basis for
the `foreign=0` guard.

This is the deliberate difference from N10.11: the sweep used a 4,096-byte
mode-scoped cap to force 8-block splits; the consumer uses the production
advertisement and a contiguous descriptor for one two-block request. The
64-block exercise is deferred to a later, separately approved proposal.
N10.2 is the evidence for the contiguity requirement: its partread case 13
read 64 blocks through the same 64-block advertisement, but its page-aligned
`IOMallocAligned` scratch was not guaranteed physically contiguous; the
observed `DRV max=8` is consistent with a 4 KiB first segment, but its actual
geometry was not recorded, so the specific cause remains a hypothesis. The
contiguous allocation makes the decomposition deterministic without changing
the advertisement or the transport.

### 6.2 Expected counters

Case 0: 2 blocks = 1 two-block CMD18 chunk, 1 CMD12 stop, 0 CMD17.

| counter | expected | meaning |
| --- | ---: | --- |
| `cases` | 1 | fixed consumer case |
| `passed` | 1 | the content check passed on the host |
| `failed` | 0 | no failed case |
| `reason` | 0 (`NONE`) | no latched reason |
| `complete` | 1 | the exact guard below |
| `reads` | 1 | one consumer `IOMedia::read()` call |
| `bytes` | 1,024 | the single two-block window |
| `span` | 1,024 | aligned, no deblocker padding |
| `blocks` | 2 | the single window |
| `subrequests` | 1 | one driver request for the contiguous consumer read |
| `max` | 2 | largest driver request, in blocks |
| `foreign` | 0 | no unattributed request inside the active window |
| `fails` | 0 | no driver request failure |
| `cancel` | 0 | no cancellation |
| `budget` | 0 | no budget expiry; consume mode has one case |
| `media` | 0 | whole-media profile correct |
| `part` | 0 | exactly one correct BOOT partition |
| `open` | 1 | consumer open succeeded |
| `close` | 1 | consumer close called exactly once |
| `alloc` | 0 | contiguous buffer allocation succeeded |
| `client` | 1 | one distinct consumer client object |
| `mismatches` | 0 | the observed digest matched under option B; recorded for the host check under option A |
| `limit` | 64 | production block advertisement |
| `seg` | 1 | production segment advertisement |
| `cap` | 61,071,360 | whole-media capacity in blocks |

The consumer's single read must contribute exactly 2 blocks, one two-block
CMD18 chunk and one stop, with no CMD17, no odd tail and `max=2` to the
transport. The `CONSUME` line reports the driver-level `subrequests`/`max`;
the observer's internal credited `c18`/`c18b`/`stops` deltas are asserted by
the host seam and are not separate report fields. These are
observer-attributed counts, not whole-boot totals: the whole-boot `DRV`/`XFER`
lines also include the partition scheme's GPT scan and any prelinked
filesystem probe. N10.14 shows the effect (`reads=22 blocks=133`
whole-boot against `reads=6 blocks=104` attributed), so the host scenarios
assert the consumer-attributed counts and require the whole-boot lines to
contain them, not to equal them.

The reference is the external host-side digest from the unchanged candidate
image, not a same-boot CMD17 read, so there is no reference traffic and no
`refs` field. The digest is a content-level external reference, not an
independent transport reference; the transport evidence is the exact
completion, stop and content counts. Under option A the consumer does not
fail closed on content; the host compares the printed digest after the run.

Incidental stack reads (the partition scheme's GPT scan and any prelinked
filesystem probe) appear in the whole-boot `DRV`/`XFER` aggregates but are not
credited to `CONSUME`. The GPT scan targets whole-media LBAs 0..33, outside
the BOOT partition, and normally completes before the consumer opens. A stack
read that arrives while a consumer case is active and is not the case's
exact tagged request (matching tag, first block and block count) sets
`foreign` and fails the guard; a tag-0 request is foreign even when it
overlaps the active window. That is deliberate fail-closed attribution, not
a pass criterion to be relaxed.

### 6.3 The last-case line

The report emits one `CONSUMEREAD` line for the single case:

```
CONSUMEREAD: index=0 target=1 start=0 length=1024 span=1024
actual=1024 status=0 subrequests=1 blocks=2 max=2
digest=5a25ed5ef1d90de9163f2f1be3bed9b2
expected=00000000000000000000000000000000 mismatch=0
```

Under option A, the observed prefix is printed, `expected` is 32 zeros and
`mismatch=0`, and the host compares `digest` with the external record after
the run; the guard does not include a compiled digest. Under option B (not
selected here), the guard would compare the prefix with a compiled expected
value and `mismatches=0` would cover it. If the case fails, the `CONSUMEREAD`
line is replaced by the first-failure detail and `complete=0`; the failure
line carries the observed digest.

## 7. Bounds, allocation, cancellation and timing

### 7.1 Bounds

- `CONSUME_CASES = 1`; `CONSUME_CASE_BLOCKS = 2`; `CONSUME_CASE_BYTES =
  1,024`; `CONSUME_MAX_BLOCKS = 64`; `CONSUME_MAX_BYTES = 32,768`;
  `CONSUME_TOTAL_BYTES = 1,024`; `CONSUME_TOTAL_BLOCKS = 2`.
- The offset and length are whole 512-byte blocks; the byte-to-sector
  conversion is checked for overflow and the end offset is checked against
  the BOOT partition size before the read.
- The window is checked against the retained BOOT partition size before the
  read: base + 1,024 must not exceed base + size. The final packaged image
  must contain at least the 1,024-byte window; the external reference is
  bound to the final packaged image. A shorter image fails closed before the
  read.
- The consumer never issues a request larger than 64 blocks or 32,768 bytes;
  the driver's defensive `kIOReturnMessageTooLarge` path remains.
- No request crosses the BOOT partition end; no request targets the whole
  media, the GPT, another partition or free space.

### 7.2 Allocation

- The consumer object allocation or `init()` failure is `reason=CLIENT`.
- The 32 KiB contiguous buffer allocation failure is `reason=ALLOC`.
- The descriptor covers the 1,024-byte request; the 32 KiB buffer is
  retained to preserve the pinned allocation contract.
- The descriptor allocation or one-segment validation failure is
  `reason=DESC`.
- No allocation is retried; no smaller buffer, fragmented buffer or
  alternative descriptor is substituted. The mode fails closed and reports
  the first allocation failure.

### 7.3 Cancellation

- The report thread checks the existing `reportCancel` flag before creating
  the consumer, before the open, before the case and before the close. A
  cancellation after the open closes exactly once and reports
  `cancel=1 complete=0`.
- Synchronous `IOMedia::read()` has no in-flight cancellation. The design does
  not pretend otherwise: a read already inside the adapter is bounded only by
  the existing per-command and per-chunk stop timeouts, not by the cancel
  flag; the report watchdog may fire before the synchronous read returns, so
  it is not a read bound. The cancel flag is a between-request stop, not an
  asynchronous abort.

### 7.4 Timing (reduced scope, conditional margin)

This revision adopts the owner-approved reduced scope: one two-block
(1,024-byte) read. The core issues one two-block CMD18 chunk and one mandatory
CMD12 stop. The pinned per-chunk terms are: data poll up to 1,000 ms,
CMD12 entry-inhibit wait up to 1,000 ms and CMD12 completion poll up to
1,000 ms, for 3,000 ms. The discovery terms are the existing 20,000 ms media
wait and 2,000 ms partition deadline, for 22,000 ms. The sum is 25,000 ms,
leaving a 5,000 ms margin below the 30,000 ms
`GTA4L_EMMC_BLOCK_WATCHDOG_MS`.

This is not a complete path-condition proof. The 5,000 ms margin must also
cover discovery step granularity, allocation, gate contention, scheduling and
terminal ownership. The 32 KiB allocation has no explicit timeout; it is a
conditional assumption. The consumer's synchronous read may wait behind an
incidental stack request that holds the controller gate; the design does not
establish a finite bound for that wait. The report watchdog can fire before
the read returns; that is a no-go, not a pass, and the watchdog path does not
free or reuse the in-flight buffer or descriptor.

The design therefore claims only a conditional host-test timing argument, not
a hardware timing pass. The owner approved host-only testing of this reduced
scope. A candidate build or hardware run requires a complete bound or an
explicit owner acceptance of the conditional margin. The watchdog, CMD/data
timeouts and chunk size are not extended. The 28,500 ms figure and the
108,000/130,000 ms sum for the earlier 64-block design are withdrawn and do
not apply to this revision.

## 8. Report and diagnostics

### 8.1 Lines

In consume mode the legacy `READ`, `QUAL` and `QUAL3` lines are suppressed,
not zeroed. Retaining them even with zero counters leaves a cap-width report
at 2,338 bytes, and retaining their full-width values leaves 2,637 bytes
(with the device's three `PART` lines); either way the writer would drop the
whole report. Every other legacy line is emitted unchanged, and every
non-consume mode stays byte-for-byte identical.

The success report adds exactly two lines: one `CONSUME` line and one
`CONSUMEREAD` line for the single case. The failure report adds one `CONSUME`
line and one `CONSUMEFAIL` first-failure detail line instead of
`CONSUMEREAD`. The `CONSUMEREAD` line carries the observed digest. The
design-bound maxima, computed from the design's fixed counts, fixed
1,024-byte window and 32-hex digests, are at most 242 bytes for `CONSUME`,
213 bytes for `CONSUMEREAD` and 236 bytes for `CONSUMEFAIL`, excluding the
newline. The formatter caps are 320, 352 and 384 bytes excluding the newline.
A line that would exceed its cap is a formatter failure: the report is
refused (the existing writer already empties the buffer on overflow) and the
host test fails; it is never truncated.

With the device's three `PART` lines (`GTA4L_EMMC_BLOCK_PART_REPORT = 3`),
the longest accepted partition content, the three suppressed legacy lines
and cap-width consume lines, the worst report is the first-failure report at
1,959 content bytes including newlines (1,960 with the terminating NUL); the
success report is 1,927 (1,928 with NUL). Using the design-bound maxima
instead of the caps, the worst is 1,733 content bytes (1,734 with NUL). Both
fit the 2,048-byte `GTA4L_EMMC_BLOCK_REPORT_BYTES` cap. A static assert must
pin the cap-width bound `CONSUME_REPORT_WORST_BYTES <=
GTA4L_EMMC_BLOCK_REPORT_BYTES`, plus a per-line assert for each of the three
caps. The host suite must render complete success, first-failure, early-exit
and watchdog reports at maximum width; the maxima above are design-bound
field sums, so the renderer is the width proof. A width mutation must fail
the host test.

```
CONSUME: cases=1 passed=1 failed=0 reason=0 complete=1 reads=1 bytes=1024
span=1024 blocks=2 subrequests=1 max=2 foreign=0 fails=0 cancel=0 budget=0
media=0 part=0 open=1 close=1 alloc=0 client=1 mismatches=0 limit=64 seg=1
cap=61071360
CONSUMEREAD: index=0 target=1 start=0 length=1024 span=1024
actual=1024 status=0 subrequests=1 blocks=2 max=2
digest=5a25ed5ef1d90de9163f2f1be3bed9b2
expected=00000000000000000000000000000000 mismatch=0
```

The example shows the option A fields; under option B, `expected` would be
the compiled reference and `mismatch=0` would be guard-checked. Under option
A, the host compares `digest` with the external record after the run.

The existing header, `DRV`, `XFER`, `MEDIA`, `PART`, `SNAP` and `FAIL` lines
remain. The legacy `READ`/`QUAL`/`QUAL3` lines are the only suppressed lines;
they are absent in consume mode and byte-identical in every other mode.
Line-byte constants and static asserts must bound the two success lines, the
failure line and the total report; a width mutation must fail the host test.

### 8.2 Failure reasons

| value | name | first-failure condition |
| ---: | --- | --- |
| 0 | `NONE` | no failure |
| 1 | `MEDIA` | whole-media selection or profile failure |
| 2 | `PART` | BOOT partition missing, duplicate, wrong base/size or writable |
| 3 | `OPEN` | whole-media provider open or consumer BOOT open refused |
| 4 | `ALLOC` | contiguous buffer allocation failed |
| 5 | `DESC` | descriptor allocation or one-segment validation failed |
| 6 | `READ` | `IOMedia::read()` returned a non-success status |
| 7 | `SHORT` | actual bytes differ from the requested length |
| 8 | `HASH` | digest prefix differs from the pinned value (§11.3 option B only; option A compares on the host) |
| 9 | `BUDGET` | read budget expired before a case |
| 10 | `CANCEL` | cancellation observed between requests |
| 11 | `ARG` | mode, token or fixed-range argument invalid |
| 12 | `ORDER` | case order broken, or duplicate/concurrent matching request while in flight |
| 13 | `FOREIGN` | active-window request with wrong tag, first block, block count or range |
| 14 | `CLIENT` | consumer object allocation or client-identity failure |
| 15 | `CLOSE` | close accounting mismatch (host guard) |

The report latches the first failure and suppresses suffix I/O; no later
observation overwrites the first reason or the first status. The `CONSUMEFAIL`
line carries exactly: reason, case, target, start, length, span, requested,
completed, actual, status, digest, expected and mismatch. That field set is
the basis of the 236-byte design maximum; adding a field requires a new
width proof and static assert. Fields that do not apply are printed as zero,
never omitted, so the line width is deterministic.

### 8.3 Complete guard

`complete=1` requires all of:

- `cases == 1`, `passed == 1`, `failed == 0`, `reason == NONE`;
- `reads == 1`, `bytes == 1,024`, `span == 1,024`, `blocks == 2`;
- `subrequests == 1`, `max == 2`;
- `foreign == 0`, `fails == 0`, `cancel == 0`, `budget == 0`;
- `media == 0`, `part == 0`;
- `open == 1`, `close == 1`, `alloc == 0`, `client == 1`;
- `limit == 64`, `seg == 1`;
- under §11.3 option A (the selected option), the guard does not include
  content and the host compares the recorded `digest` after the run. Under
  option B (not selected), the compiled window digest would also need
  `mismatches == 0`.

The guard cannot coexist with a read failure, short transfer, wrong offset,
wrong partition, open/close mismatch, allocation failure, cancellation,
budget refusal, foreign traffic or a wrong advertisement. A host-side digest
mismatch fails the acceptance run without changing the on-device `complete`
field, because the comparison happens after the report is transcribed.

### 8.4 Terminal behavior

The report thread ends with the intentional `panic("%s", reportBuffer)` in
`panicWithReport()`, as every diagnostic mode does. The panic location in the
candidate source is the normal terminal report stop, not a fault. The reduced
scope has a conditional 25,000 ms argument with a 5,000 ms margin under the
30,000 ms watchdog (§7.4); that margin also has to cover allocation, gate
contention and scheduling, so a watchdog fire remains possible and is a
no-go, not a pass. A watchdog fire must not be reported as a completed case;
the watchdog path does not free or reuse the in-flight buffer or descriptor.
The userspace `fs-timer-hold` fixture keeps the screen visible long enough
for the owner to transcribe the report. The terminal ownership and watchdog
budget must be host-tested.

## 9. Acceptance and negative controls

### 9.1 Acceptance

The implementation is accepted host-side only if:

1. The mode selector refuses every orphan, duplicate, non-1 and conflicting
   token before MMIO, and accepts exactly `gta4l-emmc-consume=1` plus
   `gta4l-emmc-block=3`.
2. The production advertisement is 64 / 32,768 / 1 and the report carries
   `limit=64 seg=1`.
3. The consumer is a distinct `IOService` client; the provider sees one
   consumer BOOT open and one close, the whole-media provider open/close is
   balanced, and the consumer object is released.
4. The 32 KiB contiguous buffer and the one case descriptor are allocated and
   released on every path; no leak and no double free.
5. The single case returns exactly 1,024 bytes. Under §11.3 option A the
   consumer records the observed prefix in `digest`, and the host fixture
   compares it with the pre-run external reference bound to the final
   packaged image.
6. The report fields `reads=1 blocks=2 max=2` and the observer's internal
   credited `c18=1 c18b=2 stops=1` deltas are modeled and observed; the
   whole-boot `DRV`/`XFER` lines are checked to contain at least those counts
   while also carrying incidental traffic.
7. The `complete=1` guard rejects every mutation below.
8. The reduced-scope timing argument in §7.4 is stated as conditional, not a
   complete proof. No implementation may claim a hardware timing pass under
   the withdrawn 28,500 ms figure or the withdrawn 108,000/130,000 ms sum.
9. Consume mode suppresses `READ`/`QUAL`/`QUAL3`, each new line stays within
   its 320/352/384-byte cap, and the complete worst-case report fits the
   2,048-byte cap with static asserts; an overflow refuses completion.
10. The host seam models the independent client, the contiguous descriptor,
    the partition media and the production advertisement.
11. The consumer translation unit's undefined-symbol set is a subset of the
    pinned candidate kernel's exports; the build gate refuses any missing
    symbol.
12. The §11.3 option A external reference is bound to the final packaged
    image before the run: the full image SHA-256 and the first 1,024-byte
    window prefix. A compiled-in expected digest is not permitted. The window
    is inside the BOOT partition and inside the image.

### 9.2 Host scenarios

| Scenario | Expected |
| --- | --- |
| `consume-pass` | full `complete=1`; option A records the prefix for host comparison |
| `consume-media-missing` | `media=1`, no read |
| `consume-part-missing` | `part=1`, no read |
| `consume-part-duplicate` | `part=1`, no read |
| `consume-part-wrong-base` | `part=1`, no read |
| `consume-part-wrong-size` | `part=1`, no read |
| `consume-part-writable` | `part=1`, no read |
| `consume-client-alloc-fail` | `reason=CLIENT`, no open |
| `consume-buffer-alloc-fail` | `reason=ALLOC`, no read |
| `consume-desc-alloc-fail` | `reason=DESC`, no read |
| `consume-desc-fragmented` | `reason=DESC`, no read |
| `consume-open-refused` | `open=0`, no read |
| `consume-read-error` | first `READ` status, no suffix |
| `consume-short` | first `SHORT`, no suffix |
| `consume-digest-host-mismatch` | on-device `complete=1`; host comparison fails the acceptance run |
| `consume-cancel-before-open` | `cancel=1`, no open |
| `consume-cancel-after-open` | `cancel=1`, close once |
| `consume-budget` | `budget=1`, no read |
| `consume-foreign-window` | `foreign=1`, `complete=0` |
| `consume-tag-missing` | `foreign=1`, `complete=0` |
| `consume-tag-wrong` | `foreign=1`, `complete=0` |
| `consume-tag-duplicate` | `reason=ORDER`, `complete=0` |
| `consume-order` | `reason=ORDER`, `complete=0` |
| `consume-close-missed` | host guard fails, `complete=0` |
| `consume-client-release-missed` | host guard fails, `complete=0` |
| `consume-wrong-advertisement` | `limit`/`seg` mismatch, `complete=0` |
| `consume-report-width` | width mutation fails the static assert/test |
| `consume-report-suppress-missing` | `READ`/`QUAL`/`QUAL3` present, width guard fails |
| `consume-report-line-cap` | per-line overflow refused, no report |
| `legacy-modes-unchanged` | byte-identical reports |
| `selector-orphan` | refused |
| `selector-duplicate` | refused |
| `selector-non-1` | refused |
| `selector-block-1` | refused |
| `selector-conflict-*` | refused for every other mode |

### 9.3 Mutation controls

The host suite must include mutations that:

- drop the open, drop the close, double-close or skip the client release;
- skip the contiguous allocation check or substitute a fragmented buffer;
- skip the `getPhysicalSegment` one-segment check, or validate the address
  but not the 1,024-byte segment length;
- omit `setTag`, set tag 0, set the tag after the read, or reuse a tag from
  another case;
- skip the descriptor release;
- omit the digest from the report or swap the expected digest (option A host
  comparison);
- accept a short read;
- change the fixed offset or length;
- skip the partition base/size/writable checks;
- skip the cancellation checks, the budget check or the foreign attribution;
- skip the `complete` guard terms (`reads`, `bytes`, `blocks`, `subrequests`,
  `max`, `limit`, `seg`, `open`, `close`, `alloc`, `client`);
- advertise the sweep 8-block cap or the deblock three-segment cap;
- retain `READ`/`QUAL`/`QUAL3` in consume mode, emit the consume lines in
  legacy mode, or change any legacy-mode byte;
- exceed a per-line cap or the total report width, or truncate instead of
  refusing the report.

Every mutation must fail the expected scenario, not merely produce a different
line. The suite must preserve the existing core/probe/adapter counts and add
the new scenarios explicitly.

## 10. Privacy

The BOOT partition contains the project's public diagnostic image. The
consumer reads only that partition and prints only counters, offsets, lengths,
statuses and truncated digests. It never prints raw sectors, GPT names, GPT
GUIDs, volume UUIDs, disk serials, device identifiers or file contents. The
GPT geometry (base and size) is already documented public layout, not a
private identifier. The report does not print the whole-media content or any
other partition's content. The owner must confirm before any hardware run that
the BOOT partition still contains the project's public diagnostic image; if it
does not, the run is not approved. Raw evidence stays in the gitignored
`out/gta4l/...` tree; the report prints truncated digests, while this design
records full SHA-256 values for the public diagnostic image.

## 11. Feasibility gates and no-go

The implementation must stop and return a no-go if any of these fails:

1. **ABI/bindings gate — closed for the pinned candidate with a repeat
   required for the one-case probe.** A corrected full-lifecycle probe
   (`new`, `init()`, `open`, one synchronous `setTag` +
   `getPhysicalSegment` read, `close`, `release`, `IOMallocContiguous`,
   `IOFreeContiguous`) compiled with the adapter's pinned
   `IOPlatformExpert.cpo.json` record plus `-I build/storage-abi/include` and
   `-I package` has the same import surface as the measured two-read probe:
   187 undefined symbols, all matching defined global symbols in the
   unchanged N10.13 candidate kernel (185 in `__TEXT_EXEC,__text`, one in
   `__DATA_CONST,__const`, one in `__DATA,__common`; `llvm-nm` labels them
   `S`). The generator still binds only `IOBlockStorageDevice` imports and
   rejects `IOMedia` imports; the consumer imports no `IOMedia` symbol, and
   the `IOService`/`OSObject`/`IOMemoryDescriptor` imports resolve from the
   kernel export table. The implementation batch must repeat the probe with
   the single-read lifecycle and automate the subset check as an additional
   gate, not a substitute for the §5.1 layout/vtable/metaclass
   qualification. A new candidate kernel must repeat both.
2. **Contiguous allocation gate — closed for the pinned candidate.**
   `IOMallocContiguous(32768, PAGE_SIZE, nullptr)` / `IOFreeContiguous` are
   the only pinned API and release path; `IOBufferMemoryDescriptor` is not
   part of this design. They are exported (deprecated warnings only, and the
   pinned records do not use `-Werror`). The pinned
   `IOBreaker::getBreakSize()` source shows that a physically contiguous
   32 KiB descriptor under the 64/32K/1 advertisement returns a 32 KiB break
   size and short-circuits to `executeRequest()`; the 1,024-byte request is
   entirely inside that break. The consumer validates the one-segment run
   with `getPhysicalSegment(..., kIOMemoryMapperNone)` and a segment length
   of at least 1,024 bytes, and fails closed with `DESC` otherwise. A
   fragmented fallback is a different design with a different expected
   decomposition.
3. **Host seam gate.** The seam must model a distinct `IOService` client,
   the partition-media open/close, the contiguous one-segment descriptor,
   the case tag and active identity, and the production advertisement.
   The current seam already models `IOService`, `IOMedia`, `open`/`close`/
   `read`, tags and the metaclass macros; the implementation batch extends it
   as listed in §11.1. If the seam cannot model them, the batch stops.
4. **Timing gate — conditional under the reduced scope.** The owner-approved
   reduced scope is one two-block read. The conditional argument is
   3,000 ms of per-chunk CMD18/CMD12 timeouts plus 22,000 ms of media wait
   and partition deadline, for 25,000 ms, leaving 5,000 ms under the
   30,000 ms watchdog (§7.4). It is not a complete path-condition proof: the
   margin also has to cover allocation, gate contention and scheduling. The
   withdrawn 28,500 ms figure and the 108,000/130,000 ms sum do not apply.
   Host-only testing is approved; candidate production and hardware require
   either a complete bound or explicit owner acceptance of the conditional
   margin.
5. **Report gate — closed with mode-inactive suppression.** Consume mode
   suppresses `READ`/`QUAL`/`QUAL3`; the `CONSUME`/`CONSUMEREAD`/
   `CONSUMEFAIL` design maxima are 242/213/236 bytes against caps of
   320/352/384, and the worst report is the first-failure report at 1,959
   content bytes including newlines (1,960 with NUL) inside the 2,048-byte
   cap. Per-line and total static asserts plus maximum-width renderings and
   width mutations are required; overflow refuses the report.
6. **Content gate — option A selected; candidate binding open.** The owner
   selected external host-side comparison. The full image SHA-256
   `b626a1fa52a559e274d7291243c9bb959947365c168b6b728265629b3f752aeb`
   and the first-1,024-byte window prefix
   `5a25ed5ef1d90de9163f2f1be3bed9b2` are reference evidence for the
   unchanged N10.13 image. The consumer prints the observed prefix; no
   expected digest is compiled in. The reference must be rebound to the
   final packaged image before any candidate run. Under option A the
   `complete=1` guard covers transport, attribution, open/close and
   allocation only; content equality is a host-side post-run check.
7. **Scope gate.** The consumer must not need a write, a mount, a root switch,
   a wider/faster mode, DMA/CQE/ICE or another partition. If it does, stop at
   a new proposal; do not include it implicitly.

If a gate fails, the batch reports the no-go and does not implement the
fallback production-stack observation or the BSD/userland consumer without a
new design and explicit owner approval.

### 11.1 Implementation surface (separately approved)

The implementation batch, if approved, is expected to touch:

- `package/Gta4lEmmcBlock.h` — consume constants, reason codes, report
  counters, line caps and the worst-case static assert; no `IOService` token
  (the policy header's forbidden-token gate).
- `package/Gta4lEmmcBlock.c` — `gta4l-emmc-consume` token parsing and
  conflict rules, the fixed one-window table and the report formatter.
- `package/Gta4lEmmcBlock.cpp` — the minimal `Gta4lEmmcConsumer` class, the
  run sequence, the consume observer and the report-thread selection; the
  existing data path and the sweep/breakup/deblock paths stay unchanged.
- `scripts/sim-gta4l-emmc-block/Gta4lEmmcBlockTestSupport.h` and the adapter
  harness — the seam already models `IOService`, `IOMedia`,
  `open`/`close`/`read`, tags and the metaclass macros; add
  `IOMallocContiguous`/`IOFreeContiguous` (no `IOBufferMemoryDescriptor`
  model), a distinct client identity, the contiguous one-segment descriptor
  with `getPhysicalSegment` validation, the case `setTag` and
  active-identity model, and the direct `executeRequest()` path where the
  current seam is insufficient.
- `scripts/sim-gta4l-emmc-block/consumer_lifecycle_probe.cpp` — the
  compile-only corrected-lifecycle probe repeated against the one-read run
  sequence; 187 undefined imports, all candidate-kernel exports, split
  185/1/1 across `__TEXT_EXEC,__text`/`__DATA_CONST,__const`/
  `__DATA,__common`.
- `scripts/test-gta4l-emmc-block.py` — the new scenarios and mutations,
  preserving the existing core/probe/adapter counts.
- `scripts/build-gta4l-emmc-block.py` — extend `generate_bindings()` to
  compile the consumer translation unit with the pinned record plus
  `-I build/storage-abi/include -I package`, enumerate its undefined symbols
  and refuse the archive if any is absent from the candidate kernel's export
  surface. The generator currently binds only `IOBlockStorageDevice` imports
  and rejects `IOMedia` imports; the consumer imports no `IOMedia` symbol,
  and the unchanged N10.13 kernel exports all 187 consumer imports. The
  automated subset check is additional to the §5.1 layout/vtable/metaclass
  qualification and must not replace it.
- `scripts/build-gta4l-diagnostic-payload.py`,
  `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py` —
  one `emmc-block-3-consume` personality that emits `gta4l-emmc-block=3` and
  `gta4l-emmc-consume=1`, refuses orphan/duplicate/non-1/conflicting tokens,
  and requires the `fs-timer-hold` fixture.
- `docs/diagnostic-payload-input-inventory.md` — the new personality/token
  and its contract.

`package/Gta4lEmmcBlockCore.c/.h` should not need a change; if the shared
request helper must change, the core/probe counts and the D13 chunk contract
are re-qualified rather than assumed.

### 11.2 Corrective Gate A status (2026-11-10)

This corrective pass was host-only: no device access, boot, flash, candidate
build, driver change, QEMU run, commit or push. It re-verified the plan's six
gaps against pinned source, headers and image evidence, corrected this
design, and recorded the owner's reduced-scope decision: one 1,024-byte
window, external host-side comparison. The earlier two-window timing no-go
and content-binding problem are resolved by that scope reduction. Gate A
remains open for candidate production and hardware because the timing
argument is conditional; the host-only implementation batch may proceed
under the owner's testing approval. The implementation batch must still
repeat the ABI probe with the one-read lifecycle and build the host seam.

| gap | result |
| --- | --- |
| 1 Allocation | Closed. One API and release path: `IOMallocContiguous(32768, PAGE_SIZE, nullptr)` / `IOFreeContiguous`; `PAGE_SIZE` alignment; 32 KiB buffer with a 1,024-byte descriptor; `getPhysicalSegment` one-segment, at-least-1,024-byte check with `DESC` fail-closed; descriptor lifetime; every cleanup path in §5.2/§5.3. |
| 2 Attribution | Closed. Nonzero `setTag`; active identity published under `reportLock`; exact tag/start/count/range and duplicate checks; tag-0 in-window requests are foreign; private `block.counters` deltas under `gate`; observer-only data path. |
| 3 Timing | Reduced scope: one two-block read; conditional 25,000 ms against the 30,000 ms watchdog with a 5,000 ms margin that must also cover allocation, gate contention and scheduling (§7.4). The 28,500 ms figure and the 108,000/130,000 ms sum are withdrawn. |
| 4 Content binding | Option A selected: the consumer prints the observed prefix; the host compares it with an external record bound to the final packaged image. No compiled-in expected digest. The reference must be rebound per candidate image. |
| 5 ABI | Closed with qualification. 187/187 exports measured on the pinned two-read probe; the one-read probe has the same import surface and is repeated in the implementation batch; 185 `__TEXT_EXEC,__text`, one `__DATA_CONST,__const`, one `__DATA,__common`; 171-entry vtable with slots 2/3, 9, 15, 16, 23 checked; metaclass/ctor/dtor/lifecycle in §5.1. The automated export subset is additional, not a substitute. |
| 6 Report bounds | Closed with suppression. `READ`/`QUAL`/`QUAL3` suppressed in consume mode; design maxima 242/213/236 against caps 320/352/384; worst report 1,959 content bytes incl newlines (1,960 with NUL) < 2,048; per-line and total static asserts plus maximum-width renderings; overflow refuses the report. |
| Consumer/evidence boundary | Survives unchanged. The consumer is an independent report-thread client, not a filesystem/BSD consumer or a mount result. |
| Scope | No write, mount, root switch, wider/faster mode, DMA/CQE/ICE or other partition is required. |

Gate A does not authorize a candidate build or a hardware run. The
conditional timing margin must be either proved or explicitly accepted
before either.

### 11.3 Content binding — option A selected

The reference-image digest in §3.1 proves only the unchanged N10.13 image.
The first 1,024 bytes contain the boot header SHA-1 at offsets 576-595,
which covers the kernel section and the ramdisk; the XNU kernel and any
embedded expected digest are in the ramdisk. A compiled-in expected digest
for this window would therefore change the bytes it is supposed to match.
The owner selected:

A. **External reference binding.** The consumer prints the observed window
   digest and status only; the expected value lives in a separately bound
   production record or host fixture. The record is checked against the
   final packaged image before the run, and the on-device report is compared
   with it after the run. No expected digest is compiled into the kernel.
   The in-boot consumer therefore cannot fail closed on content; the content
   check is a pre-run host gate plus a post-run comparison. The
   `complete=1` guard covers transport, attribution, open/close and
   allocation only.

Option B (invariant-window revision) is not selected and is retained only as
history. The window references are bound before the run: the final packaged
image SHA-256 and its first-1,024-byte prefix. Until that binding is made,
the digest in §3.1 is reference-image evidence only and no content pass may
be claimed for a candidate.

## 12. What this does not establish

- One owner-reported cold boot would be one observation, not a reliability or
  repeatability result. A failure rate is not established.
- The consumer reads one fixed BOOT window. It does not qualify arbitrary
  byte offsets, arbitrary lengths, arbitrary I/O, whole-media reads, other
  partitions, filesystem mount, root integration or normal data writes.
- It does not qualify wider or faster modes, 8-bit, HS200/HS400, DDR, DMA,
  CQE, ICE or interrupt-driven operation.
- It does not upgrade N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10 comparator
  result, the N9.11 owner-reported series, the N10.1/N10.2 partread results,
  the N10.6 deblock result or the N10.10 breakup result, and it does not
  extend the N10.14 sweep result beyond its six ranges. Initialization
  reliability remains `[U]`.
- The host model is a reconstruction of the IOKit client/provider path; a
  hardware run qualifies the real family code and transport for the single
  window, not the host model's status.
- The two-block request is not a throughput or timing measurement. The
  400 kHz, polling, two-block-chunk transport still bounds performance.
- The design does not establish a complete worst-case synchronous-read bound
  under the current two-block chunking. The 25,000 ms argument is conditional
  and leaves a 5,000 ms margin that must also cover allocation, gate
  contention and scheduling.
- The content digest proves only the unchanged N10.13 image. Option A makes
  content equality a host-side comparison; a candidate run must rebind the
  reference to the final packaged image.

## 13. Gating and next steps

This design authorizes nothing beyond this document. The owner approved the
reduced scope and host-only testing. Gate A remains open for candidate
production and hardware: the allocation, attribution, ABI and report-width
gaps are closed, the timing argument is conditional (§7.4), and the content
binding is option A (§11.3). The pinned N10.13 kernel exports all 187
consumer imports, the pinned storage-family source confirms the one-segment
break behavior, and the window digest re-verifies against the unchanged
`b626a1fa…` image; those are necessary, not sufficient. This revision
performed no device access, candidate build, commit or push. The host-only
implementation batch and host tests may proceed. The owner may:

1. Review the host-only implementation and test results, then decide whether
   to prove the conditional timing margin or explicitly accept it, and
   authorize candidate production with a fresh Gate A review.
2. Prefer the production-stack observation or the BSD/userland consumer and
   ask for that design instead.
3. Prefer M3's 4-bit SDR publication readiness audit; it remains independent
   and is not a prerequisite for this consumer.
4. Prefer the optional unchanged-image repeatability series; it needs its own
   protocol and finite boot budget.

Candidate production and any hardware run remain separate batches, each with
its own explicit owner approval: candidate-specific
archive/prelink/audit/payload/BOOT production with fresh gates; then one
explicitly scoped BOOT-only hardware run with a finite one-boot budget and a
TWRP return. No retry, reflash, rollback, reset/repair, mount, write or
further device access is authorized by this design. The one-boot budget is
consumed only by the later approved run, not by this document.

## 14. References

- `docs/emmc-n10.11-bounded-read-sweep-design-2026-11-09.md` — the bounded
  sweep design, case table, observer attribution and fail-closed guard this
  design follows.
- `docs/emmc-n10.13-sweep-candidate-2026-11-09.md` — the unchanged candidate
  image `b626a1fa…` and its production chain.
- `docs/emmc-n10.14-sweep-hardware-run-2026-11-10.md` — the one-boot sweep
  result and the boundary of what it establishes.
- `docs/gta4l-storage-publication-design.md` — the F5 publication survey,
  the prelinked `IOGUIDPartitionScheme`/`IOMediaBSDClient`/APFS facts and the
  mount boundary.
- `docs/emmc-driver.md` §5.3.85–§5.3.87 — the sweep implementation, candidate
  and hardware records.
- `package/Gta4lEmmcBlock.cpp`, `package/Gta4lEmmcBlock.h`,
  `package/Gta4lEmmcBlockCore.c/.h` — the current provider, report, selector,
  advertisement, descriptor and chunk paths.
- The corrected post-N10.10 roadmap at the top of
  `gta4l-next-stage-emmc-full-ladder.md` in the plan directory — sequencing
  authority; this document is a proposal, not a roadmap milestone.
