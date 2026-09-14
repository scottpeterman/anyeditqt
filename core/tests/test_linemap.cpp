// tests/test_linemap.cpp
#include "aced/linemap.h"

#include <random>
#include <vector>

#include "harness.h"

using aced::LineMap;

namespace {

// The whole point of the step is that it is invisible. Every test that mutates
// checks the invariant afterwards, and the randomised case at the bottom checks
// against an independent O(n) model.
struct Model {
    std::vector<int> h;
    std::vector<char> vis;

    int total() const {
        int a = 0;
        for (size_t i = 0; i < h.size(); ++i)
            if (vis[i]) a += h[i];
        return a;
    }
    int startOf(int row) const {
        int a = 0;
        for (int i = 0; i < row && i < static_cast<int>(h.size()); ++i)
            if (vis[static_cast<size_t>(i)]) a += h[static_cast<size_t>(i)];
        return a;
    }
};

}  // namespace

TEST(a_fresh_map_is_the_identity) {
    LineMap m(100);
    CHECK(m.isIdentity());
    CHECK_EQ(m.displayLineCount(), 100);
    CHECK_EQ(m.displayFromRow(37), 37);
    CHECK_EQ(m.rowFromDisplay(37), 37);
    CHECK(m.checkInvariants());
}

TEST(an_empty_map_answers_without_crashing) {
    LineMap m(0);
    CHECK_EQ(m.displayLineCount(), 0);
    CHECK_EQ(m.displayFromRow(0), 0);
    CHECK_EQ(m.rowFromDisplay(0), 0);
    CHECK_EQ(m.nextVisible(0), -1);
    CHECK_EQ(m.prevVisible(0), -1);
}

TEST(setting_a_height_of_one_does_not_allocate) {
    LineMap m(100);
    CHECK(!m.setHeight(5, 1));
    CHECK(m.isIdentity());
}

TEST(a_taller_row_pushes_the_rows_below_it_down) {
    LineMap m(10);
    CHECK(m.setHeight(3, 4));
    CHECK(!m.isIdentity());
    CHECK_EQ(m.displayFromRow(3), 3);
    CHECK_EQ(m.displayLastFromRow(3), 6);
    CHECK_EQ(m.displayFromRow(4), 7);
    CHECK_EQ(m.displayLineCount(), 13);
    CHECK(m.checkInvariants());
}

TEST(a_display_line_inside_a_tall_row_reports_its_sub_line) {
    LineMap m(10);
    m.setHeight(3, 4);
    int sub = -1;
    CHECK_EQ(m.rowFromDisplay(3, &sub), 3);
    CHECK_EQ(sub, 0);
    CHECK_EQ(m.rowFromDisplay(5, &sub), 3);
    CHECK_EQ(sub, 2);
    CHECK_EQ(m.rowFromDisplay(7, &sub), 4);
    CHECK_EQ(sub, 0);
}

TEST(hiding_rows_removes_their_display_lines) {
    LineMap m(10);
    CHECK(m.setVisible(3, 5, false));
    CHECK(m.anyHidden());
    CHECK_EQ(m.displayLineCount(), 7);
    CHECK_EQ(m.displayFromRow(2), 2);
    CHECK_EQ(m.displayFromRow(3), 3);
    CHECK_EQ(m.displayFromRow(6), 3);
    CHECK(m.checkInvariants());
}

TEST(a_display_line_over_a_hidden_run_lands_on_the_row_after_it) {
    LineMap m(10);
    m.setVisible(3, 5, false);
    // Display line 2 is row 2, the fold header. Line 3 is the first line after
    // the hidden body, which is row 6 -- not row 3, 4 or 5.
    CHECK_EQ(m.rowFromDisplay(2), 2);
    CHECK_EQ(m.rowFromDisplay(3), 6);
    CHECK(m.visible(m.rowFromDisplay(3)));
}

TEST(hiding_a_tall_row_removes_all_of_its_display_lines) {
    LineMap m(10);
    m.setHeight(4, 5);
    CHECK_EQ(m.displayLineCount(), 14);
    m.setVisible(4, 4, false);
    CHECK_EQ(m.displayLineCount(), 9);
    CHECK(m.checkInvariants());
    m.setVisible(4, 4, true);
    CHECK_EQ(m.displayLineCount(), 14);
    CHECK(m.checkInvariants());
}

