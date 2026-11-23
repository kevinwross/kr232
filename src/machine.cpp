#include <kr232/machine.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace kr232 {
namespace {

constexpr unsigned kZ = 0;
constexpr unsigned kN = 1;
constexpr unsigned kC = 2;
constexpr unsigned kV = 3;
constexpr unsigned kI = 4;
constexpr unsigned kK = 6;

constexpr std::uint32_t kHardwareFlags = (1u << kI) | (1u << kK);

bool condition_holds(unsigned cond, std::uint32_t flags) {
    const bool z = ((flags >> kZ) & 1u) != 0;
    const bool n = ((flags >> kN) & 1u) != 0;
    const bool c = ((flags >> kC) & 1u) != 0;
    const bool v = ((flags >> kV) & 1u) != 0;
    switch (cond) {
        case 0:
            return z;
        case 1:
            return !z;
        case 2:
            return c;
        case 3:
            return !c;
        case 4:
            return n;
        case 5:
            return !n;
        case 6:
            return v;
        case 7:
            return !v;
        case 8:
            return c && !z;
        case 9:
            return !c || z;
        case 10:
            return n == v;
        case 11:
            return n != v;
        case 12:
            return !z && n == v;
        case 13:
            return z || n != v;
        default:
            return false;
    }
}

int popcount16(std::uint32_t mask) {
    int count = 0;
    for (unsigned bit = 0; bit < 16; ++bit) {
        if ((mask >> bit) & 1u) {
            ++count;
        }
    }
    return count;
}

std::uint64_t double_bits(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

double bits_double(std::uint64_t bits) {
    double value = 0;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// Ops 14-28 of the compact register form: load or store, the operand size, sign
// extension, and the auto-increment step (0 plain, +1 post, -1 pre).
struct CompactMem {
    bool load;
    bool sign;
    unsigned size;
    int step;
};

constexpr CompactMem kCompactMem[15] = {
    {true, false, 4, 0},  {false, false, 4, 0}, {true, false, 1, 0}, {true, true, 1, 0},
    {false, false, 1, 0}, {true, false, 4, 1},  {false, false, 4, 1}, {true, false, 1, 1},
    {true, true, 1, 1},   {false, false, 1, 1}, {true, false, 4, -1}, {false, false, 4, -1},
    {true, false, 1, -1}, {true, true, 1, -1},  {false, false, 1, -1},
};

}  // namespace

Machine::Machine(std::uint32_t ram_bytes) : memory_(ram_bytes) { reset(); }

void Machine::reset() {
    std::fill(std::begin(regs_), std::end(regs_), 0u);
    std::fill(std::begin(f_), std::end(f_), 0.0);
    regs_[kSp] = kResetSp;
    regs_[kFlags] = 1u << kK;
    cycles_ = 0;
    halted_ = false;
    exit_status_ = 0;
    std::uint32_t entry = kResetWord + 4;
    if (memory_.read32(kResetWord, entry) != Access::Ok) {
        entry = kResetWord + 4;  // an unloaded machine starts at the vector table
    }
    regs_[kPc] = entry;
}

void Machine::set_reg(unsigned index, std::uint32_t value) noexcept {
    if ((index & 0xFu) == kFlags) {
        value = (value & ~kHardwareFlags) | (regs_[kFlags] & kHardwareFlags);
    }
    regs_[index & 0xFu] = value;
}

void Machine::set_freg(unsigned index, double value) noexcept { f_[index & 0x3u] = value; }

void Machine::set_flag(unsigned bit, bool value) noexcept {
    if (value) {
        regs_[kFlags] |= (1u << bit);
    } else {
        regs_[kFlags] &= ~(1u << bit);
    }
}

void Machine::set_nz(std::uint32_t value) noexcept {
    set_flag(kZ, value == 0);
    set_flag(kN, (value & 0x80000000u) != 0);
}

unsigned Machine::fetch(std::uint8_t* out, std::uint32_t address) const {
    unsigned count = 0;
    for (; count < 8; ++count) {
        std::uint8_t byte = 0;
        if (memory_.read8(address + count, byte) != Access::Ok) {
            break;
        }
        out[count] = byte;
    }
    return count;
}

StepResult Machine::enter_trap(unsigned vector) {
    // ISA §11.3. The frame records the flags as they were, so IRET can restore K
    // and I; a trap taken with K = 0 lands on the kernel stack, never the user's,
    // and records the interrupted SP for IRET.
    const std::uint32_t saved_sp = regs_[kSp];
    if (!kernel_mode()) {
        regs_[kSp] = kResetSp;
    }
    regs_[kSp] -= 16;
    if (memory_.write32(regs_[kSp], regs_[kEfp]) != Access::Ok ||
        memory_.write32(regs_[kSp] + 4, regs_[kPc]) != Access::Ok ||
        memory_.write32(regs_[kSp] + 8, regs_[kFlags]) != Access::Ok ||
        memory_.write32(regs_[kSp] + 12, saved_sp) != Access::Ok) {
        halted_ = true;
        return StepResult{Stop::Halted, 0, 0, false};
    }
    regs_[kEfp] = regs_[kSp];
    set_flag(kK, true);
    set_flag(kI, false);
    std::uint32_t handler = 0;
    if (memory_.read32(kResetWord + 4 * vector, handler) != Access::Ok) {
        halted_ = true;
        return StepResult{Stop::Halted, 0, 0, false};
    }
    regs_[kPc] = handler;
    StepResult result;
    // A vector whose handler word is zero is unhandled: there is nothing to run,
    // so the trap is the stop. A vector with a handler continues into it, because
    // that is what a kernel is for; the result still reports the vector so a
    // caller that wants to log traps can see it.
    result.stop = handler == 0 ? Stop::Trapped : Stop::Retired;
    result.vector = vector;
    result.jumped = true;
    return result;
}

StepResult Machine::access_trap() {
    const unsigned vector = access_vector_ != 0 ? access_vector_ : 4;
    access_vector_ = 0;
    return enter_trap(vector);
}

StepResult Machine::step() {
    if (halted_) {
        return StepResult{Stop::Halted, 0, exit_status_, false};
    }
    std::uint8_t bytes[8] = {};
    if (!kernel_mode() && (regs_[kPc] < 0x08000000u || regs_[kPc] >= 0xFC000000u)) {
        return enter_trap(8);  // a user-mode fetch outside user RAM
    }
    const unsigned available = fetch(bytes, regs_[kPc]);
    if (available == 0) {
        return access_trap();
    }
    const Decoded decoded = decode(std::span<const std::uint8_t>(bytes, available));
    if (!decoded.ok) {
        return enter_trap(2);
    }
    const StepResult result = execute(decoded);
    if (result.stop == Stop::Retired) {
        if (!result.jumped) {
            regs_[kPc] += decoded.length;
        }
        ++cycles_;
        devices_.tick(cycles_);
        if (devices_.graphics_pending()) {
            devices_.run_graphics(memory_);
        }
        if (devices_.blitter_pending()) {
            devices_.run_blitter(memory_);
        }
        if (devices_.reset_requested()) {
            devices_.clear_requests();
            reset();
        } else if (devices_.halt_requested()) {
            devices_.clear_requests();
            halted_ = true;
            exit_status_ = 0;
        }
    }
    return result;
}

StepResult Machine::execute(const Decoded& d) {
    const StepResult retired{};

    const auto nz = [this](std::uint32_t value) { set_nz(value); };
    const auto logic = [&nz, this](std::uint32_t value) {
        nz(value);
        set_flag(kC, false);
        set_flag(kV, false);
        return value;
    };
    const auto add = [&nz, this](std::uint32_t a, std::uint32_t b, std::uint32_t cin) {
        const std::uint64_t sum = static_cast<std::uint64_t>(a) + b + cin;
        const auto r = static_cast<std::uint32_t>(sum);
        nz(r);
        set_flag(kC, (sum >> 32) != 0);
        set_flag(kV, ((~(a ^ b) & (a ^ r)) >> 31) != 0);
        return r;
    };
    const auto sub = [&nz, this](std::uint32_t a, std::uint32_t b, std::uint32_t bin) {
        const std::uint64_t wide_b = static_cast<std::uint64_t>(b) + bin;
        const auto r = static_cast<std::uint32_t>(static_cast<std::uint64_t>(a) - wide_b);
        nz(r);
        set_flag(kC, static_cast<std::uint64_t>(a) >= wide_b);
        set_flag(kV, (((a ^ b) & (a ^ r)) >> 31) != 0);
        return r;
    };
    const auto shift = [&nz, this](unsigned op, std::uint32_t a, std::uint32_t count) {
        const unsigned n = count & 0x1Fu;
        if (n == 0) {
            set_flag(kC, false);
            set_flag(kV, false);
            nz(a);
            return a;
        }
        std::uint32_t r = 0;
        switch (op) {
            case 11:  // SHL
                r = a << n;
                set_flag(kC, ((a >> (32 - n)) & 1u) != 0);
                break;
            case 12:  // SHR
                r = a >> n;
                set_flag(kC, ((a >> (n - 1)) & 1u) != 0);
                break;
            default:  // SAR
                r = static_cast<std::uint32_t>(static_cast<std::int32_t>(a) >> n);
                set_flag(kC, ((a >> (n - 1)) & 1u) != 0);
                break;
        }
        set_flag(kV, false);
        nz(r);
        return r;
    };

    const auto load = [this](unsigned size, std::uint32_t address, std::uint32_t& out) {
        access_vector_ = 4;  // an allowed access that fails is an address error
        if (!kernel_mode() &&
            (address < 0x08000000u || address >= 0xFC000000u || address + size > 0xFC000000u)) {
            access_vector_ = 8;  // protection is checked before the address (§11.2)
            return false;
        }
        if (Devices::in_aperture(address)) {
            std::uint32_t word = 0;
            if (!devices_.aperture_read(address, size, word)) {
                return false;  // past 32 MiB: an address error
            }
            out = word;
            return true;
        }
        if (Devices::contains(address)) {
            std::uint32_t word = 0;
            if (devices_.read(address, size, word) != DeviceResult::Ok) {
                return false;  // an undefined offset or a bad shape is an address error
            }
            out = word;
            return true;
        }
        switch (size) {
            case 1: {
                std::uint8_t v = 0;
                if (memory_.read8(address, v) != Access::Ok) {
                    return false;
                }
                out = v;
                return true;
            }            case 2: {
                std::uint16_t v = 0;
                if (memory_.read16(address, v) != Access::Ok) {
                    return false;
                }
                out = v;
                return true;
            }
            default: {
                std::uint32_t v = 0;
                if (memory_.read32(address, v) != Access::Ok) {
                    return false;
                }
                out = v;
                return true;
            }
        }
    };
    const auto store = [this](unsigned size, std::uint32_t address, std::uint32_t value) {
        access_vector_ = 4;
        if (!kernel_mode() &&
            (address < 0x08000000u || address >= 0xFC000000u || address + size > 0xFC000000u)) {
            access_vector_ = 8;
            return false;
        }
        if (Devices::in_aperture(address)) {
            return devices_.aperture_write(address, size, value);
        }
        if (Devices::contains(address)) {
            return devices_.write(address, size, value) == DeviceResult::Ok;
        }
        switch (size) {
            case 1:
                return memory_.write8(address, static_cast<std::uint8_t>(value)) == Access::Ok;
            case 2:
                return memory_.write16(address, static_cast<std::uint16_t>(value)) == Access::Ok;
            default:
                return memory_.write32(address, value) == Access::Ok;
        }
    };
    const auto push = [this, &store](std::uint32_t value) {
        regs_[kSp] -= 4;
        return store(4, regs_[kSp], value);
    };
    const auto pop = [this, &load](std::uint32_t& value) {
        if (!load(4, regs_[kSp], value)) {
            return false;
        }
        regs_[kSp] += 4;
        return true;
    };

    switch (d.kind) {
        case OpClass::Short: {
            // EI, DI, HALT, SYS and IRET are kernel mode only (ISA §11.1);
            // SYSCALL at 0x79 is how user mode enters the kernel.
            if (d.opcode >= 0x73 && d.opcode <= 0x77 && !kernel_mode()) {
                return enter_trap(8);
            }
            const unsigned index = d.opcode & 0xFu;
            if (d.opcode <= 0x0F) {
                if (!push(regs_[index])) {
                    return access_trap();
                }
                return retired;
            }
            if (d.opcode <= 0x1F) {
                std::uint32_t value = 0;
                if (!pop(value)) {
                    return access_trap();
                }
                set_reg(index, value);
                return retired;
            }
            if (d.opcode <= 0x2F) {
                const std::uint32_t value = regs_[index] + 1;
                set_reg(index, value);
                nz(value);
                return retired;
            }
            if (d.opcode <= 0x3F) {
                const std::uint32_t value = regs_[index] - 1;
                set_reg(index, value);
                nz(value);
                return retired;
            }
            if (d.opcode <= 0x4F) {
                if (!push(regs_[kPc] + 1)) {
                    return access_trap();
                }
                set_pc(regs_[index]);
                return StepResult{Stop::Retired, 0, 0, true};
            }
            switch (d.opcode) {
                case 0x70: {  // RET
                    std::uint32_t target = 0;
                    if (!pop(target)) {
                        return access_trap();
                    }
                    set_pc(target);
                    return StepResult{Stop::Retired, 0, 0, true};
                }
                case 0x71:  // NOP
                    return retired;
                case 0x72:  // BRK
                    return enter_trap(1);
                case 0x73: {  // SYS
                    if (regs_[7] == 0) {
                        halted_ = true;
                        exit_status_ = static_cast<std::uint8_t>(regs_[2] & 0xFFu);
                        return StepResult{Stop::Halted, 0, exit_status_, false};
                    }
                    return enter_trap(3);
                }
                case 0x74:
                    set_flag(kI, true);
                    return retired;
                case 0x75:
                    set_flag(kI, false);
                    return retired;
                case 0x76:
                    halted_ = true;
                    return StepResult{Stop::Halted, 0, 0, false};
                case 0x77: {  // IRET
                    const std::uint32_t efp = regs_[kEfp];
                    std::uint32_t new_efp = 0;
                    std::uint32_t frame_pc = 0;
                    std::uint32_t frame_flags = 0;
                    std::uint32_t frame_sp = 0;
                    if (memory_.read32(efp, new_efp) != Access::Ok ||
                        memory_.read32(efp + 4, frame_pc) != Access::Ok ||
                        memory_.read32(efp + 8, frame_flags) != Access::Ok ||
                        memory_.read32(efp + 12, frame_sp) != Access::Ok) {
                        return access_trap();
                    }
                    regs_[kEfp] = new_efp;
                    regs_[kPc] = frame_pc;
                    regs_[kFlags] = frame_flags;
                    regs_[kSp] = frame_sp;
                    return StepResult{Stop::Retired, 0, 0, true};
                }
                case 0x78:
                    set_reg(2, static_cast<std::uint32_t>(cycles_ & 0xFFFFFFFFu));
                    return retired;
                case 0x79:  // SYSCALL: the saved PC is the next instruction
                    regs_[kPc] += d.length;
                    return enter_trap(9);
                default:
                    return enter_trap(2);
            }
        }
        case OpClass::Register: {
            const unsigned top = d.opcode & 0xF0u;
            if (top == 0x50) {
                set_reg(d.a, static_cast<std::uint32_t>(d.immediate));
                return retired;
            }
            if (top == 0x60) {
                std::uint32_t value = 0;
                if (!load(4, d.address, value)) {
                    return access_trap();
                }
                set_reg(d.a, value);
                return retired;
            }
            if (!store(4, d.address, regs_[d.a])) {
                return access_trap();
            }
            return retired;
        }
        case OpClass::Compact: {
            const unsigned a = d.a;
            const unsigned b = d.b;
            if (d.op == 0) {
                set_reg(a, regs_[b]);
                return retired;
            }
            if (d.op >= 1 && d.op <= 8) {
                const std::uint32_t left = regs_[a];
                const std::uint32_t right = regs_[b];
                switch (d.op) {
                    case 1:
                        set_reg(a, add(left, right, 0));
                        break;
                    case 2:
                        set_reg(a, add(left, right, flag(kC) ? 1u : 0u));
                        break;
                    case 3:
                        set_reg(a, sub(left, right, 0));
                        break;
                    case 4:
                        set_reg(a, sub(left, right, flag(kC) ? 1u : 0u));
                        break;
                    case 5:
                        set_reg(a, logic(left & right));
                        break;
                    case 6:
                        set_reg(a, logic(left | right));
                        break;
                    case 7:
                        set_reg(a, logic(left ^ right));
                        break;
                    default:
                        sub(left, right, 0);
                        break;
                }
                return retired;
            }
            if (d.op >= 9 && d.op <= 13) {
                const std::uint32_t left = regs_[a];
                const std::uint32_t right = regs_[b];
                switch (d.op) {
                    case 9: {
                        const std::uint32_t r = left * right;
                        set_reg(a, r);
                        nz(r);
                        break;
                    }
                    case 10: {
                        const std::uint64_t r = static_cast<std::uint64_t>(left) * right;
                        set_reg(a, static_cast<std::uint32_t>(r >> 32));
                        nz(static_cast<std::uint32_t>(r >> 32));
                        break;
                    }
                    default:
                        set_reg(a, shift(d.op, left, right));
                        break;
                }
                return retired;
            }
            if (d.op >= 14 && d.op <= 28) {
                const CompactMem& spec = kCompactMem[d.op - 14];
                std::uint32_t address = regs_[b];
                if (spec.step < 0) {
                    address -= spec.size;
                }
                std::uint32_t value = 0;
                if (spec.load) {
                    if (!load(spec.size, address, value)) {
                        return access_trap();
                    }
                    if (spec.sign) {
                        if (spec.size == 1) {
                            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(
                                static_cast<std::int8_t>(value & 0xFFu)));
                        }
                    }
                    set_reg(a, value);
                } else {
                    if (!store(spec.size, address, regs_[a])) {
                        return access_trap();
                    }
                }
                if (spec.step != 0) {
                    set_reg(b, address + spec.size);
                }
                return retired;
            }
            switch (d.op) {
                case 29: {  // NEG
                    const std::uint32_t r = sub(0, regs_[a], 0);
                    set_reg(a, r);
                    return retired;
                }
                case 30: {  // NOT
                    const std::uint32_t r = ~regs_[a];
                    set_reg(a, r);
                    nz(r);
                    set_flag(kC, false);
                    set_flag(kV, false);
                    return retired;
                }
                default:  // JMP
                    set_pc(regs_[a]);
                    return StepResult{Stop::Retired, 0, 0, true};
            }
        }
        case OpClass::CompactBranch: {
            const std::uint32_t target = regs_[kPc] + 2 + static_cast<std::uint32_t>(d.immediate);
            if (d.op <= 13) {
                if (condition_holds(d.op, regs_[kFlags])) {
                    set_pc(target);
                    return StepResult{Stop::Retired, 0, 0, true};
                }
                return retired;
            }
            if (d.op == 14) {
                if (!push(regs_[kPc] + 2)) {
                    return access_trap();
                }
            }
            set_pc(target);
            return StepResult{Stop::Retired, 0, 0, true};
        }
        case OpClass::Normal: {
            const unsigned op = d.op;
            const unsigned a = d.a;
            const unsigned b = d.b;
            const auto d8 = static_cast<std::int32_t>(static_cast<std::int8_t>(d.immediate));
            const std::uint32_t address = regs_[b] + static_cast<std::uint32_t>(d8);
            const std::uint32_t target = regs_[kPc] + 3 + static_cast<std::uint32_t>(d.immediate);
            switch (op) {
                case 0: {  // LD Rd,[Rb+d8]
                    std::uint32_t value = 0;
                    if (!load(4, address, value)) {
                        return access_trap();
                    }
                    set_reg(a, value);
                    return retired;
                }
                case 1:  // ST
                    if (!store(4, address, regs_[a])) {
                        return access_trap();
                    }
                    return retired;
                case 2: {  // LDBU
                    std::uint32_t value = 0;
                    if (!load(1, address, value)) {
                        return access_trap();
                    }
                    set_reg(a, value);
                    return retired;
                }
                case 3: {  // LDBS
                    std::uint32_t value = 0;
                    if (!load(1, address, value)) {
                        return access_trap();
                    }
                    set_reg(a, static_cast<std::uint32_t>(
                                    static_cast<std::int32_t>(static_cast<std::int8_t>(value & 0xFFu))));
                    return retired;
                }
                case 4:  // STB
                    if (!store(1, address, regs_[a])) {
                        return access_trap();
                    }
                    return retired;
                case 5:  // LD Rd,#s8
                    set_reg(a, static_cast<std::uint32_t>(d8));
                    return retired;
                case 6:  // LD Rd,#u8
                    set_reg(a, static_cast<std::uint32_t>(d.immediate) & 0xFFu);
                    return retired;
                case 7:  // ADD
                    set_reg(a, add(regs_[a], static_cast<std::uint32_t>(d8), 0));
                    return retired;
                case 8:  // SUB
                    set_reg(a, sub(regs_[a], static_cast<std::uint32_t>(d8), 0));
                    return retired;
                case 9:  // CMP Rd,#s8
                    sub(regs_[a], static_cast<std::uint32_t>(d8), 0);
                    return retired;
                case 10:  // CMP Rd,#u8
                    sub(regs_[a], static_cast<std::uint32_t>(d.immediate) & 0xFFu, 0);
                    return retired;
                case 11:  // AND
                    set_reg(a, logic(regs_[a] & (static_cast<std::uint32_t>(d.immediate) & 0xFFu)));
                    return retired;
                case 12:  // OR
                    set_reg(a, logic(regs_[a] | (static_cast<std::uint32_t>(d.immediate) & 0xFFu)));
                    return retired;
                case 13:  // XOR
                    set_reg(a, logic(regs_[a] ^ (static_cast<std::uint32_t>(d.immediate) & 0xFFu)));
                    return retired;
                case 14:  // LEA
                    set_reg(a, address);
                    return retired;
                case 15:  // JMP rel16
                    set_pc(target);
                    return StepResult{Stop::Retired, 0, 0, true};
                case 16:  // CALL rel16
                    if (!push(regs_[kPc] + 3)) {
                        return access_trap();
                    }
                    set_pc(target);
                    return StepResult{Stop::Retired, 0, 0, true};
                case 17: {  // LDBU Rd,[PC+d8]
                    std::uint32_t value = 0;
                    if (!load(1, regs_[kPc] + static_cast<std::uint32_t>(d8), value)) {
                        return access_trap();
                    }
                    set_reg(a, value);
                    return retired;
                }
                case 18: {  // LDBS Rd,[PC+d8]
                    std::uint32_t value = 0;
                    if (!load(1, regs_[kPc] + static_cast<std::uint32_t>(d8), value)) {
                        return access_trap();
                    }
                    set_reg(a, static_cast<std::uint32_t>(
                                    static_cast<std::int32_t>(static_cast<std::int8_t>(value & 0xFFu))));
                    return retired;
                }
                case 19: {  // SETcc
                    const bool set = condition_holds(b, regs_[kFlags]);
                    set_reg(a, set ? 1u : 0u);
                    return retired;
                }
                case 20:
                case 21:
                case 22: {  // shifts by immediate
                    const std::uint32_t count = static_cast<std::uint32_t>(d.immediate) & 0xFFu;
                    set_reg(a, shift(op, regs_[a], count));
                    return retired;
                }
                case 23:  // PUSH #mask
                    for (unsigned bit = 0; bit < 16; ++bit) {
                        if ((static_cast<std::uint32_t>(d.immediate) >> bit) & 1u) {
                            if (!push(regs_[bit])) {
                                return access_trap();
                            }
                        }
                    }
                    return retired;
                case 24:  // POP #mask
                    for (unsigned bit = 16; bit-- > 0;) {
                        if ((static_cast<std::uint32_t>(d.immediate) >> bit) & 1u) {
                            std::uint32_t value = 0;
                            if (!pop(value)) {
                                return access_trap();
                            }
                            set_reg(bit, value);
                        }
                    }
                    return retired;
                case 25:
                case 26: {  // DIV / DIVS
                    const std::uint32_t divisor = regs_[b];
                    if (divisor == 0) {
                        return enter_trap(3);
                    }
                    std::uint32_t r = 0;
                    if (op == 25) {
                        r = regs_[a] / divisor;
                    } else {
                        const auto left = static_cast<std::int32_t>(regs_[a]);
                        const auto right = static_cast<std::int32_t>(divisor);
                        if (left == INT32_MIN && right == -1) {
                            return enter_trap(3);
                        }
                        r = static_cast<std::uint32_t>(left / right);
                    }
                    set_reg(a, r);
                    nz(r);
                    set_flag(kC, false);
                    set_flag(kV, false);
                    return retired;
                }
                case 27: {  // SXTB
                    const auto r = static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(static_cast<std::int8_t>(regs_[a] & 0xFFu)));
                    set_reg(a, r);
                    nz(r);
                    set_flag(kC, false);
                    set_flag(kV, false);
                    return retired;
                }
                case 28: {  // LEAVE
                    const std::uint32_t mask = static_cast<std::uint32_t>(d.immediate);
                    set_sp(regs_[13] - 4u * static_cast<std::uint32_t>(popcount16(mask)));
                    for (unsigned bit = 16; bit-- > 0;) {
                        if ((mask >> bit) & 1u) {
                            std::uint32_t value = 0;
                            if (!pop(value)) {
                                return access_trap();
                            }
                            set_reg(bit, value);
                        }
                    }
                    std::uint32_t frame = 0;
                    if (!load(4, regs_[kSp], frame)) {
                        return access_trap();
                    }
                    set_reg(13, frame);
                    regs_[kSp] += 4;
                    return retired;
                }
                case 29: {  // LDHU
                    std::uint32_t value = 0;
                    if (!load(2, address, value)) {
                        return access_trap();
                    }
                    set_reg(a, value);
                    return retired;
                }
                case 30:  // STH
                    if (!store(2, address, regs_[a])) {
                        return access_trap();
                    }
                    return retired;
                default: {  // 31 LDHS
                    std::uint32_t value = 0;
                    if (!load(2, address, value)) {
                        return access_trap();
                    }
                    set_reg(a, static_cast<std::uint32_t>(
                                    static_cast<std::int32_t>(static_cast<std::int16_t>(value & 0xFFFFu))));
                    return retired;
                }
            }
        }
        case OpClass::Generic: {
            const Operand& one = d.op1;
            const Operand& two = d.op2;
            const auto value_of = [this](const Operand& operand) -> std::uint32_t {
                if (operand.mode == 0) {
                    return regs_[operand.reg];
                }
                if (operand.mode == 1) {
                    return static_cast<std::uint32_t>(operand.value);
                }
                return regs_[operand.reg] + static_cast<std::uint32_t>(operand.value);
            };
            const auto resolve = [this](const Operand& operand, unsigned size) -> std::uint32_t {
                if (operand.mode == 4) {
                    regs_[operand.reg] -= size;
                    return regs_[operand.reg];
                }
                if (operand.mode == 0) {
                    return regs_[operand.reg];
                }
                return regs_[operand.reg] + static_cast<std::uint32_t>(operand.value);
            };
            const auto post = [this](const Operand& operand, unsigned size) {
                if (operand.mode == 3) {
                    regs_[operand.reg] += size;
                }
            };
            switch (d.op) {
                case 0x00:
                    set_reg(one.reg, value_of(two));
                    return retired;
                case 0x01:
                    set_reg(one.reg, add(regs_[one.reg], value_of(two), 0));
                    return retired;
                case 0x02:
                    set_reg(one.reg, add(regs_[one.reg], value_of(two), flag(kC) ? 1u : 0u));
                    return retired;
                case 0x03:
                    set_reg(one.reg, sub(regs_[one.reg], value_of(two), 0));
                    return retired;
                case 0x04:
                    set_reg(one.reg, sub(regs_[one.reg], value_of(two), flag(kC) ? 1u : 0u));
                    return retired;
                case 0x05:
                    set_reg(one.reg, logic(regs_[one.reg] & value_of(two)));
                    return retired;
                case 0x06:
                    set_reg(one.reg, logic(regs_[one.reg] | value_of(two)));
                    return retired;
                case 0x07:
                    set_reg(one.reg, logic(regs_[one.reg] ^ value_of(two)));
                    return retired;
                case 0x08:
                    sub(regs_[one.reg], value_of(two), 0);
                    return retired;
                case 0x09: {
                    const std::uint32_t r = regs_[one.reg] * value_of(two);
                    set_reg(one.reg, r);
                    nz(r);
                    return retired;
                }
                case 0x0A: {
                    const std::uint64_t r =
                        static_cast<std::uint64_t>(regs_[one.reg]) * value_of(two);
                    set_reg(one.reg, static_cast<std::uint32_t>(r >> 32));
                    nz(static_cast<std::uint32_t>(r >> 32));
                    return retired;
                }
                case 0x0B:
                case 0x0C:
                case 0x0D:
                    set_reg(one.reg, shift(d.op, regs_[one.reg], value_of(two)));
                    return retired;
                case 0x0E: {  // LD
                    std::uint32_t value = 0;
                    const std::uint32_t addr = resolve(two, 4);
                    if (!load(4, addr, value)) {
                        return access_trap();
                    }
                    set_reg(one.reg, value);
                    post(two, 4);
                    return retired;
                }
                case 0x0F: {  // ST
                    const std::uint32_t addr = resolve(two, 4);
                    if (!store(4, addr, regs_[one.reg])) {
                        return access_trap();
                    }
                    post(two, 4);
                    return retired;
                }
                case 0x10: {  // LDBU
                    std::uint32_t value = 0;
                    const std::uint32_t addr = resolve(two, 1);
                    if (!load(1, addr, value)) {
                        return access_trap();
                    }
                    set_reg(one.reg, value);
                    post(two, 1);
                    return retired;
                }
                case 0x11: {  // LDBS
                    std::uint32_t value = 0;
                    const std::uint32_t addr = resolve(two, 1);
                    if (!load(1, addr, value)) {
                        return access_trap();
                    }
                    set_reg(one.reg, static_cast<std::uint32_t>(
                                         static_cast<std::int32_t>(static_cast<std::int8_t>(value & 0xFFu))));
                    post(two, 1);
                    return retired;
                }
                case 0x12: {  // STB
                    const std::uint32_t addr = resolve(two, 1);
                    if (!store(1, addr, regs_[one.reg])) {
                        return access_trap();
                    }
                    post(two, 1);
                    return retired;
                }
                case 0x13:
                    set_reg(one.reg, resolve(two, 1));
                    return retired;
                case 0x14: {  // NEG
                    const std::uint32_t r = sub(0, regs_[one.reg], 0);
                    set_reg(one.reg, r);
                    return retired;
                }
                case 0x15: {
                    const std::uint32_t r = ~regs_[one.reg];
                    set_reg(one.reg, r);
                    nz(r);
                    set_flag(kC, false);
                    set_flag(kV, false);
                    return retired;
                }
                case 0x16: {
                    const auto r = static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(static_cast<std::int8_t>(regs_[one.reg] & 0xFFu)));
                    set_reg(one.reg, r);
                    nz(r);
                    return retired;
                }
                case 0x17:
                case 0x18: {  // DIV / DIVS
                    const std::uint32_t divisor = value_of(two);
                    if (divisor == 0) {
                        return enter_trap(3);
                    }
                    const std::uint32_t r =
                        d.op == 0x17
                            ? regs_[one.reg] / divisor
                            : static_cast<std::uint32_t>(static_cast<std::int32_t>(regs_[one.reg]) /
                                                         static_cast<std::int32_t>(divisor));
                    set_reg(one.reg, r);
                    nz(r);
                    set_flag(kC, false);
                    set_flag(kV, false);
                    return retired;
                }
                case 0x19:
                    set_pc(value_of(one));
                    return StepResult{Stop::Retired, 0, 0, true};
                case 0x1A:
                    if (!push(regs_[kPc] + d.length)) {
                        return access_trap();
                    }
                    set_pc(value_of(one));
                    return StepResult{Stop::Retired, 0, 0, true};
                case 0x1B: {
                    const auto r = static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(static_cast<std::int16_t>(regs_[one.reg] & 0xFFFFu)));
                    set_reg(one.reg, r);
                    nz(r);
                    return retired;
                }
                case 0x1C: {
                    std::uint32_t value = 0;
                    const std::uint32_t addr = resolve(two, 2);
                    if (!load(2, addr, value)) {
                        return access_trap();
                    }
                    set_reg(one.reg, value);
                    post(two, 2);
                    return retired;
                }
                case 0x1D: {
                    std::uint32_t value = 0;
                    const std::uint32_t addr = resolve(two, 2);
                    if (!load(2, addr, value)) {
                        return access_trap();
                    }
                    set_reg(one.reg, static_cast<std::uint32_t>(
                                         static_cast<std::int32_t>(static_cast<std::int16_t>(value & 0xFFFFu))));
                    post(two, 2);
                    return retired;
                }
                default: {  // 0x1E STH
                    const std::uint32_t addr = resolve(two, 2);
                    if (!store(2, addr, regs_[one.reg])) {
                        return access_trap();
                    }
                    post(two, 2);
                    return retired;
                }
            }
        }
        case OpClass::LongInt: {
            const auto address = static_cast<std::uint32_t>(d.immediate);
            switch (d.op) {
                case 0: {
                    std::uint32_t value = 0;
                    if (!load(1, address, value)) {
                        return access_trap();
                    }
                    set_reg(d.a, value);
                    return retired;
                }
                case 1: {
                    std::uint32_t value = 0;
                    if (!load(1, address, value)) {
                        return access_trap();
                    }
                    set_reg(d.a, static_cast<std::uint32_t>(
                                     static_cast<std::int32_t>(static_cast<std::int8_t>(value & 0xFFu))));
                    return retired;
                }
                case 2:
                    if (!store(1, address, regs_[d.a])) {
                        return access_trap();
                    }
                    return retired;
                case 3:
                    set_reg(d.a, regs_[kPc] + address);
                    return retired;
                case 4:
                    set_reg(d.a, regs_[kSp] + address);
                    return retired;
                case 5: {
                    std::uint32_t value = 0;
                    if (!load(2, address, value)) {
                        return access_trap();
                    }
                    set_reg(d.a, value);
                    return retired;
                }
                case 6:
                    if (!store(2, address, regs_[d.a])) {
                        return access_trap();
                    }
                    return retired;
                default: {
                    std::uint32_t value = 0;
                    if (!load(2, address, value)) {
                        return access_trap();
                    }
                    set_reg(d.a, static_cast<std::uint32_t>(
                                     static_cast<std::int32_t>(static_cast<std::int16_t>(value & 0xFFFFu))));
                    return retired;
                }
            }
        }
        case OpClass::FpReg: {
            const unsigned fd = d.a;
            const unsigned fs = d.b;
            switch (d.op) {
                case 0:
                    set_freg(fd, f_[fd] + f_[fs]);
                    return retired;
                case 1:
                    set_freg(fd, f_[fd] - f_[fs]);
                    return retired;
                case 2:
                    set_freg(fd, f_[fd] * f_[fs]);
                    return retired;
                case 3:
                    set_freg(fd, f_[fd] / f_[fs]);
                    return retired;
                case 4:
                    set_freg(fd, -f_[fd]);
                    return retired;
                case 5:
                    set_freg(fd, std::fabs(f_[fd]));
                    return retired;
                case 6:
                    set_freg(fd, std::sqrt(f_[fd]));
                    return retired;
                case 7:
                    set_freg(fd, f_[fs]);
                    return retired;
                case 8: {  // FCMP
                    const double left = f_[fd];
                    const double right = f_[fs];
                    const bool unordered = std::isnan(left) || std::isnan(right);
                    set_flag(kZ, !unordered && left == right);
                    set_flag(kN, !unordered && left < right);
                    set_flag(kC, false);
                    set_flag(kV, unordered);
                    return retired;
                }
                default:  // FRND: round to binary32 precision
                    set_freg(fd, static_cast<double>(static_cast<float>(f_[fd])));
                    return retired;
            }
        }
        case OpClass::FpMem:
        case OpClass::FpMemWide: {
            const unsigned size = (d.op == 0 || d.op == 1) ? 8u : 4u;
            // F4's mode 01 is [Rb+d8]; only FA's mode 01 is [PC+d32].
            const bool pc_relative = d.kind == OpClass::FpMemWide && d.op1.mode == 1;
            std::uint32_t address = 0;
            if (pc_relative) {
                address = regs_[kPc] + static_cast<std::uint32_t>(d.immediate);
            } else if (d.op1.mode == 2) {  // [Rb]+
                address = regs_[d.b];
            } else if (d.op1.mode == 3) {  // -[Rb]
                regs_[d.b] -= size;
                address = regs_[d.b];
            } else {
                address = regs_[d.b] + static_cast<std::uint32_t>(d.immediate);
            }
            std::uint32_t low = 0;
            std::uint32_t high = 0;
            if (d.op == 0 || d.op == 2) {  // FLD, FLDS
                if (size == 8) {
                    if (!load(4, address, low) || !load(4, address + 4, high)) {
                        return access_trap();
                    }
                    set_freg(d.a, bits_double(static_cast<std::uint64_t>(low) |
                                              (static_cast<std::uint64_t>(high) << 32)));
                } else {
                    if (!load(4, address, low)) {
                        return access_trap();
                    }
                    float narrowed = 0.0F;
                    std::memcpy(&narrowed, &low, sizeof narrowed);
                    set_freg(d.a, static_cast<double>(narrowed));
                }
            } else {  // FST, FSTS
                if (size == 8) {
                    const std::uint64_t bits = double_bits(f_[d.a]);
                    if (!store(4, address, static_cast<std::uint32_t>(bits)) ||
                        !store(4, address + 4, static_cast<std::uint32_t>(bits >> 32))) {
                        return access_trap();
                    }
                } else {
                    const float narrowed = static_cast<float>(f_[d.a]);
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, &narrowed, sizeof bits);
                    if (!store(4, address, bits)) {
                        return access_trap();
                    }
                }
            }
            if (d.op1.mode == 2) {
                regs_[d.b] += size;
            }
            return retired;
        }
        case OpClass::FpConvert: {
            const unsigned fd = d.a;
            const unsigned r = d.b;
            switch (d.op) {
                case 0:
                    set_freg(fd, static_cast<double>(static_cast<std::int32_t>(regs_[r])));
                    return retired;
                case 1:
                    set_freg(fd, static_cast<double>(regs_[r]));
                    return retired;
                case 2: {
                    const double value = f_[fd];
                    if (std::isnan(value) || value < -2147483648.0 || value > 2147483647.0) {
                        return enter_trap(3);
                    }
                    set_reg(r, static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
                    return retired;
                }
                default: {
                    const double value = f_[fd];
                    if (std::isnan(value) || value < 0.0 || value > 4294967295.0) {
                        return enter_trap(3);
                    }
                    set_reg(r, static_cast<std::uint32_t>(value));
                    return retired;
                }
            }
        }
        case OpClass::FpStack: {
            if (d.op == 0) {
                regs_[kSp] -= 8;
                const std::uint64_t bits = double_bits(f_[d.a]);
                if (!store(4, regs_[kSp], static_cast<std::uint32_t>(bits)) ||
                    !store(4, regs_[kSp] + 4, static_cast<std::uint32_t>(bits >> 32))) {
                    return access_trap();
                }
                return retired;
            }
            std::uint32_t low = 0;
            std::uint32_t high = 0;
            if (!load(4, regs_[kSp], low) || !load(4, regs_[kSp] + 4, high)) {
                return access_trap();
            }
            set_freg(d.a, bits_double(static_cast<std::uint64_t>(low) |
                                      (static_cast<std::uint64_t>(high) << 32)));
            regs_[kSp] += 8;
            return retired;
        }
        case OpClass::Enter: {
            const std::uint32_t frame = static_cast<std::uint32_t>(d.immediate);
            const std::uint32_t mask = d.address;
            regs_[kSp] -= 4;
            if (!store(4, regs_[kSp], regs_[13])) {
                return access_trap();
            }
            set_reg(13, regs_[kSp]);
            for (unsigned bit = 0; bit < 16; ++bit) {
                if ((mask >> bit) & 1u) {
                    if (!push(regs_[bit])) {
                        return access_trap();
                    }
                }
            }
            regs_[kSp] -= frame;
            return retired;
        }
        case OpClass::Wide: {
            const auto target = d.address;
            const auto signed_value = static_cast<std::uint32_t>(d.immediate);
            switch (d.op) {
                case 0:
                    set_pc(target);
                    return StepResult{Stop::Retired, 0, 0, true};
                case 1:
                    if (!push(regs_[kPc] + 6)) {
                        return access_trap();
                    }
                    set_pc(target);
                    return StepResult{Stop::Retired, 0, 0, true};
                case 2:
                case 3: {
                    std::uint32_t loaded = 0;
                    if (!load(4, target, loaded)) {
                        return access_trap();
                    }
                    if (d.op == 3 && !push(regs_[kPc] + 6)) {
                        return access_trap();
                    }
                    set_pc(loaded);
                    return StepResult{Stop::Retired, 0, 0, true};
                }
                case 4:
                    set_reg(d.a, regs_[kPc] + signed_value);
                    return retired;
                case 5:
                    set_reg(d.a, regs_[kSp] + signed_value);
                    return retired;
                case 6: {
                    std::uint32_t value = 0;
                    if (!load(4, regs_[kPc] + signed_value, value)) {
                        return access_trap();
                    }
                    set_reg(d.a, value);
                    return retired;
                }
                case 7: {
                    std::uint32_t value = 0;
                    if (!load(1, regs_[kPc] + signed_value, value)) {
                        return access_trap();
                    }
                    set_reg(d.a, value);
                    return retired;
                }
                case 8: {
                    std::uint32_t value = 0;
                    if (!load(1, regs_[kPc] + signed_value, value)) {
                        return access_trap();
                    }
                    set_reg(d.a, static_cast<std::uint32_t>(
                                     static_cast<std::int32_t>(static_cast<std::int8_t>(value & 0xFFu))));
                    return retired;
                }
                case 9: {
                    std::uint32_t value = 0;
                    if (!load(2, regs_[kPc] + signed_value, value)) {
                        return access_trap();
                    }
                    set_reg(d.a, value);
                    return retired;
                }
                case 0xA:
                    set_reg(d.a, add(regs_[d.a], target, 0));
                    return retired;
                case 0xB:
                    set_reg(d.a, sub(regs_[d.a], target, 0));
                    return retired;
                case 0xC:
                    sub(regs_[d.a], target, 0);
                    return retired;
                case 0xD:
                    set_reg(d.a, logic(regs_[d.a] & target));
                    return retired;
                case 0xE:
                    set_reg(d.a, logic(regs_[d.a] | target));
                    return retired;
                default:
                    set_reg(d.a, logic(regs_[d.a] ^ target));
                    return retired;
            }
        }
        case OpClass::Reserved:
        default:
            return enter_trap(2);
    }
}

}  // namespace kr232
