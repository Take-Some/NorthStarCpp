// NorthStar native UI preview: Qt image presenter for the provider-neutral
// engine.ui raster extension. Does not replace existing editor docks.
#pragma once
#include "../host/module_loader.hpp"
#include <northstarcpp/ui.hpp>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QRegularExpression>
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
#include <functional>
#include <unordered_map>

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
    void onElementClicked(std::function<void(QString)> callback) {
        selected_=std::move(callback);
    }
    // Move preview elements without substituting a Qt rasterizer for the
    // engine provider. Callback receives logical-pixel drag displacement.
    void onElementDragged(std::function<void(QString,QPointF)> callback) {
        dragged_=std::move(callback);
    }
    // Preview the exact serialized UI asset through engine.ui.native.
    // Failed imports leave the previous preview intact.
    void showAsset(QJsonObject const& document) {
        if(!QStringList{"northstar.ui/1","northstar.ui/2"}.contains(document.value("schema").toString()))
            throw std::invalid_argument("Unsupported UI asset schema");
        if(!document.value("nodes").isArray())
            throw std::invalid_argument("UI document requires a nodes array");
        QJsonArray nodes=document.value("nodes").toArray();
        const bool v2=document.value("schema").toString()=="northstar.ui/2";
        const auto styles=v2?document.value("styles").toObject():QJsonObject{};
        const auto components=document.value("components").toObject();
        const auto tokens=v2?document.value("tokens").toObject():QJsonObject{};
        if(styles.size()>128 || tokens.size()>128)
            throw std::invalid_argument("UI style/token dictionary too large");
        static const QRegularExpression namePattern("^[A-Za-z][A-Za-z0-9_.-]{0,63}$");
        for(auto it=styles.begin();it!=styles.end();++it)
            if(!namePattern.match(it.key()).hasMatch() || !it.value().isObject() ||
               it.value().toObject().size()>48)
                throw std::invalid_argument("Invalid named UI style");
        for(auto it=tokens.begin();it!=tokens.end();++it)
            if(!namePattern.match(it.key()).hasMatch() ||
               !(it.value().isString() || it.value().isBool() || it.value().isDouble()))
                throw std::invalid_argument("Invalid UI design token");
        if(nodes.size()>256)
            throw std::invalid_argument("UI document exceeds 256 nodes");
        NorthStarUIConfig cfg{sizeof(NorthStarUIConfig),0,nullptr};
        void* replacement=nullptr;
        require(base_.create_context(&cfg,&replacement));
        std::unordered_map<std::string,NorthStarUIHandle> handles;
        std::vector<std::pair<QString,NorthStarUIHandle>> ordered;
        try {
            auto createIn=[this,replacement](std::uint32_t kind,
                                              NorthStarUIHandle parent,
                                              std::string const& id) {
                auto desc=ui::element(kind,parent,id);
                NorthStarUIHandle result=0;
                require(base_.create_element(replacement,&desc,&result));
                return result;
            };
            handles.emplace("",createIn(NORTHSTAR_UI_ROOT,0,"preview"));
            QRegularExpression valid("^[A-Za-z][A-Za-z0-9_.-]{0,63}$");
            for(auto const& item:nodes) {
                if(!item.isObject())throw std::invalid_argument("Invalid UI node");
                auto const node=item.toObject();
                const QString qid=node.value("id").toString();
                const QString parent=node.value("parent").toString();
                const QString type=node.value("type").toString();
                if(!valid.match(qid).hasMatch() || qid.size()>64)
                    throw std::invalid_argument("Invalid UI element ID");
                auto parentIt=handles.find(parent.toStdString());
                if(parentIt==handles.end() || handles.count(qid.toStdString()))
                    throw std::invalid_argument("UI parent must precede unique child");
                std::uint32_t kind=0;
                if(type=="panel")kind=NORTHSTAR_UI_PANEL;
                else if(type=="text")kind=NORTHSTAR_UI_TEXT;
                else if(type=="button")kind=NORTHSTAR_UI_BUTTON;
                else if(type=="image")kind=NORTHSTAR_UI_IMAGE;
                else if(type=="input")kind=NORTHSTAR_UI_INPUT;
                else throw std::invalid_argument("Unknown UI node type");
                auto const name=qid.toStdString();
                auto const handle=createIn(kind,parentIt->second,name);
                handles.emplace(name,handle);
                ordered.emplace_back(qid,handle);
                QJsonObject props;
                if(node.contains("component")) {
                    auto comp=node.value("component").toString();
                    auto entry=components.value(comp).toObject();
                    if(entry.value("type").toString()!=type)
                        throw std::invalid_argument("Unknown UI component");
                    props=entry.value("props").toObject();
                }
                if(node.contains("style")) {
                    auto const style=node.value("style").toString();
                    if(!v2 || style.isEmpty() || !styles.contains(style) ||
                       !styles.value(style).isObject())
                        throw std::invalid_argument("Unknown UI style");
                    auto const inherited=styles.value(style).toObject();
                    for(auto it=inherited.begin();it!=inherited.end();++it)
                        props.insert(it.key(),it.value());
                }
                auto const overrides=node.value("props").toObject();
                for(auto it=overrides.begin();it!=overrides.end();++it)
                    props.insert(it.key(),it.value());
                if(props.size()>48)
                    throw std::invalid_argument("UI node exceeds 48 properties");
                for(auto it=props.begin();it!=props.end();++it) {
                    if(!valid.match(it.key()).hasMatch() || it.key().size()>64)
                        throw std::invalid_argument("Invalid UI property");
                    const auto key=it.key().toStdString();
                    auto prop=it.value();
                    if(prop.isObject()) {
                        auto const ref=prop.toObject();
                        if(ref.size()!=1 || !ref.value("token").isString() ||
                           !tokens.contains(ref.value("token").toString()))
                            throw std::invalid_argument("Unknown UI design token");
                        prop=tokens.value(ref.value("token").toString());
                    }
                    std::string str;
                    NorthStarUIValue value{};
                    if(prop.isBool())value=ui::boolean(prop.toBool());
                    else if(prop.isDouble())value=ui::number(prop.toDouble());
                    else if(prop.isString()) {
                        str=prop.toString().toStdString();
                        static const QRegularExpression hex("^#[0-9a-fA-F]{8}$");
                        if(hex.match(QString::fromStdString(str)).hasMatch())
                            value=rgba(static_cast<std::uint32_t>(
                                std::stoul(str.substr(1),nullptr,16)));
                        else value=ui::text(str);
                    }else throw std::invalid_argument("Unsupported UI property value");
                    if(prop.isString() && prop.toString().toUtf8().size()>2048)
                        throw std::invalid_argument("UI text exceeds 2048 bytes");
                    if(prop.isDouble() &&
                       (!std::isfinite(prop.toDouble()) ||
                        std::abs(prop.toDouble())>1000000.0))
                        throw std::invalid_argument("UI number exceeds runtime limits");
                    require(base_.set_property(replacement,handle,ui::string(key),&value));
                }
            }
            require(base_.set_event_callback(replacement,&NativeUIPreview::onEvent,this));
        }catch(...) {
            base_.destroy_context(replacement);
            throw;
        }
        if(context_)base_.destroy_context(context_);
        context_=replacement;
        namesByHandle_.clear();
        orderedHandles_=std::move(ordered);
        for(const auto& [name,handle]:handles)
            if(!name.empty())namesByHandle_.emplace(handle,QString::fromStdString(name));
        button_=inputHandle_=0;
        clicks_=shownClicks_=0;
        buffer_=QImage();
        update();
    }

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
        if(dragHandle_ && !dragName_.isEmpty()) {
            NorthStarUIRect selectedRect{};
            if(raster_->get_rect(context_,dragHandle_,&selectedRect)==NORTHSTAR_UI_OK) {
                painter.setPen(QPen(QColor(255,209,100),2,Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                const QRectF box(selectedRect.x/dpi+dragDelta_.x(),
                    selectedRect.y/dpi+dragDelta_.y(),
                    selectedRect.width/dpi,selectedRect.height/dpi);
                painter.drawRect(box);
            }
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton) {
            setFocus(Qt::MouseFocusReason);
            dragName_.clear();
            dragDelta_={};
            dragStart_=event->position();
            const double scale=devicePixelRatioF();
            const double x=dragStart_.x()*scale,y=dragStart_.y()*scale;
            for(auto it=orderedHandles_.rbegin();it!=orderedHandles_.rend();++it) {
                NorthStarUIRect rect{};
                if(raster_->get_rect(context_,it->second,&rect)!=NORTHSTAR_UI_OK)
                    continue;
                if(x>=rect.x && y>=rect.y &&
                   x<rect.x+rect.width && y<rect.y+rect.height) {
                    dragName_=it->first;
                    dragHandle_=it->second;
                    if(selected_)selected_(dragName_);
                    break;
                }
            }
            sendPointer(event,1);
        }
        QWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton) {
            sendPointer(event,0);
            const auto name=dragName_;
            const auto delta=dragDelta_;
            dragName_.clear();
            dragHandle_=0;
            dragDelta_={};
            if(!name.isEmpty() && dragged_ &&
               (std::abs(delta.x())>=2. || std::abs(delta.y())>=2.))
                dragged_(name,delta);
            update();
        }
        QWidget::mouseReleaseEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if(!dragName_.isEmpty() && (event->buttons()&Qt::LeftButton)) {
            dragDelta_=event->position()-dragStart_;
            update();
        } else sendPointer(event,2);
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
        if(event && event->kind==NORTHSTAR_UI_EVENT_CLICK) {
            auto name=self.namesByHandle_.find(event->target);
            if(name!=self.namesByHandle_.end() && self.selected_)
                self.selected_(name->second);
        }
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
    std::unordered_map<NorthStarUIHandle,QString> namesByHandle_;
    std::function<void(QString)> selected_;
    std::function<void(QString,QPointF)> dragged_;
    std::vector<std::pair<QString,NorthStarUIHandle>> orderedHandles_;
    QPointF dragStart_{},dragDelta_{};
    QString dragName_;
    NorthStarUIHandle dragHandle_=0;
};
} // namespace northstarcpp::editor
