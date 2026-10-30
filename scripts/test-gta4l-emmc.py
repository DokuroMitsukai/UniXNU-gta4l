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
    'read-without-init',
    'read-data-timeout',
    'read-data-crc',
    'read-no-tc',
    'read-no-brr',
    'read-repeat',
    'read-ext-csd',
    'version',
    'reset-timeout',
    'clock-timeout',
    'rcg-busy',
    'power-off',
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
    'rca-zero',
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
    assert 'GTA4L_EMMC_INT_TC' in core, 'transfer-complete check missing'
    assert 'GTA4L_EMMC_INT_BRR' in core, 'buffer-read-ready check missing'
    assert 'block > 0xFFFFFFFFu' in core, 'block-address overflow check missing'
    assert 'GTA4L_EMMC_CMD1_TIMEOUT_US' in core
    assert 'GTA4L_EMMC_PWRCTL_BUS_OFF' in core
    assert 'GTA4L_EMMC_PWRCTL_ACK_BUS' in core
    assert 'GTA4L_EMMC_SDC1_PAD_ACTIVE' in core
    assert '*(' not in core.replace('*(uint8_t *)', ''), \
        'core contains a raw pointer dereference'

    # The simulator must model the register semantics the core depends on.
    for token in ('sim->normal_int', 'sim->error_int', 'sim->pwr_status',
                  'sim->buffer_data', 'sim_buffer_data_port'):
        if token == 'sim_buffer_data_port':
            assert 'GTA4L_EMMC_REG_BUFFER_DATA_PORT' in harness
            continue
        assert token in harness, f'harness missing {token}'


def build(compiler: str, directory: Path, name: str,
          core: Path = CORE, header_dir: Path = ROOT / 'package') -> Path:
    output = directory / name
    command = [
        compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
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


def mutate(directory: Path, name: str, old: str, new: str) -> Path:
    target = directory / name
    target.mkdir(parents=True, exist_ok=True)
    core = target / 'Gta4lEmmc.c'
    header = target / 'Gta4lEmmc.h'
    shutil.copy2(CORE, core)
    shutil.copy2(HEADER, header)
    text = header.read_text()
    assert text.count(old) == 1, (name, old, text.count(old))
    header.write_text(text.replace(old, new))
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

        # Mutation controls: a wrong clock encoding, pad value or M value must
        # be rejected by the same simulator that accepts the real core.
        mutations = [
            ('clock-cfg', 'GTA4L_EMMC_RCG_400K_CFG            0x00002017u',
             'GTA4L_EMMC_RCG_400K_CFG            0x00000401u'),
            ('clock-m', 'GTA4L_EMMC_RCG_400K_M              0x00000001u',
             'GTA4L_EMMC_RCG_400K_M              0x00000002u'),
            ('pad', 'GTA4L_EMMC_SDC1_PAD_ACTIVE         0x00009FE4u',
             'GTA4L_EMMC_SDC1_PAD_ACTIVE         0x00001FE4u'),
        ]
        for name, old, new in mutations:
            core = mutate(directory, name, old, new)
            mutated = build(compiler, directory / name, f'emmc-{name}',
                            core=core, header_dir=directory / name)
            if not run(mutated, 'success', expect_success=False):
                print(f'mutation {name} was accepted')
                return 1

    print(f'PASS gta4l eMMC core: {len(SCENARIOS)} scenarios, '
          f'{len(mutations)} mutation controls')
    return 0


if __name__ == '__main__':
    sys.exit(main())
