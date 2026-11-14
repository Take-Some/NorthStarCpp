// Native Qt 6 Content Browser for NorthStar Editor.
// No dependency on HWND, Windows Common Controls, or shell icons.
#include "qt_content_browser.hpp"
#include <northstarcpp/assets.hpp>
#include "editor_asset_thumbnails.hpp"
#include "qt_asset_preview_dialog.hpp"
#include "editor_asset_material_source.hpp"
#include "qt_asset_card_delegate.hpp"

#include <QtConcurrent/QtConcurrent>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QDir>
#include <QCursor>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
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

// The Qt RCC entry point has global linkage. This initializer must be
// declared in the global namespace (not even an anonymous namespace).
static void ensureFolderArtResource() {
    Q_INIT_RESOURCE(northstar_folder_icons);
}

namespace northstarcpp::editor {
namespace fs=std::filesystem;
namespace {
constexpr int role_path=asset_card_roles::path;
constexpr int role_directory=asset_card_roles::directory;
constexpr int role_kind=asset_card_roles::kind;
constexpr int role_reference=asset_card_roles::reference;
QString qt(fs::path const& path) {
    auto u8=path.generic_u8string();
    return QString::fromUtf8(u8.data(),static_cast<int>(u8.size()));
}
fs::path native(QString const& path) { return fs::u8path(path.toUtf8().toStdString()); }
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
    case AssetKind::material:return {QColor("#A66AD6"),"MAT"};
    case AssetKind::texture:return {QColor("#4DBA87"),"TEX"};
    case AssetKind::audio:return {QColor("#D8A24E"),"WAV"};
    case AssetKind::scene:return {QColor("#70B7C6"),"LVL"};
    case AssetKind::script:return {QColor("#D97893"),"JS"};
    case AssetKind::animation:return {QColor("#7DBD68"),"ANIM"};
    case AssetKind::shader:return {QColor("#6FAFD7"),"HLSL"};
    default:return {QColor("#718198"),"FILE"};
    }
}
// A read-only disk dictionary may be previewed before engine.assets is
// installed. Cache the last parsed ListFile per thumbnail worker thread,
// invalidating on file change to avoid repeated NEF8 inflate per item.
QImage directListFileThumbnail(fs::path const& root,std::string const& key,
                               AssetKind kind) {
    auto const separator=key.find('@');
    if(separator==std::string::npos ||
       (kind!=AssetKind::texture && kind!=AssetKind::mesh &&
        kind!=AssetKind::material))return {};
    auto const relative=key.substr(0,separator);
    auto const name=key.substr(separator+1);
    if(kind==AssetKind::material) {
        // NEF8 material previews share a codec/Qt worker backend. Protect
        // the material-decoding section pending a full sanitizer audit,
        // while leaving texture and model thumbnails parallel.
        static std::mutex materialPreviewMutex;
        std::lock_guard<std::mutex> guard(materialPreviewMutex);
        auto resources=loadMaterialPreviewSource(root,key);
        if(!resources.material)return {};
        auto sphere=northstarcpp::editor_tools::preview_material(
            *resources.material,resources.textures,256);
        if(!sphere.valid())return {};
        QImage bitmap(reinterpret_cast<uchar const*>(sphere.argb.data()),
             sphere.width,sphere.height,sphere.width*4,QImage::Format_ARGB32);
        return bitmap.copy();
    }
    if(!safe_virtual_path(relative) || name.empty() ||
       name.find_first_of("@/\\\\:")!=std::string::npos)return {};
    auto const full=root/fs::u8path(relative);
    std::error_code ec;
    auto const canonical_root=fs::weakly_canonical(root,ec);
    if(ec)return {};
    auto const canonical_file=fs::weakly_canonical(full,ec);
    if(ec)return {};
    auto base=canonical_root.begin();
    auto entry=canonical_file.begin();
    for(;base!=canonical_root.end();++base,++entry)
        if(entry==canonical_file.end() || *entry!=*base)return {};
    if(fs::is_symlink(fs::symlink_status(full,ec)) || ec)return {};
    auto const bytes_count=fs::file_size(full,ec);
    if(ec || bytes_count<128 || bytes_count>64u*1024u*1024u)return {};
    auto const modified=fs::last_write_time(full,ec);
    if(ec)return {};
    struct Cache {
        fs::path file;
        fs::file_time_type modified{};
        std::uintmax_t bytes=0;
        std::vector<northstarcpp::shared_assets::Texture> textures;
        std::vector<northstarcpp::shared_assets::Drawable> drawables;
    };
    static thread_local Cache cache;
    if(cache.file!=full || cache.modified!=modified || cache.bytes!=bytes_count) {
        std::ifstream input(full,std::ios::binary);
        if(!input)return {};
        std::vector<std::uint8_t> encoded(static_cast<std::size_t>(bytes_count));
        if(!input.read(reinterpret_cast<char*>(encoded.data()),
                       static_cast<std::streamsize>(encoded.size())))return {};
        Cache updated;
        updated.file=full;updated.modified=modified;updated.bytes=bytes_count;
        if(kind==AssetKind::texture)
            updated.textures=northstarcpp::shared_assets::load_textures_from_bytes(encoded,relative);
        else
            updated.drawables=northstarcpp::shared_assets::load_drawables_from_bytes(encoded,relative);
        cache=std::move(updated);
    }
    northstarcpp::editor_tools::AssetPreviewImage preview;
    if(kind==AssetKind::texture) {
        auto found=std::find_if(cache.textures.begin(),cache.textures.end(),
            [&](auto const& item){return item.name==name;});
        if(found==cache.textures.end())return {};
        preview=northstarcpp::editor_tools::preview_texture(*found,256);
    } else {
        auto found=std::find_if(cache.drawables.begin(),cache.drawables.end(),
            [&](auto const& item){return item.name==name;});
        if(found==cache.drawables.end())return {};
        preview=northstarcpp::editor_tools::preview_drawable(
            *found,256,256,.62f,.27f,1.f,false);
    }
    if(!preview.valid())return {};
    QImage image(reinterpret_cast<uchar const*>(preview.argb.data()),
                 preview.width,preview.height,preview.width*4,
                 QImage::Format_ARGB32);
    return image.copy();
}
QImage engineThumbnail(fs::path const& root,std::string const& key, AssetKind kind) {
    try {
        if(key.find('@')!=std::string::npos &&
           (kind==AssetKind::texture || kind==AssetKind::mesh ||
            kind==AssetKind::material)) {
            // Embedded items have their own identity; never substitute
            // the first entry or a generic icon on successful decode.
            return directListFileThumbnail(root,key,kind);
        }
        AssetManager assets;
        assets.mount_directory("",root,0);
        // Authored resources can refer to the engine's shared asset package.
        std::error_code ec;
        for(auto current=root.parent_path(); !current.empty(); ) {
            auto shared=current/"sharedAssets";
            if(fs::is_directory(shared,ec)) {
                assets.mount_shared_assets(shared);break;
            }
            auto upper=current.parent_path();
            if(upper==current)break;
            current=upper;
        }
        northstarcpp::editor_tools::Thumbnail t;
        if(kind==AssetKind::texture) {
            if(key.find('@')!=std::string::npos)
                t=northstarcpp::editor_tools::thumbnail_texture(
                    assets.load_texture(key));
            else {
                auto list=northstarcpp::shared_assets::load_textures(assets,key);
                if(!list.empty())t=northstarcpp::editor_tools::thumbnail_texture(list.front());
            }
        } else if(kind==AssetKind::mesh) {
            if(key.find('@')!=std::string::npos)
                t=northstarcpp::editor_tools::thumbnail_drawable(
                    assets.load_drawable(key));
            else {
                auto list=northstarcpp::shared_assets::load_drawables(assets,key);
                if(!list.empty())t=northstarcpp::editor_tools::thumbnail_drawable(list.front());
            }
        } else if(kind==AssetKind::material) {
            auto list=northstarcpp::shared_assets::load_materials(assets,key);
            if(!list.empty())t=northstarcpp::editor_tools::thumbnail_material(list.front());
        }
        if(t.valid()) {
            QImage image(reinterpret_cast<uchar const*>(t.bgra.data()),
                t.size,t.size,QImage::Format_ARGB32);
            return image.copy();
        }
    } catch(std::exception const&) {
        // Unavailable/invalid providers keep a category icon.
    }
    return {};
}
class AssetGridModel final:public QStandardItemModel {
public:
    using QStandardItemModel::QStandardItemModel;
    QStringList mimeTypes() const override {
        return {"application/x-northstar-asset-reference","text/plain","text/uri-list"};
    }
    QMimeData* mimeData(QModelIndexList const& indexes) const override {
        auto* data=new QMimeData;
        if(indexes.empty())return data;
        auto index=indexes.first();
        auto path=index.data(role_reference).toString();
        if(!path.isEmpty()) {
            data->setData("application/x-northstar-asset-reference",path.toUtf8());
            data->setText(path);
        }
        return data;
    }
    Qt::DropActions supportedDragActions() const override {return Qt::CopyAction;}
};
}
QtContentBrowser::QtContentBrowser(QWidget* parent):QWidget(parent) {
    ensureFolderArtResource();
    setObjectName("northstarContentBrowser");
    pool_.setMaxThreadCount(2);
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
    import_=tool("Import","Import source files into the project");
    open_content_=tool("Open Content","Select a project Content folder");
    back_=tool("‹","Back");
    forward_=tool("›","Forward");
    up_=tool("↑","Parent folder");
    reload_=tool("⟳","Rescan project content");
    crumb_=new QLabel("Content /",this);
    crumb_->setObjectName("breadcrumb");
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
                          "Scenes","Scripts","Animations","Shaders","Other"})
        types_->addItem(item);
    types_->setMinimumWidth(130);
    toolbar->addWidget(types_);
    list_mode_=tool("▦","Toggle grid/list view");
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
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);
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

    auto* details=new QWidget(splitter_);
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
    preview_icon_->setMinimumHeight(150);
    preview_icon_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    dl->addWidget(preview_icon_);
    preview_name_=new QLabel("Select an asset",details);
    preview_name_->setWordWrap(true);
    preview_name_->setObjectName("previewName");
    dl->addWidget(preview_name_);
    preview_details_=new QLabel(details);
    preview_details_->setWordWrap(true);
    preview_details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    preview_details_->setObjectName("previewMetadata");
    dl->addWidget(preview_details_);
    auto* copy=new QPushButton("Copy Asset Reference",details);
    connect(copy,&QPushButton::clicked,this,[this]{copyReference();});
    dl->addWidget(copy);
    dl->addStretch(1);
    splitter_->addWidget(folders_);
    splitter_->addWidget(grid_);
    splitter_->addWidget(details);
    splitter_->setStretchFactor(0,0);
    splitter_->setStretchFactor(1,1);
    splitter_->setStretchFactor(2,0);
    splitter_->setSizes({235,790,250});
    root->addWidget(splitter_,1);

    auto* bottom=new QHBoxLayout;
    status_=new QLabel("No project loaded",this);
    status_->setObjectName("assetStatus");
    bottom->addWidget(status_,1);
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
    add_->setMenu(addMenu);
    add_->setPopupMode(QToolButton::InstantPopup);
    connect(search_,&QLineEdit::textChanged,this,&QtContentBrowser::populateItems);
    connect(types_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[this](int){populateItems();});
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
    connect(folders_,&QTreeWidget::itemClicked,this,
            [this](QTreeWidgetItem* item,int) {
        openFolder(item->data(0,role_path).toString());
    });
    connect(grid_,&QListView::doubleClicked,this,[this](QModelIndex const& index) {
        auto ref=itemPath(index);
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
void QtContentBrowser::setProjectScene(QString const& scene) {
    auto file=QFileInfo(scene);
    setContentRoot(file.absoluteDir().filePath("assets"));
}
void QtContentBrowser::setContentRoot(QString const& path) {
    QString absolute=QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if(absolute==root_)return;
    root_=absolute;
    folder_.clear();
    back_history_.clear();forward_history_.clear();
    thumbs_.clear();icons_.clear();pending_.clear();
    refresh();
}
void QtContentBrowser::refresh() {
    auto const version=++generation_;
    pending_.clear();
    thumbs_.clear();
    entries_->clear();
    folders_->clear();
    if(root_.isEmpty() || !QFileInfo(root_).isDir()) {
        status_->setText("Choose a project with an assets/ directory.");
        updateBreadcrumb();
        updateDetails();
        return;
    }
    status_->setText("Indexing Content...");
    QString root=root_;
    auto* watcher=new QFutureWatcher<AssetCatalog>(this);
    connect(watcher,&QFutureWatcher<AssetCatalog>::finished,this,
            [this,watcher,version] {
        try {
            auto result=watcher->result();
            if(version==generation_) {
                catalog_=std::move(result);
                populateTree();
                populateItems();
            }
        } catch(std::exception const& e) {
            if(version==generation_)status_->setText(QString::fromUtf8(e.what()));
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&pool_,[root] {
        AssetCatalog result;
        result.scan(native(root),100000);
        return result;
    }));
}
int QtContentBrowser::recordCount() const {
    return static_cast<int>(catalog_.entries().size());
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
    QHash<QString,QTreeWidgetItem*> lookup;
    auto* root=new QTreeWidgetItem(folders_,{"Content"});
    root->setData(0,role_path,QString{});
    root->setIcon(0,iconFor(AssetKind::folder));
    lookup.insert(QString{},root);
    std::vector<QString> paths;
    for(auto const& rec:catalog_.entries())
        if(rec.directory)paths.push_back(from8(rec.virtual_path));
    std::sort(paths.begin(),paths.end(),[](QString const& a,QString const& b) {
        auto ad=a.count('/'),bd=b.count('/');
        return ad!=bd?ad<bd:a.compare(b,Qt::CaseInsensitive)<0;
    });
    for(auto const& path:paths) {
        auto parent=parentVirtual(path);
        auto* owner=lookup.value(parent,root);
        auto* item=new QTreeWidgetItem(owner,{path.section('/',-1)});
        item->setData(0,role_path,path);
        auto const* directory=catalog_.find(path.toUtf8().toStdString());
        item->setIcon(0,directory?iconForDirectory(*directory):
                                   iconFor(AssetKind::folder));
        lookup.insert(path,item);
    }
    folders_->expandItem(root);
    if(!lookup.contains(folder_))folder_.clear();
    folders_->setCurrentItem(lookup.value(folder_,root));
    folders_->blockSignals(false);
    back_->setEnabled(!back_history_.isEmpty());
    forward_->setEnabled(!forward_history_.isEmpty());
    updateBreadcrumb();
}
void QtContentBrowser::openFolder(QString const& folder,bool history) {
    if(folder==folder_)return;
    if(!folder.isEmpty()) {
        auto const* record=catalog_.find(folder.toUtf8().toStdString());
        if(!record || !record->directory)return;
        try {
            auto const backing=catalog_.backing_file(record->virtual_path);
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
    auto const* record=catalog_.find(virtualPath.toUtf8().toStdString());
    if(!record)return;
    if(record->directory) {
        openFolder(virtualPath);
        return;
    }
    // Previewing is read-only. Scene insertion is an explicit separate action
    // in the context menu, avoiding accidental actors on a double-click.
    openAssetPreviewModal(this,native(root_),record->asset_reference(),record->kind);
}
void QtContentBrowser::changeFolder(QString const& folder) {
    folder_=folder;
    back_->setEnabled(!back_history_.isEmpty());
    forward_->setEnabled(!forward_history_.isEmpty());
    up_->setEnabled(!folder_.isEmpty());
    populateItems();
    updateBreadcrumb();
}
void QtContentBrowser::updateBreadcrumb() {
    crumb_->setText(folder_.isEmpty()?"Content /":"Content / "+folder_);
    crumb_->setToolTip(root_+"/"+folder_);
}
QIcon QtContentBrowser::iconFor(AssetKind k) const {
    if(k==AssetKind::folder) {
        // Ordinary directories AND NEF8 virtual ListFile dictionaries
        // share the exact project artwork, rather than the old DIR tile.
        static QIcon const folderArt(
            QStringLiteral(":/northstar/icons/folder.png"));
        if(!folderArt.isNull())return folderArt;
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
    if(record.directory && record.is_list_file()) {
        auto const extension=fs::path(record.virtual_path).extension().u8string();
        if(extension==".ytd") {
            static QIcon const textureFolder(
                QStringLiteral(":/northstar/icons/texturesFolder.png"));
            if(!textureFolder.isNull())return textureFolder;
        } else if(extension==".ydd") {
            static QIcon const modelFolder(
                QStringLiteral(":/northstar/icons/modelsFolder.png"));
            if(!modelFolder.isNull())return modelFolder;
        } else if(extension==".ymat") {
            static QIcon const materialFolder(
                QStringLiteral(":/northstar/icons/materialsFolder.png"));
            if(!materialFolder.isNull())return materialFolder;
        }
    }
    return iconFor(AssetKind::folder);
}
QString QtContentBrowser::subtitleText(AssetRecord const& rec) const {
    QString const type=QString::fromLatin1(kind_name(rec.kind));
    if(rec.directory) {
        if(!rec.is_list_file())return "Folder";
        auto const extension=fs::path(rec.virtual_path).extension().u8string();
        auto const number=std::count_if(catalog_.entries().begin(),
            catalog_.entries().end(),[&](AssetRecord const& child) {
                return child.is_virtual_entry() &&
                       child.container_path==rec.virtual_path;
            });
        auto const singular=extension==".ytd"?"texture":
                            extension==".ydd"?"model":
                            extension==".ymat"?"material":"item";
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
        if(ext==".ydd")return asset_palette(AssetKind::mesh).fill;
        if(ext==".ymat")return asset_palette(AssetKind::material).fill;
    }
    return asset_palette(rec.directory?AssetKind::folder:rec.kind).fill;
}
void QtContentBrowser::populateItems() {
    entries_->clear();
    QString text=search_->text().trimmed();
    auto choice=types_->currentIndex();
    for(auto const& rec:catalog_.entries()) {
        auto path=from8(rec.virtual_path);
        if(!text.isEmpty()) {
            if(!path.contains(text,Qt::CaseInsensitive))continue;
        } else if(parentVirtual(path)!=folder_)continue;
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
            default:break;
            }
            if(rec.kind!=desired)continue;
        }
        auto const icon=rec.directory?iconForDirectory(rec):iconFor(rec.kind);
        auto* item=new QStandardItem(icon,from8(rec.filename));
        item->setEditable(false);
        item->setData(path,role_path);
        item->setData(from8(rec.asset_reference()),role_reference);
        item->setData(rec.directory,role_directory);
        item->setData(static_cast<int>(rec.kind),role_kind);
        item->setData(from8(rec.filename),asset_card_roles::title);
        item->setData(subtitleText(rec),asset_card_roles::subtitle);
        item->setData(accentColor(rec),asset_card_roles::accent_color);
        if(thumbs_.contains(path))
            item->setData(thumbs_.value(path),asset_card_roles::preview_image);
        item->setToolTip(path+"\n"+subtitleText(rec)+
                         "\n"+from8(rec.asset_reference()));
        entries_->appendRow(item);
    }
    QString status=QString::number(entries_->rowCount())+" items  |  "+
                   QString::number(catalog_.entries().size())+" indexed";
    if(catalog_.truncated())status+="  |  index limit reached";
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
    auto k=static_cast<AssetKind>(0);
    for(int i=0;i<entries_->rowCount();++i)
        if(entries_->item(i)->data(role_path).toString()==path) {
            k=static_cast<AssetKind>(entries_->item(i)->data(role_kind).toInt());
            break;
        }
    if(!isImage(path) && k!=AssetKind::texture &&
       k!=AssetKind::mesh && k!=AssetKind::material)return;
    auto const version=generation_;
    auto const base=root_;
    auto const* rec=catalog_.find(path.toUtf8().toStdString());
    if(!rec)return;
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
        if(version==generation_) {
            pending_.remove(path);
            if(!result.isNull()) {
                thumbs_.insert(path,result);
                if(thumbs_.size()>480)thumbs_.erase(thumbs_.begin());
                for(int i=0;i<entries_->rowCount();++i) {
                    auto* item=entries_->item(i);
                    if(item->data(role_path).toString()==path) {
                        // Do not overwrite the category/folder fallback icon:
                        // the card delegate reads the real preview via a
                        // dedicated role and paints it inside the safe-zone.
                        item->setData(result,asset_card_roles::preview_image);
                        break;
                    }
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
            if(!metadata.isFile() || metadata.size()>128ll*1024*1024)
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
        return;
    }
    auto index=grid_->currentIndex();
    auto kind=static_cast<AssetKind>(index.data(role_kind).toInt());
    auto filename=selected.section('/',-1);
    preview_name_->setText(filename);
    if(thumbs_.contains(selected)) {
        preview_icon_->setPixmap(QPixmap::fromImage(thumbs_[selected]).scaled(
            190,190,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    } else {
        auto const* selectedRecord=catalog_.find(selected.toUtf8().toStdString());
        auto const icon=selectedRecord && selectedRecord->directory?
            iconForDirectory(*selectedRecord):iconFor(kind);
        preview_icon_->setPixmap(icon.pixmap(130,130));
    }
    QString info=QString::fromLatin1(kind_name(kind))+"\nContent / "+selected;
    auto const* rec=catalog_.find(selected.toUtf8().toStdString());
    if(rec && rec->is_list_file()) {
        info+="\nNEF8 ListFile (virtual directory)";
        auto count=std::count_if(catalog_.entries().begin(),catalog_.entries().end(),
            [&](AssetRecord const& child) {
                return child.is_virtual_entry() &&
                       child.container_path==rec->virtual_path;
            });
        info+="\nEntries: "+QString::number(count);
    } else if(rec) {
        auto file=QFileInfo(qt(catalog_.backing_file(rec->virtual_path)));
        info+="\nSize: "+bytesText(rec->size);
        info+="\nModified: "+file.lastModified().toString(Qt::ISODate);
        if(rec->is_virtual_entry())
            info+="\nNEF8 entry: "+from8(rec->asset_reference());
    }
    preview_details_->setText(info);
}
bool QtContentBrowser::isSafeBasename(QString const& name) const {
    if(name.isEmpty() || name=="." || name==".." || name.size()>120 ||
       name.contains('/') || name.contains('\\') || name.contains(':') ||
       name.contains(QChar('\0')))return false;
    return true;
}
void QtContentBrowser::createFolder() {
    if(root_.isEmpty())return;
    if(auto const* folder=catalog_.find(folder_.toUtf8().toStdString());
       folder && folder->is_list_file()) {
        QMessageBox::information(this,"NEF8 ListFile",
            "Dictionary entries are read-only. Edit the source asset with an importer.");
        return;
    }
    bool accepted=false;
    QString name=QInputDialog::getText(this,"New Content Folder",
        "Folder name:",QLineEdit::Normal,"NewFolder",&accepted).trimmed();
    if(!accepted)return;
    if(!isSafeBasename(name)) {
        QMessageBox::warning(this,"Invalid Folder","Use a safe folder name.");return;
    }
    auto target=QDir(root_).filePath(folder_.isEmpty()?name:folder_+"/"+name);
    if(!QDir().mkpath(target))
        QMessageBox::warning(this,"Folder","Unable to create this folder.");
    else refresh();
}
void QtContentBrowser::importFiles() {
    if(root_.isEmpty())return;
    if(auto const* folder=catalog_.find(folder_.toUtf8().toStdString());
       folder && folder->is_list_file()) {
        QMessageBox::information(this,"NEF8 ListFile",
            "Dictionary entries are read-only. Edit the source asset with an importer.");
        return;
    }
    QFileDialog dialog(this,"Import Project Assets");
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setOption(QFileDialog::DontUseNativeDialog,true);
    if(dialog.exec()!=QDialog::Accepted)return;
    int imported=0;
    for(auto const& source:dialog.selectedFiles()) {
        auto file=QFileInfo(source);
        if(!file.isFile() || file.size()>512ll*1024*1024)continue;
        auto dest=QDir(root_).filePath(folder_.isEmpty()?file.fileName():
                                            folder_+"/"+file.fileName());
        if(QFileInfo::exists(dest)) {
            auto choice=QMessageBox::question(this,"Existing Asset",
                "The asset "+file.fileName()+" already exists. Skip it?",
                QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Yes);
            if(choice==QMessageBox::Cancel)break;
            continue;
        }
        if(QFile::copy(source,dest))++imported;
    }
    if(imported)refresh();
}
void QtContentBrowser::renameSelected() {
    auto path=selectedPath();
    if(auto const* rec=catalog_.find(path.toUtf8().toStdString());
       rec && rec->is_virtual_entry()) {
        QMessageBox::information(this,"NEF8 ListFile",
            "Embedded dictionary entries cannot be modified as physical files.");
        return;
    }
    if(path.isEmpty())return;
    bool accepted=false;
    auto name=QInputDialog::getText(this,"Rename Asset","New name:",
        QLineEdit::Normal,path.section('/',-1),&accepted).trimmed();
    if(!accepted)return;
    if(!isSafeBasename(name)) {
        QMessageBox::warning(this,"Invalid Name","Use a safe asset name.");return;
    }
    try {
        auto old=catalog_.resolve(path.toUtf8().toStdString());
        auto dest=old.parent_path()/native(name);
        if(fs::exists(dest))
            throw std::runtime_error("An asset already exists with this name");
        fs::rename(old,dest);
        refresh();
    } catch(std::exception const& e) {
        QMessageBox::warning(this,"Rename Asset",QString::fromUtf8(e.what()));
    }
}
void QtContentBrowser::duplicateSelected() {
    if(selectedIsDirectory())return;
    if(auto const* rec=catalog_.find(selectedPath().toUtf8().toStdString());
       rec && rec->is_virtual_entry())return;
    auto path=selectedPath();if(path.isEmpty())return;
    try {
        auto old=catalog_.resolve(path.toUtf8().toStdString());
        auto dest=old.parent_path()/
            (old.stem().u8string()+"_copy"+old.extension().u8string());
        if(fs::exists(dest))throw std::runtime_error("Duplicate already exists");
        fs::copy_file(old,dest);
        refresh();
    } catch(std::exception const& e) {
        QMessageBox::warning(this,"Duplicate Asset",QString::fromUtf8(e.what()));
    }
}
void QtContentBrowser::removeSelected() {
    auto path=selectedPath();
    if(auto const* rec=catalog_.find(path.toUtf8().toStdString());
       rec && rec->is_virtual_entry()) {
        QMessageBox::information(this,"NEF8 ListFile",
            "Embedded dictionary entries cannot be modified as physical files.");
        return;
    }if(path.isEmpty())return;
    if(QMessageBox::question(this,"Delete Asset",
        "Delete "+path+"? Folders must be empty; this cannot be undone.",
        QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    try {
        auto full=catalog_.resolve(path.toUtf8().toStdString());
        // Never recursively delete a directory tree, especially not with symlinks.
        if(!fs::remove(full))throw std::runtime_error("File/folder could not be removed");
        refresh();
    } catch(std::exception const& e) {
        QMessageBox::warning(this,"Delete Asset",QString::fromUtf8(e.what()));
    }
}
void QtContentBrowser::copyReference() {
    auto ref=selectedAsset();
    if(!ref.isEmpty())QApplication::clipboard()->setText(ref);
}
void QtContentBrowser::revealSelected() {
    auto ref=selectedPath();
    if(ref.isEmpty())return;
    try {
        auto full=catalog_.backing_file(ref.toUtf8().toStdString());
        auto parent=fs::is_directory(full)?full:full.parent_path();
        QDesktopServices::openUrl(QUrl::fromLocalFile(qt(parent)));
    } catch(std::exception const&) {}
}
void QtContentBrowser::showMenu(QPoint point) {
    QMenu menu(this);
    auto selected=selectedPath();
    auto* open=menu.addAction("Preview / Browse");
    auto* insert=menu.addAction("Add Mesh to Scene");
    auto* copy=menu.addAction("Copy Asset Reference");
    menu.addSeparator();
    auto* create=menu.addAction("New Folder...");
    auto* import=menu.addAction("Import Files...");
    auto* rename=menu.addAction("Rename...");
    auto* duplicate=menu.addAction("Duplicate");
    auto* reveal=menu.addAction("Show Containing Folder");
    menu.addSeparator();
    auto* remove=menu.addAction("Delete...");
    open->setEnabled(!selected.isEmpty());
    auto const* chosen=catalog_.find(selected.toUtf8().toStdString());
    insert->setEnabled(chosen && !chosen->directory &&
                       chosen->kind==AssetKind::mesh);
    copy->setEnabled(!selected.isEmpty()&&!selectedIsDirectory());
    rename->setEnabled(!selected.isEmpty());
    duplicate->setEnabled(!selected.isEmpty()&&!selectedIsDirectory());
    reveal->setEnabled(!selected.isEmpty());
    remove->setEnabled(!selected.isEmpty());
    auto* selectedAction=menu.exec(point.isNull()?QCursor::pos():
                                           grid_->viewport()->mapToGlobal(point));
    if(selectedAction==open) {
        if(selectedIsDirectory())openFolder(selected);
        else if(!selected.isEmpty())openAssetPreview(selected);
    } else if(selectedAction==insert) {
        if(chosen && chosen->kind==AssetKind::mesh)
            emit assetActivated(QString::fromUtf8(chosen->asset_reference().c_str()));
    } else if(selectedAction==copy)copyReference();
    else if(selectedAction==create)createFolder();
    else if(selectedAction==import)importFiles();
    else if(selectedAction==rename)renameSelected();
    else if(selectedAction==duplicate)duplicateSelected();
    else if(selectedAction==reveal)revealSelected();
    else if(selectedAction==remove)removeSelected();
}
} // namespace northstarcpp::editor
