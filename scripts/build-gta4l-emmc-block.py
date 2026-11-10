#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Build the read-only gta4l eMMC block adapter archive.

The policy object (``Gta4lEmmcBlock.c``) is compiled with the bsd_init.o
compile record. The C++ adapter (``Gta4lEmmcBlock.cpp``) is compiled with the
IOPlatformExpert.cpo record and the qualified storage headers. The original
storage base-class symbols are bound from the recorded interface audit, then
the objects and ``original-bindings.o`` are archived into
``libGta4lEmmcBlock.a``. The archive is separate from ``libGta4lEmmcCore.a``
so the portable core is linked once.

The archive is only usable when the guarded
``XNU_NATIVE_GTA4L_EMMC_BLOCK`` personality is enabled. This script refuses a
missing, stale or wrong-candidate storage-ABI audit, records the qualification
state and never marks the archive ``runtime_qualified``. It does not enable
the personality, flash, boot or access a device.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/gta4l-emmc-block'
OUT.mkdir(parents=True, exist_ok=True)

# The UniXNU driver copies this script into out/<os>/<config>/tree/scripts and
# runs it there; a direct run starts in the device repository. The board
# sources, configuration, patch series and storage-ABI evidence stay in the
# device repository either way.
IN_TREE = (ROOT / 'kits' / 'XnuPrelink' / 'native_prelink.py').is_file()
if IN_TREE:
    DEVICE_ROOT = Path(os.environ['UNIXNU_DEVICE'])
    PRELINK_KIT = ROOT / 'kits' / 'XnuPrelink'
    KERNEL = ROOT.parent / 'kernel' / 'kernel.macho'
else:
    DEVICE_ROOT = ROOT
    PRELINK_KIT = ROOT.parent / 'UniXNU' / 'kits' / 'XnuPrelink'
    KERNEL = (ROOT.parent / 'UniXNU' / 'out/gta4l/ipados-17.7.11-21H461'
              / 'gta4l/kernel/kernel.macho')

DEVICE = ROOT / 'kits' / 'gta4l'
if not (DEVICE / 'Gta4lEmmcBlock.cpp').is_file():
    DEVICE = ROOT / 'package'

ARCHIVE = OUT / 'libGta4lEmmcBlock.a'
STORAGE_INCLUDE = ROOT / 'build/storage-abi/include'
QUALIFICATION = Path(os.environ.get(
    'GTA4L_STORAGE_ABI_DIR',
    str(DEVICE_ROOT / 'out/gta4l/storage-abi')))
AUDIT = QUALIFICATION / 'interface-audit.json'
CORE_ARCHIVE = ROOT / 'build/gta4l-emmc-probe/libGta4lEmmcCore.a'
PROBE_ARCHIVE = ROOT / 'build/gta4l-emmc-probe/libGta4lEmmcProbe.a'
DEFCONFIG = DEVICE_ROOT / 'configs' / 'gta4l_defconfig'
SERIES = DEVICE_ROOT / 'patches' / 'xnu' / 'series'
PERSONALITY_PATCH = DEVICE_ROOT / 'patches' / 'xnu' / 'gta4l-emmc-block.patch'
SLOT_COUNT = 190
CLASS_MANGLE = '14Gta4lEmmcBlock'
POLICY_RECORD = (ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
                 / 'bsd/DEVELOPMENT/bsd_init.o.json')
ADAPTER_RECORD = (ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
                  / 'iokit/DEVELOPMENT/IOPlatformExpert.cpo.json')
PROBE_SOURCE = (ROOT / 'scripts' / 'sim-gta4l-emmc-block'
                / 'consumer_lifecycle_probe.cpp')
PROBE_OBJECT = OUT / 'consumer-lifecycle-probe.o'
PROBE_IMPORTS = 187
PROBE_SECTION_SPLIT = {
    '__TEXT_EXEC,__text': 185,
    '__DATA_CONST,__const': 1,
    '__DATA,__common': 1,
}
PROBE_CONST_SYMBOL = '_PAGE_SHIFT_CONST'
PROBE_COMMON_SYMBOL = '__ZN9IOService10gMetaClassE'

SOURCES = [
    {
        'source': DEVICE / 'Gta4lEmmcBlock.c',
        'header': DEVICE / 'Gta4lEmmcBlock.h',
        'object': OUT / 'Gta4lEmmcBlock.o',
        'record': POLICY_RECORD,
    },
    {
        'source': DEVICE / 'Gta4lEmmcBlock.cpp',
        'header': DEVICE / 'Gta4lEmmcBlock.h',
        'object': OUT / 'Gta4lEmmcBlockCpp.o',
        'record': ADAPTER_RECORD,
    },
]

