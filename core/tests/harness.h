// tests/harness.h
//
// Enough of a test framework to not need one. Deliberately not Catch2 or
// GoogleTest: the core has one vendored dependency and adding a second to run
// its tests would be the larger cost.
#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace harness {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case> &registry() {
    static std::vector<Case> r;
    return r;
}

inline int &failures() {
    static int f = 0;
    return f;
}

inline std::string &currentCase() {
    static std::string c;
    return c;
}

struct Register {
    Register(const std::string &name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline void fail(const char *file, int line, const std::string &msg) {
    ++failures();
    std::fprintf(stderr, "  FAIL %s:%d  %s\n", file, line, msg.c_str());
}

template <class A, class B>
void checkEq(const char *file, int line, const A &a, const B &b, const char *expr) {
    if (a == b) return;
    std::ostringstream ss;
    ss << expr << "\n        got      " << a << "\n        expected " << b;
    fail(file, line, ss.str());
}

inline void checkTrue(const char *file, int line, bool v, const char *expr) {
    if (!v) fail(file, line, std::string("expected true: ") + expr);
}

inline int run() {
    int cases = 0;
    for (auto &c : registry()) {
        currentCase() = c.name;
        int before = failures();
        c.fn();
        ++cases;
        if (failures() > before) std::fprintf(stderr, "  in case: %s\n", c.name.c_str());
    }
    std::fprintf(stderr, "%d cases, %d failures\n", cases, failures());
    return failures() == 0 ? 0 : 1;
}

}  // namespace harness

#define TEST(name)                                                      \
    static void name();                                                 \
    static harness::Register reg_##name(#name, name);                   \
    static void name()

#define CHECK_EQ(a, b) harness::checkEq(__FILE__, __LINE__, (a), (b), #a " == " #b)
#define CHECK(x) harness::checkTrue(__FILE__, __LINE__, (x), #x)
