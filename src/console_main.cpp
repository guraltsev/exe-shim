/* Provide the console-subsystem entrypoint for the shared shim workflow. */
#include "shim_main.h"

/** Forward the console process entrypoint to the shared launcher. */
int wmain() {
  return shim_main();
}