QUALIFICATION_FILES = ('gdb-stop.json', 'interface-audit.json',
                       'provenance.json')


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tool_command(name: str, *arguments: str):
    xcrun = shutil.which('xcrun')
    if xcrun is not None:
        return [xcrun, name, *arguments]
    for candidate in (f'llvm-{name}', name):
        executable = shutil.which(candidate)
        if executable is not None:
            return [executable, *arguments]
    raise SystemExit(f'no {name} tool for the eMMC block archive build')


def defined_symbols(path: Path) -> set:
    output = subprocess.run(tool_command('nm', '-g', str(path)),
                            capture_output=True, text=True, check=True).stdout
    symbols = set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 2:
            continue
        kind = fields[-2]
        if kind == 'U' or kind == 'u' or len(kind) != 1:
            continue
        symbols.add(fields[-1].lstrip('_'))
    return symbols


def archive_symbols(archive: Path) -> set:
    return defined_symbols(archive)


def symbol_sections(path: Path) -> dict:
    output = subprocess.run(tool_command('nm', '-m', '-g', str(path)),
                            capture_output=True, text=True, check=True).stdout
    sections = {}
    for line in output.splitlines():
        match = re.match(r'^[0-9a-fA-F]+ \(([^)]*)\) \S+ (\S+)$', line)
        if match is not None:
            sections[match.group(2)] = match.group(1)
    return sections


def write_archive(objects) -> None:
    ARCHIVE.unlink(missing_ok=True)
    # Apple ar stores each member's mtime.  Zero it so a relink of unchanged
    # objects produces the same archive, and therefore the same candidate.
    environment = dict(os.environ, ZERO_AR_DATE='1')
    subprocess.run(tool_command('ar', 'rcs', str(ARCHIVE),
                                *[str(obj) for obj in objects]),
                   check=True, env=environment)


def load_object(path: Path):
    sys.path.insert(0, str(PRELINK_KIT))
    import native_prelink as np
    data = path.read_bytes()
    image = np.macho(data)
    offset = next(at for command, at, size in image['commands']
                  if command == 2)
    symbol_offset, count, string_offset, _ = struct.unpack_from(
        '<4I', data, offset + 8)
    symbols = []
    for index in range(count):
        name_offset, kind, _, _, value = struct.unpack_from(
            '<IBBHQ', data, symbol_offset + index * 16)
        start = string_offset + name_offset
        name = data[start:data.index(b'\0', start)].decode()
        symbols.append((name, value, kind))
    return data, image, symbols


def vtable_relocations(data, image, symbols, vtable):
    relocations = {}
    for segment in image['segments']:
        for section in segment['sections']:
            if not (section['va'] <= vtable <
                    section['va'] + section['size']):
                continue
            offset, count = struct.unpack_from(
                '<II', data, section['command_offset'] + 56)
            for index in range(count):
                address, word = struct.unpack_from(
                    '<II', data, offset + index * 8)
                if (word >> 27 & 1 and word >> 28 == 0 and
                        word >> 25 & 3 == 3):
                    relocations[section['va'] + address] = \
                        symbols[word & 0xffffff][0]
    return relocations


def demangle(names):
    commands = []
    xcrun = shutil.which('xcrun')
    if xcrun is not None:
        commands.append([xcrun, 'c++filt'])
    for candidate in ('llvm-cxxfilt', 'c++filt'):
        executable = shutil.which(candidate)
        if executable is not None:
            commands.append([executable])
    if not commands:
        raise SystemExit('no c++filt tool for the eMMC block archive build')
    text = '\n'.join(name[1:] for name in names) + '\n'
    for command in commands:
        result = subprocess.run(command, input=text, capture_output=True,
                                text=True)
        if result.returncode == 0:
            return dict(zip(names, result.stdout.splitlines()))
    raise SystemExit('c++filt failed for the eMMC block archive build')


