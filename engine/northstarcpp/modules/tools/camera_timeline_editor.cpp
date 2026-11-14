// NorthStarCpp Camera Timeline Editor - native Qt6/C++ application.
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// Authoring tool: edits portable camera-track assets for the engine.assets VFS.
#include <northstarcpp/camera_track.hpp>
#include <northstarcpp/logger.hpp>
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <northstarcpp/frame_clock.hpp>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QSlider>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace northstarcpp;

namespace {
constexpr int column_count = 10;

class TrackPreview final : public QWidget {
public:
    explicit TrackPreview(QWidget* parent=nullptr): QWidget(parent) {
        setMinimumSize(420,320);
        setAutoFillBackground(true);
    }
    void setTrack(CameraTrack const* track,float time) {
        track_=track;
        time_=time;
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(),QColor(249,250,252));
        painter.setPen(QColor(57,66,82));
        QFont title=font();title.setBold(true);title.setPointSize(11);
        painter.setFont(title);
        painter.drawText(QRect(18,10,width()-32,28),
                         Qt::AlignLeft|Qt::AlignVCenter,
                         QStringLiteral("TOP VIEW  /  X-Z CAMERA PATH"));
        if(!track_||track_->empty())return;
        auto const& keyframes=track_->keyframes();
        float minx=keyframes.front().pose.position.x;
        float maxx=minx,minz=keyframes.front().pose.position.z,maxz=minz;
        for(auto const& frame:keyframes) {
            auto const p=frame.pose.position;
            minx=std::min(minx,p.x);maxx=std::max(maxx,p.x);
            minz=std::min(minz,p.z);maxz=std::max(maxz,p.z);
        }
        auto const bound=QRectF(26,53,width()-52,height()-80);
        auto const span_x=std::max(3.f,maxx-minx);
        auto const span_z=std::max(3.f,maxz-minz);
        double const scale=std::min(bound.width()/span_x,bound.height()/span_z)*0.84;
        double const center_x=(minx+maxx)*.5;
        double const center_z=(minz+maxz)*.5;
        auto map=[&](Vector3 const& p)->QPointF {
            return {bound.center().x()+(p.x-center_x)*scale,
                    bound.center().y()-(p.z-center_z)*scale};
        };
        painter.setPen(QPen(QColor(225,230,235),1));
        for(int n=-10;n<=10;++n) {
            double const x=bound.center().x()+n*32.;
            double const y=bound.center().y()+n*32.;
            painter.drawLine(QPointF(x,bound.top()),QPointF(x,bound.bottom()));
            painter.drawLine(QPointF(bound.left(),y),QPointF(bound.right(),y));
        }
        QPolygonF path;
        int const samples=std::max(32,std::min(512,int(track_->duration()*25)));
        for(int i=0;i<=samples;++i) {
            auto const t=track_->duration()*i/samples;
            path<<map(track_->sample(t).position);
        }
        painter.setPen(QPen(QColor(25,118,210),2.5,Qt::SolidLine,Qt::RoundCap));
        painter.drawPolyline(path);
        for(std::size_t i=0;i<keyframes.size();++i) {
            auto pos=map(keyframes[i].pose.position);
            painter.setPen(QPen(QColor(30,64,103),1));
            painter.setBrush(QColor(255,255,255));
            painter.drawEllipse(pos,5,5);
        }
        auto pose=track_->sample(time_);
        auto loc=map(pose.position);
        painter.setPen(QPen(QColor(220,63,70),3,Qt::SolidLine,Qt::RoundCap));
        painter.drawEllipse(loc,6.5,6.5);
        QPointF const nose(loc.x()+std::sin(pose.yaw)*22,
                           loc.y()-std::cos(pose.yaw)*22);
        painter.drawLine(loc,nose);
        painter.setPen(QColor(65,74,87));
        painter.drawText(QRect(20,height()-27,width()-40,22),
                         Qt::AlignLeft,
                         QString("Time %1s    FOV %2°    X %3 / Y %4 / Z %5")
                         .arg(time_,0,'f',2).arg(pose.fov_y_degrees,0,'f',1)
                         .arg(pose.position.x,0,'f',2).arg(pose.position.y,0,'f',2)
                         .arg(pose.position.z,0,'f',2));
    }
private:
    CameraTrack const* track_=nullptr;
    float time_=0.f;
};

