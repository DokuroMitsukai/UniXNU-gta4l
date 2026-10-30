#!/usr/bin/env python3
"""Normalize the ADT random seed of a diagnostic payload for comparison.

A clean payload build draws a fresh 64-byte ADT random-seed, so it is not
bit-exact. This tool extracts the ADT from a candidate and a reference payload,
requires random-seed to be the only differing property and exactly 64 bytes in
both, substitutes the reference seed into the candidate at the property's
structural offset, and writes the result only when it is byte-identical to the
reference. It is a post-build comparison tool, not a build input and not part
of the payload contract.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ADT_PATH = '::/EFI/UniXNU/Payloads/kernel.macho.adt'
SEED_NAME = 'random-seed'
SEED_BYTES = 64


class Error(Exception):
    pass


def parse_adt(data):
    """Parse an ADT property tree and reject malformed or trailing data."""

    def node(offset):
        if offset < 0 or offset + 8 > len(data):
            raise Error(f'ADT node header at {offset:#x} is out of range')
        count, children = struct.unpack_from('<II', data, offset)
        offset += 8
        properties = []
        for _ in range(count):
            if offset + 36 > len(data):
                raise Error(
                    f'ADT property header at {offset:#x} is out of range')
            raw = data[offset:offset + 32]
            length = struct.unpack_from('<I', data, offset + 32)[0]
            offset += 36
            name = raw.split(b'\0', 1)[0]
            if not name:
                raise Error(
                    f'ADT property at {offset - 36:#x} has an empty name')
            end = offset + length
            if end > len(data):
                raise Error(
                    f'ADT property {name!r} at {offset:#x} runs past the end')
            properties.append((name, data[offset:end], offset))
            offset = end + (-length % 4)
            if offset > len(data):
                raise Error(
                    f'ADT property {name!r} padding runs past the end')
        child_nodes = []
        for _ in range(children):
            child, offset = node(offset)
            child_nodes.append(child)
        return (properties, child_nodes), offset

    root, end = node(0)
    if end != len(data):
        raise Error(f'ADT has {len(data) - end} trailing bytes at {end:#x}')
    return root


def flatten(node, path=''):
    properties, children = node
    flat = {}
    for name, value, offset in properties:
        flat[f'{path}/{name.decode("latin1")}'] = (value, offset)
    for index, child in enumerate(children):
        flat.update(flatten(child, f'{path}[{index}]'))
    return flat


def normalize(candidate, reference, candidate_adt, reference_adt):
    """Return the seed-normalized candidate, or raise Error."""
    candidate_properties = flatten(parse_adt(candidate_adt))
    reference_properties = flatten(parse_adt(reference_adt))
    if set(candidate_properties) != set(reference_properties):
        missing = sorted(set(reference_properties) - set(candidate_properties))
        extra = sorted(set(candidate_properties) - set(reference_properties))
        raise Error(f'ADT property sets differ: missing {missing}, extra {extra}')
    differences = [key for key in candidate_properties
                   if candidate_properties[key][0] != reference_properties[key][0]]
    seed_keys = [key for key in candidate_properties
                 if key.rsplit('/', 1)[-1] == SEED_NAME]
    if len(seed_keys) != 1:
        raise Error(f'expected exactly one {SEED_NAME} property, found '
                    f'{seed_keys}')
    seed_key = seed_keys[0]
    candidate_seed, candidate_offset = candidate_properties[seed_key]
    reference_seed, _ = reference_properties[seed_key]
    if len(candidate_seed) != SEED_BYTES or len(reference_seed) != SEED_BYTES:
        raise Error(
            f'{seed_key} must be {SEED_BYTES} bytes; candidate '
            f'{len(candidate_seed)}, reference {len(reference_seed)}')
    if set(differences) - {seed_key}:
        raise Error(f'ADT properties other than {seed_key} differ: '
                    f'{sorted(set(differences) - {seed_key})}')
    position = candidate.find(candidate_adt)
    if position < 0:
        raise Error('candidate ADT is not present in the candidate payload')
    if candidate.find(candidate_adt, position + 1) >= 0:
        raise Error('candidate ADT occurs more than once in the candidate '
                    'payload')
    start = position + candidate_offset
    normalized = bytearray(candidate)
    normalized[start:start + SEED_BYTES] = reference_seed
    if bytes(normalized) != reference:
        raise Error('normalized candidate is not byte-identical to the '
                    'reference')
    return bytes(normalized), {
        'seed_property': seed_key,
        'candidate_seed': candidate_seed.hex(),
        'reference_seed': reference_seed.hex(),
        'seed_payload_offset': start,
        'normalized_sha256': hashlib.sha256(normalized).hexdigest(),
    }


def validate_paths(candidate_path, reference_path, output):
    """Reject aliases and an existing output before any extraction."""
    if candidate_path == reference_path or candidate_path.samefile(reference_path):
        raise SystemExit('--candidate and --reference must be different files')
    if output is None:
        return
    if output == candidate_path or output == reference_path:
        raise SystemExit(
            '--output must be a new path, not the candidate or reference')
    if output.exists():
        raise SystemExit(f'{output}: output exists; choose a new path')


def write_new(path, data):
    """Create the output exclusively so a concurrent writer cannot be clobbered."""
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
    except FileExistsError:
        raise SystemExit(f'{path}: output exists; choose a new path')
    with os.fdopen(descriptor, 'wb') as stream:
        stream.write(data)


def extract_adt(image):
    with tempfile.TemporaryDirectory(prefix='gta4l-adt-') as directory:
        destination = Path(directory) / 'kernel.macho.adt'
        result = subprocess.run(
            ['mcopy', '-i', str(image), ADT_PATH, str(destination)],
            capture_output=True, text=True)
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip()
            raise Error(f'{image}: cannot extract {ADT_PATH}: {detail}')
        return destination.read_bytes()


def synthetic_adt(seed, dram_base=0x80000000):
    properties = [(b'dram-base', struct.pack('<Q', dram_base)),
                  (b'random-seed', seed)]
    data = struct.pack('<II', len(properties), 0)
    for name, value in properties:
        data += name.ljust(32, b'\0') + struct.pack('<I', len(value))
        data += value + b'\0' * (-len(value) % 4)
    return data


def cli_guard_test():
    script = Path(__file__).resolve()
    with tempfile.TemporaryDirectory(prefix='gta4l-seed-cli-') as directory:
        base = Path(directory)
        candidate = base / 'candidate.img'
        reference = base / 'reference.img'
        existing = base / 'existing.img'
        alias = base / 'alias.img'
        candidate.write_bytes(b'candidate')
        reference.write_bytes(b'reference')
        existing.write_bytes(b'keep')
        alias.hardlink_to(candidate)

        def run(*arguments):
            return subprocess.run(
                [sys.executable, '-B', str(script), *map(str, arguments)],
                capture_output=True, text=True)

        result = run('--candidate', candidate, '--reference', reference,
                     '--output', existing)
        assert result.returncode != 0, result
        assert 'output exists' in result.stderr, result.stderr
        assert existing.read_bytes() == b'keep'

        result = run('--candidate', candidate, '--reference', reference,
                     '--output', candidate)
        assert result.returncode != 0, result
        assert 'new path' in result.stderr, result.stderr
        assert candidate.read_bytes() == b'candidate'

        result = run('--candidate', candidate, '--reference', alias)
        assert result.returncode != 0, result
        assert 'different files' in result.stderr, result.stderr
        assert candidate.read_bytes() == b'candidate'

        late = base / 'late.img'
        late.write_bytes(b'late')
        try:
            write_new(late, b'replacement')
        except SystemExit as exc:
            assert 'output exists' in str(exc), exc
        else:
            raise AssertionError('write_new overwrote an existing file')
        assert late.read_bytes() == b'late'


def self_test():
    seed = bytes(range(SEED_BYTES))
    other = bytes(range(1, SEED_BYTES + 1))
    candidate_adt = synthetic_adt(seed)
    reference_adt = synthetic_adt(other)
    candidate = b'HEAD' + candidate_adt + b'TAIL'
    reference = b'HEAD' + reference_adt + b'TAIL'
    normalized, report = normalize(candidate, reference, candidate_adt,
                                   reference_adt)
    assert normalized == reference
    assert report['seed_payload_offset'] == candidate.find(seed)
    assert report['normalized_sha256'] == hashlib.sha256(reference).hexdigest()

    def expect_error(call, text):
        try:
            call()
        except Error as exc:
            assert text in str(exc), (text, str(exc))
        else:
            raise AssertionError(f'expected Error containing {text!r}')

    changed = synthetic_adt(other, dram_base=0)
    expect_error(
        lambda: normalize(candidate, b'HEAD' + changed + b'TAIL',
                          candidate_adt, changed),
        'other than')
    short = synthetic_adt(other[:32])
    expect_error(
        lambda: normalize(candidate, b'HEAD' + short + b'TAIL',
                          candidate_adt, short),
        'must be 64 bytes')
    expect_error(
        lambda: normalize(candidate_adt + candidate_adt, reference,
                          candidate_adt, reference_adt),
        'more than once')
    expect_error(
        lambda: normalize(b'HEAD' + candidate_adt + b'TAIL',
                          b'HEAD' + reference_adt + b'TAIL' + b'X',
                          candidate_adt, reference_adt),
        'not byte-identical')
    expect_error(lambda: parse_adt(candidate_adt + b'X'), 'trailing bytes')
    cli_guard_test()
    print('PASS: seed normalization is structural, 64-byte, fail-closed and '
          'guard-checked')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()

    if args.self_test:
        self_test()
        return 0
    missing = [name for name in ('candidate', 'reference')
               if getattr(args, name) is None]
    if missing:
        parser.error('missing required options: ' +
                     ', '.join('--' + name for name in missing))
    candidate_path = args.candidate.resolve()
    reference_path = args.reference.resolve()
    for path in (candidate_path, reference_path):
        if not path.is_file():
            raise SystemExit(f'{path}: payload not found')
    output = args.output.resolve() if args.output is not None else None
    validate_paths(candidate_path, reference_path, output)

    candidate = candidate_path.read_bytes()
    reference = reference_path.read_bytes()
    try:
        normalized, report = normalize(
            candidate, reference, extract_adt(candidate_path),
            extract_adt(reference_path))
    except Error as exc:
        raise SystemExit(f'{candidate_path}: {exc}')
    report.update({
        'candidate': str(candidate_path),
        'reference': str(reference_path),
        'candidate_sha256': hashlib.sha256(candidate).hexdigest(),
        'reference_sha256': hashlib.sha256(reference).hexdigest(),
    })
    if output is not None:
        write_new(output, normalized)
        report['output'] = str(output)
        report['output_sha256'] = hashlib.sha256(normalized).hexdigest()
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f'normalized {candidate_path} with the seed from '
              f'{reference_path}: {report["normalized_sha256"]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
