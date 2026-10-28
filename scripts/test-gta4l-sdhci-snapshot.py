#!/usr/bin/env python3
"""Static checks for the gta4l observation-only SDHCI snapshot.

The test is deliberately independent of the tablet. It proves that the source
contains only the allowlisted reads, that the gate precedes the mapping, and
that no MMIO write or command path is present.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

EXPECTED = {
    'PRESENT_STATE': (0x24, 32),
    'HOST_CONTROL': (0x28, 8),
    'POWER_CONTROL': (0x29, 8),
    'CLOCK_CONTROL': (0x2C, 16),
    'INT_STATUS': (0x30, 32),
    'INT_ENABLE': (0x34, 32),
    'HOST_CONTROL2': (0x3E, 16),
    'CAPABILITIES': (0x40, 32),
    'CAPABILITIES_1': (0x44, 32),
    'MCI_VERSION': (0x50, 32),
    'HOST_VERSION': (0xFE, 16),
}


def source_and_header():
    for base in (ROOT / 'kits/gta4l', ROOT / 'package'):
        source = base / 'Gta4lSdhciSnapshot.c'
        header = base / 'Gta4lSdhciSnapshot.h'
        if source.is_file() and header.is_file():
            return source, header
    raise AssertionError('snapshot source/header not found')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def validate(header_text, source_text):
    def macro(name):
        match = re.search(rf'^#define\s+GTA4L_SDHCI_{name}\s+'
                          rf'(0x[0-9A-Fa-f]+|\d+)u?\s*$', header_text, re.M)
        assert match, f'missing macro {name}'
        return int(match.group(1), 0)

    assert macro('SNAPSHOT_BASE') == 0x04744000
    assert macro('SNAPSHOT_SIZE') == 0x1000
    for name, (offset, width) in EXPECTED.items():
        assert macro(f'REG_{name}') == offset, name
        assert macro(f'WIDTH_{name}') == width, name

    code = strip_comments(source_text)
    calls = re.findall(
        r'gta4l_sdhci_read(8|16|32)\(\s*base\s*,\s*GTA4L_SDHCI_REG_(\w+)\s*\)',
        code)
    observed = {}
    for width, name in calls:
        assert name not in observed, f'duplicate read: {name}'
        observed[name] = int(width)
    assert observed == {name: width for name, (_, width) in EXPECTED.items()}, observed

    map_calls = re.findall(r'\bml_io_map\s*\(\s*GTA4L_SDHCI_SNAPSHOT_BASE', code)
    assert len(map_calls) == 1
    assert 'GTA4L_SDHCI_SNAPSHOT_SIZE' in code
    assert code.index('PE_parse_boot_argn') < code.index('ml_io_map(GTA4L_SDHCI_SNAPSHOT_BASE')
    assert 'gta4l-sdhci-snapshot' in code
    assert 'enabled != 1' in code

    for token in ('|=', '&=', '+=', '-=', '++', '--'):
        assert token not in code, f'forbidden update token {token}'
    assert not re.search(r'\)\s*=\s*[^=]', code), 'pointer store'
    assert not re.search(r'\bsdhci_write\b|\bwrite[lq]\s*\(', code)
    for word in ('fifo', 'data_buffer', 'command', 'adma', 'cqhci', 'ice_'):
        assert word not in code.lower(), f'forbidden path: {word}'

    assert 'GTA4L-SDHCI SNAP OK' in source_text
    assert 'GTA4L-SDHCI SNAP REFUSED' in source_text
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
        bad = source_text.replace(
            'host_version = gta4l_sdhci_read16(base, GTA4L_SDHCI_REG_HOST_VERSION);',
            'host_version = gta4l_sdhci_read16(base, GTA4L_SDHCI_REG_HOST_VERSION); '
            '*(volatile uint32_t *)(base + 0x24) = 0;')
        assert bad != source_text
        try:
            validate(header_text, bad)
        except AssertionError:
            pass
        else:
            raise AssertionError('self-test did not reject an MMIO store')
        print('PASS: allowlist and no-write self-test')
    else:
        print('PASS: allowlist, gate order and no-write checks')


if __name__ == '__main__':
    try:
        main()
    except AssertionError as error:
        print(f'FAIL: {error}', file=sys.stderr)
        raise SystemExit(1)
