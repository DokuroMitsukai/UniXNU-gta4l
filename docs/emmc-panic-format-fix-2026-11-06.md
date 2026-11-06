# gta4l H9T kind-column panic formatter fix (host-only)

**Status:** focused corrective finding, 2026-11-06. Follow-up to
`docs/emmc-d12.5-closeout-2026-11-06.md`. This batch is host-only: it made no
device access, boot, flash, candidate build, driver/transport change, commit or
push. It does not rewrite the D12.5 observation record or any earlier verdict.

## 1. Defect

The D12.5 closeout found that every H9T trace line is emitted with
`GTA4L-EMMC H9T T%02u %c %u %04x %04x %08x %u %08x%08x\n`
(`package/Gta4lEmmcProbe.c:1488`), but the photographed and typed report showed
`T19  7 ...` with a double space instead of `T19 C 7 ...`. The panic-screen
formatter in the sibling UniXNU checkout consumed one variadic slot for every
conversion at `scripts/xnu-panic-screen.c` and had branches for `%s`, `%x`/`%X`,
`%u`/`%d` and `%p`, but no `%c` branch. The kind byte was therefore consumed
and never emitted while the following fields stayed aligned. The probe
harness's `panic()` uses libc `vsnprintf`, so the probe suite could not observe
the defect.

The old formatter file hash was
`54b950a7d83c30f7720d30551c6d5f10cea5e40dcccc5adb2b4c70a98240a68b`. The
D12.5 candidate was built from that source and still contains the old
formatter.

## 2. Invariant and change

The exact invariant is: a plain `%c` consumes exactly one variadic slot and
passes its low byte to the existing character-output path. It must not change
any other conversion, the argument order or the report length bound. Width,
zero-padding, precision and length modifiers for `%c` are not added; the H9T
contract uses a plain `%c`.

The change in `src/UniXNU/scripts/xnu-panic-screen.c` is:

```c
    u64 v = args[used++];
    if (c == 'c') {
      put (g, (u8)v);
    } else if (c == 's') {
```

The file comment now lists `%c` alongside the existing conversions.

### 2.1 Size budget

The `%c` branch alone made the linked formatter 2615 bytes (`0xa37`) at the
D12.5 panic cave `0xfffffff0074185c0`, which is 7 bytes past
`PANIC_CAVE_END` `0xfffffff007418ff0`. The old formatter was 2591 bytes with
17 bytes spare. The build recipe uses `--panic-screen --no-reset`, so the
available padding is the 2608-byte (`0xa30`) region from the stage-2 cave plus
`0x60` to the end; a candidate built with only the `%c` branch would be refused
by `patch-xnu-entry-probe.py`.

The log path is only live when the kernel provides a `boot_progress_panic_text`
buffer (`PANIC_TEXT`). The D12.5 kernel has no `_boot_progress_panic_text`
symbol, so the path was dead there. The fix now compiles it only when
`PANIC_TEXT` is nonzero:

```c
#if PANIC_TEXT
  if (g->log && g->logn < PANIC_TEXT_BYTES - 1 && c >= 9 && c <= 126) {
    ...
  }
#endif
```

The same guard covers the `g.log`/`g.logn` setup and reset in
`panic_screen_main`. Kernels that define `PANIC_TEXT` keep the exact previous
behavior. On the D12.5 kernel this removes 96 bytes, producing a 2519-byte
(`0x9d7`) linked formatter with 89 bytes spare before `PANIC_CAVE_END`. The
new file hash is
`8b71ed5a1fadbf3bea5839ce4b614f5b6e0c998e37131e60c1d37b3f2686ba95`. No other
conversion, table or build-selection path was changed.

## 3. Regression test

`scripts/test-gta4l-panic-format.py` now pins the new formatter hash and the
unchanged probe H9T format string, extracts the real `put`, `number` and
`format` bodies, compiles them with a log sink, and checks exact full-output
equality with libc for:

- the H9T line with each observed kind: `S` scheduled, `?` unused, `C` terminal
  success, `T` terminal timeout and `E` terminal error;
- an unaffected control format without `%c`;
- the adjacent format `A%cB%uC%%D%cE%04x`, which verifies that `%c` keeps the
  following argument aligned and that `%%` consumes no slot.

It then removes the `%c` branch textually and requires the mutated build to
reproduce the old defective output exactly: `T19  7` for each H9T kind and
`AB7C%DE1234` for the adjacent format. The fixed formatter must match libc;
the mutated formatter must fail. The test uses synthetic values only, contains
no private response word and does not read device state.

