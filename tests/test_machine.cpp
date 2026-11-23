#include "harness.h"

#include <kr232/machine.h>

#include <cstdint>
#include <initializer_list>

using kr232::Machine;
using kr232::Stop;

namespace {

// Loads a byte sequence at 0x1000, which is inside kernel RAM, and points PC at
// it. Each test then steps a known number of instructions.
struct Program {
    Machine machine;

    // The default machine is 64 KiB: enough for the code page and a stack, and
    // too small to hold user RAM at 0x08000000. A test that writes there needs
    // the bigger size; `write8` answers Unmapped rather than pretending, and
    // `the_default_test_machine_cannot_address_user_ram` pins that.
    explicit Program(std::initializer_list<std::uint8_t> code,
                     std::uint32_t bytes = 0x10000)
        : machine(bytes) {
        std::uint32_t at = 0x1000;
        for (const std::uint8_t byte : code) {
            (void)machine.memory().write8(at++, byte);
        }
        // The reset SP is at the top of 256 MiB of RAM; a short test machine has
        // to put the stack inside it or every push traps.
        machine.set_sp(0x8000);
        machine.set_pc(0x1000);
    }

    void run(unsigned steps) {
        for (unsigned i = 0; i < steps; ++i) {
            const auto result = machine.step();
            CHECK(result.stop == Stop::Retired);
        }
    }
};

}  // namespace

TEST_CASE(reset_places_the_stack_and_the_kernel_bit) {
    Machine machine(0x10000);
    CHECK(machine.sp() == Machine::kResetSp);
    CHECK(machine.flag(6));   // K
    CHECK(!machine.flag(4));  // I
    CHECK(machine.reg(2) == 0);
}

TEST_CASE(load_immediate_then_add_registers) {
    // ld r2,#0x1234 ; ld r3,#0x10 ; add r2,r3
    Program program{0x52, 0x34, 0x12, 0x00, 0x00, 0x53, 0x10, 0x00, 0x00, 0x00, 0x81, 0x23};
    program.run(3);
    CHECK(program.machine.reg(2) == 0x1244);
}

TEST_CASE(subtraction_below_zero_sets_n_and_clears_carry) {
    // ld r2,#5 ; sub r2,#7  (Normal op 8, signed imm8)
    Program program{0x52, 0x05, 0x00, 0x00, 0x00, 0xC8, 0x20, 0x07};
    program.run(2);
    CHECK(program.machine.reg(2) == 0xFFFFFFFEu);
    CHECK(program.machine.flag(1));   // N
    CHECK(!program.machine.flag(0));  // Z
    CHECK(!program.machine.flag(2));  // C: a borrow happened
}

TEST_CASE(push_decrements_sp_and_pop_restores_it) {
    // ld r2,#0xAA ; push r2 ; pop r3
    Program program{0x52, 0xAA, 0x00, 0x00, 0x00, 0x02, 0x13};
    program.run(1);
    const std::uint32_t sp_before = program.machine.sp();
    program.run(1);
    CHECK(program.machine.sp() == sp_before - 4);
    std::uint32_t value = 0;
    CHECK(program.machine.memory().read32(program.machine.sp(), value) == kr232::Access::Ok);
    CHECK(value == 0xAA);
    program.run(1);
    CHECK(program.machine.reg(3) == 0xAA);
    CHECK(program.machine.sp() == sp_before);
}

TEST_CASE(call_pushes_the_return_address_and_ret_returns) {
    // call +1  (target 0x1004) ; nop ; ret
    Program program{0xD0, 0x01, 0x00, 0x71, 0x70};
    program.run(1);
    CHECK(program.machine.pc() == 0x1004);
    std::uint32_t saved = 0;
    CHECK(program.machine.memory().read32(program.machine.sp(), saved) == kr232::Access::Ok);
    CHECK(saved == 0x1003);
    program.run(1);
    CHECK(program.machine.pc() == 0x1003);
}

