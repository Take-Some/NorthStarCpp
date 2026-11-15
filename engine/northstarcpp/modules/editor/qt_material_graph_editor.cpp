#include "qt_material_graph_editor.hpp"
#include "qt_material_authoring.hpp"
#include <QFile>
#include <QFileInfo>
#include <northstarcpp/material.hpp>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMimeData>
#include <QLabel>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <exception>
#include <functional>

namespace northstarcpp::editor {
namespace {
QStringList const operations{
    "parameter","texture","scalar","vector2","vector3","color",
    "add","multiply","lerp","dot","one_minus","saturate",
    "normalize","component","cast","function"};
QStringList const materialOutputs{
    "base_color","roughness","metallic","emissive","opacity"};
QStringList const textureSlots{
    "base_color","normal","roughness","metallic","emissive"};
QStringList const parameters{
    "base_color","roughness","metallic","opacity","uv"};
QStringList inputs(QString const& op) {
    if(op=="add" || op=="multiply" || op=="dot")return {"a","b"};
    if(op=="lerp")return {"a","b","t"};
    if(op=="one_minus" || op=="saturate" || op=="normalize" ||
       op=="component" || op=="cast")return {"a"};
    return {};
}
QString nodeTitle(QJsonObject const& node) {
    QString const op=node.value("op").toString();
    if(op=="parameter")return "Parameter: "+node.value("parameter").toString();
    if(op=="texture")return "Texture: "+node.value("slot").toString();
    if(op=="function")return "Function: "+QFileInfo(node.value("path").toString()).completeBaseName()+" / "+node.value("output").toString();
    if(op=="scalar")return "Scalar: "+QString::number(node.value("value").toDouble(),'g',4);
    if(op=="color")return "Color Constant";
    if(op=="vector2")return "Vector2 Constant";
    if(op=="vector3")return "Vector3 Constant";
    if(op=="dot")return "Dot Product";
    if(op=="normalize")return "Normalize";
    if(op=="component")return "Component "+node.value("component").toString("x");
    if(op=="cast")return "Cast to "+node.value("to").toString("vector4");
    if(op=="one_minus")return "One Minus";
    if(op=="saturate")return "Saturate";
    if(op=="lerp")return "Linear Interpolate";
    if(op=="multiply")return "Multiply";
    if(op=="add")return "Add";
    return op;
}
int estimatedWidth(QJsonArray const& array,QString const& id,
                   QSet<QString> seen={}) {
    if(id.isEmpty()||seen.size()>32||seen.contains(id))return 0;
    seen.insert(id);
    QJsonObject node;
    for(auto const& raw:array) {
        if(raw.toObject().value("id").toString()==id) {
            node=raw.toObject();break;
        }
    }
    if(node.isEmpty())return 0;
    QString const op=node.value("op").toString();
    if(op=="scalar"||op=="dot"||op=="component")return 1;
    if(op=="vector2")return 2;
    if(op=="vector3")return 3;
    if(op=="color"||op=="texture")return 4;
    if(op=="parameter") {
        QString value=node.value("parameter").toString();
        if(value=="uv")return 2;
        return value=="base_color"?4:1;
    }
    if(op=="cast") {
        auto const to=node.value("to").toString();
        if(to=="scalar")return 1;
        if(to=="vector2")return 2;
        if(to=="vector3")return 3;
        if(to=="vector4")return 4;
        return 0;
    }
    auto child=[&](QString const& input){
        return estimatedWidth(array,node.value(input).toString(),seen);
    };
    if(op=="one_minus"||op=="saturate"||op=="normalize")
        return child("a");
    if(op=="add"||op=="multiply"||op=="lerp") {
        int const a=child("a"),b=child("b");
        if(a<1||b<1 || (a!=b&&a!=1&&b!=1))return 0;
        return std::max(a,b);
    }
    return 0;
}
QString sourceTypeLabel(QJsonObject const& node) {
    QString const op=node.value("op").toString();
    if(op=="scalar"||op=="component"||op=="dot")return "Scalar";
    if(op=="vector2")return "Vector2";
    if(op=="vector3")return "Vector3";
    if(op=="color"||op=="texture")return "Vector4";
    if(op=="parameter") {
        QString const value=node.value("parameter").toString();
        return value=="uv"?"Vector2":value=="base_color"?"Vector4":"Scalar";
    }
    if(op=="cast") {
        QString const to=node.value("to").toString();
        return to=="scalar"?"Scalar":to=="vector2"?"Vector2":
               to=="vector3"?"Vector3":"Vector4";
    }
    return "Numeric";
}
QColor typeColor(QString const& type) {
    if(type=="Scalar")return QColor("#83c96f");
    if(type=="Vector2")return QColor("#5cc4c8");
    if(type=="Vector3")return QColor("#71a9e0");
    if(type=="Vector4")return QColor("#be94dd");
    if(type=="Vector")return QColor("#6aaff0");
    return QColor("#eac46e");
}
QJsonObject starter() {
    return QJsonObject{
        {"nodes",QJsonArray{
            QJsonObject{{"id","base"},{"op","parameter"},
                        {"parameter","base_color"},{"x",-140.0},{"y",100.0}}}},
        {"outputs",QJsonObject{{"base_color","base"}}}};
}
QPainterPath wirePath(QPointF const& start,QPointF const& end) {
    QPainterPath path(start);
    double d=std::clamp(std::abs(end.x()-start.x())*.55,55.0,180.0);
    path.cubicTo(start+QPointF(d,0),end-QPointF(d,0),end);
    return path;
}
class GraphCanvas final : public QGraphicsView {
public:
    explicit GraphCanvas(MaterialGraphEditor* owner,QGraphicsScene* scene)
      :QGraphicsView(scene,owner),owner_(owner) {
        setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
        setDragMode(QGraphicsView::RubberBandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setBackgroundBrush(QColor("#101821"));
        setMouseTracking(true);
        setMinimumHeight(390);
    }
protected:
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button()==Qt::MiddleButton) {
            pan_=true;
            last_=event->position().toPoint();
            setCursor(Qt::ClosedHandCursor);
            event->accept();return;
        }
        if(event->button()==Qt::LeftButton) {
            int pin=owner_->pinAt(mapToScene(event->position().toPoint()));
            if(pin>=0) {
                pending_=pin;
                pendingStart_=mapToScene(event->position().toPoint());
                preview_=scene()->addPath(QPainterPath{},
                    QPen(QColor("#e7c45b"),2,Qt::DashLine));
                preview_->setZValue(-1);
                event->accept();return;
            }
        }
        QGraphicsView::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if(pan_) {
            QPoint current=event->position().toPoint();
            QPoint delta=current-last_;
            last_=current;
            horizontalScrollBar()->setValue(horizontalScrollBar()->value()-delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value()-delta.y());
            event->accept();return;
        }
        if(pending_>=0) {
            if(preview_) {
                // Endpoint follows the cursor; connections are validated on drop.
                auto start=pendingStart_;
                if(start.isNull())start=mapToScene(event->position().toPoint());
                preview_->setPath(wirePath(start,mapToScene(event->position().toPoint())));
            }
            event->accept();return;
        }
        QGraphicsView::mouseMoveEvent(event);
        if(event->buttons() & Qt::LeftButton)owner_->refreshWires();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if(pan_ && event->button()==Qt::MiddleButton) {
            pan_=false;unsetCursor();event->accept();return;
        }
        if(pending_>=0 && event->button()==Qt::LeftButton) {
            int target=owner_->pinAt(mapToScene(event->position().toPoint()));
            if(preview_) {scene()->removeItem(preview_);delete preview_;preview_=nullptr;}
            int from=pending_;
            pending_=-1;
            if(target>=0 && target!=from)owner_->connectPins(from,target);
            event->accept();return;
        }
        QGraphicsView::mouseReleaseEvent(event);
        if(event->button()==Qt::LeftButton)owner_->moveFinished();
    }
    void contextMenuEvent(QContextMenuEvent* event) override {
        owner_->contextMenu(mapToScene(event->pos()),event->globalPos());
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton) {
            owner_->editAt(mapToScene(event->position().toPoint()));
            event->accept();return;
        }
        QGraphicsView::mouseDoubleClickEvent(event);
    }
    void wheelEvent(QWheelEvent* event) override {
        double next=transform().m11()*(event->angleDelta().y()>0?1.12:1./1.12);
        if(next>=0.3 && next<=3.2)
            scale(event->angleDelta().y()>0?1.12:1./1.12,
                  event->angleDelta().y()>0?1.12:1./1.12);
        event->accept();
    }
public:
    void setPendingStart(QPointF start) {pendingStart_=start;}
private:
    MaterialGraphEditor* owner_;
    bool pan_=false;
    QPoint last_;
    int pending_=-1;
    QPointF pendingStart_;
    QGraphicsPathItem* preview_=nullptr;
};
} // namespace

