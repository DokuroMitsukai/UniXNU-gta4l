# gta4l firmware dependency decision

Status: host-only. No firmware build, flash, boot or device operation was
performed for this record. The SDHC decision remains **NO-GO**
(`sdhc-decision-memo.md`).

This note answers which firmware inputs the gta4l image actually consumes and
what the dirty `Mu_Basecore` checkout means for packaging. It is a bounded
dependency decision, not a migration or a new build result.

## Revisions

| Input | Revision | State |
| --- | --- | --- |
| Mu-Silicium checkout | `e14626a3757007a2dbc902d3db323620d5568923` | Detached `origin/integration/hero2lte-gta4l`; only the `Mu_Basecore` submodule is modified |
| `Platforms/Samsung/gta4lPkg` | same tree | Committed and clean |
| Local `Mu_Basecore` | `3c9c7b2e211e62d2a954ad760a36fe05adc43aa3` | 10 modified files (66 insertions, 15 deletions) |
| Build basecore feed | `microsoft/mu_basecore` `release/202608`, `cfb93724a06ff86ff5ba971413368a7809ee42a5` | Clean feed checkout used by the recorded build |
| `UniXNUPkg`, `DarwinBoot` | UniXNU checkout | External to Mu-Silicium; wired by DSC/FDF |

`board.toml` ends the firmware package list with `{edk2}`, which resolves to
the `edk2` feed above. `Mu_Basecore` appears nowhere in the board inputs.

## What is committed, what is external

`gta4lPkg` is committed in Mu-Silicium: `gta4l.dsc` includes
`KamortaPkg.dsc.inc`, `UniXNULib`, `UniXNU.inf`, `UniXNUDxe.inf`,
`UniXNURamDiskDxe.inf` and `DarwinBoot.inf`; `Include/APRIORI.inc` lists the
RamDisk, DiskIo, FAT, variable and USB drivers; `gta4l.fdf` includes the
Silicium FDF fragments and `Sec.inf`.

The package sources themselves come from the UniXNU checkout: `package/`
(including `UniXNUPkg`) and `drivers/DarwinBoot`. The assembled tree links
those directories, and the recorded firmware replay contains
`BootManagerMenuApp.efi`, `DarwinBoot.efi`, `UniXNU.efi`, `UniXNUDxe.efi` and
`UniXNURamDiskDxe.efi`.

## Local `Mu_Basecore` modifications

All ten files differ from `Mu_Basecore` HEAD only in the working tree; the feed
copy matches HEAD byte-for-byte for each one. The recorded gta4l firmware build
and replay used the feed, not this checkout.

| File | Change | gta4l graph |
| --- | --- | --- |
| `ArmGenericTimerPhyCounterLib.c/.inf` | `CNTFRQ` zero fallback to `PcdTimerFreqOverwrite` | `KamortaPkg.dsc.inc` sets `USE_PHYSICAL_TIMER=1`, so the Phy library is used; the PCD is never set for gta4l, so the fallback is inert |
| `ArmGenericTimerVirtCounterLib.c/.inf` | same fallback | Not selected (the `!else` branch is unused) |
| `BootManagerMenu.c/.h` | hide `UEFI Misc Device`, add Power Off and Exit | Built unconditionally and present in the FV |
| `UsbBus.h` | `USB_BUS_TPL` `TPL_NOTIFY` to `TPL_CALLBACK` | `UsbBusDxe.inf` is in APRIORI/DXE |
| `AsmMacroLib.h` | `ASM_FUNC` emits into `.text` instead of `.text.<Name>` | Every `ASM_FUNC` consumer, including `Sec.inf` and `PlatformHelper.S` |
| `AuthService.c/.inf` | block authenticated variable writes at runtime | `AuthVariableLib`, active under `ENABLE_SECUREBOOT=1` |

The timer INF changes add a dependency on `SiliciumPkg.dec`, which exists only
in the Mu-Silicium tree. The other changes are behavioral or link-layout
experiments. None is a build enabler for the current image, and the build logs
show the feed path, not `Mu_Basecore`.

## Decision

- The packaging dependency is the committed gta4lPkg, the pinned `edk2` feed
  and the external UniXNU `package/` and `DarwinBoot` sources. That is the
  reproducible input set.
- The dirty `Mu_Basecore` checkout is preserved as local work. It is not
  absorbed into packaging commits, not force-checked-out and not used as an
  undocumented build override.
- If a behavior from those files is wanted, make it a reviewed change to the
  managed feed (or to UniXNU's packages where the code belongs), then move the
  pin. Do not place the dirty tree ahead of the feed and call the result
  reproducible; that also switches basecore from `cfb9372` to `3c9c7b2e` plus
  local edits.

## Open risks

- `gta4l.dsc` sets neither `PcdUniXNURamDiskBase` nor `PcdUniXNURamDiskSize`;
  both default to zero. `UniXNURamDiskDxe` then relies on the prepared boot
  DTB's `/chosen` initrd range. The board stages that DTB through
  `scripts/prepare-gta4l-boot-dtb.py`, so this is a boot-DTB dependency, not a
  silent success path.
- The FV has little headroom (about 3.3 KB free in the current build and
  3.8 KB in the replay). Adding modules or growing `BootManagerMenu` can
  overflow `FVMAIN`.
- The tested and replay manifests differ only in `PcdDxe.efi` path copies;
  everything else is byte-identical. That is replay evidence, not a new build.
- Whether the USB TPL, `.text` merging, runtime auth guard or BootManagerMenu
  changes affect on-device Darwin boot is not established.
