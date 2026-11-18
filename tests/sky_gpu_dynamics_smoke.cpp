// GPU Sky QA: native set_sky() + real framebuffer readback when supported.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/render_capture_api.h>
#include <northstarcpp/render_sky_api.h>
#include <northstarcpp/sky_environment.hpp>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool ok,const char* why){
    if(!ok)throw std::runtime_error(why);
}
static std::uint64_t changed(std::vector<std::uint8_t> const& a,
                            std::vector<std::uint8_t> const& b) {
    require(a.size()==b.size(),"GPU frame sizes differ");
    std::uint64_t n=0;
    for(std::size_t i=0;i<a.size();i+=4)
        n+=(std::abs(int(a[i])-int(b[i]))+
            std::abs(int(a[i+1])-int(b[i+1]))+
            std::abs(int(a[i+2])-int(b[i+2]))>9);
    return n;
}
int main(int argc,char** argv) {
    if(argc<2||argc>6)return 2;
    bool gpu_uniform_test=false,warp=false,view_sweep=false,save_sky=false;
    for(int i=2;i<argc;++i) {
        if(std::string(argv[i])=="--warp")warp=true;
        else if(std::string(argv[i])=="--view-sweep")view_sweep=true;
        else if(std::string(argv[i])=="--save-sky")save_sky=true;
        else if(std::string(argv[i])=="--gpu-uniforms")gpu_uniform_test=true;
        else return 2;
    }
    HMODULE dll=nullptr;HWND window=nullptr;void* ctx=nullptr;
    const NorthStarRenderApi* gpu=nullptr;
    const NorthStarModule* mod=nullptr;
    const wchar_t* cls=L"NorthStarSkyGpuSmoke";
    bool registered=false;
    auto cleanup=[&]{
        if(ctx&&gpu)gpu->destroy(ctx);
        if(window)DestroyWindow(window);
        if(registered)UnregisterClassW(cls,GetModuleHandleW(nullptr));
        if(dll)FreeLibrary(dll);
    };
    try {
        dll=LoadLibraryW(std::filesystem::path(argv[1]).wstring().c_str());
        require(dll!=nullptr,"Cannot load GPU provider");
        auto query=reinterpret_cast<NorthStarModuleQuery>(
            GetProcAddress(dll,"northstar_module_query"));
        require(query!=nullptr,"Missing module query");
        mod=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_ABI);
        require(mod && mod->api,"Missing render ABI");
        gpu=static_cast<const NorthStarRenderApi*>(mod->api);
        require(gpu->create&&gpu->destroy&&gpu->draw&&gpu->set_sky,
                "Missing sky functions");
        const auto ext=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_CAPTURE_MODULE_ABI);
        const auto* capture=ext?
            static_cast<const NorthStarRenderCaptureApi*>(ext->api):nullptr;
        const auto sky_extension=query(NORTHSTARCPP_MODULE_ABI,
            NORTHSTARCPP_CAPABILITY_RENDER,NORTHSTARCPP_RENDER_SKY_MODULE_ABI);
        const auto* sky_gpu=sky_extension?
            static_cast<const NorthStarRenderSkyApi*>(sky_extension->api):nullptr;
        if(gpu_uniform_test)
            require(sky_gpu && sky_gpu->abi_version==NORTHSTARCPP_RENDER_SKY_ABI &&
                    sky_gpu->set_uniforms,"GPU Sky uniform ABI not available");
        WNDCLASSW wc{};
        wc.lpfnWndProc=DefWindowProcW;
        wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=cls;
        registered=RegisterClassW(&wc)!=0;
        require(registered,"Cannot register window");
        window=CreateWindowW(cls,L"Sky Shader GPU QA",WS_OVERLAPPEDWINDOW,
            100,100,400,300,nullptr,nullptr,wc.hInstance,nullptr);
        require(window!=nullptr,"Cannot create GPU window");
        ShowWindow(window,SW_SHOW);
        ctx=gpu->create(window,warp?1u:0u);
        if(!ctx)throw std::runtime_error(std::string("Cannot create renderer: ")+
            (mod->last_error?mod->last_error():"unknown"));
        NorthStarSkyVertex vertices[]{
            {-6,-6,2,0,0},{6,-6,2,1,0},
            {6,6,2,1,1},{-6,6,2,0,1}};
        std::uint32_t indices[]{0,1,2,0,2,3};
        NorthStarRenderFrame frame{};
        frame.selected=-1;
        // Explicit first-person camera: sky rays on both backends must match
        // the camera's yaw/pitch instead of D3D12 third-person focus orbit.
        frame.first_person=1u;
        frame.camera_fov_y_degrees=70.f;
        frame.camera_near_clip=.1f;
        frame.camera_far_clip=250.f;
        // Vulkan has no public capture ABI: at least require its internal
        // verify_pixels GPU-readback path for each sky update.
        frame.verify_pixels=capture?0u:1u;
        northstarcpp::SkyEnvironment env;
        northstarcpp::SkySettings config{};
        config.enabled=true;config.sun=true;
        config.moon=false;config.stars=false;config.clouds=true;
        config.time_of_day=12.;config.cloud_coverage=.3;
        config.cloud_speed=.12;config.day_length_seconds=86400;
        // Equinox at equator: 18:00 is a genuine solar sunset;
        // at northern summer 18:00 can still be full daylight.
        config.latitude_degrees=0.;config.day_of_year=81;
        env.configure(config);
        unsigned texture_uploads=0;
        // Use the production GPU source atlas for uniform-only testing.
        // CPU-composited colored sky is legacy fallback, NOT cloud weather.
        auto source_texture=gpu_uniform_test?
            northstarcpp::SkyEnvironment::render_gpu_cloud_rgba(128,64):
            env.render_rgba(128,64);
        if(gpu_uniform_test) {
            NorthStarGpuSkyUniforms invalid{};
            invalid.cloud[0]=1.5f;
            require(!sky_gpu->set_uniforms(ctx,&invalid),
                    "GPU Sky accepted invalid cloud coverage");
        }
        auto submit_uniform=[&] {
            const auto snapshot=env.snapshot();
            NorthStarGpuSkyUniforms uniform{};
            uniform.sun_direction[0]=snapshot.sun_direction.x;
            uniform.sun_direction[1]=snapshot.sun_direction.y;
            uniform.sun_direction[2]=snapshot.sun_direction.z;
            uniform.moon_direction[0]=snapshot.moon_direction.x;
            uniform.moon_direction[1]=snapshot.moon_direction.y;
            uniform.moon_direction[2]=snapshot.moon_direction.z;
            uniform.atmosphere[0]=float(snapshot.daylight);
            uniform.atmosphere[1]=float(snapshot.settings.haze);
            uniform.atmosphere[2]=float(snapshot.settings.sun_intensity);
            uniform.atmosphere[3]=float(snapshot.settings.moon_intensity);
            uniform.cloud[0]=float(snapshot.settings.cloud_coverage);
            uniform.cloud[1]=float(snapshot.settings.cloud_speed);
            uniform.cloud[2]=float(snapshot.flow_phase);
            uniform.cloud[3]=float(snapshot.moon_illumination);
            uniform.flags[0]=uniform.flags[1]=uniform.flags[3]=1.f;
            require(sky_gpu->set_uniforms(ctx,&uniform)!=0,
                    "Sky GPU uniform update failed");
        };
        unsigned frame_dump_index=0;
        auto draw=[&](const char* label) {
            auto rgba=env.render_rgba(128,64);
            std::uint64_t srcsum=0;
            for(auto const c:rgba)srcsum+=c;
            std::printf("CPU %s source_checksum=%llu time=%.3f flow=%.3f\n",
                label,(unsigned long long)srcsum,env.snapshot().settings.time_of_day,
                env.snapshot().flow_phase);
            auto* atlas=gpu_uniform_test?source_texture.data():rgba.data();
            NorthStarSky sky{vertices,4,indices,6,atlas,128,64,1,1,1};
            if(!gpu_uniform_test || texture_uploads==0) {
                if(!gpu->set_sky(ctx,&sky))
                    throw std::runtime_error(std::string("set_sky ")+label+": "+
                        (mod->last_error?mod->last_error():"unknown"));
                ++texture_uploads;
            }
            if(gpu_uniform_test)submit_uniform();
            // Explicitly warm the provider's swapchain and update queue,
            // then capture the latest frame, not the previous in-flight one.
            for(int pass=0;pass<4;++pass) {
                if(pass==3 && capture)
                    require(capture->request(ctx)!=0,"Capture request failed");
                if(!gpu->draw(ctx,&frame))
                    throw std::runtime_error(std::string("draw ")+label+": "+
                        (mod->last_error?mod->last_error():"unknown"));
            }
            std::vector<std::uint8_t> result;
            if(capture) {
                std::uint32_t width=0,height=0;
                // Vulkan Capture ABI is non-blocking: poll the submitted
                // fence instead of invoking vkQueueWaitIdle or a CPU wait.
                bool ready=false;
                for(int retry=0;retry<1500;++retry) {
                    if(capture->read(ctx,nullptr,0,&width,&height)) {
                        ready=true;break;
                    }
                    Sleep(1);
                }
                require(ready,"GPU async readback did not complete");
                require(width>50&&height>50&&width<=4096&&height<=4096,
                        "Invalid readback dimensions");
                result.resize(std::size_t(width)*height*4);
                require(capture->read(ctx,result.data(),
                    std::uint32_t(result.size()),&width,&height)!=0,
                    "GPU pixel copy failed");
                std::uint64_t sum=0;
                for(std::size_t i=0;i<result.size();i+=4)
                    sum+=result[i]+result[i+1]+result[i+2];
                std::printf("GPU %s frame %ux%u bytes=%zu rgb_checksum=%llu first=%u,%u,%u\n",
                    label,width,height,result.size(),(unsigned long long)sum,
                    result[0],result[1],result[2]);
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
                }
            }
            return result;
        };
        auto noon=draw("noon");
        config.cloud_coverage=.96;env.configure(config);
        auto noon_overcast=draw("noon-cloud-coverage");
        config.cloud_coverage=.30;
        config.time_of_day=18.;env.configure(config);
        auto sunset=draw("sunset");
        config.cloud_coverage=.96;env.configure(config);
        auto overcast=draw("overcast");
        for(int i=0;i<5;++i)env.advance(1.);
        auto moved=draw("flow+5sec");
        if(gpu_uniform_test && capture) {
            // Translation of the actual camera must change world-space
            // clouds while the sun and weather uniforms are held constant.
            frame.camera_x=0.f;
            auto before_movement=draw("camera-translation-start");
            frame.camera_x=4500.f;
            auto after_movement=draw("camera-translation-world-4500m");
            auto const translated=changed(before_movement,after_movement);
            std::printf("GPU sky world_anchor changed_pixels=%llu\n",
                (unsigned long long)translated);
            require(translated>200u,
                    "Cloud density incorrectly follows the camera");
            frame.camera_x=0.f;
        }

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
        if(view_sweep) {
            require(gpu_uniform_test && capture,
                    "Camera sweep requires GPU sky and pixel readback");
            config.time_of_day=12.;
            config.clouds=false;
            config.cloud_coverage=0.;
            env.configure(config);
            constexpr float pi=3.14159265358979323846f;
            std::uint64_t min_covered=UINT64_MAX;
            for(int pitch_step=-2;pitch_step<=2;++pitch_step) {
                for(int yaw_step=0;yaw_step<8;++yaw_step) {
                    frame.camera_yaw=float(yaw_step)*(pi/4.f);
                    frame.camera_pitch=float(pitch_step)*.28f;
                    auto image=draw("camera-sweep");
                    std::uint64_t covered=0,ultra_dark=0;
                    std::uint64_t rgb_sum=0;
                    for(std::size_t j=0;j<image.size();j+=4) {
                        auto brightness=unsigned(image[j])+
                                        unsigned(image[j+1])+unsigned(image[j+2]);
                        rgb_sum+=brightness;
                        if(brightness>82) ++covered;
                        if(brightness<35) ++ultra_dark;
                    }
                    const auto total=image.size()/4;
                    min_covered=std::min(min_covered,covered);
                    if(covered<total*95/100) {
                        std::fprintf(stderr,
                            "Sky frustum gap yaw=%d pitch=%d pixels=%llu/%llu "
                            "ultra_dark=%llu mean_rgb=%.1f\n",
                            yaw_step,pitch_step,
                            (unsigned long long)covered,
                            (unsigned long long)total,
                            (unsigned long long)ultra_dark,
                            double(rgb_sum)/double(total));
                        throw std::runtime_error("Sky frustum coverage incomplete");
                    }
                }
            }
            std::printf("PASS: 40 camera orientations, minimum colored sky=%llu/100224\n",
                (unsigned long long)min_covered);
            frame.camera_yaw=frame.camera_pitch=0.f;
        }
        if(gpu_uniform_test) {
            // 60 consecutive production-style GPU frames: simulation advances
            // smoothly, but no new texture or sky geometry is submitted.
            for(int i=0;i<60;++i) {
                env.set_time(12.+6.*double(i)/59.);
                env.advance(1./60.);
                submit_uniform();
                require(gpu->draw(ctx,&frame)!=0,
                        "Continuous GPU Sky frame submission failed");
            }
            require(texture_uploads==1,
                    "Continuous Sky timeline unexpectedly uploaded a texture");
        }
        if(gpu_uniform_test) {
            require(texture_uploads==1,
                    "GPU Sky must keep one stable texture across all updates");
            std::printf("SKY_GPU_UNIFORMS texture_uploads=%u "
                        "uniform_updates=4\n",texture_uploads);
        }
        require(gpu->set_sky(ctx,nullptr)!=0,"Cannot clear GPU sky");
        require(gpu->draw(ctx,&frame)!=0,"Cannot draw after sky reset");
        if(capture){
            auto t=changed(noon,sunset),c=changed(sunset,overcast),
                m=changed(overcast,moved);
            auto noon_clouds=changed(noon,noon_overcast);
            std::printf("GPU sky changed_pixels: sun=%llu coverage=%llu "
                        "noon_clouds=%llu flow=%llu\n",
                        (unsigned long long)t,(unsigned long long)c,
                        (unsigned long long)noon_clouds,(unsigned long long)m);
            require(t>100,"Sun/time sky change absent on GPU");
            require(c>100,"Cloud coverage change absent on GPU");
            require(m>20,"Animated clouds change absent on GPU");
            std::printf("PASS: %s GPU real frame sky changed over time\n",mod->provider);
        }else {
            std::printf("PARTIAL: %s renders/reuploads sky, no GPU capture "
                        "extension to confirm pixel motion\n",mod->provider);
        }
        cleanup();return 0;
    }catch(std::exception const& e){
        std::fprintf(stderr,"FAIL sky GPU: %s\n",e.what());
        cleanup();return 1;
    }
}