class TimelineEditor final : public QMainWindow {
public:
    TimelineEditor() {
        setWindowTitle(QStringLiteral("NorthStarCpp • Camera Timeline Editor"));
        resize(1250,760);
        setStyleSheet(
            "QMainWindow{background:#f6f8fb} "
            "QToolBar{background:#edf1f5;border:0;padding:6px;spacing:6px} "
            "QTableWidget{background:white;gridline-color:#e4e9ee} "
            "QSlider::groove:horizontal{height:5px;background:#d1d9e1} "
            "QSlider::handle:horizontal{width:13px;background:#1976d2;margin:-5px 0;border-radius:6px}"
        );
        auto* file=menuBar()->addMenu("&File");
        auto* new_action=file->addAction("&New",QKeySequence::New,this,[this]{newTrack();});
        auto* open_action=file->addAction("&Open...",QKeySequence::Open,this,[this]{openDialog();});
        auto* save_action=file->addAction("&Save",QKeySequence::Save,this,[this]{save(false);});
        auto* save_as=file->addAction("Save &As...",QKeySequence::SaveAs,this,[this]{save(true);});
        Q_UNUSED(new_action);Q_UNUSED(open_action);Q_UNUSED(save_action);Q_UNUSED(save_as);
        auto* bar=addToolBar("Camera Timeline");
        bar->setMovable(false);
        bar->addAction("Add Keyframe",this,[this]{addKeyframe();});
        bar->addAction("Delete Keyframe",this,[this]{deleteKeyframe();});
        bar->addSeparator();
        play_action_=bar->addAction("Play",this,[this]{togglePlayback();});
        bar->addAction("Stop",this,[this]{stopPlayback();});
        auto* center=new QWidget;
        auto* vertical=new QVBoxLayout(center);
        auto* info=new QLabel("Keyframes are stored in an Asset Manager-compatible JSON file. "
                              "Double-click cells to edit.");
        vertical->addWidget(info);
        table_=new QTableWidget(0,column_count);
        table_->setHorizontalHeaderLabels({"Time (s)","X","Y","Z","Yaw (rad)",
                                           "Pitch (rad)","Roll (rad)","FOV (°)",
                                           "Near","Far"});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setAlternatingRowColors(true);
        vertical->addWidget(table_,1);
        auto* row=new QHBoxLayout;
        time_label_=new QLabel("00:00.000");
        time_label_->setMinimumWidth(86);
        timeline_=new QSlider(Qt::Horizontal);
        timeline_->setRange(0,1000);
        row->addWidget(time_label_);
        row->addWidget(timeline_,1);
        vertical->addLayout(row);
        setCentralWidget(center);
        auto* dock=new QDockWidget("Camera Path Preview",this);
        dock->setAllowedAreas(Qt::RightDockWidgetArea|Qt::LeftDockWidgetArea);
        preview_=new TrackPreview;
        dock->setWidget(preview_);
        addDockWidget(Qt::RightDockWidgetArea,dock);
        resizeDocks({dock},{450},Qt::Horizontal);
        statusBar()->showMessage("Ready");
        connect(table_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem*) {
            if(!changing_) rebuildFromTable();
        });
        connect(timeline_,&QSlider::valueChanged,this,[this](int value) {
            if(changing_)return;
            cursor_=value/1000.f;
            updatePreview();
        });
        connect(&timer_,&QTimer::timeout,this,[this]{
            auto dt=elapsed_.restart_seconds();
            cursor_+=std::clamp(float(dt),0.f,0.25f);
            if(cursor_>=track_.duration()) {
                cursor_=track_.duration();
                stopPlayback();
            }
            changing_=true;
            timeline_->setValue(int(cursor_*1000.f));
            changing_=false;
            updatePreview();
        });
        timer_.setInterval(16);
        newTrack();
    }

    void load(QString const& path) {
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("Cannot open camera-track asset");
        if(file.size()>24*1024*1024)
            throw std::runtime_error("Camera-track asset too large");
        auto bytes=file.readAll();
        auto loaded=parse_camera_track(bytes.toStdString());
        track_=std::move(loaded);
        path_=path;
        fillTable();
        statusBar()->showMessage("Opened "+path,5000);
    }

