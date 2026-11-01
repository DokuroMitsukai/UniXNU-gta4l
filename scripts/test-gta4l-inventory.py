#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host-only self-test for scripts/audit-gta4l-resolved-inputs.py.

The fixture is a synthetic device, UniXNU and firmware tree with local Git
repositories.  Nothing fetches, builds, writes production output or touches a
device.
"""
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AUDIT_PATH = Path(__file__).resolve().with_name('audit-gta4l-resolved-inputs.py')
SPEC = importlib.util.spec_from_file_location('gta4l_inventory', AUDIT_PATH)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
PROFILE = 'ipados-17.7.11-21H461'
SECRET = 'ghp_' + 's' * 36


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(text, bytes):
        path.write_bytes(text)
    else:
        path.write_text(text)
    return path


def git(path, *arguments):
    environment = dict(os.environ, GIT_AUTHOR_NAME='Fixture', GIT_AUTHOR_EMAIL='fixture@example.invalid',
                       GIT_COMMITTER_NAME='Fixture', GIT_COMMITTER_EMAIL='fixture@example.invalid')
    subprocess.run(['git', '-C', str(path), *arguments], check=True,
                   capture_output=True, text=True, env=environment)


def repository(path, files):
    path.mkdir(parents=True, exist_ok=True)
    git(path, 'init', '-q')
    for name, text in files.items():
        write(path / name, text)
    git(path, 'add', '-A')
    git(path, 'commit', '-q', '-m', 'fixture')


class Fixture:
    def __init__(self, base, component_repo=True):
        self.base = base
        self.device = base / 'device'
        self.unixnu = base / 'UniXNU'
        self.uefi = base / 'Mu-Silicium'
        self.soc = base / 'soc'
        self.out = self.unixnu / 'out'
        self.patch = write(self.device / 'patches/xnu/local.patch', 'device patch\n')
        self.series = write(self.device / 'patches/xnu/series', 'local.patch\n')
        repository(self.device, {
            'board.toml': (
                'schema_version = 2\n'
                '[board]\nname = "gta4l"\nsoc_source = "sm6115"\n'
                f'os_profile = "{PROFILE}"\n'
                '[components]\ninclude = ["Foo"]\n'
                '[package]\nunixnu = "../UniXNU"\nboot_partition_bytes = 100663296\n'
                f'[package.defconfig]\n"{PROFILE}" = "gta4l_defconfig"\n'
                '[kernel]\ndefconfig = "gta4l_defconfig"\npatch_series = ["device:patches/xnu/series"]\n'
                '[firmware]\nconfig = "gta4l"\n'
                '[payload]\nunique_chip_id = "required"\n'),
            'configs/gta4l_defconfig': 'CONFIG_XNU_NATIVE_PAN=n\n',
            'feeds.toml': ('[[source]]\nname = "sm6115"\nkind = "soc"\nlocal = "../soc"\n'),
            'scripts/package.sh': '#!/bin/sh\nexit 0\n',
        })
        repository(self.soc, {'soc.toml': '[soc]\nname = "SM6115"\n'})
        files = {
            'profiles/arch/arm64/profile.toml': '[arch]\nname = "arm64"\n[os]\narch = "arm64"\n',
            'profiles/os/' + PROFILE + '/profile.toml': (
                f'[os]\nname = "{PROFILE}"\narch = "arm64"\nxnu_version = "10063.141.1"\n'
                '[ipsw]\nurl = "https://example.invalid/restore.ipsw?token=secretvalue&sig=signaturevalue"\n'
                'product = "iPad7,1"\nversion = "17.7.11"\nbuild = "21H461"\n'
                'device_class = "j120ap"\nchip_id = 32785\nboard_id = 12\n'
                '[kernel.prelink]\nkernelcache_sha256 = "' + 'a' * 64 + '"\n'),
            'components.toml': ('[[component]]\nid = "Foo"\nkind = "driver"\n'
                                'repository = "https://user:password@example.invalid/Foo.git"\n'
                                'revision = "' + 'e' * 40 + '"\npath = "drivers/Foo"\n'),
            'feeds.toml': ('[[source]]\nname = "edk2"\nkind = "refer"\n'
                           'git = "https://token:' + 'p' * 12 + '@example.invalid/mu_basecore"\n'
                           'ref = "release/202608"\ncommit = "' + 'c' * 40 + '"\n'),
            'templates/package.sh': '#!/bin/sh\nexit 0\n',
            'drivers/Foo/Kit.toml': '[kit]\nname = "Foo"\n',
        }
        self.unixnu.mkdir(parents=True, exist_ok=True)
        (self.unixnu / 'scripts').symlink_to(ROOT.parent / 'UniXNU/scripts')
        repository(self.unixnu, files)
        if not component_repo:
            (self.unixnu / 'drivers/Foo').mkdir(parents=True, exist_ok=True)
        repository(self.uefi, {'.gitignore': 'Mu_Basecore/\n'})
        write(self.uefi / 'Resources/DTBs/gta4l.dtb', b'\xd0\x0d\xfe\xed')
        git(self.uefi, 'add', '-A')
        git(self.uefi, 'commit', '-q', '-m', 'fixture dtb')
        repository(self.uefi / 'Mu_Basecore', {'MdePkg.dec': '[defines]\n'})

    def inventory(self):
        return AUDIT.inventory(self.device, self.unixnu, self.uefi, self.out)


def write_kernelcache(fixture, data=b'wrong kernelcache'):
    write(fixture.out / 'ipsw/21H461/kernelcache.decompressed', data)


def write_report(fixture, patches=None, source=None, commit=None):
    recorded = {'source': source or str(fixture.unixnu / 'refer/xnu'),
                'commit': commit or 'd' * 40,
                'patches': patches if patches is not None else [
                    {'path': str(fixture.patch), 'sha256': AUDIT.sha256(fixture.patch)}]}
    write(fixture.out / 'gta4l' / PROFILE / 'gta4l/xnu-inputs.json', json.dumps(recorded))


class InventoryTests(unittest.TestCase):
    def test_declared_effective_and_cached_sections_are_separate(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            write_kernelcache(fixture)
            write_report(fixture)
            result = fixture.inventory()
            self.assertTrue(result['effective']['resolved'], result['effective']['errors'])
            self.assertEqual(result['effective']['selection']['config'], 'gta4l_defconfig')
            self.assertIn('board', result['declared'])
            self.assertEqual([item['path'] for item in result['effective']['profile_files']][0],
                             str(fixture.unixnu / 'profiles/arch/arm64/profile.toml'))
            foo = result['effective']['components']['Foo']
            self.assertEqual(foo['checkout']['state'], 'in-parent-checkout')
            self.assertIsNone(foo['match'])
            self.assertTrue(result['package']['template_match'])
            self.assertFalse(result['cached']['kernelcache']['match'])
            comparison = result['cached']['xnu_inputs']['comparison']
            self.assertTrue(comparison['patch_order_match'])
            self.assertEqual(comparison['hash_mismatches'], [])
            self.assertFalse((fixture.out / 'state.json').exists())

    def test_missing_caches_and_uninitialised_submodule_are_unresolved(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            (fixture.uefi / 'Mu_Basecore/MdePkg.dec').unlink()
            (fixture.uefi / 'Mu_Basecore/.git').rename(fixture.base / 'basecore-git')
            result = fixture.inventory()
            reasons = {(item['kind'], item['name']): item['reason'] for item in result['unresolved']}
            self.assertEqual(reasons[('cache', 'kernelcache')], 'not cached')
            self.assertEqual(reasons[('cache', 'xnu-inputs')], 'not cached')
            self.assertEqual(reasons[('git', 'firmware_basecore')], 'enclosing-repository')
            self.assertTrue(result['effective']['resolved'])
            self.assertIsNone(result['cached']['kernelcache'])

    def test_malformed_report_and_changed_patch_preserve_both_identities(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            malformed = fixture.out / 'gta4l' / PROFILE / 'gta4l/xnu-inputs.json'
            write(malformed, '{not json')
            result = fixture.inventory()
            self.assertEqual(result['cached']['xnu_inputs']['state'], 'malformed')
            self.assertTrue(any(item['name'] == 'xnu-inputs' for item in result['unresolved']))
            recorded = [{'path': str(fixture.patch), 'sha256': '0' * 64}]
            write_report(fixture, patches=recorded)
            fixture.patch.write_text('changed device patch\n')
            result = fixture.inventory()
            comparison = result['cached']['xnu_inputs']['comparison']
            self.assertEqual(comparison['hash_mismatches'][0]['recorded'], '0' * 64)
            self.assertEqual(comparison['hash_mismatches'][0]['current'], AUDIT.sha256(fixture.patch))

    def test_reordered_report_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            extra = write(fixture.device / 'patches/xnu/other.patch', 'other\n')
            write_report(fixture, patches=[
                {'path': str(extra), 'sha256': AUDIT.sha256(extra)},
                {'path': str(fixture.patch), 'sha256': AUDIT.sha256(fixture.patch)}])
            result = fixture.inventory()
            self.assertFalse(result['cached']['xnu_inputs']['comparison']['patch_order_match'])

    def test_secrets_and_platform_seed_never_reach_the_output(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            board = (fixture.device / 'board.toml').read_text().replace(
                'unique_chip_id = "required"', 'unique_chip_id = "deadbeef"')
            write(fixture.device / 'board.toml', board)
            write(fixture.device / 'local.toml', '[local]\n[build]\ntoken = "' + SECRET + '"\n')
            result = fixture.inventory()
            output = json.dumps(result)
            for secret in (SECRET, 'deadbeef', 'secretvalue', 'signaturevalue', 'password'):
                self.assertNotIn(secret, output)
            self.assertEqual(result['declared']['board']['payload']['unique_chip_id'], '<redacted>')
            self.assertTrue(result['overrides']['device_local_toml']['present'])

    def test_missing_soc_cache_fails_effectively_without_raising(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            write(fixture.device / 'feeds.toml',
                  '[[source]]\nname = "sm6115"\nkind = "soc"\n'
                  'git = "https://example.invalid/soc"\ncommit = "' + 'f' * 40 + '"\n')
            result = fixture.inventory()
            self.assertFalse(result['effective']['resolved'])
            self.assertIsNone(result['effective']['selection'])
            self.assertTrue(any(item['kind'] == 'effective' for item in result['unresolved']))

    def test_fdt_header_helpers(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'dtb'
            body = b'body'
            path.write_bytes(b'\xd0\x0d\xfe\xed' + (40 + len(body)).to_bytes(4, 'big') + b'\0' * 32 + body)
            self.assertEqual(AUDIT.first_fdt(path)['bytes'], 44)
            path.write_bytes(b'not a dtb')
            self.assertIsNone(AUDIT.first_fdt(path))

    def test_checkout_states(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            clean = base / 'clean'
            repository(clean, {'file': 'text'})
            self.assertEqual(AUDIT.checkout(clean)['state'], 'clean')
            (clean / 'file').write_text('changed')
            self.assertEqual(AUDIT.checkout(clean)['state'], 'dirty')
            self.assertEqual(AUDIT.checkout(base / 'missing')['state'], 'missing')
            empty = clean / 'empty'
            empty.mkdir()
            self.assertEqual(AUDIT.checkout(empty)['state'], 'enclosing-repository')
            standalone = base / 'standalone'
            standalone.mkdir()
            self.assertEqual(AUDIT.checkout(standalone)['state'], 'not-a-git-work-tree')

    def test_git_unavailable_is_its_own_state(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(AUDIT.subprocess, 'run', side_effect=FileNotFoundError('git')):
            self.assertEqual(AUDIT.checkout(Path(directory))['state'], 'git-unavailable')

    def test_main_writes_deterministic_json(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Fixture(Path(directory))
            output = StringIO()
            with redirect_stdout(output):
                self.assertEqual(AUDIT.main(['--device', str(fixture.device), '--unixnu', str(fixture.unixnu),
                                             '--uefi', str(fixture.uefi), '--out', str(fixture.out)]), 0)
            first = json.loads(output.getvalue())
            output = StringIO()
            with redirect_stdout(output):
                AUDIT.main(['--device', str(fixture.device), '--unixnu', str(fixture.unixnu),
                            '--uefi', str(fixture.uefi), '--out', str(fixture.out)])
            self.assertEqual(output.getvalue(), json.dumps(first, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    unittest.main()
