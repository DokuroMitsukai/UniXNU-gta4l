#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host-only checks for the gta4l eMMC probe failure report.

The test copies package/Gta4lEmmcProbe.c, replaces the generic-counter and
barrier instructions and the six MMIO helpers with the hooks in
scripts/sim-gta4l-emmc-probe/harness.c, and compiles the result against
minimal kernel-header stubs. The stage selection, conflict check, failure-report
clock guard, register offsets and panic strings remain the production code.

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
and require the block-read failure report and command stop. The report-width
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
    'probe-read-crc',
    'probe-read-timeout',
    'report-width-failure',
    'report-width-ident',
    'report-width-probe',
    'report-width-budget-control',
]

WIDTH_SCENARIOS = (
    'report-width-failure',
    'report-width-ident',
    'report-width-probe',
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

REWRITES = (
    ('#include "Gta4lEmmc.h"\n',
     '#include "Gta4lEmmc.h"\n' + HOOK_DECLS),
    ('\t__asm__ volatile("mrs %0, cntvct_el0" : "=r" (value));\n',
     '\tvalue = gta4l_emmc_probe_test_counter();\n'),
    ('\t__asm__ volatile("mrs %0, cntfrq_el0" : "=r" (value));\n',
     '\tvalue = gta4l_emmc_probe_test_counter_hz();\n'),
    ('\t__asm__ volatile("dsb sy" ::: "memory");\n',
     '\t/* barrier supplied by the host harness */\n'),
    ('''static void
gta4l_emmc_probe_delay_us(void *context, uint32_t microseconds)
{
\tstruct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;
\tuint64_t start;
\tuint64_t ticks;
\tuint64_t spins = 0;
\tuint64_t limit;
\tuint64_t hz = probe->counter_hz;

\tif (hz == 0u) {
\t\thz = 19200000u;
\t}
\tticks = (((uint64_t)microseconds * hz) + 999999u) / 1000000u;
\tlimit = ticks + 1000000u;
\tstart = gta4l_emmc_probe_counter();
\twhile ((gta4l_emmc_probe_counter() - start) < ticks) {
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
''', '''static void
gta4l_emmc_probe_delay_us(void *context, uint32_t microseconds)
{
\t(void)context;
\tgta4l_emmc_probe_test_delay_us(microseconds);
}
'''),
    ('''static uint8_t
gta4l_emmc_probe_read8(void *context, uint32_t space, uint32_t offset)
{
\tstruct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\treturn *(volatile uint8_t *)(probe->sdhci + offset);
\t}
\tif (space == GTA4L_EMMC_SPACE_GCC) {
\t\treturn *(volatile uint8_t *)(probe->gcc + offset);
\t}
\treturn *(volatile uint8_t *)(probe->tlmm + offset);
}
''', '''static uint8_t
gta4l_emmc_probe_read8(void *context, uint32_t space, uint32_t offset)
{
\t(void)context;
\treturn gta4l_emmc_probe_test_read8(space, offset);
}
'''),
    ('''static uint16_t
gta4l_emmc_probe_read16(void *context, uint32_t space, uint32_t offset)
{
\tstruct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\treturn *(volatile uint16_t *)(probe->sdhci + offset);
\t}
\tif (space == GTA4L_EMMC_SPACE_GCC) {
\t\treturn *(volatile uint16_t *)(probe->gcc + offset);
\t}
\treturn *(volatile uint16_t *)(probe->tlmm + offset);
}
''', '''static uint16_t
gta4l_emmc_probe_read16(void *context, uint32_t space, uint32_t offset)
{
\t(void)context;
\treturn gta4l_emmc_probe_test_read16(space, offset);
}
'''),
    ('''static uint32_t
gta4l_emmc_probe_read32(void *context, uint32_t space, uint32_t offset)
{
\tstruct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\treturn *(volatile uint32_t *)(probe->sdhci + offset);
\t}
\tif (space == GTA4L_EMMC_SPACE_GCC) {
\t\treturn *(volatile uint32_t *)(probe->gcc + offset);
\t}
\treturn *(volatile uint32_t *)(probe->tlmm + offset);
}
''', '''static uint32_t
gta4l_emmc_probe_read32(void *context, uint32_t space, uint32_t offset)
{
\t(void)context;
\treturn gta4l_emmc_probe_test_read32(space, offset);
}
'''),
    ('''static void
gta4l_emmc_probe_write8(void *context, uint32_t space, uint32_t offset,
    uint8_t value)
{
\tstruct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\t*(volatile uint8_t *)(probe->sdhci + offset) = value;
\t} else if (space == GTA4L_EMMC_SPACE_GCC) {
\t\t*(volatile uint8_t *)(probe->gcc + offset) = value;
\t} else {
\t\t*(volatile uint8_t *)(probe->tlmm + offset) = value;
\t}
}
''', '''static void
gta4l_emmc_probe_write8(void *context, uint32_t space, uint32_t offset,
    uint8_t value)
{
\t(void)context;
\tgta4l_emmc_probe_test_write8(space, offset, value);
}
'''),
    ('''static void
gta4l_emmc_probe_write16(void *context, uint32_t space, uint32_t offset,
    uint16_t value)
{
\tstruct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\t*(volatile uint16_t *)(probe->sdhci + offset) = value;
\t} else if (space == GTA4L_EMMC_SPACE_GCC) {
\t\t*(volatile uint16_t *)(probe->gcc + offset) = value;
\t} else {
\t\t*(volatile uint16_t *)(probe->tlmm + offset) = value;
\t}
}
''', '''static void
gta4l_emmc_probe_write16(void *context, uint32_t space, uint32_t offset,
    uint16_t value)
{
\t(void)context;
\tgta4l_emmc_probe_test_write16(space, offset, value);
}
'''),
    ('''static void
gta4l_emmc_probe_write32(void *context, uint32_t space, uint32_t offset,
    uint32_t value)
{
\tstruct gta4l_emmc_probe *probe = (struct gta4l_emmc_probe *)context;

\tif (space == GTA4L_EMMC_SPACE_SDHCI) {
\t\t*(volatile uint32_t *)(probe->sdhci + offset) = value;
\t} else if (space == GTA4L_EMMC_SPACE_GCC) {
\t\t*(volatile uint32_t *)(probe->gcc + offset) = value;
\t} else {
\t\t*(volatile uint32_t *)(probe->tlmm + offset) = value;
\t}
}
''', '''static void
gta4l_emmc_probe_write32(void *context, uint32_t space, uint32_t offset,
    uint32_t value)
{
\t(void)context;
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
]


def check_probe_source(text: str) -> None:
    assert text.count(GUARD) == 1, 'failure-report clock guard is missing'
    guard = text.index(GUARD)
    first = text.index('GTA4L_EMMC_REG_MCI_VERSION', guard)
    assert first > guard, 'SDHCI diagnostics are not behind the clock guard'
    assert 'clocks=%u' in text, 'failure report does not show the clock state'
    assert '__asm__' in text, 'probe no longer contains the expected asm'


def rewrite(text: str) -> str:
    for old, new in REWRITES:
        assert text.count(old) == 1, f'probe rewrite not unique: {old[:48]!r}'
        text = text.replace(old, new)
    assert '__asm__' not in text, 'architectural instructions were not replaced'
    text += '\n#include "report-width-fixture.c"\n'
    return text


def build(compiler: str, directory: Path, source: Path) -> Path:
    output = directory / 'emmc-probe-sim'
    command = [
        compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
        f'-I{INCLUDE}', f'-I{ROOT / "package"}', f'-I{HARNESS.parent}',
        str(source), str(CORE), str(HARNESS), '-o', str(output),
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

    check_probe_source(PROBE.read_text())

    compiler = args.compiler or shutil.which('gcc') or shutil.which('clang')
    if compiler is None:
        print('SKIP: no host C compiler')
        return 0

    with tempfile.TemporaryDirectory(prefix='gta4l-emmc-probe-') as tmp:
        directory = Path(tmp)
        source = rewrite(PROBE.read_text())
        fixture = FIXTURE.read_text()
        probe = directory / 'Gta4lEmmcProbe.c'
        probe.write_text(source)
        (directory / 'report-width-fixture.c').write_text(fixture)
        binary = build(compiler, directory, probe)

        for scenario in SCENARIOS:
            if scenario in WIDTH_SCENARIOS:
                ok = run_width(binary, scenario)
            else:
                ok = run(binary, scenario)
            if not ok:
                return 1

        if not run_oversized(binary):
            return 1

        for mutation in MUTATIONS:
            name, old, new, scenario, expected = mutation[:5]
            target = mutation[5] if len(mutation) > 5 else 'probe'
            if target == 'probe':
                assert source.count(old) == 1, (
                    f'{name} mutation is not unique')
                mutated_source = source.replace(old, new)
                mutated_fixture = fixture
            else:
                assert target == 'fixture', (
                    f'{name} mutation has an unknown target')
                assert fixture.count(old) == 1, (
                    f'{name} mutation is not unique')
                mutated_source = source
                mutated_fixture = fixture.replace(old, new)
            mutated = directory / name
            mutated.mkdir()
            probe = mutated / 'Gta4lEmmcProbe.c'
            probe.write_text(mutated_source)
            (mutated / 'report-width-fixture.c').write_text(mutated_fixture)
            binary = build(compiler, mutated, probe)
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
