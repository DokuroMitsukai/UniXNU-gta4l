#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Self-test for scripts/check-gta4l-boot-image.py.

Builds small synthetic Android boot images and exercises the pass path plus the
failure paths that matter for the gta4l boot-image regression:

- exact baseline and payload-only replacement pass;
- a minimal DTB without Qualcomm IDs is rejected;
- a different but well-formed DTB is rejected;
- a changed header field outside the recomputed ID is rejected;
- a wrong boot-image ID, nonzero kernel padding, trailing data, a short
  ramdisk, and a wrong expected payload are rejected;
- a malformed image is rejected by the parser.
"""
import gzip
import hashlib
import importlib.util
import struct
import sys
import tempfile
from pathlib import Path

GATE_PATH = Path(__file__).resolve().with_name('check-gta4l-boot-image.py')
SPEC = importlib.util.spec_from_file_location('gta4l_boot_image_gate', GATE_PATH)
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)
PAGE = GATE.PAGE


def sha(data):
    return hashlib.sha256(data).hexdigest()


def make_dtb(include_ids=True, extra=b''):
    body = bytearray()
    if include_ids:
        body += b'qcom,msm-id\0qcom,board-id\0'
    body += extra
    size = 40 + len(body)
    return GATE.FDT_MAGIC + struct.pack('>I', size) + b'\0' * 32 + bytes(body)


def make_kernel(firmware, dtb):
    return gzip.compress(firmware, mtime=0) + dtb


def image_id(kernel, ramdisk):
    digest = hashlib.sha1()
    digest.update(kernel)
    digest.update(struct.pack('<I', len(kernel)))
    digest.update(ramdisk)
    digest.update(struct.pack('<I', len(ramdisk)))
    digest.update(struct.pack('<I', 0))
    digest.update(struct.pack('<I', 0))
    return digest.digest().ljust(GATE.HEADER_ID_SIZE, b'\0')


def make_image(kernel, ramdisk, cmdline=b'', header_id=None,
               padding_byte=0, trailing=b''):
    header = bytearray(PAGE)
    header[:8] = GATE.MAGIC
    struct.pack_into('<I', header, 8, len(kernel))
    struct.pack_into('<I', header, 12, 0x40008000)
    struct.pack_into('<I', header, 16, len(ramdisk))
    struct.pack_into('<I', header, 20, 0x41000000)
    struct.pack_into('<I', header, 24, 0)
    struct.pack_into('<I', header, 28, 0)
    struct.pack_into('<I', header, 32, 0x40000100)
    struct.pack_into('<I', header, 36, PAGE)
    struct.pack_into('<I', header, 40, 1)
    struct.pack_into('<I', header, 44, 0)
    header[48:64] = b'\0' * 16
    header[64:64 + len(cmdline)] = cmdline
    struct.pack_into('<I', header, 1632, 0)
    struct.pack_into('<Q', header, 1636, 0)
    struct.pack_into('<I', header, 1644, 1648)
    header[GATE.HEADER_ID_OFFSET:
           GATE.HEADER_ID_OFFSET + GATE.HEADER_ID_SIZE] = (
               image_id(kernel, ramdisk) if header_id is None else header_id)
    padding = bytes([padding_byte]) * (-len(kernel) % PAGE)
    return bytes(header) + kernel + padding + ramdisk + trailing


def write(path, data):
    path.write_bytes(data)
    return path


def check(base_path, candidate_path, expected=None):
    return GATE.check(GATE.parse(base_path), GATE.parse(candidate_path),
                      expected)


def main():
    tmp = Path(tempfile.mkdtemp(prefix='gta4l-bootimg-test-'))
    try:
        firmware = b'FAKE-FIRMWARE' * 200
        dtb = make_dtb()
        kernel = make_kernel(firmware, dtb)
        ramdisk = b'R' * PAGE
        base = write(tmp / 'base.img', make_image(kernel, ramdisk))

        result = check(base, base, sha(ramdisk))
        assert result['pass'], result.get('failed_check')
        print('PASS exact baseline and payload')

        new_ramdisk = b'S' * PAGE
        candidate = write(tmp / 'payload.img',
                          make_image(kernel, new_ramdisk))
        result = check(base, candidate, sha(new_ramdisk))
        assert result['pass'], result.get('failed_check')
        print('PASS payload-only replacement with recomputed ID')

        result = check(base, candidate, sha(ramdisk))
        assert not result['pass']
        assert result['failed_check'] == 'ramdisk_matches_expected_payload'
        print('PASS wrong expected payload rejected')

        minimal_dtb = make_dtb(
            include_ids=False, extra=b'X' * (len(dtb) - 40))
        assert len(minimal_dtb) == len(dtb)
        minimal = make_kernel(firmware, minimal_dtb)
        candidate = write(tmp / 'minimal-dtb.img',
                          make_image(minimal, ramdisk))
        result = check(base, candidate)
        assert not result['pass']
        assert result['failed_check'] == 'candidate_dtb_has_qcom_ids'
        print('PASS same-size minimal DTB without Qualcomm IDs rejected')

        other_dtb = bytearray(make_dtb())
        other_dtb[10] ^= 1
        other = make_kernel(firmware, bytes(other_dtb))
        candidate = write(tmp / 'other-dtb.img',
                          make_image(other, ramdisk))
        result = check(base, candidate)
        assert not result['pass']
        assert result['failed_check'] == 'boot_dtb_matches_baseline'
        print('PASS different well-formed DTB rejected')

        candidate = write(tmp / 'cmdline.img',
                          make_image(kernel, ramdisk, cmdline=b'changed'))
        result = check(base, candidate)
        assert not result['pass']
        assert result['failed_check'] == 'header_page_excluding_id_matches_baseline'
        print('PASS changed header field outside ID rejected')

        candidate = write(tmp / 'bad-id.img',
                          make_image(kernel, ramdisk,
                                     header_id=b'\0' * GATE.HEADER_ID_SIZE))
        result = check(base, candidate)
        assert not result['pass']
        assert result['failed_check'] == 'candidate_header_id_matches_computed'
        print('PASS wrong boot-image ID rejected')

        candidate = write(tmp / 'padding.img',
                          make_image(kernel, ramdisk, padding_byte=1))
        result = check(base, candidate)
        assert not result['pass']
        assert result['failed_check'] == 'kernel_padding_is_zero'
        print('PASS nonzero kernel padding rejected')

        candidate = write(tmp / 'trailing.img',
                          make_image(kernel, ramdisk, trailing=b'X'))
        result = check(base, candidate)
        assert not result['pass']
        assert result['failed_check'] == 'no_unexpected_trailing_data'
        print('PASS unexpected trailing data rejected')

        candidate = write(tmp / 'short-ramdisk.img',
                          make_image(kernel, b'R' * 16))
        result = check(base, candidate)
        assert not result['pass']
        assert result['failed_check'] in (
            'header_page_excluding_id_matches_baseline',
            'header_fields_match_baseline',
            'ramdisk_size_matches_baseline')
        print('PASS short ramdisk rejected')

        bad_base = write(tmp / 'bad-base.img',
                         make_image(kernel, ramdisk,
                                    header_id=b'\0' * GATE.HEADER_ID_SIZE))
        result = check(bad_base, base)
        assert not result['pass']
        assert result['failed_check'] == 'baseline_header_id_matches_computed'
        print('PASS baseline with wrong ID rejected')

        malformed = write(tmp / 'malformed.img', b'not an Android image')
        try:
            GATE.parse(malformed)
        except SystemExit:
            print('PASS malformed image rejected by parser')
        else:
            raise AssertionError('malformed image was accepted')
    finally:
        for path in tmp.iterdir():
            path.unlink()
        tmp.rmdir()
    print('PASS gta4l boot-image gate self-test')
    return 0


if __name__ == '__main__':
    sys.exit(main())
