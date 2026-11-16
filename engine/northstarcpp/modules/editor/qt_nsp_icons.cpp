#include "qt_nsp_icons.hpp"
#include <northstarcpp/assets.hpp>
#include <QResource>
#include <QDebug>
#include <cstdint>
#include <exception>
#include <vector>
#include <utility>

namespace northstarcpp::editor {
bool ensure_northstar_qt_icons() noexcept
{
    // QResource references the registered byte array without copying it.
    // Keep storage alive until process exit, including while QIcon uses it.
    static std::vector<std::uint8_t> resource_bytes;
    static const bool ready=[] {
        try {
            AssetStore assets;
            assets.mount_runtime_archives();
            auto bytes=assets.read("ui/northstar_ui_icons.rcc");
            if(bytes.empty() || bytes.size()>16*1024*1024)
                throw std::runtime_error("NSP Qt resource is empty or too large");
            resource_bytes=std::move(bytes);
            if(!QResource::registerResource(resource_bytes.data()))
                throw std::runtime_error("Qt rejected the .rcc data from NSP");
            auto const folder=QResource(QStringLiteral(
                ":/northstar/icons/assetBrowser/folder.png"));
            auto const play=QResource(QStringLiteral(":/northstar/play/play.png"));
            if(!folder.isValid() || !play.isValid())
                throw std::runtime_error("NSP Qt resources are missing required icons");
            return true;
        } catch(std::exception const& error) {
            qWarning().noquote()<<"NorthStar UI icons unavailable from .nsp:"
                                <<error.what();
            return false; // No renderer/editor crash for optional artwork.
        }
    }();
    return ready;
}
} // namespace northstarcpp::editor
