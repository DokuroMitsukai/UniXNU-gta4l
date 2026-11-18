#!/usr/bin/env python3
"""Build a gta4l diagnostic payload with an explicit raw-ramdisk contract.

The tracked board.toml is never modified. A temporary copy is made, its
[payload] size is set to the requested diagnostic size, and
[payload.adt] packed_data_ramdisk is forced to false because the RAM-root
fixture is a raw N4KROOT image. The payload is built by the normal UniXNU
payload step and checked by scripts/check-gta4l-payload.py before it is copied
to --output or a manifest is written. The partobs, partcmp, partread, deblock,
breakup, sweep, consume and width personalities additionally require
--data-ramdisk-manifest naming the fs-timer-hold fixture, so the old
early-success-panic RAM-root fixture cannot be repackaged for the observation.
The widthhold personality maps to gta4l-emmc-block=3 gta4l-emmc-widthhold=1
and the widthpub personality maps to gta4l-emmc-block=3
gta4l-emmc-widthpub=1. The speed personality maps to gta4l-emmc-block=3
gta4l-emmc-speed=1 and also requires the fs-timer-hold fixture. The bulk
personality maps to gta4l-emmc-block=3 gta4l-emmc-bulk=1 and also requires
the fs-timer-hold fixture. The live personality maps to
gta4l-emmc-block=3 gta4l-emmc-bulk=1 gta4l-emmc-dma=1 gta4l-emmc-live=1
and also requires the fs-timer-hold fixture; force-slow is a host-only
token and is not a payload personality. The windows personality maps to
gta4l-emmc-block=3 gta4l-emmc-windows=1 and also requires the
fs-timer-hold fixture. The root personality maps to
gta4l-emmc-block=3 gta4l-emmc-windows=1 gta4l-emmc-verify=1
gta4l-emmc-dma=1 gta4l-emmc-root=1 and refuses a Data ramdisk: the
boot must select the verified OS window by UUID, not md0. The root-data
personality uses the same five tokens but carries the packed owner Data
seed and the original trust bundle for the full root boot; it requires
--data-seed-manifest and refuses the fs-timer fixture.
"""
import argparse
import datetime
import hashlib
import json
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / 'scripts/check-gta4l-payload.py'

# The one launchd override allowed on the mutable Data seed. launchd only
# honours disabled.plist when the file is owned by uid 0, so the seed builder
# runs as root on the Mac and records the exact ownership here.
WATCHDOG_POLICY_PATH = '/private/var/db/com.apple.xpc.launchd/disabled.plist'
WATCHDOG_POLICY_TREE_PATH = '/db/com.apple.xpc.launchd/disabled.plist'
WATCHDOG_POLICY = {'com.apple.watchdogd': True}
WATCHDOG_POLICY_BYTES = plistlib.dumps(WATCHDOG_POLICY,
                                       fmt=plistlib.FMT_BINARY)
WATCHDOG_POLICY_SHA256 = hashlib.sha256(WATCHDOG_POLICY_BYTES).hexdigest()
BASE_SEED_RAW_SHA256 = (
    'd2c4bc743d05a33246f1523d2cc3b5cd2c2217628f91ac0315bc09261f50b02f')
BASE_SEED_REGULAR_FILES = 39
BASE_SEED_REGULAR_BYTES = 611715


