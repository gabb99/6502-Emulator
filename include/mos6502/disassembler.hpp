// mos6502 - disassembler.hpp
//
// Built from the same instruction_table.def as the dispatcher, so a mnemonic
// can never disagree with what the CPU actually executes.
#pragma once

#include <array>
#include <cstdio>
#include <string>

#include "mos6502/addressing.hpp"
#include "mos6502/types.hpp"

namespace mos6502 {

struct InstructionInfo {
    const char*  mnemonic;
    mode::Syntax syntax;
    int          operandBytes;
};

inline constexpr std::array<InstructionInfo, 256> kInstructionInfo = {{
#define X(code, mnemonic, operation, addressing, shape)                        \
    InstructionInfo{mnemonic, mode::addressing::syntax, mode::addressing::operandBytes},
#include "mos6502/instruction_table.def"
#undef X
}};

// Total length of the instruction in bytes, opcode included.
inline constexpr int instructionLength(u8 opcode) {
    return 1 + kInstructionInfo[opcode].operandBytes;
}

// `peek` must be side-effect free - it is a debugger's view of memory, not a
// bus cycle. Returns the text; `next` receives the following address.
template <class Peek>
inline std::string disassemble(Peek&& peek, u16 pc, u16* next = nullptr) {
    const u8   opcode = peek(pc);
    const auto info   = kInstructionInfo[opcode];
    const u8   lo     = info.operandBytes >= 1 ? peek(static_cast<u16>(pc + 1)) : 0;
    const u8   hi     = info.operandBytes >= 2 ? peek(static_cast<u16>(pc + 2)) : 0;
    const u16  word   = static_cast<u16>(lo | (hi << 8));

    if (next) *next = static_cast<u16>(pc + 1 + info.operandBytes);

    char operand[24] = "";
    using S = mode::Syntax;
    switch (info.syntax) {
        case S::Implied:                                            break;
        case S::Accumulator: std::snprintf(operand, sizeof operand, "A");            break;
        case S::Immediate:   std::snprintf(operand, sizeof operand, "#$%02X", lo);   break;
        case S::ZeroPage:    std::snprintf(operand, sizeof operand, "$%02X", lo);    break;
        case S::ZeroPageX:   std::snprintf(operand, sizeof operand, "$%02X,X", lo);  break;
        case S::ZeroPageY:   std::snprintf(operand, sizeof operand, "$%02X,Y", lo);  break;
        case S::Absolute:    std::snprintf(operand, sizeof operand, "$%04X", word);  break;
        case S::AbsoluteX:   std::snprintf(operand, sizeof operand, "$%04X,X", word); break;
        case S::AbsoluteY:   std::snprintf(operand, sizeof operand, "$%04X,Y", word); break;
        case S::Indirect:    std::snprintf(operand, sizeof operand, "($%04X)", word); break;
        case S::IndirectX:   std::snprintf(operand, sizeof operand, "($%02X,X)", lo); break;
        case S::IndirectY:   std::snprintf(operand, sizeof operand, "($%02X),Y", lo); break;
        case S::Relative: {
            const u16 target = static_cast<u16>(pc + 2 + static_cast<i8>(lo));
            std::snprintf(operand, sizeof operand, "$%04X", target);
            break;
        }
    }

    char line[48];
    if (operand[0] == '\0') {
        std::snprintf(line, sizeof line, "%s", info.mnemonic);
    } else {
        std::snprintf(line, sizeof line, "%s %s", info.mnemonic, operand);
    }
    return std::string(line);
}

}  // namespace mos6502
