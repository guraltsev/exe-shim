# exe-shim

`exe-shim` is a small native Windows executable launcher for
[Scoop](https://scoop.sh)-style shims. Copy the compiled `shim.exe` to the
name of a command, place a matching `.shim` file beside it, and run the
command as usual.

For example, `gs.exe` reads `gs.shim`. The launcher starts the target named in
that file, adds any configured arguments, forwards the arguments supplied by
the user, and exits with the target process's exit code.

## How it works

When `C:\Bin\gs.exe` runs, it looks for `C:\Bin\gs.shim`. A shim file
contains a target executable and, optionally, arguments that should always be
provided to it:

```ini
path = C:\Program Files\Git\git.exe
args = status -uno
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

The resulting launcher is in the `build` directory.

## Test

Run the integration suite after building:

```bat
ctest --preset conan-release --output-on-failure
```

The test driver uses Python's standard library; CMake builds its disposable
argument-recording test target with the same toolchain as the launcher.

## Create a command shim

1. Copy `shim.exe` to the command name you want to expose. For example:

   ```bat
   copy shim.exe C:\Bin\gs.exe
   ```

2. Create a file with the same base name and the `.shim` extension:

   ```ini
   path = C:\Program Files\Git\git.exe
   args = status -uno
   ```

3. Ensure the directory containing `gs.exe` is on `PATH`, then run `gs` from a
   command prompt or PowerShell.

## Shim-file format

Each setting occupies its own line in `key = value` form:

```ini
path = C:\path\to\program.exe
args = optional fixed arguments
```

`path` is required. `args` is optional. Unrecognized lines are ignored. Use
the spelling and spaces shown above (`path = ` and `args = `), and save the
file as UTF-8. End each setting line with a newline.

The configured arguments are placed before arguments supplied on the command
line. For instance, `args = --color=always` and `tool --help` result in the
target receiving `--color=always --help`.

## Scoop shims

The format is compatible with Scoop shim files. To replace the executable
launchers in the default Scoop locations after building `shim.exe`, run:

```bat
repshims.bat
```

The script copies the `shim.exe` in its current directory over every `.exe` in
`%USERPROFILE%\scoop\shims` and `%ProgramData%\scoop\shims`. It does not
change the `.shim` files. Set `SCOOP` and/or `SCOOP_GLOBAL` before running it
if your Scoop directories are elsewhere.

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

- The launcher reports an error if its matching `.shim` file cannot be opened,
  if `path` is missing, or if Windows cannot start the target.
- Shim files must be valid UTF-8.

## License

SPDX-License-Identifier: MIT OR Unlicense
