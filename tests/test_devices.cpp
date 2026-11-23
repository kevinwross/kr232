#include "harness.h"

#include <kr232/devices.h>
#include <kr232/machine.h>

#include <cstdint>

using kr232::DeviceResult;
using kr232::Devices;

namespace {

// A device access is a 32-bit word at a 4-byte offset; this helper writes that
// exact shape so the tests below read as register operations.
DeviceResult read_register(Devices& devices, std::uint32_t page, std::uint32_t offset,
                           std::uint32_t& value) {
    return devices.read(Devices::kBase + page * Devices::kPageSize + offset, 4, value);
}

DeviceResult write_register(Devices& devices, std::uint32_t page, std::uint32_t offset,
                            std::uint32_t value) {
    return devices.write(Devices::kBase + page * Devices::kPageSize + offset, 4, value);
}

}  // namespace

TEST_CASE(all_eight_populated_pages_answer_their_id) {
    Devices devices;
    const unsigned pages[] = {0, 2, 4, 6, 8, 9, 10, 11};
    const std::uint32_t ids[] = {0x544D5231u, 0x434F4E31u, 0x46425231u, 0x4B424431u,
                                 0x5445524Du, 0x47504831u, 0x53595331u, 0x424C5431u};
    for (unsigned i = 0; i < 8; ++i) {
        std::uint32_t value = 0;
        CHECK(read_register(devices, pages[i], Devices::kIdOffset, value) == DeviceResult::Ok);
        CHECK(value == ids[i]);
    }
}

TEST_CASE(a_reserved_page_and_an_undefined_offset_are_address_errors) {
    Devices devices;
    std::uint32_t value = 0;
    CHECK(read_register(devices, 12, 0x00, value) == DeviceResult::Reserved);
    CHECK(read_register(devices, 1, 0x00, value) == DeviceResult::Reserved);
    CHECK(read_register(devices, 0, 0x20, value) == DeviceResult::Undefined);
    CHECK(write_register(devices, 0, 0x20, 1) == DeviceResult::Undefined);
}

TEST_CASE(a_device_access_that_is_not_a_word_at_a_word_offset_is_misshaped) {
    Devices devices;
    std::uint32_t value = 0;
    CHECK(devices.read(Devices::kBase + 0x3C, 2, value) == DeviceResult::MisShaped);
    CHECK(devices.read(Devices::kBase + 0x3E, 4, value) == DeviceResult::MisShaped);
}

TEST_CASE(the_timer_counts_down_and_sets_pending) {
    Devices devices;
    CHECK(write_register(devices, 0, 0x04, 3) == DeviceResult::Ok);  // RELOAD also loads COUNT
    CHECK(write_register(devices, 0, 0x00, 1) == DeviceResult::Ok);  // enable, not periodic

    std::uint32_t count = 0;
    CHECK(read_register(devices, 0, 0x08, count) == DeviceResult::Ok);
    CHECK(count == 3);

    devices.tick(1);
    devices.tick(2);
    CHECK(read_register(devices, 0, 0x08, count) == DeviceResult::Ok);
    CHECK(count == 1);

    devices.tick(3);
    std::uint32_t status = 0;
    CHECK(read_register(devices, 0, 0x0C, status) == DeviceResult::Ok);
    CHECK((status & 1u) != 0);  // PENDING
    CHECK(read_register(devices, 0, 0x08, count) == DeviceResult::Ok);
    CHECK(count == 0);
    std::uint32_t control = 0;
    CHECK(read_register(devices, 0, 0x00, control) == DeviceResult::Ok);
    CHECK((control & 1u) == 0);  // one-shot clears the enable

    // Writing 1 to STATUS clears PENDING.
    CHECK(write_register(devices, 0, 0x0C, 1) == DeviceResult::Ok);
    CHECK(read_register(devices, 0, 0x0C, status) == DeviceResult::Ok);
    CHECK((status & 1u) == 0);
}

TEST_CASE(a_periodic_timer_reloads_itself) {
    Devices devices;
    CHECK(write_register(devices, 0, 0x04, 2) == DeviceResult::Ok);
    CHECK(write_register(devices, 0, 0x00, 3) == DeviceResult::Ok);  // enable | periodic
    for (std::uint64_t cycle = 1; cycle <= 5; ++cycle) {
        devices.tick(cycle);
    }
    std::uint32_t count = 0;
    CHECK(read_register(devices, 0, 0x08, count) == DeviceResult::Ok);
    CHECK(count == 1);
}

