// mos6502 - tracing.hpp
// A Hooks policy that emits one nestest-style line per instruction. Swap it in
// as the third template argument of Cpu to get a full execution log.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "mos6502/disassembler.hpp"
#include "mos6502/types.hpp"

namespace mos6502 {

struct TracingHooks {
    std::vector<std::string> lines;
    bool                     enabled = true;

    template <class Cpu> void onInstruction(Cpu& cpu, u16 pc, u8 opcode) {
        if (!enabled) return;
        const auto peek = [&cpu](u16 a) { return cpu.bus().read(a); };
        u16        next = 0;
        const std::string text = disassemble(peek, pc, &next);

        char line[128];
        std::snprintf(line, sizeof line,
                      "%04X  %02X  %-12s A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu",
                      pc, opcode, text.c_str(), cpu.regs.a, cpu.regs.x, cpu.regs.y,
                      cpu.regs.pack(false), cpu.regs.s,
                      static_cast<unsigned long long>(cpu.cycles()));
        lines.emplace_back(line);
    }

    template <class Cpu> void onRead(Cpu&, u16, u8) {}
    template <class Cpu> void onWrite(Cpu&, u16, u8) {}
    template <class Cpu> void onInterrupt(Cpu&, bool) {}
    template <class Cpu> void onJam(Cpu&, u8) {}
};

}  // namespace mos6502
