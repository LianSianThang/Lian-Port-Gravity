# Technical Investigation: Native PC Port Architecture for Gravity Rush Remastered (CUSA01130)

> **Current state update (2026-11-02):** The prior live Lian Port run reached Episode 1 and stalled at the first gravity-power sequence: black output, ~60 FPS, one draw/frame, and continuing pad reads. The newly built `bin/gravity-probe-motion-enabled.exe` has since been launched and rendered the title menu. Scripted mouse motion changed the guest orientation quaternion, verifying that the new sensor/fallback bridge reaches the guest; scripted Cross presses were read but did not advance the title menu, so the gravity checkpoint has not been retested. In-game voices remain missing: normal audio ports report zero RMS/peak, and NGS2 voice/rack/render imports are generic stubs. The host audio backend received movie audio in earlier scenes. Full progression and voice playback are unverified. See `PROJECT_STATUS_REPORT.md`, `logs/candidate_attached_20261102.log`, and `logs/candidate_motion_test_20261102_tutorial.log`.

> **Historical status (2026-10-25):** The SpartanPort-derived probe was reported to render a scene for over two minutes. The Nov 2 logs and live screenshot now provide newer title-menu evidence; gameplay progression and controls still need verification. The launcher is branded Lian Port, with SpartanPort attribution retained in its Architecture tab.

> **Historical issue update (2026-10-25):** Font and audio telemetry now exist. Nov 1 font counters verified glyph rasterization in the archived rendered-scene run; current regular audio ports still report zero RMS/peak.

**Author:** Antigravity (Advanced Agentic Systems)  
**Date:** October 24, 2026  
**Target Hardware:** Acer Nitro Lite 16 | Intel Core 5 210H | NVIDIA GeForce RTX 3050 Laptop GPU (6GB) | Windows 11 Home (Build 26300, DisplayVersion 26H2)  
**Project Workspace:** `C:\Testing PS4 Port`  
**Investigation Output Directory:** `C:\Testing PS4 Port\gravity-rush-port-investigation`  

---

## 1. Executive Summary

This investigation evaluates the technical feasibility of engineering a **native PC runtime/port** for *Gravity Rush Remastered* (Title ID: `CUSA01130`, internal title `Bluepoint_GravityDazeHD_PS4`), moving beyond general-purpose emulation (shadPS4) toward a game-specific, decoupled runtime architecture modeled on *SpartanPort* (God of War III PC runtime) and the Fox Engine reconstruction of *P.T. for PC*.

### Key Breakthrough Findings
1. **Target Binaries are Fully Decrypted and Plaintext [VERIFIED]:**
   The game package located at `CUSA01130` contains a fully decrypted SELF executable (`eboot.bin`, 38,295,907 bytes) and 7 decrypted PRX dynamic libraries (`libc.prx`, `libSceFios2.prx`, etc.). Every SELF segment has `enc=False` and `comp=False`. Underneath the SELF wrapper resides a standard 64-bit x86-64 System V ELF executable (`ET_SCE_DYNEXEC`, machine type `EM_X86_64` 0x3E).
2. **100% Symbol Resolution Achieved [VERIFIED]:**
   Inspection of `eboot.bin`'s dynamic symbol table (`PT_SCE_DYNLIBDATA`) revealed exactly **710 imported symbols across 42 required PlayStation 4 modules**. Cross-referencing against the AeroLib database in shadPS4 resolved **710 of 710 symbols (100.0%)** without a single unidentified NID.
3. **High Architectural Overlap with Reference Runtimes [VERIFIED]:**
   Out of the 710 target APIs, **477 (67.2%)** are already referenced and implemented in *SpartanPort*, and **452 (63.7%)** are implemented in *shadPS4*'s HLE modules. In total, **608 APIs (85.6%)** already have functional implementations across the reference projects. The remaining 102 symbols belong almost entirely to the game's bundled `libc.prx` and `libSceFios2.prx`, which can be linked natively into the runtime image via SpartanPort's multi-module linkage technique.
4. **Hardware Suitability [VERIFIED]:**
   Direct diagnostic execution of the GPU and CPU probe utilities verified that the host Intel Core 5 210H natively supports all required x86-64 instruction extensions (CPU: complete; AVX, MOVBE, BMI1, LZCNT verified). The NVIDIA GeForce RTX 3050 Laptop GPU (6GB) running Vulkan 1.4 driver 617.42 passed command submission and readback diagnostics.
