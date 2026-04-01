#pragma once

#include <cstdint>
#include <cstddef>

namespace hm64::input {

void init();
void deinit();
void poll();
bool get_input(int controller_num, uint16_t* buttons, float* x, float* y);
void set_rumble(int controller_num, bool rumble);
bool should_quit();

}
