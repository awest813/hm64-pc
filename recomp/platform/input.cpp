/**
 * hm64::input – SDL2 keyboard/gamepad input implementation.
 */

#include "input.h"
#include "graphics.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <atomic>

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
static uint16_t s_pending_buttons = 0;
static uint8_t s_pending_stick_keys = 0;
static float s_stick_x = 0;
static float s_stick_y = 0;
static std::atomic<bool> s_quit{false};
static std::mutex s_state_mutex;

static SDL_GameController* s_gamepad = nullptr;

static constexpr float KEYBOARD_STICK_MAGNITUDE = 80.0f / 127.0f;
static constexpr int16_t GAMEPAD_DEAD_ZONE = 3200;

static void open_first_gamepad() {
    if (s_gamepad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) continue;
        s_gamepad = SDL_GameControllerOpen(i);
        if (s_gamepad) {
            printf("[hm64::input] Opened gamepad: %s\n", SDL_GameControllerName(s_gamepad));
            return;
        }
    }
}

void init() {
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[hm64::input] SDL_InitSubSystem failed: %s\n", SDL_GetError());
    }
    open_first_gamepad();
}

void deinit() {
    if (s_gamepad) {
        SDL_GameControllerClose(s_gamepad);
        s_gamepad = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
}

static uint16_t keyboard_button(SDL_Keycode key) {
    switch (key) {
        case SDLK_RETURN: return N64_START_BUTTON;
        case SDLK_c: return N64_A_BUTTON;
        case SDLK_x: return N64_B_BUTTON;
        case SDLK_z: return N64_Z_TRIG;
        case SDLK_q: return N64_L_TRIG;
        case SDLK_LSHIFT: case SDLK_RSHIFT: return N64_R_TRIG;
        case SDLK_UP: return N64_U_JPAD;
        case SDLK_DOWN: return N64_D_JPAD;
        case SDLK_LEFT: return N64_L_JPAD;
        case SDLK_RIGHT: return N64_R_JPAD;
        case SDLK_i: return N64_C_UP;
        case SDLK_k: return N64_C_DOWN;
        case SDLK_j: return N64_C_LEFT;
        case SDLK_l: return N64_C_RIGHT;
        default: return 0;
    }
}

void poll() {
    uint16_t pressed = 0;
    uint8_t stick_keys = 0;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN && !event.key.repeat) {
            pressed |= keyboard_button(event.key.keysym.sym);
            switch (event.key.keysym.sym) {
                case SDLK_w: stick_keys |= 1; break;
                case SDLK_s: stick_keys |= 2; break;
                case SDLK_a: stick_keys |= 4; break;
                case SDLK_d: stick_keys |= 8; break;
                default: break;
            }
        }
        if (event.type == SDL_QUIT) {
            s_quit = true;
        } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
            if (!s_gamepad) {
                s_gamepad = SDL_GameControllerOpen(event.cdevice.which);
            }
        } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
            if (s_gamepad) {
                SDL_Joystick* js = SDL_GameControllerGetJoystick(s_gamepad);
                SDL_JoystickID active_id = SDL_JoystickInstanceID(js);
                if (active_id == event.cdevice.which) {
                    SDL_GameControllerClose(s_gamepad);
                    s_gamepad = nullptr;
                }
            }
        } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            s_quit = true;
        } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F11) {
            hm64::graphics::toggle_fullscreen();
        }
    }

    const uint8_t* keys = SDL_GetKeyboardState(nullptr);

    uint16_t buttons = 0;
    float sx = 0, sy = 0;

    if (keys[SDL_SCANCODE_RETURN]) buttons |= N64_START_BUTTON;
    if (keys[SDL_SCANCODE_Z]) buttons |= N64_Z_TRIG;
    if (keys[SDL_SCANCODE_X]) buttons |= N64_B_BUTTON;
    if (keys[SDL_SCANCODE_C]) buttons |= N64_A_BUTTON;
    if (keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_LSHIFT]) buttons |= N64_R_TRIG;
    if (keys[SDL_SCANCODE_Q]) buttons |= N64_L_TRIG;
    if (keys[SDL_SCANCODE_UP]) buttons |= N64_U_JPAD;
    if (keys[SDL_SCANCODE_DOWN]) buttons |= N64_D_JPAD;
    if (keys[SDL_SCANCODE_LEFT]) buttons |= N64_L_JPAD;
    if (keys[SDL_SCANCODE_RIGHT]) buttons |= N64_R_JPAD;
    if (keys[SDL_SCANCODE_I]) buttons |= N64_C_UP;
    if (keys[SDL_SCANCODE_K]) buttons |= N64_C_DOWN;
    if (keys[SDL_SCANCODE_J]) buttons |= N64_C_LEFT;
    if (keys[SDL_SCANCODE_L]) buttons |= N64_C_RIGHT;

    if (keys[SDL_SCANCODE_W]) sy = KEYBOARD_STICK_MAGNITUDE;
    if (keys[SDL_SCANCODE_S]) sy = -KEYBOARD_STICK_MAGNITUDE;
    if (keys[SDL_SCANCODE_A]) sx = -KEYBOARD_STICK_MAGNITUDE;
    if (keys[SDL_SCANCODE_D]) sx = KEYBOARD_STICK_MAGNITUDE;

    if (s_gamepad) {
        auto btn = [&](SDL_GameControllerButton b) { 
            return SDL_GameControllerGetButton(s_gamepad, b) != 0; 
        };
        if (btn(SDL_CONTROLLER_BUTTON_A)) buttons |= N64_A_BUTTON;
        if (btn(SDL_CONTROLLER_BUTTON_B)) buttons |= N64_B_BUTTON;
        if (btn(SDL_CONTROLLER_BUTTON_START)) buttons |= N64_START_BUTTON;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)) buttons |= N64_U_JPAD;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) buttons |= N64_D_JPAD;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) buttons |= N64_L_JPAD;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) buttons |= N64_R_JPAD;
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) buttons |= N64_R_TRIG;
        if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) buttons |= N64_L_TRIG;
        if (btn(SDL_CONTROLLER_BUTTON_Y)) buttons |= N64_C_UP;
        if (btn(SDL_CONTROLLER_BUTTON_X)) buttons |= N64_C_LEFT;

        int16_t lx = SDL_GameControllerGetAxis(s_gamepad, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t ly = SDL_GameControllerGetAxis(s_gamepad, SDL_CONTROLLER_AXIS_LEFTY);
        if (lx > GAMEPAD_DEAD_ZONE) sx = (float)lx / 32767.0f;
        else if (lx < -GAMEPAD_DEAD_ZONE) sx = (float)lx / 32768.0f;
        if (ly > GAMEPAD_DEAD_ZONE) sy = -(float)ly / 32767.0f;
        else if (ly < -GAMEPAD_DEAD_ZONE) sy = -(float)ly / 32768.0f;
    }

    std::lock_guard lock(s_state_mutex);
    s_pending_stick_keys |= stick_keys;
    s_pending_buttons |= pressed | (buttons & ~s_buttons);
    s_buttons = buttons;
    s_stick_x = sx;
    s_stick_y = sy;
}

