// app/tests/test_language.cpp
//
// The language picker, and the tab bookkeeping it sits on top of.
#include <QApplication>
#include <QMenu>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>

#include "aced/grammar.h"
#include "acedqt/editorwidget.h"
#include "app/languages.h"
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

QString writeFileAt(const QString &dir, const QString &name) {
    const QString p = dir + "/" + name;
    QFile f(p);
    f.open(QIODevice::WriteOnly);
    f.write("x = 1\n");
    f.close();
    return p;
}

}  // namespace

TEST(display_names_are_fixed_where_the_generic_rule_is_wrong) {
    CHECK_EQ(anyedit::languageDisplayName("c_cpp").toStdString(), std::string("C/C++"));
    CHECK_EQ(anyedit::languageDisplayName("csharp").toStdString(), std::string("C#"));
    CHECK_EQ(anyedit::languageDisplayName("javascript").toStdString(),
             std::string("JavaScript"));
    CHECK_EQ(anyedit::languageDisplayName("").toStdString(), std::string("Plain Text"));
}

TEST(an_unknown_mode_is_prettified_rather_than_rejected) {
    // A corpus with a language this build has never heard of still has to
    // produce a usable menu entry.
    CHECK_EQ(anyedit::languageDisplayName("apache_conf").toStdString(),
             std::string("Apache Config"));
    CHECK_EQ(anyedit::languageDisplayName("some_new_lang").toStdString(),
             std::string("Some New Lang"));
    CHECK_EQ(anyedit::languageDisplayName("haskell").toStdString(),
             std::string("Haskell"));
}

TEST(the_language_list_covers_the_corpus_and_is_sorted) {
    const auto list = anyedit::languageList(corpus());
    // Everything except ace's do-nothing "text" grammar, which is offered once
    // at the top of the menu as Plain Text rather than twice.
    CHECK_EQ(list.size(), corpus().modes().size() - 1);
    for (const auto &l : list) CHECK(l.second != "text");
    CHECK(list.size() > 150);
    for (size_t i = 1; i < list.size(); ++i) {
        CHECK(QString::compare(list[i - 1].first, list[i].first, Qt::CaseInsensitive)
              <= 0);
    }
}

TEST(setting_a_language_by_hand_changes_the_mode_and_the_status_bar) {
    QTemporaryDir dir;
    qputenv("ANYEDITQT_CONFIG_DIR", dir.path().toLocal8Bit());
    Settings s;
    s.load(nullptr);
    MainWindow w(&corpus(), &s);
    w.newTab();
    auto *ed = w.editorAt(0);
    CHECK_EQ(ed->mode().toStdString(), std::string(""));

    w.setLanguage(ed, "python", true);
    CHECK_EQ(ed->mode().toStdString(), std::string("python"));
    CHECK(w.languageIsManualAt(0));

    QToolButton *button = nullptr;
    for (QToolButton *b : w.findChildren<QToolButton *>())
        if (b->menu()) button = b;
    CHECK(button != nullptr);
    CHECK_EQ(button->text().toStdString(), std::string("Python"));
    qunsetenv("ANYEDITQT_CONFIG_DIR");
}

TEST(the_language_menu_is_grouped_rather_than_one_flat_list) {
    QTemporaryDir dir;
    qputenv("ANYEDITQT_CONFIG_DIR", dir.path().toLocal8Bit());
    Settings s;
    s.load(nullptr);
    MainWindow w(&corpus(), &s);

    QMenu *menu = nullptr;
    for (QToolButton *b : w.findChildren<QToolButton *>())
        if (b->menu()) menu = b->menu();
    CHECK(menu != nullptr);
    // Plain Text, a separator, then one submenu per letter -- not 198 items.
    const QList<QAction *> acts = menu->actions();
    CHECK_EQ(acts.at(0)->text().toStdString(), std::string("Plain Text"));
    CHECK(acts.at(1)->isSeparator());
    CHECK(acts.size() < 40);
    int submenus = 0;
    for (QAction *a : acts)
        if (a->menu()) ++submenus;
    CHECK(submenus > 15);
    qunsetenv("ANYEDITQT_CONFIG_DIR");
}

