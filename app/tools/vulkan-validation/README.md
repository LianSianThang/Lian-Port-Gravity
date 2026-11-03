# Project-local Vulkan validation

This optional layer captures Vulkan API errors when reproducing the recorded
`nvoglv64.dll+0xe1a0a0` crashes. It does not fix those crashes. Nothing is
installed system-wide and the normal launcher remains unchanged.

From the investigation directory:

```powershell
.\Launch_Vulkan_Diagnostic.ps1 -CheckOnly
.\Launch_Vulkan_Diagnostic.ps1
```

The second command opens a separate launcher without starting a game. Use it
only after finishing the current playtest. Its normal timestamped run log
captures validation messages. Add `-Sync` to enable synchronization validation;
expect slower gameplay. Environment changes apply only to that launcher and
its children, and are restored in the calling PowerShell process.

Source: MSYS2 clang64 vulkan-validation-layers 1.4.363.0-1, downloaded from
https://repo.msys2.org/mingw/clang64/mingw-w64-clang-x86_64-vulkan-validation-layers-1.4.363.0-1-any.pkg.tar.zst

Verified archive SHA256:
`d1a8baa9b2309edf9fd11dff8cf3bbbfc7104f9093894ef2f2221445960a7e20`.
The extracted layer DLL/manifest and license are under `clang64`.

SPIRV-Tools dependencies copied from the existing `C:\msys64\clang64` installation:

| DLL | SHA256 |
|---|---|
| libSPIRV-Tools.dll | 8C32C1384CB8F5043753F66DF59BDD0C30D3FBDE26055EFCD67CF20985E4F688 |
| libSPIRV-Tools-opt.dll | 550262333173F2DDCEB81922768D519D86C733D83FC53D592AFB0F06DB988C98 |

Their license is under `clang64/share/licenses/spirv-tools`. The existing
runtime `bin/libc++.dll` supplies the C++ runtime dependency.
