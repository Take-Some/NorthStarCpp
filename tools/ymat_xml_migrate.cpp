// Controlled project YMAT normalization. Validates every material before
// saving; backups are stored OUTSIDE of the active asset directories.
#include <northstarcpp/material_ymat_authoring.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
using namespace northstarcpp::shared_assets;
static std::vector<std::uint8_t> read(fs::path const& path) {
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("Cannot open YMAT: "+path.string());
    return {std::istreambuf_iterator<char>(stream),{}};
}
static void write(fs::path const& path,
                  std::vector<std::uint8_t> const& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out)throw std::runtime_error("Cannot write: "+path.string());
    out.write(reinterpret_cast<char const*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if(!out)throw std::runtime_error("Cannot flush: "+path.string());
}
int main(int argc,char** argv) {
    if(argc<4)return 2;
    try {
        bool const apply=std::string(argv[1])=="--apply";
        if(!apply && std::string(argv[1])!="--dry-run")
            throw std::invalid_argument("Expected --apply or --dry-run");
        fs::path const root=fs::absolute(fs::u8path(argv[2]));
        fs::path const backups=fs::absolute(fs::u8path(argv[3]));
        struct Job {
            fs::path path,backup;
            std::vector<std::uint8_t> old_data,xml_data;
            std::size_t entries;
            bool conversion;
        };
        std::vector<Job> jobs;
        for(int i=4;i<argc;++i) {
            fs::path relative=fs::u8path(argv[i]);
            if(relative.empty()||relative.is_absolute())
                throw std::invalid_argument("Expected project-relative file");
            for(auto const& part:relative)
                if(part=="..")
                    throw std::invalid_argument("Directory traversal denied");
            auto path=root/relative;
            auto old=read(path);
            auto prior=load_materials_from_bytes(old,relative.generic_string());
            auto converted=migrate_ymat_to_xml(old);
            auto after=load_materials_from_bytes(converted,relative.generic_string());
            if(!is_xml_ymat(converted)||prior.size()!=after.size())
                throw std::runtime_error("YMAT conversion failed validation");
            for(std::size_t j=0;j<prior.size();++j) {
                auto const& a=prior[j];
                auto const& b=after[j];
                if(a.name!=b.name||a.shader!=b.shader||a.blend!=b.blend||
                   a.two_sided!=b.two_sided||a.parameters.size()!=b.parameters.size()||
                   a.textures.size()!=b.textures.size())
                    throw std::runtime_error("YMAT changed material identity");
                for(std::size_t k=0;k<a.textures.size();++k)
                    if(a.textures[k].slot!=b.textures[k].slot ||
                       a.textures[k].reference!=b.textures[k].reference)
                        throw std::runtime_error("YMAT changed texture bindings");
            }
            bool changed=converted!=old;
            std::cout<<(changed?"CONVERT ":"KEEP ")<<relative.generic_string()
                     <<" entries="<<prior.size()
                     <<" bytes="<<old.size()<<" -> "<<converted.size()<<"\n";
            jobs.push_back(Job{path,backups/relative,std::move(old),
                               std::move(converted),prior.size(),changed});
        }
        if(!apply)return 0;
        for(auto const& job:jobs) {
            if(!job.conversion)continue;
            if(fs::exists(job.backup))
                throw std::runtime_error("Backup already exists: "+
                                         job.backup.string());
            if(read(job.path)!=job.old_data)
                throw std::runtime_error("Source changed after preflight");
        }
        // Originals are retained for rollback in the QA build directory.
        for(auto const& job:jobs)
            if(job.conversion)write(job.backup,job.old_data);
        for(auto const& job:jobs) {
            if(!job.conversion)continue;
            auto temporary=job.path;
            temporary+=".xml-migration.tmp";
            if(fs::exists(temporary))
                throw std::runtime_error("Temporary output already exists");
            write(temporary,job.xml_data);
            if(read(temporary)!=job.xml_data)
                throw std::runtime_error("YMAT staged write corrupt");
            fs::rename(temporary,job.path);
            std::cout<<"MIGRATED "<<job.path.string()<<"\n";
        }
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"YMAT XML migration failed: "<<e.what()<<"\n";
        return 1;
    }
}
