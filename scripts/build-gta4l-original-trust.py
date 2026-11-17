#!/usr/bin/env python3
"""Build the original static+System trust bundle from range-read IPSW inputs.

The inputs are BuildManifest.plist and the two manifest-selected trust-cache
IM4P members. The builder re-verifies the manifest SHA-384 digests, extracts
the two unchanged version-2 modules, checks the expected module hashes, and
requires the real launchd cdhash to be covered before writing the bundle.
"""
import argparse
import hashlib
import json
import plistlib
import struct
from pathlib import Path

EXPECTED_STATIC = 'da88460e6e513438d5307e5c526f76497a74754476a24da29f239f0c90120fc2'
EXPECTED_SYSTEM = 'd121ba89167e03aafd00fa687c30cd8ebc4ccafd10c1408adb2ba3de12403651'
EXPECTED_BUNDLE = '7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7'
LAUNCHD_CDHASH = bytes.fromhex('1d52c69747dffc4b1937d241bdbc9cee3b22df18')


def der(data, at):
    tag, size = data[at:at + 2]
    at += 2
    if size & 128:
        count = size & 127
        size = int.from_bytes(data[at:at + count], 'big')
        at += count
    if at + size > len(data):
        raise ValueError('truncated DER')
    return tag, data[at:at + size], at + size


def module(im4p, label):
    tag, sequence, end = der(im4p, 0)
    fields = []
    cursor = 0
    while cursor < len(sequence):
        kind, content, cursor = der(sequence, cursor)
        fields.append((kind, content))
    if (tag != 0x30 or end != len(im4p) or
            [kind for kind, _ in fields] != [0x16, 0x16, 0x16, 4] or
            fields[0][1] != b'IM4P'):
        raise SystemExit(f'{label}: not the expected trust-cache IM4P')
    return fields[3][1]


def check_module(data, expected, label):
    if hashlib.sha256(data).hexdigest() != expected:
        raise SystemExit(f'{label}: module SHA-256 mismatch')
    version = struct.unpack_from('<I', data, 0)[0]
    count = struct.unpack_from('<I', data, 20)[0]
    if version != 2 or len(data) != 24 + count * 24:
        raise SystemExit(f'{label}: not a complete version-2 module')
    return count


def covers(data, cdhash):
    count = struct.unpack_from('<I', data, 20)[0]
    return any(data[24 + index * 24:24 + index * 24 + 20] == cdhash
               for index in range(count))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--static', type=Path, required=True)
    parser.add_argument('--system', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest = plistlib.loads(args.manifest.read_bytes())
    if (manifest['ProductVersion'] != '17.7.11' or
            manifest['ProductBuildVersion'] != '21H461' or
            'iPad7,1' not in manifest['SupportedProductTypes']):
        raise SystemExit('wrong product, version or build')
    identities = [item for item in manifest['BuildIdentities']
                  if item['Info']['DeviceClass'] == 'j120ap']
    identity = next(item for item in identities
                    if item['Info']['RestoreBehavior'] == 'Erase' and
                    int(item['ApChipID'], 16) == 0x8011 and
                    int(item['ApBoardID'], 16) == 12)
    inputs = {'static': args.static, 'system': args.system}
    im4ps = {}
    for name, component in (('static', 'StaticTrustCache'),
                            ('system', 'Cryptex1,SystemTrustCache')):
        data = inputs[name].read_bytes()
        entry = identity['Manifest'][component]
        if hashlib.sha384(data).digest() != entry['Digest']:
            raise SystemExit(f'{component}: manifest SHA-384 mismatch')
        for other in identities:
            if other['Manifest'][component]['Digest'] != entry['Digest']:
                raise SystemExit(f'{component}: j120ap identities disagree')
        im4ps[name] = module(data, component)
    static = im4ps['static']
    system = im4ps['system']
    static_entries = check_module(static, EXPECTED_STATIC, 'static')
    system_entries = check_module(system, EXPECTED_SYSTEM, 'system')
    if not covers(static, LAUNCHD_CDHASH) and not covers(system, LAUNCHD_CDHASH):
        raise SystemExit('the real launchd cdhash is not covered by either module')
    bundle = struct.pack('<3I', 2, 12, 12 + len(static)) + static + system
    if hashlib.sha256(bundle).hexdigest() != EXPECTED_BUNDLE:
        raise SystemExit('bundle SHA-256 mismatch')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    destination = output / 'original-static-system.bundle'
    if destination.exists():
        raise SystemExit(f'{destination}: already exists')
    destination.write_bytes(bundle)
    report = {
        'manifest': str(args.manifest.resolve()),
        'manifest_sha256': hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
        'static_im4p_sha256': hashlib.sha256(args.static.read_bytes()).hexdigest(),
        'system_im4p_sha256': hashlib.sha256(args.system.read_bytes()).hexdigest(),
        'static_module_sha256': EXPECTED_STATIC,
        'system_module_sha256': EXPECTED_SYSTEM,
        'static_entries': static_entries,
        'system_entries': system_entries,
        'launchd_cdhash': LAUNCHD_CDHASH.hex(),
        'launchd_covered': True,
        'bundle': str(destination),
        'bundle_bytes': len(bundle),
        'bundle_sha256': EXPECTED_BUNDLE,
        'original_modules_modified': False,
        'apple_personalization_claimed': False,
    }
    (output / 'inputs.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
