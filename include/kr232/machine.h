#pragma once

#include <kr232/decoder.h>
#include <kr232/devices.h>
#include <kr232/memory.h>

#include <cstdint>

namespace kr232 {

enum class Stop : std::uint8_t {
    Retired,  // the instruction completed
    Halted,   // HALT, or a SYS exit
    Trapped,  // a vector was taken; see StepResult::vector
};

struct StepResult {
    Stop stop = Stop::Retired;
    unsigned vector = 0;
    std::uint8_t status = 0;  // the exit status for Halted
    bool jumped = false;      // the instruction set PC itself
};

// The architectural state and one step's execution.
//
// Flags live in R11 at the ISA's bit positions (Z=0, N=1, C=2, V=3, I=4, T=5,
// K=6). An ordinary write to R11 changes Z, N, C, V and T only: K and I are
// hardware-owned, so `set_reg(11, x)` restores them (ISA §2.2, §15.29).
//
// An access the Memory class cannot serve becomes a trap, never a silent zero:
// the executor asks the map rather than guessing a vector. Until the protection
// task lands every access runs with K = 1, and a trap leaves PC at the trapping
// instruction because the frame entry of ISA §11.3 is that task's.
class Machine {
public:
    static constexpr unsigned kEfp = 12;
    static constexpr unsigned kPc = 14;
    static constexpr unsigned kSp = 15;
    static constexpr unsigned kFlags = 11;

    static constexpr std::uint32_t kResetSp = 0x08000000;
    static constexpr std::uint32_t kResetWord = 0x00000100;

    explicit Machine(std::uint32_t ram_bytes = Memory::kRamSize);

    // Power-on state: PC from the reset word, SP at the top of kernel RAM, K = 1
    // and I = 0, every other register zero.
    void reset();

    [[nodiscard]] Memory& memory() noexcept { return memory_; }
    [[nodiscard]] const Memory& memory() const noexcept { return memory_; }
    [[nodiscard]] Devices& devices() noexcept { return devices_; }
    [[nodiscard]] const Devices& devices() const noexcept { return devices_; }

    [[nodiscard]] std::uint32_t reg(unsigned index) const noexcept { return regs_[index & 0xFu]; }
    void set_reg(unsigned index, std::uint32_t value) noexcept;
    [[nodiscard]] double freg(unsigned index) const noexcept { return f_[index & 0x3u]; }
    void set_freg(unsigned index, double value) noexcept;

    [[nodiscard]] std::uint32_t pc() const noexcept { return regs_[kPc]; }
    void set_pc(std::uint32_t value) noexcept { regs_[kPc] = value; }
    [[nodiscard]] std::uint32_t sp() const noexcept { return regs_[kSp]; }
    void set_sp(std::uint32_t value) noexcept { regs_[kSp] = value; }

    [[nodiscard]] bool flag(unsigned bit) const noexcept {
        return ((regs_[kFlags] >> bit) & 1u) != 0;
    }
    void set_flag(unsigned bit, bool value) noexcept;
    [[nodiscard]] bool halted() const noexcept { return halted_; }
    [[nodiscard]] std::uint8_t exit_status() const noexcept { return exit_status_; }
    [[nodiscard]] std::uint64_t cycles() const noexcept { return cycles_; }

    // K = 1. Trap entry sets it and only IRET clears it (ISA §2.2).
    [[nodiscard]] bool kernel_mode() const noexcept { return flag(6); }

    // Executes the instruction at PC.
    StepResult step();

private:
    StepResult execute(const Decoded& decoded);
    // Builds the 16-byte frame of ISA §11.3 and vectors to the handler. A frame
    // that cannot be pushed halts the machine: there is no double-fault path.
    StepResult enter_trap(unsigned vector);
    // The trap a failed access takes: 8 when protection refused it, else 4.
    StepResult access_trap();
    [[nodiscard]] unsigned fetch(std::uint8_t* out, std::uint32_t address) const;
    void set_nz(std::uint32_t value) noexcept;

    Memory memory_;
    Devices devices_;
    std::uint32_t regs_[16] = {};
    double f_[4] = {};
    std::uint64_t cycles_ = 0;
    bool halted_ = false;
    std::uint8_t exit_status_ = 0;
    unsigned access_vector_ = 0;
};

}  // namespace kr232
