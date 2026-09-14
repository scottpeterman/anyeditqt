// tests/test_foldmode.cpp
#include "aced/foldmode.h"

#include <memory>
#include <string>

#include "aced/document.h"
#include "aced/grammar.h"
#include "aced/tokenchain.h"
#include "aced/tokenizer.h"
#include "harness.h"

using aced::Document;
using aced::DocumentFoldSource;
using aced::FoldMode;
using aced::FoldRange;
using aced::FoldWidget;
using aced::Grammar;
using aced::TokenChain;

namespace {

// The real corpus, loaded once. A fold mode that works against a hand-written
// token list and not against ace's own grammar is not working.
const Grammar &corpus() {
    static Grammar g;
    static bool loaded = [] { return g.loadFile(ACED_GRAMMARS_PATH); }();
    (void)loaded;
    return g;
}

struct Fixture {
    Document doc;
    TokenChain chain;
    std::unique_ptr<DocumentFoldSource> src;
    const FoldMode *mode = nullptr;

    Fixture(const std::string &modeName, const std::string &text) : doc(text) {
        chain.setDocument(&doc);
        if (!modeName.empty()) chain.setTokenizer(corpus().tokenizer(modeName));
        src = std::make_unique<DocumentFoldSource>(&doc, &chain);
        mode = aced::foldModeFor(modeName);
    }

    FoldWidget widget(int row) const { return mode->widget(*src, row); }
    bool range(int row, FoldRange *r) const { return mode->range(*src, row, r); }
};

// Compact assertion: row `row` folds to endRow `end`.
void checkFold(const Fixture &f, int row, int end, const char *file, int line) {
    FoldRange r;
    if (!f.range(row, &r)) {
        harness::fail(file, line, "no fold range at row " + std::to_string(row));
        return;
    }
    harness::checkEq(file, line, r.endRow, end, "fold end row");
}

}  // namespace

#define CHECK_FOLD(f, row, end) checkFold((f), (row), (end), __FILE__, __LINE__)

// --- cstyle ----------------------------------------------------------------

TEST(a_brace_block_folds_to_its_closing_brace) {
    Fixture f("javascript",
              "function a() {\n"
              "    return 1;\n"
              "}\n"
              "var x = 2;\n");
    CHECK(f.mode != nullptr);
    CHECK(f.widget(0) == FoldWidget::Start);
    CHECK(f.widget(1) == FoldWidget::None);
    CHECK(f.widget(3) == FoldWidget::None);
    CHECK_FOLD(f, 0, 2);
}

TEST(a_brace_inside_a_string_is_not_structure) {
    // The whole reason folding needs the tokenizer. Plain bracket counting
    // closes this fold on row 1 and swallows nothing; worse, a lone '{' in a
    // string opens a fold that runs to the end of the file.
    Fixture f("javascript",
              "function a() {\n"
              "    var s = \"}\";\n"
              "    var t = '{';\n"
              "    return s + t;\n"
              "}\n"
              "done();\n");
    CHECK_FOLD(f, 0, 4);
}

TEST(a_brace_inside_a_line_comment_is_not_structure) {
    Fixture f("javascript",
              "function a() {\n"
              "    // }\n"
              "    return 1;\n"
              "}\n");
    CHECK_FOLD(f, 0, 3);
}

TEST(nested_blocks_each_fold_to_their_own_brace) {
    Fixture f("javascript",
              "function a() {\n"
              "    if (x) {\n"
              "        y();\n"
              "    }\n"
              "    return 1;\n"
              "}\n");
    CHECK_FOLD(f, 0, 5);
    CHECK_FOLD(f, 1, 3);
}

TEST(a_closing_line_that_opens_its_own_fold_stops_a_row_short) {
    // "} else {" is both an end and a start. Ace backs the first fold up one
    // row so the two nest; without it they overlap and folding the if hides
    // the else's own header.
    Fixture f("javascript",
              "if (x) {\n"
              "    a();\n"
              "} else {\n"
              "    b();\n"
              "}\n");
    CHECK_FOLD(f, 0, 1);
    CHECK_FOLD(f, 2, 4);
}

TEST(a_line_with_a_closing_bracket_after_the_opener_does_not_fold) {
    // "foo(1)" has an opener, but the marker requires nothing closing after it.
    Fixture f("javascript", "var x = foo(1);\nvar y = 2;\n");
    CHECK(f.widget(0) == FoldWidget::None);
}

