/*
 * This file is part of pico-teletext (https://github.com/ali1234/pico-teletext).
 * Copyright (c) 2024 Alistair Buxton.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdint>
#include <pico/time.h>
#include <cstring>

#include "buffer.hpp"

#define NBUFFERS 64

uint8_t tt_buffer[NBUFFERS][42];
volatile uint8_t buffer_head = 0;
volatile uint8_t buffer_tail = 0;

// Filler: Blank row in magazine 8 row 25
const uint8_t fill_buffer[42] = {
        0xd0, 0xa1, /* Magazine 8, packet 25 */
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20
};


const uint8_t db[] = {
        0b10101010, 0b10101111, 0b11111010, 0b11111111,
};

void copy_packet(const uint8_t *src, uint8_t *dest)
{
    int n;
    for (n=0; n<42; n++) {
        *dest++ = db[(src[n])&0x3];
        *dest++ = db[(src[n]>>2)&0x3];
        *dest++ = db[(src[n]>>4)&0x3];
        *dest++ = db[(src[n]>>6)&0x3];
    }
}

void get_packet(uint8_t *dest)
{
    if (buffer_head == buffer_tail) {
        copy_packet(fill_buffer, dest);
    } else {
        copy_packet(tt_buffer[buffer_tail], dest);
        buffer_tail = (buffer_tail+1)%NBUFFERS;
    }
}

void push_packet(const uint8_t *src)
{
    while (((buffer_head+1)%NBUFFERS) == buffer_tail) {
        sleep_us(2000);
    }
    memcpy(tt_buffer[buffer_head], src, 42);
    buffer_head = (buffer_head+1)%NBUFFERS;
}
