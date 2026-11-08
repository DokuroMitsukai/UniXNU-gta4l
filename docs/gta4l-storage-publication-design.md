# gta4l storage publication design (F5)

Status: design, survey and implementation record, with a measured device
result for the corrected P5-2 candidate `[S]`/`[C]`. The P5-1 adapter and its
host gates are implemented; the D9.4 correction fixed the report-client
lifecycle, and the p5b BOOT-only device run published the whole `IOMedia` and
matched the same-boot media-path read, so F5 is measured PASS for that boot at
1-bit SDR, 400 kHz `[C]`. The boot ordinal and cold/warm class were not
supplied; the owner confirmed on 2026-11-05 that the tablet returned to
TWRP. A host-only audit matched the reported partition count to the
validated 80-entry GPT (section 5), so partition completeness is not
claimed from the report alone; other widths/timings, mount/root switch and
F6 remain unqualified. The separate F2 CMD18/CMD12 diagnostic is measured
PASS for one reported cold boot (`docs/emmc-driver.md` §5.3.41), but this
publication rung still uses CMD17 and does not combine the two results. The
host-only D13 readiness record (`docs/emmc-d13-readiness-2026-11-06.md`,
section 9.2 below) grounds the next contract and does not change these
verdicts. The follow-up N3 host batch implements a mode-3 CMD18 chunk path
behind `gta4l-emmc-block=3`; it is host-only and does not change the measured
p5b/F5 result or the F2 verdict.

This note answers the Stage-C/F5 questions in the ladder plan: what the XNU
snapshot already provides for block-device stacks, what the port's shared
SM6115 resources have, what the smallest read-only publication path is, what
the mount question means here, and what can go wrong. It deliberately keeps
the first publication rung on the already-measured read path: 1-bit SDR at
400 kHz, CMD17 single-block reads, polling PIO, no DMA, no interrupts, no
CQE/ICE, no width or timing change and no write.

## 1. What F5 must show

The ladder's F5 definition is: the XNU-side storage object for the device is
published (IOMedia-style object exists for the whole device and, ideally, per
partition), observable in a report. A mount is not required, and no
filesystem content is changed.

Concretely, the first rung should produce a report that shows:

1. an `IOMedia` object for the whole eMMC with the right block size and block
   count, `Whole = true` and the media marked non-writable;
2. one known-block read performed through the published `IOMedia` path whose
   content matches a same-boot baseline;
3. no write, no mount and no root switch;
4. as an additional observation, the GPT partition media created by the
   prelinked `IOGUIDPartitionScheme`, with a bounded summary of their
   base/size/content.

The whole-device object and the media-path read are the hard requirement.
Per-partition objects are the "ideally" part; if the GUID scheme does not
attach, F5 is still met by the whole-device publication, and the report
records partition discovery as a separate result rather than calling F5
partial.

## 2. What the snapshot already provides

### 2.1 XNU storage family is already prelinked

The kernel build's prelink roots include
`com.apple.iokit.IOStorageFamily` (`profiles/os/ipados-17.7.11-21H461/
profile.toml`). The prelinked bundle's Info.plist, visible in the kernel
image's prelink metadata, identifies itself as `IOStorageFamily` 2.1, bundle
path `/System/Library/Extensions/IOStorageFamily.kext`, and carries the
personalities the publication path needs:

| Personality | Match | Consequence for gta4l |
| --- | --- | --- |
| `IOBlockStorageDriver` | `IOProviderClass = IOBlockStorageDevice`, `IOPropertyMatch = { device-type = Generic }` | Any `IOBlockStorageDevice` subclass that keeps the base `device-type` property is driven by the family's block driver, which creates the `IOMedia`. |
| `IOGUIDPartitionScheme` | `IOProviderClass = IOMedia`, `Whole = true`, `Content Mask = GUID_partition_scheme` | The GPT is scanned and partition media are created without a filesystem or mount. |
| `IOMediaBSDClient` | `IOProviderClass = IOMedia`, `IOResourceMatch = IOBSD` | BSD disk nodes can appear for the media; this is an observation, not a mount. |
| `IOApplePartitionScheme`, `IOFDiskPartitionScheme` | `IOMedia` whole-disk matches | Present but not expected to match the Android GPT; harmless if they do not. |

`IOBlockStorageDevice::init` sets `device-type = Generic` itself
(`IOBlockStorageDevice.cpp`), so the subclass must call the base `init` (the
default does) and must not overwrite that property.

The prelinked `com.apple.filesystems.apfs` is also present. It may probe the
published media for APFS containers. Whether a container is present is not
established by the Android GPT type; the first run must report what APFS
actually did. The expectation is a read-only probe with no mount, but that
is an observation, not a guarantee.

### 2.2 Public headers and the ABI audit tooling

The build tree already prepares the public IOStorageFamily headers at
`build/storage-abi/include/IOKit/storage/` from source revision
`e45fbe30a7879e0b16e26dddb78538f1b00e0bff`
(`scripts/prepare-storage-headers.py`, `build/storage-abi/headers.json`,
`qualification = false`). `scripts/audit-storage-interface.py` exists to
qualify the ordinary-ARM64 `IOBlockStorageDevice` ABI against the original
kernelcache: it compiles `IOBlockStorageDevice.cpp`, checks the class byte
size (the script expects `0x90`), the vtable symbol set/order, the
deployment target and the imported symbols.

The audit artifact is not present in this checkout, so the first
implementation must run it before linking (fail closed if it cannot run).
The compile-only gate can still use the prepared headers on Linux, but it is
not a substitute for the audit.

### 2.2.1 Qualification feasibility (2026-11-05)

The existing audit pipeline is **not runnable as-is in this checkout**. The
gaps are concrete. The table records the state before the result below; the
qualification now exists under `out/gta4l/storage-abi/`.

| Requirement | State here |
| --- | --- |
| `refer/IOStorageFamily` public source at `e45fbe30…` | Missing at the UniXNU root. The gta4l build tree has a symlink to `out/feeds/IOStorageFamily-67f9003c`, which is the expected revision. |
| macOS SDK `TargetConditionals.h` | `xcrun --show-sdk-path` returns the literal `macosx` through the Linux shim, so `macosx/usr/include/TargetConditionals.h` does not exist. The Linux build uses `scripts/linux/TargetConditionals.h`. |
| Audit candidate path | Hardcoded to `build/xnu-prelink/native-apfs-load-positive.macho`; the gta4l candidate is `out/gta4l/ipados-17.7.11-21H461/gta4l/kernel/kernel.macho`. The audit needs `--candidate`/`--plan` (or an equivalent) to audit the gta4l candidate. |
| `build/xnu-prelink/native-apfs-load-positive.macho` and its plan | Missing. |
| `build/darwinboot-storage-native-metadata/gdb-stop.json` | Missing. The live observation is candidate-specific: the audit requires `kernel_sha256 == plan['candidate_sha256']`. The two `gdb-stop.json` files under `out/gta4l/ram-root-*-evidence/` belong to the RAM-root fixture (`kernel_sha256 dc8184da…`), not the storage qualification, and cannot be reused for the gta4l candidate (`ca983f7d…`). |
| `build/image4-ignition/aes-interface-audit.json` | Missing. Its `compiled_metaclass_offsets` are a compile-time property of the OS profile and may be reusable if the original kernel hash and layout match; the live storage observation is not reusable across candidates without checking the addresses. |
| `build/storage-abi/interface-audit.json` | Missing. |
| `build/ram-block/bindings.json` | Missing. |
| gta4l prelinked candidate | Present: `out/gta4l/ipados-17.7.11-21H461/gta4l/kernel/kernel.macho` (`ca983f7d…`) with `kernel.json`. |
| gta4l plan storage metadata | Present: storage `__TEXT/__TEXT_EXEC/__DATA_CONST/__DATA` extents and `original_executable_sha256 = 852b7846…`. |
| Original kernelcache | Present: `out/ipsw/21H461/kernelcache.decompressed` (`f18b4df8…`). |

The audit's live metadata and AES audit are evidence from the original
kernel, not generated data. The live metadata must match the candidate being
audited; the gta4l candidate has no live observation in this checkout. The
audit also produces the candidate-specific vtable/metaclass targets that
`build-virtio-mmio-bindings.py` turns into the constructor, destructor,
metaclass and method bindings; compiling the subclass against the headers
does not produce those bindings. A compile-only gate is therefore not a
substitute.

Three qualification paths are possible:

