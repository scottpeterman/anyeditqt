// app/tests/test_find.cpp
//
// The find bar against a real EditorWidget. The search itself is tested without
// Qt in core/tests/test_search.cpp; what is tested here is the part that only
// exists once there is a widget: what gets selected, what gets highlighted,
// what the status line says, and what happens to undo.
#include <QApplication>
#include <QCheckBox>
#include <QKeyEvent>
#include <QLineEdit>

#include "aced/document.h"
#include "aced/undo.h"
#include "acedqt/editorwidget.h"
#include "app/findbar.h"
#include "harness.h"

using acedqt::EditorWidget;
using anyedit::FindBar;

namespace {

struct Rig {
    EditorWidget ed;
    FindBar bar;
    QString status;

    explicit Rig(const QString &text) {
        ed.resize(600, 400);
        ed.show();
        ed.setText(text);
        bar.setEditor(&ed);
        QObject::connect(&bar, &FindBar::statusChanged, &bar,
                         [this](const QString &t) { status = t; });
    }
    QString sel() const {
        return ed.hasSelection()
                   ? QString::fromStdString(ed.document()->textInRange(ed.selection()))
                   : QString();
    }
    int selRow() const { return ed.selection().start.row; }
    int selCol() const { return ed.selection().start.column; }
    // Controls are found by the text they carry rather than by object name, so
    // the test exercises the bar a user sees rather than a naming convention.
    QCheckBox *toggle(const QString &label) {
        for (QCheckBox *c : bar.findChildren<QCheckBox *>())
            if (c->text() == label) return c;
        return nullptr;
    }
};

}  // namespace

TEST(typing_a_term_highlights_every_match) {
    Rig r("foo bar\nfoo baz\nnothing");
    r.bar.setSearchText("foo");
    CHECK_EQ(r.ed.searchMatchCount(), 2);
    CHECK_EQ(r.status.toStdString(), std::string("2 matches"));
}

TEST(clearing_the_term_clears_the_highlights) {
    Rig r("foo foo");
    r.bar.setSearchText("foo");
    CHECK_EQ(r.ed.searchMatchCount(), 2);
    r.bar.setSearchText("");
    CHECK_EQ(r.ed.searchMatchCount(), 0);
}

TEST(find_next_selects_each_match_in_turn_and_wraps) {
    Rig r("foo\nfoo\nfoo");
    r.bar.setSearchText("foo");
    r.ed.setCursor({0, 0});
    r.bar.findNext();
    CHECK_EQ(r.selRow(), 0);
    r.bar.findNext();
    CHECK_EQ(r.selRow(), 1);
    r.bar.findNext();
    CHECK_EQ(r.selRow(), 2);
    // Round the end, back to the top.
    r.bar.findNext();
    CHECK_EQ(r.selRow(), 0);
}

TEST(find_next_does_not_stick_on_the_match_the_cursor_is_inside) {
    // Click in the middle of a hit and press F3: the next one has to be the
    // NEXT one. Searching from the cursor rather than past the selection start
    // returns the same match forever, which reads as Find Next being broken.
    Rig r("foo foo");
    r.bar.setSearchText("foo");
    r.ed.setCursor({0, 1});  // inside the first match
    r.bar.findNext();
    CHECK_EQ(r.selCol(), 4);
}

TEST(find_previous_walks_the_other_way) {
    Rig r("foo\nfoo\nfoo");
    r.bar.setSearchText("foo");
    r.ed.setCursor({2, 3});
    r.bar.findPrevious();
    CHECK_EQ(r.selRow(), 2);
    r.bar.findPrevious();
    CHECK_EQ(r.selRow(), 1);
    r.bar.findPrevious();
    CHECK_EQ(r.selRow(), 0);
}

TEST(the_status_counts_which_match_you_are_on) {
    Rig r("x\nx\nx");
    r.bar.setSearchText("x");
    r.ed.setCursor({0, 0});
    r.bar.findNext();
    CHECK_EQ(r.status.toStdString(), std::string("1 of 3"));
    r.bar.findNext();
    CHECK_EQ(r.status.toStdString(), std::string("2 of 3"));
}

TEST(no_matches_says_so_rather_than_saying_nothing) {
    Rig r("abc");
    r.bar.setSearchText("zzz");
    CHECK_EQ(r.ed.searchMatchCount(), 0);
    CHECK_EQ(r.status.toStdString(), std::string("no matches"));
}

TEST(a_half_typed_regex_is_reported_and_drops_the_stale_highlights) {
    Rig r("aaa aaa");
    r.toggle(".*")->setChecked(true);
    r.bar.setSearchText("a+");
    CHECK_EQ(r.ed.searchMatchCount(), 2);
    // Every regex is invalid partway through being typed. The old highlights
    // must go, or they sit there looking like results for what is on screen.
    r.bar.setSearchText("a+(");
    CHECK_EQ(r.ed.searchMatchCount(), 0);
    CHECK(r.status.contains("missing closing parenthesis")
          || r.status.contains("parenthes"));
}

