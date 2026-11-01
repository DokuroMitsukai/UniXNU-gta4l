#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Check documented eMMC evidence counts against the executable sources.

The core and probe scenario/mutation counts are repeated in the driver record,
the CMD7 finding and the evidence baseline. This test derives them from the
test sources with ``ast`` and checks the five live count summaries listed in
``COUNT_PATTERNS``. It does not scan arbitrary prose or spelled-out counts, and
it does not rewrite historical audit snapshots. A table-driven self-test
mutates each numeric field and removes each summary to prove the checks reject
a stale or missing count.
"""
import ast
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE_TEST = ROOT / 'scripts/test-gta4l-emmc.py'
PROBE_TEST = ROOT / 'scripts/test-gta4l-emmc-probe.py'
DRIVER_DOC = ROOT / 'docs/emmc-driver.md'
CMD7_DOC = ROOT / 'docs/emmc-cmd7-timeout-finding.md'
BASELINE_DOC = ROOT / 'docs/evidence-baseline.md'


def list_length(tree, name, module_level, source):
    nodes = tree.body if module_level else list(ast.walk(tree))
    found = []
    for node in nodes:
        if not isinstance(node, ast.Assign):
            continue
        targets = [target.id for target in node.targets
                   if isinstance(target, ast.Name)]
        if name not in targets:
            continue
        if not isinstance(node.value, ast.List):
            raise AssertionError(
                f'{source.name}: {name} is not a list literal')
        found.append(node.value)
    if len(found) != 1:
        raise AssertionError(
            f'{source.name}: expected exactly one {name} list assignment, '
            f'found {len(found)}')
    return len(found[0].elts)


def source_counts():
    core_tree = ast.parse(CORE_TEST.read_text(), filename=str(CORE_TEST))
    probe_tree = ast.parse(PROBE_TEST.read_text(), filename=str(PROBE_TEST))
    core_scenarios = list_length(
        core_tree, 'SCENARIOS', True, CORE_TEST)
    core_mutations = list_length(
        core_tree, 'mutations', False, CORE_TEST)
    probe_scenarios = list_length(
        probe_tree, 'SCENARIOS', True, PROBE_TEST)
    probe_mutations = list_length(
        probe_tree, 'MUTATIONS', True, PROBE_TEST)
    return core_scenarios, core_mutations, probe_scenarios, probe_mutations


COUNT_PATTERNS = (
    (DRIVER_DOC,
     r'runs\s+(\d+)\s+scenarios\s+and\s+(\d+)\s+mutation\s+controls',
     'core', 'core scenario/mutation counts'),
    (DRIVER_DOC,
     r'Runs\s+the\s+probe\s+report,\s+(\d+)\s+scenarios\s+and\s+'
     r'(\d+)\s+mutation\s+controls',
     'probe', 'probe scenario/mutation counts'),
    (CMD7_DOC,
     r'Host\s+counts\s+are\s+(\d+)\s+core\s+scenarios\s+with\s+'
     r'(\d+)\s+mutation\s+controls\s+and\s+(\d+)\s+probe\s+scenarios\s+'
     r'with\s+(\d+)\s+mutation\s+controls',
     'both', 'core and probe counts'),
    (BASELINE_DOC,
     r'Current\s+native\s+eMMC\s+core\s+test\s*\|\s*PASS:\s*'
     r'(\d+)\s+simulator\s+scenarios\s+and\s+(\d+)\s+mutation\s+controls',
     'core', 'current core validation counts'),
    (BASELINE_DOC,
     r'Current\s+eMMC\s+probe\s+report\s+test\s*\|\s*PASS:\s*'
     r'(\d+)\s+scenarios\s+and\s+(\d+)\s+mutation\s+controls',
     'probe', 'current probe validation counts'),
)


def expected_for(kind, core_scenarios, core_mutations, probe_scenarios,
                 probe_mutations):
    core = (str(core_scenarios), str(core_mutations))
    probe = (str(probe_scenarios), str(probe_mutations))
    if kind == 'core':
        return core
    if kind == 'probe':
        return probe
    if kind == 'both':
        return core + probe
    raise AssertionError(f'unknown count summary kind: {kind}')


def require_all(path, text, pattern, expected, label):
    matches = re.findall(pattern, text)
    if not matches:
        raise AssertionError(f'{path.name}: missing documented {label}')
    for match in matches:
        actual = match if isinstance(match, tuple) else (match,)
        if actual != expected:
            raise AssertionError(
                f'{path.name}: documented {label} {actual!r}, expected '
                f'{expected!r}')


def check_documents(documents, core_scenarios, core_mutations,
                    probe_scenarios, probe_mutations):
    for path, pattern, kind, label in COUNT_PATTERNS:
        require_all(
            path, documents[path], pattern,
            expected_for(kind, core_scenarios, core_mutations,
                         probe_scenarios, probe_mutations),
            label)


def replace_group(document, pattern, group):
    matches = list(re.finditer(pattern, document))
    if not matches:
        raise AssertionError('self-test: live count phrase not found')
    pieces = []
    last = 0
    for match in matches:
        start, end = match.span(group)
        pieces.append(document[last:start])
        pieces.append(str(int(document[start:end]) + 1))
        last = end
    pieces.append(document[last:])
    return ''.join(pieces)


def self_test(documents, core_scenarios, core_mutations,
              probe_scenarios, probe_mutations):
    counts = (core_scenarios, core_mutations, probe_scenarios, probe_mutations)
    for path, pattern, kind, label in COUNT_PATTERNS:
        expected = expected_for(kind, *counts)
        for group in range(1, len(expected) + 1):
            stale_documents = dict(documents)
            stale_documents[path] = replace_group(
                documents[path], pattern, group)
            try:
                check_documents(stale_documents, *counts)
            except AssertionError as error:
                if label not in str(error):
                    raise AssertionError(
                        f'self-test: stale {label} field {group} in '
                        f'{path.name} failed for the wrong reason: {error}')
            else:
                raise AssertionError(
                    f'self-test: stale {label} field {group} in '
                    f'{path.name} was accepted')
        match = re.search(pattern, documents[path])
        if match is None:
            raise AssertionError('self-test: live count phrase not found')
        duplicate_documents = dict(documents)
        duplicate_documents[path] = (
            documents[path] + '\n' +
            replace_group(match.group(0), pattern, 1))
        try:
            check_documents(duplicate_documents, *counts)
        except AssertionError as error:
            if label not in str(error):
                raise AssertionError(
                    f'self-test: stale duplicate {label} in {path.name} '
                    f'failed for the wrong reason: {error}')
        else:
            raise AssertionError(
                f'self-test: stale duplicate {label} in {path.name} was '
                'accepted')
        missing_documents = dict(documents)
        missing_documents[path] = re.sub(pattern, '', documents[path])
        try:
            check_documents(missing_documents, *counts)
        except AssertionError as error:
            if f'missing documented {label}' not in str(error):
                raise AssertionError(
                    f'self-test: missing {label} in {path.name} failed for '
                    f'the wrong reason: {error}')
        else:
            raise AssertionError(
                f'self-test: missing {label} in {path.name} was accepted')


def main():
    core_scenarios, core_mutations, probe_scenarios, probe_mutations = (
        source_counts())
    counts = (core_scenarios, core_mutations, probe_scenarios, probe_mutations)
    if any(count <= 0 for count in counts):
        raise AssertionError(f'implausible source counts: {counts}')

    documents = {
        path: path.read_text()
        for path in (DRIVER_DOC, CMD7_DOC, BASELINE_DOC)
    }
    check_documents(documents, *counts)
    self_test(documents, *counts)

    print(f'PASS: documented counts match sources '
          f'(core {core_scenarios} scenarios, {core_mutations} mutation '
          f'controls; probe {probe_scenarios} scenarios, '
          f'{probe_mutations} mutation controls)')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
