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
PROBE = ROOT / 'package' / 'Gta4lEmmcProbe.c'
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
    'pad-switch-off',
    'vendor-mclk-hs400',
    'vendor-hc-select-en',
    'version',
    'reset-timeout',
    'clock-timeout',
    'clock-branch-apps-fail',
    'clock-branch-ahb-fail',
    'rcg-busy',
    'power-off',
    'power-no-request',
    'power-stuck',
    'power-io-only',
    'power-delayed-bus',
    'power-bus-then-io',
    'power-wrong-io',
    'power-wrong-io-then-correct',
    'power-late-bus-only',
    'power-io-then-wrong',
    'power-opposite-io',
    'power-unchanged-1v8-bus-only',
    'power-wrong-io-on-ack',
    'power-churn',
    'data-sector-4k',
    'cmd-timeout',
    'cmd-crc',
    'cmd-crc-with-cc',
    'cmd-crc-snapshot',
    'cmd0-crc',
    'cmd1-crc',
    'cmd1-timeout-recovery-crc',
    'cmd1-timeout-recover',
    'cmd1-timeout-exhausted',
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
        'POWER_SETTLE_US': '10000u',
        'CARD_CLOCK_SETTLE_US': '1000u',
        'CMD0_DELAY_US': '1000u',
        'CMD0_POST_DELAY_US': '2000u',
        'BCR_DELAY_US': '200u',
        'BCR_BLOCK_RESET': '0x00000001u',
        'VENDOR_SPEC_POR': '0x00000A9Cu',
        'VENDOR3_PWRSAVE_DLL': '0x00000008u',
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

    # Audit regressions: long-response normalization, the bus-on completion
    # requirement, the 512-byte sector-size requirement, the explicit pad
    # policy and the documented 10 us poll step.
    assert 'gta4l_emmc_read_long_response' in core, \
        'SDHCI long-response normalization missing'
    assert 'bus_on_seen' in core, 'bus-on completion tracking missing'
    assert 'io_level' in core, 'latest IO-level tracking missing'
    assert 'io_request' in core, \
        'HOST_CONTROL2 IO-transition tracking missing'
    assert 'settled' in core, 'quiet-window completion tracking missing'
    assert 'io_desired' in core, 'desired IO level tracking missing'
    assert ('(io_request == 0) && (io_level == 0)') in core, \
        'unchanged VDD_180 must still reject an observed wrong level'
    assert ('GTA4L_EMMC_PWRCTL_ACK_BUS |\n\t\t\t    '
            'GTA4L_EMMC_PWRCTL_ACK_IO;') in core, \
        'BUS_ON must acknowledge both BUS and IO completion'
    assert '(io_level == io_desired)' in core, \
        'IO completion must compare the latest level with the desired level'
    io_low = core.index('io_level = GTA4L_EMMC_PWRCTL_IO_LOW;')
    io_high = core.index('io_level = GTA4L_EMMC_PWRCTL_IO_HIGH;',
                         io_low)
    assert io_low < io_high, \
        'IO_HIGH must follow IO_LOW in the Linux handler order'
    assert 'data_sector_size_4k != 0' in core, \
        '4 KiB data-sector rejection missing'
    assert 'host->pad_pwr_switch' in core, 'explicit pad policy missing'
    assert 'gta4l_emmc_delay(host, 1);' not in core, \
        'poll loops must use the documented 10 us step'
    assert core.count(
        'gta4l_emmc_delay(host, GTA4L_EMMC_POLL_STEP_US);') >= 5, \
        'poll loops must use the documented 10 us step'

    # The probe must use width-correct MMIO, stop on a stalled counter and
    # state the fixed-1.8 V pad policy explicitly.
    probe = PROBE.read_text()
    assert 'generic counter ' in probe and 'stalled (hz=' in probe, \
        'probe must abort on a stalled timing source'
    assert 'host.pad_pwr_switch = 1;' in probe, \
        'probe fixed-1.8 V pad policy missing'
    assert 'host->clocks_enabled = 1;' in core, \
        'core must record the verified branch-clock state'
    assert 'if (host.clocks_enabled != 0) {' in probe, \
        'failure diagnostics must be gated on the active branch clocks'
    assert 'clocks=%u' in probe, 'probe must report the clock-gate state'
    for name, width in (
            ('gta4l_emmc_probe_read8', 'uint8_t'),
            ('gta4l_emmc_probe_read16', 'uint16_t'),
            ('gta4l_emmc_probe_write8', 'uint8_t'),
            ('gta4l_emmc_probe_write16', 'uint16_t')):
        start = probe.index(name)
        end = probe.index('\n}\n', start)
        body = probe[start:end]
        assert f'*(volatile {width} *)' in body, \
            f'{name} does not use a {width} MMIO access'
        assert 'volatile uint32_t' not in body, \
            f'{name} uses a 32-bit access for a narrower register'

    # The simulator must model the register semantics the core depends on.
    for token in ('sim->normal_int', 'sim->error_int', 'sim->normal_enable',
                  'sim->error_enable', 'sim->pwr_status',
                  'sim->buffer_data', 'sim_command_ok', 'access_violation',
                  'command_error', 'SIM_CMD_DATA', 'SIM_PAD_PWR_SWITCH',
                  'SIM_POWER_SETTLE_US', 'power_io_only',
                  'power_delayed_bus', 'power_io_delayed', 'data_sector_4k',
                  'power_churn', 'io_delayed_emitted', 'io_poll_count',
                  'last_clear', 'power_wrong_io',
                  'power_io_wrong_then_correct', 'power_late_bus_only',
                  'power_io_then_wrong', 'power_opposite_io',
                  'power_io_on_ack', 'ack_io_injected', 'bcr_reset',
                  'SIM_VENDOR_SPEC_POR', 'SIM_VENDOR3_PWRSAVE_DLL',
                  'SIM_VENDOR_MCLK_SEL_DFLT', 'SIM_VENDOR_HC_SELECT_IN_MASK',
                  'SIM_CMD_CRC', 'SIM_CMD_INDEX', 'cmd_crc_index',
                  'cmd_crc_with_cc', 'clear_response_on_w1c',
                  'cmd1_attempts', 'cmd1_timeouts', 'fail_response',
                  'cmd0_time_us', 'cmd1_time_us', 'SIM_GCC_BCR',
                  'SIM_BCR_BLOCK_RESET', 'SIM_BCR_DELAY_US',
                  'bcr_assert_pending', 'bcr_pulse_us',
                  'sdhci_before_bcr_deassert', 'sdhci_before_clocks',
                  'dll_pdn_before_rst', 'dll_rst_written',
                  'dll_pdn_written', 'cmd1_timeouts_remaining',
                  'apps_branch_fail', 'ahb_branch_fail',
                  'sim_note_sdhci_access', 'branch_writes'):
        assert token in harness, f'harness missing {token}'
    assert 'previous & SIM_CTRL_VDD_180' in harness, \
        'simulator does not model the VDD_180 transition'
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
        # signalling bit, settle time, access width, response layout, power
        # completion or sector-size check must be rejected by the same
        # simulator that accepts the real core.
        mutations = [
            ('clock-cfg',
             'GTA4L_EMMC_RCG_400K_CFG            0x00002017u',
             'GTA4L_EMMC_RCG_400K_CFG            0x00000401u',
             'Gta4lEmmc.h', (), 'success'),
            ('clock-m',
             'GTA4L_EMMC_RCG_400K_M              0x00000001u',
             'GTA4L_EMMC_RCG_400K_M              0x00000002u',
             'Gta4lEmmc.h', (), 'success'),
            ('pad',
             'GTA4L_EMMC_SDC1_PAD_ACTIVE         0x00009FE4u',
             'GTA4L_EMMC_SDC1_PAD_ACTIVE         0x00001FE4u',
             'Gta4lEmmc.h', (), 'success'),
            ('cmd-data',
             'GTA4L_EMMC_CMD_DATA                0x0020u',
             'GTA4L_EMMC_CMD_DATA                0x0004u',
             'Gta4lEmmc.h', (), 'success'),
            ('pad-switch',
             'GTA4L_EMMC_VENDOR_PAD_PWR_SWITCH_EN 0x00008000u',
             'GTA4L_EMMC_VENDOR_PAD_PWR_SWITCH_EN 0x00000000u',
             'Gta4lEmmc.h', (), 'success'),
            ('vdd-180',
             'GTA4L_EMMC_HOST_CONTROL2_VDD_180   0x0008u',
             'GTA4L_EMMC_HOST_CONTROL2_VDD_180   0x0000u',
             'Gta4lEmmc.h', (), 'success'),
            ('settle',
             'GTA4L_EMMC_POWER_SETTLE_US         10000u',
             'GTA4L_EMMC_POWER_SETTLE_US         0u',
             'Gta4lEmmc.h', (), 'success'),
            ('card-clock-settle',
             'GTA4L_EMMC_CARD_CLOCK_SETTLE_US    1000u',
             'GTA4L_EMMC_CARD_CLOCK_SETTLE_US       0u',
             'Gta4lEmmc.h', (), 'success'),
            ('cmd0-post-delay',
             'GTA4L_EMMC_CMD0_POST_DELAY_US      2000u',
             'GTA4L_EMMC_CMD0_POST_DELAY_US         0u',
             'Gta4lEmmc.h', (), 'success'),
            ('bcr-reset',
             'GTA4L_EMMC_BCR_BLOCK_RESET         0x00000001u',
             'GTA4L_EMMC_BCR_BLOCK_RESET         0x00000000u',
             'Gta4lEmmc.h', (), 'success'),
            ('bcr-pulse',
             'GTA4L_EMMC_BCR_DELAY_US             200u',
             'GTA4L_EMMC_BCR_DELAY_US               0u',
             'Gta4lEmmc.h', (), 'success'),
            ('bcr-no-deassert',
             '\tgta4l_emmc_write32(host, GTA4L_EMMC_SPACE_GCC,\n'
             '\t    GTA4L_EMMC_GCC_SDCC1_BCR, 0);\n',
             '\t/* BCR deassert removed by mutation */\n',
             'Gta4lEmmc.c', (), 'success'),
            ('sdhci-before-clocks',
             '\tresult = gta4l_emmc_reset_block(host);\n'
             '\tif (result != GTA4L_EMMC_OK) {\n'
             '\t\treturn result;\n'
             '\t}\n'
             '\tresult = gta4l_emmc_enable_clocks(host);\n',
             '\tresult = gta4l_emmc_reset_block(host);\n'
             '\tif (result != GTA4L_EMMC_OK) {\n'
             '\t\treturn result;\n'
             '\t}\n'
             '\t(void)gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t    GTA4L_EMMC_REG_MCI_VERSION);\n'
             '\tresult = gta4l_emmc_enable_clocks(host);\n',
             'Gta4lEmmc.c', (), 'success'),
            ('dll-pdn-first',
             '\tconfig = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t    GTA4L_EMMC_REG_CORE_DLL_CONFIG);\n'
             '\tconfig |= GTA4L_EMMC_DLL_RST;\n'
             '\tgta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t    GTA4L_EMMC_REG_CORE_DLL_CONFIG, config);\n'
             '\tconfig = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t    GTA4L_EMMC_REG_CORE_DLL_CONFIG);\n'
             '\tconfig |= GTA4L_EMMC_DLL_PDN;\n',
             '\tconfig = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t    GTA4L_EMMC_REG_CORE_DLL_CONFIG);\n'
             '\tconfig |= GTA4L_EMMC_DLL_PDN;\n'
             '\tgta4l_emmc_write32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t    GTA4L_EMMC_REG_CORE_DLL_CONFIG, config);\n'
             '\tconfig = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t    GTA4L_EMMC_REG_CORE_DLL_CONFIG);\n'
             '\tconfig |= GTA4L_EMMC_DLL_RST;\n',
             'Gta4lEmmc.c', (), 'success'),
            ('cmd1-retire-missing',
             '\t\tgta4l_emmc_clear_failure(host);\n',
             '\t\t/* recovered CMD1 timeout retained by mutation */\n',
             'Gta4lEmmc.c', ('-Wno-unused-function',),
             'cmd1-timeout-recovery-crc'),
            ('crc-response-snapshot-missing',
             '\tgta4l_emmc_read_raw_response(host, host->fail_response);\n',
             '\t/* raw response snapshot removed by mutation */\n',
             'Gta4lEmmc.c', ('-Wno-unused-function',),
             'cmd-crc-snapshot'),
            ('clocks-flag-unset',
             '\thost->clocks_enabled = 0;\n',
             '\thost->clocks_enabled = 1;\n',
             'Gta4lEmmc.c', (), 'clock-branch-apps-fail'),
            ('vendor-por',
             'GTA4L_EMMC_VENDOR_SPEC_POR          0x00000A9Cu',
             'GTA4L_EMMC_VENDOR_SPEC_POR          0x00000000u',
             'Gta4lEmmc.h', (), 'success'),
            ('vendor3-pwrsave',
             'GTA4L_EMMC_VENDOR3_PWRSAVE_DLL      0x00000008u',
             'GTA4L_EMMC_VENDOR3_PWRSAVE_DLL      0x00000000u',
             'Gta4lEmmc.h', (), 'success'),
            ('vendor-mclk',
             'GTA4L_EMMC_VENDOR_HC_MCLK_SEL_DFLT  0x00000200u',
             'GTA4L_EMMC_VENDOR_HC_MCLK_SEL_DFLT  0x00000300u',
             'Gta4lEmmc.h', (), 'success'),
            ('dll-width',
             '\tconfig = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t    GTA4L_EMMC_REG_CORE_DLL_CONFIG);\n'
             '\tconfig |= GTA4L_EMMC_DLL_RST;',
             '\tconfig = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t    GTA4L_EMMC_REG_CORE_DLL_CONFIG);\n'
             '\tconfig |= GTA4L_EMMC_DLL_RST;',
             'Gta4lEmmc.c', (), 'success'),
            ('unaligned-reset',
             'if ((gta4l_emmc_read8(host, space, offset) & mask) == value) {',
             'if ((gta4l_emmc_read16(host, space, offset) & mask) == value) {',
             'Gta4lEmmc.c', (), 'success'),
            ('status-disabled',
             '\tgta4l_emmc_enable_status(host);',
             '\t/* status disabled mutation */',
             'Gta4lEmmc.c', ('-Wno-unused-function',), 'success'),
            ('long-response-raw',
             'offset = GTA4L_EMMC_REG_RESPONSE + (3u - index) * 4u;',
             'offset = GTA4L_EMMC_REG_RESPONSE + index * 4u;',
             'Gta4lEmmc.c', (), 'success'),
            ('no-bus-on-required',
             '\t\t\tif ((bus_on_seen != 0) &&\n'
             '\t\t\t    ((io_level == io_desired) ||\n'
             '\t\t\t    ((io_request == 0) && (io_level == 0)))) {\n'
             '\t\t\t\tsettled = 1;\n'
             '\t\t\t\tbreak;\n'
             '\t\t\t}\n',
             '\t\t\tif ((io_level == io_desired) ||\n'
             '\t\t\t    ((io_request == 0) && (io_level == 0))) {\n'
             '\t\t\t\tsettled = 1;\n'
             '\t\t\t\tbreak;\n'
             '\t\t\t}\n'
             '\t\t\t(void)bus_on_seen;\n',
             'Gta4lEmmc.c', (), 'power-io-only'),
            ('bus-ack-no-io',
             '\t\t\tack |= GTA4L_EMMC_PWRCTL_ACK_BUS |\n'
             '\t\t\t    GTA4L_EMMC_PWRCTL_ACK_IO;\n',
             '\t\t\tack |= GTA4L_EMMC_PWRCTL_ACK_BUS;\n',
             'Gta4lEmmc.c', (), 'signaling-3v3'),
            ('no-io-completion',
             '\t\t\tif ((bus_on_seen != 0) &&\n'
             '\t\t\t    ((io_level == io_desired) ||\n'
             '\t\t\t    ((io_request == 0) && (io_level == 0)))) {\n'
             '\t\t\t\tsettled = 1;\n'
             '\t\t\t\tbreak;\n'
             '\t\t\t}\n',
             '\t\t\tif (bus_on_seen != 0) {\n'
             '\t\t\t\tsettled = 1;\n'
             '\t\t\t\tbreak;\n'
             '\t\t\t}\n'
             '\t\t\t(void)io_request;\n',
             'Gta4lEmmc.c', (), 'power-bus-then-io'),
            ('wrong-io-accepted',
             '\t\t\tif ((bus_on_seen != 0) &&\n'
             '\t\t\t    ((io_level == io_desired) ||\n'
             '\t\t\t    ((io_request == 0) && (io_level == 0)))) {\n'
             '\t\t\t\tsettled = 1;\n'
             '\t\t\t\tbreak;\n'
             '\t\t\t}\n',
             '\t\t\tif ((bus_on_seen != 0) && (io_level != 0)) {\n'
             '\t\t\t\tsettled = 1;\n'
             '\t\t\t\tbreak;\n'
             '\t\t\t}\n'
             '\t\t\t(void)io_request;\n',
             'Gta4lEmmc.c', (), 'power-wrong-io'),
            ('unchanged-level-ignored',
             '\t\t\tif ((bus_on_seen != 0) &&\n'
             '\t\t\t    ((io_level == io_desired) ||\n'
             '\t\t\t    ((io_request == 0) && (io_level == 0)))) {\n'
             '\t\t\t\tsettled = 1;\n'
             '\t\t\t\tbreak;\n'
             '\t\t\t}\n',
             '\t\t\tif ((bus_on_seen != 0) &&\n'
             '\t\t\t    ((io_request == 0) ||\n'
             '\t\t\t    (io_level == io_desired))) {\n'
             '\t\t\t\tsettled = 1;\n'
             '\t\t\t\tbreak;\n'
             '\t\t\t}\n',
             'Gta4lEmmc.c', (), 'power-unchanged-1v8-bus-only'),
            ('first-level-wins',
             '\t\tif ((status & GTA4L_EMMC_PWRCTL_IO_HIGH) != 0) {\n'
             '\t\t\tack |= GTA4L_EMMC_PWRCTL_ACK_IO;\n'
             '\t\t\tio_level = GTA4L_EMMC_PWRCTL_IO_HIGH;\n'
             '\t\t}\n',
             '\t\tif ((status & GTA4L_EMMC_PWRCTL_IO_HIGH) != 0) {\n'
             '\t\t\tack |= GTA4L_EMMC_PWRCTL_ACK_IO;\n'
             '\t\t\tif (io_level == 0) {\n'
             '\t\t\t\tio_level = GTA4L_EMMC_PWRCTL_IO_HIGH;\n'
             '\t\t\t}\n'
             '\t\t}\n',
             'Gta4lEmmc.c', (), 'power-io-then-wrong'),
            ('opposite-bits-accepted',
             '\t\tif ((status & GTA4L_EMMC_PWRCTL_IO_HIGH) != 0) {\n',
             '\t\tif (((status & GTA4L_EMMC_PWRCTL_IO_HIGH) != 0) &&\n'
             '\t\t    ((status & GTA4L_EMMC_PWRCTL_IO_LOW) == 0)) {\n',
             'Gta4lEmmc.c', (), 'power-opposite-io'),
            ('exhaustion-success',
             '\tif (settled == 0) {\n'
             '\t\t/* The loop must end on a quiet window with both completions\n'
             '\t\t * observed; exhaustion is a power failure. */\n'
             '\t\tgta4l_emmc_log(host, GTA4L_EMMC_LOG_POWER, io_desired, io_level);\n'
             '\t\treturn GTA4L_EMMC_ERR_POWER;\n'
             '\t}\n',
             '\tif (settled == 0) {\n'
             '\t\tgta4l_emmc_log(host, GTA4L_EMMC_LOG_POWER, io_desired, io_level);\n'
             '\t}\n',
             'Gta4lEmmc.c', (), 'power-churn'),
            ('no-4k-reject',
             '\tif (host->data_sector_size_4k != 0) {\n'
             '\t\t/* The read path is fixed at 512-byte sectors. */\n'
             '\t\tgta4l_emmc_log(host, GTA4L_EMMC_LOG_ERROR, 5,\n'
             '\t\t    host->data_sector_size_4k);\n'
             '\t\treturn GTA4L_EMMC_ERR_UNSUPPORTED;\n'
             '\t}\n',
             '\t/* 4 KiB sector check removed by mutation */\n',
             'Gta4lEmmc.c', (), 'data-sector-4k'),
        ]
        for name, old, new, filename, extra, scenario in mutations:
            core = mutate(directory, name, old, new, filename)
            mutated = build(compiler, directory / name, f'emmc-{name}',
                            core=core, header_dir=directory / name,
                            extra_flags=extra)
            if not run(mutated, scenario, expect_success=False):
                print(f'mutation {name} was accepted')
                return 1

    print(f'PASS gta4l eMMC core: {len(SCENARIOS)} scenarios, '
          f'{len(mutations)} mutation controls')
    return 0


if __name__ == '__main__':
    sys.exit(main())
