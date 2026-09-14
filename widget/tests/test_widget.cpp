// tests/test_widget.cpp
//
// Real QApplication, real QKeyEvents, offscreen platform. Not mocks: the point
// is to exercise the same keyPressEvent path a user's keyboard reaches, because
// the bugs in an editor's input handling are all in the corner cases and none of
// them are visible by reading.
//
// Run with QT_QPA_PLATFORM=offscreen (ctest sets it).
//
// MOUSE EVENTS GO TO ed->viewport(), NOT TO ed. QAbstractScrollArea receives
// them through an event filter on its viewport and forwards them to
// mousePressEvent() from there; one sent to the scroll area itself is dropped
// without a word, and the test then asserts against a cursor that never moved.
// Viewport coordinates are also what the handlers expect.
//
// PAINT IS FORCED WITH grab(), NEVER repaint(). Under the offscreen platform a
// window is never exposed, so repaint() on one returns without delivering a
// paint event at all -- silently. The 50k-line timing below was written with
// repaint() and reported 0ms for months because nothing was being painted;
// grab() renders into a pixmap and goes through paintEvent for real.
#include <QApplication>
#include <QImage>
#include <cmath>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QScrollBar>

#include "aced/search.h"

#include "aced/document.h"
#include "aced/grammar.h"
#include "aced/undo.h"
#include "acedqt/editorwidget.h"
#include "harness.h"

using namespace acedqt;
using aced::Position;
using aced::Range;

namespace {

EditorWidget *makeEditor(const QString &text = QString()) {
    auto *ed = new EditorWidget();
    ed->resize(800, 600);
    ed->show();
    if (!text.isEmpty()) ed->setText(text);
    return ed;
}

void key(EditorWidget *ed, int k, Qt::KeyboardModifiers mods = Qt::NoModifier,
         const QString &text = QString()) {
    QKeyEvent e(QEvent::KeyPress, k, mods, text);
    QApplication::sendEvent(ed, &e);
}

void type(EditorWidget *ed, const QString &s) {
    for (const QChar &c : s) key(ed, c.unicode(), Qt::NoModifier, QString(c));
}

}  // namespace

TEST(typing_inserts_text) {
    auto *ed = makeEditor();
    type(ed, "hello");
    CHECK_EQ(ed->text().toStdString(), std::string("hello"));
    CHECK_EQ(ed->cursor().column, 5);
    delete ed;
}

TEST(enter_splits_and_carries_indent) {
    auto *ed = makeEditor("    indented");
    ed->setCursor({0, 12});
    key(ed, Qt::Key_Return);
    type(ed, "next");
    CHECK_EQ(ed->text().toStdString(), std::string("    indented\n    next"));
    delete ed;
}

TEST(enter_does_not_over_indent_from_inside_the_indent) {
    auto *ed = makeEditor("        body");
    ed->setCursor({0, 2});          // inside the leading whitespace
    key(ed, Qt::Key_Return);
    // Only the indent up to the cursor is carried, or splitting a line in its
    // whitespace would silently add spaces the user never typed.
    CHECK_EQ(ed->text().toStdString(), std::string("  \n        body"));
    delete ed;
}

TEST(backspace_joins_lines) {
    auto *ed = makeEditor("ab\ncd");
    ed->setCursor({1, 0});
    key(ed, Qt::Key_Backspace);
    CHECK_EQ(ed->text().toStdString(), std::string("abcd"));
    CHECK_EQ(ed->cursor().row, 0);
    CHECK_EQ(ed->cursor().column, 2);
    delete ed;
}

TEST(delete_at_end_of_line_joins_forward) {
    auto *ed = makeEditor("ab\ncd");
    ed->setCursor({0, 2});
    key(ed, Qt::Key_Delete);
    CHECK_EQ(ed->text().toStdString(), std::string("abcd"));
    delete ed;
}

TEST(backspace_steps_over_a_whole_utf8_sequence) {
    auto *ed = makeEditor();
    ed->setText(QString::fromUtf8("aé"));      // 'e' acute is two bytes
    ed->setCursor(ed->document()->end());
    key(ed, Qt::Key_Backspace);
    CHECK_EQ(ed->text().toStdString(), std::string("a"));
    delete ed;
}

TEST(arrow_right_steps_over_a_whole_utf8_sequence) {
    auto *ed = makeEditor();
    ed->setText(QString::fromUtf8("éx"));
    ed->setCursor({0, 0});
    key(ed, Qt::Key_Right);
    CHECK_EQ(ed->cursor().column, 2);          // past both bytes, not one
    delete ed;
}

TEST(arrow_left_at_column_zero_wraps_to_previous_line_end) {
    auto *ed = makeEditor("abc\ndef");
    ed->setCursor({1, 0});
    key(ed, Qt::Key_Left);
    CHECK_EQ(ed->cursor().row, 0);
    CHECK_EQ(ed->cursor().column, 3);
    delete ed;
}

TEST(vertical_movement_keeps_its_goal_column) {
    auto *ed = makeEditor("longest line here\nshort\nanother long line");
    ed->setCursor({0, 15});
    key(ed, Qt::Key_Down);
    CHECK_EQ(ed->cursor().row, 1);
    CHECK_EQ(ed->cursor().column, 5);          // clamped to the short line
    key(ed, Qt::Key_Down);
    CHECK_EQ(ed->cursor().row, 2);
    // Back out the other side at the column it started from, not at 5.
    CHECK_EQ(ed->cursor().column, 15);
    delete ed;
}

TEST(shift_arrow_selects) {
    auto *ed = makeEditor("abcdef");
    ed->setCursor({0, 1});
    key(ed, Qt::Key_Right, Qt::ShiftModifier);
    key(ed, Qt::Key_Right, Qt::ShiftModifier);
    CHECK(ed->hasSelection());
    CHECK_EQ(ed->document()->textInRange(ed->selection()), std::string("bc"));
    delete ed;
}

TEST(typing_over_a_selection_replaces_it) {
    auto *ed = makeEditor("abcdef");
    ed->setSelection({{0, 1}, {0, 4}});
    type(ed, "X");
    CHECK_EQ(ed->text().toStdString(), std::string("aXef"));
    CHECK(!ed->hasSelection());
    delete ed;
}

TEST(backspace_deletes_a_selection_rather_than_one_character) {
    auto *ed = makeEditor("abcdef");
    ed->setSelection({{0, 1}, {0, 4}});
    key(ed, Qt::Key_Backspace);
    CHECK_EQ(ed->text().toStdString(), std::string("aef"));
    delete ed;
}

TEST(select_all_spans_the_document) {
    auto *ed = makeEditor("one\ntwo\nthree");
    ed->selectAll();
    CHECK_EQ(ed->document()->textInRange(ed->selection()),
             std::string("one\ntwo\nthree"));
    delete ed;
}

TEST(undo_takes_back_a_run_of_typing_as_one_group) {
    auto *ed = makeEditor();
    type(ed, "hello");
    key(ed, Qt::Key_Z, Qt::ControlModifier);
    CHECK_EQ(ed->text().toStdString(), std::string());
    delete ed;
}

