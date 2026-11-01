#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Exercise the bounded PixelExperience extraction recipe.

The real release archive is used only when it is already in the host cache; the
test never downloads it.  Synthetic archives cover the success path and the
truncation, malformed-header, misalignment, table-overflow, overlapping-entry,
gzip, marker, duplicate-member, symlink, traversal and expectation failures.
"""
import gzip
import hashlib
import json
import stat
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

EXTRACTOR = Path(__file__).resolve().with_name('extract-gta4l-pe-artifact.py')
REAL = Path('/tmp/opencode/pe-artifact/'
            'PixelExperience_Plus_gta4l-11.0-20261007-1420-UNOFFICIAL.zip')
FDT_MAGIC = 0xd00dfeed
DTBO_MAGIC = 0xd7b7ab1e


def need(condition, message):
    if not condition:
        raise AssertionError(message)


def run(arguments):
    return subprocess.run([sys.executable, str(EXTRACTOR), *map(str, arguments)],
                          text=True, capture_output=True)


def round_up(value, alignment):
    return ((value + alignment - 1) // alignment) * alignment


def make_fdt(*, total=80, off_struct=56, size_struct=16,
             off_strings=72, size_strings=8):
    header = struct.pack('>10I', FDT_MAGIC, total, off_struct, off_strings,
                         40, 17, 16, 0, size_strings, size_struct)
    return header + b'\0' * (total - 40)


def make_boot(kernel_gz, ramdisk, dtb, *, page=4096, magic=b'ANDROID!',
              kernel_size=None, ramdisk_size=None, dtb_size=None,
              header_version=2):
    kernel_size = len(kernel_gz) if kernel_size is None else kernel_size
    ramdisk_size = len(ramdisk) if ramdisk_size is None else ramdisk_size
    dtb_size = len(dtb) if dtb_size is None else dtb_size
    header = bytearray(page)
    header[:8] = magic
    struct.pack_into('<10I', header, 8, kernel_size, 0x8000, ramdisk_size,
                     0x20000000, 0, 0xf00000, 0x1e00000, page,
                     header_version, 0x1600015c)
    header[48:64] = b'TESTDEVICE'.ljust(16, b'\0')
    header[64:576] = b'console=test'.ljust(512, b'\0')
    struct.pack_into('<I', header, 1632, 0)
    struct.pack_into('<Q', header, 1636, 0)
    struct.pack_into('<I', header, 1644,
                     1660 if header_version == 2 else 1648)
    struct.pack_into('<I', header, 1648, dtb_size)
    struct.pack_into('<Q', header, 1652, 0x1f00000)

    def pad(data):
        return data + b'\0' * (round_up(len(data), page) - len(data))

    return bytes(header) + pad(kernel_gz) + pad(ramdisk) + dtb


def make_dtbo(entries, *, magic=DTBO_MAGIC, total=None):
    header_size, entry_size, entries_offset, page = 32, 32, 32, 4096
    table = bytearray()
    payload = bytearray()
    start = entries_offset + len(entries) * entry_size
    for data, identifier, revision in entries:
        table += struct.pack('>8I', len(data), start + len(payload),
                             identifier, revision, 0, 0, 0, 0)
        payload += data
    if total is None:
        total = start + len(payload)
    return (struct.pack('>8I', magic, total, header_size, entry_size,
                        len(entries), entries_offset, page, 0)
            + table + payload)


def image_with_config(config):
    return (b'synthetic kernel\nIKCFG_ST' + gzip.compress(config, mtime=0)
            + b'IKCFG_ED\ntrailer\n')


def base_members():
    config = b'CONFIG_MMC=y\nCONFIG_MMC_SDHCI_MSM=y\n'
    image = image_with_config(config)
    dtb = make_fdt() + make_fdt()
    kernel_gz = gzip.compress(image, mtime=0)
    ramdisk = b'ramdisk' * 64
    return {
        'boot.img': make_boot(kernel_gz, ramdisk, dtb),
        'dtbo.img': make_dtbo([(make_fdt(), 0, 0)]),
        'dynamic_partitions_op_list': b'# test\nadd_group g 1\n',
        'vendor.transfer.list': b'2\n0\n0\n',
        'META-INF/com/android/metadata': b'ota-type=BLOCK\npre-device=gta4l\n',
    }, image, config, kernel_gz, ramdisk, dtb


def write_zip(path, members, extra=()):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in members.items():
            archive.writestr(name, data)
        for info, data in extra:
            archive.writestr(info, data)


def expect_failure(name, archive, output, *arguments):
    result = run(['--archive', archive, '--output', output, *arguments])
    need(result.returncode != 0, f'{name}: unexpectedly passed')
    need(not output.exists(), f'{name}: left an output directory')


def main():
    with tempfile.TemporaryDirectory(prefix='gta4l-pe-extract-test-') as name:
        work = Path(name)
        members, image, config, kernel_gz, ramdisk, dtb = base_members()
        archive = work / 'release.zip'
        write_zip(archive, members)

        output = work / 'out'
        result = run(['--archive', archive, '--output', output])
        need(result.returncode == 0, f'success path failed: {result.stderr}')
        report = json.loads((output / 'extraction.json').read_text())
        need(report['boot']['header_version'] == 2, 'header version')
        need(report['boot']['sections_end'] <= 4096 + 2 * 4096 + len(dtb),
             'sections end')
        need(report['dtb']['count'] == 2, 'FDT count')
        need(report['dtbo']['count'] == 1, 'DTBO count')
        need(report['ikconfig']['size'] == len(config), 'config size')
        need((output / 'Image').read_bytes() == image, 'Image bytes')
        need((output / 'kernel.config').read_bytes() == config, 'config bytes')
        need((output / 'kernel.gz').read_bytes() == kernel_gz, 'kernel.gz bytes')
        need((output / 'ramdisk').read_bytes() == ramdisk, 'ramdisk bytes')
        need((output / 'dtb.img').read_bytes() == dtb, 'dtb.img bytes')
        need((output / 'dtbs/00.dtb').read_bytes() == dtb[:80], 'first DTB')
        need((output / 'dtbs/01.dtb').read_bytes() == dtb[80:], 'second DTB')
        need((output / 'dtbos/00-00000000-00000000.dtbo').read_bytes()
             == make_fdt(), 'first DTBO')
        need((output / 'metadata.txt').read_bytes()
             == members['META-INF/com/android/metadata'], 'metadata')
        print('PASS synthetic extraction and derived outputs')

        expectation = {'archive': {'size': archive.stat().st_size,
                                   'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()}}
        expectation.update({key: value for key, value in report['outputs'].items()})
        expectation_path = work / 'expect.json'
        expectation_path.write_text(json.dumps(expectation))
        expected_output = work / 'expected-out'
        result = run(['--archive', archive, '--output', expected_output,
                      '--expect', expectation_path])
        need(result.returncode == 0, f'expectation path failed: {result.stderr}')
        need(expected_output.is_dir(), 'expectation output missing')
        print('PASS expectation map accepts the extraction')

        existing = work / 'existing'
        existing.mkdir()
        marker = existing / 'keep'
        marker.write_bytes(b'preserve')
        result = run(['--archive', archive, '--output', existing])
        need(result.returncode != 0, 'existing output was accepted')
        need(marker.read_bytes() == b'preserve', 'existing output was modified')
        print('PASS existing output is refused without modification')

        wrong = dict(expectation)
        wrong['boot.img'] = {'size': report['outputs']['boot.img']['size'],
                             'sha256': '0' * 64}
        wrong_path = work / 'wrong.json'
        wrong_path.write_text(json.dumps(wrong))
        wrong_output = work / 'wrong-out'
        expect_failure('wrong expectation', archive, wrong_output,
                       '--expect', wrong_path)

        bad = work / 'bad.zip'
        broken = dict(members)
        broken['boot.img'] = make_boot(kernel_gz, ramdisk, dtb,
                                       magic=b'NOTANDR!')
        write_zip(bad, broken)
        expect_failure('bad boot magic', bad, work / 'bad-magic')

        broken = dict(members)
        broken['boot.img'] = make_boot(kernel_gz, ramdisk, dtb,
                                       kernel_size=len(kernel_gz) + 4096)
        write_zip(bad, broken)
        expect_failure('truncated boot sections', bad, work / 'bad-size')

        broken = dict(members)
        broken['boot.img'] = make_boot(kernel_gz, ramdisk, dtb, page=3000)
        write_zip(bad, broken)
        expect_failure('unaligned page size', bad, work / 'bad-page')

        broken = dict(members)
        boot = bytearray(members['boot.img'])
        struct.pack_into('<I', boot, 1632, 1)
        broken['boot.img'] = bytes(boot)
        write_zip(bad, broken)
        expect_failure('recovery DTBO offset', bad, work / 'bad-recovery')

        broken = dict(members)
        broken['boot.img'] = make_boot(kernel_gz, ramdisk,
                                       make_fdt(size_strings=100))
        write_zip(bad, broken)
        expect_failure('FDT strings block overflow', bad, work / 'bad-fdt')

        broken = dict(members)
        broken['boot.img'] = make_boot(kernel_gz, ramdisk,
                                       make_fdt() + b'extra')
        write_zip(bad, broken)
        expect_failure('FDT trailing bytes', bad, work / 'bad-fdt-trailing')

        broken = dict(members)
        broken['dtbo.img'] = make_dtbo([(make_fdt(), 0, 0)], magic=0x12345678)
        write_zip(bad, broken)
        expect_failure('bad DTBO magic', bad, work / 'bad-dtbo')

        broken = dict(members)
        broken['dtbo.img'] = make_dtbo([(make_fdt(), 0, 0)], total=1000000)
        write_zip(bad, broken)
        expect_failure('DTBO table overflow', bad, work / 'bad-dtbo-total')

        broken = dict(members)
        first = make_fdt()
        overlapping = make_dtbo([(first, 0, 0), (first, 0, 0)])
        entry = 32
        size, offset, identifier, revision = struct.unpack_from('>4I', overlapping, entry)
        blob = bytearray(overlapping)
        struct.pack_into('>4I', blob, entry + 32, size, offset, identifier, revision)
        broken['dtbo.img'] = bytes(blob)
        write_zip(bad, broken)
        expect_failure('overlapping DTBO entries', bad, work / 'bad-dtbo-overlap')

        broken = dict(members)
        broken['boot.img'] = make_boot(kernel_gz[:-8], ramdisk, dtb)
        write_zip(bad, broken)
        expect_failure('truncated kernel gzip', bad, work / 'bad-gzip')

        broken = dict(members)
        broken['boot.img'] = make_boot(gzip.compress(b'no markers', mtime=0),
                                       ramdisk, dtb)
        write_zip(bad, broken)
        expect_failure('missing IKCONFIG markers', bad, work / 'bad-ikconfig')

        broken = dict(members)
        broken['META-INF/com/android/metadata'] = b'pre-device=other\n'
        write_zip(bad, broken)
        expect_failure('wrong metadata device', bad, work / 'bad-metadata')

        duplicate = work / 'duplicate.zip'
        with zipfile.ZipFile(duplicate, 'w') as archive_file:
            for member, data in members.items():
                archive_file.writestr(member, data)
            archive_file.writestr('boot.img', members['boot.img'])
        expect_failure('duplicate member', duplicate, work / 'duplicate-out')

        traversal = work / 'traversal.zip'
        write_zip(traversal, members, [(zipfile.ZipInfo('../boot.img'), b'x')])
        expect_failure('traversal member', traversal, work / 'traversal-out')

        symlink = work / 'symlink.zip'
        link = zipfile.ZipInfo('boot.img')
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        write_zip(symlink, {key: value for key, value in members.items()
                            if key != 'boot.img'}, [(link, b'target')])
        expect_failure('symlink member', symlink, work / 'symlink-out')

        print('PASS malformed and adversarial archives are rejected')

    if REAL.is_file():
        with tempfile.TemporaryDirectory(prefix='gta4l-pe-real-') as name:
            output = Path(name) / 'release'
            result = run(['--archive', REAL, '--output', output, '--pinned'])
            need(result.returncode == 0, f'pinned release failed: {result.stderr}')
            report = json.loads((output / 'extraction.json').read_text())
            need(report['archive']['sha256']
                 == '3adbec9c4066b0efe571274346c710162d5692dd184e513d7cb5276379cc7605',
                 'pinned archive hash')
            need(report['dtb']['count'] == 12, 'pinned FDT count')
            need(report['dtbo']['count'] == 40, 'pinned DTBO count')
            need(report['boot']['sections_end'] == 19785618, 'pinned sections end')
            print('PASS pinned release extraction reproduces the recorded hashes')
    else:
        print('SKIP: pinned PE archive is not in the host cache; no download attempted')

    print('PASS gta4l PE extraction tests')
    return 0


if __name__ == '__main__':
    sys.exit(main())
