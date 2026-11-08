#!/usr/bin/env python3
"""Exercise the RAM-root fixture recipe's input and failure checks.

The reproduction recipe in docs/ram-root-fixture-reproducibility.md must stop
before building when a pinned source, dependency commit, compiler version or
replay directory differs. This test extracts the documented blocks and runs
their success and failure paths with temporary fixtures, so the recipe cannot
regress to a display-only checklist.
"""
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / 'docs/ram-root-fixture-reproducibility.md'
BASELINE = ROOT / 'docs/evidence-baseline.md'
SHARED = ROOT.parent / 'UniXNU'
CLANG = Path('/usr/bin/clang-21')
TMP = Path('/tmp/opencode')


def run(script, cwd=ROOT, env=None):
    return subprocess.run(['bash', '-c', script], cwd=cwd, env=env,
                          text=True, capture_output=True)


def expect(name, script, want_zero, contains=None, cwd=ROOT, env=None):
    result = run(script, cwd=cwd, env=env)
    output = result.stdout + result.stderr
    ok = (result.returncode == 0) == want_zero
    if contains is not None and contains not in output:
        ok = False
    if not ok:
        raise AssertionError(f'{name}: exit={result.returncode}\n{output[-2000:]}')
    print(f'ok: {name}')


def blocks(text):
    return re.findall(r'```sh\n(.*?)```', text, re.S)


def source_verifier(text):
    match = re.search(r"python3\.11 - \"\$DOC\" \"\$SRC\" <<'PY'\n(.*?)\nPY\n",
                      text, re.S)
    if match is None:
        raise AssertionError('source verifier is missing')
    return match.group(1)


def baseline_verifier(text):
    match = re.search(r"python3\.11 - <<'PY'\n(.*?)\nPY\n", text, re.S)
    if match is None:
        raise AssertionError('baseline verifier is missing')
    return match.group(1)


def make_tc(root, clang):
    tc = root / 'tc'
    for relative in ('CommandLineTools/usr/bin', 'CommandLineTools/usr/libexec',
                     'XcodeDefault.xctoolchain/usr/bin', 'bin'):
        (tc / relative).mkdir(parents=True)
    shutil.copy2(SHARED / 'scripts/linux/xcrun', tc / 'bin/xcrun')
    (tc / 'bin/xcrun').chmod(0o755)
    (tc / 'CommandLineTools/usr/bin/clang').symlink_to(clang)
    return tc


def fake_compiler(path, output=None):
    path.write_text('#!/bin/sh\n' + (f'echo "{output}"\n' if output else 'exit 1\n'))
    path.chmod(0o755)
    return path


def check_source_pins(text, work):
    pins = re.findall(r'^\| `(scripts/[^`]+)` \| `([0-9a-f]{64})` \|$', text, re.M)
    assert len(pins) == 6, f'expected 6 source pins, found {len(pins)}'
    assert len({path for path, _ in pins}) == 6, 'duplicate source pins'
    assert SHARED.is_dir(), f'shared checkout is missing: {SHARED}'
    verifier = source_verifier(text)
    script = f'''python3.11 - "$DOC" "$SRC" <<'PY'
{verifier}
PY
'''
    expect('source pins accept the shared checkout', script, True,
           'source inputs: 6 match',
           env={**os.environ, 'DOC': str(DOC), 'SRC': str(SHARED)})
    bad = work / 'bad-hash.md'
    bad.write_text(text.replace(pins[0][1], '0' * 64, 1))
    expect('wrong source hash is rejected', script, False,
           'do not match the pins',
           env={**os.environ, 'DOC': str(bad), 'SRC': str(SHARED)})
    missing = work / 'missing-row.md'
    missing.write_text(text.replace(
        f'| `{pins[0][0]}` | `{pins[0][1]}` |\n', '', 1))
    expect('missing source pin is rejected', script, False,
           'expected 6 pinned inputs',
           env={**os.environ, 'DOC': str(missing), 'SRC': str(SHARED)})
    duplicate = work / 'duplicate-row.md'
    duplicate.write_text(text.replace(
        f'| `{pins[-1][0]}` |', f'| `{pins[0][0]}` |', 1))
    expect('duplicate source pin is rejected', script, False,
           'duplicate pinned input paths',
           env={**os.environ, 'DOC': str(duplicate), 'SRC': str(SHARED)})


