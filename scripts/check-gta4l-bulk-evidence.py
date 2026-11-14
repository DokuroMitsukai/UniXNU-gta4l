#!/usr/bin/env python3
"""Check a gta4l S2 BULK and DMA line against the final BOOT image.

The S2 report prints the SHA-256 prefix of the streamed 85,688,320-byte BOOT
region, the 2,615 64-block request count, the 80-window result and the
verified combined revert. B10.13 adds the DMA line: the block-stack request
and block counts must equal the same workload (2,695 requests and 167,520
blocks), the four comparisons must pass, the stream counters must be clean and
a DMADIAG line must be absent. S3a adds an optional LIVE line in two shapes:
the held-terminal success (the same stream and windows, a 5,000 ms idle and
the deterministic last-window re-read with `held=1 revert=0`) and the
force-slow host shape (no stream, the window set at 1-bit/400 kHz, `mode=slow`
and `bytes=0`). The held shape adds one request and two blocks to the DMA
counts for the re-read. This host-only checker recomputes the stream and
re-read digests from the final packaged image, requires the success shapes,
checks the modelled read budgets, rejects BULKFAIL/LIVEFAIL lines, and rejects
mutated reports or a changed image. It never reads a device.
"""
import argparse
import hashlib
import re
import tempfile
from pathlib import Path

REGION_BYTES = 85688320
BLOCK_BYTES = 512
CHUNK_BLOCKS = 64
CHUNKS = 2615
ALIGNED_WINDOWS = 64
WINDOWS = 80
WINDOW_BLOCKS = 2
WINDOW_BYTES = WINDOW_BLOCKS * BLOCK_BYTES
STREAM_BUDGET_MS = 4000
LIVE_IDLE_MS = 5000
SEED_HEX = '47544134'
SEED = int(SEED_HEX, 16)
HEX32 = re.compile(r'^[0-9a-f]{32}$')
ZERO_DIGEST = '0' * 32
LABEL = 'GTA4L-EMMC P5 BULK: '
FAIL_LABEL = 'GTA4L-EMMC P5 BULKFAIL: '
DMA_LABEL = 'GTA4L-EMMC P5 DMA: '
DMADIAG_LABEL = 'GTA4L-EMMC P5 DMADIAG: '
LIVE_LABEL = 'GTA4L-EMMC P5 LIVE: '
LIVEFAIL_LABEL = 'GTA4L-EMMC P5 LIVEFAIL: '
FIELDS = (
    'region', 'chunks', 'bytes', 'ticks', 'hz', 'seed', 'windows',
    'wpass', 'wfail', 'woffset', 'digest')