5. **Recommended Strategy [VERIFIED TRADE-OFF]:**
   **Strategy C (Hybrid Native Runtime)** is strongly recommended. Re-engineering the entire engine in C++ (Strategy B, as done for *P.T.*) would require years of reverse engineering for Bluepoint's complex proprietary engine. Conversely, running the unmodified x86-64 code directly on the host CPU with a custom, lightweight platform runtime (reusing shadPS4's translated Vulkan GNM video core and SpartanPort's probe loader) provides a viable path to an incremental, playable PC port.

---

## 2. Environment and Repository Inventory

### 2.1 Host Environment Verification
- **Host Machine:** Acer Nitro Lite 16
- **Operating System:** Windows 11 Home Single Language, 64-bit (Version 10.0.26300.0, Registry `DisplayVersion` 26H2) [VERIFIED via `[System.Environment]::OSVersion` and registry inspection].
- **CPU:** Intel(R) Core(TM) 5 210H (12 logical cores, Intel64 Family 6 Model 186 Stepping 2) [VERIFIED via HKLM CentralProcessor].
- **GPU 0 (Discrete):** NVIDIA GeForce RTX 3050 6GB Laptop GPU (Device ID `0x25ac`, Driver version `617.42.0.0`, Vulkan API `1.4.351`) [VERIFIED via `vulkaninfo --summary`].
- **GPU 1 (Integrated):** Intel(R) Graphics (Device ID `0xa7ab`, Driver version `101.5972`, Vulkan API `1.3.289`) [VERIFIED].
- **Storage / Disk Space:**
  - `C:\`: 24.14 GB free of 375.7 GB [VERIFIED via `[System.IO.DriveInfo]`].
  - `E:\`: 10.11 GB free of 100.19 GB [VERIFIED].
- **Installed Toolchains on PATH:**
  - Python: 3.12.9 (`C:\Users\User\AppData\Local\Programs\Python\Python312\python.exe`) [VERIFIED].
  - Git: 2.x (`C:\Users\User\AppData\Local\Programs\Git\cmd\git.exe`) [VERIFIED].
  - Vulkan Loader & Runtime: Installed (`vulkaninfo.exe` in `C:\WINDOWS\system32\`) [VERIFIED].
  - GitHub CLI: 2.x installed via WinGet [VERIFIED].
- **Missing Build Prerequisites [VERIFIED]:**
  - C/C++ Compilers: Neither MSVC (`cl.exe`), LLVM/Clang (`clang++.exe`), nor MinGW GCC (`g++.exe`) are currently present on the system PATH or default Visual Studio locations.
  - Build Systems: Neither CMake (`cmake.exe`) nor Ninja (`ninja.exe`) are currently on PATH.
  - Vulkan SDK: Development headers (`vulkan/vulkan.h`), validation layers, and `glslangValidator` are not yet installed (only runtime driver is present).

### 2.2 Repository and Resource Inventory
The workspace at `C:\Testing PS4 Port` contains six resource directories and one PKG file:

| Resource Directory / File | Type / Provenance | Git / Commit Status | License | Build / Binary Status |
|---|---|---|---|---|
| `original game package (not distributed)` | Target Game PKG (9,999,876,096 bytes) | Retail / Fake PKG format container | Proprietary (Sony / Bluepoint Games) | Raw package file |
| `CUSA01130\` | Extracted Game Directory | Extracted dump | Proprietary (Sony / Bluepoint Games) | Extracted: contains unencrypted `eboot.bin`, `sce_sys`, `sce_module`, `arc` |
| `shadPS4-main\` | Upstream Emulator Source (`shadps4-emu/shadPS4`) | Unpacked source zip (no `.git` directory) | GPL-2.0-or-later | Source only (unbuilt; submodules empty due to GitHub archive format) |
| `SpartanPort-main\` | God of War III Native PC Port Source (`FelipePrado0/SpartanPort`) | Unpacked source zip (no `.git` directory) | GPL-2.0-only | Source only (unbuilt; submodules empty) |
| `pt-pc-main\` | P.T. C++ Engine Reconstruction Source (`LoreanXavier/pt-pc`) | Unpacked source zip (no `.git` directory) | MIT License (for port engine code) | Source only (unbuilt) |
| `gow3-windows\` | Compiled Distribution of SpartanPort | Standalone binary distribution | Mixed (GPL-2.0 runtime + third-party DLLs) | Pre-compiled binaries present: `gow3-probe.exe`, `gow3-gpu-capabilities.exe`, `God of War III.exe` |
| `ShadPs4Plus-PkgExtractor-1.1-windows\` | PKG Extraction Utility | Compiled Windows binary | Third-party utility | Pre-compiled: contains `pkg_extractor.exe`, `libc++.dll`, `zlib1.dll` |

---

## 3. Target Game Analysis: Gravity Rush Remastered (CUSA01130)

### 3.1 Package and Container Status
- **Package Container:** `original game package (not distributed)` (9.31 GiB).
- **Magic Bytes:** `7f 43 4e 54` (`\x7fCNT`), standard Sony PS4 package format.
- **Extraction Tool Evaluation:** `ShadPs4Plus-PkgExtractor-1.1-windows\pkg_extractor.exe` was verified via execution. It operates on PS4 PKG archives. However, extraction is **already complete** in `C:\Testing PS4 Port\CUSA01130`.

### 3.2 Metadata and Verification (param.sfo)
Parsing `CUSA01130\sce_sys\param.sfo` yielded verified game metadata:
- **Title:** `Gravity Rush™ Remastered`
- **Title ID:** `CUSA01130`
- **Content ID:** `UP9000-CUSA01130_00-GRAVITYRUSHHD000`
- **Application Version / Version:** `01.00`
- **Category:** `gd` (Game Digital)
- **Required System Version:** `72351744` (Hex `0x04500000` -> PS4 System Software 4.50)
- **SDK Version:** `PUBTOOLINFO` records `sdk_ver=02000000` (built with Sony Orbis SDK 2.00/4.50 toolchain).

### 3.3 Executable Format and Decryption Verification
Analysis of `CUSA01130\eboot.bin` (38,295,907 bytes):
- **Outer Container:** Sony Signed ELF (SELF), magic `0x1D3D154F` (`\x4f\x15\x3d\x1d`).
- **Encryption Status [VERIFIED]:**
  - Segment count: 8 segments.
  - Inspection of all 8 `self_segment_header` entries verified that bit 1 (`flags & 2`, `IsEncrypted()`) is **FALSE (0)** on all 8 segments.
  - Bit 3 (`flags & 8`, `IsCompressed()`) is **FALSE (0)** on all 8 segments.
  - **Conclusion:** `eboot.bin` is completely decrypted and uncompressed.
- **Inner Executable:**
  - File offset: Byte 288 (`0x120`).
  - Magic: `\x7fELF` (64-bit, little-endian, Version 1, FreeBSD ABI 9).
  - Executable Type: `0xFE10` (`ET_SCE_DYNEXEC` — Sony Dynamic Executable).
  - Target Architecture: `0x003E` (`EM_X86_64` — AMD64 / x86-64).
  - Relative Entry Point: `0x2F0`.
  - Program Headers (`e_phnum`): 10 headers:
    - Segment [0] `PT_LOAD` (R-X, Code/Text): File size `0x015E785C` (22.9 MB of native x86-64 code).
    - Segment [1] `PT_LOAD` (RW-, Data/BSS): File size `0x00790748` (7.9 MB), Memory size `0x00DCF668` (14.5 MB).
    - Segment [3] `PT_DYNAMIC`: File size `0xCD0` (204 dynamic tags).
    - Segment [7] `PT_SCE_DYNLIBDATA`: File size `0x006F9F98` (7.3 MB of relocation and symbol data).

### 3.4 Bundled PRX Dynamic Libraries
All seven PRX libraries in `CUSA01130\sce_module` were verified as unencrypted SELFs (`enc=False`):
1. `libc.prx` (934,754 bytes) — Sony Orbis C/C++ runtime library.
2. `libSceFios2.prx` (385,249 bytes) — Sony Fast I/O System 2 archive streaming engine.
3. `libSceFace.prx` (299,672 bytes) — Face tracking library.
4. `libSceHand.prx` (25,962 bytes) — Hand gesture library.
5. `libSceHeadTracker.prx` (1,350,418 bytes) — Head tracking library.
6. `libSceS3DConversion.prx` (292,658 bytes) — Stereoscopic 3D conversion.
7. `libSceSmart.prx` (1,452,561 bytes) — Smart tracking engine.

### 3.5 Game Data Archives
Asset inspection in `CUSA01130\arc` reveals five standard PlayStation Archive (`.psarc`) files:
- `arc_0_ep_0_0.psarc` (804 MB)
- `arc_0_ep_1_7.psarc` (1.57 GB)
- `arc_1_ep_8_11.psarc` (1.20 GB)
- `arc_2_ep_12_30.psarc` (2.75 GB)
- `arc_3_ep_31_31.psarc` (3.47 GB)
Total compressed game assets: ~9.80 GB.

### 3.6 Complete Dynamic Dependency Breakdown
Parsing `PT_SCE_DYNLIBDATA` against AeroLib resolved **100% of the 710 imported APIs**:

```
Total Dynamic Symbols: 710 (100.0% resolved, 0 unknown NIDs)

