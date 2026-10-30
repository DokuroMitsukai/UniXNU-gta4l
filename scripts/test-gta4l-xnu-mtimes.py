#!/usr/bin/env python3
"""Host-only tests for pin-gta4l-xnu-mtimes.py.

The tests build a synthetic Mach-O, synthetic BSD ar archives and a temporary
object tree. They do not need the reference kernel, a firmware image or a
device.
"""
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/pin-gta4l-xnu-mtimes.py'

MACHO_MAGIC = 0xfeedfacf
N_OSO = 0x66
PLAIN = 'osfmk/DEVELOPMENT/plain.o'
CPO = 'bsd/DEVELOPMENT/plain.cpo'
LONG = 'long_archive_member.o'
SHORT = 'short.o'
LONG_PAYLOAD = b'long member payload'
SHORT_PAYLOAD = b'short member payload'
PLAIN_MTIME = 1700000001
CPO_MTIME = 1700000002
LONG_MTIME = 1700000003
SHORT_MTIME = 1700000004
OLD_MTIME = 1600000000


def run(*arguments):
    return subprocess.run(
        [sys.executable, '-B', str(SCRIPT), *map(str, arguments)],
        capture_output=True, text=True)


def make_macho(path, entries):
    strings = bytearray(b'\0')
    offsets = {}
    for name, _mtime in entries:
        offsets[name] = len(strings)
        strings += name.encode() + b'\0'
    nsyms = len(entries) + 1
    symoff = 32 + 24
    stroff = symoff + nsyms * 16
    data = bytearray(struct.pack(
        '<IiiIIIII', MACHO_MAGIC, 0x0100000c, 0, 2, 1, 24, 0, 0))
    data += struct.pack('<IIIIII', 2, 24, symoff, nsyms, stroff, len(strings))
    data += struct.pack('<IBBHQ', 0, 0x0f, 0, 0, 0)
    for name, mtime in entries:
        data += struct.pack('<IBBHQ', offsets[name], N_OSO, 0, 0, mtime)
    data += strings
    path.write_bytes(bytes(data))


def ar_member(name, date, payload, long_name):
    if long_name:
        raw_name = name.encode()
        raw_name = raw_name.ljust((len(raw_name) + 3) // 4 * 4, b'\0')
        header_name = f'#1/{len(raw_name)}'.encode()
        body = raw_name + payload
    else:
        header_name = (name + '/').encode()
        body = payload
    header = (header_name.ljust(16, b' ') +
              str(date).encode().ljust(12, b' ') +
              b'0'.ljust(6, b' ') +
              b'0'.ljust(6, b' ') +
              b'100644'.ljust(8, b' ') +
              str(len(body)).encode().ljust(10, b' ') +
              b'`\n')
    assert len(header) == 60
    return header + body + (b'\n' if len(body) % 2 else b'')


def make_archive(path, members):
    data = bytearray(b'!<arch>\n')
    for name, date, payload, long_name in members:
        data += ar_member(name, date, payload, long_name)
    path.write_bytes(bytes(data))


def read_archive(path):
    data = path.read_bytes()
    assert data[:8] == b'!<arch>\n'
    offset = 8
    members = {}
    while offset < len(data):
        header = data[offset:offset + 60]
        name = header[:16].decode().strip()
        date = int(header[16:28].decode().strip())
        size = int(header[48:58].decode().strip())
        body = data[offset + 60:offset + 60 + size]
        if name.startswith('#1/'):
            length = int(name[3:])
            real_name = body[:length].split(b'\0', 1)[0].decode()
            payload = body[length:]
        else:
            real_name = name.rstrip('/')
            payload = body
        members[real_name] = (date, payload)
        offset += 60 + size + (size % 2)
    return members


def tree_paths(base):
    object_root = (base / 'tree/build/xnu/ios-obj'
                   / 'DEVELOPMENT_ARM64_BCM2837')
    archive_root = base / 'tree/build'
    return object_root, archive_root


def default_entries(base, recorded_base=None):
    recorded_base = recorded_base or base
    object_root, archive_root = tree_paths(recorded_base)
    archive = archive_root / 'fixture/libFixture.a'
    return [
        (str(object_root / PLAIN), PLAIN_MTIME),
        (str(object_root / CPO), CPO_MTIME),
        (f'{archive}({LONG})', LONG_MTIME),
        (f'{archive}({SHORT})', SHORT_MTIME),
    ]


def make_tree(base, entries, archive_members=None):
    object_root, archive_root = tree_paths(base)
    for relative in (PLAIN, CPO):
        path = object_root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'payload ' + relative.encode())
    archive = archive_root / 'fixture/libFixture.a'
    archive.parent.mkdir(parents=True, exist_ok=True)
    if archive_members is None:
        archive_members = [
            (LONG, 1600000011, LONG_PAYLOAD, True),
            (SHORT, 1600000012, SHORT_PAYLOAD, False),
        ]
    make_archive(archive, archive_members)
    reference = base / 'reference.macho'
    make_macho(reference, entries)
    return reference, object_root, archive_root, archive


