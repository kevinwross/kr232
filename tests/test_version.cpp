#include "harness.h"

#include <kr232/version.h>

#include <string_view>

TEST_CASE(version_names_the_machine) {
    const std::string_view text = kr232::version();
    CHECK(!text.empty());
    CHECK(text.rfind("kr232 ", 0) == 0);
}
