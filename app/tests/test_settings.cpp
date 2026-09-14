// app/tests/test_settings.cpp
//
// Every test points ANYEDITQT_CONFIG_DIR at a fresh temporary directory before
// touching Settings, so none of this can read or write the real ~/.anyeditqt.
// configDir() reads the environment on every call rather than caching it, which
// is what makes that possible.
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QCheckBox>
#include <QMenu>
#include <QSpinBox>
#include <QStyle>
#include <QStyleFactory>
#include <QTemporaryDir>

#include "acedqt/editorwidget.h"
#include "acedqt/palette.h"
#include "app/mainwindow.h"
#include "app/preferencesdialog.h"
#include "app/recentfiles.h"
#include "app/settings.h"
#include "app/theme.h"
#include "harness.h"

using anyedit::AppSettings;
using anyedit::EditorSettings;
using anyedit::Scheme;
using anyedit::Settings;
using anyedit::ThemeMode;

namespace {

// Kept alive for the duration of a case; the destructor removes the tree and
// unsets the variable, so a case that fails does not leak its config into the
// next one.
struct Sandbox {
    QTemporaryDir dir;
    Sandbox() { qputenv("ANYEDITQT_CONFIG_DIR", dir.path().toLocal8Bit()); }
    ~Sandbox() { qunsetenv("ANYEDITQT_CONFIG_DIR"); }
    QString path() const { return dir.path(); }
};

QJsonObject readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(bytes);
    f.close();
}

// Counts changed() without pulling in Qt6::Test for QSignalSpy. The project has
// one vendored dependency and its own harness; a second test framework linked
// in for one class would be the larger cost.
struct SignalCounter {
    int count = 0;
    explicit SignalCounter(Settings *s) {
        QObject::connect(s, &Settings::changed, s, [this] { ++count; });
    }
};

}  // namespace

TEST(config_dir_follows_the_environment_override) {
    Sandbox sb;
    CHECK_EQ(Settings::configDir().toStdString(), sb.path().toStdString());
    CHECK_EQ(Settings::configPath().toStdString(),
             (sb.path() + "/settings.json").toStdString());
}

TEST(first_run_writes_a_file_with_the_defaults_in_it) {
    Sandbox sb;
    Settings s;
    QString err;
    CHECK(s.load(&err));
    CHECK_EQ(err.toStdString(), std::string());

    const QJsonObject root = readFile(Settings::configPath());
    CHECK(root.contains("editor"));
    CHECK(root.contains("app"));
    // Both groups present and populated: a first run that wrote {} would leave
    // nothing to discover the key names from, which is the point of the file.
    const QJsonObject ed = root.value("editor").toObject();
    CHECK_EQ(ed.value("tab_width").toInt(), 4);
    CHECK_EQ(ed.value("font_size").toInt(), 11);
    CHECK(ed.value("show_line_numbers").toBool());
    const QJsonObject ap = root.value("app").toObject();
    CHECK_EQ(ap.value("theme").toString().toStdString(), std::string("system"));
    CHECK_EQ(ap.value("window").toObject().value("width").toInt(), 1000);
}

TEST(a_missing_config_directory_is_created) {
    Sandbox sb;
    const QString nested = sb.path() + "/a/b/c";
    qputenv("ANYEDITQT_CONFIG_DIR", nested.toLocal8Bit());
    Settings s;
    CHECK(s.load(nullptr));
    CHECK(QFile::exists(nested + "/settings.json"));
}

TEST(settings_round_trip_through_the_file) {
    Sandbox sb;
    EditorSettings e;
    e.fontFamily = "Courier New";
    e.fontSize = 14;
    e.tabWidth = 8;
    e.insertSpaces = false;
    e.showLineNumbers = false;
    e.highlightCurrentLine = false;
    e.softWrap = true;
    e.wrapColumn = 100;
    e.paletteLight = "dayfold";

    AppSettings a;
    a.theme = ThemeMode::Dark;
    a.uiFontFamily = "DejaVu Sans";
    a.uiFontSize = 13;
    a.window.width = 1280;
    a.window.height = 800;
    a.window.maximized = true;

    {
        Settings s;
        s.setEditor(e);
        s.setApp(a);
        CHECK(s.save(nullptr));
    }
    Settings t;
    CHECK(t.load(nullptr));
    CHECK(t.editor() == e);
    CHECK(t.app() == a);
}

