#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr int HOTKEY_LEFT = 1;
constexpr int HOTKEY_RIGHT = 2;
constexpr int HOTKEY_RESTORE = 3;
constexpr int HOTKEY_RELOAD = 4;
constexpr int HOTKEY_EXIT = 5;
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_TASKBARCREATED = WM_APP + 2;
constexpr UINT ID_TRAY_SETTINGS = 1001;
constexpr UINT ID_TRAY_PAUSE = 1007;
constexpr UINT ID_TRAY_EXIT = 1008;
constexpr UINT IDI_APP_ICON = 101;
constexpr UINT ID_SETTINGS_MONITOR = 2000;
constexpr UINT ID_SETTINGS_LEFT_RATIO = 2001;
constexpr UINT ID_SETTINGS_GAP = 2002;
constexpr UINT ID_SETTINGS_RESPECT_TASKBAR = 2003;
constexpr UINT ID_SETTINGS_LOCK_ZONES = 2004;
constexpr UINT ID_SETTINGS_AUTO_START = 2005;
constexpr UINT ID_SETTINGS_MONITOR_ENABLED = 2006;
constexpr UINT ID_SETTINGS_OK = IDOK;
constexpr UINT ID_SETTINGS_CANCEL = IDCANCEL;

struct Hotkey {
    UINT modifiers = 0;
    UINT vk = 0;
};

struct MonitorConfig {
    bool enabled = true;
    double leftRatio = 0.60;
    int gap = 8;
};

struct Config {
    double leftRatio = 0.60;
    int gap = 8;
    bool respectTaskbar = true;
    bool lockZones = true;
    bool autoStart = false;
    std::map<std::wstring, MonitorConfig> monitorSettings;
    Hotkey left{MOD_WIN | MOD_ALT, VK_LEFT};
    Hotkey right{MOD_WIN | MOD_ALT, VK_RIGHT};
    Hotkey restore{MOD_WIN | MOD_ALT, VK_UP};
    Hotkey reload{MOD_WIN | MOD_ALT, VK_F5};
    Hotkey exit{MOD_WIN | MOD_ALT, 'Q'};
};

std::wstring g_exeDir;
std::wofstream g_log;
Config g_config;
std::unordered_map<HWND, WINDOWPLACEMENT> g_savedPlacements;
std::unordered_map<HWND, bool> g_lockedZones;
HWINEVENTHOOK g_locationHook = nullptr;
bool g_enforcingZone = false;
bool g_paused = false;
UINT g_taskbarCreatedMessage = 0;
HINSTANCE g_instance = nullptr;

void InstallLocationHook();
void UninstallLocationHook();

std::wstring GetExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring result(path);
    const size_t slash = result.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        result.resize(slash);
    }
    return result;
}

std::wstring GetExePath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& file) {
    if (dir.empty()) {
        return file;
    }
    return dir + L"\\" + file;
}

std::wstring GetConfigPath() {
    return JoinPath(g_exeDir, L"config.ini");
}

void Log(const std::wstring& message) {
    if (!g_log.is_open()) {
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    g_log << L"[" << st.wYear << L"-";
    g_log.width(2);
    g_log.fill(L'0');
    g_log << st.wMonth << L"-";
    g_log.width(2);
    g_log << st.wDay << L" ";
    g_log.width(2);
    g_log << st.wHour << L":";
    g_log.width(2);
    g_log << st.wMinute << L":";
    g_log.width(2);
    g_log << st.wSecond << L"] " << message << std::endl;
}

std::string Trim(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char c) {
        return !isSpace(static_cast<unsigned char>(c));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char c) {
        return !isSpace(static_cast<unsigned char>(c));
    }).base(), value.end());
    return value;
}

std::string Upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

bool ParseBool(const std::string& value, bool fallback) {
    const std::string upper = Upper(Trim(value));
    if (upper == "1" || upper == "TRUE" || upper == "YES" || upper == "ON") {
        return true;
    }
    if (upper == "0" || upper == "FALSE" || upper == "NO" || upper == "OFF") {
        return false;
    }
    return fallback;
}

std::string BoolToConfigValue(bool value) {
    return value ? "true" : "false";
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return "";
    }

    std::string result(static_cast<size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring ToWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (length <= 0) {
        return L"";
    }

    std::wstring result(static_cast<size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), length);
    return result;
}

std::wstring FormatLeftRatio(double value) {
    std::wostringstream ss;
    ss << std::fixed << std::setprecision(2) << value;
    return ss.str();
}

MonitorConfig DefaultMonitorConfig(const Config& config) {
    MonitorConfig monitorConfig{};
    monitorConfig.enabled = true;
    monitorConfig.leftRatio = config.leftRatio;
    monitorConfig.gap = config.gap;
    return monitorConfig;
}

std::vector<std::string> Split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        parts.push_back(Trim(item));
    }
    return parts;
}

