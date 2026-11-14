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
#include <cstdint>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QSlider;
class QSplitter;
class QStandardItemModel;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

namespace northstarcpp::editor {
class QtContentBrowser final : public QWidget {
    Q_OBJECT
public:
    explicit QtContentBrowser(QWidget* parent=nullptr);
    ~QtContentBrowser() override;
    void setContentRoot(QString const& path);
    void setProjectScene(QString const& scene);
    void refresh();
    QString selectedAsset() const;
    int recordCount() const;
    int visibleCount() const;
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
private:
    AssetCatalog catalog_;
    QString root_,folder_;
    quint64 generation_=0;
    QTreeWidget* folders_{};
    QListView* grid_{};
    QStandardItemModel* entries_{};
    QLineEdit* search_{};
    QComboBox* types_{};
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
    QSplitter* splitter_{};
    QHash<QString,QIcon> icons_;
    QHash<QString,QImage> thumbs_;
    QSet<QString> pending_;
    QThreadPool pool_;
    QStringList back_history_, forward_history_;
    void populateTree();
    void populateItems();
    void openFolder(QString const& folder,bool history=true);
    void changeFolder(QString const& folder);
    void scheduleThumbnails();
    void updateDetails();
    void showMenu(QPoint pos);
    void createFolder();
    void importFiles();
    void renameSelected();
    void duplicateSelected();
    void removeSelected();
    void revealSelected();
    void copyReference();
    void updateBreadcrumb();
    QIcon iconFor(AssetKind kind) const;
    QIcon iconForDirectory(AssetRecord const& record) const;
    QString subtitleText(AssetRecord const& record) const;
    QColor accentColor(AssetRecord const& record) const;
    bool isSafeBasename(QString const& text) const;
    bool selectedIsDirectory() const;
    QString selectedPath() const;
    QString itemPath(QModelIndex const& index) const;
    void queueThumbnail(QString const& virtualPath);
};
} // namespace northstarcpp::editor