def check_consumer_abi():
    if not PROBE_SOURCE.is_file():
        raise SystemExit(f'missing consumer ABI probe: {PROBE_SOURCE}')
    record = json.loads(ADAPTER_RECORD.read_text().strip().rstrip(','))
    arguments = list(record['arguments'])
    try:
        arguments[arguments.index(record['file'])] = str(PROBE_SOURCE)
    except ValueError:
        raise SystemExit(f'{ADAPTER_RECORD}: no source path')
    try:
        arguments[arguments.index('-o') + 1] = str(PROBE_OBJECT)
    except (ValueError, IndexError):
        raise SystemExit(f'{ADAPTER_RECORD}: no output path')
    arguments += ['-I', str(STORAGE_INCLUDE), '-I', str(DEVICE)]
    subprocess.run(arguments, cwd=record['directory'], check=True)
    _, _, symbols = load_object(PROBE_OBJECT)
    imports = [name for name, value, kind in symbols if kind == 1]
    sections = symbol_sections(KERNEL)
    missing = sorted(name for name in imports if name not in sections)
    if missing:
        raise SystemExit('consumer import absent from candidate kernel: '
                         + ', '.join(missing))
    split = {}
    for name in imports:
        section = sections[name]
        split[section] = split.get(section, 0) + 1
    if (len(imports) != PROBE_IMPORTS) or (split != PROBE_SECTION_SPLIT):
        raise SystemExit('consumer ABI surface changed: '
                         + str(len(imports)) + ' imports ' + str(split))
    if sections.get(PROBE_CONST_SYMBOL) != '__DATA_CONST,__const':
        raise SystemExit('consumer PAGE_SHIFT_CONST is not '
                         '__DATA_CONST,__const')
    if sections.get(PROBE_COMMON_SYMBOL) != '__DATA,__common':
        raise SystemExit('consumer IOService::gMetaClass is not '
                         '__DATA,__common')
    return {
        'probe_source_sha256': sha256(PROBE_SOURCE),
        'probe_object_sha256': sha256(PROBE_OBJECT),
        'imports': len(imports),
        'sections': split,
    }


def check_archive_imports(undefined):
    archive_defined = set()
    for item in SOURCES:
        _, _, symbols = load_object(item['object'])
        for name, value, kind in symbols:
            if kind != 1:
                archive_defined.add(name)
    kernel_exports = defined_symbols(KERNEL)
    checked = set()
    for name in undefined:
        if name in archive_defined:
            continue
        if 'IOBlockStorageDevice' in name:
            continue
        checked.add(name.lstrip('_'))
    missing = sorted(checked - kernel_exports)
    if missing:
        raise SystemExit('archive import absent from candidate kernel: '
                         + ', '.join(missing))
    return len(checked)


