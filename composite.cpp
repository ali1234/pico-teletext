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

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "pico/stdlib.h"

#include "composite.pio.h"
#include "composite.hpp"


namespace Composite {

    namespace {
        struct SyncCmd {
            uint32_t repeat : 9, delay1 : 6, vsync : 1, oddeven : 1, delay2 : 9;
        };
        static_assert(sizeof(SyncCmd) == 4, "SyncCmd is wrong size.");

    // helper for building commands
    #define SYNCCMD(r, d1, v, o, d2) {(r)-1, (((d1)*3)/2)-3, v, o, (((d2)*9)/2)-8}


        alignas(8*4) const SyncCmd syncdata[8] = {
                // repeat r times:
                //     csync=0
                //     delay d1 usecs
                //     csync=1, vsync=v, oddeven=o
                //     delay d2 usecs

                //        r  d1  v  o  d2
                SYNCCMD(  5, 30, 0, 1,  2), // field 1: long x 5
                SYNCCMD(  5,  2, 1, 1, 30), // short x 5
                SYNCCMD(305,  4, 1, 1, 60), // normal x 305: lines 6 - 310
                SYNCCMD(  5,  2, 1, 1, 30), // short x 5

                SYNCCMD(  5, 30, 0, 0,  2), // field 2: long x 5
                SYNCCMD(  4,  2, 1, 0, 30), // short x 4
                SYNCCMD(305,  4, 1, 0, 60), // normal x 305: lines 318 - 622
                SYNCCMD(  6,  2, 1, 0, 30), // short x 6
        };

        PIO _pio = pio0;

        int dma_sync = -1, dma_draw = -1, dma_ctrl = -1;
        int sm_sync = -1, sm_counter = -1, sm_draw = -1;

        bool field = false;
        int line = 0;


        user_next_scanline_t _user_next_scanline = nullptr;
        user_hsync_t _user_hsync = nullptr;
        user_vsync_t _user_vsync = nullptr;


        void hsync() {
            dma_hw->ints0 = 1u << dma_draw;
            if (line < 305)
                dma_hw->ch[dma_draw].al3_read_addr_trig = (uint32_t) _user_next_scanline();
                line += 1;
                if (line < 305)
                    _user_hsync(field, line);
        }

        void vsync() {
            pio_interrupt_clear(pio0, 0);
            field = gpio_get(4);
            line = 0;
            _user_vsync(field);
            _user_hsync(field, line);
            hsync();
        }
    }

    void init(user_next_scanline_t uns, user_hsync_t uhs, user_vsync_t uvs) {
        // set sys_clk to 166.5MHz
        // divide by 12 to get 13.875MHz clock, ie 2 cycles = 1 teletext pixel
        // divide by 111 to get 1.5MHz clock, ie 3 cycles = 2us
        // divide by 37 to get 4.5MHz clock, ie 9 cycles = 2us
        set_sys_clock_pll(1332 * MHZ, 4, 2);

        _user_next_scanline = uns;
        _user_hsync = uhs;
        _user_vsync = uvs;

        sm_sync = pio_claim_unused_sm(_pio, true);
        sm_counter = pio_claim_unused_sm(_pio, true);
        sm_draw = pio_claim_unused_sm(_pio, true);

        dma_sync = dma_claim_unused_channel(true);
        dma_draw = dma_claim_unused_channel(true);
        dma_ctrl = dma_claim_unused_channel(true);


        sync_program_init(
                _pio, sm_sync,
                pio_add_program(_pio, &sync_program),
                2 // uses 2, 3, 4
        );

        counter_program_init(
                _pio, sm_counter,
                pio_add_program(_pio, &counter_program),
                2
        );

        draw_program_init(
                _pio, sm_draw,
                pio_add_program(_pio, &draw_program),
                0
        );


        // sync DMA: writes 8 words then triggers control
        dma_channel_config c = dma_channel_get_default_config(dma_sync);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_dreq(&c, pio_get_dreq(_pio, sm_sync, true));
        channel_config_set_ring(&c, false, 5);
        channel_config_set_chain_to(&c, dma_ctrl);
        dma_channel_configure(dma_sync, &c, &_pio->txf[sm_sync], syncdata, 8, false);


        // draw DMA: transfer in one line of pixel data
        dma_channel_config d = dma_channel_get_default_config(dma_draw);
        channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
        channel_config_set_read_increment(&d, true);
        channel_config_set_dreq(&d, pio_get_dreq(_pio, sm_draw, true));
        dma_channel_configure(dma_draw, &d, &_pio->txf[sm_draw], nullptr, 45, false);

        int sc = 8;

        dma_channel_config e = dma_channel_get_default_config(dma_ctrl);
        channel_config_set_transfer_data_size(&e, DMA_SIZE_32);
        channel_config_set_read_increment(&e, false);
        dma_channel_configure(dma_ctrl, &e, &dma_hw->ch[dma_sync].al1_transfer_count_trig, &sc, 1, false);

        pio_set_irq0_source_enabled(pio0, pis_interrupt0, true);
        irq_set_exclusive_handler(PIO0_IRQ_0, vsync);
        irq_set_enabled(PIO0_IRQ_0, true);

        dma_set_irq0_channel_mask_enabled(1 << dma_draw, true);
        irq_set_exclusive_handler(DMA_IRQ_0, hsync);
        irq_set_enabled(DMA_IRQ_0, true);
    }

    void start() {
        pio_enable_sm_mask_in_sync(_pio, (1 << sm_sync) | (1 << sm_counter) | (1 << sm_draw));
        dma_channel_start(Composite::dma_sync);
    }
}