<div align="center">

# MH3 World Controls

**Un helper de calidad de vida para teclado, ratón y mando en OceanTriRecomp.**

[Español](#español) · [English](#english) · [Releases](https://github.com/zefirotxd/MH3WorldControls/releases)

![Windows](https://img.shields.io/badge/Windows-10%2F11-blue) ![License](https://img.shields.io/badge/License-MIT-green) ![Build](https://github.com/zefirotxd/MH3WorldControls/actions/workflows/build-release.yml/badge.svg)

</div>

---

## Español

MH3 World Controls mejora dos controles repetitivos de **OceanTriRecomp**: cambiar objetos con la rueda del ratón y mantener la acción de confirmar para recolectar. Se ejecuta como un helper independiente de Windows; no reemplaza archivos del juego ni se instala en la carpeta `mods/`.

La aplicación incluye una ventana de estado para comprobar si el juego está enfocado, ver los mandos XInput detectados, pausar la rueda, ajustar los controles y copiar un diagnóstico sin datos personales.

### Funciones

- Cambia objetos con la rueda sin mantener Q.
- Conserva el movimiento y permite cambiar objetos mientras corres.
- Descarta el scroll durante acciones de combate detectadas para no cancelar cargas ni reproducir selecciones después.
- Evita que los pulsos de selección se conviertan en usos accidentales de objetos.
- Repite la recolección al mantener F o el botón A lógico del mando: **B en Xbox / Círculo en PlayStation**.
- Permite cambiar el botón de autorecolección de mando o desactivarlo desde el INI.
- Deja F8 para pausar o reactivar la rueda y F9 para cerrar el helper.
- Notifica cuando se conecta o desconecta un mando XInput.
- Permite configurar desde una ventana los tiempos de la rueda y la autorecolección, y aplica los cambios sin reiniciar.
- Ajusta la sensibilidad e invierte el sentido de la rueda; las fracciones de scroll se conservan para ratones de alta resolución.
- Limita la autorecolección a cualquier mando o a un puerto XInput concreto, y añade hasta ocho teclas personales de seguridad para bloquear la selección durante acciones.
- Muestra cuántos pasos de scroll esperan o se descartaron por el límite seguro de cola. La captura de entradas usa un hilo separado de la interfaz.
- Copia un diagnóstico técnico al portapapeles sin incluir rutas personales. La versión también aparece en las propiedades del ejecutable.
- Permite silenciar los avisos de conexión del mando sin ocultar su estado en la ventana. La interfaz escala con el DPI de Windows.
- La ventana confirma en vivo si detecta el botón de recolección configurado y si los pulsos están activos; el diagnóstico copia también el puerto XInput que lo está enviando.
- El aviso del mando respeta el botón de autorecolección elegido. El icono de bandeja permite abrir la ventana con un clic y ofrece un menú con configuración, rueda y cierre.

### Requisitos

- Windows 10 u 11, 64 bits.
- OceanTriRecomp para Windows.
- Para autorecolección con mando: un mando que Windows exponga como **XInput**. En mandos PlayStation, activa Steam Input o DS4Windows con emulación Xbox. Los mandos que conectes mientras el helper está abierto se detectan en un máximo aproximado de dos segundos.

### Instalación rápida

1. Descarga el ZIP de Windows desde [Releases](https://github.com/zefirotxd/MH3WorldControls/releases).
2. Extrae los archivos en una carpeta, por ejemplo junto a `OceanTriRecomp.exe`.
3. Ejecuta `MH3WorldControls.exe` y luego abre OceanTriRecomp.
4. Gira la rueda para cambiar de objeto. Mantén F o B/Círculo mientras recolectas.

El helper actúa cuando OceanTriRecomp está en primer plano. Cierra el helper con F9 o desde su ventana. Ejecútalo con el mismo nivel de permisos que el juego; normalmente ambos deben ejecutarse sin privilegios de administrador.

### Controles y ajustes

| Entrada | Acción |
| --- | --- |
| Rueda arriba/abajo | Cambiar objeto |
| F mantenida | Repetir confirmar/recolección |
| B / Círculo mantenido (XInput) | Repetir confirmar/recolección |
| F8 | Pausar/reactivar captura de la rueda |
| F9 | Cerrar el helper |

La configuración activa está en `%LOCALAPPDATA%\MH3WorldControls\MH3WorldControls.ini`. Al iniciar, el helper migra automáticamente el INI antiguo que estuviera junto al ejecutable. El INI incluido en el ZIP funciona como valores iniciales.

| Ajuste | Predeterminado | Rango |
| --- | ---: | ---: |
| `initial_delay_ms` | 300 ms | 300–1000 ms |
| `pulse_ms` | 50 ms | 50–100 ms |
| `step_interval_ms` | 150 ms | 100–600 ms |
| `release_delay_ms` | 550 ms | 550–1000 ms |
| `preopen_threshold` | 60 | 0–119 |
| `sensitivity_percent` | 100 % | 50–200 % |
| `invert` | 0 | 0 normal, 1 invertido |

Si editas el INI manualmente, reinicia el helper para aplicar los cambios. Los valores más altos hacen la navegación más pausada. Mantén los mínimos de espera y liberación: ayudan a que la selección no se confunda con el uso del objeto.

También puedes abrir **Configuración** en la ventana principal. Usa las flechas para ajustar valores o elige un ritmo de rueda **Suave**, **Equilibrado** o **Ágil**. Ajusta sensibilidad, dirección y teclas extra para bloquear el scroll durante acciones. Pulsa **Guardar y aplicar**; los cambios se guardan juntos en el INI y se aplican en el momento.

En `[Controller]`, `gather_button` elige el botón físico XInput que activa los pulsos de recolección: `A`, `B`, `X`, `Y` u `OFF`. El valor inicial `B` coincide con el botón Este del mapeo predeterminado del juego. `gather_controller=any` acepta cualquier mando; `0`, `1`, `2` o `3` limita la función a los puertos XInput 1–4. `connection_notifications=0` silencia los avisos de Windows sin ocultar el mando en el panel. `gather_pulse_ms` ajusta la duración de cada toque (50–100 ms) y `gather_interval_ms` el tiempo entre toques (200–1000 ms).

En `[Input]`, `extra_guard_keys` añade hasta ocho teclas que bloquean la rueda mientras se mantienen, separadas por comas: por ejemplo `X, E, F` o `SPACE, SHIFT`. Los guardas de seguridad integrados permanecen activos.

Pulsa **Copiar diagnóstico** en la ventana principal (o desde el menú del icono de bandeja) para copiar versión, foco del juego, estado XInput, ajustes activos y errores de cola. No copia tu nombre de usuario ni la ruta completa del INI.
Los mensajes de estado se guardan en `%LOCALAPPDATA%\MH3WorldControls\MH3WorldControls.log` y se rotan al superar 512 KB.

### Solución de problemas

- **La rueda no cambia objetos:** confirma que el helper está abierto y que OceanTriRecomp tiene el foco. Revisa que otra aplicación no esté capturando la rueda.
- **La autorecolección con mando no se activa:** Windows debe mostrar el mando como XInput; mantén B/Círculo, que corresponde al botón Este del mapeo predeterminado. Espera dos segundos si conectaste el mando con el helper abierto. Los mandos disponibles únicamente como SDL/DirectInput no pueden ser leídos por este helper externo.
- **No se detecta el juego:** abre OceanTriRecomp y deja su ventana en primer plano. El ejecutable debe llamarse `OceanTriRecomp.exe`.
- **El helper no puede enviar teclas:** ejecuta el helper y el juego con el mismo nivel de permisos.
- **Una acción de arma cancela el cambio:** el helper descarta los giros que coinciden con entradas de acción detectadas; vuelve a girar al terminar la acción.
- **La rueda se siente lenta o rápida:** ajusta `sensitivity_percent` o el ritmo desde Configuración. El ajuste no reduce las esperas de seguridad entre pasos.
- **Una tecla reasignada permite cambiar objetos durante una carga:** añádela a **Teclas extra para bloquear**. Usa **Copiar diagnóstico** si quieres incluir el estado técnico en un reporte.

### Compilar desde el código

Instala Visual Studio 2022 o Build Tools con el componente **Desktop development with C++**, y CMake 3.20 o posterior. Desde PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

El ejecutable y `MH3WorldControls.ini` quedan en `build/Release/`. GitHub Actions compila el proyecto en Windows; al publicar un tag `v*`, crea un ZIP con el ejecutable, INI inicial, README, changelog y licencia.

### Alcance

Este proyecto es un helper de entrada para Windows, no un mod de reemplazo de archivos para la pestaña MODS del juego. La rueda usa hooks de entrada de Windows y `SendInput`; la lectura de botones de mando usa XInput. No modifica la imagen del juego ni inyecta código en OceanTriRecomp.

---

## English

MH3 World Controls is a small Windows quality-of-life helper for **OceanTriRecomp**. It improves mouse-wheel item selection and repeated gathering while a keyboard key or controller button is held. It runs beside the game; it is not installed through the in-game `mods/` folder.

The app includes a status window to check game focus and XInput detection, pause wheel selection, and open its configuration.

### Features

- Select items with the mouse wheel without holding Q.
- Keep moving and change items while sprinting.
- Discard wheel input during detected combat actions to avoid interrupting charges or replaying queued selections afterward.
- Use guarded, short selection pulses to reduce accidental item use.
- Repeat gathering while holding F or the logical Confirm button: **Xbox B / PlayStation Circle**.
- Configure the XInput gather button, choose which controller port can trigger it, or turn auto-gather off.
- F8 toggles wheel capture; F9 exits the helper.
- Shows a Windows notification when an XInput controller connects or disconnects.
- Provides a settings window for wheel and auto-gather timing, with immediate apply.
- Lets you tune wheel sensitivity and direction, add up to eight action-guard keys, and see pending or dropped scroll steps.
- Keeps low-level input capture on a dedicated thread and provides privacy-conscious diagnostics in the clipboard.
- Lets you mute controller notices while retaining the live status panel, and scales the interface for Windows display DPI.
- Shows live feedback when the configured gather button is detected and when auto-gather pulses are active; copied diagnostics include the XInput slot sending it.
- The tray icon opens the main window on click and provides a menu for settings, wheel control, and exit.

### Requirements and setup

- Windows 10 or 11, 64-bit, and the Windows version of OceanTriRecomp.
- For controller auto-gather, the controller must be exposed to Windows as **XInput**. For PlayStation controllers, use Steam Input or DS4Windows in Xbox emulation mode. Controllers connected while the helper is open are detected within about two seconds.
- Download the Windows ZIP from [Releases](https://github.com/zefirotxd/MH3WorldControls/releases), extract it, start `MH3WorldControls.exe`, then launch the game.
- The helper only acts while OceanTriRecomp is in the foreground. Run both programs at the same permission level.

The active configuration is `%LOCALAPPDATA%\MH3WorldControls\MH3WorldControls.ini`. On startup, the helper migrates an older INI from beside the executable. The INI in the ZIP is the initial template. See the Spanish section above for setting names and ranges.

You can also open the settings window from the main window. Use the arrow controls to adjust values or choose a **Suave**, **Equilibrado**, or **Ágil** wheel preset. Choose **Guardar y aplicar** to save the settings together and use them immediately. Manual INI edits require a helper restart.

In `[Controller]`, `gather_button` accepts `A`, `B`, `X`, `Y`, or `OFF`. The default `B` matches the game's default East/Confirm mapping. Set `gather_controller` to `any` or an XInput port from `0` to `3`; set `connection_notifications=0` to mute Windows notices while keeping the status panel active. In `[Wheel]`, `sensitivity_percent` adjusts scroll sensitivity from 50% to 200%, and `invert=1` reverses direction. In `[Input]`, `extra_guard_keys` accepts up to eight comma-separated keys such as `X, E, F`; built-in safety guards remain active.

Use **Copiar diagnóstico** in the main window or tray menu to copy version, game-focus, controller, timing and queue status. The report excludes your Windows username and full configuration path.
Status messages are kept in `%LOCALAPPDATA%\MH3WorldControls\MH3WorldControls.log`, rotated above 512 KB.

### Build

Install Visual Studio 2022 or Build Tools with **Desktop development with C++**, plus CMake 3.20 or newer. Then run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

The executable and default INI are written to `build/Release/`. GitHub Actions builds Windows packages for tagged releases, including the README, changelog and license.

### Scope

This is a Windows input helper, not a data-replacement mod for the game's MODS page. It uses Windows input hooks, `SendInput`, and XInput for controller detection. It does not modify game data or inject code into OceanTriRecomp.
