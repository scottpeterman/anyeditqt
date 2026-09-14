// app/mainwindow.h
//
// The window: tabs, the File/View/Preferences menus, the status bar, and the
// wiring that pushes anyedit::Settings down onto every open editor.
//
// It is a class in a header rather than an anonymous namespace inside main.cpp
// so it can be constructed by a test and by an offscreen screenshot harness.
// A window that can only be reached by launching the application is a window
// whose menus and theme switching get checked by hand or not at all.
#pragma once

#include <QHash>
#include <QMainWindow>
#include <QString>

#include "app/settings.h"

class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QTabWidget;
class QToolButton;

namespace aced {
class Grammar;
}
namespace acedqt {
class EditorWidget;
}

namespace anyedit {

class FindBar;
class PreferencesDialog;
class RecentFiles;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // Neither pointer is owned and both must outlive the window: the grammar
    // corpus is shared across every tab, and the settings object is shared with
    // whatever else wants to read it.
    MainWindow(aced::Grammar *grammar, Settings *settings,
               QWidget *parent = nullptr);
    ~MainWindow() override;

    // Everything showEditorContextMenu() does EXCEPT putting the menu on
    // screen: place the caret, enable the right entries, hand back the menu.
    // Split out because exec() does not return until the menu closes, so a
    // test cannot call the whole thing -- and public for the same reason.
    QMenu *prepareContextMenu(const QPoint &viewportPos);

    void openPath(const QString &path);
    void newTab();

    // For tests and for the screenshot harness.
    int tabCount() const;
    acedqt::EditorWidget *editorAt(int index) const;
    acedqt::EditorWidget *currentEditor() const;
    QString pathAt(int index) const;
    // True when the language was chosen from the menu rather than guessed from
    // the file name.
    bool languageIsManualAt(int index) const;
    void setLanguage(acedqt::EditorWidget *ed, const QString &mode, bool manual);
    void openPreferences();
    PreferencesDialog *preferencesDialog() const;
    RecentFiles *recentFiles() const { return recent_; }
    FindBar *findBar() const { return find_; }
    void showFind(bool withReplace);
    QMenu *recentMenu() const { return recentMenu_; }

protected:
    void closeEvent(QCloseEvent *e) override;
    // Follows the desktop when the theme is "system". NOT
    // QStyleHints::colorSchemeChanged, which is Qt 6.5+: this project builds
    // against 6.4 as well, and a signal that only exists on half the supported
    // versions is a code path that only ever gets tested on half of them.
    // ApplicationPaletteChange is delivered on every Qt 6 and fires for the
    // same reason.
    bool event(QEvent *e) override;

private:
    acedqt::EditorWidget *newEditor();
    void applyTo(acedqt::EditorWidget *ed);
    void applyWindowSettings();
    void applySettings();
    void buildMenus();
    void bumpFont(int delta);
    void syncMenuState();
    void openDialog();
    void save();
    void saveAs();
    void closeTab(int i);
    void refreshStatus();
    void openSettingsFile();
    void rebuildRecentMenu();
    void buildLanguageMenu(QMenu *menu);
    void syncFindBar();
    // The clipboard and history actions, shared by the Edit menu and the
    // editor's right-click menu so the two cannot drift apart.
    void buildEditActions();
    void showEditorContextMenu(const QPoint &viewportPos, const QPoint &globalPos);
    static bool debugEvents();
    void syncEditActions();
    void openRecent(const QString &path);

    aced::Grammar *grammar_;
    Settings *settings_;
    QTabWidget *tabs_ = nullptr;
    QLabel *status_ = nullptr;
    QActionGroup *themeGroup_ = nullptr;
    QAction *lineNumbersAction_ = nullptr;
    QAction *currentLineAction_ = nullptr;
    QAction *softWrapAction_ = nullptr;
    Scheme lastScheme_ = Scheme::Light;
    PreferencesDialog *prefs_ = nullptr;
    RecentFiles *recent_ = nullptr;
    FindBar *find_ = nullptr;
    QMenu *recentMenu_ = nullptr;
    QAction *undoAction_ = nullptr;
    QAction *redoAction_ = nullptr;
    QAction *cutAction_ = nullptr;
    QAction *copyAction_ = nullptr;
    QAction *pasteAction_ = nullptr;
    QAction *selectAllAction_ = nullptr;
    // PER EDITOR, NOT PER TAB INDEX. The tab bar is movable, so a tab's index
    // changes when it is dragged and a list indexed by position silently starts
    // describing the wrong file -- which meant Ctrl+S writing over a different
    // document. Keyed by the widget, reordering cannot touch it.
    struct TabState {
        QString path;
        bool manualLanguage = false;
    };
    QHash<acedqt::EditorWidget *, TabState> state_;
    QToolButton *languageButton_ = nullptr;
    QMenu *languageMenu_ = nullptr;
};

}  // namespace anyedit
