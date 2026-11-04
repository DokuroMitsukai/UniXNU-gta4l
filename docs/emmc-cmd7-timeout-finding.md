# gta4l eMMC CMD7 R1b timeout finding

**Status:** focused finding, 2026-10-31, revised 2026-11-01. This document
decodes and bounds the owner-supplied historical `fmt=2` stage-3 panic. It is
not a full audit. The exact `fmt=2` build tree is not retained; section 3 uses
the nearest committed predecessor as a reconstruction anchor, not as recovered
build source. The committed probe now emits a `fmt=4` report with CMD7-specific
diagnostics, adds simulator coverage and applies the port's R1b busy
lifecycle: CC and TC are both required inside one shared completion budget, and
the short response is snapshotted only after CC. "R1b" here is the port's
chosen response policy for CMD7, not a protocol requirement; the inspected
Linux stacks select CMD7 as R1 (`docs/pe-storage-stack-provenance.md` §8.1).
That lifecycle change does not by itself explain the historical missing-CC
timeout. The `fmt=4` report ran on hardware on 2026-11-01; a verified BOOT-only
rerun reproduced the same CMD2 failure before CMD7
(`docs/emmc-cmd2-crc-audit.md` §2.9). A later 2026-11-01 candidate added only an
8-us post-ready-CMD1 delay and completed full identification, including CMD7
with CC and TC observed, on three units (section 9). The historical missing-CC
timeout did not reproduce, so the CMD7 capture is now exercised but the earlier
cause remains unexplained. The earlier CMD2 CRC and CMD3 timeout results remain
in `docs/emmc-cmd2-crc-audit.md`; the driver record is `docs/emmc-driver.md`.

**Scope:** identification only. No sector I/O was issued. CRC checking remains
enabled. The image was written to BOOT only; TWRP on `RECOVERY` was unchanged.
No workaround or root-cause conclusion is proposed.

---

## 1. Evidence and provenance

The panic text below is an owner-supplied transcription of the on-tablet panic.
No original photo or hash was supplied in this turn, so this document cannot
independently authenticate the transcription. The line wrapping is the
extraction's; tokens split across lines are reassembled in the decode.

Three transcription points are normalized explicitly:

- The extracted `c31=` is the actual `c3l=` field. The probe format string uses
  lowercase `l`; the historical image's `fmt=2` format string is the one that
  produced this panic, and the current tree's failure report is `fmt=4`
  (`Gta4lEmmcProbe.c:370-443`).
- The extracted `Gta41EmmcProbe.c` is the actual `Gta4lEmmcProbe.c` source
  name. The trailing `:503` is part of the transcription; the historical image's
  source is not in this tree, so this finding does not verify that line. The
  current tree's failure report spans `Gta4lEmmcProbe.c:370-443`.
- The extracted `frames`, `pages` and `free` lines show 17 hexadecimal digits.
  The panic renderer prints 16; the extra leading `f` or `0` is a transcription
  artifact, not an extra field. The 2026-11-01 `fmt=4` photo shows the same
  16-digit layout. The normalized values are
  `pages=0x0000000000079eb1` and `free=0x0000000000079455`. This does not
  affect the CMD7 decode.

Verbatim transcription as supplied:

```text
GTA4L-EMMC PROBE FAILED: stage=3 rc=4294967289 fail=1/7/65536/0000
071b/0000/0002/03f800f0/11 rsp=00000700,0f5903ff,f6dbffef,8e40400c
 cmd1rsp=1/c0ff8080,00000000,00000000,00000000 pre2rsp=1/c0ff8080,
00000000,00000000,00000000 c2=1/0001/0000/05f46619,4d420090,445836
38,00150100 c3p=1/0000/0000/03f800f0/0023/07ff c3t=1/7/071b/000100
00/1/00000700,0f5903ff,f6dbffef,8e40400c c31=1/3/0000/0001/26/0000
000000001557/19200000 cid=15010044,5836384d,42009005,f4661900 fmt=
2 hv=7202 cmd1=3/0/c0ff8080 clocks=1 mci=10000072 pwr=0f clk=0007
hc=00 hc2=0008 ven=00018a9c v3=02626040 dll=6000642c dst=00000000
ddr=80040873 pst=00000000 cfg=00002017 mnd=00000001/000000fc/00000
0fb cmd=00000000 bcr=00000000 apps=00004221 ahb=00000001 step=11 l
og=5/8/7/2 @Gta41EmmcProbe.c:503
frames ffffffff0079d2748 ffffffff0076e3eb0 ffffffff0072c4864 ffffffff0
074164a4
pages=0x00000000000079eb1
free=0x00000000000079455
```

Normalized field order in the historical `fmt=2` report (the current `fmt=4`
format keeps this prefix at `Gta4lEmmcProbe.c:375-384`):

```text
fail=<valid>/<index>/<argument>/<command>/<error>/<normal>/<present>/<step>
rsp=<RESPONSE0>,<RESPONSE1>,<RESPONSE2>,<RESPONSE3>
c2=<valid>/<normal>/<error>/<RESPONSE0..3>
c3p=<valid>/<normal>/<error>/<present>/<normal-enable>/<error-enable>
c3t=<valid>/<index>/<command>/<argument>/<response-stable>/<RESPONSE0..3>
c3l=<valid>/<index>/<first-normal>/<last-normal>/<iterations>/<ticks>/<hz>
cid=<w0>,<w1>,<w2>,<w3> fmt=2 hv=<HOST_VERSION>
```

