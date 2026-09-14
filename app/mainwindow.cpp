// app/mainwindow.cpp
#include "app/mainwindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUrl>
#include <cstdio>

#include "aced/document.h"
#include "aced/grammar.h"
#include "acedqt/editorwidget.h"
#include "app/aboutdialog.h"
#include "app/findbar.h"
#include "app/languages.h"
#include "app/preferencesdialog.h"
#include "app/recentfiles.h"
#include "app/settings.h"
#include "app/theme.h"

namespace anyedit {

MainWindow::MainWindow(aced::Grammar *grammar, Settings *settings, QWidget *parent)
    : QMainWindow(parent), grammar_(grammar), settings_(settings) {
    recent_ = new RecentFiles(this);
    recent_->load();

    tabs_ = new QTabWidget(this);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::refreshStatus);
    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::syncFindBar);

    // Under the text and over the status bar, which is where Sublime and Qt
    // Creator both put it. A sibling of the tabs rather than a QToolBar, so it
    // cannot be dragged out or docked somewhere it makes no sense, and one bar
    // for all tabs rather than one per tab.
    find_ = new FindBar(this);
    find_->hide();
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tabs_, 1);
    layout->addWidget(find_);
    setCentralWidget(central);
    connect(find_, &FindBar::statusChanged, this, [this](const QString &t) {
        statusBar()->showMessage(t, t.isEmpty() ? 1 : 4000);
    });

    buildMenus();

    // The language is a BUTTON, not part of the label: Sublime puts the syntax
    // in the status bar and lets you click it, and that is where people look
    // for it before they look in a menu.
    languageButton_ = new QToolButton(this);
    languageButton_->setAutoRaise(true);
    languageButton_->setPopupMode(QToolButton::InstantPopup);
    languageMenu_ = new QMenu(languageButton_);
    languageButton_->setMenu(languageMenu_);
    languageButton_->setToolTip("Set the language for this tab");
    buildLanguageMenu(languageMenu_);
    statusBar()->addPermanentWidget(languageButton_);

    status_ = new QLabel(this);
    statusBar()->addPermanentWidget(status_);
    setWindowTitle("anyedit");

    // Any change from any source -- the View menu, or settings.json edited on
    // disk and saved -- lands here and is pushed to every open tab.
    connect(settings_, &Settings::changed, this, &MainWindow::applySettings);
    connect(recent_, &RecentFiles::changed, this, &MainWindow::rebuildRecentMenu);
    rebuildRecentMenu();
    applyWindowSettings();
    applySettings();
}

MainWindow::~MainWindow() = default;

int MainWindow::tabCount() const { return tabs_->count(); }

acedqt::EditorWidget *MainWindow::editorAt(int index) const {
    return qobject_cast<acedqt::EditorWidget *>(tabs_->widget(index));
}

acedqt::EditorWidget *MainWindow::currentEditor() const {
    return qobject_cast<acedqt::EditorWidget *>(tabs_->currentWidget());
}

QString MainWindow::pathAt(int index) const {
    return state_.value(editorAt(index)).path;
}

bool MainWindow::languageIsManualAt(int index) const {
    return state_.value(editorAt(index)).manualLanguage;
}

void MainWindow::setLanguage(acedqt::EditorWidget *ed, const QString &mode,
                             bool manual) {
    if (!ed) return;
    ed->setMode(mode);
    state_[ed].manualLanguage = manual;
    refreshStatus();
}

void MainWindow::openPath(const QString &path) {
    auto *ed = newEditor();
    QString err;
    if (!ed->openFile(path, &err)) {
        QMessageBox::warning(this, "anyedit",
                             QString("Could not open %1:\n%2").arg(path, err));
        delete ed;
        return;
    }
    state_[ed].path = path;
    recent_->add(path);
    const int i = tabs_->addTab(ed, QFileInfo(path).fileName());
    tabs_->setCurrentIndex(i);
    connect(ed, &acedqt::EditorWidget::modifiedChanged, this, [this, ed](bool m) {
        const int idx = tabs_->indexOf(ed);
        if (idx < 0) return;
        const QString base = QFileInfo(state_.value(ed).path).fileName();
        tabs_->setTabText(idx, m ? base + " *" : base);
    });
    refreshStatus();
}

