# Mu-Silicium firmware handoff trace

Status: static binary trace only. No firmware, XNU code, build, boot or hardware
access was changed. This records what the Mu-Silicium DXE binaries do at
`ExitBootServices` and what they do not do to the PM6125 eMMC rails.

## Inputs

| Input | Path | SHA-256 |
| --- | --- | --- |
| SDCC DXE driver | `src/Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/SdccDxe/SdccDxe.efi` | `4084bbc745d66fd81eae0caf7eacb19f7204076d4ee88adc7cda45c949d5674b` |
| PMIC DXE driver | `src/Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/PmicDxe/PmicDxeLa.efi` | `db541f6302efee1546beea9b4dee9362fca5f50bcec2f494e560c150464e32ce` |
| SPMI DXE driver | `src/Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/SPMIDxe/SPMIDxe.efi` | `50be2e1a0972843af5de67d4d0b80aba68634259849459897922716a25d3e254` |
| UEFI platform config | `src/Mu-Silicium/Binaries/gta4l/RawFiles/uefiplat.cfg` | `1b8c0b8035d8b858dc8c7a763ab3b874dd16f8221295cbab116879bcf08f9c3d` |

The PE32+ images are AArch64, three sections, and for these files the section
VMA equals the file offset: `.text` starts at `0x1000`, `SdccDxe.efi` `.data`
at `0x16000`, and `PmicDxeLa.efi` `.data` at `0x1d000`. Addresses below are
VMAs.

## Provenance in the booted firmware

The three analysed binaries are packaged byte-for-byte in the booted image
`out/gta4l/ram-root-sdhci-host-init-evidence/boot-ram-root-sdhci-host-init-v2.img`
(SHA-256 `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133`).
Its gzip kernel section decompresses to `1e8a3132...` (3,145,920 bytes): a
192-byte BootShim (`0b28d033...`) followed by the outer FV `42f900a3...`
(3,145,728 bytes). The outer FV's GUIDed LZMA section decompresses to a section
stream (`eb1aa544...`, 8,339,464 bytes) whose FV-image section is the inner FV
`3bb7847a...` (8,339,456 bytes). The appended DTB is `dc4c719d...`
(316,317 bytes).

In that inner FV the three driver FFS files carry these PE images:

| Module | Inner-FV offset | FFS size | FFS GUID | PE SHA-256 |
| --- | --- | --- | --- | --- |
| `SPMIDxe.efi` | `0x3d4b10` | `0xb032` | `2a7b4bef-80cd-49e1-b473-374ba4d673fc` | `50be2e1a...` |
| `SdccDxe.efi` | `0x3dfb48` | `0x1a058` | `f10f76db-42c1-533f-34a8-69be24653110` | `4084bbc7...` |
| `PmicDxeLa.efi` | `0x436b70` | `0x2006c` | `5776232e-082d-4b75-9a0e-fe1d13f7a5d9` | `db541f63...` |

Each PE SHA-256 equals the SHA-256 of the corresponding `.efi` file in the
Inputs table. The trace therefore describes the exact bytes that booted, not a
nearby build.

The same three modules are also byte-identical in the current Mu-Silicium build
`src/Mu-Silicium/Build/gta4lPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd` (outer FV
`b2b5bdce...`, inner FV `fe6740fa...`). Other modules in that build are not
identical: `DxeCore` is `c9075668...` in the build but `f4aefa94...` in the
booted image, and `UniXNURamDiskDxe` is `72fb8380...` in the build but
`83bb854c...` in the booted image. The provenance claim is limited to the three
analysed drivers.

A separate committed copy, `src/UniXNU-gta4l/SILICIUM_UEFI.fd`, is a
gzip-wrapped volume (outer FV `4a8ca2cd...`, inner FV `2443e52a...`). It also
contains the same three driver PE images, but its `DxeCore` is `f37f7d2b...`, so
it is not the booted image.

`scripts/extract-gta4l-firmware.py` reproduces this on the host. It is read-only
and does not build, flash or touch a device:

```sh
python3 src/UniXNU-gta4l/scripts/extract-gta4l-firmware.py \
  out/gta4l/ram-root-sdhci-host-init-evidence/boot-ram-root-sdhci-host-init-v2.img
python3 src/UniXNU-gta4l/scripts/extract-gta4l-firmware.py --fv \
  src/Mu-Silicium/Build/gta4lPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd
```

The FFS header checksum is computed with `IntegrityCheck.File` and `State` zeroed
per the UEFI PI specification, so a naive sum of the stored 24 header bytes is
not zero. The script uses the specified form; a walker that sums the stored
bytes as-is will reject these volumes even though the headers are valid.

`--validate` turns the extractor into a gate: it rejects a boot image whose
declared kernel section overruns the file and an appended DTB whose header, size
or block bounds are invalid. For the firmware volume it rejects a missing or
malformed inner FV (including an FV length that exceeds the available bytes, an
invalid FV header length or header checksum, and an extended header that
overruns the volume or hides an executable FFS), a structurally invalid FFS
header rather than silently dropping the file, a malformed section stream, a
malformed trailing outer FFS entry, non-erased trailing FV bytes, an executable
FFS with no PE image, and a malformed inner FV. Malformed boot images, truncated
or corrupt gzip streams, short GUIDed sections, and garbage volumes exit
non-zero with an `extract:` message instead of a traceback. With `--json`, the
validation status goes to stderr so stdout remains one parseable JSON document.
`scripts/test-extract-gta4l-firmware.py` pins the committed volume's expected
90-module inventory, checks the three driver PE hashes above, and exercises the
malformed paths end to end.