TEST_CASE(a_taken_branch_moves_pc_and_an_untaken_one_does_not) {
    // ld r2,#0 ; cmp r2,#0 ; beq +4 ; nop ; nop ; nop
    Program taken{0x52, 0x00, 0x00, 0x00, 0x00, 0xC9, 0x20, 0x00, 0xA0, 0x04, 0x71, 0x71, 0x71};
    taken.run(3);
    CHECK(taken.machine.pc() == 0x100E);

    // The same compare against 1 leaves the branch untaken.
    Program untaken{0x52, 0x01, 0x00, 0x00, 0x00, 0xC9, 0x20, 0x00, 0xA0, 0x04, 0x71, 0x71, 0x71};
    untaken.run(3);
    CHECK(untaken.machine.pc() == 0x100A);
}

TEST_CASE(byte_loads_zero_and_sign_extend) {
    // ld r3,#0x2000 ; ldbu r2,[r3] ; ldbs r4,[r3]
    Program program{0x53, 0x00, 0x20, 0x00, 0x00, 0x90, 0x23, 0x91, 0x43};
    (void)program.machine.memory().write8(0x2000, 0xFF);
    program.run(3);
    CHECK(program.machine.reg(2) == 0x000000FFu);
    CHECK(program.machine.reg(4) == 0xFFFFFFFFu);
}

TEST_CASE(enter_and_leave_leave_the_stack_where_they_found_it) {
    // enter #8,#0 ; leave #0
    Program program{0xF7, 0x08, 0x00, 0x00, 0xDC, 0x00, 0x00};
    const std::uint32_t sp_before = program.machine.sp();
    program.run(1);
    CHECK(program.machine.sp() == sp_before - 12);
    CHECK(program.machine.reg(13) == sp_before - 4);
    program.run(1);
    CHECK(program.machine.sp() == sp_before);
    CHECK(program.machine.reg(13) == 0);
}

TEST_CASE(syscall_vectors_to_the_handler_and_the_frame_saves_the_next_pc) {
    Program program{0x79};
    (void)program.machine.memory().write32(0x100 + 4 * 9, 0x3000);  // vector 9 handler
    const auto result = program.machine.step();
    // A vector with a handler installed continues into it: the step retires and
    // the vector is still reported. Only a vector whose handler word is zero
    // stops as Trapped, which is what the protection tests rely on.
    CHECK(result.stop == Stop::Retired);
    CHECK(result.vector == 9);
    CHECK(program.machine.pc() == 0x3000);
    std::uint32_t saved_pc = 0;
    CHECK(program.machine.memory().read32(program.machine.reg(12) + 4, saved_pc) ==
          kr232::Access::Ok);
    CHECK(saved_pc == 0x1001);
}

TEST_CASE(a_trap_from_user_mode_lands_on_the_kernel_stack) {
    // 128 MiB of RAM, because a trap taken with K = 0 moves SP to 0x08000000.
    kr232::Machine machine(0x08000200);
    (void)machine.memory().write32(0x6000, 0x00000000);  // saved EFP
    (void)machine.memory().write32(0x6004, 0x08000100);  // user PC, inside user RAM
    (void)machine.memory().write32(0x6008, 0x00000000);  // K = 0
    (void)machine.memory().write32(0x600C, 0x00000100);  // user SP
    (void)machine.memory().write8(0x1000, 0x77);         // IRET into user mode
    (void)machine.memory().write8(0x08000100, 0x72);     // BRK in user mode

    machine.set_reg(12, 0x6000);
    machine.set_pc(0x1000);
    CHECK(machine.step().stop == Stop::Retired);
    CHECK(!machine.kernel_mode());

    machine.set_pc(0x08000100);
    const auto result = machine.step();
    CHECK(result.stop == Stop::Trapped);
    CHECK(result.vector == 1);
    CHECK(machine.kernel_mode());
    const std::uint32_t efp = machine.reg(12);
    CHECK(efp == 0x08000000u - 16u);
    std::uint32_t frame_pc = 0;
    std::uint32_t frame_flags = 0;
    std::uint32_t frame_sp = 0;
    CHECK(machine.memory().read32(efp + 4, frame_pc) == kr232::Access::Ok);
    CHECK(machine.memory().read32(efp + 8, frame_flags) == kr232::Access::Ok);
    CHECK(machine.memory().read32(efp + 12, frame_sp) == kr232::Access::Ok);
    CHECK(frame_pc == 0x08000100);
    CHECK((frame_flags & 0x40u) == 0);  // K as it was: user
    CHECK(frame_sp == 0x00000100);
}

