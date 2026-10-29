#!/usr/bin/env python3
"""Check documented SDHCI host-evidence counts against the test sources.

The simulator scenario and mutation counts and the oracle synthetic-control
count are duplicated in the host-init document, the decision memo and the
porting record. This test derives the counts from the executable sources and
requires every documented occurrence to agree, so a new scenario or control
cannot silently leave a stale count behind.
"""
import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIM_TEST = ROOT / 'scripts/test-gta4l-sdhci-host-init-sim.py'
ORACLE = ROOT / 'scripts/oracle-gta4l-sdhci-host-init.py'
HOST_DOC = ROOT / 'docs/sdhci-host-init.md'
MEMO = ROOT / 'docs/sdhc-decision-memo.md'
RECORD = ROOT / 'porting.toml'

ONES = (
    'zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight',
    'nine', 'ten', 'eleven', 'twelve', 'thirteen', 'fourteen', 'fifteen',
    'sixteen', 'seventeen', 'eighteen', 'nineteen')
TENS = ('', '', 'twenty', 'thirty', 'forty', 'fifty', 'sixty', 'seventy',
        'eighty', 'ninety')


def words(value):
    if value < 20:
        return ONES[value]
    if value < 100:
        tens, remainder = divmod(value, 10)
        suffix = f'-{ONES[remainder]}' if remainder else ''
        return f'{TENS[tens]}{suffix}'
    return str(value)


def load_sim():
    spec = importlib.util.spec_from_file_location('gta4l_sdhci_sim', SIM_TEST)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def number_pattern():
    values = sorted({words(value) for value in range(100)},
                    key=len, reverse=True)
    return r'(?:\d+|' + '|'.join(re.escape(value) for value in values) + r')'


NUM = number_pattern()


def require_all(path, text, pattern, expected, label):
    expected = tuple(expected) if isinstance(expected, tuple) else (expected,)
    matches = re.findall(pattern, text)
    if not matches:
        raise AssertionError(f'{path.name}: missing documented {label}')
    for match in matches:
        actual = match if isinstance(match, tuple) else (match,)
        if actual != expected:
            raise AssertionError(
                f'{path.name}: documented {label} {actual!r}, expected '
                f'{expected!r}')


def oracle_synthetics():
    match = re.search(r'^SYNTHETIC_CONTROLS = (\d+)$', ORACLE.read_text(),
                      re.MULTILINE)
    if match is None:
        raise AssertionError('oracle: SYNTHETIC_CONTROLS is missing')
    return int(match.group(1))


def check_documents(host, memo, record, scenarios, mutations, synthetics):
    for pattern, expected, label in (
            (rf'\b({NUM})\b scenarios pass', str(scenarios),
             'scenario count'),
            (rf'\b({NUM})\b mutations are applied', words(mutations),
             'mutation count'),
            (rf'\b({NUM})\b negative-control mutations are also applied',
             words(mutations), 'negative-control mutation count'),
            (rf'\b({NUM})\b synthetic traces', words(synthetics),
             'synthetic trace count'),
            (rf'\b({NUM})\b synthetic trace controls', words(synthetics),
             'synthetic trace control count')):
        require_all(HOST_DOC, host, pattern, expected, label)

    for pattern, expected, label in (
            (rf'\b({NUM})\b scenarios pass, \b({NUM})\b source mutations and '
             rf'\b({NUM})\b synthetic trace controls rejected',
             (str(scenarios), words(mutations), words(synthetics)),
             'gate-table count summary'),
            (rf'\b({NUM})\b scenarios, \b({NUM})\b mutations and the oracle',
             (str(scenarios), words(mutations)),
             'gate-table scenario and mutation summary')):
        require_all(MEMO, memo, pattern, expected, label)

    for pattern, expected, label in (
            (rf'\b({NUM})\b deterministic scenarios pass', str(scenarios),
             'deterministic scenario count'),
            (rf'\b({NUM})\b negative-control mutations are rejected by the '
             'simulator', words(mutations), 'negative-control mutation count'),
            (rf'\b({NUM})\b mutations plus \b({NUM})\b synthetic trace '
             'controls', (words(mutations), words(synthetics)),
             'mutation plus synthetic count'),
            (rf'\b({NUM})\b source mutations plus \b({NUM})\b synthetic '
             'trace controls', (words(mutations), words(synthetics)),
             'source mutation plus synthetic count'),
            (rf'\b({NUM})\b traces', str(scenarios), 'trace count')):
        require_all(RECORD, record, pattern, expected, label)


def self_test(host, memo, record, scenarios, mutations, synthetics):
    stale = memo.replace(
        f'{scenarios} scenarios, {words(mutations)} mutations and the oracle',
        f'{scenarios} scenarios, {words(mutations - 1)} mutations and the '
        'oracle', 1)
    if stale == memo:
        raise AssertionError('self-test: memo gate-table phrase not found')
    try:
        check_documents(host, stale, record, scenarios, mutations, synthetics)
    except AssertionError:
        pass
    else:
        raise AssertionError('self-test: stale memo count was accepted')


def main():
    sim = load_sim()
    scenarios = len(sim.TESTS)
    mutations = len(sim.MUTATIONS)
    synthetics = oracle_synthetics()

    if scenarios <= 0 or mutations <= 0 or synthetics <= 0:
        raise AssertionError(
            f'implausible source counts: {scenarios}/{mutations}/{synthetics}')

    host = HOST_DOC.read_text()
    memo = MEMO.read_text()
    record = RECORD.read_text()
    check_documents(host, memo, record, scenarios, mutations, synthetics)
    self_test(host, memo, record, scenarios, mutations, synthetics)

    print(f'PASS: documented counts match sources '
          f'({scenarios} scenarios, {mutations} mutations, '
          f'{synthetics} synthetic controls)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
