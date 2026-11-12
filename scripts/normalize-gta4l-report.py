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

Width detection is conservative. A width is accepted only when at least two
physical lines have the same length in the 40..200 column range. If no width
is confident, no line is joined and a warning is recorded; pass --width when
the transcript is known to be wrapped at one width. Known continuation
lookalikes (`frames`, `pages`, `free`, `panic`, `debug`) start a new record.
"""
import argparse
import re
import sys
import tempfile
from pathlib import Path

LABEL = re.compile(r'^GTA4L-EMMC(?: [A-Z0-9]+)+: ')
NEW_RECORD = re.compile(
    r'^\s*(?:GTA4L-EMMC |frames\b|pages\b|free\b|panic\b|debug\b|'
    r'dmesg\b|\[|<|console:|.*[#$] )')
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
            if not token.startswith('@'):
                errors.append(f'bare field {token!r}')
            continue
        if not VALUE.match(token):
            errors.append(f'malformed field {token!r}')
    return errors


def detect_width(lines):
    counts = {}
    for line in lines:
        length = len(line.rstrip('\n'))
        if 40 <= length <= 200:
            counts[length] = counts.get(length, 0) + 1
    confident = [(length, count) for length, count in counts.items()
                 if count >= 2]
    if not confident:
        return None
    return max(confident)[0]


def normalize(lines, width):
    output = []
    joins = []
    warnings = []
    if width is None:
        warnings.append('ambiguous wrapping width; no lines joined '
                        '(pass --width when the wrap width is known)')
        return [line.rstrip('\n') for line in lines], joins, warnings
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
        width = 40
        physical = [logical[i:i + width] for i in range(0, len(logical), width)]
        raw = base / 'raw.txt'
        raw.write_text('\n'.join(physical + [
            'GTA4L-EMMC P5 WIDTHREAD: index=7 match=1',
            '[  12.345678] unrelated kernel line',
        ]) + '\n')
        lines = raw.read_text().splitlines()
        detected = detect_width(lines)
        assert detected == width, detected
        output, joins, warnings = normalize(lines, detected)
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
        bare = 'GTA4L-EMMC P5 SPEED: hs=1 bogus'
        assert check_line(bare), 'a bare field was accepted'
        long_line = 'GTA4L-EMMC P5 REPORT: value=' + '1' * 60
        frames = 'frames deadbeef'
        assert detect_width([long_line, frames]) is None
        output, joins, warnings = normalize([long_line, frames], None)
        assert output == [long_line, frames], output
        assert joins == [] and warnings, (joins, warnings)
        output, joins, _ = normalize([long_line, frames], 70)
        assert output == [long_line, frames], 'a frames line was joined'
        assert joins == []
        write_outputs(output, joins, warnings, base / 'out.txt', base / 'log')
        log = (base / 'log').read_text()
        assert 'summary joins=0 warnings=1' in log, log
    print('PASS: normalizer joins wrapped reports and refuses ambiguous cuts')


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
    width = args.width if args.width else detect_width(lines)
    output, joins, warnings = normalize(lines, width)
    write_outputs(output, joins, warnings, args.output, args.log)
    if args.log is None:
        for warning in warnings:
            print(f'WARNING: {warning}', file=sys.stderr)
    return 1 if warnings else 0


if __name__ == '__main__':
    raise SystemExit(main())
