# References and attribution

Links identify upstream work and documentation, not endorsement or proof of
this project's compatibility. Preserve the original licenses and file headers.

## Direct foundations and adapted code

| Source | Use / revision evidence |
|---|---|
| [SpartanPort](https://github.com/FelipePrado0/SpartanPort) | Runtime/loader/renderer foundation inherited in `runtime`. Original snapshot commit was not retained; do not describe it as the current upstream version. See its retained [README/credits](runtime/README.md). |
| [shadPS4](https://github.com/shadps4-emu/shadPS4) | Video core, shader compiler and symbol names. Adapted renderer in `runtime/gpu/shadps4`; [license](runtime/gpu/shadps4/LICENSE). `app/data/aerolib.inl` preserves its copyright and GPL notice. |
| [gravityrush_pc](https://github.com/LucaPazienza/gravityrush_pc/tree/9108d33eeeb25910ce484a64df535028fe8824e0) | Rendering-scope guard reference, revision `9108d33eeeb25910ce484a64df535028fe8824e0`; [adaptation notes](runtime/GRAVITY_RUNTIME_UPSTREAM.md). Its CUSA01113 completion claim does not verify this CUSA01130 project. |
| [minustwo](https://github.com/gminustwo/minustwo/tree/6d6ebce1346fa5f2f532a0ff3cd68321c13bbc0c) | NGS2 mixer/audio source, revision `6d6ebce1346fa5f2f532a0ff3cd68321c13bbc0c`; [adaptation notes](runtime/NGS2_UPSTREAM.md). |
| [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9) | ATRAC9 decoder; [license](runtime/third_party/LibAtrac9/LICENSE). |

The inherited SpartanPort source also credits
[deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc),
[Supermedo/bloodborne_pc](https://github.com/Supermedo/bloodborne_pc),
[cuesta4's eltutz branch](https://github.com/cuesta4/shadPS4/tree/eltutz) and
additional contributors/patch authors in its [credits](runtime/README.md#credits).
Those notices remain part of the source, including inherited game-specific code
that is not a claim of a Gravity Rush feature.

## Architectural references

[LoreanXavier / P.T. for PC](https://github.com/LoreanXavier/pt-pc) was consulted
as a PC port/launcher reference. Its separate Hallway Engine is not claimed as
an implementation in this repository. The local standalone shadPS4 checkout,
packaged God of War III reference and extraction utility remain outside this
repository in the sibling `references` folder. They are not shipped as player
dependencies. The separate PS2 workspace is unrelated to this repository.

## Vendored and linked components

Exact recorded revisions for six vendored repositories are listed in
[DEPENDENCIES.md](DEPENDENCIES.md). These are normal source directories,
not incomplete Git submodules.

| Component | Source / local notice |
|---|---|
| FSR-Vulkan / AMD FidelityFX | [FireBurn/FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan), [GPUOpen FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK); [local license](runtime/gpu/third_party/fsr-vulkan/LICENSE.txt) and nested notices |
| Dear ImGui | [ocornut/imgui](https://github.com/ocornut/imgui); [license](runtime/gpu/third_party/imgui/LICENSE.txt) |
| Sirit / SPIR-V | [shadps4-emu/sirit](https://github.com/shadps4-emu/sirit), [Khronos SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers); retained licenses in `runtime/gpu/third_party/sirit` |
| magic_enum | [Neargye/magic_enum](https://github.com/Neargye/magic_enum); [license](runtime/gpu/third_party/magic_enum/LICENSE) |
| miniz | [richgel999/miniz](https://github.com/richgel999/miniz); [license](runtime/gpu/third_party/miniz/LICENSE) |
| Xbyak | [herumi/xbyak](https://github.com/herumi/xbyak); retained notices in its source tree |
| Vulkan / Vulkan Memory Allocator | [Khronos Vulkan](https://www.vulkan.org/), [GPUOpen VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) |
| SDL3 | [SDL](https://www.libsdl.org/), [SDL3 API documentation](https://wiki.libsdl.org/SDL3/FrontPage) |
| FFmpeg | [FFmpeg](https://ffmpeg.org/); linked system/toolchain libraries for audio/video |
| FreeType | [FreeType](https://freetype.org/) |
| Boost / fmt / xxHash / Zydis / robin-map | [Boost](https://www.boost.org/), [fmt](https://github.com/fmtlib/fmt), [xxHash](https://github.com/Cyan4973/xxHash), [Zydis](https://github.com/zyantific/zydis), [robin-map](https://github.com/Tessil/robin-map) |
| DejaVu fonts / half / GCN headers | Retained source and notices under `runtime/gpu/third_party`; no upstream revision is invented for unversioned snapshots |

The complete build dependency checks are in
[`runtime/gpu/CMakeLists.txt`](runtime/gpu/CMakeLists.txt). Distributed native
DLLs are resolved from the selected runtime's imports; player ZIPs include
the dependency license notices supplied with the build toolchain. Retain them.

## Launcher packaging and Windows integration

- [Python](https://www.python.org/) and [Tkinter](https://docs.python.org/3/library/tkinter.html).
  Player packages bundle Python/Tcl/Tk; development uses Python 3.11+.
- [PyInstaller](https://pyinstaller.org/) **6.22.3**, pinned by the build script;
  [runtime path documentation](https://pyinstaller.org/en/stable/runtime-information.html).
  Preserve its distributed license/bootloader exception and Python's notices.
- [SpartanPort Windows packaging source](https://github.com/FelipePrado0/SpartanPort/blob/main/packaging/windows/package.sh),
  used as a distribution-workflow reference, not as evidence of local testing.
- Microsoft: [AppUserModelIDs](https://learn.microsoft.com/en-us/windows/win32/shell/appids),
  [window property store](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shgetpropertystoreforwindow),
  [relaunch icon resource](https://learn.microsoft.com/en-us/windows/win32/properties/props-system-appusermodel-relaunchiconresource).

The blue Lian Port artwork is supplied in `app/branding`. Game artwork shown
in the launcher is loaded from the player's own game, not a bundled game dump.
Gravity Rush and its original game content belong to their respective owners.
This project is not affiliated with or endorsed by Sony or the upstream teams.