## SdccDxe

The `gEfiEventExitBootServicesGuid`
(`27abf055-b1b8-4c26-8048-748f37baa2df`) is present at VMA `0x160b8`.
`CreateEventEx` at `0x3600` registers notify function `0x2f28`, a thunk to
`0x81a8`, with type `0x200` (`EVT_NOTIFY_SIGNAL`) and TPL `8`. A second
ExitBootServices registration at `0x5920` uses notify `0x5888` and TPL `0x10`;
that path only locates a protocol and sets a flag.

The `0x81a8` handler walks the drive slots and processes the selected
`w19`/`w21` drives. Its significant calls are:

| Call | Observed effect |
| --- | --- |
| `0x3f04(drive)` | builds `0x00575000`; writes `0x9fe4` for controller 0, or adds `0x3fe000` and writes `0x1fe4` at `0x00973000` for controller 1; both are the SDC QDSD pad-group registers (`west+0x75000`, `south+0x73000`), not PMIC. `0x9fe4` matches the vendor `sdc1_*_on` drive/pull fields. The caller passes `w1=1`, but the callee does not use it. |
| `0x69ac(drive, 1)` | sets bit 0 of the SDHCI Power Control byte at slot offset `0x29` (bus power enable) |
| `0x6458(drive, 1)` | writes `1` to the SDHCI Software Reset byte at `0x2f`; because bus power is on, it reads `0x240`, writes the observed value to `0x248`, and writes `0x24c` with `IO_SUCCESS` (`4`) when `status & 0x3 == 0` or `BUS_SUCCESS` (`1`) otherwise; `0x24c` holds result bits, not the `0x240` request bits |
| `0x6c20(drive)` | writes vendor registers `0x21c` and `0x220` |
| `0x6d28(drive)` | clears bit 18 of vendor register `0x20c`; only for the `w19` drive |

The handler also calls clock helpers `0xfbec`, `0xf778`, `0x3f50`, `0xfa44`,
`0x9e78` and `0xfb00`; none of these touch PMIC or SPMI. `0x6458` waits for the
reset bit to self-clear (at most 10,000 reads, no delay in that loop) and for
the vendor power status to settle (at most 10,001 reads and 10,001 delay calls
in each of the status and clear loops). The handler does not write the PMIC,
SPMI, RPM, or any LDO register. The only `rpm` strings in the binary are eMMC
RPMB messages; there are no PMIC, SPMI, LDO, regulator or `vdd` strings. The
binary also does not contain the PMIC protocol GUID
`cea4489f-da0d-43d9-a3b2-766977a109f7`; its protocol-related strings include
the GCC SDCC clocks (`gcc_sdcc1_ahb_clk`, `gcc_sdcc1_apps_clk`,
`gcc_sdcc2_ahb_clk`, `gcc_sdcc2_apps_clk`) and RPMB. The PMIC binary contains
the GUID once.

The reset clears the standard power and clock control registers, and the
handler does not re-enable bus power after it. The firmware's final state at
`ExitBootServices` is therefore a host reset with the SDC1/SDC2 pads left in
the "on" configuration. The audit at `bsd_init` later reports `clk=0003` and
`pwr=00`; that is after the bootloader has run, so it is not a direct
measurement of the firmware's final register state and not a PMIC rail
measurement.

## PmicDxe

The same ExitBootServices GUID is present at VMA `0x1d058`. `CreateEventEx` at
`0x14e0` registers notify `0x14dc`, a thunk to `0x4220`. The handler:

- locates the PMIC protocol GUID
  `cea4489f-da0d-43d9-a3b2-766977a109f7` at VMA `0x1d1e8` with
  `gBS->LocateProtocol` (`gBS+0x140`) and, when the locate succeeds and the
  returned interface is non-null, calls the interface's second entry point at
  `[interface+8]`;
- treats a non-zero callback return as an error and logs
  `PmicDxe: pm_exit_cleanup` at `0x4290`/`0x42ac`; the success path logs nothing.

An earlier revision of this document called the handler "a generic PMIC protocol
callback plus an error log". That was incomplete: the callback is not a log stub
and it does issue SPMI writes. The following subsections record what it does.

### Which interface the GUID resolves to

The driver installs 18 protocol interfaces in one
`gBS->InstallMultipleProtocolInterfaces` call at `0x17d0` (`gBS+0x148`). The
argument list is built in the function at `0x1650`; decoding the `x1`–`x7` and
stack arguments gives (GUID, interface) pairs. The PMIC GUID `0x1d1e8` is the
fifteenth pair, and its interface is VMA `0x1d6b0`:

| Interface field | Value |
| --- | --- |
| `+0x00` | version `0x00010001` |
| `+0x08` | function `0x41ec` |

The exit callback is therefore `0x41ec`. It takes no arguments; the `x0`/`x1`/`x2`
values at the call site are leftovers from `LocateProtocol`.

### What the callback does