def check_launchd_watchdog_policy(seed, report, report_sha256, dbr, label):
    """Require exactly one added, root-owned launchd policy file."""
    def need(condition, message):
        if not condition:
            raise SystemExit(f'{label}: {message}')

    policy = seed.get('launchd_disabled')
    need(isinstance(policy, dict), 'launchd_disabled must be an object')
    expected = {
        'path': WATCHDOG_POLICY_PATH,
        'plist': WATCHDOG_POLICY,
        'uid': 0,
        'gid': 0,
        'mode': '0644',
        'bytes': len(WATCHDOG_POLICY_BYTES),
        'sha256': WATCHDOG_POLICY_SHA256,
    }
    for key, value in expected.items():
        need(policy.get(key) == value,
             f'launchd_disabled.{key} must be {value!r}')
    need(policy.get('tree_added') == [WATCHDOG_POLICY_TREE_PATH],
         'launchd_disabled.tree_added must name only the policy file')
    need(policy.get('tree_removed') == [] and
         policy.get('tree_changed') == [],
         'launchd_disabled must not remove or change other entries')

    need(report.get('run_as_uid') == 0,
         'seed report must come from a root build')
    need(report.get('mounted_with_owners') is True,
         'seed report must mount the image with owners enabled')
    need(report.get('source_raw_sha256') == BASE_SEED_RAW_SHA256,
         'seed report source is not the recorded original')

    raw = report.get('output_raw_sha256')
    need(raw == seed.get('sha256'),
         'seed report raw hash does not match the manifest')
    need(report.get('output_raw_logical_bytes') == seed.get('bytes'),
         'seed report logical size does not match the manifest')
    need(report.get('output_dbr_sha256') ==
         hashlib.sha256(dbr.read_bytes()).hexdigest(),
         'seed report packed hash does not match the DBRDISK1 file')

    need(report.get('tree_added') == [WATCHDOG_POLICY_TREE_PATH],
         'seed report added tree entries differ')
    need(report.get('tree_removed') == [] and
         report.get('tree_changed') == [],
         'seed report removed or changed tree entries')
    reported = report.get('policy')
    need(isinstance(reported, dict), 'seed report policy must be an object')
    for key, value in expected.items():
        if key == 'sha256':
            continue
        need(reported.get(key) == value,
             f'seed report policy.{key} must be {value!r}')
    need(report.get('policy_bytes_sha256') == WATCHDOG_POLICY_SHA256,
         'seed report policy bytes differ from the canonical plist')
    need(report.get('source_regular_files') == BASE_SEED_REGULAR_FILES + 1,
         'seed report must add exactly one regular file')
    need(report.get('source_regular_bytes') ==
         BASE_SEED_REGULAR_BYTES + len(WATCHDOG_POLICY_BYTES),
         'seed report added bytes differ from the policy file')
    need(seed.get('source_regular_files') ==
         report.get('source_regular_files') and
         seed.get('source_regular_bytes') ==
         report.get('source_regular_bytes'),
         'seed manifest and report file totals differ')
    need(seed.get('os_inputs_modified') is False,
         'seed manifest must keep os_inputs_modified false')
    need(policy.get('builder_report_sha256') == report_sha256,
         'seed manifest builder_report_sha256 does not match the report')


