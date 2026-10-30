#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Static host check for the documented SdccDxe power handshake.

The booted SdccDxe PE is pinned by SHA-256. The test maps the documented virtual
addresses to file offsets through the PE section table, checks the instruction
words that implement the reset/status/clear/result sequence, and runs a small
model of that sequence to check the documented selection and timeout behaviour.
It also checks the TLMM address arithmetic and the SDC1 pad-field decode.

This is a static/model check. It does not execute firmware, touch a device, or
establish any electrical behaviour.
"""
import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = (ROOT.parent / 'Mu-Silicium' / 'Binaries' / 'gta4l' / 'QcomPkg' /
            'Drivers' / 'SdccDxe' / 'SdccDxe.efi')
VOLUME = ROOT / 'SILICIUM_UEFI.fd'
EXTRACTOR = ROOT / 'scripts' / 'extract-gta4l-firmware.py'

EXPECTED_SHA256 = '4084bbc745d66fd81eae0caf7eacb19f7204076d4ee88adc7cda45c949d5674b'

STATUS_LIMIT = 10001
CLEAR_LIMIT = 10001
RESET_LIMIT = 10000

WEST_BASE = 0x00500000
SOUTH_BASE = 0x00900000
SDC1_CTL = 0x75000
SDC2_CTL = 0x73000


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def sha256(data):
    import hashlib
    return hashlib.sha256(data).hexdigest()


def load_extractor():
    spec = importlib.util.spec_from_file_location('gta4l_firmware_extract',
                                                  EXTRACTOR)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_firmware():
    if FIRMWARE.is_file():
        return FIRMWARE.read_bytes(), str(FIRMWARE)
    module = load_extractor()
    prefix, outer = module.load_outer(VOLUME)
    inner, _ = module.inner_fv(outer)
    check(inner is not None, f'{VOLUME}: no inner firmware volume')
    for offset, file_bytes in module.walk_ffs(inner, strict=True):
        for typ, section in module.sections(file_bytes, strict=True):
            if typ in module.PE_TYPES:
                pe = section[4:]
                if sha256(pe) == EXPECTED_SHA256:
                    return pe, f'{VOLUME} inner FV module at 0x{offset:x}'
    raise AssertionError(
        f'{VOLUME}: no module with SdccDxe SHA-256 {EXPECTED_SHA256}')


def pe_offset(data, rva):
    e_lfanew = struct.unpack_from('<I', data, 0x3c)[0]
    check(data[e_lfanew:e_lfanew + 4] == b'PE\0\0', 'firmware is not a PE image')
    sections = struct.unpack_from('<H', data, e_lfanew + 6)[0]
    optional_size = struct.unpack_from('<H', data, e_lfanew + 20)[0]
    table = e_lfanew + 24 + optional_size
    for index in range(sections):
        _, vsize, va, raw_size, raw = struct.unpack_from(
            '<8sIIII', data, table + 40 * index)
        span = max(vsize, raw_size)
        if va <= rva < va + span:
            offset = raw + (rva - va)
            check(offset + 4 <= len(data),
                  f'RVA 0x{rva:x} maps outside the file')
            return offset
    raise AssertionError(f'RVA 0x{rva:x} is outside every PE section')


def word_at(data, rva):
    return struct.unpack_from('<I', data, pe_offset(data, rva))[0]


def sign_extend(value, bits):
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def check_word(data, rva, expected, label):
    actual = word_at(data, rva)
    check(actual == expected,
          f'{label} at 0x{rva:x}: 0x{actual:08x} != 0x{expected:08x}')


def check_bl(data, rva, target):
    word = word_at(data, rva)
    check(word >> 26 == 0x25, f'0x{rva:x}: not a BL instruction')
    actual = rva + (sign_extend(word & 0x03ffffff, 26) << 2)
    check(actual == target, f'0x{rva:x}: BL target 0x{actual:x} != 0x{target:x}')


def check_bcond(data, rva, condition, target):
    word = word_at(data, rva)
    check((word & 0xff000010) == 0x54000000,
          f'0x{rva:x}: not a B.cond instruction')
    actual_condition = word & 0xf
    actual_target = rva + (sign_extend((word >> 5) & 0x7ffff, 19) << 2)
    check(actual_condition == condition,
          f'0x{rva:x}: condition {actual_condition} != {condition}')
    check(actual_target == target,
          f'0x{rva:x}: branch target 0x{actual_target:x} != 0x{target:x}')


def check_cb(data, rva, nonzero, target):
    word = word_at(data, rva)
    check((word & 0x7e000000) == 0x34000000,
          f'0x{rva:x}: not a CBZ/CBNZ instruction')
    actual_nonzero = (word >> 24) & 1
    actual_target = rva + (sign_extend((word >> 5) & 0x7ffff, 19) << 2)
    check(actual_nonzero == nonzero,
          f'0x{rva:x}: CBZ/CBNZ op {actual_nonzero} != {nonzero}')
    check(actual_target == target,
          f'0x{rva:x}: branch target 0x{actual_target:x} != 0x{target:x}')


def check_tbz(data, rva, bit, target):
    word = word_at(data, rva)
    check((word & 0x7f000000) == 0x36000000,
          f'0x{rva:x}: not a TBZ instruction')
    actual_bit = (((word >> 31) & 1) << 5) | ((word >> 19) & 0x1f)
    actual_target = rva + (sign_extend((word >> 5) & 0x3fff, 14) << 2)
    check(actual_bit == bit, f'0x{rva:x}: bit {actual_bit} != {bit}')
    check(actual_target == target,
          f'0x{rva:x}: branch target 0x{actual_target:x} != 0x{target:x}')


def check_handshake_instructions(data):
    check_bl(data, 0x820c, 0x3f04)
    check_bl(data, 0x825c, 0x69ac)
    check_bl(data, 0x8268, 0x6458)
    check_bl(data, 0x8270, 0x6c20)
    check_word(data, 0x8274, 0x6b14027f, 'cmp w19, w20')
    check_bcond(data, 0x8278, 1, 0x8284)
    check_bl(data, 0x8280, 0x6d28)

    check_word(data, 0x6480, 0x7100067f, 'cmp w19, #1')
    check_word(data, 0x6484, 0x3940a548, 'ldrb w8, [x10, #0x29]')
    check_word(data, 0x6488, 0x3900bd53, 'strb w19, [x10, #0x2f]')
    check_bcond(data, 0x648c, 1, 0x64fc)
    check_tbz(data, 0x6490, 0, 0x64fc)
    check_word(data, 0x6494, 0x5284e209, 'mov w9, #10000')
    check_word(data, 0x64a0, 0xb9424116, 'ldr w22, [x8, #0x240]')
    check_cb(data, 0x64a4, 1, 0x64c0)
    check_cb(data, 0x64b4, 1, 0x6498)
    check_word(data, 0x64bc, 0x2a1f03f6, 'mov w22, wzr')
    check_word(data, 0x64c0, 0x5284e20a, 'mov w10, #10000')
    check_word(data, 0x64c4, 0xb9024916, 'str w22, [x8, #0x248]')
    check_word(data, 0x64d0, 0xb942412b, 'ldr w11, [x9, #0x240]')
    check_cb(data, 0x64d4, 0, 0x64ec)
    check_cb(data, 0x64e4, 1, 0x64c8)
    check_word(data, 0x64ec, 0x720006df, 'tst w22, #3')
    check_word(data, 0x64f0, 0x321e03ea, 'orr w10, wzr, #4')
    check_word(data, 0x64f4, 0x1a9f054c, 'csinc w12, w10, wzr, eq')
    check_word(data, 0x64f8, 0xb9024d2c, 'str w12, [x9, #0x24c]')
    check_word(data, 0x64fc, 0x5284e208, 'mov w8, #10000')
    check_cb(data, 0x6500, 0, 0x6518)
    check_word(data, 0x650c, 0x3940bd69, 'ldrb w9, [x11, #0x2f]')
    check_word(data, 0x6510, 0x0a13012d, 'and w13, w9, w19')
    check_cb(data, 0x6514, 1, 0x6500)
    check_word(data, 0x6528, 0xd65f03c0, 'ret')


def check_pad_instructions(data):
    check_word(data, 0x3f14, 0x528a0008, 'mov w8, #0x5000')
    check_word(data, 0x3f18, 0x72a00ae8, 'movk w8, #0x57, lsl #16')
    check_word(data, 0x3f28, 0x5293fc89, 'mov w9, #0x9fe4')
    check_word(data, 0x3f2c, 0xb9000109, 'str w9, [x8]')
    check_word(data, 0x3f3c, 0x914ff908, 'add x8, x8, #0x3fe, lsl #12')
    check_word(data, 0x3f40, 0x5283fc8a, 'mov w10, #0x1fe4')
    check_word(data, 0x3f48, 0xb900010a, 'str w10, [x8]')
    check_word(data, 0x6d38, 0x120d7908, 'and w8, w8, #0xfffbffff')


def read(sequence, index):
    return sequence[index] if index < len(sequence) else 0


def model_handshake(argument, power_control_before, statuses=(), clears=(),
                    resets=()):
    """Model the 0x6458 sequence, including its non-aborting poll exhaustion."""
    result = {
        'reset_written': argument,
        'status_reads': 0,
        'status_delays': 0,
        'captured_status': None,
        'clear_written': None,
        'clear_reads': 0,
        'clear_delays': 0,
        'ctl_written': None,
        'reset_reads': 0,
    }
    if argument == 1 and (power_control_before & 1):
        captured = 0
        for index in range(STATUS_LIMIT):
            result['status_reads'] += 1
            value = read(statuses, index)
            if value != 0:
                captured = value
                break
            result['status_delays'] += 1
        result['captured_status'] = captured
        result['clear_written'] = captured
        for index in range(CLEAR_LIMIT):
            result['clear_reads'] += 1
            if read(clears, index) == 0:
                break
            result['clear_delays'] += 1
        result['ctl_written'] = 4 if (captured & 0x3) == 0 else 1
    for index in range(RESET_LIMIT):
        result['reset_reads'] += 1
        if (read(resets, index) & argument) == 0:
            break
    return result


def check_handshake_model():
    result = model_handshake(1, 0b1, [], [0], [0])
    check(result['status_reads'] == STATUS_LIMIT,
          'no-status poll did not perform 10,001 reads')
    check(result['status_delays'] == STATUS_LIMIT,
          'no-status poll did not perform 10,001 delays')
    check(result['captured_status'] == 0,
          'no-status poll did not force the captured status to zero')
    check(result['clear_written'] == 0,
          'no-status poll did not write zero to CORE_PWRCTL_CLEAR')
    check(result['clear_reads'] == 1 and result['clear_delays'] == 0,
          'zero clear status did not exit the clear poll immediately')
    check(result['ctl_written'] == 4,
          'zero status did not select IO_SUCCESS')
    check(result['reset_reads'] == 1,
          'self-cleared reset did not exit the reset poll immediately')

    for status, expected, label in (
            (0x1, 1, 'BUS_OFF'),
            (0x2, 1, 'BUS_ON'),
            (0x3, 1, 'mixed bus request'),
            (0x4, 4, 'IO_LOW'),
            (0x8, 4, 'IO_HIGH'),
            (0xc, 4, 'both IO request bits'),
            (0x5, 1, 'bus and IO request'),
            (0xf, 1, 'all request bits')):
        result = model_handshake(1, 0b1, [status], [0], [0])
        check(result['captured_status'] == status,
              f'{label}: captured status changed')
        check(result['ctl_written'] == expected,
              f'{label}: CTL 0x{result["ctl_written"]:x} != 0x{expected:x}')

    result = model_handshake(1, 0b1, [0x1], [1] * CLEAR_LIMIT, [0])
    check(result['clear_reads'] == CLEAR_LIMIT and
          result['clear_delays'] == CLEAR_LIMIT,
          'unclear status did not exhaust the clear poll')
    check(result['ctl_written'] == 1,
          'unclear status did not keep the captured BUS_SUCCESS result')

    result = model_handshake(1, 0b1, [], [1] * CLEAR_LIMIT, [0])
    check(result['captured_status'] == 0 and result['ctl_written'] == 4,
          'unclear status with a zero capture did not still write IO_SUCCESS')

    result = model_handshake(1, 0b1, [0x1], [0], [1] * RESET_LIMIT)
    check(result['reset_reads'] == RESET_LIMIT,
          'stuck reset did not exhaust the 10,000-read poll')
    check(result['ctl_written'] == 1,
          'stuck reset changed the earlier CTL result')
    check(result['reset_written'] == 1, 'stuck reset did not still return')

    result = model_handshake(1, 0b1, [0x1], [0], [1, 1, 0])
    check(result['reset_reads'] == 3,
          'reset self-clear did not exit after the first zero read')

    result = model_handshake(0, 0b1, [0x1], [0], [1, 1, 0])
    check(result['status_reads'] == 0 and result['clear_reads'] == 0,
          'argument 0 entered the status/clear path')
    check(result['ctl_written'] is None,
          'argument 0 wrote CORE_PWRCTL_CTL')
    check(result['reset_reads'] == 1,
          'argument 0 did not exit the reset poll immediately')

    result = model_handshake(1, 0b0, [0x1], [0], [1, 1, 0])
    check(result['status_reads'] == 0 and result['clear_reads'] == 0,
          'cleared bus-power bit entered the status/clear path')
    check(result['ctl_written'] is None,
          'cleared bus-power bit wrote CORE_PWRCTL_CTL')
    check(result['reset_reads'] == 3,
          'cleared bus-power bit changed the reset poll')


def decode_pad(value):
    return {
        'data_drive': value & 0x7,
        'cmd_drive': (value >> 3) & 0x7,
        'clk_drive': (value >> 6) & 0x7,
        'data_pull': (value >> 9) & 0x3,
        'cmd_pull': (value >> 11) & 0x3,
        'clk_pull': (value >> 13) & 0x3,
        'rclk_pull': (value >> 15) & 0x3,
    }


def check_pad_decode():
    check(WEST_BASE + SDC1_CTL == 0x00575000,
          'west tile plus SDC1 group is not 0x00575000')
    check(SOUTH_BASE + SDC2_CTL == 0x00973000,
          'south tile plus SDC2 group is not 0x00973000')
    check(0x00575000 + 0x3fe000 == 0x00973000,
          'the SDC2 address delta is not 0x3fe000')

    check(decode_pad(0x9fe4) == {
        'data_drive': 4,
        'cmd_drive': 4,
        'clk_drive': 7,
        'data_pull': 3,
        'cmd_pull': 3,
        'clk_pull': 0,
        'rclk_pull': 1,
    }, '0x9fe4 does not decode to the documented SDC1 fields')
    check(decode_pad(0x1fe4)['rclk_pull'] == 0,
          '0x1fe4 unexpectedly sets the SDC2 RCLK pull field')
    check(0xfffbffff & (1 << 18) == 0 and
          (0xfffbffff | (1 << 18)) == 0xffffffff,
          'the vendor 0x20c mask does not clear only bit 18')


def main():
    data, source = load_firmware()
    actual = sha256(data)
    check(actual == EXPECTED_SHA256,
          f'{source}: SHA-256 {actual} != {EXPECTED_SHA256}')
    check_handshake_instructions(data)
    check_pad_instructions(data)
    check_handshake_model()
    check_pad_decode()
    print(f'sdccdxe decode: sha256={actual} source={source} PASS')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except AssertionError as error:
        sys.exit(f'test-gta4l-sdccdxe-decode: {error}')
