#pragma once
// Register externally packed Qt artwork from NorthStar's active VFS.
// Does not compile any PNG/RCC payload into application binaries.
namespace northstarcpp::editor {
bool ensure_northstar_qt_icons() noexcept;
}
