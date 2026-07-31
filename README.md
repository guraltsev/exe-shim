# exe-shim

`exe-shim` is a small native Windows executable launcher configured by TOML.
Copy either `shim-console.exe` or `shim-gui.exe` to the name of a command,
place a matching `.config.toml` file beside it, and run the command as usual.

For example, `gs.exe` reads `gs.shim`. The launcher starts the target named in
that file, adds any configured arguments, forwards the arguments supplied by
the user, and exits with the target process's exit code.

## How it works

When `C:\Bin\gs.exe` runs, it reads exactly `C:\Bin\gs.config.toml`. The
configuration contains a target executable and optional fixed arguments:

```toml
target = "C:\\Program Files\\Git\\git.exe"

[[argument]]
value = "status"

[[argument]]
value = "-uno"
```

With that setup, running:

```powershell
gs -s
```

launches the equivalent of:

```powershell
& 'C:\Program Files\Git\git.exe' status -uno -s
```

Paths containing spaces are supported. The launcher quotes an unquoted target
path when necessary.

## Build

The project is modern C++23, built with CMake. Dependencies are managed by
[Conan 2](https://docs.conan.io/2/), including the battle-tested `fmt`
formatting library. From an MSYS2 UCRT64 shell or a Visual Studio developer
prompt, install dependencies and build:

```bat
conan profile detect --force
conan install . --output-folder=build --build=missing
cmake --preset conan-release
cmake --build --preset conan-release
```

The resulting `shim-console.exe` and `shim-gui.exe` launchers are in the
`build` directory. Choose the console binary for command-line tools and the
GUI binary for applications launched from Explorer or shortcuts.

## Test

Run the integration suite after building:

```bat
ctest --preset conan-release --output-on-failure
```

The test driver uses Python's standard library; CMake builds its disposable
argument-recording test target with the same toolchain as the launcher.

## Create a command shim

1. Copy `shim-console.exe` for terminal tools, or `shim-gui.exe` for a GUI
   tool that must not open a console window, to the command name you want to
   expose. For example:

   ```bat
   copy shim-console.exe C:\Bin\gs.exe
   ```

2. Create a file with the same base name and the `.config.toml` extension:

   ```toml
   target = "C:\\Program Files\\Git\\git.exe"

   [[argument]]
   value = "status"

   [[argument]]
   value = "-uno"
   ```

3. Ensure the directory containing `gs.exe` is on `PATH`, then run `gs` from a
   command prompt or PowerShell.

## Configuration format

`target` is required; relative targets resolve from the configuration file.
Configured `[[argument]]` values precede caller arguments, and
`forward_arguments` defaults to `true`. The configuration also supports
`working_dir`, `elevate`, `[environment]`, `remove_environment`, and
`path_prepend`. String values expand Windows `%NAME%` references. Unknown
keys, malformed TOML, unset variables, and ambiguous environment edits fail
before the target is started.

```toml
target = "%LOCALAPPDATA%\\Programs\\Example\\tool.exe"
working_dir = "project"
remove_environment = ["VIRTUAL_ENV"]
path_prepend = ["tools"]

[environment]
RUST_LOG = "info"
```

## Scoop shims

The format is compatible with Scoop shim files. To replace the executable
launchers in the default Scoop locations after building `shim.exe`, run:

```bat
repshims.bat
```

The script copies `shim-console.exe` in its current directory over every `.exe` in
`%USERPROFILE%\scoop\shims` and `%ProgramData%\scoop\shims`. It does not
convert legacy Scoop `.shim` files; create matching `.config.toml` files before
using those launchers. Set `SCOOP` and/or `SCOOP_GLOBAL` before running it if
your Scoop directories are elsewhere.

## Process and console behavior

The launcher passes console control events, including Ctrl+C, through so the
target can handle them. It also assigns the started process to a Windows job
object, so child processes are terminated when the launcher is terminated.

If the target requires elevation, Windows starts it through the shell. In that
case it may open in a separate window.

## Safety and reliability

- The launcher uses C++23 dynamic strings and containers rather than fixed
  buffers or hand-calculated buffer lengths. Line parsing is stream based, and
  the only mutable Win32 command line is an owned, explicitly NUL-terminated
  vector.
- Windows handles have RAII ownership, so every acquired process, thread, and
  job handle is closed on every return path.
- Command-line parsing uses Windows' `CommandLineToArgvW`, and forwarded
  arguments are escaped using the documented Windows quoting rules instead of
  custom pointer or index parsing.

## Errors

- The launcher reports the expected `.config.toml` path if it cannot be opened
  or parsed, and names invalid configuration keys.
- TOML files must be valid UTF-8.

## License

SPDX-License-Identifier: MIT OR Unlicense
