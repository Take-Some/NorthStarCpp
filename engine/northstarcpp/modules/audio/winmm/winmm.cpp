// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/audio_plugin.h>
#include "module_provider.hpp"

#include <windows.h>
#include <mmsystem.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace northstarcpp::module_provider;

void check(MMRESULT code, char const* action)
{
    if (code != MMSYSERR_NOERROR)
        throw std::runtime_error(std::string(action) + " (WinMM " + std::to_string(code) + ")");
}

struct Output {
    ~Output() { stop(); }

    void stop() noexcept
    {
        if (!device)
            return;
        waveOutReset(device);
        if (prepared)
            waveOutUnprepareHeader(device, &header, sizeof(header));
        waveOutClose(device);
        device = nullptr;
        prepared = false;
        header = {};
    }

    void play(NorthStarPcm16 const& pcm)
    {
        if (!pcm.samples || !pcm.frames || pcm.sample_rate < 8000 || pcm.sample_rate > 192000 ||
            (pcm.channels != 1 && pcm.channels != 2) || pcm.frames > pcm.sample_rate * 60u)
            throw std::runtime_error("Expected mono or stereo PCM16, 8-192 kHz, up to 60 seconds");

        stop();
        samples.assign(pcm.samples, pcm.samples + pcm.frames * pcm.channels);
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = static_cast<WORD>(pcm.channels);
        format.nSamplesPerSec = pcm.sample_rate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(pcm.channels * sizeof(int16_t));
        format.nAvgBytesPerSec = pcm.sample_rate * format.nBlockAlign;
        check(waveOutOpen(&device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL), "Open audio device");

        try {
            header.lpData = reinterpret_cast<LPSTR>(samples.data());
            header.dwBufferLength = static_cast<DWORD>(samples.size() * sizeof(int16_t));
            check(waveOutPrepareHeader(device, &header, sizeof(header)), "Prepare audio buffer");
            prepared = true;
            check(waveOutWrite(device, &header, sizeof(header)), "Submit audio buffer");
        } catch (...) {
            stop();
            throw;
        }
    }

    HWAVEOUT device = nullptr;
    WAVEHDR header{};
    bool prepared = false;
    std::vector<int16_t> samples;
};

void* create()
{
    Output* result = nullptr;
    guarded([&] { result = new Output; });
    return result;
}

void destroy(void* output) { delete static_cast<Output*>(output); }

int play(void* output, NorthStarPcm16 const* pcm)
{
    return guarded([&] {
        if (!output || !pcm)
            throw std::runtime_error("Missing audio context or PCM data");
        static_cast<Output*>(output)->play(*pcm);
    });
}

void stop(void* output)
{
    if (output)
        static_cast<Output*>(output)->stop();
}

const NorthStarAudioApi api{create, destroy, play, stop};
const NorthStarModule module{sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_AUDIO, "WinMM", "1.0.0", NORTHSTARCPP_AUDIO_ABI,
    sizeof(api), &api, last_error};

} // namespace

extern "C" const NorthStarModule* northstar_module_query(uint32_t host,
    char const* capability, uint32_t version)
{
    return match(module, host, capability, version);
}
