#include <kr232/memory.h>

#include <stdexcept>

namespace kr232 {
namespace {

struct Row {
    Region region;
    std::uint32_t base;
    std::uint32_t size;
    const char* name;
};

// ISA §2.3, in address order. The rows tile the whole 4 GiB space.
constexpr Row kRows[] = {
    {Region::NullPage, 0x00000000, 0x00000100, "null page"},
    {Region::KernelRam, 0x00000100, 0x07FFFF00, "kernel RAM"},
    {Region::UserRam, 0x08000000, 0x08000000, "user RAM"},
    {Region::Unbacked, 0x10000000, 0xEC000000, "unbacked gap"},
    {Region::Aperture, 0xFC000000, 0x02000000, "framebuffer aperture"},
    {Region::Unbacked, 0xFE000000, 0x01F00000, "unbacked gap"},
    {Region::Devices, 0xFFF00000, 0x000F0000, "device window"},
    {Region::Reserved, 0xFFFF0000, 0x00010000, "reserved"},
};

static_assert(sizeof(kRows) / sizeof(kRows[0]) == 8, "ISA section 2.3 has eight rows");

}  // namespace

RegionSpan region_of(std::uint32_t address) noexcept {
    for (const Row& row : kRows) {
        const std::uint64_t base = row.base;
        const std::uint64_t end = base + row.size;  // exclusive
        if (address >= base && address < end) {
            return RegionSpan{row.region, row.base, row.size};
        }
    }
    // The rows tile 4 GiB, so this is unreachable for a 32-bit address.
    return RegionSpan{Region::Reserved, 0xFFFF0000u, 0x00010000u};
}

const char* to_string(Region region) noexcept {
    switch (region) {
        case Region::NullPage:
            return "null page";
        case Region::KernelRam:
            return "kernel RAM";
        case Region::UserRam:
            return "user RAM";
        case Region::Unbacked:
            return "unbacked gap";
        case Region::Aperture:
            return "framebuffer aperture";
        case Region::Devices:
            return "device window";
        case Region::Reserved:
            return "reserved";
    }
    return "unknown";
}

namespace {

// Validation happens before the buffer exists: a 256 MiB allocation made only to
// throw it away is not a diagnostic, it is a stall.
std::size_t validated_ram_size(std::uint32_t ram_bytes) {
    if (ram_bytes > Memory::kRamSize) {
        throw std::invalid_argument("kr232::Memory: ram_bytes exceeds the 256 MiB map");
    }
    if (ram_bytes < Memory::kFirstRamByte) {
        throw std::invalid_argument("kr232::Memory: ram_bytes must cover the reset vector");
    }
    return ram_bytes;
}

}  // namespace

Memory::Memory(std::uint32_t ram_bytes) : ram_(validated_ram_size(ram_bytes)) {}

bool Memory::contains(std::uint32_t address, std::uint32_t width) const noexcept {
    if (width == 0) {
        return false;
    }
    // 64-bit arithmetic: a 4-byte access at 0xFFFFFFFE must not wrap into RAM.
    const std::uint64_t first = address;
    const std::uint64_t last = first + width;  // exclusive
    return first >= kFirstRamByte && last <= ram_.size();
}

Access Memory::read8(std::uint32_t address, std::uint8_t& out) const noexcept {
    if (!contains(address, 1)) {
        return Access::Unmapped;
    }
    out = ram_[address];
    return Access::Ok;
}

Access Memory::read16(std::uint32_t address, std::uint16_t& out) const noexcept {
    if (!contains(address, 2)) {
        return Access::Unmapped;
    }
    out = static_cast<std::uint16_t>(ram_[address]) |
          static_cast<std::uint16_t>(static_cast<std::uint16_t>(ram_[address + 1]) << 8);
    return Access::Ok;
}

Access Memory::read32(std::uint32_t address, std::uint32_t& out) const noexcept {
    if (!contains(address, 4)) {
        return Access::Unmapped;
    }
    out = static_cast<std::uint32_t>(ram_[address]) |
          (static_cast<std::uint32_t>(ram_[address + 1]) << 8) |
          (static_cast<std::uint32_t>(ram_[address + 2]) << 16) |
          (static_cast<std::uint32_t>(ram_[address + 3]) << 24);
    return Access::Ok;
}

bool Memory::load(std::uint32_t address, std::span<const std::uint8_t> bytes) noexcept {
    if (static_cast<std::uint64_t>(address) + bytes.size() > ram_.size()) {
        return false;
    }
    std::copy(bytes.begin(), bytes.end(),
              ram_.begin() + static_cast<std::ptrdiff_t>(address));
    return true;
}

Access Memory::write8(std::uint32_t address, std::uint8_t value) noexcept {
    if (!contains(address, 1)) {
        return Access::Unmapped;
    }
    ram_[address] = value;
    return Access::Ok;
}

Access Memory::write16(std::uint32_t address, std::uint16_t value) noexcept {
    if (!contains(address, 2)) {
        return Access::Unmapped;
    }
    ram_[address] = static_cast<std::uint8_t>(value & 0xFFu);
    ram_[address + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    return Access::Ok;
}

Access Memory::write32(std::uint32_t address, std::uint32_t value) noexcept {
    if (!contains(address, 4)) {
        return Access::Unmapped;
    }
    ram_[address] = static_cast<std::uint8_t>(value & 0xFFu);
    ram_[address + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    ram_[address + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    ram_[address + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    return Access::Ok;
}

}  // namespace kr232
