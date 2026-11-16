// NorthStar native multi-track cutscene timeline. No proprietary code is used.
#include "qt_cutscene_timeline.hpp"
#include <northstarcpp/camera_cutscene.hpp>
#include <northstarcpp/camera_track.hpp>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace northstarcpp::editor {
namespace {
namespace fs=std::filesystem;
QString utf8(fs::path const& path) {
    auto b=path.u8string();
    return QString::fromUtf8(b.data(),int(b.size()));
}
float value(QTableWidget const* table,int row,int col) {
    auto* cell=table->item(row,col);
    if(!cell)throw std::invalid_argument("Missing numeric keyframe cell");
    bool ok=false;
    auto n=cell->text().trimmed().toFloat(&ok);
    if(!ok || !std::isfinite(n))
        throw std::invalid_argument("Timeline contains a non-finite numeric value");
    return n;
}
std::string string(QTableWidget const* table,int row,int col) {
    auto* item=table->item(row,col);
    return item?item->text().trimmed().toUtf8().toStdString():std::string{};
}
QString numeric(float v) {return QString::number(v,'g',8);}
void fill(QTableWidget* table,std::vector<QStringList> const& rows) {
    QSignalBlocker guard(table);
    table->setRowCount(int(rows.size()));
    for(int row=0;row<int(rows.size());++row)
        for(int column=0;column<table->columnCount();++column)
            table->setItem(row,column,new QTableWidgetItem(
                column<rows[std::size_t(row)].size()?
                rows[std::size_t(row)][column]:QString{}));
}
QTableWidget* makeTable(QWidget* parent,QStringList columns,
                        char const* objectName) {
    auto* table=new QTableWidget(0,int(columns.size()),parent);
    table->setObjectName(objectName);
    table->setHorizontalHeaderLabels(std::move(columns));
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setDefaultSectionSize(28);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSortingEnabled(false);
    table->setMinimumHeight(210);
    return table;
}
CameraCutscene createDefault() {
    CameraKeyframe a,b;
    a.seconds=0.f; a.pose.position={0.f,2.f,-6.f};
    b.seconds=2.5f;b.pose.position={2.f,2.5f,-3.f};
    CameraTrack wide;wide.set_keyframes({a,b});
    a.seconds=0.f;a.pose.position={1.f,2.f,-2.f};a.pose.fov_y_degrees=48.f;
    b.seconds=2.f;b.pose.position={1.f,2.1f,-1.6f};b.pose.fov_y_degrees=42.f;
    CameraTrack close;close.set_keyframes({a,b});
    CameraCutscene scene;
    scene.set_shots({
        {"establish",2.5f,0.f,std::move(wide)},
        {"close",2.f,.55f,std::move(close)}
    });
    scene.set_lens_keys({{0.f,{}},{4.5f,{2.5f,2.8f,.3f,0.f,0.f,0.f}}});
    return scene;
}

class TimelineCanvas final:public QWidget {
public:
    explicit TimelineCanvas(QWidget* parent):QWidget(parent) {
        setObjectName("northstarCutsceneLaneCanvas");
        setMinimumHeight(160);
        setMouseTracking(true);
    }
    void setScene(CameraCutscene const* scene) {
        scene_=scene;
        updateGeometry();
        update();
    }
    void setCursor(float time){cursor_=time;update();}
    std::function<void(float)> onScrub;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(),QColor("#101822"));
        if(!scene_ || scene_->empty())return;
        constexpr int left=132, top=32, row=31;
        float const duration=scene_->duration();
        double const span=std::max(1.,double(duration));
        double const scale=double(std::max(1,width()-left-15))/span;
        auto tx=[&](float seconds){return left+int(scale*seconds);};
        std::vector<std::pair<std::string,std::string>> lanes;
        for(auto const& cue:scene_->cues()) {
            auto pair=std::make_pair(cue.type,cue.lane);
            if(std::find(lanes.begin(),lanes.end(),pair)==lanes.end())
                lanes.push_back(std::move(pair));
        }
        int const total_rows=2+int(lanes.size());
        if(minimumHeight()!=top+row*total_rows+12)
            setMinimumHeight(top+row*total_rows+12);
        p.setPen(QColor("#c7d3df"));
        p.drawText(10,23,"Tracks / Time");
        int const steps=std::max(1,std::min(30,int(std::ceil(duration))));
        for(int i=0;i<=steps;++i) {
            float const t=duration*float(i)/steps;
            int const x=tx(t);
            p.setPen(QColor("#344252"));
            p.drawLine(x,top-5,x,height()-6);
            p.setPen(QColor("#a7b8ca"));
            p.drawText(x+2,23,QString::number(t,'f',1)+"s");
        }
        p.fillRect(QRect(0,top-1,width(),row),QColor("#182638"));
        p.setPen(QColor("#e4edf5"));
        p.drawText(10,top+19,"Camera / Shots");
        float start=0.f;
        for(auto const& shot:scene_->shots()) {
            QRectF bar(tx(start)+1,top+5,
                       std::max(2,tx(start+shot.duration)-tx(start)-3),row-11);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#317bc7"));
            p.drawRoundedRect(bar,3,3);
            if(shot.transition_seconds>0.f) {
                p.setBrush(QColor("#7ac4e2"));
                p.drawRect(QRectF(bar.left(),bar.top(),
                    std::min(bar.width(),scale*shot.transition_seconds),
                    bar.height()));
            }
            p.setPen(QColor("#ffffff"));
            p.drawText(bar.adjusted(6,0,-2,0),Qt::AlignVCenter|Qt::AlignLeft,
                       QString::fromStdString(shot.name));
            start+=shot.duration;
        }
        int row_index=1;
        for(auto const& lane:lanes) {
            int const y=top+row*row_index;
            p.fillRect(QRect(0,y,width(),row),
                       row_index%2?QColor("#162230"):QColor("#1a2938"));
            p.setPen(QColor("#d1dceb"));
            p.drawText(QRect(8,y,119,row),Qt::AlignVCenter|Qt::AlignLeft,
                QString::fromStdString(lane.first+" / "+lane.second));
            QColor color=lane.first=="animation"?QColor("#80c78e"):
                lane.first=="audio"?QColor("#ffc36a"):
                lane.first=="dialogue"?QColor("#e0a3ed"):
                lane.first=="overlay"?QColor("#e0c68a"):QColor("#92b8ef");
            p.setBrush(color);
            p.setPen(Qt::NoPen);
            for(auto const& cue:scene_->cues()) {
                if(cue.type!=lane.first || cue.lane!=lane.second)continue;
                int const x=tx(cue.seconds);
                QPolygonF diamond{{qreal(x),qreal(y+7)},
                    {qreal(x+6),qreal(y+15)},
                    {qreal(x),qreal(y+23)},
                    {qreal(x-6),qreal(y+15)}};
                p.drawPolygon(diamond);
            }
            ++row_index;
        }
        int const lens_y=top+row*row_index;
        p.fillRect(QRect(0,lens_y,width(),row),QColor("#263143"));
        p.setPen(QColor("#e3dee8"));
        p.drawText(10,lens_y+19,"Lens / Exposure");
        p.setPen(QPen(QColor("#ffad79"),2));
        QPointF previous;
        bool has_previous=false;
        for(auto const& key:scene_->lens_keys()) {
            auto pt=QPointF(tx(key.seconds),
                lens_y+row/2-std::clamp(key.lens.exposure_ev,-5.f,5.f)*2.f);
            if(has_previous)p.drawLine(previous,pt);
            p.setBrush(QColor("#ffad79"));
            p.drawEllipse(pt,4,4);
            previous=pt;has_previous=true;
        }
        int const playhead=tx(std::clamp(cursor_,0.f,duration));
        p.setPen(QPen(QColor("#ffe174"),2));
        p.drawLine(playhead,top-8,playhead,height()-3);
        p.setBrush(QColor("#ffe174"));
        p.drawEllipse(QPointF(playhead,top-8),4,4);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if(!scene_ || scene_->empty() || !onScrub)return;
        constexpr int left=132;
        double span=std::max(1.,double(width()-left-15));
        float t=scene_->duration()*float(
            std::clamp((e->position().x()-left)/span,0.,1.));
        onScrub(t);
    }
