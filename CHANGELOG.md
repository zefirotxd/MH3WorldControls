# Changelog

## 0.1.0-preview.4

- Replaced the console window with a native dark dashboard showing game focus, wheel state, and XInput controller status.
- Added buttons to pause/resume wheel selection, open the configuration file, and close the helper.
- Kept F8/F9 hotkeys and the existing control worker.
- Reported the number of connected XInput controllers, including while the game is not focused.

## 0.1.0-preview.3

- Added INI controls for the auto-gather pulse duration and repeat interval while preserving the current defaults.
- Added startup output showing the active gathering cadence.
- Kept connected controllers responsive while spacing out checks for empty XInput slots; newly connected pads are discovered within two seconds.
- Avoided treating a rejected synthetic key press as an active gathering pulse.

## 0.1.0-preview.2

- Made the XInput auto-gather button configurable (`A`, `B`, `X`, `Y`, or `OFF`).
- Read each connected XInput controller once per helper poll instead of polling the same state twice.
- Reduced GitHub Actions write access to the release job and pinned workflow actions to commit SHAs.

## 0.1.0-preview.1 — initial public package

- Added guarded mouse-wheel item selection with a configurable, controllable pace.
- Allowed changing items while moving and sprinting.
- Added keyboard and XInput controller auto-gather pulses.
- Added queue cancellation and action guards to reduce stale selections and accidental item use.
- Added a Windows x64 CMake build and tagged-release packaging workflow.
