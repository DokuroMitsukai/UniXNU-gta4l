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
- **eMMC protocol correction (2026-11-04).** JEDEC defines `BUS_WIDTH`
  (EXT_CSD byte 183) as `W/E_P`, explicitly not readable; Linux verifies a
  width switch with a bus test or a read-only EXT_CSD comparison. The earlier
  B1/H10/H11 "card refused the whole width field" conclusions are therefore
  withdrawn: a zero byte-183 readback is not evidence of refusal. The H12
  attempt used value 4, which is reserved (DDR is 5/6), so it did not test
  DDR. CMD14 bus-test reads may carry an optional pattern CRC16 that the host
  ignores; the H13 `trc=-12` row therefore does not prove the card's data
  conversation failed. The stage-7 timeout audit modified `CLOCK_CONTROL`
  instead of the `TIMEOUT_CONTROL` byte, so its "minimum timeout armed" claim
  is withdrawn. The 1-bit bus test now uses the JEDEC `0x80` pattern and the
  two-bit check. CMD18 now uses live FIFO readiness and acknowledges BRR, and
  both simulators model that separately from the latched bit. New verified
  switch primitives (`gta4l_emmc_set_bus_width_verified`,
  `gta4l_emmc_bus_test_verified`) restore both the host and the card on
  failure and report `GTA4L_EMMC_ERR_WIDTH_RESTORE` when framing is unknown.
  Stage H15 (`gta4l-emmc-probe=15`) is the corrected 4-bit SDR rung. Its
  first cold boot (2026-11-05) passed the verified switch and then reproduced
  the CMD19 write timeout at 4-bit; the old revert was blocked by the latched
  DAT inhibit. The report/recovery fixes and the content-only H16 rung are
  documented in `docs/emmc-driver.md` §5.3.31. H16's first boot returned
  `rc=0 stop=0` with the verified 4-bit switch, the full 4-bit content pass,
  the verified revert and the block-0 recovery read (§5.3.32), so 4-bit SDR
  reads are measured on hardware; the remaining barrier is the CMD19
  completion rule, because the card sends no CRC status token for the tuning
  pattern. The already-flashed H14 image
  is not a prerequisite, and F3 remains open. Historical device reports and
  raw evidence are preserved, not rewritten.
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
  fixture is the ninth width materialization. The device run followed on
  2026-11-03 (BOOT-only flash, p69 `38c74815…` → `2f6deba3…`, five boots
  without reflashing): boots 1–4 stopped pre-identification at the H8 CMD3
  completion timeout (facts only), and boot 5 was the only H11 verdict —
  `rc=-19 stop=12`, with the W4 replay matching the H10 legacy tuple
  field-for-field and the W8 8-bit attempt ignored with the same tuple, so
  the byte-183 refusal is sticky and covers both nonzero SDR values (the
  whole SDR field). **Correction (2026-11-04): byte 183 is `W/E_P` and not
  readable, so this is not evidence of a card refusal; the width conclusion
  is withdrawn.** The V/X reverts were clean and B/T/Q were not run. F3
  remains unproven; the open owner questions are the DDR bit-plane and the
  CMD19 write-phase wedge. The flash and boot records are in
  `out/gta4l/ram-root-emmc-probe-h11-evidence/`.
  The next rung is H12 (`gta4l-emmc-probe=12`), the record-only DDR
  attempt, implemented and simulator-tested: after the stage-A pass it
  enters HS and issues one CMD6 SET of byte 183 = 4 (**correction: value 4
  is reserved, not the 4-bit DDR code; DDR is 5/6**),
  waits TRAN, applies only the 4-bit SDR host DAT width for the
  verification read, re-reads EXT_CSD, reverts byte 183 to 0 and the
  timing to legacy, and reads block 0. The controller DDR configuration is
  never touched. A clean refusal (readback 0 and a sum that changed only
  by byte 185) closes the width-study conclusion; a readback of 4 is the
  honored row and stops for an owner decision; a disagreeing
  readback/sum pair is the byte-183 paradox row. It always reports
  (`GTA4L-EMMC H12 REPORT`) and stops with 0/10/11/12/13/14/15; it issues
  no value 5, no CMD19/CMD14, no CMD18/CMD25 and no user-data write. Core
  148/90 unchanged; probe 128/69 (was 116/64); the `report-width-h12`
  fixture is the tenth width materialization. The device run followed on
  2026-11-03 (BOOT-only flash, p69 `2f6deba3…` → `0f7ec22c…`, five boots
  without reflashing): boots 1, 2 and 4 stopped pre-identification at the
  H8 CMD3 completion timeout and boot 3 at a block-1 CMD17 data timeout
  (facts only), and boot 5 was the only H12 verdict — `rc=-15 stop=12`.
  The record-only byte-183 = 4 CMD6 answered clean (`c6=00000900`) but
  the 4-bit SDR verification re-read failed with card status (`sw=0`,
  `swrc=-15`, `es=00000000`); no byte-183 = 4 readback was obtained, and
  the V/X reverts and Q recovery read were clean. With the only written
  value differing (4 versus the refused 1 and 2), the post-switch
  behavior changed, coherent with the card toggling into 4-bit DDR for
  data: the paradox row is measured but unverified and the whole-field
  refusal row is not closed. F3 remains unproven. The flash and boot
  records are in `out/gta4l/ram-root-emmc-probe-h12-evidence/`.
  The host-only write-phase wedge audit is
  [docs/emmc-write-wedge-audit.md](docs/emmc-write-wedge-audit.md): the
  command phase completes and the controller requests the CMD19 block, but
  the data phase never latches `TC` because CMD19 is a bus-test pattern,
  not a normal data write, and the card sends no CRC status token; the
  port's `TOUTVAL=0x0E` at 400 kHz puts the hardware timeout about 22
  minutes away, far beyond the 1 s window every observed wedge used. The
  audit also records the missing per-request data reset as a recovery gap
  and names a data-reset + CMD14 rung as the recommended owner-gated
  follow-up (the forced-`TOUTVAL=0` 1-bit write is second). No device work
  was performed and no write-side goal is claimed.
  The first wedge-ladder rung is H13 (`gta4l-emmc-probe=13`), implemented
  and simulator-tested: after the stage-A pass it issues one 1-bit CMD19
  write, one bounded SDHCI data-circuit reset (`SOFTWARE_RESET` data bit
  with a self-clear poll), the post-reset `PRESENT_STATE`, one CMD14 read
  with the existing pattern comparison and a block-0 recovery read, then
  reports (`GTA4L-EMMC H13 REPORT`) and stops with 0/10/11/12/13/14/15/16/17.
  It issues no CMD6, CMD18, CMD25 or user-data write and changes no card
  width or timing. Core 160/98 (was 148/90); probe 139/69 (was 128/69);
  the `report-width-h13` fixture is the eleventh width materialization.
  The device run followed on 2026-11-03 (BOOT-only flash, p69 `0f7ec22c…`
  → `18d2a83d…`, boot count lost, verdict on an estimated ~18th boot
  without reflashing): about seventeen boots stopped pre-identification at
  the H8 CMD3 completion timeout (facts only), and the only H13 verdict was
  `rc=-12 stop=14`. The CMD19 write reproduced the wedge (`wrc=-11`,
  `err=0000`, `nrm=0011`, `pst=03f801f6`; the same terminal tuple as the
  B1C/B1C-1 rows), the bounded reset self-cleared (`rc=0 wr=04 rb=00 it=6
  dinh=0`), but CMD14 failed with `ERR_DATA_CRC` (`trc=-12`, no pattern
  words) and the block-0 recovery read was refused with `ERR_INHIBIT`
  (`rc=-17`). The reset clears the controller latch, but **correction
  (2026-11-04): the CMD14 `ERR_DATA_CRC` may be the optional pattern CRC16
  that the host ignores, so this row does not prove the card's data
  conversation failed.** The verdict is a single sample (the two-matching
  rule is unmet) and F3 remains unproven. The flash and boot
  records are in `out/gta4l/ram-root-emmc-probe-h13-evidence/`. The next
  rung is the forced post-write CMD12/CMD13 drain sample, to be drafted
  only after this record lands.
  The second wedge-ladder rung is H14 (`gta4l-emmc-probe=14`), implemented
  and simulator-tested: it keeps the H13 form and inserts one forced CMD12
  `STOP_TRANSMISSION` (R1b, argument 0, no ACMD12 transfer-mode bit) plus
  one CMD13 `SEND_STATUS` (R1) between the post-reset sample and the
  record-only CMD14/Q reads, while the post-reset inhibit is clear. The
  CMD12 entry wait applies the SDHCI stop-command `DAT_INHIBIT` exemption;
  CMD13 keeps the normal both-inhibit wait. It reports
  (`GTA4L-EMMC H14 REPORT`) and stops with 0/10/11/12/13/14/15/16/17, and
  it issues no CMD6, CMD18, CMD25 or user-data write and changes no card
  width or timing. The drain is local to
  `gta4l_emmc_post_write_drain()`; `TIMEOUT_CONTROL` is untouched. Core
  168/104 (was 160/98); probe 151/77 (was 139/69); the
  `report-width-h14` fixture is the twelfth width materialization. **H14 is
  paused by the 2026-11-04 correction: the write-wedge audit itself says
  CMD12 is not appropriate for the single-block CMD19 write, and the drain's
  stop-code mapping conflated distinct failures. The BOOT-only image was
  flashed, but no boot or report is required and it is not a prerequisite
  for H15.** F3 remains unproven.
  The corrected width rung is H15 (`gta4l-emmc-probe=15`): after the stage-A
  baseline it performs a verified 4-bit SDR switch (CMD6, CMD13 TRAN wait,
  host width apply/readback, and an EXT_CSD read whose read-only properties
  must match the 1-bit baseline), the corrected CMD19/CMD14 test (optional
  pattern CRC tolerated; JEDEC 1-bit `0x80`/two-bit rule), the full 4-bit
  content pass, a verified return to 1-bit and the block-0 recovery read. Its
  first cold boot (2026-11-05) **passed the verified 4-bit switch**
  (`vf=1`, host `00→02`) and then reproduced the CMD19 write-phase timeout
  at 4-bit; the old revert's pre-switch EXT_CSD read was blocked by the
  latched `DAT_INHIBIT`, so no revert CMD6 was issued. The report now prints
  explicit attempted flags and a T result code, and the baseline-based
  recovery clears the data circuit before CMD6. The content-only isolation
  rung is H16 (`gta4l-emmc-probe=16`): the same verified switch and content
  pass with no CMD19/CMD14 at all. Its first boot (2026-11-05, warm reboot)
  returned `rc=0 stop=0`: verified 4-bit switch on the first CMD6 attempt,
  the full 4-bit content pass matching the 1-bit baseline
  (`same=1 div=0`, `b0=62084b66`), a verified baseline-based revert
  (`02→00`) and a matching block-0 recovery read. Stage H17
  (`gta4l-emmc-probe=17`) is the Linux-compatible completion rung: after the
  same verified switch and 4-bit content pass it runs one CMD19 pattern write
  whose expected data timeout is recorded and tolerated, one bounded data
  circuit reset and one CMD14 read-back before the verified revert and the
  recovery read. The rule matches `mmc_bus_test()` (which discards every
  `BUS_TEST_W` error and lets `BUS_TEST_R` decide) and TI erratum SPRZ436H
  i836 (no CRC status token is generated for the tuning pattern). Its `B`
  line records `wrc`/`rst`/`ci` beside `trc`/`tw`/`te`. H15/H16/H17 stop
  codes are 0/10/11/12/13/14/15/16, with 10 (baseline read failed) and 13
  (bus test) the additions. No HS, DDR, 8-bit, CMD18, CMD25 or
  user-data write. Core 179/104 and probe 163/77. **4-bit SDR sector reads
  are measured `[C]`**, and **F3 is measured PASS at 4-bit SDR, 400 kHz
  `[C]`**: the fourth boot of the H17 image (cold, then three warm; the first
  three stopped at the known pre-identification CMD3 class) returned
  `rc=0 stop=0` with the CMD19 timeout recorded (`wrc=-11`), a clean bounded
  data reset (`rst=0`), the CMD14 pattern match (`trc=0`,
  `tw=te=000000a5`), the matching 4-bit content pass, the verified revert
  (`02→00`) and `qb0=62084b66`. The tolerated timeout keeps `fail=1` in the
  report by design. The 8-bit width is untried and stays open separately.
  The H17 image (`13938e49…`, payload `72f6e2d4…` 7/7, gate 18/18) was
  flashed to BOOT only (`3adb7401…` → `60d3d4bc…`, verified prefix and
  unchanged tail, RECOVERY/EFS/sec_efs unchanged, battery 100%). The payload
  contract checker now derives its diagnostic list from the name map after
  the first build was refused for missing stage 17.
  The Stage-C/F5 publication survey is in
  [docs/gta4l-storage-publication-design.md](docs/gta4l-storage-publication-design.md):
  it finds that `IOStorageFamily` (including `IOBlockStorageDriver`,
  `IOGUIDPartitionScheme` and `IOMediaBSDClient`) is already prelinked, that
  `NativeRamBlock`/`NativeVirtioBlock` pin the `IOBlockStorageDevice` shape,
  and that the smallest gta4l path is a read-only `IOResources`-matched
  block driver over the existing CMD17 core, with the personality behind a
  new guarded kernel-config entry. It also records that the existing
  storage-ABI audit/bindings artifacts are absent in this checkout, so
  qualification is the first gate; the corrected design advertises transfer
  limits to the family, sends the media-state notification on a read error,
  and treats whole-device publication plus a media-path read as F5 while
  reporting GPT partition discovery separately. The Linux/QEMU qualification
  path then passed: `scripts/audit-gta4l-storage-interface.py` stopped the
  actual `ca983f7d…` candidate at `_bsd_init`, recorded the live
  `IOBlockStorageDevice`/`IOService` metaclasses and ran the full 190-slot
  audit (176 kernel bindings, native 4K, original storage code unchanged).
  The audit artifacts are pinned in `docs/evidence-baseline.md`, and
  `scripts/test-gta4l-storage-abi.py` covers the positive audit, mutated
  snapshots, a wrong candidate and the preserved run record. The portable
  request helper (`package/Gta4lEmmcBlockCore.c/.h`) is now implemented and
  host-tested (23 scenarios, 18 mutation controls); it uses explicit
  READ/WRITE/INVALID directions, returns per-request completed bytes, is
  read-only, capped at 64 blocks/32 KiB and fails closed. This clears the ABI and request-helper
  gates only: no driver is linked, and the C++ adapter, personality,
  concrete bindings and device run are still ahead.
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
