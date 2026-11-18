// Non-blocking V-HACD cook; bounded shared NorthStar workers and GUI-thread completion.
// Collision data is owned by worker; Qt widgets never cross the worker boundary.
#pragma once
#include <northstarcpp/collision_cook_cache.hpp>
#include <northstarcpp/task_scheduler.hpp>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <utility>

namespace northstarcpp::editor {
inline void startCollisionCook(QWidget* owner,
    collision_mesh::Mesh input,std::uint32_t splits,
    std::filesystem::path cache_folder,
    std::function<void(collision_hulls::Set,bool)> apply,
    QPushButton* trigger=nullptr) {
    if(!owner)throw std::invalid_argument("Collision cooker requires Qt owner");
    auto progress=std::make_shared<std::atomic<int>>(0);
    auto cancel=std::make_shared<std::atomic<bool>>(false);
    // Scheduling happens before disabling controls or creating Qt widgets.
    // Queue exhaustion leaves the GUI unchanged and reports an exception.
    std::shared_ptr<std::future<collision_cook_cache::Result>> future;
    try {
        future=std::make_shared<std::future<collision_cook_cache::Result>>(
            task_scheduler::asset_tasks().submit(
                task_scheduler::Priority::background,cancel,
                [input=std::move(input),splits,
                 cache_folder=std::move(cache_folder),progress,cancel]() mutable {
                    return collision_cook_cache::load_or_cook(input,splits,cache_folder,
                        100000,progress.get(),cancel.get());
                }));
    } catch(std::exception const& error) {
        QMessageBox::warning(owner,"Collision Generation",
                             QString::fromUtf8(error.what()));
        return;
    }

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
    auto* timer=new QTimer(dialog);
    timer->setInterval(80);
    QObject::connect(dialog,&QProgressDialog::canceled,dialog,[cancel] {
        cancel->store(true,std::memory_order_relaxed);
    });
    QObject::connect(dialog,&QObject::destroyed,[cancel] {
        cancel->store(true,std::memory_order_relaxed);
    });
    // Only the GUI timer observes the future. No blocking wait on the main
    // thread, no QThread handoff of QObject pointers, no orphan QFutureWatcher.
    QObject::connect(timer,&QTimer::timeout,dialog,
        [dialog,timer,progress,future,safe_trigger,cancel,
         apply=std::move(apply)]() mutable {
            dialog->setValue(std::clamp(progress->load(std::memory_order_relaxed),0,99));
            if(future->wait_for(std::chrono::milliseconds(0))!=
               std::future_status::ready)return;
            timer->stop();
            if(safe_trigger)safe_trigger->setEnabled(true);
            dialog->hide();
            try {
                auto result=future->get();
                if(!cancel->load(std::memory_order_relaxed))
                    apply(std::move(result.hulls),result.hit);
            }catch(std::exception const& e) {
                if(!cancel->load(std::memory_order_relaxed))
                    QMessageBox::warning(dialog->parentWidget(),
                        "Collision Generation",QString::fromUtf8(e.what()));
            }
            dialog->deleteLater();
        });
    dialog->show();
    timer->start();
}
} // namespace northstarcpp::editor