def generate_bindings():
    if not AUDIT.is_file():
        raise SystemExit(f'missing storage-ABI audit: {AUDIT}')
    if not KERNEL.is_file():
        raise SystemExit(f'missing storage-ABI candidate: {KERNEL}')
    audit = json.loads(AUDIT.read_text())
    if sha256(KERNEL) != audit.get('candidate_sha256'):
        raise SystemExit('storage-ABI audit candidate does not match '
                         + str(KERNEL))
    if (audit.get('method_slots') != SLOT_COUNT or
            not audit.get('live_native_4k') or
            not audit.get('abstract_complete_destructors_trap')):
        raise SystemExit('storage-ABI audit is not qualified')

    data, image, symbols = load_object(OUT / 'Gta4lEmmcBlockCpp.o')
    vtable = next((value for name, value, kind in symbols
                   if name == '__ZTV' + CLASS_MANGLE), None)
    if vtable is None:
        raise SystemExit('adapter object has no vtable')
    relocations = vtable_relocations(data, image, symbols, vtable)
    compiled = [relocations.get(vtable + 16 + index * 8)
                for index in range(SLOT_COUNT)]
    if (not all(compiled) or '___cxa_pure_virtual' in compiled or
            vtable + 16 + SLOT_COUNT * 8 in relocations):
        raise SystemExit('adapter has missing/additional/pure virtual '
                         'methods')

    all_names = list(dict.fromkeys(
        compiled + [row['reference_name'] for row in audit['methods']]))
    readable = demangle(all_names)
    overrides = 0
    for row, actual in zip(audit['methods'], compiled):
        expected = row['reference_name']
        own = actual.startswith(('__ZN' + CLASS_MANGLE,
                                 '__ZNK' + CLASS_MANGLE))
        if expected == '___cxa_pure_virtual':
            if not own:
                raise SystemExit('adapter does not implement a required '
                                 'transport operation')
        elif actual != expected:
            if (not own or
                    (row['slot'] not in (0, 1) and
                     readable[actual].split('::', 1)[1] !=
                     readable[expected].split('::', 1)[1])):
                raise SystemExit('adapter dispatch mismatch at slot '
                                 + str(row['slot']))
        overrides += own

    # Bind every audited original method, including ones the kernel currently
    # exports because a subclass is linked.  Keeping the alias set independent
    # of the candidate's export surface makes the archive stable across the
    # candidate-matched re-audit; the audit has already checked each target.
    possible = {row['reference_name']: int(row['candidate_target'], 16)
                for row in audit['methods']}
    possible.update({
        '__ZN20IOBlockStorageDeviceC2EPK11OSMetaClass':
            int(audit['original_protected_constructor'], 16),
        '__ZN20IOBlockStorageDeviceD2Ev':
            int(audit['original_base_destructor'], 16),
        '__ZN20IOBlockStorageDevice10gMetaClassE':
            int(audit['original_metaclass'], 16),
        '__ZTV20IOBlockStorageDevice': int(audit['vtable'], 16),
    })
    all_undefined = [name for name, value, kind in symbols if kind == 1]
    undefined = [name for name in all_undefined
                 if 'IOBlockStorageDevice' in name]
    unqualified = [name for name in undefined if name not in possible]
    if unqualified:
        raise SystemExit('unqualified original storage import: '
                         + ', '.join(sorted(unqualified)))
    if any(re.search(r'IOBlockStorageDeviceD[01]Ev$', name)
           for name in undefined):
        raise SystemExit('adapter must not invoke abstract complete/deleting '
                         'destruction')
    media = [name for name, value, kind in symbols
             if kind == 1 and 'IOMedia' in name]
    if media:
        raise SystemExit('adapter imports unbound IOMedia symbols: '
                         + ', '.join(sorted(media)))

    consumer_abi = check_consumer_abi()
    kernel_imports_checked = check_archive_imports(all_undefined)

    bindings = {name: possible[name] for name in undefined}
    assembly = OUT / 'original-bindings.S'
    assembly.write_text(''.join(
        f'.globl {name}\n.set {name}, {value:#x}\n'
        for name, value in sorted(bindings.items())))
    record = json.loads(ADAPTER_RECORD.read_text().strip().rstrip(','))
    compiler = record['arguments'][0]
    bindings_object = OUT / 'original-bindings.o'
    subprocess.run([compiler, '-Werror', '--target=arm64-apple-ios17.0',
                    '-c', str(assembly), '-o', str(bindings_object)],
                   check=True)
    return {
        'candidate_sha256': audit['candidate_sha256'],
        'interface_audit_sha256': sha256(AUDIT),
        'assembly_sha256': sha256(assembly),
        'object_sha256': sha256(bindings_object),
        'virtual_methods_checked': SLOT_COUNT,
        'concrete_overrides': overrides,
        'unresolved_original_imports': 0,
        'consumer_abi': consumer_abi,
        'kernel_imports_checked': kernel_imports_checked,
        'bindings': {name: hex(value)
                     for name, value in bindings.items()},
    }, bindings_object


def personality_state() -> dict:
    configured = False
    for line in DEFCONFIG.read_text().splitlines():
        entry = line.strip()
        if entry == 'CONFIG_XNU_NATIVE_GTA4L_EMMC_BLOCK=y':
            configured = True
        elif entry.startswith('CONFIG_XNU_NATIVE_GTA4L_EMMC_BLOCK='):
            configured = False
    series = [line.strip() for line in SERIES.read_text().splitlines()
              if line.strip() and not line.strip().startswith('#')]
    patch_in_series = 'gta4l-emmc-block.patch' in series
    guarded = ('#if defined(XNU_NATIVE_GTA4L_EMMC_BLOCK) && '
               'XNU_NATIVE_GTA4L_EMMC_BLOCK' in
               PERSONALITY_PATCH.read_text())
    return {
        'configured': configured,
        'patch_in_series': patch_in_series,
        'guarded': guarded,
        'enabled': configured and patch_in_series and guarded,
    }


