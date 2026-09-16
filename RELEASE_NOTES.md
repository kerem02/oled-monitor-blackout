# OLED Blackout 2.1.0

OLED Blackout is now a standalone native Windows x64 application. AutoHotkey is no longer required.

## Highlights

- Covers only the selected OLED with a non-activating, click-through solid-black window.
- Uses stable display device paths and fails open if the target is missing or ambiguous.
- Handles display changes, sleep/wake, DPI changes and Explorer tray recovery.
- Includes a polished native Settings UI, Identify Monitors, custom delay, optional hotkey and Start with Windows.
- Runs without administrator access, injection, input hooks, synthetic input, game-process access, drivers, telemetry or network use.

## Verification

- 96,613 core assertions passed on native Windows.
- 48 Windows platform assertions passed.
- Dual-monitor functional smoke test passed.
- The release workflow rebuilds and reruns both suites from this exact tag before publishing the ZIP.

## Installation

Download `OLED-Blackout-2.1.0-win-x64.zip`, extract it, run `OLEDBlackout.exe`, then identify and select the OLED in Settings.

This release is unsigned and Windows may show an unknown-publisher warning. The `.sha256` asset verifies the downloaded ZIP. No universal anti-cheat compatibility or burn-in-prevention guarantee is made.
