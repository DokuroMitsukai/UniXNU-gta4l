#!/usr/bin/env python3
"""Pin XNU object and archive-member mtimes recorded in N_OSO symbols.

ld64 records the mtime of every linked object in an N_OSO nlist. A rebuilt
tree has new mtimes, so a byte-identical relink needs the reference mtimes.
This host-only helper verifies or restores the mtimes of the recorded .o and
.cpo files and the member dates of the recorded in-tree ar archives. It is a
comparison/repair tool, not a build input.
"""
import argparse
import hashlib
import json
import os
import struct
import sys
from pathlib import Path

MACHO_MAGIC = 0xfeedfacf
LC_SYMTAB = 0x2
N_OSO = 0x66
NLIST64 = struct.Struct('<IBBHQ')
AR_MAGIC = b'!<arch>\n'
AR_HEADER_SIZE = 60
AR_DATE_OFFSET = 16
AR_DATE_SIZE = 12
AR_SIZE_OFFSET = 48
AR_SIZE_SIZE = 10
AR_TERMINATOR = b'`\n'


class Error(Exception):
    pass


class ObjectEntry:
    __slots__ = ('recorded', 'path', 'mtime')

    def __init__(self, recorded, path, mtime):
        self.recorded = recorded
        self.path = path
        self.mtime = mtime


class ArchiveMember:
    __slots__ = ('name', 'date', 'header_offset')

    def __init__(self, name, date, header_offset):
        self.name = name
        self.date = date
        self.header_offset = header_offset


class Archive:
    __slots__ = ('path', 'data', 'members')

    def __init__(self, path, data):
        self.path = path
        self.data = data
        self.members = {}
        self._parse()

    def _parse(self):
        data = self.data
        if not data.startswith(AR_MAGIC):
            raise Error(f'{self.path}: not a BSD ar archive')
        offset = len(AR_MAGIC)
        while offset < len(data):
            if offset + AR_HEADER_SIZE > len(data):
                raise Error(f'{self.path}: truncated member header at '
                            f'{offset:#x}')
            header = data[offset:offset + AR_HEADER_SIZE]
            if header[58:60] != AR_TERMINATOR:
                raise Error(f'{self.path}: bad member terminator at '
                            f'{offset:#x}')
            name_field = self._text(
                header[0:16], 'member name', offset).strip()
            if not name_field:
                raise Error(f'{self.path}: empty member name at {offset:#x}')
            date_text = self._text(header[AR_DATE_OFFSET:
                                          AR_DATE_OFFSET + AR_DATE_SIZE],
                                   'member date', offset).strip()
            if not date_text.isdigit():
                raise Error(f'{self.path}: bad member date at {offset:#x}')
            size_text = self._text(header[AR_SIZE_OFFSET:
                                          AR_SIZE_OFFSET + AR_SIZE_SIZE],
                                   'member size', offset).strip()
            if not size_text.isdigit():
                raise Error(f'{self.path}: bad member size at {offset:#x}')
            date = int(date_text)
            size = int(size_text)
            data_offset = offset + AR_HEADER_SIZE
            end = data_offset + size
            if end > len(data):
                raise Error(f'{self.path}: member data at {offset:#x} runs '
                            f'past the end')
            name = name_field
            if name_field.startswith('#1/'):
                length_text = name_field[3:]
                if not length_text.isdigit():
                    raise Error(f'{self.path}: bad long member name at '
                                f'{offset:#x}')
                length = int(length_text)
                if length == 0 or length > size:
                    raise Error(f'{self.path}: bad long member name length '
                                f'at {offset:#x}')
                raw_name = data[data_offset:data_offset + length]
                name = raw_name.split(b'\0', 1)[0].decode(
                    'utf-8', 'surrogateescape')
                if not name:
                    raise Error(f'{self.path}: empty long member name at '
                                f'{offset:#x}')
            elif name in ('/', '//'):
                name = ''
            elif name.endswith('/'):
                name = name[:-1]
            if name:
                self.members.setdefault(name, []).append(
                    ArchiveMember(name, date, offset))
            offset = end + (size % 2)
            if offset > len(data):
                raise Error(f'{self.path}: member padding runs past the end')
        if offset != len(data):
            raise Error(f'{self.path}: trailing bytes at {offset:#x}')

    def _text(self, field, label, offset):
        try:
            return field.decode('ascii')
        except UnicodeDecodeError:
            raise Error(f'{self.path}: non-ASCII {label} at {offset:#x}')


