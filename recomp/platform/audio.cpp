/**
 * hm64::audio – SDL2 audio backend implementation.
 *
 * Uses a simple ring-buffer approach: the recompiled game's RSP audio ucode
 * (handled by ultramodern) delivers finished sample buffers via queue_samples().
 * Those buffers are fed into SDL's audio queue for playback.
 *
 * Requires: SDL2 (linked via the CMake target SDL2::SDL2).
 */

#include "audio.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>

namespace hm64::audio {

static SDL_AudioDeviceID s_dev = 0;
// HM64 calls osAiSetFrequency(32000) during audio initialisation, so the
// SDL2 device is opened at that rate.  N64 hardware supports up to 48 kHz,
// but matching the game's own frequency avoids resampling artefacts.
static constexpr int SAMPLE_RATE = 32000;

bool init() {
    // Guard against double-initialisation: ultramodern may call the init
    // callback it was given AND main() calls init() explicitly before
    // ultramodern::start().  The second call must be a no-op.
    if (s_dev != 0) {
        return true;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[hm64::audio] SDL_InitSubSystem(AUDIO) failed: %s\n",
                SDL_GetError());
        return false;
    }

    SDL_AudioSpec desired{};
    desired.freq     = SAMPLE_RATE;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples  = 512;   // buffer size in samples
    desired.callback = nullptr; // push mode

    SDL_AudioSpec obtained{};
    s_dev = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (s_dev == 0) {
        fprintf(stderr, "[hm64::audio] SDL_OpenAudioDevice failed: %s\n",
                SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    SDL_PauseAudioDevice(s_dev, 0);
    return true;
}

void deinit() {
    // Guard against double-deinit for the same reason as init(): if ultramodern
    // calls our deinit callback and main() also calls it during cleanup, the
    // second call must be a no-op so we don't close an already-closed device or
    // call SDL_QuitSubSystem one too many times.
    if (s_dev == 0) {
        return;
    }
    SDL_CloseAudioDevice(s_dev);
    s_dev = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void queue_samples(const int16_t* samples, int num_samples) {
    if (s_dev == 0 || samples == nullptr || num_samples <= 0) {
        return;
    }
    // Prevent latency build-up: if the audio queue is already well ahead of
    // real-time playback, drop this buffer so the queue can drain.  Without
    // this guard, audio can drift seconds behind the visuals after extended
    // play sessions or when the host system is briefly under load.
    //
    // Threshold: SAMPLE_RATE * 2 channels * sizeof(int16_t) / 4
    //   = 32000 Hz * 2 * 2 bytes / 4 = 32000 bytes ≈ 250 ms at 32 kHz stereo.
    // Allows a comfortable cushion against short glitches without letting
    // the queue grow unboundedly.  (SAMPLE_RATE is defined above as 32000.)
    static constexpr Uint32 MAX_QUEUED_BYTES =
        static_cast<Uint32>(SAMPLE_RATE) * 2u * sizeof(int16_t) / 4u;
    if (SDL_GetQueuedAudioSize(s_dev) > MAX_QUEUED_BYTES) {
        return;
    }
    // num_samples is stereo pairs; each pair is 2×int16_t = 4 bytes
    SDL_QueueAudio(s_dev, samples,
                   static_cast<Uint32>(num_samples * 2 * sizeof(int16_t)));
}

} // namespace hm64::audio
