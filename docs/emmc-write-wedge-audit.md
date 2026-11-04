# gta4l eMMC write-phase wedge audit

Status: host-only dissection of the recorded CMD19 write-phase wedge. No build,
flash, boot or device access was performed. The audit decodes the recorded
register sequence against the pinned Linux SDHCI/SDHCI-MSM sources and JEDEC
B1.51, ranks the candidate causes, states what the evidence does not
distinguish, and names the next owner-gated rung. It does not establish a card
defect, a write-path verdict or permission to run a device.

**Correction (2026-11-04):** the rung-1 CMD14 result in the H13 device run
(`trc=-12`) was originally treated as a failed card data conversation. JEDEC
6.6.4 permits an optional pattern CRC16 that the host ignores, and pinned
Linux `sdhci_data_irq()` exempts `MMC_BUS_TEST_R` from its data-CRC failure
path, so that row is inconclusive rather than card-side proof. The core now
tolerates exactly one CMD14 data-CRC indication and still requires the
returned pattern. The 1-bit pattern is the JEDEC `0x80`/two-bit form. The
stage-7 "forced TOUTVAL" experiment wrote bits of `CLOCK_CONTROL`, not the
`TIMEOUT_CONTROL` byte, so its armed-minimum interpretation is withdrawn.
The recommended rung 2 (forced post-write CMD12/CMD13 drain) conflicted with
this document's own row 3 ("CMD12 is not appropriate for a single-block
write"); H14 is paused pending a corrected design. H15 is the verified-width
rung. Its 2026-11-05 cold boot verified the 4-bit SDR switch and then
reproduced the CMD19 write timeout at 4-bit, so the wedge is not a 1-bit-only
effect; the old revert was blocked by the latched `DAT_INHIBIT` before CMD6.
The report/recovery fix and the content-only H16 rung are recorded in
`docs/emmc-driver.md` §5.3.31.

**Resolution (2026-11-05):** the first H16 boot returned `rc=0 stop=0` with the
verified 4-bit switch, the full 4-bit content pass, the baseline-based revert
and the block-0 recovery read, so the 4-bit DAT path is sound for reads and
the wedge is not width-caused. Candidate (e) is adopted as the working
explanation: CMD19 produces no CRC status token, the controller's data timeout
is therefore expected, TI erratum SPRZ436H i836 says to ignore it and continue
to CMD14, and Linux `mmc_bus_test()` discards every `BUS_TEST_W` error and lets
`BUS_TEST_R` decide. The port's defect is the completion rule in
`gta4l_emmc_bus_test_common()`, which returns on the write timeout before the
data reset and CMD14. The next rung keeps the §6 rung-1 sequence (data reset +
CMD14) but runs it at verified 4-bit after the content pass, and adds the
4-bit content and pattern evidence needed for the F3 bus-test clause. See
`docs/emmc-driver.md` §5.3.32.

## 1. What is being explained

The bus-test write phase fails identically at both measured widths:

| Row | Sample | Command state | Data state | Terminal record |
| --- | --- | --- | --- | --- |
| B1C 4-bit continuation | stage-5 boots 3 and 6 | CMD19 R1 accepted, `CC` latched | `BufferWriteReady` latched, no `TC`, no error | `FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/18` at `Gta4lEmmcProbe.c:1085` |
| B1C-1 1-bit discriminator | cold boots 1 and 12 | CMD19 R1 accepted, `CC` latched | `BufferWriteReady` latched, no `TC`, no error | `FAIL fail=1/19/0/0000133a/0000/0011/03f801f6/18` at `Gta4lEmmcProbe.c:1090` |