class ArchiveEntry:
    __slots__ = ('recorded', 'archive', 'member', 'mtime')

    def __init__(self, recorded, archive, member, mtime):
        self.recorded = recorded
        self.archive = archive
        self.member = member
        self.mtime = mtime


class Plan:
    __slots__ = ('reference_path', 'reference_sha256', 'objects', 'archives',
                 'ignored_archives')

    def __init__(self, reference_path, reference_sha256, objects, archives,
                 ignored_archives):
        self.reference_path = reference_path
        self.reference_sha256 = reference_sha256
        self.objects = objects
        self.archives = archives
        self.ignored_archives = ignored_archives


def read_bytes(path):
    try:
        return path.read_bytes()
    except OSError as exc:
        raise Error(f'{path}: {exc.strerror or exc}')


def parse_reference(path, data):
    if len(data) < 32:
        raise Error(f'{path}: truncated Mach-O header')
    magic, _cputype, _cpusubtype, _filetype, ncmds, sizeofcmds, _flags, \
        _reserved = struct.unpack_from('<IiiIIIII', data, 0)
    if magic != MACHO_MAGIC:
        raise Error(f'{path}: not a 64-bit little-endian Mach-O '
                    f'(magic {magic:#x})')
    commands_end = 32 + sizeofcmds
    if commands_end > len(data):
        raise Error(f'{path}: load commands run past the end')
    offset = 32
    symtab = None
    for _ in range(ncmds):
        if offset + 8 > commands_end:
            raise Error(f'{path}: truncated load command at {offset:#x}')
        command, size = struct.unpack_from('<II', data, offset)
        if size < 8 or offset + size > commands_end:
            raise Error(f'{path}: bad load command size at {offset:#x}')
        if command == LC_SYMTAB:
            if symtab is not None:
                raise Error(f'{path}: multiple LC_SYMTAB commands')
            if size < 24:
                raise Error(f'{path}: truncated LC_SYMTAB')
            symtab = struct.unpack_from('<IIII', data, offset + 8)
        offset += size
    if offset != commands_end:
        raise Error(f'{path}: load commands do not fill sizeofcmds')
    if symtab is None:
        raise Error(f'{path}: no LC_SYMTAB')
    symoff, nsyms, stroff, strsize = symtab
    if symoff + nsyms * NLIST64.size > len(data):
        raise Error(f'{path}: symbol table runs past the end')
    if stroff + strsize > len(data):
        raise Error(f'{path}: string table runs past the end')
    entries = []
    seen = set()
    for index in range(nsyms):
        strx, n_type, _sect, _desc, value = NLIST64.unpack_from(
            data, symoff + index * NLIST64.size)
        if n_type != N_OSO:
            continue
        if strx >= strsize:
            raise Error(f'{path}: N_OSO {index} has a string index outside '
                        f'the string table')
        start = stroff + strx
        end = data.find(b'\0', start, stroff + strsize)
        if end < 0:
            raise Error(f'{path}: N_OSO {index} has an unterminated name')
        name = data[start:end].decode('utf-8', 'surrogateescape')
        if not name:
            raise Error(f'{path}: N_OSO {index} has an empty name')
        if name in seen:
            raise Error(f'{path}: duplicate N_OSO path {name}')
        seen.add(name)
        entries.append((name, value))
    if not entries:
        raise Error(f'{path}: no N_OSO entries')
    return entries


def _relative_parts(recorded, root):
    parts = Path(recorded).parts
    root_parts = root.parts
    anchors = []
    if len(root_parts) >= 2:
        anchors.append(root_parts[-2:])
    anchors.append(root_parts[-1:])
    for anchor in anchors:
        matches = [index for index in range(len(parts) - len(anchor) + 1)
                   if parts[index:index + len(anchor)] == anchor]
        if len(matches) > 1:
            raise Error(f'{recorded}: ambiguous {"/".join(anchor)} anchor')
        if len(matches) == 1:
            relative = parts[matches[0] + len(anchor):]
            if not relative or any(part in ('', '.', '..')
                                   for part in relative):
                raise Error(f'{recorded}: invalid mapped path')
            return relative
    return None


def _mapped_path(recorded, root, label, required):
    path = Path(recorded)
    if not path.is_absolute():
        raise Error(f'{recorded}: N_OSO {label} path is not absolute')
    try:
        resolved = path.resolve(strict=False)
    except OSError as exc:
        raise Error(f'{recorded}: cannot resolve {label} path: {exc}')
    lexical = path.is_relative_to(root)
    if lexical and not resolved.is_relative_to(root):
        raise Error(f'{recorded}: {label} path escapes {root}')
    if resolved.is_relative_to(root):
        return resolved
    relative = _relative_parts(recorded, root)
    if relative is None:
        if required:
            raise Error(f'{recorded}: cannot map under {label} root {root}')
        return None
    try:
        candidate = root.joinpath(*relative).resolve(strict=False)
    except OSError as exc:
        raise Error(f'{recorded}: cannot resolve mapped {label} path: {exc}')
    if not candidate.is_relative_to(root):
        raise Error(f'{recorded}: mapped {label} path escapes {root}')
    return candidate


