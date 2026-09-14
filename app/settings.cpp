// app/settings.cpp
#include "app/settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QPalette>
#include <QSaveFile>
#include <QStyleHints>

#include "acedqt/palette.h"

namespace anyedit {
namespace {

// Clamped, not rejected. A settings file is hand-edited; a font size of 400
// should give a large font and a readable window, not a refusal to start.
int clampInt(const QJsonValue &v, int fallback, int lo, int hi) {
    if (!v.isDouble()) return fallback;
    const int n = v.toInt(fallback);
    return n < lo ? lo : (n > hi ? hi : n);
}

bool boolOr(const QJsonValue &v, bool fallback) {
    return v.isBool() ? v.toBool() : fallback;
}

QString stringOr(const QJsonValue &v, const QString &fallback) {
    return v.isString() ? v.toString() : fallback;
}

// A palette name that is not one of ours falls back rather than erroring: the
// file may well have come from a build with more themes in it.
QString validPaletteOr(const QJsonValue &v, const QString &fallback) {
    const QString s = stringOr(v, fallback);
    return paletteExists(s) ? s : fallback;
}

}  // namespace

// --- palettes by name ------------------------------------------------------

QStringList paletteNames() { return {"tomorrow-night", "dayfold"}; }
bool paletteExists(const QString &name) { return paletteNames().contains(name); }
QString paletteName(bool dark) { return dark ? "tomorrow-night" : "dayfold"; }

acedqt::Palette paletteByName(const QString &name) {
    if (name == "dayfold") return acedqt::Palette::dayfold();
    return acedqt::Palette::tomorrowNight();
}

// --- comparison ------------------------------------------------------------

bool EditorSettings::operator==(const EditorSettings &o) const {
    return fontFamily == o.fontFamily && fontSize == o.fontSize
           && tabWidth == o.tabWidth && insertSpaces == o.insertSpaces
           && showLineNumbers == o.showLineNumbers
           && highlightCurrentLine == o.highlightCurrentLine
           && softWrap == o.softWrap && wrapColumn == o.wrapColumn
           && paletteDark == o.paletteDark && paletteLight == o.paletteLight;
}

bool WindowSettings::operator==(const WindowSettings &o) const {
    return width == o.width && height == o.height && maximized == o.maximized
           && rememberGeometry == o.rememberGeometry;
}

bool AppSettings::operator==(const AppSettings &o) const {
    return theme == o.theme && uiFontFamily == o.uiFontFamily
           && uiFontSize == o.uiFontSize && window == o.window;
}

// --- paths -----------------------------------------------------------------

QString Settings::configDir() {
    const QByteArray env = qgetenv("ANYEDITQT_CONFIG_DIR");
    if (!env.isEmpty()) return QDir::cleanPath(QString::fromLocal8Bit(env));
    return QDir::cleanPath(QDir::homePath() + "/.anyeditqt");
}

QString Settings::configPath() { return configDir() + "/settings.json"; }

// --- fonts -----------------------------------------------------------------

QString Settings::defaultMonospaceFamily() {
    // Ordered by preference, filtered by what is installed. The list is per
    // platform because the good default differs: Consolas does not exist on
    // Linux and DejaVu Sans Mono does not exist on a stock Windows.
#if defined(Q_OS_WIN)
    const QStringList wanted = {"Cascadia Mono", "Consolas", "Lucida Console",
                                "Courier New"};
#elif defined(Q_OS_MACOS)
    const QStringList wanted = {"SF Mono", "Menlo", "Monaco", "Courier New"};
#else
    const QStringList wanted = {"JetBrains Mono", "Fira Mono", "DejaVu Sans Mono",
                                "Liberation Mono", "Noto Sans Mono",
                                "Ubuntu Mono", "monospace"};
#endif
    for (const QString &f : wanted) {
        if (QFontDatabase::hasFamily(f)) return f;
    }
    // Always answers something, whatever is installed. QFontInfo resolves the
    // style hint against the actual database rather than echoing the request.
    QFont probe;
    probe.setStyleHint(QFont::Monospace);
    probe.setFamily(probe.defaultFamily());
    return QFontInfo(probe).family();
}

QFont Settings::editorFont() const {
    QFont f(editor_.fontFamily.isEmpty() ? defaultMonospaceFamily()
                                         : editor_.fontFamily);
    f.setPointSize(editor_.fontSize);
    f.setStyleHint(QFont::Monospace);
    // Not cosmetic. LineCache measures one character to size the gutter and the
    // horizontal scrollbar; a proportional family makes that measurement a lie.
    f.setFixedPitch(true);
    return f;
}

// --- theme -----------------------------------------------------------------

QString Settings::themeToString(ThemeMode m) {
    switch (m) {
        case ThemeMode::Light: return "light";
        case ThemeMode::Dark: return "dark";
        case ThemeMode::System: break;
    }
    return "system";
}

ThemeMode Settings::themeFromString(const QString &s, bool *ok) {
    if (ok) *ok = true;
    const QString v = s.toLower();
    if (v == "light") return ThemeMode::Light;
    if (v == "dark") return ThemeMode::Dark;
    if (v == "system") return ThemeMode::System;
    if (ok) *ok = false;
    return ThemeMode::System;
}

Scheme detectSystemScheme() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // The real signal where there is one. Unknown falls through to the
    // lightness test rather than guessing light, because Unknown is what a
    // platform with no notion of a scheme reports and its palette still says
    // which one it is using.
    if (auto *h = QGuiApplication::styleHints()) {
        const Qt::ColorScheme s = h->colorScheme();
        if (s == Qt::ColorScheme::Dark) return Scheme::Dark;
        if (s == Qt::ColorScheme::Light) return Scheme::Light;
    }
#endif
    // Qt 6.4 and below have no colorScheme(). The window colour is what every
    // desktop that themes Qt at all sets, so its lightness answers the question
    // -- and if nothing themes Qt, the default palette is light and the answer
    // is light, which is correct.
    const QColor win = QGuiApplication::palette().color(QPalette::Window);
    return win.lightness() < 128 ? Scheme::Dark : Scheme::Light;
}

