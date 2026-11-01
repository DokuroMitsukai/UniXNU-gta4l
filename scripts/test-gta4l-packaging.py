#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host-only checks for the gta4l packaging contract.

Verifies the board package values, the resolved iPadOS 17 identity, the local
component pins, the byte-identical canonical script, the boot-DTB preflight
staging and the launcher's interpreter propagation.  It does not fetch sources,
build an image, run `unixnu release` or touch a device.
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UNIXNU = ROOT.parent / 'UniXNU'
PACKAGE_BYTES = 100_663_296
SCRIPT_SHA256 = 'f7b8e3ea043058b9c61d5b45c32b8e7669a85737c89d55d404957c3b43ec4aab'
PROFILE = 'ipados-17.7.11-21H461'
XNU_REVISION = 'd8b80295118ef25ac3a784134bcf95cd8e88109f'
KERNELCACHE_SHA256 = 'f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7'
COMPONENTS = ('ArmDTPlatform', 'ArmCpu', 'SoftwareAES', 'XnuPrelink', 'DarwinBoot')


def need(condition, message):
    if not condition:
        raise SystemExit(f'FAIL {message}')


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git_head(path):
    return subprocess.run(['git', '-C', str(path), 'rev-parse', 'HEAD'],
                          text=True, capture_output=True, check=True).stdout.strip()


def make_tree(work):
    tree = work / 'tree'
    device = work / 'device'
    (tree / 'scripts').mkdir(parents=True)
    (tree / 'kits').mkdir()
    (device / 'package').mkdir(parents=True)
    (device / 'board.toml').write_bytes((ROOT / 'board.toml').read_bytes())
    (tree / 'scripts/prepare-gta4l-boot-dtb.py').write_bytes(
        (ROOT / 'scripts/prepare-gta4l-boot-dtb.py').read_bytes())
    (tree / 'kits/gta4l').symlink_to(device / 'package')
    return tree, device


def run_preflight(tree, uefi=None, keep_uefi=False):
    environment = dict(os.environ)
    if keep_uefi:
        environment['UNIXNU_UEFI'] = str(uefi)
    else:
        environment.pop('UNIXNU_UEFI', None)
    return subprocess.run(
        [sys.executable, str(tree / 'scripts/prepare-gta4l-boot-dtb.py')],
        env=environment, text=True, capture_output=True)


def write_source(uefi, body):
    (uefi / 'Resources/DTBs').mkdir(parents=True, exist_ok=True)
    (uefi / 'Resources/DTBs/gta4l.dtb').write_bytes(body)


def fdt_header(totalsize):
    return b'\xd0\x0d\xfe\xed' + totalsize.to_bytes(4, 'big') + b'\0' * 32


def check_identity():
    board = tomllib.loads((ROOT / 'board.toml').read_text())

    package = board.get('package', {})
    need(package.get('unixnu') == '../UniXNU', 'package.unixnu')
    need(package.get('boot_partition_bytes') == PACKAGE_BYTES, 'package.boot_partition_bytes')
    need(package.get('defconfig') == {PROFILE: 'gta4l_defconfig'}, 'package.defconfig mapping')
    need(board.get('board', {}).get('os_profile') == PROFILE, 'board.os_profile')

    config = board.get('firmware', {}).get('config')
    need(config == 'gta4l', 'firmware.config')
    need(board.get('bootimg', {}).get('dtb') == f'build/{config}/boot.dtb', 'bootimg.dtb')
    need('scripts/prepare-gta4l-boot-dtb.py' in board.get('firmware', {}).get('preflight', []),
         'firmware preflight')

    installed = sorted(path.name for path in (UNIXNU / 'profiles/os').iterdir() if path.is_dir())
    need(installed == [PROFILE], f'installed OS profiles {installed}')

    profile = tomllib.loads((UNIXNU / 'profiles/os' / PROFILE / 'profile.toml').read_text())
    need(profile['os']['name'] == PROFILE, 'profile os name')
    need(profile['os'].get('arch', 'arm64') == 'arm64', 'profile arch')
    need(profile['os']['xnu_version'] == '10063.141.1', 'profile xnu version')
    need(profile['os']['xnu_revision'] == XNU_REVISION, 'profile xnu revision')
    need(profile['ipsw']['product'] == 'iPad7,1', 'profile IPSW product')
    need(profile['ipsw']['version'] == '17.7.11', 'profile IPSW version')
    need(profile['ipsw']['build'] == '21H461', 'profile IPSW build')
    need(profile['ipsw']['device_class'] == 'j120ap', 'profile IPSW device class')
    need(profile['ipsw']['chip_id'] == 32785 and profile['ipsw']['board_id'] == 12, 'profile IPSW ids')
    need(profile['kernel']['prelink']['kernelcache_sha256'] == KERNELCACHE_SHA256, 'profile kernelcache')

    lock = tomllib.loads((ROOT / 'unixnu.lock').read_text())
    need(lock['source']['xnu']['commit'] == XNU_REVISION, 'lock xnu revision')
    feeds = tomllib.loads((ROOT / 'feeds.toml').read_text())
    sm6115 = next(source for source in feeds['source'] if source['name'] == 'sm6115')
    need(sm6115['commit'] == lock['source']['sm6115']['commit'], 'sm6115 feed and lock')

    catalog = {entry['id']: entry for entry in
               tomllib.loads((UNIXNU / 'components.toml').read_text())['component']}
    for identifier in COMPONENTS:
        need(identifier in board.get('components', {}).get('include', []), f'{identifier} is not included')
        entry = catalog[identifier]
        local = UNIXNU / entry['path']
        need(local.is_dir(), f'{identifier} checkout is missing')
        need(git_head(local) == entry['revision'], f'{identifier} local checkout differs from components.toml')

    kernelcache = UNIXNU / 'out/ipsw/21H461/kernelcache.decompressed'
    if kernelcache.is_file():
        need(sha256(kernelcache) == KERNELCACHE_SHA256, 'cached kernelcache hash')
    else:
        print('SKIP cached kernelcache is not present')

    script = ROOT / 'scripts/package.sh'
    need(script.is_file(), 'scripts/package.sh')
    need(os.access(script, os.X_OK), 'scripts/package.sh is executable')
    need(sha256(script) == SCRIPT_SHA256, 'canonical script hash')
    template = UNIXNU / 'templates/package.sh'
    if template.is_file():
        need(script.read_bytes() == template.read_bytes(), 'package.sh matches templates/package.sh')
    else:
        print('SKIP UniXNU templates/package.sh is not present')


