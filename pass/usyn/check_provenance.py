#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Check captured input integrity, not frontend dependency completeness or LEC."""
import argparse
import hashlib
import json
import pathlib
import re
import sys


def check(directory, report=None):
    directory = pathlib.Path(directory)
    path = directory / 'manifest.json'
    if path.is_symlink() or path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError('invalid or oversized manifest')
    data = path.read_bytes()
    manifest = json.loads(data)
    if (type(manifest.get('schema_version')) is not int or manifest['schema_version'] != 1
            or manifest.get('kind') != 'synth_provenance'):
        raise ValueError('unsupported provenance schema')
    if manifest.get('scope') != 'kernel_observed_inputs_before_mapping' or manifest.get('dependency_closure_certified') is not False:
        raise ValueError('unsupported provenance scope')
    bound = manifest['byte_limit']
    entries = manifest['entry_limit']
    if type(bound) is not int or not 0 <= bound <= 64 * 1024 * 1024 or type(entries) is not int or not 0 <= entries <= 4096:
        raise ValueError('invalid archive bounds')
    rows = manifest['files']
    if not isinstance(rows, list) or len(rows) > 2 * entries + 1:
        raise ValueError('file entry limit')
    blobs = set()
    count = total = omitted = 0
    for row in rows:
        if row['status'] != 'captured':
            omitted += 1
            continue
        blob = row['blob']
        if not isinstance(blob, str) or not re.fullmatch(r'files/[0-9]+\.bin', blob) or blob in blobs:
            raise ValueError('invalid or repeated blob path')
        blobs.add(blob)
        source = directory / blob
        if (directory / 'files').is_symlink() or source.is_symlink() or not source.is_file():
            raise ValueError('blob must be an archived regular file')
        size = row['bytes']
        if type(size) is not int or size < 0 or source.stat().st_size != size or size > bound - total:
            raise ValueError('invalid blob size or archive budget')
        digest = hashlib.sha256()
        with source.open('rb') as stream:
            remaining = size
            while remaining:
                block = stream.read(min(65536, remaining))
                if not block:
                    raise ValueError('truncated blob')
                digest.update(block)
                remaining -= len(block)
            if stream.read(1):
                raise ValueError('blob grew during verification')
        if digest.hexdigest() != row['sha256']:
            raise ValueError('blob digest mismatch')
        total += size
        count += 1
    if total != manifest['bytes'] or type(manifest['capture_complete']) is not bool:
        raise ValueError('incorrect capture accounting')
    if manifest['capture_complete'] and (omitted or not isinstance(manifest.get('context'), dict)):
        raise ValueError('incomplete capture advertised as complete')
    if report is not None:
        binding = json.loads(pathlib.Path(report).read_text())['provenance']
        capture = binding['capture']
        if (binding['directory'] != directory.name or capture['manifest_sha256'] != hashlib.sha256(data).hexdigest()
                or capture['captured_files'] != count or capture['omitted_files'] != omitted
                or capture['bytes'] != total or capture['capture_complete'] != manifest['capture_complete']):
            raise ValueError('report does not bind this capture')
    return {'captured_files': count, 'bytes': total, 'capture_complete': manifest['capture_complete'],
            'dependency_closure_certified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory')
    parser.add_argument('--report')
    args = parser.parse_args()
    try:
        result = check(args.directory, args.report)
        print(json.dumps(result, sort_keys=True))
        return 0 if result['capture_complete'] else 2
    except (OSError, ValueError, KeyError, TypeError) as error:
        print('invalid provenance: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
