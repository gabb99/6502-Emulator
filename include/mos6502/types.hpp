// mos6502 - types.hpp
// Fundamental integer aliases and the access-kind tag used to specialise
// addressing modes.
#pragma once

#include <cstdint>

namespace mos6502 {

using u8  = std::uint8_t;
using i8  = std::int8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// How an instruction touches the effective address. This is not cosmetic:
// it selects the cycle profile of the indexed addressing modes.
//
//   Read            - page cross costs one extra cycle
//   Write           - the penalty cycle is paid unconditionally
//   ReadModifyWrite - unconditional penalty, plus the NMOS dummy write-back
enum class Access { Read, Write, ReadModifyWrite };

// Vectors.
inline constexpr u16 kNmiVector   = 0xFFFA;
inline constexpr u16 kResetVector = 0xFFFC;
inline constexpr u16 kIrqVector   = 0xFFFE;

inline constexpr u16 kStackBase = 0x0100;

// True when advancing `base` by `index` leaves the 256-byte page of `base`.
constexpr bool crossesPage(u16 base, u16 effective) {
    return (base & 0xFF00) != (effective & 0xFF00);
}

}  // namespace mos6502