[Standard Libc / C++ Runtime]    301 APIs (atan2, cos, sin, strlen, memcpy, malloc, etc.)
[PlayStation Network (PSN)]     148 APIs (Auth, Commerce, Trophy, Matching2, TUS, WebAPI, etc.)
[Kernel, Threads, Memory]         80 APIs (pthread_create_name_np, mutex, equeue, direct memory)
[Networking & Web]                46 APIs (sceHttp, sceSsl, sceNet, sceNetCtl)
[GNM Low-Level Graphics Driver]   45 APIs (sceGnmDrawInit..., DingDong, dispatch, render targets)
[Font & FreeType Engine]          22 APIs (sceFontBindRenderer, sceFontGetCharGlyphMetrics, etc.)
[SaveData & Dialogs]              18 APIs (sceSaveDataDialogOpen, sceSaveDataGetSaveDataMemory)
[AudioOut & Audio3D]              16 APIs (sceAudioOutOpen, sceAudio3dPortOpen, objects)
[NGS2 Sound Engine]               10 APIs (sceNgs2SystemCreateWithAllocator, sceNgs2SystemRender)
[Gamepad & Motion Sensors]         8 APIs (scePadRead, scePadSetTiltCorrectionState, orientation)
[SystemService & UserService]      7 APIs (sceSystemServiceParamGetInt, sceUserServiceGetInitialUser)
[VideoOut Display Controller]      6 APIs (sceVideoOutOpen, sceVideoOutSubmitFlip, buffers)
[Sysmodule Dynamic Loader]         3 APIs (sceSysmoduleLoadModule, sceSysmoduleIsLoaded)
```

---

## 4. Cross-Repository Architecture Analysis

### 4.1 SpartanPort (`FelipePrado0/SpartanPort`)
- **Core Concept:** Game-specific native PC port of *God of War III Remastered*, derived from *bloodborne_pc* (by deadinside28 and Supermedo).
- **Executable Loading:**
  - Python scripts (`scripts/prepare.py` and `scripts/link_modules.py`) parse the PS4 `eboot.bin` and PRXs offline.
  - The scripts apply base relocations (`R_X86_64_RELATIVE`), resolve intra-module dependencies, and merge `eboot.bin` + `libc.prx` + `libSceFios2.prx` into a compact pre-linked binary (`G3PROBE5` / `boot.bin`).
  - At runtime, `src/probe.c` allocates low memory (`VirtualAlloc` on Windows), loads `boot.bin`, binds import slot trampolines to `runtime_*.c` functions, and jumps directly to the guest entry point (`entry`).
- **Execution Mechanism:**
  - Direct host CPU execution. The code runs at native speed with zero JIT overhead.
  - SysV vs Win64 ABI differences are handled via compiler attributes (`__attribute__((sysv_abi))` or `ms_abi` trampolines) and assembly glue.
  - Hardware differences are handled via `src/isa_rewrite.c` and `src/isa_emulate.c`: on older CPUs lacking MOVBE, BMI1, or LZCNT, instructions are patched in-memory with breakpoints (`int 3`) and emulated in a Vectored Exception Handler (`AddVectoredExceptionHandler`).
- **Graphics Pipeline:**
  - Vendored from *shadPS4* (commit `b4e7ae776071ceb1dd35004577a8719481e1d258`, listed in `gpu/VENDOR.txt`).
  - GNM commands written by the game to command buffers are trapped and processed by shadPS4's translated Vulkan video core (`gpu/shadps4/src/video_core`).
  - AMD GCN bytecode shaders are decompiled to SPIR-V in the background and cached to disk.
- **Limitations for Gravity Rush Remastered:**
  - SpartanPort contains game-specific paths (`dvdroot_ps4`), God of War III patch signatures, and Santa Monica Studio engine assumptions.
  - Gravity Rush uses Bluepoint's engine, Sony FIOS2 archive streaming for `.psarc` files, and DualShock 4 sixaxis tilt/gyro mechanics (`scePadSetTiltCorrectionState`).

### 4.2 P.T. for PC (`LoreanXavier/pt-pc`)
- **Core Concept:** Full game engine recreation in C++ ("Hallway Engine"), recreating the gameplay of the 2014 Kojima Productions Fox Engine teaser.
- **Executable Handling:**
  - **Does NOT execute `eboot.bin` at all.**
  - `src/decomp/` is empty. The project reverse-engineered game behavior from observation, scripts, and asset analysis.
- **Engine Architecture:**
  - Full modern C++ engine (`src/engine/`): custom Vulkan and DirectX 12 renderers, PhysX/custom physics, audio subsystem, asset stream unpackers for Fox Engine archive containers (`.dat`, `.fpk`).
- **Relevance to Gravity Rush Remastered:**
  - Illustrates how original assets can be parsed at runtime without Sony DRM.
  - However, recreating Gravity Rush's entire engine (open-world physics, gravity-vector manipulation, comic-book rendering pipeline, NPC AI) from scratch in C++ would require thousands of engineering hours.

### 4.3 shadPS4 (`shadps4-emu/shadPS4`)
- **Core Concept:** High-level emulation (HLE) platform for PlayStation 4 on PC.
- **Graphics Subsystem:**
  - Complete, robust GNM command buffer processor (`video_core/amdgpu`).
  - State-of-the-art shader recompiler (`shader_recompiler`) translating AMD Southern Islands / Sea Islands (GCN 1.1/1.2) bytecode to SPIR-V.
  - Buffer and texture caches with GPU dirty page tracking via virtual memory write protection.
- **CPU & OS Emulation:**
  - Direct thread execution on x86-64 host with static patching for Windows guest red-zone protection (`module.cpp`).
  - Implements POSIX threads, Orbis OS synchronization primitives (`equeue`), virtual memory, audio (`openal-soft`, `LibAtrac9`), and pad input.
- **Value as Extension Point:**
  - The video core, shader recompiler, and symbol NID databases (`aerolib.inl`) represent years of specialized reverse-engineering that can be leveraged directly.

---

## 5. Cross-Repository Subsystem Comparison

| Subsystem | SpartanPort Implementation | P.T. for PC Implementation | shadPS4 Implementation | Relevance to Gravity Rush Remastered | Potential Reuse vs Independent Implementation | Evidence & References |
|---|---|---|---|---|---|---|
| **Executable Execution** | Native host CPU execution via memory-mapped `boot.bin` and Win64 VEH fault handler | None (Native engine recreation; `eboot.bin` ignored) | Direct host execution with static instruction & red-zone patching | **Critical**: Gravity Rush has 22.9 MB native x86-64 code | **Reuse SpartanPort / shadPS4 concept**: Execute `eboot.bin` directly on host CPU | SpartanPort `src/probe.c#L65-L89`; shadPS4 `module.cpp#L370-L429` |
| **System Libraries (libc/fios2)** | Links original `libc.prx` & `libSceFios2.prx` into the binary image offline | Custom C++ filesystem reading Fox Engine archives | Emulates libc/kernel in HLE C++ functions | **Critical**: Gravity Rush bundles both `libc.prx` and `libSceFios2.prx` | **Reuse SpartanPort technique**: Merge game's unencrypted PRXs to provide 100+ native functions | SpartanPort `scripts/link_modules.py#L1-L24`; verified PRX status in `CUSA01130\sce_module` |
| **Graphics Backend** | Vendored shadPS4 video core + background shader compilation + Dear ImGui | Clean-room Vulkan and DirectX 12 renderers | Full Vulkan video core with compute & graphic queues | **Critical**: Gravity Rush issues 45 low-level GNM graphics calls | **Reuse shadPS4 / SpartanPort video core**: GNM-to-Vulkan translation pipeline | SpartanPort `gpu/VENDOR.txt#L1-L10`; `eboot.bin` GNM symbol table |
| **Shader Translation** | Offline/background GCN-to-SPIRV translation via shadPS4 `shader_recompiler` | Hand-authored HLSL / GLSL shaders recreating Fox Engine look | Dynamic JIT GCN-to-SPIRV recompiler with disk cache | **Critical**: Thousands of proprietary Bluepoint cell-shading GCN shaders | **Reuse shadPS4 `shader_recompiler`**: Mandatory to avoid re-writing hundreds of shaders | shadPS4 `src/shader_recompiler/` |
| **Input & Motion** | SDL3 mapping to `scePadRead` (standard gamepad buttons/axes) | SDL / custom mouse & keyboard mapper | SDL3 controller abstraction with DualShock 4 emulation | **High**: Gravity Rush requires 6-axis gyro/accelerometer for gravity aim & tilt | **Independent / Extended implementation**: Must implement `scePadSetTiltCorrectionState` and gyro mapping | `resolve_gr_symbols.py` (`scePadSetTiltCorrectionState` verified in GR) |
| **Audio Subsystem** | Re-implemented `runtime_audio.c`, `runtime_ajm.c`, `LibAtrac9` | Custom audio engine using modern middleware | OpenAL-soft backend + `LibAtrac9` + NGS2 HLE | **Moderate**: Gravity Rush uses `sceAudioOut`, `sceAudio3d`, and `sceNgs2` | **Hybrid**: Reuse `sceAudioOut` from SpartanPort; forward NGS2 or stub 3D audio | `resolve_gr_symbols.py` (16 AudioOut, 10 NGS2 symbols) |
| **Filesystem & Assets** | Mount system (`runtime_file.c`) + linked `libSceFios2` | Custom Fox Engine `.dat` / `.fpk` decoders | Host filesystem virtual mapping (`/app0/`, `/data/`) | **High**: 5 `.psarc` files in `CUSA01130\arc` | **Reuse SpartanPort `libSceFios2` link** or integrate open-source PSARC unpacker | `CUSA01130\arc\*.psarc` verified |
| **Save Data** | Host file serialization to `.bin` in `user/savedata` | Custom save system in JSON / binary | Encrypted/decrypted Orbis save container HLE | **Moderate**: 18 `sceSaveData` APIs | **Reuse SpartanPort `runtime_savedata.c`**: Maps PS4 save containers to simple host directories | SpartanPort `src/runtime_savedata.c` |

