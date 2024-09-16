#include <cstdio>
#include <cstring>
#include <tusb.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>

#include "composite.hpp"
#include "buffer.hpp"
#include "demo_buffer.hpp"
#include "chars.hpp"

#define LED_PIN 25

uint8_t fba[45*4] = {0};
uint8_t fbb[45*4] = {0};

uint8_t *next_fb = fba;

#define CONS_W 50
#define CONS_H 25
uint8_t console[CONS_H][CONS_W] = {0};

void fill_buffer_console(uint8_t *buffer, int line) {
    int cr = (line % 10) + 1;
    int y = (line / 10);
    buffer += (((60 - CONS_W) * 3)/2) - 1;
    for (int x=0; x<CONS_W; x++) {
        uint8_t ch = console[y][x];
        *buffer++ |= charset[ch][cr][0];
        *buffer++ = charset[ch][cr][1];
        *buffer++ = charset[ch][cr][2];
        *buffer = charset[ch][cr][3];
    }
}

void fill_buffer_tt(uint8_t *buffer) {
    buffer[0] = buffer[1] = buffer[2] = buffer[3] = 0b10101111;
    buffer[4] = buffer[5] = buffer[6] = buffer[7] = 0b10101111;
    buffer[8] = 0b11111111;
    buffer[9] = 0b10101111;
    buffer[10] = 0b11111010;
    buffer[11] = 0b10101010;
    get_packet(&buffer[12]);
}

#define FIRST_ACTIVE_LINE ((574 - (CONS_H * 20))>>2)

void fill_buffer(uint8_t *buffer, bool field, int raw_line) {
    memset(buffer, 0, 45*4);
    const int line = raw_line + (field ? 6 : 318);
    const int fb_line = (raw_line<<1) + field - 36;
    // bush tv: 6 to 22, 319 to 335
    // samsung: 6 to 22, 318 to 335
    if ((line >= 7 && line <= 21) || (line >= 320 && line <= 334)) {
        // teletext:
        fill_buffer_tt(buffer);
    } else if (line == 23) { // 23 - wss

    } else if (fb_line >= 0 && fb_line <= 573) {
        memset(buffer, 0x11, 45*4);
        // total 574 picture lines: we don't support drawing in half lines
        //buffer[(fb_line>>2) + 24] = 1 << ((fb_line&0x3)*2);
        if ((fb_line>>1) >= FIRST_ACTIVE_LINE && (fb_line>>1) < (FIRST_ACTIVE_LINE + (CONS_H*10))) {
            fill_buffer_console(buffer, (fb_line>>1) - FIRST_ACTIVE_LINE);
        }
    }
}

void serial_pump() {
    memset(console, 0x60 + 0x7f, CONS_H*CONS_W);
    uint8_t buffer[42];
    int pos = 0;

    while (true) {
        int c = getchar();

        switch(c) {
            case 0xff:
                pos = 0;
                break;
            case 0xfe:
                c = getchar() - 2;
            default:
                buffer[pos++] = c & 0xff;
                if (pos == 42) {
                    push_packet(buffer);
                    if (buffer[1] == 0x15 && (buffer[0] &0x80) == 0) {
                        gpio_put(LED_PIN, 1);
                        int row = ((buffer[0]>>1) & 1) | ((buffer[0]>>2) & 2) | ((buffer[0]>>3) & 4);
                        row = row ? row : 8;
                        for (int x = 10; x < 42; x++) {
                            console[row + ((CONS_H - 10) / 2)][x + ((CONS_W - 32) / 2) - 10] = (buffer[x] & 0x7f);
                        }
                        gpio_put(LED_PIN, 0);
                    }
                    pos = 0;
                }
                break;
        }
    }
}

void demo()
{
    static uint8_t page = 0;
    page = page ^ 1;
    gpio_put(LED_PIN, 1);
    for(int y=0; y<24; y++) {
        push_packet(demo_buffer[page][y]);
        for (int x=0; x<40; x++) {
            uint8_t c = demo_buffer[page][y][x+2] & 0x7f;
            if (c >= 0x20)
                console[y+((CONS_H-24)/2)][x+((CONS_W-40)/2)] = c + (y ? 0x60 : 0);
            else
                console[y+((CONS_H-24)/2)][x+((CONS_W-40)/2)] = 0x20;
        }
    }
    gpio_put(LED_PIN, 0);

}

uint8_t *user_next_scanline() {
    return next_fb;
}

void user_hsync(bool field, int line) {
    next_fb = (next_fb == fba) ? fbb : fba;
    fill_buffer(next_fb, field, line);
}

void user_vsync(bool field) {
    ;
}

void core1_entry() {
    user_hsync(false, 0); // pre-fill the first scanline
    Composite::init(user_next_scanline, user_hsync, user_vsync);
    Composite::start();

    while(true) {
        __wfi();
    }
    __builtin_unreachable();
}

int main() {
    stdio_init_all();
    memset(console, 0, CONS_H*CONS_W);
    multicore_launch_core1(core1_entry);
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    while(true) {
        if (tud_cdc_connected()) {
            serial_pump();
        }
        demo();
        sleep_ms(1000);
    }
    __builtin_unreachable();
}
