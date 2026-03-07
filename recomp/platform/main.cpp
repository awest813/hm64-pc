/**
 * hm64_pc - PC port entry point
 *
 * This file provides the host-platform main() that initialises the ultramodern
 * runtime, registers platform callbacks, and launches the recompiled game.
 *
 * The runtime (N64ModernRuntime / ultramodern) replaces libultra's threading,
 * VI, AI, and PI layers.  The recompiled game code calls through to it via the
 * stubs listed in hm64.us.toml, and these stubs are provided by librecomp.
 */

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

// N64ModernRuntime headers
#include "ultramodern/ultramodern.hpp"
#include "librecomp/recomp.h"

#include "audio.h"
#include "graphics.h"
#include "input.h"

// ---------------------------------------------------------------------------
// Forward declarations from the recompiled game
// ---------------------------------------------------------------------------
extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);

// ---------------------------------------------------------------------------
// Ultramodern platform-specific callbacks
// ---------------------------------------------------------------------------

static ultramodern::renderer::callbacks_t make_renderer_callbacks() {
    ultramodern::renderer::callbacks_t cb{};
    cb.create_renderer  = hm64::graphics::create_renderer;
    cb.destroy_renderer = hm64::graphics::destroy_renderer;
    cb.render_frame     = hm64::graphics::render_frame;
    return cb;
}

static ultramodern::audio_callbacks_t make_audio_callbacks() {
    ultramodern::audio_callbacks_t cb{};
    cb.init         = hm64::audio::init;
    cb.deinit       = hm64::audio::deinit;
    cb.queue_samples = hm64::audio::queue_samples;
    return cb;
}

static ultramodern::input::callbacks_t make_input_callbacks() {
    ultramodern::input::callbacks_t cb{};
    cb.poll          = hm64::input::poll;
    cb.get_controller = hm64::input::get_controller;
    return cb;
}

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {

    // Path to the original ROM image (needed by librecomp for DMA reads)
    std::string rom_path = "baserom.us.z64";
    if (argc >= 2) {
        rom_path = argv[1];
    }

    // Initialise the recomp layer with the ROM
    if (!recomp::init(rom_path.c_str())) {
        fprintf(stderr, "[hm64_pc] Failed to open ROM: %s\n", rom_path.c_str());
        fprintf(stderr, "  The ROM must be the US version: baserom.us.z64\n");
        fprintf(stderr, "  Place it in the current directory or pass its path as an argument.\n");
        fprintf(stderr, "Usage: %s [path/to/baserom.us.z64]\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("[hm64_pc] ROM: %s\n", rom_path.c_str());

    // Print the absolute save-file path so the user knows where to find it,
    // regardless of the working directory they launched from.
    {
        std::error_code ec;
        auto save_abs = std::filesystem::absolute("hm64.sav", ec);
        if (!ec) {
            printf("[hm64_pc] Save file: %s\n", save_abs.string().c_str());
        } else {
            printf("[hm64_pc] Save file: hm64.sav (current directory)\n");
        }
    }

    printf("[hm64_pc] --- Controls (keyboard defaults) ---\n");
    printf("[hm64_pc]   Enter=Start  Z key=Z  X=B  C=A  Shift=R  Q=L\n");
    printf("[hm64_pc]   Arrow keys=D-pad  WASD=Analog  IJKL=C-buttons\n");
    printf("[hm64_pc]   F11=fullscreen  Esc=quit\n");
    printf("[hm64_pc] -----------------------------------------\n");

    // Register platform callbacks with ultramodern
    ultramodern::renderer::set_callbacks(make_renderer_callbacks());
    ultramodern::audio::set_callbacks(make_audio_callbacks());
    ultramodern::input::set_callbacks(make_input_callbacks());

    // Initialise platform backends
    hm64::graphics::init();
    if (!hm64::audio::init()) {
        fprintf(stderr, "[hm64_pc] Warning: audio initialisation failed, continuing without audio\n");
    }
    hm64::input::init();

    // Start the ultramodern event loop – this calls recomp_entrypoint() in a
    // dedicated game thread and returns when the window is closed.
    ultramodern::start(recomp_entrypoint);

    // Cleanup
    hm64::input::deinit();
    hm64::audio::deinit();
    hm64::graphics::deinit();

    return EXIT_SUCCESS;
}
