#pragma once
#include "platform.hpp"
namespace oled {
// Retain the v2 namespace to prevent 2.0 and 2.1 from running together.
inline constexpr wchar_t ControllerClass[] = L"OLEDBlackout.Controller.v2";
inline constexpr wchar_t ControllerTitle[] = L"OLED Blackout";
inline constexpr wchar_t InstanceMutex[] = L"Local\\OLED.Blackout.Singleton.v2";
inline constexpr wchar_t ActivationMessageName[] = L"OLED.Blackout.Activation.v2.1";
inline constexpr wchar_t ActivationProperty[] = L"OLED.Blackout.Protocol.v2.1";
bool isActivationTarget(HWND window) noexcept;
bool activateExistingInstance();
}
