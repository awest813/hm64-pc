/**
 * hm64::graphics – SDL2 window backend with RT64 GPU renderer.
 *
 * Implements a RendererContext that drives the RT64 HLE graphics pipeline
 * (Vulkan-based, SDL2 window).  Display lists from the game are passed
 * through RT64::Application::processDisplayLists() and the result is
 * presented via RT64::Application::updateScreen().
 */

#include "graphics.h"
#include "input.h"

#include <ultramodern/ultramodern.hpp>
#include <ultramodern/renderer_context.hpp>
#include <ultramodern/config.hpp>
#include <ultramodern/ultra64.h>

// RT64 – HLSL_CPU must be defined before any RT64 headers so that the
// interop namespace types (float4, RSPViewport, etc.) resolve correctly.
#ifndef HLSL_CPU
#define HLSL_CPU
#endif
#include "hle/rt64_application.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <atomic>

static constexpr int SCREEN_W   = 320;
static constexpr int SCREEN_H   = 240;
static constexpr int WINDOW_SCALE = 2;

extern void hm64_invoke_retrace_callback();
extern void hm64_vi_count_increment();

namespace hm64::graphics {

static SDL_Window* s_window  = nullptr;
static bool s_fullscreen     = false;

/* -------------------------------------------------------------------------
 * RT64-backed RendererContext
 * ---------------------------------------------------------------------- */

class RT64RendererContext : public ultramodern::renderer::RendererContext {
public:
    RT64::Application* app    = nullptr;
    uint8_t*           rdram  = nullptr;
    SDL_Window*        window = nullptr;

    // VI register storage – filled from ultramodern::renderer::get_vi_regs()
    // each update_screen() call so RT64::Application::Core can read them.
    uint32_t vi_status   = 0;
    uint32_t vi_origin   = 0;
    uint32_t vi_width    = 0;
    uint32_t vi_intr     = 0;
    uint32_t vi_vcurrent = 0;
    uint32_t vi_timing   = 0;
    uint32_t vi_vsync    = 0;
    uint32_t vi_hsync    = 0;
    uint32_t vi_leap     = 0;
    uint32_t vi_hstart   = 0;
    uint32_t vi_vstart   = 0;
    uint32_t vi_vburst   = 0;
    uint32_t vi_xscale   = 0;
    uint32_t vi_yscale   = 0;
    uint32_t last_present_fb = 0;

    static uint32_t s_zero; // dummy register target for unused DPC regs

    explicit RT64RendererContext(uint8_t* rdram_, SDL_Window* win)
        : rdram(rdram_), window(win)
    {
        RT64::Application::Core core{};
        // RenderWindow == SDL_Window* when PLUME_SDL_VULKAN_ENABLED is set
        core.window          = win;
        core.RDRAM           = rdram_;
        core.HEADER          = rdram_ + 0x1000;
        core.DMEM            = rdram_;
        core.IMEM            = rdram_;

        core.VI_STATUS_REG          = &vi_status;
        core.VI_ORIGIN_REG          = &vi_origin;
        core.VI_WIDTH_REG           = &vi_width;
        core.VI_INTR_REG            = &vi_intr;
        core.VI_V_CURRENT_LINE_REG  = &vi_vcurrent;
        core.VI_TIMING_REG          = &vi_timing;
        core.VI_V_SYNC_REG          = &vi_vsync;
        core.VI_H_SYNC_REG          = &vi_hsync;
        core.VI_LEAP_REG            = &vi_leap;
        core.VI_H_START_REG         = &vi_hstart;
        core.VI_V_START_REG         = &vi_vstart;
        core.VI_V_BURST_REG         = &vi_vburst;
        core.VI_X_SCALE_REG         = &vi_xscale;
        core.VI_Y_SCALE_REG         = &vi_yscale;

        core.MI_INTR_REG      = &s_zero;
        core.DPC_START_REG    = &s_zero;
        core.DPC_END_REG      = &s_zero;
        core.DPC_CURRENT_REG  = &s_zero;
        core.DPC_STATUS_REG   = &s_zero;
        core.DPC_CLOCK_REG    = &s_zero;
        core.DPC_BUFBUSY_REG  = &s_zero;
        core.DPC_PIPEBUSY_REG = &s_zero;
        core.DPC_TMEM_REG     = &s_zero;
        core.checkInterrupts  = [](){};

        RT64::ApplicationConfiguration appCfg{};
        appCfg.appId                = "hm64";
        appCfg.detectDataPath       = true;
        appCfg.useConfigurationFile = false;

        app = new RT64::Application(core, appCfg);

        auto result = app->setup(static_cast<uint32_t>(SDL_ThreadID()));
        if (result != RT64::Application::SetupResult::Success) {
            fprintf(stderr, "[hm64::gfx] RT64 Application::setup() failed: %d\n",
                    static_cast<int>(result));
            delete app;
            app = nullptr;
            setup_result = ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
            chosen_api   = ultramodern::renderer::GraphicsApi::Vulkan;
            return;
        }

        fprintf(stdout, "[hm64::gfx] RT64 initialized successfully\n");
        fflush(stdout);
        setup_result = ultramodern::renderer::SetupResult::Success;
        chosen_api   = ultramodern::renderer::GraphicsApi::Vulkan;
    }