void MainWindow::newTab() {
    auto *ed = newEditor();
    state_[ed] = TabState{};
    tabs_->setCurrentIndex(tabs_->addTab(ed, "untitled"));
    refreshStatus();
}

void MainWindow::closeEvent(QCloseEvent *e) {
    // Geometry is written on the way out, not on every resize: a settings file
    // rewritten on each drag of a window edge is a settings file that loses a
    // hand edit made while the window was being dragged.
    auto a = settings_->app();
    if (a.window.rememberGeometry) {
        a.window.maximized = isMaximized();
        if (!isMaximized()) {
            a.window.width = width();
            a.window.height = height();
        }
        settings_->setApp(a);
    }
    QString err;
    if (!settings_->save(&err)) qWarning("anyedit: %s", qPrintable(err));
    QMainWindow::closeEvent(e);
}

bool MainWindow::event(QEvent *e) {
    if (e->type() == QEvent::ApplicationPaletteChange
        || e->type() == QEvent::ThemeChange) {
        // Only while following the desktop. With an explicit light or dark
        // theme the application palette was set BY applyTheme(), and reacting
        // to that would be a loop.
        if (settings_->app().theme == ThemeMode::System) {
            const Scheme now = detectSystemScheme();
            if (now != lastScheme_) {
                lastScheme_ = now;
                for (int i = 0; i < tabs_->count(); ++i)
                    if (auto *ed = editorAt(i))
                        ed->setEditorPalette(settings_->editorPalette());
            }
        }
    }
    return QMainWindow::event(e);
}

// One environment variable, read once, for the events that are hard to observe
// from the outside: context menus and shortcut routing behave differently on
// every platform and the sandbox cannot run any of them.
//
//    ANYEDITQT_DEBUG_EVENTS=1 anyedit
bool MainWindow::debugEvents() {
    static const bool on = !qEnvironmentVariableIsEmpty("ANYEDITQT_DEBUG_EVENTS");
    return on;
}

acedqt::EditorWidget *MainWindow::newEditor() {
    auto *ed = new acedqt::EditorWidget(this);
    ed->setGrammar(grammar_);
    applyTo(ed);
    connect(ed, &acedqt::EditorWidget::cursorMoved, this, &MainWindow::refreshStatus);
    connect(ed, &acedqt::EditorWidget::contextMenuRequested, this,
            [this, ed](const QPoint &pt, const QPoint &global) {
                if (debugEvents()) {
                    std::fprintf(stderr,
                                 "[anyedit] contextMenuRequested %d,%d current=%d\n",
                                 pt.x(), pt.y(), currentEditor() == ed ? 1 : 0);
                }
                if (currentEditor() == ed) showEditorContextMenu(pt, global);
            });
    // Matches are ranges into a document that just changed, so they are stale
    // the moment it does -- including after the find bar's own replace.
    connect(ed, &acedqt::EditorWidget::textChanged, this, [this, ed] {
        if (find_ && find_->isVisible() && find_->editor() == ed) find_->refresh();
    });
    return ed;
}

// The editor group, applied to one tab. Called for every tab that already
// exists whenever settings change, and once for every tab created after.
void MainWindow::applyTo(acedqt::EditorWidget *ed) {
    const auto &e = settings_->editor();
    ed->setEditorFont(settings_->editorFont());
    ed->setEditorPalette(settings_->editorPalette());
    ed->setTabWidth(e.tabWidth);
    ed->setInsertSpaces(e.insertSpaces);
    ed->setShowLineNumbers(e.showLineNumbers);
    ed->setHighlightCurrentLine(e.highlightCurrentLine);
    // Order matters: setSoftWrap() lays out against the current wrap column, so
    // setting the column afterwards would relayout the whole visible file a
    // second time on every settings change.
    ed->setWrapColumn(e.wrapColumn);
    ed->setSoftWrap(e.softWrap);
}