The historical report ends the capture at `fmt=2`. The committed `fmt=4`
report keeps those fields and appends the CMD7 block described in §2.5; the
transcription above remains a `fmt=2` record.

Artifact and flash provenance (builder tree at `$HOME/slopcode/taba7unixnu`;
the device docs live in `src/UniXNU-gta4l`):

| Item | Value |
| --- | --- |
| Diagnostic | `emmc-probe-3` (`gta4l-emmc-probe=3`, stage 3) |
| BOOT image | `out/gta4l/ram-root-emmc-probe-fmt2-evidence/boot-ram-root-emmc-probe-fmt2.img`, 85,688,320 bytes, SHA-256 `7bf3e71991f949f2da6fcff2ef388268904a512b1876062d04e9bb86b6f411b6` |
| Payload | `out/gta4l/ram-root-emmc-probe-fmt2-evidence/payload.img`, 83,886,080 bytes, SHA-256 `ba4e88ba5597d28c4ed70917c6e710f9b35a8eb8e59193e4ed834b8ff5eef4a7` |
| Kernel | `src/UniXNU/out/gta4l/ipados-17.7.11-21H461/gta4l/kernel/kernel.macho`, SHA-256 `f53860af78c677c18a9562fc04d57ff3e130d7c20f8a54e26451a53a5ffe0072` |
| Source | `Gta4lEmmc.c b4461abc…`, `Gta4lEmmc.h c50b6467…`, `Gta4lEmmcProbe.c 60cff259…` (full hashes in the flash record) |
| Flash record | `out/gta4l/ram-root-emmc-probe-fmt2-evidence/flash-record.json` |
| BOOT write | `/dev/block/mmcblk0p69`, 100,663,296 bytes; image bytes read back identical; partition SHA-256 before `d42ce6db…`, after `92e80609…`; trailing remainder identical |
| RECOVERY | `/dev/block/mmcblk0p70`, SHA-256 `14e11370c40fd5b70a3dba1d49283e938001141c9d5f8de6685653e6a793f5ae` before and after |
| Build gates | Payload contract 7/7; boot-image gate 18/18; only the ramdisk differs from the pinned `boot-ram-root-sdhci-v5.img` baseline |
| Reboot | The flash record says the write procedure did not reboot (`rebooted=false`, `booted=false`). The panic came from the owner's later boot, not from the flashing procedure. |

The flash record also records verified EFS/SEC_EFS backups and no CPEFS
partition in the gta4l GPT. Those are preconditions for the BOOT write, not
evidence about the CMD7 failure.

---

## 2. Decode of the report

### 2.1 Result and terminal command

- `stage=3` is the probe's identification stage, not the protocol step.
- `rc=4294967289` is the unsigned rendering of `0xFFFFFFF9`, i.e. **-7 =
  `GTA4L_EMMC_ERR_CMD_TIMEOUT`** (`Gta4lEmmc.h:249`). It is not the earlier
  `-8` CMD CRC result.
- `fail=1/7/65536/0000071B/0000/0002/03F800F0/11`:
  - `valid=1`: the failure record is populated.
  - `index=7`: **CMD7** (`SELECT_CARD`).
  - `argument=65536=0x00010000`: RCA 1 in bits 31:16
    (`GTA4L_EMMC_DEFAULT_RCA=1`).
  - `command=0x071B`: index 7 (`0x0700`) plus `RESP_SHORT_BUSY` (`0x3`),
    `CRC` (`0x8`) and `INDEX` (`0x10`). This is the correct SDHCI encoding
    for an R1b command with CRC and index checking; the current driver's CMD7
    call uses exactly these flags (`Gta4lEmmc.c:1515-1518`).
  - `error=0x0000`: no enabled error bit was latched.
  - `normal=0x0002`: **TC** (Transfer Complete) is set; **CC** (Command
    Complete, `0x0001`) is absent.
  - `present=0x03F800F0`: the CMD and DAT inhibit bits (bits 0-1) are clear.
    The remaining bits, including the transfer/buffer bits in the low byte,
    are not decoded in this finding.
  - `step=11`: `GTA4L_EMMC_STEP_CMD9`. CMD9 was the last completed protocol
    step; CMD7 success would have set step 12 (`GTA4L_EMMC_STEP_CMD7`). The
    report's `step=11` agrees with `fail_step=11`.
- `c3t=1/7/071B/00010000/1/...`: the generic timeout capture is valid, its
  index is 7, its command readback is `0x071B`, its argument readback is
  `0x00010000`, and its response was stable across two reads. Despite the
  legacy `c3t` name, **this capture is the CMD7 timeout, not CMD3**.

### 2.2 Earlier commands completed

- `c2=1/0001/0000/05F46619,4D420090,44583638,00150100`: CMD2 capture valid,
  `normal=0x0001` (CC), `error=0`, raw response words present.
- The normalized CID is `15010044,5836384D,42009005,F4661900` (`cid=`), which
  matches the known CID from the earlier runs. Applying
  `gta4l_emmc_read_long_response()` to the raw words gives the same four
  words; the raw order is RESPONSE0..3, not CID word order.
