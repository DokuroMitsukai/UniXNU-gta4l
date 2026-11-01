#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Print the gta4l packaging inputs as deterministic JSON.

Host-only and offline: it reads the device repository, the shared UniXNU
checkout and the firmware checkout.  It does not fetch, build, write a build
output or touch a device.

Run it with ``python3.11 -B`` from the device repository, or pass ``--device``.
The result separates four kinds of fact:

* ``declared``: raw board/profile/feed/catalog/lock values and file hashes;
* ``effective``: the production profile merge, feed resolution and patch plan
  under an explicit offline policy (no saved state, build-input file or
  environment value is applied);
* ``cached``: local build reports and artifacts, with the comparisons that
  were possible against the current tree;
* ``unresolved``: missing or invalid dependencies, caches and Git metadata.
  They are not clean or resolved states.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROFILE_FIELDS = ('name', 'darwin', 'xnu_version', 'xnu_revision', 'driverkit_tag', 'trustcache_tag', 'abi')
ENVIRONMENT = ('UNIXNU_DEVICE', 'UNIXNU_OUT', 'UNIXNU_UEFI', 'UNIXNU_EDK2', 'UNIXNU_KERNELCACHE',
               'UNIXNU_DTB', 'UNIXNU_IPSW', 'UNIXNU_UNIQUE_CHIP_ID', 'JOBS', 'GITHUB_TOKEN')
SECRET_KEYS = re.compile(
    r'(?i)(token|secret|password|passwd|credential|api[_-]?key|authorization|unique_chip_id|seed)')
TOKENS = re.compile(r'(?i)\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|glpat-[A-Za-z0-9_-]{20,})\b')
QUERY = re.compile(
    r'(?i)([?&](?:token|access_token|id_token|api_key|apikey|key|sig|signature|credential|'
    r'x-amz-signature|x-amz-credential|x-amz-security-token|googleaccessid|policy|expires|'
    r'user|password|secret)=)([^&\s"\']+)')
CREDENTIALS = re.compile(r'(?i)([a-z][a-z0-9+.-]*://)([^/@\s]+)@')


def load(path):
    return tomllib.loads(Path(path).read_text())


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def redact(text):
    """Mask token-like values, URL credentials and signed query parameters."""
    text = TOKENS.sub('<redacted>', str(text))
    text = QUERY.sub(r'\1<redacted>', text)
    return CREDENTIALS.sub(r'\1<redacted>@', text)


def scrub(value):
    """Recursively redact secret-like keys and values before they reach stdout."""
    if isinstance(value, dict):
        result = {}
        for key, item in value.items():
            if SECRET_KEYS.search(str(key)):
                keep = key == 'unique_chip_id' and item in (None, '', 'required', 'optional', 'none')
                result[key] = item if keep else '<redacted>'
            else:
                result[key] = scrub(item)
        return result
    if isinstance(value, list):
        return [scrub(item) for item in value]
    if isinstance(value, str):
        return redact(value)
    return value


def git(path, *arguments):
    try:
        return subprocess.run(['git', '-C', str(path), *arguments],
                              text=True, capture_output=True, check=False)
    except OSError:
        return None


def git_text(path, *arguments):
    result = git(path, *arguments)
    if result is None:
        return None, 'git-unavailable'
    if result.returncode:
        message = (result.stderr or '').lower()
        if 'not a git repository' in message:
            return None, 'not-a-git-work-tree'
        return None, 'git-error'
    return result.stdout.strip(), None


def checkout(path):
    """A repository entry that never reports a commit from an enclosing checkout."""
    path = Path(path)
    entry = {'path': str(path.resolve()) if path.exists() else None}
    if not path.exists():
        return {**entry, 'state': 'missing'}
    if not path.is_dir():
        return {**entry, 'state': 'not-a-directory'}
    top, problem = git_text(path, 'rev-parse', '--show-toplevel')
    if problem:
        return {**entry, 'state': problem}
    top = Path(top).resolve()
    if top != path.resolve():
        return {**entry, 'state': 'enclosing-repository', 'toplevel': str(top)}
    commit, problem = git_text(path, 'rev-parse', 'HEAD')
    branch, _ = git_text(path, 'rev-parse', '--abbrev-ref', 'HEAD')
    status, _ = git_text(path, 'status', '--porcelain')
    remote, _ = git_text(path, 'remote', 'get-url', 'origin')
    if problem:
        return {**entry, 'state': 'git-error', 'toplevel': str(top)}
    return {**entry, 'state': 'dirty' if status else 'clean', 'toplevel': str(top),
            'commit': commit, 'branch': branch, 'remote': redact(remote) if remote else None,
            'dirty_paths': len(status.splitlines()) if status else 0}