void MainWindow::applyWindowSettings() {
    const auto &w = settings_->app().window;
    resize(w.width, w.height);
    if (w.maximized) showMaximized();
}

void MainWindow::applySettings() {
    // The chrome first: the editor palette for ThemeMode::System is resolved
    // from the application palette, so it has to be the applied one.
    if (auto *a = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        lastScheme_ = applyTheme(a, settings_->app());
        applyUiFont(a, settings_->app());
    }
    for (int i = 0; i < tabs_->count(); ++i) {
        if (auto *ed = editorAt(i)) applyTo(ed);
    }
    syncMenuState();
    refreshStatus();
}

void MainWindow::buildMenus() {
    auto *file = menuBar()->addMenu("&File");
    file->addAction("&New", QKeySequence::New, this, &MainWindow::newTab);
    file->addAction("&Open...", QKeySequence::Open, this, &MainWindow::openDialog);
    recentMenu_ = file->addMenu("Open &Recent");
    file->addAction("&Save", QKeySequence::Save, this, &MainWindow::save);
    file->addAction("Save &As...", QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    file->addAction("&Close Tab", QKeySequence::Close, this,
                    [this] { closeTab(tabs_->currentIndex()); });
    file->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);

    auto *edit = menuBar()->addMenu("&Edit");
    buildEditActions();
    connect(edit, &QMenu::aboutToShow, this, &MainWindow::syncEditActions);
    edit->addAction(undoAction_);
    edit->addAction(redoAction_);
    edit->addSeparator();
    edit->addAction(cutAction_);
    edit->addAction(copyAction_);
    edit->addAction(pasteAction_);
    edit->addAction(selectAllAction_);
    edit->addSeparator();
    // Tab and Shift+Tab do the same thing; these are the named, discoverable
    // versions and they match what Sublime binds.
    edit->addAction("&Indent", QKeySequence("Ctrl+]"), this, [this] {
        if (auto *ed = currentEditor()) ed->indentSelection();
    });
    edit->addAction("&Unindent", QKeySequence("Ctrl+["), this, [this] {
        if (auto *ed = currentEditor()) ed->unindentSelection();
    });
    edit->addSeparator();
    edit->addAction("&Find...", QKeySequence::Find, this, [this] { showFind(false); });
    edit->addAction("&Replace...", QKeySequence::Replace, this,
                    [this] { showFind(true); });
    edit->addAction("Find &Next", QKeySequence::FindNext, this, [this] {
        if (find_->searchText().isEmpty()) { showFind(false); return; }
        find_->show();
        find_->findNext();
    });
    edit->addAction("Find P&revious", QKeySequence::FindPrevious, this, [this] {
        if (find_->searchText().isEmpty()) { showFind(false); return; }
        find_->show();
        find_->findPrevious();
    });

    auto *view = menuBar()->addMenu("&View");

    // Folding. Ctrl+Shift+[ / ] are VS Code's and are handled inside the
    // widget too, so they work whether or not the menu has focus; these are
    // the discoverable versions. Fold All has no standard binding worth
    // claiming, so it gets none.
    auto *fold = view->addMenu("&Folding");
    fold->addAction("&Fold", QKeySequence("Ctrl+Shift+["), this, [this] {
        auto *ed = currentEditor();
        if (!ed) return;
        if (!ed->foldRow(ed->cursor().row)) ed->foldEnclosing();
    });
    fold->addAction("&Unfold", QKeySequence("Ctrl+Shift+]"), this, [this] {
        if (auto *ed = currentEditor()) ed->unfoldRow(ed->cursor().row);
    });
    fold->addSeparator();
    fold->addAction("Fold &All", this, [this] {
        if (auto *ed = currentEditor()) ed->foldAll();
    });
    fold->addAction("Unfold A&ll", this, [this] {
        if (auto *ed = currentEditor()) ed->unfoldAll();
    });

    buildLanguageMenu(view->addMenu("&Language"));
    auto *themeMenu = view->addMenu("&Theme");
    themeGroup_ = new QActionGroup(this);
    themeGroup_->setExclusive(true);
    const struct { const char *label; ThemeMode mode; } modes[] = {
        {"&System", ThemeMode::System},
        {"&Light", ThemeMode::Light},
        {"&Dark", ThemeMode::Dark},
    };
    for (const auto &m : modes) {
        QAction *a = themeMenu->addAction(m.label);
        a->setCheckable(true);
        a->setData(static_cast<int>(m.mode));
        themeGroup_->addAction(a);
        connect(a, &QAction::triggered, this, [this, mode = m.mode] {
            auto s = settings_->app();
            s.theme = mode;
            settings_->setApp(s);
        });
    }

    view->addSeparator();
    view->addAction("Increase Font Size", QKeySequence::ZoomIn, this,
                    [this] { bumpFont(+1); });
    view->addAction("Decrease Font Size", QKeySequence::ZoomOut, this,
                    [this] { bumpFont(-1); });
    view->addAction("Reset Font Size", QKeySequence("Ctrl+0"), this, [this] {
        auto e = settings_->editor();
        e.fontSize = EditorSettings{}.fontSize;
        settings_->setEditor(e);
    });

    view->addSeparator();
    lineNumbersAction_ = view->addAction("Show &Line Numbers");
    lineNumbersAction_->setCheckable(true);
    connect(lineNumbersAction_, &QAction::triggered, this, [this](bool on) {
        auto e = settings_->editor();
        e.showLineNumbers = on;
        settings_->setEditor(e);
    });
    softWrapAction_ = view->addAction("&Word Wrap");
    softWrapAction_->setCheckable(true);
    softWrapAction_->setShortcut(QKeySequence("Alt+Z"));
    connect(softWrapAction_, &QAction::triggered, this, [this](bool on) {
        auto e = settings_->editor();
        e.softWrap = on;
        settings_->setEditor(e);
    });
    currentLineAction_ = view->addAction("Highlight &Current Line");
    currentLineAction_->setCheckable(true);
    connect(currentLineAction_, &QAction::triggered, this, [this](bool on) {
        auto e = settings_->editor();
        e.highlightCurrentLine = on;
        settings_->setEditor(e);
    });

    auto *prefs = menuBar()->addMenu("&Preferences");
    prefs->addAction("&Settings...", QKeySequence("Ctrl+,"), this,
                     &MainWindow::openPreferences);
    prefs->addAction("Edit settings.&json", this, &MainWindow::openSettingsFile);
    prefs->addAction("Open Settings &Folder", this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::configDir()));
    });

    // Last, and named "Help" so macOS recognises it: AppKit gives that menu its
    // own search field and puts it at the end regardless of where it was added.
    auto *help = menuBar()->addMenu("&Help");
    help->addAction("&About AnyEdit", this, [this] {
        AboutDialog dlg(this);
        dlg.exec();
    });
    help->addAction("About &Qt", qApp, &QApplication::aboutQt);
}

