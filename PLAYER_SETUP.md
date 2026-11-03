# Player setup (experimental)

Full-story compatibility is still being tested. Use a controller for the
original prompts; keyboard/mouse controls are documented in `app/PC_CONTROLS.md`.

## Player ZIP

1. Extract the entire player ZIP and open its `Lian-Port-Gravity` folder.
   `Launch_Lian_Port.exe`, `_internal` and `app` must remain together.
2. Use Windows with a working Vulkan graphics driver. **Python and Tkinter
   are bundled; players do not need to install Python.**
3. Put your extracted, decrypted **CUSA01130** game in
   `Lian-Port-Gravity/game/CUSA01130`. The folder must contain `eboot.bin`,
   `sce_sys/param.sfo`, `sce_module/libc.prx`, `sce_module/libSceFios2.prx`
   and the `arc` archives. A PKG file is not accepted.
4. Open `Launch_Lian_Port.exe` inside that folder, choose the game if needed,
   and press Launch.
   First launch prepares a local boot image automatically. Changing the
   executable/modules triggers preparation again. The game dump is not modified.

No saves or game files ship with the ZIP. A fresh player uses `app/user`;
existing local profiles are never copied into a package. The package includes
runtime source, upstream notices and dependency license notices.

## Git clone

The source repository intentionally omits runtime binaries and game data.
Source development requires Python 3.11+ with Tkinter.

- **Build locally:** with the documented MSYS2 clang64 dependencies installed,
  run `./Setup.ps1 -Build`. This builds the runtime, startup helper, DLL staging
  and the development launcher, then prepares your extracted game. Run
  `./Build_Player.ps1` to bundle Python/Tkinter for redistribution.
- **Use a player package's runtime:** run `./Setup.ps1 -RuntimeDirectory
  'PATH/TO/PLAYER-PACKAGE/Lian-Port-Gravity/app/bin'`. Copy that package's
  `Launch_Lian_Port.exe` and `_internal` into your clone's root as well. Bundle files are checked
  against their included SHA256 manifest before copying.
- **Check only:** `./Setup.ps1 -CheckOnly` validates the game and runtime files.
  It does not test Vulkan rendering, gameplay or the ending.

Run these PowerShell commands from the repository. `-GamePath` can select an
extracted game elsewhere. The launcher has its own Choose Game Folder button.
Player startup does not download dependencies or change system configuration.

## Make a player ZIP locally

Run `./Build_Player.ps1`. It builds the frozen launcher and packages the currently selected
runtime and resolves its DLL dependency closure. Output goes into ignored
`dist/player_TIMESTAMP.zip`; it is not uploaded or published. The package
includes repository source but excludes game files, prepared images, saves,
caches, logs, settings and the external reference checkouts.
For packaging only, after freezing, run `python app/scripts/package_player.py`.