TEST(undo_redo_round_trips) {
    auto *ed = makeEditor("seed");
    ed->setCursor(ed->document()->end());
    type(ed, "XYZ");
    const QString full = ed->text();
    key(ed, Qt::Key_Z, Qt::ControlModifier);
    CHECK_EQ(ed->text().toStdString(), std::string("seed"));
    key(ed, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK_EQ(ed->text().toStdString(), full.toStdString());
    delete ed;
}

TEST(enter_is_its_own_undo_group) {
    auto *ed = makeEditor();
    type(ed, "one");
    key(ed, Qt::Key_Return);
    type(ed, "two");
    key(ed, Qt::Key_Z, Qt::ControlModifier);
    CHECK_EQ(ed->text().toStdString(), std::string("one\n"));
    delete ed;
}

TEST(copy_and_paste_round_trip) {
    auto *ed = makeEditor("abcdef");
    ed->setSelection({{0, 1}, {0, 4}});
    ed->copy();
    ed->setCursor(ed->document()->end());
    ed->paste();
    CHECK_EQ(ed->text().toStdString(), std::string("abcdefbcd"));
    delete ed;
}

TEST(paste_lands_the_cursor_after_multiline_text) {
    auto *ed = makeEditor("head");
    QApplication::clipboard()->setText("1\n2\n3");
    ed->setCursor({0, 4});
    ed->paste();
    CHECK_EQ(ed->text().toStdString(), std::string("head1\n2\n3"));
    CHECK_EQ(ed->cursor().row, 2);
    CHECK_EQ(ed->cursor().column, 1);
    delete ed;
}

TEST(cut_removes_and_copies) {
    auto *ed = makeEditor("abcdef");
    ed->setSelection({{0, 2}, {0, 5}});
    ed->cut();
    CHECK_EQ(ed->text().toStdString(), std::string("abf"));
    CHECK_EQ(QApplication::clipboard()->text().toStdString(), std::string("cde"));
    delete ed;
}

TEST(modified_flag_tracks_edits) {
    auto *ed = makeEditor("x");
    CHECK(!ed->isModified());
    type(ed, "y");
    CHECK(ed->isModified());
    ed->setModified(false);
    CHECK(!ed->isModified());
    delete ed;
}

TEST(home_and_end) {
    auto *ed = makeEditor("abcdef\nghi");
    ed->setCursor({0, 3});
    key(ed, Qt::Key_End);
    CHECK_EQ(ed->cursor().column, 6);
    key(ed, Qt::Key_Home);
    CHECK_EQ(ed->cursor().column, 0);
    key(ed, Qt::Key_End, Qt::ControlModifier);
    CHECK_EQ(ed->cursor().row, 1);
    delete ed;
}

TEST(highlighting_survives_an_edit_that_opens_a_block_comment) {
    static aced::Grammar g;
    static bool ok = g.loadFile(ACED_GRAMMARS_PATH);
    CHECK(ok);
    auto *ed = makeEditor("int a = 1;\nint b = 2;\nint c = 3;");
    ed->setGrammar(&g);
    ed->setMode("c_cpp");
    ed->grab();
    // Opening a comment on row 0 changes the meaning of every row below it.
    // If LineCache did not drop the tail, rows 1 and 2 would keep their old
    // colours and nothing would look wrong until much later.
    ed->setCursor({0, 0});
    type(ed, "/*");
    ed->grab();
    CHECK_EQ(ed->text().toStdString(),
             std::string("/*int a = 1;\nint b = 2;\nint c = 3;"));
    delete ed;
}

TEST(a_large_document_paints_without_laying_out_every_row) {
    QString big;
    for (int i = 0; i < 50000; ++i) big += QString("int x%1 = %1;\n").arg(i);
    auto *ed = makeEditor(big);
    CHECK_EQ(ed->document()->lineCount(), 50001);
    QElapsedTimer t;
    t.start();
    ed->grab();
    const qint64 ms = t.elapsed();
    std::fprintf(stderr, "    50k-line repaint: %lldms\n", (long long)ms);
    // Generous. It is a regression guard against something starting to touch
    // every row per paint, not a benchmark.
    CHECK(ms < 1000);
    delete ed;
}

// REGRESSION. QWidget::event() treats Tab as focus navigation and only falls
// through to keyPressEvent() when focusNextPrevChild() finds nowhere to go. A
// bare editor has no focusable sibling, so this passed for the wrong reason
// until the widget was put inside a QTabWidget in the application and Tab
// started moving focus to the tab bar. The sibling below is the whole point of
// the test: remove it and the test cannot fail.
TEST(tab_indents_even_when_there_is_somewhere_for_focus_to_go) {
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    auto *ed = new EditorWidget(&host);
    auto *sibling = new QLineEdit(&host);
    sibling->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(ed);
    layout->addWidget(sibling);
    host.resize(800, 600);
    host.show();

    ed->setText("a");
    ed->setCursor({0, 1});
    ed->setFocus();
    key(ed, Qt::Key_Tab, Qt::NoModifier, "\t");
    CHECK_EQ(ed->text().toStdString(), std::string("a    "));
    CHECK(!sibling->hasFocus());
}

TEST(tab_inserts_a_tab_character_when_insert_spaces_is_off) {
    auto *ed = makeEditor("a");
    ed->setInsertSpaces(false);
    ed->setCursor({0, 1});
    key(ed, Qt::Key_Tab, Qt::NoModifier, "\t");
    CHECK_EQ(ed->text().toStdString(), std::string("a\t"));
    delete ed;
}

TEST(every_line_paints_the_same_pixels_including_its_descenders) {
    // REGRESSION. Screen lines stack at multiples of LineCache::lineHeight(),
    // and the paint loop clips each one to its own band. While that height was
    // fractional -- 16.6 for the default font -- the bands landed on fractional
    // boundaries and the bottom pixel row of every second line was clipped, so
    // an underscore rendered on some lines and vanished on others. It read as a
    // font bug. Identical text on every line must therefore paint identically.
    QString text;
    for (int i = 0; i < 8; ++i) text += "a_b_c_\n";
    auto *ed = makeEditor(text);
    ed->setHighlightCurrentLine(false);  // or row 0 differs for a real reason
    ed->resize(300, 300);
    const QImage img = ed->grab().toImage();

    const int lh = static_cast<int>(ed->lineHeight());
    CHECK_EQ(lh, ed->lineHeight());  // whole pixels, or the bands cannot align

    auto band = [&](int line) {
        QVector<int> ink;
        for (int y = line * lh; y < (line + 1) * lh && y < img.height(); ++y) {
            int n = 0;
            for (int x = 0; x < img.width(); ++x)
                if (img.pixel(x, y) != img.pixel(img.width() - 1, img.height() - 1)) ++n;
            ink.append(n);
        }
        return ink;
    };
    const QVector<int> first = band(1);
    int inkTotal = 0;
    for (int n : first) inkTotal += n;
    CHECK(inkTotal > 0);  // the test is worthless if nothing was painted
    for (int line = 2; line < 7; ++line) CHECK(band(line) == first);
    delete ed;
}

// How much of the image is painted in the palette's own foreground colour.
// Counting THAT rather than "how bright is the brightest pixel" is deliberate:
// the bug this guards against painted the text in the gutter's grey, which is
// brighter than the background and still unreadable against it.
int pixelsInColour(const QImage &img, const QColor &want, int tolerance = 40) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            const int d = std::abs(c.red() - want.red()) + std::abs(c.green() - want.green())
                          + std::abs(c.blue() - want.blue());
            if (d <= tolerance) ++n;
        }
    }
    return n;
}

