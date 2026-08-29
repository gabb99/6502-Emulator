// mos6502 - conformance and unit tests.
//
// No external framework: the point is that this builds and runs anywhere with
// a C++20 compiler and nothing else.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mos6502/mos6502.hpp"

using namespace mos6502;

// ------------------------------------------------------------- test harness --

namespace {

int gChecks = 0;
int gFailures = 0;
const char* gSection = "";

void section(const char* name) {
    gSection = name;
    std::printf("\n== %s\n", name);
}

void check(bool ok, const char* expr, int line) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("  FAIL [%s] line %d: %s\n", gSection, line, expr);
    }
}

template <class T>
void checkEq(T actual, T expected, const char* expr, int line) {
    ++gChecks;
    if (!(actual == expected)) {
        ++gFailures;
        std::printf("  FAIL [%s] line %d: %s -> got %lld, want %lld\n", gSection, line,
                    expr, static_cast<long long>(actual), static_cast<long long>(expected));
    }
}

#define CHECK(expr)          check((expr), #expr, __LINE__)
#define CHECK_EQ(a, b)       checkEq<long long>((a), (b), #a, __LINE__)

// A bus that logs every access, for verifying cycle-by-cycle bus order.
class RecordingBus {
public:
    enum class Kind { Read, Write, Idle };
    struct Access { Kind kind; u16 addr; u8 value; };

    u8 read(u16 addr) {
        const u8 value = memory_[addr];
        log.push_back({Kind::Read, addr, value});
        return value;
    }
    void write(u16 addr, u8 value) {
        memory_[addr] = value;
        log.push_back({Kind::Write, addr, value});
    }
    void tick() {}

    u8& operator[](u16 addr) { return memory_[addr]; }
    void setVector(u16 v, u16 t) {
        memory_[v] = static_cast<u8>(t & 0xFF);
        memory_[static_cast<u16>(v + 1)] = static_cast<u8>(t >> 8);
    }

    std::vector<Access> log;
private:
    std::array<u8, 65536> memory_{};
};
static_assert(BusLike<RecordingBus>);

// Builds a CPU sitting at $0400 with the given code, already reset.
struct Machine {
    FlatBus            bus;
    Cpu<FlatBus>       cpu{bus};

    explicit Machine(std::initializer_list<u8> code, u16 org = 0x0400) {
        bus.setVector(kResetVector, org);
        cpu.reset();
        u16 addr = org;
        for (u8 byte : code) bus[addr++] = byte;
        bus.resetCycles();
    }
};

// --------------------------------------------------- canonical cycle counts --
// The published base cycle count for every opcode, illegals included, with no
// page-cross penalty and no branch taken. Cross-checked against the emulator's
// actual bus traffic - not used by the implementation itself.
constexpr u8 kBaseCycles[256] = {
/*      x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF */
/*0x*/   7, 6,11, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,
/*1x*/   2, 5,11, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
/*2x*/   6, 6,11, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,
/*3x*/   2, 5,11, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
/*4x*/   6, 6,11, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,
/*5x*/   2, 5,11, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
/*6x*/   6, 6,11, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,
/*7x*/   2, 5,11, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
/*8x*/   2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
/*9x*/   2, 6,11, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
/*Ax*/   2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
/*Bx*/   2, 5,11, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
/*Cx*/   2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
/*Dx*/   2, 5,11, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
/*Ex*/   2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
/*Fx*/   2, 5,11, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
};

// ------------------------------------------------------------------- tests --

// Every opcode, executed with zeroed operands and zeroed index registers so
// that no page-cross penalty applies and no branch is taken, must consume
// exactly its documented base cycle count.
void testAllOpcodeCycles() {
    section("base cycle count of all 256 opcodes");
    int mismatches = 0;
    for (int code = 0; code < 256; ++code) {
        FlatBus bus;
        bus.setVector(kResetVector, 0x0400);
        Cpu<FlatBus> cpu(bus);
        cpu.reset();
        bus[0x0400] = static_cast<u8>(code);
        bus[0x0401] = 0x00;
        bus[0x0402] = 0x00;

        // Force every branch to fall through, so each measures its base cost.
        switch (code) {
            case 0x10: cpu.regs.n = true;  break;  // BPL
            case 0x30: cpu.regs.n = false; break;  // BMI
            case 0x50: cpu.regs.v = true;  break;  // BVC
            case 0x70: cpu.regs.v = false; break;  // BVS
            case 0x90: cpu.regs.c = true;  break;  // BCC
            case 0xB0: cpu.regs.c = false; break;  // BCS
            case 0xD0: cpu.regs.z = true;  break;  // BNE
            case 0xF0: cpu.regs.z = false; break;  // BEQ
            default: break;
        }

        const u32 cycles = cpu.step();
        if (cycles != kBaseCycles[code]) {
            ++mismatches;
            std::printf("  FAIL opcode $%02X (%s): %u cycles, expected %u\n",
                        code, kInstructionInfo[code].mnemonic, cycles, kBaseCycles[code]);
        }
    }
    ++gChecks;
    if (mismatches) ++gFailures;
    CHECK_EQ(mismatches, 0);
}

