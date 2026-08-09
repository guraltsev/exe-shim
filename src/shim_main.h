/* Run the shared TOML-configured shim launcher workflow.
 *
 * The console and GUI launchers call this entrypoint from their respective
 * Windows-subsystem entrypoints, keeping configuration and process-launch
 * behavior identical between the two binaries.
 */
#pragma once

/** Run the common launcher implementation and return its process exit code. */
int shim_main();