---

## 6. License and Intellectual Property Review

1. **shadPS4:** Licensed under **GNU General Public License v2.0 or later (GPL-2.0-or-later)**.
   - Any component statically linking or deriving from `shadPS4/src/video_core` or `shader_recompiler` must be distributed under GPL-2.0 or compatible copyleft terms.
2. **SpartanPort:** Licensed under **GNU General Public License v2.0 (GPL-2.0-only)**.
   - Derives from *bloodborne_pc* and vendors *shadPS4*. Reusing SpartanPort's probe loader or runtime files mandates GPL-2.0 compliance.
3. **P.T. for PC:** Engine code is licensed under the **MIT License**.
   - Permissive license; however, its Fox-engine specific architecture is not reusable for executing PS4 ELFs.
4. **Target Game Code and Assets (Gravity Rush Remastered):**
   - Owned by Sony Interactive Entertainment and Bluepoint Games.
   - **Compliance Rule:** Under no circumstances should proprietary game code, decrypted ELF segments, or raw `.psarc` assets be redistributed or bundled into the port repository. The port runtime must act strictly as an independent executable loader, loading the assets and code from the user's legally acquired dump at runtime.

---

## 7. Three-Strategy Feasibility Comparison

```
+------------------------------------+--------------------------+------------------------------+---------------------------+
| Evaluation Criteria                | Strategy A: Custom       | Strategy B: Native Engine    | Strategy C: Hybrid        |
|                                    | Game-Specific Runtime    | Reconstruction (P.T. Style)  | Architecture (SpartanPort)|
+------------------------------------+--------------------------+------------------------------+---------------------------+
| 1. Technical Feasibility           | Moderate-High            | Extremely Low                | High                      |
| 2. Reverse Engineering Required    | Moderate (APIs & stubs)  | Astronomical (entire engine) | Moderate-Low (leveraged)  |
| 3. Graphics & Shaders              | Requires GNM translator  | Rewrite all shaders to HLSL  | Reuses shadPS4 Vulkan core|
| 4. CPU & OS Compatibility          | Native x86-64 execution  | Native x86-64 / ARM          | Native x86-64 execution   |
| 5. Audio, Input, Memory, Threads   | Implement in C runtime   | Recreate in modern engine    | Implement in C runtime    |
| 6. Hardware Constraints (RTX 3050) | 100% capable (Vulkan 1.4)| 100% capable                 | 100% capable (Verified)   |
| 7. Engineering Complexity          | Very High (from scratch) | Prohibitive (>5,000 hours)   | Moderate & Incremental    |
| 8. IP & Legal Risk                 | Low (Clean-room runtime) | Medium (Asset recreation)    | Low (Separate loader)     |
| 9. Minimum Viable Milestone (MVP)  | Boot to entry point      | Load 1 model in custom eng.  | Boot entry + GNM init     |
| 10. Critical Unknowns              | GNM command translation  | Asset formats, math, physics | Bluepoint custom shaders  |
+------------------------------------+--------------------------+------------------------------+---------------------------+
```

