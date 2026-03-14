# OLED Blackout

A lightweight AutoHotkey utility for OLED multi-monitor setups.

It places a black, click-through overlay on a selected OLED monitor after the mouse leaves that display for a set amount of time. When the mouse returns, the overlay disappears instantly.

## Features

- Blackout only on the selected OLED monitor
- Adjustable delay: 10 / 30 / 60 / 120 seconds
- Toggle on/off with `Ctrl + Alt + B`
- Tray menu with monitor selection
- `Identify Monitors` helper
- Settings saved automatically in `settings.ini`
- Works from `.ahk` or compiled `.exe`

## How it works

- Mouse leaves the selected OLED monitor
- Countdown starts
- After the delay, the OLED is blacked out
- Mouse returns to the OLED
- Overlay disappears instantly

## Installation

### Option 1 — Source
- Install AutoHotkey v2
- Run `oled_blackout.ahk`

### Option 2 — Compiled EXE
- Download and run `oled_blackout.exe`
- No AutoHotkey installation required

## Startup with Windows

To launch automatically on login:

1. Press `Win + R`
2. Type `shell:startup`
3. Press Enter
4. Put a shortcut to `oled_blackout.exe` in that folder

Using the compiled `.exe` is the easiest option.

## Tray Menu

- **Enable / Disable** `Ctrl+Alt+B`
- **Blackout Delay**
- **OLED Monitor**
- **Choose OLED Monitor...**
- **Identify Monitors**
- **Rebuild Overlay**
- **Exit**

## Notes

- `Identify Monitors` is the most reliable way to choose the correct screen
- `Rebuild Overlay` helps if monitor layout, resolution, or display state changes
- This is a lightweight helper, not a replacement for built-in OLED protection features

## License

MIT
