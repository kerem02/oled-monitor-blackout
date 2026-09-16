#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "core.hpp"
#include <filesystem>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace oled {
std::wstring wide(const std::string& text);
std::string utf8(const std::wstring& text);
bool windowsOrdinalEqual(const std::wstring& a, const std::wstring& b);
bool devicePathEqual(const std::string& a, const std::string& b);
std::string windowsErrorText(const char* operation, DWORD code);
void showError(HWND owner, const char* text, const wchar_t* title) noexcept;
[[noreturn]] void winError(const char* operation, DWORD code = GetLastError());
class Handle {
public:
    explicit Handle(HANDLE h = nullptr) noexcept : h_(h) {}
    ~Handle() { if (h_ && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return h_; }
private: HANDLE h_;
};
class Log {
public:
    explicit Log(std::filesystem::path dir) : dir_(std::move(dir)) {}
    void write(const std::string& message) noexcept;
private: std::filesystem::path dir_;
};
std::filesystem::path dataDirectory();
class SettingsStore {
public:
    SettingsStore(std::filesystem::path dir, Log& log) : dir_(std::move(dir)), log_(log) {}
    ParsedSettings load();
    void save(const Settings& settings);
private:
    std::filesystem::path dir_;
    Log& log_;
};
struct Display {
    HMONITOR handle{};
    Rect bounds{};
    std::wstring device, name;
    std::string path;
    bool primary{}, selectable{};
    unsigned hz{};
};
std::vector<Display> enumerateDisplays();
std::wstring displayLabel(const Display& d, std::size_t ordinal);
std::wstring executablePath();
struct StartupInfo { StartupState state{StartupState::Disabled}; DWORD error{}; };
class StartupRegistration {
public:
    StartupRegistration(HKEY root, std::wstring subkey, std::wstring executable)
        : root_(root), subkey_(std::move(subkey)), executable_(std::move(executable)) {}
    StartupInfo read() const;
    void set(bool enabled) const;
private:
    HKEY root_;
    std::wstring subkey_, executable_;
};
StartupRegistration currentUserStartup();
} // namespace oled