The same test now also compiles the full formatter for `aarch64-none-elf` with
the exact `patch-xnu-entry-probe.py` flags and the D12.5 panic-cave and symbol
addresses. It rejects writable data, requires `panic_screen` to be the first
`.text` byte, and requires at least 32 bytes to remain before
`PANIC_CAVE_END`. The test prelude defines `PANIC_TEXT 1` so the log sink still
exercises the guarded path; the full-source fit compile leaves `PANIC_TEXT`
undefined, matching the D12.5 kernel. Missing cross tools or Mu-Silicium
`Font.h` make this phase print `SKIP`, not fail.

Run against the pre-fix formatter (`git show HEAD:scripts/xnu-panic-screen.c` at
the old hash) with the pin adjusted in a temporary copy, the test exits 1 with
`the %c branch is missing; the pre-fix formatter drops the H9T kind`. Run
against the unguarded `%c` formatter (hash `3915f34f...`, before the size
guard) with its pin adjusted, the behavior phase passes and the fit phase exits
1 with `panic screen is 0xa37 bytes ... only -7 bytes before ...`. On the
guarded formatter it exits 0 with `FIT: panic screen 0x9d7 bytes ... 89 bytes
spare`.

## 4. Validation

Host validation was run from `src/UniXNU-gta4l/` with `LC_ALL=C` and
`/usr/bin/python3.11 -B`:

| Check | Result |
| --- | --- |
| `scripts/test-gta4l-panic-format.py` | exited 0: fixed formatter matches libc for `S`/`?`/`C`/`T`/`E`, the control and adjacent conversions; removing the branch reproduces the defect; `FIT: panic screen 0x9d7 bytes ... 89 bytes spare` |
| Pre-fix formatter baseline | exited 1: `the %c branch is missing; the pre-fix formatter drops the H9T kind` |
| Unguarded `%c` formatter baseline | exited 1 at the fit phase: `panic screen is 0xa37 bytes ... only -7 bytes before ...` |
| 24-script `scripts/test-*.py` battery | 23 exited 0; `scripts/test-gta4l-storage-abi.py` exited 1 with `Derived candidate does not match the plan` (pre-existing artifact staleness, not regenerated) |
| `scripts/porting-record.py check --device ../UniXNU-gta4l` | exited 0: 13 structured areas checked |
| `scripts/check-gta4l.py` | exited 0: `SM6115 (gta4l) board layout verified.` |
| `tomllib` parse of `porting.toml`, `board.toml`, `feeds.toml` | exited 0; 13 porting areas |
| `scripts/test-gta4l-ram-root-fixture-recipe.py` | exited 0 after the affected baseline rows were refreshed |
| `git diff --check` in both repositories | exited 0 |

The storage-ABI failure is the same pre-existing condition recorded by the
D12.5 closeout: the shared `kernel.macho` is the D12.5 prelinked `b1871d40`,
while the storage-ABI derive and provenance remain pinned to the p5b kernel
`2f8238b9`. The artifacts were not regenerated.

Evidence is under the ignored
`out/gta4l/ram-root-emmc-panic-format-fix/`: the pre-fix and fixed formatter
copies, the formatter diff, the pre-fix failure log, full battery output,
host-check output, source hashes and a new SHA-256 manifest. The D12.5
closeout manifest remains an unchanged historical snapshot; this batch has its
own manifest because it changes files that the earlier manifest hashed.

## 5. Limits and next decisions

This fix does not qualify the framebuffer glyph path, any other conversion, or
the build/link path of a payload candidate. The fit result is specific to the
D12.5 kernel and the `--panic-screen --no-reset` recipe (stages 1 and 2,
2608 bytes available); a different kernel, a different stage set, or a
`PANIC_TEXT` build has a different budget and needs its own fit check.
`patch-xnu-entry-probe.py` still refuses any formatter that passes
`PANIC_CAVE_END`, so an unguarded or future growth fails the patch step rather
than overwriting padding. The `PANIC_TEXT` guard only removes dead code when
the kernel has no `_boot_progress_panic_text`; kernels that provide the buffer
keep the previous log behavior.

It does not build, flash or boot anything, and the D12.5 image still carries
the old formatter. A new candidate carrying this source and an on-device
observation need fresh explicit approval and a new budget; the one D12.5 boot
remains a clean non-reproduction, not a reliability qualification. F2 remains
measured PASS for the one D10.5 cold boot, p5b F5 is unchanged, and D13 still
requires its own reliability disposition and implementation decision. No
commit or push was made.