namespace {

// IRETs into user mode at `user_pc`, leaving the machine in user mode.
void enter_user_mode(kr232::Machine& machine, std::uint32_t user_pc) {
    (void)machine.memory().write32(0x6000, 0x00000000);
    (void)machine.memory().write32(0x6004, user_pc);
    (void)machine.memory().write32(0x6008, 0x00000000);
    (void)machine.memory().write32(0x600C, 0x00000100);
    (void)machine.memory().write8(0x1000, 0x77);
    machine.set_reg(12, 0x6000);
    machine.set_pc(0x1000);
    (void)machine.step();
}

}  // namespace

TEST_CASE(a_user_mode_load_from_kernel_ram_traps_8) {
    kr232::Machine machine(0x08000200);
    enter_user_mode(machine, 0x08000100);
    (void)machine.memory().write8(0x08000100, 0x90);  // ldbu r2,[r3]
    (void)machine.memory().write8(0x08000101, 0x23);
    machine.set_reg(3, 0x3000);  // kernel RAM
    machine.set_pc(0x08000100);
    const auto result = machine.step();
    CHECK(result.stop == Stop::Trapped);
    CHECK(result.vector == 8);
    CHECK(machine.kernel_mode());
}

TEST_CASE(a_user_mode_load_from_the_gap_is_an_address_error_not_protection) {
    kr232::Machine machine(0x08000200);
    enter_user_mode(machine, 0x08000100);
    (void)machine.memory().write8(0x08000100, 0x90);
    (void)machine.memory().write8(0x08000101, 0x23);
    machine.set_reg(3, 0x10000000);  // unbacked gap
    machine.set_pc(0x08000100);
    const auto result = machine.step();
    CHECK(result.stop == Stop::Trapped);
    CHECK(result.vector == 4);
}

TEST_CASE(a_kernel_mode_load_from_the_null_page_traps_4) {
    Program program{0x90, 0x23};  // ldbu r2,[r3]
    program.machine.set_reg(3, 0x10);
    const auto result = program.machine.step();
    CHECK(result.stop == Stop::Trapped);
    CHECK(result.vector == 4);
}

TEST_CASE(a_user_mode_fetch_outside_user_ram_traps_8) {
    kr232::Machine machine(0x08000200);
    enter_user_mode(machine, 0x08000100);
    machine.set_pc(0x3000);  // kernel RAM: protection, not a gap
    const auto result = machine.step();
    CHECK(result.stop == Stop::Trapped);
    CHECK(result.vector == 8);
}

TEST_CASE(kernel_only_instructions_trap_in_user_mode) {
    kr232::Machine machine(0x08000200);
    const std::uint8_t kernel_only[] = {0x73, 0x74, 0x75, 0x76, 0x77};  // SYS, EI, DI, HALT, IRET
    for (const std::uint8_t opcode : kernel_only) {
        enter_user_mode(machine, 0x08000100);
        CHECK(!machine.kernel_mode());
        (void)machine.memory().write8(0x08000100, opcode);
        machine.set_pc(0x08000100);
        const auto result = machine.step();
        CHECK(result.stop == Stop::Trapped);
        CHECK(result.vector == 8);
        CHECK(machine.kernel_mode());
        std::uint32_t frame_pc = 0;
        CHECK(machine.memory().read32(machine.reg(12) + 4, frame_pc) == kr232::Access::Ok);
        CHECK(frame_pc == 0x08000100);
    }
}

TEST_CASE(syscall_is_the_one_system_instruction_user_mode_may_execute) {
    kr232::Machine machine(0x08000200);
    enter_user_mode(machine, 0x08000100);
    (void)machine.memory().write8(0x08000100, 0x79);  // SYSCALL
    machine.set_pc(0x08000100);
    const auto result = machine.step();
    CHECK(result.stop == Stop::Trapped);
    CHECK(result.vector == 9);
    CHECK(machine.kernel_mode());
}

