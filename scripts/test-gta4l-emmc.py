#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host-only checks for the gta4l SDHCI v5 / eMMC core.

The test compiles the production core against the simulated controller in
scripts/sim-gta4l-emmc/harness.c and runs every scenario. It also checks the
source policy: the core must use only the injected ops, must not contain DMA,
CQE, ICE, tuning or card-write commands, and must carry the documented 400 kHz
RCG and SDC1 pad values.

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
CORE = ROOT / 'package' / 'Gta4lEmmc.c'
HEADER = ROOT / 'package' / 'Gta4lEmmc.h'
HARNESS = ROOT / 'scripts' / 'sim-gta4l-emmc' / 'harness.c'

SCENARIOS = [
    'success',
    'read-arg',
    'read-out-of-range',
    'read-last-block',
    'read-without-init',
    'read-data-timeout',
    'read-data-crc',
    'read-no-tc',
    'read-no-brr',
    'read-repeat',
    'read-ext-csd',
    'signaling-3v3',
    'version',
    'reset-timeout',
    'clock-timeout',
    'rcg-busy',
    'power-off',
    'power-no-request',
    'power-stuck',
    'cmd-timeout',
    'cmd-crc',
    'cmd-index',
    'data-timeout-init',
    'data-crc-init',
    'no-tc-init',
    'no-brr-init',
    'card-not-ready',
    'bad-cid',
    'bad-csd',
    'cmd3-error',
    'small-card',
    'partition-access',
]

FORBIDDEN_CORE = [
    'ml_io_map',
    'uintptr_t',
    'panic(',
    'printf(',
    'kprintf(',
    'GTA4L_EMMC_CMD12',
    'GTA4L_EMMC_CMD18',
    'GTA4L_EMMC_CMD24',
    'GTA4L_EMMC_CMD25',
    'CQE',
    'cqhci',
    'ICE_CORE',
    'tuning',
]


def check_source() -> None:
    header = HEADER.read_text()
    core = CORE.read_text()
    harness = HARNESS.read_text()

    required = {
        'CMD_DATA': '0x0020u',
        'HOST_CONTROL2_VDD_180': '0x0008u',
        'VENDOR_PAD_PWR_SWITCH_EN': '0x00008000u',
        'VENDOR_PAD_PWR_SWITCH': '0x00010000u',
        'POWER_SETTLE_US': '5000u',
        'RCG_400K_CFG': '0x00002017u',
        'RCG_400K_M': '0x00000001u',
        'RCG_400K_N': '0x000000FCu',
        'RCG_400K_D': '0x000000FBu',
        'SDC1_PAD_ACTIVE': '0x00009FE4u',
    }
    for name, value in required.items():
        match = re.search(
            rf'^#define\s+GTA4L_EMMC_{name}\s+({value})\s*$', header, re.M)
        assert match, f'missing or wrong {name}'

    assert '#include <stdint.h>' in core, 'core must include stdint.h'
    includes = re.findall(r'^#include\s+[<"]([^>"]+)[>"]', core, re.M)
    assert includes == ['stdint.h', 'Gta4lEmmc.h'], includes

    for token in FORBIDDEN_CORE:
        assert token not in core, f'forbidden core token: {token}'

    assert 'gta4l_emmc_read_data' in core
    assert 'gta4l_emmc_wait8(' in core, 'byte-width reset poll missing'
    assert re.search(
        r'gta4l_emmc_wait8\(host, GTA4L_EMMC_SPACE_SDHCI,\s*\n\s*'
        r'GTA4L_EMMC_REG_SOFTWARE_RESET', core), \
        'software reset must be polled with byte accesses'
    assert 'GTA4L_EMMC_INT_TC' in core, 'transfer-complete check missing'
    assert 'GTA4L_EMMC_INT_BRR' in core, 'buffer-read-ready check missing'
    assert 'GTA4L_EMMC_REG_NORMAL_INT_ENABLE' in core, \
        'polling status enable missing'
    assert 'GTA4L_EMMC_REG_ERROR_INT_ENABLE' in core, \
        'error status enable missing'
    assert 'GTA4L_EMMC_REG_CORE_VENDOR_SPEC' in core, \
        'vendor pad-switch handling missing'
    assert 'GTA4L_EMMC_EXT_CSD_PARTITION_ACCESS_MASK' in core, \
        'partition-access validation missing'
    assert 'block >= (uint64_t)host->capacity_blocks' in core, \
        'capacity-bounded read check missing'
    assert 'block > 0xFFFFFFFFu' in core, 'block-address overflow check missing'
    assert 'GTA4L_EMMC_CMD1_TIMEOUT_US' in core
    assert 'GTA4L_EMMC_PWRCTL_BUS_OFF' in core
    assert 'GTA4L_EMMC_PWRCTL_ACK_BUS' in core
    assert 'GTA4L_EMMC_SDC1_PAD_ACTIVE' in core
    assert '*(' not in core.replace('*(uint8_t *)', ''), \
        'core contains a raw pointer dereference'

    # The simulator must model the register semantics the core depends on.
    for token in ('sim->normal_int', 'sim->error_int', 'sim->normal_enable',
                  'sim->error_enable', 'sim->pwr_status',
                  'sim->buffer_data', 'sim_command_ok', 'access_violation',
                  'command_error', 'SIM_CMD_DATA', 'SIM_PAD_PWR_SWITCH',
                  'SIM_POWER_SETTLE_US'):
        assert token in harness, f'harness missing {token}'
    assert 'GTA4L_EMMC_REG_BUFFER_DATA_PORT' in harness


