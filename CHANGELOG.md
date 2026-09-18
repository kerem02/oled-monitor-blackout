# Changelog

## 2.1.2 — 2026-09-18

Maintenance release for a second, log-confirmed intermittent blackout dismissal source.

- Fixed active blackout being dismissed by unrelated Windows Plug and Play
  device notifications (`DBT_DEVNODES_CHANGED`). Display safety remains covered
  by `WM_DISPLAYCHANGE`, continuous selected-monitor health checks and reconnect
  retries.
- Updated repository wording after the 2.1.1 release and removed stale release-candidate language.
- Release scripts and CI artifact names now read the project version from CMake instead of duplicating it manually.

## 2.1.1 — 2026-09-17

Maintenance release for an intermittent blackout dismissal observed during normal use.

- Ignore broad `WM_SETTINGCHANGE` broadcasts that do not establish a display-topology change.
- Treat repeated on/dimmed display-power notifications as steady state; reset blackout only on an actual off/on transition.
- Handle overlay `WM_DPICHANGED` locally instead of rebuilding topology and hiding the overlay.
- Log every overlay dismissal with its exact reason to make any future recurrence diagnosable.
- Add display-power transition regression coverage; 96,621 core assertions and 48 Windows platform assertions pass in CI.
- Confirm the fix on the affected dual-monitor system: blackout remains active while the pointer stays away.

## 2.1.0 — 2026-09-16

First stable native C++17/Win32 release. It replaces the AutoHotkey implementation with a standalone, unsigned Windows x64 application.

- A second normal launch requests Settings in the existing session-local instance using a bounded, validated controller-window protocol. Duplicate login launches stay quiet.
- Login startup distinguishes off, this executable, stale path, and unreadable value. Save removes an unchecked stale entry or repairs it to the current quoted path.
- Failed corrupt/future-settings backup no longer aborts safe recovery; no display is selected and the failure is reported.
- Windows errors include system text and their numeric code; Unicode messages are preserved.
- Settings gains a native seven-column monitor list, retained unsaved selection, unavailable-row restrictions, delay presets/custom input, grouped controls, header/status, scrollable DPI layout and keyboard navigation.
- Unicode device identities use Windows ordinal case comparison; impossible signed rectangle extents fail open.
- Topology generation guards protect pre-show revalidation, logoff pauses blackout, and tray loss pauses blackout while retrying recovery.
- Disable/reset/session paths also remove Identify cards. Tray status is captured before opening the menu and coordinates retain their full signed range.
- Extended portable and isolated Windows tests; resource diagnostics record USER/GDI objects and peak handles/private bytes.
- Native Windows verification completed: 96,613 core assertions and 48 platform assertions passed, followed by a successful dual-monitor functional smoke test.
- Added a tag-driven GitHub Actions release workflow that builds and tests the exact source before packaging it.
- Removed legacy scripts, pre-release handoff material, generated binaries and obsolete evidence from the current source tree.

No new runtime framework, elevation, network access, input/graphics hooks, synthetic input or game-process access. No fabricated native QA results or screenshots.

## 2.0.0 — Native rewrite

- Replaced AutoHotkey runtime with portable native x64 C++17/Win32.
- Added exact device-path selection, fail-open display handling, atomic settings, bounded logs, native tray/Settings and core/platform tests.
- The historical AHK implementation remains available in the repository's v1.0.0 tag; it is no longer shipped in the current source tree.
