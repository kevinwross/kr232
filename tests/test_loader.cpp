#include "harness.h"

#include <kr232/loader.h>
#include <kr232/machine.h>

#include <cstdint>

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
