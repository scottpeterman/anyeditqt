// app/findbar.cpp
#include "app/findbar.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include "aced/document.h"
#include "aced/undo.h"
#include "acedqt/editorwidget.h"

namespace anyedit {

FindBar::FindBar(QWidget *parent) : QWidget(parent) {
    find_ = new QLineEdit(this);
    find_->setPlaceholderText("Find");
    find_->setClearButtonEnabled(true);

    replace_ = new QLineEdit(this);
    replace_->setPlaceholderText("Replace with");
    replace_->setClearButtonEnabled(true);

    caseSensitive_ = new QCheckBox("Aa", this);
    caseSensitive_->setToolTip("Match case");
    wholeWord_ = new QCheckBox("Word", this);
    wholeWord_->setToolTip("Whole word only");
    regex_ = new QCheckBox(".*", this);
    regex_->setToolTip("Regular expression");

    status_ = new QLabel(this);
    status_->setMinimumWidth(90);

    auto *prev = new QToolButton(this);
    prev->setText("\u2191");
    prev->setToolTip("Previous (Shift+F3)");
    auto *next = new QToolButton(this);
    next->setText("\u2193");
    next->setToolTip("Next (F3)");
    auto *close = new QToolButton(this);
    close->setText("\u2715");
    close->setToolTip("Close (Esc)");

    auto *findRow = new QWidget(this);
    auto *fl = new QHBoxLayout(findRow);
    fl->setContentsMargins(4, 2, 4, 2);
    fl->addWidget(find_, 1);
    fl->addWidget(prev);
    fl->addWidget(next);
    fl->addWidget(caseSensitive_);
    fl->addWidget(wholeWord_);
    fl->addWidget(regex_);
    fl->addWidget(status_);
    fl->addWidget(close);

    auto *replaceOne = new QToolButton(this);
    replaceOne->setText("Replace");
    auto *replaceEvery = new QToolButton(this);
    replaceEvery->setText("All");

    replaceRow_ = new QWidget(this);
    auto *rl = new QHBoxLayout(replaceRow_);
    rl->setContentsMargins(4, 0, 4, 2);
    rl->addWidget(replace_, 1);
    rl->addWidget(replaceOne);
    rl->addWidget(replaceEvery);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(findRow);
    outer->addWidget(replaceRow_);
    replaceRow_->hide();

    connect(find_, &QLineEdit::textChanged, this, &FindBar::refresh);
    // returnPressed rather than a default button: a default button inside a
    // QMainWindow steals Enter from the editor once the bar has been closed.
    connect(find_, &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(replace_, &QLineEdit::returnPressed, this, &FindBar::replaceCurrent);
    connect(next, &QToolButton::clicked, this, &FindBar::findNext);
    connect(prev, &QToolButton::clicked, this, &FindBar::findPrevious);
    connect(close, &QToolButton::clicked, this, &FindBar::dismiss);
    connect(replaceOne, &QToolButton::clicked, this, &FindBar::replaceCurrent);
    connect(replaceEvery, &QToolButton::clicked, this, &FindBar::replaceAll);
    for (QCheckBox *c : {caseSensitive_, wholeWord_, regex_})
        connect(c, &QCheckBox::toggled, this, &FindBar::refresh);
}

FindBar::~FindBar() = default;

void FindBar::setEditor(acedqt::EditorWidget *ed) {
    if (editor_ == ed) return;
    // The old editor keeps its highlights otherwise, and they are wrong the
    // moment the bar is pointed somewhere else.
    if (editor_) editor_->clearSearchMatches();
    editor_ = ed;
    if (isVisible()) refresh();
}

bool FindBar::replaceVisible() const { return !replaceRow_->isHidden(); }

QString FindBar::searchText() const { return find_->text(); }
void FindBar::setSearchText(const QString &t) { find_->setText(t); }
QString FindBar::replaceText() const { return replace_->text(); }
void FindBar::setReplaceText(const QString &t) { replace_->setText(t); }

aced::SearchOptions FindBar::options() const {
    aced::SearchOptions o;
    o.caseSensitive = caseSensitive_->isChecked();
    o.wholeWord = wholeWord_->isChecked();
    o.regex = regex_->isChecked();
    o.wrap = true;
    return o;
}

void FindBar::activate(bool withReplace) {
    replaceRow_->setVisible(withReplace);
    // Seeded from the selection, but only a single-line one: a multi-line
    // selection as a search term is never what was meant, and aced::Search
    // cannot match across lines anyway.
    if (editor_ && editor_->hasSelection()) {
        const aced::Range sel = editor_->selection();
        if (sel.start.row == sel.end.row) {
            const QString text =
                QString::fromStdString(editor_->document()->textInRange(sel));
            if (!text.isEmpty()) find_->setText(text);
        }
    }
    show();
    find_->setFocus();
    find_->selectAll();
    refresh();
}

void FindBar::refresh() {
    matches_.clear();
    error_.clear();
    if (!editor_) {
        updateStatus();
        return;
    }
    const QString needle = find_->text();
    if (needle.isEmpty()) {
        editor_->clearSearchMatches();
        updateStatus();
        return;
    }
    const aced::Search s(needle.toStdString(), options());
    if (!s.valid()) {
        // A partly typed regex is invalid on almost every keystroke, so this is
        // shown rather than popped up, and the old highlights go away rather
        // than staying on screen looking current.
        error_ = QString::fromStdString(s.error());
        editor_->clearSearchMatches();
        updateStatus();
        return;
    }
    matches_ = s.all(*editor_->document());
    editor_->setSearchMatches(matches_);
    const int idx = currentIndex();
    if (idx >= 0) editor_->setCurrentSearchMatch(matches_[static_cast<size_t>(idx)]);
    updateStatus();
}

int FindBar::currentIndex() const {
    if (!editor_ || matches_.empty()) return -1;
    // The match the SELECTION covers, not the one nearest the cursor: after a
    // findNext the hit is selected, and that is what "3 of 17" is counting.
    const aced::Range sel = editor_->selection();
    for (size_t i = 0; i < matches_.size(); ++i) {
        if (matches_[i].start == sel.start && matches_[i].end == sel.end)
            return static_cast<int>(i);
    }
    return -1;
}

void FindBar::updateStatus() {
    QString text;
    if (!error_.isEmpty()) {
        text = "bad pattern";
        status_->setToolTip(error_);
    } else if (find_->text().isEmpty()) {
        status_->setToolTip({});
    } else if (matches_.empty()) {
        text = "no matches";
        status_->setToolTip({});
    } else {
        const int idx = currentIndex();
        text = idx >= 0 ? QString("%1 of %2").arg(idx + 1).arg(matches_.size())
                        : QString("%1 matches").arg(matches_.size());
        status_->setToolTip({});
    }
    status_->setText(text);
    Q_EMIT statusChanged(error_.isEmpty() ? text : error_);
}

void FindBar::findNext() {
    if (!editor_ || find_->text().isEmpty()) return;
    const aced::Search s(find_->text().toStdString(), options());
    if (!s.valid()) return;
    // PAST THE SELECTION'S START when there is one, and from the cursor exactly
    // when there is not. Both halves matter and they pull opposite ways:
    // stepping past the current hit is what stops Find Next returning the same
    // match forever, and NOT stepping when there is no selection is what lets a
    // cursor resting at the start of a match find that match rather than the
    // one after it. Doing the +1 unconditionally skips the first hit in the
    // file every time the bar is opened.
    aced::Position from = editor_->cursor();
    if (editor_->hasSelection()) {
        from = editor_->selection().start;
        from.column += 1;
    }
    bool found = false;
    const aced::Range r = s.next(*editor_->document(), from, &found);
    if (!found) {
        updateStatus();
        return;
    }
    editor_->setSelection(r);
    editor_->setCurrentSearchMatch(r);
    editor_->ensureCursorVisible();
    updateStatus();
}

void FindBar::findPrevious() {
    if (!editor_ || find_->text().isEmpty()) return;
    const aced::Search s(find_->text().toStdString(), options());
    if (!s.valid()) return;
    const aced::Position from =
        editor_->hasSelection() ? editor_->selection().start : editor_->cursor();
    bool found = false;
    const aced::Range r = s.previous(*editor_->document(), from, &found);
    if (!found) {
        updateStatus();
        return;
    }
    editor_->setSelection(r);
    editor_->setCurrentSearchMatch(r);
    editor_->ensureCursorVisible();
    updateStatus();
}

void FindBar::replaceCurrent() {
    if (!editor_ || find_->text().isEmpty()) return;
    // Only when the selection IS a match. Replace with the cursor parked
    // somewhere arbitrary would otherwise overwrite whatever it was next to.
    if (currentIndex() < 0) {
        findNext();
        return;
    }
    const aced::Range r = editor_->selection();
    editor_->undo()->mark();
    editor_->document()->replace(r, replace_->text().toStdString());
    editor_->undo()->mark();
    editor_->setCursor({r.start.row,
                        r.start.column
                            + static_cast<int>(replace_->text().toUtf8().size())});
    refresh();
    findNext();
}

void FindBar::replaceAll() {
    if (!editor_ || find_->text().isEmpty()) return;
    const aced::Search s(find_->text().toStdString(), options());
    if (!s.valid()) return;
    // ONE undo step for the whole thing. Marking per replacement would make
    // undoing a 400-match replace-all a 400-press job.
    editor_->undo()->mark();
    const int n = s.replaceAll(*editor_->document(), replace_->text().toStdString());
    editor_->undo()->mark();
    editor_->clearSelection();
    refresh();
    Q_EMIT statusChanged(QString("replaced %1").arg(n));
}

void FindBar::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    QWidget::keyPressEvent(e);
}

void FindBar::dismiss() {
    if (editor_) {
        editor_->clearSearchMatches();
        editor_->setFocus();
    }
    hide();
    Q_EMIT dismissed();
}

}  // namespace anyedit
