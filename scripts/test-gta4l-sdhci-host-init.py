#!/usr/bin/env python3
"""Static checks for the gta4l bounded host-local SDHCI initialization.

The test is independent of the tablet. It proves that the source reads only
the allowlisted registers, writes only the four contract registers, has no
direct MMIO store outside the helpers, uses bounded polling, and refuses
conflicting diagnostics before mapping.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SDHCI_READ = [
    ('PRESENT_STATE', 0x24, 32),
    ('POWER_CONTROL', 0x29, 8),
    ('CLOCK_CONTROL', 0x2C, 16),
    ('SOFTWARE_RESET', 0x2F, 8),
    ('INT_ENABLE', 0x34, 32),
    ('SIGNAL_ENABLE', 0x38, 32),
    ('MCI_VERSION', 0x318, 32),
    ('CORE_GENERICS', 0x320, 32),
    ('CORE_PWRCTL_STATUS', 0x240, 32),
    ('CORE_PWRCTL_MASK', 0x244, 32),
    ('CORE_VENDOR_SPEC', 0x20C, 32),
    ('CORE_DDR_CONFIG', 0x25C, 32),
    ('CORE_DLL_STATUS', 0x208, 32),
]
GCC_READ = [
    ('GCC_SDCC1_BCR', 0x00, 32),
    ('GCC_SDCC1_APPS_CBCR', 0x04, 32),
    ('GCC_SDCC1_AHB_CBCR', 0x08, 32),
    ('GCC_SDCC1_ICE_CBCR', 0x0C, 32),
    ('GCC_SDCC1_APPS_CMD', 0x28, 32),
    ('GCC_SDCC1_APPS_CFG', 0x2C, 32),
    ('GCC_SDCC1_APPS_M', 0x30, 32),
    ('GCC_SDCC1_APPS_N', 0x34, 32),
    ('GCC_SDCC1_APPS_D', 0x38, 32),
]
WRITE = [
    ('INT_ENABLE', 0x34, 32),
    ('SIGNAL_ENABLE', 0x38, 32),
    ('SOFTWARE_RESET', 0x2F, 8),
    ('CLOCK_CONTROL', 0x2C, 16),
]


def source_and_header():
    for base in (ROOT / 'kits/gta4l', ROOT / 'package'):
        source = base / 'Gta4lSdhciHostInit.c'
        header = base / 'Gta4lSdhciHostInit.h'
        if source.is_file() and header.is_file():
            return source, header
    raise AssertionError('host-init source/header not found')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def validate(header_text, source_text):
    def macro(name):
        match = re.search(
            rf'^#define\s+GTA4L_SDHCI_HOST_INIT_{name}\s+'
            rf'(0x[0-9A-Fa-f]+|\d+)u?\s*$', header_text, re.M)
        assert match, f'missing macro {name}'
        return int(match.group(1), 0)

    assert macro('SDHCI_BASE') == 0x04744000
    assert macro('SDHCI_SIZE') == 0x1000
    assert macro('GCC_BASE') == 0x01438000
    assert macro('GCC_SIZE') == 0x1000
    for name, offset, width in SDHCI_READ + WRITE:
        assert macro(f'REG_{name}') == offset, name
        assert macro(f'WIDTH_{name}') == width, name
        assert offset % (width // 8) == 0, f'misaligned {name}'
        assert offset + width // 8 <= macro('SDHCI_SIZE'), name
    for name, offset, width in GCC_READ:
        assert macro(f'REG_{name}') == offset, name
        assert macro(f'WIDTH_{name}') == width, name
        assert offset % (width // 8) == 0, f'misaligned {name}'
        assert offset + width // 8 <= macro('GCC_SIZE'), name

    assert macro('POWER_BUS_POWER') == 0x01
    assert macro('CLOCK_INT_EN') == 0x0001
    assert macro('CLOCK_INT_STABLE') == 0x0002
    assert macro('CLOCK_CARD_EN') == 0x0004
    assert macro('SOFTWARE_RESET_ALL') == 0x01
    assert macro('TIMEOUT_TICKS') > 0
    assert macro('MAX_POLL_ITERATIONS') > 0
    for name in ('CBCR_CLK_OFF', 'CBCR_CLOCK_ENABLE',
                 'RCG_CMD_UPDATE', 'RCG_CMD_ROOT_OFF',
                 'RCG_SRC_SEL_MASK', 'RCG_SRC_SEL_GPLL6',
                 'RCG_SRC_DIV_MASK', 'RCG_SRC_DIV_EXPECTED',
                 'RCG_MODE_MASK'):
        assert f'GTA4L_SDHCI_HOST_INIT_{name}' in header_text, name
    for name in ('CORE_PWRCTL_CLEAR', 'CORE_PWRCTL_CTL', 'CORE_SW_RST'):
        assert name not in header_text, f'forbidden register {name}'

    code = strip_comments(source_text)
    reads = re.findall(
        r'gta4l_sdhci_host_init_read(8|16|32)\(\s*(sdhci|gcc)\s*,\s*'
        r'GTA4L_SDHCI_HOST_INIT_REG_(\w+)\s*\)', code)
    assert reads, 'no reads found'
    observed = {}
    for width, base, name in reads:
        entry = (base, int(width))
        if name in observed:
            assert observed[name] == entry, f'inconsistent read {name}'
        observed[name] = entry
    expected = {name: ('sdhci', width) for name, _, width in SDHCI_READ}
    expected.update({name: ('gcc', width) for name, _, width in GCC_READ})
    assert observed == expected, observed

    writes = re.findall(
        r'gta4l_sdhci_host_init_write(8|16|32)\(\s*(sdhci|gcc)\s*,\s*'
        r'GTA4L_SDHCI_HOST_INIT_REG_(\w+)', code)
    assert writes, 'no writes found'
    write_observed = {}
    for width, base, name in writes:
        assert base == 'sdhci', f'GCC write: {name}'
        entry = (base, int(width))
        if name in write_observed:
            assert write_observed[name] == entry, f'inconsistent write {name}'
        write_observed[name] = entry
    write_expected = {name: ('sdhci', width) for name, _, width in WRITE}
    assert write_observed == write_expected, write_observed
    for name in ('POWER_CONTROL', 'CORE_PWRCTL_STATUS', 'CORE_PWRCTL_MASK',
                 'GCC_SDCC1_BCR', 'GCC_SDCC1_APPS_CFG'):
        assert name not in write_observed, f'forbidden write {name}'

    # Count every dereference-cast and every volatile token. A typedef, a
    # reordered qualifier, or whitespace between `*` and `(` must not hide an
    # extra MMIO access: the production source has exactly six dereference-cast
    # helpers, each using volatile, and two __asm__ volatile instructions.
    assert 'typedef' not in code, 'typedef hides an MMIO access'
    volatile_tokens = re.findall(r'\bvolatile\b', code)
    assert len(volatile_tokens) == 8, (
        f'unexpected volatile token count: {len(volatile_tokens)}')
    dereference_casts = re.findall(r'\*\s*\(', code)
    assert len(dereference_casts) == 6, (
        f'unexpected dereference-cast count: {len(dereference_casts)}')
    assert not re.search(r'\w+\s*\[[^\]]*\]\s*=', code), (
        'pointer-index store hides an MMIO access')
    assert not re.search(
        r'\(\s*(?:u?int(?:8|16|32|64)_t|char|short|long|unsigned)\s*\*\s*\)',
        code), 'non-volatile pointer cast hides an MMIO access'
    assert 'clock_write = (uint16_t)((clock_after_reset &' in code
    assert '~GTA4L_SDHCI_HOST_INIT_CLOCK_CARD_EN' in code
    assert 'GTA4L_SDHCI_HOST_INIT_CLOCK_INT_EN' in code
    assert 'GTA4L_SDHCI_HOST_INIT_REG_CLOCK_CONTROL, clock_write' in code

    assert 'gta4l-sdhci-host-init' in code
    assert code.count('gta4l_sdhci_host_init_conflict()') >= 2
    assert code.index('PE_parse_boot_argn') < code.index(
        'ml_io_map(GTA4L_SDHCI_HOST_INIT_SDHCI_BASE')
    assert 'conflicting diagnostic' in code
    assert code.count(
        'ml_io_map(GTA4L_SDHCI_HOST_INIT_SDHCI_BASE') == 1
    assert code.count(
        'ml_io_map(GTA4L_SDHCI_HOST_INIT_GCC_BASE') == 1
    assert 'GTA4L_SDHCI_HOST_INIT_SDHCI_SIZE' in code
    assert 'GTA4L_SDHCI_HOST_INIT_GCC_SIZE' in code

    assert 'cntvct_el0' in code
    assert 'GTA4L_SDHCI_HOST_INIT_TIMEOUT_TICKS' in code
    assert 'GTA4L_SDHCI_HOST_INIT_MAX_POLL_ITERATIONS' in code
    assert 'while (1)' not in code
    assert 'for (;;)' not in code
    for token in ('fifo', 'adma', 'cqhci', 'data_buffer',
                  'sdhci_send_command', 'sdhci_prepare_dma'):
        assert token not in code.lower(), f'forbidden path: {token}'

    assert 'GTA4L-SDHCI HOST INIT REPORT' in source_text
    assert 'vendor state reported, not enforced' in source_text
    assert 'GTA4L-SDHCI HOST INIT REFUSED' in source_text
    assert 'GTA4L-SDHCI HOST INIT FAILED' in source_text
    assert 'stage blocked' in source_text
    assert 'kprintf' not in code
    assert 'panic(' in code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    source, header = source_and_header()
    header_text = header.read_text()
    source_text = source.read_text()
    validate(header_text, source_text)

    if args.self_test:
        needle = ('mci_version = gta4l_sdhci_host_init_read32(sdhci,\n'
                  '\t    GTA4L_SDHCI_HOST_INIT_REG_MCI_VERSION);')
        assert needle in source_text
        bad = source_text.replace(
            needle,
            needle + '\n\t*(volatile uint32_t *)(sdhci + 0x29) = 0;',
            1)
        try:
            validate(header_text, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError('self-test did not reject a direct MMIO store')

        # Qualifier order is not a loophole: `uint32_t volatile` is the same
        # type as `volatile uint32_t` and must also be rejected.
        bad = source_text.replace(
            needle,
            needle + '\n\t*(uint32_t volatile *)(sdhci + 0x29) = 0;',
            1)
        try:
            validate(header_text, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError(
                'self-test did not reject a reordered-qualifier MMIO store')

        # A typedef must not be able to hide a direct MMIO store either, even
        # when the typedef itself omits the volatile qualifier.
        bad = source_text.replace(
            needle,
            needle + '\n\ttypedef uint32_t audit_mmio_t;\n'
            '\t*(audit_mmio_t *)(sdhci + 0x29) = 0;',
            1)
        try:
            validate(header_text, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError(
                'self-test did not reject a typedef-hidden MMIO store')

        # Whitespace between the dereference operator and the cast must not
        # hide a direct MMIO store either.
        bad = source_text.replace(
            needle,
            needle + '\n\t* (uint32_t *)(sdhci + 0x29) = 0;',
            1)
        try:
            validate(header_text, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError(
                'self-test did not reject a whitespace-split MMIO store')

        # A pointer-index store must not slip past the cast and volatile
        # counts either; this is a source guardrail, not a proof of absence.
        bad = source_text.replace(
            needle,
            needle + '\n\tuint32_t *audit_ptr = (uint32_t *)(sdhci + 0x29);'
            '\n\taudit_ptr[0] = 0;',
            1)
        try:
            validate(header_text, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError(
                'self-test did not reject a pointer-index MMIO store')

        bad = source_text.replace(
            'GTA4L_SDHCI_HOST_INIT_REG_SOFTWARE_RESET,\n'
            '\t    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL',
            'GTA4L_SDHCI_HOST_INIT_REG_POWER_CONTROL,\n'
            '\t    GTA4L_SDHCI_HOST_INIT_SOFTWARE_RESET_ALL', 1)
        assert bad != source_text
        try:
            validate(header_text, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError('self-test did not reject a power write')

        bad_header = re.sub(
            r'(#define\s+GTA4L_SDHCI_HOST_INIT_TIMEOUT_TICKS\s+)192000u',
            r'\g<1>0u', header_text)
        assert bad_header != header_text
        try:
            validate(bad_header, source_text)
        except AssertionError:
            pass
        else:
            raise AssertionError('self-test did not reject a zero timeout')

        bad = source_text.replace('if (gta4l_sdhci_host_init_conflict())',
                                  'if (0)', 1)
        assert bad != source_text
        try:
            validate(header_text, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError('self-test did not reject a lost conflict check')

        print('PASS: host-init allowlist, write contract and bounded-wait '
              'self-test')
    else:
        print('PASS: host-init allowlist, write contract and bounded-wait checks')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as error:
        print(f'FAIL: {error}', file=sys.stderr)
        raise SystemExit(1)
