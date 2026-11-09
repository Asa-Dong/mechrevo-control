#!/usr/bin/env python3
"""Check or synchronize release metadata from VERSION (GNOME uses an integer too)."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--write', action='store_true', help='synchronize derived metadata')
args = parser.parse_args()
version = (ROOT / 'VERSION').read_text().strip()
if not re.fullmatch(r'\d+\.\d+\.\d+', version):
    parser.error('VERSION must contain major.minor.patch')
changes = {}
for name, pattern, replacement in (
    ('driver/dkms.conf', r'PACKAGE_VERSION="[^"]+"', f'PACKAGE_VERSION="{version}"'),
    ('driver/mechrevo_control.c', r'MODULE_VERSION\("[^"]+"\)', f'MODULE_VERSION("{version}")'),
):
    text = (ROOT / name).read_text()
    updated, count = re.subn(pattern, replacement, text)
    if count != 1:
        parser.error(f'{name}: expected exactly one version declaration')
    if updated != text:
        changes[name] = updated
name = 'extension/metadata.json'
metadata = json.loads((ROOT / name).read_text())
if not isinstance(metadata.get('version'), int) or metadata['version'] < 1:
    parser.error('GNOME extension version must be a positive integer')
if metadata.get('version-name') != version:
    metadata['version-name'] = version
    changes[name] = json.dumps(metadata, ensure_ascii=False, indent=2) + '\n'
if changes and not args.write:
    parser.error('version mismatch: ' + ', '.join(changes) + '; run scripts/version.py --write')
for name, text in changes.items():
    (ROOT / name).write_text(text)
print(f'Version metadata: {version}')
