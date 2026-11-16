#pragma once
#include <QTreeWidget>
#include <QMouseEvent>
#include <QDropEvent>
#include <QTimer>
#include <functional>
namespace northstarcpp::editor {
class ActorTreeWidget final : public QTreeWidget {
public:
    explicit ActorTreeWidget(QWidget* parent):QTreeWidget(parent) {}
    std::function<void(int,int,QString)> moveActor;
    std::function<void(int)> toggleVisibility;
protected:
    void mousePressEvent(QMouseEvent* event) override {
        // The UE-style eye control toggles visibility without changing selection.
        if(event->button()==Qt::LeftButton &&
           columnAt(int(event->position().x()))==1 && toggleVisibility) {
            auto* item=itemAt(event->position().toPoint());
            if(item) {
                auto const index=item->data(0,Qt::UserRole).toInt();
                if(index>=0) {
                    auto callback=toggleVisibility;
                    QTimer::singleShot(0,this,[callback,index]{callback(index);});
                    event->accept();
                    return;
                }
            }
        }
        QTreeWidget::mousePressEvent(event);
    }
    void dropEvent(QDropEvent* event) override {
        if(event->source()!=this || !moveActor) {
            event->ignore();
            return;
        }
        auto* source=currentItem();
        auto* target=itemAt(event->position().toPoint());
        if(!source || source->data(0,Qt::UserRole).toInt()<0) {
            event->ignore();
            return;
        }
        int sourceIndex=source->data(0,Qt::UserRole).toInt();
        int targetIndex=target?target->data(0,Qt::UserRole).toInt():-1;
        if(source==target || sourceIndex==targetIndex) {
            event->ignore();
            return;
        }
        QString targetLayer=target && targetIndex<0?target->text(0):QString();
        auto callback=moveActor;
        QTimer::singleShot(0,this,[callback,sourceIndex,targetIndex,targetLayer] {
            callback(sourceIndex,targetIndex,targetLayer);
        });
        event->acceptProposedAction();
    }
};
}
