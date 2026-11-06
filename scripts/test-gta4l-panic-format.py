#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Regression-test the H9T kind column in the panic-screen formatter.

The D12.5 stage-9 report is rendered by the freestanding formatter in the
sibling UniXNU checkout (scripts/xnu-panic-screen.c). Before the 2026-11-06
fix that formatter consumed a variadic slot for every conversion but had no %c
branch, so the H9T kind byte was dropped while the following fields stayed
aligned. The probe harness implements panic() with libc vsnprintf, which handles
%c, so the probe suite cannot observe the device behavior.

This test extracts the pinned put/number/format bodies, compiles them with a
memory sink, and compares the production output with libc for the exact H9T
line format with every kind (S, ?, C, T, E), an unaffected control without %c,
and an adjacent-conversion format that checks argument alignment and %%. It
then removes the %c branch textually and requires the mutated build to
reproduce the old defective output.

It also compiles the full formatter for aarch64-none-elf with the production
patch flags and the D12.5 panic-cave addresses, rejects writable data, and
requires the linked image to leave at least 32 bytes of the 0xA30-byte cave
free. The PANIC_TEXT log path is compiled out when the kernel has no
boot-progress buffer, which is what keeps the %c fix inside that budget.

The formatter SHA-256 and the probe format string are pinned, so a change to
either forces a re-audit. It uses only synthetic values and does not read
device state.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FORMATTER = ROOT.parent / 'UniXNU' / 'scripts' / 'xnu-panic-screen.c'
FORMATTER_SHA256 = (
    '8b71ed5a1fadbf3bea5839ce4b614f5b6e0c998e37131e60c1d37b3f2686ba95')
# D12.5 kernel.macho: stage-2 probe cave + 0x60 to PANIC_CAVE_END. The
# --panic-screen --no-reset recipe uses stages 1 and 2, so this is the
# tightest padding the current build can hand the formatter.
PANIC_CAVE_VA = 0xFFFFFFF0074185C0
PANIC_CAVE_END_VA = 0xFFFFFFF007418FF0
PANIC_SYMBOLS = (
    ('VINFO', 0xFFFFFFF007AA2970),
    ('MAX_MEM', 0xFFFFFFF007AA41F0),
    ('PAGE_TOTAL', 0xFFFFFFF007AA271C),
    ('PAGE_FREE', 0xFFFFFFF007AA26F8),
)
MIN_PANIC_SPARE = 32
FONT_RELATIVE = Path(
    'Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib/Font.h')
PROBE = ROOT / 'package' / 'Gta4lEmmcProbe.c'
H9T_FORMAT = 'GTA4L-EMMC H9T T%02u %c %u %04x %04x %08x %u %08x%08x\\n'
ADJACENT_FORMAT = 'A%cB%uC%%D%cE%04x\\n'
CONTROL_FORMAT = 'GTA4L-EMMC H9T X%02u %u %04x\\n'
MUTATION_OLD = (
    "    if (c == 'c') {\n"
    "      put (g, (u8)v);\n"
    "    } else if (c == 's') {\n")
MUTATION_NEW = "    if (c == 's') {\n"
KINDS = ('S', '?', 'C', 'T', 'E')


def h9t(kind):
    return (f'GTA4L-EMMC H9T T19 {kind} 7 0003 0000 00000900 1 '
            '0000000000001234')


def h9t_without_kind(kind):
    return h9t(kind).replace(f'T19 {kind} ', 'T19  ', 1)


def extract_body(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f'unterminated body for {signature!r}')


def check_probe_format(source):
    match = re.search(
        r'#define GTA4L_EMMC_H9T_LINE \\\n\s*("(?:[^"\\]|\\.)*")', source)
    if match is None:
        raise AssertionError('GTA4L_EMMC_H9T_LINE was not found')
    literal = match.group(1)
    if literal != f'"{H9T_FORMAT}"':
        raise AssertionError(
            f'GTA4L_EMMC_H9T_LINE changed: {literal} != "{H9T_FORMAT}"')


def find_font():
    roots = []
    uefi = os.environ.get('UNIXNU_UEFI')
    if uefi:
        roots.append(Path(uefi))
    roots += [ROOT.parent / 'Mu-Silicium',
              ROOT.parent / 'UniXNU' / 'uefi-sheng']
    for root in roots:
        font = root / FONT_RELATIVE
        if font.is_file():
            return font.parent
    return None


