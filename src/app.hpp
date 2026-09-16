#pragma once
#include "platform.hpp"
#include <shellapi.h>
#include <wtsapi32.h>

namespace oled {
class App {
public:
    App(HINSTANCE instance, std::filesystem::path dir);
    ~App();
    int run(bool startup);
    void emergencyHide() noexcept;
private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM) noexcept;
    static LRESULT CALLBACK overlayProc(HWND, UINT, WPARAM, LPARAM) noexcept;
    static INT_PTR CALLBACK dialogProc(HWND, UINT, WPARAM, LPARAM) noexcept;
    LRESULT message(UINT, WPARAM, LPARAM);
    INT_PTR dialogMessage(HWND, UINT, WPARAM, LPARAM);
    void initialize();
    void tick();
    void reset(const char* reason = "state reset");
    void scheduleRefresh(const char* reason);
    void refreshDisplays();
    void setOverlay(bool visible, const char* hideReason = nullptr);
    void clearIdentify() noexcept;
    void identify();
    void updateTray(bool add = false);
    void menu(POINT point);
    void settingsDialog();
    void fillDisplayList(HWND dialog);
    void initializeSettingsLayout(HWND dialog);
    void layoutSettings(HWND dialog);
    void scrollSettings(HWND dialog, bool horizontal, unsigned action, int position);
    void revealControl(HWND control);
    void updateSettingsStatus();
    void updateStartupStatus(HWND dialog);
    void updateHeadingFont(HWND dialog);
    void saveDialog(HWND dialog);
    void applySettings(Settings settings, bool persist = true);
    void registerHotkey();
    void notify(const std::wstring& text, bool warning = false);
    void reportError(const std::exception& error, HWND owner = nullptr) noexcept;
    void fatal(const char* message) noexcept;
    bool selectedHealthy() const noexcept;
    std::wstring statusText() const;

    HINSTANCE instance_{};
    HWND window_{}, overlay_{}, dialog_{};
    HICON icon_{};
    HPOWERNOTIFY powerNotify_{};
    bool sessionRegistered_{}, trayAdded_{}, hotkeyRegistered_{};
    bool overlayVisible_{}, topologyPending_{}, suspended_{}, locked_{}, displayOff_{};
    bool fatal_{}, menuOpen_{};
    unsigned uiDepth_{}, pollMs_{};
    ULONGLONG refreshAt_{}, retryAt_{};
    UINT taskbarCreated_{}, activationMessage_{};
    bool ready_{}, activationQueued_{}, trayVersion4_{}, endingSession_{};
    HWND lastSettingsFocus_{};
    ULONGLONG trayRetryAt_{};
    std::uint64_t topologyGeneration_{};
    std::filesystem::path dir_;
    Log log_;
    SettingsStore store_;
    Settings settings_;
    StateMachine machine_;
    State reportedState_{State::Paused};
    std::string lastDisplayReport_;
    std::vector<Display> displays_;
    std::optional<std::size_t> selected_;
    std::vector<HWND> identifyWindows_;
    SelectionList dialogList_;
    std::string draftPath_;
    bool fillingList_{}, updatingPreset_{}, layoutBusy_{};
    StartupInfo startupInDialog_{};
    struct ControlLayout { HWND window; RECT at96; };
    std::vector<ControlLayout> settingsLayout_;
    SIZE settingsCanvas96_{};
    int scrollX_{}, scrollY_{};
    HFONT headingFont_{};
};
} // namespace oled
