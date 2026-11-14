// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once

#include <memory>
#include <string>

#include <northstarcpp/audio_plugin.h>

namespace northstarcpp {

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();
    AudioOutput(AudioOutput const&) = delete;
    AudioOutput& operator=(AudioOutput const&) = delete;

    std::string provider_name() const;
    void play(NorthStarPcm16 const& pcm);
    void stop();

private:
    struct State;
    std::unique_ptr<State> state;
};

} // namespace northstarcpp
