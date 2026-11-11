#!/usr/bin/env python3
"""Check the widthhold WIDTHHOLD/WIDTHHOLDREAD lines against the BOOT image.

The widthhold report prints 16-byte (32-hex-character) SHA-256 prefixes for
the concatenated 4-bit window, the 1-bit reference window and the last case's
observed/reference slices. This host-only checker recomputes those prefixes
from the final packaged BOOT image, requires the success-line shape and
content agreement, and rejects a report whose digest or one-byte-changed
image does not match. It never reads a device, firmware or a candidate kernel.
"""
import argparse
import hashlib
import re
import tempfile
from pathlib import Path

WINDOW_BYTES = 4096
CASE_BYTES = 1024
CASE_START = 3072
CASE_INDEX = 3
CASE_LBA = 2255366
PREFIX_HEX = 32
HEX = re.compile(r'^[0-9a-f]{32}$')
SUMMARY_FIELDS = (
    'cases', 'passed', 'failed', 'reason', 'complete', 'switch', 'verified',
    'hb', 'ha', 'rb', 'cmd6', 'refs', 'refbytes', 'refblocks', 'refstatus',
    'refchunks', 'refstops', 'refstopfail', 'refdigest', 'reads', 'bytes',
    'blocks', 'subrequests', 'max', 'c18', 'stops', 'stopfail', 'match',
    'mismatches', 'fails', 'foreign', 'cancel', 'budget', 'obsdigest',
    'partbase', 'partsize', 'partro', 'revert', 'rverified', 'rhb', 'rha',
    'rcmd6', 'rstatus', 'limit', 'seg', 'cap')
READ_FIELDS = (
    'index', 'lba', 'start', 'length', 'span', 'actual', 'status',
    'subrequests', 'blocks', 'max', 'digest', 'predigest', 'match')
SUMMARY_LABEL = 'GTA4L-EMMC P5 WIDTHHOLD: '
READ_LABEL = 'GTA4L-EMMC P5 WIDTHHOLDREAD: '
FAIL_LABEL = 'GTA4L-EMMC P5 WIDTHHOLDFAIL: '
SUMMARY_FIXED = {
    'cases': '4', 'passed': '4', 'failed': '0', 'reason': '0',
    'complete': '1', 'switch': '4', 'verified': '1', 'refs': '1',
    'refbytes': str(WINDOW_BYTES), 'refblocks': '8', 'refstatus': '0',
    'refchunks': '4', 'refstops': '4', 'refstopfail': '0', 'reads': '4',
    'bytes': str(WINDOW_BYTES), 'blocks': '8', 'subrequests': '4',
    'max': '2', 'c18': '4', 'stops': '4', 'stopfail': '0', 'match': '1',
    'mismatches': '0', 'fails': '0', 'foreign': '0', 'cancel': '0',
    'budget': '0', 'partbase': '1154744320', 'partsize': '100663296',
    'partro': '1', 'revert': '1', 'rverified': '1', 'rstatus': '0',
    'limit': '64', 'seg': '1',
}
READ_FIXED = {
    'index': str(CASE_INDEX), 'lba': str(CASE_LBA), 'start': str(CASE_START),
    'length': str(CASE_BYTES), 'span': str(CASE_BYTES),
    'actual': str(CASE_BYTES), 'status': '0', 'subrequests': '1',
    'blocks': '2', 'max': '2', 'match': '1',
}
UNPINNED_NUMBERS = (
    'hb', 'ha', 'rb', 'cmd6', 'rhb', 'rha', 'rcmd6', 'cap')


def window_prefix(image, start=0, length=WINDOW_BYTES):
    with Path(image).open('rb') as handle:
        handle.seek(start)
        data = handle.read(length)
    if len(data) != length:
        raise ValueError(
            f'{image}: shorter than {start + length} bytes')
    return hashlib.sha256(data).hexdigest()[:PREFIX_HEX]


def parse_line(line, label, fields):
    line = line.strip()
    if not line.startswith(label):
        raise ValueError(f'not a {label.split()[-1].rstrip(":")} line')
    parsed = {}
    for token in line[len(label):].split():
        if '=' not in token:
            raise ValueError(f'malformed field {token!r}')
        name, value = token.split('=', 1)
        if name in parsed:
            raise ValueError(f'duplicate field {name}')
        parsed[name] = value
    missing = [name for name in fields if name not in parsed]
    extra = sorted(set(parsed) - set(fields))
    if missing:
        raise ValueError('missing fields ' + ','.join(missing))
    if extra:
        raise ValueError('unexpected fields ' + ','.join(extra))
    return parsed


