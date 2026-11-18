#include "../engine/northstarcpp/modules/editor/qt_player_blueprint_editor.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QTabWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QTimer>
#include <QPushButton>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    northstarcpp::Scene scene;scene.version=3;scene.name="Player Blueprint GUI QA";
    auto ids=northstarcpp::player_blueprint::create(scene,"Hero",{0,0,0},true);
    auto const cameraIndex=northstarcpp::gameplay::entity_index(scene,ids.camera).value();
    scene.objects[cameraIndex].position={.625f,1.9f,-.5f};
    northstarcpp::player_blueprint::assign(scene,ids.controller,
        northstarcpp::player_blueprint::controller_type,
        northstarcpp::player_blueprint::serialize_controller(ids.pawn,false));
    bool editor_seen=false;
    QTimer::singleShot(0,&app,[&] {
        for(QWidget* w:QApplication::topLevelWidgets()) {
            auto* dlg=qobject_cast<QDialog*>(w);
            if(!dlg||dlg->objectName()!="playerBlueprintEditor")continue;
            editor_seen=true;
            auto* tabs=dlg->findChild<QTabWidget*>("playerBlueprintDetails");
            auto* startup=dlg->findChild<QCheckBox*>("playerControllerAutoPossess");
            auto* anchorX=dlg->findChild<QDoubleSpinBox*>("playerCameraAnchorX");
            if(!tabs || tabs->tabText(tabs->currentIndex())!="Camera" ||
               !startup || startup->isChecked() || !anchorX || anchorX->value()!=.625)
                throw std::runtime_error("Player Blueprint did not open selected camera or read authored controller and anchor");
            anchorX->setValue(.875);
            auto* walk=dlg->findChild<QDoubleSpinBox*>("playerWalkSpeed");
            auto* fov=dlg->findChild<QDoubleSpinBox*>("playerCameraFov");
            auto* camera=dlg->findChild<QComboBox*>("playerCameraMode");
            auto* model=dlg->findChild<QLineEdit*>("playerSkeletalMeshAsset");
            auto* prefab=dlg->findChild<QLineEdit*>("playerVisualPrefab");
            auto* anim=dlg->findChild<QLineEdit*>("playerAnimWalk");
            auto* controls=dlg->findChild<QDialogButtonBox*>("playerBlueprintButtons");
            if(!walk||!fov||!camera||!model||!prefab||!anim||!controls)
                throw std::runtime_error("Missing UE-style Player Blueprint controls");
            walk->setValue(4.75);
            fov->setValue(93);
            auto* spring=dlg->findChild<QDoubleSpinBox*>("playerCameraSpringArm");
            auto* shoulder=dlg->findChild<QDoubleSpinBox*>("playerCameraShoulder");
            auto* lag=dlg->findChild<QDoubleSpinBox*>("playerCameraLag");
            if(!spring||!shoulder||!lag||camera->currentIndex()!=0||
               spring->isEnabled()||shoulder->isEnabled()||lag->isEnabled())
                throw std::runtime_error("First Person camera shows inactive Third Person settings");
            camera->setCurrentIndex(1);
            if(!spring->isEnabled()||!shoulder->isEnabled()||!lag->isEnabled())
                throw std::runtime_error("Third Person camera controls did not activate");
            model->setText("models/npc_test.ydd");
            prefab->setText("prefabs/npc_test.nprefab");
            anim->setText("animations/npc_walk.ycd");
            controls->button(QDialogButtonBox::Save)->click();
            break;
        }
    });
    auto edited=northstarcpp::editor::player_editor::edit(nullptr,scene,ids.pawn,{},ids.camera);
    if(!editor_seen||!edited)throw std::runtime_error("Player editor did not save");
    auto layout=northstarcpp::player_blueprint::layout(*edited,ids.pawn);
    if(!layout||layout->camera!=ids.camera||layout->mesh!=ids.mesh||
       layout->motor!=ids.motor)
        throw std::runtime_error("GUI damaged player hierarchy");
    auto rig=northstarcpp::gameplay::camera_rig(*edited,ids.pawn);
    auto cam=northstarcpp::player_blueprint::camera(*edited,ids.camera);
    auto animation=northstarcpp::player_blueprint::animation(*edited,ids.mesh);
    auto const meshIndex=northstarcpp::gameplay::entity_index(*edited,ids.mesh);
    if(rig.fov_degrees!=93.f||
        northstarcpp::gameplay::motor_settings(*edited,ids.pawn).walk_speed!=4.75f||
       cam.mode!=northstarcpp::player_blueprint::ViewMode::ThirdPerson||
       !meshIndex||
       edited->objects[*meshIndex].node.asset!="models/npc_test.ydd"||
       northstarcpp::player_blueprint::mesh(*edited,ids.mesh).prefab!=
           "prefabs/npc_test.nprefab"||
       animation.walk!="animations/npc_walk.ycd")
        throw std::runtime_error("Player Blueprint UI failed to persist settings");
    auto const anchor=northstarcpp::gameplay::camera_anchor(*edited,ids.pawn,rig.eye_height);
    if(anchor.x!=.875f || anchor.y!=1.9f || anchor.z!=-.5f ||
       northstarcpp::scene_pawn::is_local(*edited,ids.pawn))
        throw std::runtime_error("Saving Player Blueprint changed untouched camera axes or possession");
    std::cout<<"Qt Player Blueprint Editor: camera/mesh/animation edits PASS\n";
    return 0;
}