TEST_CASE(iret_restores_the_interrupt_flag_from_the_frame) {
    Program program{0x77};  // IRET
    (void)program.machine.memory().write32(0x6000, 0x00000000);
    (void)program.machine.memory().write32(0x6004, 0x00003000);
    (void)program.machine.memory().write32(0x6008, 0x00000010);  // I set, K clear
    (void)program.machine.memory().write32(0x600C, 0x00008000);
    program.machine.set_reg(12, 0x6000);
    program.run(1);
    CHECK(program.machine.flag(4));       // I restored
    CHECK(!program.machine.kernel_mode());  // K restored
    CHECK(program.machine.sp() == 0x8000);
}

TEST_CASE(the_gaps_are_address_errors_in_kernel_mode_and_protection_above_fc_in_user) {
    // Kernel mode: both unbacked gaps are address errors.
    Program low_gap{0x90, 0x23};
    low_gap.machine.set_reg(3, 0x10000000);
    const auto low = low_gap.machine.step();
    CHECK(low.stop == Stop::Trapped);
    CHECK(low.vector == 4);

    Program high_gap{0x90, 0x23};
    high_gap.machine.set_reg(3, 0xFE000000);
    const auto high = high_gap.machine.step();
    CHECK(high.stop == Stop::Trapped);
    CHECK(high.vector == 4);

    // User mode at or above 0xFC000000 is protection, not an address error.
    kr232::Machine machine(0x08000200);
    enter_user_mode(machine, 0x08000100);
    (void)machine.memory().write8(0x08000100, 0x90);
    (void)machine.memory().write8(0x08000101, 0x23);
    machine.set_reg(3, 0xFE000000);
    machine.set_pc(0x08000100);
    const auto result = machine.step();
    CHECK(result.stop == Stop::Trapped);
    CHECK(result.vector == 8);
}

TEST_CASE(sys_exit_halts_with_the_status_in_r2) {
    Program program{0x73};
    program.machine.set_reg(7, 0);
    program.machine.set_reg(2, 42);
    const auto result = program.machine.step();
    CHECK(result.stop == Stop::Halted);
    CHECK(result.status == 42);
    CHECK(program.machine.halted());
}

TEST_CASE(division_by_zero_is_an_illegal_operand_trap) {
    // ld r2,#5 ; div r2,r3 with r3 = 0
    Program program{0x52, 0x05, 0x00, 0x00, 0x00, 0xD9, 0x23, 0x00};
    program.run(1);
    const auto result = program.machine.step();
    CHECK(result.stop == Stop::Trapped);
    CHECK(result.vector == 3);
}

TEST_CASE(the_generic_form_moves_between_registers) {
    // F0 00 02 03: mov r2, r3
    Program program{0xF0, 0x00, 0x02, 0x03};
    program.machine.set_reg(3, 0xDEADBEEFu);
    program.run(1);
    CHECK(program.machine.reg(2) == 0xDEADBEEFu);
}

TEST_CASE(user_mode_cannot_load_or_call_into_kernel_memory) {
    // The Program helper's machine is 64 KiB, which cannot hold user RAM at
    // 0x08000000; this case builds the full 256 MiB machine the ISA describes.
    constexpr std::uint32_t kUserPc = 0x08000000u;
    Machine machine{256u * 1024u * 1024u};
    (void)machine.memory().write8(0x1000, 0x77);              // iret
    (void)machine.memory().write8(kUserPc, 0xC0);             // ld r2, [r0+16]
    (void)machine.memory().write8(kUserPc + 1, 0x20);
    (void)machine.memory().write8(kUserPc + 2, 0x10);
    (void)machine.memory().write32(0x2000, 0);
    (void)machine.memory().write32(0x2004, kUserPc);
    (void)machine.memory().write32(0x2008, 0);                // K clear
    (void)machine.memory().write32(0x200C, 0x0FFFFFF0u);
    machine.set_pc(0x1000);
    machine.set_sp(0x8000);
    machine.set_reg(12, 0x2000);
    (void)machine.step();                                     // iret
    CHECK(!machine.kernel_mode());
    CHECK(machine.pc() == kUserPc);
    const kr232::StepResult result = machine.step();
    CHECK(result.stop == Stop::Trapped);
    CHECK(result.vector == 8);
}