1. **Rebuild the original storage-audit harness on a macOS host** (original
   candidate, original live observation, AES audit, public source), then run
   the same harness against the gta4l candidate to produce a gta4l
   `gdb-stop.json`. Parameterize the audit for `--candidate`/`--plan`; the
   AES metaclass offsets may be reused if the OS profile and original kernel
   hash match, but the live storage observation must be produced for the
   gta4l candidate.
2. **Port the audit and the observation harness to the Linux/QEMU setup**
   (parameterize the SDK header to `scripts/linux/TargetConditionals.h`,
   use `llvm-dwarfdump`/`llvm-cxxfilt`, accept `--candidate`/`--plan`) and
   produce the gta4l candidate's live observation there. This changes the
   qualification harness, not the acceptance criteria, and needs owner
   review.
3. **Defer the driver** until path 1 or 2 succeeds. Do not link a
   `IOBlockStorageDevice` subclass without the audit/bindings.

Recommendation: path 2 if the QEMU/GDB observation harness can run the gta4l
candidate; otherwise path 1. The first implementation task is the
qualification harness, not the block driver.

**Result (2026-11-05): path 2 succeeded on Linux/QEMU.** The harness is
`scripts/audit-gta4l-storage-interface.py` in this repository. Run it from
the repository root as `python3.11 -B
scripts/audit-gta4l-storage-interface.py`; it writes the artifacts under
`out/gta4l/storage-abi/`, and `--skip-qemu` reuses an existing snapshot for
an audit-only rerun. It drives the shared
`scripts/audit-storage-interface.py` and
`scripts/qemu-entry/gdb-stop.py` from the workspace UniXNU checkout (commit
`0a1cc71`), stages the existing QEMU ESP with the PMGR-linked kernel
arguments, stops the actual candidate at `_bsd_init`, reads the live
`IOBlockStorageDevice`/`IOService` metaclass memory and the system registers,
and then runs the full audit. The default macOS-host invocation of the audit
is unchanged.

Inputs: candidate `ca983f7d…` and its plan, public source `e45fbe30…`,
`OSMetaClass.cpo` `76526ced…`, the compile record,
`scripts/linux/TargetConditionals.h`, QEMU 8.2.0 TCG and the 64 MiB AArch64
pflash firmware (`f4cd2f77…`).

Outputs under `out/gta4l/storage-abi/`:

- `gdb-stop.json` `218e935e…` — `pc=_bsd_init`, not interrupted, live
  metaclass `0xfffffff006add1d0`, `IOService` metaclass
  `0xfffffff007adb5b8`, `SCTLR_EL1=0x3454593d`, native 4K;
- `interface-audit.json` `695c7bbe…` — 190 slots, 176 kernel bindings
  (162 inherited + 14 pure virtual), vtable `0xfffffff005cb44c0`, live
  instance sizes `0x90`/`0x88`, `disk_read_tested=false`;
- `derive.json` — the candidate vtable/metaclass addresses reused by the
  audit;
- `run.json` — the original QEMU version, firmware hash, kernel arguments and
  stop address, preserved by `--skip-qemu`;
- `provenance.json` — input/output hashes.

The D8 re-audit against the final relinked candidate `b509c511…` supersedes
these first-run values: `interface-audit.json` is `6908349252b920c…` (190
slots, 181 kernel bindings, 167 inherited + 14 pure virtual, live native 4K,
0 binding-target diffs against the `66977822…` audit), `provenance.json` is
`eace94306cd4…` and the live `gdb-stop.json` snapshot is `d328190b…`. The
audit's `gdb_stop_sha256` field names the `gdb-stop.py` harness script
(`3ba447bc…`); the builder's field of the same name names the snapshot.

`scripts/test-gta4l-storage-abi.py` covers the positive audit and the
negative cases (interrupted snapshot, wrong stop PC, missing memory, wrong
class size, missing SCTLR, wrong TCR, wrong candidate) plus the
`--skip-qemu` run-record checks. It does not run QEMU and skips cleanly when
the qualification artifacts are absent.

Portability changes in the shared harness: the audit accepts
`--candidate/--plan/--snapshot/--metaclass-object/--output/--work-dir` and
the Linux tool paths, uses `llvm-dwarfdump`, preserves the build's `-Wno-*`
flags and downgrades clang 24's new
`-Wmissing-designated-field-initializers` from `-Werror`; `gdb-stop` maps
QEMU's current-EL `SCTLR` to `SCTLR_EL1` at the EL1 stop. The acceptance
criteria are unchanged: the live superclass/size checks, the 190/176
dispatch shape, the native-4K registers and the original executable hash all
run.

The gate is host-only: no device run exists, and block I/O, GPT publication
and hardware remain unqualified. The host implementation that consumes this
qualification is recorded in section 9.1.

### 2.3 Existing `IOBlockStorageDevice` implementations are the template

Two in-tree UniXNU drivers already subclass `IOBlockStorageDevice` against
these headers:

- `drivers/RamBlock/NativeRamBlock.cpp` — a RAM-backed read/write block
  device with an optional persistent overlay;
- `drivers/VirtioMmioBlock/NativeVirtioBlock.cpp` — the QEMU read-only
  transport.

They are not reusable hardware backends, but they pin the shape the gta4l
driver needs:

- a complete set of `IOBlockStorageDevice` overrides (vendor/product/
  revision strings, block size, maximum valid block, media state,
  ejectability/removability, write protection, format/eject refusal,
  `doAsyncReadWrite`);
- a provider-independent personality (`NativeRamBlock` matches
  `IOResources` with `IOResourceMatch = IOBSD`; `NativeVirtioBlock` matches
  an `IOPlatformDevice`);
- a synchronous `doAsyncReadWrite` that completes the request with
  `completion->action(...)` before returning and returns
  `kIOReturnSuccess` even when the completion carries an I/O error;
- an `IOLock` serializing the device state, and the bounce-buffer/copy
  pattern through `IOMemoryDescriptor::writeBytes`/`readBytes`.

`NativeRamBlock` is the closer template because it has no hardware DMA,
uses `IOResources` rather than a device-tree node, and obtains its geometry
from boot-time state.

### 2.4 The gta4l eMMC core is already the backend

`package/Gta4lEmmc.c` provides the controller and card operations the
publication path needs:

| Operation | API |
| --- | --- |
| Controller reset, GCC 400 kHz, retained-rail power handshake, pads | `gta4l_emmc_controller_init()` |
| Full controller + card initialization (CMD0/1/2/3/9/7/13/8) | `gta4l_emmc_init()` |
| One 512-byte CMD17 read | `gta4l_emmc_read_block()` |
| Optional multi-block CMD18 + CMD12 | `gta4l_emmc_read_multi()` (not used by this rung) |
| EXT_CSD read | `gta4l_emmc_read_ext_csd()` |
| Width/timing/bus-test helpers | `gta4l_emmc_set_bus_width_verified()`, `gta4l_emmc_bus_test_verified()`, `gta4l_emmc_set_hs_timing()` (not used by this rung) |

There is no public user-data write API: `gta4l_emmc_write_data()` is static
and only the CMD19 pattern path uses it. The first publication rung therefore
cannot construct a user-data write from the public core.

The XNU probe already maps the three register windows and implements the
`gta4l_emmc_ops` callbacks (`Gta4lEmmcProbe.c`):
`0x04744000` (SDHCI), `0x01438000` (GCC) and `0x00575000` (TLMM), one 4 KiB
window each, through `ml_io_map()`. The probe is entered from the board's
`patches/xnu/gta4l-emmc-probe.patch` in `bsd_init.c` and panics with its
report, so it is a diagnostic, not a service.

### 2.5 Shared SM6115 resources

`UniXNU-sm6115-common` has no SDHCI, eMMC or block-storage component: its
tree carries configs, DTS/DTSI, patches, kits and `soc.toml`, with no storage
driver or register map. The catalog's only UFS component (`ShengUfs`,
`NativeUfs`) targets the SM8550 UFS controller and is not applicable; its
`os_abi` row matches this profile, but its hardware and verified-device rows
do not. The generic block drivers (`RamBlock`, `VirtioMmioBlock`) are ABI
references, not gta4l hardware.

The SDHCI controller bases, GCC clock setup, pad policy and retained-rail
handshake are currently gta4l board facts, so the first publication driver
belongs in `UniXNU-gta4l`. If a second SM6115 board needs the same backend,
the platform layer can move to `UniXNU-sm6115-common` later; that is not a
prerequisite for F5.

