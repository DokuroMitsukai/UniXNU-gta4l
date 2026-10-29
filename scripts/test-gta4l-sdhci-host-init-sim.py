#!/usr/bin/env python3
"""Run the host-side simulated-register tests for the gta4l SDHCI probe.

The production Gta4lSdhciHostInit.c is copied and only its two architectural
instructions (generic counter read and DSB) and its six MMIO helpers are
replaced with test hooks. The stage order, preconditions, register offsets,
write values and panic paths remain the production code. The hooks record every
access so the tests can reject a transient write that leaves final memory
unchanged. A compiled-store inspection also disassembles the run function and
confirms a pointer-index mutation adds memory stores that the event trace would
not see; it is a scoped guardrail, not full dataflow. Minimal kernel-header
stubs and a fake MMIO window are linked in.
"""
import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'package/Gta4lSdhciHostInit.c'
HEADER = ROOT / 'package/Gta4lSdhciHostInit.h'
HARNESS = ROOT / 'scripts/sim-gta4l-sdhci-host-init/harness.c'
INCLUDE = ROOT / 'scripts/sim-gta4l-sdhci-host-init/include'
PACKAGE = ROOT / 'package'
TESTS = ('conflict', 'no-argument', 'map-failure', 'gcc-map-failure',
         'bus-power', 'reset-timeout', 'clock-timeout', 'reset-deadline',
         'clock-deadline', 'stuck-counter', 'clock-stuck-counter',
         'reset-delayed-success', 'clock-delayed-success',
         'gcc-reset-asserted', 'gcc-apps-branch', 'gcc-ahb-branch',
         'gcc-ice-branch', 'rcg-busy', 'rcg-config',
         'post-reset-version', 'post-reset-power', 'post-reset-clock',
         'post-reset-irq', 'irq-suppress', 'irq-suppress-failure',
         'vendor-power', 'vendor-change', 'vendor-arbitrary',
         'unexpected-version',
         'read-unavailable', 'card-clock', 'inhibit', 'status-change')

