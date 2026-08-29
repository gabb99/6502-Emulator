// mos6502 - bus.hpp
// The CPU is a template over its bus, so a test bench, a NES cartridge map and
// a C64 PLA all plug in with no virtual dispatch anywhere.
#pragma once

#include <array>

#include "mos6502/types.hpp"

namespace mos6502 {

// tick() is called once per bus cycle, immediately before the access it
// belongs to, so a peripheral is caught up to the current cycle at the moment
// the CPU reads or writes it. Internal CPU cycles (dummy reads, stack
// arithmetic) call tick() with no access following.
template <typename T>
concept BusLike = requires(T& bus, u16 addr, u8 data) {
    { bus.read(addr) } -> std::same_as<u8>;
    { bus.write(addr, data) };
    { bus.tick() };
};

// Flat 64 KiB memory. The reference bus for the conformance suites.
class FlatBus {
public:
    FlatBus() { memory_.fill(0); }

    u8   read(u16 addr) const { return memory_[addr]; }
    void write(u16 addr, u8 data) { memory_[addr] = data; }
    void tick() { ++cycles_; }

    [[nodiscard]] u64 cycles() const { return cycles_; }
    void resetCycles() { cycles_ = 0; }

    // Direct access that does not consume a cycle - for loading test images.
    u8&  operator[](u16 addr) { return memory_[addr]; }
    void load(u16 addr, const u8* data, std::size_t size) {
        for (std::size_t k = 0; k < size; ++k) {
            memory_[static_cast<u16>(addr + k)] = data[k];
        }
    }
    void setVector(u16 vector, u16 target) {
        memory_[vector]                          = static_cast<u8>(target & 0xFF);
        memory_[static_cast<u16>(vector + 1)]    = static_cast<u8>(target >> 8);
    }

private:
    std::array<u8, 65536> memory_{};
    u64                   cycles_ = 0;
};

static_assert(BusLike<FlatBus>);

}  // namespace mos6502
