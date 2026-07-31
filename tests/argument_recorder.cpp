#include <cstdlib>
#include <cwchar>
#include <Windows.h>
#include <fstream>
#include <string>

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

  std::wstring report_path(output);
  report_path += L".context";
  std::wofstream report(report_path);
  std::wstring directory(32768, L'\0');
  const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(directory.size()), directory.data());
  report << L"cwd=" << directory.substr(0, length) << L'\n';
  for (const wchar_t* name : {L"SHIM_TEST_VALUE", L"REMOVE_ME", L"PATH"}) {
    const wchar_t* value = _wgetenv(name);
    report << name << L"=" << (value == nullptr ? L"<missing>" : value) << L'\n';
  }

  const wchar_t* exit_code = _wgetenv(L"SHIM_TEST_EXIT_CODE");
  return exit_code == nullptr ? 0 : std::wcstol(exit_code, nullptr, 10);
}
