// app/findbar.cpp
#include "app/findbar.h"

#include <algorithm>
#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "aced/document.h"
#include "aced/undo.h"
#include "acedqt/editorwidget.h"

namespace anyedit {

namespace {
// Past this many rows a full scan is felt per keystroke (~0.25 s at 1M rows),
// so typing is debounced. Below it, refresh stays immediate.
constexpr int kDebounceRows = 50000;
constexpr int kDebounceMs = 150;
// A replace-all on fewer rows than kDebounceRows stays synchronous; it is over
// before a dialog could be seen. The edit phase yields to the event loop every
// kSliceMs so the dialog repaints and Cancel is clickable.
constexpr int kSliceMs = 30;
constexpr int kPollMs = 50;
}  // namespace

struct FindBar::ReplaceJob {
    QPointer<acedqt::EditorWidget> editor;
    std::unique_ptr<aced::Search> search;
    std::string replacement;
    int rows = 0;

    std::atomic<bool> cancel{false};
    std::atomic<int> rowsScanned{0};
    std::vector<aced::Range> ranges;  // written by the worker only
    QThread *worker = nullptr;

    size_t applied = 0;  // counted from the back of `ranges`
    QProgressDialog *dialog = nullptr;
    QTimer *poll = nullptr;
};

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

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(kDebounceMs);
    connect(refreshTimer_, &QTimer::timeout, this, &FindBar::refresh);
    // textEdited, not textChanged: only the user's typing is debounced.
    // setSearchText() and activate() set the text and refresh themselves.
    connect(find_, &QLineEdit::textEdited, this, &FindBar::scheduleRefresh);
    // Emptying the field is free to refresh and should clear highlights at
    // once, whichever path emptied it (the clear button included).
    connect(find_, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (t.isEmpty()) refresh();
    });
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

FindBar::~FindBar() {
    // The worker holds a pointer to the document. It has to be gone before
    // the editor can be.
    if (job_ && job_->worker) {
        job_->cancel = true;
        job_->worker->wait();
    }
}

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
void FindBar::setSearchText(const QString &t) {
    find_->setText(t);
    refresh();
}

void FindBar::scheduleRefresh() {
    if (job_) return;
    if (editor_ && editor_->document()->lineCount() > kDebounceRows)
        refreshTimer_->start();
    else
        refresh();
}

void FindBar::flushPendingRefresh() {
    if (refreshTimer_->isActive()) {
        refreshTimer_->stop();
        refresh();
    }
}
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
    refreshTimer_->stop();
    if (job_) return;
    matches_.clear();
    total_ = 0;
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
    matches_ = s.all(*editor_->document(), aced::Search::kDefaultLimit, &total_);
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
    // matches_ is in document order, so this is a lookup, not a scan. A
    // selection past the cap has no index and falls through to -1.
    auto it = std::lower_bound(matches_.begin(), matches_.end(), sel.start,
                               [](const aced::Range &r, const aced::Position &p) {
                                   return r.start < p;
                               });
    if (it != matches_.end() && it->start == sel.start && it->end == sel.end)
        return static_cast<int>(it - matches_.begin());
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
        text = idx >= 0 ? QString("%1 of %2").arg(idx + 1).arg(total_)
                        : QString("%1 matches").arg(total_);
        status_->setToolTip({});
    }
    status_->setText(text);
    Q_EMIT statusChanged(error_.isEmpty() ? text : error_);
}

void FindBar::findNext() {
    if (!editor_ || find_->text().isEmpty()) return;
    flushPendingRefresh();
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
    flushPendingRefresh();
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
    flushPendingRefresh();
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
    if (!editor_ || find_->text().isEmpty() || job_) return;
    flushPendingRefresh();
    auto s = std::make_unique<aced::Search>(find_->text().toStdString(), options());
    if (!s->valid()) return;
    std::string replacement = replace_->text().toStdString();
    if (editor_->document()->lineCount() > kDebounceRows) {
        startReplaceJob(std::move(s), std::move(replacement));
        return;
    }
    // ONE undo step for the whole thing. Marking per replacement would make
    // undoing a 400-match replace-all a 400-press job.
    editor_->undo()->mark();
    const int n = s->replaceAll(*editor_->document(), replacement);
    editor_->undo()->mark();
    editor_->clearSelection();
    refresh();
    Q_EMIT statusChanged(QString("replaced %1").arg(n));
}

