// mos6502 - cpu.hpp
//
// The CPU core. Stepping is instruction-at-a-time, but every bus access is
// issued in hardware order and ticks the bus, so cycle-level behaviour is
// preserved for anything hanging off the bus.
#pragma once

#include "mos6502/addressing.hpp"
#include "mos6502/bus.hpp"
#include "mos6502/hooks.hpp"
#include "mos6502/instructions.hpp"
#include "mos6502/operations.hpp"
#include "mos6502/registers.hpp"
#include "mos6502/types.hpp"
#include "mos6502/variants.hpp"

namespace mos6502 {

template <BusLike BusT, class VariantT = Nmos6502, class HooksT = NullHooks>
class Cpu {
public:
    using Bus     = BusT;
    using Variant = VariantT;
    using Hooks   = HooksT;

    explicit Cpu(Bus& bus) : bus_(bus) {}

    Registers regs{};

    // ------------------------------------------------------------- control --

    // Seven cycles, of which three are the phantom stack "pushes" that only
    // decrement S.
    void reset() {
        jammed_        = false;
        pendingNmi_    = false;
        pendingIrq_    = false;
        nmiEdge_       = false;
        read(regs.pc);
        read(regs.pc);
        // Three phantom stack pushes: the address bus walks the stack, but the
        // write line is never asserted.
        peekStack(); regs.s = static_cast<u8>(regs.s - 1);
        peekStack(); regs.s = static_cast<u8>(regs.s - 1);
        peekStack(); regs.s = static_cast<u8>(regs.s - 1);
        regs.i    = true;
        const u8 lo = read(kResetVector);
        const u8 hi = read(static_cast<u16>(kResetVector + 1));
        regs.pc   = static_cast<u16>(lo | (hi << 8));
        pollI_    = regs.i;
    }

    // Executes one instruction, or services a pending interrupt. Returns the
    // number of cycles consumed.
    u32 step() {
        const u64 start = cycles_;

        if (jammed_) {           // a JAM opcode holds the bus until reset
            read(regs.pc);
            return static_cast<u32>(cycles_ - start);
        }

        if (pendingNmi_) {
            pendingNmi_ = false;
            hooks_.onInterrupt(*this, true);
            interruptSequence(false, kNmiVector);
            pollI_ = regs.i;
            pollInterrupts();
            return static_cast<u32>(cycles_ - start);
        }
        if (pendingIrq_) {
            pendingIrq_ = false;
            hooks_.onInterrupt(*this, false);
            interruptSequence(false, kIrqVector);
            pollI_ = regs.i;
            pollInterrupts();
            return static_cast<u32>(cycles_ - start);
        }

        // Interrupts are sampled during the penultimate cycle of an
        // instruction, so a flag change made by that instruction is not yet
        // visible to the poll. Latching I here reproduces the CLI/SEI/PLP
        // one-instruction delay without special-casing them.
        pollI_ = regs.i;

        const u16 pc     = regs.pc;
        const u8  opcode = fetch();
        hooks_.onInstruction(*this, pc, opcode);
        dispatch(opcode);

        pollInterrupts();
        return static_cast<u32>(cycles_ - start);
    }

    // Runs until at least `budget` cycles have elapsed; returns the actual
    // count, which overshoots by at most one instruction.
    u64 run(u64 budget) {
        const u64 start = cycles_;
        while (cycles_ - start < budget) step();
        return cycles_ - start;
    }

    // ------------------------------------------------------- interrupt pins --

    // NMI is edge triggered: only the high-to-low transition matters, and it
    // latches, so it cannot be missed.
    void setNmi(bool asserted) {
        if (asserted && !nmiLine_) nmiEdge_ = true;
        nmiLine_ = asserted;
    }
    // IRQ is level sensed: it must still be asserted when polled.
    void setIrq(bool asserted) { irqLine_ = asserted; }

    [[nodiscard]] bool jammed() const { return jammed_; }
    void jam() { jammed_ = true; hooks_.onJam(*this, 0); }

