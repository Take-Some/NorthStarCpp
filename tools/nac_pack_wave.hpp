#pragma once
#include <northstarcpp/nac.hpp>
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
inline Track load_wave(std::filesystem::path const& path,
                       std::string const& name,std::uint32_t id) {
    std::error_code ec;
    auto const file_size=std::filesystem::file_size(path,ec);
    if(ec || file_size<44 || file_size>16ull*1024*1024)
        throw std::runtime_error("WAV invalid size");
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(file_size));
    std::ifstream file(path,std::ios::binary);
    if(!file || !file.read(reinterpret_cast<char*>(buf.data()),buf.size()))
        throw std::runtime_error("WAV read failed");
    auto const tag=[&](std::size_t at, char const* name) {
        for(std::size_t i=0;i<4;++i)
            if(buf[at+i] != std::uint8_t(name[i]))return false;
        return true;
    };
    if(!tag(0,"RIFF") || !tag(8,"WAVE") ||
       std::uint64_t(read32(buf.data()+4))+8>file_size)
        throw std::runtime_error("Not RIFF WAVE");
    auto const end=std::uint64_t(read32(buf.data()+4))+8;
    std::uint32_t rate=0;
    std::uint16_t channels=0;
    std::uint64_t pcm_pos=0,pcm_bytes=0;
    bool got_fmt=false,got_data=false;
    for(std::uint64_t i=12;i+8<=end;) {
        auto const n=std::uint64_t(read32(buf.data()+i+4));
        auto const payload=i+8;
        if(n>end-payload)throw std::runtime_error("Truncated RIFF chunk");
        if(tag(i,"fmt ")) {
            if(got_fmt || n<16)throw std::runtime_error("Bad WAV fmt");
            got_fmt=true;
            auto const* p=buf.data()+payload;
            channels=read16(p+2);rate=read32(p+4);
            if(read16(p)!=1 || (channels!=1 && channels!=2) ||
               rate<8000 || rate>192000 || read16(p+14)!=16 ||
               read16(p+12)!=channels*2 || read32(p+8)!=rate*channels*2)
                throw std::runtime_error("WAV requires PCM16 mono/stereo");
        } else if(tag(i,"data")) {
            if(got_data)throw std::runtime_error("Multiple data chunks");
            got_data=true;pcm_pos=payload;pcm_bytes=n;
        }
        i=payload+n+(n&1);
    }
    if(!got_fmt || !got_data || !pcm_bytes ||
       (pcm_bytes % (channels*2)))
        throw std::runtime_error("Incomplete WAV PCM frames");
    Track out;
    out.id=id;out.name=name;out.sample_rate=rate;out.channels=channels;
    out.samples.resize(static_cast<std::size_t>(pcm_bytes/2));
    for(std::size_t i=0;i<out.samples.size();++i)
        out.samples[i]=static_cast<std::int16_t>(read16(buf.data()+pcm_pos+i*2));
    return out;
}
} // namespace northstarcpp::nac::pack