// Opens settings.json as a tab. Saving it fires the file watcher and the change
// applies to the window it was edited in -- which is both the quickest way to
// expose every key and the most direct test that the watch works.
// One dialog, kept alive and re-shown. A second one would be a second thing
// pushing to the same Settings object, and the two would fight over which was
// restored on Cancel.
void MainWindow::openPreferences() {
    if (!prefs_) {
        prefs_ = new PreferencesDialog(settings_, this);
        prefs_->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    prefs_->show();
    prefs_->raise();
    prefs_->activateWindow();
}

PreferencesDialog *MainWindow::preferencesDialog() const { return prefs_; }

void MainWindow::openSettingsFile() {
    QString err;
    if (!settings_->save(&err)) {
        QMessageBox::warning(this, "anyedit", "Could not write settings:\n" + err);
        return;
    }
    const QString p = Settings::configPath();
    for (int i = 0; i < tabs_->count(); ++i) {
        if (pathAt(i) == p) {
            tabs_->setCurrentIndex(i);
            return;
        }
    }
    openPath(p);
}

void MainWindow::bumpFont(int delta) {
    auto e = settings_->editor();
    const int n = e.fontSize + delta;
    e.fontSize = n < 6 ? 6 : (n > 72 ? 72 : n);
    settings_->setEditor(e);
}

void MainWindow::syncMenuState() {
    if (themeGroup_) {
        const int want = static_cast<int>(settings_->app().theme);
        for (QAction *a : themeGroup_->actions()) a->setChecked(a->data().toInt() == want);
    }
    if (lineNumbersAction_)
        lineNumbersAction_->setChecked(settings_->editor().showLineNumbers);
    if (currentLineAction_)
        currentLineAction_->setChecked(settings_->editor().highlightCurrentLine);
    if (softWrapAction_) softWrapAction_->setChecked(settings_->editor().softWrap);
}

// Rebuilt whole rather than patched. The list is twenty entries and the menu is
// rebuilt only when it changes; anything cleverer would be more code than the
// menu.
void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();

    const QStringList paths = recent_->paths();
    if (paths.isEmpty()) {
        QAction *none = recentMenu_->addAction("No Recent Files");
        none->setEnabled(false);
        return;
    }

    // Which base names occur more than once, so only those get their directory
    // shown. Putting the directory on every entry makes the common case -- all
    // different names -- much harder to read than it needs to be.
    QHash<QString, int> seen;
    for (const QString &p : paths) seen[QFileInfo(p).fileName()] += 1;

    int n = 0;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        QString label = fi.fileName();
        if (seen.value(label) > 1) label += "  \u2014  " + fi.absolutePath();
        // ESCAPED, because a single & in a file name is a mnemonic to Qt and
        // disappears from the menu, taking the following letter's underline
        // with it. "Q&A notes.md" would show as "QA notes.md".
        label.replace("&", "&&");
        if (++n <= 9) label = QString("&%1  %2").arg(n).arg(label);

        QAction *a = recentMenu_->addAction(label);
        a->setData(p);
        a->setToolTip(p);
        a->setStatusTip(p);
        connect(a, &QAction::triggered, this, [this, p] { openRecent(p); });
    }

    recentMenu_->addSeparator();
    QAction *clear = recentMenu_->addAction("Clear Menu");
    connect(clear, &QAction::triggered, recent_, &RecentFiles::clear);
}