    [[nodiscard]] u64 cycles() const { return cycles_; }
    Hooks&       hooks() { return hooks_; }
    const Hooks& hooks() const { return hooks_; }
    Bus&         bus() { return bus_; }

    // ----------------------------------------------- primitives for policies --
    // Public because the addressing-mode and operation policies drive them.

    void tick() {
        ++cycles_;
        bus_.tick();
    }

    u8 read(u16 addr) {
        tick();
        const u8 value = bus_.read(addr);
        hooks_.onRead(*this, addr, value);
        return value;
    }

    void write(u16 addr, u8 value) {
        tick();
        bus_.write(addr, value);
        hooks_.onWrite(*this, addr, value);
    }

    u8  fetch() { return read(regs.pc++); }

    u16 fetchWord() {
        const u8 lo = fetch();
        const u8 hi = fetch();
        return static_cast<u16>(lo | (hi << 8));
    }

    // Pointer fetches inside page zero wrap at $FF - $00, never $0100.
    u16 readZeroPageWord(u8 addr) {
        const u8 lo = read(addr);
        const u8 hi = read(static_cast<u8>(addr + 1));
        return static_cast<u16>(lo | (hi << 8));
    }

    void push(u8 value) {
        write(static_cast<u16>(kStackBase | regs.s), value);
        regs.s = static_cast<u8>(regs.s - 1);
    }

    u8 pull() {
        regs.s = static_cast<u8>(regs.s + 1);
        return read(static_cast<u16>(kStackBase | regs.s));
    }

    // The discarded stack access that precedes a pull, and the internal cycle
    // of JSR. The address bus carries the current stack pointer.
    u8 peekStack() { return read(static_cast<u16>(kStackBase | regs.s)); }

    void setZN(u8 value) {
        regs.z = value == 0;
        regs.n = (value & 0x80) != 0;
    }

    // RTI is the one instruction whose I-flag change takes effect immediately;
    // it calls this to defeat the usual one-instruction delay.
    void pollImmediate() { pollI_ = regs.i; }

    // Shared by BRK, IRQ and NMI. `isBreak` selects both the pushed B flag and
    // whether the padding byte after the opcode is consumed.
    void interruptSequence(bool isBreak, u16 vector) {
        if (isBreak) {
            fetch();          // padding byte: read, discarded, PC advances past it
        } else {
            read(regs.pc);    // two discarded reads at the current PC
            read(regs.pc);
        }
        push(static_cast<u8>(regs.pc >> 8));
        push(static_cast<u8>(regs.pc & 0xFF));
        push(regs.pack(isBreak));
        regs.i = true;
        if constexpr (Variant::clearDecimalOnInterrupt) regs.d = false;
        const u8 lo = read(vector);
        const u8 hi = read(static_cast<u16>(vector + 1));
        regs.pc = static_cast<u16>(lo | (hi << 8));
    }

private:
    void pollInterrupts() {
        if (nmiEdge_) {
            nmiEdge_    = false;
            pendingNmi_ = true;
        } else if (irqLine_ && !pollI_) {
            pendingIrq_ = true;
        }
    }

    // One switch over 256 compile-time constants: the compiler emits a jump
    // table and inlines each instantiation into its own arm.
    void dispatch(u8 opcode) {
        switch (opcode) {
#define X(code, mnemonic, operation, addressing, shape)                        \
    case code:                                                                 \
        shape<op::operation, mode::addressing>::execute(*this);                \
        break;
#include "mos6502/instruction_table.def"
#undef X
        }
    }

    Bus&  bus_;
    Hooks hooks_{};
    u64   cycles_ = 0;

    bool jammed_     = false;
    bool nmiLine_    = false;
    bool nmiEdge_    = false;
    bool irqLine_    = false;
    bool pendingNmi_ = false;
    bool pendingIrq_ = false;
    bool pollI_      = true;
};

}  // namespace mos6502
