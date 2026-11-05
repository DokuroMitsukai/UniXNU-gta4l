# gta4l SDHC evidence baseline

Status: host-only snapshot of the working tree used for the software-evidence
closure `[S]`. It is not a hardware result, an electrical measurement or
permission to activate bus power or the card clock; the decision remains
**NO-GO** (`sdhc-decision-memo.md`) `[S]`.

**Protocol correction (2026-11-04).** `BUS_WIDTH[183]` is `W/E_P` and not
readable, so the B1/H10/H11 byte-183 refusal and paradox conclusions are
withdrawn; a verified switch must compare read-only EXT_CSD properties and
restore both sides on failure. H12 issued reserved value 4, not DDR (5/6).
CMD14 bus-test reads may carry an optional pattern CRC16 that the host
ignores, so H13's `trc=-12` does not prove a card-side conversation failure.
The stage-7 timeout experiment wrote `CLOCK_CONTROL`, not the
`TIMEOUT_CONTROL` byte, so its armed-minimum conclusion is withdrawn. The
1-bit bus test now uses the JEDEC `0x80`/two-bit rule. CMD18 uses live FIFO
readiness and acknowledges BRR; the prior ordinal values are software counts,
not validated sectors. H14 is paused. The correction adds H15
(`gta4l-emmc-probe=15`), host-tested at the time of that batch (its first
cold boot is recorded below), plus the verified width/bus-test primitives;
raw device reports and images are preserved. The current host counts are core
179/104 and probe 163/77. The corrected H16 boot measured the 4-bit content
pass, and the H17 device run closed the bus-test clause: its fourth attempt
(cold, then three warm) returned `rc=0 stop=0` with the expected CMD19 data
timeout recorded (`wrc=-11`), a clean bounded data reset (`rst=0`), the CMD14
pattern match (`trc=0`, `tw=te=000000a5`), the matching 4-bit content pass,
the verified revert and the recovery read, so **F3 is measured PASS at 4-bit
SDR, 400 kHz `[C]`**. The 8-bit width is untried.

