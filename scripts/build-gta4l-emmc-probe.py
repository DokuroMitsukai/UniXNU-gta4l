#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Build the opt-in gta4l eMMC diagnostic archives.

The portable core, the board platform and the read-only block request helper
are compiled into ``libGta4lEmmcCore.a``; the XNU probe is compiled into
``libGta4lEmmcProbe.a``. Both are compiled with the exact compile record of
bsd_init.o so they use the same XNU headers, target and ABI as the kernel that
calls them. The archives contain no framework or vendor code, and the build
rejects duplicate global symbols across them.

The probe is disabled unless the boot argument "gta4l-emmc-probe=1" is present.
It performs controller reset, GCC 400 kHz setup, the retained-rail power
handshake, eMMC enumeration and single-block CMD17 PIO reads of blocks 0/1,
the primary GPT array, the first-usable sector and the last sector. With
"gta4l-emmc-mb=1" alongside "gta4l-emmc-probe=1" it also re-reads the 21
primary-array sectors as one open-ended CMD18 transfer and stops it with an
explicit CMD12, reporting the per-LBA sums and the in-device comparison. It
does not publish a block device, mount a filesystem, write the card or touch
the PMIC.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/gta4l-emmc-probe'
OUT.mkdir(parents=True, exist_ok=True)

# In the assembled build tree the device package is kits/gta4l; in the device
# repository it is package/. Resolve the one that actually carries the core.
DEVICE = ROOT / 'kits' / 'gta4l'
if not (DEVICE / 'Gta4lEmmc.c').is_file():
    DEVICE = ROOT / 'package'

# Reusable code (the portable core, the board platform and the read-only block
# request helper) is separate from the diagnostic probe consumer so a future
# block driver can link the core without the probe.
ARCHIVES = {
    'core': OUT / 'libGta4lEmmcCore.a',
    'probe': OUT / 'libGta4lEmmcProbe.a',
}

PROBES = [
    {
        'source': DEVICE / 'Gta4lEmmc.c',
        'header': DEVICE / 'Gta4lEmmc.h',
        'object': OUT / 'Gta4lEmmc.o',
        'archive': 'core',
        'symbols': [],
    },
    {
        'source': DEVICE / 'Gta4lEmmcPlatform.c',
        'header': DEVICE / 'Gta4lEmmcPlatform.h',
        'object': OUT / 'Gta4lEmmcPlatform.o',
        'archive': 'core',
        'symbols': [],
    },
    {
        'source': DEVICE / 'Gta4lEmmcBlockCore.c',
        'header': DEVICE / 'Gta4lEmmcBlockCore.h',
        'object': OUT / 'Gta4lEmmcBlockCore.o',
        'archive': 'core',
        'symbols': [],
    },
    {
        'source': DEVICE / 'Gta4lEmmcProbe.c',
        'header': DEVICE / 'Gta4lEmmc.h',
        'object': OUT / 'Gta4lEmmcProbe.o',
        'archive': 'probe',
        'symbols': ['gta4l_emmc_probe_preflight',
                    'gta4l_emmc_probe_run'],
    },
]

record_path = (ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
               / 'bsd/DEVELOPMENT/bsd_init.o.json')

for probe in PROBES:
    if not probe['source'].is_file():
        raise SystemExit(f"missing source: {probe['source']}")
    if not probe['header'].is_file():
        raise SystemExit(f"missing header: {probe['header']}")

def tool_command(name: str, *arguments: str):
    xcrun = shutil.which('xcrun')
    if xcrun is not None:
        return [xcrun, name, *arguments]
    executable = shutil.which(name)
    if executable is None:
        raise SystemExit(f'no {name} tool for the eMMC archive build')
    return [executable, *arguments]


def archive_symbols(archive: Path) -> set:
    output = subprocess.run(tool_command('nm', '-g', str(archive)),
                            capture_output=True, text=True, check=True).stdout
    symbols = set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 2:
            continue
        kind = fields[-2]
        if kind == 'U' or len(kind) != 1:
            continue
        symbols.add(fields[-1].lstrip('_'))
    return symbols


def write_archive(archive: Path, objects) -> None:
    archive.unlink(missing_ok=True)
    subprocess.run(tool_command('ar', 'rcs', str(archive),
                                *[str(obj) for obj in objects]), check=True)


