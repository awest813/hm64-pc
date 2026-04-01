/**
 * hm64::audio – SDL2 audio backend implementation.
 */

#include "audio.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>

namespace hm64::audio {

static SDL_AudioDeviceID s_dev = 0;
static int s_sample_rate = 32000;

bool init() {
    if (s_dev != 0) {
        return true;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[hm64::audio] SDL_InitSubSystem(AUDIO) failed: %s\n",
                SDL_GetError());
        return false;
    }

    SDL_AudioSpec desired{};
    desired.freq     = s_sample_rate;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples  = 512;
    desired.callback = nullptr;

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
    if (s_dev == 0) {
        return;
    }
    SDL_CloseAudioDevice(s_dev);
    s_dev = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void queue_samples(int16_t* samples, std::size_t num_samples) {
    if (s_dev == 0 || samples == nullptr || num_samples == 0) {
        return;
    }
    static constexpr Uint32 MAX_QUEUED_BYTES = 32000 * 2 * sizeof(int16_t) / 4;
    if (SDL_GetQueuedAudioSize(s_dev) > MAX_QUEUED_BYTES) {
        return;
    }
    SDL_QueueAudio(s_dev, samples,
                   static_cast<Uint32>(num_samples * 2 * sizeof(int16_t)));
}

std::size_t get_frames_remaining() {
    if (s_dev == 0) return 0;
    return SDL_GetQueuedAudioSize(s_dev) / (2 * sizeof(int16_t));
}

void set_frequency(uint32_t freq) {
    s_sample_rate = freq;
}

}