def check_compiler(text, work):
    block = next(b for b in blocks(text) if 'xcrun --find clang' in b)

    def script(tc):
        return 'set -eu\n' + block.replace('$TC', str(tc))

    assert CLANG.is_file(), f'pinned compiler is missing: {CLANG}'
    expect('clang 21.1.8 is accepted',
           script(make_tc(work / 'real', CLANG)), True)
    for name, banner in (('patch', 'clang version 21.1.80 (wrong)'),
                         ('suffix', 'clang version 21.1.8-extra'),
                         ('apple', 'Apple clang version 21.1.8')):
        root = work / name
        tc = make_tc(root, CLANG)
        (tc / 'CommandLineTools/usr/bin/clang').unlink()
        fake_compiler(tc / 'CommandLineTools/usr/bin/clang', banner)
        expect(f'compiler banner {banner!r} is rejected', script(tc), False)
    root = work / 'failed'
    tc = make_tc(root, CLANG)
    (tc / 'CommandLineTools/usr/bin/clang').unlink()
    fake_compiler(tc / 'CommandLineTools/usr/bin/clang')
    expect('failed compiler is rejected', script(tc), False)
    root = work / 'fallback'
    tc = make_tc(root, CLANG)
    (tc / 'CommandLineTools/usr/bin/clang').unlink()
    fallback = root / 'bin'
    fallback.mkdir()
    fake_compiler(fallback / 'clang', 'clang version 21.1.8 (fake)')
    env = dict(os.environ)
    env['PATH'] = f'{fallback}:' + env['PATH']
    expect('PATH fallback is rejected', script(tc), False, env=env)


def check_dependencies(text, work):
    block = next(b for b in blocks(text) if 'cctools-work/cctools-port' in b)
    marker = 'test "$(git -C "$REPLAY/cctools-work/cctools-port"'
    checks = block[block.index(marker):]
    assert '904de2a71d4da6a9b30d2efaf912a10ddc7d9ddb' in checks
    assert '024805abec6daa9cdda569fd736b9c7686dfd735' in checks
    repo = work / 'fake-repo'
    subprocess.run(['git', 'init', '-q', str(repo)], check=True)
    subprocess.run(['git', '-C', str(repo), 'config', 'user.email',
                    'test@example.invalid'], check=True)
    subprocess.run(['git', '-C', str(repo), 'config', 'user.name', 'Test'],
                   check=True)
    subprocess.run(['git', '-C', str(repo), 'commit', '-q', '--allow-empty',
                    '-m', 'x'], check=True)
    checks = checks.replace('$REPLAY/cctools-work/cctools-port', str(repo))
    checks = checks.replace('$REPLAY/cctools-work/swift-corelibs-libdispatch',
                            str(repo))
    expect('different dependency commit is rejected', 'set -eu\n' + checks,
           False)


def check_isolation(text, work):
    assert 'test -z "$(ls -A "$REPLAY")"' in text
    nonempty = work / 'nonempty'
    nonempty.mkdir()
    (nonempty / 'x').write_text('x')
    expect('non-empty replay directory is rejected',
           f'test -z "$(ls -A "{nonempty}")"', False)
    fresh = work / 'fresh'
    fresh.mkdir()
    expect('fresh replay directory is accepted',
           f'test -z "$(ls -A "{fresh}")"', True)