TEST_CASE(the_system_page_reports_the_cycle_counter_and_requests_a_halt) {
    kr232::Machine machine(0x10000);
    // The machine starts at the reset word (0 here), so give it an instruction
    // and a stack inside the short machine: the reset SP is at the top of RAM.
    (void)machine.memory().write8(0x1000, 0x71);
    machine.set_sp(0x8000);
    machine.set_pc(0x1000);
    for (int i = 0; i < 3; ++i) {
        (void)machine.step();
    }
    Devices& devices = machine.devices();
    std::uint32_t low = 0;
    CHECK(read_register(devices, 10, 0x08, low) == DeviceResult::Ok);
    CHECK(low == 3);

    CHECK(write_register(devices, 10, 0x00, 1) == DeviceResult::Ok);  // HALT
    CHECK(devices.halt_requested());
    (void)machine.step();  // the write itself is an instruction; it retires
    CHECK(machine.halted());
}

TEST_CASE(the_console_streams_one_byte_each_way) {
    Devices devices;
    CHECK(write_register(devices, 2, 0x00, 'A') == DeviceResult::Ok);  // OUT
    std::uint8_t byte = 0;
    CHECK(devices.console_pop_output(byte));
    CHECK(byte == 'A');

    std::uint32_t status = 0;
    CHECK(read_register(devices, 2, 0x08, status) == DeviceResult::Ok);
    CHECK((status & 0x2u) != 0);  // TX_READY is always set
    CHECK((status & 0x1u) == 0);  // nothing to read yet

    CHECK(devices.console_push_input('Z'));
    CHECK(read_register(devices, 2, 0x08, status) == DeviceResult::Ok);
    CHECK((status & 0x1u) != 0);

    std::uint32_t value = 0;
    CHECK(read_register(devices, 2, 0x04, value) == DeviceResult::Ok);  // IN
    CHECK(value == 'Z');
    CHECK(read_register(devices, 2, 0x04, value) == DeviceResult::Ok);
    CHECK(value == 0xFFFFFFFFu);  // empty
}

TEST_CASE(a_console_overrun_is_sticky_until_written) {
    Devices devices;
    for (std::size_t i = 0; i < Devices::kConsoleQueue; ++i) {
        CHECK(devices.console_push_input(static_cast<std::uint8_t>('a' + i)));
    }
    CHECK(!devices.console_push_input('!'));  // the arrival is dropped

    std::uint32_t status = 0;
    CHECK(read_register(devices, 2, 0x08, status) == DeviceResult::Ok);
    CHECK((status & 0x4u) != 0);  // OVERRUN

    CHECK(write_register(devices, 2, 0x08, 1) == DeviceResult::Ok);
    CHECK(read_register(devices, 2, 0x08, status) == DeviceResult::Ok);
    CHECK((status & 0x4u) == 0);
}

TEST_CASE(the_console_output_and_input_registers_are_one_directional) {
    Devices devices;
    std::uint32_t value = 1;
    CHECK(read_register(devices, 2, 0x00, value) == DeviceResult::Ok);  // OUT reads as zero
    CHECK(value == 0);
    CHECK(write_register(devices, 2, 0x04, 'x') == DeviceResult::Ok);  // IN writes are ignored
    std::uint8_t byte = 0;
    CHECK(!devices.console_pop_output(byte));
}

TEST_CASE(the_keyboard_fifo_reports_availability_and_count) {
    Devices devices;
    const std::uint32_t event = ('a') | (0x1u << 8) | (0x1Eu << 16) | (0x1u << 24);
    CHECK(devices.keyboard_push_event(event));

    std::uint32_t status = 0;
    CHECK(read_register(devices, 6, 0x04, status) == DeviceResult::Ok);
    CHECK((status & 0x1u) != 0);                       // AVAILABLE
    CHECK(((status >> 8) & 0xFFu) == 1);               // count
    std::uint32_t value = 0;
    CHECK(read_register(devices, 6, 0x00, value) == DeviceResult::Ok);
    CHECK(value == event);
    CHECK(read_register(devices, 6, 0x00, value) == DeviceResult::Ok);
    CHECK(value == 0xFFFFFFFFu);  // empty FIFO
    CHECK(read_register(devices, 6, 0x04, status) == DeviceResult::Ok);
    CHECK((status & 0x1u) == 0);
}