private:
    CameraCutscene const* scene_=nullptr;
    float cursor_=0.f;
};

class CutsceneTimelineDialog final:public QDialog {
public:
    CutsceneTimelineDialog(QWidget* parent,fs::path assets,CutscenePreview preview)
        :QDialog(parent),preview_(std::move(preview)),
         default_path_(std::move(assets)/"cinematics"/"intro.camera-cutscene.json"),
         timer_(this) {
        setObjectName("northstarCutsceneTimeline");
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle("NorthStar - Cinematic Sequencer");
        setMinimumSize(820,680);
        resize(1100,860);
        setStyleSheet(
            "QDialog{background:#131d29;color:#dde9f4}"
            "QLabel{color:#dce6ef}"
            "QTableWidget{background:#162330;alternate-background-color:#1c2c3c;"
              "color:#eaf1fa;gridline-color:#3b4b5c;selection-background-color:#286da8;"
              "selection-color:#ffffff}"
            "QHeaderView::section{background:#293c50;color:#f1f6ff;padding:6px;"
              "border:1px solid #435467}"
            "QPushButton{background:#273c52;color:#f4f6fb;border:1px solid #526982;"
              "border-radius:3px;padding:7px 12px}"
            "QPushButton:hover{background:#366287}"
            "QComboBox,QLineEdit{background:#26374a;color:#f3f7fb;"
              "selection-background-color:#3282bd}"
            "QTabWidget::pane{border:1px solid #31455b}"
            "QTabBar::tab{background:#27384a;color:#f3f7fb;padding:9px 16px}"
            "QTabBar::tab:selected{background:#3473a5}"
        );
        scene_=createDefault();
        auto* layout=new QVBoxLayout(this);
        auto* toolbar=new QHBoxLayout();
        auto button=[&](QString text,char const* id,
                        std::function<void()> callback) {
            auto* control=new QPushButton(std::move(text),this);
            control->setObjectName(id);
            QObject::connect(control,&QPushButton::clicked,this,
                [callback=std::move(callback)](){callback();});
            toolbar->addWidget(control);
            return control;
        };
        button("New","cutsceneNew",[this]{newDocument();});
        button("Open...","cutsceneOpen",[this]{openDocument();});
        button("Save","cutsceneSave",[this]{save(false);});
        button("Save As...","cutsceneSaveAs",[this]{save(true);});
        toolbar->addStretch();
        space_=new QComboBox(this);
        space_->setObjectName("cutsceneSpace");
        space_->addItems({"World Space","Target Space"});
        toolbar->addWidget(new QLabel("Space:",this));
        toolbar->addWidget(space_);
        layout->addLayout(toolbar);

        auto* info=new QLabel(
            "Camera shots | actor animation | sound | dialogue | events | HUD "
            "| focus / aperture / exposure. Double-click table cells to edit. "
            "All cue times are global seconds.",this);
        info->setWordWrap(true);layout->addWidget(info);

        auto* scroll=new QScrollArea(this);
        scroll->setWidgetResizable(true);
        canvas_=new TimelineCanvas(scroll);
        scroll->setWidget(canvas_);
        scroll->setMinimumHeight(235);
        layout->addWidget(scroll,2);

        auto* playback=new QHBoxLayout();
        auto small=[&](QString text,char const* name,std::function<void()> callback){
            auto* b=new QPushButton(text,this);b->setObjectName(name);
            QObject::connect(b,&QPushButton::clicked,this,
                [callback=std::move(callback)](){callback();});
            playback->addWidget(b);
        };
        small("Play / Pause","cutscenePlay",[this]{togglePlay();});
        small("Stop","cutsceneStop",[this]{timer_.stop();setTime(0.f);});
        small("Preview Camera","cutscenePreview",[this]{previewCamera();});
        time_=new QLabel("0.000 s",this);time_->setMinimumWidth(90);
        slider_=new QSlider(Qt::Horizontal,this);
        slider_->setObjectName("cutsceneScrub");
        playback->addWidget(slider_,1);
        playback->addWidget(time_);
        layout->addLayout(playback);
        canvas_->onScrub=[this](float seconds){setTime(seconds);};

        tabs_=new QTabWidget(this);
        tabs_->setObjectName("cutsceneTracks");
        shots_=makeTable(tabs_,{"Name","Duration (s)","Blend (s)"},
                         "cutsceneShots");
        keys_=makeTable(tabs_,
            {"Shot","Local time","X","Y","Z","Yaw","Pitch","Roll","FOV","Near","Far"},
            "cutsceneCameraKeys");
        cues_=makeTable(tabs_,
            {"Type","Lane","Time (s)","Target","Action","Value"},
            "cutsceneCues");
        lens_=makeTable(tabs_,
            {"Time (s)","Focus (m)","Aperture (f)","Exposure (EV)",
             "Motion blur","Letterbox","DoF strength"},
            "cutsceneLens");
        tabs_->addTab(shots_,"Camera Shots");
        tabs_->addTab(keys_,"Camera Keyframes");
        tabs_->addTab(cues_,"Audio / Animation / Events");
        tabs_->addTab(lens_,"Cinematic Lens");
        layout->addWidget(tabs_,3);
        auto* edit=new QHBoxLayout;
        auto* add=new QPushButton("Add Key / Shot",this);
        add->setObjectName("cutsceneAddKey");
        auto* remove=new QPushButton("Delete Selected",this);
        remove->setObjectName("cutsceneDeleteKey");
        edit->addWidget(add);edit->addWidget(remove);
        edit->addStretch();
        layout->addLayout(edit);
        status_=new QLabel("Ready",this);
        status_->setObjectName("cutsceneStatus");
        layout->addWidget(status_);
        QObject::connect(add,&QPushButton::clicked,this,[this]{addRow();});
        QObject::connect(remove,&QPushButton::clicked,this,[this]{deleteRow();});
        QObject::connect(slider_,&QSlider::valueChanged,this,[this](int ms){
            if(updating_)return;
            cursor_=ms/1000.f;
            canvas_->setCursor(cursor_);
            time_->setText(QString::number(cursor_,'f',3)+" s");
        });
        for(auto* table:{shots_,keys_,cues_,lens_})
            QObject::connect(table,&QTableWidget::itemChanged,this,
                [this](QTableWidgetItem*) {
                    if(!updating_)validateTables();
                });
        QObject::connect(space_,QOverload<int>::of(&QComboBox::currentIndexChanged),
                         this,[this](int){if(!updating_)validateTables();});
        QObject::connect(&timer_,&QTimer::timeout,this,[this]{
            const float seconds=std::min(.25f,
                float(elapsed_.restart())/1000.f);
            if(scene_.empty())return;
            if(cursor_+seconds>=scene_.duration()) {
                setTime(scene_.duration());
                timer_.stop();
            } else setTime(cursor_+seconds);
            previewCamera();
        });
        timer_.setInterval(16);
        fillTables();
        std::error_code ec;
        if(fs::is_regular_file(default_path_,ec))
            load(default_path_);
    }

