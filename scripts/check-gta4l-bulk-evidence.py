#!/usr/bin/env python3
"""Check a gta4l S2 BULK and DMA line against the final BOOT image.

The S2 report prints the SHA-256 prefix of the streamed 85,688,320-byte BOOT
region, the 2,615 64-block request count, the 80-window result and the
verified combined revert. B10.13 adds the DMA line: the block-stack request
and block counts must equal the same workload (2,695 requests and 167,520
blocks), the four comparisons must pass, the stream counters must be clean and
a DMADIAG line must be absent. This host-only checker recomputes the digest
from the final packaged image, requires both success shapes, checks the
modelled read budget, rejects a BULKFAIL line, and rejects mutated reports or
a changed image. It never reads a device.
"""
import argparse
import hashlib
import re
import tempfile
from pathlib import Path

REGION_BYTES = 85688320
CHUNKS = 2615
WINDOWS = 80
WINDOW_BLOCKS = 2
STREAM_BUDGET_MS = 4000
SEED = '47544134'
HEX32 = re.compile(r'^[0-9a-f]{32}$')
LABEL = 'GTA4L-EMMC P5 BULK: '
FAIL_LABEL = 'GTA4L-EMMC P5 BULKFAIL: '
DMA_LABEL = 'GTA4L-EMMC P5 DMA: '
DMADIAG_LABEL = 'GTA4L-EMMC P5 DMADIAG: '
FIELDS = (
    'region', 'chunks', 'bytes', 'ticks', 'hz', 'seed', 'windows',
    'wpass', 'wfail', 'woffset', 'digest')
FIXED = {
    'region': str(REGION_BYTES),
    'chunks': str(CHUNKS),
    'bytes': str(REGION_BYTES),
    'seed': SEED,
    'windows': str(WINDOWS),
    'wpass': str(WINDOWS),
    'wfail': '0',
    'woffset': '0',
}
UNSIGNED = ('ticks', 'hz')
DMA_FIELDS = (
    'mode', 'smmu', 'cmp', 'canary', 'poison', 'chunks', 'bytes',
    'ticks', 'dat', 'fails', 'stage', 'ast', 'aaddr', 'desc',
    'budget', 'skip', 'req', 'blk')
