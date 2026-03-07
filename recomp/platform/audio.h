#pragma once
/**
 * hm64::audio – minimal SDL2 audio backend for the ultramodern audio callback.
 *
 * ultramodern generates 16-bit stereo PCM at the rate set by the game
 * (osAiSetFrequency).  We open an SDL audio device at that rate and stream
 * sample buffers provided by the runtime into it.
 */

#include <cstdint>

namespace hm64::audio {

/**
 * Open the host audio device.  Called once before ultramodern::start().
 * Returns true on success.
 */
bool init();

/**
 * Close the host audio device.  Called once after ultramodern::start()
 * returns.
 */
void deinit();

/**
 * Queue a block of 16-bit stereo PCM samples for playback.
 * @param samples   Pointer to interleaved [L, R, L, R, …] int16_t data.
 * @param num_samples  Number of stereo sample pairs (i.e. buffer length / 2).
 */
void queue_samples(const int16_t* samples, int num_samples);

} // namespace hm64::audio