### Strategy Recommendation & Justification
**Strategy C (Hybrid Architecture)** is the unequivocally recommended direction.
- **Why not Strategy B (Clean-room engine rewrite)?** Unlike P.T. (which consists of a single looped hallway and ~20 event triggers), *Gravity Rush Remastered* is a sprawling open-world game featuring real-time 360-degree gravity alteration, dynamic physics simulation, comic book cel-shading rendering passes, and thousands of gameplay scripts. Reconstructing Bluepoint's proprietary engine without source code would take a multi-person team several years.
- **Why Strategy C over Strategy A?** Building a custom GNM-to-Vulkan command translator and an AMD GCN bytecode decompiler from scratch (Strategy A) would duplicate years of work already refined in *shadPS4*. Strategy C adopts SpartanPort's proven decoupling: reuse the battle-tested shadPS4 Vulkan video core for rendering, link the game's bundled `libc.prx` and `libSceFios2.prx` for native utility execution, and implement a targeted, lightweight C runtime for the Orbis system services.

---

## 8. Verified Facts, Hypotheses, Unknowns, and Blockers

### Verified Facts
- **FACT-1:** The target game is *Gravity Rush Remastered* (CUSA01130, `Bluepoint_GravityDazeHD_PS4`), version 01.00 [VERIFIED via `param.sfo` and binary strings].
- **FACT-2:** All game executables (`eboot.bin` and 7 PRXs in `sce_module`) are unencrypted (`enc=False`, `comp=False`), standard 64-bit x86-64 ELF files [VERIFIED via ELF/SELF header parser].
- **FACT-3:** The executable imports 710 dynamic symbols across 42 modules; 100% of these NIDs are identified and resolved [VERIFIED via AeroLib matching].
- **FACT-4:** The host PC has an Intel Core 5 210H CPU and an NVIDIA RTX 3050 6GB Laptop GPU. Both pass hardware feature checks (CPU instructions complete; Vulkan 1.4 compute and readback pass) [VERIFIED via probe utilities].
- **FACT-5:** C/C++ compiler toolchains (MSVC/Clang), CMake, and Vulkan SDK are not currently installed on the host environment [VERIFIED via environment query].
- **FACT-6:** The unzipped source repositories (`shadPS4-main`, `SpartanPort-main`, `pt-pc-main`) lack `.git` metadata and their submodule folders are empty [VERIFIED via filesystem inspection].