`0x41ec` calls `0x120a8` and `0x11cd4`, ORs their results, and returns
`EFI_SUCCESS` when both are zero and `EFI_DEVICE_ERROR`
(`0x8000000000000007`) otherwise — provided `0x120a8` returns. `0x120a8`
branches to non-returning self-loop paths when either of its two configuration
pointers is null (`0x120d0` → `0x121cc`, `0x120e0` → `0x12200`), so a missing
table or count can hang the exit callback instead of returning an error. Both
callees issue SPMI writes through `0x7d18`, the single-byte SPMI write helper:
it passes a value buffer and a write flag to `0x133e8` and increments a write
counter at `0x1e694`.

| Function | Operation |
| --- | --- |
| `0x120a8` | Walks a config table from `0x11c3c(0x87)` with count `0x11c3c(0x88)`, stride 6 bytes. Skips an entry when `byte[4]` bit 0 or `byte[2]` bit 0 is clear, or when the slave id in `byte[0]` is `0xe`. Writes `0xff` to `(byte[1] << 8) \| 0x16` and `(byte[1] << 8) \| 0x14` on slave `byte[0]`, then calls `0x7f18`, which reaches the `+0x28` SPMI operation. |
| `0x11cd4` | Walks a table from `0x11c3c(0x9a)` with count `0x11c3c(0x9b)`, stride 4 bytes. Keeps entries whose `(u16 >> 12) & 3` is 2 and whose `(u16 >> 8) & 0xf` index passes the presence check `0x7ab8`. Writes `(u16 >> 14)` to `(u16 << 8) \| 0xd1` on slave `(u16 >> 8) & 0xf`. |

The tables are resolved through `0x11c3c`, which dispatches to a handler
registered at `[0x1e5a0 + 0x10]` by `0x7594`. Their contents are runtime
configuration rather than data in this image, so the exact slave set cannot be
read statically.

### Where the transport actually lives

`0x7d18` does not touch the arbiter. It calls `0x133e8`, which dispatches through
the SPMI protocol handle that `0x13310` stores at `[0x1eb98]`:

- `0x13310` calls `gBS->LocateProtocol` with the SPMI protocol GUID
  `fa5f306b-f47d-4ac4-a47d-882f8204ec30` (VMA `0x1d108`) and the out pointer
  `0x1eb98`. Failure logs `Locate protocol failed` (`0x1bdc0`); a null interface
  logs `Invalid SPMI protocol handle` (`0x1bdd8`); both return `0xd`.
- `SPMIDxe.efi` installs that GUID at VMA `0x8048` with the interface at VMA
  `0x8088`: version `0x00010001` followed by five entry points.

| Interface offset | SPMIDxe entry | Role |
| --- | --- | --- |
| `+0x08` | `0x13dc` | SPMI read |
| `+0x10` | `0x1494` | SPMI write |
| `+0x18` | `0x152c` | not used by PmicDxe |
| `+0x20` | `0x15cc` | not used by PmicDxe |
| `+0x28` | `0x164c` | SPMI-arbiter interrupt status/clear (see `spmi-0x28-analysis.md`) |

On the PmicDxe side the three wrappers are `0x1337c` (read), `0x133e8` (write) and
`0x134b4` (`+0x28`); each logs `SPMI protocol handle is invalid` and returns `0xe`
when the handle is null.

Inside `SPMIDxe` the entry points are thin:

| Entry | Behaviour |
| --- | --- |
| `0x13dc` read | rejects a null handle with `0xc`; raises TPL to `8` via `gBS->RaiseTPL` (`gBS` at `[0x81b8]`, entry `+0x18`); calls `0x2730(...)`; restores the saved TPL with `gBS+0x20`; copies the transaction status out |
| `0x1494` write | same raise/call/restore shape |
| `0x164c` (`+0x28`) | same raise/restore shape around `0x2d48`, which resolves two helper values and calls `0x2e20` |
| `0x2730` | serialises through the object at `[0x8870]` (acquire `0x21ac`, release `0x21e0`, gated on a `0x10005` version check), calls `0x27bc` with op `1` |
| `0x27bc` | the transaction core; a zero flag at `[0x84f8]` short-circuits to the error path at `0x2954` |

The `+0x28` operation is **not** a queue drain or commit. `0x2e20` reads the
arbiter owner/access-status bitmap, iterates the selected APID positions, reads
`irq_status(n)`, reads `irq_clear(n)`, replaces its low eight bits with the
status byte and writes the whole word back. The offsets match the pinned Linux
PMIC-arbiter v5 accessors and the initial `.data` base is `0x01C00000`, so this
is arbiter interrupt handling, not a PMIC regulator
access. The entry discards its handle after the null check and operates on the
global configuration, and the same routine also runs during driver
initialisation. An earlier revision called it a flush and called the transport
"fully resolved"; both were wrong. See `spmi-0x28-analysis.md` for the
instruction trace and register derivation.

`[S]` During initialization the same `0x2e20` routine runs via
`0x266c → 0x2d90`, and `0x2d90` then calls `0x2ec8` with `w3 = 0`, which clears
access-enable bit 0 for mapping entries that pass its checks. The exit callback
does not call `0x2ec8`.

