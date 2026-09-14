// tests/test_search.cpp
#include "aced/search.h"

#include "aced/document.h"
#include "harness.h"

using aced::Document;
using aced::Position;
using aced::Range;
using aced::Search;
using aced::SearchOptions;

namespace {

std::string rangeStr(const Range &r) {
    return std::to_string(r.start.row) + ":" + std::to_string(r.start.column) + "-"
           + std::to_string(r.end.row) + ":" + std::to_string(r.end.column);
}

std::string findNext(const Document &d, const Search &s, Position from) {
    bool found = false;
    const Range r = s.next(d, from, &found);
    return found ? rangeStr(r) : std::string("none");
}

std::string findPrev(const Document &d, const Search &s, Position from) {
    bool found = false;
    const Range r = s.previous(d, from, &found);
    return found ? rangeStr(r) : std::string("none");
}

}  // namespace

TEST(an_empty_needle_is_invalid_rather_than_matching_everywhere) {
    Search s("", {});
    CHECK(!s.valid());
    Document d("hello");
    CHECK_EQ(s.all(d).size(), size_t(0));
    CHECK_EQ(s.replaceAll(d, "x"), 0);
    CHECK_EQ(d.text(), std::string("hello"));
}

TEST(a_broken_regex_reports_instead_of_throwing) {
    SearchOptions o;
    o.regex = true;
    Search s("(unclosed", o);
    CHECK(!s.valid());
    CHECK(!s.error().empty());
}

TEST(a_literal_needle_does_not_get_treated_as_a_pattern) {
    Document d("a.b axb a.b");
    Search s("a.b", {});  // regex off
    CHECK_EQ(s.all(d).size(), size_t(2));
    CHECK_EQ(findNext(d, s, {0, 0}), std::string("0:0-0:3"));
    CHECK_EQ(findNext(d, s, {0, 3}), std::string("0:8-0:11"));
}

TEST(escaping_survives_a_needle_containing_backslash_E) {
    // \Q...\E quoting would end at the \E in the needle and hand the rest to
    // PCRE2 as a pattern. Escaping character by character does not.
    Document d("literal \\E(a) here");
    Search s("\\E(a)", {});
    CHECK_EQ(s.all(d).size(), size_t(1));
}

TEST(search_is_case_insensitive_by_default_and_sensitive_on_request) {
    Document d("Foo foo FOO");
    CHECK_EQ(Search("foo", {}).all(d).size(), size_t(3));
    SearchOptions o;
    o.caseSensitive = true;
    CHECK_EQ(Search("foo", o).all(d).size(), size_t(1));
}

TEST(whole_word_does_not_match_inside_a_longer_word) {
    Document d("cat concatenate cat_x cat");
    SearchOptions o;
    o.wholeWord = true;
    Search s("cat", o);
    const auto all = s.all(d);
    // "cat" twice; "concatenate" is not a match, and neither is "cat_x" because
    // an underscore is a word character.
    CHECK_EQ(all.size(), size_t(2));
    CHECK_EQ(rangeStr(all[0]), std::string("0:0-0:3"));
}

TEST(regex_mode_matches_patterns) {
    Document d("ip 10.0.0.1 and 192.168.1.1 here");
    SearchOptions o;
    o.regex = true;
    Search s("\\d+\\.\\d+\\.\\d+\\.\\d+", o);
    CHECK(s.valid());
    const auto all = s.all(d);
    CHECK_EQ(all.size(), size_t(2));
    CHECK_EQ(d.textInRange(all[0]), std::string("10.0.0.1"));
    CHECK_EQ(d.textInRange(all[1]), std::string("192.168.1.1"));
}

TEST(a_pattern_that_can_match_nothing_does_not_hang) {
    // A zero-length match does not advance the offset on its own; without an
    // explicit step past it this loops forever on any line.
    Document d("aaa\nbbb");
    SearchOptions o;
    o.regex = true;
    Search s("x*", o);
    CHECK(s.valid());
    const auto all = s.all(d);
    CHECK(all.size() > 0);
    CHECK(all.size() < 100);  // finite, which is the whole assertion
}

TEST(next_walks_forward_across_lines) {
    Document d("one two\nthree two\nfour");
    Search s("two", {});
    CHECK_EQ(findNext(d, s, {0, 0}), std::string("0:4-0:7"));
    CHECK_EQ(findNext(d, s, {0, 5}), std::string("1:6-1:9"));
}

TEST(next_wraps_to_the_top_and_stops_when_told_not_to) {
    Document d("target\nnothing\nnothing");
    Search s("target", {});
    CHECK_EQ(findNext(d, s, {1, 0}), std::string("0:0-0:6"));

    SearchOptions o;
    o.wrap = false;
    CHECK_EQ(findNext(d, Search("target", o), {1, 0}), std::string("none"));
}

