// Audio output (ADR-004/ADR-005): SDL3 stream on the system default device, 48 kHz stereo s16.
// Plays silence until the SOUND/VAG services and the mixer exist.
#pragma once

#include <string>

#include "platform/settings.h"

struct SDL_AudioStream;

namespace ac04::platform {

class AudioOutput {
public:
    ~AudioOutput();

    bool Open(std::string& error);
    void Close();

    void ApplySettings(const AudioSettings& audio);
    void OnFocusChanged(bool focused);

    std::string DeviceName() const;

private:
    void UpdateGain();

    SDL_AudioStream* stream_ = nullptr;
    AudioSettings settings_;
    bool focused_ = true;
};

}  // namespace ac04::platform