TEST(the_toggles_change_what_matches) {
    Rig r("Cat concatenate cat");
    r.bar.setSearchText("cat");
    CHECK_EQ(r.ed.searchMatchCount(), 3);

    r.toggle("Aa")->setChecked(true);
    CHECK_EQ(r.ed.searchMatchCount(), 2);  // "concatenate" and "cat"

    r.toggle("Word")->setChecked(true);
    CHECK_EQ(r.ed.searchMatchCount(), 1);  // just "cat"
}

TEST(opening_the_bar_seeds_it_from_the_selection) {
    Rig r("alpha beta gamma");
    r.ed.setSelection({{0, 6}, {0, 10}});  // "beta"
    r.bar.activate(false);
    CHECK_EQ(r.bar.searchText().toStdString(), std::string("beta"));
    CHECK_EQ(r.ed.searchMatchCount(), 1);
}

TEST(a_multi_line_selection_does_not_become_the_search_term) {
    // aced::Search cannot match across a newline, so seeding from one would put
    // a term in the box that can never match anything.
    Rig r("alpha\nbeta");
    r.bar.setSearchText("keep me");
    r.ed.setSelection({{0, 0}, {1, 4}});
    r.bar.activate(false);
    CHECK_EQ(r.bar.searchText().toStdString(), std::string("keep me"));
}

TEST(replace_only_fires_when_the_selection_is_actually_a_match) {
    Rig r("foo bar foo");
    r.bar.setSearchText("foo");
    r.bar.setReplaceText("X");
    // Cursor parked on "bar", which is not a hit: this must find rather than
    // overwrite whatever happens to be next to the cursor.
    r.ed.setCursor({0, 4});
    r.ed.clearSelection();
    r.bar.replaceCurrent();
    CHECK_EQ(r.ed.text().toStdString(), std::string("foo bar foo"));
    CHECK_EQ(r.sel().toStdString(), std::string("foo"));

    // Now it is on a hit.
    r.bar.replaceCurrent();
    CHECK_EQ(r.ed.text().toStdString(), std::string("foo bar X"));
}

TEST(replace_all_rewrites_everything_in_one_undo_step) {
    Rig r("foo foo foo\nfoo");
    r.bar.setSearchText("foo");
    r.bar.setReplaceText("bar");
    r.bar.replaceAll();
    CHECK_EQ(r.ed.text().toStdString(), std::string("bar bar bar\nbar"));
    CHECK_EQ(r.status.toStdString(), std::string("replaced 4"));

    // ONE press, not four. A mark per replacement would make undoing a large
    // replace-all a job rather than a keystroke.
    CHECK(r.ed.undo()->undo());
    CHECK_EQ(r.ed.text().toStdString(), std::string("foo foo foo\nfoo"));
}

TEST(editing_the_document_refreshes_the_matches) {
    // The ranges point into a document that just changed, so leaving them is
    // painting highlights at offsets that no longer mean anything.
    Rig r("foo foo");
    r.bar.setSearchText("foo");
    CHECK_EQ(r.ed.searchMatchCount(), 2);
    r.ed.document()->insert({0, 0}, "foo ");
    r.bar.refresh();
    CHECK_EQ(r.ed.searchMatchCount(), 3);
}

TEST(pointing_the_bar_at_another_editor_clears_the_first_ones_highlights) {
    Rig r("foo foo");
    r.bar.setSearchText("foo");
    CHECK_EQ(r.ed.searchMatchCount(), 2);

    EditorWidget other;
    other.setText("foo");
    r.bar.setEditor(&other);
    CHECK_EQ(r.ed.searchMatchCount(), 0);
    r.bar.setEditor(nullptr);
}

TEST(escape_closes_the_bar_and_takes_the_highlights_with_it) {
    Rig r("foo foo");
    r.bar.activate(false);
    r.bar.setSearchText("foo");
    CHECK(r.bar.isVisible());
    CHECK_EQ(r.ed.searchMatchCount(), 2);

    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&r.bar, &esc);
    CHECK(!r.bar.isVisible());
    CHECK_EQ(r.ed.searchMatchCount(), 0);
}

TEST(escape_from_inside_the_text_field_also_closes_it) {
    // The field has focus whenever the bar is open, so if QLineEdit swallowed
    // Escape the key would do nothing where it is actually pressed.
    Rig r("foo");
    r.bar.activate(false);
    QLineEdit *field = r.bar.findChildren<QLineEdit *>().value(0);
    CHECK(field != nullptr);
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(field, &esc);
    CHECK(!r.bar.isVisible());
}

TEST(replace_is_hidden_until_asked_for) {
    Rig r("foo");
    r.bar.activate(false);
    CHECK(!r.bar.replaceVisible());
    r.bar.activate(true);
    CHECK(r.bar.replaceVisible());
}
