// tests/test_grammar.cpp
#include "aced/grammar.h"

#include "aced/document.h"
#include "aced/tokenizer.h"
#include "harness.h"

using namespace aced;

static Grammar &corpus() {
    static Grammar g;
    static bool loaded = g.loadFile(ACED_GRAMMARS_PATH);
    (void)loaded;
    return g;
}

TEST(corpus_loads) {
    CHECK(corpus().error().empty());
    CHECK(corpus().modes().size() > 150);
}

TEST(known_modes_present) {
    for (const char *m : {"javascript", "python", "c_cpp", "json", "yaml", "golang"})
        CHECK(corpus().has(m));
}

TEST(unknown_mode_is_null_not_a_throw) {
    CHECK(corpus().tokenizer("no_such_language_here") == nullptr);
}

TEST(tokenizer_is_cached) {
    const Tokenizer *a = corpus().tokenizer("javascript");
    const Tokenizer *b = corpus().tokenizer("javascript");
    CHECK(a != nullptr);
    CHECK(a == b);
}

TEST(tokenizes_javascript) {
    const Tokenizer *tk = corpus().tokenizer("javascript");
    CHECK(tk != nullptr);
    if (!tk) return;
    auto r = tk->tokenize("function f(a) { return a * 2; }", "start");
    CHECK(r.tokens.size() > 5);
    CHECK_EQ(r.tokens[0].type, std::string("storage.type"));
    CHECK_EQ(r.tokens[0].value, std::string("function"));
}

TEST(state_threads_across_rows) {
    const Tokenizer *tk = corpus().tokenizer("c_cpp");
    CHECK(tk != nullptr);
    if (!tk) return;
    Document d("/* open\nstill inside\n*/ done");
    std::string state = "start";
    std::vector<std::string> stack;
    std::vector<std::string> firstTypes;
    for (int r = 0; r < d.lineCount(); ++r) {
        auto res = tk->tokenize(d.line(r), state, stack);
        state = res.state;
        stack = res.stack;
        firstTypes.push_back(res.tokens.empty() ? "" : res.tokens[0].type);
    }
    // Row 1 is entirely inside the block comment opened on row 0; if state were
    // not threaded it would come back as plain text.
    CHECK(firstTypes[1].find("comment") != std::string::npos);
}

TEST(keyword_table_survived_the_export) {
    const Tokenizer *tk = corpus().tokenizer("python");
    CHECK(tk != nullptr);
    if (!tk) return;
    auto r = tk->tokenize("import os", "start");
    CHECK(!r.tokens.empty());
    if (!r.tokens.empty()) CHECK_EQ(r.tokens[0].type, std::string("keyword"));
}

TEST(every_grammar_builds_without_a_broken_state) {
    int modes = 0, withBadStates = 0;
    for (const auto &m : corpus().modes()) {
        const Tokenizer *tk = corpus().tokenizer(m);
        if (!tk) continue;
        ++modes;
        if (!tk->badStates().empty()) ++withBadStates;
    }
    std::fprintf(stderr, "    %d grammars built, %d with a rejected regex\n", modes, withBadStates);
    CHECK(modes > 150);
    // A handful use JS-only regex features PCRE2 will not take; the rule is
    // skipped and the rest of the grammar still works. Guard against that set
    // growing silently.
    CHECK(withBadStates <= 10);
}

TEST(mode_for_filename) {
    CHECK_EQ(Grammar::modeForFilename("main.cpp"), std::string("c_cpp"));
    CHECK_EQ(Grammar::modeForFilename("/a/b/app.py"), std::string("python"));
    CHECK_EQ(Grammar::modeForFilename("Makefile"), std::string("makefile"));
    CHECK_EQ(Grammar::modeForFilename("config.yml"), std::string("yaml"));
    CHECK_EQ(Grammar::modeForFilename("data.json"), std::string("json"));
    CHECK_EQ(Grammar::modeForFilename("noextension"), std::string());
}

TEST(tokens_tile_the_line_in_every_mode) {
    // THE INVARIANT EVERYTHING DOWNSTREAM ASSUMES. Token values laid end to end
    // must reconstruct the line exactly: LineCache turns them into format runs
    // by accumulating byte offsets, and TokenChain locates brackets the same
    // way. A dropped token is not a missing colour, it is a hole -- every run
    // after it lands one position to the left.
    //
    // This is how the javascript grammar's bare "[{}]" rule was found. It
    // produced an empty token type, the emit path dropped empty-typed tokens,
    // and every brace in every JS file vanished from the stream.
    static Grammar g;
    if (!g.loadFile(ACED_GRAMMARS_PATH)) { CHECK(false); return; }

    const std::vector<std::string> lines = {
        "function a() {",
        "} else {",
        "  if (x) { return \"}\"; }",
        "d = {'a': 1}",
        "#include <stdio.h>",
        "/* comment */ code();",
        "  x = [1, 2, 3]  # trailing",
        "<div class=\"a\">text</div>",
        "SELECT * FROM t WHERE a = 1;",
        "",
        "   ",
        "\tindented\twith\ttabs",
    };

    int checked = 0, holes = 0;
    std::string firstBad;
    for (const std::string &mode : g.modes()) {
        const Tokenizer *tk = g.tokenizer(mode);
        if (!tk) continue;
        std::string state = "start";
        std::vector<std::string> stack;
        for (const std::string &line : lines) {
            auto res = tk->tokenize(line, state, stack);
            std::string rebuilt;
            for (const auto &t : res.tokens) rebuilt += t.value;
            ++checked;
            if (rebuilt != line) {
                ++holes;
                if (firstBad.empty())
                    firstBad = mode + ": [" + line + "] -> [" + rebuilt + "]";
            }
            state = res.state;
            stack = res.stack;
        }
    }
    std::fprintf(stderr, "    %d mode/line pairs tiled, %d holes\n", checked, holes);
    if (holes && !firstBad.empty())
        std::fprintf(stderr, "    first: %s\n", firstBad.c_str());
    CHECK_EQ(holes, 0);
}
