#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host-only checks for the portable gta4l read-only block request helper.

The test compiles package/Gta4lEmmcBlockCore.c against a mock read/copy
transport, runs every scenario, checks the source policy, and rejects a set
of source mutations that remove one safety check or counter each. It never
touches a device, IOKit or real MMIO.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'package' / 'Gta4lEmmcBlockCore.c'
HEADER = ROOT / 'package' / 'Gta4lEmmcBlockCore.h'
HARNESS = ROOT / 'scripts' / 'sim-gta4l-emmc-block' / 'harness.c'

FORBIDDEN = [
    'gta4l_emmc_read_block',
    'sdhci',
    '#include <IOKit',
    'IOKit/',
    'IOService',
    'IOBlockStorageDevice',
    'kIODirection',
    'IOMemoryDescriptor',
    'malloc(',
    'printf(',
    'memcpy(',
    'memset(',
    'panic(',
    'GTA4L_EMMC_CMD',
]

REQUIRED = [
    'read_block',
    'copy_out',
    'first_fail_valid',
    'capacity_blocks',
    'online',
    'completed_bytes',
    'counted',
    'GTA4L_EMMC_BLOCK_MAX_BLOCKS',
    'GTA4L_EMMC_BLOCK_BYTES',
]

MUTATIONS = [
    ('zero-blocks',
     '\tif (nblks == 0) {\n',
     '\tif (0) {\n',
     'zero'),
    ('out-of-range',
     '\tif (block_number >= (uint64_t)block->capacity_blocks) {\n',
     '\tif (0) {\n',
     'out-of-range'),
    ('range-overflow',
     '\tif ((uint64_t)nblks >\n'
     '\t    (uint64_t)block->capacity_blocks - block_number) {\n',
     '\tif (0) {\n',
     'overflow'),
    ('oversized',
     '\tif (nblks > GTA4L_EMMC_BLOCK_MAX_BLOCKS) {\n',
     '\tif (0) {\n',
     'oversized'),
    ('short-buffer',
     '\tif ((uint64_t)nblks * GTA4L_EMMC_BLOCK_BYTES > buffer_bytes) {\n',
     '\t(void)buffer_bytes;\n'
     '\tif (0) {\n',
     'short-buffer'),
    ('write-rejected',
     '\tif (direction == GTA4L_EMMC_BLOCK_WRITE) {\n',
     '\tif (0) {\n',
     'write'),
    ('invalid-direction',
     '\t\tif (direction == GTA4L_EMMC_BLOCK_WRITE) {\n'
     '\t\t\treturn GTA4L_EMMC_BLOCK_NOT_WRITABLE;\n'
     '\t\t}\n'
     '\t\treturn GTA4L_EMMC_BLOCK_BAD_ARGUMENT;\n'
     '\t}\n',
     '\t\tif (direction == GTA4L_EMMC_BLOCK_WRITE) {\n'
     '\t\t\treturn GTA4L_EMMC_BLOCK_NOT_WRITABLE;\n'
     '\t\t}\n'
     '\t\t/* invalid accepted by mutation */\n'
     '\t}\n',
     'invalid-direction'),
    ('offline-rejected',
     '\tif (block->online == 0) {\n',
     '\tif (0) {\n',
     'offline-init'),
    ('read-failure',
     '\t\tresult = block->io->read_block(block->io->context,\n'
     '\t\t    block_number + index, block->bounce);\n'
     '\t\tif (result != 0) {\n',
     '\t\tresult = block->io->read_block(block->io->context,\n'
     '\t\t    block_number + index, block->bounce);\n'
     '\t\t(void)result;\n'
     '\t\tif (0) {\n',
     'read-fail-first'),
    ('copy-failure',
     '\t\tcopied = block->io->copy_out(block->io->context,\n'
     '\t\t    index * GTA4L_EMMC_BLOCK_BYTES, block->bounce,\n'
     '\t\t    GTA4L_EMMC_BLOCK_BYTES);\n'
     '\t\t/* A callback that claims more than the block length cannot have\n'
     '\t\t * copied more than one block through this interface. Count at\n'
     '\t\t * most one block so completed_bytes never overstates the data\n'
     '\t\t * actually transferred. */\n'
     '\t\tcounted = (copied > GTA4L_EMMC_BLOCK_BYTES) ?\n'
     '\t\t    GTA4L_EMMC_BLOCK_BYTES : copied;\n'
     '\t\tif (completed_bytes != 0) {\n'
     '\t\t\t*completed_bytes += counted;\n'
     '\t\t}\n'
     '\t\tif (copied != GTA4L_EMMC_BLOCK_BYTES) {\n',
     '\t\tcopied = block->io->copy_out(block->io->context,\n'
     '\t\t    index * GTA4L_EMMC_BLOCK_BYTES, block->bounce,\n'
     '\t\t    GTA4L_EMMC_BLOCK_BYTES);\n'
     '\t\t/* A callback that claims more than the block length cannot have\n'
     '\t\t * copied more than one block through this interface. Count at\n'
     '\t\t * most one block so completed_bytes never overstates the data\n'
     '\t\t * actually transferred. */\n'
     '\t\tcounted = (copied > GTA4L_EMMC_BLOCK_BYTES) ?\n'
     '\t\t    GTA4L_EMMC_BLOCK_BYTES : copied;\n'
     '\t\tif (completed_bytes != 0) {\n'
     '\t\t\t*completed_bytes += counted;\n'
     '\t\t}\n'
     '\t\tif (0) {\n',
     'copy-fail'),
    ('overlong-clamp',
     '\t\tcounted = (copied > GTA4L_EMMC_BLOCK_BYTES) ?\n'
     '\t\t    GTA4L_EMMC_BLOCK_BYTES : copied;\n',
     '\t\tcounted = copied;\n',
     'copy-overlong'),
    ('online-clear',
     '\t\t\tblock->online = 0;\n'
     '\t\t\tblock->counters.failures++;\n'
     '\t\t\tblock->counters.last_error = GTA4L_EMMC_BLOCK_IO_ERROR;\n',
     '\t\t\t/* online kept by mutation */\n'
     '\t\t\tblock->counters.failures++;\n'
     '\t\t\tblock->counters.last_error = GTA4L_EMMC_BLOCK_IO_ERROR;\n',
     'read-fail-first'),
    ('first-failure-guard',
     '\t\t\tif (block->counters.first_fail_valid == 0) {\n'
     '\t\t\t\tblock->counters.first_fail_valid = 1;\n'
     '\t\t\t\tblock->counters.first_fail_block =\n'
     '\t\t\t\t    block_number + index;\n'
     '\t\t\t\tblock->counters.first_fail_error = result;\n'
     '\t\t\t}\n'
     '\t\t\treturn GTA4L_EMMC_BLOCK_IO_ERROR;\n',
     '\t\t\tif (1) {\n'
     '\t\t\t\tblock->counters.first_fail_valid = 1;\n'
     '\t\t\t\tblock->counters.first_fail_block =\n'
     '\t\t\t\t    block_number + index;\n'
     '\t\t\t\tblock->counters.first_fail_error = result;\n'
     '\t\t\t}\n'
     '\t\t\treturn GTA4L_EMMC_BLOCK_IO_ERROR;\n',
     'first-failure-preserved'),
    ('completed-reset',
     '\t\t*completed_bytes = 0;\n',
     '\t\t/* completed kept by mutation */\n',
     'write'),
    ('completed-add',
     '\t\t\t*completed_bytes += counted;\n',
     '\t\t\t(void)counted;\n',
     'single'),
    ('requests-counter',
     '\tblock->counters.requests++;\n',
     '\t/* requests kept by mutation */\n',
     'single'),
    ('blocks-counter',
     '\t\tblock->counters.blocks++;\n',
     '\t\t/* blocks kept by mutation */\n',
     'single'),
    ('max-counter',
     '\tif (nblks > block->counters.max_request_blocks) {\n',
     '\tif (0) {\n',
     'max'),
]