# Each mutation is applied to the copied production source. The runner builds
# it, runs the named scenario, and requires the scenario to fail for the
# expected reason. These are negative controls: a passing mutation would mean
# the test does not enforce the policy.
MUTATIONS = (
    ('unexpected-address',
     (('\tsdhci = (uintptr_t)sdhci_mapping;\n',
       '\tsdhci = (uintptr_t)sdhci_mapping;\n'
       '\tgta4l_sdhci_host_init_write32(sdhci, 0x30u, 0x0u);\n'),),
     'irq-suppress', 'forbidden write'),
    ('unexpected-width',
     (('\tgta4l_sdhci_host_init_write8(sdhci,\n'
       '\t    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET,\n'
       '\t    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL);',
       '\tgta4l_sdhci_host_init_write32(sdhci,\n'
       '\t    GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET,\n'
       '\t    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL);'),),
     'irq-suppress', 'forbidden write'),
    ('premature-ack',
     (('\tsdhci = (uintptr_t)sdhci_mapping;\n',
       '\tsdhci = (uintptr_t)sdhci_mapping;\n'
       '\tgta4l_sdhci_host_init_write32(sdhci, 0x24cu, 0x1u);\n'),),
     'vendor-power', 'forbidden write'),
    ('missing-prerequisite',
     (('if (power_before & GTA4L_SDHCI_HOST_INIT_POWER_BUS_POWER) {',
       'if (0) {'),),
     'bus-power', 'reset timeout'),
    ('excessive-polling',
     (('gta4l_sdhci_host_init_poll_clear8(uintptr_t base, uint32_t offset,\n'
       '    uint32_t *last_value, uint64_t *elapsed_ticks)\n'
       '{\n'
       '\tuint64_t start = gta4l_sdhci_host_init_counter();\n'
       '\tuint32_t i;\n'
       '\n'
       '\tfor (i = 0; i < GTA4L_SDHCI_HOST_INIT_MAX_POLL_ITERATIONS; i++) {',
       'gta4l_sdhci_host_init_poll_clear8(uintptr_t base, uint32_t offset,\n'
       '    uint32_t *last_value, uint64_t *elapsed_ticks)\n'
       '{\n'
       '\tuint64_t start = gta4l_sdhci_host_init_counter();\n'
       '\tuint32_t i;\n'
       '\n'
       '\tfor (i = 0; i < (GTA4L_SDHCI_HOST_INIT_MAX_POLL_ITERATIONS * 2u); '
       'i++) {'),),
     'reset-timeout', 'poll bound'),
    ('false-success',
     (('\t\tif (value == 0) {\n'
       '\t\t\t*elapsed_ticks = gta4l_sdhci_host_init_counter() - start;\n'
       '\t\t\treturn 1;\n'
       '\t\t}\n'
       '\t\tif ((gta4l_sdhci_host_init_counter() - start) >=\n'
       '\t\t    GTA4L_SDHCI_HOST_INIT_TIMEOUT_TICKS) {\n'
       '\t\t\tbreak;\n'
       '\t\t}\n'
       '\t}\n'
       '\t*elapsed_ticks = gta4l_sdhci_host_init_counter() - start;\n'
       '\treturn 0;\n}',
       '\t\tif (value == 0) {\n'
       '\t\t\t*elapsed_ticks = gta4l_sdhci_host_init_counter() - start;\n'
       '\t\t\treturn 1;\n'
       '\t\t}\n'
       '\t\tif ((gta4l_sdhci_host_init_counter() - start) >=\n'
       '\t\t    GTA4L_SDHCI_HOST_INIT_TIMEOUT_TICKS) {\n'
       '\t\t\tbreak;\n'
       '\t\t}\n'
       '\t}\n'
       '\t*elapsed_ticks = gta4l_sdhci_host_init_counter() - start;\n'
       '\treturn 1;\n}'),),
     'reset-timeout', "panic does not contain 'reset timeout'"),
    ('wrong-reset-value',
     (('\t    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL);',
       '\t    0x02u);'),),
     'irq-suppress', 'reset timeout'),
    ('wrong-clock-value',
     (('\tclock_write = (uint16_t)((clock_after_reset &\n'
       '\t    ~GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN) |\n'
       '\t    GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN);',
       '\tclock_write = 0x0005u;'),),
     'irq-suppress', 'card clock enabled'),
    ('interrupts-enabled',
     (('\t\tgta4l_sdhci_host_init_write32(sdhci,\n'
       '\t\t    GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE, 0);',
       '\t\tgta4l_sdhci_host_init_write32(sdhci,\n'
       '\t\t    GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE, 0xffffffffu);'),),
     'irq-suppress', 'interrupt suppression did not clear'),
    ('duplicate-write',
     (('\tgta4l_sdhci_host_init_write16(sdhci,\n'
       '\t    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, clock_write);\n'
       '\tgta4l_sdhci_host_init_barrier();',
       '\tgta4l_sdhci_host_init_write16(sdhci,\n'
       '\t    GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, clock_write);\n'
       '\tgta4l_sdhci_host_init_barrier();\n'
       '\tgta4l_sdhci_host_init_write32(sdhci,\n'
       '\t    GTA4L_SDHCI_HOST_INIT_REG_INT_ENABLE, 0);'),),
     'irq-suppress', 'unexpected write'),
)

ACCESSORS = (
    ('\treturn *(volatile uint32_t *)(base + offset);',
     '\treturn gta4l_sdhci_host_init_test_read32(base, offset);'),
    ('\treturn *(volatile uint16_t *)(base + offset);',
     '\treturn gta4l_sdhci_host_init_test_read16(base, offset);'),
    ('\treturn *(volatile uint8_t *)(base + offset);',
     '\treturn gta4l_sdhci_host_init_test_read8(base, offset);'),
    ('\t*(volatile uint32_t *)(base + offset) = value;',
     '\tgta4l_sdhci_host_init_test_write32(base, offset, value);'),
    ('\t*(volatile uint16_t *)(base + offset) = value;',
     '\tgta4l_sdhci_host_init_test_write16(base, offset, value);'),
    ('\t*(volatile uint8_t *)(base + offset) = value;',
     '\tgta4l_sdhci_host_init_test_write8(base, offset, value);'),
)

