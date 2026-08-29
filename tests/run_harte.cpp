// Runs the SingleStepTests / Tom Harte ProcessorTests suite: 10,000 randomised
// cases per opcode, each specifying the processor state before and after *and*
// every bus cycle in order.
//
//   ./run_harte                    every opcode in the default suite directory
//   ./run_harte a9 bd 91           just these opcodes
//   ./run_harte path/to/a9.json    an explicit file
//   ./run_harte Harte-65x02/wdc65c02/v1    every opcode in another directory
//
// The default directory is $HARTE_DIR if set, otherwise the vendored
// Harte-65x02/6502/v1, searched from the working directory and its parents so
// that running from either the project root or build/ works.
//
// This is the suite that validates the cycle-level design rather than just the
// results, because it checks the address and direction of every single cycle.
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
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

namespace fs = std::filesystem;

// $HARTE_DIR wins; otherwise look for the vendored suite from here upwards, so
// the binary works when run from the project root or from build/.
fs::path findDefaultDirectory() {
    if (const char* fromEnv = std::getenv("HARTE_DIR")) {
        if (fs::is_directory(fromEnv)) return fromEnv;
        std::fprintf(stderr, "HARTE_DIR=%s is not a directory\n", fromEnv);
        return {};
    }
    static constexpr const char* kCandidates[] = {
        "Harte-65x02/6502/v1",       "../Harte-65x02/6502/v1",
        "../../Harte-65x02/6502/v1", "6502/v1",
        "../6502/v1",
    };
    std::error_code ignored;
    for (const char* candidate : kCandidates) {
        if (fs::is_directory(candidate, ignored)) return candidate;
    }
    return {};
}

// Every .json in the directory, in name order - which for this suite is
// opcode order, 00 through ff.
std::vector<fs::path> jsonFilesIn(const fs::path& directory) {
    std::vector<fs::path> files;
    std::error_code       ignored;
    for (const auto& entry : fs::directory_iterator(directory, ignored)) {
        if (entry.is_regular_file(ignored) && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool looksLikeOpcode(std::string_view text) {
    if (text.empty() || text.size() > 2) return false;
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

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
    int maxReports = 3;
    if (const char* env = std::getenv("HARTE_MAX_REPORTS")) maxReports = std::atoi(env);

    const fs::path defaultDirectory = findDefaultDirectory();
    std::vector<fs::path> files;

    if (argc < 2) {
        // No arguments: run the whole suite.
        if (defaultDirectory.empty()) {
            std::fprintf(stderr,
                         "no test directory found.\n"
                         "  run from the project root, or set HARTE_DIR to a directory\n"
                         "  of per-opcode JSON files (e.g. Harte-65x02/6502/v1).\n\n"
                         "usage: %s [opcode | file.json | directory ...]\n",
                         argv[0]);
            return 2;
        }
        files = jsonFilesIn(defaultDirectory);
        if (files.empty()) {
            std::fprintf(stderr, "no .json files in %s\n", defaultDirectory.c_str());
            return 2;
        }
        std::printf("running %zu opcode files from %s\n\n", files.size(),
                    defaultDirectory.c_str());
    } else {
        std::error_code ignored;
        for (int k = 1; k < argc; ++k) {
            const std::string argument = argv[k];
            if (fs::is_directory(argument, ignored)) {
                const auto found = jsonFilesIn(argument);
                files.insert(files.end(), found.begin(), found.end());
            } else if (looksLikeOpcode(argument)) {
                // A bare opcode resolves against the default directory.
                if (defaultDirectory.empty()) {
                    std::fprintf(stderr,
                                 "cannot resolve opcode '%s': no test directory found "
                                 "(set HARTE_DIR)\n",
                                 argument.c_str());
                    return 2;
                }
                char name[8];
                std::snprintf(name, sizeof name, "%02x.json",
                              static_cast<unsigned>(std::strtoul(argument.c_str(), nullptr, 16)));
                files.push_back(defaultDirectory / name);
            } else {
                files.emplace_back(argument);
            }
        }
    }

    FileResult total;
    for (const auto& file : files) {
        const FileResult one = runFile(file.c_str(), maxReports);
        total.passed += one.passed;
        total.failed += one.failed;
    }
    std::printf("\n%zu file%s - TOTAL: %d passed, %d failed\n", files.size(),
                files.size() == 1 ? "" : "s", total.passed, total.failed);
    return total.failed == 0 ? 0 : 1;
}
