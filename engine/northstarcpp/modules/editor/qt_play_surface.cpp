#include "qt_play_surface.hpp"
#include "qt_pie_protocol.hpp"
#include <QLabel>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QMetaObject>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace northstarcpp::editor {
namespace {
#ifdef _WIN32
constexpr wchar_t player_class[]=L"NorthStarCppWindow";
constexpr auto* exit_message_name=pie_protocol::exit_request;
#endif
}
QtPlaySurface::QtPlaySurface(QWidget* parent):QWidget(parent) {
    setObjectName("northstarPlayInEditorSurface");
    setAttribute(Qt::WA_NativeWindow,true);
    setAutoFillBackground(true);
    setStyleSheet("QWidget#northstarPlayInEditorSurface {background:#080c14;}"
                  "QLabel {color:#d4e4f0;font-size:17px;}");
    setFocusPolicy(Qt::StrongFocus);
    auto* layout=new QVBoxLayout(this);
    placeholder_=new QLabel("Starting NorthStar game simulation…\nEscape — return to Editor",this);
    placeholder_->setObjectName("playInEditorStarting");
    placeholder_->setAlignment(Qt::AlignCenter);
    layout->addWidget(placeholder_);
    // Native sibling window: the embedded Player owns a separate GPU HWND.
    // A non-native Qt label would otherwise be obscured by its swapchain.
    playing_from_editor_=new QLabel("PLAYING FROM EDITOR",this);
    playing_from_editor_->setObjectName("northstarPlayingFromEditor");
    playing_from_editor_->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
    playing_from_editor_->setStyleSheet(
        "QLabel#northstarPlayingFromEditor {"
        "color:#f4f7ff;background:#10151d;"
        "border-left:3px solid #f3c54b;"
        "padding-left:10px;padding-right:10px;"
        "font-size:14px;font-weight:700;}");
    playing_from_editor_->setFocusPolicy(Qt::NoFocus);
    playing_from_editor_->setAttribute(Qt::WA_NativeWindow,true);
    playing_from_editor_->setAttribute(Qt::WA_TransparentForMouseEvents,true);
    playing_from_editor_->setGeometry(16,16,233,34);
    playing_from_editor_->hide();
    sync_timer_.setInterval(40);
    connect(&sync_timer_,&QTimer::timeout,this,
            &QtPlaySurface::synchronizePlayerWindow);
}
QtPlaySurface::~QtPlaySurface() {
    sync_timer_.stop();
}
void QtPlaySurface::setPlayerProcessId(quint64 id) {
    expected_pid_=id;
    if(id)sync_timer_.start();
    else sync_timer_.stop();
    synchronizePlayerWindow();
}
void QtPlaySurface::synchronizePlayerWindow() {
#ifdef _WIN32
    // Qt may reorder native children during dock or DPI/layout updates.
    // Reassert overlay z-order only if a sibling has moved above it.
    if(isVisible())raiseAboveEditorChrome();
    auto const parent=reinterpret_cast<HWND>(winId());
    HWND child=nullptr;
    if(expected_pid_ && IsWindow(parent))
        for(HWND next=FindWindowExW(parent,nullptr,player_class,nullptr);
            next;
            next=FindWindowExW(parent,next,player_class,nullptr)) {
            DWORD pid=0;
            GetWindowThreadProcessId(next,&pid);
            if(quint64(pid)==expected_pid_) {
                child=next;
                break;
            }
        }
    auto const handle=reinterpret_cast<quintptr>(child);
    if(handle!=player_window_) {
        player_window_=handle;
        if(placeholder_)placeholder_->setVisible(!child);
        if(playing_from_editor_)playing_from_editor_->setVisible(bool(child));
        emit playerAttachedChanged(bool(child));
    }
    if(child) {
        if(playing_from_editor_) {
            auto const badge=reinterpret_cast<HWND>(playing_from_editor_->winId());
            if(IsWindow(badge)) {
                auto const style=GetWindowLongPtrW(badge,GWL_EXSTYLE);
                if((style & (WS_EX_TRANSPARENT|WS_EX_NOACTIVATE)) !=
                   (WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))
                    SetWindowLongPtrW(badge,GWL_EXSTYLE,
                        style|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE);
                if(GetWindow(badge,GW_HWNDPREV)) {
                    playing_from_editor_->raise();
                    SetWindowPos(badge,HWND_TOP,0,0,0,0,
                        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
                }
            }
        }
        // WM_SIZE may recreate a GPU swapchain: only resize on a real
        // geometry change, not on every 40 ms discovery polling tick.
        RECT bounds{};
        if(GetWindowRect(child,&bounds)) {
            POINT top{bounds.left,bounds.top},bottom{bounds.right,bounds.bottom};
            MapWindowPoints(nullptr,parent,&top,1);
            MapWindowPoints(nullptr,parent,&bottom,1);
            if(top.x!=0 || top.y!=0 ||
               bottom.x-top.x!=width() || bottom.y-top.y!=height())
                MoveWindow(child,0,0,width(),height(),TRUE);
        }
    }
#endif
}
void QtPlaySurface::raiseAboveEditorChrome() {
    raise();
#ifdef _WIN32
    auto const handle=reinterpret_cast<HWND>(winId());
    if(IsWindow(handle) && GetWindow(handle,GW_HWNDPREV)) {
        // An embedded GPU swapchain is a child HWND. Qt's QWidget raise()
        // alone is insufficient after docking/layout restoration: enforce
        // HWND_TOP within the same Editor parent without OS-level topmost.
        SetWindowPos(handle,HWND_TOP,0,0,0,0,
            SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }
#endif
}
void QtPlaySurface::requestPlayerClose() {
#ifdef _WIN32
    auto const handle=reinterpret_cast<HWND>(player_window_);
    if(handle && IsWindow(handle))
        PostMessageW(handle,WM_CLOSE,0,0);
#endif
}
void QtPlaySurface::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    synchronizePlayerWindow();
}
void QtPlaySurface::keyPressEvent(QKeyEvent* event) {
    if(event->key()==Qt::Key_Escape && !event->isAutoRepeat()) {
        emit exitRequested();
        event->accept();
        return;
    }
    if(event->key()==Qt::Key_F9 && !event->isAutoRepeat()) {
        emit stepRequested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
bool QtPlaySurface::nativeEvent(QByteArray const& eventType,
                               void* message,qintptr* result) {
#ifdef _WIN32
    if(eventType=="windows_generic_MSG" || eventType=="windows_dispatcher_MSG") {
        auto const* msg=static_cast<MSG*>(message);
        static UINT const escape=RegisterWindowMessageW(exit_message_name);
        static UINT const pause=RegisterWindowMessageW(
            pie_protocol::pause_request);
        static UINT const resume=RegisterWindowMessageW(
            pie_protocol::resume_request);
        static UINT const step=RegisterWindowMessageW(
            pie_protocol::step_request);
        static UINT const eject=RegisterWindowMessageW(
            pie_protocol::eject_request);
        if(msg && pause && msg->message==pause) {
            QMetaObject::invokeMethod(this,[this]{emit pauseRequested();},
                                      Qt::QueuedConnection);
            if(result)*result=0;
            return true;
        }
        if(msg && resume && msg->message==resume) {
            QMetaObject::invokeMethod(this,[this]{emit resumeRequested();},
                                      Qt::QueuedConnection);
            if(result)*result=0;
            return true;
        }
        if(msg && step && msg->message==step) {
            QMetaObject::invokeMethod(this,[this]{emit stepRequested();},
                                      Qt::QueuedConnection);
            if(result)*result=0;
            return true;
        }
        if(msg && eject && msg->message==eject) {
            QMetaObject::invokeMethod(this,[this]{emit ejectRequested();},
                                      Qt::QueuedConnection);
            if(result)*result=0;
            return true;
        }
        if(escape && msg && msg->message==escape) {
            // A cross-process Win32 notification; never stop QProcess while
            // Qt is dispatching this native message.
            QMetaObject::invokeMethod(this,[this]{emit exitRequested();},
                                      Qt::QueuedConnection);
            if(result)*result=0;
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return QWidget::nativeEvent(eventType,message,result);
}
} // namespace northstarcpp::editor