TRANSIENT_WRITE = (
    '\tsdhci = (uintptr_t)sdhci_mapping;\n',
    '\tsdhci = (uintptr_t)sdhci_mapping;\n'
    '\tgta4l_sdhci_host_init_write8(sdhci,\n'
    '\t    GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL, 0);\n',
)

POINTER_STORE = (
    '\tsdhci = (uintptr_t)sdhci_mapping;\n',
    '\tsdhci = (uintptr_t)sdhci_mapping;\n'
    '\tuint32_t *audit_ptr = (uint32_t *)(sdhci + 0x29);\n'
    '\taudit_ptr[0] = 0;\n',
)

STORE = re.compile(
    r'\b(?:str|strb|strh|stur|sturb|sturh|stp|push)\b|'
    r'\bmov(?:b|w|l|q)?\s+[^,\n]+,\s*[^,\n]*\(')


def count_stores(objdump, binary):
    """Count memory-store instructions in the production run function."""
    result = subprocess.run(
        [objdump, '-d', '--no-show-raw-insn', str(binary)],
        capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f'objdump failed: {result.stderr}')
    inside = False
    count = 0
    for line in result.stdout.splitlines():
        if line.rstrip().endswith('<gta4l_sdhci_host_init_run>:'):
            inside = True
            continue
        if inside and line.rstrip().endswith('>:'):
            break
        if inside and STORE.search(line):
            count += 1
    return count


