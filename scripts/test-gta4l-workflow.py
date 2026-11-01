#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host-only contract checks for .github/workflows/package-gta4l.yml.

The workflow is parsed with an available YAML backend (the Python ``yaml``
module, then Ruby Psych).  Both are YAML 1.1 parsers that read the unquoted
``on`` key as a boolean, so the top-level key is normalized before the checks.

The checks are static: manual trigger, read-only permissions, immutable action
and source pins, the exact OS profile, host tests before release construction,
checkout paths, tool and interpreter assertions, seed handling, shell syntax
and the intentional absence of artifact publication.  No network, runner,
build or device operation is performed.

``scripts/test-gta4l-inventory.py`` is not in the workflow's host-test step
yet: the pinned UniXNU commit predates ``scripts/unixnu/inspection.py``.  Add
it to both when the pin advances.
"""
import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = ROOT / '.github/workflows/package-gta4l.yml'
UNIXNU_PIN = 'f50d5bd5093ede81f8c1897b8ce5c93473c0fb7b'
MU_SILICIUM_PIN = 'e14626a3757007a2dbc902d3db323620d5568923'
PROFILE = 'ipados-17.7.11-21H461'
ACTIONS = {
    'actions/checkout': '3d3c42e5aac5ba805825da76410c181273ba90b1',
    'actions/setup-python': '6d4fd22b20465ccc0f7354476588e8df7c9ca0e0',
}
CHECK_STEP = 'Check the profile, sources and patch order'
PACKAGE_STEP = 'Package the iPadOS 17 profile'
HOST_STEP = 'Run the host contract tests'

PYTHON_YAML = """
import json, sys, yaml
with open(sys.argv[1]) as stream:
    print(json.dumps(yaml.safe_load(stream)))