TEST_CASE(the_keyboard_fifo_drops_the_sixty_fifth_event_and_says_so) {
    Devices devices;
    for (std::size_t i = 0; i < Devices::kKeyboardFifo; ++i) {
        CHECK(devices.keyboard_push_event(static_cast<std::uint32_t>(i)));
    }
    CHECK(!devices.keyboard_push_event(0xDEADBEEFu));

    std::uint32_t status = 0;
    CHECK(read_register(devices, 6, 0x04, status) == DeviceResult::Ok);
    CHECK((status & 0x2u) != 0);  // OVERRUN
    CHECK(write_register(devices, 6, 0x04, 1) == DeviceResult::Ok);
    CHECK(read_register(devices, 6, 0x04, status) == DeviceResult::Ok);
    CHECK((status & 0x2u) == 0);
    std::uint32_t value = 0;
    CHECK(read_register(devices, 6, 0x00, value) == DeviceResult::Ok);
    CHECK(value == 0);  // the oldest event, not the dropped one
}

TEST_CASE(the_framebuffer_resets_to_800_by_600_with_back_above_it) {
    Devices devices;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::uint32_t back = 0;
    CHECK(read_register(devices, 4, 0x04, width) == DeviceResult::Ok);
    CHECK(read_register(devices, 4, 0x08, height) == DeviceResult::Ok);
    CHECK(read_register(devices, 4, 0x0C, stride) == DeviceResult::Ok);
    CHECK(read_register(devices, 4, 0x18, back) == DeviceResult::Ok);
    CHECK(width == 800);
    CHECK(height == 600);
    CHECK(stride == 3200);
    CHECK(back == 0x001E0000u);

    std::uint32_t status = 0;
    CHECK(read_register(devices, 4, 0x1C, status) == DeviceResult::Ok);
    CHECK((status & 0x1u) == 0);       // nothing committed yet
    CHECK((status & 0x2u) != 0);       // the default mode is valid
}

TEST_CASE(a_width_write_sets_the_stride_and_an_invalid_width_is_ignored) {
    Devices devices;
    CHECK(write_register(devices, 4, 0x04, 1024) == DeviceResult::Ok);
    std::uint32_t stride = 0;
    CHECK(read_register(devices, 4, 0x0C, stride) == DeviceResult::Ok);
    CHECK(stride == 4096);

    CHECK(write_register(devices, 4, 0x04, 0) == DeviceResult::Ok);  // out of range
    std::uint32_t width = 0;
    CHECK(read_register(devices, 4, 0x04, width) == DeviceResult::Ok);
    CHECK(width == 1024);
    CHECK(read_register(devices, 4, 0x0C, stride) == DeviceResult::Ok);
    CHECK(stride == 4096);
}

TEST_CASE(a_flip_commits_the_back_surface_swaps_them_and_counts_the_frame) {
    Devices devices;
    CHECK(write_register(devices, 4, 0x00, 1) == DeviceResult::Ok);  // enable
    CHECK(write_register(devices, 4, 0x18, 0x00200000) == DeviceResult::Ok);  // BACK
    CHECK(write_register(devices, 4, 0x20, 1) == DeviceResult::Ok);  // FLIP

    const Devices::Frame frame = devices.committed_frame();
    CHECK(frame.enabled);
    CHECK(frame.offset == 0x00200000u);  // the surface just drawn
    CHECK(frame.number == 1);
    CHECK(frame.width == 800);
    CHECK(frame.height == 600);

    std::uint32_t front = 0;
    std::uint32_t back = 0;
    CHECK(read_register(devices, 4, 0x14, front) == DeviceResult::Ok);
    CHECK(read_register(devices, 4, 0x18, back) == DeviceResult::Ok);
    CHECK(front == 0x00200000u);
    CHECK(back == 0);  // the two swapped

    std::uint32_t status = 0;
    CHECK(read_register(devices, 4, 0x1C, status) == DeviceResult::Ok);
    CHECK((status & 0x1u) != 0);  // committed enable
}

TEST_CASE(a_flip_that_fails_sets_the_error_and_changes_nothing) {
    Devices devices;
    CHECK(write_register(devices, 4, 0x08, 1081) == DeviceResult::Ok);  // rejected at write
    // Make the bundle invalid in a way a register write cannot refuse: an
    // offset whose surface would run past the aperture.
    CHECK(write_register(devices, 4, 0x18, 0x01FFFFFC) == DeviceResult::Ok);
    CHECK(write_register(devices, 4, 0x20, 1) == DeviceResult::Ok);  // FLIP must fail

    std::uint32_t status = 0;
    CHECK(read_register(devices, 4, 0x1C, status) == DeviceResult::Ok);
    CHECK((status & 0x4u) != 0);  // FLIP_ERROR
    CHECK((status & 0x2u) == 0);  // and MODE_OK says why
    CHECK(devices.committed_frame().number == 0);

    CHECK(write_register(devices, 4, 0x1C, 4) == DeviceResult::Ok);  // clear the error
    CHECK(read_register(devices, 4, 0x1C, status) == DeviceResult::Ok);
    CHECK((status & 0x4u) == 0);
}

