/* Record the child process context so integration tests can inspect launch behavior. */
#include <cstdlib>
#include <cwchar>
#include <Windows.h>
#include <fstream>
#include <string>

int wmain(int argc, wchar_t** argv) {
  // The shim supplies this path; a missing variable means the test was invoked incorrectly.
  const wchar_t* output = _wgetenv(L"SHIM_TEST_OUTPUT");
  if (output == nullptr) {
    return 90;
  }

  std::wofstream file(output);
  if (!file) {
    return 91;
  }
  // Keep argv[0] in the file so tests can verify the target path independently
  // from the user arguments that follow it.
  for (int index = 0; index < argc; ++index) {
    file << argv[index] << L'\n';
  }

  // Record the working directory and selected environment entries separately
  // from argv to make failures identify which launch setting was wrong.
  std::wstring report_path(output);
  report_path += L".context";
  std::wofstream report(report_path.c_str());
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