The transaction core also has a recovery path. `0x27bc` can call `0x3904`, which
reaches `0x39ec`, `0x3350` and `0x31d0`; when its configuration and failure
conditions permit, that path writes the arbiter owner and APID mapping registers
at `base+apid*4+0xa700` and `base+apid*4+0x40900` (`0x3ab8`, `0x3b0c`) and
retries. It is not limited to software channel-mapping structures. This does not
prove that recovery ran during the recorded boot, but it does mean "one command,
bounded wait, return" is not a complete description of this transport. The
mapping-walk loops depend on table contents, so a transaction polling limit does
not bound the whole call graph.

Inside `SPMIDxe`, the strings `SPMI bus busy` and `Timeout while executing SPMI
transaction` are the only visible failure reporting.

**Reachability of the exit path.** `0x120a8` calls `0x7d18` (write) and `0x7f18`,
which reaches `0x134b4` (`+0x28`). `0x11cd4` calls `0x7d18` (write). The
`ExitBootServices` callback therefore reaches **the write entry and the `+0x28`
entry only**; it never issues an SPMI read, and it never enters the `+0x18` or
`+0x20` entries.

### What the write addresses can and cannot reach

An SPMI address is `(peripheral << 8) | register_offset`, and every arbiter
version derives the target from exactly those fields. The peripheral byte selects
the block; the register byte selects the register inside it. Every address this
path writes has a **register byte** of `0x14`, `0x16` or `0xd1`.

The `0xd1` write is transcribed exactly as `(u16 << 8) | 0xd1` with slave
`(u16 >> 8) & 0xf`. That expression is not reconciled with the 16-bit
`(peripheral << 8) | register` model, so the peripheral reached by that write
remains unresolved `[S]/[U]`. The direct-address exclusion below does not depend
on resolving it: the register byte is `0xd1`, which is not a documented LDO
control register.

The PM6125 LDO control registers are registers `0x40` (range), `0x41` (voltage
set), `0x45` (mode), `0x46` (enable), `0x48` (pull-down), `0x4c` (soft start) and
`0x61` (step control) of their respective peripherals.

No write from this path can therefore land on L11 at `0x4a46`, `0x4a41` or
`0x4a45`, or on L24 at `0x5746`, `0x5741` or `0x5745`, whatever the runtime
tables contain. The map itself is recorded in `pm6125-register-map.md`.

The remaining uncertainty is narrower than "could it touch a rail": a write to
register `0x14` of an LDO peripheral is still a write into an LDO block, and the
common layout does not define `0x14`. That register is therefore unspecified, not
proven harmless. The address exclusion shows only that these writes do not target
the known `0x40`/`0x41`/`0x45`/`0x46`/`0x48`/`0x4c`/`0x61` control registers; it
does not establish that an unspecified register cannot have an indirect effect
on enable, voltage, mode or range.

For peripherals that implement the PMIC interrupt layout, the pinned
`spmi-pmic-arb.c` defines `0x14` as `QPNPINT_REG_LATCHED_CLR` and `0x16` as
`QPNPINT_REG_EN_CLR`. So the `0xff` writes are, on that layout, **clearing
latched interrupt bits and clearing interrupt enables**. That is a real state
change: it can drop fault indications and mask interrupt delivery. It is not
automatically harmless, and whether PM6125's LDO peripherals expose this layout
is not established.

The second table's value is `u16 >> 14`, not necessarily zero: the entry filter
tests bits 12–13, so the written value can be `0`–`3`. An earlier revision
described it as a zero-value write; that was wrong.

Neither write is a control-register write, but the safe statement is limited to
the addresses they can reach. It is not a claim that the callback leaves the
rails unchanged.

This is a stronger result than the earlier "no rail name appears in the exit
path" argument, because the exclusion now rests on the address encoding rather
than on the absence of strings. The binary still contains no `L24`, `L11`, `LDO`
or `vdd` name in the exit path, and the LDO driver it does contain
(`pm_ldo_driver.c`) is not called from it.

## Bounded handoff timeline and side-effect map

This pass reconciles the three traced drivers and performs a bounded scan of the
locally available firmware inventory. An expanded pass now disassembles every
`ExitBootServices` registration in the shipping set and follows each
notification body. The scan covers the 90-module local volume
`src/UniXNU-gta4l/SILICIUM_UEFI.fd` (file SHA-256 `3d5dbd6f...`; decompressed
source `7c20a5b9...`, outer FV `4a8ca2cd...`, inner FV `2443e52a...`) `[S]`.
That volume is not the booted image (outer `42f900a3...`, inner `3bb7847a...`),
but its three driver PEs are byte-identical to the booted drivers `[S]`.

The `gEfiEventExitBootServicesGuid` bytes occur in 11 modules of the local
volume: `DxeCore`, `CapsuleRuntimeDxe`, `EnvDxe`, `GlinkDxe`, `NpaDxe`,
`SdccDxe`, `PmicDxe`, `UsbfnDwc3Dxe`, `XhciDxe`, `UsbMsdDxe` and
`UsbConfigDxe` `[S]`. `DxeCore` only carries the GUID for its own core handling
and registers no callback `[S]`. The other ten each register at least one
notification; the inventory below records the registration site, the
notification body and the reached effects `[S]`. `SPMIDxe` is reached indirectly
through the PMIC protocol `[S]`.

### EBS-callback inventory

