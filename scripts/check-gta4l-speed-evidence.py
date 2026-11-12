#!/usr/bin/env python3
"""Check a gta4l S1 SPEED line against the final BOOT image.

The S1 report prints the SHA-256 of the first 4 KiB of the BOOT partition
twice, the winning width, the clock state and the combined width/timing
revert fields. This host-only checker recomputes the digest from the final
packaged image, requires the success shape, rejects well-formed but
incorrect values, and rejects a mutated report or a changed image. It never
reads a device.
"""
import argparse
import hashlib
import re
import tempfile
from pathlib import Path

WINDOW_BYTES = 4096
REQUESTS = 8
HEX32 = re.compile(r'^[0-9a-f]{32}$')
HEX8 = re.compile(r'^[0-9a-f]{8}$')
HEX2 = re.compile(r'^[0-9a-f]{2}$')
LABEL = 'GTA4L-EMMC P5 SPEED: '
FIELDS = (
    'hs', 'hsrb', 'hsa', 'rcg', 'hispd', 'wtry', 'wfall', 'wroll',
    'wid', 'verified', 'hb', 'ha', 'cmd6', 'base', 'bytes1', 'ticks1',
    'hz', 'dig1', 'bytes2', 'ticks2', 'dig2', 'req', 'fails', 'cancel',
    'budget', 'partial', 'pub', 'partdiag', 'revert', 'rverified',
    'rhb', 'rha', 'rcmd6', 'rstatus', 'hsrevert', 'hsverified',
    'rhsb', 'rhs', 'rclk', 'limit', 'seg', 'cap')
FIXED = {
    'hs': '1', 'hsrb': '0', 'hsa': '1', 'rcg': '0000020b',
    'verified': '1', 'base': '1',
    'bytes1': str(WINDOW_BYTES), 'bytes2': str(WINDOW_BYTES),
    'req': str(REQUESTS), 'fails': '0', 'cancel': '0', 'budget': '0',
    'partial': '0', 'pub': '1', 'partdiag': '0', 'revert': '1',
    'rverified': '1', 'rstatus': '0', 'hsrevert': '1',
    'hsverified': '1', 'rhsb': '1', 'rhs': '0', 'rclk': '400000',
    'limit': '64', 'seg': '1', 'cap': '61071360',
}
UNSIGNED = (
    'hsrb', 'hsa', 'cmd6', 'ticks1', 'hz', 'ticks2', 'req', 'rcmd6',
    'rhsb', 'rhs', 'rclk', 'limit', 'seg', 'cap')
WIDTH = {'8': 0x20, '4': 0x02}
WIDTH_BITS = 0x22


def window_prefix(image):
    with Path(image).open('rb') as handle:
        data = handle.read(WINDOW_BYTES)
    if len(data) != WINDOW_BYTES:
        raise ValueError(f'{image}: shorter than {WINDOW_BYTES} bytes')
    return hashlib.sha256(data).hexdigest()[:32]


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


def extract_report(lines):
    found = [line.strip() for line in lines if line.strip().startswith(LABEL)]
    if len(found) != 1:
        raise ValueError(f'expected one SPEED line, found {len(found)}')
    return found[0]