void testPageCrossPenalties() {
    section("page-cross penalties");

    {   // LDA $12F0,X with X=$20 crosses into $1310: 4 + 1
        Machine m{0xBD, 0xF0, 0x12};
        m.cpu.regs.x = 0x20;
        CHECK_EQ(m.cpu.step(), 5);
    }
    {   // Same instruction without a crossing stays at 4.
        Machine m{0xBD, 0xF0, 0x12};
        m.cpu.regs.x = 0x0F;
        CHECK_EQ(m.cpu.step(), 4);
    }
    {   // STA $12F0,X pays the penalty whether or not it crosses.
        Machine m{0x9D, 0xF0, 0x12};
        m.cpu.regs.x = 0x0F;
        CHECK_EQ(m.cpu.step(), 5);
    }
    {   // LDA ($10),Y crossing: 5 + 1
        Machine m{0xB1, 0x10};
        m.bus[0x0010] = 0xF0;
        m.bus[0x0011] = 0x12;
        m.cpu.regs.y = 0x20;
        CHECK_EQ(m.cpu.step(), 6);
    }
    {   // ASL $12F0,X is 7 cycles regardless.
        Machine m{0x1E, 0xF0, 0x12};
        m.cpu.regs.x = 0x20;
        CHECK_EQ(m.cpu.step(), 7);
    }
}

void testBranchTiming() {
    section("branch timing");
    {   // Not taken: 2
        Machine m{0xD0, 0x10};
        m.cpu.regs.z = true;
        CHECK_EQ(m.cpu.step(), 2);
    }
    {   // Taken, same page: 3
        Machine m{0xD0, 0x10};
        m.cpu.regs.z = false;
        CHECK_EQ(m.cpu.step(), 3);
        CHECK_EQ(m.cpu.regs.pc, 0x0412);
    }
    {   // Taken across a page boundary: 4
        Machine m({0xD0, 0x7F}, 0x04C0);
        m.cpu.regs.z = false;
        CHECK_EQ(m.cpu.step(), 4);
        CHECK_EQ(m.cpu.regs.pc, 0x0541);
    }
    {   // Backwards branch.
        Machine m{0x10, 0xFC};   // BPL -4
        m.cpu.regs.n = false;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.pc, 0x03FE);
    }
}

// The NMOS part writes the unmodified value back before the modified one. Some
// hardware is driven by that double write, so the order is worth pinning down.
void testReadModifyWriteBusOrder() {
    section("RMW bus access order");
    RecordingBus bus;
    bus.setVector(kResetVector, 0x0400);
    Cpu<RecordingBus> cpu(bus);
    cpu.reset();
    bus[0x0400] = 0x06;   // ASL $10
    bus[0x0401] = 0x10;
    bus[0x0010] = 0x21;
    bus.log.clear();

    cpu.step();

    CHECK_EQ(bus.log.size(), 5u);
    if (bus.log.size() == 5) {
        CHECK(bus.log[0].kind == RecordingBus::Kind::Read  && bus.log[0].addr == 0x0400);
        CHECK(bus.log[1].kind == RecordingBus::Kind::Read  && bus.log[1].addr == 0x0401);
        CHECK(bus.log[2].kind == RecordingBus::Kind::Read  && bus.log[2].addr == 0x0010);
        CHECK(bus.log[3].kind == RecordingBus::Kind::Write && bus.log[3].addr == 0x0010);
        CHECK_EQ(bus.log[3].value, 0x21);   // the dummy write-back: old value
        CHECK(bus.log[4].kind == RecordingBus::Kind::Write && bus.log[4].addr == 0x0010);
        CHECK_EQ(bus.log[4].value, 0x42);   // the real write: shifted
    }
}

