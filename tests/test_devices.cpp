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
