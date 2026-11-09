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
POLICY = ROOT / 'package' / 'Gta4lEmmcBlock.c'
POLICY_HEADER = ROOT / 'package' / 'Gta4lEmmcBlock.h'
ADAPTER = ROOT / 'package' / 'Gta4lEmmcBlock.cpp'
SIM = ROOT / 'scripts' / 'sim-gta4l-emmc-block'
ADAPTER_SUPPORT = SIM / 'adapter_support.cpp'
ADAPTER_HARNESS = SIM / 'adapter_harness.cpp'
UNIXNU = ROOT.parent / 'UniXNU'
BUILD_SCRIPT = UNIXNU / 'scripts' / 'build-xnu-4k-baseline.sh'
PERSONALITY_HELPER = UNIXNU / 'scripts' / 'xnu-native-personality.sh'
DEFCONFIG = ROOT / 'configs' / 'gta4l_defconfig'
PATCH_SERIES = ROOT / 'patches' / 'xnu' / 'series'
PERSONALITY_PATCH = ROOT / 'patches' / 'xnu' / 'gta4l-emmc-block.patch'

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
    'read_multi',
    'chunk_buffer',
    'copy_out',
    'first_fail_valid',
    'capacity_blocks',
    'online',
    'completed_bytes',
    'counted',
    'GTA4L_EMMC_BLOCK_MAX_BLOCKS',
    'GTA4L_EMMC_BLOCK_BYTES',
    'GTA4L_EMMC_BLOCK_CHUNK_BLOCKS',
    'multi.stop_attempted',
    'multi.stop_result',
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
     '\t\tif (completed_bytes != 0) {\n'
     '\t\t\t*completed_bytes += counted;\n',
     '\t\tif (completed_bytes != 0) {\n'
     '\t\t\t(void)counted;\n',
     'single'),
    ('requests-counter',
     '\tblock->counters.requests++;\n',
     '\t/* requests kept by mutation */\n',
     'single'),
    ('blocks-counter',
     '\t\tblock->counters.blocks++;\n'
     '\t\tblock->counters.cmd17_reads++;\n',
     '\t\t/* blocks kept by mutation */\n'
     '\t\tblock->counters.cmd17_reads++;\n',
     'single'),
    ('max-counter',
     '\tif (nblks > block->counters.max_request_blocks) {\n',
     '\tif (0) {\n',
     'max'),
    ('chunk-transport',
     '\t\t\tif ((multi.transport_result != 0) ||\n',
     '\t\t\tif ((0) ||\n',
     'chunk-data-result'),
    ('chunk-completed',
     '\t\t\t    (multi.completed_blocks != chunk_blocks) ||\n',
     '\t\t\t    (0) ||\n',
     'chunk-short-count'),
    ('chunk-stop-missing',
     '\t\t\t    (multi.stop_attempted == 0) ||\n',
     '\t\t\t    (0) ||\n',
     'chunk-stop-missing'),
    ('chunk-stop-result',
     '\t\t\t    (multi.stop_attempted == 0) ||\n'
     '\t\t\t    (multi.stop_result != 0)) {\n',
     '\t\t\t    (multi.stop_attempted == 0) ||\n'
     '\t\t\t    (0)) {\n',
     'chunk-stop-fail'),
    ('chunk-online-clear',
     '\tblock->online = 0;\n'
     '\tblock->counters.failures++;\n',
     '\t/* online kept by mutation */\n'
     '\tblock->counters.failures++;\n',
     'chunk-fail-first'),
    ('chunk-bounce-guard',
     '\t\tif ((block->io->chunk_buffer == 0) ||\n'
     '\t\t    (block->io->chunk_buffer_bytes <\n'
     '\t\t    GTA4L_EMMC_BLOCK_CHUNK_BYTES)) {\n'
     '\t\t\treturn GTA4L_EMMC_BLOCK_BAD_ARGUMENT;\n'
     '\t\t}\n',
     '\t\tif (0) {\n'
     '\t\t\treturn GTA4L_EMMC_BLOCK_BAD_ARGUMENT;\n'
     '\t\t}\n',
     'chunk-bounce-missing'),
    ('chunk-stop-counter',
     '\t\t\tif (multi.stop_attempted != 0) {\n'
     '\t\t\t\tblock->counters.stop_attempts++;\n'
     '\t\t\t\tif (multi.stop_result != 0) {\n'
     '\t\t\t\t\tblock->counters.stop_failures++;\n'
     '\t\t\t\t}\n'
     '\t\t\t}\n',
     '\t\t\tif (0) {\n'
     '\t\t\t\tblock->counters.stop_attempts++;\n'
     '\t\t\t\tif (multi.stop_result != 0) {\n'
     '\t\t\t\t\tblock->counters.stop_failures++;\n'
     '\t\t\t\t}\n'
     '\t\t\t}\n',
     'chunk-pair'),
    ('chunk-stop-failure-counter',
     '\t\t\t\tif (multi.stop_result != 0) {\n'
     '\t\t\t\t\tblock->counters.stop_failures++;\n'
     '\t\t\t\t}\n',
     '\t\t\t\t/* stop failure kept by mutation */\n',
     'chunk-stop-fail-return'),
    ('chunk-requested-count',
     '\t\t\t    (multi.requested_blocks != chunk_blocks) ||\n',
     '\t\t\t    (0) ||\n',
     'chunk-requested-mismatch'),
    ('chunk-completed-overclaim',
     '\t\t\t    (multi.completed_blocks != chunk_blocks) ||\n',
     '\t\t\t    (0) ||\n',
     'chunk-completed-overclaim'),
    ('chunk-overlong-clamp',
     '\t\t\tcounted = (copied >\n'
     '\t\t\t    chunk_blocks * GTA4L_EMMC_BLOCK_BYTES) ?\n'
     '\t\t\t    chunk_blocks * GTA4L_EMMC_BLOCK_BYTES : copied;\n',
     '\t\t\tcounted = copied;\n',
     'chunk-overlong-copy'),
    ('chunk-first-failure',
     '\tblock->counters.last_error = result;\n'
     '\tif (block->counters.first_fail_valid == 0) {\n',
     '\tblock->counters.last_error = result;\n'
     '\tif (1) {\n',
     'chunk-first-failure-preserved'),
    ('chunk-cmd17-substitution',
     '\tif ((nblks > 1u) && (block->io->read_multi != 0)) {\n'
     '\t\tuint32_t chunk_index = 0;\n',
     '\tif (0) {\n'
     '\t\tuint32_t chunk_index = 0;\n',
     'chunk-pair'),
    ('chunk-failed-copy',
     '\t\t\t\tblock->counters.chunk_failures++;\n'
     '\t\t\t\treturn gta4l_emmc_block_record_failure(block,\n'
     '\t\t\t\t    block_number + chunk_index,\n'
     '\t\t\t\t    (multi.transport_result != 0) ?',
     '\t\t\t\tblock->counters.chunk_failures++;\n'
     '\t\t\t\t(void)gta4l_emmc_block_record_failure(block,\n'
     '\t\t\t\t    block_number + chunk_index,\n'
     '\t\t\t\t    (multi.transport_result != 0) ?',
     'chunk-stop-fail'),
    ('chunk-suffix-after-error',
     '\t\t\t\tblock->counters.chunk_failures++;\n'
     '\t\t\t\treturn gta4l_emmc_block_record_failure(block,\n'
     '\t\t\t\t    block_number + chunk_index, result,\n'
     '\t\t\t\t    GTA4L_EMMC_BLOCK_IO_ERROR);\n',
     '\t\t\t\tblock->counters.chunk_failures++;\n'
     '\t\t\t\t(void)gta4l_emmc_block_record_failure(block,\n'
     '\t\t\t\t    block_number + chunk_index, result,\n'
     '\t\t\t\t    GTA4L_EMMC_BLOCK_IO_ERROR);\n'
     '\t\t\t\tbreak;\n',
     'chunk-fail-first'),
    ('chunk-tail-bounce',
     '\t\t\t\tresult = block->io->read_block(block->io->context,\n'
     '\t\t\t\t    block_number + chunk_index, block->bounce);\n',
     '\t\t\t\tresult = block->io->read_block(block->io->context,\n'
     '\t\t\t\t    block_number + chunk_index,\n'
     '\t\t\t\t    block->io->chunk_buffer);\n',
     'chunk-split'),
    ('chunk-blocks-counter',
     '\t\t\tblock->counters.blocks += chunk_blocks;\n'
     '\t\t\tblock->counters.cmd18_blocks += chunk_blocks;\n',
     '\t\t\t(void)chunk_blocks;\n',
     'chunk-pair'),
    ('chunk-tail-lba',
     '\t\t\t\tresult = block->io->read_block(block->io->context,\n'
     '\t\t\t\t    block_number + chunk_index, block->bounce);\n',
     '\t\t\t\tresult = block->io->read_block(block->io->context,\n'
     '\t\t\t\t    block_number, block->bounce);\n',
     'chunk-split'),
    ('chunk-copy-offset',
     '\t\t\tcopied = block->io->copy_out(block->io->context,\n'
     '\t\t\t    chunk_index * GTA4L_EMMC_BLOCK_BYTES,\n',
     '\t\t\tcopied = block->io->copy_out(block->io->context,\n'
     '\t\t\t    0,\n',
     'chunk-boundary'),
    ('chunk-fail-counter',
     '\t\t\t\tblock->counters.chunk_failures++;\n'
     '\t\t\t\treturn gta4l_emmc_block_record_failure(block,\n'
     '\t\t\t\t    block_number + chunk_index, result,\n',
     '\t\t\t\treturn gta4l_emmc_block_record_failure(block,\n'
     '\t\t\t\t    block_number + chunk_index, result,\n',
     'chunk-fail-first'),
]