The terminal tuple is field-for-field identical: `error=0x0000`,
`normal=0x0011` (`CC | BufferWriteReady`), present state `0x03f801f6`,
probe step 18, `rc=-11` (`GTA4L_EMMC_ERR_DATA_TIMEOUT`). The 4-bit row follows
a CMD6 width switch whose effect the card may ignore (the "wire never left
1-bit" model), so the 1-bit row is the clean sample: it issues no CMD6
(`BW=4 att=0`, `bc=0`) and still wedges. The wedge is therefore not caused by
the width switch, the HS timing, or a card-state write.

Two neighbouring observations bound the class but are not the same row:

- Single-block CMD17 reads latch `TC` cleanly (stage-A content pass). Reads
  and writes differ in what completes the data phase.
- The stage-7 8-block CMD18 read also missed `TC`
  (`fail=1/18/2/0000123a/0000/0021/03f80af6/22` in the E1 row; `normal=0x0021`
  = `CC | BufferReadReady`, `error=0`), and the stage-7 DTO row forced
  `TOUTVAL=0` and still saw no `DTO` within a 5 s drain. That row is kept
  separate: its `mb=7` field cannot distinguish "7 of 8 blocks drained with
  the eighth pending" from "all 8 drained with only `TC` missing".

The 8 s write window (`bus_test_window_us = 8000000`) was designed for the
stage-7 E2 repeat of the 1-bit CMD19 write, but E2 never ran: the E1 recovery
read `Q` failed with `ERR_INHIBIT` (`Q rc=-17`) and the probe skipped E2. Every
observed write wedge therefore used the universal 1 s window
(100,000 iterations of the 10 us poll step), not 8 s.

## 2. The recorded write sequence

The core path is `gta4l_emmc_write_data()` (`package/Gta4lEmmc.c:970`). For
the 1-bit and 4-bit cases `length=4`; for 8-bit `length=8`
(`package/Gta4lEmmc.c:1941-1943`). The sequence is:

1. `gta4l_emmc_wait_inhibit()` polls `PRESENT_STATE` until both
   `CMD_INHIBIT` and `DAT_INHIBIT` are clear
   (`package/Gta4lEmmc.c:491-507`).
2. `gta4l_emmc_clear_interrupts()` writes `0xFFFF` to both status registers.
3. `BLOCK_SIZE = length` (`0x04`), `BLOCK_COUNT = 1` (`0x06`),
   `ARGUMENT = 0` (`0x08`).
4. `TRANSFER_MODE = GTA4L_EMMC_TM_BLOCK_COUNT_EN` (`0x0C = 0x0002`).
5. `COMMAND = (19 << 8) | RESP_SHORT | CRC | INDEX | DATA` (`0x0E = 0x133a`).
6. The poll loop reads `ERROR_INT_STATUS` (`0x32`), then
   `NORMAL_INT_STATUS` (`0x30`), latches the response on `CC`, writes the
   block words to `BUFFER_DATA_PORT` (`0x20`) on `BWR`, and accepts success
   only when `CC`, the buffer write and `TC` are all observed
   (`package/Gta4lEmmc.c:1041-1077`).
7. On window expiry it records `error` and `PRESENT_STATE` and returns
   `GTA4L_EMMC_ERR_DATA_TIMEOUT` (`package/Gta4lEmmc.c:1088-1095`).

Decoding the recorded state:

| Field | Value | Decode |
| --- | --- | --- |
| `TRANSFER_MODE` | `0x0002` | `BLK_CNT_EN` only: write direction (no `TM_READ`), single block (no `TM_MULTI`), PIO (no DMA). Matches Linux `sdhci_set_transfer_mode()` for one write block (`sdhci.c:1172-1196`). |
| `COMMAND` | `0x133a` | index 19, short response, CRC check, index check, data present (`Gta4lEmmc.h:165-170`). |
| `BLOCK_SIZE` / `BLOCK_COUNT` | 4 or 8 / 1 | One PIO block; one 32-bit word at 1-bit/4-bit, two words at 8-bit. |
| `normal` | `0x0011` | `CC` (`0x0001`) and `BufferWriteReady` (`0x0010`) latched. The command phase completed and the controller asked for the block. |
| `error` | `0x0000` | No command or data error bit, including `DATA_TIMEOUT` (`0x0010`). |
| `present` | `0x03f801f6` | bit 0 `CMD_INHIBIT=0`; bit 1 `DAT_INHIBIT=1`; bit 2 `DAT_LINE_ACTIVE=1`; the upper bits are line levels. The controller is still in a data transfer, not idle. |

The last row explains the recovery failure: after the write returns, the data
circuit is still active, so the next command fails the inhibit wait. The core
has exactly one software reset, `RESET_ALL` during initialisation
(`package/Gta4lEmmc.c:1480`), and no per-request data reset. Linux does the
opposite: `__sdhci_finish_data_common()` calls
`sdhci_reset_for(REQUEST_ERROR*)` on a data error
(`/tmp/opencode/linux-pin/sdhci.c:1566-1600`), and the reset helpers reset
the data circuit (`sdhci.c:230-300`). The port's missing reset is a host-side
recovery gap and a confound for later rungs; it is not the cause of the first
write wedge.

## 3. What must happen for `TC` on a write

SDHCI write completion is not the same as read completion:

- In PIO the host fills the buffer port while `BufferWriteReady`
  (`SDHCI_SPACE_AVAILABLE`) is set
  (`/tmp/opencode/linux-pin/sdhci.c:615-655`). The controller then sends the
  block and waits for the card's CRC status token before it declares the data
  transfer complete. Linux finishes the request on `SDHCI_INT_DATA_END`
  (`sdhci.c:3531-3541`), and on a data error it resets the data circuit
  (`sdhci.c:1566-1600`).
- The bus-test write is not a normal data write. JEDEC B1.51 §6.6.4
  (PDF page 70) defines CMD19 as a host-to-device pattern: the device ignores
  all but the first two bits of each data line, the minimum pattern is two
  bytes, and an optional CRC16 is ignored by the device. Table 7 and
  §A.6.3/Table A.223 (PDF pages 302-303) show the per-line pattern with the
  optional CRC16 and end bit. The spec does not define a card CRC status
  token for this pattern; the TI erratum states that none is generated.
- External precedent matches that reading. TI erratum SPRZ436H i836 records
  that some MMC controllers wait for a CRC status after CMD19, that the card
  generates none at this step, and that the controller signals Data Timeout;
  its guideline is to ignore the DTO, clear it, and proceed to CMD14. Linux
  goes further: `mmc_bus_test()` ignores errors from `BUS_TEST_W` and lets
  `BUS_TEST_R` decide (`/tmp/opencode/linux-verify/mmc_ops-pinned.c:847-866`,
  "Ignore errors from BUS_TEST_W"), and `sdhci_data_irq()` excludes
  `MMC_BUS_TEST_R` from the data-CRC error path
  (`/tmp/opencode/linux-pin/sdhci.c:3471-3477`).

The timeout arithmetic makes the observed silence expected even if the
controller's counter works. The Qcom MSM driver documents the hardware data
timeout as `4 * MCLK * 2^(count + 13)` and caps the *software* timeout at 22 s
only when `host->clock > 400000`
(`/tmp/opencode/linux-pin/sdhci-msm.c:2213-2227`); it sets
`max_timeout_count = 0xF` (`sdhci-msm.c:2875-2876`). The port writes
`TIMEOUT_CONTROL = 0x0E` at init (`package/Gta4lEmmc.c:1502`,
`Gta4lEmmc.h:83`). At the identification clock of 400 kHz that is about
22 minutes (4 * 2^27 / 400 kHz), far beyond the 1 s window and beyond the
8 s E2 window. At 50 MHz the same count is about 10.7 s. A write waiting for
a CRC status token therefore has no reason to raise `DTO` inside either
observed window.

## 4. Candidate causes

| Candidate | Expected signature | Evidence | Verdict |
| --- | --- | --- | --- |
| (a) The 4-byte CMD19 block is illegal for the card at 1-bit, so the card never returns to `TRAN`/RL/RW. | Command R1 error or a card-side abort; a larger legal block would behave differently. | JEDEC §6.6.4: the minimum pattern is two bytes and the device ignores all but the first two bits, so 4 bytes is legal. The 1-bit row issues no CMD6 and still accepts the command (`CC`) and asks for the block (`BWR`). | Rejected. |
| (b) The controller's `DAT_INHIBIT` bit is what suppresses `TC`. | `DAT_INHIBIT` set before the command; no `BWR`. | `wait_inhibit()` requires both inhibit bits clear before the command; `BWR` latches; `DAT_INHIBIT` sets only after the command, with `DAT_LINE_ACTIVE`. It is a symptom of the unfinished transfer, not an independent suppressor. | Rejected as a cause; kept as a consequence. |
| (c) `TC` for a write needs an enabled data-interrupt or `TRNS_DONE` class the core never unmasks. | `TC` absent because the status bit is gated; enabling the bit would show it. | The core polls the status register directly; `NORMAL_INT_ENABLE = CC | TC | BWR | BRR` and `ERROR_INT_ENABLE = INT_ERR_MASK`; only `SIGNAL_ENABLE` is zero (`package/Gta4lEmmc.c:475-486`). `TC` never sets in the status register. | Rejected. |
| (d) `TC` is late rather than never; the window is too short. | A longer window eventually latches `TC` or `DTO`. | Every observed write wedge used the 1 s window; the 8 s E2 never ran. At 400 kHz `TOUTVAL=0x0E` is about 22 minutes, so 8 s is still short; a longer window alone is weak. Forcing `TOUTVAL=0` before the write is the sharper test. | Live, but subordinate. |
| (e) The card sends no CRC status token for the CMD19 pattern, so the controller waits for a completion that never arrives. | `CC` and `BWR` latch, the block is written, `TC` never latches, `DAT_LINE_ACTIVE` stays set, no error before the window; reads still complete. | JEDEC defines a pattern, not a data write, and no CRC status token; TI i836 records the same controller wait; Linux ignores `BUS_TEST_W` errors and relies on `BUS_TEST_R`; the read/write asymmetry is exactly this shape. | Leading. |
| (f) The controller's completion-status latch fails independently of the CRC status. | `TC` missing even when the data has clearly completed; a read row can miss `TC` too. | The CMD18 row missed `TC` after at least 7 blocks, but `mb=7` cannot prove all 8 arrived; if they did, this is a status-latch failure. It does not explain the CMD19 write alone, but it means "`TC` missing" is not unique to writes. | Live as a separate class; needs a reset/`CMD13` probe. |
| (g) The missing data-circuit reset is the cause. | The first write would complete but later commands fail. | The wedge occurs on the first write, before any later command; the reset gap explains the *persistence* (`Q rc=-17`, E2 skipped), not the first failure. | Not a cause; a confound and a required fix for the next rung. |

Ranking: (e) is the leading mechanism; (d) is a live window question but is
unlikely to be answered by waiting longer at 400 kHz; (f) is a separate
completion-status question; (g) is a host recovery defect that must be fixed
before a clean discriminator can run; (a), (b) and (c) are rejected by the
recorded sequence and the spec.

## 5. What the evidence does not distinguish

- Whether the card emits any CRC status token on CMD19. The audit sees only
  the controller's state; the card side is inferred from JEDEC and the TI
  precedent.
- Whether the controller's data-timeout counter is silent in the write-wait
  state or merely beyond the window. The stage-7 E1 forced `TOUTVAL=0` on a
  read and saw no `DTO` in 5 s, but `mb=7` cannot distinguish a pending eighth
  block from a completed transfer whose `TC` never latched. The write-wait
  state itself has never been tested with `TOUTVAL=0`.
- Whether the 4-bit B1C failure is a true 4-bit write or a 1-bit write in
  disguise. The 1-bit discriminator settles the width question for the wedge,
  not the card's 4-bit state.
- Whether the CMD18 `TC` miss shares the CMD19 mechanism. The read row has no
  CRC status token to wait for, so the shared symptom does not imply a shared
  cause.
- The card's state after the CMD19 write. The R1 response was read, but no
  CMD13 was issued and no post-write state sample exists.

## 6. Decision table and next rung

The §3.1.4 decision row is "write-phase timeout (CC + BWR, no TC, present
`0x03f801f6`) → the write path is broken independent of width". The audit
narrows that row: the write path is not shown to be electrically broken; the
completion signal the driver requires is not the signal CMD19 produces. The
next rung must therefore test a completion rule that matches Linux's bus-test
handling, not another width or timing variant.