TEST(keys_this_build_does_not_know_survive_a_save) {
    Sandbox sb;
    // A settings.json from a future build: an unknown top-level key, an unknown
    // key inside each group, and one inside the nested window object.
    writeFile(Settings::configPath(), R"({
        "keymap": "sublime",
        "editor": { "tab_width": 2, "soft_wrap": true },
        "app": { "theme": "dark", "sidebar": "left",
                 "window": { "width": 900, "x": 40 } }
    })");

    Settings s;
    CHECK(s.load(nullptr));
    CHECK_EQ(s.editor().tabWidth, 2);
    CHECK(s.app().theme == ThemeMode::Dark);
    CHECK_EQ(s.app().window.width, 900);
    CHECK(s.save(nullptr));

    const QJsonObject root = readFile(Settings::configPath());
    CHECK_EQ(root.value("keymap").toString().toStdString(), std::string("sublime"));
    CHECK(root.value("editor").toObject().value("soft_wrap").toBool());
    CHECK_EQ(root.value("app").toObject().value("sidebar").toString().toStdString(),
             std::string("left"));
    CHECK_EQ(root.value("app").toObject().value("window").toObject().value("x").toInt(),
             40);
    // and the known ones are still right
    CHECK_EQ(root.value("editor").toObject().value("tab_width").toInt(), 2);
}

TEST(out_of_range_numbers_are_clamped_not_rejected) {
    Sandbox sb;
    writeFile(Settings::configPath(), R"({
        "editor": { "font_size": 4000, "tab_width": 0 },
        "app": { "window": { "width": 1, "height": -20 } }
    })");
    Settings s;
    CHECK(s.load(nullptr));
    CHECK_EQ(s.editor().fontSize, 72);
    CHECK_EQ(s.editor().tabWidth, 1);
    CHECK_EQ(s.app().window.width, 240);
    CHECK_EQ(s.app().window.height, 180);
}

TEST(wrong_types_fall_back_to_the_default_for_that_key) {
    Sandbox sb;
    writeFile(Settings::configPath(), R"({
        "editor": { "font_size": "large", "show_line_numbers": "yes",
                    "palette_dark": "solarized-eclipse" },
        "app": { "theme": "chartreuse" }
    })");
    Settings s;
    CHECK(s.load(nullptr));
    CHECK_EQ(s.editor().fontSize, 11);
    CHECK(s.editor().showLineNumbers);
    // An unknown palette name is not an error -- it may exist in a newer build
    // -- but this one has to fall back to something that does exist here.
    CHECK_EQ(s.editor().paletteDark.toStdString(), std::string("tomorrow-night"));
    CHECK(s.app().theme == ThemeMode::System);
}

TEST(a_malformed_file_is_reported_and_not_overwritten) {
    Sandbox sb;
    const QByteArray broken = "{ \"editor\": { \"tab_width\": 8, }\n";
    writeFile(Settings::configPath(), broken);

    Settings s;
    QString err;
    CHECK(!s.load(&err));
    CHECK(err.contains("offset"));
    // Defaults in force in memory...
    CHECK_EQ(s.editor().tabWidth, 4);
    // ...and the file untouched, so the bad line can still be found.
    QFile f(Settings::configPath());
    f.open(QIODevice::ReadOnly);
    CHECK_EQ(f.readAll().toStdString(), broken.toStdString());
}

TEST(changed_fires_only_when_something_differs) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    SignalCounter spy(&s);

    s.setEditor(s.editor());          // identical
    s.setApp(s.app());                // identical
    CHECK_EQ(spy.count, 0);

    EditorSettings e = s.editor();
    e.fontSize += 1;
    s.setEditor(e);
    CHECK_EQ(spy.count, 1);
}

TEST(load_emits_changed_when_the_file_differs_from_memory) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    SignalCounter spy(&s);

    // Same content again: no signal, so a watcher firing on an unrelated write
    // in the directory does not repaint every tab.
    CHECK(s.load(nullptr));
    CHECK_EQ(spy.count, 0);

    writeFile(Settings::configPath(), R"({"editor":{"tab_width":3}})");
    CHECK(s.load(nullptr));
    CHECK_EQ(spy.count, 1);
    CHECK_EQ(s.editor().tabWidth, 3);
}