TEST_CASE(the_aperture_is_byte_addressable_and_little_endian) {
    Devices devices;
    CHECK(devices.aperture_write(Devices::kApertureBase + 0x10, 4, 0x00112233u));
    std::uint32_t word = 0;
    CHECK(devices.aperture_read(Devices::kApertureBase + 0x10, 4, word));
    CHECK(word == 0x00112233u);
    std::uint32_t byte = 0;
    CHECK(devices.aperture_read(Devices::kApertureBase + 0x10, 1, byte));
    CHECK(byte == 0x33u);  // B first: XRGB8888 stores B, G, R, 0
    CHECK(devices.aperture_read(Devices::kApertureBase + 0x12, 1, byte));
    CHECK(byte == 0x11u);
    CHECK(!devices.aperture_read(Devices::kApertureBase + Devices::kApertureSize - 2, 4, word));
    CHECK(devices.frame_bytes() != nullptr);
}

namespace {

// Submits one graphics command: registers, then the doorbell.
void submit(Devices& devices, std::uint32_t op, std::initializer_list<std::uint32_t> args) {
    CHECK(write_register(devices, 9, 0x00, op) == DeviceResult::Ok);
    std::uint32_t offset = 0x04;
    for (const std::uint32_t arg : args) {
        CHECK(write_register(devices, 9, offset, arg) == DeviceResult::Ok);
        offset += 4;
    }
    CHECK(write_register(devices, 9, 0x24, 1) == DeviceResult::Ok);
}

std::uint32_t pixel(Devices& devices, std::uint32_t offset) {
    std::uint32_t value = 0;
    (void)devices.aperture_read(Devices::kApertureBase + offset, 4, value);
    return value;
}

}  // namespace

TEST_CASE(fill_rect_draws_inside_the_selected_surface) {
    Devices devices;
    kr232::Memory ram(0x10000);
    submit(devices, 8, {0, 4, 2, 16});        // SET_SURFACE: 4x2 at 0
    devices.run_graphics(ram);
    CHECK(devices.target_surface().width == 4);
    submit(devices, 3, {1, 0, 2, 1, 0x00FF0000u});  // FILL_RECT
    devices.run_graphics(ram);
    CHECK(pixel(devices, 0) == 0);            // x = 0 untouched
    CHECK(pixel(devices, 4) == 0x00FF0000u);  // x = 1
    CHECK(pixel(devices, 8) == 0x00FF0000u);  // x = 2
    CHECK(pixel(devices, 12) == 0);
    std::uint32_t status = 0;
    CHECK(read_register(devices, 9, 0x28, status) == DeviceResult::Ok);
    CHECK((status & 1u) == 0);  // no error
}

TEST_CASE(a_rectangle_that_leaves_the_surface_is_clamped_not_rejected) {
    Devices devices;
    kr232::Memory ram(0x10000);
    submit(devices, 8, {0, 4, 2, 16});
    devices.run_graphics(ram);
    submit(devices, 3, {3, 1, 10, 10, 0x0000FF00u});  // starts in, runs out
    devices.run_graphics(ram);
    std::uint32_t status = 0;
    CHECK(read_register(devices, 9, 0x28, status) == DeviceResult::Ok);
    CHECK((status & 1u) == 0);  // succeeds
    CHECK(pixel(devices, 3 * 4 + 16) == 0x0000FF00u);  // the one pixel that fits
}

TEST_CASE(an_invalid_surface_and_an_unknown_command_set_the_sticky_error) {
    Devices devices;
    kr232::Memory ram(0x10000);
    submit(devices, 8, {0, 4, 2, 8});  // stride below width*4
    devices.run_graphics(ram);
    std::uint32_t status = 0;
    CHECK(read_register(devices, 9, 0x28, status) == DeviceResult::Ok);
    CHECK((status & 1u) != 0);  // ERROR
    CHECK(devices.target_surface().width == 800);  // the default target is unchanged

    CHECK(write_register(devices, 9, 0x28, 1) == DeviceResult::Ok);  // clear
    CHECK(read_register(devices, 9, 0x28, status) == DeviceResult::Ok);
    CHECK((status & 1u) == 0);

    submit(devices, 77, {});  // reserved opcode
    devices.run_graphics(ram);
    CHECK(read_register(devices, 9, 0x28, status) == DeviceResult::Ok);
    CHECK((status & 1u) != 0);
}