TEST(opening_a_file_still_guesses_the_language) {
    QTemporaryDir dir;
    qputenv("ANYEDITQT_CONFIG_DIR", dir.path().toLocal8Bit());
    Settings s;
    s.load(nullptr);
    MainWindow w(&corpus(), &s);
    w.openPath(writeFileAt(dir.path(), "thing.py"));
    CHECK_EQ(w.editorAt(0)->mode().toStdString(), std::string("python"));
    // Guessed, not chosen: a Save As is still allowed to re-guess.
    CHECK(!w.languageIsManualAt(0));
    qunsetenv("ANYEDITQT_CONFIG_DIR");
}

TEST(dragging_a_tab_does_not_move_the_files_around_under_it) {
    // REGRESSION, AND IT LOST DATA. The tab bar is movable and the path list
    // was indexed by tab POSITION, so dragging a tab left the paths where they
    // were: tab 0 then reported the file that used to be there, and Ctrl+S on
    // it wrote over a document the user was not even looking at.
    QTemporaryDir dir;
    qputenv("ANYEDITQT_CONFIG_DIR", dir.path().toLocal8Bit());
    Settings s;
    s.load(nullptr);
    MainWindow w(&corpus(), &s);
    const QString a = writeFileAt(dir.path(), "a.py");
    const QString b = writeFileAt(dir.path(), "b.py");
    w.openPath(a);
    w.openPath(b);
    CHECK_EQ(w.pathAt(0).toStdString(), a.toStdString());
    CHECK_EQ(w.pathAt(1).toStdString(), b.toStdString());

    auto *bar = w.findChild<QTabBar *>();
    CHECK(bar != nullptr);
    bar->moveTab(0, 1);
    CHECK_EQ(w.pathAt(0).toStdString(), b.toStdString());
    CHECK_EQ(w.pathAt(1).toStdString(), a.toStdString());
    qunsetenv("ANYEDITQT_CONFIG_DIR");
}

TEST(a_language_set_by_hand_is_per_tab) {
    QTemporaryDir dir;
    qputenv("ANYEDITQT_CONFIG_DIR", dir.path().toLocal8Bit());
    Settings s;
    s.load(nullptr);
    MainWindow w(&corpus(), &s);
    w.newTab();
    w.newTab();
    w.setLanguage(w.editorAt(0), "python", true);
    w.setLanguage(w.editorAt(1), "yaml", true);
    CHECK_EQ(w.editorAt(0)->mode().toStdString(), std::string("python"));
    CHECK_EQ(w.editorAt(1)->mode().toStdString(), std::string("yaml"));
    // ...and it follows the tab, not the position.
    w.findChild<QTabBar *>()->moveTab(0, 1);
    CHECK_EQ(w.editorAt(1)->mode().toStdString(), std::string("python"));
    CHECK(w.languageIsManualAt(1));
    qunsetenv("ANYEDITQT_CONFIG_DIR");
}

TEST(a_hand_picked_language_survives_a_save_as) {
    // The case this whole feature exists for: a new buffer, set to a language
    // by hand, then saved. Re-guessing from the extension would throw the
    // choice away at the one moment it matters.
    QTemporaryDir dir;
    qputenv("ANYEDITQT_CONFIG_DIR", dir.path().toLocal8Bit());
    Settings s;
    s.load(nullptr);
    MainWindow w(&corpus(), &s);
    w.newTab();
    auto *ed = w.editorAt(0);
    w.setLanguage(ed, "yaml", true);
    // saveAs() opens a file dialog, so the branch it guards is exercised at the
    // level below: a manual language is not re-derived, a guessed one is.
    CHECK(w.languageIsManualAt(0));
    w.setLanguage(ed, "python", false);
    CHECK(!w.languageIsManualAt(0));
    qunsetenv("ANYEDITQT_CONFIG_DIR");
}
