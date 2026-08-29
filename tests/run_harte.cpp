// Runs the SingleStepTests / Tom Harte ProcessorTests suite: 10,000 randomised
// cases per opcode, each specifying the processor state before and after *and*
// every bus cycle in order.
//
//   ./run_harte 6502/v1/a9.json [more.json ...]
//
// This is the suite that validates the cycle-level design rather than just the
// results, because it checks the address and direction of every single cycle.
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "mos6502/mos6502.hpp"
#include "json.hpp"

using namespace mos6502;

namespace {

// Bits 4 and 5 of P are not processor state - they exist only in the byte
// pushed to the stack, which the cycle log checks exactly.
constexpr u8 kStatusMask = 0xCF;

struct CycleRecord {
    u16  addr;
    u8   value;
    bool isWrite;
};

class TestBus {
public:
    u8 read(u16 addr) {
        const u8 value = memory_[addr];
        log.push_back({addr, value, false});
        return value;
    }
    void write(u16 addr, u8 value) {
        memory_[addr] = value;
        log.push_back({addr, value, true});
    }
    void tick() {}

    void clear() {
        memory_.fill(0);
        log.clear();
    }
    u8&  operator[](u16 addr) { return memory_[addr]; }

    std::vector<CycleRecord> log;

private:
    std::array<u8, 65536> memory_{};
};

struct State {
    u16                            pc = 0;
    u8                             s = 0, a = 0, x = 0, y = 0, p = 0;
    std::vector<std::pair<u16, u8>> ram;
};

struct TestCase {
    std::string              name;
    State                    initial;
    State                    final;
    std::vector<CycleRecord> cycles;
};

void parseState(json::Cursor& cur, State& state) {
    state.ram.clear();
    cur.object([&](std::string_view key) {
        if (key == "pc")      state.pc = static_cast<u16>(cur.number());
        else if (key == "s")  state.s  = static_cast<u8>(cur.number());
        else if (key == "a")  state.a  = static_cast<u8>(cur.number());
        else if (key == "x")  state.x  = static_cast<u8>(cur.number());
        else if (key == "y")  state.y  = static_cast<u8>(cur.number());
        else if (key == "p")  state.p  = static_cast<u8>(cur.number());
        else if (key == "ram") {
            cur.array([&] {
                cur.expect('[');
                const auto addr  = static_cast<u16>(cur.number());
                cur.expect(',');
                const auto value = static_cast<u8>(cur.number());
                cur.expect(']');
                state.ram.emplace_back(addr, value);
            });
        } else {
            cur.fail("unexpected key in state object");
        }
    });
}

void parseCase(json::Cursor& cur, TestCase& test) {
    test.cycles.clear();
    cur.object([&](std::string_view key) {
        if (key == "name") {
            test.name = std::string(cur.string());
        } else if (key == "initial") {
            parseState(cur, test.initial);
        } else if (key == "final" || key == "expected") {
            parseState(cur, test.final);
        } else if (key == "cycles") {
            cur.array([&] {
                cur.expect('[');
                const auto addr  = static_cast<u16>(cur.number());
                cur.expect(',');
                const auto value = static_cast<u8>(cur.number());
                cur.expect(',');
                const std::string_view kind = cur.string();
                cur.expect(']');
                test.cycles.push_back({addr, value, kind == "write"});
            });
        } else {
            cur.fail("unexpected key in test case");
        }
    });
}

std::string describeMismatch(const TestCase& test, const Cpu<TestBus>& cpu, const TestBus& bus) {
    char buffer[512];
    std::string out;

    const auto& r = cpu.regs;
    std::snprintf(buffer, sizeof buffer,
                  "    got  PC:%04X A:%02X X:%02X Y:%02X S:%02X P:%02X  cycles:%zu\n"
                  "    want PC:%04X A:%02X X:%02X Y:%02X S:%02X P:%02X  cycles:%zu\n",
                  r.pc, r.a, r.x, r.y, r.s, r.pack(true) & kStatusMask, bus.log.size(),
                  test.final.pc, test.final.a, test.final.x, test.final.y, test.final.s,
                  test.final.p & kStatusMask, test.cycles.size());
    out += buffer;

    const std::size_t count = std::min(bus.log.size(), test.cycles.size());
    for (std::size_t k = 0; k < count; ++k) {
        const auto& got  = bus.log[k];
        const auto& want = test.cycles[k];
        if (got.addr != want.addr || got.value != want.value || got.isWrite != want.isWrite) {
            std::snprintf(buffer, sizeof buffer,
                          "    cycle %zu: got %s $%04X=%02X, want %s $%04X=%02X\n", k,
                          got.isWrite ? "write" : "read ", got.addr, got.value,
                          want.isWrite ? "write" : "read ", want.addr, want.value);
            out += buffer;
        }
    }
    return out;
}

struct FileResult {
    int passed = 0;
    int failed = 0;
};

FileResult runFile(const char* path, int maxReports) {
    FileResult result;

    std::FILE* file = std::fopen(path, "rb");
    if (!file) {
        std::printf("%-28s CANNOT OPEN\n", path);
        result.failed = 1;
        return result;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (std::fread(text.data(), 1, text.size(), file) != text.size()) {
        std::fclose(file);
        std::printf("%-28s SHORT READ\n", path);
        result.failed = 1;
        return result;
    }
    std::fclose(file);

    TestBus  bus;
    TestCase test;
    int      reported = 0;

    json::Cursor cur(text.data(), text.data() + text.size());
    cur.array([&] {
        parseCase(cur, test);

        bus.clear();
        for (const auto& [addr, value] : test.initial.ram) bus[addr] = value;

        Cpu<TestBus> cpu(bus);
        cpu.regs.pc = test.initial.pc;
        cpu.regs.a  = test.initial.a;
        cpu.regs.x  = test.initial.x;
        cpu.regs.y  = test.initial.y;
        cpu.regs.s  = test.initial.s;
        cpu.regs.unpack(test.initial.p);
        bus.log.clear();

        cpu.step();

        bool ok = cpu.regs.pc == test.final.pc && cpu.regs.a == test.final.a &&
                  cpu.regs.x == test.final.x && cpu.regs.y == test.final.y &&
                  cpu.regs.s == test.final.s &&
                  (cpu.regs.pack(true) & kStatusMask) == (test.final.p & kStatusMask);

        if (ok) {
            for (const auto& [addr, value] : test.final.ram) {
                if (bus[addr] != value) { ok = false; break; }
            }
        }
        if (ok && bus.log.size() != test.cycles.size()) ok = false;
        if (ok) {
            for (std::size_t k = 0; k < bus.log.size(); ++k) {
                if (bus.log[k].addr != test.cycles[k].addr ||
                    bus.log[k].value != test.cycles[k].value ||
                    bus.log[k].isWrite != test.cycles[k].isWrite) {
                    ok = false;
                    break;
                }
            }
        }

        if (ok) {
            ++result.passed;
        } else {
            ++result.failed;
            if (reported < maxReports) {
                ++reported;
                std::printf("  FAIL %s\n%s", test.name.c_str(),
                            describeMismatch(test, cpu, bus).c_str());
            }
        }
    });

    const char* base = std::strrchr(path, '/');
    std::printf("%-12s %5d passed %5d failed%s\n", base ? base + 1 : path, result.passed,
                result.failed, result.failed ? "   <<<" : "");
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <test.json> [more.json ...]\n", argv[0]);
        return 2;
    }
    int maxReports = 3;
    if (const char* env = std::getenv("HARTE_MAX_REPORTS")) maxReports = std::atoi(env);

    FileResult total;
    for (int k = 1; k < argc; ++k) {
        const FileResult one = runFile(argv[k], maxReports);
        total.passed += one.passed;
        total.failed += one.failed;
    }
    std::printf("\nTOTAL: %d passed, %d failed\n", total.passed, total.failed);
    return total.failed == 0 ? 0 : 1;
}
