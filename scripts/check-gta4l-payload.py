#!/usr/bin/env python3
"""Check a gta4l UniXNU payload against DarwinBoot's placeholder contract.

The payload is a FAT16 image. This check extracts the ADT sidecar and
unixnu.toml, then verifies that every ramdisk/trust-cache/addon file has the
matching memory-map placeholder and that a packed placeholder is used only for
a DBRDISK1 ramdisk. It also requires exactly one board diagnostic argument.
"""
import argparse
import json
import struct
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
EVIDENCE = REPO / 'out/gta4l/ram-root-sdhci-host-init-evidence'
PAYLOAD_DIR = '::/EFI/UniXNU/Payloads'
ADT_MEMBER = PAYLOAD_DIR + '/kernel.macho.adt'
TOML_MEMBER = '::/EFI/UniXNU/unixnu.toml'
RAMDISK_MEMBER = PAYLOAD_DIR + '/scratch-data.img'
TRUST_MEMBER = PAYLOAD_DIR + '/kernel.macho.trustcache'
ADDON_MEMBER = PAYLOAD_DIR + '/kernel.macho.gpuaddon'
DIAGNOSTICS = ('gta4l-sdhci-host-init=1',
               'gta4l-sdhci-audit=1',
               'gta4l-sdhci-snapshot=1',
               'gta4l-emmc-probe=1',
               'gta4l-emmc-probe=2',
               'gta4l-emmc-probe=3')
DIAGNOSTIC_NAMES = {
    'host-init': 'gta4l-sdhci-host-init=1',
    'audit': 'gta4l-sdhci-audit=1',
    'snapshot': 'gta4l-sdhci-snapshot=1',
    'emmc-probe-1': 'gta4l-emmc-probe=1',
    'emmc-probe-2': 'gta4l-emmc-probe=2',
    'emmc-probe-3': 'gta4l-emmc-probe=3',
}


def run_tool(*args):
    try:
        return subprocess.run(args, capture_output=True, text=True, check=True).stdout
    except FileNotFoundError as error:
        raise SystemExit(f'{args[0]} not found; install mtools') from error
    except subprocess.CalledProcessError as error:
        raise SystemExit(f'{args[0]} failed: {error.stderr.strip()}') from error


def list_payload(image):
    output = run_tool('mdir', '-i', str(image), '-b', PAYLOAD_DIR)
    return {Path(line.strip()).name for line in output.splitlines() if line.strip()}


def extract(image, member, destination):
    run_tool('mcopy', '-i', str(image), member, str(destination))
    return destination


def parse_adt(data):
    def node(offset):
        if offset + 8 > len(data):
            raise ValueError('truncated ADT node')
        count, children_count = struct.unpack_from('<II', data, offset)
        offset += 8
        properties = {}
        for _ in range(count):
            raw, length = struct.unpack_from('<32sI', data, offset)
            offset += 36
            name = raw.split(b'\0', 1)[0].decode('latin1')
            if offset + length > len(data):
                raise ValueError('truncated ADT property')
            properties[name] = data[offset:offset + length]
            offset += length + (-length % 4)
        children = []
        for _ in range(children_count):
            child, offset = node(offset)
            children.append(child)
        return (properties, children), offset

    root, offset = node(0)
    if offset > len(data):
        raise ValueError('ADT overruns file')
    return root


def memory_map_properties(root):
    def walk(entry):
        properties, children = entry
        name = properties.get('name', b'').split(b'\0', 1)[0]
        if name == b'memory-map':
            return properties
        for child in children:
            found = walk(child)
            if found is not None:
                return found
        return None

    found = walk(root)
    if found is None:
        raise ValueError('ADT has no memory-map node')
    return found


def ramdisk_layout(data):
    if len(data) < 8 or data[:8] != b'DBRDISK1':
        return False, len(data)
    if len(data) < 32:
        raise ValueError('truncated packed ramdisk header')
    version, page = struct.unpack_from('<II', data, 8)
    logical, count = struct.unpack_from('<QQ', data, 16)
    if version != 1 or page != 4096 or logical == 0 or logical % page:
        raise ValueError('invalid packed ramdisk header')
    if count > 65536 or count > (len(data) - 32) // 16:
        raise ValueError('invalid packed ramdisk table')
    table = 32 + count * 16
    cursor = table
    end = 0
    for index in range(count):
        offset, length = struct.unpack_from('<QQ', data, 32 + index * 16)
        if not length or offset % page or length % page or offset < end:
            raise ValueError('invalid packed ramdisk range')
        if offset > logical or length > logical - offset:
            raise ValueError('packed ramdisk range exceeds logical size')
        if length > len(data) - cursor:
            raise ValueError('packed ramdisk range exceeds file')
        end = offset + length
        cursor += length
    if cursor != len(data):
        raise ValueError('packed ramdisk has trailing data')
    return True, logical


