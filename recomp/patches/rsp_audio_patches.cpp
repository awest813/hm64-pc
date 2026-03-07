/**
 * rsp_audio_patches.cpp – RSP audio (Reality Signal Processor) integration.
 *
 * HM64 uses the n_aspMain RSP microcode for audio mixing.  On N64 hardware,
 * the CPU writes an audio task display-list to the RSP which mixes sample
 * data into the AI (Audio Interface) DMA buffer.
 *
 * On PC, ultramodern intercepts RSP task submission (via nuGfxTaskStart with
 * NU_GFX_UCODE_ASPMAN) and routes audio tasks through its own mixer.  The
 * mixed PCM output is forwarded to the platform audio backend (SDL2) via the
 * queue_samples() callback registered in platform/main.cpp.
 *
 * This file provides any additional game-specific audio patches needed.
 * Currently none are required for the initial integration.
 */

#include "librecomp/recomp.h"
#include "ultramodern/ultramodern.hpp"

// ---------------------------------------------------------------------------
// Placeholder: audio RSP patches go here
// ---------------------------------------------------------------------------
// Example – if the game calls a custom audio helper that needs to map to a
// PC-native SDL operation:
//
// RECOMP_PATCH void customAudioHelper(uint8_t* rdram, recomp_context* ctx) {
//     // ...
// }
