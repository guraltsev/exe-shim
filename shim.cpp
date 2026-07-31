#include <Windows.h>
#include <shellapi.h>

#include <fmt/format.h>
#include <fmt/xchar.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class unique_handle {
 public:
  explicit unique_handle(HANDLE handle = nullptr) noexcept : handle_(handle) {}
  ~unique_handle() { reset(); }

  unique_handle(const unique_handle&) = delete;
  unique_handle& operator=(const unique_handle&) = delete;

  unique_handle(unique_handle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
  unique_handle& operator=(unique_handle&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  [[nodiscard]] HANDLE get() const noexcept { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
  void reset(HANDLE handle = nullptr) noexcept {
    if (handle_ != nullptr) {
      CloseHandle(handle_);
    }
    handle_ = handle;
  }

 private:
  HANDLE handle_;
};

struct shim_configuration {
  std::wstring target;
  std::wstring arguments;
};

using local_argument_list = std::unique_ptr<wchar_t*, decltype(&LocalFree)>;

[[nodiscard]] std::wstring format_win32_error(DWORD error) {
  return fmt::format(L"Windows error {}", error);
}

[[nodiscard]] std::optional<std::wstring> utf8_to_wide(const std::string& text) {
  if (text.empty()) {
    return std::wstring{};
  }
  if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }

  const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
      static_cast<int>(text.size()), nullptr, 0);
  if (required == 0) {
    return std::nullopt;
  }

  std::wstring result(static_cast<size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
          static_cast<int>(text.size()), result.data(), required) == 0) {
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] std::optional<shim_configuration> read_configuration(const std::filesystem::path& shim_path) {
  std::ifstream file(shim_path, std::ios::binary);
  if (!file) {
    std::wcerr << L"Cannot open shim file for read.\n";
    return std::nullopt;
  }

  const std::string contents{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
  const auto wide_contents = utf8_to_wide(contents);
  if (!wide_contents) {
    std::wcerr << L"Shim file must be valid UTF-8.\n";
    return std::nullopt;
  }

  shim_configuration configuration;
  std::wistringstream lines(*wide_contents);
  std::wstring owned_line;
  while (std::getline(lines, owned_line)) {
    std::wstring_view line = owned_line;
    if (!line.empty() && line.back() == L'\r') {
      line.remove_suffix(1);
    }

    constexpr std::wstring_view path_prefix = L"path = ";
    constexpr std::wstring_view args_prefix = L"args = ";
    if (line.starts_with(path_prefix)) {
      configuration.target = line.substr(path_prefix.size());
    } else if (line.starts_with(args_prefix)) {
      configuration.arguments = line.substr(args_prefix.size());
    }

  }

  if (configuration.target.empty()) {
    std::wcerr << L"Could not read shim file.\n";
    return std::nullopt;
  }
  return configuration;
}

[[nodiscard]] std::wstring quote_executable(std::wstring_view target) {
  std::wstring quoted{L"\""};
  size_t backslashes = 0;
  for (const wchar_t character : target) {
    if (character == L'\\') {
      ++backslashes;
    } else if (character == L'"') {
      quoted.append(backslashes + 1, L'\\');
      quoted += character;
      backslashes = 0;
    } else {
      quoted.append(backslashes, L'\\');
      quoted += character;
      backslashes = 0;
    }
  }
  quoted.append(backslashes * 2, L'\\');
  quoted += L'"';
  return quoted;
}

[[nodiscard]] std::optional<std::wstring> user_arguments() {
  int argument_count = 0;
  const local_argument_list arguments(
      CommandLineToArgvW(GetCommandLineW(), &argument_count), &LocalFree);
  if (!arguments || argument_count < 1) {
    return std::nullopt;
  }

  std::wstring result;
  for (int index = 1; index < argument_count; ++index) {
    if (!result.empty()) {
      result += L' ';
    }
    result += quote_executable(arguments.get()[index]);
  }
  return result;
}

BOOL WINAPI control_handler(DWORD) {
  return TRUE;
}

[[nodiscard]] int run() {
  int ignored_argument_count = 0;
  const local_argument_list parsed_command_line(
      CommandLineToArgvW(GetCommandLineW(), &ignored_argument_count), &LocalFree);
  if (!parsed_command_line || ignored_argument_count < 1) {
    std::wcerr << L"Could not determine the launcher path.\n";
    return 1;
  }
  std::filesystem::path shim_path = parsed_command_line.get()[0];
  shim_path.replace_extension(L".shim");

  const auto configuration = read_configuration(shim_path);
  if (!configuration) {
    return 1;
  }
  const auto forwarded_arguments = user_arguments();
  if (!forwarded_arguments) {
    std::wcerr << L"Could not parse command-line arguments.\n";
    return 1;
  }

  std::wstring command_line = quote_executable(configuration->target);
  for (const std::wstring_view part : {std::wstring_view{configuration->arguments},
           std::wstring_view{*forwarded_arguments}}) {
    if (!part.empty()) {
      command_line += L' ';
      command_line += part;
    }
  }
  std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
  mutable_command_line.push_back(L'\0');

  unique_handle job(CreateJobObjectW(nullptr, nullptr));
  if (!job) {
    std::wcerr << L"Could not create job object: " << format_win32_error(GetLastError()) << L".\n";
    return 1;
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
  if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
    std::wcerr << L"Could not configure job object: " << format_win32_error(GetLastError()) << L".\n";
    return 1;
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, mutable_command_line.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED,
          nullptr, nullptr, &startup, &process)) {
    const DWORD error = GetLastError();
    if (error != ERROR_ELEVATION_REQUIRED) {
      std::wcerr << L"Shim: Could not create process with command '" << command_line << L"'.\n";
      return 1;
    }

    const std::wstring elevated_arguments = configuration->arguments.empty()
        ? *forwarded_arguments
        : forwarded_arguments->empty() ? configuration->arguments
                                       : fmt::format(L"{} {}", configuration->arguments, *forwarded_arguments);
    SHELLEXECUTEINFOW execution{};
    execution.cbSize = sizeof(execution);
    execution.fMask = SEE_MASK_NOCLOSEPROCESS;
    execution.lpFile = configuration->target.c_str();
    execution.lpParameters = elevated_arguments.c_str();
    execution.nShow = SW_SHOW;
    if (!ShellExecuteExW(&execution)) {
      std::wcerr << L"Unable to create elevated process: " << format_win32_error(GetLastError()) << L".\n";
      return 1;
    }
    process.hProcess = execution.hProcess;
  } else {
    unique_handle process_handle(process.hProcess);
    unique_handle thread_handle(process.hThread);
    if (!AssignProcessToJobObject(job.get(), process_handle.get())) {
      const DWORD error = GetLastError();
      TerminateProcess(process_handle.get(), 1);
      std::wcerr << L"Could not assign child process to its job: " << format_win32_error(error) << L".\n";
      return 1;
    }
    if (ResumeThread(thread_handle.get()) == static_cast<DWORD>(-1)) {
      const DWORD error = GetLastError();
      TerminateProcess(process_handle.get(), 1);
      std::wcerr << L"Could not start child process: " << format_win32_error(error) << L".\n";
      return 1;
    }
    SetConsoleCtrlHandler(control_handler, TRUE);
    WaitForSingleObject(process_handle.get(), INFINITE);
    DWORD exit_code = 1;
    return GetExitCodeProcess(process_handle.get(), &exit_code) ? static_cast<int>(exit_code) : 1;
  }

  unique_handle process_handle(process.hProcess);
  SetConsoleCtrlHandler(control_handler, TRUE);
  WaitForSingleObject(process_handle.get(), INFINITE);
  DWORD exit_code = 1;
  return GetExitCodeProcess(process_handle.get(), &exit_code) ? static_cast<int>(exit_code) : 1;
}

}  // namespace

int wmain() {
  return run();
}
