// Non-blocking V-HACD collision cook with progress, cancellation and DDC.
// UI callbacks always run on the Qt GUI thread; worker owns all input by value.
#pragma once
#include <northstarcpp/collision_cook_cache.hpp>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <functional>
#include <memory>

namespace northstarcpp::editor {
inline void startCollisionCook(QWidget* owner,
    collision_mesh::Mesh input,std::uint32_t splits,
    std::filesystem::path cache_folder,
    std::function<void(collision_hulls::Set,bool)> apply,
    QPushButton* trigger=nullptr) {
    if(!owner)throw std::invalid_argument("Collision cooker requires Qt owner");
    auto progress=std::make_shared<std::atomic<int>>(0);
    auto cancel=std::make_shared<std::atomic<bool>>(false);
    QPointer<QPushButton> safe_trigger(trigger);
    if(safe_trigger)safe_trigger->setEnabled(false);
    auto* dialog=new QProgressDialog("V-HACD convex decomposition...","Cancel",
                                     0,100,owner);
    dialog->setObjectName("collisionVhacdProgress");
    dialog->setWindowTitle("Generate Collision");
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    auto* watcher=new QFutureWatcher<collision_cook_cache::Result>(owner);
    auto* timer=new QTimer(dialog);
    timer->setInterval(80);
    QObject::connect(timer,&QTimer::timeout,dialog,[dialog,progress] {
        dialog->setValue(std::clamp(progress->load(std::memory_order_relaxed),0,99));
    });
    QObject::connect(dialog,&QProgressDialog::canceled,owner,[cancel] {
        cancel->store(true,std::memory_order_relaxed);
    });
    QObject::connect(watcher,&QFutureWatcher<collision_cook_cache::Result>::finished,
        owner,[watcher,dialog,timer,safe_trigger,cancel,apply=std::move(apply)]() mutable {
            timer->stop();
            if(safe_trigger)safe_trigger->setEnabled(true);
            dialog->hide();
            try {
                auto result=watcher->result();
                if(!cancel->load(std::memory_order_relaxed))
                    apply(std::move(result.hulls),result.hit);
            }catch(std::exception const& e) {
                if(!cancel->load(std::memory_order_relaxed))
                    QMessageBox::warning(dialog->parentWidget(),
                        "Collision Generation",QString::fromUtf8(e.what()));
            }
            watcher->deleteLater();
            dialog->deleteLater();
        });
    auto future=QtConcurrent::run(
        [input=std::move(input),splits,cache_folder=std::move(cache_folder),
         progress,cancel]() mutable {
            return collision_cook_cache::load_or_cook(input,splits,cache_folder,
                100000,progress.get(),cancel.get());
        });
    watcher->setFuture(future);
    dialog->show();
    timer->start();
}
} // namespace northstarcpp::editor
