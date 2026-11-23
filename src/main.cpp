#include <kr232/frame.h>
#include <kr232/loader.h>
#include <kr232/decoder.h>
#include <span>
#include <kr232/machine.h>
#include <kr232/version.h>

#include <cstdio>
#include <deque>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

void usage(std::FILE* out) {
    std::fputs(
        "usage: kr232-emu <command> [options]\n"
        "  vm <image> [--base <hex>] [--max-steps <n>] [--frame-hash]\n"
        "               run a flat image; --base 0 reads the entry from 0x0100\n"
        "  --version    print the version and exit\n"
        "  --help       print this text and exit\n",
        out);
}

bool read_file(const char* path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

}  // namespace

// The command names are fixed by kr232-impl.md §1: krcompiler's
// RunKr232Image.cmake launches `kr232-emu vm <image> --max-steps <n>`.
int main(int argc, char** argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    if (std::strcmp(argv[1], "--version") == 0) {
        const auto text = kr232::version();
        std::printf("%.*s\n", static_cast<int>(text.size()), text.data());
        return 0;
    }
    if (std::strcmp(argv[1], "--help") == 0) {
        usage(stdout);
        return 0;
    }
    if (std::strcmp(argv[1], "vm") != 0) {
        usage(stderr);
        return 2;
    }
    if (argc < 3) {
        std::fputs("kr232-emu: vm needs an image path\n", stderr);
        return 2;
    }

    const char* image_path = argv[2];
    std::uint32_t base = 0;
    std::uint64_t max_steps = 1000000;
    bool want_frame_hash = false;
    bool want_trace = false;
    std::deque<std::pair<std::uint32_t, std::string>> trace;
    bool want_transcript = false;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frame-hash") == 0) {
            want_frame_hash = true;
        } else if (std::strcmp(argv[i], "--trace") == 0) {
            want_trace = true;
        } else if (std::strcmp(argv[i], "--transcript") == 0) {
            want_transcript = true;
        } else if (std::strcmp(argv[i], "--base") == 0 && i + 1 < argc) {
            base = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        } else if (std::strcmp(argv[i], "--max-steps") == 0 && i + 1 < argc) {
            max_steps = std::strtoull(argv[++i], nullptr, 0);
        } else {
            std::fprintf(stderr, "kr232-emu: unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    std::vector<std::uint8_t> image;
    if (!read_file(image_path, image)) {
        std::fprintf(stderr, "kr232-emu: cannot read '%s'\n", image_path);
        return 2;
    }

    kr232::Machine machine;
    const kr232::LoadOutcome loaded = kr232::load_image(machine, image, base);
    if (!loaded.ok) {
        std::fprintf(stderr, "kr232-emu: %s\n", loaded.error.c_str());
        return 2;
    }

    std::uint64_t steps = 0;
    while (steps < max_steps) {
        const std::uint32_t step_pc = machine.pc();
        if (want_trace) {
            // The instruction about to run, so a trap names its cause. Decoded here
            // because the machine has already moved on after the step.
            std::uint8_t bytes[8] = {};
            for (std::size_t i = 0; i < sizeof bytes; ++i) {
                std::uint8_t byte = 0;
                if (machine.memory().read8(step_pc + static_cast<std::uint32_t>(i), byte) ==
                    kr232::Access::Ok) {
                    bytes[i] = byte;
                }
            }
            const kr232::Decoded decoded = kr232::decode(std::span<const std::uint8_t>(bytes));
            trace.emplace_back(step_pc, decoded.ok ? decoded.text : std::string("(data)"));
            if (trace.size() > 40) {
                trace.pop_front();
            }
        }
        const kr232::StepResult result = machine.step();
        ++steps;
        if (result.stop == kr232::Stop::Halted) {
            std::printf("halted status %u after %llu steps\n", static_cast<unsigned>(result.status),
                        static_cast<unsigned long long>(steps));
            break;
        }
        if (result.stop == kr232::Stop::Trapped) {
            std::fprintf(stderr, "trap %u at 0x%08x after %llu steps\n", result.vector, machine.pc(),
                         static_cast<unsigned long long>(steps));
            // The last few instructions before the fault, so a trap names its cause
            // instead of only its effect. Kept in a ring so a long run costs nothing.
            if (want_trace) {
                std::fprintf(stderr, "trace:\n");
                for (const auto& entry : trace) {
                    std::fprintf(stderr, "  0x%08x %s\n", entry.first, entry.second.c_str());
                }
                // The registers a memory fault depends on, so a bad address can be
                // traced to the register that held it.
                std::fprintf(stderr, "fp=0x%08x sp=0x%08x r6=0x%08x r7=0x%08x r8=0x%08x\n",
                             machine.reg(13), machine.reg(15), machine.reg(6), machine.reg(7),
                             machine.reg(8));
            }
            return 1;
        }
    }
    if (steps >= max_steps && !machine.halted()) {
        std::printf("stopped after %llu steps\n", static_cast<unsigned long long>(steps));
    }
    if (want_frame_hash) {
        std::printf("frame %s\n",
                    kr232::frame_hash_text(kr232::frame_hash(machine.devices())).c_str());
    }
    // The terminal grid is what a program wrote to the console; printing it is how
    // a transcript check reads the screen without a renderer.
    if (want_transcript) {
        const auto& devices = machine.devices();
        // The console page's bytes first: they are what a program wrote, whether
        // or not a renderer ever saw them.
        std::string console;
        for (const std::uint8_t byte : devices.console_output()) {
            if (byte >= 0x20 || byte == '\n' || byte == '\t') {
                console.push_back(static_cast<char>(byte));
            }
        }
        if (!console.empty()) {
            std::printf("console:\n%s\n", console.c_str());
        }
        std::printf("transcript:\n");
        for (std::uint32_t y = 0; y < devices.terminal_rows(); ++y) {
            std::string row;
            for (std::uint32_t x = 0; x < devices.terminal_columns(); ++x) {
                row.push_back(static_cast<char>(devices.terminal_cell(x, y).ch));
            }
            while (!row.empty() && row.back() == ' ') {
                row.pop_back();
            }
            std::printf("%s\n", row.c_str());
        }
    }
    // The program's SYS exit status is the process status (kr232-plan 6).
    return machine.halted() ? machine.exit_status() : 0;
}
