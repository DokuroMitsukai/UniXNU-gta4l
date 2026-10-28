#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Build the board-only, observation-only gta4l SDHCI snapshot archive.

The source is compiled with the exact compile record of bsd_init.o so it uses
the same XNU headers, target and ABI as the kernel that calls it. The archive
contains only the probe object; no framework or vendor code is linked.
"""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/gta4l-sdhci-snapshot'
OUT.mkdir(parents=True, exist_ok=True)

source = ROOT / 'kits/gta4l/Gta4lSdhciSnapshot.c'
header = ROOT / 'kits/gta4l/Gta4lSdhciSnapshot.h'
record_path = (ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
               / 'bsd/DEVELOPMENT/bsd_init.o.json')
if not source.is_file():
    raise SystemExit(f'missing source: {source}')
if not header.is_file():
    raise SystemExit(f'missing header: {header}')
if not record_path.is_file():
    # A clean tree has no bsd_init.o compile record yet. Link a no-op first so
    # the bootstrap kernel can be produced; the next kernel build links the
    # real probe with the record from that bootstrap.
    stub = OUT / 'FirstPassStub.c'
    stub.write_text('// Bootstrap only: the next kernel build links the real probe.\n'
                    'void gta4l_sdhci_snapshot_run(void);\n'
                    'void gta4l_sdhci_snapshot_run(void)\n{\n}\n')
    subprocess.run(['xcrun', '--sdk', 'iphoneos', 'clang', '-arch', 'arm64',
                    '-mkernel', '-miphoneos-version-min=17.0', '-Os', '-c',
                    str(stub), '-o', str(OUT / 'FirstPassStub.o')], check=True)
    archive = OUT / 'libGta4lSdhciSnapshot.a'
    archive.unlink(missing_ok=True)
    subprocess.run(['xcrun', 'ar', 'rcs', str(archive),
                    str(OUT / 'FirstPassStub.o')], check=True)
    report = {
        'mode': 'bootstrap no-op; rerun the kernel build to link the real probe',
        'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
        'runtime_qualified': False,
    }
    (OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'gta4l SDHCI snapshot: no compile record yet; linked a no-op bootstrap archive')
    raise SystemExit(0)

record = json.loads(record_path.read_text().strip().rstrip(','))
arguments = list(record['arguments'])
try:
    arguments[arguments.index(record['file'])] = str(source)
except ValueError:
    raise SystemExit('compile record has no source path')
try:
    arguments[arguments.index('-o') + 1] = str(OUT / 'Gta4lSdhciSnapshot.o')
except (ValueError, IndexError):
    raise SystemExit('compile record has no output path')
arguments += ['-Wall', '-Wextra', '-Wno-missing-include-dirs', '-Wno-cast-qual',
              '-Wno-unsafe-buffer-usage', '-Wno-shadow-header']

subprocess.run(arguments, cwd=record['directory'], check=True)

obj = OUT / 'Gta4lSdhciSnapshot.o'
archive = OUT / 'libGta4lSdhciSnapshot.a'
archive.unlink(missing_ok=True)
subprocess.run(['ar', 'rcs', str(archive), str(obj)], check=True)

report = {
    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'header_sha256': hashlib.sha256(header.read_bytes()).hexdigest(),
    'object_sha256': hashlib.sha256(obj.read_bytes()).hexdigest(),
    'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
    'compile_record': str(record_path),
    'mode': 'opt-in observation only; no register writes, commands, DMA or storage access',
    'runtime_qualified': False,
}
(OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'built {archive} sha256 {report["archive_sha256"]}')