"""
RUBY_YAML = """
require 'yaml'
require 'json'
print JSON.generate(YAML.load_file(ARGV[0]))
"""


def parse_workflow():
    """Return the workflow mapping with the YAML 1.1 ``on`` key normalized."""
    backends = (
        [sys.executable, '-c', PYTHON_YAML, str(WORKFLOW)],
        ['python3', '-c', PYTHON_YAML, str(WORKFLOW)],
        ['ruby', '-e', RUBY_YAML, str(WORKFLOW)],
    )
    problems = []
    for command in backends:
        result = subprocess.run(command, text=True, capture_output=True)
        if result.returncode == 0 and result.stdout.strip():
            data = json.loads(result.stdout)
            if 'true' in data and 'on' not in data:
                data['on'] = data.pop('true')
            return data
        problems.append(f'{command[0]}: {result.stderr.strip() or "no output"}')
    raise SystemExit('FAIL no YAML parser backend: ' + '; '.join(problems))


class WorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = parse_workflow()
        cls.job = cls.data['jobs']['package']
        cls.steps = cls.job['steps']
        cls.names = [step.get('name', '') for step in cls.steps]

    def step(self, name):
        matches = [step for step in self.steps if step.get('name') == name]
        self.assertEqual(len(matches), 1, name)
        return matches[0]

    def run_text(self, name):
        return self.step(name).get('run', '')

    def test_manual_trigger_permissions_and_concurrency(self):
        self.assertEqual(self.data['name'], 'package-gta4l')
        self.assertEqual(self.data['on'], {'workflow_dispatch': None})
        self.assertEqual(self.data['permissions'], {'contents': 'read'})
        self.assertEqual(self.data['concurrency'], {
            'group': 'package-gta4l-${{ github.ref }}', 'cancel-in-progress': False})

    def test_job_runner_timeout_and_environment(self):
        self.assertEqual(self.job['runs-on'], 'macos-26')
        self.assertEqual(self.job['timeout-minutes'], 360)
        self.assertEqual(self.job['defaults'], {'run': {'shell': 'bash'}})
        self.assertEqual(self.job['env']['DEVELOPER_DIR'],
                         '/Applications/Xcode_26.6.app/Contents/Developer')
        self.assertEqual(len(set(self.names)), len(self.names))

    def test_checkouts_use_immutable_pins_and_no_stored_credentials(self):
        device = self.step('Check out the device repository')
        self.assertEqual(device['uses'],
                         f'actions/checkout@{ACTIONS["actions/checkout"]}')
        self.assertEqual(device['with']['path'], 'device')
        self.assertIs(device['with']['persist-credentials'], False)
        self.assertNotIn('ref', device['with'])
        self.assertNotIn('submodules', device['with'])

        unixnu = self.step('Check out UniXNU')
        self.assertEqual(unixnu['uses'],
                         f'actions/checkout@{ACTIONS["actions/checkout"]}')
        self.assertEqual(unixnu['with']['repository'], 'DokuroMitsukai/UniXNU')
        self.assertEqual(unixnu['with']['ref'], UNIXNU_PIN)
        self.assertEqual(unixnu['with']['path'], 'UniXNU')
        self.assertEqual(unixnu['with']['submodules'], 'recursive')
        self.assertIs(unixnu['with']['persist-credentials'], False)

        firmware = self.step('Check out Mu-Silicium')
        self.assertEqual(firmware['uses'],
                         f'actions/checkout@{ACTIONS["actions/checkout"]}')
        self.assertEqual(firmware['with']['repository'], 'DokuroMitsukai/Mu-Silicium')
        self.assertEqual(firmware['with']['ref'], MU_SILICIUM_PIN)
        self.assertEqual(firmware['with']['path'], 'Mu-Silicium')
        self.assertIs(firmware['with']['persist-credentials'], False)
        self.assertNotIn('submodules', firmware['with'])

        expected = [(f'actions/checkout@{ACTIONS["actions/checkout"]}',)] * 3
        expected.append((f'actions/setup-python@{ACTIONS["actions/setup-python"]}',))
        uses = [step['uses'] for step in self.steps if 'uses' in step]
        self.assertEqual([(item,) for item in uses], expected)

    def test_setup_python_and_interpreter_assertions(self):
        setup = self.step('Set up Python')
        self.assertEqual(setup['with']['python-version'], '3.13')
        interpreter = self.run_text('Assert the interpreter and Xcode')
        self.assertIn('sys.version_info >= (3, 11)', interpreter)
        self.assertIn('test -d "$DEVELOPER_DIR"', interpreter)
        self.assertIn('xcodebuild -version', interpreter)
        self.assertIn('xcrun --find clang', interpreter)

    def test_tool_assertions_match_the_shared_tool_lookup(self):
        tools = self.run_text('Install and assert tools')
        for package in ('dtc', 'llvm', 'lld', 'cmake', 'mtools', 'ninja', 'unifdef'):
            self.assertIn(package, tools)
        for tool in ('clang', 'llvm-objcopy', 'llvm-nm', 'fdtput', 'fdtget',
                     'compression_tool', 'hdiutil', 'mformat', 'mcopy', 'unifdef',
                     'xcodebuild'):
            self.assertIn(tool, tools)
        self.assertNotIn('ipsw', tools)
        self.assertNotIn('aea', tools)
        self.assertIn('$(brew --prefix llvm)', tools)
        self.assertIn('$(brew --prefix lld)', tools)
        self.assertIn('llvm-config', tools)
        self.assertIn('ld.lld', tools)

    def test_seed_is_validated_without_being_echoed(self):
        seed = self.step('Validate the platform seed')
        self.assertEqual(seed['env']['seed'], '${{ secrets.UNIXNU_UNIQUE_CHIP_ID }}')
        self.assertIn('^[0-9a-fA-F]{8}$', seed['run'])
        self.assertNotIn('echo "$seed"', seed['run'])
        self.assertNotIn('echo $seed', seed['run'])
        for name in ('Run the host contract tests', CHECK_STEP, PACKAGE_STEP):
            self.assertNotIn('UNIXNU_UNIQUE_CHIP_ID=', self.run_text(name))

    def test_host_contract_tests_precede_release_construction(self):
        host = self.run_text(HOST_STEP)
        for script in ('scripts/test-gta4l-workflow.py',
                       'scripts/test-gta4l-packaging.py',
                       'scripts/test-gta4l-boot-image.py',
                       'scripts/test-gta4l-diagnostic-payload.py'):
            self.assertIn(script, host)
        host_index = self.names.index(HOST_STEP)
        self.assertLess(self.names.index('Set up Python'), host_index)
        release = [index for index, step in enumerate(self.steps)
                   if 'scripts/package.sh' in step.get('run', '')]
        self.assertEqual(len(release), 2)
        self.assertTrue(all(host_index < index for index in release))

    def test_release_commands_use_the_exact_profile_and_dependency_policy(self):
        check = self.run_text(CHECK_STEP)
        self.assertEqual(check,
                         f'scripts/package.sh --os {PROFILE} --uefi ../Mu-Silicium --check')
        package = self.run_text(PACKAGE_STEP)
        self.assertEqual(package,
                         f'scripts/package.sh --os {PROFILE} --uefi ../Mu-Silicium --no-deps')
        for name in (CHECK_STEP, PACKAGE_STEP):
            self.assertEqual(self.step(name)['working-directory'], 'device')
            environment = self.step(name)['env']
            self.assertEqual(environment['UNIXNU_UNIQUE_CHIP_ID'],
                             '${{ secrets.UNIXNU_UNIQUE_CHIP_ID }}')
            self.assertEqual(environment['GITHUB_TOKEN'], '${{ github.token }}')
        self.assertNotIn('--os 17', WORKFLOW.read_text())

    def test_publication_is_disabled(self):
        text = WORKFLOW.read_text()
        self.assertNotIn('upload-artifact', text)
        self.assertNotIn('download-artifact', text)
        self.assertNotIn('if: success()', text)
        self.assertIn('Image publication is disabled', text)

    def test_comment_pins_match_the_checkout_pins(self):
        text = WORKFLOW.read_text()
        self.assertIn(UNIXNU_PIN, text)
        self.assertIn(MU_SILICIUM_PIN, text)
        for step in self.steps:
            ref = step.get('with', {}).get('ref')
            if ref is not None:
                self.assertRegex(ref, r'^[0-9a-f]{40}$')

    def test_embedded_shell_scripts_parse(self):
        for step in self.steps:
            script = step.get('run')
            if not script:
                continue
            result = subprocess.run(['bash', '-n'], input=script, text=True,
                                    capture_output=True)
            self.assertEqual(result.returncode, 0,
                             f"{step.get('name')}: {result.stderr.strip()}")


if __name__ == '__main__':
    unittest.main()