TEST(a_height_change_on_a_hidden_row_moves_nothing_until_it_reappears) {
    LineMap m(10);
    m.setVisible(4, 4, false);
    CHECK_EQ(m.displayLineCount(), 9);
    // Re-wrapping a folded-away row must not resize the scrollbar.
    CHECK(!m.setHeight(4, 6));
    CHECK_EQ(m.displayLineCount(), 9);
    CHECK(m.checkInvariants());
    m.setVisible(4, 4, true);
    CHECK_EQ(m.displayLineCount(), 15);
    CHECK(m.checkInvariants());
}

TEST(expanded_is_independent_of_visible) {
    LineMap m(20);
    // Outer fold 2..15 collapsed, with an inner fold at 6 that was already
    // collapsed. Reopening the outer one must leave the inner one shut.
    m.setExpanded(6, false);
    m.setVisible(7, 9, false);
    m.setExpanded(2, false);
    m.setVisible(3, 15, false);
    CHECK(!m.expanded(6));
    m.setVisible(3, 15, true);
    m.setExpanded(2, true);
    CHECK(!m.expanded(6));
    CHECK_EQ(m.contractedNext(0), 6);
    CHECK_EQ(m.contractedNext(7), -1);
}

TEST(inserting_rows_keeps_the_heights_of_the_rows_below) {
    LineMap m(10);
    m.setHeight(8, 3);
    CHECK_EQ(m.displayLineCount(), 12);
    m.insertRows(2, 2);
    CHECK_EQ(m.rowCount(), 12);
    // The tall row is now row 10 and is still tall. This is the regression the
    // predecessor had: it reset every height below an edit to a guess, so the
    // scrollbar shrank on every keystroke and grew again as rows repainted.
    CHECK_EQ(m.height(10), 3);
    CHECK_EQ(m.displayLineCount(), 14);
    CHECK_EQ(m.displayFromRow(2), 2);
    CHECK_EQ(m.displayFromRow(4), 4);
    CHECK(m.checkInvariants());
}

TEST(removing_rows_removes_exactly_their_display_lines) {
    LineMap m(10);
    m.setHeight(3, 4);
    m.setHeight(7, 2);
    CHECK_EQ(m.displayLineCount(), 14);
    m.removeRows(3, 2);  // the tall row and the one after it
    CHECK_EQ(m.rowCount(), 8);
    CHECK_EQ(m.displayLineCount(), 9);
    CHECK_EQ(m.height(5), 2);  // was row 7
    CHECK(m.checkInvariants());
}

TEST(removing_hidden_rows_keeps_the_hidden_count_honest) {
    LineMap m(10);
    m.setVisible(3, 6, false);
    CHECK_EQ(m.displayLineCount(), 6);
    m.removeRows(2, 3);  // header plus two hidden rows
    CHECK_EQ(m.rowCount(), 7);
    CHECK(m.checkInvariants());
    m.showAll();
    CHECK(!m.anyHidden());
    CHECK_EQ(m.displayLineCount(), 7);
    CHECK(m.checkInvariants());
}

TEST(inserting_into_a_still_identity_map_stays_identity) {
    LineMap m(10);
    m.insertRows(4, 3);
    CHECK(m.isIdentity());
    CHECK_EQ(m.displayLineCount(), 13);
    m.removeRows(0, 5);
    CHECK(m.isIdentity());
    CHECK_EQ(m.displayLineCount(), 8);
}

TEST(forgetting_heights_with_nothing_folded_returns_to_identity) {
    LineMap m(50);
    m.setHeight(10, 4);
    CHECK(!m.isIdentity());
    m.forgetHeights();
    CHECK(m.isIdentity());
    CHECK_EQ(m.displayLineCount(), 50);
}

TEST(forgetting_heights_with_a_fold_open_keeps_the_fold) {
    LineMap m(50);
    m.setHeight(10, 4);
    m.setExpanded(20, false);
    m.setVisible(21, 25, false);
    m.forgetHeights();
    CHECK(!m.isIdentity());
    CHECK_EQ(m.height(10), 1);
    CHECK(!m.visible(23));
    CHECK(!m.expanded(20));
    CHECK_EQ(m.displayLineCount(), 45);
    CHECK(m.checkInvariants());
}

