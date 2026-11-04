# gta4l storage publication design (F5)

Status: host-only design and survey `[S]`. No code, build, flash or boot was
performed for this document, and it does not authorize a device run. The
device-run section is a proposal that needs the same explicit BOOT-only
approval as the earlier rungs.

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
2. the GPT partition media created by the prelinked `IOGUIDPartitionScheme`,
   with their base/size/content and, where present, BSD names;
3. one known-block read performed through the published `IOMedia` path whose
   content matches the stage-A baseline;
4. no write, no mount and no root switch.

The whole-device object is the hard requirement. Per-partition objects are
the "ideally" part; if the GUID scheme does not attach, publication is
partial, not absent, and the report must say which condition failed.

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
published media for APFS containers. On this GPT it will not find any, so the
expected result is read-only probing and log noise, not a mount.

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
    `kIOReturnIOError`/`kIOReturnNoMemory`; mark `online = false` so the
    media is decommissioned rather than left half-working. The failure
    record and counters are preserved.
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
`gta4l_emmc_read_multi()`: F2 (CMD18/CMD12) is still unproven, and the
storage stack's request sizes are unknown. A per-block loop is slower but is
the exact path the stage-A/F1 content evidence covers.

Request size: the BSD/storage layer may issue large reads. The first rung
accepts up to `GTA4L_EMMC_BLOCK_MAX_BLOCKS` (proposed 64 blocks = 32 KiB) per
call and returns `kIOReturnMessageTooLarge` above it, with a counter. The
partition scheme and APFS probing read small amounts, so this bound should
not block publication. The value is measured in the first run and revisited
before any performance claim. A large request is not silently truncated.

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

`doAsyncReadWrite` is the only data path. `reportMediaState` returning false
after a read error lets the block driver decommission the media instead of
retrying reads forever.

### 3.6 Report and observation

F5 needs an observable object, not a mount. The first rung uses report mode
`gta4l-emmc-block=1`:

- After `registerService()`, start one kernel thread
  (`kernel_thread_start`, as in `drivers/ArmCpu/ArmCpu.cpp`).
- The thread polls every 100 ms for up to 20 s for an `IOMedia` with
  `Whole = true` whose provider chain contains the driver. It then gives the
  partition scheme a short settle window (about 500 ms) and snapshots:
  - driver/card state: capacity, block size, width, timing, clock, online,
    write protection;
  - the whole media: `Base`, `Size`, `Content`, `BSD Name` if present,
    `Writable`;
  - each partition media: base, size, content, BSD name;
  - counters;
  - one known-block read through `IOMedia::read()` (not the raw core) with a
    completion waiter, compared to the stage-A baseline sum for LBA 1 or
    the 21-block array.
- It then panics with a bounded `GTA4L-EMMC P5 REPORT` block (the panic is
  the delivery mechanism, as in the earlier rungs). If no media or no
  partitions appear, it panics with the failure/partial state instead of
  returning silently.

Proposed report shape (the implementation pins the exact fields and keeps
the whole block under 2048 bytes, like the existing reports):

```
GTA4L-EMMC P5 REPORT: rc=0 stop=0 cap=61071360 bs=512 wid=1 tim=0 clk=400000
GTA4L-EMMC P5 DRV: online=1 wp=1 reads=... blocks=... fails=... max=...
GTA4L-EMMC P5 MEDIA: whole=1 base=0 size=61071360 content=GUID_partition_scheme bsd=disk0
GTA4L-EMMC P5 PART: n=... entries...
GTA4L-EMMC P5 READ: lba=1 sum=... base=... same=1
GTA4L-EMMC P5 FAIL: fail=.../...
```

Stop codes: 0 clean publication and read-back; 10 the block argument is
absent or contradictory; 11 controller/card initialization failed; 12 no
whole media within the timeout; 13 whole-media geometry/property mismatch;
14 the media read-back diverged; 15 whole media exists but no GPT partition
media (partial F5); 16 the report thread timed out or the panic budget was
exceeded.

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
2. **Split archives** (recommended): `libGta4lEmmcCore.a` (core C object,
   linked once), `libGta4lEmmcProbe.a` (probe consumer) and
   `libGta4lEmmcBlock.a` (C helper + C++ adapter). Link all three with
   `-force_load`; the core symbols exist once. This keeps the probe and
   block builds independent and avoids duplicate `gta4l_emmc_init`/read
   symbols.

