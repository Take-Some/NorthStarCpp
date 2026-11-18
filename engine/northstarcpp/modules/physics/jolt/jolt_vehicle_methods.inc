// Inside World. Native Jolt 6-DOF rigid chassis + per-wheel raycast constraint.
struct VehicleRecord {
    j::Ref<j::VehicleConstraint> constraint;
    std::uint32_t wheel_count=0;
    float deck_half_width=0.f,deck_half_length=0.f,deck_top=0.f;
    float city_mass=0.f,drive_force=0.f,brake_force=0.f;
    float throttle=0.f,steer=0.f,brake=0.f,handbrake=0.f;
    std::vector<NorthStarVehicleWheelConfig> city_wheels;
    std::vector<NorthStarVehicleWheelState> city_wheel_states;
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
       !std::isfinite(rig.mass)||rig.mass<100.f||rig.mass>1000000000.f ||
       !std::isfinite(rig.engine_force)||rig.engine_force<=0.f ||
       !std::isfinite(rig.brake_force)||rig.brake_force<=0.f)
        throw std::invalid_argument("Invalid Jolt vehicle mass or rig");
    auto& iface=physics.GetBodyInterface();
    auto& record=bodies[index];
    j::RefConst<j::Shape> original_shape=iface.GetShape(record.id);
    auto const position=iface.GetPosition(record.id);
    // Large vehicles are *not* a 25cm marker. Create a single mass-bearing
    // platform collider owned by this Jolt dynamic body; the authored
    // deck meshes are purely visual. Players and cargo contact this body.
    // Wheel mounts determine footprint, including future project-defined
    // CityRoot vehicles, without embedding Sea of Rust asset names.
    const bool city=rig.mass>=1000000.f && rig.wheel_count>=8;
    float max_x=0.f,max_z=0.f,mean_y=0.f;
    for(std::uint32_t n=0;n<rig.wheel_count;++n) {
        const auto& wheel=rig.wheels[n];
        max_x=std::max(max_x,std::abs(wheel.mount[0]));
        max_z=std::max(max_z,std::abs(wheel.mount[2]));
        mean_y+=wheel.mount[1];
    }
    mean_y/=static_cast<float>(rig.wheel_count);
    const float deck_top=mean_y+(max_x>15.f?4.f:3.2f);
    j::RefConst<j::Shape> shifted_shape;
    if(city) {
        const float half_width=max_x>15.f?max_x-5.f:max_x+1.5f;
        const float half_length=max_z+6.f;
        // Keep 70 cm of plate thickness for stable continuous Jolt contact.
        const float half_plate=.35f;
        j::StaticCompoundShapeSettings compound;
        j::RefConst<j::Shape> platform=new j::BoxShape(
            j::Vec3(half_width,half_plate,half_length));
        // The authored root is at its bottom position; the original .25m
        // marker shape center is only .125m above that authored position.
        compound.AddShape(j::Vec3(0.f,deck_top-half_plate-record.half,0.f),
                          j::Quat::sIdentity(),platform.GetPtr());
        // Broad hull in the same rigid body, well above the terrain contacts.
        j::RefConst<j::Shape> hull=new j::BoxShape(
            j::Vec3(half_width*.85f,.9f,half_length*.85f));
        compound.AddShape(j::Vec3(0.f,deck_top-1.6f-record.half,0.f),
                          j::Quat::sIdentity(),hull.GetPtr());
        auto built=compound.Create();
        if(built.HasError())throw std::runtime_error(
            "Cannot create city dynamic compound collider");
        // StaticCompoundShape recenters children around its COM. Move
        // geometry back up to the authored deck height (6.85m for Bastion).
        // Otherwise its hull intersects the streamed heightfield and
        // contact correction shoots the entire 25,000 tonne city skyward.
        j::RefConst<j::Shape> centered=built.Get();
        float const top=centered->GetLocalBounds().mMax.GetY();
        float const lift=deck_top-record.half-top;
        // Moving the collision geometry requires changing the relative COM,
        // not wrapping in RotatedTranslatedShape (that decorator recenters
        // its local bounds again). Keep the mass COM low below the deck.
        shifted_shape=new j::OffsetCenterOfMassShape(
            centered.GetPtr(),j::Vec3(0.f,-lift,0.f));
    } else {
        float const axle_y=mean_y;
        float const com_y=std::clamp(axle_y+rig.center_of_mass_y,-2.f,2.f);
        shifted_shape=j::OffsetCenterOfMassShapeSettings(
            j::Vec3(0.f,com_y,0.f),original_shape.GetPtr()).Create().Get();
    }
    if(city) {
        auto const bounds=shifted_shape->GetLocalBounds();
        if(std::abs(bounds.mMax.GetY()-(deck_top-record.half))>.06f ||
           bounds.mMin.GetY()<.2f)
            throw std::runtime_error(
                "City collider COM placement invalid: terrain intersection");
    }
    j::BodyCreationSettings chassis(shifted_shape,position,
        j::Quat::sRotation(j::Vec3::sAxisY(),yaw),
        j::EMotionType::Dynamic,layers::dynamic_body);
    // A city built from independent kinematic deck colliders must not yaw
    // independently until the compound-body rotation ABI is available.
    // Keep mass-bearing translational Jolt physics (X/Y/Z), but defer
    // compound-body angular DOFs until the deck-collider rotation ABI exists.
    // Ordinary vehicles retain full 6DOF rigid-body simulation.
    if(city)
        chassis.mAllowedDOFs=
            j::EAllowedDOFs::TranslationX|j::EAllowedDOFs::TranslationY|
            j::EAllowedDOFs::TranslationZ;
    chassis.mFriction=.75f;
    chassis.mMotionQuality=j::EMotionQuality::LinearCast;
    // A city-sized chassis needs city-sized inertia; the root marker's
    // placeholder collider is too small to derive inertia from its shape.
    float half_x=0.f,half_z=0.f;
    for(std::uint32_t i=0;i<rig.wheel_count;++i) {
        auto const& wheel=rig.wheels[i];
        half_x=std::max(half_x,std::abs(wheel.mount[0])+wheel.radius);
        half_z=std::max(half_z,std::abs(wheel.mount[2])+wheel.radius);
    }
    const float width=2.f*std::max(1.f,half_x);
    const float length=2.f*std::max(1.f,half_z);
    const float height=std::max(2.f,rig.center_of_mass_y*2.f+4.f);
    chassis.mOverrideMassProperties=
        j::EOverrideMassProperties::MassAndInertiaProvided;
    chassis.mMassPropertiesOverride.mInertia=j::Mat44::sScale(j::Vec3(
        rig.mass*(height*height+length*length)/12.f,
        rig.yaw_inertia>0.f?rig.yaw_inertia:
            rig.mass*(width*width+length*length)/12.f,
        rig.mass*(height*height+width*width)/12.f));
    chassis.mMassPropertiesOverride.mMass=rig.mass;
    // Retain the authored collider shape, but change the body to dynamic
    // instead of teleporting an immutable static collision actor.
    j::Body* dynamic=iface.CreateBody(chassis);
    if(!dynamic)throw std::runtime_error("Jolt dynamic vehicle body creation failed");
    j::Ref<j::VehicleConstraint> constraint;
    try {
        if(city) {
            // Tracked cities use a mass-bearing dynamic Jolt chassis and
            // distributed terrain-contact forces instead of Jolt's
            // lightweight-car VehicleConstraint, which can become unstable
            // when 25,000-tonne wheel springs contact triangle heightfields.
            iface.RemoveBody(record.id);
            try {
                iface.AddBody(dynamic->GetID(),j::EActivation::Activate);
            }catch(...) {
                iface.AddBody(record.id,j::EActivation::DontActivate);
                throw;
            }
            iface.DestroyBody(record.id);
            record.id=dynamic->GetID();
            record.is_static=false;
            VehicleRecord state{};
            state.wheel_count=rig.wheel_count;
            state.deck_half_width=max_x>15.f?max_x-5.f:max_x+1.5f;
            state.deck_half_length=max_z+6.f;
            state.deck_top=deck_top;
            state.city_mass=rig.mass;
            state.drive_force=rig.engine_force;
            state.brake_force=rig.brake_force;
            state.city_wheels.assign(rig.wheels,rig.wheels+rig.wheel_count);
            state.city_wheel_states.resize(rig.wheel_count);
            vehicles.emplace(index,std::move(state));
            physics.OptimizeBroadPhase();
            return;
        }
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
            // City suspension must be mass-scaled: treating 50 MN/m as
            // wheel-level stiffness in Jolt yields an ~0.84 Hz giant-body
            // oscillator that can eject a 25,000 tonne platform at 500 m/s
            // on even gentle heightfield triangle contacts. Use a 0.5 Hz
            // heavily damped suspension whose force scales with real mass.
            // Ordinary vehicle configs continue to use their authored rate.
            w->mSuspensionSpring.mFrequency=city?
                std::clamp(frequency,.35f,.5f):
                std::clamp(frequency,.5f,8.f);
            w->mSuspensionSpring.mDamping=city?.85f:std::clamp(
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
        // Opposed track axles: all left wheels precede the right wheels.
        bool const grouped=rig.wheel_count%2==0 && [&] {
            unsigned half=rig.wheel_count/2;
            for(unsigned i=0;i<half;++i)
                if(rig.wheels[i].mount[0]>=0.f ||
                   rig.wheels[i+half].mount[0]<=0.f ||
                   std::abs(rig.wheels[i].mount[2]-rig.wheels[i+half].mount[2])>2.f)
                    return false;
            return true;
        }();
        auto axle=[&](unsigned n){
            unsigned half=rig.wheel_count/2;
            return std::pair<unsigned,unsigned>{grouped?n/2:n,
                grouped?n/2+half:n+1};
        };
        // One Jolt anti-roll bar per opposed wheel pair. This transfers
        // load across each axle during cornering without suppressing roll DOF.
        for(std::uint32_t n=0;n+1<rig.wheel_count;n+=2) {
            auto const [li,ri]=axle(n);
            auto const& left=rig.wheels[li];
            auto const& right=rig.wheels[ri];
            if(left.mount[0]*right.mount[0]>=0.f)continue;
            j::VehicleAntiRollBar bar;
            bar.mLeftWheel=static_cast<int>(li);
            bar.mRightWheel=static_cast<int>(ri);
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
            auto const [li,ri]=axle(n);
            if(rig.wheels[li].drive_factor<=0.f &&
               rig.wheels[ri].drive_factor<=0.f)continue;
            auto diff=j::VehicleDifferentialSettings{};
            diff.mLeftWheel=static_cast<int>(li);
            diff.mRightWheel=static_cast<int>(ri);
            diff.mEngineTorqueRatio=(rig.wheels[li].drive_factor+
                rig.wheels[ri].drive_factor)/total_drive;
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
        vehicles.emplace(index,VehicleRecord{
            constraint,rig.wheel_count,
            city?(max_x>15.f?max_x-5.f:max_x+1.5f):0.f,
            city?max_z+6.f:0.f,city?deck_top:0.f});
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
    if(it->second.constraint) {
        physics.RemoveStepListener(it->second.constraint);
        physics.RemoveConstraint(it->second.constraint);
    }
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
    if(!it->second.constraint) {
        auto& v=it->second;
        v.throttle=gas;v.steer=steer;v.brake=brake;v.handbrake=hand;
        physics.GetBodyInterface().ActivateBody(bodies[index].id);
        return;
    }
    auto* controller=static_cast<j::WheeledVehicleController*>(
        it->second.constraint->GetController());
    controller->SetDriverInput(gas,steer,brake,hand);
    physics.GetBodyInterface().ActivateBody(bodies[index].id);
}
// True dynamic tracked-city physics: 8/14 independent ground probes, with
// mass-proportional spring/damper forces and bounded traction each Jolt step.
// Regular-sized cars continue to use Jolt VehicleConstraint unchanged.
void advance_city_vehicles(float dt) {
    if(dt<=0.f)return;
    auto& iface=physics.GetBodyInterface();
    constexpr float gravity_mps2=9.81f;
    for(auto& [index,vehicle]:vehicles) {
        if(vehicle.city_mass<=0.f || index>=bodies.size())continue;
        auto const& city_body=bodies[index];
        if(!city_body.alive||!city_body.resident)continue;
        auto const pos=iface.GetPosition(city_body.id);
        auto const velocity=iface.GetLinearVelocity(city_body.id);
        float total_support=0.f;
        std::uint32_t grounded=0;
        const float wheel_mass=vehicle.city_mass/
            std::max(1u,vehicle.wheel_count);
        const float target_compression=.65f;
        const float k=wheel_mass*gravity_mps2/target_compression;
        const float c=2.f*std::sqrt(k*wheel_mass)*.85f;
        for(std::size_t n=0;n<vehicle.city_wheels.size();++n) {
            auto const& wheel=vehicle.city_wheels[n];
            auto& telemetry=vehicle.city_wheel_states[n];
            telemetry.size=sizeof(telemetry);
            telemetry.in_contact=0u;
            telemetry.normal_force=0.f;
            telemetry.compression=0.f;
            telemetry.steer_angle=0.f;
            const float wx=float(pos.GetX())+wheel.mount[0];
            const float wz=float(pos.GetZ())+wheel.mount[2];
            const float start_y=float(pos.GetY())+wheel.mount[1]+.4f;
            const float max_length=wheel.rest_length+wheel.travel*.5f;
            const float cast_distance=wheel.radius+max_length+1.f;
            auto const hit=sweep(wx,start_y,wz,
                wx,start_y-cast_distance,wz,.08f,index);
            if(hit.hit && hit.normal_y>.4f &&
               hit.object_index==UINT32_MAX) {
                // The swept sphere centre stays .08m above the surface.
                const float ground_y=start_y-
                    cast_distance*hit.fraction-.08f;
                const float length=float(pos.GetY())+wheel.mount[1]-
                    wheel.radius-ground_y;
                if(length<=max_length+.08f && length>=-.6f) {
                    const float compression=std::clamp(
                        max_length-length,0.f,wheel.travel);
                    const float force=std::clamp(
                        k*compression-c*velocity.GetY(),0.f,
                        wheel_mass*gravity_mps2*2.25f);
                    telemetry.in_contact=1u;
                    telemetry.compression=compression;
                    telemetry.normal_force=force;
                    total_support+=force;
                    ++grounded;
                }
            }
            telemetry.angular_speed=velocity.GetZ()/
                std::max(.1f,wheel.radius);
            telemetry.spin_angle=std::remainder(
                telemetry.spin_angle+telemetry.angular_speed*dt,
                6.283185307179586f);
            telemetry.slip_ratio=0.f;
            telemetry.slip_angle=0.f;
        }
        const float mass=vehicle.city_mass;
        const float vz=velocity.GetZ();
        float drive=0.f,braking=0.f;
        if(grounded>=2) {
            const float traction=std::max(0.f,total_support*1.15f);
            drive=std::clamp(vehicle.throttle*vehicle.drive_force,
                -traction,traction);
            if(std::abs(vz)>.015f)
                braking=-std::copysign(
                    std::min((vehicle.brake+vehicle.handbrake)*
                             vehicle.brake_force,mass*std::abs(vz)/dt),
                    vz);
        }
        const float lateral=-mass*.4f*velocity.GetX();
        const float rolling=-mass*.08f*vz;
        iface.AddForce(city_body.id,j::Vec3(
            lateral,total_support,drive+braking+rolling));
    }
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
    if(!it->second.constraint) {
        out=it->second.city_wheel_states.at(ordinal);
        out.size=sizeof(out);
        return;
    }
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
