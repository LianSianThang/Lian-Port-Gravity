"""Install a freshly built probe, its startup helper and required toolchain DLLs."""
import argparse
from pathlib import Path
import shutil
from runtime_files import dependency_closure, write_manifest

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--probe', type=Path, required=True)
parser.add_argument('--helper', type=Path, required=True)
parser.add_argument('--compiler-bin', type=Path, required=True)
parser.add_argument('--destination', type=Path, required=True)
args = parser.parse_args()
dependencies = dependency_closure([args.probe, args.helper], [args.compiler_bin])
args.destination.mkdir(parents=True, exist_ok=True)
for source in dependencies:
    shutil.copy2(source, args.destination / source.name)
shutil.copy2(args.probe, args.destination / 'gravity-probe-local.exe')
shutil.copy2(args.helper, args.destination / 'lianport-guest-start.exe')
write_manifest(args.destination)
print(f'Installed runtime with {len(dependencies)} DLL dependencies.')
