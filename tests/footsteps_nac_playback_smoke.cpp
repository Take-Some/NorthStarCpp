#include <northstarcpp/audio.hpp>
#include <northstarcpp/nac.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
int main(int argc,char** argv) {
  try {
    if(argc!=3) return 2;
    auto path=std::filesystem::u8path(argv[1]);
    std::vector<northstarcpp::nac::TrackInfo> index;
    std::string error;
    if(!northstarcpp::nac::inspect_file(path,index,error) || index.empty())
      throw std::runtime_error("Invalid NAC index: "+error);
    std::ifstream in(path,std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    std::vector<northstarcpp::nac::Track> tracks;
    if(!northstarcpp::nac::decode(bytes,tracks,error) || tracks.size()!=index.size())
      throw std::runtime_error("Invalid NEF8 PCM: "+error);
    auto selected=tracks.end();
    for(auto it=tracks.begin();it!=tracks.end();++it)
      if(it->name.rfind("walk_",0)==0){selected=it;break;}
    if(selected==tracks.end() || selected->sample_rate!=48000 ||
       selected->channels!=1)throw std::runtime_error("No 48kHz walking clip");
    northstarcpp::AudioEngine audio;
    if(audio.provider_name()!=argv[2])throw std::runtime_error("Wrong provider");
    NorthStarAudioVoiceOptions opts{
       sizeof(NorthStarAudioVoiceOptions),0.75f,0,0,{0,0,0}};
    if(audio.supports_voices()) {
      auto id=audio.play_nac_track(bytes,selected->id,opts);
      if(!id)throw std::runtime_error("No voice allocated");
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
      audio.stop_voice(id);
    } else {
      NorthStarPcm16 pcm{selected->samples.data(),
         static_cast<std::uint32_t>(selected->samples.size()),
         selected->sample_rate,selected->channels};
      audio.play(pcm);
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
      audio.stop();
    }
    std::cout<<"PASS "<<audio.provider_name()<<" "<<selected->name<<"\n";
    return 0;
  } catch(std::exception const& e) {
    std::cerr<<"Footsteps NEF8 audio test failed: "<<e.what()<<"\n";
    return 1;
  }
}