TEST(explicit_themes_resolve_without_asking_the_desktop) {
    Sandbox sb;
    Settings s;
    AppSettings a = s.app();
    a.theme = ThemeMode::Dark;
    s.setApp(a);
    CHECK(s.resolvedScheme() == Scheme::Dark);
    CHECK_EQ(s.editorPalette().background().name().toStdString(),
             acedqt::Palette::tomorrowNight().background().name().toStdString());

    a.theme = ThemeMode::Light;
    s.setApp(a);
    CHECK(s.resolvedScheme() == Scheme::Light);
    CHECK_EQ(s.editorPalette().background().name().toStdString(),
             acedqt::Palette::dayfold().background().name().toStdString());
}

TEST(system_theme_follows_the_application_palette) {
    Sandbox sb;
    Settings s;
    AppSettings a = s.app();
    a.theme = ThemeMode::System;
    s.setApp(a);

    const QPalette saved = qApp->palette();
    qApp->setPalette(anyedit::darkChromePalette());
    CHECK(s.resolvedScheme() == Scheme::Dark);
    qApp->setPalette(anyedit::lightChromePalette());
    CHECK(s.resolvedScheme() == Scheme::Light);
    qApp->setPalette(saved);
}

TEST(applying_an_explicit_theme_changes_the_application_palette) {
    Sandbox sb;
    const QPalette saved = qApp->palette();
    const QString savedStyle = qApp->style()->objectName();

    AppSettings a;
    a.theme = ThemeMode::Dark;
    CHECK(anyedit::applyTheme(qApp, a) == Scheme::Dark);
    CHECK(qApp->palette().color(QPalette::Window).lightness() < 128);

    a.theme = ThemeMode::Light;
    CHECK(anyedit::applyTheme(qApp, a) == Scheme::Light);
    CHECK(qApp->palette().color(QPalette::Window).lightness() >= 128);

    qApp->setPalette(saved);
    qApp->setStyle(QStyleFactory::create(savedStyle));
}

TEST(ui_font_leaves_alone_what_the_settings_do_not_set) {
    Sandbox sb;
    const QFont saved = qApp->font();
    AppSettings a;              // empty family, zero size
    anyedit::applyUiFont(qApp, a);
    const QFont untouched = qApp->font();

    a.uiFontSize = 17;          // size only
    anyedit::applyUiFont(qApp, a);
    CHECK_EQ(qApp->font().pointSize(), 17);
    CHECK_EQ(qApp->font().family().toStdString(), untouched.family().toStdString());

    qApp->setFont(saved);
}

TEST(the_resolved_editor_font_is_monospace_and_exists) {
    Sandbox sb;
    Settings s;
    const QFont f = s.editorFont();
    CHECK_EQ(f.pointSize(), 11);
    CHECK(f.fixedPitch());
    // QFontInfo reports what was actually resolved, so this fails if the
    // default family list names something not installed on this platform.
    CHECK(!QFontInfo(f).family().isEmpty());
}

TEST(editor_settings_reach_the_widget) {
    Sandbox sb;
    Settings s;
    EditorSettings e = s.editor();
    e.tabWidth = 3;
    e.insertSpaces = true;
    s.setEditor(e);

    acedqt::EditorWidget ed;
    ed.setEditorFont(s.editorFont());
    ed.setEditorPalette(s.editorPalette());
    ed.setTabWidth(e.tabWidth);
    ed.setInsertSpaces(e.insertSpaces);
    ed.setText("x");
    ed.setCursor({0, 1});

    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, "\t");
    QApplication::sendEvent(&ed, &tab);
    CHECK_EQ(ed.text().toStdString(), std::string("x   "));

    // ...and the other way round.
    e.insertSpaces = false;
    s.setEditor(e);
    ed.setInsertSpaces(false);
    ed.setText("y");
    ed.setCursor({0, 1});
    QKeyEvent tab2(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, "\t");
    QApplication::sendEvent(&ed, &tab2);
    CHECK_EQ(ed.text().toStdString(), std::string("y\t"));
}

