#Requires AutoHotkey v2.0
#SingleInstance Force

CoordMode "Mouse", "Screen"

configFile := A_ScriptDir "\settings.ini"

global oledMonitor := 1
global blackoutDelay := 30
global enabled := true
global checkMs := 500

global monLeft := 0
global monTop := 0
global monRight := 0
global monBottom := 0
global monW := 0
global monH := 0

global blackGui := 0
global overlayVisible := false
global lastAwayTick := A_TickCount

global delayMenu := 0
global monitorMenu := 0
global monitorFriendlyNames := Map()

LoadSettings()
RefreshMonitorFriendlyNames()
EnsureValidMonitorSelection()
BuildOverlay()
BuildTrayMenu()
UpdateTrayChecks()

SetTimer CheckOLEDState, checkMs

^!b::ToggleEnabled()

; =========================================================
; Startup / Settings
; =========================================================

LoadSettings() {
    global configFile, oledMonitor, blackoutDelay, enabled

    if FileExist(configFile) {
        try oledMonitor := Integer(IniRead(configFile, "Settings", "OledMonitor", "0"))
        catch
            oledMonitor := 0

        try blackoutDelay := Integer(IniRead(configFile, "Settings", "DelaySeconds", "30"))
        catch
            blackoutDelay := 30

        try enabled := IniRead(configFile, "Settings", "Enabled", "1") = "1"
        catch
            enabled := true
    } else {
        oledMonitor := 0
        blackoutDelay := 30
        enabled := true
    }
}

SaveSettings() {
    global configFile, oledMonitor, blackoutDelay, enabled
    IniWrite(oledMonitor, configFile, "Settings", "OledMonitor")
    IniWrite(blackoutDelay, configFile, "Settings", "DelaySeconds")
    IniWrite(enabled ? 1 : 0, configFile, "Settings", "Enabled")
}

EnsureValidMonitorSelection() {
    global oledMonitor

    count := MonitorGetCount()
    if (count < 1) {
        MsgBox "No monitors detected.", "OLED Blackout", "Icon!"
        ExitApp
    }

    if (oledMonitor < 1 || oledMonitor > count) {
        selected := ShowMonitorSelectionGui()
        if !selected {
            MsgBox "No monitor selected. Exiting script.", "OLED Blackout", "Icon!"
            ExitApp
        }

        oledMonitor := selected
        SaveSettings()
    }
}

; =========================================================
; Monitor Friendly Names
; =========================================================

RefreshMonitorFriendlyNames() {
    global monitorFriendlyNames
    monitorFriendlyNames := GetFriendlyMonitorNames()
}

GetFriendlyMonitorNames() {
    result := Map()
    tempNames := []

    try {
        svc := ComObject("WbemScripting.SWbemLocator").ConnectServer(".", "root\wmi")
        col := svc.ExecQuery("SELECT * FROM WmiMonitorID WHERE Active = True")

        for item in col {
            name := DecodeWmiString(item.UserFriendlyName)
            manufacturer := DecodeWmiString(item.ManufacturerName)

            if (name = "")
                name := "Unknown display"

            if (manufacturer != "" && !InStr(name, manufacturer))
                fullName := manufacturer " " name
            else
                fullName := name

            tempNames.Push(fullName)
        }
    } catch {
        ; WMI lookup is optional. Fall back below if unavailable.
    }

    count := MonitorGetCount()
    Loop count {
        idx := A_Index
        if (idx <= tempNames.Length && tempNames[idx] != "")
            result[idx] := tempNames[idx]
        else
            result[idx] := "Monitor " idx
    }

    return result
}

DecodeWmiString(arr) {
    s := ""

    try {
        for code in arr {
            if (code = 0)
                continue
            s .= Chr(code)
        }
    } catch {
        return ""
    }

    return Trim(s)
}

GetMonitorLabel(idx) {
    global monitorFriendlyNames

    MonitorGet(idx, &left, &top, &right, &bottom)
    width := right - left
    height := bottom - top
    name := monitorFriendlyNames.Has(idx) ? monitorFriendlyNames[idx] : ("Monitor " idx)

    return "Monitor " idx " — " name " — " width "x" height " — (" left "," top " to " right "," bottom ")"
}

GetShortMonitorLabel(idx) {
    global monitorFriendlyNames

    MonitorGet(idx, &left, &top, &right, &bottom)
    width := right - left
    height := bottom - top
    name := monitorFriendlyNames.Has(idx) ? monitorFriendlyNames[idx] : ("Monitor " idx)

    return "Monitor " idx " — " name " — " width "x" height
}

; =========================================================
; Overlay
; =========================================================

