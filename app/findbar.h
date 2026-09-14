// app/findbar.h
//
// The strip that appears under the tabs on Ctrl+F. Drives aced::Search against
// whichever editor is current and tells it what to paint.
//
// IT LIVES IN app/, NOT IN THE WIDGET. EditorWidget paints the matches and
// knows nothing about where they came from, because "highlight all occurrences
// of the word under the cursor" and a language server's rename preview want the
// same painting and neither is a find bar. The search itself is in aced::Search,
// which has no Qt in it and is tested without one. What is left here is the
// bar: three fields, some toggles, and the rule for what happens when you press
// Enter.
#pragma once

#include <QWidget>
#include <memory>

#include "aced/search.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QToolButton;

namespace acedqt {
class EditorWidget;
}

namespace anyedit {

class FindBar : public QWidget {
    Q_OBJECT

public:
    explicit FindBar(QWidget *parent = nullptr);
    ~FindBar() override;

    // Not owned, and may be null when no tab is open. Set on every tab change:
    // the bar follows the current editor rather than holding one.
    void setEditor(acedqt::EditorWidget *ed);
    acedqt::EditorWidget *editor() const { return editor_; }

    // Shows the bar, focuses the field, and seeds it from the selection if
    // there is one -- selecting a word and pressing Ctrl+F should search for
    // that word, not reopen the last search.
    void activate(bool withReplace);
    bool replaceVisible() const;

    QString searchText() const;
    void setSearchText(const QString &text);
    QString replaceText() const;
    void setReplaceText(const QString &text);

    aced::SearchOptions options() const;

    // Re-runs against the current editor and repaints the highlights. Called
    // when the text changes, when a toggle moves, and when the document under
    // the bar is edited.
    void refresh();

public Q_SLOTS:
    void findNext();
    void findPrevious();
    void replaceCurrent();
    void replaceAll();
    void dismiss();

Q_SIGNALS:
    // Text for the status bar: "3 of 17", "no matches", or a regex error.
    void statusChanged(const QString &text);
    void dismissed();

protected:
    // Esc closes the bar from either field. A QShortcut on the window would
    // also fire while the editor has focus and nothing is open, and a QAction
    // with an Esc shortcut swallows Esc everywhere else in the application.
    void keyPressEvent(QKeyEvent *e) override;

private:
    void updateStatus();
    // The index of the match the cursor is sitting on, or -1.
    int currentIndex() const;

    acedqt::EditorWidget *editor_ = nullptr;
    QLineEdit *find_ = nullptr;
    QLineEdit *replace_ = nullptr;
    QLabel *status_ = nullptr;
    QCheckBox *caseSensitive_ = nullptr;
    QCheckBox *wholeWord_ = nullptr;
    QCheckBox *regex_ = nullptr;
    QWidget *replaceRow_ = nullptr;
    std::vector<aced::Range> matches_;
    QString error_;
};

}  // namespace anyedit