| Rung | What it does | What it decides | Rank |
| --- | --- | --- | --- |
| 1. Data reset + CMD14 | After `CC` + `BWR` and the buffer writes, perform a bounded SDHCI data-circuit reset (`SOFTWARE_RESET` data bit, then poll the reset clear), record the post-reset present state, and issue CMD14 with the existing read path and pattern comparison. | If CMD14 returns the expected reversed pattern, the CMD19 write was accepted and the missing `TC` is a completion-signal property, not a broken write path. If CMD14 fails, the bus test itself is inconclusive and rung 2 is the diagnostic. | Recommended. |
| 2. Forced `TOUTVAL=0` + 1-bit CMD19 write | Force `TIMEOUT_CONTROL` to `TOUTVAL=0`, repeat the 1-bit CMD19 write with a bounded window, and do not gate it behind a recovery read. | Whether the controller's data-timeout counter fires in the write-wait state. A `DTO` within the window means the counter works and the wedge is "no CRC status + long timeout"; silence means the counter is not counting in that state. | Second. |
| 3. Post-write CMD13 drain sample | After the write phase, issue CMD13 (status) and record the R1 response and present state. | Whether the card is in a command-accepting state (controller-side wedge) or not (card-side stall). CMD12 is not appropriate for a single-block write. | Third. |
| 4. Duplicate CMD19 with fresh polls | Reset the data circuit, then repeat CMD19 with fresh polls. | Whether the first write leaves a recoverable controller state. Without the reset it is expected to fail the inhibit wait. | Fourth. |
| 5. Larger block count if (a) survives | Force a larger legal block. | Nothing: JEDEC's minimum is two bytes and 4 bytes is legal, so (a) is already rejected. | Do not run. |