Scheme Settings::resolvedScheme() const {
    switch (app_.theme) {
        case ThemeMode::Light: return Scheme::Light;
        case ThemeMode::Dark: return Scheme::Dark;
        case ThemeMode::System: break;
    }
    return detectSystemScheme();
}

acedqt::Palette Settings::editorPalette() const {
    return paletteByName(resolvedScheme() == Scheme::Dark ? editor_.paletteDark
                                                          : editor_.paletteLight);
}

// --- lifecycle -------------------------------------------------------------

Settings::Settings(QObject *parent) : QObject(parent) {}
Settings::~Settings() = default;

void Settings::setEditor(const EditorSettings &e) {
    if (e == editor_) return;
    editor_ = e;
    Q_EMIT changed();
}

void Settings::setApp(const AppSettings &a) {
    if (a == app_) return;
    app_ = a;
    Q_EMIT changed();
}

// --- load / save -----------------------------------------------------------

bool Settings::load(QString *error) {
    const QString path = configPath();
    QFile f(path);
    if (!f.exists()) {
        // First run. Defaults are already in place; write them so there is a
        // file to edit, and report a write failure since a config directory
        // that cannot be created means nothing will persist all session.
        return save(error);
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("%1: %2").arg(path, f.errorString());
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            // The byte offset, because "invalid JSON" in a file somebody just
            // hand-edited is not actionable and a position is.
            *error = QString("%1: %2 at offset %3")
                         .arg(path,
                              pe.error == QJsonParseError::NoError
                                  ? QString("expected a JSON object")
                                  : pe.errorString())
                         .arg(pe.offset);
        }
        return false;
    }

    raw_ = doc.object();
    const QJsonObject ed = raw_.value("editor").toObject();
    const QJsonObject ap = raw_.value("app").toObject();
    const QJsonObject win = ap.value("window").toObject();

    EditorSettings e;
    e.fontFamily = stringOr(ed.value("font_family"), e.fontFamily);
    e.fontSize = clampInt(ed.value("font_size"), e.fontSize, 6, 72);
    e.tabWidth = clampInt(ed.value("tab_width"), e.tabWidth, 1, 16);
    e.insertSpaces = boolOr(ed.value("insert_spaces"), e.insertSpaces);
    e.showLineNumbers = boolOr(ed.value("show_line_numbers"), e.showLineNumbers);
    e.highlightCurrentLine =
        boolOr(ed.value("highlight_current_line"), e.highlightCurrentLine);
    e.softWrap = boolOr(ed.value("soft_wrap"), e.softWrap);
    e.wrapColumn = clampInt(ed.value("wrap_column"), e.wrapColumn, 0, 1000);
    e.paletteDark = validPaletteOr(ed.value("palette_dark"), e.paletteDark);
    e.paletteLight = validPaletteOr(ed.value("palette_light"), e.paletteLight);

    AppSettings a;
    a.theme = themeFromString(stringOr(ap.value("theme"), "system"));
    a.uiFontFamily = stringOr(ap.value("ui_font_family"), a.uiFontFamily);
    a.uiFontSize = clampInt(ap.value("ui_font_size"), a.uiFontSize, 0, 72);
    a.window.width = clampInt(win.value("width"), a.window.width, 240, 32000);
    a.window.height = clampInt(win.value("height"), a.window.height, 180, 32000);
    a.window.maximized = boolOr(win.value("maximized"), a.window.maximized);
    a.window.rememberGeometry =
        boolOr(win.value("remember_geometry"), a.window.rememberGeometry);

    const bool differs = (e != editor_) || (a != app_);
    editor_ = e;
    app_ = a;
    lastWritten_ = bytes;
    if (differs) Q_EMIT changed();
    return true;
}