ADAPTER_MUTATIONS = [
    ('double-completion',
     [('\tcompletion->action(completion->target, completion->parameter, '
       'status,\n'
       '\t    completed);\n'
       '\tif (wentOffline) {',
       '\tcompletion->action(completion->target, completion->parameter, '
       'status,\n'
       '\t    completed);\n'
       '\tcompletion->action(completion->target, completion->parameter, '
       'status,\n'
       '\t    completed);\n'
       '\tif (wentOffline) {')],
     'read-one'),
    ('callback-under-lock',
     [('\tIOLockUnlock(gate);\n\n\tswitch (result) {',
       '\tswitch (result) {'),
      ('\tcompletion->action(completion->target, completion->parameter, '
       'status,\n'
       '\t    completed);\n'
       '\tif (wentOffline) {',
       '\tcompletion->action(completion->target, completion->parameter, '
       'status,\n'
       '\t    completed);\n'
       '\tIOLockUnlock(gate);\n'
       '\tif (wentOffline) {')],
     'read-one'),
    ('stale-request-buffer',
     [('\trequestBuffer = nullptr;\n'
       '\tif (result != GTA4L_EMMC_BLOCK_OK) {',
       '\tif (result != GTA4L_EMMC_BLOCK_OK) {')],
     'read-one'),
    ('partobs-no-stability',
     [('\t\tstable = ((count != 0) &&\n'
       '\t\t    (stableMs >= GTA4L_EMMC_BLOCK_PART_STABLE_MS)) ?\n'
       '\t\t    1u : 0u;\n',
       '\t\tstable = (count != 0) ? 1u : 0u;\n')],
     'partobs-incremental'),
    ('partobs-complete-claim',
     [('\treport.part_obs_slots = report.part_expected;\n'
       '\treport.part_obs_complete = 0;\n'
       '\treport.part_error = 0;\n',
       '\treport.part_obs_slots = report.part_expected;\n'
       '\treport.part_obs_complete = 1;\n'
       '\treport.part_error = 0;\n')],
     'partobs-incremental'),
    ('partobs-slots-count',
     [('\treport.part_obs_slots = report.part_expected;\n',
       '\treport.part_obs_slots = report.part_count;\n')],
     'partobs-stable-partial'),
    ('partobs-slots-final',
     [('\t\tprevious = count;\n'
       '\t\tfinal = count;\n',
       '\t\tprevious = count;\n'
       '\t\tfinal = report.part_expected;\n')],
     'partobs-stable-partial'),
    ('partobs-cancel-skip',
     [('\t\tif (reportCancel) {\n'
       '\t\t\treportDone = true;\n'
       '\t\t\treturn;\n'
       '\t\t}\n'
       '\t\tcount = countPartitions();\n',
       '\t\tif (0) {\n'
       '\t\t\treportDone = true;\n'
       '\t\t\treturn;\n'
       '\t\t}\n'
       '\t\tcount = countPartitions();\n')],
     'partobs-cancel'),
    ('partobs-error-skip',
     [('\t\tcount = countPartitions();\n'
       '\t\tsamples++;\n'
       '\t\tif (report.part_error != 0) {\n',
       '\t\tcount = countPartitions();\n'
       '\t\tsamples++;\n'
       '\t\tif (0) {\n')],
     'partobs-error'),
    ('partobs-deadline-ignore',
     [('\t\tif (elapsed >= GTA4L_EMMC_BLOCK_PART_OBSERVE_MS) {\n',
       '\t\tif (samples >= 100u) {\n')],
     'partobs-none'),
    ('partobs-gate-across-sleep',
     [('\t\tIOSleep(GTA4L_EMMC_BLOCK_PART_OBSERVE_STEP_MS);\n',
       '\t\tIOLockLock(gate);\n'
       '\t\tIOSleep(GTA4L_EMMC_BLOCK_PART_OBSERVE_STEP_MS);\n'
       '\t\tIOLockUnlock(gate);\n')],
     'partobs-incremental'),
    ('partobs-media-report',
     [('\t\tcount = countPartitions();\n'
       '\t\tsamples++;\n',
       '\t\tcount = countPartitions();\n'
       '\t\tcount = report.part_reported;\n'
       '\t\tsamples++;\n')],
     'partobs-incremental'),
    ('partcmp-stability-skip',
     [('\t\tstableMs = elapsed - lastChangeMs;\n'
       '\t\tstable = (stableMs >=\n'
       '\t\t    GTA4L_EMMC_BLOCK_PARTMAP_STABLE_MS) ? 1u : 0u;\n',
       '\t\tstableMs = elapsed - lastChangeMs;\n'
       '\t\tstable = 1u;\n')],
     'partcmp-match'),
    ('partcmp-deadline-ignore',
     [('\t\tif (elapsed >= GTA4L_EMMC_BLOCK_PARTMAP_DEADLINE_MS) {\n',
       '\t\tif (samples >= 100u) {\n')],
     'partcmp-zero'),
    ('partcmp-compare-skip',
     [('\t\tmatch = gta4l_emmc_block_partmap_compare(&partmapGpt,\n'
       '\t\t    partmapMedia, count, &missing, &extra, &dup);\n',
       '\t\tmatch = gta4l_emmc_block_partmap_compare(&partmapGpt,\n'
       '\t\t    partmapMedia, count, &missing, &extra, &dup);\n'
       '\t\tmatch = 1;\n')],
     'partcmp-zero'),
    ('partcmp-cancel-skip',
     [('\tfor (;;) {\n'
       '\t\tif (reportCancel) {\n'
       '\t\t\tpartmapReportCancel();\n'
       '\t\t\treturn;\n'
       '\t\t}\n',
       '\tfor (;;) {\n'
       '\t\tif (0) {\n'
       '\t\t\tpartmapReportCancel();\n'
       '\t\t\treturn;\n'
       '\t\t}\n')],
     'partcmp-cancel'),
    ('partcmp-scheme-class-guard',
     [('\t\tif (object->metaCast("IOGUIDPartitionScheme") == nullptr) {\n'
       '\t\t\tcontinue;\n'
       '\t\t}\n',
       '\t\tif (false) {\n'
       '\t\t\tcontinue;\n'
       '\t\t}\n')],
     'partcmp-scheme-wrong-class'),
    ('partcmp-scheme-exactly-one',
     [('\tif (count != 1u) {\n',
       '\tif (count == 0u) {\n')],
     'partcmp-scheme-duplicate'),
    ('partcmp-read-detail-status',
     [('\t\treport.partmap_read_status = (int32_t)status;\n',
       '\t\treport.partmap_read_status = 0;\n')],
     'partcmp-read-failure'),
    ('partcmp-read-detail-actual',
     [('\t\treport.partmap_read_actual = (uint64_t)actual;\n',
       '\t\treport.partmap_read_actual = 0;\n')],
     'partcmp-read-short'),
    ('partcmp-read-detail-desc-len',
     [('\t\treport.partmap_read_desc_len =\n'
       '\t\t    (uint32_t)descriptor->getLength();\n',
       '\t\treport.partmap_read_desc_len =\n'
       '\t\t    0;\n')],
     'partcmp-read-failure'),
    ('partcmp-read-detail-short',
     [('\t\t} else if (actual != requested) {\n'
       '\t\t\treport.partmap_read_state =\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_PARTMAP_READ_SHORT;\n',
       '\t\t} else if (0) {\n'
       '\t\t\treport.partmap_read_state =\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_PARTMAP_READ_SHORT;\n')],
     'partcmp-read-short'),
    ('partcmp-read-detail-flight',
     [('\t\treport.partmap_read_state =\n'
       '\t\t    GTA4L_EMMC_BLOCK_PARTMAP_READ_FLIGHT;\n',
       '\t\treport.partmap_read_state =\n'
       '\t\t    GTA4L_EMMC_BLOCK_PARTMAP_READ_NONE;\n')],
     'partcmp-read-inflight'),
    ('partcmp-bounce-skip',
     [('\t\tdestination = partmapDma;\n',
       '\t\tdestination = buffer;\n')],
     'partcmp-page-split'),
    ('partread-select-refused-accepted',
     [('\tif (partread == GTA4L_EMMC_BLOCK_PARTREAD_REFUSED) {\n',
       '\tif ((partread == GTA4L_EMMC_BLOCK_PARTREAD_REFUSED) && 0) {\n')],
     'partread-refused-value'),
    ('partread-hash-skip',
     [('\t\t\t\t\tgta4l_emmc_block_sha256(\n'
       '\t\t\t\t\t    partreadBuffer, expectedBytes,\n'
       '\t\t\t\t\t    digest);\n'
       '\t\t\t\t\tif (memcmp(digest, item->expected,\n'
       '\t\t\t\t\t    sizeof(digest)) != 0) {\n',
       '\t\t\t\t\tgta4l_emmc_block_sha256(\n'
       '\t\t\t\t\t    partreadBuffer, expectedBytes,\n'
       '\t\t\t\t\t    digest);\n'
       '\t\t\t\t\tif ((memcmp(digest, item->expected,\n'
       '\t\t\t\t\t    sizeof(digest)) != 0) && 0) {\n')],
     'partread-hash-mismatch'),
    ('partread-short-accept',
     [('\t\t\t\t} else if (actual != expectedBytes) {\n',
       '\t\t\t\t} else if ((actual != expectedBytes) && 0) {\n')],
     'partread-short-read'),
    ('partread-read-error-accept',
     [('\t\t\t\tif (status != kIOReturnSuccess) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_PARTREAD_REASON_READ;\n',
       '\t\t\t\tif ((status != kIOReturnSuccess) && 0) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_PARTREAD_REASON_READ;\n')],
     'partread-read-failure'),
    ('partread-budget-skip',
     [('\t\tif (partreadElapsedMs() >=\n'
       '\t\t    GTA4L_EMMC_BLOCK_PARTREAD_READ_BUDGET_MS) {\n',
       '\t\tif ((partreadElapsedMs() >=\n'
       '\t\t    GTA4L_EMMC_BLOCK_PARTREAD_READ_BUDGET_MS) && 0) {\n')],
     'partread-budget'),
    ('partread-cancel-skip',
     [('\t\tbzero(digest, sizeof(digest));\n'
       '\t\tif (reportCancel) {\n'
       '\t\t\tpartreadReportCancel();\n',
       '\t\tbzero(digest, sizeof(digest));\n'
       '\t\tif (reportCancel && 0) {\n'
       '\t\t\tpartreadReportCancel();\n')],
     'partread-cancel'),
    ('partread-complete-claim',
     [('\tcomplete = ((budget == 0) &&\n'
       '\t    (reason == GTA4L_EMMC_BLOCK_PARTREAD_REASON_NONE) &&\n'
       '\t    (failed == 0) && (passed == cases) && (cases != 0u)) ? 1u : 0u;\n',
       '\tcomplete = 1u;\n')],
     'partread-hash-mismatch'),
    ('partread-whole-close-skip',
     [('\tpartreadWholeOpen = true;\n',
       '\tpartreadWholeOpen = false;\n')],
     'partread-match'),
    ('partread-boot-close-skip',
     [('\t\t\t\t\tpartreadBootOpen = true;\n',
       '\t\t\t\t\tpartreadBootOpen = false;\n')],
     'partread-match'),
    ('deblock-select-refused-accepted',
     [('\tif (deblock == GTA4L_EMMC_BLOCK_DEBLOCK_REFUSED) {\n',
       '\tif ((deblock == GTA4L_EMMC_BLOCK_DEBLOCK_REFUSED) && 0) {\n')],
     'deblock-refused-value'),
    ('deblock-hash-skip',
     [('\t\t\t\t\tgta4l_emmc_block_sha256(\n'
       '\t\t\t\t\t    deblockBuffer, length, digest);\n'
       '\t\t\t\t\tif (memcmp(digest, item->expected,\n'
       '\t\t\t\t\t    sizeof(digest)) != 0) {\n',
       '\t\t\t\t\tgta4l_emmc_block_sha256(\n'
       '\t\t\t\t\t    deblockBuffer, length, digest);\n'
       '\t\t\t\t\tif ((memcmp(digest, item->expected,\n'
       '\t\t\t\t\t    sizeof(digest)) != 0) && 0) {\n')],
     'deblock-hash-mismatch'),
    ('deblock-short-accept',
     [('\t\t\t\t} else if (actual != length) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_SHORT;\n',
       '\t\t\t\t} else if ((actual != length) && 0) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_SHORT;\n')],
     'deblock-short-read'),
    ('deblock-read-error-accept',
     [('\t\t\t\tif (status != kIOReturnSuccess) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_READ;\n',
       '\t\t\t\tif ((status != kIOReturnSuccess) && 0) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_READ;\n')],
     'deblock-read-failure'),
    ('deblock-budget-skip',
     [('\t\tif (deblockElapsedMs() >=\n'
       '\t\t    GTA4L_EMMC_BLOCK_DEBLOCK_READ_BUDGET_MS) {\n',
       '\t\tif ((deblockElapsedMs() >=\n'
       '\t\t    GTA4L_EMMC_BLOCK_DEBLOCK_READ_BUDGET_MS) && 0) {\n')],
     'deblock-budget'),
    ('deblock-cancel-skip',
     [('\t\tbzero(digest, sizeof(digest));\n'
       '\t\tif (reportCancel) {\n'
       '\t\t\tdeblockReportCancel();\n',
       '\t\tbzero(digest, sizeof(digest));\n'
       '\t\tif (reportCancel && 0) {\n'
       '\t\t\tdeblockReportCancel();\n')],
     'deblock-cancel'),
    ('deblock-complete-claim',
     [('\treport.deblock_complete = complete;\n',
       '\treport.deblock_complete = complete | 1u;\n')],
     'deblock-hash-mismatch'),
    ('deblock-segment-advertise-1',
     [('\t    GTA4L_EMMC_BLOCK_BREAKUP_SEGMENTS : deblockMode ?\n',
       '\t    GTA4L_EMMC_BLOCK_BREAKUP_SEGMENTS : 0 ?\n')],
     'deblock-segment-property'),
    ('deblock-bounce-skip',
     [('\t\t\tdescriptor = IOMemoryDescriptor::withAddress(\n'
       '\t\t\t    deblockBuffer, length, kIODirectionIn);\n',
       '\t\t\tdescriptor = IOMemoryDescriptor::withAddress(\n'
       '\t\t\t    deblockBuffer + 8, length, kIODirectionIn);\n')],
     'deblock-match'),
    ('deblock-range-accept',
     [('\t\t\tif ((start > limit) ||\n'
       '\t\t\t    ((uint64_t)length > limit - start)) {\n'
       '\t\t\t\tcaseReason =\n'
       '\t\t\t\t    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_ARG;\n',
       '\t\t\tif (((start > limit) ||\n'
       '\t\t\t    ((uint64_t)length > limit - start)) && 0) {\n'
       '\t\t\t\tcaseReason =\n'
       '\t\t\t\t    GTA4L_EMMC_BLOCK_DEBLOCK_REASON_ARG;\n')],
     'deblock-range-invalid'),
    ('deblock-whole-close-skip',
     [('\tdeblockWholeOpen = true;\n',
       '\tdeblockWholeOpen = false;\n')],
     'deblock-match'),
    ('deblock-boot-close-skip',
     [('\t\t\t\t\tdeblockBootOpen = true;\n',
       '\t\t\t\t\tdeblockBootOpen = false;\n')],
     'deblock-match'),
    ('breakup-select-refused-accepted',
     [('\tif (breakup == GTA4L_EMMC_BLOCK_BREAKUP_REFUSED) {\n',
       '\tif ((breakup == GTA4L_EMMC_BLOCK_BREAKUP_REFUSED) && 0) {\n')],
     'breakup-refused-value'),
    ('breakup-hash-skip',
     [('\t\t\t\t\tgta4l_emmc_block_sha256(\n'
       '\t\t\t\t\t    breakupBuffer, length, digest);\n'
       '\t\t\t\t\tif (memcmp(digest, item->expected,\n'
       '\t\t\t\t\t    sizeof(digest)) != 0) {\n',
       '\t\t\t\t\tgta4l_emmc_block_sha256(\n'
       '\t\t\t\t\t    breakupBuffer, length, digest);\n'
       '\t\t\t\t\tif ((memcmp(digest, item->expected,\n'
       '\t\t\t\t\t    sizeof(digest)) != 0) && 0) {\n')],
     'breakup-hash-mismatch'),
    ('breakup-short-accept',
     [('\t\t\t\t} else if (actual != length) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_BREAKUP_REASON_SHORT;\n',
       '\t\t\t\t} else if ((actual != length) && 0) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_BREAKUP_REASON_SHORT;\n')],
     'breakup-short-read'),
    ('breakup-read-error-accept',
     [('\t\t\t\tif (status != kIOReturnSuccess) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_BREAKUP_REASON_READ;\n',
       '\t\t\t\tif ((status != kIOReturnSuccess) && 0) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_BREAKUP_REASON_READ;\n')],
     'breakup-read-failure'),
    ('breakup-budget-skip',
     [('\t\tif (breakupElapsedMs() >=\n'
       '\t\t    GTA4L_EMMC_BLOCK_BREAKUP_READ_BUDGET_MS) {\n',
       '\t\tif ((breakupElapsedMs() >=\n'
       '\t\t    GTA4L_EMMC_BLOCK_BREAKUP_READ_BUDGET_MS) && 0) {\n')],
     'breakup-budget'),
    ('breakup-cancel-skip',
     [('\t\tbzero(digest, sizeof(digest));\n'
       '\t\tif (reportCancel) {\n'
       '\t\t\tbreakupReportCancel();\n',
       '\t\tbzero(digest, sizeof(digest));\n'
       '\t\tif (reportCancel && 0) {\n'
       '\t\t\tbreakupReportCancel();\n')],
     'breakup-cancel'),
    ('breakup-complete-claim',
     [('\treport.breakup_complete = complete;\n',
       '\treport.breakup_complete = complete | 1u;\n')],
     'breakup-hash-mismatch'),
    ('breakup-segment-advertise-1',
     [('\t    GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS : breakupMode ?\n',
       '\t    GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS : 0 ?\n')],
     'breakup-properties'),
    ('breakup-bounce-skip',
     [('\t\t\tdescriptor = IOMemoryDescriptor::withAddress(\n'
       '\t\t\t    breakupBuffer, length, kIODirectionIn);\n',
       '\t\t\tdescriptor = IOMemoryDescriptor::withAddress(\n'
       '\t\t\t    breakupBuffer + 8, length, kIODirectionIn);\n')],
     'breakup-match'),
    ('breakup-order-skip',
     [('\t\t\t\t\t} else if (observedOrder != 0) {\n'
       '\t\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t\t    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ORDER;\n',
       '\t\t\t\t\t} else if ((observedOrder != 0) && 0) {\n'
       '\t\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t\t    GTA4L_EMMC_BLOCK_BREAKUP_REASON_ORDER;\n')],
     'breakup-split-extra'),
    ('breakup-split-count-skip',
     [('\t\t\t\t\t} else if ((observedSubrequests !=\n'
       '\t\t\t\t\t    caseSubrequests) ||\n'
       '\t\t\t\t\t    (observedBlocks != caseBlocks) ||\n'
       '\t\t\t\t\t    (observedMax != caseMax)) {\n',
       '\t\t\t\t\t} else if (0) {\n')],
     'breakup-split-missing'),
    ('breakup-whole-close-skip',
     [('\tbreakupWholeOpen = true;\n',
       '\tbreakupWholeOpen = false;\n')],
     'breakup-match'),
    ('breakup-boot-close-skip',
     [('\t\t\t\t\tbreakupBootOpen = true;\n',
       '\t\t\t\t\tbreakupBootOpen = false;\n')],
     'breakup-match'),
    ('sweep-short-reference-accept',
     [('\t\t\t} else if (refBytes !=\n'
       '\t\t\t    item->blocks * GTA4L_EMMC_BLOCK_BYTES) {\n',
       '\t\t\t} else if ((refBytes !=\n'
       '\t\t\t    item->blocks * GTA4L_EMMC_BLOCK_BYTES) && 0) {\n')],
     'sweep-short-reference'),
    ('sweep-hash-skip',
     [('\t\t\t\t\tif (memcmp(sweepBuffer,\n'
       '\t\t\t\t\t    sweepReference, length) != 0) {\n',
       '\t\t\t\t\tif ((memcmp(sweepBuffer,\n'
       '\t\t\t\t\t    sweepReference, length) != 0) && 0) {\n')],
     'sweep-reference-wrong-lba'),
    ('sweep-anchor-skip',
     [('\t\t\t\t\t\tif (memcmp(digest,\n'
       '\t\t\t\t\t\t    item->expected,\n'
       '\t\t\t\t\t\t    sizeof(digest)) != 0) {\n',
       '\t\t\t\t\t\tif ((memcmp(digest,\n'
       '\t\t\t\t\t\t    item->expected,\n'
       '\t\t\t\t\t\t    sizeof(digest)) != 0) && 0) {\n')],
     'sweep-hash-mismatch'),
    ('sweep-budget-skip',
     [('\t\tif (sweepElapsedMs() >=\n'
       '\t\t    GTA4L_EMMC_BLOCK_SWEEP_READ_BUDGET_MS) {\n',
       '\t\tif ((sweepElapsedMs() >=\n'
       '\t\t    GTA4L_EMMC_BLOCK_SWEEP_READ_BUDGET_MS) && 0) {\n')],
     'sweep-budget'),
    ('sweep-cancel-skip',
     [('\t\tbzero(digest, sizeof(digest));\n'
       '\t\tbzero(refdigest, sizeof(refdigest));\n'
       '\t\tif (reportCancel) {\n'
       '\t\t\tsweepReportCancel();\n',
       '\t\tbzero(digest, sizeof(digest));\n'
       '\t\tbzero(refdigest, sizeof(refdigest));\n'
       '\t\tif (reportCancel && 0) {\n'
       '\t\t\tsweepReportCancel();\n')],
     'sweep-cancel'),
    ('sweep-complete-claim',
     [('\treport.sweep_complete = complete;\n',
       '\treport.sweep_complete = complete | 1u;\n')],
     'sweep-mismatch'),
    ('sweep-start-hardcode',
     [('\t\t\t\t} else {\n'
       '\t\t\t\t\tstart = startBlock *\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_SWEEP_SECTOR_BYTES;\n'
       '\t\t\t\t}\n',
       '\t\t\t\t} else {\n'
       '\t\t\t\t\tstart = 4096u;\n'
       '\t\t\t\t}\n')],
     'sweep-match'),
    ('sweep-bounce-skip',
     [('\t\t\tdescriptor = IOMemoryDescriptor::withAddress(\n'
       '\t\t\t    sweepBuffer, length, kIODirectionIn);\n',
       '\t\t\tdescriptor = IOMemoryDescriptor::withAddress(\n'
       '\t\t\t    sweepBuffer + 8, length, kIODirectionIn);\n')],
     'sweep-match'),
    ('sweep-decomposition-skip',
     [('\t\t\t\t\t\t} else if (\n'
       '\t\t\t\t\t\t    (observedSubrequests !=\n'
       '\t\t\t\t\t\t    item->subrequests) ||\n'
       '\t\t\t\t\t\t    (observedBlocks !=\n'
       '\t\t\t\t\t\t    item->blocks) ||\n'
       '\t\t\t\t\t\t    (observedMax !=\n'
       '\t\t\t\t\t\t    item->max_blocks)) {\n',
       '\t\t\t\t\t\t} else if (0) {\n')],
     'sweep-split-missing'),
    ('sweep-order-skip',
     [('\t\t\t\t\t\tif (observedOrder != 0) {\n'
       '\t\t\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t\t\t    GTA4L_EMMC_BLOCK_SWEEP_REASON_ORDER;\n',
       '\t\t\t\t\t\tif ((observedOrder != 0) && 0) {\n'
       '\t\t\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t\t\t    GTA4L_EMMC_BLOCK_SWEEP_REASON_ORDER;\n')],
     'sweep-order'),
    ('sweep-segment-advertise-3',
     [('\t    (unsigned long long)(sweepMode ?\n'
       '\t    GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS : breakupMode ?\n',
       '\t    (unsigned long long)(sweepMode ?\n'
       '\t    3u : breakupMode ?\n')],
     'sweep-properties'),
    ('sweep-reference-lba-shift',
     [('\t\tresult = gta4l_emmc_read_block(&host, firstBlock + index,\n',
       '\t\tresult = gta4l_emmc_read_block(&host, firstBlock + index + 8u,\n')],
     'sweep-match'),
    ('sweep-short-media-accept',
     [('\t\t\t\t} else if (actual != length) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_SWEEP_REASON_SHORT;\n',
       '\t\t\t\t} else if ((actual != length) && 0) {\n'
       '\t\t\t\t\tcaseReason =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_SWEEP_REASON_SHORT;\n')],
     'sweep-short-read'),
]

