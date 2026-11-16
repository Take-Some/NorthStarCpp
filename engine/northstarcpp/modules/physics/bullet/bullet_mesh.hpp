// Bullet provider: cooked triangle mesh or grouped convex hulls.
#pragma once
#include "../collision_split.hpp"
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include <memory>
#include <vector>
#include <stdexcept>
namespace northstarcpp::bullet_mesh {
struct Built {
    std::unique_ptr<btTriangleMesh> triangles;
    std::vector<std::unique_ptr<btCollisionShape>> children;
    std::unique_ptr<btCollisionShape> shape;
};
inline Built build(const float* xyz,std::uint32_t n,const std::uint32_t* indices,
                   std::uint32_t count,std::uint32_t mode,std::uint32_t splits) {
    auto groups=physics_split::groups(xyz,n,indices,count,splits);
    Built out;
    if(mode==3) {
        out.triangles=std::make_unique<btTriangleMesh>();
        for(std::uint32_t i=0;i<count;i+=3) {
            btVector3 p[3];
            for(int j=0;j<3;++j) {
                auto k=3*indices[i+j];
                p[j]=btVector3(xyz[k],xyz[k+1],xyz[k+2]);
            }
            out.triangles->addTriangle(p[0],p[1],p[2],true);
        }
        out.shape=std::make_unique<btBvhTriangleMeshShape>(
            out.triangles.get(),true);
        return out;
    }
    if(mode!=4)throw std::invalid_argument("Unsupported Bullet mesh collision mode");
    auto compound=std::make_unique<btCompoundShape>();
    btTransform identity;
    identity.setIdentity();
    for(auto const& group:groups) {
        auto hull=std::make_unique<btConvexHullShape>();
        for(auto idx:group.vertices) {
            auto const k=idx*3;
            hull->addPoint(btVector3(xyz[k],xyz[k+1],xyz[k+2]),false);
        }
        hull->recalcLocalAabb();
        compound->addChildShape(identity,hull.get());
        out.children.push_back(std::move(hull));
    }
    if(out.children.empty())
        throw std::runtime_error("No valid Bullet collision hulls");
    out.shape=std::move(compound);
    return out;
}
} // namespace northstarcpp::bullet_mesh
