// mos6502 - variants.hpp
// Chip-family differences, resolved at compile time. Empty types: no cost.
#pragma once

namespace mos6502 {

// The original NMOS part, quirks and all.
struct Nmos6502 {
    static constexpr bool decimalMode           = true;
    static constexpr bool jmpIndirectPageBug    = true;   // JMP ($xxFF) wraps
    static constexpr bool clearDecimalOnInterrupt = false;
    static constexpr bool illegalOpcodes        = true;
    static constexpr bool rmwDummyWriteBack     = true;   // read, write old, write new
};

// The CMOS redesign: undocumented opcodes become NOPs, the indirect-JMP bug is
// fixed, D is cleared on interrupt entry, and RMW does a dummy *read* rather
// than a dummy write.
struct Cmos65C02 {
    static constexpr bool decimalMode           = true;
    static constexpr bool jmpIndirectPageBug    = false;
    static constexpr bool clearDecimalOnInterrupt = true;
    static constexpr bool illegalOpcodes        = false;
    static constexpr bool rmwDummyWriteBack     = false;
};

}  // namespace mos6502
