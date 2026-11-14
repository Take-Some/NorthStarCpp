#include "qt_asset_card_delegate.hpp"
#include <QAbstractItemView>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QStyle>
#include <QTextLayout>
#include <QVariant>
#include <algorithm>

namespace northstarcpp::editor {
namespace {
constexpr int outerGap=6;
constexpr int innerGap=8;
constexpr int titleHeight=36;
constexpr int stripeHeight=5;

// A multi-line title is laid out, clipped, and ellipsized. It never extends
// beyond its own area or over the metadata footer.
void drawTitle(QPainter* painter,QRect const& bounds,QString const& title) {
    QFont font=painter->font();
    font.setBold(true);
    font.setPixelSize(13);
    QFontMetrics fm(font);
    painter->setFont(font);
    painter->setPen(QColor("#e9eff7"));
    QTextLayout layout(title,font);
    layout.beginLayout();
    int y=0;
    for(int i=0;i<2;++i) {
        auto line=layout.createLine();
        if(!line.isValid())break;
        line.setLineWidth(bounds.width());
        if(line.height()<=0)break;
        auto const start=line.textStart();
        auto const used=line.textLength();
        QString part=title.mid(start,used).trimmed();
        if(i==1 && start+used<title.size())
            part=fm.elidedText(title.mid(start).trimmed(),
                               Qt::ElideRight,bounds.width());
        if(part.isEmpty())break;
        painter->drawText(QRect(bounds.left(),bounds.top()+y,
                                bounds.width(),std::min(18,bounds.height()-y)),
                          Qt::AlignLeft|Qt::AlignVCenter,part);
        y+=18;
        if(y>=titleHeight)break;
    }
    layout.endLayout();
}
}
AssetCardDelegate::AssetCardDelegate(QObject* parent):QStyledItemDelegate(parent) {}
void AssetCardDelegate::setPreviewSize(int value) {
    value=std::clamp(value,80,240);
    cell_width_=std::clamp(value+56,168,296);
    preview_height_=std::clamp(value-12,96,220);
}
QSize AssetCardDelegate::sizeHint(QStyleOptionViewItem const&,
                                 QModelIndex const&) const {
    return cellSize();
}
void AssetCardDelegate::paint(QPainter* painter,
                             QStyleOptionViewItem const& option,
                             QModelIndex const& index) const {
    if(!index.isValid())return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing,true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform,true);
    // Hard cell boundary: even maliciously long names cannot overlap a
    // neighboring card. 12px gutters preserve hover/selection outlines.
    painter->setClipRect(option.rect);
    QRect const card=option.rect.adjusted(outerGap,outerGap,-outerGap,-outerGap);
    if(card.width()<140 || card.height()<180) {
        painter->restore();
        return;
    }
    bool const selected=option.state.testFlag(QStyle::State_Selected);
    bool const hovered=option.state.testFlag(QStyle::State_MouseOver);
    QColor outline=selected?QColor("#64b5ec"):
                   hovered?QColor("#526c85"):QColor("#364456");
    painter->setBrush(QColor(selected?"#25384b":hovered?"#273140":"#202734"));
    painter->setPen(QPen(outline,selected?2:1));
    painter->drawRoundedRect(card,7,7);

    QRect const previewBox(card.left()+innerGap,card.top()+innerGap,
                           card.width()-2*innerGap,preview_height_);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor("#151d29"));
    painter->drawRoundedRect(previewBox,5,5);

    // Actual asset preview takes priority; embedded folder artwork and
    // unrenderable asset type icons remain independent decorations.
    QImage picture=qvariant_cast<QImage>(index.data(asset_card_roles::preview_image));
    if(picture.isNull()) {
        QIcon icon=qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if(!icon.isNull())
            picture=icon.pixmap(QSize(256,256)).toImage();
    }
    if(!picture.isNull()) {
        auto const inner=previewBox.adjusted(9,9,-9,-9);
        QSize fitted=picture.size();
        fitted.scale(inner.size(),Qt::KeepAspectRatio);
        if(fitted.width()>0 && fitted.height()>0) {
            QRect target(QPoint(0,0),fitted);
            target.moveCenter(inner.center());
            painter->save();
            painter->setClipRect(previewBox.adjusted(2,2,-2,-2));
            painter->drawImage(target,picture);
            painter->restore();
        }
    }

    QColor accent=qvariant_cast<QColor>(index.data(asset_card_roles::accent_color));
    if(!accent.isValid())accent=QColor("#718198");
    QRect const stripe(previewBox.left(),previewBox.bottom()+7,
                       previewBox.width(),stripeHeight);
    painter->fillRect(stripe,accent);

    QRect const titleBox(stripe.left(),stripe.bottom()+7,
                         stripe.width(),titleHeight);
    painter->save();
    painter->setClipRect(titleBox);
    drawTitle(painter,titleBox,index.data(asset_card_roles::title).toString());
    painter->restore();

    QRect const metadata(titleBox.left(),titleBox.bottom()+3,
                         titleBox.width(),17);
    QFont font=painter->font();
    font.setBold(false);
    font.setPixelSize(11);
    painter->setFont(font);
    QFontMetrics fm(font);
    painter->setPen(QColor("#a4b6c9"));
    QString const detail=fm.elidedText(
        index.data(asset_card_roles::subtitle).toString(),
        Qt::ElideRight,metadata.width());
    painter->drawText(metadata,Qt::AlignVCenter|Qt::AlignLeft,detail);
    painter->restore();
}
} // namespace northstarcpp::editor
