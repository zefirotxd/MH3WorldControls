# Changelog

## 0.1.0-preview.11

### Español

- Añade captura del botón de autorecolección desde la configuración: selecciona un puerto XInput o cualquiera, pulsa «Capturar» y luego A, B, X o Y en el mando.
- La captura solo cambia la selección pendiente; no activa acciones del juego y requiere «Guardar y aplicar» para persistirla. Si no detecta un botón en 15 segundos, permite reintentar.

### English

- Adds in-settings capture for the auto-gather button: choose an XInput slot or any controller, press “Capture,” then press A, B, X, or Y on the controller.
- Capture only changes the pending selection; it does not trigger game actions and requires “Save and Apply” to persist. If no button is detected within 15 seconds, it can be retried.

## 0.1.0-preview.10

### Español

- Añade bloqueos XInput configurables para botones, cruceta y gatillos, desde la ventana y el INI. Se suman a las guardas de combate integradas; los gatillos configurados se activan desde 32/255.
- El diagnóstico copiable informa la configuración de bloqueos extra y cuántos scrolls se descartaron por una acción activa.
- La ventana de configuración crece para mostrar los nuevos ajustes del mando.

### English

- Adds configurable XInput action guards for buttons, D-pad directions, and triggers through the settings window and INI. They extend the built-in combat guards; configured triggers activate at 32/255.
- Copied diagnostics report the extra guard configuration and how many wheel events were discarded during an active action.
- Expands the settings window to fit the additional controller options.

## 0.1.0-preview.9

### Español

- La ventana muestra si el botón XInput configurado está siendo detectado y si la recolección automática está activa, esperando el foco del juego o necesita conectar el mando seleccionado.
- El diagnóstico copiable incluye la ranura XInput cuyo botón de recolección está presionado para facilitar los reportes de soporte.
- Descarga el ZIP de Windows x64, extráelo y ejecuta `MH3WorldControls.exe` antes de abrir OceanTriRecomp. La recolección con mando requiere que Windows lo exponga como XInput.
- La compilación de Windows x64 se valida en GitHub Actions; esta vista previa todavía requiere validación dentro del juego con distintos mandos y armas.

### English

- Added live controller-button feedback to the dashboard so users can confirm whether the selected XInput gather button is being detected.
- Added the held controller-slot mask to copied diagnostics to make controller setup reports more actionable.
- Download the Windows x64 ZIP, extract it, and run `MH3WorldControls.exe` before launching OceanTriRecomp. Controller auto-gather requires Windows to expose the pad as XInput.
- The Windows x64 build is checked in GitHub Actions; this preview still needs in-game validation with different controllers and weapons.

## 0.1.0-preview.8

- Fixed shutdown cleanup so the helper never sends Q-up over a physically held Q key.
- Moved low-level keyboard and mouse hooks to a dedicated message thread and removed process-image queries from the keyboard hook callback.
- Moved active settings to `%LOCALAPPDATA%\MH3WorldControls`, with automatic migration from the old beside-the-executable INI and support for creating a missing configuration on save.
- Added configurable wheel sensitivity and direction while preserving fractional high-resolution wheel deltas.
- Added optional per-controller auto-gather selection and configurable extra action-guard keys.
- Added visible queue/drop counts and a privacy-conscious copy-diagnostics action.
- Added wheel/focus details to the dashboard, keyboard-focus outlines, per-monitor DPI scaling, and Windows executable version metadata.
- Added a size-limited rolling status log and an option to mute controller connection notifications.
- Included the changelog in release packages and documented the new controls.

## 0.1.0-preview.7

- Fixed Save and Apply rejecting a successful Windows INI cache flush as an error.
- The helper now records actual profile-write failures and checks the staged file flush before replacing the existing INI.

## 0.1.0-preview.6

- Made controller connection notifications match the configured gather button.
- Added single-click tray restore, a right-click tray menu, and tray icon recovery after Explorer restarts.
- Added bounded spin controls and smooth, balanced, and agile wheel presets to the settings window.
- Staged INI changes before replacing the live configuration so failed saves leave the previous file intact.

## 0.1.0-preview.5

- Added Windows notifications when an XInput controller connects or disconnects, including startup detection.
- Added a full settings window for wheel timing and controller auto-gather options.
- Settings are validated, saved to the INI, and applied immediately without restarting.

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