bool ParseVirtualKey(const std::string& token, UINT& vk) {
    if (token.size() == 1) {
        const char c = token[0];
        if (c >= 'A' && c <= 'Z') {
            vk = static_cast<UINT>(c);
            return true;
        }
        if (c >= '0' && c <= '9') {
            vk = static_cast<UINT>(c);
            return true;
        }
    }

    static const std::map<std::string, UINT> keys = {
        {"LEFT", VK_LEFT},       {"RIGHT", VK_RIGHT},       {"UP", VK_UP},
        {"DOWN", VK_DOWN},       {"HOME", VK_HOME},         {"END", VK_END},
        {"PGUP", VK_PRIOR},      {"PAGEUP", VK_PRIOR},      {"PGDN", VK_NEXT},
        {"PAGEDOWN", VK_NEXT},   {"INSERT", VK_INSERT},     {"INS", VK_INSERT},
        {"DELETE", VK_DELETE},   {"DEL", VK_DELETE},        {"SPACE", VK_SPACE},
        {"TAB", VK_TAB},         {"ENTER", VK_RETURN},      {"RETURN", VK_RETURN},
        {"ESC", VK_ESCAPE},      {"ESCAPE", VK_ESCAPE},     {"BACKSPACE", VK_BACK},
        {"BACK", VK_BACK}
    };

    const auto found = keys.find(token);
    if (found != keys.end()) {
        vk = found->second;
        return true;
    }

    if (token.size() >= 2 && token[0] == 'F') {
        const int number = std::atoi(token.c_str() + 1);
        if (number >= 1 && number <= 24) {
            vk = static_cast<UINT>(VK_F1 + number - 1);
            return true;
        }
    }

    return false;
}

Hotkey ParseHotkey(const std::string& value, const Hotkey& fallback) {
    Hotkey hotkey{};
    for (std::string token : Split(Upper(value), '+')) {
        if (token.empty()) {
            continue;
        }

        if (token == "WIN" || token == "WINDOWS" || token == "SUPER") {
            hotkey.modifiers |= MOD_WIN;
        } else if (token == "ALT") {
            hotkey.modifiers |= MOD_ALT;
        } else if (token == "CTRL" || token == "CONTROL") {
            hotkey.modifiers |= MOD_CONTROL;
        } else if (token == "SHIFT") {
            hotkey.modifiers |= MOD_SHIFT;
        } else {
            UINT vk = 0;
            if (!ParseVirtualKey(token, vk)) {
                return fallback;
            }
            hotkey.vk = vk;
        }
    }

    if (hotkey.vk == 0) {
        return fallback;
    }

    return hotkey;
}

std::string HotkeyToString(const Hotkey& hotkey) {
    std::vector<std::string> parts;
    if ((hotkey.modifiers & MOD_WIN) != 0) {
        parts.push_back("WIN");
    }
    if ((hotkey.modifiers & MOD_CONTROL) != 0) {
        parts.push_back("CTRL");
    }
    if ((hotkey.modifiers & MOD_ALT) != 0) {
        parts.push_back("ALT");
    }
    if ((hotkey.modifiers & MOD_SHIFT) != 0) {
        parts.push_back("SHIFT");
    }

    static const std::array<std::pair<UINT, const char*>, 31> keys = {{
        {VK_LEFT, "LEFT"},       {VK_RIGHT, "RIGHT"},       {VK_UP, "UP"},
        {VK_DOWN, "DOWN"},       {VK_HOME, "HOME"},         {VK_END, "END"},
        {VK_PRIOR, "PGUP"},      {VK_NEXT, "PGDN"},         {VK_INSERT, "INSERT"},
        {VK_DELETE, "DELETE"},   {VK_SPACE, "SPACE"},      {VK_TAB, "TAB"},
        {VK_RETURN, "ENTER"},    {VK_ESCAPE, "ESC"},        {VK_BACK, "BACKSPACE"},
        {VK_F1, "F1"},           {VK_F2, "F2"},             {VK_F3, "F3"},
        {VK_F4, "F4"},           {VK_F5, "F5"},             {VK_F6, "F6"},
        {VK_F7, "F7"},           {VK_F8, "F8"},             {VK_F9, "F9"},
        {VK_F10, "F10"},         {VK_F11, "F11"},           {VK_F12, "F12"},
        {VK_F13, "F13"},         {VK_F14, "F14"},           {VK_F15, "F15"},
        {VK_F16, "F16"}
    }};

    std::string keyName;
    if ((hotkey.vk >= 'A' && hotkey.vk <= 'Z') || (hotkey.vk >= '0' && hotkey.vk <= '9')) {
        keyName.push_back(static_cast<char>(hotkey.vk));
    } else {
        for (const auto& entry : keys) {
            if (entry.first == hotkey.vk) {
                keyName = entry.second;
                break;
            }
        }
    }

    if (keyName.empty() && hotkey.vk >= VK_F17 && hotkey.vk <= VK_F24) {
        keyName = "F" + std::to_string(hotkey.vk - VK_F1 + 1);
    }
    if (keyName.empty()) {
        keyName = std::to_string(hotkey.vk);
    }

    parts.push_back(keyName);

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            result += "+";
        }
        result += parts[i];
    }
    return result;
}