Hashes were first taken at `2026-10-30T21:06:39Z` and refreshed on
`2026-10-31` after the porting-approach reassessment, the native eMMC
implementation, the eMMC audit fixes, the stage-2/stage-3 hardware runs, the
follow-up `cmd2window`/`fmt=2` runs and the CMD7 diagnostics-only changes. The
table was refreshed again on `2026-10-31` after the CMD7 diagnostics-correction
checkpoint (fresh terminal sample, observed-transition-only overflow,
count-consistency checker and report-budget check) and on `2026-11-01` after the
diagnostics width-closure and terminal-read observation-boundary checkpoints
(field-wise report-width fixtures, the renderer-specific budget correction, the
late-trace width correction and the final CMD7 error re-read). It was refreshed
again on `2026-11-01` after the PixelExperience storage-stack provenance review
and its closeout correction; that review adds a document but no manifest row,
while the correction refreshes the `README.md`, `docs/emmc-reference.md` and
`porting.toml` rows. It was refreshed once more on `2026-11-01` after the
release-packaging adoption and the firmware-dependency decision; that change
refreshes the `porting.toml` row. The new packaging documents, workflow and
audit script are outside this bounded manifest and are recorded by their own
files. It was refreshed once more on `2026-11-01` after the LinuxForGta4l
organization source audit: that change refreshes the `docs/emmc-reference.md`
and `porting.toml` rows, adds no manifest row, and records the new audit
document and retrieval TSV outside this bounded manifest. It was refreshed once
more on `2026-11-01` after the host-only fail-closed batch: that change adds
reserved-`DATA_SECTOR_SIZE` rejection plus final pre-clear error samples for
non-busy command and data success paths, refreshes the `porting.toml`,
`docs/emmc-driver.md`, core/header, simulator and host-test rows, and corrects
the `dtbo-lk2nd` manifest license note. It was refreshed once more on
`2026-11-01` after the `fmt=4` fail-closed stage-3 hardware runs on two units
and the pulled-prefix readback gate rerun: that change refreshes the
`README.md`, `docs/emmc-driver.md`,
`docs/diagnostic-payload-input-inventory.md`,
`docs/sdhc-evidence-gap-checklist.md` and `porting.toml` rows, adds no manifest
row, and records the runs and provenance in `docs/emmc-cmd2-crc-audit.md`
§2.9-2.10 and `docs/emmc-driver.md` §5.3.3; the table stays at 45 rows. It was
refreshed once more on `2026-11-01` after the `cmd1gap` candidate stage-3
hardware runs on three units: that change refreshes the `README.md`,
`docs/emmc-driver.md`,
`docs/diagnostic-payload-input-inventory.md` and `porting.toml` rows, adds no
manifest row, and records the candidate and runs in
`docs/emmc-cmd1-gap-candidate.md`, `docs/emmc-cmd2-crc-audit.md` §2.11 and
`docs/emmc-driver.md` §5.3.4; the table stays at 45 rows. It was refreshed once
more on `2026-11-02` after the stage-1 full-probe runs on two units: that change
refreshes the `README.md`, `docs/emmc-driver.md`,
`docs/diagnostic-payload-input-inventory.md` and `porting.toml` rows, adds no
manifest row, and records the run in `docs/emmc-cmd2-crc-audit.md` §2.12,
`docs/emmc-driver.md` §5.3.5 and §6.1; the same batch closes the §2.10
fold-width caveat with the pinned panic-screen renderer provenance; the
table stays at 45 rows. One further refresh on `2026-11-02` records the
owner-authorized read-only recovery-side readback of eMMC blocks 0 and 1 on the
owner's SM-T505N: it independently reproduces the MBR signature and GPT header
CRC, refreshes the `README.md`, `docs/emmc-driver.md` and `porting.toml` rows,
adds no manifest row, and keeps the table at 45 rows. One more refresh on
`2026-11-02` records the stage-A extended content read: the rebuilt
`emmc-probe-1` image read the GPT header, the full 84-entry primary array
(LBAs 2..22), the first usable sector and the last sector on the owner's
SM-T505N, and every per-LBA sum matched a same-session read-only recovery
readback; it refreshes the `README.md`, `docs/emmc-driver.md`,
`package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`,
`scripts/build-gta4l-emmc-probe.py` and `porting.toml` rows, adds no manifest
row, and keeps the table at 45 rows. A further refresh on `2026-11-02` records
the corrected A2/F2 attempt 2: the image entered the MD phase, stopped at
block ordinal 20 and then hit the helper's data bound, and the `CMD12` stop
did not complete; it refreshes the `README.md` and `docs/emmc-driver.md` rows,
adds no manifest row, and keeps the table at 45 rows. One more refresh on
`2026-11-02` records the A2/F2 attempt-3 boot: the image was flashed and
verified, but it stopped during identification at the same CMD3-completion
timeout before the MD phase, so the post-bound drain was not exercised; it
refreshes the `README.md` and `docs/emmc-driver.md` rows, adds no manifest row,
and keeps the table at 45 rows. A final refresh on `2026-11-02` records two
later boots of the same attempt-3 image: one reached the MD phase and
exercised the bounded drain without resolving the stall, and one failed the
stage-A array read; it refreshes the `README.md` and `docs/emmc-driver.md`
rows, adds no manifest row, and keeps the table at 45 rows. A host-side
refresh on `2026-11-02` adds the Stage-B1 bus-width/bus-test rung
(`docs/emmc-driver.md` §5.3.9): it refreshes the `README.md`,
`docs/emmc-driver.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`,
`package/Gta4lEmmcProbe.c`, `scripts/sim-gta4l-emmc/harness.c`,
`scripts/test-gta4l-emmc.py` and `porting.toml` rows, adds no manifest row,
and keeps the table at 45 rows. One more refresh on `2026-11-02` records the
Stage-B1 hardware run: the verified B1 image was flashed to BOOT only and
booted three times. Only one boot reached B1; it stopped at the 4-bit
byte-183 readback (`sw=0`, `swrc=-19`, stop 1), before CMD19/CMD14 or any
per-width content pass. The other two stopped before B1 at the CMD3-completion
timeout and a block-1 CMD17 data timeout, so F3 remains `[U]`. That change
refreshes the `README.md`, `docs/emmc-driver.md` and `porting.toml` rows, adds
no manifest row, and keeps the table at 45 rows. A further host-side refresh on
`2026-11-02` records the Stage-B1 report-completeness audit
(`docs/emmc-driver.md` §5.3.11, `docs/emmc-cmd2-crc-audit.md` §2.17): the B1
result now keeps the CMD6 status, the `HOST_CONTROL` bytes around the width
write and the EXT_CSD readback sum, and the probe suite adds a zero-readback
and a lost-host-width model. That change refreshes the `docs/emmc-driver.md`,
`package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c` and
`porting.toml` rows, adds no manifest row, and keeps the table at 45 rows; the
probe simulator, width fixture, probe test, CMD7 finding and CMD2 audit are
outside the table. A further refresh on `2026-11-02` records the Stage-B1R2
diagnostic re-run (`docs/emmc-driver.md` §5.3.12,
`docs/emmc-cmd2-crc-audit.md` §2.18): the rebuilt image (base `24f8c9d`) was
flashed BOOT only and booted five times as warm force reboots. Two boots
reached B1 and reproduced the 4-bit stop with a clean CMD6 R1, the host width
bit set and read back, and an EXT_CSD read at the attempted width returning
the 1-bit content (byte 183 still 0); three stopped before B1 (two
TC-only/no-error CMD17 timeouts and the H8 CMD3 timeout), so F3 remains
`[U]`. That change refreshes the `README.md`, `docs/emmc-driver.md` and
`porting.toml` rows, adds no manifest row, and keeps the table at 45 rows;
`docs/emmc-cmd2-crc-audit.md` and this document are outside the table. One
more refresh on `2026-11-02` records the Stage-B1R2 cold session and report
binding (`docs/emmc-driver.md` §5.3.13, `docs/emmc-cmd2-crc-audit.md`
§2.19): three cold boots of the same image gave one pre-B1 TC-only/no-error
CMD17 stop and two byte-identical 4-bit stops, and the same-session read-only
p69 re-read matched the post-flash hash, binding all eight boots to the
flashed bytes. That change refreshes the `README.md`, `docs/emmc-driver.md`
and `porting.toml` rows, adds no manifest row, and keeps the table at 45
rows; `docs/emmc-cmd2-crc-audit.md` and this document are outside the table.
One more refresh on `2026-11-02` records the Stage-B1C
continue-past-readback host batch (`docs/emmc-driver.md` §5.3.13.1): the
probe gains a fifth stage that, on exactly the observed 4-bit tuple,
continues to the CMD19/CMD14 bus test and the stage-A content pass,
re-reads EXT_CSD byte 183, restores the host width and emits one B1 report
with `bc=`/`e183a=`/`esa=`; the 8-bit rung is not attempted. That change
refreshes the `README.md`, `docs/emmc-driver.md`,
`docs/diagnostic-payload-input-inventory.md`, `package/Gta4lEmmcProbe.c`,
`scripts/test-gta4l-diagnostic-payload.py` and `porting.toml` rows, adds no
manifest row, and keeps the table at 45 rows; the diagnostic
builder/packager/gate scripts, the probe simulator/fixture/test,
`docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md` and this
document are outside the table or not listed.
One more refresh on `2026-11-03` records the Stage-B1C hardware run
(`docs/emmc-driver.md` §5.3.14, `docs/emmc-cmd2-crc-audit.md` §2.20): the
image was flashed BOOT only and booted seven times, two boots reached the
continuation on the pinned 4-bit tuple and both timed out in the CMD19 write
phase (`tt=1`, `trc=-11`, present state `0x03f801f6`) with CC and
BufferWriteReady latched but no TC and no error bit, the other five boots
stopped before B1, and the same-session p69 re-read matched the post-flash
hash. That change refreshes the `README.md`, `docs/emmc-driver.md` and
`porting.toml` rows, adds no manifest row, and keeps the table at 45 rows;
`docs/emmc-cmd2-crc-audit.md` and this document are outside the table.
One more refresh on `2026-11-03` records the Stage-B1C 1-bit CMD19/CMD14
discriminator host batch (`docs/emmc-driver.md` §5.3.15): `gta4l-emmc-probe=6`
runs one 1-bit bus-test round trip after the stage-A pass and before the
first CMD6, stops with `stop=9` on a 1-bit failure (no width switch) and
otherwise falls through to the unchanged stage-5 flow. The core now accepts
`bits == 1`; the 1-bit PIO form uses a 4-byte block with first byte `0x5A`
and compares the first returned byte `0xA5`. That change refreshes the
`README.md`, `docs/diagnostic-payload-input-inventory.md`,
`docs/emmc-driver.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`,
`package/Gta4lEmmcProbe.c`, `scripts/sim-gta4l-emmc/harness.c`,
`scripts/test-gta4l-emmc.py`, `scripts/test-gta4l-diagnostic-payload.py` and
`porting.toml` rows, adds no manifest row, and keeps the table at 45 rows;
the probe simulator/fixture/test, the diagnostic builder/packager/gate
scripts, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`
and this document are outside the table or not listed.
One more refresh on `2026-11-03` records the Stage-B1C-1 hardware run
(`docs/emmc-driver.md` §5.3.16, `docs/emmc-cmd2-crc-audit.md` §2.21): the
stage-6 image was flashed BOOT only and booted thirteen times (seven cold,
six warm) without reflashing, both p69 re-reads matched the post-flash hash,
two cold boots reached the 1-bit discriminator and both timed out in the
CMD19 write phase with the same terminal class as the 4-bit continuation
(`tt=1`, `trc=-11`, CC + BufferWriteReady, no TC, no error, present
`0x03f801f6`) and no CMD6 issued, so the write path is broken independent of
bus width; the other eleven boots stopped pre-B1 (eight H8 CMD3, three CMD17
TC-only, one at the GPT array's LBA 2). That change refreshes the
`README.md`, `docs/emmc-driver.md` and `porting.toml` rows, adds no manifest
row, and keeps the table at 45 rows; `docs/emmc-cmd2-crc-audit.md` and this
document are outside the table.
One more refresh on `2026-11-03` records the data-timeout audit host batch
(`docs/emmc-driver.md` §5.3.17): `gta4l-emmc-probe=7` reads back the
timeout/enable configuration, arms the minimum timeout for an 8-block
CMD18+CMD12 read longer than the counter, re-reads block 0 and repeats the
1-bit CMD19 wedge under an 8 s software bound; it always reports and issues
no CMD6. That change refreshes the `README.md`, `docs/emmc-driver.md`,
`docs/diagnostic-payload-input-inventory.md`, `package/Gta4lEmmc.c`,
`package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`,
`scripts/sim-gta4l-emmc-probe/harness.c`,
`scripts/test-gta4l-emmc-probe.py` and `porting.toml` rows, adds no manifest
row, and keeps the table at 45 rows; the diagnostic
builder/packager/gate scripts, `docs/emmc-cmd2-crc-audit.md`,
`docs/emmc-cmd7-timeout-finding.md` and this document are outside the table.
One more refresh on `2026-11-03` records the Stage-B1C-2 data-timeout audit
hardware run (`docs/emmc-driver.md` §5.3.18, `docs/emmc-cmd2-crc-audit.md`
§2.22): the stage-7 image was built from clean `d158378` (kernel `9d7ae51e…`,
payload `b4171d96…` with a 7/7 contract, boot image `3cc5f9f1…` with an
18/18 gate), flashed BOOT only (pre-flash p69 `1ea626f5…` → post-flash
`13f88342…`, prefix/tail read back, RECOVERY/EFS/sec_efs unchanged) and
booted twelve times without reflashing (one cold, eleven warm; the
post-batch p69 re-read matched). Five boots reached the DTO stage with
identical `stop=12` reports; the stop is a probe-gate artifact (TOUTVAL was
already zero and the readback differs only on the read-only `INT_STABLE`
bit), and E1 was silent — no error bit latched while BufferReadReady
streamed seven blocks roughly 3× past the 20.5 ms minimum, then CMD12 got
no R1b and the recovery read ended in ERR_INHIBIT. The other seven boots
stopped pre-B1 (six block-1 CMD17 TC-only timeouts, one post-CMD7 CMD13
completion timeout). That change refreshes the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows, adds no manifest row, and
keeps the table at 45 rows; `docs/emmc-cmd2-crc-audit.md` and this document
are outside the table.
One more refresh on `2026-11-03` records the Stage-B2 HS-timing host batch
(`docs/emmc-driver.md` §5.3.19, `docs/emmc-cmd2-crc-audit.md` §2.23):
`gta4l-emmc-probe=8` samples the 400 kHz state, switches EXT_CSD
`HS_TIMING` byte 185 to 1, ramps the GCC SDCC1 APPS RCG to the pinned
50 MHz floor row (`0x20B`; the 52 MHz class floors to 50 MHz), repeats the
full stage-A content pass at that clock against the held baseline, then
drops the clock and clears byte 185 before a recovery read. It always
reports and stops with 0/10/11/12/13/14; no CMD19/CMD14, width switch or
user-data write. The stage-7 arming gate is narrowed to TOUTVAL only. That
change refreshes the `README.md`,
`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`,
`package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`,
`scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`,
`scripts/test-gta4l-diagnostic-payload.py` and `porting.toml` rows, adds no
manifest row, and keeps the table at 45 rows; the probe simulator/fixture/
test, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`
and this document are outside the table.
One more refresh on `2026-11-03` records the Stage-B2 HS-timing hardware run
(`docs/emmc-driver.md` §5.3.20, `docs/emmc-cmd2-crc-audit.md` §2.24): the
stage-8 image was built from `9a84215` plus the packager fix `9d6fd1f`
(kernel `fc3d5ee3…`, payload `9d17d0a1…` with a 7/7 contract, boot image
`e5bc5024…` with an 18/18 gate), flashed BOOT only (pre-flash p69
`13f88342…` → post-flash `2cd82e0f…`, prefix/tail read back,
RECOVERY/EFS/sec_efs unchanged) and booted four times without reflashing
(one cold, three warm; the post-batch p69 re-read matched). Boots 2 and 4
produced field-for-field identical `B2 REPORT rc=0 stop=0` reports: byte
185 switched to 1, the RCG read `0x20b` (the pinned 50 MHz floor row), the
full stage-A content pass matched at 50 MHz (`same=1`, `div=0`) and the
revert was clean (`hs=00`, RCG `0x2017`, block-0 read matching). Boots 1 and
3 stopped pre-B2 at the H8 CMD3-completion timeout. F4 is measured PASS at
1-bit/50 MHz (content-level only; no width, write, DMA or frequency result)
and F3 remains `[U]`. That change refreshes the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows, adds no manifest row, and
keeps the table at 45 rows; `docs/emmc-cmd2-crc-audit.md` and this document
are outside the table.
One more refresh on `2026-11-03` records the Stage-H9 CMD3-completion retry
host batch (`docs/emmc-driver.md` §5.3.21, `docs/emmc-cmd2-crc-audit.md`
§2.25): `gta4l-emmc-probe=9` runs the standard read-only identification
sequence once with the pinned 8-us post-ready CMD1 delay and, on the H8 CMD3
completion timeout, snapshots attempt 1 and re-issues exactly one identical
CMD3 (`0x00010000`, same timing path); it never runs a third attempt and
stops after the TRAN wait before any EXT_CSD read or stage-A pass, so it
issues no CMD6, width switch or user-data access. It always reports
`GTA4L-EMMC H9 REPORT` and stops with 0/10/11/12/13/14/15. That change
refreshes the `README.md`, `docs/diagnostic-payload-input-inventory.md`,
`docs/emmc-driver.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`,
`package/Gta4lEmmcProbe.c`, `scripts/sim-gta4l-emmc/harness.c`,
`scripts/test-gta4l-emmc.py`, `scripts/test-gta4l-diagnostic-payload.py` and
`porting.toml` rows, adds no manifest row, and keeps the table at 45 rows;
the probe simulator/fixture/test, the diagnostic builder/packager/gate
scripts, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`
and this document are outside the table.
One more refresh on `2026-11-03` records the Stage-H9 CMD3-completion
retry hardware run (`docs/emmc-driver.md` §5.3.22,
`docs/emmc-cmd2-crc-audit.md` §2.26): the stage-9 image was built from
`a952a09` (kernel `75e529a3…`, payload `e1b474f7…` with a 7/7 contract,
boot image `92d9471c…` with an 18/18 gate), flashed BOOT only (pre-flash
p69 `2cd82e0f…` → post-flash `25385fd7…`, prefix/tail read back,
RECOVERY/EFS/sec_efs unchanged) and booted four times without reflashing
(one cold, three warm; the post-batch p69 re-read matched). Boot 2 was
the clean control (`rc=0 stop=0`, a clean CMD3 within 26 poll iterations
≈0.29 ms). Boots 1/3/4 were field-identical `rc=-7 stop=11` reports:
attempt 1 exhausted the full 100,000-iteration ≈1.14 s window
completion-silent, and the approved duplicate CMD3 also timed out (0/3
recovery, with the latched `ERROR=0x0001`/`NORMAL=0x8000` exit). The
two-matching rule closed the sticky row with three samples; the cold
boot took part, so no cold-path caveat applies. No per-boot retry policy
is adopted; the flake stays per-boot random (3/4 this batch). F3 remains
`[U]`. That change refreshes the `README.md`, `docs/emmc-driver.md` and
`porting.toml` rows, adds no manifest row, and keeps the table at 45
rows; `docs/emmc-cmd2-crc-audit.md` and this document are outside the
table.
One more refresh on `2026-11-03` records the Stage-H10 width-at-HS
continuation host batch (`docs/emmc-driver.md` §5.3.23,
`docs/emmc-cmd2-crc-audit.md` §2.27): `gta4l-emmc-probe=10` runs the
standard identification once (no CMD3 retry), the stage-A pass at
400 kHz/1-bit, HS entry, a 4-bit BUS_WIDTH switch at HS, one CMD19/CMD14
round trip, the content pass at 4-bit/50 MHz, the width and HS reverts and
a block-0 recovery read; it always emits `GTA4L-EMMC H10 REPORT` and stops
with 0/10/11/12/13/14/15/16/17, with no 8-bit step, retry or fall-through
and no user-data write. The probe suite is 102 scenarios and 56 mutation
controls (was 91/51), the core suite is unchanged at 148/90, the
`report-width-h10` fixture is the eighth width materialization, and the
diagnostic wrapper/payload gate accept `emmc-probe-10`. That change
refreshes the `README.md`, `docs/diagnostic-payload-input-inventory.md`,
`docs/emmc-driver.md`, `package/Gta4lEmmcProbe.c` and `porting.toml` rows,
adds no manifest row, and keeps the table at 45 rows;
`docs/emmc-cmd2-crc-audit.md` and this document are outside the table.
One more refresh on `2026-11-03` records the Stage-H10 width-at-HS
hardware run (`docs/emmc-driver.md` §5.3.24,
`docs/emmc-cmd2-crc-audit.md` §2.27): the stage-10 image was flashed BOOT
only (p69 `25385fd7…` → `38c74815…`, post-batch re-read matched) and
booted fifteen times without reflashing (12-boot batch plus an
owner-approved 3-boot extension; two cold starts, boots 1 and 6). Boot 2
was the only H10 verdict: `rc=-19 stop=12` with the legacy width tuple at
HS (`sw=0`, `swrc=-19`, `c6=00000900`, `hb=00`→`ha=02`, `es=000009cc` =
the 1-bit-at-HS sum), clean V/X reverts and zero-initialized not-run B/T/Q
fields (`log=33` matching the committed failure path and the simulator
fingerprint). The other fourteen boots were pre-HS CMD3 completion-silent
stops (`PROBE FAILED stage=10 rc=-7`, step 9, including both cold boots) —
facts only, re-run not retried. The timing-class model for the BUS_WIDTH
refusal is excluded; the verdict carries the single-sample caveat
(two-matching rule unmet, 15-boot cap reached); F3 remains `[U]`. That
change refreshes the `README.md`, `docs/emmc-driver.md` and `porting.toml`
rows, adds no manifest row, and keeps the table at 45 rows;
`docs/emmc-cmd2-crc-audit.md` and this document are outside the table.
One more refresh on `2026-11-03` records the Stage-H11 width-value
continuation host batch (`docs/emmc-driver.md` §5.3.25,
`docs/emmc-cmd2-crc-audit.md` §2.28): `gta4l-emmc-probe=11` runs the
standard identification once (no CMD3 retry), the stage-A pass at
400 kHz/1-bit, HS entry, the record-only 4-bit replay (W4), the 8-bit
BUS_WIDTH attempt (W8, byte 183 = 2, still SDR), one CMD19/CMD14 round
trip and the content pass at 8-bit/50 MHz only when W8 is honored, the
width and HS reverts and a block-0 recovery read; it always emits
`GTA4L-EMMC H11 REPORT` and stops with 0/10/11/12/13/14/15/16/17/18,
with no DDR variant, no CMD18/CMD25, no retry and no user-data write.
A clean-shape W4 match closes the H10 single-sample caveat and a
clean-shape mismatch re-opens H10 as per-boot random; a W8 readback that
disagrees with its own re-read sum is the byte-183 paradox row. The
probe suite is 116 scenarios and 64 mutation controls (was 102/56), the
core suite is unchanged at 148/90, the `report-width-h11` fixture is the
ninth width materialization, and the diagnostic wrapper/payload gate
accept `emmc-probe-11`. That change refreshes the `README.md`,
`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`,
`package/Gta4lEmmcProbe.c` and `porting.toml` rows, adds no manifest
row, and keeps the table at 45 rows; `docs/emmc-cmd2-crc-audit.md` and
this document are outside the table.
One more refresh on `2026-11-03` records the Stage-H11 width-value
continuation hardware run (`docs/emmc-driver.md` §5.3.25,
`docs/emmc-cmd2-crc-audit.md` §2.28): the stage-11 image was flashed BOOT
only (p69 `38c74815…` → `2f6deba3…`, image prefix `3dc561cd…`, unchanged
tail `a28b4ea2…`; RECOVERY/EFS/sec_efs unchanged) and booted five times
without reflashing by the owner; the per-boot cold/warm method was not
captured. The post-boot p69 re-read matched `2f6deba3…` (battery 100%
Full), binding every boot. Boots 1–4 were pre-identification CMD3
completion-silent stops (`PROBE FAILED stage=11 rc=-7`, step 9) — facts
only, re-run not retried. Boot 5 was the only H11 verdict: `rc=-19
stop=12`, matching the committed `probe-h11-w4-replay` fingerprint
field-for-field. W4 replayed the H10 4-bit switch at HS and returned the
legacy tuple (`sw=0`, `swrc=-19`, `c6=00000900`, `hb=00`→`ha=02`,
`es=000009cc`), the second matching sample that closes the H10 `N=1`
caveat. W8 (byte 183 = 2) was ignored with the same legacy tuple despite
the host applying the 8-bit code (`ha=20`), so the byte-183 refusal is
sticky, timing-class-independent and covers both nonzero SDR values
tried — the whole-SDR-field refusal row (row 1) is closed. The V/X
reverts were clean, `PAD` pre equals post, and `B`/`T`/`Q` are the
zero-initialized not-run records (`log=33`). F3 remains `[U]`; the open
owner questions are the DDR bit-plane and the CMD19 write-phase wedge.
That change refreshes the `README.md`, `docs/emmc-driver.md` and
`porting.toml` rows, adds no manifest row, and keeps the table at 45
rows; `docs/emmc-cmd2-crc-audit.md` and this document are outside the
table.
One more refresh on `2026-11-03` records the Stage-H12 record-only DDR
attempt host batch (`docs/emmc-driver.md` §5.3.26,
`docs/emmc-cmd2-crc-audit.md` §2.29): `gta4l-emmc-probe=12` runs the
standard identification once (no CMD3 retry), the stage-A pass at
400 kHz/1-bit, HS entry, one record-only CMD6 SET of byte 183 to 4 (the
4-bit DDR code), its CMD13 TRAN wait, the matching 4-bit SDR host DAT
width for the verification read, the byte-183 readback and a fresh
EXT_CSD re-read with its byte sum, the width and HS reverts and a block-0
recovery read; it always emits `GTA4L-EMMC H12 REPORT` and stops with
0/10/11/12/13/14/15, with no controller DDR mode, no value-5 variant, no
CMD19/CMD14, no CMD18/CMD25, no retry and no user-data write. A clean
refusal (readback 0 and the 1-bit-at-HS sum) closes the width-study
conclusion; a readback of 4 (or a sum that grew by exactly 4) is the
honored row stopped before controller DDR work; a disagreeing
readback/sum pair is the byte-183 paradox row. The probe suite is 128
scenarios and 69 mutation controls (was 116/64), the core suite is
unchanged at 148/90, the `report-width-h12` fixture is the tenth width
materialization, and the diagnostic wrapper/payload gate accept
`emmc-probe-12`. That change refreshes the `README.md`,
`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`,
`package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`,
`porting.toml` and `scripts/test-gta4l-diagnostic-payload.py` rows, adds
no manifest row, and keeps the table at 45 rows;
`docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`, the
probe simulator/fixture/test, the diagnostic builder/wrapper/gate scripts
and this document are outside the table or not listed.
One more refresh on `2026-11-03` records the Stage-H12 record-only DDR
hardware run (`docs/emmc-driver.md` §5.3.26,
`docs/emmc-cmd2-crc-audit.md` §2.29): the stage-12 image was built from
clean `dc1a85a` (kernel `75a4661a…`, payload `27d8228f…` with a 7/7
contract, boot image `e5744884…` with an 18/18 gate against the
`ce4d76e8…` v5 baseline), flashed BOOT only (p69 `2f6deba3…` →
`0f7ec22c…`, image prefix `e5744884…`, unchanged tail `a28b4ea2…`;
RECOVERY/EFS/sec_efs unchanged) and booted five times without reflashing
by the owner; the per-boot cold/warm method was not captured. The
post-boot p69 re-read matched `0f7ec22c…` (battery 97%), binding every
boot. Boots 1, 2 and 4 were pre-identification CMD3 completion-silent
stops (`PROBE FAILED stage=12 rc=-7`, step 9) and boot 3 was a block-1
CMD17 data timeout (`rc=-11`, step 15, TC only, no error) — facts only,
re-run not retried. Boot 5 was the only H12 verdict: `rc=-15 stop=12`.
The byte-183 = 4 CMD6 answered clean (`c6=00000900`) but the 4-bit SDR
verification re-read failed with card status (`sw=0`, `swrc=-15`,
`es=00000000`); no byte-183 = 4 readback was obtained. The V/X reverts
and Q recovery read were clean, `PAD` pre equals post, and `log=33`.
This is not the clean-refusal fingerprint (`swrc=-19`, `es=0x9cc`) and
not the honored row (`sw=4`, `es=0x9cd`), so the byte-183 paradox row is
measured but unverified and the whole-field-refusal conclusion remains
open; F3 remains `[U]`. That change refreshes the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows, adds no manifest row, and
keeps the table at 45 rows; `docs/emmc-cmd2-crc-audit.md` and this
document are outside the table.
One more refresh on `2026-11-03` records the host-only write-phase wedge
audit (`docs/emmc-write-wedge-audit.md`, `docs/emmc-driver.md` §5.3.27,
`docs/emmc-cmd2-crc-audit.md` §2.30): the audit decodes the recorded
CMD19 write sequence against the pinned `sdhci.c` PIO/transfer-mode path,
the `sdhci-msm` timeout formula, JEDEC B1.51 §6.6.4/Table 7 and
§A.6.3/Table A.223, and Linux `mmc_bus_test()`. At both measured widths
the command phase completes (`CC` latched) and the controller requests
the block (`BufferWriteReady` latched), but `TC` never latches and no
error bit sets (`normal=0x0011`, `error=0x0000`, present `0x03f801f6`).
The leading cause is that CMD19 is a host-to-device bus-test pattern for
which the card sends no CRC status token, so the controller waits for a
completion that never arrives; the port's `TIMEOUT_CONTROL = 0x0E` puts
the hardware timeout about 22 minutes away at 400 kHz, beyond the 1 s
window every observed wedge used, and the 8 s E2 repeat never ran
because the stage-7 E1 recovery read failed with `ERR_INHIBIT`. The
audit also records the missing per-request data-circuit reset as a host
recovery gap (not the first-wedge cause) and names a data-reset + CMD14
rung as the recommended owner-gated follow-up, with a forced-`TOUTVAL=0`
1-bit write second. No build, flash, boot or device access was performed,
no card-state write or write-side verdict was added, and F3 remains
`[U]`. That change refreshes the `README.md`, `docs/emmc-driver.md` and
`porting.toml` rows, adds no manifest row, and keeps the table at 45
rows; the new audit document, `docs/emmc-cmd2-crc-audit.md` and this
document are outside the table.
One more refresh on `2026-11-03` records the Stage-H13 rung-1 write-phase
recovery host batch and hardware run (`docs/emmc-driver.md` §5.3.28 and
§5.3.29, `docs/emmc-cmd2-crc-audit.md` §2.31): the stage-13 rung adds one
1-bit CMD19 write, one bounded SDHCI data-circuit reset (`SOFTWARE_RESET`
data bit with a self-clear poll), the post-reset
`PRESENT_STATE`/`DAT_INHIBIT` sample, one CMD14 read and the block-0
recovery read; it always emits `GTA4L-EMMC H13 REPORT` and stops with
0/10/11/12/13/14/15/16/17 (probe 139/69, core 160/98). The stage-13 image
was built from the clean `5c79aa9` base (kernel `3e02d187…`, payload
`1fb04a4d…` with a 7/7 contract, boot image `bc8175e9…` with an 18/18
gate against the `ce4d76e8…` v5 baseline; the probe archive `4ffb6d2c…`
matches the committed sources), flashed BOOT only (p69 `0f7ec22c…` →
`18d2a83d…`, image prefix `bc8175e9…`, unchanged tail `a28b4ea2…`;
RECOVERY/EFS/sec_efs unchanged) and booted repeatedly without reflashing;
the exact count was lost and the verdict arrived on an estimated ~18th
boot. The post-boot p69 re-read matched `18d2a83d…` (battery 92%),
binding every boot. About seventeen boots were pre-identification CMD3
completion-silent stops (`PROBE FAILED stage=13 rc=-7`, step 9, the
historical tuple and 100,000-iteration poll window; facts only, re-run not
retried). The verdict boot was `rc=-12 stop=14`: the CMD19 write
reproduced the wedge (`wrc=-11`, `err=0000`, `nrm=0011`, `pst=03f801f6`;
the same terminal tuple as the B1C/B1C-1 rows), the bounded reset
self-cleared (`rc=0 wr=04 rb=00 it=6 pst=03f800f0 dinh=0`), CMD14 failed
with `ERR_DATA_CRC` (`trc=-12`, no pattern words) and the block-0
recovery read was refused with `ERR_INHIBIT` (`rc=-17`). `PAD` pre equals
post and `log=31`. The reset clears the controller latch but does not
restore the card's data conversation; the verdict is a single sample (the
two-matching rule is unmet) and F3 remains `[U]`. That change refreshes
the `README.md`, `docs/diagnostic-payload-input-inventory.md`,
`docs/emmc-driver.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`,
`package/Gta4lEmmcProbe.c`, `scripts/sim-gta4l-emmc/harness.c`,
`scripts/test-gta4l-emmc.py`, `scripts/test-gta4l-diagnostic-payload.py`
and `porting.toml` rows, adds no manifest row, and keeps the table at 45
rows; `docs/emmc-cmd2-crc-audit.md`, the probe simulator/fixture/test, the
diagnostic builder/wrapper/gate scripts and this document are outside the
table or not listed.
One more refresh on `2026-11-04` records the Stage-H14 rung-2 forced
post-write CMD12/CMD13 drain host batch (`docs/emmc-driver.md` §5.3.30,
`docs/emmc-cmd2-crc-audit.md` §2.32): `gta4l-emmc-probe=14` keeps the
stage-13 form (read-only identification once, the stage-A pass at
400 kHz/1-bit, one 1-bit CMD19 write, one bounded SDHCI data-circuit
reset with the self-clear poll, the post-reset
`PRESENT_STATE`/`DAT_INHIBIT` sample) and inserts one forced CMD12
`STOP_TRANSMISSION` (R1b) plus one CMD13 `SEND_STATUS` (R1) between the
reset and the record-only CMD14/Q reads; it always emits
`GTA4L-EMMC H14 REPORT` and stops with 0/10/11/12/13/14/15/16/17
(probe 151/77, core 168/104). The drain is local to
`gta4l_emmc_post_write_drain()`; the CMD12 entry wait applies the SDHCI
stop-command `DAT_INHIBIT` exemption and `TIMEOUT_CONTROL` is untouched.
That change refreshes the `README.md`,
`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`,
`package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`,
`scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`,
`scripts/test-gta4l-diagnostic-payload.py` and `porting.toml` rows, adds
no manifest row, and keeps the table at 45 rows;
`docs/emmc-cmd2-crc-audit.md`, the probe simulator/fixture/test, the
diagnostic builder/wrapper/gate scripts and this document are outside the
table or not listed.
This document's own hash is not listed. Hashes describe
the working tree at the snapshot and change with later edits; re-run the
commands in §6 before reusing them. The snapshot is intended to be committed
without further content changes; if the committed content differs, re-run §6
and update the table.

One more refresh on `2026-11-05` records the host-only Stage-C/F5
storage-publication survey (`docs/gta4l-storage-publication-design.md`). The
survey finds that the kernel prelink already carries
`com.apple.iokit.IOStorageFamily` 2.1 with the `IOBlockStorageDriver`
(`IOProviderClass = IOBlockStorageDevice`, `device-type = Generic`),
`IOGUIDPartitionScheme` (whole `IOMedia`, `GUID_partition_scheme` content
mask) and `IOMediaBSDClient` (`IOMedia`, `IOBSD`) personalities; that
`NativeRamBlock` and `NativeVirtioBlock` pin the working
`IOBlockStorageDevice` shape; that the gta4l core already exposes
read-only controller/card initialization and 512-byte CMD17 reads with no
public user-data write API; and that the shared SM6115 resources have no
SDHCI/block component. It proposes a board-local, read-only
`IOBlockStorageDevice` over the CMD17 path, registered through a guarded
`IOResources`/`IOBSD` personality, with the prelinked storage family
creating the whole `IOMedia`, a boot-argument-gated report thread, and no
mount, write, CMD18, CMD6, CMD19, DMA, CQE or ICE. A source review then
corrected the design: the existing storage-ABI audit/bindings artifacts are
absent here, the audit hardcodes the original candidate and the live
storage-metaclass observation is candidate-specific, so qualification is the
first gate; the design now advertises `kIOMaximumBlockCountReadKey`/
`kIOMaximumByteCountReadKey` so the family splits requests, sends
`kIOMessageMediaStateHasChanged(kIOMediaStateOffline)` outside the
controller lock on a read error, and treats the whole-device `IOMedia` plus
one media-path read as F5 while reporting GPT partition discovery separately
with a bounded summary and no UUIDs/serials. The Linux/QEMU qualification
path then passed: `scripts/audit-gta4l-storage-interface.py` stopped the
actual `ca983f7d…` candidate at `_bsd_init`, recorded the live
`IOBlockStorageDevice`/`IOService` metaclasses, and ran the full 190-slot
audit (176 kernel bindings, live sizes `0x90`/`0x88`, native 4K, original
storage executable unchanged). No driver is linked, and block I/O, GPT
publication and hardware remain unqualified; F5 remains `[U]`. That change
refreshes the `README.md` and `porting.toml` rows, adds the new design
document and the board qualification harness outside the table, and keeps
the table at 45 rows.

## 1. Repository state

| Item | Value |
| --- | --- |
| Repository | `src/UniXNU-gta4l` (device repository) |
| Branch | `bringup/emmc-core` |
| HEAD at the hash snapshot | `60817c3` (merge main into the eMMC branch) |
| Parent of the fourteenth evidence batch | `95ff0a1293194b2108f4041068c77f18e708c1fd` (same commit) |
| Correction commit paths | 8: 7 modified (`docs/evidence-baseline.md`, `docs/firmware-handoff.md`, `docs/sdhc-decision-memo.md`, `docs/sdhc-dependency-trace.md`, `docs/sdhc-electrical-contract.md`, `docs/sdhc-power-pad-dependency-report.md`, `porting.toml`) and 1 added (`docs/sdhc-decision-update-2026-10-30.md`) |
| First follow-up commit paths | 6: 5 modified (`docs/evidence-baseline.md`, `docs/firmware-handoff.md`, `docs/sdhc-decision-update-2026-10-30.md`, `docs/sdhc-power-pad-dependency-report.md`, `porting.toml`) and 1 added (`scripts/test-gta4l-sdccdxe-decode.py`) |
| Second follow-up commit paths | 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-power-pad-dependency-report.md`, `scripts/test-gta4l-sdccdxe-decode.py`) |
| Third follow-up commit paths | 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-power-pad-dependency-report.md`, `scripts/test-gta4l-sdccdxe-decode.py`) |
| Fourth follow-up paths | 4: 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-decision-memo.md`, `porting.toml`) and 1 added (`docs/sdhc-evidence-gap-checklist.md`) |
| Fifth correction paths | 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `porting.toml`) |
| Sixth correction paths | 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `porting.toml`) |
| Seventh correction paths | 3 modified (`docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `porting.toml`) |
| Eighth correction paths | 6: 5 modified (`docs/evidence-baseline.md`, `docs/pmic-audit.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/spmi-0x28-analysis.md`, `porting.toml`) and 1 added (`scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Ninth correction paths | 6 modified (`docs/evidence-baseline.md`, `docs/pmic-audit.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/spmi-0x28-analysis.md`, `porting.toml`, `scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Tenth correction paths | 6 modified (`docs/evidence-baseline.md`, `docs/pmic-audit.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/spmi-0x28-analysis.md`, `porting.toml`, `scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Checker-fix follow-up paths | 4 modified (`docs/evidence-baseline.md`, `docs/pmic-audit.md`, `docs/sdhc-evidence-gap-checklist.md`, `porting.toml`) |
| Eleventh correction paths | 5 modified (`docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/spmi-0x28-analysis.md`, `porting.toml`, `scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Twelfth evidence-batch paths (committed as `34761fc`) | 5: 4 modified (`docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/stock-firmware-analysis-audit.md`, `porting.toml`) and 1 added (`docs/vendor-sdhc-power-sequence.md`) |
| Thirteenth correction paths (committed as `88aa689`) | 10 modified (`.circleci/config.yml`, `.github/workflows/build-xnu-kernel-gta4l.yaml`, `.github/workflows/workflowtest.yaml`, `.github/workflows/workingworkflow.yaml`, `docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/stock-firmware-analysis-audit.md`, `docs/vendor-sdhc-power-sequence.md`, `porting.toml`, `scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Checker-fix follow-up paths (committed as `95ff0a1`) | 2 modified (`docs/evidence-baseline.md`, `scripts/test-gta4l-spmidxe-descriptor-base.py`) |
| Fourteenth evidence-batch paths at this snapshot | 5 modified (`docs/emmc-reference.md`, `docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `docs/vendor-sdhc-power-sequence.md`, `porting.toml`) |
| Fifteenth reassessment paths at this snapshot | 3: 2 modified (`docs/sdhc-decision-memo.md`, `porting.toml`) and 1 added (`docs/porting-approach-reassessment-2026-10-31.md`) |
| Sixteenth implementation paths at this snapshot | 15: 6 modified (`README.md`, `board.toml`, `docs/evidence-baseline.md`, `docs/porting-approach-reassessment-2026-10-31.md`, `patches/xnu/series`, `porting.toml`) and 9 added (`docs/archlinux-gta4l-evidence.md`, `docs/emmc-driver.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `patches/xnu/gta4l-emmc-probe.patch`, `scripts/build-gta4l-emmc-probe.py`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`) |
| Seventeenth audit-fix paths at this snapshot | 8 modified (`docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `porting.toml`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`) |
| Eighteenth audit-fix paths at this snapshot | 13 modified (`configs/gta4l_defconfig`, `docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `porting.toml`, `scripts/build-gta4l-diagnostic-payload.py`, `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`) |
| Nineteenth audit-fix paths at this snapshot | 6 modified (`docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.c`, `porting.toml`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`) |
| Twentieth audit-fix paths at this snapshot | 7 modified (`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.c`, `porting.toml`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`) |
| Twenty-first audit-fix paths at this snapshot | 7 modified (`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.c`, `porting.toml`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`) |
| Twenty-second hardware-run paths at this snapshot | 5 modified (`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `docs/sdhc-evidence-gap-checklist.md`, `porting.toml`) |
| Twenty-third hardware-run paths at this snapshot | 4 modified (`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) |
| Twenty-fourth CMD7-diagnostics paths at this snapshot | 17: 14 modified (`README.md`, `docs/diagnostic-payload-input-inventory.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `porting.toml`, `scripts/sim-gta4l-emmc-probe/harness.c`, `scripts/sim-gta4l-emmc-probe/include/kern/debug.h`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc-probe.py`, `scripts/test-gta4l-emmc.py`) and 3 added (`docs/emmc-cmd7-timeout-finding.md`, `scripts/sim-gta4l-emmc-probe/report-width-fixture.c`, `scripts/test-gta4l-emmc-evidence-counts.py`) |
| Twenty-fifth PixelExperience-provenance paths at this snapshot | 4: 3 modified (`README.md`, `docs/emmc-reference.md`, `porting.toml`) and 1 added (`docs/pe-storage-stack-provenance.md`) |
| Twenty-sixth LinuxForGta4l-source-audit paths at this snapshot | 5: 3 modified (`docs/emmc-reference.md`, `docs/evidence-baseline.md`, `porting.toml`) and 2 added (`docs/linuxforgta4l-org-audit.md`, `docs/linuxforgta4l-org-audit-manifest.tsv`) |
| Twenty-seventh fail-closed hardware-run paths at this snapshot | 7 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`, `docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) |
| Twenty-eighth cmd1gap hardware-run paths at this snapshot | 8 modified (`README.md`, `docs/emmc-cmd1-gap-candidate.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`, `docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) |
| Twenty-ninth stage-1 hardware-run paths at this snapshot | 6 modified (`README.md`, `docs/diagnostic-payload-input-inventory.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) |
| Thirtieth stage-A content-read paths at this snapshot | 12 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `porting.toml`, `scripts/build-gta4l-emmc-probe.py`, `scripts/sim-gta4l-emmc-probe/harness.c`, `scripts/sim-gta4l-emmc-probe/report-width-fixture.c`, `scripts/test-gta4l-emmc-probe.py`) |
| Thirty-first Stage-B1 host-side paths at this snapshot | 14 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `porting.toml`, `scripts/sim-gta4l-emmc-probe/harness.c`, `scripts/sim-gta4l-emmc-probe/report-width-fixture.c`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc-probe.py`, `scripts/test-gta4l-emmc.py`) |
| Thirty-second Stage-B1 hardware-run paths at this snapshot | 5 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) |
| Thirty-third Stage-B1 report-completeness paths at this snapshot | 12 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `porting.toml`, `scripts/sim-gta4l-emmc-probe/harness.c`, `scripts/sim-gta4l-emmc-probe/report-width-fixture.c`, `scripts/test-gta4l-emmc-probe.py`) |
| Thirty-fourth Stage-B1R2 hardware-run paths at this snapshot | 5 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) |
| Thirty-fifth Stage-B1C hardware-run paths at this snapshot | 5 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) |
| Thirty-sixth Stage-H9 host-batch paths at this snapshot | 19 modified (`README.md`, `docs/diagnostic-payload-input-inventory.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `porting.toml`, `scripts/build-gta4l-diagnostic-payload.py`, `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py`, `scripts/sim-gta4l-emmc-probe/harness.c`, `scripts/sim-gta4l-emmc-probe/report-width-fixture.c`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-diagnostic-payload.py`, `scripts/test-gta4l-emmc-probe.py`, `scripts/test-gta4l-emmc.py`) |
| Thirty-seventh Stage-H9 hardware-run paths at this snapshot | 5 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) |
| Thirty-ninth Stage-H10 device-run record paths at this snapshot | 5 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) |
| Forty-first Stage-H11 device-run record paths at this snapshot | 5 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) |
| Forty-third Stage-H12 device-run record paths at this snapshot | 5 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) |
| Forty-fourth host-only write-phase wedge audit paths at this snapshot | 6: 5 modified (`README.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`) and 1 added (`docs/emmc-write-wedge-audit.md`) |
| Forty-fifth Stage-H13 host-batch paths (committed as `5c79aa9`) | 8 modified (`package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`, `scripts/sim-gta4l-emmc-probe/harness.c`, `scripts/sim-gta4l-emmc-probe/report-width-fixture.c`, `scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc-probe.py`, `scripts/test-gta4l-emmc.py`) |
| Forty-sixth Stage-H13 device-run record paths at this snapshot | 11 modified (`README.md`, `docs/diagnostic-payload-input-inventory.md`, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `porting.toml`, `scripts/build-gta4l-diagnostic-payload.py`, `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py`, `scripts/test-gta4l-diagnostic-payload.py`) |
| Dirty entries at the hash snapshot | 11, exactly the forty-sixth paths above |
| Staging and commits | None at the snapshot; the tree remains unstaged and uncommitted, and no staging or commit is authorized |