Each row is a static disassembly of the registration site and the notification
body. "Reached set" is the transitive `bl`/`b` closure from the body, so it
over-approximates when a body ends in `br` rather than `ret`. No module below
references the PMIC protocol GUID (`cea4489f...`) or the SPMI protocol GUID
(`fa5f306b...`) except `PmicDxe` itself `[S]`.

| Module | Register site → body | Reached effects | Rail verdict |
| --- | --- | --- | --- |
| `SdccDxe` | `0x360c` → `0x2f28` → `0x81a8` | TLMM page writes; SDHCI `0x29`/`0x2f`/`0x240`/`0x248`/`0x24c`/`0x21c`/`0x220`/`0x20c`; no PMIC/SPMI/RPM call | No direct L11/L24 control write `[S]` |
| `SdccDxe` | `0x5690` → `0x55b0` | Sets flag byte `0x17291`; reach=1 | No rail effect `[S]` |
| `SdccDxe` | `0x5920` → `0x5888` → `0x5804` | Guarded `LocateProtocol(gEfiStatusCodeRuntimeProtocolGuid)` via `gBS+0x140`; nulls the pointer on failure; sets flag `0x172b8` | Status-code lookup only `[S]` |
| `EnvDxe` | `0x174c` → `0x1620` | TZ-milestone call and event close; no PMIC/SPMI/RPM GUID or power string in the 126-function reached set | No rail effect found `[S]` |
| `EnvDxe` | `0x44b4` → `0x43d4` | Sets flag byte `0xe339`; reach=1 | No rail effect `[S]` |
| `EnvDxe` | `0x4820` → `0x4780` → `0x46fc` | Guarded `LocateProtocol(gEfiStatusCodeRuntimeProtocolGuid)`; sets flag `0xe360` | Status-code lookup only `[S]` |
| `PmicDxe` | `0x14f0` → `0x14dc` → `0x4220` | `LocateProtocol(PMIC guid 0x1d1e8)`; interface `+0x08` = `0x41ec` → `0x120a8`/`0x11cd4` (SPMI writes to `0x14`/`0x16` and `0xd1`); error path logs via `0x3e70`/`0x4194`/`0x4510` | No direct L11/L24 control write `[S]`; arbiter/indirect effects `[U]` |
| `GlinkDxe` | `0x13dc` → `0x146c` → `0x4a04` | Builds a GLINK RPM port (`"rpm"`, `"glink_ssr"`, `"RPM"`) and calls the port open/register path `0x2214`; no PMIC/SPMI GUID and no NPA/RPM regulator request found | No direct rail write `[S]`; RPM-side effect `[U]` |
| `NpaDxe` | `0x149c` | Calls the NPA protocol method at interface `+0x20`, then `gBS->UninstallMultipleProtocolInterfaces`; no PMIC/SPMI GUID | NPA teardown only `[S]` |
| `CapsuleRuntimeDxe` | `0x102bc` → `0x10708` | Sets flag `0x30ff9`; calls `0x10c40`; prints via `0x103d0` with string `0x20b10`; no PMIC/SPMI/RPM | No rail effect found `[S]` |
| `UsbConfigDxe` | `0x16e4` | USB configuration teardown; no PMIC/SPMI GUID or power string in the reached set | No rail effect found `[S]` |
| `UsbMsdDxe` | `0x15b4` | USB mass-storage teardown; no PMIC/SPMI GUID or power string in the reached set | No rail effect found `[S]` |
| `UsbfnDwc3Dxe` | `0x1bb0` | USB device-controller teardown; no PMIC/SPMI GUID or power string in the reached set | No rail effect found `[S]` |
| `XhciDxe` | `0x1e6c` → `0x49e4` | xHCI teardown: cancels controller timers and calls `0x5500`; no PMIC/SPMI GUID or power string in the reached set | No rail effect found `[S]` |

`gEfiStatusCodeRuntimeProtocolGuid` is `D2B2B828-0826-48A7-B3DF-983C006024F0`.
Its use by the `SdccDxe` and `EnvDxe` notifications is a status-code lookup, not
a PMIC/SPMI access `[S]`. This resolves the earlier `[U]` on those two
`LocateProtocol` targets.

The `PmicDxe` row is the important one: the notification does reach SPMI, but
only through the protocol method `0x41ec` and only with register bytes `0x14`,
`0x16` and `0xd1`. It is the same path recorded in the timeline and side-effect
map below, not a new write `[S]`. The `GlinkDxe` and `NpaDxe` rows touch the
RPM/NPA transport layer, so their indirect effect on a rail is left `[U]`; they
do not build an L11/L24 resource request in the traced body `[S]`.

### Timeline

