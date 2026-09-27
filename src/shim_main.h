/* Run the shared TOML-configured shim launcher workflow.
 *
 * The console and GUI launchers call this entrypoint from their respective
 * Windows-subsystem entrypoints, keeping configuration, environment, and
 * process-launch behavior identical between the two binaries.
 */
#pragma once

/**
 * Run the common launcher implementation and return its process exit code.
 *
 * The implementation locates the launcher beside the current executable,
 * reads its sibling `.config.toml` file, and starts the configured target.
 * Diagnostics are written to standard error when configuration or process
 * creation fails.
 *
 * Returns
 * -------
 * `int`
 *     The target process exit code, or `1` when the launcher cannot start it.
 */
int shim_main();
