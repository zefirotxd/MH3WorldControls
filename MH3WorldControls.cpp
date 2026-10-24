#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include <commctrl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <cwchar>
#include <deque>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <shellapi.h>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

constexpr wchar_t kGameExecutable[] = L"OceanTriRecomp.exe";
constexpr UINT kWheelDeltaMessage = WM_APP + 0x31;
constexpr UINT kUiStatusMessage = WM_APP + 0x32;
constexpr UINT kTrayCallbackMessage = WM_APP + 0x33;
constexpr int kToggleWheelButton = 1001;
constexpr int kOpenConfigButton = 1002;
constexpr int kExitButton = 1003;
constexpr wchar_t kWindowClassName[] = L"MH3WorldControlsWindow";
constexpr wchar_t kSettingsWindowClassName[] = L"MH3WorldControlsSettingsWindow";
constexpr UINT_PTR kTrayIconId = 1;
constexpr int kSettingsGatherButton = 2001;
constexpr int kSettingsWheelPreset = 2009;
constexpr int kSettingsWheelInitialDelay = 2001 + 1;
constexpr int kSettingsWheelPulse = 2001 + 2;
constexpr int kSettingsWheelStep = 2001 + 3;
constexpr int kSettingsWheelRelease = 2001 + 4;
constexpr int kSettingsPreopenThreshold = 2001 + 5;
constexpr int kSettingsGatherPulse = 2001 + 6;
constexpr int kSettingsGatherInterval = 2001 + 7;
constexpr int kSettingsSave = 2010;
constexpr int kSettingsDefaults = 2011;
constexpr int kSettingsCancel = 2012;
constexpr int kSettingsGatherController = 2013;
constexpr int kSettingsExtraGuardKeys = 2014;
constexpr int kSettingsWheelInvert = 2015;
constexpr int kSettingsWheelSensitivity = 2016;
constexpr int kSettingsControllerNotifications = 2017;
constexpr int kTrayShow = 3001;
constexpr int kTraySettings = 3002;
constexpr int kTrayToggleWheel = 3003;
constexpr int kTrayExit = 3004;
constexpr int kTrayCopyDiagnostics = 3005;
constexpr int kCopyDiagnosticsButton = 1004;
constexpr wchar_t kAppVersion[] = L"0.1.0-preview.9";
constexpr int kMainLogicalWidth = 720;
constexpr int kMainLogicalHeight = 540;
constexpr int kSettingsLogicalWidth = 520;
constexpr int kSettingsLogicalHeight = 665;
constexpr int kMaxExtraGuardKeys = 8;
constexpr int64_t kWheelDeltaScale = 100;
constexpr int64_t kScaledWheelNotch = int64_t(WHEEL_DELTA) * kWheelDeltaScale;
std::atomic<WORD> g_controllerGatherButton{XINPUT_GAMEPAD_B};
std::atomic<int> g_gatherControllerIndex{-1}; // -1 means any connected controller.
std::atomic<bool> g_controllerNotificationsEnabled{true};
std::array<std::atomic<int>, kMaxExtraGuardKeys> g_extraGuardKeys{};
std::atomic<DWORD> g_extraGuardKeyCount{0};

