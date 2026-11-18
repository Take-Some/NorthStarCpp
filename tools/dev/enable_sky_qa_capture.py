#!/usr/bin/env python3
"""Add bounded PPM sky-only readbacks to the existing real-GPU QA harness."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
p=ROOT/"tests/sky_gpu_dynamics_smoke.cpp"
s=p.read_text(encoding="utf8")
needle="                    result[0],result[1],result[2]);"
capture=r"""
                if(save_sky) {
                    std::filesystem::path const directory="out/qa/sky_frames";
                    std::filesystem::create_directories(directory);
                    std::string safe_label(label);
                    for(char& c:safe_label)
                        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                             (c>='0'&&c<='9')||c=='-'))c='_';
                    auto const name=std::string(mod->provider)+"_"+
                        std::to_string(frame_dump_index++)+"_"+safe_label+".ppm";
                    std::ofstream stream(directory/name,std::ios::binary);
                    require(stream.good(),"Cannot write GPU sky capture");
                    stream<<"P6\n"<<width<<" "<<height<<"\n255\n";
                    for(std::size_t i=0;i<result.size();i+=4)
                        stream.write(reinterpret_cast<char const*>(&result[i]),3);
                    require(stream.good(),"Truncated sky capture PPM");
                    std::printf("SKY_CAPTURE %s\n",(directory/name).string().c_str());
                }"""
assert needle in s
assert "SKY_CAPTURE %s" not in s
s=s.replace(needle,needle+capture,1)
marker="        if(view_sweep) {"
assert s.count(marker)==1
motion=r"""
        if(save_sky && capture) {
            // A short repeatable camera-motion trace with real overcast
            // clouds; the old view sweep disables clouds deliberately.
            config.time_of_day=12.;
            config.clouds=true;
            config.cloud_coverage=.75;
            config.haze=.45;
            env.configure(config);
            for(int k=0;k<12;++k) {
                frame.camera_yaw=float(k)*.004f;
                frame.camera_x=float(k)*.12f;
                auto name=std::string("cloud-motion-")+std::to_string(k);
                draw(name.c_str());
            }
            frame.camera_x=0.f;
            frame.camera_yaw=0.f;
        }
"""
s=s.replace(marker,motion+marker,1)
p.write_text(s,encoding="utf8")
print("PASS GPU capture harness: PPM frames and 12 camera-motion poses")