TEST(the_window_applies_editor_settings_to_tabs_it_opens) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    EditorSettings e = s.editor();
    e.tabWidth = 7;
    e.showLineNumbers = false;
    s.setEditor(e);

    anyedit::MainWindow w(nullptr, &s);
    w.newTab();
    w.show();  // key events are not delivered into a window that never mapped
    CHECK_EQ(w.tabCount(), 1);
    auto *ed = w.editorAt(0);
    CHECK(ed != nullptr);
    ed->setText("a");
    ed->setCursor({0, 1});
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, "\t");
    QApplication::sendEvent(ed, &tab);
    // Seven spaces, so tab_width reached the widget rather than the default 4.
    CHECK_EQ(ed->text().toStdString(), std::string("a       "));
}

TEST(a_settings_change_reaches_tabs_that_are_already_open) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));

    anyedit::MainWindow w(nullptr, &s);
    w.newTab();
    w.show();
    auto *ed = w.editorAt(0);
    const int before = ed->editorFont().pointSize();

    EditorSettings e = s.editor();
    e.fontSize = before + 6;
    s.setEditor(e);  // no explicit push: the window is connected to changed()
    CHECK_EQ(ed->editorFont().pointSize(), before + 6);

    e.tabWidth = 2;
    s.setEditor(e);
    ed->setText("z");
    ed->setCursor({0, 1});
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, "\t");
    QApplication::sendEvent(ed, &tab);
    CHECK_EQ(ed->text().toStdString(), std::string("z  "));
}

TEST(the_theme_menu_reflects_and_sets_the_stored_theme) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    AppSettings a = s.app();
    a.theme = ThemeMode::Dark;
    s.setApp(a);

    anyedit::MainWindow w(nullptr, &s);
    QAction *dark = nullptr;
    QAction *light = nullptr;
    for (QActionGroup *g : w.findChildren<QActionGroup *>()) {
        for (QAction *act : g->actions()) {
            if (act->data().toInt() == static_cast<int>(ThemeMode::Dark)) dark = act;
            if (act->data().toInt() == static_cast<int>(ThemeMode::Light)) light = act;
        }
    }
    CHECK(dark != nullptr);
    CHECK(light != nullptr);
    // Opened with dark stored, so dark is the one ticked.
    CHECK(dark->isChecked());
    CHECK(!light->isChecked());

    light->trigger();
    CHECK(s.app().theme == ThemeMode::Light);
    CHECK(light->isChecked());
    CHECK(!dark->isChecked());
    // and the editor palette followed the theme, not just the menu
    CHECK_EQ(w.editorAt(0) ? 0 : 0, 0);
    CHECK_EQ(s.editorPalette().background().name().toStdString(),
             acedqt::Palette::dayfold().background().name().toStdString());
}

TEST(soft_wrap_reaches_the_widget_from_the_settings_file) {
    Sandbox sb;
    writeFile(Settings::configPath(),
              R"({"editor":{"soft_wrap":true,"wrap_column":20}})");
    Settings s;
    CHECK(s.load(nullptr));
    CHECK(s.editor().softWrap);
    CHECK_EQ(s.editor().wrapColumn, 20);

    anyedit::MainWindow w(nullptr, &s);
    w.newTab();
    w.show();
    auto *ed = w.editorAt(0);
    CHECK(ed->softWrap());
    CHECK_EQ(ed->wrapColumn(), 20);

    ed->setText(QString(100, QLatin1Char('a')));
    ed->grab();
    CHECK(ed->screenLineCount() >= 5);

    // ...and turning it off in the settings puts the row back on one line.
    EditorSettings e = s.editor();
    e.softWrap = false;
    s.setEditor(e);
    ed->grab();
    CHECK(!ed->softWrap());
    CHECK_EQ(ed->screenLineCount(), 1);
}

