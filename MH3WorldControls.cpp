#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace {
using Clock = std::chrono::steady_clock;

constexpr wchar_t kGameExecutable[] = L"OceanTriRecomp.exe";
constexpr UINT kWheelDeltaMessage = WM_APP + 0x31;
WORD g_controllerGatherButton = XINPUT_GAMEPAD_B;

struct WheelTiming {
    DWORD initialDelayMs = 300;
    DWORD pulseMs = 50;
    DWORD stepIntervalMs = 150;
    DWORD releaseDelayMs = 550;
    DWORD preopenThreshold = 60;
    DWORD gatherPulseMs = 60;
    DWORD gatherIntervalMs = 300;
};
WheelTiming g_wheelTiming;

std::atomic<bool> g_running{true};
std::atomic<bool> g_wheelEnabled{true};
std::atomic<bool> g_pickupHeld{false};
std::atomic<bool> g_itemKeyHeld{false};
std::atomic<bool> g_physicalYHeld{false};
std::atomic<bool> g_physicalAHeld{false};
std::atomic<bool> g_physicalXHeld{false};
std::atomic<bool> g_controllerActionHeld{false};
std::atomic<bool> g_wheelModifierInjected{false};
std::atomic<bool> g_wheelModifierPending{false};
std::atomic<ULONGLONG> g_wheelReleaseAt{0};
std::atomic<ULONGLONG> g_wheelReadyAt{0};
std::atomic<DWORD> g_gameProcessId{0};
std::mutex g_wheelMutex;
std::mutex g_foregroundCacheMutex;
std::deque<WORD> g_wheelDirections;
int g_pendingWheelDelta = 0;
bool g_wheelBusy = false;
bool g_deferredItemKeyRelease = false;
DWORD g_cachedForegroundPid = 0;
bool g_cachedForegroundIsGame = false;
Clock::time_point g_foregroundCacheUpdatedAt{};
DWORD g_mainThreadId = 0;
HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseHook = nullptr;
HANDLE g_instanceGuard = nullptr;
HMODULE g_xinputModule = nullptr;
using XInputGetStateProc = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
XInputGetStateProc g_xinputGetState = nullptr;
std::mutex g_xinputStateMutex;
std::array<bool, XUSER_MAX_COUNT> g_xinputConnected{};
Clock::time_point g_nextXinputDiscovery{};
constexpr WORD kMouseRightAction = 0xFFFF;
constexpr ULONG_PTR kWheelActionTag = 0x4D483357; // "MH3W"

void Log(const std::string& message);

void LoadWheelTiming() {
    wchar_t executablePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executablePath,
                                            static_cast<DWORD>(std::size(executablePath)));
    if (length == 0 || length >= std::size(executablePath)) {
        Log("No se pudo localizar MH3WorldControls.ini; se usaran los tiempos seguros por defecto.");
        return;
    }

    std::wstring configPath(executablePath, length);
    const size_t separator = configPath.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        Log("No se pudo localizar MH3WorldControls.ini; se usaran los tiempos seguros por defecto.");
        return;
    }
    configPath.resize(separator + 1);
    configPath += L"MH3WorldControls.ini";

    const auto readSetting = [&configPath](const wchar_t* name, DWORD defaultValue,
                                           DWORD minimum, DWORD maximum) {
        const UINT raw = GetPrivateProfileIntW(L"Wheel", name, defaultValue, configPath.c_str());
        return std::clamp<DWORD>(raw, minimum, maximum);
    };

    // A short L lead-in caused Y/A to reach the game before the item bar opened,
    // which can use the selected item. Keep the lead-in and release guard bounded.
    g_wheelTiming.initialDelayMs = readSetting(L"initial_delay_ms", 300, 300, 1000);
    g_wheelTiming.pulseMs = readSetting(L"pulse_ms", 50, 50, 100);
    g_wheelTiming.stepIntervalMs = readSetting(L"step_interval_ms", 150, 100, 600);
    g_wheelTiming.stepIntervalMs = std::max(g_wheelTiming.stepIntervalMs, g_wheelTiming.pulseMs);
    g_wheelTiming.releaseDelayMs = readSetting(L"release_delay_ms", 550, 550, 1000);
    const UINT rawPreopenThreshold = GetPrivateProfileIntW(L"Wheel", L"preopen_threshold", 60,
                                                            configPath.c_str());
    g_wheelTiming.preopenThreshold = std::min<DWORD>(rawPreopenThreshold, WHEEL_DELTA - 1);

    wchar_t gatherButton[16]{};
    GetPrivateProfileStringW(L"Controller", L"gather_button", L"B", gatherButton,
                              static_cast<DWORD>(std::size(gatherButton)), configPath.c_str());
    if (CompareStringOrdinal(gatherButton, -1, L"A", -1, TRUE) == CSTR_EQUAL)
        g_controllerGatherButton = XINPUT_GAMEPAD_A;
    else if (CompareStringOrdinal(gatherButton, -1, L"B", -1, TRUE) == CSTR_EQUAL)
        g_controllerGatherButton = XINPUT_GAMEPAD_B;
    else if (CompareStringOrdinal(gatherButton, -1, L"X", -1, TRUE) == CSTR_EQUAL)
        g_controllerGatherButton = XINPUT_GAMEPAD_X;
    else if (CompareStringOrdinal(gatherButton, -1, L"Y", -1, TRUE) == CSTR_EQUAL)
        g_controllerGatherButton = XINPUT_GAMEPAD_Y;
    else if (CompareStringOrdinal(gatherButton, -1, L"OFF", -1, TRUE) == CSTR_EQUAL)
        g_controllerGatherButton = 0;
    else {
        g_controllerGatherButton = XINPUT_GAMEPAD_B;
        Log("Controller.gather_button invalido; se usara B como valor seguro.");
    }
    g_wheelTiming.gatherPulseMs = std::clamp<DWORD>(
        GetPrivateProfileIntW(L"Controller", L"gather_pulse_ms", 60, configPath.c_str()), 50, 100);
    g_wheelTiming.gatherIntervalMs = std::clamp<DWORD>(
        GetPrivateProfileIntW(L"Controller", L"gather_interval_ms", 300, configPath.c_str()), 200, 1000);
    g_wheelTiming.gatherIntervalMs = std::max(g_wheelTiming.gatherIntervalMs,
                                              g_wheelTiming.gatherPulseMs);
}