TEST(the_nearest_visible_row_is_found_in_both_directions) {
    LineMap m(10);
    m.setVisible(3, 6, false);
    CHECK_EQ(m.nextVisible(4), 7);
    CHECK_EQ(m.prevVisible(4), 2);
    CHECK_EQ(m.nextVisible(7), 7);
    CHECK_EQ(m.prevVisible(9), 9);
}

TEST(a_lookup_past_the_end_clamps_to_the_last_visible_row) {
    LineMap m(10);
    m.setVisible(7, 9, false);
    int sub = -1;
    CHECK_EQ(m.displayLineCount(), 7);
    CHECK_EQ(m.rowFromDisplay(99, &sub), 6);
    CHECK_EQ(sub, 0);
    CHECK_EQ(m.rowFromDisplay(-5), 0);
}

TEST(the_step_survives_edits_walking_backwards_through_the_document) {
    // Heights set from the bottom up is the pattern that makes the step move
    // backwards, which is the branch a forward-only paint loop never reaches.
    LineMap m(400);
    for (int r = 399; r >= 0; --r) m.setHeight(r, 1 + (r % 3));
    CHECK(m.checkInvariants());
    int expect = 0;
    for (int r = 0; r < 400; ++r) {
        CHECK_EQ(m.displayFromRow(r), expect);
        expect += 1 + (r % 3);
    }
    CHECK_EQ(m.displayLineCount(), expect);
}

TEST(random_edits_agree_with_an_independent_model) {
    std::mt19937 rng(20260914);
    LineMap m(200);
    Model model;
    model.h.assign(200, 1);
    model.vis.assign(200, 1);

    for (int iter = 0; iter < 4000; ++iter) {
        const int n = static_cast<int>(model.h.size());
        const int op = static_cast<int>(rng() % 100);
        if (n == 0 || op < 40) {
            if (n == 0) {
                m.insertRows(0, 1);
                model.h.insert(model.h.begin(), 1);
                model.vis.insert(model.vis.begin(), 1);
                continue;
            }
            const int row = static_cast<int>(rng() % static_cast<unsigned>(n));
            const int h = 1 + static_cast<int>(rng() % 5);
            m.setHeight(row, h);
            model.h[static_cast<size_t>(row)] = h;
        } else if (op < 70) {
            const int a = static_cast<int>(rng() % static_cast<unsigned>(n));
            const int b = std::min(n - 1, a + static_cast<int>(rng() % 12));
            const bool vis = (rng() % 2) == 0;
            m.setVisible(a, b, vis);
            for (int i = a; i <= b; ++i) model.vis[static_cast<size_t>(i)] = vis ? 1 : 0;
        } else if (op < 85) {
            const int row = static_cast<int>(rng() % static_cast<unsigned>(n + 1));
            const int count = 1 + static_cast<int>(rng() % 3);
            m.insertRows(row, count);
            model.h.insert(model.h.begin() + row, static_cast<size_t>(count), 1);
            model.vis.insert(model.vis.begin() + row, static_cast<size_t>(count), 1);
        } else {
            const int row = static_cast<int>(rng() % static_cast<unsigned>(n));
            const int count = std::min(1 + static_cast<int>(rng() % 3), n - row);
            m.removeRows(row, count);
            model.h.erase(model.h.begin() + row, model.h.begin() + row + count);
            model.vis.erase(model.vis.begin() + row, model.vis.begin() + row + count);
        }

        if (iter % 97 != 0) continue;
        if (!m.checkInvariants()) {
            CHECK(false);
            return;
        }
        const int rows = static_cast<int>(model.h.size());
        if (m.rowCount() != rows) {
            CHECK_EQ(m.rowCount(), rows);
            return;
        }
        if (m.displayLineCount() != model.total()) {
            CHECK_EQ(m.displayLineCount(), model.total());
            return;
        }
        for (int r = 0; r <= rows; r += 7) {
            if (m.displayFromRow(r) != model.startOf(r)) {
                CHECK_EQ(m.displayFromRow(r), model.startOf(r));
                return;
            }
        }
        // Round trip: every display line must map to a visible row that owns it.
        for (int line = 0; line < model.total(); line += 5) {
            int sub = 0;
            const int row = m.rowFromDisplay(line, &sub);
            if (!m.visible(row)) {
                CHECK(false);
                return;
            }
            if (model.startOf(row) + sub != line) {
                CHECK_EQ(model.startOf(row) + sub, line);
                return;
            }
        }
    }
    CHECK(m.checkInvariants());
}
