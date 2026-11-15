// Clipboard and search affordances for the NorthStar material node graph.
#include "qt_material_graph_editor.hpp"
#include <QApplication>
#include <QClipboard>
#include <QGraphicsRectItem>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace northstarcpp::editor {
namespace {
constexpr auto clipboardType="application/x-northstarcpp-material-graph+json";
QStringList inputsFor(QString const& op) {
    if(op=="add"||op=="multiply"||op=="dot")return {"a","b"};
    if(op=="lerp")return {"a","b","t"};
    if(op=="one_minus"||op=="saturate"||op=="normalize"||
       op=="component"||op=="cast")return {"a"};
    return {};
}
bool supported(QString const& op) {
    return QStringList{"scalar","vector2","vector3","color",
        "parameter","texture","add","multiply","lerp","dot",
        "one_minus","saturate","normalize","component","cast","function"}.contains(op);
}
}
void MaterialGraphEditor::highlightSearch(QString const& phrase) {
    QString const term=phrase.trimmed();
    QHash<QString,QString> names;
    for(auto const& raw:document_.value("nodes").toArray()) {
        auto const node=raw.toObject();
        auto title=node.value("op").toString()+" "+
            node.value("parameter").toString()+" "+
            node.value("slot").toString();
        names.insert(node.value("id").toString(),title);
    }
    QGraphicsRectItem* first=nullptr;
    for(auto it=graphics_.cbegin();it!=graphics_.cend();++it) {
        bool const hit=term.isEmpty()||
            it.key().contains(term,Qt::CaseInsensitive)||
            names.value(it.key()).contains(term,Qt::CaseInsensitive);
        it.value()->setOpacity(hit?1.:.3);
        if(hit&&!first)first=it.value();
    }
    if(!term.isEmpty()&&first)view_->centerOn(first);
}
void MaterialGraphEditor::copySelection() {
    QSet<QString> selected;
    for(auto it=graphics_.cbegin();it!=graphics_.cend();++it)
        if(it.value()->isSelected())selected.insert(it.key());
    if(selected.isEmpty()) {
        setMessage("Select nodes before copying.");
        return;
    }
    QJsonArray nodes;
    for(auto const& raw:withPositions().value("nodes").toArray()) {
        auto node=raw.toObject();
        if(!selected.contains(node.value("id").toString()))continue;
        if(node.value("op").toString()=="function") {
            auto bound=node.value("inputs").toObject();
            for(auto const& key:bound.keys())
                if(!selected.contains(bound.value(key).toString()))
                    bound.remove(key);
            node["inputs"]=bound;
        }else for(auto const& input:inputsFor(node.value("op").toString())) {
            if(!selected.contains(node.value(input).toString()))
                node.remove(input);
        }
        nodes.append(node);
    }
    QByteArray serialized=QJsonDocument(
        QJsonObject{{"schema",1},{"nodes",nodes}})
        .toJson(QJsonDocument::Compact);
    if(serialized.size()>32768) {
        setMessage("Copy rejected: 32 KiB clipboard limit.",true);
        return;
    }
    auto* mime=new QMimeData;
    mime->setData(clipboardType,serialized);
    QApplication::clipboard()->setMimeData(mime);
    setMessage(QString::number(nodes.size())+" node(s) copied.");
}
void MaterialGraphEditor::pasteSelection() {
    auto const* mime=QApplication::clipboard()->mimeData();
    if(!mime||!mime->hasFormat(clipboardType)) {
        setMessage("Clipboard does not contain material nodes.");
        return;
    }
    QByteArray serialized=mime->data(clipboardType);
    if(serialized.isEmpty()||serialized.size()>32768) {
        setMessage("Paste rejected: invalid clipboard size.",true);
        return;
    }
    QJsonParseError error;
    auto doc=QJsonDocument::fromJson(serialized,&error);
    if(error.error!=QJsonParseError::NoError || !doc.isObject() ||
       doc.object().value("schema").toInt(-1)!=1) {
        setMessage("Paste rejected: invalid node clipboard.",true);
        return;
    }
    auto pasted=doc.object().value("nodes").toArray();
    auto next=withPositions();
    auto nodes=next.value("nodes").toArray();
    if(pasted.isEmpty()||nodes.size()+pasted.size()>64) {
        setMessage("Paste rejected: maximum 64 nodes.",true);
        return;
    }
    QSet<QString> existing;
    for(auto const& raw:nodes)
        existing.insert(raw.toObject().value("id").toString());
    QHash<QString,QString> remap;
    QRegularExpression const idPattern("^[a-zA-Z0-9_]{1,48}$");
    unsigned counter=0;
    for(auto const& raw:pasted) {
        if(!raw.isObject()) {
            setMessage("Paste rejected: invalid node.",true);return;
        }
        auto const node=raw.toObject();
        QString const oldId=node.value("id").toString();
        if(!idPattern.match(oldId).hasMatch() || remap.contains(oldId) ||
           !supported(node.value("op").toString())) {
            setMessage("Paste rejected: unsafe or unsupported node.",true);
            return;
        }
        QString newId;
        do {newId="node_"+QString::number(++counter);}
        while(existing.contains(newId));
        existing.insert(newId);
        remap.insert(oldId,newId);
    }
    for(auto const& raw:pasted) {
        auto node=raw.toObject();
        node["id"]=remap.value(node.value("id").toString());
        node["x"]=std::clamp(node.value("x").toDouble(),-9000.,9000.)+48.;
        node["y"]=std::clamp(node.value("y").toDouble(),-9000.,9000.)+48.;
        if(node.value("op").toString()=="function") {
            auto bound=node.value("inputs").toObject();
            for(auto const& key:bound.keys()) {
                auto old=bound.value(key).toString();
                if(remap.contains(old))bound[key]=remap.value(old);
                else bound.remove(key);
            }
            node["inputs"]=bound;
        }else for(auto const& input:inputsFor(node.value("op").toString())) {
            auto old=node.value(input).toString();
            if(remap.contains(old))node[input]=remap.value(old);
            else node.remove(input);
        }
        nodes.append(node);
    }
    next["nodes"]=nodes;
    if(QJsonDocument(next).toJson(QJsonDocument::Compact).size()>32768) {
        setMessage("Paste rejected: graph JSON exceeds 32 KiB.",true);
        return;
    }
    commit(next);
    for(auto const& newId:remap) {
        if(auto* box=graphics_.value(newId,nullptr))
            box->setSelected(true);
    }
    setMessage(QString::number(pasted.size())+" node(s) pasted.");
}
} // namespace northstarcpp::editor