TEST(a_trailing_open_paren_folds_like_a_brace) {
    Fixture f("javascript",
              "foo(\n"
              "    1,\n"
              "    2\n"
              ");\n");
    CHECK(f.widget(0) == FoldWidget::Start);
    CHECK_FOLD(f, 0, 3);
}

TEST(a_block_comment_folds_and_a_one_line_one_does_not) {
    Fixture f("javascript",
              "/*\n"
              " * hello\n"
              " */\n"
              "var x = 1;\n"
              "/* one line */\n"
              "var y = 2;\n");
    CHECK(f.widget(0) == FoldWidget::Start);
    CHECK_FOLD(f, 0, 2);
    CHECK(f.widget(4) == FoldWidget::None);
}

TEST(a_three_star_comment_folds_even_on_one_line) {
    Fixture f("javascript", "/*** banner ***/\nvar x = 1;\n");
    CHECK(f.widget(0) == FoldWidget::Start);
}

TEST(region_markers_fold_and_nest) {
    Fixture f("javascript",
              "//#region outer\n"
              "var a = 1;\n"
              "//#region inner\n"
              "var b = 2;\n"
              "//#endregion\n"
              "var c = 3;\n"
              "//#endregion\n"
              "var d = 4;\n");
    CHECK(f.widget(0) == FoldWidget::Start);
    CHECK(f.widget(2) == FoldWidget::Start);
    CHECK(f.widget(4) == FoldWidget::None);
    CHECK_FOLD(f, 0, 6);
    CHECK_FOLD(f, 2, 4);
}

TEST(a_region_without_the_pound_sign_still_folds) {
    Fixture f("javascript",
              "//region thing\n"
              "var a = 1;\n"
              "//endregion\n");
    CHECK(f.widget(0) == FoldWidget::Start);
    CHECK_FOLD(f, 0, 2);
}

TEST(regionalism_is_not_a_region) {
    // The \\b in ace's marker. Without it every identifier starting "region"
    // opens a fold that runs to the end of the file.
    Fixture f("javascript", "// regionalism\nvar a = 1;\n");
    CHECK(f.widget(0) == FoldWidget::None);
}

TEST(an_unclosed_brace_folds_nothing_rather_than_the_rest_of_the_file) {
    Fixture f("javascript", "function a() {\n    return 1;\n");
    FoldRange r;
    CHECK(!f.range(0, &r));
}

TEST(cstyle_works_on_a_c_file_too) {
    Fixture f("c_cpp",
              "int main(void) {\n"
              "    if (1) {\n"
              "        return 0;  /* } not a brace */\n"
              "    }\n"
              "    return 1;\n"
              "}\n");
    CHECK_FOLD(f, 0, 5);
    CHECK_FOLD(f, 1, 3);
}

// --- pythonic --------------------------------------------------------------

TEST(a_python_block_folds_by_indentation) {
    Fixture f("python",
              "def a():\n"
              "    x = 1\n"
              "    return x\n"
              "\n"
              "def b():\n"
              "    pass\n");
    CHECK(f.mode != nullptr);
    CHECK(f.widget(0) == FoldWidget::Start);
    CHECK(f.widget(1) == FoldWidget::None);
    // The blank line at row 3 is not part of the block.
    CHECK_FOLD(f, 0, 2);
    CHECK(f.widget(4) == FoldWidget::Start);
    CHECK_FOLD(f, 4, 5);
}

TEST(a_python_colon_with_a_trailing_comment_still_folds) {
    Fixture f("python",
              "if x:  # why\n"
              "    y()\n"
              "z()\n");
    CHECK(f.widget(0) == FoldWidget::Start);
    CHECK_FOLD(f, 0, 1);
}

TEST(a_python_bracket_at_end_of_line_folds_to_its_partner) {
    Fixture f("python",
              "d = {\n"
              "    'a': 1,\n"
              "}\n"
              "print(d)\n");
    CHECK(f.widget(0) == FoldWidget::Start);
    CHECK_FOLD(f, 0, 2);
}

TEST(a_python_block_does_not_end_inside_a_triple_quoted_string) {
    Fixture f("python",
              "def a():\n"
              "    s = \"\"\"\n"
              "text at column zero\n"
              "\"\"\"\n"
              "    return s\n"
              "b = 1\n");
    CHECK_FOLD(f, 0, 4);
}

// --- indentation (coffee family) -------------------------------------------

TEST(an_indented_block_folds_in_an_indentation_mode) {
    Fixture f("coffee",
              "foo = ->\n"
              "  bar()\n"
              "  baz()\n"
              "qux()\n");
    CHECK(f.mode != nullptr);
    CHECK(f.widget(0) == FoldWidget::Start);
    CHECK(f.widget(1) == FoldWidget::None);
    CHECK_FOLD(f, 0, 2);
}

