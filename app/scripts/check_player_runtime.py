"""Check installed runtime dependencies without executing the guest game."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from GravityRushLauncher import selected_probe_exe, GUEST_START_EXE, BASE_DIR
from runtime_files import dependency_closure

try:
    probe = selected_probe_exe()
    if not probe.is_file() or not GUEST_START_EXE.is_file():
        raise ValueError('Install a player runtime bundle or run Setup.ps1 -Build first.')
    dependencies = dependency_closure([probe, GUEST_START_EXE], [BASE_DIR / 'bin'])
    print(f'Runtime ready: {probe.name}; {len(dependencies)} local DLL dependencies resolved.')
except (OSError, ValueError) as exc:
    sys.exit(f'Runtime check failed: {exc}')
