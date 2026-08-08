#include "settings.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace perfmon {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\PerformanceMonitor";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"PerformanceMonitor";

constexpr wchar_t kAlwaysOnTopValue[] = L"AlwaysOnTop";
constexpr wchar_t kOpacityValue[] = L"OpacityPercent";
constexpr wchar_t kShowCpuValue[] = L"ShowCpu";
constexpr wchar_t kShowGpuValue[] = L"ShowGpu";
constexpr wchar_t kWindowSizeValue[] = L"WindowSize";
constexpr wchar_t kStartWithWindowsValue[] = L"StartWithWindows";
constexpr wchar_t kWindowXValue[] = L"WindowX";
constexpr wchar_t kWindowYValue[] = L"WindowY";
constexpr wchar_t kHasWindowPositionValue[] = L"HasWindowPosition";

[[nodiscard]] bool ReadDword(HKEY key, const wchar_t* name, DWORD& value) noexcept {
    DWORD size = sizeof(value);
    DWORD type = 0;
    return RegQueryValueExW(
               key,
               name,
               nullptr,
               &type,
               reinterpret_cast<BYTE*>(&value),
               &size) == ERROR_SUCCESS &&
           type == REG_DWORD && size == sizeof(value);
}

void WriteDword(HKEY key, const wchar_t* name, DWORD value) noexcept {
    (void)RegSetValueExW(
        key,
        name,
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&value),
        sizeof(value));
}

[[nodiscard]] DWORD BoolToDword(bool value) noexcept {
    return value ? 1U : 0U;
}

[[nodiscard]] int SanitizeOpacity(DWORD value) noexcept {
    switch (value) {
    case 60:
    case 80:
    case 100:
        return static_cast<int>(value);
    default:
        return 100;
    }
}

[[nodiscard]] ui::WindowSizePreset SanitizeWindowSize(DWORD value) noexcept {
    switch (value) {
    case static_cast<DWORD>(ui::WindowSizePreset::Small):
        return ui::WindowSizePreset::Small;
    case static_cast<DWORD>(ui::WindowSizePreset::Medium):
        return ui::WindowSizePreset::Medium;
    case static_cast<DWORD>(ui::WindowSizePreset::Large):
        return ui::WindowSizePreset::Large;
    default:
        return ui::WindowSizePreset::Medium;
    }
}

[[nodiscard]] std::wstring CurrentExecutablePath() noexcept {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::wstring(buffer.data(), length);
}

} // namespace

AppSettings SettingsStore::Load() const noexcept {
    AppSettings settings{};

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return settings;
    }

    DWORD value = 0;
    if (ReadDword(key, kAlwaysOnTopValue, value)) {
        settings.always_on_top = value != 0;
    }
    if (ReadDword(key, kOpacityValue, value)) {
        settings.opacity_percent = SanitizeOpacity(value);
    }
    if (ReadDword(key, kShowCpuValue, value)) {
        settings.show_cpu = value != 0;
    }
    if (ReadDword(key, kShowGpuValue, value)) {
        settings.show_gpu = value != 0;
    }
    if (!settings.show_cpu && !settings.show_gpu) {
        settings.show_cpu = true;
    }
    if (ReadDword(key, kWindowSizeValue, value)) {
        settings.window_size = SanitizeWindowSize(value);
    }
    if (ReadDword(key, kStartWithWindowsValue, value)) {
        settings.start_with_windows = value != 0;
    }
    if (ReadDword(key, kHasWindowPositionValue, value)) {
        settings.has_window_position = value != 0;
    }

    DWORD x = 0;
    DWORD y = 0;
    if (settings.has_window_position &&
        ReadDword(key, kWindowXValue, x) &&
        ReadDword(key, kWindowYValue, y)) {
        settings.window_position.x = static_cast<LONG>(x);
        settings.window_position.y = static_cast<LONG>(y);
    } else {
        settings.has_window_position = false;
        settings.window_position = POINT{AppSettings::kDefaultWindowX, AppSettings::kDefaultWindowY};
    }

    RegCloseKey(key);
    return settings;
}

void SettingsStore::Save(const AppSettings& settings) const noexcept {
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kSettingsKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return;
    }

    WriteDword(key, kAlwaysOnTopValue, BoolToDword(settings.always_on_top));
    WriteDword(key, kOpacityValue, static_cast<DWORD>(settings.opacity_percent));
    WriteDword(key, kShowCpuValue, BoolToDword(settings.show_cpu));
    WriteDword(key, kShowGpuValue, BoolToDword(settings.show_gpu));
    WriteDword(key, kWindowSizeValue, static_cast<DWORD>(settings.window_size));
    WriteDword(key, kStartWithWindowsValue, BoolToDword(settings.start_with_windows));
    WriteDword(key, kHasWindowPositionValue, BoolToDword(settings.has_window_position));

    if (settings.has_window_position) {
        WriteDword(key, kWindowXValue, static_cast<DWORD>(settings.window_position.x));
        WriteDword(key, kWindowYValue, static_cast<DWORD>(settings.window_position.y));
    }

    RegCloseKey(key);
}

void SettingsStore::SetStartWithWindows(bool enabled) const noexcept {
    WriteStartupRegistration(enabled);
}

void SettingsStore::WriteStartupRegistration(bool enabled) noexcept {
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kRunKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return;
    }

    if (!enabled) {
        (void)RegDeleteValueW(key, kRunValueName);
        RegCloseKey(key);
        return;
    }

    const std::wstring executable_path = CurrentExecutablePath();
    if (executable_path.empty()) {
        RegCloseKey(key);
        return;
    }

    const std::wstring command = L"\"" + executable_path + L"\"";
    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    (void)RegSetValueExW(
        key,
        kRunValueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        bytes);

    RegCloseKey(key);
}

} // namespace perfmon