The hash table describes the thirty-sixth working-tree snapshot: the
`60817c3` base, the twenty-fourth CMD7-diagnostics batch, the twenty-fifth
PixelExperience-provenance paths, the LinuxForGta4l organization source audit,
the 2026-11-01 `fmt=4` fail-closed hardware runs on two units, the
`cmd1gap` candidate hardware runs on three units, the 2026-11-02 stage-1
full-probe runs on two units, the stage-A extended content read on the
owner's SM-T505N, the Stage-B1 host-side bus-width/bus-test rung, the
2026-11-02 Stage-B1 hardware run (three boots; one B1 stop at the 4-bit
readback), the host-only Stage-B1 report-completeness audit, the
Stage-B1R2 diagnostic re-run (five boots; two B1 stops with the new fields),
the Stage-B1C continuation host batch, the 2026-11-03 Stage-B1C hardware
run (seven boots; two CMD19 write-phase timeouts), the Stage-B1C 1-bit
discriminator host batch, the 2026-11-03 Stage-B1C-1 hardware run (thirteen
boots; two identical 1-bit CMD19 write-phase timeouts), the data-timeout
audit host batch, the 2026-11-03 Stage-B1C-2 data-timeout audit hardware
run (twelve boots; five identical `stop=12` DTO reports), the Stage-B2
HS-timing host batch, the 2026-11-03 Stage-B2 HS-timing hardware run
(four boots; two matching 1-bit/50 MHz B2 passes), the Stage-H9
CMD3-completion retry host batch, and the 2026-11-03 Stage-H9
CMD3-completion retry hardware run (four boots; three field-identical
sticky `stop=11` reports and one clean control), and the Stage-H10
width-at-HS continuation host batch, and the Stage-H11 width-value
continuation host batch, and the Stage-H12 record-only DDR attempt host
batch.
The stage-A
batch refreshed the `README.md`,
`docs/emmc-driver.md`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`,
`scripts/build-gta4l-emmc-probe.py` and `porting.toml` rows;
`docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`,
`scripts/sim-gta4l-emmc-probe/*`, `scripts/test-gta4l-emmc-probe.py` and
`docs/evidence-baseline.md` are outside this hash table or not listed. The
report-completeness batch refreshed the `docs/emmc-driver.md`,
`package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c` and
`porting.toml` rows and left the `README.md` bytes unchanged; the Stage-B1R2
batch then refreshed the `README.md`, `docs/emmc-driver.md` and
`porting.toml` rows; the Stage-B1C hardware-run batch refreshed those same
three rows.
Historical batch rows remain as recorded. The current snapshot is taken on top
of `2c6578a`; the `60817c3` base row is retained as the historical base for the
batches below. The eleventh correction commit `4c447b9`
contains exactly the five paths in its row. The
tenth correction commit `07a36e6`
contains exactly the six paths in its row. The ninth correction commit
`3e47264`
contains exactly the six paths in its row. The eighth correction commit
`e21d7da` contains exactly the six paths in its row. The seventh
correction commit `4148a61` contains exactly the three paths in its row. The
sixth correction commit `9699ded` contains
exactly the three paths in its row. The fifth correction commit `339a7e2`
contains exactly the three paths in its row. The fourth follow-up commit
`769d1bc` contains
exactly the four paths in its row. The third and second follow-up commits each
contain exactly the three paths in their rows. The first follow-up commit
contains exactly the six paths in its row: five documentation/porting edits and
the new host-only decode test. The correction commit `a958e26` contains exactly
the eight paths in its row and is not the earlier report batch.

The fourth batch added the Phase 1 evidence-gap checklist, linked it from the
decision memo and recorded it in the storage and power porting areas. The fifth
correction batch separated staged approvals from gate closure, labeled
dependencies as informational, added claim-level source references, recorded the
SPMI-arbiter acquisition gaps, narrowed unsupported acquisition restrictions,
marked roles as proposed and prepared the Phase 2 acquisition register. The sixth
correction batch completed shared-consumer and parent-supply acquisition
tracking, separated USID, runtime-APID and descriptor-base evidence, mapped
acquisition items to gates and corrected the E1/E3/E5/E9 source citations. The
seventh correction batch aligns `D11` with the `0x8878` descriptor-base override
path and extends `D6` to the `E5`/`E6`/`E7` measurement and recovery records. The
eighth correction batch added the initial pinned-image descriptor-base scan and
its reproducible host test. The ninth correction batch corrected the scan's
pointer propagation, address-range and index handling and the resulting claim
scope: it follows newly loaded `.data` pointers, pre/post-index addressing and
bounded direct `BL` recursion, resolves the known caller stores, and finds no
write intersecting `[0x8878,0x8880)`. The tenth correction batch makes the scan
distinguish XZR from SP, invalidate `.data` pointer slots overwritten in the
path or by modeled calls, stop and report instructions outside its
register-only whitelist, and report unresolved non-stack loads and stores and
every revisited-state/step/budget/call-depth/indirect-branch truncation,
conservatively invalidating all `.data` pointer slots on a revisit. Internal
indirect/computed and runtime/external writers remain unresolved. A checker-fix
follow-up corrected the revision-7 and scan-scope wording to state the
revisited-state invalidation explicitly; it changes no gate disposition. The
eleventh correction batch keeps conditional-branch paths independent,
propagates their invalidation union at function return, and stops seeding XZR
roots; it adds regressions for all three and updates the revision-8 scan scope.
The twelfth evidence batch adds two host-only inputs: an exact-build SM-T505N
package whose four BL images and both AP images match the capture in full while
six BL images match only on their decompressed prefixes (the TWRP recovery
differs as expected and the vbmeta mismatch is unresolved), and a pinned
vendor-kernel trace of the stock BUS_ON path (controller clock, BUS_ON, L24,
L11, pads, acknowledgement, the 10 ms core delay, then the card clock, with
explicit RPM enable and voltage votes). The thirteenth correction batch
narrows and corrects both inputs after the broad host-only audit: it separates
the unexplained nonzero partition tails from padding and full-partition
equality; retraces the vendor path against the captured DT's
`rpm-smd-regulator` backend, including aggregation, active/sleep request rules,
cached-state limits, RPM acknowledgement, the BUS_FAIL and 5,000 ms timeout
paths, the conditional register-restore path, and the framework/DT delay
limits; and records the overlay correspondence as name/ID-level only, not
binary provenance. It also scopes CI GitHub credentials to trusted HTTPS GitHub
endpoints with cross-host redirect stripping, and applies 64-bit register and
effective-address semantics, paired access/writeback and boundary-crossing
handling to the descriptor-base scanner, with unchanged result sets against the
audited `4c447b9` baseline. The vendor path is a software precedent and a
different design from the pinned mainline port; it closes no electrical or
measurement requirement. None of these batches adds a measurement, device
action or activation code, and none closes a gate.

The checker-fix follow-up adds single and paired boundary-crossing store/load
controls to the scanner test, states the site-level set comparison used for the
six result sets, and records why the hash table now has 36 rows; it changes no
gate disposition.

The fourteenth evidence batch records the official Samsung CXG1/CWI2 kernel
source comparison: both releases carry Linux 4.19.157 and the same relevant
power/regulator files; the source corroborates the vendor trace but omits the
LineageOS card-detect early-fail/cleanup blocks, and the exact CXB1 source
remains unavailable. It also records that the inspected SM-T505 family service
manual is not a verified T505N board record (PM4250 versus the captured PM6125; board
photo 85943_1_04 versus the title 85943_1_21), and that KMDX60018M-B425
(32 GB eMMC 5.1 + 24 Gb LPDDR4X, 254FBGA) is the leading eMMC candidate while
the exact installed part and electrical limits remain unresolved. No gate
disposition changed.

The fifteenth batch is the owner-directed porting-approach reassessment. It
narrows the existing `NO-GO` to the on-device bus-power activation operation,
states that software porting may proceed with the mainline Linux,
vendor/LineageOS and captured-firmware stack as the reference, and records the
dependencies to reproduce plus the residual electrical, retention, UEFI-volume
and boot-medium risks. It closes no electrical gate, adds no device action and
does not mark any missing measurement as established.

The sixteenth batch is the native eMMC implementation. It adds the portable
SDHCI v5/eMMC core, the simulated controller and card, the 25-scenario host
test with 3 mutation controls, the opt-in XNU probe, the archive builder and
board patch wiring, the `docs/emmc-driver.md` record and the
`docs/archlinux-gta4l-evidence.md` working-stack note. It updates the storage
porting area, README and reassessment. The probe has not been booted on the
tablet; no block device, filesystem, DMA, CQE, ICE, write or microSD path
exists, and no electrical gate is closed.

The seventeenth batch corrects the eMMC audit findings: the command
data-present bit is `0x20`; status bits are enabled for polling with signalling
left off; the reset is polled with byte accesses; CMD3 assigns the RCA through
the argument and checks R1; a missing or stuck power request and exhausted
status-clear retries are failures; `HOST_CONTROL2` is updated in place and the
fixed-1.8 V pad switch is selected; a 5 ms settle precedes the card clock; and
sector reads are bounded by the discovered capacity and the user-partition
selection. The simulator now enforces register widths, command encoding,
status-enable gating and RCA/block bounds independently, and the host test has
32 scenarios and 9 mutation controls. The probe adds stage 2 (host only) and
stage 3 (identification only) and reads `CNTFRQ_EL0` for its delay loop. The
probe is still not booted on the tablet and no electrical gate is closed.

The eighteenth batch closes the second eMMC audit. Bus and IO power requests
are now handled separately and success requires an observed `BUS_ON`
completion; an IO-only request, a delayed bus request and a bus-then-IO
sequence have simulator scenarios. The pad-supply policy is an explicit host
flag, documented as a deliberate departure from the DT-derived Linux state.
The probe aborts on a stalled generic counter instead of skipping settle.
SDHCI long responses are normalized with the same word shift as Linux, and the
response-type test now uses a two-bit mask so `RESP_SHORT_BUSY` is not parsed as
R2. A 4 KiB `DATA_SECTOR_SIZE` is rejected before any sector read, and all poll
loops use the documented 10 µs step. The host test now has 37 scenarios and 12
mutation controls. The probe is still not booted on the tablet and no
electrical gate is closed.

The nineteenth batch closes the third eMMC audit. A `BUS_ON` request is now
acknowledged with both `BUS_SUCCESS` and `IO_SUCCESS`, matching the Linux
`sdhci_msm_handle_pwr_irq()` handler, which treats bus-on as an implicit
IO-high transition; the bus-only case is tested by the 3.3 V scenario and a
mutation control. The core records the IO transition requested by the
`HOST_CONTROL2` `VDD_180` write and requires that transition to complete, so
the two-empty-read window no longer stands in for IO completion. The simulator
raises the delayed IO request after five status reads, beyond the old quiet
window, and a churn scenario keeps requests arriving so the loop can only end
by exhaustion. Power-loop exhaustion now returns `GTA4L_EMMC_ERR_POWER` in every
case. The host test now has 38 scenarios and 15 mutation controls. The probe is
still not booted on the tablet and no electrical gate is closed.

The twentieth batch closes the fourth eMMC audit. Completion of the
`HOST_CONTROL2` IO transition now requires the requested level, not any IO
request: an IO-low target is completed only by an IO-low request, and an
IO-high target only by an IO-high request (or by the implicit IO-high of
`BUS_ON` when no IO-low request is present). A wrong-level request is still
acknowledged, but the loop keeps waiting for the requested level. The simulator
has a wrong-level-only scenario that must fail and a wrong-then-correct
scenario that must succeed; a mutation control accepts any IO level and is
rejected. The host test now has 40 scenarios and 16 mutation controls. The
kernel, stage-2 payload and boot image were rebuilt, and the diagnostic input
inventory records the current artifact hashes. The probe is still not booted on
the tablet and no electrical gate is closed.

The twenty-first batch closes the fifth eMMC audit. Completion now tracks the
latest acknowledged IO level instead of a sticky "target seen" flag, applying
the Linux handler order inside each status word: `BUS_ON` implies IO-high, an
IO-low request overrides it, and an IO-high request overrides IO-low. Success
requires the latest level to equal the `HOST_CONTROL2` request, so a later
opposite level, a later standalone `BUS_ON` or contradictory bits invalidate an
earlier completion. New scenarios cover a correct level followed by a wrong
level, an IO-low completion followed by a standalone `BUS_ON`, and a status word
with both IO levels; all three must fail with the card clock disabled. Two new
mutation controls cover a first-level-wins sticky level and opposite bits
accepted as completion. An unchanged `VDD_180` bit no longer excuses an
observed wrong level, and the simulator no longer clears request latches on the
acknowledgement, so a request injected on the ack write is still observed; two
more scenarios and one mutation cover those cases. The host test now has 45
scenarios and 19 mutation controls. The kernel, stage-2 payload and boot image
were rebuilt and re-gated after the fix: kernel sha256
`1a4710892e49fa96c053de8f33f1b4b5d89a36fc3d688febd6fa0f7e4a89003a`, payload
sha256 `2953e90220f1e2d9341fc154f3f2eb6523ab2539be68404a3beffa8ee4702633`, boot
image sha256
`4f2252df6bbd5d9c7f05ef97c0041da9acdc47212debf31a10334aefa45a12e5`. The
stage-2 hardware run is recorded in the next batch; no electrical gate is
closed.

The twenty-second batch records the first stage-2 hardware run. The owner
connected the tablet (SM-T505N, TWRP 3.7.1) and approved the
BOOT-only procedure. The image was written to BOOT only; the first 85,688,320
bytes read back identical, and recovery stayed byte-identical to the recorded
TWRP partition (`14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae`).
The previous boot content was saved as
`dcfe6852035edbb4109231cd41dc5e618e1b8755f47188c1a01bf22e36fcce7b`. The tablet
panicked with `GTA4L-EMMC HOST REPORT rc=0 mci=10000072 pwr=0f clk=0007
hc2=0008 ven=00318a0c pst=00000000 log=4/1/268435570`. Because the image
contains the strict latest-level rule, `rc=0` also means the requested IO-low
completion was observed. This establishes controller reset, bus/IO power
completion, pad policy and card clock at the controller-local level; card
identification, CID/CSD/EXT_CSD, sector reads, rail voltages and retention
remain unproven. The build was not marked hardware-tested as built; the
hardware run is recorded separately. Photo sha256
`d049547e8fd6135aafe6fb8aff13c9e8295cb9e2012b3944cdb66ac364882a2c`.

The twenty-third batch records the stage-3 identification run. The stage-3
payload, boot image and BOOT-only write are recorded on the unchanged kernel.
The tablet booted the image and panicked before the identification report with
`GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967288 mci=10000072 pwr=0f clk=0007
hc2=0008 ven=00318a0c pst=00000000 cfg=00002017 cmd=00000000
log=4/1/268435570`. `rc=4294967288` is `-8`, `GTA4L_EMMC_ERR_CMD_CRC`; the
registers repeat the stage-2 host-init state and the last log event is the
completed controller reset, so host init completed and a card command failed a
response CRC check. The probe does not record the failing command or the SDHCI
interrupt status; that older report records no RCA, capacity, EXT_CSD, OCR, CID,
CSD or sector data and no sector read was issued. Later current-source reports
do record the CMD1 OCR value. This is a failing state: it establishes
the repeated host state and the failed identification attempt, not card
absence, incompatibility or any storage capability. Photo sha256
`0014db219d5d3dd1b1db572845d81c8a6fb966d0912c0c23aae02c9d70a082c9`.

A later diagnostics-only `cmd2window` image recorded the response window around
CMD2. It completed CMD2 and stopped at CMD3 with `rc=-7` (`CMD_TIMEOUT`),
`fail_index=3`, `fail_command=0x031A`, `fail_error=0x0000` and `step=9`; the
failed window held CID words 2..4 and a plausible CMD3 R1 status. Photo sha256
`a9656fc934f1fec9417633054441e28d192d93be3864ee8bf83a377560634305`. See
`docs/emmc-driver.md` §5.3.1 and `docs/emmc-cmd2-crc-audit.md` §2.7. This is
still a failing identification state, but the terminal command is CMD3, not
CMD2.

The later `fmt=2` diagnostics-only image is
`out/gta4l/ram-root-emmc-probe-fmt2-evidence/boot-ram-root-emmc-probe-fmt2.img`
(85,688,320 bytes, SHA-256
`7bf3e71991f949f2da6fcff2ef388268904a512b1876062d04e9bb86b6f411b6`); its
payload is `ba4e88ba…`, kernel `f53860af…`, source hashes `b4461abc…`,
`c50b6467…` and `60cff259…`. It was written to BOOT only on 2026-10-31
(readback matched and recovery stayed byte-identical) and the owner booted it.
The run completed CMD2, CMD3 and CMD9 and stopped at CMD7 with `rc=-7`,
`fail_index=7`, `fail_command=0x071B`, `fail_error=0x0000`,
`fail_normal=0x0002` (TC, CC absent) and `step=11`. The response window is not
a validated CMD7 R1. See `docs/emmc-cmd7-timeout-finding.md` and
`docs/emmc-driver.md` §5.3.2. This is still a failing identification state; the
terminal command is CMD7, the first R1b command in the sequence.

The 2026-11-01 `fmt=4` fail-closed image is
`out/gta4l/ram-root-emmc-probe-failclosed-evidence/boot-ram-root-emmc-probe-failclosed.img`
(85,688,320 bytes, SHA-256
`4d2f7975d3bc2e26a1ab15fb4d60d765190fba2df0e6b4ee7c5226d41a63e5bf`); its
payload is `954cc0dc…`, kernel `27cb1b54…`, source hashes `e493b3eb…`,
`e0a2830c…` and `932552a2…`. The payload contract is 7/7 and the boot-image
gate is 18/18. The owner reports two runs of the same image on two different
units: an SM-T505 first, then the owner's SM-T505N. Both panics record
`stage=3 rc=4294967288` with
`fail=1/2/0/00000209/0002/8000/03f800f0/8`,
`rsp=00000000,00000000,80000000,00c0ff80` and `step=8`, identical to the
current-source CMD2 CRC record, and the owner reports the results are identical
1:1. The first run has no flash record or run ID in this tree; its unit
attribution is owner-reported. The later BOOT-only run on the SM-T505N has a
flash record at
`out/gta4l/ram-root-emmc-probe-failclosed-rerun-evidence/flash-record.json`
(pre-flash BOOT `92e80609…`, post-flash full partition `b53f31fe…`, image-byte
readback `4d2f7975…`, tail `a28b4ea2…` unchanged, `RECOVERY` `14e11370…`
unchanged, gate 18/18 and payload contract 7/7; the gates were also rerun on
the pulled image prefix and extracted payload). The photo is
`out/gta4l/ram-root-emmc-probe-failclosed-rerun-evidence/stage3-fmt4-rerun-photo-2026-11-01.jpg`
(SHA-256
`c079952510039d236a705985a91d9e4a8b042a125afb81ef66bf1a4e6ddc8c76`). The
runs failed at CMD2 before CMD3/CMD9/CMD7, so the `fmt=4` CMD7 capture was not
exercised. CMD1 OCR `0xC0FF8080` was observed; no validated CID, CSD, EXT_CSD,
capacity or sector result was obtained and no sector read was issued. See
`docs/emmc-cmd2-crc-audit.md` §2.9-2.10 and
`docs/emmc-driver.md` §5.3.3. This is still a failing identification state; the
terminal command is CMD2.

The `cmd1gap` candidate is recorded in
`docs/emmc-cmd1-gap-candidate.md`: one 8-us delay after an accepted ready CMD1,
a simulator model that is explicitly hypothetical, and 98 core scenarios with
55 mutation controls. It was built from `2a3268c7` (source `874e6878…`,
`5dd223f7…`, `932552a2…`; archive `22e6186f…`; payload `6b6f5212…`, 7/7;
boot image `8a463dae…`, 18/18) and flashed to BOOT only with an unchanged tail
and `RECOVERY` (`out/gta4l/ram-root-emmc-probe-cmd1gap-evidence/`). The owner
then ran it on the owner's SM-T505N and two owner-reported SM-T505 units; all
three reached `GTA4L-EMMC IDENT REPORT: rc=0` with `step=15` and CMD7 CC+TC, so
stage-3 identification completed and the historical CMD7 capture gap is closed.
The owner's unit had failed CMD2 on the `fmt=4` image, and the delay is the
only source delta; the earlier `cmd2window` and `fmt=2` builds passed CMD2
without it, so the delay is not proven necessary and no general fix is adopted.
The three run results are owner-supplied text without a photo file or hash in
the tree. See `docs/emmc-cmd2-crc-audit.md` §2.11 and
`docs/emmc-driver.md` §5.3.4.

The second batch derived the status/clear/reset loop bounds from the pinned
MOVZ instructions, added last-read and delayed-completion boundary cases and
wrong-bound negative controls, and corrected the reset-argument and SDC2 field
wording. This third batch adds synthetic SDC2 CLK-pull field-position vectors,
corrects the raw-bit wording, and labels the earlier `porting.toml` change as
historical.

The earlier documentation batch contained the eleven paths listed in the
original evidence-closure provenance below: six modified and five added.

### Historical provenance

The original evidence-closure snapshot was taken on
`bringup/tab-a7-qemu-fixture` at
`e2d0cfc08c0c0119f3c55d1bcb6f9b8323893961` with 27 dirty entries: 8 modified
and 19 untracked, including this document. The tree was already dirty. This
task edited only paths already in that set and
added this document and `docs/stock-firmware-analysis-audit.md`; it did not
discard, stage, commit or revert existing work.
No pre-task snapshot exists, so the exact line-level split between earlier work
and this task is not independently recorded. The task-touched paths are:

- `docs/evidence-baseline.md` (new)
- `docs/stock-firmware-analysis-audit.md` (new)
- `docs/firmware-handoff.md`
- `docs/power-retention-evidence.md`
- `docs/rpm-power-interface.md`
- `docs/pm6125-register-map.md`
- `docs/sdhc-decision-memo.md`
- `docs/sdhc-dependency-trace.md`
- `docs/sdhc-electrical-contract.md`
- `docs/sdhci-host-init.md`
- `porting.toml`
- `scripts/oracle-gta4l-sdhci-host-init.py`
- `scripts/sim-gta4l-sdhci-host-init/harness.c`
- `scripts/test-gta4l-sdhci-evidence-counts.py`
- `scripts/test-gta4l-sdhci-host-init-sim.py`

The later bounded RPM/XBL trace updated `power-retention-evidence.md`,
`rpm-power-interface.md`, `sdhc-decision-memo.md`, `sdhc-electrical-contract.md`,
`stock-firmware-analysis-audit.md`, `pm6125-register-map.md` and `porting.toml`.

A later report batch was recorded with HEAD
`3c3d586dd1feb1d63ade4452800a4ccc7cbcee31` and six dirty paths: five modified
(`docs/evidence-baseline.md`, `docs/kernel-build-recipe.md`,
`docs/sdhc-decision-memo.md`, `docs/sdhc-dependency-trace.md`,
`porting.toml`) and one added (`docs/sdhc-power-pad-dependency-report.md`).
That inventory is a historical pre-commit observation for that batch, not the
current snapshot.

### New paths in this task

- `docs/evidence-baseline.md`
- `docs/stock-firmware-analysis-audit.md`

The pre-existing untracked entries are earlier work in the same SDHC evidence
effort. They are listed so the baseline is reproducible and so unrelated work is
not mistaken for this task's output.

### Modified paths in the pre-existing tree

- `docs/emmc-reference.md`
- `docs/sdhci-handoff-audit.md`
- `docs/sdhci-host-init.md`
- `package/Gta4lSdhciHostInit.c`
- `porting.toml`
- `scripts/sim-gta4l-sdhci-host-init/harness.c`
- `scripts/test-gta4l-sdhci-host-init-sim.py`
- `scripts/test-gta4l-sdhci-host-init.py`

### Untracked paths in the pre-existing tree

- `docs/firmware-handoff.md`
- `docs/linux-regulator-lifecycle.md`
- `docs/pm6125-identity-evidence.md`
- `docs/pm6125-register-map.md`
- `docs/pm6125-usid-research.md`
- `docs/pmic-audit.md`
- `docs/power-path-decision.md`
- `docs/power-retention-evidence.md`
- `docs/rpm-power-interface.md`
- `docs/sdhc-decision-memo.md`
- `docs/sdhc-dependency-trace.md`
- `docs/sdhc-electrical-contract.md`
- `docs/spmi-0x28-analysis.md`
- `scripts/extract-gta4l-firmware.py`
- `scripts/oracle-gta4l-sdhci-host-init.py`
- `scripts/test-extract-gta4l-firmware.py`
- `scripts/test-gta4l-sdhci-evidence-counts.py`

## 2. Current host-tested source hashes

These are the bytes the host checks exercise at the snapshot; they are not the
bytes of the previously booted image `[S]`.

| Artifact | Path | SHA-256 |
| --- | --- | --- |
| Host-init probe source | `package/Gta4lSdhciHostInit.c` | `3f8f45316cff8b3ad19266f901dba69475d7dd0816ce9b6040f2bee37eaebafb` |
| Probe header | `package/Gta4lSdhciHostInit.h` | `a91f76e6c0405315e6df3bca79d067ae798f688dac7b80f665d70f569482f0d5` |
| Simulator harness | `scripts/sim-gta4l-sdhci-host-init/harness.c` | `dd8c92092a6c7a609a81f6f202f5aa4470fdf6375cebd50ec49a828041066cdc` |
| Static host test | `scripts/test-gta4l-sdhci-host-init.py` | `6dd2c1d8ca64527c70b8410797a0361341d6b593b127b8a71a44f624f9580f6f` |
| Simulator test | `scripts/test-gta4l-sdhci-host-init-sim.py` | `fe696332841d247b58d81d2a2c1cd32cf59474eccaa5de64c1ab2a059d3de9b9` |
| Access-policy oracle | `scripts/oracle-gta4l-sdhci-host-init.py` | `488e2d0150692569fe20303facd4b7be3e5afa79e680f6c3785cad35bc570990` |
| Count-consistency test | `scripts/test-gta4l-sdhci-evidence-counts.py` | `f219e3cd281040e3d0c5731f626275ab8a943d119c4bfd141d8660079fed7f59` |
| Host-init policy document | `docs/sdhci-host-init.md` | `a50d093c379c10604291bcb135399878a96e83b24141d58ec6171411f7b206af` |
| Decision memo | `docs/sdhc-decision-memo.md` | `9dd6b5fcaf6eaa4f543d3140fc946ccd9d9ff27f33919b5e3e9000e704369bba` |
| Evidence-gap checklist | `docs/sdhc-evidence-gap-checklist.md` | `1ed307e26070f8a0a041aef32be4cc6db5d6de022c01350449d9588163fee76d` |
| Electrical contract | `docs/sdhc-electrical-contract.md` | `88cd9d6a4ac15ea5e9d738f947db0127d4b929a0c24a3713873e927c29907593` |
| Retention evidence | `docs/power-retention-evidence.md` | `eb91725bd6a63acded9e63ca0673670f36b43a3253da15d8ec17cb67f0387e07` |
| RPM interface | `docs/rpm-power-interface.md` | `4f3fe2e8527a9c334de0478f4a250a49d443b6e552eb4845f75e0cfc44a35e49` |
| PM6125 register map | `docs/pm6125-register-map.md` | `501672b50a86319b7bd095a92b674c162ccc485963e510a708649aa985486c22` |
| PMIC audit | `docs/pmic-audit.md` | `6fe005441dbc987313e6f9d2b810286158a23000cbd604fe30d51e86c50d3dae` |
| SPMI `+0x28` analysis | `docs/spmi-0x28-analysis.md` | `3b3b6e7485ac5939df400bbba1cb8146288961a7cd1df2f220e0dc2627bf3933` |
| RPM LDOA analyzer | `scripts/analyze-gta4l-rpm-ldoa.py` | `d967e640ca8f6f7eb0607d0a51aea9b541b21901ed4bf5a0a3e9be090f2fa4a8` |
| RPM LDOA analyzer self-test | `scripts/test-gta4l-rpm-ldoa-analysis.py` | `c670e2788044bd8b5054b19452dde8080ee9dc93d794ffe8abf3e2890e202afc` |
| Dependency trace | `docs/sdhc-dependency-trace.md` | `7399f421fd11db96fcea80e9c3f5bafc178e2d99099391e293188eb0dd8045b9` |
| Power/pad dependency report | `docs/sdhc-power-pad-dependency-report.md` | `35005a995918eaaa0da69d3f50238609c32d202fa0eca1945a4624dd170eecc2` |
| Decision update 2026-10-30 | `docs/sdhc-decision-update-2026-10-30.md` | `09cfa0d63131124374804ce5bb2b0bffa04de0a6d2ac0f70cd6e6174f1b81cb7` |
| Firmware handoff | `docs/firmware-handoff.md` | `3007de28ad245ae3a268f42fe4ffa9fda289e55473135b6e8a89207ee751cf19` |
| SdccDxe decode test | `scripts/test-gta4l-sdccdxe-decode.py` | `a346959cc424c46be49dccc4c6e6392f776a5297d938f72796e1b34bc2bffd78` |
| SPMIDxe descriptor-base test | `scripts/test-gta4l-spmidxe-descriptor-base.py` | `c835ee091399eba8c236e0cf5c268bf5d5b418ed77438b41297bb274dace436d` |
| Supplied-analysis audit | `docs/stock-firmware-analysis-audit.md` | `1185e1a702efa047a3e5b3e195ec126f1d62070a82d49397efa31ae0dd6f2dbf` |
| Vendor SDHC power sequence | `docs/vendor-sdhc-power-sequence.md` | `73028c8c605d44245f9c57b339c22ddefc8ca41c35b45bfc234a916c2563c995` |
| Repository README | `README.md` | `b6ec8ebd8cbfc3e696753d6a5a44ceb41d91989ed3da5f5144568f1d2d75e456` |
| eMMC reference | `docs/emmc-reference.md` | `d2c0167febb8d1afec6cba2afa9a842712c7f937c0c34514acd8077750054b3d` |
| NativeEmmc audit | `docs/native-emmc-reference-audit.md` | `c123577953c3c8625e2119967a112826448dd0a95c861023a9593bca0dc1a215` |
| Porting record | `porting.toml` | `2ce538b853df4556d26bc1049f9a5bd3e8e904fba65d634e594cbe81017ba2d5` |
| Porting-approach reassessment | `docs/porting-approach-reassessment-2026-10-31.md` | `ad77a51fbe36ab78333ba7c62e3f606ebbcede1549bfebc0bc5a08f777e8fa71` |
| Arch Linux gta4l evidence | `docs/archlinux-gta4l-evidence.md` | `9704e402411a31cbffe6dd1c1318c678e9275f96506728e18fe6c761116b13a7` |
| eMMC driver record | `docs/emmc-driver.md` | `02676d08d39d3e655cd1e4dc2e6762bb8a12ea5ff6e89eae98d3603397d91b65` |
| eMMC core header | `package/Gta4lEmmc.h` | `082a9da608d2f28baeee068eb043e18c41677d9dd9ec4bcd46c79446c8de867a` |
| eMMC core source | `package/Gta4lEmmc.c` | `ac7614e5ee00e190fba0e218dde8f77f4c24abc200bf9527e2f4e6c0b83af008` |
| eMMC XNU probe | `package/Gta4lEmmcProbe.c` | `da989ef4ebebae3bbf2a485cce4f20890a4e85caf17e2b281480d9e205e32077` |
| eMMC simulator | `scripts/sim-gta4l-emmc/harness.c` | `017405885bc4a6a29e72a2cc2caf927192c2b20ba3637e322b33470a9d60a9ec` |
| eMMC host test | `scripts/test-gta4l-emmc.py` | `21b99c488171f98d059d289a60c1a4c701b09027521ec8a6d952b9d62072d879` |
| eMMC archive builder | `scripts/build-gta4l-emmc-probe.py` | `ce5e4cd35a1164d78642abba3db7c9d34d383f58d08f7c5d6bb3a5087209e1f1` |
| RAM-root fixture reproduction | `docs/ram-root-fixture-reproducibility.md` | `f2004be7bb0a804b946d7ed97b79d8b24d4b644e104b44275aeae7812d944faa` |
| RAM-root fixture recipe test | `scripts/test-gta4l-ram-root-fixture-recipe.py` | `4366ea84438e84ee70d2c0ee9b911d0b5d58817352d27e0076829eedffdc1f8b` |
| Diagnostic payload/boot-image inventory | `docs/diagnostic-payload-input-inventory.md` | `799b35fd265459e405ce4ca727712a69159c4357d6cea8779d69e42f19fbb375` |
| Diagnostic payload build recipe | `docs/payload-build-recipe.md` | `4c7ddf9f62fd8921481ec0a2304fc0ce845a9ba46f7c3d2a6648e7c650002ab5` |
| ADT seed normalizer | `scripts/normalize-gta4l-adt-seed.py` | `cf80caafdd3b56fa590fed208e17e2eb8f5c27be1451dbaf1c57b0bbb7f76cf5` |
| Diagnostic payload builder test | `scripts/test-gta4l-diagnostic-payload.py` | `7478c30f44b862cf6d4ba483472ba95d1ce46875d81d3cca7cd93b4fc1a647d2` |

The table has 45 rows: the sixteenth batch added the eMMC core, probe,
simulator, test, builder and documentation rows, plus the Arch Linux evidence
row. The seventeenth batch refreshed the eight changed-file hashes without
adding a row. The stage-A batch refreshed six rows (`README.md`,
`docs/emmc-driver.md`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`,
`scripts/build-gta4l-emmc-probe.py` and `porting.toml`) without adding a row.
The A2/F2 batch refreshed nine rows (the same six plus `package/Gta4lEmmc.c`,
`scripts/sim-gta4l-emmc/harness.c` and `scripts/test-gta4l-emmc.py`) without
adding a row. The attempt-3 batch refreshed six rows (`docs/emmc-driver.md`,
`package/Gta4lEmmc.h`, `package/Gta4lEmmc.c`, `package/Gta4lEmmcProbe.c`,
`scripts/sim-gta4l-emmc/harness.c` and `scripts/test-gta4l-emmc.py`) without
adding a row. The A2/F2 attempt-3 hardware-run batch refreshed the `README.md`
and `docs/emmc-driver.md` rows without adding a row; the later-boot batch
refreshed those same two rows without adding a row. The Stage-B1 host-side
batch refreshed eight rows (`README.md`, `docs/emmc-driver.md`,
`package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`,
`scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py` and
`porting.toml`) without adding a row; the probe simulator, width fixture,
probe test, audit, CMD7 finding and this document are outside the table or
not listed. The Stage-B1 hardware-run batch refreshed three rows (`README.md`,
`docs/emmc-driver.md` and `porting.toml`) and two stale rows from the
`emmc-probe-4` payload-support commit
(`docs/diagnostic-payload-input-inventory.md` and
`scripts/test-gta4l-diagnostic-payload.py`) without adding a row;
`docs/emmc-cmd2-crc-audit.md` and this document are outside the table or not
listed. The Stage-B1 report-completeness batch refreshed five rows
(`docs/emmc-driver.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`,
`package/Gta4lEmmcProbe.c` and `porting.toml`) without adding a row; the probe
simulator, width fixture, probe test, CMD7 finding and CMD2 audit are outside
the table or not listed. The Stage-B1R2 hardware-run batch refreshed three
rows (`README.md`, `docs/emmc-driver.md` and `porting.toml`) without adding a
row; `docs/emmc-cmd2-crc-audit.md` and this document are outside the table or
not listed. The Stage-B1R2 cold-session batch refreshed those same three rows
(`README.md`, `docs/emmc-driver.md` and `porting.toml`) without adding a row;
`docs/emmc-cmd2-crc-audit.md` and this document are outside the table or not
listed. The Stage-B1C host batch refreshed six rows (`README.md`,
`docs/emmc-driver.md`, `docs/diagnostic-payload-input-inventory.md`,
`package/Gta4lEmmcProbe.c`, `scripts/test-gta4l-diagnostic-payload.py` and
`porting.toml`) without adding a row; the diagnostic builder/packager/gate
scripts, the probe simulator/fixture/test, `docs/emmc-cmd2-crc-audit.md`,
`docs/emmc-cmd7-timeout-finding.md` and this document are outside the table
or not listed. The Stage-B1C hardware-run batch refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a row;
`docs/emmc-cmd2-crc-audit.md` and this document are outside the table or not
listed. The Stage-B1C 1-bit discriminator batch refreshed ten rows
(`README.md`, `docs/diagnostic-payload-input-inventory.md`,
`docs/emmc-driver.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`,
`package/Gta4lEmmcProbe.c`, `scripts/sim-gta4l-emmc/harness.c`,
`scripts/test-gta4l-emmc.py`, `scripts/test-gta4l-diagnostic-payload.py` and
`porting.toml`) without adding a row; the probe simulator/fixture/test, the
diagnostic builder/packager/gate scripts, `docs/emmc-cmd2-crc-audit.md`,
`docs/emmc-cmd7-timeout-finding.md` and this document are outside the table
or not listed. The Stage-B1C-1 hardware-run batch refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a row;
`docs/emmc-cmd2-crc-audit.md` and this document are outside the table or not
listed. The data-timeout audit host batch refreshed nine rows (`README.md`,
`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`,
`package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`,
`scripts/sim-gta4l-emmc-probe/harness.c`,
`scripts/test-gta4l-emmc-probe.py` and `porting.toml`) without adding a row;
the diagnostic builder/packager/gate scripts, `docs/emmc-cmd2-crc-audit.md`,
`docs/emmc-cmd7-timeout-finding.md` and this document are outside the table
or not listed. The Stage-B1C-2 hardware-run batch refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a row;
`docs/emmc-cmd2-crc-audit.md` and this document are outside the table or not
listed. The Stage-B2 HS-timing host batch refreshed ten rows (`README.md`,
`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`,
`package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcProbe.c`,
`scripts/sim-gta4l-emmc/harness.c`, `scripts/test-gta4l-emmc.py`,
`scripts/test-gta4l-diagnostic-payload.py` and `porting.toml`) without
adding a row; the probe simulator/fixture/test, `docs/emmc-cmd2-crc-audit.md`,
`docs/emmc-cmd7-timeout-finding.md` and this document are outside the table
or not listed. The Stage-B2 hardware-run batch refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a row;
`docs/emmc-cmd2-crc-audit.md` and this document are outside the table or not
listed. The Stage-H9 CMD3-completion retry host batch refreshed ten rows
(`README.md`, `docs/diagnostic-payload-input-inventory.md`,
`docs/emmc-driver.md`, `package/Gta4lEmmc.c`, `package/Gta4lEmmc.h`,
`package/Gta4lEmmcProbe.c`, `scripts/sim-gta4l-emmc/harness.c`,
`scripts/test-gta4l-emmc.py`, `scripts/test-gta4l-diagnostic-payload.py` and
`porting.toml`) without adding a row; the probe simulator/fixture/test, the
diagnostic builder/packager/gate scripts, `docs/emmc-cmd2-crc-audit.md`,
`docs/emmc-cmd7-timeout-finding.md` and this document are outside the table
or not listed. The Stage-H9 hardware-run batch refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a row;
`docs/emmc-cmd2-crc-audit.md` and this document are outside the table or
not listed. The Stage-H10 host batch refreshed the `README.md`,
`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`,
`package/Gta4lEmmcProbe.c` and `porting.toml` rows without adding a row;
the probe simulator fixture/harness/test and the diagnostic wrapper/gate
scripts, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`
and this document are outside the table or not listed.
The Stage-H11 host batch refreshed the same five rows (`README.md`,
`docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`,
`package/Gta4lEmmcProbe.c` and `porting.toml`) without adding a row; the
probe simulator fixture/harness/test, the diagnostic builder/wrapper/gate
scripts, `docs/emmc-cmd2-crc-audit.md`, `docs/emmc-cmd7-timeout-finding.md`
and this document are outside the table or not listed.
The §6 self-check expected 35 until an earlier correction, then
36, then 37, and now expects 45.

Hash coverage scope: the 45-row manifest above does not include every file in
this batch. In particular, it has no row for
`scripts/sim-gta4l-emmc-probe/harness.c`,
`scripts/sim-gta4l-emmc-probe/report-width-fixture.c`,
`scripts/test-gta4l-emmc-probe.py`, `docs/emmc-cmd2-crc-audit.md`,
`docs/emmc-cmd7-timeout-finding.md`,
`scripts/test-gta4l-emmc-evidence-counts.py`,
`docs/pe-storage-stack-provenance.md`, `docs/linuxforgta4l-org-audit.md` or
`docs/linuxforgta4l-org-audit-manifest.tsv`; those files are outside the
manifest scope and no hash is claimed for them here. The row count stays 45.

The active tree carries the committed `bf2e7d4` source (`3f8f4531...`, the
qualified `HOST INIT REPORT` wording). The historical `bc228be7...` source
(`HOST INIT OK` wording) was used only for the isolated kernel replay and the
reproduced-kernel evidence packets; see `kernel-build-recipe.md` §5 and
`out/gta4l/ram-root-sdhci-repro-kernel-host-init-evidence/README.md`.

The HostInit source correction was a working-tree restoration to the existing
HEAD blob `3f8f4531...`; it created no content diff and is therefore absent
from both commits. The regression it removed was an uncommitted local revert to
`bc228be7...`. In the first follow-up snapshot, `porting.toml` changed only the
diagnostics finding that summarizes the power/pad report and names the new
decode regression test; it is not a build-input change.

## 3. Pinned Linux source cache

| Item | Value |
| --- | --- |
| Repository | `https://github.com/limonabisi/linux-gta4l` |
| Branch and revision | `gta4l-7.2`, commit `cf5f763f0ef8a5b4d8bb4163833cba1160c51894` |
| Cache path | `/tmp/opencode/linux-verify/` |
| Cache shape | 26 flat files at the snapshot; later additions are `gta4l.dts`, `mmc-card.h` and `mmc-mmc.c` |
| Sorted `sha256sum *` manifest digest (26-file snapshot) | `35b55f07119534f899da80ba1bae933b1bba67f91ec776553f21a91a150987e2` |
| Current 29-file sorted digest | `f34fec505fa58492eddaf833205f332a6cac534e4e2f31ad818812541a380e50` |

```text
98192e8a5bd656dd68fe624d03e4a48a6964b4066a3453268d32b05440e2ea90  board.dts
9896430492f89adde15547b72780e8e795dcdf2a7b46c87237fa69941172f8fd  core.c
9207beba399bf366902fcebf6a57fbb890c9f0289c37a301a3513b4d9d232f97  gcc-sm6115.c
45a71ac0cfb9b9d07bcb15eb93133ba0aa847aea1428ef072f75ba6e971b7bf5  icc-core.c
7e125d9308913c32803b0d482c070fb68356a00d19e0e9a687ac75fd5208fec0  icc-rpm.c
76792d3702e438e44a44ca93d86827940baa764833391319a8f887829e21b83c  icc-rpm.h
56678ec9c220d952b63f4e2af7072f7d3b815cd867c7809c937b5f38cddd5304  mmc-core.c
3eee0021701861692f738d359e0773af8eb046633b33988edb03c0b90c488777  opp-core.c
5f181cb21628ed3043fbf5329875fc56b74564770b3755ad8799502811f8e95f  pm6125.dtsi
32e2f6b0988eba52a2f662fdc2d7132022fbe980f281b2a82babcf3d7f46e18a  pmdomain-core.c
d6638c03c09f5c0b24d9e343e19fb04ad7d483ec4f1c5fdabde4efc3570d59f8  pinctrl-msm.c
68e7845e7f785a84c476b03b347b09fbf6789de991f4d0684b81d2af6354d814  pinctrl-msm.h
c993ffc565c2bb3816ea808d45e23b160c5aa4b77c45a0a176d932307faeaa87  pinctrl-sm6115.c
2da9e5b25ed33eaff3ebd0c8e497c0fc045c019156e686a8d5675e21e6fc9c57  pmi632.dtsi
cba250364921bfff3e8cfe10372906631cf2cba70365b2d75eabb8822084b9bd  qcom-spmi-pmic.c
4148ae42b825fecf124e964e585f775597a77e99df83e274f7236d1640c32370  qcom_smd-regulator.c
09436b8793dc69bf0805f941512b796b0831507d0c7d9d566ce186b6031c0dbc  qcom_spmi-regulator.c
9896430492f89adde15547b72780e8e795dcdf2a7b46c87237fa69941172f8fd  regulator-core.c
6c6a0b277d001297d11cddeeb87e9f4367387a622dcfc6ca42c000754e0a722a  regulator.c
4aba9e20b5e68105fb68a378011248ad016b439f064b22fb6e56281c38c459e3  rpmpd.c
3ba6c4baf1adeaa90043f46db08dfc9df2809209b4e29386e9148d26da827a9a  runtime.c
b668e01dfcb6efac44249c287c6b8583b0a3d584f497e82d33df223cee61f53e  sdhci-msm.c
186305e102bd32df419ff80669e6018d6b3a9e4310c3101193413970355b9857  sdhci.c
d4cff8991af9bde1820f872b75bcb8259b8a0e4c87cf976582b61674707d7efb  sm6115.c
5b91a04c3119f2c846b620e1dc9fd50ce6ed61f07cf1ef2674c640c23a432d9c  sm6115.dtsi
320e71b64915eb4e32be90b49fd65f12da2241e877bf4f68f0c0c1eb4ab3e28d  spmi-pmic-arb.c
```

The cache has no git metadata, so the revision is a documented fetch provenance,
not a property that can be independently verified from the cache alone. If the
cache is lost, re-fetch the listed files from the pinned commit and compare the
manifest digest `[S]/[U]`.

The 2026-11-01 LinuxForGta4l comparison used a separate host cache,
`/tmp/opencode/lfg4l-audit/`, and did not alter the limonabisi cache or its
digest. That cache has no git metadata either; its durable retrieval inventory
is `docs/linuxforgta4l-org-audit-manifest.tsv`, which records the repository,
immutable SHA, path, computed blob hash, size, SHA-256, license note and
variant for 69 files. The audit document is `docs/linuxforgta4l-org-audit.md`.
Neither file is a row in the 45-row table `[S]`.

## 4. Firmware and boot artifacts

| Artifact | Path or identity | SHA-256 | Status and relationship |
| --- | --- | --- | --- |
| Booted corrected v2 image | `out/gta4l/ram-root-sdhci-host-init-evidence/boot-ram-root-sdhci-host-init-v2.img` | `d4aee57a9d1874d23dc010b7de43384ace6ab29241f5ee18cb10f0a561bab133` | Present in the gitignored output tree; not tracked by git `[S]` |
| Local firmware volume | `SILICIUM_UEFI.fd` | `3d5dbd6ff28093e38a16c02af813fe7d2624d8e9b83ff407045e467e86904f5a` | Present; gzip-wrapped `[S]` |
| Local decompressed source | inner extraction of the above | `7c20a5b9165100a2f07edc58d2b348e576b372e368af0ebd6ecdeceac50ba10e` | 3,145,840 bytes; 112-byte prefix plus outer FV `[S]` |
| Local outer FV | inner extraction of the above | `4a8ca2cddcd578391dfd846ae994dafc7590b68e1e7ebde8f86ceca0c904b676` | 3,145,728 bytes; differs from booted outer FV `42f900a3...` `[S]` |
| Local inner FV | inner extraction of the above | `2443e52a31a3208c70d10e5a6a733c285de48dc1b48358f626cc1557f7d5f6b5` | 6,991,872 bytes; 90 PE modules; differs from booted inner FV `3bb7847a...` `[S]` |
| Booted outer FV | recorded in `firmware-handoff.md` | `42f900a3...` | 3,145,728 bytes `[S]` |
| Booted inner FV | recorded in `firmware-handoff.md` | `3bb7847a...` | 8,339,456 bytes `[S]` |
| SdccDxe PE | `../Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/SdccDxe/SdccDxe.efi` | `4084bbc745d66fd81eae0caf7eacb19f7204076d4ee88adc7cda45c949d5674b` | Same bytes in the local volume and the booted image `[S]` |
| PmicDxeLa PE | `../Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/PmicDxe/PmicDxeLa.efi` | `db541f6302efee1546beea9b4dee9362fca5f50bcec2f494e560c150464e32ce` | Same bytes in the local volume and the booted image `[S]` |
| SPMIDxe PE | `../Mu-Silicium/Binaries/gta4l/QcomPkg/Drivers/SPMIDxe/SPMIDxe.efi` | `50be2e1a0972843af5de67d4d0b80aba68634259849459897922716a25d3e254` | Same bytes in the local volume and the booted image `[S]` |
| UEFI platform config | `../Mu-Silicium/Binaries/gta4l/RawFiles/uefiplat.cfg` | `1b8c0b8035d8b858dc8c7a763ab3b874dd16f8221295cbab116879bcf08f9c3d` | Present `[S]` |
| RPM image | `../../device-dump/private/gta4l-dump/partitions/rpm.img` | `272bebd3ddd7ef223d6e95e9b4cd30385a3df0eb4fa7beae6b622f457b4937c6` | Private device dump; static analysis only `[S]` |
| XBL image | `../../device-dump/private/gta4l-dump/partitions/xbl.img` | `a09c7b62e639a0565c30b7a3b61e2d68f2a07bfe9c3199d38f9af17bdf9e073e` | Private device dump; ELF64 AArch64 `ET_EXEC`, 17 program headers, no sections; 15 `LOAD` segments including `0x5fc00000`; policy not analyzed `[S]/[U]` |
| XBL config image | `../../device-dump/private/gta4l-dump/partitions/xbl_config.img` | `6dbaad63204b761e7d8fb4dba7c861d97b25dc5094aabc0a9fe45f19fb20d99e` | Private device dump; embeds `/pmic_settings.bin` at `0x3054`, size `0x7240`, SHA-256 `410e45ac...f4e97597`; static analysis only `[S]` |
| Supplied stock-firmware analysis | archive from https://github.com/NoNameBuilder; not vendored | `6d750114c4465e894a043ce190ea3a776d25a3bf862d5b50df3d95d73f2368e9` | 214/214 manifest-listed files match; supplied XBL bytes and DT overlays differ from the captured state; audit in `stock-firmware-analysis-audit.md` `[S]/[U]` |
| Samsung CXG1 source archive | owner-supplied `sos/` archive; not vendored | `84e8f9934987834da6867d37243bb18fe6fab623019e3d7a4e10fa603f069f8e` | Kernel 4.19.157, `gta4l_mea_jt_defconfig`; official vendor source; not the shipped binary `[S]/[U]` |
| Samsung CWI2 source archive | owner-supplied `sos/` archive; not vendored | `b00cbe4827b62af8ea888b89b000e31368b644e5ec0c07ba6b65a164718081fb` | Comparison release; relevant files byte-identical to CXG1 `[S]` |
| Live FDT blob | `../../device-dump/private/gta4l-dump/live-fdt.dtb` | `da1cb76fcf9031de8ee2f1152456994220d3d01a2a2b73b211b684e92a0b6ebc` | Captured vendor FDT `[S]` |
| Decompiled live DTS | `../../device-dump/private/gta4l-dump/live.dts` | `81c1865589df632ae76b77edd44eb9326b317f8abef471465504618636a9b629` | Text used for DT citations; not the booted blob `[S]` |

The local `SILICIUM_UEFI.fd` is **not** the booted image. It packages the same
three driver PEs, but its outer and inner firmware volumes differ and its
`DxeCore` is `f37f7d2b...` rather than the booted `f4aefa94...`
(`firmware-handoff.md`). The booted artifact is present locally in the
gitignored output tree, not tracked by git; its hash is a recorded provenance
value, not a reproduction. Do not present the current tree or the local
firmware volume as the previously booted image `[S]`.

Private firmware and device dumps are referenced by path and hash only; their
contents are not duplicated in this repository `[S]`.

Build provenance note: the Mu-Silicium parent is at `e14626a3...`; its
`Mu_Basecore` submodule carries 10 uncommitted platform edits at this snapshot.
The payload and boot images tested here preserve the v5 baseline firmware bytes
and do not rebuild firmware from that subtree. A future firmware rebuild must
pin or discard those edits `[S]`.

## 5. Evidence categories

| Category | What it is | What it is not |
| --- | --- | --- |
| Current host-tested source | Probe, harness, simulator, oracle, counts and policy documents at the hashes above | Not a hardware, electrical or retention result `[S]` |
| Official vendor kernel source | Samsung CXG1/CWI2 source archives, pinned by ZIP/tarball hash | Not the shipped binary, not a board schematic; the archive contains no vendor DTS `[S]/[U]` |
| Previously booted controller-local image | Recorded v2 image hash and its controller-local result | Not reproduced by this tree and not a power result `[S]` |
| Captured vendor DT | `live-fdt.dtb`/`live.dts` board description | Not proof of the XNU-time rail, pad or regulator state `[S]` |
| Static firmware analysis | RPM LDOA resource tables, XBL boot PMIC settings and Mu-Silicium exit traces | Not runtime behavior; the RPM consumer/policy, PBS semantics, indirect effects and untraced callbacks remain `[U]` |
| Supplied third-party analysis | stock-firmware archive from https://github.com/NoNameBuilder, manifest-checked and audited | Not authenticated, not the captured XBL or live FDT, and not a measurement `[S]/[U]` |
| Physical measurements | None | No voltage, current, transient, pad or retention measurement exists `[U]` |

## 6. Reproduction

Run from `src/UniXNU-gta4l/`:

```sh
git rev-parse HEAD
git status --porcelain
sha256sum package/Gta4lSdhciHostInit.c package/Gta4lSdhciHostInit.h \
  scripts/sim-gta4l-sdhci-host-init/harness.c \
  scripts/test-gta4l-sdhci-host-init.py \
  scripts/test-gta4l-sdhci-host-init-sim.py \
  scripts/oracle-gta4l-sdhci-host-init.py \
  scripts/test-gta4l-sdhci-evidence-counts.py \
  scripts/test-gta4l-sdccdxe-decode.py \
  docs/sdhci-host-init.md docs/sdhc-decision-memo.md \
  docs/sdhc-evidence-gap-checklist.md \
  docs/sdhc-electrical-contract.md docs/power-retention-evidence.md \
  docs/rpm-power-interface.md docs/pm6125-register-map.md \
  docs/sdhc-dependency-trace.md docs/firmware-handoff.md \
  docs/stock-firmware-analysis-audit.md \
  docs/vendor-sdhc-power-sequence.md README.md \
  docs/emmc-reference.md docs/native-emmc-reference-audit.md \
  docs/pe-storage-stack-provenance.md \
  docs/sdhc-power-pad-dependency-report.md \
  docs/sdhc-decision-update-2026-10-30.md \
  docs/ram-root-fixture-reproducibility.md porting.toml \
  docs/pmic-audit.md docs/spmi-0x28-analysis.md \
  scripts/analyze-gta4l-rpm-ldoa.py scripts/test-gta4l-rpm-ldoa-analysis.py \
  scripts/test-gta4l-ram-root-fixture-recipe.py \
  scripts/test-gta4l-spmidxe-descriptor-base.py
( cd /tmp/opencode/linux-verify && sha256sum * | sort -k2 | sha256sum )
python3.11 -B scripts/test-gta4l-sdhci-host-init.py
python3.11 -B scripts/test-gta4l-sdhci-host-init-sim.py
python3.11 -B scripts/oracle-gta4l-sdhci-host-init.py
python3.11 -B scripts/test-gta4l-sdhci-evidence-counts.py
python3.11 -B scripts/test-gta4l-sdccdxe-decode.py
python3.11 -B scripts/test-gta4l-emmc.py
python3.11 -B scripts/test-gta4l-emmc-probe.py
python3.11 -B scripts/test-gta4l-emmc-evidence-counts.py
python3.11 -B scripts/test-gta4l-spmidxe-descriptor-base.py
python3.11 -B scripts/test-gta4l-ram-root-fixture-recipe.py
python3.11 -B scripts/test-gta4l-rpm-ldoa-analysis.py
python3.11 -B scripts/analyze-gta4l-rpm-ldoa.py \
  ../../device-dump/private/gta4l-dump/partitions/rpm.img --code
```

The cache has no `.git` directory. The `linux-verify` command now includes the
three later additions and prints the 29-file digest `f34fec50...`; the
historical 26-file snapshot digest is `35b55f07...` in §3.

The analyzer command needs the private `rpm.img`; it refuses a different image
hash unless `--expect-sha256` is given explicitly. `--code` checks the
documented Thumb call sites, literals, destination registers, registration
setup and class registration. The self-test uses only a synthetic image `[S]`.

`scripts/test-gta4l-sdccdxe-decode.py` reads the sibling Mu-Silicium
`SdccDxe.efi` when present and otherwise extracts the same pinned PE from the
committed `SILICIUM_UEFI.fd`. It pins instruction bytes and models the loop
behaviour; it does not execute firmware `[S]`.

The `fs-timer` fixture reproduction is recorded in
`docs/ram-root-fixture-reproducibility.md`; it rebuilds the signed fixture,
trust cache and root image byte-for-byte with the pinned cctools-port and
`rcodesign` versions. The hash table above can be checked mechanically:

```sh
python3.11 - <<'PY'
import hashlib, pathlib, re
text = pathlib.Path('docs/evidence-baseline.md').read_text()
rows = re.findall(r'^\| [^|]+ \| `([^`]+)` \| `([0-9a-f]{64})` \|$', text, re.M)
expected = 45
paths = [path for path, _ in rows]
assert len(rows) == expected, f'expected {expected} rows, found {len(rows)}'
assert len(set(paths)) == len(paths), 'duplicate paths in the table'
bad = []
for path, want in rows:
    got = hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
    if got != want:
        bad.append((path, got, want))
print(f'rows={len(rows)} mismatches={len(bad)}')
for path, got, want in bad:
    print(f'{path}: {got} != {want}')
raise SystemExit(1 if bad else 0)
PY
```

### 6.1 Validation record

#### Historical validation record (preserved)

After the final edits, the host checks were run from `src/UniXNU-gta4l/` with
`LC_ALL=C` and `python3.11` (`/usr/bin/python3.11`):

| Check | Result |
| --- | --- |
| `scripts/test-gta4l-diagnostic-payload.py`, `test-gta4l-diagnostic-package.py`, `test-gta4l-boot-image.py`, `test-gta4l-sdhci-handoff-audit.py`, `test-gta4l-sdhci-evidence-counts.py`, `test-gta4l-xnu-mtimes.py`, `test-gta4l-ram-root-fixture-recipe.py`, `test-gta4l-rpm-ldoa-analysis.py`, `test-gta4l-sdhci-host-init.py`, `test-gta4l-sdhci-host-init-sim.py`, `oracle-gta4l-sdhci-host-init.py`, `check-gta4l.py` | All exited 0 |
| `scripts/test-gta4l-sdccdxe-decode.py` (static PE/model check, separate from the diagnostic tests) | PASS: pinned SdccDxe SHA-256, handshake and pad instruction words, MOVZ-derived loop bounds, last-read and delayed-completion boundary cases, wrong-bound negative controls, captured-status selection, three non-aborting timeout paths, TLMM arithmetic, SDC1 pad decode and SDC2 CLK-pull field positions |
| `scripts/test-gta4l-spmidxe-descriptor-base.py` (static PE/scan check, separate from the diagnostic tests) | PASS: pinned SPMIDxe SHA-256, `0x38b0` read/override words, `0x38c8` caller and `.data` layout, one `0x8878` read, no write intersecting `[0x8878,0x8880)`, resolved caller stores at `0x38bc`/`0x38f4`, 150 unresolved non-stack loads and 36 unresolved non-stack stores, explicit revisited-state/step/unsupported/call-depth/indirect-branch truncations, 64-bit ADD/SUB and effective-address wrapping with the demonstrated overflow regression, paired access/writeback and single/paired address-space-boundary controls, a boundary-crossing load that invalidates its derived store, ADR-to-XZR, and pointer-chain, overlap, partial-store, pre/post-index, call-clobber, XZR/SP, XZR-root, branch-separation, callee-conditional, revisit, stale-pointer, cross-call, truncation and decoder controls; all six result sets are unchanged from the audited `4c447b9` baseline in a site-level set comparison with zero added and zero removed records in accesses, stores, calls, boundaries, unresolved and clobbers |
| Workflow token scoping (offline harness) | PASS: all 5 extracted patch strings attach the dummy token only to HTTPS GitHub hosts and strip it on cross-host redirects; no real token was used |
| Official Samsung source comparison | PASS: CXG1/CWI2 ZIP and tarball hashes recorded; kernel 4.19.157; six relevant driver files byte-identical; no vendor DTS |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| Native eMMC core test (historical) | PASS: 45 simulator scenarios and 19 mutation controls; the production core compiles with `-std=c11 -Wall -Wextra -Werror` and with the real `bsd_init.o` compile record. This row is the historical native eMMC core suite from `92ccf3b`, not the controller-local host-init suite (`Gta4lSdhciHostInit`) and not the later 80/42 native eMMC core suite. |
| eMMC power-handshake regressions | PASS: the bus-only request is acknowledged with `BUS_SUCCESS | IO_SUCCESS`; a delayed IO request arriving after the old two-read quiet window is handled; a wrong-level IO request is not accepted as completion, while a later correct-level request is handled; a correct level followed by a wrong level, a late standalone `BUS_ON`, a status word with both IO levels, an unchanged `VDD_180` followed by a standalone `BUS_ON`, and a wrong-level request raised on the ack write all fail with the card clock disabled; a churning request sequence fails with `GTA4L_EMMC_ERR_POWER` |
| eMMC kernel link | PASS: `./unixnu build kernel -D ../UniXNU-gta4l` links the probe through `CONFIG_XNU_EXTRA_LD_ARCHIVES`; `kernel.macho` sha256 `1a4710892e49fa96c053de8f33f1b4b5d89a36fc3d688febd6fa0f7e4a89003a` |
| eMMC stage-2 payload | PASS: 80 MiB raw payload sha256 `2953e90220f1e2d9341fc154f3f2eb6523ab2539be68404a3beffa8ee4702633`, kernel args include `gta4l-emmc-probe=2`, payload contract 7/7 |
| eMMC stage-2 boot image | PASS: 85,688,320-byte image sha256 `4f2252df6bbd5d9c7f05ef97c0041da9acdc47212debf31a10334aefa45a12e5`, boot-image gate 18/18; build was not marked hardware-tested |
| eMMC stage-2 BOOT write and readback | PASS: BOOT-only write on SM-T505N; the first 85,688,320 bytes read back identical to the image; recovery stayed byte-identical TWRP `14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae`; previous boot saved as `dcfe6852035edbb4109231cd41dc5e618e1b8755f47188c1a01bf22e36fcce7b` |
| eMMC stage-2 hardware report | PASS: `GTA4L-EMMC HOST REPORT rc=0 mci=10000072 pwr=0f clk=0007 hc2=0008 ven=00318a0c pst=00000000 log=4/1/268435570`; photo sha256 `d049547e8fd6135aafe6fb8aff13c9e8295cb9e2012b3944cdb66ac364882a2c`. Controller-local host-init result only; no card identification or sector read. |
| eMMC stage-3 payload | PASS: 80 MiB raw payload sha256 `4028e432f200dc1069a50f1baa6786d2f4861b187f2c5a0bd126bb20f5a9cd41`, kernel args include `gta4l-emmc-probe=3`, payload contract 7/7 |
| eMMC stage-3 boot image | PASS: 85,688,320-byte image sha256 `88b4d28607b371fe008922131c6ee51f27f73da30249fd4a07969f770fa90d87`, boot-image gate 18/18; build was not marked hardware-tested |
| eMMC stage-3 BOOT write and readback | PASS: BOOT-only write on SM-T505N; the first 85,688,320 bytes read back identical to the image; recovery stayed byte-identical TWRP `14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae`; previous stage-2 boot saved as partition sha256 `545b57ebc3d451cae0ab87b8020fa28619c144023300833892481b529abf8475` |
| eMMC stage-3 hardware report | FAIL: `GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967288 mci=10000072 pwr=0f clk=0007 hc2=0008 ven=00318a0c pst=00000000 cfg=00002017 cmd=00000000 log=4/1/268435570`; `rc` is `-8`, `GTA4L_EMMC_ERR_CMD_CRC`. Identification did not complete; that older report records no RCA, capacity, EXT_CSD, OCR, CID, CSD or sector data. Later current-source reports record CMD1 OCR `0xC0FF8080` and no validated CID/CSD/EXT_CSD/capacity or sector result. Photo sha256 `0014db219d5d3dd1b1db572845d81c8a6fb966d0912c0c23aae02c9d70a082c9`. |
| XNU probe compile | PASS: `Gta4lEmmc.c` and `Gta4lEmmcProbe.c` compile against the pinned XNU headers with `-Wall -Wextra`; the archive exports `gta4l_emmc_probe_preflight`, `gta4l_emmc_probe_run`, `gta4l_emmc_init` and `gta4l_emmc_read_block` |
| Patch-series apply | PASS: `sheng-platform-power`, `gta4l-sdhci-snapshot`, `gta4l-sdhci-handoff-audit` and `gta4l-emmc-probe` apply in order to the clean `bsd_init.c` |
| Reassessment re-run 2026-10-31 | PASS: `test-gta4l-sdhci-host-init.py`, `test-gta4l-sdhci-host-init-sim.py`, `oracle-gta4l-sdhci-host-init.py`, `test-gta4l-sdhci-evidence-counts.py`, `test-gta4l-sdccdxe-decode.py`, `test-gta4l-spmidxe-descriptor-base.py`, `test-gta4l-ram-root-fixture-recipe.py`, `test-gta4l-rpm-ldoa-analysis.py` and `check-gta4l.py` all exited 0; `git diff --check` clean |
| `porting.toml` parse with `tomllib` | 13 areas |
| `git diff --check` | Clean |

#### Current validation record (2026-11-02 attempt-3 host-side follow-up)

The following host results were produced after the attempt-3 bounded post-bound
drain changes (which build on the 2026-11-01 CMD7 diagnostics width-closure and
terminal-read observation-boundary edits), run from
`src/UniXNU-gta4l/` with `LC_ALL=C` and
`/usr/bin/python3.11`: `scripts/test-gta4l-emmc.py`,
`scripts/test-gta4l-emmc-probe.py`, `scripts/test-gta4l-emmc-evidence-counts.py`,
`scripts/test-gta4l-sdhci-evidence-counts.py`, the §6 hash-table self-check and
`git diff --check`; `porting.toml` parses with `tomllib` (13 areas).

| Check | Result |
| --- | --- |
| Current native eMMC core test | PASS: 179 simulator scenarios and 104 mutation controls, including the Stage-B1 4-bit/8-bit switch, CMD6 and bus-test error cases, the verified 4-bit switch (read-only properties comparison with the unreadable byte-183 value held at zero), the verified mismatch, failed-rollback and CMD6-error cases, the baseline-based recovery pass, surviving-inhibit and mismatched-properties cases, the corrected CMD14 CRC exemption and 1-bit two-bit pattern, the Stage-B2 HS enter/leave, ignored byte-185, SWITCH_ERROR and busy-RCG cases, the Stage-H9 CMD3 retry clean/pass/fail/status/later-failure cases, the Stage-H13 data-reset clear/stuck, surviving-inhibit, CMD14 mismatch/read-timeout/read-CRC/read-status, non-wedged-write and non-timeout-write cases, and the Stage-H14 clean/silent/CMD13-CRC/exempt-inhibit/uninitialized post-write drain plus the rung-2 reset-plus-drain pass, record-only silent drain and surviving-inhibit cases |
| Current eMMC probe report test | PASS: 163 scenarios and 77 mutation controls, including the Stage-H15 verified 4-bit switch, the reproduced 4-bit write wedge with clean and unproven baseline-based recovery, the Stage-H16 content-only pass and divergence, ignored CMD14 pattern-CRC and content-divergence reports, field-wise width-bound failure, identification and full-probe reports (1093, 788 and 1493 bytes), twelve width-bound materializations, two width-fixture narrowing controls, two full-probe block-read failure controls, the stage-A GPT gate and 21-sector array content phase, four stage-A loop mutation controls (duplicate LBA, continued error, block-count off-by-one, wrong last LBA), the optional A2/F2 CMD18+CMD12 success, mismatch, mid-transfer failure, stop-timeout and bare-argument refusal cases, the post-bound drain TC/late-BRR/error/exhaustion cases with the `drain=` report field, a missing CMD18 Multi/Single Block Select bit, the Stage-B1 4-bit/8-bit success, stuck-width, zero-readback, host-width-stuck, 8-bit-stuck, bus-test-failure and content-divergence reports, the Stage-B1C success, continuation-pass, late-commit, continuation bus-test failure, continuation content mismatch and content/re-read errors, CMD6-error, empty-readback and lost-host-width reports, the B1 field-wise width bound with `bc`/`e183a`/`esa`, the Stage-B2 pass, ignored byte-185, busy-RCG, content-divergence, content-read, revert-failure, recovery-failure and pre-read scenarios, the TOUTVAL-only arm artifact, the Stage-H9 clean/no-retry, retry-pass, retry-fail, retry-second-ident and mismatch reports, the Stage-H10 pass, HS-ignored, busy-RCG, width-ignored, bus-test-timeout, bus-test-mismatch, content-divergence, content-read, width-revert-failure, HS-revert-failure and recovery-read reports, the Stage-H11 honored-pass, W4-replay, W4-mismatch, W8-divergence, W8-error, W4-anomaly, clock-busy, bus-test-timeout, bus-test-mismatch, content-read, content-divergence, width-revert-failure, HS-revert-failure and recovery-read reports, the Stage-H12 refused-pass, honored, DDR-error, DDR-paradox, DDR-empty, HS-ignored, clock-busy, width-revert-failure, HS-revert-failure, recovery-read and pre-read-failure reports, the Stage-H13 pass, reset-stuck, inhibit, mismatch, read-timeout, read-CRC, read-status, write-CRC, normal-write and recovery-read reports, the Stage-H14 pass, reset-stuck, inhibit, CMD12-silent, CMD12-CRC, CMD13-failure, mismatch, read-CRC, recovery-read, write-CRC and normal-write reports, and the oversized-report rejection control |
| Stage-B1 bus-width/bus-test implementation | HOST-TESTED: CMD6 byte-183 switch to 4/8-bit with readback and `HOST_CONTROL` bits, CMD19/CMD14 pattern round trip, per-width stage-A content comparison, CMD6 status plus host-before/host-after and EXT_CSD readback sum, before/after pad-DLL samples, 2048-byte report bound; hardware run recorded in the next row |
| Stage-B1 report-completeness audit (2026-11-02) | HOST-TESTED ONLY: `probe-b1-stuck-width` (card ignored the CMD6), `probe-b1-zero-readback` (empty EXT_CSD read) and `probe-b1-host-width-stuck` (lost `HOST_CONTROL` width) assert distinct `c6`/`hb`/`ha`/`es` tuples; the success case asserts each width's readback sum; the field-wise fixture still fits 2048 bytes. No build, flash or boot; F3 remains `[U]` |
| eMMC Stage-B1 hardware run (2026-11-02) | RECORDED: three boots of the verified B1 image; attempt 1 stopped pre-B1 at the H8 CMD3-completion timeout (`stage=4`, `rc=-7`, `step=9`), attempt 2 reached B1 and stopped at the 4-bit switch readback (`sw=0`, `swrc=-19`, `stop=1`), attempt 3 stopped pre-B1 at the block-1 CMD17 (`rc=-11`, `normal=0x0002`, TC only). No CMD19/CMD14 or per-width content pass ran; F3 remains `[U]` |
| eMMC Stage-B1R2 hardware run (2026-11-02) | RECORDED: five owner boots of the rebuilt `24f8c9d` image (warm force reboots only). Boots 1-2 reached B1 and reproduced the 4-bit stop with a clean CMD6 R1 (`c6=00000900`), the host width bit set and read back (`hb=00`, `ha=02`) and a successful EXT_CSD read returning the 1-bit content (`es=000009cb`, byte 183 still 0); boots 3-4 were TC-only/no-error CMD17 timeouts (block 1, array LBA 2) and boot 5 the H8 CMD3 timeout. No CMD19/CMD14 or content pass ran; F3 remains `[U]` |
| eMMC Stage-B1C hardware run (2026-11-03) | RECORDED: seven owner boots of the `5ad1dd6` image (two cold, five warm). Boots 3 and 6 entered the continuation on the pinned 4-bit tuple and both timed out in the CMD19 write phase (`tt=1`, `trc=-11`, `normal=0x0011`, present `0x03f801f6`; CC and BufferWriteReady latched, no TC, no error), identical except the stack counters, with a cold power cycle between them. The other five stopped pre-B1 (four CMD17 TC-only/no-error timeouts, one H8 CMD3 timeout); the p69 re-read after boot 3 matched the post-flash hash. F3 remains `[U]` |
| eMMC Stage-B1C 1-bit discriminator host batch (2026-11-03) | HOST-TESTED ONLY: `gta4l-emmc-probe=6` runs one 1-bit CMD19/CMD14 round trip after the stage-A pass and before the first CMD6; a failure stops with `stop=9` and no CMD6, a pass falls through to the stage-5 flow. The `BW=1` line carries the 1-bit `tt`/`tw`/`te`/`trc` for stage 6 and stays byte-identical for stages 4/5. Core 138/83 and probe 71/42, including the 1-bit write-phase timeout class and a pass-plus-continuation case; no build, flash or boot, F3 remains `[U]` |
| eMMC Stage-B1C-1 hardware run (2026-11-03) | RECORDED: thirteen owner boots of the `5f57dcb` stage-6 image (seven cold, six warm) without reflashing; both p69 re-reads matched `1ea626f5…`. Boots 1 and 12, both cold, reached the 1-bit discriminator and timed out in the CMD19 write phase with the same terminal class as the 4-bit continuation (`tt=1`, `tw=00000000`, `te=00000000`, `trc=-11`, CC + BufferWriteReady, no TC, no error, present `0x03f801f6`), field-for-field identical, and no CMD6 was issued — the data-write path is broken independent of bus width and the width-caused model is excluded. The other eleven stopped pre-B1 (eight H8 CMD3, three CMD17 TC-only, one at the GPT array's LBA 2). The `trc=-20` class remains unobserved; F3 remains `[U]` |
| eMMC data-timeout audit host batch (2026-11-03) | HOST-TESTED ONLY: `gta4l-emmc-probe=7` runs after the stage-A pass with no CMD6 and always reports the `GTA4L-EMMC DTO REPORT`: R1 reads back the timeout/enable registers, E1 arms the minimum timeout and reads 8 blocks through CMD18+CMD12 (≈83 ms, well past the 2^13-cycle minimum), Q re-reads block 0 to prove recovery, and E2 repeats the 1-bit CMD19 wedge under an 8 s bus-test window. Stop codes 0/10/11/12/13/14 cover the E1 result, recovery failure, arm mismatch, E2 pass and an unexpected E2 class. Probe 75/46 (was 71/42) with four scenarios and four mutations, including a read-count proof of the extended window; core 138/83 unchanged; the report is 518 bytes. No build, flash or boot, F3 remains `[U]` |
| eMMC Stage-B1C-2 hardware run (2026-11-03) | RECORDED: twelve owner boots of the `d158378` stage-7 image (one cold, eleven warm) without reflashing; the post-batch p69 re-read matched `13f88342…`. Five boots (1, 3, 8, 9, 10) reached the DTO stage with identical `stop=12` reports; the stop is a probe-gate artifact (TOUTVAL already zero; the readback differs only on the read-only `INT_STABLE` bit). E1 was silent: no error bit of any kind latched while BufferReadReady streamed seven blocks roughly 3× past the 20.5 ms minimum, then CMD12 got no R1b (`-7`) and the block-0 recovery read ended in ERR_INHIBIT (`-17`), so the controller never raises data-timeout in this configuration. The other seven boots stopped pre-B1 (six block-1 CMD17 TC-only, one post-CMD7 CMD13 completion timeout). E2 did not run; F3 remains `[U]` |
| eMMC Stage-B2 HS-timing host batch (2026-11-03) | HOST-TESTED ONLY: `gta4l-emmc-probe=8` runs after the stage-A pass with no CMD19/CMD14, width switch or user-data write; the only card write is EXT_CSD `HS_TIMING` byte 185. R samples byte 185, `CLOCK_CONTROL` and the pad/DLL state; S sends CMD6 SET index 185 value 1, waits TRAN/ready, reads byte 185 back and ramps the GCC SDCC1 APPS RCG to the pinned 50 MHz floor row (`0x20B`, M/N/D zero; the 52 MHz class floors to 50 MHz); T repeats the full stage-A content pass at 50 MHz and compares every sum with the 400 kHz baseline; V drops the clock to 400 kHz first, clears byte 185, reads both back and re-reads block 0. Stop codes 0/10/11/12/13/14 cover pass, switch/byte-185 failure, clock failure, content read/divergence, revert failure and recovery failure. The stage-7 arming gate now compares only TOUTVAL, with a new arm-artifact scenario. Core 143/86 (was 138/83), probe 85/51 (was 75/46), six width materializations including `report-width-b2`; no build, flash or boot, F4 remains `[U]` |
| eMMC Stage-B2 HS-timing hardware run (2026-11-03) | RECORDED: four owner boots of the `9a84215`+`9d6fd1f` stage-8 image (boot 1 cold, boots 2–4 warm) without reflashing; the post-batch p69 re-read matched `2cd82e0f…`. Boots 2 and 4 produced field-for-field identical `rc=0 stop=0` reports: `S` read byte 185 = 1 with CMD6 R1 `00000900`, `T same=1 div=0` with all stage-A sums matching at 50 MHz after the RCG reached the pinned `0x20b` floor row, and `V` reverted to `hs=00`/RCG `0x2017` with block-0 matching. Boots 1 and 3 stopped pre-B2 at the H8 CMD3-completion timeout. F4 measured PASS at 1-bit/50 MHz (content-level only); F3 remains `[U]` |
| eMMC Stage-H9 CMD3-completion retry host batch (2026-11-03) | HOST-TESTED ONLY: `gta4l-emmc-probe=9` runs the standard read-only identification sequence once with the pinned 8-us post-ready CMD1 delay and, on the H8 CMD3 completion timeout, snapshots attempt 1 and re-issues exactly one identical CMD3 (`0x00010000`, same timing path); a third attempt is impossible and there is no fall-through. The stage stops after the TRAN wait before any EXT_CSD read or stage-A pass, so it issues no CMD6, width switch or user-data access; the only new card-facing action is the duplicate CMD3. It always emits `GTA4L-EMMC H9 REPORT` (retry engaged/ok, two attempt slots with result/R1/fail record/pre-command and timeout samples/poll history) and stops with 0/10/11/12/13/14/15. Core 148/90 (was 143/86), probe 91/51 (was 85/51), seven width materializations including `report-width-h9`; no build, flash or boot, F3 remains `[U]` |
| eMMC Stage-H9 CMD3-retry hardware run (2026-11-03) | RECORDED: four owner boots of the `a952a09` stage-9 image (boot 1 cold, boots 2–4 warm) without reflashing; the post-batch p69 re-read matched `25385fd7…`. Boots 1/3/4 were field-identical `rc=-7 stop=11` reports (`retry=1/0 att=2 step=9`): attempt 1 is the historical H8 tuple and exhausted the full 100,000-iteration ≈1.14 s window completion-silent, and the approved duplicate CMD3 also timed out (0/3 recovery) with the latched `ERROR=0x0001`/`NORMAL=0x8000` exit. Boot 2 was the clean control (`rc=0 stop=0 step=13`, a clean CMD3 within 26 poll iterations ≈0.29 ms, empty attempt-2 slot). The two-matching rule closed the sticky row with three samples; the cold boot took part. No in-boot retry policy; F3 remains `[U]` |
| eMMC Stage-H10 width-at-HS hardware run (2026-11-03) | RECORDED: fifteen owner boots of the `912e257` (+payload-gate fix `076a548`) stage-10 image (12-boot batch plus an owner-approved 3-boot extension; two cold starts, boots 1 and 6; no reflash); the post-batch p69 re-read matched `38c74815…`. Boot 2 was the only H10 verdict: `rc=-19 stop=12` with the legacy width tuple at HS (`c6=00000900`, `hb=00`→`ha=02`, `sw=0`, `swrc=-19`, `es=000009cc` = the 1-bit-at-HS sum), clean V/X reverts and zero-initialized not-run B/T/Q fields (`log=33` matching the committed failure path and the simulator fingerprint). The other fourteen boots were pre-HS CMD3 completion-silent stops (`PROBE FAILED stage=10 rc=-7`, step 9, including both cold boots) — facts only, re-run not retried. The timing-class model for the BUS_WIDTH refusal is excluded; the verdict carries the single-sample caveat (two-matching rule unmet, 15-boot cap reached). The CMD19-at-4-bit/HS question stays unmeasured; F3 remains `[U]` |
| eMMC Stage-H11 width-value continuation host batch (2026-11-03) | HOST-TESTED ONLY: `gta4l-emmc-probe=11` runs the standard identification once (no CMD3 retry), the stage-A pass at 400 kHz/1-bit, HS entry, the record-only 4-bit replay (W4), the 8-bit BUS_WIDTH attempt (W8, byte 183 = 2, still SDR), one CMD19/CMD14 round trip and the content pass at 8-bit/50 MHz only when W8 is honored, the width and HS reverts and a block-0 recovery read; it always emits `GTA4L-EMMC H11 REPORT` and stops with 0/10/11/12/13/14/15/16/17/18 with no DDR variant, CMD18/CMD25, retry or user-data write. A clean-shape W4 match closes the H10 single-sample caveat and a clean-shape mismatch re-opens H10 as per-boot random; a W8 readback that disagrees with its own re-read sum is the byte-183 paradox row. Probe 116/64 (was 102/56) with fourteen scenarios and eight mutation controls; core 148/90 unchanged; `report-width-h11` is the ninth width materialization; the wrapper/payload gate accept `emmc-probe-11`. No build, flash or boot; F3 remains `[U]` |
| eMMC Stage-H12 record-only DDR attempt host batch (2026-11-03) | HOST-TESTED ONLY: `gta4l-emmc-probe=12` runs the standard identification once (no CMD3 retry), the stage-A pass at 400 kHz/1-bit, HS entry, one record-only CMD6 SET of byte 183 to 4 (the 4-bit DDR code), its CMD13 TRAN wait, the matching 4-bit SDR host DAT width for the verification read, the byte-183 readback and a fresh EXT_CSD re-read with its byte sum, the width and HS reverts and a block-0 recovery read; it always emits `GTA4L-EMMC H12 REPORT` and stops with 0/10/11/12/13/14/15 with no controller DDR mode, no value-5 variant, no CMD19/CMD14, no CMD18/CMD25, no retry and no user-data write. A clean refusal (readback 0 and the 1-bit-at-HS sum) closes the width-study conclusion; a readback of 4 (or a sum that grew by exactly 4) is the honored row stopped before controller DDR work; a disagreeing readback/sum pair is the byte-183 paradox row. Probe 128/69 (was 116/64) with eleven scenarios and five mutation controls; core 148/90 unchanged; `report-width-h12` is the tenth width materialization; the wrapper/payload gate accept `emmc-probe-12`. No build, flash or boot; F3 remains `[U]` |
| eMMC Stage-H13 rung-1 write-phase recovery host batch (2026-11-03) | HOST-TESTED ONLY: `gta4l-emmc-probe=13` runs the standard identification once (no CMD3 retry), the stage-A pass at 400 kHz/1-bit, one 1-bit CMD19 write, one bounded SDHCI data-circuit reset (`SOFTWARE_RESET` data bit with the self-clear poll), the post-reset `PRESENT_STATE`/`DAT_INHIBIT` sample, one CMD14 read with the existing pattern comparison, the block-0 recovery read and the `GTA4L-EMMC H13 REPORT`; it always stops with 0/10/11/12/13/14/15/16/17 and issues no CMD6, CMD18, CMD25 or user-data write and changes no card width or timing. Probe 139/69 (was 128/69) with ten scenarios plus the eleventh width materialization `report-width-h13`; core 160/98 (was 148/90) with twelve scenarios and eight mutation controls; the wrapper/payload gate accept `emmc-probe-13`. No build, flash or boot; F3 remains `[U]` |
| eMMC Stage-H15 verified-width host batch (2026-11-04) | HISTORICAL HOST BATCH: `gta4l-emmc-probe=15` runs the standard identification once, the stage-A content pass at 400 kHz/1-bit, a verified 4-bit SDR switch (CMD6, CMD13 TRAN wait, host width apply/readback, EXT_CSD properties comparison with card+host rollback), one corrected CMD19/CMD14 bus test, the 4-bit content pass, a verified return to 1-bit and the block-0 recovery read; it emits `GTA4L-EMMC H15 REPORT` and stops with 0/11/12/13/14/15/16 (12 is unknown framing after a failed restore). The verified switch never uses the unreadable byte-183 value as proof. Probe 154/77 (was 151/77) with three H15 scenarios; core 173/104 (was 168/104) with five verified-width/bus-test scenarios. H14 is paused; F3 remains `[U]` |
| eMMC Stage-H15 hardware run (2026-11-05) | RECORDED: first cold boot of the `9facb6b` image, identification clean, `rc=-17 stop=15`. The verified 4-bit switch passed (`c6=00000900`, host `00→02`, `vf=1`, read-only EXT_CSD `es=000009cb`), so 4-bit data transfer at 400 kHz is observed. The single CMD19 then timed out (`trc=-11`, `err=0000`, `nrm=0011`, `pst=03f801f6`), the write-phase wedge reproduced at 4-bit. The old revert's pre-switch EXT_CSD read was blocked by the latched `DAT_INHIBIT` (`V rc=-17`, `c6=0`), so no CMD6 was issued and the host stayed at 4-bit; the content pass and recovery read did not run. The old report printed `T att=1` unconditionally and left `Q rc=0` as a default. PAD/DLL unchanged (`qdsd=00009fe4`, `dll=6000642c`, `dst=0`). Evidence: `out/gta4l/ram-root-emmc-probe-h15-evidence/` (`device-run-record.json`, `device-run-transcript.txt`). F3 remains `[U]`; this overturns the byte-183 refusal reading but does not prove a 4-bit sector-read content pass |
| eMMC Stage-H15 reporting/recovery fix and Stage-H16 host batch (2026-11-05) | HOST-TESTED ONLY: H15/H16 emit explicit `att` flags on B/T/V/Q and a T result code; `gta4l_emmc_set_bus_width_recover()` takes a healthy-path EXT_CSD baseline, clears a latched `DAT_INHIBIT` with one bounded local data reset before CMD6, and verifies the restore against the read-only properties. H16 (`gta4l-emmc-probe=16`) is the content-only isolation rung: stage-A baseline, verified 4-bit switch, 4-bit content pass, baseline-based recovery and block-0 recovery read, with no CMD19/CMD14. H15/H16 stop codes are 0/10/11/12/13/14/15/16 (10 baseline read failed, 13 H15-only bus test). Core 176/104 (was 173/104) with the clean recovery, surviving-inhibit and mismatched-properties cases; probe 158/77 (was 154/77) with the reproduced 4-bit wedge, its unproven recovery, H16 pass and H16 divergence. No build, flash or boot; F3 remains `[U]` |
| eMMC Stage-H16 device run (2026-11-05, warm boot) | RECORDED: first attempt of the `20c9f9a` image (`4f1bfb90…`, payload `7404870a…` 7/7, gate 18/18) returned `rc=0 stop=0` with no retry. Verified 4-bit switch on the first CMD6 attempt (`c6=00000900`, host `00→02`, `vf=1`, `es=000009cb`), the full 4-bit content pass matched the 1-bit baseline (`same=1 div=0`, `b0=62084b66 b1=84d0543e data=f418cd33 last=a89b26d5`), the baseline-based revert was verified (`02→00`) and the block-0 recovery read returned `qb0=62084b66`. `B att=0` means no CMD19/CMD14 was attempted, `fail=0` and the PAD samples are unchanged. The `W` line read `BUS_WIDTH` back as 0 where H15 read 1, so byte 183 is not a usable switch indicator in either direction. 4-bit SDR sector reads are measured `[C]` at 400 kHz; F3 stays `[U]` for the missing CMD19/CMD14 round trip. Raw report outside the tree at `out/gta4l/ram-root-emmc-probe-h16-evidence/`. |
| eMMC Stage-H17 Linux-compatible bus-test host batch (2026-11-05) | BUILT AND FLASHED: `gta4l-emmc-probe=17` applies the reference completion rule the recorded wedge calls for — Linux `mmc_bus_test()` discards every `BUS_TEST_W` error and lets `BUS_TEST_R` decide, `sdhci_data_irq()` exempts `MMC_BUS_TEST_R` from the data-CRC path, JEDEC 6.6.4 defines CMD19 as a pattern with no CRC status token, and TI erratum SPRZ436H i836 expects the resulting data timeout and continues to CMD14. The core gains an opt-in `bus_test_tolerate_write_timeout` flag plus `write_result`/`reset_result`; only a CMD19 `DATA_TIMEOUT` is tolerated, the data circuit is cleared with one bounded local reset, and any other write error or a reset that cannot prove the circuit idle still stops. The stage runs the baseline, the verified 4-bit switch, the full 4-bit content pass, one CMD19 pattern write with the tolerated timeout, the data reset, one CMD14 read-back, the verified revert and the block-0 recovery read; it emits `GTA4L-EMMC H17 REPORT` and stops 0/10/11/12/13/14/15/16. The shared H15/H16/H17 `B` line adds `wrc`/`rst`/`ci`; H15 keeps its strict `wrc=-11 trc=-11` record. Core 179/104 (was 176/104) and probe 163/77 (was 158/77). The kernel was rebuilt (`ca983f7d…`), the payload `72f6e2d4…` passes 7/7 and the boot image `13938e49…` passes the 18/18 gate; the first build was refused because the contract checker's diagnostic tuple still ended at stage 16, and that tuple is now derived from the name map so it cannot drift. The image was flashed BOOT only (`3adb7401…` → `60d3d4bc…`, verified prefix, unchanged tail, RECOVERY/EFS/sec_efs unchanged, battery 100%) with no reboot; its device run is the next row. |
| eMMC Stage-H17 device run (2026-11-05, fourth boot) | RECORDED: four boots of the `a198db1` image (`13938e49…`, payload `72f6e2d4…` 7/7, gate 18/18) without reflashing: cold, then three warm. The first three stopped at the known pre-identification CMD3-completion timeout and produced no report; the fourth (warm) returned `rc=0 stop=0`. The verified 4-bit SDR switch passed on the first CMD6 attempt (`c6=00000900`, host `00→02`, `vf=1`, `es=000009cb`), the 4-bit content pass matched the 1-bit baseline (`same=1 div=0`, `b0=62084b66 b1=84d0543e data=f418cd33 last=a89b26d5`), and the CMD19 write produced the expected no-CRC-status timeout (`wrc=-11`, fail record bound to CMD19 with `CC` latched, present `0x03f801f6`). The opt-in rule tolerated only that timeout, one bounded data reset cleared the circuit (`rst=0`), CMD14 returned the expected word (`trc=0`, `tw=te=000000a5`) — the first successful bus-test round trip on this device — and the verified revert (`02→00`), block-0 recovery read (`qb0=62084b66`) and unchanged PAD samples followed. `fail=1` preserves the tolerated timeout; `rc=0 stop=0` is the verdict. Byte 183 read back 1 here, 0 in H16 and 1 in H15, all on verified live 4-bit paths, confirming it is not a usable switch indicator in either direction. **F3 is measured PASS at 4-bit SDR, 400 kHz `[C]`**; the 8-bit width is untried, and cold-boot identification reliability remains open. Raw report and decoded record at `out/gta4l/ram-root-emmc-probe-h17-evidence/`. |
| F5 storage-publication design (2026-11-05) | HOST-ONLY DESIGN, REVIEW-CORRECTED: `docs/gta4l-storage-publication-design.md` surveys the prelinked IOStorageFamily 2.1 personalities (`IOBlockStorageDriver` on `IOBlockStorageDevice` with `device-type=Generic`, `IOGUIDPartitionScheme` on whole `IOMedia`, `IOMediaBSDClient` on `IOMedia`/`IOBSD`), the working `NativeRamBlock`/`NativeVirtioBlock` templates, the prepared public headers at `build/storage-abi/include` (revision `e45fbe30…`) and the existing CMD17 core. The review found the qualification pipeline is not runnable as-is: `refer/IOStorageFamily` is missing at the UniXNU root, the Linux `xcrun` shim returns the literal `macosx`, the audit hardcodes the original candidate, and the original candidate plan, live storage metadata, AES audit, `interface-audit.json` and `bindings.json` are absent; the live observation is candidate-specific (the RAM-root `gdb-stop.json` files are `dc8184da…`, not the gta4l candidate `ca983f7d…`). The gta4l candidate/plan and original kernelcache are present. The corrected design proposes a board-local read-only `IOBlockStorageDevice` matched through `IOResources`/`IOBSD` behind a new `XNU_NATIVE_GTA4L_EMMC_BLOCK` personality, a split core archive, a C request loop plus a thin C++ adapter, `gta4l-emmc-block=1` report mode, 1-bit/400 kHz per-block CMD17, advertised read limits (64 blocks/32768 bytes/1 segment) so the family splits, write rejection, `kIOMessageMediaStateHasChanged` on a read error, and a bounded report of the whole media plus one media-path read; GPT partition discovery is separate and UUIDs/serials are omitted. Qualification must succeed before linking. No code, build, flash or boot; F5 remains `[U]`. |
| Storage ABI qualification (2026-11-05) | HOST-ONLY PASS: `scripts/audit-gta4l-storage-interface.py` drove the shared UniXNU audit (commit `0a1cc71`) against the actual candidate `ca983f7d…` on Linux/QEMU. It staged the existing ESP with `-v serial=3 debug=0x14e keepsym=1 sheng-pmgr=2`, stopped at `_bsd_init` (`0xfffffff0076e7a3c`), and read the live storage metaclass `0xfffffff006add1d0` and `IOService` metaclass `0xfffffff007adb5b8` (`SCTLR_EL1=0x3454593d`, native 4K). The audit reports 190 slots, 176 kernel bindings (162 inherited + 14 pure virtual), vtable `0xfffffff005cb44c0`, live sizes `0x90`/`0x88`, and the original storage executable unchanged; `disk_read_tested=false`, no driver linked. Artifacts: `out/gta4l/storage-abi/gdb-stop.json` (`218e935e…`), `interface-audit.json` (`695c7bbe…`) and `provenance.json`. This clears the ABI gate only; block I/O, GPT publication and hardware remain unqualified and F5 stays `[U]`. `scripts/test-gta4l-storage-abi.py` passes the positive audit, six mutated-snapshot refusals, the wrong-candidate refusal and the `--skip-qemu` run-record checks. |
| Block request helper (2026-11-05) | HOST-ONLY: `package/Gta4lEmmcBlockCore.c/.h` implements the read-only request helper. `scripts/test-gta4l-emmc-block.py` compiles it against `scripts/sim-gta4l-emmc-block/harness.c` and passes 23 scenarios plus 18 mutation controls. It uses explicit READ/WRITE/INVALID directions, returns per-request completed bytes (including a partial final copy), rejects writes without touching the callbacks, caps a request at 64 blocks/32 KiB, loops one 512-byte read per block through a caller-supplied `read_block`, copies each block through a caller-supplied `copy_out`, marks the media offline on the first read or copy failure and preserves the first failure record. No IOKit, transport or hardware dependency; the C++ adapter, personality, bindings and device run are not implemented. |
| eMMC platform extraction and archive split (2026-11-05) | HOST-ONLY: `package/Gta4lEmmcPlatform.c/.h` now owns the SDHCI/GCC/TLMM mapping, width-specific register access, barrier, generic counter and delay; `package/Gta4lEmmcProbe.c` keeps only the ops adapters and the diagnostic report, with no window addresses or `__asm__`. `scripts/build-gta4l-emmc-probe.py` builds `libGta4lEmmcCore.a` (core, platform, block helper) and `libGta4lEmmcProbe.a`, records both hashes and rejects duplicate global symbols with `nm`; `configs/gta4l_defconfig` force-loads both. The probe simulator injects its hooks into a rewritten platform copy and still passes 163/77, the core passes 179/104, the block helper 23/18, and the storage-ABI and board checks pass. A synthetic host record exercised the split build end-to-end (three core objects, one probe object, no duplicate globals); the real device archives were not built here because the `bsd_init.o` record and macOS `xcrun` are absent. No flash or boot; F5 remains `[U]`. |
| eMMC Stage-H14 rung-2 forced post-write drain host batch (2026-11-04, paused by the protocol correction) | HISTORICAL: `gta4l-emmc-probe=14` keeps the stage-13 form (standard identification once, the stage-A pass at 400 kHz/1-bit, one 1-bit CMD19 write, one bounded SDHCI data-circuit reset with the self-clear poll, the post-reset `PRESENT_STATE`/`DAT_INHIBIT` sample) and inserts one forced CMD12 `STOP_TRANSMISSION` (R1b) plus one CMD13 `SEND_STATUS` (R1) between the reset and the record-only CMD14/Q reads, then emits the `GTA4L-EMMC H14 REPORT`; it always stops with 0/10/11/12/13/14/15/16/17 and issues no CMD6, CMD18, CMD25 or user-data write and changes no card width or timing. The drain is local to `gta4l_emmc_post_write_drain()`; the CMD12 entry wait applies the SDHCI stop-command `DAT_INHIBIT` exemption and `TIMEOUT_CONTROL` is untouched. Probe 151/77 (was 139/69) with eleven scenarios plus the twelfth width materialization `report-width-h14`; core 168/104 (was 160/98) with eight scenarios and six mutation controls; the wrapper/payload gate accept `emmc-probe-14`. No build, flash or boot; F3 remains `[U]` |
| `scripts/test-gta4l-emmc-evidence-counts.py` | PASS: documented counts match sources |
| `scripts/test-gta4l-sdhci-evidence-counts.py` | PASS: 33 scenarios, 10 mutations, 41 synthetic controls |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| `porting.toml` parse with `tomllib` | 13 areas |
| `git diff --check` | PASS: clean |

#### PixelExperience provenance validation (2026-11-01)

The following host results were produced after the PixelExperience storage-stack
provenance review and its closeout correction, run from `src/UniXNU-gta4l/` with
`LC_ALL=C` and `/usr/bin/python3.11`. The provenance document has no manifest
row; the table stays at 45 rows and its self-check passes after the `README.md`,
`docs/emmc-reference.md` and `porting.toml` hash refresh.

| Check | Result |
| --- | --- |
| `scripts/test-gta4l-emmc.py` | PASS: 98 simulator scenarios and 55 mutation controls |
| `scripts/test-gta4l-emmc-probe.py` | PASS: 31 scenarios, 21 mutation controls, 3 width-bound materializations and the oversized-report rejection control |
| `scripts/test-gta4l-emmc-evidence-counts.py` | PASS: documented counts match sources |
| `scripts/test-gta4l-sdhci-evidence-counts.py` | PASS: 33 scenarios, 10 mutations, 41 synthetic controls |
| `scripts/test-gta4l-ram-root-fixture-recipe.py` | PASS: input and failure checks |
| `scripts/check-gta4l.py` | PASS: SM6115 (gta4l) board layout verified |
| Provenance reproduction recipe | PASS: boot header sizes within 100,663,296 bytes; 12 FDTs / 3,950,482 bytes; 40 DTBO entries / 4,474,460-byte table; 41 properties, 0 mismatches |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| `porting.toml` parse with `tomllib` | 13 areas |
| `git diff --check` | PASS: clean |

#### fmt=4 fail-closed hardware-run record (2026-11-01)

The `fmt=4` fail-closed image was built from `669abc9` with
`Gta4lEmmc.c e493b3eb…`, `Gta4lEmmc.h e0a2830c…` and
`Gta4lEmmcProbe.c 932552a2…`; kernel `27cb1b54…`, archive `e9578564…`,
payload `954cc0dc…` and boot image `4d2f7975…` (85,688,320 bytes). The payload
contract is 7/7 and the boot-image gate is 18/18. The owner reports two runs of
the same image on two different units: an SM-T505 first, then the owner's
SM-T505N. Both panics record `stage=3 rc=4294967288` with
`fail=1/2/0/00000209/0002/8000/03f800f0/8`,
`rsp=00000000,00000000,80000000,00c0ff80` and `step=8`, identical to the
current-source CMD2 CRC record, and the owner reports the results are identical
1:1. The first run has no flash record or run ID in this tree; its unit
attribution is owner-reported. The later BOOT-only run on the SM-T505N has a
flash record, readback hashes and photo in
`out/gta4l/ram-root-emmc-probe-failclosed-rerun-evidence/`. The runs failed at
CMD2 before CMD3/CMD9/CMD7; CMD1 OCR `0xC0FF8080` was observed, no validated
CID, CSD, EXT_CSD, capacity or sector result was obtained and no sector read was
issued. The panic is a screen photo, not a serial console capture.

The following host checks were re-run after the documentation edits, from
`src/UniXNU-gta4l/` with `LC_ALL=C` and `/usr/bin/python3.11`:

| Check | Result |
| --- | --- |
| `scripts/test-gta4l-emmc.py` | PASS: 98 simulator scenarios and 55 mutation controls |
| `scripts/test-gta4l-emmc-probe.py` | PASS: 31 scenarios, 21 mutation controls, 3 width-bound materializations and the oversized-report rejection control |
| `scripts/test-gta4l-emmc-evidence-counts.py` | PASS: documented counts match sources |
| `scripts/test-gta4l-sdhci-evidence-counts.py` | PASS: 33 scenarios, 10 mutations, 41 synthetic controls |
| `scripts/test-gta4l-ram-root-fixture-recipe.py` | PASS: input and failure checks |
| `scripts/check-gta4l.py` | PASS: SM6115 (gta4l) board layout verified |
| eMMC `fmt=4` rerun BOOT write and readback | PASS: BOOT-only write; image-byte readback `4d2f7975…`, full partition `b53f31fe…`, tail unchanged, `RECOVERY` `14e11370…` unchanged |
| eMMC `fmt=4` pulled-prefix gate rerun | PASS: 18/18 boot-image checks on the pulled 85,688,320-byte prefix; 7/7 payload checks on the 83,886,080-byte extraction |
| eMMC `fmt=4` rerun photo | RECORDED: `stage3-fmt4-rerun-photo-2026-11-01.jpg`, 2560x1920, SHA-256 `c0799525…` |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| `porting.toml` parse with `tomllib` | 13 areas |
| `git diff --check` | PASS: clean |

The host checks are read-only and do not build, flash, boot, power or access a
device `[S]`. The two device rows record the BOOT-only write and photo described
above; the pulled-prefix row verifies the pulled artifact on the host and is not
a new device result.

#### cmd1gap candidate hardware-run record (2026-11-01)

The `cmd1-crc-retire-8us` candidate is the `fmt=4` fail-closed source plus one
8-us delay after an accepted ready CMD1. It was built from
`2a3268c7fa68c384470bb0ee87270ac594ec3fec` (clean) with
`Gta4lEmmc.c 874e6878…`, `Gta4lEmmc.h 5dd223f7…` and
`Gta4lEmmcProbe.c 932552a2…`; archive `22e6186f…`, payload kernel `ee33719e…`,
payload `6b6f5212…` (83,886,080 bytes, `gta4l-emmc-probe=3`, contract 7/7) and
boot image `8a463dae…` (85,688,320 bytes, gate 18/18 against the v5 baseline
`ce4d76e8…`). The build provenance is
`out/gta4l/ram-root-emmc-probe-cmd1gap-evidence/build-provenance.json`.

The BOOT-only flash record is
`out/gta4l/ram-root-emmc-probe-cmd1gap-evidence/flash-record.json`: pre-flash
partition `b53f31fe…` (the `fmt=4` image plus its unchanged tail), post-flash
`92f4000e…`, image-byte readback `8a463dae…`, tail `a28b4ea2…` unchanged,
`RECOVERY` `14e11370…` unchanged, pulled gate 18/18 and pulled payload contract
7/7. The record was written after the write, records no reboot or boot at that
time, and names the `fmt=4` failure as the matched baseline on that unit.

The owner then ran the candidate on three units: the owner's SM-T505N and two
owner-reported SM-T505s. All three reached `GTA4L-EMMC IDENT REPORT: rc=0` and
stopped at the intentional report panic. Common values: `mci=10000072 rca=1
cap=61071360 rev=8 bw=0 ocr=c0ff8080 hc2=0008 ven=00018a9c dll=6000642c
dst=00000000 pst=00000000 step=15 log=5`; CMD2 normal `0x0001`; Samsung CID
with product name `DX68M`; identical CSD-shaped window; `c3l`/`c7q` valid with
38 iterations; CMD7 CC and TC observed with no error (`0x071B`, argument
`0x00010000`). `step=15` is `GTA4L_EMMC_STEP_READY`, reached only after the
EXT_CSD read and the sector-size, capacity and partition-access checks pass.
Per-unit differences are limited to CID serial/date words, the plain EXT_CSD
byte sum (`0x9CB`/`0x9C7`), a few poll ticks and XNU VM counters. The full
transcription, field comparison and decode are in
`docs/emmc-cmd2-crc-audit.md` §2.11 and `docs/emmc-driver.md` §5.3.4.

The three run results are owner-supplied text transcriptions; no photo file or
photo hash from this run is in the tree, so they are not independently
authenticated, and the unit/model attribution is owner-reported. On the owner's
unit the `fmt=4` image had failed CMD2 and the candidate completed
identification; the delay is the only source delta, which supports a
timing-sensitive contribution for these runs. It does not establish the CMD2
cause and does not prove the delay necessary: the earlier `cmd2window` and
`fmt=2` builds passed CMD2 without it. No general fix is adopted, no sector
read was issued, and CRC checking remains enabled.

| Check | Result |
| --- | --- |
| eMMC `cmd1gap` BOOT write and readback | PASS: BOOT-only write; image-byte readback `8a463dae…`, full partition `92f4000e…`, tail `a28b4ea2…` unchanged, `RECOVERY` `14e11370…` unchanged |
| eMMC `cmd1gap` pulled-image gate | PASS: 18/18 on the pulled 85,688,320-byte image; 7/7 on the 83,886,080-byte payload extraction |
| eMMC `cmd1gap` three-unit run | RECORDED: three owner-supplied `IDENT REPORT rc=0` text transcriptions; no photo file or hash |
| `scripts/test-gta4l-emmc.py` | PASS: 98 simulator scenarios and 55 mutation controls |
| `scripts/test-gta4l-emmc-probe.py` | PASS: 31 scenarios, 21 mutation controls, 3 width-bound materializations and the oversized-report rejection control |
| `scripts/test-gta4l-emmc-evidence-counts.py` | PASS: documented counts match sources |
| `scripts/test-gta4l-sdhci-evidence-counts.py` | PASS: 33 scenarios, 10 mutations, 41 synthetic controls |
| `scripts/test-gta4l-ram-root-fixture-recipe.py` | PASS: input and failure checks |
| `scripts/check-gta4l.py` | PASS: SM6115 (gta4l) board layout verified |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| `porting.toml` parse with `tomllib` | 13 areas |
| `git diff --check` | PASS: clean |

#### Stage-1 full-probe hardware-run record (2026-11-02)

The stage-1 image is the full `emmc-probe-1` diagnostic (`gta4l-emmc-probe=1`)
built from `417acc4d89ab72491cf769bb8ed1131c79e1bb15` (clean) with
`Gta4lEmmc.c 874e6878…`, `Gta4lEmmc.h 5dd223f7…` and `Gta4lEmmcProbe.c
932552a2…`; archive `e2a013e2…`, payload kernel `194c909f…`, payload
`42e7a518…` (83,886,080 bytes, contract 7/7) and boot image `bf73a39b…`
(85,688,320 bytes, gate 18/18 against the v5 baseline `ce4d76e8…`). The build
provenance is
`out/gta4l/ram-root-emmc-probe-stage1-evidence/build-provenance.json`.

The BOOT-only flash record for the owner's SM-T505N is
`out/gta4l/ram-root-emmc-probe-stage1-evidence/flash-record.json`: pre-flash
partition `92f4000e…` (the `cmd1gap` image plus its unchanged tail), post-flash
`421e8848…`, image-byte readback `bf73a39b…`, tail `a28b4ea2…` unchanged,
`RECOVERY` `14e11370…` unchanged, pulled gate 18/18 and pulled payload contract
7/7. The record was written before the boot. The same image was shared for a
second unit; that unit has no flash record or readback in this tree.

The owner booted the image on the SM-T505N and on one SM-T505 and supplied
screen photographs. Both reports reached `GTA4L-EMMC PROBE REPORT: rc=0` with
`mci=10000072 rca=1 cap=61071360 rev=8 bw=0 ocr=c0ff8080 hc2=0008
ven=00018a9c dll=6000642c dst=00000000 pst=00000000 step=15 log=7`. The
recovery-known CSD matches on both units, the owner's CID matches the
recovery-known CID, and the SM-T505 CID matches the first SM-T505 column in
`docs/emmc-cmd2-crc-audit.md` §2.11. Block 0 reports
`sum=62084b66 words=00000000 00000000 000000ee 00000001` on both units (a
protective-MBR type `0xEE` entry with start LBA 1); block 1 reports
`words=20494645 54524150 00010000 0000005c` (`EFI PART`, revision 1.0, 92-byte
GPT header) with sums `84d0543e` and `425cd1ab`. The sum is the probe's
`sum*31 + byte` content fingerprint, not a CRC; the identical block 0 and
different block 1 fingerprints are consistent with a shared protective MBR and
a per-device GPT header (disk GUID/header CRC). The selected words do not
include the `0x55AA` MBR signature or the GPT header CRC; the owner-authorized
recovery-side readback records both directly.

The two reports are owner-supplied photographs; no photo file or hash from
either run is in the tree. The SM-T505 flash procedure and unit attribution are
owner-reported and its image identity rests on the shared `bf73a39b…` copy. The
run is read-only (`CMD0`/`CMD1`/`CMD2`/`CMD3`/`CMD9`/`CMD7`/`CMD13`/`CMD8`/
`CMD17`, CRC enabled, no writes, DMA, CQE, ICE, bus-width change, high-speed
mode, mount or publication). It establishes sector-read and MBR/GPT content
evidence for blocks 0 and 1, not a filesystem or multi-block reliability; an
owner-authorized read-only recovery-side readback independently reproduces the
MBR signature and the GPT header CRC (images and sector hashes stay in the
gitignored `out/gta4l/ram-root-emmc-probe-stage1-evidence/recovery-readback/`
record). The full transcription and field comparison are in
`docs/emmc-cmd2-crc-audit.md` §2.12 and `docs/emmc-driver.md` §5.3.5.

| Check | Result |
| --- | --- |
| eMMC stage-1 BOOT write and readback | PASS: BOOT-only write; image-byte readback `bf73a39b…`, full partition `421e8848…`, tail `a28b4ea2…` unchanged, `RECOVERY` `14e11370…` unchanged |
| eMMC stage-1 pulled-image gate | PASS: 18/18 on the pulled 85,688,320-byte image; 7/7 on the 83,886,080-byte payload extraction |
| eMMC stage-1 two-unit run | RECORDED: two owner-supplied `PROBE REPORT rc=0` photographs; no photo file or hash |
| `scripts/test-gta4l-emmc.py` | PASS: 98 simulator scenarios and 55 mutation controls |
| `scripts/test-gta4l-emmc-probe.py` | PASS: 33 scenarios and 22 mutation controls |
| `scripts/test-gta4l-emmc-evidence-counts.py` | PASS: documented counts match sources |
| `scripts/test-gta4l-sdhci-evidence-counts.py` | PASS: 33 scenarios, 10 mutations, 41 synthetic controls |
| `scripts/test-gta4l-ram-root-fixture-recipe.py` | PASS: input and failure checks |
| `scripts/check-gta4l.py` | PASS: SM6115 (gta4l) board layout verified |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| `porting.toml` parse with `tomllib` | 13 areas |
| `git diff --check` | PASS: clean |

#### Stage-A extended content-read hardware-run record (2026-11-02)

The stage-A image is the full `emmc-probe-1` diagnostic rebuilt from
`53753df` with the extended read phase; the tree was dirty with
`package/Gta4lEmmc.h 7e063c08…`, `package/Gta4lEmmcProbe.c 034571f1…`,
`scripts/sim-gta4l-emmc-probe/harness.c 1e8ccdc2…`,
`scripts/sim-gta4l-emmc-probe/report-width-fixture.c 522a6b5b…`,
`scripts/test-gta4l-emmc-probe.py 68a91eb0…` and
`scripts/build-gta4l-emmc-probe.py 06d63380…`; `Gta4lEmmc.c` stayed
`874e6878…`. The build provenance is
`out/gta4l/ram-root-emmc-probe-stage2-evidence/build-provenance.json`: probe
archive `57ed9995…`, payload `fdd85d25…` (83,886,080 bytes,
`gta4l-emmc-probe=1`, contract 7/7) and boot image `721e5c87…` (85,688,320
bytes, gate 18/18 against the v5 baseline `ce4d76e8…`).

The BOOT-only flash record for the owner's SM-T505N is
`out/gta4l/ram-root-emmc-probe-stage2-evidence/flash-record.json`: pre-flash
partition `421e8848…` (the stage-1 image plus its unchanged tail), post-flash
`d55c09bf…`, image-byte readback `721e5c87…`, tail `a28b4ea2…` unchanged,
`RECOVERY` `14e11370…` unchanged, and the EFS/sec_efs backups unchanged; no
other partition was written. The record was written before the boot.

The owner booted the image on the same SM-T505N and supplied a screen
photograph, `probe-report-photo.jpg` (SHA-256 `663d2db3…`), with the
transcription in `probe-report.txt`; both stay in the evidence directory. The
report reached `GTA4L-EMMC PROBE REPORT: rc=0` with `step=18 log=30`, the new
content lines and the same identification fields as the stage-1 record above.
The GPT gate reported `elba=2 n=84 sz=128 first=34 last=61071326
backup=61071359`, exactly the live and pinned header values. 84 × 128 =
10,752 bytes = 21 sectors, so the primary array is LBAs 2..22;
`BLOCKS 22=00000000` is expected because only entries 1..80 are populated and
the last four 128-byte entry slots are zero. `DATA 34=f418cd33` is the first
usable sector and `LSTLBA=61071359 sum=a89b26d5` is the backup GPT header at
the last sector.

The same session then read LBAs 0..33, LBA 34 and LBAs 61,071,326..61,071,359
from `/dev/block/mmcblk0` in TWRP with `dd bs=512`, hashing each range on the
device and capturing it with `adb exec-out`; no write, mount, format or
partition change was issued. All 23 stage-A report sums (21 array + `DATA 34` +
`LSTLBA`) reproduce from those bytes. The live primary sectors `0c7e9c03…` are
byte-identical to the pinned recovery GPT dump, the live backup tail
`41912e59…` is byte-identical to the pinned backup dump, both header CRCs are
valid (`41c874ee`/`fde724e6`), the backup array equals the primary array, and
the LBA 0/1 hashes match the stage-1 readback. Sector images and hashes stay in
the gitignored
`out/gta4l/ram-root-emmc-probe-stage2-evidence/recovery-readback/` record. The
full transcription and decoding are in `docs/emmc-cmd2-crc-audit.md` §2.13 and
`docs/emmc-driver.md` §5.3.6.

| Check | Result |
| --- | --- |
| eMMC stage-A BOOT write and readback | PASS: BOOT-only write; image-byte readback `721e5c87…`, full partition `d55c09bf…`, tail `a28b4ea2…` unchanged, `RECOVERY` `14e11370…` unchanged |
| eMMC stage-A pulled-image gate | PASS: 18/18 on the pulled 85,688,320-byte image; 7/7 on the 83,886,080-byte payload extraction |
| eMMC stage-A on-device report | RECORDED: owner-supplied photograph `probe-report-photo.jpg` (SHA-256 `663d2db3…`) with `rc=0`, `step=18`, `log=30`, the GPT gate, 21 array sums, `DATA 34` and `LSTLBA` |
| eMMC stage-A recovery readback | PASS: same-session read-only `dd` of LBAs 0..33, 34 and 61,071,326..61,071,359; all 23 stage-A sums match; live primary bytes equal the pinned recovery GPT dump, live backup tail equals the pinned backup dump, both header CRCs valid, backup array equals primary array |
| `scripts/test-gta4l-emmc.py` | PASS: 98 simulator scenarios and 55 mutation controls |
| `scripts/test-gta4l-emmc-probe.py` | PASS: 38 scenarios and 26 mutation controls, including the stage-A GPT gate, 21-sector array content phase and four loop mutation controls |
| `scripts/test-gta4l-emmc-evidence-counts.py` | PASS: documented counts match sources |
| `scripts/test-gta4l-sdhci-evidence-counts.py` | PASS: 33 scenarios, 10 mutations, 41 synthetic controls |
| `scripts/test-gta4l-ram-root-fixture-recipe.py` | PASS: input and failure checks |
| `scripts/check-gta4l.py` | PASS: SM6115 (gta4l) board layout verified |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| `porting.toml` parse with `tomllib` | 13 areas |
| `git diff --check` | PASS: clean |

#### A2/F2 multi-block attempt-2 hardware-run record (2026-11-02)

The attempt-2 image is the corrected optional multi-block build
(`gta4l-emmc-probe=1` plus `gta4l-emmc-mb=1`) from clean `209766c4` with
`Gta4lEmmc.c 942117eb…`, `Gta4lEmmc.h dce8a428…` and `Gta4lEmmcProbe.c
e67ad2bc…`; probe archive `7c08c6fb…`, kernel `5132f235…`, payload `a5d9e63f…`
(83,886,080 bytes, contract 7/7) and boot image `115e5c63…` (85,688,320
bytes, gate 18/18 against the v5 baseline `ce4d76e8…`). The build provenance
is `out/gta4l/ram-root-emmc-probe-stage2b-attempt2-evidence/build-provenance.json`.

The BOOT-only flash record for the owner's SM-T505N is
`out/gta4l/ram-root-emmc-probe-stage2b-attempt2-evidence/flash-record.json`:
pre-flash partition `64fa7190…` (the attempt-1 image `cac4356b…` plus its
unchanged tail `a28b4ea2…`), post-flash `4ca40951…`, image-byte readback
`115e5c63…`, tail `a28b4ea2…` unchanged, `RECOVERY` `14e11370…` unchanged,
EFS `51fa824c…` and sec_efs `3a265d58…` unchanged; no other partition was
written. A re-read on the still-connected device after the owner's boot
returned the same partition and prefix hashes, so the panic belongs to this
image.

The owner booted the image and supplied a screen OCR text; the attached photo
file and hash are not in this tree. The transcription is
`out/gta4l/ram-root-emmc-probe-stage2b-attempt2-evidence/stage2b-attempt2-failure.txt`.
The report entered the MD phase (`step=19`, `STEP_PROBE_MB`) and failed:

```text
GTA4L-EMMC PROBE FAILED: mb read lba=2 n=21 rc=4294967285 step=19
mb=20 stop=4294967289/1/1/0000/0000/03f80af6/00000c1b/00000000
fail=1/18/2/0000123a/0000/0021/03f80af6/19
```

`rc=-11` (`GTA4L_EMMC_ERR_DATA_TIMEOUT`). The CMD18 record (index 18,
argument 2, command `0x123a`, `normal=0x0021` = `CC | BRR`, `error=0`,
present `0x03f80af6`, step 19) shows the command was accepted, the buffer-read
path was active, no error was latched and `TC` was never observed. `mb=20` is
the failing/completed block ordinal (0-based; `Gta4lEmmc.h:438`), not a block
count: the helper sets it to `done` while blocks are pending and to
`count - 1` when all blocks were drained but `TC` was missing. The ordinal
shows at least 20 of the 21 requested blocks were drained before the
1,000,000 µs / 10 µs data-completion poll expired, and cannot distinguish
"20 drained with the ordinal-20 block pending" from "all 21 drained with only
`TC` missing"; the terminal sample still showed `CC | BRR`.
The CMD12 stop record
`-7/1/1/0000/0000/03f80af6/00000c1b/00000000` is CMD12 `R1b` with stop-valid
and terminal-capture-valid 1, `normal=0`, `error=0`, and no `CC` or `TC`
observed within its 1,000,000 µs bound; the capture flags record the terminal
state, not completion. The read helper keeps the first failing result, so the
report's `rc` remains the CMD18 data timeout.

This is the first hardware evidence of a multi-block data phase beyond one
block: at least 20 consecutive blocks were consumed under one open-ended
CMD18, consistent with the `TM_MULTI` correction. It does not establish F2:
the report does not establish 21/21 completion or `TC`, no `MBLOCKS` sums or
content comparison exist, and the stop did not complete. The record does not distinguish a marginal data bound from a
stalled block 21.

| Check | Result |
| --- | --- |
| eMMC A2/F2 attempt-2 BOOT write and readback | PASS: BOOT-only write; image-byte readback `115e5c63…`, full partition `4ca40951…`, tail `a28b4ea2…` unchanged, `RECOVERY` `14e11370…` unchanged, EFS/sec_efs unchanged |
| eMMC A2/F2 attempt-2 pulled-image gate | PASS: 18/18 on the pulled 85,688,320-byte image; 7/7 on the 83,886,080-byte payload extraction |
| eMMC A2/F2 attempt-2 on-device report | RECORDED: owner-supplied OCR text; `step=19`, `mb=20`, CMD18 `rc=-11`, stop `-7`; no established 21/21 completion, `TC`, MB sums or content comparison |
| `scripts/test-gta4l-emmc.py` | PASS: 112 simulator scenarios and 62 mutation controls |
| `scripts/test-gta4l-emmc-probe.py` | PASS: 44 scenarios, 34 mutation controls and 1 oversized-report control |
| `scripts/test-gta4l-emmc-evidence-counts.py` | PASS: documented counts match sources |
| `scripts/test-gta4l-sdhci-evidence-counts.py` | PASS: 33 scenarios, 10 mutations, 41 synthetic controls |
| `scripts/test-gta4l-ram-root-fixture-recipe.py` | PASS: input and failure checks |
| `scripts/check-gta4l.py` | PASS: SM6115 (gta4l) board layout verified |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| `porting.toml` parse with `tomllib` | 13 areas |
| `git diff --check` | PASS: clean |

#### A2/F2 multi-block attempt-3 hardware-run record (2026-11-02)

The attempt-3 image is the bounded post-bound drain build from `9ed12bd`
with `Gta4lEmmc.c 367978fd…`, `Gta4lEmmc.h d6be3022…`, `Gta4lEmmcProbe.c
c344b35e…`, kernel `f034dda2…`, payload `0048962c…` (83,886,080 bytes,
contract 7/7) and boot image `f85c18d9…` (85,688,320 bytes, gate 18/18 against
the v5 baseline `ce4d76e8…`). The build provenance and flash record are under
`out/gta4l/ram-root-emmc-probe-stage2b-attempt3-evidence/`.

The BOOT-only write was verified: pre-flash partition `4ca40951…` (the
attempt-2 image `115e5c63…` plus the unchanged tail `a28b4ea2…`), post-flash
partition `d652a8b5…`, image-byte readback `f85c18d9…`, tail `a28b4ea2…`
unchanged, `RECOVERY` `14e11370…` unchanged, EFS `51fa824c…` and sec_efs
`3a265d58…` unchanged. The pulled boot-image gate is 18/18 and the pulled
payload contract is 7/7.

The owner's first boot of the image stopped during identification, before the
MD phase, with the attempt-1 H8 CMD3-completion signature:

| Field | Report value | Decode |
| --- | --- | --- |
| `stage` | `1` | full-probe stage |
| `rc` | `4294967289` | `-7`, `GTA4L_EMMC_ERR_CMD_TIMEOUT` |
| `fail` | `1/3/65536/0000031a/0000/0000/03f800f0/9` | valid 1, index 3 (CMD3), argument `0x00010000`, command `0x031a`, error 0, normal 0, present `0x03f800f0`, step 9 |
| `c3p` | `1/0000/0000/03f800f0/0023/07ff` | valid pre-CMD3 sample, no error |
| `c3t` | `1/3/031a/00010000/1/00000500,<CID words>` | valid, stable CMD3 timeout capture |
| `c3l` | `1/3/0000/0000/100000/0000000014c343d/19200000` | 100,000 iterations, `0x14c343d` ticks at 19.2 MHz (about 1.134 s), first/last normal 0 |
| `step`/`log` | `9`/`5/8/3/0` | `STEP_CMD2` last completed |
| `c7p`–`c7x` | all invalid/zero | CMD7 never attempted |
| `drain` | absent | MD phase never reached |

`step=9` is `STEP_CMD2`, so CMD2 completed and CMD3 did not. `fail` names CMD3
with no error/normal bits; `c3p` is a valid pre-CMD3 sample with no error;
`c3t` is valid and stable and holds a plausible CMD3 R1 status in `RESPONSE0`
with the CID words in `RESPONSE1..3`; `c3l` exhausted its 100,000-iteration,
1,000,000 µs / 10 µs poll with neither `CC` nor an error. The `c7` block is
invalid/zero, so no CMD7, CMD18, CMD12 or sector read occurred. The raw CID
and response words are in the untracked transcript and are not repeated here.

This is the documented H8 CMD3-completion signature, field-for-field the same
as the attempt-1 stage-1 failure except for the panic frame/page/free context
and the `c3l` tick count (a fresh run). The attempt-3 drain is entered only in
`gta4l_emmc_read_multi()` after a CMD18 primary-bound expiry, so it was not
exercised on this boot; the report neither supports nor refutes it. The
previous attempt-2 boot did reach the MD phase, so this is a recurrence of the
open CMD3/H8 behavior rather than an MD/drain result. F2 remains unestablished
and no on-device `drain=` value existed on this boot; a later boot of the same
image did exercise the drain (next section). The transcription is
`out/gta4l/ram-root-emmc-probe-stage2b-attempt3-evidence/stage2b-attempt3-failure.txt`;
the owner photo file and hash are not in this tree. The attempt-3 image was
not re-read after this boot, so the run is bound by the verified flash/readback
record, not by a post-boot partition re-read. The suite counts in the table
below are the attempt-3 batch snapshot, not the current counts; the current
suite counts are in the §2 table.

| Check | Result |
| --- | --- |
| eMMC A2/F2 attempt-3 BOOT write and readback | PASS: BOOT-only write; image-byte readback `f85c18d9…`, full partition `d652a8b5…`, tail `a28b4ea2…` unchanged, `RECOVERY` `14e11370…` unchanged, EFS/sec_efs unchanged |
| eMMC A2/F2 attempt-3 pulled-image gate | PASS: 18/18 on the pulled 85,688,320-byte image; 7/7 on the 83,886,080-byte payload extraction |
| eMMC A2/F2 attempt-3 on-device report | RECORDED: owner-supplied OCR text; `step=9`, CMD3 `rc=-7`, no CMD7/CMD18/CMD12/sector read; the drain field is absent |
| `scripts/test-gta4l-emmc.py` | PASS: 116 simulator scenarios and 67 mutation controls |
| `scripts/test-gta4l-emmc-probe.py` | PASS: 48 scenarios, 34 mutation controls, 4 width-bound materializations and 1 oversized-report control |
| `scripts/test-gta4l-emmc-evidence-counts.py` | PASS: documented counts match sources |
| `scripts/test-gta4l-sdhci-evidence-counts.py` | PASS: 33 scenarios, 10 mutations, 41 synthetic controls |
| `scripts/test-gta4l-ram-root-fixture-recipe.py` | PASS: input and failure checks |
| `scripts/check-gta4l.py` | PASS: SM6115 (gta4l) board layout verified |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| `porting.toml` parse with `tomllib` | 13 areas |
| `git diff --check` | PASS: clean |

#### A2/F2 multi-block attempt-3 later boots (2026-11-02)

Two more owner boots of the same verified `f85c18d9…` image were recorded. The
image was not reflashed and the BOOT partition was not re-read after the first
boot, so both records are bound by the attempt-3 flash/readback record and the
owner's statement that they are boots of this image. Both are owner-supplied
OCR text only; no photo file or hash is in this tree.

**Boot 2 (drain exercised).** The stage-A pass and its GPT gate completed and
the optional `CMD18` ran; the primary 1,000,000 µs bound expired at block
ordinal 20 and the bounded drain ran to exhaustion:

```text
GTA4L-EMMC PROBE FAILED: mb read lba=2 n=21 rc=4294967285 step=19
mb=20 drain=1/0/0/50000 stop=4294967289/1/1/0000/0000/03f80af6/00000c1b/00000000 fail=1/18/2/0000123a/0000/0021/03f80af6/19
```

`drain=1/0/0/50000` decodes as used 1, resolved 0, 0 additional blocks and the
full 50,000 iterations of the 500,000 µs / 10 µs window. The post-drain sample
still shows `CC | BRR` and no `TC`; the stop captured a valid terminal state
with no `CC`/`TC` and returned `-7`, field-for-field identical to attempt 2's
stop record. The CMD18 failure record is valid index 18, argument 2, error 0,
normal `0x0021`, present `0x03f80af6`, step 19. No MB sums were printed and
the `mb=20` ambiguity (20-of-21 versus 21-of-21 drained) remains; if all 21
blocks were already buffered (`done == count`), the zero drain-block count
means there was nothing further to read, not that a late block was missed. The
drain does not resolve the stall; the record does not support a marginally
longer data bound as the explanation, though it does not formally exclude a
block arriving just after the window. Transcription:
`out/gta4l/ram-root-emmc-probe-stage2b-attempt3-evidence/stage2b-attempt3-boot2-drain-failure.txt`.

**Boot 3 (array read).** The stage-A extended content read failed at its first
sector (LBA 2), after the GPT gate, so the probe stopped before the
data/last-sector reads and the MD phase:

```text
GTA4L-EMMC PROBE FAILED: array read lba=2 rc=4294967285 step=16
fail=1/17/2/0000113a/0000/0002/03f800f0/16
```

`rc=-11`, `step=16` is `STEP_PROBE_ARRAY`, and `fail` names CMD17 (index 17,
argument 2) with error 0 and normal `0x0002` (`TC` only; no `CC` or `BRR`); the
helper's 1,000,000 µs bound expired with no buffer-ready condition. This is a
new single-block data-phase signature; the same image completed the stage-A
pass on boot 2, so it is intermittent across boots. Whether the reboot between
boots power-cycled the card (the previous boot ended with a failed CMD18 and a
timed-out CMD12) is not recorded. Transcription:
`out/gta4l/ram-root-emmc-probe-stage2b-attempt3-evidence/stage2b-attempt3-boot3-array-failure.txt`.

H8 status: boot 1 failed identification at CMD3, while boots 2 and 3 completed
identification, so H8 is intermittent rather than a deterministic property of
this build. F2 remains unestablished.

| Check | Result |
| --- | --- |
| eMMC A2/F2 attempt-3 boot 2 (drain) | RECORDED: `step=19`, `mb=20`, `drain=1/0/0/50000`, `rc=-11`, stop `-7`; drain entered and exhausted, no `TC`, no MB sums |
| eMMC A2/F2 attempt-3 boot 3 (array) | RECORDED: `step=16`, CMD17 LBA 2 `rc=-11`, normal `0x0002` (TC only); new intermittent single-block data timeout |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| `git diff --check` | PASS: clean |

#### Stage-B1 hardware-run record (2026-11-02)

The verified B1 image was built from `09b8f30` plus the `emmc-probe-4` payload
mapping in `5cf8565`: kernel `f25d3cee…`, payload `cf08c1b2…` (contract 7/7)
and boot image `327fe8c9…` (gate 18/18 against `ce4d76e8…`). The BOOT-only
write had pre-flash p69 `d652a8b5…`, post-flash p69 `1c64333a…`, image prefix
`327fe8c9…`, unchanged tail `a28b4ea2…`, and unchanged `RECOVERY`
`14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…`; the pulled gates are
18/18 and 7/7. The owner booted the same image three times without reflashing.
The BOOT partition was not re-read after the boots, so the reports are bound by
the flash/readback record and the owner's statement. The flash record's
`status` field predates the boots; the boot results are in
`out/gta4l/ram-root-emmc-probe-b1-evidence/hardware-run-record.json` and the
three transcripts.

| Attempt | Report | Result |
| --- | --- | --- |
| 1 | generic probe failure | `stage=4`, `rc=-7`, `step=9`; H8 CMD3-completion timeout before any B1 command |
| 2 | `GTA4L-EMMC B1 REPORT` | `rc=-19`, `stop=1`; stage-A pass completed (`step=18`, `log=30`, `b0=62084b66`, `b1=84d0543e`), then the 4-bit CMD6 post-switch byte-183 readback was 0 (`sw=0`) and the host width was reverted; no CMD19/CMD14 or content pass |
| 3 | generic probe failure | block-1 CMD17 `rc=-11`, `step=15` (`STEP_READY`), `normal=0x0002` (TC only); identification and block 0 succeeded, failure before the GPT gate and B1 |

Attempt 2 is the only B1 result. Its report matches the simulator
`probe-b1-stuck-width` failure fields; the 4-bit and 8-bit
`b0`/`b1`/`same`/`div` fields are zero-initialized because those content
passes never ran, and
the zero `FAIL` record means no command/data failure was recorded. Attempt 1
is the H8 signature, and attempt 3 is the same intermittent single-block
TC-only/no-error signature recorded for the earlier attempt-3 boot 3, now at
LBA 1. Attempt 3 has no field-for-field simulator case: the simulator's
`probe-read-timeout` expects a latched `DATA_TIMEOUT` (`error=0x0010`,
`normal=0`), while the hardware report has `error=0` and `normal=0x0002`.
No attempt produced `rc=0 stop=0`, a CMD19/CMD14 success, an 8-bit attempt or
a per-width content comparison; F3 remains `[U]`. The full decode is in
`docs/emmc-cmd2-crc-audit.md` §2.16 and `docs/emmc-driver.md` §5.3.10.

| Check | Result |
| --- | --- |
| eMMC Stage-B1 attempt 1 | RECORDED: H8 CMD3 timeout before B1; no width command |
| eMMC Stage-B1 attempt 2 | RECORDED: 4-bit byte-183 readback mismatch (`sw=0`, `swrc=-19`, stop 1); no CMD19/CMD14 or content pass; matches simulator stuck-readback case |
| eMMC Stage-B1 attempt 3 | RECORDED: block-1 CMD17 `-11`, `normal=0x0002` (TC only); pre-B1; no field-for-field simulator case |
| Stage-B1 F3 | UNPROVEN: no `rc=0 stop=0`, no 4-bit CMD19/CMD14 success, no 8-bit attempt, no per-width content comparison |
| Hash-table self-check in §6 | `rows=45 mismatches=0` |
| `git diff --check` | PASS: clean |

## 7. Limits

- The hashes are a snapshot of a dirty tree; any later edit invalidates them.
- The 2026-11-01 `fmt=4` hardware record consists of two owner-attributed
  observations: an owner-supplied panic text from an SM-T505 that has no flash
  record in this tree, and a photo from the owner's SM-T505N whose BOOT-only
  write and readback are `[C]`. The owner reports the results are identical 1:1.
  The panic itself is a screen photo, not a serial console capture. The flash
  record, readback hashes and photo for the SM-T505N run are in
  `out/gta4l/ram-root-emmc-probe-failclosed-rerun-evidence/`.
- The 2026-11-01 `cmd1gap` hardware record consists of three owner-supplied
  text transcriptions with no photo file or hash in this tree; the run results
  are not independently authenticated and the unit/model attribution is
  owner-reported. The result establishes a successful identification run
  sequence, not the CMD2 cause, the necessity of the 8-us delay, reliability or
  a sector read. The delay remains a candidate, not an adopted fix.
- The 2026-11-02 stage-A record is a single read-only pass on the owner's
  SM-T505N. The report is an owner-supplied photograph whose file and SHA-256
  are in the gitignored `out/` record; every per-LBA sum is bound by one
  same-session TWRP readback, and the GPT gate values are pinned to the live
  header. It establishes repeated single-block CMD17 content reads over the GPT
  region, not a multi-block transfer, bus-width/high-speed result, reliability
  test, filesystem claim or write capability. The SM-T505 unit from the stage-1
  batch was not re-run with the stage-A image.
- The 2026-11-02 A2/F2 attempt-2 record is an owner-supplied OCR text; the
  attached photo file and hash are not in this tree. The BOOT-only write and
  readback were verified on the still-connected device after the failed boot.
  The run establishes a partial open-ended CMD18 data phase (at least 20 of
  the 21 blocks; `mb=20` is an ordinal, not a count) on the owner's SM-T505N,
  not a completed multi-block transfer, a completed CMD12 stop, a content
  match, a reliability result or F2. The terminal
  `CC | BRR` sample means a buffer-read-ready condition was present when
  polling stopped; it does not show that the remaining block or the stop would
  have completed.
- The 2026-11-02 A2/F2 attempt-3 record covers three owner boots of the same
  image; the transcriptions are owner-supplied OCR text (the boot-1 record
  also mentions an owner photograph, but no photo file or hash is in this
  tree). The BOOT-only write and readback were verified; the image was not
  reflashed and the BOOT partition was not re-read after boot 1, so the later
  boots are bound by the flash record and the owner's statement. Boot 1
  stopped during identification at the H8 CMD3-completion timeout (`step=9`,
  no CMD7/CMD18/CMD12 and no sector read). Boot 2 reached the MD phase: the
  bounded drain was entered and exhausted its window with no additional
  blocks and no `TC`, then the `CMD12` stop timed out; the drain does not
  resolve the stall. Boot 3 failed the stage-A `CMD17` read at LBA 2 with
  `TC` but no `CC`/`BRR`, a new intermittent single-block data-phase
  signature. H8 is therefore intermittent across boots, not a deterministic
  build property. The records do not establish a completed multi-block
  transfer, a completed stop, a content match, the drain's sufficiency or F2;
  the CMD3 and CMD17 causes remain open, and whether the reboot between boots
  power-cycled the card is not recorded.
- The 2026-11-02 Stage-B1 record covers three owner boots of the same verified
  image; the transcriptions are owner-supplied OCR text and no photo file or
  hash is in this tree. The image was not reflashed and the BOOT partition was
  not re-read after the boots, so the reports are bound by the flash/readback
  record and the owner's statement. Whether each reboot power-cycled the eMMC
  is not recorded; attempt 2 followed the attempt-1 H8 stop. Only attempt 2 reached B1; it stopped at
  the 4-bit byte-183 readback, so no CMD19/CMD14 or per-width content pass ran.
  Attempt 1 is H8 and attempt 3 is the same TC-only/no-error single-block
  signature as the earlier attempt-3 boot 3. Attempt 2 matches the simulator's
  stuck-readback case; attempt 3 has no field-for-field simulator case. The
  record does not assign the readback failure to the card, pad/DLL or host
  width path, does not establish repeatability, and does not establish F3.
- The 2026-11-03 Stage-B1C record covers seven owner boots of the same verified
  image; the transcriptions are owner-supplied chat text and no photo file or
  hash is in this tree. The BOOT partition was re-read only after boot 3
  (matching the post-flash hash); boots 4-7 are bound by the no-reflash
  statement, not a re-read. Boots 1 and 4 were owner-stated cold boots and the
  rest were warm force reboots. Only boots 3 and 6 reached the continuation;
  both timed out in the CMD19 write phase, and the hard error skipped the
  content pass and byte-183 re-read, whose fields are placeholders. The record
  does not establish a bus-test or content pass, does not separate a
  width-caused timeout from an independent first-write property, and does not
  establish F3.
- The 2026-11-03 Stage-B1C-1 record covers thirteen owner boots of the same
  verified image; the transcriptions are owner-supplied chat text and no photo
  file or hash is in this tree. The BOOT partition was re-read after boot 9
  and after boot 13, both matching the post-flash hash; the no-reflash
  statement covers the boots between the reads. The first nine boots followed
  the owner-stated cold/warm sequence and the final four were the requested
  cold sampling batch. Boots 1 and 12, both cold, reached the 1-bit
  discriminator and timed out in the CMD19 write phase with identical fields;
  no CMD6 was issued on those boots, so the width-caused model is excluded for
  the write failure. The record does not assign that width-independent failure
  to the card, the pad/DLL or the host PIO path, does not observe the
  `trc=-20` class, and does not establish F3.
- The 2026-11-03 data-timeout audit host batch is host-only: the DTO
  report's field values, the forced-TOUTVAL readback and the stop-code
  classification are simulator results, not device samples.
- The 2026-11-03 Stage-B1C-2 record covers twelve owner boots of the same
  verified image; the transcriptions are owner-supplied chat text and no photo
  file or hash is in this tree. The BOOT partition was re-read once after the
  whole batch, matching the post-flash hash; the no-reflash statement covers
  every boot. Boots 1, 3, 8, 9 and 10 reached the DTO stage and their five
  `stop=12` reports are field-for-field identical except the stack counters.
  The stop is a probe-gate artifact: TOUTVAL was already at its minimum and
  the readback differs only on the read-only `INT_STABLE` bit, so E2 did not
  run and no bus-test datapoint was added. The E1-silent reading covers the
  tested configuration and windows only; it does not establish a controller
  or card cause for the pre-B1 CMD17 or CMD13 stops, does not observe the
  `trc=-20` class, and does not establish F3.
- The 2026-11-03 Stage-B2 batch is host-only: the HS probe's byte-185
  readbacks, RCG/clock values, content sums and stop codes are simulator
  results, not device samples. The 50 MHz figure is the pinned
  `clk_rcg2_floor_ops` table result for a 52 MHz request; the parent rate
  and the pad-level frequency were not measured. The batch does not show
  that the card accepts HS_TIMING, that content survives at 50 MHz, or that
  F4 is established, and it does not re-run the B1C-2 device verdict.
- The 2026-11-03 Stage-B2 record covers four owner boots of the same
  verified image; the transcriptions are owner-supplied chat text and no
  photo file or hash is in this tree. The post-batch p69 re-read binds the
  batch; the no-reflash statement covers every boot. Boots 2 and 4 are
  field-for-field identical except the stack counters, and boot 4's
  owner-supplied PAD `post` field carries an extra leading zero (an OCR
  artifact of `00009fe4`). The F4 pass is content-level at the 50 MHz floor
  only: the parent rate is a pinned table value, not a measurement, and the
  batch does not establish a cold-start pass, a width, write, DMA, CQE, ICE,
  HS200 or frequency-measurement result, a cause for the H8 pre-B2 stops, or
  F3.
- The 2026-11-03 Stage-H9 record covers four owner boots of the same
  verified image; the transcriptions are owner-supplied chat text and no
  photo file or hash is in this tree. The post-batch p69 re-read binds the
  batch; the no-reflash statement covers every boot. Boots 1/3/4 are
  field-identical except the stack counters and the attempt-1 poll-tick
  timestamps; their attempt-1 record is the historical H8 tuple, and the
  attempt-2 record's non-zero ERROR/NORMAL samples are the latched-error
  exit shape (the zero `A2L` poll capture is that exit's expected shape,
  not a missing record). The result establishes that an in-boot duplicate
  CMD3 does not recover the completion timeout (0/3) and that the flake
  re-rolls per boot; it does not establish a cause for the H8 class, a
  cold-vs-warm correlation, a recovery mechanism, or any width, write,
  DMA, frequency or F3 result. The batch lost three of four boots to the
  flake, which informs but does not measure a rate.
- The 2026-11-03 Stage-H10 hardware run is one verdict sample: boot 2's
  width-at-HS refusal matches the committed failure fingerprint, but the
  two-matching rule was not met and the 15-boot cap was reached, so the
  verdict carries a single-sample caveat. The run does not establish a
  card BUS_WIDTH acceptance at HS, a CMD19 completion at 4-bit/HS, a
  content result at 4-bit/HS, or F3; the other fourteen boots stopped
  pre-HS with the CMD3 completion-silent class, and their 14/15 count
  informs but does not measure a rate.
- The 2026-11-03 Stage-H11 batch is host-only: the scenario suite exercises
  the probe's H11 flow against simulated MMIO and does not establish a card
  BUS_WIDTH acceptance at 8-bit/HS, a CMD19 completion at 8-bit/HS, a
  content result at 8-bit/HS, the byte-183 paradox on the device, or F3.
  The device run remains owner-gated by a fresh explicit BOOT-only approval
  naming the byte-183 = 1 and = 2 writes, the 8-bit DAT application, the
  CMD19/CMD14 round trip and the 50 MHz ramp.
- The 2026-11-03 Stage-H12 batch is host-only: the scenario suite exercises
  the probe's H12 flow against simulated MMIO and does not establish a card
  BUS_WIDTH acceptance of the DDR value 4, a byte-183 DDR readback on the
  device, the byte-183 paradox on the device, a controller DDR-mode result,
  or F3. The device run remains owner-gated by a fresh explicit BOOT-only
  approval naming the record-only byte-183 = 4 write, its CMD13 TRAN wait,
  its EXT_CSD re-read at the 4-bit SDR DAT width and its revert to 0.
- The pinned Linux cache has no `.git`; its revision is documented provenance,
  not a verifiable git checkout `[U]`.
- The booted image is not tracked; the copy in the gitignored output tree
  matches the recorded hash `[S]`, but reproduction of its firmware and kernel
  inputs from the current checkout is not established `[U]`.
- The local firmware volume is a different build; only the three driver PEs are
  byte-identical to the booted drivers `[S]`.
- No physical measurement exists; the measurement-dependent parts of `E1`–`E9`
  remain `[U]`.
- The official vendor source is CXG1/CWI2, not the captured CXB1; source/binary
  correspondence and the LineageOS-only card-detect blocks remain `[U]`.