## 3. Minimal publication architecture

### 3.1 Object graph

```
IOResources
  └─ Gta4lEmmcBlock            (new, IOBlockStorageDevice subclass, board archive)
       └─ IOBlockStorageDriver (prelinked IOStorageFamily)
            └─ IOMedia (whole device, Whole=true, Writable=false)
                 ├─ IOGUIDPartitionScheme (prelinked)
                 │    └─ IOMedia per GPT partition (Whole=false)
                 └─ IOMediaBSDClient (prelinked)  -> /dev disk nodes
```

`Gta4lEmmcBlock` does not create `IOMedia` itself. It registers as an
`IOBlockStorageDevice`; the family's `IOBlockStorageDriver` matches it and
publishes the media. This is the smallest path and avoids reimplementing
media/partition semantics.

### 3.2 Files and layering

The implementation is split so the portable logic stays host-testable and the
C++ adapter stays thin:

| File | Language | Role |
| --- | --- | --- |
| `package/Gta4lEmmcPlatform.c/.h` | C | Maps SDHCI/GCC/TLMM, implements `gta4l_emmc_ops`, exposes a small `gta4l_emmc_platform` object. Shared by the probe and the block driver. |
| `package/Gta4lEmmcBlockCore.c/.h` | C | Request validation, per-block loop, bounce copy, counters and report formatting. Takes a `read_block` callback so host tests can drive it without hardware. |
| `package/Gta4lEmmcBlock.cpp` | C++ | `IOBlockStorageDevice` subclass: lifecycle, lock, `IOMemoryDescriptor` copies, `IOStorageCompletion`, report thread. Calls the C helpers. |
| `package/Gta4lEmmcBlock.h` | C | Boot-argument and report limits shared by the C and C++ sides (or keep the constants in `Gta4lEmmcBlockCore.h`). |

The request helper is implemented in `package/Gta4lEmmcBlockCore.c/.h`.
`scripts/test-gta4l-emmc-block.py` compiles it against
`scripts/sim-gta4l-emmc-block/harness.c` and passes 23 scenarios plus 18
mutation controls. The helper uses explicit READ/WRITE/INVALID directions,
rejects writes without touching the callbacks, returns per-request completed
bytes (including a partial final copy), caps a request at 64 blocks/32 KiB,
loops one 512-byte read per block through the caller's `read_block`, copies
each block through the caller's `copy_out`, marks the media offline on the
first read or copy failure and preserves the first failure record. It has no
IOKit, transport or hardware dependency.

The probe is refactored to use `Gta4lEmmcPlatform.c` instead of carrying its
own mapping and `gta4l_emmc_probe_*` callbacks. That is a host-only
refactor and must keep every existing probe report byte-identical for the
same inputs; a report-width and a simulator scenario must prove that.

### 3.3 Lifecycle

`Gta4lEmmcBlock::start(IOService *provider)`:

1. `IOBlockStorageDevice::start(provider)`.
2. If `gta4l-emmc-block` is absent, return `false` (no device, no side
   effects). If `gta4l-emmc-probe` is also present, log a refusal and return
   `false`; the probe preflight should reject the combination at build time.
3. Allocate the `IOLock` and the 512-byte bounce buffer.
4. Map the three register windows and call `gta4l_emmc_init()` with the
   platform ops. The core performs the controller reset, GCC 400 kHz setup,
   retained-rail power handshake, pads and read-only identification. It does
   not issue CMD6, CMD18, CMD19 or any data write.
5. Set `capacity_blocks` from the core, `online = true` and
   `write_protected = true`; `registerService()`.
6. In report mode, start the bounded report thread (section 3.6). On an
   initialization failure, panic with the failure report in report mode;
   otherwise log and return `false`.

`stop()` clears `online`, wakes/joins the report thread if it exists, and
calls `IOBlockStorageDevice::stop(provider)`. `free()` releases the lock,
bounce buffer and any thread state, then calls the base `free()`.

The driver does not unmap the register windows; they live for the kernel
lifetime, as in the probe.

### 3.4 Read path

`doAsyncReadWrite(buffer, block, nblks, attributes, completion)`:

- Reject a null buffer/completion/action, zero blocks, `block >= capacity`,
  `nblks > capacity - block`, a buffer shorter than `nblks * 512`, or a
  direction that is neither `kIODirectionIn` nor `kIODirectionOut`, with
  `kIOReturnBadArgument`.
- Reject `kIODirectionOut` with `kIOReturnNotWritable`. This is the second,
  independent write refusal besides `reportWriteProtection(true)`.
- Take the lock, check `online`, and loop `nblks` times:
  - `gta4l_emmc_read_block(&host, block + i, bounce)`;
  - `buffer->writeBytes(i * 512, bounce, 512)`;
  - on a card error or a short copy, stop and complete short with
    `kIOReturnIOError`/`kIOReturnNoMemory`; mark `online = false` and
    preserve the failure record and counters. After the failing completion
    returns, send `kIOMessageMediaStateHasChanged` with
    `(void *)(uintptr_t)kIOMediaStateOffline` through `messageClients()`
    **outside** the controller lock. `IOBlockStorageDriver::message()` handles
    that message by decommissioning the media. A subsequent request fails
    with `kIOReturnNoMedia`; outstanding requests are serialized by the lock
    and the failing request completes before the notification.
- Counters: requests, blocks, failures, maximum request, last error, first
  failure record.
- Unlock, then call `completion->action(target, parameter, status, actual)`.
  Return `kIOReturnSuccess` after a completion, matching `NativeRamBlock`;
  a synchronous error return would make `IOBlockStorageDriver` log a failed
  start instead of completing the request.

There is no per-block retry and no data-circuit reset in the first rung. The
H17 evidence shows a CMD19 data timeout can be tolerated, but a CMD17 read
failure is a different class; adding a recovery policy without evidence would
be a new hypothesis. Failing closed and reporting is the conservative choice.

The first rung uses one block per command. It does not use
`gta4l_emmc_read_multi()`: F2 (CMD18/CMD12) is measured PASS for one
owner-reported cold boot (`docs/emmc-driver.md` §5.3.41) but is not
repeatable evidence or a publication path, and the first run must measure the
storage stack's actual request sizes. A per-block loop is slower but is the
exact path the stage-A/F1 content evidence covers.

Request size: the BSD/storage layer may issue large reads. The driver must
**advertise** the limit, not only enforce it. Before `registerService()`, set
the provider properties that `IOBlockStorageDriver::handleStart()` reads
through `copyProperty(..., gIOServicePlane)`:

- `kIOMaximumBlockCountReadKey` = 64;
- `kIOMaximumByteCountReadKey` = 32768;
- `kIOMaximumSegmentCountReadKey` = 1 (one physically contiguous segment per
  request; the driver copies through a bounce buffer, so no scatter/gather
  contract is implied).

The family then splits larger requests instead of failing them. The driver
still keeps defensive bounds checks and returns `kIOReturnBadArgument` for an
out-of-range or overflowed request, and `kIOReturnMessageTooLarge` if a
request somehow exceeds the advertised limit. The first run measures the
actual request sizes and the split behavior; the value is not a performance
claim. The write-limit properties are left at their default because the
media is non-writable and `doAsyncReadWrite` rejects writes; the read limits
are the ones that matter.

### 3.5 Reported device properties

| Method | Value |
| --- | --- |
| `getVendorString` | `"UniXNU"` |
| `getProductString` | `"gta4l eMMC (read-only)"` |
| `getRevisionString` | `"1.0"` |
| `getAdditionalDeviceInfoString` | `"read-only bring-up; no DMA/CQE/ICE"` |
| `reportBlockSize` | `512` |
| `reportMaxValidBlock` | `capacity_blocks - 1` |
| `reportMediaState` | `*mediaPresent = online`, `*changedState = false` |
| `reportWriteProtection` | `true` |
| `reportRemovability` | `false` |
| `reportEjectability` | `false` |
| `doEjectMedia`, `doFormatMedia` | `kIOReturnUnsupported` |
| `doGetFormatCapacities` | `0` |
| `doSynchronizeCache`, `doSynchronize` | `kIOReturnSuccess` (no cache) |
| `getWriteCacheState` | `false` |
| `setWriteCacheState` | `kIOReturnUnsupported` |
| `doDiscard`, `doUnmap`, `doGetProvisionStatus` | `kIOReturnUnsupported` |

