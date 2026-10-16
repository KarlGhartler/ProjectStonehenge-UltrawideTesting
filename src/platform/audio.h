// Audio output (ADR-004/ADR-005): SDL3 stream on the system default device, 48 kHz stereo s16.
// Plays silence until the SOUND/VAG services and the mixer exist.
#pragma once

#include <string>

#include "platform/settings.h"

struct SDL_AudioStream;

namespace ac04::platform {

namespace audio {
class SpuMixer;
}

class AudioOutput {
public:
    ~AudioOutput();

    bool Open(std::string& error);
    void Close();

    void ApplySettings(const AudioSettings& audio);
    void OnFocusChanged(bool focused);

    std::string DeviceName() const;

    // Samples come from the mixer's ring (silence without one).
    void SetSource(audio::SpuMixer* mixer) { source_ = mixer; }

private:
    void UpdateGain();

    SDL_AudioStream* stream_ = nullptr;
    AudioSettings settings_;
    bool focused_ = true;
    audio::SpuMixer* source_ = nullptr;

    friend void FillFromSource(void*, SDL_AudioStream*, int, int);
};

}  // namespace ac04::platform