QJsonObject MaterialGraphEditor::functionDescriptor(QString const& reference) const {
    if(functionRoot_.empty()||reference.isEmpty())return {};
    try {
        auto path=material_authoring::resolve_existing(functionRoot_,reference);
        QFile file(QString::fromStdWString(path.wstring()));
        if(!file.open(QIODevice::ReadOnly)||file.size()>32768)return {};
        auto doc=QJsonDocument::fromJson(file.readAll());
        if(!doc.isObject() ||
           doc.object().value("schema").toString()!="northstar.material-function.v1")
            return {};
        return doc.object();
    }catch(std::exception const&) {return {};}
}
QStringList MaterialGraphEditor::nodeInputs(QJsonObject const& node) const {
    if(node.value("op").toString()!="function")
        return inputs(node.value("op").toString());
    QStringList result;
    for(auto const& raw:functionDescriptor(node.value("path").toString())
                              .value("inputs").toArray())
        result.append(raw.toObject().value("name").toString());
    return result;
}
QString MaterialGraphEditor::nodePinType(
    QJsonObject const& node,QString const& input) const {
    if(node.value("op").toString()!="function")return "Numeric";
    auto const details=functionDescriptor(node.value("path").toString());
    auto const field=input.isEmpty()?"outputs":"inputs";
    for(auto const& raw:details.value(field).toArray()) {
        auto pin=raw.toObject();
        if(input.isEmpty() && pin.value("name").toString()!=node.value("output").toString())
            continue;
        if(!input.isEmpty() && pin.value("name").toString()!=input)
            continue;
        auto type=pin.value("type").toString();
        return type=="scalar"?"Scalar":type=="vector2"?"Vector2":
               type=="vector3"?"Vector3":type=="vector4"?"Vector4":"Numeric";
    }
    return "Numeric";
}
MaterialGraphEditor::MaterialGraphEditor(QJsonObject const& graph,QWidget* parent,
    std::filesystem::path functionRoot)
    :QWidget(parent),document_(graph.isEmpty()?starter():graph),
     functionRoot_(std::move(functionRoot)),enabled_(!graph.isEmpty()) {
    setObjectName("northstarMaterialGraphEditor");
    auto* layout=new QVBoxLayout(this);
    auto* toolbar=new QHBoxLayout;
    palette_=new QComboBox(this);
    palette_->setObjectName("materialGraphNodePalette");
    for(auto const& op:operations)palette_->addItem(nodeTitle({{"op",op}}),op);
    search_=new QLineEdit(this);
    search_->setObjectName("materialGraphNodeSearch");
    search_->setPlaceholderText("Find node...");
    search_->setClearButtonEnabled(true);
    search_->setMinimumWidth(125);
    auto* add=new QPushButton("Add Node",this);
    auto* remove=new QPushButton("Delete",this);
    auto* groupButton=new QPushButton("Comment Group",this);
    auto* back=new QPushButton("Undo",this);
    auto* forward=new QPushButton("Redo",this);
    auto* reset=new QPushButton("Reset",this);
    auto* enable=new QPushButton("Enable Graph",this);
    auto* verify=new QPushButton("Validate",this);
    for(QWidget* control:std::initializer_list<QWidget*>{search_,palette_,add,remove,
                           groupButton,back,forward,reset,enable,verify})toolbar->addWidget(control);
    layout->addLayout(toolbar);
    scene_=new QGraphicsScene(this);
    scene_->setSceneRect(-650,-450,2050,1300);
    view_=new GraphCanvas(this,scene_);
    view_->setObjectName("materialGraphCanvas");
    layout->addWidget(view_,1);
    message_=new QLabel(this);
    message_->setObjectName("materialGraphStatus");
    message_->setWordWrap(true);
    layout->addWidget(message_);
    connect(search_,&QLineEdit::textChanged,this,
            [this](QString const& text){highlightSearch(text);});
    connect(search_,&QLineEdit::returnPressed,this,[this] {
        auto candidate=search_->text().trimmed();
        for(auto const& op:operations) {
            if(op.contains(candidate,Qt::CaseInsensitive) && !candidate.isEmpty()) {
                addNode(op,view_->mapToScene(view_->viewport()->rect().center()));
                search_->clear();return;
            }
        }
    });
    connect(add,&QPushButton::clicked,this,[this] {
        addAt(view_->mapToScene(view_->viewport()->rect().center()));
    });
    connect(remove,&QPushButton::clicked,this,[this]{deleteSelected();});
    connect(groupButton,&QPushButton::clicked,this,[this]{createCommentGroup();});
    connect(back,&QPushButton::clicked,this,[this]{undo();});
    connect(forward,&QPushButton::clicked,this,[this]{redo();});
    connect(reset,&QPushButton::clicked,this,[this]{commit(starter());});
    connect(enable,&QPushButton::clicked,this,[this] {
        enabled_=true;
        emit graphChanged();
        QString error;
        if(validate(&error))setMessage("Graph enabled: ready for material compilation.");
        else setMessage(error,true);
    });
    connect(verify,&QPushButton::clicked,this,[this] {
        QString error;
        if(validate(&error))
            setMessage(enabled_?"Graph compiled successfully.":"Graph inactive; click Enable Graph to use it.");
        else setMessage(error,true);
    });
    auto* copyKey=new QShortcut(QKeySequence::Copy,this);
    auto* pasteKey=new QShortcut(QKeySequence::Paste,this);
    auto* deleteKey=new QShortcut(QKeySequence::Delete,view_);
    auto* findKey=new QShortcut(QKeySequence::Find,this);
    auto* groupKey=new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_G),this);
    connect(groupKey,&QShortcut::activated,this,[this]{createCommentGroup();});
    connect(copyKey,&QShortcut::activated,this,[this]{copySelection();});
    connect(pasteKey,&QShortcut::activated,this,[this]{pasteSelection();});
    connect(deleteKey,&QShortcut::activated,this,[this]{deleteSelected();});
    connect(findKey,&QShortcut::activated,this,[this]{search_->setFocus();search_->selectAll();});
    auto* undoKey=new QShortcut(QKeySequence::Undo,this);
    auto* redoKey=new QShortcut(QKeySequence::Redo,this);
    connect(undoKey,&QShortcut::activated,this,[this]{undo();});
    connect(redoKey,&QShortcut::activated,this,[this]{redo();});
    draw();
    view_->centerOn(190,180);
    setMessage(enabled_?"Drag a pin to another pin. Double-click nodes to edit values."
                       :"Default graph preview. Click Enable Graph to save it.");
}
QJsonObject MaterialGraphEditor::graph() const {
    return enabled_?withPositions():QJsonObject{};
}
bool MaterialGraphEditor::validate(QString* reason) const {
    if(!enabled_)return true;
    try {
        auto data=QJsonDocument(withPositions()).toJson(QJsonDocument::Compact);
        (void)northstarcpp::compile_material_graph_with_functions(
            data.toStdString(),[this](std::string const& reference) {
                return material_authoring::material_function_source(
                    functionRoot_,reference);
            });
        if(reason)reason->clear();
        return true;
    }catch(std::exception const& e) {
        if(reason)*reason=QString::fromUtf8(e.what());
        return false;
    }
}
void MaterialGraphEditor::showDiagnostic(QString const& message) {
    static QRegularExpression const pattern(
        "Material Graph node ([A-Za-z0-9_]{1,48}):");
    auto const match=pattern.match(message);
    if(match.hasMatch()) {
        auto const id=match.captured(1);
        if(auto* item=graphics_.value(id,nullptr)) {
            item->setPen(QPen(QColor("#f16c76"),2.6));
            view_->centerOn(item);
        }
    }
    setMessage(message,true);
}
void MaterialGraphEditor::setMessage(QString const& value,bool failure) {
    message_->setText(value);
    message_->setStyleSheet(failure?"color:#e99595;":"color:#a8c2d4;");
}
QJsonObject MaterialGraphEditor::withPositions() const {
    auto result=document_;
    auto nodes=result.value("nodes").toArray();
    for(int i=0;i<nodes.size();++i) {
        auto node=nodes[i].toObject();
        auto* item=graphics_.value(node.value("id").toString(),nullptr);
        if(item) {
            node["x"]=item->scenePos().x();
            node["y"]=item->scenePos().y();
            nodes.replace(i,node);
        }
    }
    result.insert("nodes",nodes);
    auto groups=result.value("groups").toArray();
    for(int i=0;i<groups.size();++i) {
        auto group=groups[i].toObject();
        if(auto* item=groupGraphics_.value(group.value("id").toString(),nullptr)) {
            group["x"]=item->scenePos().x();
            group["y"]=item->scenePos().y();
            groups.replace(i,group);
        }
    }
    if(result.contains("groups"))result["groups"]=groups;
    return result;
}
void MaterialGraphEditor::commit(QJsonObject const& next) {
    auto previous=withPositions();
    if(next==previous) {
        enabled_=true;return;
    }
    if(undo_.size()>=80)undo_.removeFirst();
    undo_.push_back(previous);
    redo_.clear();
    document_=next;
    enabled_=true;
    draw();
    emit graphChanged();
    setMessage("Graph updated. Validate before saving.");
}
void MaterialGraphEditor::undo() {
    if(undo_.isEmpty())return;
    redo_.push_back(withPositions());
    document_=undo_.takeLast();
    draw();
    emit graphChanged();
    setMessage("Undo graph edit.");
}
void MaterialGraphEditor::redo() {
    if(redo_.isEmpty())return;
    undo_.push_back(withPositions());
    document_=redo_.takeLast();
    draw();
    emit graphChanged();
    setMessage("Redo graph edit.");
}
void MaterialGraphEditor::draw() {
    pins_.clear();graphics_.clear();groupGraphics_.clear();wires_.clear();
    scene_->clear();
    auto addText=[](QGraphicsItem* owner,QString const& text,qreal x,qreal y,
                    QColor color=QColor("#d7e2ea")) {
        auto* label=new QGraphicsSimpleTextItem(text,owner);
        label->setBrush(color);label->setPos(x,y);
    };
    auto addPin=[this](QGraphicsRectItem* box,QString node,QString field,
                       bool isSource,qreal x,qreal y,QString type) {
        auto* circle=new QGraphicsEllipseItem(-6,-6,12,12,box);
        circle->setPos(x,y);
        circle->setZValue(4);
        circle->setPen(QPen(QColor("#dce8ed"),1.3));
        circle->setBrush(typeColor(type));
        circle->setToolTip((isSource?"Output: ":"Input: ")+type+
            (isSource?". Drag to another pin.":
            ". Drop an output here; right-click to disconnect."));
        pins_.push_back({node,field,isSource,circle});
    };
    auto groups=document_.value("groups").toArray();
    for(auto const& raw:groups) {
        auto const desc=raw.toObject();
        QString const id=desc.value("id").toString();
        if(id.isEmpty()||groupGraphics_.contains(id))continue;
        qreal width=std::clamp(desc.value("w").toDouble(360.),180.,1400.);
        qreal height=std::clamp(desc.value("h").toDouble(220.),95.,1100.);
        auto* box=scene_->addRect(0,0,width,height,
            QPen(QColor("#697792"),1.5,Qt::DashLine),
            QBrush(QColor(38,51,74,130)));
        box->setFlag(QGraphicsItem::ItemIsMovable,true);
        box->setFlag(QGraphicsItem::ItemIsSelectable,true);
        box->setZValue(1);
        box->setPos(desc.value("x").toDouble(),desc.value("y").toDouble());
        addText(box,desc.value("title").toString("Comment Group").left(48),12,7,
                QColor("#c4d5ea"));
        groupGraphics_.insert(id,box);
    }
    auto nodes=document_.value("nodes").toArray();
    for(auto const& raw:nodes) {
        auto const item=raw.toObject();
        QString id=item.value("id").toString();
        QString op=item.value("op").toString();
        auto in=nodeInputs(item);
        qreal height=std::max<qreal>(78,54+in.size()*28);
        auto* box=scene_->addRect(0,0,212,height,QPen(QColor("#5e7890"),1.3),
                                   QBrush(QColor("#202b38")));
        box->setFlag(QGraphicsItem::ItemIsMovable,true);
        box->setFlag(QGraphicsItem::ItemIsSelectable,true);
        box->setFlag(QGraphicsItem::ItemSendsGeometryChanges,true);
        box->setZValue(2);
        box->setPos(item.value("x").toDouble(0),item.value("y").toDouble(0));
        box->setToolTip("Double click to edit; Delete removes selected node");
        auto* bar=new QGraphicsRectItem(0,0,212,30,box);
        bar->setPen(Qt::NoPen);
        QColor barColor=op=="texture"?QColor("#406875"):
          op=="parameter"?QColor("#5b587b"):
          op=="color"||op=="scalar"?QColor("#686147"):QColor("#345b68");
        bar->setBrush(barColor);
        addText(box,nodeTitle(item).left(28),9,6);
        addText(box,id,12,height-22,QColor("#91a9bc"));
        addPin(box,id,{},true,212,44,
               op=="function"?nodePinType(item,{}):sourceTypeLabel(item));
        for(int j=0;j<in.size();++j) {
            qreal y=44+j*28;
            QString required=op=="function"?nodePinType(item,in[j]):
               op=="dot"||op=="normalize"?"Vector":
               op=="component"?"Vector":"Numeric";
            addPin(box,id,in[j],false,0,y,required);
            addText(box,in[j].toUpper(),12,y-10,QColor("#adcdd3"));
        }
        graphics_.insert(id,box);
    }
    // Each node belongs to at most one frame; children move with the frame.
    QSet<QString> grouped;
    for(auto const& raw:groups) {
        auto const desc=raw.toObject();
        auto* frame=groupGraphics_.value(desc.value("id").toString(),nullptr);
        if(!frame)continue;
        for(auto const& member:desc.value("members").toArray()) {
            QString const id=member.toString();
            auto* node=graphics_.value(id,nullptr);
            if(!node||grouped.contains(id))continue;
            QPointF absolute=node->scenePos();
            node->setParentItem(frame);
            node->setPos(absolute-frame->scenePos());
            grouped.insert(id);
        }
    }
    // The material output is not itself a shader node and cannot be deleted.
    auto* output=scene_->addRect(0,0,220,220,
        QPen(QColor("#cba55f"),1.7),QBrush(QColor("#302d31")));
    output->setPos(625,80);
    output->setZValue(2);
    auto* bar=new QGraphicsRectItem(0,0,220,37,output);
    bar->setPen(Qt::NoPen);bar->setBrush(QColor("#80653b"));
    addText(output,"Material Output",14,9);
    for(int i=0;i<materialOutputs.size();++i) {
        int y=60+i*30;
        addPin(output,{},materialOutputs[i],false,0,y,
               materialOutputs[i]=="roughness"||materialOutputs[i]=="metallic"||
               materialOutputs[i]=="opacity"?"Scalar":"Vector4");
        addText(output,QString(materialOutputs[i]).replace('_',' ').toUpper(),14,y-11);
    }
    refreshWires();
    if(search_)highlightSearch(search_->text());
}
void MaterialGraphEditor::refreshWires() {
    for(auto* path:wires_){scene_->removeItem(path);delete path;}
    wires_.clear();
    auto wire=[this](QString const& source,QString const& target,QString const& input) {
        QGraphicsEllipseItem *from=nullptr,*to=nullptr;
        for(auto const& p:pins_) {
            if(p.source && p.node==source)from=p.item;
            if(!p.source && p.node==target && p.input==input)to=p.item;
        }
        if(!from || !to)return;
        auto* edge=scene_->addPath(wirePath(from->scenePos(),to->scenePos()),
            QPen(QColor(target.isEmpty()?"#d9b86a":"#77bbcc"),2.2));
        edge->setZValue(0);
        wires_.push_back(edge);
    };
    auto nodes=withPositions().value("nodes").toArray();
    for(auto const& raw:nodes) {
        auto node=raw.toObject();
        auto id=node.value("id").toString();
        for(auto const& input:nodeInputs(node)) {
            auto from=node.value("op").toString()=="function"?
                node.value("inputs").toObject().value(input).toString():
                node.value(input).toString();
            if(!from.isEmpty())wire(from,id,input);
        }
    }
    auto outputs=document_.value("outputs").toObject();
    for(auto const& field:materialOutputs) {
        auto source=outputs.value(field).toString();
        if(!source.isEmpty())wire(source,{},field);
    }
}
int MaterialGraphEditor::pinAt(QPointF position) const {
    for(int i=0;i<pins_.size();++i)
        if(QLineF(position,pins_[i].item->scenePos()).length()<13)return i;
    return -1;
}
bool MaterialGraphEditor::wouldCycle(QString const& source,
                                     QString const& destination) const {
    if(source==destination)return true;
    QHash<QString,QJsonObject> nodes;
    for(auto const& entry:document_.value("nodes").toArray()) {
        auto obj=entry.toObject();nodes.insert(obj.value("id").toString(),obj);
    }
    QSet<QString> visited;
    std::function<bool(QString const&)> traverse=[&](QString const& id)->bool {
        if(id==destination)return true;
        if(visited.contains(id))return false;
        visited.insert(id);
        auto node=nodes.value(id);
        for(auto const& input:nodeInputs(node)) {
            auto child=node.value("op").toString()=="function"?
                node.value("inputs").toObject().value(input).toString():
                node.value(input).toString();
            if(!child.isEmpty() && traverse(child))return true;
        }
        return false;
    };
    return traverse(source);
}
void MaterialGraphEditor::connectPins(int a,int b) {
    if(a<0 || b<0 || a>=pins_.size() || b>=pins_.size())return;
    Pin const source=pins_[a].source?pins_[a]:pins_[b];
    Pin const destination=pins_[a].source?pins_[b]:pins_[a];
    if(!source.source || destination.source)return;
    if(!destination.node.isEmpty() && wouldCycle(source.node,destination.node)) {
        setMessage("Connection rejected: material graph cannot contain a cycle.",true);
        return;
    }
    auto next=withPositions();
    if(destination.node.isEmpty()) {
        auto outputs=next.value("outputs").toObject();
        outputs[destination.input]=source.node;
        next["outputs"]=outputs;
    }else {
        auto nodes=next.value("nodes").toArray();
        for(int i=0;i<nodes.size();++i) {
            auto node=nodes[i].toObject();
            if(node.value("id").toString()!=destination.node)continue;
            if(node.value("op").toString()=="function") {
                auto bound=node.value("inputs").toObject();
                bound[destination.input]=source.node;
                node["inputs"]=bound;
            }else node[destination.input]=source.node;
            nodes.replace(i,node);break;
        }
        next["nodes"]=nodes;
    }
    if(!destination.node.isEmpty()) {
        QJsonArray const nodes=next.value("nodes").toArray();
        int const sourceWidth=estimatedWidth(nodes,source.node);
        QJsonObject target;
        for(auto const& raw:nodes) {
            if(raw.toObject().value("id").toString()==destination.node) {
                target=raw.toObject();break;
            }
        }
        QString const op=target.value("op").toString();
        auto incompatible=[&](QString const& otherInput) {
            int const other=estimatedWidth(nodes,target.value(otherInput).toString());
            return sourceWidth>1 && other>1 && sourceWidth!=other;
        };
        bool bad=false;
        if(op=="function") {
            auto expected=nodePinType(target,destination.input);
            QString actual=sourceWidth==1?"Scalar":
                           sourceWidth==2?"Vector2":
                           sourceWidth==3?"Vector3":
                           sourceWidth==4?"Vector4":"Numeric";
            if(actual=="Numeric") {
                for(auto const& raw:nodes) {
                    auto sourceNode=raw.toObject();
                    if(sourceNode.value("id").toString()==source.node &&
                       sourceNode.value("op").toString()=="function") {
                        actual=nodePinType(sourceNode,{});
                        break;
                    }
                }
            }
            bad=expected!="Numeric" && actual!="Numeric" &&
                actual!=expected;
        }
        if(op=="dot") {
            bad=sourceWidth==1 || incompatible(
                destination.input=="a"?"b":"a");
        }else if(op=="add"||op=="multiply"||op=="lerp") {
            if(destination.input=="a")bad=incompatible("b");
            else if(destination.input=="b")bad=incompatible("a");
            if(op=="lerp" && destination.input=="t") {
                int const base=estimatedWidth(nodes,target.value("a").toString());
                bad=sourceWidth>1 && base>1 && sourceWidth!=base;
            }
        }else if(op=="normalize")bad=sourceWidth==1;
        else if(op=="component") {
            int const channel=QString("xyzw").indexOf(
                target.value("component").toString());
            bad=sourceWidth>0 && channel>=sourceWidth;
        }
        if(bad) {
            setMessage("Connection rejected: pin vector types are incompatible. "
                       "Insert Cast to convert explicitly.",true);
            return;
        }
    }
    commit(next);
}
void MaterialGraphEditor::disconnectPin(int index) {
    if(index<0 || index>=pins_.size() || pins_[index].source)return;
    Pin const p=pins_[index];
    auto next=withPositions();
    if(p.node.isEmpty()) {
        auto outputs=next.value("outputs").toObject();
        outputs.remove(p.input);next["outputs"]=outputs;
    }else {
        auto nodes=next.value("nodes").toArray();
        for(int i=0;i<nodes.size();++i) {
            auto node=nodes[i].toObject();
            if(node.value("id").toString()!=p.node)continue;
            if(node.value("op").toString()=="function") {
                auto bound=node.value("inputs").toObject();
                bound.remove(p.input);
                node["inputs"]=bound;
            }else node.remove(p.input);
            nodes.replace(i,node);break;
        }
        next["nodes"]=nodes;
    }
    commit(next);
}
QString MaterialGraphEditor::nodeAt(QPointF position) const {
    for(auto it=graphics_.cbegin();it!=graphics_.cend();++it)
        if(it.value()->sceneBoundingRect().contains(position))return it.key();
    return {};
}
void MaterialGraphEditor::moveFinished() {
    auto next=withPositions();
    if(next==document_)return;
    if(undo_.size()>=80)undo_.removeFirst();
    undo_.push_back(document_);
    redo_.clear();
    document_=next;
    enabled_=true;
    refreshWires();
    emit graphChanged();
}
void MaterialGraphEditor::addNode(QString const& op,QPointF position) {
    auto next=withPositions();
    auto nodes=next.value("nodes").toArray();
    if(nodes.size()>=64) {setMessage("Node limit: 64 per material.",true);return;}
    QSet<QString> ids;
    for(auto const& raw:nodes)ids.insert(raw.toObject().value("id").toString());
    QString id;
    for(int n=1;n<=999;++n) {
        id="node_"+QString::number(n);
        if(!ids.contains(id))break;
    }
    QJsonObject node{{"id",id},{"op",op},{"x",position.x()},{"y",position.y()}};
    if(op=="parameter")node["parameter"]="base_color";
    if(op=="texture")node["slot"]="base_color";
    if(op=="scalar")node["value"]=0.5;
    if(op=="vector2")node["value"]=QJsonArray{0.5,0.5};
    if(op=="vector3")node["value"]=QJsonArray{0.5,0.5,0.5};
    if(op=="color")node["value"]=QJsonArray{1.0,1.0,1.0,1.0};
    if(op=="component")node["component"]="x";
    if(op=="cast")node["to"]="vector4";
    if(op=="function") {
        auto paths=material_authoring::list_material_functions(functionRoot_);
        if(paths.isEmpty()) {
            setMessage("No Material Function assets in project. "
                       "Create a .material-function.json first.",true);
            return;
        }
        bool selected=false;
        auto reference=QInputDialog::getItem(this,"Add Material Function",
            "Function:",paths,0,false,&selected);
        if(!selected||reference.isEmpty())return;
        auto descriptor=functionDescriptor(reference);
        QStringList outputs;
        for(auto const& raw:descriptor.value("outputs").toArray())
            outputs.append(raw.toObject().value("name").toString());
        if(outputs.isEmpty()) {
            setMessage("Material Function has no valid outputs.",true);
            return;
        }
        auto output=QInputDialog::getItem(this,"Material Function Output",
            "Output:",outputs,0,false,&selected);
        if(!selected)return;
        node["path"]=reference;
        node["output"]=output;
        node["inputs"]=QJsonObject{};
    }
    nodes.append(node);next["nodes"]=nodes;
    commit(next);
}
void MaterialGraphEditor::addAt(QPointF position) {
    addNode(palette_->currentData().toString(),position);
}
void MaterialGraphEditor::deleteSelected() {
    QStringList selection;
    for(auto it=graphics_.cbegin();it!=graphics_.cend();++it)
        if(it.value()->isSelected())selection.push_back(it.key());
    if(selection.isEmpty()) {
        ungroupSelected();
        return;
    }
    auto next=withPositions();
    QJsonArray nodes;
    for(auto const& raw:next.value("nodes").toArray()) {
        auto node=raw.toObject();
        if(selection.contains(node.value("id").toString()))continue;
        for(auto const& input:nodeInputs(node)) {
            if(node.value("op").toString()=="function") {
                auto bound=node.value("inputs").toObject();
                if(selection.contains(bound.value(input).toString()))
                    bound.remove(input);
                node["inputs"]=bound;
            }else if(selection.contains(node.value(input).toString()))
                node.remove(input);
        }
        nodes.push_back(node);
    }
    if(nodes.isEmpty()) {setMessage("Use Reset to restore the initial graph.",true);return;}
    next["nodes"]=nodes;
    auto groups=next.value("groups").toArray();
    for(int i=0;i<groups.size();++i) {
        auto group=groups[i].toObject();
        QJsonArray members;
        for(auto const& raw:group.value("members").toArray())
            if(!selection.contains(raw.toString()))members.append(raw);
        group["members"]=members;
        groups.replace(i,group);
    }
    if(next.contains("groups"))next["groups"]=groups;
    auto outputs=next.value("outputs").toObject();
    for(auto const& key:outputs.keys())
        if(selection.contains(outputs.value(key).toString()))outputs.remove(key);
    next["outputs"]=outputs;
    commit(next);
}
void MaterialGraphEditor::editNode(QString const& id) {
    auto next=withPositions();
    auto nodes=next.value("nodes").toArray();
    for(int i=0;i<nodes.size();++i) {
        auto node=nodes[i].toObject();
        if(node.value("id").toString()!=id)continue;
        QString op=node.value("op").toString();
        bool ok=false;
        if(op=="parameter" || op=="texture") {
            auto choices=op=="parameter"?parameters:textureSlots;
            QString property=op=="parameter"?"parameter":"slot";
            QString value=QInputDialog::getItem(this,"Edit Material Node",
                op=="parameter"?"Parameter":"Texture slot",choices,
                std::max(0,int(choices.indexOf(node.value(property).toString()))),
                false,&ok);
            if(ok)node[property]=value;
        }else if(op=="scalar") {
            double value=QInputDialog::getDouble(this,"Scalar Constant",
                "Value (-64..64)",node.value("value").toDouble(0.5),
                -64,64,5,&ok);
            if(ok)node["value"]=value;
        }else if(op=="function") {
            auto descriptor=functionDescriptor(node.value("path").toString());
            QStringList outputChoices;
            for(auto const& raw:descriptor.value("outputs").toArray())
                outputChoices.append(raw.toObject().value("name").toString());
            if(outputChoices.isEmpty()) {
                setMessage("Material Function missing or invalid.",true);
                return;
            }
            auto selected=QInputDialog::getItem(this,"Material Function Output",
                "Output:",outputChoices,
                std::max(0,int(outputChoices.indexOf(
                    node.value("output").toString()))),false,&ok);
            if(ok)node["output"]=selected;
        }else if(op=="vector2" || op=="vector3") {
            int const length=op=="vector2"?2:3;
            auto old=node.value("value").toArray();
            QStringList original;
            for(auto const& component:old)
                original.push_back(QString::number(component.toDouble(),'g',6));
            QString const entered=QInputDialog::getText(
                this,"Edit "+op,"Components, separated by commas:",
                QLineEdit::Normal,original.join(", "),&ok);
            if(!ok)return;
            auto parts=entered.split(',',Qt::KeepEmptyParts);
            if(parts.size()!=length) {
                setMessage("Expected "+QString::number(length)+
                           " comma-separated components.",true);return;
            }
            QJsonArray values;
            for(auto const& part:parts) {
                bool valid=false;
                double number=part.trimmed().toDouble(&valid);
                if(!valid || !std::isfinite(number) || number<(-64.) ||
                   number>64.) {
                    setMessage("Vector components must be finite in [-64,64].",true);
                    return;
                }
                values.append(number);
            }
            node["value"]=values;
        }else if(op=="component" || op=="cast") {
            QString const key=op=="cast"?"to":"component";
            QStringList const choices=op=="cast"?
                QStringList{"scalar","vector2","vector3","vector4"}:
                QStringList{"x","y","z","w"};
            QString const selected=QInputDialog::getItem(
                this,"Edit "+op,op=="cast"?"Output type":"Source channel",
                choices,std::max(0,int(choices.indexOf(node.value(key).toString()))),
                false,&ok);
            if(ok)node[key]=selected;
        }else if(op=="color") {
            auto old=node.value("value").toArray();
            QColor initial(Qt::white);
            if(old.size()>=3)initial.setRgbF(old[0].toDouble(),old[1].toDouble(),
                                                old[2].toDouble());
            auto value=QColorDialog::getColor(initial,this,"Color Constant",
                                              QColorDialog::ShowAlphaChannel);
            ok=value.isValid();
            if(ok)node["value"]=QJsonArray{value.redF(),value.greenF(),
                                            value.blueF(),value.alphaF()};
        }else {
            setMessage("Connect the pins to edit this operator.");
            return;
        }
        if(!ok)return;
        nodes.replace(i,node);
        next["nodes"]=nodes;
        commit(next);
        return;
    }
}
void MaterialGraphEditor::editAt(QPointF position) {
    auto id=nodeAt(position);
    if(!id.isEmpty())editNode(id);
}
void MaterialGraphEditor::contextMenu(QPointF position,QPoint globalPosition) {
    QMenu menu(this);
    int pin=pinAt(position);
    QString id=nodeAt(position);
    if(pin>=0 && !pins_[pin].source) {
        auto* disconnect=menu.addAction("Disconnect Input");
        if(menu.exec(globalPosition)==disconnect)disconnectPin(pin);
        return;
    }
    if(!id.isEmpty()) {
        auto* edit=menu.addAction("Edit Node...");
        auto* remove=menu.addAction("Delete Node");
        auto* selected=menu.exec(globalPosition);
        if(selected==edit)editNode(id);
        if(selected==remove) {
            for(auto* item:graphics_)item->setSelected(false);
            if(auto* item=graphics_.value(id,nullptr))item->setSelected(true);
            deleteSelected();
        }
        return;
    }
    QString const group=groupAt(position);
    if(!group.isEmpty()) {
        auto* rename=menu.addAction("Rename Comment Group...");
        auto* ungroup=menu.addAction("Ungroup Nodes");
        auto* selected=menu.exec(globalPosition);
        if(selected==rename)renameCommentGroup(group);
        if(selected==ungroup) {
            for(auto* item:groupGraphics_)item->setSelected(false);
            if(auto* item=groupGraphics_.value(group,nullptr))item->setSelected(true);
            ungroupSelected();
        }
        return;
    }
    auto* comment=menu.addAction("Create Comment Group");
    menu.addSeparator();
    for(auto const& op:operations) {
        auto* action=menu.addAction("Add "+op);
        action->setData(op);
    }
    auto* selected=menu.exec(globalPosition);
    if(selected==comment)createCommentGroup();
    else if(selected)addNode(selected->data().toString(),position);
}
} // namespace northstarcpp::editor
