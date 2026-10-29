#!/usr/bin/env python3
"""Independent access-policy oracle for the gta4l host-init probe.

This oracle derives its expectations from the probe header and the documented
write contract in docs/sdhci-host-init.md, not from the simulator's own
allowed_reads/allowed_writes arrays. It then runs the production source through
the instrumented harness with GTA4L_SDHCI_TRACE=1 and compares the captured
access history against that policy.

The oracle is host-only. It proves software access policy for the modelled
scenarios; it does not establish electrical safety or handoff state.
"""
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'package/Gta4lSdhciHostInit.h'
DOC = ROOT / 'docs/sdhci-host-init.md'
SIM_TEST = ROOT / 'scripts/test-gta4l-sdhci-host-init-sim.py'

TRACE_RE = re.compile(
    r'^TRACE v1 seq=(\d+) time=(\d+) space=(sdhci|gcc|other) '
    r'off=(0x[0-9A-Fa-f]+|0) width=(\d+) dir=([rw]) '
    r'value=(0x[0-9A-Fa-f]+|0) '
    r'phase=(\S+)$')

# Derived from the "No other write is permitted" list and the vendor-register
# section of docs/sdhci-host-init.md. A write to any of these is forbidden even
# if it is later restored.
FORBIDDEN_WRITES = {
    0x29: 'SDHCI_POWER_CONTROL',
    0x240: 'CORE_PWRCTL_STATUS',
    0x244: 'CORE_PWRCTL_MASK',
    0x248: 'CORE_PWRCTL_CLEAR',
    0x24c: 'CORE_PWRCTL_CTL',
    0x20c: 'CORE_VENDOR_SPEC',
}

# Derived from the stage order in docs/sdhci-host-init.md: interrupt
# suppression precedes reset, and reset precedes the internal-clock write.
ORDER = ((0x34, 0x2F), (0x38, 0x2F), (0x2F, 0x2C))

# Phase tags are defined by the probe stages in docs/sdhci-host-init.md.
WRITE_PHASES = {
    0x34: 'irq-suppress',
    0x38: 'irq-suppress',
    0x2F: 'reset',
    0x2C: 'clock',
}

# Every synthetic trace that must be rejected. The consistency test in
# scripts/test-gta4l-sdhci-evidence-counts.py reads this value and requires the
# documents to match it.
SYNTHETIC_CONTROLS = 41


def load_header():
    text = HEADER.read_text()
    registers = {}
    widths = {}
    for name, value in re.findall(
            r'^#define\s+GTA4L_SDHCI_HOST_INIT_REG_(\w+)\s+'
            r'(0x[0-9A-Fa-f]+)u?\s*$', text, re.M):
        registers[name] = int(value, 0)
    for name, value in re.findall(
            r'^#define\s+GTA4L_SDHCI_HOST_INIT_WIDTH_(\w+)\s+'
            r'(\d+)u?\s*$', text, re.M):
        widths[name] = int(value)
    max_poll = int(re.search(
        r'^#define\s+GTA4L_SDHCI_HOST_INIT_MAX_POLL_ITERATIONS\s+'
        r'(\d+)u?\s*$', text, re.M).group(1))
    reads = []
    for name, offset in registers.items():
        if name not in widths:
            continue
        space = 'gcc' if name.startswith('GCC_') else 'sdhci'
        reads.append((space, offset, widths[name], name))
    return registers, reads, max_poll


def load_constants():
    text = HEADER.read_text()
    values = {}
    for name, expression in re.findall(
            r'^#define\s+GTA4L_SDHCI_HOST_INIT_(\w+)\s+(.+?)\s*$',
            text, re.M):
        try:
            values[name] = eval(
                expression.replace('u', '').replace('U', ''),
                {'__builtins__': {}}, {})
        except (SyntaxError, NameError):
            continue
    return values


def split_cells(line):
    cells = []
    current = ''
    escaped = False
    for character in line:
        if escaped:
            current += character
            escaped = False
        elif character == '\\':
            escaped = True
        elif character == '|':
            cells.append(current)
            current = ''
        else:
            current += character
    cells.append(current)
    return [cell.strip() for cell in cells]


def load_write_contract():
    text = DOC.read_text()
    start = text.index('## Write contract')
    end = text.index('No other write is permitted', start)
    section = text[start:end]
    writes = {}
    for line in section.splitlines():
        if not line.lstrip().startswith('|'):
            continue
        cells = split_cells(line)
        if len(cells) < 6:
            continue
        offset_match = re.fullmatch(r'`0x([0-9A-Fa-f]+)`', cells[1])
        if offset_match is None or not cells[2].isdigit():
            continue
        offset = int(offset_match.group(1), 16)
        width = int(cells[2])
        write = cells[4]
        if write == '`0`':
            kind = ('const', 0)
        elif 'old' in write:
            kind = ('clock', None)
        else:
            value_match = re.search(r'`0x([0-9A-Fa-f]+)`', write)
            if value_match is None:
                raise AssertionError(
                    f'cannot derive a value for write {offset:#x}: {write!r}')
            kind = ('const', int(value_match.group(1), 16))
        writes[offset] = {'width': width, 'kind': kind, 'text': write}
    if not writes:
        raise AssertionError('no writes parsed from the documented contract')
    return writes