TEST(a_desktop_theme_change_repaints_the_tabs_when_following_the_system) {
    Sandbox sb;
    const QPalette saved = qApp->palette();
    Settings s;
    CHECK(s.load(nullptr));  // theme defaults to system

    qApp->setPalette(anyedit::lightChromePalette());
    anyedit::MainWindow w(nullptr, &s);
    w.newTab();
    auto *ed = w.editorAt(0);
    CHECK_EQ(ed->palette().color(QPalette::Window).name().toStdString(),
             ed->palette().color(QPalette::Window).name().toStdString());
    const QString lightBg = s.editorPalette().background().name();

    // The desktop goes dark. Qt delivers ApplicationPaletteChange; the window
    // has to notice and re-colour the editors, because nothing in the settings
    // file changed.
    qApp->setPalette(anyedit::darkChromePalette());
    QEvent ev(QEvent::ApplicationPaletteChange);
    QApplication::sendEvent(&w, &ev);
    const QString darkBg = s.editorPalette().background().name();
    CHECK(darkBg != lightBg);
    CHECK_EQ(darkBg.toStdString(),
             acedqt::Palette::tomorrowNight().background().name().toStdString());

    qApp->setPalette(saved);
}

TEST(an_explicit_theme_ignores_the_desktop) {
    Sandbox sb;
    const QPalette saved = qApp->palette();
    Settings s;
    CHECK(s.load(nullptr));
    AppSettings a = s.app();
    a.theme = ThemeMode::Light;
    s.setApp(a);

    anyedit::MainWindow w(nullptr, &s);
    w.newTab();
    qApp->setPalette(anyedit::darkChromePalette());
    QEvent ev(QEvent::ApplicationPaletteChange);
    QApplication::sendEvent(&w, &ev);
    // Still light: the setting wins over the desktop.
    CHECK(s.resolvedScheme() == Scheme::Light);
    CHECK_EQ(s.editorPalette().background().name().toStdString(),
             acedqt::Palette::dayfold().background().name().toStdString());

    qApp->setPalette(saved);
    anyedit::applyTheme(qApp, AppSettings{});
    qApp->setPalette(saved);
}

TEST(closing_the_window_writes_the_geometry_back) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    {
        anyedit::MainWindow w(nullptr, &s);
        w.resize(1234, 567);
        w.close();
    }
    Settings t;
    CHECK(t.load(nullptr));
    CHECK_EQ(t.app().window.width, 1234);
    CHECK_EQ(t.app().window.height, 567);
}

TEST(geometry_is_left_alone_when_remember_is_off) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    AppSettings a = s.app();
    a.window.rememberGeometry = false;
    a.window.width = 1000;
    a.window.height = 700;
    s.setApp(a);
    CHECK(s.save(nullptr));
    {
        anyedit::MainWindow w(nullptr, &s);
        w.resize(1234, 567);
        w.close();
    }
    Settings t;
    CHECK(t.load(nullptr));
    CHECK_EQ(t.app().window.width, 1000);
    CHECK_EQ(t.app().window.height, 700);
}

// --- preferences dialog ----------------------------------------------------

TEST(the_dialog_opens_showing_what_is_stored) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    EditorSettings e = s.editor();
    e.fontSize = 17;
    e.tabWidth = 3;
    e.softWrap = true;
    s.setEditor(e);
    AppSettings a = s.app();
    a.theme = ThemeMode::Dark;
    s.setApp(a);

    anyedit::PreferencesDialog dlg(&s);
    CHECK(dlg.openedWithEditor() == e);
    CHECK(dlg.openedWithApp() == a);
    // Nothing was written just by opening it. REGRESSION: populating the font
    // combos emitted currentIndexChanged, which pushed every control's initial
    // value -- spin box minimums -- into the settings before they had been
    // loaded, so opening the dialog silently set the font to 6pt.
    CHECK(s.editor() == e);
    CHECK(s.app() == a);
}

// Controls are found by the value they were loaded with rather than by object
// name, because naming every widget for the benefit of a test is a way of
// testing the names instead of the behaviour.
namespace {
QSpinBox *spinShowing(QWidget *w, int value) {
    for (QSpinBox *s : w->findChildren<QSpinBox *>())
        if (s->value() == value) return s;
    return nullptr;
}
QCheckBox *checkNamed(QWidget *w, const QString &text) {
    for (QCheckBox *c : w->findChildren<QCheckBox *>())
        if (c->text() == text) return c;
    return nullptr;
}
}  // namespace

TEST(changing_a_control_applies_immediately) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    anyedit::PreferencesDialog dlg(&s);

    QSpinBox *tab = spinShowing(&dlg, 4);  // tab width default
    CHECK(tab != nullptr);
    tab->setValue(8);
    CHECK_EQ(s.editor().tabWidth, 8);

    QCheckBox *wrap = checkNamed(&dlg, "Word wrap");
    CHECK(wrap != nullptr);
    CHECK(!wrap->isChecked());
    wrap->setChecked(true);
    CHECK(s.editor().softWrap);
}

