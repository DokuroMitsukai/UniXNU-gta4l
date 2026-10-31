#!/usr/bin/env python3
"""Build a gta4l diagnostic payload with an explicit raw-ramdisk contract.

The tracked board.toml is never modified. A temporary copy is made, its
[payload] size is set to the requested diagnostic size, and
[payload.adt] packed_data_ramdisk is forced to false because the RAM-root
fixture is a raw N4KROOT image. The payload is built by the normal UniXNU
payload step and checked by scripts/check-gta4l-payload.py before it is copied
to --output or a manifest is written.
"""
import argparse
import datetime
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / 'scripts/check-gta4l-payload.py'
DIAGNOSTICS = {
    'host-init': 'gta4l-sdhci-host-init=1',
    'audit': 'gta4l-sdhci-audit=1',
    'snapshot': 'gta4l-sdhci-snapshot=1',
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def set_diagnostic_board(board_text, size_mib):
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
            lines[index] = 'packed_data_ramdisk = false\n'
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
        print('PASS: raw diagnostic board forces size_mib=80 and '
              'packed_data_ramdisk=false')
        return 0

    missing = [name for name in ('data_ramdisk', 'trust_cache',
                                 'unique_chip_id', 'diagnostic', 'output')
               if getattr(args, name) is None]
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
    if not args.data_ramdisk.is_file():
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
    data_hash = sha(args.data_ramdisk)
    trust_hash = sha(args.trust_cache)

    workdir = Path(tempfile.mkdtemp(prefix='gta4l-diagnostic-payload-'))
    try:
        temp_device = workdir / device.name
        shutil.copytree(
            device, temp_device,
            ignore=shutil.ignore_patterns('.git', 'out', '__pycache__',
                                          '*.pyc'))
        (temp_device / 'board.toml').write_text(
            set_diagnostic_board(board, args.payload_size_mib))
        temp_hash = sha(temp_device / 'board.toml')

        command = [
            sys.executable, str(unixnu / 'unixnu'), 'build', 'payload',
            '-D', str(temp_device),
            '--uefi', str(uefi),
            *(['--kernel', str(args.kernel.resolve())] if args.kernel else []),
            '--unique-chip-id', args.unique_chip_id,
            '--data-ramdisk', str(args.data_ramdisk.resolve()),
            '--owner-trust-cache' if args.owner_trust_cache else '--trust-cache',
            str(args.trust_cache.resolve()),
        ]
        for value in args.kernel_arg:
            command.extend(['--kernel-arg', value])
        command.extend(['--kernel-arg', DIAGNOSTICS[args.diagnostic]])
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
            'packed_data_ramdisk': False,
            'diagnostic': args.diagnostic,
            'diagnostic_argument': DIAGNOSTICS[args.diagnostic],
            'kernel_args': args.kernel_arg,
            'data_ramdisk': {
                'path': str(args.data_ramdisk.resolve()),
                'bytes': args.data_ramdisk.stat().st_size,
                'sha256': data_hash,
            },
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
