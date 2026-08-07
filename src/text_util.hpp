#pragma once

#include "win32_headers.hpp"

#include <string>
#include <string_view>

namespace perfmon {

[[nodiscard]] inline std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);

    if (required <= 0) {
        return L"<UTF-8 conversion failed>";
    }

    std::wstring converted(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        converted.data(),
        required);

    if (written <= 0) {
        return L"<UTF-8 conversion failed>";
    }

    return converted;
}

} // namespace perfmon
