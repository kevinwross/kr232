#include <kr232/decoder.h>
#include <kr232/loader.h>
#include <kr232/machine.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Prints `address: bytes  text` for one instruction, or one byte as data when the
// decoder refuses it -- a disassembler has to keep going through a byte stream it
// does not own.
void disassemble(const std::vector<std::uint8_t>& bytes, std::uint32_t base, std::uint32_t address,
                 std::uint32_t size) {
    std::uint32_t offset = 0;
    while (offset < size) {
        const std::uint32_t remaining = size - offset;
        const std::uint8_t* at = bytes.data() + base + offset;
        const auto span = std::span<const std::uint8_t>(at, remaining < 8 ? remaining : 8);
        const kr232::Decoded decoded = kr232::decode(span);
        if (!decoded.ok || decoded.length == 0 || decoded.length > remaining) {
            std::printf("0x%08x: %02x          .byte 0x%02x\n", address + offset, at[0], at[0]);
            ++offset;
            continue;
        }
        std::string hex;
        char pair[4] = {};
        for (unsigned i = 0; i < decoded.length; ++i) {
            std::snprintf(pair, sizeof pair, "%02x", at[i]);
            hex += pair;
            if (i + 1 < decoded.length) {
                hex.push_back(' ');
            }
        }
        std::printf("0x%08x: %-24s %s\n", address + offset, hex.c_str(), decoded.text.c_str());
        offset += decoded.length;
    }
}

bool read_file(const char* path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fputs("usage: kr232-dis <image>\n", stderr);
        return 2;
    }
    std::vector<std::uint8_t> bytes;
    if (!read_file(argv[1], bytes)) {
        std::fprintf(stderr, "kr232-dis: cannot read '%s'\n", argv[1]);
        return 2;
    }
    // The disassembler reads the container itself rather than loading a machine:
    // it must decode what is in the file, not what a loader decided to map.
    if (bytes.size() >= 4 && bytes[0] == 'K' && bytes[1] == '2' && bytes[2] == '3' && bytes[3] == '2') {
        if (bytes.size() < 64) {
            std::fputs("kr232-dis: container shorter than its header\n", stderr);
            return 1;
        }
        const auto count = static_cast<std::uint16_t>(bytes[22] | (bytes[23] << 8));
        const std::uint32_t table = static_cast<std::uint32_t>(bytes[32]) |
                                    (static_cast<std::uint32_t>(bytes[33]) << 8) |
                                    (static_cast<std::uint32_t>(bytes[34]) << 16) |
                                    (static_cast<std::uint32_t>(bytes[35]) << 24);
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::size_t at = table + static_cast<std::size_t>(i) * 32;
            const std::uint16_t flags = static_cast<std::uint16_t>(bytes[at + 6] | (bytes[at + 7] << 8));
            const std::uint32_t address = static_cast<std::uint32_t>(bytes[at + 8]) |
                                          (static_cast<std::uint32_t>(bytes[at + 9]) << 8) |
                                          (static_cast<std::uint32_t>(bytes[at + 10]) << 16) |
                                          (static_cast<std::uint32_t>(bytes[at + 11]) << 24);
            const std::uint32_t offset = static_cast<std::uint32_t>(bytes[at + 12]) |
                                         (static_cast<std::uint32_t>(bytes[at + 13]) << 8) |
                                         (static_cast<std::uint32_t>(bytes[at + 14]) << 16) |
                                         (static_cast<std::uint32_t>(bytes[at + 15]) << 24);
            const std::uint32_t size = static_cast<std::uint32_t>(bytes[at + 16]) |
                                       (static_cast<std::uint32_t>(bytes[at + 17]) << 8) |
                                       (static_cast<std::uint32_t>(bytes[at + 18]) << 16) |
                                       (static_cast<std::uint32_t>(bytes[at + 19]) << 24);
            if ((flags & 4u) == 0) {
                continue;  // not executable
            }
            if (static_cast<std::uint64_t>(offset) + size > bytes.size()) {
                std::fputs("kr232-dis: a section runs past the container\n", stderr);
                return 1;
            }
            std::printf("; section 0x%08x, %u bytes\n", address, size);
            disassemble(bytes, offset, address, size);
        }
        return 0;
    }
    // A flat image is a kernel image: its reset word at 0x0100 names the entry, so
    // that is where disassembly starts, and the bytes before it are its data.
    std::uint32_t entry = 0;
    if (bytes.size() >= 0x104) {
        entry = static_cast<std::uint32_t>(bytes[0x100]) |
                (static_cast<std::uint32_t>(bytes[0x101]) << 8) |
                (static_cast<std::uint32_t>(bytes[0x102]) << 16) |
                (static_cast<std::uint32_t>(bytes[0x103]) << 24);
        if (entry >= bytes.size()) {
            entry = 0;
        }
    }
    disassemble(bytes, entry, entry, static_cast<std::uint32_t>(bytes.size()) - entry);
    return 0;
}
