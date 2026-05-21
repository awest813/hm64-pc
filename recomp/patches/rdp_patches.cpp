/**
 * rdp_patches.cpp – RDP (Reality Display Processor) integration.
 *
 * The N64's RDP processes display-list commands to produce rendered frames.
 * On PC, this is handled by ultramodern via the RT64 renderer (or a software
 * fallback).  This file contains any game-specific RDP patches needed to
 * account for differences between the N64 hardware and the PC renderer.
 *
 * For the initial HM64 integration, no game-specific RDP patches are required
 * because the stock RDP command set (F3DEX2 / Fast3D Extended) used by the
 * game is fully supported by RT64 / ultramodern.
 *
 * Add patches here as needed when testing reveals incompatibilities.
 */

#include "recomp.h"
#include "ultramodern/ultramodern.hpp"

// ---------------------------------------------------------------------------
// Placeholder: additional RDP patches go here
// ---------------------------------------------------------------------------
// Example (stubbing a game-specific display-list helper that needs changes
// for widescreen or PC rendering):
//
// RECOMP_PATCH void someRdpHelper(uint8_t* rdram, recomp_context* ctx) {
//     // PC-adjusted implementation ...
// }
