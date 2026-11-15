#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host test for the gta4l window-policy signer.

Runs the signer's own self-test (real RSA, the kit's C parser, block and
negative controls), then checks the committed generation inputs: the two
signed windows, their table objects, the C header arrays and the reported
hashes. No device access.
"""
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts' / 'build-gta4l-window-policies.py'
OUT = ROOT / 'out' / 'gta4l' / 's5-policies'
EXPECTED = {
    'OS': ('04769D26-2CA0-4825-B443-E2E5F88300F2', 1483785,
           47481120, '5f14cb46c74da9cfd64a456d589c845d81ffe3e7547fd035ca3a1961c9eefcb8'),
    'SystemOS': ('8846FE78-2910-4297-AF97-D59BB6F8FC16', 806400,
                 25804800, 'e3cbe58362060689f806cc81ecd5569debedcfa0b8df4da9875bbd79ba01afc8'),
}


def array(header, name):
    match = re.search(r'static const uint8_t ' + name + r'\[(\d+)\] = \{(.*?)\};', header, re.S)
    if not match:
        raise SystemExit(f'{name}: array not found')
    data = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})', match.group(2)))
    if len(data) != int(match.group(1)):
        raise SystemExit(f'{name}: declared size differs')
    return data


def main():
    run = subprocess.run([sys.executable, '-B', str(SCRIPT), '--self-test'],
                         capture_output=True, text=True)
    if run.returncode or 'PASS: policy self-test' not in run.stdout:
        print(run.stdout + run.stderr)
        raise SystemExit('signer self-test failed')
    report = json.loads((OUT / 'policies.json').read_text())
    header = (OUT / 'Gta4lEmmcBlockPolicies.h').read_text()
    anchor = (OUT / 'OS' / 'trusted-key.bin').read_bytes()
    if hashlib.sha256(anchor).hexdigest() != report['trusted_key_sha256']:
        raise SystemExit('anchor hash differs from the report')
    if array(header, 'gta4l_owner_anchor') != anchor:
        raise SystemExit('C header anchor differs')
    for entry in report['windows']:
        name = entry['name']
        expected_uuid, blocks, table_bytes, table_sha = EXPECTED[name]
        if entry['uuid'] != expected_uuid or entry['blocks'] != blocks:
            raise SystemExit(f'{name}: identity differs from the expected window')
        if entry['table_sha256'] != table_sha:
            raise SystemExit(f'{name}: table hash differs')
        obj = ROOT / entry['object']
        if not obj.is_file() or obj.stat().st_size != table_bytes or \
                hashlib.sha256(obj.read_bytes()).hexdigest() != table_sha:
            raise SystemExit(f'{name}: object differs from the signed table')
        header_bytes = (OUT / name / 'header.bin').read_bytes()
        signature = (OUT / name / 'signature.bin').read_bytes()
        if len(header_bytes) != 160 or len(signature) != 384:
            raise SystemExit(f'{name}: unexpected envelope size')
        if array(header, f'gta4l_{name.lower()}_policy_header') != header_bytes or \
                array(header, f'gta4l_{name.lower()}_policy_signature') != signature:
            raise SystemExit(f'{name}: C header differs from the signed policy')
        manifest = (OUT / name / 'manifest.bin').read_bytes()
        if manifest[:160] != header_bytes or manifest[160:544] != signature:
            raise SystemExit(f'{name}: manifest envelope differs')
    print('PASS: window policies (signer self-test, objects, header and report)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