def build(compiler: str, directory: Path, name: str,
          core: Path = CORE, header_dir: Path = ROOT / 'package',
          extra_flags=()) -> Path:
    output = directory / name
    command = [
        compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
        *extra_flags,
        f'-I{header_dir}', f'-I{ROOT / "scripts" / "sim-gta4l-emmc"}',
        str(core), str(HARNESS), '-o', str(output),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        raise SystemExit(f'compile failed: {name}')
    return output


def run(binary: Path, scenario: str, expect_success: bool = True) -> bool:
    result = subprocess.run([str(binary), scenario], capture_output=True,
                            text=True)
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
    return True


def mutate(directory: Path, name: str, old: str, new: str,
           filename: str = 'Gta4lEmmc.h') -> Path:
    target = directory / name
    target.mkdir(parents=True, exist_ok=True)
    core = target / 'Gta4lEmmc.c'
    header = target / 'Gta4lEmmc.h'
    shutil.copy2(CORE, core)
    shutil.copy2(HEADER, header)
    path = target / filename
    text = path.read_text()
    assert text.count(old) == 1, (name, filename, old, text.count(old))
    path.write_text(text.replace(old, new))
    return core


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler')
    args = parser.parse_args()

    check_source()

    compiler = args.compiler or shutil.which('gcc') or shutil.which('clang')
    if compiler is None:
        print('SKIP: no host C compiler')
        return 0

    with tempfile.TemporaryDirectory(prefix='gta4l-emmc-') as tmp:
        directory = Path(tmp)
        binary = build(compiler, directory, 'emmc-sim')
        failures = 0
        for scenario in SCENARIOS:
            if not run(binary, scenario):
                failures += 1
        if failures:
            return 1

        # Mutation controls: a wrong clock encoding, pad value, command bit,
        # signalling bit, settle time or access width must be rejected by the
        # same simulator that accepts the real core.
        mutations = [
            ('clock-cfg',
             'GTA4L_EMMC_RCG_400K_CFG            0x00002017u',
             'GTA4L_EMMC_RCG_400K_CFG            0x00000401u',
             'Gta4lEmmc.h', ()),
            ('clock-m',
             'GTA4L_EMMC_RCG_400K_M              0x00000001u',
             'GTA4L_EMMC_RCG_400K_M              0x00000002u',
             'Gta4lEmmc.h', ()),
            ('pad',
             'GTA4L_EMMC_SDC1_PAD_ACTIVE         0x00009FE4u',
             'GTA4L_EMMC_SDC1_PAD_ACTIVE         0x00001FE4u',
             'Gta4lEmmc.h', ()),
            ('cmd-data',
             'GTA4L_EMMC_CMD_DATA                0x0020u',
             'GTA4L_EMMC_CMD_DATA                0x0004u',
             'Gta4lEmmc.h', ()),
            ('pad-switch',
             'GTA4L_EMMC_VENDOR_PAD_PWR_SWITCH_EN 0x00008000u',
             'GTA4L_EMMC_VENDOR_PAD_PWR_SWITCH_EN 0x00000000u',
             'Gta4lEmmc.h', ()),
            ('vdd-180',
             'GTA4L_EMMC_HOST_CONTROL2_VDD_180   0x0008u',
             'GTA4L_EMMC_HOST_CONTROL2_VDD_180   0x0000u',
             'Gta4lEmmc.h', ()),
            ('settle',
             'GTA4L_EMMC_POWER_SETTLE_US         5000u',
             'GTA4L_EMMC_POWER_SETTLE_US         0u',
             'Gta4lEmmc.h', ()),
            ('unaligned-reset',
             'if ((gta4l_emmc_read8(host, space, offset) & mask) == value) {',
             'if ((gta4l_emmc_read16(host, space, offset) & mask) == value) {',
             'Gta4lEmmc.c', ()),
            ('status-disabled',
             '\tgta4l_emmc_enable_status(host);',
             '\t/* status disabled mutation */',
             'Gta4lEmmc.c', ('-Wno-unused-function',)),
        ]
        for name, old, new, filename, extra in mutations:
            core = mutate(directory, name, old, new, filename)
            mutated = build(compiler, directory / name, f'emmc-{name}',
                            core=core, header_dir=directory / name,
                            extra_flags=extra)
            if not run(mutated, 'success', expect_success=False):
                print(f'mutation {name} was accepted')
                return 1

    print(f'PASS gta4l eMMC core: {len(SCENARIOS)} scenarios, '
          f'{len(mutations)} mutation controls')
    return 0


if __name__ == '__main__':
    sys.exit(main())
