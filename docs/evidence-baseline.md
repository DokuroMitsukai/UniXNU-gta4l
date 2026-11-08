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

A final refresh on `2026-11-05` records the P5-1 read-only block-adapter host
batch and the D8 enabled-personality candidate
(`docs/emmc-driver.md` §5.3.35–§5.3.36,
`docs/gta4l-storage-publication-design.md`). The working tree adds
`package/Gta4lEmmcBlock.c/.h` (portable policy), `package/Gta4lEmmcBlock.cpp`
(thin `IOBlockStorageDevice` adapter), the guarded
`patches/xnu/gta4l-emmc-block.patch` and its `patches/xnu/series` entry,
`scripts/build-gta4l-emmc-block.py` and the host IOKit seam under
`scripts/sim-gta4l-emmc-block/`. The sibling UniXNU working tree adds
`scripts/xnu-native-personality.sh`, sourced by
`scripts/build-xnu-4k-baseline.sh`, which maps
`CONFIG_XNU_NATIVE_GTA4L_EMMC_BLOCK=0` (or absent) to no flag, `=1` to
`-DXNU_NATIVE_GTA4L_EMMC_BLOCK=1` and any other value to a hard error.
`configs/gta4l_defconfig` sets `CONFIG_XNU_NATIVE_GTA4L_EMMC_BLOCK=y` and
force-loads the block archive.

The P5-1 builder first produced `libGta4lEmmcBlock.a` (`fedd4bc3…`) against
the earlier `ca983f7d…` candidate (interface audit `695c7bbe…`, provenance
`cbf3a80b…`, 190 virtual methods, 26 concrete overrides, 0 unresolved
original imports). The D8 host kernel build (exit 0) then produced the
prelinked candidate `b509c511…` (51,187,712 bytes; `kernel.json`
`candidate_sha256` matches) with `-DXNU_NATIVE_GTA4L_EMMC_BLOCK=1` in the
`KernelConfigTables.cpo` record. A relink changes the candidate hash through
the version timestamp, so the storage-ABI audit was re-run against this final
candidate: `interface-audit.json` (`6908349252b920c…`) reports 190 method
slots, 181 kernel bindings (167 inherited + 14 pure virtual), live native 4K
and 0 binding-target diffs against the earlier `66977822…` audit, with the
same `_bsd_init` stop (`0xfffffff0076e7a3c`); `provenance.json` is
`eace94306cd4…` and the live `gdb-stop.json` snapshot is `d328190b…`. The
audit's `gdb_stop_sha256` field names the harness script (`3ba447bc…`), while
the builder's field of the same name names the snapshot; both are recorded so
the two meanings stay distinct.

`scripts/build-gta4l-emmc-block.py` (`ae6fc34a…`, identical in the repository
and the build tree) was re-run in the tree without relinking the kernel and
produced a byte-identical `libGta4lEmmcBlock.a` (`ff447a48…`) from the policy
object, the C++ object and the generated original bindings, with 190 virtual
methods, 26 concrete overrides and 0 unresolved original imports. The
qualification records `qualified=true`, `candidate_matches_audit=true`,
`personality_enabled=true` and `runtime_qualified=false`; the in-tree core
and probe archives are `5e5ee937…` and `19ac22d0…`.
`scripts/test-gta4l-emmc-block.py` passes 23 core scenarios with 18 mutation
controls and 28 adapter scenarios; the core/probe suites pass 179/104 and
163/77; the storage-ABI, payload-contract, inventory, package and boot-image
suites pass. The `emmc-block-1` payload token maps to `gta4l-emmc-block=1`
and is mutually exclusive with every probe token. This is host evidence only:
nothing was flashed or booted, no `IOMedia` object was observed, and F5
remains `[U]`; the device candidate needs a separate BOOT-only approval. That
change refreshes the `README.md`, `porting.toml`, `docs/emmc-driver.md`,
`docs/diagnostic-payload-input-inventory.md`, `package/Gta4lEmmc.h`,
`package/Gta4lEmmcProbe.c`, `scripts/test-gta4l-emmc.py`,
`scripts/build-gta4l-emmc-probe.py` and
`scripts/test-gta4l-diagnostic-payload.py` rows, adds no manifest row, and
keeps the table at 45 rows. The block sources, builder, seam and this
document are outside the table or not listed.

A final refresh on `2026-11-05` records the P5-2 device qualification and its
D9.4 correction (`docs/emmc-driver.md` §5.3.37,
`docs/gta4l-storage-publication-design.md`). The first approved P5-2
BOOT-only flash used the D8 candidate `b509c511…` (block archive `ff447a48…`,
payload `7b2ac8c0…`, boot image `4a635cae…`); it published the whole
`IOMedia` but the report client was not open, so the media-path read failed.
The initial image and failure remain in
`out/gta4l/ram-root-emmc-block-p5-evidence/` and are not relabeled. The D9.4
correction opens the media before the report read and closes it exactly once
on every exit; the host IOKit seam now starts closed and covers the
unopened-read regression and balanced-open accounting. The block suite passes
23 core scenarios with 18 mutation controls and 32 adapter scenarios (was
28).

The corrected p5b candidate is kernel `2f8238b9…` (51,187,712 bytes;
`kernel.json` matches), re-audited against `2f8238b9…` (interface audit
`1f456fec…`, provenance `cdfe6950…`, 190 slots, 26 concrete overrides, 0
unresolved original imports), block archive `6d36826a…` (350,248 bytes),
payload `47b9593d…` (9/9 contract, exactly one `gta4l-emmc-block=1`, no probe
token) and boot image `e256b27d…` (18/18 gate, only the ramdisk differs from
the `ce4d76e8…` baseline); the packaged payload kernel is `26af8293…`. It was
flashed BOOT only to p69 (pre `21f3b5f2…`, post `f1954fd4…`, unchanged tail
`a28b4ea2…`; RECOVERY `14e11370…`, EFS `51fa824c…`, sec_efs `3a265d58…`
unchanged; battery 100%; no host reboot). The owner-supplied report is `rc=0
stop=0 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0 clk=400000`; the
whole media is `whole=1 base=0 size=31268536320 content=GUID_partition_scheme
writable=0`; the driver reports `online=1 wp=1 reads=10 blocks=30 fails=0
max=8`; the media-path read is `lba=1 attempted=1 ok=1 bytes=512
sum=2228245566 base=2228245566 same=1`; the failure record is all zero and
the terminal panic is the intentional report-mode panic at
`Gta4lEmmcBlock.cpp:914`. **F5 is measured PASS for this reported boot at
1-bit SDR, 400 kHz `[C]`.** The boot ordinal and cold/warm class were not
supplied and are not inferred; the owner confirmed on `2026-11-05` that the
tablet returned to TWRP. Partition discovery is a separate observation:
`count=80 expected=84 waited_ms=0 error=0 reported=3`; `expected` is the GPT
header entry-array slot count and the reporter stops waiting on any nonzero
count. Evidence is in `out/gta4l/ram-root-emmc-block-p5b-evidence/`
(`flash-record.json`, `owner-report.txt`, `device-run-record.json`,
`build-provenance.json`, `kernel-string-verification.txt`).

A host-only partition-accounting audit on `2026-11-05` re-used the validated
recovery GPT capture recorded in `docs/emmc-reference.md` (84 entries of 128
bytes, 80 nonempty partitions, no overlaps, identical primary/backup arrays;
primary `98bcfd9a…`, backup `12f80350…`) and the `gpt-report-verified.json`
record (`partition_count=80`, `partitions_equal=true`), and inspected
`IOGUIDPartitionScheme::scan()` and `Gta4lEmmcBlock::countPartitions()`. The
partition scheme skips null-type entries and publishes one non-whole
`IOMedia` per used entry; the reporter counts non-whole `IOMedia` whose
provider chain reaches the driver. The report's `count=80` matches the 80
populated entries, and its first three children (bases 4,194,304, 8,388,608
and 12,582,912 bytes, each 4,194,304 bytes) match GPT entries 1-3. There is
no missing-partition or counting defect; `expected=84` is the entry-array
slot count and remains a misleading label only. Partition completeness is
not claimed from the report alone, the accepted p5b artifacts are unchanged,
and no source correction is proposed. This audit refreshes the `README.md`,
`porting.toml` and `docs/emmc-driver.md` rows and this document, adds no
manifest row, and keeps the table at 45 rows.

A 2026-11-06 closeout refresh records the D10.5 F2 device run and its
one-cold-boot verdict (`docs/emmc-driver.md` §5.3.41). The owner-supplied
first cold boot of the dirty-tree candidate (prelinked kernel `bb22f6ca…` of
51,187,712 bytes, packaged kernel `2d87a4be…`, payload `93110d8c…` with an
8/8 contract selecting exactly one `gta4l-emmc-probe=1` and one
`gta4l-emmc-mb=1`, and BOOT image `bcc93a47…` with an 18/18 gate) returned
`rc=0 step=19 log=55 mb=1`. The short request
`s=0/2/2/2/0/0/0003/0000/00000b00` and the long request
`l=0/21/21/21/0/0/0003/0000/00000b00` both completed and byte-validated every
requested sector, both explicit CMD12 stops were clean (`NORMAL=0x0003`,
`ERROR=0x0000`), `rec=0/1/0` records clean matching recovery reads,
`mbsame=1` records aggregate agreement with the same-boot CMD17 reference and
`drain=0/0/0/0` records that the optional drain was not used. The `rc=0`
report is reachable only through the D10.4a acceptance gate, so **F2 is
measured PASS for that one cold boot at 1-bit SDR, 400 kHz `[C]`**.
Repeatability, cold-start reliability, general performance and multi-block
publication remain `[U]`; the owner confirmed TWRP recovery after the boot and
no host reboot was issued. Raw evidence is in
`out/gta4l/ram-root-emmc-mb-d10.5-closeout-evidence/` (`owner-report.txt`,
`owner-clarification.txt`, `device-run-record.json`, `evidence-sha256.txt`),
with the original 94-file build/flash manifest unchanged in
`out/gta4l/ram-root-emmc-mb-d10.5-evidence/`. This refresh updates the
`README.md`, `porting.toml`, `docs/emmc-driver.md`,
`docs/diagnostic-payload-input-inventory.md` and
`docs/gta4l-storage-publication-design.md` rows and this document, adds no
manifest row, and keeps the table at 45 rows.

A 2026-11-06 host-only refresh records the D12.6 closeout and the D13
readiness pass. D12.6 is one owner-supplied H9T report from the first
post-flash cold boot of BOOT image `7c76c7eb…` (18/18 gate, payload
`e384cc06…`, packaged kernel `12385bc7…`): `ver=1 rc=0 stop=0 sub=1 att=1
tr=0 r1=0 retry=0/0`, a 25-iteration poll with zero-based terminal 24, kind
`C`, NORMAL `0001`, RESPONSE0 `00000500`, no timeout capture, `TRC n=6` at
0/1/2/4/8/16 and zero FAIL validity. The kind column renders `S/?/C`,
accepting the formatter correction for that report; no exact timestamp or
photo bytes were supplied, the scheduled samples remain interval-bounded and
reliability is unresolved. F2 and p5b F5 are unchanged. The D13 readiness
record (`docs/emmc-d13-readiness-2026-11-06.md`) is design and source
inspection only: it grounds the one-sector adapter seam, the portable core and
the diagnostic CMD18/CMD12 API, proposes the conservative two-sector chunk
contract, records the open timing/selector decisions and enumerates the host
scenarios without implementing, building, running QEMU, accessing the device,
committing or pushing. The storage-ABI suite still fails closed on the stale
p5b candidate/provenance and was not regenerated. This refresh updates
`README.md`, `porting.toml`, `docs/emmc-driver.md` and
`docs/gta4l-storage-publication-design.md`, refreshes the `README.md`,
`porting.toml` and `docs/emmc-driver.md` hash rows, adds the two rows below and
this document; no earlier verdict or manifest is rewritten. The follow-up N3
host implementation is recorded in `docs/emmc-driver.md` §5.3.48 and the N3
row below; it changes no earlier verdict or manifest.

