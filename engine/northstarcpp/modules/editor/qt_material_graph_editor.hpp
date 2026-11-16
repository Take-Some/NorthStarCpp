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
class QFormLayout;
class QScrollArea;
class QSplitter;
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
    ~MaterialGraphEditor() override;
    QJsonObject graph() const;
    void setGraph(QJsonObject const& graph);
    // Function mode reuses exactly the Material Graph canvas, including
    // undo/redo, typed wires, grouping, zoom and clipboard.
    void setFunctionDocument(QJsonObject const& function,std::string reference);
    QJsonObject functionDocument() const;
    bool isFunctionMode() const noexcept {return functionMode_;}
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
    bool functionMode_=false;
    std::string functionReference_;
    QJsonObject functionDefinition_;
    QStringList outputNames() const;
    QString outputType(QString const& output) const;
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
    QWidget* detailsContent_{};
    QFormLayout* detailsForm_{};
    QLabel* detailsHint_{};
    QLabel* stats_{};
    bool snapEnabled_=true;
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
    void updateNodeDetails();
    void applyNodeDetail(QString const& id,QString const& field,
                         QJsonValue const& value);
    void focusSelection();
    void focusAll();
};
} // namespace northstarcpp::editor