// The dummy read of an indexed access lands on the address *before* the high
// byte is corrected. Visible to I/O registers, so it must be reproduced.
void testIndexedDummyReadAddress() {
    section("indexed dummy read hits the uncorrected address");
    RecordingBus bus;
    bus.setVector(kResetVector, 0x0400);
    Cpu<RecordingBus> cpu(bus);
    cpu.reset();
    bus[0x0400] = 0xBD;   // LDA $12F0,X
    bus[0x0401] = 0xF0;
    bus[0x0402] = 0x12;
    cpu.regs.x  = 0x20;
    bus.log.clear();

    cpu.step();

    CHECK_EQ(bus.log.size(), 5u);
    if (bus.log.size() == 5) {
        CHECK_EQ(bus.log[3].addr, 0x1210);   // uncorrected: high byte still $12
        CHECK_EQ(bus.log[4].addr, 0x1310);   // corrected
    }
}

void testZeroPageWrapping() {
    section("zero page wrapping");
    {   // LDA $F0,X with X=$20 wraps to $10, never $0110.
        Machine m{0xB5, 0xF0};
        m.cpu.regs.x  = 0x20;
        m.bus[0x0010] = 0x5A;
        m.bus[0x0110] = 0xFF;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x5A);
    }
    {   // The pointer for ($FF,X) with X=0 is read from $FF and $00.
        Machine m{0xA1, 0xFF};
        m.bus[0x00FF] = 0x34;
        m.bus[0x0000] = 0x12;
        m.bus[0x1234] = 0x77;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x77);
    }
    {   // Same wrap for ($FF),Y.
        Machine m{0xB1, 0xFF};
        m.bus[0x00FF] = 0x34;
        m.bus[0x0000] = 0x12;
        m.bus[0x1234] = 0x99;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x99);
    }
}

void testJmpIndirectPageBug() {
    section("JMP ($xxFF) page bug");
    {   // NMOS: the high byte comes from $30FF's own page, i.e. $3000.
        Machine m{0x6C, 0xFF, 0x30};
        m.bus[0x30FF] = 0x40;
        m.bus[0x3000] = 0x80;
        m.bus[0x3100] = 0x50;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.pc, 0x8040);
    }
    {   // 65C02 fixes it - and spends an extra cycle doing so.
        FlatBus bus;
        bus.setVector(kResetVector, 0x0400);
        Cpu<FlatBus, Cmos65C02> cpu(bus);
        cpu.reset();
        bus[0x0400] = 0x6C; bus[0x0401] = 0xFF; bus[0x0402] = 0x30;
        bus[0x30FF] = 0x40; bus[0x3000] = 0x80; bus[0x3100] = 0x50;
        CHECK_EQ(cpu.step(), 6);
        CHECK_EQ(cpu.regs.pc, 0x5040);
    }
}

void testArithmeticFlags() {
    section("ADC / SBC / CMP flags");
    {   // $50 + $50 = $A0: overflow set, negative set, no carry.
        Machine m{0x69, 0x50};
        m.cpu.regs.a = 0x50;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0xA0);
        CHECK(m.cpu.regs.v);
        CHECK(m.cpu.regs.n);
        CHECK(!m.cpu.regs.c);
    }
    {   // $D0 + $90 = $60 with carry out and overflow.
        Machine m{0x69, 0x90};
        m.cpu.regs.a = 0xD0;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x60);
        CHECK(m.cpu.regs.c);
        CHECK(m.cpu.regs.v);
    }
    {   // SBC borrows when carry is clear: $50 - $10 - 1 = $3F.
        Machine m{0xE9, 0x10};
        m.cpu.regs.a = 0x50;
        m.cpu.regs.c = false;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x3F);
        CHECK(m.cpu.regs.c);
    }
    {   // CMP sets carry on greater-or-equal and leaves A alone.
        Machine m{0xC9, 0x30};
        m.cpu.regs.a = 0x30;
        m.cpu.step();
        CHECK(m.cpu.regs.c);
        CHECK(m.cpu.regs.z);
        CHECK_EQ(m.cpu.regs.a, 0x30);
    }
}

