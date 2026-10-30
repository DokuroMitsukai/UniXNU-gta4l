#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Build the opt-in gta4l eMMC diagnostic archive.

The portable core and the XNU probe are compiled with the exact compile record
of bsd_init.o so they use the same XNU headers, target and ABI as the kernel
that calls them. The archive contains no framework or vendor code.

The probe is disabled unless the boot argument "gta4l-emmc-probe=1" is present.
It performs controller reset, GCC 400 kHz setup, the retained-rail power
handshake, eMMC enumeration and two single-block PIO reads. It does not publish
a block device, mount a filesystem, write the card or touch the PMIC.
"""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/gta4l-emmc-probe'
OUT.mkdir(parents=True, exist_ok=True)

# In the assembled build tree the device package is kits/gta4l; in the device
# repository it is package/. Resolve the one that actually carries the core.
DEVICE = ROOT / 'kits' / 'gta4l'
if not (DEVICE / 'Gta4lEmmc.c').is_file():
    DEVICE = ROOT / 'package'

PROBES = [
    {
        'source': DEVICE / 'Gta4lEmmc.c',
        'header': DEVICE / 'Gta4lEmmc.h',
        'object': OUT / 'Gta4lEmmc.o',
        'symbols': [],
    },
    {
        'source': DEVICE / 'Gta4lEmmcProbe.c',
        'header': DEVICE / 'Gta4lEmmc.h',
        'object': OUT / 'Gta4lEmmcProbe.o',
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

if not record_path.is_file():
    # A clean tree has no bsd_init.o compile record yet. Link a no-op first so
    # the bootstrap kernel can be produced; the next kernel build links the
    # real probe with the record from that bootstrap.
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
    archive = OUT / 'libGta4lEmmcProbe.a'
    archive.unlink(missing_ok=True)
    subprocess.run(['xcrun', 'ar', 'rcs', str(archive),
                    str(OUT / 'FirstPassStub.o')], check=True)
    report = {
        'mode': 'bootstrap no-op; rerun the kernel build to link the real probe',
        'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
        'runtime_qualified': False,
    }
    (OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('gta4l eMMC probe: no compile record yet; linked a no-op bootstrap '
          'archive')
    raise SystemExit(0)

record = json.loads(record_path.read_text().strip().rstrip(','))
objects = []
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
    objects.append(probe['object'])
    source_records.append({
        'source': probe['source'].name,
        'source_sha256': hashlib.sha256(probe['source'].read_bytes()).hexdigest(),
        'header': probe['header'].name,
        'header_sha256': hashlib.sha256(probe['header'].read_bytes()).hexdigest(),
        'object_sha256': hashlib.sha256(probe['object'].read_bytes()).hexdigest(),
    })

archive = OUT / 'libGta4lEmmcProbe.a'
archive.unlink(missing_ok=True)
subprocess.run(['ar', 'rcs', str(archive), *[str(obj) for obj in objects]],
               check=True)

report = {
    'sources': source_records,
    'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
    'compile_record': str(record_path),
    'mode': ('opt-in controller reset, GCC 400 kHz, retained-rail power '
             'handshake, eMMC enumeration and two single-block PIO reads; no '
             'block-device publication, filesystem mount, card write, DMA, '
             'CQE, ICE or PMIC access'),
    'runtime_qualified': False,
}
(OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'built {archive} sha256 {report["archive_sha256"]}')