Rung 1 is the recommended next rung. It is a non-persistent bus-test write
(CMD19 pattern), one host-register reset and one CMD14 read; it issues no
CMD6, CMD18, CMD25 or user-data write. Its report must carry the write-phase
terminal sample (`normal`, `error`, `present`), the reset write and readback,
the post-reset `PRESENT_STATE`, the CMD14 result and pattern words, and the
terminal `FAIL`. It must also record whether the reset cleared `DAT_INHIBIT`
before CMD14; a reset that does not clear it is a host-controller row, not a
card row.

Rung 2 is the sharper diagnostic if rung 1's CMD14 fails. It writes only
`TIMEOUT_CONTROL` (a host register) and repeats the already-approved
non-persistent 1-bit CMD19 write; it must not be gated behind a recovery read
(the stage-7 E2 mistake). If rung 2 sees a `DTO`, the controller's counter
works and the 1 s/8 s silence is explained by the 22-minute `TOUTVAL=0x0E`
curtain; if it does not, the counter is silent in the write-wait state and the
completion rule must come from the reset/CMD14 path, not from waiting.

Both rungs are owner-gated. The stage-7 approval does not carry over, and
neither rung may be built, flashed or booted until the owner answers an
explicit BOOT-only approval question that names every card-facing action and
every host register written.

