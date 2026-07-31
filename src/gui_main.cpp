#include <Windows.h>

#include "shim_main.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  return shim_main();
}