A 2026-11-07 closeout refresh records the N4 candidate and the N5 hardware
observation (`docs/emmc-n5-integration-observation-2026-11-07.md`). N4 built
the mode-3 integration candidate through the archive/prelink/payload/BOOT flow:
prelinked kernel `f37f8404…` (51,224,576 bytes), packaged kernel `2e6d91a0…`,
payload `4a400ca8…` (9/9 contract, `emmc-block-3`) and BOOT image `54cdbfb6…`
(85,688,320 bytes, 18/18 gate, only the ramdisk differs from `ce4d76e8…`).
A candidate-specific storage-ABI audit is host/QEMU PASS (`72d4df59…`
interface audit, `672aa25b…` gdb-stop snapshot) with
`runtime_qualified=false`; it is not a runtime ABI claim.
N5 flashed that image BOOT only to p69 (pre-flash `b19c59b8…` saved as
`boot-before-n5.img.xz`; post-flash full `22bea393…`, prefix `54cdbfb6…`, tail
`a28b4ea2…`; RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…`
unchanged; battery 95%; no host reboot). One owner-classified cold boot
returned `rc=0 stop=0 mode=3` with the read-only whole media, an LBA 1
baseline match and two controlled IOMedia reads at LBA 2: two blocks delivered
1,024 exact bytes through one two-sector CMD18 chunk and one clean CMD12;
three blocks delivered 1,536 exact bytes through one CMD18 chunk, one clean
CMD12 and one CMD17 odd tail. The report's `count=22 expected=84 waited_ms=0`
partition snapshot is an unsynchronized mid-publication observation, not a
completeness verdict; `expected` is the same-boot GPT entry-array slot count
and the validated GPT has 80 populated entries. Acceptance is limited to those
two requests on that one cold boot at 1-bit SDR, 400 kHz; repeatability,
initialization reliability, wider/faster modes, arbitrary I/O, mount/root
switch, DMA/CQE/ICE and partition completeness remain `[U]`. The one-boot
budget is consumed. Raw evidence is in
`out/gta4l/ram-root-emmc-n5-hardware-evidence/` (`flash-record.json`,
`flash-commands.log`, `preflash-hashes.txt`, `postflash-hashes.txt`,
`staging.txt`, the candidate and rollback xz files, `supplemental-manifest.txt`
and `observation-1/` with `owner-transcript-raw.txt` SHA-256 `60700f36…`,
`observation-record.json` and `interpretation.md`), with the N4 build evidence
unchanged in `out/gta4l/ram-root-emmc-n4-candidate-evidence/`. This refresh
updates `README.md`, `porting.toml`, `docs/emmc-driver.md`,
`docs/gta4l-storage-publication-design.md` and this document, refreshes the
`README.md`, `porting.toml` and `docs/emmc-driver.md` hash rows, adds the N5
row below and keeps the table at 45 rows.

A 2026-11-07 N6 closeout records the second clean control of the unchanged
D12.6 stage-9 candidate (`docs/emmc-n6-reliability-observation-2026-11-07.md`).
The preserved BOOT image `7c76c7eb…` (85,688,320 bytes, 18/18 gate, payload
`e384cc06…`, packaged kernel `12385bc7…`) was re-flashed BOOT only to p69
after backing up the N5 image `22bea393…` as `boot-before-n6.img.xz`
(`5eaaa423…`); the post-flash full p69 is `b19c59b8…`, the prefix is
`7c76c7eb…`, the tail is `a28b4ea2…`, and RECOVERY/EFS/sec_efs are unchanged.
One owner-classified cold boot returned `rc=0 stop=0 sub=1 att=1 tr=0 r1=0
retry=0/0` with a 25-iteration CMD3 poll, terminal kind `C`, NORMAL `0001`,
ERROR `0000`, RESPONSE0 `00000500`, ticks `0x14f2`, no timeout capture and an
all-zero failure record. The typed C3L poll ticks are normalized to `0x14e4`
from the source format and the prior D12.6 report; poll and terminal timings
are distinct sequential measurements. This is a clean control/non-reproduction:
two clean cold-boot controls of the same candidate, not a continuous trial,
failure-rate estimate, fix or proof of reliable initialization. Initialization
reliability remains `[U]`; F2, p5b/F5 and N5 are unchanged; the N6 boot budget
is consumed. Raw evidence is in
`out/gta4l/ram-root-emmc-n6-reliability-evidence/` (`flash-record.json`,
`flash-commands.log`, `preflash-hashes.txt`, `postflash-hashes.txt`,
`staging.txt`, `backup-device.log`, the candidate and rollback xz files, and
`observation-1/` with `owner-transcript-raw.txt` SHA-256 `b7702fce…`,
`observation-record.json` and `interpretation.md`). This refresh updates
`README.md`, `porting.toml`, `docs/emmc-driver.md`,
`docs/gta4l-storage-publication-design.md` and this document, refreshes the
`README.md`, `porting.toml` and `docs/emmc-driver.md` hash rows without adding
a hash-table row, adds the N6 row below and keeps the hash table at 45 rows.

A 2026-11-07 host-only N8.1 design records a bounded partition-publication
observation (`docs/emmc-n8-partition-observation-design-2026-11-07.md`). The
current reporter's `countPartitions()` stops at the first nonzero count and
immediately re-samples, and `expected` is the GPT header entry-array slot count
(84), not the 80 populated entries, so N5's `count=22 expected=84 waited_ms=0`
is an unsynchronized mid-publication snapshot. The design chooses an opt-in
`gta4l-emmc-partobs=1` token orthogonal to `gta4l-emmc-block=1|3`; when it is
absent, the legacy mode-1 and mode-3 reporter is unchanged. It proposes a
4,000 ms deadline, a 50 ms sampling interval and a 250 ms count-unchanged
window, all in the report thread and never under the controller gate, and a
`PARTOBS` line with first/final counts, nominal times, changes, stability,
timeout, none, error, the 84 slot count and an explicit `complete=0`. A stable
count is not completeness; no completeness claim is made without an
independent validated GPT-to-`IOMedia` range/count comparison. Iterator
failure keeps `stop=15`; a deadline or zero count is not a failure. The
worst-case report path is 24 s against the 30 s watchdog. Host scenarios and
mutation controls are specified; no code, build, QEMU, device access, flash or
boot was performed. This refresh updates `README.md`, `porting.toml`,
`docs/emmc-driver.md`, `docs/gta4l-storage-publication-design.md` and this
document, refreshes the `README.md`, `porting.toml` and `docs/emmc-driver.md`
hash rows without adding a hash-table row, adds the N8 row below and keeps the
hash table at 45 rows.

A 2026-11-07 N8.2 host implementation completes that observation without a
candidate build, QEMU run, device access, flash or boot. The selector accepts
`gta4l-emmc-partobs=1` only with exactly one block-mode 1/3 token and refuses
an orphan, duplicate or non-1 value; the legacy `PART` path is byte-for-byte
unchanged when the token is absent. The bounded `PARTOBS` line keeps
`complete=0`, and a stable partial count is not presented as complete. The
diagnostic payload builder maps `emmc-block-1-partobs` and
`emmc-block-3-partobs` to two kernel arguments; the payload checker refuses a
partobs token without a block token or with a mismatched personality, and the
packager accepts both names. Effective host results: core 48 scenarios and 38
mutation controls (unchanged), adapter 64 scenarios (nine partobs) with 12
adapter mutation controls (nine partobs) and three policy mutation controls,
all rejected for the intended reason; the diagnostic payload suite passes 22
checks including both partobs mappings. The design document's section 8
records the implementation and the `partobs-changing` result (a final count
that follows the last published profile entry, with `changes=80`, `stable=0`
and `timed_out=1`). This refresh updates `README.md`, `porting.toml`,
`docs/emmc-driver.md`, `docs/gta4l-storage-publication-design.md`,
`docs/diagnostic-payload-input-inventory.md` and this document, refreshes the
`README.md`, `porting.toml`, `docs/emmc-driver.md`,
`docs/diagnostic-payload-input-inventory.md` and
`scripts/test-gta4l-diagnostic-payload.py` hash rows without adding a
hash-table row, and keeps the hash table at 45 rows. N8.3 candidate production
and N8.4 hardware observation remain separate approvals; initialization
reliability remains `[U]`.

A 2026-11-07 host-only N8.3 batch produced and gated the candidate for that
observation without device access, flash, boot, QEMU device run, commit or
push. The immutable chain is prelinked kernel `4dcbf24d…` (51,224,576 bytes;
`kernel.json` `0481cbab…`), linked archive `f9f83526…` (512,896 bytes, 190
method slots, 181 kernel bindings, 26 concrete overrides, 0 unresolved
original imports, `runtime_qualified=false`, `personality_enabled=true`),
candidate storage-ABI audit `out/gta4l/storage-abi-4dcbf24d` (interface audit
`a30cfcde…`, provenance `a806c6cf…`, snapshot `222f6322…`, run `85307464…`,
candidate plan `0481cbab…`), packaged kernel `023c8ff8…` (51,224,576 bytes),
payload `5f6759f2…` (83,886,080 bytes, 11/11 contract, diagnostic
`emmc-block-3-partobs` mapping to `gta4l-emmc-block=3 gta4l-emmc-partobs=1`)
and BOOT image `3d7e71af…` (85,688,320 bytes, 18/18 gate, only the ramdisk
differs from the `ce4d76e8…` v5 baseline). The kernel-build archive step
produced the `3e76771d…` container variant; the requalified `f9f83526…`
archive has byte-identical members. The block suite passes 48 core
scenarios/38 mutation controls and 64 adapter scenarios with 12 adapter
mutation controls plus three policy mutation controls; the storage-ABI
positive and negative checks pass. A token scan on the extracted payload
kernel finds one `PARTOBS` string, three `REPORT` strings, one legacy `PART:`
string, four `gta4l-emmc-partobs` strings and no literal `complete=`; the
runtime `PARTOBS` line still formats `complete=0`, so the count-only non-claim
is preserved. This refresh updates `README.md`, `porting.toml`,
`docs/emmc-driver.md`, `docs/gta4l-storage-publication-design.md` and this
document, refreshes the `README.md`, `porting.toml` and `docs/emmc-driver.md`
hash rows without adding a hash-table row, adds the N8.3 row below and keeps
the hash table at 45 rows. N8.4 hardware observation remains a separate
BOOT-only approval; initialization reliability remains `[U]`.

A 2026-11-07 N8.4 batch flashed the N8.3 candidate BOOT-only to p69 and
performed one owner-controlled first cold boot. The pre-flash p69
`b19c59b8…` was backed up and verified as `boot-before-n8.img.xz`
(`8b37f41c…`); the full p69 readback `7c22e499…` matched the candidate prefix
`3d7e71af…` plus the unchanged tail `a28b4ea2…`, and RECOVERY `14e11370…`,
EFS `51fa824c…` and sec_efs `3a265d58…` stayed unchanged. The host did not
reboot. The boot returned the userspace `RAMROOT-FST PASS: pid=1 page=4096
fs=read+seek+eof timer=poll` panic and no eMMC report. The fixture deliberately
panics after its checks and a 50 ms poll, while the eMMC reporter independently
waits for media (up to 20,000 ms), performs the controlled reads and observes
the partition count for up to 4,000 ms; the fixture can therefore win the
terminal before the report exists. The observation is **inconclusive**: no
`PARTOBS`, controlled-read or count line was captured, and the transcript
cannot distinguish a reporter that had not started, one still waiting for
media, and one that lost the terminal race. The boot budget is consumed; no
retry or reflash was performed. The host-only correction adds an additive
`fs-timer-hold` fixture variant that runs the same checks and then blocks in a
one-second poll instead of panicking, leaving termination to the eMMC report
thread or its watchdog; failure paths still panic immediately, and the
original `fs-timer` fixture is byte-identical (`user.o` `6984333d…`, signed
`b316e44f…`, trust cache `e146717a…`, root `77d5eba0…`, `fixture.json`
`37cbdd0b…`). The hold variant
builds and signs as `e6546715…` (root `327ac826…`, trust cache `30a72f78…`,
nine verified pages). The diagnostic payload builder now refuses
`emmc-block-1-partobs`/`emmc-block-3-partobs` unless
`--data-ramdisk-manifest` names the hold fixture (and any declared
`hold_after_success` agrees), records the fixture path/hash/diagnostic/hold
state in the payload manifest, and thereby rejects the old early-panic fixture.
Host tests cover the original object hash, the hold loop, the retained failure
panic, the missing-manifest rejection, the early-panic rejection and the
hold/diagnostic mismatch. This refresh updates `README.md`, `porting.toml`,
`docs/emmc-driver.md`, `docs/ram-root-fixture-reproducibility.md`,
`docs/diagnostic-payload-input-inventory.md`,
`docs/gta4l-storage-publication-design.md`,
`docs/emmc-n8-partition-observation-design-2026-11-07.md`,
`docs/emmc-n8.4-partobs-observation-2026-11-07.md`,
`scripts/test-gta4l-diagnostic-payload.py`,
`scripts/test-gta4l-ram-root-fixture-recipe.py` and this document, refreshes
the `README.md`, `porting.toml`, `docs/emmc-driver.md`,
`docs/ram-root-fixture-reproducibility.md`,
`docs/diagnostic-payload-input-inventory.md`,
`scripts/test-gta4l-diagnostic-payload.py` and
`scripts/test-gta4l-ram-root-fixture-recipe.py` hash rows without adding a
hash-table row, adds the N8.4 row below and keeps the hash table at 45 rows.
A new candidate build, a new BOOT-only flash and one new owner-controlled boot
remain separately gated; initialization reliability remains `[U]`.

A 2026-11-07 N8.5 batch rebuilt the observation payload and BOOT image with
the additive `fs-timer-hold` fixture and flashed BOOT-only to p69. The
unchanged N8.3 kernel `4dcbf24d…` and archive `f9f83526…` were reused; the
hold root is `327ac826…`, the hold trust cache `30a72f78…`, the signed fixture
`e6546715…` (nine verified pages) and `fixture.json` `00363681…`
(`diagnostic=fs-timer-hold`, `hold_after_success=true`). The original
`fs-timer` fixture remains byte-identical (`user.o` `6984333d…`, signed
`b316e44f…`, root `77d5eba0…`, `fixture.json` `37cbdd0b…`). The new payload is
`28eafd24…` (83,886,080 bytes, 11/11 contract, `emmc-block-3-partobs`) and the
BOOT image is `b4f4f1ea…` (85,688,320 bytes, 18/18 gate, only the ramdisk
differs from the `ce4d76e8…` v5 baseline); the packaged kernel is `023c8ff8…`.
The payload manifest records the hold fixture path/hash, diagnostic and hold
state, and the builder rejects the old early-panic fixture for both partobs
personalities. The pre-flash p69 `7c22e499…` (the N8.3 candidate) was backed
up and verified as `boot-before-n8.5.img.xz` (`e5b43228…`); the full p69
readback `9d51fe80…` matches the candidate prefix `b4f4f1ea…` plus the
unchanged tail `a28b4ea2…`, and RECOVERY/EFS/sec_efs are unchanged. The device
disconnected once after the backup and before any write; adb was restarted,
p69 was re-read unchanged, and the candidate was then written once. The host
did not reboot; one owner-controlled boot remains, with a stable count still
not a completeness claim and a watchdog report an observer result rather than
a card verdict. This refresh updates `README.md`, `porting.toml`,
`docs/emmc-driver.md`, `docs/emmc-n8-partition-observation-design-2026-11-07.md`,
`docs/emmc-n8.4-partobs-observation-2026-11-07.md`,
`docs/gta4l-storage-publication-design.md`, adds
`docs/emmc-n8.5-hold-fixture-candidate-2026-11-07.md` and this document,
refreshes the `README.md`, `porting.toml` and `docs/emmc-driver.md` hash rows
without adding a hash-table row, adds the N8.5 row below and keeps the hash
table at 45 rows. The subsequent N8.5 observation is recorded below. The boot
budget is one; no retry or reflash follows, and initialization reliability
remains `[U]`.

A 2026-11-07 N8.5 observation closeout records the one owner-classified first
cold boot of that image. The report returned `rc=0 stop=0 mode=3` with the
whole read-only media (base 0, size 31,268,536,320, `GUID_partition_scheme`,
`writable=0`), an LBA 1 baseline match (`sum=2228245566 same=1`), exact two-
and three-block reads through two-sector CMD18 chunks with clean CMD12 stops
and a CMD17 odd tail, and `PARTOBS samples=8 first=39 final=80 final_ms=350
changes=2 stable=1 stable_ms=250 deadline_ms=4000 timed_out=0 none=0 error=0
slots=84 reported=3 complete=0`. The first sample was already 39, so the
initial transition was not observed; the intermediate count is unknown.
`stable=1` is a count-held window, not a completeness claim; `final=80` is
consistent with the validated GPT's 80 populated entries among 84 slots, but a
count is not an exact GPT-to-`IOMedia` comparison and `complete=0` remains
explicit. The boot budget is consumed, no driver fix is indicated, and
initialization reliability remains `[U]`. The raw transcript `240ab0dd…`,
interpretation `0bea53a6…` and observation record `92852a40…` are in
`out/gta4l/ram-root-emmc-n8.5-hardware-evidence/observation-1/`, with a
separate non-self-hashing manifest; the original flash record and
`evidence-sha256.txt` are unchanged. The same host-only batch produced the
N9.1 design (`docs/emmc-n9.1-gpt-iomedia-comparison-design-2026-11-07.md`)
for an opt-in, bounded, read-only exact GPT-to-`IOMedia` comparison that
preserves N8's `complete=0`. This refresh updates `README.md`, `porting.toml`,
`docs/emmc-driver.md`, `docs/gta4l-storage-publication-design.md`,
`docs/ram-root-fixture-reproducibility.md`,
`docs/diagnostic-payload-input-inventory.md` and this document, adds the N8.5
finding and N9.1 design documents, refreshes the affected hash rows without
adding a hash-table row, and keeps the hash table at 45 rows. N9.2
implementation, N9.3 production and any N9.4 hardware run remain separately
gated.

## 1. Repository state

| Item | Value |
| --- | --- |
| Repository | `src/UniXNU-gta4l` (device repository) |
| Branch | `bringup/emmc-core` |
| HEAD at the hash snapshot | `1ee1f4f` (storage: extract the eMMC platform and split the archives) |
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
| Forty-seventh P5-1/D8 host-batch paths at this snapshot | 27: 19 modified (`README.md`, `board.toml`, `configs/gta4l_defconfig`, `docs/diagnostic-payload-input-inventory.md`, `docs/emmc-driver.md`, `docs/evidence-baseline.md`, `docs/gta4l-storage-publication-design.md`, `package/Gta4lEmmc.h`, `package/Gta4lEmmcBlockCore.h`, `package/Gta4lEmmcPlatform.h`, `patches/xnu/series`, `porting.toml`, `scripts/audit-gta4l-storage-interface.py`, `scripts/build-gta4l-diagnostic-payload.py`, `scripts/check-gta4l-payload.py`, `scripts/package-gta4l-diagnostic.py`, `scripts/test-gta4l-diagnostic-payload.py`, `scripts/test-gta4l-emmc-block.py`, `scripts/test-gta4l-storage-abi.py`) and 8 added (`package/Gta4lEmmcBlock.c`, `package/Gta4lEmmcBlock.cpp`, `package/Gta4lEmmcBlock.h`, `patches/xnu/gta4l-emmc-block.patch`, `scripts/build-gta4l-emmc-block.py`, `scripts/sim-gta4l-emmc-block/Gta4lEmmcBlockTestSupport.h`, `scripts/sim-gta4l-emmc-block/adapter_harness.cpp`, `scripts/sim-gta4l-emmc-block/adapter_support.cpp`) |
| Dirty entries at the hash snapshot | 27, exactly the forty-seventh paths above |
| Staging and commits | None at the snapshot; the tree remains unstaged and uncommitted, and no staging or commit is authorized |

The hash table describes the forty-seventh working-tree snapshot: the
`1ee1f4f` base, the twenty-fourth CMD7-diagnostics batch, the twenty-fifth
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
batch, the Stage-H13 host and device-record batches, and the P5-1/D8 host
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
of `1ee1f4f`; the `60817c3` base recorded by the earlier snapshots is
superseded by the current `HEAD` row. The eleventh correction commit `4c447b9`
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
| Repository README | `README.md` | `abf536634ba08a21d3a3583b9d0633baa69551f9bf869a394f1e31c682cdc825` |
| eMMC reference | `docs/emmc-reference.md` | `d2c0167febb8d1afec6cba2afa9a842712c7f937c0c34514acd8077750054b3d` |
| NativeEmmc audit | `docs/native-emmc-reference-audit.md` | `c123577953c3c8625e2119967a112826448dd0a95c861023a9593bca0dc1a215` |
| Porting record | `porting.toml` | `4feb51a62e831a7e06e5d066a2866db4df76627e188e7f5357926fba8d1ed447` |
| Porting-approach reassessment | `docs/porting-approach-reassessment-2026-10-31.md` | `ad77a51fbe36ab78333ba7c62e3f606ebbcede1549bfebc0bc5a08f777e8fa71` |
| Arch Linux gta4l evidence | `docs/archlinux-gta4l-evidence.md` | `9704e402411a31cbffe6dd1c1318c678e9275f96506728e18fe6c761116b13a7` |
| eMMC driver record | `docs/emmc-driver.md` | `7b121daf4b38fe5a2043c6b6672e0f5d7bfdd520c7c02a4532d2cf949a3ad5ff` |
| eMMC core header | `package/Gta4lEmmc.h` | `a05a9a06ee4fa2ac5f60c6add22fc56cb90972ef6917010aa9c7fabdff3149eb` |
| eMMC core source | `package/Gta4lEmmc.c` | `66fb20a2de47edffdfa517df5622284609d187b283847db287dcb88e3141bf18` |
| eMMC XNU probe | `package/Gta4lEmmcProbe.c` | `51d3ad6520cd09a1b90b7c3ab998dc16e424407663989227af0c5c22b750d17e` |
| eMMC simulator | `scripts/sim-gta4l-emmc/harness.c` | `744330d9a5ed8684dabb2f60171eb695311fe9c6c587c29ccc7ec5383f52ec43` |
| eMMC host test | `scripts/test-gta4l-emmc.py` | `a851d113da952d87aebf4776d819b213afaa580ab6a597e8407e0c3463d57bf8` |
| eMMC archive builder | `scripts/build-gta4l-emmc-probe.py` | `272db9f0fd02fe9f6960f647e017f4e7cc156f6cc53010bb8ec8d6a1c6137d7f` |
| RAM-root fixture reproduction | `docs/ram-root-fixture-reproducibility.md` | `73ca21a8bebf91354be878f8aff6b912fca3e03dee7e7ac1e2c55525381adf9e` |
| RAM-root fixture recipe test | `scripts/test-gta4l-ram-root-fixture-recipe.py` | `ebe98274007c554c3bb69293eefc1a606d68dddb3ce401efb57d0b3409ed891f` |
| Diagnostic payload/boot-image inventory | `docs/diagnostic-payload-input-inventory.md` | `17bef3a3eba2d25384251f43a2d641513b89d28a48bc3628042960321e2955ff` |
| Diagnostic payload build recipe | `docs/payload-build-recipe.md` | `4c7ddf9f62fd8921481ec0a2304fc0ce845a9ba46f7c3d2a6648e7c650002ab5` |
| ADT seed normalizer | `scripts/normalize-gta4l-adt-seed.py` | `cf80caafdd3b56fa590fed208e17e2eb8f5c27be1451dbaf1c57b0bbb7f76cf5` |
| Diagnostic payload builder test | `scripts/test-gta4l-diagnostic-payload.py` | `c60868d5b015c47fb9792b2f65a90123fb9f59da542a7511074ff30413811cd6` |

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
The N5 closeout refreshed the `README.md`, `porting.toml` and
`docs/emmc-driver.md` rows without adding a row; the N5 finding document and
the private evidence directory are outside the table or not listed.
The N6 closeout refreshed the same three rows without adding a hash-table row;
the N6 finding document and the private evidence directory are outside the
table or not listed.
The N8 design refreshed the same three rows without adding a hash-table row;
the N8 design document and the design-doc section are outside the table or not
listed. The N8.2 implementation refreshed the `README.md`, `porting.toml`,
`docs/emmc-driver.md`, `docs/diagnostic-payload-input-inventory.md` and
`scripts/test-gta4l-diagnostic-payload.py` rows without adding a row; the N8.2
design-document section, the block package/simulator files and the diagnostic
builder/checker/packager scripts are outside the table or not listed.
The N8.3 candidate production refreshed the `README.md`, `porting.toml` and
`docs/emmc-driver.md` rows without adding a hash-table row; the N8.3
design-document sections, `docs/gta4l-storage-publication-design.md` and the
private evidence directory are outside the table or not listed.
The N8.4 batch refreshed the `README.md`, `porting.toml`, `docs/emmc-driver.md`,
`docs/ram-root-fixture-reproducibility.md`,
`docs/diagnostic-payload-input-inventory.md`,
`scripts/test-gta4l-diagnostic-payload.py` and
`scripts/test-gta4l-ram-root-fixture-recipe.py` rows without adding a
hash-table row; the N8.4 finding document, the design-document section,
`docs/gta4l-storage-publication-design.md`, the shared UniXNU fixture
builder/include and the private evidence directory are outside the table or
not listed.
The N8.5 batch refreshed the `README.md`, `porting.toml` and
`docs/emmc-driver.md` rows without adding a hash-table row; the new N8.5
finding document, the design-document sections,
`docs/gta4l-storage-publication-design.md`, the shared UniXNU fixture
builder/include and the private evidence directories are outside the table or
not listed.
The N8.5 observation closeout refreshed the `README.md`, `porting.toml`,
`docs/emmc-driver.md`, `docs/ram-root-fixture-reproducibility.md` and
`docs/diagnostic-payload-input-inventory.md` rows without adding a hash-table
row; the N8.5 finding, the N9.1 design document, the design-document sections
and the private evidence directories are outside the table or not listed.
The N9.2 host implementation refreshed the `README.md`,
`docs/emmc-driver.md`, `docs/diagnostic-payload-input-inventory.md`,
`scripts/test-gta4l-diagnostic-payload.py` and `porting.toml` rows without
adding a hash-table row; the N9.1 design document, the design-document
section, the block package/simulator files and the diagnostic
builder/checker/packager scripts are outside the table or not listed.
The N9.3 candidate production refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a hash-table
row; the N9.1 design document, the N9.3 finding document, the design-document
sections, the block package/simulator files and the diagnostic
builder/checker/packager scripts are outside the table or not listed.
The N9.5 candidate production refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a
hash-table row; the N9.5 finding document, the storage-ABI
artifacts and the private evidence directory are outside the table
or not listed.
The N9.6 observation and N9.7 seam investigation refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a hash-table row;
the new N9.6 finding document, the N9.7 seam sections and the private evidence
directories are outside the table or not listed.
The N9.7 correction and candidate production refreshed the same `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a hash-table row;
the N9.6 finding document, the correction and production sections and the
block package/simulator files are outside the table or not listed.
The N9.7 observation closeout refreshed the `README.md`, `docs/emmc-driver.md`
and `porting.toml` rows without adding a hash-table row; the new N9.7 finding
document, the §5.3.66 section and the private evidence directory are outside
the table or not listed.
The N9.7 read-detail host acceptance refreshed the `README.md`,
`docs/emmc-driver.md`, `docs/diagnostic-payload-input-inventory.md` and
`porting.toml` rows without adding a hash-table row; the block
package/simulator/test files, the new §5.3.67 section and the
`docs/gta4l-storage-publication-design.md` sections are outside the table or
not listed.
The N9.8 read-detail candidate production refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a hash-table row;
the new N9.8 finding document, the §5.3.68 section and the private evidence
directory are outside the table or not listed.
The N9.9 page-aligned comparator read buffer refreshed the `docs/emmc-driver.md`
and `porting.toml` rows without adding a hash-table row; the new N9.9 finding
document and the §5.3.70 section are outside the table or not listed.
The N9.10 page-aligned comparator candidate production and its one-boot
observation refreshed the `README.md`, `docs/emmc-driver.md` and `porting.toml`
rows without adding a hash-table row; the new N9.10 candidate and observation
documents, the §5.3.71-§5.3.72 sections and the private evidence directory are
outside the table or not listed.
The N9.11 unchanged-image reliability protocol design is a host-only proposal
that refreshed the `README.md` and `porting.toml` rows without adding a
hash-table row; the new N9.11 document is outside the table or not listed.
The N9.11 owner-reported repeatability observation refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a hash-table row;
the new observation document is outside the table or not listed.
The N10.1 read-only IOMedia content qualification candidate production
refreshed the `README.md`, `docs/emmc-driver.md`,
`docs/diagnostic-payload-input-inventory.md`, `porting.toml` and
`scripts/test-gta4l-diagnostic-payload.py` rows without adding a hash-table
row; the new N10.1 finding document, the §5.3.74 section and the private
evidence directory are outside the table or not listed.
The N10.1 hardware-result closeout refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a hash-table row;
the new hardware-result document, the §5.3.75 section, the corrected candidate
illustrative line and the private evidence directory are outside the table or
not listed. The independent read-only post-boot comparison is recorded with
the hardware result and is not a second boot.
The N10.1 read-only content-qualification captured repeatability protocol is a
host-only proposal that refreshed the `README.md` and `porting.toml` rows
without adding a hash-table row; the new N10.1 protocol document is outside
the table or not listed. The proposal pins the unchanged N10.1 image, proposes
three additional owner-controlled cold boots (owner-selected finite budget),
requires per-attempt full-report capture and hash-chained evidence, defines
PARTREAD outcome classes and stop rules, and authorizes no device access,
flash, boot, commit or push. A formatting-only cleanup also removed the extra
blank line at EOF from the N9.11 protocol document; its content is unchanged.
The N10.1 captured repeatability execution then completed: the owner approved
the protocol's section 10 scope with three additional cold boots, observations
2-4 were supplied as screen transcriptions and all three were class D with the
same six-of-six PARTREAD signature, and the approved read-only pre-series and
post-series checks matched the unchanged p69 and protected partitions.
`docs/emmc-n10.1-partread-repeatability-observation-2026-11-08.md` is outside
the table or not listed. The series supports
descriptive repeatability only; initialization reliability remains `[U]`.
The N10.2 read-only content-qualification expansion refreshed the `README.md`,
`docs/emmc-driver.md` and `porting.toml` rows without adding a hash-table row;
the new N10.2 candidate document, the §5.3.77 section and the private evidence
directory are outside the table or not listed. The host-only candidate
expands the pinned partread table from six to fourteen cases (whole-media
PMBR/header/array splits, the first usable LBA, the backup array/header and
two kernel-section ranges at 4 KiB and 1 MiB) and does not change the
selector, discovery, bounce buffer, digest comparison, reason codes or
`complete=1` guard. It records no flash, boot or device access; the N10.1
hardware and repeatability results are preserved and initialization
reliability remains `[U]`.
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
| Current native eMMC core test | PASS: 187 simulator scenarios and 115 mutation controls, including the Stage-B1 4-bit/8-bit switch, CMD6 and bus-test error cases, the verified 4-bit switch (read-only properties comparison with the unreadable byte-183 value held at zero), the verified mismatch, failed-rollback and CMD6-error cases, the baseline-based recovery pass, surviving-inhibit and mismatched-properties cases, the corrected CMD14 CRC exemption and 1-bit two-bit pattern, the Stage-B2 HS enter/leave, ignored byte-185, SWITCH_ERROR and busy-RCG cases, the Stage-H9 CMD3 retry clean/pass/fail/status/later-failure cases plus the D12.5 timeline clean/timeout/stale-CC-clear-order/store-capacity-boundary/mixed-config/reinitialization/short-budget/saturation cases, the Stage-H13 data-reset clear/stuck, surviving-inhibit, CMD14 mismatch/read-timeout/read-CRC/read-status, non-wedged-write and non-timeout-write cases, and the Stage-H14 clean/silent/CMD13-CRC/exempt-inhibit/uninitialized post-write drain plus the rung-2 reset-plus-drain pass, record-only silent drain and surviving-inhibit cases |
| Current eMMC probe report test | PASS: 196 scenarios and 100 mutation controls, including the D10.4a acceptance barriers (short-request, long-request, recovery and overall success gate), the Stage-H15 verified 4-bit switch, the reproduced 4-bit write wedge with clean and unproven baseline-based recovery, the Stage-H16 content-only pass and divergence, ignored CMD14 pattern-CRC and content-divergence reports, field-wise width-bound failure, identification and full-probe reports (1093, 788 and 1493 bytes), thirteen width-bound materializations, two width-fixture narrowing controls, two full-probe block-read failure controls, the stage-A GPT gate and 21-sector array content phase, four stage-A loop mutation controls (duplicate LBA, continued error, block-count off-by-one, wrong last LBA), the D10 F2 short+long CMD18 success, middle/final-sector mismatch, reordered and checksum-preserving corruption, both short-request sector divergences, mid-transfer readiness stall, R1-error, block-0 and LBA-2 recovery transport/byte failures, the controlled acceptance seam, the mid-transfer failure, stop-timeout and bare-argument refusal cases, the post-bound drain TC/late-BRR/error/exhaustion cases with the `drain=` report field, a missing CMD18 Multi/Single Block Select bit, the Stage-B1 4-bit/8-bit success, stuck-width, zero-readback, host-width-stuck, 8-bit-stuck, bus-test-failure and content-divergence reports, the Stage-B1C success, continuation-pass, late-commit, continuation bus-test failure, continuation content mismatch and content/re-read errors, CMD6-error, empty-readback and lost-host-width reports, the B1 field-wise width bound with `bc`/`e183a`/`esa`, the Stage-B2 pass, ignored byte-185, busy-RCG, content-divergence, content-read, revert-failure, recovery-failure and pre-read scenarios, the TOUTVAL-only arm artifact, the Stage-H9 no-retry timeline mapping-failure, clock-failure, pre-CMD3 CMD1 failure, pre-CMD3 CMD2 failure, clean, timeout, status, error, later-failure, between-sample completion, at-sample completion, response-update-without-CC, no-update, preexisting-R1, identical-response, multiple-change, last-sample, terminal-only, late-CC, index-error, simultaneous-CC-and-error, in-sample error, post-read error, inhibit and stalled-counter `H9T` reports, the Stage-H10 pass, HS-ignored, busy-RCG, width-ignored, bus-test-timeout, bus-test-mismatch, content-divergence, content-read, width-revert-failure, HS-revert-failure and recovery-read reports, the Stage-H11 honored-pass, W4-replay, W4-mismatch, W8-divergence, W8-error, W4-anomaly, clock-busy, bus-test-timeout, bus-test-mismatch, content-read, content-divergence, width-revert-failure, HS-revert-failure and recovery-read reports, the Stage-H12 refused-pass, honored, DDR-error, DDR-paradox, DDR-empty, HS-ignored, clock-busy, width-revert-failure, HS-revert-failure, recovery-read and pre-read-failure reports, the Stage-H13 pass, reset-stuck, inhibit, mismatch, read-timeout, read-CRC, read-status, write-CRC, normal-write and recovery-read reports, the Stage-H14 pass, reset-stuck, inhibit, CMD12-silent, CMD12-CRC, CMD13-failure, mismatch, read-CRC, recovery-read, write-CRC and normal-write reports, and the oversized-report rejection control |
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
| F5 P5-2 device qualification (2026-11-05) | MEASURED PASS FOR ONE REPORTED BOOT: the first P5-2 BOOT-only run (`b509c511…`, block `ff447a48…`, payload `7b2ac8c0…`, boot `4a635cae…`) published the whole `IOMedia` but the report client was not open, so the media-path read failed; that image and failure remain in `out/gta4l/ram-root-emmc-block-p5-evidence/` and are not relabeled. The D9.4 correction opens the media before the report read (`media->open(this, 0, kIOStorageAccessReader)`), closes it exactly once on every exit and records the access mode; the host seam starts closed and adds the unopened-read and balanced-open checks, and the block suite is 23 core scenarios/18 mutation controls plus 32 adapter scenarios (was 28). The corrected p5b candidate is kernel `2f8238b9…` (51,187,712 bytes; re-audited with interface audit `1f456fec…`, provenance `cdfe6950…`, 190 slots, 26 concrete overrides, 0 unresolved original imports), block archive `6d36826a…` (350,248 bytes), payload `47b9593d…` (9/9 contract, exactly one `gta4l-emmc-block=1`, no probe token) and boot image `e256b27d…` (18/18 gate, only the ramdisk differs from `ce4d76e8…`); the packaged payload kernel is `26af8293…`. It was flashed BOOT only to p69 (pre `21f3b5f2…`, post `f1954fd4…`, unchanged tail `a28b4ea2…`; RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…` unchanged; battery 100%; no host reboot). The owner-supplied report is `rc=0 stop=0 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0 clk=400000`, whole media `whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0`, driver `online=1 wp=1 reads=10 blocks=30 fails=0 max=8`, media-path read `lba=1 attempted=1 ok=1 bytes=512 sum=2228245566 base=2228245566 same=1`, all-zero failure record and the intentional report panic at `Gta4lEmmcBlock.cpp:914`. **F5 is measured PASS at 1-bit SDR, 400 kHz `[C]`**; the boot ordinal and cold/warm class were not supplied, and the owner confirmed the tablet returned to TWRP on 2026-11-05. The `count=80 expected=84` partition observation was audited separately (row below): `count=80` matches the 80 populated GPT entries and `expected=84` is the entry-array slot count, so no missing-partition or counting defect was found and partition completeness is not claimed from the report alone. Evidence: `out/gta4l/ram-root-emmc-block-p5b-evidence/` (`flash-record.json`, `owner-report.txt`, `device-run-record.json`, `build-provenance.json`, `kernel-string-verification.txt`). |
| F5 partition accounting audit (2026-11-05) | HOST-ONLY AUDIT, NO DEFECT FOUND: re-used the validated recovery GPT capture (`docs/emmc-reference.md`: 84 entries of 128 bytes, 80 nonempty partitions, no overlaps, identical primary/backup arrays; primary `98bcfd9a…`, backup `12f80350…`) and `gpt-report-verified.json` (`partition_count=80`, `partitions_equal=true`), and inspected `IOGUIDPartitionScheme::scan()` and `Gta4lEmmcBlock::countPartitions()`. The scheme skips null-type entries and publishes one non-whole `IOMedia` per used entry; the reporter counts non-whole media whose provider chain reaches the driver. The p5b report's `count=80` matches the 80 populated entries and its first three children match GPT entries 1-3 (bases 4,194,304, 8,388,608 and 12,582,912 bytes, each 4,194,304 bytes). `expected=84` is the entry-array slot count, not a populated count; there is no missing-partition or counting defect. Partition completeness is not claimed from the report alone, the accepted p5b artifacts are unchanged, and no reporter/test correction is proposed. The owner confirmed the tablet returned to TWRP on 2026-11-05. |
| eMMC Stage-H14 rung-2 forced post-write drain host batch (2026-11-04, paused by the protocol correction) | HISTORICAL: `gta4l-emmc-probe=14` keeps the stage-13 form (standard identification once, the stage-A pass at 400 kHz/1-bit, one 1-bit CMD19 write, one bounded SDHCI data-circuit reset with the self-clear poll, the post-reset `PRESENT_STATE`/`DAT_INHIBIT` sample) and inserts one forced CMD12 `STOP_TRANSMISSION` (R1b) plus one CMD13 `SEND_STATUS` (R1) between the reset and the record-only CMD14/Q reads, then emits the `GTA4L-EMMC H14 REPORT`; it always stops with 0/10/11/12/13/14/15/16/17 and issues no CMD6, CMD18, CMD25 or user-data write and changes no card width or timing. The drain is local to `gta4l_emmc_post_write_drain()`; the CMD12 entry wait applies the SDHCI stop-command `DAT_INHIBIT` exemption and `TIMEOUT_CONTROL` is untouched. Probe 151/77 (was 139/69) with eleven scenarios plus the twelfth width materialization `report-width-h14`; core 168/104 (was 160/98) with eight scenarios and six mutation controls; the wrapper/payload gate accept `emmc-probe-14`. No build, flash or boot; F3 remains `[U]` |
| eMMC D10.5 F2 device run (2026-11-05, owner-reported first cold boot) | RECORDED: the dirty-tree candidate (kernel `bb22f6ca…` 51,187,712 bytes, packaged kernel `2d87a4be…`, payload `93110d8c…` 8/8, BOOT image `bcc93a47…` 18/18) returned `rc=0 step=19 log=55 mb=1 bw=0`. The short request `s=0/2/2/2/0/0/0003/0000/00000b00` and the long request `l=0/21/21/21/0/0/0003/0000/00000b00` completed and byte-validated every sector with clean explicit CMD12 stops (`NORMAL=0x0003`, `ERROR=0x0000`); `rec=0/1/0`, `mbsame=1` and `drain=0/0/0/0`. The `rc=0` path is gated by the D10.4a acceptance barriers. **F2 is measured PASS for that one cold boot at 1-bit SDR, 400 kHz `[C]`**; repeatability, cold-start reliability, performance and multi-block publication remain `[U]`. The owner confirmed TWRP recovery after the boot. Evidence: `out/gta4l/ram-root-emmc-mb-d10.5-closeout-evidence/` and the unchanged 94-file `out/gta4l/ram-root-emmc-mb-d10.5-evidence/` tree. |
| eMMC D12.5 stage-9 no-retry CMD3 timeline host batch (2026-11-06) | HOST-ONLY: `gta4l-emmc-probe=9` keeps its selector but now means a versioned no-retry identification capture (`GTA4L_EMMC_CMD3_TIMELINE_VERSION` 1, 19 scheduled slots plus one reserved terminal slot, fixed host storage). Stage 9 sets `cmd3_retry_enabled = 0`, enables the timeline, and keeps `identify_only`; the duplicate-CMD3 seam remains only for direct core regression tests. The added budget per diagnostic CMD3 is at most 4 pre-command `RESPONSE0..3` reads plus 19 scheduled `RESPONSE0` reads and 20 counter reads, with no added status reads, sleeps, resets, retries or commands. `GTA4L-EMMC H9T REPORT ver=1` carries the header, `C3P`/`C3L`/`C3T`, `TRC` and a fixed `T00..T19` trace; the clean materialization is 1658 bytes and the widest field-wise form is 1799 bytes (margin 248) inside the 2048-byte budget. Stops are 0/14/15; historical retry stops 10-13 are unreachable. Host suites pass core 187/115 and probe 196/100 with 13 width materializations plus the oversized-report control; the probe rows cover mapping, clock, pre-CMD3 CMD1 and CMD2 failures plus clean, completion-silent, R1 status, transport error, later failure, between/at-schedule completion, response update without CC, no update, preexisting R1, identical response, multiple changes, last-sample and terminal-only changes, late CC, index error, simultaneous CC+error, in-sample and post-read errors, inhibit and a stalled counter; the core adds shortened-budget and forced-saturation boundary variants, a stale-CC clear-order case, a capacity-boundary store fixture and retry-coupling, unguarded-capture, extended-poll, capacity-overflow, capacity-guard-removal and pre-clear-retime mutation gates; the probe adds extra-status-read, late-CC-promotion and overflow-field gates. The TOML parse, `check-gta4l.py`, `git diff --check`, privacy scan and the full 23-script battery pass. No candidate build, image, flash, boot or device access; F2 and p5b F5 verdicts are unchanged and reliability remains unresolved. Evidence: `out/gta4l/ram-root-emmc-d12.5-cmd3-timeline/`. |
| eMMC D12.6 panic-format observation (2026-11-06) | RECORDED: one owner-supplied H9T report from the first post-flash cold boot of BOOT image `7c76c7eb…` (18/18 gate, payload `e384cc06…`, packaged kernel `12385bc7…`), flashed BOOT/p69 only with prefix match, protected partitions matching the pre-flash values and a saved rollback. Header `ver=1 rc=0 stop=0 sub=1 att=1 tr=0 r1=0 retry=0/0`, 25-iteration poll with zero-based terminal 24, kind `C`, NORMAL `0001`, RESPONSE0 `00000500`, no timeout capture, `TRC n=6` at 0/1/2/4/8/16, zero FAIL validity. The kind column renders `S/?/C`, accepting the formatter correction for this report; no exact timestamp or photo bytes were supplied, scheduled samples remain interval-bounded and reliability remains unresolved. F2 and p5b F5 are unchanged. Evidence: `docs/emmc-d12.6-panic-format-observation-2026-11-06.md`. |
| eMMC D13 readiness (2026-11-06) | HOST-ONLY DESIGN: `docs/emmc-d13-readiness-2026-11-06.md` grounds the current one-sector adapter, portable core and diagnostic CMD18/CMD12 API; proposes a conservative two-sector CMD18 chunk plus CMD17 tail contract with a private bounded bounce, explicit transport/completed/stop results, exact-count plus clean-CMD12 delivery, first-failure preservation and fail-closed no-retry behavior; records the open selector and request-level timing decisions and enumerates the D13.2 host scenarios/negative controls while preserving core 23/18, adapter 32 and diagnostic 18 `multi-*`/12 multi-block mutations. The storage-ABI suite still fails closed on the stale p5b candidate/provenance and was not regenerated. No implementation, build, QEMU, device access, commit or push. |
| eMMC D13 N3 host implementation (2026-11-06) | HOST-ONLY: adds `gta4l-emmc-block=3` chunked report mode. The portable helper's optional multi-read callback returns transport, requested/completed sectors and stop attempted/result; the adapter owns a 1024-byte bounce and copies a two-sector CMD18 chunk only after exact count and a clean mandatory CMD12. `=1` stays CMD17 and `=2` remains refused. `gta4l_emmc_read_multi_result()` snapshots the existing diagnostic `multi_*` fields without changing `gta4l_emmc_read_multi()` semantics; the P5 report adds `mode=3` and `req/c17/c18/c18b/cfail/stops/sfail/last_stop` inside the 2048-byte bound. The block suite passes 48 core scenarios/38 mutation controls, 55 adapter scenarios and 3 adapter-source mutations, and `emmc-block-3` maps end-to-end through the diagnostic builder, checker, packager and tests. The N3 corrective host batch then fixed the multi-result stop-failure accounting, wired the callback result contract and added the mode-3 controlled qualification reporter: a three-block reference read, intentional two- and three-block IOMedia reads through the adapter, exact-byte comparison (including a sum-preserving corruption control), CMD18/CMD12 and odd-tail accounting, incidental-attribution exclusion, a bounded watchdog snapshot and terminal-race suppression. Host acceptance is complete for the intentional mode-3 path; it is not hardware or initialization-reliability acceptance. The 64-sector/two-sector synchronous gate-hold bound (~128-144 s pathological) is a recorded host-development risk; no timeout, ceiling, watchdog scope or device result changed. No build, QEMU, device access, flash, boot, commit or push; F2/p5b F5 are unchanged and N4 requires separate approval. Evidence: `out/gta4l/ram-root-emmc-d13-n3-chunk-evidence/` and `out/gta4l/ram-root-emmc-n3-corrective-evidence/`. |
| eMMC N5 mode-3 device observation (2026-11-07) | RECORDED: one owner-classified cold boot of the N4 BOOT image `54cdbfb6…` (85,688,320 bytes, 18/18 gate, payload `4a400ca8…` 9/9, packaged kernel `2e6d91a0…`) after a BOOT-only p69 flash with prefix and protected-partition verification. Header `rc=0 stop=0 mode=3 blocks=61071360 bs=512 bytes=31268536320 wid=0 tim=0 clk=400000`; read-only whole media; LBA 1 read matching the same-boot baseline (`sum=2228245566 same=1`). One two-block LBA 2 IOMedia read delivered 1,024 exact bytes through one two-sector CMD18 chunk and one clean CMD12 (`two_c18=1 two_c17=0 two_stops=1 two_sfail=0`); one three-block read delivered 1,536 exact bytes through one CMD18 chunk, one clean CMD12 and one CMD17 odd tail (`c18=1 c17=1 stops=1 sfail=0`). The `count=22 expected=84 waited_ms=0` partition snapshot is an unsynchronized mid-publication observation, not a completeness verdict. Acceptance is limited to those two requests on that one cold boot; repeatability, initialization reliability, wider/faster modes, arbitrary I/O, mount/root switch, DMA/CQE/ICE and partition completeness remain `[U]`. The one-boot budget is consumed. Evidence: `docs/emmc-n5-integration-observation-2026-11-07.md` and `out/gta4l/ram-root-emmc-n5-hardware-evidence/`. |
| eMMC N6 reliability observation (2026-11-07) | RECORDED: the unchanged D12.6 stage-9 BOOT image `7c76c7eb…` (85,688,320 bytes, 18/18 gate, payload `e384cc06…`, packaged kernel `12385bc7…`) was re-flashed BOOT only to p69 after backing up the N5 image `22bea393…` as `boot-before-n6.img.xz` (`5eaaa423…`); post-flash full `b19c59b8…`, prefix `7c76c7eb…`, tail `a28b4ea2…`, protected partitions unchanged. One owner-classified cold boot returned `rc=0 stop=0 sub=1 att=1 tr=0 r1=0 retry=0/0` with a 25-iteration CMD3 poll, terminal iteration 24, kind `C`, NORMAL `0001`, ERROR `0000`, RESPONSE0 `00000500`, ticks `0x14f2`, no timeout capture and an all-zero failure record. The typed C3L poll ticks are normalized to `0x14e4` (about 278.54 us) from the source format and the prior D12.6 report; poll and terminal timings are distinct sequential measurements. Classified as a clean control/non-reproduction: a second clean cold-boot control of the same candidate, not a continuous two-boot trial, failure-rate estimate, fix or proof of reliable initialization. Initialization reliability remains `[U]`; F2, p5b/F5 and N5 are unchanged; the N6 boot budget is consumed. Evidence: `docs/emmc-n6-reliability-observation-2026-11-07.md` and `out/gta4l/ram-root-emmc-n6-reliability-evidence/`. |
| eMMC N8 partition-observation design (2026-11-07) | HOST-ONLY DESIGN: `docs/emmc-n8-partition-observation-design-2026-11-07.md` records that N5's `count=22 expected=84 waited_ms=0` is an unsynchronized mid-publication snapshot because `countPartitions()` stops at the first nonzero count and immediately re-samples, and `expected` is the 84-slot GPT header count, not the 80 populated entries. It chooses an opt-in `gta4l-emmc-partobs=1` token orthogonal to `gta4l-emmc-block=1|3`; the legacy mode-1 and mode-3 reporter is unchanged when the token is absent. It proposes a 4,000 ms deadline, a 50 ms interval and a 250 ms count-unchanged window in the report thread only, and a `PARTOBS` line with first/final counts, nominal times, changes, stability, timeout, none, error, slots and an explicit `complete=0`. A stable count is not completeness; no completeness claim is made without an independent validated GPT-to-`IOMedia` range/count comparison. Iterator failure keeps `stop=15`; deadline and zero count are not failures; the worst-case path is 24 s against the 30 s watchdog. Host scenarios and mutation controls are specified. No code, build, QEMU, device access, flash, boot, commit or push; F2, p5b/F5, N5 and N6 are unchanged and initialization reliability remains `[U]`. |
| eMMC N8.2 host implementation (2026-11-07) | HOST-ONLY: implements the opt-in bounded partition-publication observation. `gta4l_emmc_block_partobs_select()` accepts `gta4l-emmc-partobs=1` only with exactly one block-mode 1/3 token and refuses an orphan, duplicate or non-1 value; the legacy `PART` path is byte-for-byte unchanged when the token is absent. The bounded `PARTOBS` line reports samples, first/first-nonzero/final counts and nominal times, changes, stability, timeout, none, error, the 84 GPT slot count and an explicit `complete=0`; a stable partial count is not presented as complete, and the first registry-walk failure latches `stop=15`. Sampling runs in the report thread with 4,000 ms/50 ms/250 ms budgets, never under the controller gate, and the 30,000 ms watchdog stays above the 24,000 ms worst case. The diagnostic payload builder maps `emmc-block-1-partobs` and `emmc-block-3-partobs` to two kernel arguments; the payload checker refuses an orphan, duplicate or non-1 partobs token and a mismatched personality, and the packager accepts both names. Host results: core 48 scenarios/38 mutation controls (unchanged), adapter 64 scenarios (nine partobs) with 12 adapter mutation controls (nine partobs), three policy mutation controls and 22 diagnostic payload checks, all passing; the packager and payload-checker self-tests also pass. No candidate build, QEMU run, device access, flash or boot; N8.3 candidate production and N8.4 hardware observation need separate approvals, and initialization reliability remains `[U]`. |
| eMMC N8.3 candidate production (2026-11-07) | HOST-ONLY: builds and gates the opt-in `gta4l-emmc-partobs=1` candidate without device access, flash, boot, QEMU device run, commit or push. Prelinked kernel `4dcbf24d…` (51,224,576 bytes; `kernel.json` `0481cbab…`), linked archive `f9f83526…` (512,896 bytes, 190 method slots, 181 kernel bindings, 26 concrete overrides, 0 unresolved original imports, `runtime_qualified=false`, `personality_enabled=true`), candidate storage-ABI audit `out/gta4l/storage-abi-4dcbf24d` (`a30cfcde…`, provenance `a806c6cf…`, snapshot `222f6322…`, run `85307464…`), packaged kernel `023c8ff8…`, payload `5f6759f2…` (83,886,080 bytes, 11/11 contract, `emmc-block-3-partobs` mapping to `gta4l-emmc-block=3 gta4l-emmc-partobs=1`) and BOOT image `3d7e71af…` (85,688,320 bytes, 18/18 gate, only the ramdisk differs from the `ce4d76e8…` v5 baseline). The kernel-build archive step produced the `3e76771d…` container variant; the requalified `f9f83526…` archive has byte-identical members. Block suite 48 core scenarios/38 mutation controls and 64 adapter scenarios with 12 adapter mutation controls plus three policy mutation controls; storage-ABI positive and negative checks PASS; the token scan preserves the `complete=0` non-claim. The closeout is `n8-closeout.txt` and the read-only proposal is `n8-hardware-proposal.txt` in `out/gta4l/ram-root-emmc-n8-candidate-evidence/`. N8.4 is a separate BOOT-only approval; initialization reliability remains `[U]`. |
| eMMC N8.4 first boot and hold-fixture correction (2026-11-07) | RECORDED + HOST-ONLY FIX: the N8.3 BOOT image `3d7e71af…` was flashed to p69 only after the pre-flash p69 `b19c59b8…` was backed up and verified as `boot-before-n8.img.xz` (`8b37f41c…`); the full p69 readback `7c22e499…` matched candidate prefix + unchanged tail `a28b4ea2…`, and RECOVERY/EFS/sec_efs were unchanged. One owner-controlled first cold boot returned the userspace `RAMROOT-FST PASS` panic and no eMMC report, so the partition observation is inconclusive and the boot budget is consumed; the fixture's intentional early panic can preempt the independent eMMC report thread. The additive `fs-timer-hold` fixture runs the same checks and holds in a one-second poll instead of panicking, with failure paths still panicking; the original `fs-timer` is byte-identical (`user.o` `6984333d…`, signed `b316e44f…`, root `77d5eba0…`) and the hold build is `e6546715…` (root `327ac826…`, trust cache `30a72f78…`, nine verified pages). The payload builder now requires `--data-ramdisk-manifest` naming `fs-timer-hold` for both partobs personalities, records the fixture diagnostic/hold state, and rejects the old early-panic fixture; host tests cover the original hash, hold loop, retained failure panic and packaging rejections. No new candidate build, flash or boot; a new candidate, BOOT-only flash and one boot remain separately gated, and initialization reliability remains `[U]`. |
| eMMC N8.5 hold-fixture candidate, flash and observation (2026-11-07) | RECORDED + HOST-ONLY BUILD: rebuilt the partobs payload/BOOT with the additive `fs-timer-hold` fixture and flashed BOOT-only to p69. The unchanged N8.3 kernel `4dcbf24d…` and archive `f9f83526…` were reused; hold root `327ac826…`, hold trust cache `30a72f78…`, signed fixture `e6546715…` (nine verified pages) and `fixture.json` `00363681…` (`diagnostic=fs-timer-hold`, `hold_after_success=true`); the original `fs-timer` is byte-identical (`user.o` `6984333d…`, signed `b316e44f…`, root `77d5eba0…`, `fixture.json` `37cbdd0b…`). New payload `28eafd24…` (11/11 contract) and BOOT image `b4f4f1ea…` (18/18 gate, only ramdisk differs from `ce4d76e8…`); packaged kernel `023c8ff8…`. The payload manifest records the hold fixture path/hash/diagnostic/hold state. Pre-flash p69 `7c22e499…` was backed up and verified as `boot-before-n8.5.img.xz` (`e5b43228…`); full p69 readback `9d51fe80…` matches candidate prefix + unchanged tail `a28b4ea2…`, and RECOVERY/EFS/sec_efs are unchanged. A device disconnect after the backup and before any write was handled by restarting adb, re-reading p69 unchanged and writing once. One owner-classified first cold boot then returned the clean mode-3 report and TWRP: whole read-only media (base 0, size 31,268,536,320, `GUID_partition_scheme`, `writable=0`), LBA 1 baseline match (`sum=2228245566 same=1`), exact two- and three-block reads through two-sector CMD18 chunks with clean CMD12 stops and a CMD17 odd tail, and `PARTOBS samples=8 first=39 final=80 final_ms=350 changes=2 stable=1 stable_ms=250 deadline_ms=4000 timed_out=0 none=0 error=0 slots=84 reported=3 complete=0`. The first sample was already 39; the intermediate count is unknown. `stable=1` is a count-held window, not completeness; `final=80` is consistent with the validated GPT's 80 populated entries among 84 slots, but a count is not an exact GPT-to-`IOMedia` comparison and `complete=0` remains explicit. The boot budget is consumed, no driver fix is indicated, and initialization reliability remains `[U]`. Raw evidence is in `out/gta4l/ram-root-emmc-n8.5-hardware-evidence/observation-1/` (raw transcript `240ab0dd…`, interpretation `0bea53a6…`, observation record `92852a40…`) with a separate non-self-hashing manifest; the original flash record and manifest are unchanged. The N9.1 design is `docs/emmc-n9.1-gpt-iomedia-comparison-design-2026-11-07.md`. |
| eMMC N9.6 exact-comparator observation (2026-11-07) | RECORDED: BOOT-only p69 flash of the N9.5 corrected candidate (BOOT `45f3bb81…`, payload `79ac3a10…`, prelinked kernel `a610e9d1…`, packaged kernel `a17c71dd…`, source `b7aeff0a…`) with full p69 readback `9c5bcf2c…` (prefix `45f3bb81…` + unchanged tail `a28b4ea2…`), unchanged RECOVERY/EFS/sec_efs and saved rollback `boot-before-n9.6.img.xz` (`baddb107…` -> raw `8d5042d3…`). One owner-controlled cold boot returned `rc=0 stop=0 mode=3` with the real whole media selected (`whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0`), confirming the absent-`Base` correction, but `PARTMAP gpt=0 reason=10 samples=0 reads=0 blocks=0 fails=0 cancel=0 complete=0` stopped at the GUID-scheme lookup before any GPT read or comparison. DRV 10 reads/29 blocks, fails=0, max=8 and XFER req=29/c17=7/c18=11/c18b=22/cfail=0/stops=11/sfail=0 are incidental scan requests. The supplied panic suffix `@Gta41EmmcBlock.cpp:1991` is the intentional report stop. The owner returned to TWRP; the boot budget is consumed and no completeness or reliability claim is made. Raw evidence is in `out/gta4l/ram-root-emmc-n9.6-hardware-evidence/observation-1/` (raw `440b0d44…`, interpretation `57221ca8…`, record `87c45795…`) with a separate non-self-hashing manifest binding `evidence-sha256.txt` `423923e7…`. |
| eMMC N9.7 GUID-scheme seam investigation (2026-11-07) | HOST-ONLY: traces the N9.6 `reason=10` to registration visibility. `getMatchingServices()` uses `copyExistingServices(matching, kIOServiceMatchedState)`; `instanceMatch()` requires that state, which only `registerService()`/`doServiceMatch()` sets. Pinned IOStorageFamily `67f9003c` `IOGUIDPartitionScheme::start()` registers only partition media and never the scheme; the scheme is still a direct service-plane child of the whole media (`IOService::startCandidate()` attaches before probe/start) with its `UUID` set by `scan()`, and its `Content Mask` is why the whole media carries `GUID_partition_scheme`. The alternative reader-open guard is eliminated: `IOGUIDPartitionScheme::scan()` opens/closes the media as a reader and pinned `IOMedia::handleOpen()` sums reader levels. The host seam now filters `getMatchingServices()` by registration state and adds `partcmp-scheme-{absent,unregistered,no-uuid,wrong-provider,duplicate}` plus `partcmp-open-refused`, pinning the hardware-identical pre-read signature; the block suite passes 48 core scenarios/38 mutation controls and 93 adapter scenarios with 16 adapter and eight policy mutation controls. The proposed smallest correction for owner approval is to enumerate the selected whole media's direct service-plane children while keeping the class, provider, `UUID` and exactly-one guards. No driver correction, candidate build, ABI/QEMU audit, device access, flash, boot, commit or push was performed. Findings: `docs/emmc-n9.6-partcmp-observation-2026-11-07.md` and `docs/emmc-driver.md` §5.3.62-§5.3.63. |
| eMMC N9.7 GUID-scheme correction (2026-11-07) | HOST-ONLY: implements the owner-approved smallest correction. `findGuidScheme()` now enumerates the selected whole media's direct service-plane children with `whole->getChildIterator(gIOServicePlane)` instead of `getMatchingServices(kIOServiceMatchedState)`; the direct-provider, `UUID` and exactly-one guards and the discovery wait are unchanged, and the four remaining matching walks are media-only. The class guard now matches the registered class by name with `object->metaCast("IOGUIDPartitionScheme")` because a direct `OSDynamicCast` failed the kernel link with an undefined `IOGUIDPartitionScheme::metaClass`; the pinned metaclass is not exported. In the pinned headers `getChildIterator()` returns `OSPtr<OSIterator>`, which is a raw `OSIterator *` because `IOKIT_ENABLE_SHARED_PTR` is not defined. The host seam now tracks direct children, exposes `gIOServicePlane`, models `IOGUIDPartitionScheme`, and attaches an unregistered scheme as a child without registering it. `partcmp-scheme-unregistered` is inverted to require the full successful comparison (`gpt=1 reason=0 slots=84 used=80 media=80 match=1 complete=1`) and asserts the scheme never registered; the new `partcmp-scheme-wrong-class` control rejects a plain `IOService` child with a `UUID`; `partcmp-scheme-{absent,no-uuid,wrong-provider,duplicate}` and `partcmp-open-refused` are retained. Adapter mutation controls grow to 18 with the class-guard and exactly-one regressions; policy controls stay at eight. The block suite passes 48 core scenarios/38 mutation controls and 94 adapter scenarios with 18 adapter and eight policy mutation controls; diagnostic payload 28, packager 11, evidence-count core 187/115 and probe 196/100, RAM-root recipe and board checks pass. The corrected candidate production is recorded in the next row; the correction is not hardware-verified and N9.6's `complete=0` is not upgraded. Findings: `docs/emmc-driver.md` §5.3.64-§5.3.65. |
| eMMC N9.7 corrected candidate production (2026-11-07) | HOST-ONLY: builds and gates the corrected GUID-scheme candidate without device access, flash, boot, commit or push. Prelinked kernel `06826fc5…` (51,257,344 bytes; plan `c9a11f43…`), kernel-build archive `d8234e69…` with `Gta4lEmmcBlock.cpp` `0bc83206…` compiled to `Gta4lEmmcBlockCpp.o` `19f0ba8b…` and the policy object unchanged at `fb94f96f…`; candidate storage-ABI audit `out/gta4l/storage-abi-06826fc5` (`gdb-stop.json` `0c1f6e34…`, `interface-audit.json` `e70c0503…`, `provenance.json` `1ad75463…`, `run.json` `56e469d4…`; 190 slots, 181 kernel bindings, 26 concrete overrides, 0 unresolved original imports, `runtime_qualified=false`); requalified archive `6a6af596…` with byte-identical members; packaged kernel `61b9cea0…`; payload `79c83d67…` (83,886,080 bytes; manifest `1bdd7051…`; 14/14 contract; `emmc-block-3-partcmp` mapping to `gta4l-emmc-block=3 gta4l-emmc-partcmp=1`; hold root `327ac826…`, fixture `00363681…`, trust cache `30a72f78…`); and BOOT image `74a4c687…` (85,688,320 bytes; manifest `788d5008…`; gate `bc2c0281…`, 18/18, only the ramdisk differs from the `ce4d76e8…` v5 baseline; baseline kernel section `a4b054e0…`, firmware `1e8a3132…` and DTB `dc4c719d…` unchanged). Block suite 48 core scenarios/38 mutation controls and 94 adapter scenarios with 18 adapter and eight policy mutation controls; payload 28, packager 11, storage-ABI, evidence-count, fixture recipe and board checks pass. No flash, boot, device access, commit or push; N9.6's `complete=0` is not upgraded and initialization reliability remains `[U]`. Findings: `docs/emmc-driver.md` §5.3.64-§5.3.65. |
| eMMC N9.7 corrected-candidate observation (2026-11-07) | RECORDED: BOOT-only p69 flash of the N9.7 corrected GUID-scheme candidate (BOOT `74a4c687…`, payload `79c83d67…`, prelinked kernel `06826fc5…`, packaged kernel `61b9cea0…`, source `0bc83206…`) with full p69 readback `aaaf8f03…` (prefix `74a4c687…` + unchanged tail `a28b4ea2…`), unchanged RECOVERY/EFS/sec_efs and saved rollback `boot-before-n9.7.img.xz` (`f7e5ac3c…` -> raw `9c5bcf2c…`). One owner-controlled cold boot returned `rc=0 stop=0 mode=3` with the real whole media selected (`whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0`) and the attached GUID scheme found, but `PARTMAP gpt=0 reason=8 reads=1 blocks=1 fails=1 samples=0 cancel=0 complete=0` stopped at the first comparator LBA 0/512-byte read. The report discards the raw `IOReturn` and actual bytes, so the cause is unresolved; host-only analysis points to `partmapPmbr` at page offset 3,608 in a page-aligned 16 KiB kalloc element, a 488+24-byte page split, and `kIOReturnDMAError` from `breakUpRequest()` under the advertised one-segment limit before the adapter, with physical adjacency and allocation failure unselected. DRV 9 reads/29 blocks, fails=0, max=8 and XFER req=29/c17=7/c18=11/c18b=22/cfail=0/stops=11/sfail=0 are incidental scan requests. The supplied panic suffix `@Gta41EmmcBlock.cpp:2000` is the intentional report stop. The owner returned to TWRP; the boot budget is consumed, N9.6's `complete=0` is not upgraded and no completeness or reliability claim is made. Raw evidence is in `out/gta4l/ram-root-emmc-n9.7-hardware-evidence/observation-1/` (raw `bb984f90…`, interpretation `b38255c7…`, record `21a39d86…`) with a separate non-self-hashing manifest binding `evidence-sha256.txt` `c35f25d3…`. Findings: `docs/emmc-n9.7-partcmp-observation-2026-11-07.md` and `docs/emmc-driver.md` §5.3.66. |
| eMMC N9.7 comparator read-detail host batch (2026-11-07) | HOST-ONLY: adds bounded read diagnostics to the exact-comparator path without changing transport policy. `struct gta4l_emmc_block_report` now carries `partmap_read_state` (0 not attempted, 1 in flight, 2 returned error, 3 short transfer, 4 full completion), `partmap_read_lba`, `partmap_read_blocks`, `partmap_read_bytes`, the raw signed 32-bit `partmap_read_status`, the returned `UInt64` `partmap_read_actual`, and the `IOMemoryDescriptor` length/direction. `readPartmapSectors()` publishes the request under `reportLock` before `media->read`, latches the outcome only for the same LBA/block pair, preserves the first error or short transfer, and emits `GTA4L-EMMC P5 READDETAIL: state=… lba=… blocks=… bytes=… status=… actual=… desc_len=… desc_dir=…` inside the 2048-byte report with a 256-byte line cap. Host coverage pins descriptor-allocation refusal and open refusal (`state=0`, `reason=10`), `kIOReturnDMAError` (`state=2`, `status=3758097098`, `actual=0`), `actual=0`/`511` and a 512-of-1024-byte short transfer (`state=3`), a short transfer after earlier successes, the in-flight watchdog snapshot (`state=1`), and full completion (`state=4`); five adapter mutation controls reject a discarded status, actual, descriptor length, short state or in-flight state, and all existing scheme/comparator/deadline/legacy/partobs controls are retained. Newly executed results: 48 core scenarios/38 mutation controls and 97 adapter scenarios with 23 adapter and eight policy mutation controls. The host seam calls `doAsyncReadWrite` directly and does not qualify the real context/deblock/breakup layers; the hardware `reason=8` cause remains unresolved and the page-straddle and allocation-failure inputs stay unselected. No driver correction, candidate build, ABI/QEMU audit, device access, flash, boot, retry, reset/repair, commit or push; N9.6/N9.7 `complete=0` are not upgraded. A host-acceptance re-check records the N9.4-N9.7 continuation in `docs/gta4l-storage-publication-design.md`, confirms the payload-input inventory has no new block input or personality, and refreshes its hash-table row. Findings: `docs/emmc-driver.md` §5.3.67. Constant note: this row originally recorded the injected status as `0xe00002ca`/`3758097098`; that value is `kIOReturnIOError`, while `kIOReturnDMAError` is `0xe00002d4`/`3758097108`. The N9.8 closeout corrected the fixture and current records; the historical text is retained. |
| eMMC N9.8 read-detail candidate production (2026-11-08) | HOST-ONLY: builds and gates the read-detail-instrumented `emmc-block-3-partcmp` candidate without device access, flash, boot, commit or push. Prelinked kernel `2777bfce…` (51,257,344 bytes; plan `c85c3abd…`), kernel-build archive `176b533b…` with `Gta4lEmmcBlock.c` `f84d1bd2…` compiled to `Gta4lEmmcBlock.o` `0c76b09e…` and `Gta4lEmmcBlock.cpp` `440279b2…` compiled to `Gta4lEmmcBlockCpp.o` `db6d2b74…`; candidate storage-ABI audit `out/gta4l/storage-abi-2777bfce` (`gdb-stop.json` `8a3feec1…`, `interface-audit.json` `9a7845cf…`, `provenance.json` `c89275ad…`, `run.json` `f4430cf1…`; 190 slots, 181 kernel bindings, 26 concrete overrides, 0 unresolved original imports, `runtime_qualified=false`); requalified archive `4a811585…` with byte-identical members; packaged kernel `525fdaa4…`; payload `8d57eb3d…` (83,886,080 bytes; manifest `86e6c447…`; 14/14 contract; `emmc-block-3-partcmp` mapping to `gta4l-emmc-block=3 gta4l-emmc-partcmp=1`; hold root `327ac826…`, fixture `00363681…`, trust cache `30a72f78…`); and BOOT image `1e767155…` (85,688,320 bytes; manifest `969683cd…`; gate `cfa4227f…`, 18/18, only the ramdisk differs from the `ce4d76e8…` v5 baseline; baseline kernel section `a4b054e0…`, firmware `1e8a3132…` and DTB `dc4c719d…` unchanged). The packaged kernel carries one `GTA4L-EMMC P5 PARTMAP` string, three `GTA4L-EMMC P5 REPORT` strings, one `GTA4L-EMMC P5 READDETAIL` string, one `GTA4L-EMMC P5 PARTOBS` string, one legacy `PART:` string, five `gta4l-emmc-partcmp` and five `gta4l-emmc-partobs` strings and no literal `complete=`. Block suite 48 core scenarios/38 mutation controls and 97 adapter scenarios with 23 adapter and eight policy mutation controls; payload 28, packager 11, boot-image, storage-ABI, evidence-count, fixture recipe and board checks pass. No flash, boot, device access, commit or push; the hardware `reason=8` cause remains unresolved, N9.6/N9.7 `complete=0` are not upgraded and initialization reliability remains `[U]`. Findings: `docs/emmc-n9.8-partcmp-readdetail-candidate-2026-11-08.md` and `docs/emmc-driver.md` §5.3.68. |
| eMMC N9.8 read-detail observation and DMA-error trace (2026-11-08) | RECORDED: BOOT-only p69 flash of the N9.8 read-detail candidate (BOOT `1e767155…`, payload `8d57eb3d…`, prelinked kernel `2777bfce…`, packaged kernel `525fdaa4…`, source `440279b2…`, policy `f84d1bd2…`, header `6cb1f7cd…`, archive `4a811585…`) with full p69 readback `cf13acfc…` (prefix `1e767155…` + unchanged tail `a28b4ea2…`), unchanged RECOVERY/EFS/sec_efs and saved rollback `boot-before-n9.8.img.xz` (`19ab50d7…` -> raw `aaaf8f03…`); the flash manifest verifies 5/5. One owner-controlled cold boot returned `rc=0 stop=0 mode=3` with the real whole media selected (`whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0`) and the attached GUID scheme found, but `PARTMAP gpt=0 reason=8 reads=1 blocks=1 fails=1 samples=0 cancel=0 complete=0` and `READDETAIL state=2 lba=0 blocks=1 bytes=512 status=3758097108 actual=0 desc_len=512 desc_dir=1`. `3758097108` is `0xe00002d4` = `kIOReturnDMAError`; the earlier `0xe00002ca`/`3758097098` records are `kIOReturnIOError` and are retained with correction notes. Host trace: the only pre-adapter DMA-error exit in pinned IOStorageFamily `67f9003c` is `IOBlockStorageDriver::breakUpRequest()` (`IOBlockStorageDriver.cpp:3272-3275`) with `getBreakSize()==0` (`:2988` truncation under the advertised one-segment limit, `:2975-2980`); allocation failures return `kIOReturnNoMemory`, so the status selects the breakup path. DWARF on the flashed object `db6d2b74…` keeps `partmapPmbr` at `0x0e18` (3,608) in the 13,992-byte object, the 16 KiB kalloc class and the 488+24 page split. Physical adjacency of the two page segments remains unmeasured; no driver correction is authorized. Host suites pass 48 core/38 mutation and 97 adapter scenarios with 23 adapter and eight policy mutation controls. The owner returned to TWRP; the boot budget is consumed, N9.6/N9.7/N9.8 `complete=0` are preserved and no completeness or reliability claim is made. Raw evidence is in `out/gta4l/ram-root-emmc-n9.8-hardware-evidence/observation-1/` (raw `95ad5e8c…`, interpretation `8bc30296…`, record `81ca711e…`) with a separate non-self-hashing manifest binding `evidence-sha256.txt` `b540ba0e…`. Findings: `docs/emmc-driver.md` §5.3.69. |
| eMMC N9.9 page-aligned comparator read buffer (2026-11-08) | HOST-ONLY: corrects the exact-comparator read path selected by the N9.8 DMA-error trace. `GTA4L_EMMC_BLOCK_PARTMAP_DMA_BYTES` is two blocks (1,024 bytes); comparator mode allocates one page-aligned buffer with `IOMallocAligned(size, PAGE_SIZE)` and frees it with `IOFreeAligned`; `readPartmapSectors()` issues every comparator read of at most 1,024 bytes from it and copies to the caller only after a full successful transfer; the advertised one-segment and request limits and the `READDETAIL` line are unchanged. The host seam now models the worst-case `IOBlockStorageDriver::breakUpRequest()` constraint (one segment per page, `IOTrunc` to the 512-byte request block size, zero break size returns `kIOReturnDMAError`). `partcmp-page-aligned` runs the full 25-read comparator with the model enabled (zero rejects, zero unaligned descriptors, `gpt=1 reason=0 media=80 match=1 complete=1`); `partcmp-page-split` calls `readPartmapSectors()` with a destination at page offset 3,608 and requires the exact PMBR copy; the new `partcmp-bounce-skip` mutation restores the caller buffer and is rejected by that scenario. Block suite 48 core scenarios/38 mutation controls and 99 adapter scenarios with 24 adapter and eight policy mutation controls; the evidence-count guard passes. The seam still calls `doAsyncReadWrite` directly, the breakup model is a host reconstruction, physical adjacency remains unmeasured and the correction is not hardware-verified. No candidate build, storage-ABI/QEMU audit, device access, flash, boot, commit or push; N9.6/N9.7/N9.8 `complete=0` are preserved. Findings: `docs/emmc-n9.9-page-aligned-comparator-buffer-2026-11-08.md` and `docs/emmc-driver.md` §5.3.70. |
| eMMC N9.10 page-aligned comparator candidate production (2026-11-08) | HOST-ONLY: builds and gates the page-aligned exact-comparator candidate without device access, flash or boot. Prelinked kernel `fcc61714…` (51,257,344 bytes; plan `240ea822…`), kernel-build archive `b8557d3e…` with `Gta4lEmmcBlock.c` `f84d1bd2…` compiled to `Gta4lEmmcBlock.o` `bf2c80ce…` and `Gta4lEmmcBlock.cpp` `78144956…` compiled to `Gta4lEmmcBlockCpp.o` `44a5686d…`; candidate storage-ABI audit `out/gta4l/storage-abi-fcc61714` (`gdb-stop.json` `e9e01873…`, `interface-audit.json` `4f66b116…`, `provenance.json` `64e46c60…`, `run.json` `94746dec…`; 190 slots, 181 kernel bindings, 26 concrete overrides, 0 unresolved original imports, `runtime_qualified=false`); requalified archive `0575c413…` with byte-identical members; packaged kernel `149b1e6c…`; payload `800d293d…` (83,886,080 bytes; manifest `c600a8fd…`; 14/14 contract; `emmc-block-3-partcmp` mapping to `gta4l-emmc-block=3 gta4l-emmc-partcmp=1`; hold root `327ac826…`, fixture `00363681…`, trust cache `30a72f78…`); and BOOT image `dc79fab2…` (85,688,320 bytes; manifest `057efbb5…`; gate `3290d179…`, 18/18, only the ramdisk differs from the `ce4d76e8…` v5 baseline; baseline kernel section `a4b054e0…`, firmware `1e8a3132…` and DTB `dc4c719d…` unchanged). The packaged kernel carries one `GTA4L-EMMC P5 PARTMAP` string, three `GTA4L-EMMC P5 REPORT` strings, one `GTA4L-EMMC P5 READDETAIL` string, one `GTA4L-EMMC P5 PARTOBS` string, one legacy `PART:` string, five `gta4l-emmc-partcmp` and five `gta4l-emmc-partobs` strings and no literal `complete=`. Block suite 48 core scenarios/38 mutation controls and 99 adapter scenarios with 24 adapter and eight policy mutation controls; payload, packager, boot-image, storage-ABI, evidence-count, fixture recipe and board checks pass. No flash, boot, device access, commit or push; N9.6/N9.7/N9.8 `complete=0` are preserved and initialization reliability remains `[U]`. Findings: `docs/emmc-n9.10-partcmp-page-aligned-candidate-2026-11-08.md` and `docs/emmc-driver.md` §5.3.71. |
| eMMC N9.10 exact-comparator observation (2026-11-08) | RECORDED: BOOT-only p69 flash of the N9.10 page-aligned exact-comparator candidate (BOOT `dc79fab2…`, payload `800d293d…`, prelinked kernel `fcc61714…`, packaged kernel `149b1e6c…`, source `78144956…`, policy `f84d1bd2…`, header `6c51b5b3…`, archive `0575c413…`) with full p69 readback `004b9d6d…` (prefix `dc79fab2…` + unchanged tail `a28b4ea2…`), unchanged RECOVERY/EFS/sec_efs and saved rollback `boot-before-n9.10.img.xz` (`3c7e9e5f…` -> raw `cf13acfc…`); the flash manifest verifies. One owner-controlled cold boot returned `rc=0 stop=0 mode=3` and selected the real whole media with the attached GUID scheme (`whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0`). `PARTMAP gpt=1 reason=0 slots=84 used=80 pmbr=1 hdr=1 arr=1 bak=1 xcheck=1 media=80 match=1 missing=0 extra=0 dup=0 nested=0 prop=0 samples=9 changes=3 stable=1 stable_ms=250 deadline_ms=2000 timed_out=0 budget=0 reads=25 blocks=45 fails=0 cancel=0 complete=1` is the first hardware `complete=1`: both GPT copies validated and agreed, all 80 qualifying media tuples matched the 80 used entries, the set was stable for 250 ms, and all 25 comparator reads/45 blocks succeeded. The source-derived order is PMBR LBA 0, primary header LBA 1, 11 primary-array reads LBA 2-22, backup header LBA 61,071,359 and 11 backup-array reads LBA 61,071,327-61,071,347 in two-block chunks; `READDETAIL state=4 lba=61071347 blocks=1 bytes=512 status=0 actual=512 desc_len=512 desc_dir=1` is the final backup-array tail read, not an aggregate. `complete=1` is the guard at `package/Gta4lEmmcBlock.cpp:2034-2039` and cannot coexist with a mismatch, timeout, budget refusal, cancellation or read failure. The N9.8 first-read `kIOReturnDMAError` did not recur, which supports the N9.9 page-aligned read path; the N9.8 return site and physical adjacency remain host reconstructions. This is one cold boot and a same-boot completeness result, not an initialization-reliability, mount, write or wider/faster-mode result; N9.6/N9.7/N9.8 `complete=0` are preserved and initialization reliability remains `[U]`. The owner returned to TWRP, the one-boot budget is consumed, and no retry, reflash, rollback, reset/repair, commit or push was performed. Raw evidence is in `out/gta4l/ram-root-emmc-n9.10-hardware-evidence/observation-1/` (raw `2fc677de…`, interpretation `cacab157…`, record `138cbeb7…`) with a separate non-self-hashing manifest `2056f59e…` binding `evidence-sha256.txt` `89f5cc24…`. Findings: `docs/emmc-n9.10-partcmp-observation-2026-11-08.md` and `docs/emmc-driver.md` §5.3.72. |
| eMMC N10.1 read-only IOMedia content qualification candidate production (2026-11-08) | HOST-ONLY: builds and gates the orthogonal `gta4l-emmc-partread=1` read-only content qualification without device access, flash or boot. Prelinked kernel `12e00d9f…` (51,277,824 bytes; plan `83a8b81b…`), kernel-build archive `b137c2e4…` with `Gta4lEmmcBlock.c` `9dad3d79…` compiled to `Gta4lEmmcBlock.o` `2402c895…` and `Gta4lEmmcBlock.cpp` `6a11aa76…` compiled to `Gta4lEmmcBlockCpp.o` `5f72925e…`; candidate storage-ABI audit `out/gta4l/storage-abi-12e00d9f` (`gdb-stop.json` `275d693e…`, `interface-audit.json` `48d8d4a7…`, `provenance.json` `99c15186…`, `run.json` `b9888e50…`; 190 slots, 181 kernel bindings, 26 concrete overrides, 0 unresolved original imports, vtable `0xfffffff005cb44c0`, service metaclass `0xfffffff007af75b8`, `runtime_qualified=false`); requalified archive `5a59d055…` with byte-identical members; packaged kernel `05fde642…`; payload `e2dc899d…` (83,886,080 bytes; manifest `08ff16bf…`; 18/18 contract; `emmc-block-3-partread` mapping to `gta4l-emmc-block=3 gta4l-emmc-partread=1`; hold root `327ac826…`, fixture `00363681…`, trust cache `30a72f78…`); and BOOT image `0cee22c9…` (85,688,320 bytes; manifest `31ce0993…`; gate 18/18, only the ramdisk differs from the `ce4d76e8…` v5 baseline; baseline kernel section `a4b054e0…`, firmware `1e8a3132…` and DTB `dc4c719d…` unchanged). The six pinned ranges are whole LBA 0/34 blocks, whole last 34 blocks, and boot 64/3/2/1-block tails; their digests were re-verified against `lbas-0-33.bin`, `lbas-61071326-61071359.bin`, the retained primary/backup GPT prefixes and the boot-tail ranges of `boot-backup-20261027-pmgr.img`. The packaged kernel carries one `GTA4L-EMMC P5 PARTREAD`, one `READTEST`, one `READFAIL`, one `PARTMAP`, three `REPORT`, one `READDETAIL`, one `PARTOBS`, one legacy `PART:` and six `gta4l-emmc-partread` strings and no literal `complete=`. Block suite 48 core scenarios/38 mutation controls and 114 adapter scenarios with 33 adapter and eight policy mutation controls; payload, packager, boot-image, storage-ABI, evidence-count, fixture recipe and board checks pass. No flash, boot, device access, commit or push while producing the record; the separately approved BOOT-only flash and one owner-reported cold boot were executed later and are recorded in the next row, and they qualify only the six pinned ranges on that boot, not arbitrary I/O, mount, writes, wider/faster modes or initialization reliability. Findings: `docs/emmc-n10.1-partread-candidate-2026-11-08.md` and `docs/emmc-driver.md` §5.3.74. |
| eMMC N10.1 read-only IOMedia content qualification hardware result (2026-11-08) | RECORDED: BOOT-only p69 flash of the N10.1 read-only content-qualification candidate (BOOT `0cee22c9…`, payload `e2dc899d…`, prelinked kernel `12e00d9f…`, packaged kernel `05fde642…`, source `6a11aa76…`, policy `9dad3d79…`, header `d05b982e…`, archive `5a59d055…`) with full p69 readback `cc3c8492…` (prefix `0cee22c9…` + unchanged tail `a28b4ea2…`), unchanged RECOVERY/EFS/sec_efs and saved rollback `boot-before-n10.1.img.xz` (`de7d4d51…` -> raw `004b9d6d…`). One owner-reported cold boot returned `rc=0 stop=0 mode=3` with the read-only whole media (`whole=1 base=0 size=31268536320 content=GUID_partition_scheme writable=0`) and `PARTREAD cases=6 passed=6 failed=0 reason=0 complete=1 reads=6 blocks=138 bytes=70656 fails=0 cancel=0 budget=0 media=0 part=0 cap=61071360`: all six pinned ranges matched their retained SHA-256 digests (whole LBA 0/34 blocks, whole last 34 blocks, boot 64/3/2/1-block tails), `media=0 part=0` are clear failure flags, and `complete=1` is the guard at `package/Gta4lEmmcBlock.cpp:2297-2299`. `READTEST index=5 … off=100662784 … digest=400039fd…` is the final singleton case; `DRV`/`XFER` are whole-boot aggregates that include the six partread reads. The supplied `@Gta41EmmcBlock.cpp:2642` is the intentional terminal report stop. An independent read-only post-boot comparison then matched p69 full/prefix/tail, RECOVERY `14e11370…`, EFS `51fa824c…` and sec_efs `3a265d58…`, and the pulled prefix/extracted payload passed 18/18 boot-image and 18/18 payload checks; it is not a second boot. This qualifies only the six pinned ranges on one cold boot, not arbitrary I/O, mount, writes, wider/faster modes, DMA/CQE/ICE or initialization reliability; N8.5/N9.6/N9.7/N9.8 `complete=0`, the N9.10 comparator result and the N9.11 owner-reported series are preserved. Raw evidence is in `out/gta4l/ram-root-emmc-n10.1-hardware-evidence/` (transcript `2717eb74…` and post-boot record `620d20aa…` in the parent; interpretation `a6a431c6…` and record `a158f74b…` in `observation-1/`) with a separate non-self-hashing manifest `898bbda0…` binding the extended `evidence-sha256.txt` `d2b23ecf…`. Findings: `docs/emmc-n10.1-partread-hardware-result-2026-11-08.md` and `docs/emmc-driver.md` §5.3.75. |
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
