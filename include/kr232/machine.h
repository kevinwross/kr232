#pragma once

#include <kr232/decoder.h>
#include <kr232/devices.h>
#include <kr232/memory.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kr232 {

enum class Stop : std::uint8_t {
    Retired,  // the instruction completed
    Halted,   // HALT, or a SYS exit
    Trapped,  // a vector was taken; see StepResult::vector
};

struct StepResult {
    Stop stop = Stop::Retired;
    unsigned vector = 0;
    std::uint32_t status = 0;  // the exit status for Halted, in full
    bool jumped = false;      // the instruction set PC itself
};

// The architectural state and one step's execution.
//
// Flags live in R11 at the ISA's bit positions (Z=0, N=1, C=2, V=3, I=4, T=5,
// K=6). An ordinary write to R11 changes Z, N, C, V and T only: K and I are
// hardware-owned, so `set_reg(11, x)` restores them (ISA §2.2, §15.29).
//
// An access the Memory class cannot serve becomes a trap, never a silent zero:
// the executor asks the map rather than guessing a vector. Protection is the two
// range checks of ISA §2.3, and trap entry builds the frame of ISA §11.3, which
// saves PC at the trapping instruction.
class Machine {
public:
    static constexpr unsigned kZero = 0;
    static constexpr unsigned kOne = 1;
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

    // The shared text display behind the terminal page (kr232-devices.md §7). A
    // debugger renders its cells, cursor and colours, and sends input through it; the
    // terminal page and the SYS standard streams are the guest-visible front ends of
    // the same display.
    [[nodiscard]] krc::display::TextDisplay& display() noexcept { return devices_.display(); }
    [[nodiscard]] const krc::display::TextDisplay& display() const noexcept {
        return devices_.display();
    }
    // The display's host input, for a debugger front end. push_text turns line feeds
    // into Enter and drops carriage returns; push_key carries a raw key code.
    void push_display_key(std::uint16_t code) { devices_.display().push_key(code); }
    void push_display_text(std::string_view text) { devices_.display().push_text(text); }
    void push_display_click(std::uint16_t row, std::uint16_t col, std::uint16_t buttons) {
        devices_.display().push_click(row, col, buttons);
    }

    // R0 and R1 read as their constants whatever the array holds (ISA 2.1): the
    // value is the machine model, not a register that happens to be initialised.
    [[nodiscard]] std::uint32_t reg(unsigned index) const noexcept {
        switch (index & 0xFu) {
            case 0: return 0;  // R0, the zero register
            case 1: return 1;  // R1, the one register
            default: return regs_[index & 0xFu];
        }
    }
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
    [[nodiscard]] std::uint32_t exit_status() const noexcept { return exit_status_; }
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
    std::uint32_t exit_status_ = 0;
    unsigned access_vector_ = 0;

    // Decoding an instruction formats its disassembly text, which allocates for
    // almost every instruction. Execution does not need that text, so keep the
    // decoded form beside the bytes it came from: a hit skips both the decoder
    // and the formatting. The bytes are compared on every hit because guest code
    // can be overwritten; a changed instruction misses and is decoded again.
    struct InstructionCacheEntry {
        std::uint32_t pc = 0;
        unsigned count = 0;
        std::uint8_t bytes[8] = {};
        Decoded decoded;
        bool valid = false;
    };
    static constexpr std::size_t kInstructionCacheEntries = 4096;
    // Heap storage: a Machine is a test-local object in several suites, and
    // 4096 decoded entries are too large for the stack.
    std::vector<InstructionCacheEntry> instruction_cache_;
    void clear_instruction_cache() noexcept;
};

}  // namespace kr232
