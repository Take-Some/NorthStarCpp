// NorthStar Content Browser: read-only sound inspector and PCM audition.
#pragma once
#include <filesystem>
#include <string>
class QWidget;
namespace northstarcpp::editor {
int openAudioPreviewModal(QWidget* parent,
                          std::filesystem::path const& physical,
                          std::string const& reference);
}
