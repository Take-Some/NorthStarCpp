#include "qt_material_graph_editor.hpp"
#include <QGraphicsRectItem>
#include <QGraphicsView>
#include <QInputDialog>
#include <QLineEdit>
#include <QJsonArray>
#include <QSet>
#include <QRectF>
#include <algorithm>
namespace northstarcpp::editor {
QString MaterialGraphEditor::groupAt(QPointF p) const {
    for(auto it=groupGraphics_.cbegin();it!=groupGraphics_.cend();++it)
        if(it.value()->sceneBoundingRect().contains(p))return it.key();
    return {};
}
void MaterialGraphEditor::createCommentGroup() {
    auto next=withPositions();
    auto groups=next.value("groups").toArray();
    if(groups.size()>=16){setMessage("Maximum 16 groups.",true);return;}
    QSet<QString> ids,selected;
    for(auto const& raw:groups)ids.insert(raw.toObject().value("id").toString());
    QString id;
    for(unsigned n=1;n<999;++n){
        id="group_"+QString::number(n);
        if(!ids.contains(id))break;
    }
    if(ids.contains(id))return;
    QRectF rect;
    bool any=false;
    for(auto it=graphics_.cbegin();it!=graphics_.cend();++it) {
        if(!it.value()->isSelected())continue;
        selected.insert(it.key());
        auto const bounds=it.value()->sceneBoundingRect();
        rect=any?rect.united(bounds):bounds;
        any=true;
    }
    QJsonArray members;
    for(auto const& node:next.value("nodes").toArray())
        if(selected.contains(node.toObject().value("id").toString()))
            members.append(node.toObject().value("id"));
    if(!any) {
        auto middle=view_->mapToScene(view_->viewport()->rect().center());
        rect={middle.x()-160,middle.y()-90,320,180};
    }else rect=rect.adjusted(-35,-55,35,35);
    for(int i=0;i<groups.size();++i) {
        auto group=groups[i].toObject();
        QJsonArray left;
        for(auto const& member:group.value("members").toArray())
            if(!selected.contains(member.toString()))left.append(member);
        group["members"]=left;groups.replace(i,group);
    }
    groups.append(QJsonObject{
        {"id",id},{"title","Comment Group"},
        {"x",rect.x()},{"y",rect.y()},
        {"w",std::clamp(rect.width(),180.,1400.)},
        {"h",std::clamp(rect.height(),95.,1100.)},
        {"members",members}
    });
    next["groups"]=groups;
    commit(next);
    setMessage(QString("Created group with %1 node(s).").arg(members.size()));
}
void MaterialGraphEditor::ungroupSelected() {
    QSet<QString> selected;
    for(auto it=groupGraphics_.cbegin();it!=groupGraphics_.cend();++it)
        if(it.value()->isSelected())selected.insert(it.key());
    if(selected.isEmpty())return;
    auto next=withPositions();
    QJsonArray groups;
    for(auto const& raw:next.value("groups").toArray())
        if(!selected.contains(raw.toObject().value("id").toString()))
            groups.append(raw);
    next["groups"]=groups;
    commit(next);
    setMessage("Group removed; nodes preserved.");
}
void MaterialGraphEditor::renameCommentGroup(QString const& id) {
    auto next=withPositions();
    auto groups=next.value("groups").toArray();
    for(int i=0;i<groups.size();++i) {
        auto group=groups[i].toObject();
        if(group.value("id").toString()!=id)continue;
        bool ok=false;
        auto title=QInputDialog::getText(this,"Rename Comment Group",
            "Title:",QLineEdit::Normal,group.value("title").toString(),&ok).trimmed();
        if(!ok)return;
        if(title.isEmpty()||title.size()>96) {
            setMessage("Group title must be 1..96 characters.",true);return;
        }
        group["title"]=title;
        groups.replace(i,group);
        next["groups"]=groups;
        commit(next);
        return;
    }
}
} // namespace northstarcpp::editor