void WriteDefaultConfigIfMissing(const std::wstring& path) {
    std::ifstream existing(path.c_str());
    if (existing.good()) {
        return;
    }

    std::ofstream file(path.c_str());
    file << "; Splitlet configuration\n"
         << "; Hotkey format: WIN+ALT+LEFT, CTRL+SHIFT+F1, ALT+Q\n\n"
         << "left_ratio=0.60\n"
         << "gap=8\n"
         << "respect_taskbar=true\n\n"
         << "lock_zones=true\n"
         << "auto_start=false\n\n"
         << "hotkey_left=WIN+ALT+LEFT\n"
         << "hotkey_right=WIN+ALT+RIGHT\n"
         << "hotkey_restore=WIN+ALT+UP\n"
         << "hotkey_reload=WIN+ALT+F5\n"
         << "hotkey_exit=WIN+ALT+Q\n";
}

Config LoadConfig() {
    Config config;
    const std::wstring path = GetConfigPath();
    WriteDefaultConfigIfMissing(path);

    std::ifstream file(path.c_str());
    if (!file.good()) {
        Log(L"Could not read config.ini; using defaults.");
        return config;
    }

    std::string line;
    std::wstring currentMonitorDevice;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            const std::string section = line.substr(1, line.size() - 2);
            constexpr const char* monitorPrefix = "monitor:";
            if (section.rfind(monitorPrefix, 0) == 0) {
                currentMonitorDevice = ToWide(section.substr(std::strlen(monitorPrefix)));
                config.monitorSettings[currentMonitorDevice] = DefaultMonitorConfig(config);
            } else {
                currentMonitorDevice.clear();
            }
            continue;
        }

        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = Upper(Trim(line.substr(0, equals)));
        const std::string value = Trim(line.substr(equals + 1));

        if (!currentMonitorDevice.empty()) {
            MonitorConfig& monitorConfig = config.monitorSettings[currentMonitorDevice];
            if (key == "ENABLED") {
                monitorConfig.enabled = ParseBool(value, monitorConfig.enabled);
            } else if (key == "LEFT_RATIO") {
                try {
                    monitorConfig.leftRatio = std::clamp(std::stod(value), 0.10, 0.90);
                } catch (...) {
                    Log(L"Invalid monitor left_ratio; using default.");
                }
            } else if (key == "GAP") {
                try {
                    monitorConfig.gap = std::clamp(std::stoi(value), 0, 200);
                } catch (...) {
                    Log(L"Invalid monitor gap; using default.");
                }
            }
            continue;
        }

        if (key == "LEFT_RATIO") {
            try {
                config.leftRatio = std::clamp(std::stod(value), 0.10, 0.90);
            } catch (...) {
                Log(L"Invalid left_ratio; using default.");
            }
        } else if (key == "GAP") {
            try {
                config.gap = std::clamp(std::stoi(value), 0, 200);
            } catch (...) {
                Log(L"Invalid gap; using default.");
            }
        } else if (key == "RESPECT_TASKBAR") {
            config.respectTaskbar = ParseBool(value, config.respectTaskbar);
        } else if (key == "LOCK_ZONES") {
            config.lockZones = ParseBool(value, config.lockZones);
        } else if (key == "AUTO_START") {
            config.autoStart = ParseBool(value, config.autoStart);
        } else if (key == "HOTKEY_LEFT") {
            config.left = ParseHotkey(value, config.left);
        } else if (key == "HOTKEY_RIGHT") {
            config.right = ParseHotkey(value, config.right);
        } else if (key == "HOTKEY_RESTORE") {
            config.restore = ParseHotkey(value, config.restore);
        } else if (key == "HOTKEY_RELOAD") {
            config.reload = ParseHotkey(value, config.reload);
        } else if (key == "HOTKEY_EXIT") {
            config.exit = ParseHotkey(value, config.exit);
        }
    }

    return config;
}

