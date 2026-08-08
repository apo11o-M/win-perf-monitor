#pragma once

#include "../ui/ui_state.hpp"
#include "../win32_headers.hpp"

namespace perfmon {

struct AppSettings {
    static constexpr int kDefaultWindowX = 100;
    static constexpr int kDefaultWindowY = 100;

    bool always_on_top = true;
    int opacity_percent = 100;
    bool show_cpu = true;
    bool show_gpu = true;
    ui::WindowSizePreset window_size = ui::WindowSizePreset::Medium;
    bool start_with_windows = false;

    bool has_window_position = false;
    POINT window_position{kDefaultWindowX, kDefaultWindowY};
};

class SettingsStore {
public:
    [[nodiscard]] AppSettings Load() const noexcept;
    void Save(const AppSettings& settings) const noexcept;
    void SetStartWithWindows(bool enabled) const noexcept;

private:
    static void WriteStartupRegistration(bool enabled) noexcept;
};

} // namespace perfmon
