#!/usr/bin/env python3
"""Move static scene colliders with Jolt kinematic velocities, not teleports.

Project-owned scene roots move every deck collider via Scene.moveStatic.
The physics provider turns only continuously moving colliders into kinematic
bodies; inertial frictional motion for arbitrary dynamic objects is solved
by Jolt, and Character Motor receives support velocity once per fixed step.
"""
from pathlib import Path
import shutil
root=Path(__file__).resolve().parents[2]
p=root/"engine/northstarcpp/modules/physics/jolt/jolt.cpp"
s=p.read_text("utf8")
backup=p.with_name("jolt.cpp.bak-before-kinematic-city-20261117")
if not backup.exists():shutil.copy2(p,backup)
def patch(old,new):
 global s
 n=s.count(old)
 if n!=1:raise RuntimeError("Expected unique patch source: "+old[:115]+" count "+str(n))
 s=s.replace(old,new,1)
patch('#include <unordered_map>','#include <unordered_map>\n#include <unordered_set>')
patch('''            setup.mFriction = input.initially_controlled ? 0.f : 0.65f;
            setup.mUserData=std::uint64_t(n)+1;''',
'''            setup.mFriction = input.initially_controlled ? 0.f : 0.85f;
            // Static scenery can become a moving kinematic carrier without
            // destroying its broadphase identity or collision metadata.
            setup.mAllowDynamicOrKinematic = input.mass == 0.f;
            setup.mUserData=std::uint64_t(n)+1;''')
patch('''        setup.mFriction = 0.65f;
        setup.mUserData=std::uint64_t(bodies.size())+1;''',
'''        setup.mFriction = 0.85f;
        setup.mAllowDynamicOrKinematic = input.mass == 0.f;
        setup.mUserData=std::uint64_t(bodies.size())+1;''')
start=s.index('    void move_static(uint32_t index, float x, float y, float z)')
end=s.index('    uint32_t add_runtime_body(',start)
s=s[:start]+'''    void move_static(uint32_t index, float x, float y, float z)
    {
        if (index >= bodies.size() || !bodies[index].alive ||
            !bodies[index].is_static ||
            !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            throw std::runtime_error("Invalid Jolt static relocation");
        auto& iface = physics.GetBodyInterface();
        auto const& body = bodies[index];
        j::RVec3 const target(x, y+body.half, z);
        // The existing physics pose is authoritative until the next fixed step.
        auto const old=iface.GetPosition(body.id);
        float const dx=x-static_cast<float>(old.GetX());
        float const dy=y+body.half-static_cast<float>(old.GetY());
        float const dz=z-static_cast<float>(old.GetZ());
        float const horizontal=std::hypot(dx,dz);
        if(horizontal<1.e-6f && std::abs(dy)<1.e-6f) {
            pending_platform_moves.erase(index);
            moved_static_deltas.erase(index);
            return;
        }
        const bool driven=body.resident && body.collision_enabled &&
            horizontal<=2.5f && std::abs(dy)<.12f;
        if(driven) {
            if(!kinematic_platforms.count(index)) {
                // A true kinematic is simulated by Jolt with world velocity,
                // so *all* grounded dynamic items receive friction/impulses.
                iface.SetMotionType(body.id,j::EMotionType::Kinematic,
                    j::EActivation::Activate);
                iface.SetObjectLayer(body.id,layers::dynamic_body);
                iface.SetFriction(body.id,.95f);
                kinematic_platforms.insert(index);
            }
            pending_platform_moves.insert_or_assign(index,target);
            // Overwrite, don't accumulate: repeated authoring writes before
            // a physics step describe one final target, not stacked motion.
            moved_static_deltas[index]={dx,dz};
            return;
        }
        // True teleport (world streaming / editor reposition / noncolliding
        // visual) never transfers momentum to passengers.
        pending_platform_moves.erase(index);
        moved_static_deltas.erase(index);
        if(kinematic_platforms.count(index))
            iface.SetLinearAndAngularVelocity(body.id,
                j::Vec3::sZero(),j::Vec3::sZero());
        iface.SetPosition(body.id,target,j::EActivation::DontActivate);
    }

'''+s[end:]
patch('''    // Provider-owned support velocity: carried dynamic characters inherit
    // moving-platform world velocity but keep their own input-relative motor.
    std::unordered_map<std::uint32_t,std::array<float,2>> moved_static_deltas;''',
'''    // A true Kinematic platform drives all Jolt dynamic objects by contact.
    // Targets are applied once per real fixed physics step, never as teleports.
    std::unordered_map<std::uint32_t,j::RVec3> pending_platform_moves;
    std::unordered_set<std::uint32_t> kinematic_platforms;
    // Player controller independently accounts for support motion because its
    // friction is intentionally zero; it must retain inertia when airborne.
    std::unordered_map<std::uint32_t,std::array<float,2>> moved_static_deltas;''')
