// app/main.cpp
//
//   anyedit [file...]
//   anyedit --check
//
// Corpus, settings, window, event loop. Everything else is in mainwindow.cpp,
// which is a class in a header so a test can construct it -- see app/tests/.
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#include <iostream>
#endif

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QFileInfo>
#include <QMessageBox>

#include "aced/grammar.h"
#include "app/mainwindow.h"
#include "app/settings.h"

namespace {

// The same lookup tools/render_probe.cpp uses, and the same one the bundle
// scripts verify: Resources/grammars.json beside the executable, then one level
// up, then the source tree. A packaged build that cannot find its corpus opens
// and highlights nothing, which reads as a tokenizer bug.
QString findGrammars() {
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString &c : {appDir + "/Resources/grammars.json",
                             appDir + "/../Resources/grammars.json",
                             appDir + "/../../grammars/grammars.json",
                             appDir + "/../../../grammars/grammars.json"}) {
        if (QFileInfo::exists(c)) return QDir::cleanPath(c);
    }
    return {};
}

#ifdef _WIN32
// Make stdout and stderr work when there is somewhere for them to go, and not
// open a console window when there is not.
//
// The executable is linked WIN32_EXECUTABLE, i.e. /SUBSYSTEM:WINDOWS, so
// double-clicking it does not drag a black box along behind the editor. The
// cost is that the CRT binds stdout to nothing, and `anyedit --check` -- which
// bundle-windows.bat greps to prove a staged tree finds its own corpus -- goes
// silent.
//
// THE ORDER BELOW IS THE WHOLE THING, and the obvious version is wrong:
//
//   AttachConsole(ATTACH_PARENT_PROCESS);
//   freopen_s(&f, "CONOUT$", "w", stdout);
//
// That prints to the console, and ONLY to the console. AttachConsole resets
// the standard handles, and reopening CONOUT$ then points stdout at the
// console window -- overriding any redirection the caller asked for. Run with
// `> file` the text appears on screen and the file is empty, which is exactly
// what happened: the smoke test could see its own output and still failed.
//
// So the handles are read FIRST. One that already refers to a file, a pipe or
// an inherited console is bound to the CRT stream as it is. Only when there is
// no handle at all is the parent's console borrowed -- and that is the case
// where there is nothing to override.
bool bindStandardStream(DWORD which, FILE *stream, const char *device,
                        const char *mode) {
    const HANDLE h = GetStdHandle(which);
    if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) {
        const int flags = (mode[0] == 'r') ? (_O_RDONLY | _O_TEXT) : _O_TEXT;
        const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), flags);
        if (fd == -1) return false;
        if (_dup2(fd, _fileno(stream)) != 0) return false;
        setvbuf(stream, nullptr, _IONBF, 0);
        return true;
    }
    FILE *f = nullptr;
    return freopen_s(&f, device, mode, stream) == 0;
}

void attachParentConsole() {
    // Read before attaching: AttachConsole overwrites these.
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    const auto usable = [](HANDLE h) {
        return h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
    };
    const bool haveOut = usable(out);
    const bool haveErr = usable(err);

    if (!haveOut && !haveErr) {
        // Nothing inherited. Either launched from Explorer, where this fails
        // and nothing happens, or from a shell whose console we can borrow.
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    }
    bindStandardStream(STD_OUTPUT_HANDLE, stdout, "CONOUT$", "w");
    bindStandardStream(STD_ERROR_HANDLE, stderr, "CONOUT$", "w");
    std::ios::sync_with_stdio(true);
}
#endif  // _WIN32

}  // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
    attachParentConsole();
#endif
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("anyedit");
    QCoreApplication::setOrganizationName("anyeditqt");
    QCoreApplication::setApplicationVersion(ANYEDIT_VERSION);

    // The window manager's icon: title bar, task switcher, panel. The macOS
    // bundle icon and the Linux .desktop Icon= cover the Dock and the launcher
    // and are read from elsewhere; this one has to be set by the process.
    // Compiled in rather than loaded from assets/, which exists only in a
    // source tree -- see app/anyedit.qrc.
    {
        QIcon icon;
        for (const char *p : {":/icons/anyedit-32.png", ":/icons/anyedit-128.png",
                              ":/icons/anyedit-256.png"})
            icon.addFile(QString::fromLatin1(p));
        if (!icon.isNull()) QApplication::setWindowIcon(icon);
    }

    // --check: resolve and load the corpus, say what happened, exit. No window,
    // no event loop, and NO settings file written -- a packaging smoke test
    // should not leave a config directory behind on the build machine. It
    // exists so the packaging scripts can assert that a STAGED tree finds its
    // own grammars.json, which is the one thing about this application that is
    // broken by packaging and by nothing else. The alternative -- starting the
    // GUI under a timeout and grepping whatever it prints on the way up --
    // works, but treats exit-by-SIGKILL as success and tells you nothing if the
    // message ever changes.
    const bool checkOnly = app.arguments().contains("--check");

    static aced::Grammar grammar;
    const QString gp = findGrammars();
    if (checkOnly) {
        if (gp.isEmpty()) {
            fprintf(stderr,
                    "no grammars found: looked for Resources/grammars.json beside %s "
                    "and one level up\n",
                    QCoreApplication::applicationDirPath().toUtf8().constData());
            return 1;
        }
        if (!grammar.loadFile(gp.toStdString())) {
            fprintf(stderr, "grammars unreadable: %s\n", grammar.error().c_str());
            return 1;
        }
        printf("grammars: %s\nmodes: %zu\nqt: %s\nsettings: %s\n",
               gp.toUtf8().constData(), grammar.modes().size(), qVersion(),
               anyedit::Settings::configPath().toUtf8().constData());
        return 0;
    }

    if (gp.isEmpty() || !grammar.loadFile(gp.toStdString())) {
        // Said by name and with the paths tried. An editor that silently opens
        // without syntax highlighting looks like a tokenizer bug for an hour.
        QMessageBox::critical(
            nullptr, "anyedit",
            QString("No grammar corpus found.\n\nLooked for Resources/grammars.json "
                    "beside %1 and one level up.\n\nSyntax highlighting is not "
                    "available.")
                .arg(QCoreApplication::applicationDirPath()));
    }

    anyedit::Settings settings;
    QString serr;
    if (!settings.load(&serr)) {
        // Not fatal, and not silent. Defaults are in force; the file is left
        // exactly as it is so the offending line can be found and fixed.
        QMessageBox::warning(nullptr, "anyedit",
                             "Could not read settings; using defaults.\n\n" + serr);
    }
    settings.setWatching(true);

    anyedit::MainWindow w(&grammar, &settings);
    const QStringList args = app.arguments().mid(1);
    for (const QString &a : args) {
        if (a == "--check") continue;
        w.openPath(a);
    }
    if (w.tabCount() == 0) w.newTab();
    w.show();
    return app.exec();
}