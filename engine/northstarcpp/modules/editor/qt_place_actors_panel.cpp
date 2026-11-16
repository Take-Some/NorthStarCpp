#include "qt_place_actors_panel.hpp"
#include <northstarcpp/scene_component_registry.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>

namespace northstarcpp::editor {
namespace {
constexpr auto settingsGroup="PlaceActorsV1";
// Separate MIME from asset references: a drop creates an actor through
// registered scene/authoring factories, not a fake file-based mesh.
constexpr auto placeMime="application/x-northstar-placeable-id";

class PlaceList final : public QListWidget {
public:
    explicit PlaceList(QWidget* parent):QListWidget(parent) {
        setObjectName("placeActorsList");
        setDragEnabled(true);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setSpacing(3);
    }
protected:
    void mousePressEvent(QMouseEvent* event) override {
        pressPos_=event->pos();
        QListWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if(!(event->buttons()&Qt::LeftButton) ||
           (event->pos()-pressPos_).manhattanLength()<
               QApplication::startDragDistance()) {
            QListWidget::mouseMoveEvent(event);
            return;
        }
        auto* item=currentItem();
        if(!item)return;
        auto const id=item->data(Qt::UserRole).toString();
        if(id.isEmpty())return;
        auto* mime=new QMimeData;
        mime->setData(placeMime,id.toUtf8());
        auto* drag=new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
private:
    QPoint pressPos_;
};
} // namespace

QVector<PlaceActorEntry> availablePlaceActors() {
    QVector<PlaceActorEntry> entries{
        {"geometry.cube","Cube","Geometry","Editable box primitive"},
        {"geometry.cylinder","Cylinder","Geometry","Procedural solid cylinder"},
        {"geometry.ellipsoid","Ellipsoid","Geometry","Procedural ellipsoid"},
        {"geometry.ring","Ring","Geometry","Procedural ring"},
        {"geometry.torus","Torus","Geometry","Procedural torus"},
        {"gameplay.player_blueprint","Player Blueprint","Gameplay",
         "Controller + Pawn + Camera + Mesh + Animation"},
        {"gameplay.character","Character Pawn","Gameplay",
         "Independent Pawn with motor and capsule"},
        {"camera.free","Camera Actor","Cameras",
         "Independent camera with a selectable cinematic priority"},
        {"light.point","Point Light","Lights",
         "Omnidirectional dynamic light (engine.render.light)"},
        {"light.spot","Spot Light","Lights",
         "Cone-shaped dynamic light (engine.render.light)"}
    };
    // Provider-driven actor factories are offered only when a default, zero
    // initialized component is actually accepted by the active ECS schema.
    // Never expose arbitrary schemas that would create invalid scene blobs.
    for(auto const& schema:list_scene_component_schemas()) {
        if(schema.type=="northstar.scene.camera" ||
           schema.type=="engine.render.light")continue;
        std::size_t bytes=0;
        for(auto const& field:schema.editor_fields)
            bytes=std::max(bytes,std::size_t(field.offset)+4);
        if(bytes>4096)continue;
        SceneComponentBlob test{0,schema.type,schema.min_version,
                                std::string(bytes,'\0')};
        if(!validate_scene_component_blob(test))continue;
        auto const id=QString::fromStdString(schema.type);
        entries.push_back({"component."+id,id,"Components",
            "Provider component v"+QString::number(schema.min_version)});
    }
    return entries;
}
QtPlaceActorsPanel::QtPlaceActorsPanel(QWidget* parent):QWidget(parent) {
    setObjectName("placeActorsPanel");
    QSettings settings("KaylasSystems","NorthStarQtEditor");
    settings.beginGroup(settingsGroup);
    favorites_=settings.value("favorites").toStringList();
    recent_=settings.value("recent").toStringList();
    settings.endGroup();

    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(8,8,8,8);
    layout->setSpacing(7);
    auto* heading=new QLabel("PLACE ACTORS",this);
    heading->setObjectName("placeActorsHeading");
    layout->addWidget(heading);
    filter_=new QLineEdit(this);
    filter_->setObjectName("placeActorsSearch");
    filter_->setPlaceholderText("Search actors and components...");
    filter_->setClearButtonEnabled(true);
    layout->addWidget(filter_);
    category_=new QComboBox(this);
    category_->setObjectName("placeActorsCategory");
    category_->addItems({"All","Favorites","Recent","Geometry",
                         "Gameplay","Cameras","Lights","Components"});
    layout->addWidget(category_);
    auto* help=new QLabel("Double-click to add at origin, or drag into Viewport. "
                          "Right-click to pin an actor.",this);
    help->setObjectName("placeActorsHelp");
    help->setWordWrap(true);
    layout->addWidget(help);
    list_=new PlaceList(this);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(list_,1);
    connect(filter_,&QLineEdit::textChanged,this,[this]{rebuild();});
    connect(category_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[this]{rebuild();});
    connect(list_,&QListWidget::itemDoubleClicked,this,
            [this](QListWidgetItem* item) {
        if(placeRequested && item)
            placeRequested(item->data(Qt::UserRole).toString());
    });
    connect(list_,&QWidget::customContextMenuRequested,this,
            [this](QPoint point) {
        auto* item=list_->itemAt(point);
        if(!item)return;
        const auto id=item->data(Qt::UserRole).toString();
        QMenu menu(this);
        menu.addAction("Add Actor",[this,id] {
            if(placeRequested)placeRequested(id);
        });
        menu.addAction(favorites_.contains(id)?"Remove from Favorites":
                        "Add to Favorites",[this,id] {toggleFavorite(id);});
        menu.exec(list_->viewport()->mapToGlobal(point));
    });
    refreshCatalog();
}
void QtPlaceActorsPanel::refreshCatalog() {
    entries_=availablePlaceActors();
    rebuild();
}
bool QtPlaceActorsPanel::hasEntry(QString const& id) const {
    return std::any_of(entries_.cbegin(),entries_.cend(),
        [&](PlaceActorEntry const& value){return value.id==id;});
}
void QtPlaceActorsPanel::rebuild() {
    if(!list_ || !category_ || !filter_)return;
    const auto current=list_->currentItem()?
        list_->currentItem()->data(Qt::UserRole).toString():QString{};
    list_->clear();
    const auto chosen=category_->currentText();
    const auto needle=filter_->text().trimmed();
    auto addItem=[&](PlaceActorEntry const& entry) {
        if(!needle.isEmpty() &&
           !(entry.label+" "+entry.id+" "+entry.description)
                .contains(needle,Qt::CaseInsensitive))return;
        if(chosen=="Favorites" && !favorites_.contains(entry.id))return;
        if(chosen=="Recent" && !recent_.contains(entry.id))return;
        if(chosen!="All" && chosen!="Favorites" && chosen!="Recent" &&
           chosen!=entry.category)return;
        auto* item=new QListWidgetItem(list_);
        item->setData(Qt::UserRole,entry.id);
        item->setText(QString("%1%2\n%3")
            .arg(favorites_.contains(entry.id)?"★  ":"")
            .arg(entry.label,entry.description));
        item->setToolTip(entry.id+" — "+entry.description);
        item->setSizeHint(QSize(100,51));
        if(entry.id==current)list_->setCurrentItem(item);
    };
    if(chosen=="Recent") {
        for(auto const& id:recent_)
            for(auto const& entry:entries_)
                if(entry.id==id)addItem(entry);
    } else {
        for(auto const& entry:entries_)addItem(entry);
    }
}
void QtPlaceActorsPanel::toggleFavorite(QString const& id) {
    if(!hasEntry(id))return;
    if(favorites_.contains(id))favorites_.removeAll(id);
    else favorites_.prepend(id);
    QSettings settings("KaylasSystems","NorthStarQtEditor");
    settings.setValue(QString(settingsGroup)+"/favorites",favorites_);
    rebuild();
}
void QtPlaceActorsPanel::setFavorite(QString const& id,bool enabled) {
    if(hasEntry(id) && favorites_.contains(id)!=enabled)
        toggleFavorite(id);
}
void QtPlaceActorsPanel::recordPlaced(QString const& id) {
    if(!hasEntry(id))return;
    recent_.removeAll(id);
    recent_.prepend(id);
    while(recent_.size()>16)recent_.removeLast();
    QSettings settings("KaylasSystems","NorthStarQtEditor");
    settings.setValue(QString(settingsGroup)+"/recent",recent_);
    rebuild();
}
} // namespace northstarcpp::editor