DIAGNOSTICS = {
    'host-init': 'gta4l-sdhci-host-init=1',
    'audit': 'gta4l-sdhci-audit=1',
    'snapshot': 'gta4l-sdhci-snapshot=1',
    'emmc-probe-1': 'gta4l-emmc-probe=1',
    'emmc-probe-2': 'gta4l-emmc-probe=2',
    'emmc-probe-3': 'gta4l-emmc-probe=3',
    'emmc-probe-4': 'gta4l-emmc-probe=4',
    'emmc-probe-5': 'gta4l-emmc-probe=5',
    'emmc-probe-6': 'gta4l-emmc-probe=6',
    'emmc-probe-7': 'gta4l-emmc-probe=7',
    'emmc-probe-8': 'gta4l-emmc-probe=8',
    'emmc-probe-9': 'gta4l-emmc-probe=9',
    'emmc-probe-10': 'gta4l-emmc-probe=10',
    'emmc-probe-11': 'gta4l-emmc-probe=11',
    'emmc-probe-12': 'gta4l-emmc-probe=12',
    'emmc-probe-13': 'gta4l-emmc-probe=13',
    'emmc-probe-14': 'gta4l-emmc-probe=14',
    'emmc-probe-15': 'gta4l-emmc-probe=15',
    'emmc-probe-16': 'gta4l-emmc-probe=16',
    'emmc-probe-17': 'gta4l-emmc-probe=17',
    'emmc-block-1': 'gta4l-emmc-block=1',
    'emmc-block-3': 'gta4l-emmc-block=3',
    'emmc-block-1-partobs': 'gta4l-emmc-block=1 gta4l-emmc-partobs=1',
    'emmc-block-3-partobs': 'gta4l-emmc-block=3 gta4l-emmc-partobs=1',
    'emmc-block-1-partcmp': 'gta4l-emmc-block=1 gta4l-emmc-partcmp=1',
    'emmc-block-3-partcmp': 'gta4l-emmc-block=3 gta4l-emmc-partcmp=1',
    'emmc-block-1-partread': 'gta4l-emmc-block=1 gta4l-emmc-partread=1',
    'emmc-block-3-partread': 'gta4l-emmc-block=3 gta4l-emmc-partread=1',
    'emmc-block-1-deblock': 'gta4l-emmc-block=1 gta4l-emmc-deblock=1',
    'emmc-block-3-deblock': 'gta4l-emmc-block=3 gta4l-emmc-deblock=1',
    'emmc-block-1-breakup': 'gta4l-emmc-block=1 gta4l-emmc-breakup=1',
    'emmc-block-3-breakup': 'gta4l-emmc-block=3 gta4l-emmc-breakup=1',
    'emmc-block-1-sweep': 'gta4l-emmc-block=1 gta4l-emmc-sweep=1',
    'emmc-block-3-sweep': 'gta4l-emmc-block=3 gta4l-emmc-sweep=1',
    'emmc-block-3-consume': 'gta4l-emmc-block=3 gta4l-emmc-consume=1',
    'emmc-block-3-width4': 'gta4l-emmc-block=3 gta4l-emmc-width=4',
    'emmc-block-3-widthhold': 'gta4l-emmc-block=3 gta4l-emmc-widthhold=1',
    'emmc-block-3-widthpub': 'gta4l-emmc-block=3 gta4l-emmc-widthpub=1',
    'emmc-block-3-speed': 'gta4l-emmc-block=3 gta4l-emmc-speed=1',
    'emmc-block-3-bulk': 'gta4l-emmc-block=3 gta4l-emmc-bulk=1',
    'emmc-block-3-bulk-dma': 'gta4l-emmc-block=3 gta4l-emmc-bulk=1 '
                             'gta4l-emmc-dma=1',
    'emmc-block-3-live': 'gta4l-emmc-block=3 gta4l-emmc-bulk=1 '
                         'gta4l-emmc-dma=1 gta4l-emmc-live=1',
    'emmc-block-3-windows': 'gta4l-emmc-block=3 gta4l-emmc-windows=1',
    'emmc-block-3-verify': 'gta4l-emmc-block=3 gta4l-emmc-windows=1 '
                           'gta4l-emmc-verify=1 gta4l-emmc-dma=1',
    'emmc-block-3-vread': 'gta4l-emmc-block=3 gta4l-emmc-windows=1 '
                          'gta4l-emmc-verify=1 gta4l-emmc-dma=1 '
                          'gta4l-emmc-vread=1',
    'emmc-block-3-root': 'gta4l-emmc-block=3 gta4l-emmc-windows=1 '
                         'gta4l-emmc-verify=1 gta4l-emmc-dma=1 '
                         'gta4l-emmc-root=1',
    'emmc-block-3-root-data': 'gta4l-emmc-block=3 gta4l-emmc-windows=1 '
                              'gta4l-emmc-verify=1 gta4l-emmc-dma=1 '
                              'gta4l-emmc-root=1',
}
PARTOBS_DIAGNOSTICS = ('emmc-block-1-partobs', 'emmc-block-3-partobs')
PARTCMP_DIAGNOSTICS = ('emmc-block-1-partcmp', 'emmc-block-3-partcmp')
PARTREAD_DIAGNOSTICS = ('emmc-block-1-partread', 'emmc-block-3-partread')
DEBLOCK_DIAGNOSTICS = ('emmc-block-1-deblock', 'emmc-block-3-deblock')
BREAKUP_DIAGNOSTICS = ('emmc-block-1-breakup', 'emmc-block-3-breakup')
SWEEP_DIAGNOSTICS = ('emmc-block-1-sweep', 'emmc-block-3-sweep')
CONSUME_DIAGNOSTICS = ('emmc-block-3-consume',)
WIDTH_DIAGNOSTICS = ('emmc-block-3-width4',)
WIDTHHOLD_DIAGNOSTICS = ('emmc-block-3-widthhold',)
WIDTHPUB_DIAGNOSTICS = ('emmc-block-3-widthpub',)
SPEED_DIAGNOSTICS = ('emmc-block-3-speed',)
BULK_DIAGNOSTICS = ('emmc-block-3-bulk', 'emmc-block-3-bulk-dma')
LIVE_DIAGNOSTICS = ('emmc-block-3-live',)
WINDOWS_DIAGNOSTICS = ('emmc-block-3-windows', 'emmc-block-3-verify',
                        'emmc-block-3-vread')
