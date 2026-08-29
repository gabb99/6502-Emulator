// mos6502 - instructions.hpp
//
// Instruction shapes: the small set of templates that combine an operation
// with an addressing mode. Roughly 230 of the 256 opcodes are produced by
// composing these; the rest are SpecialInstr, which owns its own cycles.
#pragma once

#include "mos6502/types.hpp"

namespace mos6502 {

// Load-and-operate: LDA, ADC, CMP, BIT, ...
template <class Op, class Mode>
struct ReadInstr {
    template <class Cpu> static void execute(Cpu& c) {
        const u16 addr = Mode::template effective<Access::Read>(c);
        Op::apply(c, c.read(addr));
    }
};

// Store: STA, STX, SAX, ... The address is handed to the operation because the
// unstable stores (SHA/SHX/SHY/TAS) derive their value from it.
template <class Op, class Mode>
struct WriteInstr {
    template <class Cpu> static void execute(Cpu& c) {
        const u16 addr = Mode::template effective<Access::Write>(c);
        c.write(addr, Op::value(c, addr));
    }
};

// Read-modify-write. The NMOS part writes the *old* value back before the new
// one - two writes, both visible on the bus, which some hardware relies on.
template <class Op, class Mode>
struct RmwInstr {
    template <class Cpu> static void execute(Cpu& c) {
        using V = typename Cpu::Variant;
        const u16 addr  = Mode::template effective<Access::ReadModifyWrite>(c);
        const u8  value = c.read(addr);
        if constexpr (V::rmwDummyWriteBack) {
            c.write(addr, value);
        } else {
            c.read(addr);
        }
        c.write(addr, Op::apply(c, value));
    }
};

// ASL A, ROR A, ... : the same operation, applied to the accumulator. The
// second cycle is a discarded read of the byte after the opcode - the chip
// always drives the address bus, so it is issued as a real access.
template <class Op, class Mode>
struct AccumInstr {
    template <class Cpu> static void execute(Cpu& c) {
        c.read(c.regs.pc);
        c.regs.a = Op::apply(c, c.regs.a);
    }
};

// Two-cycle register and flag instructions. The tick is the discarded read of
// the byte after the opcode. Stack instructions also land here and issue their
// own further accesses.
template <class Op, class Mode>
struct ImpliedInstr {
    template <class Cpu> static void execute(Cpu& c) {
        c.read(c.regs.pc);
        Op::apply(c);
    }
};

// 2 cycles not taken, 3 taken, 4 taken across a page boundary.
template <class Cond, class Mode>
struct BranchInstr {
    template <class Cpu> static void execute(Cpu& c) {
        const auto offset = static_cast<i8>(c.fetch());
        if (!Cond::test(c)) return;
        const u16 from   = c.regs.pc;
        const u16 target = static_cast<u16>(from + offset);
        c.read(from);                       // discarded read at the untaken path
        if (crossesPage(from, target)) {
            // The fixup cycle reads the target with an uncorrected high byte.
            c.read(static_cast<u16>((from & 0xFF00) | (target & 0x00FF)));
        }
        c.regs.pc = target;
    }
};

// SHA, SHX, SHY, TAS. These genuinely do not fit the ordinary store path: the
// value depends on the high byte of the address *before* indexing, and when
// the index crosses a page the stored value replaces the address's own high
// byte - the chip drops the fixup onto the data bus.
template <class Op, class Mode>
struct UnstableStoreInstr {
    template <class Cpu> static void execute(Cpu& c) {
        const auto e     = Mode::template resolve<Access::Write>(c);
        const u8   value = Op::value(c, static_cast<u8>(e.base >> 8));
        const u16  addr  = crossesPage(e.base, e.addr)
                              ? static_cast<u16>((static_cast<u16>(value) << 8) |
                                                 (e.addr & 0x00FF))
                              : e.addr;
        c.write(addr, value);
    }
};

// JSR, RTS, RTI, BRK, JMP, JAM: control flow whose bus pattern does not
// decompose into "addressing mode plus operation".
template <class Op, class Mode>
struct SpecialInstr {
    template <class Cpu> static void execute(Cpu& c) { Op::execute(c); }
};

}  // namespace mos6502