TEST(a_wrap_does_not_hand_back_the_match_the_cursor_is_on) {
    // One match in the whole document, and the cursor immediately after it.
    // Wrapping must find it again -- there is nothing else to find -- but a
    // wrap that ignored the cursor column would also "find" it when the cursor
    // was sitting at its start, which is how Find Next stops advancing.
    Document d("only here");
    Search s("only", {});
    CHECK_EQ(findNext(d, s, {0, 4}), std::string("0:0-0:4"));
    // From the start of the one match, the next one wraps around to itself.
    CHECK_EQ(findNext(d, s, {0, 0}), std::string("0:0-0:4"));
}

TEST(previous_walks_backward_and_wraps) {
    Document d("two\nmid two end\ntwo");
    Search s("two", {});
    CHECK_EQ(findPrev(d, s, {2, 3}), std::string("2:0-2:3"));
    CHECK_EQ(findPrev(d, s, {2, 0}), std::string("1:4-1:7"));
    CHECK_EQ(findPrev(d, s, {1, 4}), std::string("0:0-0:3"));
    // From the top it wraps to the last match in the document.
    CHECK_EQ(findPrev(d, s, {0, 0}), std::string("2:0-2:3"));
}

TEST(previous_picks_the_last_match_on_a_line_not_the_first) {
    Document d("a a a a");
    Search s("a", {});
    CHECK_EQ(findPrev(d, s, {0, 7}), std::string("0:6-0:7"));
}

TEST(all_finds_every_match_in_document_order) {
    Document d("x\nxx\nx x x");
    Search s("x", {});
    const auto all = s.all(d);
    CHECK_EQ(all.size(), size_t(6));
    CHECK_EQ(rangeStr(all.front()), std::string("0:0-0:1"));
    CHECK_EQ(rangeStr(all.back()), std::string("2:4-2:5"));
}

TEST(all_stops_at_the_limit) {
    std::string text;
    for (int i = 0; i < 500; ++i) text += "x\n";
    Document d(text);
    Search s("x", {});
    CHECK_EQ(s.all(d, 10).size(), size_t(10));
}

TEST(replace_all_rewrites_every_match) {
    Document d("foo bar foo\nfoo");
    Search s("foo", {});
    CHECK_EQ(s.replaceAll(d, "qux"), 3);
    CHECK_EQ(d.text(), std::string("qux bar qux\nqux"));
}

TEST(replace_all_is_correct_when_the_replacement_is_a_different_length) {
    // REGRESSION SHAPE. Replacing front to back shifts every later match by the
    // length difference, so the second replacement lands in the wrong place and
    // the error compounds. Back to front is what makes this come out right.
    Document d("aa aa aa");
    Search s("aa", {});
    CHECK_EQ(s.replaceAll(d, "bbbbb"), 3);
    CHECK_EQ(d.text(), std::string("bbbbb bbbbb bbbbb"));

    Document d2("aaaa aaaa aaaa");
    CHECK_EQ(Search("aaaa", {}).replaceAll(d2, "b"), 3);
    CHECK_EQ(d2.text(), std::string("b b b"));
}

TEST(replace_all_replacement_is_literal_not_a_backreference) {
    Document d("abc");
    SearchOptions o;
    o.regex = true;
    Search s("(a)(b)", o);
    CHECK_EQ(s.replaceAll(d, "$1-$2"), 1);
    // Documented behaviour: the dollar signs go in as themselves.
    CHECK_EQ(d.text(), std::string("$1-$2c"));
}

TEST(replace_all_goes_through_the_document_so_undo_sees_it) {
    Document d("foo foo");
    int deltas = 0;
    d.addListener([&](const aced::Delta &) { ++deltas; });
    Search("foo", {}).replaceAll(d, "x");
    // Two replacements, each a remove and an insert.
    CHECK_EQ(deltas, 4);
    CHECK_EQ(d.text(), std::string("x x"));
}

TEST(search_works_on_multibyte_text_in_byte_columns) {
    // Columns are byte offsets everywhere in aced and PCRE2 matches in bytes,
    // so the two agree without conversion -- but only if the pattern is
    // compiled with UTF mode, which is what makes the accented characters one
    // unit rather than two.
    Document d("caf\xc3\xa9 and caf\xc3\xa9");
    Search s("caf\xc3\xa9", {});
    const auto all = s.all(d);
    CHECK_EQ(all.size(), size_t(2));
    CHECK_EQ(d.textInRange(all[0]), std::string("caf\xc3\xa9"));
    CHECK_EQ(all[0].end.column, 5);  // 3 ASCII + 2 bytes of e-acute
}

TEST(matches_do_not_span_lines) {
    // Stated in the header as a limitation, asserted here so it stays a known
    // one rather than becoming a surprise.
    Document d("one\ntwo");
    SearchOptions o;
    o.regex = true;
    CHECK_EQ(Search("one.two", o).all(d).size(), size_t(0));
    CHECK_EQ(Search("one\\ntwo", o).all(d).size(), size_t(0));
}