void MainWindow::openRecent(const QString &path) {
    // Already open: go to that tab rather than opening a second copy of the
    // same file and letting the two disagree about what is on disk.
    for (int i = 0; i < tabs_->count(); ++i) {
        if (pathAt(i) == path) {
            tabs_->setCurrentIndex(i);
            return;
        }
    }
    if (!QFileInfo::exists(path)) {
        // Dropped, not just reported. Leaving a dead entry in the list means
        // the same dialog every time the menu is used.
        recent_->remove(path);
        QMessageBox::warning(this, "anyedit",
                             QString("%1 is no longer there.\n\nIt has been removed "
                                     "from the recent files list.")
                                 .arg(path));
        return;
    }
    openPath(path);
}

// A-Z SUBMENUS, not one flat list. The corpus has 198 languages and a menu with
// 198 items in it is a scrolling column taller than most screens that you
// cannot find anything in. Grouping by first letter turns it into two clicks.
void MainWindow::buildLanguageMenu(QMenu *menu) {
    menu->clear();
    QAction *plain = menu->addAction("Plain Text");
    connect(plain, &QAction::triggered, this,
            [this] { setLanguage(currentEditor(), QString(), true); });
    menu->addSeparator();

    if (!grammar_) return;
    QMenu *group = nullptr;
    QChar groupLetter;
    for (const auto &lang : languageList(*grammar_)) {
        const QChar first = lang.first.isEmpty() ? QChar('?')
                                                 : lang.first.at(0).toUpper();
        if (!group || first != groupLetter) {
            groupLetter = first;
            group = menu->addMenu(QString(first));
        }
        // Escaped: a language whose display name contains an ampersand would
        // otherwise lose it to a mnemonic, same as the recent files menu.
        QString label = lang.first;
        label.replace("&", "&&");
        QAction *a = group->addAction(label);
        a->setData(lang.second);
        const QString mode = lang.second;
        connect(a, &QAction::triggered, this,
                [this, mode] { setLanguage(currentEditor(), mode, true); });
    }
}

