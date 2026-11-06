# gta4l D12.5 stage-9 observation closeout and H9T kind-column finding

**Status:** focused finding, 2026-11-06. Host-only analysis of one
owner-supplied observation; this batch performed no device access, boot, flash,
candidate build, formatter or driver change. It does not rewrite the D12.5
host-batch record or any earlier verdict.

## 1. Observation

The single authorized D12.5 stage-9 no-retry observation was consumed on the
candidate recorded in
`out/gta4l/ram-root-emmc-d12.5-cmd3-timeline-evidence/`:

- BOOT image `boot-ram-root-emmc-d12.5-probe9.img`, SHA-256
  `9e0317909530f7d59eeb7d8d80dfa9f0a8ed354d2fd0bb5165c646c6b17f6bc2`,
  85,688,320 bytes, 18/18 gate, flashed BOOT-only to p69 with the prefix
  verified and the tail unchanged.
- Payload `37f97ce9…` with an 8/8 contract and exactly one
  `gta4l-emmc-probe=9` token; prelinked kernel `b1871d40…`, packaged kernel
  `555b3594…`.
- Built from device HEAD `1ee1f4f9` and shared HEAD `0a1cc714`; the
  build-time dirty diff is bound by `device-unstaged-diff.sha256` in the
  sealed evidence set.

The owner classified the boot as cold, supplied the report transcript and a
photograph, and confirmed return to TWRP. No host-issued reboot occurred, no
reflash was needed between boots, and no further attempt was made. The
authorization is exhausted. The owner's method note says the report was
transcribed with terminal wrapping; the photograph is authoritative where the
typed text omits a character. No exact boot timestamp was recorded and none is
invented.

### 1.1 Header and CMD3 result

The report header is:

```
GTA4L-EMMC H9T REPORT ver=1 rc=0 stop=0 sub=1 att=1 tr=0 r1=0 retry=0/0 step=13 log=4 hz=19200000
```

CMD3 was submitted once (`sub=1`, `att=1`) with the retry seam disabled
(`retry=0/0`). The `C3L` poll history is valid with 25 iterations: first
NORMAL `0000`, last NORMAL `0001`, zero-based terminal iteration 24. The
reserved terminal slot 19 records kind SUCCESS, NORMAL `0001` (command
complete), ERROR `0000`, RESPONSE0 `00000500`, valid 1 and ticks
`0x14f0`. No timeout capture exists (`C3T v=0`). `TRC n=6 ov=0` counts six
scheduled entries at iterations 0, 1, 2, 4, 8 and 16; unused scheduled slots
6-18 are kind `?`/valid 0 and slot 19 is reserved for the terminal entry.
`FAIL fail=0/0/0/00000000/0000/0000/00000000/0` is the intentional report
stop, not a recorded hardware failure.

Disposition: clean instrumented identification; the CMD3 completion-silent
failure did not reproduce. Initialization reliability remains unresolved. One
boot does not qualify repeatability, cold-start reliability or a failure rate,
and this closeout is not a fix or a D13 entry decision. The measured F2 verdict
for the one D10.5 cold boot and the p5b F5 verdict are unchanged.

### 1.2 Response interpretation limits

The six scheduled samples all read the same retained long-response window's
first word (the CMD2 CID prefix) sampled while polling before completion. They
are not CMD3 responses. The terminal slot reads `00000500` after completion. Comparing the terminal sample with the last
scheduled sample bounds a register change to an interval; it does not prove a
fresh card response, an exact arrival time or a card-state transition. The
driver applies its R1 error mask to the terminal RESPONSE0 and reported no
masked error (`r1=0`); it assigns the RCA in the CMD3 argument and does not
parse an RCA from the response. No stronger CMD3 or RCA claim is made.

## 2. Missing H9T kind column

Every H9T trace line is emitted with the format at
`package/Gta4lEmmcProbe.c:1488`:

```c
#define GTA4L_EMMC_H9T_LINE \
	"GTA4L-EMMC H9T T%02u %c %u %04x %04x %08x %u %08x%08x\n"
```

The on-screen report and the typed transcript omit the `%c` kind character;
`Txx` is the slot index, not the kind. The photograph confirms the omission is
not a transcription artifact. Expected kinds are `S` for a scheduled sample,
`C` for terminal success, `T` for terminal timeout, `E` for terminal error and
`?` for an unused slot.

### 2.1 Root cause in the panic-screen formatter

The panic report is rendered by the freestanding formatter in the sibling
UniXNU checkout, `scripts/xnu-panic-screen.c`, SHA-256
`54b950a7d83c30f7720d30551c6d5f10cea5e40dcccc5adb2b4c70a98240a68b`
(identical copy in the build tree `scripts/`). Its `format()` (lines 127-200)
supports `%s`, `%x`/`%X`, `%u`/`%d` and `%p`, with the `0` flag, width,
precision and `l`/`ll`/`z`/`h` length modifiers. It has no `%c` branch and no
default that emits the argument.

