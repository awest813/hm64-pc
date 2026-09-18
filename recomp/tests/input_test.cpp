#include "input.h"
#include <SDL2/SDL.h>
#include <cassert>
#include <cstdio>

namespace hm64::graphics { void toggle_fullscreen() {} }

static void tap(SDL_Keycode key) {
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    assert(SDL_PushEvent(&event) == 1);
    event.type = SDL_KEYUP;
    assert(SDL_PushEvent(&event) == 1);
}

int main() {
    hm64::input::init();
    tap(SDLK_RETURN);
    hm64::input::poll();
    // A renderer/OS controller sample must not consume game input.
    uint16_t buttons;
    float x, y;
    assert(hm64::input::get_input(0, &buttons, &x, &y));
    hm64::input::poll();
    hm64::input::get_input(0, &buttons, &x, &y, true);
    assert(buttons == 0x1000);
    hm64::input::get_input(0, &buttons, &x, &y, true);
    assert(buttons == 0);
    tap(SDLK_c);
    tap(SDLK_x);
    hm64::input::poll();
    hm64::input::get_input(0, &buttons, &x, &y, true);
    assert(buttons == 0xC000);
    hm64::input::get_input(0, &buttons, &x, &y, true);
    assert(buttons == 0);
    tap(SDLK_s);
    hm64::input::poll();
    hm64::input::get_input(0, &buttons, &x, &y);
    hm64::input::poll();
    hm64::input::get_input(0, &buttons, &x, &y, true);
    assert(y < -0.6f && x == 0);
    hm64::input::get_input(0, &buttons, &x, &y, true);
    assert(y == 0 && x == 0);
    hm64::input::deinit();
    std::puts("Short button presses survive polling and are consumed once.");
}
