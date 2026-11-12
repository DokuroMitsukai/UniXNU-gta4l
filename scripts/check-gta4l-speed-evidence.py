#!/usr/bin/env python3
"""Check a gta4l S1 SPEED line against the final BOOT image.

The S1 report prints the SHA-256 of the first 1 MiB of the BOOT partition
twice, the winning width and the timing revert fields. This host-only
checker recomputes the digest from the final packaged image, requires the
success shape, and rejects a mutated report or a changed image. It never
reads a device.
"""
import argparse
import hashlib
import re
import tempfile
from pathlib import Path

WINDOW_BYTES = 1024 * 1024
PREFIX_HEX = 64
HEX32 = re.compile(r'^[0-9a-f]{32}$')
HEX64 = re.compile(r'^[0-9a-f]{64}$')
HEX8 = re.compile(r'^[0-9a-f]{8}$')
HEX2 = re.compile(r'^[0-9a-f]{2}$')
LABEL = 'GTA4L-EMMC P5 SPEED: '
FIELDS = (
    'hs', 'hsrb', 'rcg', 'hispd', 'wtry', 'wid', 'verified', 'hb', 'ha',
    'cmd6', 'base', 'bytes1', 'ticks1', 'dig1', 'bytes2', 'ticks2',
    'dig2', 'req', 'fails', 'cancel', 'budget', 'partial', 'pub',
    'partdiag', 'revert', 'rverified', 'rhb', 'rha', 'rcmd6', 'rstatus',
    'hsrevert', 'hsverified', 'rhsb', 'rhs', 'rclk', 'limit', 'seg', 'cap')
FIXED = {
    'hs': '1', 'verified': '1', 'base': '1', 'cmd6': '1',
    'bytes1': str(WINDOW_BYTES), 'bytes2': str(WINDOW_BYTES),
    'req': '32', 'fails': '0', 'cancel': '0', 'budget': '0',
    'partial': '0', 'pub': '1', 'partdiag': '0', 'revert': '1',
    'rverified': '1', 'rstatus': '0', 'hsrevert': '1',
    'hsverified': '1', 'rhs': '0', 'limit': '64', 'seg': '1',
    'cap': '61071360',
}
UNSIGNED = (
    'hsrb', 'cmd6', 'ticks1', 'ticks2', 'req', 'rcmd6', 'rhsb', 'limit',
    'seg', 'cap')


def window_prefix(image):
    with Path(image).open('rb') as handle:
        data = handle.read(WINDOW_BYTES)
    if len(data) != WINDOW_BYTES:
        raise ValueError(f'{image}: shorter than {WINDOW_BYTES} bytes')
    return hashlib.sha256(data).hexdigest()


def parse_line(line):
    line = line.strip()
    if not line.startswith(LABEL):
        raise ValueError('not a SPEED line')
    fields = {}
    for token in line[len(LABEL):].split():
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
    for name, value in FIXED.items():
        if fields[name] != value:
            errors.append(f'{name}={fields[name]} expected {value}')
    for name in UNSIGNED:
        if not fields[name].isdigit():
            errors.append(f'{name}={fields[name]} is not an unsigned integer')
    for name in ('wtry', 'wid'):
        if fields[name] not in ('8', '4'):
            errors.append(f'{name}={fields[name]} is not 8 or 4')
    if fields['wid'] == '8' and fields['wtry'] != '8':
        errors.append('wid=8 requires wtry=8')
    if not HEX64.match(fields['dig1']) or not HEX64.match(fields['dig2']):
        errors.append('digest is not 64 lowercase hex characters')
    if fields['dig1'] != fields['dig2']:
        errors.append('dig1 and dig2 disagree')
    if fields['dig1'] != expected_prefix:
        errors.append('dig1 does not match the final BOOT window')
    for name in ('rcg', 'rclk'):
        if not HEX8.match(fields[name]):
            errors.append(f'{name} is not 8 lowercase hex characters')
    for name in ('hispd', 'hb', 'ha', 'rhb', 'rha'):
        if not HEX2.match(fields[name]):
            errors.append(f'{name} is not 2 lowercase hex characters')
    try:
        hispd = int(fields['hispd'], 16)
        ha = int(fields['ha'], 16)
        rha = int(fields['rha'], 16)
    except ValueError:
        hispd = ha = rha = 0
    if hispd & 0x04:
        errors.append('hispd is set; the primary S1 configuration keeps it clear')
    if fields['wid'] == '8' and not (ha & 0x20):
        errors.append('ha does not carry the 8-bit host width')
    if fields['wid'] == '4' and not (ha & 0x02):
        errors.append('ha does not carry the 4-bit host width')
    if rha & 0x22:
        errors.append('rha still carries a wide host width')
    if fields['ticks1'].isdigit() and int(fields['ticks1']) == 0:
        errors.append('ticks1 is zero')
    if fields['ticks2'].isdigit() and int(fields['ticks2']) == 0:
        errors.append('ticks2 is zero')
    return errors


