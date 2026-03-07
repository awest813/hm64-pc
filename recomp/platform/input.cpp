/**
 * hm64::input – SDL2 keyboard/gamepad input implementation.
 *
 * Default keyboard mapping:
 *   Enter       → Start
 *   Z           → Z button
 *   X           → B button
 *   C           → A button
 *   Shift       → R trigger
 *   Q           → L trigger
 *   Arrow keys  → D-Pad
 *   WASD        → Analog stick
 *   IJKL        → C-Up / C-Down / C-Left / C-Right
 *
 * SDL2 gamepad (first detected controller) is also supported using the
 * standard SDL GameController API mapping.
 */

#include "input.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <cstring>
#include <cstdio>

// N64 button bitmasks (from libultra <PR/controller.h>)
#define N64_A_BUTTON       0x8000
#define N64_B_BUTTON       0x4000
#define N64_Z_TRIG         0x2000
#define N64_START_BUTTON   0x1000
#define N64_U_JPAD         0x0800
#define N64_D_JPAD         0x0400
#define N64_L_JPAD         0x0200
#define N64_R_JPAD         0x0100
#define N64_L_TRIG         0x0020
#define N64_R_TRIG         0x0010
#define N64_C_UP           0x0008
#define N64_C_DOWN         0x0004
#define N64_C_LEFT         0x0002
#define N64_C_RIGHT        0x0001

namespace hm64::input {

static uint16_t s_buttons = 0;
static int8_t   s_stick_x = 0;
static int8_t   s_stick_y = 0;
static bool     s_quit    = false;

static SDL_GameController* s_gamepad = nullptr;

// Intentionally lower than the N64's full ±127 range so keyboard movement
// feels proportional rather than always at maximum speed.
static constexpr int8_t KEYBOARD_STICK_MAGNITUDE = 80;

// Minimum axis displacement before a gamepad analog axis is considered active.
// This prevents stick drift on gamepads whose physical centre is not perfectly
// at zero.  ~10 % of the SDL full-scale range (32 767).
static constexpr int16_t GAMEPAD_DEAD_ZONE = 3200;

void init() {
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);

    // Open the first available gamepad (optional)
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            s_gamepad = SDL_GameControllerOpen(i);
            if (s_gamepad) {
                printf("[hm64::input] Opened gamepad: %s\n",
                       SDL_GameControllerName(s_gamepad));
                break;
            }
        }
    }
}

void deinit() {
    if (s_gamepad) {
        SDL_GameControllerClose(s_gamepad);
        s_gamepad = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
}

void poll() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            s_quit = true;
        }
        if (event.type == SDL_KEYDOWN &&
            event.key.keysym.sym == SDLK_ESCAPE) {
            s_quit = true;
        }
    }

    // ------------------------------------
    // Keyboard state
    // ------------------------------------
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);

    uint16_t buttons = 0;
    int8_t   sx = 0;
    int8_t   sy = 0;

    if (keys[SDL_SCANCODE_RETURN])      buttons |= N64_START_BUTTON;
    if (keys[SDL_SCANCODE_Z])           buttons |= N64_Z_TRIG;
    if (keys[SDL_SCANCODE_X])           buttons |= N64_B_BUTTON;
    if (keys[SDL_SCANCODE_C])           buttons |= N64_A_BUTTON;
    if (keys[SDL_SCANCODE_RSHIFT] ||
        keys[SDL_SCANCODE_LSHIFT])      buttons |= N64_R_TRIG;
    if (keys[SDL_SCANCODE_Q])           buttons |= N64_L_TRIG;
    if (keys[SDL_SCANCODE_UP])          buttons |= N64_U_JPAD;
    if (keys[SDL_SCANCODE_DOWN])        buttons |= N64_D_JPAD;
    if (keys[SDL_SCANCODE_LEFT])        buttons |= N64_L_JPAD;
    if (keys[SDL_SCANCODE_RIGHT])       buttons |= N64_R_JPAD;

    // C-buttons: IJKL (mirrors the WASD analog cluster, one position to the right)
    if (keys[SDL_SCANCODE_I])           buttons |= N64_C_UP;
    if (keys[SDL_SCANCODE_K])           buttons |= N64_C_DOWN;
    if (keys[SDL_SCANCODE_J])           buttons |= N64_C_LEFT;
    if (keys[SDL_SCANCODE_L])           buttons |= N64_C_RIGHT;

    // WASD → analog stick
    if (keys[SDL_SCANCODE_W]) sy =  KEYBOARD_STICK_MAGNITUDE;
    if (keys[SDL_SCANCODE_S]) sy = -KEYBOARD_STICK_MAGNITUDE;
    if (keys[SDL_SCANCODE_A]) sx = -KEYBOARD_STICK_MAGNITUDE;
    if (keys[SDL_SCANCODE_D]) sx =  KEYBOARD_STICK_MAGNITUDE;

    // ------------------------------------
    // SDL Gamepad (if present)
    // ------------------------------------
    if (s_gamepad) {
        auto btn = [&](SDL_GameControllerButton b) -> bool {
            return SDL_GameControllerGetButton(s_gamepad, b) != 0;
        };
        if (btn(SDL_CONTROLLER_BUTTON_A))             buttons |= N64_A_BUTTON;
        if (btn(SDL_CONTROLLER_BUTTON_B))             buttons |= N64_B_BUTTON;
        if (btn(SDL_CONTROLLER_BUTTON_START))         buttons |= N64_START_BUTTON;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP))       buttons |= N64_U_JPAD;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN))     buttons |= N64_D_JPAD;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT))     buttons |= N64_L_JPAD;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT))    buttons |= N64_R_JPAD;
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) buttons |= N64_R_TRIG;
        if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  buttons |= N64_L_TRIG;

        // Left stick for analog
        auto axis_to_byte = [](int16_t v) -> int8_t {
            // SDL range: -32768 .. 32767 → N64 range: -128 .. 127
            return static_cast<int8_t>(v >> 8);
        };
        int16_t lx = SDL_GameControllerGetAxis(s_gamepad,
                                               SDL_CONTROLLER_AXIS_LEFTX);
        int16_t ly = SDL_GameControllerGetAxis(s_gamepad,
                                               SDL_CONTROLLER_AXIS_LEFTY);
        // Apply dead zone: ignore small deflections to prevent stick drift.
        if (lx > GAMEPAD_DEAD_ZONE || lx < -GAMEPAD_DEAD_ZONE) sx = axis_to_byte(lx);
        if (ly > GAMEPAD_DEAD_ZONE || ly < -GAMEPAD_DEAD_ZONE) sy = axis_to_byte(-ly); // Invert Y

        // Z-trigger (left trigger axis)
        int16_t zt = SDL_GameControllerGetAxis(s_gamepad,
                                               SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        if (zt > 8000) buttons |= N64_Z_TRIG;
    }

    s_buttons = buttons;
    s_stick_x = sx;
    s_stick_y = sy;
}

void get_controller(int controller_index,
                    uint16_t& out_buttons,
                    int8_t&   out_stick_x,
                    int8_t&   out_stick_y) {
    if (controller_index == 0) {
        out_buttons = s_buttons;
        out_stick_x = s_stick_x;
        out_stick_y = s_stick_y;
    } else {
        out_buttons = 0;
        out_stick_x = 0;
        out_stick_y = 0;
    }
}

bool should_quit() {
    return s_quit;
}

} // namespace hm64::input
