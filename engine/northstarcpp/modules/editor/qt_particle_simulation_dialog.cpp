// NorthStar native YPT live preview.
// Authenticated NEF8 -> NEF16 -> NVPT decode is in shared_assets. The CPU
// billboard pass is deliberately labelled an approximation: game parity
// requires the same GPU particle/material provider in this viewport.
#include "qt_particle_simulation_dialog.hpp"
#include <northstarcpp/shared_assets.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QHash>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace northstarcpp::editor {
namespace {
namespace fs=std::filesystem;
constexpr int maximum_particles=1200;
constexpr float timestep=1.f/60.f;
constexpr float pi=3.14159265358979323846f;

QString text(std::string const& s) {
    return QString::fromUtf8(s.data(),static_cast<int>(s.size()));
}
QJsonArray first_keyframe(QJsonObject const& property) {
    auto const frames=property.value("keyframes").toArray();
    return frames.isEmpty()?QJsonArray{}:
           frames[0].toObject().value("value").toArray();
}
float finite(float n,float default_value,float lo,float hi) {
    return std::isfinite(n)?std::clamp(n,lo,hi):default_value;
}
float num(QJsonValue const& v,float fallback=0.f) {
    return v.isDouble()?static_cast<float>(v.toDouble()):fallback;
}
QVector3D source_coordinates(QJsonArray const& a) {
    // RAGE imported sources are Z-up; NorthStar's 3D preview is Y-up.
    if(a.size()<3)return {};
    return {num(a[0]),num(a[2]),num(a[1])};
}
QVector3D domain_position(QJsonObject const& domain) {
    auto keyframes=first_keyframe(domain.value("position").toObject());
    return source_coordinates(keyframes);
}
QJsonArray property_values(QJsonObject const& emitter,QString const& key) {
    for(auto const& value:emitter.value("keyframe_properties").toArray()) {
        auto const property=value.toObject();
        if(property.value("name").toString().contains(key))
            return first_keyframe(property);
    }
    return {};
}
float property_scalar(QJsonObject const& emitter,QString const& key,
                      float fallback,float lo,float hi) {
    auto const values=property_values(emitter,key);
    return values.isEmpty()?fallback:
        finite(num(values[0],fallback),fallback,lo,hi);
}
QVector3D particle_acceleration(QJsonObject const& rule) {
    for(auto const& value:rule.value("behaviours").toArray()) {
        auto const behavior=value.toObject();
        if(behavior.value("type").toString()!="acceleration")continue;
        auto const motion=source_coordinates(first_keyframe(
            behavior.value("xyz_max").toObject()));
        if(motion.lengthSquared()>1.e-6f)
            return motion;
        if(behavior.value("enable_gravity").toBool(false))
            return {0.f,-9.81f,0.f};
    }
    return {};
}
QColor particle_color(QJsonObject const& rule,QJsonObject const& event) {
    for(auto const& value:rule.value("behaviours").toArray()) {
        auto const behavior=value.toObject();
        if(behavior.value("type").toString()!="colour")continue;
        auto const rgba=first_keyframe(behavior.value("rgba_max").toObject());
        if(rgba.size()>=3) {
            auto channel=[&](int i) {
                return std::clamp(num(rgba[i],1.f),0.f,1.f);
            };
            // Some imported RAGE curves set W=0 while alpha is supplied
            // by a separate shader parameter. Avoid invisible previews.
            float alpha=rgba.size()>3?channel(3):1.f;
            if(alpha<=.001f) {
                auto const tint=event.value("colour_tint_max").toArray();
                alpha=tint.size()>3?
                    std::clamp(num(tint[3],1.f),0.f,1.f):.8f;
            }
            return QColor::fromRgbF(channel(0),channel(1),channel(2),alpha);
        }
    }
    auto const tint=event.value("colour_tint_max").toArray();
    if(tint.size()>=3)
        return QColor::fromRgbF(
            std::clamp(num(tint[0],1.f),0.f,1.f),
            std::clamp(num(tint[1],1.f),0.f,1.f),
            std::clamp(num(tint[2],1.f),0.f,1.f));
    return QColor("#bbd7f9");
}
float particle_size(QJsonObject const& rule) {
    for(auto const& value:rule.value("behaviours").toArray()) {
        auto const behavior=value.toObject();
        if(behavior.value("type").toString()!="size")continue;
        auto const whd=first_keyframe(behavior.value("whd_max").toObject());
        if(!whd.isEmpty())return finite(num(whd[0],.04f),.04f,.008f,.65f);
    }
    return .04f;
}
struct Particle {
    QVector3D position,velocity,acceleration;
    QColor color;
    float life=1.f,age=0.f,size=.05f;
};
struct Emitter {
    QVector3D origin,direction,acceleration;
    QColor color;
    float rate=12.f,life=1.f,speed=1.f,size=.05f,carry=0.f;
    float start=0.f,end=1.f;
    int max_particles=8;
    bool one_shot=false,emitted=false;
};
class ParticlePreviewCanvas final:public QWidget {
public:
    explicit ParticlePreviewCanvas(QWidget* parent=nullptr):QWidget(parent) {
        setMinimumSize(570,390);
        setMouseTracking(true);
        setToolTip("Left drag: orbit camera; mouse wheel: zoom");
        particles_.reserve(maximum_particles);
        emitters_.reserve(32);
        connect(&timer_,&QTimer::timeout,this,[this]{tick();});
        timer_.setTimerType(Qt::PreciseTimer);
        clock_.start();
        timer_.start(16);
    }
    void set_document(QJsonObject doc) {
        data_=std::move(doc);
        emitter_rules_.clear();particle_rules_.clear();
        for(auto const& value:data_.value("emitters").toArray()) {
            auto const object=value.toObject();
            emitter_rules_.insert(object.value("name").toString(),object);
        }
        for(auto const& value:data_.value("particles").toArray()) {
            auto const object=value.toObject();
            particle_rules_.insert(object.value("name").toString(),object);
        }
    }
    void set_effect(QString name) {
        selected_=std::move(name);
        rebuild_emitters();
        reset();
    }
    void set_playing(bool playing) {
        playing_=playing;
        clock_.restart();
    }
    bool playing() const noexcept {return playing_;}
    void set_loop(bool loop) noexcept {loop_=loop;}
    void set_speed(float speed) noexcept {
        speed_=finite(speed,1.f,.1f,3.f);
    }
    void reset() {
        particles_.clear();
        for(auto& source:emitters_) {
            source.carry=0.f;
            source.emitted=false;
        }
        elapsed_=0.f;
        accumulator_=0.f;
        rng_=0x6e6f7274u;
        clock_.restart();
        update();
    }
    std::function<void(int,float)> on_progress;
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if(e->button()==Qt::LeftButton) {
            dragging_=true;last_=e->position();
            setCursor(Qt::ClosedHandCursor);
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if(dragging_) {
            auto const difference=e->position()-last_;
            last_=e->position();
            yaw_+=float(difference.x())*.008f;
            pitch_=std::clamp(pitch_+float(difference.y())*.006f,.12f,1.25f);
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if(e->button()==Qt::LeftButton) {
            dragging_=false;unsetCursor();
        }
    }
    void wheelEvent(QWheelEvent* e) override {
        zoom_=std::clamp(zoom_*std::pow(1.12f,
            float(e->angleDelta().y())/120.f),.35f,5.f);
        update();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing,true);
        QLinearGradient sky(0,0,0,height());
        sky.setColorAt(0,QColor("#182c45"));
        sky.setColorAt(.52,QColor("#283f52"));
        sky.setColorAt(.53,QColor("#263b3f"));
        sky.setColorAt(1,QColor("#15252d"));
        p.fillRect(rect(),sky);

        // World-space reference ground; no fixed camera-facing 2D animation.
        p.setPen(QPen(QColor(92,127,143,76),1));
        for(int i=-8;i<=8;i++) {
            p.drawLine(project({float(i),0.f,-8.f}),
                       project({float(i),0.f,8.f}));
            p.drawLine(project({-8.f,0.f,float(i)}),
                       project({8.f,0.f,float(i)}));
        }
        p.setPen(QPen(QColor("#de9b6e"),2));
        p.drawLine(project({0,0,0}),project({1,0,0}));
        p.setPen(QPen(QColor("#9cd9a9"),2));
        p.drawLine(project({0,0,0}),project({0,1,0}));
        p.setPen(QPen(QColor("#81b5ed"),2));
        p.drawLine(project({0,0,0}),project({0,0,1}));
        p.setPen(Qt::NoPen);

        // Screen-facing sprites are driven by the live 3D motion model.
        // Texture shader/blend parity remains a separate game-renderer task.
        for(auto const& particle:particles_) {
            auto const pos=project(particle.position);
            if(pos.x()<-90 || pos.y()<-90 || pos.x()>width()+90 ||
               pos.y()>height()+90)continue;
            float const fade=std::clamp(
                std::min(particle.age*10.f,
                         (particle.life-particle.age)*3.f),0.f,1.f);
            if(fade<=0.f)continue;
            auto c=particle.color;
            c.setAlphaF(c.alphaF()*fade);
            float const radius=std::clamp(
                particle.size*scale()*1.2f,2.f,65.f);
            QRadialGradient glow(pos,radius);
            glow.setColorAt(0,c);
            auto edge=c;
            edge.setAlpha(0);
            glow.setColorAt(.65,c.lighter(120));
            glow.setColorAt(1,edge);
            p.setBrush(glow);
            p.drawEllipse(pos,radius,radius);
        }
        p.setPen(QColor("#b5cad9"));
        p.drawText(QRect(12,10,width()-24,22),
                   Qt::AlignLeft|Qt::AlignVCenter,
                   QString("LIVE FX  |  %1 particles  |  %2 s")
                   .arg(particles_.size()).arg(elapsed_,0,'f',2));
        p.setPen(QColor("#8295a9"));
        p.drawText(QRect(12,height()-28,width()-24,24),
                   Qt::AlignLeft|Qt::AlignVCenter,
                   "CPU billboards (untextured)  •  Drag: orbit  •  Wheel: zoom");
    }
private:
    void rebuild_emitters() {
        emitters_.clear();
        auto const effect_name=selected_.section('.',1);
        auto const mode=selected_.section('.',0,0);
        QJsonArray events;
        if(mode=="effect") {
            for(auto const& value:data_.value("effects").toArray()) {
                auto const effect=value.toObject();
                if(effect.value("name").toString()==effect_name) {
                    duration_=finite(num(effect.value("duration_max"),2.f),
                                     2.f,.1f,60.f);
                    events=effect.value("events").toArray();
                    break;
                }
            }
        } else {
            QJsonObject single;
            single.insert("emitter_rule",effect_name);
            single.insert("particle_rule",effect_name);
            events.append(single);
            duration_=3.f;
        }
        if(mode=="particle" && !emitter_rules_.contains(effect_name)) {
            // Direct rule selection can still be viewed when no matching
            // emitter exists; the UI must not show a static placeholder.
            QJsonObject synthetic;
            synthetic.insert("name",effect_name);
            emitter_rules_.insert(effect_name,synthetic);
        }
        for(auto const& value:events) {
            if(emitters_.size()>=32)break;
            auto const event=value.toObject();
            auto const emitter_name=event.value("emitter_rule").toString();
            auto const particle_name=event.value("particle_rule").toString();
            auto emitter=emitter_rules_.value(emitter_name);
            auto particle=particle_rules_.value(particle_name);
            if(emitter.isEmpty() && particle.isEmpty())continue;
            Emitter source;
            source.origin=domain_position(
                emitter.value("creation_domain").toObject());
            auto const target=domain_position(
                emitter.value("target_domain").toObject());
            source.direction=(target-source.origin).normalized();
            if(source.direction.lengthSquared()<.01f)
                source.direction={0.f,1.f,0.f};
            source.rate=property_scalar(
                emitter,"spawnRateOverTime",12.f,0.f,400.f);
            source.life=property_scalar(emitter,"particleLife",
                1.6f,.08f,20.f);
            source.speed=property_scalar(emitter,"speedScalar",
                1.f,0.f,20.f);
            source.size=particle_size(particle)*
                property_scalar(emitter,"sizeScalar",1.f,.05f,20.f);
            source.acceleration=particle_acceleration(particle);
            source.color=particle_color(particle,event);
            source.one_shot=emitter.value("one_shot").toBool();
            source.max_particles=std::clamp(
                emitter.value("max_particles").toInt(8),1,128);
            source.start=finite(num(event.value("start_ratio")),0.f,0.f,1.f);
            source.end=finite(num(event.value("end_ratio"),1.f),1.f,0.f,1.f);
            emitters_.push_back(source);
        }
    }
    void tick() {
        float dt=std::clamp(float(clock_.restart())/1000.f,0.f,.08f);
        if(playing_) {
            accumulator_+=dt*speed_;
            int steps=0;
            while(accumulator_>=timestep && steps++<6) {
                simulate(timestep);
                accumulator_-=timestep;
            }
        }
        if(++refresh_count_%8==0 && on_progress)
            on_progress(int(particles_.size()),elapsed_);
        update();
    }
    void simulate(float dt) {
        elapsed_+=dt;
        auto const loop_time=loop_?
            std::fmod(elapsed_,std::max(.1f,duration_)):elapsed_;
        auto const ratio=loop_time/std::max(.1f,duration_);
        for(auto& emitter:emitters_) {
            if(ratio<emitter.start || ratio>emitter.end ||
               (!loop_ && elapsed_>=duration_))continue;
            if(emitter.one_shot && emitter.emitted)continue;
            emitter.carry+=dt*emitter.rate;
            int spawn=std::min(45,int(emitter.carry));
            if(emitter.one_shot)spawn=emitter.max_particles;
            emitter.carry=std::max(0.f,emitter.carry-float(spawn));
            emitter.emitted=emitter.one_shot;
            for(int i=0;i<spawn && particles_.size()<maximum_particles;++i) {
                Particle particle;
                particle.position=emitter.origin+
                    QVector3D(random()-.5f,0,random()-.5f)*.12f;
                particle.velocity=(emitter.direction+
                    QVector3D(random()-.5f,random()-.5f,random()-.5f)*.45f)*
                    emitter.speed;
                particle.acceleration=emitter.acceleration;
                particle.life=emitter.life*(.85f+random()*.30f);
                particle.size=std::clamp(emitter.size,.009f,.4f);
                particle.color=emitter.color;
                particles_.push_back(particle);
            }
        }
        for(auto& particle:particles_) {
            particle.velocity+=particle.acceleration*dt;
            particle.position+=particle.velocity*dt;
            particle.age+=dt;
        }
        particles_.erase(std::remove_if(
            particles_.begin(),particles_.end(),
            [](auto const& p){return p.age>=p.life;}),particles_.end());
    }
    float random() {
        rng_^=rng_<<13;
        rng_^=rng_>>17;
        rng_^=rng_<<5;
        return float(rng_&0xffffff)/float(0x1000000);
    }
    float scale() const noexcept {
        return std::min(width(),height())*.125f*zoom_;
    }
    QPointF project(QVector3D const& pos) const {
        auto const sy=std::sin(yaw_),cy=std::cos(yaw_);
        auto const sp=std::sin(pitch_),cp=std::cos(pitch_);
        float const x=pos.x()*cy+pos.z()*sy;
        float const d=pos.z()*cy-pos.x()*sy;
        float const y=pos.y()*cp-d*sp;
        return {width()*.5f+x*scale(),height()*.67f-y*scale()};
    }
    QTimer timer_;
    QElapsedTimer clock_;
    QJsonObject data_;
    QHash<QString,QJsonObject> emitter_rules_,particle_rules_;
    std::vector<Particle> particles_;
    std::vector<Emitter> emitters_;
    QString selected_;
    QPointF last_;
    float elapsed_=0.f,accumulator_=0.f,duration_=3.f;
    float speed_=1.f,yaw_=.6f,pitch_=.54f,zoom_=1.f;
    std::uint32_t rng_=0x6e6f7274u;
    int refresh_count_=0;
    bool playing_=true,loop_=true,dragging_=false;
};
fs::path secure_path(fs::path const& root,std::string const& logical) {
    if(logical.empty() || logical.front()=='/' ||
       logical.find_first_of("\\:")!=std::string::npos)
        throw std::invalid_argument("Unsafe particle asset path");
    auto rel=fs::u8path(logical);
    for(auto const& part:rel)
        if(part=="." || part==".." || part.empty())
            throw std::invalid_argument("Unsafe particle reference component");
    std::error_code ec;
    auto const real_root=fs::weakly_canonical(root,ec);
    if(ec)throw std::runtime_error("Invalid particle Content root");
    auto const file=fs::weakly_canonical(real_root/rel,ec);
    if(ec || !fs::is_regular_file(file))
        throw std::runtime_error("Particle dictionary does not exist");
    auto it=file.begin();
    for(auto i=real_root.begin();i!=real_root.end();++i,++it)
        if(it==file.end() || *it!=*i)
            throw std::invalid_argument("Particle path escapes Content root");
    return file;
}
} // namespace

