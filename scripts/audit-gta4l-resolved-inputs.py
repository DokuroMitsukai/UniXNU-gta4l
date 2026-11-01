#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Print the resolved gta4l packaging inputs as deterministic JSON.

Host-only and offline: it reads the device repository, the shared UniXNU
checkout and the firmware checkout.  It does not fetch, build, write a build
output or touch a device.  Run it with ``python3.11 -B`` from the device
repository, or pass ``--device``.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROFILE_FIELDS = ('name', 'darwin', 'xnu_version', 'xnu_revision', 'driverkit_tag', 'trustcache_tag', 'abi')


def load(path):
    return tomllib.loads(Path(path).read_text())


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def git(path, *arguments):
    try:
        result = subprocess.run(['git', '-C', str(path), *arguments],
                                text=True, capture_output=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return result.stdout.strip()


def checkout(path):
    if not Path(path).is_dir():
        return {'path': None}
    return {'path': str(Path(path).resolve()), 'commit': git(path, 'rev-parse', 'HEAD'),
            'branch': git(path, 'rev-parse', '--abbrev-ref', 'HEAD'),
            'dirty_paths': len((git(path, 'status', '--porcelain') or '').splitlines())}


def first_fdt(path):
    data = Path(path).read_bytes()
    if len(data) < 40 or data[:4] != b'\xd0\x0d\xfe\xed':
        return None
    size = int.from_bytes(data[4:8], 'big')
    if size < 40 or size > len(data):
        return None
    return {'bytes': size, 'sha256': hashlib.sha256(data[:size]).hexdigest()}


def feed(feeds, name):
    for entry in feeds.get('source', []):
        if entry.get('name') == name:
            return {key: entry.get(key) for key in ('name', 'kind', 'ref', 'commit')}
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', type=Path, default=ROOT)
    parser.add_argument('--uefi', type=Path,
                        help='Mu-Silicium checkout (default: UNIXNU_UEFI or ../Mu-Silicium)')
    arguments = parser.parse_args()

    device = arguments.device.resolve()
    board = load(device / 'board.toml')
    package = board.get('package', {})
    profile_name = board['board']['os_profile']
    unixnu = (device / package['unixnu']).resolve()
    uefi = arguments.uefi or (Path(os.environ['UNIXNU_UEFI']) if os.environ.get('UNIXNU_UEFI')
                              else device.parent / 'Mu-Silicium')
    profile = load(unixnu / 'profiles/os' / profile_name / 'profile.toml')
    unixnu_feeds = load(unixnu / 'feeds.toml')
    catalog = {entry['id']: entry for entry in load(unixnu / 'components.toml')['component']}
    lock = load(device / 'unixnu.lock')

    components = {}
    for identifier in board.get('components', {}).get('include', []):
        entry = catalog[identifier]
        local = unixnu / entry['path']
        components[identifier] = {
            'repository': entry.get('repository'),
            'declared': entry.get('revision'),
            'local': git(local, 'rev-parse', 'HEAD'),
            'dirty_paths': len((git(local, 'status', '--porcelain') or '').splitlines()),
            'match': git(local, 'rev-parse', 'HEAD') == entry.get('revision'),
        }

    build = profile['ipsw']['build']
    script = device / 'scripts/package.sh'
    template = unixnu / 'templates/package.sh'
    kernelcache = unixnu / 'out/ipsw' / build / 'kernelcache.decompressed'
    evidence = None
    for candidate in (device / 'out', device.parent.parent / 'out', unixnu / 'out'):
        path = candidate / 'gta4l/ram-root-sdhci-audit-evidence/boot-dtb-v5.bin'
        if path.is_file():
            evidence = path
            break
    source_dtb = uefi / 'Resources/DTBs' / f"{board['firmware']['config']}.dtb"
    config_name = package.get('defconfig', {}).get(profile_name, '')
    inputs = unixnu / 'out/gta4l' / profile_name / config_name.removesuffix('_defconfig') / 'xnu-inputs.json'

    inventory = {
        'device': {**checkout(device), 'revision': git(device, 'rev-parse', 'HEAD')},
        'unixnu': checkout(unixnu),
        'firmware': {**checkout(uefi), 'basecore': checkout(uefi / 'Mu_Basecore')},
        'board': {'schema_version': board.get('schema_version'), 'name': board['board'].get('name'),
                  'os_profile': profile_name, 'soc_source': board['board'].get('soc_source'),
                  'components': board.get('components', {}).get('include', [])},
        'profile': {field: profile['os'].get(field) for field in PROFILE_FIELDS},
        'ipsw': {key: profile['ipsw'].get(key) for key in
                 ('url', 'product', 'version', 'build', 'device_class', 'chip_id', 'board_id', 'fetch')},
        'ipsw_members': profile['ipsw'].get('sha256', {}),
        'feeds': {entry['name']: {key: entry.get(key) for key in ('kind', 'ref', 'commit')}
                  for entry in load(device / 'feeds.toml').get('source', [])},
        'lock': lock.get('source', {}),
        'components': components,
        'firmware_feed': feed(unixnu_feeds, 'edk2'),
        'local_overrides': {'unixnu.local.toml': (unixnu / 'unixnu.local.toml').is_file()},
        'package': {'unixnu': package.get('unixnu'), 'boot_partition_bytes': package.get('boot_partition_bytes'),
                    'defconfig': package.get('defconfig'),
                    'script_sha256': sha256(script) if script.is_file() else None,
                    'template_sha256': sha256(template) if template.is_file() else None,
                    'template_match': script.is_file() and template.is_file() and script.read_bytes() == template.read_bytes()},
        'boot_dtb': {'source': {'path': str(source_dtb) if source_dtb.is_file() else None,
                                'full_sha256': sha256(source_dtb) if source_dtb.is_file() else None,
                                'first': first_fdt(source_dtb) if source_dtb.is_file() else None},
                     'evidence': ({'path': str(evidence), 'bytes': evidence.stat().st_size,
                                   'sha256': sha256(evidence)} if evidence else None)},
        'kernelcache': ({'path': str(kernelcache), 'bytes': kernelcache.stat().st_size,
                         'sha256': sha256(kernelcache)} if kernelcache.is_file() else None),
        'xnu_inputs': None,
    }
    if inputs.is_file():
        recorded = json.loads(inputs.read_text())
        inventory['xnu_inputs'] = {
            'path': str(inputs),
            'source': recorded.get('source'),
            'commit': recorded.get('commit'),
            'patches': [{'name': Path(item['path']).name, 'sha256': item['sha256']}
                        for item in recorded.get('patches', [])],
        }

    json.dump(inventory, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write('\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