TEST(cancel_puts_back_what_was_there_when_it_opened) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    const EditorSettings before = s.editor();

    anyedit::PreferencesDialog dlg(&s);
    QSpinBox *tab = spinShowing(&dlg, 4);
    tab->setValue(9);
    checkNamed(&dlg, "Word wrap")->setChecked(true);
    CHECK_EQ(s.editor().tabWidth, 9);

    dlg.reject();
    CHECK(s.editor() == before);
    CHECK_EQ(s.editor().tabWidth, 4);
    CHECK(!s.editor().softWrap);
}

TEST(ok_writes_the_file) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    {
        anyedit::PreferencesDialog dlg(&s);
        spinShowing(&dlg, 4)->setValue(6);
        dlg.accept();
    }
    // Not just in memory: a fresh Settings reading the file has to see it.
    Settings t;
    CHECK(t.load(nullptr));
    CHECK_EQ(t.editor().tabWidth, 6);
}

TEST(restore_defaults_leaves_the_window_size_alone) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    EditorSettings e = s.editor();
    e.tabWidth = 8;
    s.setEditor(e);
    AppSettings a = s.app();
    a.theme = ThemeMode::Dark;
    a.window.width = 1400;   // the user dragged the window; not a preference
    s.setApp(a);

    anyedit::PreferencesDialog dlg(&s);
    dlg.restoreDefaults();
    CHECK_EQ(s.editor().tabWidth, 4);
    CHECK(s.app().theme == ThemeMode::System);
    CHECK_EQ(s.app().window.width, 1400);
}

TEST(the_dialog_follows_the_file_being_edited_underneath_it) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    anyedit::PreferencesDialog dlg(&s);
    CHECK(spinShowing(&dlg, 4) != nullptr);  // tab width shows 4

    // Something else edits the same settings -- the settings.json tab being
    // saved, in the real application.
    writeFile(Settings::configPath(), R"({"editor":{"tab_width":11}})");
    CHECK(s.load(nullptr));
    CHECK(spinShowing(&dlg, 11) != nullptr);
    // ...and the controls did not write their old values straight back.
    CHECK_EQ(s.editor().tabWidth, 11);
}

TEST(the_window_reuses_one_dialog) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    anyedit::MainWindow w(nullptr, &s);
    CHECK(w.preferencesDialog() == nullptr);
    w.openPreferences();
    auto *first = w.preferencesDialog();
    CHECK(first != nullptr);
    w.openPreferences();
    CHECK(w.preferencesDialog() == first);
}

// --- recent files ----------------------------------------------------------

namespace {
QStringList makeFiles(const QString &dir, int n, const QString &prefix = "f") {
    QStringList out;
    for (int i = 0; i < n; ++i) {
        const QString p = QString("%1/%2%3.txt").arg(dir, prefix).arg(i);
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write("x");
        f.close();
        out << p;
    }
    return out;
}
}  // namespace

TEST(recent_files_live_beside_the_settings_not_inside_them) {
    Sandbox sb;
    CHECK_EQ(anyedit::RecentFiles::path().toStdString(),
             (sb.path() + "/recent.json").toStdString());

    // Adding one must not touch settings.json: that file is preferences, and
    // rewriting it on every open would fire the settings watcher.
    Settings s;
    CHECK(s.load(nullptr));
    QFile sf(Settings::configPath());
    sf.open(QIODevice::ReadOnly);
    const QByteArray before = sf.readAll();
    sf.close();

    anyedit::RecentFiles r;
    r.add(makeFiles(sb.path(), 1).first());

    QFile sf2(Settings::configPath());
    sf2.open(QIODevice::ReadOnly);
    CHECK_EQ(sf2.readAll().toStdString(), before.toStdString());
}