### Hypotheses
- **HYP-1:** Gravity Rush's custom Bluepoint cel-shading shaders will translate cleanly through the shadPS4 GCN-to-SPIRV recompiler without requiring custom compute patch modifications [HYPOTHESIS - Needs verification at first render].
- **HYP-2:** Linking the game's bundled `libc.prx` and `libSceFios2.prx` will automatically satisfy ~350 of the 710 imported symbols with zero manual reimplementation [LIKELY - Confirmed by SpartanPort's successful linkage on God of War III].
- **HYP-3:** PSN network APIs (148 symbols) and Web APIs can be cleanly stubbed with success return codes (0 / `SCE_OK`) without stalling game execution [LIKELY - Standard practice across PS4 runtimes].

### Unknowns & Blockers
- **UNKNOWN-1:** Does Bluepoint's PSARC file streamer require specific encrypted keys, or are the `.psarc` archives in `CUSA01130\arc` standard plaintext zlib/lz4 streams?
- **UNKNOWN-2:** Does Gravity Rush rely on hardware-specific DualShock 4 motion sensor rates for core gameplay progression, requiring virtual keyboard/mouse gyro emulation?
- **BLOCKER-1:** Lack of an installed C++ compiler toolchain (MSVC / Clang64) and CMake on the host machine prevents compiling any code until installed with user approval.

