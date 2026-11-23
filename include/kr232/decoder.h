#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace kr232 {

// The classes of ISA §3.1's first-byte map, one enumerator per distinct form.
enum class OpClass : std::uint8_t {
    Short,
    Register,
    Compact,
    CompactBranch,
    Normal,
    Generic,
    LongInt,
    FpReg,
    FpMem,
    FpConvert,
    FpStack,
    Enter,
    Wide,
    FpMemWide,
    Reserved,
};

// A generic-form operand: the addressing mode of ISA §9, its register and, when
// the mode has one, its extension.
struct Operand {
    std::uint8_t mode = 0;
    std::uint8_t reg = 0;
    std::int64_t value = 0;
};

// One decoded instruction. `length` is what an executor advances by, so a wrong
// value desynchronizes the machine; the fields a form needs are populated and the
// rest keep their defaults.
struct Decoded {
    bool ok = false;
    unsigned length = 0;
    OpClass kind = OpClass::Reserved;
    std::uint8_t opcode = 0;     // the first byte
    std::uint8_t op = 0;         // op nibble, or the operation byte of an escape
    std::uint8_t a = 0;          // data / destination register, or Fd
    std::uint8_t b = 0;          // base / second register, or Fs
    int64_t immediate = 0;       // sign-extended immediate or displacement
    std::uint32_t address = 0;   // absolute target or address
    Operand op1;                 // the generic form's operands only
    Operand op2;
    std::string text;            // canonical disassembly, lowercase
};

// Decodes one instruction from `bytes`, using `address` only to render a
// PC-relative operand. A reserved or truncated encoding returns ok = false with
// `length` set to the bytes consumed (0 when the first byte is not an opcode)
// and a short reason in `text`.
[[nodiscard]] Decoded decode(std::span<const std::uint8_t> bytes);

}  // namespace kr232