BuildOverlay() {
    global oledMonitor
    global monLeft, monTop, monRight, monBottom, monW, monH
    global blackGui, overlayVisible

    if (oledMonitor > MonitorGetCount()) {
        MsgBox "Invalid monitor index. Detected monitors: " MonitorGetCount(), "OLED Blackout", "Icon!"
        ExitApp
    }

    if blackGui {
        try blackGui.Destroy()
        blackGui := 0
    }

    MonitorGet(oledMonitor, &monLeft, &monTop, &monRight, &monBottom)
    monW := monRight - monLeft
    monH := monBottom - monTop

    blackGui := Gui("-Caption +ToolWindow +AlwaysOnTop")
    blackGui.BackColor := "000000"
    blackGui.MarginX := 0
    blackGui.MarginY := 0
    blackGui.Show("NA x" monLeft " y" monTop " w" monW " h" monH)

    ; Click-through + layered window
    WinSetExStyle("+0x20", "ahk_id " blackGui.Hwnd)     ; WS_EX_TRANSPARENT
    WinSetExStyle("+0x80000", "ahk_id " blackGui.Hwnd)  ; WS_EX_LAYERED

    blackGui.Hide()
    overlayVisible := false
}

ShowOverlay() {
    global blackGui, monLeft, monTop, monW, monH, overlayVisible

    if !blackGui
        return

    try blackGui.Show("NA x" monLeft " y" monTop " w" monW " h" monH)
    overlayVisible := true
}

HideOverlay() {
    global blackGui, overlayVisible

    if !blackGui
        return

    try blackGui.Hide()
    overlayVisible := false
}

RebuildOverlay(*) {
    global lastAwayTick

    HideOverlay()
    RefreshMonitorFriendlyNames()
    BuildOverlay()
    lastAwayTick := A_TickCount
    RebuildMonitorMenu()
    UpdateTrayChecks()
}

; =========================================================
; Core Logic
; =========================================================

CheckOLEDState() {
    global oledMonitor, lastAwayTick, blackoutDelay, overlayVisible, enabled

    if !enabled {
        if overlayVisible
            HideOverlay()
        return
    }

    MouseGetPos &mx, &my
    currentMonitor := GetMonitorFromPoint(mx, my)

    if (currentMonitor = oledMonitor) {
        lastAwayTick := A_TickCount
        if overlayVisible
            HideOverlay()
        return
    }

    elapsed := (A_TickCount - lastAwayTick) / 1000
    if (elapsed >= blackoutDelay) && !overlayVisible
        ShowOverlay()
}

GetMonitorFromPoint(x, y) {
    count := MonitorGetCount()

    Loop count {
        idx := A_Index
        MonitorGet(idx, &l, &t, &r, &b)
        if (x >= l && x < r && y >= t && y < b)
            return idx
    }

    return 0
}

ToggleEnabled(*) {
    global enabled, lastAwayTick

    enabled := !enabled
    SaveSettings()

    if !enabled {
        HideOverlay()
        ShowTrayTip("OLED Blackout", "Disabled")
    } else {
        lastAwayTick := A_TickCount
        ShowTrayTip("OLED Blackout", "Enabled")
    }

    UpdateTrayChecks()
}

SetDelay(seconds) {
    global blackoutDelay, lastAwayTick

    blackoutDelay := seconds
    lastAwayTick := A_TickCount
    SaveSettings()
    UpdateTrayChecks()
    ShowTrayTip("OLED Blackout", "Delay set to " seconds " seconds")
}

; =========================================================
; Tray Menu
; =========================================================

BuildTrayMenu() {
    global delayMenu, monitorMenu

    A_TrayMenu.Delete()

    A_TrayMenu.Add("Enable / Disable`tCtrl+Alt+B", ToggleEnabled)
    A_TrayMenu.Add()

    delayMenu := Menu()
    delayMenu.Add("10 seconds", (*) => SetDelay(10))
    delayMenu.Add("30 seconds", (*) => SetDelay(30))
    delayMenu.Add("60 seconds", (*) => SetDelay(60))
    delayMenu.Add("120 seconds", (*) => SetDelay(120))
    A_TrayMenu.Add("Blackout Delay", delayMenu)

    monitorMenu := Menu()
    RebuildMonitorMenu()
    A_TrayMenu.Add("OLED Monitor", monitorMenu)

    A_TrayMenu.Add("Choose OLED Monitor...", (*) => ChooseMonitorFromGui())
    A_TrayMenu.Add("Identify Monitors", (*) => IdentifyMonitors())
    A_TrayMenu.Add("Rebuild Overlay", RebuildOverlay)
    A_TrayMenu.Add()
    A_TrayMenu.Add("Exit", (*) => ExitApp())
}

RebuildMonitorMenu() {
    global monitorMenu

    monitorMenu.Delete()
    RefreshMonitorFriendlyNames()

    count := MonitorGetCount()
    Loop count {
        idx := A_Index
        label := GetShortMonitorLabel(idx)
        monitorMenu.Add(label, SelectMonitor.Bind(idx))
    }
}