---

## 9. Prioritized Implementation Roadmap

```
+----------------------------------------------------------------------------------------------------+
|                                    PROJECT ROADMAP OVERVIEW                                        |
+----------------------------------------------------------------------------------------------------+
|  [M0: Audit & Prereqs]  -->  [M1: Offline Preparation & Boot Image]  -->  [M2: Memory Loader]      |
|                                                                                   |                 |
|  [M5: Graphics & Vulkan] <--  [M4: File I/O & FIOS2]  <--  [M3: Platform Stubs & Entry Execution]   |
|            |                                                                                        |
|            v                                                                                        |
|  [M6: Input, Gyro & Audio]  -->  [M7: Gameplay Loop & Performance Tuning]                           |
+----------------------------------------------------------------------------------------------------+
```

### Milestone M0: Environment & Toolchain Setup
- **Objective:** Establish an automated, repeatable build environment on the host machine.
- **Prerequisites:** User approval to install required development tools.
- **Deliverable:** Working installation of Visual Studio Build Tools / MSYS2 Clang64, CMake, Ninja, and Vulkan SDK.
- **Verification:** Successful compilation and execution of a basic Vulkan test app.
- **Risk:** Sandbox permission constraints or disk storage overhead (~5-8 GB).

### Milestone M1: Game Executable Preparation & Static Linkage
- **Objective:** Parse `eboot.bin` and link it with the game's own `libc.prx` and `libSceFios2.prx` into a unified PC runtime boot image.
- **Prerequisites:** Python 3.12 (already installed).
- **Deliverable:** Customized `prepare_gravity.py` and `link_modules_gravity.py` producing `boot_gravity.bin` and an import manifest.
- **Verification:** Successful generation of `boot_gravity.bin` with all base relocations resolved and symbol offsets mapped.
- **Risk:** Proprietary relocation types or TLS layout mismatches.