SUPPORT_MUTATIONS = [
    ('deblock-head-discard',
     [('\tchunks[chunk_count].length = request_length;\n'
       '\tchunks[chunk_count].span_offset = head;\n',
       '\tchunks[chunk_count].length = request_length;\n'
       '\tchunks[chunk_count].span_offset = 0;\n')],
     'deblock-match'),
    ('deblock-copy-shift',
     [('\tchunks[chunk_count].length = request_length;\n'
       '\tchunks[chunk_count].span_offset = head;\n',
       '\tchunks[chunk_count].length = request_length;\n'
       '\tchunks[chunk_count].span_offset = head + 8u;\n')],
     'deblock-match'),
    ('deblock-tail-into-request',
     [('\t\tchunks[chunk_count].address = scratch;\n'
       '\t\tchunks[chunk_count].physical_address = 0;\n'
       '\t\tchunks[chunk_count].length = tail;\n',
       '\t\tchunks[chunk_count].address =\n'
       '\t\t    static_cast<uint8_t *>(buffer->address());\n'
       '\t\tchunks[chunk_count].physical_address = 0;\n'
       '\t\tchunks[chunk_count].length = tail;\n')],
     'deblock-match'),
]

POLICY_MUTATIONS = [
    ('partobs-value-accepted',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_PARTOBS_REFUSED;\n'
       '\t}\n'
       '\tif (token_value != 1) {\n',
       '\tif ((duplicate_tokens != 0) || (block_selected == 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_PARTOBS_REFUSED;\n'
       '\t}\n'
       '\tif ((token_value != 1) && 0) {\n')],
     'policy'),
    ('partobs-block-required',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0)) {\n',
       '\tif ((duplicate_tokens != 0) || ((block_selected == 0) && 0)) {\n')],
     'policy'),
    ('partobs-width-overflow',
     [('\t\tgta4l_emmc_block_put_string(&writer,\n'
       '\t\t    "GTA4L-EMMC P5 PARTOBS: ");\n',
       '\t\tgta4l_emmc_block_put_string(&writer,\n'
       '\t\t    "' + 'P' * 2100 + '");\n')],
     'policy'),
    ('partcmp-value-accepted',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (partobs_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_PARTCMP_REFUSED;\n'
       '\t}\n'
       '\tif (token_value != 1) {\n',
       '\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (partobs_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_PARTCMP_REFUSED;\n'
       '\t}\n'
       '\tif ((token_value != 1) && 0) {\n')],
     'partcmp-refused-value'),
    ('partcmp-partobs-exclusive',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (partobs_selected != 0)) {\n',
       '\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    ((partobs_selected != 0) && 0)) {\n')],
     'partcmp-refused-partobs'),
    ('partcmp-pmbr-skip',
     [('\tif (gta4l_emmc_block_pmbr_valid(pmbr, capacity_blocks) == 0) {\n',
       '\tif ((gta4l_emmc_block_pmbr_valid(pmbr, capacity_blocks) == 0) '
       '&& 0) {\n')],
     'partcmp-gpt-invalid'),
    ('partcmp-array-crc-skip',
     [('\tif (gta4l_emmc_block_crc32(array, header->slots * 128u) !=\n'
       '\t    header->entry_crc) {\n',
       '\tif ((gta4l_emmc_block_crc32(array, header->slots * 128u) !=\n'
       '\t    header->entry_crc) && 0) {\n')],
     'partcmp-gpt-invalid'),
    ('partcmp-slots-cap-skip',
     [('\tif ((out->slots == 0u) ||\n'
       '\t    (out->slots > GTA4L_EMMC_BLOCK_PARTMAP_MAX_SLOTS)) {\n',
       '\tif ((out->slots == 0u) ||\n'
       '\t    ((out->slots > GTA4L_EMMC_BLOCK_PARTMAP_MAX_SLOTS) && 0)) '
       '{\n')],
     'partcmp-geometry'),
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


