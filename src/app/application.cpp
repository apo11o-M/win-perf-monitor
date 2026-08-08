#include "application.hpp"

#include "main_window.hpp"
#include <stdexcept>

namespace perfmon {

Application::Application(HINSTANCE instance) noexcept : instance_(instance) {}

Application::~Application() {
    if (com_initialized_) {
        CoUninitialize();
    }
}

void Application::EnableDpiAwareness() noexcept {
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

void Application::InitializeCom() {
    const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(result)) {
        throw std::runtime_error("CoInitializeEx failed");
    }
    com_initialized_ = true;
}

void Application::InitializeGraphicsFactories() {
    HRESULT result = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        d2d_factory_.GetAddressOf());
    if (FAILED(result)) {
        throw std::runtime_error("D2D1CreateFactory failed");
    }

    result = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwrite_factory_.GetAddressOf()));
    if (FAILED(result)) {
        throw std::runtime_error("DWriteCreateFactory failed");
    }
}

int Application::Run(int show_command) {
    EnableDpiAwareness();
    InitializeCom();
    InitializeGraphicsFactories();

    MainWindow main_window(
        instance_,
        d2d_factory_.Get(),
        dwrite_factory_.Get());
    main_window.Create(show_command);

    MSG message{};
    while (true) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            throw std::runtime_error("GetMessageW failed");
        }
        if (result == 0) {
            break;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

} // namespace perfmon