    void load(fs::path const& path) {
        QFile file(utf8(path));
        if(!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("Cannot open cinematic sequence");
        if(file.size()>24*1024*1024)
            throw std::invalid_argument("Cinematic sequence exceeds 24 MiB");
        auto bytes=file.readAll();
        auto parsed=parse_camera_cutscene(
            std::string(bytes.constData(),std::size_t(bytes.size())));
        scene_=std::move(parsed);
        path_=path;
        std::error_code ec;
        stamp_=fs::last_write_time(path,ec);
        have_stamp_=!ec;
        cursor_=0.f;
        timer_.stop();
        fillTables();
        status_->setText("Loaded "+utf8(path));
    }
private:
    void fillTables() {
        updating_=true;
        space_->setCurrentIndex(scene_.space()==CameraCutsceneSpace::target?1:0);
        std::vector<QStringList> rows;
        for(auto const& s:scene_.shots()) {
            rows.push_back({QString::fromStdString(s.name),
                numeric(s.duration),numeric(s.transition_seconds)});
        }
        fill(shots_,rows);
        rows.clear();
        for(auto const& shot:scene_.shots())
            for(auto const& k:shot.track.keyframes()) {
                auto const& p=k.pose;
                rows.push_back({QString::fromStdString(shot.name),numeric(k.seconds),
                    numeric(p.position.x),numeric(p.position.y),numeric(p.position.z),
                    numeric(p.yaw),numeric(p.pitch),numeric(p.roll),
                    numeric(p.fov_y_degrees),numeric(p.near_plane),
                    numeric(p.far_plane)});
            }
        fill(keys_,rows);
        rows.clear();
        for(auto const& c:scene_.cues())
            rows.push_back({QString::fromStdString(c.type),
                QString::fromStdString(c.lane),numeric(c.seconds),
                QString::fromStdString(c.target),QString::fromStdString(c.action),
                QString::fromStdString(c.value)});
        fill(cues_,rows);
        rows.clear();
        for(auto const& k:scene_.lens_keys()) {
            auto const& x=k.lens;
            rows.push_back({numeric(k.seconds),numeric(x.focus_metres),
                numeric(x.aperture_f),numeric(x.exposure_ev),
                numeric(x.motion_blur),numeric(x.letterbox),
                numeric(x.dof_strength)});
        }
        fill(lens_,rows);
        slider_->setMaximum(std::max(1,int(scene_.duration()*1000.f)));
        slider_->setValue(int(cursor_*1000.f));
        canvas_->setScene(&scene_);
        canvas_->setCursor(cursor_);
        updating_=false;
    }
    CameraCutscene collect() const {
        std::vector<CameraCutsceneShot> shots;
        std::map<std::string,std::vector<CameraKeyframe>> frames;
        for(int i=0;i<shots_->rowCount();++i) {
            CameraCutsceneShot shot;
            shot.name=string(shots_,i,0);
            shot.duration=value(shots_,i,1);
            shot.transition_seconds=value(shots_,i,2);
            shots.push_back(std::move(shot));
        }
        for(int i=0;i<keys_->rowCount();++i) {
            CameraKeyframe k;
            k.seconds=value(keys_,i,1);
            k.pose.position={value(keys_,i,2),value(keys_,i,3),value(keys_,i,4)};
            k.pose.yaw=value(keys_,i,5);
            k.pose.pitch=value(keys_,i,6);
            k.pose.roll=value(keys_,i,7);
            k.pose.fov_y_degrees=value(keys_,i,8);
            k.pose.near_plane=value(keys_,i,9);
            k.pose.far_plane=value(keys_,i,10);
            frames[string(keys_,i,0)].push_back(k);
        }
        for(auto& shot:shots) {
            auto found=frames.find(shot.name);
            if(found==frames.end())
                throw std::invalid_argument("Camera shot has no keyframes: "+shot.name);
            shot.track.set_keyframes(std::move(found->second));
            frames.erase(found);
        }
        if(!frames.empty())
            throw std::invalid_argument("Camera keyframe refers to unknown shot: "+
                                         frames.begin()->first);
        CameraCutscene result;
        result.set_space(space_->currentIndex()==1?
                         CameraCutsceneSpace::target:CameraCutsceneSpace::world);
        result.set_shots(std::move(shots));
        std::vector<CameraCutsceneCue> cues;
        for(int i=0;i<cues_->rowCount();++i) {
            CameraCutsceneCue cue;
            cue.type=string(cues_,i,0);
            cue.lane=string(cues_,i,1);
            cue.seconds=value(cues_,i,2);
            cue.target=string(cues_,i,3);
            cue.action=string(cues_,i,4);
            cue.value=string(cues_,i,5);
            cues.push_back(std::move(cue));
        }
        result.set_cues(std::move(cues));
        std::vector<CameraLensKey> lens;
        for(int i=0;i<lens_->rowCount();++i) {
            CameraLensKey k;
            k.seconds=value(lens_,i,0);
            k.lens={value(lens_,i,1),value(lens_,i,2),value(lens_,i,3),
                    value(lens_,i,4),value(lens_,i,5),value(lens_,i,6)};
            lens.push_back(k);
        }
        result.set_lens_keys(std::move(lens));
        return result;
    }
    bool validateTables() {
        try {
            auto next=collect();
            scene_=std::move(next);
            slider_->setMaximum(std::max(1,int(scene_.duration()*1000.f)));
            cursor_=std::min(cursor_,scene_.duration());
            canvas_->setScene(&scene_);
            status_->setText(QString("Valid cutscene | %1 shots | %2 cues | %3 lens keys")
                .arg(scene_.size()).arg(scene_.cues().size())
                .arg(scene_.lens_keys().size()));
            return true;
        }catch(std::exception const& e) {
            status_->setText("Edit not yet valid: "+QString::fromUtf8(e.what()));
            return false;
        }
    }
    void setTime(float seconds) {
        cursor_=std::clamp(seconds,0.f,scene_.duration());
        QSignalBlocker blocker(slider_);
        slider_->setValue(int(cursor_*1000.f));
        canvas_->setCursor(cursor_);
        time_->setText(QString::number(cursor_,'f',3)+" s");
    }
    void previewCamera() {
        if(!validateTables() || !preview_)return;
        try {
            preview_(scene_.sample(cursor_).pose);
            status_->setText(QString("Camera preview %1 s | active shot: %2")
                .arg(cursor_,0,'f',2)
                .arg(QString::fromStdString(scene_.sample(cursor_).shot)));
        }catch(std::exception const& e) {
            status_->setText("Camera preview failed: "+QString::fromUtf8(e.what()));
        }
    }
    void togglePlay() {
        if(timer_.isActive()){timer_.stop();return;}
        if(!validateTables())return;
        if(cursor_>=scene_.duration())setTime(0.f);
        elapsed_.start();timer_.start();
    }
    QTableWidget* selectedTable() const {
        auto* widget=tabs_->currentWidget();
        for(auto* t:{shots_,keys_,cues_,lens_})
            if(widget==t)return t;
        return shots_;
    }
    void addRow() {
        auto* table=selectedTable();
        const int index=table->rowCount();
        table->insertRow(index);
        updating_=true;
        QStringList values;
        if(table==shots_)
            values={QString("shot_%1").arg(index), "2","0.5"};
        else if(table==keys_)
            values={shots_->rowCount()>0?
                string(shots_,0,0).c_str():"establish",
                numeric(std::min(scene_.duration(),cursor_)),
                "0","2","-4","0","0","0","75","0.08","500"};
        else if(table==cues_)
            values={"event","story",numeric(cursor_),"scene","signal","cue"};
        else values={numeric(cursor_),"5","4","0","0","0","0"};
        for(int i=0;i<table->columnCount();++i)
            table->setItem(index,i,new QTableWidgetItem(values.value(i)));
        updating_=false;
        table->selectRow(index);
        validateTables();
    }
    void deleteRow() {
        auto* table=selectedTable();
        if(table->rowCount()==0)return;
        int row=table->currentRow();
        if(row<0)row=table->rowCount()-1;
        table->removeRow(row);
        validateTables();
    }
    void newDocument() {
        if(timer_.isActive())timer_.stop();
        scene_=createDefault();
        path_.clear();have_stamp_=false;cursor_=0.f;
        fillTables();
        status_->setText("New unsaved cutscene. Save to the current project.");
    }
    void openDocument() {
        auto path=QFileDialog::getOpenFileName(this,"Open cinematic sequence",
            utf8(default_path_.parent_path()),
            "Cutscene (*.camera-cutscene.json *.json)");
        if(path.isEmpty())return;
        try {load(fs::path(path.toStdWString()));}
        catch(std::exception const& e) {
            QMessageBox::warning(this,"Cutscene",QString::fromUtf8(e.what()));
        }
    }
    void save(bool choosePath) {
        if(!validateTables()) {
            QMessageBox::warning(this,"Invalid cutscene",
                "Fix the highlighted validation problem before saving.");
            return;
        }
        fs::path path=path_.empty()?default_path_:path_;
        if(choosePath || path.empty()) {
            auto selected=QFileDialog::getSaveFileName(this,"Save cutscene",
                utf8(path),"Cutscene (*.camera-cutscene.json *.json)");
            if(selected.isEmpty())return;
            path=fs::path(selected.toStdWString());
        }
        try {
            if(path.empty())throw std::runtime_error("Open a project before saving");
            auto bytes=serialize_camera_cutscene(scene_);
            std::error_code ec;
            if(fs::exists(path,ec)) {
                if(ec || (path==path_ && (!have_stamp_ ||
                    fs::last_write_time(path,ec)!=stamp_)))
                    throw std::runtime_error(
                        "Cutscene file modified externally; reload before saving");
                if(path_!=path && !choosePath)
                    throw std::runtime_error("Refusing overwrite of another cutscene");
            }
            if(!QDir().mkpath(utf8(path.parent_path())))
                throw std::runtime_error("Cannot create cinematics project folder");
            QSaveFile file(utf8(path));
            if(!file.open(QIODevice::WriteOnly) ||
               file.write(bytes.data(),qint64(bytes.size()))!=qint64(bytes.size()) ||
               !file.commit())
                throw std::runtime_error("Atomic cutscene save failed");
            auto test=parse_camera_cutscene(bytes);
            (void)test;
            path_=path;
            stamp_=fs::last_write_time(path_,ec);have_stamp_=!ec;
            status_->setText("Saved "+utf8(path_));
        }catch(std::exception const& e){
            QMessageBox::warning(this,"Cutscene save",QString::fromUtf8(e.what()));
        }
    }
    CameraCutscene scene_;
    CutscenePreview preview_;
    fs::path default_path_,path_;
    fs::file_time_type stamp_{};
    bool have_stamp_=false,updating_=false;
    QComboBox* space_=nullptr;
    TimelineCanvas* canvas_=nullptr;
    QTableWidget *shots_=nullptr,*keys_=nullptr,*cues_=nullptr,*lens_=nullptr;
    QTabWidget* tabs_=nullptr;
    QSlider* slider_=nullptr;
    QLabel* time_=nullptr,*status_=nullptr;
    QTimer timer_;
    QElapsedTimer elapsed_;
    float cursor_=0.f;
};
} // namespace

void openCutsceneTimeline(QWidget* parent,fs::path const& project_assets,
                          CutscenePreview preview) {
    if(!parent)return;
    if(auto* current=parent->findChild<QDialog*>("northstarCutsceneTimeline")) {
        current->show();current->raise();current->activateWindow();
        return;
    }
    auto* dialog=new CutsceneTimelineDialog(parent,project_assets,
                                             std::move(preview));
    dialog->show();
}
} // namespace northstarcpp::editor