## 7. Owner questions

1. Approve a BOOT-only device run of rung 1 (data reset + CMD14)? The scope
   would be: the standard read-only identification and stage-A pass, one
   non-persistent 4-byte CMD19 write at 1-bit, one SDHCI data-circuit reset,
   one CMD14 read, the existing block-0 recovery read, and no CMD6, CMD18,
   CMD25 or user-data write. The run would also record the post-reset
   `PRESENT_STATE` and the CMD14 pattern words.
2. Or prefer rung 2 (forced `TOUTVAL=0` + 1-bit CMD19 write) first? It writes
   only the host `TIMEOUT_CONTROL` register and repeats the same
   non-persistent CMD19 write; it does not reset the data circuit unless the
   owner adds that to the scope.
3. Should the data-circuit reset become a general post-timeout behaviour in
   the core (so later rungs can recover from any data timeout), or stay local
   to the bus-test helper for this rung? A general change affects every
   timeout path and needs its own simulator rows.
4. Should the CMD18 `TC`-miss row get its own rung, or be folded into the
   bus-test rung's recovery sample? The two rows share a symptom but not a
   proven cause.
5. Is it acceptable to record "the CMD19 write phase has no completion token
   in this configuration" as the write-direction property until rung 1 runs,
   with no write-side goal claimed in the meantime?

## 8. Explicit non-supported statements

- Not proven: the eMMC device is defective; the write direction is broken for
  normal data commands; CMD19 data is not accepted by the card; the 4-bit
  width is bad; the controller's data-timeout counter is broken; a longer
  window would pass; the CMD18 `TC` miss has the same cause as the CMD19
  wedge.
- Not performed: any build, flash, boot, device access, card-state write,
  CMD6, CMD18, CMD25 or user-data write.
- Not authorized: any device run, including the rungs in §6. They require a
  fresh explicit owner approval.

## 9. Anchor index

Driver and probe:

- `package/Gta4lEmmc.c:970-1096`: `gta4l_emmc_write_data()` (wait inhibit,
  transfer-mode word, command word, `BWR` block write, `TC` condition, timeout
  return).
- `package/Gta4lEmmc.c:853-961`: `gta4l_emmc_read_data()` (`BRR`, `TC`).
- `package/Gta4lEmmc.c:2551-2800`: `gta4l_emmc_read_multi()` (`CMD18`, `TC`,
  drain).