TEST_CASE(the_flip_command_commits_a_frame) {
    Devices devices;
    kr232::Memory ram(0x10000);
    submit(devices, 7, {});  // FLIP
    devices.run_graphics(ram);
    CHECK(devices.committed_frame().number == 1);
    CHECK(devices.committed_frame().offset == 0x001E0000u);  // the default BACK
}

TEST_CASE(upload_copies_rows_from_guest_ram_into_the_surface) {
    Devices devices;
    kr232::Memory ram(0x10000);
    (void)ram.write32(0x2000, 0x00000011u);
    (void)ram.write32(0x2004, 0x00000022u);
    submit(devices, 8, {0, 2, 1, 8});                  // SET_SURFACE: 2x1 at 0
    devices.run_graphics(ram);
    submit(devices, 10, {0x2000, 0, 2, 1, 8});         // UPLOAD
    devices.run_graphics(ram);
    CHECK(pixel(devices, 0) == 0x00000011u);
    CHECK(pixel(devices, 4) == 0x00000022u);
}

TEST_CASE(the_terminal_grid_follows_the_committed_mode) {
    Devices devices;
    std::uint32_t status = 0;
    CHECK(read_register(devices, 8, 0x04, status) == DeviceResult::Ok);
    CHECK((status & 1u) != 0);                 // READY
    CHECK(((status >> 8) & 0xFFu) == 80);      // 800 / 10
    CHECK(((status >> 16) & 0xFFu) == 30);     // 600 / 20
    CHECK(devices.terminal_columns() == 80);
    CHECK(devices.terminal_rows() == 30);
}

TEST_CASE(a_terminal_write_stores_the_cell_and_mirrors_to_the_console) {
    Devices devices;
    CHECK(write_register(devices, 8, 0x10, 0x1Eu) == DeviceResult::Ok);  // fg 14, bg 1
    CHECK(write_register(devices, 8, 0x00, 'A') == DeviceResult::Ok);
    const Devices::Cell& cell = devices.terminal_cell(0, 0);
    CHECK(cell.ch == 'A');
    CHECK(cell.fg == 14);
    CHECK(cell.bg == 1);
    CHECK(devices.terminal_cursor_x() == 1);

    std::uint8_t mirrored = 0;
    CHECK(devices.console_pop_output(mirrored));
    CHECK(mirrored == 'A');
}

TEST_CASE(terminal_control_characters_move_the_cursor_and_clear) {
    Devices devices;
    CHECK(write_register(devices, 8, 0x00, 'a') == DeviceResult::Ok);
    CHECK(write_register(devices, 8, 0x00, 0x0D) == DeviceResult::Ok);  // CR
    CHECK(devices.terminal_cursor_x() == 0);
    CHECK(write_register(devices, 8, 0x00, 0x0A) == DeviceResult::Ok);  // LF
    CHECK(devices.terminal_cursor_y() == 1);
    CHECK(write_register(devices, 8, 0x00, 0x09) == DeviceResult::Ok);  // tab
    CHECK(devices.terminal_cursor_x() == 8);
    CHECK(write_register(devices, 8, 0x00, 0x7F) == DeviceResult::Ok);  // ignored
    CHECK(devices.terminal_cursor_x() == 8);
    CHECK(write_register(devices, 8, 0x0C, 1) == DeviceResult::Ok);     // clear
    CHECK(devices.terminal_cell(0, 1).ch == ' ');
    CHECK(devices.terminal_cursor_y() == 0);
}

TEST_CASE(a_write_at_the_bottom_row_scrolls_the_grid) {
    Devices devices;
    CHECK(write_register(devices, 8, 0x00, 'q') == DeviceResult::Ok);
    for (int i = 0; i < 31; ++i) {
        CHECK(write_register(devices, 8, 0x00, 0x0A) == DeviceResult::Ok);
    }
    CHECK(devices.terminal_cursor_y() == 29);
    CHECK(devices.terminal_cell(0, 0).ch != 'q');  // the top row scrolled away
}

