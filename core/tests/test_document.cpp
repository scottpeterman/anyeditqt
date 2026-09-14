// tests/test_document.cpp
#include "aced/document.h"

#include "harness.h"

using namespace aced;

TEST(empty_document_is_one_empty_line) {
    Document d;
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.line(0), std::string());
    CHECK_EQ(d.text(), std::string());
    CHECK(d.end() == (Position{0, 0}));
}

TEST(split_lines) {
    CHECK_EQ(splitLines("").size(), size_t(1));
    CHECK_EQ(splitLines("a\nb").size(), size_t(2));
    CHECK_EQ(splitLines("a\n").size(), size_t(2));
    CHECK_EQ(splitLines("a\n").back(), std::string());
    CHECK_EQ(splitLines("a\r\nb").size(), size_t(2));
    CHECK_EQ(splitLines("a\r\nb")[0], std::string("a"));
    CHECK_EQ(splitLines("a\rb").size(), size_t(2));
}

TEST(construct_from_text) {
    Document d("one\ntwo\nthree");
    CHECK_EQ(d.lineCount(), 3);
    CHECK_EQ(d.line(1), std::string("two"));
    CHECK_EQ(d.text(), std::string("one\ntwo\nthree"));
    CHECK(d.end() == (Position{2, 5}));
}

TEST(single_line_insert) {
    Document d("hello world");
    Delta x = d.insert({0, 5}, ",");
    CHECK_EQ(d.line(0), std::string("hello, world"));
    CHECK(x.end == (Position{0, 6}));
    CHECK_EQ(x.lines.size(), size_t(1));
}

TEST(multi_line_insert_splits_the_row) {
    Document d("abcdef");
    Delta x = d.insert({0, 3}, "1\n2\n3");
    CHECK_EQ(d.lineCount(), 3);
    CHECK_EQ(d.line(0), std::string("abc1"));
    CHECK_EQ(d.line(1), std::string("2"));
    CHECK_EQ(d.line(2), std::string("3def"));
    CHECK(x.end == (Position{2, 1}));
}

TEST(insert_at_end_of_document) {
    Document d("a");
    d.insert(d.end(), "\nb");
    CHECK_EQ(d.text(), std::string("a\nb"));
}

TEST(single_line_remove) {
    Document d("hello, world");
    d.remove({{0, 5}, {0, 7}});
    CHECK_EQ(d.line(0), std::string("helloworld"));
}

TEST(multi_line_remove_joins_rows) {
    Document d("one\ntwo\nthree");
    d.remove({{0, 1}, {2, 2}});
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.line(0), std::string("oree"));
}

TEST(remove_captures_removed_text) {
    Document d("one\ntwo\nthree");
    Delta x = d.remove({{0, 1}, {1, 2}});
    CHECK_EQ(x.lines.size(), size_t(2));
    CHECK_EQ(x.lines[0], std::string("ne"));
    CHECK_EQ(x.lines[1], std::string("tw"));
}

TEST(delta_is_invertible_in_place) {
    Document d("one\ntwo\nthree");
    std::string before = d.text();
    Delta x = d.remove({{0, 1}, {2, 2}});
    CHECK(d.text() != before);
    d.applyDelta(x.inverted());
    CHECK_EQ(d.text(), before);
}

TEST(insert_delta_is_invertible_too) {
    Document d("abc");
    Delta x = d.insert({0, 1}, "XY\nZ");
    d.revertDelta(x);
    CHECK_EQ(d.text(), std::string("abc"));
}

TEST(replace_range) {
    Document d("one\ntwo\nthree");
    d.replace({{1, 0}, {1, 3}}, "TWO");
    CHECK_EQ(d.text(), std::string("one\nTWO\nthree"));
}

TEST(reversed_range_is_normalized) {
    Document d("hello");
    d.remove({{0, 4}, {0, 1}});
    CHECK_EQ(d.line(0), std::string("ho"));
}

TEST(text_in_range) {
    Document d("one\ntwo\nthree");
    CHECK_EQ(d.textInRange({{0, 1}, {2, 2}}), std::string("ne\ntwo\nth"));
    CHECK_EQ(d.textInRange({{1, 0}, {1, 3}}), std::string("two"));
    CHECK_EQ(d.textInRange({{1, 1}, {1, 1}}), std::string());
}

TEST(clamp_bounds_positions) {
    Document d("ab\ncd");
    CHECK(d.clamp({-5, -5}) == (Position{0, 0}));
    CHECK(d.clamp({99, 99}) == (Position{1, 2}));
    CHECK(d.clamp({0, 99}) == (Position{0, 2}));
}

TEST(offset_position_roundtrip) {
    Document d("one\ntwo\nthree");
    for (int r = 0; r < d.lineCount(); ++r) {
        for (int c = 0; c <= d.lineLength(r); ++c) {
            Position p{r, c};
            CHECK(d.offsetToPosition(d.positionToOffset(p)) == p);
        }
    }
    CHECK_EQ(d.positionToOffset({1, 0}), 4);
    CHECK_EQ(d.positionToOffset({2, 5}), 13);
}

TEST(set_text_replaces_everything) {
    Document d("one\ntwo");
    d.setText("fresh");
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.text(), std::string("fresh"));
    d.setText("");
    CHECK_EQ(d.lineCount(), 1);
    CHECK_EQ(d.text(), std::string());
}

TEST(listeners_see_every_delta) {
    Document d("abc");
    int inserts = 0, removes = 0;
    auto id = d.addListener([&](const Delta &x) {
        if (x.action == Delta::Action::Insert) ++inserts;
        else ++removes;
    });
    d.insert({0, 0}, "x");
    d.remove({{0, 0}, {0, 1}});
    CHECK_EQ(inserts, 1);
    CHECK_EQ(removes, 1);
    d.removeListener(id);
    d.insert({0, 0}, "y");
    CHECK_EQ(inserts, 1);
}

TEST(listener_sees_consistent_document) {
    Document d("abc");
    std::string seen;
    d.addListener([&](const Delta &) { seen = d.text(); });
    d.insert({0, 3}, "def");
    CHECK_EQ(seen, std::string("abcdef"));
}

TEST(utf8_floor_walks_back_to_lead_byte) {
    // "aé b" -- the e-acute is two bytes at offsets 1..2.
    std::string s = "a\xC3\xA9 b";
    CHECK_EQ(Document::utf8Floor(s, 0), 0);
    CHECK_EQ(Document::utf8Floor(s, 1), 1);
    CHECK_EQ(Document::utf8Floor(s, 2), 1);  // inside the sequence
    CHECK_EQ(Document::utf8Floor(s, 3), 3);
}

TEST(multibyte_text_survives_edits) {
    Document d("héllo wörld");
    std::string before = d.text();
    Delta x = d.remove({{0, 0}, {0, 6}});
    d.revertDelta(x);
    CHECK_EQ(d.text(), before);
}
