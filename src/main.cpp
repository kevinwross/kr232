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
#include <vector>
#include <fstream>
#include <string>
#include <vector>

namespace {

void usage(std::FILE* out) {
    std::fputs(
        "usage: kr232-emu <command> [options]\n"
        "  vm <image> [--base <hex>] [--max-steps <n>] [--frame-hash]\n"
        "               run a flat image; --base 0 reads the entry from 0x0100\n"
   "               --input <text> queues console input, --user <image> runs a user program\n"
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
    std::string user_path;
    std::uint64_t max_steps = 1000000;
    bool want_frame_hash = false;
    bool want_trace = false;
    bool want_watch = false;
    std::uint32_t watch_address = 0;
    bool want_break = false;
    std::uint32_t break_address = 0;
    unsigned break_limit = 1;
    unsigned break_hits = 0;
    bool want_fp = false;
    bool want_page = false;
    std::uint32_t page_base = 0;
    std::vector<std::uint32_t> page_before;
    std::deque<std::pair<std::uint32_t, std::string>> trace;
    bool want_transcript = false;
    // Text queued on the console before the run, so a program that reads input
    // can be tested without a terminal. It is the same queue the device uses.
    std::string input_text;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frame-hash") == 0) {
            want_frame_hash = true;
        } else if (std::strcmp(argv[i], "--trace") == 0) {
            want_trace = true;
        } else if (std::strcmp(argv[i], "--break") == 0 && i + 1 < argc) {
            break_address = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 0));
            want_break = true;

        } else if (std::strcmp(argv[i], "--watch") == 0 && i + 1 < argc) {
            watch_address = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 0));
            want_watch = true;
        } else if (std::strcmp(argv[i], "--watch-fp") == 0) {
            want_fp = true;
        } else if (std::strcmp(argv[i], "--watch-page") == 0 && i + 1 < argc) {
            page_base = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 0)) & ~0xFFFu;
            want_page = true;
        } else if (std::strcmp(argv[i], "--break-max") == 0 && i + 1 < argc) {
            break_limit = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 0));
        } else if (std::strcmp(argv[i], "--transcript") == 0) {
            want_transcript = true;
        } else if (std::strcmp(argv[i], "--base") == 0 && i + 1 < argc) {
            base = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        } else if (std::strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            user_path = argv[++i];
        } else if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_text = argv[++i];
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

    // A user image is preloaded where user-mode code must live; its entry reaches
    // the kernel in R2, which is where the kernel reads the address it enters.
    if (!user_path.empty()) {
        std::vector<std::uint8_t> user_image;
        if (!read_file(user_path.c_str(), user_image)) {
            std::fprintf(stderr, "kr232-emu: cannot read '%s'\n", user_path.c_str());
            return 2;
        }
        // Loading a user image points PC at its entry, which is not where the
        // machine should start: the kernel runs first and enters the program
        // itself. Keep the kernel's PC across the load.
        const std::uint32_t kernel_pc = machine.pc();
        const kr232::LoadOutcome user_loaded =
            kr232::load_image(machine, user_image, 0x08000000u);
        machine.set_pc(kernel_pc);
        if (user_loaded.ok && user_loaded.entry < 0x08000000u) {
            // The image carries its own base, so one linked at the default base
            // lands below user RAM and its first fetch traps. Say which mistake
            // it is instead of letting the program fault with a bare vector 8.
            std::fprintf(stderr,
                         "kr232-emu: the user image is based at 0x%08X, below user RAM; "
                         "link it with --base=0x08000000\n",
                         static_cast<unsigned>(user_loaded.entry));
            return 2;
        }
        if (!user_loaded.ok) {
            std::fprintf(stderr, "kr232-emu: user image: %s\n", user_loaded.error.c_str());
            return 2;
        }
        machine.set_reg(2, user_loaded.entry);
    }

    for (const char ch : input_text) {
        (void)machine.devices().console_push_input(static_cast<std::uint8_t>(ch));
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
        if (want_break && machine.pc() == break_address) {
            // The state at a named instruction: the argument registers, the frame and
            // stack pointers, and the words just below the stack pointer, which is
            // where a callee's incoming slots live.
            std::printf("break at 0x%08x: r2=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x r6=0x%08x r7=0x%08x r8=0x%08x fp=0x%08x sp=0x%08x\n",
                        step_pc, machine.reg(2), machine.reg(3), machine.reg(4), machine.reg(5),
                        machine.reg(6), machine.reg(7), machine.reg(8), machine.reg(13),
                        machine.reg(15));
            for (int word = -2; word <= 4; ++word) {
                std::uint32_t value = 0;
                const std::int64_t at = static_cast<std::int64_t>(machine.reg(8)) +
                                        static_cast<std::int64_t>(4 * word);
                if (at >= 0) {
                    (void)machine.memory().read32(static_cast<std::uint32_t>(at), value);
                }
                std::printf("  [r8%+3d] = 0x%08x\n", 4 * word, value);
            }
            for (int word = 1; word <= 4; ++word) {
                std::uint32_t value = 0;
                (void)machine.memory().read32(machine.reg(13) - static_cast<std::uint32_t>(4 * word), value);
                std::printf("  [fp-%2d] = 0x%08x\n", 4 * word, value);
            }
            if (++break_hits >= break_limit) {
                return 0;
            }
        }
        if (want_page) {
            if (page_before.empty()) {
                page_before.assign(1024, 0);
                for (std::uint32_t word = 0; word < 1024; ++word) {
                    (void)machine.memory().read32(page_base + 4 * word, page_before[word]);
                }
            }
        }
        const std::uint32_t fp_before = machine.reg(13);
        std::uint32_t watched_before = 0;
        if (want_watch) {
            (void)machine.memory().read32(watch_address, watched_before);
        }
        const kr232::StepResult result = machine.step();
        ++steps;
        if (want_watch) {
            std::uint32_t watched_after = 0;
            (void)machine.memory().read32(watch_address, watched_after);
            if (watched_after != watched_before) {
                std::printf("watch 0x%08x at pc=0x%08x: 0x%08x -> 0x%08x\n", watch_address,
                            step_pc, watched_before, watched_after);
            }
        }
        if (want_fp && machine.reg(13) != fp_before) {
            std::printf("fp 0x%08x -> 0x%08x at pc=0x%08x\n", fp_before, machine.reg(13), step_pc);
        }
        if (want_page) {
            for (std::uint32_t word = 0; word < 1024; ++word) {
                std::uint32_t value = 0;
                (void)machine.memory().read32(page_base + 4 * word, value);
                if (value != page_before[word]) {
                    std::printf("page 0x%08x at pc=0x%08x: 0x%08x -> 0x%08x\n",
                                page_base + 4 * word, step_pc, page_before[word], value);
                    page_before[word] = value;
                }
            }
        }
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
    // The program's SYS status is the process exit code, in full: a test that
    // exits 300 must be distinguishable from one that exits 44.
    return machine.halted() ? static_cast<int>(machine.exit_status()) : 0;
}