| Stage | Path | Direct effect | Indirect effect | Runtime-dependent / unresolved |
| --- | --- | --- | --- | --- |
| Pre-handoff XBL/RPM | Not traced in this pass | Initial L11/L24 programming is not established | None established | Initial, active, sleep and reset policy `[U]` |
| DXE initialization (before `ExitBootServices`) | `SPMIDxe` `0x266c` → `0x2d90` → `0x2ec8(w3=0)` | Arbiter interrupt status/clear; clears access-enable bit 0 for entries that pass its checks | None observed | Mapping-table contents and APID set `[U]` |
| `ExitBootServices` notification, TPL 8 | `SdccDxe` `0x2f28` → `0x81a8` | SDC QDSD pad writes `0x9fe4`/`0x1fe4`; SDHCI power, reset and vendor register writes | Clock helpers; no PMIC/SPMI/RPM call found | Selected drives and final register state `[U]`; pad encoding resolved `[S]` |
| `ExitBootServices` notification, TPL `0x10` | `SdccDxe` `0x5888` → `0x5804` | Guarded status-code `LocateProtocol`; sets flag `0x172b8` | None observed | Status-code protocol only `[S]` |
| `ExitBootServices` notification | `PmicDxe` `0x14dc` → `0x4220` → `0x41ec` → `0x120a8`/`0x11cd4` | SPMI writes to register bytes `0x14`, `0x16` and `0xd1`; `+0x28` arbiter interrupt status/clear | SPMIDxe protocol; arbiter recovery path `0x27bc` → `0x3904` | Slave set, APID bitmap, PM6125 `QPNPINT` applicability, `0xd1` target, null-config hang `[U]` |
| `ExitBootServices` notification | `EnvDxe` `0x1620` | TZ-milestone call, event close, environment teardown | None to PMIC/SPMI/RPM found | Milestone target and failure path `[U]` |
| `ExitBootServices` notification | `EnvDxe` `0x43d4` | Sets flag `0xe339` | None | No rail effect `[S]` |
| `ExitBootServices` notification | `EnvDxe` `0x4780` → `0x46fc` | Guarded status-code `LocateProtocol`; sets flag `0xe360` | None to PMIC/SPMI/RPM found | None `[S]` |
| `ExitBootServices` notification | `CapsuleRuntimeDxe` `0x10708` | Sets flag `0x30ff9`; capsule cleanup; status print | None to PMIC/SPMI/RPM found | Capsule state only `[S]` |
| `ExitBootServices` notification | `GlinkDxe` `0x146c` → `0x4a04` | GLINK RPM port registration/teardown (`"rpm"`, `"glink_ssr"`) | GLINK/RPM transport; no NPA regulator request found | RPM-side effect `[U]` |
| `ExitBootServices` notification | `NpaDxe` `0x149c` | NPA protocol `+0x20` call; uninstalls protocol interfaces | NPA teardown | NPA-side effect `[U]` |
| `ExitBootServices` notification | `UsbConfigDxe`/`UsbMsdDxe`/`UsbfnDwc3Dxe`/`XhciDxe` | USB controller teardown; no PMIC/SPMI GUID or power string found | None to PMIC/SPMI/RPM found | No rail effect found `[S]` |
| Handoff to XNU | Not traced | Rail and pad state at entry is not measured | None established | Retention and pad state `[U]` |

The relative order of the notifications is not established from the static
images. They register for the same event, and the runtime order depends on
registration and TPL; the timeline therefore does not assert an order between
the drivers `[U]`.

### Side-effect map

| Path | Direct MMIO/SPMI | Indirect protocol calls | Runtime branches / unresolved targets | Direct L11/L24 control write? |
| --- | --- | --- | --- | --- |
| `SdccDxe` `0x81a8` | SDC QDSD pad writes; SDHCI `0x29`, `0x2f`, `0x240`/`0x248`/`0x24c`, `0x21c`/`0x220`, `0x20c` | None to PMIC/SPMI/RPM found | Selected `w19`/`w21` drives; pad encoding resolved; vendor values | No `[S]` |
| `SdccDxe` `0x5888` | None observed | Guarded `LocateProtocol(gEfiStatusCodeRuntimeProtocolGuid)`; nulls the pointer on failure | Status-code protocol only; flag `0x172b8` `[S]` | No `[S]` |
| `SdccDxe` `0x55b0` | None observed | None | Flag `0x17291` only `[S]` | No `[S]` |
| `EnvDxe` `0x1620` | None observed | TZ milestone, event close, environment teardown | Milestone target `[U]`; no PMIC/SPMI/RPM | No `[S]` |
| `EnvDxe` `0x43d4` | None observed | None | Flag `0xe339` only `[S]` | No `[S]` |
| `EnvDxe` `0x4780` → `0x46fc` | None observed | Guarded `LocateProtocol(gEfiStatusCodeRuntimeProtocolGuid)` | Status-code protocol only; flag `0xe360` `[S]` | No `[S]` |
| `CapsuleRuntimeDxe` `0x10708` | None observed | Capsule cleanup; status print | Capsule state only `[S]` | No `[S]` |
| `GlinkDxe` `0x146c` → `0x4a04` | None observed | GLINK RPM port registration/teardown (`"rpm"`, `"glink_ssr"`) | No NPA regulator request in the traced body `[S]`; RPM-side effect `[U]` | No `[S]` |
| `NpaDxe` `0x149c` | None observed | NPA protocol `+0x20`; `UninstallMultipleProtocolInterfaces` | NPA teardown `[S]` | No `[S]` |
| `UsbConfigDxe`/`UsbMsdDxe`/`UsbfnDwc3Dxe`/`XhciDxe` | None observed | USB controller teardown | No PMIC/SPMI GUID or power string found `[S]` | No `[S]` |
| `PmicDxe` `0x120a8` | SPMI `0xff` writes to `(slave << 8) \| 0x16` and `\| 0x14` | `0x7f18` reaches the `+0x28` arbiter entry | Table `0x11c3c(0x87/0x88)`, stride 6; slave set `[U]` | No known control register `[S]` |
| `PmicDxe` `0x11cd4` | SPMI write `(u16 >> 14)` to `(u16 << 8) \| 0xd1` | None beyond the SPMI write wrapper | Table `0x11c3c(0x9a/0x9b)`, stride 4; `0xd1` target `[U]` | No known control register `[S]` |
| `SPMIDxe` `0x2e20` (`+0x28`) | Arbiter `irq_status`/`irq_clear` read-modify-write | Reached from PmicDxe | APID owner/status bitmap contents `[U]` | No `[S]` |
| `SPMIDxe` `0x2ec8(w3=0)` (initialization) | Clears arbiter access-enable bit 0 for entries that pass checks | Shared initialization path, not an exit callback | Mapping-table contents `[U]` | No `[S]` |
| `SPMIDxe` `0x3904` (recovery) | Writes arbiter owner/APID registers at `base+apid*4+0xa700` and `base+apid*4+0x40900` | Reached from the transaction core `0x27bc` when conditions permit | Whether recovery ran during the recorded boot `[U]` | No `[S]` |

