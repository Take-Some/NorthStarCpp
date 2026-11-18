#include "window_win32_controls.hpp"
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace northstarcpp::win32_controls {
std::wstring to_wide(std::string_view text)
{
    int const size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (!size)
        return L"";
    std::wstring value(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), value.data(), size);
    return value;
}

std::string to_utf8(std::wstring_view text)
{
    if (text.empty())
        return {};
    int const size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size)
        throw std::runtime_error("Object name contains invalid characters");
    std::string value(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        value.data(), size, nullptr, nullptr);
    return value;
}

std::wstring get_text(HWND control)
{
    int const length = GetWindowTextLengthW(control);
    std::wstring text(length + 1, L'\0');
    text.resize(GetWindowTextW(control, text.data(), length + 1));
    return text;
}

float parse_number(HWND control)
{
    std::wistringstream stream(get_text(control));
    stream.imbue(std::locale::classic());
    float number = 0.f;
    if (!(stream >> number) || !(stream >> std::ws).eof())
        throw std::runtime_error("Enter a number using a decimal point");
    return number;
}

void set_number(HWND control, float number)
{
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(8) << number;
    SetWindowTextW(control, stream.str().c_str());
}

 } // namespace northstarcpp::win32_controls
