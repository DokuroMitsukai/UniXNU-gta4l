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


def check_emmc_probe(base, number):
    unixnu, uefi, device, kernel, data, trust, checker = make_tree(base)
    record = base / 'argv.json'
    os.environ['FAKE_UNIXNU_RECORD'] = str(record)
    os.environ['FAKE_UNIXNU_BOARD'] = str(base / 'board.json')
    output = base / 'payload.img'
    manifest = base / 'payload.manifest.json'
    module = load_builder(checker)
    args = common_arguments(unixnu, uefi, device, data, trust)
    name = f'emmc-probe-{number}'
    argument = f'gta4l-emmc-probe={number}'
    args[args.index('--diagnostic') + 1] = name
    code = run(module, [
        *args,
        '--kernel', str(kernel),
        '--output', str(output),
        '--manifest', str(manifest),
    ])
    assert code == 0
    argv = json.loads(record.read_text())
    kernel_args = [argv[index + 1] for index, value in enumerate(argv)
                   if value == '--kernel-arg']
    assert kernel_args[-1] == argument
    assert json.loads(manifest.read_text())['diagnostic_argument'] == \
        argument
    print(f'PASS {name} maps to {argument}')


def test_emmc_probe_4(base):
    check_emmc_probe(base, 4)


def test_emmc_probe_5(base):
    check_emmc_probe(base, 5)


def test_emmc_probe_6(base):
    check_emmc_probe(base, 6)


def test_emmc_probe_7(base):
    check_emmc_probe(base, 7)


def test_emmc_probe_8(base):
    check_emmc_probe(base, 8)


def test_emmc_probe_9(base):
    check_emmc_probe(base, 9)


def test_emmc_probe_10(base):
    check_emmc_probe(base, 10)


def test_emmc_probe_11(base):
    check_emmc_probe(base, 11)


def test_emmc_probe_12(base):
    check_emmc_probe(base, 12)


def test_emmc_probe_13(base):
    check_emmc_probe(base, 13)


def test_emmc_probe_14(base):
    check_emmc_probe(base, 14)


def test_emmc_probe_15(base):
    check_emmc_probe(base, 15)


def test_emmc_probe_16(base):
    check_emmc_probe(base, 16)


def test_emmc_block_1(base):
    unixnu, uefi, device, kernel, data, trust, checker = make_tree(base)
    record = base / 'argv.json'
    os.environ['FAKE_UNIXNU_RECORD'] = str(record)
    os.environ['FAKE_UNIXNU_BOARD'] = str(base / 'board.json')
    output = base / 'payload.img'
    manifest = base / 'payload.manifest.json'
    module = load_builder(checker)
    args = common_arguments(unixnu, uefi, device, data, trust)
    args[args.index('--diagnostic') + 1] = 'emmc-block-1'
    code = run(module, [
        *args,
        '--kernel', str(kernel),
        '--output', str(output),
        '--manifest', str(manifest),
    ])
    assert code == 0
    argv = json.loads(record.read_text())
    kernel_args = [argv[index + 1] for index, value in enumerate(argv)
                   if value == '--kernel-arg']
    assert kernel_args[-1] == 'gta4l-emmc-block=1'
    assert json.loads(manifest.read_text())['diagnostic_argument'] == \
        'gta4l-emmc-block=1'
    print('PASS emmc-block-1 maps to gta4l-emmc-block=1')


def test_emmc_block_3(base):
    unixnu, uefi, device, kernel, data, trust, checker = make_tree(base)
    record = base / 'argv.json'
    os.environ['FAKE_UNIXNU_RECORD'] = str(record)
    os.environ['FAKE_UNIXNU_BOARD'] = str(base / 'board.json')
    output = base / 'payload.img'
    manifest = base / 'payload.manifest.json'
    module = load_builder(checker)
    args = common_arguments(unixnu, uefi, device, data, trust)
    args[args.index('--diagnostic') + 1] = 'emmc-block-3'
    code = run(module, [
        *args,
        '--kernel', str(kernel),
        '--output', str(output),
        '--manifest', str(manifest),
    ])
    assert code == 0
    argv = json.loads(record.read_text())
    kernel_args = [argv[index + 1] for index, value in enumerate(argv)
                   if value == '--kernel-arg']
    assert kernel_args[-1] == 'gta4l-emmc-block=3'
    assert json.loads(manifest.read_text())['diagnostic_argument'] == \
        'gta4l-emmc-block=3'
    print('PASS emmc-block-3 maps to gta4l-emmc-block=3')


def check_partobs_personality(base, personality, block_mode):
    unixnu, uefi, device, kernel, data, trust, checker = make_tree(base)
    record = base / 'argv.json'
    os.environ['FAKE_UNIXNU_RECORD'] = str(record)
    os.environ['FAKE_UNIXNU_BOARD'] = str(base / 'board.json')
    output = base / 'payload.img'
    manifest = base / 'payload.manifest.json'
    module = load_builder(checker)
    args = common_arguments(unixnu, uefi, device, data, trust)
    args[args.index('--diagnostic') + 1] = personality
    code = run(module, [
        *args,
        '--kernel', str(kernel),
        '--output', str(output),
        '--manifest', str(manifest),
    ])
    assert code == 0
    argv = json.loads(record.read_text())
    kernel_args = [argv[index + 1] for index, value in enumerate(argv)
                   if value == '--kernel-arg']
    assert kernel_args[-2:] == [f'gta4l-emmc-block={block_mode}',
                                'gta4l-emmc-partobs=1'], kernel_args
    argument = f'gta4l-emmc-block={block_mode} gta4l-emmc-partobs=1'
    assert json.loads(manifest.read_text())['diagnostic_argument'] == argument
    print(f'PASS {personality} maps to {argument}')