**Boundary.** The supportable statement is: no direct L11/L24 control-register
write was found in any traced `ExitBootServices` notification body `[S]`. It is
not: the handoff cannot affect the rails `[U]`. The runtime SPMI tables, the
APID bitmap, the unresolved `0xd1` target, the PM6125 `QPNPINT` applicability,
the non-returning null-configuration path in `0x120a8`, and the RPM/NPA
transport effects of the `GlinkDxe` and `NpaDxe` bodies all remain open.

Modules that contain power-related strings but do **not** register an
`ExitBootServices` notification (`ClockDxe`/`VcsDxe` for `vdd`; `UFSDxe` for
RPM; `DALSys` for `L11`/RPM/PMIC/SPMI; `BdsDxe`/`SimpleFbDxe` for PMIC/SPMI;
`AdcDxe` for SPMI) are outside this inventory. Their own protocol GUIDs are not
referenced by any traced EBS body, so they are not reached through a GUID lookup
from an EBS callback `[S]`; an indirect dispatch through a non-GUID interface is
not fully excluded `[U]`. This is a reason to keep the boundary rather than to claim
a negative `[S]/[U]`. Expanding this into unrestricted reversing is out of scope
for this pass.

## Platform config

`uefiplat.cfg` has `Sdc1GpioConfigOn = 0x1E92`,
`Sdc1GpioConfigOff = 0xA00`, `Sdc2GpioConfigOn = 0x1E92`,
`Sdc2GpioConfigOff = 0xA00`, and `EnableSDHCSwitch = 0x1`. These are TLMM pad
and SDHC switch settings. The register write decoded in the `SdccDxe` exit
handler is `0x9fe4`/`0x1fe4`, not these configuration-map values; the
relationship between the two remains `[U]`. The file has no LDO, `vdd`, `vreg`
or regulator entry. The only PMIC reference is the `PMIC ARB SPMI` MMIO region.

## What this proves

- The firmware's SDCC exit path is a controller and pad path. It enables bus
  power, runs the v5 reset/power handshake, and writes vendor registers; the
  reset clears the standard power and clock registers and the handler does not
  re-enable bus power. It has no PMIC, SPMI, LDO or RPM-request operation.
- The firmware's PMIC exit path does reach SPMI, but only with register byte
  `0x14`, `0x16` or `0xd1`. Its writes cannot directly address the L11/L24
  range, voltage, mode or enable registers, which are registers `0x40`, `0x41`,
  `0x45`, `0x46`, `0x48`, `0x4c` and `0x61` of their peripherals.
- All ten modules that register an `ExitBootServices` notification were traced
  to their bodies. None of them except `PmicDxe` locates the PMIC or SPMI
  protocol. `SdccDxe`, `EnvDxe`, `CapsuleRuntimeDxe` and the four USB modules
  set flags, do status-code lookups or tear down their own controllers;
  `GlinkDxe` and `NpaDxe` only touch the GLINK/RPM/NPA transport layer.
- The UEFI platform config does not configure L24/L11.

## What this does not prove

- It does not measure the L24/L11 voltage or enable state. This trace does not
  establish the rail state at XNU handoff. If the XBL/RPM programming is retained
  through `ExitBootServices`, the rails remain in that programmed state; that
  retention is not proven here.
- It does not establish the exact slave set the PMIC exit path writes. The
  tables are runtime configuration resolved through `0x11c3c`, so only the
  register offsets are provable from this image.
- It does not establish the effect of writing `0xff` to registers `0x14`/`0x16`.
  For peripherals with the PMIC interrupt layout those are
  `QPNPINT_REG_LATCHED_CLR` and `QPNPINT_REG_EN_CLR`, so the writes can clear
  latched interrupts and mask interrupt delivery. PM6125-specific applicability
  is not established.
- It does not establish which APIDs are set in the `intr` owner-status bitmap at a
  given moment, and therefore which APIDs `0x2e20` actually processes. The
  operation itself is resolved as the arbiter v5 `irq_status`/`irq_clear` pair,
  so the callback's traced direct effect set is bounded to arbiter interrupt
  handling plus the traced register writes; indirect paths are still not
  excluded.
