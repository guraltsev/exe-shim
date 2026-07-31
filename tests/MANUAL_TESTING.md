# Manual testing TOML-configured launchers

Build the project, then copy `shim-console.exe` into a disposable directory as
`tool.exe`. Copy `fixtures/arguments.toml` beside it as `tool.config.toml` and
replace `{{target}}` with a real executable such as
`C:\\Windows\\System32\\cmd.exe`.

From a different current directory, run `tool.exe /c echo launcher-ok`. Confirm
that configured arguments precede user arguments. Repeat with a relative
target after moving the directory, and verify it resolves from the
configuration directory.

Use `environment.toml` with real defined variables to confirm child-only
overrides and removals. Rename the configuration, introduce invalid TOML, and
use `%EXE_SHIM_MISSING_VARIABLE%` in `target`; each must fail before launching
the target and print the expected configuration path. Finally set
`elevate = true`, cancel the UAC prompt, and confirm a non-zero result without
an unelevated launch.

To verify subsystem behavior, make a second launcher by copying
`shim-gui.exe`, configure it with a harmless GUI program such as Notepad, and
open it from Explorer. It should launch the target without opening a console
window. Use `shim-console.exe` for console targets so Ctrl+C and inherited
terminal behavior remain available.
