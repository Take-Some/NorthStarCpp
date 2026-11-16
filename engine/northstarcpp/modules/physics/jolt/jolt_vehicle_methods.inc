// Inside World. Native Jolt 6-DOF rigid chassis + per-wheel raycast constraint.
struct VehicleRecord {
    j::Ref<j::VehicleConstraint> constraint;
    std::uint32_t wheel_count=0;
};
std::map<std::uint32_t,VehicleRecord> vehicles;

void create_vehicle(std::uint32_t index,NorthStarVehicleRig const& rig,float yaw) {
    if(index>=bodies.size() || !bodies[index].alive ||
       !bodies[index].resident || !bodies[index].is_static ||
       bodies[index].id==player || vehicles.count(index))
        throw std::invalid_argument("Vehicle requires a free, resident static scene entity");
    if(rig.size<sizeof(rig)||rig.wheel_count<2||
       rig.wheel_count>NORTHSTARCPP_VEHICLE_MAX_WHEELS||!rig.wheels ||
       !std::isfinite(yaw)||std::abs(yaw)>1000.f||
       !std::isfinite(rig.mass)||rig.mass<100.f||rig.mass>100000.f ||
       !std::isfinite(rig.engine_force)||rig.engine_force<=0.f ||
       !std::isfinite(rig.brake_force)||rig.brake_force<=0.f)
        throw std::invalid_argument("Invalid Jolt vehicle mass or rig");
    auto& iface=physics.GetBodyInterface();
    auto& record=bodies[index];
    j::RefConst<j::Shape> original_shape=iface.GetShape(record.id);
    auto const position=iface.GetPosition(record.id);
    // Explicit lower centre of mass. Authoring specifies COM height above
    // the wheel axle plane, wheel mounts are relative to the body centre.
    float axle_y=0.f;
    for(std::uint32_t n=0;n<rig.wheel_count;++n)
        axle_y+=rig.wheels[n].mount[1];
    axle_y/=static_cast<float>(rig.wheel_count);
    float const com_y=std::clamp(
        axle_y+rig.center_of_mass_y,-2.0f,2.0f);
    j::RefConst<j::Shape> shifted_shape =
        j::OffsetCenterOfMassShapeSettings(
            j::Vec3(0.f,com_y,0.f),original_shape.GetPtr()).Create().Get();
    j::BodyCreationSettings chassis(shifted_shape,position,
        j::Quat::sRotation(j::Vec3::sAxisY(),yaw),
        j::EMotionType::Dynamic,layers::dynamic_body);
    chassis.mFriction=.75f;
    chassis.mMotionQuality=j::EMotionQuality::LinearCast;
    chassis.mOverrideMassProperties=j::EOverrideMassProperties::CalculateInertia;
    chassis.mMassPropertiesOverride.mMass=rig.mass;
    // Retain the authored collider shape, but change the body to dynamic
    // instead of teleporting an immutable static collision actor.
    j::Body* dynamic=iface.CreateBody(chassis);
    if(!dynamic)throw std::runtime_error("Jolt dynamic vehicle body creation failed");
    j::Ref<j::VehicleConstraint> constraint;
    try {
        j::VehicleConstraintSettings settings;
        settings.mMaxPitchRollAngle=j::DegreesToRadians(75.f);
        settings.mWheels.resize(rig.wheel_count);
        std::vector<std::pair<std::uint32_t,std::uint32_t>> drive_pairs;
        float total_drive=0.f;
        float driven_count=0.f;
        for(std::uint32_t n=0;n<rig.wheel_count;++n) {
            auto const& wheel=rig.wheels[n];
            if(wheel.size<sizeof(wheel) || wheel.radius<.12f ||
               wheel.radius>2.f || wheel.travel<=0.f ||
               wheel.spring_rate<=0.f || wheel.rotational_inertia<=0.f ||
               !std::isfinite(wheel.mount[0])||
               !std::isfinite(wheel.mount[1])||
               !std::isfinite(wheel.mount[2]) ||
               wheel.grip_lat<=0.f || wheel.grip_long<=0.f)
                throw std::invalid_argument("Invalid Jolt wheel configuration");
            auto* w=new j::WheelSettingsWV;
            w->mPosition=j::Vec3(wheel.mount[0],wheel.mount[1],wheel.mount[2]);
            w->mRadius=wheel.radius;
            w->mWidth=.25f;
            w->mSuspensionMinLength=std::max(.02f,wheel.rest_length-wheel.travel*.5f);
            w->mSuspensionMaxLength=w->mSuspensionMinLength+wheel.travel;
            auto const supported_mass=rig.mass/rig.wheel_count;
            float const frequency=std::sqrt(wheel.spring_rate/supported_mass)/(2.f*j::JPH_PI);
            w->mSuspensionSpring.mFrequency=std::clamp(frequency,.5f,8.f);
            w->mSuspensionSpring.mDamping=std::clamp(
                wheel.damper_rate/(2.f*std::sqrt(wheel.spring_rate*supported_mass)),.1f,2.f);
            w->mMaxSteerAngle=std::clamp(wheel.steer_factor,0.f,1.f)*.53f;
            w->mMaxBrakeTorque=rig.brake_force*wheel.radius*
                wheel.brake_factor/std::max(1u,rig.wheel_count);
            w->mMaxHandBrakeTorque=0.f;
            w->mInertia=wheel.rotational_inertia;
            // Preserve Jolt default nonlinear tire friction curves.
            // Jolt's wheel friction defaults are non-linear curves. Preserve
            // their built-in material response; the road body supplies friction.
            settings.mWheels[n]=w;
            total_drive+=wheel.drive_factor;
            driven_count+=wheel.drive_factor>0.f?1.f:0.f;
        }
        // One Jolt anti-roll bar per opposed wheel pair. This transfers
        // load across each axle during cornering without suppressing roll DOF.
        for(std::uint32_t n=0;n+1<rig.wheel_count;n+=2) {
            auto const& left=rig.wheels[n];
            auto const& right=rig.wheels[n+1];
            if(left.mount[0]*right.mount[0]>=0.f)continue;
            j::VehicleAntiRollBar bar;
            bar.mLeftWheel=static_cast<int>(n);
            bar.mRightWheel=static_cast<int>(n+1);
            bar.mStiffness=.16f*std::min(left.spring_rate,right.spring_rate);
            settings.mAntiRollBars.push_back(bar);
        }
        auto* controller=new j::WheeledVehicleControllerSettings;
        settings.mController=controller;
        controller->mEngine.mMaxTorque=rig.engine_force*.5f;
        controller->mEngine.mMaxRPM=4200.f;
        controller->mTransmission.mGearRatios={3.6f,2.4f,1.7f,1.25f,1.f,.78f};
        controller->mTransmission.mReverseGearRatios={-3.2f};
        if(total_drive<=0.f)throw std::invalid_argument("Jolt rig has no driven wheels");
        // Split powered axle pairs across differentials; supports 4/6/8+ wheels.
        for(std::uint32_t n=0;n+1<rig.wheel_count;n+=2) {
            if(rig.wheels[n].drive_factor<=0.f &&
               rig.wheels[n+1].drive_factor<=0.f)continue;
            auto diff=j::VehicleDifferentialSettings{};
            diff.mLeftWheel=static_cast<int>(n);
            diff.mRightWheel=static_cast<int>(n+1);
            diff.mEngineTorqueRatio=(rig.wheels[n].drive_factor+
                rig.wheels[n+1].drive_factor)/total_drive;
            controller->mDifferentials.push_back(diff);
        }
        if(controller->mDifferentials.empty())
            throw std::invalid_argument("Driven wheels require at least one axle pair");
        constraint=new j::VehicleConstraint(*dynamic,settings);
        constraint->SetVehicleCollisionTester(
            new j::VehicleCollisionTesterRay(layers::dynamic_body));
        // Transaction boundary. Create the new chassis first and replace the
        // original registered static body only after rig validation succeeds.
        iface.RemoveBody(record.id);
        try {
            iface.AddBody(dynamic->GetID(),j::EActivation::Activate);
            physics.AddConstraint(constraint);
            physics.AddStepListener(constraint);
        }catch(...) {
            if(dynamic->IsInBroadPhase())iface.RemoveBody(dynamic->GetID());
            iface.AddBody(record.id,j::EActivation::DontActivate);
            throw;
        }
        iface.DestroyBody(record.id);
        record.id=dynamic->GetID();
        record.is_static=false;
        vehicles.emplace(index,VehicleRecord{constraint,rig.wheel_count});
        physics.OptimizeBroadPhase();
    }catch(...) {
        if(dynamic->IsInBroadPhase())iface.RemoveBody(dynamic->GetID());
        iface.DestroyBody(dynamic->GetID());
        throw;
    }
}
void destroy_vehicle(std::uint32_t index) {
    auto it=vehicles.find(index);
    if(it==vehicles.end())throw std::invalid_argument("Unknown Jolt vehicle");
    auto& iface=physics.GetBodyInterface();
    auto& entry=bodies[index];
    auto const old=entry.id;
    auto const shape=iface.GetShape(old);
    auto const position=iface.GetPosition(old);
    auto const rotation=iface.GetRotation(old);
    j::BodyCreationSettings setup(shape,position,rotation,
        j::EMotionType::Static,layers::static_body);
    auto* restored=iface.CreateBody(setup);
    if(!restored)throw std::runtime_error("Cannot restore static vehicle body");
    physics.RemoveStepListener(it->second.constraint);
    physics.RemoveConstraint(it->second.constraint);
    vehicles.erase(it);
    iface.RemoveBody(old);
    iface.AddBody(restored->GetID(),j::EActivation::DontActivate);
    iface.DestroyBody(old);
    entry.id=restored->GetID();
    entry.is_static=true;
    entry.resident=true;
}
void vehicle_controls(std::uint32_t index,float gas,float steer,float brake,float hand) {
    auto it=vehicles.find(index);
    if(it==vehicles.end() ||
       !std::isfinite(gas)||!std::isfinite(steer)||!std::isfinite(brake)||
       !std::isfinite(hand) || std::abs(gas)>1.f||std::abs(steer)>1.f||
       brake<0.f||brake>1.f||hand<0.f||hand>1.f)
        throw std::invalid_argument("Invalid vehicle control command");
    auto* controller=static_cast<j::WheeledVehicleController*>(
        it->second.constraint->GetController());
    controller->SetDriverInput(gas,steer,brake,hand);
    physics.GetBodyInterface().ActivateBody(bodies[index].id);
}
void vehicle_pose(std::uint32_t index,NorthStarPhysicsVehiclePose& out) const {
    if(!vehicles.count(index))throw std::invalid_argument("Missing Jolt vehicle pose");
    auto const& iface=physics.GetBodyInterface();
    auto const& body=bodies[index];
    auto const position=iface.GetPosition(body.id);
    auto const rotation=iface.GetRotation(body.id);
    auto const vx=rotation.RotateAxisX(),vy=rotation.RotateAxisY(),vz=rotation.RotateAxisZ();
    auto const velocity=iface.GetLinearVelocity(body.id);
    auto const angular=iface.GetAngularVelocity(body.id);
    out.size=sizeof(out);
    out.x=static_cast<float>(position.GetX());
    out.y=static_cast<float>(position.GetY())-body.half;
    out.z=static_cast<float>(position.GetZ());
    out.rotation[0]=vx.GetX();out.rotation[1]=vx.GetY();out.rotation[2]=vx.GetZ();
    out.rotation[3]=vy.GetX();out.rotation[4]=vy.GetY();out.rotation[5]=vy.GetZ();
    out.rotation[6]=vz.GetX();out.rotation[7]=vz.GetY();out.rotation[8]=vz.GetZ();
    out.linear_velocity[0]=velocity.GetX();
    out.linear_velocity[1]=velocity.GetY();
    out.linear_velocity[2]=velocity.GetZ();
    out.angular_velocity[0]=angular.GetX();
    out.angular_velocity[1]=angular.GetY();
    out.angular_velocity[2]=angular.GetZ();
}
void vehicle_wheel(std::uint32_t index,std::uint32_t ordinal,
                   NorthStarVehicleWheelState& out) const {
    auto it=vehicles.find(index);
    if(it==vehicles.end()||ordinal>=it->second.wheel_count)
        throw std::invalid_argument("Unknown Jolt wheel ordinal");
    auto* wheel=static_cast<j::WheelWV const*>(it->second.constraint->GetWheel(ordinal));
    auto const* setup=wheel->GetSettings();
    out.size=sizeof(out);
    out.steer_angle=wheel->GetSteerAngle();
    out.spin_angle=wheel->GetRotationAngle();
    out.angular_speed=wheel->GetAngularVelocity();
    out.compression=std::clamp(
        setup->mSuspensionMaxLength-wheel->GetSuspensionLength(),0.f,
        setup->mSuspensionMaxLength-setup->mSuspensionMinLength);
    out.normal_force=wheel->GetSuspensionLambda()*120.f;
    out.slip_ratio=wheel->mLongitudinalSlip;
    out.slip_angle=wheel->mLateralSlip;
    out.in_contact=wheel->HasContact()?1u:0u;
}