void SaveConfig() {
    std::ofstream file(GetConfigPath().c_str());
    if (!file.good()) {
        Log(L"Could not write config.ini.");
        return;
    }

    file << "; Splitlet configuration\n"
         << "; Hotkey format: WIN+ALT+LEFT, CTRL+SHIFT+F1, ALT+Q\n\n"
         << "left_ratio=" << std::fixed << std::setprecision(2) << g_config.leftRatio << "\n"
         << "gap=" << g_config.gap << "\n"
         << "respect_taskbar=" << BoolToConfigValue(g_config.respectTaskbar) << "\n"
         << "lock_zones=" << BoolToConfigValue(g_config.lockZones) << "\n"
         << "auto_start=" << BoolToConfigValue(g_config.autoStart) << "\n\n"
         << "hotkey_left=" << HotkeyToString(g_config.left) << "\n"
         << "hotkey_right=" << HotkeyToString(g_config.right) << "\n"
         << "hotkey_restore=" << HotkeyToString(g_config.restore) << "\n"
         << "hotkey_reload=" << HotkeyToString(g_config.reload) << "\n"
         << "hotkey_exit=" << HotkeyToString(g_config.exit) << "\n";

    for (const auto& monitor : g_config.monitorSettings) {
        file << "\n"
             << "[monitor:" << ToUtf8(monitor.first) << "]\n"
             << "enabled=" << BoolToConfigValue(monitor.second.enabled) << "\n"
             << "left_ratio=" << std::fixed << std::setprecision(2) << monitor.second.leftRatio << "\n"
             << "gap=" << monitor.second.gap << "\n";
    }
}

void UnregisterHotkeys(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_LEFT);
    UnregisterHotKey(hwnd, HOTKEY_RIGHT);
    UnregisterHotKey(hwnd, HOTKEY_RESTORE);
    UnregisterHotKey(hwnd, HOTKEY_RELOAD);
    UnregisterHotKey(hwnd, HOTKEY_EXIT);
}

bool RegisterOneHotkey(HWND hwnd, int id, const Hotkey& hotkey, const wchar_t* name) {
    if (RegisterHotKey(hwnd, id, hotkey.modifiers | MOD_NOREPEAT, hotkey.vk)) {
        return true;
    }

    std::wstring message = L"Failed to register hotkey: ";
    message += name;
    Log(message);
    return false;
}

void RegisterConfiguredHotkeys(HWND hwnd) {
    UnregisterHotkeys(hwnd);
    RegisterOneHotkey(hwnd, HOTKEY_LEFT, g_config.left, L"left");
    RegisterOneHotkey(hwnd, HOTKEY_RIGHT, g_config.right, L"right");
    RegisterOneHotkey(hwnd, HOTKEY_RESTORE, g_config.restore, L"restore");
    RegisterOneHotkey(hwnd, HOTKEY_RELOAD, g_config.reload, L"reload");
    RegisterOneHotkey(hwnd, HOTKEY_EXIT, g_config.exit, L"exit");
}

void ApplyRuntimeState(HWND hwnd) {
    if (g_paused) {
        UnregisterHotkeys(hwnd);
        UninstallLocationHook();
        g_lockedZones.clear();
        return;
    }

    RegisterConfiguredHotkeys(hwnd);
    InstallLocationHook();
}

bool IsManagedWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return false;
    }

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((style & WS_CHILD) != 0 || (exStyle & WS_EX_TOOLWINDOW) != 0) {
        return false;
    }

    return true;
}

HWND ResolveManagedWindow(HWND hwnd) {
    if (IsManagedWindow(hwnd)) {
        return hwnd;
    }

    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (IsManagedWindow(root)) {
        return root;
    }

    HWND rootOwner = GetAncestor(hwnd, GA_ROOTOWNER);
    HWND lastPopup = rootOwner ? GetLastActivePopup(rootOwner) : nullptr;
    if (IsManagedWindow(lastPopup)) {
        return lastPopup;
    }
    if (IsManagedWindow(rootOwner)) {
        return rootOwner;
    }

    HWND parent = hwnd;
    while (parent) {
        parent = GetParent(parent);
        if (IsManagedWindow(parent)) {
            return parent;
        }
    }

    return nullptr;
}

HWND GetActiveManagedWindow() {
    return ResolveManagedWindow(GetForegroundWindow());
}

struct MonitorEntry {
    HMONITOR handle = nullptr;
    std::wstring device;
    std::wstring label;
    MonitorConfig settings;
};

struct MonitorEnumContext {
    const Config* config = nullptr;
    std::vector<MonitorEntry>* monitors = nullptr;
};

std::wstring GetMonitorDevice(HMONITOR monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    return info.szDevice;
}

MonitorConfig GetMonitorConfig(HMONITOR monitor) {
    const std::wstring device = GetMonitorDevice(monitor);
    const auto found = g_config.monitorSettings.find(device);
    if (found != g_config.monitorSettings.end()) {
        return found->second;
    }
    return DefaultMonitorConfig(g_config);
}

BOOL CALLBACK EnumMonitorProc(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* context = reinterpret_cast<MonitorEnumContext*>(data);

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);

    MonitorEntry entry{};
    entry.handle = monitor;
    entry.device = info.szDevice;
    entry.settings = DefaultMonitorConfig(*context->config);

    const auto found = context->config->monitorSettings.find(entry.device);
    if (found != context->config->monitorSettings.end()) {
        entry.settings = found->second;
    }

    std::wostringstream label;
    label << L"Display " << (context->monitors->size() + 1);
    if ((info.dwFlags & MONITORINFOF_PRIMARY) != 0) {
        label << L" (Primary)";
    }
    label << L" - " << entry.device;
    entry.label = label.str();

    context->monitors->push_back(entry);
    return TRUE;
}

