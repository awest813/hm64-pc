/**
 * title_patches.cpp – PC patches for the title/menu screen functions.
 *
 * This file is the primary test bed for the initial HM64 recomp integration.
 * The title screen (src/game/title.c) was chosen as the first target because:
 *   • It has minimal gameplay logic and no save-file dependencies.
 *   • It exercises the sprite DMA path, sprite rendering, and animation.
 *   • It is easy to verify visually (the Harvest Moon 64 logo + "Press Start").
 *
 * Current patches in this file:
 *   1. Removing the JP nuPakMenu call from the startup path (not needed on PC).
 *   2. Stripping the NMI/VI setup from mainproc so it runs cleanly without
 *      real N64 hardware.
 *
 * As testing progresses, add more patches here or move them to a separate
 * file per system area.
 */

#include "librecomp/recomp.h"
#include "ultramodern/ultramodern.hpp"

// ---------------------------------------------------------------------------
// mainproc – strip hardware-specific initialisation
// ---------------------------------------------------------------------------
// The original mainproc() starts with a VI mode setup block that calls
// osTvType, osViSetMode, and osViSetSpecialFeatures.  On PC these are stubs,
// but the code also contains a `while(TRUE)` hang for unsupported TV types.
// We replace the entire function with a clean PC version that skips the VI
// setup and goes straight to initializeEngine() / setupGameStart().
//
// Note: if you later want to keep the original mainproc and just stub the VI
// calls, remove this patch and add `osViSetMode`, `osViSetSpecialFeatures`,
// and `nuGfxDisplayOff` to the [[patches.stub]] list in hm64.us.toml.

extern "C" {
    // Declarations for recompiled game functions that mainproc calls.
    void recomp_initializeEngine(uint8_t* rdram, recomp_context* ctx);
    void recomp_setupGameStart(uint8_t* rdram, recomp_context* ctx);
    void recomp_mainLoop(uint8_t* rdram, recomp_context* ctx);
}

RECOMP_PATCH void mainproc(uint8_t* rdram, recomp_context* ctx) {
    // Skip N64 VI / TV-type setup entirely.
    // Jump straight into the game engine initialisation.
    recomp_initializeEngine(rdram, ctx);
    recomp_setupGameStart(rdram, ctx);
    recomp_mainLoop(rdram, ctx);
}

// ---------------------------------------------------------------------------
// initializeTitleScreen – integration smoke test
// ---------------------------------------------------------------------------
// This is intentionally NOT patched here; we let the recompiled version run
// as-is so we can verify that the sprite DMA and rendering pipeline work
// end-to-end.  Any issues observed when the title screen fails to render
// should be fixed by adding targeted patches below.
//
// Example: if dmaSprite() crashes due to a ROM DMA issue, add a patch here.
