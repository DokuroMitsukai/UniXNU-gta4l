#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host test for the report normalizer and the evidence dispatcher.

The test always runs the normalizer and dispatcher self-tests. When the
gitignored N10.16, N10.23 and N10.24 evidence images and normalized reports
are present, it also re-runs the three historical PASS verdicts, first
wrapping the reports at 80 columns and joining them again, and then checks
that a one-character digest mutation is rejected. Missing evidence is
reported as SKIP; it is not an error.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'scripts'
EVIDENCE = ROOT / 'out' / 'gta4l'
CASES = (
    ('width4',
     EVIDENCE / 'ram-root-emmc-n10.16-width4-gatefix-candidate-evidence' /
     'boot-ram-root-emmc-n10.16-width4-gatefix.img',
     EVIDENCE / 'normalizer-check' / 'n10.16-report.txt',
     '--widthread'),
    ('widthhold',
     EVIDENCE / 'ram-root-emmc-n10.22-widthhold-candidate-evidence' /
     'boot-ram-root-emmc-n10.22-widthhold.img',
     EVIDENCE / 'normalizer-check' / 'n10.23-report.txt',
     '--report'),
    ('widthpub',
     EVIDENCE / 'ram-root-emmc-n10.24-widthpub-candidate-evidence' /
     'boot-ram-root-emmc-n10.24-widthpub.img',
     EVIDENCE / 'ram-root-emmc-n10.24-widthpub-candidate-evidence' /
     'hardware-report-n10.24-widthpub.txt',
     '--report'),
)


def run(*arguments):
    return subprocess.run([sys.executable, '-B', *map(str, arguments)],
                          capture_output=True, text=True)


def wrap(text, width):
    lines = []
    for line in text.splitlines():
        if line.startswith('GTA4L-EMMC') and len(line) > width:
            lines.extend(line[index:index + width]
                         for index in range(0, len(line), width))
        else:
            lines.append(line)
    return '\n'.join(lines) + '\n'


def cases():
    for mode, image, report, flag in CASES:
        if not image.is_file() or not report.is_file():
            print(f'SKIP: {mode} evidence is not present')
            continue
        with tempfile.TemporaryDirectory(prefix='gta4l-evidence-') as tmp:
            raw = Path(tmp) / 'wrapped.txt'
            joined = Path(tmp) / 'joined.txt'
            raw.write_text(wrap(report.read_text(), 80))
            normalized = run(SCRIPTS / 'normalize-gta4l-report.py',
                             '--input', raw, '--output', joined, '--width', 80)
            if normalized.returncode != 0:
                raise AssertionError(
                    f'{mode}: normalizer failed: {normalized.stdout} '
                    f'{normalized.stderr}')
            ok = run(SCRIPTS / 'check-gta4l-emmc-evidence.py', '--mode', mode,
                     '--image', image, flag, joined)
            if ok.returncode != 0:
                raise AssertionError(
                    f'{mode}: historical PASS was rejected: {ok.stdout} '
                    f'{ok.stderr}')
            text = joined.read_text()
            field = 'refdigest=' if 'refdigest=' in text else 'digest='
            at = text.index(field) + len(field)
            replacement = '0' if text[at] != '0' else '1'
            changed = text[:at] + replacement + text[at + 1:]
            joined.write_text(changed)
            bad = run(SCRIPTS / 'check-gta4l-emmc-evidence.py', '--mode', mode,
                      '--image', image, flag, joined)
            if bad.returncode == 0:
                raise AssertionError(
                    f'{mode}: a one-character digest mutation was accepted')
        print(f'PASS: {mode} normalized report and mutation control')


def main():
    for script, label in (
            ('normalize-gta4l-report.py', 'normalizer'),
            ('check-gta4l-emmc-evidence.py', 'dispatcher')):
        completed = run(SCRIPTS / script, '--self-test')
        if completed.returncode != 0:
            print(f'FAIL: {label} self-test: {completed.stdout} '
                  f'{completed.stderr}')
            return 1
    cases()
    print('PASS: evidence normalizer and dispatcher host test')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