def test_emmc_block_1_partobs(base):
    check_partobs_personality(base, 'emmc-block-1-partobs', 1)


def test_emmc_block_3_partobs(base):
    check_partobs_personality(base, 'emmc-block-3-partobs', 3)


def test_diagnostic_contract(_base):
    spec = importlib.util.spec_from_file_location(
        'gta4l_payload_checker', ROOT / 'scripts/check-gta4l-payload.py')
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)

    def checks(args, expected='any'):
        return checker.diagnostic_contract(args, expected)

    good = checks('rd=md0 gta4l-emmc-block=1', 'emmc-block-1')
    assert all(good.values()), good
    chunked = checks('rd=md0 gta4l-emmc-block=3', 'emmc-block-3')
    assert all(chunked.values()), chunked
    duplicate = checks('gta4l-emmc-block=1 gta4l-emmc-block=1')
    assert duplicate['exactly_one_diagnostic_argument']
    assert not duplicate['exactly_one_block_mode_token']
    duplicate_chunked = checks(
        'gta4l-emmc-block=3 gta4l-emmc-block=3')
    assert duplicate_chunked['exactly_one_diagnostic_argument']
    assert not duplicate_chunked['exactly_one_block_mode_token']
    mixed = checks('gta4l-emmc-probe=1 gta4l-emmc-block=1')
    assert not mixed['exactly_one_diagnostic_argument']
    mixed_chunked = checks('gta4l-emmc-probe=1 gta4l-emmc-block=3')
    assert not mixed_chunked['exactly_one_diagnostic_argument']
    malformed_probe = checks('gta4l-emmc-block=1 gta4l-emmc-probe=99')
    assert malformed_probe['exactly_one_diagnostic_argument']
    assert not malformed_probe['no_probe_token_in_block_mode']
    stray_block = checks('gta4l-emmc-probe=1 gta4l-emmc-block=2')
    assert stray_block['exactly_one_diagnostic_argument']
    assert not stray_block['no_block_token_outside_block_mode']
    deferred = checks('gta4l-emmc-block=2')
    assert not deferred['exactly_one_diagnostic_argument']
    wrong = checks('gta4l-emmc-block=1', 'emmc-probe-1')
    assert not wrong['emmc-probe-1_diagnostic_selected']
    observed = checks('rd=md0 gta4l-emmc-block=1 gta4l-emmc-partobs=1',
                      'emmc-block-1-partobs')
    assert all(observed.values()), observed
    observed_chunked = checks(
        'gta4l-emmc-block=3 gta4l-emmc-partobs=1', 'emmc-block-3-partobs')
    assert all(observed_chunked.values()), observed_chunked
    orphan = checks('gta4l-emmc-probe=1 gta4l-emmc-partobs=1')
    assert not orphan['no_partobs_token_outside_block_mode']
    duplicate_partobs = checks(
        'gta4l-emmc-block=1 gta4l-emmc-partobs=1 gta4l-emmc-partobs=1',
        'emmc-block-1-partobs')
    assert not duplicate_partobs['at_most_one_partobs_token']
    bad_value = checks('gta4l-emmc-block=1 gta4l-emmc-partobs=2',
                       'emmc-block-1-partobs')
    assert not bad_value['partobs_value_one']
    assert not bad_value['emmc-block-1-partobs_diagnostic_selected']
    plain_expected = checks('gta4l-emmc-block=1 gta4l-emmc-partobs=1',
                            'emmc-block-1')
    assert not plain_expected['emmc-block-1_diagnostic_selected']
    missing_partobs = checks('gta4l-emmc-block=1', 'emmc-block-1-partobs')
    assert not missing_partobs['emmc-block-1-partobs_diagnostic_selected']
    empty = checks('')
    assert not empty['exactly_one_diagnostic_argument']
    assert not empty['unixnu_toml_has_xnu_args']
    print('PASS diagnostic family and block-token contract cases')


def main():
    tests = (test_explicit_kernel, test_default_kernel, test_missing_kernel,
             test_emmc_probe_4, test_emmc_probe_5, test_emmc_probe_6,
             test_emmc_probe_7, test_emmc_probe_8, test_emmc_probe_9,
             test_emmc_probe_10, test_emmc_probe_11, test_emmc_probe_12,
             test_emmc_probe_13, test_emmc_probe_14, test_emmc_probe_15,
             test_emmc_probe_16, test_emmc_block_1, test_emmc_block_3,
             test_emmc_block_1_partobs, test_emmc_block_3_partobs,
             test_diagnostic_contract)
    for test in tests:
        with tempfile.TemporaryDirectory(prefix='gta4l-payload-test-') as tmp:
            test(Path(tmp))
    print('PASS gta4l diagnostic payload builder tests')
    return 0


if __name__ == '__main__':
    sys.exit(main())
