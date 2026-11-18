#pragma once
#include "native_mixer.hpp"
#include <atomic>
#include <mutex>
#include <thread>
#include <array>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif
namespace northstarcpp::native_audio {
class Device {
public:
    Device(Mixer&, std::mutex&);
    ~Device();
    Device(Device const&) = delete;
    Device& operator=(Device const&) = delete;
    bool available() const noexcept { return available_; }
private:
    void run();
    Mixer& mixer_;
    std::mutex& mutex_;
    std::atomic<bool> running_{false};
    bool available_ = false;
    std::thread worker_;
#ifdef _WIN32
    // 12 x 256 frames = 64 ms of stable ring-buffered audio. The completion
    // event, rather than coarse Windows Sleep(2), drives every refill.
    static constexpr std::size_t count = 12;
    HWAVEOUT wave_ = nullptr;
    HANDLE completion_event_ = nullptr;
    std::array<WAVEHDR,count> headers_{};
    std::array<std::array<std::int16_t,block_frames*2>,count> pcm_{};
    std::array<bool,count> prepared_{};
    std::array<bool,count> queued_{};
#endif
};
} // namespace northstarcpp::native_audio
