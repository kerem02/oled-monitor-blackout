# OLED Blackout 2.1.2

OLED Blackout 2.1.2 is a maintenance release for a second, log-confirmed case where an active blackout could unexpectedly disappear even though the pointer had not returned to the OLED.

## Highlights

- Stops treating generic Windows Plug and Play broadcasts as display-topology changes. `DBT_DEVNODES_CHANGED` may describe USB, audio, Bluetooth, storage or other unrelated hardware.
- Continues to fail open for real monitor changes through `WM_DISPLAYCHANGE`, continuous selected-monitor health checks and automatic reconnect retries.
- Includes the repository wording/version-handling cleanup completed after 2.1.1.
- Retains exact blackout-dismissal reason logging for future diagnostics.
- Retains the native, non-activating and click-through design with no administrator access, injection, hooks, drivers, telemetry or network use.

## Verification

- 96,621 core assertions passed with AddressSanitizer and UndefinedBehaviorSanitizer.
- 48 Windows platform assertions passed.
- Windows CI build and both test suites passed.
- The release workflow rebuilds and reruns both suites from this exact tag before publishing the ZIP.
- The non-display-device physical regression is documented as open manual QA; automated tests are not presented as proof of hardware behavior.

## Installation

Download `OLED-Blackout-2.1.2-win-x64.zip`, extract it, run `OLEDBlackout.exe`, then identify and select the OLED in Settings.

This release is unsigned and Windows may show an unknown-publisher warning. The `.sha256` asset verifies the downloaded ZIP. No universal anti-cheat compatibility or burn-in-prevention guarantee is made.