patch('''        moving_support_dt+=static_cast<float>(admitted);
        auto& iface = physics.GetBodyInterface();''',
'''        moving_support_dt+=static_cast<float>(admitted);
        auto& iface = physics.GetBodyInterface();
        auto const steps=static_cast<int>(std::floor((elapsed+1.e-9)/fixed_step));
        float const physics_dt=float(steps*fixed_step);
        // The body has not moved yet: this is precisely the timestep during
        // which Jolt will integrate its Kinematic linear velocity.
        if(steps>0) {
            for(auto const index:kinematic_platforms) {
                if(index>=bodies.size()||!bodies[index].alive||
                   !bodies[index].resident)continue;
                auto const& body=bodies[index];
                auto const target=pending_platform_moves.find(index);
                if(target!=pending_platform_moves.end())
                    iface.MoveKinematic(body.id,target->second,
                        j::Quat::sIdentity(),physics_dt);
                else
                    iface.SetLinearAndAngularVelocity(body.id,
                        j::Vec3::sZero(),j::Vec3::sZero());
            }
        }''')
# Don't use moved_static_deltas alone: currently parked previously-moving
# supports must reduce their passenger momentum to zero.
patch('''        float support_vx=0.f,support_vz=0.f;
        if(!player.IsInvalid() && !moved_static_deltas.empty() &&
           moving_support_dt>1.e-5f) {''',
'''        float support_vx=0.f,support_vz=0.f;
        bool on_kinematic_support=false;
        if(steps>0 && !player.IsInvalid() && !kinematic_platforms.empty() &&
           physics_dt>1.e-5f &&
           iface.GetLinearVelocity(player).GetY()<1.f) {''')
patch('''            for(auto const& entry:moved_static_deltas) {
                auto const index=entry.first;''',
'''            for(auto const index:kinematic_platforms) {''')
patch('''                auto const& moved=entry.second;
                if(std::abs(dx)>body.half+player_half+std::abs(moved[0])+.04f ||
                   std::abs(dz)>body.half+player_half+std::abs(moved[1])+.04f)
                    continue;''',
'''                auto const moved=moved_static_deltas.find(index);
                float const sx=moved==moved_static_deltas.end()?0.f:moved->second[0];
                float const sz=moved==moved_static_deltas.end()?0.f:moved->second[1];
                if(std::abs(dx)>body.half+player_half+std::abs(sx)+.04f ||
                   std::abs(dz)>body.half+player_half+std::abs(sz)+.04f)
                    continue;''')
patch('''                best=score;
                support_vx=moved[0]/moving_support_dt;
                support_vz=moved[1]/moving_support_dt;''',
'''                best=score;
                on_kinematic_support=true;
                support_vx=sx/physics_dt;
                support_vz=sz/physics_dt;''')
patch('''            if(std::hypot(support_vx,support_vz)>45.f)
                support_vx=support_vz=0.f;
        }
        float remaining_root_seconds''',
'''            if(std::hypot(support_vx,support_vz)>45.f)
                support_vx=support_vz=0.f;
            if(on_kinematic_support &&
               std::hypot(support_vx,support_vz)>1.e-4f)
                iface.ActivateBody(player);
        }
        float remaining_root_seconds''')
patch('''                     // Separate input-relative velocity from support motion.
                     // Friction/physics contacts still resolve in Jolt; the
                     // platform itself is not an unswept pawn teleport.
                     auto const relative_x=velocity.GetX()-last_support_vx;
                     auto const relative_z=velocity.GetZ()-last_support_vz;''',
'''                    // Grounded: subtract the previous carrier contribution
                    // before applying this step's support velocity. Airborne:
                    // retain actual world inertia, including takeoff speed.
                    auto const previous_vx=on_kinematic_support?last_support_vx:0.f;
                    auto const previous_vz=on_kinematic_support?last_support_vz:0.f;
                    auto const relative_x=velocity.GetX()-previous_vx;
                    auto const relative_z=velocity.GetZ()-previous_vz;''')
patch('''            last_support_vx=support_vx;
            last_support_vz=support_vz;
            moved_static_deltas.clear();
            moving_support_dt=0.f;''',
'''            last_support_vx=on_kinematic_support?support_vx:0.f;
            last_support_vz=on_kinematic_support?support_vz:0.f;
            moved_static_deltas.clear();
            pending_platform_moves.clear();
            moving_support_dt=0.f;''')
# Remaining additional non-spawn motion:
p.write_text(s,'utf8')
print("Jolt kinematic moving platform patch installed; backup:",backup)
