#include "platform/audio.h"

#include <SDL3/SDL.h>

#include <vector>

namespace ac04::platform {
namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;

// Pull callback: SDL asks for more data; the mixer does not exist yet, so it gets silence.
void SDLCALL FillSilence(void* /*userdata*/, SDL_AudioStream* stream, int additional_amount,
                         int /*total_amount*/) {
    if (additional_amount <= 0) return;
    static thread_local std::vector<unsigned char> zeros;
    if (zeros.size() < static_cast<size_t>(additional_amount)) zeros.assign(additional_amount, 0);
    SDL_PutAudioStreamData(stream, zeros.data(), additional_amount);
}

}  // namespace

AudioOutput::~AudioOutput() { Close(); }

bool AudioOutput::Open(std::string& error) {
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = kChannels;
    spec.freq = kSampleRate;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, FillSilence, this);
    if (!stream_) {
        error = std::string("cannot open the default audio device: ") + SDL_GetError();
        return false;
    }
    UpdateGain();
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void AudioOutput::Close() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

void AudioOutput::ApplySettings(const AudioSettings& audio) {
    settings_ = audio;
    UpdateGain();
}

void AudioOutput::OnFocusChanged(bool focused) {
    focused_ = focused;
    UpdateGain();
}

void AudioOutput::UpdateGain() {
    if (!stream_) return;
    // Focus loss never pauses anything: with "mute" only the output gain drops to zero.
    const bool mute = !focused_ && settings_.on_focus_loss == FocusLossAudio::Mute;
    SDL_SetAudioStreamGain(stream_, mute ? 0.0f : 1.0f);
}

std::string AudioOutput::DeviceName() const {
    if (!stream_) return "(none)";
    const char* name = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_));
    return name ? name : "(default device)";
}

}  // namespace ac04::platform
