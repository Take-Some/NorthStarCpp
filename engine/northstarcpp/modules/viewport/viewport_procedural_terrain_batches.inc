    // Render the same absolute-world height samples used by Jolt's
    // streamed triangle colliders. LOD skirts close distant seam gaps.
    // No authored scene cuboids, no camera-relative ground scrolling.
    if(terrain_chunks && !terrain_chunks->empty() && !terrain_material.empty()) {
        current_pick_entity=0; // Native terrain owns collision, not scene picking.
        auto& dst=material_vertices(terrain_material);
        std::size_t triangles=0;
        for(auto const& [key,chunk]:*terrain_chunks)
            triangles+=chunk.render.indices.size()/3;
        dst.reserve(dst.size()+triangles*3);
        for(auto const& [key,chunk]:*terrain_chunks) {
            auto const& m=chunk.render;
            auto const anchor=chunk.anchor;
            for(auto const id:m.indices) {
                if(id>=m.vertices.size())
                    throw std::runtime_error("Streamed terrain index outside render mesh");
                auto const& vertex=m.vertices[id];
                push_material(dst,{
                        static_cast<float>(anchor.x+vertex.position.x),
                        static_cast<float>(anchor.y+vertex.position.y),
                        static_cast<float>(anchor.z+vertex.position.z)},
                    {vertex.normal.x,vertex.normal.y,vertex.normal.z},
                    vertex.u,vertex.v);
            }
        }
    }