ROOT_DIAGNOSTICS = ('emmc-block-3-root',)
ROOT_DATA_DIAGNOSTICS = ('emmc-block-3-root-data',)
PARTITION_DIAGNOSTICS = (PARTOBS_DIAGNOSTICS + PARTCMP_DIAGNOSTICS +
                         PARTREAD_DIAGNOSTICS)
OBSERVATION_DIAGNOSTICS = (PARTITION_DIAGNOSTICS + DEBLOCK_DIAGNOSTICS +
                           BREAKUP_DIAGNOSTICS + SWEEP_DIAGNOSTICS +
                           CONSUME_DIAGNOSTICS + WIDTH_DIAGNOSTICS +
                           WIDTHHOLD_DIAGNOSTICS + WIDTHPUB_DIAGNOSTICS +
                           SPEED_DIAGNOSTICS + BULK_DIAGNOSTICS +
                           LIVE_DIAGNOSTICS + WINDOWS_DIAGNOSTICS)
DATA_RAMDISK_DIAGNOSTICS = ('report', 'fs-timer', 'fs-timer-hold')
NO_DATA_RAMDISK_DIAGNOSTICS = ROOT_DIAGNOSTICS


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def set_diagnostic_board(board_text, size_mib, packed=False):
    lines = board_text.splitlines(keepends=True)
    section = None
    found_size = False
    found_packed = False
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith('[') and stripped.endswith(']'):
            section = stripped
            continue
        if section == '[payload]' and re.match(r'^size_mib\s*=', stripped):
            lines[index] = f'size_mib = {size_mib}\n'
            found_size = True
        elif (section == '[payload.adt]' and
              re.match(r'^packed_data_ramdisk\s*=', stripped)):
            lines[index] = ('packed_data_ramdisk = true\n' if packed else
                            'packed_data_ramdisk = false\n')
            found_packed = True
    if not found_size or not found_packed:
        raise SystemExit('board.toml lacks [payload] size_mib or '
                         '[payload.adt] packed_data_ramdisk')
    return ''.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', type=Path, default=ROOT)
    parser.add_argument('--unixnu', type=Path)
    parser.add_argument('--uefi', type=Path,
                        help='Mu-Silicium checkout for the panic screen '
                             '(default: sibling Mu-Silicium)')
    parser.add_argument('--data-ramdisk', type=Path)
    parser.add_argument('--data-ramdisk-manifest', type=Path,
                        help='fixture.json describing the data-ramdisk '
                             'diagnostic; required for the observation '
                             'personalities, which require fs-timer-hold')
    parser.add_argument('--data-seed-manifest', type=Path,
                        help='seed.json describing the packed owner Data seed; '
                             'required for emmc-block-3-root-data')
    parser.add_argument('--data-seed-report', type=Path,
                        help='seed-build.json from the root seed builder; '
                             'required when the seed declares launchd_disabled')
    parser.add_argument('--require-launchd-watchdog-policy', action='store_true',
                        help='fail unless the seed adds exactly the root-owned '
                             'com.apple.watchdogd disabled.plist')
    parser.add_argument('--trust-cache', type=Path)
    parser.add_argument('--owner-trust-cache', action='store_true',
                        help='use --owner-trust-cache instead of --trust-cache')
    parser.add_argument('--kernel', type=Path,
                        help='prelinked kernel Mach-O to package (default: the '
                             'UniXNU build-tree kernel)')
    parser.add_argument('--unique-chip-id')
    parser.add_argument('--diagnostic', choices=tuple(DIAGNOSTICS))
    parser.add_argument('--kernel-arg', action='append', default=[],
                        help='extra kernel argument; repeatable')
    parser.add_argument('--payload-size-mib', type=int, default=80)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--force', action='store_true')
    parser.add_argument('--keep-workdir', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()

    if args.self_test:
        board = (args.device / 'board.toml').read_text()
        transformed = set_diagnostic_board(board, 80)
        parsed = tomllib.loads(transformed)
        assert parsed['payload']['size_mib'] == 80
        assert parsed['payload']['adt']['packed_data_ramdisk'] is False
        packed = tomllib.loads(set_diagnostic_board(board, 80, True))
        assert packed['payload']['adt']['packed_data_ramdisk'] is True
        print('PASS: diagnostic board forces size_mib=80, raw by default and '
              'packed on request')
        return 0

    required = ['trust_cache', 'unique_chip_id', 'diagnostic', 'output']
    if args.diagnostic not in NO_DATA_RAMDISK_DIAGNOSTICS:
        required.append('data_ramdisk')
    if args.diagnostic in ROOT_DATA_DIAGNOSTICS:
        required.append('data_seed_manifest')
    missing = [name for name in required if getattr(args, name) is None]
    if missing:
        parser.error('missing required options: ' +
                     ', '.join('--' + name.replace('_', '-')
                               for name in missing))
    device = args.device.resolve()
    unixnu = (args.unixnu or (device.parent / 'UniXNU')).resolve()
    uefi = (args.uefi or (unixnu.parent / 'Mu-Silicium')).resolve()
    output = args.output.resolve()
    manifest_path = (args.manifest.resolve() if args.manifest else
                     Path(str(output) + '.manifest.json'))
    if not (device / 'board.toml').is_file():
        raise SystemExit(f'{device}: board.toml not found')
    if not (unixnu / 'unixnu').is_file():
        raise SystemExit(f'{unixnu}: unixnu not found')
    if not uefi.is_dir():
        raise SystemExit(f'{uefi}: Mu-Silicium checkout not found')
    if args.diagnostic in NO_DATA_RAMDISK_DIAGNOSTICS:
        if args.data_ramdisk is not None:
            raise SystemExit(
                f'--diagnostic {args.diagnostic} boots from the verified eMMC '
                'window; do not pass --data-ramdisk')
        if args.data_ramdisk_manifest is not None:
            raise SystemExit(
                f'--diagnostic {args.diagnostic} has no Data ramdisk; '
                'do not pass --data-ramdisk-manifest')
        if args.data_seed_manifest is not None:
            raise SystemExit(
                f'--diagnostic {args.diagnostic} has no Data ramdisk; '
                'do not pass --data-seed-manifest')
    if args.diagnostic in ROOT_DATA_DIAGNOSTICS:
        if args.data_ramdisk_manifest is not None:
            raise SystemExit(
                f'--diagnostic {args.diagnostic} uses the owner Data seed; '
                'do not pass the fs-timer --data-ramdisk-manifest')
        if args.data_ramdisk.read_bytes()[:8] != b'DBRDISK1':
            raise SystemExit(
                f'{args.data_ramdisk}: root-data requires the packed DBRDISK1 '
                'seed')
    if args.data_ramdisk is not None and not args.data_ramdisk.is_file():
        raise SystemExit(f'{args.data_ramdisk}: data ramdisk not found')
    if not args.trust_cache.is_file():
        raise SystemExit(f'{args.trust_cache}: trust cache not found')
    if args.kernel is not None and not args.kernel.is_file():
        raise SystemExit(f'{args.kernel}: kernel not found')
    if output.exists() and not args.force:
        raise SystemExit(f'{output}: output exists; pass --force to replace')
    if manifest_path.exists() and not args.force:
        raise SystemExit(f'{manifest_path}: manifest exists; pass --force')
    if args.payload_size_mib <= 0:
        raise SystemExit('--payload-size-mib must be positive')

    board = (device / 'board.toml').read_text()
    config = tomllib.loads(board)
    os_profile = config['board']['os_profile']
    board_hash = sha(device / 'board.toml')
    data_hash = sha(args.data_ramdisk) if args.data_ramdisk else None
    trust_hash = sha(args.trust_cache)

    data_ramdisk_manifest = None
    data_ramdisk_diagnostic = None
    data_ramdisk_hold = None
    data_ramdisk_manifest_hash = None
    if args.data_ramdisk_manifest is not None:
        if not args.data_ramdisk_manifest.is_file():
            raise SystemExit(
                f'{args.data_ramdisk_manifest}: data-ramdisk manifest not found')
        try:
            data_ramdisk_manifest = json.loads(
                args.data_ramdisk_manifest.read_text())
        except json.JSONDecodeError as error:
            raise SystemExit(
                f'{args.data_ramdisk_manifest}: invalid JSON: {error}')
        data_ramdisk_diagnostic = data_ramdisk_manifest.get('diagnostic')
        if data_ramdisk_diagnostic not in DATA_RAMDISK_DIAGNOSTICS:
            raise SystemExit(
                f'{args.data_ramdisk_manifest}: unknown or missing '
                'data-ramdisk diagnostic; expected one of ' +
                ', '.join(DATA_RAMDISK_DIAGNOSTICS))
        data_ramdisk_hold = data_ramdisk_diagnostic == 'fs-timer-hold'
        declared_hold = data_ramdisk_manifest.get('hold_after_success')
        if declared_hold is not None and declared_hold != data_ramdisk_hold:
            raise SystemExit(
                f'{args.data_ramdisk_manifest}: hold_after_success disagrees '
                'with the diagnostic')
        data_ramdisk_manifest_hash = sha(args.data_ramdisk_manifest)
    data_seed = None
    data_seed_manifest_hash = None
    if args.data_seed_manifest is not None:
        if args.diagnostic not in ROOT_DATA_DIAGNOSTICS:
            raise SystemExit(
                '--data-seed-manifest is only for emmc-block-3-root-data')
        if not args.data_seed_manifest.is_file():
            raise SystemExit(
                f'{args.data_seed_manifest}: data-seed manifest not found')
        try:
            data_seed = json.loads(args.data_seed_manifest.read_text())
        except json.JSONDecodeError as error:
            raise SystemExit(
                f'{args.data_seed_manifest}: invalid JSON: {error}')
        if data_seed.get('guest_mutable') is not True:
            raise SystemExit(
                f'{args.data_seed_manifest}: seed is not guest mutable')
        if (data_seed.get('owner_keybag_created') is not False or
                data_seed.get('os_inputs_modified') is not False):
            raise SystemExit(
                f'{args.data_seed_manifest}: seed provenance is not clean')
        try:
            uuid.UUID(data_seed['volume_uuid'])
        except (KeyError, ValueError):
            raise SystemExit(
                f'{args.data_seed_manifest}: invalid volume UUID')
        if args.diagnostic in ROOT_DATA_DIAGNOSTICS:
            sys.path.insert(0, str(unixnu / 'drivers/DarwinBoot/Test'))
            import packed_ramdisk
            try:
                packed, _ = packed_ramdisk.inspect(args.data_ramdisk)
            except ValueError as error:
                raise SystemExit(f'{args.data_ramdisk}: {error}')
            if (not packed['packed'] or
                    packed['bytes'] != data_seed.get('bytes') or
                    packed['sha256'] != data_seed.get('sha256')):
                raise SystemExit(
                    f'{args.data_seed_manifest}: logical seed does not match '
                    'the packed image')
            data_seed['packed'] = packed
            if args.data_seed_report is not None and not data_seed.get(
                    'launchd_disabled'):
                raise SystemExit(
                    f'{args.data_seed_report}: a seed report requires a seed '
                    'that declares launchd_disabled')
            if data_seed.get('launchd_disabled'):
                if args.data_seed_report is None:
                    raise SystemExit(
                        f'{args.data_seed_manifest}: launchd_disabled requires '
                        '--data-seed-report')
                if not args.data_seed_report.is_file():
                    raise SystemExit(
                        f'{args.data_seed_report}: seed report not found')
                try:
                    seed_report = json.loads(
                        args.data_seed_report.read_text())
                except json.JSONDecodeError as error:
                    raise SystemExit(
                        f'{args.data_seed_report}: invalid JSON: {error}')
                check_launchd_watchdog_policy(
                    data_seed, seed_report, sha(args.data_seed_report),
                    args.data_ramdisk, str(args.data_seed_manifest))
                data_seed['report'] = {
                    'path': str(args.data_seed_report.resolve()),
                    'sha256': sha(args.data_seed_report),
                }
            elif args.require_launchd_watchdog_policy:
                raise SystemExit(
                    f'{args.data_seed_manifest}: '
                    '--require-launchd-watchdog-policy needs the exact '
                    'root-owned watchdogd disabled.plist')
        else:
            if data_seed.get('sha256') != data_hash:
                raise SystemExit(
                    f'{args.data_seed_manifest}: sha256 does not match the seed')
            if data_seed.get('bytes') != args.data_ramdisk.stat().st_size:
                raise SystemExit(
                    f'{args.data_seed_manifest}: bytes does not match the seed')
        data_seed_manifest_hash = sha(args.data_seed_manifest)
    if args.diagnostic in OBSERVATION_DIAGNOSTICS:
        if data_ramdisk_diagnostic is None:
            raise SystemExit(
                'the observation personalities require '
                '--data-ramdisk-manifest naming the fs-timer-hold fixture; '
                'the old fs-timer fixture panics after success and can '
                'preempt the eMMC report')
        if not data_ramdisk_hold:
            raise SystemExit(
                f'{args.data_ramdisk_manifest}: the observation '
                'personalities require the fs-timer-hold data ramdisk; got '
                f'{data_ramdisk_diagnostic}')

    workdir = Path(tempfile.mkdtemp(prefix='gta4l-diagnostic-payload-'))
    try:
        temp_device = workdir / device.name
        shutil.copytree(
            device, temp_device,
            ignore=shutil.ignore_patterns('.git', 'out', 'build',
                                          '__pycache__', '*.pyc'))
        (temp_device / 'board.toml').write_text(
            set_diagnostic_board(board, args.payload_size_mib,
                                 args.diagnostic in ROOT_DATA_DIAGNOSTICS))
        temp_hash = sha(temp_device / 'board.toml')

        command = [
            sys.executable, str(unixnu / 'unixnu'), 'build', 'payload',
            '-D', str(temp_device),
            '--uefi', str(uefi),
            *(['--kernel', str(args.kernel.resolve())] if args.kernel else []),
            '--unique-chip-id', args.unique_chip_id,
            *(['--data-ramdisk', str(args.data_ramdisk.resolve())]
              if args.data_ramdisk else []),
            '--owner-trust-cache' if args.owner_trust_cache else '--trust-cache',
            str(args.trust_cache.resolve()),
        ]
        for value in args.kernel_arg:
            command.extend(['--kernel-arg', value])
        for token in DIAGNOSTICS[args.diagnostic].split():
            command.extend(['--kernel-arg', token])
        print('+ ' + ' '.join(command), flush=True)
        subprocess.run(command, cwd=unixnu, check=True)

        built = (unixnu / 'out/gta4l' / os_profile / 'gta4l/payload/payload.img')
        built_json = built.with_name('payload.json')
        if not built.is_file():
            raise SystemExit(f'{built}: payload output not found')

        contract = subprocess.run(
            [sys.executable, str(CHECKER), '--payload', str(built),
             '--expect-diagnostic', args.diagnostic, '--json'],
            capture_output=True, text=True)
        if contract.returncode != 0:
            if contract.stdout:
                print(contract.stdout, file=sys.stderr)
            if contract.stderr:
                print(contract.stderr, file=sys.stderr)
            raise SystemExit('payload/ADT contract failed; output not published')
        contract_result = json.loads(contract.stdout)

        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(built, output)
        output_hash = sha(output)

        manifest = {
            'schema_version': 1,
            'created_utc': datetime.datetime.now(
                datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
            'device': str(device),
            'unixnu': str(unixnu),
            'uefi': str(uefi),
            'kernel': ({
                'path': str(args.kernel.resolve()),
                'sha256': sha(args.kernel),
            } if args.kernel else None),
            'os_profile': os_profile,
            'source_board_sha256': board_hash,
            'diagnostic_board_sha256': temp_hash,
            'payload_size_mib': args.payload_size_mib,
            'packed_data_ramdisk': args.diagnostic in ROOT_DATA_DIAGNOSTICS,
            'diagnostic': args.diagnostic,
            'diagnostic_argument': DIAGNOSTICS[args.diagnostic],
            'kernel_args': args.kernel_arg,
            'data_ramdisk': ({
                'path': str(args.data_ramdisk.resolve()),
                'bytes': args.data_ramdisk.stat().st_size,
                'sha256': data_hash,
                'manifest': (str(args.data_ramdisk_manifest.resolve())
                             if args.data_ramdisk_manifest else None),
                'manifest_sha256': data_ramdisk_manifest_hash,
                'diagnostic': data_ramdisk_diagnostic,
                'hold_after_success': data_ramdisk_hold,
            } if args.data_ramdisk else None),
            'data_seed': ({
                'path': str(args.data_seed_manifest.resolve()),
                'manifest_sha256': data_seed_manifest_hash,
                'volume_uuid': data_seed['volume_uuid'],
                'seed': data_seed.get('seed'),
                'packed': data_seed.get('packed'),
                'launchd_disabled': data_seed.get('launchd_disabled'),
                'report': data_seed.get('report'),
            } if data_seed else None),
            'trust_cache': {
                'path': str(args.trust_cache.resolve()),
                'bytes': args.trust_cache.stat().st_size,
                'sha256': trust_hash,
                'owner': args.owner_trust_cache,
            },
            'output': {
                'path': str(output),
                'bytes': output.stat().st_size,
                'sha256': output_hash,
            },
            'unixnu_payload_json': str(built_json),
            'payload_contract': contract_result,
            'command': command,
        }
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'wrote {output} ({output.stat().st_size} bytes, {output_hash})')
        print(f'wrote {manifest_path}')
        return 0
    finally:
        if args.keep_workdir:
            print(f'kept workdir {workdir}')
        else:
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