std::vector<MonitorEntry> EnumerateMonitors(const Config& config) {
    std::vector<MonitorEntry> monitors;
    MonitorEnumContext context{&config, &monitors};
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorProc, reinterpret_cast<LPARAM>(&context));
    return monitors;
}

bool IsMonitorSplitEnabled(HWND hwnd) {
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    return GetMonitorConfig(monitor).enabled;
}

RECT GetMonitorRect(HWND hwnd) {
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    return g_config.respectTaskbar ? info.rcWork : info.rcMonitor;
}

RECT GetTargetRect(HWND hwnd, bool toLeft) {
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    const MonitorConfig monitorConfig = GetMonitorConfig(monitor);
    RECT work = GetMonitorRect(hwnd);
    const int width = work.right - work.left;
    const int splitX = work.left + static_cast<int>(std::lround(width * monitorConfig.leftRatio));
    const int halfGap = monitorConfig.gap / 2;

    RECT target{};
    if (toLeft) {
        target.left = work.left;
        target.top = work.top;
        target.right = std::max<LONG>(target.left + 100, splitX - halfGap);
        target.bottom = work.bottom;
    } else {
        target.left = std::min<LONG>(work.right - 100, splitX + (monitorConfig.gap - halfGap));
        target.top = work.top;
        target.right = work.right;
        target.bottom = work.bottom;
    }

    return target;
}

void SavePlacement(HWND hwnd) {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd, &placement)) {
        g_savedPlacements[hwnd] = placement;
    }
}

