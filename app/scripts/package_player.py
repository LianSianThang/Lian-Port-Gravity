"""Assemble an experimental player ZIP with source, excluding all local game data."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from GravityRushLauncher import selected_probe_exe, GUEST_START_EXE
from runtime_files import dependency_closure, write_manifest, sha256

REPO = Path(__file__).resolve().parents[2]


def filesystem_path(path):
    path = Path(path).resolve()
    if os.name == 'nt' and not str(path).startswith('\\\\?\\'):
        return Path('\\\\?\\' + str(path))
    return path


def forbidden(relative):
    path = Path(relative)
    parts = set(p.lower() for p in path.parts)
    return bool(parts & {'.git', 'game', 'gamer', 'savedata', 'sce_sys', 'sce_module',
                         'user', 'cache', '__pycache__', 'local-data', 'references', 'out', 'logs', 'dist'}) or (
        path.suffix.lower() in {'.pkg', '.prx', '.sprx', '.sxd1', '.sxd2', '.log', '.bak', '.spv'} or
        path.name.lower() in {'eboot.bin', 'boot_gravity.bin', 'launcher_settings.json'})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler-root', type=Path, default=Path('C:/msys64/clang64'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    stamp = datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S')
    output = (args.output or REPO / 'dist' / f'player_{stamp}').resolve()
    if output.exists():
        parser.error('Use a new output directory; existing packages are never overwritten.')
    output = filesystem_path(output)
    probe = selected_probe_exe()
    launcher_bundle = REPO / 'app/out/frozen/Launch_Lian_Port'
    launcher = launcher_bundle / 'Launch_Lian_Port.exe'
    for required in (probe, GUEST_START_EXE, launcher):
        if not required.is_file():
            parser.error(f'Missing build input: {required}')
    if not list((launcher_bundle / '_internal').glob('python3*.dll')):
        parser.error('Bundled Python is missing. Run app/scripts/freeze_launcher.ps1 first.')
    dependencies = dependency_closure([probe, GUEST_START_EXE], [probe.parent, args.compiler_root / 'bin'])
    source_list = subprocess.check_output(['git', '-C', str(REPO), 'ls-files', '--cached', '--others',
                                           '--exclude-standard', '-z']).split(b'\0')
    output.mkdir(parents=True)
    project = output / 'Lian-Port-Gravity'
    for raw in sorted(set(source_list)):
        if not raw:
            continue
        relative = Path(raw.decode('utf-8'))
        if forbidden(relative) or relative.suffix.lower() in ('.exe', '.dll', '.lnk'):
            continue
        source = REPO / relative
        if source.is_symlink() or not source.resolve().is_relative_to(REPO):
            raise ValueError(f'Source path escapes repository: {relative}')
        if source.is_file():
            target = project / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(filesystem_path(source), target)
    bin_dir = project / 'app/bin'
    bin_dir.mkdir(parents=True)
    shutil.copy2(probe, bin_dir / 'gravity-probe-local.exe')
    shutil.copy2(GUEST_START_EXE, bin_dir / 'lianport-guest-start.exe')
    for dependency in dependencies:
        shutil.copy2(dependency, bin_dir / dependency.name)
    write_manifest(bin_dir)
    shutil.copytree(launcher_bundle, project, dirs_exist_ok=True)
    # Include the Python/PyInstaller distribution notices shipped by the build environment.
    python_license = Path(sys.base_prefix) / 'LICENSE.txt'
    if not python_license.is_file():
        raise ValueError('Python distribution license is missing.')
    shutil.copy2(python_license, project / 'PYTHON_LICENSE.txt')
    freezer_license = REPO / 'app/out/launcher-build-venv/Lib/site-packages/pyinstaller-6.22.3.dist-info/licenses/COPYING.txt'
    if not freezer_license.is_file():
        raise ValueError('PyInstaller license/bootloader exception is missing.')
    shutil.copy2(freezer_license, project / 'PYINSTALLER_LICENSE.txt')
    licenses = args.compiler_root / 'share/licenses'
    if not licenses.is_dir():
        raise ValueError('Toolchain dependency license directory is missing.')
    shutil.copytree(licenses, project / 'dependency-licenses')
    (project / 'START_HERE.txt').write_text(
        'Lian Port Gravity Rush — experimental player package\n\n'
        'Windows and a Vulkan-capable graphics driver are required. Python and Tkinter are bundled.\n'
        'Put your extracted, decrypted CUSA01130 game in game/CUSA01130 inside this folder.\n'
        'Open Launch_Lian_Port.exe and press Launch; preparation happens automatically.\n'
        'No game files or saves are included. Full-story completion is not yet verified.\n'
        'Keep the _internal and app folders beside the launcher. Source and notices are included.\n', encoding='utf-8')
    # Audit the final tree, independent of Git's current ignore rules.
    files = [p for p in output.rglob('*') if p.is_file()]
    for path in files:
        relative = path.relative_to(output)
        if forbidden(relative):
            raise ValueError(f'Forbidden player-package file: {relative}')
    manifest = {'created_utc': stamp, 'runtime_source': probe.name, 'runtime_sha256': sha256(probe),
                'dll_count': len(dependencies), 'source_included': True, 'game_included': False,
                'python_bundled': True, 'launcher': 'Lian-Port-Gravity/Launch_Lian_Port.exe',
                'files': {p.relative_to(output).as_posix(): sha256(p) for p in files}}
    (project / 'package_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    archive = Path(str(output) + '.zip')
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=1) as zipped:
        zipped.writestr('Lian-Port-Gravity/game/CUSA01130/', '')
        for path in output.rglob('*'):
            if path.is_file():
                zipped.write(path, path.relative_to(output))
    display_archive = str(archive).removeprefix('\\\\?\\')
    print(f'Player package: {display_archive}\nDLLs: {len(dependencies)}; no game data or user profiles included.')


if __name__ == '__main__':
    main()