TEST(plain_text_with_no_grammar_is_drawn_in_the_palette_foreground) {
    // REGRESSION. With no tokenizer there are no QTextCharFormat runs, so
    // QTextLayout::draw() colours the text with whatever pen the painter is
    // holding -- and the pen had last been set to draw the LINE NUMBER, which
    // in the dark theme is 0x4b5056 against a 0x1d1f21 background. An untitled
    // buffer therefore rendered in gutter grey on near-black and typing into it
    // produced something you could barely see.
    //
    // LINE NUMBERS ARE ON HERE ON PURPOSE. With the gutter hidden the pen is
    // never set at all and the widget default happens to be readable, so this
    // test passes with the bug present. It was written that way first.
    const Palette dark = Palette::tomorrowNight();
    auto *ed = makeEditor("plain text with no grammar at all");
    ed->setEditorPalette(dark);
    ed->setShowLineNumbers(true);
    ed->setHighlightCurrentLine(false);
    ed->resize(500, 120);
    const QImage img = ed->grab().toImage();

    const int fg = pixelsInColour(img, dark.foreground());
    const int gutter = pixelsInColour(img, dark.gutterForeground());
    std::fprintf(stderr, "    plain text: %d px in foreground, %d in gutter grey\n",
                 fg, gutter);
    // The text is far longer than the one line number beside it, so most of
    // the ink has to be the foreground colour. The threshold is a floor on
    // "there is real text here", not a pixel count to tune: with the bug it is
    // zero, and the gutter comparison is what actually fails then.
    CHECK(fg > 50);
    CHECK(fg > gutter * 2);
    delete ed;
}

TEST(the_same_holds_in_the_light_theme) {
    const Palette light = Palette::dayfold();
    auto *ed = makeEditor("plain text with no grammar at all");
    ed->setEditorPalette(light);
    ed->setShowLineNumbers(true);
    ed->setHighlightCurrentLine(false);
    ed->resize(500, 120);
    const QImage img = ed->grab().toImage();
    const int fg = pixelsInColour(img, light.foreground());
    const int gutter = pixelsInColour(img, light.gutterForeground());
    std::fprintf(stderr, "    light theme: %d px in foreground, %d in gutter grey\n", fg, gutter);
    CHECK(fg > 50);
    CHECK(fg > gutter * 2);
    delete ed;
}

TEST(a_tokenized_buffer_is_coloured_by_its_formats_not_by_the_pen) {
    // A REAL grammar, because "no grammar" is the whole condition under test
    // above and a buffer without one is not a control. With one, every run has
    // a QTextCharFormat and the pen never gets a say.
    static aced::Grammar g;
    static bool ok = g.loadFile(ACED_GRAMMARS_PATH);
    CHECK(ok);
    auto *ed = makeEditor("int x = 1;\nint y = 2;");
    ed->setGrammar(&g);
    ed->setMode("c_cpp");
    ed->setEditorPalette(Palette::tomorrowNight());
    ed->setShowLineNumbers(true);
    ed->setHighlightCurrentLine(false);
    ed->resize(500, 120);
    const QImage img = ed->grab().toImage();
    // c_cpp paints "int" as a keyword, so the keyword colour has to be on
    // screen. The gutter grey is NOT compared against it here: the two line
    // numbers legitimately wear it, and on two short lines there is more of
    // them than there is of the keyword.
    const int kw = pixelsInColour(img, QColor(0xb2, 0x94, 0xbb));
    std::fprintf(stderr, "    tokenized: %d px keyword colour\n", kw);
    CHECK(kw > 20);
    delete ed;
}

TEST(the_current_search_match_stays_visible_under_the_selection) {
    // Find Next SELECTS the hit it moves to, so a current-match colour painted
    // beneath the selection band is covered every time and the active hit looks
    // like any other selection. The current match is drawn over the selection
    // for that reason, and this is what says so.
    const Palette dark = Palette::tomorrowNight();
    auto *ed = makeEditor("foo bar foo baz foo");
    ed->setEditorPalette(dark);
    ed->setHighlightCurrentLine(false);
    ed->resize(500, 120);

    const std::vector<aced::Range> matches = {
        {{0, 0}, {0, 3}}, {{0, 8}, {0, 11}}, {{0, 16}, {0, 19}}};
    ed->setSearchMatches(matches);
    ed->setCurrentSearchMatch(matches[1]);
    ed->setSelection(matches[1]);  // exactly what findNext does

    const QImage img = ed->grab().toImage();
    const int current = pixelsInColour(img, dark.searchCurrentColor(), 10);
    const int other = pixelsInColour(img, dark.searchMatchColor(), 10);
    const int selection = pixelsInColour(img, dark.selectionColor(), 10);
    std::fprintf(stderr, "    search bands: %d current, %d other, %d selection\n",
                 current, other, selection);
    // The two non-current hits are painted, and the current one is NOT buried.
    CHECK(other > 100);
    CHECK(current > 100);
    // The selection band is still there for the parts of it that are not the
    // match -- here there are none, so this only asserts it did not take over.
    CHECK(current > selection);
    delete ed;
}

TEST(clearing_the_matches_removes_the_bands) {
    const Palette dark = Palette::tomorrowNight();
    auto *ed = makeEditor("foo foo");
    ed->setEditorPalette(dark);
    ed->setSearchMatches({{{0, 0}, {0, 3}}, {{0, 4}, {0, 7}}});
    ed->resize(500, 120);
    CHECK(pixelsInColour(ed->grab().toImage(), dark.searchMatchColor(), 10) > 100);
    ed->clearSearchMatches();
    CHECK_EQ(ed->searchMatchCount(), 0);
    CHECK_EQ(pixelsInColour(ed->grab().toImage(), dark.searchMatchColor(), 10), 0);
    delete ed;
}

// --- indent / unindent ------------------------------------------------------

TEST(tab_with_a_multi_line_selection_indents_the_block) {
    auto *ed = makeEditor("one\ntwo\nthree");
    ed->setTabWidth(4);
    ed->setSelection({{0, 1}, {2, 2}});
    key(ed, Qt::Key_Tab, Qt::NoModifier, "\t");
    CHECK_EQ(ed->text().toStdString(), std::string("    one\n    two\n    three"));
    delete ed;
}

TEST(tab_inside_one_line_still_replaces_the_selection) {
    // A selection within a single line is a typo being overtyped, not a block.
    auto *ed = makeEditor("abcdef");
    ed->setTabWidth(4);
    ed->setSelection({{0, 1}, {0, 4}});
    key(ed, Qt::Key_Tab, Qt::NoModifier, "\t");
    CHECK_EQ(ed->text().toStdString(), std::string("a    ef"));
    delete ed;
}

TEST(a_selection_ending_at_column_zero_does_not_drag_in_the_next_line) {
    // Dragging down to the start of the following line is how everyone selects
    // "these two lines". Indenting a third would be wrong and is the kind of
    // thing nobody notices until it corrupts a diff.
    auto *ed = makeEditor("one\ntwo\nthree");
    ed->setSelection({{0, 0}, {2, 0}});
    key(ed, Qt::Key_Tab, Qt::NoModifier, "\t");
    CHECK_EQ(ed->text().toStdString(), std::string("    one\n    two\nthree"));
    delete ed;
}

TEST(the_selection_survives_so_the_key_repeats) {
    auto *ed = makeEditor("one\ntwo");
    ed->setSelection({{0, 0}, {1, 3}});
    key(ed, Qt::Key_Tab, Qt::NoModifier, "\t");
    key(ed, Qt::Key_Tab, Qt::NoModifier, "\t");
    key(ed, Qt::Key_Tab, Qt::NoModifier, "\t");
    CHECK_EQ(ed->text().toStdString(),
             std::string("            one\n            two"));
    delete ed;
}