void testDecimalMode() {
    section("decimal mode");
    {   // 49 + 1 = 50 in BCD.
        Machine m{0x69, 0x01};
        m.cpu.regs.a = 0x49;
        m.cpu.regs.d = true;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x50);
        CHECK(!m.cpu.regs.c);
    }
    {   // 99 + 1 wraps to 00 and carries.
        Machine m{0x69, 0x01};
        m.cpu.regs.a = 0x99;
        m.cpu.regs.d = true;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x00);
        CHECK(m.cpu.regs.c);
    }
    {   // 50 - 25 = 25 in BCD (carry set means no borrow).
        Machine m{0xE9, 0x25};
        m.cpu.regs.a = 0x50;
        m.cpu.regs.c = true;
        m.cpu.regs.d = true;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x25);
        CHECK(m.cpu.regs.c);
    }
    {   // 00 - 01 borrows: 99, carry clear.
        Machine m{0xE9, 0x01};
        m.cpu.regs.a = 0x00;
        m.cpu.regs.c = true;
        m.cpu.regs.d = true;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x99);
        CHECK(!m.cpu.regs.c);
    }
    {   // The 65C02 clears D on interrupt entry; the NMOS part does not.
        FlatBus bus;
        bus.setVector(kResetVector, 0x0400);
        bus.setVector(kIrqVector, 0x0500);
        Cpu<FlatBus, Cmos65C02> cpu(bus);
        cpu.reset();
        bus[0x0400] = 0x00;   // BRK
        cpu.regs.d  = true;
        cpu.step();
        CHECK(!cpu.regs.d);
    }
}

void testStack() {
    section("stack and status packing");
    {   // PHA then PLA round-trips, and the stack pointer returns.
        Machine m{0x48, 0x68};
        m.cpu.regs.a = 0x37;
        const u8 sp = m.cpu.regs.s;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.s, sp - 1);
        CHECK_EQ(m.bus[static_cast<u16>(kStackBase | sp)], 0x37);
        m.cpu.regs.a = 0x00;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x37);
        CHECK_EQ(m.cpu.regs.s, sp);
    }
    {   // PHP pushes with B and bit 5 set; PLP ignores both.
        Machine m{0x08};
        m.cpu.regs.c = true;
        m.cpu.regs.n = true;
        const u8 sp = m.cpu.regs.s;
        m.cpu.step();
        const u8 pushed = m.bus[static_cast<u16>(kStackBase | sp)];
        CHECK_EQ(pushed & 0x30, 0x30);
        CHECK_EQ(pushed & 0x81, 0x81);
    }
    {   // JSR pushes the address of its own last byte; RTS adds the one back.
        Machine m{0x20, 0x00, 0x05};   // JSR $0500
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.pc, 0x0500);
        CHECK_EQ(m.bus[0x01FD], 0x04);  // high byte of $0402
        CHECK_EQ(m.bus[0x01FC], 0x02);  // low byte
        m.bus[0x0500] = 0x60;           // RTS
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.pc, 0x0403);
    }
}

