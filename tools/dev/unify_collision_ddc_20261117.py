from pathlib import Path
p=Path('engine/northstarcpp/modules/api/include/northstarcpp/collision_cook_cache.hpp')
s=p.read_text(encoding='utf8')
assert '#include <northstarcpp/derived_data_cache.hpp>' not in s
s=s.replace('#include <northstarcpp/collision_hulls.hpp>',
            '#include <northstarcpp/collision_hulls.hpp>\n#include <northstarcpp/derived_data_cache.hpp>',1)
a=s.index('    std::uint64_t a=14695981039346656037ull')
z=s.index('    return hex.str();',a)+len('    return hex.str();')
new='''    derived_data_cache::KeyBuilder digest;
    digest.bytes("NorthStar/VHACD/4.1/NCH1/maxverts64/vol1");
    digest.u32(splits);
    digest.u32(resolution);
    digest.bytes(mesh.source);
    digest.u32(static_cast<std::uint32_t>(mesh.xyz.size()));
    for(float value:mesh.xyz)digest.float32(value);
    digest.u32(static_cast<std::uint32_t>(mesh.indices.size()));
    for(auto index:mesh.indices)digest.u32(index);
    return digest.hex();'''
s=s[:a]+new+s[z:]
a=s.index('    {\n        std::error_code error;\n        if(std::filesystem::exists(file,error)')
z=s.index('    auto cooked=collision_hulls::cook(',a)
new='''    if(auto bytes=derived_data_cache::read_blob(
           file,collision_hulls::max_bytes)) {
        auto decoded=collision_hulls::decode(*bytes);
        if(decoded && decoded->source==mesh.source &&
           decoded->splits_num==splits) {
            if(progress)progress->store(100);
            return {std::move(*decoded),true};
        }
    }
'''
s=s[:a]+new+s[z:]
a=s.index('    auto bytes=collision_hulls::encode(cooked);')
z=s.index('    if(progress)progress->store(100);',a)
new='''    auto bytes=collision_hulls::encode(cooked);
    derived_data_cache::publish_blob(file,bytes,cancel);
'''
s=s[:a]+new+s[z:]
p.write_text(s,encoding='utf8')
print('Collision DDC moved to shared key/atomic blob utilities')