The split layout is preferred for the future mount/verification work. The
build script must prove there are no duplicate global symbols across the
three archives (`nm` check) and record the archive hashes in the existing
build-report style.

### 4.2 Compile records

- C core/probe/platform/helper objects: use the `bsd_init.o.json` compile
  record as `scripts/build-gta4l-emmc-probe.py` does today.
- C++ adapter: use the `IOPlatformExpert.cpo.json` record and the storage
  ABI include, as `scripts/build-arm-driver.py` does for `NativeRamBlock`
  and `NativeVirtioBlock` (`-I build/storage-abi/include`, `-Werror` and
  the kernel C++ flags).

### 4.3 Personality and kernel configuration

The board patch series currently applies `sheng-platform-power.patch`,
`gta4l-sdhci-snapshot.patch`, `gta4l-sdhci-handoff-audit.patch` and
`gta4l-emmc-probe.patch` in `bsd_init.c`. Add a board patch
`gta4l-emmc-block.patch` that appends a guarded personality to
`iokit/KernelConfigTables.cpp`, modelled on the `XNU_NATIVE_RAM_BLOCK`
block in the shared `native-aes-personality.patch`:

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
`NativeRamBlock`: the driver starts after BSD init resources exist, which is
also after the `bsd_init` probe hook, so the two never overlap when the boot
arguments are mutually exclusive.

`scripts/build-xnu-4k-baseline.sh` must translate a new
`CONFIG_XNU_NATIVE_GTA4L_EMMC_BLOCK=y` into
`-DXNU_NATIVE_GTA4L_EMMC_BLOCK=1` in `PERSONALITY_CFLAGS`, in the same style
as `XNU_NATIVE_RAM_BLOCK`. That is a small shared-repo change; the board
defconfig cannot inject the compiler flag directly. If the shared change is
not acceptable, the alternative is to define the personality in a board-only
`KernelConfigTables.cpp` patch that is only applied for this board and is
unconditional, but that advertises a class that might not be linked in other
configurations and is not recommended.

### 4.4 Boot arguments and payload

- New driver argument: `gta4l-emmc-block=0|1|2` (absent/0 off, 1 report
  mode, 2 publish-only).
- Existing probe argument: `gta4l-emmc-probe=1..17`.
- The payload contract must require exactly one of the two families, not
  just one `gta4l-emmc-probe` token. A build that sets both is refused
  before flashing.
- Add `emmc-block-1` to `scripts/build-gta4l-diagnostic-payload.py`,
  `scripts/check-gta4l-payload.py` (the names map is already the single
  source) and `scripts/package-gta4l-diagnostic.py`, mapping to
  `gta4l-emmc-block=1`.
- Add the archive paths to `configs/gta4l_defconfig`
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

The full existing battery (`test-gta4l-emmc.py`, `test-gta4l-emmc-probe.py`,
the counts test, the fixture/board/payload/package/boot-image tests, the
hash sweep, `git diff --check` and the privacy checks) still runs before any
commit.

## 6. Device-run plan (proposal, owner-gated)

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

Pass/partial/fail:

- **F5 pass** — whole-device `IOMedia` with the expected geometry and
  write-protection, GPT partition media present, and the media read-back
  matching the stage-A baseline.
- **F5 partial** — whole media present but no partition media, or a
  partition set that does not match the GPT. Publication works; the scheme
  needs a follow-up.
- **F5 fail** — no whole media, initialization failure, or a read-back
  mismatch. The report must name the stop code and the first failure
  record.

The known pre-identification CMD3 flake still applies to any boot; a
pre-publication stop is a boot fact, not an F5 verdict. No in-boot retry.

## 7. Mount question

F5 does not require a mount and this design does not add one. The expected
automatic behavior is:

- `IOGUIDPartitionScheme` reads the GPT and creates partition media;
- `IOMediaBSDClient` can create BSD disk nodes;
- APFS is prelinked and may probe the media; it will not find an APFS
  container on the Android GPT and must not mount anything;
- there is no ext4/f2fs driver in the prelinked set, and no FAT driver is
  guaranteed; no partition content is changed by a read-only probe.

