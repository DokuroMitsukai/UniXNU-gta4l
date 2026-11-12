#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Join screen-wrapped GTA4L-EMMC report lines without changing values.

An owner transcript is pasted from a terminal, so one logical report line may
be split across several physical lines at the terminal width. This host-only
tool joins those physical lines back into one line per report label, records
every join with its source line numbers, and leaves every other line
unchanged. It never inserts, removes or normalises a value: a suspicious
value or token is reported, not repaired.

    python3.11 scripts/normalize-gta4l-report.py \\
        --input out/gta4l/report-raw.txt \\
        --output out/gta4l/report-normalized.txt \\
        --log out/gta4l/report-joins.log --width 80

The normalizer is intentionally conservative: a continuation is joined only
when the previous physical line reaches the wrapping width and the next line
does not start a new report, a kernel log record or a shell prompt.
"""
import argparse
import re
import sys
import tempfile
from pathlib import Path

LABEL = re.compile(r'^GTA4L-EMMC(?: [A-Z0-9]+)+: ')
NEW_RECORD = re.compile(
    r'^\s*(?:GTA4L-EMMC |\[|<|dmesg|console:|.*[#$] )')
VALUE = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*=\S+$')


def report_start(line):
    return bool(LABEL.match(line))


def new_record(line):
    return bool(NEW_RECORD.match(line))


def fragments(line):
    body = LABEL.sub('', line, count=1)
    return body.split()


def check_line(line):
    errors = []
    if not report_start(line):
        errors.append('not a GTA4L-EMMC report label')
        return errors
    tokens = fragments(line)
    if not tokens:
        errors.append('report line has no fields')
    for token in tokens:
        if '=' not in token:
            continue
        if not VALUE.match(token):
            errors.append(f'malformed field {token!r}')
    return errors


def auto_width(lines):
    lengths = [len(line.rstrip('\n')) for line in lines
               if report_start(line.rstrip('\n'))]
    longest = max(lengths, default=0)
    if longest >= 120:
        return 120
    if longest >= 80:
        return 80
    return longest


def normalize(lines, width):
    output = []
    joins = []
    warnings = []
    index = 0
    while index < len(lines):
        line = lines[index].rstrip('\n')
        if not report_start(line):
            output.append(line)
            index += 1
            continue
        start = index
        physical = [line]
        while index + 1 < len(lines):
            previous = physical[-1]
            following = lines[index + 1].rstrip('\n')
            if len(previous) < width - 1:
                break
            if report_start(following) or new_record(following):
                break
            if not following.strip():
                break
            physical.append(following)
            index += 1
        joined = ''.join(physical)
        output.append(joined)
        if len(physical) > 1:
            joins.append((start + 1, index + 1, joined))
        for error in check_line(joined):
            warnings.append(f'line {start + 1}: {error}')
        index += 1
    return output, joins, warnings


def write_outputs(output, joins, warnings, output_path, log_path):
    text = '\n'.join(output)
    if output_path is None:
        sys.stdout.write(text + '\n')
    else:
        Path(output_path).write_text(text + '\n')
    if log_path is not None:
        log = []
        for first, last, line in joins:
            log.append(f'join lines={first}-{last} bytes={len(line)} '
                       f'label={line.split(": ", 1)[0]}')
        for warning in warnings:
            log.append(f'warning {warning}')
        log.append(f'summary joins={len(joins)} warnings={len(warnings)}')
        Path(log_path).write_text('\n'.join(log) + '\n')


def self_test():
    with tempfile.TemporaryDirectory(prefix='gta4l-normalize-') as tmp:
        base = Path(tmp)
        logical = ('GTA4L-EMMC P5 WIDTHPUB: cases=8 passed=8 failed=0 '
                   'reason=0 complete=1 pub=1 refdigest=0123456789abcdef')
        width = 31
        physical = [logical[i:i + width] for i in range(0, len(logical), width)]
        raw = base / 'raw.txt'
        raw.write_text('\n'.join(physical + [
            'GTA4L-EMMC P5 WIDTHREAD: index=7 match=1',
            '[  12.345678] unrelated kernel line',
        ]) + '\n')
        lines = raw.read_text().splitlines()
        output, joins, warnings = normalize(lines, width)
        assert output[0] == logical, output[0]
        assert len(joins) == 1, joins
        assert warnings == [], warnings
        assert output[1] == 'GTA4L-EMMC P5 WIDTHREAD: index=7 match=1'
        assert output[2] == '[  12.345678] unrelated kernel line'
        assert output[1] not in joins[0][2], 'short line was joined'
        bad = base / 'bad.txt'
        bad.write_text('GTA4L-EMMC P5 WIDTHPUB: cases=\n')
        _, _, bad_warnings = normalize(bad.read_text().splitlines(), 80)
        assert bad_warnings, 'a malformed field was accepted'
        assert auto_width(lines[:len(physical)]) == width
        write_outputs(output, joins, warnings, base / 'out.txt', base / 'log')
        written = (base / 'out.txt').read_text().splitlines()
        assert written[0] == logical
        log = (base / 'log').read_text()
        assert 'join lines=1-' in log, log
        assert 'summary joins=1 warnings=0' in log, log
    print('PASS: normalizer joins wrapped reports, logs joins and keeps values')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--log', type=Path)
    parser.add_argument('--width', type=int,
                        help='terminal wrapping width; default: detect')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.input is None and not sys.stdin.isatty():
        lines = sys.stdin.read().splitlines()
    elif args.input is not None:
        lines = args.input.read_text().splitlines()
    else:
        parser.error('--input is required (or pipe the transcript)')
        return 2
    width = args.width if args.width else auto_width(lines)
    if width is None or width < 8:
        parser.error('could not detect a wrapping width; pass --width')
    output, joins, warnings = normalize(lines, width)
    write_outputs(output, joins, warnings, args.output, args.log)
    if args.log is None:
        for warning in warnings:
            print(f'WARNING: {warning}', file=sys.stderr)
    return 1 if warnings else 0


if __name__ == '__main__':
    raise SystemExit(main())
