/**
 * rsp_audio_patches.cpp – RSP audio stubs.
 *
 * Stubs out the NuSystem audio/music library initialization so the game
 * doesn't crash trying to call into the libmus (MusBankInitialize, etc.)
 * or the N64 RSP audio ucode.
 */

#include "librecomp/recomp.h"
#include <cstdio>

extern "C" {

// nuAuStlInit – NuSystem audio library init (calls MusBankInitialize → crash)
RECOMP_PATCH void nuAuStlInit(uint8_t* rdram, recomp_context* ctx) {
    printf("[audio] nuAuStlInit stubbed – skipping music library init\n");
    fflush(stdout);
}

// nuAuStlMgrInit – internal NuSystem audio manager init
RECOMP_PATCH void nuAuStlMgrInit(uint8_t* rdram, recomp_context* ctx) {
}

// nuAuStlPlayerInit – libmus player init
RECOMP_PATCH void nuAuStlPlayerInit(uint8_t* rdram, recomp_context* ctx) {
}

// nuAuPreNMIFuncSet – NMI handler for audio
RECOMP_PATCH void nuAuPreNMIFuncSet(uint8_t* rdram, recomp_context* ctx) {
}

// MusBankInitialize – libmus bank init (crashes on NULL ptrs without real audio)
RECOMP_PATCH void MusBankInitialize(uint8_t* rdram, recomp_context* ctx) {
}

// MusBankSet – set current sound bank
RECOMP_PATCH void MusBankSet(uint8_t* rdram, recomp_context* ctx) {
}

// InitializeWaveTable – initialise wavetable audio
RECOMP_PATCH void initializeWaveTable(uint8_t* rdram, recomp_context* ctx) {
    printf("[audio] initializeWaveTable stubbed\n");
    fflush(stdout);
}

} // extern "C"