def check_errors(line, expected_prefix):
    try:
        return check(line, expected_prefix)
    except ValueError as error:
        return [str(error)]


def self_test():
    with tempfile.TemporaryDirectory(prefix='gta4l-speed-check-') as tmp:
        image = Path(tmp) / 'boot.img'
        image.write_bytes(bytes(range(256)) * (WINDOW_BYTES // 256))
        digest = window_prefix(image)
        line = (
            f'{LABEL}hs=1 hsrb=0 rcg=0000020b hispd=02 wtry=8 wid=8 '
            'verified=1 hb=00 ha=20 cmd6=1 base=1 '
            f'bytes1={WINDOW_BYTES} ticks1=1000 dig1={digest} '
            f'bytes2={WINDOW_BYTES} ticks2=1000 dig2={digest} '
            'req=32 fails=0 cancel=0 budget=0 partial=0 pub=1 partdiag=0 '
            'revert=1 rverified=1 rhb=20 rha=00 rcmd6=1 rstatus=0 '
            'hsrevert=1 hsverified=1 rhsb=00 rhs=0 rclk=00000007 '
            'limit=64 seg=1 cap=61071360')
        assert not check_errors(line, digest), check_errors(line, digest)
        changed = Path(tmp) / 'changed.img'
        data = bytearray(image.read_bytes())
        data[0] ^= 0xff
        changed.write_bytes(bytes(data))
        other = window_prefix(changed)
        assert check_errors(line, other), 'a digest from another image passed'
        assert check_errors(line.replace(digest, other), digest)
        assert check_errors(line.replace('wid=8', 'wid=4'), digest)
        assert check_errors(line.replace('hispd=02', 'hispd=06'), digest)
        assert check_errors(line.replace('ha=20', 'ha=02'), digest)
        assert check_errors(line.replace('rha=00', 'rha=20'), digest)
        assert check_errors(line.replace('hsrevert=1', 'hsrevert=0'), digest)
        assert check_errors(line.replace('rhs=0', 'rhs=1'), digest)
        assert check_errors(line.replace('partial=0', 'partial=1'), digest)
        assert check_errors(line.replace('req=32', 'req=31'), digest)
        assert check_errors(line.replace('ticks1=1000', 'ticks1=0'), digest)
        assert check_errors(line.replace('rcg=0000020b', 'rcg=20b'), digest)
        assert check_errors(line.replace('pub=1', 'pub=0'), digest)
        assert check_errors(line.replace('partdiag=0', 'partdiag=1'), digest)
        assert check_errors(line.replace('dig2=' + digest, ''), digest)
        assert check_errors(line + ' extra=1', digest)
    print('PASS: SPEED host digest, width, revert and mutation checks')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--line')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.image is None or (args.report is None and args.line is None):
        parser.error('--image and one of --report/--line are required')
    prefix = window_prefix(args.image)
    line = args.line if args.line is not None else args.report.read_text()
    errors = check_errors(line, prefix)
    if errors:
        for error in errors:
            print(f'FAIL: {error}')
        return 1
    print(f'PASS: SPEED digest and fields match {prefix[:16]}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