- `c3l=1/3/0000/0001/26/0000000000001557/19200000`: the CMD3 poll capture is
  valid, index 3, first normal status 0, last normal status 1 (CC), 26
  iterations, 5,463 ticks at 19,200,000 Hz (about 284.5 microseconds). **This
  is CMD3's poll history; the `c3l` fields remain CMD3-only**
  (`Gta4lEmmc.c:546,614-619,649-653`), and the committed core records CMD7 history separately
  in `c7q`. It is not CMD7's timeout duration and does not say why
  CMD7 timed out.
- `c3p=1/0000/0000/03F800F0/0023/07FF`: the pre-CMD3 capture is valid and
  clean: no pending normal/error status, CMD/DAT inhibit clear, normal enable
  `CC|TC|BRR` (`0x0023`), error enable `0x07FF`. This confirms the status
  enables before CMD3; there is no equivalent pre-CMD7 capture.
- `step=11` and the successful CMD3/CMD9 progression show that this run got
  past the previous `cmd2window` terminal point (CMD3, step 9). CMD2, CMD3 and
  CMD9 all completed before CMD7 was submitted.

### 2.3 Response window is ambiguous

`rsp=00000700,0F5903FF,F6DBFFEF,8E40400C` and the same words in `c3t`:

- `RESPONSE0=0x00000700`. If this were a fresh CMD7 R1 response, it would
  decode as CURRENT_STATE=3 (STBY), READY_FOR_DATA set, and no error bits
  (`R1_ERROR_MASK=0xFDF9A080`). That is a plausible state for a card that has
  not yet been selected.
- `RESPONSE1..3` are not part of a CMD7 short response; a CMD7 R1 populates
  only `RESPONSE0`. Applying the driver's 32-bit long-response transformation
  to the window gives `40400CF6,DBFFEF0F,5903FF00,00070000`. That arithmetic
  does not recover a coherent CMD9 CSD, so it does not establish retention.
  Separately, the raw words are consistent with retention: `RESPONSE1` and
  `RESPONSE2` match the CSD words recorded in the recovery inventory
  (`d02701320f5903fff6dbffef8e404000`;
  `docs/sdhc-evidence-gap-checklist.md`), and `RESPONSE3` matches that CSD's
  last word except for its low nibble. That is a consistency observation, not
  a validated R2 capture.
- The raw window therefore **cannot distinguish a fresh CMD7 R1 from a
  retained window**. `timeout_response_stable=1` only means two consecutive
  reads matched; it does not establish freshness or validity.
- The window is not a validated CMD7 response and must not be promoted to one.
  In particular, the report does not establish that CMD7 was accepted, that
  the card entered TRAN, or that the card is selected.

### 2.4 Log and live-register fields

- `log=5/8/7/2` is the probe's last log event, not a reset history:
  `log_count=5`, `event=8` (`GTA4L_EMMC_LOG_ERROR`), `a=7` (the failing CMD7
  index), and `b=(error << 16) | normal = 0x00000002` (TC). The failure record
  logs this event after taking the fresh timeout snapshot
  (`Gta4lEmmc.c:659-684`; the log call is `:123-124`). It is not a CMD/DAT
  reset log.
- `cmd=00000000`, `bcr=00000000`, `apps=00004221` and `ahb=00000001` are GCC
  SDCC1 registers read after `gta4l_emmc_init()` returned. They are not the
  SDHCI COMMAND register. The SDHCI command submission evidence is the
  `c3t` command readback `0x071B`.
- `clocks=1`, `mci=10000072`, `pwr=0F`, `clk=0007`, `hc=00`, `hc2=0008`,
  `ven=00018A9C`, `v3=02626040`, `dll=6000642C`, `dst=00000000`,
  `ddr=80040873`, `pst=00000000`, `cfg=00002017` and
  `mnd=00000001/000000FC/000000FB` repeat the expected current-source host
  state from the earlier runs. This finding does not attribute the CMD7
  timeout to those values.
- `frames`, `pages` and `free` are panic metadata, not probe fields.

### 2.5 CMD7-specific capture in the committed core (`fmt=4`)

The committed core adds CMD7-specific fields to both the failure and success
reports. The capture is observational; the R1b decision path now also requires
both completions (section 3). The poll history now spans the busy-completion wait
instead of stopping at CC, so the capture format is version 4 and earlier
CC-terminated CMD7 history must not be decoded with these semantics:

- `c7p=<valid>/<normal>/<error>/<present>/<normal-enable>/<error-enable>/<RESPONSE0..3>`:
  sampled after the pre-command clear and before the `ARGUMENT`/`COMMAND`
  writes.
- `c7q=<valid>/<or-normal>/<or-error>/<first-normal>/<last-normal>/<iterations>/<ticks>`:
  the OR of observed normal and error samples, the first and last in-loop normal
  samples, the completed iterations and the counter delta.
- `c7c=<cc-valid>/<cc-iteration>/<cc-ticks>/<tc-valid>/<tc-iteration>/<tc-ticks>`:
  the first observed CC and TC, with separate validity flags.
- `c7t=<valid>/<normal>/<error>/<present>/<command>/<argument>/<RESPONSE0..3>`:
  a sequential status capture before the status clear. The three status fields
  come from the path's preceding reads, then the helper reads `COMMAND`,
  `ARGUMENT` and the raw response window. On the in-loop error path the fields
  are that loop sample; on the timeout path they are fresh post-timeout
  samples; on the busy-completion success path they are fresh samples taken
  before the response snapshot is used. It is not a copy of the last in-loop
  poll sample; `c7q`/`c7c` remain strictly in-loop history. In the CC-then-TC
  case the poll sees CC only while `c7t` can show CC|TC.