def load_sim_module():
    spec = importlib.util.spec_from_file_location('simtest', SIM_TEST)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_trace(binary, scenario):
    env = dict(os.environ)
    env['GTA4L_SDHCI_TRACE'] = '1'
    result = subprocess.run([str(binary), scenario], capture_output=True,
                            text=True, env=env)
    events = []
    declared = None
    for line in result.stdout.splitlines():
        if line.startswith('TRACE v1 scenario='):
            match = re.search(r'events=(\d+)$', line)
            if match:
                declared = int(match.group(1))
            continue
        match = TRACE_RE.match(line)
        if match:
            events.append({
                'seq': int(match.group(1)),
                'time': int(match.group(2)),
                'space': match.group(3),
                'offset': int(match.group(4), 16),
                'width': int(match.group(5)),
                'dir': match.group(6),
                'value': int(match.group(7), 16),
                'phase': match.group(8),
            })
    if declared is None:
        raise AssertionError(f'{scenario}: no trace header')
    if declared != len(events):
        raise AssertionError(
            f'{scenario}: declared {declared} events, parsed {len(events)}')
    return result, events


def check_value(kind, value, constants):
    if kind == 'mci-major-1':
        return (value >> 28) == 1
    if kind == 'mci-major-bad':
        return (value >> 28) != 1
    if kind == 'bus-power-off':
        return not value & constants['POWER_BUS_POWER']
    if kind == 'bus-power-on':
        return bool(value & constants['POWER_BUS_POWER'])
    if kind == 'card-clock-off':
        return not value & constants['CLOCK_CARD_EN']
    if kind == 'card-clock-on':
        return bool(value & constants['CLOCK_CARD_EN'])
    if kind == 'irq-zero':
        return value == 0
    if kind == 'irq-nonzero':
        return value != 0
    raise AssertionError(f'unknown value check {kind!r}')


