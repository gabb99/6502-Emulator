// mos6502 - operations.hpp
//
// Operations are policy types: "what", independent of "where". They are
// grouped by signature so that instructions compose mechanically:
//
//   read op    : void apply(Cpu&, u8 value)
//   write op   : u8   value(Cpu&, u16 addr)      (addr matters only to SHA/SHX/SHY/TAS)
//   rmw op     : u8   apply(Cpu&, u8 value)
//   implied op : void apply(Cpu&)
//   branch     : bool test(Cpu&)
//   special    : void execute(Cpu&)              (owns its own bus cycles)
#pragma once

#include "mos6502/types.hpp"

namespace mos6502::op {

// ---------------------------------------------------------------- helpers --

template <class Cpu>
inline void compare(Cpu& c, u8 reg, u8 value) {
    const u16 diff = static_cast<u16>(reg) - value;
    c.regs.c = reg >= value;
    c.setZN(static_cast<u8>(diff));
}

// NMOS decimal ADC: the low nibble is corrected first, N and V are taken from
// the half-corrected high nibble, and Z comes from the plain binary sum. That
// asymmetry is the hardware's, not a simplification.
template <class Cpu>
inline void addWithCarry(Cpu& c, u8 value) {
    using V = typename Cpu::Variant;
    auto&    r     = c.regs;
    const u8 a     = r.a;
    const u16 carry = r.c ? 1u : 0u;
    const u16 bin   = static_cast<u16>(a) + value + carry;

    if constexpr (V::decimalMode) {
        if (r.d) {
            u16 lo = static_cast<u16>(a & 0x0F) + (value & 0x0F) + carry;
            u16 hi = static_cast<u16>(a >> 4) + (value >> 4);

            r.z = (bin & 0xFF) == 0;
            if (lo > 0x09) { lo += 0x06; hi += 1; }
            r.n = ((hi << 4) & 0x80) != 0;
            r.v = ((~(a ^ value) & (a ^ static_cast<u8>(hi << 4))) & 0x80) != 0;
            if (hi > 0x09) hi += 0x06;
            r.c = hi > 0x0F;
            r.a = static_cast<u8>((hi << 4) | (lo & 0x0F));
            return;
        }
    }

    r.c = bin > 0xFF;
    r.v = ((~(a ^ value) & (a ^ static_cast<u8>(bin))) & 0x80) != 0;
    r.a = static_cast<u8>(bin);
    c.setZN(r.a);
}

// NMOS decimal SBC: every flag comes from the binary subtraction; only the
// stored result is BCD-corrected.
template <class Cpu>
inline void subtractWithBorrow(Cpu& c, u8 value) {
    using V = typename Cpu::Variant;
    auto&    r      = c.regs;
    const u8 a      = r.a;
    const u16 carry = r.c ? 1u : 0u;
    const u16 bin   = static_cast<u16>(a) + static_cast<u8>(~value) + carry;

    r.c = bin > 0xFF;
    r.v = (((a ^ value) & (a ^ static_cast<u8>(bin))) & 0x80) != 0;
    c.setZN(static_cast<u8>(bin));

    if constexpr (V::decimalMode) {
        if (r.d) {
            int lo = (a & 0x0F) - (value & 0x0F) - static_cast<int>(1 - carry);
            int hi = (a >> 4) - (value >> 4);
            if (lo & 0x10) { lo -= 0x06; hi -= 1; }
            if (hi & 0x10) { hi -= 0x06; }
            r.a = static_cast<u8>(((hi & 0x0F) << 4) | (lo & 0x0F));
            return;
        }
    }
    r.a = static_cast<u8>(bin);
}

// ---------------------------------------------------------------- read ops --

#define MOS6502_READ_OP(NAME, BODY)                                            \
    struct NAME {                                                              \
        template <class Cpu> static void apply(Cpu& c, u8 value) { BODY }      \
    };

MOS6502_READ_OP(LDA, { c.regs.a = value; c.setZN(value); })
MOS6502_READ_OP(LDX, { c.regs.x = value; c.setZN(value); })
MOS6502_READ_OP(LDY, { c.regs.y = value; c.setZN(value); })
MOS6502_READ_OP(ORA, { c.regs.a |= value; c.setZN(c.regs.a); })
MOS6502_READ_OP(AND, { c.regs.a &= value; c.setZN(c.regs.a); })
MOS6502_READ_OP(EOR, { c.regs.a ^= value; c.setZN(c.regs.a); })
MOS6502_READ_OP(ADC, { addWithCarry(c, value); })
MOS6502_READ_OP(SBC, { subtractWithBorrow(c, value); })
MOS6502_READ_OP(CMP, { compare(c, c.regs.a, value); })
MOS6502_READ_OP(CPX, { compare(c, c.regs.x, value); })
MOS6502_READ_OP(CPY, { compare(c, c.regs.y, value); })
MOS6502_READ_OP(BIT, {
    c.regs.z = (c.regs.a & value) == 0;
    c.regs.n = (value & 0x80) != 0;
    c.regs.v = (value & 0x40) != 0;
})
// Illegal but stable.
MOS6502_READ_OP(LAX, { c.regs.a = value; c.regs.x = value; c.setZN(value); })
MOS6502_READ_OP(NOP_R, { (void)c; (void)value; })
MOS6502_READ_OP(ANC, {
    c.regs.a &= value; c.setZN(c.regs.a); c.regs.c = c.regs.n;
})
MOS6502_READ_OP(ALR, {
    c.regs.a &= value;
    c.regs.c = (c.regs.a & 0x01) != 0;
    c.regs.a = static_cast<u8>(c.regs.a >> 1);
    c.setZN(c.regs.a);
})
MOS6502_READ_OP(ARR, {
    using V = typename Cpu::Variant;
    auto&      r        = c.regs;
    const u8   t        = static_cast<u8>(r.a & value);
    const bool oldCarry = r.c;
    u8         result   = static_cast<u8>((t >> 1) | (oldCarry ? 0x80 : 0));

    if constexpr (V::decimalMode) {
        if (r.d) {
            // In decimal mode the flags come from the un-adjusted rotate, and
            // each nibble is then corrected independently.
            r.n = oldCarry;
            r.z = result == 0;
            r.v = ((t ^ result) & 0x40) != 0;
            const u8 lowNibble  = static_cast<u8>(t & 0x0F);
            const u8 highNibble = static_cast<u8>(t >> 4);
            if (lowNibble + (lowNibble & 1) > 5) {
                result = static_cast<u8>((result & 0xF0) | ((result + 6) & 0x0F));
            }
            r.c = (highNibble + (highNibble & 1)) > 5;
            if (r.c) result = static_cast<u8>(result + 0x60);
            r.a = result;
            return;
        }
    }
    r.a = result;
    c.setZN(result);
    r.c = ((result >> 6) & 1) != 0;
    r.v = (((result >> 6) ^ (result >> 5)) & 1) != 0;
})
MOS6502_READ_OP(SBX, {
    const u8 lhs = static_cast<u8>(c.regs.a & c.regs.x);
    c.regs.c = lhs >= value;
    c.regs.x = static_cast<u8>(lhs - value);
    c.setZN(c.regs.x);
})
MOS6502_READ_OP(LAS, {
    const u8 result = static_cast<u8>(value & c.regs.s);
    c.regs.a = result; c.regs.x = result; c.regs.s = result;
    c.setZN(result);
})
// Unstable: the "magic" constant depends on the individual chip, temperature
// and the preceding bus value. 0xEE is the conventional stand-in.
MOS6502_READ_OP(ANE, {
    c.regs.a = static_cast<u8>((c.regs.a | 0xEE) & c.regs.x & value);
    c.setZN(c.regs.a);
})
MOS6502_READ_OP(LXA, {
    const u8 result = static_cast<u8>((c.regs.a | 0xEE) & value);
    c.regs.a = result; c.regs.x = result;
    c.setZN(result);
})
#undef MOS6502_READ_OP

// --------------------------------------------------------------- write ops --

#define MOS6502_WRITE_OP(NAME, BODY)                                           \
    struct NAME {                                                              \
        template <class Cpu> static u8 value(Cpu& c, u16 addr) { BODY }        \
    };

MOS6502_WRITE_OP(STA, { (void)addr; return c.regs.a; })
MOS6502_WRITE_OP(STX, { (void)addr; return c.regs.x; })
MOS6502_WRITE_OP(STY, { (void)addr; return c.regs.y; })
MOS6502_WRITE_OP(SAX, { (void)addr; return static_cast<u8>(c.regs.a & c.regs.x); })
#undef MOS6502_WRITE_OP

// Unstable stores. The value is ANDed with the high byte of the *base* address
// plus one - not the indexed one - and UnstableStoreInstr additionally applies
// the page-cross address corruption.
#define MOS6502_UNSTABLE_STORE_OP(NAME, BODY)                                  \
    struct NAME {                                                              \
        template <class Cpu> static u8 value(Cpu& c, u8 baseHigh) { BODY }     \
    };

MOS6502_UNSTABLE_STORE_OP(SHA, {
    return static_cast<u8>(c.regs.a & c.regs.x & (baseHigh + 1));
})
MOS6502_UNSTABLE_STORE_OP(SHX, { return static_cast<u8>(c.regs.x & (baseHigh + 1)); })
MOS6502_UNSTABLE_STORE_OP(SHY, { return static_cast<u8>(c.regs.y & (baseHigh + 1)); })
MOS6502_UNSTABLE_STORE_OP(TAS, {
    c.regs.s = static_cast<u8>(c.regs.a & c.regs.x);
    return static_cast<u8>(c.regs.s & (baseHigh + 1));
})
#undef MOS6502_UNSTABLE_STORE_OP

// ----------------------------------------------------------------- rmw ops --

#define MOS6502_RMW_OP(NAME, BODY)                                             \
    struct NAME {                                                              \
        template <class Cpu> static u8 apply(Cpu& c, u8 value) { BODY }        \
    };

MOS6502_RMW_OP(ASL, {
    c.regs.c = (value & 0x80) != 0;
    const u8 r = static_cast<u8>(value << 1);
    c.setZN(r); return r;
})
MOS6502_RMW_OP(LSR, {
    c.regs.c = (value & 0x01) != 0;
    const u8 r = static_cast<u8>(value >> 1);
    c.setZN(r); return r;
})
MOS6502_RMW_OP(ROL, {
    const u8 r = static_cast<u8>((value << 1) | (c.regs.c ? 1 : 0));
    c.regs.c = (value & 0x80) != 0;
    c.setZN(r); return r;
})
MOS6502_RMW_OP(ROR, {
    const u8 r = static_cast<u8>((value >> 1) | (c.regs.c ? 0x80 : 0));
    c.regs.c = (value & 0x01) != 0;
    c.setZN(r); return r;
})
MOS6502_RMW_OP(INC, { const u8 r = static_cast<u8>(value + 1); c.setZN(r); return r; })
MOS6502_RMW_OP(DEC, { const u8 r = static_cast<u8>(value - 1); c.setZN(r); return r; })
// Illegal read-modify-write combinations: shift/step the memory operand, then
// fold it into A with the matching ALU operation.
MOS6502_RMW_OP(SLO, {
    const u8 r = ASL::apply(c, value); c.regs.a |= r; c.setZN(c.regs.a); return r;
})
MOS6502_RMW_OP(RLA, {
    const u8 r = ROL::apply(c, value); c.regs.a &= r; c.setZN(c.regs.a); return r;
})
MOS6502_RMW_OP(SRE, {
    const u8 r = LSR::apply(c, value); c.regs.a ^= r; c.setZN(c.regs.a); return r;
})
MOS6502_RMW_OP(RRA, {
    const u8 r = ROR::apply(c, value); addWithCarry(c, r); return r;
})
MOS6502_RMW_OP(DCP, {
    const u8 r = static_cast<u8>(value - 1); compare(c, c.regs.a, r); return r;
})
MOS6502_RMW_OP(ISC, {
    const u8 r = static_cast<u8>(value + 1); subtractWithBorrow(c, r); return r;
})
#undef MOS6502_RMW_OP

// ------------------------------------------------------------- implied ops --

#define MOS6502_IMPLIED_OP(NAME, BODY)                                         \
    struct NAME {                                                              \
        template <class Cpu> static void apply(Cpu& c) { BODY }                \
    };

MOS6502_IMPLIED_OP(TAX, { c.regs.x = c.regs.a; c.setZN(c.regs.x); })
MOS6502_IMPLIED_OP(TAY, { c.regs.y = c.regs.a; c.setZN(c.regs.y); })
MOS6502_IMPLIED_OP(TXA, { c.regs.a = c.regs.x; c.setZN(c.regs.a); })
MOS6502_IMPLIED_OP(TYA, { c.regs.a = c.regs.y; c.setZN(c.regs.a); })
MOS6502_IMPLIED_OP(TSX, { c.regs.x = c.regs.s; c.setZN(c.regs.x); })
MOS6502_IMPLIED_OP(TXS, { c.regs.s = c.regs.x; })   // the one transfer with no flags
MOS6502_IMPLIED_OP(INX, { c.regs.x = static_cast<u8>(c.regs.x + 1); c.setZN(c.regs.x); })
MOS6502_IMPLIED_OP(INY, { c.regs.y = static_cast<u8>(c.regs.y + 1); c.setZN(c.regs.y); })
MOS6502_IMPLIED_OP(DEX, { c.regs.x = static_cast<u8>(c.regs.x - 1); c.setZN(c.regs.x); })
MOS6502_IMPLIED_OP(DEY, { c.regs.y = static_cast<u8>(c.regs.y - 1); c.setZN(c.regs.y); })
MOS6502_IMPLIED_OP(CLC, { c.regs.c = false; })
MOS6502_IMPLIED_OP(SEC, { c.regs.c = true; })
MOS6502_IMPLIED_OP(CLI, { c.regs.i = false; })
MOS6502_IMPLIED_OP(SEI, { c.regs.i = true; })
MOS6502_IMPLIED_OP(CLV, { c.regs.v = false; })
MOS6502_IMPLIED_OP(CLD, { c.regs.d = false; })
MOS6502_IMPLIED_OP(SED, { c.regs.d = true; })
MOS6502_IMPLIED_OP(NOP, { (void)c; })
// Stack instructions live here too: the implied wrapper supplies the dummy
// read cycle, and the push/pull below supplies the rest.
MOS6502_IMPLIED_OP(PHA, { c.push(c.regs.a); })
MOS6502_IMPLIED_OP(PHP, { c.push(c.regs.pack(true)); })
MOS6502_IMPLIED_OP(PLA, { c.peekStack(); c.regs.a = c.pull(); c.setZN(c.regs.a); })
MOS6502_IMPLIED_OP(PLP, { c.peekStack(); c.regs.unpack(c.pull()); })
#undef MOS6502_IMPLIED_OP

// -------------------------------------------------------------- conditions --

#define MOS6502_BRANCH(NAME, EXPR)                                             \
    struct NAME {                                                              \
        template <class Cpu> static bool test(Cpu& c) { return (EXPR); }       \
    };

MOS6502_BRANCH(BPL, !c.regs.n)
MOS6502_BRANCH(BMI,  c.regs.n)
MOS6502_BRANCH(BVC, !c.regs.v)
MOS6502_BRANCH(BVS,  c.regs.v)
MOS6502_BRANCH(BCC, !c.regs.c)
MOS6502_BRANCH(BCS,  c.regs.c)
MOS6502_BRANCH(BNE, !c.regs.z)
MOS6502_BRANCH(BEQ,  c.regs.z)
#undef MOS6502_BRANCH

// ----------------------------------------------------------------- special --
// These own their bus cycles outright because their access pattern does not
// decompose into an addressing mode plus an ALU step.

struct BRK {
    template <class Cpu> static void execute(Cpu& c) { c.interruptSequence(true, kIrqVector); }
};

struct JSR {
    template <class Cpu> static void execute(Cpu& c) {
        const u8 lo = c.fetch();
        c.peekStack();                             // internal: stack pointer predecrement
        c.push(static_cast<u8>(c.regs.pc >> 8));   // pushes the address of the *last*
        c.push(static_cast<u8>(c.regs.pc & 0xFF)); // operand byte, not the next opcode
        const u8 hi = c.fetch();
        c.regs.pc = static_cast<u16>(lo | (hi << 8));
    }
};

struct RTS {
    template <class Cpu> static void execute(Cpu& c) {
        c.read(c.regs.pc);                         // dummy read of the next byte
        c.peekStack();                             // stack pointer increment
        const u8 lo = c.pull();
        const u8 hi = c.pull();
        c.regs.pc = static_cast<u16>(lo | (hi << 8));
        c.read(c.regs.pc);                         // the +1 that undoes JSR's off-by-one
        ++c.regs.pc;
    }
};

struct RTI {
    template <class Cpu> static void execute(Cpu& c) {
        c.read(c.regs.pc);
        c.peekStack();
        c.regs.unpack(c.pull());
        const u8 lo = c.pull();
        const u8 hi = c.pull();
        c.regs.pc = static_cast<u16>(lo | (hi << 8));  // no +1: RTI returns exactly
        c.pollImmediate();
    }
};

struct JMP_ABS {
    template <class Cpu> static void execute(Cpu& c) { c.regs.pc = c.fetchWord(); }
};

struct JMP_IND {
    template <class Cpu> static void execute(Cpu& c) {
        using V = typename Cpu::Variant;
        const u16 ptr = c.fetchWord();
        const u8  lo  = c.read(ptr);
        u16 hiAddr;
        if constexpr (V::jmpIndirectPageBug) {
            // The high byte is fetched from the same page: $xxFF reads $xx00.
            hiAddr = static_cast<u16>((ptr & 0xFF00) | ((ptr + 1) & 0x00FF));
        } else {
            hiAddr = static_cast<u16>(ptr + 1);
            c.read(c.regs.pc);   // the CMOS fix costs a cycle
        }
        const u8 hi = c.read(hiAddr);
        c.regs.pc = static_cast<u16>(lo | (hi << 8));
    }
};

// Undocumented opcodes that wedge the chip until reset. PC stops one past the
// opcode and the address bus parks on the vector pins. The real part hangs
// forever; the eleven-cycle sequence below is the convention the per-cycle
// test suite uses to bound it.
struct JAM {
    template <class Cpu> static void execute(Cpu& c) {
        c.read(c.regs.pc);
        c.read(0xFFFF);
        c.read(0xFFFE);
        c.read(0xFFFE);
        for (int k = 0; k < 6; ++k) c.read(0xFFFF);
        c.jam();
    }
};

}  // namespace mos6502::op