- `c7x=<count>/<overflow>/<i>:<normal>:<error>,...`: a bounded trace that stores
  the first observed sample and then each observed change. Repeated unchanged
  samples do not count. Once the four stored entries fill, the overflow counter
  advances once per omitted observed change, compared with the previous
  observation rather than the last stored entry; a transition back to the last
  stored value is still an observed change.

These are sequential register samples, not an atomic snapshot. The OR mask and
first/last values show only which bits were observed; they cannot detect an
assertion and clear between samples. A trace entry records an observed sample;
later entries record observed changes, not continuous bus visibility. Added
diagnostic reads can perturb timing.

The final evaluated error and the return value differ by path:

| Path | Status reads before `c7t` | After `c7t` | Final result |
| --- | --- | --- | --- |
| Enabled error in the poll | error, then normal, present | `COMMAND`, `ARGUMENT`, raw response | that in-loop error, mapped |
| Poll exhausted | fresh error, normal, present | `COMMAND`, `ARGUMENT`, raw response | software timeout; the pre-capture error is recorded, with no post-capture re-read |
| CC and TC observed | fresh normal, present, error | `COMMAND`, `ARGUMENT`, raw response, then error again | success unless the final error read has an enabled bit |

Only the last path re-reads error after the terminal diagnostics. On the timeout
path an error that becomes visible during those reads is not evaluated, and the
return remains `GTA4L_EMMC_ERR_CMD_TIMEOUT`. The success-path final re-read is
the observation boundary for that path; an error asserted after it remains
unobserved. The simulator's `cmd7-terminal-error` case exercises that
success-path boundary, and a mutation that removes the final sample is rejected.

The command timeout is a nominal delay budget, not a counter deadline.
`gta4l_emmc_poll_iterations()` converts `GTA4L_EMMC_CMD_TIMEOUT_US` (1,000,000
µs for CMD7) into at most 100,000 iterations with a 10 µs delay each. The
counter deltas in `c7q`/`c7c` are observations; they do not bound or extend the
loop, and the actual elapsed time includes MMIO and diagnostic overhead.
Terminal samples are taken after the loop and are outside that nominal budget.

Report fit and renderer budget are separate limits. The probe harness formats
each report into a 2048-byte buffer and rejects a negative or truncated
`vsnprintf` result, so a report must be at most 2047 bytes. That is a host libc
check only.

The earlier maximum-width scenarios are end-to-end samples, not a width bound:
each combines a long CMD3/CMD7 poll with a 32-bit capacity and does not
maximize every field independently.
`scripts/sim-gta4l-emmc-probe/report-width-fixture.c` (included only by
`scripts/test-gta4l-emmc-probe.py`, never by the production build) closes that
gap by calling the production failure, identification and full-probe formatters
with synthetic field-wise extremes. Only genuinely arbitrary 32-bit register,
argument and response values are set to `0xFFFFFFFF`; derived or narrow fields
use their own bounds:

| Field(s) | Type and production bound | Printed width | Source |
| --- | --- | --- | --- |
| `counter_hz` | `uint64_t`, probe accepts 1,000,000..1,000,000,000; out-of-range falls back to 19,200,000 | `%u`, at most 10 decimal | `gta4l_emmc_probe_counter_hz()` |
| `poll_iterations`, `cmd7_poll_iterations` | `uint32_t`, at most `GTA4L_EMMC_MAX_POLL_ITERATIONS` = 100,000 | `%u`, at most 6 | core poll helper |
| `cmd7_cc_iteration`, `cmd7_tc_iteration`, stored trace iterations | `uint32_t`, loop index 0..99,999 | `%u`, at most 5 | `send_command()` poll loop |
| `cmd7_trace_overflow` | `uint32_t`, at most iterations minus trace entries = 99,996 | `%u`, at most 5 | trace recorder |
| `poll_ticks`, `cmd7_poll_ticks`, `cmd7_cc_ticks`, `cmd7_tc_ticks` | `uint64_t` counter differences | `%08x%08x`, 16 hex | counter arithmetic |
| `cmd1_attempts`, `cmd1_timeouts` | `uint32_t`, at most 2,000,000/100,000 = 20 | `%u`, at most 2 | `card_ready()` retry loop |
| `fail_index`, `timeout_index`, `poll_index` | command index, 0..63 | `%u`, at most 2 | 6-bit SD command field |
| `rev` | `uint8_t`, 0..255 | `%u`, at most 3 | EXT_CSD revision byte |
| `bw` | low nibble, 0..15 | `%u`, at most 2 | EXT_CSD `BUS_WIDTH` |
| `step`, `fail_step` | 0..15 | `%u`, at most 2 | `GTA4L_EMMC_STEP_*` |
| `cmd7_trace_count` | 0..`GTA4L_EMMC_CMD7_TRACE_ENTRIES` = 4 | `%u`, 1 | bounded trace store |
| `last_event` | 0 or a `GTA4L_EMMC_LOG_*` event (1..8) | `%u`, 1 | probe log callback |
| validity and `response_stable` flags | boolean 0/1 | `%u`, 1 | capture validity |
| `stage` | `int`, `gta4l_emmc_probe_stage()` accepts 1..3 | `%d`, 1 | stage clamp |
| `result` | `int`, error constants -18..-1 | `%d`, at most 3 host; 10 renderer unsigned | error constants |
| 8-bit registers (`pwr`, `hc`) | `uint8_t` | `%02x`, 2 | access width |
| 16-bit registers (`clk`, `hc2`, `hv`, `timeout_command`, interrupt windows) | `uint16_t` | `%04x`, 4 | access width |
| `fail_command`, `cmd7_term_command` | `uint16_t` encoded command word / SDHCI COMMAND readback, stored in `uint32_t` | `%08x`, 8 with zero high half | `record_failure()` argument; SDHCI readback |
| 32-bit registers, arguments, responses, CID/CSD words, block sums/words, `last_a`, `last_b` | `uint32_t` | `%08x`, 8 hex; `%u`, at most 10 decimal | arbitrary sampled value |
| `rca` | fixed `GTA4L_EMMC_DEFAULT_RCA` = 1 | `%u`, 1 | driver policy |
| `log_count` | `uint32_t`, at most 50,000 power-poll iterations plus at most 20 CMD1 timeout logs and fewer than ten other one-shot calls | `%u`, at most 5 | probe log callback |