def qualification_state() -> dict:
    missing = [name for name in QUALIFICATION_FILES
               if not (QUALIFICATION / name).is_file()]
    state = {
        'qualified': False,
        'missing': missing,
        'directory': str(QUALIFICATION),
    }
    if missing:
        return state
    provenance = json.loads((QUALIFICATION / 'provenance.json').read_text())
    audit = json.loads((QUALIFICATION / 'interface-audit.json').read_text())
    kernel_sha = sha256(KERNEL) if KERNEL.is_file() else None
    candidate = (provenance.get('candidate_sha256') or
                 provenance.get('candidate') or provenance.get('kernel'))
    matches = (kernel_sha is not None and
               kernel_sha == audit.get('candidate_sha256') and
               kernel_sha == candidate)
    state.update({
        'provenance_sha256': sha256(QUALIFICATION / 'provenance.json'),
        'interface_audit_sha256': sha256(QUALIFICATION / 'interface-audit.json'),
        'gdb_stop_sha256': sha256(QUALIFICATION / 'gdb-stop.json'),
        'candidate': candidate,
        'kernel_sha256': kernel_sha,
        'candidate_matches_audit': matches,
        'audit_method_slots': audit.get('method_slots'),
        'audit_live_native_4k': audit.get('live_native_4k'),
        'audit_abstract_complete_destructors_trap':
            audit.get('abstract_complete_destructors_trap'),
    })
    state['qualified'] = (
        matches and
        audit.get('method_slots') == SLOT_COUNT and
        audit.get('live_native_4k') is True and
        audit.get('abstract_complete_destructors_trap') is True)
    return state


def bootstrap() -> None:
    write_archive([])
    personality = personality_state()
    report = {
        'mode': 'bootstrap no-op; rerun the kernel build to link the real adapter',
        'archive': {
            'path': str(ARCHIVE),
            'sha256': sha256(ARCHIVE),
            'objects': [],
        },
        'qualification': qualification_state(),
        'runtime_qualified': False,
        'personality_enabled': personality['enabled'],
        'personality': personality,
    }
    (OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('gta4l eMMC block: no compile record yet; linked an empty bootstrap '
          'archive')
    raise SystemExit(0)


for item in SOURCES:
    if not item['source'].is_file():
        raise SystemExit(f"missing source: {item['source']}")
    if not item['header'].is_file():
        raise SystemExit(f"missing header: {item['header']}")
    if not item['record'].is_file():
        bootstrap()

if not STORAGE_INCLUDE.is_dir():
    raise SystemExit(f'missing storage ABI include directory: {STORAGE_INCLUDE}')

source_records = []
for item in SOURCES:
    record = json.loads(item['record'].read_text().strip().rstrip(','))
    arguments = list(record['arguments'])
    try:
        arguments[arguments.index(record['file'])] = str(item['source'])
    except ValueError:
        raise SystemExit(f"{item['record']}: no source path")
    try:
        arguments[arguments.index('-o') + 1] = str(item['object'])
    except (ValueError, IndexError):
        raise SystemExit(f"{item['record']}: no output path")
    arguments += ['-I', str(DEVICE)]
    if item['source'].suffix == '.cpp':
        arguments += ['-I', str(STORAGE_INCLUDE)]
    subprocess.run(arguments, cwd=record['directory'], check=True)
    source_records.append({
        'source': item['source'].name,
        'source_sha256': sha256(item['source']),
        'header': item['header'].name,
        'header_sha256': sha256(item['header']),
        'object': item['object'].name,
        'object_sha256': sha256(item['object']),
        'compile_record': str(item['record']),
    })

bindings, bindings_object = generate_bindings()
write_archive([item['object'] for item in SOURCES] + [bindings_object])

block_symbols = archive_symbols(ARCHIVE)
for other in (CORE_ARCHIVE, PROBE_ARCHIVE):
    if not other.is_file():
        continue
    duplicates = archive_symbols(other) & block_symbols
    if duplicates:
        raise SystemExit('duplicate global symbols across the eMMC archives: '
                         + ', '.join(sorted(duplicates)))

qualification = qualification_state()
personality = personality_state()
report = {
    'sources': source_records,
    'bindings': bindings,
    'archive': {
        'path': str(ARCHIVE),
        'sha256': sha256(ARCHIVE),
        'objects': [item['object'].name for item in SOURCES] +
                   [bindings_object.name],
    },
    'qualification': qualification,
    'runtime_qualified': False,
    'personality_enabled': personality['enabled'],
    'personality': personality,
    'mode': ('read-only IOBlockStorageDevice adapter and policy with '
             'audit-derived original storage bindings; the guarded '
             'XNU_NATIVE_GTA4L_EMMC_BLOCK personality is '
             + ('enabled' if personality['enabled'] else 'disabled')
             + ' in the board configuration'),
}
(OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'built {ARCHIVE}')
if not qualification['qualified']:
    print('storage-ABI qualification artifacts are missing: '
          + ', '.join(qualification['missing']))