void MainWindow::showFind(bool withReplace) {
    syncFindBar();
    find_->activate(withReplace);
}

// The bar follows the current tab. Without this it keeps painting matches into
// the editor it was opened over while the user reads a different one.
void MainWindow::syncFindBar() {
    if (!find_) return;
    find_->setEditor(currentEditor());
    if (find_->isVisible()) find_->refresh();
}

void MainWindow::openDialog() {
    const QStringList files =
        QFileDialog::getOpenFileNames(this, "Open", QDir::currentPath());
    for (const QString &f : files) openPath(f);
}

void MainWindow::save() {
    auto *ed = currentEditor();
    if (!ed) return;
    const QString path = state_.value(ed).path;
    if (path.isEmpty()) {
        saveAs();
        return;
    }
    QString err;
    if (!ed->saveFile(path, &err))
        QMessageBox::warning(this, "anyedit", "Could not save:\n" + err);
}

void MainWindow::saveAs() {
    auto *ed = currentEditor();
    if (!ed) return;
    const QString path =
        QFileDialog::getSaveFileName(this, "Save As", QDir::currentPath());
    if (path.isEmpty()) return;
    QString err;
    if (!ed->saveFile(path, &err)) {
        QMessageBox::warning(this, "anyedit", "Could not save:\n" + err);
        return;
    }
    state_[ed].path = path;
    recent_->add(path);
    tabs_->setTabText(tabs_->indexOf(ed), QFileInfo(path).fileName());
    // A language chosen from the menu SURVIVES a Save As. Re-guessing from the
    // new extension would silently undo a deliberate choice, and the moment
    // that matters most is the one this feature exists for: a new buffer set to
    // a language by hand and then saved.
    if (!state_.value(ed).manualLanguage) {
        ed->setMode(QString::fromStdString(
            aced::Grammar::modeForFilename(path.toStdString())));
    }
    refreshStatus();
}

void MainWindow::closeTab(int i) {
    if (i < 0) return;
    auto *ed = editorAt(i);
    if (ed && ed->isModified()) {
        const auto r = QMessageBox::question(
            this, "anyedit",
            QString("%1 has unsaved changes. Close it anyway?").arg(tabs_->tabText(i)),
            QMessageBox::Discard | QMessageBox::Cancel);
        if (r != QMessageBox::Discard) return;
    }
    tabs_->removeTab(i);
    state_.remove(ed);
    delete ed;
    refreshStatus();
}


// The clipboard and history actions exist ONCE and are put into both the Edit
// menu and the context menu. Two sets would mean two labels, two shortcut
// declarations and two chances to fix a bug in only one of them.
//
// THE SHORTCUTS ARE LABELS, NOT BINDINGS. EditorWidget already handles Ctrl+C,
// X, V, Z, Y and A in its own keyPressEvent, which is what keeps it usable
// embedded in a host with no menu bar at all. An action with the default
// WindowShortcut context would take those keys for the whole window and win
// over the focused widget -- so Ctrl+C with the caret in the find bar's line
// edit would copy the editor's selection instead of the search text. Qt has no
// "display only" flag, but WidgetShortcut on an action that is never added to
// a widget can never fire, and the menu still draws the sequence beside the
// entry. That is the whole trick.
void MainWindow::buildEditActions() {
    auto make = [this](const QString &text, QKeySequence keys, auto fn) {
        auto *a = new QAction(text, this);
        a->setShortcut(keys);
        a->setShortcutContext(Qt::WidgetShortcut);
        a->setShortcutVisibleInContextMenu(true);
        connect(a, &QAction::triggered, this, [this, fn] {
            if (auto *ed = currentEditor()) fn(ed);
        });
        return a;
    };

    undoAction_ = make("&Undo", QKeySequence::Undo,
                       [](acedqt::EditorWidget *ed) { ed->undoEdit(); });
    redoAction_ = make("&Redo", QKeySequence::Redo,
                       [](acedqt::EditorWidget *ed) { ed->redoEdit(); });
    cutAction_ = make("Cu&t", QKeySequence::Cut,
                      [](acedqt::EditorWidget *ed) { ed->cut(); });
    copyAction_ = make("&Copy", QKeySequence::Copy,
                       [](acedqt::EditorWidget *ed) { ed->copy(); });
    pasteAction_ = make("&Paste", QKeySequence::Paste,
                        [](acedqt::EditorWidget *ed) { ed->paste(); });
    selectAllAction_ = make("Select &All", QKeySequence::SelectAll,
                            [](acedqt::EditorWidget *ed) { ed->selectAll(); });

}

