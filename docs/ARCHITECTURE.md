# Architecture — 2.1.0

Priority: correctness, stability, game compatibility, resources, usability, then code size.

| Option | Strengths | Trade-offs |
| --- | --- | --- |
| C# / modern .NET | Strong managed safety; excellent maintainability and desktop UI support | Win32 interop still needed; conventional self-contained deployment is larger and has managed runtime startup/working-set overhead; NativeAOT adds framework/interoperability constraints |
| C++17 / Win32 (chosen) | Direct documented API access, small native deployment, no framework runtime, ordinary tray/dialog/message-loop behavior | Handle ownership, callback exception boundaries and Unicode conversions require explicit discipline; UI development is more manual |
| Rust + Windows APIs | Ownership and memory safety for most logic; native deployment | FFI remains unsafe; Win32 UI is still low-level; more binding/build dependencies for this tiny application |

All three can follow the same non-invasive architecture. Language alone does not establish anti-cheat compatibility. C++ was selected for straightforward native integration and a dependency-free portable release, with RAII, checked API results and a separately testable core compensating for its safety trade-offs. DDC/CI is not used: powering off can change topology or delay waking and is not necessary for the requested behavior.

## State ownership

`StateMachine::step` consumes an immutable input snapshot and produces `Disabled`, `Unavailable`, `Paused`, `OnDisplay`, `Waiting`, or `BlackedOut`. Only Waiting/BlackedOut retain a departure timestamp. Disable, cursor return/failure, UI pause, unavailable display and explicit reset discard it. Re-enable/reconnect starts a fresh delay. `GetTickCount64` avoids system-clock changes and 32-bit wrap. A backwards sample also resets safely.

All HWND operations, timer callbacks, configuration changes and discovery run on the UI message thread. A hidden top-level controller receives broadcasts; a message-only window would miss them. Callback exception boundaries never allow C++ exceptions to cross a Win32 ABI. Dialogs/menus pause blackout. Menu commands retain device paths so a topology update cannot turn an old menu index into another monitor.

The release does not use worker threads, hooks, multimedia/high-resolution timer changes, process discovery, graphics interception, remote communication or DDC writes.

## Monitor selection and geometry

1. Enumerate desktop regions with `EnumDisplayMonitors` and `GetMonitorInfo`.
2. Retrieve active DisplayConfig paths, retrying the buffer-sizing race up to five times.
3. Join by `DISPLAYCONFIG_SOURCE_DEVICE_NAME.viewGdiDeviceName`, not enumeration order.
4. Use `DISPLAYCONFIG_TARGET_DEVICE_NAME.monitorDevicePath` as the persisted identity.
5. If target-name data is missing, consider the interface path from `EnumDisplayDevices` only for an unambiguous one-target/one-active-device mapping.
6. Refuse duplicate paths, multiple targets on one source (clone), and overlapping desktop rectangles.
7. Resolve exactly one selected path; never choose by current index, model name, EDID manufacturer or nearest geometry.

Negative X/Y bounds remain signed. Right and bottom are exclusive. The overlay uses full monitor bounds, not work area. PMv2 DPI awareness is declared before HWND creation. Windows 10 1703+ is the supported baseline for automatic native dialog DPI behavior. The refresh label is the current nominal integer frequency from GDI; it is not a measured VRR frame rate. Missing names/frequencies remain unknown/omitted.

Device paths normally survive enumeration reorder; they are not globally unique serials across arbitrary port/driver changes. Some identical no-serial panels may be indistinguishable after replacement at a connector. The app cannot prove physical identity in that case. Reconfirm after hardware changes; no heuristic fallback is advertised as certainty.

## Window policy

The owner-linked popup is hidden at creation. `WS_EX_TOOLWINDOW` omits shell switching lists, `WS_EX_NOACTIVATE` and `SWP_NOACTIVATE` preserve focus, and `WS_EX_TOPMOST` provides normal desktop coverage. `WM_MOUSEACTIVATE` rejects activation. The default background and paint are RGB zero.

A fully opaque (`alpha=255`) `WS_EX_LAYERED | WS_EX_TRANSPARENT` window is intentional: documented cross-process mouse pass-through is needed during the short interval between pointer return and the next poll. `HTTRANSPARENT` alone is insufficient outside the calling thread. No color key, per-pixel alpha animation, DirectX surface, graphics hook or injection is used. This trades some compositor memory for correctness. No repeated z-order enforcement is used against other topmost windows.

Identify cards use the same non-activating pass-through style and close after two seconds. Opening Settings or an explicit tray menu may activate app UI; automatic blackout and identification must not. The HWNDs belong to the app and disappear on process termination. Fatal handlers hide first; they cannot guarantee recovery from a hung OS/driver/process.

## Persistence and errors

