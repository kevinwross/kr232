#include "harness.h"

#include <kr232/decoder.h>
#include <kr232/machine.h>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The appendix pairs assembly text with bytes; the vectors are the contract the
// decoder must reproduce, so the test reads the document rather than a copy.
struct Vector {
    std::string assembly;
    std::vector<std::uint8_t> bytes;
};

std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string first_token(const std::string& text) {
    const auto end = text.find(' ');
    return text.substr(0, end);
}

// Returns the text between the `from`-th backtick pair (0-based) and the offset
// just after the closing backtick.
bool backticked(const std::string& line, std::size_t index, std::string& out, std::size_t& after) {
    std::size_t open = std::string::npos;
    std::size_t count = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '`') {
            continue;
        }
        if (count == index * 2) {
            open = i;
        } else if (count == index * 2 + 1 && open != std::string::npos) {
            out = line.substr(open + 1, i - open - 1);
            after = i + 1;
            return true;
        }
        ++count;
    }
    return false;
}

bool parse_bytes(const std::string& text, std::vector<std::uint8_t>& out) {
    out.clear();
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }
        if (i + 1 >= text.size() || !std::isxdigit(static_cast<unsigned char>(text[i])) ||
            !std::isxdigit(static_cast<unsigned char>(text[i + 1]))) {
            return false;
        }
        out.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
        i += 2;
    }
    return !out.empty();
}

std::vector<Vector> read_vectors() {
    std::ifstream file(KR232_ISA_PATH);
    std::vector<Vector> vectors;
    if (!file) {
        return vectors;
    }
    std::string line;
    bool in_appendix = false;
    while (std::getline(file, line)) {
        if (line.rfind("## Appendix A", 0) == 0) {
            in_appendix = true;
            continue;
        }
        if (line.rfind("## Appendix B", 0) == 0) {
            break;
        }
        if (!in_appendix || line.rfind("| `", 0) != 0) {
            continue;
        }
        Vector vector;
        std::size_t after = 0;
        if (!backticked(line, 0, vector.assembly, after)) {
            continue;
        }
        std::string bytes_text;
        if (!backticked(line, 1, bytes_text, after)) {
            continue;
        }
        if (!parse_bytes(bytes_text, vector.bytes)) {
            continue;
        }
        vectors.push_back(std::move(vector));
    }
    return vectors;
}

}  // namespace

TEST_CASE(every_appendix_a_vector_decodes_to_its_mnemonic_and_length) {
    const std::vector<Vector> vectors = read_vectors();
    CHECK(vectors.size() == 81u);
    for (const Vector& vector : vectors) {
        const kr232::Decoded decoded = kr232::decode(vector.bytes);
        if (!decoded.ok) {
            std::fprintf(stderr, "  vector '%s': %s\n", vector.assembly.c_str(),
                         decoded.text.c_str());
        }
        CHECK(decoded.ok);
        CHECK(decoded.length == vector.bytes.size());
        const std::string expected = first_token(lowered(vector.assembly));
        const std::string actual = first_token(decoded.text);
        if (expected != actual) {
            std::fprintf(stderr, "  vector '%s': decoded '%s'\n", vector.assembly.c_str(),
                         decoded.text.c_str());
        }
        CHECK(expected == actual);
    }
}

TEST_CASE(a_truncated_instruction_is_not_decoded_as_something_else) {
    const std::uint8_t short_form[] = {0x52u, 0x34u};
    const auto decoded = kr232::decode(short_form);
    CHECK(!decoded.ok);
}

