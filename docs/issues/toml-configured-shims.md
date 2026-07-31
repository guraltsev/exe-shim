# Design: TOML-configured shims

## Summary

Make TOML the configuration format for `exe-shim`.  Every launcher remains a
copy of `shim.exe`; the TOML configuration beside it determines how it starts
its target.

For `C:\\Bin\\tool.exe`, the launcher reads `C:\\Bin\\tool.config.toml`.

## Configuration lookup

The launcher reads exactly one configuration file: `<name>.config.toml`.  A
missing or malformed file is an error and must name the expected configuration
path.

`<name>` is the launcher's file name without its `.exe` extension.  Thus
`C:\\Bin\\tool.exe` reads `C:\\Bin\\tool.config.toml`, independent of the
current working directory.

## TOML configuration

The required setting is `target`, the executable to start.  It is a string
and is expanded for Windows environment-variable references before the target
is resolved and launched.

```toml
# tool.config.toml
target = "%LOCALAPPDATA%\\Programs\\Example\\tool.exe"
forward_arguments = true

[[argument]]
value = "--config"

[[argument]]
value = "%APPDATA%\\Example\\tool.ini"
```

Use `%NAME%` syntax and the Windows `ExpandEnvironmentStringsW` API.  Expand
environment variables in `target`, configured argument strings, `working_dir`,
environment values, and `path_prepend` entries. An unset variable should
produce a clear error that
names the setting and variable; leaving `%NAME%` literal makes failures too
surprising.  Expansion happens once, after TOML parsing, and does not invoke a
shell, so values cannot cause command injection.

`target` may be absolute.  For a relative target, resolve it relative to the
directory containing the configuration file, not the caller's working
directory.  This keeps a shim relocatable when its directory is copied or
placed on `PATH`.

Each `[[argument]]` section adds one configured argument through its required
`value` string. Sections are applied in file order, so argument boundaries and
ordering are unambiguous. Configured arguments precede user-supplied
arguments.

`forward_arguments` is an optional Boolean that controls whether arguments
provided to the launcher are passed to the target. It defaults to `true` when
omitted. Set it to `false` for a fixed-purpose shim that always launches the
same target and configured arguments.

### Initial schema

```toml
# Required
target = "%USERPROFILE%\\bin\\real-tool.exe"

# Optional
forward_arguments = true # Defaults to true.
elevate = false # Defaults to false.
working_dir = "%USERPROFILE%\\projects\\demo"
remove_environment = ["PYTHONHOME", "VIRTUAL_ENV"]
path_prepend = ["%USERPROFILE%\\bin", "tools"]

[environment]
RUST_LOG = "info"
TOOL_CACHE = "%LOCALAPPDATA%\\tool-cache"

[[argument]]
value = "serve"

[[argument]]
value = "--color=always"
```

Unknown keys and missing or non-string `argument.value` fields should be errors
in TOML configurations. A strict schema catches typos before a potentially
surprising command is launched. Error messages should include the
configuration-file path, key, and TOML line when available.

The child environment starts with the launcher's inherited environment. Each
entry in `[environment]` then sets or overwrites that variable for the child
process. Environment-variable names are case-insensitive on Windows, so
`Path`, `PATH`, and `path` identify the same variable; the configured spelling
should be retained in the resulting environment block. These overrides affect
only the target process and its descendants, never the launcher's parent shell.

`remove_environment` is an optional array of environment-variable names to
remove from the child environment. Each name must be a non-empty string without
`=` or a NUL character. Removal is case-insensitive. A name may not appear in
both `remove_environment` and `[environment]`, including with different casing;
that ambiguity is a configuration error. Duplicate names in
`remove_environment`, also case-insensitively, are errors. This prevents a
configuration from depending on an implicit operation order.

Build the child environment by copying the inherited environment, removing the
requested names, then applying `[environment]` overrides. Expand configured
values using the inherited environment before these changes are applied. This
makes expansion deterministic and ensures an override cannot unexpectedly
change the meaning of a later value.

`path_prepend` is an optional array of directory strings to place at the front
of the target's `PATH`. Each entry is expanded for environment variables and a
relative entry is resolved from the configuration-file directory. Entries must
be non-empty after expansion and must not contain a NUL character or `;`.
Apply the entries in the listed order, followed by the current child `PATH`.
If `[environment]` sets `PATH`, that value is the current child `PATH`; if it
does not, the inherited value is used. If neither provides `PATH`, the result
contains only the configured prefix entries. `path_prepend` therefore affects
only the target process and its descendants.

`elevate` is an optional Boolean that defaults to `false`. When `true`, the
shim must request elevation with the Windows `runas` verb before starting the
target. If the user cancels the UAC prompt or Windows cannot elevate the target,
the shim must report the failure and return a non-zero exit code; it must not
run the target unelevated. When `false`, retain the current behavior of
requesting elevation only when Windows reports that the target requires it.

## Launch behavior

1. Determine the launcher path and locate its TOML configuration.
2. Parse and validate the selected configuration before creating a process.
3. Expand supported environment variables and resolve relative paths.
4. Construct an argument vector from configured `[[argument]]` entries. When
   `forward_arguments` is true, append the caller's arguments; otherwise omit
   them. Use the existing Windows quoting logic.
5. Start the target with the configured working directory and a child
   environment derived from the parent's environment, with
   `remove_environment` entries removed and `[environment]` entries
   overwriting inherited variables, then prepend `path_prepend` entries to
   `PATH`.
6. Use the configured elevation behavior, and preserve the current exit-code,
   Ctrl+C, and job-object behavior where Windows supports it for the selected
   launch mechanism.

The launcher should display the resolved target and relevant configuration key
when validation or process creation fails, but should not print environment
values: they may contain secrets.

## Useful future functionality

The first TOML release should stay small.  The following additions are useful,
but need explicit semantics and tests before adoption.

| Capability | Possible TOML form | Value and design constraints |
| --- | --- | --- |
| Per-shim working directory | `working_dir = "..."` | Useful for project tools; include in the initial schema. |
| Argument rewriting | `[arguments]` rules | Could provide defaults or translate user input, but quickly becomes a shell language; postpone until a concrete use case defines constrained, testable rules. |
| Multiple targets / subcommands | `[[command]]` | Could route `tool build` and `tool test`; prefer a small dispatcher program unless a concrete use case justifies its complexity. |
| Target search on `PATH` | `search_path = true` | Convenient but less deterministic and can change which executable runs; absolute or config-relative targets are safer defaults. |
| GUI window controls | `hide_window = true` | May improve UX, but needs clear interaction with console control events and elevated launches. |
| Diagnostics | `--shim-diagnose` | A built-in mode could show the selected config, resolved target, working directory, and argument boundaries while redacting environment values. |

Avoid features that execute through `cmd.exe`, PowerShell, or a general
pre-launch script.  The shim's core benefit is deterministic native process
launching; shell evaluation would introduce quoting, injection, and debugging
problems.

## Rollout

The implementation should replace the current configuration reader with TOML
parsing.  Documentation and tests should cover a missing configuration,
required `target`, malformed TOML, `%VARIABLE%` expansion and missing
variables, relative target resolution, argument-section ordering,
`forward_arguments` defaults and false behavior, working directories, and
child-environment overrides, removals, duplicate names, and set/remove
conflicts; `path_prepend` ordering, relative paths, and `PATH` overrides;
explicit elevation; and UAC cancellation.

TOML parsing should use a small, maintained parser library or a deliberately
limited parser that fully supports the documented schema; it should not be
implemented with line-prefix matching.  Any added dependency must fit the
project's Conan/CMake build.
