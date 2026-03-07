/**
 * nusys_patches.cpp – PC replacements for NuSystem API stubs.
 *
 * The N64Recomp configuration (hm64.us.toml) stubs out all nuGfx* and
 * nuCont* functions so the runtime can intercept them.  This file provides
 * the implementations that bridge between the recompiled game code and the
 * ultramodern runtime.
 *
 * The macro signatures here follow the N64ModernRuntime librecomp convention:
 *   void RECOMP_FUNC(uint8_t* rdram, recomp_context* ctx);
 * where arguments/return values are accessed through the ctx register file.
 */

#include "librecomp/recomp.h"
#include "ultramodern/ultramodern.hpp"

// ---------------------------------------------------------------------------
// nuGfxInit  – replaced by ultramodern graphics initialisation
// ---------------------------------------------------------------------------
RECOMP_PATCH void nuGfxInit(uint8_t* rdram, recomp_context* ctx) {
    // ultramodern initialises graphics when the renderer callback fires;
    // nothing to do here at startup.
}

// ---------------------------------------------------------------------------
// nuGfxTaskStart – submit an RSP/RDP display list
// ---------------------------------------------------------------------------
RECOMP_PATCH void nuGfxTaskStart(uint8_t* rdram, recomp_context* ctx) {
    // a0 = gfxList pointer (N64 virtual address)
    // a1 = list size in bytes
    // a2 = ucode type (NU_GFX_UCODE_F3DEX etc.)
    // a3 = swap-buffer flag
    gpr gfx_list_ptr  = ctx->r4;
    gpr list_size     = ctx->r5;
    gpr ucode_type    = ctx->r6;
    gpr swap_flag     = ctx->r7;

    ultramodern::submit_rsp_task(rdram, gfx_list_ptr, list_size,
                                  ucode_type, swap_flag);
}

// ---------------------------------------------------------------------------
// nuGfxTaskAllEndWait – wait for all pending RSP/RDP tasks to finish
// ---------------------------------------------------------------------------
RECOMP_PATCH void nuGfxTaskAllEndWait(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::wait_for_rsp_done();
}

// ---------------------------------------------------------------------------
// nuGfxSwapCfbFuncSet – register the framebuffer-swap callback
// ---------------------------------------------------------------------------
RECOMP_PATCH void nuGfxSwapCfbFuncSet(uint8_t* rdram, recomp_context* ctx) {
    // a0 = function pointer (or NULL to clear)
    // ultramodern handles the swap internally; this is a no-op on PC.
}

// ---------------------------------------------------------------------------
// nuGfxPreNMIFuncSet – register the pre-NMI callback
// ---------------------------------------------------------------------------
RECOMP_PATCH void nuGfxPreNMIFuncSet(uint8_t* rdram, recomp_context* ctx) {
    // No NMI on PC; ignore.
}

// ---------------------------------------------------------------------------
// nuGfxFuncSet – register the retrace (vsync) callback
// ---------------------------------------------------------------------------
RECOMP_PATCH void nuGfxFuncSet(uint8_t* rdram, recomp_context* ctx) {
    // ultramodern drives the game loop at the configured frame rate;
    // the retrace callback is invoked by ultramodern automatically.
}

// ---------------------------------------------------------------------------
// nuGfxDisplayOn / nuGfxDisplayOff
// ---------------------------------------------------------------------------
RECOMP_PATCH void nuGfxDisplayOn(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::set_display_enabled(true);
}

RECOMP_PATCH void nuGfxDisplayOff(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::set_display_enabled(false);
}

// ---------------------------------------------------------------------------
// nuContInit / nuContDataGetAll – controller handling
// ---------------------------------------------------------------------------
RECOMP_PATCH void nuContInit(uint8_t* rdram, recomp_context* ctx) {
    // ultramodern provides controller data; nothing to initialise here.
}

RECOMP_PATCH void nuContDataGetAll(uint8_t* rdram, recomp_context* ctx) {
    // Called each frame to refresh the nuContData[] array.
    // ultramodern populates this through the input callback.
    ultramodern::update_input(rdram);
}