bool get_input(int controller_num, uint16_t* buttons, float* x, float* y, bool consume) {
    std::lock_guard lock(s_state_mutex);
    if (controller_num == 0) {
        *buttons = s_buttons;
        *x = s_stick_x;
        *y = s_stick_y;
        if (consume) {
            // Render/OS samples may read state, but only the game tick consumes edges.
            *buttons |= s_pending_buttons;
            if (*x == 0) *x = ((s_pending_stick_keys & 8 ? 1 : 0) -
                              (s_pending_stick_keys & 4 ? 1 : 0)) * KEYBOARD_STICK_MAGNITUDE;
            if (*y == 0) *y = ((s_pending_stick_keys & 1 ? 1 : 0) -
                              (s_pending_stick_keys & 2 ? 1 : 0)) * KEYBOARD_STICK_MAGNITUDE;
            s_pending_buttons = 0;
            s_pending_stick_keys = 0;
        }
        return true;
    }
    *buttons = 0;
    *x = 0;
    *y = 0;
    return false;
}

void set_rumble(int controller_num, bool rumble) {
    if (controller_num == 0 && s_gamepad) {
        SDL_GameControllerRumble(s_gamepad, rumble ? 0xFFFF : 0, rumble ? 0xFFFF : 0, 0);
    }
}

bool should_quit() {
    return s_quit;
}

}
