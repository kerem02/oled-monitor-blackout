#include "app.hpp"
#include "instance.hpp"
#include "../resources/resource.h"
#include <commctrl.h>
#include <dbt.h>
#include <algorithm>
#include <cstdio>

namespace oled {
namespace {
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT RefreshMessage = WM_APP + 2;
constexpr UINT PollTimer = 1, IdentifyTimer = 2, HotkeyId = 1;
constexpr UINT ShowSettingsMessage = WM_APP + 3;
constexpr wchar_t OverlayClass[] = L"OLEDBlackout.Overlay.v2";
constexpr GUID ConsoleDisplayState = {0x6fe69556,0x704a,0x47a0,{0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};
struct MenuHandle {
    HMENU value{CreatePopupMenu()};
    MenuHandle() { if (!value) winError("Create menu"); }
    ~MenuHandle() { if (value) DestroyMenu(value); }
    MenuHandle(const MenuHandle&) = delete;
    MenuHandle& operator=(const MenuHandle&) = delete;
    HMENU release() { auto result = value; value = nullptr; return result; }
};
void item(HMENU menu, UINT flags, UINT_PTR id, const std::wstring& label) {
    if (!AppendMenuW(menu, flags, id, label.c_str())) winError("Append menu item");
}
std::wstring menuLabel(std::wstring text) {
    // EDID model names may contain '&', which menus otherwise interpret as mnemonics.
    std::wstring result;
    for (wchar_t c : text) { result += c; if (c == L'&') result += c; }
    return result;
}
}
App::App(HINSTANCE instance, std::filesystem::path dir)
    : instance_(instance), dir_(std::move(dir)), log_(dir_), store_(dir_, log_) {}
App::~App() {
    ready_ = false;
    if (window_) RemovePropW(window_, ActivationProperty);
    emergencyHide();
    clearIdentify();
    if (powerNotify_) UnregisterPowerSettingNotification(powerNotify_);
    if (sessionRegistered_) WTSUnRegisterSessionNotification(window_);
    if (hotkeyRegistered_) UnregisterHotKey(window_, HotkeyId);
    if (trayAdded_) {
        NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = window_; data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data);
    }
    if (overlay_) DestroyWindow(overlay_);
    if (window_) { KillTimer(window_, PollTimer); DestroyWindow(window_); }
    if (icon_) DestroyIcon(icon_);
    log_.write("Shutdown");
}
void App::initialize() {
    log_.write("Startup v2.1.1");
    auto parsed = store_.load(); settings_ = std::move(parsed.settings);
    WNDCLASSEXW klass{}; klass.cbSize = sizeof(klass); klass.hInstance = instance_;
    klass.lpfnWndProc = windowProc; klass.lpszClassName = ControllerClass;
    if (!RegisterClassExW(&klass)) winError("Register controller class");
    klass.lpfnWndProc = overlayProc; klass.lpszClassName = OverlayClass;
    klass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!RegisterClassExW(&klass)) winError("Register overlay class");
    // Hidden top-level HWND receives broadcasts; HWND_MESSAGE would miss them.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, ControllerClass, ControllerTitle, WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (!window_) winError("Create controller");
    overlay_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        OverlayClass, L"OLED Blackout surface", WS_POPUP, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    if (!overlay_) winError("Create blackout window");
    // Opaque, not translucent. LAYERED + TRANSPARENT provides cross-process hit-test pass-through.
    if (!SetLayeredWindowAttributes(overlay_, 0, 255, LWA_ALPHA)) winError("Set blackout opacity");
    icon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0));
    if (!icon_) winError("Load icon");
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (!taskbarCreated_) winError("Register TaskbarCreated");
    sessionRegistered_ = WTSRegisterSessionNotification(window_, NOTIFY_FOR_THIS_SESSION) != FALSE;
    if (!sessionRegistered_) log_.write("Session notifications unavailable; cursor failures still pause blackout");
    powerNotify_ = RegisterPowerSettingNotification(window_, &ConsoleDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!powerNotify_) log_.write("Display-power notifications unavailable");
    registerHotkey();
    refreshDisplays();
    updateTray(true);
    pollMs_ = pollInterval(State::OnDisplay,false);
    if (!SetTimer(window_, PollTimer, pollMs_, nullptr)) winError("Create polling timer");
    tick();
    if (parsed.status == ConfigStatus::Corrupt || parsed.status == ConfigStatus::Unsupported)
        notify(parsed.backupFailed ? L"Settings were reset safely, but the backup could not be saved. No OLED is selected. See the log." :
            L"Settings were reset safely and backed up. Select your OLED again.", true);
    if (parsed.status == ConfigStatus::Sanitized) notify(L"Invalid delay was reset to 30 seconds.", true);
    activationMessage_ = RegisterWindowMessageW(ActivationMessageName);
    if (!activationMessage_) winError("Register instance activation");
    if (!SetPropW(window_, ActivationProperty, reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(ActivationProtocol))))
        winError("Advertise instance activation");
    ready_ = true;
}
int App::run(bool startup) {
    initialize();
    if (!startup && settings_.monitorPath.empty()) settingsDialog();
    MSG msg{};
    int result{};
    while ((result = static_cast<int>(GetMessageW(&msg, nullptr, 0, 0))) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (result < 0) winError("Message loop");
    return fatal_ ? 1 : static_cast<int>(msg.wParam);
}
void App::emergencyHide() noexcept {
    if (overlay_) ShowWindow(overlay_, SW_HIDE);
    for (HWND card : identifyWindows_) ShowWindow(card, SW_HIDE);
    overlayVisible_ = false;
}
void App::reset(const char* reason) { setOverlay(false, reason); clearIdentify(); machine_.reset(); }
void App::scheduleRefresh(const char* reason) {
    ++topologyGeneration_;
    reset(reason); selected_.reset(); topologyPending_ = true;
    refreshAt_ = GetTickCount64() + 300;
}
void App::refreshDisplays() {
    const auto generation = ++topologyGeneration_;
    reset("display snapshot refresh"); selected_.reset(); topologyPending_ = false;
    try {
        auto discovered = enumerateDisplays();
        if (generation != topologyGeneration_ || topologyPending_) return;
        displays_ = std::move(discovered);
        std::vector<Identity> identities;
        for (const auto& d : displays_) identities.push_back({d.path, d.selectable});
        selected_ = resolveMonitor(settings_.monitorPath, identities, devicePathEqual);
        std::string report = "Display snapshot: " + std::to_string(displays_.size()) + " desktop regions; selection " +
            (selected_ ? "matched" : "unavailable/unselected");
        for (const auto& d : displays_) report += "\n  Display " + utf8(d.device) + " " + utf8(d.name) + " " +
            std::to_string(d.bounds.width()) + "x" + std::to_string(d.bounds.height()) +
            (d.selectable ? " selectable" : " rejected");
        if (report != lastDisplayReport_) { log_.write(report); lastDisplayReport_ = std::move(report); }
        // Do not log device paths: they may contain monitor serial numbers.
    } catch (const std::exception& e) {
        selected_.reset(); displays_.clear();
        const std::string report = std::string("Display refresh failed: ") + e.what();
        if (report != lastDisplayReport_) { log_.write(report); lastDisplayReport_ = report; }
    }
    retryAt_ = GetTickCount64() + 5000;
    if (dialog_) fillDisplayList(dialog_);
}
bool App::selectedHealthy() const noexcept {
    if (!selected_ || *selected_ >= displays_.size()) return false;
    const auto& d = displays_[*selected_];
    MONITORINFOEXW info{}; info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(d.handle, &info)) return false;
    const Rect current{info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom};
    return d.bounds == current && _wcsicmp(d.device.c_str(), info.szDevice) == 0;
}
void App::tick() {
    const auto now = GetTickCount64();
    if (!trayAdded_ && now >= trayRetryAt_) updateTray(true);
    if (dialog_) {
        HWND focus = GetFocus();
        if (focus != lastSettingsFocus_) { lastSettingsFocus_ = focus; revealControl(focus); }
    }
    const bool blocked = suspended_ || locked_ || displayOff_ || endingSession_ || !trayAdded_;
    if (!blocked && ((topologyPending_ && now >= refreshAt_) ||
        (!topologyPending_ && !selected_ && !settings_.monitorPath.empty() && now >= retryAt_))) refreshDisplays();
    if (selected_ && !selectedHealthy()) scheduleRefresh("selected display health check failed");
    POINT cursor{};
    Input input;
    input.enabled = settings_.enabled;
    input.available = selected_.has_value() && !topologyPending_;
    input.paused = blocked || uiDepth_ > 0 || menuOpen_ || !identifyWindows_.empty();
    input.cursorValid = settings_.enabled && !input.paused && GetCursorPos(&cursor) != FALSE;
    input.cursor = {cursor.x, cursor.y};
    input.nowMs = now; input.delaySeconds = settings_.delaySeconds;
    if (selected_) input.bounds = displays_[*selected_].bounds;
    State next = machine_.step(input);
    if (next == State::BlackedOut && !overlayVisible_) {
        // Revalidate identity immediately before covering a screen. No enumeration on steady-state ticks.
        bool verified = false;
        const auto generation = topologyGeneration_;
        const auto expectedPath = settings_.monitorPath;
        try {
            const auto fresh = enumerateDisplays();
            std::vector<Identity> identities;
            for (const auto& d : fresh) identities.push_back({d.path, d.selectable});
            const auto match = resolveMonitor(settings_.monitorPath, identities, devicePathEqual);
            verified = generation == topologyGeneration_ && match && selected_ && !topologyPending_ &&
                devicePathEqual(expectedPath,settings_.monitorPath) && fresh[*match].bounds == input.bounds &&
                fresh[*match].handle == displays_[*selected_].handle && fresh[*match].device == displays_[*selected_].device;
        } catch (const std::exception& e) {
            log_.write(std::string("Pre-blackout verification failed: ") + e.what());
        }
        if (!verified) { scheduleRefresh("pre-blackout display verification failed"); next = State::Paused; }
        else {
            input.enabled = settings_.enabled;
            input.available = selected_.has_value() && !topologyPending_;
            input.paused = suspended_ || locked_ || displayOff_ || endingSession_ || !trayAdded_ || uiDepth_ > 0 || menuOpen_ || !identifyWindows_.empty();
            input.nowMs = GetTickCount64();
            input.cursorValid = settings_.enabled && !input.paused && GetCursorPos(&cursor) != FALSE;
            input.cursor = {cursor.x, cursor.y};
            next = machine_.step(input);
        }
    }
    const char* hideReason = nullptr;
    if (next != State::BlackedOut) {
        if (!settings_.enabled) hideReason = "automatic blackout disabled";
        else if (!selected_ || topologyPending_) hideReason = "selected display unavailable or refresh pending";
        else if (suspended_) hideReason = "system suspended";
        else if (locked_) hideReason = "session locked or disconnected";
        else if (displayOff_) hideReason = "console display powered off";
        else if (endingSession_) hideReason = "session ending";
        else if (!trayAdded_) hideReason = "tray icon unavailable";
        else if (uiDepth_ > 0) hideReason = "application dialog active";
        else if (menuOpen_) hideReason = "tray menu open";
        else if (!identifyWindows_.empty()) hideReason = "monitor identification active";
        else if (!input.cursorValid) hideReason = "cursor position unavailable";
        else if (input.bounds.contains(input.cursor)) hideReason = "cursor entered selected display";
        else hideReason = "state machine left blackout";
    }
    setOverlay(next == State::BlackedOut, hideReason);
    if (topologyPending_) { setOverlay(false, "display refresh pending"); machine_.reset(); next = State::Paused; }
    const unsigned interval = pollInterval(next,blocked);
    if (interval != pollMs_) {
        if (!SetTimer(window_, PollTimer, interval, nullptr)) winError("Change polling timer");
        pollMs_ = interval;
    }
    if (next != reportedState_) { reportedState_ = next; updateTray(); updateSettingsStatus(); }
}
void App::setOverlay(bool visible, const char* hideReason) {
    if (visible == overlayVisible_) return;
    if (!visible) {
        emergencyHide();
        log_.write(std::string("Blackout hidden: ") + (hideReason ? hideReason : "unspecified state change"));
        return;
    }
    if (!selected_ || topologyPending_) return;
    const auto generation = topologyGeneration_;
    const auto bounds = displays_[*selected_].bounds; // Copy before synchronous window callbacks.
    if (!SetWindowPos(overlay_, HWND_TOPMOST, bounds.left, bounds.top, bounds.width(), bounds.height(),
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW)) winError("Show blackout");
    overlayVisible_ = true;
    if (generation != topologyGeneration_ || topologyPending_ || !settings_.enabled || suspended_ || locked_ ||
        displayOff_ || endingSession_ || !trayAdded_) {
        setOverlay(false, "post-show safety validation failed"); machine_.reset(); return;
    }
    log_.write("Blackout shown");
}
std::wstring App::statusText() const {
    if (!settings_.enabled) return L"Disabled";
    if (settings_.monitorPath.empty()) return L"Select an OLED monitor";
    if (!selected_ || topologyPending_) return L"Selected OLED unavailable; blackout stopped";
    if (!trayAdded_) return L"Paused - restoring tray icon";
    if (dialog_) return L"Paused - Settings open";
    if (machine_.state() == State::OnDisplay) return L"Active - cursor on OLED";
    return wide(stateName(machine_.state()));
}
void App::updateTray(bool add) {
    if (!window_ || !icon_) return;
    NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = window_; data.uID = 1;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    data.hIcon = icon_; data.uCallbackMessage = TrayMessage;
    std::wstring tip = L"OLED Blackout: " + statusText() + L" | " + std::to_wstring(settings_.delaySeconds) + L" s";
    if (settings_.hotkeyEnabled && !hotkeyRegistered_) tip += L" | Hotkey unavailable";
    if (selected_) tip += L"\n" + displays_[*selected_].name;
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
    bool success = Shell_NotifyIconW(add || !trayAdded_ ? NIM_ADD : NIM_MODIFY, &data) != FALSE;
    if (!success && !trayAdded_) success = Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
    if (success) {
        if (!trayAdded_ && trayRetryAt_) log_.write("Tray icon restored");
        trayAdded_ = true; trayRetryAt_ = 0; data.uVersion = NOTIFYICON_VERSION_4;
        trayVersion4_ = Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
    } else {
        if (!trayRetryAt_) log_.write("Tray unavailable; blackout paused, retrying automatically");
        trayAdded_ = false; trayRetryAt_ = GetTickCount64() + 2000;
        reset("tray icon unavailable");
    }
}
void App::notify(const std::wstring& text, bool warning) {
    NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = window_; data.uID = 1;
    data.uFlags = NIF_INFO; data.dwInfoFlags = warning ? NIIF_WARNING : NIIF_INFO;
    wcsncpy_s(data.szInfoTitle, L"OLED Blackout", _TRUNCATE);
    wcsncpy_s(data.szInfo, text.c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_MODIFY, &data)) log_.write("Tray notification unavailable");
}
void App::registerHotkey() {
    if (hotkeyRegistered_) UnregisterHotKey(window_, HotkeyId);
    hotkeyRegistered_ = false;
    if (settings_.hotkeyEnabled) {
        hotkeyRegistered_ = RegisterHotKey(window_, HotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'B') != FALSE;
        if (!hotkeyRegistered_) log_.write("Ctrl+Alt+B unavailable (conflict or OS restriction); tray toggle remains available");
    }
}
void App::applySettings(Settings settings, bool persist) {
    // Always remove blackout first, including if persistence subsequently fails.
    reset("settings changed");
    settings_ = std::move(settings);
    registerHotkey(); refreshDisplays(); tick(); updateTray();
    try { if (persist) store_.save(settings_); }
    catch (const std::exception& e) { reportError(e); }
}
void App::clearIdentify() noexcept {
    if (window_) KillTimer(window_, IdentifyTimer);
    for (HWND handle : identifyWindows_) DestroyWindow(handle);
    identifyWindows_.clear();
}
void App::identify() {
    clearIdentify(); reset("monitor identification opened");
    for (std::size_t i = 0; i < displays_.size(); ++i) {
        const auto bounds = displays_[i].bounds;
        const int width = std::min(240, bounds.width());
        const int height = std::min(150, bounds.height());
        HWND w = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
            OverlayClass, L"OLED Blackout identification", WS_POPUP,
            bounds.left + (bounds.width() - width) / 2,
            bounds.top + (bounds.height() - height) / 2, width, height, window_, nullptr, instance_, nullptr);
        if (!w) winError("Create monitor identification");
        try { identifyWindows_.push_back(w); } catch (...) { DestroyWindow(w); throw; }
        SetWindowLongPtrW(w, GWLP_USERDATA, static_cast<LONG_PTR>(i + 1));
        if (!SetLayeredWindowAttributes(w, 0, 255, LWA_ALPHA)) winError("Set identification opacity");
        ShowWindow(w, SW_SHOWNOACTIVATE);
    }
    if (!SetTimer(window_, IdentifyTimer, 2000, nullptr)) winError("Identify timer");
}
void App::menu(POINT point) {
    if (menuOpen_) return;
    const auto statusBeforeMenu = statusText();
    menuOpen_ = true; reset("tray menu opened");
    struct Done { App& a; ~Done() { a.menuOpen_ = false; a.machine_.reset(); } } done{*this};
    MenuHandle root, delay, monitors;
    item(root.value, MF_GRAYED, 0, statusBeforeMenu);
    item(root.value, MF_GRAYED, 0, selected_ ? L"OLED: " + menuLabel(displays_[*selected_].name) : L"OLED: unavailable or not selected");
    item(root.value, MF_STRING | (settings_.enabled ? MF_CHECKED : 0), 1, L"Enable automatic blackout\tCtrl+Alt+B");
    if (settings_.hotkeyEnabled && !hotkeyRegistered_) item(root.value, MF_GRAYED, 0, L"Hotkey unavailable (conflict)");
    item(root.value, MF_SEPARATOR, 0, L"");
    constexpr unsigned delays[] = {10, 30, 60, 120};
    for (unsigned i = 0; i < 4; ++i)
        item(delay.value, MF_STRING | (settings_.delaySeconds == delays[i] ? MF_CHECKED : 0), 100 + i, std::to_wstring(delays[i]) + L" seconds");
    item(delay.value, MF_STRING, 2, L"Custom delay... (Settings)");
    item(root.value, MF_POPUP, reinterpret_cast<UINT_PTR>(delay.value), L"Blackout delay"); delay.release();
    std::vector<std::string> paths;
    for (std::size_t i = 0; i < displays_.size(); ++i) {
        paths.push_back(displays_[i].path);
        item(monitors.value, MF_STRING | (displays_[i].selectable ? 0 : MF_GRAYED) |
            (selected_ && *selected_ == i ? MF_CHECKED : 0), 1000 + i, menuLabel(displayLabel(displays_[i], i)));
    }
    if (paths.empty()) item(monitors.value, MF_GRAYED, 0, L"No selectable displays");
    item(root.value, MF_POPUP, reinterpret_cast<UINT_PTR>(monitors.value), L"OLED monitor"); monitors.release();
    item(root.value, MF_STRING, 3, L"Identify monitors");
    const auto startup = currentUserStartup().read();
    item(root.value, MF_STRING | (startup.state == StartupState::Current ? MF_CHECKED : 0), 4,
        startup.state == StartupState::Stale ? L"Windows startup: old path - Settings..." :
        startup.state == StartupState::Unreadable ? L"Windows startup: unreadable - Settings..." : L"Start with Windows");
    item(root.value, MF_STRING, 2, L"Settings...");
    item(root.value, MF_STRING, 5, L"Refresh displays");
    item(root.value, MF_SEPARATOR, 0, L"");
    item(root.value, MF_STRING, 6, L"About");
    item(root.value, MF_STRING, 7, L"Exit");
    // Foreground activation is ONLY for an explicit tray-menu request, never the blackout path.
    SetForegroundWindow(window_);
    UINT command = TrackPopupMenuEx(root.value, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    if (command == 1) { auto s = settings_; s.enabled = !s.enabled; applySettings(std::move(s)); }
    else if (command == 2) settingsDialog();
    else if (command == 3) identify();
    else if (command == 4) {
        const auto current = currentUserStartup().read();
        if (current.state == StartupState::Stale || current.state == StartupState::Unreadable) settingsDialog();
        else { currentUserStartup().set(current.state != StartupState::Current); log_.write("Windows startup preference changed"); }
    }
    else if (command == 5) scheduleRefresh("manual display refresh");
    else if (command == 6) {
        MessageBoxW(window_, L"OLED Blackout 2.1.1\nNative Windows utility - MIT License\nCopyright (c) 2026 Kerem Albayrak\n\n"
            L"Ctrl+Alt+B toggles blackout.\nNo injection, input hooks, telemetry or network access.\n"
            L"Compatibility with every anti-cheat product cannot be guaranteed.\n\n"
            L"Settings and bounded logs: %LocalAppData%\\OLED Blackout", L"About OLED Blackout", MB_OK | MB_ICONINFORMATION);
    } else if (command == 7) { emergencyHide(); if (dialog_) EndDialog(dialog_, IDCANCEL); PostQuitMessage(0); }
    else if (command >= 100 && command < 104) { auto s = settings_; s.delaySeconds = delays[command - 100]; applySettings(std::move(s)); }
    else if (command >= 1000 && command - 1000 < paths.size()) {
        // Resolve the captured path against a fresh topology, not the possibly stale menu index.
        auto s = settings_; s.monitorPath = paths[command - 1000]; applySettings(std::move(s));
    }
}
void App::reportError(const std::exception& error, HWND owner) noexcept {
    emergencyHide(); machine_.reset();
    log_.write(std::string("Operation failed: ") + error.what());
    ++uiDepth_;
    showError(owner ? owner : window_, error.what(), L"OLED Blackout - operation failed");
    --uiDepth_;
}
void App::fatal(const char* message) noexcept {
    emergencyHide(); clearIdentify(); fatal_ = true;
    log_.write(message);
    if (dialog_) EndDialog(dialog_, IDCANCEL);
    PostQuitMessage(1);
    showError(nullptr, message, L"OLED Blackout stopped safely");
}
LRESULT App::message(UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreated_ && msg == taskbarCreated_) {
        reset("Explorer taskbar recreated"); trayAdded_ = false; updateTray(true); return 0;
    }
    if (activationMessage_ && msg == activationMessage_) {
        if (!ready_ || !validActivation(wp,static_cast<std::uint64_t>(lp)) || !isActivationTarget(window_)) return 0;
        if (wp == ActivationShow && !activationQueued_) {
            if (!PostMessageW(window_,ShowSettingsMessage,0,0)) return 0;
            activationQueued_ = true;
        }
        return static_cast<LRESULT>(ActivationReply);
    }
    switch (msg) {
    case ShowSettingsMessage:
        activationQueued_ = false;
        if (menuOpen_) EndMenu();
        settingsDialog(); return 0;
    case WM_TIMER:
        if (wp == PollTimer) tick();
        else if (wp == IdentifyTimer) clearIdentify();
        return 0;
    case WM_HOTKEY:
        if (wp == HotkeyId) {
            auto s = settings_; s.enabled = !s.enabled; applySettings(std::move(s));
            if (dialog_) CheckDlgButton(dialog_, IDC_ENABLED, settings_.enabled ? BST_CHECKED : BST_UNCHECKED);
        }
        return 0;
    case TrayMessage: {
        const UINT event = trayVersion4_ ? LOWORD(lp) : static_cast<UINT>(lp);
        if (event == WM_CONTEXTMENU || (!trayVersion4_ && event == WM_RBUTTONUP)) {
            POINT p{};
            if (!GetCursorPos(&p)) {
                NOTIFYICONIDENTIFIER identity{}; identity.cbSize=sizeof(identity); identity.hWnd=window_; identity.uID=1;
                RECT icon{};
                if (FAILED(Shell_NotifyIconGetRect(&identity,&icon))) return 0;
                p={icon.left,icon.bottom};
            }
            try { menu(p); } catch (const std::exception& e) { reportError(e); }
        } else if (event == NIN_SELECT || event == NIN_KEYSELECT || (!trayVersion4_ && event == WM_LBUTTONUP)) settingsDialog();
        return 0;
    }
    case RefreshMessage:
    case WM_DISPLAYCHANGE:
        log_.write("Display topology changed"); scheduleRefresh("display topology notification"); return 0;
    case WM_SETTINGCHANGE:
        // This broadcast covers many unrelated user-preference changes and is
        // commonly sent in the background. Display topology has dedicated
        // WM_DISPLAYCHANGE/WM_DEVICECHANGE notifications; do not blink an
        // active blackout for an unrelated setting update.
        return 0;
    case WM_DEVICECHANGE:
        if (wp == DBT_DEVNODES_CHANGED || wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE)
            scheduleRefresh("display device notification");
        return TRUE;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMSUSPEND) { suspended_ = true; reset("system suspending"); }
        else if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
            suspended_ = false; displayOff_ = false; log_.write("Resume"); scheduleRefresh("system resume");
        } else if (wp == PBT_POWERSETTINGCHANGE && lp) {
            const auto* power = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
            if (IsEqualGUID(power->PowerSetting, ConsoleDisplayState) && power->DataLength == sizeof(DWORD)) {
                DWORD state{}; CopyMemory(&state, power->Data, sizeof(state));
                const auto transition = displayPowerTransition(displayOff_, state);
                if (transition == DisplayPowerTransition::BecameOff) {
                    displayOff_ = true;
                    reset("console display powered off");
                } else if (transition == DisplayPowerTransition::BecameOn) {
                    displayOff_ = false;
                    scheduleRefresh("console display powered on");
                }
            }
        }
        return TRUE;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_LOCK || wp == WTS_CONSOLE_DISCONNECT || wp == WTS_REMOTE_DISCONNECT) {
            locked_ = true; reset("session locked or disconnected");
        }
        else if (wp == WTS_SESSION_UNLOCK || wp == WTS_CONSOLE_CONNECT || wp == WTS_REMOTE_CONNECT) {
            locked_ = false; scheduleRefresh("session unlocked or connected");
        }
        return 0;
    case WM_QUERYENDSESSION: endingSession_=true; reset("session ending"); clearIdentify(); return TRUE;
    case WM_ENDSESSION:
        endingSession_=wp != 0; reset(wp ? "session ended" : "session end cancelled");
        if (wp) { if (dialog_) EndDialog(dialog_,IDCANCEL); PostQuitMessage(0); }
        return 0;
    case WM_CLOSE: emergencyHide(); if (dialog_) EndDialog(dialog_, IDCANCEL); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window_, msg, wp, lp);
}
LRESULT CALLBACK App::windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->window_ = hwnd;
    }
    if (!app) return DefWindowProcW(hwnd, msg, wp, lp);
    if (app->fatal_) return DefWindowProcW(hwnd, msg, wp, lp);
    try { return app->message(msg, wp, lp); }
    catch (const std::exception& e) { app->fatal(e.what()); }
    catch (...) { app->fatal("Unexpected Windows callback failure. Blackout removed."); }
    return 0;
}
INT_PTR CALLBACK App::dialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    if (msg == WM_INITDIALOG) { app = reinterpret_cast<App*>(lp); SetWindowLongPtrW(hwnd, DWLP_USER, lp); }
    if (!app) return FALSE;
    try { return app->dialogMessage(hwnd, msg, wp, lp); }
    catch (const std::exception& e) {
        app->reportError(e, hwnd);
        if (msg == WM_INITDIALOG) EndDialog(hwnd, IDCANCEL);
    } catch (...) { app->fatal("Unexpected settings failure. Blackout removed."); }
    return TRUE;
}
LRESULT CALLBACK App::overlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_ERASEBKGND: return 1;
    case WM_DPICHANGED:
        // Ignore the suggested logical rect. The controller already positions
        // this window from physical monitor bounds. Moving/showing the overlay
        // across mixed-DPI displays must not reset an active blackout.
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
        RECT r{}; GetClientRect(hwnd, &r); FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const auto ordinal = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (ordinal) {
            wchar_t text[32]{}; swprintf_s(text, L"%lld", static_cast<long long>(ordinal));
            HFONT font = CreateFontW(-90, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
            SetTextColor(dc, RGB(255, 255, 255)); SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (old) SelectObject(dc, old);
            if (font) DeleteObject(font);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace oled
