# OLED Blackout 2.1.1

OLED Blackout 2.1.1 is a maintenance release that fixes an intermittent case where an active blackout could unexpectedly disappear even though the pointer had not returned to the OLED.

## Highlights

- Ignores unrelated Windows settings broadcasts that previously could dismiss blackout.
- Resets blackout only for a real display-power transition, not repeated steady-state power notifications.
- Handles overlay DPI notifications without an unnecessary topology refresh.
- Records the exact reason whenever blackout is hidden, improving future diagnostics.
- Retains the native, non-activating and click-through design with no administrator access, injection, hooks, drivers, telemetry or network use.

## Verification

- 96,621 core assertions passed, including new display-power transition regression tests.
- 48 Windows platform assertions passed.
- Windows CI build and both test suites passed.
- The affected dual-monitor scenario was retested successfully: blackout stayed active while the pointer remained away.
- The release workflow rebuilds and reruns both suites from this exact tag before publishing the ZIP.

## Installation

Download `OLED-Blackout-2.1.1-win-x64.zip`, extract it, run `OLEDBlackout.exe`, then identify and select the OLED in Settings.

This release is unsigned and Windows may show an unknown-publisher warning. The `.sha256` asset verifies the downloaded ZIP. No universal anti-cheat compatibility or burn-in-prevention guarantee is made.