TEST(blank_lines_before_an_indented_block_do_not_lose_the_widget) {
    // Ace decides this against the immediately next line and patches the
    // neighbours, which drops the widget when more than one blank line is in
    // the way. Looking past the blanks is the deviation, and it is deliberate.
    Fixture f("coffee",
              "foo = ->\n"
              "\n"
              "\n"
              "  bar()\n"
              "qux()\n");
    CHECK(f.widget(0) == FoldWidget::Start);
    CHECK_FOLD(f, 0, 3);
}

TEST(a_run_of_comment_lines_folds_from_its_first_row_only) {
    Fixture f("coffee",
              "# one\n"
              "# two\n"
              "# three\n"
              "code()\n");
    CHECK(f.widget(0) == FoldWidget::Start);
    CHECK(f.widget(1) == FoldWidget::None);
    CHECK(f.widget(2) == FoldWidget::None);
    CHECK_FOLD(f, 0, 2);
}

// --- coverage and degenerate input -----------------------------------------

TEST(an_unknown_mode_has_no_fold_mode) {
    CHECK(aced::foldModeFor("") == nullptr);
    CHECK(aced::foldModeFor("not_a_mode") == nullptr);
}

TEST(the_expected_number_of_corpus_modes_have_a_fold_mode) {
    int n = 0;
    for (const auto &m : corpus().modes())
        if (aced::foldModeFor(m)) ++n;
    std::fprintf(stderr, "    %d of %d modes fold\n", n,
                 static_cast<int>(corpus().modes().size()));
    // Drops only if the table is regenerated against a newer ace. A fall is a
    // regression; a rise is a new fold mode and this number moves with it.
    CHECK_EQ(n, 115);
}

TEST(folding_works_with_no_tokenizer_at_all) {
    // A buffer with no grammar still gets bracket folding, just without the
    // comment and string awareness. It must not crash and must not fold to the
    // wrong place on ordinary code.
    Document doc("function a() {\n    return 1;\n}\nx();\n");
    TokenChain chain;
    chain.setDocument(&doc);
    DocumentFoldSource src(&doc, &chain);
    const FoldMode *m = aced::foldModeFor("javascript");
    CHECK(m != nullptr);
    CHECK(m->widget(src, 0) == FoldWidget::Start);
    FoldRange r;
    CHECK(m->range(src, 0, &r));
    CHECK_EQ(r.endRow, 2);
}

TEST(rows_out_of_range_are_answered_not_dereferenced) {
    Fixture f("javascript", "var x = 1;\n");
    FoldRange r;
    CHECK(f.widget(-1) == FoldWidget::None);
    CHECK(f.widget(99) == FoldWidget::None);
    CHECK(!f.range(-1, &r));
    CHECK(!f.range(99, &r));
}

TEST(an_empty_document_folds_nothing) {
    Fixture f("javascript", "");
    FoldRange r;
    CHECK(f.widget(0) == FoldWidget::None);
    CHECK(!f.range(0, &r));
}

TEST(every_row_of_a_real_source_file_answers_without_crashing) {
    // The fold modes run over their own implementation. The point is not the
    // answers, it is that 300 rows of real C++ produce no out-of-range index
    // and every range that comes back is ordered and inside the document.
    Document doc;
    {
        std::string text;
        for (int i = 0; i < 40; ++i) {
            text += "namespace n" + std::to_string(i) + " {\n";
            text += "  /* block\n     comment */\n";
            text += "  int f() { return \"}\"[0]; }\n";
            text += "  struct S {\n    int a;\n  };\n";
            text += "}\n\n";
        }
        doc.setText(text);
    }
    TokenChain chain;
    chain.setDocument(&doc);
    chain.setTokenizer(corpus().tokenizer("c_cpp"));
    DocumentFoldSource src(&doc, &chain);
    const FoldMode *m = aced::foldModeFor("c_cpp");
    int folds = 0;
    for (int r = 0; r < doc.lineCount(); ++r) {
        FoldRange fr;
        if (m->widget(src, r) != FoldWidget::Start) continue;
        if (!m->range(src, r, &fr)) continue;
        ++folds;
        CHECK(fr.startRow == r);
        CHECK(fr.endRow > fr.startRow);
        CHECK(fr.endRow < doc.lineCount());
    }
    CHECK(folds > 50);
}
