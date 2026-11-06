#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host-only checks for the gta4l eMMC probe failure report.

The test copies package/Gta4lEmmcProbe.c and package/Gta4lEmmcPlatform.c,
replaces the generic-counter and barrier instructions and the six MMIO helpers
with the hooks in scripts/sim-gta4l-emmc-probe/harness.c, and compiles the
result against minimal kernel-header stubs. The stage selection, conflict check,
failure-report clock guard, register offsets and panic strings remain the
production code.

The scenarios require that a failed AHB/APPS branch enable reports the GCC state
without any SDHCI access, that an MMIO-map failure or conflicting argument
refuses the probe without touching either window, and that a failure after the
clocks were verified still samples the SDHCI diagnostics (the instrumentation
control). The clock-timeout scenario programs the 400 kHz RCG and asserts the
formatted GCC config/M/N/D/command values and the per-offset diagnostic read
counts. The cmd3-no-cc, cmd3-late-cc and cmd9-timeout scenarios run the
identification sequence far enough to record a real CMD3 poll history and a
timeout capture, including a late completion that arrives only after the poll
loop has timed out. The cmd3-identify scenario completes identification through
EXT_CSD and checks that the success report still carries the populated CMD2,
pre-CMD3, poll-history and CID fields. Mutations insert an unguarded SDHCI read,
replace the guard with an unconditional branch, suppress a GCC report read,
suppress an SDHCI report read, suppress the raw R2 snapshot, suppress either
identification response window, suppress the captured CMD2 error, suppress the
success-report CID or poll history, or suppress the timeout command index; each
must be rejected for the expected reported reason, not merely by a nonzero exit.

The report-max-width scenarios run the failure, identification and full-probe
success report forms with the widest values the simulator can produce: a
10-digit capacity, both poll budgets at their limit, 16-hex-digit tick fields,
a full four-entry CMD7 trace and its overflow count. Two full-probe failure
scenarios fail the first and second CMD17 with a data CRC and a data timeout
and require the block-read failure report and command stop. The MD scenarios
add the optional CMD18 pass: a matching success, a content mismatch that must
stay rc=0 and name the first differing LBA, a mid-transfer data timeout that
must report the block ordinal and the explicit CMD12 result, and a CMD12 that
never completes. They also require AUTO_CMD12 to stay clear, exactly one
explicit stop and the step=19/log=51 row, while the default full probe keeps
the stage-A step=18/log=30 row with no MD tokens. A bare gta4l-emmc-mb
argument must refuse before any MMIO access. The report-width
scenarios instead invoke the production formatters from
scripts/sim-gta4l-emmc-probe/report-width-fixture.c with synthetic field-wise
extremes, assert every bounded field and require the measured length plus margin
to fit the 2048-byte buffer; a width-budget control checks the accept boundary
at 2047 and 2048 bytes. Two width-fixture mutations narrow a stored trace index
and must fail the corresponding width scenario's trace assertion. The
oversized-report control feeds the panic guard a message
that cannot fit the 2048-byte buffer and requires the explicit truncation
rejection, not an unrelated crash. The fit assertions are host vsnprintf
checks; they do not establish that the kernel panic renderer or the display
shows the whole report.

The Stage-B1C scenarios run the fifth probe stage. One success case keeps the
normal stage-B1 escalation with the continuation fields disabled. The
continuation cases model the observed 4-bit tuple and one outcome each: a
passing bus test and content pass with byte 183 still 0, a late byte-183
commit seen only by the post-pass re-read, a continuation bus-test mismatch,
a continuation content mismatch, a continuation content-read error, a
continuation re-read error, and the three non-entering rows (CMD6 R1 error
bits, empty readback and lost host width), which must keep the stage-B1 stop
report with `bc=0` and no continuation commands. Five
mutations reject a continuation entered on a non-entering tuple, a
continuation that does not stop, a wrong content baseline, a skipped
host-width restore and a wrong continue marker.

The stage-6 scenarios add the 1-bit CMD19/CMD14 discriminator before the
first CMD6: a pass that falls through to the normal B1 escalation, a pass
that reaches the observed 4-bit tuple and runs the B1C continuation, a
1-bit CMD19 write-phase timeout (CC and BufferWriteReady latch, no TC, no
error, present `0x03f801f6`) and a 1-bit pattern mismatch. Each failure must
stop with `stop=9` and issue no CMD6; the pass cases must carry the 1-bit
words on the `BW=1` line. Three mutations reject a skipped discriminator, a
failure that continues to the width switch and a wrong stop code.