With every field at its widest, no narrower production combination can format
longer, and the production format strings and argument lists are the ones under
test. The four stored trace indices use the late ordered sequence
`0,99997,99998,99999`, so every index after the first is at five digits. The
measured host lengths are 1093 bytes for the failure report, 788 for the
identification report and 1493 for the full-probe report, leaving 954, 1259
and 554 bytes of the 2048-byte buffer. The width-budget control separately
checks the accept boundary at `sizeof(panic_message) - 1` and
`sizeof(panic_message)`. This remains a host libc bound; it does not include the
renderer's `\nframes` suffix, log truncation or framebuffer clipping, which are
the separate limits below.

The on-device panic path uses the renderer at
`../UniXNU/scripts/xnu-panic-screen.c` in the sibling `UniXNU` repository
(working-file SHA-256
`54b950a7d83c30f7720d30551c6d5f10cea5e40dcccc5adb2b4c70a98240a68b`, sibling
HEAD `6ee4ada8a82f5bd2d9032b4d7cf033e878e0551f`; the file was clean in that
worktree when checked). Its `PANIC_TEXT` log buffer is 2048 bytes but stores at
most 2047 characters, keeps only bytes 9-126 (control bytes below 32 become
spaces) and is skipped when an earlier panic already wrote text. After the
report it appends `\nframes` and up to 24 space-prefixed `%016x` frame addresses
while logging is still enabled, so the worst-case suffix is 7 + 24*17 = 415
characters; a report that fits 2048 bytes does not by itself guarantee that the
full logged panic fits. `pages=` and `free=` are emitted after logging is
disabled and are screen-only.

