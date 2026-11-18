#pragma once
#include <northstarcpp/nac.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace northstarcpp::nac::pack {
inline std::uint32_t read32(std::uint8_t const* b) {
    return std::uint32_t(b[0]) | (std::uint32_t(b[1])<<8) |
           (std::uint32_t(b[2])<<16) | (std::uint32_t(b[3])<<24);
}
inline std::uint16_t read16(std::uint8_t const* b) {
    return std::uint16_t(std::uint16_t(b[0]) | (std::uint16_t(b[1])<<8));
}
class UnsupportedWaveEncoding : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline Track load_wave(std::filesystem::path const& path,
                       std::string const& name,std::uint32_t id,
                       std::size_t pcm_budget=max_pcm_bytes) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Cannot open WAV source");
    auto const file_size=file.tellg();
    auto read=[&](std::uint8_t* data,std::size_t size) {
        if(!file.read(reinterpret_cast<char*>(data),static_cast<std::streamsize>(size)))
            throw std::runtime_error("Truncated WAV source");
    };
    std::array<std::uint8_t,12> header{};
    file.seekg(0);
    read(header.data(),header.size());
    if(std::memcmp(header.data(),"RIFF",4) ||
       std::memcmp(header.data()+8,"WAVE",4))
        throw UnsupportedWaveEncoding("Expected RIFF/WAVE audio");
    auto const end=std::uint64_t(read32(header.data()+4))+8;
    if(end<header.size() || file_size<0 || end>static_cast<std::uint64_t>(file_size))
        throw std::runtime_error("Truncated RIFF/WAVE body");

    std::uint32_t rate=0;
    std::uint16_t channels=0;
    std::uint64_t pcm_pos=0,pcm_bytes=0;
    bool got_fmt=false,got_data=false;
    for(std::uint64_t offset=header.size();offset<end;) {
        std::array<std::uint8_t,8> chunk{};
        if(end-offset<chunk.size())throw std::runtime_error("Truncated WAV chunk header");
        file.seekg(static_cast<std::streamoff>(offset));
        read(chunk.data(),chunk.size());
        auto const size=std::uint64_t(read32(chunk.data()+4));
        auto const payload=offset+chunk.size();
        auto const padded=size+(size&1);
        if(padded>end-payload)throw std::runtime_error("Truncated WAV chunk");
        if(!std::memcmp(chunk.data(),"fmt ",4)) {
            std::array<std::uint8_t,16> format{};
            if(got_fmt || size<format.size())throw std::runtime_error("Invalid WAV format chunk");
            got_fmt=true;
            read(format.data(),format.size());
            auto const* p=format.data();
            channels=read16(p+2);rate=read32(p+4);
            if(read16(p)!=1 || (channels!=1 && channels!=2) ||
               rate<8000 || rate>192000 || read16(p+14)!=16)
                throw UnsupportedWaveEncoding("WAV needs conversion to PCM16 mono/stereo");
            if(read16(p+12)!=channels*2 || read32(p+8)!=rate*channels*2)
                throw std::runtime_error("Inconsistent WAV byte rate or frame alignment");
        } else if(!std::memcmp(chunk.data(),"data",4)) {
            if(got_data)throw std::runtime_error("Multiple WAV data chunks");
            got_data=true;pcm_pos=payload;pcm_bytes=size;
        }
        offset=payload+padded;
    }
    if(!got_fmt || !got_data || !pcm_bytes || pcm_bytes%(channels*2))
        throw std::runtime_error("Incomplete WAV PCM frames");
    if(pcm_bytes>pcm_budget)
        throw std::runtime_error("NAC: the decoded audio exceeds the available PCM capacity in this pack");

    Track out;
    out.id=id;out.name=name;out.sample_rate=rate;out.channels=channels;
    out.samples.resize(static_cast<std::size_t>(pcm_bytes/2));
    file.seekg(static_cast<std::streamoff>(pcm_pos));
    std::array<std::uint8_t,64*1024> block{};
    for(std::size_t written=0;written<out.samples.size();) {
        auto const count=std::min(block.size()/2,out.samples.size()-written);
        read(block.data(),count*2);
        for(std::size_t i=0;i<count;++i)
            out.samples[written+i]=static_cast<std::int16_t>(read16(block.data()+i*2));
        written+=count;
    }
    return out;
}
} // namespace northstarcpp::nac::pack
