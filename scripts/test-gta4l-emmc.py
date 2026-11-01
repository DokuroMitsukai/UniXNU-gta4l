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
    'read-late-fifo-error',
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
    'data-sector-reserved',
    'cmd-timeout',
    'cmd-crc',
    'cmd-crc-with-cc',
    'cmd-crc-snapshot',
    'cmd0-crc',
    'cmd1-crc',
    'cmd1-timeout-recovery-crc',
    'cmd1-not-ready-then-ready',
    'cmd1-not-ready-then-ready-crc',
    'cmd1-crc-retire',
    'cmd1-crc-retire-not-ready',
    'cmd1-crc-retire-short',
    'cmd1-clear-invalidates-window',
    'reinit-clears-windows',
    'cmd2-crc-distinct',
    'cmd2-crc-unchanged',
    'cmd2-crc-ocr-like',
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
    'cmd2-normalized',
    'cmd3-no-cc',
    'cmd3-late-cc',
    'cmd3-stale-cc',
    'cmd3-identify',
    'cmd3-enable-readback',
    'cmd9-late-error',
    'cmd7-tc-only',
    'cmd7-fresh-window',
    'cmd7-cc-then-tc',
    'cmd7-tc-then-cc',
    'cmd7-simultaneous',
    'cmd7-terminal-change',
    'cmd7-last-poll',
    'cmd7-delayed-release',
    'cmd7-late-tc',
    'cmd7-cc-only',
    'cmd7-neither',
    'cmd7-stale-status',
    'cmd7-busy-error',
    'cmd7-late-error',
    'cmd7-terminal-error',
    'cmd7-data-timeout-after-cc',
    'cmd7-data-crc-after-cc',
    'cmd0-irrelevant-tc',
    'cmd2-irrelevant-tc',
    'cmd3-irrelevant-tc',
    'cmd7-trace-overflow',
    'cmd7-trace-plateau',
    'cmd7-trace-return',
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
        'CMD1_CRC_RETIRE_US': '8u',
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
    assert re.search(
        r'host->cmd1_response_valid = 1;[\s\S]{0,400}?'
        r'gta4l_emmc_delay\(host,\s*\n\s*'
        r'GTA4L_EMMC_CMD1_CRC_RETIRE_US\);\s*\n\s*'
        r'return GTA4L_EMMC_OK;', core), \
        'CMD1 CRC-retire delay must follow ready success only'
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
    assert 'data_sector_size_code > 1u' in core, \
        'reserved data-sector-size rejection missing'
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
                  'cmd1_not_ready_remaining', 'cmd2_keep_response',
                  'cmd2_ocr_like_response', 'cmd_clear_count',
                  'cmd2_normalized_layout', 'cmd3_no_cc', 'cmd3_late_cc',
                  'cmd3_stale_cc', 'cmd3_identify_status',
                  'cmd7_cc_immediate', 'cmd7_tc_immediate',
                  'cmd7_keep_response', 'cmd7_fresh_response',
                  'cmd7_toggle_status', 'cmd7_trace_overflow',
                  'enable_readback_wrong', 'cmd_issue_clear',
                  'response_read_offset', 'cmd1_response_valid',
                  'pre_cmd2_response_valid',
                  'clear_response_on_normal_w1c',
                  'preserve_host_control2',
                  'apps_branch_fail', 'ahb_branch_fail',
                  'sim_note_sdhci_access', 'branch_writes',
                  'data_sector_size_code', 'data_late_error_words',
                  'data_late_error_emitted', 'cmd_late_error_index',
                  'cmd_late_error_emitted'):
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