void MoveActiveWindow(bool toLeft) {
    HWND hwnd = GetActiveManagedWindow();
    if (!hwnd) {
        Log(L"No manageable foreground window.");
        return;
    }
    if (!IsMonitorSplitEnabled(hwnd)) {
        Log(L"Split is disabled for this monitor.");
        return;
    }

    SavePlacement(hwnd);

    if (IsIconic(hwnd) || IsZoomed(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    RECT work = GetMonitorRect(hwnd);
    const int width = work.right - work.left;
    const int height = work.bottom - work.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    RECT target = GetTargetRect(hwnd, toLeft);

    SetWindowPos(
        hwnd,
        nullptr,
        target.left,
        target.top,
        target.right - target.left,
        target.bottom - target.top,
        SWP_NOZORDER | SWP_NOACTIVATE
    );

    if (g_config.lockZones) {
        g_lockedZones[hwnd] = toLeft;
    }
}

void RestoreActiveWindow() {
    HWND hwnd = GetActiveManagedWindow();
    if (!hwnd) {
        Log(L"No manageable foreground window to restore.");
        return;
    }

    const auto found = g_savedPlacements.find(hwnd);
    if (found == g_savedPlacements.end()) {
        return;
    }

    SetWindowPlacement(hwnd, &found->second);
    g_savedPlacements.erase(found);
    g_lockedZones.erase(hwnd);
}

bool ExceedsRect(const RECT& rect, const RECT& bounds) {
    constexpr LONG tolerance = 2;
    return rect.left < bounds.left - tolerance ||
           rect.top < bounds.top - tolerance ||
           rect.right > bounds.right + tolerance ||
           rect.bottom > bounds.bottom + tolerance;
}

void EnforceLockedZone(HWND hwnd) {
    if (g_paused || g_enforcingZone || !g_config.lockZones) {
        return;
    }

    const auto found = g_lockedZones.find(hwnd);
    if (found == g_lockedZones.end()) {
        return;
    }

    if (!IsManagedWindow(hwnd)) {
        g_lockedZones.erase(found);
        g_savedPlacements.erase(hwnd);
        return;
    }
    if (!IsMonitorSplitEnabled(hwnd)) {
        g_lockedZones.erase(found);
        return;
    }

    RECT current{};
    if (!GetWindowRect(hwnd, &current)) {
        return;
    }

    RECT target = GetTargetRect(hwnd, found->second);
    if (!IsZoomed(hwnd) && !ExceedsRect(current, target)) {
        return;
    }

    g_enforcingZone = true;
    if (IsIconic(hwnd) || IsZoomed(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    SetWindowPos(
        hwnd,
        nullptr,
        target.left,
        target.top,
        target.right - target.left,
        target.bottom - target.top,
        SWP_NOZORDER | SWP_NOACTIVATE
    );
    g_enforcingZone = false;
}

void CALLBACK WinEventProc(
    HWINEVENTHOOK,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD,
    DWORD
) {
    if (event != EVENT_OBJECT_LOCATIONCHANGE || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
        return;
    }

    EnforceLockedZone(hwnd);
}

void InstallLocationHook() {
    if (g_locationHook) {
        return;
    }

    g_locationHook = SetWinEventHook(
        EVENT_OBJECT_LOCATIONCHANGE,
        EVENT_OBJECT_LOCATIONCHANGE,
        nullptr,
        WinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    if (!g_locationHook) {
        Log(L"Failed to install location hook.");
    }
}

void UninstallLocationHook() {
    if (!g_locationHook) {
        return;
    }

    UnhookWinEvent(g_locationHook);
    g_locationHook = nullptr;
}

void ApplyAutoStartSetting() {
    constexpr const wchar_t* runKeyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr const wchar_t* valueName = L"Splitlet";

    HKEY runKey = nullptr;
    const LONG openResult = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        runKeyPath,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &runKey,
        nullptr
    );

    if (openResult != ERROR_SUCCESS) {
        Log(L"Failed to open current-user Run registry key.");
        return;
    }

    if (g_config.autoStart) {
        const std::wstring command = L"\"" + GetExePath() + L"\"";
        const DWORD byteCount = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
        const LONG setResult = RegSetValueExW(
            runKey,
            valueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            byteCount
        );

        if (setResult != ERROR_SUCCESS) {
            Log(L"Failed to enable auto start.");
        }
    } else {
        const LONG deleteResult = RegDeleteValueW(runKey, valueName);
        if (deleteResult != ERROR_SUCCESS && deleteResult != ERROR_FILE_NOT_FOUND) {
            Log(L"Failed to disable auto start.");
        }
    }

    RegCloseKey(runKey);
}

void RefreshConfig(HWND hwnd, bool save) {
    if (save) {
        SaveConfig();
    }
    ApplyAutoStartSetting();
    ApplyRuntimeState(hwnd);
}

void ReloadConfig(HWND hwnd) {
    g_config = LoadConfig();
    RefreshConfig(hwnd, false);
    Log(L"Reloaded config.ini.");
}

void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));
    wcscpy_s(nid.szTip, L"Splitlet");

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        Log(L"Failed to add tray icon.");
    }

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void DeleteTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void AppendCheckedMenuItem(HMENU menu, UINT id, const wchar_t* text, bool checked, bool enabled = true) {
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
    item.wID = id;
    item.dwTypeData = const_cast<wchar_t*>(text);
    item.fState = (checked ? MFS_CHECKED : MFS_UNCHECKED) | (enabled ? MFS_ENABLED : MFS_DISABLED);
    InsertMenuItemW(menu, static_cast<UINT>(-1), TRUE, &item);
}

void AppendMenuText(HMENU menu, UINT id, const std::wstring& text) {
    AppendMenuW(menu, MF_STRING, id, text.c_str());
}

struct SettingsDialogState {
    Config config;
    std::vector<MonitorEntry> monitors;
    int selectedMonitor = 0;
    bool accepted = false;
};

void SetCheckbox(HWND hwnd, UINT id, bool checked) {
    SendDlgItemMessageW(hwnd, id, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool IsCheckboxChecked(HWND hwnd, UINT id) {
    return SendDlgItemMessageW(hwnd, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void LoadMonitorControls(HWND hwnd, const MonitorEntry& monitor) {
    SetWindowTextW(GetDlgItem(hwnd, ID_SETTINGS_LEFT_RATIO), FormatLeftRatio(monitor.settings.leftRatio).c_str());
    SetWindowTextW(GetDlgItem(hwnd, ID_SETTINGS_GAP), std::to_wstring(monitor.settings.gap).c_str());
    SetCheckbox(hwnd, ID_SETTINGS_MONITOR_ENABLED, monitor.settings.enabled);
}

bool ReadMonitorControls(HWND hwnd, MonitorEntry& monitor) {
    wchar_t leftRatioBuffer[64]{};
    wchar_t gapBuffer[64]{};
    GetDlgItemTextW(hwnd, ID_SETTINGS_LEFT_RATIO, leftRatioBuffer, static_cast<int>(sizeof(leftRatioBuffer) / sizeof(leftRatioBuffer[0])));
    GetDlgItemTextW(hwnd, ID_SETTINGS_GAP, gapBuffer, static_cast<int>(sizeof(gapBuffer) / sizeof(gapBuffer[0])));

    try {
        monitor.settings.leftRatio = std::clamp(std::stod(ToUtf8(leftRatioBuffer)), 0.10, 0.90);
        monitor.settings.gap = std::clamp(std::stoi(ToUtf8(gapBuffer)), 0, 200);
    } catch (...) {
        MessageBoxW(hwnd, L"Invalid display settings. Left Ratio must be 0.10-0.90, and Gap must be 0-200.", L"Splitlet", MB_OK | MB_ICONWARNING);
        return false;
    }

    monitor.settings.enabled = IsCheckboxChecked(hwnd, ID_SETTINGS_MONITOR_ENABLED);
    return true;
}

LRESULT CALLBACK SettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        auto* state = reinterpret_cast<SettingsDialogState*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams
        );
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        CreateWindowExW(0, L"STATIC", L"Display", WS_CHILD | WS_VISIBLE,
            18, 20, 112, 20, hwnd, nullptr, g_instance, nullptr);
        HWND monitorCombo = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            138, 18, 246, 180, hwnd, reinterpret_cast<HMENU>(ID_SETTINGS_MONITOR), g_instance, nullptr);
        for (const MonitorEntry& monitor : state->monitors) {
            SendMessageW(monitorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(monitor.label.c_str()));
        }
        SendMessageW(monitorCombo, CB_SETCURSEL, static_cast<WPARAM>(state->selectedMonitor), 0);

        CreateWindowExW(0, L"BUTTON", L"Enable Split on This Display",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            18, 56, 220, 22, hwnd, reinterpret_cast<HMENU>(ID_SETTINGS_MONITOR_ENABLED), g_instance, nullptr);

        CreateWindowExW(0, L"STATIC", L"Left Ratio", WS_CHILD | WS_VISIBLE,
            18, 90, 112, 20, hwnd, nullptr, g_instance, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            138, 88, 160, 24, hwnd, reinterpret_cast<HMENU>(ID_SETTINGS_LEFT_RATIO), g_instance, nullptr);
        CreateWindowExW(0, L"STATIC", L"0.10 - 0.90", WS_CHILD | WS_VISIBLE,
            306, 92, 78, 20, hwnd, nullptr, g_instance, nullptr);

        CreateWindowExW(0, L"STATIC", L"Gap", WS_CHILD | WS_VISIBLE,
            18, 126, 112, 20, hwnd, nullptr, g_instance, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            138, 124, 160, 24, hwnd, reinterpret_cast<HMENU>(ID_SETTINGS_GAP), g_instance, nullptr);
        CreateWindowExW(0, L"STATIC", L"0 - 200 px", WS_CHILD | WS_VISIBLE,
            306, 128, 78, 20, hwnd, nullptr, g_instance, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Respect Taskbar",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            18, 164, 180, 22, hwnd, reinterpret_cast<HMENU>(ID_SETTINGS_RESPECT_TASKBAR), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Lock Zones",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            18, 192, 180, 22, hwnd, reinterpret_cast<HMENU>(ID_SETTINGS_LOCK_ZONES), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Start with Windows",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            18, 220, 180, 22, hwnd, reinterpret_cast<HMENU>(ID_SETTINGS_AUTO_START), g_instance, nullptr);

        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            218, 258, 80, 26, hwnd, reinterpret_cast<HMENU>(ID_SETTINGS_OK), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            310, 258, 80, 26, hwnd, reinterpret_cast<HMENU>(ID_SETTINGS_CANCEL), g_instance, nullptr);

        if (!state->monitors.empty()) {
            LoadMonitorControls(hwnd, state->monitors[static_cast<size_t>(state->selectedMonitor)]);
        }
        SetCheckbox(hwnd, ID_SETTINGS_RESPECT_TASKBAR, state->config.respectTaskbar);
        SetCheckbox(hwnd, ID_SETTINGS_LOCK_ZONES, state->config.lockZones);
        SetCheckbox(hwnd, ID_SETTINGS_AUTO_START, state->config.autoStart);
        SendDlgItemMessageW(hwnd, ID_SETTINGS_LEFT_RATIO, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(hwnd, ID_SETTINGS_LEFT_RATIO));
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_SETTINGS_MONITOR && HIWORD(wParam) == CBN_SELCHANGE) {
            auto* state = reinterpret_cast<SettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state || state->monitors.empty()) {
                return 0;
            }

            const int nextSelection = static_cast<int>(SendDlgItemMessageW(hwnd, ID_SETTINGS_MONITOR, CB_GETCURSEL, 0, 0));
            if (nextSelection < 0 || nextSelection >= static_cast<int>(state->monitors.size())) {
                return 0;
            }

            if (!ReadMonitorControls(hwnd, state->monitors[static_cast<size_t>(state->selectedMonitor)])) {
                SendDlgItemMessageW(hwnd, ID_SETTINGS_MONITOR, CB_SETCURSEL, static_cast<WPARAM>(state->selectedMonitor), 0);
                return 0;
            }

            state->selectedMonitor = nextSelection;
            LoadMonitorControls(hwnd, state->monitors[static_cast<size_t>(state->selectedMonitor)]);
            return 0;
        }

        if (LOWORD(wParam) == ID_SETTINGS_OK) {
            auto* state = reinterpret_cast<SettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state || state->monitors.empty()) {
                return 0;
            }
            if (!ReadMonitorControls(hwnd, state->monitors[static_cast<size_t>(state->selectedMonitor)])) {
                return 0;
            }

            state->config.respectTaskbar = IsCheckboxChecked(hwnd, ID_SETTINGS_RESPECT_TASKBAR);
            state->config.lockZones = IsCheckboxChecked(hwnd, ID_SETTINGS_LOCK_ZONES);
            state->config.autoStart = IsCheckboxChecked(hwnd, ID_SETTINGS_AUTO_START);
            state->config.monitorSettings.clear();
            for (const MonitorEntry& monitor : state->monitors) {
                state->config.monitorSettings[monitor.device] = monitor.settings;
            }
            state->config.leftRatio = state->monitors.front().settings.leftRatio;
            state->config.gap = state->monitors.front().settings.gap;
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == ID_SETTINGS_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool ShowSettingsDialog(HWND owner, Config& config) {
    constexpr const wchar_t* className = L"SplitletSettingsDialog";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = SettingsDialogProc;
        wc.hInstance = g_instance;
        wc.lpszClassName = className;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    SettingsDialogState state{};
    state.config = config;
    state.monitors = EnumerateMonitors(config);
    if (state.monitors.empty()) {
        MonitorEntry fallback{};
        fallback.device = L"default";
        fallback.label = L"Default Display";
        fallback.settings = DefaultMonitorConfig(config);
        state.monitors.push_back(fallback);
    }

    const int width = 426;
    const int height = 338;
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfoW(monitor, &monitorInfo);
    const RECT work = monitorInfo.rcWork;
    const int x = work.left + ((work.right - work.left - width) / 2);
    const int y = work.top + ((work.bottom - work.top - height) / 2);

    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        className,
        L"Splitlet Settings",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        g_instance,
        &state
    );

    if (!dialog) {
        return false;
    }

    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG msg{};
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);

    if (state.accepted) {
        config = state.config;
    }
    return state.accepted;
}

void ShowTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenuText(menu, ID_TRAY_SETTINGS, L"Settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendCheckedMenuItem(menu, ID_TRAY_PAUSE, g_paused ? L"Resume" : L"Pause", g_paused);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursor.x, cursor.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void HandleTrayCommand(HWND hwnd, UINT command) {
    switch (command) {
    case ID_TRAY_SETTINGS: {
        Config nextConfig = g_config;
        if (ShowSettingsDialog(hwnd, nextConfig)) {
            g_config = nextConfig;
            g_lockedZones.clear();
            RefreshConfig(hwnd, true);
        }
        break;
    }
    case ID_TRAY_PAUSE:
        g_paused = !g_paused;
        if (g_paused) {
            g_lockedZones.clear();
        }
        ApplyRuntimeState(hwnd);
        Log(g_paused ? L"Paused Splitlet." : L"Resumed Splitlet.");
        break;
    case ID_TRAY_EXIT:
        DestroyWindow(hwnd);
        break;
    default:
        break;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_taskbarCreatedMessage) {
        AddTrayIcon(hwnd);
        return 0;
    }

    switch (message) {
    case WM_COMMAND:
        HandleTrayCommand(hwnd, LOWORD(wParam));
        return 0;
    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
            ShowTrayMenu(hwnd);
        }
        return 0;
    case WM_HOTKEY:
        switch (static_cast<int>(wParam)) {
        case HOTKEY_LEFT:
            if (g_paused) {
                return 0;
            }
            MoveActiveWindow(true);
            return 0;
        case HOTKEY_RIGHT:
            if (g_paused) {
                return 0;
            }
            MoveActiveWindow(false);
            return 0;
        case HOTKEY_RESTORE:
            if (g_paused) {
                return 0;
            }
            RestoreActiveWindow();
            return 0;
        case HOTKEY_RELOAD:
            ReloadConfig(hwnd);
            return 0;
        case HOTKEY_EXIT:
            DestroyWindow(hwnd);
            return 0;
        default:
            return 0;
        }
    case WM_DESTROY:
        DeleteTrayIcon(hwnd);
        UninstallLocationHook();
        UnregisterHotkeys(hwnd);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;
    g_exeDir = GetExeDir();
    const std::wstring logPath = JoinPath(g_exeDir, L"Splitlet.log");
    g_log.open(logPath.c_str(), std::ios::app);
    Log(L"Starting Splitlet.");
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Splitlet_single_instance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        Log(L"Another instance is already running.");
        CloseHandle(mutex);
        return 0;
    }

    const wchar_t className[] = L"SplitletHiddenWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;

    if (!RegisterClassW(&wc)) {
        Log(L"Failed to register hidden window class.");
        if (mutex) {
            CloseHandle(mutex);
        }
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        className,
        L"Splitlet",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        instance,
        nullptr
    );

    if (!hwnd) {
        Log(L"Failed to create hidden message window.");
        if (mutex) {
            CloseHandle(mutex);
        }
        return 1;
    }

    g_config = LoadConfig();
    AddTrayIcon(hwnd);
    RefreshConfig(hwnd, false);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Log(L"Exiting Splitlet.");

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }

    return 0;
}
