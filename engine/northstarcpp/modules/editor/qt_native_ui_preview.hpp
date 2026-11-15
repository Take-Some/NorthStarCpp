// NorthStar native UI preview: Qt image presenter for the provider-neutral
// engine.ui raster extension. Does not replace existing editor docks.
#pragma once
#include "../host/module_loader.hpp"
#include <northstarcpp/ui.hpp>
#include <QImage>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace northstarcpp::editor {
class NativeUIPreview final : public QWidget {
public:
    explicit NativeUIPreview(QWidget* parent=nullptr)
        : QWidget(parent), module_(NORTHSTARCPP_CAPABILITY_UI),
          base_(*module_.query<NorthStarUIApi>(NORTHSTARCPP_UI_ABI)),
          raster_(module_.query_extension<NorthStarUIRasterApi>(NORTHSTARCPP_UI_RASTER_ABI)),
          input_(module_.query_extension<NorthStarUIInputApi>(NORTHSTARCPP_UI_INPUT_ABI)) {
        if (!raster_) throw std::runtime_error("UI native raster ABI is unavailable");
        NorthStarUIConfig config{sizeof(NorthStarUIConfig),0,nullptr};
        require(base_.create_context(&config,&context_));
        auto root=create(NORTHSTAR_UI_ROOT,0,"preview");
        auto panel=create(NORTHSTAR_UI_PANEL,root,"inspector");
        set(panel,"background",rgba(0x1a2738ff));
        set(panel,"width",number(430));
        set(panel,"height",number(220));
        auto title=create(NORTHSTAR_UI_TEXT,panel,"headline");
        set(title,"text",text("NORTHSTAR UI NATIVE"));
        set(title,"height",number(38));
        button_=create(NORTHSTAR_UI_BUTTON,panel,"demoButton");
        set(button_,"width",number(240));
        set(button_,"height",number(48));
        set(button_,"text",text("CLICK TO TEST"));
        inputHandle_=create(NORTHSTAR_UI_INPUT,panel,"textField");
        set(inputHandle_,"text",text("Type here"));
        set(inputHandle_,"width",number(320));
        set(inputHandle_,"height",number(38));
        base_.set_event_callback(context_,&NativeUIPreview::onEvent,this);
        setObjectName("nativeUiPreview");
        setMinimumSize(350,250);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_InputMethodEnabled,true);
    }
    ~NativeUIPreview() override {
        if(context_)base_.destroy_context(context_);
    }
    unsigned clickCount() const noexcept { return clicks_; }
protected:
    void paintEvent(QPaintEvent*) override {
        const double dpi=devicePixelRatioF();
        const int widthPx=std::max(1,static_cast<int>(std::ceil(width()*dpi)));
        const int heightPx=std::max(1,static_cast<int>(std::ceil(height()*dpi)));
        if(widthPx>8192||heightPx>8192)return;
        if(buffer_.size()!=QSize(widthPx,heightPx))
            buffer_=QImage(widthPx,heightPx,QImage::Format_RGBA8888);
        if(base_.resize(context_,widthPx,heightPx,static_cast<float>(dpi))!=NORTHSTAR_UI_OK)return;
        if(raster_->draw_rgba(context_,buffer_.bits(),
                              static_cast<uint32_t>(buffer_.bytesPerLine()),
                              widthPx,heightPx)!=NORTHSTAR_UI_OK)return;
        QPainter painter(this);
        painter.fillRect(rect(),QColor(16,21,30));
        painter.drawImage(rect(),buffer_);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton) {
            setFocus(Qt::MouseFocusReason);
            sendPointer(event,1);
        }
        QWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton)sendPointer(event,0);
        QWidget::mouseReleaseEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        sendPointer(event,2);
        QWidget::mouseMoveEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if(!input_)return QWidget::keyPressEvent(event);
        QByteArray characters;
        if(!(event->modifiers() & (Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier)))
            characters=event->text().toUtf8();
        const auto contents=ui::string(std::string_view(characters.constData(),
                                                      static_cast<size_t>(characters.size())));
        input_->keyboard(context_,static_cast<uint32_t>(event->key()==Qt::Key_Backspace?
            8:event->key()),1,contents);
        base_.pump_events(context_);
        update();
        event->accept();
    }
    void inputMethodEvent(QInputMethodEvent* event) override {
        if(!input_)return QWidget::inputMethodEvent(event);
        const QByteArray preedit=event->preeditString().toUtf8();
        const QByteArray commit=event->commitString().toUtf8();
        input_->composition(context_,ui::string(std::string_view(preedit.constData(),
            static_cast<size_t>(preedit.size()))),0);
        if(!commit.isEmpty())
            input_->composition(context_,ui::string(std::string_view(commit.constData(),
                static_cast<size_t>(commit.size()))),1);
        base_.pump_events(context_);
        update();
        event->accept();
    }
    void wheelEvent(QWheelEvent* event) override {
        if(!input_)return QWidget::wheelEvent(event);
        const QPoint delta=event->pixelDelta().isNull()?
            event->angleDelta()/8:event->pixelDelta();
        input_->wheel(context_,static_cast<float>(-delta.x()),
                               static_cast<float>(-delta.y()));
        update();
        event->accept();
    }
private:
    using UIString=NorthStarUIString;
    static NorthStarUIValue number(double v) { return ui::number(v); }
    static NorthStarUIValue text(const char* s) { return ui::text(s); }
    static NorthStarUIValue rgba(uint32_t c) {
        return {sizeof(NorthStarUIValue),NORTHSTAR_UI_VALUE_COLOR_RGBA8,
                0.0,c,0,{nullptr,0}};
    }
    void require(int32_t code) {
        if(code!=NORTHSTAR_UI_OK)
            throw std::runtime_error("engine.ui.native: "+
                std::string(module_.error("UI provider failed").what()));
    }
    NorthStarUIHandle create(uint32_t kind,NorthStarUIHandle parent,const char* name) {
        auto desc=ui::element(kind,parent,name);
        NorthStarUIHandle result=0;
        require(base_.create_element(context_,&desc,&result));
        return result;
    }
    void set(NorthStarUIHandle handle,const char* name,NorthStarUIValue value) {
        require(base_.set_property(context_,handle,ui::string(name),&value));
    }
    static void onEvent(void* user,const NorthStarUIEvent* event) {
        auto& self=*static_cast<NativeUIPreview*>(user);
        if(event && event->target==self.button_ && event->kind==NORTHSTAR_UI_EVENT_CLICK)
            ++self.clicks_;
        // Never call provider API inside callback (it is non-reentrant).
    }
    void sendPointer(QMouseEvent* event,uint32_t phase) {
        const double dpi=devicePixelRatioF();
        raster_->pointer(context_,static_cast<float>(event->position().x()*dpi),
                          static_cast<float>(event->position().y()*dpi),phase);
        base_.pump_events(context_);
        if(clicks_!=shownClicks_) {
            shownClicks_=clicks_;
            auto value=std::string("CLICKED ")+std::to_string(clicks_);
            auto v=ui::text(value);
            base_.set_property(context_,button_,ui::string("text"),&v);
        }
        update();
    }
    Module module_;
    const NorthStarUIApi& base_;
    const NorthStarUIRasterApi* raster_;
    const NorthStarUIInputApi* input_;
    void* context_=nullptr;
    NorthStarUIHandle button_=0;
    NorthStarUIHandle inputHandle_=0;
    QImage buffer_;
    unsigned clicks_=0,shownClicks_=0;
};
} // namespace northstarcpp::editor
