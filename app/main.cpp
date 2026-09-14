// app/main.cpp
//
//   anyedit [file...]
//   anyedit --check
//
// Corpus, settings, window, event loop. Everything else is in mainwindow.cpp,
// which is a class in a header so a test can construct it -- see app/tests/.
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

}  // namespace

int main(int argc, char **argv) {
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
