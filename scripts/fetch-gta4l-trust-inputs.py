#!/usr/bin/env python3
"""Fetch the iPadOS 17.7.11 static and System trust modules by HTTP range.

Only BuildManifest.plist and the two manifest-selected trust-cache members
are read from the public restore image. A server that ignores Range is
refused. Each member is checked against its manifest size and SHA-384
before it is published under --output.
"""
import argparse
import hashlib
import io
import json
import plistlib
import urllib.request
import zipfile
from pathlib import Path

DEFAULT_URL = ('https://updates.cdn-apple.com/2025SpringFCS/fullrestores/'
               '122-77166/DCA6CFE8-90E9-4B92-880D-3DE171798E40/'
               'iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw')
MAX_RANGE_BYTES = 4 << 20


class HttpRangeFile(io.RawIOBase):
    def __init__(self, url):
        super().__init__()
        self.url = url
        self.position = 0
        request = urllib.request.Request(url, method='HEAD')
        with urllib.request.urlopen(request, timeout=60) as response:
            self.size = int(response.headers['Content-Length'])

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self.position

    def seek(self, offset, whence=io.SEEK_SET):
        if whence == io.SEEK_SET:
            position = offset
        elif whence == io.SEEK_CUR:
            position = self.position + offset
        elif whence == io.SEEK_END:
            position = self.size + offset
        else:
            raise ValueError('bad whence')
        if position < 0:
            raise ValueError('negative seek')
        self.position = position
        return self.position

    def readinto(self, buffer):
        count = min(len(buffer), MAX_RANGE_BYTES)
        if count == 0 or self.position >= self.size:
            return 0
        end = min(self.position + count, self.size) - 1
        request = urllib.request.Request(
            self.url, headers={'Range': f'bytes={self.position}-{end}'})
        with urllib.request.urlopen(request, timeout=120) as response:
            if response.status != 206:
                raise SystemExit('server ignored Range; refusing a full image')
            expected = f'bytes {self.position}-{end}/{self.size}'
            if response.headers.get('Content-Range') != expected:
                raise SystemExit(
                    f'bad Content-Range: {response.headers.get("Content-Range")}')
            data = response.read()
        if len(data) != end - self.position + 1:
            raise SystemExit('short range response')
        buffer[:len(data)] = data
        self.position += len(data)
        return len(data)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_member(archive, component, destination):
    path = component['Info']['Path']
    info = archive.getinfo(path)
    digest = hashlib.sha384()
    content = hashlib.sha256()
    count = 0
    temporary = destination.with_suffix(destination.suffix + '.partial')
    try:
        with archive.open(info) as source, temporary.open('xb') as target:
            while True:
                chunk = source.read(MAX_RANGE_BYTES)
                if not chunk:
                    break
                target.write(chunk)
                digest.update(chunk)
                content.update(chunk)
                count += len(chunk)
        if count != info.file_size:
            raise SystemExit(f'{path}: incomplete member')
        if digest.digest() != component['Digest']:
            raise SystemExit(f'{path}: SHA-384 does not match the manifest')
        temporary.rename(destination)
    finally:
        temporary.unlink(missing_ok=True)
    return {
        'member': path,
        'bytes': count,
        'sha384': digest.hexdigest(),
        'sha256': content.hexdigest(),
        'zip_crc_verified': True,
        'manifest_digest_verified': True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default=DEFAULT_URL)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / 'BuildManifest.plist'
    if manifest_path.exists():
        raise SystemExit(f'{manifest_path}: already exists; inspect it first')
    with zipfile.ZipFile(HttpRangeFile(args.url)) as archive:
        manifest_bytes = archive.read('BuildManifest.plist')
        manifest = plistlib.loads(manifest_bytes)
        if (manifest['ProductVersion'] != '17.7.11' or
                manifest['ProductBuildVersion'] != '21H461' or
                'iPad7,1' not in manifest['SupportedProductTypes']):
            raise SystemExit('wrong product, version or build')
        identities = [item for item in manifest['BuildIdentities']
                      if item['Info']['DeviceClass'] == 'j120ap']
        identity = next(item for item in identities
                        if item['Info']['RestoreBehavior'] == 'Erase' and
                        int(item['ApChipID'], 16) == 0x8011 and
                        int(item['ApBoardID'], 16) == 12)
        records = {}
        for name in ('StaticTrustCache', 'Cryptex1,SystemTrustCache'):
            component = identity['Manifest'][name]
            member = Path(component['Info']['Path']).name
            destination = output / member
            if destination.exists():
                raise SystemExit(f'{destination}: already exists')
            records[name] = read_member(archive, component, destination)
            records[name]['path'] = str(destination)
        for item in identities:
            for name in ('StaticTrustCache', 'Cryptex1,SystemTrustCache'):
                if item['Manifest'][name]['Digest'] != identity['Manifest'][name]['Digest']:
                    raise SystemExit(f'{name}: j120ap identities disagree')
    manifest_path.write_bytes(manifest_bytes)
    report = {
        'url': args.url,
        'product': 'iPad7,1',
        'version': manifest['ProductVersion'],
        'build': manifest['ProductBuildVersion'],
        'manifest': str(manifest_path),
        'manifest_sha256': sha256(manifest_bytes),
        'components': records,
        'full_ipsw_downloaded': False,
    }
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