TEST_CASE(a_byte_store_keeps_every_bit_of_its_value) {
    // `stb [r10], r6` is 0x92 with A = source (6) and B = base (10) in one byte.
    // A bug that clears one bit survives any test that only writes text, so this
    // walks the whole range and compares every value.
    const std::uint8_t code[] = {0x92, 0x6A};
    for (std::uint32_t value = 0; value < 256; ++value) {
        Program program{code[0], code[1]};
        program.machine.set_reg(6, value);
        program.machine.set_reg(10, 0x2000);
        (void)program.machine.step();
        std::uint8_t stored = 0;
        CHECK(program.machine.memory().read8(0x2000, stored) == kr232::Access::Ok);
        CHECK(stored == static_cast<std::uint8_t>(value));
        if (stored != static_cast<std::uint8_t>(value)) {
            return;  // one failure names the value; the rest repeat it
        }
    }
}

TEST_CASE(a_load_into_r11_cannot_write_the_hardware_flags) {
    // R11 is the flags word. `ld r11, [r10]` loads 0x50, but bit 4 is I and bit 6
    // is K: the hardware owns them, so the value lands with those bits as they
    // were. Data must not live in R11 -- a byte held there loses bit 4, which is
    // what made a kernel's console output arrive 0x10 low.
    Program program{0x8E, 0xBA};
    program.machine.set_reg(10, 0x2000);
    (void)program.machine.memory().write8(0x2000, 0x50);
    program.run(1);
    CHECK((program.machine.reg(11) & 0x10u) == 0u);        // I stayed clear
    CHECK((program.machine.reg(11) & 0x2Fu) == 0x00u);     // Z N C V T all clear
    CHECK(program.machine.kernel_mode());
}

TEST_CASE(the_exit_status_is_the_whole_word) {
    // A test that exits 300 must read 300 back, not 44: the status is a full
    // 32-bit value in the machine and in the process the emulator returns.
    Program program{0x73};  // sys
    program.machine.set_reg(7, 0);
    program.machine.set_reg(2, 300);
    const kr232::StepResult result = program.machine.step();
    CHECK(result.stop == Stop::Halted);
    CHECK(result.status == 300u);
    CHECK(program.machine.exit_status() == 300u);

    // And a value with every byte set survives unchanged.
    Program wide{0x73};
    wide.machine.set_reg(7, 0);
    wide.machine.set_reg(2, 0x12345678u);
    (void)wide.machine.step();
    CHECK(wide.machine.exit_status() == 0x12345678u);
}

TEST_CASE(sys_write_sends_guest_bytes_to_the_console) {
    // R7 = 1, R2 = fd 1 (console), R3 = address, R4 = length.
    Program program{0x73};
    (void)program.machine.memory().write8(0x2000, 'h');
    (void)program.machine.memory().write8(0x2001, 'i');
    program.machine.set_reg(7, 1);
    program.machine.set_reg(2, 1);
    program.machine.set_reg(3, 0x2000);
    program.machine.set_reg(4, 2);
    (void)program.machine.step();
    CHECK(program.machine.reg(2) == 2u);
    const auto& out = program.machine.devices().console_output();
    CHECK(out.size() == 2u);
    if (out.size() == 2u) {
        CHECK(out[0] == 'h');
        CHECK(out[1] == 'i');
    }

    // A refused descriptor answers -1 and writes nothing. Descriptor 2 is the
    // conventional error stream and is not one the host serves, so it is refused
    // like any other number (ISA §13: 1 is the console, 0 its input).
    Program refusal{0x73};
    (void)refusal.machine.memory().write8(0x2000, 'x');
    refusal.machine.set_reg(7, 1);
    refusal.machine.set_reg(2, 2);
    refusal.machine.set_reg(3, 0x2000);
    refusal.machine.set_reg(4, 1);
    (void)refusal.machine.step();
    CHECK(refusal.machine.reg(2) == 0xFFFFFFFFu);
    CHECK(refusal.machine.devices().console_output().empty());
}