def map_object_path(recorded, object_root):
    return _mapped_path(recorded, object_root, 'object', True)


def map_archive_path(recorded, archive_root):
    return _mapped_path(recorded, archive_root, 'archive', False)


def split_archive_name(name):
    if not name.endswith(')'):
        raise Error(f'{name}: malformed archive member path')
    archive, separator, member = name[:-1].rpartition('(')
    if not separator or not archive or not member:
        raise Error(f'{name}: malformed archive member path')
    return archive, member


def build_plan(reference_path, reference_sha256, entries, object_root,
               archive_root):
    objects = []
    archives = []
    ignored = []
    seen_objects = {}
    seen_members = {}
    archive_cache = {}
    for recorded, mtime in entries:
        if '(' in recorded or ')' in recorded:
            archive_name, member_name = split_archive_name(recorded)
            if archive_root is None:
                ignored.append(archive_name)
                continue
            mapped = map_archive_path(archive_name, archive_root)
            if mapped is None:
                ignored.append(archive_name)
                continue
            archive = archive_cache.get(mapped)
            if archive is None:
                if not mapped.is_file():
                    raise Error(f'{archive_name}: archive not found at '
                                f'{mapped}')
                archive = Archive(mapped, read_bytes(mapped))
                archive_cache[mapped] = archive
            matches = archive.members.get(member_name, [])
            if not matches:
                raise Error(f'{recorded}: member {member_name!r} not found in '
                            f'{mapped}')
            if len(matches) > 1:
                raise Error(f'{recorded}: member {member_name!r} is ambiguous '
                            f'in {mapped}')
            key = (mapped, member_name)
            if key in seen_members:
                raise Error(f'{recorded}: duplicate archive member mapping')
            seen_members[key] = mtime
            archives.append(ArchiveEntry(recorded, archive, matches[0], mtime))
        else:
            mapped = map_object_path(recorded, object_root)
            if not mapped.is_file():
                raise Error(f'{recorded}: object not found at {mapped}')
            if mapped in seen_objects:
                raise Error(f'{recorded}: maps to duplicate object {mapped}')
            seen_objects[mapped] = mtime
            objects.append(ObjectEntry(recorded, mapped, mtime))
    return Plan(reference_path, reference_sha256, objects, archives, ignored)


def object_mtime(entry):
    try:
        return entry.path.stat().st_mtime_ns // 1_000_000_000
    except OSError as exc:
        raise Error(f'{entry.path}: cannot read mtime: '
                    f'{exc.strerror or exc}')


def archive_member_date(entry):
    try:
        with entry.archive.path.open('rb') as stream:
            stream.seek(entry.member.header_offset + AR_DATE_OFFSET)
            text = stream.read(AR_DATE_SIZE).decode('ascii').strip()
    except (OSError, UnicodeDecodeError) as exc:
        raise Error(f'{entry.archive.path}: cannot read member date: {exc}')
    if not text.isdigit():
        raise Error(f'{entry.archive.path}: bad member date at '
                    f'{entry.member.header_offset:#x}')
    return int(text)


def format_date(value):
    text = str(value).encode('ascii')
    if len(text) > AR_DATE_SIZE:
        raise Error(f'{value}: timestamp does not fit in an ar date field')
    return text.ljust(AR_DATE_SIZE, b' ')


def set_object_mtime(entry):
    timestamp = entry.mtime * 1_000_000_000
    try:
        os.utime(entry.path, ns=(timestamp, timestamp))
    except OSError as exc:
        raise Error(f'{entry.path}: cannot set mtime: '
                    f'{exc.strerror or exc}')


def patch_archive_member(entry):
    date = format_date(entry.mtime)
    try:
        with entry.archive.path.open('r+b') as stream:
            stream.seek(entry.member.header_offset + AR_DATE_OFFSET)
            stream.write(date)
    except OSError as exc:
        raise Error(f'{entry.archive.path}: cannot patch member date: '
                    f'{exc.strerror or exc}')