def check_compare(text, work):
    block = next(b for b in blocks(text) if 'cmp "$NEW/user.o"' in b)
    block = block.replace('ROOT=/path/to/taba7unixnu', f'ROOT={work}')
    replay = work / 'replay'
    new = replay / 'fixture-out'
    new.mkdir(parents=True)
    names = ['user.o', 'launchd-static.macho', 'launchd.macho',
             'trustcache.bundle', 'trustcache.json', 'root.img',
             'addresses.json', 'fixture.json']
    historical = {
        'user.o': work / 'out/gta4l/ram-root-sdhci-v5-evidence/user.o',
        'launchd-static.macho':
            work / 'out/gta4l/ram-root-sdhci-v5-evidence/launchd-static.macho',
    }
    for name in names[2:]:
        historical[name] = work / f'out/gta4l/ram-root-fs-timer-evidence/{name}'
    for name in names:
        payload = f'{name}\n'.encode()
        (new / name).write_bytes(payload)
        historical[name].parent.mkdir(parents=True, exist_ok=True)
        historical[name].write_bytes(payload)
    script = 'set -eu\n' + block
    env = {**os.environ, 'REPLAY': str(replay)}
    expect('matching replay outputs are accepted', script, True, env=env)
    (new / 'user.o').write_bytes(b'corrupt\n')
    expect('corrupted replay output is rejected', script, False, env=env)


def check_baseline(text, work):
    verifier = baseline_verifier(text)
    script = f"python3.11 - <<'PY'\n{verifier}\nPY\n"
    expect('baseline table is accepted', script, True, 'mismatches=0', cwd=ROOT)
    rows = re.findall(r'^\| [^|]+ \| `([^`]+)` \| `([0-9a-f]{64})` \|$',
                      text, re.M)
    expected = int(re.search(r'^expected = (\d+)$', verifier, re.M).group(1))
    assert len(rows) == expected, (
        f'baseline has {len(rows)} rows, expected {expected}')
    row_lines = [f'| row | `{path}` | `{digest}` |' for path, digest in rows]
    cases = (
        ('empty baseline inventory', [],
         f'expected {expected} rows, found 0'),
        ('missing baseline row', row_lines[:-1],
         f'expected {expected} rows, found {expected - 1}'),
        ('duplicate baseline row', row_lines[:-1] + [row_lines[0]],
         'duplicate paths in the table'),
    )
    for name, lines, message in cases:
        case = work / name.replace(' ', '-')
        (case / 'docs').mkdir(parents=True)
        (case / 'docs/evidence-baseline.md').write_text(
            '| Area | Path | SHA-256 |\n| --- | --- | --- |\n'
            + '\n'.join(lines) + '\n')
        expect(name + ' is rejected', script, False, message, cwd=case)
    case = work / 'wrong-baseline-hash'
    (case / 'docs').mkdir(parents=True)
    (case / 'docs/evidence-baseline.md').write_text(
        text.replace(rows[0][1], '0' * 64, 1))
    for path, _ in rows:
        target = case / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.symlink_to(ROOT / path)
    expect('wrong baseline hash is rejected', script, False, 'mismatches=1',
           cwd=case)


