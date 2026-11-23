#pragma once

// A deliberately small test harness: this repository has no third-party
// dependency, and a teaching VM's suite does not need one. A test is
// `TEST_CASE(name) { ... CHECK(...); }`; `main` runs every case and returns
// non-zero when any check failed.

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace kr232test {

struct Case {
    std::string_view name;
    void (*function)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int count = 0;
    return count;
}

inline int& case_failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(std::string_view name, void (*function)()) { registry().push_back({name, function}); }
};

inline void check(bool condition, std::string_view expression, const char* file, int line) {
    if (condition) {
        return;
    }
    ++failures();
    ++case_failures();
    std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", file, line, std::string(expression).c_str());
}

}  // namespace kr232test

#define TEST_CASE(name)                                                            \
    static void name();                                                            \
    [[maybe_unused]] static const ::kr232test::Registrar name##_registrar{#name,   \
                                                                         &name};   \
    static void name()

#define CHECK(expr) ::kr232test::check((expr), #expr, __FILE__, __LINE__)
