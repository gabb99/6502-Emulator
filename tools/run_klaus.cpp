// Runs a 6502 test image that signals completion by trapping - branching to
// itself. Written for Klaus Dormann's 6502_functional_test.bin, which loads at
// $0000, starts at $0400 and, on success, traps at a known address.
//
//   ./run_klaus 6502_functional_test.bin [start_pc] [success_pc]
//
// A trap anywhere other than success_pc is a failure, and the reported PC
// points straight at the test that broke.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "mos6502/mos6502.hpp"

using namespace mos6502;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <image.bin> [start_pc] [success_pc]\n", argv[0]);
        return 2;
    }
    const u16 startPc   = argc > 2 ? static_cast<u16>(std::strtoul(argv[2], nullptr, 0)) : 0x0400;
    const u16 successPc = argc > 3 ? static_cast<u16>(std::strtoul(argv[3], nullptr, 0)) : 0x3469;

    std::FILE* file = std::fopen(argv[1], "rb");
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::vector<u8> image(65536);
    const std::size_t size = std::fread(image.data(), 1, image.size(), file);
    std::fclose(file);
    std::printf("loaded %zu bytes from %s\n", size, argv[1]);

    FlatBus bus;
    bus.load(0x0000, image.data(), size);
    Cpu<FlatBus> cpu(bus);
    cpu.reset();
    cpu.regs.pc = startPc;

    for (u64 instructions = 0; instructions < 500'000'000ull; ++instructions) {
        const u16 pc = cpu.regs.pc;
        cpu.step();
        if (cpu.regs.pc == pc) {          // branch-to-self: the image has trapped
            if (pc == successPc) {
                std::printf("PASS - trapped at success address $%04X after %llu cycles\n",
                            pc, static_cast<unsigned long long>(cpu.cycles()));
                return 0;
            }
            std::printf("FAIL - trapped at $%04X (expected $%04X)\n", pc, successPc);
            std::printf("       A:%02X X:%02X Y:%02X P:%02X SP:%02X\n",
                        cpu.regs.a, cpu.regs.x, cpu.regs.y, cpu.regs.pack(false), cpu.regs.s);
            return 1;
        }
        if (cpu.jammed()) {
            std::printf("FAIL - jammed at $%04X\n", pc);
            return 1;
        }
    }
    std::printf("FAIL - no trap reached; last PC $%04X\n", cpu.regs.pc);
    return 1;
}
