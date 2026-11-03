# UniXNU for the Galaxy Tab A7 10.4 (gta4l)

Samsung Galaxy Tab A7 10.4 (2020), SM-T505N, Qualcomm SM6115 (Bengal).
This repository owns the board DTS, peripheral wiring, firmware inputs,
partition layout, payload policy and packaging.

## Status

- Format 2 configuration: `schema_version = 2`, pinned `sm6115` SoC source,
  iPadOS 17.7.11 OS profile, layered ARM64/SoC/OS/device inputs and board DTS.
- The shared SM6115 tree describes eight CPUs, PSCI 1.0 SMC, GICv3 at
  `0xf200000` and the 19.2 MHz timer. UFS is described but disabled.
- The kernel host build and prelink pass from the regenerated Format 2
  `.config` (`CONFIG_XNU_NATIVE_PAN=n`): the PMGR-linked
  `out/gta4l/ipados-17.7.11-21H461/gta4l/kernel/kernel.macho`
  is a 49 MiB Mach-O arm64 image with SHA-256
  `dc8184da757b5323d2f4fb6c7ac1d588790b3d44d583c6d6fd24897cdbbcfe60`.
  See [docs/build-revisions.md](docs/build-revisions.md#kernel-build) for the
  earlier failures and the no-bridge hash.
- The original-PMGR bridge is adapted with credit from TB128FU; its
  AMFI/PMGR contract audit passes, and the kernel contains
  `ShengPlatformPower`, `sheng_pmgr_run` and the `bsd_init` hook. A QEMU run
  with `sheng-pmgr=2` and no AMFI bypass reaches `_bsd_init`,
  `_sheng_pmgr_run` and `_IOKitBSDInit`, then stalls at `_ml_get_timebase`
  before `_vfs_mountroot`. This is host evidence, not a tablet boot.
- The firmware builds with `ENABLE_SECUREBOOT=1`. The PMGR-linked kernel was
  packaged into the RAM-root fs/timer diagnostic image
  (`c12e601e…`, 85,688,320 bytes), flashed and passed on the tablet as a
  bounded fixture; this is not a normal-userspace boot. The fixture artifacts
  reproduce byte-for-byte on Linux; see
  [docs/ram-root-fixture-reproducibility.md](docs/ram-root-fixture-reproducibility.md).
- Display, input, power, GPU and network drivers remain unimplemented or
  unvalidated.
- A native SDHCI v5/eMMC core is implemented and host-tested in
  `package/Gta4lEmmc.c`: controller reset, GCC 400 kHz, retained-rail power
  handshake, CMD3 RCA assignment, enumeration, capacity/partition validation
  and single-block PIO reads. The opt-in `gta4l-emmc-probe=1/2/3` XNU probe is
  built into the kernel archive list. Stage 2 (controller and host init)
  completed on the tablet. Stage 3 (identification) first failed at CMD2 with
  a response CRC error; a later `cmd2window` run passed CMD2 and stopped at a
  CMD3 completion timeout, and the `fmt=2` run passed CMD3 and CMD9 and stopped
  at a CMD7 R1b completion timeout with TC set and CC absent. The `fmt=4`
  fail-closed image was run on 2026-11-01 on two units (an SM-T505 and the
  owner's SM-T505N, per the owner); both failed at CMD2 with the same `-8`
  record, so its CMD7 capture was not exercised. A later 2026-11-01
  `cmd1gap` candidate added only an 8-us delay after an accepted ready CMD1;
  it completed the full stage-3 identification sequence (`rc=0`, `step=15`,
  CMD7 CC and TC observed) on three units (the owner's SM-T505N and two
  owner-reported SM-T505s). The result is owner-supplied text without a photo
  file in the tree, supports a timing-sensitive contribution but does not
  establish the CMD2 cause, and the delay is not proven necessary. On
  2026-11-02 the stage-1 image, built from the same source, completed
  identification and both read-only `CMD17` PIO reads (blocks 0 and 1) on the
  owner's SM-T505N and one of those SM-T505s. Both reports show `step=15`,
  `log=7`, a protective-MBR type `0xEE` entry starting at LBA 1 in block 0,
  the `EFI PART` GPT header in block 1 and the recovery-known CSD; the owner's
  unit also shows the recovery-known CID. The T505N BOOT-only flash and
  readback record is under
  `out/gta4l/ram-root-emmc-probe-stage1-evidence/`; the SM-T505 run has no
  flash record in the tree and its image identity is owner-reported/shared.
  This is first sector-read content evidence, not a filesystem, block-device,
  multi-block, DMA, CQE, ICE or write result, and CRC checking remains enabled.
  An owner-authorized read-only recovery-side readback of blocks 0 and 1 on
  the owner's SM-T505N independently reproduces the MBR signature and GPT
  header CRC (`docs/emmc-cmd2-crc-audit.md` §2.12; sector hashes stay in
  `out/`). On the same day the stage-A image read the full 84-entry primary
  GPT array (LBAs 2..22), the first usable sector (LBA 34) and the backup GPT
  header (LBA 61,071,359) with repeated `CMD17` reads at the identification
  clock. The report (`step=18`, `log=30`) matched a same-session read-only
  TWRP readback of the same sectors and the pinned recovery GPT dumps; the
  84 × 128-byte array spans 21 sectors, and the all-zero LBA 22 reflects the
  four unused trailing entry slots. This closes the stage-A/F1 content claim;
  it is not a multi-block (`CMD18`), bus-width/high-speed, publication,
  filesystem or write result. An optional multi-block mode
  (`gta4l-emmc-mb=1`) is implemented and host-tested. Its first 2026-11-02
  boot stopped during identification, before any `CMD18` transfer, at the
  known CMD3-completion timeout. The post-attempt audit fixed a missing SDHCI
  Multi/Single Block Select bit in the `CMD18` mode word; the corrected image
  then issued one open-ended `CMD18` over the 21 GPT array sectors and
  stopped at block ordinal 20 (at least 20 of 21 blocks drained) before the
  helper's 1,000,000 µs data bound expired (`rc=-11`, no latched controller
  error, no `TC`), and the explicit `CMD12` stop did not report `CC` or `TC`
  within its bound. This exercises the multi-block data phase beyond one block
  but does not establish F2: no established 21/21 transfer, completed stop or
  content comparison exists (`docs/emmc-driver.md` §5.3.7). The post-bound
  drain image (§5.3.8) was then booted three times: the first stopped during
  identification at the same CMD3-completion timeout, the second reached the
  MD phase and exhausted the 500,000 µs drain window with no additional
  blocks and no `TC` before the `CMD12` stop again timed out, and the third
  failed earlier with a single-block `CMD17` data timeout at array LBA 2. The
  drain therefore did not resolve the stall, H8 is intermittent, and F2
  remains unproven. Stage B1 (`gta4l-emmc-probe=4`) is implemented and
  host-tested: it switches the card EXT_CSD BUS_WIDTH to 4-bit and then
  8-bit, verifies the byte-183 readback and the SDHCI `HOST_CONTROL` bit,
  runs the CMD19/CMD14 bus test at each width, and repeats the stage-A
  content sums against the 1-bit baseline. The first B1 image was flashed to
  BOOT only and booted three times on 2026-11-02. Only one boot reached B1; it
  stopped at the 4-bit byte-183 readback (`sw=0`, `swrc=-19`, stop=1), before
  CMD19/CMD14 or any per-width content pass. The other two boots stopped
  before B1 at the CMD3-completion timeout and a block-1 CMD17 data timeout.
  A rebuilt image with the report-completeness fields was then flashed to
  BOOT only and booted five times (warm resets): two boots reached B1 and
  reproduced the same 4-bit stop with a clean CMD6 R1 (`c6=00000900`), the
  host width bit set and read back (`ha=02`), and an EXT_CSD read at the
  attempted width that returned the 1-bit content (`es=000009cb`, byte 183
  still 0). The other three boots failed before B1 at two TC-only/no-error
  CMD17 timeouts and one CMD3-completion timeout. A cold-boot session then
  booted the same image three times with full power-down and power-on: one
  stopped before B1 at the block-1 CMD17 timeout and two reproduced the same
  4-bit tuple byte-for-byte, and the same-session BOOT re-read matched the
  post-flash hash. The 4-bit stop is therefore 4/4 across warm and cold
  boots, a retained warm-reset card state is excluded as its cause, and all
  eight boots are bound to the flashed bytes. Stage B1C
  (`gta4l-emmc-probe=5`) was then flashed BOOT only and booted seven times
  (two cold, five warm) without reflashing: both boots that reached the
  continuation entered on exactly the observed 4-bit tuple (`bc=1`,
  `c6=00000900`, `hb=00`→`ha=02`, `es=000009cb`, `sw=0`, `swrc=-19`) and
  both timed out in the CMD19 write phase (`tt=1`, `trc=-11`,
  `tw=00000000`, `te=00000000`;
  `FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/18`), with CC and
  BufferWriteReady latched but no TC and no error bit — the project's first
  data-write-direction terminal sample. The two reports are identical except
  for the stack counters, and a full cold power cycle occurred between them,
  so the timeout is deterministic conditional on reaching the rung and is
  not caused by card state retained from an earlier interrupted CMD19. The
  content pass and byte-183 re-read were skipped after the hard error;
  `e183a`/`esa` are placeholders. The other five boots stopped before B1
  (four CMD17 TC-only timeouts, one CMD3 timeout), and the same-session BOOT
  re-read matched the post-flash hash. The 4-bit read paradox is unchanged
  (`es=000009cb` at the attempted width). F3 remains unproven
  (`docs/emmc-driver.md` §5.3.12–5.3.19,
  `docs/emmc-cmd2-crc-audit.md` §2.18–2.23). The stage-6 1-bit CMD19/CMD14
  discriminator (`gta4l-emmc-probe=6`) was built from clean `5f57dcb`
  (kernel `a20d6269…`, payload `fdaa507d…`, boot image `90cf074c…`),
  flashed BOOT only (p69 `c6a9f8e0…` → `1ea626f5…`) and booted thirteen
  times without reflashing (seven cold, six warm; both p69 re-reads
  matched). Boots 1 and 12, both cold, reached the rung and timed out in the
  1-bit CMD19 write phase with the same terminal class as the 4-bit
  continuation (`tt=1`, `tw=00000000`, `te=00000000`, `trc=-11`; CC and
  BufferWriteReady latched, no TC, no error; present `0x03f801f6`), and no
  CMD6 was issued, so the data-write path is broken independent of bus
  width. The other eleven boots stopped pre-B1 (eight H8 CMD3 timeouts,
  three CMD17 TC-only/no-error timeouts, one at the GPT array's LBA 2). The
  `trc=-20` class remains unobserved; the byte-183 paradox is unchanged.
  The next rung (owner-chosen) was a host data-timeout audit
  (`gta4l-emmc-probe=7`). The stage-7 image was built from clean `d158378`
  (kernel `9d7ae51e…`, payload `b4171d96…`, boot image `3cc5f9f1…`),
  flashed BOOT only (p69 `1ea626f5…` → `13f88342…`) and booted twelve times
  without reflashing (one cold, eleven warm; the post-batch p69 re-read
  matched). Five boots reached the DTO stage and reported `stop=12` with
  identical fields; the stop is a probe-gate artifact, not a device result:
  TOUTVAL `[7:3]` was already zero at rest, and the forced-write readback
  differed only on the read-only `INT_STABLE` bit. E1 was silent — no error
  bit of any kind latched while BufferReadReady streamed seven blocks
  roughly 3× past the 20.5 ms minimum, then CMD12 got no R1b (`-7`) and
  the recovery read ended in ERR_INHIBIT (`-17`), so the controller never
  raises data-timeout in this configuration. The other seven boots stopped
  pre-B1 (six block-1 CMD17 TC-only timeouts and one post-CMD7 CMD13
  completion timeout). E2 did not run, the `trc=-20` class remains
  unobserved, the byte-183 paradox is unchanged, and F3 remains unproven.
  The next rung is B2 high-speed timing (`gta4l-emmc-probe=8`), implemented
  and simulator-tested: after the stage-A pass it switches EXT_CSD
  `HS_TIMING` (byte 185) to 1, ramps the GCC SDCC1 APPS RCG to the pinned
  50 MHz floor row (`0x20B`; the 52 MHz class floors to 50 MHz), repeats
  the full content pass at that clock and compares every sum with the
  400 kHz baseline, then drops the clock and clears byte 185 before a
  recovery read. It always reports (`GTA4L-EMMC B2 REPORT`) and stops with
  0/10/11/12/13/14; it issues no CMD19/CMD14, width switch or user-data
  write. The host batch also narrows the stage-7 arming gate to TOUTVAL
  only, so the read-only `INT_STABLE` bit cannot block a DTO report. Core
  143/86 and probe 85/51; the `report-width-b2` fixture fits the 2048-byte
  buffer. The stage-8 image was then built from `9a84215` plus the packager
  fix `9d6fd1f` (kernel `fc3d5ee3…`, payload `9d17d0a1…` with a 7/7
  contract, boot image `e5bc5024…` with an 18/18 gate), flashed BOOT only
  (p69 `13f88342…` → `2cd82e0f…`) and booted four times without reflashing
  (one cold, three warm; the post-batch p69 re-read matched). Boots 1 and 3
  stopped pre-B2 at the H8 CMD3-completion timeout (`stage=8`, `rc=-7`,
  `step=9`); boots 2 and 4 produced field-identical `B2 REPORT rc=0 stop=0`
  reports: byte 185 switched to 1 (`S … hs=01`), the RCG read `0x20b` (the
  pinned 50 MHz floor row), the full stage-A content pass matched at 50 MHz
  (`T same=1 div=0`, all 21 block sums), and the revert was clean
  (`V … hs=00 rcg=00002017`, block-0 `qb0=62084b66`). F4 is therefore
  measured PASS at 1-bit/50 MHz (content-level only; not a width, write,
  DMA, CQE, ICE, HS200 or frequency-measurement result), and the cold boot
  is not part of that evidence. F3 remains unproven. The flash record and
  transcript are in `out/gta4l/ram-root-emmc-probe-b2-evidence/`.
  The next rung is H9 (`gta4l-emmc-probe=9`), implemented and
  simulator-tested: the standard read-only identification sequence runs once
  with one designed duplicate CMD3 after the H8 completion timeout (same
  argument and timing path, never a third attempt), then stops after the TRAN
  wait before any EXT_CSD read or stage-A pass and always reports
  `GTA4L-EMMC H9 REPORT`. It issues no CMD6, width switch or user-data
  access; the only new card-facing action is the duplicate CMD3. Core 148/90
  and probe 91/51; the `report-width-h9` fixture is the seventh width
  materialization. The stage-9 image was then built from clean `a952a09`
  (kernel `75e529a3…`, payload `e1b474f7…` with a 7/7 contract, boot image
  `92d9471c…` with an 18/18 gate), flashed BOOT only (p69 `2cd82e0f…` →
  `25385fd7…`) and booted four times without reflashing (one cold, three
  warm; the post-batch p69 re-read matched). Boot 2 was the clean control
  (`rc=0 stop=0`, a clean CMD3 within 26 poll iterations ≈0.29 ms); boots
  1/3/4 were field-identical `rc=-7 stop=11` reports: attempt 1 exhausted
  the full 100,000-iteration ≈1.14 s window completion-silent, and the
  approved duplicate CMD3 also timed out (0/3 recovery), so no in-boot
  retry policy is adopted and the flake stays per-boot random (3/4 this
  batch). F3 remains unproven. The flash record and transcript are in
  `out/gta4l/ram-root-emmc-probe-h9-evidence/`.
  The next rung is H10 (`gta4l-emmc-probe=10`), the width-at-HS
  continuation, implemented and simulator-tested: after the stage-A pass it
  enters HS (byte 185 = 1, 50 MHz), switches EXT_CSD BUS_WIDTH (byte 183)
  to 4-bit at HS, runs one CMD19/CMD14 round trip and the content pass at
  4-bit/HS, then reverts the width and the timing and re-reads block 0.
  It always reports (`GTA4L-EMMC H10 REPORT`) and stops with
  0/10/11/12/13/14/15/16/17; it issues no 8-bit step, no retry and no
  user-data write. Core 148/90 unchanged; probe 102/56 (was 91/51); the
  `report-width-h10` fixture is the eighth width materialization. The
  device run followed on 2026-11-03 (BOOT-only flash, p69 `25385fd7…` →
  `38c74815…`, 15 boots without reflashing): one H10 verdict (boot 2) —
  `rc=-19 stop=12` with the legacy width tuple at HS (`sw=0`,
  `swrc=-19`, clean CMD6 R1, host width set and read back, byte-183
  readback 0, EXT_CSD re-read equal to the 1-bit-at-HS sum) — so the card
  ignores the 4-bit BUS_WIDTH write at HS exactly as at 400 kHz and the
  timing-class model is excluded; the other 14 boots were pre-HS CMD3
  completion-silent stops (including both cold boots), and the verdict
  carries a single-sample caveat because the two-matching rule was not
  met. F3 remains unproven. The flash and boot records are in
  `out/gta4l/ram-root-emmc-probe-h10-evidence/`.
  The next rung is H11 (`gta4l-emmc-probe=11`), the width-value
  continuation, implemented and simulator-tested: it replays the H10
  4-bit switch at HS as a record-only sample, attempts the 8-bit
  BUS_WIDTH value (byte 183 = 2, still SDR) at HS, and only when that
  switch is honored runs one CMD19/CMD14 round trip and the content pass
  at 8-bit/HS before reverting the width and the timing and reading
  block 0. A clean-shape W4 mismatch re-opens the H10 verdict as
  per-boot random; a W8 readback that disagrees with its own re-read sum
  is the byte-183 paradox row. It always reports (`GTA4L-EMMC H11
  REPORT`) and stops with 0/10/11/12/13/14/15/16/17/18; it issues no
  DDR variant, no CMD18/CMD25, no retry and no user-data write. Core
  148/90 unchanged; probe 116/64 (was 102/56); the `report-width-h11`
  fixture is the ninth width materialization. The device run needs a
  fresh explicit BOOT-only approval naming the byte-183 = 1 and = 2
  writes, the 8-bit DAT application, the CMD19/CMD14 round trip and the
  50 MHz ramp. F3 remains unproven.
  See [docs/emmc-driver.md](docs/emmc-driver.md),
  [docs/emmc-cmd2-crc-audit.md](docs/emmc-cmd2-crc-audit.md),
  [docs/emmc-cmd1-gap-candidate.md](docs/emmc-cmd1-gap-candidate.md) and
  [docs/emmc-cmd7-timeout-finding.md](docs/emmc-cmd7-timeout-finding.md).
- QEMU AppleImage4/AMFI findings are in `docs/qemu.md` and are cross-port
  input requirements, not gta4l hardware evidence.

## TB128FU reference

The Lenovo Xiaoxin Pad 2022 (TB128FU, Snapdragon 680 / SM6225) port by
[xVanTuring](https://github.com/xVanTuring/UniXNU-tb128fu) is the closest
proven reference: same Bengal family, A53/A73 cores, GICv3 addresses, timer
PPI list, PAN constraint and Adreno 610. Its timer, IPI and original-PMGR
findings are compared at pinned revisions in
[docs/tb128fu-comparison.md](docs/tb128fu-comparison.md), with the upstream
BSD-2-Clause-Patent credit. No TB128FU source is imported here.

## NativeEmmc reference

The Redmi Note 9 (merlinx) NativeEmmc driver by
[Dracen](https://github.com/Dracen-W) is a read-only XNU block-device
reference, pinned at `4980f1070b0c822dedb9eb350df710a57df28590`
([catalog](https://unixnu.com/drivers/community-native-emmc)). It targets the
MediaTek MT6768 MSDC controller, not the SM6115 SDHCI v5 controller, so its
register and command code is not transferable. The host-only static audit in
[docs/native-emmc-reference-audit.md](docs/native-emmc-reference-audit.md)
records the reusable publication patterns, the MediaTek-specific parts and the
completion/error-path findings. No code is imported.

## PixelExperience reference

The PixelExperience Plus 11 (Android 11) release for `gta4l`/SM-T505 by
[dolbaras](https://github.com/dolbaras/gta4l-gta4lwifi-roms) is a reference for
the vendor storage stack, not for XNU. Its release payload and pinned source
anchors are compared in
[docs/pe-storage-stack-provenance.md](docs/pe-storage-stack-provenance.md):
the artifact digest matches the published release metadata, the boot board name
matches the pinned device configuration, the QRD overlay's `sdhci@4744000`
node matches the live FDT, and the inspected vendor source sends CMD7 as R1
rather than the port's R1B. Exact source-to-artifact identity is unresolved,
only the SM-T505 build was device-tested, and no code or quirk is imported.

## Inputs

| Item | Value |
| --- | --- |
| SoC | Qualcomm SM6115 (Bengal) |
| SoC source | `gta4lunix/UniXNU-sm6115-common` |
| OS profile | `ipados-17.7.11-21H461` |
| DRAM | 3 GiB in three live banks (`0x40000000+0x3e580000`, `0x80000000+0x20000000`, `0xa0000000+0x60000000`); ADT span `0x40000000+0xC0000000`; 2 GiB DarwinBoot window at `0x80000000` |
| Framebuffer | `0x5C000000`, 1200x2000, stride 4800 |
| Boot image | v0, base `0x40000000`, kernel `0x40008000`, tags `0x40000100`, page 4096 |
| Firmware | Mu-Silicium `gta4lPkg`, `SILICIUM_UEFI.fd` |

The SoC source is pinned in `feeds.toml`. The board DTS includes
`sm6115.dtsi`; the common ADT generator supplies `/chosen`, `/cpus`,
`/arm-io`, `/defaults`, `/options`, `/product`, NVRAM and payload handoff.

## Build

From the shared UniXNU checkout:

```sh
./unixnu defconfig gta4l_defconfig -D ../UniXNU-gta4l
./unixnu fetch -D ../UniXNU-gta4l
./unixnu status --json -D ../UniXNU-gta4l
./unixnu tree -D ../UniXNU-gta4l
./unixnu build kernel -D ../UniXNU-gta4l
./unixnu build package -D ../UniXNU-gta4l
./unixnu build firmware --uefi ../Mu-Silicium -D ../UniXNU-gta4l
./unixnu build payload --unique-chip-id <hex> -D ../UniXNU-gta4l
./unixnu bootimg -D ../UniXNU-gta4l
```

The canonical one-command flow is `scripts/package.sh` (a verbatim copy of
UniXNU's `templates/package.sh`); it runs `unixnu release` with the board's
`[package]` values and the boot DTB staged by the firmware preflight. The
board's 192 MiB packed payload produces a boot image larger than the
100,663,296-byte BOOT partition, so the release verify step refuses it. That is
expected: the release artifact is not the current safe diagnostic image. See
[docs/upstream-packaging.md](docs/upstream-packaging.md) and
[docs/boot-dtb-provenance.md](docs/boot-dtb-provenance.md).

For a raw RAM-root diagnostic payload, use the explicit builder instead of
editing `board.toml`:

```sh
python3 scripts/build-gta4l-diagnostic-payload.py \
  --data-ramdisk <root.img> --trust-cache <trustcache.bundle> \
  --owner-trust-cache --unique-chip-id <hex> --diagnostic host-init \
  --kernel-arg rd=md0 --kernel-arg sheng-pmgr=2 \
  --output out/gta4l/diagnostic-payload.img
```

It forces `payload.size_mib=80` and `packed_data_ramdisk=false` in a temporary
device copy and requires the payload/ADT contract check. Package the result
with `scripts/package-gta4l-diagnostic.py`; that packager also enforces the
contract before publishing an image.

Read [Port a device](https://unixnu.com/porting/) and
[Device tree](https://unixnu.com/porting/device-tree) before changing inputs.
Record host, QEMU and hardware results separately. Do not treat the panic screen
or a QEMU checkpoint as a gta4l hardware boot.
