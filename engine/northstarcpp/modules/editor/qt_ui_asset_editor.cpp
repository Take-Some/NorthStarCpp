#include "qt_ui_asset_editor.hpp"
#include "qt_native_ui_preview.hpp"
#include "qt_nui_xml_codec.hpp"
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTreeWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFormLayout>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QSignalBlocker>
#include <QShortcut>
#include <QCheckBox>
#include <QKeySequence>
#include <QRegularExpression>
#include <QHash>
#include <QVector>
#include <cmath>
#include <stdexcept>

namespace northstarcpp::editor {
namespace {
bool validName(QString const& value) {
    static QRegularExpression const pattern("^[A-Za-z][A-Za-z0-9_.-]{0,63}$");
    return pattern.match(value).hasMatch();
}
QString printValue(QJsonValue const& value) {
    if(value.isObject() && value.toObject().value("token").isString())
        return "$token:"+value.toObject().value("token").toString();
    if(value.isBool())return value.toBool()?"true":"false";
    if(value.isDouble())return QString::number(value.toDouble(),'g',16);
    return value.toString();
}
class UiAssetDialog final : public QDialog {
public:
    UiAssetDialog(QWidget* owner,QString path,bool shared)
        : QDialog(owner),path_(std::move(path)),shared_(shared) {
        QFile file(path_);
        if(!file.open(QIODevice::ReadOnly) || file.size()<1 ||
           file.size()>262144)throw std::runtime_error("Cannot read bounded .nui asset");
        doc_=nui_qt::parse(file.readAll());
        if(doc_.value("schema").toString()!="northstar.ui/2" ||
           !QStringList{"screen","world"}.contains(doc_.value("target").toString()) ||
           !doc_.value("nodes").isArray())
            throw std::runtime_error("Unsupported UI document schema");
        nodes_=doc_.value("nodes").toArray();
        if(nodes_.size()>256)throw std::runtime_error("UI node limit");
        setObjectName("northstarUiAssetEditor");
        setWindowTitle(QFileInfo(path_).fileName()+" - UI Designer"+
                       (shared_?" [Shared / Read-only]":""));
        setWindowModality(Qt::ApplicationModal);
        resize(1260,790);
        auto* outer=new QVBoxLayout(this);
        auto* split=new QSplitter(Qt::Horizontal,this);
        auto* left=new QWidget(split);
        auto* column=new QVBoxLayout(left);
        column->addWidget(new QLabel("Hierarchy",left));
        tree_=new QTreeWidget(left);
        tree_->setObjectName("uiAssetHierarchy");
        tree_->setHeaderLabels({"Element","Type"});
        tree_->header()->setSectionResizeMode(0,QHeaderView::Stretch);
        column->addWidget(tree_,2);
        auto* row=new QHBoxLayout;
        auto* add=new QPushButton("+ Add",left);
        auto* remove=new QPushButton("Remove",left);
        row->addWidget(add);row->addWidget(remove);
        column->addLayout(row);
        auto* form=new QFormLayout;
        id_=new QLineEdit(left);
        type_=new QComboBox(left);
        type_->addItems({"panel","text","button","image","input"});
        form->addRow("ID",id_);form->addRow("Widget",type_);
        parent_=new QComboBox(left);
        parent_->setObjectName("uiAssetParent");
        style_=new QComboBox(left);
        style_->setObjectName("uiAssetStyle");
        form->addRow("Parent",parent_);
        form->addRow("Style",style_);
        column->addLayout(form);
        auto* designSystem=new QPushButton("Design System / Styles...",left);
        column->addWidget(designSystem);
        connect(designSystem,&QPushButton::clicked,this,
                [this]{editDesignSystem();});
        designSystem->setEnabled(!shared_);
        auto* sourceEditor=new QPushButton("Edit XML Source...",left);
        sourceEditor->setObjectName("uiAssetXmlSource");
        column->addWidget(sourceEditor);
        connect(sourceEditor,&QPushButton::clicked,this,[this]{editXmlSource();});
        sourceEditor->setEnabled(!shared_);
        column->addWidget(new QLabel("Properties",left));
        props_=new QTableWidget(left);
        props_->setColumnCount(3);
        props_->setHorizontalHeaderLabels({"Key","Type","Value"});
        props_->setColumnWidth(1,88);
        props_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
        props_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        props_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        column->addWidget(props_,3);
        auto* propertyRow=new QHBoxLayout;
        auto* addProp=new QPushButton("+ Property",left);
        auto* delProp=new QPushButton("- Property",left);
        propertyRow->addWidget(addProp);propertyRow->addWidget(delProp);
        column->addLayout(propertyRow);
        auto* right=new QWidget(split);
        auto* rightColumn=new QVBoxLayout(right);
        auto* canvasToolbar=new QHBoxLayout;
        canvasToolbar->addWidget(new QLabel("Canvas — drag elements",right));
        canvasToolbar->addStretch(1);
        snap_=new QCheckBox("Snap 8 px",right);
        snap_->setChecked(true);
        canvasToolbar->addWidget(snap_);
        rightColumn->addLayout(canvasToolbar);
        preview_=new NativeUIPreview(right);
        preview_->setObjectName("uiAssetLivePreview");
        preview_->onElementClicked([this](QString const& id) {
            const auto matches=tree_->findItems(id,Qt::MatchExactly|Qt::MatchRecursive,0);
            if(!matches.empty())tree_->setCurrentItem(matches.front());
        });
        preview_->onElementDragged([this](QString const& id,QPointF distance) {
            if(shared_)return;
            for(int i=0;i<nodes_.size();++i) {
                auto node=nodes_[i].toObject();
                if(node.value("id").toString()!=id)continue;
                auto props=node.value("props").toObject();
                // Never silently replace a binding to a design token.
                if(props.value("x").isObject() || props.value("y").isObject()) {
                    status_->setText("Token-bound position cannot be dragged");
                    return;
                }
                const double factor=preview_->devicePixelRatioF();
                const double snap=snap_->isChecked()?8.:1.;
                auto next=[&](double old,double delta) {
                    return std::round((old+delta*factor)/snap)*snap;
                };
                props["x"]=next(props.value("x").toDouble(),distance.x());
                props["y"]=next(props.value("y").toDouble(),distance.y());
                node["props"]=props;nodes_.replace(i,node);
                refresh();redrawTree(i);
                return;
            }
        });
        rightColumn->addWidget(preview_,1);
        status_=new QLabel(right);rightColumn->addWidget(status_);
        split->addWidget(left);split->addWidget(right);
        split->setSizes({390,850});
        outer->addWidget(split,1);
        auto* actions=new QDialogButtonBox(
            QDialogButtonBox::Save|QDialogButtonBox::Close,this);
        auto* footer=new QHBoxLayout;
        undoButton_=new QPushButton("Undo",this);
        redoButton_=new QPushButton("Redo",this);
        footer->addWidget(undoButton_);
        footer->addWidget(redoButton_);
        footer->addStretch(1);
        footer->addWidget(actions);
        outer->addLayout(footer);
        connect(undoButton_,&QPushButton::clicked,this,[this]{undo();});
        connect(redoButton_,&QPushButton::clicked,this,[this]{redo();});
        auto* undoKey=new QShortcut(QKeySequence::Undo,this);
        auto* redoKey=new QShortcut(QKeySequence::Redo,this);
        connect(undoKey,&QShortcut::activated,this,[this]{undo();});
        connect(redoKey,&QShortcut::activated,this,[this]{redo();});
        connect(actions->button(QDialogButtonBox::Close),&QPushButton::clicked,
                this,&QDialog::reject);
        connect(actions->button(QDialogButtonBox::Save),&QPushButton::clicked,
                this,[this]{save();});
        connect(tree_,&QTreeWidget::currentItemChanged,this,
                [this]{select();});
        connect(add,&QPushButton::clicked,this,[this]{addNode();});
        connect(remove,&QPushButton::clicked,this,[this]{removeNode();});
        connect(addProp,&QPushButton::clicked,this,[this]{addProperty();});
        connect(delProp,&QPushButton::clicked,this,[this]{removeProperty();});
        connect(id_,&QLineEdit::editingFinished,this,[this]{renameNode();});
        connect(type_,&QComboBox::currentTextChanged,this,
                [this](QString const& kind){
            if(sync_)return;
            int i=selected();if(i<0)return;
            auto node=nodes_[i].toObject();
            node["type"]=kind;nodes_.replace(i,node);
            refresh();
        });
        connect(parent_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,
                [this](int){changeParent();});
        connect(style_,&QComboBox::currentTextChanged,this,
                [this](QString const&){changeStyle();});
        connect(props_,&QTableWidget::itemChanged,this,
                [this]{updateProperties();});
        for(auto* button:{add,remove,addProp,delProp,
                          actions->button(QDialogButtonBox::Save)})
            button->setEnabled(!shared_);
        saved_=doc_;
        latest_=doc_;
        updateHistoryButtons();
        redrawTree();
        refresh(false);
    }
protected:
    void reject() override {
        if(dirty_ && QMessageBox::question(this,"Unsaved UI",
            "Discard changes?",QMessageBox::Discard|QMessageBox::Cancel,
            QMessageBox::Cancel)!=QMessageBox::Discard)return;
        QDialog::reject();
    }
private:
    int selected() const {
        auto* item=tree_->currentItem();
        if(!item)return -1;
        auto i=item->data(0,Qt::UserRole).toInt();
        return i>=0 && i<nodes_.size()?i:-1;
    }
    void redrawTree(int focus=0) {
        QSignalBlocker block(tree_);
        tree_->clear();
        QHash<QString,QTreeWidgetItem*> items;
        QTreeWidgetItem* selectedItem=nullptr;
        for(int i=0;i<nodes_.size();++i) {
            auto node=nodes_[i].toObject();
            QString parent=node.value("parent").toString();
            auto* item=items.contains(parent)?
                new QTreeWidgetItem(items.value(parent)):
                new QTreeWidgetItem(tree_);
            item->setText(0,node.value("id").toString());
            item->setText(1,node.value("type").toString());
            item->setData(0,Qt::UserRole,i);
            items.insert(node.value("id").toString(),item);
            if(i==focus)selectedItem=item;
        }
        tree_->expandAll();
        if(selectedItem)tree_->setCurrentItem(selectedItem);
        select();
    }
    void select() {
        sync_=true;
        int i=selected();
        QJsonObject node=i>=0?nodes_[i].toObject():QJsonObject{};
        id_->setText(node.value("id").toString());
        type_->setCurrentText(node.value("type").toString());
        parent_->clear();
        parent_->addItem("<document root>",QString{});
        for(int n=0;n<i;++n) {
            auto parentNode=nodes_[n].toObject();
            auto name=parentNode.value("id").toString();
            parent_->addItem(name,name);
        }
        auto parentIdx=parent_->findData(node.value("parent").toString());
        parent_->setCurrentIndex(parentIdx<0?0:parentIdx);
        style_->clear();
        style_->addItem("<no style>",QString{});
        const auto styles=doc_.value("styles").toObject();
        for(const auto& name:styles.keys())style_->addItem(name,name);
        const auto styleIdx=style_->findData(node.value("style").toString());
        style_->setCurrentIndex(styleIdx<0?0:styleIdx);
        props_->setRowCount(0);
        QJsonObject properties=node.value("props").toObject();
        for(auto it=properties.begin();it!=properties.end();++it) {
            int row=props_->rowCount();props_->insertRow(row);
            props_->setItem(row,0,new QTableWidgetItem(it.key()));
            props_->setItem(row,1,new QTableWidgetItem(nui_qt::valueType(it.value())));
            props_->setItem(row,2,new QTableWidgetItem(printValue(it.value())));
        }
        id_->setEnabled(!shared_ && i>=0);
        type_->setEnabled(!shared_ && i>=0);
        parent_->setEnabled(!shared_ && i>=0);
        style_->setEnabled(!shared_ && i>=0 &&
                          doc_.value("schema").toString()=="northstar.ui/2");
        props_->setEnabled(!shared_ && i>=0);
        sync_=false;
    }
    void updateHistoryButtons() {
        undoButton_->setEnabled(!shared_ && !undo_.isEmpty());
        redoButton_->setEnabled(!shared_ && !redo_.isEmpty());
    }
    void refresh(bool changed=true) {
        doc_["nodes"]=nodes_;
        if(changed && doc_!=latest_) {
            undo_.append(latest_);
            if(undo_.size()>100)undo_.removeFirst();
            redo_.clear();
            latest_=doc_;
        }
        dirty_=doc_!=saved_;
        updateHistoryButtons();
        try {
            preview_->showAsset(doc_);
            status_->setText(QString("%1 nodes | %2 | %3")
                .arg(nodes_.size()).arg(doc_.value("target").toString())
                .arg(dirty_?"Unsaved":"Saved"));
            status_->setToolTip(QString{});
        }catch(std::exception const& e) {
            status_->setText(QString("Validation: %1").arg(e.what()));
            status_->setToolTip("Invalid documents cannot be saved");
        }
    }
    void restore(QJsonObject const& state) {
        doc_=state;
        nodes_=doc_.value("nodes").toArray();
        latest_=doc_;
        redrawTree();
        refresh(false);
    }
    void undo() {
        if(shared_ || undo_.isEmpty())return;
        redo_.append(doc_);
        auto last=undo_.takeLast();
        restore(last);
    }
    void redo() {
        if(shared_ || redo_.isEmpty())return;
        undo_.append(doc_);
        auto next=redo_.takeLast();
        restore(next);
    }
    void changeParent() {
        if(sync_)return;
        auto index=selected();if(index<0)return;
        auto node=nodes_[index].toObject();
        auto parent=parent_->currentData().toString();
        if(parent==node.value("parent").toString())return;
        node["parent"]=parent;
        nodes_.replace(index,node);
        refresh();
        redrawTree(index);
    }
    void changeStyle() {
        if(sync_ || doc_.value("schema").toString()!="northstar.ui/2")return;
        auto index=selected();if(index<0)return;
        auto node=nodes_[index].toObject();
        auto style=style_->currentData().toString();
        if(style==node.value("style").toString())return;
        if(style.isEmpty())node.remove("style");
        else node["style"]=style;
        nodes_.replace(index,node);
        refresh();
    }
    void editXmlSource() {
        if(shared_)return;
        QDialog dialog(this);
        dialog.setWindowTitle("NorthStar UI XML Source");
        dialog.resize(960,710);
        auto* layout=new QVBoxLayout(&dialog);
        auto* editor=new QPlainTextEdit(&dialog);
        editor->setObjectName("uiAssetXmlCodeEditor");
        try{editor->setPlainText(QString::fromUtf8(nui_qt::serialize(doc_)));}
        catch(std::exception const& error) {
            QMessageBox::warning(this,"UI XML",error.what());return;
        }
        layout->addWidget(editor,1);
        auto* controls=new QDialogButtonBox(QDialogButtonBox::Save|
            QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(controls);
        connect(controls->button(QDialogButtonBox::Cancel),&QPushButton::clicked,
                &dialog,&QDialog::reject);
        connect(controls->button(QDialogButtonBox::Save),&QPushButton::clicked,
                &dialog,[&] {
                    try {
                        auto updated=nui_qt::parse(editor->toPlainText().toUtf8());
                        preview_->showAsset(updated);
                        doc_=updated;
                        nodes_=doc_.value("nodes").toArray();
                        refresh();
                        redrawTree();
                        dialog.accept();
                    }catch(std::exception const& error){
                        QMessageBox::warning(&dialog,"Invalid UI XML",error.what());
                    }
                });
        dialog.exec();
    }
    void editDesignSystem() {
        if(shared_)return;
        // Preview and persisted .nui use the same canonical XML schema.
        QJsonObject system{{"schema","northstar.ui/2"},{"target","screen"},
            {"nodes",QJsonArray{}},{"tokens",doc_.value("tokens")},
            {"styles",doc_.value("styles")}};
        QString initial;
        try {initial=QString::fromUtf8(nui_qt::serialize(system));}
        catch(std::exception const& e) {
            QMessageBox::warning(this,"Design System XML",e.what());
            return;
        }
        bool accepted=false;
        auto xml=QInputDialog::getMultiLineText(this,
            "NorthStar UI Design System — XML",
            "Edit Tokens and Styles. Keep Elements empty.",
            initial,&accepted);
        if(!accepted)return;
        QJsonObject candidate;
        try {
            candidate=nui_qt::parse(xml.toUtf8());
            if(!candidate.value("nodes").toArray().isEmpty())
                throw std::invalid_argument("Design System cannot contain widgets");
        }catch(std::exception const& e){
            QMessageBox::warning(this,"Design System XML",e.what());return;
        }
        auto old=doc_;
        doc_["tokens"]=candidate.value("tokens");
        doc_["styles"]=candidate.value("styles");
        try {preview_->showAsset(doc_);}
        catch(std::exception const& e){
            doc_=old;
            QMessageBox::warning(this,"Design System XML",e.what());
            return;
        }
        refresh();
        select();
    }
    void addNode() {
        bool ok=false;
        auto kind=QInputDialog::getItem(this,"Add Element","Widget:",
            {"panel","text","button","image","input"},0,false,&ok);
        if(!ok || nodes_.size()>=256)return;
        QString parent;
        int idx=selected();
        if(idx>=0)parent=nodes_[idx].toObject().value("id").toString();
        QString id;
        for(int n=1;n<=256;++n) {
            QString candidate=kind+QString::number(n);
            bool found=false;
            for(auto const& node:nodes_)
                found|=node.toObject().value("id")==candidate;
            if(!found){id=candidate;break;}
        }
        if(id.isEmpty())return;
        QJsonObject props{{"width",220},{"height",48}};
        if(kind=="panel")props["background"]="#172536ee";
        if(kind=="button" || kind=="text")props["text"]="New "+kind;
        if(kind=="image")props["source"]="textures/ui.ytd@icon";
        nodes_.append(QJsonObject{{"id",id},{"parent",parent},
                                  {"type",kind},{"props",props}});
        refresh();redrawTree(nodes_.size()-1);
    }
    void removeNode() {
        int idx=selected();if(idx<0)return;
        QStringList doomed{nodes_[idx].toObject().value("id").toString()};
        for(bool again=true;again;) {
            again=false;
            for(auto const& node:nodes_) {
                auto data=node.toObject();
                auto id=data.value("id").toString();
                if(doomed.contains(data.value("parent").toString()) &&
                   !doomed.contains(id)){doomed.append(id);again=true;}
            }
        }
        QJsonArray keep;
        for(auto const& item:nodes_)
            if(!doomed.contains(item.toObject().value("id").toString()))
                keep.append(item);
        nodes_=keep;refresh();redrawTree(qMax(0,idx-1));
    }
    void renameNode() {
        if(sync_)return;
        int idx=selected();if(idx<0)return;
        auto node=nodes_[idx].toObject();
        auto before=node.value("id").toString();
        auto after=id_->text().trimmed();
        if(before==after)return;
        bool duplicate=false;
        for(int i=0;i<nodes_.size();++i)
            duplicate|=(i!=idx && nodes_[i].toObject().value("id")==after);
        if(!validName(after)||duplicate) {
            QMessageBox::warning(this,"UI ID","ID must be unique (letters, digits, . _ -).");
            id_->setText(before);return;
        }
        node["id"]=after;nodes_.replace(idx,node);
        for(int i=0;i<nodes_.size();++i){
            auto child=nodes_[i].toObject();
            if(child.value("parent")==before) {
                child["parent"]=after;nodes_.replace(i,child);
            }
        }
        refresh();redrawTree(idx);
    }
    void updateProperties() {
        if(sync_)return;
        int idx=selected();if(idx<0)return;
        QJsonObject properties;
        for(int r=0;r<props_->rowCount();++r) {
            auto* name=props_->item(r,0);
            auto* kind=props_->item(r,1);
            auto* value=props_->item(r,2);
            if(!name||!kind||!value)continue;
            auto key=name->text().trimmed();
            if(!validName(key)||properties.contains(key)||properties.size()>=48){
                status_->setText("Invalid or duplicate property: "+key);return;
            }
            try{properties[key]=nui_qt::typedEdit(kind->text(),value->text());}
            catch(std::exception const& error){
                status_->setText(QString("Invalid %1: %2").arg(key,error.what()));return;
            }
        }
        auto node=nodes_[idx].toObject();
        node["props"]=properties;nodes_.replace(idx,node);
        refresh();
    }
    void addProperty() {
        int idx=selected();if(idx<0)return;
        bool ok=false;
        auto key=QInputDialog::getText(this,"Property","Name:",
            QLineEdit::Normal,"color",&ok).trimmed();
        if(!ok || !validName(key))return;
        auto node=nodes_[idx].toObject();
        auto props=node.value("props").toObject();
        if(props.contains(key)||props.size()>=48)return;
        props[key]="";node["props"]=props;nodes_.replace(idx,node);
        refresh();select();
    }
    void removeProperty() {
        int idx=selected(),row=props_->currentRow();
        if(idx<0||row<0||!props_->item(row,0))return;
        auto node=nodes_[idx].toObject();
        auto props=node.value("props").toObject();
        props.remove(props_->item(row,0)->text());
        node["props"]=props;nodes_.replace(idx,node);
        refresh();select();
    }
    void save() {
        if(shared_)return;
        try {
            preview_->showAsset(doc_);
            auto bytes=nui_qt::serialize(doc_);
            if(bytes.size()>262144)throw std::runtime_error("UI exceeds 256 KiB");
            QSaveFile file(path_);
            if(!file.open(QIODevice::WriteOnly) ||
               file.write(bytes)!=bytes.size() || !file.commit())
                throw std::runtime_error("Failed to save UI asset");
            saved_=doc_;dirty_=false;refresh(false);
        }catch(std::exception const& e) {
            QMessageBox::warning(this,"UI Asset",e.what());
        }
    }
    QString path_;
    bool shared_=false,dirty_=false,sync_=false;
    QJsonObject doc_,saved_,latest_;
    QVector<QJsonObject> undo_,redo_;
    QJsonArray nodes_;
    QTreeWidget* tree_{};
    QTableWidget* props_{};
    QLineEdit* id_{};
    QComboBox* type_{};
    QComboBox* parent_{};
    QComboBox* style_{};
    QCheckBox* snap_{};
    QPushButton* undoButton_{};
    QPushButton* redoButton_{};
    QLabel* status_{};
    NativeUIPreview* preview_{};
};
} // namespace
bool editUiAsset(QWidget* parent,QString const& path,bool sharedAsset) {
    try {
        UiAssetDialog dialog(parent,path,sharedAsset);
        dialog.exec();
        return true;
    }catch(std::exception const& e) {
        QMessageBox::warning(parent,"NorthStar UI Asset",e.what());
        return false;
    }
}
} // namespace northstarcpp::editor
