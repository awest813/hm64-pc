#pragma once

#include <cstdint>

namespace hm64::audio {

bool init();
void deinit();
void queue_samples(int16_t* samples, std::size_t num_samples);
std::size_t get_frames_remaining();
void set_frequency(uint32_t freq);

}
