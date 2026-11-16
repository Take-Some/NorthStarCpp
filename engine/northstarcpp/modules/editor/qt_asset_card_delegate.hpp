#pragma once
// NorthStarCpp Asset Browser: complete UE-style item cards.
// The model supplies data; painting never touches files or decodes assets.
#include <QStyledItemDelegate>
#include <QSize>

namespace northstarcpp::editor {
namespace asset_card_roles {
// Preserve existing role offsets used by selection/drag-drop/Qt tests.
inline constexpr int path=Qt::UserRole+1;
inline constexpr int directory=Qt::UserRole+2;
inline constexpr int kind=Qt::UserRole+3;
inline constexpr int reference=Qt::UserRole+4;
inline constexpr int preview_image=Qt::UserRole+5;
inline constexpr int title=Qt::UserRole+6;
inline constexpr int subtitle=Qt::UserRole+7;
inline constexpr int accent_color=Qt::UserRole+8;
inline constexpr int git_status=Qt::UserRole+9;
}
class AssetCardDelegate final: public QStyledItemDelegate {
public:
    explicit AssetCardDelegate(QObject* parent=nullptr);
    // Width includes 6px cell gutters on both sides; card is drawn inside.
    void setPreviewSize(int px);
    QSize cellSize() const noexcept {return {cell_width_,preview_height_+94};}
    QSize sizeHint(QStyleOptionViewItem const&,QModelIndex const&) const override;
    void paint(QPainter*,QStyleOptionViewItem const&,QModelIndex const&) const override;
    static constexpr int defaultCellWidth=200; // card 188 x 214
private:
    int cell_width_=defaultCellWidth;
    int preview_height_=132;
};
} // namespace northstarcpp::editor