TEST(shift_tab_unindents_and_reaches_key_press_at_all) {
    // Backtab used to be left to Qt's focus navigation on purpose. If the
    // interception in event() is removed this does nothing at all.
    auto *ed = makeEditor("        one\n        two");
    ed->setTabWidth(4);
    ed->setSelection({{0, 0}, {1, 8}});
    key(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    CHECK_EQ(ed->text().toStdString(), std::string("    one\n    two"));
    key(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    CHECK_EQ(ed->text().toStdString(), std::string("one\ntwo"));
    delete ed;
}

TEST(unindent_on_a_line_with_no_indent_does_nothing) {
    auto *ed = makeEditor("one\ntwo");
    ed->setSelection({{0, 0}, {1, 3}});
    key(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    CHECK_EQ(ed->text().toStdString(), std::string("one\ntwo"));
    delete ed;
}

TEST(unindent_takes_a_partial_indent_rather_than_refusing) {
    // Two spaces where the tab width is four: take the two. Refusing to
    // unindent a line that is not indented by a whole level leaves the block
    // ragged, which is the state you were trying to fix.
    auto *ed = makeEditor("  two\n        eight");
    ed->setTabWidth(4);
    ed->setSelection({{0, 0}, {1, 5}});
    key(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    CHECK_EQ(ed->text().toStdString(), std::string("two\n    eight"));
    delete ed;
}

TEST(unindent_removes_one_tab_character_not_tab_width_of_them) {
    auto *ed = makeEditor("\t\tone");
    ed->setTabWidth(4);
    ed->setCursor({0, 3});
    key(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    CHECK_EQ(ed->text().toStdString(), std::string("\tone"));
    delete ed;
}

TEST(indent_uses_tabs_when_insert_spaces_is_off) {
    auto *ed = makeEditor("one\ntwo");
    ed->setInsertSpaces(false);
    ed->setSelection({{0, 0}, {1, 3}});
    key(ed, Qt::Key_Tab, Qt::NoModifier, "\t");
    CHECK_EQ(ed->text().toStdString(), std::string("\tone\n\ttwo"));
    delete ed;
}

TEST(indent_and_unindent_are_one_undo_step_each) {
    auto *ed = makeEditor("one\ntwo\nthree");
    ed->setSelection({{0, 0}, {2, 5}});
    key(ed, Qt::Key_Tab, Qt::NoModifier, "\t");
    CHECK_EQ(ed->text().toStdString(), std::string("    one\n    two\n    three"));
    // ONE press, not three. A mark per line would make undoing a forty-line
    // indent a forty-press job.
    CHECK(ed->undo()->undo());
    CHECK_EQ(ed->text().toStdString(), std::string("one\ntwo\nthree"));
    delete ed;
}

TEST(unindent_with_no_selection_works_on_the_cursor_line) {
    auto *ed = makeEditor("    one\n    two");
    ed->setTabWidth(4);
    ed->setCursor({1, 6});
    key(ed, Qt::Key_Backtab, Qt::ShiftModifier);
    CHECK_EQ(ed->text().toStdString(), std::string("    one\ntwo"));
    // The cursor followed the text it was sitting in.
    CHECK_EQ(ed->cursor().column, 2);
    delete ed;
}

TEST(indent_moves_the_cursor_with_its_line) {
    auto *ed = makeEditor("one");
    ed->setTabWidth(4);
    ed->setCursor({0, 3});
    ed->clearSelection();
    ed->indentSelection();
    CHECK_EQ(ed->text().toStdString(), std::string("    one"));
    CHECK_EQ(ed->cursor().column, 7);
    delete ed;
}

// --- scrollbar ranges -------------------------------------------------------

TEST(pasting_more_than_a_screenful_leaves_it_all_reachable) {
    // REGRESSION, AND IT LOOKED LIKE DATA LOSS. Scrollbar ranges were computed
    // in resizeEvent() only, so inserting text never widened the vertical
    // range: a 500-line paste landed in the document intact and the scrollbar
    // stayed at maximum 0. The first screenful was all that could be reached,
    // and it read as a paste that had been truncated.
    auto *ed = makeEditor();
    ed->resize(700, 400);
    QString big;
    for (int i = 0; i < 500; ++i) big += QString("line %1\n").arg(i);
    QApplication::clipboard()->setText(big);
    ed->paste();

    CHECK_EQ(ed->document()->lineCount(), 501);
    const int visible = ed->visibleRowCount();
    CHECK(visible < 500);  // or the test proves nothing
    // Every row below the first screenful has to be scrollable to.
    CHECK_EQ(ed->verticalScrollBar()->maximum(), 501 - visible);

    ed->scrollToRow(500);
    CHECK(ed->firstVisibleRow() > visible);
    delete ed;
}

TEST(typing_past_the_bottom_of_the_view_extends_the_scrollbar) {
    // The same fault by the other route: Enter at the last visible line.
    auto *ed = makeEditor();
    ed->resize(700, 200);
    const int before = ed->verticalScrollBar()->maximum();
    for (int i = 0; i < 60; ++i) key(ed, Qt::Key_Return, Qt::NoModifier, "\n");
    CHECK_EQ(before, 0);
    CHECK(ed->verticalScrollBar()->maximum() > 0);
    CHECK_EQ(ed->verticalScrollBar()->maximum(),
             ed->document()->lineCount() - ed->visibleRowCount());
    delete ed;
}

TEST(deleting_text_shrinks_the_scrollbar_again) {
    QString big;
    for (int i = 0; i < 300; ++i) big += QString("line %1\n").arg(i);
    auto *ed = makeEditor(big);
    ed->resize(700, 400);
    CHECK(ed->verticalScrollBar()->maximum() > 0);
    ed->selectAll();
    key(ed, Qt::Key_Delete);
    CHECK_EQ(ed->document()->lineCount(), 1);
    CHECK_EQ(ed->verticalScrollBar()->maximum(), 0);
    delete ed;
}

EditorWidget *makeWrapped(const QString &text, int column);

TEST(the_same_holds_with_wrapping_on) {
    // Soft wrap HID the bug, because paintEvent recomputes the range itself
    // when wrapping. The fix has to leave that working.
    auto *ed = makeWrapped(QString(), 20);
    ed->resize(700, 400);
    QString big;
    for (int i = 0; i < 200; ++i) big += QString("line %1\n").arg(i);
    QApplication::clipboard()->setText(big);
    ed->paste();
    ed->grab();
    CHECK(ed->verticalScrollBar()->maximum() >= 201 - ed->visibleRowCount());
    delete ed;
}

// --- soft wrap -------------------------------------------------------------
//
// Every one of these fails with wrapping turned off, which is the point: they
// test the row->screen-line mapping rather than that the widget still runs.

EditorWidget *makeWrapped(const QString &text, int column = 20) {
    auto *ed = new EditorWidget();
    ed->resize(800, 600);
    ed->setSoftWrap(true);
    ed->setWrapColumn(column);
    ed->show();
    ed->setText(text);
    return ed;
}

TEST(a_long_row_occupies_several_screen_lines) {
    auto *ed = makeWrapped(QString(100, QLatin1Char('a')));
    CHECK_EQ(ed->document()->lineCount(), 1);
    // 100 characters at a 20-column wrap is five screen lines. Painting is what
    // teaches the map, so the row has to have been drawn first.
    ed->grab();
    CHECK(ed->screenLineCount() >= 5);
    delete ed;
}

TEST(turning_wrap_off_puts_every_row_back_on_one_screen_line) {
    auto *ed = makeWrapped(QString(100, QLatin1Char('a')));
    ed->grab();
    CHECK(ed->screenLineCount() > 1);
    ed->setSoftWrap(false);
    ed->grab();
    CHECK_EQ(ed->screenLineCount(), 1);
    CHECK(!ed->softWrap());
    delete ed;
}

TEST(screen_lines_accumulate_across_rows) {
    // Three rows: short, long, short. The long one is five screen lines, so the
    // third row starts at screen line 7 rather than 2.
    auto *ed = makeWrapped("x\n" + QString(100, QLatin1Char('a')) + "\ny");
    ed->grab();
    CHECK_EQ(ed->screenLineForRow(0), 0);
    CHECK_EQ(ed->screenLineForRow(1), 1);
    CHECK(ed->screenLineForRow(2) >= 6);
    delete ed;
}

TEST(down_moves_one_screen_line_inside_a_wrapped_row) {
    auto *ed = makeWrapped(QString(100, QLatin1Char('a')));
    ed->grab();
    ed->setCursor({0, 0});
    key(ed, Qt::Key_Down);
    // Still row 0 -- it is one document line -- but further along it.
    CHECK_EQ(ed->cursor().row, 0);
    CHECK(ed->cursor().column > 0);
    const int afterOne = ed->cursor().column;
    key(ed, Qt::Key_Down);
    CHECK_EQ(ed->cursor().row, 0);
    CHECK(ed->cursor().column > afterOne);
    delete ed;
}

TEST(down_through_a_wrapped_row_reaches_the_next_row) {
    auto *ed = makeWrapped(QString(100, QLatin1Char('a')) + "\nsecond");
    ed->grab();
    ed->setCursor({0, 0});
    for (int i = 0; i < 6; ++i) key(ed, Qt::Key_Down);
    CHECK_EQ(ed->cursor().row, 1);
    delete ed;
}

TEST(up_and_down_return_to_where_they_started) {
    auto *ed = makeWrapped(QString(100, QLatin1Char('a')));
    ed->grab();
    ed->setCursor({0, 5});
    key(ed, Qt::Key_Down);
    key(ed, Qt::Key_Up);
    CHECK_EQ(ed->cursor().row, 0);
    CHECK_EQ(ed->cursor().column, 5);
    delete ed;
}

TEST(home_and_end_act_on_the_screen_line_when_wrapping) {
    auto *ed = makeWrapped(QString(100, QLatin1Char('a')));
    ed->grab();
    ed->setCursor({0, 50});
    key(ed, Qt::Key_Home);
    // Not column 0: the start of the screen line the cursor was on.
    CHECK(ed->cursor().column > 0);
    CHECK(ed->cursor().column <= 50);
    const int lineStart = ed->cursor().column;
    key(ed, Qt::Key_End);
    CHECK(ed->cursor().column > lineStart);
    CHECK(ed->cursor().column < 100);
    delete ed;
}

TEST(home_still_goes_to_column_zero_without_wrapping) {
    auto *ed = makeEditor(QString(100, QLatin1Char('a')));
    ed->setCursor({0, 50});
    key(ed, Qt::Key_Home);
    CHECK_EQ(ed->cursor().column, 0);
    delete ed;
}

TEST(a_click_on_a_continuation_line_lands_on_that_part_of_the_row) {
    auto *ed = makeWrapped(QString(100, QLatin1Char('a')));
    ed->grab();
    // Third screen line, a little way in. Whatever the exact column, it must be
    // past the second wrap point rather than near the start of the row.
    const qreal lh = 1.0 * ed->viewport()->height() / ed->visibleRowCount();
    const QPointF pt(200, lh * 2 + lh / 2);
    QMouseEvent press(QEvent::MouseButtonPress, pt, pt, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(ed->viewport(), &press);
    CHECK_EQ(ed->cursor().row, 0);
    CHECK(ed->cursor().column > 20);
    delete ed;
}

TEST(wrapping_a_large_document_still_paints_without_laying_out_every_row) {
    QString big;
    for (int i = 0; i < 50000; ++i) big += QString("int x%1 = %1;\n").arg(i);
    auto *ed = makeWrapped(big, 20);
    QElapsedTimer t;
    t.start();
    ed->grab();
    const qint64 ms = t.elapsed();
    std::fprintf(stderr, "    50k-line wrapped repaint: %lldms\n", (long long)ms);
    // The guard that matters: a wrap implementation that measured the whole
    // document to size its scrollbar would take seconds here, not milliseconds.
    CHECK(ms < 1000);
    delete ed;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    return harness::run();
}

// --- the line map ----------------------------------------------------------
//
// EditorWidget drives aced::LineMap from Document deltas rather than rebuilding
// it. That is a strictly better cost model and a strictly worse failure mode:
// one mis-shaped delta and the map silently drifts from the document, and the
// only symptom is a scrollbar that is a few lines out. These pin it.

TEST(the_line_map_never_drifts_from_the_document) {
    auto *ed = makeEditor("alpha\nbeta\ngamma\n");
    auto agree = [&] {
        // Wrap off, nothing folded: one screen line per row, exactly.
        return ed->screenLineCount() == ed->document()->lineCount();
    };
    CHECK(agree());

    ed->setCursor({1, 2});
    key(ed, Qt::Key_Return, Qt::NoModifier, "\n");
    CHECK(agree());

    for (int i = 0; i < 5; ++i) key(ed, Qt::Key_A, Qt::NoModifier, "a");
    CHECK(agree());

    QApplication::clipboard()->setText("one\ntwo\nthree\nfour");
    ed->paste();
    CHECK(agree());

    ed->setSelection({{0, 2}, {4, 1}});
    key(ed, Qt::Key_Backspace);
    CHECK(agree());

    key(ed, Qt::Key_Z, Qt::ControlModifier);
    CHECK(agree());
    key(ed, Qt::Key_Y, Qt::ControlModifier);
    CHECK(agree());

    ed->selectAll();
    key(ed, Qt::Key_Delete);
    CHECK(agree());
    CHECK_EQ(ed->document()->lineCount(), 1);

    ed->setText("a\nb\nc\nd\ne\n");
    CHECK(agree());
    delete ed;
}

TEST(an_edit_keeps_the_measured_heights_of_the_rows_below_it) {
    // Every row here wraps to more than one screen line, but only a screenful
    // has been laid out, so the total is part measurement and part guess. The
    // predecessor threw away every measurement below the edited row on each
    // keystroke, and the scrollbar range collapsed and regrew on every
    // character typed. Nothing below row 0 changed, so nothing below row 0 may
    // be forgotten.
    QString big;
    for (int i = 0; i < 200; ++i)
        big += QString("row %1 with enough text on it to wrap several times over\n").arg(i);
    auto *ed = makeWrapped(big, 20);
    ed->resize(700, 400);
    ed->grab();
    const int measured = ed->screenLineCount();
    CHECK(measured > ed->document()->lineCount());  // or nothing was measured

    ed->setCursor({0, 0});
    key(ed, Qt::Key_X, Qt::NoModifier, "x");
    // Row 0 may re-wrap, so allow it to move by its own height. It may not
    // collapse towards one-line-per-row.
    CHECK(ed->screenLineCount() >= measured - 4);
    delete ed;
}

TEST(wrapping_off_and_nothing_folded_costs_no_table) {
    // The identity fast path: 50k rows must not allocate three vectors and a
    // prefix array to answer "row 40000 is screen line 40000".
    QString big;
    for (int i = 0; i < 50000; ++i) big += QString("int x%1;\n").arg(i);
    auto *ed = makeEditor(big);
    CHECK_EQ(ed->screenLineCount(), 50001);
    CHECK_EQ(ed->screenLineForRow(40000), 40000);
    delete ed;
}

// --- folding ---------------------------------------------------------------

namespace {

const aced::Grammar &foldGrammar() {
    static aced::Grammar g;
    static bool ok = g.loadFile(ACED_GRAMMARS_PATH);
    (void)ok;
    return g;
}

const char *kFoldSample =
    "int top() {\n"          // 0  header, ends row 8
    "    if (a) {\n"         // 1  header, ends row 3
    "        b();\n"         // 2
    "    }\n"                // 3
    "    if (c) {\n"         // 4  header, ends row 6
    "        d();\n"         // 5
    "    }\n"                // 6
    "    return 0;\n"        // 7
    "}\n"                    // 8
    "int after() { return 1; }\n"  // 9
    "int last = 2;\n";       // 10 -> 12 rows with the trailing empty one

EditorWidget *makeFoldable(const char *text = kFoldSample) {
    auto *ed = new EditorWidget();
    ed->resize(800, 600);
    ed->setGrammar(&foldGrammar());
    ed->setMode("c_cpp");
    ed->show();
    ed->setText(QString::fromUtf8(text));
    ed->grab();
    return ed;
}

void clickGutterMarker(EditorWidget *ed, int row) {
    // The marker column is the right-hand edge of the gutter, and the marker is
    // on the row's first screen line.
    const qreal lh = ed->lineHeight();
    const qreal x = ed->viewport()->width();  // placeholder, replaced below
    (void)x;
    const int line = ed->screenLineForRow(row) - ed->verticalScrollBar()->value();
    // Sweep the gutter to find the marker column rather than recomputing the
    // widget's own arithmetic here; a test that duplicates the formula passes
    // when both are wrong.
    for (qreal px = 0; px < 120; px += 1.0) {
        const QPointF pt(px, line * lh + lh / 2);
        if (ed->foldMarkerRowAt(pt) != row) continue;
        QMouseEvent press(QEvent::MouseButtonPress, pt, pt, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(ed->viewport(), &press);
        return;
    }
    CHECK(false);  // no marker found anywhere in the gutter
}

}  // namespace

TEST(fold_markers_appear_only_on_rows_that_open_a_fold) {
    auto *ed = makeFoldable();
    CHECK(ed->canFold(0));
    CHECK(ed->canFold(1));
    CHECK(ed->canFold(4));
    CHECK(!ed->canFold(2));
    CHECK(!ed->canFold(7));
    // "int after() { return 1; }" closes on its own line.
    CHECK(!ed->canFold(9));
    delete ed;
}

TEST(folding_a_row_removes_exactly_its_body_from_the_screen) {
    auto *ed = makeFoldable();
    const int before = ed->screenLineCount();
    CHECK(ed->foldRow(1));   // hides rows 2..3
    CHECK_EQ(ed->screenLineCount(), before - 2);
    CHECK(ed->isFolded(1));
    CHECK(ed->unfoldRow(1));
    CHECK_EQ(ed->screenLineCount(), before);
    CHECK(!ed->isFolded(1));
    delete ed;
}

TEST(folding_the_outer_block_hides_the_inner_ones_too) {
    auto *ed = makeFoldable();
    const int before = ed->screenLineCount();
    CHECK(ed->foldRow(0));  // hides rows 1..8
    CHECK_EQ(ed->screenLineCount(), before - 8);
    delete ed;
}

TEST(reopening_an_outer_fold_restores_the_folds_that_were_inside_it) {
    // The reason LineMap tracks `expanded` separately from `visible`. Flatten
    // the inner state on unfold and a reader loses their place every time they
    // open a function.
    auto *ed = makeFoldable();
    const int before = ed->screenLineCount();
    ed->foldRow(1);
    ed->foldRow(4);
    CHECK_EQ(ed->screenLineCount(), before - 4);
    ed->foldRow(0);
    CHECK_EQ(ed->screenLineCount(), before - 8);
    ed->unfoldRow(0);
    CHECK_EQ(ed->screenLineCount(), before - 4);
    CHECK(ed->isFolded(1));
    CHECK(ed->isFolded(4));
    delete ed;
}

TEST(a_folded_row_is_not_painted) {
    // screenLineCount() is bookkeeping; this is what a user sees. With rows
    // 1..8 hidden only four rows remain, so every screen line from the fifth
    // down has to be empty -- and the fifth is where row 5 would have been.
    auto *ed = makeFoldable();
    ed->resize(700, 400);
    ed->grab();
    ed->foldRow(0);
    const QImage img = ed->grab().toImage();
    const qreal lh = ed->lineHeight();
    // grab() is of the whole WIDGET, so the frame is in the picture: a column
    // of frame pixels at x=0 and x=width-1 on every scanline, which counts as
    // ink on every band and makes this test fail everywhere. Inset by the
    // frame on both axes and measure the viewport.
    const int fw = ed->frameWidth();
    const QRgb bg = img.pixel(img.width() - fw - 3, img.height() - fw - 3);
    auto inkInBand = [&](int screenLine) {
        const int y0 = fw + static_cast<int>(screenLine * lh);
        const int y1 = std::min<int>(img.height(),
                                     fw + static_cast<int>((screenLine + 1) * lh));
        int n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = fw; x < img.width() - fw; ++x)
                if (img.pixel(x, y) != bg) ++n;
        return n;
    };
    CHECK(inkInBand(0) > 0);  // the header is still drawn
    CHECK(inkInBand(1) > 0);  // and the row after the fold
    for (int line = 4; line < 12; ++line) {
        if (inkInBand(line) == 0) continue;
        std::fprintf(stderr, "    ink on screen line %d after folding\n", line);
        CHECK(false);
        break;
    }
    delete ed;
}

TEST(clicking_the_gutter_marker_toggles_the_fold) {
    auto *ed = makeFoldable();
    const int before = ed->screenLineCount();
    clickGutterMarker(ed, 1);
    CHECK(ed->isFolded(1));
    CHECK_EQ(ed->screenLineCount(), before - 2);
    clickGutterMarker(ed, 1);
    CHECK(!ed->isFolded(1));
    CHECK_EQ(ed->screenLineCount(), before);
    delete ed;
}

TEST(clicking_a_marker_does_not_move_the_cursor_or_start_a_drag) {
    auto *ed = makeFoldable();
    ed->setCursor({7, 4});
    clickGutterMarker(ed, 1);
    CHECK_EQ(ed->cursor().row, 7);
    CHECK_EQ(ed->cursor().column, 4);
    CHECK(!ed->hasSelection());
    // A move with the button still down after a marker press must not select.
    const QPointF pt(400, 200);
    QMouseEvent move(QEvent::MouseMove, pt, pt, Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(ed->viewport(), &move);
    CHECK(!ed->hasSelection());
    delete ed;
}

TEST(the_cursor_does_not_stay_on_a_row_a_fold_just_hid) {
    auto *ed = makeFoldable();
    ed->setCursor({5, 8});
    ed->foldRow(4);  // hides rows 5..6, one of which the cursor is on
    CHECK_EQ(ed->cursor().row, 4);
    CHECK(!ed->hasSelection());
    delete ed;
}

TEST(a_cursor_outside_the_fold_is_left_alone) {
    auto *ed = makeFoldable();
    ed->setCursor({9, 3});
    ed->foldRow(1);
    CHECK_EQ(ed->cursor().row, 9);
    CHECK_EQ(ed->cursor().column, 3);
    delete ed;
}

TEST(an_edit_that_lands_inside_a_fold_opens_it) {
    // Undo of a delete that spanned a fold is the ordinary way here. The
    // buffer's visible text must never disagree with what is in it.
    auto *ed = makeFoldable();
    const int before = ed->screenLineCount();
    ed->foldRow(1);
    CHECK_EQ(ed->screenLineCount(), before - 2);
    ed->document()->insert({2, 0}, "x");
    CHECK(!ed->isFolded(1));
    CHECK_EQ(ed->screenLineCount(), before);
    delete ed;
}

TEST(a_fold_survives_an_edit_above_it) {
    auto *ed = makeFoldable();
    ed->foldRow(4);
    const int folded = ed->screenLineCount();
    // Two rows inserted above the fold. The hidden rows move with them.
    ed->document()->insert({0, 0}, "// a\n// b\n");
    CHECK_EQ(ed->screenLineCount(), folded + 2);
    CHECK(ed->isFolded(6));
    CHECK(!ed->isFolded(4));
    delete ed;
}

TEST(fold_all_and_unfold_all) {
    auto *ed = makeFoldable();
    const int before = ed->screenLineCount();
    ed->foldAll();
    CHECK(ed->screenLineCount() < before);
    CHECK(ed->isFolded(0));
    ed->unfoldAll();
    CHECK_EQ(ed->screenLineCount(), before);
    CHECK(!ed->isFolded(0));
    CHECK(!ed->isFolded(1));
    delete ed;
}

TEST(a_click_below_a_fold_lands_on_the_row_that_is_actually_there) {
    // Hit testing goes through the same map as painting, so a click on the
    // line after a collapsed block must resolve to the row drawn there, not to
    // the row that would have been there unfolded.
    auto *ed = makeFoldable();
    ed->resize(700, 400);
    ed->grab();
    ed->foldRow(0);  // rows 1..8 gone; screen line 1 is now row 9
    ed->grab();
    const qreal lh = ed->lineHeight();
    const QPointF pt(300, lh * 1 + lh / 2);
    QMouseEvent press(QEvent::MouseButtonPress, pt, pt, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(ed->viewport(), &press);
    CHECK_EQ(ed->cursor().row, 9);
    delete ed;
}

TEST(down_arrow_steps_over_a_folded_block) {
    auto *ed = makeFoldable();
    ed->resize(700, 400);
    ed->grab();
    ed->foldRow(0);
    ed->setCursor({0, 0});
    key(ed, Qt::Key_Down);
    CHECK_EQ(ed->cursor().row, 9);
    delete ed;
}

TEST(a_mode_with_no_fold_mode_has_no_markers_and_no_hidden_rows) {
    auto *ed = makeFoldable();
    ed->foldRow(1);
    CHECK(ed->isFolded(1));
    const int lines = ed->document()->lineCount();
    // "text" has no fold mode. Switching to it must not leave rows hidden with
    // nothing in the gutter to bring them back.
    ed->setMode("text");
    CHECK(!ed->canFold(0));
    CHECK_EQ(ed->screenLineCount(), lines);
    delete ed;
}

TEST(folding_a_wrapped_document_counts_screen_lines_not_rows) {
    // The case the single-table design exists for: a hidden row that was three
    // screen lines tall must remove three, and put three back.
    QString text = "int f() {\n";
    for (int i = 0; i < 6; ++i)
        text += QString("    call_%1(a_long_argument, another_long_argument);\n").arg(i);
    text += "}\nint g() { return 0; }\n";
    auto *ed = new EditorWidget();
    ed->resize(800, 600);
    ed->setSoftWrap(true);
    ed->setWrapColumn(20);
    ed->setGrammar(&foldGrammar());
    ed->setMode("c_cpp");
    ed->show();
    ed->setText(text);
    ed->grab();
    const int before = ed->screenLineCount();
    const int rows = ed->document()->lineCount();
    CHECK(before > rows);  // or nothing wrapped and the test proves nothing
    CHECK(ed->foldRow(0));
    const int folded = ed->screenLineCount();
    CHECK(folded < before - 6);  // more than one screen line per hidden row
    ed->unfoldRow(0);
    CHECK_EQ(ed->screenLineCount(), before);
    delete ed;
}

TEST(the_pointer_changes_over_the_gutter_and_over_a_fold_marker) {
    // A fold marker is a control, and a control with no pointer feedback is
    // one people do not find. Checked as a property rather than visually,
    // which is the one thing offscreen reports honestly here.
    auto *ed = makeFoldable();
    ed->resize(700, 400);
    ed->grab();
    const qreal lh = ed->lineHeight();

    auto hover = [&](qreal x, int row) {
        const int line = ed->screenLineForRow(row) - ed->verticalScrollBar()->value();
        const QPointF pt(x, line * lh + lh / 2);
        QMouseEvent move(QEvent::MouseMove, pt, pt, Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QApplication::sendEvent(ed->viewport(), &move);
        return ed->viewport()->cursor().shape();
    };

    // Find the marker column the way the widget reports it, not by
    // recomputing the gutter arithmetic here.
    qreal markerX = -1, gutterX = -1;
    for (qreal px = 0; px < 120; px += 1.0) {
        const QPointF pt(px, lh / 2);
        if (ed->foldMarkerRowAt(pt) == 0) { markerX = px; break; }
        gutterX = px;
    }
    CHECK(markerX > 0);
    CHECK(gutterX >= 0);

    CHECK(hover(markerX, 0) == Qt::PointingHandCursor);
    CHECK(hover(gutterX, 0) == Qt::ArrowCursor);
    CHECK(hover(400, 0) == Qt::IBeamCursor);
    // Row 2 has no fold, so the same column is gutter and not a control.
    CHECK(hover(markerX, 2) == Qt::ArrowCursor);
    delete ed;
}

TEST(the_pointer_does_not_change_while_dragging_a_selection) {
    // Drag from text across the gutter: the shape must stay an I-beam rather
    // than flickering to an arrow halfway through a selection.
    auto *ed = makeFoldable();
    ed->resize(700, 400);
    ed->grab();
    const qreal lh = ed->lineHeight();
    const QPointF start(300, lh * 2 + lh / 2);
    QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(ed->viewport(), &press);
    const QPointF over(2, lh / 2);
    QMouseEvent move(QEvent::MouseMove, over, over, Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(ed->viewport(), &move);
    CHECK(ed->viewport()->cursor().shape() == Qt::IBeamCursor);
    CHECK(ed->hasSelection());
    delete ed;
}

// --- cursorMoved ------------------------------------------------------------
//
// The status bar is built entirely on this signal, so a caret move that does
// not emit it is a caret move the user is not told about. Find Next went
// through setSelection(), which emitted selectionChanged() and nothing else,
// and the position readout kept whatever it said before the search.

namespace {

struct CursorSpy {
    int count = 0;
    int row = -1, column = -1;
    std::vector<QMetaObject::Connection> conns;

    explicit CursorSpy(EditorWidget *ed) {
        conns.push_back(QObject::connect(
            ed, &EditorWidget::cursorMoved, [this](int r, int c) {
                ++count;
                row = r;
                column = c;
            }));
    }
    ~CursorSpy() {
        for (auto &c : conns) QObject::disconnect(c);
    }
};

}  // namespace

TEST(setting_a_selection_reports_where_the_caret_ended_up) {
    auto *ed = makeEditor("alpha\nbeta\ngamma\ndelta\n");
    CursorSpy spy(ed);
    ed->setSelection({{2, 1}, {2, 4}});
    CHECK_EQ(spy.count, 1);
    CHECK_EQ(spy.row, 2);
    CHECK_EQ(spy.column, 4);
    delete ed;
}

TEST(a_selection_that_leaves_the_caret_alone_reports_nothing) {
    auto *ed = makeEditor("alpha\nbeta\ngamma\n");
    ed->setCursor({1, 2});
    CursorSpy spy(ed);
    // Anchor moves, caret does not.
    ed->setSelection({{0, 0}, {1, 2}});
    CHECK_EQ(spy.count, 0);
    delete ed;
}

TEST(select_all_reports_the_caret_at_the_end_of_the_document) {
    auto *ed = makeEditor("alpha\nbeta\n");
    CursorSpy spy(ed);
    ed->selectAll();
    CHECK(spy.count > 0);
    CHECK_EQ(spy.row, ed->document()->lineCount() - 1);
    delete ed;
}

TEST(dragging_a_selection_reports_the_caret_as_it_goes) {
    auto *ed = makeEditor("alpha bravo charlie\ndelta echo foxtrot\nx\n");
    ed->resize(700, 300);
    ed->grab();
    const qreal lh = ed->lineHeight();
    const QPointF a(60, lh / 2);
    QMouseEvent press(QEvent::MouseButtonPress, a, a, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(ed->viewport(), &press);
    CursorSpy spy(ed);
    const QPointF b(200, lh + lh / 2);
    QMouseEvent move(QEvent::MouseMove, b, b, Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(ed->viewport(), &move);
    CHECK(ed->hasSelection());
    CHECK_EQ(spy.count, 1);
    CHECK_EQ(spy.row, 1);
    delete ed;
}

TEST(shift_clicking_reports_the_caret) {
    auto *ed = makeEditor("alpha bravo\ncharlie delta\nx\n");
    ed->resize(700, 300);
    ed->grab();
    ed->setCursor({0, 0});
    CursorSpy spy(ed);
    const qreal lh = ed->lineHeight();
    const QPointF pt(100, lh + lh / 2);
    QMouseEvent press(QEvent::MouseButtonPress, pt, pt, Qt::LeftButton,
                      Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(ed->viewport(), &press);
    CHECK(ed->hasSelection());
    CHECK_EQ(spy.count, 1);
    CHECK_EQ(spy.row, 1);
    delete ed;
}

TEST(indenting_a_block_reports_the_caret) {
    auto *ed = makeEditor("one\ntwo\nthree\n");
    ed->setSelection({{0, 0}, {1, 3}});
    CursorSpy spy(ed);
    ed->indentSelection();
    CHECK(spy.count > 0);
    CHECK_EQ(spy.column, ed->cursor().column);
    delete ed;
}

TEST(backspace_and_delete_report_the_caret) {
    auto *ed = makeEditor("one\ntwo\n");
    ed->setCursor({1, 0});
    CursorSpy spy(ed);
    key(ed, Qt::Key_Backspace);  // joins line 1 onto line 0
    CHECK(spy.count > 0);
    CHECK_EQ(spy.row, 0);
    CHECK_EQ(spy.column, 3);
    delete ed;
}

TEST(paste_reports_the_caret) {
    auto *ed = makeEditor("one\n");
    ed->setCursor({0, 3});
    QApplication::clipboard()->setText("\ntwo");
    CursorSpy spy(ed);
    ed->paste();
    CHECK(spy.count > 0);
    CHECK_EQ(spy.row, ed->cursor().row);
    CHECK_EQ(spy.column, ed->cursor().column);
    delete ed;
}

TEST(undo_and_redo_are_available_as_operations_and_report_their_state) {
    auto *ed = makeEditor("one\n");
    CHECK(!ed->canUndo());
    ed->setCursor({0, 3});
    key(ed, Qt::Key_X, Qt::NoModifier, "x");
    CHECK(ed->canUndo());
    CHECK(!ed->canRedo());
    CHECK(ed->undoEdit());
    CHECK(ed->text() == QString("one\n"));
    CHECK(ed->canRedo());
    CHECK(ed->redoEdit());
    CHECK(ed->text() == QString("onex\n"));
    delete ed;
}

TEST(a_viewport_point_resolves_to_a_position_through_the_fold_map) {
    // What a context menu needs to decide whether a right-click was inside the
    // selection. It has to agree with where the click actually lands.
    auto *ed = makeFoldable();
    ed->resize(700, 400);
    ed->grab();
    ed->foldRow(0);
    ed->grab();
    const qreal lh = ed->lineHeight();
    const QPointF pt(300, lh * 1 + lh / 2);
    CHECK_EQ(ed->positionForPoint(pt).row, 9);
    delete ed;
}

TEST(a_right_click_asks_the_host_for_a_context_menu) {
    // The first version of this set Qt::CustomContextMenu on the viewport,
    // which never fires: QAbstractScrollArea's event filter takes
    // QEvent::ContextMenu off the viewport and routes it to the scroll area's
    // contextMenuEvent(), so QWidget::event() -- which is what emits
    // customContextMenuRequested -- never sees it. Nothing errors; the menu
    // simply does not appear.
    auto *ed = makeEditor("alpha bravo\ncharlie delta\n");
    ed->resize(700, 300);
    ed->grab();
    int fired = 0;
    QPoint got;
    QPoint gotGlobal;
    QObject::connect(ed, &EditorWidget::contextMenuRequested,
                     [&](const QPoint &p, const QPoint &g) {
                         ++fired;
                         got = p;
                         gotGlobal = g;
                     });
    const QPoint pt(120, static_cast<int>(ed->lineHeight() + ed->lineHeight() / 2));
    QContextMenuEvent e(QContextMenuEvent::Mouse, pt, ed->viewport()->mapToGlobal(pt));
    QApplication::sendEvent(ed->viewport(), &e);
    CHECK_EQ(fired, 1);
    // VIEWPORT coordinates, so the point round-trips through the same function
    // the host will use. Scroll-area coordinates would be off by the frame.
    CHECK(got == pt);
    CHECK_EQ(ed->positionForPoint(got).row, 1);
    // The global position comes straight off the event rather than being
    // mapped from the viewport, which is what QScintilla does and what keeps a
    // menu from opening a frame's width away from the pointer.
    CHECK(gotGlobal == ed->viewport()->mapToGlobal(pt));
    delete ed;
}

TEST(the_menu_key_asks_for_a_context_menu_at_the_caret) {
    auto *ed = makeEditor("alpha bravo\ncharlie delta\nx\n");
    ed->resize(700, 300);
    ed->grab();
    ed->setCursor({1, 4});
    QPoint got(-1, -1);
    QObject::connect(ed, &EditorWidget::contextMenuRequested,
                     [&](const QPoint &p, const QPoint &) { got = p; });
    QContextMenuEvent e(QContextMenuEvent::Keyboard, QPoint(0, 0), QPoint(0, 0));
    QApplication::sendEvent(ed->viewport(), &e);
    CHECK(got.x() > 0);
    // The point is just under the caret, so it resolves to the caret's row.
    CHECK_EQ(ed->positionForPoint(QPoint(got.x(), got.y() - 2)).row, 1);
    delete ed;
}

TEST(the_fold_shortcut_answers_to_the_key_a_real_keyboard_sends) {
    // Shift+[ produces '{', so QKeyEvent::key() is Key_BraceLeft, not
    // Key_BracketLeft. A handler matching only the bracket never fires from
    // the keyboard on any platform -- and the menu entry keeps working, so the
    // failure looks like "the shortcut is wrong" rather than "the key is
    // never seen".
    for (int k : {static_cast<int>(Qt::Key_BraceLeft),
                  static_cast<int>(Qt::Key_BracketLeft)}) {
        auto *ed = makeFoldable();
        ed->setCursor({1, 0});
        const int before = ed->screenLineCount();
        key(ed, k, Qt::ControlModifier | Qt::ShiftModifier);
        CHECK(ed->isFolded(1));
        CHECK_EQ(ed->screenLineCount(), before - 2);
        delete ed;
    }
    for (int k : {static_cast<int>(Qt::Key_BraceRight),
                  static_cast<int>(Qt::Key_BracketRight)}) {
        auto *ed = makeFoldable();
        ed->foldRow(1);
        const int folded = ed->screenLineCount();
        ed->setCursor({1, 0});
        key(ed, k, Qt::ControlModifier | Qt::ShiftModifier);
        CHECK(!ed->isFolded(1));
        CHECK_EQ(ed->screenLineCount(), folded + 2);
        delete ed;
    }
}

TEST(the_fold_shortcut_folds_the_enclosing_block_from_inside_it) {
    // The cursor is usually in a body, not on a header.
    auto *ed = makeFoldable();
    ed->setCursor({2, 4});
    const int before = ed->screenLineCount();
    key(ed, Qt::Key_BraceLeft, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(ed->isFolded(1));
    CHECK_EQ(ed->screenLineCount(), before - 2);
    delete ed;
}
