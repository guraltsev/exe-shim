#include <cstdlib>
#include <cwchar>
#include <fstream>

int wmain(int argc, wchar_t** argv) {
  const wchar_t* output = _wgetenv(L"SHIM_TEST_OUTPUT");
  if (output == nullptr) {
    return 90;
  }

  std::wofstream file(output);
  if (!file) {
    return 91;
  }
  for (int index = 0; index < argc; ++index) {
    file << argv[index] << L'\n';
  }

  const wchar_t* exit_code = _wgetenv(L"SHIM_TEST_EXIT_CODE");
  return exit_code == nullptr ? 0 : std::wcstol(exit_code, nullptr, 10);
}
