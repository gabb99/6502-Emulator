# mos6502

A cycle-correct MOS 6502 emulator in C++20. Header-only, no dependencies, no
virtual calls anywhere in the execution path.

```cpp
#include "mos6502/mos6502.hpp"

mos6502::FlatBus bus;
mos6502::Cpu<mos6502::FlatBus> cpu(bus);

bus.setVector(mos6502::kResetVector, 0x0400);
cpu.reset();
cpu.step();          // one instruction; returns the cycles it consumed
cpu.run(1'000'000);  // or run a cycle budget
```

## Design

The CPU is a template over three policies, all resolved at compile time.

| Axis | Purpose |
|---|---|
| `Bus` | What the CPU is plugged into. A concept, not a base class. |
| `Variant` | `Nmos6502` or `Cmos65C02` - the family differences. |
| `Hooks` | Debug instrumentation. Empty by default, so it compiles away. |

Instructions are composed from two orthogonal policy families - operations
("what": `LDA`, `ASL`, `ADC`) and addressing modes ("where": `Absolute`,
`IndirectY`) - joined by a handful of instruction shapes (`ReadInstr`,
`WriteInstr`, `RmwInstr`, ...). About 230 of the 256 opcodes fall out of that
composition; the remaining control-flow instructions own their bus cycles
directly.

**Cycle counts are never looked up in a table.** Each addressing mode issues
its own bus cycles in hardware order, and `Cpu::tick()` advances the bus once
per cycle, so the count emerges from the accesses actually performed. The
`Access` tag (`Read` / `Write` / `ReadModifyWrite`) is what makes the indexed
modes correct: a page cross costs an extra cycle on a read, is paid
unconditionally on a write, and read-modify-write additionally performs the
NMOS dummy write-back.

### Single source of truth

`include/mos6502/instruction_table.def` lists all 256 opcodes once. It is
expanded by the dispatch switch, by the disassembler, and by the tests, so a
mnemonic cannot disagree with what actually executes.

```
X(0xBD, "LDA", LDA, AbsoluteX, ReadInstr)
```

### Files

```
include/mos6502/
  types.hpp               integer aliases, Access tag, vectors
  registers.hpp           register file; flags unpacked, packed only on push
  bus.hpp                 BusLike concept + FlatBus reference implementation
  variants.hpp            NMOS vs CMOS behaviour traits
  addressing.hpp          addressing-mode policies
  operations.hpp          operation policies, including decimal ADC/SBC
  instructions.hpp        the instruction shapes
  instruction_table.def   the opcode list - single source of truth
  cpu.hpp                 the core: dispatch, stack, interrupts
  disassembler.hpp        built from the same table
  tracing.hpp             a Hooks policy that logs every instruction
tools/  run_klaus.cpp, run_harte.cpp, json.hpp, harte.sh, benchmark.cpp
tests/  test_cpu.cpp, data/
```

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build
```

## What is modelled

- All 151 documented opcodes, plus every undocumented one (256/256 decoded).
- Exact cycle counts, verified for all 256 opcodes against the published table.
- Page-cross penalties, including the asymmetry between reads and writes.
- The dummy read of an indexed access landing on the *uncorrected* address.
- The NMOS read-modify-write double write (old value, then new).
- Zero-page wrapping for `zp,X`, `(zp,X)` and `(zp),Y` pointer fetches.
- The `JMP ($xxFF)` page-wrap bug, fixed under `Cmos65C02`.
- NMOS decimal `ADC`/`SBC`, including the odd flag behaviour.
- NMI edge detection and latching; IRQ level sensing.
- The one-instruction delay on `CLI`/`SEI`/`PLP`, and `RTI`'s exemption from it.
- The B flag distinction between `BRK`/`PHP` and `IRQ`/`NMI` pushes.
- `JAM` opcodes wedging the processor: PC stops one past the opcode and the
  address bus parks on the vector pins.
- The unstable stores (`SHA`/`SHX`/`SHY`/`TAS`), including the page-cross case
  where the stored value replaces the address's own high byte.
- Decimal-mode `ARR`, whose nibble corrections are a different algorithm from
  the binary path rather than an adjustment to it.

## Verification

Three suites, all passing.

### Unit tests

```bash
ctest --test-dir build
```

112 checks with no external framework: the cycle count of every opcode, bus
access order, the quirks listed above, and an end-to-end program whose total
cycle count is asserted exactly.

### Klaus Dormann's functional tests

```bash
./build/run_klaus Klaus-tests/data/6502_functional_test.bin 0x0400 0x3469
```

> `PASS - trapped at success address $3469 after 96241374 cycles`

Exercises every documented opcode's logic, including decimal mode, over 96
million cycles. The image is fetched from
[Klaus2m5/6502_65C02_functional_tests](https://github.com/Klaus2m5/6502_65C02_functional_tests);
the success address depends on how the image was assembled and the listing
gives it.

### SingleStepTests / Tom Harte, per cycle

```bash
tools/harte.sh          # all 256 opcodes
tools/harte.sh a9 bd    # or just a few
```

> `ALL PASS - 256 opcodes`

**2,560,000 cases: 10,000 per opcode, all 256 opcodes, zero failures.** Each
case pins down every register, every touched memory byte, and the address,
value and direction of every single bus cycle. This is the suite that validates
the cycle-level design rather than just the results - it is what caught the
internal cycles that were being burned without driving the address bus.

The JSON is ~3.3 MB per opcode (~840 MB for the set) and is not vendored;
`tools/harte.sh` streams it a batch at a time and deletes as it goes, so peak
disk use stays in the tens of megabytes. `KEEP=1` retains the files.

## Known approximations

- **The unstable opcodes' magic constant.** `ANE` and `LXA` compute
  `(A | magic) & X & imm`; `magic` is modelled as `0xEE`, which is what the
  test suite assumes. On real silicon it depends on the individual chip, its
  temperature and the preceding bus value.
- **`JAM` is bounded at 11 cycles**, matching the test suite's convention. A
  real chip hangs until reset, which is why `jammed()` latches.
- **Interrupt hijacking**, where an NMI asserted during a `BRK` sequence
  redirects the vector fetch, is not implemented.
- **Branch interrupt polling.** Interrupts are polled at the instruction
  boundary rather than during a taken branch's extra cycle. Neither external
  suite covers interrupt timing, so this is unverified either way.
- Stepping is instruction-at-a-time, so the CPU cannot be halted *mid*
  instruction. Systems that need that (NES DMC DMA, C64 VIC badlines) would
  need `step()` refactored into a micro-op state machine - a change confined to
  `cpu.hpp`, leaving every operation and addressing policy untouched.

## Performance

Around 196 M instructions/s (563 M cycles/s) on an Apple M-series core at `-O2`
— roughly 560x a 1 MHz 6502.
