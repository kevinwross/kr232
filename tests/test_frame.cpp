#include "harness.h"

#include <kr232/devices.h>
#include <kr232/frame.h>

#include <cstdint>

using kr232::Devices;

namespace {

// Commits a `width` x `height` frame whose rows are written at `stride` bytes
// apart, with every visible pixel set to `colour`.
void commit_frame(Devices& devices, std::uint32_t width, std::uint32_t height,
                  std::uint32_t stride, std::uint32_t colour) {
    (void)devices.write(Devices::kBase + 4 * Devices::kPageSize + 0x04, 4, width);   // WIDTH
    (void)devices.write(Devices::kBase + 4 * Devices::kPageSize + 0x08, 4, height);  // HEIGHT
    if (stride != width * 4) {
        (void)devices.write(Devices::kBase + 4 * Devices::kPageSize + 0x0C, 4, stride);
    }
    const std::uint32_t back = 0x001E0000;  // the default BACK
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            (void)devices.aperture_write(Devices::kApertureBase + back + y * stride + x * 4, 4,
                                         colour);
        }
    }
    (void)devices.write(Devices::kBase + 4 * Devices::kPageSize + 0x20, 4, 1);  // FLIP
}

}  // namespace

TEST_CASE(the_empty_frame_hashes_to_the_fnv_offset_basis) {
    Devices devices;
    CHECK(kr232::frame_hash(devices) == 0xCBF29CE484222325ull);
    CHECK(kr232::frame_hash_text(0xCBF29CE484222325ull) == "cbf29ce484222325");
}

TEST_CASE(the_same_pixels_hash_the_same_and_a_changed_pixel_does_not) {
    Devices first;
    commit_frame(first, 8, 4, 32, 0x00112233u);
    Devices second;
    commit_frame(second, 8, 4, 32, 0x00112233u);
    CHECK(kr232::frame_hash(first) == kr232::frame_hash(second));
    CHECK(kr232::frame_hash(first) != 0xCBF29CE484222325ull);

    Devices changed;
    commit_frame(changed, 8, 4, 32, 0x00112234u);
    CHECK(kr232::frame_hash(changed) != kr232::frame_hash(first));
}

TEST_CASE(stride_padding_does_not_change_the_hash) {
    Devices tight;
    commit_frame(tight, 8, 4, 32, 0x0000FF00u);
    Devices padded;
    commit_frame(padded, 8, 4, 64, 0x0000FF00u);  // same visible pixels, wider rows
    CHECK(kr232::frame_hash(tight) == kr232::frame_hash(padded));
}

TEST_CASE(an_uncommitted_frame_does_not_hash_the_aperture_bytes) {
    Devices devices;
    // Pixels written but never committed must not enter the hash.
    (void)devices.aperture_write(Devices::kApertureBase + 0x001E0000, 4, 0x00FFFFFFu);
    CHECK(kr232::frame_hash(devices) == 0xCBF29CE484222325ull);
}

TEST_CASE(the_hash_text_is_sixteen_lowercase_hex_digits) {
    CHECK(kr232::frame_hash_text(0).size() == 16);
    CHECK(kr232::frame_hash_text(0) == "0000000000000000");
    CHECK(kr232::frame_hash_text(0xDEADBEEFCAFEF00Dull) == "deadbeefcafef00d");
}
