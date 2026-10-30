# Arch Linux on gta4l: working-stack evidence

Status: host-only source review `[S]`. No image was downloaded, flashed, booted
or executed, and no device was connected. This note records a public working
Linux distribution for the board family; it is not an XNU result, an electrical
measurement or a microSD-boot result.

## 1. Source identity

| Item | Value |
| --- | --- |
| Repository | `https://github.com/limonabisi/archlinux-on-gta4l` |
| Default branch | `main` |
| Repository created | 2026-10-20 `[S]` |
| Last push at review | 2026-10-21 `[S]` |
| Target | Galaxy Tab A7 10.4 2020, SM-T500 (Wi-Fi), gta4l `[S]` |
| Kernel source | `limonabisi/linux-gta4l`, branch `gta4l-7.2-clean`, via the postmarketOS port `limonabisi/pmaports-gta4l` `[S]` |
| Boot chain | U-Boot with the pmOS kernel 7.2, initramfs and device tree on `boot`; Arch Linux ARM root on `userdata` `[S]` |

The repository builds an Arch Linux ARM `userdata` image from the official
Arch Linux ARM rootfs plus the gta4l parts of the postmarketOS port. Its README
also documents a prebuilt image and an Odin installation path; none of those
artifacts were downloaded or run for this note `[S]`.

## 2. What the project reports as working

The README status table reports the following as working on the SM-T500
`[S]` (project-reported, not independently reproduced here):

| Capability | Reported state |
| --- | --- |
| Boot | U-Boot plus pmOS kernel 7.2; Arch root on `userdata` |
| Display | 1200x2000 with brightness control |
| Touch | NT36523 panel tested; all 15 panel variants supported |
| GPU | Adreno 610 with freedreno and Turnip |
| Plasma Wayland and SDDM | Working, including the on-screen keyboard |
| Wi-Fi | WCN3990 |
| Bluetooth | RFCOMM and BNEP modules included |
| Speakers and microphone | Working |
| Battery and charging | Working, with a reported percentage drift |
| Offline charging | Working in U-Boot |
| USB networking and USB OTG host | Working |
| Suspend | Untested on Arch; reported working on postmarketOS |
| Hardware video decode | H.264/HEVC up to 1080p; VP9 off |
| Hardware video encode | Broken because the Samsung firmware is too old |
| Camera and GPS | No driver / no position |

## 3. Why it is relevant

This is a complete, public Linux userspace on the same board family. It
demonstrates that the display, touch, GPU, Wi-Fi, Bluetooth, audio, battery,
charging, USB and eMMC-root paths work together on gta4l under a mainline-based
kernel `[S]`. That is the working-stack reference the porting approach uses: a
software port can reproduce documented behaviour without first characterising
every rail `[S]/[C]`.

It does **not** establish:

- that the exact SM-T505N unit behaves identically `[U]`;
- the rail or pad state at XNU handoff `[U]`;
- that microSD boot works; the documented root is `userdata` on eMMC `[S]/[U]`;
- that the prebuilt image or its kernel is bit-identical to the pinned source
  revision `[U]`;
- anything about XNU, UniXNU, PMIC/SPMI access or retention `[U]`.

## 4. Use in this repository

The porting-approach reassessment cites this project as working-stack evidence
alongside the pinned mainline kernel, the vendor/LineageOS sources and the
captured firmware. It is a status and build-source reference, not an imported
code source. No code or image from the repository is vendored.

## 5. Limits

- The README is a project status page, not an independent measurement or a
  hardware qualification report `[S]/[U]`.
- The target is the Wi-Fi SM-T500 variant; the captured device is the LTE
  SM-T505N `[S]/[U]`.
- The boot medium is eMMC `userdata`; microSD boot remains unproven `[S]/[U]`.
- No electrical, thermal, current or retention data is provided `[U]`.

## 6. Reference

`https://github.com/limonabisi/archlinux-on-gta4l` (reviewed 2026-10-31).