def check_build_contract():
    build = BUILD_SCRIPT.read_text()
    helper = PERSONALITY_HELPER.read_text()
    assert 'xnu_gta4l_emmc_block_flag' in helper, \
        'personality helper must expose the flag translation'
    assert 'xnu-native-personality.sh' in build, \
        'kernel build must source the personality helper'
    assert 'GTA4L_EMMC_BLOCK_FLAG=$(xnu_gta4l_emmc_block_flag)' in build, \
        'kernel build must translate the gta4l block personality'
    assert 'PERSONALITY_CFLAGS="$PERSONALITY_CFLAGS $GTA4L_EMMC_BLOCK_FLAG"' \
        in build, 'translated flag must reach PERSONALITY_CFLAGS'

    def flag(value=None):
        script = '. "$1"; '
        if value is not None:
            script += 'XNU_NATIVE_GTA4L_EMMC_BLOCK="$2"; '
        script += 'xnu_gta4l_emmc_block_flag'
        command = ['bash', '-c', script, '--', str(PERSONALITY_HELPER)]
        if value is not None:
            command.append(value)
        return subprocess.run(command, text=True, capture_output=True)

    disabled = flag('0')
    assert disabled.returncode == 0 and disabled.stdout == '', disabled.stderr
    unset = flag()
    assert unset.returncode == 0 and unset.stdout == '', unset.stderr
    enabled = flag('1')
    assert enabled.returncode == 0, enabled.stderr
    assert enabled.stdout.strip() == '-DXNU_NATIVE_GTA4L_EMMC_BLOCK=1', \
        enabled.stdout
    invalid = flag('2')
    assert invalid.returncode != 0, 'invalid personality value accepted'
    assert 'must be 0 or 1' in invalid.stderr, invalid.stderr

    config = DEFCONFIG.read_text()
    assert 'CONFIG_XNU_NATIVE_GTA4L_EMMC_BLOCK=y' in config, \
        'candidate configuration must enable the personality'
    assert 'CONFIG_XNU_BUILD_ARCHIVES=y' in config, \
        'candidate must build the qualified archive before linking'
    assert 'libGta4lEmmcBlock.a' in config, \
        'candidate must force-load the block archive'

    series = [line.strip() for line in PATCH_SERIES.read_text().splitlines()
              if line.strip() and not line.strip().startswith('#')]
    assert 'gta4l-emmc-block.patch' in series, \
        'personality patch missing from the board series'
    assert (series.index('gta4l-emmc-block.patch') >
            series.index('gta4l-emmc-probe.patch')), \
        'personality patch must follow the probe predecessor'
    patch = PERSONALITY_PATCH.read_text()
    assert ('#if defined(XNU_NATIVE_GTA4L_EMMC_BLOCK) && '
            'XNU_NATIVE_GTA4L_EMMC_BLOCK') in patch, \
        'personality patch must stay behind its compile guard'
    assert "'IOClass'         = Gta4lEmmcBlock;" in patch, \
        'personality patch must advertise the adapter class'
    print('PASS build contract: personality translation, guarded patch and '
          'archive configuration')


