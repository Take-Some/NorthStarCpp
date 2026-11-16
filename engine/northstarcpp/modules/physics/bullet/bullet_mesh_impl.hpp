// Included by bullet.cpp after World is defined.
inline void World::configure_mesh(uint32_t index,uint32_t mode,uint32_t splits,
                      const float* vertices,uint32_t vertex_count,
                      const uint32_t* indices,uint32_t index_count) {
    if(index>=bodies.size()||!bodies[index].alive ||
       (mode!=3 && mode!=4) ||
       (mode==3 && !bodies[index].rigid->isStaticObject()))
        throw std::invalid_argument(
            "Bullet Complex Mesh only supports static objects; choose Auto Convex for dynamic objects");
    auto cooked=northstarcpp::bullet_mesh::build(
        vertices,vertex_count,indices,index_count,mode,splits);
    auto& body=bodies[index];
    auto* rigid=body.rigid.get();
    float const mass=rigid->getInvMass()>0.f?
        1.f/float(rigid->getInvMass()):0.f;
    btVector3 inertia(0,0,0);
    if(mass>0.f)cooked.shape->calculateLocalInertia(mass,inertia);
    bool const was_resident=body.resident;
    if(was_resident) {
        world.removeRigidBody(rigid);
        body.resident=false;
    }
    rigid->setCollisionShape(cooked.shape.get());
    rigid->setMassProps(mass,inertia);
    rigid->updateInertiaTensor();
    // Destroy old shape before releasing child hulls/mesh buffers.
    body.shape.reset();
    body.child_shape.reset();
    body.convex_children.clear();
    body.mesh_triangles.reset();
    body.mesh_triangles=std::move(cooked.triangles);
    body.convex_children=std::move(cooked.children);
    body.shape=std::move(cooked.shape);
    if(was_resident&&body.collision_enabled) {
        world.addRigidBody(rigid);
        world.updateSingleAabb(rigid);
        rigid->activate(true);
        body.resident=true;
    }
}