TEST_CASE(reserved_bytes_are_illegal) {
    const std::uint8_t reserved[] = {0xF2u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    CHECK(!kr232::decode(reserved).ok);
    const std::uint8_t reserved_fc[] = {0xFCu};
    CHECK(!kr232::decode(reserved_fc).ok);
    const std::uint8_t reserved_7a[] = {0x7Au};
    CHECK(!kr232::decode(reserved_7a).ok);
}

TEST_CASE(a_compact_unary_operand_must_name_r0) {
    // NEG R2 is `9D 20`; `9D 21` sets Rb = R1 and is illegal.
    const std::uint8_t neg_ok[] = {0x9Du, 0x20u};
    const std::uint8_t neg_bad[] = {0x9Du, 0x21u};
    CHECK(kr232::decode(neg_ok).ok);
    CHECK(!kr232::decode(neg_bad).ok);
}

TEST_CASE(f4_is_three_bytes_before_its_displacement_and_four_after) {
    // `fst f1, [r2]` is `F4 14 20`: no displacement byte.
    const std::uint8_t no_disp[] = {0xF4u, 0x14u, 0x20u};
    const auto plain = kr232::decode(no_disp);
    CHECK(plain.ok);
    CHECK(plain.length == 3u);

    // `fsts f1, [r2+4]` is `F4 35 20 04`: mode 01 adds one byte.
    const std::uint8_t with_disp[] = {0xF4u, 0x35u, 0x20u, 0x04u};
    const auto displaced = kr232::decode(with_disp);
    CHECK(displaced.ok);
    CHECK(displaced.length == 4u);
}

TEST_CASE(a_generic_form_never_carries_two_extensions) {
    // MOV R2, #imm16 is legal (mode 1); an address in operand 2 with an
    // immediate in operand 1 is not encodable because operand 1 must be a
    // register, so the illegal shape is two extension-taking modes.
    const std::uint8_t generic_ok[] = {0xF0u, 0x00u, 0x02u, 0x10u, 0x00u, 0x40u};
    const auto ok = kr232::decode(generic_ok);
    CHECK(ok.ok);
    CHECK(ok.length == 6u);

    const std::uint8_t generic_bad[] = {0xF0u, 0x00u, 0x15u, 0x15u, 0x00u, 0x00u};
    CHECK(!kr232::decode(generic_bad).ok);
}

namespace {

// The effect a vector must have, chosen per mnemonic. A value is asserted only
// where the Appendix fixes one; where the value would mean reimplementing the
// executor inside its own test, the shape is asserted and the twelve targeted
// cases in test_machine.cpp carry the semantics.
enum class Shape {
    None,
    Push,
    Pop,
    Call,
    Ret,
    Branch,
    Trap1,
    Trap9,
    Halt,
    SysExit,
    Alu,
    Load,
    Store,
    Enter,
    Leave,
    Iret,
    Fp,
    FpLoad,
    FpStore,
    FpStack,
    Unclassified,
};

Shape shape_of(const std::string& mnemonic) {
    if (mnemonic == "nop") return Shape::None;
    if (mnemonic == "push") return Shape::Push;
    if (mnemonic == "pop") return Shape::Pop;
    if (mnemonic == "call") return Shape::Call;
    if (mnemonic == "ret") return Shape::Ret;
    if (mnemonic == "brk") return Shape::Trap1;
    if (mnemonic == "syscall") return Shape::Trap9;
    if (mnemonic == "halt") return Shape::Halt;
    if (mnemonic == "sys") return Shape::SysExit;
    if (mnemonic == "iret") return Shape::Iret;
    if (mnemonic == "enter") return Shape::Enter;
    if (mnemonic == "leave") return Shape::Leave;
    if (mnemonic == "jmp" || mnemonic == "bra" || mnemonic == "beq") return Shape::Branch;
    if (mnemonic == "ld" || mnemonic == "ldbu" || mnemonic == "ldbs" || mnemonic == "ldhu" ||
        mnemonic == "ldhs") {
        return Shape::Load;
    }
    if (mnemonic == "st" || mnemonic == "stb" || mnemonic == "sth") return Shape::Store;
    if (mnemonic == "fld" || mnemonic == "flds") return Shape::FpLoad;
    if (mnemonic == "fst" || mnemonic == "fsts") return Shape::FpStore;
    if (mnemonic == "fpush" || mnemonic == "fpop") return Shape::FpStack;
    if (mnemonic == "fadd" || mnemonic == "fsub" || mnemonic == "fmul" || mnemonic == "fdiv" ||
        mnemonic == "fneg" || mnemonic == "fabs" || mnemonic == "fsqrt" || mnemonic == "fmov" ||
        mnemonic == "fcmp" || mnemonic == "frnd") {
        return Shape::Fp;
    }
    if (mnemonic == "mov" || mnemonic == "add" || mnemonic == "addc" || mnemonic == "sub" ||
        mnemonic == "subb" || mnemonic == "and" || mnemonic == "or" || mnemonic == "xor" ||
        mnemonic == "cmp" || mnemonic == "mul" || mnemonic == "mulh" || mnemonic == "shl" ||
        mnemonic == "shr" || mnemonic == "sar" || mnemonic == "div" || mnemonic == "divs" ||
        mnemonic == "neg" || mnemonic == "not" || mnemonic == "sxtb" || mnemonic == "sxth" ||
        mnemonic == "setlt" || mnemonic == "lea" || mnemonic == "inc" || mnemonic == "dec" ||
        mnemonic == "rdcycle" || mnemonic == "ei" || mnemonic == "di" || mnemonic == "i2f" ||
        mnemonic == "u2f" || mnemonic == "f2i" || mnemonic == "f2u") {
        return Shape::Alu;
    }
    return Shape::Unclassified;
}

struct Snapshot {
    std::uint32_t regs[16] = {};
    double f[4] = {};
    std::uint32_t memory[6] = {};  // words at 0x2000, 0x2004, 0x4000, 0x1040, 0x2008, 0x200c
};

Snapshot snapshot_of(const kr232::Machine& machine) {
    Snapshot shot;
    for (unsigned i = 0; i < 16; ++i) {
        shot.regs[i] = machine.reg(i);
    }
    for (unsigned i = 0; i < 4; ++i) {
        shot.f[i] = machine.freg(i);
    }
    (void)machine.memory().read32(0x2000, shot.memory[0]);
    (void)machine.memory().read32(0x2004, shot.memory[1]);
    (void)machine.memory().read32(0x4000, shot.memory[2]);
    (void)machine.memory().read32(0x1044, shot.memory[3]);
    (void)machine.memory().read32(0x2008, shot.memory[4]);
    (void)machine.memory().read32(0x200C, shot.memory[5]);
    return shot;
}

bool registers_changed(const Snapshot& before, const Snapshot& after) {
    for (unsigned i = 0; i < 16; ++i) {
        if (i == 14) {
            continue;  // PC moves by design on every retired instruction
        }
        if (before.regs[i] != after.regs[i]) {
            return true;
        }
    }
    return false;
}

bool floats_changed(const Snapshot& before, const Snapshot& after) {
    for (unsigned i = 0; i < 4; ++i) {
        if (before.f[i] != after.f[i]) {
            return true;
        }
    }
    return false;
}

bool memory_changed(const Snapshot& before, const Snapshot& after) {
    for (unsigned i = 0; i < 6; ++i) {
        if (before.memory[i] != after.memory[i]) {
            return true;
        }
    }
    return false;
}

// The branch target, computed from the bytes independently of the decoder.
// 0xFFFFFFFF means "a register form": any PC other than the fall-through will do.
std::uint32_t branch_target(const Vector& vector) {
    const std::vector<std::uint8_t>& bytes = vector.bytes;
    if (bytes[0] == 0xF9u && bytes.size() >= 6) {
        const unsigned wide_op = (bytes[1] >> 4) & 0xFu;
        if (wide_op == 2 || wide_op == 3) {
            return 0xFFFFFFFFu;  // indirect: the target is the word at the address
        }
        return static_cast<std::uint32_t>(bytes[2]) |
               (static_cast<std::uint32_t>(bytes[3]) << 8) |
               (static_cast<std::uint32_t>(bytes[4]) << 16) |
               (static_cast<std::uint32_t>(bytes[5]) << 24);
    }
    if ((bytes[0] == 0xD0u || bytes[0] == 0xCFu) && bytes.size() >= 3) {
        const auto rel = static_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[1]) |
                                                   (static_cast<std::uint16_t>(bytes[2]) << 8));
        return 0x1000u + 3u + static_cast<std::uint32_t>(rel);
    }
    if (bytes[0] >= 0xA0u && bytes[0] <= 0xBFu && bytes.size() >= 2) {
        int disp = (static_cast<int>(bytes[0] & 1u) << 8) | bytes[1];
        if ((disp & 0x100) != 0) {  // bit 8 is the sign of a nine-bit displacement
            disp -= 0x200;
        }
        return 0x1000u + 2u + static_cast<std::uint32_t>(disp);
    }
    return 0xFFFFFFFFu;
}

}  // namespace