The walker still consumes one argument slot for every conversion at line 175:

```c
u64 v = args[used++];
```

For `%c`, that slot is consumed and no branch prints it, so the kind byte is
dropped while the following fields stay aligned. The fetched XNU kernel source
handles `%c` at `out/feeds/xnu-b7545ced/osfmk/kern/printf.c:459`, confirming
the divergence is in the panic-screen path rather than in the report format or
the probe.

The existing host probe harness does not exercise the real formatter: its
`panic()` implementation calls libc `vsnprintf` at
`scripts/sim-gta4l-emmc-probe/harness.c:1966`. libc handles `%c`, so the
probe suite could not observe the device behavior. This is a host-substitute
blind spot, not a probe-format regression.

### 2.2 Bounded reproducer

`scripts/test-gta4l-panic-format.py` extracts the pinned `put`, `number` and
`format` bodies from the formatter source, compiles them with a log sink, and
runs the exact H9T line format with synthetic arguments (slot 19, kind `C`,
iteration 7, NORMAL `0003`, ERROR `0000`, RESPONSE0 `00000900`, valid 1,
ticks `0x1234`). It asserts:

- the production formatter emits
  `GTA4L-EMMC H9T T19  7 0003 0000 00000900 1 0000000000001234` with a double
  space and no kind;
- the libc reference emits `GTA4L-EMMC H9T T19 C 7 …`;
- a control format without `%c` is identical in both paths;
- a textual mutation that adds a `%c` branch makes the production output
  match libc.

The test pins the formatter SHA-256 and the probe's H9T format string, so a
change to either fails it and forces a re-audit rather than silently changing
the finding. It uses no private response word and does not read device state.
The test passes.

### 2.3 Scope and next decision

This analysis does not qualify the framebuffer glyph path, the panic entry ABI
beyond the existing source comments, or any other conversion. The missing
kind column limits trace-format acceptance but does not erase the
independently visible clean header, completion and trace-field evidence.

No formatter or driver fix is made in this closeout. A proposed fix needs an
exact invariant (one `%c` consumes one argument and emits that byte), a
minimal seam, the baseline-failing reproducer above and a mutation that
restores the failure; implementation then requires a separate decision. The
same batch would need to update this finding, the test pin and the report
acceptance record.

## 3. Evidence and validation

Private raw evidence is preserved under the ignored output tree at
`out/gta4l/ram-root-emmc-d12.5-cmd3-timeline-evidence/observation-1/`:
the exact owner transcript, the original photograph bytes, the structured
observation record and this interpretation. The original sealed 40-file
manifest is unchanged; the closeout manifest
`out/gta4l/ram-root-emmc-d12.5-cmd3-timeline-evidence/closeout-sha256.txt`
covers this closeout and is verified after all writes. Raw response windows
and CID fragments remain private in that ignored tree and do not appear in
this document.

Host validation was run from `src/UniXNU-gta4l/` with `LC_ALL=C` and
`/usr/bin/python3.11 -B`:

```sh
for f in scripts/test-*.py; do
  out=$(LC_ALL=C /usr/bin/python3.11 -B "$f" 2>&1); rc=$?
  printf '%s rc=%d | %s\n' "$f" "$rc" "$(printf '%s\n' "$out" | tail -1)"
done
```

| Check | Result |
| --- | --- |
| 24-script `scripts/test-*.py` battery | 23 exited 0; `scripts/test-gta4l-storage-abi.py` exited 1 with `Derived candidate does not match the plan` |
| `scripts/test-gta4l-panic-format.py` | exited 0: production formatter drops the H9T `%c` kind, libc keeps it, the control matches and a `%c` mutation restores it |
| `scripts/test-gta4l-emmc.py` / `scripts/test-gta4l-emmc-probe.py` / `scripts/test-gta4l-emmc-evidence-counts.py` | exited 0: core 187 scenarios/115 mutation controls; probe 196/100 with 13 width-bound materializations and one oversized-report control; documented counts match |
| `scripts/check-gta4l.py` | exited 0: `SM6115 (gta4l) board layout verified.` |
| `tomllib` parse of `porting.toml`, `board.toml`, `feeds.toml` | exited 0; 13 porting areas |
| `git diff --check` | exited 0 |

The storage-ABI failure is a pre-existing artifact-staleness condition: the
shared `kernel.macho` is the D12.5 prelinked
`b1871d40ec0c6b5021ca6dd14a83951650255b40824399483679116cf6b4eef3`, while the
storage-ABI derive and provenance remain pinned to the p5b kernel `2f8238b9`.
The artifacts were not regenerated, and the failure is not a regression from
this batch. The results are also summarized in `docs/emmc-driver.md` §5.3.44
and `porting.toml`. No commit, push, candidate build, flash, boot or further
device access was made in this batch.
