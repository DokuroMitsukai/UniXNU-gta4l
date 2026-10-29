#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Extract and enumerate firmware modules from a gta4l boot image or raw FV.

Host-only and read-only. It does not touch a device, a build, or a power state.

The gta4l boot image wraps the firmware as:

    Android boot image
      kernel section (gzip)
        BootShim (192 bytes)
        outer FV
          PEI core FFS
          FV image FFS
            GUIDed LZMA section
              section stream
                inner FV
                  driver FFS files (PmicDxe, SPMIDxe, SdccDxe, ...)

The script prints image, firmware, volume, module GUID, UI name and PE hash so
that packaged bytes can be compared with the binaries cited in the documents.
Pass `--fv` to read a raw or gzip-wrapped firmware volume (for example a
Mu-Silicium `SILICIUM_UEFI.fd`) instead of a boot image.
"""
import argparse
import gzip
import hashlib
import lzma
import struct
import sys
import zlib
from pathlib import Path

PAGE = 4096
MAGIC = b'ANDROID!'
FDT_MAGIC = b'\xd0\x0d\xfe\xed'
FV_SIGNATURE = 0x4856465F
LZMA_GUID = 'ee4e5898-3914-4259-9d6e-dc7bd79403cf'
BOOTSHIM_BYTES = 192

FFS_TYPES = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
             0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xF0}
FFS_STATES = {0x07, 0x08, 0x09, 0x0A, 0xF8, 0xF9, 0xFA, 0xFB}
EXECUTABLE_FFS_TYPES = {0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
                        0x0C, 0x0D, 0x0E, 0x0F}
PE_TYPES = {0x10, 0x11, 0x12}
UI_TYPE = 0x15
FV_IMAGE_TYPE = 0x17
GUIDED_TYPE = 0x02


class FirmwareError(Exception):
    """A malformed or unsupported firmware structure."""



def sha(data):
    return hashlib.sha256(data).hexdigest()


def guid(raw):
    d1, d2, d3 = struct.unpack_from('<IHH', raw, 0)
    return f'{d1:08x}-{d2:04x}-{d3:04x}-{raw[8:10].hex()}-{raw[10:16].hex()}'


def boot_firmware(path):
    data = Path(path).read_bytes()
    if len(data) < PAGE or data[:8] != MAGIC:
        raise FirmwareError(f'{path}: not an Android boot image')
    header = dict(zip(
        ('kernel_size', 'kernel_address', 'ramdisk_size', 'ramdisk_address',
         'second_size', 'second_address', 'tags_address', 'page_size',
         'header_version', 'os_version'),
        struct.unpack_from('<10I', data, 8)))
    if header['page_size'] != PAGE or header['header_version'] != 1:
        raise FirmwareError(f'{path}: unsupported boot header')
    kernel_end = PAGE + header['kernel_size']
    if kernel_end > len(data):
        raise FirmwareError(
            f'{path}: kernel section overruns the image '
            f'({header["kernel_size"]:#x} bytes at {PAGE:#x}, '
            f'image is {len(data):#x} bytes)')
    kernel = data[PAGE:kernel_end]
    if kernel[:2] != b'\x1f\x8b':
        raise FirmwareError(f'{path}: kernel section is not gzip')
    decompressor = zlib.decompressobj(31)
    try:
        firmware = decompressor.decompress(kernel)
    except zlib.error as error:
        raise FirmwareError(f'{path}: kernel gzip stream is corrupt: {error}')
    if not decompressor.eof:
        raise FirmwareError(f'{path}: kernel gzip stream is truncated')
    dtb = decompressor.unused_data
    return data, header, firmware, dtb


def split_shim(firmware):
    if len(firmware) <= BOOTSHIM_BYTES:
        raise FirmwareError('firmware region too small for BootShim + FV')
    return firmware[:BOOTSHIM_BYTES], firmware[BOOTSHIM_BYTES:]


def dtb_ok(dtb):
    """Check the flat-device-tree header and its declared block bounds."""
    if len(dtb) < 40 or dtb[:4] != FDT_MAGIC:
        return False
    (totalsize, off_struct, off_strings, off_rsvmap, version, last_comp,
     _, size_strings, size_struct) = struct.unpack_from('>9I', dtb, 4)
    if totalsize < 40 or totalsize > len(dtb):
        return False
    if version < 16 or last_comp > version:
        return False
    for offset in (off_struct, off_strings, off_rsvmap):
        if offset < 40 or offset > totalsize:
            return False
    if off_struct + size_struct > totalsize:
        return False
    if off_strings + size_strings > totalsize:
        return False
    return True



def is_fv(data):
    return len(data) >= 0x38 and struct.unpack_from('<I', data, 0x28)[0] == FV_SIGNATURE


def fv_header_length(fv):
    return struct.unpack_from('<H', fv, 0x30)[0]


def fv_length(fv):
    if not is_fv(fv):
        raise FirmwareError('not a firmware volume (bad signature or too short)')
    fvlen = struct.unpack_from('<Q', fv, 0x20)[0]
    header_len = fv_header_length(fv)
    if fvlen < 0x38 or fvlen > len(fv):
        raise FirmwareError(f'FV length {fvlen:#x} is invalid for {len(fv):#x} bytes')
    if header_len < 0x38 or header_len > fvlen:
        raise FirmwareError(f'FV header length {header_len:#x} is invalid')
    ext_offset = struct.unpack_from('<H', fv, 0x34)[0]
    if ext_offset:
        if ext_offset + 20 > fvlen:
            raise FirmwareError(
                f'FV extended header offset {ext_offset:#x} is invalid')
        ext_size = struct.unpack_from('<I', fv, ext_offset + 16)[0]
        if ext_size < 20 or ext_offset + ext_size > fvlen:
            raise FirmwareError(
                f'FV extended header size {ext_size:#x} is invalid')
    header = bytearray(fv[:header_len])
    if header_len & 1:
        header.append(0)
    if sum(struct.unpack(f'<{len(header) // 2}H', header)) & 0xFFFF:
        raise FirmwareError('FV header checksum is invalid')
    return fvlen


def ffs_start(fv):
    """Return the first 8-byte-aligned FFS offset, honouring the FV ext header."""
    off = fv_header_length(fv)
    fvlen = struct.unpack_from('<Q', fv, 0x20)[0] if len(fv) >= 0x28 else len(fv)
    ext_offset = struct.unpack_from('<H', fv, 0x34)[0] if len(fv) >= 0x36 else 0
    if ext_offset:
        if ext_offset + 20 > fvlen:
            raise FirmwareError(
                f'FV extended header offset {ext_offset:#x} is invalid')
        ext_size = struct.unpack_from('<I', fv, ext_offset + 16)[0]
        if ext_size < 20 or ext_offset + ext_size > fvlen:
            raise FirmwareError(
                f'FV extended header size {ext_size:#x} is invalid')
        off = max(off, ext_offset + ext_size)
    return (off + 7) & ~7


def ffs_header(fv, off, end):
    """Return the FFS file size if a structurally valid header starts at off."""
    if off + 0x18 > end:
        return None
    typ = fv[off + 0x12]
    state = fv[off + 0x17]
    size = fv[off + 0x14] | (fv[off + 0x15] << 8) | (fv[off + 0x16] << 16)
    if typ not in FFS_TYPES or state not in FFS_STATES or size < 0x18:
        return None
    if off + size > end:
        return None
    header = bytearray(fv[off:off + 0x18])
    header[0x11] = 0
    header[0x17] = 0
    if sum(header) & 0xFF:
        return None
    return size


def walk_ffs(fv, strict=False):
    """Yield (offset, file_bytes) for structurally valid FFS files.

    The header checksum is computed with IntegrityCheck.File and State zeroed,
    so a naive sum of the final 24 header bytes is not zero.

    In strict mode an invalid, non-erased header raises `FirmwareError` instead
    of being skipped. Erased (`0xff`) padding and 8-byte alignment gaps are
    still tolerated, so a valid volume walks the same files in both modes.
    """
    if not is_fv(fv):
        if strict:
            raise FirmwareError('not a firmware volume (bad signature or too short)')
        return
    fvlen = fv_length(fv) if strict else struct.unpack_from('<Q', fv, 0x20)[0]
    end = min(fvlen, len(fv))
    header_end = fv_header_length(fv)
    off = ffs_start(fv)
    if strict:
        ext_offset = struct.unpack_from('<H', fv, 0x34)[0]
        probe = (header_end + 7) & ~7
        while probe + 0x18 <= off:
            inside_ext_header = ext_offset <= probe < ext_offset + 20
            if not inside_ext_header and \
                    fv[probe + 0x12] in EXECUTABLE_FFS_TYPES and \
                    ffs_header(fv, probe, end) is not None:
                raise FirmwareError(
                    f'executable FFS at {probe:#x} is hidden before the '
                    'FFS stream')
            probe += 8
    while off + 0x18 <= end:
        if fv[off:off + 8] == b'\xff' * 8:
            if all(byte == 0xFF for byte in fv[off:end]):
                return
            off += 8
            continue
        typ = fv[off + 0x12]
        state = fv[off + 0x17]
        size = fv[off + 0x14] | (fv[off + 0x15] << 8) | (fv[off + 0x16] << 16)
        if ffs_header(fv, off, end) is None:
            if strict:
                if typ not in FFS_TYPES or state not in FFS_STATES or size < 0x18:
                    raise FirmwareError(
                        f'FFS header at {off:#x} is invalid '
                        f'(type={typ:#x} state={state:#x} size={size:#x})')
                if off + size > end:
                    raise FirmwareError(
                        f'FFS file at {off:#x} overruns the volume '
                        f'(size={size:#x} end={end:#x})')
                raise FirmwareError(
                    f'FFS header at {off:#x} has a bad checksum')
            off += 8
            continue
        yield off, fv[off:off + size]
        off += (size + 7) & ~7
    if strict and any(byte != 0xFF for byte in fv[off:end]):
        raise FirmwareError(f'non-erased trailing bytes at {off:#x}')


def sections(file_bytes, start=0x18, strict=False):
    off = start
    while off + 4 <= len(file_bytes):
        size = (file_bytes[off] | (file_bytes[off + 1] << 8)
                | (file_bytes[off + 2] << 16))
        typ = file_bytes[off + 3]
        if size < 4 or off + size > len(file_bytes):
            if strict and any(file_bytes[off:]):
                raise FirmwareError(
                    f'section at {off:#x} is invalid (size={size:#x} '
                    f'remaining={len(file_bytes) - off:#x})')
            return
        yield typ, file_bytes[off:off + size]
        off += (size + 3) & ~3


def guided_lzma(section):
    if len(section) < 24:
        raise FirmwareError(
            f'GUIDed section is too short ({len(section)} bytes)')
    if guid(section[4:20]) != LZMA_GUID:
        return None
    data_offset = struct.unpack_from('<H', section, 20)[0]
    if data_offset < 24 or data_offset > len(section):
        raise FirmwareError(
            f'GUIDed LZMA data offset {data_offset:#x} is outside the section')
    try:
        return lzma.decompress(section[data_offset:], format=lzma.FORMAT_ALONE)
    except (lzma.LZMAError, EOFError) as error:
        raise FirmwareError(f'LZMA-guided section is corrupt: {error}')


def inner_fv(outer):
    """Return the first LZMA-guided inner FV after validating the whole stream.

    The outer FFS and every outer section stream are walked to completion, so a
    malformed trailing entry cannot hide behind an earlier valid inner FV.
    """
    candidate = None
    stream = None
    for _, file_bytes in walk_ffs(outer, strict=True):
        for typ, section in sections(file_bytes, strict=True):
            if typ != GUIDED_TYPE:
                continue
            raw = guided_lzma(section)
            if raw is None:
                continue
            for inner_type, inner_section in sections(raw, start=0, strict=True):
                if inner_type != FV_IMAGE_TYPE:
                    continue
                fv = inner_section[4:]
                if not is_fv(fv):
                    raise FirmwareError(
                        'FV image section does not contain a firmware volume')
                fv_length(fv)
                if candidate is None:
                    candidate, stream = fv, raw
    return candidate, stream


def pe_ok(pe):
    if len(pe) < 0x40 or pe[:2] != b'MZ':
        return False
    e_lfanew = struct.unpack_from('<I', pe, 0x3c)[0]
    return e_lfanew <= len(pe) - 4 and pe[e_lfanew:e_lfanew + 4] == b'PE\0\0'


def describe_module(offset, file_bytes, strict=False):
    module = {'offset': offset, 'guid': guid(file_bytes[:16]),
              'type': file_bytes[0x12], 'size': len(file_bytes),
              'ui': None, 'pe_bytes': None, 'pe_sha256': None, 'pe_ok': False}
    for typ, section in sections(file_bytes, strict=strict):
        if typ == UI_TYPE:
            module['ui'] = section[4:].decode('utf-16-le', 'replace').rstrip('\x00')
        elif typ in PE_TYPES:
            module['pe_bytes'] = len(section) - 4
            module['pe_sha256'] = sha(section[4:])
            module['pe_ok'] = pe_ok(section[4:])
    if strict and module['type'] in EXECUTABLE_FFS_TYPES and not module['pe_sha256']:
        raise FirmwareError(
            f"executable FFS {module['guid']} has no PE image")
    return module


def find_fv(data):
    for start in range(0, min(len(data) - 0x2c, 4096)):
        if is_fv(data[start:]):
            return start
    return None


def load_outer(path):
    raw = Path(path).read_bytes()
    if raw[:2] == b'\x1f\x8b':
        try:
            raw = gzip.decompress(raw)
        except (OSError, EOFError, zlib.error) as error:
            raise FirmwareError(f'{path}: gzip volume is corrupt: {error}')
    start = find_fv(raw)
    if start is None:
        raise FirmwareError(f'{path}: no firmware volume found')
    return raw[:start], raw[start:]


def collect_modules(inner):
    modules = []
    for offset, file_bytes in walk_ffs(inner, strict=True):
        module = describe_module(offset, file_bytes, strict=True)
        if module['pe_sha256']:
            modules.append(module)
    return modules


def validate_result(result):
    """Reject an extraction that is structurally incomplete."""
    inner = result['inner_fv']
    if not inner.get('sha256'):
        raise FirmwareError('inner firmware volume is missing')
    modules = result['modules']
    if not modules:
        raise FirmwareError('no PE modules found in the inner firmware volume')
    for module in modules:
        if not module['pe_bytes'] or not module['pe_sha256']:
            raise FirmwareError(f"module {module['guid']} has no PE image")
        if not module['pe_ok']:
            raise FirmwareError(f"module {module['guid']} has an invalid PE image")
    if 'dtb' in result:
        if not result['dtb'].get('valid'):
            raise FirmwareError('appended DTB is invalid')
    return len(modules)



def emit(result, as_json):
    if as_json:
        import json
        print(json.dumps(result, indent=2))
        return
    for key, label in (('image', 'image'), ('firmware', 'firmware'),
                       ('bootshim', 'bootshim'), ('outer_fv', 'outer FV'),
                       ('guided_stream', 'guided'), ('inner_fv', 'inner FV'),
                       ('dtb', 'dtb')):
        if key in result:
            print(f"{label:<12} {result[key]['bytes']:>10}  {result[key]['sha256']}")
    print()
    print(f"{'offset':>8}  {'size':>7}  {'UI':<24} {'PE sha256':<64} guid")
    for module in result['modules']:
        print(f"{module['offset']:>8x}  {module['size']:>7x}  "
              f"{str(module['ui']):<24} {module['pe_sha256']} {module['guid']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', help='gta4l boot image or firmware volume')
    parser.add_argument('--fv', action='store_true',
                        help='treat the input as a raw or gzip firmware volume')
    parser.add_argument('--json', action='store_true', help='emit JSON')
    parser.add_argument('--validate', action='store_true',
                        help='fail unless the inner FV and every module are complete')
    args = parser.parse_args()

    try:
        result = extract(args.image, args.fv)
        count = validate_result(result) if args.validate else None
        emit(result, args.json)
        if count is not None:
            print(f'valid: {count} PE modules',
                  file=sys.stderr if args.json else sys.stdout)
    except FirmwareError as error:
        parser.exit(1, f'extract: {error}\n')


def extract(path, as_fv):
    if as_fv:
        prefix, outer = load_outer(path)
        fv_length(outer)
        inner, stream = inner_fv(outer)
        if inner is None:
            raise FirmwareError('no LZMA-guided inner firmware volume found')
        return {
            'source': {'path': path, 'bytes': len(prefix) + len(outer),
                       'sha256': sha(prefix + outer)},
            'prefix': {'bytes': len(prefix), 'sha256': sha(prefix)},
            'outer_fv': {'bytes': len(outer), 'sha256': sha(outer)},
            'guided_stream': {'bytes': len(stream), 'sha256': sha(stream)},
            'inner_fv': {'bytes': len(inner), 'sha256': sha(inner)},
            'modules': collect_modules(inner),
        }

    data, header, firmware, dtb = boot_firmware(path)
    shim, outer = split_shim(firmware)
    inner, stream = inner_fv(outer)
    if inner is None:
        raise FirmwareError('no LZMA-guided inner firmware volume found')

    return {
        'image': {'path': path, 'bytes': len(data), 'sha256': sha(data),
                  'kernel_bytes': header['kernel_size'],
                  'ramdisk_bytes': header['ramdisk_size']},
        'dtb': {'bytes': len(dtb), 'sha256': sha(dtb),
                'magic_ok': dtb[:4] == FDT_MAGIC, 'valid': dtb_ok(dtb)},
        'firmware': {'bytes': len(firmware), 'sha256': sha(firmware)},
        'bootshim': {'bytes': len(shim), 'sha256': sha(shim)},
        'outer_fv': {'bytes': len(outer), 'sha256': sha(outer)},
        'guided_stream': {'bytes': len(stream), 'sha256': sha(stream)},
        'inner_fv': {'bytes': len(inner), 'sha256': sha(inner)},
        'modules': collect_modules(inner),
    }


if __name__ == '__main__':
    main()
