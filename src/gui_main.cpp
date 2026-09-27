/* Provide the GUI-subsystem entrypoint without duplicating launcher logic. */
#include <Windows.h>

#include "shim_main.h"

/** Forward the GUI process entrypoint to the shared launcher. */
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  return shim_main();
}
