// tests/test_anchor.cpp
#include "aced/anchor.h"

#include "aced/document.h"
#include <memory>
#include <vector>

#include "harness.h"

using namespace aced;

TEST(anchor_before_insert_does_not_move) {
    Document d("one\ntwo\nthree");
    Anchor a(&d, {0, 1});
    d.insert({1, 0}, "X");
    CHECK(a.position() == (Position{0, 1}));
}

TEST(anchor_after_insert_on_later_row_shifts_down) {
    Document d("one\ntwo\nthree");
    Anchor a(&d, {2, 3});
    d.insert({0, 0}, "X\nY\n");
    CHECK(a.position() == (Position{4, 3}));
}

TEST(anchor_on_same_row_after_insert_shifts_right) {
    Document d("abcdef");
    Anchor a(&d, {0, 4});
    d.insert({0, 2}, "XY");
    CHECK(a.position() == (Position{0, 6}));
}

TEST(anchor_on_same_row_rebases_on_multiline_insert) {
    Document d("abcdef");
    Anchor a(&d, {0, 4});      // 2 chars past the insert point
    d.insert({0, 2}, "X\nYZ");  // last inserted line is "YZ", so col 2
    CHECK(a.position() == (Position{1, 4}));
}

TEST(insert_right_tiebreak_at_exact_position) {
    Document d("abc");
    Anchor moving(&d, {0, 1}, true);
    Anchor staying(&d, {0, 1}, false);
    d.insert({0, 1}, "XY");
    CHECK(moving.position() == (Position{0, 3}));
    CHECK(staying.position() == (Position{0, 1}));
}

TEST(anchor_inside_removed_span_collapses_to_start) {
    Document d("one\ntwo\nthree");
    Anchor a(&d, {1, 2});
    d.remove({{0, 1}, {2, 2}});
    CHECK(a.position() == (Position{0, 1}));
}

TEST(anchor_after_removed_span_pulls_back) {
    Document d("one\ntwo\nthree");
    Anchor a(&d, {2, 4});
    d.remove({{0, 1}, {1, 1}});
    CHECK(a.position() == (Position{1, 4}));
}

TEST(anchor_on_the_end_row_of_a_removal_rebases_column) {
    Document d("one\ntwo\nthree");
    Anchor a(&d, {2, 4});           // "three", col 4
    d.remove({{0, 1}, {2, 2}});     // leaves "oree"
    // col 4 was 2 past the removal end; start col was 1, so 1 + 2 = 3
    CHECK(a.position() == (Position{0, 3}));
}

TEST(anchor_exactly_at_removal_start_does_not_move) {
    Document d("abcdef");
    Anchor a(&d, {0, 2});
    d.remove({{0, 2}, {0, 4}});
    CHECK(a.position() == (Position{0, 2}));
}

TEST(anchor_before_removal_untouched) {
    Document d("abcdef");
    Anchor a(&d, {0, 1});
    d.remove({{0, 2}, {0, 4}});
    CHECK(a.position() == (Position{0, 1}));
}

TEST(many_anchors_all_updated) {
    Document d("0\n1\n2\n3\n4");
    std::vector<std::unique_ptr<Anchor>> as;
    for (int r = 0; r < 5; ++r) as.push_back(std::make_unique<Anchor>(&d, Position{r, 0}));
    d.insert({0, 0}, "X\n");
    for (int r = 0; r < 5; ++r) CHECK_EQ(as[r]->row(), r + 1);
}

TEST(anchor_survives_round_trip_through_undo_shaped_edit) {
    Document d("one\ntwo\nthree");
    Anchor a(&d, {2, 3});
    Delta x = d.insert({0, 0}, "AAA\nBBB\n");
    CHECK(a.position() == (Position{4, 3}));
    d.revertDelta(x);
    CHECK(a.position() == (Position{2, 3}));
}

TEST(detached_anchor_does_not_crash_after_document_dies) {
    auto d = std::make_unique<Document>("abc");
    auto a = std::make_unique<Anchor>(d.get(), Position{0, 1});
    d.reset();
    CHECK(a->document() == nullptr);
    a->setPosition({0, 0});  // must not dereference the dead document
    CHECK(a->position() == (Position{0, 0}));
}

TEST(transform_is_usable_without_registering) {
    Delta x;
    x.action = Delta::Action::Insert;
    x.start = {0, 0};
    x.end = {1, 0};
    x.lines = {"", ""};
    Position p = Anchor::transform(x, {3, 7}, true);
    CHECK(p == (Position{4, 7}));
}
