// app/tests/test_editmenu.cpp
//
// The Edit menu and the editor's right-click menu are built from ONE set of
// QActions. Two sets would mean two labels, two shortcut declarations, and two
// chances to fix a bug in only one of them -- so the test that matters is that
// they are the same objects, not that both happen to work today.
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenuBar>
#include <QTemporaryDir>

#include "aced/grammar.h"
#include "acedqt/editorwidget.h"
#include "app/mainwindow.h"
#include "app/settings.h"
#include "harness.h"

using anyedit::MainWindow;
using anyedit::Settings;

namespace {

aced::Grammar &corpus() {
    static aced::Grammar g;
    static bool once = g.loadFile(ACED_GRAMMARS_PATH);
    (void)once;
    return g;
}

QMenu *menuNamed(MainWindow &w, const QString &title) {
    for (QAction *a : w.menuBar()->actions())
        if (a->menu() && a->text().remove('&') == title) return a->menu();
    return nullptr;
}

QAction *actionNamed(QMenu *m, const QString &label) {
    if (!m) return nullptr;
    for (QAction *a : m->actions())
        if (a->text().remove('&') == label) return a;
    return nullptr;
}

// The context menu is built fresh per right-click, so it is asked for rather
// than found: prepareContextMenu() returns the menu popup() is about to open.
QMenu *contextMenuOf(MainWindow &w) { return w.prepareContextMenu(QPoint(5, 5)); }

struct Fixture {
    QTemporaryDir dir;
    Settings s;
    MainWindow w;
    Fixture() : s(), w(&corpus(), &s) {
        qputenv("ANYEDITQT_CONFIG_DIR", dir.path().toLocal8Bit());
        s.load(nullptr);
        w.newTab();
    }
    ~Fixture() { qunsetenv("ANYEDITQT_CONFIG_DIR"); }
};

}  // namespace

TEST(the_edit_menu_has_the_clipboard_and_history_entries) {
    Fixture f;
    QMenu *edit = menuNamed(f.w, "Edit");
    CHECK(edit != nullptr);
    for (const char *label : {"Undo", "Redo", "Cut", "Copy", "Paste", "Select All"}) {
        if (actionNamed(edit, label)) continue;
        std::fprintf(stderr, "    Edit menu is missing: %s\n", label);
        CHECK(false);
    }
}

TEST(the_context_menu_shares_the_edit_menus_actions) {
    Fixture f;
    QMenu *edit = menuNamed(f.w, "Edit");
    QMenu *ctx = contextMenuOf(f.w);
    CHECK(ctx != nullptr);
    for (const char *label : {"Undo", "Redo", "Cut", "Copy", "Paste", "Select All"}) {
        QAction *a = actionNamed(edit, label);
        QAction *b = actionNamed(ctx, label);
        CHECK(a != nullptr);
        CHECK(b != nullptr);
        // Same pointer, not merely the same text.
        CHECK(a == b);
    }
}

TEST(the_entries_carry_the_platform_shortcuts) {
    Fixture f;
    QMenu *edit = menuNamed(f.w, "Edit");
    CHECK(actionNamed(edit, "Copy")->shortcut() == QKeySequence(QKeySequence::Copy));
    CHECK(actionNamed(edit, "Paste")->shortcut() == QKeySequence(QKeySequence::Paste));
    CHECK(actionNamed(edit, "Undo")->shortcut() == QKeySequence(QKeySequence::Undo));
}

TEST(cut_copy_and_undo_are_disabled_when_they_would_do_nothing) {
    Fixture f;
    QMenu *edit = menuNamed(f.w, "Edit");
    auto *ed = f.w.editorAt(0);
    ed->setText("alpha bravo\ncharlie\n");
    QApplication::clipboard()->clear();

    Q_EMIT edit->aboutToShow();
    CHECK(!actionNamed(edit, "Cut")->isEnabled());
    CHECK(!actionNamed(edit, "Copy")->isEnabled());
    CHECK(!actionNamed(edit, "Undo")->isEnabled());
    CHECK(!actionNamed(edit, "Paste")->isEnabled());

    ed->setSelection({{0, 0}, {0, 5}});
    QApplication::clipboard()->setText("x");
    Q_EMIT edit->aboutToShow();
    CHECK(actionNamed(edit, "Cut")->isEnabled());
    CHECK(actionNamed(edit, "Copy")->isEnabled());
    CHECK(actionNamed(edit, "Paste")->isEnabled());
}

