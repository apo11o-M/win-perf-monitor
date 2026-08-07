#pragma once

#include "../win32_headers.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace perfmon {

class Application {
public:
    explicit Application(HINSTANCE instance) noexcept;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int Run(int show_command);

private:
    static void EnableDpiAwareness() noexcept;
    void InitializeCom();
    void InitializeGraphicsFactories();

    HINSTANCE instance_ = nullptr;
    bool com_initialized_ = false;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory_;
};

} // namespace perfmon