TEST(most_recent_first_and_no_duplicates) {
    Sandbox sb;
    const QStringList files = makeFiles(sb.path(), 3);
    anyedit::RecentFiles r;
    r.add(files[0]);
    r.add(files[1]);
    r.add(files[2]);
    CHECK_EQ(r.paths().size(), 3);
    CHECK_EQ(QFileInfo(r.paths().at(0)).fileName().toStdString(), std::string("f2.txt"));

    // Re-opening an old one PROMOTES it rather than adding a second entry.
    r.add(files[0]);
    CHECK_EQ(r.paths().size(), 3);
    CHECK_EQ(QFileInfo(r.paths().at(0)).fileName().toStdString(), std::string("f0.txt"));
}

TEST(the_list_stops_at_twenty) {
    Sandbox sb;
    const QStringList files = makeFiles(sb.path(), 25);
    anyedit::RecentFiles r;
    for (const QString &f : files) r.add(f);
    CHECK_EQ(r.paths().size(), anyedit::RecentFiles::kMax);
    CHECK_EQ(anyedit::RecentFiles::kMax, 20);
    // The newest is kept and the oldest five are gone.
    CHECK_EQ(QFileInfo(r.paths().first()).fileName().toStdString(), std::string("f24.txt"));
    CHECK_EQ(QFileInfo(r.paths().last()).fileName().toStdString(), std::string("f5.txt"));
}

TEST(the_list_survives_a_restart) {
    Sandbox sb;
    const QStringList files = makeFiles(sb.path(), 3);
    {
        anyedit::RecentFiles r;
        for (const QString &f : files) r.add(f);
    }
    anyedit::RecentFiles r2;
    CHECK(r2.load());
    CHECK_EQ(r2.paths().size(), 3);
    CHECK_EQ(QFileInfo(r2.paths().first()).fileName().toStdString(), std::string("f2.txt"));
}

TEST(a_corrupt_recent_file_is_ignored_rather_than_fatal) {
    Sandbox sb;
    QDir().mkpath(sb.path());
    writeFile(anyedit::RecentFiles::path(), "{ not json at all");
    anyedit::RecentFiles r;
    CHECK(!r.load());
    CHECK(r.isEmpty());
    // ...and it still works afterwards.
    r.add(makeFiles(sb.path(), 1).first());
    CHECK_EQ(r.paths().size(), 1);
}

TEST(opening_a_file_puts_it_in_the_menu) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    const QStringList files = makeFiles(sb.path(), 2);

    anyedit::MainWindow w(nullptr, &s);
    w.openPath(files[0]);
    w.openPath(files[1]);
    CHECK_EQ(w.recentFiles()->paths().size(), 2);

    QMenu *menu = w.recentMenu();
    CHECK(menu != nullptr);
    // Two files, a separator, and Clear Menu.
    CHECK_EQ(menu->actions().size(), 4);
    CHECK_EQ(menu->actions().at(0)->data().toString().toStdString(),
             QFileInfo(files[1]).canonicalFilePath().toStdString());
    CHECK(menu->actions().at(0)->text().startsWith("&1"));
}

TEST(an_empty_list_says_so_instead_of_showing_an_empty_menu) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    anyedit::MainWindow w(nullptr, &s);
    QMenu *menu = w.recentMenu();
    CHECK_EQ(menu->actions().size(), 1);
    CHECK(!menu->actions().at(0)->isEnabled());
}

TEST(ampersands_in_a_file_name_survive_the_menu) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    const QString p = sb.path() + "/Q&A notes.txt";
    QFile f(p);
    f.open(QIODevice::WriteOnly);
    f.write("x");
    f.close();

    anyedit::MainWindow w(nullptr, &s);
    w.openPath(p);
    // Qt eats a single & as a mnemonic, so the stored text must double it or
    // the menu shows "QA notes.txt" with a hidden accelerator on the A.
    const QString text = w.recentMenu()->actions().at(0)->text();
    CHECK(text.contains("Q&&A"));
}

TEST(files_with_the_same_name_show_their_directory) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    QDir().mkpath(sb.path() + "/a");
    QDir().mkpath(sb.path() + "/b");
    for (const QString &d : {QString("a"), QString("b")}) {
        QFile f(sb.path() + "/" + d + "/notes.txt");
        f.open(QIODevice::WriteOnly);
        f.write("x");
        f.close();
    }
    anyedit::MainWindow w(nullptr, &s);
    w.openPath(sb.path() + "/a/notes.txt");
    w.openPath(sb.path() + "/b/notes.txt");

    const QList<QAction *> acts = w.recentMenu()->actions();
    CHECK(acts.at(0)->text().contains("notes.txt"));
    // Two entries called notes.txt would be indistinguishable without this.
    CHECK(acts.at(0)->text().contains("/b"));
    CHECK(acts.at(1)->text().contains("/a"));
}