def check_trace(name, events, reads, writes, max_poll, spec, prereqs,
                constants):
    errors = []
    read_rules = {(space, offset, width) for space, offset, width, _ in reads}
    write_rules = {(offset, rule['width'])
                   for offset, rule in writes.items()}
    by_name = {register_name: (space, offset)
               for space, offset, _width, register_name in reads}

    write_counts = {}
    first_write_index = None
    for index, event in enumerate(events):
        if event['seq'] != index:
            errors.append(f'seq {event["seq"]} at index {index}')
        if index and event['time'] < events[index - 1]['time']:
            errors.append(f'time went backwards at seq {event["seq"]}')
        rule = (event['space'], event['offset'], event['width'])
        if event['dir'] == 'r':
            if rule not in read_rules:
                errors.append(
                    f'forbidden read {rule} at seq {event["seq"]}')
        else:
            if first_write_index is None:
                first_write_index = index
            write_counts[event['offset']] = (
                write_counts.get(event['offset'], 0) + 1)
            if event['offset'] in FORBIDDEN_WRITES:
                errors.append(
                    f'forbidden write {FORBIDDEN_WRITES[event["offset"]]} '
                    f'at seq {event["seq"]}')
            elif event['space'] != 'sdhci':
                errors.append(
                    f'forbidden {event["space"]} write at seq {event["seq"]}')
            elif (event['offset'], event['width']) not in write_rules:
                errors.append(
                    f'write outside the contract {rule} at seq '
                    f'{event["seq"]}')
            else:
                kind, expected = writes[event['offset']]['kind']
                if kind == 'const' and event['value'] != expected:
                    errors.append(
                        f'write {event["offset"]:#x} value '
                        f'{event["value"]:#x}, expected {expected:#x}')
                elif kind == 'clock':
                    prior = [
                        earlier for earlier in events[:index]
                        if earlier['dir'] == 'r'
                        and earlier['space'] == 'sdhci'
                        and earlier['offset'] == event['offset']]
                    if not prior:
                        errors.append(
                            'clock write without a prior clock read')
                    else:
                        expected = ((prior[-1]['value'] & ~0x0004) | 0x0001)
                        if event['value'] != expected:
                            errors.append(
                                f'clock write {event["value"]:#06x}, '
                                f'expected {expected:#06x}')
                        if event['value'] & 0x0004:
                            errors.append('clock write sets the card clock')
                        if not event['value'] & 0x0001:
                            errors.append(
                                'clock write does not set the internal clock')
        if event['phase'] not in ('capture', 'irq-suppress', 'reset', 'clock',
                                  'other-write', 'gcc-write'):
            errors.append(f'unexpected phase {event["phase"]}')
        if event['dir'] == 'w' and event['offset'] in WRITE_PHASES:
            expected_phase = WRITE_PHASES[event['offset']]
            if event['phase'] != expected_phase:
                errors.append(
                    f'write {event["offset"]:#x} tagged phase '
                    f'{event["phase"]}, expected {expected_phase}')

    for offset, count in write_counts.items():
        if count > 1:
            errors.append(f'write {offset:#x} repeated {count} times')

    if first_write_index is not None:
        observed_reads = {
            (earlier['space'], earlier['offset'])
            for earlier in events[:first_write_index]
            if earlier['dir'] == 'r'}
        for space, offset in prereqs:
            if (space, offset) not in observed_reads:
                errors.append(
                    f'prerequisite read {space}:{offset:#x} missing before '
                    'the first write')

        if spec.get('check_preconditions', True):
            def first(space, offset):
                for earlier in events[:first_write_index]:
                    if (earlier['dir'] == 'r'
                            and earlier['space'] == space
                            and earlier['offset'] == offset):
                        return earlier['value']
                return None

            def require(register_name, predicate, message):
                space, offset = by_name[register_name]
                value = first(space, offset)
                if value is not None and not predicate(value):
                    errors.append(f'{register_name} precondition: {message}')

            require('MCI_VERSION', lambda value: (value >> 28) == 1,
                    'major version is not 1 before the first write')
            require('PRESENT_STATE',
                    lambda value: not value & (
                        constants['PRESENT_CMD_INHIBIT'] |
                        constants['PRESENT_DAT_INHIBIT']),
                    'command/data inhibit is set before the first write')
            require('POWER_CONTROL',
                    lambda value: not value & constants['POWER_BUS_POWER'],
                    'bus power is on before the first write')
            require('CLOCK_CONTROL',
                    lambda value: not value & constants['CLOCK_CARD_EN'],
                    'card clock is on before the first write')
            require('GCC_SDCC1_BCR', lambda value: value == 0,
                    'SDCC1 block reset is asserted before the first write')

            def branch_running(value):
                return ((value & (constants['CBCR_CLK_OFF'] |
                                  constants['CBCR_CLOCK_ENABLE'])) ==
                        constants['CBCR_CLOCK_ENABLE'])

            require('GCC_SDCC1_APPS_CBCR', branch_running,
                    'apps branch is not running before the first write')
            require('GCC_SDCC1_AHB_CBCR', branch_running,
                    'AHB branch is not running before the first write')
            require('GCC_SDCC1_ICE_CBCR', branch_running,
                    'ICE branch is not running before the first write')
            require('GCC_SDCC1_APPS_CMD',
                    lambda value: not value & (
                        constants['RCG_CMD_UPDATE'] |
                        constants['RCG_CMD_ROOT_OFF']),
                    'apps RCG is busy or off before the first write')

            def rcg_configured(value):
                return ((value & constants['RCG_SRC_SEL_MASK']) ==
                        constants['RCG_SRC_SEL_GPLL6'] and
                        (value & constants['RCG_SRC_DIV_MASK']) ==
                        constants['RCG_SRC_DIV_EXPECTED'] and
                        (value & constants['RCG_MODE_MASK']) == 0)

            require('GCC_SDCC1_APPS_CFG', rcg_configured,
                    'unexpected apps RCG configuration before the first '
                    'write')

    write_index = {}
    for index, event in enumerate(events):
        if event['dir'] == 'w':
            write_index.setdefault(event['offset'], index)
    for first, second in ORDER:
        if first in write_index and second in write_index:
            if write_index[first] > write_index[second]:
                errors.append(
                    f'ordering: write {first:#x} after {second:#x}')

    reset_write_index = write_index.get(0x2F)
    clock_write_index = write_index.get(0x2C)
    if clock_write_index is not None and reset_write_index is None:
        errors.append('clock write without a reset write')
    reset_clear_index = None
    if reset_write_index is not None:
        for index in range(reset_write_index + 1, len(events)):
            event = events[index]
            if (event['dir'] == 'r' and event['space'] == 'sdhci'
                    and event['offset'] == 0x2F and event['value'] == 0):
                reset_clear_index = index
                break
        if spec.get('require_reset_clear') and reset_clear_index is None:
            errors.append('reset did not self-clear after the reset write')
        if (clock_write_index is not None and
                (reset_clear_index is None or
                 reset_clear_index > clock_write_index)):
            errors.append('clock write before observing reset self-clear')

        if spec.get('require_irq_suppression'):
            for offset in (0x34, 0x38):
                earlier = [
                    (index, event) for index, event in enumerate(events)
                    if index < reset_write_index
                    and event['space'] == 'sdhci'
                    and event['offset'] == offset]
                earlier_reads = [
                    (index, event) for index, event in earlier
                    if event['dir'] == 'r']
                earlier_writes = [
                    index for index, event in earlier
                    if event['dir'] == 'w']
                if not earlier_reads:
                    errors.append(
                        f'interrupt enable {offset:#x} was not read before '
                        'reset')
                    continue
                if earlier_reads[0][1]['value'] != 0 and not earlier_writes:
                    errors.append(
                        f'interrupt enable {offset:#x} was nonzero before '
                        'reset but was not cleared')
                    continue
                confirmation_start = (earlier_writes[-1] if earlier_writes
                                      else earlier_reads[0][0])
                confirmed = [
                    event['value'] for index, event in earlier
                    if event['dir'] == 'r' and index > confirmation_start]
                if not confirmed:
                    errors.append(
                        f'interrupt enable {offset:#x} was not confirmed '
                        'clear before reset')
                elif any(value != 0 for value in confirmed):
                    errors.append(
                        f'interrupt enable {offset:#x} had a nonzero '
                        'readback before reset')

        observation_start = reset_clear_index
        observation_end = (clock_write_index if clock_write_index is not None
                           else len(events))
        if spec.get('post_reset_ok') or spec.get('post_reset_expect'):
            if observation_start is None:
                errors.append(
                    'post-reset observations have no reset self-clear '
                    'window')
            else:
                def observation(register_name):
                    space, offset = by_name[register_name]
                    for index in range(observation_start + 1,
                                       observation_end):
                        event = events[index]
                        if (event['dir'] == 'r'
                                and event['space'] == space
                                and event['offset'] == offset):
                            return event['value']
                    return None

                if spec.get('post_reset_ok'):
                    for register_name in ('MCI_VERSION', 'POWER_CONTROL',
                                          'CLOCK_CONTROL', 'INT_ENABLE',
                                          'SIGNAL_ENABLE'):
                        value = observation(register_name)
                        expected = {
                            'MCI_VERSION': 'mci-major-1',
                            'POWER_CONTROL': 'bus-power-off',
                            'CLOCK_CONTROL': 'card-clock-off',
                            'INT_ENABLE': 'irq-zero',
                            'SIGNAL_ENABLE': 'irq-zero',
                        }[register_name]
                        if value is None:
                            errors.append(
                                f'post-reset read {register_name} is '
                                'missing in the reset-to-clock window')
                        elif not check_value(expected, value, constants):
                            errors.append(
                                f'post-reset {register_name} value '
                                f'{value:#x} violates {expected}')

                for register_name, expected in spec.get(
                        'post_reset_expect', ()):
                    value = observation(register_name)
                    if value is None:
                        errors.append(
                            f'post-reset read {register_name} is missing '
                            'in the reset-to-clock window')
                    elif not check_value(expected, value, constants):
                        errors.append(
                            f'post-reset {register_name} value {value:#x} '
                            f'does not show {expected}')

    stable_mask = (constants['CLOCK_INT_EN'] |
                   constants['CLOCK_INT_STABLE'])
    confirmation_index = None
    if clock_write_index is not None and spec.get('require_clock_stable'):
        clock_indices = [
            index for index, event in enumerate(events)
            if index > clock_write_index and event['dir'] == 'r'
            and event['space'] == 'sdhci' and event['offset'] == 0x2C]
        for index in clock_indices:
            if events[index]['value'] & constants['CLOCK_CARD_EN']:
                errors.append(
                    'card clock observed set after the clock write')
        stable_indices = [
            index for index in clock_indices
            if (events[index]['value'] & stable_mask) == stable_mask]
        if not clock_indices:
            errors.append('no clock read after the clock write')
        elif not stable_indices:
            errors.append(
                'internal clock stable not observed after the clock write')
        else:
            completion = stable_indices[0]
            interrupted = [
                index for index in range(clock_write_index + 1, completion)
                if not (events[index]['dir'] == 'r'
                        and events[index]['space'] == 'sdhci'
                        and events[index]['offset'] == 0x2C)]
            if interrupted:
                errors.append(
                    'downstream access before the clock poll completed')
            if clock_indices[-1] == completion:
                errors.append(
                    'final clock confirmation is missing after the stable '
                    'poll read')
            else:
                confirmation = completion + 1
                if not (events[confirmation]['dir'] == 'r'
                        and events[confirmation]['space'] == 'sdhci'
                        and events[confirmation]['offset'] == 0x2C):
                    errors.append(
                        'final clock confirmation is not the next access '
                        'after the stable poll read')
                else:
                    confirmation_index = confirmation
                    if (events[confirmation]['value'] & stable_mask) != (
                            stable_mask):
                        errors.append(
                            'final clock observation is not stable after the '
                            'clock write')
                    if clock_indices[-1] != confirmation:
                        errors.append(
                            'unexpected clock read after the final '
                            'confirmation')

    if confirmation_index is not None:
        for register_name in spec.get('final_observations', ()):
            space, offset = by_name[register_name]
            present = any(
                event['dir'] == 'r' and event['space'] == space
                and event['offset'] == offset
                for event in events[confirmation_index + 1:])
            if not present:
                errors.append(
                    f'final {register_name} observation is missing after the '
                    'clock confirmation')

    if reset_write_index is not None:
        reset_end = (clock_write_index if clock_write_index is not None
                     else len(events))
        reset_stage_reads = sum(
            1 for event in events[reset_write_index + 1:reset_end]
            if event['dir'] == 'r' and event['space'] == 'sdhci'
            and event['offset'] == 0x2F)
        if reset_stage_reads > max_poll:
            errors.append(
                f'reset poll count {reset_stage_reads} exceeds {max_poll}')
    if clock_write_index is not None:
        clock_stage = [
            event for event in events[clock_write_index + 1:]
            if event['dir'] == 'r' and event['space'] == 'sdhci'
            and event['offset'] == 0x2C]
        stable_positions = [
            position for position, event in enumerate(clock_stage)
            if (event['value'] & stable_mask) == stable_mask]
        poll_reads = (stable_positions[0] + 1 if stable_positions
                      else len(clock_stage))
        if poll_reads > max_poll:
            errors.append(
                f'clock poll count {poll_reads} exceeds {max_poll}')

    for offset in spec.get('require_writes', ()):
        if offset not in write_index:
            errors.append(f'required write {offset:#x} is missing')
    for offset in spec.get('forbid_writes', ()):
        if offset in write_index:
            errors.append(f'forbidden write {offset:#x} is present')
    for offset, limit in spec.get('max_reads', {}).items():
        count = sum(1 for event in events
                    if event['dir'] == 'r' and event['space'] == 'sdhci'
                    and event['offset'] == offset)
        if count > limit:
            errors.append(
                f'{offset:#x} read count {count} exceeds {limit}')
    if spec.get('zero_writes') and write_index:
        errors.append(f'expected no writes, saw {sorted(write_index)}')
    return errors