def check_hold_fixture(text, work):
    builder = (SHARED / 'scripts/build-ram-root-report-fixture.py').read_text()
    inc = (SHARED / 'scripts/qemu-entry/native-ram-root-fs-timer.inc').read_text()
    assert 'fs-timer-hold' in builder, 'hold diagnostic is missing from the builder'
    assert 'NATIVE_RAM_ROOT_FS_TIMER_HOLD' in builder, \
        'hold define is missing from the builder'
    hold_at = inc.index('#ifdef NATIVE_RAM_ROOT_FS_TIMER_HOLD')
    else_at = inc.index('#else', hold_at)
    endif_at = inc.index('#endif', else_at)
    hold_block = inc[hold_at:else_at]
    plain_block = inc[else_at:endif_at]
    assert 'Ldiag_hold' in hold_block
    assert 'mov x2, #1000' in hold_block
    assert 'mov x16, #230' in hold_block
    assert 'b Ldiag_hold' in hold_block
    assert 'adrp x1, Ldiag_pass_message@PAGE' in plain_block
    assert 'b Ldiag_panic' in plain_block

    tc = make_tc(work / 'hold-tc', CLANG)
    env = {**os.environ,
           'PATH': f'{tc}/CommandLineTools/usr/bin:{tc}/bin:' + os.environ['PATH'],
           'XNU_TOOLCHAIN_BIN': (
               f'{tc}/CommandLineTools/usr/bin '
               f'{tc}/XcodeDefault.xctoolchain/usr/bin')}
    source = SHARED / 'scripts/qemu-entry/native-user.S'

    def compile_object(defines, output):
        command = [str(tc / 'bin/xcrun'), 'clang', '-target',
                   'arm64-apple-ios17.0', '-isysroot', 'iphoneos',
                   *defines, '-c', str(source), '-o', str(output)]
        subprocess.run(command, check=True, env=env, capture_output=True)
        return hashlib.sha256(output.read_bytes()).hexdigest()

    original = work / 'hold-original.o'
    held = work / 'hold-held.o'
    original_sha = compile_object(['-DNATIVE_RAM_ROOT_FS_TIMER=1'], original)
    held_sha = compile_object(
        ['-DNATIVE_RAM_ROOT_FS_TIMER=1', '-DNATIVE_RAM_ROOT_FS_TIMER_HOLD=1'],
        held)
    assert original_sha == \
        '6984333d83feb8e02ee71c98f3ccf90cd75766814f270a597f859ad8c6152490', \
        'the original fs-timer object changed'
    assert original_sha != held_sha, 'the hold define did not change the object'

    objdump = shutil.which('llvm-objdump') or shutil.which('objdump')
    if objdump is None:
        print('SKIP: no objdump for the hold-fixture instruction check')
        return
    disassembly = subprocess.run(
        [objdump, '-d', '--no-show-raw-insn', str(held)],
        check=True, text=True, capture_output=True).stdout
    instructions = []
    for address, body in re.findall(r'^\s*([0-9a-f]+):\s+(.*)$',
                                    disassembly, re.M):
        instructions.append((int(address, 16),
                             re.sub(r'\s+', ' ', body.split(';')[0]).strip()))
    sequence = ['mov x8, #0x7', 'str x8, [x20]', 'mov x0, #0x0',
                'mov x1, #0x0', 'mov x2, #0x3e8', 'mov x16, #0xe6',
                'svc #0x80']
    start = None
    for index in range(len(instructions) - len(sequence)):
        if [body for _, body in
                instructions[index:index + len(sequence)]] == sequence:
            start = index
            break
    assert start is not None, 'the hold sequence is missing'
    hold_address = instructions[start + 2][0]
    branch = instructions[start + len(sequence)]
    match = re.match(r'b 0x([0-9a-f]+)', branch[1])
    assert match is not None, f'expected the hold branch, got {branch[1]}'
    assert int(match.group(1), 16) == hold_address, \
        'the hold loop does not branch back to its poll'
    assert any(body == 'mov x0, #0x804' for _, body in instructions)
    assert any(body == 'mov x16, #0x37' for _, body in instructions)
    print('ok: original fixture is byte-identical and the hold fixture '
          'polls without panicking; failure paths still panic')


def main():
    text = DOC.read_text()
    baseline = BASELINE.read_text()
    work = Path(tempfile.mkdtemp(
        prefix='gta4l-recipe-', dir=TMP if TMP.is_dir() else None))
    try:
        check_source_pins(text, work)
        check_compiler(text, work)
        check_dependencies(text, work)
        check_isolation(text, work)
        check_compare(text, work)
        check_hold_fixture(text, work)
        check_baseline(baseline, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print('PASS: RAM-root fixture recipe input and failure checks')


if __name__ == '__main__':
    main()