private:
    static QString number(float value) {return QString::number(value,'g',8);}
    void newTrack() {
        stopPlayback();
        CameraKeyframe a,b;
        a.seconds=0.f;
        a.pose.position={0.f,2.f,-4.f};
        b.seconds=2.f;
        b.pose.position={0.f,2.f,1.f};
        track_.set_keyframes({a,b});
        path_.clear();
        fillTable();
        statusBar()->showMessage("New track",3000);
    }

    void fillTable() {
        changing_=true;
        auto const& frames=track_.keyframes();
        table_->setRowCount(int(frames.size()));
        for(int i=0;i<int(frames.size());++i){
            auto const& f=frames[i];
            float values[]{f.seconds,f.pose.position.x,f.pose.position.y,
                f.pose.position.z,f.pose.yaw,f.pose.pitch,f.pose.roll,
                f.pose.fov_y_degrees,f.pose.near_plane,f.pose.far_plane};
            for(int col=0;col<column_count;++col)
                table_->setItem(i,col,new QTableWidgetItem(number(values[col])));
        }
        cursor_=std::clamp(cursor_,0.f,track_.duration());
        timeline_->setMaximum(std::max(1,int(track_.duration()*1000.f)));
        timeline_->setValue(int(cursor_*1000.f));
        changing_=false;
        updatePreview();
    }

    bool rebuildFromTable() {
        try {
            std::vector<CameraKeyframe> frames;
            frames.reserve(table_->rowCount());
            for(int row=0;row<table_->rowCount();++row) {
                float v[column_count]{};
                for(int col=0;col<column_count;++col) {
                    auto* item=table_->item(row,col);
                    bool ok=false;
                    if(!item) throw std::runtime_error("Missing timeline cell");
                    v[col]=item->text().toFloat(&ok);
                    if(!ok||!std::isfinite(v[col]))
                        throw std::runtime_error("Timeline contains invalid number");
                }
                CameraKeyframe k;
                k.seconds=v[0];
                k.pose.position={v[1],v[2],v[3]};
                k.pose.yaw=v[4];k.pose.pitch=v[5];k.pose.roll=v[6];
                k.pose.fov_y_degrees=v[7];
                k.pose.near_plane=v[8];k.pose.far_plane=v[9];
                frames.push_back(k);
            }
            CameraTrack loaded;
            loaded.set_keyframes(std::move(frames));
            track_=std::move(loaded);
            cursor_=std::min(cursor_,track_.duration());
            changing_=true;
            timeline_->setMaximum(std::max(1,int(track_.duration()*1000.f)));
            changing_=false;
            updatePreview();
            statusBar()->showMessage("Keyframes updated",2500);
            return true;
        } catch (std::exception const& e) {
            statusBar()->showMessage(QString("Invalid timeline: %1").arg(e.what()),10000);
            return false;
        }
    }

    void addKeyframe() {
        if(!rebuildFromTable())return;
        auto frames=track_.keyframes();
        float const last=frames.back().seconds;
        CameraKeyframe k;
        k.seconds=std::min(36000.f,last+1.f);
        if(k.seconds<=last) {
            statusBar()->showMessage("Maximum timeline duration reached",5000);
            return;
        }
        k.pose=track_.sample(last);
        frames.push_back(k);
        track_.set_keyframes(std::move(frames));
        fillTable();
        table_->selectRow(table_->rowCount()-1);
    }

    void deleteKeyframe() {
        if(track_.size()<=1)return;
        auto frames=track_.keyframes();
        int row=table_->currentRow();
        if(row<0||row>=int(frames.size()))row=int(frames.size())-1;
        frames.erase(frames.begin()+row);
        track_.set_keyframes(std::move(frames));
        fillTable();
    }

    void openDialog() {
        auto path=QFileDialog::getOpenFileName(this,"Open camera track",path_,
            "Camera tracks (*.camera.json *.json);;All files (*)");
        if(path.isEmpty())return;
        try {load(path);}
        catch(std::exception const& e) {
            QMessageBox::critical(this,"Open failed",QString::fromUtf8(e.what()));
        }
    }

    void save(bool ask_path) {
        if(!rebuildFromTable()) {
            QMessageBox::warning(this,"Invalid track","Correct invalid numeric cells before saving.");
            return;
        }
        QString path=path_;
        if(ask_path||path.isEmpty()) {
            path=QFileDialog::getSaveFileName(this,"Save camera track",
                path.isEmpty()?"camera.camera.json":path,
                "Camera tracks (*.camera.json *.json)");
        }
        if(path.isEmpty())return;
        auto const data=serialize_camera_track(track_);
        QSaveFile target(path);
        if(!target.open(QIODevice::WriteOnly)||
            target.write(data.data(),qint64(data.size()))!=qint64(data.size())||
            !target.commit()) {
            QMessageBox::critical(this,"Save failed","Cannot atomically save camera-track asset.");
            return;
        }
        path_=path;
        statusBar()->showMessage("Saved "+path,5000);
    }

    void updatePreview(){
        if(track_.empty())return;
        time_label_->setText(QString("%1s").arg(cursor_,7,'f',3));
        preview_->setTrack(&track_,cursor_);
    }

    void togglePlayback(){
        if(timer_.isActive()){stopPlayback();return;}
        if(!rebuildFromTable())return;
        if(cursor_>=track_.duration())cursor_=0.f;
        elapsed_.start();
        timer_.start();
        play_action_->setText("Pause");
    }
    void stopPlayback(){
        timer_.stop();
        if(play_action_)play_action_->setText("Play");
    }

    QTableWidget* table_=nullptr;
    QSlider* timeline_=nullptr;
    QLabel* time_label_=nullptr;
    TrackPreview* preview_=nullptr;
    QAction* play_action_=nullptr;
    QTimer timer_;
    northstarcpp::timing::FrameClock elapsed_;
    CameraTrack track_;
    QString path_;
    float cursor_=0.f;
    bool changing_=false;
};
} // namespace

