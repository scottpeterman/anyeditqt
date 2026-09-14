// app/settings.h
//
// ~/.anyeditqt/settings.json, in two groups.
//
//   "editor"  -- everything that applies to an EditorWidget inside a tab:
//                font, tab width, gutter, the palette each scheme uses.
//   "app"     -- everything outside the tab: theme, the chrome font, the
//                window.
//
// The split is not cosmetic. The editor group is applied to every open tab and
// to every tab opened later; the app group is applied once, to QApplication and
// to the main window. Anything that has to reach both -- the theme -- lives in
// "app" and is pushed down, because there is one window and many tabs.
//
// NOTHING HERE IS DECLARED AND IGNORED. Every key below is read by main.cpp and
// changes something you can see. Keys the running binary does not know are
// preserved verbatim on save (see load/save), so a settings.json written by a
// newer build survives an older one rather than being silently truncated.
#pragma once

#include <QFont>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QFileSystemWatcher;

namespace acedqt {
class Palette;
}

namespace anyedit {

// Which palette the editor uses. Two exist; both are named in the file so the
// key has meaning rather than being a bool spelled as a string.
QString paletteName(bool dark);
bool paletteExists(const QString &name);
acedqt::Palette paletteByName(const QString &name);
QStringList paletteNames();

struct EditorSettings {
    // Empty means "the best monospace family on this platform", resolved at
    // apply time by defaultMonospaceFamily(). Stored empty rather than resolved
    // so the same settings.json is portable across the three platforms, which
    // is the entire point of a cross-platform editor having a config file.
    QString fontFamily;
    int fontSize = 11;
    int tabWidth = 4;
    bool insertSpaces = true;
    bool showLineNumbers = true;
    bool highlightCurrentLine = true;
    bool softWrap = false;
    // Characters. Zero wraps at whatever the window is currently wide enough
    // for, which is what most people mean by wrapping; a number pins it, which
    // is what somebody writing to an 80-column standard means.
    int wrapColumn = 0;
    QString paletteDark = "tomorrow-night";
    QString paletteLight = "dayfold";

    bool operator==(const EditorSettings &o) const;
    bool operator!=(const EditorSettings &o) const { return !(*this == o); }
};

enum class ThemeMode { System, Light, Dark };
enum class Scheme { Light, Dark };

// What the desktop is currently using, independent of any stored setting.
// Qt 6.5+ asks QStyleHints; below that it reads the window colour's lightness.
Scheme detectSystemScheme();

struct WindowSettings {
    int width = 1000;
    int height = 700;
    bool maximized = false;
    // When false the two above are left alone on exit, so a window dragged
    // about for one session does not become the permanent size.
    bool rememberGeometry = true;

    bool operator==(const WindowSettings &o) const;
    bool operator!=(const WindowSettings &o) const { return !(*this == o); }
};

struct AppSettings {
    ThemeMode theme = ThemeMode::System;
    // Empty family / zero size mean "whatever Qt picked for this desktop".
    // A user who wants larger menus sets the size and leaves the family alone.
    QString uiFontFamily;
    int uiFontSize = 0;
    WindowSettings window;

    bool operator==(const AppSettings &o) const;
    bool operator!=(const AppSettings &o) const { return !(*this == o); }
};

class Settings : public QObject {
    Q_OBJECT

public:
    // $ANYEDITQT_CONFIG_DIR overrides, which is what the tests use. Without it,
    // ~/.anyeditqt on every platform -- not APPDATA and not XDG_CONFIG_HOME,
    // because one documented path across the three beats three correct ones.
    static QString configDir();
    static QString configPath();

    // The monospace family this platform actually has, checked against the
    // installed families rather than assumed. Falls back to whatever Qt's
    // Monospace style hint resolves to, which always answers something.
    static QString defaultMonospaceFamily();

    static QString themeToString(ThemeMode m);
    static ThemeMode themeFromString(const QString &s, bool *ok = nullptr);

    explicit Settings(QObject *parent = nullptr);
    ~Settings() override;

    // Reads configPath(). A missing file is not an error: defaults are kept and
    // the file is written, so the first run leaves something to edit rather
    // than an empty directory. A malformed file IS an error -- defaults are
    // kept in memory, the file is left alone, and the caller says so. Silently
    // overwriting a file somebody hand-edited badly is the wrong answer; they
    // want to know which line.
    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr);

    const EditorSettings &editor() const { return editor_; }
    const AppSettings &app() const { return app_; }

    // Both emit changed() only when something actually differs, so a save
    // triggered by an unrelated key does not repaint every tab.
    void setEditor(const EditorSettings &e);
    void setApp(const AppSettings &a);

    // The font the editor group resolves to on this machine, family fallback
    // applied. Fixed pitch is forced: a proportional family in the editor makes
    // the gutter and the cursor disagree with the text.
    QFont editorFont() const;

    // ThemeMode::System resolved against the desktop. On Qt 6.5+ that is
    // QStyleHints::colorScheme(); below it, the lightness of the window colour,
    // which is right on every desktop that themes Qt and wrong on none that
    // matter. See the note in settings.cpp.
    Scheme resolvedScheme() const;
    acedqt::Palette editorPalette() const;

    // Reload when the file changes underneath us, so editing settings.json in
    // anyedit itself applies on save. Off by default: the tests drive load()
    // directly and a watcher would make them racy.
    void setWatching(bool on);
    bool isWatching() const { return watcher_ != nullptr; }

Q_SIGNALS:
    void changed();

private:
    void onFileChanged(const QString &path);

    EditorSettings editor_;
    AppSettings app_;
    // Keys this build does not understand, kept so save() can put them back.
    QJsonObject raw_;
    QFileSystemWatcher *watcher_ = nullptr;
    QByteArray lastWritten_;
};

}  // namespace anyedit
