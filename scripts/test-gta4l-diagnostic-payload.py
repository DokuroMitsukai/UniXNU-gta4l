#!/usr/bin/env python3
"""Host-only tests for build-gta4l-diagnostic-payload.py.

The tests replace the UniXNU driver and the payload contract with temporary
stand-ins. They do not build a payload, access a device, or touch the tracked
board.toml.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import sys
import tempfile
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILDER = ROOT / 'scripts/build-gta4l-diagnostic-payload.py'

FAKE_UNIXNU = '''\
import json, os, sys, tomllib
from pathlib import Path
record = Path(os.environ['FAKE_UNIXNU_RECORD'])
board = Path(os.environ['FAKE_UNIXNU_BOARD'])
record.write_text(json.dumps(sys.argv[1:]))
args = sys.argv[1:]
device = Path(args[args.index('-D') + 1])
text = (device / 'board.toml').read_text()
board.write_text(text)
profile = tomllib.loads(text)['board']['os_profile']
out = Path('out/gta4l') / profile / 'gta4l/payload'
out.mkdir(parents=True, exist_ok=True)
(out / 'payload.img').write_bytes(b'fake payload')
(out / 'payload.json').write_text('{}')
'''

FAKE_CHECKER = '''\
import json, sys
print(json.dumps({'pass': True, 'payload': sys.argv[sys.argv.index('--payload') + 1]}))
'''

BOARD = '''\
[board]
os_profile = "test-profile"

[payload]
size_mib = 192

[payload.adt]
packed_data_ramdisk = true
'''


def load_builder(checker):
    spec = importlib.util.spec_from_file_location('gta4l_payload_builder',
                                                  BUILDER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.CHECKER = checker
    return module


def run(module, argv):
    old = sys.argv
    sys.argv = ['builder', *argv]
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            return module.main()
    finally:
        sys.argv = old


def make_tree(base):
    unixnu = base / 'UniXNU'
    unixnu.mkdir()
    (unixnu / 'unixnu').write_text(FAKE_UNIXNU)
    uefi = base / 'Mu-Silicium'
    uefi.mkdir()
    device = base / 'device'
    device.mkdir()
    (device / 'board.toml').write_text(BOARD)
    kernel = base / 'kernel.prelinked.macho'
    kernel.write_bytes(b'kernel')
    data = base / 'root.img'
    data.write_bytes(b'root')
    trust = base / 'trustcache.bundle'
    trust.write_bytes(b'trust')
    checker = base / 'check-payload.py'
    checker.write_text(FAKE_CHECKER)
    return unixnu, uefi, device, kernel, data, trust, checker


def common_arguments(unixnu, uefi, device, data, trust):
    return [
        '--device', str(device),
        '--unixnu', str(unixnu),
        '--uefi', str(uefi),
        '--data-ramdisk', str(data),
        '--trust-cache', str(trust),
        '--owner-trust-cache',
        '--unique-chip-id', '0x1122334455667788',
        '--diagnostic', 'audit',
        '--kernel-arg', 'rd=md0',
        '--kernel-arg', 'sheng-pmgr=2',
    ]


def test_explicit_kernel(base):
    unixnu, uefi, device, kernel, data, trust, checker = make_tree(base)
    record = base / 'argv.json'
    board_record = base / 'board.json'
    os.environ['FAKE_UNIXNU_RECORD'] = str(record)
    os.environ['FAKE_UNIXNU_BOARD'] = str(board_record)
    output = base / 'payload.img'
    manifest = base / 'payload.manifest.json'
    module = load_builder(checker)
    code = run(module, [
        *common_arguments(unixnu, uefi, device, data, trust),
        '--kernel', str(kernel),
        '--output', str(output),
        '--manifest', str(manifest),
    ])
    assert code == 0
    argv = json.loads(record.read_text())
    assert argv[:2] == ['build', 'payload']
    assert argv[argv.index('--kernel') + 1] == str(kernel.resolve())
    kernel_args = [argv[index + 1] for index, value in enumerate(argv)
                   if value == '--kernel-arg']
    assert kernel_args == ['rd=md0', 'sheng-pmgr=2', 'gta4l-sdhci-audit=1']
    assert argv[-2:] == ['--kernel-arg', 'gta4l-sdhci-audit=1']
    assert '--owner-trust-cache' in argv
    assert '--trust-cache' not in argv
    transformed = tomllib.loads(board_record.read_text())
    assert transformed['payload']['size_mib'] == 80
    assert transformed['payload']['adt']['packed_data_ramdisk'] is False
    data_manifest = json.loads(manifest.read_text())
    assert data_manifest['kernel'] == {
        'path': str(kernel.resolve()),
        'sha256': hashlib.sha256(b'kernel').hexdigest(),
    }
    assert data_manifest['kernel_args'] == ['rd=md0', 'sheng-pmgr=2']
    assert data_manifest['diagnostic_argument'] == 'gta4l-sdhci-audit=1'
    assert data_manifest['packed_data_ramdisk'] is False
    assert data_manifest['source_board_sha256'] != \
        data_manifest['diagnostic_board_sha256']
    assert data_manifest['output']['sha256'] == \
        hashlib.sha256(b'fake payload').hexdigest()
    assert data_manifest['payload_contract']['pass'] is True
    assert data_manifest['command'][0] == sys.executable
    assert output.read_bytes() == b'fake payload'
    print('PASS explicit kernel, argument order, transformed board and manifest')


def test_default_kernel(base):
    unixnu, uefi, device, _kernel, data, trust, checker = make_tree(base)
    record = base / 'argv.json'
    os.environ['FAKE_UNIXNU_RECORD'] = str(record)
    os.environ['FAKE_UNIXNU_BOARD'] = str(base / 'board.json')
    output = base / 'payload.img'
    manifest = base / 'payload.manifest.json'
    module = load_builder(checker)
    code = run(module, [
        *common_arguments(unixnu, uefi, device, data, trust),
        '--output', str(output),
        '--manifest', str(manifest),
    ])
    assert code == 0
    argv = json.loads(record.read_text())
    assert '--kernel' not in argv
    assert json.loads(manifest.read_text())['kernel'] is None
    print('PASS default kernel omits --kernel and records a null manifest entry')


def test_missing_kernel(base):
    unixnu, uefi, device, _kernel, data, trust, checker = make_tree(base)
    os.environ['FAKE_UNIXNU_RECORD'] = str(base / 'argv.json')
    os.environ['FAKE_UNIXNU_BOARD'] = str(base / 'board.json')
    module = load_builder(checker)
    try:
        run(module, [
            *common_arguments(unixnu, uefi, device, data, trust),
            '--kernel', str(base / 'missing.macho'),
            '--output', str(base / 'payload.img'),
            '--manifest', str(base / 'payload.manifest.json'),
        ])
    except SystemExit as exc:
        assert 'kernel not found' in str(exc), exc
    else:
        raise AssertionError('missing kernel was accepted')
    print('PASS missing kernel is rejected before the build')


def main():
    tests = (test_explicit_kernel, test_default_kernel, test_missing_kernel)
    for test in tests:
        with tempfile.TemporaryDirectory(prefix='gta4l-payload-test-') as tmp:
            test(Path(tmp))
    print('PASS gta4l diagnostic payload builder tests')
    return 0


if __name__ == '__main__':
    sys.exit(main())
