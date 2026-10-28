#!/usr/bin/env python3
"""Run the host-side simulated-register tests for the gta4l SDHCI probe.

The production Gta4lSdhciHostInit.c is copied and only its two architectural
instructions (generic counter read and DSB) are replaced with test hooks. The
stage order, preconditions, MMIO writes and panic paths remain the production
code. Minimal kernel-header stubs and a fake MMIO window are linked in.
"""
import argparse
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
TESTS = ('conflict', 'bus-power', 'reset-timeout', 'clock-timeout',
         'vendor-power')


def build(compiler, directory):
    source = SOURCE.read_text()
    header_include = '#include "Gta4lSdhciHostInit.h"'
    assert source.count(header_include) == 1
    source = source.replace(
        header_include,
        header_include + '\nuint64_t gta4l_sdhci_host_init_test_counter(void);'
        '\nvoid gta4l_sdhci_host_init_test_barrier(void);',
        1)

    counter = '\t__asm__ volatile("mrs %0, cntvct_el0" : "=r" (value));'
    barrier = '\t__asm__ volatile("dsb sy" ::: "memory");'
    assert source.count(counter) == 1, 'counter instruction changed'
    assert source.count(barrier) == 1, 'barrier instruction changed'
    source = source.replace(counter,
                            '\tvalue = gta4l_sdhci_host_init_test_counter();',
                            1)
    source = source.replace(barrier,
                            '\tgta4l_sdhci_host_init_test_barrier();', 1)

    probe = directory / 'probe.c'
    probe.write_text(source)
    binary = directory / 'sim'
    command = [
        compiler, '-std=c11', '-O0', '-g', '-pthread',
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
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    print('PASS: simulated register tests for conflict, bus power, reset '
          'timeout, unstable clock and vendor power request')
    return 0


if __name__ == '__main__':
    sys.exit(main())
