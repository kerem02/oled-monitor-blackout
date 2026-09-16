#include "app.hpp"
#include "../resources/resource.h"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <limits>

namespace oled {
namespace {
constexpr UINT LayoutMessage = WM_APP + 4, NavigateMessage = WM_APP + 5;
LRESULT CALLBACK monitorListProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (message == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END || wp == VK_PRIOR || wp == VK_NEXT)) {
        SendMessageW(GetParent(window), NavigateMessage, wp, 0);
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, monitorListProc, id);
    return DefSubclassProc(window, message, wp, lp);
}
constexpr unsigned Presets[] = {10, 30, 60, 120};
constexpr int ColumnWidths[] = {32, 158, 94, 60, 60, 100, 140}; // At 96 DPI; native horizontal scroll handles long text.
const wchar_t* ColumnNames[] = {L"No.", L"Model", L"Resolution", L"Refresh", L"Primary", L"Device", L"Availability"};
int scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }
void cell(HWND list, int row, int column, const std::wstring& text) {
    LVITEMW item{}; item.iSubItem = column; item.pszText = const_cast<wchar_t*>(text.c_str());
    if (!SendMessageW(list, LVM_SETITEMTEXTW, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&item)))
        throw std::runtime_error("Could not populate display list");
}
}
void App::settingsDialog() {
    if (dialog_) {
        if (IsIconic(dialog_)) ShowWindow(dialog_, SW_RESTORE);
        if (!SetForegroundWindow(dialog_)) {
            FLASHWINFO flash{sizeof(flash), dialog_, FLASHW_TRAY, 3, 0}; FlashWindowEx(&flash);
            log_.write("Settings opened; Windows denied foreground activation");
        }
        return;
    }
    ++uiDepth_; reset();
    const INT_PTR result = DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, dialogProc, reinterpret_cast<LPARAM>(this));
    dialog_ = nullptr; --uiDepth_; machine_.reset(); settingsLayout_.clear();
    if (headingFont_) { DeleteObject(headingFont_); headingFont_ = nullptr; }
    if (result == -1) winError("Open Settings");
}
void App::updateHeadingFont(HWND dialog) {
    const HFONT base = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
    LOGFONTW font{};
    if (!base || !GetObjectW(base, sizeof(font), &font)) return;
    font.lfWeight = FW_SEMIBOLD;
    font.lfHeight = MulDiv(font.lfHeight, 3, 2);
    HFONT replacement = CreateFontIndirectW(&font);
    if (!replacement) return; // Standard dialog font remains a usable fallback.
    SendDlgItemMessageW(dialog, IDC_HEADER, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
    if (headingFont_) DeleteObject(headingFont_);
    headingFont_ = replacement;
}
void App::initializeSettingsLayout(HWND dialog) {
    settingsLayout_.clear(); scrollX_ = scrollY_ = 0;
    const UINT dpi = GetDpiForWindow(dialog);
    RECT client{}; GetClientRect(dialog, &client);
    settingsCanvas96_ = {MulDiv(client.right, 96, static_cast<int>(dpi)), MulDiv(client.bottom, 96, static_cast<int>(dpi))};
    for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT rect{}; GetWindowRect(child, &rect);
        if (GetDlgCtrlID(child) == IDC_PRESET) SendMessageW(child, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&rect));
        MapWindowPoints(HWND_DESKTOP, dialog, reinterpret_cast<POINT*>(&rect), 2);
        settingsLayout_.push_back({child, {MulDiv(rect.left,96,static_cast<int>(dpi)), MulDiv(rect.top,96,static_cast<int>(dpi)),
            MulDiv(rect.right,96,static_cast<int>(dpi)), MulDiv(rect.bottom,96,static_cast<int>(dpi))}});
    }
    // At large scaling on small displays the native scrollbars keep every control reachable.
    MONITORINFO info{}; info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &info)) {
        RECT window{}; GetWindowRect(dialog, &window);
        const int width = std::min(window.right - window.left, info.rcWork.right - info.rcWork.left);
        const int height = std::min(window.bottom - window.top, info.rcWork.bottom - info.rcWork.top);
        const int x = std::clamp(static_cast<int>(window.left), static_cast<int>(info.rcWork.left), static_cast<int>(info.rcWork.right) - width);
        const int y = std::clamp(static_cast<int>(window.top), static_cast<int>(info.rcWork.top), static_cast<int>(info.rcWork.bottom) - height);
        SetWindowPos(dialog, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    layoutSettings(dialog); updateHeadingFont(dialog);
}
void App::layoutSettings(HWND dialog) {
    if (settingsLayout_.empty() || layoutBusy_) return;
    layoutBusy_ = true;
    struct Reset { bool& value; ~Reset() { value = false; } } reset{layoutBusy_};
    const UINT dpi = GetDpiForWindow(dialog);
    const int contentWidth = scale(settingsCanvas96_.cx, dpi), contentHeight = scale(settingsCanvas96_.cy, dpi);
    RECT client{};
    // Scrollbars change client area; two passes settle their interdependent visibility.
    for (int pass = 0; pass < 2; ++pass) {
        GetClientRect(dialog, &client);
        SCROLLINFO horizontal{sizeof(horizontal), SIF_RANGE | SIF_PAGE | SIF_POS, 0, contentWidth - 1,
            static_cast<UINT>(std::max<LONG>(1,client.right)), scrollX_, 0};
        SCROLLINFO vertical{sizeof(vertical), SIF_RANGE | SIF_PAGE | SIF_POS, 0, contentHeight - 1,
            static_cast<UINT>(std::max<LONG>(1,client.bottom)), scrollY_, 0};
        SetScrollInfo(dialog, SB_HORZ, &horizontal, TRUE); SetScrollInfo(dialog, SB_VERT, &vertical, TRUE);
    }
    scrollX_ = GetScrollPos(dialog, SB_HORZ); scrollY_ = GetScrollPos(dialog, SB_VERT);
    for (const auto& control : settingsLayout_) {
        const auto& r = control.at96;
        SetWindowPos(control.window, nullptr, scale(r.left,dpi)-scrollX_, scale(r.top,dpi)-scrollY_,
            scale(r.right-r.left,dpi), scale(r.bottom-r.top,dpi), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    for (int column = 0; column < 7; ++column)
        ListView_SetColumnWidth(GetDlgItem(dialog, IDC_MONITORS), column, scale(ColumnWidths[column],dpi));
    InvalidateRect(dialog, nullptr, TRUE);
}
void App::scrollSettings(HWND dialog, bool horizontal, unsigned action, int position) {
    const int bar = horizontal ? SB_HORZ : SB_VERT;
    SCROLLINFO info{}; info.cbSize = sizeof(info); info.fMask = SIF_ALL; GetScrollInfo(dialog, bar, &info);
    int next = info.nPos;
    const int line = scale(24, GetDpiForWindow(dialog));
    switch (action) {
    case SB_LINEUP: next -= line; break;
    case SB_LINEDOWN: next += line; break;
    case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
    case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: next = info.nTrackPos; break;
    case SB_TOP: next = 0; break;
    case SB_BOTTOM: next = info.nMax; break;
    default: next += position; break;
    }
    const int maximum = std::max(0, info.nMax - static_cast<int>(info.nPage) + 1);
    next = std::clamp(next, 0, maximum);
    if (horizontal) scrollX_ = next; else scrollY_ = next;
    layoutSettings(dialog);
}
void App::revealControl(HWND control) {
    if (!dialog_ || !control || !IsChild(dialog_, control)) return;
    RECT r{}, client{}; GetWindowRect(control, &r);
    MapWindowPoints(HWND_DESKTOP, dialog_, reinterpret_cast<POINT*>(&r), 2); GetClientRect(dialog_, &client);
    int dx = r.left < 0 ? r.left : r.right > client.right ? r.right-client.right : 0;
    int dy = r.top < 0 ? r.top : r.bottom > client.bottom ? r.bottom-client.bottom : 0;
    if (dx) scrollSettings(dialog_, true, 99, dx);
    if (dy) scrollSettings(dialog_, false, 99, dy);
}
void App::fillDisplayList(HWND dialog) {
    std::vector<Identity> identities;
    for (const auto& d : displays_) identities.push_back({d.path, d.selectable});
    dialogList_ = selectionList(draftPath_, identities, devicePathEqual);
    fillingList_ = true;
    HWND list = GetDlgItem(dialog, IDC_MONITORS);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    struct Resume {
        HWND list; bool& filling;
        ~Resume() { filling = false; SendMessageW(list, WM_SETREDRAW, TRUE, 0); InvalidateRect(list,nullptr,TRUE); }
    } resume{list, fillingList_};
    ListView_DeleteAllItems(list);
    for (std::size_t i = 0; i < dialogList_.rows.size(); ++i) {
        const auto& row = dialogList_.rows[i];
        std::array<std::wstring,7> values{};
        if (row.display) {
            const auto& d = displays_[*row.display];
            values = {std::to_wstring(*row.display+1), d.name,
                std::to_wstring(d.bounds.width())+L" x "+std::to_wstring(d.bounds.height()),
                d.hz ? std::to_wstring(d.hz)+L" Hz" : L"Unknown", d.primary ? L"Yes" : L"No", d.device,
                row.selectable ? (devicePathEqual(d.path,settings_.monitorPath) ? L"Saved OLED" : L"Available") : L"Unavailable / ambiguous"};
        } else if (row.path.empty()) values[1] = L"No display selected";
        else { values[1] = L"Keep pending selection"; values[6] = L"Unavailable - retained"; }
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = values[0].data();
        if (SendMessageW(list,LVM_INSERTITEMW,0,reinterpret_cast<LPARAM>(&item)) == -1)
            throw std::runtime_error("Could not create display row");
        for (int column=1; column<7; ++column) cell(list,static_cast<int>(i),column,values[column]);
    }
    const int selected = static_cast<int>(dialogList_.selected);
    ListView_SetItemState(list,selected,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list,selected,FALSE);
    updateSettingsStatus();
}
void App::updateSettingsStatus() {
    if (!dialog_) return;
    const auto status = L"Status: " + statusText() + L"  |  Saved delay: " + std::to_wstring(settings_.delaySeconds) + L" s";
    SetDlgItemTextW(dialog_,IDC_STATUS,status.c_str());
    const auto row = dialogList_.selected;
    const bool missing = row < dialogList_.rows.size() && !dialogList_.rows[row].selectable;
    SetDlgItemTextW(dialog_,IDC_FOOTER_HELP,missing ? L"Pending OLED unavailable. Save keeps its identity; no blackout." : L"Blackout pauses in Settings. Changes apply when saved.");
    const bool wanted = IsDlgButtonChecked(dialog_,IDC_HOTKEY) == BST_CHECKED;
    SetDlgItemTextW(dialog_,IDC_HOTKEY_STATUS,!wanted ? L"Off after Save" : hotkeyRegistered_ ? L"Shortcut available" :
        settings_.hotkeyEnabled ? L"Shortcut unavailable - use tray toggle" : L"Will register when saved");
}
void App::updateStartupStatus(HWND dialog) {
    const bool checked = IsDlgButtonChecked(dialog,IDC_STARTUP) == BST_CHECKED;
    const wchar_t* text{};
    switch (startupInDialog_.state) {
    case StartupState::Disabled: text = checked ? L"Save will enable login startup for this copy." : L"Off. This app will not start at login."; break;
    case StartupState::Current: text = checked ? L"On for this copy of OLED Blackout." : L"Save will remove this app's startup entry."; break;
    case StartupState::Stale: text = checked ? L"Old/different path detected. Save repairs it." : L"Old/different path detected. Save removes it."; break;
    case StartupState::Unreadable: text = checked ? L"Entry unreadable. Save attempts to replace it." : L"Entry unreadable. Save attempts to remove it."; break;
    }
    SetDlgItemTextW(dialog,IDC_STARTUP_STATUS,text);
}
void App::saveDialog(HWND dialog) {
    BOOL valid{}; const unsigned delay = GetDlgItemInt(dialog,IDC_DELAY,&valid,FALSE);
    if (!valid || delay < 5 || delay > 3600) {
        MessageBoxW(dialog,L"Enter a whole number from 5 to 3600 seconds.",L"Check blackout delay",MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog,IDC_DELAY)); revealControl(GetDlgItem(dialog,IDC_DELAY)); return;
    }
    Settings candidate = settings_; candidate.monitorPath = draftPath_; candidate.delaySeconds = delay;
    candidate.enabled = IsDlgButtonChecked(dialog,IDC_ENABLED) == BST_CHECKED;
    candidate.hotkeyEnabled = IsDlgButtonChecked(dialog,IDC_HOTKEY) == BST_CHECKED;
    const bool startup = IsDlgButtonChecked(dialog,IDC_STARTUP) == BST_CHECKED;
    if (!candidate.enabled) { settings_.enabled = false; reset(); updateTray(); }
    // applySettings always resolves the captured identity against a fresh snapshot.
    store_.save(candidate);
    try { currentUserStartup().set(startup); } // Always reconcile: stale unchecked must be removed.
    catch (const std::exception& error) {
        applySettings(candidate,false); startupInDialog_ = currentUserStartup().read(); updateStartupStatus(dialog);
        reportError(error,dialog); return;
    }
    applySettings(std::move(candidate),false);
    if (settings_.hotkeyEnabled && !hotkeyRegistered_)
        MessageBoxW(dialog,L"Settings saved. Ctrl+Alt+B is unavailable. Use the tray toggle or turn the hotkey off in Settings.",L"Shortcut unavailable",MB_OK | MB_ICONWARNING);
    EndDialog(dialog,IDOK);
}
INT_PTR App::dialogMessage(HWND dialog, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        dialog_ = dialog; draftPath_ = settings_.monitorPath;
        SendMessageW(dialog,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(icon_));
        HWND list = GetDlgItem(dialog,IDC_MONITORS);
        if (!SetWindowSubclass(list, monitorListProc, 1, 0)) throw std::runtime_error("Could not enable monitor keyboard navigation");
        ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        for (int i=0; i<7; ++i) {
            LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t*>(ColumnNames[i]); column.cx = ColumnWidths[i];
            if (SendMessageW(list,LVM_INSERTCOLUMNW,static_cast<WPARAM>(i),reinterpret_cast<LPARAM>(&column)) == -1)
                throw std::runtime_error("Could not create monitor columns");
        }
        SendDlgItemMessageW(dialog,IDC_DELAY,EM_SETLIMITTEXT,4,0);
        SetDlgItemInt(dialog,IDC_DELAY,settings_.delaySeconds,FALSE);
        int choice = 4;
        for (int i=0;i<4;++i) {
            const auto label = std::to_wstring(Presets[i])+L" seconds";
            SendDlgItemMessageW(dialog,IDC_PRESET,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            if (settings_.delaySeconds == Presets[i]) choice=i;
        }
        SendDlgItemMessageW(dialog,IDC_PRESET,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Custom"));
        SendDlgItemMessageW(dialog,IDC_PRESET,CB_SETCURSEL,choice,0);
        CheckDlgButton(dialog,IDC_ENABLED,settings_.enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog,IDC_HOTKEY,settings_.hotkeyEnabled ? BST_CHECKED : BST_UNCHECKED);
        startupInDialog_ = currentUserStartup().read();
        CheckDlgButton(dialog,IDC_STARTUP,startupInDialog_.state == StartupState::Current ? BST_CHECKED : BST_UNCHECKED);
        if (startupInDialog_.error) log_.write(windowsErrorText("Read login startup",startupInDialog_.error));
        updateStartupStatus(dialog); fillDisplayList(dialog); initializeSettingsLayout(dialog); return TRUE;
    }
    if (msg == NavigateMessage) {
        const bool forward = wp == VK_DOWN || wp == VK_END || wp == VK_NEXT;
        auto row = dialogList_.selected;
        const auto count = wp == VK_HOME || wp == VK_END ? dialogList_.rows.size() :
            wp == VK_PRIOR || wp == VK_NEXT ? static_cast<std::size_t>(std::max(1,ListView_GetCountPerPage(GetDlgItem(dialog,IDC_MONITORS)))) : 1;
        for (std::size_t i=0; i<count; ++i) {
            const auto next = nextSelectable(dialogList_,row,forward);
            if (next == row) break;
            row = next;
        }
        if (row < dialogList_.rows.size() && dialogList_.rows[row].selectable) {
            HWND list=GetDlgItem(dialog,IDC_MONITORS);
            ListView_SetItemState(list,static_cast<int>(row),LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list,static_cast<int>(row),FALSE);
        }
        return TRUE;
    }
    if (msg == WM_NOTIFY && lp) {
        const auto* header = reinterpret_cast<const NMHDR*>(lp);
        if (header->idFrom == IDC_MONITORS) {
            if (header->code == NM_SETFOCUS) revealControl(header->hwndFrom);
            if (header->code == LVN_ITEMCHANGING && !fillingList_) {
                const auto* change = reinterpret_cast<const NMLISTVIEW*>(lp);
                if ((change->uChanged & LVIF_STATE) && (change->uNewState & LVIS_SELECTED) && change->iItem >= 0 &&
                    static_cast<std::size_t>(change->iItem) < dialogList_.rows.size() && !dialogList_.rows[change->iItem].selectable) {
                    SetWindowLongPtrW(dialog,DWLP_MSGRESULT,TRUE); return TRUE;
                }
            }
            if (header->code == LVN_ITEMCHANGED && !fillingList_) {
                const auto* change = reinterpret_cast<const NMLISTVIEW*>(lp);
                if ((change->uChanged & LVIF_STATE) && (change->uNewState & LVIS_SELECTED) && change->iItem >= 0 &&
                    static_cast<std::size_t>(change->iItem) < dialogList_.rows.size()) {
                    dialogList_.selected=static_cast<std::size_t>(change->iItem);
                    draftPath_=dialogList_.rows[dialogList_.selected].path; updateSettingsStatus();
                }
            }
        }
    }
    if (msg == WM_COMMAND) {
        const auto id=LOWORD(wp), code=HIWORD(wp);
        if (code == EN_SETFOCUS || code == BN_SETFOCUS || code == CBN_SETFOCUS) revealControl(reinterpret_cast<HWND>(lp));
        switch (id) {
        case IDOK: saveDialog(dialog); return TRUE;
        case IDCANCEL: EndDialog(dialog,IDCANCEL); return TRUE;
        case IDC_IDENTIFY: identify(); return TRUE;
        case IDC_REFRESH: refreshDisplays(); return TRUE;
        case IDC_STARTUP: updateStartupStatus(dialog); return TRUE;
        case IDC_HOTKEY: updateSettingsStatus(); return TRUE;
        case IDC_PRESET:
            if (code == CBN_SELCHANGE) {
                const auto selected=SendDlgItemMessageW(dialog,IDC_PRESET,CB_GETCURSEL,0,0);
                if (selected >= 0 && selected < 4) {
                    updatingPreset_=true; SetDlgItemInt(dialog,IDC_DELAY,Presets[selected],FALSE); updatingPreset_=false;
                } else SetFocus(GetDlgItem(dialog,IDC_DELAY));
            }
            return TRUE;
        case IDC_DELAY:
            if (code == EN_CHANGE && !updatingPreset_) {
                BOOL valid{}; const auto delay=GetDlgItemInt(dialog,IDC_DELAY,&valid,FALSE); int selected=4;
                if (valid) for (int i=0;i<4;++i) if (delay==Presets[i]) selected=i;
                SendDlgItemMessageW(dialog,IDC_PRESET,CB_SETCURSEL,selected,0);
            }
            return TRUE;
        }
    }
    if (msg == WM_VSCROLL && !lp) { scrollSettings(dialog,false,LOWORD(wp),0); return TRUE; }
    if (msg == WM_HSCROLL && !lp) { scrollSettings(dialog,true,LOWORD(wp),0); return TRUE; }
    if (msg == WM_MOUSEWHEEL) {
        scrollSettings(dialog,false,99,-MulDiv(static_cast<short>(HIWORD(wp)),scale(60,GetDpiForWindow(dialog)),WHEEL_DELTA)); return TRUE;
    }
    if (msg == WM_SIZE) { layoutSettings(dialog); return TRUE; }
    if (msg == WM_DPICHANGED) { PostMessageW(dialog,LayoutMessage,0,0); return FALSE; } // PMv2 dialog manager scales fonts first.
    if (msg == LayoutMessage) { layoutSettings(dialog); updateHeadingFont(dialog); return TRUE; }
    if (msg == WM_SETTINGCHANGE || msg == WM_SYSCOLORCHANGE) {
        HWND list=GetDlgItem(dialog,IDC_MONITORS);
        ListView_SetBkColor(list,GetSysColor(COLOR_WINDOW)); ListView_SetTextBkColor(list,GetSysColor(COLOR_WINDOW));
        ListView_SetTextColor(list,GetSysColor(COLOR_WINDOWTEXT)); InvalidateRect(dialog,nullptr,TRUE);
    }
    if (msg == WM_CLOSE) { EndDialog(dialog,IDCANCEL); return TRUE; }
    return FALSE;
}
} // namespace oled
