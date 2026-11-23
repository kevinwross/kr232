#include <kr232/loader.h>

#include <cstddef>
#include <vector>

namespace kr232 {

LoadOutcome load_flat(Machine& machine, std::span<const std::uint8_t> image, std::uint32_t base) {
    LoadOutcome outcome;
    if (image.empty()) {
        outcome.error = "the image is empty";
        return outcome;
    }
    // The host may fill the null page, so the fit check is against RAM size
    // rather than the guest's readable range.
    if (static_cast<std::uint64_t>(base) + image.size() > machine.memory().ram_bytes()) {
        outcome.error = "the image does not fit in RAM at that base";
        return outcome;
    }
    if (!machine.memory().load(base, image)) {
        outcome.error = "the image does not fit in RAM at that base";
        return outcome;
    }
    if (base == 0) {
        std::uint32_t entry = 0;
        if (machine.memory().read32(0x100, entry) != Access::Ok) {
            outcome.error = "no reset word at 0x0100";
            return outcome;
        }
        machine.set_pc(entry);
        outcome.ok = true;
        outcome.entry = entry;
        return outcome;
    }
    machine.set_pc(base);
    outcome.ok = true;
    outcome.entry = base;
    return outcome;
}

namespace {

std::uint16_t read16(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

std::uint32_t read32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

constexpr std::uint32_t kHeaderSize = 64;
constexpr std::uint32_t kSectionRecordSize = 32;

}  // namespace

LoadOutcome load_k232(Machine& machine, std::span<const std::uint8_t> image) {
    LoadOutcome outcome;
    if (image.size() < kHeaderSize) {
        outcome.error = "the image is shorter than the container header";
        return outcome;
    }
    if (image[0] != 'K' || image[1] != '2' || image[2] != '3' || image[3] != '2') {
        outcome.error = "the container magic is not K232";
        return outcome;
    }
    if (read16(image, 4) != 1) {
        outcome.error = "the container version is not 1";
        return outcome;
    }
    if (read16(image, 6) != 2) {
        outcome.error = "the container kind is not an executable";
        return outcome;
    }
    if (read32(image, 12) != kHeaderSize) {
        outcome.error = "the header size is not 64";
        return outcome;
    }
    if (read16(image, 24) != 5) {
        outcome.error = "the target id is not 5 (kr232)";
        return outcome;
    }
    if (read16(image, 20) != 1) {
        outcome.error = "the ABI version is not 1";
        return outcome;
    }
    if (read32(image, 44) != image.size()) {
        outcome.error = "the file size does not match the buffer";
        return outcome;
    }
    const std::uint32_t stored_crc = read32(image, 48);
    if (stored_crc != 0) {
        std::vector<std::uint8_t> copy(image.begin(), image.end());
        copy[48] = copy[49] = copy[50] = copy[51] = 0;
        if (crc32(copy) != stored_crc) {
            outcome.error = "the checksum does not match";
            return outcome;
        }
    }

    const std::uint32_t base = read32(image, 28);
    if (base != 0 && base != 0x08000000u) {
        outcome.error = "the image base is neither 0 nor 0x08000000";
        return outcome;
    }
    const std::uint32_t limit = base == 0 ? 0x08000000u : 0x10000000u;
    const std::uint32_t entry = read32(image, 16);
    if (entry < base || entry >= limit) {
        outcome.error = "the entry is outside the image's half of the memory map";
        return outcome;
    }
    const std::uint16_t count = read16(image, 22);
    const std::uint32_t table = read32(image, 32);
    if (static_cast<std::uint64_t>(table) + static_cast<std::uint64_t>(count) * kSectionRecordSize >
        image.size()) {
        outcome.error = "the section table runs past the image";
        return outcome;
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t at = table + static_cast<std::size_t>(i) * kSectionRecordSize;
        const std::uint32_t address = read32(image, at + 8);
        const std::uint32_t offset = read32(image, at + 12);
        const std::uint32_t size = read32(image, at + 16);
        const std::uint16_t flags = read16(image, at + 6);
        if (flags == 0) {
            continue;  // unallocated, such as debug data
        }
        if (address < base || static_cast<std::uint64_t>(address) + size > limit) {
            outcome.error = "a section leaves the image's half of the memory map";
            return outcome;
        }
        if (static_cast<std::uint64_t>(offset) + size > image.size()) {
            outcome.error = "a section payload runs past the image";
            return outcome;
        }
    }
    // Everything is valid: write it.
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t at = table + static_cast<std::size_t>(i) * kSectionRecordSize;
        const std::uint32_t address = read32(image, at + 8);
        const std::uint32_t offset = read32(image, at + 12);
        const std::uint32_t size = read32(image, at + 16);
        const std::uint16_t flags = read16(image, at + 6);
        if (flags == 0 || size == 0) {
            continue;
        }
        (void)machine.memory().load(address, image.subspan(offset, size));
    }
    if (base == 0) {
        // The header's entry is authoritative over the image's reset word.
        const std::uint8_t word[4] = {
            static_cast<std::uint8_t>(entry & 0xFF),
            static_cast<std::uint8_t>((entry >> 8) & 0xFF),
            static_cast<std::uint8_t>((entry >> 16) & 0xFF),
            static_cast<std::uint8_t>((entry >> 24) & 0xFF),
        };
        (void)machine.memory().load(0x100, std::span<const std::uint8_t>(word, 4));
    }
    machine.set_pc(entry);
    outcome.ok = true;
    outcome.entry = entry;
    return outcome;
}

LoadOutcome load_image(Machine& machine, std::span<const std::uint8_t> image,
                       std::uint32_t flat_base) {
    if (image.size() >= 4 && image[0] == 'K' && image[1] == '2' && image[2] == '3' &&
        image[3] == '2') {
        return load_k232(machine, image);
    }
    return load_flat(machine, image, flat_base);
}

}  // namespace kr232
