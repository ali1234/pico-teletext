#pragma once

#include <cstdint>

namespace Composite {
    typedef uint8_t *(*user_next_scanline_t)();
    typedef void (*user_hsync_t)(bool, int);
    typedef void (*user_vsync_t)(bool);
    void init(user_next_scanline_t, user_hsync_t, user_vsync_t);
    void start();
}