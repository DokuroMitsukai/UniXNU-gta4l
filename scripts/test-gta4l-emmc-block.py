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
CONSUMER_PROBE = SIM / 'consumer_lifecycle_probe.cpp'
BUILD = ROOT / 'scripts' / 'build-gta4l-emmc-block.py'
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
    ('chunk-blocks-counter',
     '\t\t\tblock->counters.blocks += chunk_blocks;\n'
     '\t\t\tblock->counters.cmd18_blocks += chunk_blocks;\n',
     '\t\t\t(void)chunk_blocks;\n',
     'chunk-pair'),
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
     [('\tIOLockUnlock(gate);\n\n\tif (consumeAttributed &&',
       '\tif (consumeAttributed &&'),
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
    ('consume-open-drop',
     [('\tif (!consumeWhole->open(this, 0, kIOStorageAccessReader)) {\n'
       '\t\tconsumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_OPEN);\n'
       '\t\treturn;\n'
       '\t}\n'
       '\tconsumeWholeOpen = true;\n',
       '\tif (false) {\n'
       '\t\tconsumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_OPEN);\n'
       '\t\treturn;\n'
       '\t}\n'
       '\tconsumeWholeOpen = true;\n')],
     'consume-match'),
    ('consume-close-drop',
     [('\tconsumeBootOpen = true;\n',
       '\tconsumeBootOpen = false;\n')],
     'consume-match'),
    ('consume-client-release-drop',
     [('\tif (consumeClient != nullptr) {\n'
       '\t\tconsumeClient->release();\n'
       '\t\tconsumeClient = nullptr;\n'
       '\t}\n'
       '\treleaseConsumeMedia();\n',
       '\tif (consumeClient != nullptr) {\n'
       '\t\tconsumeClient = nullptr;\n'
       '\t}\n'
       '\treleaseConsumeMedia();\n')],
     'consume-match'),
    ('consume-double-close',
     [('\tif (consumeBootOpen && (consumeBoot != nullptr)) {\n'
       '\t\tconsumeBoot->close(consumeClient, 0);\n'
       '\t\tconsumeBootOpen = false;\n'
       '\t\tIOLockLock(reportLock);\n'
       '\t\treport.consume_close = 1;\n'
       '\t\tIOLockUnlock(reportLock);\n'
       '\t}\n'
       '\tif (consumeClient != nullptr) {\n'
       '\t\tconsumeClient->release();\n'
       '\t\tconsumeClient = nullptr;\n'
       '\t}\n'
       '\treleaseConsumeMedia();\n',
       '\tif (consumeBootOpen && (consumeBoot != nullptr)) {\n'
       '\t\tconsumeBoot->close(consumeClient, 0);\n'
       '\t\tconsumeBoot->close(consumeClient, 0);\n'
       '\t\tconsumeBootOpen = false;\n'
       '\t\tIOLockLock(reportLock);\n'
       '\t\treport.consume_close = 1;\n'
       '\t\tIOLockUnlock(reportLock);\n'
       '\t}\n'
       '\tif (consumeClient != nullptr) {\n'
       '\t\tconsumeClient->release();\n'
       '\t\tconsumeClient = nullptr;\n'
       '\t}\n'
       '\treleaseConsumeMedia();\n')],
     'consume-match'),
    ('consume-alloc-check',
     [('\tbuffer = (uint8_t *)IOMallocContiguous(\n'
       '\t    GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES, PAGE_SIZE, nullptr);\n'
       '\tif (buffer == nullptr) {\n'
       '\t\tresult->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_ALLOC;\n',
       '\tbuffer = (uint8_t *)IOMallocContiguous(\n'
       '\t    GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES, PAGE_SIZE, nullptr);\n'
       '\tif (false) {\n'
       '\t\tresult->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_ALLOC;\n')],
     'consume-alloc-failure'),
    ('consume-segment-length',
     [('\tif ((address == 0) || (segmentLength < (IOByteCount)length)) {\n'
       '\t\tdescriptor->release();\n'
       '\t\tIOFreeContiguous(buffer,\n'
       '\t\t    GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES);\n',
       '\tif (address == 0) {\n'
       '\t\tdescriptor->release();\n'
       '\t\tIOFreeContiguous(buffer,\n'
       '\t\t    GTA4L_EMMC_BLOCK_CONSUME_BUFFER_BYTES);\n')],
     'consume-descriptor-failure'),
    ('consume-tag-omit',
     [('\t\tresult->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_DESC;\n'
       '\t\treturn false;\n'
       '\t}\n'
       '\tdescriptor->setTag(tag);\n',
       '\t\tresult->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_DESC;\n'
       '\t\treturn false;\n'
       '\t}\n'
       '\t(void)tag;\n')],
     'consume-match'),
    ('consume-tag-late',
     [('\t\tresult->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_DESC;\n'
       '\t\treturn false;\n'
       '\t}\n'
       '\tdescriptor->setTag(tag);\n'
       '\tresult->attempted = 1;\n'
       '\tstatus = boot->read(this, start, descriptor, nullptr, &actual);\n'
       '\tdescriptor->release();\n',
       '\t\tresult->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_DESC;\n'
       '\t\treturn false;\n'
       '\t}\n'
       '\tresult->attempted = 1;\n'
       '\tstatus = boot->read(this, start, descriptor, nullptr, &actual);\n'
       '\tdescriptor->release();\n'
       '\tdescriptor->setTag(tag);\n')],
     'consume-match'),
    ('consume-descriptor-release-drop',
     [('\tstatus = boot->read(this, start, descriptor, nullptr, &actual);\n'
       '\tdescriptor->release();\n'
       '\tresult->status = (int32_t)status;\n'
       '\tresult->actual = actual;\n'
       '\tif (status != kIOReturnSuccess) {\n'
       '\t\tresult->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_READ;\n',
       '\tstatus = boot->read(this, start, descriptor, nullptr, &actual);\n'
       '\tresult->status = (int32_t)status;\n'
       '\tresult->actual = actual;\n'
       '\tif (status != kIOReturnSuccess) {\n'
       '\t\tresult->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_READ;\n')],
     'consume-match'),
    ('consume-digest-omit',
     [('\t\tgta4l_emmc_block_sha256(buffer, length, result->digest);\n',
       '\t\tbzero(result->digest, sizeof(result->digest));\n')],
     'consume-match'),
    ('consume-short-accept',
     [('\t} else if (actual != (UInt64)length) {\n'
       '\t\tresult->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_SHORT;\n',
       '\t} else if (false) {\n'
       '\t\tresult->reason = GTA4L_EMMC_BLOCK_CONSUME_REASON_SHORT;\n')],
     'consume-short-read'),
    ('consume-foreign-skip',
     [('\tif ((reason == GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE) &&\n'
       '\t    (observedForeign != 0u)) {\n'
       '\t\treason = GTA4L_EMMC_BLOCK_CONSUME_REASON_FOREIGN;\n'
       '\t}\n',
       '\tif (false) {\n'
       '\t\treason = GTA4L_EMMC_BLOCK_CONSUME_REASON_FOREIGN;\n'
       '\t}\n')],
     'consume-foreign'),
    ('consume-order-skip',
     [('\tif ((reason == GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE) &&\n'
       '\t    ((observedOrder != 0u) || (observedSubrequests != 1u))) {\n'
       '\t\treason = GTA4L_EMMC_BLOCK_CONSUME_REASON_ORDER;\n'
       '\t}\n',
       '\tif ((reason == GTA4L_EMMC_BLOCK_CONSUME_REASON_NONE) &&\n'
       '\t    ((observedOrder != 0u) || (observedSubrequests != 1u)) &&\n'
       '\t    0) {\n'
       '\t\treason = GTA4L_EMMC_BLOCK_CONSUME_REASON_ORDER;\n'
       '\t}\n')],
     'consume-order'),
    ('consume-budget-skip',
     [('\tif (consumeElapsedMs() >=\n'
       '\t    GTA4L_EMMC_BLOCK_CONSUME_READ_MS) {\n'
       '\t\tconsumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_BUDGET);\n',
       '\tif (false) {\n'
       '\t\tconsumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_BUDGET);\n')],
     'consume-budget'),
    ('consume-cancel-skip',
     [('\tif (reportCancel) {\n'
       '\t\tconsumeReportCancel();\n'
       '\t\treturn;\n'
       '\t}\n'
       '\n'
       '\tif (consumeBootOpen && (consumeBoot != nullptr)) {\n',
       '\tif (false) {\n'
       '\t\tconsumeReportCancel();\n'
       '\t\treturn;\n'
       '\t}\n'
       '\n'
       '\tif (consumeBootOpen && (consumeBoot != nullptr)) {\n')],
     'consume-cancel'),
    ('consume-complete-claim',
     [('\treport.consume_complete = complete;\n',
       '\treport.consume_complete = complete ? 1 : 1;\n')],
     'consume-read-failure'),
    ('consume-offset-shift',
     [('\t(void)consumeClient->run(consumeBoot, 0,\n'
       '\t    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES,\n',
       '\t(void)consumeClient->run(consumeBoot, 512,\n'
       '\t    GTA4L_EMMC_BLOCK_CONSUME_CASE_BYTES,\n')],
     'consume-match'),
    ('consume-base-skip',
     [('\t\tif (baseProperty->unsigned64BitValue() != base) {\n'
       '\t\t\tcontinue;\n'
       '\t\t}\n',
       '\t\tif ((baseProperty->unsigned64BitValue() != base) && 0) {\n'
       '\t\t\tcontinue;\n'
       '\t\t}\n')],
     'consume-part-wrong-base'),
    ('consume-writable-skip',
     [('\tif ((bootWritableProperty == nullptr) ||\n'
       '\t    bootWritableProperty->getValue()) {\n'
       '\t\tconsumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_PART);\n',
       '\tif (((bootWritableProperty == nullptr) ||\n'
       '\t    bootWritableProperty->getValue()) && 0) {\n'
       '\t\tconsumeReportFailure(GTA4L_EMMC_BLOCK_CONSUME_REASON_PART);\n')],
     'consume-part-writable'),
    ('consume-advertise-8',
     [('\t    GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS :\n'
       '\t    GTA4L_EMMC_BLOCK_MAX_BLOCKS), 32)) {\n',
       '\t    GTA4L_EMMC_BLOCK_BREAKUP_READ_BLOCKS :\n'
       '\t    GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS), 32)) {\n')],
     'consume-match'),
    ('consume-digest-report-omit',
     [('\tmemcpy(report.consume_last_digest, result.digest,\n'
       '\t    sizeof(report.consume_last_digest));\n',
       '\tbzero(report.consume_last_digest,\n'
       '\t    sizeof(report.consume_last_digest));\n')],
     'consume-match'),
    ('width-switch-guard-skip',
     [('\t\tif ((widthRc != GTA4L_EMMC_OK) ||\n'
       '\t\t    (widthSwitch.verified != 1u) ||\n'
       '\t\t    (widthSwitch.switch_result != GTA4L_EMMC_OK) ||\n'
       '\t\t    ((widthSwitch.host_after &\n'
       '\t\t    GTA4L_EMMC_HOST_CONTROL_4BIT) !=\n'
       '\t\t    GTA4L_EMMC_HOST_CONTROL_4BIT) ||\n'
       '\t\t    (host.bus_width !=\n'
       '\t\t    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_4)) {\n',
       '\t\tif ((widthRc != GTA4L_EMMC_OK) &&\n'
       '\t\t    (widthSwitch.verified == 2u)) {\n')],
     'width-switch-failure'),
    ('width-complete-claim',
     [('\treport.width_complete = complete;\n',
       '\treport.width_complete = complete | 1u;\n')],
     'width-match-mismatch'),
    ('width-match-skip',
     [('\tif (readOk && (memcmp(widthReference, widthObserved,\n'
       '\t    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES) == 0)) {\n',
       '\tif (readOk) {\n')],
     'width-match-mismatch'),
    ('width-revert-skip',
     [('\trevertOk = widthAttemptRevert();\n',
       '\trevertOk = true;\n')],
     'width-match'),
    ('width-read-credit',
     [('\treport.width_fails = (readOk) ? 0u : 1u;\n',
       '\treport.width_fails = 0u;\n')],
     'width-read-failure'),
    ('width-limit-advertise',
     [('\treport.width_limit = GTA4L_EMMC_BLOCK_MAX_BLOCKS;\n'
       '\treport.width_segments = 1u;\n'
       '\treport.width_capacity = capacityBlocks;\n'
       '\tIOLockUnlock(reportLock);\n'
       '\n'
       '\tif (reportCancel) {\n',
       '\treport.width_limit = GTA4L_EMMC_BLOCK_SWEEP_READ_BLOCKS;\n'
       '\treport.width_segments = 1u;\n'
       '\treport.width_capacity = capacityBlocks;\n'
       '\tIOLockUnlock(reportLock);\n'
       '\n'
       '\tif (reportCancel) {\n')],
     'width-match'),
    ('width-segment-advertise',
     [('\treport.width_limit = GTA4L_EMMC_BLOCK_MAX_BLOCKS;\n'
       '\treport.width_segments = 1u;\n'
       '\treport.width_capacity = capacityBlocks;\n'
       '\tIOLockUnlock(reportLock);\n'
       '\n'
       '\tif (reportCancel) {\n',
       '\treport.width_limit = GTA4L_EMMC_BLOCK_MAX_BLOCKS;\n'
       '\treport.width_segments = 3u;\n'
       '\treport.width_capacity = capacityBlocks;\n'
       '\tIOLockUnlock(reportLock);\n'
       '\n'
       '\tif (reportCancel) {\n')],
     'width-match'),
    ('width-predigest-truncate',
     [('\t\tgta4l_emmc_block_sha256(widthReference,\n'
       '\t\t    GTA4L_EMMC_BLOCK_WIDTH_CASE_BYTES,\n'
       '\t\t    widthReferenceDigest);\n',
       '\t\tgta4l_emmc_block_sha256(widthReference,\n'
       '\t\t    GTA4L_EMMC_BLOCK_BYTES,\n'
       '\t\t    widthReferenceDigest);\n')],
     'width-match'),
    ('width-reference-lba-shift',
     [('\tresult = gta4l_emmc_block_read(&block,\n'
       '\t    GTA4L_EMMC_BLOCK_WIDTH_LBA,\n',
       '\tresult = gta4l_emmc_block_read(&block,\n'
       '\t    GTA4L_EMMC_BLOCK_WIDTH_LBA + 1u,\n')],
     'width-match'),
    ('widthhold-complete-claim',
     [('\t    revertOk) ? 1u : 0u;\n'
       '\treport.widthhold_complete = complete;\n',
       '\t    revertOk) ? 1u : 0u;\n'
       '\tcomplete = 1u;\n'
       '\treport.widthhold_complete = complete;\n')],
     'widthhold-short'),
    ('widthhold-bytes-skip',
     [('\tif (!speedMode && !bulkMode &&\n'
       '\t    (memcmp(slice, widthholdReference + start,\n'
       '\t    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BYTES) != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_MATCH;\n'
       '\t}\n',
       '\tif ((0) && !speedMode && !bulkMode &&\n'
       '\t    (memcmp(slice, widthholdReference + start,\n'
       '\t    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASE_BYTES) != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_MATCH;\n'
       '\t}\n')],
     'widthhold-match-mismatch'),
    ('widthhold-prefix-compare',
     [('\tif (!speedMode && !bulkMode &&\n'
       '\t    (memcmp(slice, widthholdReference + start,\n',
       '\tif (!speedMode && !bulkMode &&\n'
       '\t    (memcmp(slice, widthholdReference,\n')],
     'widthhold-match'),
    ('widthhold-rb-proof',
     [('\t\tif ((widthholdRc != GTA4L_EMMC_OK) ||\n'
       '\t\t    (widthholdSwitch.verified != 1u) ||\n',
       '\t\tif ((widthholdRc != GTA4L_EMMC_OK) ||\n'
       '\t\t    ((widthholdSwitch.verified != 1u) &&\n'
       '\t\t    (widthholdSwitch.switched != 1u)) ||\n')],
     'widthhold-switch-unverified'),
    ('widthhold-foreign-ignore',
     [('\tif ((tag == 0u) || (tag != widthholdActiveTag) ||\n'
       '\t    (block != widthholdActiveFirst) ||\n'
       '\t    (nblks != widthholdActiveBlocks)) {\n',
       '\tif ((tag == 0u) && (tag != widthholdActiveTag) &&\n'
       '\t    (block != widthholdActiveFirst) &&\n'
       '\t    (nblks != widthholdActiveBlocks)) {\n')],
     'widthhold-foreign-tag'),
    ('widthhold-overlap-ignore',
     [('\tif ((tag == 0u) || (tag != widthholdActiveTag) ||\n'
       '\t    (block != widthholdActiveFirst) ||\n'
       '\t    (nblks != widthholdActiveBlocks)) {\n',
       '\tif ((tag == 0u) || (tag != widthholdActiveTag) ||\n'
       '\t    (block < widthholdActiveFirst) ||\n'
       '\t    (block >= widthholdActiveEnd) ||\n'
       '\t    (nblks > widthholdActiveBlocks)) {\n')],
     'widthhold-foreign-extra'),
    ('widthhold-refc18-expect',
     [('\treturn (result == GTA4L_EMMC_BLOCK_OK) &&\n'
       '\t    (completed == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES) &&\n'
       '\t    (widthholdRefSubrequests == 1u) &&\n'
       '\t    (blockDelta == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BLOCKS) &&\n'
       '\t    (chunkDelta == 1u) &&\n'
       '\t    (stopDelta == 1u) &&\n'
       '\t    (stopFailDelta == 0u) && (after.last_stop_result == 0);\n',
       '\treturn (result == GTA4L_EMMC_BLOCK_OK) &&\n'
       '\t    (completed == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES) &&\n'
       '\t    (widthholdRefSubrequests == 1u) &&\n'
       '\t    (blockDelta == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BLOCKS) &&\n'
       '\t    (chunkDelta == 2u) &&\n'
       '\t    (stopDelta == 1u) &&\n'
       '\t    (stopFailDelta == 0u) && (after.last_stop_result == 0);\n')],
     'widthhold-match'),
    ('widthhold-refstop-expect',
     [('\treturn (result == GTA4L_EMMC_BLOCK_OK) &&\n'
       '\t    (completed == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES) &&\n'
       '\t    (widthholdRefSubrequests == 1u) &&\n'
       '\t    (blockDelta == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BLOCKS) &&\n'
       '\t    (chunkDelta == 1u) &&\n'
       '\t    (stopDelta == 1u) &&\n'
       '\t    (stopFailDelta == 0u) && (after.last_stop_result == 0);\n',
       '\treturn (result == GTA4L_EMMC_BLOCK_OK) &&\n'
       '\t    (completed == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES) &&\n'
       '\t    (widthholdRefSubrequests == 1u) &&\n'
       '\t    (blockDelta == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BLOCKS) &&\n'
       '\t    (chunkDelta == 1u) &&\n'
       '\t    (stopDelta == 2u) &&\n'
       '\t    (stopFailDelta == 0u) && (after.last_stop_result == 0);\n')],
     'widthhold-match'),
    ('widthhold-refstopfail-expect',
     [('\treturn (result == GTA4L_EMMC_BLOCK_OK) &&\n'
       '\t    (completed == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES) &&\n'
       '\t    (widthholdRefSubrequests == 1u) &&\n'
       '\t    (blockDelta == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BLOCKS) &&\n'
       '\t    (chunkDelta == 1u) &&\n'
       '\t    (stopDelta == 1u) &&\n'
       '\t    (stopFailDelta == 0u) && (after.last_stop_result == 0);\n',
       '\treturn (result == GTA4L_EMMC_BLOCK_OK) &&\n'
       '\t    (completed == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BYTES) &&\n'
       '\t    (widthholdRefSubrequests == 1u) &&\n'
       '\t    (blockDelta == GTA4L_EMMC_BLOCK_WIDTHHOLD_REF_BLOCKS) &&\n'
       '\t    (chunkDelta == 1u) &&\n'
       '\t    (stopDelta == 1u) &&\n'
       '\t    (stopFailDelta != 0u) && (after.last_stop_result == 0);\n')],
     'widthhold-match'),
    ('widthhold-short-ignore',
     [('\t} else if (actual != (UInt64)length) {\n'
       '\t\tresult->reason = GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_SHORT;\n'
       '\t} else {\n',
       '\t} else if (0) {\n'
       '\t\tresult->reason = GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_SHORT;\n'
       '\t} else {\n')],
     'widthhold-short'),
    ('widthhold-cancel-quiet',
     [('\tif (reportCancel) {\n'
       '\t\twidthholdReportCancel();\n'
       '\t\treason = GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_CANCEL;\n'
       '\t} else {\n',
       '\tif (reportCancel) {\n'
       '\t\twidthholdReportCancel();\n'
       '\t\treason = GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_CANCEL;\n'
       '\t\treturn;\n'
       '\t} else {\n')],
     'widthhold-cancel-post-switch'),
    ('widthhold-budget-success',
     [('\t\t\t\t\t\t\tbudget = 1u;\n'
       '\t\t\t\t\t\t\treason =\n'
       '\t\t\t\t\t\t\t    GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_BUDGET;\n'
       '\t\t\t\t\t\t\tbreak;\n',
       '\t\t\t\t\t\t\tbudget = 1u;\n'
       '\t\t\t\t\t\t\treason =\n'
       '\t\t\t\t\t\t\t    GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_NONE;\n'
       '\t\t\t\t\t\t\tbreak;\n')],
     'widthhold-budget'),
    ('widthhold-case-skip',
     [('\t\t\t\t\tfor (index = 0;\n'
       '\t\t\t\t\t    index < cases;\n',
       '\t\t\t\t\tfor (index = 0;\n'
       '\t\t\t\t\t    index < (cases - 1u);\n'),
      ('\t    revertOk) ? 1u : 0u;\n'
       '\treport.widthhold_complete = complete;\n',
       '\t    revertOk) ? 1u : 0u;\n'
       '\tcomplete = 1u;\n'
       '\treport.widthhold_complete = complete;\n')],
     'widthhold-match'),
    ('widthhold-revert-skip',
     [('\t\trevertOk = widthholdAttemptRevert();\n',
       '\t\trevertOk = true;\n'),
      ('\t    revertOk) ? 1u : 0u;\n'
       '\treport.widthhold_complete = complete;\n',
       '\t    revertOk) ? 1u : 0u;\n'
       '\tcomplete = 1u;\n'
       '\treport.widthhold_complete = complete;\n')],
     'widthhold-match'),
    ('widthhold-advertise-mismatch',
     [('\treport.widthhold_limit = GTA4L_EMMC_BLOCK_MAX_BLOCKS;\n'
       '\treport.widthhold_segments = 1u;\n'
       '\treport.widthhold_capacity = capacityBlocks;\n'
       '\treport.widthhold_partbase = 0u;\n'
       '\treport.widthhold_partsize = 0u;\n'
       '\treport.widthhold_partro = 0u;\n'
       '\treport.widthhold_partdiag = 0u;\n'
       '\tIOLockUnlock(reportLock);\n'
       '\n'
       '\tif (reportCancel) {\n',
       '\treport.widthhold_limit =\n'
       '\t    GTA4L_EMMC_BLOCK_MAX_BLOCKS + 1u;\n'
       '\treport.widthhold_segments = 2u;\n'
       '\treport.widthhold_capacity = capacityBlocks - 1u;\n'
       '\treport.widthhold_partbase = 0u;\n'
       '\treport.widthhold_partsize = 0u;\n'
       '\treport.widthhold_partro = 0u;\n'
       '\treport.widthhold_partdiag = 0u;\n'
       '\tIOLockUnlock(reportLock);\n'
       '\n'
       '\tif (reportCancel) {\n')],
     'widthhold-match'),
    ('widthhold-terminal-claim',
     [('\tIOLockLock(reportLock);\n'
       '\tif (!terminalClaimed) {\n'
       '\t\tterminalClaimed = true;\n'
       '\t\tclaimed = true;\n'
       '\t}\n'
       '\tIOLockUnlock(reportLock);\n'
       '\treturn claimed;\n',
       '\tIOLockLock(reportLock);\n'
       '\tterminalClaimed = true;\n'
       '\tclaimed = true;\n'
       '\tIOLockUnlock(reportLock);\n'
       '\treturn claimed;\n')],
     'widthhold-watchdog-after-revert'),
    ('widthhold-partdiag-lost',
     [('\tIOLockLock(reportLock);\n'
       '\tif (report.widthhold_partdiag == 0u) {\n'
       '\t\treport.widthhold_partdiag = diag;\n'
       '\t}\n'
       '\tIOLockUnlock(reportLock);\n',
       '\tIOLockLock(reportLock);\n'
       '\treport.widthhold_partdiag = diag & 0u;\n'
       '\tIOLockUnlock(reportLock);\n')],
     'widthhold-part-absent'),
    ('widthhold-partdiag-reset',
     [('\treport.widthhold_partdiag = 0u;\n',
       '\treport.widthhold_partdiag = 2u;\n')],
     'widthhold-part-absent'),
    ('widthhold-part-content-guard',
     [('\tif (contentProperty == nullptr) {\n'
       '\t\twidthholdLatchPartDiag(\n'
       '\t\t    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_CONTENT);\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_PART;\n'
       '\t}\n',
       '\tif ((0) && (contentProperty == nullptr)) {\n'
       '\t\twidthholdLatchPartDiag(\n'
       '\t\t    GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_CONTENT);\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_PART;\n'
       '\t}\n')],
     'widthhold-part-content-missing'),
    ('widthhold-part-content-name',
     [('\tif (contentProperty == nullptr) {\n',
       '\tif ((contentProperty == nullptr) ||\n'
       '\t    !gta4l_emmc_block_text_equal(\n'
       '\t    contentProperty->getCStringNoCopy(), "Apple_Boot")) {\n')],
     'widthhold-part-content-guid'),
    ('widthhold-part-writable-guard',
     [('\tif ((writableProperty == nullptr) ||\n'
       '\t    writableProperty->getValue()) {\n',
       '\tif ((0) && ((writableProperty == nullptr) ||\n'
       '\t    writableProperty->getValue())) {\n')],
     'widthhold-part-writable'),
    ('widthpub-single-pass',
     [('\treturn widthpubMode ? GTA4L_EMMC_BLOCK_WIDTHPUB_CASES :\n'
       '\t    GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES;\n',
       '\treturn GTA4L_EMMC_BLOCK_WIDTHHOLD_CASES;\n')],
     'widthpub-match'),
    ('widthpub-window-collapse',
     [('\t\t\t\t\t\twindowIndex = widthpubMode ?\n'
       '\t\t\t\t\t\t    (index %\n'
       '\t\t\t\t\t\t    GTA4L_EMMC_BLOCK_WIDTHPUB_CASE_OFFSETS) :\n'
       '\t\t\t\t\t\t    index;\n',
       '\t\t\t\t\t\twindowIndex = widthpubMode ?\n'
       '\t\t\t\t\t\t    (index %\n'
       '\t\t\t\t\t\t    GTA4L_EMMC_BLOCK_WIDTHPUB_PASSES) :\n'
       '\t\t\t\t\t\t    index;\n')],
     'widthpub-match'),
    ('widthpub-pub-cleared',
     [('\t\t\t\treport.widthpub_pub = 1u;\n',
       '\t\t\t\treport.widthpub_pub = 0u;\n')],
     'widthpub-match'),
    ('widthpub-pub-before-discovery',
     [('\t\treason = widthholdDiscoverMedia();\n'
       '\t\tif (reason == GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_NONE) {\n'
       '\t\t\tif (widthpubMode) {\n',
       '\t\treason = widthholdDiscoverMedia();\n'
       '\t\treport.widthpub_pub = 1u;\n'
       '\t\tif (reason == GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_NONE) {\n'
       '\t\t\tif (widthpubMode) {\n')],
     'widthpub-media-absent'),
    ('widthpub-offline-missing',
     [('\tif (complete == 0u) {\n'
       '\t\tnotifyOfflineOnce();\n'
       '\t}\n',
       '\tif (complete == 0u) {\n'
       '\t}\n')],
     'widthpub-budget'),
    ('widthpub-offline-latch',
     [('\tif (!offlineNotified) {\n'
       '\t\tofflineNotified = true;\n'
       '\t\tsend = true;\n'
       '\t}\n',
       '\tif (1) {\n'
       '\t\tofflineNotified = true;\n'
       '\t\tsend = true;\n'
       '\t}\n')],
     'widthpub-read-error'),
    ('speed-start-budget-dropped',
     [('\t\tgta4l_emmc_deadline_arm(&host,\n'
       '\t\t    GTA4L_EMMC_BLOCK_SPEED_BUDGET_MS);\n',
       '\t\tgta4l_emmc_deadline_clear(&host);\n')],
     'speed-start-8bit'),
    ('speed-hs-skipped',
     [('\t\tspeedRc = gta4l_emmc_set_hs_timing(&host, 1u,\n'
       '\t\t    &speedHs);\n',
       '\t\tspeedRc = GTA4L_EMMC_OK;\n')],
     'speed-start-8bit'),
    ('speed-fallback-unverified',
     [('\t\t\tif ((speedSwitch.cmd6_attempts < 1u) ||\n'
       '\t\t\t    (speedSwitch.rollback_result !=\n'
       '\t\t\t    GTA4L_EMMC_OK)) {\n',
       '\t\t\tif (0) {\n')],
     'speed-start-width-restore'),
    ('speed-wroll-cleared',
     [('\t\t\treport.speed_wroll = 1u;\n',
       '\t\t\treport.speed_wroll = 0u;\n')],
     'speed-start-fallback'),
    ('speed-start-budget-uncleared',
     [('\t\tgta4l_emmc_deadline_clear(&host);\n'
       '\t\tIOLockLock(reportLock);\n'
       '\t\treport.speed_base = 1u;\n',
       '\t\tIOLockLock(reportLock);\n'
       '\t\treport.speed_base = 1u;\n')],
     'speed-start-8bit'),
    ('speed-hs-stage-misreported',
     [('\t\t\tspeedLatchFailure(\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_SPEED_STAGE_HS,\n',
       '\t\t\tspeedLatchFailure(\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_SPEED_STAGE_WIDTH,\n')],
     'speed-start-hs-fail'),
    ('speed-hispd-masked',
     [('\tspeedControl = control & 0xffu;\n',
       '\tspeedControl = control & 0u;\n')],
     'speed-start-hispd-record'),
    ('speed-report-budget-dropped',
     [('\t\t\t\t\t\tif (widthholdElapsedMs() >=\n'
       '\t\t\t\t\t\t    GTA4L_EMMC_BLOCK_SPEED_READ_BUDGET_MS) {\n',
       '\t\t\t\t\t\tif (0) {\n')],
     'speed-budget'),
    ('speed-report-cancel-dropped',
     [('\t\t\t\t\t\tif (reportCancel) {\n'
       '\t\t\t\t\t\t\tcancel = 1;\n'
       '\t\t\t\t\t\t\treason =\n'
       '\t\t\t\t\t\t\t    GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_CANCEL;\n'
       '\t\t\t\t\t\t\tbreak;\n'
       '\t\t\t\t\t\t}\n',
       '\t\t\t\t\t\tif (0) {\n'
       '\t\t\t\t\t\t\tbreak;\n'
       '\t\t\t\t\t\t}\n')],
     'speed-cancel-during'),
    ('speed-report-digest-swapped',
     [('\t\t\t\t\t\t\tmemcpy(\n'
       '\t\t\t\t\t\t\t    report.speed_dig1,\n'
       '\t\t\t\t\t\t\t    digest,\n'
       '\t\t\t\t\t\t\t    16u);\n',
       '\t\t\t\t\t\t\tmemcpy(\n'
       '\t\t\t\t\t\t\t    report.speed_dig2,\n'
       '\t\t\t\t\t\t\t    digest,\n'
       '\t\t\t\t\t\t\t    16u);\n')],
     'speed-match-8bit'),
    ('speed-report-hash-early',
     [('\t\t\t\t\tif ((windowIndex ==\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_SPEED_CASE_OFFSETS) &&\n',
       '\t\t\t\t\tif ((windowIndex == 0u) &&\n')],
     'speed-match-8bit'),
    ('speed-report-requests-dropped',
     [('\t\t\t\t\t\t\trequests++;\n',
       '\t\t\t\t\t\t\t;\n')],
     'speed-match-8bit'),
    ('speed-report-io-dropped',
     [('\treport.speed_fail_status = speedFailStatus;\n'
       '\treport.speedio_req = (uint32_t)host.timing_requests;\n',
       '\treport.speed_fail_status = speedFailStatus;\n'
       '\treport.speedio_req = 0u;\n')],
     'speed-match-8bit'),
    ('speed-report-fails-dropped',
     [('\t\t\t\t\t\t\tfails++;\n',
       '\t\t\t\t\t\t\t;\n')],
     'speed-read-crc'),
    ('speed-report-partial-dropped',
     [('\t\t\t\t\t\t\tpartial = 1;\n',
       '\t\t\t\t\t\t\tpartial = 0;\n')],
     'speed-short'),
    ('speed-report-pub-early',
     [('\tuint32_t partial = 0;\n'
       '\tuint32_t published = 0;\n',
       '\tuint32_t partial = 0;\n'
       '\tuint32_t published = 1;\n')],
     'speed-media-absent'),
    ('speed-report-partdiag-dropped',
     [('\treport.speed_partial = partial;\n'
       '\treport.speed_pub = published;\n'
       '\treport.speed_partdiag = report.widthhold_partdiag;\n',
       '\treport.speed_partial = partial;\n'
       '\treport.speed_pub = published;\n'
       '\treport.speed_partdiag = 0u;\n')],
     'speed-part-writable'),
    ('speed-report-success-no-revert',
     [('\tspeedTimingActive = false;\n'
       '\thost.timing_capture = 0;\n'
       '\trevertOk = speedRecover();\n',
       '\tspeedTimingActive = false;\n'
       '\thost.timing_capture = 0;\n'
       '\trevertOk = true;\n')],
     'speed-match-8bit'),
    ('speed-report-revert-ignored',
     [('\tif (!revertOk) {\n'
       '\t\tonline = false;\n'
       '\t}\n'
       '\treleaseWidthholdMedia();\n',
       '\tif (0) {\n'
       '\t\tonline = false;\n'
       '\t}\n'
       '\treleaseWidthholdMedia();\n')],
     'speed-revert-fail'),
    ('speed-report-media-not-released',
     [('\treleaseWidthholdMedia();\n'
       '\n'
       '\tIOLockLock(reportLock);\n'
       '\treport.speed_req = requests;\n',
       '\tIOLockLock(reportLock);\n'
       '\treport.speed_req = requests;\n')],
     'speed-match-8bit'),
    ('speed-report-offline-skipped',
     [('\tif ((reason != GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_NONE) ||\n'
       '\t    !revertOk) {\n'
       '\t\tnotifyOfflineOnce();\n'
       '\t}\n',
       '\tif (0) {\n'
       '\t\tnotifyOfflineOnce();\n'
       '\t}\n')],
     'speed-media-absent'),
    ('speed-report-offline-always',
     [('\tif ((reason != GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_NONE) ||\n'
       '\t    !revertOk) {\n'
       '\t\tnotifyOfflineOnce();\n'
       '\t}\n',
       '\tif (1) {\n'
       '\t\tnotifyOfflineOnce();\n'
       '\t}\n')],
     'speed-match-8bit'),
    ('speed-report-revert-budget',
     [('\t    GTA4L_EMMC_BLOCK_SPEED_RECOVER_MS);\n',
       '\t    GTA4L_EMMC_BLOCK_SPEED_BUDGET_MS);\n')],
     'speed-revert-budget'),
    ('speed-report-latch-overwrites',
     [('\tif (speedFailStage == GTA4L_EMMC_BLOCK_SPEED_STAGE_NONE) {\n',
       '\tif (1) {\n')],
     'speed-first-failure-latch'),
    ('speed-report-stage-swapped',
     [('\tcase GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_READ:\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_STAGE_READ;\n',
       '\tcase GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_READ:\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_STAGE_SHORT;\n')],
     'speed-read-crc'),
    ('speed-report-hsrevert-unchecked',
     [('\t    (speedRevert.hs_after == 0u)) ? 1u : 0u;\n',
       '\t    (1u)) ? 1u : 0u;\n')],
     'speed-revert-hs-stuck'),
    ('speed-report-rclk-unchecked',
     [('\treport.speed_rclk = ((speedRevert.clock_result == GTA4L_EMMC_OK) &&\n'
       '\t    ((speedRevert.rcg_after & mask) ==\n'
       '\t    (GTA4L_EMMC_RCG_400K_CFG & mask))) ?\n'
       '\t    GTA4L_EMMC_BLOCK_CLOCK_HZ : 0u;\n',
       '\treport.speed_rclk = GTA4L_EMMC_BLOCK_CLOCK_HZ;\n'
       '\t(void)mask;\n')],
     'speed-revert-clock'),
    ('speed-report-rverified-from-status',
     [('\treport.speed_rverified = speedRevert.verified;\n',
       '\treport.speed_rverified =\n'
       '\t    (speedRecoverStatus == GTA4L_EMMC_OK) ? 1u : 0u;\n')],
     'speed-revert-unverified'),
    ('speed-report-winning-width',
     [('\t\t    (speedFallback.requested != 0u) ? &speedFallback :\n'
       '\t\t    &speedSwitch;\n',
       '\t\t    &speedSwitch;\n')],
     'speed-match-4bit'),
    ('speed-report-hz-zero',
     [('\tspeedGateTimedOut = false;\n'
       '\treport.speed_req = 0;\n'
       '\treport.speed_fails = 0;\n'
       '\treport.speed_cancel = 0;\n'
       '\treport.speed_budget = 0;\n'
       '\treport.speed_partial = 0;\n'
       '\treport.speed_pub = 0;\n'
       '\treport.speed_partdiag = 0;\n'
       '\treport.speed_bytes1 = 0;\n'
       '\treport.speed_ticks1 = 0;\n'
       '\treport.speed_bytes2 = 0;\n'
       '\treport.speed_ticks2 = 0;\n'
       '\treport.speed_hz = (uint32_t)gta4l_emmc_platform_counter_hz();\n',
       '\tspeedGateTimedOut = false;\n'
       '\treport.speed_req = 0;\n'
       '\treport.speed_fails = 0;\n'
       '\treport.speed_cancel = 0;\n'
       '\treport.speed_budget = 0;\n'
       '\treport.speed_partial = 0;\n'
       '\treport.speed_pub = 0;\n'
       '\treport.speed_partdiag = 0;\n'
       '\treport.speed_bytes1 = 0;\n'
       '\treport.speed_ticks1 = 0;\n'
       '\treport.speed_bytes2 = 0;\n'
       '\treport.speed_ticks2 = 0;\n'
       '\treport.speed_hz = 0u;\n')],
     'speed-match-8bit'),
    ('bulk-budget-skip',
     [('\t\t\tif (bulkElapsedMs() >=\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_BULK_STREAM_BUDGET_MS) {\n',
       '\t\t\tif ((0) && bulkElapsedMs() >=\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_BULK_STREAM_BUDGET_MS) {\n')],
     'bulk-budget'),
    ('bulk-budget-flag-dropped',
     [('\t\t\tif (bulkElapsedMs() >=\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_BULK_STREAM_BUDGET_MS) {\n'
       '\t\t\t\tbudget = 1;\n',
       '\t\t\tif (bulkElapsedMs() >=\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_BULK_STREAM_BUDGET_MS) {\n'
       '\t\t\t\tbudget = 0;\n')],
     'bulk-budget'),
    ('bulk-cancel-stream-skip',
     [('\t\t\tif (reportCancel) {\n'
       '\t\t\t\tcancel = 1;\n'
       '\t\t\t\tstage =\n'
       '\t\t\t\t    GTA4L_EMMC_BLOCK_BULK_STAGE_CANCEL;\n'
       '\t\t\t\tbreak;\n'
       '\t\t\t}\n'
       '\t\t\tif (bulkElapsedMs() >=\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_BULK_STREAM_BUDGET_MS) {\n',
       '\t\t\tif (0) {\n'
       '\t\t\t\tcancel = 1;\n'
       '\t\t\t\tstage =\n'
       '\t\t\t\t    GTA4L_EMMC_BLOCK_BULK_STAGE_CANCEL;\n'
       '\t\t\t\tbreak;\n'
       '\t\t\t}\n'
       '\t\t\tif (bulkElapsedMs() >=\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_BULK_STREAM_BUDGET_MS) {\n')],
     'bulk-cancel-during'),
    ('bulk-window-compare-skip',
     [('\t\t\tif (memcmp(bulkClient->data(), bulkReference,\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_BULK_WINDOW_BYTES) != 0) {\n',
       '\t\t\tif ((0) && memcmp(bulkClient->data(), bulkReference,\n'
       '\t\t\t    GTA4L_EMMC_BLOCK_BULK_WINDOW_BYTES) != 0) {\n')],
     'bulk-window-mismatch'),
    ('bulk-digest-report-omit',
     [('\tif (streamHashed) {\n'
       '\t\tmemcpy(report.bulk_digest, digest,\n'
       '\t\t    sizeof(report.bulk_digest));\n'
       '\t}\n',
       '\tif (streamHashed) {\n'
       '\t\tbzero(report.bulk_digest, sizeof(report.bulk_digest));\n'
       '\t}\n')],
     'bulk-success'),
    ('bulk-chunks-overclaim',
     [('\t\t\tif (readOk) {\n'
       '\t\t\t\tchunks++;\n'
       '\t\t\t}\n',
       '\t\t\tif (readOk) {\n'
       '\t\t\t\tchunks += 2u;\n'
       '\t\t\t}\n')],
     'bulk-success'),
    ('bulkio-brr-dropped',
     [('\treport.bulkio_brr = (uint32_t)host.timing_brr_wait_ticks;\n',
       '\treport.bulkio_brr = 0u;\n')],
     'bulk-success'),
    ('bulkio-clock-dropped',
     [('\treport.bulkio_clock_control = host.diag_clock_control;\n',
       '\treport.bulkio_clock_control = 0u;\n')],
     'bulk-success'),
    ('bulkio-cal-dropped',
     [('\treport.bulkio_cal = (uint32_t)host.drain_cal_ticks;\n',
       '\treport.bulkio_cal = 0u;\n')],
     'bulk-success'),
    ('bulk-short-stage-swapped',
     [('\t\t\t\t\tstage =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_BULK_STAGE_SHORT;\n',
       '\t\t\t\t\tstage =\n'
       '\t\t\t\t\t    GTA4L_EMMC_BLOCK_BULK_STAGE_READ;\n')],
     'bulk-short'),
    ('bulk-read-failure-skip',
     [('\t\t\tif (!readOk) {\n'
       '\t\t\t\tfailStatus = result.status;\n',
       '\t\t\tif (0) {\n'
       '\t\t\t\tfailStatus = result.status;\n')],
     'bulk-read-crc'),
    ('bulk-foreign-accept',
     [('\tif ((tag == 0u) || (tag != bulkActiveTag) ||\n'
       '\t    (block != bulkActiveFirst) ||\n'
       '\t    (nblks != bulkActiveBlocks)) {\n',
       '\tif ((tag == 0u) && (tag != bulkActiveTag) &&\n'
       '\t    (block != bulkActiveFirst) &&\n'
       '\t    (nblks != bulkActiveBlocks)) {\n')],
     'bulk-shape'),
    ('bulk-revert-ignore',
     [('\trevertOk = speedRecover();\n'
       '\tif (!revertOk) {\n'
       '\t\tonline = false;\n'
       '\t}\n'
       '\treleaseBulkMedia();\n',
       '\trevertOk = speedRecover();\n'
       '\tif (0) {\n'
       '\t\tonline = false;\n'
       '\t}\n'
       '\treleaseBulkMedia();\n')],
     'bulk-revert-fail'),
    ('smmu-map-fail-ignored',
     [('\tif (gta4l_emmc_platform_map_smmu(&platform) != 0) {\n'
       '\t\tdecision = GTA4L_EMMC_BLOCK_SMMU_DECISION_MAP_FAIL;\n'
       '\t\tgoto done;\n'
       '\t}\n',
       '\tif (0) {\n'
       '\t\tdecision = GTA4L_EMMC_BLOCK_SMMU_DECISION_MAP_FAIL;\n'
       '\t\tgoto done;\n'
       '\t}\n')],
     'bulk-smmu-map-fail'),
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
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_PARTOBS_REFUSED;\n'
       '\t}\n'
       '\tif (token_value != 1) {\n',
       '\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_PARTOBS_REFUSED;\n'
       '\t}\n'
       '\tif ((token_value != 1) && 0) {\n')],
     'policy'),
    ('partobs-block-required',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (speed_selected != 0)) {\n',
       '\tif ((duplicate_tokens != 0) || ((block_selected == 0) && 0) ||\n'
       '\t    (speed_selected != 0)) {\n')],
     'policy'),
    ('partobs-width-overflow',
     [('\t\tgta4l_emmc_block_put_string(&writer,\n'
       '\t\t    "GTA4L-EMMC P5 PARTOBS: ");\n',
       '\t\tgta4l_emmc_block_put_string(&writer,\n'
       '\t\t    "' + 'P' * 2100 + '");\n')],
     'policy'),
    ('partcmp-value-accepted',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (partobs_selected != 0) || (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_PARTCMP_REFUSED;\n'
       '\t}\n'
       '\tif (token_value != 1) {\n',
       '\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (partobs_selected != 0) || (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_PARTCMP_REFUSED;\n'
       '\t}\n'
       '\tif ((token_value != 1) && 0) {\n')],
     'partcmp-refused-value'),
    ('partcmp-partobs-exclusive',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (partobs_selected != 0) || (speed_selected != 0)) {\n',
       '\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    ((partobs_selected != 0) && 0) || (speed_selected != 0)) {\n')],
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
    ('consume-suppress-read',
     [('\tif ((report->consume == 0) && (report->width == 0) &&\n'
       '\t    (report->widthhold == 0) && (report->speed == 0)) {\n'
       '\t\tgta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 READ: ");\n',
       '\tif (1) {\n'
       '\t\tgta4l_emmc_block_put_string(&writer, "GTA4L-EMMC P5 READ: ");\n')],
     'consume-match'),
    ('consume-width-overflow',
     [('\t\tgta4l_emmc_block_put_string(&writer,\n'
       '\t\t    "GTA4L-EMMC P5 CONSUME: ");\n',
       '\t\tgta4l_emmc_block_put_string(&writer,\n'
       '\t\t    "' + 'C' * 2100 + '");\n')],
     'policy'),
    ('widthhold-value-accepted',
     [('\tif (token_value != (int)GTA4L_EMMC_BLOCK_WIDTHHOLD_ON) {\n',
       '\tif ((token_value != (int)GTA4L_EMMC_BLOCK_WIDTHHOLD_ON) &&\n'
       '\t    0) {\n')],
     'widthhold-refused-value'),
    ('widthhold-duplicate-accepted',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHHOLD_REFUSED;\n'
       '\t}\n',
       '\tif (((duplicate_tokens != 0) && 0) || (block_selected == 0) ||\n'
       '\t    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHHOLD_REFUSED;\n'
       '\t}\n')],
     'widthhold-refused-duplicate'),
    ('widthhold-block-accepted',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHHOLD_REFUSED;\n'
       '\t}\n',
       '\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    ((block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) &&\n'
       '\t    0) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHHOLD_REFUSED;\n'
       '\t}\n')],
     'widthhold-refused-block'),
    ('widthhold-width-conflict-accepted',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHHOLD_REFUSED;\n'
       '\t}\n',
       '\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) ||\n'
       '\t    ((width_selected != 0) && 0) ||\n'
       '\t    (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHHOLD_REFUSED;\n'
       '\t}\n')],
     'widthhold-refused-conflicts'),
    ('widthhold-partdiag-omitted',
     [('\t\tgta4l_emmc_block_put_key(&writer, "partdiag");\n'
       '\t\tgta4l_emmc_block_put_u64(&writer,\n'
       '\t\t    report->widthhold_partdiag);\n'
       '\t\tgta4l_emmc_block_put_char(&writer, \' \');\n',
       '')],
     'widthhold-part-absent'),
    ('widthpub-value-accepted',
     [('\tif (token_value != (int)GTA4L_EMMC_BLOCK_WIDTHPUB_ON) {\n',
       '\tif ((token_value != (int)GTA4L_EMMC_BLOCK_WIDTHPUB_ON) &&\n'
       '\t    0) {\n')],
     'widthpub-refused-value'),
    ('widthpub-duplicate-accepted',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHPUB_REFUSED;\n'
       '\t}\n',
       '\tif (((duplicate_tokens != 0) && 0) ||\n'
       '\t    (block_selected == 0) ||\n'
       '\t    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHPUB_REFUSED;\n'
       '\t}\n')],
     'widthpub-refused-duplicate'),
    ('widthpub-widthhold-conflict-accepted',
     [('\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHPUB_REFUSED;\n',
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    ((widthhold_selected != 0) && 0) ||\n'
       '\t    (speed_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_WIDTHPUB_REFUSED;\n')],
     'widthpub-refused-conflicts'),
    ('speed-value-accepted',
     [('\tif (token_value != (int)GTA4L_EMMC_BLOCK_SPEED_ON) {\n',
       '\tif ((token_value != (int)GTA4L_EMMC_BLOCK_SPEED_ON) && 0) {\n')],
     'speed-refused-value'),
    ('speed-duplicate-accepted',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n'
       '\t}\n',
       '\tif (((duplicate_tokens != 0) && 0) || (block_selected == 0) ||\n'
       '\t    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n'
       '\t}\n')],
     'speed-refused-duplicate'),
    ('speed-block-accepted',
     [('\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    (block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n'
       '\t}\n',
       '\tif ((duplicate_tokens != 0) || (block_selected == 0) ||\n'
       '\t    ((block_value != GTA4L_EMMC_BLOCK_MODE_REPORT_CHUNKED) &&\n'
       '\t    0) ||\n'
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n'
       '\t}\n')],
     'speed-refused-block'),
    ('speed-partobs-conflict-accepted',
     [('\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n',
       '\t    ((partobs_selected != 0) && 0) ||\n'
       '\t    (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n')],
     'policy'),
    ('speed-widthpub-conflict-accepted',
     [('\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n',
       '\t    (widthhold_selected != 0) ||\n'
       '\t    ((widthpub_selected != 0) && 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n')],
     'policy'),
    ('speed-partcmp-conflict-accepted',
     [(
       '\t    (partobs_selected != 0) || (partcmp_selected != 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n',
       '\t    (partobs_selected != 0) ||\n'
       '\t    ((partcmp_selected != 0) && 0) ||\n'
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n')],
     'policy'),
    ('speed-partread-conflict-accepted',
     [(
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n',
       '\t    ((partread_selected != 0) && 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n')],
     'policy'),
    ('speed-deblock-conflict-accepted',
     [(
       '\t    (partread_selected != 0) || (deblock_selected != 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n',
       '\t    (partread_selected != 0) || ((deblock_selected != 0) && 0) ||\n'
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n')],
     'policy'),
    ('speed-breakup-conflict-accepted',
     [(
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n',
       '\t    ((breakup_selected != 0) && 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n')],
     'policy'),
    ('speed-sweep-conflict-accepted',
     [(
       '\t    (breakup_selected != 0) || (sweep_selected != 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n',
       '\t    (breakup_selected != 0) || ((sweep_selected != 0) && 0) ||\n'
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n')],
     'policy'),
    ('speed-consume-conflict-accepted',
     [(
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n',
       '\t    ((consume_selected != 0) && 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n')],
     'policy'),
    ('speed-width-conflict-accepted',
     [(
       '\t    (consume_selected != 0) || (width_selected != 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n',
       '\t    (consume_selected != 0) || ((width_selected != 0) && 0) ||\n'
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n')],
     'policy'),
    ('speed-widthhold-conflict-accepted',
     [(
       '\t    (widthhold_selected != 0) || (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n',
       '\t    ((widthhold_selected != 0) && 0) ||\n'
       '\t    (widthpub_selected != 0)) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SPEED_REFUSED;\n')],
     'policy'),
    ('speed-formatter-dig1-dropped',
     [('\t\tgta4l_emmc_block_put_key(&writer, "dig1");\n'
       '\t\tgta4l_emmc_block_put_digest(&writer, report->speed_dig1, 16u);\n'
       '\t\tgta4l_emmc_block_put_char(&writer, \' \');\n',
       '\t\tgta4l_emmc_block_put_char(&writer, \' \');\n')],
     'speed-match-8bit'),
    ('speed-formatter-digest-swapped',
     [('\t\tgta4l_emmc_block_put_digest(&writer, report->speed_dig1, 16u);\n',
       '\t\tgta4l_emmc_block_put_digest(&writer, report->speed_dig2, 16u);\n')],
     'speed-digest-mismatch'),
    ('speed-formatter-rcg-decimal',
     [('\t\tgta4l_emmc_block_put_hex(&writer, report->speed_rcg, 8u);\n',
       '\t\tgta4l_emmc_block_put_u64(&writer, report->speed_rcg);\n')],
     'speed-match-8bit'),
    ('speed-formatter-hispd-short',
     [('\t\tgta4l_emmc_block_put_hex(&writer, report->speed_hispd, 2u);\n',
       '\t\tgta4l_emmc_block_put_hex(&writer, report->speed_hispd, 1u);\n')],
     'speed-match-8bit'),
    ('speed-formatter-part-line',
     [('\t} else if ((report->widthhold == 0) && (report->speed == 0)) {\n',
       '\t} else if (report->widthhold == 0) {\n')],
     'speed-match-8bit'),
    ('speed-formatter-read-line',
     [('\tif ((report->consume == 0) && (report->width == 0) &&\n'
       '\t    (report->widthhold == 0) && (report->speed == 0)) {\n',
       '\tif ((report->consume == 0) && (report->width == 0) &&\n'
       '\t    (report->widthhold == 0)) {\n')],
     'speed-match-8bit'),
    ('smmu-decision-inverted',
     [('\tif (type == GTA4L_EMMC_BLOCK_SMMU_TYPE_BYPASS) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS;\n'
       '\t}\n'
       '\tif (type == GTA4L_EMMC_BLOCK_SMMU_TYPE_FAULT) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SMMU_DECISION_FAULT;\n'
       '\t}\n',
       '\tif (type == GTA4L_EMMC_BLOCK_SMMU_TYPE_BYPASS) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SMMU_DECISION_FAULT;\n'
       '\t}\n'
       '\tif (type == GTA4L_EMMC_BLOCK_SMMU_TYPE_FAULT) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS;\n'
       '\t}\n')],
     'bulk-smmu-fault'),
    ('smmu-match-inverted',
     [('\treturn (((stream ^ smr) & ~mask &\n'
       '\t    GTA4L_EMMC_BLOCK_SMMU_SMR_ID_MASK) == 0u) ? 1 : 0;\n',
       '\treturn (((stream ^ smr) & ~mask &\n'
       '\t    GTA4L_EMMC_BLOCK_SMMU_SMR_ID_MASK) == 0u) ? 0 : 1;\n')],
     'bulk-smmu-fault'),
    ('smmu-usfcfg-inverted',
     [('\t\treturn ((scr0 & GTA4L_EMMC_BLOCK_SMMU_SCR0_USFCFG) != 0u) ?\n'
       '\t\t    GTA4L_EMMC_BLOCK_SMMU_DECISION_FAULT :\n'
       '\t\t    GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS;\n',
       '\t\treturn ((scr0 & GTA4L_EMMC_BLOCK_SMMU_SCR0_USFCFG) != 0u) ?\n'
       '\t\t    GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS :\n'
       '\t\t    GTA4L_EMMC_BLOCK_SMMU_DECISION_FAULT;\n')],
     'bulk-smmu-nomatch'),
    ('smmu-clientpd-inverted',
     [('\tif ((scr0 & GTA4L_EMMC_BLOCK_SMMU_SCR0_CLIENTPD) != 0u) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SMMU_DECISION_BYPASS;\n'
       '\t}\n',
       '\tif ((scr0 & GTA4L_EMMC_BLOCK_SMMU_SCR0_CLIENTPD) != 0u) {\n'
       '\t\treturn GTA4L_EMMC_BLOCK_SMMU_DECISION_FAULT;\n'
       '\t}\n')],
     'bulk-smmu-clientpd'),
    ('smmu-formatter-line-dropped',
     [('\t\tgta4l_emmc_block_put_string(&writer,\n'
       '\t\t    "GTA4L-EMMC P5 SMMU: ");\n',
       '')],
     'bulk-success'),
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
    assert 'GTA4L_EMMC_BLOCK_CONSUME_ARG' in adapter, \
        'adapter must parse the opt-in consume token'
    assert 'gta4l_emmc_block_consume_select' in adapter, \
        'adapter must use the shared consume selector'
    assert 'runConsume' in adapter, \
        'adapter must implement the fixed read-only consumer window'
    assert 'class Gta4lEmmcConsumer : public IOService' in adapter, \
        'consumer must be a distinct IOService client'
    assert 'IOMallocContiguous(' in adapter and \
        'IOFreeContiguous(' in adapter, \
        'consumer must own one contiguous buffer and free it'
    assert 'getPhysicalSegment(0, &segmentLength' in adapter, \
        'consumer must validate the one-segment descriptor'
    assert 'descriptor->setTag(tag)' in adapter, \
        'consumer must tag the single case descriptor'
    assert '->prepare(' not in adapter and '->complete(' not in adapter, \
        'consumer must not manually prepare or complete the descriptor'
    assert 'kIOStorageAccessReaderWriter' not in adapter, \
        'report media must never request writer access'
    assert '->write(' not in adapter and 'IOStorage::write' not in adapter, \
        'adapter must not issue media writes'
    assert 'GTA4L_EMMC_BLOCK_WIDTH_ARG' in adapter, \
        'adapter must parse the opt-in width token'
    assert 'gta4l_emmc_block_width_select' in adapter, \
        'adapter must use the shared width selector'
    assert 'runWidth' in adapter, \
        'adapter must implement the fixed 4-bit window'
    assert 'GTA4L_EMMC_BLOCK_WIDTH_LBA' in adapter, \
        'width window must use the pinned BOOT base'
    assert 'gta4l_emmc_read_ext_csd(&host, widthBaseline)' in adapter, \
        'width mode must capture a 1-bit EXT_CSD baseline'
    assert 'gta4l_emmc_set_bus_width_verified(&host' in adapter, \
        'width mode must use the verified 4-bit switch'
    assert 'gta4l_emmc_set_bus_width_recover(&host, 1u' in adapter, \
        'width mode must revert to 1-bit'
    assert 'memcmp(widthReference, widthObserved' in adapter, \
        'width mode must compare the whole reference window'
    assert adapter.index('gta4l_emmc_read_ext_csd(&host') < \
        adapter.index('gta4l_emmc_set_bus_width_verified(&host') < \
        adapter.index('gta4l_emmc_set_bus_width_recover(&host'), \
        'width sequence must baseline, switch and revert in order'
    assert 'GTA4L_EMMC_BLOCK_WIDTH_WORST_MS <' in adapter, \
        'width mode must keep its worst case below the watchdog'
    assert 'GTA4L_EMMC_BLOCK_WIDTH_REPORT_WORST_BYTES <=' in adapter, \
        'width report must fit the report buffer'
    assert 'GTA4L-EMMC P5 WIDTHFAIL: ' in policy, \
        'width failure must emit the first-failure detail line'
    assert 'report->width == 0' in policy, \
        'legacy READ/QUAL lines must stay suppressed in width mode'
    assert 'GTA4L_EMMC_BLOCK_WIDTHHOLD_ARG' in adapter, \
        'adapter must parse the opt-in widthhold token'
    assert 'gta4l_emmc_block_widthhold_select' in adapter, \
        'adapter must use the shared widthhold selector'
    assert 'gta4l_emmc_block_widthhold_select' in policy, \
        'widthhold selector must live in the shared policy'
    assert 'runWidthhold' in adapter, \
        'adapter must implement the sustained-width qualification'
    assert 'widthholdReference' in adapter and \
        'IOFreeContiguous(widthholdReference' in adapter, \
        'widthhold reference scratch must be allocated and freed'
    assert 'widthholdObserved' in adapter and \
        'IOFreeContiguous(widthholdObserved' in adapter, \
        'widthhold observed scratch must be allocated and freed'
    assert 'GTA4L_EMMC_BLOCK_WIDTHHOLD_BITS_4' in adapter, \
        'widthhold mode must switch to 4-bit'
    assert 'widthholdSynchronousRecover' in adapter, \
        'widthhold mode must recover synchronously on failure'
    assert 'memcmp(slice, widthholdReference + start' in adapter, \
        'widthhold cases must compare against their reference slice'
    assert 'widthholdObservedOrder' in adapter and \
        'widthholdObservedForeign' in adapter, \
        'widthhold mode must reject foreign and overlapping requests'
    assert 'GTA4L-EMMC P5 WIDTHHOLDFAIL: ' in policy, \
        'widthhold failure must emit the first-failure detail line'
    assert 'GTA4L-EMMC P5 WIDTHHOLDREAD: ' in policy, \
        'widthhold success must emit the observed detail line'
    assert 'report->widthhold == 0' in policy and \
        'report->widthhold != 0' in policy, \
        'widthhold mode must suppress the legacy PART lines'
    assert '(report->widthhold == 0)' in policy, \
        'widthhold mode must suppress the legacy READ/QUAL lines'
    assert '#define GTA4L_EMMC_BLOCK_REPORT_BYTES 3072u' in policy_header, \
        'widthhold report must raise the report buffer to 2,560 bytes'
    assert 'GTA4L_EMMC_BLOCK_WIDTHHOLD_LINE_BYTES <=' in policy, \
        'widthhold summary must stay inside the report buffer'
    assert 'GTA4L_EMMC_BLOCK_WIDTHHOLD_REPORT_WORST_BYTES <=' in policy, \
        'widthhold worst case must stay inside the report buffer'
    assert 'GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_BUDGET 14u' in policy_header, \
        'widthhold budget must have a distinct reason'
    assert 'GTA4L_EMMC_BLOCK_WIDTHHOLD_REASON_ALLOC 21u' in policy_header, \
        'widthhold allocation failure must have a distinct reason'
    for bit, name in ((0, 'NOTFOUND'), (1, 'DUPLICATE'), (2, 'SIZE'),
                      (3, 'BASE'), (4, 'WRITABLE'), (5, 'CONTENT')):
        assert f'#define GTA4L_EMMC_BLOCK_WIDTHHOLD_PART_{name} (1u << {bit})' \
            in policy_header, \
            f'widthhold PART {name} diagnostic bit must stay fixed'
    assert 'widthholdLatchPartDiag' in adapter and \
        'report.widthhold_partdiag = 0u;' in adapter, \
        'widthhold PART diagnostics must be latched and reset per run'
    assert 'Structural Content qualification only' in adapter, \
        'widthhold Content qualification must stay structural'
    assert 'partdiag' in policy and \
        'widthhold_partdiag) == 4u' in policy, \
        'widthhold PART diagnostics must be reported as a u32 field'
    assert '#define GTA4L_EMMC_BLOCK_SPEED_BUDGET_MS 25000u' in \
        policy_header, 'speed start budget must stay fixed at 25,000 ms'
    assert '#define GTA4L_EMMC_BLOCK_SPEED_RECOVER_MS 9500u' in \
        policy_header, 'speed combined revert must stay at 9,500 ms'
    assert '#define GTA4L_EMMC_BLOCK_SPEED_WINDOW_BLOCKS 8u' in \
        policy_header, 'speed window must stay at eight blocks'
    assert '#define GTA4L_EMMC_BLOCK_SPEED_CLOCK_HZ 50000000u' in \
        policy_header, 'speed clock must stay at 50 MHz'
    for index, name in enumerate(('NONE', 'ALLOC', 'BASELINE', 'HS',
                                  'WIDTH', 'FALLBACK', 'BASE', 'ADVERT',
                                  'GATE', 'REVERT')):
        assert f'#define GTA4L_EMMC_BLOCK_SPEED_STAGE_{name} {index}u' in \
            policy_header, f'speed stage {name} must stay fixed'
    assert 'IOMallocContiguous(' in adapter and \
        'GTA4L_EMMC_BLOCK_SPEED_WINDOW_BYTES' in adapter, \
        'speed mode must allocate one contiguous window'
    assert 'IOFreeContiguous(speedWindow' in adapter, \
        'speed window must be freed'
    assert 'gta4l_emmc_deadline_arm(&host,\n\t\t    ' \
        'GTA4L_EMMC_BLOCK_SPEED_BUDGET_MS)' in adapter, \
        'speed start must arm the fixed budget once'
    speed_baseline_at = adapter.index(
        'gta4l_emmc_read_ext_csd(&host, speedBaseline)')
    speed_hs_at = adapter.index('gta4l_emmc_set_hs_timing(&host')
    speed_w8_at = adapter.index(
        'GTA4L_EMMC_BLOCK_SPEED_BITS_8, &speedSwitch)')
    assert speed_baseline_at < speed_hs_at < speed_w8_at, \
        'speed sequence must baseline, enter HS and switch width in order'
    speed_start_at = adapter.index(
        'if (speedMode || bulkMode) {',
        adapter.index('Gta4lEmmcBlock::start('))
    speed_start = adapter[speed_start_at:adapter.index(
        '\n\tif (widthholdMode || widthpubMode) {', speed_start_at)]
    speed_arm_at = speed_start.index('gta4l_emmc_deadline_arm')
    assert speed_start.index('GTA4L_EMMC_BLOCK_SPEED_STAGE_ALLOC') < \
        speed_arm_at, 'speed allocation failure must precede the budget arm'
    for stage in ('BASELINE', 'HS', 'WIDTH', 'FALLBACK'):
        latch_at = speed_start.index(
            f'GTA4L_EMMC_BLOCK_SPEED_STAGE_{stage},')
        window = speed_start[max(0, latch_at - 300):latch_at]
        assert 'gta4l_emmc_deadline_clear(&host);' in window, \
            f'speed {stage} failure must clear the start budget first'
    base_latch_at = adapter.index('GTA4L_EMMC_BLOCK_SPEED_STAGE_BASE,')
    assert 'gta4l_emmc_deadline_clear(&host);' in \
        adapter[max(0, base_latch_at - 400):base_latch_at], \
        'speed BASE failure must clear the start budget first'
    assert adapter.index(
        'gta4l_emmc_deadline_clear(&host);\n\t\tIOLockLock('
        'reportLock);\n\t\treport.speed_base = 1u;') < \
        adapter.index('GTA4L_EMMC_BLOCK_SPEED_STAGE_ADVERT,'), \
        'speed publication exits must run after the budget is cleared'
    assert 'GTA4L_EMMC_BLOCK_SPEED_REPORT_WORST_MS +' in adapter and \
        'GTA4L_EMMC_BLOCK_SPEED_REPORT_MARGIN_MS <=' in adapter, \
        'speed report worst case must be checked against the watchdog'
    assert 'GTA4L_EMMC_BLOCK_SPEED_CASE_WORST_MS ==' in adapter, \
        'speed in-flight case bound must be tied to the transport timeouts'
    assert '#define GTA4L_EMMC_BLOCK_SPEED_STAGE_UNKNOWN 23u' in \
        policy_header, 'an unknown speed reason must have a distinct stage'
    assert '#define GTA4L_EMMC_BLOCK_SPEED_LINE_BYTES 746u' in \
        policy_header, 'the SPEED formatter bound must stay exact'
    assert '#define GTA4L_EMMC_BLOCK_SPEEDIO_LINE_BYTES 267u' in \
        policy_header, 'the SPEEDIO formatter bound must stay exact'
    assert '#define GTA4L_EMMC_BLOCK_SPEED_REPORT_WORST_BYTES 1586u' in \
        policy_header, 'the SPEED report bound must match the base block'
    assert adapter.index(
        'bzero(&speedFallback, sizeof(speedFallback));') < \
        adapter.index('GTA4L_EMMC_BLOCK_SPEED_BITS_8, &speedSwitch)'), \
        'speedFallback must be cleared before the first width attempt'
    assert 'tag = ((speedMode || bulkMode) ?\n' \
        '\t    GTA4L_EMMC_BLOCK_SPEED_TAG :' in adapter \
        and 'tag != widthholdActiveTag' in adapter, \
        'speed tags must be exact and foreign tags rejected'
    assert 'speedSwitch.rollback_result' in adapter and \
        'speedSwitch.cmd6_attempts < 1u' in adapter, \
        'speed fallback must require a verified rollback and an issued CMD6'
    assert 'gta4l_emmc_recover_width_and_timing(&host,' in adapter and \
        'GTA4L_EMMC_BLOCK_SPEED_RECOVER_MS' in adapter, \
        'speed failures must use the combined revert with its own budget'
    assert 'speedControl = control & 0xffu;' in adapter, \
        'speed report must record the HOST_CONTROL HISPD value'
    assert 'report.speed_wid = bits;' in adapter, \
        'speed report must record the winning width'
    assert 'report.speed_wfall = 1u;' in adapter and \
        'report.speed_wroll = 1u;' in adapter, \
        'speed report must record the fallback and its verified rollback'
    assert 'GTA4L_EMMC_BLOCK_BULK_ARG' in adapter, \
        'adapter must parse the opt-in bulk token'
    assert 'gta4l_emmc_block_bulk_select' in adapter and \
        'gta4l_emmc_block_bulk_select' in policy, \
        'bulk selector must live in the shared policy'
    assert 'runBulk' in adapter, \
        'adapter must implement the sustained bulk qualification'
    assert '#define GTA4L_EMMC_BLOCK_BULK_REGION_BYTES 85688320u' in \
        policy_header, 'bulk region must stay at the BOOT image size'
    assert '#define GTA4L_EMMC_BLOCK_BULK_STREAM_BUDGET_MS 4000u' in \
        policy_header, 'bulk stream budget must stay at 4,000 ms'
    assert '#define GTA4L_EMMC_BLOCK_BULK_TOTAL_BUDGET_MS 4500u' in \
        policy_header, 'bulk inclusive transport budget must stay at 4,500 ms'
    assert '#define GTA4L_EMMC_BLOCK_BULK_CMD12_TAIL_MS' in policy_header \
        and 'GTA4L_EMMC_BLOCK_BULK_CMD12_TAIL_MS ==' in adapter, \
        'bulk stop tail must be modeled and pinned to the CMD12 bounds'
    assert '#define GTA4L_EMMC_BLOCK_BULK_REPORT_WORST_MS' in policy_header \
        and 'GTA4L_EMMC_BLOCK_BULK_REPORT_MARGIN_MS <=' in adapter, \
        'bulk report worst case must be checked against the watchdog'
    assert 'host.timing_capture = 1;' in adapter and \
        'report.speedio_dat = host.timing_data_ticks;' in adapter and \
        'report.speedio_io = speedIoTicks;' in adapter, \
        'bulk mode must capture and report the phase split'
    assert '#define GTA4L_EMMC_BLOCK_BULK_LINE_BYTES 249u' in policy_header, \
        'the BULK formatter bound must stay exact'
    assert '#define GTA4L_EMMC_BLOCK_BULKFAIL_LINE_BYTES 167u' in \
        policy_header, 'the BULKFAIL formatter bound must stay exact'
    assert '#define GTA4L_EMMC_BLOCK_BULKIO_LINE_BYTES 250u' in \
        policy_header, 'the BULKIO formatter bound must stay exact'
    assert '#define GTA4L_EMMC_BLOCK_BULK_REPORT_WORST_BYTES 2663u' in \
        policy_header, 'the BULK report bound must match the base block'
    assert 'GTA4L-EMMC P5 BULKIO: ' in policy, \
        'bulk mode must emit the S2.1 data-phase and register line'
    assert '#define GTA4L_EMMC_BLOCK_SMMU_LINE_BYTES 139u' in \
        policy_header, 'the SMMU formatter bound must stay exact'
    assert 'GTA4L-EMMC P5 SMMU: ' in policy, \
        'bulk mode must emit the read-only SMMU state line'
    assert 'gta4l_emmc_block_smmu_match' in policy and \
        'gta4l_emmc_block_smmu_decide' in policy, \
        'the SMMU match and decision matrix must live in the policy'
    assert 'gta4l_emmc_platform_map_smmu' in adapter and \
        'gta4l_emmc_platform_smmu_read32' in adapter, \
        'the gate must map and read only the SMMU global page'
    assert 'GTA4L_EMMC_BLOCK_SMMU_STAGE_PRE' in adapter, \
        'the gate must set the pre-access marker'
    assert 'report.smmu_decision = decision;' in adapter, \
        'the gate must publish its decision'
    assert adapter.index('(void)runSmmuGate();') < \
        adapter.index('for (chunk = 0;'), \
        'the SMMU gate must run before the BOOT stream'
    assert 'gta4l_emmc_capture_diag_regs(&host);' in adapter, \
        'bulk mode must snapshot the link registers before the stream'
    assert 'report.bulkio_brr = (uint32_t)host.timing_brr_wait_ticks;' in \
        adapter, 'bulk mode must report the BRR wait'
    assert 'report.bulkio_drain = (uint32_t)host.timing_drain_ticks;' in \
        adapter, 'bulk mode must report the FIFO drain time'
    assert 'report.bulkio_cal = (uint32_t)host.drain_cal_ticks;' in \
        adapter, 'bulk mode must report the direct drain calibration'
    assert 'gta4l_emmc_calibrate_drain(&host,' in adapter, \
        'bulk mode must calibrate the data-port read cost'
    assert 'shaTicks += gta4l_emmc_platform_counter() - shaStart;' in \
        adapter, 'bulk mode must time the SHA-256 update separately'
    assert 'class Gta4lEmmcBulkClient : public IOService' in adapter, \
        'bulk reads must use a distinct IOService client'
    assert 'GTA4L_EMMC_BLOCK_BULK_CHUNK_BYTES' in adapter and \
        'IOFreeContiguous(buffer' in adapter, \
        'bulk stream must own and free one contiguous chunk'
    assert 'GTA4L_EMMC_BLOCK_BULK_WINDOW_BYTES' in adapter and \
        'IOFreeContiguous(bulkReference' in adapter, \
        'bulk window reference must be allocated and freed'
    assert 'gta4l_emmc_block_sha256_init(&sha)' in adapter and \
        'gta4l_emmc_block_sha256_update(&sha,' in adapter and \
        'gta4l_emmc_block_sha256_final(&sha, digest)' in adapter, \
        'bulk stream must hash exactly the bytes read'
    assert 'gta4l_emmc_block_bulk_window(window' in adapter, \
        'bulk windows must use the shared window derivation'
    assert 'bulkObservedOrder' in adapter and \
        'bulkObservedForeign' in adapter, \
        'bulk mode must reject foreign and overlapping requests'
    assert 'bulkObserve(buffer, blockNumber, nblks)' in adapter, \
        'adapter must observe the actual bulk subrequest decomposition'
    assert 'memcmp(bulkClient->data(), bulkReference' in adapter, \
        'bulk windows must compare the whole observed window'
    assert 'gta4l_emmc_deadline_arm(&host,\n' \
        '\t\t    GTA4L_EMMC_BLOCK_BULK_TOTAL_BUDGET_MS)' in adapter, \
        'bulk stream must arm the inclusive transport budget once'
    assert 'GTA4L-EMMC P5 BULK: ' in policy, \
        'bulk success must emit the summary line'
    assert 'GTA4L-EMMC P5 BULKFAIL: ' in policy, \
        'bulk failure must emit the first-failure detail line'
    assert 'report->bulk != 0' in policy and \
        'report->bulk_fail_stage != 0u' in policy, \
        'bulk failure line must be conditional on a latched stage'
    assert '(report->widthhold == 0) && (report->speed == 0)' in policy, \
        'bulk must suppress the legacy READ/QUAL lines through speed mode'
    probe = CONSUMER_PROBE.read_text()
    assert 'IOMallocContiguous(' in probe and \
        'IOFreeContiguous(' in probe, \
        'consumer ABI probe must own and free the contiguous buffer'
    assert 'getPhysicalSegment(0, &segmentLength' in probe, \
        'consumer ABI probe must validate the one-segment descriptor'
    assert 'descriptor->setTag(tag)' in probe, \
        'consumer ABI probe must tag the single case descriptor'
    assert 'boot->open(this, 0, kIOStorageAccessReader)' in probe, \
        'consumer ABI probe must open the BOOT media read-only'
    assert 'boot->read(this, start, descriptor, nullptr, &actual)' in probe, \
        'consumer ABI probe must issue one synchronous read'
    assert 'boot->close(this, 0)' in probe, \
        'consumer ABI probe must close the BOOT media'
    assert 'kIOStorageAccessReaderWriter' not in probe and \
        'IOBufferMemoryDescriptor' not in probe, \
        'consumer ABI probe must stay read-only and contiguous'
    build = BUILD.read_text()
    assert 'check_consumer_abi' in build, \
        'archive build must gate the consumer ABI probe'
    assert 'PROBE_IMPORTS = 187' in build, \
        'archive build must pin the consumer import count'
    assert 'check_archive_imports' in build, \
        'archive build must gate imports against the candidate kernel'
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


def check_speed_report_roundtrip(stdout, temp):
    """Bind the adapter's SPEED line to the host evidence checker.

    The adapter covers the 4 KiB BOOT window at LBA 2255360 with the same
    deterministic sector pattern the host model uses, so the checker must
    accept the line against a synthetic image built from that pattern. The
    mismatch scenario keeps dig1 clean and must be rejected for disagreement.
    """
    match = re.search(r'^SPEEDMATCH (GTA4L-EMMC P5 SPEED: .*)$',
                      stdout, re.M)
    assert match, 'speed-match-8bit did not print a SPEED line'
    line = match.group(1)
    mismatch = re.search(r'^SPEEDMISMATCH (GTA4L-EMMC P5 SPEED: .*)$',
                         stdout, re.M)
    assert mismatch, 'speed-digest-mismatch did not print a SPEED line'
    lba = 2255360
    image = temp / 'speed-window.img'
    data = bytearray()
    for block in range(lba, lba + 8):
        data.extend(bytes((block + index) & 0xff for index in range(512)))
    assert len(data) == 4096
    image.write_bytes(bytes(data))
    checker = ROOT / 'scripts' / 'check-gta4l-speed-evidence.py'
    passed = subprocess.run(
        [sys.executable, str(checker), '--image', str(image),
         '--line', line], text=True, capture_output=True)
    assert passed.returncode == 0, \
        'speed checker rejected the adapter line:\n' + \
        (passed.stdout + passed.stderr)
    assert 'PASS' in passed.stdout, passed.stdout
    rejected = subprocess.run(
        [sys.executable, str(checker), '--image', str(image),
         '--line', mismatch.group(1)], text=True, capture_output=True)
    assert rejected.returncode != 0, \
        'speed checker accepted a digest mismatch'
    assert 'disagree' in (rejected.stdout + rejected.stderr), \
        rejected.stdout + rejected.stderr
    print('PASS speed checker roundtrip: adapter line and mismatch binding')


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
        assert passes == 48, f'expected 48 PASS lines, got {passes}'
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
        assert passes == 449, f'expected 449 adapter PASS lines, got {passes}'
        print('PASS adapter: 449 scenarios against the host IOKit seam')
        check_speed_report_roundtrip(run.stdout, temp)
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
    print(f'PASS gta4l eMMC block core: 47 scenarios, '
          f'{len(MUTATIONS)} mutation controls')
    return 0


if __name__ == '__main__':
    sys.exit(main())
