#include <northstarcpp/nac.hpp>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
    namespace nac=northstarcpp::nac;
    nac::Track one;
    one.id=11;one.name="Engine";one.sample_rate=48000;
    one.channels=1;one.looping=true;one.loop_start=1;one.loop_end=4;
    one.samples={1000,-2000,3000,-4000};
    nac::Track two;
    two.id=12;two.name="Traffic";two.sample_rate=22050;
    two.channels=2;two.samples={1,2,3,4,5,6};
    std::vector<nac::Track> inputs{one,two};
    std::vector<std::uint8_t> bytes;
    std::string error;
    if(!nac::encode(inputs,bytes,error)) {
        std::cerr<<error;return 1;
    }
    if(bytes.size()<160 || bytes[0]!='N' || bytes[1]!='E' ||
       bytes[2]!='F' || bytes[3]!='8' ||
       bytes[8]!=12 || bytes[12]!=1) return 2;
    std::vector<nac::Track> output;
    if(!nac::decode(bytes,output,error)) {
        std::cerr<<error;return 3;
    }
    if(output.size()!=2 || output[0].id!=11 ||
       output[0].samples!=one.samples || output[1].samples!=two.samples ||
       !output[0].looping || output[0].loop_start!=1 ||
       output[0].loop_end!=4 || output[1].channels!=2) return 4;
    auto corrupted=bytes;
    corrupted[64]^=0x40; // mutate canonical NEF8 BLAKE3 digest
    if(nac::decode(corrupted,output,error) || !output.empty())return 5;
    corrupted=bytes;
    corrupted[4]=2; // unsupported envelope major
    if(nac::decode(corrupted,output,error))return 6;
    corrupted=bytes;
    corrupted[8]=6; // NEF8 Scene cannot be decoded as NAC
    if(nac::decode(corrupted,output,error))return 7;
    corrupted=bytes;
    corrupted[128]='!'; // invalid embedded audio index
    if(nac::decode(corrupted,output,error))return 8;
    inputs[1].id=11;
    if(nac::encode(inputs,bytes,error) || !bytes.empty())return 9;
    inputs[1].id=12;
    inputs[0].loop_start=9;
    if(nac::encode(inputs,bytes,error) || !bytes.empty())return 10;
    // NAC1 envelopes are not silently accepted as new NEF8 assets.
    std::vector<std::uint8_t> legacy{'N','A','C','1',0,0,0,0};
    if(nac::decode(legacy,output,error))return 11;

    // A soundtrack-sized track must cross the former 64 MiB NAC body limit.
    std::vector<nac::Track> extended(1);
    auto& clip=extended.front();
    clip.id=20;clip.name="LongClip";clip.sample_rate=48000;clip.channels=2;
    clip.samples.resize(34u*1024*1024);
    clip.samples.front()=101;
    clip.samples.back()=-302;
    if(!nac::encode(extended,bytes,error) || !nac::decode(bytes,output,error)) {
        std::cerr<<error;return 12;
    }
    if(output.size()!=1 || output.front().id!=clip.id ||
       output.front().samples!=clip.samples)return 13;
    return 0;
}
