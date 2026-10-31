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
counts. Four mutations insert an unguarded SDHCI read, replace the guard with an
unconditional branch, suppress a GCC report read and suppress an SDHCI report
read; each must be rejected for the expected reported reason, not merely by a
nonzero exit.

Nothing here touches a device, maps real MMIO or executes firmware.
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / 'package' / 'Gta4lEmmcProbe.c'
CORE = ROOT / 'package' / 'Gta4lEmmc.c'
HARNESS = ROOT / 'scripts' / 'sim-gta4l-emmc-probe' / 'harness.c'
INCLUDE = ROOT / 'scripts' / 'sim-gta4l-emmc-probe' / 'include'

SCENARIOS = [
    'stage-off',
    'map-fail',
    'conflict',
    'branch-apps',
    'branch-ahb',
    'version-fail',
    'clock-timeout',
]

GUARD = '\tif (host.clocks_enabled != 0) {\n'

HOOK_DECLS = '''
/* Host-test hooks supplied by scripts/sim-gta4l-emmc-probe/harness.c. */
uint64_t gta4l_emmc_probe_test_counter(void);
uint64_t gta4l_emmc_probe_test_counter_hz(void);
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
    ('\t\t__asm__ volatile("yield");\n',
     '\t\t/* yield supplied by the host harness */\n'),
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
    return text


def build(compiler: str, directory: Path, source: Path) -> Path:
    output = directory / 'emmc-probe-sim'
    command = [
        compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
        f'-I{INCLUDE}', f'-I{ROOT / "package"}',
        str(source), str(CORE), str(HARNESS), '-o', str(output),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        raise SystemExit('probe simulation compile failed')
    return output


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
        probe = directory / 'Gta4lEmmcProbe.c'
        probe.write_text(source)
        binary = build(compiler, directory, probe)

        for scenario in SCENARIOS:
            if not run(binary, scenario):
                return 1

        for name, old, new, scenario, expected in MUTATIONS:
            assert source.count(old) == 1, f'{name} mutation is not unique'
            mutated = directory / name
            mutated.mkdir()
            probe = mutated / 'Gta4lEmmcProbe.c'
            probe.write_text(source.replace(old, new))
            binary = build(compiler, mutated, probe)
            if not run(binary, scenario, expect_success=False,
                       expect_failure_text=expected):
                print(f'mutation {name} was accepted')
                return 1

    print(f'PASS gta4l eMMC probe: {len(SCENARIOS)} scenarios, '
          f'{len(MUTATIONS)} mutation controls')
    return 0


if __name__ == '__main__':
    sys.exit(main())
