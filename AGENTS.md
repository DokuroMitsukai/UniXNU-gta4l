# Device agent guide

This repository owns the gta4l board DTS, peripheral wiring, component
selections, firmware inputs, partition layout and packaging.

## Read first

Read this README and the following UniXNU documentation before changing the port:

| Task | Required reading |
| --- | --- |
| Every port | [Porting](https://unixnu.com/porting/), [device repository](https://unixnu.com/porting/device-repository), [component catalog](https://unixnu.com/reference/components), UniXNU `components.toml` |
| Hardware | [Device tree](https://unixnu.com/porting/device-tree), [matching](https://unixnu.com/drivers/device-tree-matching), `UniXNU-sm6115-common` README, `soc.toml`, DTSI |
| Component | Its site page, README, catalog build command, dependencies and source |
| Kernel | [Source and patches](https://unixnu.com/xnu/source-and-patches), [patch reference](https://unixnu.com/xnu/patch-reference), selected OS profile |
| Images | [Prelink](https://unixnu.com/concepts/prelink), [payload](https://unixnu.com/concepts/payload), [UEFI](https://unixnu.com/porting/uefi-firmware) |
| Verification | [Building](https://unixnu.com/building), [bring-up](https://unixnu.com/porting/bring-up) |
| Migration | [Migration](https://unixnu.com/porting/migration) |

## Work

Search the catalog and inspect existing component interfaces before adding code.
Match hardware scope, OS ABI and board dependencies. Shared SM6115 resources and
operations belong in `UniXNU-sm6115-common`. Reusable drivers and libraries
belong in their component repositories. Use canonical component IDs and preserve
runtime identifiers recorded in the catalog.

The board uses the SM6115 SoC source pinned in `feeds.toml`. The firmware keeps
TWRP on `RECOVERY`; never flash or wipe without the owner's approval and a
verified EFS/CPEFS backup.

Run from UniXNU:

```sh
./unixnu defconfig gta4l_defconfig -D ../UniXNU-gta4l
./unixnu fetch
./unixnu status --json
./unixnu tree
./unixnu build kernel
./unixnu build package
./unixnu build firmware --uefi ../Mu-Silicium
./unixnu build payload --unique-chip-id <hex>
./unixnu bootimg
```

For the Mu-Silicium payload, include `UniXNURamDiskDxe`, DiskIoDxe and FAT
support in the firmware DSC and FDF. Follow
[required firmware integration](https://unixnu.com/porting/uefi-firmware#required-integration)
before building the firmware and boot image.

Record commands, revisions, output paths, results and unresolved inputs.
Report host, QEMU and hardware results separately. Finish with the
[repository checklist](https://unixnu.com/porting/device-repository#completion)
and update the device pin/documentation when the shared revisions move.

## Porting record

Read `porting.toml` and the [record guide](https://unixnu.com/porting/records)
before implementation. Update each affected area's findings or evidence with
its code change. Follow the schema and completion levels for maintainer and
agent work.

## Local builds and boot diagnosis

Use the shared checkout's `local.toml` for local source overrides. Read
[boot arguments](https://unixnu.com/xnu/boot-arguments#apply-arguments) before
changing the command line. Rebuild the payload and boot image after argument
changes. Use the [green-bar diagnosis](https://unixnu.com/guide/debugging#green-bar)
for loader handoff stalls. Record the last confirmed stage and relevant logs.

## Hardware adaptation

Follow [Port a device](https://unixnu.com/porting/) in order. Read the
[driver adaptation checklist](https://unixnu.com/porting/drivers) before selecting
components. Follow each subsystem guide for board inputs, implementation paths,
archive integration and expected results. Include brightness, buttons, Wi-Fi,
DSP, battery and CPU-frequency services in the porting record.
