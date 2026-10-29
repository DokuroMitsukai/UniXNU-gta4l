#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Self-test for scripts/extract-gta4l-firmware.py.

The test is host-only. It uses the committed gzip-wrapped firmware volume and
synthetic malformed inputs. It checks that:

- the committed volume passes the strict `--validate` gate with its expected
  module inventory;
- the three analysed driver PE hashes are present and unchanged;
- the JSON form parses, including with `--validate`;
- a boot image whose declared kernel overruns the file, and an appended DTB
  with a bad header, size or block bounds are rejected;
- an FV whose declared length exceeds the bytes present, an FV with an invalid
  header length or header checksum, an FV with an extended header that overruns
  the volume or hides an executable FFS, an FFS header on a PE-containing file
  with a bad type, state or checksum, a malformed section stream, a malformed
  trailing outer FFS entry, and non-erased trailing FV bytes are rejected
  instead of being skipped;
- a short or out-of-range GUIDed LZMA section is rejected;
- a non-boot image, a truncated gzip kernel, a corrupt gzip volume and a raw
  garbage volume are rejected with a clean `extract:` error and no traceback.
"""
import gzip
import importlib.util
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts' / 'extract-gta4l-firmware.py'
VOLUME = ROOT / 'SILICIUM_UEFI.fd'
LZMA_GUID = bytes.fromhex('98584eee143959429d6edc7bd79403cf')
DRIVERS = {
    'SPMIDxe': '50be2e1a0972843af5de67d4d0b80aba68634259849459897922716a25d3e254',
    'SdccDxe': '4084bbc745d66fd81eae0caf7eacb19f7204076d4ee88adc7cda45c949d5674b',
    'PmicDxe': 'db541f6302efee1546beea9b4dee9362fca5f50bcec2f494e560c150464e32ce',
}
EXPECTED_MODULES = 90

def load_module():
    spec = importlib.util.spec_from_file_location('gta4l_firmware', SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expect_error(module, path, as_fv, needle):
    try:
        module.extract(path, as_fv)
    except module.FirmwareError as error:
        assert needle in str(error), f'wrong error for {path}: {error}'
    else:
        raise AssertionError(f'{path} was accepted')


def expect_modules_error(module, volume, needle):
    try:
        module.collect_modules(volume)
    except module.FirmwareError as error:
        assert needle in str(error), f'wrong error: {error}'
    else:
        raise AssertionError(f'mutated volume was accepted ({needle})')


def expect_guided_error(module, section, needle):
    try:
        module.guided_lzma(section)
    except module.FirmwareError as error:
        assert needle in str(error), f'wrong error: {error}'
    else:
        raise AssertionError(f'GUIDed section was accepted ({needle})')


def expect_outer_error(module, volume, needle):
    try:
        module.inner_fv(volume)
    except module.FirmwareError as error:
        assert needle in str(error), f'wrong error: {error}'
    else:
        raise AssertionError(f'mutated outer volume was accepted ({needle})')


def expect_validate_error(module, path, as_fv, needle):
    try:
        module.validate_result(module.extract(path, as_fv))
    except module.FirmwareError as error:
        assert needle in str(error), f'wrong error for {path}: {error}'
    else:
        raise AssertionError(f'{path} passed --validate ({needle})')


def minimal_dtb():
    header = bytearray(48)
    header[0:4] = b'\xd0\x0d\xfe\xed'
    struct.pack_into('>I', header, 4, 48)
    struct.pack_into('>I', header, 8, 48)
    struct.pack_into('>I', header, 12, 48)
    struct.pack_into('>I', header, 16, 40)
    struct.pack_into('>I', header, 20, 17)
    struct.pack_into('>I', header, 24, 16)
    return bytes(header)


def boot_image(kernel):
    data = bytearray(4096 + len(kernel))
    data[:8] = b'ANDROID!'
    struct.pack_into('<10I', data, 8, len(kernel), 0, 0, 0, 0, 0, 0,
                     4096, 1, 0)
    data[4096:] = kernel
    return bytes(data)


def run_cli(path, *args):
    result = subprocess.run(
        [sys.executable, str(SCRIPT), str(path), *args],
        text=True, capture_output=True)
    assert result.returncode == 1, f'{path}: exit {result.returncode}'
    assert 'Traceback' not in result.stderr, result.stderr
    assert result.stderr.startswith('extract: '), result.stderr
    return result


def main():
    module = load_module()
    assert VOLUME.is_file(), f'missing {VOLUME}'

    result = module.extract(VOLUME, True)
    count = module.validate_result(result)
    assert count == EXPECTED_MODULES, count
    found = {m['pe_sha256'] for m in result['modules']}
    missing = set(DRIVERS.values()) - found
    assert not missing, f'missing driver PE hashes: {sorted(missing)}'

    cli = subprocess.run(
        [sys.executable, str(SCRIPT), '--fv', str(VOLUME), '--json'],
        text=True, capture_output=True, check=True)
    json.loads(cli.stdout)

    cli = subprocess.run(
        [sys.executable, str(SCRIPT), '--fv', str(VOLUME), '--json',
         '--validate'],
        text=True, capture_output=True, check=True)
    json.loads(cli.stdout)
    assert f'valid: {count} PE modules' in cli.stderr, cli.stderr

    _, outer = module.load_outer(VOLUME)
    inner, _ = module.inner_fv(outer)
    modules = module.collect_modules(inner)
    assert modules, 'committed volume has no PE modules'
    assert len(modules) == EXPECTED_MODULES, len(modules)

    tail = bytearray(inner)
    tail[-1] = 0x00
    expect_modules_error(module, tail, 'trailing')

    last_end = 0
    for offset, file_bytes in module.walk_ffs(outer, strict=True):
        last_end = offset + ((len(file_bytes) + 7) & ~7)
    trailing_outer = bytearray(outer)
    trailing_outer[last_end:last_end + 24] = b'\x01' * 24
    expect_outer_error(module, trailing_outer, 'FFS header')

    declared = bytearray(inner)
    struct.pack_into('<Q', declared, 0x20, len(declared) + 4096)
    expect_modules_error(module, declared, 'FV length')

    header = bytearray(inner)
    struct.pack_into('<H', header, 0x30, 0)
    expect_modules_error(module, header, 'FV header length')

    fv_checksum = bytearray(inner)
    fv_checksum[0x33] ^= 1
    expect_modules_error(module, fv_checksum, 'FV header checksum')

    checksum = bytearray(inner)
    checksum[modules[0]['offset'] + 0x10] ^= 1
    expect_modules_error(module, checksum, 'checksum')

    bad_type = bytearray(inner)
    bad_type[modules[0]['offset'] + 0x12] = 0x34
    expect_modules_error(module, bad_type, 'invalid')

    bad_state = bytearray(inner)
    bad_state[modules[0]['offset'] + 0x17] = 0x40
    expect_modules_error(module, bad_state, 'invalid')

    bad_section = bytearray(inner)
    bad_section[modules[0]['offset'] + 0x18:modules[0]['offset'] + 0x1b] = \
        b'\x00' * 3
    expect_modules_error(module, bad_section, 'section')

    ext_offset = struct.unpack_from('<H', inner, 0x34)[0]
    hidden = bytearray(inner)
    struct.pack_into('<I', hidden, ext_offset + 16,
                     modules[0]['offset'] + modules[0]['size'] - ext_offset)
    expect_modules_error(module, hidden, 'hidden')

    ext_bounds = bytearray(inner)
    struct.pack_into('<I', ext_bounds, ext_offset + 16, len(ext_bounds))
    expect_modules_error(module, ext_bounds, 'extended header size')

    expect_guided_error(module, b'\x04\x00\x00\x02', 'too short')
    expect_guided_error(
        module,
        b'\x18\x00\x00\x02' + LZMA_GUID + b'\x00\x00\x00\x00',
        'outside the section')

    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)

        bad = tmp / 'bad.bin'
        bad.write_bytes(b'hello')
        expect_error(module, bad, False, 'not an Android boot image')
        run_cli(bad)

        garbage = tmp / 'garbage.fd'
        garbage.write_bytes(b'\x00' * 300)
        expect_error(module, garbage, True, 'no firmware volume found')
        run_cli(garbage, '--fv')

        truncated = tmp / 'truncated.img'
        truncated.write_bytes(boot_image(gzip.compress(b'x' * 1024)[:-8]))
        expect_error(module, truncated, False, 'truncated')
        run_cli(truncated)

        kernel_overrun = tmp / 'kernel-overrun.img'
        overrun = bytearray(boot_image(gzip.compress(b'x' * 1024)))
        struct.pack_into('<I', overrun, 8, len(overrun) + 4096)
        kernel_overrun.write_bytes(overrun)
        expect_error(module, kernel_overrun, False, 'overruns the image')
        run_cli(kernel_overrun)

        firmware_gzip = gzip.compress(b'\x00' * 192 + outer)

        good_dtb = tmp / 'good-dtb.img'
        good_dtb.write_bytes(boot_image(firmware_gzip + minimal_dtb()))
        assert module.validate_result(module.extract(good_dtb, False)) == \
            EXPECTED_MODULES

        bad_dtb = tmp / 'bad-dtb.img'
        bad_dtb.write_bytes(boot_image(firmware_gzip + b'\xd0\x0d\xfe\xed'))
        expect_validate_error(module, bad_dtb, False, 'DTB is invalid')
        run_cli(bad_dtb, '--validate')

        bad_dtb_size = tmp / 'bad-dtb-size.img'
        dtb = bytearray(minimal_dtb())
        struct.pack_into('>I', dtb, 4, 0x1000)
        bad_dtb_size.write_bytes(boot_image(firmware_gzip + bytes(dtb)))
        expect_validate_error(module, bad_dtb_size, False, 'DTB is invalid')
        run_cli(bad_dtb_size, '--validate')

        bad_deflate = tmp / 'bad-deflate.fd'
        bad_deflate.write_bytes(b'\x1f\x8b\x08\x00' + b'\x00' * 6 + b'\x07'
                                + b'\x00' * 8)
        expect_error(module, bad_deflate, True, 'gzip volume is corrupt')
        run_cli(bad_deflate, '--fv')

        oversized = tmp / 'oversized.fd'
        prefix, _ = module.load_outer(VOLUME)
        oversized.write_bytes(prefix + declared)
        expect_error(module, oversized, True, 'FV length')
        run_cli(oversized, '--fv')

        corrupt = tmp / 'corrupt.fd'
        corrupt.write_bytes(b'\x1f\x8b' + b'\x00' * 64)
        expect_error(module, corrupt, True, 'gzip volume is corrupt')
        run_cli(corrupt, '--fv')

    print(f'PASS: firmware extraction gate ({count} modules, three drivers)')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