- `package/Gta4lEmmc.c:1887-1995`: `gta4l_emmc_bus_test()` (patterns,
  CMD19/CMD14, comparison).
- `package/Gta4lEmmc.c:475-486`: status-enable writes.
- `package/Gta4lEmmc.c:491-507`: `gta4l_emmc_wait_inhibit()`.
- `package/Gta4lEmmc.c:1480`: the only `RESET_ALL`.
- `package/Gta4lEmmc.c:1502`: `TIMEOUT_CONTROL = 0x0E`.
- `package/Gta4lEmmc.h:22-44`: SDHCI register offsets.
- `package/Gta4lEmmc.h:83`: `GTA4L_EMMC_TIMEOUT_MAX 0x0E`.
- `package/Gta4lEmmc.h:137-160`: interrupt bits.
- `package/Gta4lEmmc.h:165-175`: command and transfer-mode bits.
- `package/Gta4lEmmc.h:225-237`: data timeout, poll step, 800,000-iteration
  cap.
- `package/Gta4lEmmc.h:288-294`: bus-test lengths and patterns.
- `package/Gta4lEmmcProbe.c:51`: `GTA4L_EMMC_PROBE_DTO_WINDOW_US`.
- `package/Gta4lEmmcProbe.c:2427-2430`: E2 window set and cleared.
- `package/Gta4lEmmcProbe.c:1085` and `:1090`: the 4-bit and 1-bit terminal
  report sites.

Pinned Linux:

- `/tmp/opencode/linux-pin/sdhci.c:615-655`: `sdhci_transfer_pio()` and the
  space-available mask.
- `/tmp/opencode/linux-pin/sdhci.c:1057-1072`: `__sdhci_set_timeout()`.
- `/tmp/opencode/linux-pin/sdhci.c:1172-1196`: `sdhci_set_transfer_mode()`.
- `/tmp/opencode/linux-pin/sdhci.c:1566-1600`: data-error reset.
- `/tmp/opencode/linux-pin/sdhci.c:230-300`: reset helpers.
- `/tmp/opencode/linux-pin/sdhci.c:3420-3541`: `sdhci_data_irq()`,
  `DATA_END`, and the `MMC_BUS_TEST_R` data-CRC exclusion.
- `/tmp/opencode/linux-pin/sdhci-msm.c:2213-2227`: MSM timeout formula and
  22 s software cap.
- `/tmp/opencode/linux-pin/sdhci-msm.c:2875-2876`: `max_timeout_count = 0xF`.
- `/tmp/opencode/linux-verify/mmc_ops-pinned.c:770-866`:
  `mmc_send_bus_test()` and `mmc_bus_test()` ("Ignore errors from
  BUS_TEST_W").
- `/tmp/opencode/linux-verify/mmc-mmc.c:1030-1085`: the bus-width caller.

JEDEC B1.51 (`/tmp/opencode/jedec-b51.pdf`):

- §6.6.4, PDF page 70: CMD19/CMD14 pattern, two-bit rule, minimum two bytes,
  optional CRC16 ignored.
- Table 7, PDF page 70: start bit, data pattern, end bit.
- §A.6.3 and Table A.223, PDF pages 302-303: per-line bus-test pattern with
  optional CRC16 and end bit.

External precedent:

- TI SPRZ436H erratum i836: bus-test CMD19 incorrectly waits for CRC status;
  guideline: ignore the DTO and proceed to CMD14.
- Linux `mmc_bus_test()` comment "Ignore errors from BUS_TEST_W" and the
  `MMC_BUS_TEST_R` data-CRC exclusion above.

Hardware evidence (outside the tracked tree):

- `out/gta4l/ram-root-emmc-probe-b1c-evidence/`: B1C 4-bit continuations.
- `out/gta4l/ram-root-emmc-probe-b1c1-evidence/`: 1-bit discriminator.
- `out/gta4l/ram-root-emmc-probe-dto-evidence/`: stage-7 R1/E1/Q/E2 rows.
- `out/gta4l/ram-root-emmc-probe-stage2b-attempt2-evidence/` and
  `out/gta4l/ram-root-emmc-probe-stage2b-attempt3-evidence/`: CMD18 `TC`
  rows.
