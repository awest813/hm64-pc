/**
 * hm64::graphics – SDL2 window backend.
 *
 * Window management only.  The actual RDP/RSP emulation is handled by
 * ultramodern + RT64 (or the software RDP fallback in the N64ModernRuntime).
 *
 * This translation unit is intentionally thin: it creates the SDL2 window,
 * passes its native handle to create_renderer() so ultramodern/RT64 can set up
 * its own rendering context, and forwards per-frame callbacks.
 */

#include "graphics.h"
#include "input.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>

// N64 native resolution
static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 240;
static constexpr int WINDOW_SCALE = 2; // default 2× upscale → 640×480

// Declared in nusys_patches.cpp; called once per vsync to drive the game's
// retrace mechanism (gfxRetraceCallback → drawFrame + stepMainLoop advance).
extern void hm64_invoke_retrace_callback();

namespace hm64::graphics {

static SDL_Window* s_window      = nullptr;
static bool        s_fullscreen  = false;

void init() {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[hm64::graphics] SDL_InitSubSystem(VIDEO) failed: %s\n",
                SDL_GetError());
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    s_window = SDL_CreateWindow(
        "Harvest Moon 64 - PC (n64recomp)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W * WINDOW_SCALE,
        SCREEN_H * WINDOW_SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!s_window) {
        fprintf(stderr, "[hm64::graphics] SDL_CreateWindow failed: %s\n",
                SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SDL_Quit();
        exit(EXIT_FAILURE);
    }
}

void toggle_fullscreen() {
    if (!s_window) {
        return;
    }
    s_fullscreen = !s_fullscreen;
    SDL_SetWindowFullscreen(s_window,
        s_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void deinit() {
    if (s_window) {
        SDL_DestroyWindow(s_window);
        s_window = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void* create_renderer(void* /*window_handle*/) {
    // Return the SDL window so ultramodern/RT64 can query its native handle.
    return s_window;
}

void destroy_renderer(void* /*renderer*/) {
    // RT64 owns its own resources; nothing to do here.
}

void render_frame(void* /*renderer*/,
                  const void* /*rdp_commands*/,
                  uint32_t    /*rdp_command_count*/) {
    // Poll input first so that keyboard/gamepad state is up-to-date before
    // the retrace callback reads controller data via nuContDataGetAll.
    hm64::input::poll();

    // Drive the game's vsync/retrace mechanism.  On real N64 hardware,
    // NuSystem's graphics scheduler thread called gfxRetraceCallback each
    // vsync; here the render thread takes that role.  This unblocks the game
    // thread's busy-wait in mainLoop() by setting stepMainLoop = TRUE.
    hm64_invoke_retrace_callback();

    // If the user closed the window or pressed Escape, inject a single
    // SDL_QUIT so ultramodern's own event loop also sees the request and
    // can shut down cleanly.  We guard with a flag so the event is queued
    // at most once (avoiding flooding the event queue every frame).
    static bool s_quit_pushed = false;
    if (hm64::input::should_quit() && !s_quit_pushed) {
        SDL_Event quit_event{};
        quit_event.type = SDL_QUIT;
        SDL_PushEvent(&quit_event);
        s_quit_pushed = true;
    }

    // RT64 handles the actual buffer swap via the SDL window handle.
    // If using the software fallback, update/present would happen here.
}

} // namespace hm64::graphics
