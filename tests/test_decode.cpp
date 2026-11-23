#include "harness.h"

#include <kr232/decoder.h>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The appendix pairs assembly text with bytes; the vectors are the contract the
// decoder must reproduce, so the test reads the document rather than a copy.
struct Vector {
    std::string assembly;
    std::vector<std::uint8_t> bytes;
};

std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string first_token(const std::string& text) {
    const auto end = text.find(' ');
    return text.substr(0, end);
}

// Returns the text between the `from`-th backtick pair (0-based) and the offset
// just after the closing backtick.
bool backticked(const std::string& line, std::size_t index, std::string& out, std::size_t& after) {
    std::size_t open = std::string::npos;
    std::size_t count = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '`') {
            continue;
        }
        if (count == index * 2) {
            open = i;
        } else if (count == index * 2 + 1 && open != std::string::npos) {
            out = line.substr(open + 1, i - open - 1);
            after = i + 1;
            return true;
        }
        ++count;
    }
    return false;
}

bool parse_bytes(const std::string& text, std::vector<std::uint8_t>& out) {
    out.clear();
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }
        if (i + 1 >= text.size() || !std::isxdigit(static_cast<unsigned char>(text[i])) ||
            !std::isxdigit(static_cast<unsigned char>(text[i + 1]))) {
            return false;
        }
        out.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
        i += 2;
    }
    return !out.empty();
}

std::vector<Vector> read_vectors() {
    std::ifstream file(KR232_ISA_PATH);
    std::vector<Vector> vectors;
    if (!file) {
        return vectors;
    }
    std::string line;
    bool in_appendix = false;
    while (std::getline(file, line)) {
        if (line.rfind("## Appendix A", 0) == 0) {
            in_appendix = true;
            continue;
        }
        if (line.rfind("## Appendix B", 0) == 0) {
            break;
        }
        if (!in_appendix || line.rfind("| `", 0) != 0) {
            continue;
        }
        Vector vector;
        std::size_t after = 0;
        if (!backticked(line, 0, vector.assembly, after)) {
            continue;
        }
        std::string bytes_text;
        if (!backticked(line, 1, bytes_text, after)) {
            continue;
        }
        if (!parse_bytes(bytes_text, vector.bytes)) {
            continue;
        }
        vectors.push_back(std::move(vector));
    }
    return vectors;
}

}  // namespace

TEST_CASE(every_appendix_a_vector_decodes_to_its_mnemonic_and_length) {
    const std::vector<Vector> vectors = read_vectors();
    CHECK(vectors.size() == 81u);
    for (const Vector& vector : vectors) {
        const kr232::Decoded decoded = kr232::decode(vector.bytes);
        if (!decoded.ok) {
            std::fprintf(stderr, "  vector '%s': %s\n", vector.assembly.c_str(),
                         decoded.text.c_str());
        }
        CHECK(decoded.ok);
        CHECK(decoded.length == vector.bytes.size());
        const std::string expected = first_token(lowered(vector.assembly));
        const std::string actual = first_token(decoded.text);
        if (expected != actual) {
            std::fprintf(stderr, "  vector '%s': decoded '%s'\n", vector.assembly.c_str(),
                         decoded.text.c_str());
        }
        CHECK(expected == actual);
    }
}

TEST_CASE(a_truncated_instruction_is_not_decoded_as_something_else) {
    const std::uint8_t short_form[] = {0x52u, 0x34u};
    const auto decoded = kr232::decode(short_form);
    CHECK(!decoded.ok);
}

TEST_CASE(reserved_bytes_are_illegal) {
    const std::uint8_t reserved[] = {0xF2u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    CHECK(!kr232::decode(reserved).ok);
    const std::uint8_t reserved_fc[] = {0xFCu};
    CHECK(!kr232::decode(reserved_fc).ok);
    const std::uint8_t reserved_7a[] = {0x7Au};
    CHECK(!kr232::decode(reserved_7a).ok);
}

TEST_CASE(a_compact_unary_operand_must_name_r0) {
    // NEG R2 is `9D 20`; `9D 21` sets Rb = R1 and is illegal.
    const std::uint8_t neg_ok[] = {0x9Du, 0x20u};
    const std::uint8_t neg_bad[] = {0x9Du, 0x21u};
    CHECK(kr232::decode(neg_ok).ok);
    CHECK(!kr232::decode(neg_bad).ok);
}

TEST_CASE(f4_is_three_bytes_before_its_displacement_and_four_after) {
    // `fst f1, [r2]` is `F4 14 20`: no displacement byte.
    const std::uint8_t no_disp[] = {0xF4u, 0x14u, 0x20u};
    const auto plain = kr232::decode(no_disp);
    CHECK(plain.ok);
    CHECK(plain.length == 3u);

    // `fsts f1, [r2+4]` is `F4 35 20 04`: mode 01 adds one byte.
    const std::uint8_t with_disp[] = {0xF4u, 0x35u, 0x20u, 0x04u};
    const auto displaced = kr232::decode(with_disp);
    CHECK(displaced.ok);
    CHECK(displaced.length == 4u);
}

TEST_CASE(a_generic_form_never_carries_two_extensions) {
    // MOV R2, #imm16 is legal (mode 1); an address in operand 2 with an
    // immediate in operand 1 is not encodable because operand 1 must be a
    // register, so the illegal shape is two extension-taking modes.
    const std::uint8_t generic_ok[] = {0xF0u, 0x00u, 0x02u, 0x10u, 0x00u, 0x40u};
    const auto ok = kr232::decode(generic_ok);
    CHECK(ok.ok);
    CHECK(ok.length == 6u);

    const std::uint8_t generic_bad[] = {0xF0u, 0x00u, 0x15u, 0x15u, 0x00u, 0x00u};
    CHECK(!kr232::decode(generic_bad).ok);
}