def check_adapter_source():
    adapter = ADAPTER.read_text()
    policy = POLICY.read_text()
    policy_header = POLICY_HEADER.read_text()

    includes = re.findall(r'^#include\s+[<"]([^>"]+)[>"]', policy, re.M)
    assert includes == ['Gta4lEmmcBlock.h', 'Gta4lEmmcBlockCore.h'], includes
    for token in FORBIDDEN:
        assert token not in policy, f'forbidden policy token: {token}'
    policy_includes = re.findall(r'^#include\s+[<"]([^>"]+)[>"]',
                                 policy_header, re.M)
    assert policy_includes == ['stdint.h'], policy_includes
    assert '#ifdef GTA4L_EMMC_BLOCK_HOST_TEST' in adapter, \
        'adapter must keep the host seam behind its macro'
    assert 'gta4l_emmc_block_report_format' in adapter, \
        'adapter must use the bounded report formatter'
    assert 'gta4l_emmc_block_token_count' in adapter, \
        'adapter must use the shared boot-argument parser'
    assert 'messageClients' in adapter, \
        'adapter must report media-state changes'
    assert 'doAsyncReadWrite' in adapter, \
        'adapter must implement the storage request entry point'
    assert 'media->open(this, 0, kIOStorageAccessReader)' in adapter, \
        'report media must be opened read-only before the media-path read'
    assert 'media->close(this, 0)' in adapter, \
        'report media must be closed after the media-path read'
    assert 'kIOStorageAccessReaderWriter' not in adapter, \
        'report media must never request writer access'
    assert '->write(' not in adapter and 'IOStorage::write' not in adapter, \
        'adapter must not issue media writes'
    watchdog = re.search(r'#define GTA4L_EMMC_BLOCK_WATCHDOG_MS (\d+)u', adapter)
    assert watchdog is not None, 'missing adapter watchdog budget'
    media_wait = re.search(
        r'#define GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS (\d+)u', policy_header)
    observe = re.search(
        r'#define GTA4L_EMMC_BLOCK_PART_OBSERVE_MS (\d+)u', policy_header)
    step = re.search(
        r'#define GTA4L_EMMC_BLOCK_PART_OBSERVE_STEP_MS (\d+)u',
        policy_header)
    stable = re.search(
        r'#define GTA4L_EMMC_BLOCK_PART_STABLE_MS (\d+)u', policy_header)
    assert media_wait is not None, 'missing media-wait budget'
    assert observe is not None, 'missing observation deadline'
    assert step is not None, 'missing observation step'
    assert stable is not None, 'missing stability window'
    assert int(observe.group(1)) == 4000
    assert int(step.group(1)) == 50
    assert int(stable.group(1)) == 250
    assert int(watchdog.group(1)) > (
        int(media_wait.group(1)) + int(observe.group(1))), \
        'watchdog must exceed the media wait plus the observation deadline'
    assert 'GTA4L_EMMC_BLOCK_MEDIA_WAIT_MS' in adapter, \
        'adapter must use the shared media window'
    assert 'GTA4L_EMMC_BLOCK_PARTOBS_ARG' in adapter, \
        'adapter must parse the opt-in observation token'
    assert 'gta4l_emmc_block_partobs_select' in adapter, \
        'adapter must use the shared observation selector'
    assert 'runPartitionObservation' in adapter, \
        'adapter must implement the bounded observation'
    assert 'report.part_obs_complete = 0' in adapter, \
        'observation must keep the explicit non-claim'
    assert 'report.part_obs_slots = report.part_expected' in adapter, \
        'observation must report the GPT slot count separately'
    assert 'reportCancel' in adapter and 'countPartitions()' in adapter, \
        'observation must keep cancellation and the registry walk'
    assert 'whole->getChildIterator(gIOServicePlane)' in adapter, \
        'scheme lookup must enumerate attached service-plane children'
    assert 'object->metaCast("IOGUIDPartitionScheme")' in adapter, \
        'scheme lookup must keep the partition-scheme class guard'
    assert adapter.count('getMatchingServices(matching)') == 5, \
        'only whole-media, partition-media and pinned-range walks may use matching'
    assert '#define GTA4L_EMMC_BLOCK_PARTREAD_MAX_BLOCKS 64u' in \
        policy_header, 'pinned-range reads must stay inside the request limit'
    assert 'GTA4L_EMMC_BLOCK_PARTREAD_REASON_HASH 7u' in policy_header, \
        'pinned-range digest mismatch must have a distinct reason'
    assert 'GTA4L_EMMC_BLOCK_PARTREAD_BOOT_BASE_BYTES' in policy_header, \
        'pinned-range boot geometry must be explicit'
    assert 'GTA4L_EMMC_BLOCK_PARTREAD_ARG' in adapter, \
        'adapter must parse the pinned-range token'
    assert 'gta4l_emmc_block_partread_select' in adapter, \
        'adapter must use the shared pinned-range selector'
    assert 'runPartitionRead' in adapter, \
        'adapter must implement the pinned-range qualification'
    assert 'gta4l_emmc_block_sha256' in adapter, \
        'adapter must use the portable pinned-content oracle'
    assert 'partreadBuffer' in adapter and \
        'IOFreeAligned(partreadBuffer' in adapter, \
        'pinned-range scratch must be allocated and freed'
    assert 'report.partread_complete = 0' in adapter, \
        'pinned-range qualification must start as an explicit non-claim'
    assert 'partreadElapsedMs() >=' in adapter, \
        'pinned-range qualification must enforce the read budget'
    assert 'static_assert(GTA4L_EMMC_BLOCK_PARTREAD_WORST_MS <' in adapter, \
        'pinned-range worst case must stay below the watchdog'
    assert 'static_assert(GTA4L_EMMC_BLOCK_PARTREAD_MAX_BYTES <=' in \
        adapter, 'pinned-range case must stay inside the request limit'
    assert '#define GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BLOCKS 66u' in \
        policy_header, 'deblocked reads must stay inside the request limit'
    assert 'GTA4L_EMMC_BLOCK_DEBLOCK_REASON_HASH 7u' in policy_header, \
        'deblocked digest mismatch must have a distinct reason'
    assert 'GTA4L_EMMC_BLOCK_DEBLOCK_SEGMENTS 3u' in policy_header, \
        'deblocked reads must advertise the multi-segment path'
    assert 'GTA4L_EMMC_BLOCK_DEBLOCK_ARG' in adapter, \
        'adapter must parse the deblock token'
    assert 'gta4l_emmc_block_deblock_select' in adapter, \
        'adapter must use the shared deblock selector'
    assert 'runDeblock' in adapter, \
        'adapter must implement the deblock qualification'
    assert 'deblockBuffer' in adapter and \
        'IOFreeAligned(deblockBuffer' in adapter, \
        'deblock scratch must be allocated and freed'
    assert 'report.deblock_complete = 0' in adapter, \
        'deblock qualification must start as an explicit non-claim'
    assert 'deblockElapsedMs() >=' in adapter, \
        'deblock qualification must enforce the read budget'
    assert 'static_assert(GTA4L_EMMC_BLOCK_DEBLOCK_WORST_MS <' in adapter, \
        'deblock worst case must stay below the watchdog'
    assert 'static_assert(GTA4L_EMMC_BLOCK_DEBLOCK_MAX_BYTES >=' in \
        adapter, 'deblock bounce must cover the aligned span'
    assert '#define GTA4L_EMMC_BLOCK_BREAKUP_MAX_BLOCKS 128u' in \
        policy_header, 'breakup spans must stay inside the request limit'
    assert 'GTA4L_EMMC_BLOCK_BREAKUP_REASON_HASH 7u' in policy_header, \
        'breakup digest mismatch must have a distinct reason'
    assert 'GTA4L_EMMC_BLOCK_BREAKUP_SEGMENTS 3u' in policy_header, \
        'breakup reads must advertise the multi-segment path'
    assert 'GTA4L_EMMC_BLOCK_BREAKUP_ARG' in adapter, \
        'adapter must parse the breakup token'
    assert 'gta4l_emmc_block_breakup_select' in adapter, \
        'adapter must use the shared breakup selector'
    assert 'runBreakup' in adapter, \
        'adapter must implement the breakup qualification'
    assert 'breakupBuffer' in adapter and \
        'IOFreeAligned(breakupBuffer' in adapter, \
        'breakup scratch must be allocated and freed'
    assert 'report.breakup_complete = 0' in adapter, \
        'breakup qualification must start as an explicit non-claim'
    assert 'breakupElapsedMs() >=' in adapter, \
        'breakup qualification must enforce the read budget'
    assert 'static_assert(GTA4L_EMMC_BLOCK_BREAKUP_WORST_MS <' in adapter, \
        'breakup worst case must stay below the watchdog'
    assert 'static_assert(GTA4L_EMMC_BLOCK_BREAKUP_MAX_BYTES >=' in \
        adapter, 'breakup bounce must cover the aligned span'
    assert 'breakupObserve(buffer, blockNumber, nblks)' in adapter, \
        'adapter must observe the actual subrequest decomposition'
    assert '#define GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS 8u' in \
        policy_header, 'sweep subrequests must stay inside the request limit'
    assert 'GTA4L_EMMC_BLOCK_SWEEP_REASON_MISMATCH 14u' in policy_header, \
        'sweep reference mismatch must have a distinct reason'
    assert 'GTA4L_EMMC_BLOCK_SWEEP_REASON_REF 15u' in policy_header, \
        'sweep reference failure must have a distinct reason'
    assert 'GTA4L_EMMC_BLOCK_SWEEP_SEGMENTS 1u' in policy_header, \
        'sweep reads must advertise the single-segment path'
    assert 'GTA4L_EMMC_BLOCK_SWEEP_ARG' in adapter, \
        'adapter must parse the sweep token'
    assert 'gta4l_emmc_block_sweep_select' in adapter, \
        'adapter must use the shared sweep selector'
    assert 'runSweep' in adapter, \
        'adapter must implement the bounded read sweep'
    assert 'sweepBuffer' in adapter and \
        'IOFreeAligned(sweepBuffer' in adapter, \
        'sweep media scratch must be allocated and freed'
    assert 'sweepReference' in adapter and \
        'IOFreeAligned(sweepReference' in adapter, \
        'sweep reference scratch must be allocated and freed'
    assert 'report.sweep_complete = 0' in adapter, \
        'sweep qualification must start as an explicit non-claim'
    assert 'sweepElapsedMs() >=' in adapter, \
        'sweep qualification must enforce the read budget'
    assert 'static_assert(GTA4L_EMMC_BLOCK_SWEEP_WORST_MS <' in adapter, \
        'sweep worst case must stay below the watchdog'
    assert 'static_assert(GTA4L_EMMC_BLOCK_SWEEP_MAX_BYTES == 16384u' in \
        adapter, 'sweep scratch must cover the aligned span'
    assert 'sweepObserve(buffer, blockNumber, nblks)' in adapter, \
        'adapter must observe the actual sweep subrequest decomposition'


