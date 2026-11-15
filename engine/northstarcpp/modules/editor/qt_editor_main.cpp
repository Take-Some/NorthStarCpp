#include "qt_editor_window.hpp"
#include "../shared/startup_report.hpp"
#include <northstarcpp/logger.hpp>
#include "qt_editor_viewport.hpp"
#include "qt_camera_studio.hpp"
#include <northstarcpp/camera.hpp>
#include "qt_material_gpu_preview.hpp"
#include "qt_material_graph_editor.hpp"
#include "qt_material_authoring.hpp"
#include "qt_material_instances.hpp"
#include <northstarcpp/editor_asset_catalog.hpp>
#include <northstarcpp/material.hpp>
#include <northstarcpp/assets.hpp>
#include <QApplication>
#include "qt_menu_theme.hpp"
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QTemporaryDir>
#include <QStatusBar>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QDialog>
#include <QFile>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QThread>
#include <cmath>
#include <stdexcept>
#include <QTimer>
#include <iostream>
#include <cstdlib>
#include <memory>
int run_material_function_smoke();
static int run_editor(int argc,char** argv,
    northstarcpp::diagnostics::StartupReport& startup) {
    auto app=startup.measure("qt.application", [&] {
        return std::make_unique<QApplication>(argc,argv);
    });
    app->setApplicationName("NorthStar Qt Editor");
    app->setOrganizationName("Kayla's Systems");
    northstarcpp::editor::installMenuTheme(
        *app,northstarcpp::editor::MenuTheme::Dark);
    QCommandLineParser parser;
    parser.setApplicationDescription("Cross-platform Qt 6 NorthStar editor shell");
    parser.addHelpOption();
    QCommandLineOption scene({"s","scene"},"Scene file to open (.nscn/.nscene).","file");
    QCommandLineOption smoke("smoke","Run dock/scene/binary/asset-drop smoke.");
    QCommandLineOption gpuSmoke("gpu-smoke",
        "Render one real engine.render GPU frame inside Qt native viewport.");
    QCommandLineOption mouseLookSmoke("mouse-look-smoke",
        "Verify Qt RMB fly and LMB orbit camera mouse look directions.");
    QCommandLineOption smokeOutput("smoke-output",
        "Save a valid binary scene at FILE for packaged-editor smoke.", "file");
    QCommandLineOption materialPreviewGpuSmoke("material-preview-gpu-smoke",
        "Render the isolated authored PBR material on the real engine.render surface.");
    QCommandLineOption materialInstanceSmoke("material-instance-smoke",
        "Test material instance inheritance, atomic save and cycle rejection.");
    QCommandLineOption materialGraphSmoke("material-graph-smoke",
        "Test Material Graph authoring, compilation and isolated GPU asset staging.");
    QCommandLineOption functionSmoke("material-function-smoke",
        "Run Material Function typed GUI/VFS/GPU preview end-to-end regression.");
    QCommandLineOption cameraStudioSmoke("camera-studio-smoke",
        "Verify Camera Studio project VFS presets, UI preview and atomic saving.");
    QCommandLineOption playerCheck("player-check",
        "Verify installed Player can load a scene authored by this editor.");
    parser.addOption(scene);
    parser.addOption(smoke);
    parser.addOption(gpuSmoke);
    parser.addOption(mouseLookSmoke);
    parser.addOption(smokeOutput);
    parser.addOption(playerCheck);
    parser.addOption(materialGraphSmoke);
    parser.addOption(functionSmoke);
    parser.addOption(cameraStudioSmoke);
    parser.addOption(materialInstanceSmoke);
    parser.addOption(materialPreviewGpuSmoke);
    startup.measure("qt.arguments", [&] {parser.process(*app);});
    auto window=startup.measure("qt.window", [&] {
        return std::make_unique<northstarcpp::editor::QtEditorWindow>();
    });
    if(parser.isSet(scene)) {
        auto const file=parser.value(scene).toUtf8().toStdString();
        startup.info("scene.selected","path="+file);
        try {
            startup.measure("scene.load", [&] {
                if(!window->openScene(parser.value(scene)))
                    throw std::runtime_error("Qt Editor could not load selected scene");
            });
        } catch(std::exception const&) {
            return 2;
        }
    }
    startup.measure("qt.window_show", [&] {window->show();});
    if(parser.isSet(mouseLookSmoke)) {
        bool passed=false;
        try {
            auto* viewport=window->findChild<
                northstarcpp::editor::QtEditorViewport*>("sceneViewport");
            if(!viewport)throw std::runtime_error("Qt Scene Viewport not found");
            QCoreApplication::processEvents();
            auto mouseGesture=[&](Qt::MouseButton button,QPointF from,QPointF to) {
                QMouseEvent press(QEvent::MouseButtonPress,from,
                    QPointF(viewport->mapToGlobal(from.toPoint())),
                    button,button,Qt::NoModifier);
                QCoreApplication::sendEvent(viewport,&press);
                QMouseEvent move(QEvent::MouseMove,to,
                    QPointF(viewport->mapToGlobal(to.toPoint())),
                    Qt::NoButton,button,Qt::NoModifier);
                QCoreApplication::sendEvent(viewport,&move);
                QMouseEvent release(QEvent::MouseButtonRelease,to,
                    QPointF(viewport->mapToGlobal(to.toPoint())),
                    button,Qt::NoButton,Qt::NoModifier);
                QCoreApplication::sendEvent(viewport,&release);
            };
            auto pose=viewport->camera().pose;
            pose.position={0.f,1.f,-10.f};
            pose.yaw=pose.pitch=0.f;
            viewport->setOrbitMode(false,{0,1,0},4.f);
            viewport->setCameraPose(pose);
            mouseGesture(Qt::RightButton,{260,180},{305,138});
            if(viewport->camera().pose.yaw<=.001f ||
               viewport->camera().pose.pitch<=.001f)
                throw std::runtime_error(
                    "RMB fly drag right/up uses inverted look direction");
            // Hover without a held mouse button must not rotate the view.
            auto const afterDrag=viewport->camera().pose;
            QMouseEvent hover(QEvent::MouseMove,{275,210},
                QPointF(viewport->mapToGlobal(QPoint(275,210))),
                Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QCoreApplication::sendEvent(viewport,&hover);
            if(std::abs(viewport->camera().pose.yaw-afterDrag.yaw)>1.e-6f ||
               std::abs(viewport->camera().pose.pitch-afterDrag.pitch)>1.e-6f)
                throw std::runtime_error("Camera rotates on passive hover");
            viewport->setOrbitMode(true,{0.f,1.f,0.f},4.f);
            viewport->setOrbitAngles(0.f,0.f);
            mouseGesture(Qt::LeftButton,{260,180},{305,138});
            auto const& orbit=viewport->camera();
            if(orbit.pose.yaw<=.001f || orbit.pose.pitch<=.001f)
                throw std::runtime_error(
                    "LMB orbit drag right/up uses inverted look direction");
            auto const f=orbit.forward();
            auto const d=viewport->orbitDistance();
            auto const pivot=northstarcpp::Vector3{
                orbit.pose.position.x+f.x*d,
                orbit.pose.position.y+f.y*d,
                orbit.pose.position.z+f.z*d};
            if(std::abs(pivot.x)>1.e-3f ||
               std::abs(pivot.y-1.f)>1.e-3f ||
               std::abs(pivot.z)>1.e-3f)
                throw std::runtime_error("Orbit target moved during mouse look");
            viewport->setOrbitMode(false,{0.f,1.f,0.f},4.f);
            // Unreal-style flight: RMB must be held; plain W/A/S/D/Q/E
            // must work without Ctrl. A standalone key press must not fly.
            auto keyboard=[&](int key,bool down,Qt::KeyboardModifiers modifiers=
                    Qt::NoModifier) {
                QKeyEvent evt(down?QEvent::KeyPress:QEvent::KeyRelease,
                              key,modifiers);
                QCoreApplication::sendEvent(viewport,&evt);
                return evt.isAccepted();
            };
            auto mouseButton=[&](bool down) {
                QMouseEvent evt(down?QEvent::MouseButtonPress:
                    QEvent::MouseButtonRelease,{260,180},
                    QPointF(viewport->mapToGlobal(QPoint(260,180))),
                    Qt::RightButton,
                    down?Qt::RightButton:Qt::NoButton,Qt::NoModifier);
                QCoreApplication::sendEvent(viewport,&evt);
            };
            viewport->setFocus(Qt::OtherFocusReason);
            viewport->setCameraPose(pose);
            keyboard(Qt::Key_W,true);
            viewport->advanceFly(.1f);
            keyboard(Qt::Key_W,false);
            if(viewport->camera().pose.position.z!=pose.position.z)
                throw std::runtime_error("W moves the camera without RMB");
            mouseButton(true);
            // Qt's ShortcutOverride must capture W before menu actions,
            // independent of Ctrl modifier.
            QKeyEvent shortcut(QEvent::ShortcutOverride,Qt::Key_W,Qt::NoModifier);
            shortcut.ignore();
            QCoreApplication::sendEvent(viewport,&shortcut);
            if(!shortcut.isAccepted())
                throw std::runtime_error("RMB+W not protected from menu shortcut");
            mouseButton(false);
            shortcut.ignore();
            QCoreApplication::sendEvent(viewport,&shortcut);
            if(shortcut.isAccepted())
                throw std::runtime_error("W shortcut captured outside flight");
            auto checkMovement=[&](int key,northstarcpp::Vector3 expected) {
                viewport->setCameraPose(pose);
                mouseButton(true);
                if(!keyboard(key,true))
                    throw std::runtime_error("RMB navigation key was rejected");
                viewport->advanceFly(.1f);
                keyboard(key,false);
                mouseButton(false);
                auto const now=viewport->camera().pose.position;
                auto const delta=northstarcpp::Vector3{now.x-pose.position.x,
                    now.y-pose.position.y,now.z-pose.position.z};
                auto const along=delta.x*expected.x+
                    delta.y*expected.y+delta.z*expected.z;
                if(along<=.005f)
                    throw std::runtime_error(
                        "RMB+key="+std::to_string(key)+
                        " wrong direction: dx="+std::to_string(delta.x)+
                        " dy="+std::to_string(delta.y)+
                        " dz="+std::to_string(delta.z));
                // Releasing RMB must also cancel held flight/inertia.
                viewport->advanceFly(.1f);
                auto const after=viewport->camera().pose.position;
                if(after.x!=now.x || after.y!=now.y || after.z!=now.z)
                    throw std::runtime_error("Flight continues after RMB release");
            };
            checkMovement(Qt::Key_W,{0.f,0.f,1.f});
            checkMovement(Qt::Key_S,{0.f,0.f,-1.f});
            checkMovement(Qt::Key_A,{-1.f,0.f,0.f});
            checkMovement(Qt::Key_D,{1.f,0.f,0.f});
            checkMovement(Qt::Key_Q,{0.f,-1.f,0.f});
            checkMovement(Qt::Key_E,{0.f,1.f,0.f});
#ifdef _WIN32
            // Windows physical scan code for the W key. Supports Cyrillic
            // keyboard layouts even when Qt's symbolic key is not Latin W.
            viewport->setCameraPose(pose);
            mouseButton(true);
            QKeyEvent nativeW(QEvent::KeyPress,Qt::Key_unknown,
                Qt::NoModifier,0x11,0,0);
            QCoreApplication::sendEvent(viewport,&nativeW);
            viewport->advanceFly(.1f);
            QKeyEvent nativeWRelease(QEvent::KeyRelease,Qt::Key_unknown,
                Qt::NoModifier,0x11,0,0);
            QCoreApplication::sendEvent(viewport,&nativeWRelease);
            mouseButton(false);
            if(viewport->camera().pose.position.z<=pose.position.z+.005f)
                throw std::runtime_error(
                    "Physical W scan code ignored in non-Latin keyboard layout");
#endif
            passed=true;
        }catch(std::exception const& error) {
            std::cerr<<"Qt camera mouse-look smoke failed: "<<error.what()
                     <<std::endl;
        }
        std::cout<<"Qt RMB fly + LMB orbit mouse-look direction: "
                 <<(passed?"PASS":"FAIL")<<std::endl;
        return passed?0:17;
    }
    if(parser.isSet(functionSmoke))
        return run_material_function_smoke();
    if(parser.isSet(cameraStudioSmoke)) {
        QTemporaryDir project;
        bool ok=project.isValid();
        QString error;
        try {
            if(!ok)throw std::runtime_error("No temporary camera project");
            auto const root=std::filesystem::u8path(
                (project.path()+"/assets").toUtf8().toStdString());
            auto* viewport=window->findChild<
                northstarcpp::editor::QtEditorViewport*>();
            if(!viewport)
                throw std::runtime_error("Qt Editor Viewport not available");
            if(!window->findChild<QAction*>("openCameraStudioAction"))
                throw std::runtime_error("Camera Studio View action absent");
            northstarcpp::editor::openCameraStudio(window.get(),viewport,root);
            QCoreApplication::processEvents();
            auto* dialog=window->findChild<QDialog*>("northstarCameraStudio");
            if(!dialog)
                throw std::runtime_error("Camera Studio dialog did not open");
            auto* id=dialog->findChild<QLineEdit*>("cameraPresetSource");
            auto* x=dialog->findChild<QDoubleSpinBox*>("cameraPresetField0");
            auto* fov=dialog->findChild<QDoubleSpinBox*>("cameraPresetField6");
            auto* priority=dialog->findChild<QSpinBox*>("cameraPresetPriority");
            auto* blend=dialog->findChild<QDoubleSpinBox*>("cameraPresetBlend");
            auto* preview=dialog->findChild<QPushButton*>("cameraPresetPreview");
            auto* store=dialog->findChild<QPushButton*>("cameraPresetStore");
            auto* save=dialog->findChild<QPushButton*>("cameraPresetSave");
            auto* reload=dialog->findChild<QPushButton*>("cameraPresetReload");
            if(!id||!x||!fov||!priority||!blend||
               !preview||!store||!save||!reload)
                throw std::runtime_error("Camera Studio controls not ready");
            id->setText("cinematic.qt_smoke");
            x->setValue(27.5);
            fov->setValue(58.0);
            priority->setValue(70);
            blend->setValue(.4);
            preview->click();
            QCoreApplication::processEvents();
            if(std::abs(viewport->camera().pose.position.x-27.5f)>.01f ||
               std::abs(viewport->camera().pose.fov_y_degrees-58.f)>.01f)
                throw std::runtime_error("Camera Studio preview did not drive viewport");
            store->click();
            save->click();
            QCoreApplication::processEvents();
            auto file=root/"gameplay"/"camera_presets.json";
            QFile disk(QString::fromUtf8(file.u8string().c_str()));
            if(!disk.open(QIODevice::ReadOnly))
                throw std::runtime_error("Camera Studio did not create project preset");
            auto bytes=disk.readAll();
            auto presets=northstarcpp::parse_camera_presets(std::string(
                bytes.constData(),std::size_t(bytes.size())));
            if(presets.size()!=1 ||
               presets.front().source!="cinematic.qt_smoke" ||
               presets.front().priority!=70 ||
               std::abs(presets.front().pose.position.x-27.5f)>.01f ||
               std::abs(presets.front().pose.fov_y_degrees-58.f)>.01f)
                throw std::runtime_error("Studio save/codec project roundtrip failed");
            reload->click();
            QCoreApplication::processEvents();
            auto* picker=dialog->findChild<QComboBox*>("cameraPresetPicker");
            if(!picker || picker->count()!=1 ||
               picker->currentText()!="cinematic.qt_smoke")
                throw std::runtime_error("Camera Studio reload did not preserve asset");

            auto* editTuning=dialog->findChild<QPushButton*>("cameraTuningOpen");
            if(!editTuning)
                throw std::runtime_error("Gameplay Camera Tuning action missing");
            editTuning->click();
            QCoreApplication::processEvents();
            auto* tuning=dialog->findChild<QDialog*>("northstarCameraTuning");
            if(!tuning)
                throw std::runtime_error("Gameplay Camera Tuning dialog missing");
            auto* frequency=tuning->findChild<QDoubleSpinBox*>("cameraTuningField9");
            auto* collisionRadius=tuning->findChild<QDoubleSpinBox*>(
                "cameraTuningField12");
            auto* tuningSave=tuning->findChild<QPushButton*>("cameraTuningSave");
            if(!frequency||!collisionRadius||!tuningSave)
                throw std::runtime_error("Gameplay tuning fields are incomplete");
            frequency->setValue(3.7);
            collisionRadius->setValue(.31);
            tuningSave->click();
            QCoreApplication::processEvents();
            auto tuningPath=root/"gameplay"/"camera.json";
            QFile tuningFile(QString::fromUtf8(tuningPath.u8string().c_str()));
            if(!tuningFile.open(QIODevice::ReadOnly))
                throw std::runtime_error("Gameplay Camera Tuning did not save");
            auto config=tuningFile.readAll();
            auto parsed=northstarcpp::parse_camera_settings(
                std::string(config.constData(),std::size_t(config.size())));
            if(std::abs(parsed.follow_spring_frequency_hz-3.7f)>.001f ||
               std::abs(parsed.collision_radius-.31f)>.001f)
                throw std::runtime_error(
                    "Gameplay Camera Tuning save did not retain native fields");
            dialog->close();
            ok=true;
        }catch(std::exception const& e) {
            ok=false;
            error=QString::fromUtf8(e.what());
        }
        std::cout<<"Qt Camera Studio Viewport/VFS/atomic-save: "
                 <<(ok?"PASS":"FAIL");
        if(!error.isEmpty())std::cout<<" "<<error.toStdString();
        std::cout<<std::endl;
        return ok?0:14;
    }

    if(parser.isSet(materialPreviewGpuSmoke)) {
        QTemporaryDir project;
        if(!project.isValid())return 10;
        northstarcpp::editor::MaterialGpuPreview preview(
            std::filesystem::u8path(project.path().toUtf8().toStdString()));
        preview.resize(620,560);
        preview.show();
        QJsonObject descriptor{
            {"schema",1},{"name","GpuPreviewAcceptance"},
            {"shader","standard_lit"},{"base_color","#a0b4c8"},
            {"roughness",0.4},{"metallic",0.18},
            {"graph",QJsonObject{
                {"nodes",QJsonArray{QJsonObject{
                    {"id","base"},{"op","parameter"},
                    {"parameter","base_color"},{"x",-140},{"y",100}
                }}},
                {"outputs",QJsonObject{{"base_color","base"}}}
            }}
        };
        preview.setMaterial(descriptor);
        QCoreApplication::processEvents();
        preview.start();
        QCoreApplication::processEvents();
        auto* previewViewport=preview.findChild<
            northstarcpp::editor::QtEditorViewport*>("materialGpuPreviewViewport");
        auto* grid=preview.findChild<QCheckBox*>("materialPreviewGrid");
        bool const orbit=previewViewport && previewViewport->orbitMode();
        if(previewViewport)previewViewport->setOrbitAngles(.35f,-.25f);
        if(grid)grid->setChecked(true);
        auto* intensity=preview.findChild<QDoubleSpinBox*>(
            "materialPreviewLightIntensity");
        if(intensity)intensity->setValue(6.0);
        QCoreApplication::processEvents();
        bool const ok=preview.gpuActive() && orbit && grid && grid->isChecked() &&
            intensity && intensity->value()==6.0 && previewViewport->renderGpuNow();
        std::cout<<"Material GPU Preview: "<<(ok?"PASS":"FAIL")
                 <<" | "<<preview.diagnostic().toStdString()<<std::endl;
        return ok?0:11;
    }
    if(parser.isSet(materialInstanceSmoke)) {
        QTemporaryDir project;
        bool ok=false;
        try {
            if(!project.isValid())
                throw std::runtime_error("No temporary project");
            auto const root=std::filesystem::u8path(
                (project.path()+"/assets").toUtf8().toStdString());
            namespace ma=northstarcpp::editor::material_authoring;
            QJsonObject const base{
                {"schema",1},{"name","Base"},{"shader","standard_lit"},
                {"base_color","#8090a0"},{"metallic",0.2},{"roughness",0.7}};
            ma::save_descriptor(root,"Materials/Base.material.json",base);
            QString const a=ma::create_material_instance(root,
                "Materials/Base.material.json","TestInstance");
            if(!ma::is_material(a) ||
               northstarcpp::editor::kind_from_extension(
                   std::filesystem::u8path(a.toUtf8().toStdString()))!=
                   northstarcpp::editor::AssetKind::material)
                throw std::runtime_error("Material Instance not indexed");
            auto instance=ma::load_material_instance(root,a);
            QJsonObject over{{"metallic",0.92}};
            instance["overrides"]=over;
            ma::save_material_instance(root,a,instance);
            auto inherited=ma::resolve_material_instance(root,a);
            if(inherited.value("metallic").toDouble()!=0.92 ||
               inherited.value("roughness").toDouble()!=0.7 ||
               inherited.value("base_color").toString()!="#8090a0")
                throw std::runtime_error("Material Instance inheritance failed");
            northstarcpp::AssetStore assets;
            assets.mount_directory("",root);
            auto gpuMaterial=northstarcpp::load_material_definition(
                assets,a.toUtf8().toStdString());
            if(std::abs(gpuMaterial.metallic-0.92f)>0.0001f ||
               std::abs(gpuMaterial.roughness-0.7f)>0.0001f)
                throw std::runtime_error("Material Instance runtime GPU factors wrong");
            QString const b=ma::create_material_instance(root,a,"ChildInstance");
            auto parent=ma::load_material_instance(root,a);
            parent["parent"]=b;
            bool rejected=false;
            try {
                ma::save_material_instance(root,a,parent);
            }catch(std::exception const&) {rejected=true;}
            if(!rejected || ma::load_material_instance(root,a).value("parent")
                             .toString()!="Materials/Base.material.json")
                throw std::runtime_error("Instance cycle not rejected atomically");
            ok=true;
        }catch(std::exception const& e){
            std::cerr<<"Material Instance smoke: "<<e.what()<<std::endl;
        }
        std::cout<<"Material Instance source/override/cycle: "
                 <<(ok?"PASS":"FAIL")<<std::endl;
        return ok?0:12;
    }
    if(parser.isSet(materialGraphSmoke)) {
        QTemporaryDir project;
        bool ok=project.isValid();
        QString diagnostic;
        try {
            if(!ok)throw std::runtime_error("Cannot create test content root");
            // The test exercises real Qt Graph Editor and descriptor
            // validation without requiring a swapchain or an installed GPU.
            northstarcpp::editor::MaterialGraphEditor graph;
            if(graph.enabled())throw std::runtime_error("Graph opt-in lost");
            graph.addAt(QPointF(25,260));
            int const sourcePin=graph.pinAt(QPointF(237,304));
            int const outputPin=graph.pinAt(QPointF(625,140));
            if(sourcePin<0 || outputPin<0)
                throw std::runtime_error("Graph pins were not created");
            graph.connectPins(sourcePin,outputPin);
            if(graph.graph().value("outputs").toObject()
                .value("base_color").toString()!="node_1")
                throw std::runtime_error("Graph wire binding was not serialized");
            if(!graph.enabled() || !graph.validate(&diagnostic))
                throw std::runtime_error("Graph editor emitted invalid JSON");
            auto authored=graph.graph();
            if(authored.value("nodes").toArray().size()!=2)
                throw std::runtime_error("Graph node addition was not serialized");
            auto* canvas=graph.findChild<QGraphicsView*>("materialGraphCanvas");
            if(!canvas)throw std::runtime_error("Material Graph canvas not present");
            bool selected=false;
            for(auto* item:canvas->scene()->items()) {
                if(item->flags()&QGraphicsItem::ItemIsSelectable) {
                    item->setSelected(true);selected=true;break;
                }
            }
            if(!selected)throw std::runtime_error("Cannot select a graph node");
            graph.copySelection();
            graph.pasteSelection();
            authored=graph.graph();
            if(authored.value("nodes").toArray().size()!=3 ||
               !graph.validate(&diagnostic))
                throw std::runtime_error("Graph clipboard roundtrip invalid");
            graph.createCommentGroup();
            authored=graph.graph();
            if(authored.value("groups").toArray().size()!=1 ||
               !graph.validate(&diagnostic))
                throw std::runtime_error("Graph comment grouping invalid");
            auto before=authored.value("groups").toArray()[0].toObject();
            auto const members=before.value("members").toArray();
            if(members.isEmpty())
                throw std::runtime_error("Comment group has no member nodes");
            QString const groupedNode=members.at(0).toString();
            auto lookupX=[&](QJsonObject const& data)->double {
                for(auto const& raw:data.value("nodes").toArray()) {
                    auto const node=raw.toObject();
                    if(node.value("id").toString()==groupedNode)
                        return node.value("x").toDouble();
                }
                throw std::runtime_error("Comment group member missing");
            };
            double const memberX=lookupX(authored);
            bool moved=false;
            for(auto* item:canvas->scene()->items()) {
                auto* box=dynamic_cast<QGraphicsRectItem*>(item);
                if(!box || box->pen().style()!=Qt::DashLine)continue;
                box->setPos(box->pos()+QPointF(24.,12.));
                moved=true;break;
            }
            if(!moved)throw std::runtime_error("Graph group was not rendered");
            graph.moveFinished();
            authored=graph.graph();
            auto after=authored.value("groups").toArray()[0].toObject();
            if(after.value("x").toDouble()!=before.value("x").toDouble()+24. ||
               after.value("y").toDouble()!=before.value("y").toDouble()+12. ||
               lookupX(authored)!=memberX+24.)
                throw std::runtime_error("Group/member transform not serialized");
            QJsonObject descriptor{
                {"schema",1},{"name","PreviewAcceptance"},
                {"shader","standard_lit"},{"base_color","#8090a0"},
                {"roughness",0.35},{"metallic",0.1},{"graph",authored}};
            northstarcpp::editor::MaterialGpuPreview preview(
                std::filesystem::u8path(project.path().toUtf8().toStdString()));
            if(!preview.stageForTest(descriptor,&diagnostic))
                throw std::runtime_error("Isolated material preview staging failed: "+
                    diagnostic.toStdString());
            QJsonObject invalid=descriptor;
            auto cycle=authored;
            QJsonArray badNodes{
                QJsonObject{{"id","loop"},{"op","one_minus"},{"a","loop"}}};
            cycle.insert("nodes",badNodes);
            cycle.insert("outputs",QJsonObject{{"base_color","loop"}});
            invalid.insert("graph",cycle);
            if(preview.stageForTest(invalid,&diagnostic))
                throw std::runtime_error("Cyclic graph accepted by descriptor validation");
            // Exercise the real debounced QtConcurrent worker (no GPU required).
            preview.setMaterial(invalid);
            QElapsedTimer elapsed;
            elapsed.start();
            while(!preview.diagnostic().contains("Graph compiler rejected") &&
                  elapsed.elapsed()<6000) {
                QCoreApplication::processEvents();
                QThread::msleep(8);
            }
            if(!preview.diagnostic().contains("Graph compiler rejected"))
                throw std::runtime_error("Async graph compiler did not report cycle");
            ok=true;
        } catch(std::exception const& e) {
            std::cerr<<"Material Graph smoke: "<<e.what()<<std::endl;
            ok=false;
        }
        std::cout<<"Qt Material Graph / isolated preview smoke: "
                 <<(ok?"PASS":"FAIL")<<std::endl;
        return ok?0:9;
    }
    if(parser.isSet(smokeOutput)) {
        if(!parser.isSet(smoke)) {
            std::cerr<<"--smoke-output requires --smoke"<<std::endl;return 5;
        }
        bool ok=window->saveScene(parser.value(smokeOutput));
        QCoreApplication::processEvents();
        std::cout<<"Qt editor binary scene smoke: "<<(ok?"PASS":"FAIL")
                 <<std::endl;
        return ok?0:6;
    }
    if(parser.isSet(gpuSmoke)) {
        QTemporaryDir temp;
        bool ok=temp.isValid() && window->runGpuSmoke(temp.path());
        QCoreApplication::processEvents();
        std::cout<<"Qt engine.render native GPU viewport: "
                 <<(ok?"PASS":"FAIL")<<std::endl;
        return ok?0:8;
    }
    if(parser.isSet(playerCheck)) {
        QTemporaryDir temp;
        return temp.isValid() && window->runPlayerCheckSmoke(temp.path())?0:7;
    }
    if(parser.isSet(smoke)) {
        QTemporaryDir temporary;
        if(!temporary.isValid())return 3;
        bool ok=window->runSmoke(temporary.path());
        QCoreApplication::processEvents();
        if(!ok)std::cerr<<"Qt editor smoke failed: "<<window->statusBar()->currentMessage().toStdString()<<std::endl;
        std::cout<<"Qt unified editor + scene + asset placement + undo/redo: "
                 <<(ok?"PASS":"FAIL")<<std::endl;
        return ok?0:4;
    }
    startup.complete(startup.warning_count()?"READY_WITH_WARNINGS":"READY");
    return app->exec();
}

int main(int argc,char** argv) {
    northstarcpp::diagnostics::StartupReport startup;
    startup.info("process.begin","host=qt_editor");
    std::unique_ptr<northstarcpp::Logger> logger;
    try {
        startup.measure("logger.initialize", [&] {
            auto const configuration=northstarcpp::application_logger_configuration(
                "NorthStarCpp-QtEditor");
            if(!configuration.enabled) {
                startup.warning("logger.initialize","logging disabled by bootstrap.json");
                return;
            }
            northstarcpp::prepare_application_log_console(configuration);
            logger=std::make_unique<northstarcpp::Logger>(configuration.options);
            auto folder=configuration.options.directory;
            if(folder.empty()) {
                if(auto const* local=std::getenv("LOCALAPPDATA"))
                    folder=std::filesystem::u8path(local)/
                        "KaylasSystems"/"NorthStarCpp"/"Logs";
                else folder=std::filesystem::temp_directory_path()/
                    "KaylasSystems"/"NorthStarCpp"/"Logs";
            }
            startup.set_report_path(folder/
                ("NorthStarCpp-QtEditor-"+std::to_string(QCoreApplication::applicationPid())+
                    "-startup.json"));
        });
        if(logger) {
            startup.set_sink([&](auto severity,std::string const& message) {
                using Severity=northstarcpp::diagnostics::StartupReport::Severity;
                logger->write(severity==Severity::error?northstarcpp::LogLevel::error:
                    severity==Severity::warning?northstarcpp::LogLevel::warn:
                        northstarcpp::LogLevel::info,"engine.startup",message);
            });
            startup.info("logger.provider","selected="+logger->provider_name()+
                " version="+logger->provider_version());
        }
    } catch(std::exception const& error) {
        startup.warning("logger.initialize",error.what());
    }
    try {
        auto const exit_code=run_editor(argc,argv,startup);
        if(!startup.completed())
            startup.complete(exit_code?"FAILED":
                (startup.warning_count()?"READY_WITH_WARNINGS":"READY"));
        if(logger)logger->flush();
        return exit_code;
    } catch(std::exception const& error) {
        startup.failure("qt.bootstrap",error.what());
        startup.complete("FAILED");
        if(logger) {
            try {logger->flush();}catch(...) {}
        }
        std::fprintf(stderr,"Qt editor startup failed: %s\n",error.what());
        return 1;
    }
}