// Greyed out is information: Cut with nothing selected would do nothing, and an
// enabled entry that does nothing reads as a bug in the editor.
void MainWindow::syncEditActions() {
    auto *ed = currentEditor();
    const bool have = ed != nullptr;
    const bool sel = have && ed->hasSelection();
    undoAction_->setEnabled(have && ed->canUndo());
    redoAction_->setEnabled(have && ed->canRedo());
    cutAction_->setEnabled(sel);
    copyAction_->setEnabled(sel);
    pasteAction_->setEnabled(have && !QApplication::clipboard()->text().isEmpty());
    selectAllAction_->setEnabled(have && ed->document()->lineCount() > 0);
}

QMenu *MainWindow::prepareContextMenu(const QPoint &viewportPos) {
    auto *ed = currentEditor();
    if (!ed) return nullptr;
    // A right-click outside the selection puts the caret where it landed, the
    // way every other editor does, so Cut afterwards cuts something the user
    // can see. A right-click INSIDE the selection leaves it alone -- moving it
    // there would throw away what they were about to act on.
    const aced::Position p = ed->positionForPoint(viewportPos);
    const aced::Range sel = ed->selection();
    const bool inside = ed->hasSelection() && !(p < sel.start) && p < sel.end;
    if (!inside) ed->setCursor(p);
    syncEditActions();

    // A FRESH QMenu each time, holding the shared actions. QScintilla builds
    // its context menu this way and deletes it on close, and it is the shape
    // that works: a long-lived popup menu can be left in a state where it
    // believes it is already shown, and a second popup() then does nothing
    // with no error. The ACTIONS are still the Edit menu's own, so there is
    // one place a label or a handler is defined.
    auto *menu = new QMenu(ed);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(undoAction_);
    menu->addAction(redoAction_);
    menu->addSeparator();
    menu->addAction(cutAction_);
    menu->addAction(copyAction_);
    menu->addAction(pasteAction_);
    menu->addSeparator();
    menu->addAction(selectAllAction_);
    return menu;
}

void MainWindow::showEditorContextMenu(const QPoint &viewportPos, const QPoint &globalPos) {
    QMenu *menu = prepareContextMenu(viewportPos);
    if (!menu) return;
    if (debugEvents()) {
        std::fprintf(stderr, "[anyedit] popup at global %d,%d with %d actions\n",
                     globalPos.x(), globalPos.y(),
                     static_cast<int>(menu->actions().size()));
    }
    // popup(), not exec(). QScintilla uses popup() and works on macOS, so the
    // trailing right-button release is not the problem exec() would be solving
    // -- and exec() spins its own event loop, which makes a right-click
    // re-entrant with whatever else the window is doing and the whole path
    // untestable.
    menu->popup(globalPos);
}

void MainWindow::refreshStatus() {
    auto *ed = currentEditor();
    if (!ed) {
        status_->setText("");
        if (languageButton_) languageButton_->setText("");
        return;
    }
    const auto c = ed->cursor();
    const auto &e = settings_->editor();
    if (languageButton_)
        languageButton_->setText(languageDisplayName(ed->mode().toStdString()));
    status_->setText(QString("%1 %2%3    Ln %4, Col %5")
                         .arg(e.insertSpaces ? "Spaces:" : "Tab width:")
                         .arg(e.tabWidth)
                         .arg(e.softWrap ? "    Wrap" : "")
                         .arg(c.row + 1)
                         .arg(c.column + 1));
}

}  // namespace anyedit