def first_fdt(path):
    data = Path(path).read_bytes()
    if len(data) < 40 or data[:4] != b'\xd0\x0d\xfe\xed':
        return None
    size = int.from_bytes(data[4:8], 'big')
    if size < 40 or size > len(data):
        return None
    return {'bytes': size, 'sha256': hashlib.sha256(data[:size]).hexdigest()}


def feed(feeds, name):
    for entry in feeds.get('source', []):
        if entry.get('name') == name:
            return {key: entry.get(key) for key in ('name', 'kind', 'ref', 'commit', 'local', 'git')}
    return None


def unresolved_for(label, entry):
    state = entry.get('state')
    if state in (None, 'clean', 'dirty', 'in-parent-checkout'):
        return []
    return [{'kind': 'git', 'name': label, 'reason': state, 'path': entry.get('path')}]


def module_checkout(path, parents):
    """A module inside a recorded checkout is not an invalid repository."""
    entry = checkout(path)
    if entry.get('state') == 'enclosing-repository':
        top = Path(entry['toplevel'])
        if any(top == Path(parent).resolve() for parent in parents):
            entry['state'] = 'in-parent-checkout'
    return entry


def effective(device, unixnu, out_root):
    """Run the shared side-effect-free resolver when the checkout provides it."""
    scripts = unixnu / 'scripts'
    if not (scripts / 'unixnu/inspection.py').is_file():
        return {'resolved': False, 'errors': ['UniXNU inspection module is not available'],
                'selection': None, 'profile_files': [], 'modules': [], 'patches': [], 'board': None}
    text = str(scripts)
    if text not in sys.path:
        sys.path.insert(0, text)
    try:
        from unixnu import inspection
    except (ImportError, SyntaxError) as error:
        return {'resolved': False, 'errors': [f'UniXNU inspection module is not importable: {error}'],
                'selection': None, 'profile_files': [], 'modules': [], 'patches': [], 'board': None}
    return inspection.inspect(device, root=unixnu, out_root=out_root)


def compare_xnu(cached, effective_result):
    """Compare the cached XNU report with the current patch plan and commit."""
    recorded = cached.get('patches', [])
    current = effective_result.get('patches') or []
    current_names = [item['name'] for item in current]
    recorded_names = [Path(item.get('path', '')).name for item in recorded]
    mismatches, missing = [], []
    for item in recorded:
        path = Path(item.get('path', ''))
        if not path.is_file():
            missing.append(path.name or '<unnamed>')
            continue
        digest = sha256(path)
        if digest != item.get('sha256'):
            mismatches.append({'name': path.name, 'recorded': item.get('sha256'), 'current': digest})
    xnu = next((module for module in effective_result.get('modules', [])
                if module['kind'] == 'refer' and module['name'] == 'xnu'), None)
    return {'source': {'recorded': redact(cached.get('source') or ''),
                       'current': xnu['path'] if xnu else None,
                       'match': bool(xnu and cached.get('source') == xnu['path']) if xnu else None},
            'commit': {'recorded': cached.get('commit'), 'current': xnu['revision'] if xnu else None,
                       'match': bool(xnu and cached.get('commit') == xnu['revision']) if xnu else None},
            'patch_order_match': recorded_names == current_names,
            'missing_recorded_files': missing,
            'hash_mismatches': mismatches,
            'current_patches': current}


