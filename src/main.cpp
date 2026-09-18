#include "app.hpp"
#include "instance.hpp"
#include <commctrl.h>
#include <exception>

namespace {
oled::App* runningApp{};
LONG WINAPI crashHandler(EXCEPTION_POINTERS*) noexcept {
    if (runningApp) runningApp->emergencyHide();
    OutputDebugStringW(L"OLED Blackout: unhandled native exception; terminating\n");
    return EXCEPTION_EXECUTE_HANDLER;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    SetUnhandledExceptionFilter(crashHandler);
    std::set_terminate([] {
        if (runningApp) runningApp->emergencyHide();
        OutputDebugStringW(L"OLED Blackout: C++ termination\n");
        ExitProcess(1);
    });
    try {
        oled::Handle mutex(CreateMutexW(nullptr, FALSE, oled::InstanceMutex));
        const DWORD error = GetLastError();
        if (!mutex.get()) oled::winError("Create single-instance mutex", error);
        if (error == ERROR_ALREADY_EXISTS) {
            if (commandLine && std::wstring(commandLine) == L"--startup") return 0;
            if (!oled::activateExistingInstance()) {
                const char* message = "OLED Blackout is already running, but Settings could not be opened. Use its tray icon. If an older version is running, exit it before starting 2.1.2.";
                OutputDebugStringA(message);
                oled::showError(nullptr, message, L"OLED Blackout is already running");
                return 1;
            }
            return 0;
        }
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
        if (!InitCommonControlsEx(&controls)) oled::winError("Initialize controls");
        // Awareness is established by the embedded manifest before creating any HWNDs.
        oled::App app(instance, oled::dataDirectory());
        runningApp = &app;
        struct Clear { ~Clear() { runningApp = nullptr; } } clear;
        return app.run(commandLine && std::wstring(commandLine) == L"--startup");
    } catch (const std::exception& error) {
        OutputDebugStringA(error.what());
        oled::showError(nullptr, error.what(), L"OLED Blackout could not continue");
        return 1;
    }
}
