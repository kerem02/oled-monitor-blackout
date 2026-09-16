# Manual QA — 2.1.0

Unchecked items are still open coverage, not implied passes. Record OS build, GPU/driver, connection/dock, monitor model, resolution, refresh, DPI, HDR and EXE SHA-256 for each future run.

## Recorded native result

On 2026-09-16, the 2.1.0 pre-release build was exercised on a native Windows dual-monitor system. Both supplied verification programs passed (96,613 core assertions and 48 platform assertions), and the user reported successful application launch, monitor discovery/selection and blackout behavior. The Settings window displayed 2560 x 1440 monitors at 360 Hz and 165 Hz without visible layout clipping; the captured UI is in `assets/settings.png`.

The exact Windows build number, GPU/driver, DPI values, HDR state, game/anti-cheat session and resource measurements were not recorded. Accordingly, only the checks explicitly described above are treated as passed; the remaining matrix below stays open.

## Common procedure

1. Exit older OLED Blackout versions and other blackout tools. Run without elevation. Choose the OLED using Identify.
2. Set delay to 5 seconds. Keep pointer on OLED for 10 seconds: no blackout.
3. Move to the other display: only the selected display must turn black after the full delay. Verify taskbar/borders/corners.
4. Keep a text editor/game focused on the other display. It must remain focused and receive keyboard input throughout show/hide.
5. Return the pointer and immediately click a harmless underlying control: blackout should clear promptly and must not intercept the click. Measure return-to-clear with high-speed video if needed, not a subjective "instant" claim.
6. Disable while blacked out with Ctrl+Alt+B; it must clear immediately. Re-enable: a fresh full delay.
7. Exit during blackout; the window must disappear. Repeat by ending the app in Task Manager. Test process hang separately; termination is the recovery mechanism for a hang.
8. Record pass/fail and any delay/coverage anomaly. Re-run failed conditions after a fix.

## Configuration matrix

| Check | Expected | Result |
| --- | --- | --- |
| Windows 10 1703+ x64 | Common procedure passes | Not run |
| Windows 11 x64 | Common procedure passes | Not run |
| Single monitor | Selectable; no blackout while pointer stays on it | Not run |
| Dual displays | Only selected OLED covered | Not run |
| Triple displays; primary in middle | Only selected OLED covered | Not run |
| Four or more displays | Labels/selection remain usable, no ordinal identity dependency | Not run |
| OLED left / negative X | Exact full bounds | Not run |
| OLED right | Exact full bounds | Not run |
| OLED above / negative Y | Exact full bounds | Not run |
| OLED below | Exact full bounds | Not run |
| Portrait + landscape | Correct rectangles after rotation | Not run |
| 1080p + 1440p + 4K | No uncovered strips or neighboring coverage | Not run |
| 100%, 125%, 150%, 175%, 200% DPI | Full pixel coverage; usable Settings on each DPI | Not run |
| Taskbar on OLED / on another display | Exact coverage with taskbar arrangement recorded | Not run |
| Mixed DPI; move Settings between screens | Dialog text/buttons scale, no clipping | Not run |
| Mirrored/duplicate desktop | Ambiguous source rejected; no physical-only blackout promise | Not run |
| Identical monitors with no serial | Reconfirm after connection changes; no model guessing | Not run |

## Display/session events

Perform both while waiting and while blacked out. Confirm a full new delay after recovery.

- [ ] Unplug selected OLED: hide; unavailable status; other monitors remain untouched.
- [ ] Reconnect same path: match automatically; never switch to another screen during settling.
- [ ] Move to another port/dock: either exact match or unavailable; manually reconfirm any changed identity.
- [ ] Disconnect a different monitor: rebuild geometry safely.
- [ ] Change primary display / rearrange / rotate / change resolution / refresh rate.
- [ ] Change DPI while running; check borders on all sides.
- [ ] Toggle HDR; verify black visually and with a meter if physical black-level claims are needed.
- [ ] Sleep/wake; monitor power-save and wake; lock/unlock; fast user switch.
- [ ] Restart GPU driver via the normal Windows shortcut or driver tools; no stale-screen blackout.
- [ ] Restart Windows Explorer: tray icon returns, hotkey still works.
- [ ] Remote desktop attach/detach: missing DisplayConfig identity must fail open.

## Games (real hardware required)

Use your actual titles and record game build and anti-cheat product/version. Never disable or bypass anti-cheat to test this app.

- [ ] Borderless game on non-OLED: no focus change, minimization or capture change.
- [ ] Exclusive-fullscreen game on non-OLED: same checks; OLED-only blackout.
- [ ] Alt+Tab away from and back to the game while OLED is blacked out.
- [ ] Captured/clipped cursor: app does not change capture; return only when the game allows it.
- [ ] Game on OLED: record coverage limitations; no exclusive-fullscreen guarantee.
- [ ] Immediate click after pointer crosses back: underlying application's click works.
- [ ] Other topmost/system notifications: document when coverage is interrupted.
- [ ] Record whether the game/anti-cheat accepts the app; this is title/version-specific evidence, not a universal guarantee.