def check(line, expected_prefix):
    fields = parse_line(line)
    errors = []
    for name, value in FIXED.items():
        if fields[name] != value:
            errors.append(f'{name}={fields[name]} expected {value}')
    for name in UNSIGNED:
        if not fields[name].isdigit():
            errors.append(f'{name}={fields[name]} is not an unsigned integer')
    if fields['wtry'] != '8':
        errors.append('wtry must be 8; S1 always tries 8-bit first')
    if fields['wfall'] not in ('0', '1'):
        errors.append('wfall is not 0 or 1')
    if fields['wroll'] not in ('0', '1'):
        errors.append('wroll is not 0 or 1')
    if fields['wid'] not in WIDTH:
        errors.append(f'wid={fields["wid"]} is not 8 or 4')
    elif fields['wid'] == '8' and fields['wfall'] != '0':
        errors.append('wid=8 requires wfall=0')
    elif fields['wid'] == '8' and fields['wroll'] != '0':
        errors.append('wid=8 requires wroll=0')
    elif fields['wid'] == '4' and fields['wfall'] != '1':
        errors.append('wid=4 requires wfall=1')
    elif fields['wid'] == '4' and fields['wroll'] != '1':
        errors.append('wid=4 requires a verified 8-bit rollback (wroll=1)')
    if not HEX32.match(fields['dig1']) or not HEX32.match(fields['dig2']):
        errors.append('digest is not 32 lowercase hex characters')
    if fields['dig1'] != fields['dig2']:
        errors.append('dig1 and dig2 disagree')
    if fields['dig1'] != expected_prefix:
        errors.append('dig1 does not match the final BOOT window')
    if not HEX8.match(fields['rcg']):
        errors.append('rcg is not 8 lowercase hex characters')
    for name in ('hispd', 'hb', 'ha', 'rhb', 'rha'):
        if not HEX2.match(fields[name]):
            errors.append(f'{name} is not 2 lowercase hex characters')
    try:
        hispd = int(fields['hispd'], 16)
        hb = int(fields['hb'], 16)
        ha = int(fields['ha'], 16)
        rhb = int(fields['rhb'], 16)
        rha = int(fields['rha'], 16)
    except ValueError:
        hispd = hb = ha = rhb = rha = 0
    if hispd & 0x04:
        errors.append('hispd is set; the primary S1 configuration keeps it clear')
    if hispd & WIDTH_BITS:
        errors.append('hispd carries a wide host width before the switch')
    if fields['wid'] in WIDTH:
        want = WIDTH[fields['wid']]
        if (hb & 0x22) != 0:
            errors.append('hb carries a wide host width before the switch')
        if (ha & 0x22) != want:
            errors.append('ha does not carry exactly the reported width')
        if (rhb & 0x22) != want:
            errors.append('rhb does not match the reported width')
        if (rha & 0x22) != 0:
            errors.append('rha still carries a wide host width')
    if fields['rcmd6'].isdigit() and int(fields['rcmd6']) < 2:
        errors.append('rcmd6 must record the HS and width CMD6 writes')
    if fields['cmd6'].isdigit() and int(fields['cmd6']) < 1:
        errors.append('cmd6 must record at least one width CMD6 write')
    for name in ('ticks1', 'ticks2'):
        if fields[name].isdigit() and int(fields[name]) == 0:
            errors.append(f'{name} is zero')
    if fields['hz'].isdigit() and int(fields['hz']) == 0:
        errors.append('hz is zero')
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
            f'{LABEL}hs=1 hsrb=0 hsa=1 rcg=0000020b hispd=00 wtry=8 '
            'wfall=0 wroll=0 wid=8 verified=1 hb=00 ha=20 cmd6=1 base=1 '
            f'bytes1={WINDOW_BYTES} ticks1=1000 hz=19200000 dig1={digest} '
            f'bytes2={WINDOW_BYTES} ticks2=1000 dig2={digest} '
            f'req={REQUESTS} fails=0 cancel=0 budget=0 partial=0 pub=1 '
            'partdiag=0 revert=1 rverified=1 rhb=20 rha=00 rcmd6=2 '
            'rstatus=0 hsrevert=1 hsverified=1 rhsb=1 rhs=0 rclk=400000 '
            'limit=64 seg=1 cap=61071360')
        assert not check_errors(line, digest), check_errors(line, digest)
        changed = Path(tmp) / 'changed.img'
        data = bytearray(image.read_bytes())
        data[0] ^= 0xff
        changed.write_bytes(bytes(data))
        other = window_prefix(changed)
        assert check_errors(line, other), 'a digest from another image passed'
        mutations = (
            (line.replace(digest, other), 'foreign digest'),
            (line.replace('rcg=0000020b', 'rcg=00000000'), 'wrong rcg'),
            (line.replace('rclk=400000', 'rclk=50000000'), 'wrong clock'),
            (line.replace('rcmd6=2', 'rcmd6=1'), 'short revert count'),
            (line.replace('ha=20', 'ha=22'), 'both width bits'),
            (line.replace('ha=20', 'ha=02'), 'wrong width bit'),
            (line.replace('hb=00', 'hb=20'), 'wide before switch'),
            (line.replace('rhb=20', 'rhb=00'), 'wrong pre-revert width'),
            (line.replace('hsrb=0', 'hsrb=1'), 'HS before entry'),
            (line.replace('hsa=1', 'hsa=0'), 'HS not entered'),
            (line.replace('rhs=0', 'rhs=1'), 'HS not left'),
            (line.replace('rhsb=1', 'rhsb=0'), 'HS not set before leave'),
            (line.replace('wfall=0', 'wfall=1'), 'false fallback'),
            (line.replace('wroll=0', 'wroll=1'), 'false rollback'),
            (line.replace('wtry=8', 'wtry=4'), 'wrong first width'),
            (line.replace('hispd=00', 'hispd=04'), 'HISPD set'),
            (line.replace('hispd=00', 'hispd=02'), 'wide before switch'),
            (line.replace('hispd=00', 'hispd=20'), '8-bit before switch'),
            (line.replace('rha=00', 'rha=20'), 'revert width remains'),
            (line.replace('hsrevert=1', 'hsrevert=0'), 'no HS revert'),
            (line.replace('partial=0', 'partial=1'), 'partial pass'),
            (line.replace(f'req={REQUESTS}', 'req=7'), 'wrong request count'),
            (line.replace('ticks1=1000', 'ticks1=0'), 'zero ticks'),
            (line.replace('hz=19200000', 'hz=0'), 'zero timebase'),
            (line.replace('pub=1', 'pub=0'), 'no publication'),
            (line.replace('partdiag=0', 'partdiag=1'), 'partition diag'),
            (line.replace('rcg=0000020b', 'rcg=20b'), 'short rcg'),
            (line.replace('dig2=' + digest, ''), 'missing digest'),
            (line + ' extra=1', 'extra field'),
        )
        for text, label in mutations:
            assert check_errors(text, digest), f'{label} was accepted'
        for name in FIXED:
            changed = re.sub(rf'(?<!\S){re.escape(name)}=\S+',
                             f'{name}=999999999999', line, count=1)
            assert changed != line, f'FIXED field {name} was not found'
            assert check_errors(changed, digest), \
                f'FIXED field {name} was accepted with a wrong value'
        conditional = (
            (line.replace('wid=8', 'wid=4'), 'wid=4 without fallback'),
            (line.replace('cmd6=1', 'cmd6=0'), 'zero CMD6 count'),
            (line.replace('rcmd6=2', 'rcmd6=0'), 'zero revert CMD6 count'),
            (line.replace('ticks1=1000', 'ticks1=0'), 'zero ticks1'),
            (line.replace('ticks2=1000', 'ticks2=0'), 'zero ticks2'),
        )
        for text, label in conditional:
            assert check_errors(text, digest), f'{label} was accepted'
        fallback = (line.replace('wfall=0', 'wfall=1')
                    .replace('wroll=0', 'wroll=1')
                    .replace('wid=8', 'wid=4').replace('ha=20', 'ha=02')
                    .replace('rhb=20', 'rhb=02'))
        assert not check_errors(fallback, digest), check_errors(fallback, digest)
        report = 'MEDIA line\n' + line + '\nSNAP line\n'
        assert extract_report(report.splitlines()) == line
        try:
            extract_report(('MEDIA\n' + line + '\n' + line).splitlines())
        except ValueError as error:
            assert 'expected one' in str(error)
        else:
            raise AssertionError('a duplicate SPEED line was accepted')
    print('PASS: SPEED digest, mode, revert and well-formed mutation checks')


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
    try:
        if args.report is not None:
            line = extract_report(args.report.read_text().splitlines())
        else:
            line = args.line
    except ValueError as error:
        print(f'FAIL: {error}')
        return 1
    errors = check_errors(line, prefix)
    if errors:
        for error in errors:
            print(f'FAIL: {error}')
        return 1
    print(f'PASS: SPEED digest and fields match {prefix}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