    ~RT64RendererContext() override {
        if (app) { app->end(); delete app; app = nullptr; }
    }

    bool valid() override { return app != nullptr; }

    bool update_config(const ultramodern::renderer::GraphicsConfig&,
                       const ultramodern::renderer::GraphicsConfig&) override { return true; }

    void enable_instant_present() override {}

    void send_dl(const OSTask* task) override {
        if (!app || !rdram) return;

        static uint32_t send_dl_log_count = 0;
        if (send_dl_log_count++ < 24) {
            printf("[hm64::gfx] send_dl called with task data_ptr=0x%llx size=%u\n",
                   (unsigned long long)task->t.data_ptr,
                   static_cast<uint32_t>(task->t.data_size));
            fflush(stdout);
        }

        // Ensure RT64's HLE GBI is initialized before processing the display list.
        // Use HM64's actual gspF3DEX2_fifo symbols from hm64.map.
        if (app->interpreter) {
            constexpr uint32_t F3DEX2_TEXT_PHYS = 0x000EFA80u;
            constexpr uint32_t F3DEX2_DATA_PHYS = 0x000FC7C0u;
            app->interpreter->loadUCodeGBI(F3DEX2_TEXT_PHYS, F3DEX2_DATA_PHYS, /*resetFromTask=*/true);
        }

        // data_ptr is a virtual address; strip the segment bits for a
        // physical offset into RDRAM. HM64 submits sceneGraphDisplayList for
        // sprites/scene plus separate init/viewport tasks. The init and
        // viewport tasks can still wedge or FPE RT64 in this partially patched
        // boot path, so process scene DLs and present manually afterward.
        uint32_t dl_start = static_cast<uint32_t>(task->t.data_ptr) & 0x00FFFFFFu;
        bool is_scene_task = dl_start >= 0x00180000u && dl_start < 0x00190000u;
        if (!is_scene_task) {
            static uint32_t skipped_dl_count = 0;
            if (skipped_dl_count++ < 16) {
                fprintf(stdout, "[hm64::gfx] skipping unknown DL task start=0x%06X\n", dl_start);
                fflush(stdout);
            }
            return;
        }

        // The boot/logo path can submit a tiny placeholder scene DL before
        // title assets exist. RT64 may hang while chasing that incomplete
        // graph, which starves the real title DL queued behind it.
        if (is_scene_task && task->t.data_size < 512) {
            static uint32_t skipped_short_scene_count = 0;
            if (skipped_short_scene_count++ < 8) {
                fprintf(stdout,
                        "[hm64::gfx] skipping short pre-title scene DL start=0x%06X size=%u\n",
                        dl_start, static_cast<uint32_t>(task->t.data_size));
                fflush(stdout);
            }
            return;
        }

        app->processDisplayLists(rdram, dl_start, 0, /*isHLE=*/true);
        app->updateScreen();
    }