bool Settings::save(QString *error) {
    const QString dir = configDir();
    if (!QDir().mkpath(dir)) {
        if (error) *error = QString("could not create %1").arg(dir);
        return false;
    }

    // Merge into the object that was read, rather than building a fresh one.
    // Keys from a newer build survive a save by an older one; without this,
    // opening settings.json in last month's binary quietly deletes them.
    QJsonObject root = raw_;
    QJsonObject ed = root.value("editor").toObject();
    ed["font_family"] = editor_.fontFamily;
    ed["font_size"] = editor_.fontSize;
    ed["tab_width"] = editor_.tabWidth;
    ed["insert_spaces"] = editor_.insertSpaces;
    ed["show_line_numbers"] = editor_.showLineNumbers;
    ed["highlight_current_line"] = editor_.highlightCurrentLine;
    ed["soft_wrap"] = editor_.softWrap;
    ed["wrap_column"] = editor_.wrapColumn;
    ed["palette_dark"] = editor_.paletteDark;
    ed["palette_light"] = editor_.paletteLight;
    root["editor"] = ed;

    QJsonObject ap = root.value("app").toObject();
    ap["theme"] = themeToString(app_.theme);
    ap["ui_font_family"] = app_.uiFontFamily;
    ap["ui_font_size"] = app_.uiFontSize;
    QJsonObject win = ap.value("window").toObject();
    win["width"] = app_.window.width;
    win["height"] = app_.window.height;
    win["maximized"] = app_.window.maximized;
    win["remember_geometry"] = app_.window.rememberGeometry;
    ap["window"] = win;
    root["app"] = ap;

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);

    // QSaveFile: write to a temporary and rename, so an interrupted save leaves
    // the previous settings rather than half a file. The watcher below has to
    // know that the rename replaces the inode -- see onFileChanged.
    QSaveFile out(configPath());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QString("%1: %2").arg(configPath(), out.errorString());
        return false;
    }
    if (out.write(bytes) != bytes.size() || !out.commit()) {
        if (error) *error = QString("%1: %2").arg(configPath(), out.errorString());
        return false;
    }
    raw_ = root;
    lastWritten_ = bytes;
    return true;
}

// --- watching --------------------------------------------------------------

void Settings::setWatching(bool on) {
    if (on == (watcher_ != nullptr)) return;
    if (!on) {
        delete watcher_;
        watcher_ = nullptr;
        return;
    }
    watcher_ = new QFileSystemWatcher(this);
    // BOTH the file and its directory. A QSaveFile commit is a rename, which
    // deletes the watched inode; the file watch fires once and then goes dead,
    // so the directory watch is what notices the replacement and re-arms it.
    watcher_->addPath(configDir());
    if (QFileInfo::exists(configPath())) watcher_->addPath(configPath());
    connect(watcher_, &QFileSystemWatcher::fileChanged, this,
            &Settings::onFileChanged);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
            &Settings::onFileChanged);
}

void Settings::onFileChanged(const QString &) {
    const QString path = configPath();
    if (watcher_ && !watcher_->files().contains(path) && QFileInfo::exists(path))
        watcher_->addPath(path);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = f.readAll();
    f.close();
    // Our own save() comes back through here via the directory watch. Comparing
    // the bytes we last wrote is cheaper and more reliable than a flag, which
    // would have to guess how long the notification takes to arrive.
    if (bytes == lastWritten_) return;
    load(nullptr);
}

}  // namespace anyedit
