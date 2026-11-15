// Shader/PSO disk cache regression: deterministic keys, validation,
// corruption recovery and opt-out. Owns only isolated QA fixture files.
#include "shader_disk_cache.hpp"
#include <windows.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    namespace cache=northstarcpp::render::shader_disk_cache;
    namespace fs=std::filesystem;
    try {
        fs::path root=fs::u8path(argv[1]);
        root/=std::to_string(GetCurrentProcessId());
        if(!fs::create_directories(root) && !fs::is_directory(root))
            throw std::runtime_error("Cannot create owned QA cache root");
        if(!SetEnvironmentVariableW(
            L"NORTHSTARCPP_SHADER_CACHE_DIR",root.c_str()))
            throw std::runtime_error("Could not set QA cache location");
        SetEnvironmentVariableW(L"NORTHSTARCPP_SHADER_CACHE_DISABLE",nullptr);
        std::string const key="material|variant=foo|shader=ps_5_0|PSO=1";
        std::vector<std::uint8_t> bytes{0,1,9,17,250,27,0,3};
        cache::write("d3d12",key,bytes.data(),bytes.size());
        auto result=cache::read("d3d12",key);
        if(!result || *result!=bytes)
            throw std::runtime_error("Cached bytecode roundtrip failed");
        if(cache::read("d3d12",key+"different"))
            throw std::runtime_error("Unexpected cache collision accepted");
        auto const path=cache::entry_path(
            root/"v1","d3d12",key);
        if(!fs::is_regular_file(path))
            throw std::runtime_error("Cache file not atomically committed");
        // Break payload integrity; a poisoned persistent entry must be ignored.
        {
            std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);
            if(!file)throw std::runtime_error("Cannot mutate QA cache fixture");
            file.seekp(-1,std::ios::end);
            char bad=static_cast<char>(0x0f);
            file.write(&bad,1);
        }
        if(cache::read("d3d12",key))
            throw std::runtime_error("Corrupted shader cache was accepted");
        cache::write("d3d12",key,bytes.data(),bytes.size());
        if(cache::read("d3d12",key)!=std::optional(bytes))
            throw std::runtime_error("Cache did not recover from corruption");
        SetEnvironmentVariableW(L"NORTHSTARCPP_SHADER_CACHE_DISABLE",L"1");
        if(cache::read("d3d12",key))
            throw std::runtime_error("Disabled shader cache still served data");
        std::string const disabled="disabled-no-write";
        cache::write("d3d12",disabled,bytes.data(),bytes.size());
        if(fs::exists(cache::entry_path(root/"v1","d3d12",disabled)))
            throw std::runtime_error("Shader cache disable ignored by writer");
        SetEnvironmentVariableW(L"NORTHSTARCPP_SHADER_CACHE_DISABLE",nullptr);
        std::cout<<"Shader disk cache validation, corruption recovery and opt-out: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Shader disk cache QA: FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
