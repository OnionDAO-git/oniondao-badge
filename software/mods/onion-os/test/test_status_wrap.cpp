// Native unit test for status update line wrapping in drawAboutScreen.
// Compile and run:  g++ -std=c++17 -o test_status_wrap test/test_status_wrap.cpp && ./test_status_wrap

#include <cassert>
#include <cstdio>
#include <string>

// Mirrors Arduino String::substring(start, end)
static std::string substring(const std::string& s, int start, int end = -1) {
    if (start >= (int)s.size()) return "";
    if (end < 0 || end > (int)s.size()) end = (int)s.size();
    if (end <= start) return "";
    return s.substr(start, end - start);
}

// Mirrors Arduino String::length()
static int len(const std::string& s) { return (int)s.size(); }

// Reproduces the wrapping logic from drawAboutScreen.
// Returns (first_line, second_line).  second_line is empty if no wrap needed.
static std::pair<std::string, std::string> wrapStatus(const std::string& body, int maxFirst) {
    if (len(body) <= maxFirst) {
        return {body, ""};
    }
    std::string first = substring(body, 0, maxFirst);
    std::string rest  = substring(body, maxFirst);
    return {first, rest};
}

static void check(const std::string& body, int maxFirst,
                  const std::string& expectFirst, const std::string& expectRest) {
    auto [first, rest] = wrapStatus(body, maxFirst);
    if (first != expectFirst || rest != expectRest) {
        std::fprintf(stderr,
            "FAIL:\n  body       = \"%s\"\n  got first  = \"%s\"\n  expect     = \"%s\"\n"
            "  got rest   = \"%s\"\n  expect     = \"%s\"\n",
            body.c_str(), first.c_str(), expectFirst.c_str(),
            rest.c_str(), expectRest.c_str());
        assert(false);
    }
}

int main() {
    // "LastStatus: " is 12 chars.  Screen fits ~24 chars total → maxFirst = 12.
    const int MAX = 12;

    // 1. Body fits entirely on first line
    check("hello", MAX, "hello", "");

    // 2. Body exactly maxFirst chars — no wrap
    check("123456789012", MAX, "123456789012", "");

    // 3. One char over — wraps the single extra char
    check("1234567890123", MAX, "123456789012", "3");

    // 4. Word inside limit, overflow chars wrap
    //    "how is this overflowed" → first 12 = "how is this ", rest = "overflowed"
    check("how is this overflowed", MAX, "how is this ", "overflowed");

    // 5. No spaces at all — hard cut at maxFirst
    check("abcdefghijklmno", MAX, "abcdefghijkl", "mno");

    // 6. Empty body
    check("", MAX, "", "");

    // 7. Second line gets clipped to 22 by caller (2-char indent + 22 = 24)
    std::string longBody = "short and then a very long remainder that goes on and on";
    auto [f, r] = wrapStatus(longBody, MAX);
    assert(f == "short and th");
    assert(r == "en a very long remainder that goes on and on");
    // Caller does: "  " + clipped(r, 22) → "  en a very long remaind.."

    std::printf("All %d tests passed.\n", 7);
    return 0;
}
