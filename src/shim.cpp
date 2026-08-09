#include <Windows.h>
#include <shellapi.h>

#include <fmt/format.h>
#include <fmt/xchar.h>
#include <toml++/toml.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
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
    if (this != &other) { reset(); handle_ = std::exchange(other.handle_, nullptr); }
    return *this;
  }
  [[nodiscard]] HANDLE get() const noexcept { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
  void reset(HANDLE handle = nullptr) noexcept { if (handle_ != nullptr) CloseHandle(handle_); handle_ = handle; }
 private:
  HANDLE handle_;
};

using local_argument_list = std::unique_ptr<wchar_t*, decltype(&LocalFree)>;

struct environment_entry { std::wstring name; std::wstring value; };
struct shim_configuration {
  std::wstring target;
  std::vector<std::wstring> arguments;
  bool forward_arguments = true;
  bool elevate = false;
  std::optional<std::wstring> working_directory;
  std::vector<environment_entry> environment;
  std::vector<std::wstring> remove_environment;
  std::vector<std::wstring> path_prepend;
};

[[nodiscard]] std::wstring format_win32_error(DWORD error) { return fmt::format(L"Windows error {}", error); }

[[nodiscard]] std::optional<std::wstring> utf8_to_wide(const std::string& text) {
  if (text.empty()) return std::wstring{};
  if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return std::nullopt;
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size == 0) return std::nullopt;
  std::wstring result(static_cast<size_t>(size), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size) == 0) return std::nullopt;
  return result;
}

