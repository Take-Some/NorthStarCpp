// Qt Content Browser: shared UTF-8/path conversions used by UI and actions.
#pragma once
#include <QString>
#include <filesystem>
namespace northstarcpp::editor::browser_internal {
inline QString qt(std::filesystem::path const& path) {
    auto const value=path.generic_u8string();
    return QString::fromUtf8(value.data(),static_cast<int>(value.size()));
}
inline std::filesystem::path native(QString const& path) {
    return std::filesystem::u8path(path.toUtf8().toStdString());
}
}