def inventory(device, unixnu=None, uefi=None, out_root=None):
    device = Path(device).resolve()
    declared_board = load(device / 'board.toml')
    package = declared_board.get('package', {})
    if unixnu is None and not package.get('unixnu'):
        raise ValueError('board.toml [package] unixnu is required (or pass --unixnu)')
    unixnu = Path(unixnu or (device / package['unixnu'])).resolve()
    out_root = Path(out_root or unixnu / 'out').resolve()
    if uefi is None:
        uefi = Path(os.environ['UNIXNU_UEFI']) if os.environ.get('UNIXNU_UEFI') else device.parent / 'Mu-Silicium'
    uefi = Path(uefi).resolve()

    unixnu_feeds = load(unixnu / 'feeds.toml') if (unixnu / 'feeds.toml').is_file() else {}
    catalog = {entry['id']: entry for entry in
               (load(unixnu / 'components.toml').get('component', [])
                if (unixnu / 'components.toml').is_file() else [])}
    lock = load(device / 'unixnu.lock') if (device / 'unixnu.lock').is_file() else {}
    board_profile = declared_board['board']['os_profile']
    profile_path = unixnu / 'profiles/os' / board_profile / 'profile.toml'
    profile = load(profile_path) if profile_path.is_file() else {}
    device_feeds = load(device / 'feeds.toml') if (device / 'feeds.toml').is_file() else {}

    resolved = effective(device, unixnu, out_root)
    unresolved = [{'kind': 'effective', 'name': 'resolution', 'reason': error}
                  for error in resolved.get('errors', [])]

    repositories = {'device': checkout(device), 'unixnu': checkout(unixnu),
                    'firmware': checkout(uefi), 'firmware_basecore': checkout(uefi / 'Mu_Basecore')}
    for name, entry in repositories.items():
        unresolved += unresolved_for(name, entry)

    modules = []
    parents = (device, unixnu, uefi)
    for module in resolved.get('modules', []):
        item = dict(module)
        item['path'] = redact(item['path']) if item.get('path') else None
        item['repository'] = redact(item['repository']) if item.get('repository') else None
        if item.get('path'):
            item['checkout'] = module_checkout(item['path'], parents)
            unresolved += unresolved_for(f"{item['kind']}:{item['name']}", item['checkout'])
        elif item.get('origin') == 'git' and item.get('pin'):
            unresolved.append({'kind': 'module', 'name': f"{item['kind']}:{item['name']}",
                               'reason': 'not cached', 'pin': item['pin']})
        modules.append(item)

    components = {}
    for identifier in declared_board.get('components', {}).get('include', []):
        entry = catalog.get(identifier)
        if entry is None:
            unresolved.append({'kind': 'component', 'name': identifier, 'reason': 'not in components.toml'})
            components[identifier] = {'declared': None, 'effective': None, 'checkout': None, 'match': None}
            continue
        module = next((item for item in modules if item['kind'] == entry.get('kind')
                       and item['name'] == identifier), None)
        local = module['path'] if module and module.get('path') else unixnu / entry.get('path', '')
        state = module_checkout(local, parents) if local else {'path': None, 'state': 'missing'}
        unresolved += unresolved_for(identifier, state)
        declared = entry.get('revision')
        components[identifier] = {
            'repository': redact(entry.get('repository') or ''),
            'declared': declared,
            'effective': ({'origin': module['origin'], 'path': module['path'], 'pin': module['pin']}
                          if module else None),
            'checkout': state,
            'match': state.get('commit') == declared if state.get('commit') else None,
        }
        if components[identifier]['match'] is False:
            unresolved.append({'kind': 'component', 'name': identifier, 'reason': 'revision mismatch',
                               'declared': declared, 'current': state.get('commit')})

    build = profile.get('ipsw', {}).get('build', '')
    expected = profile.get('kernel', {}).get('prelink', {}).get('kernelcache_sha256')
    kernelcache_path = out_root / 'ipsw' / build / 'kernelcache.decompressed'
    kernelcache = None
    if kernelcache_path.is_file():
        digest = sha256(kernelcache_path)
        kernelcache = {'path': str(kernelcache_path), 'bytes': kernelcache_path.stat().st_size,
                       'sha256': digest, 'expected': expected,
                       'match': digest == expected if expected else None,
                       'compared_to_current_inputs': False}
    else:
        unresolved.append({'kind': 'cache', 'name': 'kernelcache', 'reason': 'not cached',
                           'path': str(kernelcache_path)})

    selection = resolved.get('selection')
    config_name = (selection or {}).get('config') or package.get('defconfig', {}).get(board_profile, '')
    report_path = out_root / 'gta4l' / board_profile / str(config_name).removesuffix('_defconfig') / 'xnu-inputs.json'
    xnu_inputs = None
    if report_path.is_file():
        cached_report = {'path': str(report_path), 'sha256': sha256(report_path)}
        try:
            recorded = json.loads(report_path.read_text())
            cached_report.update({'state': 'parsed', 'source': redact(recorded.get('source') or ''),
                                  'commit': recorded.get('commit'),
                                  'patches': [{'path': redact(item.get('path', '')),
                                               'sha256': item.get('sha256')}
                                              for item in recorded.get('patches', [])]})
            cached_report['comparison'] = compare_xnu(recorded, resolved)
        except (ValueError, TypeError, KeyError) as error:
            cached_report.update({'state': 'malformed', 'error': redact(error)})
            unresolved.append({'kind': 'cache', 'name': 'xnu-inputs', 'reason': 'malformed',
                               'path': str(report_path)})
        xnu_inputs = cached_report
    else:
        unresolved.append({'kind': 'cache', 'name': 'xnu-inputs', 'reason': 'not cached',
                           'path': str(report_path)})

    evidence = None
    for candidate in (device / 'out', device.parent.parent / 'out', out_root):
        path = candidate / 'gta4l/ram-root-sdhci-audit-evidence/boot-dtb-v5.bin'
        if path.is_file():
            evidence = path
            break
    source_dtb = uefi / 'Resources/DTBs' / f"{declared_board.get('firmware', {}).get('config', '')}.dtb"
    if source_dtb.is_file():
        boot_dtb = {'source': {'path': str(source_dtb), 'full_sha256': sha256(source_dtb),
                               'first': first_fdt(source_dtb)},
                    'evidence': ({'path': str(evidence), 'bytes': evidence.stat().st_size,
                                  'sha256': sha256(evidence), 'compared_to_current_inputs': False}
                                 if evidence else None)}
    else:
        boot_dtb = {'source': None, 'evidence': None}
        unresolved.append({'kind': 'input', 'name': 'boot_dtb', 'reason': 'missing',
                           'path': str(source_dtb)})

    script = device / 'scripts/package.sh'
    template = unixnu / 'templates/package.sh'
    package_output = {
        'unixnu': redact(package.get('unixnu') or ''),
        'boot_partition_bytes': package.get('boot_partition_bytes'),
        'defconfig': scrub(package.get('defconfig', {})),
        'script_sha256': sha256(script) if script.is_file() else None,
        'template_sha256': sha256(template) if template.is_file() else None,
        'template_match': script.is_file() and template.is_file() and script.read_bytes() == template.read_bytes(),
    }

    declared = {
        'board': scrub(declared_board),
        'profile': {field: scrub(profile.get('os', {}).get(field)) for field in PROFILE_FIELDS},
        'ipsw': scrub({key: profile.get('ipsw', {}).get(key) for key in
                       ('url', 'product', 'version', 'build', 'device_class', 'chip_id', 'board_id', 'fetch')}),
        'ipsw_members': scrub(profile.get('ipsw', {}).get('sha256', {})),
        'feeds': {entry['name']: scrub({key: entry.get(key) for key in ('kind', 'ref', 'commit', 'local', 'git')})
                  for entry in device_feeds.get('source', [])},
        'lock': scrub(lock.get('source', {})),
        'firmware_feed': scrub(feed(unixnu_feeds, 'edk2')),
        'source_hashes': {name: (sha256(path) if path.is_file() else None) for name, path in (
            ('board.toml', device / 'board.toml'), ('feeds.toml', device / 'feeds.toml'),
            ('unixnu.lock', device / 'unixnu.lock'), ('profile.toml', profile_path),
            ('feeds.toml (UniXNU)', unixnu / 'feeds.toml'),
            ('components.toml', unixnu / 'components.toml'))},
    }

    return {
        'tool': 'audit-gta4l-resolved-inputs',
        'policy': {
            'network': 'disabled',
            'fetch': 'disabled',
            'saved_state': 'ignored',
            'host_build_input_file': 'ignored',
            'environment_values': 'presence only, never values',
            'paths': 'absolute for this filesystem state',
            'out_root': str(out_root),
        },
        'repositories': repositories,
        'declared': declared,
        'effective': {
            'resolved': bool(resolved.get('resolved')),
            'errors': [redact(error) for error in resolved.get('errors', [])],
            'selection': scrub(selection),
            'board': scrub(resolved.get('board')),
            'profile_files': resolved.get('profile_files', []),
            'modules': modules,
            'patches': resolved.get('patches', []),
            'components': components,
        },
        'cached': {'kernelcache': kernelcache, 'xnu_inputs': xnu_inputs},
        'boot_dtb': boot_dtb,
        'package': package_output,
        'overrides': {
            'unixnu_local_toml': {'path': str(unixnu / 'unixnu.local.toml'),
                                  'present': (unixnu / 'unixnu.local.toml').is_file()},
            'device_local_toml': {'path': str(device / 'local.toml'),
                                  'present': (device / 'local.toml').is_file(),
                                  'sha256': sha256(device / 'local.toml') if (device / 'local.toml').is_file() else None},
            'environment_present': {name: bool(os.environ.get(name)) for name in ENVIRONMENT},
            'command_line_sources': 'not applied by this inventory',
        },
        'unresolved': unresolved,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', type=Path, default=ROOT)
    parser.add_argument('--unixnu', type=Path, help='UniXNU checkout (default: [package] unixnu)')
    parser.add_argument('--uefi', type=Path, help='Mu-Silicium checkout (default: UNIXNU_UEFI or ../Mu-Silicium)')
    parser.add_argument('--out', type=Path, help='UniXNU output root (default: <unixnu>/out)')
    arguments = parser.parse_args(argv)
    result = inventory(arguments.device, arguments.unixnu, arguments.uefi, arguments.out)
    json.dump(result, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write('\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
