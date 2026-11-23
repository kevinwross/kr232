#include <kr232/version.h>

#include <cstdio>
#include <cstring>

namespace {

void usage(std::FILE* out) {
    std::fputs(
        "usage: kr232-emu <command> [options]\n"
        "  vm <image>   run an image (kr232-isa.md, kr232-devices.md)\n"
        "  --version    print the version and exit\n"
        "  --help       print this text and exit\n",
        out);
}

}  // namespace

// The command names are fixed by kr232-impl.md §1: krcompiler's
// RunKr232Image.cmake launches `kr232-emu vm <image> --max-steps <n>`, so the
// subcommand is `vm`. The machine itself arrives with M1's decoder and executor.
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
    if (std::strcmp(argv[1], "vm") == 0) {
        std::fputs("kr232-emu: the machine is not implemented yet (M1)\n", stderr);
        return 2;
    }
    usage(stderr);
    return 2;
}