def check_summary(line, expected_prefix):
    fields = parse_line(line, SUMMARY_LABEL, SUMMARY_FIELDS)
    errors = []
    for name, value in SUMMARY_FIXED.items():
        if fields[name] != value:
            errors.append(f'{name}={fields[name]} expected {value}')
    for name in UNPINNED_NUMBERS:
        if not fields[name].isdigit():
            errors.append(f'{name}={fields[name]} is not an unsigned integer')
    if fields['rcmd6'].isdigit() and not 1 <= int(fields['rcmd6']) <= 2:
        errors.append(f'rcmd6={fields["rcmd6"]} outside [1, 2]')
    if not HEX.match(fields['refdigest']):
        errors.append('refdigest is not 32 lowercase hex characters')
    if not HEX.match(fields['obsdigest']):
        errors.append('obsdigest is not 32 lowercase hex characters')
    if fields['refdigest'] != expected_prefix:
        errors.append('refdigest does not match the final BOOT window')
    if fields['obsdigest'] != expected_prefix:
        errors.append('obsdigest does not match the final BOOT window')
    if fields['refdigest'] != fields['obsdigest']:
        errors.append('refdigest and obsdigest disagree')
    return errors


def check_read(line, expected_prefix):
    fields = parse_line(line, READ_LABEL, READ_FIELDS)
    errors = []
    for name, value in READ_FIXED.items():
        if fields[name] != value:
            errors.append(f'{name}={fields[name]} expected {value}')
    if not HEX.match(fields['digest']):
        errors.append('digest is not 32 lowercase hex characters')
    if not HEX.match(fields['predigest']):
        errors.append('predigest is not 32 lowercase hex characters')
    if fields['digest'] != expected_prefix:
        errors.append('digest does not match the last BOOT case slice')
    if fields['predigest'] != expected_prefix:
        errors.append('predigest does not match the last BOOT case slice')
    if fields['digest'] != fields['predigest']:
        errors.append('digest and predigest disagree')
    return errors


def check_summary_errors(line, expected_prefix):
    try:
        return check_summary(line, expected_prefix)
    except ValueError as error:
        return [str(error)]


def check_read_errors(line, expected_prefix):
    try:
        return check_read(line, expected_prefix)
    except ValueError as error:
        return [str(error)]


def check_lines(summary, read, image):
    errors = []
    if not summary:
        errors.append('missing WIDTHHOLD line')
    if not read:
        errors.append('missing WIDTHHOLDREAD line')
    if errors:
        return errors
    try:
        window = window_prefix(image)
    except (OSError, ValueError) as error:
        return [str(error)]
    try:
        case = window_prefix(image, CASE_START, CASE_BYTES)
    except (OSError, ValueError) as error:
        return [str(error)]
    errors.extend(check_summary(summary, window))
    errors.extend(check_read(read, case))
    return errors


def extract_report(lines):
    summary = []
    read = []
    errors = []
    for line in lines:
        line = line.strip()
        if line.startswith(SUMMARY_LABEL):
            summary.append(line)
        elif line.startswith(READ_LABEL):
            read.append(line)
        elif line.startswith(FAIL_LABEL):
            errors.append('WIDTHHOLDFAIL line present in the report')
    if len(summary) != 1:
        errors.append(f'expected one WIDTHHOLD line, found {len(summary)}')
    if len(read) != 1:
        errors.append(f'expected one WIDTHHOLDREAD line, found {len(read)}')
    return summary, read, errors


