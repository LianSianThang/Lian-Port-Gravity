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
    'private-key': re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
    'github-token': re.compile(rb'\b(?:ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b'),
    'aws-access-key': re.compile(rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
}
SYNTHETIC_FIXTURE = 'runtime/tests/data/isa_fixture.bin'
FIXTURE_SHA = '3d30e8e0f858b93a960cf7db915df83f100bf161597e58c5eccf99b89ff4c47f'


def audit():
    raw = subprocess.check_output(['git', '-C', str(REPO), 'ls-files', '--cached', '--others',
                                   '--exclude-standard', '-z'])
    paths = sorted({p.decode('utf-8') for p in raw.split(b'\0') if p})
    findings = []
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
            data = path.read_bytes()
            for kind, pattern in PATTERNS.items():
                if pattern.search(data):
                    findings.append({'file': relative, 'kind': kind})
    return files, {'eligible_files': len(files), 'eligible_bytes': sum(p.stat().st_size for p in files),
                   'findings': findings, 'scope': 'Git-eligible source; pattern scan, not a legal or security guarantee'}


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