`doAsyncReadWrite` is the only data path. On a read error the driver marks
the media offline and sends the explicit `kIOMessageMediaStateHasChanged`
notification; `reportMediaState` then returns false, and later requests fail
with `kIOReturnNoMedia`. The driver does not retry reads forever.

### 3.6 Report and observation

F5 needs an observable object, not a mount. The first rung uses report mode
`gta4l-emmc-block=1`:

- After `registerService()`, start one kernel thread
  (`kernel_thread_start`, as in `drivers/ArmCpu/ArmCpu.cpp`).
- The thread polls every 100 ms for up to 20 s for an `IOMedia` with
  `Whole = true` whose provider chain contains the driver. It snapshots:
  - driver/card state: capacity, block size, width, timing, clock, online,
    write protection;
  - the whole media: `Base`, `Size`, `Content`, `BSD Name` if present,
    `Writable`;
  - a bounded partition summary: count, the first few entries
    (index/base/size/content/BSD name), and whether the count matches the
    GPT entry count. Do not dump every partition and do not include volume
    UUIDs, disk UUIDs or any other unique identifier;
  - counters;
  - one known-block read through `IOMedia::read()` (not the raw core) with a
    completion waiter, compared to a **same-boot** stage-A baseline sum for
    LBA 1 or the 21-block array.
- It then panics with a bounded `GTA4L-EMMC P5 REPORT` block (the panic is
  the delivery mechanism, as in the earlier rungs). If no whole media
  appears, it panics with the failure state instead of returning silently.

The report does **not** treat a fixed settle delay as proof that partition
discovery finished. Partition media are an additional observation with a
bounded wait; the report states how long it waited and how many partitions
it saw, and does not claim completeness from the delay.

Proposed report shape (the implementation pins the exact fields and keeps
the whole block under 2048 bytes, like the existing reports):

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 blocks=61071360 bs=512 bytes=31268536320 wid=1 tim=0 clk=400000
GTA4L-EMMC P5 DRV: online=1 wp=1 reads=... blocks=... fails=... max=...
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0
GTA4L-EMMC P5 PART: count=... expected=... waited_ms=... first=...
GTA4L-EMMC P5 READ: lba=1 sum=... base=... same=1
GTA4L-EMMC P5 FAIL: fail=.../...
```

`Size` is in bytes (`61071360 * 512 = 31268536320`); the block count is
reported separately. `BSD Name` may be omitted from the report; it is not
needed for F5 and is not a unique device identifier.

Stop codes: 0 clean whole-media publication and read-back; 10 the block
argument is absent or contradictory; 11 controller/card initialization
failed; 12 no whole media within the timeout; 13 whole-media
geometry/property mismatch; 14 the media read-back diverged; 15 whole media
exists but the partition summary could not be obtained (this is **not** an
F5 failure; it is reported as a separate partition-discovery result); 16 the
report thread timed out or the panic budget was exceeded.

`gta4l-emmc-block=2` can mean "publish without the report panic" for later
interactive work. The first rung uses `=1` only; `=0`/absent is fully inert.

### 3.7 Why not reuse the probe or the RAM block

- The probe runs in `bsd_init` before IOKit matching, has no object
  lifecycle, and panics by design. It cannot publish.
- `NativeRamBlock` reads a RAM disk from `/chosen/memory-map`; it never
  touches SDHCI. `NativeVirtioBlock` is a QEMU transport. Neither can serve
  the eMMC.
- A new filesystem driver is not needed: the GPT scheme and BSD media client
  are prelinked, and F5 does not require a mount.

## 4. Build, link and boot-argument plumbing

### 4.1 Archives

Two viable layouts:

1. **One archive** `libGta4lEmmc.a` containing the core, the probe and the
   block driver. Replace the current
   `libGta4lEmmcProbe.a` entry in `CONFIG_XNU_EXTRA_LD_ARCHIVES`. Simple,
   but every image carries the C++ block object and the probe build script
   now needs the C++ compile record.
2. **Split archives** (implemented 2026-11-05): `libGta4lEmmcCore.a`
   (portable core, board platform and read-only request helper, linked once),
   `libGta4lEmmcProbe.a` (probe consumer) and `libGta4lEmmcBlock.a` (adapter).
   Link them with `-force_load`; the core symbols exist once. This keeps the
   probe and block builds independent and avoids duplicate
   `gta4l_emmc_init`/read symbols. `scripts/build-gta4l-emmc-block.py` builds
   the adapter archive from the policy object, the C++ object and the
   generated original bindings; `configs/gta4l_defconfig` already force-loads
   all three archives.

The split layout is preferred for the future mount/verification work. The
build scripts prove there are no duplicate global symbols across the core,
probe and block archives (`nm` check) and record the archive hashes in the
existing build-report style.

### 4.2 Compile records

- C core/probe/platform/helper objects: use the `bsd_init.o.json` compile
  record as `scripts/build-gta4l-emmc-probe.py` does today.
- C++ adapter: use the `IOPlatformExpert.cpo.json` record and the storage
  ABI include, as `scripts/build-arm-driver.py` does for `NativeRamBlock`
  and `NativeVirtioBlock` (`-I build/storage-abi/include`, `-Werror` and
  the kernel C++ flags). `scripts/build-gta4l-emmc-block.py` implements this
  path: it refuses a missing, stale or wrong-candidate storage-ABI audit,
  binds the original storage base-class symbols from the recorded interface
  audit, records every source hash and never marks the archive
  `runtime_qualified`.

### 4.3 Personality and kernel configuration

The board patch series applies `sheng-platform-power.patch`,
`gta4l-sdhci-snapshot.patch`, `gta4l-sdhci-handoff-audit.patch`,
`gta4l-emmc-probe.patch` and `gta4l-emmc-block.patch`. The last one appends
the guarded personality to `iokit/KernelConfigTables.cpp`, modelled on the
`XNU_NATIVE_RAM_BLOCK` block in the shared
`native-aes-personality.patch`:

```
#if defined(XNU_NATIVE_GTA4L_EMMC_BLOCK) && XNU_NATIVE_GTA4L_EMMC_BLOCK
    "   ,{"
    "     'IOClass'         = Gta4lEmmcBlock;"
    "     'IOMatchCategory' = Gta4lEmmcBlock;"
    "     'IOProviderClass' = IOResources;"
    "     'IOResourceMatch' = IOBSD;"
    "     'IOProbeScore'    = 1000:32;"
    "   }"