def compile_fit(directory):
    tools = {name: shutil.which(name) for name in
             ('clang', 'ld.lld', 'llvm-objcopy', 'llvm-nm',
              'llvm-readelf', 'llvm-size')}
    missing = [name for name, path in tools.items() if path is None]
    if missing:
        print(f'SKIP: missing aarch64 fit tools: {", ".join(missing)}')
        return False
    font_dir = find_font()
    if font_dir is None:
        print('SKIP: Mu-Silicium Font.h not found (set UNIXNU_UEFI)')
        return False

    obj = directory / 'panic-fit.o'
    elf = directory / 'panic-fit.elf'
    binary = directory / 'panic-fit.bin'
    linker = directory / 'panic-fit.ld'
    command = [
        tools['clang'], '--target=aarch64-none-elf', '-ffreestanding',
        '-fno-builtin', '-fno-stack-protector', '-mgeneral-regs-only',
        '-fno-pic', '-mcmodel=tiny', '-Oz',
        *(f'-D{name}={value:#x}UL' for name, value in PANIC_SYMBOLS),
        f'-I{font_dir}', '-c', str(FORMATTER), '-o', str(obj),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        raise AssertionError('aarch64 panic-screen compile failed')

    sizes = subprocess.run(
        [tools['llvm-size'], '--format=berkeley', str(obj)],
        capture_output=True, text=True, check=True).stdout.splitlines()[-1].split()
    if int(sizes[1]) or int(sizes[2]):
        raise AssertionError(
            f'panic screen has writable data (data {sizes[1]}, bss {sizes[2]})')

    linker.write_text(
        'SECTIONS { . = %#x; .text : { *(.text.a_entry) *(.text.a_common) '
        '*(.text*) *(.rodata*) } /DISCARD/ : { *(.comment) *(.note*) '
        '*(.eh_frame*) *(.ARM.*) } }\n' % PANIC_CAVE_VA)
    subprocess.run(
        [tools['ld.lld'], '-T', str(linker), '-e', 'panic_screen',
         str(obj), '-o', str(elf)], check=True)
    subprocess.run(
        [tools['llvm-objcopy'], '-O', 'binary', '-j', '.text', str(elf),
         str(binary)], check=True)

    load = None
    for line in subprocess.run(
            [tools['llvm-readelf'], '-S', str(elf)],
            capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.replace('[', ' ').replace(']', ' ').split()
        if len(parts) >= 4 and parts[1] == '.text':
            load = int(parts[3], 16)
    entry = None
    for line in subprocess.run(
            [tools['llvm-nm'], str(elf)],
            capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == 'panic_screen':
            entry = int(parts[0], 16)
    if load is None or entry != load:
        raise AssertionError(
            f'panic_screen is not the first .text byte (load {load}, '
            f'entry {entry})')

    length = binary.stat().st_size
    end = load + length
    spare = PANIC_CAVE_END_VA - end
    if spare < MIN_PANIC_SPARE:
        raise AssertionError(
            f'panic screen is {length:#x} bytes at {load:#x}, end {end:#x}, '
            f'only {spare} bytes before {PANIC_CAVE_END_VA:#x}')
    print(f'FIT: panic screen {length:#x} bytes at {load:#x}, '
          f'{spare} bytes spare before {PANIC_CAVE_END_VA:#x}')
    return True


def build(compiler, directory, source):
    output = directory / (source.stem + '-panic-format')
    command = [
        compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
        str(source), '-o', str(output),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        raise AssertionError('panic-format reproducer compile failed')
    return output


def run(binary, mode):
    result = subprocess.run([str(binary), mode], capture_output=True,
                            text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        raise AssertionError(f'{mode} reproducer failed')
    return result.stdout


def expected_output(kinds, without_kind=False):
    lines = []
    for kind in kinds:
        line = h9t_without_kind(kind) if without_kind else h9t(kind)
        lines.append(f'FMT[{kind}]:[{line}]')
        lines.append(f'REF[{kind}]:[{h9t(kind)}]')
    lines += [
        'CFMT[-]:[GTA4L-EMMC H9T X07 42 0001]',
        'CREF[-]:[GTA4L-EMMC H9T X07 42 0001]',
        ('AFMT[-]:[AB7C%DE1234]' if without_kind
         else 'AFMT[-]:[ASB7C%D?E1234]'),
        'AREF[-]:[ASB7C%D?E1234]',
    ]
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler')
    args = parser.parse_args()

    check_probe_format(PROBE.read_text())

    if not FORMATTER.is_file():
        print(f'SKIP: sibling formatter not present at {FORMATTER}')
        return 0
    formatter_bytes = FORMATTER.read_bytes()
    digest = hashlib.sha256(formatter_bytes).hexdigest()
    if digest != FORMATTER_SHA256:
        raise AssertionError(
            f'formatter SHA-256 changed ({digest}); re-audit the finding '
            f'and update the pin')

    formatter = formatter_bytes.decode()
    bodies = '\n\n'.join([
        extract_body(formatter, 'static void put (struct ctx *g, u8 c)'),
        extract_body(
            formatter, 'static void number (struct ctx *g, u64 value, '
                       'u32 base, u32 width, int zero)'),
        extract_body(
            formatter, 'static void format (struct ctx *g, const char *fmt, '
                       'const u64 *args)'),
    ])
    if MUTATION_OLD not in bodies:
        raise AssertionError(
            'the %c branch is missing; the pre-fix formatter drops the H9T '
            'kind')
    if bodies.count(MUTATION_OLD) != 1:
        raise AssertionError('%c mutation target is not unique')

    compiler = args.compiler or shutil.which('gcc') or shutil.which('clang')
    if compiler is None:
        print('SKIP: no host C compiler')
        return 0

    with tempfile.TemporaryDirectory(prefix='gta4l-panic-format-') as tmp:
        directory = Path(tmp)
        production = directory / 'production.c'
        mutated = directory / 'mutated.c'
        production.write_text(PRELUDE + bodies + harness())
        mutated.write_text(
            PRELUDE + bodies.replace(MUTATION_OLD, MUTATION_NEW, 1) +
            harness())
        production_binary = build(compiler, directory, production)
        mutated_binary = build(compiler, directory, mutated)

        output = run(production_binary, 'format')
        if output != expected_output(KINDS):
            print(output)
            raise AssertionError(
                'production formatter does not match libc for the H9T kinds')

        mutated_output = run(mutated_binary, 'format')
        if mutated_output != expected_output(KINDS, without_kind=True):
            print(mutated_output)
            raise AssertionError(
                'removing the %c branch did not reproduce the pre-fix defect')

    with tempfile.TemporaryDirectory(prefix='gta4l-panic-fit-') as tmp:
        fitted = compile_fit(Path(tmp))

    print('PASS: fixed formatter matches libc for S/?/C/T/E, the control and '
          'adjacent conversions; removing the %c branch reproduces the defect'
          + (f'; the ARM64 panic screen fits the D12.5 cave with at least '
             f'{MIN_PANIC_SPARE} bytes spare' if fitted else ''))
    return 0


def harness():
    return f'''
static u8 logbuf[4096];
static u32 fake_fb[4];

static void run(struct ctx *g, const char *fmt, const u64 *arguments)
{{
  memset(g, 0, sizeof(*g));
  g->base = (u8 *)fake_fb;
  g->log = logbuf;
  format(g, fmt, arguments);
}}

static void show(const char *label, char kind, char *text)
{{
  size_t length = strlen(text);
  if (length > 0 && text[length - 1] == ' ') {{
    text[length - 1] = 0;
  }}
  printf("%s[%c]:[%s]\\n", label, kind, text);
}}

static void show_reference(const char *label, char kind, char *text)
{{
  text[strcspn(text, "\\n")] = 0;
  printf("%s[%c]:[%s]\\n", label, kind, text);
}}

int main(void)
{{
  struct ctx g;
  const char kinds[] = {{'S', '?', 'C', 'T', 'E'}};
  u64 h9t[] = {{19, 0, 7, 0x0003, 0x0000, 0x00000900, 1, 0, 0x1234}};
  const u64 control[] = {{7, 42, 0x0001}};
  const u64 adjacent[] = {{'S', 7, '?', 0x1234}};
  char reference[256], control_reference[256], adjacent_reference[256];
  unsigned i;

  for (i = 0; i < sizeof(kinds); i++) {{
    h9t[1] = (u64)kinds[i];
    run(&g, "{H9T_FORMAT}", h9t);
    show("FMT", kinds[i], (char *)logbuf);
    snprintf(reference, sizeof(reference), "{H9T_FORMAT}",
        19u, (int)kinds[i], 7u, 0x0003u, 0x0000u, 0x00000900u, 1u, 0u, 0x1234u);
    show_reference("REF", kinds[i], reference);
  }}

  run(&g, "{CONTROL_FORMAT}", control);
  show("CFMT", '-', (char *)logbuf);
  snprintf(control_reference, sizeof(control_reference), "{CONTROL_FORMAT}",
      7u, 42u, 0x0001u);
  show_reference("CREF", '-', control_reference);

  run(&g, "{ADJACENT_FORMAT}", adjacent);
  show("AFMT", '-', (char *)logbuf);
  snprintf(adjacent_reference, sizeof(adjacent_reference), "{ADJACENT_FORMAT}",
      (int)'S', 7u, (int)'?', 0x1234u);
  show_reference("AREF", '-', adjacent_reference);
  return 0;
}}
'''


PRELUDE = '''#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned char u8;

#define SCALE 3
#define PANIC_TEXT_BYTES 2048
#define PANIC_TEXT 1

struct ctx {
  u8  *base;
  u32 rb, w, h, x, y;
  u8  *log;
  u32 logn;
};

static const u64 gFont[95] = {0};

static int readable (const char *s, const u64 *args)
{
  (void)s;
  (void)args;
  return 0;
}

'''


if __name__ == '__main__':
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
