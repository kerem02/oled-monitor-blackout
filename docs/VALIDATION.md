# Validation — OLED Blackout 2.1.0

This document separates executed checks from planned coverage. A successful build or unit test is not presented as proof of every physical display, accessibility or gaming scenario.

## Executed checks

### Native Windows — 2026-09-16

The supplied 2.1.0 pre-release package was run on a native Windows dual-monitor system.

| Check | Result |
| --- | --- |
| `verification\oled_core_tests.exe` | PASS — 96,613 assertions |
| `verification\oled_platform_tests.exe` | PASS — 48 assertions |
| Application launch and native Settings UI | PASS — user-confirmed |
| Two-display discovery and selection | PASS — 2560 x 1440 displays at reported 360 Hz and 165 Hz |
| Functional blackout behavior | PASS — user-confirmed smoke test |

The exact Windows build, GPU/driver, DPI scale, HDR state and detailed timing measurements were not recorded. The captured Settings UI is available at [`assets/settings.png`](assets/settings.png).

### Independent portable checks — 2026-09-16

The final portable core source was built and run on Linux in both normal and AddressSanitizer/UndefinedBehaviorSanitizer configurations. The same 96,613 assertions passed. The final pre-release PE was also checked as x64 GUI, for system-only imported DLLs and for ASLR, NX and high-entropy address-space flags.

### Tagged release CI

Every `v*.*.*` tag runs the Windows MSVC Release build and both native test programs before the workflow can package and publish the release. The regular build workflow also runs portable sanitizer tests on Ubuntu. The release ZIP is therefore produced from the same tagged commit that passed CI; binaries are not committed to the repository.

## Test coverage

Portable tests cover geometry boundaries and extreme signed coordinates, state transitions and monotonic time edges, identity reorder/ambiguity, configuration recovery, startup classification, unsaved selection refresh, keyboard navigation, polling/activation policies and randomized safety properties.

Windows platform tests cover persistence and recovery, isolated startup-registry behavior, Unicode identity comparison, existing-instance activation validation and log rotation.

## Remaining manual coverage

The broader matrix remains in [`MANUAL-QA.md`](MANUAL-QA.md), including:

- Windows 10 and additional Windows 11/GPU combinations;
- negative-coordinate, portrait, mixed-DPI, HDR and 3+ display layouts;
- hotplug, sleep/wake, GPU-driver reset and Explorer recovery;
- accessibility/high-contrast/screen-reader checks;
- resource measurements and long lifecycle/leak observation;
- title-specific borderless/fullscreen and anti-cheat compatibility checks.

No universal game, anti-cheat, monitor or burn-in-prevention certification is claimed.
