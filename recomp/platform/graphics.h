#pragma once
/**
 * hm64::graphics – graphics backend interface for the ultramodern renderer
 * callback.
 *
 * The renderer callback glues ultramodern's RDP command processing to a
 * window-system backend.  Here we use SDL2 for window management and rely
 * on ultramodern/RT64 for the actual RDP emulation.
 *
 * For the initial testing phase (menu rendering), this backend opens a
 * 320×240 window, which matches the N64's standard resolution.
 */

#include <cstdint>

namespace hm64::graphics {

// Called once before ultramodern::start()
void init();

// Called once after ultramodern::start() returns
void deinit();

// ultramodern renderer callbacks
void* create_renderer(void* window_handle);
void  destroy_renderer(void* renderer);
void  render_frame(void* renderer,
                   const void* rdp_commands,
                   uint32_t    rdp_command_count);

} // namespace hm64::graphics
