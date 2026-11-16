// NorthStarCpp: UE-style component Player Blueprint editor.
// Edits a transactionally validated Scene copy; caller owns Undo/Redo and save.
#pragma once
#include <northstarcpp/player_blueprint.hpp>
#include "editor_visual_prefab.hpp"
#include <northstarcpp/player_controller.hpp>
#include <northstarcpp/collision_authoring.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
namespace northstarcpp::editor::player_editor {
inline QDoubleSpinBox* number(QWidget* parent,char const* name,
    float value,double minimum,double maximum,double step=.1,int precision=2) {
    auto* field=new QDoubleSpinBox(parent);
    field->setObjectName(name);
    field->setDecimals(precision);
    field->setRange(minimum,maximum);
    field->setSingleStep(step);
    field->setValue(value);
    return field;
}
inline std::optional<Scene> edit(QWidget* owner,Scene const& source,
                                std::uint64_t pawn_id,
                                std::filesystem::path const& project_root={}) {
    auto const layout=player_blueprint::layout(source,pawn_id);
    if(!layout || !layout->camera || !layout->mesh)
        throw std::invalid_argument("Select a Player Blueprint Pawn with Camera and SkeletalMesh");
    Scene candidate=source;
    QDialog dialog(owner);
    dialog.setObjectName("playerBlueprintEditor");
    dialog.setWindowTitle("NorthStar Player Blueprint Editor");
    dialog.resize(1200,790);
    dialog.setMinimumSize(940,590);
    auto* root=new QVBoxLayout(&dialog);
    auto* title=new QLabel(
        "Player Blueprint    |    Controller  вЂў  Pawn  вЂў  Camera  вЂў  SkeletalMesh  вЂў  Animation",&dialog);
    title->setObjectName("playerBlueprintBreadcrumb");
    root->addWidget(title);
    auto* split=new QSplitter(Qt::Horizontal,&dialog);
    root->addWidget(split,1);
    auto* hierarchy=new QTreeWidget(split);
    hierarchy->setObjectName("playerBlueprintComponents");
    hierarchy->setHeaderLabel("Components / Entities");
    hierarchy->setMinimumWidth(250);
    auto* controller=new QTreeWidgetItem(hierarchy,{
        QString("PlayerController  [%1]").arg(layout->controller)});
    controller->setData(0,Qt::UserRole,0);
    auto* pawn=new QTreeWidgetItem(hierarchy,{
        QString("Character Pawn  [%1]").arg(layout->pawn)});
    pawn->setData(0,Qt::UserRole,1);
    auto* motor=new QTreeWidgetItem(pawn,{
        QString("CharacterMotor  [%1]").arg(layout->motor)});
    motor->setData(0,Qt::UserRole,1);
    auto* camera=new QTreeWidgetItem(pawn,{
        QString("Camera  [%1]").arg(layout->camera)});
    camera->setData(0,Qt::UserRole,2);
    auto* mesh=new QTreeWidgetItem(pawn,{
        QString("SkeletalMesh  [%1]").arg(layout->mesh)});
    mesh->setData(0,Qt::UserRole,3);
    auto* anim=new QTreeWidgetItem(mesh,{"Animation Controller"});
    anim->setData(0,Qt::UserRole,4);
    hierarchy->expandAll();
    auto* details=new QTabWidget(split);
    details->setObjectName("playerBlueprintDetails");
    split->setStretchFactor(1,1);
    QObject::connect(hierarchy,&QTreeWidget::currentItemChanged,&dialog,
        [details](QTreeWidgetItem* current,QTreeWidgetItem*) {
            if(current)details->setCurrentIndex(
                current->data(0,Qt::UserRole).toInt());
        });

    auto* controls=new QWidget(details);
    auto* controlsForm=new QFormLayout(controls);
    auto* local=new QCheckBox("Auto Possess on Play",controls);
    local->setObjectName("playerControllerAutoPossess");
    auto* pawnMetadata=player_blueprint::find(
        candidate,layout->pawn,player_character::pawn_type);
    local->setChecked(pawnMetadata && pawnMetadata->payload.size()==12 &&
        player_character::detail::get32(pawnMetadata->payload.data(),4)!=0);
    controlsForm->addRow("Possession",local);
    controlsForm->addRow(new QLabel(
        "PlayerController is independent of the Pawn. Possess/Unpossess switches "
        "the controlled stable Entity ID without replacing the model.",controls));
    details->addTab(controls,"Controller");

    auto* physics=new QWidget(details);
    auto* physicsForm=new QFormLayout(physics);
    auto tune=gameplay::motor_settings(candidate,layout->pawn);
    auto* walk=number(physics,"playerWalkSpeed",tune.walk_speed,.01,200,.25);
    auto* run=number(physics,"playerRunSpeed",tune.run_speed,.01,200,.25);
    auto* jump=number(physics,"playerJumpSpeed",tune.jump_speed,0,100,.25);
    auto* accel=number(physics,"playerAcceleration",tune.acceleration,0,10000,1);
    auto* air=number(physics,"playerAirControl",tune.air_control,0,1,.05);
    auto* gravity=number(physics,"playerGravityScale",tune.gravity_scale,0,20,.1);
    physicsForm->addRow("Walk Speed (m/s)",walk);
    physicsForm->addRow("Run Speed (m/s)",run);
    physicsForm->addRow("Jump Impulse",jump);
    physicsForm->addRow("Acceleration",accel);
    physicsForm->addRow("Air Control",air);
    physicsForm->addRow("Gravity Scale",gravity);
    auto pawnIndex=gameplay::entity_index(candidate,layout->pawn);
    auto capsule=collision_authoring::for_object(candidate,
        candidate.objects.at(*pawnIndex));
    auto* radius=number(physics,"playerCapsuleDiameter",
        capsule.dimensions.x,.05,10,.05);
    auto* height=number(physics,"playerCapsuleHeight",
        capsule.dimensions.y,.1,20,.1);
    physicsForm->addRow("Capsule Diameter",radius);
    physicsForm->addRow("Capsule Height",height);
    details->addTab(physics,"Motor / Pawn Collider");

    auto* cameraPanel=new QWidget(details);
    auto* cameraForm=new QFormLayout(cameraPanel);
    auto cam=player_blueprint::camera(candidate,layout->camera);
    auto rig=gameplay::camera_rig(candidate,layout->pawn);
    auto* view=new QComboBox(cameraPanel);
    view->setObjectName("playerCameraMode");
    view->addItems({"First Person","Third Person"});
    view->setCurrentIndex(int(cam.mode));
    auto* eye=number(cameraPanel,"playerCameraEyeHeight",rig.eye_height,0,5,.05);
    auto* fov=number(cameraPanel,"playerCameraFov",rig.fov_degrees,10,170,1);
    auto* minPitch=number(cameraPanel,"playerCameraMinPitch",rig.min_pitch,-89,0,1);
    auto* maxPitch=number(cameraPanel,"playerCameraMaxPitch",rig.max_pitch,0,89,1);
    auto* distance=number(cameraPanel,"playerCameraSpringArm",cam.spring_arm_length,0,20,.1);
    auto* shoulder=number(cameraPanel,"playerCameraShoulder",cam.shoulder,-5,5,.05);
    auto* lag=number(cameraPanel,"playerCameraLag",cam.lag,0,10,.05);
    cameraForm->addRow("View Mode",view);
    cameraForm->addRow("Eye Height",eye);
    cameraForm->addRow("Field of View",fov);
    cameraForm->addRow("Min Pitch",minPitch);
    cameraForm->addRow("Max Pitch",maxPitch);
    cameraForm->addRow("Spring Arm Distance",distance);
    cameraForm->addRow("Shoulder Offset",shoulder);
    cameraForm->addRow("Camera Lag",lag);
    details->addTab(cameraPanel,"Camera");

    auto* meshPanel=new QWidget(details);
    auto* meshForm=new QFormLayout(meshPanel);
    auto meshSettings=player_blueprint::mesh(candidate,layout->mesh);
    auto visualIndex=gameplay::entity_index(candidate,layout->mesh);
    auto* asset=new QLineEdit(QString::fromStdString(
        candidate.objects.at(*visualIndex).node.asset),meshPanel);
    asset->setObjectName("playerSkeletalMeshAsset");
    asset->setPlaceholderText("models/hero.ydd  (project VFS path)");
    auto* browse=new QPushButton("Browse Project Assets...",meshPanel);
    browse->setObjectName("playerBrowseSkeletalMesh");
    auto* assetRow=new QHBoxLayout;
    assetRow->addWidget(asset,1);
    assetRow->addWidget(browse);
    meshForm->addRow("Skeletal Mesh (YDD / YDR / OBJ)",assetRow);
    auto* prefab=new QLineEdit(QString::fromStdString(meshSettings.prefab),meshPanel);
    prefab->setObjectName("playerVisualPrefab");
    prefab->setPlaceholderText("prefabs/character.nprefab  (project or sharedAssets)");
    auto* browsePrefab=new QPushButton("Browse Prefabs...",meshPanel);
    browsePrefab->setObjectName("playerBrowseVisualPrefab");
    auto* prefabRow=new QHBoxLayout;
    prefabRow->addWidget(prefab,1);
    prefabRow->addWidget(browsePrefab);
    meshForm->addRow("Visual Prefab (.nprefab)",prefabRow);
    auto* skeleton=new QLineEdit(QString::fromStdString(meshSettings.skeleton),meshPanel);
    skeleton->setObjectName("playerSkeletonAsset");
    skeleton->setPlaceholderText("animations/hero.ymt or rigs/hero.skeleton.json");
    auto* material=new QLineEdit(QString::fromStdString(meshSettings.material),meshPanel);
    material->setObjectName("playerMeshMaterial");
    material->setPlaceholderText("materials/hero.ymat");
    auto* offsetX=number(meshPanel,"playerMeshOffsetX",meshSettings.offset.x,-100,100,.05);
    auto* offsetY=number(meshPanel,"playerMeshOffsetY",meshSettings.offset.y,-100,100,.05);
    auto* offsetZ=number(meshPanel,"playerMeshOffsetZ",meshSettings.offset.z,-100,100,.05);
    auto* hide= new QCheckBox("Hide full body in First Person",meshPanel);
    hide->setObjectName("playerHideMeshFirstPerson");
    hide->setChecked(meshSettings.hide_in_first_person);
    meshForm->addRow("Skeleton Resource",skeleton);
    meshForm->addRow("Material Resource",material);
    meshForm->addRow("Local Offset X",offsetX);
    meshForm->addRow("Local Offset Y",offsetY);
    meshForm->addRow("Local Offset Z",offsetZ);
    meshForm->addRow(hide);
    details->addTab(meshPanel,"SkeletalMesh");
    QObject::connect(browse,&QPushButton::clicked,&dialog,
        [&dialog,asset,project_root] {
            auto const file=QFileDialog::getOpenFileName(&dialog,
                "Choose SkeletalMesh (Project Assets)",
                QString::fromStdWString(project_root.wstring()),
                "Models (*.ydd *.ydr *.obj *.gltf *.glb);;All Files (*)");
            if(file.isEmpty())return;
            if(project_root.empty()) {
                QMessageBox::warning(&dialog,"Player Model",
                    "Save a scene in the project before browsing resources.");
                return;
            }
            auto const candidate=std::filesystem::path(file.toStdWString());
            std::error_code error;
            auto relative=std::filesystem::relative(candidate,project_root,error);
            if(error||relative.empty()||relative.wstring().rfind(L"..",0)==0) {
                QMessageBox::warning(&dialog,"Player Model",
                    "The model must be inside the project. Move it to project assets first.");
                return;
            }
            asset->setText(QString::fromStdString(relative.generic_string()));
        });

    QObject::connect(browsePrefab,&QPushButton::clicked,&dialog,
        [&dialog,prefab,project_root] {
            if(project_root.empty()) {
                QMessageBox::warning(&dialog,"Visual Prefab",
                    "Save the project before browsing prefab resources.");
                return;
            }
            auto const file=QFileDialog::getOpenFileName(&dialog,
                "Choose Visual Prefab (.nprefab)",
                QString::fromStdWString(project_root.wstring()),
                "NorthStar Prefabs (*.nprefab)");
            if(file.isEmpty())return;
            auto const absolute=std::filesystem::path(file.toStdWString());
            std::vector<std::filesystem::path> bases{project_root,project_root/"assets"};
            for(auto dir=project_root;!dir.empty();) {
                if(std::filesystem::is_directory(dir/"sharedAssets")) {
                    bases.push_back(dir/"sharedAssets");break;
                }
                auto parent=dir.parent_path();
                if(parent==dir)break;
                dir=parent;
            }
            for(auto const& base:bases) {
                std::error_code ec;
                auto const relative=std::filesystem::relative(absolute,base,ec);
                if(ec || relative.empty() || relative.is_absolute())continue;
                auto const ref=relative.generic_string();
                if(player_blueprint::safe_asset(ref) &&
                   ref.size()>8 && ref.substr(ref.size()-8)==".nprefab") {
                    try { (void)visual_prefab::locate(project_root,ref); }
                    catch(std::exception const&){ continue; }
                    prefab->setText(QString::fromStdString(ref));
                    return;
                }
            }
            QMessageBox::warning(&dialog,"Visual Prefab",
                "Choose a prefab located in project, assets or sharedAssets.");
        });
    auto* animationPanel=new QWidget(details);
    auto* animForm=new QFormLayout(animationPanel);
    auto graph=player_blueprint::animation(candidate,layout->mesh);
    std::array<QLineEdit*,5> clips{};
    std::array<char const*,5> labels{"Idle","Walk","Run","Jump","Fall"};
    std::array<std::string,5> values{graph.idle,graph.walk,graph.run,graph.jump,graph.fall};
    for(std::size_t i=0;i<5;++i) {
        auto* clip=new QLineEdit(QString::fromStdString(values[i]),animationPanel);
        clip->setObjectName(QString("playerAnim%1").arg(labels[i]));
        clip->setPlaceholderText("animations/hero.ycd or animations/clip.json");
        clips[i]=clip;
        animForm->addRow(labels[i],clip);
    }
    auto* walkThreshold=number(animationPanel,"playerAnimWalkThreshold",
        graph.walk_threshold,0,30,.05);
    auto* runThreshold=number(animationPanel,"playerAnimRunThreshold",
        graph.run_threshold,0,100,.1);
    auto* blend=number(animationPanel,"playerAnimBlendSeconds",
        graph.blend_seconds,0,5,.05);
    auto* rootMotion=new QCheckBox("Root Motion (experimental)",animationPanel);
    rootMotion->setObjectName("playerAnimRootMotion");
    rootMotion->setChecked(graph.root_motion);
    rootMotion->setToolTip("Metadata for a future animator provider; currently disabled.");
    rootMotion->setEnabled(false); // do not advertise unsupported runtime animation
    animForm->addRow("Walk Threshold",walkThreshold);
    animForm->addRow("Run Threshold",runThreshold);
    animForm->addRow("Blend Duration (s)",blend);
    animForm->addRow(rootMotion);
    animForm->addRow(new QLabel(
        "The Animation Controller selects Idle / Walk / Run / Jump / Fall. "
        "Clip references are retained for the engine animation provider.",animationPanel));
    details->addTab(animationPanel,"Animation");
    hierarchy->setCurrentItem(pawn);
    details->setCurrentIndex(1);

    auto* buttons=new QDialogButtonBox(
        QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
    buttons->setObjectName("playerBlueprintButtons");
    root->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        try {
            if(run->value()<walk->value())
                throw std::invalid_argument("Run Speed must exceed Walk Speed");
            if(height->value()<radius->value())
                throw std::invalid_argument("Capsule Height must be at least its diameter");
            if(minPitch->value()>maxPitch->value())
                throw std::invalid_argument("Camera pitch limits inverted");
            auto const meshRef=asset->text().trimmed().toStdString();
            if(!player_blueprint::safe_asset(meshRef))
                throw std::invalid_argument("Use a valid relative VFS model asset path");
            auto const prefabRef=prefab->text().trimmed().toStdString();
            if(!prefabRef.empty() && !project_root.empty()) {
                // Validate before mutating a saved Scene. The prefab is
                // authoritative for its native model while Pawn remains owner.
                (void)visual_prefab::load(project_root,prefabRef);
            }
            auto next=candidate;
            // Pawn and Controller are different entities with independent lifetimes.
            player_blueprint::assign(next,layout->controller,
                player_blueprint::controller_type,
                player_blueprint::serialize_controller(layout->pawn,local->isChecked()));
            std::string pawnPayload;
            player_character::detail::push32(pawnPayload,1u);
            player_character::detail::push32(pawnPayload,local->isChecked()?1u:0u);
            player_character::detail::push32(pawnPayload,1u);
            player_blueprint::assign(next,layout->pawn,
                player_character::pawn_type,std::move(pawnPayload));
            std::string motor;
            for(float v:{float(walk->value()),float(run->value()),
                         float(jump->value()),float(accel->value()),
                         float(air->value()),float(gravity->value())})
                player_character::detail::pushfloat(motor,v);
            player_blueprint::assign(next,
                layout->motor?layout->motor:layout->pawn,
                player_character::motor_type,std::move(motor));
            auto idx=gameplay::entity_index(next,layout->pawn).value();
            auto collider=collision_authoring::for_object(next,next.objects[idx]);
            collider.shape=collision_authoring::Shape::Capsule;
            collider.dimensions={float(radius->value()),float(height->value()),
                                 float(radius->value())};
            collider.object_type=collision_authoring::ObjectType::Pawn;
            collision_authoring::assign(next,layout->pawn,collider);
            auto newCamera=player_blueprint::Camera{
                static_cast<player_blueprint::ViewMode>(view->currentIndex()),
                float(distance->value()),float(shoulder->value()),float(lag->value())};
            player_blueprint::set_camera(next,layout->camera,newCamera);
            std::string rigPayload;
            for(float v:{float(eye->value()),float(fov->value()),
                         float(minPitch->value()),float(maxPitch->value())})
                player_character::detail::pushfloat(rigPayload,v);
            player_blueprint::assign(next,layout->camera,
                player_character::camera_type,std::move(rigPayload));
            auto model=player_blueprint::Mesh{};
            model.offset={float(offsetX->value()),float(offsetY->value()),
                          float(offsetZ->value())};
            model.hide_in_first_person=hide->isChecked();
            model.skeleton=skeleton->text().trimmed().toStdString();
            model.material=material->text().trimmed().toStdString();
            model.prefab=prefabRef;
            player_blueprint::set_mesh(next,layout->mesh,model);
            auto meshIdx=gameplay::entity_index(next,layout->mesh).value();
            auto resolvedModel=meshRef;
            if(!prefabRef.empty() && !project_root.empty()) {
                // Store the root drawable for native runtime rendering,
                // in addition to the live prefab link used by editor preview.
                auto templateScene=visual_prefab::load(project_root,prefabRef);
                auto root=std::find_if(templateScene.objects.begin(),
                    templateScene.objects.end(),
                    [](SceneObject const& o){return o.node.parent==0;});
                if(root==templateScene.objects.end() || root->node.asset.empty())
                    throw std::invalid_argument("Visual prefab root has no model");
                resolvedModel=root->node.asset;
            }
            next.objects[meshIdx].node.asset=resolvedModel;
            next.objects[meshIdx].node.visible=!resolvedModel.empty();
            next.objects[meshIdx].position={
                next.objects[idx].position.x+model.offset.x,
                next.objects[idx].position.y+model.offset.y,
                next.objects[idx].position.z+model.offset.z};
            auto anim=player_blueprint::Animation{};
            anim.idle=clips[0]->text().trimmed().toStdString();
            anim.walk=clips[1]->text().trimmed().toStdString();
            anim.run=clips[2]->text().trimmed().toStdString();
            anim.jump=clips[3]->text().trimmed().toStdString();
            anim.fall=clips[4]->text().trimmed().toStdString();
            anim.walk_threshold=float(walkThreshold->value());
            anim.run_threshold=float(runThreshold->value());
            anim.blend_seconds=float(blend->value());
            anim.root_motion=false;
            player_blueprint::set_animation(next,layout->mesh,anim);
            auto cameraIdx=gameplay::entity_index(next,layout->camera).value();
            next.objects[cameraIdx].position={
                next.objects[idx].position.x,
                next.objects[idx].position.y+float(eye->value()),
                next.objects[idx].position.z};
            validate_scene(next);
            candidate=std::move(next);
            dialog.accept();
        }catch(std::exception const& e) {
            QMessageBox::warning(&dialog,"Player Blueprint",e.what());
        }
    });
    if(dialog.exec()!=QDialog::Accepted)return std::nullopt;
    return candidate;
}
} // namespace northstarcpp::editor::player_editor