def evaluate(binary, name, spec, reads, writes, max_poll, prereqs,
             constants):
    result, events = run_trace(binary, name)
    policy_errors = check_trace(name, events, reads, writes, max_poll, spec,
                                prereqs, constants)
    harness_errors = []
    expected = spec.get('expect_text')
    if expected and expected not in result.stdout:
        harness_errors.append(f'expected output {expected!r} is missing')
    if result.returncode != 0:
        harness_errors.append(
            f'scenario returned {result.returncode}: '
            f'{result.stdout.splitlines()[-1] if result.stdout else ""}')
    return result, events, policy_errors, harness_errors


def main():
    if not HEADER.is_file() or not DOC.is_file() or not SIM_TEST.is_file():
        print('SKIP: oracle inputs are not present')
        return 0
    compiler = shutil.which('gcc') or shutil.which('clang')
    if compiler is None:
        print('SKIP: no host C compiler')
        return 0

    registers, reads, max_poll = load_header()
    constants = load_constants()
    writes = load_write_contract()
    sim = load_sim_module()
    prereq_names = (
        'MCI_VERSION', 'PRESENT_STATE', 'POWER_CONTROL', 'CLOCK_CONTROL',
        'INT_ENABLE', 'SIGNAL_ENABLE', 'GCC_SDCC1_BCR',
        'GCC_SDCC1_APPS_CBCR', 'GCC_SDCC1_AHB_CBCR', 'GCC_SDCC1_ICE_CBCR',
        'GCC_SDCC1_APPS_CMD', 'GCC_SDCC1_APPS_CFG')
    prereqs = {
        ('gcc' if name.startswith('GCC_') else 'sdhci', registers[name])
        for name in prereq_names}

    success = {
        'require_writes': (0x2F, 0x2C),
        'forbid_writes': tuple(FORBIDDEN_WRITES),
        'expect_text': 'host-local stages complete',
        'require_irq_suppression': True,
        'require_reset_clear': True,
        'post_reset_ok': True,
        'require_clock_stable': True,
        'final_observations': (
            'PRESENT_STATE', 'POWER_CONTROL', 'INT_ENABLE', 'SIGNAL_ENABLE',
            'SOFTWARE_RESET', 'CORE_GENERICS', 'CORE_PWRCTL_STATUS',
            'CORE_PWRCTL_MASK', 'CORE_VENDOR_SPEC', 'CORE_DDR_CONFIG',
            'CORE_DLL_STATUS'),
    }
    reset_only = {
        'require_writes': (0x2F,),
        'forbid_writes': (0x2C,),
        'require_irq_suppression': True,
        'require_reset_clear': True,
    }
    scenarios = (
        ('conflict', {'zero_writes': True,
                      'expect_text': 'conflicting diagnostic arguments'}),
        ('no-argument', {'zero_writes': True,
                         'expect_text': 'no mapping and no write'}),
        ('map-failure', {'zero_writes': True,
                         'expect_text': 'SDHCI map failed'}),
        ('gcc-map-failure', {'zero_writes': True,
                             'expect_text': 'GCC map failed'}),
        ('bus-power', {'zero_writes': True,
                       'expect_text': 'bus power on before writes'}),
        ('reset-timeout', {
            'require_writes': (0x2F,),
            'forbid_writes': (0x2C,),
            'require_irq_suppression': True,
            'max_reads': {0x2F: max_poll + 1},
            'expect_text': 'reset timeout',
        }),
        ('clock-timeout', {
            'require_writes': (0x2F, 0x2C),
            'require_irq_suppression': True,
            'max_reads': {0x2C: max_poll + 2},
            'expect_text': 'internal clock timeout',
            'require_reset_clear': True,
            'post_reset_ok': True,
        }),
        ('reset-deadline', {
            'require_writes': (0x2F,),
            'forbid_writes': (0x2C,),
            'require_irq_suppression': True,
            'max_reads': {0x2F: 4},
            'expect_text': 'reset timeout',
        }),
        ('clock-deadline', {
            'require_writes': (0x2F, 0x2C),
            'require_irq_suppression': True,
            'max_reads': {0x2C: 4},
            'expect_text': 'internal clock timeout',
            'require_reset_clear': True,
            'post_reset_ok': True,
        }),
        ('stuck-counter', {
            'require_writes': (0x2F,),
            'forbid_writes': (0x2C,),
            'require_irq_suppression': True,
            'max_reads': {0x2F: max_poll + 1},
            'expect_text': 'reset timeout',
        }),
        ('clock-stuck-counter', {
            'require_writes': (0x2F, 0x2C),
            'require_irq_suppression': True,
            'max_reads': {0x2C: max_poll + 2},
            'expect_text': 'internal clock timeout',
            'require_reset_clear': True,
            'post_reset_ok': True,
        }),
        ('reset-delayed-success', dict(success, max_reads={0x2F: 300})),
        ('clock-delayed-success', dict(success, max_reads={0x2C: 300})),
        ('gcc-reset-asserted', {
            'zero_writes': True,
            'expect_text': 'SDCC1 block reset asserted',
        }),
        ('gcc-apps-branch', {
            'zero_writes': True,
            'expect_text': 'apps branch not running',
        }),
        ('gcc-ahb-branch', {
            'zero_writes': True,
            'expect_text': 'AHB branch not running',
        }),
        ('gcc-ice-branch', {
            'zero_writes': True,
            'expect_text': 'ICE branch not running',
        }),
        ('rcg-busy', {
            'zero_writes': True,
            'expect_text': 'apps RCG busy or off',
        }),
        ('rcg-config', {
            'zero_writes': True,
            'expect_text': 'unexpected apps RCG configuration',
        }),
        ('post-reset-version', dict(
            reset_only,
            expect_text='MCI version changed after reset',
            post_reset_expect=(('MCI_VERSION', 'mci-major-bad'),))),
        ('post-reset-power', dict(
            reset_only,
            expect_text='bus power on after reset',
            post_reset_expect=(('POWER_CONTROL', 'bus-power-on'),))),
        ('post-reset-clock', dict(
            reset_only,
            expect_text='card clock on after reset',
            post_reset_expect=(('CLOCK_CONTROL', 'card-clock-on'),))),
        ('post-reset-irq', dict(
            reset_only,
            expect_text='interrupts re-enabled after reset',
            post_reset_expect=(('INT_ENABLE', 'irq-nonzero'),))),
        ('irq-suppress', dict(
            success, require_writes=(0x34, 0x38, 0x2F, 0x2C))),
        ('irq-suppress-failure', {
            'require_writes': (0x34, 0x38),
            'forbid_writes': (0x2F, 0x2C),
            'expect_text': 'interrupt suppression did not clear',
        }),
        ('vendor-power', dict(
            success, forbid_writes=(0x240, 0x244, 0x24C),
            expect_text='power stage blocked')),
        ('vendor-change', dict(
            success, forbid_writes=(0x240, 0x244, 0x24C),
            expect_text='power stage blocked')),
        ('vendor-arbitrary', dict(
            success, forbid_writes=(0x240, 0x244, 0x24C),
            expect_text='power stage blocked')),
        ('unexpected-version', {'zero_writes': True,
                                'expect_text': 'unexpected MCI version'}),
        ('read-unavailable', {'zero_writes': True,
                              'expect_text': 'unexpected MCI version'}),
        ('card-clock', {'zero_writes': True,
                        'expect_text': 'card clock on before writes'}),
        ('inhibit', {'zero_writes': True,
                     'expect_text': 'command/data inhibit set'}),
        ('status-change', dict(
            success, forbid_writes=(0x240, 0x244, 0x24C),
            expect_text='power stage blocked')),
    )

    directory = Path(tempfile.mkdtemp(prefix='gta4l-sdhci-oracle-'))
    failures = 0
    synthetic_count = 0
    baseline_events = None
    try:
        binary = sim.build(compiler, directory, name='oracle')
        scenario_specs = dict(scenarios)
        for name, spec in scenarios:
            try:
                result, events, policy_errors, harness_errors = evaluate(
                    binary, name, spec, reads, writes, max_poll, prereqs,
                    constants)
            except AssertionError as exc:
                failures += 1
                print(f'FAIL {name}: {exc}')
                continue
            if name == 'irq-suppress':
                baseline_events = events
            errors = policy_errors + harness_errors
            if errors:
                failures += 1
                print(f'FAIL {name}')
                for error in errors:
                    print(f'  {error}')
            else:
                print(f'PASS {name}: {len(events)} accesses match the policy')

        for name, mutation, scenario, _needle in sim.MUTATIONS:
            spec = scenario_specs[scenario]
            mutated = sim.build(compiler, directory,
                                name=f'oracle-mutation-{name}',
                                mutations=mutation)
            try:
                result, events, policy_errors, harness_errors = evaluate(
                    mutated, scenario, spec, reads, writes, max_poll, prereqs,
                    constants)
            except AssertionError as exc:
                failures += 1
                print(f'FAIL mutation {name}: oracle could not evaluate: '
                      f'{exc}')
                continue
            if not policy_errors:
                failures += 1
                detail = ('; '.join(harness_errors) if harness_errors
                          else 'no policy finding')
                print(f'FAIL mutation {name}: oracle found no independent '
                      f'violation ({detail})')
            else:
                print(f'PASS mutation {name}: oracle rejected the violation '
                      f'({policy_errors[0]})')

        def synthetic(name, events, needle, spec=None):
            nonlocal failures, synthetic_count
            synthetic_count += 1
            for index, event in enumerate(events):
                event['seq'] = index
                event['time'] = index
            errors = check_trace(
                name, events, reads, writes, max_poll,
                spec or scenario_specs['irq-suppress'], prereqs, constants)
            if not any(needle in error for error in errors):
                failures += 1
                print(f'FAIL synthetic {name}: oracle did not reject with '
                      f'{needle!r} ({errors[:1]})')
            else:
                print(f'PASS synthetic {name}: oracle rejected the violation '
                      f'({errors[0]})')

        if baseline_events is None:
            failures += 1
            print('FAIL synthetic prerequisite-reads-removed: no baseline '
                  'trace')
        else:
            base = [dict(event) for event in baseline_events]
            synthetic('prerequisite-reads-removed',
                      [event for event in base if event['dir'] == 'w'],
                      'prerequisite read')

            violated = [dict(event) for event in base]
            for event in violated:
                if event['dir'] == 'r' and event['offset'] == 0x29:
                    event['value'] = constants['POWER_BUS_POWER']
                    break
            synthetic('preconditions-violated', violated,
                      'POWER_CONTROL precondition')

            reset_write = next(
                index for index, event in enumerate(base)
                if event['dir'] == 'w' and event['offset'] == 0x2F)
            clear_read = next(
                index for index, event in enumerate(base)
                if index > reset_write and event['dir'] == 'r'
                and event['offset'] == 0x2F and event['value'] == 0)
            reordered = (base[:reset_write] + [dict(base[clear_read])] +
                         base[reset_write:clear_read] +
                         base[clear_read + 1:])
            synthetic('reset-clear-before-write', reordered,
                      'clock write before observing reset self-clear')

            clock_write = next(
                index for index, event in enumerate(base)
                if event['dir'] == 'w' and event['offset'] == 0x2C)
            no_stable = [dict(event) for event in base]
            for index, event in enumerate(no_stable):
                if (index > clock_write and event['dir'] == 'r'
                        and event['offset'] == 0x2C):
                    event['value'] = constants['CLOCK_INT_EN']
            synthetic('clock-stable-missing', no_stable,
                      'internal clock stable not observed after the clock '
                      'write')

            post_keys = {
                ('sdhci', registers[name]) for name in (
                    'MCI_VERSION', 'POWER_CONTROL', 'CLOCK_CONTROL',
                    'INT_ENABLE', 'SIGNAL_ENABLE')}
            post_indices = [
                index for index, event in enumerate(base)
                if reset_write < index < clock_write
                and event['dir'] == 'r'
                and (event['space'], event['offset']) in post_keys]
            post_events = [dict(base[index]) for index in post_indices]
            moved = [
                dict(event) for index, event in enumerate(base)
                if index not in set(post_indices)]
            moved_clock = next(
                index for index, event in enumerate(moved)
                if event['dir'] == 'w' and event['offset'] == 0x2C)
            moved = (moved[:moved_clock + 1] + post_events +
                     moved[moved_clock + 1:])
            synthetic('post-reset-after-clock', moved,
                      'post-reset read')

            irq_reads = [
                index for index, event in enumerate(base)
                if index < reset_write and event['dir'] == 'r'
                and event['offset'] in (0x34, 0x38)]
            missing = [
                dict(event) for index, event in enumerate(base)
                if index not in set(irq_reads[-2:])]
            synthetic('interrupt-readback-missing', missing,
                      'was not confirmed clear before reset')

            nonzero = [dict(event) for event in base]
            for index in irq_reads[-2:]:
                nonzero[index]['value'] = 1
            synthetic('interrupt-readback-nonzero', nonzero,
                      'had a nonzero readback before reset')

            def transient_readback(events, offset, name, reset_index,
                                   spec=None):
                transient = [dict(event) for event in events]
                confirm = max(
                    index for index, event in enumerate(transient)
                    if index < reset_index and event['dir'] == 'r'
                    and event['offset'] == offset)
                transient.insert(confirm, dict(transient[confirm], value=1))
                synthetic(name, transient,
                          f'interrupt enable {offset:#x} had a nonzero '
                          'readback before reset', spec)

            transient_readback(base, registers['INT_ENABLE'],
                               'interrupt-readback-transient', reset_write)
            transient_readback(base, registers['SIGNAL_ENABLE'],
                               'interrupt-readback-transient-signal',
                               reset_write)

            unstable = [dict(event) for event in base]
            final_clock = max(
                index for index, event in enumerate(unstable)
                if index > clock_write and event['dir'] == 'r'
                and event['offset'] == 0x2C)
            unstable[final_clock]['value'] = constants['CLOCK_INT_EN']
            synthetic('clock-final-unstable', unstable,
                      'final clock observation is not stable')

            clock_read_indices = [
                index for index, event in enumerate(base)
                if index > clock_write and event['dir'] == 'r'
                and event['offset'] == 0x2C]
            final_missing = [
                dict(event) for index, event in enumerate(base)
                if index != clock_read_indices[-1]]
            synthetic('clock-final-missing', final_missing,
                      'final clock confirmation is missing')

            stable_clock = dict(base[clock_read_indices[-1]])
            unstable_clock = dict(stable_clock,
                                  value=constants['CLOCK_INT_EN'])
            overrun = (
                base[:clock_write + 1] +
                [dict(unstable_clock) for _ in range(max_poll)] +
                [dict(stable_clock), dict(stable_clock)] +
                base[clock_read_indices[-1] + 1:])
            synthetic('clock-poll-overrun', overrun, 'clock poll count')

            final_after = (
                [dict(event) for index, event in enumerate(base)
                 if index != clock_read_indices[-1]] +
                [dict(base[clock_read_indices[-1]])])
            synthetic('clock-final-after-downstream', final_after,
                      'final clock confirmation is not the next access')

            both_after = (
                [dict(event) for index, event in enumerate(base)
                 if index not in set(clock_read_indices)] +
                [dict(base[index]) for index in clock_read_indices])
            synthetic('clock-poll-after-downstream', both_after,
                      'downstream access before the clock poll completed')

            final_clock = clock_read_indices[-1]
            final_names = (
                'PRESENT_STATE', 'POWER_CONTROL', 'INT_ENABLE',
                'SIGNAL_ENABLE', 'SOFTWARE_RESET', 'CORE_GENERICS',
                'CORE_PWRCTL_STATUS', 'CORE_PWRCTL_MASK',
                'CORE_VENDOR_SPEC', 'CORE_DDR_CONFIG', 'CORE_DLL_STATUS')
            final_keys = {
                ('sdhci', registers[name]) for name in final_names}
            final_missing_all = [
                dict(event) for index, event in enumerate(base)
                if not (index > final_clock
                        and (event['space'], event['offset']) in final_keys)]
            synthetic('final-observations-missing', final_missing_all,
                      'final PRESENT_STATE observation is missing')

            for name in final_names:
                offset = registers[name]
                final_indices = [
                    index for index, event in enumerate(base)
                    if index > final_clock and event['space'] == 'sdhci'
                    and event['offset'] == offset]
                if not final_indices:
                    failures += 1
                    print(f'FAIL synthetic final-{name.lower()}: no final '
                          f'observation in the baseline')
                    continue
                missing = [
                    dict(event) for index, event in enumerate(base)
                    if index not in set(final_indices)]
                synthetic(f'final-{name.lower()}-missing', missing,
                          f'final {name} observation is missing')

                early = [
                    dict(event) for index, event in enumerate(base)
                    if index not in set(final_indices)]
                early.insert(clock_write, dict(base[final_indices[-1]]))
                synthetic(f'final-{name.lower()}-early', early,
                          f'final {name} observation is missing')

            zero_spec = dict(scenario_specs['irq-suppress'],
                             require_writes=(0x2F, 0x2C))
            zero_base = [
                dict(event) for event in base
                if not (event['dir'] == 'w'
                        and event['offset'] in (0x34, 0x38))]
            zero_initial = set()
            for offset in (0x34, 0x38):
                first = next(
                    index for index, event in enumerate(zero_base)
                    if event['dir'] == 'r' and event['offset'] == offset)
                zero_initial.add(first)
                zero_base[first]['value'] = 0
            zero_reset = next(
                index for index, event in enumerate(zero_base)
                if event['dir'] == 'w' and event['offset'] == 0x2F)
            zero_confirm = [
                index for index, event in enumerate(zero_base)
                if index < zero_reset and index not in zero_initial
                and event['dir'] == 'r'
                and event['offset'] in (0x34, 0x38)]
            for index, event in enumerate(zero_base):
                event['seq'] = index
                event['time'] = index
            zero_errors = check_trace(
                'initially-zero-baseline', zero_base, reads, writes,
                max_poll, zero_spec, prereqs, constants)
            if zero_errors:
                failures += 1
                print(f'FAIL synthetic initially-zero-baseline: oracle '
                      f'rejected a valid trace ({zero_errors[:1]})')
            else:
                print('PASS synthetic initially-zero-baseline: oracle '
                      'accepted the valid trace')

            zero_missing = [
                dict(event) for index, event in enumerate(zero_base)
                if index not in set(zero_confirm)]
            synthetic('initially-zero-readback-missing', zero_missing,
                      'was not confirmed clear before reset', zero_spec)

            zero_nonzero = [dict(event) for event in zero_base]
            for index in zero_confirm:
                zero_nonzero[index]['value'] = 1
            synthetic('initially-zero-readback-nonzero', zero_nonzero,
                      'had a nonzero readback before reset', zero_spec)

            transient_readback(zero_base, registers['INT_ENABLE'],
                               'initially-zero-readback-transient',
                               zero_reset, zero_spec)
            transient_readback(zero_base, registers['SIGNAL_ENABLE'],
                               'initially-zero-readback-transient-signal',
                               zero_reset, zero_spec)

            if synthetic_count != SYNTHETIC_CONTROLS:
                failures += 1
                print(f'FAIL synthetic control count: expected '
                      f'{SYNTHETIC_CONTROLS}, got {synthetic_count}')
    finally:
        shutil.rmtree(directory, ignore_errors=True)

    print(f'ORACLE policy: {len(reads)} read rules, '
          f'{len(writes)} write rules, {len(FORBIDDEN_WRITES)} forbidden '
          f'write offsets, max poll {max_poll}')
    print(f'ORACLE synthetic controls: {synthetic_count}')
    if failures:
        print(f'FAIL: {failures} scenario(s) or mutation(s) disagree with '
              'the oracle')
        return 1
    print('PASS: every captured trace matches the separate-implementation access policy '
          'and every mutation was rejected')
    return 0


if __name__ == '__main__':
    sys.exit(main())
