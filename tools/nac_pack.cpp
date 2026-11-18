// Native NorthStar Audio Container packer. No external audio SDK required.
#include <northstarcpp/nac.hpp>
#include "nac_pack_wave.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs=std::filesystem;
namespace nac=northstarcpp::nac;

std::uint32_t stable_id(std::string const& name) {
    std::uint32_t hash=2166136261u;
    for(unsigned char c:name)hash=(hash^c)*16777619u;
    return hash;
}

std::vector<nac::Track> collect(fs::path const& directory) {
    if(!fs::is_directory(directory))
        throw std::runtime_error("NAC source must be a directory");
    std::vector<fs::path> wav;
    for(auto const& entry:fs::recursive_directory_iterator(
            directory,fs::directory_options::skip_permission_denied)) {
        if(entry.is_symlink())continue;
        if(!entry.is_regular_file())continue;
        auto extension=entry.path().extension().u8string();
        std::transform(extension.begin(),extension.end(),extension.begin(),
            [](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(extension==".wav")wav.push_back(entry.path());
    }
    std::sort(wav.begin(),wav.end(),[&](auto const& a,auto const& b) {
        return a.lexically_relative(directory).generic_u8string() <
               b.lexically_relative(directory).generic_u8string();
    });
    if(wav.empty() || wav.size()>1024)
        throw std::runtime_error("NAC v1 requires 1..1024 WAV inputs");
    std::unordered_set<std::uint32_t> ids;
    std::vector<nac::Track> tracks;
    tracks.reserve(wav.size());
    for(auto const& file:wav) {
        auto relative=file.lexically_relative(directory);
        relative.replace_extension();
        auto const name=relative.generic_u8string();
        if(name.empty() || name.size()>47)
            throw std::runtime_error("NAC name exceeds 47 bytes: "+name);
        auto const id=stable_id(name);
        if(!id || !ids.insert(id).second)
            throw std::runtime_error("NAC duplicate/colliding track ID: "+name);
        try {
            tracks.push_back(nac::pack::load_wave(file,name,id));
        } catch(std::exception const& error) {
            throw std::runtime_error(file.u8string()+": "+error.what());
        }
    }
    return tracks;
}
void verify_bytes(std::vector<std::uint8_t> const& bytes,
                  std::size_t expected) {
    std::vector<nac::Track> decoded;
    std::string error;
    if(!nac::decode(bytes,decoded,error) || decoded.size()!=expected)
        throw std::runtime_error("NAC CRC/decoder verification: "+error);
}
int main(int argc,char** argv) {
    try {
        if(argc==3 && std::string(argv[1])=="verify") {
            fs::path const input=fs::u8path(argv[2]);
            std::ifstream file(input,std::ios::binary|std::ios::ate);
            if(!file)throw std::runtime_error("Cannot open NAC for verification");
            auto const size=file.tellg();
            if(size<=0 || size>static_cast<std::streamoff>(nac::max_container_bytes))
                throw std::runtime_error("NAC verify size invalid");
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            file.seekg(0);
            if(!file.read(reinterpret_cast<char*>(bytes.data()),size))
                throw std::runtime_error("Cannot read NAC for verification");
            std::vector<nac::Track> tracks;
            std::string error;
            if(!nac::decode(bytes,tracks,error))
                throw std::runtime_error("NAC invalid: "+error);
            std::cout<<"VALID NEF8/NAC tracks="<<tracks.size()
                     <<" bytes="<<bytes.size()<<'\n';
            return 0;
        }
        if(argc!=4 || std::string(argv[1])!="pack")
            throw std::runtime_error("Usage: nac_pack pack <wav-folder> <output.nac> | verify <file.nac>");
        fs::path const source=fs::u8path(argv[2]);
        fs::path const target=fs::u8path(argv[3]);
        if(target.extension()!=".nac")
            throw std::runtime_error("Output must have .nac extension");
        if(fs::exists(target))
            throw std::runtime_error("Output already exists: refusing to overwrite");
        auto tracks=collect(source);
        std::vector<std::uint8_t> bytes;
        std::string error;
        if(!nac::encode(tracks,bytes,error))
            throw std::runtime_error("NAC encode failed: "+error);
        verify_bytes(bytes,tracks.size());
        fs::create_directories(target.parent_path());
        fs::path temp=target;
        temp+=".tmp";
        if(fs::exists(temp))throw std::runtime_error("Temporary file already exists");
        {
            std::ofstream output(temp,std::ios::binary|std::ios::trunc);
            if(!output || !output.write(reinterpret_cast<char const*>(bytes.data()),
                                        static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("Unable to write temporary NAC");
            output.close();
            if(!output)throw std::runtime_error("NAC write failed");
        }
        std::vector<nac::TrackInfo> inspected;
        if(!nac::inspect_file(temp,inspected,error) ||
           inspected.size()!=tracks.size()) {
            fs::remove(temp);
            throw std::runtime_error("NAC disk index verification failed: "+error);
        }
        fs::rename(temp,target);
        std::cout<<"CREATED "<<target.u8string()<<" tracks="<<tracks.size()
                 <<" bytes="<<bytes.size()<<" format=NEF8 PCM16\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<"NAC pack: "<<e.what()<<'\n';
        return 1;
    }
}