A later mount test, if the owner wants one, needs a known filesystem on a
scratch region, an explicit read/write policy and a separate approval. It is
not part of F5 and must not be smuggled into the publication rung.

## 8. Risk register

| Risk | Effect | Mitigation |
| --- | --- | --- |
| Storage ABI mismatch (`IOBlockStorageDevice` size/vtable) | Link failure or runtime crash | Run the existing audit; compile against the prepared public headers; mirror the proven `NativeRamBlock` overrides; fail closed if the audit cannot run. |
| Duplicate core symbols from probe and block archives | Link failure | Split the core into `libGta4lEmmcCore.a`; `nm` duplicate check. |
| `IOBlockStorageDriver` does not match | No `IOMedia`, no F5 | Keep the base `device-type = Generic`; use the `IOResources`/`IOBSD` personality; report the IOService tree. |
| GPT scheme does not attach | Whole media only (partial F5) | Report the whole-media properties and the scheme absence; debug with the report, not a new device hypothesis. |
| CMD3 identification flake at IOBSD time | No publication on that boot | Four-boot rule; failure report; no in-boot retry. |
| Long synchronous reads / watchdog | Boot stall or timeout | Per-call cap of 64 blocks (32 KiB) and a documented refusal above it; per-block progress counters; revisit after measurement. |
| Read error wedges the controller | Media goes offline | Fail closed, mark offline, preserve the first failure record; no unproven reset/retry in the first rung. |
| APFS/filesystem probe reads a lot | Slow boot or noisy logs | Read-only, capped requests, report the observed media; no mount. |
| Both boot arguments set | Probe panics before publication, wasted boot | Contract check rejects the combination at build time; driver also refuses at start. |
| Power rails differ at IOBSD time | Initialization failure | Full controller reset and retained-rail handshake; report the failing step. |
| Report thread races media creation | No media in the report | Poll for the whole media, settle window for partitions, bounded timeout with a partial report. |
| Panic report exceeds the buffer | Truncated or missing report | Field-wise width fixture and a 2048-byte budget, as with the existing reports. |

## 9. Open owner decisions and recommendation

1. **Publication scope** — recommend whole-device plus GPT partitions,
   read-only, no mount. The whole-device object is the F5 requirement; the
   partitions are the "ideally" part.
2. **First-rung width/timing** — recommend 1-bit SDR at 400 kHz. It is the
   path with the most content evidence and the widest margin; the verified
   4-bit read path can be a later publication rung after the first success.
3. **Read scope** — the storage stack will choose LBAs, so the approval must
   cover arbitrary read-only LBAs, not the probe's pinned list. This is the
   main scope difference from the earlier rungs and should be explicit in
   the approval.
4. **Mount** — recommend no mount in F5; a mount test is a separate,
   owner-gated stage with a known filesystem and a scratch region.
5. **Report mode** — recommend `gta4l-emmc-block=1` (report + panic) for the
   first rung and `=2` (publish only) later, if interactive work needs it.

## 10. What this design does not do

- It does not add code, change the probe, build an image or touch a device.
- It does not claim F5: no `IOMedia` object has been observed yet.
- It does not use CMD18/CMD12, DMA, CQE, ICE, interrupts, DDR, HS200/HS400
  or 8-bit; those remain separate rungs.
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
- `scripts/audit-storage-interface.py`, `scripts/prepare-storage-headers.py`
  and `build/storage-abi/headers.json` — ABI qualification and headers.
- `drivers/RamBlock/NativeRamBlock.cpp`, `drivers/VirtioMmioBlock/
  NativeVirtioBlock.cpp` — `IOBlockStorageDevice` implementation templates.
- `patches/xnu/native-aes-personality.patch` — guarded personality pattern.
- `patches/xnu/gta4l-emmc-probe.patch` — the existing `bsd_init` probe hook.
- `package/Gta4lEmmc.h`, `package/Gta4lEmmc.c`,
  `package/Gta4lEmmcProbe.c` — the core, the ops and the register windows.
- `docs/emmc-reference.md`, `docs/native-emmc-reference-audit.md` — the
  read-only publication shape and the MediaTek reference limits.
- `docs/emmc-driver.md` §§5.3.33–5.3.34 — the F3 completion rule and the
  measured 4-bit read path.
