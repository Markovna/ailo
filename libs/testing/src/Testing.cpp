#include "Testing.h"

#include <exception>
#include <iostream>
#include <mutex>

namespace ailo::test {

namespace {

std::mutex gMutex;
std::string gCurrentTest;
int gChecks = 0;
int gFailures = 0;
int gTests = 0;
int gFailedTests = 0;
bool gCurrentFailed = false;

void report(std::string_view message, const std::source_location* where) {
    gFailures++;
    gCurrentFailed = true;
    if (where) {
        std::cerr << where->file_name() << ":" << where->line() << ": ";
    }
    if (!gCurrentTest.empty()) {
        std::cerr << "[" << gCurrentTest << "] ";
    }
    std::cerr << "FAILED: " << message << std::endl;
}

}

void run(std::string_view name, const std::function<void()>& test) {
    {
        std::lock_guard lock(gMutex);
        gCurrentTest = name;
        gCurrentFailed = false;
        gTests++;
    }

    try {
        test();
    } catch (const RequireFailed&) {
        // already reported
    } catch (const std::exception& e) {
        std::lock_guard lock(gMutex);
        report(std::string("unexpected exception: ") + e.what(), nullptr);
    } catch (...) {
        std::lock_guard lock(gMutex);
        report("unexpected exception", nullptr);
    }

    std::lock_guard lock(gMutex);
    if (gCurrentFailed) {
        gFailedTests++;
    }
    gCurrentTest.clear();
}

void fail(std::string_view message, std::source_location where) {
    std::lock_guard lock(gMutex);
    gChecks++;
    report(message, &where);
}

int failureCount() {
    std::lock_guard lock(gMutex);
    return gFailures;
}

int finish() {
    std::lock_guard lock(gMutex);
    if (gFailures == 0) {
        std::cout << "All " << gTests << " tests passed (" << gChecks << " checks)" << std::endl;
        return 0;
    }
    std::cerr << gFailures << " of " << gChecks << " checks failed, in " << gFailedTests << " of " << gTests
              << " tests" << std::endl;
    return 1;
}

namespace detail {

bool check(bool ok, std::string_view text, std::source_location where) {
    std::lock_guard lock(gMutex);
    gChecks++;
    if (!ok) {
        report(text, &where);
    }
    return ok;
}

}

}
