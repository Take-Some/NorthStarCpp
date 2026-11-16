// Native Qt 6 Content Browser for NorthStar Editor.
// No dependency on HWND, Windows Common Controls, or shell icons.
#include "qt_content_browser.hpp"
#include "qt_nsp_icons.hpp"
#include "qt_content_browser_internal.hpp"
#include "qt_content_browser_thumbnails.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/embedded_script_api.hpp>
#include "editor_asset_thumbnails.hpp"
#include "qt_asset_preview_dialog.hpp"
#include "qt_text_asset_editor.hpp"
#include "qt_audio_preview_dialog.hpp"
#include "qt_prefab_preview.hpp"
#include "editor_prefab_composition.hpp"
#include "editor_asset_material_source.hpp"
#include "qt_asset_card_delegate.hpp"
#include "qt_content_browser_favorites.hpp"
#include "asset_browser_query.hpp"
#include "asset_browser_collections.hpp"
#include "asset_team_collections.hpp"
#include "asset_dependency_registry.hpp"

#include <QtConcurrent/QtConcurrent>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QPlainTextEdit>
#include <QFontDatabase>
#include <QFile>
#include <QDir>
#include <QCursor>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSettings>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QShortcut>
#include <QCryptographicHash>
#include <QCollator>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QFuture>
#include <QFutureWatcher>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace northstarcpp::editor {
namespace fs=std::filesystem;
namespace {
constexpr int role_path=asset_card_roles::path;
constexpr int role_directory=asset_card_roles::directory;
constexpr int role_kind=asset_card_roles::kind;
constexpr int role_reference=asset_card_roles::reference;
constexpr int role_source=Qt::UserRole+65; // QTreeWidget only, not grid roles
using browser_internal::qt;
using browser_internal::native;
using browser_internal::engineThumbnail;
QString from8(std::string const& s) { return QString::fromUtf8(s.data(),static_cast<int>(s.size())); }
QString parentVirtual(QString const& path) {
    auto i=path.lastIndexOf('/');
    return i<0?QString{}:path.left(i);
}
bool isImage(QString const& name) {
    auto ext=QFileInfo(name).suffix().toLower();
    return QStringList{"png","jpg","jpeg","bmp","webp","gif","tga"}.contains(ext);
}
QString bytesText(std::uintmax_t count) {
    if(count>=1048576)return QString::number(double(count)/1048576.0,'f',1)+" MB";
    if(count>=1024)return QString::number(double(count)/1024.0,'f',1)+" KB";
    return QString::number(static_cast<qulonglong>(count))+" B";
}
struct Palette {QColor fill;QString text;};
Palette asset_palette(AssetKind kind) {
    switch(kind) {
    case AssetKind::folder:return {QColor("#C99A52"),"DIR"};
    case AssetKind::mesh:return {QColor("#58A7DB"),"3D"};
    case AssetKind::collision:return {QColor("#B58C59"),"COL"};
    case AssetKind::material:return {QColor("#A66AD6"),"MAT"};
    case AssetKind::texture:return {QColor("#4DBA87"),"TEX"};
    case AssetKind::audio:return {QColor("#D8A24E"),"WAV"};
    case AssetKind::scene:return {QColor("#70B7C6"),"LVL"};
    case AssetKind::prefab:return {QColor("#55C29F"),"PFB"};
    case AssetKind::character:return {QColor("#B38FD9"),"CHR"};
    case AssetKind::material_function:return {QColor("#d08fd8"),"FUNC"};
    case AssetKind::script:return {QColor("#D97893"),"JS"};
    case AssetKind::animation:return {QColor("#7DBD68"),"ANIM"};
    case AssetKind::skeleton:return {QColor("#8C9DDA"),"SKEL"};
    case AssetKind::shader:return {QColor("#6FAFD7"),"HLSL"};
    default:return {QColor("#718198"),"FILE"};
    }
}
// A read-only disk dictionary may be previewed before engine.assets is
// installed. Cache the last parsed ListFile per thumbnail worker thread,
// invalidating on file change to avoid repeated NEF8 inflate per item.
class AssetGridModel final:public QStandardItemModel {
public:
    using QStandardItemModel::QStandardItemModel;
    QStringList mimeTypes() const override {
        return {"application/x-northstar-asset-reference","text/plain","text/uri-list"};
    }
    QMimeData* mimeData(QModelIndexList const& indexes) const override {
        auto* data=new QMimeData;
        if(indexes.empty())return data;
        QStringList selected;
        for(auto const& candidate:indexes) {
            auto value=candidate.data(role_reference).toString();
            if(!value.isEmpty() && !selected.contains(value))
                selected.append(value);
        }
        if(selected.size()>1)
            data->setData("application/x-northstar-asset-references",
                          selected.join('\n').toUtf8());
        if(!selected.isEmpty())
            data->setText(selected.join('\n'));
        auto index=indexes.first();
        auto path=index.data(role_reference).toString();
        if(!path.isEmpty()) {
            data->setData("application/x-northstar-asset-reference",path.toUtf8());
            if(selected.size()<=1)data->setText(path);
        }
        return data;
    }
    Qt::DropActions supportedDragActions() const override {return Qt::CopyAction;}
};
}
QtContentBrowser::QtContentBrowser(QWidget* parent):QWidget(parent) {
    ensure_northstar_qt_icons();
    setObjectName("northstarContentBrowser");
    pool_.setMaxThreadCount(2);
    change_watcher_=new QFileSystemWatcher(this);
    change_debounce_=new QTimer(this);
    change_debounce_->setSingleShot(true);
    change_debounce_->setInterval(750);
    connect(change_debounce_,&QTimer::timeout,this,[this] {
        if(!project_root_.isEmpty())refresh();
    });
    auto changed=[this](QString const&) {
        if(!project_root_.isEmpty())change_debounce_->start();
    };
    connect(change_watcher_,&QFileSystemWatcher::directoryChanged,this,changed);
    connect(change_watcher_,&QFileSystemWatcher::fileChanged,this,changed);
    auto* root=new QVBoxLayout(this);
    root->setContentsMargins(8,8,8,8);
    root->setSpacing(6);
    auto* toolbar=new QHBoxLayout;
    toolbar->setSpacing(5);
    auto tool=[&](QString const& title,QString const& tip)->QToolButton* {
        auto* button=new QToolButton(this);
        button->setText(title);
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(31);
        toolbar->addWidget(button);
        return button;
    };
    add_=tool("+ Add","Import assets or create a folder");
    add_->setObjectName("browserAddAsset");
    import_=tool("Import","Import source files into the project");
    import_->setObjectName("browserImportAsset");
    open_content_=tool("Open Content","Select a project Content folder");
    open_content_->setObjectName("contentRootChooser");
    back_=tool("‹","Back");
    forward_=tool("›","Forward");
    up_=tool("↑","Parent folder");
    reload_=tool("⟳","Rescan project content");
    crumb_=new QLabel("Content /",this);
    crumb_->setObjectName("breadcrumb");
    crumb_->setTextFormat(Qt::RichText);
    crumb_->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    crumb_->setOpenExternalLinks(false);
    connect(crumb_,&QLabel::linkActivated,this,[this](QString const& href) {
        if(!href.startsWith("crumb:"))return;
        bool okay=false;
        int count=href.mid(6).toInt(&okay);
        if(!okay || count<0)return;
        auto path=folder_.split('/',Qt::SkipEmptyParts);
        if(count>path.size())return;
        openFolder(path.mid(0,count).join('/'));
    });
    crumb_->setMinimumWidth(120);
    crumb_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    toolbar->addWidget(crumb_,1);
    search_=new QLineEdit(this);
    search_->setPlaceholderText("Search Content...");
    search_->setClearButtonEnabled(true);
    search_->setMinimumWidth(170);
    toolbar->addWidget(search_);
    types_=new QComboBox(this);
    for(auto const& item:{"All Assets","Meshes","Materials","Textures","Audio",
                          "Scenes","Scripts","Animations","Shaders","Other","Prefabs","Characters","Material Functions","Collisions"})
        types_->addItem(item);
    types_->setMinimumWidth(130);
    toolbar->addWidget(types_);
    collections_=new QComboBox(this);
    collections_->setObjectName("assetCollections");
    collections_->setToolTip("Named asset collections. Select a collection to browse it.");
    collections_->setMinimumWidth(128);
    collections_->addItem("All Content",QString{});
    toolbar->addWidget(collections_);
    list_mode_=tool("▦","Toggle grid/list view");
    auto* viewOptions=tool("View","Show/hide Content Browser panels");
    viewOptions->setObjectName("assetViewOptions");
    root->addLayout(toolbar);

    splitter_=new QSplitter(Qt::Horizontal,this);
    folders_=new QTreeWidget(splitter_);
    folders_->setObjectName("folderTree");
    folders_->setHeaderHidden(true);
    folders_->setIndentation(18);
    folders_->setMinimumWidth(155);
    folders_->setAnimated(true);
    folders_->setUniformRowHeights(true);
    folders_->setContextMenuPolicy(Qt::CustomContextMenu);

    grid_=new QListView(splitter_);
    grid_->setObjectName("assetGrid");
    grid_->setUniformItemSizes(true);
    grid_->setViewMode(QListView::IconMode);
    grid_->setFlow(QListView::LeftToRight);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setWrapping(true);
    grid_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    grid_->setDragDropMode(QAbstractItemView::DragOnly);
    grid_->setDragEnabled(true);
    grid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    entries_=new AssetGridModel(this);
    grid_->setModel(entries_);
    auto* cards=new AssetCardDelegate(grid_);
    auto* listItems=new QStyledItemDelegate(grid_);
    grid_->setItemDelegate(cards);
    grid_->setIconSize(QSize(144,144)); // Only the classic ListMode uses icons.
    grid_->setGridSize(cards->cellSize());
    grid_->setSpacing(10);
    grid_->setMinimumWidth(260);

    // Inspector content may be longer than the dock itself. Keep its
    // scrolling internal so selection never changes the editor dock size.
    auto* detailsScroll=new QScrollArea(splitter_);
    detailsScroll->setObjectName("assetDetailsScroll");
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setFrameShape(QFrame::NoFrame);
    detailsScroll->setMinimumSize(160,0);
    detailsScroll->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Ignored);
    auto* details=new QWidget;
    details->setObjectName("assetDetails");
    details->setMinimumWidth(160);
    auto* dl=new QVBoxLayout(details);
    dl->setContentsMargins(12,15,12,10);
    dl->setSpacing(12);
    auto* cap=new QLabel("ASSET DETAILS",details);
    cap->setObjectName("detailsCaption");
    dl->addWidget(cap);
    preview_icon_=new QLabel(details);
    preview_icon_->setObjectName("assetPreviewIcon");
    preview_icon_->setAlignment(Qt::AlignCenter);
    preview_icon_->setFixedHeight(192);
    preview_icon_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    dl->addWidget(preview_icon_);
    preview_name_=new QLabel("Select an asset",details);
    preview_name_->setWordWrap(true);
    preview_name_->setMaximumHeight(50);
    preview_name_->setObjectName("previewName");
    dl->addWidget(preview_name_);
    preview_details_=new QLabel(details);
    preview_details_->setWordWrap(true);
    preview_details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    preview_details_->setObjectName("previewMetadata");
    dl->addWidget(preview_details_);
    audio_preview_=new QPushButton("Audio Preview / Play",details);
    audio_preview_->setObjectName("audioPreviewButton");
    audio_preview_->hide();
    connect(audio_preview_,&QPushButton::clicked,this,[this]{
        if(!selectedPath().isEmpty())openAssetPreview(selectedPath());
    });
    dl->addWidget(audio_preview_);
    auto* copy=new QPushButton("Copy Asset Reference",details);
    connect(copy,&QPushButton::clicked,this,[this]{copyReference();});
    dl->addWidget(copy);
    dl->addStretch(1);
    splitter_->addWidget(folders_);
    splitter_->addWidget(grid_);
    detailsScroll->setWidget(details);
    splitter_->addWidget(detailsScroll);
    splitter_->setStretchFactor(0,0);
    splitter_->setStretchFactor(1,1);
    splitter_->setStretchFactor(2,0);
    splitter_->setSizes({235,790,250});
    root->addWidget(splitter_,1);
    // UE-style view options keep source, inspector and card list independent.
    // They change visibility only, never force a new dock height.
    auto* viewMenu=new QMenu(viewOptions);
    QSettings viewState(QStringLiteral("Kayla's Systems"),
                        QStringLiteral("NorthStarCpp"));
    auto* sourcesAction=viewMenu->addAction("Show Sources Panel");
    sourcesAction->setCheckable(true);
    sourcesAction->setChecked(viewState.value(
        "ContentBrowser/View/Sources",true).toBool());
    folders_->setVisible(sourcesAction->isChecked());
    connect(sourcesAction,&QAction::toggled,this,[this](bool visible) {
        folders_->setVisible(visible);
        QSettings settings(QStringLiteral("Kayla's Systems"),
                           QStringLiteral("NorthStarCpp"));
        settings.setValue("ContentBrowser/View/Sources",visible);
    });
    auto* inspectorAction=viewMenu->addAction("Show Asset Details");
    inspectorAction->setCheckable(true);
    inspectorAction->setChecked(viewState.value(
        "ContentBrowser/View/Details",true).toBool());
    detailsScroll->setVisible(inspectorAction->isChecked());
    connect(inspectorAction,&QAction::toggled,this,[detailsScroll](bool visible) {
        detailsScroll->setVisible(visible);
        QSettings settings(QStringLiteral("Kayla's Systems"),
                           QStringLiteral("NorthStarCpp"));
        settings.setValue("ContentBrowser/View/Details",visible);
    });
    viewMenu->addSeparator();
    viewMenu->addAction("Reset Pane Widths",[this] {
        splitter_->setSizes({235,790,250});
    });
    viewOptions->setMenu(viewMenu);
    viewOptions->setPopupMode(QToolButton::InstantPopup);

    auto* bottom=new QHBoxLayout;
    status_=new QLabel("No project loaded",this);
    status_->setObjectName("assetStatus");
    bottom->addWidget(status_,1);
    sort_=new QComboBox(this);
    sort_->setObjectName("assetSortOrder");
    sort_->setToolTip("Sort assets; folders always appear first");
    sort_->addItems({"Name (A-Z)","Name (Z-A)","Type","Size (largest)","Size (smallest)"});
    sort_->setMaximumWidth(142);
    bottom->addWidget(sort_);
    {
        QSettings settings(QStringLiteral("Kayla's Systems"),
                           QStringLiteral("NorthStarCpp"));
        sort_->setCurrentIndex(std::clamp(
            settings.value("ContentBrowser/Sort",0).toInt(),0,4));
    }
    bottom->addWidget(new QLabel("Thumbnail Size",this));
    zoom_=new QSlider(Qt::Horizontal,this);
    zoom_->setRange(80,240);
    zoom_->setValue(144);
    zoom_->setMaximumWidth(130);
    bottom->addWidget(zoom_);
    root->addLayout(bottom);

    setStyleSheet(R"QSS(
QWidget#northstarContentBrowser {background:#151a22;color:#d8e1ec;}
QTreeWidget#folderTree,QListView#assetGrid,QWidget#assetDetails {
 background:#1b222e;color:#dbe5ef;border:1px solid #303b4c;border-radius:4px;
}
QTreeWidget::item {padding:5px 3px;}
QTreeWidget::item:selected,QListView::item:selected {
 background:#294b69;color:#effaff;border:1px solid #4e9acb;border-radius:4px;
}
QTreeWidget::item:hover,QListView::item:hover {background:#273343;}
QToolButton,QPushButton {
 background:#293443;color:#e1edf8;border:1px solid #43526a;
 border-radius:4px;padding:5px 9px;
}
QToolButton:hover,QPushButton:hover {background:#355570;border-color:#5998c5;}
QLineEdit,QComboBox {
 background:#101721;color:#e2e9f2;border:1px solid #405066;
 border-radius:4px;padding:6px;
}
QLineEdit:focus {border:1px solid #5aaae1;}
QLabel#breadcrumb {color:#a7bdd3;font-weight:600;}
QLabel#detailsCaption {color:#78a9d4;font-size:11px;font-weight:700;}
QLabel#previewName {color:#e9f4ff;font-size:15px;font-weight:700;}
QLabel#previewMetadata,QLabel#assetStatus {color:#97a9bd;}
QSplitter::handle {background:#2c394a;width:4px;}
QScrollBar:vertical {background:#17202a;width:10px;}
QScrollBar::handle:vertical {background:#475568;border-radius:4px;}
)QSS");

    // Editor-focused shortcuts, without changing global editor commands.
    auto bindShortcut=[this](char const* keys,auto callback) {
        auto* shortcut=new QShortcut(QKeySequence(QString::fromLatin1(keys)),this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut,&QShortcut::activated,this,callback);
    };
    bindShortcut("Ctrl+F",[this]{focusSearch();});
    bindShortcut("F5",[this]{refresh();});
    bindShortcut("F2",[this]{renameSelected();});
    bindShortcut("Ctrl+D",[this]{toggleFavoriteSelected();});
    auto* copyReferences=new QShortcut(QKeySequence::Copy,grid_);
    copyReferences->setContext(Qt::WidgetShortcut);
    connect(copyReferences,&QShortcut::activated,this,
            &QtContentBrowser::copySelectedReferences);
    bindShortcut("Alt+Left",[this]{
        if(back_->isEnabled())back_->click();
    });
    bindShortcut("Alt+Right",[this]{
        if(forward_->isEnabled())forward_->click();
    });
    connect(reload_,&QToolButton::clicked,this,&QtContentBrowser::refresh);
    connect(up_,&QToolButton::clicked,this,[this]{openFolder(parentVirtual(folder_));});
    connect(back_,&QToolButton::clicked,this,[this] {
        if(back_history_.isEmpty())return;
        forward_history_.push_back(folder_);
        changeFolder(back_history_.takeLast());
    });
    connect(forward_,&QToolButton::clicked,this,[this] {
        if(forward_history_.isEmpty())return;
        back_history_.push_back(folder_);
        changeFolder(forward_history_.takeLast());
    });
    connect(import_,&QToolButton::clicked,this,&QtContentBrowser::importFiles);
    connect(open_content_,&QToolButton::clicked,this,[this] {
        QFileDialog dialog(this,"Open Project Content Directory",root_);
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setOption(QFileDialog::DontUseNativeDialog,true);
        dialog.setOption(QFileDialog::ShowDirsOnly,true);
        if(dialog.exec()==QDialog::Accepted &&
           !dialog.selectedFiles().isEmpty())
            setContentRoot(dialog.selectedFiles().first());
    });
    auto* addMenu=new QMenu(add_);
    addMenu->addAction("New Folder",this,&QtContentBrowser::createFolder);
    addMenu->addAction("Import Files...",this,&QtContentBrowser::importFiles);
    addMenu->addSeparator();
    addMenu->addAction("New Collection...",this,&QtContentBrowser::createCollection);
    addMenu->addAction("New Team Collection...",this,&QtContentBrowser::createTeamCollection);
    add_->setMenu(addMenu);
    add_->setPopupMode(QToolButton::InstantPopup);
    connect(search_,&QLineEdit::textChanged,this,&QtContentBrowser::populateItems);
    connect(types_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[this](int){populateItems();});
    connect(collections_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[this](int index){
        active_collection_=collections_->itemData(index).toString();
        populateItems();
    });
    connect(sort_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[this](int order) {
                QSettings settings(QStringLiteral("Kayla's Systems"),
                                   QStringLiteral("NorthStarCpp"));
                settings.setValue("ContentBrowser/Sort",order);
                populateItems();
            });
    connect(zoom_,&QSlider::valueChanged,this,[this,cards](int size) {
        cards->setPreviewSize(size);
        if(grid_->viewMode()==QListView::IconMode) {
            grid_->setGridSize(cards->cellSize());
            grid_->doItemsLayout();
        }
        scheduleThumbnails();
    });
    connect(list_mode_,&QToolButton::clicked,this,[this,cards,listItems] {
        bool const list=grid_->viewMode()==QListView::IconMode;
        grid_->setViewMode(list?QListView::ListMode:QListView::IconMode);
        grid_->setFlow(list?QListView::TopToBottom:QListView::LeftToRight);
        grid_->setWrapping(!list);
        grid_->setItemDelegate(list?listItems:cards);
        grid_->setIconSize(list?QSize(28,28):QSize(zoom_->value(),zoom_->value()));
        grid_->setGridSize(list?QSize():cards->cellSize());
        grid_->doItemsLayout();
        scheduleThumbnails();
    });
    auto activateTreeFolder=[this](QTreeWidgetItem* item) {
        if(item && item->data(0,role_source).toInt()<0)return;
        if(!item)return;
        auto const folder=item->data(0,role_path).toString();
        auto const shared=item->data(0,role_source).toInt()==1;
        if(engine_selected_!=shared)
            selectAssetSource(shared);
        openFolder(folder);
    };
    connect(folders_,&QTreeWidget::itemClicked,this,
            [activateTreeFolder](QTreeWidgetItem* item,int) {
        activateTreeFolder(item);
    });
    connect(folders_,&QTreeWidget::itemDoubleClicked,this,
            [activateTreeFolder](QTreeWidgetItem* item,int) {
        activateTreeFolder(item);
    });
    connect(grid_,&QListView::doubleClicked,this,[this](QModelIndex const& index) {
        auto ref=itemPath(index);
        // NEF8 YMAT is a library, never the first material implicitly.
        // Double-click the library to browse every material record; double-
        // click an entry to open the Material Editor for that exact name.
        if(index.data(role_directory).toBool())openFolder(ref);
        else if(!ref.isEmpty())openAssetPreview(ref);
    });
    connect(grid_->selectionModel(),&QItemSelectionModel::currentChanged,
            this,[this](QModelIndex const&,QModelIndex const&) {
        updateDetails();
        emit assetSelected(selectedAsset());
    });
    connect(grid_,&QListView::customContextMenuRequested,
            this,&QtContentBrowser::showMenu);
    connect(folders_,&QTreeWidget::customContextMenuRequested,
            this,[this](QPoint){showMenu(QPoint());});
    connect(grid_->verticalScrollBar(),&QScrollBar::valueChanged,
            this,[this](int){scheduleThumbnails();});
    updateDetails();
    updateBreadcrumb();
}
QtContentBrowser::~QtContentBrowser() {
    ++generation_;
    pool_.waitForDone();
}
void QtContentBrowser::setEditorDockMode(bool enabled) {
    editor_dock_mode_=enabled;
    // In the unified editor the project manager / scene owns the root.
    // Never offer a second arbitrary directory picker in the asset panel.
    open_content_->setVisible(!enabled);
    open_content_->setEnabled(!enabled);
}
void QtContentBrowser::focusSearch() {
    search_->setFocus(Qt::ShortcutFocusReason);
    search_->selectAll();
}
void QtContentBrowser::setSharedContentRoot(QString const& path) {
    QString const normalized=path.trimmed().isEmpty()?QString{}:
        QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if(normalized==engine_root_)return;
    engine_root_=normalized;
    if(engine_selected_) {
        active_collection_.clear();
        refreshCollections();
    }
    if(engine_selected_) {
        root_=engine_root_;
        folder_.clear();
        back_history_.clear();
        forward_history_.clear();
    }
    refresh();
    emit contentRootsChanged(project_root_,engine_root_);
}
// The shared root can change independently of the active project.
void QtContentBrowser::selectAssetSource(bool shared) {
    if(shared==engine_selected_)return;
    engine_selected_=shared;
    active_collection_.clear();
    root_=engine_selected_?engine_root_:project_root_;
    refreshCollections();
    folder_.clear();
    back_history_.clear();
    forward_history_.clear();
    ++thumbnail_generation_;
    thumbs_.clear();
    pending_.clear();
    grid_->clearSelection();
    // Do not rebuild QTreeWidget while processing itemClicked: Qt can
    // still refer to the clicked item during the same mouse event.
    // Both tree roots are persistent and are built by refresh().
    if(auto* current=folders_->currentItem();
       !current || current->data(0,role_source).toInt()!=(shared?1:0)) {
        QSignalBlocker block(folders_);
        folders_->setCurrentItem(folders_->topLevelItem(shared?1:0));
    }
    populateItems();
    // Disable all project-mutating toolbar actions on the engine mount.
    add_->setEnabled(!engine_selected_ && !root_.isEmpty());
    import_->setEnabled(!engine_selected_ && !root_.isEmpty());
}
void QtContentBrowser::setProjectScene(QString const& scene) {
    if(scene.trimmed().isEmpty()) {
        setContentRoot({});
        return;
    }
    auto const file=QFileInfo(scene);
    setContentRoot(file.absoluteDir().filePath("assets"));
}
void QtContentBrowser::setContentRoot(QString const& path) {
    // QFileInfo("").absoluteFilePath() resolves to the process working
    // directory: never index the entire repository for an unsaved scene.
    QString const absolute=path.trimmed().isEmpty()?QString{}:
        QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if(absolute==project_root_)return;
    project_root_=absolute;
    active_collection_.clear();
    refreshCollections();
    engine_selected_=false;
    root_=project_root_;
    refreshCollections();
    folder_.clear();
    pending_reveal_.clear();
    back_history_.clear();forward_history_.clear();
    thumbs_.clear();icons_.clear();pending_.clear();
    project_catalog_=AssetCatalog{};
    catalog_ready_=false;
    refresh();
    emit contentRootsChanged(project_root_,engine_root_);
}
void QtContentBrowser::revealAsset(QString const& virtualReference) {
    if((project_root_.isEmpty() && engine_root_.isEmpty()) ||
       virtualReference.trimmed().isEmpty())return;
    pending_reveal_=virtualReference;
    completeReveal();
}
void QtContentBrowser::completeReveal() {
    if(!catalog_ready_ || pending_reveal_.isEmpty())return;
    auto const desired=pending_reveal_;
    auto const locate=[](AssetCatalog const& catalog,QString const& path) {
        auto const* record=catalog.find(path.toUtf8().toStdString());
        if(!record)
            for(auto const& entry:catalog.entries())
                if(from8(entry.asset_reference())==path)return &entry;
        return record;
    };
    if(!locate(activeCatalog(),desired)) {
        if(locate(engine_selected_?project_catalog_:engine_catalog_,desired))
            selectAssetSource(!engine_selected_);
    }
    auto const* record=activeCatalog().find(desired.toUtf8().toStdString());
    if(!record) {
        for(auto const& entry:activeCatalog().entries())
            if(QString::fromUtf8(entry.asset_reference().c_str())==desired) {
                record=&entry;
                break;
            }
    }
    pending_reveal_.clear();
    if(!record) {
        status_->setText("Asset not indexed in the active project: "+desired);
        return;
    }
    auto const virtualPath=QString::fromUtf8(record->virtual_path.c_str());
    auto const destination=record->directory?
        virtualPath:parentVirtual(virtualPath);
    if(!search_->text().isEmpty())search_->clear();
    if(types_->currentIndex()!=0)types_->setCurrentIndex(0);
    openFolder(destination);
    for(int row=0;row<entries_->rowCount();++row) {
        auto const index=entries_->index(row,0);
        if(index.data(role_reference).toString()==desired ||
           index.data(role_path).toString()==virtualPath) {
            grid_->setCurrentIndex(index);
            grid_->scrollTo(index,QAbstractItemView::PositionAtCenter);
            break;
        }
    }
}
void QtContentBrowser::refresh() {
    auto const version=++generation_;
    ++registry_generation_;
    registry_.reset();
    git_decorations_.clear();
    ++thumbnail_generation_;
    catalog_ready_=false;
    project_catalog_=AssetCatalog{};
    engine_catalog_=AssetCatalog{};
    pending_.clear();
    thumbs_.clear();
    visible_items_.clear();
    entries_->clear();
    populateTree(); // Both root labels remain visible while indexing.
    bool const hasProject=!project_root_.isEmpty() &&
        QFileInfo(project_root_).isDir();
    bool const hasEngine=!engine_root_.isEmpty() &&
        QFileInfo(engine_root_).isDir();
    add_->setEnabled(!engine_selected_ && hasProject);
    import_->setEnabled(!engine_selected_ && hasProject);
    if(!hasProject && !hasEngine) {
        status_->setText("Open a project or connect Engine Shared Assets.");
        updateBreadcrumb();
        updateDetails();
        return;
    }
    status_->setText("Indexing Project and Engine Shared Assets...");
    auto const project=hasProject?project_root_:QString{};
    auto const engine=hasEngine?engine_root_:QString{};
    using Sources=std::pair<AssetCatalog,AssetCatalog>;
    auto* watcher=new QFutureWatcher<Sources>(this);
    connect(watcher,&QFutureWatcher<Sources>::finished,this,
            [this,watcher,version] {
        try {
            auto result=watcher->result();
            if(version==generation_) {
                project_catalog_=std::move(result.first);
                engine_catalog_=std::move(result.second);
                catalog_ready_=true;
                configureFileWatching();
                scheduleDependencyIndex();
                refreshGitDecorations();
                refreshCollections();
                populateTree();
                populateItems();
                completeReveal();
            }
        }catch(std::exception const& e) {
            if(version==generation_)
                status_->setText(QString::fromUtf8(e.what()));
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&pool_,[project,engine] {
        Sources result;
        if(!project.isEmpty())
            result.first.scan(native(project),100000);
        if(!engine.isEmpty())
            result.second.scan(native(engine),100000,true);
        return result;
    }));
}
int QtContentBrowser::recordCount() const {
    return static_cast<int>(activeCatalog().entries().size());
}
int QtContentBrowser::visibleCount() const {return entries_->rowCount();}
QImage QtContentBrowser::cachedThumbnail(QString const& path) const {
    return thumbs_.value(path);
}
void QtContentBrowser::setSearchText(QString const& s){search_->setText(s);}
void QtContentBrowser::setTypeFilter(QString const& kind) {
    int index=types_->findText(kind,Qt::MatchFixedString);
    if(index>=0)types_->setCurrentIndex(index);
}
QString QtContentBrowser::itemPath(QModelIndex const& index) const {
    return index.isValid()?index.data(role_path).toString():QString{};
}
QString QtContentBrowser::selectedPath() const {return itemPath(grid_->currentIndex());}
QString QtContentBrowser::selectedAsset() const {
    auto index=grid_->currentIndex();
    return index.isValid()&&!index.data(role_directory).toBool()?
        index.data(role_reference).toString():QString{};
}
bool QtContentBrowser::selectedIsDirectory() const {
    return grid_->currentIndex().data(role_directory).toBool();
}
void QtContentBrowser::populateTree() {
    folders_->blockSignals(true);
    folders_->clear();
    QTreeWidgetItem* selected=nullptr;
    for(int source=0;source!=2;++source) {
        auto const shared=source==1;
        auto const& catalog=shared?engine_catalog_:project_catalog_;
        auto const path=shared?engine_root_:project_root_;
        auto* root=new QTreeWidgetItem(folders_,{
            shared?"Engine Shared Assets":"Project Assets"});
        root->setData(0,role_path,QString{});
        root->setData(0,role_source,source);
        root->setIcon(0,iconFor(AssetKind::folder));
        root->setToolTip(0,path.isEmpty()?
            (shared?"Engine shared content unavailable":"Open a project"):
            path+(shared?"\nRead-only engine resources":"\nEditable project resources"));
        if(shared==engine_selected_)selected=root;
        QHash<QString,QTreeWidgetItem*> lookup;
        lookup.insert(QString{},root);
        std::vector<QString> paths;
        paths.reserve(catalog.entries().size());
        for(auto const& rec:catalog.entries())
            if(rec.directory)paths.push_back(from8(rec.virtual_path));
        std::sort(paths.begin(),paths.end(),
                  [](QString const& a,QString const& b) {
            auto ad=a.count('/'),bd=b.count('/');
            return ad!=bd?ad<bd:a.compare(b,Qt::CaseInsensitive)<0;
        });
        for(auto const& folder:paths) {
            auto* owner=lookup.value(parentVirtual(folder),root);
            auto* item=new QTreeWidgetItem(owner,{folder.section('/',-1)});
            item->setData(0,role_path,folder);
            item->setData(0,role_source,source);
            auto const* record=catalog.find(folder.toUtf8().toStdString());
            item->setIcon(0,record?iconForDirectory(*record):
                                     iconFor(AssetKind::folder));
            lookup.insert(folder,item);
        }
        folders_->expandItem(root);
        if(shared==engine_selected_) {
            if(!lookup.contains(folder_))folder_.clear();
            selected=lookup.value(folder_,root);
        }
    }
    // Favorite links preserve the original source/read-only contract.
    auto* favorites=new QTreeWidgetItem(folders_,{"Favorites"});
    favorites->setData(0,role_source,-1);
    favorites->setIcon(0,iconFor(AssetKind::folder));
    for(int source=0;source<2;++source) {
        bool const shared=source==1;
        auto const& catalog=shared?engine_catalog_:project_catalog_;
        QString const home=shared?engine_root_:project_root_;
        for(QString const& path:browser_favorites::load(home)) {
            auto const* rec=catalog.find(path.toUtf8().toStdString());
            if(!rec || !rec->directory)continue;
            auto* item=new QTreeWidgetItem(favorites,{
                (shared?"Engine: ":"Project: ")+path.section('/',-1)});
            item->setData(0,role_path,path);
            item->setData(0,role_source,source);
            item->setIcon(0,iconForDirectory(*rec));
            item->setToolTip(0,path);
        }
    }
    folders_->expandItem(favorites);
    folders_->setCurrentItem(selected);
    folders_->blockSignals(false);
    back_->setEnabled(!back_history_.isEmpty());
    forward_->setEnabled(!forward_history_.isEmpty());
    updateBreadcrumb();
}
void QtContentBrowser::openFolder(QString const& folder,bool history) {
    if(!active_collection_.isEmpty()) {
        active_collection_.clear();
        QSignalBlocker blocker(collections_);
        collections_->setCurrentIndex(0);
    }
    if(folder==folder_)return;
    if(!folder.isEmpty()) {
        auto const* record=activeCatalog().find(folder.toUtf8().toStdString());
        if(!record || !record->directory)return;
        if(record->embedded_engine_api) {
            if(history){back_history_.push_back(folder_);forward_history_.clear();}
            changeFolder(folder);
            return;
        }
        try {
            auto const backing=activeCatalog().backing_file(record->virtual_path);
            if(record->is_list_file()?
                    !fs::is_regular_file(backing):!fs::is_directory(backing))
                return;
        } catch(std::exception const&) {return;}
    }
    if(history){back_history_.push_back(folder_);forward_history_.clear();}
    changeFolder(folder);
}
void QtContentBrowser::browseTo(QString const& virtualFolder) {
    openFolder(virtualFolder);
}
void QtContentBrowser::openAssetPreview(QString const& virtualPath) {
    if(root_.isEmpty() || virtualPath.isEmpty())return;
    auto const* record=activeCatalog().find(virtualPath.toUtf8().toStdString());
    if(!record)return;
    if(record->directory) {
        openFolder(virtualPath);
        return;
    }
    if(record->embedded_engine_api) {
        auto const* embedded=embedded_api::find(record->virtual_path);
        if(!embedded)return;
        QDialog dialog(this);
        dialog.setWindowTitle(QString::fromUtf8(record->filename.c_str())+
                              " — Engine API (read-only)");
        dialog.resize(920,620);
        QVBoxLayout layout(&dialog);
        auto* source=text_assets::createSourceEditor(&dialog,
            text_assets::formatFor(QString::fromUtf8(record->filename.c_str())),true);
        source->setObjectName("northstarEmbeddedApiSource");
        source->setReadOnly(true);
        source->setLineWrapMode(QPlainTextEdit::NoWrap);
        source->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        source->setPlainText(QString::fromUtf8(embedded->source.data(),
            static_cast<qsizetype>(embedded->source.size())));
        layout.addWidget(source);
        dialog.exec();
        return;
    }
    if(record->kind==AssetKind::audio) {
        try {
            openAudioPreviewModal(this,activeCatalog().backing_file(record->virtual_path),
                                  record->asset_reference());
        } catch(std::exception const& e) {
            QMessageBox::warning(this,"Audio Preview",QString::fromUtf8(e.what()));
        }
        return;
    }
    // Shared materials remain read-only in the Content Browser.
    // Project materials may open their authoring editor.
    if(record->kind==AssetKind::material && !engine_selected_) {
        auto const reference=QString::fromUtf8(record->asset_reference().c_str());
        if(receivers(SIGNAL(materialEditorRequested(QString)))>0) {
            emit materialEditorRequested(reference);
            return;
        }
    }
    // Text assets are first-class authoring documents, not binary preview tiles.
    // Shared engine resources remain inspectable but never writable here.
    if(!record->is_virtual_entry() &&
       text_assets::formatFor(from8(record->virtual_path))!=
           text_assets::Format::Unsupported) {
        try {
            auto const physical=activeCatalog().backing_file(record->virtual_path);
            auto const absolute=from8(physical.u8string());
            if(editor_dock_mode_ && receivers(SIGNAL(textEditorRequested(QString,bool)))>0)
                emit textEditorRequested(absolute,engine_selected_);
            else text_assets::open(this,absolute,engine_selected_);
        } catch(std::exception const& e) {
            QMessageBox::warning(this,"Text Asset",QString::fromUtf8(e.what()));
        }
        return;
    }
    openAssetPreviewModal(this,native(root_),record->asset_reference(),record->kind);
}
void QtContentBrowser::changeFolder(QString const& folder) {
    folder_=folder;
    for(auto* root:{folders_->topLevelItem(0),folders_->topLevelItem(1)}) {
        if(!root || root->data(0,role_source).toInt()!=(engine_selected_?1:0))
            continue;
        QTreeWidgetItem* target=root;
        QList<QTreeWidgetItem*> todo{root};
        while(!todo.isEmpty()) {
            auto* item=todo.takeLast();
            if(item->data(0,role_path).toString()==folder) {
                target=item;break;
            }
            for(int i=0;i<item->childCount();++i)
                todo.push_back(item->child(i));
        }
        QSignalBlocker block(folders_);
        folders_->setCurrentItem(target);
        break;
    }
    back_->setEnabled(!back_history_.isEmpty());
    forward_->setEnabled(!forward_history_.isEmpty());
    up_->setEnabled(!folder_.isEmpty());
    populateItems();
    updateBreadcrumb();
}
void QtContentBrowser::updateBreadcrumb() {
    QString const source=engine_selected_?
        "Engine Shared Assets":"Project Assets";
    QString html="<a href='crumb:0' style='color:#a7bdd3;text-decoration:none'>"+
        source.toHtmlEscaped()+"</a>";
    auto const parts=folder_.split('/',Qt::SkipEmptyParts);
    for(int i=0;i<parts.size();++i)
        html+=" / <a href='crumb:"+QString::number(i+1)+
              "' style='color:#c4dded;text-decoration:none'>"+
              parts[i].toHtmlEscaped()+"</a>";
    crumb_->setText(html);
    crumb_->setToolTip(root_+"/"+folder_);
}
QIcon QtContentBrowser::iconFor(AssetKind k) const {
    if(k==AssetKind::folder) {
        // Ordinary directories AND NEF8 virtual ListFile dictionaries
        // share the exact project artwork, rather than the old DIR tile.
        static QIcon const folderArt(
            QStringLiteral(":/northstar/icons/assetBrowser/folder.png"));
        if(!folderArt.isNull())return folderArt;
    }
    // Dedicated NorthStar file art, embedded in RCC for both the editor
    // dock and standalone Asset Browser. Avoid FILE/FUNC placeholder tiles.
    if(k==AssetKind::collision) {
        static QIcon const collisionFileArt(
            QStringLiteral(":/northstar/icons/assetBrowser/collisionFile.png"));
        if(!collisionFileArt.isNull())return collisionFileArt;
    }
    if(k==AssetKind::audio) {
        static QIcon const soundFileArt(
            QStringLiteral(":/northstar/icons/assetBrowser/soundFile.png"));
        if(!soundFileArt.isNull())return soundFileArt;
    }
    if(k==AssetKind::material_function) {
        static QIcon const functionArt(
            QStringLiteral(":/northstar/icons/assetBrowser/materialFunc.png"));
        if(!functionArt.isNull())return functionArt;
    } else if(k==AssetKind::other) {
        static QIcon const otherArt(
            QStringLiteral(":/northstar/icons/assetBrowser/otherFile.png"));
        if(!otherArt.isNull())return otherArt;
    }
    auto data=asset_palette(k);
    QPixmap image(120,120);image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing,true);
    QRectF box(14,15,92,90);
    painter.setPen(QPen(data.fill.lighter(140),2));
    painter.setBrush(data.fill.darker(300));
    painter.drawRoundedRect(box,12,12);
    painter.setPen(data.fill.lighter(165));
    QFont font=painter.font();font.setBold(true);
    font.setPixelSize(data.text.size()>3?20:31);
    painter.setFont(font);
    painter.drawText(box,Qt::AlignCenter,data.text);
    painter.end();
    return QIcon(image);
}
QIcon QtContentBrowser::iconForDirectory(AssetRecord const& record) const {
    // Only NEF8 ListFile dictionaries use the specialized folder artwork.
    // Physical directories always use the original folder.png, regardless
    // of their display names (e.g. a physical folder called "textures").
    // Native collision folders use their dedicated artwork. For future
    // NEF8/YBN virtual dictionaries the same icon identifies the container.
    if(record.directory) {
        auto const path=fs::path(record.virtual_path);
        auto const basename=path.filename().u8string();
        auto const extension=path.extension().u8string();
        if(extension==".ybn" || basename=="collisions" || basename=="collision") {
            static QIcon const collisionFolder(
                QStringLiteral(":/northstar/icons/assetBrowser/collisionDir.png"));
            if(!collisionFolder.isNull())return collisionFolder;
        }
    }
    if(record.directory && record.is_list_file()) {
        auto const extension=fs::path(record.virtual_path).extension().u8string();
        if(extension==".ytd") {
            static QIcon const textureFolder(
                QStringLiteral(":/northstar/icons/assetBrowser/texturesFolder.png"));
            if(!textureFolder.isNull())return textureFolder;
        } else if((extension==".ydd" || extension==".ydr")) {
            static QIcon const modelFolder(
                QStringLiteral(":/northstar/icons/assetBrowser/modelsFolder.png"));
            if(!modelFolder.isNull())return modelFolder;
        } else if(extension==".ymat") {
            static QIcon const materialFolder(
                QStringLiteral(":/northstar/icons/assetBrowser/materialsFolder.png"));
            if(!materialFolder.isNull())return materialFolder;
        } else if(extension==".ymt") {
            static QIcon const skeletonFolder(
                QStringLiteral(":/northstar/icons/assetBrowser/skeletalFolder.png"));
            if(!skeletonFolder.isNull())return skeletonFolder;
        } else if(extension==".nac") {
            static QIcon const soundFolder(
                QStringLiteral(":/northstar/icons/assetBrowser/soundsFolder.png"));
            if(!soundFolder.isNull())return soundFolder;
        } else if(extension==".ycd") {
            static QIcon const animationFolder(
                QStringLiteral(":/northstar/icons/assetBrowser/animFolder.png"));
            if(!animationFolder.isNull())return animationFolder;
        }
    }
    return iconFor(AssetKind::folder);
}
QIcon QtContentBrowser::iconForRecord(AssetRecord const& record) const {
    if(record.directory)return iconForDirectory(record);
    if(!record.is_virtual_entry()) {
        auto const extension=QFileInfo(from8(record.virtual_path)).suffix().toLower();
        // Cache the decoded resource icon across all cards and refreshes.
        if(extension=="md" || extension=="markdown") {
            static QIcon const md(QStringLiteral(":/northstar/icons/assetBrowser/fileMd.png"));
            if(!md.isNull())return md;
        } else if(extension=="js") {
            static QIcon const js(QStringLiteral(":/northstar/icons/assetBrowser/fileJs.png"));
            if(!js.isNull())return js;
        } else if(extension=="vert") {
            static QIcon const vert(QStringLiteral(":/northstar/icons/assetBrowser/vertShader.png"));
            if(!vert.isNull())return vert;
        } else if(extension=="frag") {
            static QIcon const frag(QStringLiteral(":/northstar/icons/assetBrowser/fragShader.png"));
            if(!frag.isNull())return frag;
        }
    }
    if(record.is_virtual_entry()) {
        // The virtual child is an individual skeleton or animation record:
        // container_path is the physical NEF8 dictionary; entry_name is
        // not a file extension and must not determine the artwork.
        auto const container=from8(record.container_path);
        if(record.kind==AssetKind::skeleton &&
           container.endsWith(".ymt",Qt::CaseInsensitive)) {
            static QIcon const skeletonFile(
                QStringLiteral(":/northstar/icons/assetBrowser/skelFile.png"));
            if(!skeletonFile.isNull())return skeletonFile;
        }
        if(record.kind==AssetKind::animation &&
           container.endsWith(".ycd",Qt::CaseInsensitive)) {
            static QIcon const animationFile(
                QStringLiteral(":/northstar/icons/assetBrowser/animFile.png"));
            if(!animationFile.isNull())return animationFile;
        }
    }
    return iconFor(record.kind);
}
QString QtContentBrowser::subtitleText(AssetRecord const& rec) const {
    if(!rec.directory && !rec.is_virtual_entry()) {
        auto const suffix=QFileInfo(from8(rec.virtual_path)).suffix().toLower();
        if(suffix=="md" || suffix=="markdown")return "Markdown Document";
        if(suffix=="js")return "JavaScript Source";
        if(suffix=="vert")return "GLSL Vertex Shader";
        if(suffix=="frag")return "GLSL Fragment Shader";
    }
    QString const type=QString::fromLatin1(kind_name(rec.kind));
    if(rec.kind==AssetKind::prefab && !rec.directory) {
        if(!rec.prefab_valid)return "Prefab - Invalid Scene V3";
        return QString("Prefab - %1 actor%2")
            .arg(rec.prefab_actor_count)
            .arg(rec.prefab_actor_count==1?"":"s");
    }
    if(rec.directory) {
        if(!rec.is_list_file())return "Folder";
        auto const extension=fs::path(rec.virtual_path).extension().u8string();
        auto const number=rec.virtual_child_count;
        auto const singular=extension==".ytd"?"texture":
                            (extension==".ydd" || extension==".ydr")?"model":
                            extension==".ymat"?"material":
                            extension==".ymt"?"joint":
                            extension==".ycd"?"animation clip":
                            extension==".nac"?"audio track":"item";
        return QString::number(number)+" "+singular+
               (number==1?"":"s");
    }
    // The ListFile's compressed file size is NOT the size of each record.
    // Never show a fabricated 0 B for an unknown embedded mesh/material.
    if(rec.is_virtual_entry() && rec.size==0)
        return type+" · embedded";
    return type+" · "+bytesText(rec.size);
}
QColor QtContentBrowser::accentColor(AssetRecord const& rec) const {
    if(rec.directory && rec.is_list_file()) {
        auto const ext=fs::path(rec.virtual_path).extension().u8string();
        if(ext==".ytd")return asset_palette(AssetKind::texture).fill;
        if(ext==".ydd" || ext==".ydr")return asset_palette(AssetKind::mesh).fill;
        if(ext==".ymat")return asset_palette(AssetKind::material).fill;
        if(ext==".ymt")return asset_palette(AssetKind::skeleton).fill;
        if(ext==".ycd")return asset_palette(AssetKind::animation).fill;
        if(ext==".nac")return asset_palette(AssetKind::audio).fill;
    }
    return asset_palette(rec.directory?AssetKind::folder:rec.kind).fill;
}
void QtContentBrowser::populateItems() {
    visible_items_.clear();
    entries_->clear();
    QString const text=search_->text().trimmed();
    AssetQuery const query(text);
    search_->setToolTip(query.valid()?
        "AND / OR / NOT, parentheses, quoted terms, name:, path:, type:, ext:, is:, size>10MB":
        query.error());
    QStringList collectionList;
    if(active_collection_.startsWith("@team/")) {
        asset_collections::Items team;
        QString issue;
        if(asset_team_collections::load(root_,team,issue))
            collectionList=team.value(active_collection_.mid(6));
    }else if(!active_collection_.isEmpty())
        collectionList=asset_collections::load(root_).value(active_collection_);
    QSet<QString> collectionSet;
    for(QString const& path:collectionList)collectionSet.insert(path);
    auto choice=types_->currentIndex();
    std::vector<AssetRecord const*> matches;
    matches.reserve(activeCatalog().entries().size());
    for(auto const& rec:activeCatalog().entries()) {
        auto path=from8(rec.virtual_path);
        if(!active_collection_.isEmpty() &&
           !collectionSet.contains(path))continue;
        if(!text.isEmpty()) {
            bool const matchesQuery=query.matches(rec);
            if(!matchesQuery)continue;
        } else if(active_collection_.isEmpty() &&
                  parentVirtual(path)!=folder_)continue;
        if(choice>0 && !rec.directory) {
            AssetKind desired=AssetKind::other;
            switch(choice) {
            case 1:desired=AssetKind::mesh;break;
            case 2:desired=AssetKind::material;break;
            case 3:desired=AssetKind::texture;break;
            case 4:desired=AssetKind::audio;break;
            case 5:desired=AssetKind::scene;break;
            case 6:desired=AssetKind::script;break;
            case 7:desired=AssetKind::animation;break;
            case 8:desired=AssetKind::shader;break;
            case 9:desired=AssetKind::other;break;
            case 10:desired=AssetKind::prefab;break;
            case 11:desired=AssetKind::character;break;
            case 12:desired=AssetKind::material_function;break;
            case 13:desired=AssetKind::collision;break;
            default:break;
            }
            if(rec.kind!=desired)continue;
        }
        matches.push_back(&rec);
    }
    // Sort only visible entries; catalog indices and asset references remain stable.
    QCollator natural;
    natural.setCaseSensitivity(Qt::CaseInsensitive);
    natural.setNumericMode(true);
    std::stable_sort(matches.begin(),matches.end(),
        [this,&natural](AssetRecord const* a,AssetRecord const* b) {
            if(a->directory!=b->directory)return a->directory;
            auto compare=[&](AssetRecord const* x,AssetRecord const* y) {
                int result=natural.compare(from8(x->filename),from8(y->filename));
                if(result!=0)return result<0;
                return x->virtual_path<y->virtual_path;
            };
            switch(sort_->currentIndex()) {
                case 1:return compare(b,a);
                case 2:
                    if(a->kind!=b->kind)
                        return QString::compare(kind_name(a->kind),kind_name(b->kind),
                                                Qt::CaseInsensitive)<0;
                    break;
                case 3:if(a->size!=b->size)return a->size>b->size;break;
                case 4:if(a->size!=b->size)return a->size<b->size;break;
                default:break;
            }
            return compare(a,b);
        });
    for(auto const* match:matches) {
        auto const& rec=*match;
        auto path=from8(rec.virtual_path);
        auto const icon=iconForRecord(rec);
        auto* item=new QStandardItem(icon,from8(rec.filename));
        item->setEditable(false);
        item->setData(path,role_path);
        item->setData(from8(rec.asset_reference()),role_reference);
        item->setData(rec.directory,role_directory);
        item->setData(static_cast<int>(rec.kind),role_kind);
        item->setData(from8(rec.filename),asset_card_roles::title);
        const auto subtitle=subtitleText(rec);
        item->setData(subtitle,asset_card_roles::subtitle);
        item->setData(accentColor(rec),asset_card_roles::accent_color);
        if(!engine_selected_)
            item->setData(git_decorations_.value(path),asset_card_roles::git_status);
        if(thumbs_.contains(path))
            item->setData(thumbs_.value(path),asset_card_roles::preview_image);
        item->setToolTip(path+"\n"+subtitle+
                         "\n"+from8(rec.asset_reference()));
        entries_->appendRow(item);
        visible_items_.insert(path,item);
    }
    QString status=QString::number(entries_->rowCount())+" items  |  "+
                   QString::number(activeCatalog().entries().size())+" indexed";
    if(activeCatalog().truncated())status+="  |  index limit reached";
    status_->setText(status);
    updateBreadcrumb();
    updateDetails();
    scheduleThumbnails();
}
void QtContentBrowser::scheduleThumbnails() {
    if(entries_->rowCount()==0 || grid_->viewMode()==QListView::ListMode)return;
    auto first=grid_->indexAt(QPoint(5,6)).row();
    if(first<0)first=0;
    int columns=std::max(1,grid_->viewport()->width()/
                          std::max(1,grid_->gridSize().width()));
    int rows=std::max(1,grid_->viewport()->height()/
                       std::max(1,grid_->gridSize().height())+2);
    int end=std::min(entries_->rowCount(),first+columns*rows+columns*2);
    for(int i=first;i<end;++i) {
        auto* item=entries_->item(i);
        if(!item || item->data(role_directory).toBool())continue;
        auto path=item->data(role_path).toString();
        if(thumbs_.contains(path)) {
            item->setData(thumbs_.value(path),asset_card_roles::preview_image);
            continue;
        }
        if(!pending_.contains(path))queueThumbnail(path);
    }
}
void QtContentBrowser::queueThumbnail(QString const& path) {
    auto const* rec=activeCatalog().find(path.toUtf8().toStdString());
    if(!rec)return;
    const auto k=rec->kind;
    if(!isImage(path) && k!=AssetKind::texture &&
       k!=AssetKind::mesh && k!=AssetKind::material &&
       k!=AssetKind::prefab)return;
    auto const version=thumbnail_generation_;
    auto const base=root_;
    auto const physical=from8(rec->container_path.empty()?
                              rec->virtual_path:rec->container_path);
    auto const reference=from8(rec->asset_reference());
    auto const embedded=rec->is_virtual_entry();
    pending_.insert(path);
    auto* watcher=new QFutureWatcher<QImage>(this);
    connect(watcher,&QFutureWatcher<QImage>::finished,this,
            [this,watcher,path,version] {
        QImage result;
        try {result=watcher->result();}
        catch(std::exception const&){}
        if(version==thumbnail_generation_) {
            pending_.remove(path);
            if(!result.isNull()) {
                thumbs_.insert(path,result);
                if(thumbs_.size()>480)thumbs_.erase(thumbs_.begin());
                if(auto* item=visible_items_.value(path,nullptr)) {
                    // Do not overwrite the category/folder fallback icon:
                    // card previews use a dedicated role and safe-zone.
                    item->setData(result,asset_card_roles::preview_image);
                }
                if(selectedPath()==path)updateDetails();
            }
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&pool_,[base,physical,reference,embedded,k] {
        try {
            auto const full=QDir(base).filePath(physical);
            auto const metadata=QFileInfo(full);
            if(k!=AssetKind::prefab &&
               (!metadata.isFile() || metadata.size()>128ll*1024*1024))
                return QImage{};
            if(!embedded && isImage(physical)) {
                QImageReader reader(full);
                reader.setAutoTransform(true);
                if(reader.canRead()) {
                    auto size=reader.size();
                    if(size.isValid())reader.setScaledSize(
                        size.scaled(QSize(320,320),Qt::KeepAspectRatio));
                    auto image=reader.read();
                    if(!image.isNull())return image;
                }
            }
            return engineThumbnail(native(base),reference.toUtf8().toStdString(),k);
        } catch(std::exception const&) {return QImage{};}
    }));
}
void QtContentBrowser::updateDetails() {
    auto selected=selectedPath();
    if(selected.isEmpty()) {
        preview_name_->setText("Select an asset");
        preview_icon_->clear();preview_details_->setText({});
        audio_preview_->hide();
        return;
    }
    auto index=grid_->currentIndex();
    auto kind=static_cast<AssetKind>(index.data(role_kind).toInt());
    audio_preview_->setVisible(kind==AssetKind::audio);
    auto filename=selected.section('/',-1);
    if(auto const* selectedRecord=activeCatalog().find(
            selected.toUtf8().toStdString());
       selectedRecord && selectedRecord->is_virtual_entry())
        filename=from8(selectedRecord->filename);
    preview_name_->setText(filename);
    preview_name_->setToolTip(filename);
    if(thumbs_.contains(selected)) {
        preview_icon_->setPixmap(QPixmap::fromImage(thumbs_[selected]).scaled(
            190,190,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    } else {
        auto const* selectedRecord=activeCatalog().find(selected.toUtf8().toStdString());
        auto const icon=selectedRecord?
            iconForRecord(*selectedRecord):iconFor(kind);
        preview_icon_->setPixmap(icon.pixmap(130,130));
    }
    QString info=QString::fromLatin1(kind_name(kind))+"\nContent / "+selected;
    auto const* rec=activeCatalog().find(selected.toUtf8().toStdString());
    if(rec && kind==AssetKind::prefab) {
        try {
            info+="\n"+prefab_preview::details(
                prefab_preview::load(native(root_),selected.toUtf8().toStdString()));
        } catch(std::exception const& e) {
            info+="\nInvalid prefab: "+QString::fromUtf8(e.what());
        }
    }
    if(rec && kind==AssetKind::material_function) {
        try {
            QFile function(qt(activeCatalog().backing_file(rec->virtual_path)));
            if(!function.open(QIODevice::ReadOnly) ||
               function.size()>32768)
                throw std::runtime_error("Cannot read bounded Material Function");
            QJsonParseError parse{};
            auto doc=QJsonDocument::fromJson(function.readAll(),&parse);
            if(parse.error!=QJsonParseError::NoError || !doc.isObject() ||
               doc.object().value("schema").toString()!=
                   "northstar.material-function.v1")
                throw std::runtime_error("Invalid Material Function schema");
            auto const object=doc.object();
            info+="\nReusable Material Function";
            info+="\nInputs:";
            for(auto const& raw:object.value("inputs").toArray()) {
                auto pin=raw.toObject();
                info+="\n  "+pin.value("name").toString()+" : "+
                      pin.value("type").toString();
            }
            info+="\nOutputs:";
            for(auto const& raw:object.value("outputs").toArray()) {
                auto pin=raw.toObject();
                info+="\n  "+pin.value("name").toString()+" : "+
                      pin.value("type").toString();
            }
            info+="\nNodes: "+QString::number(
                object.value("nodes").toArray().size());
        }catch(std::exception const& e) {
            info+="\nInvalid Material Function: "+QString::fromUtf8(e.what());
        }
    }
    if(rec && kind==AssetKind::character) {
        try {
            QFile manifest(qt(activeCatalog().backing_file(rec->virtual_path)));
            if(!manifest.open(QIODevice::ReadOnly) ||
               manifest.size()<2 || manifest.size()>32768)
                throw std::runtime_error("Cannot read bounded character manifest");
            QJsonParseError parse{};
            auto document=QJsonDocument::fromJson(manifest.readAll(),&parse);
            if(parse.error!=QJsonParseError::NoError || !document.isObject())
                throw std::runtime_error("Malformed character manifest JSON");
            auto descriptor=document.object();
            auto const id=descriptor.value("id").toString();
            if(descriptor.value("schema").toString()!="northstar.character.v1" ||
               id!=parentVirtual(selected) ||
               !safe_virtual_path(id.toUtf8().toStdString()))
                throw std::runtime_error("Invalid character identity or schema");
            QString model=descriptor.value("model").toString();
            QString skeleton=descriptor.value("skeleton").toString();
            if(!safe_virtual_path(model.toUtf8().toStdString()) ||
               !safe_virtual_path(skeleton.toUtf8().toStdString()))
                throw std::runtime_error("Invalid character model/rig reference");
            info+="\nCharacter: "+id+"\nModel: "+model+
                  "\nSkeleton: "+skeleton;
            auto modelFile=activeCatalog().find(model.toUtf8().toStdString());
            auto rigFile=activeCatalog().find(skeleton.toUtf8().toStdString());
            info+=(modelFile && rigFile)?"\nResources: indexed":
                "\nResources: model or skeleton missing";
            info+="\nAnimations: "+QString::number(
                descriptor.value("animations").toArray().size());
        }catch(std::exception const& e) {
            info+="\nInvalid character manifest: "+QString::fromUtf8(e.what());
        }
    }
    if(rec && rec->is_list_file()) {
        if(fs::path(rec->virtual_path).extension()==".nac")
            info+="\nNorthStar Audio Container (NAC)";
        else
            info+="\nNEF8 ListFile (virtual directory)";
        info+="\nEntries: "+QString::number(rec->virtual_child_count);
    } else if(rec) {

        info+="\nSize: "+bytesText(rec->size);
        if(rec->modified_epoch_ms)
            info+="\nModified: "+QDateTime::fromMSecsSinceEpoch(
                rec->modified_epoch_ms).toString(Qt::ISODate);
        info+="\nSource: "+QString(engine_selected_?
            "Engine Shared Assets (read-only)":"Project Assets");
        info+="\nAsset ID: "+from8(rec->virtual_path);
        if(rec->is_virtual_entry())
            info+=(fs::path(rec->container_path).extension()==".nac"?
                QString("\nNAC track: ")+from8(rec->filename)+
                "\nTrack ID: "+QString::number(rec->audio_track_id)+
                "\nSample rate: "+QString::number(rec->audio_sample_rate)+" Hz"+
                "\nChannels: "+QString::number(rec->audio_channels)+
                "\nDuration: "+QString::number(
                    rec->audio_sample_rate?double(rec->audio_frames)/
                    rec->audio_sample_rate:0.0,'f',2)+" s"+
                "\nReference: "+from8(rec->asset_reference()):
                QString("\nNEF8 entry: ")+from8(rec->asset_reference()));
    }
    if(rec && registry_ && !engine_selected_) {
        auto const references=registry_->dependenciesOf(selected);
        auto const referencers=registry_->referencersOf(selected);
        info+="\nDependencies: "+QString::number(references.size())+
              "\nReferenced by: "+QString::number(referencers.size());
        for(auto const& path:referencers.mid(0,8))
            info+="\n  <- "+path;
        if(referencers.size()>8)info+="\n  ...";
    }
    preview_details_->setText(info);
}
} // namespace northstarcpp::editor