def compile_adapter(compiler, temp):
    cxx = shutil.which('g++') or shutil.which('clang++')
    if not cxx:
        return None, None, None
    common = ['-std=c11', '-Wall', '-Wextra', '-Werror',
              '-DGTA4L_EMMC_BLOCK_HOST_TEST',
              '-I', str(ROOT / 'package')]
    policy_objects = []
    for source in (POLICY, CORE):
        target = temp / (source.stem + '.o')
        built = subprocess.run([compiler] + common + ['-c', str(source),
                               '-o', str(target)], text=True,
                               capture_output=True)
        if built.returncode != 0:
            return None, built, None
        policy_objects.append(str(target))
    cxx_common = ['-std=c++17', '-Wall', '-Wextra', '-Werror',
                  '-DGTA4L_EMMC_BLOCK_HOST_TEST',
                  '-I', str(ROOT / 'package'), '-I', str(SIM)]
    objects = list(policy_objects)
    for source in (ADAPTER_SUPPORT, ADAPTER_HARNESS):
        target = temp / (source.stem + '.o')
        built = subprocess.run([cxx] + cxx_common + ['-c', str(source),
                               '-o', str(target)], text=True,
                               capture_output=True)
        if built.returncode != 0:
            return None, built, None
        objects.append(str(target))
    binary = temp / 'adapter-test'
    linked = subprocess.run([cxx] + objects + ['-o', str(binary)],
                            text=True, capture_output=True)
    if linked.returncode != 0:
        return None, linked, None
    return (subprocess.run([str(binary)], text=True, capture_output=True),
            linked, policy_objects)