TEST_CASE(the_blitter_copies_like_memmove) {
    Devices devices;
    kr232::Memory ram(0x10000);
    for (std::uint32_t i = 0; i < 8; ++i) {
        (void)ram.write8(0x2000 + i, static_cast<std::uint8_t>(i + 1));
    }
    CHECK(write_register(devices, 11, 0x00, 0x2000) == DeviceResult::Ok);  // SRC
    CHECK(write_register(devices, 11, 0x04, 0x2002) == DeviceResult::Ok);  // DST, overlapping
    CHECK(write_register(devices, 11, 0x08, 6) == DeviceResult::Ok);       // LEN
    CHECK(write_register(devices, 11, 0x10, 1) == DeviceResult::Ok);       // copy, 1-byte units
    devices.run_blitter(ram);

    std::uint32_t status = 0;
    CHECK(read_register(devices, 11, 0x14, status) == DeviceResult::Ok);
    CHECK((status & 1u) != 0);  // DONE
    // The six bytes read before any write: 0x2002..0x2007 now hold 1..6, and the
    // two source bytes below the destination are untouched.
    for (std::uint32_t i = 0; i < 6; ++i) {
        std::uint8_t byte = 0;
        CHECK(ram.read8(0x2002 + i, byte) == kr232::Access::Ok);
        CHECK(byte == static_cast<std::uint8_t>(i + 1));
    }
    std::uint8_t first = 0;
    std::uint8_t second = 0;
    CHECK(ram.read8(0x2000, first) == kr232::Access::Ok);
    CHECK(ram.read8(0x2001, second) == kr232::Access::Ok);
    CHECK(first == 1);
    CHECK(second == 2);
}

TEST_CASE(a_fill_writes_the_pattern_by_unit_and_a_trailing_partial_takes_low_bytes) {
    Devices devices;
    kr232::Memory ram(0x10000);
    CHECK(write_register(devices, 11, 0x04, 0x3000) == DeviceResult::Ok);  // DST
    CHECK(write_register(devices, 11, 0x08, 6) == DeviceResult::Ok);       // LEN
    CHECK(write_register(devices, 11, 0x0C, 0x0000AABB) == DeviceResult::Ok);  // VALUE
    CHECK(write_register(devices, 11, 0x10, 1u | 2u | (2u << 2)) == DeviceResult::Ok);
    devices.run_blitter(ram);  // fill, 4-byte units

    const std::uint8_t expected[6] = {0xBB, 0xAA, 0x00, 0x00, 0xBB, 0xAA};
    for (std::uint32_t i = 0; i < 6; ++i) {
        std::uint8_t byte = 0;
        CHECK(ram.read8(0x3000 + i, byte) == kr232::Access::Ok);
        CHECK(byte == expected[i]);
    }
}

TEST_CASE(a_descriptor_that_leaves_the_backed_ranges_sets_error_and_moves_nothing) {
    Devices devices;
    kr232::Memory ram(0x10000);
    (void)ram.write8(0x2000, 0x11);
    CHECK(write_register(devices, 11, 0x00, 0xFFFFF000) == DeviceResult::Ok);  // SRC in the gap
    CHECK(write_register(devices, 11, 0x04, 0x2000) == DeviceResult::Ok);
    CHECK(write_register(devices, 11, 0x08, 4) == DeviceResult::Ok);
    CHECK(write_register(devices, 11, 0x10, 1) == DeviceResult::Ok);
    devices.run_blitter(ram);

    std::uint32_t status = 0;
    CHECK(read_register(devices, 11, 0x14, status) == DeviceResult::Ok);
    CHECK((status & 2u) != 0);  // ERROR
    std::uint8_t byte = 0;
    CHECK(ram.read8(0x2000, byte) == kr232::Access::Ok);
    CHECK(byte == 0x11);  // nothing moved

    CHECK(write_register(devices, 11, 0x14, 2) == DeviceResult::Ok);  // write 1 clears
    CHECK(read_register(devices, 11, 0x14, status) == DeviceResult::Ok);
    CHECK((status & 2u) == 0);
}

TEST_CASE(a_zero_length_blit_succeeds) {
    Devices devices;
    kr232::Memory ram(0x10000);
    CHECK(write_register(devices, 11, 0x08, 0) == DeviceResult::Ok);  // LEN = 0
    CHECK(write_register(devices, 11, 0x10, 1) == DeviceResult::Ok);
    devices.run_blitter(ram);
    std::uint32_t status = 0;
    CHECK(read_register(devices, 11, 0x14, status) == DeviceResult::Ok);
    CHECK((status & 1u) != 0);  // DONE
}