void testInterrupts() {
    section("interrupts");
    {   // BRK: pushes PC+2 and a status byte with B set, then vectors.
        Machine m{0x00, 0xEA};
        m.bus.setVector(kIrqVector, 0x0500);
        m.cpu.regs.i = false;
        const u8 sp = m.cpu.regs.s;
        CHECK_EQ(m.cpu.step(), 7);
        CHECK_EQ(m.cpu.regs.pc, 0x0500);
        CHECK(m.cpu.regs.i);
        CHECK_EQ(m.bus[static_cast<u16>(kStackBase | sp)], 0x04);
        CHECK_EQ(m.bus[static_cast<u16>(kStackBase | (sp - 1))], 0x02);
        CHECK_EQ(m.bus[static_cast<u16>(kStackBase | (sp - 2))] & 0x10, 0x10);
    }
    {   // An IRQ pushed status has B clear - the only way to tell them apart.
        Machine m{0xEA, 0xEA};
        m.bus.setVector(kIrqVector, 0x0500);
        m.cpu.regs.i = false;
        m.cpu.setIrq(true);
        m.cpu.step();                     // NOP; the IRQ is sampled during it
        const u8 sp = m.cpu.regs.s;
        CHECK_EQ(m.cpu.step(), 7);        // IRQ sequence
        CHECK_EQ(m.cpu.regs.pc, 0x0500);
        CHECK_EQ(m.bus[static_cast<u16>(kStackBase | (sp - 2))] & 0x10, 0x00);
    }
    {   // A masked IRQ is ignored, however long it is held.
        Machine m{0xEA, 0xEA, 0xEA};
        m.bus.setVector(kIrqVector, 0x0500);
        m.cpu.regs.i = true;
        m.cpu.setIrq(true);
        m.cpu.step();
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.pc, 0x0402);
    }
    {   // NMI is edge triggered and cannot be masked by I.
        Machine m{0xEA, 0xEA};
        m.bus.setVector(kNmiVector, 0x0600);
        m.cpu.regs.i = true;
        m.cpu.setNmi(true);
        m.cpu.step();                     // NOP, edge latched
        CHECK_EQ(m.cpu.step(), 7);
        CHECK_EQ(m.cpu.regs.pc, 0x0600);
    }
    {   // Holding NMI asserted must not re-trigger: it is an edge, not a level.
        Machine m{0xEA, 0xEA, 0xEA, 0xEA};
        m.bus.setVector(kNmiVector, 0x0600);
        m.bus[0x0600] = 0xEA;
        m.cpu.setNmi(true);
        m.cpu.step();
        m.cpu.step();                     // NMI serviced here
        CHECK_EQ(m.cpu.regs.pc, 0x0600);
        m.cpu.step();                     // NOP at $0600
        CHECK_EQ(m.cpu.regs.pc, 0x0601);  // not vectored again
    }
    {   // CLI's effect is delayed by one instruction: the IRQ arrives after
        // the instruction *following* the CLI, not immediately.
        Machine m{0x58, 0xEA, 0xEA};      // CLI, NOP, NOP
        m.bus.setVector(kIrqVector, 0x0500);
        m.cpu.regs.i = true;
        m.cpu.setIrq(true);
        m.cpu.step();                     // CLI
        CHECK_EQ(m.cpu.regs.pc, 0x0401);
        m.cpu.step();                     // NOP still executes
        CHECK_EQ(m.cpu.regs.pc, 0x0402);
        m.cpu.step();                     // now the IRQ is taken
        CHECK_EQ(m.cpu.regs.pc, 0x0500);
    }
    {   // The mirror image: SEI does not prevent the IRQ that follows it.
        Machine m{0x78, 0xEA};            // SEI, NOP
        m.bus.setVector(kIrqVector, 0x0500);
        m.cpu.regs.i = false;
        m.cpu.setIrq(true);
        m.cpu.step();                     // SEI
        m.cpu.step();                     // IRQ taken anyway
        CHECK_EQ(m.cpu.regs.pc, 0x0500);
    }
    {   // RTI restores the flags and returns without the RTS +1.
        Machine m{0x40};                  // RTI
        m.bus[0x01FE] = 0x81;             // status: N and C
        m.bus[0x01FF] = 0x34;             // PCL
        m.bus[0x0100] = 0x12;             // PCH (stack wraps)
        m.cpu.regs.s  = 0xFD;
        CHECK_EQ(m.cpu.step(), 6);
        CHECK_EQ(m.cpu.regs.pc, 0x1234);
        CHECK(m.cpu.regs.n);
        CHECK(m.cpu.regs.c);
    }
}

void testJam() {
    section("JAM halts the processor");
    Machine m{0x02};
    m.cpu.step();
    CHECK(m.cpu.jammed());
    const u16 pc = m.cpu.regs.pc;
    m.cpu.step();
    m.cpu.step();
    CHECK_EQ(m.cpu.regs.pc, pc);          // PC never advances again
}