int main(int argc,char** argv) {
    if(argc==2&&std::strcmp(argv[1],"--self-test")==0) {
        try {
            CameraTrack track;
            CameraKeyframe a,b;
            b.seconds=1.f;b.pose.position={0.f,2.f,3.f};
            track.set_keyframes({a,b});
            auto decoded=parse_camera_track(serialize_camera_track(track));
            return decoded.size()==2&&std::abs(decoded.sample(1.f).position.z-3.f)<0.001f
                ? 0 : 3;
        } catch(...) {return 5;}
    }
    QApplication app(argc,argv);
    bool const smoke=app.arguments().contains("--smoke");
    std::unique_ptr<northstarcpp::Logger> process_logger;
    if(!smoke || northstarcpp::application_log_directory_overridden()) {
        try {
            auto const logging=northstarcpp::application_logger_configuration(
                "NorthStarCpp-CameraEditor");
            if (logging.enabled) {
                northstarcpp::prepare_application_log_console(logging);
                process_logger=std::make_unique<northstarcpp::Logger>(logging.options);
                process_logger->info("camera","Camera Timeline Editor starting");
            }
        } catch (std::exception const& error) {
            std::fprintf(stderr,"Camera Timeline logger: %s\n",error.what());
        }
    }
    TimelineEditor window;
    if(argc==2&&argv[1][0]!='-') {
        try {window.load(QString::fromLocal8Bit(argv[1]));}
        catch(std::exception const& e) {
            QMessageBox::warning(&window,"Camera track",QString::fromUtf8(e.what()));
        }
    }
    if (!smoke)window.show();
    else QTimer::singleShot(0,&app,[&] {app.quit();});
    int const result=app.exec();
    if(process_logger) {
        try {
            if (result)
                process_logger->error("camera","Camera Timeline Editor exited with error");
            else
                process_logger->info("camera","Camera Timeline Editor closed");
            process_logger->flush();
        } catch(std::exception const&) {}
    }
    return result;
}