def run(binary: Path, scenario: str, expect_success: bool = True,
        expected_reason=None) -> bool:
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
    if expected_reason is not None and expected_reason not in result.stderr:
        print(result.stderr)
        print(f'FAIL mutation {scenario}: expected diagnostic '
              f'{expected_reason!r}')
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
            ('cmd1-response-snapshot-missing',
             '\t\tgta4l_emmc_read_raw_response(host, host->cmd1_response);\n',
             '\t\t/* final CMD1 snapshot removed by mutation */\n',
             'Gta4lEmmc.c', (), 'cmd1-not-ready-then-ready'),
            ('cmd1-response-valid-missing',
             '\t\t\t\thost->cmd1_response_valid = 1;\n',
             '\t\t\t\t/* CMD1 window validity removed by mutation */\n',
             'Gta4lEmmc.c', (), 'cmd1-not-ready-then-ready'),
            ('cmd1-crc-retire-delay-removed',
             '\t\t\t\thost->cmd1_response_valid = 1;\n'
             '\t\t\t\t/* Timing candidate only: give the controller\'s\n'
             '\t\t\t\t * internal CRC source the downstream 8 us\n'
             '\t\t\t\t * retirement window after a successful CMD1,\n'
             '\t\t\t\t * before the next command. No retry, timeout,\n'
             '\t\t\t\t * clock, rail, command-flag or MMIO-width\n'
             '\t\t\t\t * change is part of this candidate. */\n'
             '\t\t\t\tgta4l_emmc_delay(host,\n'
             '\t\t\t\t    GTA4L_EMMC_CMD1_CRC_RETIRE_US);\n'
             '\t\t\t\treturn GTA4L_EMMC_OK;\n',
             '\t\t\t\thost->cmd1_response_valid = 1;\n'
             '\t\t\t\t/* CMD1 CRC-retire delay removed by mutation */\n'
             '\t\t\t\treturn GTA4L_EMMC_OK;\n',
             'Gta4lEmmc.c', (), 'cmd1-crc-retire'),
            ('cmd1-response-wrong-field',
             '\t\tgta4l_emmc_read_raw_response(host, host->cmd1_response);\n',
             '\t\tgta4l_emmc_read_raw_response(host, host->fail_response);\n',
             'Gta4lEmmc.c', (), 'cmd1-not-ready-then-ready'),
            ('pre-cmd2-snapshot-missing',
             '\t\tgta4l_emmc_read_raw_response(host, host->pre_cmd2_response);\n'
             '\t\thost->pre_cmd2_response_valid = 1;\n',
             '\t\t/* pre-CMD2 snapshot removed by mutation */\n',
             'Gta4lEmmc.c', (), 'cmd2-crc-distinct'),
            ('pre-cmd2-snapshot-retimed',
             '\tgta4l_emmc_clear_interrupts(host);\n'
             '\tif (index == GTA4L_EMMC_CMD2) {\n'
             '\t\t/* Capture the post-clear window immediately before CMD2 is\n'
             '\t\t * submitted, so a W1C side effect is visible separately from\n'
             '\t\t * the failed-command snapshot. Diagnostics only. */\n'
             '\t\tgta4l_emmc_read_raw_response(host, host->pre_cmd2_response);\n'
             '\t\thost->pre_cmd2_response_valid = 1;\n'
             '\t}\n',
             '\tif (index == GTA4L_EMMC_CMD2) {\n'
             '\t\t/* pre-CMD2 snapshot retimed before the status clear */\n'
             '\t\tgta4l_emmc_read_raw_response(host, host->pre_cmd2_response);\n'
             '\t\thost->pre_cmd2_response_valid = 1;\n'
             '\t}\n'
             '\tgta4l_emmc_clear_interrupts(host);\n',
             'Gta4lEmmc.c', (), 'cmd2-crc-distinct'),
            ('cmd1-response-retimed',
             '\t\tif (index == GTA4L_EMMC_CMD1) {\n'
             '\t\t\t/* Keep the raw window from this CMD1 before the status\n'
             '\t\t\t * clear can invalidate it. card_ready marks it valid only\n'
             '\t\t\t * for the final accepted ready response. Diagnostics\n'
             '\t\t\t * only. */\n'
             '\t\t\tgta4l_emmc_read_raw_response(host, host->cmd1_response);\n'
             '\t\t}\n',
             '\t\t/* CMD1 raw snapshot removed before the status clear by\n'
             '\t\t * mutation. */\n',
             'Gta4lEmmc.c', (), 'cmd1-clear-invalidates-window'),
            ('clear-failure-windows-missing',
             '\thost->cmd1_response[0] = 0;\n'
             '\thost->cmd1_response[1] = 0;\n'
             '\thost->cmd1_response[2] = 0;\n'
             '\thost->cmd1_response[3] = 0;\n'
             '\thost->pre_cmd2_response[0] = 0;\n'
             '\thost->pre_cmd2_response[1] = 0;\n'
             '\thost->pre_cmd2_response[2] = 0;\n'
             '\thost->pre_cmd2_response[3] = 0;\n'
             '\thost->cmd1_response_valid = 0;\n'
             '\thost->pre_cmd2_response_valid = 0;\n',
             '\t/* response-window clear removed by mutation */\n',
             'Gta4lEmmc.c', (), 'reinit-clears-windows'),
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
            ('cmd7-trace-last-stored',
             '\thost->cmd7_trace_prev_normal = normal;\n'
             '\thost->cmd7_trace_prev_error = error;\n'
             '\thost->cmd7_trace_prev_valid = 1;\n'
             '\tcount = host->cmd7_trace_count;\n'
             '\tif (count < GTA4L_EMMC_CMD7_TRACE_ENTRIES) {\n'
             '\t\thost->cmd7_trace[count].iteration = iteration;\n',
             '\tcount = host->cmd7_trace_count;\n'
             '\tif (count < GTA4L_EMMC_CMD7_TRACE_ENTRIES) {\n'
             '\t\thost->cmd7_trace_prev_normal = normal;\n'
             '\t\thost->cmd7_trace_prev_error = error;\n'
             '\t\thost->cmd7_trace_prev_valid = 1;\n'
             '\t\thost->cmd7_trace[count].iteration = iteration;\n',
             'Gta4lEmmc.c', (), 'cmd7-trace-return'),
            ('cmd7-terminal-normal-reused',
             '\t\tnormal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t    GTA4L_EMMC_REG_NORMAL_INT_STATUS);\n'
             '\t\tpresent = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t    GTA4L_EMMC_REG_PRESENT_STATE);\n'
             '\t\terror = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t    GTA4L_EMMC_REG_ERROR_INT_STATUS);\n',
             '\t\t/* Reused poll normal sample kept by mutation. */\n'
             '\t\tpresent = gta4l_emmc_read32(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t    GTA4L_EMMC_REG_PRESENT_STATE);\n'
             '\t\terror = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t    GTA4L_EMMC_REG_ERROR_INT_STATUS);\n',
             'Gta4lEmmc.c', (), 'cmd7-terminal-change'),
            ('cmd7-cc-only-success',
             '\tbusy = ((flags & GTA4L_EMMC_CMD_RESP_TYPE_MASK) ==\n'
             '\t    GTA4L_EMMC_CMD_RESP_SHORT_BUSY);\n',
             '\tbusy = 0;\n',
             'Gta4lEmmc.c', (), 'cmd7-cc-only'),
            ('cmd7-accept-tc-only',
             '\t\t\tif ((normal & GTA4L_EMMC_INT_CC) != 0) {\n'
             '\t\t\t\tcc_seen = 1;\n'
             '\t\t\t}\n',
             '\t\t\tif ((normal & (GTA4L_EMMC_INT_CC |\n'
             '\t\t\t    GTA4L_EMMC_INT_TC)) != 0) {\n'
             '\t\t\t\tcc_seen = 1;\n'
             '\t\t\t}\n',
             'Gta4lEmmc.c', (), 'cmd7-tc-only'),
            ('cmd7-clear-sticky-evidence',
             '\t\t\tif ((normal & GTA4L_EMMC_INT_TC) != 0) {\n'
             '\t\t\t\ttc_seen = 1;\n'
             '\t\t\t}\n',
             '\t\t\tif ((normal & GTA4L_EMMC_INT_TC) != 0) {\n'
             '\t\t\t\ttc_seen = 1;\n'
             '\t\t\t\tcc_seen = 0;\n'
             '\t\t\t}\n',
             'Gta4lEmmc.c', (), 'cmd7-cc-then-tc'),
            ('cmd7-terminal-error-final-sample-removed',
             '\t\t/* The terminal capture reads COMMAND, ARGUMENT and the raw\n'
             '\t\t * response window after the first error sample. Re-read the\n'
             '\t\t * error status after those reads so an error that becomes\n'
             '\t\t * visible during them is still evaluated before the status\n'
             '\t\t * clear. This final sample is the observation boundary; an\n'
             '\t\t * error asserted after it remains unobserved. */\n'
             '\t\terror = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t    GTA4L_EMMC_REG_ERROR_INT_STATUS);\n',
             '\t\t/* final terminal-read error sample removed by mutation */\n',
             'Gta4lEmmc.c', (), 'cmd7-terminal-error'),
            ('cmd7-ignore-final-error',
             '\t\tif ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {\n'
             '\t\t\tgta4l_emmc_record_failure(host, index, argument,\n'
             '\t\t\t    command, error, normal, present);\n'
             '\t\t\tgta4l_emmc_clear_interrupts(host);\n'
             '\t\t\treturn gta4l_emmc_map_error(error, 0);\n'
             '\t\t}\n',
             '\t\tif (0) {\n'
             '\t\t\tgta4l_emmc_record_failure(host, index, argument,\n'
             '\t\t\t    command, error, normal, present);\n'
             '\t\t\tgta4l_emmc_clear_interrupts(host);\n'
             '\t\t\treturn gta4l_emmc_map_error(error, 0);\n'
             '\t\t}\n',
             'Gta4lEmmc.c', (), 'cmd7-late-error'),
            ('cmd7-response-before-cc',
             '\t\t\tif ((cc_seen != 0) && (response_snapshot == 0) &&\n'
             '\t\t\t    (response != 0)) {\n',
             '\t\t\tif ((response_snapshot == 0) && (response != 0)) {\n',
             'Gta4lEmmc.c', (), 'cmd7-tc-only'),
            ('cmd7-budget-rearmed-after-cc',
             '\tfor (step = 0; step < iterations; step++) {\n'
             '\t\terror = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t    GTA4L_EMMC_REG_ERROR_INT_STATUS);\n'
             '\n'
             '\t\tif ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {\n'
             '\t\t\tnormal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t\t    GTA4L_EMMC_REG_NORMAL_INT_STATUS);\n'
             '\t\t\tpresent = gta4l_emmc_read32(host,\n'
             '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t\t    GTA4L_EMMC_REG_PRESENT_STATE);\n'
             '\t\t\tif (record_cmd7 != 0) {\n'
             '\t\t\t\tgta4l_emmc_note_cmd7_poll(host, step, normal,\n'
             '\t\t\t\t    error, poll_start);\n'
             '\t\t\t\thost->cmd7_poll_iterations = step + 1u;\n'
             '\t\t\t\thost->cmd7_poll_ticks =\n'
             '\t\t\t\t    gta4l_emmc_counter(host) - poll_start;\n'
             '\t\t\t\thost->cmd7_poll_capture_valid = 1;\n'
             '\t\t\t\tgta4l_emmc_capture_cmd7_terminal(host, normal,\n'
             '\t\t\t\t    error, present);\n'
             '\t\t\t}\n'
             '\t\t\tgta4l_emmc_record_failure(host, index, argument,\n'
             '\t\t\t    command, error, normal, present);\n'
             '\t\t\tgta4l_emmc_clear_interrupts(host);\n'
             '\t\t\treturn gta4l_emmc_map_error(error, 0);\n'
             '\t\t}\n',
             '\tfor (step = 0; (step < iterations) ||\n'
             '\t    ((cc_seen != 0) && (tc_seen == 0)); step++) {\n'
             '\t\terror = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t    GTA4L_EMMC_REG_ERROR_INT_STATUS);\n'
             '\n'
             '\t\tif ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {\n'
             '\t\t\tnormal = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t\t    GTA4L_EMMC_REG_NORMAL_INT_STATUS);\n'
             '\t\t\tpresent = gta4l_emmc_read32(host,\n'
             '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t\t    GTA4L_EMMC_REG_PRESENT_STATE);\n'
             '\t\t\tif (record_cmd7 != 0) {\n'
             '\t\t\t\tgta4l_emmc_note_cmd7_poll(host, step, normal,\n'
             '\t\t\t\t    error, poll_start);\n'
             '\t\t\t\thost->cmd7_poll_iterations = step + 1u;\n'
             '\t\t\t\thost->cmd7_poll_ticks =\n'
             '\t\t\t\t    gta4l_emmc_counter(host) - poll_start;\n'
             '\t\t\t\thost->cmd7_poll_capture_valid = 1;\n'
             '\t\t\t\tgta4l_emmc_capture_cmd7_terminal(host, normal,\n'
             '\t\t\t\t    error, present);\n'
             '\t\t\t}\n'
             '\t\t\tgta4l_emmc_record_failure(host, index, argument,\n'
             '\t\t\t    command, error, normal, present);\n'
             '\t\t\tgta4l_emmc_clear_interrupts(host);\n'
             '\t\t\treturn gta4l_emmc_map_error(error, 0);\n'
             '\t\t}\n',
             'Gta4lEmmc.c', (), 'cmd7-late-tc'),
            ('cmd7-loose-response-type',
             '\tbusy = ((flags & GTA4L_EMMC_CMD_RESP_TYPE_MASK) ==\n'
             '\t    GTA4L_EMMC_CMD_RESP_SHORT_BUSY);\n',
             '\tbusy = ((flags & GTA4L_EMMC_CMD_RESP_TYPE_MASK) != 0);\n',
             'Gta4lEmmc.c', (), 'success'),
            ('cmd7-early-command',
             '\tbusy = ((flags & GTA4L_EMMC_CMD_RESP_TYPE_MASK) ==\n'
             '\t    GTA4L_EMMC_CMD_RESP_SHORT_BUSY);\n',
             '\tbusy = 0;\n',
             'Gta4lEmmc.c', (), 'cmd7-delayed-release'),
            ('data-sector-reserved-accepted',
             '\tif (host->data_sector_size_code > 1u) {\n'
             '\t\t/* Only 0 (512-byte) and 1 (4 KiB) are defined. A reserved\n'
             '\t\t * encoding must not be read as 512-byte mode. */\n'
             '\t\tgta4l_emmc_log(host, GTA4L_EMMC_LOG_ERROR, 5,\n'
             '\t\t    host->data_sector_size_code);\n'
             '\t\treturn GTA4L_EMMC_ERR_UNSUPPORTED;\n'
             '\t}\n',
             '\t/* reserved data-sector-size rejection removed by mutation */\n',
             'Gta4lEmmc.c', (), 'data-sector-reserved'),
            ('data-final-error-sample-removed',
             '\t\t\t/* Re-evaluate the error latch after the response and\n'
             '\t\t\t * FIFO reads so an error that became visible during\n'
             '\t\t\t * them is still recorded before the status clear. An\n'
             '\t\t\t * error after this sample remains unobserved. */\n'
             '\t\t\terror = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t\t    GTA4L_EMMC_REG_ERROR_INT_STATUS);\n'
             '\t\t\tif ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {\n'
             '\t\t\t\tpresent = gta4l_emmc_read32(host,\n'
             '\t\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t\t\t    GTA4L_EMMC_REG_PRESENT_STATE);\n'
             '\t\t\t\tgta4l_emmc_record_failure(host, index, argument,\n'
             '\t\t\t\t    command, error, normal, present);\n'
             '\t\t\t\tgta4l_emmc_clear_interrupts(host);\n'
             '\t\t\t\treturn gta4l_emmc_map_error(error, 1);\n'
             '\t\t\t}\n'
             '\t\t\tgta4l_emmc_clear_interrupts(host);\n'
             '\t\t\treturn GTA4L_EMMC_OK;\n',
             '\t\t\tgta4l_emmc_clear_interrupts(host);\n'
             '\t\t\treturn GTA4L_EMMC_OK;\n',
             'Gta4lEmmc.c', (), 'read-late-fifo-error'),
            ('nonbusy-final-error-sample-removed',
             '\t\t/* The loop only observed the error latch before the response\n'
             '\t\t * reads. Evaluate it once more so an error asserted during\n'
             '\t\t * those reads is still recorded before the status clear. An\n'
             '\t\t * error after this sample remains unobserved. */\n'
             '\t\terror = gta4l_emmc_read16(host, GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t    GTA4L_EMMC_REG_ERROR_INT_STATUS);\n'
             '\t\tif ((error & GTA4L_EMMC_INT_ERR_MASK) != 0) {\n'
             '\t\t\tpresent = gta4l_emmc_read32(host,\n'
             '\t\t\t    GTA4L_EMMC_SPACE_SDHCI,\n'
             '\t\t\t    GTA4L_EMMC_REG_PRESENT_STATE);\n'
             '\t\t\tgta4l_emmc_record_failure(host, index, argument,\n'
             '\t\t\t    command, error, normal, present);\n'
             '\t\t\tgta4l_emmc_clear_interrupts(host);\n'
             '\t\t\treturn gta4l_emmc_map_error(error, 0);\n'
             '\t\t}\n',
             '\t\t/* non-busy final error sample removed by mutation */\n',
             'Gta4lEmmc.c', (), 'cmd9-late-error'),
        ]
        mutation_reasons = {
            'cmd1-response-snapshot-missing':
                'ready CMD1 raw window does not hold the OCR',
            'cmd1-response-valid-missing':
                'ready CMD1 raw window was not marked valid',
            'cmd1-response-wrong-field':
                'ready CMD1 raw window does not hold the OCR',
            'cmd1-response-retimed':
                'CMD1 window did not survive the later status clear',
            'pre-cmd2-snapshot-missing':
                'pre-CMD2 raw window was not marked valid',
            'pre-cmd2-snapshot-retimed':
                'pre-CMD2 window was not captured after the last status '
                'clear',
            'clear-failure-windows-missing':
                'reinit did not clear the CMD1 window validity',
            'cmd7-trace-last-stored':
                'return trace did not count the transition back to the '
                'last stored state',
            'cmd7-terminal-normal-reused':
                'CMD7 terminal sample lost the post-poll status change',
            'cmd7-cc-only-success':
                'cmd7-cc-only returned',
            'cmd7-accept-tc-only':
                'cmd7-tc-only returned',
            'cmd7-clear-sticky-evidence':
                'init result -7',
            'cmd7-ignore-final-error':
                'a later command followed the CMD7 failure',
            'cmd7-terminal-error-final-sample-removed':
                'cmd7-terminal-error returned 0',
            'cmd7-response-before-cc':
                'TC-only run read the response before CC',
            'cmd7-budget-rearmed-after-cc':
                'cmd7-late-tc returned 0',
            'cmd7-loose-response-type':
                'init result -14',
            'cmd7-early-command':
                'a command was submitted before the busy release',
            'data-final-error-sample-removed':
                'late FIFO error not reported',
        }
        for name, old, new, filename, extra, scenario in mutations:
            core = mutate(directory, name, old, new, filename)
            mutated = build(compiler, directory / name, f'emmc-{name}',
                            core=core, header_dir=directory / name,
                            extra_flags=extra)
            reason = mutation_reasons.get(name)
            if not run(mutated, scenario, expect_success=False,
                       expected_reason=reason):
                print(f'mutation {name} was accepted')
                return 1

    print(f'PASS gta4l eMMC core: {len(SCENARIOS)} scenarios, '
          f'{len(mutations)} mutation controls')
    return 0


if __name__ == '__main__':
    sys.exit(main())
