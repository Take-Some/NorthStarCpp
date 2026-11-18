#include "native_device.hpp"
#include <chrono>

namespace northstarcpp::native_audio {
Device::Device(Mixer& mixer, std::mutex& mutex) : mixer_(mixer), mutex_(mutex) {
#ifdef _WIN32
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = sample_rate*4;
    // The old Sleep(2) loop starved 5.3ms buffers whenever Windows extended
    // a timer sleep to its default ~15.6ms scheduler quantum.
    completion_event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!completion_event_)return;
    if(waveOutOpen(&wave_,WAVE_MAPPER,&format,
                   reinterpret_cast<DWORD_PTR>(completion_event_),
                   0,CALLBACK_EVENT)!=MMSYSERR_NOERROR) {
        wave_=nullptr;
        CloseHandle(completion_event_);
        completion_event_=nullptr;
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        auto& header = headers_[i];
        header.lpData = reinterpret_cast<LPSTR>(pcm_[i].data());
        header.dwBufferLength = static_cast<DWORD>(pcm_[i].size()*sizeof(std::int16_t));
        if (waveOutPrepareHeader(wave_, &header, sizeof(header)) != MMSYSERR_NOERROR) {
            for (std::size_t j=0;j<i;++j)
                waveOutUnprepareHeader(wave_,&headers_[j],sizeof(WAVEHDR));
            waveOutClose(wave_);
            wave_=nullptr;
            CloseHandle(completion_event_);
            completion_event_=nullptr;
            return;
        }
        prepared_[i] = true;
    }
    available_ = true;
    running_.store(true);
    try {
        worker_ = std::thread(&Device::run,this);
    } catch (...) {
        running_.store(false);
        waveOutReset(wave_);
        for (std::size_t i=0;i<count;++i)
            if (prepared_[i]) waveOutUnprepareHeader(wave_,&headers_[i],sizeof(WAVEHDR));
        waveOutClose(wave_);
        wave_=nullptr;
        CloseHandle(completion_event_);
        completion_event_=nullptr;
        available_=false;
        throw;
    }
#endif
}
Device::~Device() {
    running_.store(false);
#ifdef _WIN32
    if(completion_event_)SetEvent(completion_event_);
#endif
    if (worker_.joinable()) worker_.join();
#ifdef _WIN32
    if (wave_) {
        waveOutReset(wave_);
        for (std::size_t i=0;i<count;++i)
            if (prepared_[i]) waveOutUnprepareHeader(wave_,&headers_[i],sizeof(WAVEHDR));
        waveOutClose(wave_);
    }
    if(completion_event_)CloseHandle(completion_event_);
#endif
}
void Device::run() {
#ifdef _WIN32
    // Protect audio deadlines without starving render/game threads.
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
    while (running_.load()) {
        bool submitted = false;
        for (std::size_t i=0;i<count && running_.load();++i) {
            if (queued_[i] && !(headers_[i].dwFlags & WHDR_DONE)) continue;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                mixer_.mix(pcm_[i].data(),block_frames);
            }
            headers_[i].dwFlags &= ~WHDR_DONE;
            auto const code = waveOutWrite(wave_,&headers_[i],sizeof(WAVEHDR));
            if (code != MMSYSERR_NOERROR) {
                running_.store(false);
                break;
            }
            queued_[i] = true;
            submitted = true;
        }
        if(!submitted && running_.load())
            // Wake when the device finishes a buffer; no coarse polling.
            WaitForSingleObject(completion_event_,10);
    }
#endif
}
} // namespace northstarcpp::native_audio
