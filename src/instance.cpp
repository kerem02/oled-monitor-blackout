#include "instance.hpp"
namespace oled {
bool isActivationTarget(HWND window) noexcept {
    if (!window || !IsWindow(window) || GetAncestor(window, GA_ROOT) != window) return false;
    wchar_t name[128]{}, title[128]{};
    if (!GetClassNameW(window, name, static_cast<int>(std::size(name))) ||
        !GetWindowTextW(window, title, static_cast<int>(std::size(title)))) return false;
    return wcscmp(name, ControllerClass) == 0 && wcscmp(title, ControllerTitle) == 0 &&
        reinterpret_cast<UINT_PTR>(GetPropW(window, ActivationProperty)) == ActivationProtocol;
}
bool activateExistingInstance() {
    const UINT message = RegisterWindowMessageW(ActivationMessageName);
    if (!message) winError("Register activation message");
    // A concurrent first launch may still be creating its controller. Never enumerate processes.
    HWND target{};
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        target = FindWindowW(ControllerClass, ControllerTitle);
        if (isActivationTarget(target)) break;
        target = nullptr;
        Sleep(50);
    }
    if (!target) return false; // Includes a running v2.0 instance without the protocol.
    DWORD_PTR reply{};
    if (!SendMessageTimeoutW(target, message, ActivationProbe, ActivationProtocol,
        SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT, 1000, &reply) || reply != ActivationReply) return false;
    DWORD owner{};
    if (!isActivationTarget(target) || !GetWindowThreadProcessId(target, &owner) || !owner) return false;
    // Only the validated controller's owner receives permission. No process handle is opened.
    if (!AllowSetForegroundWindow(owner))
        OutputDebugStringW(L"OLED Blackout: foreground grant denied; Settings will request attention normally\n");
    reply = 0;
    return SendMessageTimeoutW(target, message, ActivationShow, ActivationProtocol,
        SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT, 1000, &reply) && reply == ActivationReply;
}
}