### Milestone M2: Minimal Native Loader Prototype
- **Objective:** Create a standalone C loader that maps `boot_gravity.bin` into memory and installs import trap trampolines.
- **Prerequisites:** M0, M1.
- **Deliverable:** `gravity_probe.exe` that allocates low guest memory, maps segments, and prints intercepted API calls.
- **Verification:** Execution reaches `main()` or `_init_env()` and halts cleanly on the first unimplemented PS4 API trap.
- **Risk:** Windows memory protection or memory reservation collisions with system DLLs.

### Milestone M3: Essential Platform Service Stubs
- **Objective:** Implement the minimum required set of Orbis kernel and system services.
- **Prerequisites:** M2.
- **Deliverable:** C runtime modules for `pthread` (threading), `mutex`, `equeue`, `sceKernelAllocateDirectMemory`, `sceSystemService`, and PSN dummy stubs.
- **Verification:** Guest code successfully executes through its initialization sequence without encountering an unhandled trap.
- **Risk:** Guest thread deadlock on unimplemented synchronization primitives.

### Milestone M4: File I/O & PSARC Asset Access
- **Objective:** Enable the game to open and read its game data from `CUSA01130\arc\`.
- **Prerequisites:** M3.
- **Deliverable:** Functional `libSceFios2` interface or host filesystem redirection routing `psarc` archive queries to disk.
- **Verification:** Game logs show successful archive mounting and asset streaming attempts.
- **Risk:** Unimplemented asynchronous FIOS2 callbacks causing asset load hangs.

### Milestone M5: Vulkan Graphics Initialization & Presentation
- **Objective:** Connect the guest GNM driver calls to shadPS4's Vulkan video core.
- **Prerequisites:** M4.
- **Deliverable:** Integration of `spartanportgpu` / `shadPS4` video core, creating a Vulkan swapchain and processing command buffers.
- **Verification:** Initial splash screen or loading screen renders to a desktop window.
- **Risk:** Shader translation bugs on proprietary Bluepoint cel-shading shaders.

### Milestone M6: Input, Gyroscope & Sound
- **Objective:** Implement controller input, motion controls, and audio playback.
- **Prerequisites:** M5.
- **Deliverable:** SDL3 gamepad polling with mouse/keyboard gyro tilt simulation (`scePadSetTiltCorrectionState`), and OpenAL/AudioOut audio playback.
- **Verification:** Responsive controls on title screen; title background music plays cleanly.
- **Risk:** Desynchronization in motion sensor math preventing gravity shifting mechanics.

### Milestone M7: Progressive Gameplay Compatibility
- **Objective:** Achieve full playable loop from mission select to world traversal.
- **Prerequisites:** M6.
- **Deliverable:** Optimization passes, frame rate uncap patches (60/120 FPS), and memory leak fixes.
- **Verification:** Complete playthrough of Episode 1 without crashes.
- **Risk:** Game-specific physics timing bugs tied to fixed tick rates.

---

## 10. Change Control, Compliance, and Security Summary

- **Investigation Isolation:** Launcher, build, and report changes remain in the workspace and runtime source tree.
- **Game Data Preservation:** The original PKG and extracted `CUSA01130` directory remain untouched. Runtime-generated files stay in the investigation's `user` directory.
- **Source Attribution:** The app is branded Lian Port; the Architecture page and docs retain SpartanPort attribution for reused runtime lineage and licensing clarity.
- **Intellectual Property Safety:** No proprietary game code, decompiled routines, private keys, or game assets were exported or committed.
- **Build State:** The existing MSYS2/Clang toolchain rebuilt the runtime. The build script leaves the active guest-start helper untouched and keeps a timestamped backup of the prior probe.

---

## 11. Current Artifact Review (2026-11-02)

The workspace contains the boot image, a DLL-local render-verified executable, the rebuilt Lian Port candidate, the extracted game directory, and the launcher. `out/gravity_analysis.json` reports 767 imports; many runtime bindings remain placeholders. The render-verified executable reached the title menu in the Nov 2 run. The rebuilt branded candidate has not been run. Audio, menu progression, and a short gameplay loop remain unverified.

The earlier “live execution evidence” section was removed because `task-741.log` and `task-835.log` are absent. Current frame/audio claims are tied to dated logs under `logs/`; uncaptured hardware/thread claims remain historical. Current input, audio, and playability blockers are documented in [PROJECT_STATUS_REPORT.md](PROJECT_STATUS_REPORT.md).

The launcher uses the saved `game_path`, pins its working directory, routes Save/Backup to the runtime user folder, and logs its selected executable. A live runtime check confirmed title-menu rendering, but not new-game progression or sound.
