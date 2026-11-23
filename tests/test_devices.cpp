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