Renderer width differs from host width in one field: the failure report's
`rc=%d`. The pinned renderer formats a non-long `%d` as an unsigned 32-bit
value, so a negative result never keeps its sign. The host-widest form used by
the fixture, `-18`, becomes `4294967278` (10 characters instead of 3); the
widest expansion is a one-digit negative result such as `-7`, which becomes
`4294967289`, eight characters more than the host's two. The failure report's
1093-byte host bound therefore becomes at most 1100 bytes on the renderer
(`1093 - 1 + 8`, or equivalently `1093 - 3 + 10` for the fixture's `-18`).
Identification and full-probe reports carry `rc=0`, so their renderer lengths
stay 788 and 1493 bytes. Adding the 415-character worst-case frame suffix gives
1515 bytes for the failure report and 1908 bytes for the full-probe report,
both within the 2047-character log by static arithmetic; that
assumes the report is logged first and all 24 frames are emitted. It is not a
device demonstration, so full-log capture remains an open gate.

Framebuffer visibility is a third, independent limit: the renderer requires a
32-bpp framebuffer, starts text at `h/8 + 8`, uses 18x42-pixel cells, wraps at
the width and silently clips below the bottom, and `pages`/`free` consume screen
lines too. The formatter supports `%s`, `%x`/`%X`, `%u`/`%d`, `%p` and `%%`,
with width, zero-padding, precision and `l`/`ll`/`z`/`h`; it has no `%c`, and
`%d` is printed unsigned as described above, so the failure report's negative
`rc` loses its sign on the device. The host check therefore does not prove
on-device display or full-log capture, and the framebuffer gate remains open.

The core simulator and probe simulator cover the transcribed TC-only timeout,
changed and unchanged response windows, CC-then-TC, TC-then-CC, simultaneous
completion, CC without a busy completion, busy-phase errors, a late busy-phase
error, an error asserted during the terminal diagnostic reads on the
busy-completion success path, and bounded-trace overflow. The core simulator additionally covers a
repeated unchanged plateau and a return to the last stored trace value. A core
mutation restores the old comparison against the last stored entry; the return
scenario must reject it, while the plateau scenario checks that repeated
unchanged samples do not advance the overflow count. The CC-only case now ends
in a bounded timeout because CC alone is not success while the busy completion is
missing; a core mutation that restores CC-only success is rejected.
Host counts are 179 core scenarios with 104 mutation controls and 163 probe
scenarios with 77 mutation controls; the `fmt=4` report ran on 2026-11-01 on two
units (an SM-T505 and the owner's SM-T505N, per the owner); both reproduced the
CMD2 failure before the CMD7 capture. The later `cmd1gap` candidate reached CMD7
and observed CC and TC on three units (section 9), so the CMD7 capture is now
hardware-exercised; the simulator scenarios remain host checks.

---

## 3. Nearest retained source path for `-7`

The `fmt=2` image was built from an uncommitted working tree whose source
snapshot is not retained; the flash record pins only the source hashes
(`b4461abc…` for the core, `c50b6467…` for the header and `60cff259…` for the
probe). The nearest retained source is the last committed
pre-CMD7-instrumentation file, `7c2ae09:package/Gta4lEmmc.c` (SHA-256
`722110a5…`; its content is unchanged at `HEAD`). It is a reconstruction
anchor, not a recovered build: its hash differs from the flash record's
`b4461abc…`, so byte identity is not established. It contains a CC-only
completion rule and a terminal-failure record consistent with the transcribed
fields, but the lost build may differ. That revision is also the core source
of the earlier `cmd2window` hardware image, so its shared command path was
exercised on the tablet; that does not make it the `fmt=2` build. The
CMD7-specific pre-command, poll-history, first-CC/TC, terminal and trace
captures (`c7p`/`c7q`/`c7c`/`c7t`/`c7x`) were added later and exist in the
committed `fmt=4` core (section 2.5). The sequence below describes the
predecessor; all line references are to `7c2ae09:package/Gta4lEmmc.c`, not to
the current source tree.

1. Waits for CMD/DAT inhibit clear (`:300-307`); an inhibit failure records the
   terminal command failure before returning.
2. Clears both interrupt status registers (`:308`).
3. Writes `ARGUMENT=0x00010000` and `COMMAND=0x071B`, with barriers
   (`:309-314`).
4. Polls `ERROR_INT_STATUS` first, then `NORMAL_INT_STATUS` (`:316-338`). If
   any enabled error bit is set, it samples normal and present, records the
   failure, clears the latches and returns the mapped error (`:321-331`).
   Otherwise it breaks only when `CC` is set (`:334-335`).
5. If the loop exhausts without `CC`, it takes fresh `ERROR` and `PRESENT`
   samples, records the failure with the last in-loop normal sample, clears the
   latches, and returns `GTA4L_EMMC_ERR_CMD_TIMEOUT` (`:339-348`). It does not
   re-read `COMMAND`, `ARGUMENT` or the raw response window here.
6. On the `CC` path it reads the response (`:350-358`), clears all interrupt
   status and returns success (`:360-361`), without waiting for the R1b busy
   completion. This snapshot has no CMD7 pre-command or terminal capture; the
   `c7p`/`c7t` fields are later instrumentation.

The observed `error=0` means the error path was not taken. The transcribed
`-7` establishes the explicit software-timeout result, not a mapped hardware
`CMD_TIMEOUT` bit; in the predecessor that return is at `:347`, but the lost
build's exact line is unknown. The predecessor records the timeout with its
last in-loop normal sample; the committed core instead takes fresh post-timeout
samples for the timeout record and keeps the loop value in `poll_last_normal`.
The `fmt=2` report's `fail_normal=0x0002` (TC without CC) is therefore a
recorded sample whose provenance depends on the unretained build. It does not
distinguish a never-asserted CC from one cleared between samples; the build
reported no CMD7 in-loop history, and a single recorded sample would not settle
the question. The committed core's `c7q` is the first capture that records
in-loop samples.

The command readback `0x071B` in `c3t` confirms that the controller still held
the CMD7 command at the timeout sample. The argument readback matches RCA 1.
The driver's CMD7 encoding and response type are therefore not obviously
malformed.

**Separate implementation gap (F1), fixed in the committed core.** The retained
predecessor reached the success path on `CC` alone and did not wait for the R1b
busy completion (`TC`) before clearing the status registers. The lost `fmt=2`
build plausibly did the same, but that is inferred from its recorded fields and
the nearest source, not verified against its bytes. A CC-only rule would return
success for an R1b command whose busy phase had not finished and could discard
a late busy-phase error at the next pre-command clear. The committed core now
accumulates CC and TC across one shared poll budget, requires both, and
snapshots the short response only after CC. The gap is **not** the established
cause of the transcribed missing-CC timeout: requiring `TC` in addition to `CC`
would not turn the observed TC-only terminal state into a success. The two
observations must not be conflated.

---

## 4. Observed anomaly and candidate interpretations

The concrete anomaly is:

> CMD7 was submitted with R1b/CRC/index checking, no command error bit was
> latched, the controller's terminal status shows TC (`0x0002`) but not CC
> (`0x0001`), and the driver timed out after its completion poll.

This is a completion-status mismatch on the first busy-wait command in the
sequence. CMD0/CMD1/CMD2/CMD3/CMD9 are non-busy commands and all completed
(or were recovered) before CMD7. The report does not establish why the mismatch
occurred. Separately, the retained pre-instrumentation source accepted an R1b
command on CC alone (F1 in section 3); the lost build's exact source is not
retained, and the committed core now requires both CC and TC. That defect does not
explain why CC was absent here.

Candidate interpretations, **none established**:

1. **CC was asserted and cleared before the terminal sample.** The driver does
   not clear status during the poll, so this would require a hardware
   auto-clear or another unobserved clear. No evidence for that behavior is in
   the report.
2. **The controller did not assert CC for this R1b command and raised TC
   alone.** That would make the historical driver's CC-only completion rule
   insufficient for this controller's R1b path. However, the pinned Linux path
   also uses CC to read the response and TC to finish the busy wait (section 5),
   so this would be a controller/vendor deviation that needs direct evidence,
   not an assumption.
3. **The command did not complete on the bus and TC is spurious or late.** The
   command readback and the absence of error bits argue against a simple
   malformed submission, but they do not prove the card accepted CMD7.
4. **The card accepted CMD7 and the `0x00000700` R1 is genuine, but the host
   missed completion.** This is consistent with the R1 decode, but the
   response window is not a validated CMD7 response (section 2.3), so it is not
   established.
5. **The nominal 1-second delay budget is too short for the card's R1b busy
   period.** TC being set would normally indicate that busy ended, so this is
   not the leading interpretation; the exact TC semantics on this controller
   are not established.

None of these is a root-cause conclusion. The report is consistent with a
CMD7/R1b completion-path problem, but it does not distinguish a host status
problem from a card/command problem.

---

## 5. Reference comparison (context, not a fix)

The pinned LineageOS `sdhci.c` reference (local copy used for the earlier
audits) handles a non-data R1b command in two parts. The line references below
are to `/tmp/opencode/lineage-kernel-files/sdhci.c` (SHA-256
`846e5ea0dd264df5b95a6848245f44b42ed86bfa0d9a7b9ee1d486eda9aeca48`) and
`sdhci-msm.c` (`298aeafcd49392bc706ca7573b178da7bcf614294bed26ccd9c21c4f4dbef751`),
as recorded in `docs/vendor-sdhc-power-sequence.md`; the mainline
`linux-verify` cache is a different file, so its line numbers do not apply.

- `host->data_cmd` is set for `MMC_RSP_BUSY` (`sdhci.c:1385-1386`).
- `sdhci_cmd_irq()` finishes the command only on `SDHCI_INT_RESPONSE` (CC)
  (`sdhci.c:3287`). For an R1b command it returns without finishing the
  request after reading the response (`sdhci.c:1498-1503`).
- The busy end is handled in `sdhci_data_irq()` on `SDHCI_INT_DATA_END` (TC),
  which finishes the request once the command has also completed
  (`sdhci.c:3350-3361`).

Linux therefore also expects CC for command completion and TC for the R1b busy
end. A TC-only state is not a normal completion in that path either. The
`SDHCI_QUIRK2_IGNORE_DATATOUT_FOR_R1BCMD` quirk (`sdhci-msm.c:5810`) only
suppresses data-timeout handling for R1b commands; it does not supply CC and is
not evidence that this timeout is fixed by that quirk. The driver's polling
loop uses the same CC bit as Linux's interrupt path; polling versus interrupts
is not by itself an explanation for a missing sticky CC bit, but the controller
may behave differently and that is an open question.

---

## 6. What this report does not establish

- It is not a CMD2 CRC failure. CMD2 completed with CC and a matching
  normalized CID.
- It is not the previous CMD3 completion problem. CMD3 completed (the CMD3
  poll saw CC) and CMD9 completed (`step=11`).
- It does not establish why CMD7 timed out, whether the controller ever
  asserted CC, or whether TC is the R1b busy-end indication on this controller.
- It does not establish that the card accepted CMD7, entered TRAN, or is
  selected. The `0x00000700` response is not a validated CMD7 R1. The upper
  raw words match the recovery CSD apart from one nibble, which is consistent
  with a retained CMD9 window, but that is consistency only, not proof.
- It does not establish a card, host-controller, pad or rail fault. No
  electrical measurement was made.
- It does not establish that the nominal 1-second delay budget is wrong.
- It does not justify accepting TC as completion, retrying CMD7, changing the
  response type, skipping CC, or disabling CRC checking.
- It does not include an independent photo hash. The panic is an
  owner-supplied transcription.

No EXT_CSD, capacity, partition or sector data was obtained, and no sector read
was issued.

---

## 7. Open questions for the full audit

1. **CMD7 poll history.** The historical `c3l` capture is CMD3-only. The
   committed core's `c7q` records a CMD7-specific OR mask, first/last normal
   sample, iteration count and tick count, and `c7c` records the first observed
   CC/TC with separate validity flags. These samples can show whether CC or TC
   was observed at any sample, but they cannot distinguish "CC never appeared"
   from "CC appeared and cleared between samples"; only an observed change is
   evidence. The 2026-11-01 `fmt=4` runs failed at CMD2 before CMD7, so those
   runs did not exercise the CMD7 poll history. The later `cmd1gap` candidate
   reached CMD7 and observed CC and TC on three units (section 9).
2. **Pre-CMD7 response window.** The committed core's `c7p` captures raw
   `RESPONSE0..3` immediately before CMD7, so a later run can compare it with
   the terminal window. Equality alone does not prove staleness, and a
   difference does not prove a valid response.
3. **R1b CC/TC semantics on this controller.** Determine whether this SDHCI
   implementation raises CC for R1b commands and how it orders CC and TC.
   Compare with the pinned Linux `sdhci_cmd_irq()`/`sdhci_data_irq()` path and
   with the vendor `sdhci-msm` behavior. Do not infer a quirk from the
   `IGNORE_DATATOUT_FOR_R1BCMD` name alone.
4. **Timeout budget.** Is the nominal 1-second delay budget sufficient for the
   card's CMD7 busy period? Compare with Linux `busy_timeout` handling and the
   software counter samples already captured; no electrical or direct duration
   measurement is proposed. Note that TC is already set, so the timeout may not
   be a busy-duration issue.
5. **Present-state bits.** `present=0x03F800F0` has the CMD/DAT inhibit bits
   clear but also has transfer/buffer bits set in the low byte. The current
   docs do not decode those vendor bits. They are not used in this finding's
   attribution, but the full audit should determine whether they are a normal
   vendor encoding or a stale data-path state.
6. **Pre-command clear ordering for CMD7.** The driver clears both status
   registers before submitting each command and has a barrier after the clear.
   The `c3p` capture shows a clean pre-CMD3 state; the committed core's `c7p` adds
   the equivalent pre-CMD7 capture. The historical `fmt=2` report alone cannot
   fully exclude a stale or late status transition around CMD7.

The `fmt=4` CMD7 capture from a run that reaches CMD7 now exists: the `cmd1gap`
candidate completed CMD7 with CC and TC on three units (section 9). It closes
the capture gap but does not explain the historical timeout, so a
reference-level explanation of this controller's R1b completion behavior is
still the next discriminating evidence. The committed shared-budget CC+TC
requirement is the port's chosen R1b completion policy; the inspected Linux
stacks select R1 for CMD7, so this is a host policy choice rather than a
universal protocol mandate. It is not a workaround derived from this report;
it is not claimed to fix the missing-CC timeout. No workaround for the
observed timeout should be made from this report alone. CRC checking must remain
enabled.

---

## 8. Related records

- `docs/emmc-cmd2-crc-audit.md` §2.6 (CMD2 CRC record), §2.7 (CMD3 timeout),
  §2.8 (this result), §2.9 (the 2026-11-01 `fmt=4` runs, which failed at CMD2),
  §2.11 (the `cmd1gap` candidate runs that reached CMD7), H8
  (completion-path hypothesis), D7 (capture plan) and §10 (capture fields).
- `docs/emmc-driver.md` §4 (simulator coverage), §5.3.1 (CMD3 timeout),
  §5.3.2 (this result and the `fmt=4` fields), §5.3.3 (the 2026-11-01 `fmt=4`
  runs), §5.3.4 (the `cmd1gap` candidate runs) and §7 (limits).
- `docs/emmc-cmd1-gap-candidate.md` for the candidate definition, build and
  three-unit result.
- `docs/evidence-baseline.md` and `docs/diagnostic-payload-input-inventory.md`
  for the artifact batch.
- `scripts/test-gta4l-emmc.py` and `scripts/sim-gta4l-emmc/harness.c` for the
  core CMD7 scenarios, including the `cmd7-cc-only` timeout case.
- `scripts/test-gta4l-emmc-probe.py` and
  `scripts/sim-gta4l-emmc-probe/harness.c` for the `fmt=4` report text and
  mutation controls.

---

## 9. 2026-11-01 cmd1gap candidate update

The `cmd1-crc-retire-8us` candidate (`docs/emmc-cmd1-gap-candidate.md`) is the
`fmt=4` fail-closed source plus one 8-us delay after an accepted ready CMD1.
The image is
`out/gta4l/ram-root-emmc-probe-cmd1gap-evidence/boot-ram-root-emmc-probe-cmd1gap.img`,
85,688,320 bytes, SHA-256 `8a463dae…`; source `874e6878…`, `5dd223f7…` and
`932552a2…`; payload `6b6f5212…`; gate 18/18 and contract 7/7. It was written
to BOOT only, with the tail and `RECOVERY` unchanged; the full flash
provenance is in `docs/emmc-cmd2-crc-audit.md` §2.11.

The owner ran it on the owner's SM-T505N and two SM-T505s and supplied text
transcriptions. All three reached the `GTA4L-EMMC IDENT REPORT: rc=0` success
report and stopped at the intentional panic. On the owner's unit the CMD7
fields were `c7p=1/0000/0000/03f800f0/0023/07ff/…`,
`c7q=1/0003/0000/0000/0003/26/…1582`,
`c7c=1/25/…1573/1/25/…1573`,
`c7t=1/0003/0000/03f800f0/0000071b/00010000/…` and
`c7x=2/0/0:0000:0000,25:0003:0000,0:0000:0000,0:0000:0000
@Gta4lEmmcProbe.c:497`. The two SM-T505 transcriptions differ only in the
fields listed in `docs/emmc-cmd2-crc-audit.md` §2.11: CID serial/date, the
EXT_CSD byte sum, a few poll ticks and VM counters. On all three the CMD7 poll
OR mask contains both CC (`0x0001`) and TC (`0x0002`), error is 0, the first
observed change at iteration 37 already carries both, and the terminal command
is `0x071B` with argument `0x00010000` (RCA 1).

This is positive evidence that the committed CC+TC busy lifecycle completes on
this controller and card for CMD7, and that the historical `fmt=2` TC-only
timeout is not reproduced by the current policy on these units. It does not
explain the historical missing-CC sample: the `fmt=2` build is not retained, its
exact source differs from the current tree, and the CMD7 capture is a sequence
of samples, not a continuous trace. No workaround, timeout change or R1 policy
change is adopted from this result. CRC checking remains enabled. The remaining
open questions in section 7 stand, except that the CMD7 poll history is now
exercised.
