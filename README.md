<div align="center">

# MH3 World Controls

**Un helper de calidad de vida para teclado, ratón y mando en OceanTriRecomp.**

[Español](#español) · [English](#english) · [Releases](https://github.com/zefirotxd/MH3WorldControls/releases)

![Windows](https://img.shields.io/badge/Windows-10%2F11-blue) ![License](https://img.shields.io/badge/License-MIT-green) ![Build](https://github.com/zefirotxd/MH3WorldControls/actions/workflows/build-release.yml/badge.svg)

</div>

---

## Español

MH3 World Controls mejora dos controles repetitivos de **OceanTriRecomp**: cambiar objetos con la rueda del ratón y mantener la acción de confirmar para recolectar. Se ejecuta como un helper independiente de Windows; no reemplaza archivos del juego ni se instala en la carpeta `mods/`.

### Funciones

- Cambia objetos con la rueda sin mantener Q.
- Conserva el movimiento y permite cambiar objetos mientras corres.
- Descarta el scroll durante acciones de combate detectadas para no cancelar cargas ni reproducir selecciones después.
- Evita que los pulsos de selección se conviertan en usos accidentales de objetos.
- Repite la recolección al mantener F o el botón A lógico del mando: **B en Xbox / Círculo en PlayStation**.
- Permite cambiar el botón de autorecolección de mando o desactivarlo desde el INI.
- Deja F8 para pausar o reactivar la rueda y F9 para cerrar el helper.

### Requisitos

- Windows 10 u 11, 64 bits.
- OceanTriRecomp para Windows.
- Para autorecolección con mando: un mando que Windows exponga como **XInput**. En mandos PlayStation, activa Steam Input o DS4Windows con emulación Xbox.

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

Los tiempos están en `MH3WorldControls.ini`, junto al ejecutable:

| Ajuste | Predeterminado | Rango |
| --- | ---: | ---: |
| `initial_delay_ms` | 300 ms | 300–1000 ms |
| `pulse_ms` | 50 ms | 50–100 ms |
| `step_interval_ms` | 150 ms | 100–600 ms |
| `release_delay_ms` | 550 ms | 550–1000 ms |
| `preopen_threshold` | 60 | 0–119 |

Reinicia el helper para aplicar cambios. Los valores más altos hacen la navegación más pausada. Mantén los mínimos de espera y liberación: ayudan a que la selección no se confunda con el uso del objeto.

En `[Controller]`, `gather_button` elige el botón físico XInput que activa los pulsos de recolección: `A`, `B`, `X`, `Y` u `OFF`. El valor inicial `B` coincide con el botón Este del mapeo predeterminado del juego.

### Solución de problemas

- **La rueda no cambia objetos:** confirma que el helper está abierto y que OceanTriRecomp tiene el foco. Revisa que otra aplicación no esté capturando la rueda.
- **La autorecolección con mando no se activa:** Windows debe mostrar el mando como XInput; mantén B/Círculo, que corresponde al botón Este del mapeo predeterminado. Los mandos disponibles únicamente como SDL/DirectInput no pueden ser leídos por este helper externo.
- **No se detecta el juego:** abre OceanTriRecomp y deja su ventana en primer plano. El ejecutable debe llamarse `OceanTriRecomp.exe`.
- **El helper no puede enviar teclas:** ejecuta el helper y el juego con el mismo nivel de permisos.
- **Una acción de arma cancela el cambio:** el helper descarta los giros que coinciden con entradas de acción detectadas; vuelve a girar al terminar la acción.

### Compilar desde el código

Instala Visual Studio 2022 o Build Tools con el componente **Desktop development with C++**, y CMake 3.20 o posterior. Desde PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

El ejecutable y `MH3WorldControls.ini` quedan en `build/Release/`. GitHub Actions compila el proyecto en Windows; al publicar un tag `v*`, crea un ZIP de lanzamiento.

### Alcance

Este proyecto es un helper de entrada para Windows, no un mod de reemplazo de archivos para la pestaña MODS del juego. La rueda usa hooks de entrada de Windows y `SendInput`; la lectura de botones de mando usa XInput. No modifica la imagen del juego ni inyecta código en OceanTriRecomp.

---

## English

MH3 World Controls is a small Windows quality-of-life helper for **OceanTriRecomp**. It improves mouse-wheel item selection and repeated gathering while a keyboard key or controller button is held. It runs beside the game; it is not installed through the in-game `mods/` folder.

### Features

- Select items with the mouse wheel without holding Q.
- Keep moving and change items while sprinting.
- Discard wheel input during detected combat actions to avoid interrupting charges or replaying queued selections afterward.
- Use guarded, short selection pulses to reduce accidental item use.
- Repeat gathering while holding F or the logical Confirm button: **Xbox B / PlayStation Circle**.
- Configure the XInput gather button or turn controller auto-gather off in the INI.
- F8 toggles wheel capture; F9 exits the helper.

### Requirements and setup

- Windows 10 or 11, 64-bit, and the Windows version of OceanTriRecomp.
- For controller auto-gather, the controller must be exposed to Windows as **XInput**. For PlayStation controllers, use Steam Input or DS4Windows in Xbox emulation mode.
- Download the Windows ZIP from [Releases](https://github.com/zefirotxd/MH3WorldControls/releases), extract it, start `MH3WorldControls.exe`, then launch the game.
- The helper only acts while OceanTriRecomp is in the foreground. Run both programs at the same permission level.

The timing values can be edited in `MH3WorldControls.ini` beside the executable. See the Spanish section above for the setting names and supported ranges.

In `[Controller]`, `gather_button` accepts `A`, `B`, `X`, `Y`, or `OFF`. The default `B` matches the game's default East/Confirm mapping.

### Build

Install Visual Studio 2022 or Build Tools with **Desktop development with C++**, plus CMake 3.20 or newer. Then run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

The executable and default INI are written to `build/Release/`. GitHub Actions builds Windows packages for tagged releases.

### Scope

This is a Windows input helper, not a data-replacement mod for the game's MODS page. It uses Windows input hooks, `SendInput`, and XInput for controller detection. It does not modify game data or inject code into OceanTriRecomp.