- It does not establish what hardware returns when `irq_clear(n)` is read. The
  routine replaces bits 7:0 with the status byte and writes back bits 31:8
  unchanged, so the effect of that read-modify-write is not established.
- It does not establish that the callback has no indirect effect on the rails.
  The constants exclude direct writes to the listed control registers by the
  traced callees; they do not exclude every indirect path.
- It does not establish the RPM-side effect of the `GlinkDxe` GLINK port
  registration or the NPA-side effect of the `NpaDxe` protocol teardown. Neither
  body builds an L11/L24 resource request in the traced instructions, but the
  remote RPM firmware's handling of the transport is not visible here `[U]`.
- The inventory covers the 11 modules that carry the EBS GUID in the local
  shipping volume. It does not cover a different firmware revision, a
  runtime-loaded driver, or a protocol method that an EBS body reaches only
  through a non-GUID dispatch `[U]`.
- It does not establish the complete arbiter command sequence. PmicDxe delegates
  to the SPMI protocol, so the arbiter access lives in `SPMIDxe.efi`; the entry
  points and `0x2e20` are traced here and in `spmi-0x28-analysis.md`, but the
  command/status/data sequence inside `0x27bc`/`0x3530` is not fully re-derived.
  For the hardware side, the mainline `spmi-pmic-arb.c` trace in
  `pm6125-register-map.md` is the reference.
- It does not prove that the bootloader or XNU leaves the rails on. The live
  FDT's `qcom,init-voltage` and `always-on` properties are board description,
  not a readback.
- It does not replace the `sdhci-handoff-audit.md` controller observations, the
  `rpm-power-interface.md` RPM/regulator source trace, or the
  `pm6125-register-map.md` SPMI readback map.

## Safety conclusion

The firmware trace supports a precise but limited claim: no traced
`ExitBootServices` notification body writes an L11/L24 control register, and the
only SPMI-writing body (`PmicDxe`) can reach only register bytes `0x14`, `0x16`
or `0xd1`, while the control registers are `0x40`, `0x41`, `0x45`, `0x46`,
`0x48`, `0x4c` and `0x61`. The other nine registering modules set flags, do
status-code lookups, tear down their own controllers, or touch only the
GLINK/RPM/NPA transport.

It does **not** support "the callback cannot change the rails". The `+0x28`
operation is arbiter interrupt status/clear, not a regulator access, but the
runtime bitmap contents, the RPM/NPA transport effects and any other indirect
path remain unresolved, and the trace shows the handoff is not PMIC-passive. A
software-visible success at the RAM-root stage is not evidence of electrical
safety either; software rollback cannot repair electrical damage.

Keep bus power and the card clock off, issue no card command, DMA or storage
write, and do not acknowledge `CORE_PWRCTL_STATUS` from XNU. The remaining
evidence gaps are the written electrical contract and a direct rail measurement;
the SPMI readback that `pm6125-register-map.md` maps out would report commanded
state only and is itself blocked. See `pmic-audit.md` for the audit record and
the outstanding blockers.

## Reproduction

```sh
llvm-objdump -d --no-show-raw-insn \
  src/Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/SdccDxe/SdccDxe.efi
llvm-objdump -d --no-show-raw-insn \
  src/Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/PmicDxe/PmicDxeLa.efi
```

Search the PE image for the little-endian ExitBootServices GUID bytes
`55f0ab27b8b1264c8048748f37baa2df`, then read the `CreateEventEx` arguments at
the referencing site. For these images the section VMA equals the file offset.

For the PMIC protocol, search for the little-endian GUID bytes
`9f48a4ce0ddad943a3b2766977a109f7`. It occurs once, at VMA `0x1d1e8`. The
interface for that GUID is the second element of the pair whose first element is
`0x1d1e8`, which is VMA `0x1d6b0`. Do not pair the GUID with the structure that
precedes it in the stack copy (`0x1d3b8`): that structure is the interface for
the preceding GUID at `0x1d0b8`. Recover the pairing by tracking the `adrp`/`add`
values per register and the `stp` offsets in the function at `0x1650`, then read
`[interface+8]`.

For the SPMI transport, search `PmicDxeLa.efi` for the little-endian GUID bytes
`6b305ffa7df4c44aa47d882f8204ec30` (VMA `0x1d108`), then read the
`LocateProtocol` arguments at `0x13310`. The same GUID appears in `SPMIDxe.efi`
at file offset `0x8048`; the `InstallMultipleProtocolInterfaces` call at `0x16e8`
passes it with the interface at `0x8088`.

The EBS-callback inventory is reproduced by scanning every `bootmods/*.efi`
image for the EBS GUID bytes, resolving each `adrp`/`add` pair to its target,
then following `bl`/`b` from the notification body. Resolve an `adrp` target as
the page base and add the immediate only from the matching `add`; do not treat a
page-relative `adrp` result as a full address. The status-code GUID bytes are
`28b8b2d22608a748b3df983c006024f0` (`gEfiStatusCodeRuntimeProtocolGuid`); its
two EBS uses are the guarded `LocateProtocol` calls in `SdccDxe` `0x5804` and
`EnvDxe` `0x46fc`.