def check_preflight():
    with tempfile.TemporaryDirectory(prefix='gta4l-packaging-') as name:
        work = Path(name)
        tree, device = make_tree(work)
        uefi = work / 'uefi'

        body = fdt_header(48) + b'trailing'
        write_source(uefi, body)
        result = run_preflight(tree, uefi, keep_uefi=True)
        need(result.returncode == 0, f'preflight failed: {result.stderr.strip()}')
        staged = tree / 'build/gta4l/boot.dtb'
        need(staged.is_file(), 'preflight output missing')
        need(staged.read_bytes() == body[:48], 'preflight did not truncate to totalsize')
        need(f'{staged.stat().st_size} bytes' in result.stdout, 'preflight did not report the size')

        (tree / 'build/gta4l/boot.dtb').unlink()
        result = run_preflight(tree, uefi)
        need(result.returncode == 1 and 'UNIXNU_UEFI is not set' in result.stderr, 'missing UNIXNU_UEFI')
        need(not staged.exists(), 'preflight wrote output without UNIXNU_UEFI')

        result = run_preflight(tree, work / 'absent', keep_uefi=True)
        need(result.returncode == 1 and 'missing firmware device tree' in result.stderr, 'missing source DTB')

        write_source(uefi, b'not an fdt' + b'\0' * 32)
        result = run_preflight(tree, uefi, keep_uefi=True)
        need(result.returncode == 1 and 'not an FDT' in result.stderr, 'bad FDT magic')

        write_source(uefi, b'\xd0\x0d\xfe\xed' + (8).to_bytes(4, 'big') + b'\0' * 32)
        result = run_preflight(tree, uefi, keep_uefi=True)
        need(result.returncode == 1 and 'invalid FDT totalsize 8' in result.stderr, 'short FDT totalsize')

        write_source(uefi, fdt_header(1024) + b'\0' * 24)
        result = run_preflight(tree, uefi, keep_uefi=True)
        need(result.returncode == 1 and 'invalid FDT totalsize 1024' in result.stderr, 'overlong FDT totalsize')

        write_source(uefi, fdt_header(52) + b'\0' * 8 + b'longer')
        need(run_preflight(tree, uefi, keep_uefi=True).returncode == 0, 'stale-output setup failed')
        need(staged.stat().st_size == 52, 'stale-output setup size')
        write_source(uefi, fdt_header(48) + b'\0' * 8 + b'tail')
        need(run_preflight(tree, uefi, keep_uefi=True).returncode == 0, 'stale-output overwrite failed')
        need(staged.stat().st_size == 48, 'preflight kept a stale output tail')
        need(staged.read_bytes() == fdt_header(48) + b'\0' * 8, 'preflight wrote stale bytes')


def check_launcher():
    with tempfile.TemporaryDirectory(prefix='gta4l-launcher-') as name:
        work = Path(name)
        fake = work / 'fake'
        fake.mkdir()
        (fake / 'unixnu').write_text(
            'import json, sys\n'
            'print(json.dumps({"argv": sys.argv[1:], "version": list(sys.version_info[:2])}))\n')
        device = work / 'launcher'
        (device / 'scripts').mkdir(parents=True)
        (device / 'scripts/package.sh').write_bytes((ROOT / 'scripts/package.sh').read_bytes())
        os.chmod(device / 'scripts/package.sh', 0o755)
        (device / 'board.toml').write_text('[package]\nunixnu = "../fake"\n')
        shim = work / 'bin'
        shim.mkdir()
        (shim / 'python3').symlink_to(sys.executable)
        environment = dict(os.environ, PATH=f'{shim}:{os.environ["PATH"]}')
        result = subprocess.run(
            [str(device / 'scripts/package.sh'), '--os', PROFILE, '--check'],
            env=environment, text=True, capture_output=True)
        need(result.returncode == 0, f'launcher failed: {result.stderr.strip()}')
        recorded = json.loads(result.stdout.strip().splitlines()[-1])
        need(tuple(recorded['version']) >= (3, 11), f'launcher used Python {recorded["version"]}')
        need(recorded['argv'] == ['release', '-D', str(device), '--os', PROFILE, '--check'],
             f'launcher arguments {recorded["argv"]}')


def main() -> int:
    check_identity()
    check_preflight()
    check_launcher()
    print('PASS gta4l packaging contract')
    return 0


if __name__ == '__main__':
    sys.exit(main())
