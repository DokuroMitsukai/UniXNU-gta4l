#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""One entry point for the gta4l eMMC evidence checkers.

Each rung keeps its own predicate script. This dispatcher selects the script
for the rung's report mode so the closing checklist, records and host tests
name one command:

    python3.11 scripts/check-gta4l-emmc-evidence.py --mode widthpub \\
        --image out/gta4l/boot.img --report out/gta4l/report.txt

Arguments after --mode are passed through unchanged, including --self-test.
New rungs add one MODES entry and their predicate script; they do not add a
second dispatcher.
"""
import argparse
import subprocess
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
MODES = {
    'width4': 'check-gta4l-width4-evidence.py',
    'widthhold': 'check-gta4l-widthhold-evidence.py',
    'widthpub': 'check-gta4l-widthpub-evidence.py',
    'speed': 'check-gta4l-speed-evidence.py',
}
FUTURE = ('bulk', 'live', 'windows', 'root', 'dma', 'writes')


def run(mode, arguments):
    script = SCRIPTS / MODES[mode]
    if not script.is_file():
        print(f'FAIL: {script} is missing', file=sys.stderr)
        return 1
    completed = subprocess.run([sys.executable, str(script), *arguments])
    return completed.returncode


def self_test():
    errors = []
    for mode in sorted(MODES):
        completed = subprocess.run(
            [sys.executable, str(SCRIPTS / MODES[mode]), '--self-test'],
            capture_output=True, text=True)
        if completed.returncode != 0:
            errors.append(f'{mode} self-test failed: '
                          f'{completed.stdout.strip()} {completed.stderr.strip()}')
    for mode in FUTURE:
        if mode in MODES:
            errors.append(f'future mode {mode} is already selectable')
    with subprocess.Popen(
            [sys.executable, str(Path(__file__).resolve()),
             '--mode', 'not-a-mode'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as child:
        output, error = child.communicate()
        if child.returncode == 0:
            errors.append('an unknown mode was accepted')
        if 'unknown mode' not in (output + error):
            errors.append('an unknown mode did not name the error')
    for error in errors:
        print(f'FAIL: {error}')
    if errors:
        return 1
    print(f'PASS: {len(MODES)} evidence modes dispatch and self-test')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', help='report mode: ' + ', '.join(sorted(MODES)))
    parser.add_argument('--list-modes', action='store_true')
    args, extra = parser.parse_known_args()
    if args.list_modes:
        for mode in sorted(MODES):
            print(f'{mode}\t{MODES[mode]}')
        return 0
    if args.mode is None:
        if extra == ['--self-test']:
            return self_test()
        parser.error('--mode is required unless --list-modes or --self-test is used')
    if args.mode not in MODES:
        print(f'FAIL: unknown mode {args.mode!r}; '
              f'choose from {", ".join(sorted(MODES))}', file=sys.stderr)
        return 2
    return run(args.mode, extra)


if __name__ == '__main__':
    raise SystemExit(main())