    void update_screen() override {
        if (!app) return;

        const ultramodern::renderer::ViRegs* vi = ultramodern::renderer::get_vi_regs();
        if (vi) {
            vi_status   = vi->VI_STATUS_REG;
            vi_origin   = vi->VI_ORIGIN_REG;
            vi_width    = vi->VI_WIDTH_REG;
            vi_intr     = vi->VI_INTR_REG;
            vi_vcurrent = vi->VI_V_CURRENT_LINE_REG;
            vi_timing   = vi->VI_TIMING_REG;
            vi_vsync    = vi->VI_V_SYNC_REG;
            vi_hsync    = vi->VI_H_SYNC_REG;
            vi_leap     = vi->VI_LEAP_REG;
            vi_hstart   = vi->VI_H_START_REG;
            vi_vstart   = vi->VI_V_START_REG;
            vi_vburst   = vi->VI_V_BURST_REG;
            vi_xscale   = vi->VI_X_SCALE_REG;
            vi_yscale   = vi->VI_Y_SCALE_REG;
        }

        if (rdram) {
            // Some HM64 PC boot paths bypass the original VI mode setup or leave
            // the runtime's dummy VI framebuffer active. Prefer NuSystem's
            // current framebuffer pointer so RT64 scans out what the game just
            // rendered instead of the dummy black buffer.
            uint32_t cfbp = *reinterpret_cast<uint32_t*>(rdram + (0x80205750u & 0x1FFFFFFFu));
            if (cfbp == 0 && (vi_origin == 0 || vi_width == 0)) {
                cfbp = 0x8038F800u;
            }

            if (cfbp != 0) {
                vi_origin = cfbp & 0x00FFFFFFu;
                vi_width = 320;
                vi_status = 0x0000320Eu;
                vi_hstart = 0x006C02ECu;
                vi_vstart = 0x002501FFu;
                vi_xscale = 0x00000200u;
                vi_yscale = 0x00000400u;

                if (last_present_fb != cfbp) {
                    fprintf(stdout, "[hm64::gfx] VI present fb=0x%08X origin=0x%06X width=%u\n",
                            cfbp, vi_origin, vi_width);
                    fflush(stdout);
                    last_present_fb = cfbp;
                }
            }
        }

        app->updateScreen();
    }

    void shutdown() override {
        if (app) { app->end(); delete app; app = nullptr; }
    }

    uint32_t get_display_framerate() const override { return 60; }
    float    get_resolution_scale()  const override { return 1.0f; }
};

uint32_t RT64RendererContext::s_zero = 0;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void init() {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[hm64::graphics] SDL_InitSubSystem(VIDEO) failed: %s\n", SDL_GetError());
        SDL_Quit(); exit(EXIT_FAILURE);
    }

    // SDL_WINDOW_VULKAN is required so plume/RT64 can create a Vulkan surface.
    s_window = SDL_CreateWindow(
        "Harvest Moon 64 – PC",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W * WINDOW_SCALE, SCREEN_H * WINDOW_SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (!s_window) {
        fprintf(stderr, "[hm64::graphics] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO); SDL_Quit(); exit(EXIT_FAILURE);
    }

    fprintf(stdout, "[hm64::graphics] SDL window created (%dx%d)\n",
            SCREEN_W * WINDOW_SCALE, SCREEN_H * WINDOW_SCALE);
    fflush(stdout);
}

void toggle_fullscreen() {
    if (!s_window) return;
    s_fullscreen = !s_fullscreen;
    SDL_SetWindowFullscreen(s_window, s_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void deinit() {
    if (s_window) { SDL_DestroyWindow(s_window); s_window = nullptr; }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

WindowHandle get_window_handle() { return s_window; }
WindowHandle create_window()     { return s_window; }

std::unique_ptr<RendererContext> create_render_context(
    uint8_t* rdram, WindowHandle window_handle, bool developer_mode)
{
    (void)developer_mode;
    fprintf(stdout, "[hm64::graphics] Creating RT64 renderer context (rdram=%p)\n", rdram);
    fflush(stdout);
    return std::make_unique<RT64RendererContext>(rdram, window_handle);
}

bool run_rsp_task(uint8_t* rdram, const OSTask* task) {
    (void)rdram; (void)task;
    return true;
}

void on_vi_interrupt() {
    hm64_vi_count_increment();

    hm64::input::poll();
    hm64_invoke_retrace_callback();

    static bool s_quit_pushed = false;
    if (hm64::input::should_quit() && !s_quit_pushed) {
        SDL_Event quit_event{};
        quit_event.type = SDL_QUIT;
        SDL_PushEvent(&quit_event);
        s_quit_pushed = true;
    }
}

void update_gfx() {}

} // namespace hm64::graphics