TEST(the_entries_act_on_the_editor_in_the_current_tab) {
    Fixture f;
    auto *ed = f.w.editorAt(0);
    ed->setText("alpha bravo\n");
    ed->setSelection({{0, 0}, {0, 5}});
    QMenu *edit = menuNamed(f.w, "Edit");

    actionNamed(edit, "Copy")->trigger();
    CHECK(QApplication::clipboard()->text() == QString("alpha"));

    actionNamed(edit, "Cut")->trigger();
    CHECK(ed->text() == QString(" bravo\n"));

    actionNamed(edit, "Undo")->trigger();
    CHECK(ed->text() == QString("alpha bravo\n"));

    actionNamed(edit, "Select All")->trigger();
    CHECK(ed->hasSelection());
}

TEST(the_edit_actions_do_not_take_the_keys_from_the_focused_widget) {
    // The shortcuts on these actions are LABELS. With Qt's default
    // WindowShortcut context they would win over the focused widget, so Ctrl+C
    // with the caret in the find bar's line edit would copy the editor's
    // selection rather than the search text. WidgetShortcut on an action that
    // is never added to a widget can never fire; EditorWidget's own
    // keyPressEvent is what actually runs, which is also what keeps the widget
    // usable with no menu bar at all.
    Fixture f;
    QMenu *edit = menuNamed(f.w, "Edit");
    for (const char *label : {"Undo", "Redo", "Cut", "Copy", "Paste", "Select All"}) {
        QAction *a = actionNamed(edit, label);
        CHECK(a != nullptr);
        if (a->shortcutContext() == Qt::WidgetShortcut) continue;
        std::fprintf(stderr, "    %s would take the key from the focus widget\n", label);
        CHECK(false);
    }
}

TEST(the_keystrokes_still_work_without_the_menu) {
    // The other half of the same decision: with the shortcuts inert, the keys
    // have to come from the widget. A regression here is silent -- the menu
    // entries keep working and only the keyboard stops.
    Fixture f;
    auto *ed = f.w.editorAt(0);
    ed->setText("alpha bravo\n");
    ed->setSelection({{0, 0}, {0, 5}});
    QApplication::clipboard()->clear();
    QKeyEvent copyKey(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(ed, &copyKey);
    CHECK(QApplication::clipboard()->text() == QString("alpha"));
}

TEST(a_right_click_reaches_the_window_and_places_the_caret) {
    // The end-to-end assertion, minus the literal popup: exec() does not
    // return until the menu closes, so the test stops at prepareContextMenu().
    // The caret moving is proof the whole chain fired -- EditorWidget's
    // contextMenuEvent, the signal, the window's slot -- which is exactly what
    // the Qt::CustomContextMenu version silently failed to do.
    Fixture f;
    auto *ed = f.w.editorAt(0);
    ed->setText("alpha bravo\ncharlie delta\n");
    ed->resize(700, 300);
    ed->show();
    ed->grab();
    ed->setCursor({0, 0});

    int reached = 0;
    QObject::connect(ed, &acedqt::EditorWidget::contextMenuRequested,
                     [&](const QPoint &) { ++reached; });
    const QPoint pt(120, static_cast<int>(ed->lineHeight() * 1.5));
    QContextMenuEvent e(QContextMenuEvent::Mouse, pt, ed->viewport()->mapToGlobal(pt));
    QApplication::sendEvent(ed->viewport(), &e);
    CHECK_EQ(reached, 1);
    // A click outside the selection places the caret where it landed, so Cut
    // afterwards acts on something the user can see.
    CHECK_EQ(ed->cursor().row, 1);
}

TEST(a_right_click_inside_the_selection_keeps_it) {
    // Moving the caret there would throw away exactly what the user was about
    // to Cut or Copy.
    Fixture f;
    auto *ed = f.w.editorAt(0);
    ed->setText("alpha bravo\ncharlie delta\n");
    ed->resize(700, 300);
    ed->show();
    ed->grab();
    ed->setSelection({{0, 0}, {1, 7}});
    const QPoint pt(60, static_cast<int>(ed->lineHeight() * 0.5));
    QMenu *m = f.w.prepareContextMenu(pt);
    CHECK(m != nullptr);
    CHECK(ed->hasSelection());
    CHECK_EQ(ed->selection().end.row, 1);
    CHECK(actionNamed(m, "Copy")->isEnabled());
}

TEST(the_context_menu_is_populated_before_it_is_shown) {
    // prepareContextMenu() hands back the menu that exec() is about to open,
    // so what it returns is what the user sees.
    Fixture f;
    auto *ed = f.w.editorAt(0);
    ed->setText("alpha bravo\n");
    ed->resize(700, 300);
    ed->show();
    ed->grab();
    ed->setSelection({{0, 0}, {0, 5}});
    QMenu *m = f.w.prepareContextMenu(QPoint(20, 5));
    CHECK(m != nullptr);
    CHECK(m->actions().size() >= 6);
    CHECK(actionNamed(m, "Cut")->isEnabled());
    CHECK(actionNamed(m, "Copy")->isEnabled());
}
