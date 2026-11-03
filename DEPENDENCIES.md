# Vendored dependency snapshots

These build dependencies are ordinary source directories in this repository, not Git submodules. Their original Git metadata was archived outside this repository; source files and local changes were preserved. A normal clone should include them without a recursive clone.

| Directory | Upstream | Base revision |
|---|---|---|
| `runtime/gpu/third_party/fsr-vulkan` | [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) | [`c64f09340412`](https://github.com/FireBurn/FSR-Vulkan/commit/c64f093404125e960813f95f1996c14780f0dd69) |
| `runtime/gpu/third_party/imgui` | [imgui](https://github.com/ocornut/imgui) | [`4e99c0ab1a28`](https://github.com/ocornut/imgui/commit/4e99c0ab1a28c2b3d1f250ec0266dbf1e455ef58) |
| `runtime/gpu/third_party/magic_enum` | [magic_enum](https://github.com/Neargye/magic_enum) | [`5de969c90059`](https://github.com/Neargye/magic_enum/commit/5de969c9005938a27a34d3af3ea5923d18679ad8) |
| `runtime/gpu/third_party/miniz` | [miniz](https://github.com/richgel999/miniz) | [`2948eebafc5b`](https://github.com/richgel999/miniz/commit/2948eebafc5b9f1c69bebed80333e403a09bd62d) |
| `runtime/gpu/third_party/xbyak` | [xbyak](https://github.com/herumi/xbyak) | [`df112862583c`](https://github.com/herumi/xbyak/commit/df112862583c2d327c1d9782fd28f53aa675f11d) |
| `runtime/third_party/LibAtrac9` | [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9) | [`efca2e3af355`](https://github.com/Thealexbarney/LibAtrac9/commit/efca2e3af35562a09a9bb6deed90e45b4b824dc4) |

The FSR-Vulkan tree includes inherited local modifications to the FSR backend, frame interpolation, upscaling, optical flow and message source files. The other five trees were clean against their recorded revisions when vendored. Original license and copyright notices remain in each directory.

Other dependencies (including Sirit/SPIR-V headers, AMD GCN headers, half and DejaVu fonts) were already supplied as source snapshots; do not infer a specific upstream revision where no original metadata was retained. See `REFERENCES.md`, `runtime/gpu/CMakeLists.txt` and the licenses beside those files.
