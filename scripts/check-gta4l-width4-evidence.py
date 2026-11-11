#!/usr/bin/env python3
"""Check a width4 WIDTHREAD line against the final BOOT image window.

The width-mode report prints a 16-byte (32-hex-character) SHA-256 prefix for
the observed and reference copies of the pinned 1,024-byte BOOT window. This
host-only checker recomputes that prefix from the final packaged BOOT image
and requires both fields, the on-device match flag and the fixed case shape
to agree. It never reads a device, firmware or a candidate kernel.
"""
import argparse
import hashlib
import re
import tempfile
from pathlib import Path

WINDOW_BYTES = 1024
PREFIX_HEX = 32
LBA = 2255360
FIELDS = ('index', 'lba', 'start', 'length', 'span', 'actual', 'status',
          'subrequests', 'blocks', 'max', 'digest', 'predigest', 'match')
HEX = re.compile(r'^[0-9a-f]{32}$')


def window_prefix(image):
    data = Path(image).read_bytes()[:WINDOW_BYTES]
    if len(data) != WINDOW_BYTES:
        raise ValueError(f'{image}: shorter than {WINDOW_BYTES} bytes')
    return hashlib.sha256(data).hexdigest()[:PREFIX_HEX]


def parse_line(line):
    if not line.startswith('GTA4L-EMMC P5 WIDTHREAD: '):
        raise ValueError('not a WIDTHREAD line')
    fields = {}
    for token in line[len('GTA4L-EMMC P5 WIDTHREAD: '):].split():
        if '=' not in token:
            raise ValueError(f'malformed field {token!r}')
        name, value = token.split('=', 1)
        if name in fields:
            raise ValueError(f'duplicate field {name}')
        fields[name] = value
    missing = [name for name in FIELDS if name not in fields]
    extra = sorted(set(fields) - set(FIELDS))
    if missing:
        raise ValueError('missing fields ' + ','.join(missing))
    if extra:
        raise ValueError('unexpected fields ' + ','.join(extra))
    return fields


def check(line, expected_prefix):
    fields = parse_line(line)
    errors = []
    if not HEX.match(fields['digest']):
        errors.append('digest is not 32 lowercase hex characters')
    if not HEX.match(fields['predigest']):
        errors.append('predigest is not 32 lowercase hex characters')
    if fields['digest'] != expected_prefix:
        errors.append('digest does not match the final BOOT window')
    if fields['predigest'] != expected_prefix:
        errors.append('predigest does not match the final BOOT window')
    if fields['digest'] != fields['predigest']:
        errors.append('digest and predigest disagree')
    if fields['match'] != '1':
        errors.append('match is not 1')
    fixed = {
        'index': '0', 'lba': str(LBA), 'start': '0',
        'length': str(WINDOW_BYTES), 'span': str(WINDOW_BYTES),
        'actual': str(WINDOW_BYTES), 'status': '0', 'subrequests': '1',
        'blocks': '2', 'max': '2',
    }
    for name, value in fixed.items():
        if fields[name] != value:
            errors.append(f'{name}={fields[name]} expected {value}')
    return errors


def check_errors(line, expected_prefix):
    try:
        return check(line, expected_prefix)
    except ValueError as error:
        return [str(error)]


def self_test():
    with tempfile.TemporaryDirectory(prefix='gta4l-width4-check-') as tmp:
        image = Path(tmp) / 'boot.img'
        image.write_bytes(bytes(range(256)) * 4)
        prefix = window_prefix(image)
        good = (f'GTA4L-EMMC P5 WIDTHREAD: index=0 lba={LBA} start=0 '
                f'length={WINDOW_BYTES} span={WINDOW_BYTES} '
                f'actual={WINDOW_BYTES} status=0 subrequests=1 blocks=2 max=2 '
                f'digest={prefix} predigest={prefix} match=1')
        assert not check_errors(good, prefix), check_errors(good, prefix)
        changed = good.replace(
            f'digest={prefix}', 'digest=' + '0' * PREFIX_HEX)
        assert check_errors(changed, prefix)
        changed = good.replace(
            f'predigest={prefix}', 'predigest=' + '0' * PREFIX_HEX)
        assert check_errors(changed, prefix)
        assert check_errors(good.replace(' digest=' + prefix, ''), prefix)
        assert check_errors(good.replace(prefix, 'ABCDEF0123456789'), prefix)
        assert check_errors(good.replace(prefix, prefix[:12]), prefix)
        assert check_errors(good.replace('match=1', 'match=0'), prefix)
        assert check_errors(good.replace('lba=' + str(LBA), 'lba=0'), prefix)
        assert check_errors(good.replace('index=0', 'index=1'), prefix)
        assert check_errors(good.replace('index=0', 'index=x'), prefix)
        assert check_errors(good[:-12], prefix)
        image.write_bytes(bytes(reversed(range(256))) * 4)
        assert check_errors(good, window_prefix(image))
    print('PASS: width4 WIDTHREAD host digest and shape checks')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path,
                        help='final packaged BOOT image')
    parser.add_argument('--widthread', type=Path,
                        help='file containing one WIDTHREAD line')
    parser.add_argument('--line', help='WIDTHREAD line text')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.image is None or (args.widthread is None and args.line is None):
        parser.error('--image and one of --widthread/--line are required')
    prefix = window_prefix(args.image)
    line = (args.line if args.line is not None
            else args.widthread.read_text().strip())
    try:
        errors = check(line, prefix)
    except ValueError as error:
        errors = [str(error)]
    if errors:
        for error in errors:
            print(f'FAIL: {error}')
        return 1
    print(f'PASS: WIDTHREAD digest and predigest match {prefix}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