struct WheelTiming {
    DWORD initialDelayMs = 300;
    DWORD pulseMs = 50;
    DWORD stepIntervalMs = 150;
    DWORD releaseDelayMs = 550;
    DWORD preopenThreshold = 60;
    DWORD gatherPulseMs = 60;
    DWORD gatherIntervalMs = 300;
    DWORD wheelSensitivityPercent = 100;
    bool invertWheel = false;
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
std::atomic<bool> g_wheelBusySnapshot{false};
std::atomic<bool> g_deferredItemKeyRelease{false};
std::atomic<ULONGLONG> g_wheelReleaseAt{0};
std::atomic<ULONGLONG> g_wheelReadyAt{0};
std::atomic<DWORD> g_gameProcessId{0};
std::atomic<DWORD> g_connectedControllerCount{0};
std::atomic<DWORD> g_connectedControllerMask{0};
std::atomic<DWORD> g_gatherButtonHeldMask{0};
std::atomic<DWORD> g_droppedWheelSteps{0};
std::mutex g_wheelMutex;
std::mutex g_foregroundCacheMutex;
std::mutex g_statusMutex;
std::mutex g_settingsMutex;
std::mutex g_fileLogMutex;
std::deque<WORD> g_wheelDirections;
std::wstring g_configPath;
std::wstring g_latestLog;
std::wstring g_extraGuardKeysText;
int64_t g_pendingWheelDelta = 0;
bool g_wheelBusy = false;
DWORD g_cachedForegroundPid = 0;
bool g_cachedForegroundIsGame = false;
Clock::time_point g_foregroundCacheUpdatedAt{};
DWORD g_mainThreadId = 0;
DWORD g_hookThreadId = 0;
UINT g_uiDpi = 96;
HWND g_mainWindow = nullptr;
HWND g_settingsWindow = nullptr;
HWND g_toggleWheelControl = nullptr;
HFONT g_uiFont = nullptr;
HFONT g_uiBoldFont = nullptr;
HFONT g_uiPaintFont = nullptr;
HFONT g_uiPaintBoldFont = nullptr;
HBRUSH g_settingsEditBrush = nullptr;
HBRUSH g_settingsListBrush = nullptr;
NOTIFYICONDATAW g_trayIconData{};
bool g_trayIconAdded = false;
bool g_trayIconVersion4 = false;
bool g_controllerStateInitialized = false;
DWORD g_lastNotifiedControllerMask = 0;
UINT g_taskbarCreatedMessage = 0;
bool g_wheelHotkeyRegistered = false;
bool g_exitHotkeyRegistered = false;
HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseHook = nullptr;
HANDLE g_instanceGuard = nullptr;
HANDLE g_hookReadyEvent = nullptr;
std::atomic<DWORD> g_hookErrorCode{ERROR_SUCCESS};
HMODULE g_xinputModule = nullptr;
using XInputGetStateProc = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
XInputGetStateProc g_xinputGetState = nullptr;
std::mutex g_xinputStateMutex;
std::array<bool, XUSER_MAX_COUNT> g_xinputConnected{};
Clock::time_point g_nextXinputDiscovery{};
constexpr WORD kMouseRightAction = 0xFFFF;
constexpr ULONG_PTR kWheelActionTag = 0x4D483357; // "MH3W"

void Log(const std::string& message);
void OpenSettingsWindow();

constexpr wchar_t kDpiPropertyName[] = L"MH3WorldControls.WindowDpi";

int ScaleUi(int value) {
    return MulDiv(value, static_cast<int>(g_uiDpi), 96);
}

void EnableDpiAwareness() {
    using SetDpiContextProc = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto setDpiContext = reinterpret_cast<SetDpiContextProc>(
        user32 != nullptr ? GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr);
    if (setDpiContext != nullptr &&
        setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return;
    SetProcessDPIAware();
}

UINT GetSystemDpiSafe() {
    using GetDpiForSystemProc = UINT(WINAPI*)();
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto getDpiForSystem = reinterpret_cast<GetDpiForSystemProc>(
        user32 != nullptr ? GetProcAddress(user32, "GetDpiForSystem") : nullptr);
    if (getDpiForSystem != nullptr) {
        const UINT dpi = getDpiForSystem();
        if (dpi != 0)
            return dpi;
    }
    const HDC screen = GetDC(nullptr);
    if (screen == nullptr)
        return 96;
    const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}

BOOL AdjustWindowRectForDpiSafe(LPRECT rect, DWORD style, BOOL hasMenu,
                                DWORD extendedStyle, UINT dpi) {
    using AdjustWindowRectForDpiProc = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto adjustForDpi = reinterpret_cast<AdjustWindowRectForDpiProc>(
        user32 != nullptr ? GetProcAddress(user32, "AdjustWindowRectExForDpi") : nullptr);
    return adjustForDpi != nullptr
               ? adjustForDpi(rect, style, hasMenu, extendedStyle, dpi)
               : AdjustWindowRectEx(rect, style, hasMenu, extendedStyle);
}

bool CreateUiFonts(UINT dpi) {
    const int scaledNormalHeight = -MulDiv(16, static_cast<int>(dpi), 96);
    const int scaledBoldHeight = -MulDiv(18, static_cast<int>(dpi), 96);
    HFONT normal = CreateFontW(scaledNormalHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT bold = CreateFontW(scaledBoldHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT paintNormal = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT paintBold = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (normal == nullptr || bold == nullptr || paintNormal == nullptr || paintBold == nullptr) {
        for (HGDIOBJ font : {reinterpret_cast<HGDIOBJ>(normal), reinterpret_cast<HGDIOBJ>(bold),
                             reinterpret_cast<HGDIOBJ>(paintNormal), reinterpret_cast<HGDIOBJ>(paintBold)}) {
            if (font != nullptr)
                DeleteObject(font);
        }
        return false;
    }
    for (HGDIOBJ font : {reinterpret_cast<HGDIOBJ>(g_uiFont), reinterpret_cast<HGDIOBJ>(g_uiBoldFont),
                         reinterpret_cast<HGDIOBJ>(g_uiPaintFont),
                         reinterpret_cast<HGDIOBJ>(g_uiPaintBoldFont)}) {
        if (font != nullptr)
            DeleteObject(font);
    }
    g_uiFont = normal;
    g_uiBoldFont = bold;
    g_uiPaintFont = paintNormal;
    g_uiPaintBoldFont = paintBold;
    return true;
}

BOOL CALLBACK ApplyFontToChild(HWND child, LPARAM) {
    const int id = GetDlgCtrlID(child);
    const bool bold = id == kToggleWheelButton || id == kOpenConfigButton ||
                      id == kCopyDiagnosticsButton || id == kExitButton ||
                      id == kSettingsSave || id == kSettingsDefaults || id == kSettingsCancel;
    SendMessageW(child, WM_SETFONT,
                 reinterpret_cast<WPARAM>(bold ? g_uiBoldFont : g_uiFont), TRUE);
    return TRUE;
}

void ApplyFontsToChildren(HWND parent) {
    if (parent != nullptr)
        EnumChildWindows(parent, ApplyFontToChild, 0);
}

struct DpiScaleContext {
    HWND parent = nullptr;
    UINT oldDpi = 96;
    UINT newDpi = 96;
};

BOOL CALLBACK ScaleChildForDpi(HWND child, LPARAM parameter) {
    const auto* context = reinterpret_cast<const DpiScaleContext*>(parameter);
    RECT screenRect{};
    if (!GetWindowRect(child, &screenRect))
        return TRUE;
    POINT points[2]{{screenRect.left, screenRect.top}, {screenRect.right, screenRect.bottom}};
    MapWindowPoints(HWND_DESKTOP, context->parent, points, 2);
    const int left = MulDiv(points[0].x, static_cast<int>(context->newDpi),
                            static_cast<int>(context->oldDpi));
    const int top = MulDiv(points[0].y, static_cast<int>(context->newDpi),
                           static_cast<int>(context->oldDpi));
    const int width = MulDiv(points[1].x - points[0].x, static_cast<int>(context->newDpi),
                             static_cast<int>(context->oldDpi));
    const int height = MulDiv(points[1].y - points[0].y, static_cast<int>(context->newDpi),
                              static_cast<int>(context->oldDpi));
    SetWindowPos(child, nullptr, left, top, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    return TRUE;
}

void HandleDpiChanged(HWND window, UINT newDpi, const RECT* suggestedBounds) {
    UINT oldDpi = static_cast<UINT>(reinterpret_cast<ULONG_PTR>(GetPropW(window, kDpiPropertyName)));
    if (oldDpi == 0)
        oldDpi = g_uiDpi;
    if (newDpi == 0)
        newDpi = oldDpi;
    if (newDpi != g_uiDpi) {
        g_uiDpi = newDpi;
        if (CreateUiFonts(g_uiDpi)) {
            ApplyFontsToChildren(g_mainWindow);
            ApplyFontsToChildren(g_settingsWindow);
        }
    }
    if (oldDpi != newDpi) {
        const DpiScaleContext context{window, oldDpi, newDpi};
        EnumChildWindows(window, ScaleChildForDpi,
                         reinterpret_cast<LPARAM>(&context));
    }
    SetPropW(window, kDpiPropertyName, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(newDpi)));
    if (suggestedBounds != nullptr) {
        SetWindowPos(window, nullptr, suggestedBounds->left, suggestedBounds->top,
                     suggestedBounds->right - suggestedBounds->left,
                     suggestedBounds->bottom - suggestedBounds->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateRect(window, nullptr, FALSE);
}

std::wstring GetExecutablePath() {
    std::vector<wchar_t> path(512);
    while (path.size() <= 32768) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                                 static_cast<DWORD>(path.size()));
        if (length == 0)
            return {};
        if (length < path.size())
            return std::wstring(path.data(), length);
        path.resize(path.size() * 2);
    }
    return {};
}

std::wstring GetExecutableDirectory() {
    const std::wstring executablePath = GetExecutablePath();
    const size_t separator = executablePath.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring{} : executablePath.substr(0, separator);
}

void InitializeConfigPath() {
    const std::wstring executableDirectory = GetExecutableDirectory();
    const std::wstring legacyPath = executableDirectory.empty()
                                        ? std::wstring{}
                                        : executableDirectory + L"\\MH3WorldControls.ini";

    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required > 1) {
        std::vector<wchar_t> localAppData(required);
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), required);
        if (length > 0 && length < required) {
            const std::wstring configDirectory = std::wstring(localAppData.data(), length) +
                                                  L"\\MH3WorldControls";
            if (CreateDirectoryW(configDirectory.c_str(), nullptr) ||
                GetLastError() == ERROR_ALREADY_EXISTS) {
                const DWORD attributes = GetFileAttributesW(configDirectory.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES &&
                    (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                    g_configPath = configDirectory + L"\\MH3WorldControls.ini";
                    const DWORD configAttributes = GetFileAttributesW(g_configPath.c_str());
                    if (configAttributes == INVALID_FILE_ATTRIBUTES && !legacyPath.empty() &&
                        CompareStringOrdinal(g_configPath.c_str(), -1, legacyPath.c_str(), -1,
                                             TRUE) != CSTR_EQUAL &&
                        GetFileAttributesW(legacyPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        if (CopyFileW(legacyPath.c_str(), g_configPath.c_str(), TRUE))
                            Log("Configuracion anterior migrada a LocalAppData.");
                        else
                            Log("No se pudo migrar el INI anterior; se usaran los valores seguros.");
                    }
                    return;
                }
            }
        }
    }

    g_configPath = legacyPath;
    Log(g_configPath.empty()
            ? "No se pudo determinar una carpeta para MH3WorldControls.ini."
            : "LocalAppData no esta disponible; se usara la carpeta del helper para el INI.");
}

std::wstring Trim(const std::wstring& value) {
    const size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
        return {};
    const size_t last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

int ParseGuardKeyToken(std::wstring token) {
    token = Trim(token);
    std::transform(token.begin(), token.end(), token.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
    if (token.size() == 1 && ((token[0] >= L'A' && token[0] <= L'Z') ||
                              (token[0] >= L'0' && token[0] <= L'9')))
        return static_cast<int>(token[0]);
    if (token == L"SPACE") return VK_SPACE;
    if (token == L"TAB") return VK_TAB;
    if (token == L"ENTER") return VK_RETURN;
    if (token == L"ESC") return VK_ESCAPE;
    if (token == L"BACKSPACE") return VK_BACK;
    if (token == L"SHIFT") return VK_SHIFT;
    if (token == L"CTRL") return VK_CONTROL;
    if (token == L"ALT") return VK_MENU;
    if (token == L"CAPSLOCK") return VK_CAPITAL;
    if (token == L"INSERT") return VK_INSERT;
    if (token == L"DELETE") return VK_DELETE;
    if (token == L"HOME") return VK_HOME;
    if (token == L"END") return VK_END;
    if (token == L"PAGEUP") return VK_PRIOR;
    if (token == L"PAGEDOWN") return VK_NEXT;
    if (token == L"UP") return VK_UP;
    if (token == L"DOWN") return VK_DOWN;
    if (token == L"LEFT") return VK_LEFT;
    if (token == L"RIGHT") return VK_RIGHT;
    if (token.size() >= 2 && token[0] == L'F') {
        wchar_t* end = nullptr;
        const long number = std::wcstol(token.c_str() + 1, &end, 10);
        if (end != token.c_str() + 1 && *end == L'\0' && number >= 1 && number <= 24)
            return VK_F1 + static_cast<int>(number) - 1;
    }
    if (token.size() == 7 && token.compare(0, 6, L"NUMPAD") == 0 &&
        token[6] >= L'0' && token[6] <= L'9')
        return VK_NUMPAD0 + static_cast<int>(token[6] - L'0');
    return 0;
}

bool ParseExtraGuardKeys(const std::wstring& text, std::array<int, kMaxExtraGuardKeys>& keys,
                         DWORD& count) {
    keys.fill(0);
    count = 0;
    size_t start = 0;
    while (start < text.size()) {
        const size_t separator = text.find(L',', start);
        const std::wstring token = Trim(text.substr(start, separator == std::wstring::npos
                                                             ? std::wstring::npos
                                                             : separator - start));
        const int key = ParseGuardKeyToken(token);
        if (key == 0 || count >= kMaxExtraGuardKeys)
            return false;
        if (std::find(keys.begin(), keys.begin() + count, key) == keys.begin() + count)
            keys[count++] = key;
        if (separator == std::wstring::npos)
            break;
        start = separator + 1;
        if (start == text.size())
            return false;
    }
    return true;
}

void ApplyExtraGuardKeys(const std::array<int, kMaxExtraGuardKeys>& keys, DWORD count) {
    for (DWORD index = 0; index < kMaxExtraGuardKeys; ++index)
        g_extraGuardKeys[index].store(index < count ? keys[index] : 0, std::memory_order_release);
    g_extraGuardKeyCount.store(count, std::memory_order_release);
}

WheelTiming GetWheelTiming() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    return g_wheelTiming;
}

void ApplyWheelTiming(const WheelTiming& timing) {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_wheelTiming = timing;
}

void SetWheelBusyLocked(bool busy) {
    g_wheelBusy = busy;
    g_wheelBusySnapshot.store(busy, std::memory_order_release);
}

void LoadWheelTiming() {
    InitializeConfigPath();
    if (g_configPath.empty()) {
        Log("No se pudo localizar MH3WorldControls.ini; se usaran los tiempos seguros por defecto.");
        return;
    }

    const auto readSetting = [](const wchar_t* name, DWORD defaultValue,
                                DWORD minimum, DWORD maximum) {
        const UINT raw = GetPrivateProfileIntW(L"Wheel", name, defaultValue, g_configPath.c_str());
        return std::clamp<DWORD>(raw, minimum, maximum);
    };

    // A short L lead-in caused Y/A to reach the game before the item bar opened,
    // which can use the selected item. Keep the lead-in and release guard bounded.
    g_wheelTiming.initialDelayMs = readSetting(L"initial_delay_ms", 300, 300, 1000);
    g_wheelTiming.pulseMs = readSetting(L"pulse_ms", 50, 50, 100);
    g_wheelTiming.stepIntervalMs = readSetting(L"step_interval_ms", 150, 100, 600);
    g_wheelTiming.stepIntervalMs = std::max(g_wheelTiming.stepIntervalMs, g_wheelTiming.pulseMs);
    g_wheelTiming.releaseDelayMs = readSetting(L"release_delay_ms", 550, 550, 1000);
    g_wheelTiming.wheelSensitivityPercent = readSetting(L"sensitivity_percent", 100, 50, 200);
    g_wheelTiming.invertWheel = GetPrivateProfileIntW(L"Wheel", L"invert", 0,
                                                       g_configPath.c_str()) != 0;
    const UINT rawPreopenThreshold = GetPrivateProfileIntW(L"Wheel", L"preopen_threshold", 60,
                                                            g_configPath.c_str());
    g_wheelTiming.preopenThreshold = std::min<DWORD>(rawPreopenThreshold, WHEEL_DELTA - 1);

    wchar_t gatherButton[16]{};
    GetPrivateProfileStringW(L"Controller", L"gather_button", L"B", gatherButton,
                              static_cast<DWORD>(std::size(gatherButton)), g_configPath.c_str());
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
        GetPrivateProfileIntW(L"Controller", L"gather_pulse_ms", 60, g_configPath.c_str()), 50, 100);
    g_wheelTiming.gatherIntervalMs = std::clamp<DWORD>(
        GetPrivateProfileIntW(L"Controller", L"gather_interval_ms", 300, g_configPath.c_str()), 200, 1000);
    g_wheelTiming.gatherIntervalMs = std::max(g_wheelTiming.gatherIntervalMs,
                                              g_wheelTiming.gatherPulseMs);

    wchar_t controllerIndex[16]{};
    GetPrivateProfileStringW(L"Controller", L"gather_controller", L"any", controllerIndex,
                              static_cast<DWORD>(std::size(controllerIndex)), g_configPath.c_str());
    if (CompareStringOrdinal(controllerIndex, -1, L"any", -1, TRUE) == CSTR_EQUAL) {
        g_gatherControllerIndex.store(-1, std::memory_order_release);
    } else {
        wchar_t* end = nullptr;
        const long parsedIndex = std::wcstol(controllerIndex, &end, 10);
        g_gatherControllerIndex.store(end != controllerIndex && *end == L'\0' &&
                                      parsedIndex >= 0 && parsedIndex < XUSER_MAX_COUNT
                                          ? static_cast<int>(parsedIndex) : -1,
                                      std::memory_order_release);
    }
    g_controllerNotificationsEnabled.store(
        GetPrivateProfileIntW(L"Controller", L"connection_notifications", 1,
                              g_configPath.c_str()) != 0,
        std::memory_order_release);

    wchar_t extraKeysText[256]{};
    GetPrivateProfileStringW(L"Input", L"extra_guard_keys", L"", extraKeysText,
                              static_cast<DWORD>(std::size(extraKeysText)), g_configPath.c_str());
    std::array<int, kMaxExtraGuardKeys> extraKeys{};
    DWORD extraKeyCount = 0;
    if (ParseExtraGuardKeys(extraKeysText, extraKeys, extraKeyCount)) {
        ApplyExtraGuardKeys(extraKeys, extraKeyCount);
        g_extraGuardKeysText = extraKeysText;
    } else {
        ApplyExtraGuardKeys({}, 0);
        g_extraGuardKeysText.clear();
        Log("Input.extra_guard_keys invalido; se ignoraran las teclas adicionales.");
    }
}

void AppendDiagnosticLog(const std::wstring& message) {
    if (g_configPath.empty())
        return;
    const size_t separator = g_configPath.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return;
    const std::wstring logPath = g_configPath.substr(0, separator) +
                                 L"\\MH3WorldControls.log";
    const std::wstring backupPath = logPath + L".1";

    std::lock_guard<std::mutex> lock(g_fileLogMutex);
    HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 512 * 1024) {
        CloseHandle(file);
        MoveFileExW(logPath.c_str(), backupPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        size.QuadPart = 0;
        GetFileSizeEx(file, &size);
    }

    DWORD written = 0;
    if (size.QuadPart == 0) {
        constexpr wchar_t bom = 0xFEFF;
        WriteFile(file, &bom, sizeof(bom), &written, nullptr);
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t prefix[48]{};
    swprintf_s(prefix, L"[%04u-%02u-%02u %02u:%02u:%02u] ",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    std::wstring line = prefix;
    line += message;
    line += L"\r\n";
    const DWORD byteCount = static_cast<DWORD>(line.size() * sizeof(wchar_t));
    WriteFile(file, line.data(), byteCount, &written, nullptr);
    CloseHandle(file);
}

void Log(const std::string& message) {
    OutputDebugStringA((message + "\n").c_str());
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        g_latestLog.assign(message.begin(), message.end());
    }
    AppendDiagnosticLog(std::wstring(message.begin(), message.end()));
    if (g_mainWindow != nullptr)
        PostMessageW(g_mainWindow, kUiStatusMessage, 0, 0);
}

bool EnsureSingleInstance() {
    constexpr wchar_t kMutexName[] = L"Local\\OceanTriRecomp.MH3WorldControls";
    SetLastError(ERROR_SUCCESS);
    g_instanceGuard = CreateMutexW(nullptr, FALSE, kMutexName);
    if (g_instanceGuard == nullptr) {
        MessageBoxW(nullptr, L"No se pudo iniciar el control de instancia unica.",
                    L"MH3 World Controls", MB_OK | MB_ICONERROR);
        return false;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_instanceGuard);
        g_instanceGuard = nullptr;
        MessageBoxW(nullptr, L"MH3 World Controls ya está abierto. Ciérralo con F9.",
                    L"MH3 World Controls", MB_OK | MB_ICONINFORMATION);
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
    DWORD gatherButtonMask = 0;
};

XInputHeldActions ReadXInputHeldActions() {
    XInputHeldActions held;
    if (g_xinputGetState == nullptr) {
        g_connectedControllerCount.store(0, std::memory_order_release);
        g_connectedControllerMask.store(0, std::memory_order_release);
        g_gatherButtonHeldMask.store(0, std::memory_order_release);
        return held;
    }

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
    const WORD gatherButton = g_controllerGatherButton.load(std::memory_order_acquire);
    const int gatherController = g_gatherControllerIndex.load(std::memory_order_acquire);
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
        if (gatherButton != 0 && (gatherController < 0 ||
                                  gatherController == static_cast<int>(userIndex)) &&
            (pad.wButtons & gatherButton) != 0) {
            held.pickupButton = true;
            held.gatherButtonMask |= 1u << userIndex;
        }
    }
    const DWORD connectedCount = static_cast<DWORD>(
        std::count(g_xinputConnected.begin(), g_xinputConnected.end(), true));
    DWORD connectedMask = 0;
    for (DWORD userIndex = 0; userIndex < XUSER_MAX_COUNT; ++userIndex) {
        if (g_xinputConnected[userIndex])
            connectedMask |= 1u << userIndex;
    }
    g_connectedControllerCount.store(connectedCount, std::memory_order_release);
    g_connectedControllerMask.store(connectedMask, std::memory_order_release);
    g_gatherButtonHeldMask.store(held.gatherButtonMask, std::memory_order_release);
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
    const DWORD extraKeyCount = std::min<DWORD>(
        g_extraGuardKeyCount.load(std::memory_order_acquire),
        static_cast<DWORD>(kMaxExtraGuardKeys));
    for (DWORD index = 0; index < extraKeyCount; ++index) {
        const int key = g_extraGuardKeys[index].load(std::memory_order_acquire);
        if (key != 0 && (GetAsyncKeyState(key) & 0x8000) != 0)
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
                // This path runs inside the low-level hook callback. Use the cached
                // process identity check; do not open/query a process from the hook.
                const bool gameFocused = IsKnownGameForeground();
                if (key->vkCode == 'F') {
                    g_pickupHeld.store(keyDown && gameFocused, std::memory_order_release);
                } else if (key->vkCode == 'E') {
                    // E maps to Y. Track only physical input so a wheel-generated
                    // key-up can never be mistaken for the player's charge release.
                    g_physicalYHeld.store(keyDown, std::memory_order_release);
                } else {
                    g_itemKeyHeld.store(keyDown && gameFocused, std::memory_order_release);
                    if (keyDown) {
                        g_deferredItemKeyRelease = false;
                    } else if (gameFocused &&
                               g_wheelBusySnapshot.load(std::memory_order_acquire)) {
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
    const WheelTiming timing = GetWheelTiming();
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
        SetWheelBusyLocked(true);
        const ULONGLONG delay = modifierWasInjected ? timing.releaseDelayMs : 0;
        g_wheelReleaseAt.store(GetTickCount64() + delay, std::memory_order_release);
    } else {
        SetWheelBusyLocked(false);
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

    if (actionHeldAtScroll || IsAnyGameplayActionPhysicallyHeld()) {
        // Scrolling during a charge is ignored, never queued for after the attack.
        DiscardPendingWheelInput(true);
        return;
    }

    const WheelTiming timing = GetWheelTiming();

    bool queuedSelection = false;
    bool injectModifier = false;
    {
        std::lock_guard<std::mutex> lock(g_wheelMutex);
        int64_t scaledDelta = static_cast<int64_t>(delta) * timing.wheelSensitivityPercent;
        if (timing.invertWheel)
            scaledDelta = -scaledDelta;
        g_pendingWheelDelta += scaledDelta;
        while (g_pendingWheelDelta >= kScaledWheelNotch ||
               g_pendingWheelDelta <= -kScaledWheelNotch) {
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
                const uint64_t pendingMagnitude = static_cast<uint64_t>(
                    g_pendingWheelDelta >= 0 ? g_pendingWheelDelta : -g_pendingWheelDelta);
                const DWORD dropped = static_cast<DWORD>(std::max<uint64_t>(
                    1, pendingMagnitude / static_cast<uint64_t>(kScaledWheelNotch)));
                g_droppedWheelSteps.fetch_add(dropped, std::memory_order_relaxed);
                g_pendingWheelDelta = 0;
                break;
            }
            g_pendingWheelDelta += scrollUp ? -kScaledWheelNotch : kScaledWheelNotch;
        }
        queuedSelection = !g_wheelDirections.empty();
        if (queuedSelection)
            SetWheelBusyLocked(true);
        if (g_wheelDirections.size() >= 8 &&
            (g_pendingWheelDelta >= kScaledWheelNotch ||
             g_pendingWheelDelta <= -kScaledWheelNotch))
            g_pendingWheelDelta = 0;
        const bool preopenPartial = timing.preopenThreshold > 0 &&
            (g_pendingWheelDelta >= int64_t(timing.preopenThreshold) * kWheelDeltaScale ||
             g_pendingWheelDelta <= -int64_t(timing.preopenThreshold) * kWheelDeltaScale);
        if (queuedSelection || preopenPartial)
            SetWheelBusyLocked(true);
        if (!queuedSelection && !preopenPartial &&
            g_wheelModifierPending.load(std::memory_order_acquire) &&
            !g_wheelModifierInjected.load(std::memory_order_acquire) &&
            !g_deferredItemKeyRelease) {
            // The net wheel movement returned to zero before Q was sent.
            g_wheelModifierPending.store(false, std::memory_order_release);
            SetWheelBusyLocked(false);
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
            g_wheelReleaseAt.store(GetTickCount64() + timing.releaseDelayMs,
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
                                            : timing.initialDelayMs;
                g_wheelReadyAt.store(GetTickCount64() + delay, std::memory_order_release);
            }
        } else {
            std::lock_guard<std::mutex> lock(g_wheelMutex);
            g_wheelDirections.clear();
            g_pendingWheelDelta = 0;
            SetWheelBusyLocked(false);
            const bool deferredRelease = g_deferredItemKeyRelease.exchange(false,
                std::memory_order_acq_rel);
            g_wheelModifierInjected.store(false, std::memory_order_release);
            g_wheelModifierPending.store(false, std::memory_order_release);
            g_wheelReleaseAt.store(0, std::memory_order_release);
            g_wheelReadyAt.store(0, std::memory_order_release);
            if (deferredRelease && IsGameForeground())
                SendKey('Q', false);
        }
    }
}

bool ReleaseWheelModifier() {
    const bool gameFocused = IsGameForeground();
    std::lock_guard<std::mutex> lock(g_wheelMutex);
    const bool modifierWasInjected = g_wheelModifierInjected.exchange(false, std::memory_order_acq_rel);
    const bool deferredRelease = g_deferredItemKeyRelease.exchange(false,
        std::memory_order_acq_rel);
    g_wheelModifierPending.store(false, std::memory_order_release);
    SetWheelBusyLocked(false);
    g_wheelDirections.clear();
    g_pendingWheelDelta = 0;
    g_wheelReleaseAt.store(0, std::memory_order_release);
    g_wheelReadyAt.store(0, std::memory_order_release);
    // Never inject Q-up over a physical Q hold, even during shutdown.
    const bool sendRelease = gameFocused && (modifierWasInjected || deferredRelease) &&
                             !g_itemKeyHeld.load(std::memory_order_acquire);
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
    auto nextIdleControllerPoll = Clock::time_point::min();
    bool lastGameFocused = false;
    bool reportedPickupHeld = false;

    while (g_running.load(std::memory_order_acquire)) {
        const auto now = Clock::now();
        const WheelTiming timing = GetWheelTiming();
        const bool gameFocused = IsGameForeground();
        XInputHeldActions xinput{};
        if (gameFocused || now >= nextIdleControllerPoll) {
            xinput = ReadXInputHeldActions();
            nextIdleControllerPoll = now + (gameFocused ? std::chrono::milliseconds(5)
                                                        : std::chrono::milliseconds(250));
        }
        g_controllerActionHeld.store(gameFocused && xinput.gameplayAction,
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
                SetWheelBusyLocked(false);
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
                                                    : timing.initialDelayMs;
                        const ULONGLONG nowTick = GetTickCount64();
                        g_wheelReadyAt.store(nowTick + delay, std::memory_order_release);
                        g_wheelReleaseAt.store(nowTick + timing.releaseDelayMs,
                                               std::memory_order_release);
                    }
                } else {
                    std::lock_guard<std::mutex> lock(g_wheelMutex);
                    g_wheelDirections.clear();
                    g_pendingWheelDelta = 0;
                    SetWheelBusyLocked(false);
                    const bool deferredRelease = g_deferredItemKeyRelease.exchange(false,
                        std::memory_order_acq_rel);
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
                            SetWheelBusyLocked(false);
                            g_wheelReleaseAt.store(0, std::memory_order_release);
                            g_wheelReadyAt.store(0, std::memory_order_release);
                            discardedUnsafeQueue = true;
                            if (g_deferredItemKeyRelease.exchange(false, std::memory_order_acq_rel) &&
                                IsGameForeground())
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
                        releaseWheelArrow = now + std::chrono::milliseconds(timing.pulseMs);
                        nextWheelPress = now + std::chrono::milliseconds(timing.stepIntervalMs);
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
                        deferredRelease = g_deferredItemKeyRelease.exchange(false,
                            std::memory_order_acq_rel);
                        SetWheelBusyLocked(false);
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
                releasePickup = now + std::chrono::milliseconds(timing.gatherPulseMs);
            nextPickup = now + std::chrono::milliseconds(timing.gatherIntervalMs);
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
    ReleaseWheelModifier();
}

constexpr COLORREF kUiBackground = RGB(7, 17, 32);
constexpr COLORREF kUiPanel = RGB(13, 29, 49);
constexpr COLORREF kUiPanelRaised = RGB(17, 38, 61);
constexpr COLORREF kUiBorder = RGB(34, 72, 103);
constexpr COLORREF kUiCyan = RGB(39, 205, 246);
constexpr COLORREF kUiText = RGB(234, 244, 255);
constexpr COLORREF kUiMuted = RGB(143, 169, 194);
constexpr COLORREF kUiGreen = RGB(66, 220, 163);
constexpr COLORREF kUiAmber = RGB(255, 190, 78);

void DrawUiText(HDC dc, const wchar_t* text, RECT rect, COLORREF color, HFONT font,
                UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    const HGDIOBJ previousFont = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rect, format);
    SelectObject(dc, previousFont);
}

void DrawUiCard(HDC dc, RECT rect, COLORREF fill = kUiPanel) {
    const HBRUSH brush = CreateSolidBrush(fill);
    const HPEN pen = CreatePen(PS_SOLID, 1, kUiBorder);
    const HGDIOBJ oldBrush = SelectObject(dc, brush);
    const HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 16, 16);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void UpdateWheelButton() {
    if (g_toggleWheelControl == nullptr)
        return;
    const bool enabled = g_wheelEnabled.load(std::memory_order_acquire);
    SetWindowTextW(g_toggleWheelControl, enabled ? L"Pausar rueda" : L"Reactivar rueda");
    InvalidateRect(g_toggleWheelControl, nullptr, TRUE);
}

void ToggleWheel() {
    const bool enabled = !g_wheelEnabled.load(std::memory_order_acquire);
    g_wheelEnabled.store(enabled, std::memory_order_release);
    Log(enabled ? "Rueda activada." : "Rueda pausada; la secuencia actual terminara de forma segura.");
    UpdateWheelButton();
    if (g_mainWindow != nullptr)
        InvalidateRect(g_mainWindow, nullptr, FALSE);
}

void ShowWindowsError(const wchar_t* action) {
    const DWORD errorCode = GetLastError();
    std::wstring message(action);
    message += L"\nCódigo de Windows: ";
    message += std::to_wstring(errorCode);
    MessageBoxW(g_mainWindow, message.c_str(), L"MH3 World Controls", MB_OK | MB_ICONERROR);
}

void AddTrayIcon(HWND window) {
    g_trayIconData = {};
    g_trayIconData.cbSize = sizeof(g_trayIconData);
    g_trayIconData.hWnd = window;
    g_trayIconData.uID = kTrayIconId;
    // Use the HWND/ID identity: this portable ZIP can be moved between folders.
    g_trayIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIconData.uCallbackMessage = kTrayCallbackMessage;
    g_trayIconData.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wcscpy_s(g_trayIconData.szTip, L"MH3 World Controls");
    g_trayIconAdded = Shell_NotifyIconW(NIM_ADD, &g_trayIconData) != FALSE;
    if (g_trayIconAdded) {
        g_trayIconData.uVersion = NOTIFYICON_VERSION_4;
        g_trayIconVersion4 = Shell_NotifyIconW(NIM_SETVERSION, &g_trayIconData) != FALSE;
    }
}

void NotifyControllerChange(bool connected, DWORD count) {
    std::wstring title;
    std::wstring message;
    if (connected) {
        title = L"Mando conectado";
        if (count != 1) {
            message = L"Se detectó otro mando XInput.";
        } else {
            const WORD button = g_controllerGatherButton.load(std::memory_order_acquire);
            if (button == XINPUT_GAMEPAD_B)
                message = L"XInput listo. Mantén B / Círculo para recolectar.";
            else if (button == XINPUT_GAMEPAD_A)
                message = L"XInput listo. Mantén A / Cruz para recolectar.";
            else if (button == XINPUT_GAMEPAD_X)
                message = L"XInput listo. Mantén X / Cuadrado para recolectar.";
            else if (button == XINPUT_GAMEPAD_Y)
                message = L"XInput listo. Mantén Y / Triángulo para recolectar.";
            else
                message = L"XInput listo. La autorecolección está desactivada en Configuración.";
        }
    } else {
        title = L"Mando desconectado";
        message = count == 0 ? L"No hay mandos XInput conectados."
                             : L"Quedan mandos XInput conectados.";
    }

    const std::wstring notificationLog = title + L": " + message;
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        g_latestLog = notificationLog;
    }
    AppendDiagnosticLog(notificationLog);
    if (g_mainWindow != nullptr)
        PostMessageW(g_mainWindow, kUiStatusMessage, 0, 0);

    if (!g_controllerNotificationsEnabled.load(std::memory_order_acquire))
        return;

    if (g_trayIconAdded) {
        NOTIFYICONDATAW notification = g_trayIconData;
        notification.uFlags = NIF_INFO;
        notification.dwInfoFlags = NIIF_INFO;
        notification.uTimeout = 5000;
        wcscpy_s(notification.szInfoTitle, title.c_str());
        wcscpy_s(notification.szInfo, message.c_str());
        Shell_NotifyIconW(NIM_MODIFY, &notification);
    }
}

void CheckControllerConnectionChange() {
    const DWORD currentMask = g_connectedControllerMask.load(std::memory_order_acquire);
    const DWORD currentCount = g_connectedControllerCount.load(std::memory_order_acquire);
    if (!g_controllerStateInitialized) {
        g_controllerStateInitialized = true;
        if (currentMask != 0)
            NotifyControllerChange(true, currentCount);
    } else if (currentMask != g_lastNotifiedControllerMask) {
        const bool connected = (currentMask & ~g_lastNotifiedControllerMask) != 0;
        NotifyControllerChange(connected, currentCount);
    }
    g_lastNotifiedControllerMask = currentMask;
}

void RemoveTrayIcon() {
    if (g_trayIconAdded) {
        Shell_NotifyIconW(NIM_DELETE, &g_trayIconData);
        g_trayIconAdded = false;
    }
}

void ShowMainWindow(HWND window) {
    ShowWindow(window, SW_SHOWNORMAL);
    SetForegroundWindow(window);
}

void CopyDiagnostics(HWND owner) {
    const WheelTiming timing = GetWheelTiming();
    DWORD pendingSteps = 0;
    {
        std::lock_guard<std::mutex> lock(g_wheelMutex);
        pendingSteps = static_cast<DWORD>(g_wheelDirections.size());
    }
    const DWORD controllerMask = g_connectedControllerMask.load(std::memory_order_acquire);
    const WORD gatherButtonMask = g_controllerGatherButton.load(std::memory_order_acquire);
    const wchar_t* gatherButton = gatherButtonMask == XINPUT_GAMEPAD_A ? L"A" :
                                  gatherButtonMask == XINPUT_GAMEPAD_B ? L"B / Circle" :
                                  gatherButtonMask == XINPUT_GAMEPAD_X ? L"X" :
                                  gatherButtonMask == XINPUT_GAMEPAD_Y ? L"Y" : L"Off";
    std::wstring latestLog;
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        latestLog = g_latestLog;
    }

    std::wstring report = L"MH3 World Controls " + std::wstring(kAppVersion);
    report += L"\r\nGame focused: ";
    report += IsGameForeground() ? L"yes" : L"no";
    report += L"\r\nWheel: ";
    report += g_wheelEnabled.load(std::memory_order_acquire) ? L"enabled" : L"paused";
    report += L"; sensitivity=" + std::to_wstring(timing.wheelSensitivityPercent) + L"%";
    report += timing.invertWheel ? L"; inverted" : L"; normal direction";
    report += L"; interval=" + std::to_wstring(timing.stepIntervalMs) + L"ms";
    report += L"; queued=" + std::to_wstring(pendingSteps);
    report += L"; dropped=" + std::to_wstring(g_droppedWheelSteps.load(std::memory_order_relaxed));
    report += L"\r\nController backend: ";
    report += g_xinputGetState != nullptr ? L"XInput" : L"unavailable";
    report += L"; connected=" + std::to_wstring(
        g_connectedControllerCount.load(std::memory_order_acquire));
    report += L"; slot mask=0x";
    wchar_t maskText[16]{};
    swprintf_s(maskText, L"%lX", static_cast<unsigned long>(controllerMask));
    report += maskText;
    report += L"; gather button held slots=0x";
    const DWORD gatherButtonHeldMask = g_gatherButtonHeldMask.load(std::memory_order_acquire);
    swprintf_s(maskText, L"%lX", static_cast<unsigned long>(gatherButtonHeldMask));
    report += maskText;
    report += L"; gather button=";
    report += gatherButton;
    const int selectedController = g_gatherControllerIndex.load(std::memory_order_acquire);
    report += L"; gather controller=";
    report += selectedController < 0 ? L"any" : std::to_wstring(selectedController + 1);
    report += L"; connection notifications=";
    report += g_controllerNotificationsEnabled.load(std::memory_order_acquire) ? L"on" : L"off";
    report += L"\r\nHooks: keyboard=";
    report += g_keyboardHook != nullptr ? L"active" : L"inactive";
    report += L"; mouse=";
    report += g_mouseHook != nullptr ? L"active" : L"inactive";
    report += L"\r\nConfig file: ";
    report += !g_configPath.empty() && GetFileAttributesW(g_configPath.c_str()) != INVALID_FILE_ATTRIBUTES
                  ? L"present" : L"missing";
    const size_t configSeparator = g_configPath.find_last_of(L"\\/");
    const std::wstring logPath = configSeparator == std::wstring::npos
                                     ? std::wstring{}
                                     : g_configPath.substr(0, configSeparator) +
                                           L"\\MH3WorldControls.log";
    report += L"; rolling log=";
    report += !logPath.empty() && GetFileAttributesW(logPath.c_str()) != INVALID_FILE_ATTRIBUTES
                  ? L"present" : L"not created";
    report += L"\r\nLatest status: ";
    report += latestLog.empty() ? L"none" : latestLog;
    report += L"\r\n";

    if (!OpenClipboard(owner)) {
        MessageBoxW(owner, L"No se pudo abrir el portapapeles.", L"Diagnóstico",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    if (!EmptyClipboard()) {
        CloseClipboard();
        MessageBoxW(owner, L"No se pudo limpiar el portapapeles.", L"Diagnóstico",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    const SIZE_T byteCount = (report.size() + 1) * sizeof(wchar_t);
    HGLOBAL clipboardData = GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (clipboardData == nullptr) {
        CloseClipboard();
        MessageBoxW(owner, L"No hay memoria suficiente para copiar el diagnóstico.",
                    L"Diagnóstico", MB_OK | MB_ICONWARNING);
        return;
    }
    auto* clipboardText = static_cast<wchar_t*>(GlobalLock(clipboardData));
    if (clipboardText == nullptr) {
        GlobalFree(clipboardData);
        CloseClipboard();
        MessageBoxW(owner, L"No se pudo preparar el diagnóstico.", L"Diagnóstico",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    std::copy(report.begin(), report.end(), clipboardText);
    clipboardText[report.size()] = L'\0';
    GlobalUnlock(clipboardData);
    if (SetClipboardData(CF_UNICODETEXT, clipboardData) == nullptr) {
        GlobalFree(clipboardData);
        CloseClipboard();
        MessageBoxW(owner, L"Windows no pudo copiar el diagnóstico.", L"Diagnóstico",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    CloseClipboard();
    Log("Diagnostico copiado al portapapeles.");
}

void ShowTrayMenu(HWND window) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
        return;

    AppendMenuW(menu, MF_STRING, kTrayShow, L"Abrir MH3 World Controls");
    AppendMenuW(menu, MF_STRING, kTraySettings, L"Configuración");
    AppendMenuW(menu, MF_STRING, kTrayToggleWheel,
                g_wheelEnabled.load(std::memory_order_acquire) ? L"Pausar rueda" : L"Reactivar rueda");
    AppendMenuW(menu, MF_STRING, kTrayCopyDiagnostics, L"Copiar diagnóstico");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExit, L"Cerrar helper");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(window);
    const UINT selected = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                         cursor.x, cursor.y, 0, window, nullptr);
    DestroyMenu(menu);
    PostMessageW(window, WM_NULL, 0, 0);

    switch (selected) {
    case kTrayShow:
        ShowMainWindow(window);
        break;
    case kTraySettings:
        OpenSettingsWindow();
        break;
    case kTrayToggleWheel:
        ToggleWheel();
        break;
    case kTrayCopyDiagnostics:
        CopyDiagnostics(window);
        break;
    case kTrayExit:
        PostMessageW(window, WM_CLOSE, 0, 0);
        break;
    }
}

void DrawMainWindow(HDC dc, const RECT& client) {
    const WheelTiming timing = GetWheelTiming();
    HBRUSH background = CreateSolidBrush(kUiBackground);
    FillRect(dc, &client, background);
    DeleteObject(background);

    RECT title{30, 22, client.right - 30, 58};
    DrawUiText(dc, L"MH3 WORLD CONTROLS", title, kUiText, g_uiPaintBoldFont);
    RECT subtitle{31, 58, client.right - 30, 82};
    const std::wstring subtitleText = L"Controles de calidad de vida para OceanTriRecomp · v" +
                                      std::wstring(kAppVersion);
    DrawUiText(dc, subtitleText.c_str(), subtitle, kUiMuted, g_uiPaintFont);

    RECT gameCard{25, 98, client.right - 25, 213};
    DrawUiCard(dc, gameCard, kUiPanelRaised);
    RECT section{45, 111, client.right - 45, 135};
    DrawUiText(dc, L"ESTADO DEL JUEGO", section, kUiCyan, g_uiPaintBoldFont);

    const bool gameFocused = IsGameForeground();
    const COLORREF gameColor = gameFocused ? kUiGreen : kUiAmber;
    const HBRUSH statusBrush = CreateSolidBrush(gameColor);
    RECT dot{47, 151, 59, 163};
    FillRect(dc, &dot, statusBrush);
    DeleteObject(statusBrush);
    RECT gameStatus{70, 144, client.right - 48, 173};
    DrawUiText(dc, gameFocused ? L"OceanTriRecomp está en primer plano" :
                                 L"Abre OceanTriRecomp para activar los controles",
               gameStatus, gameFocused ? kUiGreen : kUiText, g_uiPaintBoldFont);
    RECT gameHint{70, 176, client.right - 48, 198};
    DrawUiText(dc, L"El helper solo envía controles cuando el juego tiene el foco.",
                gameHint, kUiMuted, g_uiPaintFont);

    const int middle = client.right / 2;
    RECT wheelCard{25, 228, middle - 8, 337};
    RECT controllerCard{middle + 8, 228, client.right - 25, 337};
    DrawUiCard(dc, wheelCard);
    DrawUiCard(dc, controllerCard);

    RECT wheelTitle{43, 241, middle - 20, 263};
    DrawUiText(dc, L"CAMBIO CON RUEDA", wheelTitle, kUiCyan, g_uiPaintBoldFont);
    RECT wheelState{43, 271, middle - 20, 296};
    const bool wheelEnabled = g_wheelEnabled.load(std::memory_order_acquire);
    DrawUiText(dc, wheelEnabled ? L"Activo" : L"Pausado", wheelState,
               wheelEnabled ? kUiGreen : kUiAmber, g_uiPaintBoldFont);
    RECT wheelDetail{43, 303, middle - 20, 326};
    wchar_t wheelText[128]{};
    DWORD pendingSteps = 0;
    {
        std::lock_guard<std::mutex> lock(g_wheelMutex);
        pendingSteps = static_cast<DWORD>(g_wheelDirections.size());
    }
    swprintf_s(wheelText, L"%lu%% · cola %lu · perdidos %lu",
               static_cast<unsigned long>(timing.wheelSensitivityPercent),
               static_cast<unsigned long>(pendingSteps),
               static_cast<unsigned long>(g_droppedWheelSteps.load(std::memory_order_relaxed)));
    DrawUiText(dc, wheelText, wheelDetail, kUiMuted, g_uiPaintFont);

    RECT controllerTitle{middle + 26, 241, client.right - 42, 263};
    DrawUiText(dc, L"MANDO Y RECOLECCIÓN", controllerTitle, kUiCyan, g_uiPaintBoldFont);
    RECT controllerState{middle + 26, 271, client.right - 42, 296};
    const DWORD controllerCount = g_connectedControllerCount.load(std::memory_order_acquire);
    wchar_t controllerText[80]{};
    if (g_xinputGetState == nullptr)
        wcscpy_s(controllerText, L"XInput no disponible");
    else if (controllerCount == 0)
        wcscpy_s(controllerText, L"Conecta un mando XInput");
    else
        swprintf_s(controllerText, L"%lu mando(s) XInput conectado(s)",
                   static_cast<unsigned long>(controllerCount));
    DrawUiText(dc, controllerText, controllerState,
                controllerCount > 0 ? kUiGreen : kUiText, g_uiPaintBoldFont);
    RECT controllerDetail{middle + 26, 298, client.right - 42, 316};
    wchar_t gatherDetail[128]{};
    const WORD gatherButtonMask = g_controllerGatherButton.load(std::memory_order_acquire);
    const wchar_t* gatherButton = gatherButtonMask == XINPUT_GAMEPAD_A ? L"A" :
                                  gatherButtonMask == XINPUT_GAMEPAD_B ? L"B / Círculo" :
                                  gatherButtonMask == XINPUT_GAMEPAD_X ? L"X" :
                                  gatherButtonMask == XINPUT_GAMEPAD_Y ? L"Y" : L"Desactivada";
    const int selectedController = g_gatherControllerIndex.load(std::memory_order_acquire);
    swprintf_s(gatherDetail, L"%s · %s · cada %lu ms", gatherButton,
               selectedController < 0 ? L"cualquiera" :
                   (selectedController == 0 ? L"mando 1" : selectedController == 1 ? L"mando 2" :
                    selectedController == 2 ? L"mando 3" : L"mando 4"),
               static_cast<unsigned long>(timing.gatherIntervalMs));
    DrawUiText(dc, gatherDetail, controllerDetail, kUiMuted, g_uiPaintFont,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT gatherButtonStatus{middle + 26, 317, client.right - 42, 335};
    const DWORD selectedSlotMask = selectedController < 0 ? (1u << XUSER_MAX_COUNT) - 1u
                                                           : 1u << selectedController;
    const DWORD connectedMask = g_connectedControllerMask.load(std::memory_order_acquire);
    const DWORD heldMask = g_gatherButtonHeldMask.load(std::memory_order_acquire) & selectedSlotMask;
    wchar_t gatherStatusText[96]{};
    const wchar_t* gatherStatus = L"";
    COLORREF gatherStatusColor = kUiMuted;
    if (gatherButtonMask == 0) {
        gatherStatus = L"Autorecolección desactivada";
    } else if (g_xinputGetState == nullptr) {
        gatherStatus = L"XInput no disponible";
        gatherStatusColor = kUiAmber;
    } else if (selectedController >= 0 &&
               (connectedMask & (1u << selectedController)) == 0) {
        gatherStatus = L"Conecta el mando seleccionado";
        gatherStatusColor = kUiAmber;
    } else if (controllerCount == 0) {
        gatherStatus = L"Conecta un mando XInput";
        gatherStatusColor = kUiAmber;
    } else if (heldMask != 0) {
        gatherStatus = gameFocused ? L"Detectado · recolección activa"
                                   : L"Detectado · abre el juego";
        gatherStatusColor = gameFocused ? kUiGreen : kUiAmber;
    } else {
        swprintf_s(gatherStatusText, L"Mantén %s para probar", gatherButton);
        gatherStatus = gatherStatusText;
    }
    DrawUiText(dc, gatherStatus, gatherButtonStatus, gatherStatusColor, g_uiPaintFont,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT settingsCard{25, 352, client.right - 25, 433};
    DrawUiCard(dc, settingsCard);
    RECT settingsTitle{44, 364, client.right - 44, 387};
    DrawUiText(dc, L"AJUSTES", settingsTitle, kUiCyan, g_uiPaintBoldFont);
    RECT settingsDetail{44, 390, client.right - 44, 410};
    wchar_t settingsText[160]{};
    swprintf_s(settingsText, L"Rueda: %lu ms por paso  ·  Recolección: pulso %lu ms / intervalo %lu ms",
               static_cast<unsigned long>(timing.stepIntervalMs),
               static_cast<unsigned long>(timing.gatherPulseMs),
               static_cast<unsigned long>(timing.gatherIntervalMs));
    DrawUiText(dc, settingsText, settingsDetail, kUiText, g_uiPaintFont);
    RECT settingsHint{44, 411, client.right - 44, 429};
    DrawUiText(dc, timing.invertWheel ? L"Scroll invertido · Configuración guarda y aplica al instante." :
                                        L"Scroll normal · Configuración guarda y aplica al instante.",
               settingsHint, kUiMuted, g_uiPaintFont);

    std::wstring latestLog;
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        latestLog = g_latestLog;
    }
    RECT footer{29, client.bottom - 31, client.right - 29, client.bottom - 8};
    if (latestLog.empty())
        latestLog = L"Mantén F o B/Círculo para recolectar · F9 cierra el helper";
    DrawUiText(dc, latestLog.c_str(), footer, kUiMuted, g_uiPaintFont,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void CreateSettingsLabel(HWND window, const wchar_t* text, int y,
                         int x = 42, int width = 295) {
    const HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        ScaleUi(x), ScaleUi(y), ScaleUi(width), ScaleUi(26), window, nullptr, nullptr, nullptr);
    if (label != nullptr)
        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
}

HWND CreateSettingsEdit(HWND window, int id, int y, DWORD value,
                        DWORD minimum, DWORD maximum) {
    const std::wstring text = std::to_wstring(value);
    const HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_RIGHT | ES_AUTOHSCROLL,
        ScaleUi(356), ScaleUi(y), ScaleUi(112), ScaleUi(27), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    if (edit == nullptr)
        return nullptr;
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);

    HWND spinner = CreateWindowExW(0, UPDOWN_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_SETBUDDYINT,
        0, ScaleUi(y), 0, 0, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id + 100)), nullptr, nullptr);
    if (spinner == nullptr) {
        DestroyWindow(edit);
        return nullptr;
    }
    SendMessageW(spinner, UDM_SETBUDDY, reinterpret_cast<WPARAM>(edit), 0);
    SendMessageW(spinner, UDM_SETRANGE32, minimum, maximum);
    return spinner;
}

HWND CreateSettingsTextInput(HWND window, int id, int y, const wchar_t* value) {
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        ScaleUi(220), ScaleUi(y), ScaleUi(248), ScaleUi(27), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    if (edit != nullptr) {
        SendMessageW(edit, EM_SETLIMITTEXT, 120, 0);
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
    }
    return edit;
}

void SetSettingsDefaults(HWND window) {
    SetDlgItemInt(window, kSettingsWheelInitialDelay, 300, FALSE);
    SetDlgItemInt(window, kSettingsWheelPulse, 50, FALSE);
    SetDlgItemInt(window, kSettingsWheelStep, 150, FALSE);
    SetDlgItemInt(window, kSettingsWheelRelease, 550, FALSE);
    SetDlgItemInt(window, kSettingsPreopenThreshold, 60, FALSE);
    SetDlgItemInt(window, kSettingsWheelSensitivity, 100, FALSE);
    SendDlgItemMessageW(window, kSettingsWheelInvert, BM_SETCHECK, BST_UNCHECKED, 0);
    SendDlgItemMessageW(window, kSettingsControllerNotifications, BM_SETCHECK, BST_CHECKED, 0);
    SetDlgItemTextW(window, kSettingsExtraGuardKeys, L"");
    SendDlgItemMessageW(window, kSettingsWheelPreset, CB_SETCURSEL, 2, 0);
    SendDlgItemMessageW(window, kSettingsGatherButton, CB_SETCURSEL, 0, 0);
    SendDlgItemMessageW(window, kSettingsGatherController, CB_SETCURSEL, 0, 0);
    SetDlgItemInt(window, kSettingsGatherPulse, 60, FALSE);
    SetDlgItemInt(window, kSettingsGatherInterval, 300, FALSE);
}

void PopulateSettings(HWND window) {
    const WheelTiming timing = GetWheelTiming();
    SetDlgItemInt(window, kSettingsWheelInitialDelay, timing.initialDelayMs, FALSE);
    SetDlgItemInt(window, kSettingsWheelPulse, timing.pulseMs, FALSE);
    SetDlgItemInt(window, kSettingsWheelStep, timing.stepIntervalMs, FALSE);
    SetDlgItemInt(window, kSettingsWheelRelease, timing.releaseDelayMs, FALSE);
    SetDlgItemInt(window, kSettingsPreopenThreshold, timing.preopenThreshold, FALSE);
    SetDlgItemInt(window, kSettingsWheelSensitivity, timing.wheelSensitivityPercent, FALSE);
    SendDlgItemMessageW(window, kSettingsWheelInvert, BM_SETCHECK,
                        timing.invertWheel ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(window, kSettingsControllerNotifications, BM_SETCHECK,
        g_controllerNotificationsEnabled.load(std::memory_order_acquire) ? BST_CHECKED : BST_UNCHECKED,
        0);
    SetDlgItemTextW(window, kSettingsExtraGuardKeys, g_extraGuardKeysText.c_str());
    SetDlgItemInt(window, kSettingsGatherPulse, timing.gatherPulseMs, FALSE);
    SetDlgItemInt(window, kSettingsGatherInterval, timing.gatherIntervalMs, FALSE);

    const WORD button = g_controllerGatherButton.load(std::memory_order_acquire);
    const WPARAM selection = button == XINPUT_GAMEPAD_B ? 0 :
                             button == XINPUT_GAMEPAD_A ? 1 :
                             button == XINPUT_GAMEPAD_X ? 2 :
                             button == XINPUT_GAMEPAD_Y ? 3 : 4;
    SendDlgItemMessageW(window, kSettingsGatherButton, CB_SETCURSEL, selection, 0);
    const int controllerIndex = g_gatherControllerIndex.load(std::memory_order_acquire);
    SendDlgItemMessageW(window, kSettingsGatherController, CB_SETCURSEL,
                        controllerIndex < 0 ? 0 : controllerIndex + 1, 0);
    const LRESULT preset = timing.stepIntervalMs == 180 ? 1 :
                           timing.stepIntervalMs == 150 ? 2 :
                           timing.stepIntervalMs == 120 ? 3 : 0;
    SendDlgItemMessageW(window, kSettingsWheelPreset, CB_SETCURSEL, preset, 0);
}

bool ReadSettingsNumber(HWND window, int id, DWORD minimum, DWORD maximum,
                         const wchar_t* settingName, DWORD& value) {
    BOOL translated = FALSE;
    const UINT input = GetDlgItemInt(window, id, &translated, FALSE);
    if (!translated || input < minimum || input > maximum) {
        std::wstring message = settingName;
        message += L" debe estar entre ";
        message += std::to_wstring(minimum);
        message += L" y ";
        message += std::to_wstring(maximum);
        message += L".";
        MessageBoxW(window, message.c_str(), L"Valor fuera de rango", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(window, id));
        SendDlgItemMessageW(window, id, EM_SETSEL, 0, -1);
        return false;
    }
    value = input;
    return true;
}

bool WriteIniNumber(const std::wstring& iniPath, const wchar_t* section,
                    const wchar_t* key, DWORD value) {
    const std::wstring text = std::to_wstring(value);
    return WritePrivateProfileStringW(section, key, text.c_str(), iniPath.c_str()) != FALSE;
}

bool WriteIniText(const std::wstring& iniPath, const wchar_t* section,
                  const wchar_t* key, const wchar_t* value) {
    return WritePrivateProfileStringW(section, key, value, iniPath.c_str()) != FALSE;
}

void SaveSettings(HWND window) {
    if (g_configPath.empty()) {
        Log("ERROR: no se pudo determinar donde guardar la configuracion.");
        MessageBoxW(window, L"No se pudo determinar dónde guardar la configuración.",
                    L"Configuración", MB_OK | MB_ICONERROR);
        return;
    }

    const WheelTiming previousTiming = GetWheelTiming();
    WheelTiming timing = previousTiming;
    if (!ReadSettingsNumber(window, kSettingsWheelInitialDelay, 300, 1000,
                             L"La espera inicial", timing.initialDelayMs) ||
        !ReadSettingsNumber(window, kSettingsWheelPulse, 50, 100,
                             L"El pulso de la rueda", timing.pulseMs) ||
        !ReadSettingsNumber(window, kSettingsWheelStep, 100, 600,
                             L"El intervalo de la rueda", timing.stepIntervalMs) ||
        !ReadSettingsNumber(window, kSettingsWheelRelease, 550, 1000,
                             L"La espera de cierre", timing.releaseDelayMs) ||
        !ReadSettingsNumber(window, kSettingsPreopenThreshold, 0, WHEEL_DELTA - 1,
                             L"La preapertura", timing.preopenThreshold) ||
        !ReadSettingsNumber(window, kSettingsWheelSensitivity, 50, 200,
                             L"La sensibilidad de la rueda", timing.wheelSensitivityPercent) ||
        !ReadSettingsNumber(window, kSettingsGatherPulse, 50, 100,
                             L"El pulso de recolección", timing.gatherPulseMs) ||
        !ReadSettingsNumber(window, kSettingsGatherInterval, 200, 1000,
                             L"El intervalo de recolección", timing.gatherIntervalMs))
        return;

    timing.invertWheel = SendDlgItemMessageW(window, kSettingsWheelInvert, BM_GETCHECK, 0, 0) ==
                         BST_CHECKED;

    wchar_t extraGuardText[128]{};
    GetDlgItemTextW(window, kSettingsExtraGuardKeys, extraGuardText,
                    static_cast<int>(std::size(extraGuardText)));
    std::array<int, kMaxExtraGuardKeys> extraGuardKeys{};
    DWORD extraGuardKeyCount = 0;
    if (!ParseExtraGuardKeys(extraGuardText, extraGuardKeys, extraGuardKeyCount)) {
        MessageBoxW(window,
            L"Las teclas extra deben ser nombres válidos separados por comas (por ejemplo: X, E, F). Se permiten hasta ocho.",
            L"Teclas de seguridad", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(window, kSettingsExtraGuardKeys));
        SendDlgItemMessageW(window, kSettingsExtraGuardKeys, EM_SETSEL, 0, -1);
        return;
    }

    if (timing.stepIntervalMs < timing.pulseMs) {
        MessageBoxW(window, L"El intervalo de la rueda no puede ser menor que la duración del pulso.",
                    L"Revisa los tiempos", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(window, kSettingsWheelStep));
        return;
    }
    if (timing.gatherIntervalMs < timing.gatherPulseMs) {
        MessageBoxW(window, L"El intervalo de recolección no puede ser menor que la duración del pulso.",
                    L"Revisa los tiempos", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(window, kSettingsGatherInterval));
        return;
    }

    const LRESULT selection = SendDlgItemMessageW(window, kSettingsGatherButton, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) {
        MessageBoxW(window, L"Selecciona un botón de mando válido.",
                    L"Configuración", MB_OK | MB_ICONWARNING);
        return;
    }
    const wchar_t* buttonValue = selection == 0 ? L"B" :
                                 selection == 1 ? L"A" :
                                 selection == 2 ? L"X" :
                                 selection == 3 ? L"Y" : L"OFF";

    const LRESULT controllerSelection = SendDlgItemMessageW(
        window, kSettingsGatherController, CB_GETCURSEL, 0, 0);
    if (controllerSelection == CB_ERR) {
        MessageBoxW(window, L"Selecciona qué mando puede activar la autorecolección.",
                    L"Configuración", MB_OK | MB_ICONWARNING);
        return;
    }
    const int gatherControllerIndex = controllerSelection == 0
                                          ? -1 : static_cast<int>(controllerSelection - 1);
    const std::wstring gatherControllerValue = gatherControllerIndex < 0
                                                   ? L"any"
                                                   : std::to_wstring(gatherControllerIndex);
    const bool controllerNotificationsEnabled =
        SendDlgItemMessageW(window, kSettingsControllerNotifications, BM_GETCHECK, 0, 0) == BST_CHECKED;

    const size_t configSeparator = g_configPath.find_last_of(L"\\/");
    if (configSeparator == std::wstring::npos) {
        Log("ERROR: la ruta del INI no contiene una carpeta valida.");
        MessageBoxW(window, L"La ruta de configuración no tiene una carpeta válida.",
                    L"Error al guardar", MB_OK | MB_ICONERROR);
        return;
    }
    const std::wstring configDirectory = g_configPath.substr(0, configSeparator);
    if (!CreateDirectoryW(configDirectory.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        const DWORD errorCode = GetLastError();
        Log("ERROR: no se pudo preparar la carpeta del INI; codigo=" +
            std::to_string(errorCode));
        std::wstring message = L"No se pudo preparar la carpeta de configuración. Código de Windows: ";
        message += std::to_wstring(errorCode);
        MessageBoxW(window, message.c_str(), L"Error al guardar", MB_OK | MB_ICONERROR);
        return;
    }

    const std::wstring stagedConfigPath = g_configPath + L".tmp";
    bool stagingReady = false;
    if (GetFileAttributesW(g_configPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const DWORD attributesError = GetLastError();
        if (attributesError == ERROR_FILE_NOT_FOUND || attributesError == ERROR_PATH_NOT_FOUND) {
            HANDLE emptyStage = CreateFileW(stagedConfigPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (emptyStage != INVALID_HANDLE_VALUE) {
                CloseHandle(emptyStage);
                stagingReady = true;
            }
        }
    } else {
        stagingReady = CopyFileW(g_configPath.c_str(), stagedConfigPath.c_str(), FALSE) != FALSE;
    }
    if (!stagingReady) {
        const DWORD errorCode = GetLastError();
        Log("ERROR: no se pudo preparar el INI temporal; codigo=" +
            std::to_string(errorCode));
        std::wstring message = L"No se pudo preparar una copia segura del INI. Código de Windows: ";
        message += std::to_wstring(errorCode);
        MessageBoxW(window, message.c_str(), L"Error al guardar", MB_OK | MB_ICONERROR);
        return;
    }

    bool saved = true;
    DWORD saveError = ERROR_SUCCESS;
    const auto recordWrite = [&saved, &saveError](bool succeeded) {
        if (succeeded)
            return;
        saved = false;
        if (saveError == ERROR_SUCCESS) {
            saveError = GetLastError();
            if (saveError == ERROR_SUCCESS)
                saveError = ERROR_WRITE_FAULT;
        }
    };
    const auto writeNumber = [&stagedConfigPath, &recordWrite](const wchar_t* section,
                                                                const wchar_t* key, DWORD value) {
        SetLastError(ERROR_SUCCESS);
        recordWrite(WriteIniNumber(stagedConfigPath, section, key, value));
    };

    writeNumber(L"Wheel", L"initial_delay_ms", timing.initialDelayMs);
    writeNumber(L"Wheel", L"pulse_ms", timing.pulseMs);
    writeNumber(L"Wheel", L"step_interval_ms", timing.stepIntervalMs);
    writeNumber(L"Wheel", L"release_delay_ms", timing.releaseDelayMs);
    writeNumber(L"Wheel", L"preopen_threshold", timing.preopenThreshold);
    writeNumber(L"Wheel", L"sensitivity_percent", timing.wheelSensitivityPercent);
    writeNumber(L"Wheel", L"invert", timing.invertWheel ? 1 : 0);
    SetLastError(ERROR_SUCCESS);
    recordWrite(WritePrivateProfileStringW(L"Controller", L"gather_button", buttonValue,
                                           stagedConfigPath.c_str()) != FALSE);
    writeNumber(L"Controller", L"gather_pulse_ms", timing.gatherPulseMs);
    writeNumber(L"Controller", L"gather_interval_ms", timing.gatherIntervalMs);
    writeNumber(L"Controller", L"connection_notifications", controllerNotificationsEnabled ? 1 : 0);
    SetLastError(ERROR_SUCCESS);
    recordWrite(WriteIniText(stagedConfigPath, L"Controller", L"gather_controller",
                             gatherControllerValue.c_str()));
    SetLastError(ERROR_SUCCESS);
    recordWrite(WriteIniText(stagedConfigPath, L"Input", L"extra_guard_keys", extraGuardText));

    // WritePrivateProfileString returns zero when asked to flush its profile cache,
    // including after a successful flush. Verify the file itself instead of using
    // that ambiguous return value as a write failure.
    SetLastError(ERROR_SUCCESS);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, stagedConfigPath.c_str());
    SetLastError(ERROR_SUCCESS);
    HANDLE stagedFile = CreateFileW(stagedConfigPath.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stagedFile == INVALID_HANDLE_VALUE) {
        recordWrite(false);
    } else {
        SetLastError(ERROR_SUCCESS);
        const bool fileFlushed = FlushFileBuffers(stagedFile) != FALSE;
        if (!fileFlushed)
            recordWrite(false);
        CloseHandle(stagedFile);
    }
    if (!saved) {
        DeleteFileW(stagedConfigPath.c_str());
        Log("ERROR: fallo al escribir o vaciar el INI temporal; codigo=" +
            std::to_string(saveError));
        std::wstring message = L"No se pudo guardar la configuración completa; el INI anterior sigue intacto. Código de Windows: ";
        message += std::to_wstring(saveError);
        MessageBoxW(window, message.c_str(), L"Error al guardar", MB_OK | MB_ICONERROR);
        return;
    }

    if (!MoveFileExW(stagedConfigPath.c_str(), g_configPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD errorCode = GetLastError();
        DeleteFileW(stagedConfigPath.c_str());
        Log("ERROR: no se pudo reemplazar el INI; codigo=" + std::to_string(errorCode));
        std::wstring message = L"No se pudo reemplazar MH3WorldControls.ini. Los valores no se aplicaron. Código de Windows: ";
        message += std::to_wstring(errorCode);
        MessageBoxW(window, message.c_str(), L"Error al guardar", MB_OK | MB_ICONERROR);
        return;
    }

    if (timing.wheelSensitivityPercent != previousTiming.wheelSensitivityPercent ||
        timing.invertWheel != previousTiming.invertWheel) {
        DiscardPendingWheelInput(true);
    }
    ApplyWheelTiming(timing);
    ApplyExtraGuardKeys(extraGuardKeys, extraGuardKeyCount);
    g_extraGuardKeysText = extraGuardText;
    g_controllerGatherButton.store(selection == 0 ? XINPUT_GAMEPAD_B :
                                   selection == 1 ? XINPUT_GAMEPAD_A :
                                   selection == 2 ? XINPUT_GAMEPAD_X :
                                   selection == 3 ? XINPUT_GAMEPAD_Y : 0,
                                   std::memory_order_release);
    g_gatherControllerIndex.store(gatherControllerIndex, std::memory_order_release);
    g_controllerNotificationsEnabled.store(controllerNotificationsEnabled,
                                           std::memory_order_release);
    Log("Configuracion guardada y aplicada sin reiniciar.");
    if (g_mainWindow != nullptr)
        InvalidateRect(g_mainWindow, nullptr, FALSE);
    DestroyWindow(window);
}

void DrawSettingsWindow(HDC dc, const RECT& client) {
    HBRUSH background = CreateSolidBrush(kUiBackground);
    FillRect(dc, &client, background);
    DeleteObject(background);

    RECT title{24, 17, client.right - 24, 48};
    DrawUiText(dc, L"Configuración", title, kUiText, g_uiPaintBoldFont);
    RECT subtitle{25, 47, client.right - 24, 69};
    DrawUiText(dc, L"Ajusta los controles y aplica sin reiniciar", subtitle, kUiMuted, g_uiPaintFont);

    DrawUiCard(dc, RECT{20, 78, client.right - 20, 365});
    RECT wheelHeader{39, 88, 220, 112};
    DrawUiText(dc, L"RUEDA DEL RATÓN", wheelHeader, kUiCyan, g_uiPaintBoldFont);
    DrawUiCard(dc, RECT{20, 376, client.right - 20, 580});
    RECT controllerHeader{39, 386, client.right - 38, 410};
    DrawUiText(dc, L"MANDO / AUTORECOLECCIÓN", controllerHeader, kUiCyan, g_uiPaintBoldFont);

    RECT footer{23, client.bottom - 27, client.right - 23, client.bottom - 7};
    DrawUiText(dc, L"El INI se guarda en LocalAppData; el antiguo se migra automáticamente.",
               footer, kUiMuted, g_uiPaintFont);
}

LRESULT CALLBACK SettingsWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        SetPropW(window, kDpiPropertyName,
                 reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(g_uiDpi)));
        CreateSettingsLabel(window, L"Ritmo:", 88);
        CreateSettingsLabel(window, L"Espera inicial (300–1000 ms)", 119);
        CreateSettingsLabel(window, L"Duración del pulso (50–100 ms)", 150);
        CreateSettingsLabel(window, L"Intervalo entre pasos (100–600 ms)", 181);
        CreateSettingsLabel(window, L"Espera para cerrar (550–1000 ms)", 212);
        CreateSettingsLabel(window, L"Preapertura de la rueda (0–119)", 243);
        CreateSettingsLabel(window, L"Sensibilidad (50–200 %)", 274);
        CreateSettingsLabel(window, L"Teclas extra para bloquear", 306, 42, 170);
        CreateSettingsLabel(window, L"Botón de recolección", 419);
        CreateSettingsLabel(window, L"Mando permitido", 450);
        CreateSettingsLabel(window, L"Duración del pulso (50–100 ms)", 481);
        CreateSettingsLabel(window, L"Intervalo entre intentos (200–1000 ms)", 512);

        const WheelTiming timing = GetWheelTiming();
        HWND controls[] = {
            CreateSettingsEdit(window, kSettingsWheelInitialDelay, 118, timing.initialDelayMs, 300, 1000),
            CreateSettingsEdit(window, kSettingsWheelPulse, 149, timing.pulseMs, 50, 100),
            CreateSettingsEdit(window, kSettingsWheelStep, 180, timing.stepIntervalMs, 100, 600),
            CreateSettingsEdit(window, kSettingsWheelRelease, 211, timing.releaseDelayMs, 550, 1000),
            CreateSettingsEdit(window, kSettingsPreopenThreshold, 242, timing.preopenThreshold, 0, WHEEL_DELTA - 1),
            CreateSettingsEdit(window, kSettingsWheelSensitivity, 273, timing.wheelSensitivityPercent, 50, 200),
            CreateSettingsEdit(window, kSettingsGatherPulse, 480, timing.gatherPulseMs, 50, 100),
            CreateSettingsEdit(window, kSettingsGatherInterval, 511, timing.gatherIntervalMs, 200, 1000)
        };
        HWND wheelPresetCombo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            ScaleUi(337), ScaleUi(87), ScaleUi(143), ScaleUi(150), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsWheelPreset)), nullptr, nullptr);
        if (wheelPresetCombo != nullptr) {
            const wchar_t* presets[] = {L"Personalizado", L"Suave · 180 ms",
                                        L"Equilibrado · 150 ms", L"Ágil · 120 ms"};
            for (const wchar_t* preset : presets)
                SendMessageW(wheelPresetCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(preset));
            SendMessageW(wheelPresetCombo, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
        }
        HWND gatherCombo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            ScaleUi(356), ScaleUi(418), ScaleUi(112), ScaleUi(150), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsGatherButton)), nullptr, nullptr);
        if (gatherCombo != nullptr) {
            const wchar_t* buttons[] = {L"B / Círculo", L"A", L"X", L"Y", L"Desactivar"};
            for (const wchar_t* button : buttons)
                SendMessageW(gatherCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(button));
            SendMessageW(gatherCombo, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
        }

        HWND controllerCombo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            ScaleUi(356), ScaleUi(449), ScaleUi(112), ScaleUi(150), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsGatherController)), nullptr, nullptr);
        if (controllerCombo != nullptr) {
            const wchar_t* controllerChoices[] = {L"Cualquiera", L"Mando 1", L"Mando 2",
                                                   L"Mando 3", L"Mando 4"};
            for (const wchar_t* choice : controllerChoices)
                SendMessageW(controllerCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice));
            SendMessageW(controllerCombo, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
        }
        HWND extraGuardEdit = CreateSettingsTextInput(window, kSettingsExtraGuardKeys, 305,
                                                       g_extraGuardKeysText.c_str());
        HWND invertCheck = CreateWindowExW(0, L"BUTTON", L"Invertir dirección del scroll",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            ScaleUi(42), ScaleUi(336), ScaleUi(240), ScaleUi(24), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsWheelInvert)), nullptr, nullptr);
        if (invertCheck != nullptr)
            SendMessageW(invertCheck, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);

