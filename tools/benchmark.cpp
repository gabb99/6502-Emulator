// Measures raw interpretation throughput. The workload is a tight loop mixing
// immediate, zero-page and indexed accesses so that the dispatch switch is not
// trivially predictable.
#include <chrono>
#include <cstdio>

#include "mos6502/mos6502.hpp"

using namespace mos6502;

int main() {
    FlatBus bus;
    const u8 program[] = {
        0xA9, 0x00,        // LDA #$00
        0xA2, 0x00,        // LDX #$00
        0xA0, 0x00,        // LDY #$00
        0x18,              // loop: CLC
        0x69, 0x01,        //   ADC #$01
        0x85, 0x10,        //   STA $10
        0xE6, 0x11,        //   INC $11
        0xBD, 0x00, 0x02,  //   LDA $0200,X
        0xE8,              //   INX
        0xC8,              //   INY
        0x4C, 0x06, 0x04,  //   JMP loop
    };
    bus.load(0x0400, program, sizeof program);
    bus.setVector(kResetVector, 0x0400);

    Cpu<FlatBus> cpu(bus);
    cpu.reset();

    constexpr u64 kInstructions = 200'000'000;
    const auto start = std::chrono::steady_clock::now();
    for (u64 k = 0; k < kInstructions; ++k) cpu.step();
    const auto finish = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(finish - start).count();
    std::printf("%llu instructions in %.3f s\n",
                static_cast<unsigned long long>(kInstructions), seconds);
    std::printf("%.1f M instructions/s, %.1f M cycles/s (%.0fx a 1 MHz 6502)\n",
                kInstructions / seconds / 1e6,
                cpu.cycles() / seconds / 1e6,
                cpu.cycles() / seconds / 1e6);
    return 0;
}