def check_source():
    header = HEADER.read_text()
    core = CORE.read_text()
    harness = HARNESS.read_text()

    assert '#define GTA4L_EMMC_BLOCK_MAX_BLOCKS 64u' in header, \
        'missing 64-block limit'
    assert '#define GTA4L_EMMC_BLOCK_MAX_BYTES' in header, \
        'missing maximum-byte limit'
    assert '#include "Gta4lEmmc.h"' in header, \
        'block header must reuse the core block size'
    includes = re.findall(r'^#include\s+[<"]([^>"]+)[>"]', core, re.M)
    assert includes == ['Gta4lEmmcBlockCore.h'], includes
    for token in FORBIDDEN:
        assert token not in core, f'forbidden core token: {token}'
    for token in REQUIRED:
        assert token in core, f'missing core token: {token}'
    assert 'GTA4L_EMMC_BLOCK_MAX_BLOCKS' in harness, \
        'harness must exercise the maximum request'
    assert 'first_fail_valid' in core, 'first-failure preservation missing'


def compile_and_run(compiler, core_path, binary, scenario=None):
    command = [
        compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-I', str(ROOT / 'package'), str(core_path), str(HARNESS),
        '-o', str(binary),
    ]
    built = subprocess.run(command, text=True, capture_output=True)
    if built.returncode != 0:
        return None, built
    invocation = [str(binary)]
    if scenario is not None:
        invocation.append(scenario)
    run = subprocess.run(invocation, text=True, capture_output=True)
    return run, built


def main():
    compiler = shutil.which('gcc') or shutil.which('clang')
    if not compiler:
        print('SKIP: no C compiler on PATH')
        return 0
    check_source()
    with tempfile.TemporaryDirectory(prefix='gta4l-emmc-block-',
                                     dir='/tmp/opencode') as raw:
        temp = Path(raw)
        run, built = compile_and_run(compiler, CORE, temp / 'block-test')
        assert run is not None, \
            'baseline compile failed:\n' + (built.stdout + built.stderr)
        assert run.returncode == 0, \
            'baseline scenarios failed:\n' + (run.stdout + run.stderr)
        assert 'PASS gta4l eMMC block core' in run.stdout, run.stdout
        passes = run.stdout.count('PASS ')
        assert passes == 24, f'expected 24 PASS lines, got {passes}'
        for name, old, new, scenario in MUTATIONS:
            text = CORE.read_text()
            assert text.count(old) == 1, \
                f'mutation {name}: anchor count {text.count(old)}'
            mutated = temp / f'{name}.c'
            mutated.write_text(text.replace(old, new))
            run, built = compile_and_run(compiler, mutated,
                                         temp / f'{name}-test', scenario)
            assert run is not None, \
                f'mutation {name}: compile failed:\n' + (built.stdout + built.stderr)
            assert run.returncode != 0, \
                f'mutation {name} was accepted ({scenario})'
            assert f'FAIL {scenario}' in run.stdout, \
                f'mutation {name} did not fail its scenario {scenario}:\n' + \
                (run.stdout + run.stderr)
            print(f'PASS mutation {name} rejected ({scenario})')
    print(f'PASS gta4l eMMC block core: 23 scenarios, '
          f'{len(MUTATIONS)} mutation controls')
    return 0


if __name__ == '__main__':
    sys.exit(main())
