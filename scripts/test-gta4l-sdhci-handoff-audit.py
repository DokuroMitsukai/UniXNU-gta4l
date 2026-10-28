#!/usr/bin/env python3
"""Static checks for the gta4l read-only SDHCI handoff audit.

The test is deliberately independent of the tablet. It proves that the source
contains only the allowlisted loads, that both mappings are gated, and that no
MMIO write, command or DMA path is present.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SDHCI = [
    ('PRESENT_STATE', 0x24, 32),
    ('HOST_CONTROL', 0x28, 8),
    ('POWER_CONTROL', 0x29, 8),
    ('CLOCK_CONTROL', 0x2C, 16),
    ('INT_STATUS', 0x30, 32),
    ('INT_ENABLE', 0x34, 32),
    ('HOST_CONTROL2', 0x3E, 16),
    ('CAPABILITIES', 0x40, 32),
    ('CAPABILITIES_1', 0x44, 32),
    ('MCI_VERSION', 0x318, 32),
    ('HOST_VERSION', 0xFE, 16),
    ('CORE_GENERICS', 0x320, 32),
    ('CORE_PWRCTL_STATUS', 0x240, 32),
    ('CORE_PWRCTL_MASK', 0x244, 32),
    ('CORE_VENDOR_SPEC', 0x20C, 32),
    ('CORE_VENDOR_SPEC_CAPS0', 0x21C, 32),
    ('CORE_DDR_CONFIG', 0x25C, 32),
    ('CORE_DLL_STATUS', 0x208, 32),
]
GCC = [
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


def source_and_header():
    for base in (ROOT / 'kits/gta4l', ROOT / 'package'):
        source = base / 'Gta4lSdhciHandoffAudit.c'
        header = base / 'Gta4lSdhciHandoffAudit.h'
        if source.is_file() and header.is_file():
            return source, header
    raise AssertionError('handoff audit source/header not found')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def validate(header_text, source_text):
    def macro(name):
        match = re.search(rf'^#define\s+GTA4L_SDHCI_AUDIT_{name}\s+'
                          rf'(0x[0-9A-Fa-f]+|\d+)u?\s*$', header_text, re.M)
        assert match, f'missing macro {name}'
        return int(match.group(1), 0)

    assert macro('SDHCI_BASE') == 0x04744000
    assert macro('SDHCI_SIZE') == 0x1000
    assert macro('GCC_BASE') == 0x01438000
    assert macro('GCC_SIZE') == 0x1000
    for name, offset, width in SDHCI + GCC:
        assert macro(f'REG_{name}') == offset, name
        assert macro(f'WIDTH_{name}') == width, name
        size = macro('SDHCI_SIZE') if name in {n for n, _, _ in SDHCI} else macro('GCC_SIZE')
        assert offset % (width // 8) == 0, f'misaligned {name}'
        assert offset + width // 8 <= size, f'{name} outside mapped window'
    assert not re.search(r'^#define\s+GTA4L_SDHCI_AUDIT_REG_\w+\s+0x50u?\s*$',
                         header_text, re.M), 'legacy MCI offset 0x50'

    code = strip_comments(source_text)
    calls = re.findall(
        r'gta4l_sdhci_audit_read(8|16|32)\(\s*(sdhci|gcc)\s*,\s*'
        r'GTA4L_SDHCI_AUDIT_REG_(\w+)\s*\)', code)
    observed = {}
    for width, base, name in calls:
        assert name not in observed, f'duplicate read: {name}'
        observed[name] = (base, int(width))
    expected = {name: ('sdhci', width) for name, _, width in SDHCI}
    expected.update({name: ('gcc', width) for name, _, width in GCC})
    assert observed == expected, observed
    for name in ('CORE_PWRCTL_CLEAR', 'CORE_PWRCTL_CTL'):
        assert name not in header_text, f'write-only control read: {name}'
        assert name not in code, f'write-only control read: {name}'

    assert len(re.findall(r'\bml_io_map\s*\(\s*GTA4L_SDHCI_AUDIT_SDHCI_BASE',
                          code)) == 1
    assert len(re.findall(r'\bml_io_map\s*\(\s*GTA4L_SDHCI_AUDIT_GCC_BASE',
                          code)) == 1
    assert 'GTA4L_SDHCI_AUDIT_SDHCI_SIZE' in code
    assert 'GTA4L_SDHCI_AUDIT_GCC_SIZE' in code
    assert code.index('PE_parse_boot_argn') < code.index(
        'ml_io_map(GTA4L_SDHCI_AUDIT_SDHCI_BASE')
    assert 'gta4l-sdhci-audit' in code
    assert 'enabled != 1' in code

    for token in ('|=', '&=', '+=', '-=', '++', '--'):
        assert token not in code, f'forbidden update token {token}'
    assert not re.search(r'\)\s*=\s*[^=]', code), 'pointer store'
    assert not re.search(r'\bsdhci_write\b|\bwrite[lq]\s*\(', code)
    for word in ('fifo', 'data_buffer', 'adma', 'cqhci'):
        assert word not in code.lower(), f'forbidden path: {word}'

    assert 'GTA4L-SDHCI AUDIT OK' in source_text
    assert 'GTA4L-SDHCI AUDIT REFUSED' in source_text
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
        needle = 'GTA4L_SDHCI_AUDIT_REG_HOST_VERSION);'
        bad = source_text.replace(
            needle,
            needle + '\n\t*(volatile uint32_t *)(sdhci + 0x24) = 0;',
            1)
        assert bad != source_text
        try:
            validate(header_text, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError('self-test did not reject an MMIO store')

        for replacement in ('0x38000u', '0x01u'):
            bad_header = re.sub(
                r'(#define\s+GTA4L_SDHCI_AUDIT_REG_GCC_SDCC1_BCR\s+)0x00u',
                rf'\g<1>{replacement}', header_text)
            assert bad_header != header_text
            try:
                validate(bad_header, source_text)
            except AssertionError:
                pass
            else:
                raise AssertionError(
                    f'self-test did not reject GCC offset {replacement}')
        print('PASS: audit allowlist, bounds and no-write self-test')
    else:
        print('PASS: audit allowlist, gate order and no-write checks')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as error:
        print(f'FAIL: {error}', file=sys.stderr)
        raise SystemExit(1)