UpdateTrayChecks() {
    global delayMenu, monitorMenu, blackoutDelay, oledMonitor, enabled

    ; Main enabled state
    try {
        if enabled
            A_TrayMenu.Check("Enable / Disable`tCtrl+Alt+B")
        else
            A_TrayMenu.Uncheck("Enable / Disable`tCtrl+Alt+B")
    }

    ; Delay submenu
    for label in ["10 seconds", "30 seconds", "60 seconds", "120 seconds"] {
        try delayMenu.Uncheck(label)
    }

    switch blackoutDelay {
        case 10:
            try delayMenu.Check("10 seconds")
        case 30:
            try delayMenu.Check("30 seconds")
        case 60:
            try delayMenu.Check("60 seconds")
        case 120:
            try delayMenu.Check("120 seconds")
    }

    ; Monitor submenu
    RefreshMonitorFriendlyNames()
    count := MonitorGetCount()

    Loop count {
        idx := A_Index
        label := GetShortMonitorLabel(idx)
        try monitorMenu.Uncheck(label)
        if (idx = oledMonitor)
            try monitorMenu.Check(label)
    }
}

SelectMonitor(idx, *) {
    global oledMonitor, lastAwayTick

    oledMonitor := idx
    SaveSettings()

    HideOverlay()
    BuildOverlay()
    lastAwayTick := A_TickCount

    RebuildMonitorMenu()
    UpdateTrayChecks()
    ShowTrayTip("OLED Blackout", "OLED monitor set to Monitor " idx)
}

ChooseMonitorFromGui() {
    global oledMonitor, lastAwayTick

    selected := ShowMonitorSelectionGui()
    if selected {
        oledMonitor := selected
        SaveSettings()

        HideOverlay()
        BuildOverlay()
        lastAwayTick := A_TickCount

        RebuildMonitorMenu()
        UpdateTrayChecks()
        ShowTrayTip("OLED Blackout", "OLED monitor set to Monitor " selected)
    }
}

; =========================================================
; Selection GUI
; =========================================================

ShowMonitorSelectionGui() {
    RefreshMonitorFriendlyNames()

    choices := []
    count := MonitorGetCount()

    Loop count
        choices.Push(GetMonitorLabel(A_Index))

    guiSel := Gui("+AlwaysOnTop", "Select OLED Monitor")
    guiSel.SetFont("s10")
    guiSel.MarginX := 12
    guiSel.MarginY := 12

    guiSel.AddText("w760", "Select which monitor should be treated as the OLED display.")
    guiSel.AddText("w760 cGray", "Tip: click 'Identify Monitors' to show large numbers on each screen.")

    lb := guiSel.AddListBox("w760 r8 Choose1", choices)

    btnIdentify := guiSel.AddButton("xm w150", "Identify Monitors")
    btnRefresh  := guiSel.AddButton("x+10 w110", "Refresh List")
    btnOK       := guiSel.AddButton("x+220 w100 Default", "OK")
    btnCancel   := guiSel.AddButton("x+10 w100", "Cancel")

    selectedMonitor := 0

    btnIdentify.OnEvent("Click", (*) => IdentifyMonitors())
    btnRefresh.OnEvent("Click", (*) => RefreshSelectionList(lb))

    btnOK.OnEvent("Click", (*) => (
        selectedMonitor := lb.Value,
        guiSel.Destroy()
    ))

    btnCancel.OnEvent("Click", (*) => (
        selectedMonitor := 0,
        guiSel.Destroy()
    ))

    guiSel.OnEvent("Close", (*) => (
        selectedMonitor := 0,
        guiSel.Destroy()
    ))

    guiSel.Show("AutoSize Center")
    WinWaitClose("ahk_id " guiSel.Hwnd)

    return selectedMonitor
}

RefreshSelectionList(lb) {
    RefreshMonitorFriendlyNames()
    lb.Delete()

    count := MonitorGetCount()
    Loop count
        lb.Add([GetMonitorLabel(A_Index)])

    lb.Choose(1)
}

; =========================================================
; Identify Monitors
; =========================================================

IdentifyMonitors() {
    guis := []
    count := MonitorGetCount()

    Loop count {
        idx := A_Index
        MonitorGet(idx, &left, &top, &right, &bottom)
        width := right - left
        height := bottom - top

        g := Gui("-Caption +AlwaysOnTop +ToolWindow +Border")
        g.BackColor := "Black"
        g.SetFont("s72 Bold cWhite", "Segoe UI")
        g.AddText("Center w" width " h" height, idx)
        g.Show("NA x" left " y" top " w" width " h" height)
        WinSetTransparent(185, g)

        guis.Push(g)
    }

    SetTimer(() => DestroyIdentifyGuis(guis), -1500)
}

DestroyIdentifyGuis(guis) {
    for g in guis {
        try g.Destroy()
    }
}

; =========================================================
; Helper
; =========================================================

ShowTrayTip(title, text) {
    try TrayTip(text, title, 1)
    catch {
    }
}