TEST_CASE(every_vector_executes_with_the_effect_its_form_requires) {
    for (const Vector& vector : read_vectors()) {
        kr232::Machine machine(0x10000);
        machine.set_sp(0x8000);
        for (unsigned index = 2; index <= 10; ++index) {
            machine.set_reg(index, 0x2000u + 4u * (index - 2));
        }
        machine.set_reg(7, 0);  // SYS exit
        machine.set_reg(12, 0x6000);
        machine.set_reg(13, 0x7000);
        machine.set_freg(0, 1.5);
        machine.set_freg(1, 2.5);
        machine.set_freg(2, 3.5);
        machine.set_freg(3, 0.5);
        (void)machine.memory().write32(0x8000, 0x4000);                    // RET target
        (void)machine.memory().write32(0x7000, 0x00000000);                // saved FP for LEAVE
        (void)machine.memory().write32(0x6000, 0x00000000);                // IRET: saved EFP
        (void)machine.memory().write32(0x6004, 0x00004000);                // IRET: PC
        (void)machine.memory().write32(0x6008, 1u << 6);                   // IRET: flags, K = 1
        (void)machine.memory().write32(0x600C, 0x00008000);                // IRET: SP
        std::uint32_t at = 0x1000;
        for (const std::uint8_t byte : vector.bytes) {
            (void)machine.memory().write8(at++, byte);
        }
        machine.set_pc(0x1000);

        const Snapshot before = snapshot_of(machine);
        const kr232::StepResult result = machine.step();
        const Snapshot after = snapshot_of(machine);
        const std::string mnemonic = first_token(lowered(vector.assembly));
        const Shape shape = shape_of(mnemonic);
        const std::uint32_t advance = 0x1000u + static_cast<std::uint32_t>(vector.bytes.size());
        const std::uint32_t target = branch_target(vector);

        bool ok = false;
        switch (shape) {
            case Shape::None:
                ok = result.stop == kr232::Stop::Retired && machine.pc() == advance &&
                     !registers_changed(before, after);
                break;
            case Shape::Push:
                ok = result.stop == kr232::Stop::Retired && machine.sp() < before.regs[15];
                break;
            case Shape::Pop:
                ok = result.stop == kr232::Stop::Retired && machine.sp() > before.regs[15];
                break;
            case Shape::Call:
                ok = result.stop == kr232::Stop::Retired &&
                     machine.sp() == before.regs[15] - 4 && machine.pc() != advance;
                break;
            case Shape::Ret:
                ok = result.stop == kr232::Stop::Retired && machine.pc() == 0x4000;
                break;
            case Shape::Branch:
                ok = result.stop == kr232::Stop::Retired &&
                     (machine.pc() == advance || machine.pc() == target ||
                      (target == 0xFFFFFFFFu && machine.pc() != advance));
                break;
            case Shape::Trap1:
                ok = result.stop == kr232::Stop::Trapped && result.vector == 1;
                break;
            case Shape::Trap9:
                ok = result.stop == kr232::Stop::Trapped && result.vector == 9;
                break;
            case Shape::Halt:
            case Shape::SysExit:
                ok = result.stop == kr232::Stop::Halted;
                break;
            case Shape::Iret:
                ok = result.stop == kr232::Stop::Retired && machine.pc() == 0x4000;
                break;
            case Shape::Enter:
                ok = result.stop == kr232::Stop::Retired && machine.sp() < before.regs[15];
                break;
            case Shape::Leave:
                ok = result.stop == kr232::Stop::Retired && machine.sp() == before.regs[13] + 4;
                break;
            case Shape::Alu:
                // Several ALU results can equal their input for the canonical
                // operands (`and r2,#0xff00` on r2=0x2000, `sxth` of 0x2000), and
                // `ei`/`di` may leave the I bit as it was. The shape therefore
                // asserts retirement and the fall-through; the value cases in
                // test_machine.cpp carry the arithmetic.
                ok = result.stop == kr232::Stop::Retired && machine.pc() == advance;
                break;
            case Shape::Load:
                ok = result.stop == kr232::Stop::Retired && machine.pc() == advance &&
                     registers_changed(before, after);
                break;
            case Shape::Store:
                ok = result.stop == kr232::Stop::Retired && machine.pc() == advance &&
                     (memory_changed(before, after) || registers_changed(before, after));
                break;
            case Shape::Fp:
            case Shape::FpLoad:
                // A load or an arithmetic operation changes a register; fcmp
                // writes flags instead; and frnd of an exactly representable
                // value (2.5) legitimately changes nothing.
                ok = result.stop == kr232::Stop::Retired &&
                     (floats_changed(before, after) || before.regs[11] != after.regs[11] ||
                      mnemonic == "frnd");
                break;
            case Shape::FpStore:
                ok = result.stop == kr232::Stop::Retired && memory_changed(before, after);
                break;
            case Shape::FpStack:
                ok = result.stop == kr232::Stop::Retired && machine.sp() != before.regs[15];
                break;
            case Shape::Unclassified:
                ok = false;
                break;
        }
        if (!ok) {
            std::fprintf(stderr, "  vector '%s' (mnemonic '%s', shape %u) stop=%u vector=%u pc=0x%x\n",
                         vector.assembly.c_str(), mnemonic.c_str(), static_cast<unsigned>(shape),
                         static_cast<unsigned>(result.stop), result.vector, machine.pc());
        }
        CHECK(ok);
    }
}
