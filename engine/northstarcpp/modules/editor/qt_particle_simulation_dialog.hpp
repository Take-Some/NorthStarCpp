#pragma once
#include <filesystem>
#include <string>
class QWidget;

namespace northstarcpp::editor {
// Live, bounded NVPT particle simulation in the asset browser.
// Reuses the engine's authenticated YPT codec; CPU preview renders
// billboards until the native game Particle Render Pass reaches parity.
int openParticleSimulationPreview(QWidget* parent,
    std::filesystem::path const& content_root,
    std::string const& asset_reference);
}