def build(compiler, directory, name='sim', transient_write=False,
          pointer_store=False, mutations=()):
    source = SOURCE.read_text()
    header_include = '#include "Gta4lSdhciHostInit.h"'
    assert source.count(header_include) == 1
    hooks = (
        '\nuint64_t gta4l_sdhci_host_init_test_counter(void);'
        '\nvoid gta4l_sdhci_host_init_test_barrier(void);'
        '\nuint8_t gta4l_sdhci_host_init_test_read8(uintptr_t, uint32_t);'
        '\nuint16_t gta4l_sdhci_host_init_test_read16(uintptr_t, uint32_t);'
        '\nuint32_t gta4l_sdhci_host_init_test_read32(uintptr_t, uint32_t);'
        '\nvoid gta4l_sdhci_host_init_test_write8(uintptr_t, uint32_t, uint8_t);'
        '\nvoid gta4l_sdhci_host_init_test_write16(uintptr_t, uint32_t, '
        'uint16_t);'
        '\nvoid gta4l_sdhci_host_init_test_write32(uintptr_t, uint32_t, '
        'uint32_t);'
    )
    source = source.replace(header_include, header_include + hooks, 1)

    counter = '\t__asm__ volatile("mrs %0, cntvct_el0" : "=r" (value));'
    barrier = '\t__asm__ volatile("dsb sy" ::: "memory");'
    assert source.count(counter) == 1, 'counter instruction changed'
    assert source.count(barrier) == 1, 'barrier instruction changed'
    source = source.replace(counter,
                            '\tvalue = gta4l_sdhci_host_init_test_counter();',
                            1)
    source = source.replace(barrier,
                            '\tgta4l_sdhci_host_init_test_barrier();', 1)

    for old, new in ACCESSORS:
        assert source.count(old) == 1, f'accessor changed: {old!r}'
        source = source.replace(old, new, 1)

    if transient_write:
        old, new = TRANSIENT_WRITE
        assert source.count(old) == 1, 'mutation point changed'
        source = source.replace(old, new, 1)

    if pointer_store:
        old, new = POINTER_STORE
        assert source.count(old) == 1, 'mutation point changed'
        source = source.replace(old, new, 1)

    for old, new in mutations:
        assert source.count(old) == 1, f'mutation point changed: {old!r}'
        source = source.replace(old, new, 1)

    probe = directory / f'{name}.c'
    probe.write_text(source)
    binary = directory / name
    command = [
        compiler, '-std=c11', '-O0', '-g',
        '-Wall', '-Wextra',
        '-I', str(INCLUDE), '-I', str(PACKAGE),
        str(HARNESS), str(probe), '-o', str(binary),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr, file=sys.stderr)
        raise SystemExit('simulator build failed')
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler')
    args = parser.parse_args()

    if not SOURCE.is_file() or not HEADER.is_file() or not HARNESS.is_file():
        print('SKIP: host-init source, header or harness is not present')
        return 0
    compiler = args.compiler or shutil.which('gcc') or shutil.which('clang')
    if compiler is None:
        print('SKIP: no host C compiler')
        return 0

    directory = Path(tempfile.mkdtemp(prefix='gta4l-sdhci-sim-'))
    try:
        binary = build(compiler, directory)
        negative = build(compiler, directory, name='sim-negative',
                         transient_write=True)
        negative_result = subprocess.run([str(negative), 'reset-timeout'],
                                         capture_output=True, text=True)
        negative_output = negative_result.stdout + negative_result.stderr
        if negative_result.returncode == 0:
            print('FAIL: event trace accepted a transient power-control write',
                  file=sys.stderr)
            return 1
        if 'forbidden write' not in negative_output:
            print('FAIL: negative event-trace build failed for the wrong '
                  'reason:', file=sys.stderr)
            print(negative_output, file=sys.stderr)
            return 1
        print('PASS transient-write: event trace rejected a same-value '
              'power-control write')

        objdump = shutil.which('llvm-objdump') or shutil.which('objdump')
        if objdump is None:
            print('SKIP: no objdump for the compiled-store inspection')
        else:
            baseline_stores = count_stores(objdump, binary)
            pointer = build(compiler, directory, name='sim-pointer',
                            pointer_store=True)
            pointer_stores = count_stores(objdump, pointer)
            if pointer_stores <= baseline_stores:
                print('FAIL: compiled-store inspection missed a pointer-index '
                      'MMIO store', file=sys.stderr)
                return 1
            print(f'PASS compiled-store: pointer-index store added '
                  f'{pointer_stores - baseline_stores} memory store(s)')
        for name in TESTS:
            result = subprocess.run([str(binary), name],
                                    capture_output=True, text=True)
            if result.stdout:
                print(result.stdout, end='')
            if result.stderr:
                print(result.stderr, end='', file=sys.stderr)
            if result.returncode != 0:
                print(f'FAIL: simulated test {name}', file=sys.stderr)
                return 1
        for name, mutation, scenario, needle in MUTATIONS:
            mutated = build(compiler, directory, name=f'sim-{name}',
                            mutations=mutation)
            result = subprocess.run([str(mutated), scenario],
                                    capture_output=True, text=True)
            output = result.stdout + result.stderr
            if result.returncode == 0:
                print(f'FAIL mutation {name}: scenario {scenario} accepted '
                      'the violation', file=sys.stderr)
                return 1
            if needle not in output:
                print(f'FAIL mutation {name}: failed for the wrong reason:',
                      file=sys.stderr)
                print(output, file=sys.stderr)
                return 1
            print(f'PASS mutation {name}: {scenario} rejected the violation')
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    print('PASS: simulated register tests for conflict, absent argument, map '
          'failure, GCC map failure, bus power, reset timeout, unstable clock, '
          'reset deadline, clock deadline, stuck counters, delayed reset and '
          'clock completion, GCC reset/branch/RCG preconditions, post-reset '
          'version/power/clock/interrupt changes, interrupt suppression, '
          'interrupt suppression failure, vendor power, vendor change, '
          'arbitrary vendor status, unexpected version, unavailable read, '
          'card clock, inhibit and changing status')
    print('PASS: mutations for unexpected address, unexpected width, '
          'premature acknowledgement, missing prerequisite, excessive '
          'polling, false success, wrong reset value, wrong clock value, '
          'enabled interrupts and duplicate write were rejected')
    return 0


if __name__ == '__main__':
    sys.exit(main())