Nothing here touches a device, maps real MMIO or executes firmware.
"""
import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / 'package' / 'Gta4lEmmcProbe.c'
PLATFORM = ROOT / 'package' / 'Gta4lEmmcPlatform.c'
CORE = ROOT / 'package' / 'Gta4lEmmc.c'
HARNESS = ROOT / 'scripts' / 'sim-gta4l-emmc-probe' / 'harness.c'
FIXTURE = ROOT / 'scripts' / 'sim-gta4l-emmc-probe' / 'report-width-fixture.c'
INCLUDE = ROOT / 'scripts' / 'sim-gta4l-emmc-probe' / 'include'

SCENARIOS = [
    'stage-off',
    'map-fail',
    'conflict',
    'branch-apps',
    'branch-ahb',
    'version-fail',
    'clock-timeout',
    'clock-stable-timeout',
    'cmd2-crc',
    'cmd3-no-cc',
    'cmd3-late-cc',
    'cmd9-timeout',
    'cmd3-identify',
    'probe-h9-map-fail',
    'probe-h9-clock-fail',
    'probe-h9-cmd1-fail',
    'probe-h9-cmd2-fail',
    'probe-h9-clean',
    'probe-h9-timeout',
    'probe-h9-status',
    'probe-h9-error',
    'probe-h9-later-fail',
    'probe-h9-cc-between',
    'probe-h9-cc-at-sample',
    'probe-h9-update-no-cc',
    'probe-h9-no-update',
    'probe-h9-preexisting-r1',
    'probe-h9-identical',
    'probe-h9-multi-change',
    'probe-h9-last-sample',
    'probe-h9-terminal-only',
    'probe-h9-late-cc',
    'probe-h9-index-error',
    'probe-h9-simultaneous',
    'probe-h9-error-in-sample',
    'probe-h9-error-after-read',
    'probe-h9-inhibit',
    'probe-h9-counter-stall',
    'cmd7-tc-only',
    'cmd7-fresh-window',
    'cmd7-cc-then-tc',
    'cmd7-tc-then-cc',
    'cmd7-simultaneous',
    'cmd7-last-poll',
    'cmd7-cc-only',
    'cmd7-busy-error',
    'cmd7-late-error',
    'cmd7-terminal-error',
    'cmd7-trace-overflow',
    'report-max-width-failure',
    'report-max-width-success',
    'report-max-width-probe',
    'probe-full',
    'probe-gpt-mismatch',
    'probe-read-crc',
    'probe-read-timeout',
    'probe-array-crc',
    'probe-mid-loop-error',
    'probe-last-error',
    'probe-md-success',
    'probe-md-mismatch',
    'probe-md-final-mismatch',
    'probe-md-swap',
    'probe-md-sum-preserve',
    'probe-md-short-mismatch',
    'probe-md-short-second-mismatch',
    'probe-md-accept-seam',
    'probe-md-mid-stall',
    'probe-md-mid-fail',
    'probe-md-r1-error',
    'probe-md-recovery-fail',
    'probe-md-recovery-block0-mismatch',
    'probe-md-recovery-mismatch',
    'probe-md-recovery-lba2-fail',
    'probe-md-stop-no-tc',
    'probe-md-drain-tc',
    'probe-md-drain-brr',
    'probe-md-drain-error',
    'probe-md-stall-tc',
    'probe-mb-bare-refuse',
    'probe-b1-success',
    'probe-b1-stuck-width',
    'probe-b1-zero-readback',
    'probe-b1-host-width-stuck',
    'probe-b1-bustest-fail',
    'probe-b1-switch8-stuck',
    'probe-b1-bustest8-fail',
    'probe-b1-content8-div',
    'probe-b1-1bit-pass',
    'probe-b1-1bit-timeout',
    'probe-b1-1bit-mismatch',
    'probe-b1-1bit-continue',
    'probe-dto-fired',
    'probe-dto-silent',
    'probe-dto-q-fail',
    'probe-dto-e2-pass',
    'probe-dto-arm-ro',
    'probe-b2-pass',
    'probe-b2-ignored',
    'probe-b2-clock-busy',
    'probe-b2-content-div',
    'probe-b2-content-read',
    'probe-b2-revert-fail',
    'probe-b2-q-fail',
    'probe-b2-pre-read-fail',
    'probe-h10-pass',
    'probe-h10-hs-ignored',
    'probe-h10-clock-busy',
    'probe-h10-width-ignored',
    'probe-h10-bustest-timeout',
    'probe-h10-bustest-mismatch',
    'probe-h10-content-div',
    'probe-h10-content-read',
    'probe-h10-width-revert-fail',
    'probe-h10-hs-revert-fail',
    'probe-h10-q-fail',
    'probe-h11-honored-pass',
    'probe-h11-w4-replay',
    'probe-h11-w4-mismatch',
    'probe-h11-w8-div',
    'probe-h11-w8-err',
    'probe-h11-w4-anomaly',
    'probe-h11-clock-busy',
    'probe-h11-bustest-timeout',
    'probe-h11-bustest-mismatch',
    'probe-h11-content-read',
    'probe-h11-content-div',
    'probe-h11-width-revert-fail',
    'probe-h11-hs-revert-fail',
    'probe-h11-q-fail',
    'probe-h12-refused-pass',
    'probe-h12-honored',
    'probe-h12-wddr-err',
    'probe-h12-wddr-paradox',
    'probe-h12-wddr-empty',
    'probe-h12-hs-ignored',
    'probe-h12-clock-busy',
    'probe-h12-width-revert-fail',
    'probe-h12-hs-revert-fail',
    'probe-h12-q-fail',
    'probe-h12-pre-read-fail',
    'probe-h13-pass',
    'probe-h13-reset-stuck',
    'probe-h13-inhibit',
    'probe-h13-mismatch',
    'probe-h13-read-timeout',
    'probe-h13-read-crc',
    'probe-h13-read-status',
    'probe-h13-write-crc',
    'probe-h13-normal',
    'probe-h13-q-fail',
    'probe-h14-pass',
    'probe-h14-reset-stuck',
    'probe-h14-inhibit',
    'probe-h14-cmd12-silent',
    'probe-h14-cmd12-crc',
    'probe-h14-cmd13-fail',
    'probe-h14-mismatch',
    'probe-h14-read-crc',
    'probe-h14-q-fail',
    'probe-h14-write-crc',
    'probe-h14-normal',
    'probe-h15-pass',
    'probe-h15-crc',
    'probe-h15-content-div',
    'probe-h15-bustest-wedge',
    'probe-h15-bustest-wedge-unproven',
    'probe-h16-pass',
    'probe-h16-content-div',
    'probe-h17-pass',
    'probe-h17-content-div',
    'probe-h17-pattern-mismatch',
    'probe-h17-write-crc',
    'probe-h17-inhibit',
    'probe-b1c-success',
    'probe-b1c-pass',
    'probe-b1c-late-commit',
    'probe-b1c-bustest-fail',
    'probe-b1c-content-div',
    'probe-b1c-content-error',
    'probe-b1c-reread-error',
    'probe-b1c-c6-error',
    'probe-b1c-zero-readback',
    'probe-b1c-host-width-stuck',
    'report-width-failure',
    'report-width-ident',
    'report-width-probe',
    'report-width-md',
    'report-width-md-failure',
    'report-width-b1',
    'report-width-b2',
    'report-width-h9',
    'report-width-h12',
    'report-width-h13',
    'report-width-h14',
    'report-width-budget-control',
]

WIDTH_SCENARIOS = (
    'report-width-failure',
    'report-width-ident',
    'report-width-probe',
    'report-width-md',
    'report-width-md-failure',
    'report-width-b1',
    'report-width-b2',
    'report-width-h9',
    'report-width-h10',
    'report-width-h11',
    'report-width-h12',
    'report-width-h13',
    'report-width-h14',
)

GUARD = '\tif (host.clocks_enabled != 0) {\n'

HOOK_DECLS = '''
/* Host-test hooks supplied by scripts/sim-gta4l-emmc-probe/harness.c. */
uint64_t gta4l_emmc_probe_test_counter(void);
uint64_t gta4l_emmc_probe_test_counter_hz(void);
void gta4l_emmc_probe_test_delay_us(uint32_t microseconds);
uint8_t gta4l_emmc_probe_test_read8(uint32_t space, uint32_t offset);
uint16_t gta4l_emmc_probe_test_read16(uint32_t space, uint32_t offset);
uint32_t gta4l_emmc_probe_test_read32(uint32_t space, uint32_t offset);
void gta4l_emmc_probe_test_write8(uint32_t space, uint32_t offset,
    uint8_t value);
void gta4l_emmc_probe_test_write16(uint32_t space, uint32_t offset,
    uint16_t value);
void gta4l_emmc_probe_test_write32(uint32_t space, uint32_t offset,
    uint32_t value);
'''

PLATFORM_REWRITES = (
    ('#include "Gta4lEmmcPlatform.h"\n',
     '#include "Gta4lEmmcPlatform.h"\n' + HOOK_DECLS),
    ('\t__asm__ volatile("mrs %0, cntvct_el0" : "=r" (value));\n',
     '\tvalue = gta4l_emmc_probe_test_counter();\n'),
    ('\t__asm__ volatile("mrs %0, cntfrq_el0" : "=r" (value));\n',
     '\tvalue = gta4l_emmc_probe_test_counter_hz();\n'),
    ('\t__asm__ volatile("dsb sy" ::: "memory");\n',
     '\t/* barrier supplied by the host harness */\n'),
    ('''void
gta4l_emmc_platform_delay_us(struct gta4l_emmc_platform *platform,
    uint32_t microseconds)
{
\tuint64_t start;
\tuint64_t ticks;
\tuint64_t spins = 0;
\tuint64_t limit;
\tuint64_t hz = platform->counter_hz;

\tif (hz == 0u) {
\t\thz = 19200000u;
\t}
\tticks = (((uint64_t)microseconds * hz) + 999999u) / 1000000u;
\tlimit = ticks + 1000000u;
\tstart = gta4l_emmc_platform_counter();
\twhile ((gta4l_emmc_platform_counter() - start) < ticks) {
\t\tif (++spins > limit) {
\t\t\t/* A stalled counter would silently skip the settle
\t\t\t * interval. Stop the diagnostic instead. */
\t\t\tpanic("GTA4L-EMMC PROBE FAILED: generic counter "
\t\t\t    "stalled (hz=%u us=%u)",
\t\t\t    (unsigned int)hz,
\t\t\t    (unsigned int)microseconds);
\t\t}
\t\t__asm__ volatile("yield");
\t}
}
''', '''void
gta4l_emmc_platform_delay_us(struct gta4l_emmc_platform *platform,
    uint32_t microseconds)
{
\t(void)platform;
\tgta4l_emmc_probe_test_delay_us(microseconds);
}
'''),
    ('''uint8_t
gta4l_emmc_platform_read8(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\treturn *(volatile uint8_t *)(platform->sdhci + offset);
\t}
\tif (space == GTA4L_EMMC_SPACE_GCC) {
\t\treturn *(volatile uint8_t *)(platform->gcc + offset);
\t}
\treturn *(volatile uint8_t *)(platform->tlmm + offset);
}
''', '''uint8_t
gta4l_emmc_platform_read8(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
\t(void)platform;
\treturn gta4l_emmc_probe_test_read8(space, offset);
}
'''),
    ('''uint16_t
gta4l_emmc_platform_read16(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\treturn *(volatile uint16_t *)(platform->sdhci + offset);
\t}
\tif (space == GTA4L_EMMC_SPACE_GCC) {
\t\treturn *(volatile uint16_t *)(platform->gcc + offset);
\t}
\treturn *(volatile uint16_t *)(platform->tlmm + offset);
}
''', '''uint16_t
gta4l_emmc_platform_read16(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
\t(void)platform;
\treturn gta4l_emmc_probe_test_read16(space, offset);
}
'''),
    ('''uint32_t
gta4l_emmc_platform_read32(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\treturn *(volatile uint32_t *)(platform->sdhci + offset);
\t}
\tif (space == GTA4L_EMMC_SPACE_GCC) {
\t\treturn *(volatile uint32_t *)(platform->gcc + offset);
\t}
\treturn *(volatile uint32_t *)(platform->tlmm + offset);
}
''', '''uint32_t
gta4l_emmc_platform_read32(const struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset)
{
\t(void)platform;
\treturn gta4l_emmc_probe_test_read32(space, offset);
}
'''),
    ('''void
gta4l_emmc_platform_write8(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint8_t value)
{
\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\t*(volatile uint8_t *)(platform->sdhci + offset) = value;
\t} else if (space == GTA4L_EMMC_SPACE_GCC) {
\t\t*(volatile uint8_t *)(platform->gcc + offset) = value;
\t} else {
\t\t*(volatile uint8_t *)(platform->tlmm + offset) = value;
\t}
}
''', '''void
gta4l_emmc_platform_write8(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint8_t value)
{
\t(void)platform;
\tgta4l_emmc_probe_test_write8(space, offset, value);
}
'''),
    ('''void
gta4l_emmc_platform_write16(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint16_t value)
{
\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\t*(volatile uint16_t *)(platform->sdhci + offset) = value;
\t} else if (space == GTA4L_EMMC_SPACE_GCC) {
\t\t*(volatile uint16_t *)(platform->gcc + offset) = value;
\t} else {
\t\t*(volatile uint16_t *)(platform->tlmm + offset) = value;
\t}
}
''', '''void
gta4l_emmc_platform_write16(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint16_t value)
{
\t(void)platform;
\tgta4l_emmc_probe_test_write16(space, offset, value);
}
'''),
    ('''void
gta4l_emmc_platform_write32(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint32_t value)
{
\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\t*(volatile uint32_t *)(platform->sdhci + offset) = value;
\t} else if (space == GTA4L_EMMC_SPACE_GCC) {
\t\t*(volatile uint32_t *)(platform->gcc + offset) = value;
\t} else {
\t\t*(volatile uint32_t *)(platform->tlmm + offset) = value;
\t}
}
''', '''void
gta4l_emmc_platform_write32(struct gta4l_emmc_platform *platform,
    uint32_t space, uint32_t offset, uint32_t value)
{
\t(void)platform;
\tgta4l_emmc_probe_test_write32(space, offset, value);
}
'''),
)

MUTATIONS = [
    ('unguarded-read',
     '\tif (result != GTA4L_EMMC_OK) {\n\t\tif (host.clocks_enabled != 0) {\n',
     '\tif (result != GTA4L_EMMC_OK) {\n'
     '\t\t(void)gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t\t    GTA4L_EMMC_REG_MCI_VERSION);\n'
     '\t\tif (host.clocks_enabled != 0) {\n',
     'branch-apps',
     'SDHCI was read while the branch clocks were off'),
    ('guard-removed',
     '\tif (result != GTA4L_EMMC_OK) {\n\t\tif (host.clocks_enabled != 0) {\n',
     '\tif (result != GTA4L_EMMC_OK) {\n\t\tif (1) {\n',
     'branch-apps',
     'SDHCI was read while the branch clocks were off'),
    ('report-gcc-suppressed',
     '\t\tgcc_m = gta4l_emmc_probe_read32(&probe, GTA4L_EMMC_SPACE_GCC,\n'
     '\t\t    GTA4L_EMMC_GCC_SDCC1_APPS_M);\n',
     '\t\tgcc_m = 0;\n',
     'branch-apps',
     'the report did not sample the RCG M value'),
    ('report-sdhci-suppressed',
     '\t\t\tpower = gta4l_emmc_probe_read8(&probe,\n'
     '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t\t\t    GTA4L_EMMC_REG_POWER_CONTROL);\n',
     '\t\t\tpower = 0;\n',
     'version-fail',
     'the report did not sample the power control'),
    ('cmd2-response-suppressed',
     '\t    host->fail_step, host->fail_response[0],\n',
     '\t    host->fail_step, 0u,\n',
     'cmd2-crc',
     'the report did not capture the raw R2 response'),
    ('cmd2-cmd1-window-suppressed',
     '\t    host->cmd1_response[0], host->cmd1_response[1],\n',
     '\t    0u, host->cmd1_response[1],\n',
     'cmd2-crc',
     'the report did not capture the final CMD1 window'),
    ('cmd2-pre-window-suppressed',
     '\t    host->pre_cmd2_response[0], host->pre_cmd2_response[1],\n',
     '\t    0u, host->pre_cmd2_response[1],\n',
     'cmd2-crc',
     'the report did not capture the pre-CMD2 window'),
    ('cmd2-error-suppressed',
     '\t    stage, result, (unsigned int)host->fail_valid,\n'
     '\t    host->fail_index, host->fail_argument, host->fail_command,\n'
     '\t    (unsigned int)host->fail_error,\n',
     '\t    stage, result, (unsigned int)host->fail_valid,\n'
     '\t    host->fail_index, host->fail_argument, host->fail_command,\n'
     '\t    0u,\n',
     'cmd2-crc',
     'the report does not name the failed CMD2'),
    ('capture-format-suppressed',
     'cid=%08x,%08x,%08x,%08x fmt=4 hv=%04x ',
     'cid=%08x,%08x,%08x,%08x fmt=3 hv=%04x ',
     'cmd2-crc',
     'the report does not carry the capture format'),
    ('host-version-suppressed',
     '\t\t\thost_version = gta4l_emmc_probe_read16(&probe,\n'
     '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t\t\t    GTA4L_EMMC_REG_HOST_VERSION);\n',
     '\t\t\thost_version = 0;\n',
     'cmd2-crc',
     'the report did not sample HOST_VERSION'),
    ('ident-cid-suppressed',
     '\t    ident_host->cid[0], ident_host->cid[1],\n'
     '\t    ident_host->cid[2], ident_host->cid[3],\n'
     '\t    GTA4L_EMMC_PROBE_CMD7_ARGS(*ident_host));\n',
     '\t    0u, ident_host->cid[1],\n'
     '\t    ident_host->cid[2], ident_host->cid[3],\n'
     '\t    GTA4L_EMMC_PROBE_CMD7_ARGS(*ident_host));\n',
     'cmd3-identify',
     'the success report does not carry the decoded CID'),
    ('ident-poll-first-suppressed',
     '\t    (unsigned int)regs->host_version,\n'
     '\t    (unsigned int)ident_host->cmd2_capture_valid,\n'
     '\t    (unsigned int)ident_host->cmd2_normal,\n'
     '\t    (unsigned int)ident_host->cmd2_error,\n'
     '\t    ident_host->cmd2_raw_response[0],\n'
     '\t    ident_host->cmd2_raw_response[1],\n'
     '\t    ident_host->cmd2_raw_response[2],\n'
     '\t    ident_host->cmd2_raw_response[3],\n'
     '\t    (unsigned int)ident_host->pre_cmd3_capture_valid,\n'
     '\t    (unsigned int)ident_host->pre_cmd3_normal,\n'
     '\t    (unsigned int)ident_host->pre_cmd3_error,\n'
     '\t    ident_host->pre_cmd3_present,\n'
     '\t    (unsigned int)ident_host->pre_cmd3_normal_enable,\n'
     '\t    (unsigned int)ident_host->pre_cmd3_error_enable,\n'
     '\t    (unsigned int)ident_host->timeout_capture_valid,\n'
     '\t    (unsigned int)ident_host->timeout_index,\n'
     '\t    (unsigned int)ident_host->timeout_command,\n'
     '\t    ident_host->timeout_argument,\n'
     '\t    (unsigned int)ident_host->timeout_response_stable,\n'
     '\t    ident_host->timeout_response[0], ident_host->timeout_response[1],\n'
     '\t    ident_host->timeout_response[2], ident_host->timeout_response[3],\n'
     '\t    (unsigned int)ident_host->poll_capture_valid,\n'
     '\t    ident_host->poll_index,\n'
     '\t    (unsigned int)ident_host->poll_first_normal,\n',
     '\t    (unsigned int)regs->host_version,\n'
     '\t    (unsigned int)ident_host->cmd2_capture_valid,\n'
     '\t    (unsigned int)ident_host->cmd2_normal,\n'
     '\t    (unsigned int)ident_host->cmd2_error,\n'
     '\t    ident_host->cmd2_raw_response[0],\n'
     '\t    ident_host->cmd2_raw_response[1],\n'
     '\t    ident_host->cmd2_raw_response[2],\n'
     '\t    ident_host->cmd2_raw_response[3],\n'
     '\t    (unsigned int)ident_host->pre_cmd3_capture_valid,\n'
     '\t    (unsigned int)ident_host->pre_cmd3_normal,\n'
     '\t    (unsigned int)ident_host->pre_cmd3_error,\n'
     '\t    ident_host->pre_cmd3_present,\n'
     '\t    (unsigned int)ident_host->pre_cmd3_normal_enable,\n'
     '\t    (unsigned int)ident_host->pre_cmd3_error_enable,\n'
     '\t    (unsigned int)ident_host->timeout_capture_valid,\n'
     '\t    (unsigned int)ident_host->timeout_index,\n'
     '\t    (unsigned int)ident_host->timeout_command,\n'
     '\t    ident_host->timeout_argument,\n'
     '\t    (unsigned int)ident_host->timeout_response_stable,\n'
     '\t    ident_host->timeout_response[0], ident_host->timeout_response[1],\n'
     '\t    ident_host->timeout_response[2], ident_host->timeout_response[3],\n'
     '\t    (unsigned int)ident_host->poll_capture_valid,\n'
     '\t    ident_host->poll_index,\n'
     '\t    0u,\n',
     'cmd3-identify',
     'the success report lost the CMD3 poll history'),
    ('timeout-index-suppressed',
     '\t    host->pre_cmd2_response[2], host->pre_cmd2_response[3],\n'
     '\t    (unsigned int)host->cmd2_capture_valid,\n'
     '\t    (unsigned int)host->cmd2_normal,\n'
     '\t    (unsigned int)host->cmd2_error,\n'
     '\t    host->cmd2_raw_response[0], host->cmd2_raw_response[1],\n'
     '\t    host->cmd2_raw_response[2], host->cmd2_raw_response[3],\n'
     '\t    (unsigned int)host->pre_cmd3_capture_valid,\n'
     '\t    (unsigned int)host->pre_cmd3_normal,\n'
     '\t    (unsigned int)host->pre_cmd3_error,\n'
     '\t    host->pre_cmd3_present,\n'
     '\t    (unsigned int)host->pre_cmd3_normal_enable,\n'
     '\t    (unsigned int)host->pre_cmd3_error_enable,\n'
     '\t    (unsigned int)host->timeout_capture_valid,\n'
     '\t    (unsigned int)host->timeout_index,\n',
     '\t    host->pre_cmd2_response[2], host->pre_cmd2_response[3],\n'
     '\t    (unsigned int)host->cmd2_capture_valid,\n'
     '\t    (unsigned int)host->cmd2_normal,\n'
     '\t    (unsigned int)host->cmd2_error,\n'
     '\t    host->cmd2_raw_response[0], host->cmd2_raw_response[1],\n'
     '\t    host->cmd2_raw_response[2], host->cmd2_raw_response[3],\n'
     '\t    (unsigned int)host->pre_cmd3_capture_valid,\n'
     '\t    (unsigned int)host->pre_cmd3_normal,\n'
     '\t    (unsigned int)host->pre_cmd3_error,\n'
     '\t    host->pre_cmd3_present,\n'
     '\t    (unsigned int)host->pre_cmd3_normal_enable,\n'
     '\t    (unsigned int)host->pre_cmd3_error_enable,\n'
     '\t    (unsigned int)host->timeout_capture_valid,\n'
     '\t    0u,\n',
     'cmd3-no-cc',
     'the timeout capture does not carry the CMD3 index and raw response'),
    ('cmd7-pre-valid-suppressed',
     '\t(unsigned int)(host).cmd7_pre_capture_valid, \\\n',
     '\t0u, \\\n',
     'cmd7-tc-only',
     'the report did not capture the pre-CMD7 state'),
    ('cmd7-poll-mask-suppressed',
     '\t(unsigned int)(host).cmd7_poll_or_normal, \\\n',
     '\t0u, \\\n',
     'cmd7-tc-only',
     'the TC-only poll history mismatch'),
    ('cmd7-tc-valid-suppressed',
     '\t(unsigned int)(host).cmd7_tc_valid, (host).cmd7_tc_iteration, \\\n',
     '\t0u, (host).cmd7_tc_iteration, \\\n',
     'cmd7-tc-only',
     'the TC-only capture lost the observed TC'),
    ('cmd7-cc-valid-suppressed',
     '\t(unsigned int)(host).cmd7_cc_valid, (host).cmd7_cc_iteration, \\\n',
     '\t0u, (host).cmd7_cc_iteration, \\\n',
     'cmd7-tc-then-cc',
     'the TC-then-CC completion history mismatch'),
    ('cmd7-trace-overflow-suppressed',
     '\t(host).cmd7_trace_count, (host).cmd7_trace_overflow, \\\n',
     '\t(host).cmd7_trace_count, 0u, \\\n',
     'cmd7-trace-overflow',
     'the bounded trace or overflow count mismatch'),
    ('cmd7-terminal-window-suppressed',
     '\t(host).cmd7_term_response[0], (host).cmd7_term_response[1], \\\n',
     '\t0u, (host).cmd7_term_response[1], \\\n',
     'cmd7-fresh-window',
     'the report did not capture the fresh CMD7 window'),
    ('width-trace-index-1-narrowed',
     '\thost.cmd7_trace[1].iteration = 99997u;\n',
     '\thost.cmd7_trace[1].iteration = 1u;\n',
     'report-width-failure',
     'the synthetic width failure report lost the late stored trace '
     'index or the overflow bound',
     'fixture'),
    ('width-trace-index-2-narrowed',
     '\thost.cmd7_trace[2].iteration = 99998u;\n',
     '\thost.cmd7_trace[2].iteration = 2u;\n',
     'report-width-ident',
     'the synthetic width ident report lost the late stored trace '
     'index or the overflow bound',
     'fixture'),
    ('block-read-error-suppressed',
     '\t\tpanic("GTA4L-EMMC PROBE FAILED: block 0 read rc=%d step=%u "\n'
     '\t\t    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u", result,\n'
     '\t\t    host.last_step, (unsigned int)host.fail_valid,\n'
     '\t\t    host.fail_index, host.fail_argument, host.fail_command,\n'
     '\t\t    (unsigned int)host.fail_error,\n',
     '\t\tpanic("GTA4L-EMMC PROBE FAILED: block 0 read rc=%d step=%u "\n'
     '\t\t    "fail=%u/%u/%u/%08x/%04x/%04x/%08x/%u", result,\n'
     '\t\t    host.last_step, (unsigned int)host.fail_valid,\n'
     '\t\t    host.fail_index, host.fail_argument, host.fail_command,\n'
     '\t\t    0u,\n',
     'probe-read-crc',
     'the block 0 data-CRC report lost the failure record'),
    ('array-duplicate-lba',
     '\t\tresult = gta4l_emmc_read_block(&host,\n'
     '\t\t    (uint64_t)range.entries_lba + index, range_block);\n',
     '\t\tresult = gta4l_emmc_read_block(&host,\n'
     '\t\t    (uint64_t)range.entries_lba, range_block);\n',
     'probe-full',
     'the full probe read the wrong LBA list'),
    ('array-error-continued',
     '\t\tif (result != GTA4L_EMMC_OK) {\n'
     '\t\t\tgta4l_emmc_probe_report_read_failure(&host, "array",\n'
     '\t\t\t    range.entries_lba + index, result);\n'
     '\t\t}\n',
     '\t\tif (result != GTA4L_EMMC_OK) {\n'
     '\t\t\tcontinue;\n'
     '\t\t}\n',
     'probe-array-crc',
     'the array data-CRC report lost the failure record'),
    ('array-blocks-off-by-one',
     '#define GTA4L_EMMC_PROBE_ARRAY_BLOCKS    21u\n',
     '#define GTA4L_EMMC_PROBE_ARRAY_BLOCKS    20u\n',
     'probe-full',
     'the full probe did not complete the content reads'),
    ('last-read-uses-last-usable',
     '\thost.last_step = GTA4L_EMMC_STEP_PROBE_LAST;\n'
     '\tresult = gta4l_emmc_read_block(&host, range.backup_lba, range_block);\n',
     '\thost.last_step = GTA4L_EMMC_STEP_PROBE_LAST;\n'
     '\tresult = gta4l_emmc_read_block(&host, range.last_usable, '
     'range_block);\n',
     'probe-full',
     'the full probe read the wrong LBA list'),
    ('md-default-enabled',
     '\tmb_mode = gta4l_emmc_probe_mb();\n',
     '\tmb_mode = 1;\n',
     'probe-full',
     'the default probe report invented the MD mode marker'),
    ('md-marker-suppressed',
     '\t    (mb_mode != 0) ? " mb=1" : "",\n',
     '\t    "",\n',
     'probe-md-success',
     'the MD probe report does not name the MD step and log counts'),
    ('md-short-barrier-skipped',
     '\t\tif (range.short_mb.verdict != GTA4L_EMMC_OK) {\n'
     '\t\t\tmb_failure = range.short_mb.verdict;\n'
     '\t\t\tmb_stage = 1u;\n'
     '\t\t}\n',
     '\t\tif (range.short_mb.verdict != GTA4L_EMMC_OK) {\n'
     '\t\t\tmb_stage = 1u;\n'
     '\t\t}\n',
     'probe-md-short-mismatch',
     'the short mismatch did not report the validated count, '
     'first divergence or not-run long request'),
    ('md-long-barrier-skipped',
     '\t\t\tif (range.long_mb.verdict != GTA4L_EMMC_OK) {\n'
     '\t\t\t\tmb_failure = range.long_mb.verdict;\n'
     '\t\t\t\tmb_stage = 2u;\n'
     '\t\t\t}\n',
     '\t\t\tif (range.long_mb.verdict != GTA4L_EMMC_OK) {\n'
     '\t\t\t\tmb_stage = 2u;\n'
     '\t\t\t}\n',
     'probe-md-mismatch',
     'the long mismatch did not report the exact counts, first '
     'divergence and not-run recovery state'),
    ('md-byte-compare-skipped',
     '\t\t\tif (blocks[(index * GTA4L_EMMC_BLOCK_BYTES) + byte] !=\n'
     '\t\t\t    reference[(index * GTA4L_EMMC_BLOCK_BYTES) +\n'
     '\t\t\t    byte]) {\n'
     '\t\t\t\tsame = 0;\n'
     '\t\t\t\tbreak;\n'
     '\t\t\t}\n',
     '\t\t\tif (blocks[(index * GTA4L_EMMC_BLOCK_BYTES) + byte] !=\n'
     '\t\t\t    reference[(index * GTA4L_EMMC_BLOCK_BYTES) +\n'
     '\t\t\t    byte]) {\n'
     '\t\t\t\tsame = 1;\n'
     '\t\t\t\tbreak;\n'
     '\t\t\t}\n',
     'probe-md-sum-preserve',
     'a matching aggregate checksum turned a byte mismatch into '
     'success'),
    ('md-completed-check-skipped',
     '\tif (mb->completed != requested) {\n'
     '\t\treturn GTA4L_EMMC_PROBE_ERR_LENGTH;\n'
     '\t}\n',
     '\tif (mb->completed != requested) {\n'
     '\t\treturn GTA4L_EMMC_OK;\n'
     '\t}\n',
     'probe-md-accept-seam',
     'the acceptance seam rejected a qualifying state or accepted '
     'an incomplete one'),
    ('md-validated-check-skipped',
     '\tif (mb->validated != requested) {\n'
     '\t\treturn GTA4L_EMMC_PROBE_ERR_CONTENT;\n'
     '\t}\n',
     '\tif (mb->validated != requested) {\n'
     '\t\treturn GTA4L_EMMC_OK;\n'
     '\t}\n',
     'probe-md-accept-seam',
     'the acceptance seam rejected a qualifying state or accepted '
     'an incomplete one'),
    ('md-recovery-transport-stop-skipped',
     '\t\t\tif (result != GTA4L_EMMC_OK) {\n'
     '\t\t\t\trange.recovery_result = result;\n'
     '\t\t\t\trange.recovery_same = 0;\n'
     '\t\t\t\tmb_failure = result;\n'
     '\t\t\t\tmb_stage = 3u;\n'
     '\t\t\t} else {\n',
     '\t\t\tif (result != GTA4L_EMMC_OK) {\n'
     '\t\t\t\trange.recovery_result = result;\n'
     '\t\t\t\trange.recovery_same = 0;\n'
     '\t\t\t\tmb_stage = 3u;\n'
     '\t\t\t} else {\n',
     'probe-md-recovery-fail',
     'the failed block-0 recovery read lost its transport result '
     'or not-run LBA-2 state'),
    ('md-recovery-divergence-stop-skipped',
     '\t\t\t\t\trange.recovery_result =\n'
     '\t\t\t\t\t    GTA4L_EMMC_PROBE_ERR_CONTENT;\n'
     '\t\t\t\t\tmb_failure =\n'
     '\t\t\t\t\t    GTA4L_EMMC_PROBE_ERR_CONTENT;\n'
     '\t\t\t\t\tmb_stage = 3u;\n',
     '\t\t\t\t\trange.recovery_result =\n'
     '\t\t\t\t\t    GTA4L_EMMC_PROBE_ERR_CONTENT;\n'
     '\t\t\t\t\tmb_stage = 3u;\n',
     'probe-md-recovery-block0-mismatch',
     'the block-0 recovery mismatch lost its LBA-0 divergence '
     'flag or the not-run LBA-2 state'),
    ('md-overall-gate-inverted',
     '\t\tif ((mb_failure != GTA4L_EMMC_OK) ||\n'
     '\t\t    (range.short_mb.verdict != GTA4L_EMMC_OK) ||\n',
     '\t\tif ((mb_failure != GTA4L_EMMC_OK) ||\n'
     '\t\t    (range.short_mb.verdict == GTA4L_EMMC_OK) ||\n',
     'probe-md-success',
     'the MD probe report does not carry the device capacity'),
    ('md-div-suppressed',
     '\t\t} else if (*div == 0u) {\n'
     '\t\t\t*div = first_lba + index;\n'
     '\t\t}\n',
     '\t\t} else if (*div == 0u) {\n'
     '\t\t\t*div = first_lba;\n'
     '\t\t}\n',
     'probe-md-mismatch',
     'the long mismatch did not report the exact counts, first '
     'divergence and not-run recovery state'),
    ('md-step-suppressed',
     '\t\thost.last_step = GTA4L_EMMC_STEP_PROBE_MB;\n',
     '\t\thost.last_step = GTA4L_EMMC_STEP_PROBE_LAST;\n',
     'probe-md-success',
     'the MD probe report does not name the MD step and log counts'),
    ('md-block-ordinal-suppressed',
     '\t    result, stage, host->last_step, host->multi_block,\n',
     '\t    result, stage, host->last_step, 0u,\n',
     'probe-md-mid-fail',
     'the mid-CMD18 failure report lost the block ordinal, the stop '
     'result or the terminal record'),
    ('md-bare-refusal-suppressed',
     '\tpanic("GTA4L-EMMC PROBE REFUSED: conflicting diagnostic arguments "\n'
     '\t    "(gta4l-emmc-mb requires gta4l-emmc-probe)");\n',
     '\treturn;\n',
     'probe-mb-bare-refuse',
     'a bare MD argument was not refused'),
    ('md-no-multi',
     '\tgta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t    GTA4L_EMMC_REG_ARGUMENT, (uint32_t)start_lba);\n'
     '\tgta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t    GTA4L_EMMC_REG_TRANSFER_MODE,\n'
     '\t    GTA4L_EMMC_TM_READ | GTA4L_EMMC_TM_BLOCK_COUNT_EN |\n'
     '\t    GTA4L_EMMC_TM_MULTI);\n',
     '\tgta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t    GTA4L_EMMC_REG_ARGUMENT, (uint32_t)start_lba);\n'
     '\tgta4l_emmc_write16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t    GTA4L_EMMC_REG_TRANSFER_MODE,\n'
     '\t    GTA4L_EMMC_TM_READ | GTA4L_EMMC_TM_BLOCK_COUNT_EN);\n',
     'probe-md-success',
     'the MD probe used the wrong CMD18/CMD12 form',
     'core'),
    ('b1c-enter-non-entering',
     '\tif (bus->result.readback_sum != host->ext_csd_sum) {\n'
     '\t\treturn 0;\n\t}\n',
     '\tif (0) {\n\t\treturn 0;\n\t}\n\t(void)host;\n',
     'probe-b1c-zero-readback',
     'the empty-readback row is not the non-entering stage-B1 report plus zero B1C fields'),
    ('b1c-continue-must-stop',
     '\t\t\tgta4l_emmc_probe_report_b1(&host, &probe, &range,\n'
     '\t\t\t    &base, &w4, &w8, result, 1);\n',
     '\t\t\t(void)result;\n',
     'probe-b1c-pass',
     'the B1C pass report does not keep the verification stop'),
    ('b1c-wrong-baseline',
     '\t\tbase.block0_sum = gta4l_emmc_probe_sum(block0,\n\t\t    GTA4L_EMMC_BLOCK_BYTES);\n',
     '\t\tbase.block0_sum = 0u;\n',
     'probe-b1c-pass',
     'the B1C pass report lost the continuation marker or the re-read fields'),
    ('b1c-skip-restore',
     '\tgta4l_emmc_probe_write8(probe, GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t    GTA4L_EMMC_REG_HOST_CONTROL, (uint8_t)control);\n',
     '\t(void)control;\n\t(void)probe;\n',
     'probe-b1c-pass',
     'the B1C pass report lost the failed-verification tuple or the bus test'),
    ('b1c-wrong-marker',
     '\t\t\t\tw4.continued = 1u;\n',
     '\t\t\t\tw4.continued = 0u;\n',
     'probe-b1c-pass',
     'the B1C pass report lost the continuation marker or the re-read fields'),
    ('b1-1bit-skipped',
     '\t\tif (stage == 6) {\n',
     '\t\tif (0) {\n',
     'probe-b1-1bit-pass',
     'the 1-bit discriminator did not record its passing words'),
    ('b1-1bit-failure-continues',
     '\t\t\tif (result != GTA4L_EMMC_OK) {\n'
     '\t\t\t\tgta4l_emmc_probe_report_b1(&host, &probe,\n'
     '\t\t\t\t    &range, &base, &w4, &w8, result, 9);\n'
     '\t\t\t}\n',
     '\t\t\t(void)result;\n',
     'probe-b1-1bit-timeout',
     'the 1-bit timeout does not name the data timeout and the '
     'discriminator stop'),
    ('b1-1bit-wrong-stop',
     '\t\t\t\t    &range, &base, &w4, &w8, result, 9);\n',
     '\t\t\t\t    &range, &base, &w4, &w8, result, 1);\n',
     'probe-b1-1bit-timeout',
     'the 1-bit timeout does not name the data timeout and the '
     'discriminator stop'),
    ('dto-window-ignored',
     '\tif (host->bus_test_window_us != 0u) {\n'
     '\t\titerations = host->bus_test_window_us /\n'
     '\t\t    GTA4L_EMMC_POLL_STEP_US;\n'
     '\t\tif (iterations == 0u) {\n'
     '\t\t\titerations = 1u;\n'
     '\t\t}\n'
     '\t\tif (iterations > GTA4L_EMMC_MAX_BUS_TEST_ITERATIONS) {\n'
     '\t\t\titerations = GTA4L_EMMC_MAX_BUS_TEST_ITERATIONS;\n'
     '\t\t}\n'
     '\t} else {\n'
     '\t\titerations =\n'
     '\t\t    gta4l_emmc_poll_iterations(GTA4L_EMMC_DATA_TIMEOUT_US);\n'
     '\t}\n',
     '\titerations = gta4l_emmc_poll_iterations(GTA4L_EMMC_DATA_TIMEOUT_US);\n',
     'probe-dto-fired',
     'the extended data-timeout window was not used',
     'core'),
    ('dto-silent-accepted',
     '\t} else if (e1_dto != 0) {\n\t\tstop = 0;\n',
     '\t} else if (e1_dto == 2) {\n\t\tstop = 0;\n',
     'probe-dto-fired',
     'the fired row does not name the E1 timeout and stop 0'),
    ('dto-q-skipped',
     '\tdto.q_result = gta4l_emmc_read_block(host, 0u, block0);\n',
     '\tdto.q_result = GTA4L_EMMC_OK;\n\t(void)block0;\n',
     'probe-dto-q-fail',
     'the failed recovery read was not recorded'),
    ('dto-wrong-blocks',
     '\tdto.e1_blocks = GTA4L_EMMC_PROBE_DTO_BLOCKS;\n',
     '\tdto.e1_blocks = 21u;\n',
     'probe-dto-fired',
     'the E1 line lost the latched data-timeout record'),
    ('b2-no-readback',
     '\tif (readback != timing) {\n',
     '\tif (0) {\n',
     'probe-b2-ignored',
     'the ignored byte-185 switch does not keep stop 10',
     'core'),
    ('b2-div-accepted',
     '\tif ((b2.content_rc != GTA4L_EMMC_OK) ||\n'
     '\t    (b2.content.same == 0u)) {\n',
     '\tif (b2.content_rc != GTA4L_EMMC_OK) {\n',
     'probe-b2-content-div',
     'the HS divergence does not keep stop 12'),
    ('b2-no-revert',
     '\t\tb2.revert_rc = gta4l_emmc_set_hs_timing(host, 0u,\n'
     '\t\t    &b2.revert);\n'
     '\t\tgta4l_emmc_probe_report_b2(host, probe, &b2,\n'
     '\t\t    b2.switch_rc,\n',
     '\t\tb2.revert_rc = GTA4L_EMMC_OK;\n'
     '\t\tgta4l_emmc_probe_report_b2(host, probe, &b2,\n'
     '\t\t    b2.switch_rc,\n',
     'probe-b2-ignored',
     'the ignored switch did not revert cleanly'),
    ('b2-clock-stop',
     '\t\t    (b2.hs.clock_result != GTA4L_EMMC_OK) ? 11 : 10);\n',
     '\t\t    (b2.hs.clock_result != GTA4L_EMMC_OK) ? 10 : 10);\n',
     'probe-b2-clock-busy',
     'the busy RCG does not keep stop 11'),
    ('dto-arm-unmasked',
     '\tif (dto.arm != 0u) {\n',
     '\tif (0) {\n',
     'probe-dto-arm-ro',
     'a TIMEOUT_CONTROL readback that did not arm is not stop 12'),
    ('h10-width-step-skipped',
     '\th10.width_rc = gta4l_emmc_set_bus_width(host, 4u, &h10.width);\n',
     '\th10.width_rc = GTA4L_EMMC_OK;\n',
     'probe-h10-width-ignored',
     'the ignored H10 width switch does not keep stop 12'),
    ('h10-width-wrong-stop',
     '\t\tgta4l_emmc_probe_report_h10(host, probe, &h10,\n'
     '\t\t    h10.width_rc, (unwind != 0) ? unwind : 12);\n',
     '\t\tgta4l_emmc_probe_report_h10(host, probe, &h10,\n'
     '\t\t    h10.width_rc, (unwind != 0) ? unwind : 13);\n',
     'probe-h10-width-ignored',
     'the ignored H10 width switch does not keep stop 12'),
    ('h10-silent-revert',
     '\th10->width_revert_rc = gta4l_emmc_set_bus_width(host, 1u,\n'
     '\t    &h10->width_revert);\n',
     '\th10->width_revert_rc = GTA4L_EMMC_OK;\n',
     'probe-h10-content-div',
     'the H10 divergence run did not revert the width'),
    ('h10-content-skipped',
     '\th10.content_rc = gta4l_emmc_probe_b1_content(host, range,\n'
     '\t    &h10.content, 0);\n',
     '\th10.content_rc = GTA4L_EMMC_OK;\n',
     'probe-h10-pass',
     'the H10 pass does not name its stop'),
    ('h10-bustest-continues',
     '\t\tgta4l_emmc_probe_report_h10(host, probe, &h10, rc, 13);\n'
     '\t}\n',
     '\t\t(void)rc;\n\t}\n',
     'probe-h10-bustest-timeout',
     'the H10 bus-test timeout does not keep stop 13'),
    ('h11-w4-step-skipped',
     '\th11.w4_rc = gta4l_emmc_set_bus_width(host, 4u, &h11.w4);\n',
     '\th11.w4_rc = GTA4L_EMMC_OK;\n',
     'probe-h11-w4-replay',
     'the H11 W4 replay did not reproduce the H10 tuple'),
    ('h11-w8-step-skipped',
     '\th11.w8_rc = gta4l_emmc_set_bus_width(host, 8u, &h11.w8);\n',
     '\th11.w8_rc = GTA4L_EMMC_OK;\n',
     'probe-h11-honored-pass',
     'the H11 honored pass does not name its stop'),
    ('h11-bustest-wrong-width',
     '\t\trc = gta4l_emmc_bus_test(host, 8u, &h11.bus_test);\n',
     '\t\trc = gta4l_emmc_bus_test(host, 4u, &h11.bus_test);\n',
     'probe-h11-honored-pass',
     'the H11 honored pass does not name its stop'),
    ('h11-silent-revert',
     '\th11->width_revert_rc = gta4l_emmc_set_bus_width(host, 1u,\n'
     '\t    &h11->width_revert);\n',
     '\th11->width_revert_rc = GTA4L_EMMC_OK;\n',
     'probe-h11-content-div',
     'the H11 divergence run did not revert the width'),
    ('h11-w8-wrong-stop',
     '\tunwind = gta4l_emmc_probe_h11_unwind(host, &h11);\n'
     '\tgta4l_emmc_probe_report_h11(host, probe, &h11,\n'
     '\t    h11.w8_rc, (unwind != 0) ? unwind : 12);\n',
     '\tunwind = gta4l_emmc_probe_h11_unwind(host, &h11);\n'
     '\tgta4l_emmc_probe_report_h11(host, probe, &h11,\n'
     '\t    h11.w8_rc, (unwind != 0) ? unwind : 13);\n',
     'probe-h11-w4-replay',
     'the H11 W4 replay does not keep stop 12'),
    ('h11-content-skipped',
     '\t\th11.content_rc = gta4l_emmc_probe_b1_content(host, range,\n'
     '\t\t    &h11.content, 0);\n',
     '\t\th11.content_rc = GTA4L_EMMC_OK;\n',
     'probe-h11-honored-pass',
     'the H11 honored pass does not name its stop'),
    ('h11-host-width-stuck',
     '\th11->width_revert_rc = gta4l_emmc_set_bus_width(host, 1u,\n'
     '\t    &h11->width_revert);\n',
     '\th11->width_revert_rc = gta4l_emmc_set_bus_width(host, 8u,\n'
     '\t    &h11->width_revert);\n',
     'probe-h11-honored-pass',
     'the H11 width revert did not restore 1-bit'),
    ('h11-width-ddr',
     '\tif (bits == 8u) {\n'
     '\t\treturn GTA4L_EMMC_EXT_CSD_BUS_WIDTH_8;\n'
     '\t}\n',
     '\tif (bits == 8u) {\n'
     '\t\treturn 6u;\n'
     '\t}\n',
     'probe-h11-honored-pass',
     'a stage 1..11 rung issued a byte-183 DDR variant',
     'core'),
    ('h12-wddr-step-skipped',
     '\th12.wddr_rc = gta4l_emmc_set_bus_width_reserved4(host, &h12.wddr);\n',
     '\th12.wddr_rc = GTA4L_EMMC_OK;\n',
     'probe-h12-honored',
     'the honored H12 pass does not name its stop'),
    ('h12-wrong-stop',
     '\t\tgta4l_emmc_probe_h12_finish(host, probe, &h12, block0_sum,\n'
     '\t\t    13);\n',
     '\t\tgta4l_emmc_probe_h12_finish(host, probe, &h12, block0_sum,\n'
     '\t\t    12);\n',
     'probe-h12-honored',
     'the honored H12 pass does not name its stop'),
    ('h12-silent-revert',
     '\th12->width_revert_rc = gta4l_emmc_set_bus_width(host, 1u,\n'
     '\t    &h12->width_revert);\n',
     '\th12->width_revert_rc = GTA4L_EMMC_OK;\n',
     'probe-h12-honored',
     'the honored H12 pass did not revert the width'),
    ('h12-q-skipped',
     '\th12->q_result = gta4l_emmc_read_block(host, 0u, qblock);\n',
     '\th12->q_result = GTA4L_EMMC_OK;\n',
     'probe-h12-q-fail',
     'the failed H12 recovery read lost its core code'),
    ('h12-ddr-code-wrong',
     '\treturn gta4l_emmc_set_bus_width_value(host, 4u,\n'
     '\t    GTA4L_EMMC_EXT_CSD_BUS_WIDTH_RESERVED_4, result, 0);\n',
     '\treturn gta4l_emmc_set_bus_width_value(host, 4u,\n'
     '\t    5u, result, 0);\n',
     'probe-h12-refused-pass',
     'the H12 pass did not issue exactly one byte-183 value-4 attempt',
     'core'),
    ('h14-drain-skipped',
     '\tif (drain != 0) {\n'
     '\t\t/* Record-only here: the rung-2 caller owns the drain verdict,\n'
     '\t\t * and CMD14 still runs so the report carries both samples. */\n'
     '\t\t(void)gta4l_emmc_post_write_drain(host, drain);\n'
     '\t}\n',
     '\tif (drain != 0) {\n'
     '\t\t/* drain removed by mutation */\n'
     '\t\tdrain->status_result = GTA4L_EMMC_OK;\n'
     '\t}\n',
     'probe-h14-pass',
     'the H14 rung-2 pass does not keep stop 0',
     'core'),
    ('h14-stop-result-silent',
     '\t\tresult->stop_result = stop.result;\n',
     '\t\tresult->stop_result = GTA4L_EMMC_OK;\n',
     'probe-h14-cmd12-silent',
     'the silent H14 CMD12 does not keep stop 13',
     'core'),
    ('h14-status-response-lost',
     '\t\tresult->status_response = status.response;\n',
     '\t\tresult->status_response = 0u;\n',
     'probe-h14-pass',
     'the H14 rung-2 pass lost the completed CMD13 drain',
     'core'),
    ('h14-stop-verdict-skipped',
     '\t} else if (h14.drain.stop_result != GTA4L_EMMC_OK) {\n'
     '\t\trc = h14.drain.stop_result;\n'
     '\t\tstop = 13;\n'
     '\t} else if (h14.drain.status_result != GTA4L_EMMC_OK) {\n',
     '\t} else if (0) {\n'
     '\t\trc = h14.drain.stop_result;\n'
     '\t\tstop = 13;\n'
     '\t} else if (h14.drain.status_result != GTA4L_EMMC_OK) {\n',
     'probe-h14-cmd12-silent',
     'the silent H14 CMD12 does not keep stop 13'),
    ('h14-status-verdict-skipped',
     '\t} else if (h14.drain.status_result != GTA4L_EMMC_OK) {\n'
     '\t\trc = h14.drain.status_result;\n'
     '\t\tstop = 14;\n'
     '\t} else if ((h14.bus.test_result != GTA4L_EMMC_OK) ||\n',
     '\t} else if (0) {\n'
     '\t\trc = h14.drain.status_result;\n'
     '\t\tstop = 14;\n'
     '\t} else if ((h14.bus.test_result != GTA4L_EMMC_OK) ||\n',
     'probe-h14-cmd13-fail',
     'the H14 CMD13 failure does not keep stop 14'),
    ('h14-q-verdict-skipped',
     '\t} else if ((h14.bus.test_result != GTA4L_EMMC_OK) ||\n'
     '\t    (h14.q_result != GTA4L_EMMC_OK) ||\n'
     '\t    (h14.q_block0_sum != block0_sum)) {\n',
     '\t} else if ((h14.bus.test_result != GTA4L_EMMC_OK) ||\n'
     '\t    (block0_sum == 0u)) {\n',
     'probe-h14-q-fail',
     'the failed H14 recovery read does not keep stop 15'),
    ('h14-normal-stop',
     '\t} else if (h14.bus.write_result == GTA4L_EMMC_OK) {\n'
     '\t\trc = GTA4L_EMMC_OK;\n'
     '\t\tstop = 16;\n'
     '\t} else {\n',
     '\t} else if (h14.bus.write_result == GTA4L_EMMC_OK) {\n'
     '\t\trc = GTA4L_EMMC_OK;\n'
     '\t\tstop = 0;\n'
     '\t} else {\n',
     'probe-h14-normal',
     'the H14 non-wedge write does not keep stop 16'),
    ('h14-report-stop-code',
     '\t    h14->drain.stop_result, h14->drain.stop_response,\n',
     '\t    GTA4L_EMMC_OK, h14->drain.stop_response,\n',
     'probe-h14-cmd12-silent',
     'the silent H14 CMD12 lost its terminal tuple'),
    ('h9-scheduled-response-omitted',
     '\t\t\ttimeline_response0 = gta4l_emmc_read32(host,\n'
     '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t\t\t    GTA4L_EMMC_REG_RESPONSE);\n',
     '\t\t\ttimeline_response0 = 0u;\n',
     'probe-h9-update-no-cc',
     'the update-without-CC rung lost the updated sample',
     'core'),
    ('h9-last-schedule-retimed',
     '\t\t65536u, 99999u\n',
     '\t\t65536u, 99998u\n',
     'probe-h9-timeout',
     'the timeout rung lost the last scheduled sample',
     'core'),
    ('h9-scheduled-store-omitted',
     '\t\t\tgta4l_emmc_cmd3_timeline_store(host,\n'
     '\t\t\t    GTA4L_EMMC_CMD3_TRACE_SCHEDULED, step, normal,\n'
     '\t\t\t    error, timeline_response0, timeline_ticks);\n'
     '\t\t\thost->cmd3_timeline_cursor++;\n',
     '\t\t\thost->cmd3_timeline_cursor++;\n',
     'probe-h9-clean',
     'the clean rung lost the scheduled completion sample',
     'core'),
    ('h9-terminal-slot-overwritten',
     '\t\tslot = GTA4L_EMMC_CMD3_TIMELINE_ENTRIES - 1u;\n',
     '\t\tslot = 0u;\n',
     'probe-h9-clean',
     'the clean rung lost the scheduled completion sample',
     'core'),
    ('h9-r1-content-treated-as-success',
     '\tif ((result == GTA4L_EMMC_OK) &&\n'
     '\t    ((response[0] & GTA4L_EMMC_R1_ERROR_MASK) != 0)) {\n'
     '\t\treturn GTA4L_EMMC_ERR_CARD_STATUS;\n'
     '\t}\n',
     '\t/* R1 response content ignored by mutation. */\n'
     '\t(void)response;\n',
     'probe-h9-preexisting-r1',
     'a retained R1 error was not reported as a card status failure',
     'core'),
    ('h9-timeout-terminal-omitted',
     '\t\tif (timeline != 0) {\n'
     '\t\t\t/* The timeout verdict is kept even if this fresh\n'
     '\t\t\t * terminal NORMAL sample already shows late CC. */\n'
     '\t\t\ttimeline_ticks = gta4l_emmc_counter(host) -\n'
     '\t\t\t    poll_start;\n'
     '\t\t\tgta4l_emmc_cmd3_timeline_store(host,\n'
     '\t\t\t    GTA4L_EMMC_CMD3_TRACE_TERM_TIMEOUT, step,\n'
     '\t\t\t    normal, error, host->timeout_response[0],\n'
     '\t\t\t    timeline_ticks);\n'
     '\t\t}\n',
     '',
     'probe-h9-timeout',
     'the timeout rung lost the terminal timeout sample',
     'core'),
    ('h9-final-error-store-omitted',
     '\t\t\tif (timeline != 0) {\n'
     '\t\t\t\tgta4l_emmc_cmd3_timeline_store(host,\n'
     '\t\t\t\t    GTA4L_EMMC_CMD3_TRACE_TERM_ERROR, step,\n'
     '\t\t\t\t    normal, error, host->fail_response[0],\n'
     '\t\t\t\t    gta4l_emmc_counter(host) - poll_start);\n'
     '\t\t\t}\n',
     '',
     'probe-h9-error-after-read',
     'the final pre-clear check did not catch the error raised during '
     'the successful response read',
     'core'),
    ('h9-version-report-changed',
     '\t    GTA4L_EMMC_CMD3_TIMELINE_VERSION, rc, stop,\n',
     '\t    2u, rc, stop,\n',
     'probe-h9-clean',
     'the H9 report does not carry the timeline version'),
    ('h9-pre-response-omitted',
     '\t\tif (timeline != 0) {\n'
     '\t\t\t/* Four sequential raw-window reads after the status,\n'
     '\t\t\t * enable and present samples above. Not an atomic\n'
     '\t\t\t * snapshot; RESPONSE0..3 are read in register order. */\n'
     '\t\t\tgta4l_emmc_read_raw_response(host,\n'
     '\t\t\t    host->pre_cmd3_response);\n'
     '\t\t\thost->pre_cmd3_response_valid = 1;\n'
     '\t\t}\n',
     '\t\t/* pre-CMD3 response reads removed by mutation */\n',
     'probe-h9-clean',
     'the clean rung lost the pre-CMD3 response',
     'core'),
    ('h9-extra-response-read',
     '\t\t\ttimeline_response0 = gta4l_emmc_read32(host,\n'
     '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t\t\t    GTA4L_EMMC_REG_RESPONSE);\n',
     '\t\t\ttimeline_response0 = gta4l_emmc_read32(host,\n'
     '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t\t\t    GTA4L_EMMC_REG_RESPONSE);\n'
     '\t\t\t(void)gta4l_emmc_read32(host,\n'
     '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t\t\t    GTA4L_EMMC_REG_RESPONSE);\n',
     'probe-h9-clean',
     'the clean rung changed the exact RESPONSE read count',
     'core'),
    ('h9-later-command-clears-trace',
     '\tif (index == GTA4L_EMMC_CMD7) {\n'
     '\t\t/* Capture the state after the pre-command clear and before\n',
     '\tif (index == GTA4L_EMMC_CMD9) {\n'
     '\t\tgta4l_emmc_cmd3_timeline_reset(host);\n'
     '\t}\n'
     '\tif (index == GTA4L_EMMC_CMD7) {\n'
     '\t\t/* Capture the state after the pre-command clear and before\n',
     'probe-h9-later-fail',
     'the later-failure rung lost the preserved CMD3 trace',
     'core'),
    ('h9-frequency-zeroed',
     '\t    probe->log_count, (unsigned int)probe->platform.counter_hz,\n',
     '\t    probe->log_count, 0u,\n',
     'probe-h9-clean',
     'the clean rung lost the counter frequency'),
    ('h9-unused-valid-invented',
     '\t(h)->cmd3_timeline[i].valid, \\\n',
     '\t1u, \\\n',
     'probe-h9-cc-between',
     'the between-sample rung invented a valid unused slot'),
    ('h9-overflow-zeroed',
     '\t    count, host->cmd3_timeline_overflow,\n',
     '\t    count, 0u,\n',
     'report-width-h9',
     'the synthetic width H9 report lost the widest trace header'),
    ('h9-extra-status-read',
     '\t\t\ttimeline_response0 = gta4l_emmc_read32(host,\n'
     '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t\t\t    GTA4L_EMMC_REG_RESPONSE);\n',
     '\t\t\ttimeline_response0 = gta4l_emmc_read32(host,\n'
     '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t\t\t    GTA4L_EMMC_REG_RESPONSE);\n'
     '\t\t\t(void)gta4l_emmc_read16(host,\n'
     '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
     '\t\t\t    GTA4L_EMMC_REG_NORMAL_INT_STATUS);\n',
     'probe-h9-clean',
     'the clean rung changed the exact NORMAL status read count',
     'core'),
    ('h9-late-cc-promoted',
     '\t\tgta4l_emmc_clear_interrupts(host);\n'
     '\t\treturn GTA4L_EMMC_ERR_CMD_TIMEOUT;\n',
     '\t\tif ((normal & GTA4L_EMMC_INT_CC) != 0) {\n'
     '\t\t\treturn GTA4L_EMMC_OK;\n'
     '\t\t}\n'
     '\t\tgta4l_emmc_clear_interrupts(host);\n'
     '\t\treturn GTA4L_EMMC_ERR_CMD_TIMEOUT;\n',
     'probe-h9-late-cc',
     'a late terminal CC changed the timeout verdict',
     'core'),
]


def check_probe_source(text: str, platform_text: str) -> None:
    assert text.count(GUARD) == 1, 'failure-report clock guard is missing'
    guard = text.index(GUARD)
    first = text.index('GTA4L_EMMC_REG_MCI_VERSION', guard)
    assert first > guard, 'SDHCI diagnostics are not behind the clock guard'
    assert 'clocks=%u' in text, 'failure report does not show the clock state'
    assert '__asm__' not in text, 'probe still contains architectural asm'
    assert '__asm__' in platform_text, \
        'platform no longer contains the expected asm'


def rewrite_platform(text: str) -> str:
    for old, new in PLATFORM_REWRITES:
        assert text.count(old) == 1, f'platform rewrite not unique: {old[:48]!r}'
        text = text.replace(old, new)
    assert '__asm__' not in text, 'architectural instructions were not replaced'
    return text


def build(compiler: str, directory: Path, source: Path,
          platform: Path, core: Path = CORE) -> Path:
    output = directory / 'emmc-probe-sim'
    command = [
        compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
        f'-I{INCLUDE}', f'-I{ROOT / "package"}', f'-I{HARNESS.parent}',
        str(source), str(platform), str(core), str(HARNESS), '-o', str(output),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        raise SystemExit('probe simulation compile failed')
    return output


def run_oversized(binary: Path) -> bool:
    result = subprocess.run([str(binary), 'oversized-report'],
                            capture_output=True, text=True)
    combined = result.stdout + result.stderr
    if result.returncode != 2:
        print(combined)
        print('FAIL oversized report was not rejected with exit 2')
        return False
    if 'panic report truncated: 2099 bytes into 2048' not in combined:
        print(combined)
        print('FAIL oversized report did not report the expected rejection')
        return False
    return True


def run_width(binary: Path, scenario: str) -> bool:
    result = subprocess.run([str(binary), scenario], capture_output=True,
                            text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        print(f'FAIL width scenario {scenario}')
        return False
    match = re.search(
        rf'^PASS {re.escape(scenario)} bytes=(\d+) margin=(\d+)$',
        result.stdout, re.MULTILINE)
    if match is None:
        print(result.stdout)
        print(f'FAIL width scenario {scenario}: no measured PASS line')
        return False
    length, margin = (int(group) for group in match.groups())
    if length == 0:
        print(result.stdout)
        print(f'FAIL width scenario {scenario}: empty report')
        return False
    if length + 1 + margin != 2048:
        print(result.stdout)
        print(f'FAIL width scenario {scenario}: measurement does not add up '
              'to the 2048-byte panic buffer')
        return False
    return True


def run(binary: Path, scenario: str, expect_success: bool = True,
        expect_failure_text=None) -> bool:
    result = subprocess.run([str(binary), scenario], capture_output=True,
                            text=True)
    combined = result.stdout + result.stderr
    if expect_success:
        if result.returncode != 0:
            print(result.stdout)
            print(result.stderr)
            print(f'FAIL scenario {scenario}')
            return False
        if f'PASS {scenario}' not in result.stdout:
            print(result.stdout)
            print(f'FAIL scenario {scenario}: no PASS line')
            return False
        return True
    if result.returncode == 0:
        print(f'FAIL mutation accepted scenario {scenario}')
        return False
    if (expect_failure_text is not None and
            expect_failure_text not in combined):
        print(result.stdout)
        print(result.stderr)
        print(f'FAIL mutation {scenario}: expected {expect_failure_text!r}')
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler')
    args = parser.parse_args()

    check_probe_source(PROBE.read_text(), PLATFORM.read_text())

    compiler = args.compiler or shutil.which('gcc') or shutil.which('clang')
    if compiler is None:
        print('SKIP: no host C compiler')
        return 0

    with tempfile.TemporaryDirectory(prefix='gta4l-emmc-probe-') as tmp:
        directory = Path(tmp)
        source = PROBE.read_text() + '\n#include "report-width-fixture.c"\n'
        platform_source = rewrite_platform(PLATFORM.read_text())
        fixture = FIXTURE.read_text()
        probe = directory / 'Gta4lEmmcProbe.c'
        probe.write_text(source)
        platform = directory / 'Gta4lEmmcPlatform.c'
        platform.write_text(platform_source)
        (directory / 'report-width-fixture.c').write_text(fixture)
        binary = build(compiler, directory, probe, platform)

        for scenario in SCENARIOS:
            if scenario in WIDTH_SCENARIOS:
                ok = run_width(binary, scenario)
            else:
                ok = run(binary, scenario)
            if not ok:
                return 1

        if not run_oversized(binary):
            return 1

        core_text = CORE.read_text()
        for mutation in MUTATIONS:
            name, old, new, scenario, expected = mutation[:5]
            target = mutation[5] if len(mutation) > 5 else 'probe'
            mutated = directory / name
            mutated.mkdir()
            mutated_core = CORE
            if target == 'probe':
                assert source.count(old) == 1, (
                    f'{name} mutation is not unique')
                mutated_source = source.replace(old, new)
                mutated_fixture = fixture
            elif target == 'fixture':
                assert fixture.count(old) == 1, (
                    f'{name} mutation is not unique')
                mutated_source = source
                mutated_fixture = fixture.replace(old, new)
            elif target == 'core':
                assert core_text.count(old) == 1, (
                    f'{name} mutation is not unique')
                mutated_source = source
                mutated_fixture = fixture
                mutated_core = mutated / 'Gta4lEmmc.c'
                mutated_core.write_text(core_text.replace(old, new))
            else:
                raise AssertionError(
                    f'{name} mutation has an unknown target')
            probe = mutated / 'Gta4lEmmcProbe.c'
            probe.write_text(mutated_source)
            platform = mutated / 'Gta4lEmmcPlatform.c'
            platform.write_text(platform_source)
            (mutated / 'report-width-fixture.c').write_text(mutated_fixture)
            binary = build(compiler, mutated, probe, platform, mutated_core)
            if not run(binary, scenario, expect_success=False,
                       expect_failure_text=expected):
                print(f'mutation {name} was accepted')
                return 1

    print(f'PASS gta4l eMMC probe: {len(SCENARIOS)} scenarios, '
          f'{len(MUTATIONS)} mutation controls, '
          f'{len(WIDTH_SCENARIOS)} width-bound materializations, '
          f'1 oversized-report control')
    return 0


if __name__ == '__main__':
    sys.exit(main())
