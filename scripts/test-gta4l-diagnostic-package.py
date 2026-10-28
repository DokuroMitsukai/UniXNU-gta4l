#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Integration self-test for scripts/package-gta4l-diagnostic.py.

Uses the retained v5 baseline and audit payload when the evidence directory is
present. Verifies that a valid package reproduces the hardware-tested v2 image
and that wrong baseline hashes, wrong payload hashes, a wrong payload size and
an existing output are refused.
"""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
PACKAGER = Path(__file__).resolve().with_name('package-gta4l-diagnostic.py')
BASELINE = REPO / 'out/gta4l/boot/boot-ram-root-sdhci-v5.img'
PAYLOAD = REPO / 'out/gta4l/ram-root-sdhci-audit-evidence/payload.img'
EXPECTED_IMAGE_SHA256 = (
    'a34519c66820eb5f1f4c4790be5e10233ffd4c8a4ece2df4768e7dca151c8546'
)
EXPECTED_PAYLOAD_SHA256 = (
    '31b78bec7529706407f682e64dbc9c4c990e86009025174fd3f791606b29ea31'
)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args):
    return subprocess.run(
        [sys.executable, str(PACKAGER), *map(str, args)],
        capture_output=True, text=True)


def base_args(output, manifest):
    return [
        '--baseline', BASELINE,
        '--payload', PAYLOAD,
        '--payload-sha256', EXPECTED_PAYLOAD_SHA256,
        '--output', output,
        '--manifest', manifest,
    ]


def main():
    if not BASELINE.is_file() or not PAYLOAD.is_file():
        print('SKIP: retained v5 baseline or audit payload is not present')
        return 0

    tmp = Path(tempfile.mkdtemp(prefix='gta4l-package-test-'))
    try:
        output = tmp / 'reproduced.img'
        manifest = tmp / 'reproduced.json'
        result = run(base_args(output, manifest))
        assert result.returncode == 0, result.stderr
        assert output.is_file()
        assert sha(output) == EXPECTED_IMAGE_SHA256, sha(output)
        data = json.loads(manifest.read_text())
        assert data['output']['sha256'] == EXPECTED_IMAGE_SHA256
        assert data['payload']['sha256'] == EXPECTED_PAYLOAD_SHA256
        assert data['component_gate']['pass'] is True
        assert data['only_ramdisk_differs'] is True
        print('PASS valid package reproduces v2 byte-for-byte')

        bad = tmp / 'wrong-baseline.img'
        result = run(base_args(bad, tmp / 'wrong-baseline.json')
                     + ['--baseline-sha256', '0' * 64])
        assert result.returncode != 0
        assert not bad.exists()
        print('PASS wrong baseline hash refused')

        result = run(base_args(tmp / 'wrong-bytes.img',
                               tmp / 'wrong-bytes.json')
                     + ['--baseline-bytes', '1'])
        assert result.returncode != 0
        assert not (tmp / 'wrong-bytes.img').exists()
        print('PASS wrong baseline size refused')

        bad = tmp / 'wrong-payload.img'
        result = run(base_args(bad, tmp / 'wrong-payload.json')
                     + ['--payload-sha256', '0' * 64])
        assert result.returncode != 0
        assert not bad.exists()
        print('PASS wrong payload hash refused')

        short = tmp / 'short.bin'
        short.write_bytes(b'X' * 1024)
        bad = tmp / 'short-payload.img'
        result = run([
            '--baseline', BASELINE,
            '--payload', short,
            '--payload-sha256', sha(short),
            '--output', bad,
            '--manifest', tmp / 'short-payload.json',
        ])
        assert result.returncode != 0
        assert not bad.exists()
        print('PASS wrong payload size refused')

        existing = tmp / 'existing.img'
        existing.write_bytes(b'do not replace')
        result = run(base_args(existing, tmp / 'existing.json'))
        assert result.returncode != 0
        assert existing.read_bytes() == b'do not replace'
        print('PASS existing output refused without --force')
    finally:
        for path in sorted(tmp.iterdir()):
            path.unlink()
        tmp.rmdir()
    print('PASS gta4l diagnostic packager self-test')
    return 0


if __name__ == '__main__':
    sys.exit(main())