FIXED = {
    'region': str(REGION_BYTES),
    'chunks': str(CHUNKS),
    'bytes': str(REGION_BYTES),
    'seed': SEED_HEX,
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
SPEED_NO_REVERT = {name: '0' for name in SPEED_REVERT}
LIVE_FIELDS = (
    'mode', 'held', 'degraded', 'online', 'errors', 'recoveries',
    'notified', 'bytes', 'digest', 'idle_ms', 'reread', 'reread_digest',
    'revert', 'rverified', 'rclk', 'budget')
LIVEFAIL_FIELDS = (
    'mode', 'held', 'stage', 'status', 'window', 'offset', 'online',
    'errors', 'recoveries', 'notified', 'revert', 'rverified', 'rclk',
    'budget')
LIVE_HELD_FIXED = {
    'mode': 'held',
    'held': '1',
    'degraded': '0',
    'online': '1',
    'errors': '0',
    'recoveries': '0',
    'notified': '0',
    'bytes': str(REGION_BYTES),
    'idle_ms': str(LIVE_IDLE_MS),
    'reread': '1',
    'revert': '0',
    'rverified': '0',
    'rclk': '0',
    'budget': '0',
}
LIVE_SLOW_FIXED = dict(LIVE_HELD_FIXED, mode='slow', bytes='0')
SLOW_BULK_FIXED = {
    'region': str(REGION_BYTES),
    'chunks': '0',
    'bytes': '0',
    'ticks': '0',
    'seed': SEED_HEX,
    'windows': str(WINDOWS),
    'wpass': str(WINDOWS),
    'wfail': '0',
    'woffset': '0',
    'digest': ZERO_DIGEST,
}
SLOW_SPEED_FIXED = {
    'hs': '0',
    'hsa': '0',
    'verified': '0',
    'wid': '0',
    'cmd6': '0',
    'base': '0',
    'req': '0',
    'fails': '0',
    'cancel': '0',
    'budget': '0',
    'pub': '1',
}
SLOW_SPEED_FIXED.update(SPEED_NO_REVERT)


def region_digest(image):
    with Path(image).open('rb') as handle:
        data = handle.read(REGION_BYTES)
    if len(data) != REGION_BYTES:
        raise ValueError(f'{image}: shorter than {REGION_BYTES} bytes')
    return hashlib.sha256(data).hexdigest()[:32]


def bulk_window(index):
    region_blocks = REGION_BYTES // BLOCK_BYTES
    if index < ALIGNED_WINDOWS:
        return ((index * CHUNKS) // ALIGNED_WINDOWS) * CHUNK_BLOCKS
    state = SEED
    for _ in range(index - ALIGNED_WINDOWS + 1):
        state ^= (state << 13) & 0xffffffff
        state ^= state >> 17
        state ^= (state << 5) & 0xffffffff
        state &= 0xffffffff
    block = state % (region_blocks - WINDOW_BLOCKS)
    if block % CHUNK_BLOCKS == 0:
        block += 1
    return block


def window_digest(image, index):
    offset = bulk_window(index) * BLOCK_BYTES
    with Path(image).open('rb') as handle:
        handle.seek(offset)
        data = handle.read(WINDOW_BYTES)
    if len(data) != WINDOW_BYTES:
        raise ValueError(f'{image}: shorter than the window at {offset}')
    return hashlib.sha256(data).hexdigest()[:32]


def parse_fields(line, label, names):
    line = line.strip()
    if not line.startswith(label):
        raise ValueError(f'not a {label.strip()} line')
    fields = {}
    for token in line[len(label):].split():
        if '=' not in token:
            raise ValueError(f'malformed field {token!r}')
        name, value = token.split('=', 1)
        if name in fields:
            raise ValueError(f'duplicate field {name}')
        fields[name] = value
    missing = [name for name in names if name not in fields]
    extra = sorted(set(fields) - set(names))
    if missing:
        raise ValueError('missing fields ' + ','.join(missing))
    if extra:
        raise ValueError('unexpected fields ' + ','.join(extra))
    return fields


def parse_line(line):
    return parse_fields(line, LABEL, FIELDS)


def parse_dma(line):
    return parse_fields(line, DMA_LABEL, DMA_FIELDS)


def parse_live(line):
    return parse_fields(line, LIVE_LABEL, LIVE_FIELDS)


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


def extract_live(lines):
    found = [line.strip() for line in lines
             if line.strip().startswith(LIVE_LABEL)]
    failures = [line.strip() for line in lines
                if line.strip().startswith(LIVEFAIL_LABEL)]
    if len(found) > 1:
        raise ValueError(f'expected at most one LIVE line, found {len(found)}')
    if failures:
        raise ValueError('LIVEFAIL line present')
    return found[0] if found else None


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


def check(line, expected_digest, speed_fields=None,
          speed_revert=None):
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
        for name, value in (speed_revert or SPEED_REVERT).items():
            if speed_fields.get(name) != value:
                errors.append(
                    f'SPEED {name}={speed_fields.get(name)} expected {value}')
    return errors


def check_dma(line, requests=None, blocks=None):
    fields = parse_dma(line)
    expected = dict(DMA_FIXED)
    if requests is not None:
        expected['req'] = str(requests)
    if blocks is not None:
        expected['blk'] = str(blocks)
    errors = []
    for name, value in expected.items():
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


def check_errors(line, expected_digest, speed_fields=None,
                 speed_revert=None):
    try:
        return check(line, expected_digest, speed_fields, speed_revert)
    except ValueError as error:
        return [str(error)]


def check_dma_errors(line, requests=None, blocks=None):
    try:
        return check_dma(line, requests, blocks)
    except ValueError as error:
        return [str(error)]


def check_live(line, expected_digest, expected_reread):
    fields = parse_live(line)
    mode = fields['mode']
    if mode == 'held':
        fixed = LIVE_HELD_FIXED
    elif mode == 'slow':
        fixed = LIVE_SLOW_FIXED
    else:
        return [f'LIVE mode={mode} is not held or slow']
    errors = []
    for name, value in fixed.items():
        if fields[name] != value:
            errors.append(f'LIVE {name}={fields[name]} expected {value}')
    for name in ('held', 'degraded', 'online', 'errors', 'recoveries',
                 'notified', 'bytes', 'idle_ms', 'reread', 'revert',
                 'rverified', 'rclk', 'budget'):
        if not fields[name].isdigit():
            errors.append(
                f'LIVE {name}={fields[name]} is not an unsigned integer')
    if not HEX32.match(fields['digest']):
        errors.append('LIVE digest is not 32 lowercase hex characters')
    if not HEX32.match(fields['reread_digest']):
        errors.append('LIVE reread_digest is not 32 lowercase hex characters')
    if mode == 'held':
        if fields['digest'] != expected_digest:
            errors.append('LIVE digest does not match the final BOOT region')
    elif fields['digest'] != ZERO_DIGEST:
        errors.append('LIVE slow digest must be zero (no stream)')
    if fields['reread_digest'] != expected_reread:
        errors.append('LIVE reread_digest does not match the re-read window')
    return errors


def check_slow_bulk(line):
    fields = parse_line(line)
    errors = []
    for name, value in SLOW_BULK_FIXED.items():
        if fields[name] != value:
            errors.append(f'BULK {name}={fields[name]} expected {value}')
    if not fields['hz'].isdigit() or int(fields['hz']) == 0:
        errors.append(f'BULK hz={fields["hz"]} is not a positive integer')
    return errors


def check_slow_speed(fields):
    errors = []
    for name, value in SLOW_SPEED_FIXED.items():
        if fields.get(name) != value:
            errors.append(
                f'SPEED {name}={fields.get(name)} expected {value}')
    for name in ('dig1', 'dig2'):
        if fields.get(name) != ZERO_DIGEST:
            errors.append(f'SPEED {name} must be zero without a stream')
    return errors


def check_live_errors(line, expected_digest, expected_reread):
    try:
        return check_live(line, expected_digest, expected_reread)
    except ValueError as error:
        return [str(error)]


def check_slow_bulk_errors(line):
    try:
        return check_slow_bulk(line)
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
            f'bytes={REGION_BYTES} ticks=2500000 hz=1000000 seed={SEED_HEX} '
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
            (line.replace(f'seed={SEED_HEX}', 'seed=00000000'), 'wrong seed'),
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
        assert bulk_window(WINDOWS - 1) == 38946, \
            'the last bulk window derivation drifted'
        reread = window_digest(image, WINDOWS - 1)
        live = (
            f'{LIVE_LABEL}mode=held held=1 degraded=0 online=1 errors=0 '
            f'recoveries=0 notified=0 bytes={REGION_BYTES} '
            f'digest={digest} idle_ms={LIVE_IDLE_MS} reread=1 '
            f'reread_digest={reread} revert=0 rverified=0 rclk=0 budget=0')
        assert not check_live_errors(live, digest, reread), \
            check_live_errors(live, digest, reread)
        live_requests = CHUNKS + WINDOWS + 1
        live_blocks = (REGION_BYTES // BLOCK_BYTES +
                       WINDOWS * WINDOW_BLOCKS + WINDOW_BLOCKS)
        live_dma = dma.replace(
            f'req={CHUNKS + WINDOWS}', f'req={live_requests}').replace(
            f'blk={REGION_BYTES // BLOCK_BYTES + WINDOWS * WINDOW_BLOCKS}',
            f'blk={live_blocks}')
        assert not check_dma_errors(live_dma, live_requests, live_blocks), \
            check_dma_errors(live_dma, live_requests, live_blocks)
        assert check_dma_errors(dma, live_requests, live_blocks), \
            'the pre-live DMA counts were accepted in the live shape'
        no_revert = dict(SPEED_NO_REVERT)
        assert not check_errors(line, digest, no_revert, SPEED_NO_REVERT), \
            check_errors(line, digest, no_revert, SPEED_NO_REVERT)
        assert check_errors(line, digest, speed, SPEED_NO_REVERT), \
            'a verified revert was accepted in the held live shape'
        live_mutations = (
            (live.replace('mode=held', 'mode=fast'), 'LIVE mode'),
            (live.replace('held=1', 'held=0'), 'LIVE held'),
            (live.replace('degraded=0', 'degraded=1'), 'LIVE degraded'),
            (live.replace('online=1', 'online=0'), 'LIVE online'),
            (live.replace('errors=0', 'errors=1'), 'LIVE errors'),
            (live.replace('recoveries=0', 'recoveries=1'),
             'LIVE recoveries'),
            (live.replace('notified=0', 'notified=1'), 'LIVE notified'),
            (live.replace(f'bytes={REGION_BYTES}', 'bytes=85688319'),
             'LIVE bytes'),
            (live.replace(digest, other), 'LIVE digest'),
            (live.replace(f'idle_ms={LIVE_IDLE_MS}', 'idle_ms=0'),
             'LIVE idle'),
            (live.replace('reread=1', 'reread=0'), 'LIVE reread'),
            (live.replace(reread, other), 'LIVE reread digest'),
            (live.replace('revert=0', 'revert=1'), 'LIVE revert'),
            (live.replace('rverified=0', 'rverified=1'),
             'LIVE revert verified'),
            (live.replace('rclk=0', 'rclk=400000'), 'LIVE revert clock'),
            (live.replace('budget=0', 'budget=1'), 'LIVE budget'),
            (live + ' extra=1', 'LIVE extra field'),
        )
        for text, label in live_mutations:
            assert check_live_errors(text, digest, reread), \
                f'{label} was accepted'
        slow_bulk = (
            f'{LABEL}region={REGION_BYTES} chunks=0 bytes=0 ticks=0 '
            f'hz=1000000 seed={SEED_HEX} windows={WINDOWS} '
            f'wpass={WINDOWS} wfail=0 woffset=0 digest={ZERO_DIGEST}')
        assert not check_slow_bulk_errors(slow_bulk), \
            check_slow_bulk_errors(slow_bulk)
        slow_speed = dict(SLOW_SPEED_FIXED, dig1=ZERO_DIGEST,
                          dig2=ZERO_DIGEST)
        assert not check_slow_speed(slow_speed), check_slow_speed(slow_speed)
        slow_live = (
            f'{LIVE_LABEL}mode=slow held=1 degraded=0 online=1 errors=0 '
            f'recoveries=0 notified=0 bytes=0 digest={ZERO_DIGEST} '
            f'idle_ms={LIVE_IDLE_MS} reread=1 reread_digest={reread} '
            f'revert=0 rverified=0 rclk=0 budget=0')
        assert not check_live_errors(slow_live, digest, reread), \
            check_live_errors(slow_live, digest, reread)
        slow_bulk_mutations = (
            (slow_bulk.replace('chunks=0', f'chunks={CHUNKS}'),
             'slow stream chunks'),
            (slow_bulk.replace('bytes=0', f'bytes={REGION_BYTES}'),
             'slow stream bytes'),
            (slow_bulk.replace('ticks=0', 'ticks=2500000'), 'slow ticks'),
            (slow_bulk.replace(f'wpass={WINDOWS}', 'wpass=79'),
             'slow window pass'),
            (slow_bulk.replace(ZERO_DIGEST, digest), 'slow digest'),
            (slow_bulk.replace('hz=1000000', 'hz=0'), 'slow hz'),
            (slow_bulk + ' extra=1', 'slow extra field'),
        )
        for text, label in slow_bulk_mutations:
            assert check_slow_bulk_errors(text), f'{label} was accepted'
        slow_speed_mutations = (
            (dict(slow_speed, revert='1'), 'slow revert'),
            (dict(slow_speed, hs='1'), 'slow HS'),
            (dict(slow_speed, req='8'), 'slow requests'),
            (dict(slow_speed, dig1=digest), 'slow digest'),
        )
        for fields, label in slow_speed_mutations:
            assert check_slow_speed(fields), f'{label} was accepted'
        assert check_live_errors(
            slow_live.replace('bytes=0', f'bytes={REGION_BYTES}'),
            digest, reread), \
            'a slow live line with stream bytes was accepted'
        assert check_live_errors(
            slow_live.replace(ZERO_DIGEST, digest), digest, reread), \
            'a slow live line with a stream digest was accepted'
        assert extract_live(report.splitlines()) is None
        assert extract_live((report + live + '\n').splitlines()) == live
        try:
            extract_live((live + '\n' + LIVEFAIL_LABEL +
                          'mode=held').splitlines())
        except ValueError as error:
            assert 'LIVEFAIL' in str(error)
        else:
            raise AssertionError('a LIVEFAIL line was accepted')
        try:
            extract_live((live + '\n' + live).splitlines())
        except ValueError as error:
            assert 'expected at most one' in str(error)
        else:
            raise AssertionError('a duplicate LIVE line was accepted')
    print('PASS: BULK digest, counts, budget, revert, DMA, LIVE held and '
          'slow lines and mutation checks')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--line')
    parser.add_argument('--live-line')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.image is None or (args.report is None and args.line is None):
        parser.error('--image and one of --report/--line are required')
    digest = region_digest(args.image)
    reread = window_digest(args.image, WINDOWS - 1)
    dma_line = None
    live_line = args.live_line
    slow = False
    try:
        if args.report is not None:
            lines = args.report.read_text().splitlines()
            live_line = extract_live(lines)
            line = extract_report(lines)
            speed = parse_speed(lines)
            if live_line is not None:
                slow = parse_live(live_line)['mode'] == 'slow'
            if not slow:
                dma_line = extract_dma(lines)
        else:
            line = args.line
            speed = None
            if live_line is not None:
                slow = parse_live(live_line)['mode'] == 'slow'
    except ValueError as error:
        print(f'FAIL: {error}')
        return 1
    if slow:
        errors = check_slow_bulk_errors(line)
        errors.extend(check_slow_speed(speed or {}))
    else:
        speed_revert = SPEED_NO_REVERT if live_line is not None else None
        errors = check_errors(line, digest, speed, speed_revert)
        if dma_line is not None:
            if live_line is not None:
                errors.extend(check_dma_errors(
                    dma_line, CHUNKS + WINDOWS + 1,
                    REGION_BYTES // BLOCK_BYTES +
                    WINDOWS * WINDOW_BLOCKS + WINDOW_BLOCKS))
            else:
                errors.extend(check_dma_errors(dma_line))
    if live_line is not None:
        errors.extend(check_live_errors(live_line, digest, reread))
    if errors:
        for error in errors:
            print(f'FAIL: {error}')
        return 1
    if slow:
        print(f'PASS: BULK force-slow and LIVE shapes match {digest}')
    elif live_line is not None:
        print(f'PASS: BULK digest, DMA stack path and LIVE held shape '
              f'match {digest}')
    elif dma_line is not None:
        print(f'PASS: BULK digest and DMA stack path match {digest}')
    else:
        print(f'PASS: BULK digest and fields match {digest}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