def changes(plan):
    changed_objects = [entry for entry in plan.objects
                       if object_mtime(entry) != entry.mtime]
    changed_members = [entry for entry in plan.archives
                       if entry.member.date != entry.mtime]
    return changed_objects, changed_members


def apply_changes(changed_objects, changed_members):
    for entry in changed_objects:
        set_object_mtime(entry)
    for entry in changed_members:
        patch_archive_member(entry)
    for entry in changed_objects:
        if object_mtime(entry) != entry.mtime:
            raise Error(f'{entry.path}: mtime did not stick')
    for entry in changed_members:
        if archive_member_date(entry) != entry.mtime:
            raise Error(f'{entry.archive.path}: member date did not stick')


def make_report(plan, mode, changed_objects, changed_members, errors,
                reference_sha256):
    object_changed = len(changed_objects)
    member_changed = len(changed_members)
    return {
        'mode': mode,
        'reference_kernel': str(plan.reference_path) if plan else None,
        'reference_sha256': reference_sha256,
        'object_root': None,
        'archive_root': None,
        'objects': {
            'total': len(plan.objects) if plan else 0,
            'changed': object_changed,
            'unchanged': (len(plan.objects) - object_changed) if plan else 0,
        },
        'archive_members': {
            'total': len(plan.archives) if plan else 0,
            'changed': member_changed,
            'unchanged': (len(plan.archives) - member_changed) if plan else 0,
        },
        'ignored_archives': sorted(set(plan.ignored_archives)) if plan else [],
        'changed': object_changed + member_changed,
        'unchanged': ((len(plan.objects) - object_changed) +
                      (len(plan.archives) - member_changed)) if plan else 0,
        'errors': errors,
    }


def print_report(report):
    print(f'reference kernel: {report["reference_kernel"]}')
    print(f'reference sha256: {report["reference_sha256"]}')
    print(f'object root: {report["object_root"]}')
    print(f'archive root: {report["archive_root"]}')
    print(f'objects: {report["objects"]["total"]} recorded, '
          f'{report["objects"]["changed"]} changed, '
          f'{report["objects"]["unchanged"]} unchanged')
    print(f'archive members: {report["archive_members"]["total"]} recorded, '
          f'{report["archive_members"]["changed"]} changed, '
          f'{report["archive_members"]["unchanged"]} unchanged')
    print(f'ignored archives: {len(report["ignored_archives"])}')
    for error in report['errors']:
        print(f'error: {error}', file=sys.stderr)
    if report['errors']:
        print(f'{report["mode"]}: failed validation', file=sys.stderr)
    elif report['mode'] == 'verify' and report['changed']:
        print(f'verify: {report["changed"]} recorded mtimes differ')
    elif report['mode'] == 'verify':
        print('verify: all recorded mtimes match')
    else:
        print(f'apply: {report["changed"]} recorded mtimes updated')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-kernel', type=Path, required=True)
    parser.add_argument('--object-root', type=Path, required=True)
    parser.add_argument('--archive-root', type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--verify', action='store_const', const='verify',
                      dest='mode')
    mode.add_argument('--apply', action='store_const', const='apply',
                      dest='mode')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    selected_mode = args.mode or 'verify'

    reference_path = args.reference_kernel
    object_root = args.object_root
    archive_root = args.archive_root
    reference_sha256 = None
    plan = None
    errors = []
    changed_objects = []
    changed_members = []

    try:
        reference_path = reference_path.resolve()
        object_root = object_root.resolve()
        if archive_root is not None:
            archive_root = archive_root.resolve()
        if not reference_path.is_file():
            raise Error(f'{reference_path}: reference kernel not found')
        if not object_root.is_dir():
            raise Error(f'{object_root}: object root not found')
        if archive_root is not None and not archive_root.is_dir():
            raise Error(f'{archive_root}: archive root not found')
        reference_data = read_bytes(reference_path)
        reference_sha256 = hashlib.sha256(reference_data).hexdigest()
        entries = parse_reference(reference_path, reference_data)
        plan = build_plan(reference_path, reference_sha256, entries,
                          object_root, archive_root)
        changed_objects, changed_members = changes(plan)
        if selected_mode == 'apply' and (changed_objects or changed_members):
            apply_changes(changed_objects, changed_members)
    except (Error, OSError) as exc:
        errors.append(str(exc))

    report = make_report(plan, selected_mode, changed_objects,
                         changed_members, errors, reference_sha256)
    report['reference_kernel'] = str(reference_path)
    report['object_root'] = str(object_root)
    report['archive_root'] = str(archive_root) if archive_root else None
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print_report(report)
    if errors:
        return 2
    if selected_mode == 'verify' and report['changed']:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
