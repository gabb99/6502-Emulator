// mos6502 - hooks.hpp
// Debug instrumentation policy. The default is empty, so every call site
// disappears; tracing costs nothing when it is switched off.
#pragma once

#include "mos6502/types.hpp"

namespace mos6502 {

struct NullHooks {
    template <class Cpu> void onInstruction(Cpu&, u16 /*pc*/, u8 /*opcode*/) {}
    template <class Cpu> void onRead(Cpu&, u16 /*addr*/, u8 /*value*/) {}
    template <class Cpu> void onWrite(Cpu&, u16 /*addr*/, u8 /*value*/) {}
    template <class Cpu> void onInterrupt(Cpu&, bool /*isNmi*/) {}
    template <class Cpu> void onJam(Cpu&, u8 /*opcode*/) {}
};

}  // namespace mos6502
