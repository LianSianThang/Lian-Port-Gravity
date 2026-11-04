# Lian Port — Gravity Rush Remastered

Hobby project, no longer actively maintained. Based on SpartanPort and shadPS4 (GPL-2.0); see [LICENSES/](LICENSES/) and [NOTICE.md](NOTICE.md).

An **experimental community PC runtime for Gravity Rush Remastered (PS4),
CUSA01130**, built on SpartanPort and shadPS4 with additional upstream and local
Gravity Rush work. Original x86-64 game code runs through the runtime; graphics
commands and shaders are translated to Vulkan. This is not a from-source
recreation of the game or an official Sony release.

**Early gameplay works. Start-to-ending compatibility is not verified.**
Later episodes, bosses and longer sessions may encounter crashes, missing
functionality or stuck transitions. No reliable crash percentage is available.

No game dump, PKG, executable, prepared boot image, save or shader cache ships
in the source repository or player ZIP. Supply your own **extracted, decrypted
CUSA01130**. The preparation tools do not decrypt retail PKGs.

## Current status

| Area | Evidence and limits |
|---|---|
| Launcher | Branded Windows player build; bundled Python/Tkinter; automatic preparation |
| Rendering | Title menu and early gameplay demonstrated locally |
| Audio | Voices and streamed music demonstrated; extended audiovisual timing needs testing |
| Progression | Local checkpoints cover Episodes 1–3; Episode 3 completed save resumes into gameplay/map |
| Later episodes / ending | **Untested; not claimed fully playable** |
| Controls | Keyboard/mouse implemented; controller recommended; contextual finishers need broader testing |
| Display | Windowed/borderless tested; exclusive fullscreen falls back to borderless if Windows rejects it |
| Graphics settings | Mode, VSync and presentation FPS apply; resolution/FSR/sharpening launcher controls remain prototypes |
| Stability | Earlier NVIDIA driver/watchdog faults investigated; short successful runs do not establish all-game stability |

See [checkpoint testing](CHECKPOINT_TESTING.md) and the
[status report](app/PROJECT_STATUS_REPORT.md). The latter contains historical
paths/hypotheses; its current checkpoint sections take precedence.
Documentation updated **3 November 2026**.

## Player requirements

| Requirement | Details |
|---|---|
| OS | 64-bit Windows; the inherited runtime targets Windows 10 1903+ / Windows 11. Other platforms are not validated here. |
| CPU | x86-64 with SSE4.2, POPCNT and AVX. Older-CPU instruction rewriting exists upstream; compatibility/performance here is not established. |
| GPU | Vulkan **1.3** and a working vendor driver. Local rendering demonstrated on an NVIDIA GeForce RTX 3050 6 GB Laptop GPU; this is a tested device, not a measured minimum. |
| RAM / VRAM | Minimum capacities and performance targets have not been established. Memory pressure/allocation failure can prevent startup. |
| Storage | Space for the extracted game (about 9.2 GiB for the local dump), application, prepared image and growing caches. Source builds need additional space. |
| Game | Extracted, decrypted **CUSA01130**, with the files below. Other regions/title IDs are not accepted by this setup. |
| Input | Keyboard/mouse or SDL-compatible controller. Original controller prompts remain in-game. |
| Python | **Not required for the bundled player ZIP.** Python/Tcl/Tk are included. Source development requires Python 3.11+ with Tkinter. |

Requirement sources: local [CPU checks](runtime/src/cpu_check.c),
[Vulkan target](runtime/gpu/shadps4/video_core/renderer_vulkan/vk_platform.h),
[Windows runtime documentation](runtime/packaging/windows/README-Windows.txt),
and local test evidence. Upstream God of War benchmarks/compatibility do not
establish Gravity Rush performance or compatibility.

## Download and play

