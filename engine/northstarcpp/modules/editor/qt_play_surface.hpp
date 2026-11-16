// NorthStar Qt Editor: complete-window in-editor game surface.
#pragma once
#include <QWidget>
#include <QTimer>
#include <QtGlobal>
class QLabel;
namespace northstarcpp::editor {
class QtPlaySurface final : public QWidget {
    Q_OBJECT
public:
    explicit QtPlaySurface(QWidget* parent=nullptr);
    ~QtPlaySurface() override;
    void setPlayerProcessId(quint64 processId);
    bool playerAttached() const noexcept {return player_window_!=0;}
    quintptr nativePlayerHandle() const noexcept {return player_window_;}
    quintptr nativeParentHandle() {return quintptr(winId());}
    // Keep the native child surface ahead of every Editor-owned QWidget/HWND.
    void raiseAboveEditorChrome();
    void requestPlayerClose();
signals:
    void exitRequested();
    void pauseRequested();
    void resumeRequested();
    void stepRequested();
    void ejectRequested();
    void playerAttachedChanged(bool attached);
protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool nativeEvent(QByteArray const& eventType,void* message,
                     qintptr* result) override;
private:
    void synchronizePlayerWindow();
    QTimer sync_timer_;
    QLabel* placeholder_{};
    QLabel* playing_from_editor_{};
    quint64 expected_pid_=0;
    quintptr player_window_=0;
};
} // namespace northstarcpp::editor
