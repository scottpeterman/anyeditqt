// tests/test_undo.cpp
#include "aced/undo.h"

#include "aced/anchor.h"
#include "aced/document.h"
#include "harness.h"

using namespace aced;

TEST(undo_nothing_is_false) {
    Document d("abc");
    UndoManager u(&d);
    CHECK(!u.canUndo());
    CHECK(!u.undo());
}

TEST(single_edit_undo_redo) {
    Document d("abc");
    UndoManager u(&d);
    d.insert({0, 3}, "def");
    CHECK_EQ(d.text(), std::string("abcdef"));
    CHECK(u.undo());
    CHECK_EQ(d.text(), std::string("abc"));
    CHECK(u.redo());
    CHECK_EQ(d.text(), std::string("abcdef"));
}

TEST(consecutive_edits_coalesce_until_marked) {
    Document d;
    UndoManager u(&d);
    d.insert({0, 0}, "h");
    d.insert({0, 1}, "i");
    CHECK_EQ(d.text(), std::string("hi"));
    CHECK(u.undo());
    CHECK_EQ(d.text(), std::string());  // both, one group
}

TEST(mark_separates_groups) {
    Document d;
    UndoManager u(&d);
    d.insert({0, 0}, "hello");
    u.mark();
    d.insert({0, 5}, " world");
    CHECK(u.undo());
    CHECK_EQ(d.text(), std::string("hello"));
    CHECK(u.undo());
    CHECK_EQ(d.text(), std::string());
    CHECK(!u.undo());
}

TEST(group_inverts_in_reverse_order) {
    Document d("....");
    UndoManager u(&d);
    d.insert({0, 0}, "A");
    d.insert({0, 1}, "B");
    d.insert({0, 2}, "C");
    CHECK_EQ(d.text(), std::string("ABC...."));
    CHECK(u.undo());
    CHECK_EQ(d.text(), std::string("...."));
}

TEST(multiline_group_round_trips) {
    Document d("one\ntwo\nthree");
    UndoManager u(&d);
    d.remove({{0, 1}, {2, 2}});
    d.insert({0, 1}, "XX\nYY");
    CHECK(u.undo());
    CHECK_EQ(d.text(), std::string("one\ntwo\nthree"));
}

TEST(new_edit_clears_redo) {
    Document d;
    UndoManager u(&d);
    d.insert({0, 0}, "a");
    u.mark();
    d.insert({0, 1}, "b");
    u.undo();
    CHECK(u.canRedo());
    d.insert({0, 1}, "c");
    CHECK(!u.canRedo());
    CHECK_EQ(d.text(), std::string("ac"));
}

TEST(undo_does_not_record_itself) {
    Document d;
    UndoManager u(&d);
    d.insert({0, 0}, "a");
    u.undo();
    CHECK_EQ(u.undoDepth(), 0);
    CHECK_EQ(u.redoDepth(), 1);
}

TEST(repeated_undo_redo_is_stable) {
    Document d("seed");
    UndoManager u(&d);
    d.insert({0, 4}, "\nline two");
    u.mark();
    d.insert({1, 0}, ">> ");
    u.mark();
    std::string full = d.text();
    for (int i = 0; i < 20; ++i) {
        u.undo();
        u.undo();
        u.redo();
        u.redo();
    }
    CHECK_EQ(d.text(), full);
}

TEST(anchors_track_through_undo) {
    Document d("one\ntwo\nthree");
    UndoManager u(&d);
    Anchor a(&d, {2, 3});
    d.insert({0, 0}, "A\nB\n");
    u.mark();
    CHECK_EQ(a.row(), 4);
    u.undo();
    CHECK_EQ(a.row(), 2);
    u.redo();
    CHECK_EQ(a.row(), 4);
}

TEST(last_touched_reports_where_to_put_the_cursor) {
    Document d("abc");
    UndoManager u(&d);
    d.insert({0, 1}, "XYZ");
    u.undo();
    CHECK(u.lastTouched() == (Position{0, 1}));
    u.redo();
    CHECK(u.lastTouched() == (Position{0, 4}));
}

TEST(clear_empties_both_stacks) {
    Document d;
    UndoManager u(&d);
    d.insert({0, 0}, "a");
    u.mark();
    u.clear();
    CHECK(!u.canUndo());
    CHECK(!u.canRedo());
}

TEST(double_mark_does_not_create_an_empty_group) {
    Document d;
    UndoManager u(&d);
    d.insert({0, 0}, "a");
    u.mark();
    u.mark();
    u.mark();
    CHECK_EQ(u.undoDepth(), 1);
    CHECK(u.undo());
    CHECK_EQ(d.text(), std::string());
}

TEST(clear_redo_leaves_undo_alone) {
    Document d("abc");
    UndoManager u(&d);
    d.insert({0, 3}, "d");
    u.mark();
    d.insert({0, 4}, "e");
    CHECK(u.undo());
    CHECK(u.canRedo());
    u.clearRedo();
    CHECK(!u.canRedo());
    CHECK_EQ(u.undoDepth(), 1);
    CHECK_EQ(d.text(), std::string("abcd"));
}