        HWND notificationsCheck = CreateWindowExW(0, L"BUTTON", L"Avisar al conectar o desconectar mando",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            ScaleUi(42), ScaleUi(548), ScaleUi(400), ScaleUi(24), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsControllerNotifications)), nullptr, nullptr);
        if (notificationsCheck != nullptr)
            SendMessageW(notificationsCheck, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);

        HWND defaults = CreateWindowExW(0, L"BUTTON", L"Valores iniciales",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            ScaleUi(20), ScaleUi(590), ScaleUi(148), ScaleUi(42),
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsDefaults)), nullptr, nullptr);
        HWND save = CreateWindowExW(0, L"BUTTON", L"Guardar y aplicar",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            ScaleUi(186), ScaleUi(590), ScaleUi(148), ScaleUi(42),
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsSave)), nullptr, nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancelar",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            ScaleUi(352), ScaleUi(590), ScaleUi(148), ScaleUi(42),
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsCancel)), nullptr, nullptr);
        if (std::any_of(std::begin(controls), std::end(controls), [](HWND control) { return control == nullptr; }) ||
            wheelPresetCombo == nullptr || gatherCombo == nullptr || controllerCombo == nullptr ||
            extraGuardEdit == nullptr || invertCheck == nullptr || notificationsCheck == nullptr ||
            defaults == nullptr ||
            save == nullptr || cancel == nullptr)
            return -1;

        for (HWND control : {defaults, save, cancel})
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiBoldFont), TRUE);
        PopulateSettings(window);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DPICHANGED:
        HandleDpiChanged(window, HIWORD(wParam),
                         reinterpret_cast<const RECT*>(lParam));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT physicalClient{};
        GetClientRect(window, &physicalClient);
        const int saved = SaveDC(dc);
        SetMapMode(dc, MM_ANISOTROPIC);
        SetWindowExtEx(dc, kSettingsLogicalWidth, kSettingsLogicalHeight, nullptr);
        SetViewportExtEx(dc, physicalClient.right, physicalClient.bottom, nullptr);
        const RECT logicalClient{0, 0, kSettingsLogicalWidth, kSettingsLogicalHeight};
        DrawSettingsWindow(dc, logicalClient);
        if (saved != 0)
            RestoreDC(dc, saved);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), kUiText);
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wParam), kUiText);
        SetBkColor(reinterpret_cast<HDC>(wParam), kUiPanelRaised);
        return reinterpret_cast<LRESULT>(message == WM_CTLCOLOREDIT ?
                                          g_settingsEditBrush : g_settingsListBrush);
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item == nullptr)
            break;
        const bool primary = item->CtlID == kSettingsSave;
        const bool hot = (item->itemState & ODS_HOTLIGHT) != 0;
        const COLORREF fill = primary ? (hot ? RGB(48, 218, 255) : RGB(21, 172, 218))
                                      : (hot ? RGB(30, 57, 82) : kUiPanelRaised);
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, primary ? kUiCyan : kUiBorder);
        HGDIOBJ oldBrush = SelectObject(item->hDC, brush);
        HGDIOBJ oldPen = SelectObject(item->hDC, pen);
        RoundRect(item->hDC, item->rcItem.left, item->rcItem.top,
                  item->rcItem.right, item->rcItem.bottom, 10, 10);
        wchar_t label[80]{};
        GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
        RECT textRect = item->rcItem;
        InflateRect(&textRect, -5, 0);
        DrawUiText(item->hDC, label, textRect, kUiText, g_uiBoldFont,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if ((item->itemState & ODS_FOCUS) != 0) {
            RECT focusRect = item->rcItem;
            InflateRect(&focusRect, -8, -6);
            DrawFocusRect(item->hDC, &focusRect);
        }
        if ((item->itemState & ODS_FOCUS) != 0) {
            RECT focusRect = item->rcItem;
            InflateRect(&focusRect, -5, -5);
            DrawFocusRect(item->hDC, &focusRect);
        }
        SelectObject(item->hDC, oldBrush);
        SelectObject(item->hDC, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kSettingsSave:
            SaveSettings(window);
            return 0;
        case kSettingsDefaults:
            SetSettingsDefaults(window);
            return 0;
        case kSettingsCancel:
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == kSettingsWheelPreset && HIWORD(wParam) == CBN_SELCHANGE) {
            const LRESULT preset = SendDlgItemMessageW(window, kSettingsWheelPreset, CB_GETCURSEL, 0, 0);
            if (preset == 1)
                SetDlgItemInt(window, kSettingsWheelStep, 180, FALSE);
            else if (preset == 2)
                SetDlgItemInt(window, kSettingsWheelStep, 150, FALSE);
            else if (preset == 3)
                SetDlgItemInt(window, kSettingsWheelStep, 120, FALSE);
            return 0;
        }
        if (LOWORD(wParam) == kSettingsWheelStep && HIWORD(wParam) == EN_CHANGE &&
            (GetFocus() == GetDlgItem(window, kSettingsWheelStep) ||
             GetDlgCtrlID(GetFocus()) == kSettingsWheelStep + 100)) {
            SendDlgItemMessageW(window, kSettingsWheelPreset, CB_SETCURSEL, 0, 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (g_settingsEditBrush != nullptr) {
            DeleteObject(g_settingsEditBrush);
            g_settingsEditBrush = nullptr;
        }
        if (g_settingsListBrush != nullptr) {
            DeleteObject(g_settingsListBrush);
            g_settingsListBrush = nullptr;
        }
        g_settingsWindow = nullptr;
        RemovePropW(window, kDpiPropertyName);
        if (g_mainWindow != nullptr)
            InvalidateRect(g_mainWindow, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void OpenSettingsWindow() {
    if (g_settingsWindow != nullptr) {
        ShowWindow(g_settingsWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_settingsWindow);
        return;
    }

    static bool settingsClassRegistered = false;
    if (!settingsClassRegistered) {
        WNDCLASSEXW settingsClass{};
        settingsClass.cbSize = sizeof(settingsClass);
        settingsClass.style = CS_HREDRAW | CS_VREDRAW;
        settingsClass.lpfnWndProc = SettingsWindowProc;
        settingsClass.hInstance = GetModuleHandleW(nullptr);
        settingsClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        settingsClass.lpszClassName = kSettingsWindowClassName;
        if (RegisterClassExW(&settingsClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            ShowWindowsError(L"No se pudo registrar la ventana de configuración.");
            return;
        }
        settingsClassRegistered = true;
    }

    g_settingsEditBrush = CreateSolidBrush(kUiPanelRaised);
    g_settingsListBrush = CreateSolidBrush(kUiPanelRaised);
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT bounds{0, 0, ScaleUi(kSettingsLogicalWidth), ScaleUi(kSettingsLogicalHeight)};
    AdjustWindowRectForDpiSafe(&bounds, style, FALSE, WS_EX_DLGMODALFRAME, g_uiDpi);
    g_settingsWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsWindowClassName,
        L"MH3 World Controls · Configuración", style, CW_USEDEFAULT, CW_USEDEFAULT,
        bounds.right - bounds.left, bounds.bottom - bounds.top,
        g_mainWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (g_settingsWindow == nullptr) {
        if (g_settingsEditBrush != nullptr) {
            DeleteObject(g_settingsEditBrush);
            g_settingsEditBrush = nullptr;
        }
        if (g_settingsListBrush != nullptr) {
            DeleteObject(g_settingsListBrush);
            g_settingsListBrush = nullptr;
        }
        ShowWindowsError(L"No se pudo abrir la ventana de configuración.");
        return;
    }
    ShowWindow(g_settingsWindow, SW_SHOWNORMAL);
    UpdateWindow(g_settingsWindow);
    SetForegroundWindow(g_settingsWindow);
}

LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_taskbarCreatedMessage != 0 && message == g_taskbarCreatedMessage) {
        g_trayIconAdded = false;
        AddTrayIcon(window);
        return 0;
    }

    switch (message) {
    case WM_CREATE: {
        SetPropW(window, kDpiPropertyName,
                 reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(g_uiDpi)));
        CreateUiFonts(g_uiDpi);
        g_toggleWheelControl = CreateWindowExW(0, L"BUTTON", L"Pausar rueda",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            ScaleUi(25), ScaleUi(454), ScaleUi(158), ScaleUi(48),
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToggleWheelButton)), nullptr, nullptr);
        const HWND configControl = CreateWindowExW(0, L"BUTTON", L"Configuración",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            ScaleUi(195), ScaleUi(454), ScaleUi(158), ScaleUi(48),
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenConfigButton)), nullptr, nullptr);
        const HWND diagnosticsControl = CreateWindowExW(0, L"BUTTON", L"Copiar diagnóstico",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            ScaleUi(365), ScaleUi(454), ScaleUi(158), ScaleUi(48),
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCopyDiagnosticsButton)), nullptr, nullptr);
        const HWND exitControl = CreateWindowExW(0, L"BUTTON", L"Cerrar helper",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            ScaleUi(535), ScaleUi(454), ScaleUi(158), ScaleUi(48),
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExitButton)), nullptr, nullptr);
        if (g_uiFont == nullptr || g_uiBoldFont == nullptr || g_uiPaintFont == nullptr ||
            g_uiPaintBoldFont == nullptr || g_toggleWheelControl == nullptr ||
            configControl == nullptr || diagnosticsControl == nullptr || exitControl == nullptr)
            return -1;
        for (int id : {kToggleWheelButton, kOpenConfigButton, kCopyDiagnosticsButton, kExitButton}) {
            const HWND control = GetDlgItem(window, id);
            if (control != nullptr)
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiBoldFont), TRUE);
        }
        AddTrayIcon(window);
        SetTimer(window, 1, 250, nullptr);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DPICHANGED:
        HandleDpiChanged(window, HIWORD(wParam),
                         reinterpret_cast<const RECT*>(lParam));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT physicalClient{};
        GetClientRect(window, &physicalClient);
        const int saved = SaveDC(dc);
        SetMapMode(dc, MM_ANISOTROPIC);
        SetWindowExtEx(dc, kMainLogicalWidth, kMainLogicalHeight, nullptr);
        SetViewportExtEx(dc, physicalClient.right, physicalClient.bottom, nullptr);
        const RECT logicalClient{0, 0, kMainLogicalWidth, kMainLogicalHeight};
        DrawMainWindow(dc, logicalClient);
        if (saved != 0)
            RestoreDC(dc, saved);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item == nullptr)
            break;
        const bool primary = item->CtlID == kToggleWheelButton;
        const bool hot = (item->itemState & ODS_HOTLIGHT) != 0;
        const COLORREF fill = primary ? (hot ? RGB(48, 218, 255) : RGB(21, 172, 218))
                                      : (hot ? RGB(30, 57, 82) : kUiPanelRaised);
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, primary ? kUiCyan : kUiBorder);
        HGDIOBJ oldBrush = SelectObject(item->hDC, brush);
        HGDIOBJ oldPen = SelectObject(item->hDC, pen);
        RoundRect(item->hDC, item->rcItem.left, item->rcItem.top,
                  item->rcItem.right, item->rcItem.bottom, 12, 12);
        wchar_t label[80]{};
        GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
        RECT textRect = item->rcItem;
        InflateRect(&textRect, -8, 0);
        DrawUiText(item->hDC, label, textRect, kUiText, g_uiBoldFont,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(item->hDC, oldBrush);
        SelectObject(item->hDC, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kToggleWheelButton:
            ToggleWheel();
            return 0;
        case kOpenConfigButton:
            OpenSettingsWindow();
            return 0;
        case kCopyDiagnosticsButton:
            CopyDiagnostics(window);
            return 0;
        case kExitButton:
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == 1) {
            CheckControllerConnectionChange();
            RECT client{};
            GetClientRect(window, &client);
            RECT gameStatus{ScaleUi(25), ScaleUi(98), client.right - ScaleUi(25), ScaleUi(213)};
            RECT controllerStatus{client.right / 2 + ScaleUi(8), ScaleUi(228),
                                  client.right - ScaleUi(25), ScaleUi(337)};
            InvalidateRect(window, &gameStatus, FALSE);
            InvalidateRect(window, &controllerStatus, FALSE);
        }
        return 0;
    case kUiStatusMessage:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_HOTKEY:
        if (wParam == 1) {
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wParam == 2) {
            ToggleWheel();
            return 0;
        }
        break;
    case kTrayCallbackMessage:
    {
        const UINT event = g_trayIconVersion4 ? LOWORD(lParam) : static_cast<UINT>(lParam);
        if (event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_LBUTTONUP ||
            event == WM_LBUTTONDBLCLK) {
            ShowMainWindow(window);
            return 0;
        }
        if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
            ShowTrayMenu(window);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        g_running.store(false, std::memory_order_release);
        KillTimer(window, 1);
        if (g_settingsWindow != nullptr)
            DestroyWindow(g_settingsWindow);
        RemoveTrayIcon();
        if (g_exitHotkeyRegistered) {
            UnregisterHotKey(window, 1);
            g_exitHotkeyRegistered = false;
        }
        if (g_wheelHotkeyRegistered) {
            UnregisterHotKey(window, 2);
            g_wheelHotkeyRegistered = false;
        }
        if (g_uiFont != nullptr) {
            DeleteObject(g_uiFont);
            g_uiFont = nullptr;
        }
        if (g_uiBoldFont != nullptr) {
            DeleteObject(g_uiBoldFont);
            g_uiBoldFont = nullptr;
        }
        if (g_uiPaintFont != nullptr) {
            DeleteObject(g_uiPaintFont);
            g_uiPaintFont = nullptr;
        }
        if (g_uiPaintBoldFont != nullptr) {
            DeleteObject(g_uiPaintBoldFont);
            g_uiPaintBoldFont = nullptr;
        }
        RemovePropW(window, kDpiPropertyName);
        g_toggleWheelControl = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void CleanupHooks() {
    if (g_mouseHook != nullptr) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
    if (g_keyboardHook != nullptr) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
}

void InputHookThreadMain() {
    g_hookThreadId = GetCurrentThreadId();
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);

    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook,
                                       GetModuleHandleW(nullptr), 0);
    if (g_keyboardHook == nullptr) {
        const DWORD errorCode = GetLastError();
        g_hookErrorCode.store(errorCode, std::memory_order_release);
        Log("ERROR: fallo el hook de teclado; codigo=" + std::to_string(errorCode));
        SetEvent(g_hookReadyEvent);
        return;
    }

    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook,
                                    GetModuleHandleW(nullptr), 0);
    if (g_mouseHook == nullptr) {
        const DWORD errorCode = GetLastError();
        g_hookErrorCode.store(errorCode, std::memory_order_release);
        Log("ERROR: fallo el hook de raton; codigo=" + std::to_string(errorCode));
        CleanupHooks();
        SetEvent(g_hookReadyEvent);
        return;
    }

    SnapshotPhysicalActionState();
    SetEvent(g_hookReadyEvent);
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CleanupHooks();
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    EnableDpiAwareness();
    g_uiDpi = GetSystemDpiSafe();
    g_mainThreadId = GetCurrentThreadId();
    if (!EnsureSingleInstance())
        return 0;
    LoadWheelTiming();
    LoadXInput();

    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_UPDOWN_CLASS;
    if (!InitCommonControlsEx(&commonControls)) {
        MessageBoxW(nullptr, L"No se pudieron inicializar los controles de configuración.",
                    L"MH3 World Controls", MB_OK | MB_ICONERROR);
        if (g_xinputModule != nullptr)
            FreeLibrary(g_xinputModule);
        CloseHandle(g_instanceGuard);
        return 1;
    }
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE); // Create the queue before the hook posts wheel deltas.

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = MainWindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        ShowWindowsError(L"No se pudo registrar la ventana del helper.");
        if (g_xinputModule != nullptr)
            FreeLibrary(g_xinputModule);
        CloseHandle(g_instanceGuard);
        return 1;
    }

    constexpr DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT windowRect{0, 0, ScaleUi(kMainLogicalWidth), ScaleUi(kMainLogicalHeight)};
    AdjustWindowRectForDpiSafe(&windowRect, windowStyle, FALSE, 0, g_uiDpi);
    g_mainWindow = CreateWindowExW(0, kWindowClassName, L"MH3 World Controls",
        windowStyle, CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
        nullptr, nullptr, instance, nullptr);
    if (g_mainWindow == nullptr) {
        ShowWindowsError(L"No se pudo crear la ventana del helper.");
        if (g_xinputModule != nullptr)
            FreeLibrary(g_xinputModule);
        CloseHandle(g_instanceGuard);
        return 1;
    }

    g_hookReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_hookReadyEvent == nullptr) {
        ShowWindowsError(L"No se pudo preparar el hilo de captura de entrada.");
        DestroyWindow(g_mainWindow);
        if (g_xinputModule != nullptr)
            FreeLibrary(g_xinputModule);
        CloseHandle(g_instanceGuard);
        return 1;
    }
    std::thread hookThread(InputHookThreadMain);
    const DWORD hookWait = WaitForSingleObject(g_hookReadyEvent, 5000);
    CloseHandle(g_hookReadyEvent);
    g_hookReadyEvent = nullptr;
    const DWORD hookError = g_hookErrorCode.load(std::memory_order_acquire);
    if (hookWait != WAIT_OBJECT_0 || hookError != ERROR_SUCCESS) {
        if (hookWait == WAIT_OBJECT_0) {
            std::wstring messageText = L"No se pudo instalar un hook de entrada. Código de Windows: ";
            messageText += std::to_wstring(hookError);
            MessageBoxW(g_mainWindow, messageText.c_str(), L"MH3 World Controls",
                        MB_OK | MB_ICONERROR);
        } else {
            MessageBoxW(g_mainWindow, L"El hilo de captura no respondió al iniciar.",
                        L"MH3 World Controls", MB_OK | MB_ICONERROR);
            if (g_hookThreadId != 0)
                PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
        }
        if (hookThread.joinable())
            hookThread.join();
        DestroyWindow(g_mainWindow);
        if (g_xinputModule != nullptr)
            FreeLibrary(g_xinputModule);
        CloseHandle(g_instanceGuard);
        return 1;
    }

    g_wheelHotkeyRegistered = RegisterHotKey(g_mainWindow, 2, MOD_NOREPEAT, VK_F8) != FALSE;
    if (!g_wheelHotkeyRegistered)
        Log("F8 no se pudo registrar; la rueda queda activa sin alternancia.");

    g_exitHotkeyRegistered = RegisterHotKey(g_mainWindow, 1, MOD_NOREPEAT, VK_F9) != FALSE;
    if (!g_exitHotkeyRegistered) {
        ShowWindowsError(L"No se pudo registrar F9 para cerrar el helper.");
        DestroyWindow(g_mainWindow);
        PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
        hookThread.join();
        if (g_xinputModule != nullptr)
            FreeLibrary(g_xinputModule);
        CloseHandle(g_instanceGuard);
        return 1;
    }

    std::thread worker(PickupWorker);
    ShowWindow(g_mainWindow, showCommand);
    UpdateWindow(g_mainWindow);
    int result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.message == kWheelDeltaMessage) {
            const SHORT delta = static_cast<SHORT>(static_cast<WORD>(message.wParam));
            HandleWheelDelta(delta, message.lParam != 0);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_running.store(false, std::memory_order_release);
    worker.join();
    g_mainWindow = nullptr;
    ReleaseWheelModifier();
    PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
    hookThread.join();
    if (g_xinputModule != nullptr)
        FreeLibrary(g_xinputModule);
    CloseHandle(g_instanceGuard);
    g_instanceGuard = nullptr;
    return 0;
}
