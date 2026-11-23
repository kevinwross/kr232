#include <kr232/frame.h>

namespace kr232 {
namespace {

constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ull;
constexpr std::uint64_t kFnvPrime = 0x00000100000001B3ull;

}  // namespace

std::uint64_t frame_hash(const Devices& devices) {
    std::uint64_t hash = kFnvOffset;
    const Devices::Frame frame = devices.committed_frame();
    const std::uint8_t* pixels = devices.frame_bytes();
    if (frame.number == 0 || pixels == nullptr) {
        return hash;  // nothing has been committed yet: a stable empty hash
    }
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(frame.width) * 4u;
    for (std::uint64_t y = 0; y < frame.height; ++y) {
        const std::uint64_t row = static_cast<std::uint64_t>(frame.offset) + y * frame.stride;
        for (std::uint64_t i = 0; i < row_bytes; ++i) {
            hash ^= pixels[row + i];
            hash *= kFnvPrime;
        }
    }
    return hash;
}

std::string frame_hash_text(std::uint64_t hash) {
    static const char* digits = "0123456789abcdef";
    std::string text(16, '0');
    for (unsigned i = 0; i < 16; ++i) {
        text[15 - i] = digits[(hash >> (4 * i)) & 0xFu];
    }
    return text;
}

}  // namespace kr232
