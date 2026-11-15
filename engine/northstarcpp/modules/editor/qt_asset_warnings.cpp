#include "qt_asset_warnings.hpp"
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

namespace northstarcpp::editor {
namespace {
QString title(AssetWarningKind type) {
    switch(type) {
    case AssetWarningKind::Model:return "Missing model";
    case AssetWarningKind::Material:return "Missing material";
    case AssetWarningKind::Texture:return "Missing texture";
    case AssetWarningKind::Prefab:return "Prefab unavailable";
    case AssetWarningKind::Other:return "Asset warning";
    }
    return "Asset warning";
}
QFrame* card(AssetWarning const& warning,QWidget* parent) {
    auto* frame=new QFrame(parent);
    frame->setObjectName("assetWarningCard");
    frame->setAccessibleName(title(warning.kind));
    frame->setFrameShape(QFrame::NoFrame);
    frame->setStyleSheet(
        "QFrame#assetWarningCard{background-color:#332a1b;"
        "border:1px solid #bc8d45;border-left:4px solid #e6b55e;"
        "border-radius:6px;}"
        "QFrame#assetWarningCard QLabel{border:0;background:transparent;}"
        "QFrame#assetWarningCard QPushButton{background:#50402a;"
        "color:#f5dca7;border:1px solid #876e43;border-radius:4px;"
        "padding:3px 7px;}"
        "QFrame#assetWarningCard QPushButton:hover{background:#655132;}");
    auto* layout=new QHBoxLayout(frame);
    layout->setContentsMargins(11,10,10,11);
    layout->setSpacing(9);
    auto* symbol=new QLabel(frame);
    symbol->setObjectName("assetWarningIcon");
    symbol->setPixmap(QApplication::style()->standardIcon(
        QStyle::SP_MessageBoxWarning).pixmap(22,22));
    symbol->setFixedSize(24,24);
    symbol->setAlignment(Qt::AlignCenter);
    symbol->setAccessibleName("Warning");
    layout->addWidget(symbol,0,Qt::AlignTop);
    auto* messages=new QVBoxLayout;
    messages->setSpacing(3);
    auto* heading=new QLabel(title(warning.kind),frame);
    heading->setObjectName("assetWarningTitle");
    heading->setStyleSheet("color:#ffdb91;font-weight:700;font-size:12px;");
    messages->addWidget(heading);
    if(!warning.reference.isEmpty()) {
        auto* reference=new QLabel(warning.reference,frame);
        reference->setObjectName("assetWarningReference");
        reference->setStyleSheet("color:#ffedc5;font-size:11px;");
        reference->setWordWrap(true);
        reference->setTextInteractionFlags(Qt::TextSelectableByMouse);
        reference->setToolTip(warning.reference);
        reference->setAccessibleName("Resource reference");
        messages->addWidget(reference);
    }
    if(!warning.reason.isEmpty()) {
        auto* details=new QLabel(warning.reason,frame);
        details->setObjectName("assetWarningReason");
        details->setStyleSheet("color:#d9c3a0;font-size:11px;");
        details->setWordWrap(true);
        details->setTextInteractionFlags(Qt::TextSelectableByMouse);
        messages->addWidget(details);
    }
    layout->addLayout(messages,1);
    if(!warning.reference.isEmpty()) {
        auto* copy=new QPushButton("Copy",frame);
        copy->setObjectName("assetWarningCopyReference");
        copy->setToolTip("Copy missing resource reference");
        QObject::connect(copy,&QPushButton::clicked,frame,
            [reference=warning.reference] {
                QApplication::clipboard()->setText(reference);
            });
        layout->addWidget(copy,0,Qt::AlignTop);
    }
    return frame;
}
} // namespace
AssetWarningPanel::AssetWarningPanel(QWidget* parent):QWidget(parent) {
    setObjectName("assetPreviewWarnings");
    setAccessibleName("Asset dependency warnings");
    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(5);
    auto* heading=new QLabel("ASSET WARNINGS",this);
    heading->setObjectName("assetWarningsHeading");
    heading->setStyleSheet("color:#f2c778;font-size:12px;font-weight:700");
    layout->addWidget(heading);
    auto* scroll=new QScrollArea(this);
    scroll->setObjectName("assetWarningsScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea{background:transparent;border:none;}");
    card_container_=new QWidget(scroll);
    card_container_->setObjectName("assetWarningsContent");
    auto* items=new QVBoxLayout(card_container_);
    items->setContentsMargins(0,0,0,0);
    items->setSpacing(6);
    scroll->setWidget(card_container_);
    layout->addWidget(scroll);
    setMaximumHeight(240);
    hide();
}
void AssetWarningPanel::setWarnings(std::vector<AssetWarning> const& warnings) {
    auto* layout=qobject_cast<QVBoxLayout*>(card_container_->layout());
    while(auto* item=layout->takeAt(0)) {
        if(auto* widget=item->widget())delete widget;
        delete item;
    }
    warning_count_=static_cast<int>(warnings.size());
    for(auto const& warning:warnings)
        layout->addWidget(card(warning,card_container_));
    layout->addStretch();
    // QWidget::show() on an unshown parent can force an unexpected top-level
    // window. setVisible only after parent/layout owns the panel.
    setVisible(!warnings.empty());
}
} // namespace northstarcpp::editor