def arguments(reference, object_root, archive_root, mode):
    return ('--reference-kernel', reference, '--object-root', object_root,
            '--archive-root', archive_root, mode)


def test_verify_then_apply(base):
    reference, object_root, archive_root, archive = make_tree(
        base, default_entries(base))
    for relative in (PLAIN, CPO):
        os.utime(object_root / relative, (OLD_MTIME, OLD_MTIME))
    before = archive.read_bytes()

    result = run(*arguments(reference, object_root, archive_root, '--verify'),
                 '--json')
    assert result.returncode == 1, result
    report = json.loads(result.stdout)
    assert report['mode'] == 'verify'
    assert report['changed'] == 4
    assert report['unchanged'] == 0
    assert report['errors'] == []
    assert report['objects']['total'] == 2
    assert report['archive_members']['total'] == 2
    assert report['reference_sha256'] == hashlib.sha256(
        reference.read_bytes()).hexdigest()
    assert archive.read_bytes() == before

    result = run(*arguments(reference, object_root, archive_root, '--apply'),
                 '--json')
    assert result.returncode == 0, result
    report = json.loads(result.stdout)
    assert report['mode'] == 'apply'
    assert report['changed'] == 4
    assert report['objects']['changed'] == 2
    assert report['archive_members']['changed'] == 2
    for relative, mtime in ((PLAIN, PLAIN_MTIME), (CPO, CPO_MTIME)):
        assert int((object_root / relative).stat().st_mtime) == mtime
    members = read_archive(archive)
    assert members[LONG] == (LONG_MTIME, LONG_PAYLOAD)
    assert members[SHORT] == (SHORT_MTIME, SHORT_PAYLOAD)
    after_first_apply = archive.read_bytes()

    result = run(*arguments(reference, object_root, archive_root, '--apply'),
                 '--json')
    assert result.returncode == 0, result
    report = json.loads(result.stdout)
    assert report['changed'] == 0
    assert report['unchanged'] == 4
    assert archive.read_bytes() == after_first_apply

    result = run(*arguments(reference, object_root, archive_root, '--verify'),
                 '--json')
    assert result.returncode == 0, result
    report = json.loads(result.stdout)
    assert report['changed'] == 0
    assert report['unchanged'] == 4
    print('PASS verify mismatch, apply, idempotent reapply and verify')


def test_relocated_roots(base):
    recorded_base = base / 'old-build'
    reference, object_root, archive_root, archive = make_tree(
        base, default_entries(base, recorded_base))
    for relative in (PLAIN, CPO):
        os.utime(object_root / relative, (OLD_MTIME, OLD_MTIME))
    result = run(*arguments(reference, object_root, archive_root, '--apply'))
    assert result.returncode == 0, result
    for relative, mtime in ((PLAIN, PLAIN_MTIME), (CPO, CPO_MTIME)):
        assert int((object_root / relative).stat().st_mtime) == mtime
    members = read_archive(archive)
    assert members[LONG][0] == LONG_MTIME
    assert members[SHORT][0] == SHORT_MTIME
    print('PASS paths recorded under a different build root are remapped')


def test_missing_object(base):
    reference, object_root, archive_root, archive = make_tree(
        base, default_entries(base))
    os.utime(object_root / PLAIN, (OLD_MTIME, OLD_MTIME))
    (object_root / CPO).unlink()
    before = archive.read_bytes()
    result = run(*arguments(reference, object_root, archive_root, '--apply'))
    assert result.returncode == 2, result
    assert 'object not found' in result.stderr, result.stderr
    assert int((object_root / PLAIN).stat().st_mtime) == OLD_MTIME
    assert archive.read_bytes() == before
    print('PASS missing object fails closed without modifying anything')


