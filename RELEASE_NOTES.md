# v0.1.0-preview.1 — Experimental Windows preview

Lian Port brings the current Gravity Rush Remastered CUSA01130 runtime work
into a standalone Windows player package, based on SpartanPort, shadPS4 and
the additional upstream work listed in [REFERENCES.md](REFERENCES.md).

**This is an experimental preview, not a verified start-to-ending release.**
Early gameplay, voices/music and an Episode 3 completed checkpoint resume have
been demonstrated. Later episodes, bosses, the ending and long-term stability
remain unverified. Testers are welcome; no reliable crash percentage is known.

## Download and run

Download `Lian-Port-Gravity-v0.1.0-preview.1-windows-x64.zip`, extract the whole
archive, open `Lian-Port-Gravity` and run `Launch_Lian_Port.exe`.
Keep `_internal` and `app` beside it. Python/Tkinter, the native runtime and
its DLLs are bundled; players do not need a compiler or installed Python.

Supply your own **extracted, decrypted CUSA01130** game folder. Put it in
`game/CUSA01130` or use Choose Game Folder. Press Launch Game to prepare and
start it. A retail/encrypted PKG is not accepted. No game files, generated
boot image, save data or shader cache are included.

Windows x64, the loader's required CPU features (SSE4.2/POPCNT/AVX) and a
working Vulkan 1.3 graphics driver are required. Minimum RAM/VRAM has not been
established. Read the [README](README.md) for full requirements and controls.

## Included work

- Lian Port branding, native taskbar icons and explicit Windows taskbar identity.
- Automatic extracted-game validation and boot preparation on first launch.
- Window mode, VSync and presentation FPS controls; cursor confinement in menus.
- PC input mapping and a controller-to-PC controls table.
- Native NGS2/audio work, video-frame selection and rendering-scope fixes.
- Source, upstream notices and dependency license notices.

Some launcher graphics controls remain prototypes; contextual controls,
audiovisual timing and later progression need broader testing. See
[CHECKPOINT_TESTING.md](CHECKPOINT_TESTING.md) and the current status report.

`SHA256SUMS.txt` accompanies the ZIP for download integrity checking. The
GitHub Source code archive is for developers and does not include player
binaries. GitHub Actions/CI is disabled; this preview was built/tested locally.
