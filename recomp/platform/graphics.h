#pragma once

#include <cstdint>
#include <memory>
#include <SDL2/SDL.h>

#include <ultramodern/renderer_context.hpp>

namespace hm64::graphics {

void init();
void deinit();
void toggle_fullscreen();

using RendererContext = ultramodern::renderer::RendererContext;
using WindowHandle = SDL_Window*;

std::unique_ptr<RendererContext> create_render_context(uint8_t* rdram, WindowHandle window_handle, bool developer_mode);
WindowHandle get_window_handle();
bool run_rsp_task(uint8_t* rdram, const OSTask* task);
void on_vi_interrupt();
void update_gfx();
// Keep display lists and their referenced game memory alive until HLE parses them.
void submit_task_and_wait(uint8_t* rdram, uint32_t task_address);
WindowHandle create_window();

}
