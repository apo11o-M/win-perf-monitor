#include "app/application.hpp"
#include "text_util.hpp"
#include "win32_headers.hpp"

#include <cstdlib>
#include <exception>
#include <string>

namespace {
constexpr wchar_t kWindowTitle[] = L"Performance Monitor";
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int show_command) {
    try {
        perfmon::Application application(instance);
        return application.Run(show_command);
    } catch (const std::exception& exception) {
        const std::wstring message = L"Performance Monitor failed to start.\n\n" +
                                     perfmon::Utf8ToWide(exception.what());
        MessageBoxW(nullptr, message.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    } catch (...) {
        MessageBoxW(
            nullptr,
            L"Performance Monitor failed to start because of an unknown error.",
            kWindowTitle,
            MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
}
