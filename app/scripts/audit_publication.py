"""Audit Git-eligible source without staging, committing or exposing credential contents."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

REPO = Path(__file__).resolve().parents[2]
PATTERNS = {
    'package-reference': re.compile(rb'\.pkg', re.IGNORECASE),
    'release-group-reference': re.compile(rb'\[SPSX\]', re.IGNORECASE),
    'local-testing-path': re.compile(rb'C:\\Testing', re.IGNORECASE),
    'private-key': re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
    'github-token': re.compile(rb'\b(?:ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b'),
    'aws-access-key': re.compile(rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
}

# Exact reviewed file/kind pairs and hashes of their matching lines.
# Any new or changed matching line, even in these files, requires review.
ALLOWLIST = {
    ('.gitignore', 'package-reference'):
        '3916173371f1b8194e86ed74e40c314fec6f647a6f9e5a3206682ca1f755e997',
    ('app/LAUNCHER_START.md', 'local-testing-path'):
        '4b9bf125260235b47ae304358bea0de3d2c6bd16529d8287a79f3a64a16bb43a',
    ('app/PROJECT_STATUS_REPORT.md', 'local-testing-path'):
        '571c58c075686f428251eaf94c1f8b5b1aaa7fe7f1316c089a265330b74da429',
    ('app/REPORT.md', 'local-testing-path'):
        '7a237eb0eb56b8ffdd16b4539cca93f7754589086d1569572ac9cd667bddddbb',
    ('app/scripts/audit_publication.py', 'package-reference'):
        '4083f2eac159005e50a8702039079ca22137668bb2d693c5e9c8c1f641e8aa97',
    ('app/scripts/package_player.py', 'package-reference'):
        '60dc0b9af3798bd9fad5ac6246b21f4820d3b2ba6caf41974803a9a66253234a',
    ('app/tests/test_player_setup.py', 'package-reference'):
        '26e0ec590551c8a1ad675ed11b63d2b4d21f726c2c03bd7ce88b32c2dff9c014',
    ('app/tools/vulkan-validation/README.md', 'package-reference'):
        'e4e45bc13664cfe0a407984c639b4495a7f9f27b8d1c1b863a84412dbccea18b',
    ('runtime/packaging/windows/package.sh', 'package-reference'):
        'af42165aa2768dbcec580f9381ef3d1c6ef31aa481c60e9251c35df161cd1827',
    ('runtime/packaging/windows/smoke-test.ps1', 'package-reference'):
        'e71151b23e602e6caf8924194d366db632862c590ebdac5164d84ab2cdc19be6',
}

SYNTHETIC_FIXTURE = 'runtime/tests/data/isa_fixture.bin'
FIXTURE_SHA = '3d30e8e0f858b93a960cf7db915df83f100bf161597e58c5eccf99b89ff4c47f'


def pattern_findings(relative, data):
    findings = []
    allowlisted = []
    for kind, pattern in PATTERNS.items():
        matched = [line for line in data.splitlines() if pattern.search(line)]
        if not matched:
            continue
        item = {'file': relative, 'kind': kind}
        fingerprint = hashlib.sha256(b'\n'.join(matched)).hexdigest()
        if ALLOWLIST.get((relative, kind)) == fingerprint:
            allowlisted.append(item)
        else:
            findings.append(item)
    return findings, allowlisted


def audit():
    raw = subprocess.check_output(['git', '-C', str(REPO), 'ls-files', '--cached', '--others',
                                   '--exclude-standard', '-z'])
    paths = sorted({p.decode('utf-8') for p in raw.split(b'\0') if p})
    findings = []
    allowlisted = []
    files = []
    for relative in paths:
        path = REPO / relative
        if path.is_dir():
            findings.append({'file': relative, 'kind': 'embedded-git-checkout'})
            continue
        if path.is_symlink() or not path.resolve().is_relative_to(REPO):
            findings.append({'file': relative, 'kind': 'external-link'})
            continue
        if not path.is_file():
            findings.append({'file': relative, 'kind': 'missing-index-file'})
            continue
        files.append(path)
        parts = set(p.lower() for p in Path(relative).parts)
        if parts & {'game', 'gamer', 'user', 'savedata', 'out', 'logs', 'local-data',
                    'dist', '_internal', 'references', '.git'}:
            findings.append({'file': relative, 'kind': 'local-or-game-data'})
        if path.suffix.lower() in {'.pkg', '.prx', '.sprx', '.sxd1', '.sxd2', '.arc', '.psarc',
                                   '.exe', '.dll', '.pdb', '.pyd', '.zip', '.zst', '.bin'}:
            known_fixture = relative == SYNTHETIC_FIXTURE and hashlib.sha256(path.read_bytes()).hexdigest() == FIXTURE_SHA
            if not known_fixture:
                findings.append({'file': relative, 'kind': 'binary-requires-review'})
        size = path.stat().st_size
        if size > 95 * 1024**2:
            findings.append({'file': relative, 'kind': 'large-file'})
        if size < 20 * 1024**2:
            # Scan every eligible file, including .md documentation.
            data = path.read_bytes()
            detected, accepted = pattern_findings(relative, data)
            findings.extend(detected)
            allowlisted.extend(accepted)
    return files, {'eligible_files': len(files), 'eligible_bytes': sum(p.stat().st_size for p in files),
                   'findings': findings, 'allowlisted': allowlisted, 'scope': 'Git-eligible source; pattern scan, not a legal or security guarantee'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', type=Path, help='New directory inside ignored local-data for a clean-source configure check')
    args = parser.parse_args()
    files, report = audit()
    print(json.dumps(report, indent=2))
    if report['findings']:
        raise SystemExit(1)
    if args.export:
        target = args.export.resolve()
        if not target.is_relative_to((REPO / 'local-data').resolve()) or target.exists():
            parser.error('Export must be a new directory inside ignored local-data.')
        target.mkdir(parents=True)
        for source in files:
            destination = target / source.relative_to(REPO)
            # Source snapshots contain deep SDK paths; do not require a global
            # Windows long-path setting just to export them.
            if os.name == 'nt':
                source = Path('\\\\?\\' + str(source))
                destination = Path('\\\\?\\' + str(destination))
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        print(f'Clean source exported: {target}')
