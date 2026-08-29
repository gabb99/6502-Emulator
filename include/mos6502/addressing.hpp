// mos6502 - addressing.hpp
//
// Addressing modes are policy types: "where", independent of "what". Each one
// runs its own bus cycles in hardware order, so the cycle count of an
// instruction is never a table lookup - it falls out of the accesses actually
// performed.
//
// effective<Access>() is specialised on the access kind because the indexed
// modes have three different cycle profiles depending on it (see Access).
#pragma once

#include "mos6502/types.hpp"

namespace mos6502::mode {

// Operand shape, used by the disassembler.
enum class Syntax {
    Implied, Accumulator, Immediate, ZeroPage, ZeroPageX, ZeroPageY,
    Absolute, AbsoluteX, AbsoluteY, Indirect, IndirectX, IndirectY, Relative
};

// An indexed resolution, before and after indexing. The unstable stores need
// the base address: their value depends on the *un-indexed* high byte, and a
// page cross corrupts the address they finally write to.
struct Effective {
    u16 base;
    u16 addr;
};

struct Implied {
    static constexpr int    operandBytes = 0;
    static constexpr Syntax syntax       = Syntax::Implied;
};

struct Accumulator {
    static constexpr int    operandBytes = 0;
    static constexpr Syntax syntax       = Syntax::Accumulator;
};

struct Immediate {
    static constexpr int    operandBytes = 1;
    static constexpr Syntax syntax       = Syntax::Immediate;

    // No cycle here: the operand fetch *is* the instruction's read.
    template <Access A, class Cpu>
    static u16 effective(Cpu& cpu) {
        return cpu.regs.pc++;
    }
};

struct ZeroPage {
    static constexpr int    operandBytes = 1;
    static constexpr Syntax syntax       = Syntax::ZeroPage;

    template <Access A, class Cpu>
    static u16 effective(Cpu& cpu) {
        return cpu.fetch();
    }
};

// Shared body for zp,X and zp,Y: fetch the base, burn a cycle on a dummy read
// of the *unindexed* address, then wrap the sum into page zero.
template <class Index>
struct ZeroPageIndexed {
    template <Access A, class Cpu>
    static u16 effective(Cpu& cpu) {
        const u8 base = cpu.fetch();
        cpu.read(base);
        return static_cast<u8>(base + Index::get(cpu));
    }
};

struct IndexX { template <class Cpu> static u8 get(Cpu& cpu) { return cpu.regs.x; } };
struct IndexY { template <class Cpu> static u8 get(Cpu& cpu) { return cpu.regs.y; } };

struct ZeroPageX : ZeroPageIndexed<IndexX> {
    static constexpr int    operandBytes = 1;
    static constexpr Syntax syntax       = Syntax::ZeroPageX;
};

struct ZeroPageY : ZeroPageIndexed<IndexY> {
    static constexpr int    operandBytes = 1;
    static constexpr Syntax syntax       = Syntax::ZeroPageY;
};

struct Absolute {
    static constexpr int    operandBytes = 2;
    static constexpr Syntax syntax       = Syntax::Absolute;

    template <Access A, class Cpu>
    static u16 effective(Cpu& cpu) {
        return cpu.fetchWord();
    }
};

// abs,X and abs,Y. The dummy read lands on the *un-fixed-up* address - the
// high byte has not been corrected yet - which is visible to I/O registers,
// so it is reproduced rather than skipped.
template <class Index>
struct AbsoluteIndexed {
    template <Access A, class Cpu>
    static Effective resolve(Cpu& cpu) {
        const u16 base   = cpu.fetchWord();
        const u16 addr   = static_cast<u16>(base + Index::get(cpu));
        const u16 unfixed = static_cast<u16>((base & 0xFF00) | (addr & 0x00FF));
        if constexpr (A == Access::Read) {
            if (crossesPage(base, addr)) cpu.read(unfixed);
        } else {
            cpu.read(unfixed);  // writes and RMW always pay it
        }
        return {base, addr};
    }

    template <Access A, class Cpu>
    static u16 effective(Cpu& cpu) {
        return resolve<A>(cpu).addr;
    }
};

struct AbsoluteX : AbsoluteIndexed<IndexX> {
    static constexpr int    operandBytes = 2;
    static constexpr Syntax syntax       = Syntax::AbsoluteX;
};

struct AbsoluteY : AbsoluteIndexed<IndexY> {
    static constexpr int    operandBytes = 2;
    static constexpr Syntax syntax       = Syntax::AbsoluteY;
};

// JMP ($xxxx) only. Handled by the instruction itself so the page-wrap bug can
// be selected by the chip variant.
struct Indirect {
    static constexpr int    operandBytes = 2;
    static constexpr Syntax syntax       = Syntax::Indirect;
};

// (zp,X): index first, then dereference. The pointer fetch wraps in page zero.
struct IndirectX {
    static constexpr int    operandBytes = 1;
    static constexpr Syntax syntax       = Syntax::IndirectX;

    template <Access A, class Cpu>
    static u16 effective(Cpu& cpu) {
        const u8 zp = cpu.fetch();
        cpu.read(zp);                                   // dummy read before indexing
        return cpu.readZeroPageWord(static_cast<u8>(zp + cpu.regs.x));
    }
};

// (zp),Y: dereference first, then index - so it can cross a page.
struct IndirectY {
    static constexpr int    operandBytes = 1;
    static constexpr Syntax syntax       = Syntax::IndirectY;

    template <Access A, class Cpu>
    static Effective resolve(Cpu& cpu) {
        const u8  zp      = cpu.fetch();
        const u16 base    = cpu.readZeroPageWord(zp);
        const u16 addr    = static_cast<u16>(base + cpu.regs.y);
        const u16 unfixed = static_cast<u16>((base & 0xFF00) | (addr & 0x00FF));
        if constexpr (A == Access::Read) {
            if (crossesPage(base, addr)) cpu.read(unfixed);
        } else {
            cpu.read(unfixed);
        }
        return {base, addr};
    }

    template <Access A, class Cpu>
    static u16 effective(Cpu& cpu) {
        return resolve<A>(cpu).addr;
    }
};

struct Relative {
    static constexpr int    operandBytes = 1;
    static constexpr Syntax syntax       = Syntax::Relative;
};

}  // namespace mos6502::mode
