#include "harness.h"

int main() {
    int cases = 0;
    for (const auto& test : kr232test::registry()) {
        const int before = kr232test::failures();
        test.function();
        ++cases;
        if (kr232test::failures() != before) {
            std::fprintf(stderr, "FAIL %s\n", std::string(test.name).c_str());
        }
    }
    std::printf("kr232_tests: %d case(s), %d failed check(s)\n", cases, kr232test::failures());
    return kr232test::failures() == 0 ? 0 : 1;
}