def test_missing_member(base):
    entries = default_entries(base)
    archive_name = entries[2][0].split('(', 1)[0]
    entries.append((f'{archive_name}(missing.o)', 1700000005))
    reference, object_root, archive_root, archive = make_tree(base, entries)
    os.utime(object_root / PLAIN, (OLD_MTIME, OLD_MTIME))
    before = archive.read_bytes()
    result = run(*arguments(reference, object_root, archive_root, '--apply'))
    assert result.returncode == 2, result
    assert 'not found' in result.stderr, result.stderr
    assert int((object_root / PLAIN).stat().st_mtime) == OLD_MTIME
    assert archive.read_bytes() == before
    print('PASS missing archive member fails closed without changes')


def test_outside_roots(base):
    entries = default_entries(base)
    entries.append((str(base / 'outside.o'), 1700000006))
    reference, object_root, archive_root, archive = make_tree(base, entries)
    before = archive.read_bytes()
    result = run(*arguments(reference, object_root, archive_root, '--verify'))
    assert result.returncode == 2, result
    assert 'cannot map' in result.stderr, result.stderr
    assert archive.read_bytes() == before
    print('PASS object path outside the object root is rejected')


def test_escaping_archive_path(base):
    entries = default_entries(base)
    _, archive_root = tree_paths(base)
    escaping = archive_root / '..' / 'outside' / 'libOutside.a'
    entries.append((f'{escaping}(member.o)', 1700000008))
    reference, object_root, archive_root, archive = make_tree(base, entries)
    before = archive.read_bytes()
    result = run(*arguments(reference, object_root, archive_root, '--verify'))
    assert result.returncode == 2, result
    assert 'escapes' in result.stderr, result.stderr
    assert archive.read_bytes() == before
    print('PASS archive path escaping the archive root is rejected')


def test_outside_archive_is_ignored(base):
    entries = default_entries(base)
    prebuilt = base / 'prebuilt/libPrebuilt.a'
    entries.append((f'{prebuilt}(prebuilt.o)', 1700000007))
    reference, object_root, archive_root, archive = make_tree(base, entries)
    for relative, mtime in ((PLAIN, PLAIN_MTIME), (CPO, CPO_MTIME)):
        os.utime(object_root / relative, (mtime, mtime))
    result = run(*arguments(reference, object_root, archive_root, '--verify'),
                 '--json')
    assert result.returncode == 1, result
    report = json.loads(result.stdout)
    assert report['ignored_archives'] == [str(prebuilt)]
    assert report['archive_members']['total'] == 2
    assert report['archive_members']['changed'] == 2
    assert report['objects']['changed'] == 0
    print('PASS archive outside the archive root is ignored and reported')


def test_malformed_macho(base):
    reference, object_root, archive_root, archive = make_tree(
        base, default_entries(base))
    reference.write_bytes(b'not a Mach-O')
    result = run(*arguments(reference, object_root, archive_root, '--verify'))
    assert result.returncode == 2, result
    assert 'Mach-O' in result.stderr, result.stderr

    make_macho(reference, default_entries(base))
    data = reference.read_bytes()
    reference.write_bytes(data[:-4])
    result = run(*arguments(reference, object_root, archive_root, '--verify'))
    assert result.returncode == 2, result
    assert 'string table' in result.stderr, result.stderr
    print('PASS malformed Mach-O fails closed')


def test_malformed_archive(base):
    reference, object_root, archive_root, archive = make_tree(
        base, default_entries(base))
    data = bytearray(archive.read_bytes())
    data[8 + 58:8 + 60] = b'XX'
    archive.write_bytes(bytes(data))
    result = run(*arguments(reference, object_root, archive_root, '--apply'))
    assert result.returncode == 2, result
    assert 'terminator' in result.stderr, result.stderr
    assert archive.read_bytes() == bytes(data)
    print('PASS malformed archive fails closed')


def main():
    tests = (
        test_verify_then_apply,
        test_relocated_roots,
        test_missing_object,
        test_missing_member,
        test_outside_roots,
        test_escaping_archive_path,
        test_outside_archive_is_ignored,
        test_malformed_macho,
        test_malformed_archive,
    )
    for test in tests:
        with tempfile.TemporaryDirectory(prefix='gta4l-mtimes-test-') as tmp:
            test(Path(tmp))
    print('PASS gta4l XNU mtime pinning tests')
    return 0


if __name__ == '__main__':
    sys.exit(main())