def xnu_args(path):
    data = tomllib.loads(Path(path).read_text())
    entries = data.get('entry', [])
    for entry in entries:
        if entry.get('id') == 'xnu':
            return entry.get('args', '')
    raise ValueError('unixnu.toml has no xnu entry')


def check(image, expected='any'):
    image = Path(image)
    result = {'payload': str(image), 'expected_diagnostic': expected,
              'checks': {}}

    def require(name, condition):
        result['checks'][name] = bool(condition)
        if not condition:
            raise ValueError(name)

    try:
        files = list_payload(image)
        with tempfile.TemporaryDirectory(prefix='gta4l-payload-check-') as tmp:
            tmp = Path(tmp)
            adt = extract(image, ADT_MEMBER, tmp / 'kernel.adt')
            toml_path = extract(image, TOML_MEMBER, tmp / 'unixnu.toml')
            memory_map = memory_map_properties(parse_adt(adt.read_bytes()))

            data_present = 'scratch-data.img' in files
            trust_present = 'kernel.macho.trustcache' in files
            addon_present = 'kernel.macho.gpuaddon' in files
            raw_placeholder = 'RAMDisk' in memory_map
            packed_placeholder = 'PackedRAMDisk' in memory_map

            require('exactly_one_ramdisk_placeholder',
                    raw_placeholder != packed_placeholder if data_present
                    else not raw_placeholder and not packed_placeholder)
            if data_present:
                ramdisk = extract(image, RAMDISK_MEMBER,
                                  tmp / 'scratch-data.img').read_bytes()
                packed, expanded = ramdisk_layout(ramdisk)
                if raw_placeholder:
                    require('raw_ramdisk_layout', not packed)
                else:
                    require('packed_ramdisk_layout', packed)
                    require('packed_ramdisk_expanded_size', expanded > 0)
            require('trustcache_placeholder_matches_file',
                    trust_present == ('TrustCache' in memory_map))
            require('gpuaddon_placeholder_matches_file',
                    addon_present == ('GpuAddon' in memory_map))

            args = xnu_args(toml_path)
            found = [item for item in DIAGNOSTICS if item in args]
            require('exactly_one_diagnostic_argument', len(found) == 1)
            if expected != 'any':
                require(f'{expected}_diagnostic_selected',
                        found == [DIAGNOSTIC_NAMES[expected]])
            require('unixnu_toml_has_xnu_args', bool(args))
        result['pass'] = True
    except (ValueError, OSError) as error:
        result['pass'] = False
        result['failed_check'] = str(error)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--payload')
    parser.add_argument('--expect-diagnostic',
                        choices=('any', 'host-init', 'audit', 'snapshot',
                                 'emmc-probe-1', 'emmc-probe-2',
                                 'emmc-probe-3'),
                        default='any',
                        help='require this board diagnostic argument')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()

    if args.self_test:
        bad = EVIDENCE / 'payload.img'
        fixed = EVIDENCE / 'payload-v2.img'
        if not bad.is_file() or not fixed.is_file():
            print('SKIP: retained failed/corrected payloads are not present')
            return 0
        bad_result = check(bad, 'host-init')
        assert not bad_result['pass'], 'failed payload unexpectedly passed'
        assert bad_result['failed_check'] == 'packed_ramdisk_layout', bad_result
        fixed_result = check(fixed, 'host-init')
        assert fixed_result['pass'], fixed_result
        print('PASS: packed/raw ADT mismatch rejected; corrected payload passes')
        return 0

    if not args.payload:
        parser.error('--payload is required unless --self-test is used')
    result = check(args.payload, args.expect_diagnostic)
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        for name, ok in result['checks'].items():
            print(f'{"PASS" if ok else "FAIL"} {name}')
        if not result['pass']:
            print(f'FAIL: {result["failed_check"]}', file=sys.stderr)
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
