// Native Qt Material Graph surface for the first-class NorthStar material compiler.
#pragma once
#include <QJsonObject>
#include <QList>
#include <QHash>
#include <QPointF>
#include <QWidget>
#include <filesystem>

class QComboBox;
class QLineEdit;
class QLabel;
class QGraphicsScene;
class QGraphicsRectItem;
class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QGraphicsView;

namespace northstarcpp::editor {
class MaterialGraphEditor final : public QWidget {
    Q_OBJECT
public:
    explicit MaterialGraphEditor(QJsonObject const& graph={},QWidget* parent=nullptr,
                                 std::filesystem::path functionRoot={});
    QJsonObject graph() const;
    bool enabled() const noexcept {return enabled_;}
    bool validate(QString* reason=nullptr) const;
    void showDiagnostic(QString const& message);
signals:
    void graphChanged();
public:

    // Used by the native graph canvas. A connection is a source node output
    // into a typed input pin or a material output slot.
    int pinAt(QPointF scenePosition) const;
    void connectPins(int first,int second);
    void disconnectPin(int index);
    void moveFinished();
    void refreshWires();
    void addAt(QPointF position);
    void contextMenu(QPointF position,QPoint globalPosition);
    void editAt(QPointF position);
    void copySelection();
    void pasteSelection();
    void createCommentGroup();
    void ungroupSelected();
    void renameCommentGroup(QString const& id);
private:
    struct Pin {
        QString node;
        QString input;
        bool source=false;
        QGraphicsEllipseItem* item=nullptr;
    };
    QJsonObject document_;
    std::filesystem::path functionRoot_;
    QJsonObject functionDescriptor(QString const& reference) const;
    QStringList nodeInputs(QJsonObject const& node) const;
    QString nodePinType(QJsonObject const& node,QString const& input) const;
    bool enabled_=false;
    QList<QJsonObject> undo_,redo_;
    QGraphicsScene* scene_{};
    QGraphicsView* view_{};
    QComboBox* palette_{};
    QLineEdit* search_{};
    QLabel* message_{};
    QList<Pin> pins_;
    QHash<QString,QGraphicsRectItem*> graphics_;
    QHash<QString,QGraphicsRectItem*> groupGraphics_;
    QList<QGraphicsPathItem*> wires_;
    void draw();
    void commit(QJsonObject const& next);
    void undo();
    void redo();
    void addNode(QString const& op,QPointF position);
    void deleteSelected();
    void editNode(QString const& id);
    QString nodeAt(QPointF position) const;
    QString groupAt(QPointF position) const;
    QJsonObject withPositions() const;
    bool wouldCycle(QString const& source,QString const& destination) const;
    void setMessage(QString const& value,bool failure=false);
    void highlightSearch(QString const& phrase);
};
} // namespace northstarcpp::editor