void Log(const std::string& message) {
    std::cout << message << std::endl;
}

bool EnsureSingleInstance() {
    constexpr wchar_t kMutexName[] = L"Local\\OceanTriRecomp.MH3WorldControls";
    SetLastError(ERROR_SUCCESS);
    g_instanceGuard = CreateMutexW(nullptr, FALSE, kMutexName);
    if (g_instanceGuard == nullptr) {
        std::cerr << "No se pudo crear el control de instancia unica. Error de Windows: "
                  << GetLastError() << '\n';
        return false;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_instanceGuard);
        g_instanceGuard = nullptr;
        std::cout << "MH3WorldControls ya esta abierto. Cierra la otra instancia con F9.\n";
        return false;
    }
    return true;
}

bool IsGameForeground() {
    const HWND window = GetForegroundWindow();
    if (window == nullptr)
        return false;

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0)
        return false;

    const auto now = Clock::now();
    {
        std::lock_guard<std::mutex> lock(g_foregroundCacheMutex);
        if (processId == g_cachedForegroundPid && g_cachedForegroundIsGame) {
            g_gameProcessId.store(processId, std::memory_order_release);
            return true;
        }
        if (processId == g_cachedForegroundPid &&
            now - g_foregroundCacheUpdatedAt < std::chrono::seconds(1))
            return false;
    }

    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    bool isGame = false;
    if (process != nullptr) {
        wchar_t imagePath[MAX_PATH]{};
        DWORD pathLength = static_cast<DWORD>(std::size(imagePath));
        const bool gotPath = QueryFullProcessImageNameW(process, 0, imagePath, &pathLength) != FALSE;
        CloseHandle(process);
        if (gotPath) {
            const wchar_t* executable = imagePath;
            for (const wchar_t* cursor = imagePath; *cursor != L'\0'; ++cursor) {
                if (*cursor == L'\\' || *cursor == L'/')
                    executable = cursor + 1;
            }
            isGame = CompareStringOrdinal(executable, -1, kGameExecutable, -1, TRUE) == CSTR_EQUAL;
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_foregroundCacheMutex);
        g_cachedForegroundPid = processId;
        g_cachedForegroundIsGame = isGame;
        g_foregroundCacheUpdatedAt = now;
        if (isGame)
            g_gameProcessId.store(processId, std::memory_order_release);
    }
    return isGame;
}

bool IsKnownGameForeground() {
    const DWORD gameProcessId = g_gameProcessId.load(std::memory_order_acquire);
    if (gameProcessId == 0)
        return false;

    const HWND window = GetForegroundWindow();
    if (window == nullptr)
        return false;

    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(window, &foregroundProcessId);
    return foregroundProcessId == gameProcessId;
}

bool SendKey(WORD key, bool down, bool wheelSelection = false) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.dwExtraInfo = wheelSelection ? kWheelActionTag : 0;
    if (!down)
        input.ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(1, &input, sizeof(input)) == 1)
        return true;

    Log("ERROR: Windows rechazo una tecla simulada (SendInput). Comprueba que "
        "el helper y el juego se ejecuten con el mismo nivel de permisos.");
    return false;
}

bool SendMouseRight(bool down, bool wheelSelection = false) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    input.mi.dwExtraInfo = wheelSelection ? kWheelActionTag : 0;
    if (SendInput(1, &input, sizeof(input)) == 1)
        return true;

    Log("ERROR: Windows rechazo un clic derecho simulado (SendInput). Comprueba que "
        "el helper y el juego se ejecuten con el mismo nivel de permisos.");
    return false;
}

