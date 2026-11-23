#include <kr232/decoder.h>

#include <cstdio>
#include <cstring>

namespace kr232 {
namespace {

constexpr const char* kConditions[] = {"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
                                       "hi", "ls", "ge", "lt", "gt", "le"};

std::string reg(unsigned index) {
    switch (index) {
        case 12:
            return "efp";
        case 13:
            return "fp";
        case 14:
            return "pc";
        case 15:
            return "sp";
        default:
            return "r" + std::to_string(index);
    }
}

std::string freg(unsigned index) { return "f" + std::to_string(index); }

std::string hex(std::uint64_t value, bool pad4 = false) {
    char buffer[24];
    if (pad4 && value <= 0xFFFFu) {
        std::snprintf(buffer, sizeof buffer, "0x%04llx", static_cast<unsigned long long>(value));
    } else {
        std::snprintf(buffer, sizeof buffer, "0x%llx", static_cast<unsigned long long>(value));
    }
    return buffer;
}

std::string decimal(std::int64_t value) { return std::to_string(value); }

// A signed displacement is written the way the ISA writes one: `+8`, `-3`.
std::string displacement(std::int64_t value) { return (value < 0 ? "" : "+") + decimal(value); }

// The operand syntax of one compact register-form opcode.
enum class CompactForm { Binary, Unary, Load, Store, Jump };

struct CompactOp {
    CompactForm form;
    const char* name;
};

// ISA §6.1, ops 0-31.
constexpr CompactOp kCompact[32] = {
    {CompactForm::Binary, "mov"},  {CompactForm::Binary, "add"},  {CompactForm::Binary, "addc"},
    {CompactForm::Binary, "sub"},  {CompactForm::Binary, "subb"}, {CompactForm::Binary, "and"},
    {CompactForm::Binary, "or"},   {CompactForm::Binary, "xor"},  {CompactForm::Binary, "cmp"},
    {CompactForm::Binary, "mul"},  {CompactForm::Binary, "mulh"}, {CompactForm::Binary, "shl"},
    {CompactForm::Binary, "shr"},  {CompactForm::Binary, "sar"},  {CompactForm::Load, "ld"},
    {CompactForm::Store, "st"},    {CompactForm::Load, "ldbu"},   {CompactForm::Load, "ldbs"},
    {CompactForm::Store, "stb"},   {CompactForm::Load, "ld"},     {CompactForm::Store, "st"},
    {CompactForm::Load, "ldbu"},   {CompactForm::Load, "ldbs"},   {CompactForm::Store, "stb"},
    {CompactForm::Load, "ld"},     {CompactForm::Store, "st"},    {CompactForm::Load, "ldbu"},
    {CompactForm::Load, "ldbs"},   {CompactForm::Store, "stb"},   {CompactForm::Unary, "neg"},
    {CompactForm::Unary, "not"},   {CompactForm::Jump, "jmp"},
};

// The bracket form each load/store opcode uses, in the order of §6.1: plain,
// post-increment, plain with byte step, and so on. Deriving it from the opcode
// keeps the table above to one column.
enum class Bracket { Plain, PostIncrement, PreDecrement, PlainByte, PostIncrementByte, PreDecrementByte };

Bracket compact_bracket(unsigned op) {
    switch (op) {
        case 14:
        case 15:
        case 16:
        case 18:
            return Bracket::Plain;
        case 19:
        case 20:
        case 21:
        case 23:
            return Bracket::PostIncrement;
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
            return Bracket::PreDecrement;
        default:
            return Bracket::Plain;
    }
}

std::string bracket(const std::string& base, Bracket form) {
    switch (form) {
        case Bracket::PostIncrement:
            return "[" + base + "]+";
        case Bracket::PreDecrement:
            return "-[" + base + "]";
        default:
            return "[" + base + "]";
    }
}

// Normal class op 0-31: instruction text plus the operand shape it needs.
enum class NormalForm {
    RegDisp,      // Rd, [Rb+d8]
    StoreDisp,    // [Rb+d8], Rs
    RegSigned8,   // Rd, #s8
    RegUnsigned8, // Rd, #u8
    RegReg,       // Rd, Rb
    BranchRel,    // rel16
    PcDisp,       // Rd, [pc+d8]
    Setcc,        // Rd
    Mask16,       // #mask16
    RegOnly,      // Rd
};

struct NormalOp {
    NormalForm form;
    const char* name;
};

// ISA §7.
constexpr NormalOp kNormal[32] = {
    {NormalForm::RegDisp, "ld"},     {NormalForm::StoreDisp, "st"},  {NormalForm::RegDisp, "ldbu"},
    {NormalForm::RegDisp, "ldbs"},   {NormalForm::StoreDisp, "stb"}, {NormalForm::RegSigned8, "ld"},
    {NormalForm::RegUnsigned8, "ld"}, {NormalForm::RegSigned8, "add"},
    {NormalForm::RegSigned8, "sub"}, {NormalForm::RegSigned8, "cmp"},
    {NormalForm::RegUnsigned8, "cmp"}, {NormalForm::RegUnsigned8, "and"},
    {NormalForm::RegUnsigned8, "or"}, {NormalForm::RegUnsigned8, "xor"},
    {NormalForm::RegDisp, "lea"},    {NormalForm::BranchRel, "jmp"}, {NormalForm::BranchRel, "call"},
    {NormalForm::PcDisp, "ldbu"},    {NormalForm::PcDisp, "ldbs"},  {NormalForm::Setcc, "setcc"},
    {NormalForm::RegUnsigned8, "shl"}, {NormalForm::RegUnsigned8, "shr"},
    {NormalForm::RegUnsigned8, "sar"}, {NormalForm::Mask16, "push"},
    {NormalForm::Mask16, "pop"},     {NormalForm::RegReg, "div"},   {NormalForm::RegReg, "divs"},
    {NormalForm::RegOnly, "sxtb"},   {NormalForm::Mask16, "leave"}, {NormalForm::RegDisp, "ldhu"},
    {NormalForm::StoreDisp, "sth"},  {NormalForm::RegDisp, "ldhs"},
};

// F0's operation byte, ISA §8.1.
struct GenericOp {
    const char* name;
    bool store;  // operand order in assembly is [addr], Rs
};

constexpr GenericOp kGeneric[0x1F] = {
    {"mov", false},  {"add", false},  {"addc", false}, {"sub", false}, {"subb", false},
    {"and", false},  {"or", false},   {"xor", false},  {"cmp", false}, {"mul", false},
    {"mulh", false}, {"shl", false},  {"shr", false},  {"sar", false}, {"ld", false},
    {"st", true},    {"ldbu", false}, {"ldbs", false}, {"stb", true},  {"lea", false},
    {"neg", false},  {"not", false},  {"sxtb", false}, {"div", false}, {"divs", false},
    {"jmp", false},  {"call", false}, {"sxth", false}, {"ldhu", false}, {"ldhs", false},
    {"sth", true},
};

// Reads one specifier byte and its extension from `bytes` starting at `offset`.
Operand read_operand(std::span<const std::uint8_t> bytes, unsigned offset) {
    Operand operand;
    operand.mode = (bytes[offset] >> 4) & 0x7u;
    operand.reg = bytes[offset] & 0xFu;
    unsigned extension = 0;
    switch (operand.mode) {
        case 1:
            extension = 2;
            break;
        case 5:
            extension = 1;
            break;
        case 6:
            extension = 2;
            break;
        case 7:
            extension = 4;
            break;
        default:
            return operand;
    }
    if (offset + 1 + extension > bytes.size()) {
        return operand;
    }
    std::uint32_t raw = 0;
    for (unsigned i = 0; i < extension; ++i) {
        raw |= static_cast<std::uint32_t>(bytes[offset + 1 + i]) << (8 * i);
    }
    switch (extension) {
        case 1:
            operand.value = static_cast<std::int8_t>(raw & 0xFFu);
            break;
        case 2:
            operand.value = static_cast<std::int16_t>(raw & 0xFFFFu);
            break;
        default:
            operand.value = static_cast<std::int32_t>(raw);
            break;
    }
    return operand;
}

unsigned extension_bytes(unsigned mode) {
    switch (mode) {
        case 1:
            return 2;
        case 5:
            return 1;
        case 6:
            return 2;
        case 7:
            return 4;
        default:
            return 0;
    }
}

std::string operand_text(const Operand& operand) {
    switch (operand.mode) {
        case 0:
            return reg(operand.reg);
        case 1:
            return "#" + hex(static_cast<std::uint32_t>(operand.value));
        case 2:
            return "[" + reg(operand.reg) + "]";
        case 3:
            return "[" + reg(operand.reg) + "]+";
        case 4:
            return "-[" + reg(operand.reg) + "]";
        case 5:
        case 6:
        case 7:
            return "[" + reg(operand.reg) + displacement(operand.value) + "]";
        default:
            return "?";
    }
}

Decoded fail(std::span<const std::uint8_t> bytes, unsigned consumed, const char* why) {
    Decoded decoded;
    decoded.ok = false;
    decoded.kind = OpClass::Reserved;
    decoded.length = consumed;
    decoded.opcode = bytes.empty() ? 0 : bytes[0];
    decoded.text = why;
    return decoded;
}

Decoded make(OpClass kind, unsigned length, std::uint8_t opcode, std::string text) {
    Decoded decoded;
    decoded.ok = true;
    decoded.kind = kind;
    decoded.length = length;
    decoded.opcode = opcode;
    decoded.text = std::move(text);
    return decoded;
}

}  // namespace

Decoded decode(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return fail(bytes, 0, "end of input");
    }
    const std::uint8_t first = bytes[0];

