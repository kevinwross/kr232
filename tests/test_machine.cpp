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
    Machine machine{0x10000};

    explicit Program(std::initializer_list<std::uint8_t> code) {
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