bool IsWheelActionPhysicallyHeld(WORD direction) {
    if (direction == 'E') {
        return g_physicalYHeld.load(std::memory_order_acquire);
    }
    if (direction == kMouseRightAction) {
        return g_physicalAHeld.load(std::memory_order_acquire);
    }
    return false;
}

bool IsAnyWheelActionPhysicallyHeld() {
    return IsWheelActionPhysicallyHeld('E') ||
           IsWheelActionPhysicallyHeld(kMouseRightAction) ||
           g_physicalXHeld.load(std::memory_order_acquire);
}

void SnapshotPhysicalActionState() {
    g_physicalYHeld.store((GetAsyncKeyState('E') & 0x8000) != 0, std::memory_order_release);
    g_physicalAHeld.store((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0,
                          std::memory_order_release);
    g_physicalXHeld.store((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
                          std::memory_order_release);
    g_pickupHeld.store(IsGameForeground() && (GetAsyncKeyState('F') & 0x8000) != 0,
                       std::memory_order_release);
}

void LoadXInput() {
    constexpr const wchar_t* kLibraries[] = {
        L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (const wchar_t* library : kLibraries) {
        HMODULE module = LoadLibraryW(library);
        if (module == nullptr)
            continue;
        auto getState = reinterpret_cast<XInputGetStateProc>(GetProcAddress(module, "XInputGetState"));
        if (getState != nullptr) {
            g_xinputModule = module;
            g_xinputGetState = getState;
            Log("Deteccion de botones de mando XInput activada.");
            return;
        }
        FreeLibrary(module);
    }
    Log("Aviso: no se detecto XInput; la autorecoleccion con mando no estara disponible.");
}

struct XInputHeldActions {
    bool gameplayAction = false;
    bool pickupButton = false;
};

XInputHeldActions ReadXInputHeldActions() {
    XInputHeldActions held;
    if (g_xinputGetState == nullptr)
        return held;

    std::lock_guard<std::mutex> lock(g_xinputStateMutex);
    const auto now = Clock::now();
    const bool discoverControllers = now >= g_nextXinputDiscovery;
    if (discoverControllers)
        g_nextXinputDiscovery = now + std::chrono::seconds(2);

    constexpr WORD kCombatButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B |
                                   XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y |
                                   XINPUT_GAMEPAD_LEFT_SHOULDER |
                                   XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK |
                                   XINPUT_GAMEPAD_LEFT_THUMB |
                                   XINPUT_GAMEPAD_RIGHT_THUMB;
    for (DWORD userIndex = 0; userIndex < XUSER_MAX_COUNT; ++userIndex) {
        // Keep connected pads responsive at the worker's normal rate, but avoid
        // polling empty XInput slots every 5 ms. Discover new pads every 2 s.
        if (!g_xinputConnected[userIndex] && !discoverControllers)
            continue;

        XINPUT_STATE state{};
        if (g_xinputGetState(userIndex, &state) != ERROR_SUCCESS) {
            g_xinputConnected[userIndex] = false;
            continue;
        }
        g_xinputConnected[userIndex] = true;

        const auto& pad = state.Gamepad;
        const bool rightShoulderHeld = (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        const bool moving = std::abs(int(pad.sThumbLX)) >= 12000 ||
                            std::abs(int(pad.sThumbLY)) >= 12000;
        if ((pad.wButtons & kCombatButtons) != 0 ||
            (rightShoulderHeld && !moving) || pad.bRightTrigger >= 32) {
            held.gameplayAction = true;
        }
        if (g_controllerGatherButton != 0 &&
            (pad.wButtons & g_controllerGatherButton) != 0) {
            held.pickupButton = true;
        }
        if (held.gameplayAction && held.pickupButton)
            break;
    }
    return held;
}

bool IsAnyGameplayActionPhysicallyHeld() {
    if (IsAnyWheelActionPhysicallyHeld() ||
        g_controllerActionHeld.load(std::memory_order_acquire)) {
        return true;
    }

    constexpr int kActionKeys[] = {VK_SPACE, VK_BACK, 'Z', 'C'};
    for (int key : kActionKeys) {
        if ((GetAsyncKeyState(key) & 0x8000) != 0)
            return true;
    }
    const bool runningHeld = (GetAsyncKeyState('R') & 0x8000) != 0 ||
                             (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
                             (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
    const bool movementHeld = (GetAsyncKeyState('W') & 0x8000) != 0 ||
                              (GetAsyncKeyState('A') & 0x8000) != 0 ||
                              (GetAsyncKeyState('S') & 0x8000) != 0 ||
                              (GetAsyncKeyState('D') & 0x8000) != 0;
    if (runningHeld && !movementHeld)
        return true;
    return g_pickupHeld.load(std::memory_order_acquire) ||
           (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
}

LRESULT CALLBACK KeyboardHook(int code, WPARAM message, LPARAM data) {
    bool deferItemKeyRelease = false;
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        const bool injected = (key->flags & LLKHF_INJECTED) != 0;
        const bool keyDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        if (injected && key->dwExtraInfo == kWheelActionTag && keyDown &&
            !g_itemKeyHeld.load(std::memory_order_acquire) &&
            !g_wheelModifierInjected.load(std::memory_order_acquire)) {
            return 1; // Never let a wheel-generated Y pulse through without L.
        }
        if (!injected && (key->vkCode == 'F' || key->vkCode == 'Q' || key->vkCode == 'E')) {
            const bool keyUp = message == WM_KEYUP || message == WM_SYSKEYUP;
            if (keyDown || keyUp) {
                const bool gameFocused = IsGameForeground();
                if (key->vkCode == 'F') {
                    g_pickupHeld.store(keyDown && gameFocused, std::memory_order_release);
                } else if (key->vkCode == 'E') {
                    // E maps to Y. Track only physical input so a wheel-generated
                    // key-up can never be mistaken for the player's charge release.
                    g_physicalYHeld.store(keyDown, std::memory_order_release);
                } else {
                    std::lock_guard<std::mutex> lock(g_wheelMutex);
                    g_itemKeyHeld.store(keyDown && gameFocused, std::memory_order_release);
                    if (keyDown) {
                        g_deferredItemKeyRelease = false;
                    } else if (gameFocused && g_wheelBusy) {
                        // Keep L held until queued wheel steps finish, even if the
                        // player releases Q quickly after scrolling.
                        g_deferredItemKeyRelease = true;
                        deferItemKeyRelease = true;
                    }
                }
            }
        }
    }

    if (deferItemKeyRelease)
        return 1;
    return CallNextHookEx(g_keyboardHook, code, message, data);
}

LRESULT CALLBACK MouseHook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ||
                              message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)) {
        const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        const bool injected = (mouse->flags & LLMHF_INJECTED) != 0;
        if (!injected) {
            const bool buttonDown = message == WM_RBUTTONDOWN || message == WM_LBUTTONDOWN;
            if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
                // Classic Style 1 charges Great Sword with X (left mouse in this
                // helper). Do not open the item bar during that held charge.
                g_physicalXHeld.store(buttonDown, std::memory_order_release);
            } else {
                g_physicalAHeld.store(buttonDown, std::memory_order_release);
            }
        } else if (message == WM_RBUTTONDOWN && mouse->dwExtraInfo == kWheelActionTag &&
                   !g_itemKeyHeld.load(std::memory_order_acquire) &&
                   !g_wheelModifierInjected.load(std::memory_order_acquire)) {
            return 1; // Never let a wheel-generated A pulse use an item without L.
        }
    }

    if (code == HC_ACTION && message == WM_MOUSEWHEEL) {
        const auto* wheel = reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        const bool injected = (wheel->flags & LLMHF_INJECTED) != 0;
        if (!injected && g_wheelEnabled.load(std::memory_order_acquire) && IsKnownGameForeground()) {
            const SHORT delta = static_cast<SHORT>(HIWORD(wheel->mouseData));
            const WPARAM packedDelta = static_cast<WPARAM>(static_cast<WORD>(delta));
            // Preserve the button state from the instant of the physical scroll.
            // The main thread may process this message after the player releases it.
            const LPARAM actionHeldAtScroll = IsAnyGameplayActionPhysicallyHeld() ? 1 : 0;
            if (PostThreadMessageW(g_mainThreadId, kWheelDeltaMessage, packedDelta,
                                   actionHeldAtScroll))
                return 1;
        }
    }

    return CallNextHookEx(g_mouseHook, code, message, data);
}

bool DiscardPendingWheelInputLocked(bool force) {
    const bool hasPendingInput = force || !g_wheelDirections.empty() || g_pendingWheelDelta != 0 ||
                                 g_wheelModifierPending.load(std::memory_order_acquire);
    if (!hasPendingInput)
        return false;

    g_wheelDirections.clear();
    g_pendingWheelDelta = 0;
    g_wheelModifierPending.store(false, std::memory_order_release);
    g_wheelReadyAt.store(0, std::memory_order_release);

    const bool modifierWasInjected = g_wheelModifierInjected.load(std::memory_order_acquire);
    const bool deferredRelease = g_deferredItemKeyRelease;
    if (modifierWasInjected || deferredRelease) {
        // Let an already-open bar close safely. Do not preserve any item steps.
        g_wheelBusy = true;
        const ULONGLONG delay = modifierWasInjected ? g_wheelTiming.releaseDelayMs : 0;
        g_wheelReleaseAt.store(GetTickCount64() + delay, std::memory_order_release);
    } else {
        g_wheelBusy = false;
        g_wheelReleaseAt.store(0, std::memory_order_release);
    }
    return true;
}

bool DiscardPendingWheelInput(bool force) {
    std::lock_guard<std::mutex> lock(g_wheelMutex);
    return DiscardPendingWheelInputLocked(force);
}

void HandleWheelDelta(SHORT delta, bool actionHeldAtScroll) {
    if (delta == 0 || !g_wheelEnabled.load(std::memory_order_acquire) || !IsGameForeground())
        return;

    if (actionHeldAtScroll || IsAnyGameplayActionPhysicallyHeld() ||
        ReadXInputHeldActions().gameplayAction) {
        // Scrolling during a charge is ignored, never queued for after the attack.
        DiscardPendingWheelInput(true);
        return;
    }

    bool queuedSelection = false;
    bool injectModifier = false;
    {
        std::lock_guard<std::mutex> lock(g_wheelMutex);
        g_pendingWheelDelta += delta;
        while (std::abs(g_pendingWheelDelta) >= WHEEL_DELTA) {
            const bool scrollUp = g_pendingWheelDelta > 0;
            const WORD direction = scrollUp ? 'E' : kMouseRightAction;
            // The item window uses L + Y/A. E maps to Y; right mouse maps to A.
            // Cancel opposite steps that have not been sent yet. This prevents
            // quick direction corrections from replaying stale selections.
            if (!g_wheelDirections.empty() && g_wheelDirections.back() != direction) {
                g_wheelDirections.pop_back();
            } else if (g_wheelDirections.size() < 8) {
                g_wheelDirections.push_back(direction);
            } else {
                g_pendingWheelDelta = 0;
                break;
            }
            g_pendingWheelDelta += scrollUp ? -WHEEL_DELTA : WHEEL_DELTA;
        }
        queuedSelection = !g_wheelDirections.empty();
        if (queuedSelection)
            g_wheelBusy = true;
        if (g_wheelDirections.size() >= 8 && std::abs(g_pendingWheelDelta) >= WHEEL_DELTA)
            g_pendingWheelDelta = 0;
        const bool preopenPartial = g_wheelTiming.preopenThreshold > 0 &&
                                    std::abs(g_pendingWheelDelta) >= int(g_wheelTiming.preopenThreshold);
        if (queuedSelection || preopenPartial)
            g_wheelBusy = true;
        if (!queuedSelection && !preopenPartial &&
            g_wheelModifierPending.load(std::memory_order_acquire) &&
            !g_wheelModifierInjected.load(std::memory_order_acquire) &&
            !g_deferredItemKeyRelease) {
            // The net wheel movement returned to zero before Q was sent.
            g_wheelModifierPending.store(false, std::memory_order_release);
            g_wheelBusy = false;
            g_wheelReadyAt.store(0, std::memory_order_release);
            g_wheelReleaseAt.store(0, std::memory_order_release);
        }
        if (g_wheelBusy) {
            if (g_itemKeyHeld.load(std::memory_order_acquire)) {
                g_wheelModifierPending.store(false, std::memory_order_release);
                g_wheelReadyAt.store(GetTickCount64(), std::memory_order_release);
            } else if (!g_wheelModifierInjected.load(std::memory_order_acquire) &&
                       !g_wheelModifierPending.load(std::memory_order_acquire)) {
                // Mark Q as pending while holding the queue lock so the worker
                // cannot release the modifier between queueing and injection.
                if (IsAnyGameplayActionPhysicallyHeld()) {
                    // Opening the item bar or sending Y/A can interrupt an action.
                    // Defer while X/A/Y is physically held (including GS charge on X).
                    g_wheelModifierPending.store(true, std::memory_order_release);
                } else {
                    g_wheelModifierInjected.store(true, std::memory_order_release);
                    injectModifier = true;
                }
                g_wheelReadyAt.store(std::numeric_limits<ULONGLONG>::max(), std::memory_order_release);
            }
            g_wheelReleaseAt.store(GetTickCount64() + g_wheelTiming.releaseDelayMs,
                                   std::memory_order_release);
        }
    }

    if (!queuedSelection && !injectModifier)
        return;

    if (injectModifier) {
        if (IsGameForeground() && SendKey('Q', true)) {
            std::lock_guard<std::mutex> lock(g_wheelMutex);
            if (g_wheelBusy && g_wheelModifierInjected.load(std::memory_order_acquire)) {
                const ULONGLONG delay = g_itemKeyHeld.load(std::memory_order_acquire)
                                            ? 0
                                            : g_wheelTiming.initialDelayMs;
                g_wheelReadyAt.store(GetTickCount64() + delay, std::memory_order_release);
            }
        } else {
            std::lock_guard<std::mutex> lock(g_wheelMutex);
            g_wheelDirections.clear();
            g_pendingWheelDelta = 0;
            g_wheelBusy = false;
            const bool deferredRelease = std::exchange(g_deferredItemKeyRelease, false);
            g_wheelModifierInjected.store(false, std::memory_order_release);
            g_wheelModifierPending.store(false, std::memory_order_release);
            g_wheelReleaseAt.store(0, std::memory_order_release);
            g_wheelReadyAt.store(0, std::memory_order_release);
            if (deferredRelease && IsGameForeground())
                SendKey('Q', false);
        }
    }
}

bool ReleaseWheelModifier(bool force) {
    const bool gameFocused = IsGameForeground();
    std::lock_guard<std::mutex> lock(g_wheelMutex);
    const bool modifierWasInjected = g_wheelModifierInjected.exchange(false, std::memory_order_acq_rel);
    g_wheelModifierPending.store(false, std::memory_order_release);
    const bool sendRelease = gameFocused && modifierWasInjected &&
                             (force || !g_itemKeyHeld.load(std::memory_order_acquire));
    if (sendRelease)
        SendKey('Q', false);
    g_wheelReadyAt.store(0, std::memory_order_release);
    return sendRelease;
}

void PickupWorker() {
    bool pickupPressed = false;
    bool wheelArrowPressed = false;
    WORD activeWheelKey = 0;
    bool activeWheelMouse = false;
    auto nextPickup = Clock::time_point::min();
    auto releasePickup = Clock::time_point::min();
    auto nextWheelPress = Clock::time_point::min();
    auto releaseWheelArrow = Clock::time_point::min();
    bool lastGameFocused = false;
    bool reportedPickupHeld = false;

    while (g_running.load(std::memory_order_acquire)) {
        const auto now = Clock::now();
        const bool gameFocused = IsGameForeground();
        const XInputHeldActions xinput = gameFocused ? ReadXInputHeldActions() : XInputHeldActions{};
        g_controllerActionHeld.store(xinput.gameplayAction,
                                     std::memory_order_release);
        const bool pickupHeld = gameFocused &&
                                (g_pickupHeld.load(std::memory_order_acquire) ||
                                 xinput.pickupButton);

        if (gameFocused != lastGameFocused) {
            Log(gameFocused ? "OceanTriRecomp esta enfocada; el helper puede enviar controles." :
                              "OceanTriRecomp no esta enfocada; controles pausados.");
            lastGameFocused = gameFocused;
            if (!gameFocused)
                g_pickupHeld.store(false, std::memory_order_release);
        }

        const ULONGLONG wheelReleaseAt = g_wheelReleaseAt.load(std::memory_order_acquire);
        if (!gameFocused) {
            {
                std::lock_guard<std::mutex> lock(g_wheelMutex);
                g_wheelDirections.clear();
                g_pendingWheelDelta = 0;
                g_wheelBusy = false;
                g_deferredItemKeyRelease = false;
                g_wheelModifierInjected.store(false, std::memory_order_release);
                g_wheelModifierPending.store(false, std::memory_order_release);
                g_wheelReadyAt.store(0, std::memory_order_release);
                g_wheelReleaseAt.store(0, std::memory_order_release);
            }
            if (wheelArrowPressed) {
                // Losing focus resets the game's input state. Do not send a global
                // mouse/key release into whichever application became foreground.
                wheelArrowPressed = false;
                activeWheelKey = 0;
                activeWheelMouse = false;
            }
            nextWheelPress = Clock::time_point::min();
            g_itemKeyHeld.store(false, std::memory_order_release);
        } else {
            // A charge may begin after a scroll was queued. Drop those steps too,
            // so they cannot replay when the player releases the attack.
            if (IsAnyGameplayActionPhysicallyHeld())
                DiscardPendingWheelInput(false);

            bool sendPendingModifier = false;
            {
                std::lock_guard<std::mutex> lock(g_wheelMutex);
                if (g_wheelBusy && g_wheelModifierPending.load(std::memory_order_acquire)) {
                    if (g_itemKeyHeld.load(std::memory_order_acquire)) {
                        // The player opened the item bar themselves while the
                        // helper was waiting; use that physical L press.
                        g_wheelModifierPending.store(false, std::memory_order_release);
                        g_wheelReadyAt.store(GetTickCount64(), std::memory_order_release);
                    } else if (!IsAnyGameplayActionPhysicallyHeld()) {
                        g_wheelModifierPending.store(false, std::memory_order_release);
                        g_wheelModifierInjected.store(true, std::memory_order_release);
                        sendPendingModifier = true;
                    }
                }
            }
            if (sendPendingModifier) {
                if (IsGameForeground() && SendKey('Q', true)) {
                    std::lock_guard<std::mutex> lock(g_wheelMutex);
                    if (g_wheelBusy && g_wheelModifierInjected.load(std::memory_order_acquire)) {
                        const ULONGLONG delay = g_itemKeyHeld.load(std::memory_order_acquire)
                                                    ? 0
                                                    : g_wheelTiming.initialDelayMs;
                        const ULONGLONG nowTick = GetTickCount64();
                        g_wheelReadyAt.store(nowTick + delay, std::memory_order_release);
                        g_wheelReleaseAt.store(nowTick + g_wheelTiming.releaseDelayMs,
                                               std::memory_order_release);
                    }
                } else {
                    std::lock_guard<std::mutex> lock(g_wheelMutex);
                    g_wheelDirections.clear();
                    g_pendingWheelDelta = 0;
                    g_wheelBusy = false;
                    const bool deferredRelease = std::exchange(g_deferredItemKeyRelease, false);
                    g_wheelModifierInjected.store(false, std::memory_order_release);
                    g_wheelModifierPending.store(false, std::memory_order_release);
                    g_wheelReleaseAt.store(0, std::memory_order_release);
                    g_wheelReadyAt.store(0, std::memory_order_release);
                    if (deferredRelease && IsGameForeground())
                        SendKey('Q', false);
                }
            }

            if (wheelArrowPressed && now >= releaseWheelArrow) {
                if (IsGameForeground()) {
                    if (activeWheelMouse) {
                        if (!IsWheelActionPhysicallyHeld(kMouseRightAction))
                            SendMouseRight(false);
                    } else if (!IsWheelActionPhysicallyHeld(activeWheelKey)) {
                        SendKey(activeWheelKey, false);
                    }
                }
                wheelArrowPressed = false;
                activeWheelKey = 0;
                activeWheelMouse = false;
            }

            if (!wheelArrowPressed && now >= nextWheelPress &&
                GetTickCount64() >= g_wheelReadyAt.load(std::memory_order_acquire)) {
                WORD direction = 0;
                bool discardedUnsafeQueue = false;
                {
                    std::lock_guard<std::mutex> lock(g_wheelMutex);
                    if (!g_wheelDirections.empty()) {
                        const bool selectionModifierActive =
                            g_itemKeyHeld.load(std::memory_order_acquire) ||
                            g_wheelModifierInjected.load(std::memory_order_acquire);
                        if (selectionModifierActive) {
                            if (!IsAnyGameplayActionPhysicallyHeld()) {
                                direction = g_wheelDirections.front();
                                g_wheelDirections.pop_front();
                            } else {
                                // Recheck under the queue lock to close the race
                                // between the worker's discard check and this pulse.
                                DiscardPendingWheelInputLocked(false);
                                discardedUnsafeQueue = true;
                            }
                        } else {
                            // Fail closed: never emit Y/A if L is no longer held.
                            g_wheelDirections.clear();
                            g_pendingWheelDelta = 0;
                            g_wheelBusy = false;
                            g_wheelReleaseAt.store(0, std::memory_order_release);
                            g_wheelReadyAt.store(0, std::memory_order_release);
                            discardedUnsafeQueue = true;
                            if (std::exchange(g_deferredItemKeyRelease, false) && IsGameForeground())
                                SendKey('Q', false);
                        }
                    }
                }
                if (discardedUnsafeQueue)
                    nextWheelPress = Clock::time_point::min();
                if (direction != 0) {
                    activeWheelKey = direction;
                    activeWheelMouse = direction == kMouseRightAction;
                    const bool sent = IsGameForeground() &&
                                      (activeWheelMouse ? SendMouseRight(true, true)
                                                        : SendKey(activeWheelKey, true, true));
                    if (sent) {
                        wheelArrowPressed = true;
                        releaseWheelArrow = now + std::chrono::milliseconds(g_wheelTiming.pulseMs);
                        nextWheelPress = now + std::chrono::milliseconds(g_wheelTiming.stepIntervalMs);
                    } else {
                        activeWheelKey = 0;
                        activeWheelMouse = false;
                    }
                }
            }

            if (wheelReleaseAt != 0 && GetTickCount64() >= wheelReleaseAt && !wheelArrowPressed) {
                bool deferredRelease = false;
                bool finishedWheelSequence = false;
                {
                    std::lock_guard<std::mutex> lock(g_wheelMutex);
                    const ULONGLONG latestReleaseAt = g_wheelReleaseAt.load(std::memory_order_acquire);
                    if (latestReleaseAt != 0 && GetTickCount64() >= latestReleaseAt &&
                        g_wheelDirections.empty() && !wheelArrowPressed) {
                        const bool modifierWasInjected =
                            g_wheelModifierInjected.exchange(false, std::memory_order_acq_rel);
                        const bool physicalModifierHeld = g_itemKeyHeld.load(std::memory_order_acquire);
                        g_wheelModifierPending.store(false, std::memory_order_release);
                        deferredRelease = std::exchange(g_deferredItemKeyRelease, false);
                        g_wheelBusy = false;
                        g_wheelReleaseAt.store(0, std::memory_order_release);
                        g_wheelReadyAt.store(0, std::memory_order_release);
                        // Serialize Q-up with wheel enqueue so a fresh scroll cannot
                        // inherit a release from the previous selection sequence.
                        if ((deferredRelease || (modifierWasInjected && !physicalModifierHeld)) &&
                            IsGameForeground()) {
                            SendKey('Q', false);
                        }
                        finishedWheelSequence = true;
                    }
                }
                if (finishedWheelSequence)
                    nextWheelPress = Clock::time_point::min();
            }
        }

        if (pickupHeld && !reportedPickupHeld)
            Log("F o Confirmar del mando detectado: enviando pulsos breves de F.");
        reportedPickupHeld = pickupHeld;

        if (pickupPressed && (!pickupHeld || now >= releasePickup)) {
            if (gameFocused && IsGameForeground())
                SendKey('F', false);
            pickupPressed = false;
        }

        if (!pickupHeld) {
            nextPickup = Clock::time_point::min();
        } else if (!pickupPressed && now >= nextPickup && IsGameForeground()) {
            pickupPressed = SendKey('F', true);
            if (pickupPressed)
                releasePickup = now + std::chrono::milliseconds(g_wheelTiming.gatherPulseMs);
            nextPickup = now + std::chrono::milliseconds(g_wheelTiming.gatherIntervalMs);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const bool gameFocused = IsGameForeground();
    if (pickupPressed && gameFocused)
        SendKey('F', false);
    if (wheelArrowPressed && gameFocused) {
        if (activeWheelMouse) {
            if (!IsWheelActionPhysicallyHeld(kMouseRightAction))
                SendMouseRight(false);
        } else if (!IsWheelActionPhysicallyHeld(activeWheelKey)) {
            SendKey(activeWheelKey, false);
        }
    }
    ReleaseWheelModifier(true);
}

BOOL WINAPI ConsoleControlHandler(DWORD control) {
    if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT ||
        control == CTRL_CLOSE_EVENT || control == CTRL_SHUTDOWN_EVENT) {
        g_running.store(false, std::memory_order_release);
        if (g_mainThreadId != 0)
            PostThreadMessageW(g_mainThreadId, WM_QUIT, 0, 0);
        return TRUE;
    }
    return FALSE;
}
} // namespace

int main() {
    g_mainThreadId = GetCurrentThreadId();
    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
    if (!EnsureSingleInstance())
        return 0;
    LoadWheelTiming();
    LoadXInput();

    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE); // Create the queue before the hook posts wheel deltas.

    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, GetModuleHandleW(nullptr), 0);
    if (g_keyboardHook == nullptr) {
        std::cerr << "No se pudo instalar el hook del teclado. Error de Windows: "
                  << GetLastError() << '\n';
        return 1;
    }

    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, GetModuleHandleW(nullptr), 0);
    if (g_mouseHook == nullptr) {
        std::cerr << "No se pudo instalar el hook del raton. Error de Windows: "
                  << GetLastError() << '\n';
        UnhookWindowsHookEx(g_keyboardHook);
        return 1;
    }
    SnapshotPhysicalActionState();

    const bool wheelHotkeyRegistered = RegisterHotKey(nullptr, 2, MOD_NOREPEAT, VK_F8) != FALSE;
    if (!wheelHotkeyRegistered)
        Log("F8 no se pudo registrar; la rueda queda activa sin alternancia.");

    if (!RegisterHotKey(nullptr, 1, MOD_NOREPEAT, VK_F9)) {
        std::cerr << "No se pudo registrar F9 para cerrar el helper. Error de Windows: "
                  << GetLastError() << '\n';
        if (wheelHotkeyRegistered)
            UnregisterHotKey(nullptr, 2);
        UnhookWindowsHookEx(g_mouseHook);
        UnhookWindowsHookEx(g_keyboardHook);
        return 1;
    }

    std::cout << "MH3WorldControls QOL activo. Abre OceanTriRecomp para usarlo.\n"
                 "F: repetir recoleccion | rueda: cambiar objeto sin mantener Q\n"
                 "F8: pausar/reactivar la rueda | F9: salir\n"
                 "La rueda se convierte en pulsos Q + E/clic derecho (Y/A) para seleccionar.\n"
                 "Los giros durante acciones sostenidas se descartan para evitar ataques accidentales.\n"
                 "Los pasos opuestos pendientes se cancelan al corregir el sentido del giro.\n"
              << "Rueda: espera " << g_wheelTiming.initialDelayMs << " ms, pulso "
              << g_wheelTiming.pulseMs << " ms, paso " << g_wheelTiming.stepIntervalMs
              << " ms, preapertura " << g_wheelTiming.preopenThreshold
              << " unidades. Recoleccion: pulso " << g_wheelTiming.gatherPulseMs
              << " ms cada " << g_wheelTiming.gatherIntervalMs
              << " ms. Configura MH3WorldControls.ini junto al helper.\n";

    std::thread worker(PickupWorker);
    while (g_running.load(std::memory_order_acquire)) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
            break;
        if (message.message == kWheelDeltaMessage) {
            const SHORT delta = static_cast<SHORT>(static_cast<WORD>(message.wParam));
            HandleWheelDelta(delta, message.lParam != 0);
            continue;
        }
        if (message.message == WM_HOTKEY) {
            if (message.wParam == 1)
                break;
            if (message.wParam == 2) {
                const bool enabled = !g_wheelEnabled.load(std::memory_order_acquire);
                g_wheelEnabled.store(enabled, std::memory_order_release);
                Log(enabled ? "Rueda activada." :
                              "Rueda pausada; la secuencia actual terminara de forma segura.");
            }
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_running.store(false, std::memory_order_release);
    worker.join();
    ReleaseWheelModifier(true);
    UnregisterHotKey(nullptr, 1);
    if (wheelHotkeyRegistered)
        UnregisterHotKey(nullptr, 2);
    UnhookWindowsHookEx(g_mouseHook);
    UnhookWindowsHookEx(g_keyboardHook);
    if (g_xinputModule != nullptr)
        FreeLibrary(g_xinputModule);
    CloseHandle(g_instanceGuard);
    g_instanceGuard = nullptr;
    return 0;
}
