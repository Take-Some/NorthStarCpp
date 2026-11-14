// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#pragma once

#include <northstarcpp/project_catalog.hpp>
#include <windows.h>
#include <filesystem>

namespace northstarcpp::projects::ui {
// Returns the resulting directory, or an empty path when cancelled.
std::filesystem::path project_properties_dialog(
    HWND owner, Catalog& catalog, Project const& project, bool duplicate);
}