Download the **player ZIP from [GitHub Releases](https://github.com/LianSianThang/Lian-Port-Gravity/releases)**.
GitHub's automatic **Source code ZIP** does not contain launcher binaries or
runtime DLLs. Preview releases are experimental, with the limitations above.

1. Extract the whole player ZIP and open its `Lian-Port-Gravity` folder.
2. Keep `Launch_Lian_Port.exe`, `_internal` and `app` together.
3. Put your extracted game in `game/CUSA01130`, or use **Choose Game Folder**.
4. Open **Launch_Lian_Port.exe**, then press **Launch Game**. First launch
   prepares a local boot image automatically before starting the runtime.

```text
Lian-Port-Gravity/
├─ Launch_Lian_Port.exe
├─ _internal/                    bundled Python/Tcl/Tk; keep this folder
├─ app/
│  ├─ bin/                       runtime and DLLs
│  ├─ branding/                  Lian Port artwork
│  ├─ out/                       generated locally
│  ├─ user/                      saves/profile data
│  └─ logs/                      local diagnostics
└─ game/
   └─ CUSA01130/
      ├─ eboot.bin
      ├─ sce_sys/param.sfo
      ├─ sce_module/libc.prx
      ├─ sce_module/libSceFios2.prx
      └─ arc/                    extracted game archives
```

A PKG alone is not playable. Preparation checks the title ID and fingerprints
the executable/modules so a different dump is prepared again. It does not
modify game files. Fresh packages use `app/user` and contain no developer saves.
Back up saves before testing updates. Full instructions: [PLAYER_SETUP.md](PLAYER_SETUP.md).

## Controls

**Controller recommended** for matching the original prompts. Keyboard/mouse
is an alternative; the launcher lists PS4 prompts beside PC bindings.

| Action | PC binding |
|---|---|
| Move / look | WASD / mouse |
| Float / shift gravity (R1) | Left Shift |
| Reset gravity (L1) | C |
| Jump / confirm (Cross) | Space / Enter |
| Kick (Square) | Left mouse button |
| Stasis / Circle | F; Backspace also works in menus |
| Special power (Triangle) | Q |
| Slide (L2 + R2) | Hold Left Ctrl |
| Evade (R2) | Left Alt, right-click or middle-click with a direction |
| Map / game menu | M or Esc |
| Contextual touchpad click | E or Tab |
| Port overlay | Insert |

Esc menus retain cursor confinement while focused; Alt+Tab releases the mouse.
See [PC_CONTROLS.md](app/PC_CONTROLS.md) for other bindings and test limits.

## Build from source

The repository excludes generated binaries and game data. A normal clone
includes vendored build dependencies; external reference checkouts are not
required. Recorded snapshots: [DEPENDENCIES.md](DEPENDENCIES.md).

The current Windows build uses **MSYS2 clang64**, normally at `C:\msys64`.
Set `MSYS2_ROOT` if installed elsewhere. Build prerequisites:

- Clang/Clang++, CMake, Ninja, pkg-config and a Windows resource compiler;
- Vulkan headers/loader, glslangValidator, fmt, Boost, tsl-robin-map,
  Vulkan Memory Allocator, xxHash, SDL3 and Zydis;
- FFmpeg development libraries: avformat, avcodec, avutil, swscale, swresample;
- FreeType and the vendored libraries/headers in `runtime`;
- Windows Python **3.11+ with Tkinter**, plus Git for package/source inventory.

The authoritative dependency checks are in
[`runtime/gpu/CMakeLists.txt`](runtime/gpu/CMakeLists.txt).
The [inherited Windows build guide](runtime/packaging/windows/README.md)
provides toolchain context; its SpartanPort game/launcher instructions are not
this project's player instructions.

From the repository root:

```powershell
./Setup.ps1 -Build              # Build/install runtime, helper and DLLs; prepare game
./Setup.ps1 -CheckOnly          # Validate files without running the game
./Build_Player.ps1              # Bundle launcher; produce dist/player_TIMESTAMP.zip
python -m unittest discover -s app/tests -v
python app/scripts/audit_publication.py  # Check Git-eligible source before publishing
```

The first player build installs pinned **PyInstaller 6.22.3** and build
dependencies into a private environment under ignored `app/out`. Players do
not install these tools. `app/scripts/build_launcher_start.ps1` builds a smaller
development bootstrap requiring installed Python; `Build_Player.ps1` replaces
it with the bundled player launcher. ZIPs stay local until explicitly published.

## Troubleshooting and reports

Include build/runtime version, title ID, Windows/GPU/driver details, episode
and the exact action before a problem. Review relevant `app/logs` files for
personal information before attaching them. Distinguish a waiting Continue
prompt from a stuck transition or terminated process. Do not attach game
executables, archives, PKGs or full dumps to an issue.

If an old taskbar pin still shows a blank/previous icon, close the old launcher,
open the current EXE and replace that old pin. The app explicitly supplies its
native icons, Windows taskbar identity and relaunch icon properties.

## Credits, citations and references

This work relies on substantial upstream contributions; it does not claim
independent authorship of their runtime, renderer or decoder.

| Project | Relationship |
|---|---|
| [FelipePrado0 / SpartanPort](https://github.com/FelipePrado0/SpartanPort) | Main runtime/loader/renderer integration foundation; modified source in `runtime`. |
| [shadPS4 team](https://github.com/shadps4-emu/shadPS4) | PS4 Vulkan video core, shader translation and API-name metadata; original notices preserved. |
| [LucaPazienza / gravityrush_pc](https://github.com/LucaPazienza/gravityrush_pc) | Rendering-scope guard reference; [pinned attribution](runtime/GRAVITY_RUNTIME_UPSTREAM.md). |
| [gminustwo / minustwo](https://github.com/gminustwo/minustwo) | NGS2 mixer/audio foundation; [pinned attribution](runtime/NGS2_UPSTREAM.md). |
| [LoreanXavier / P.T. for PC](https://github.com/LoreanXavier/pt-pc) | Architectural/porting reference consulted locally; its Hallway Engine is not claimed as this runtime's implementation. |
| [Supermedo / bloodborne_pc](https://github.com/Supermedo/bloodborne_pc), [deadinside28 / bloodborne_pc](https://github.com/deadinside28/bloodborne_pc), [cuesta4 / shadPS4](https://github.com/cuesta4/shadPS4/tree/eltutz) | Earlier foundations/rendering work credited by the inherited SpartanPort source. |

See **[REFERENCES.md](REFERENCES.md)** for revisions, dependency sources and
packaging/Windows API documentation. Upstream full-story claims apply to their
tested projects/regions, not automatically to CUSA01130 here.

## License and publication scope

Retain [LICENSE](LICENSE), upstream copyright notices and the licenses in
vendored directories. Dependencies do not all share one license; consult their
file notices and [references](REFERENCES.md). Player packages include source
and notices for the project, Python/PyInstaller and library dependencies.

`.gitignore` excludes the game, saves, caches, logs, generated boot images,
build outputs, DLLs/EXEs, player ZIPs and local reference checkouts. The sibling
`references` folder and separate PS2 workspace are outside this repository.
Publish as an **experimental preview**, not a verified start-to-ending release.
Setup does not create a GitHub remote, make commits or push files.
GitHub Actions/CI workflows are excluded; this project does not use them.