int openParticleSimulationPreview(QWidget* parent,fs::path const& root,
                                  std::string const& reference) {
    try {
        auto const at=reference.find('@');
        auto const relative=reference.substr(0,at);
        auto const requested=at==std::string::npos?
            QString{}:text(reference.substr(at+1));
        auto const path=secure_path(root,relative);
        std::ifstream stream(path,std::ios::binary);
        if(!stream)throw std::runtime_error("Cannot open YPT dictionary");
        std::vector<std::uint8_t> bytes(
            std::istreambuf_iterator<char>{stream},
            std::istreambuf_iterator<char>{});
        if(bytes.empty() || bytes.size()>64u*1024u*1024u)
            throw std::runtime_error("Invalid or oversized particle file");
        // Common engine decoder verifies the NEF8 hash, NVPT header and
        // runtime schema. Qt only consumes the validated JSON document.
        auto const json=shared_assets::decode_particle_runtime_json(bytes);
        auto const parsed=QJsonDocument::fromJson(
            QByteArray(json.data(),static_cast<qsizetype>(json.size())));
        if(!parsed.isObject())
            throw std::runtime_error("Cannot parse validated particle runtime");
        auto const data=parsed.object();
        QDialog dialog(parent);
        dialog.setObjectName("nativeParticleSimulationPreviewDialog");
        dialog.setWindowTitle("NorthStar Particle Simulation — "+text(relative));
        dialog.resize(1080,760);
        dialog.setStyleSheet(
            "QDialog{background:#151d28;color:#e0e9f2}"
            "QComboBox,QPushButton{background:#26384d;color:#e2eef8;"
            "border:1px solid #46617a;border-radius:5px;padding:6px}"
            "QLabel{color:#b9cee2}");
        auto* layout=new QVBoxLayout(&dialog);
        auto* description=new QLabel(
            "Live simulation from NVPT: emission, lifetime, motion and colour. "
            "GPU textures and shader effects are not yet reproduced.",&dialog);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto* canvas=new ParticlePreviewCanvas(&dialog);
        canvas->setObjectName("particleSimulationCanvas");
        canvas->set_document(data);
        layout->addWidget(canvas,1);
        auto* control=new QHBoxLayout;
        auto* effects=new QComboBox(&dialog);
        effects->setObjectName("particleEffectSelector");
        for(auto const& value:data.value("effects").toArray())
            effects->addItem("Effect  •  "+value.toObject().value("name").toString(),
                             "effect."+value.toObject().value("name").toString());
        for(auto const& value:data.value("emitters").toArray())
            effects->addItem("Emitter  •  "+value.toObject().value("name").toString(),
                             "emitter."+value.toObject().value("name").toString());
        for(auto const& value:data.value("particles").toArray())
            effects->addItem("Particle  •  "+value.toObject().value("name").toString(),
                             "particle."+value.toObject().value("name").toString());
        control->addWidget(effects,1);
        auto* play=new QPushButton("Pause",&dialog);
        play->setObjectName("particlePauseButton");
        QObject::connect(play,&QPushButton::clicked,&dialog,[canvas,play] {
            canvas->set_playing(!canvas->playing());
            play->setText(canvas->playing()?"Pause":"Play");
        });
        control->addWidget(play);
        auto* restart=new QPushButton("Restart",&dialog);
        restart->setObjectName("particleRestartButton");
        QObject::connect(restart,&QPushButton::clicked,canvas,
            [canvas]{canvas->reset();});
        control->addWidget(restart);
        auto* loop=new QCheckBox("Loop",&dialog);
        loop->setChecked(true);
        QObject::connect(loop,&QCheckBox::toggled,canvas,
            [canvas](bool v){canvas->set_loop(v);});
        control->addWidget(loop);
        auto* rate=new QComboBox(&dialog);
        rate->setObjectName("particlePlaybackSpeed");
        for(auto const speed:{.25,.5,1.,2.})
            rate->addItem(QString::number(speed,'g',2)+"x",speed);
        rate->setCurrentIndex(2);
        QObject::connect(rate,QOverload<int>::of(&QComboBox::currentIndexChanged),
            canvas,[canvas,rate](int){canvas->set_speed(
                float(rate->currentData().toDouble()));});
        control->addWidget(rate);
        layout->addLayout(control);
        auto* status=new QLabel(&dialog);
        status->setObjectName("particleSimulationStatus");
        canvas->on_progress=[status](int active,float time){
            status->setText(QString("Active particles: %1 / %2   •   %3 s")
                .arg(active).arg(maximum_particles).arg(time,0,'f',2));
        };
        layout->addWidget(status);
        QObject::connect(effects,QOverload<int>::of(&QComboBox::currentIndexChanged),
            canvas,[canvas,effects](int idx) {
                if(idx>=0)canvas->set_effect(effects->itemData(idx).toString());
            });
        auto current=effects->findData(requested);
        effects->setCurrentIndex(current>=0?current:0);
        if(effects->count())
            canvas->set_effect(effects->currentData().toString());
        return dialog.exec();
    } catch(std::exception const& error) {
        QMessageBox::warning(parent,"Particle Preview",
            QString::fromUtf8(error.what()));
        return QDialog::Rejected;
    }
}
} // namespace northstarcpp::editor
