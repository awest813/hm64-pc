#pragma once
/**
 * hm64::input – gamepad/keyboard input backend for the ultramodern
 * controller callback.
 *
 * ultramodern polls this whenever it needs a controller state update.
 * We map keyboard and SDL2 gamepad events onto the N64 button layout.
 */

#include <cstdint>

// Match the N64 controller button bitmask layout used by libultra / NuSystem.
// These values come from <PR/controller.h> in libultra.
namespace hm64::input {

void init();
void deinit();

/**
 * Process pending SDL events and update internal controller state.
 * Called once per frame by the graphics backend before rendering.
 */
void poll();

/**
 * Fill in a controller data structure.
 * @param controller_index  0–3 (N64 supports 4 controllers)
 * @param out_buttons       bitmask of pressed N64 buttons (OSContPad.button)
 * @param out_stick_x       analog stick X, -128 .. 127
 * @param out_stick_y       analog stick Y, -128 .. 127
 */
void get_controller(int controller_index,
                    uint16_t& out_buttons,
                    int8_t&   out_stick_x,
                    int8_t&   out_stick_y);

// Returns true if the user requested application exit (window closed / ESC).
bool should_quit();

} // namespace hm64::input
