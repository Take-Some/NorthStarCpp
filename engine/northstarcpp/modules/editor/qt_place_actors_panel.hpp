// NorthStarCpp: Qt-native Place Actors palette; no runtime/UI coupling.
#pragma once
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

class QLineEdit;
class QComboBox;
class QListWidget;

namespace northstarcpp::editor {
struct PlaceActorEntry {
    QString id;
    QString label;
    QString category;
    QString description;
};
QVector<PlaceActorEntry> availablePlaceActors();

class QtPlaceActorsPanel final : public QWidget {
public:
    explicit QtPlaceActorsPanel(QWidget* parent=nullptr);
    std::function<void(QString const&)> placeRequested;
    void refreshCatalog();
    void recordPlaced(QString const& id);
    void setFavorite(QString const& id,bool enabled);
    bool hasEntry(QString const& id) const;
    QStringList recentIds() const {return recent_;}
    QStringList favoriteIds() const {return favorites_;}
private:
    QLineEdit* filter_{};
    QComboBox* category_{};
    QListWidget* list_{};
    QVector<PlaceActorEntry> entries_;
    QStringList favorites_;
    QStringList recent_;
    void rebuild();
    void toggleFavorite(QString const& id);
};
} // namespace northstarcpp::editor