void FindBar::startReplaceJob(std::unique_ptr<aced::Search> search, std::string replacement) {
    job_ = std::make_unique<ReplaceJob>();
    ReplaceJob *job = job_.get();
    job->editor = editor_;
    job->search = std::move(search);
    job->replacement = std::move(replacement);
    job->rows = editor_->document()->lineCount();

    // Stale ranges would paint at offsets that stop meaning anything after the
    // first edit.
    editor_->clearSearchMatches();
    matches_.clear();
    total_ = 0;

    // WINDOW-MODAL is what makes the worker's read safe: no typing, no paste,
    // no tab switch or close while it runs. Painting still happens, and
    // painting only reads.
    job->dialog = new QProgressDialog(QStringLiteral("Finding matches\u2026"),
                                      QStringLiteral("Cancel"), 0, 1000, window());
    job->dialog->setWindowTitle(QStringLiteral("Replace All"));
    job->dialog->setWindowModality(Qt::WindowModal);
    job->dialog->setMinimumDuration(0);
    job->dialog->setAutoClose(false);
    job->dialog->setAutoReset(false);
    job->dialog->setValue(0);
    connect(job->dialog, &QProgressDialog::canceled, this, [job] { job->cancel = true; });

    job->poll = new QTimer(this);
    job->poll->setInterval(kPollMs);
    connect(job->poll, &QTimer::timeout, this, [job] {
        if (job->dialog->wasCanceled()) job->cancel = true;
        const int rows = std::max(1, job->rows);
        job->dialog->setValue(static_cast<int>(
            static_cast<qint64>(job->rowsScanned.load()) * 1000 / rows));
    });
    job->poll->start();

    const aced::Document *doc = editor_->document();
    job->worker = QThread::create([job, doc] {
        job->ranges = job->search->collect(*doc, job->cancel, &job->rowsScanned);
    });
    connect(job->worker, &QThread::finished, this, &FindBar::onScanFinished,
            Qt::QueuedConnection);
    job->worker->start();
}

void FindBar::onScanFinished() {
    ReplaceJob *job = job_.get();
    if (!job) return;
    job->poll->stop();
    job->worker->wait();
    delete job->worker;
    job->worker = nullptr;

    if (job->cancel || !job->editor) {
        finishReplaceJob(QStringLiteral("replace cancelled"));
        return;
    }
    if (job->ranges.empty()) {
        finishReplaceJob(QStringLiteral("replaced 0"));
        return;
    }
    job->dialog->setLabelText(
        QStringLiteral("Replacing %L1 matches\u2026").arg(job->ranges.size()));
    job->dialog->setMaximum(static_cast<int>(std::min<size_t>(job->ranges.size(), INT32_MAX)));
    job->dialog->setValue(0);
    // One group for the whole job, opened here and closed in finish.
    job->editor->undo()->mark();
    QTimer::singleShot(0, this, &FindBar::applySlice);
}

void FindBar::applySlice() {
    ReplaceJob *job = job_.get();
    if (!job) return;
    if (!job->editor) {
        finishReplaceJob(QStringLiteral("replace cancelled"));
        return;
    }
    if (job->dialog->wasCanceled()) job->cancel = true;
    if (job->cancel) {
        // Roll back what was applied. The group is ours -- it was marked open
        // before the first edit and nothing else can edit under the dialog --
        // and a half-finished replace must not come back through Ctrl+Y.
        if (job->applied > 0) {
            job->dialog->setLabelText(QStringLiteral("Cancelling\u2026"));
            job->editor->undo()->undo();
            job->editor->undo()->clearRedo();
        }
        finishReplaceJob(QStringLiteral("replace cancelled, nothing changed"));
        return;
    }

    // BACK TO FRONT, across slices as within one: an edit only moves text
    // after it, so the ranges still ahead keep the offsets they were found at.
    aced::Document *doc = job->editor->document();
    const size_t n = job->ranges.size();
    QElapsedTimer t;
    t.start();
    while (job->applied < n) {
        doc->replace(job->ranges[n - 1 - job->applied], job->replacement);
        ++job->applied;
        if ((job->applied & 255) == 0 && t.elapsed() >= kSliceMs) break;
    }
    job->dialog->setValue(static_cast<int>(std::min<size_t>(job->applied, INT32_MAX)));

    if (job->applied < n) {
        QTimer::singleShot(0, this, &FindBar::applySlice);
        return;
    }
    job->editor->undo()->mark();
    job->editor->clearSelection();
    finishReplaceJob(QStringLiteral("replaced %L1").arg(n));
}

void FindBar::finishReplaceJob(const QString &status) {
    std::unique_ptr<ReplaceJob> job = std::move(job_);
    if (job->poll) job->poll->deleteLater();
    if (job->dialog) {
        job->dialog->hide();
        job->dialog->deleteLater();
    }
    if (job->editor && job->editor == editor_) refresh();
    Q_EMIT statusChanged(status);
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