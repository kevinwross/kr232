#include "harness.h"

#include <kr232/loader.h>
#include <kr232/machine.h>

#include <cstdint>
#include <vector>

namespace {

const std::uint8_t kNop = 0x71;
const std::uint8_t kSys = 0x73;

}  // namespace

TEST_CASE(a_kernel_image_at_base_zero_takes_its_entry_from_the_reset_word) {
    kr232::Machine machine(0x10000);
    // nop at 0x0104, and the reset word pointing there.
    std::uint8_t image[0x108] = {};
    image[0x104] = kNop;
    image[0x100] = 0x04;
    image[0x101] = 0x01;

    const auto outcome = kr232::load_flat(machine, image, 0);
    CHECK(outcome.ok);
    CHECK(outcome.entry == 0x0104u);
    CHECK(machine.pc() == 0x0104u);
    CHECK(machine.step().stop == kr232::Stop::Retired);
    CHECK(machine.pc() == 0x0105u);
}

TEST_CASE(a_user_image_at_a_nonzero_base_is_its_own_entry) {
    kr232::Machine machine(0x10000);
    const std::uint8_t image[] = {kNop, kSys};
    const auto outcome = kr232::load_flat(machine, image, 0x08000000);
    // 0x08000000 is outside the short machine's RAM, so this must be refused.
    CHECK(!outcome.ok);

    kr232::Machine big(0x08000100);
    const auto placed = kr232::load_flat(big, image, 0x08000000);
    CHECK(placed.ok);
    CHECK(placed.entry == 0x08000000u);
    CHECK(big.pc() == 0x08000000u);
    CHECK(big.step().stop == kr232::Stop::Retired);
    CHECK(big.pc() == 0x08000001u);
}

TEST_CASE(an_empty_image_is_refused) {
    kr232::Machine machine(0x10000);
    const auto outcome = kr232::load_flat(machine, std::span<const std::uint8_t>(), 0x1000);
    CHECK(!outcome.ok);
    CHECK(!outcome.error.empty());
}

TEST_CASE(an_image_that_does_not_fit_is_refused_whole) {
    kr232::Machine machine(0x10000);
    std::uint8_t image[0x200] = {};
    image[0] = kNop;
    // A base near the end of RAM: the whole image cannot fit, and nothing of it
    // may be written.
    const auto outcome = kr232::load_flat(machine, image, 0xFF00);
    CHECK(!outcome.ok);
    std::uint8_t byte = 0xAA;
    CHECK(machine.memory().read8(0xFF00, byte) == kr232::Access::Ok);
    CHECK(byte == 0);
}

namespace {

// A minimal `.kr232`: header, one executable section at 0x0200 holding `nop`, its
// record and a string table. The CRC field is left zero, which the container
// defines as "absent", so the tests below exercise the other validations.
std::vector<std::uint8_t> make_container(std::uint32_t entry, std::uint16_t target, std::uint32_t section_address, std::uint32_t crc) {
    std::vector<std::uint8_t> image(64 + 1 + 32 + 6, 0);
    image[0] = 'K'; image[1] = '2'; image[2] = '3'; image[3] = '2';
    image[4] = 1;              // container version
    image[6] = 2;              // kind executable
    image[12] = 64;            // header size
    image[16] = static_cast<std::uint8_t>(entry & 0xFF);
    image[17] = static_cast<std::uint8_t>((entry >> 8) & 0xFF);
    image[20] = 1;             // ABI version
    image[22] = 1;             // one section
    image[24] = static_cast<std::uint8_t>(target & 0xFF);
    image[25] = static_cast<std::uint8_t>((target >> 8) & 0xFF);
    const std::uint32_t size = static_cast<std::uint32_t>(image.size());
    image[44] = static_cast<std::uint8_t>(size & 0xFF);
    image[45] = static_cast<std::uint8_t>((size >> 8) & 0xFF);
    image[46] = static_cast<std::uint8_t>((size >> 16) & 0xFF);
    image[47] = static_cast<std::uint8_t>((size >> 24) & 0xFF);
    // The single payload byte then the table at 65, the strings at 97.
    image[64] = 0x71;          // nop
    const std::uint32_t table = 65;
    for (unsigned i = 0; i < 4; ++i) {
        image[32 + i] = static_cast<std::uint8_t>((table >> (8 * i)) & 0xFF);  // section table offset
    }
    const std::uint32_t strings = 97;
    for (unsigned i = 0; i < 4; ++i) {
        image[36 + i] = static_cast<std::uint8_t>((strings >> (8 * i)) & 0xFF);
        image[48 + i] = static_cast<std::uint8_t>((crc >> (8 * i)) & 0xFF);
    }
    image[40] = 6;             // string table size
    // Section record.
    image[table + 4] = 1;      // progbits
    image[table + 6] = 5;      // alloc | execute
    for (unsigned i = 0; i < 4; ++i) {
        image[table + 8 + i] = static_cast<std::uint8_t>((section_address >> (8 * i)) & 0xFF);
        image[table + 12 + i] = static_cast<std::uint8_t>((64u >> (8 * i)) & 0xFF);
    }
    image[table + 16] = 1;     // size
    image[table + 20] = 4;     // alignment
    const char name[] = ".text";
    for (unsigned i = 0; i < 6; ++i) {
        image[strings + i] = static_cast<std::uint8_t>(name[i]);
    }
    return image;
}

}  // namespace

TEST_CASE(a_k232_container_loads_its_section_and_entry) {
    kr232::Machine machine(0x10000);
    const auto image = make_container(0x0200, 5, 0x0200, 0);
    const auto outcome = kr232::load_k232(machine, image);
    CHECK(outcome.ok);
    CHECK(outcome.entry == 0x0200u);
    CHECK(machine.pc() == 0x0200u);
    CHECK(machine.step().stop == kr232::Stop::Retired);
    CHECK(machine.pc() == 0x0201u);
    std::uint8_t byte = 0;
    CHECK(machine.memory().read8(0x0200, byte) == kr232::Access::Ok);
    CHECK(byte == 0x71);
}

TEST_CASE(a_bad_magic_version_target_or_section_is_refused) {
    kr232::Machine machine(0x10000);
    std::string unused;

    auto wrong_target = make_container(0x0200, 4, 0x0200, 0);
    CHECK(!kr232::load_k232(machine, wrong_target).ok);

    auto outside = make_container(0x0200, 5, 0x09000000, 0);  // user RAM in a kernel image
    CHECK(!kr232::load_k232(machine, outside).ok);

    auto bad_crc = make_container(0x0200, 5, 0x0200, 0x12345678);
    CHECK(!kr232::load_k232(machine, bad_crc).ok);

    auto short_header = std::vector<std::uint8_t>(32, 0);
    CHECK(!kr232::load_k232(machine, short_header).ok);
}

TEST_CASE(load_image_dispatches_on_the_magic) {
    kr232::Machine machine(0x10000);
    const auto container = make_container(0x0200, 5, 0x0200, 0);
    CHECK(kr232::load_image(machine, container).ok);

    kr232::Machine flat_machine(0x10000);
    const std::uint8_t flat[] = {0x71};
    CHECK(kr232::load_image(flat_machine, flat, 0x1000).ok);
    CHECK(flat_machine.pc() == 0x1000u);
}