[[nodiscard]] std::wstring folded(std::wstring_view value) {
  std::wstring result(value);
  std::ranges::transform(result, result.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return result;
}

[[nodiscard]] std::vector<environment_entry> inherited_environment() {
  std::vector<environment_entry> result;
  wchar_t* block = GetEnvironmentStringsW();
  if (block == nullptr) return result;
  for (const wchar_t* entry = block; *entry != L'\0'; entry += std::wcslen(entry) + 1) {
    const std::wstring_view text(entry);
    const size_t equal = text.find(L'=', text.starts_with(L'=') ? 1 : 0);
    if (equal != std::wstring_view::npos) result.push_back({std::wstring(text.substr(0, equal)), std::wstring(text.substr(equal + 1))});
  }
  FreeEnvironmentStringsW(block);
  return result;
}

[[nodiscard]] bool inherited_contains(const std::vector<environment_entry>& environment, std::wstring_view name) {
  const auto expected = folded(name);
  return std::ranges::any_of(environment, [&](const environment_entry& entry) { return folded(entry.name) == expected; });
}

[[nodiscard]] std::optional<std::wstring> expand_environment(std::wstring_view value, std::wstring_view key,
    const std::filesystem::path& config_path, const std::vector<environment_entry>& inherited) {
  // Detect undefined %NAME% references before asking Windows to expand them; the API otherwise leaves them literal.
  for (size_t begin = value.find(L'%'); begin != std::wstring_view::npos;) {
    const size_t end = value.find(L'%', begin + 1);
    if (end == std::wstring_view::npos) break;
    const std::wstring_view name = value.substr(begin + 1, end - begin - 1);
    if (!name.empty() && !inherited_contains(inherited, name)) {
      std::wcerr << config_path.wstring() << L": " << key << L" references unset environment variable %" << name << L"%.\n";
      return std::nullopt;
    }
    begin = value.find(L'%', end + 1);
  }
  const DWORD required = ExpandEnvironmentStringsW(std::wstring(value).c_str(), nullptr, 0);
  if (required == 0) { std::wcerr << config_path.wstring() << L": could not expand " << key << L".\n"; return std::nullopt; }
  std::vector<wchar_t> buffer(required);
  if (ExpandEnvironmentStringsW(std::wstring(value).c_str(), buffer.data(), required) == 0) {
    std::wcerr << config_path.wstring() << L": could not expand " << key << L".\n"; return std::nullopt;
  }
  return std::wstring(buffer.data());
}

[[nodiscard]] std::optional<std::wstring> toml_string(const toml::node& node, std::wstring_view key,
    const std::filesystem::path& config_path) {
  const auto value = node.value<std::string>();
  if (!value) { std::wcerr << config_path.wstring() << L": " << key << L" must be a string.\n"; return std::nullopt; }
  const auto wide = utf8_to_wide(*value);
  if (!wide) std::wcerr << config_path.wstring() << L": " << key << L" must be valid UTF-8.\n";
  return wide;
}

[[nodiscard]] bool valid_environment_name(std::wstring_view name) {
  return !name.empty() && name.find(L'=') == std::wstring_view::npos && name.find(L'\0') == std::wstring_view::npos;
}

[[nodiscard]] std::optional<shim_configuration> read_configuration(const std::filesystem::path& config_path) {
  if (!std::filesystem::exists(config_path)) { std::wcerr << L"Missing configuration file: " << config_path.wstring() << L".\n"; return std::nullopt; }
  toml::table table;
  try { table = toml::parse_file(config_path.string()); }
  catch (const toml::parse_error& error) {
    const auto description = utf8_to_wide(std::string(error.description())).value_or(L"parser error");
    std::wcerr << config_path.wstring() << L":" << error.source().begin.line << L": malformed TOML: " << description << L".\n";
    return std::nullopt;
  }

  constexpr std::string_view allowed[] = {"target", "forward_arguments", "elevate", "working_dir", "remove_environment", "path_prepend", "environment", "argument"};
  for (const auto& [key, node] : table) {
    if (std::ranges::find(allowed, key.str()) == std::end(allowed)) { std::wcerr << config_path.wstring() << L": unknown key " << utf8_to_wide(std::string(key.str())).value_or(L"?") << L".\n"; return std::nullopt; }
  }
  const toml::node* target_node = table.get("target");
  if (target_node == nullptr) { std::wcerr << config_path.wstring() << L": required key target is missing.\n"; return std::nullopt; }
  const auto inherited = inherited_environment();
  const auto target_text = toml_string(*target_node, L"target", config_path);
  if (!target_text) return std::nullopt;
  const auto expanded_target = expand_environment(*target_text, L"target", config_path, inherited);
  if (!expanded_target || expanded_target->empty()) { if (expanded_target) std::wcerr << config_path.wstring() << L": target must not be empty.\n"; return std::nullopt; }
  shim_configuration config;
  std::filesystem::path target(*expanded_target);
  config.target = (target.is_absolute() ? target : config_path.parent_path() / target).lexically_normal().wstring();

  for (const auto& [name, destination] : {std::pair{"forward_arguments", &config.forward_arguments}, {"elevate", &config.elevate}}) {
    if (const toml::node* node = table.get(name)) { const auto value = node->value<bool>(); if (!value) { std::wcerr << config_path.wstring() << L": " << utf8_to_wide(name).value_or(L"?") << L" must be a Boolean.\n"; return std::nullopt; } *destination = *value; }
  }
  if (const toml::node* node = table.get("working_dir")) {
    const auto value = toml_string(*node, L"working_dir", config_path); if (!value) return std::nullopt;
    const auto expanded = expand_environment(*value, L"working_dir", config_path, inherited); if (!expanded) return std::nullopt;
    std::filesystem::path directory(*expanded); config.working_directory = (directory.is_absolute() ? directory : config_path.parent_path() / directory).lexically_normal().wstring();
  }
  if (const toml::array* arguments = table["argument"].as_array()) for (const toml::node& item : *arguments) {
    const toml::table* argument = item.as_table(); const toml::node* value = argument == nullptr ? nullptr : argument->get("value");
    if (argument == nullptr || argument->size() != 1 || value == nullptr) { std::wcerr << config_path.wstring() << L": each argument must contain only string value.\n"; return std::nullopt; }
    const auto text = toml_string(*value, L"argument.value", config_path); if (!text) return std::nullopt;
    const auto expanded = expand_environment(*text, L"argument.value", config_path, inherited); if (!expanded) return std::nullopt;
    config.arguments.push_back(*expanded);
  } else if (table.contains("argument")) { std::wcerr << config_path.wstring() << L": argument must be an array of tables.\n"; return std::nullopt; }
  for (const auto& [name, destination] : {std::pair{"remove_environment", &config.remove_environment}, {"path_prepend", &config.path_prepend}}) if (const toml::node* node = table.get(name)) {
    const toml::array* values = node->as_array(); if (!values) { std::wcerr << config_path.wstring() << L": " << utf8_to_wide(name).value_or(L"?") << L" must be an array.\n"; return std::nullopt; }
    for (const toml::node& item : *values) { const auto text = toml_string(item, utf8_to_wide(name).value_or(L"?"), config_path); if (!text) return std::nullopt; destination->push_back(*text); }
  }
  std::vector<std::wstring> seen_removed;
  for (const auto& name : config.remove_environment) { if (!valid_environment_name(name) || std::ranges::find(seen_removed, folded(name)) != seen_removed.end()) { std::wcerr << config_path.wstring() << L": remove_environment contains an invalid or duplicate name.\n"; return std::nullopt; } seen_removed.push_back(folded(name)); }
  if (const toml::table* values = table["environment"].as_table()) for (const auto& [key, node] : *values) {
    const auto name = utf8_to_wide(std::string(key.str())); const auto text = toml_string(node, L"environment value", config_path);
    if (!name || !valid_environment_name(*name) || !text) { std::wcerr << config_path.wstring() << L": environment has an invalid name or value.\n"; return std::nullopt; }
    if (std::ranges::find(seen_removed, folded(*name)) != seen_removed.end()) { std::wcerr << config_path.wstring() << L": environment and remove_environment both name " << *name << L".\n"; return std::nullopt; }
    const auto expanded = expand_environment(*text, L"environment value", config_path, inherited); if (!expanded) return std::nullopt; config.environment.push_back({*name, *expanded});
  } else if (table.contains("environment")) { std::wcerr << config_path.wstring() << L": environment must be a table.\n"; return std::nullopt; }
  for (std::wstring& entry : config.path_prepend) { const auto expanded = expand_environment(entry, L"path_prepend", config_path, inherited); if (!expanded || expanded->empty() || expanded->find(L'\0') != std::wstring::npos || expanded->find(L';') != std::wstring::npos) { std::wcerr << config_path.wstring() << L": path_prepend has an invalid entry.\n"; return std::nullopt; } std::filesystem::path path(*expanded); entry = (path.is_absolute() ? path : config_path.parent_path() / path).lexically_normal().wstring(); }
  return config;
}

[[nodiscard]] std::wstring quote_argument(std::wstring_view value) {
  std::wstring quoted{L"\""}; size_t slashes = 0;
  for (const wchar_t character : value) { if (character == L'\\') ++slashes; else if (character == L'\"') { quoted.append(slashes + 1, L'\\'); quoted += character; slashes = 0; } else { quoted.append(slashes, L'\\'); quoted += character; slashes = 0; } }
  quoted.append(slashes * 2, L'\\'); return quoted + L'"';
}

[[nodiscard]] std::optional<std::vector<std::wstring>> user_arguments() {
  int count = 0; const local_argument_list values(CommandLineToArgvW(GetCommandLineW(), &count), &LocalFree);
  if (!values || count < 1) return std::nullopt;
  std::vector<std::wstring> result; for (int index = 1; index < count; ++index) result.emplace_back(values.get()[index]); return result;
}

[[nodiscard]] std::vector<wchar_t> child_environment(const shim_configuration& config) {
  auto values = inherited_environment();
  for (const auto& name : config.remove_environment) std::erase_if(values, [&](const environment_entry& entry) { return folded(entry.name) == folded(name); });
  for (const auto& replacement : config.environment) { std::erase_if(values, [&](const environment_entry& entry) { return folded(entry.name) == folded(replacement.name); }); values.push_back(replacement); }
  if (!config.path_prepend.empty()) { std::wstring prefix; for (const auto& entry : config.path_prepend) { if (!prefix.empty()) prefix += L';'; prefix += entry; } const auto path = std::ranges::find_if(values, [](const environment_entry& entry) { return folded(entry.name) == L"path"; }); if (path != values.end() && !path->value.empty()) prefix += L';' + path->value; if (path != values.end()) values.erase(path); values.push_back({L"PATH", std::move(prefix)}); }
  std::ranges::sort(values, [](const environment_entry& left, const environment_entry& right) { return folded(left.name) < folded(right.name); });
  std::vector<wchar_t> block; for (const auto& entry : values) { block.insert(block.end(), entry.name.begin(), entry.name.end()); block.push_back(L'='); block.insert(block.end(), entry.value.begin(), entry.value.end()); block.push_back(L'\0'); } block.push_back(L'\0'); return block;
}

BOOL WINAPI control_handler(DWORD) { return TRUE; }

[[nodiscard]] int run() {
  std::vector<wchar_t> module_path(260);
  DWORD length = 0; do { length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size())); module_path.resize(module_path.size() * 2); } while (length >= module_path.size() / 2 - 1);
  if (length == 0) { std::wcerr << L"Could not determine launcher path.\n"; return 1; }
  std::filesystem::path config_path(std::wstring(module_path.data(), length)); config_path.replace_extension(); config_path += L".config.toml";
  const auto config = read_configuration(config_path); if (!config) return 1;
  const auto forwarded = user_arguments(); if (!forwarded) { std::wcerr << L"Could not parse command-line arguments.\n"; return 1; }
  std::vector<std::wstring> arguments = config->arguments; if (config->forward_arguments) arguments.insert(arguments.end(), forwarded->begin(), forwarded->end());
  std::wstring command_line = quote_argument(config->target); for (const auto& argument : arguments) command_line += L' ' + quote_argument(argument);
  const std::wstring parameters = command_line.substr(quote_argument(config->target).size() + (arguments.empty() ? 0 : 1));
  if (config->elevate) { SHELLEXECUTEINFOW execution{}; execution.cbSize = sizeof(execution); execution.fMask = SEE_MASK_NOCLOSEPROCESS; execution.lpVerb = L"runas"; execution.lpFile = config->target.c_str(); execution.lpParameters = parameters.c_str(); execution.lpDirectory = config->working_directory ? config->working_directory->c_str() : nullptr; execution.nShow = SW_SHOW; if (!ShellExecuteExW(&execution)) { std::wcerr << L"Unable to create elevated process for " << config->target << L": " << format_win32_error(GetLastError()) << L".\n"; return 1; } unique_handle process(execution.hProcess); WaitForSingleObject(process.get(), INFINITE); DWORD code = 1; return GetExitCodeProcess(process.get(), &code) ? static_cast<int>(code) : 1; }
  auto environment = child_environment(*config); std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end()); mutable_command.push_back(L'\0');
  unique_handle job(CreateJobObjectW(nullptr, nullptr)); if (!job) { std::wcerr << L"Could not create job object: " << format_win32_error(GetLastError()) << L".\n"; return 1; }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK; if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) { std::wcerr << L"Could not configure job object.\n"; return 1; }
  STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
  if (!CreateProcessW(config->target.c_str(), mutable_command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, environment.data(), config->working_directory ? config->working_directory->c_str() : nullptr, &startup, &process)) { const DWORD error = GetLastError(); if (error == ERROR_ELEVATION_REQUIRED) { std::wcerr << L"Target requires elevation; set elevate = true in " << config_path.wstring() << L".\n"; } else std::wcerr << L"Could not create target " << config->target << L": " << format_win32_error(error) << L".\n"; return 1; }
  unique_handle process_handle(process.hProcess); unique_handle thread_handle(process.hThread); if (!AssignProcessToJobObject(job.get(), process_handle.get())) { const DWORD error = GetLastError(); TerminateProcess(process_handle.get(), 1); std::wcerr << L"Could not assign child process to its job: " << format_win32_error(error) << L".\n"; return 1; }
  if (ResumeThread(thread_handle.get()) == static_cast<DWORD>(-1)) { const DWORD error = GetLastError(); TerminateProcess(process_handle.get(), 1); std::wcerr << L"Could not start child process: " << format_win32_error(error) << L".\n"; return 1; }
  SetConsoleCtrlHandler(control_handler, TRUE); WaitForSingleObject(process_handle.get(), INFINITE); DWORD code = 1; return GetExitCodeProcess(process_handle.get(), &code) ? static_cast<int>(code) : 1;
}
}  // namespace
int shim_main() { return run(); }