DMA_FIXED = {
    'mode': 'adma2-32',
    'cmp': '4/4',
    'canary': '1',
    'poison': '1',
    'chunks': str(CHUNKS),
    'bytes': str(REGION_BYTES),
    'fails': '0',
    'stage': '0',
    'ast': '00000000',
    'aaddr': '00000000',
    'desc': '4294967295',
    'budget': '0',
    'skip': '0',
    'req': str(CHUNKS + WINDOWS),
    'blk': str(REGION_BYTES // 512 + WINDOWS * WINDOW_BLOCKS),
}
DMA_UNSIGNED = ('ticks', 'dat')
DMA_SMMU = ('bypass', 'cb-off')
SPEED_LABEL = 'GTA4L-EMMC P5 SPEED: '
SPEED_REVERT = {
    'revert': '1',
    'rverified': '1',
    'rstatus': '0',
    'hsrevert': '1',
    'hsverified': '1',
    'rhsb': '1',
    'rhs': '0',
    'rclk': '400000',
}


def region_digest(image):
    with Path(image).open('rb') as handle:
        data = handle.read(REGION_BYTES)
    if len(data) != REGION_BYTES:
        raise ValueError(f'{image}: shorter than {REGION_BYTES} bytes')
    return hashlib.sha256(data).hexdigest()[:32]


def parse_line(line):
    line = line.strip()
    if not line.startswith(LABEL):
        raise ValueError('not a BULK line')
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


def parse_dma(line):
    line = line.strip()
    if not line.startswith(DMA_LABEL):
        raise ValueError('not a DMA line')
    fields = {}
    for token in line[len(DMA_LABEL):].split():
        if '=' not in token:
            raise ValueError(f'malformed field {token!r}')
        name, value = token.split('=', 1)
        if name in fields:
            raise ValueError(f'duplicate field {name}')
        fields[name] = value
    missing = [name for name in DMA_FIELDS if name not in fields]
    extra = sorted(set(fields) - set(DMA_FIELDS))
    if missing:
        raise ValueError('missing fields ' + ','.join(missing))
    if extra:
        raise ValueError('unexpected fields ' + ','.join(extra))
    return fields


def extract_report(lines):
    bulk = [line.strip() for line in lines
            if line.strip().startswith(LABEL)]
    failures = [line.strip() for line in lines
                if line.strip().startswith(FAIL_LABEL)]
    if len(bulk) != 1:
        raise ValueError(f'expected one BULK line, found {len(bulk)}')
    if failures:
        raise ValueError('BULKFAIL line present')
    return bulk[0]


def extract_dma(lines):
    found = [line.strip() for line in lines
             if line.strip().startswith(DMA_LABEL)]
    diagnostics = [line.strip() for line in lines
                   if line.strip().startswith(DMADIAG_LABEL)]
    if len(found) != 1:
        raise ValueError(f'expected one DMA line, found {len(found)}')
    if diagnostics:
        raise ValueError('DMADIAG line present')
    return found[0]


def parse_speed(lines):
    found = [line.strip() for line in lines
             if line.strip().startswith(SPEED_LABEL)]
    if len(found) != 1:
        raise ValueError(f'expected one SPEED line, found {len(found)}')
    fields = {}
    for token in found[0][len(SPEED_LABEL):].split():
        if '=' in token:
            name, value = token.split('=', 1)
            fields[name] = value
    return fields


def check(line, expected_digest, speed_fields=None):
    fields = parse_line(line)
    errors = []
    for name, value in FIXED.items():
        if fields[name] != value:
            errors.append(f'{name}={fields[name]} expected {value}')
    for name in UNSIGNED:
        if not fields[name].isdigit():
            errors.append(f'{name}={fields[name]} is not an unsigned integer')
    if not HEX32.match(fields['digest']):
        errors.append('digest is not 32 lowercase hex characters')
    if fields['digest'] != expected_digest:
        errors.append('digest does not match the final BOOT region')
    if fields['ticks'].isdigit() and int(fields['ticks']) == 0:
        errors.append('ticks is zero')
    if fields['hz'].isdigit() and int(fields['hz']) == 0:
        errors.append('hz is zero')
    if fields['ticks'].isdigit() and fields['hz'].isdigit() and \
            int(fields['hz']) != 0:
        if int(fields['ticks']) * 1000 > int(fields['hz']) * STREAM_BUDGET_MS:
            errors.append('ticks exceed the stream budget')
    if speed_fields is not None:
        for name, value in SPEED_REVERT.items():
            if speed_fields.get(name) != value:
                errors.append(
                    f'SPEED {name}={speed_fields.get(name)} expected {value}')
    return errors


def check_dma(line):
    fields = parse_dma(line)
    errors = []
    for name, value in DMA_FIXED.items():
        if fields[name] != value:
            errors.append(f'DMA {name}={fields[name]} expected {value}')
    if fields['smmu'] not in DMA_SMMU:
        errors.append(f'DMA smmu={fields["smmu"]} is not a clean decision')
    for name in DMA_UNSIGNED:
        if not fields[name].isdigit():
            errors.append(
                f'DMA {name}={fields[name]} is not an unsigned integer')
    if fields['ticks'].isdigit() and int(fields['ticks']) == 0:
        errors.append('DMA ticks is zero')
    if fields['dat'].isdigit() and int(fields['dat']) == 0:
        errors.append('DMA dat is zero')
    return errors


def check_errors(line, expected_digest, speed_fields=None):
    try:
        return check(line, expected_digest, speed_fields)
    except ValueError as error:
        return [str(error)]


def check_dma_errors(line):
    try:
        return check_dma(line)
    except ValueError as error:
        return [str(error)]


def synthetic_image(path):
    pattern = bytes((index * 7 + 3) & 0xff for index in range(4096))
    with path.open('wb') as handle:
        remaining = REGION_BYTES
        while remaining:
            take = min(remaining, len(pattern))
            handle.write(pattern[:take])
            remaining -= take


def self_test():
    with tempfile.TemporaryDirectory(prefix='gta4l-bulk-check-') as tmp:
        image = Path(tmp) / 'boot.img'
        synthetic_image(image)
        digest = region_digest(image)
        speed = dict(SPEED_REVERT)
        line = (
            f'{LABEL}region={REGION_BYTES} chunks={CHUNKS} '
            f'bytes={REGION_BYTES} ticks=2500000 hz=1000000 seed={SEED} '
            f'windows={WINDOWS} wpass={WINDOWS} wfail=0 woffset=0 '
            f'digest={digest}')
        assert not check_errors(line, digest, speed), \
            check_errors(line, digest, speed)
        changed = Path(tmp) / 'changed.img'
        data = bytearray(image.read_bytes())
        data[0] ^= 0xff
        changed.write_bytes(bytes(data))
        other = region_digest(changed)
        assert check_errors(line, other, speed), \
            'a digest from another image passed'
        mutations = (
            (line.replace(digest, other), 'foreign digest'),
            (line.replace(f'chunks={CHUNKS}', 'chunks=2614'),
             'wrong chunk count'),
            (line.replace(f'bytes={REGION_BYTES}', 'bytes=85688319'),
             'short byte count'),
            (line.replace(f'wpass={WINDOWS}', 'wpass=79'), 'short window pass'),
            (line.replace('wfail=0', 'wfail=1'), 'window failure'),
            (line.replace('woffset=0', 'woffset=64'), 'window offset'),
            (line.replace('ticks=2500000', 'ticks=0'), 'zero ticks'),
            (line.replace('hz=1000000', 'hz=0'), 'zero timebase'),
            (line.replace('ticks=2500000', 'ticks=5000000'),
             'stream budget overrun'),
            (line.replace(f'seed={SEED}', 'seed=00000000'), 'wrong seed'),
            (line.replace(f'windows={WINDOWS}', 'windows=79'),
             'wrong window count'),
            (line.replace(f'region={REGION_BYTES}', 'region=85688319'),
             'wrong region'),
            (line + ' extra=1', 'extra field'),
        )
        for text, label in mutations:
            assert check_errors(text, digest, speed), f'{label} was accepted'
        dma = (
            f'{DMA_LABEL}mode=adma2-32 smmu=cb-off cmp=4/4 canary=1 '
            f'poison=1 chunks={CHUNKS} bytes={REGION_BYTES} '
            f'ticks=2500000 dat=2000000 fails=0 stage=0 ast=00000000 '
            f'aaddr=00000000 desc=4294967295 budget=0 skip=0 '
            f'req={CHUNKS + WINDOWS} '
            f'blk={REGION_BYTES // 512 + WINDOWS * WINDOW_BLOCKS}')
        assert not check_dma_errors(dma), check_dma_errors(dma)
        dma_mutations = (
            (dma.replace('mode=adma2-32', 'mode=none'), 'DMA mode'),
            (dma.replace('smmu=cb-off', 'smmu=fault'), 'DMA SMMU fault'),
            (dma.replace('cmp=4/4', 'cmp=3/4'), 'DMA compare'),
            (dma.replace('canary=1', 'canary=0'), 'DMA canary'),
            (dma.replace('poison=1', 'poison=0'), 'DMA poison'),
            (dma.replace(f'chunks={CHUNKS}', 'chunks=2614'), 'DMA chunks'),
            (dma.replace(f'bytes={REGION_BYTES}', 'bytes=85688319'),
             'DMA bytes'),
            (dma.replace('ticks=2500000', 'ticks=0'), 'DMA ticks'),
            (dma.replace('dat=2000000', 'dat=0'), 'DMA dat'),
            (dma.replace('fails=0', 'fails=1'), 'DMA failure count'),
            (dma.replace('stage=0', 'stage=4'), 'DMA failure stage'),
            (dma.replace('ast=00000000', 'ast=00000001'), 'DMA ADMA state'),
            (dma.replace('aaddr=00000000', 'aaddr=875c3018'),
             'DMA ADMA address'),
            (dma.replace('desc=4294967295', 'desc=1'), 'DMA descriptor'),
            (dma.replace('budget=0', 'budget=1'), 'DMA budget exit'),
            (dma.replace('skip=0', 'skip=1'), 'DMA skip'),
            (dma.replace(f'req={CHUNKS + WINDOWS}', 'req=2615'),
             'DMA request count'),
            (dma.replace(
                f'blk={REGION_BYTES // 512 + WINDOWS * WINDOW_BLOCKS}',
                'blk=167360'), 'DMA block count'),
            (dma + ' extra=1', 'DMA extra field'),
        )
        for text, label in dma_mutations:
            assert check_dma_errors(text), f'{label} was accepted'
        bad_speed = dict(speed)
        bad_speed['rverified'] = '0'
        assert check_errors(line, digest, bad_speed), \
            'an unverified revert was accepted'
        report = 'GTA4L-EMMC P5 SPEED: ' + ' '.join(
            f'{name}={value}' for name, value in speed.items()) + '\n'
        report += line + '\n' + dma + '\n'
        assert extract_report(report.splitlines()) == line
        assert extract_dma(report.splitlines()) == dma
        assert parse_speed(report.splitlines()) == speed
        try:
            extract_dma((dma + '\n' + DMADIAG_LABEL +
                         'tp=00000000').splitlines())
        except ValueError as error:
            assert 'DMADIAG' in str(error)
        else:
            raise AssertionError('a DMADIAG line was accepted')
        try:
            extract_dma((dma + '\n' + dma).splitlines())
        except ValueError as error:
            assert 'expected one' in str(error)
        else:
            raise AssertionError('a duplicate DMA line was accepted')
        try:
            extract_report(('BULK\n' + line + '\n' +
                            f'{FAIL_LABEL}stage=1').splitlines())
        except ValueError as error:
            assert 'BULKFAIL' in str(error)
        else:
            raise AssertionError('a BULKFAIL line was accepted')
        try:
            extract_report(('BULK\n' + line + '\n' + line).splitlines())
        except ValueError as error:
            assert 'expected one' in str(error)
        else:
            raise AssertionError('a duplicate BULK line was accepted')
    print('PASS: BULK digest, counts, budget, revert, DMA line and '
          'mutation checks')


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
    digest = region_digest(args.image)
    dma_line = None
    try:
        if args.report is not None:
            lines = args.report.read_text().splitlines()
            line = extract_report(lines)
            dma_line = extract_dma(lines)
            speed = parse_speed(lines)
        else:
            line = args.line
            speed = None
    except ValueError as error:
        print(f'FAIL: {error}')
        return 1
    errors = check_errors(line, digest, speed)
    if dma_line is not None:
        errors.extend(check_dma_errors(dma_line))
    if errors:
        for error in errors:
            print(f'FAIL: {error}')
        return 1
    if dma_line is not None:
        print(f'PASS: BULK digest and DMA stack path match {digest}')
    else:
        print(f'PASS: BULK digest and fields match {digest}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