#endif
```

`IOResources` + `IOResourceMatch = IOBSD` is the same start ordering as
`NativeRamBlock`: the driver starts after BSD init resources exist. The
ordering is pinned in source: the board probe hook runs in `bsd_init.c`
around the host-init/audit calls, while `IOKitBSDInit()` is called later in
the same function, and `IOKitBSDInit.cpp` publishes the `IOBSD` resource.
The block driver's personality waits for that resource, so it cannot overlap
the `bsd_init` probe when the boot arguments are mutually exclusive. The
exact match order still needs a trace in the first report; do not infer it
from the boot argument alone.

`scripts/build-xnu-4k-baseline.sh` translates
`CONFIG_XNU_NATIVE_GTA4L_EMMC_BLOCK=y` into
`-DXNU_NATIVE_GTA4L_EMMC_BLOCK=1` in `PERSONALITY_CFLAGS`, in the same style
as `XNU_NATIVE_RAM_BLOCK`. The D8 batch adds the minimal shared helper
`scripts/xnu-native-personality.sh`, sourced by the kernel build script: it
maps `0` (or absent) to no flag, `1` to `-DXNU_NATIVE_GTA4L_EMMC_BLOCK=1` and
any other value to a hard error, so an enabled configuration cannot silently
become a half-translated one. The board defconfig cannot inject the compiler
flag directly. `configs/gta4l_defconfig` now sets
`CONFIG_XNU_NATIVE_GTA4L_EMMC_BLOCK=y` and force-loads
`libGta4lEmmcBlock.a`; the driver still requires report mode
`gta4l-emmc-block=1` before any MMIO, so the personality being compiled in is
not by itself a device action. If the shared change is not acceptable, the
alternative is to define the personality in a board-only
`KernelConfigTables.cpp` patch that is only applied for this board and is
unconditional, but that advertises a class that might not be linked in other
configurations and is not recommended.

### 4.4 Boot arguments and payload

- New driver argument: `gta4l-emmc-block=0|1|2|3` (absent/0 off, 1 CMD17
  report mode, 2 publish-only, 3 chunked report mode with two-sector CMD18
  chunks). The adapter implements report modes 1 and 3; mode 2 is deferred
  and is refused rather than silently treated as another mode.
- Existing probe argument: `gta4l-emmc-probe=1..17`.
- The payload contract requires exactly one of the two families, not just
  one `gta4l-emmc-probe` token. A build that sets both, repeats the block
  token or carries a malformed probe token in block mode is refused before
  flashing.
- `emmc-block-1` and `emmc-block-3` are implemented in
  `scripts/build-gta4l-diagnostic-payload.py`, `scripts/check-gta4l-payload.py`
  (the names map is the single source) and
  `scripts/package-gta4l-diagnostic.py`, mapping to
  `gta4l-emmc-block=1` and `gta4l-emmc-block=3`.
- The three archive paths are in `configs/gta4l_defconfig`
  `CONFIG_XNU_EXTRA_LD_ARCHIVES`.
- No new kernel prelink root is needed; IOStorageFamily is already there.

## 5. Host verification plan

All of this is host-only and can run before any owner approval.

1. **ABI gate.** Run `scripts/audit-storage-interface.py` (macOS/xcrun and
   the original kernelcache) or fail closed. Compile the C++ adapter against
   `build/storage-abi/include` with the `IOPlatformExpert.cpo.json` flags.
   Check the object is ordinary ARM64, the class symbols are present and the
   vtable/class size matches the audited reference.
2. **Request-path tests.** Compile `Gta4lEmmcBlockCore.c` against a fake
   `read_block` callback and test: read-only rejection, out-of-range block,
   count overflow, short buffer, per-block offsets, short read, card error,
   offline transition, counter values, max-request refusal and report
   formatting at field bounds.
3. **Source policy.** Reject any call to a write/CMD18/CMD19/CMD25 helper
   from the publication path; the only backend calls are `gta4l_emmc_init`,
   `gta4l_emmc_read_block`, `gta4l_emmc_read_ext_csd` (if needed for
   geometry) and the platform ops. No CMD6, CMD18, CMD19 or data write.
4. **Probe-refactor regression.** The platform-layer extraction must keep
   every existing probe scenario and report byte-identical for the same
   inputs. Run the full core/probe suites and the report-width fixtures.
5. **Duplicate-symbol check.** `nm` over the three archives: core symbols
   appear once; probe and block consumers only reference them.
6. **Boot-image gate.** Extend the payload/boot-image contract for
   `emmc-block-1` and verify the image contains the class string and the
   personality string, no `gta4l-emmc-probe=` token, and exactly one
   `gta4l-emmc-block=1` token.

**Host result (2026-11-05).** The P5-1 implementation and the available host
gates pass. The P5-1 builder produced `libGta4lEmmcBlock.a` (`fedd4bc3…`)
against the earlier `ca983f7d…` candidate (interface audit `695c7bbe…`,
provenance `cbf3a80b…`). The D8 host kernel build then produced the prelinked
candidate `b509c511…` with the personality compiled in, and the re-run
builder produced a byte-identical `libGta4lEmmcBlock.a` (`ff447a48…`) from
the policy object, the C++ object and the generated original bindings,
against the re-audited `b509c511…` candidate (interface audit
`6908349252b920c…`, provenance `eace94306cd4…`, 190 virtual methods, 26
concrete overrides, 0 unresolved original imports). The builder reports
`qualified=true`, `candidate_matches_audit=true`, `personality_enabled=true`
and `runtime_qualified=false`. `scripts/test-gta4l-emmc-block.py` passes 23
core scenarios with 18 mutation controls and 28 adapter scenarios at the P5-1
snapshot (32 after the D9.4 correction) against the host IOKit seam; the
core/probe suites pass 179/104 and 163/77; the storage-ABI positive/negative
suite and the payload/inventory suites pass.
The adapter scenarios cover disabled/conflicting/duplicate/deferred
arguments, init/lock/thread failures, read-only properties and limits, write
and invalid-request refusal, one/multiple/maximum reads, copy and media-path
failures, absent/wrong-geometry media, same-boot divergence and match, GPT
absence, watchdog, and the policy mapping. This is host evidence only: no
payload image was built, nothing was flashed or booted, no `IOMedia` object
was observed, and F5 remains `[U]` for the P5-1/D8 batch.

**P5-2 device result (2026-11-05).** The first approved P5-2 BOOT-only flash
used the D8 candidate `b509c511…` (block archive `ff447a48…`, payload
`7b2ac8c0…`, boot image `4a635cae…`). That boot published the whole
`IOMedia`, but the report client did not hold a valid open and the media-path
read failed; the failure is preserved in
`out/gta4l/ram-root-emmc-block-p5-evidence/` and is not relabeled. The D9.4
correction opens the media before the report read
(`media->open(this, 0, kIOStorageAccessReader)`), closes exactly once on every
exit and records the access mode; the host IOKit seam now starts closed and
covers the unopened-read regression and balanced-open accounting in 32
adapter scenarios (was 28). The
corrected p5b candidate was rebuilt from the same working tree: kernel
`2f8238b9…` (51,187,712 bytes), re-audited candidate (interface audit
`1f456fec…`, provenance `cdfe6950…`, 190 virtual methods, 26 concrete
overrides, 0 unresolved original imports), block archive `6d36826a…`
(350,248 bytes), payload `47b9593d…` (9/9 contract, exactly one
`gta4l-emmc-block=1`, no probe token) and boot image `e256b27d…` (18/18 gate,
only the ramdisk differs from the `ce4d76e8…` baseline). The packaged payload
kernel is `26af8293…`; `kernel-string-verification.txt` records the class,
personality and report strings and the payload token counts.

The owner approved a fresh BOOT-only flash of p69 only (pre-flash
`21f3b5f2…`, post-flash `f1954fd4…`, unchanged tail `a28b4ea2…`; RECOVERY,
EFS and sec_efs unchanged; no host-issued reboot). The owner-supplied report
is `rc=0 stop=0 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0
clk=400000`; the whole media is `whole=1 base=0 size=31268536320
content=GUID_partition_scheme writable=0`; the driver reports `online=1 wp=1
reads=10 blocks=30 fails=0 max=8`; the media-path read is `lba=1 attempted=1
ok=1 bytes=512 sum=2228245566 base=2228245566 same=1`; the failure record is
all zero and the terminal panic is the intentional report-mode panic at
`Gta4lEmmcBlock.cpp:914`. **F5 is measured PASS for this reported boot at
1-bit SDR, 400 kHz `[C]`.** The boot ordinal and cold/warm class were not
supplied and are not inferred; the owner confirmed on 2026-11-05 that the
tablet returned to TWRP. Partition discovery is a separate observation:
`count=80 expected=84 waited_ms=0 error=0 reported=3`; `expected` is the GPT
header entry-array slot count and the reporter stops waiting on any nonzero
count. A host-only audit (`docs/emmc-driver.md` §5.3.38) matched `count=80`
to the validated 80 populated entries and matched the first three children
to GPT entries 1-3, so there is no missing-partition or counting defect;
partition completeness is not claimed from the report alone. F2 is now
measured PASS for one reported cold boot (`docs/emmc-driver.md` §5.3.41), but
multi-block publication is still unimplemented and unqualified; 8-bit,
4-bit/HS publication, mount/root switch and F6 remain separate gates. Evidence:
`out/gta4l/ram-root-emmc-block-p5b-evidence/` (`flash-record.json`,
`owner-report.txt`, `device-run-record.json`, `build-provenance.json`,
`kernel-string-verification.txt`).

The full existing battery (`test-gta4l-emmc.py`, `test-gta4l-emmc-probe.py`,
the counts test, the fixture/board/payload/package/boot-image tests, the
hash sweep, `git diff --check` and the privacy checks) still runs before any
commit.

## 6. Device-run plan (executed for p5b)

Status: executed for the corrected p5b candidate. The reported boot published
the whole-device media and matched the same-boot media-path read, so F5 is
measured PASS for that boot at 1-bit SDR, 400 kHz `[C]` (section 5 and
`docs/emmc-driver.md` §5.3.37). The plan below remains the scope record; the
initial P5-2 attempt failed at the report-client open and is preserved, not
relabeled.

The first F5 device run is a new image with `gta4l-emmc-block=1`, no
`gta4l-emmc-probe` token, BOOT-only flash after a verified backup, and up to
four boots without reflashing (the standing stop budget). The report thread
panics; the owner supplies the `GTA4L-EMMC P5 REPORT` block.

Card-facing scope for the approval:

- initialization: CMD0, CMD1, CMD2, CMD3, CMD9, CMD7, CMD13 and the EXT_CSD
  read (CMD8), exactly the read-only identification already approved for the
  probe;
- data: CMD17 single-block reads of LBAs chosen by the XNU storage stack.
  This is **broader than the probe's pinned LBA list** because the partition
  scheme and any prelinked filesystem probe choose their own LBAs. Every
  access is a read; the driver rejects writes and reports write protection;
- no CMD6, no CMD18/CMD12, no CMD19, no CMD25, no user-data write, no
  mount, no root switch, no DMA, no CQE, no ICE.

Host-facing scope: the standard controller reset, GCC 400 kHz setup,
retained-rail power handshake, pads and polling PIO. No new register class
beyond what the probe already touches.

Pass/fail and the separate partition result:

- **F5 pass** — whole-device `IOMedia` with the expected geometry and
  write-protection, and the media-path read-back matching the same-boot
  baseline.
- **F5 fail** — no whole media, initialization failure, or a read-back
  mismatch. The report must name the stop code and the first failure
  record.
- **Partition discovery (separate result)** — whether GPT partition media
  appeared, how many, and the bounded summary. A whole media with no
  partition media is still an F5 pass; the partition result is recorded for
  the next investigation.

The known pre-identification CMD3 flake still applies to any boot; a
pre-publication stop is a boot fact, not an F5 verdict. No in-boot retry.

## 7. Mount question

F5 does not require a mount and this design does not add one. The expected
automatic behavior is:

- `IOGUIDPartitionScheme` reads the GPT and creates partition media;
- `IOMediaBSDClient` can create BSD disk nodes;
- APFS is prelinked and may probe the media. Whether an APFS container is
  present is not established by the Android GPT type; the first run must
  report what APFS actually did. The expectation is a read-only probe with
  no mount, but that is an observation, not a guarantee;
- there is no ext4/f2fs driver in the prelinked set, and no FAT driver is
  guaranteed; no partition content is changed by a read-only probe.

Read-only publication prevents writes through this driver. It does **not**
by itself prevent a read-only mount by a filesystem client, so “no mount” is
a run-scope decision and a report observation, not a property the block
driver can guarantee.

A later mount test, if the owner wants one, needs a known filesystem on a
scratch region, an explicit read/write policy and a separate approval. It is
not part of F5 and must not be smuggled into the publication rung.

## 8. Risk register

| Risk | Effect | Mitigation |
| --- | --- | --- |
| Storage ABI mismatch (`IOBlockStorageDevice` size/vtable) | Link failure or runtime crash | The audit/bindings artifacts are missing (section 2.2.1). Produce the audit and concrete bindings before linking; compile-only is not sufficient. Fail closed. |
| Duplicate core symbols from probe and block archives | Link failure | Split the core into `libGta4lEmmcCore.a`; `nm` duplicate check. |
| `IOBlockStorageDriver` does not match | No `IOMedia`, no F5 | Keep the base `device-type = Generic`; use the `IOResources`/`IOBSD` personality; report the IOService tree. |
| GPT scheme does not attach | Partition discovery result absent; F5 can still pass | Report the whole-media properties and the partition count/absence; debug with the report, not a new device hypothesis. |
| CMD3 identification flake at IOBSD time | No publication on that boot | Four-boot rule; failure report; no in-boot retry. |
| Long synchronous reads / watchdog | Boot stall or timeout | Advertise `kIOMaximumBlockCountReadKey`/`kIOMaximumByteCountReadKey` so the family splits; keep defensive bounds checks; measure request sizes. |
| Read error wedges the controller | Media goes offline | Fail closed, mark offline, send `kIOMessageMediaStateHasChanged` outside the lock, preserve the first failure record; no unproven reset/retry in the first rung. |
| APFS/filesystem probe reads a lot | Slow boot or noisy logs | Read-only, advertised limits, report the observed media; no mount. |
| Both boot arguments set | Probe panics before publication, wasted boot | Contract check rejects the combination at build time; driver also refuses at start. |
| Power rails differ at IOBSD time | Initialization failure | Full controller reset and retained-rail handshake; report the failing step. |
| Report thread races media creation | No media in the report | Poll for the whole media; report partition discovery separately with a bounded wait and no completeness claim. |
| Panic report exceeds the buffer | Truncated or missing report | Field-wise width fixture and a 2048-byte budget, as with the existing reports. |
| Report leaks a unique identifier | Privacy violation | No volume/disk UUIDs, serials or other identifiers; bounded partition summary only. |

## 9. Open owner decisions and recommendation

1. **Publication scope** — recommend whole-device read-only publication,
   no mount. The whole-device object and the media-path read are F5; the GPT
   partition media are a separate additional observation.
2. **ABI qualification path** — decide between running the existing audit
   on a macOS host (path 1) and porting it to the Linux toolchain with a
   pinned live-evidence record (path 2). Do not link the driver before one
   path succeeds.
3. **First-rung width/timing** — recommend 1-bit SDR at 400 kHz. It is the
   path with the most content evidence and the widest margin; the verified
   4-bit read path can be a later publication rung after the first success.
4. **Read scope** — the storage stack will choose LBAs, so the approval must
   cover arbitrary read-only LBAs, not the probe's pinned list. This is the
   main scope difference from the earlier rungs and should be explicit in
   the approval.
5. **Mount** — recommend no mount in F5; a mount test is a separate,
   owner-gated stage with a known filesystem and a scratch region. Read-only
   publication does not itself prevent a read-only mount.
6. **Report mode** — recommend `gta4l-emmc-block=1` (report + panic) for the
   first rung and `=2` (publish only) later, if interactive work needs it.

### 9.1 Implementation sequence

1. **Qualification harness — DONE (2026-11-05).** Path 2 in section 2.2.1
   succeeded on Linux/QEMU: `scripts/audit-gta4l-storage-interface.py`
   produced the live `gdb-stop.json`, `derive.json`, `interface-audit.json`
   and `provenance.json` for candidate `ca983f7d…`, and the audit passes the
   190-slot/176-binding/live-metaclass/native-4K checks. The D8 batch then
   re-ran the audit against the final relinked candidate `b509c511…`
   (`6908349252b920c…`, 181 bindings, 0 binding-target diffs). The D9 batch
   re-ran it once more against `2f8238b9…` (`1f456fec…`, provenance
   `cdfe6950…`, 190 slots, 26 concrete overrides, 0 unresolved original
   imports). The concrete bindings are generated from the compiled driver
   object in step 2.
2. **Host implementation — DONE (host-only, 2026-11-05).** The portable
   request helper `package/Gta4lEmmcBlockCore.c/.h` passes 23 scenarios and
   18 mutation controls; the thin C++ adapter
   `package/Gta4lEmmcBlock.cpp`, the policy object
   `package/Gta4lEmmcBlock.c/.h`, the guarded personality patch, the block
   archive builder and the `gta4l-emmc-block` payload mapping are
   implemented. `scripts/test-gta4l-emmc-block.py` also runs 32 adapter
   scenarios against a host IOKit seam after the D9.4 correction, and the
   builder produced the qualified `libGta4lEmmcBlock.a` (`6d36826a…`)
   against `2f8238b9…`. The D9.4 correction opens the media before the
   report read and closes it exactly once on every exit; the seam starts
   closed and covers the unopened-read regression and balanced-open
   accounting. The D8 batch enables the personality in
   `configs/gta4l_defconfig` and adds the shared configuration-to-flag
   translation, so the D8 host kernel `b509c511…` compiles and links the
   adapter; the D9 host kernel `2f8238b9…` is the device candidate.
3. **Device candidate — DONE (2026-11-05).** The `emmc-block-1` payload
   (`47b9593d…`, 9/9 contract) and boot image (`e256b27d…`, 18/18 gate)
   were built from the enabled-personality candidate, flashed BOOT only
   after a verified backup, and booted. The owner-supplied `GTA4L-EMMC P5
   REPORT` records `rc=0 stop=0`, the whole `IOMedia` (`whole=1`, 61,071,360
   blocks of 512 bytes, `GUID_partition_scheme`, `writable=0`) and a
   media-path read matching the same-boot baseline (`same=1`). F5 is
   measured PASS for this boot at 1-bit SDR, 400 kHz `[C]`; the boot ordinal
   and cold/warm class were not supplied. The follow-up host-only audit
   (§5.3.38) matched `count=80` to the validated 80-entry GPT; partition
   completeness is not claimed from the report alone.
4. **Follow-ups.** F2 multi-block reads are measured PASS for one reported
   cold boot but have no publication rung; 8-bit width, 4-bit/HS publication,
   a CMD18 IOMedia path, a mount test and F6 are separate rungs with separate
   approvals. None is a prerequisite for the first whole-device F5 result.

### 9.2 D13 readiness (2026-11-06, host-only)

The plan's N1 readiness pass is recorded in
`docs/emmc-d13-readiness-2026-11-06.md` (design and source inspection only; no
implementation, build, QEMU, device access or commit). It grounds the current
one-sector adapter seam and the diagnostic CMD18/CMD12 API, and proposes a
conservative first contract:

- keep the 64-sector / 32-KiB / one-segment external request shape and the
  `gta4l-emmc-block=1` singleton backend;
- CMD17 for a singleton, two-sector CMD18 chunks and one CMD17 tail;
- an optional multi-read callback that returns transport, requested and
  completed sectors and the mandatory stop outcome separately;
- a private bounded bounce, with no byte delivered until exact count and a
  clean CMD12 qualify; a failed chunk contributes no bytes, earlier clean
  chunks may be partial, and a failed stop is never repaired by a copy or a
  CMD17 fallback;
- first-failure preservation, fail closed/offline, no suffix I/O, retry,
  fallback or recovery, one completion per accepted request and no callback
  under the gate;
- a new explicit selector, implemented as `gta4l-emmc-block=3`, that does not
  silently change `=1`; no mount, write, DMA, CQE, ICE or width/speed work.

File-by-file implementation map (N3 implemented host-only):

| File | Proposed change |
| --- | --- |
| `package/Gta4lEmmcBlockCore.c/.h` | Add an optional multi-read callback and a chunk loop that qualifies transport/completed/stop before copy-out; add bounded counters for CMD17/CMD18 chunks, requested/completed/delivered bytes and stop status; keep the one-sector path unchanged for `=1`. |
| `package/Gta4lEmmcBlock.c/.h` | Add the new selector and its conflicts; extend the report struct/format within the 2048-byte budget. |
| `package/Gta4lEmmcBlock.cpp` | Implement the callback through a production wrapper; own the chunk bounce; copy out only qualified chunks; preserve the gate, single-completion and offline-notification order. |
| `package/Gta4lEmmc.c/.h` | Add a wrapper that exposes completed and stop outcomes explicitly without changing the diagnostic `read_multi` semantics. |
| `package/Gta4lEmmcPlatform.c/.h` | No change expected. |
| `patches/xnu/gta4l-emmc-block.patch`, `scripts/build-gta4l-emmc-block.py` | Update only if new symbols or the personality flag require it. |
| `scripts/sim-gta4l-emmc-block/*`, `scripts/test-gta4l-emmc-block.py` | Add chunk/split/failure scenarios and runtime-negative mutations. Implemented as 48 core / 38 mutation / 55 adapter plus 3 adapter-source mutations after the N3 corrective batch; the readiness inventory was 23/18/32. |
| `scripts/sim-gta4l-emmc/*`, `scripts/test-gta4l-emmc.py` | Keep the 18 `multi-*` scenarios and 12 multi-block mutations; add wrapper-specific tests if the transport wrapper changes. |
| `scripts/build-gta4l-diagnostic-payload.py`, `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py`, `docs/diagnostic-payload-input-inventory.md` | Add the new selector token/value, mutual-exclusion rule, allowlist and inventory row. |
| `docs/emmc-driver.md`, `docs/gta4l-storage-publication-design.md`, `porting.toml`, `README.md`, `docs/evidence-baseline.md` | Update findings/evidence with the implementation; not part of this readiness batch. |

The N3 host batch and its corrective pass implemented this map: the block
suite now runs 48 core scenarios / 38 mutation controls, 55 adapter scenarios
and 3 adapter-source mutations, and `emmc-block-3` is accepted end-to-end by
the diagnostic builder/checker/packager tests. The corrective pass added the
stop-failure accounting, the multi-result qualification contract and the
mode-3 controlled reporter (exact-byte reference comparison, CMD18/CMD12 and
odd-tail accounting, incidental-attribution exclusion, watchdog snapshot and
terminal-race controls). N3 selected the first timing option: it keeps the
two-sector shape and 64-sector ceiling, records the ~128–144 s pathological
synchronous gate-hold bound as a host-development risk, and changes no timeout
or advertised limit. The first hardware discriminator remains a two-sector
GPT-array read plus an odd-length split/tail. N3 host acceptance is complete;
it is not a hardware, initialization-reliability or general-I/O
qualification. The next step is an N4 candidate proposal, which requires
separate candidate-build and ABI/QEMU approval. The stale storage-ABI
candidate/provenance still needs its own requalification path and is not
repaired by this design pass.

### 9.3 N5 mode-3 device observation (2026-11-07, one cold boot)

The N4 candidate built the N3 contract through the archive/prelink/payload/BOOT
flow (`docs/emmc-n5-integration-observation-2026-11-07.md`). Its BOOT image
`54cdbfb6…` (85,688,320 bytes, 18/18 gate, payload `4a400ca8…` with a 9/9
contract, packaged kernel `2e6d91a0…`) was flashed BOOT only to p69 after the
existing pre-flash backup; the post-flash full hash, image-length prefix and
unchanged tail matched, and RECOVERY/EFS/sec_efs were unchanged. One
owner-controlled cold boot produced `rc=0 stop=0 mode=3` with the read-only
whole media (`whole=1 base=0 size=31268536320 content=GUID_partition_scheme
writable=0`) and an LBA 1 media-path read matching the same-boot baseline
(`sum=2228245566 same=1`).

Both controlled IOMedia requests at LBA 2 passed on that boot at 1-bit SDR,
400 kHz:

- the two-block request delivered 1,024 exact bytes through one two-sector
  CMD18 chunk and one clean explicit CMD12 (`two_att=1 two_ok=1 two_reason=0
  two_bytes=1024 two_mismatch=0 two_c18=1 two_c17=0 two_stops=1
  two_sfail=0`);
- the three-block request delivered 1,536 exact bytes through one two-sector
  CMD18 chunk, one clean explicit CMD12 and one CMD17 odd tail (`att=1 ok=1
  reason=0 bytes=1536 mismatch=0 c18=1 c17=1 stops=1 sfail=0`).

The raw reference and baseline reads are direct transport reads outside the
helper counters; the per-request QUAL fields bind the intentional requests,
and the aggregate counters also include incidental partition-scheme reads. The
report's `count=22 expected=84 waited_ms=0 reported=3` partition snapshot is an
unsynchronized mid-publication observation: `expected` is the same-boot GPT
entry-array slot count, `countPartitions()` stops at the first nonzero count,
and `IOGUIDPartitionScheme::start()` publishes its scan result one media at a
time. The validated GPT has 80 populated entries; partition completeness is not
claimed. The final all-zero failure record and the report panic are the
intentional report stop.

Acceptance is limited to the two actual requests measured on that one cold
boot. Arbitrary LBAs/sizes, the 64-sector ceiling, repeatability,
initialization reliability, writable media, mount/root switch, DMA/CQE/ICE,
wider/faster modes and partition completeness remain unqualified. The one-boot
budget is consumed; any further observation needs a fresh owner decision.

### 9.4 N6 reliability control and the next measurement target (2026-11-07)

N6 re-flashed the unchanged D12.6 stage-9 image (`7c76c7eb…`, payload
`e384cc06…`) BOOT-only to p69 after saving the N5 image, and one
owner-classified cold boot produced a second clean CMD3 control: `rc=0
stop=0`, one attempt, no retry, a 25-iteration poll ending in command
complete, no timeout capture and no recorded error. The typed C3L poll ticks
are normalized to `0x14e4` from the source format and the prior D12.6 report;
poll and terminal timings are distinct sequential measurements. This is a
clean control/non-reproduction, not a failure-rate estimate, a fix or proof of
reliable initialization, and it changes no storage verdict.

The next design target is the partition-publication observation. The current
reporter stops at the first nonzero count and takes one immediate snapshot, so
N5's `count=22 expected=84` is not a completeness verdict. A bounded stability
window would improve the observation but a stable count is still not proof of
completion; any such change needs host tests, a candidate build and separate
hardware approval. Initialization reliability remains a separate open issue.

### 9.5 N8 bounded partition-observation design (2026-11-07, host-only)

The N8.1 design is in
`docs/emmc-n8-partition-observation-design-2026-11-07.md`. It replaces the
first-nonzero inference with an opt-in `gta4l-emmc-partobs=1` observation: a
4,000 ms deadline, a 50 ms sampling interval and a 250 ms count-unchanged
window, all in the report thread and never under the controller gate. The
`PARTOBS` line reports first/final counts and nominal times, changes,
stability, timeout, none, error, the 84 GPT slot count and an explicit
`complete=0`. A stable count is not completeness; no completeness claim is
made without an independent validated GPT-to-`IOMedia` range/count comparison.
The legacy mode-1 and mode-3 reporter is unchanged when the token is absent;
iterator failure keeps `stop=15` and a deadline or zero count is not a failure.
Host tests and mutation controls are specified in the design; no code, build,
QEMU, device access, flash or boot is part of N8.1. Candidate production and
any hardware observation need separate approvals, and initialization
reliability remains a separate open issue.

N8.2 completed the host implementation: the opt-in token is enforced by the
selector, the bounded `PARTOBS` observation runs only in the report thread,
and `complete=0` stays explicit. The diagnostic payload builder exposes
`emmc-block-1-partobs` and `emmc-block-3-partobs`; the checker refuses orphan,
duplicate or non-1 partobs tokens and the packager accepts both names. The
host suites pass 48 core scenarios/38 mutation controls, 64 adapter scenarios
(nine partobs) with 12 adapter mutation controls and three policy mutation
controls, and 22 diagnostic payload checks. No candidate build, QEMU run,
device access, flash or boot was performed.

N8.3 then produced the host-only candidate: prelinked kernel `4dcbf24d…`,
linked archive `f9f83526…` (190 slots, 181 bindings,
`runtime_qualified=false`), candidate storage-ABI audit
`out/gta4l/storage-abi-4dcbf24d` (`a30cfcde…`), packaged kernel `023c8ff8…`,
payload `5f6759f2…` (11/11 contract, `emmc-block-3-partobs`) and BOOT image
`3d7e71af…` (18/18 gate, only the ramdisk differs from the `ce4d76e8…`
baseline). The block and storage-ABI host suites pass. No device access,
flash or boot was performed; N8.4 hardware observation is a separate
BOOT-only approval with verified EFS/CPEFS backups.

N8.4 was then approved and the candidate was flashed BOOT-only to p69. One
owner-controlled first cold boot returned the userspace `RAMROOT-FST PASS`
panic and no eMMC report, so the partition observation is inconclusive and the
boot budget is consumed; the fixture's intentional early panic can preempt the
independent eMMC report thread. The host-only correction adds an additive
`fs-timer-hold` fixture that runs the same checks and holds in a one-second
poll instead of panicking (original `fs-timer` byte-identical), and the
payload builder now requires `--data-ramdisk-manifest` naming that hold
fixture for both partobs personalities. A new candidate build, BOOT-only
flash and one boot remain separately gated.

N8.5 then built payload `28eafd24…` (11/11) and BOOT image `b4f4f1ea…`
(18/18) with the hold fixture and flashed p69 only. The pre-flash N8.3
candidate `7c22e499…` was backed up as `boot-before-n8.5.img.xz`
(`e5b43228…`); the full p69 readback `9d51fe80…` matches the candidate prefix
plus the unchanged tail `a28b4ea2…`, and RECOVERY/EFS/sec_efs are unchanged.
One owner-classified first cold boot then returned the clean mode-3 report and
TWRP: the whole read-only media, the LBA 1 baseline match and both controlled
reads succeeded, and `PARTOBS` reported `first=39 final=80 changes=2 stable=1
complete=0` over eight nominal samples. The count held for the 250 ms window
but is not an exact partition-set comparison; `complete=0` remains explicit.
The boot budget is consumed. The host-only N9.1 design for an opt-in, bounded,
read-only exact GPT-to-`IOMedia` range comparison is
`docs/emmc-n9.1-gpt-iomedia-comparison-design-2026-11-07.md`; it preserves
N8's `complete=0` and does not silently upgrade it. N9.2 then implemented
`gta4l-emmc-partcmp=1` on the host: validated primary/backup GPT, exact sorted
`(base, size)` multiset comparison against the direct GUID-scheme children,
bounded snapshots, cancellation and the `PARTMAP` line, plus the
`emmc-block-1-partcmp`/`emmc-block-3-partcmp` payload personalities and the
partobs+partcmp refusal. N9.2 performed no candidate build, QEMU run, device
access, flash or boot. N9.3 then produced the host-only `emmc-block-3-partcmp`
candidate (kernel `fd2c8c65…`, requalified archive `02cb5563…`, payload
`fd73b37f…` with a 14/14 contract and BOOT image `fa55ef61…` with an 18/18
gate) and moved the comparator scratch to one bounded allocation after the
real kernel build rejected the N9.2 object at the 16 KiB kalloc type limit.
No device access, flash or boot; N9.4 remains separately gated.

## 10. What this design does not do

- The first rung is measured for one reported boot at 1-bit SDR, 400 kHz:
  the whole `IOMedia` was published and the media-path read matched the
  same-boot baseline. It does not establish a second matching boot or any
  other width/timing; the audited partition count is consistent with the
  validated GPT, but completeness is not claimed from the report alone.
- N6 measured a second clean CMD3 control on the unchanged stage-9 candidate;
  it does not add a second mode-3 publication boot, a partition-completeness
  result or a reliability rate.
- The measured p5b publication rung does not use CMD18/CMD12, DMA, CQE, ICE,
  interrupts, DDR, HS200/HS400 or 8-bit; those remain separate rungs. The
  host-only N3 mode-3 path does use two-sector CMD18 chunks, and N5 measured
  exactly one two-block and one three-block IOMedia read on one cold boot;
  that result does not change the measured p5b rung and does not qualify
  arbitrary I/O, repeatability or reliability.
- It does not add a filesystem driver, a mount, a root switch or a write
  path.
- It does not resolve the cold-boot identification flake; that remains a
  separate open issue and every future device run keeps the four-boot stop
  budget.

## 11. References

- `profiles/os/ipados-17.7.11-21H461/profile.toml` — IOStorageFamily prelink
  root and kernelcache identity.
- `out/feeds/IOStorageFamily-67f9003c/` — public IOStorageFamily sources and
  personalities (`IOBlockStorageDriver`, `IOGUIDPartitionScheme`,
  `IOMediaBSDClient`), revision `e45fbe30…`.
- `scripts/audit-storage-interface.py`, `scripts/build-virtio-mmio-bindings.py`,
  `scripts/prepare-storage-headers.py` and `build/storage-abi/headers.json` —
  ABI qualification, concrete bindings and headers. The audit depends on the
  original candidate, `build/xnu-prelink/native-apfs-load-positive.macho` and
  its plan, `build/darwinboot-storage-native-metadata/gdb-stop.json`,
  `build/image4-ignition/aes-interface-audit.json` and the public source.
- `drivers/RamBlock/NativeRamBlock.cpp`, `drivers/VirtioMmioBlock/
  NativeVirtioBlock.cpp` — `IOBlockStorageDevice` implementation templates.
- `patches/xnu/native-aes-personality.patch` — guarded personality pattern.
- `patches/xnu/gta4l-emmc-probe.patch` — the existing `bsd_init` probe hook.
- `iokit/bsddev/IOKitBSDInit.cpp` — publishes the `IOBSD` resource;
  `bsd_init.c` calls `IOKitBSDInit()` after the probe hook.
- `IOBlockStorageDriver.cpp` — `handleStart` reads the advertised transfer
  limits; `message()` handles `kIOMessageMediaStateHasChanged` by
  decommissioning the media.
- `IOService.h` — `messageClients()`; `IOBlockStorageDevice.h` — the
  `kIOMessageMediaStateHasChanged` contract.
- `package/Gta4lEmmc.h`, `package/Gta4lEmmc.c`,
  `package/Gta4lEmmcProbe.c` — the core, the ops and the register windows.
- `docs/emmc-reference.md`, `docs/native-emmc-reference-audit.md` — the
  read-only publication shape and the MediaTek reference limits.
- `docs/emmc-driver.md` §§5.3.33–5.3.34 — the F3 completion rule and the
  measured 4-bit read path.
