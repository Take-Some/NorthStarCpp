// NorthStar Editor Content Browser: reusable cross-platform Qt 6 widget.
// No dependency on HWND, Win32 common controls, or Shell file icons.
#pragma once
#include <northstarcpp/editor_asset_catalog.hpp>
#include <QWidget>
#include <QFutureWatcher>
#include <QImage>
#include <QPixmap>
#include <QThreadPool>
#include <QString>
#include <QHash>
#include <QIcon>
#include <QColor>
#include <QSet>
#include <memory>
#include <functional>
#include <QMap>
#include <cstdint>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QSlider;
class QPushButton;
class QSplitter;
class QStandardItem;
class QStandardItemModel;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;
class QTimer;
class QFileSystemWatcher;

namespace northstarcpp::editor {
class AssetDependencyRegistry;
class QtContentBrowser final : public QWidget {
    Q_OBJECT
public:
    explicit QtContentBrowser(QWidget* parent=nullptr);
    ~QtContentBrowser() override;
    void setContentRoot(QString const& path);
    void setProjectScene(QString const& scene);
    // Both VFS sources are displayed as persistent top-level tree roots.
    // Engine shared assets are previewable but not mutated by the browser.
    void setSharedContentRoot(QString const& path);
    QString sharedContentRoot() const {return engine_root_;}
    bool viewingSharedAssets() const noexcept {return engine_selected_;}
    void selectAssetSource(bool shared);
    // Editor owns the mounted project root; content navigation must remain
    // inside its dock rather than opening a second browser or an arbitrary tree.
    void setEditorDockMode(bool enabled);
    bool editorDockMode() const noexcept {return editor_dock_mode_;}
    void setMutationPreflight(std::function<bool()> callback) {
        mutation_preflight_=std::move(callback);
    }
    QString contentRoot() const {return project_root_;}
    void focusSearch();
    // Browse to and highlight a project asset; deferred until indexing
    // completes when another scene has just been opened.
    void revealAsset(QString const& virtualReference);
    void refresh();
    QString selectedAsset() const;
    int recordCount() const;
    int visibleCount() const;
    void setActiveCollection(QString const& name);
    // Returns an empty image until asynchronous preview generation completes.
    QImage cachedThumbnail(QString const& virtualPath) const;
    void setSearchText(QString const& search);
    void setTypeFilter(QString const& kind);
    // Navigate to a physical folder or an NEF8 virtual dictionary.
    void browseTo(QString const& virtualFolder);
    // Double-click action: show read-only texture/mesh inspection in a modal.
    void openAssetPreview(QString const& virtualPath);
signals:
    void assetActivated(QString virtualPath);
    void assetSelected(QString virtualPath);
    void textEditorRequested(QString absolutePath,bool sharedAsset);
    // Keep additional dock instances on the same currently opened project.
    void contentRootsChanged(QString projectAssets,QString engineAssets);
    void assetReferencesRemapped(QMap<QString,QString> changes);
    // Double-click always requests the material editor, not a generic read-only card.
    void materialEditorRequested(QString materialReference);
private:
    AssetCatalog project_catalog_,engine_catalog_;
    AssetCatalog const& activeCatalog() const noexcept {
        return engine_selected_?engine_catalog_:project_catalog_;
    }
    QString root_,project_root_,engine_root_,folder_;
    bool engine_selected_=false;
    quint64 thumbnail_generation_=0;
    QString pending_reveal_;
    bool editor_dock_mode_=false;
    bool catalog_ready_=false;
    quint64 generation_=0;
    std::shared_ptr<AssetDependencyRegistry> registry_;
    std::function<bool()> mutation_preflight_;
    QFileSystemWatcher* change_watcher_{};
    QTimer* change_debounce_{};
    quint64 registry_generation_=0;
    QHash<QString,QString> git_decorations_;
    QTreeWidget* folders_{};
    QListView* grid_{};
    QStandardItemModel* entries_{};
    QLineEdit* search_{};
    QComboBox* types_{};
    QComboBox* collections_{};
    QString active_collection_;
    QComboBox* sort_{};
    QToolButton* back_{};
    QToolButton* forward_{};
    QToolButton* up_{};
    QToolButton* reload_{};
    QToolButton* add_{};
    QToolButton* import_{};
    QToolButton* open_content_{};
    QToolButton* list_mode_{};
    QSlider* zoom_{};
    QLabel* crumb_{};
    QLabel* status_{};
    QLabel* preview_icon_{};
    QLabel* preview_name_{};
    QLabel* preview_details_{};
    QPushButton* audio_preview_{};
    QSplitter* splitter_{};
    QHash<QString,QIcon> icons_;
    QHash<QString,QImage> thumbs_;
    // Current folder only. Avoid repeated O(visible rows) scans when a
    // thumbnail finishes or is queued.
    QHash<QString,QStandardItem*> visible_items_;
    QSet<QString> pending_;
    QThreadPool pool_;
    QStringList back_history_, forward_history_;
    void scheduleDependencyIndex();
    void configureFileWatching();
    void refreshGitDecorations();
    void renameMoveSelected();
    void showDependencyGraph();
    void populateTree();
    void populateItems();
    void openFolder(QString const& folder,bool history=true);
    void changeFolder(QString const& folder);
    void scheduleThumbnails();
    void updateDetails();
    void completeReveal();
    void showMenu(QPoint pos);
    void createFolder();
    void importFiles();
    void createNativeModelPrefab();
    void renameSelected();
    void duplicateSelected();
    void removeSelected();
    void revealSelected();
    void copyReference();
    void toggleFavoriteSelected();
    void refreshCollections();
    void createCollection();
    void createTeamCollection();
    void publishLocalCollection();
    void addSelectionToCollection(QString const& name);
    void removeSelectionFromCollection(QString const& name);
    void duplicateSelectedBatch();
    void deleteSelectedBatch();
    void copySelectedReferences();
    void showGitStatus();
    void showGitDiff();
    void gitStage(bool stage);
    QStringList selectedPaths() const;
    void updateBreadcrumb();
    QIcon iconFor(AssetKind kind) const;
    QIcon iconForDirectory(AssetRecord const& record) const;
    // File entries in YMT/YCD NEF8 dictionaries use the file artwork,
    // while their parent dictionaries retain specialized folder artwork.
    QIcon iconForRecord(AssetRecord const& record) const;
    QString subtitleText(AssetRecord const& record) const;
    QColor accentColor(AssetRecord const& record) const;
    bool isSafeBasename(QString const& text) const;
    bool selectedIsDirectory() const;
    QString selectedPath() const;
    QString itemPath(QModelIndex const& index) const;
    void queueThumbnail(QString const& virtualPath);
};
} // namespace northstarcpp::editor