def compile_adapter_mutation(cxx, temp, name, replacements, scenario, objects,
                             support_replacements=None):
    mirror = temp / f'adapter-{name}'
    sim = mirror / 'scripts' / 'sim-gta4l-emmc-block'
    package = mirror / 'package'
    sim.mkdir(parents=True)
    package.mkdir(parents=True)
    adapter = ADAPTER.read_text()
    for old, new in replacements:
        assert adapter.count(old) == 1, \
            f'adapter mutation {name}: anchor count {adapter.count(old)}'
        adapter = adapter.replace(old, new)
    (package / ADAPTER.name).write_text(adapter)
    support = ADAPTER_SUPPORT.read_text()
    for old, new in (support_replacements or []):
        assert support.count(old) == 1, \
            f'adapter mutation {name}: support anchor count ' \
            f'{support.count(old)}'
        support = support.replace(old, new)
    (sim / ADAPTER_SUPPORT.name).write_text(support)
    shutil.copy2(ADAPTER_HARNESS, sim / ADAPTER_HARNESS.name)
    shutil.copy2(SIM / 'Gta4lEmmcBlockTestSupport.h',
                 sim / 'Gta4lEmmcBlockTestSupport.h')
    cxx_common = ['-std=c++17', '-Wall', '-Wextra', '-Werror',
                  '-DGTA4L_EMMC_BLOCK_HOST_TEST',
                  '-I', str(ROOT / 'package'), '-I', str(sim)]
    local = list(objects)
    for source in (sim / ADAPTER_SUPPORT.name, sim / ADAPTER_HARNESS.name):
        target = temp / f'{name}-{source.stem}.o'
        built = subprocess.run([cxx] + cxx_common + ['-c', str(source),
                               '-o', str(target)], text=True,
                               capture_output=True)
        if built.returncode != 0:
            return None, built
        local.append(str(target))
    binary = temp / f'adapter-{name}-test'
    linked = subprocess.run([cxx] + local + ['-o', str(binary)],
                            text=True, capture_output=True)
    if linked.returncode != 0:
        return None, linked
    return subprocess.run([str(binary), scenario], text=True,
                          capture_output=True), linked


