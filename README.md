pico-teletext

This is an experimental teletext inserter for Raspberry Pi Pico and similar RP2040 and RP2350 boards.

It can work as a standalone video generator with just two resistors and a connector.

It can also lock to an external video signal using a LM1980 or similar sync separator.

It uses three PIO programs:

`draw` outputs pixel data.

`counter` receives CSYNC, VSYNC, and FIELD signals on three pins, and triggers the draw program to output a line at the right time.

`sync` generates CSYNC, VSYNC, and FIELD according to a bytecode program which allows different types of syncs to be generated.

The three signals used, CSYNC, VSYNC and FIELD are the same as those generated by an LM1980 or similar. When the internal
`sync` program is enabled, it outputs the signals on the same pins that are read by `counter`. When using an external 
sync separator, you just need to connect it to those pins and disable the internal generator.

The code also create a text console in the visible framebuffer. By default this is used to display inserter status when
recieving packets on USB, or a test pattern otherwise.

Packets are sent over USB serial as t42 with escape sequences to mark packet boundaries. To send packets eg from vbit2, use [vhs-teletext](https://github.com/ali1234/vhs-teletext):

    `vbit2 | teletext serial`