TEST_CASE(sys_read_takes_console_input_and_time_answers_two_words) {
    Program read{0x73};
    CHECK(read.machine.devices().console_push_input('A'));
    read.machine.set_reg(7, 2);
    read.machine.set_reg(2, 0);
    read.machine.set_reg(3, 0x2000);
    read.machine.set_reg(4, 4);
    (void)read.machine.step();
    CHECK(read.machine.reg(2) == 1u);
    std::uint8_t got = 0;
    CHECK(read.machine.memory().read8(0x2000, got) == kr232::Access::Ok);
    CHECK(got == 'A');

    // An empty queue is end of input: 0 bytes, not an error.
    Program empty{0x73};
    empty.machine.set_reg(7, 2);
    empty.machine.set_reg(2, 0);
    empty.machine.set_reg(3, 0x2000);
    empty.machine.set_reg(4, 1);
    (void)empty.machine.step();
    CHECK(empty.machine.reg(2) == 0u);

    // time fills R2:R3 with 64-bit seconds. The high word is zero until 2106,
    // which is why no test can reach the carry through it: the contract is
    // checked, the top half is not exercisable on a host clock today.
    Program time_call{0x73};
    time_call.machine.set_reg(7, 3);
    (void)time_call.machine.step();
    CHECK(time_call.machine.reg(2) != 0u);
    CHECK(time_call.machine.reg(3) == 0u);
}

TEST_CASE(sys_debug_write_and_a_reserved_number) {
    // R7 = 4 writes guest bytes to the host's debug stream: the result is 0 for
    // a valid range and -1 for one that is not mapped.
    Program debug{0x73};
    (void)debug.machine.memory().write8(0x2000, 'd');
    debug.machine.set_reg(7, 4);
    debug.machine.set_reg(2, 0x2000);
    debug.machine.set_reg(3, 1);
    (void)debug.machine.step();
    CHECK(debug.machine.reg(2) == 0u);

    Program bad{0x73};
    bad.machine.set_reg(7, 4);
    bad.machine.set_reg(2, 0x00010000u);  // outside this machine's 64 KiB
    bad.machine.set_reg(3, 1);
    (void)bad.machine.step();
    CHECK(bad.machine.reg(2) == 0xFFFFFFFFu);

    // 5 and above are reserved: vector 3, not a silent no-op.
    Program reserved{0x73};
    reserved.machine.set_reg(7, 5);
    const kr232::StepResult result = reserved.machine.step();
    CHECK(result.stop == Stop::Trapped);
    CHECK(result.vector == 3u);
}

TEST_CASE(the_default_test_machine_cannot_address_user_ram) {
    // The hazard this documents: a write to user RAM in the 64 KiB machine fails
    // silently unless the test looks at the result, which cost four rounds of
    // wrong conclusions about the protection path.
    Machine small{0x10000};
    CHECK(small.memory().write8(0x08000000u, 0x50) == kr232::Access::Unmapped);
    Machine full{256u * 1024u * 1024u};
    CHECK(full.memory().write8(0x08000000u, 0x50) == kr232::Access::Ok);
}
TEST_CASE(a_shift_by_an_immediate_selects_its_own_direction) {
    // `ld r7, #1` then one immediate shift of each kind. The emulator sent every
    // immediate shift to the arithmetic-right default, so `shl r7, #2` shifted
    // right instead and produced 0: every scaled index in every compiled program
    // came out zero, and the resulting misaligned pointers cost twenty-five rounds
    // of investigation in the compiler.
    const struct {
        std::uint8_t opcode;
        std::uint32_t expected;
    } cases[] = {
        {0xD4, 4u},   // SHL by 2: 1 << 2
        {0xD5, 0u},   // SHR by 2: 1 >> 2
        {0xD6, 0u},   // SAR by 2: 1 >> 2
    };
    for (const auto& shift : cases) {
        Program program{0xC6, 0x70, 0x01, shift.opcode, 0x70, 0x02};
        CHECK(program.machine.step().stop == Stop::Retired);
        CHECK(program.machine.step().stop == Stop::Retired);
        CHECK(program.machine.reg(7) == shift.expected);
    }
}
