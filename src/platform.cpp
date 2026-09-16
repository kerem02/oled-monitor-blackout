#include "platform.hpp"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace oled {
std::string windowsErrorText(const char* operation, DWORD code) {
    wchar_t buffer[1024]{};
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, buffer, static_cast<DWORD>(std::size(buffer)), nullptr);
    std::wstring detail(buffer, count);
    while (!detail.empty() && (detail.back() == L'\r' || detail.back() == L'\n' || detail.back() == L' ')) detail.pop_back();
    return std::string(operation) + (detail.empty() ? "" : ": " + utf8(detail)) +
        " (Windows error " + std::to_string(code) + ")";
}
void showError(HWND owner, const char* text, const wchar_t* title) noexcept {
    try { MessageBoxW(owner, wide(text).c_str(), title, MB_OK | MB_ICONERROR); }
    catch (...) {
        OutputDebugStringA(text);
        MessageBoxW(owner, L"An error occurred. See the application log for details.", title, MB_OK | MB_ICONERROR);
    }
}
void winError(const char* operation, DWORD code) { throw std::runtime_error(windowsErrorText(operation, code)); }
bool windowsOrdinalEqual(const std::wstring& a, const std::wstring& b) {
    const int result = CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE);
    if (!result) winError("Compare device identity");
    return result == CSTR_EQUAL;
}
bool devicePathEqual(const std::string& a, const std::string& b) {
    return windowsOrdinalEqual(wide(a), wide(b));
}
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!n) winError("Decode UTF-8");
    std::wstring out(n, 0);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), n)) winError("Decode UTF-8");
    return out;
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!n) winError("Encode UTF-8");
    std::string out(n, 0);
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr)) winError("Encode UTF-8");
    return out;
}
std::filesystem::path dataDirectory() {
    PWSTR raw{};
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw);
    if (FAILED(hr)) winError("Find LocalAppData", static_cast<DWORD>(hr));
    // Free the COM allocation even if path construction throws.
    struct Free { PWSTR p; ~Free() { CoTaskMemFree(p); } } guard{raw};
    auto dir = std::filesystem::path(raw) / L"OLED Blackout";
    std::filesystem::create_directories(dir);
    return dir;
}
void Log::write(const std::string& message) noexcept {
    try {
        const auto path = dir_ / L"app.log";
        if (std::filesystem::exists(path) && std::filesystem::file_size(path) >= 256 * 1024) {
            // Two bounded files; failure is observable in debugger, never recursive logging.
            if (!MoveFileExW(path.c_str(), (dir_ / L"app.previous.log").c_str(), MOVEFILE_REPLACE_EXISTING)) {
                OutputDebugStringW(L"OLED Blackout: log rotation failed; skipping write to keep size bounded\n");
                return;
            }
        }
        SYSTEMTIME t{}; GetLocalTime(&t);
        std::ofstream out(path, std::ios::app | std::ios::binary);
        out << t.wYear << '-' << std::setfill('0') << std::setw(2) << t.wMonth << '-' << std::setw(2) << t.wDay
            << ' ' << std::setw(2) << t.wHour << ':' << std::setw(2) << t.wMinute << ':' << std::setw(2) << t.wSecond
            << ' ' << message << '\n';
        if (!out) OutputDebugStringW(L"OLED Blackout: log write failed\n");
    } catch (...) { OutputDebugStringW(L"OLED Blackout: logging unavailable\n"); }
}
ParsedSettings SettingsStore::load() {
    const auto path = dir_ / L"settings.ini";
    if (!std::filesystem::exists(path)) return {};
    ParsedSettings result;
    if (std::filesystem::file_size(path) > 16384) result.status = ConfigStatus::Corrupt;
    else {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot read settings.ini");
        std::string bytes((std::istreambuf_iterator<char>(in)), {});
        if (in.bad()) throw std::runtime_error("Settings read failed");
        result = parseSettings(bytes);
        try { (void)wide(result.settings.monitorPath); }
        catch (const std::exception&) { result = {Settings{}, ConfigStatus::Corrupt}; }
    }
    if (result.status == ConfigStatus::Corrupt || result.status == ConfigStatus::Unsupported) {
        if (!CopyFileW(path.c_str(), (dir_ / L"settings.recovered.ini").c_str(), FALSE)) {
            const DWORD error = GetLastError();
            result.backupFailed = true;
            log_.write(windowsErrorText("Settings backup failed; continuing with safe defaults and no monitor selected", error));
        } else log_.write("Invalid/unsupported configuration backed up; safe defaults, no monitor selected");
    } else if (result.status == ConfigStatus::Sanitized) log_.write("Invalid delay reset to 30 seconds");
    return result;
}
void SettingsStore::save(const Settings& settings) {
    auto bytes = serializeSettings(settings);
    const auto temp = dir_ / L"settings.tmp";
    const auto target = dir_ / L"settings.ini";
    {
        Handle file(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.get() == INVALID_HANDLE_VALUE) winError("Create settings temporary file");
        DWORD written{};
        if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size())
            winError("Write settings");
        if (!FlushFileBuffers(file.get())) winError("Flush settings");
    }
    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        winError("Atomically replace settings");
}
namespace {
struct MonitorContext { std::vector<Display> displays; std::exception_ptr error; };
BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) noexcept {
    auto& context = *reinterpret_cast<MonitorContext*>(param);
    try {
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) winError("GetMonitorInfo");
        Display d; d.handle = monitor; d.device = info.szDevice;
        d.bounds = {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom};
        d.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        d.name = L"Unknown display";
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(d.device.c_str(), ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
            d.hz = mode.dmDisplayFrequency;
        context.displays.push_back(std::move(d));
        return TRUE;
    } catch (...) { context.error = std::current_exception(); return FALSE; }
}

}
std::vector<Display> enumerateDisplays() {
    MonitorContext context;
    const BOOL enumerated = EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&context));
    if (context.error) std::rethrow_exception(context.error);
    if (!enumerated) winError("EnumDisplayMonitors");
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    bool queried = false;
    for (unsigned attempt = 0; attempt < 5; ++attempt) {
        UINT32 pc{}, mc{};
        LONG result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pc, &mc);
        if (result != ERROR_SUCCESS) winError("DisplayConfig sizes", result);
        paths.resize(pc); modes.resize(mc);
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pc, paths.data(), &mc, modes.data(), nullptr);
        if (result == ERROR_INSUFFICIENT_BUFFER) continue;
        if (result != ERROR_SUCCESS) winError("QueryDisplayConfig", result);
        paths.resize(pc); queried = true; break;
    }
    if (!queried) throw std::runtime_error("Display topology did not settle");
    for (auto& display : context.displays) {
        unsigned matches = 0;
        for (const auto& path : paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id};
            LONG error = DisplayConfigGetDeviceInfo(&source.header);
            // Incomplete mapping could hide a clone; reject the entire snapshot.
            if (error != ERROR_SUCCESS) winError("DisplayConfig source name", error);
            if (_wcsicmp(source.viewGdiDeviceName, display.device.c_str()) != 0) continue;
            ++matches;
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), path.targetInfo.adapterId, path.targetInfo.id};
            if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) {
                display.path = canonicalPath(utf8(target.monitorDevicePath));
                if (target.monitorFriendlyDeviceName[0]) display.name = target.monitorFriendlyDeviceName;
            }
        }
        // Only a single physical target may share this desktop rectangle.
        // If target-name lookup failed, use the same interface identity from GDI.
        if (matches == 1 && display.path.empty()) {
            std::vector<std::wstring> ids;
            for (DWORD i = 0; ; ++i) {
                DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
                if (!EnumDisplayDevicesW(display.device.c_str(), i, &device, EDD_GET_DEVICE_INTERFACE_NAME)) break;
                if ((device.StateFlags & DISPLAY_DEVICE_ACTIVE) && device.DeviceID[0]) ids.emplace_back(device.DeviceID);
            }
            if (ids.size() == 1) display.path = canonicalPath(utf8(ids.front()));
        }
        display.selectable = matches == 1 && !display.path.empty() && display.bounds.valid();
    }
    // Reject duplicates AND overlapping desktop regions (clone/driver oddities).
    for (std::size_t i = 0; i < context.displays.size(); ++i) {
        for (std::size_t j = i + 1; j < context.displays.size(); ++j) {
            auto& a = context.displays[i]; auto& b = context.displays[j];
            const bool overlap = a.bounds.left < b.bounds.right && b.bounds.left < a.bounds.right &&
                a.bounds.top < b.bounds.bottom && b.bounds.top < a.bounds.bottom;
            if (overlap || (!a.path.empty() && devicePathEqual(a.path, b.path))) a.selectable = b.selectable = false;
        }
    }
    std::sort(context.displays.begin(), context.displays.end(), [](const Display& a, const Display& b) { return a.device < b.device; });
    return context.displays;
}
std::wstring displayLabel(const Display& d, std::size_t ordinal) {
    return std::to_wstring(ordinal + 1) + L" - " + d.name + L" - " +
        std::to_wstring(d.bounds.width()) + L" x " + std::to_wstring(d.bounds.height()) +
        (d.hz ? L" / " + std::to_wstring(d.hz) + L" Hz" : L"") + (d.primary ? L" / Primary" : L"") +
        L" / " + d.device + (d.selectable ? L"" : L" / unavailable or mirrored");
}
std::wstring executablePath() {
    std::wstring path(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) winError("Executable path");
    path.resize(n); return path;
}
StartupInfo StartupRegistration::read() const {
    DWORD bytes{}, type{};
    LONG result = RegGetValueW(root_, subkey_.c_str(), L"OLED Blackout", RRF_RT_REG_SZ, &type, nullptr, &bytes);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return {};
    if (result != ERROR_SUCCESS) return {StartupState::Unreadable, static_cast<DWORD>(result)};
    if (bytes > 65536 || bytes < sizeof(wchar_t) || bytes % sizeof(wchar_t))
        return {StartupState::Unreadable, ERROR_INVALID_DATA};
    std::wstring value(bytes / sizeof(wchar_t) + 1, 0);
    result = RegGetValueW(root_, subkey_.c_str(), L"OLED Blackout", RRF_RT_REG_SZ, nullptr, value.data(), &bytes);
    if (result != ERROR_SUCCESS) return {StartupState::Unreadable, static_cast<DWORD>(result)};
    const auto end = value.find(L'\0');
    // Embedded nulls followed by data are malformed, rather than silently truncating a command.
    for (std::size_t i = end + 1; i < bytes / sizeof(wchar_t); ++i)
        if (value[i] != L'\0') return {StartupState::Unreadable, ERROR_INVALID_DATA};
    value.resize(end);
    return {classifyStartup(value, executable_, true, windowsOrdinalEqual), ERROR_SUCCESS};
}
void StartupRegistration::set(bool enabled) const {
    if (!enabled) {
        const LONG result = RegDeleteKeyValueW(root_, subkey_.c_str(), L"OLED Blackout");
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND)
            winError("Remove Windows startup entry", static_cast<DWORD>(result));
        return;
    }
    const auto command = startupCommand(executable_);
    HKEY key{};
    LONG result = RegCreateKeyExW(root_, subkey_.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) winError("Open Windows startup key", static_cast<DWORD>(result));
    struct Close { HKEY k; ~Close() { RegCloseKey(k); } } close{key};
    result = RegSetValueExW(key, L"OLED Blackout", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    if (result != ERROR_SUCCESS) winError("Write Windows startup entry", static_cast<DWORD>(result));
}
StartupRegistration currentUserStartup() {
    return {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", executablePath()};
}
} // namespace oled
