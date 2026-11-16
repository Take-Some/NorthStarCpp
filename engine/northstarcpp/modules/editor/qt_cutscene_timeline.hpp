// Multi-track cinematic timeline editor for NorthStar Qt Editor.
#pragma once
#include <filesystem>
#include <functional>
class QWidget;
namespace northstarcpp {struct CameraPose;}
namespace northstarcpp::editor {
using CutscenePreview=std::function<void(CameraPose const&)>;
void openCutsceneTimeline(QWidget* parent,
    std::filesystem::path const& project_assets,
    CutscenePreview preview={});
}
