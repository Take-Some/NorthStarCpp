#include "qt_camera_studio.hpp"
#include "qt_cutscene_timeline.hpp"
#include "qt_editor_viewport.hpp"
#include <northstarcpp/camera.hpp>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollArea>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace northstarcpp::editor {
namespace {
constexpr float radians=0.01745329251994329577f;
constexpr float degrees=57.295779513082320876f;
QString fromPath(std::filesystem::path const& p) {
    auto const bytes=p.u8string();
    return QString::fromUtf8(bytes.data(),int(bytes.size()));
}
struct Studio {
    std::filesystem::path path;
    std::vector<CameraShot> shots;
    std::filesystem::file_time_type file_stamp{};
    bool have_stamp=false;
    bool dirty=false;
    QComboBox* picker=nullptr;
    QLineEdit* source=nullptr;
    std::array<QDoubleSpinBox*,9> pose{}; // xyz, yaw/pitch/roll, fov, near/far
    QSpinBox* priority=nullptr;
    QDoubleSpinBox* blend=nullptr;
    QLabel* status=nullptr;

    void load() {
        shots.clear();
        have_stamp=false;
        dirty=false;
        std::error_code ec;
        if(path.empty() || !std::filesystem::is_regular_file(path,ec))
            return;
        QFile file(fromPath(path));
        if(!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("Cannot open project camera preset file");
        auto bytes=file.readAll();
        if(bytes.size()>65536)
            throw std::length_error("Camera preset file exceeds 64 KiB");
        shots=parse_camera_presets(std::string(bytes.constData(),
                                               std::size_t(bytes.size())));
        file_stamp=std::filesystem::last_write_time(path,ec);
        have_stamp=!ec;
    }
    void refresh(QString const& selection={}) {
        QSignalBlocker block(picker);
        picker->clear();
        for(auto const& shot:shots)
            picker->addItem(QString::fromUtf8(shot.source.c_str()));
        auto const row=picker->findText(selection);
        if(row>=0)picker->setCurrentIndex(row);
        else if(!shots.empty())picker->setCurrentIndex(0);
        else picker->setCurrentIndex(-1);
        status->setText(QString("%1 camera presets%2")
            .arg(shots.size()).arg(dirty?" (unsaved changes)":""));
    }
    void fill(CameraShot const& shot) {
        QSignalBlocker blocker(source);
        source->setText(QString::fromUtf8(shot.source.c_str()));
        std::array<float,9> values{
            shot.pose.position.x,shot.pose.position.y,shot.pose.position.z,
            shot.pose.yaw*degrees,shot.pose.pitch*degrees,
            shot.pose.roll*degrees,shot.pose.fov_y_degrees,
            shot.pose.near_plane,shot.pose.far_plane};
        for(std::size_t i=0;i<pose.size();++i) {
            QSignalBlocker silence(pose[i]);
            pose[i]->setValue(values[i]);
        }
        priority->setValue(shot.priority);
        blend->setValue(shot.blend_seconds);
    }
    CameraShot selected() const {
        CameraShot result;
        result.source=source->text().trimmed().toUtf8().toStdString();
        result.priority=priority->value();
        result.blend_seconds=float(blend->value());
        result.pose.position={
            float(pose[0]->value()),float(pose[1]->value()),float(pose[2]->value())};
        result.pose.yaw=float(pose[3]->value())*radians;
        result.pose.pitch=float(pose[4]->value())*radians;
        result.pose.roll=float(pose[5]->value())*radians;
        result.pose.fov_y_degrees=float(pose[6]->value());
        result.pose.near_plane=float(pose[7]->value());
        result.pose.far_plane=float(pose[8]->value());
        validate_camera_shot(result);
        return result;
    }
    void save() {
        if(path.empty())
            throw std::runtime_error("Open a project scene before saving camera presets");
        auto bytes=serialize_camera_presets(shots);
        std::error_code ec;
        if(std::filesystem::exists(path,ec)) {
            auto now=std::filesystem::last_write_time(path,ec);
            if(!ec && (!have_stamp || now!=file_stamp))
                throw std::runtime_error(
                    "Camera preset file changed externally; Reload before saving");
        }
        if(!QDir().mkpath(fromPath(path.parent_path())))
            throw std::runtime_error("Cannot create project gameplay folder");
        QSaveFile file(fromPath(path));
        if(!file.open(QIODevice::WriteOnly))
            throw std::runtime_error("Cannot open atomic camera preset save");
        if(file.write(bytes.data(),qint64(bytes.size()))!=qint64(bytes.size()) ||
           !file.commit())
            throw std::runtime_error("Could not commit camera presets to project");
        file_stamp=std::filesystem::last_write_time(path,ec);
        have_stamp=!ec;
        dirty=false;
        refresh(picker->currentText());
    }
};

void fail(QWidget* parent,std::exception const& e) {
    QMessageBox::warning(parent,"Camera Studio",QString::fromUtf8(e.what()));
}
void openGameplayTuning(QWidget* parent,std::filesystem::path const& assets) {
    if(assets.empty()) {
        QMessageBox::information(parent,"Camera Studio",
            "Open a project scene before editing gameplay camera settings.");
        return;
    }
    if(auto* existing=parent->findChild<QDialog*>("northstarCameraTuning")) {
        existing->raise();existing->activateWindow();
        return;
    }
    auto const path=assets/"gameplay"/"camera.json";
    QJsonObject document{{"schema",1}};
    std::error_code error;
    bool const existed=std::filesystem::is_regular_file(path,error);
    auto const stamp=existed?std::filesystem::last_write_time(path,error):
        std::filesystem::file_time_type{};
    try {
        if(existed) {
            QFile file(fromPath(path));
            if(!file.open(QIODevice::ReadOnly))
                throw std::runtime_error("Cannot read gameplay camera tuning");
            auto const contents=file.readAll();
            if(contents.size()>16384)
                throw std::length_error("Camera tuning exceeds 16 KiB");
            QJsonParseError parsing;
            auto const result=QJsonDocument::fromJson(contents,&parsing);
            if(parsing.error!=QJsonParseError::NoError || !result.isObject())
                throw std::invalid_argument("Gameplay camera JSON is invalid");
            document=result.object();
        }
        auto const initial=QJsonDocument(document).toJson(QJsonDocument::Compact);
        auto const settings=parse_camera_settings(std::string(
            initial.constData(),std::size_t(initial.size())));

        auto* dialog=new QDialog(parent);
        dialog->setObjectName("northstarCameraTuning");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle("Gameplay Camera — Follow, Vehicle & Collision");
        dialog->resize(460,590);
        auto* layout=new QVBoxLayout(dialog);
        auto* scroll=new QScrollArea(dialog);
        scroll->setWidgetResizable(true);
        auto* inner=new QWidget(scroll);
        auto* form=new QFormLayout(inner);
        struct Field {
            char const* label;
            char const* path;
            float value,minimum,maximum,step;
        };
        std::array<Field,16> const fields{{
            {"Third-person distance","third_person.distance",
                settings.third_person_distance,.5f,100.f,.25f},
            {"Third-person height","third_person.height",
                settings.third_person_height,-10.f,20.f,.1f},
            {"Shoulder offset","third_person.shoulder_offset",
                settings.shoulder_offset,-10.f,10.f,.1f},
            {"Third-person look-ahead (s)","third_person.lookahead_seconds",
                settings.third_person_lookahead_seconds,0.f,2.f,.02f},
            {"Third-person max lead (m)","third_person.lookahead_max",
                settings.third_person_lookahead_max,0.f,20.f,.25f},
            {"Vehicle chase distance","vehicle.distance",
                settings.vehicle_distance,.5f,100.f,.5f},
            {"Vehicle height","vehicle.height",
                settings.vehicle_height,-30.f,30.f,.25f},
            {"Vehicle lead (s)","vehicle.lookahead_seconds",
                settings.vehicle_lookahead_seconds,0.f,2.f,.02f},
            {"Follow smoothing (s)","smoothing.seconds",
                settings.follow_smoothing,0.f,10.f,.02f},
            {"Spring frequency (Hz)","smoothing.spring_frequency_hz",
                settings.follow_spring_frequency_hz,0.f,20.f,.25f},
            {"Lead smoothing (s)","smoothing.lookahead_seconds",
                settings.lookahead_damping_seconds,0.f,2.f,.02f},
            {"Teleport cut threshold (m)","smoothing.teleport_cut_distance",
                settings.teleport_cut_distance,1.f,10000.f,1.f},
            {"Collision radius (m)","collision.radius",
                settings.collision_radius,0.f,2.f,.05f},
            {"Collision padding (m)","collision.padding",
                settings.collision_padding,0.f,1.f,.02f},
            {"Collision recovery (s)","collision.restore_seconds",
                settings.collision_restore_seconds,0.f,4.f,.05f},
            {"Default FOV (deg)","projection.fov_y_degrees",
                settings.default_fov_y,25.f,120.f,2.f}
        }};
        std::array<QDoubleSpinBox*,16> controls{};
        for(std::size_t i=0;i<fields.size();++i) {
            auto* input=new QDoubleSpinBox(inner);
            input->setObjectName(QString("cameraTuningField%1").arg(i));
            input->setRange(fields[i].minimum,fields[i].maximum);
            input->setSingleStep(fields[i].step);
            input->setDecimals(3);
            input->setValue(fields[i].value);
            input->setToolTip(fields[i].path);
            form->addRow(fields[i].label,input);
            controls[i]=input;
        }
        scroll->setWidget(inner);
        layout->addWidget(scroll,1);
        auto* save=new QPushButton("Validate and Save",dialog);
        save->setObjectName("cameraTuningSave");
        layout->addWidget(save);
        QObject::connect(save,&QPushButton::clicked,dialog,
            [dialog,path,existed,stamp,document,fields,controls] {
                try {
                    auto updated=document;
                    for(std::size_t i=0;i<fields.size();++i) {
                        auto const parts=QString::fromUtf8(fields[i].path)
                            .split('.');
                        auto parent=updated.value(parts[0]).toObject();
                        parent.insert(parts[1],controls[i]->value());
                        updated.insert(parts[0],parent);
                    }
                    auto const bytes=QJsonDocument(updated).toJson(
                        QJsonDocument::Indented);
                    (void)parse_camera_settings(std::string(
                        bytes.constData(),std::size_t(bytes.size())));
                    std::error_code ec;
                    auto const present=std::filesystem::exists(path,ec);
                    if(ec || present!=existed ||
                       (present &&
                        std::filesystem::last_write_time(path,ec)!=stamp) || ec)
                        throw std::runtime_error(
                            "Gameplay camera settings changed externally; reopen before saving");
                    if(!QDir().mkpath(fromPath(path.parent_path())))
                        throw std::runtime_error("Cannot create gameplay folder");
                    QSaveFile out(fromPath(path));
                    if(!out.open(QIODevice::WriteOnly) ||
                       out.write(bytes)!=bytes.size() || !out.commit())
                        throw std::runtime_error("Atomic camera settings save failed");
                    dialog->accept();
                }catch(std::exception const& e) {fail(dialog,e);}
            });
        dialog->show();
    }catch(std::exception const& e) {fail(parent,e);}
}
} // namespace

void openCameraStudio(QWidget* parent,QtEditorViewport* viewport,
                      std::filesystem::path const& project_assets) {
    if(!parent || !viewport)return;
    if(auto* old=parent->findChild<QDialog*>("northstarCameraStudio")) {
        old->show();old->raise();old->activateWindow();
        return;
    }
    auto* dialog=new QDialog(parent);
    dialog->setObjectName("northstarCameraStudio");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("NorthStar Camera Studio — Project Presets");
    dialog->resize(440,650);
    dialog->setMinimumWidth(370);
    auto state=std::make_shared<Studio>();
    if(!project_assets.empty())
        state->path=project_assets/"gameplay"/"camera_presets.json";

    auto* body=new QVBoxLayout(dialog);
    auto* form=new QFormLayout();
    state->picker=new QComboBox(dialog);
    state->picker->setObjectName("cameraPresetPicker");
    form->addRow("Preset",state->picker);
    state->source=new QLineEdit(dialog);
    state->source->setObjectName("cameraPresetSource");
    state->source->setMaxLength(80);
    state->source->setPlaceholderText("cinematic.scene_intro");
    form->addRow("Source ID",state->source);
    std::array<QString,9> titles{
        "Position X (m)","Position Y (m)","Position Z (m)",
        "Yaw (deg)","Pitch (deg)","Roll (deg)",
        "Vertical FOV (deg)","Near plane (m)","Far plane (m)"
    };
    for(std::size_t i=0;i<state->pose.size();++i) {
        auto* spin=new QDoubleSpinBox(dialog);
        spin->setObjectName(QString("cameraPresetField%1").arg(i));
        spin->setDecimals(i>=6?3:2);
        spin->setSingleStep(i==6?2.:i>=7?.01:1.);
        if(i<=2)spin->setRange(-1000000.,1000000.);
        else if(i==3||i==5)spin->setRange(-180.,180.);
        else if(i==4)spin->setRange(-89.,89.);
        else if(i==6)spin->setRange(25.,120.);
        else if(i==7)spin->setRange(.01,2.);
        else spin->setRange(1.02,10000.);
        form->addRow(titles[i],spin);
        state->pose[i]=spin;
    }
    state->priority=new QSpinBox(dialog);
    state->priority->setObjectName("cameraPresetPriority");
    state->priority->setRange(-100,100);
    form->addRow("Priority",state->priority);
    state->blend=new QDoubleSpinBox(dialog);
    state->blend->setObjectName("cameraPresetBlend");
    state->blend->setRange(0.,10.);
    state->blend->setDecimals(2);
    state->blend->setSuffix(" s");
    form->addRow("Blend",state->blend);
    body->addLayout(form);
    state->status=new QLabel(dialog);
    state->status->setWordWrap(true);
    state->status->setObjectName("cameraStudioStatus");
    body->addWidget(state->status);
    auto* capture=new QPushButton("Capture View",dialog);
    capture->setObjectName("cameraPresetCapture");
    auto* preview=new QPushButton("Preview Camera",dialog);
    preview->setObjectName("cameraPresetPreview");
    auto* store=new QPushButton("Store / Update",dialog);
    store->setObjectName("cameraPresetStore");
    auto* erase=new QPushButton("Delete Preset",dialog);
    erase->setObjectName("cameraPresetDelete");
    auto* save=new QPushButton("Save Project Presets",dialog);
    save->setObjectName("cameraPresetSave");
    auto* reload=new QPushButton("Reload File",dialog);
    reload->setObjectName("cameraPresetReload");
    auto* close=new QPushButton("Close",dialog);
    auto addRow=[&](QPushButton* first,QPushButton* second) {
        auto* row=new QHBoxLayout;
        row->addWidget(first);row->addWidget(second);
        body->addLayout(row);
    };
    addRow(capture,preview);
    addRow(store,erase);
    addRow(save,reload);
    auto* tuning=new QPushButton("Gameplay Camera Tuning...",dialog);
    tuning->setObjectName("cameraTuningOpen");
    QObject::connect(tuning,&QPushButton::clicked,dialog,
        [dialog,project_assets] {
            openGameplayTuning(dialog,project_assets);
        });
    body->addWidget(tuning);
    auto* sequencer=new QPushButton("Multi-track Cutscene Sequencer...",dialog);
    sequencer->setObjectName("cameraCutsceneSequencerOpen");
    QObject::connect(sequencer,&QPushButton::clicked,dialog,
        [dialog,viewport,project_assets] {
            openCutsceneTimeline(dialog,project_assets,
                [viewport](CameraPose const& pose) {
                    viewport->setOrbitMode(false);
                    viewport->setCameraPose(pose);
                });
        });
    body->addWidget(sequencer);
    body->addWidget(close);
    auto fillCurrent=[state,viewport] {
        CameraShot shot;
        shot.source=state->source->text().trimmed().toUtf8().toStdString();
        if(shot.source.empty())shot.source="cinematic.new_shot";
        shot.pose=viewport->camera().pose;
        shot.priority=state->priority->value();
        shot.blend_seconds=float(state->blend->value());
        state->fill(shot);
    };
    QObject::connect(state->picker,QOverload<int>::of(&QComboBox::currentIndexChanged),
        dialog,[state](int index) {
            if(index>=0 && std::size_t(index)<state->shots.size())
                state->fill(state->shots[std::size_t(index)]);
        });
    QObject::connect(capture,&QPushButton::clicked,dialog,fillCurrent);
    QObject::connect(preview,&QPushButton::clicked,dialog,[state,viewport,dialog]{
        try {
            auto shot=state->selected();
            viewport->setOrbitMode(false);
            viewport->setCameraPose(shot.pose);
            state->status->setText("Camera pose applied to current Editor Viewport");
        }catch(std::exception const& error){fail(dialog,error);}
    });
    QObject::connect(store,&QPushButton::clicked,dialog,[state,dialog]{
        try {
            auto shot=state->selected();
            auto it=std::find_if(state->shots.begin(),state->shots.end(),
                [&](CameraShot const& candidate){return candidate.source==shot.source;});
            if(it!=state->shots.end())*it=shot;
            else {
                if(state->shots.size()>=32)
                    throw std::length_error("Camera preset limit: 32");
                state->shots.push_back(shot);
            }
            state->dirty=true;
            state->refresh(QString::fromUtf8(shot.source.c_str()));
        }catch(std::exception const& error){fail(dialog,error);}
    });
    QObject::connect(erase,&QPushButton::clicked,dialog,[state]{
        auto const source=state->picker->currentText().toUtf8().toStdString();
        if(source.empty())return;
        state->shots.erase(std::remove_if(state->shots.begin(),state->shots.end(),
            [&](CameraShot const& shot){return shot.source==source;}),
            state->shots.end());
        state->dirty=true;
        state->refresh();
    });
    QObject::connect(save,&QPushButton::clicked,dialog,[state,dialog]{
        try {
            state->save();
            state->status->setText("Presets saved atomically to project VFS");
        }catch(std::exception const& error){fail(dialog,error);}
    });
    QObject::connect(reload,&QPushButton::clicked,dialog,[state,dialog] {
        if(state->dirty && QMessageBox::question(
            dialog,"Reload Camera Presets",
            "Discard unsaved camera preset changes?")!=QMessageBox::Yes)return;
        try {
            state->load();
            state->refresh();
            if(!state->shots.empty())state->fill(state->shots.front());
        }catch(std::exception const& error){fail(dialog,error);}
    });
    QObject::connect(close,&QPushButton::clicked,dialog,&QDialog::close);
    try {
        state->load();
        state->refresh();
        if(!state->shots.empty())state->fill(state->shots.front());
        else fillCurrent();
    }catch(std::exception const& error) {
        state->status->setText("Preset file warning: "+
                               QString::fromUtf8(error.what()));
        fillCurrent();
    }
    save->setEnabled(!project_assets.empty());
    dialog->show();
}
} // namespace northstarcpp::editor