def self_test():
    with tempfile.TemporaryDirectory(prefix='gta4l-widthhold-check-') as tmp:
        image = Path(tmp) / 'boot.img'
        image.write_bytes(bytes(range(256)) * 24)
        changed = Path(tmp) / 'changed.img'
        changed.write_bytes(bytes(range(256)) * 24)
        data = bytearray(changed.read_bytes())
        data[CASE_START] ^= 0xff
        changed.write_bytes(bytes(data))
        window = window_prefix(image)
        case = window_prefix(image, CASE_START, CASE_BYTES)
        other = window_prefix(changed)
        other_case = window_prefix(changed, CASE_START, CASE_BYTES)
        summary = (
            f'{SUMMARY_LABEL}cases=4 passed=4 failed=0 reason=0 complete=1 '
            'switch=4 verified=1 hb=0 ha=2 rb=0 cmd6=1 refs=1 '
            f'refbytes={WINDOW_BYTES} refblocks=8 refstatus=0 refchunks=4 '
            'refstops=4 refstopfail=0 '
            f'refdigest={window} reads=4 bytes={WINDOW_BYTES} blocks=8 '
            'subrequests=4 max=2 c18=4 stops=4 stopfail=0 match=1 '
            f'mismatches=0 fails=0 foreign=0 cancel=0 budget=0 '
            f'obsdigest={window} partbase=1154744320 partsize=100663296 '
            'partro=1 revert=1 rverified=1 rhb=2 rha=0 rcmd6=1 rstatus=0 '
            'limit=64 seg=1 cap=61071360')
        read = (
            f'{READ_LABEL}index={CASE_INDEX} lba={CASE_LBA} '
            f'start={CASE_START} length={CASE_BYTES} span={CASE_BYTES} '
            f'actual={CASE_BYTES} status=0 subrequests=1 blocks=2 max=2 '
            f'digest={case} predigest={case} match=1')
        assert not check_lines(summary, read, image), \
            check_lines(summary, read, image)
        assert not check_summary_errors(summary.replace('rb=0', 'rb=1'), window)
        assert check_lines(summary, read, changed), \
            'one-byte-changed image was accepted'
        assert check_lines(
            summary.replace(window, other), read, image), \
            'a digest from a different image was accepted'
        assert check_lines(
            summary, read.replace(case, other_case), image)
        assert check_summary_errors(
            summary.replace(f'refdigest={window}', f'refdigest={window[:12]}'),
            window)
        assert check_summary_errors(
            summary.replace(f'obsdigest={window}', f'obsdigest={window.upper()}'),
            window)
        assert check_summary_errors(
            summary.replace(f'obsdigest={window}', 'obsdigest=' + '0' * 32),
            window)
        assert check_summary_errors(
            summary.replace('refdigest=' + window, ''), window)
        assert check_summary_errors(summary + ' extra=1', window)
        assert check_summary_errors(summary.replace('complete=1', 'complete=0'),
                             window)
        assert check_summary_errors(summary.replace('mismatches=0', 'mismatches=1'),
                             window)
        assert check_summary_errors(summary.replace('rcmd6=1', 'rcmd6=0'), window)
        assert check_summary_errors(summary.replace('rcmd6=1', 'rcmd6=3'), window)
        assert check_summary_errors(summary.replace('hb=0', 'hb=x'), window)
        assert check_read_errors(read.replace('match=1', 'match=0'), case)
        assert check_read_errors(read.replace(f'index={CASE_INDEX}', 'index=2'), case)
        assert check_read_errors(read.replace(f'lba={CASE_LBA}', 'lba=0'), case)
        assert check_read_errors(read.replace(f'digest={case}', 'digest=' +
                                       case[:12]), case)
        assert check_read_errors(read.replace(f'predigest={case}', ''), case)
        short = Path(tmp) / 'short.img'
        short.write_bytes(bytes(WINDOW_BYTES - 1))
        assert check_lines(summary, read, short)
        report = '\n'.join([
            'GTA4L-EMMC P5 MEDIA: media=1 part=1',
            summary,
            read,
        ])
        found_summary, found_read, errors = extract_report(
            report.splitlines())
        assert not errors, errors
        assert check_lines(found_summary[0], found_read[0], image) == []
        _summary, _read, errors = extract_report(
            (report + '\n' + FAIL_LABEL + 'index=0 status=11').splitlines())
        assert errors, 'WIDTHHOLDFAIL was not rejected'
        _summary, _read, errors = extract_report(report.splitlines()[:-1])
        assert errors, 'a missing WIDTHHOLDREAD line was not rejected'
    print('PASS: widthhold host digest, shape, mismatch and truncation checks')


def read_source(path, text):
    if path is not None:
        return path.read_text().strip()
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path,
                        help='final packaged BOOT image')
    parser.add_argument('--report', type=Path,
                        help='terminal report containing both lines')
    parser.add_argument('--summary', type=Path,
                        help='file containing one WIDTHHOLD line')
    parser.add_argument('--read', type=Path,
                        help='file containing one WIDTHHOLDREAD line')
    parser.add_argument('--summary-line', help='WIDTHHOLD line text')
    parser.add_argument('--read-line', help='WIDTHHOLDREAD line text')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.image is None:
        parser.error('--image is required unless --self-test is used')
    if args.report is not None:
        if (args.summary is not None or args.summary_line is not None or
                args.read is not None or args.read_line is not None):
            parser.error('--report cannot be combined with line sources')
        summary, read, errors = extract_report(
            args.report.read_text().splitlines())
        summary = summary[0] if summary else ''
        read = read[0] if read else ''
    else:
        if ((args.summary is None) == (args.summary_line is None) or
                (args.read is None) == (args.read_line is None)):
            parser.error('exactly one of --summary/--summary-line and one of '
                         '--read/--read-line are required without --report')
        summary = read_source(args.summary, args.summary_line)
        read = read_source(args.read, args.read_line)
        errors = []
    errors.extend(check_lines(summary, read, args.image))
    if errors:
        for error in errors:
            print(f'FAIL: {error}')
        return 1
    print('PASS: WIDTHHOLD and WIDTHHOLDREAD digests match the BOOT image')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
