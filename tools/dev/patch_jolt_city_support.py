#!/usr/bin/env python3
"""Add physically carried pawn velocity for continuously translating Jolt static supports.

It is intentionally bounded to small frame-to-frame moves; streamed terrain and
teleports do not impart velocity. A moving CityRoot remains a kinematic authored
collection, not an unconstrained dynamic rigidbody.
"""
from pathlib import Path
import shutil
p=Path(__file__).resolve().parents[2]/"engine/northstarcpp/modules/physics/jolt/jolt.cpp"
s=p.read_text("utf8")
backup=p.with_name("jolt.cpp.bak-pre-rustsea-carrier-20261117")
if not backup.exists():shutil.copy2(p,backup)
def put(before,after):
 global s
 n=s.count(before)
 if n!=1:raise RuntimeError(f"Expected unique C++ patch section, got {n}: {before[:80]}")
 s=s.replace(before,after,1)
put("#include <algorithm>","#include <algorithm>\n#include <array>")
put('''    void move_static(uint32_t index, float x, float y, float z)
    {
        if (index >= bodies.size() || !bodies[index].alive ||
            !bodies[index].is_static ||
            !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            throw std::runtime_error("Invalid Jolt static relocation");
        auto& iface = physics.GetBodyInterface();
        iface.SetPosition(bodies[index].id, j::RVec3(x, y + bodies[index].half, z),
            j::EActivation::DontActivate);
    }''',
'''    void move_static(uint32_t index, float x, float y, float z)
    {
        if (index >= bodies.size() || !bodies[index].alive ||
            !bodies[index].is_static || !bodies[index].resident ||
            !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            throw std::runtime_error("Invalid Jolt static relocation");
        auto& iface = physics.GetBodyInterface();
        auto const old=iface.GetPosition(bodies[index].id);
        float const dx=x-static_cast<float>(old.GetX());
        float const dz=z-static_cast<float>(old.GetZ());
        float const motion=std::hypot(dx,dz);
        // Sample continuous kinematic support displacement. Deliberate
        // teleports, large streaming jumps and vertical edits do not carry.
        if(motion>.00001f && motion<=2.5f &&
           std::abs(y+bodies[index].half-static_cast<float>(old.GetY()))<.12f) {
            auto& delta=moved_static_deltas[index];
            delta[0]+=dx;
            delta[1]+=dz;
        } else if(motion>2.5f) {
            moved_static_deltas.erase(index);
        }
        iface.SetPosition(bodies[index].id, j::RVec3(x, y + bodies[index].half, z),
            j::EActivation::DontActivate);
    }''')
put("    std::vector<Body> bodies;\n    uint32_t initial_body_count = 0;",
'''    std::vector<Body> bodies;
    // Provider-owned support velocity: carried dynamic characters inherit
    // moving-platform world velocity but keep their own input-relative motor.
    std::unordered_map<std::uint32_t,std::array<float,2>> moved_static_deltas;
    float moving_support_dt=0.f;
    float last_support_vx=0.f, last_support_vz=0.f;
    uint32_t initial_body_count = 0;''')
put('''        elapsed += std::min(static_cast<double>(delta), max_frame_time);
        auto& iface = physics.GetBodyInterface();
        float remaining_root_seconds=root_motion_duration;''',
'''        auto const admitted=std::min(static_cast<double>(delta),max_frame_time);
        elapsed += admitted;
        moving_support_dt+=static_cast<float>(admitted);
        auto& iface = physics.GetBodyInterface();
        // Jolt receives authored static moves before this fixed physics step.
        // Find the support directly under the possessed body; do not carry
        // airborne players or actors walking on world terrain.
        float support_vx=0.f,support_vz=0.f;
        if(!player.IsInvalid() && !moved_static_deltas.empty() &&
           moving_support_dt>1.e-5f) {
            auto const p=iface.GetPosition(player);
            float const player_half=(controlled_index<bodies.size())
                ? bodies[controlled_index].half:.75f;
            float const foot=static_cast<float>(p.GetY())-player_half;
            float best=std::numeric_limits<float>::max();
            for(auto const& entry:moved_static_deltas) {
                auto const index=entry.first;
                if(index>=bodies.size())continue;
                auto const& body=bodies[index];
                if(!body.alive || !body.resident || !body.is_static ||
                   !body.collision_enabled)continue;
                auto const o=iface.GetPosition(body.id);
                float const top=static_cast<float>(o.GetY())+body.half;
                if(std::abs(foot-top)>.21f)continue;
                float const dx=static_cast<float>(p.GetX()-o.GetX());
                float const dz=static_cast<float>(p.GetZ()-o.GetZ());
                // Include swept motion to retain support at tile seams.
                auto const& moved=entry.second;
                if(std::abs(dx)>body.half+player_half+std::abs(moved[0])+.04f ||
                   std::abs(dz)>body.half+player_half+std::abs(moved[1])+.04f)
                    continue;
                auto const score=dx*dx+dz*dz;
                if(score>=best)continue;
                best=score;
                support_vx=moved[0]/moving_support_dt;
                support_vz=moved[1]/moving_support_dt;
            }
            // Guard against accidental transport from high-speed teleports.
            if(std::hypot(support_vx,support_vz)>45.f)
                support_vx=support_vz=0.f;
        }
        float remaining_root_seconds=root_motion_duration;''')
put('''                    iface.SetLinearVelocity(player,j::Vec3(
                        velocity.GetX()+(x*desired_speed+root_vx-velocity.GetX())*accel,
                        vertical,
                        velocity.GetZ()+(z*desired_speed+root_vz-velocity.GetZ())*accel));''',
'''                     // Separate input-relative velocity from support motion.
                     // Friction/physics contacts still resolve in Jolt; the
                     // platform itself is not an unswept pawn teleport.
                     auto const relative_x=velocity.GetX()-last_support_vx;
                     auto const relative_z=velocity.GetZ()-last_support_vz;
                    iface.SetLinearVelocity(player,j::Vec3(
                         support_vx+relative_x+
                             (x*desired_speed+root_vx-relative_x)*accel,
                        vertical,
                         support_vz+relative_z+
                             (z*desired_speed+root_vz-relative_z)*accel));''')
put('''        if(stepped)root_motion_x=root_motion_z=root_motion_duration=0.f;''',
'''        if(stepped) {
            root_motion_x=root_motion_z=root_motion_duration=0.f;
            last_support_vx=support_vx;
            last_support_vz=support_vz;
            moved_static_deltas.clear();
            moving_support_dt=0.f;
        }''')
p.write_text(s,"utf8")
print("Jolt support velocity implemented, source backed up:",backup)
