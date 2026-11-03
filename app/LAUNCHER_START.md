# Open Lian Port

Open `C:\Testing PS4 Port\Lian Port.lnk`, or double-click
`C:\Testing PS4 Port\Lian-Port-Gravity\Launch_Lian_Port.exe`.
The player launcher bundles Python and Tkinter, uses the blue Lian Port icon
and opens without a console. Keep `_internal` and `app` beside it.
Close an older launcher window before restarting.

The compatibility batch file forwards to the executable and exits. Opening a
batch file can briefly show a command window; use the shortcut or executable
for console-free startup. Opening the Python source directly uses the Windows
Python file association instead of the branded entry point.

The launcher sets its own Windows taskbar identity and ICO image. Player ZIPs
do not require an installed Python. No system file association is modified.

Source and local data live in `app` beside the executable. Game data goes in
`Lian-Port-Gravity/game/CUSA01130`;
reference checkouts stay in the external `references` folder.

Run `Build_Player.ps1` from the repository to build/install the bundled launcher
and create a player ZIP. The private build environment uses PyInstaller 6.22.3.
The older `app/scripts/build_launcher_start.ps1` produces a development-only
bootstrap in the same repository root; that version requires installed Python.