def compile_policy_mutation(compiler, cxx, temp, name, replacements,
                            scenario):
    mirror = temp / f'policy-{name}'
    package = mirror / 'package'
    package.mkdir(parents=True)
    policy = POLICY.read_text()
    for old, new in replacements:
        assert policy.count(old) == 1, \
            f'policy mutation {name}: anchor count {policy.count(old)}'
        policy = policy.replace(old, new)
    (package / POLICY.name).write_text(policy)
    common = ['-std=c11', '-Wall', '-Wextra', '-Werror',
              '-DGTA4L_EMMC_BLOCK_HOST_TEST',
              '-I', str(ROOT / 'package')]
    objects = []
    for source in (package / POLICY.name, CORE):
        target = temp / f'policy-{name}-{source.stem}.o'
        built = subprocess.run([compiler] + common + ['-c', str(source),
                               '-o', str(target)], text=True,
                               capture_output=True)
        if built.returncode != 0:
            return None, built
        objects.append(str(target))
    cxx_common = ['-std=c++17', '-Wall', '-Wextra', '-Werror',
                  '-DGTA4L_EMMC_BLOCK_HOST_TEST',
                  '-I', str(ROOT / 'package'), '-I', str(SIM)]
    for source in (ADAPTER_SUPPORT, ADAPTER_HARNESS):
        target = temp / f'policy-{name}-{source.stem}.o'
        built = subprocess.run([cxx] + cxx_common + ['-c', str(source),
                               '-o', str(target)], text=True,
                               capture_output=True)
        if built.returncode != 0:
            return None, built
        objects.append(str(target))
    binary = temp / f'policy-{name}-test'
    linked = subprocess.run([cxx] + objects + ['-o', str(binary)],
                            text=True, capture_output=True)
    if linked.returncode != 0:
        return None, linked
    return subprocess.run([str(binary), scenario], text=True,
                          capture_output=True), linked


def main():
    compiler = shutil.which('gcc') or shutil.which('clang')
    if not compiler:
        print('SKIP: no C compiler on PATH')
        return 0
    check_source()
    check_build_contract()
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
        assert passes == 49, f'expected 49 PASS lines, got {passes}'
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
        check_adapter_source()
        run, built, policy_objects = compile_adapter(compiler, temp)
        if run is None:
            if built is None:
                print('SKIP: no C++ compiler on PATH')
                return 0
            assert False, \
                'adapter compile failed:\n' + (built.stdout + built.stderr)
        assert run.returncode == 0, \
            'adapter scenarios failed:\n' + (run.stdout + run.stderr)
        assert 'PASS gta4l eMMC block adapter' in run.stdout, run.stdout
        passes = run.stdout.count('PASS ')
        assert passes == 219, f'expected 219 adapter PASS lines, got {passes}'
        print('PASS adapter: 218 scenarios against the host IOKit seam')
        cxx = shutil.which('g++') or shutil.which('clang++')
        for name, replacements, scenario in ADAPTER_MUTATIONS:
            run, built = compile_adapter_mutation(
                cxx, temp, name, replacements, scenario, policy_objects)
            assert run is not None, \
                f'adapter mutation {name}: compile failed:\n' + \
                (built.stdout + built.stderr)
            assert run.returncode != 0, \
                f'adapter mutation {name} was accepted ({scenario})'
            assert f'FAIL {scenario}' in run.stdout, \
                f'adapter mutation {name} did not fail {scenario}:\n' + \
                (run.stdout + run.stderr)
            print(f'PASS adapter mutation {name} rejected ({scenario})')
        for name, replacements, scenario in SUPPORT_MUTATIONS:
            run, built = compile_adapter_mutation(
                cxx, temp, name, [], scenario, policy_objects,
                support_replacements=replacements)
            assert run is not None, \
                f'adapter mutation {name}: compile failed:\n' + \
                (built.stdout + built.stderr)
            assert run.returncode != 0, \
                f'adapter mutation {name} was accepted ({scenario})'
            assert f'FAIL {scenario}' in run.stdout, \
                f'adapter mutation {name} did not fail {scenario}:\n' + \
                (run.stdout + run.stderr)
            print(f'PASS adapter mutation {name} rejected ({scenario})')
        for name, replacements, scenario in POLICY_MUTATIONS:
            run, built = compile_policy_mutation(
                compiler, cxx, temp, name, replacements, scenario)
            assert run is not None, \
                f'policy mutation {name}: compile failed:\n' + \
                (built.stdout + built.stderr)
            assert run.returncode != 0, \
                f'policy mutation {name} was accepted ({scenario})'
            assert f'FAIL {scenario}' in run.stdout, \
                f'policy mutation {name} did not fail {scenario}:\n' + \
                (run.stdout + run.stderr)
            print(f'PASS policy mutation {name} rejected ({scenario})')
    print(f'PASS gta4l eMMC block core: 48 scenarios, '
          f'{len(MUTATIONS)} mutation controls')
    return 0


if __name__ == '__main__':
    sys.exit(main())
