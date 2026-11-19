#include "Test.hpp"

#include <cstring>

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;      // run only the tests whose name contains this
    int ran = 0, failedTests = 0;
    for (const moltentest::Case& test : moltentest::cases()) {
        if (filter && !std::strstr(test.name, filter)) continue;
        int before = moltentest::failures();
        test.body();
        ran++;
        bool ok = moltentest::failures() == before;
        if (!ok) failedTests++;
        std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", test.name);
    }
    std::printf("\n%d tests, %d failed\n", ran, failedTests);
    return failedTests == 0 ? 0 : 1;
}
