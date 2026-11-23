#include "harness.h"

#include <kr232/memory.h>

#include <cstdint>
#include <stdexcept>

using kr232::Access;
using kr232::Memory;
using kr232::Region;
using kr232::region_of;

namespace {

bool region_is(Region expected, std::uint32_t address) {
    return region_of(address).region == expected;
}

}  // namespace

TEST_CASE(every_isa_row_starts_and_ends_where_the_map_says) {
    CHECK(region_is(Region::NullPage, 0x00000000));
    CHECK(region_is(Region::NullPage, 0x000000FF));
    CHECK(region_is(Region::KernelRam, 0x00000100));
    CHECK(region_is(Region::KernelRam, 0x07FFFFFF));
    CHECK(region_is(Region::UserRam, 0x08000000));
    CHECK(region_is(Region::UserRam, 0x0FFFFFFF));
    CHECK(region_is(Region::Unbacked, 0x10000000));
    CHECK(region_is(Region::Unbacked, 0xFBFFFFFF));
    CHECK(region_is(Region::Aperture, 0xFC000000));
    CHECK(region_is(Region::Aperture, 0xFDFFFFFF));
    CHECK(region_is(Region::Unbacked, 0xFE000000));
    CHECK(region_is(Region::Unbacked, 0xFFEFFFFF));
    CHECK(region_is(Region::Devices, 0xFFF00000));
    CHECK(region_is(Region::Devices, 0xFFFEFFFF));
    CHECK(region_is(Region::Reserved, 0xFFFF0000));
    CHECK(region_is(Region::Reserved, 0xFFFFFFFF));
}

TEST_CASE(the_rows_tile_the_whole_space_without_a_seam) {
    // Each row's last byte and the next row's first byte are adjacent, which is
    // what the lint proves for the table and this proves for the code.
    const std::uint32_t boundaries[] = {0x00000100u, 0x08000000u, 0x10000000u, 0xFC000000u,
                                        0xFE000000u, 0xFFF00000u, 0xFFFF0000u};
    for (const std::uint32_t boundary : boundaries) {
        CHECK(region_of(boundary - 1).region != region_of(boundary).region);
        CHECK(region_of(boundary - 1).end() == boundary);
    }
}

TEST_CASE(ram_is_little_endian_at_every_width) {
    Memory memory(0x10000);
    CHECK(memory.write8(0x1000, 0xA5u) == Access::Ok);
    CHECK(memory.write16(0x1001, 0x1234u) == Access::Ok);
    CHECK(memory.write32(0x1004, 0xDEADBEEFu) == Access::Ok);

    std::uint8_t byte = 0;
    std::uint16_t half = 0;
    std::uint32_t word = 0;
    CHECK(memory.read8(0x1000, byte) == Access::Ok);
    CHECK(byte == 0xA5u);
    CHECK(memory.read16(0x1001, half) == Access::Ok);
    CHECK(half == 0x1234u);
    CHECK(memory.read32(0x1004, word) == Access::Ok);
    CHECK(word == 0xDEADBEEFu);

    // The low byte of the halfword sits at the lower address.
    CHECK(memory.read8(0x1001, byte) == Access::Ok);
    CHECK(byte == 0x34u);
    CHECK(memory.read8(0x1002, byte) == Access::Ok);
    CHECK(byte == 0x12u);
}

TEST_CASE(a_trap_row_is_never_served_even_though_it_is_inside_the_buffer) {
    Memory memory(0x10000);
    std::uint8_t byte = 0;
    std::uint32_t word = 0;
    CHECK(memory.read8(0x00000000, byte) == Access::Unmapped);
    CHECK(memory.write8(0x00000000, 0x11u) == Access::Unmapped);
    CHECK(memory.read32(0x000000FC, word) == Access::Unmapped);
    // The first byte after the null page is ordinary kernel RAM.
    CHECK(memory.write8(0x00000100, 0x22u) == Access::Ok);
}

TEST_CASE(the_aperture_the_devices_and_the_reserved_page_are_not_ram) {
    // Classification is by address, so a short machine answers this without
    // allocating 256 MiB to prove a fact that does not depend on the buffer.
    Memory memory(0x10000);
    std::uint32_t word = 0;
    CHECK(region_is(Region::Aperture, 0xFC000000));
    CHECK(memory.read32(0xFC000000, word) == Access::Unmapped);
    CHECK(memory.write32(0xFFF00000, 0x1u) == Access::Unmapped);
    CHECK(memory.read32(0xFFFF0000, word) == Access::Unmapped);
}

TEST_CASE(an_access_that_crosses_the_end_of_ram_is_refused_whole) {
    Memory memory(0x10000);
    std::uint32_t word = 0;
    CHECK(memory.write32(0x0000FFFE, 0x11223344u) == Access::Unmapped);
    CHECK(memory.read32(0x0000FFFE, word) == Access::Unmapped);
    // Nothing was written by the refused store.
    std::uint8_t byte = 0;
    CHECK(memory.read8(0x0000FFFE, byte) == Access::Ok);
    CHECK(byte == 0x00u);
}

TEST_CASE(a_full_machine_refuses_the_first_byte_of_the_gap) {
    Memory memory;
    CHECK(memory.contains(0x0FFFFFFC, 4));
    CHECK(!memory.contains(0x0FFFFFFD, 4));
    CHECK(!memory.contains(0x10000000, 1));
    std::uint32_t word = 0;
    CHECK(memory.read32(0x0FFFFFFC, word) == Access::Ok);
    CHECK(memory.read32(0x10000000, word) == Access::Unmapped);
}

TEST_CASE(a_short_machine_is_a_configuration_error_when_it_is_absurd) {
    bool threw = false;
    try {
        Memory too_big(Memory::kRamSize + 1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        Memory too_small(0x80);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}