    // Short class, ISA §4.
    if (first <= 0x4F) {
        const unsigned index = first & 0xFu;
        const char* name = nullptr;
        if (first <= 0x0F) {
            name = "push";
        } else if (first <= 0x1F) {
            name = "pop";
        } else if (first <= 0x2F) {
            name = "inc";
        } else if (first <= 0x3F) {
            name = "dec";
        } else {
            name = "call";
        }
        return make(OpClass::Short, 1, first, std::string(name) + " " + reg(index));
    }

    if (first >= 0x50 && first <= 0x5F) {
        if (bytes.size() < 5) {
            return fail(bytes, 5, "truncated ld Rd,#imm32");
        }
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(bytes[1 + i]) << (8 * i);
        }
        auto decoded = make(OpClass::Register, 5, first,
                            "ld " + reg(first & 0xFu) + ", #" + hex(value));
        decoded.a = first & 0xFu;
        decoded.immediate = value;
        return decoded;
    }

    if (first >= 0x60 && first <= 0x6F) {
        if (bytes.size() < 5) {
            return fail(bytes, 5, "truncated ld Rd,[addr32]");
        }
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(bytes[1 + i]) << (8 * i);
        }
        auto decoded = make(OpClass::Register, 5, first,
                            "ld " + reg(first & 0xFu) + ", [" + hex(value) + "]");
        decoded.a = first & 0xFu;
        decoded.address = value;
        return decoded;
    }

    if (first >= 0xE0 && first <= 0xEF) {
        if (bytes.size() < 5) {
            return fail(bytes, 5, "truncated st [addr32],Rd");
        }
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(bytes[1 + i]) << (8 * i);
        }
        auto decoded = make(OpClass::Register, 5, first,
                            "st [" + hex(value) + "], " + reg(first & 0xFu));
        decoded.a = first & 0xFu;
        decoded.address = value;
        return decoded;
    }

    // The system instructions, and the reserved bytes beside them.
    if (first >= 0x70 && first <= 0x7F) {
        static const char* const names[] = {"ret", "nop", "brk",  "sys", "ei",
                                            "di",  "halt", "iret", "rdcycle", "syscall"};
        if (first <= 0x79) {
            return make(OpClass::Short, 1, first, names[first - 0x70]);
        }
        return fail(bytes, 1, "reserved");
    }

    // Compact register form, ISA §6.1.
    if (first >= 0x80 && first <= 0x9F) {
        if (bytes.size() < 2) {
            return fail(bytes, 2, "truncated compact");
        }
        const unsigned op = first & 0x1Fu;
        const unsigned a = (bytes[1] >> 4) & 0xFu;
        const unsigned b = bytes[1] & 0xFu;
        const CompactOp& entry = kCompact[op];
        std::string text = entry.name;
        switch (entry.form) {
            case CompactForm::Binary:
                text += " " + reg(a) + ", " + reg(b);
                break;
            case CompactForm::Unary:
                if (b != 0) {
                    return fail(bytes, 2, "unary compact requires Rb = R0");
                }
                text += " " + reg(a);
                break;
            case CompactForm::Jump:
                if (b != 0) {
                    return fail(bytes, 2, "jmp compact requires Rb = R0");
                }
                text += " " + reg(a);
                break;
            case CompactForm::Load:
                text += " " + reg(a) + ", " + bracket(reg(b), compact_bracket(op));
                break;
            case CompactForm::Store:
                text += " " + bracket(reg(b), compact_bracket(op)) + ", " + reg(a);
                break;
        }
        auto decoded = make(OpClass::Compact, 2, first, text);
        decoded.op = static_cast<std::uint8_t>(op);
        decoded.a = static_cast<std::uint8_t>(a);
        decoded.b = static_cast<std::uint8_t>(b);
        return decoded;
    }

    // Compact branch form, ISA §6.2.
    if (first >= 0xA0 && first <= 0xBF) {
        if (bytes.size() < 2) {
            return fail(bytes, 2, "truncated branch");
        }
        const unsigned cond = (first >> 1) & 0xFu;
        int disp = static_cast<int>((first & 0x1u) << 8) | bytes[1];
        if (disp & 0x100) {
            disp -= 0x200;
        }
        std::string text;
        if (cond <= 13) {
            text = std::string("b") + kConditions[cond] + " " + displacement(disp);
        } else if (cond == 14) {
            text = "call " + displacement(disp);
        } else {
            text = "bra " + displacement(disp);
        }
        auto decoded = make(OpClass::CompactBranch, 2, first, text);
        decoded.op = static_cast<std::uint8_t>(cond);
        decoded.immediate = disp;
        return decoded;
    }

    // Normal class, ISA §7.
    if (first >= 0xC0 && first <= 0xDF) {
        if (bytes.size() < 3) {
            return fail(bytes, 3, "truncated normal");
        }
        const unsigned op = first & 0x1Fu;
        const NormalOp& entry = kNormal[op];
        const unsigned a = (bytes[1] >> 4) & 0xFu;
        const unsigned b = bytes[1] & 0xFu;
        const auto d8 = static_cast<std::int8_t>(bytes[2]);
        std::string text = entry.name;
        switch (entry.form) {
            case NormalForm::RegDisp:
                text += " " + reg(a) + ", [" + reg(b) + displacement(d8) + "]";
                break;
            case NormalForm::StoreDisp:
                text += " [" + reg(b) + displacement(d8) + "], " + reg(a);
                break;
            case NormalForm::RegSigned8:
                text += " " + reg(a) + ", #" + decimal(d8);
                break;
            case NormalForm::RegUnsigned8:
                text += " " + reg(a) + ", #" + hex(bytes[2]);
                break;
            case NormalForm::RegReg:
                text += " " + reg(a) + ", " + reg(b);
                break;
            case NormalForm::BranchRel: {
                const auto rel = static_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[1]) |
                                                           (static_cast<std::uint16_t>(bytes[2]) << 8));
                text += " " + displacement(rel);
                break;
            }
            case NormalForm::PcDisp:
                text += " " + reg(a) + ", [pc" + displacement(d8) + "]";
                break;
            case NormalForm::Setcc:
                if (b > 13) {
                    return fail(bytes, 3, "setcc condition 14 or 15 is illegal");
                }
                text = std::string("set") + kConditions[b] + " " + reg(a);
                break;
            case NormalForm::Mask16: {
                const auto mask = static_cast<std::uint16_t>(bytes[1]) |
                                  (static_cast<std::uint16_t>(bytes[2]) << 8);
                text += " #" + hex(mask, true);
                break;
            }
            case NormalForm::RegOnly:
                text += " " + reg(a);
                break;
        }
        auto decoded = make(OpClass::Normal, 3, first, text);
        decoded.op = static_cast<std::uint8_t>(op);
        decoded.a = static_cast<std::uint8_t>(a);
        decoded.b = static_cast<std::uint8_t>(b);
        decoded.immediate = d8;
        if (entry.form == NormalForm::BranchRel) {
            decoded.immediate = static_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[1]) |
                                                          (static_cast<std::uint16_t>(bytes[2]) << 8));
        }
        if (entry.form == NormalForm::Mask16) {
            decoded.immediate = static_cast<std::uint16_t>(bytes[1]) |
                                (static_cast<std::uint16_t>(bytes[2]) << 8);
        }
        return decoded;
    }

    switch (first) {
        case 0xF0: {
            if (bytes.size() < 4) {
                return fail(bytes, 4, "truncated generic");
            }
            const unsigned op = bytes[1];
            if (op > 0x1E) {
                return fail(bytes, 4, "reserved generic op");
            }
            const Operand one = read_operand(bytes, 2);
            const Operand two = read_operand(bytes, 3);
            const unsigned ext1 = extension_bytes(one.mode);
            const unsigned ext2 = extension_bytes(two.mode);
            if (ext1 != 0 && ext2 != 0) {
                return fail(bytes, 4, "both generic operands need an extension");
            }
            const unsigned length = 4 + ext1 + ext2;
            if (bytes.size() < length) {
                return fail(bytes, length, "truncated generic extension");
            }
            const GenericOp& entry = kGeneric[op];
            std::string text = entry.name;
            text += " ";
            if (entry.store) {
                text += operand_text(two) + ", " + operand_text(one);
            } else if (op >= 0x14 && op <= 0x16) {  // NEG, NOT, SXTB
                text += operand_text(one);
            } else if (op == 0x1B) {  // SXTH
                text += operand_text(one);
            } else {
                text += operand_text(one) + ", " + operand_text(two);
            }
            auto decoded = make(OpClass::Generic, length, first, text);
            decoded.op = static_cast<std::uint8_t>(op);
            decoded.a = static_cast<std::uint8_t>(one.reg);
            decoded.b = static_cast<std::uint8_t>(two.reg);
            decoded.immediate = two.mode == 1 ? two.value : 0;
            decoded.op1 = Operand{static_cast<std::uint8_t>(one.mode),
                                  static_cast<std::uint8_t>(one.reg), one.value};
            decoded.op2 = Operand{static_cast<std::uint8_t>(two.mode),
                                  static_cast<std::uint8_t>(two.reg), two.value};
            return decoded;
        }
        case 0xF1: {
            if (bytes.size() < 6) {
                return fail(bytes, 6, "truncated long integer");
            }
            const unsigned op = (bytes[1] >> 4) & 0xFu;
            const unsigned r = bytes[1] & 0xFu;
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i) {
                value |= static_cast<std::uint32_t>(bytes[2 + i]) << (8 * i);
            }
            std::string text;
            switch (op) {
                case 0:
                    text = "ldbu " + reg(r) + ", [" + hex(value) + "]";
                    break;
                case 1:
                    text = "ldbs " + reg(r) + ", [" + hex(value) + "]";
                    break;
                case 2:
                    text = "stb [" + hex(value) + "], " + reg(r);
                    break;
                case 3:
                    text = "lea " + reg(r) + ", [pc" + displacement(static_cast<std::int32_t>(value)) + "]";
                    break;
                case 4:
                    text = "lea " + reg(r) + ", [sp" + displacement(static_cast<std::int32_t>(value)) + "]";
                    break;
                case 5:
                    text = "ldhu " + reg(r) + ", [" + hex(value) + "]";
                    break;
                case 6:
                    text = "sth [" + hex(value) + "], " + reg(r);
                    break;
                case 7:
                    text = "ldhs " + reg(r) + ", [" + hex(value) + "]";
                    break;
                default:
                    return fail(bytes, 6, "reserved long-integer op");
            }
            auto decoded = make(OpClass::LongInt, 6, first, text);
            decoded.op = static_cast<std::uint8_t>(op);
            decoded.a = static_cast<std::uint8_t>(r);
            decoded.immediate = value;
            return decoded;
        }
        case 0xF3: {
            if (bytes.size() < 2) {
                return fail(bytes, 2, "truncated fp register");
            }
            const unsigned op = (bytes[1] >> 4) & 0xFu;
            const unsigned fd = (bytes[1] >> 2) & 0x3u;
            const unsigned fs = bytes[1] & 0x3u;
            static const char* const names[] = {"fadd", "fsub", "fmul", "fdiv", "fneg",
                                                "fabs", "fsqrt", "fmov", "fcmp", "frnd"};
            if (op > 9) {
                return fail(bytes, 2, "reserved fp operation");
            }
            const bool unary = op == 4 || op == 5 || op == 6 || op == 9;
            std::string text = names[op];
            if (unary) {
                if (fs != 0) {
                    return fail(bytes, 2, "unary fp operation requires Fs = 0");
                }
                text += " " + freg(fd);
            } else {
                text += " " + freg(fd) + ", " + freg(fs);
            }
            auto decoded = make(OpClass::FpReg, 2, first, text);
            decoded.op = static_cast<std::uint8_t>(op);
            decoded.a = static_cast<std::uint8_t>(fd);
            decoded.b = static_cast<std::uint8_t>(fs);
            return decoded;
        }
        case 0xF4: {
            if (bytes.size() < 3) {
                return fail(bytes, 3, "truncated fp load/store");
            }
            const unsigned op = (bytes[1] >> 4) & 0xFu;
            const unsigned fd = (bytes[1] >> 2) & 0x3u;
            const unsigned mode = bytes[1] & 0x3u;
            const unsigned base = (bytes[2] >> 4) & 0xFu;
            if (op > 3) {
                return fail(bytes, 3, "reserved fp load/store op");
            }
            if ((bytes[2] & 0xFu) != 0) {
                return fail(bytes, 3, "reserved bits in fp load/store");
            }
            const bool has_displacement = mode == 1;
            const unsigned length = has_displacement ? 4 : 3;
            if (bytes.size() < length) {
                return fail(bytes, length, "truncated fp displacement");
            }
            static const char* const names[] = {"fld", "fst", "flds", "fsts"};
            std::string address_text;
            switch (mode) {
                case 0:
                    address_text = "[" + reg(base) + "]";
                    break;
                case 1:
                    address_text = "[" + reg(base) +
                                   displacement(static_cast<std::int8_t>(bytes[3])) + "]";
                    break;
                case 2:
                    address_text = "[" + reg(base) + "]+";
                    break;
                default:
                    address_text = "-[" + reg(base) + "]";
                    break;
            }
            auto decoded = make(OpClass::FpMem, length, first,
                                std::string(names[op]) + " " + freg(fd) + ", " + address_text);
            decoded.op = static_cast<std::uint8_t>(op);
            decoded.a = static_cast<std::uint8_t>(fd);
            decoded.b = static_cast<std::uint8_t>(base);
            decoded.op1.mode = static_cast<std::uint8_t>(mode);
            decoded.immediate = has_displacement ? static_cast<std::int8_t>(bytes[3]) : 0;
            return decoded;
        }
        case 0xF5: {
            if (bytes.size() < 3) {
                return fail(bytes, 3, "truncated fp convert");
            }
            const unsigned op = (bytes[1] >> 4) & 0xFu;
            const unsigned fd = (bytes[1] >> 2) & 0x3u;
            const unsigned r = (bytes[2] >> 4) & 0xFu;
            if (op > 3) {
                return fail(bytes, 3, "reserved fp convert op");
            }
            if ((bytes[1] & 0x3u) != 0 || (bytes[2] & 0xFu) != 0) {
                return fail(bytes, 3, "reserved bits in fp convert");
            }
            std::string text;
            switch (op) {
                case 0:
                    text = "i2f " + freg(fd) + ", " + reg(r);
                    break;
                case 1:
                    text = "u2f " + freg(fd) + ", " + reg(r);
                    break;
                case 2:
                    text = "f2i " + reg(r) + ", " + freg(fd);
                    break;
                default:
                    text = "f2u " + reg(r) + ", " + freg(fd);
                    break;
            }
            auto decoded = make(OpClass::FpConvert, 3, first, text);
            decoded.op = static_cast<std::uint8_t>(op);
            decoded.a = static_cast<std::uint8_t>(fd);
            decoded.b = static_cast<std::uint8_t>(r);
            return decoded;
        }
        case 0xF6: {
            if (bytes.size() < 2) {
                return fail(bytes, 2, "truncated fp push/pop");
            }
            const unsigned op = (bytes[1] >> 2) & 0x3Fu;
            const unsigned fd = bytes[1] & 0x3u;
            if (op > 1) {
                return fail(bytes, 2, "reserved fp stack op");
            }
            auto decoded = make(OpClass::FpStack, 2, first,
                                std::string(op == 0 ? "fpush " : "fpop ") + freg(fd));
            decoded.op = static_cast<std::uint8_t>(op);
            decoded.a = static_cast<std::uint8_t>(fd);
            return decoded;
        }
        case 0xF7:
        case 0xF8:
        case 0xFB: {
            const unsigned length = first == 0xF7 ? 4 : (first == 0xF8 ? 5 : 7);
            if (bytes.size() < length) {
                return fail(bytes, length, "truncated enter");
            }
            std::uint32_t frame = 0;
            unsigned mask_at = 0;
            switch (first) {
                case 0xF7:
                    frame = bytes[1];
                    mask_at = 2;
                    break;
                case 0xF8:
                    frame = static_cast<std::uint32_t>(bytes[1]) |
                            (static_cast<std::uint32_t>(bytes[2]) << 8);
                    mask_at = 3;
                    break;
                default:
                    frame = static_cast<std::uint32_t>(bytes[1]) |
                            (static_cast<std::uint32_t>(bytes[2]) << 8) |
                            (static_cast<std::uint32_t>(bytes[3]) << 16) |
                            (static_cast<std::uint32_t>(bytes[4]) << 24);
                    mask_at = 5;
                    break;
            }
            const auto mask = static_cast<std::uint16_t>(bytes[mask_at]) |
                              (static_cast<std::uint16_t>(bytes[mask_at + 1]) << 8);
            const std::string frame_text = frame < 0x1000 ? decimal(frame) : hex(frame);
            const std::string mask_text = mask == 0 ? "0" : hex(mask, true);
            auto decoded = make(OpClass::Enter, length, first,
                                "enter #" + frame_text + ", #" + mask_text);
            decoded.immediate = frame;
            decoded.address = mask;
            return decoded;
        }
        case 0xF9: {
            if (bytes.size() < 6) {
                return fail(bytes, 6, "truncated wide");
            }
            const unsigned op = (bytes[1] >> 4) & 0xFu;
            const unsigned r = bytes[1] & 0xFu;
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i) {
                value |= static_cast<std::uint32_t>(bytes[2 + i]) << (8 * i);
            }
            const auto signed_value = static_cast<std::int32_t>(value);
            std::string text;
            switch (op) {
                case 0:
                    text = "jmp " + hex(value);
                    break;
                case 1:
                    text = "call " + hex(value);
                    break;
                case 2:
                    text = "jmp [" + hex(value) + "]";
                    break;
                case 3:
                    text = "call [" + hex(value) + "]";
                    break;
                case 4:
                    text = "lea " + reg(r) + ", [pc" + displacement(signed_value) + "]";
                    break;
                case 5:
                    text = "lea " + reg(r) + ", [sp" + displacement(signed_value) + "]";
                    break;
                case 6:
                    text = "ld " + reg(r) + ", [pc" + displacement(signed_value) + "]";
                    break;
                case 7:
                    text = "ldbu " + reg(r) + ", [pc" + displacement(signed_value) + "]";
                    break;
                case 8:
                    text = "ldbs " + reg(r) + ", [pc" + displacement(signed_value) + "]";
                    break;
                case 9:
                    text = "ldhu " + reg(r) + ", [pc" + displacement(signed_value) + "]";
                    break;
                case 0xA:
                    text = "add " + reg(r) + ", #" + hex(value);
                    break;
                case 0xB:
                    text = "sub " + reg(r) + ", #" + hex(value);
                    break;
                case 0xC:
                    text = "cmp " + reg(r) + ", #" + hex(value);
                    break;
                case 0xD:
                    text = "and " + reg(r) + ", #" + hex(value);
                    break;
                case 0xE:
                    text = "or " + reg(r) + ", #" + hex(value);
                    break;
                default:
                    text = "xor " + reg(r) + ", #" + hex(value);
                    break;
            }
            auto decoded = make(OpClass::Wide, 6, first, text);
            decoded.op = static_cast<std::uint8_t>(op);
            decoded.a = static_cast<std::uint8_t>(r);
            decoded.immediate = static_cast<std::int64_t>(signed_value);
            decoded.address = value;
            return decoded;
        }
        case 0xFA: {
            if (bytes.size() < 7) {
                return fail(bytes, 7, "truncated fp load/store wide");
            }
            const unsigned op = (bytes[1] >> 4) & 0xFu;
            const unsigned fd = (bytes[1] >> 2) & 0x3u;
            const unsigned mode = bytes[1] & 0x3u;
            const unsigned base = (bytes[2] >> 4) & 0xFu;
            if (op > 3 || mode > 1) {
                return fail(bytes, 7, "reserved fp wide form");
            }
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i) {
                value |= static_cast<std::uint32_t>(bytes[3 + i]) << (8 * i);
            }
            const auto disp = static_cast<std::int32_t>(value);
            static const char* const names[] = {"fld", "fst", "flds", "fsts"};
            const std::string address_text =
                mode == 0 ? "[" + reg(base) + displacement(disp) + "]"
                          : "[pc" + displacement(disp) + "]";
            auto decoded = make(OpClass::FpMemWide, 7, first,
                                std::string(names[op]) + " " + freg(fd) + ", " + address_text);
            decoded.op = static_cast<std::uint8_t>(op);
            decoded.a = static_cast<std::uint8_t>(fd);
            decoded.b = static_cast<std::uint8_t>(base);
            decoded.op1.mode = static_cast<std::uint8_t>(mode);
            decoded.immediate = disp;
            return decoded;
        }
        default:
            return fail(bytes, 0, "reserved");
    }
}

}  // namespace kr232