Strict versioned UTF-8 text has bounded file/field sizes. A missing file uses defaults; unsupported/corrupt input drops the selected monitor and attempts a backup. Copy failure sets a recovery diagnostic and does not abort startup. Delay alone can be sanitized. A write creates `settings.tmp`, writes fully, calls `FlushFileBuffers`, closes it and atomically replaces the destination with `MoveFileEx`. Failed replacement retains the previous file and surfaces an error. This is crash-resistant replacement, not an absolute guarantee against storage hardware failure.

The Run-key preference is independent of the file. A registry error is reported even if other settings were saved. Log rotation keeps the current and previous event log, approximately 256 KiB each. No continuous coordinates or serial-bearing paths are written. Log failure falls back to debugger output without recursive logging.

## Reliability details

**Reentrancy and lifecycle.** One UI thread can still receive nested synchronous window messages. A topology generation counter invalidates snapshots if discovery or a window call overlaps a refresh. The pre-show check validates the path, handle, device and bounds; the post-show check hides again if topology/session/enable/tray state changed. This reduces stale-snapshot risk, but OS topology notifications cannot make an external hardware transition atomic. `WM_QUERYENDSESSION` inhibits new blackout until shutdown or cancellation. Reset destroys Identify cards, including on disable/session transitions. Timer replacement reuses one HWND timer ID. Tray registration failure hides blackout and retries; legacy tray callbacks remain supported if version negotiation fails.

**Identity.** UTF-8 storage preserves non-ASCII bytes. Native matching converts to UTF-16 and uses `CompareStringOrdinal(..., TRUE)`, not locale-sensitive linguistic folding. It intentionally does not normalize Unicode or match by model. Portable tests use ASCII comparison unless a comparator is supplied. Rectangle extents compute in 64 bits and reject dimensions not representable by Win32's signed int sizes.

**Existing instance.** The `Local\OLED.Blackout.Singleton.v2` mutex and controller class/title intentionally retain their v2 namespace so 2.0 and 2.1 cannot run concurrently. New `.v2.1` message/property identifiers and a protocol value distinguish compatible controllers. The second process finds only that exact controller class/title, checks root HWND and protocol property, performs a bounded probe, grants foreground permission to the matched HWND owner PID, then requests Settings. No process handle is opened and no process list is scanned. An ACK means the request was queued, not that Windows guaranteed foreground focus. Failure offers the tray/old-version exit guidance. The receiver coalesces pending requests and validates action/protocol/own HWND. These markers prevent accidental target confusion; they are not cryptographic authentication against another malicious same-user process. No privileged operation is exposed.

**Startup.** `StartupRegistration` has an injected root/subkey/executable so integration tests use a volatile isolated key, never the real Run key. Production owns only the `OLED Blackout` value. State is Disabled, Current, Stale or Unreadable. Save always reconciles the checkbox, including removing stale unchecked values. File and registry cannot form one atomic transaction: if the file saves but startup fails, the other settings take effect, the dialog remains open and a visible error explains the failed registry operation.

**Settings.** Standard resource dialog, list-view, combo, edit, checkboxes and buttons; no custom-drawn framework or theme dependency. Draft monitor identity is separate from saved settings, and refresh maps it to a live safe row or a retained unavailable row. No row index is persisted. The list vetoes new unavailable selections; its own standard-control subclass routes navigation around unavailable rows without synthesizing input. A portable navigation policy is tested. PMv2 scales standard dialog fonts; cached 96-DPI control geometry and native scrollbars retain access on smaller work areas. The header font is created/deleted per dialog/DPI change, and the list uses system colors for high contrast. The preset's full dropped rectangle is retained during layout. The native dialog has been smoke-tested on a dual-monitor Windows system; the broader DPI/accessibility matrix remains in `MANUAL-QA.md`.

**Resource review.** File/mutex handles, registry keys, COM allocations, menus/submenus, icon, header/Identify fonts, timers, window subclasses, power/session notifications and owned HWNDs have explicit cleanup paths. GDI painting uses BeginPaint/EndPaint and restores selected fonts. No native leak claim follows from source review; USER/GDI/handle sampling is a release gate.

## Microsoft references consulted

- [Layered windows and mouse hit testing](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows)
- [WM_NCHITTEST thread-scoped HTTRANSPARENT](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest)
- [DisplayConfig target device identity](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_target_device_name)
- [QueryDisplayConfig](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig)
- [GetCursorPos and input-desktop constraints](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcursorpos)
- [RegisterHotKey and conflicts](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey)

- [CompareStringOrdinal](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringordinal)
- [AllowSetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow)
- [SendMessageTimeoutW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagetimeoutw)
- [LVN_ITEMCHANGING](https://learn.microsoft.com/en-us/windows/win32/controls/lvn-itemchanging)