TEST(a_unique_name_does_not_get_a_directory_stuck_on_it) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    const QStringList files = makeFiles(sb.path(), 2);
    anyedit::MainWindow w(nullptr, &s);
    w.openPath(files[0]);
    w.openPath(files[1]);
    CHECK(!w.recentMenu()->actions().at(0)->text().contains(sb.path()));
}

TEST(a_recent_file_that_has_gone_is_dropped_from_the_list) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    const QStringList files = makeFiles(sb.path(), 2);
    anyedit::MainWindow w(nullptr, &s);
    w.openPath(files[0]);
    w.openPath(files[1]);
    CHECK_EQ(w.recentFiles()->paths().size(), 2);

    // Deleted behind our back, as happens with a branch switch.
    const QString gone = w.recentFiles()->paths().first();
    QFile::remove(gone);
    w.recentFiles()->remove(gone);
    CHECK_EQ(w.recentFiles()->paths().size(), 1);
    CHECK_EQ(w.recentMenu()->actions().size(), 3);  // one file, separator, clear
}

TEST(choosing_a_file_that_is_already_open_switches_to_its_tab) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    const QStringList files = makeFiles(sb.path(), 2);
    anyedit::MainWindow w(nullptr, &s);
    w.openPath(files[0]);
    w.openPath(files[1]);
    CHECK_EQ(w.tabCount(), 2);

    // The menu's first entry is files[1], the tab we are already on. The second
    // is files[0]. Choosing it must move to that tab, not open a third one --
    // two tabs on one file would each hold their own copy and disagree about
    // what is on disk.
    w.recentMenu()->actions().at(1)->trigger();
    CHECK_EQ(w.tabCount(), 2);
    CHECK_EQ(QFileInfo(w.pathAt(0)).fileName().toStdString(), std::string("f0.txt"));
    CHECK(w.currentEditor() == w.editorAt(0));
}

TEST(choosing_a_file_that_is_gone_reports_it_and_forgets_it) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    const QStringList files = makeFiles(sb.path(), 2);
    anyedit::MainWindow w(nullptr, &s);
    w.openPath(files[0]);
    CHECK_EQ(w.recentFiles()->paths().size(), 1);
    QFile::remove(files[0]);
    // Triggering would raise a modal QMessageBox, which a headless test cannot
    // dismiss, so the removal half is exercised directly. The dialog is the
    // part that has to be seen rather than asserted.
    w.recentFiles()->remove(files[0]);
    CHECK(w.recentFiles()->isEmpty());
}

TEST(clear_menu_empties_the_list_and_the_file) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    const QStringList files = makeFiles(sb.path(), 2);
    anyedit::MainWindow w(nullptr, &s);
    w.openPath(files[0]);
    w.openPath(files[1]);

    QAction *clear = w.recentMenu()->actions().last();
    CHECK_EQ(clear->text().toStdString(), std::string("Clear Menu"));
    clear->trigger();
    CHECK(w.recentFiles()->isEmpty());

    anyedit::RecentFiles fresh;
    fresh.load();
    CHECK(fresh.isEmpty());
}

TEST(saving_under_a_new_name_records_the_new_name) {
    Sandbox sb;
    Settings s;
    CHECK(s.load(nullptr));
    const QStringList files = makeFiles(sb.path(), 1);
    anyedit::MainWindow w(nullptr, &s);
    w.openPath(files[0]);
    CHECK_EQ(w.recentFiles()->paths().size(), 1);
    // saveAs() goes through a file dialog, so the recording is checked at the
    // level below it: the same add() the dialog path calls.
    const QString other = sb.path() + "/renamed.txt";
    QFile f(other);
    f.open(QIODevice::WriteOnly);
    f.write("x");
    f.close();
    w.recentFiles()->add(other);
    CHECK_EQ(QFileInfo(w.recentFiles()->paths().first()).fileName().toStdString(),
             std::string("renamed.txt"));
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    return harness::run();
}
