// mos6502 - registers.hpp
#pragma once

#include "mos6502/types.hpp"

namespace mos6502 {

// The status flags are kept unpacked. The hot path writes them on nearly every
// instruction, so a packed byte would mean a shift-and-mask per update; they
// are only assembled into a real P byte when pushed to the stack.
struct Registers {
    u8  a  = 0x00;
    u8  x  = 0x00;
    u8  y  = 0x00;
    u8  s  = 0x00;   // stack pointer - undefined at power-on, $FD after reset
    u16 pc = 0x0000;

    bool n = false;  // negative
    bool v = false;  // overflow
    bool d = false;  // decimal
    bool i = true;   // interrupt disable
    bool z = false;  // zero
    bool c = false;  // carry

    // Bit 5 always reads as 1. Bit 4 (B) is not a register at all - it only
    // exists in the pushed copy, set by PHP/BRK and clear by IRQ/NMI.
    [[nodiscard]] constexpr u8 pack(bool breakFlag) const {
        return static_cast<u8>((n ? 0x80 : 0) | (v ? 0x40 : 0) | 0x20 |
                               (breakFlag ? 0x10 : 0) | (d ? 0x08 : 0) |
                               (i ? 0x04 : 0) | (z ? 0x02 : 0) | (c ? 0x01 : 0));
    }

    constexpr void unpack(u8 p) {
        n = (p & 0x80) != 0;
        v = (p & 0x40) != 0;
        d = (p & 0x08) != 0;
        i = (p & 0x04) != 0;
        z = (p & 0x02) != 0;
        c = (p & 0x01) != 0;
    }
};

}  // namespace mos6502