## UX/persistence/lifecycle

- [ ] First launch prompts for selection without briefly blacking any screen.
- [ ] Cancel first Settings: tray remains available, no default-monitor blackout.
- [ ] Choose each OLED; quit/restart; match original path after enumeration order changes.
- [ ] Custom delays 5 and 3600; invalid input rejected.
- [ ] Missing/corrupt/future config: backup where applicable, safe defaults, no selected monitor.
- [ ] LocalAppData read-only/full disk: visible error; disable still removes active blackout.
- [ ] Hotkey conflict: warning visible; tray controls remain usable.
- [ ] Start with Windows: login launch, quoted path with spaces, no UAC; disable removes Run value.
- [ ] Move portable folder: stale state visible. Save unchecked removes only the app value; Save checked repairs to this copy. Test path with spaces/non-ASCII; unrelated Run values unchanged. Also test read/write access denied and Task Manager startup policy.
- [ ] Corrupt/future config with settings.recovered.ini unwritable: starts safely, no OLED selected, backup failure visible.
- [ ] Select B without saving, reorder/refresh/disconnect B: pending B stays selected or retained unavailable; reconnect resolves B; Cancel preserves previous saved A.
- [ ] Unicode device paths, duplicate identity/mirror/overlap rows: safe comparison and unavailable rows cannot be selected.
- [ ] Launch twice: one instance, existing Settings opens/restores/foregrounds or flashes if Windows denies foreground. Repeat with Settings already open, minimized, tray menu active and concurrent first launch.
- [ ] Duplicate --startup stays silent. Old 2.0 controller or unresponsive controller produces useful failure, no second overlay.
- [ ] Cancel logoff: blackout remains hidden until a fresh delay. Actual logoff removes both blackout and Identify windows.
- [ ] Open/close Settings and Identify repeatedly; no increasing GDI/USER handle trend.
- [ ] Identify while blacked out: readable cards, unchanged configured monitor/enabled flag, fresh countdown afterward.
- [ ] Keyboard: Tab/Shift+Tab logical traversal; Space checkboxes; arrows/Home/End/Page keys skip unsafe rows; Enter saves; Escape cancels; tray keyboard activation.
- [ ] DPI 100/125/150/175/200%, mixed-DPI move, resize/maximize, small work area: no clipped controls; scrollbar and focus reveal reach Save/Cancel; preset dropdown remains usable.
- [ ] High contrast on/off while dialog open: text/focus/selection visible and all state also expressed in words.
- [ ] Windows Narrator: monitor list and columns, combo/edit/checkbox names, status/error announcements; record any limitations.
- [ ] Long monitor names: columns scroll/label tips usable; focus rectangles visible.
- [ ] Exit/logout/shutdown cleanly; no stranded identify/blackout windows.

## Resource measurements

Run `scripts/measure-resources.ps1 -Seconds 60 -State OnOLED` after a short warm-up in each state: on OLED, waiting, blacked out, disabled, selected OLED absent. Record mean/peak working set, private bytes, CPU milliseconds and handle counts. Repeat at 1080p and 4K, and after 100 show/hide or Settings/Identify cycles. Use Resource Monitor/Process Monitor filtered to **OLEDBlackout.exe** to check unchanged-state disk writes. Do not inspect game processes.

Provisional goals, not measured claims: CPU below 0.1% of one logical processor over a quiet 60-second interval, no growing handle/private-memory trend, no steady-state app-file writes, usual cursor-return visibility latency below 100 ms. Use sustained samples; brief Task Manager spikes are not enough. Record DWM/GPU memory separately when assessing compositor cost. If a goal fails, investigate before promoting the release.


### Measurement worksheet (no results collected)

| State / operator label | Seconds | CPU ms / one-core % | Mean / peak working set | Mean / peak private bytes | Handles / USER / GDI first-last-peak | Result |
| --- | --- | --- | --- | --- | --- | --- |
| OnOLED | >=60 | — | — | — | — | Not measured |
| Waiting | >=60 | — | — | — | — | Not measured |
| BlackedOut | >=60 | — | — | — | — | Not measured |
| Disabled | >=60 | — | — | — | — | Not measured |
| Unavailable | >=60 | — | — | — | — | Not measured |
| LifecycleCycles (Settings/Identify/refresh) | >=60, repeat | — | — | — | — | Not measured |

Use `-State` to label the state you establish manually. For Waiting, set a delay longer than the entire sample; do not let it switch to BlackedOut midway. Keep focus/monitor conditions fixed for each sample. Export results, for example `... | Export-Csv measurements.csv -NoTypeInformation -Append`. Compare several steady samples before and after at least 100 lifecycle cycles; counts should return to a stable range. The external script opens a diagnostic handle to OLED Blackout only. It is not shipped inside or called by the runtime app.
