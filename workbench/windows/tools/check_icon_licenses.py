"""Offline gate for vendored icon provenance, license headers and unchanged bytes."""
import hashlib
import argparse
import json
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[1] / 'assets' / 'icons'
manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
listed = set()
for item in manifest['assets']:
    name = item['file']
    assert pathlib.PurePosixPath(name).name == name and name.endswith('.svg'), name
    assert name not in listed, f'Duplicate: {name}'
    listed.add(name)
    data = (root / name).read_bytes()
    assert hashlib.sha256(data).hexdigest() == item['sha256'], f'Unreviewed change: {name}'
    assert item['modified'] is False and item['commercial_use'] is True, name
    if name == 'python.svg':
        assert item['license'] == 'LicenseRef-PSF-Logo' and item['permission'].startswith('https://www.python.org/psf/trademarks/'), name
        assert item['source'] == 'https://s3.dualstack.us-east-2.amazonaws.com/pythondotorg-assets/media/files/python-logo-only.svg'
        assert item['usage'].startswith('Unaltered artwork identifies Python'), name
    else:
        assert item['license'] == 'Apache-2.0', name
        assert b'governed by the Apache 2.0 license' in data, f'Missing explicit per-file license: {name}'
        assert item['source'].startswith('https://raw.githubusercontent.com/JetBrains/intellij-community/' + manifest['upstream_commit'] + '/'), name
    assert not re.search(rb'<(?:script|foreignObject)\b', data, re.I), name
    assert not re.search(rb'(?:xlink:)?href\s*=\s*[\'"](?!#)', data), f'External reference: {name}'

assert listed == {p.name for p in root.glob('*.svg')}, 'Unreviewed or missing SVG file'
for name in ('Apache-2.0.txt', 'JetBrains-NOTICE.txt', 'Python-logo-NOTICE.txt'):
    assert (root / 'licenses' / name).stat().st_size > 100, f'Missing notice: {name}'
assert b'END OF TERMS AND CONDITIONS' in (root / 'licenses/Apache-2.0.txt').read_bytes()
result = json.dumps({'passed': True, 'svg_files': len(listed), 'apache_2_0_files': len(listed) - 1,
                  'psf_permitted_original_logo': 1, 'excluded_unlicensed_candidates': len(manifest['excluded_candidates']),
                  'checks': ['sha256', 'per-file license', 'commercial-use permission', 'official source',
                             'complete license and notices', 'no unlisted assets', 'no external SVG resources']}, indent=2)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--report', type=pathlib.Path)
args = parser.parse_args()
if args.report:
    args.report.write_text(result + '\n', encoding='utf-8')
print(result)