if not record_path.is_file():
    # A clean tree has no bsd_init.o compile record yet. Link a no-op first so
    # the bootstrap kernel can be produced; the next kernel build links the
    # real probe with the record from that bootstrap. The core archive stays
    # empty because nothing references the core until the real build.
    stub = OUT / 'FirstPassStub.c'
    declarations = '\n'.join(
        f'void {symbol}(void);\n'
        f'void {symbol}(void)\n{{\n}}\n'
        for probe in PROBES for symbol in probe['symbols'])
    stub.write_text('// Bootstrap only: the next kernel build links the real '
                    'probe.\n' + declarations)
    subprocess.run(['xcrun', '--sdk', 'iphoneos', 'clang', '-arch', 'arm64',
                    '-mkernel', '-miphoneos-version-min=17.0', '-Os', '-c',
                    str(stub), '-o', str(OUT / 'FirstPassStub.o')], check=True)
    write_archive(ARCHIVES['core'], [])
    write_archive(ARCHIVES['probe'], [OUT / 'FirstPassStub.o'])
    report = {
        'mode': 'bootstrap no-op; rerun the kernel build to link the real probe',
        'archives': {
            'core': {
                'sha256': hashlib.sha256(
                    ARCHIVES['core'].read_bytes()).hexdigest(),
                'objects': [],
            },
            'probe': {
                'sha256': hashlib.sha256(
                    ARCHIVES['probe'].read_bytes()).hexdigest(),
                'objects': ['FirstPassStub.o'],
            },
        },
        'archive_sha256': hashlib.sha256(
            ARCHIVES['probe'].read_bytes()).hexdigest(),
        'runtime_qualified': False,
    }
    (OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('gta4l eMMC probe: no compile record yet; linked a no-op bootstrap '
          'probe archive and an empty core archive')
    raise SystemExit(0)

record = json.loads(record_path.read_text().strip().rstrip(','))
grouped = {name: [] for name in ARCHIVES}
source_records = []
for probe in PROBES:
    arguments = list(record['arguments'])
    try:
        arguments[arguments.index(record['file'])] = str(probe['source'])
    except ValueError:
        raise SystemExit('compile record has no source path')
    try:
        arguments[arguments.index('-o') + 1] = str(probe['object'])
    except (ValueError, IndexError):
        raise SystemExit('compile record has no output path')
    arguments += [
        '-I', str(DEVICE),
        '-Wall', '-Wextra', '-Wno-missing-include-dirs',
        '-Wno-cast-qual', '-Wno-unsafe-buffer-usage', '-Wno-shadow-header',
    ]
    subprocess.run(arguments, cwd=record['directory'], check=True)
    grouped[probe['archive']].append(probe['object'])
    source_records.append({
        'source': probe['source'].name,
        'source_sha256': hashlib.sha256(probe['source'].read_bytes()).hexdigest(),
        'header': probe['header'].name,
        'header_sha256': hashlib.sha256(probe['header'].read_bytes()).hexdigest(),
        'object_sha256': hashlib.sha256(probe['object'].read_bytes()).hexdigest(),
        'archive': probe['archive'],
    })

for name, archive in ARCHIVES.items():
    write_archive(archive, grouped[name])

duplicates = archive_symbols(ARCHIVES['core']) & \
    archive_symbols(ARCHIVES['probe'])
if duplicates:
    raise SystemExit('duplicate global symbols across the eMMC archives: ' +
                     ', '.join(sorted(duplicates)))

report = {
    'sources': source_records,
    'archives': {
        name: {
            'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
            'objects': [obj.name for obj in grouped[name]],
        }
        for name, archive in ARCHIVES.items()
    },
    'archive_sha256': hashlib.sha256(
        ARCHIVES['probe'].read_bytes()).hexdigest(),
    'compile_record': str(record_path),
    'mode': ('opt-in controller reset, GCC 400 kHz, retained-rail power '
             'handshake, eMMC enumeration, CMD17 reads of blocks 0/1, the '
             'primary GPT array, the first usable sector and the last '
             'sector, plus an opt-in open-ended CMD18 re-read of the 21 '
             'array sectors stopped by one explicit CMD12; no '
             'block-device publication, filesystem mount, card write, DMA, '
             'CQE, ICE or PMIC access'),
    'runtime_qualified': False,
}
(OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'built {ARCHIVES["core"]} and {ARCHIVES["probe"]}')
