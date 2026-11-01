# gta4l boot device-tree provenance

Status: host-only. No device, flash, boot, firmware or image was changed to
establish this record. The SDHC decision remains **NO-GO**
(`sdhc-decision-memo.md`).

This note resolves where the device tree appended to a gta4l boot image comes
from and records a byte-exact reproduction of the v5 audit DTB.

## Result

The appended DTB is the Mu-Silicium gta4l board FDT, not a merge of the Android
PixelExperience release `boot.img` base DTB and its `dtbo.img` overlay 00.
`bootimg` takes that FDT, applies `[[bootimg.dtb_node]]` from `board.toml` and
appends the initrd marks. The DTB/DTBO inputs named below are Android 11
artifact inputs recorded in `docs/pe-storage-stack-provenance.md`; they are not
an Apple IPSW.

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| Audit evidence DTB `out/gta4l/ram-root-sdhci-audit-evidence/boot-dtb-v5.bin` | 316,317 | `dc4c719d06e408654680524af26052d609baa14dd57da9082fd9f35c6509de9e` |
| Mu-Silicium `Resources/DTBs/gta4l.dtb`, full file | 4,619,189 | `e2ae2d671e28d9e295cc98ea3573ee6b41c46f5402e0769a417228d9fd8927fd` |
| First FDT in that file (its `totalsize`) | 316,095 | `5c4c0599ff430bec71de6bb75663eb857c30ae368823d55054fd9a90e8eb77f8` |
| PE release `boot` DTB image FDT 0 + `dtbo.img` entry 0, merged with `fdtoverlay` | 823,717 | `47ba96e5692b4006d28b6a39ef701df1e6b76f7a333eada1b686e186063ee97f` |

The merged PE DTB does not match the evidence DTB in size or bytes, so the
Android overlay path is ruled out.

The Mu-Silicium file is not a single 316 KiB image: it starts with the board
FDT (`totalsize` 316,095) and carries thirteen further FDT images plus padding
after it. The next FDT starts at byte 316,095 (`totalsize` 347,979); another
starts at 1,360,352 (`totalsize` 347,947). The bootloader appends one FDT, and
`fdtput` rewrites the first image at its `totalsize`, so only the first image
is staged.

## Reproduction

Starting from the full Mu-Silicium file:

```sh
python3.11 - <<'PY'
import struct
from pathlib import Path
data = Path('src/Mu-Silicium/Resources/DTBs/gta4l.dtb').read_bytes()
Path('/tmp/opencode/gta4l-base.dtb').write_bytes(data[:struct.unpack_from('>I', data, 4)[0]])
PY
cp /tmp/opencode/gta4l-base.dtb /tmp/opencode/gta4l-boot.dtb
fdtput -c /tmp/opencode/gta4l-boot.dtb /chosen/framebuffer@5c000000
fdtput -t x /tmp/opencode/gta4l-boot.dtb /chosen/framebuffer@5c000000 reg 0x0 0x5c000000 0x0 0x00927c00
fdtput -t i /tmp/opencode/gta4l-boot.dtb /chosen/framebuffer@5c000000 width 1200
fdtput -t i /tmp/opencode/gta4l-boot.dtb /chosen/framebuffer@5c000000 height 2000
fdtput -t i /tmp/opencode/gta4l-boot.dtb /chosen/framebuffer@5c000000 stride 4800
fdtput -t s /tmp/opencode/gta4l-boot.dtb /chosen/framebuffer@5c000000 format a8r8g8b8
fdtput -t x /tmp/opencode/gta4l-boot.dtb /chosen linux,initrd-start 0x0 0x41000000
fdtput -t x /tmp/opencode/gta4l-boot.dtb /chosen linux,initrd-end 0x0 0x46000000
sha256sum /tmp/opencode/gta4l-boot.dtb
```

The result is byte-identical to `boot-dtb-v5.bin`
(`dc4c719d…`). The same recipe run against the full 4,619,189-byte file also
produces `dc4c719d…`; `fdtput` truncates the trailing images, so truncating
first is an explicit form of the same operation.

The Mu-Silicium source FDT and the evidence DTB agree on:

- `model = "Qualcomm Technologies, Inc. Bengal SoC"`, `compatible = "qcom,bengal"`;
- `/soc/sdhci@4744000` is `qcom,sdhci-msm-v5 qcom,sdhci-msm-cqe` with status
  `disabled`;
- `qcom,msm-id = <0x1a1 0x10000 0x1bc 0x10000>` and
  `qcom,board-id = <0 0>`.

`/chosen/framebuffer@5c000000` is **not** in the unpatched Mu-Silicium source.
It exists in the evidence DTB (`reg = <0 0x5c000000 0 0x927c00>`, 1200x2000,
stride 4800, `a8r8g8b8`) because the board's `[[bootimg.dtb_node]]` entries
create it during `bootimg`. The earlier claim that all three trees contain the
node was wrong.

## Current build differences

The historical current-build DTB was the same FDT except for two values. That
artifact was not retained for this audit, so these are recorded measurements,
not freshly reproduced ones:

| Property | Evidence | Historical current build | Cause |
| --- | --- | --- | --- |
| `/chosen/linux,initrd-end` | `0x0 0x46000000` | `0x0 0x4d000000` | The initrd end is the payload size for that run (`0x41000000 + 80 MiB` versus `0x41000000 + 192 MiB`), added by `bootimg` |
| `/soc/qcom,rpm-smd/rpm-regulator-ldoa15/regulator-l15/regulator-max-microvolt` | 3 bytes `01 51 00` | 4 bytes `00 31 51 00` | Different length and value; see below |

The earlier record called the regulator pair "the same value, different DTB
serialization by the local `fdtput`/libfdt". That equivalence is withdrawn.
The Mu-Silicium source, the PE merged DTB and the evidence DTB all carry the
3-byte property `01 51 00`; the captured live FDT carries the 4-byte property
`00 31 51 00` (`0x00315100`). `0x015100` (86,272) is below the node's own
`regulator-min-microvolt` (2,920,000) and is not a valid 32-bit cell; the
4-byte value is plausible. The cause of the 3-byte encoding is unresolved and
the historical blobs are not modified to force a match.

The source FDT comes from the local Mu-Silicium checkout, whose
`Resources/DTBs/gta4l.dtb` is tracked at `cf61b472fe593802fb3229265b15b1b1f3226b30`
and clean. The boot DTB is not taken from an Apple IPSW or from the Android
release image.

## Release wiring

`board.toml` sets `[bootimg] dtb = "build/gta4l/boot.dtb"`. The firmware
preflight `scripts/prepare-gta4l-boot-dtb.py` reads `[firmware] config`,
stages `<UNIXNU_UEFI>/Resources/DTBs/<config>.dtb` truncated to its
`totalsize`, and writes it to that tree path. `unixnu release` then passes it
to the pack step, which applies the `[[bootimg.dtb_node]]` entries and appends
the initrd marks. The source and staged hashes are printed by the preflight.

This is a packaging provenance result only. It does not change eMMC behavior,
does not close the CMD7 finding and does not authorize flashing.