void testIllegalOpcodes() {
    section("undocumented opcodes");
    {   // LAX loads both A and X.
        Machine m{0xA7, 0x10};
        m.bus[0x0010] = 0x8F;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x8F);
        CHECK_EQ(m.cpu.regs.x, 0x8F);
        CHECK(m.cpu.regs.n);
    }
    {   // SAX stores A AND X without touching either.
        Machine m{0x87, 0x10};
        m.cpu.regs.a = 0xF0;
        m.cpu.regs.x = 0x3C;
        m.cpu.step();
        CHECK_EQ(m.bus[0x0010], 0x30);
        CHECK_EQ(m.cpu.regs.a, 0xF0);
    }
    {   // SLO = ASL memory, then ORA into A.
        Machine m{0x07, 0x10};
        m.bus[0x0010] = 0x40;
        m.cpu.regs.a  = 0x01;
        m.cpu.step();
        CHECK_EQ(m.bus[0x0010], 0x80);
        CHECK_EQ(m.cpu.regs.a, 0x81);
    }
    {   // DCP = DEC memory, then CMP against A.
        Machine m{0xC7, 0x10};
        m.bus[0x0010] = 0x43;
        m.cpu.regs.a  = 0x42;
        m.cpu.step();
        CHECK_EQ(m.bus[0x0010], 0x42);
        CHECK(m.cpu.regs.z);
        CHECK(m.cpu.regs.c);
    }
    {   // ISC = INC memory, then SBC it from A.
        Machine m{0xE7, 0x10};
        m.bus[0x0010] = 0x0F;
        m.cpu.regs.a  = 0x20;
        m.cpu.regs.c  = true;
        m.cpu.step();
        CHECK_EQ(m.bus[0x0010], 0x10);
        CHECK_EQ(m.cpu.regs.a, 0x10);
    }
    {   // ANC leaves the AND result's sign bit in the carry.
        Machine m{0x0B, 0xFF};
        m.cpu.regs.a = 0x80;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x80);
        CHECK(m.cpu.regs.c);
    }
    {   // $EB is an undocumented alias for SBC #.
        Machine m{0xEB, 0x01};
        m.cpu.regs.a = 0x10;
        m.cpu.regs.c = true;
        m.cpu.step();
        CHECK_EQ(m.cpu.regs.a, 0x0F);
    }
}

void testDisassembler() {
    section("disassembler");
    FlatBus bus;
    const u8 code[] = {
        0xA9, 0x42,        // LDA #$42
        0xBD, 0x34, 0x12,  // LDA $1234,X
        0x91, 0x20,        // STA ($20),Y
        0x6C, 0xFF, 0x30,  // JMP ($30FF)
        0x0A,              // ASL A
        0xD0, 0xFC,        // BNE (backwards)
        0xEA,              // NOP
    };
    bus.load(0x0400, code, sizeof code);

    const auto peek = [&bus](u16 a) { return bus.read(a); };
    const char* expected[] = {
        "LDA #$42", "LDA $1234,X", "STA ($20),Y", "JMP ($30FF)",
        "ASL A",    "BNE $0409",   "NOP",
    };

    u16 pc = 0x0400;
    for (const char* want : expected) {
        u16 next = 0;
        const std::string got = disassemble(peek, pc, &next);
        ++gChecks;
        if (got != want) {
            ++gFailures;
            std::printf("  FAIL at $%04X: got \"%s\", want \"%s\"\n", pc, got.c_str(), want);
        }
        pc = next;
    }
    CHECK_EQ(pc, 0x040E);
}

// End to end: sum 1..10 with a real loop, and confirm both the result and the
// exact cycle count of the whole run.
void testProgram() {
    section("end-to-end program");
    Machine m({
        0xA9, 0x00,        // $0400  LDA #$00      total = 0
        0xA2, 0x0A,        // $0402  LDX #$0A      n = 10
        0x18,              // $0404  CLC           loop:
        0x86, 0x10,        // $0405  STX $10
        0x65, 0x10,        // $0407  ADC $10       total += n
        0xCA,              // $0409  DEX
        0xD0, 0xF8,        // $040A  BNE loop
        0x85, 0x11,        // $040C  STA $11
        0x02,              // $040E  JAM           stop
    });

    while (!m.cpu.jammed()) m.cpu.step();

    CHECK_EQ(m.cpu.regs.a, 55);        // 1 + 2 + ... + 10
    CHECK_EQ(m.bus[0x0011], 55);
    CHECK_EQ(m.cpu.regs.x, 0);

    // 2 + 2 setup, ten iterations of CLC+STX+ADC+DEX+BNE, plus STA and JAM.
    // Nine taken branches at 3 cycles, one not taken at 2.
    const u64 loopBody = 10ull * (2 + 3 + 3 + 2);
    const u64 branches = 9ull * 3 + 2;
    CHECK_EQ(m.bus.cycles(), 4 + loopBody + branches + 3 + 11);
}

}  // namespace

int main() {
    testAllOpcodeCycles();
    testPageCrossPenalties();
    testBranchTiming();
    testReadModifyWriteBusOrder();
    testIndexedDummyReadAddress();
    testZeroPageWrapping();
    testJmpIndirectPageBug();
    testArithmeticFlags();
    testDecimalMode();
    testStack();
    testInterrupts();
    testJam();
    testIllegalOpcodes();
    testDisassembler();
    testProgram();

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
