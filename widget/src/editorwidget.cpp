// src/editorwidget.cpp
#include "acedqt/editorwidget.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <cstdio>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <cmath>

#include "aced/anchor.h"
#include "aced/document.h"
#include "aced/foldmode.h"
#include "aced/linemap.h"
#include "aced/tokenchain.h"
#include "aced/grammar.h"
#include "aced/tokenizer.h"
#include "aced/undo.h"
#include "acedqt/linecache.h"

namespace acedqt {

using aced::Position;
using aced::Range;

namespace {
// A word, for double-click and for Ctrl+arrow. Deliberately not the tokenizer's
// idea of a word: the tokenizer answers "what colour", which is a different
// question and gives the wrong answer here (it would treat `foo.bar` as three
// tokens and a comment as one).
bool isWordByte(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
           || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}
// Document rows to screen lines, for wrapping.
//
// THE COUNT OF A ROW THAT HAS NEVER BEEN LAID OUT IS ASSUMED TO BE 1 and is
// corrected the first time that row is painted. That assumption is the whole
// design: the honest alternative -- lay out all 50,000 rows so the scrollbar
// range is exact -- is the cost LineCache exists to avoid, and it would be paid
// on every edit, not once. The visible consequence is that the scrollbar range
// grows as wrapped rows are discovered, so the thumb shifts slightly while
// scrolling through a file with long lines in it. Every editor that wraps
// lazily does this.

}  // namespace

struct EditorWidget::Impl {
    aced::Document doc;
    std::unique_ptr<aced::UndoManager> undo;
    LineCache cache;
    Palette palette;

    const aced::Grammar *grammar = nullptr;
    QString modeName;

    // Folding. The chain is a second tokenizer state walk, separate from the
    // one in LineCache, because LineCache keeps end states and throws the
    // tokens away. It stays cold: the gutter decides whether a row has a fold
    // marker from the line text alone, and tokens are read only when a range
    // is computed, which is on a click.
    aced::TokenChain chain;
    std::unique_ptr<aced::DocumentFoldSource> foldSrc;
    const aced::FoldMode *foldMode = nullptr;

    Position cursor{0, 0};
    Position anchor{0, 0};   // == cursor when there is no selection
    bool showGutter = true;
    bool insertSpaces = true;
    bool highlightCurrentLine = true;
    bool softWrap = false;
    // Sorted by (row, column), because that is what all() returns and what the
    // binary search below relies on.
    std::vector<Range> searchMatches;
    Range currentMatch{{-1, -1}, {-1, -1}};
    int wrapColumn = 0;  // 0 = the viewport's width
    // Rows to screen lines. Heights for wrapping, visibility for folding, one
    // table -- see aced/linemap.h. Identity and allocation-free while wrap is
    // off and nothing is folded, which is the common case.
    aced::LineMap map;
    bool modified = false;
    // A press on a fold marker is not the start of a text drag. Without this
    // the next mouse-move with the button down selects from wherever the
    // cursor happened to be to wherever the pointer went.
    bool dragging = false;
    // Last shape handed to the viewport. setCursor() on every mouse-move would
    // be a round trip to the window system per pixel.
    Qt::CursorShape hoverShape = Qt::IBeamCursor;
    bool cursorVisible = true;
    int tabWidth = 4;
    // Column the cursor "wants" when moving vertically, so travelling down
    // through a short line and out the other side returns to where it started.
    // -1 means "take it from the current column".
    int goalX = -1;
    QTimer blink;

    // Matches on one row, as a half-open index span into searchMatches.
    // BINARY SEARCH, NOT A SCAN. all() over a large file returns thousands of
    // ranges and the paint loop runs per row per frame; scanning the whole list
    // for every visible row is how highlighting a common word makes scrolling
    // crawl.
    std::pair<size_t, size_t> matchesOnRow(int row) const {
        const auto lo = std::lower_bound(
            searchMatches.begin(), searchMatches.end(), row,
            [](const Range &r, int v) { return r.start.row < v; });
        const auto hi = std::upper_bound(
            lo, searchMatches.end(), row,
            [](int v, const Range &r) { return v < r.start.row; });
        return {static_cast<size_t>(lo - searchMatches.begin()),
                static_cast<size_t>(hi - searchMatches.begin())};
    }

    int gutterWidth(int lineCount, qreal charW) const {
        if (!showGutter) return 0;
        int digits = 1;
        for (int n = lineCount; n >= 10; n /= 10) ++digits;
        return static_cast<int>(charW * (digits + 3));
    }

    // The fold markers live in the padding the gutter already had to the right
    // of the number, so turning folding on does not move the text. drawText()
    // stops at gutter - charW*1.5, which is where this starts.
    qreal foldColumnLeft(int gutter, qreal charW) const {
        return gutter - charW * 1.5;
    }

    bool hasFoldWidget(int row) const {
        if (!foldMode || !foldSrc) return false;
        return foldMode->widget(*foldSrc, row) == aced::FoldWidget::Start;
    }

    // Last row of the hidden run that starts immediately after `header`.
    int hiddenRunEnd(int header) const {
        int b = header;
        while (b + 1 < map.rowCount() && !map.visible(b + 1)) ++b;
        return b;
    }
};

// A caret move that nothing is told about is a caret move that did not happen,
// as far as a status bar is concerned. Find Next moved it through
// setSelection(), which emitted selectionChanged() and nothing else, so the
// position readout kept whatever it said before the search. Rather than
// remembering the signal in each of the twenty-odd branches that can move it,
// every entry point declares one of these.
struct EditorWidget::CursorWatch {
    EditorWidget *w;
    Position before;
    explicit CursorWatch(EditorWidget *widget)
        : w(widget), before(widget->d_->cursor) {}
    ~CursorWatch() { w->emitIfCursorMoved(before); }
    CursorWatch(const CursorWatch &) = delete;
    CursorWatch &operator=(const CursorWatch &) = delete;
};

void EditorWidget::emitIfCursorMoved(Position before) {
    if (d_->cursor == before) return;
    Q_EMIT cursorMoved(d_->cursor.row, d_->cursor.column);
}

EditorWidget::EditorWidget(QWidget *parent)
    : QAbstractScrollArea(parent), d_(new Impl) {
    d_->undo = std::make_unique<aced::UndoManager>(&d_->doc);
    d_->cache.setDocument(&d_->doc);
    d_->cache.setPalette(d_->palette);
    // An empty Document is one row, not zero. Without this the map is short by
    // one for the whole life of a widget that is never given setText() -- and
    // the only visible symptom is a scrollbar maximum one too small, which
    // looks like an off-by-one in the scrollbar rather than an uninitialised
    // map.
    d_->map.reset(d_->doc.lineCount());
    d_->chain.setDocument(&d_->doc);
    d_->foldSrc = std::make_unique<aced::DocumentFoldSource>(&d_->doc, &d_->chain);

    // One listener, doing the two things every edit implies: drop the cached
    // layouts from the edited row down, and remember that the buffer is dirty.
    d_->doc.addListener([this](const aced::Delta &delta) {
        d_->cache.invalidateFrom(delta.start.row);
        d_->chain.invalidateFrom(delta.start.row);
        // An edit inside a collapsed fold has to open it. The alternative is a
        // buffer whose visible text no longer matches what was typed, which is
        // the one failure mode a folding editor cannot have. Undo of a delete
        // that spanned a fold is the ordinary way to get here.
        if (!d_->map.visible(delta.start.row)) revealRow(delta.start.row);
        // Move the rows the edit added or removed, and leave every other
        // height alone. A row's wrapped height depends on its own text and the
        // wrap width, not on the rows above it -- only its COLOURS depend on
        // those, and colours are the cache's problem. The predecessor reset
        // every height below the edit to a guess, so the scrollbar shrank on
        // each keystroke and grew back as rows repainted.
        const int rowsMoved = static_cast<int>(delta.lines.size()) - 1;
        if (rowsMoved > 0) {
            if (delta.action == aced::Delta::Action::Insert)
                d_->map.insertRows(delta.start.row + 1, rowsMoved);
            else
                d_->map.removeRows(delta.start.row + 1, rowsMoved);
        }
        // The document just changed size. Without this the scrollbar keeps the
        // range it had when the widget was last resized.
        updateScrollRanges();
        if (!d_->modified) {
            d_->modified = true;
            Q_EMIT modifiedChanged(true);
        }
        Q_EMIT textChanged();
    });

    setViewportMargins(0, 0, 0, 0);
    viewport()->setCursor(Qt::IBeamCursor);
    // Without this, mouse-move is only delivered while a button is down, and
    // the gutter never gets a chance to change the pointer.
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    viewport()->setAutoFillBackground(false);

    d_->blink.setInterval(QApplication::cursorFlashTime() / 2);
    connect(&d_->blink, &QTimer::timeout, this, [this] {
        d_->cursorVisible = !d_->cursorVisible;
        viewport()->update();
    });
}

EditorWidget::~EditorWidget() = default;

aced::Document *EditorWidget::document() const { return &d_->doc; }
aced::UndoManager *EditorWidget::undo() const { return d_->undo.get(); }

QString EditorWidget::text() const { return QString::fromStdString(d_->doc.text()); }

void EditorWidget::setText(const QString &text) {
    d_->doc.setText(text.toStdString());
    d_->undo->clear();
    d_->cache.invalidateAll();
    d_->chain.invalidateAll();
    d_->map.reset(d_->doc.lineCount());
    d_->cursor = d_->anchor = Position{0, 0};
    d_->modified = false;
    Q_EMIT modifiedChanged(false);
    updateGeometry();
    resizeEvent(nullptr);
    viewport()->update();
}

bool EditorWidget::openFile(const QString &path, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    setText(QString::fromUtf8(f.readAll()));
    const std::string m = aced::Grammar::modeForFilename(path.toStdString());
    setMode(QString::fromStdString(m));
    return true;
}

bool EditorWidget::saveFile(const QString &path, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QByteArray bytes = QByteArray::fromStdString(d_->doc.text());
    if (f.write(bytes) != bytes.size()) {
        if (error) *error = f.errorString();
        return false;
    }
    setModified(false);
    return true;
}

void EditorWidget::setGrammar(const aced::Grammar *grammar) {
    d_->grammar = grammar;
    setMode(d_->modeName);
}

void EditorWidget::setMode(const QString &mode) {
    d_->modeName = mode;
    const aced::Tokenizer *tk = nullptr;
    if (d_->grammar && !mode.isEmpty()) tk = d_->grammar->tokenizer(mode.toStdString());
    d_->cache.setTokenizer(tk);
    d_->chain.setTokenizer(tk);
    d_->foldMode = aced::foldModeFor(mode.toStdString());
    if (!d_->foldMode && d_->map.anyHidden()) {
        // Switching to a mode that cannot fold must not leave rows hidden with
        // no marker to bring them back.
        d_->map.showAll();
        updateScrollRanges();
        Q_EMIT foldsChanged();
    }
    viewport()->update();
}

QString EditorWidget::mode() const { return d_->modeName; }

void EditorWidget::setEditorFont(const QFont &font) {
    d_->cache.setFont(font);
    resizeEvent(nullptr);
    viewport()->update();
}

QFont EditorWidget::editorFont() const { return d_->cache.font(); }

void EditorWidget::setEditorPalette(const Palette &p) {
    d_->palette = p;
    d_->cache.setPalette(p);
    viewport()->update();
}

void EditorWidget::setShowLineNumbers(bool on) {
    d_->showGutter = on;
    viewport()->update();
}

void EditorWidget::setTabWidth(int columns) {
    d_->tabWidth = columns > 0 ? columns : 4;
    d_->cache.setTabWidth(d_->tabWidth);
    viewport()->update();
}

void EditorWidget::setInsertSpaces(bool on) { d_->insertSpaces = on; }

void EditorWidget::setSoftWrap(bool on) {
    if (d_->softWrap == on) return;
    d_->softWrap = on;
    if (on) {
        d_->map.forgetHeights();
    } else {
        d_->cache.setWrapWidth(0);
        // Back to one screen line per row. forgetHeights() keeps folds.
        d_->map.forgetHeights();
    }
    // The horizontal scrollbar is meaningless while wrapping and leaving it
    // visible-but-dead is worse than hiding it.
    setHorizontalScrollBarPolicy(on ? Qt::ScrollBarAlwaysOff
                                    : Qt::ScrollBarAsNeeded);
    if (on) horizontalScrollBar()->setValue(0);
    resizeEvent(nullptr);
    ensureCursorVisible();
    viewport()->update();
}

bool EditorWidget::softWrap() const { return d_->softWrap; }

void EditorWidget::setWrapColumn(int columns) {
    if (columns < 0) columns = 0;
    if (d_->wrapColumn == columns) return;
    d_->wrapColumn = columns;
    if (d_->softWrap) {
        resizeEvent(nullptr);
        ensureCursorVisible();
        viewport()->update();
    }
}

int EditorWidget::wrapColumn() const { return d_->wrapColumn; }

void EditorWidget::setHighlightCurrentLine(bool on) {
    d_->highlightCurrentLine = on;
    viewport()->update();
}

// --- selection -------------------------------------------------------------

bool EditorWidget::hasSelection() const { return d_->cursor != d_->anchor; }

Range EditorWidget::selection() const {
    Position a = d_->anchor, b = d_->cursor;
    if (b < a) std::swap(a, b);
    return {a, b};
}

void EditorWidget::setSelection(const Range &r) {
    CursorWatch watch(this);
    const bool had = hasSelection();
    d_->anchor = d_->doc.clamp(r.start);
    d_->cursor = d_->doc.clamp(r.end);
    if (had != hasSelection()) Q_EMIT selectionChanged(hasSelection());
    ensureCursorVisible();
    viewport()->update();
}

// The rows a block operation applies to.
//
// A SELECTION ENDING AT COLUMN 0 DOES NOT INCLUDE THAT ROW. Dragging down to
// the start of the next line is how everyone selects "these three lines", and
// counting the row the cursor landed on would indent a fourth line that has
// nothing selected on it.
static std::pair<int, int> blockRows(const Range &sel, bool haveSelection) {
    if (!haveSelection) return {sel.start.row, sel.start.row};
    int last = sel.end.row;
    if (sel.end.column == 0 && last > sel.start.row) --last;
    return {sel.start.row, last};
}

void EditorWidget::indentSelection() {
    CursorWatch watch(this);
    const bool had = hasSelection();
    const Range sel = selection();
    const auto rows = blockRows(sel, had);
    const std::string unit =
        d_->insertSpaces ? std::string(d_->tabWidth, ' ') : std::string("\t");
    const int width = static_cast<int>(unit.size());

    // ONE undo step for the whole block. A mark per line would make undoing a
    // forty-line indent a forty-press job.
    d_->undo->mark();
    // Front to back is safe here: inserting at the start of a row shifts no
    // other row's columns and adds no lines, so the row indices stay put. That
    // is not true of unindent, which is why only one of the two says so.
    for (int row = rows.first; row <= rows.second; ++row)
        d_->doc.insert({row, 0}, unit);
    d_->undo->mark();

    auto shift = [&](Position p) {
        if (p.row >= rows.first && p.row <= rows.second) p.column += width;
        return p;
    };
    if (had) {
        d_->anchor = d_->doc.clamp(shift(sel.start));
        d_->cursor = d_->doc.clamp(shift(sel.end));
    } else {
        d_->cursor = d_->anchor = d_->doc.clamp(shift(sel.start));
    }
    Q_EMIT cursorMoved(d_->cursor.row, d_->cursor.column);
    ensureCursorVisible();
    viewport()->update();
}

void EditorWidget::unindentSelection() {
    CursorWatch watch(this);
    const bool had = hasSelection();
    const Range sel = selection();
    const auto rows = blockRows(sel, had);

    d_->undo->mark();
    // How much came off each row, so the selection can be put back accurately
    // -- rows differ, because a line indented with two spaces loses two and its
    // neighbour indented with eight loses a full tab width.
    std::vector<int> removed(static_cast<size_t>(rows.second - rows.first + 1), 0);
    for (int row = rows.first; row <= rows.second; ++row) {
        const std::string &line = d_->doc.line(row);
        int n = 0;
        if (!line.empty() && line[0] == '\t') {
            // ONE tab, not tabWidth of them. A tab is one indent level however
            // wide it is drawn.
            n = 1;
        } else {
            while (n < d_->tabWidth && n < static_cast<int>(line.size())
                   && line[static_cast<size_t>(n)] == ' ')
                ++n;
        }
        if (n > 0) d_->doc.remove({{row, 0}, {row, n}});
        removed[static_cast<size_t>(row - rows.first)] = n;
    }
    d_->undo->mark();

    auto shift = [&](Position p) {
        if (p.row < rows.first || p.row > rows.second) return p;
        const int n = removed[static_cast<size_t>(p.row - rows.first)];
        p.column = std::max(0, p.column - n);
        return p;
    };
    if (had) {
        d_->anchor = d_->doc.clamp(shift(sel.start));
        d_->cursor = d_->doc.clamp(shift(sel.end));
    } else {
        d_->cursor = d_->anchor = d_->doc.clamp(shift(sel.start));
    }
    Q_EMIT cursorMoved(d_->cursor.row, d_->cursor.column);
    ensureCursorVisible();
    viewport()->update();
}

void EditorWidget::selectAll() { setSelection({{0, 0}, d_->doc.end()}); }

void EditorWidget::clearSelection() {
    if (!hasSelection()) return;
    d_->anchor = d_->cursor;
    Q_EMIT selectionChanged(false);
    viewport()->update();
}

Position EditorWidget::cursor() const { return d_->cursor; }

void EditorWidget::setCursor(Position p) {
    const bool had = hasSelection();
    d_->cursor = d_->anchor = d_->doc.clamp(p);
    d_->goalX = -1;
    if (had) Q_EMIT selectionChanged(false);
    Q_EMIT cursorMoved(d_->cursor.row, d_->cursor.column);
    ensureCursorVisible();
    viewport()->update();
}

bool EditorWidget::isModified() const { return d_->modified; }

void EditorWidget::setModified(bool m) {
    if (d_->modified == m) return;
    d_->modified = m;
    Q_EMIT modifiedChanged(m);
}

// --- clipboard -------------------------------------------------------------

void EditorWidget::setSearchMatches(const std::vector<Range> &matches) {
    d_->searchMatches = matches;
    viewport()->update();
}

void EditorWidget::setCurrentSearchMatch(const Range &r) {
    d_->currentMatch = r;
    viewport()->update();
}

void EditorWidget::clearSearchMatches() {
    d_->searchMatches.clear();
    d_->currentMatch = Range{{-1, -1}, {-1, -1}};
    viewport()->update();
}

int EditorWidget::searchMatchCount() const {
    return static_cast<int>(d_->searchMatches.size());
}

void EditorWidget::copy() {
    if (!hasSelection()) return;
    QApplication::clipboard()->setText(
        QString::fromStdString(d_->doc.textInRange(selection())));
}

void EditorWidget::cut() {
    CursorWatch watch(this);
    if (!hasSelection()) return;
    copy();
    d_->undo->mark();
    const Range r = selection();
    d_->doc.remove(r);
    setCursor(r.start);
    d_->undo->mark();
}

void EditorWidget::paste() {
    CursorWatch watch(this);
    const QString clip = QApplication::clipboard()->text();
    if (clip.isEmpty()) return;
    d_->undo->mark();
    if (hasSelection()) {
        const Range r = selection();
        d_->doc.remove(r);
        d_->cursor = d_->anchor = r.start;
    }
    // Anchored so the cursor lands after the pasted text without this having to
    // work out where that is -- which for a multi-line paste is exactly the
    // arithmetic Anchor exists to stop us writing twice.
    aced::Anchor at(&d_->doc, d_->cursor, true);
    d_->doc.insert(d_->cursor, clip.toStdString());
    setCursor(at.position());
    d_->undo->mark();
}


// --- folding ---------------------------------------------------------------

bool EditorWidget::canUndo() const { return d_->undo->canUndo(); }
bool EditorWidget::canRedo() const { return d_->undo->canRedo(); }

bool EditorWidget::undoEdit() {
    if (!d_->undo->undo()) return false;
    d_->cache.invalidateAll();
    setCursor(d_->undo->lastTouched());
    return true;
}

bool EditorWidget::redoEdit() {
    if (!d_->undo->redo()) return false;
    d_->cache.invalidateAll();
    setCursor(d_->undo->lastTouched());
    return true;
}

bool EditorWidget::canFold(int row) const { return d_->hasFoldWidget(row); }

bool EditorWidget::isFolded(int row) const {
    if (row < 0 || row >= d_->doc.lineCount()) return false;
    return !d_->map.expanded(row);
}

bool EditorWidget::foldRow(int row) {
    if (!d_->foldMode || row < 0 || row >= d_->doc.lineCount()) return false;
    if (!d_->map.expanded(row)) return false;  // already folded
    aced::FoldRange fr;
    if (!d_->foldMode->range(*d_->foldSrc, row, &fr)) return false;
    if (!fr.multiLine()) return false;

    d_->map.setVisible(row + 1, std::min(fr.endRow, d_->doc.lineCount() - 1), false);
    d_->map.setExpanded(row, false);
    rescueCursor(row);
    updateScrollRanges();
    viewport()->update();
    Q_EMIT foldsChanged();
    return true;
}

bool EditorWidget::unfoldRow(int row) {
    if (row < 0 || row >= d_->doc.lineCount()) return false;
    if (d_->map.expanded(row)) return false;

    const int end = d_->hiddenRunEnd(row);
    d_->map.setExpanded(row, true);
    if (end > row) {
        d_->map.setVisible(row + 1, end, true);
        // Put back the folds that were already shut inside this one. Their
        // ranges come from the document text, which the fold did not change,
        // so they can simply be recomputed -- no stored ranges to keep in sync
        // with edits.
        for (int r = row + 1; r <= end; ++r) {
            if (d_->map.expanded(r)) continue;
            aced::FoldRange fr;
            if (!d_->foldMode || !d_->foldMode->range(*d_->foldSrc, r, &fr) ||
                !fr.multiLine()) {
                // The edit that happened while this was hidden took its fold
                // away. Leaving it marked contracted would show a closed
                // marker over visible rows.
                d_->map.setExpanded(r, true);
                continue;
            }
            const int inner = std::min(fr.endRow, end);
            if (inner > r) d_->map.setVisible(r + 1, inner, false);
            r = inner;
        }
    }
    updateScrollRanges();
    viewport()->update();
    Q_EMIT foldsChanged();
    return true;
}

bool EditorWidget::foldEnclosing() {
    if (!d_->foldMode) return false;
    // Nearest header above the cursor whose range actually contains it. Walking
    // up rather than scanning the file keeps this proportional to nesting
    // depth, not to document size.
    for (int r = d_->cursor.row - 1; r >= 0; --r) {
        if (!d_->hasFoldWidget(r)) continue;
        aced::FoldRange fr;
        if (!d_->foldMode->range(*d_->foldSrc, r, &fr)) continue;
        if (fr.endRow < d_->cursor.row) continue;
        return foldRow(r);
    }
    return false;
}

bool EditorWidget::toggleFold(int row) {
    return isFolded(row) ? unfoldRow(row) : foldRow(row);
}

void EditorWidget::foldAll() {
    if (!d_->foldMode) return;
    // Top down. An inner fold set while its outer one is already closed simply
    // hides rows that are hidden, which LineMap treats as a no-op, and leaves
    // the inner header marked contracted -- which is what "fold all" means.
    for (int r = 0; r < d_->doc.lineCount(); ++r) {
        if (!d_->map.expanded(r)) continue;
        if (!d_->hasFoldWidget(r)) continue;
        aced::FoldRange fr;
        if (!d_->foldMode->range(*d_->foldSrc, r, &fr) || !fr.multiLine()) continue;
        d_->map.setVisible(r + 1, std::min(fr.endRow, d_->doc.lineCount() - 1), false);
        d_->map.setExpanded(r, false);
    }
    rescueCursor(-1);
    updateScrollRanges();
    viewport()->update();
    Q_EMIT foldsChanged();
}

void EditorWidget::unfoldAll() {
    if (!d_->map.anyHidden() && d_->map.contractedNext(0) < 0) return;
    d_->map.showAll();
    updateScrollRanges();
    viewport()->update();
    Q_EMIT foldsChanged();
}

void EditorWidget::revealRow(int row) {
    if (row < 0 || row >= d_->doc.lineCount()) return;
    if (d_->map.visible(row)) return;
    int a = row;
    while (a > 0 && !d_->map.visible(a - 1)) --a;
    const int b = d_->hiddenRunEnd(row);
    d_->map.setVisible(a, b, true);
    // Everything in the run is visible now, so nothing in it may still claim
    // to be contracted -- including the header above it.
    if (a > 0) d_->map.setExpanded(a - 1, true);
    for (int r = a; r <= b; ++r) d_->map.setExpanded(r, true);
    updateScrollRanges();
    viewport()->update();
    Q_EMIT foldsChanged();
}

int EditorWidget::foldMarkerRowAt(const QPointF &pt) const {
    if (!d_->foldMode || !d_->showGutter) return -1;
    const qreal lh = d_->cache.lineHeight();
    if (lh <= 0) return -1;
    const int gutter = d_->gutterWidth(d_->doc.lineCount(), d_->cache.charWidth());
    if (gutter <= 0) return -1;
    const qreal left = d_->foldColumnLeft(gutter, d_->cache.charWidth());
    if (pt.x() < left || pt.x() >= gutter) return -1;

    const int line = verticalScrollBar()->value() +
                     static_cast<int>(std::floor(pt.y() / lh));
    if (line < 0) return -1;
    int sub = 0;
    const int row = d_->map.rowFromDisplay(line, &sub);
    // Only against the row's first screen line, which is where the marker is
    // drawn. A click beside a continuation line is not a click on a marker.
    if (sub != 0) return -1;
    return d_->hasFoldWidget(row) ? row : -1;
}

// The cursor cannot sit on a row nobody can see. `preferred` is the fold header
// that was just closed, which is where it should land.
void EditorWidget::rescueCursor(int preferred) {
    if (d_->map.visible(d_->cursor.row)) return;
    int row = preferred;
    if (row < 0 || !d_->map.visible(row)) row = d_->map.prevVisible(d_->cursor.row);
    if (row < 0) row = d_->map.nextVisible(d_->cursor.row);
    if (row < 0) return;
    d_->cursor = d_->anchor = d_->doc.clamp({row, d_->cursor.column});
    Q_EMIT cursorMoved(d_->cursor.row, d_->cursor.column);
    Q_EMIT selectionChanged(false);
}

// --- viewport --------------------------------------------------------------

aced::Position EditorWidget::positionForPoint(const QPointF &pt) const {
    return positionAt(pt);
}

int EditorWidget::firstVisibleRow() const {
    return d_->map.rowFromDisplay(verticalScrollBar()->value(), nullptr);
}

int EditorWidget::visibleRowCount() const {
    const qreal h = d_->cache.lineHeight();
    return h > 0 ? std::max(1, static_cast<int>(viewport()->height() / h)) : 1;
}

qreal EditorWidget::lineHeight() const { return d_->cache.lineHeight(); }

int EditorWidget::screenLineCount() const {
    return d_->map.displayLineCount();
}

int EditorWidget::screenLineForRow(int row) const {
    return d_->map.displayFromRow(row);
}

void EditorWidget::scrollToRow(int row) {
    verticalScrollBar()->setValue(screenLineForRow(row));
}

// The screen line the cursor sits on, and which sub-line of its row that is.
// One place, because four callers used to each recompute "row - first" and only
// three of them would have been fixed.
int EditorWidget::cursorScreenLine(int *subLine) const {
    int sub = 0;
    if (d_->softWrap) d_->cache.xForColumn(d_->cursor.row, d_->cursor.column, &sub);
    if (subLine) *subLine = sub;
    return screenLineForRow(d_->cursor.row) + sub;
}

// Pixels available for text, gutter and caret column excluded. Also what the
// wrap width is set from, which is why it is not inlined at each use.
qreal EditorWidget::textWidth() const {
    const qreal gutter = d_->gutterWidth(d_->doc.lineCount(), d_->cache.charWidth());
    return std::max<qreal>(d_->cache.charWidth() * 4,
                           viewport()->width() - gutter - d_->cache.charWidth());
}

void EditorWidget::ensureCursorVisible() {
    const int first = verticalScrollBar()->value();
    const int visible = visibleRowCount();
    const int line = cursorScreenLine(nullptr);
    if (line < first)
        verticalScrollBar()->setValue(line);
    else if (line >= first + visible)
        verticalScrollBar()->setValue(line - visible + 1);

    if (d_->softWrap) return;  // no horizontal scrolling to do

    const qreal x = d_->cache.xForColumn(d_->cursor.row, d_->cursor.column);
    QScrollBar *hb = horizontalScrollBar();
    const int vw = viewport()->width() - d_->gutterWidth(d_->doc.lineCount(), d_->cache.charWidth());
    if (x < hb->value())
        hb->setValue(static_cast<int>(x));
    else if (x > hb->value() + vw - d_->cache.charWidth())
        hb->setValue(static_cast<int>(x - vw + d_->cache.charWidth() * 2));
}

// Scrollbar ranges, from the document's current size.
//
// CALLED ON EVERY EDIT, not only on resize. It used to live inside
// resizeEvent() alone, so inserting text never widened the vertical range: a
// paste of a 500-line file landed in the document intact and the scrollbar
// stayed at maximum 0, which meant the first screenful was all you could ever
// reach. The text was there. There was no way to scroll to it, and it read as
// a paste that had been truncated.
//
// Soft wrap hid this, because paintEvent recomputes the range itself when
// wrapping, so the bug only ever showed with wrap off -- which is the default.
void EditorWidget::updateScrollRanges() {
    if (d_->softWrap) {
        // Set BEFORE the scrollbar range is computed: changing it drops every
        // layout, which makes every count in the wrap map a guess again, and a
        // range computed from the old counts would be wrong for exactly one
        // paint -- long enough to see the scrollbar jump on every resize.
        const qreal want = d_->wrapColumn > 0
                               ? d_->wrapColumn * d_->cache.charWidth()
                               : textWidth();
        const qreal had = d_->cache.wrapWidth();
        d_->cache.setWrapWidth(want);
        if (d_->cache.wrapWidth() != had) d_->map.forgetHeights();
    }

    const int visible = visibleRowCount();
    verticalScrollBar()->setRange(0, std::max(0, screenLineCount() - visible));
    verticalScrollBar()->setPageStep(visible);
    verticalScrollBar()->setSingleStep(1);

    const int vw = viewport()->width() - d_->gutterWidth(d_->doc.lineCount(), d_->cache.charWidth());
    if (d_->softWrap) {
        horizontalScrollBar()->setRange(0, 0);
    } else {
        const int wide = static_cast<int>(d_->cache.maxWidthSeen()) - vw;
        horizontalScrollBar()->setRange(0, std::max(0, wide + 16));
        horizontalScrollBar()->setPageStep(vw);
        horizontalScrollBar()->setSingleStep(static_cast<int>(d_->cache.charWidth()));
    }
}

void EditorWidget::resizeEvent(QResizeEvent *event) {
    if (event) QAbstractScrollArea::resizeEvent(event);
    updateScrollRanges();
}

// --- painting --------------------------------------------------------------

bool EditorWidget::event(QEvent *e) {
    if (e->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(e);
        if ((k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab)
            && !(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            keyPressEvent(k);
            return true;
        }
    }
    return QAbstractScrollArea::event(e);
}

void EditorWidget::paintEvent(QPaintEvent *) {
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), d_->palette.background());

    const qreal lh = d_->cache.lineHeight();
    const int gutter = d_->gutterWidth(d_->doc.lineCount(), d_->cache.charWidth());
    const qreal xoff = gutter - (d_->softWrap ? 0 : horizontalScrollBar()->value());
    const int topLine = verticalScrollBar()->value();
    const int visible = visibleRowCount() + 1;  // one extra, partially shown

    if (gutter > 0)
        p.fillRect(QRectF(0, 0, gutter, viewport()->height()), d_->palette.gutterBackground());

    const Range sel = selection();
    const bool haveSel = hasSelection();

    // Where the walk starts. Without wrapping the top screen line IS the row;
    // with it, the row and how far into that row we begin both come out of the
    // map, and the first row drawn may be partly above the viewport.
    int row = 0, skip = 0;
    row = d_->map.rowFromDisplay(topLine, &skip);

    int drawn = 0;
    for (; row < d_->doc.lineCount() && drawn < visible; ++row, skip = 0) {
        // A folded-away row is laid out by nobody and painted by nobody. Its
        // height in the map stays whatever it last measured, which is what
        // makes unfolding restore the scrollbar without a re-measure.
        if (!d_->map.visible(row)) continue;
        QTextLayout *tl = d_->cache.layout(row);
        if (!tl) continue;
        const int subCount = std::max(1, tl->lineCount());
        // The row has now been laid out, so its real height is known. This is
        // the only place the map ever learns a count.
        d_->map.setHeight(row, subCount);

        for (int sub = skip; sub < subCount && drawn < visible; ++sub, ++drawn) {
            const qreal y = drawn * lh;
            const QTextLine ql = tl->lineAt(sub);
            const int lineStartCol = d_->cache.columnAtSubLineStart(row, sub);
            const int lineEndCol = d_->cache.columnAtSubLineEnd(row, sub);

            if (d_->highlightCurrentLine && row == d_->cursor.row && !haveSel)
                p.fillRect(QRectF(gutter, y, viewport()->width() - gutter, lh),
                           d_->palette.currentLineColor());

            // Bands for the OTHER matches go under the selection; the band for
            // the current one goes over it, further down. Find Next selects the
            // hit it moves to, so a current-match colour painted underneath is
            // covered by the selection every time and the active hit looks
            // exactly like an ordinary selection.
            auto bandX = [&](int a, int b, qreal *x1, qreal *x2) {
                *x1 = ql.cursorToX(
                    QString::fromUtf8(d_->doc.line(row).c_str(), a).size());
                *x2 = ql.cursorToX(
                    QString::fromUtf8(d_->doc.line(row).c_str(), b).size());
            };
            auto isCurrent = [&](const Range &m) {
                return m.start == d_->currentMatch.start
                       && m.end == d_->currentMatch.end;
            };
            if (!d_->searchMatches.empty()) {
                const auto span = d_->matchesOnRow(row);
                for (size_t i = span.first; i < span.second; ++i) {
                    const Range &m = d_->searchMatches[i];
                    if (isCurrent(m)) continue;
                    const int a = std::max(lineStartCol, m.start.column);
                    const int b = std::min(lineEndCol, m.end.column);
                    if (b <= a) continue;
                    qreal x1 = 0, x2 = 0;
                    bandX(a, b, &x1, &x2);
                    p.fillRect(QRectF(xoff + x1, y, std::max<qreal>(x2 - x1, 1), lh),
                               d_->palette.searchMatchColor());
                }
            }

            if (haveSel && row >= sel.start.row && row <= sel.end.row) {
                // Clipped to this sub-line, so a selection across a wrapped row
                // paints a band per screen line instead of one impossible
                // rectangle spanning all of them.
                const int a = std::max(lineStartCol,
                                       (row == sel.start.row) ? sel.start.column : 0);
                const int b = std::min(lineEndCol, (row == sel.end.row)
                                                       ? sel.end.column
                                                       : d_->doc.lineLength(row));
                if (b >= a) {
                    const qreal x1 = ql.cursorToX(
                        QString::fromUtf8(d_->doc.line(row).c_str(), a).size());
                    const qreal x2 = ql.cursorToX(
                        QString::fromUtf8(d_->doc.line(row).c_str(), b).size());
                    qreal right = x2;
                    // A selection running through the end of a line shows the
                    // newline as selected; through a WRAP point it does not --
                    // there is no newline there, and a trailing block would
                    // claim there was.
                    const bool endsRow = (sub == subCount - 1);
                    if (row < sel.end.row && endsRow) right += d_->cache.charWidth();
                    p.fillRect(QRectF(xoff + x1, y, std::max<qreal>(right - x1, 1), lh),
                               d_->palette.selectionColor());
                }
            }

            // The current match, over the selection. See the note above.
            if (!d_->searchMatches.empty() && d_->currentMatch.start.row == row) {
                const int a = std::max(lineStartCol, d_->currentMatch.start.column);
                const int b = std::min(lineEndCol, d_->currentMatch.end.column);
                if (b > a) {
                    qreal x1 = 0, x2 = 0;
                    bandX(a, b, &x1, &x2);
                    p.fillRect(QRectF(xoff + x1, y, std::max<qreal>(x2 - x1, 1), lh),
                               d_->palette.searchCurrentColor());
                }
            }

            if (gutter > 0 && sub == 0) {
                // Only against the FIRST screen line of a row. A number beside
                // every continuation line would be four different numbers for
                // one line of the file.
                p.setPen(d_->palette.gutterForeground());
                p.setFont(d_->cache.font());
                p.drawText(QRectF(0, y, gutter - d_->cache.charWidth() * 1.5, lh),
                           Qt::AlignRight | Qt::AlignVCenter, QString::number(row + 1));

                // The marker is a painted triangle, not a glyph. A character
                // like U+25B8 depends on font fallback, and the one thing the
                // offscreen platform cannot tell you is whether fallback found
                // anything -- it renders a box here and looks fine.
                if (d_->foldMode && d_->hasFoldWidget(row)) {
                    const qreal cw = d_->cache.charWidth();
                    const qreal cx = gutter - cw * 0.75;
                    const qreal cy = y + lh / 2;
                    const qreal a = std::max<qreal>(3.0, cw * 0.32);
                    QPolygonF tri;
                    if (d_->map.expanded(row))
                        tri << QPointF(cx - a, cy - a * 0.6)
                            << QPointF(cx + a, cy - a * 0.6)
                            << QPointF(cx, cy + a * 0.8);
                    else
                        tri << QPointF(cx - a * 0.6, cy - a)
                            << QPointF(cx - a * 0.6, cy + a)
                            << QPointF(cx + a * 0.8, cy);
                    p.save();
                    p.setRenderHint(QPainter::Antialiasing, true);
                    p.setPen(Qt::NoPen);
                    p.setBrush(d_->palette.gutterForeground());
                    p.drawPolygon(tri);
                    p.restore();
                }
            }

            // draw() paints the whole layout at once and clips to the painter,
            // so each sub-line is drawn by offsetting the layout so that this
            // sub-line lands on this y, and clipping to this row of pixels.
            p.save();
            p.setClipRect(QRectF(gutter, y, viewport()->width() - gutter, lh));
            // THE PEN IS THE COLOUR OF ANY TEXT THE FORMATS DO NOT COVER, and
            // with no tokenizer -- an untitled buffer, or a file with no
            // grammar -- the formats cover nothing at all. Left unset it was
            // whichever pen the gutter had last used, or the widget palette's
            // default black, so a plain-text tab in the dark theme rendered
            // dark on dark. The editor's own foreground is the only right
            // answer here; nothing else knows what the background is.
            p.setPen(d_->palette.foreground());
            tl->draw(&p, QPointF(xoff, y - sub * lh));
            p.restore();

            // "... " after a collapsed header, so a folded region is visible
            // as something and not just as a jump in the line numbers.
            if (!d_->map.expanded(row) && sub == subCount - 1) {
                const qreal cw = d_->cache.charWidth();
                const qreal ex =
                    xoff + tl->lineAt(sub).naturalTextWidth() + cw * 0.5;
                const qreal ey = y + lh / 2;
                p.save();
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(Qt::NoPen);
                p.setBrush(d_->palette.gutterForeground());
                for (int k = 0; k < 3; ++k)
                    p.drawEllipse(QPointF(ex + cw * (0.4 + 0.6 * k), ey),
                                  std::max<qreal>(1.0, cw * 0.11),
                                  std::max<qreal>(1.0, cw * 0.11));
                p.restore();
            }
        }
    }

    if (d_->cursorVisible && hasFocus()) {
        const int line = cursorScreenLine(nullptr);
        if (line >= topLine && line < topLine + visible) {
            int sub = 0;
            const qreal x = xoff + d_->cache.xForColumn(d_->cursor.row, d_->cursor.column, &sub);
            const qreal y = (line - topLine) * lh;
            p.fillRect(QRectF(x, y, 2, lh), d_->palette.cursorColor());
        }
    }

    if (d_->softWrap) {
        // Rows measured during this paint may have changed the total. Applied
        // after painting rather than guessed before it, same as maxWidthSeen().
        const int want = std::max(0, screenLineCount() - visibleRowCount());
        if (verticalScrollBar()->maximum() != want)
            verticalScrollBar()->setRange(0, want);
    } else {
        // maxWidthSeen() only grows as rows are laid out, so the scrollbar range
        // is refreshed after painting rather than guessed before it.
        const int vw = viewport()->width() - gutter;
        const int wide = static_cast<int>(d_->cache.maxWidthSeen()) - vw;
        if (horizontalScrollBar()->maximum() != std::max(0, wide + 16))
            horizontalScrollBar()->setRange(0, std::max(0, wide + 16));
    }
}

// --- input -----------------------------------------------------------------


void EditorWidget::focusInEvent(QFocusEvent *e) {
    QAbstractScrollArea::focusInEvent(e);
    d_->cursorVisible = true;
    d_->blink.start();
    viewport()->update();
}

void EditorWidget::focusOutEvent(QFocusEvent *e) {
    QAbstractScrollArea::focusOutEvent(e);
    d_->blink.stop();
    d_->cursorVisible = false;
    viewport()->update();
}

// One place turns a viewport point into a document position. Both press and
// drag used to do it inline and the two copies had to agree about the gutter,
// the scroll offset and -- once wrapping existed -- which sub-line was under
// the pointer. They no longer can disagree.
aced::Position EditorWidget::positionAt(const QPointF &pt) const {
    const qreal lh = d_->cache.lineHeight();
    const int gutter = d_->gutterWidth(d_->doc.lineCount(), d_->cache.charWidth());
    const int line = verticalScrollBar()->value()
                     + static_cast<int>(std::floor(pt.y() / lh));

    int row = 0, sub = 0;
    // The gate is the MAP, not soft wrap. Folding makes rows-to-screen-lines
    // non-trivial with wrapping off, and the old `row = line` shortcut then
    // resolves a click below a collapsed block to a row that is not on screen.
    if (!d_->map.isIdentity()) {
        row = d_->map.rowFromDisplay(std::max(0, line), &sub);
        // The map may still think a row above is one screen line tall. Clicking
        // is the one moment that has to be exact, so the rows between the top
        // of the viewport and the click are laid out and the map corrected
        // before the lookup is trusted. Bounded by what is on screen.
        const int top = verticalScrollBar()->value();
        int probe = d_->map.rowFromDisplay(top, nullptr);
        int acc = d_->map.displayFromRow(probe);
        while (probe < d_->doc.lineCount() && acc <= line) {
            if (d_->map.visible(probe)) {
                const int n = d_->cache.screenLineCount(probe);
                d_->map.setHeight(probe, n);
                acc = d_->map.displayFromRow(probe) + n;
            }
            ++probe;
        }
        row = d_->map.rowFromDisplay(std::max(0, line), &sub);
    } else {
        row = line;
    }
    row = std::clamp(row, 0, std::max(0, d_->doc.lineCount() - 1));

    const qreal x = pt.x() - gutter
                    + (d_->softWrap ? 0 : horizontalScrollBar()->value());
    return d_->doc.clamp({row, d_->cache.columnForX(row, sub, std::max<qreal>(x, 0))});
}

void EditorWidget::mousePressEvent(QMouseEvent *e) {
    CursorWatch watch(this);
    if (e->button() != Qt::LeftButton) return;

    const int foldRowHit = foldMarkerRowAt(e->position());
    if (foldRowHit >= 0) {
        d_->dragging = false;
        toggleFold(foldRowHit);
        updateHoverShape(e->position());
        // Not a text click: leave the cursor and the selection where they are,
        // and do NOT start a drag. Falling through would select from wherever
        // the marker happens to sit to wherever the mouse goes next.
        return;
    }
    const Position p = positionAt(e->position());

    if (e->modifiers() & Qt::ShiftModifier) {
        d_->cursor = p;
        Q_EMIT selectionChanged(hasSelection());
    } else {
        setCursor(p);
    }
    d_->dragging = true;
    d_->undo->mark();
    viewport()->update();
}

// The pointer says what a click will do. Over text that is an I-beam; over the
// gutter it is not, because a click there does not place a caret at the point
// under it; and over a fold marker it is a hand, because the marker is a
// control.
void EditorWidget::updateHoverShape(const QPointF &pt) {
    Qt::CursorShape want = Qt::IBeamCursor;
    const int gutter = d_->gutterWidth(d_->doc.lineCount(), d_->cache.charWidth());
    if (gutter > 0 && pt.x() < gutter) {
        want = Qt::ArrowCursor;
        if (foldMarkerRowAt(pt) >= 0) want = Qt::PointingHandCursor;
    }
    if (want == d_->hoverShape) return;
    d_->hoverShape = want;
    viewport()->setCursor(want);
}

void EditorWidget::mouseMoveEvent(QMouseEvent *e) {
    if (!d_->dragging) updateHoverShape(e->position());
    if (!d_->dragging || !(e->buttons() & Qt::LeftButton)) return;
    CursorWatch watch(this);
    d_->cursor = positionAt(e->position());
    Q_EMIT selectionChanged(hasSelection());
    ensureCursorVisible();
    viewport()->update();
}

void EditorWidget::mouseReleaseEvent(QMouseEvent *) { d_->dragging = false; }

// QAbstractScrollArea installs an event filter on its viewport and takes
// QEvent::ContextMenu off it, calling this instead. So Qt::CustomContextMenu
// set on the viewport NEVER FIRES -- that signal is emitted from
// QWidget::event(), which the filter stops the event from ever reaching, and
// nothing reports an error. Setting the policy on the scroll area does work,
// by accident of the event propagating to the parent after this ignores it,
// but then the position arrives in scroll-area coordinates: off by the frame
// width from every other point in this class, so a menu opened at the top-left
// of the document acts on the wrong character.
//
// Hence an explicit signal, in viewport coordinates.
void EditorWidget::contextMenuEvent(QContextMenuEvent *e) {
    QPoint pos = e->pos();
    // Straight off the event, not mapped. QScintilla -- which is a
    // QAbstractScrollArea with a working right-click menu on all three
    // platforms -- places its menu at e->globalPos() for exactly this reason:
    // the event arrives at the viewport, is re-routed here by the scroll
    // area's filter, and which widget its pos() is relative to is then a
    // question with an easy wrong answer.
    QPoint global = e->globalPos();
    if (e->reason() == QContextMenuEvent::Keyboard) {
        // The Menu key has no pointer to sit under, so the menu goes at the
        // caret -- and the caret is the position it should act on.
        const QRectF r = inputMethodQuery(Qt::ImCursorRectangle).toRectF();
        pos = QPoint(static_cast<int>(r.left()), static_cast<int>(r.bottom()));
        global = viewport()->mapToGlobal(pos);
    }
    e->accept();
    if (!qEnvironmentVariableIsEmpty("ANYEDITQT_DEBUG_EVENTS")) {
        std::fprintf(
            stderr,
            "[anyedit] contextMenuEvent reason=%d viewport=%d,%d global=%d,%d\n",
            static_cast<int>(e->reason()), pos.x(), pos.y(), global.x(), global.y());
    }
    Q_EMIT contextMenuRequested(pos, global);
}

void EditorWidget::mouseDoubleClickEvent(QMouseEvent *e) {
    CursorWatch watch(this);
    mousePressEvent(e);
    const std::string &line = d_->doc.line(d_->cursor.row);
    int a = d_->cursor.column, b = a;
    while (a > 0 && isWordByte(line[a - 1])) --a;
    while (b < static_cast<int>(line.size()) && isWordByte(line[b])) ++b;
    if (a == b) return;
    setSelection({{d_->cursor.row, a}, {d_->cursor.row, b}});
}

void EditorWidget::inputMethodEvent(QInputMethodEvent *e) {
    CursorWatch watch(this);
    // Commit only. Preedit rendering is not implemented, and pretending
    // otherwise by swallowing the preedit string would lose characters.
    if (!e->commitString().isEmpty()) {
        d_->doc.insert(d_->cursor, e->commitString().toStdString());
        setCursor({d_->cursor.row,
                   d_->cursor.column + static_cast<int>(e->commitString().toUtf8().size())});
    }
    e->accept();
}

QVariant EditorWidget::inputMethodQuery(Qt::InputMethodQuery query) const {
    switch (query) {
        case Qt::ImCursorRectangle: {
            const qreal lh = d_->cache.lineHeight();
            const int gutter = d_->gutterWidth(d_->doc.lineCount(), d_->cache.charWidth());
            const qreal x = d_->cache.xForColumn(d_->cursor.row, d_->cursor.column);
            const int line = cursorScreenLine(nullptr) - verticalScrollBar()->value();
            return QRectF(gutter + x - (d_->softWrap ? 0 : horizontalScrollBar()->value()),
                          line * lh, 2, lh);
        }
        case Qt::ImSurroundingText:
            return QString::fromStdString(d_->doc.line(d_->cursor.row));
        case Qt::ImCurrentSelection:
            return hasSelection() ? QString::fromStdString(d_->doc.textInRange(selection()))
                                  : QString();
        default:
            return QAbstractScrollArea::inputMethodQuery(query);
    }
}

void EditorWidget::keyPressEvent(QKeyEvent *e) {
    CursorWatch watch(this);
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const bool ctrl = e->modifiers() & Qt::ControlModifier;

    auto moveTo = [&](Position p) {
        p = d_->doc.clamp(p);
        const bool had = hasSelection();
        d_->cursor = p;
        if (!shift) d_->anchor = p;
        if (had != hasSelection()) Q_EMIT selectionChanged(hasSelection());
        Q_EMIT cursorMoved(p.row, p.column);
        ensureCursorVisible();
        viewport()->update();
    };
    // Vertical movement goes through x, not through the column number: with a
    // tab or a proportional font, column N on two different rows is not the
    // same place on screen, and moving down would visibly drift.
    // Up and Down move by SCREEN line, not by document row. With wrapping on
    // those differ, and moving by row would step over every continuation line
    // of a wrapped paragraph -- which is not what the arrow key means to
    // anybody. Without wrapping the two are identical and this is the old path.
    auto moveVertical = [&](int deltaLines) {
        if (d_->goalX < 0)
            d_->goalX = static_cast<int>(d_->cache.xForColumn(d_->cursor.row, d_->cursor.column));
        const int goal = d_->goalX;
        // Same gate as positionAt: with anything folded, the next row down is
        // not cursor.row + 1.
        if (d_->map.isIdentity()) {
            const int row = std::clamp(d_->cursor.row + deltaLines, 0,
                                       d_->doc.lineCount() - 1);
            moveTo({row, d_->cache.columnForX(row, 0, goal)});
            d_->goalX = goal;  // survives the move, so a run of Downs stays straight
            return;
        }
        int sub = 0;
        const int line = cursorScreenLine(&sub);
        int target = std::max(0, line + deltaLines);

        // Rows are LAID OUT while walking towards the target rather than read
        // straight out of the map. Rows below the viewport have never been
        // painted, so the map still assumes they are one screen line tall; a
        // Down at the bottom of the screen would land in the wrong place and
        // then correct itself on the next paint, which looks like the cursor
        // jumping on its own. Bounded by the distance moved, so PageDown lays
        // out a page and nothing more.
        for (int r = d_->cursor.row; r < d_->doc.lineCount(); ++r) {
            // A hidden row occupies no screen line, so laying it out to learn
            // its height would cost a tokenize for nothing.
            if (!d_->map.visible(r)) continue;
            const int n = d_->cache.screenLineCount(r);
            d_->map.setHeight(r, n);
            const int start = d_->map.displayFromRow(r);
            if (target < start + n) break;
            if (r + 1 >= d_->doc.lineCount()) { target = start + n - 1; break; }
        }
        int newSub = 0;
        const int newRow = d_->map.rowFromDisplay(target, &newSub);
        moveTo({newRow, d_->cache.columnForX(newRow, newSub, goal)});
        d_->goalX = goal;
    };
    auto deleteSelection = [&]() -> bool {
        if (!hasSelection()) return false;
        const Range r = selection();
        d_->doc.remove(r);
        d_->cursor = d_->anchor = r.start;
        Q_EMIT selectionChanged(false);
        return true;
    };

    const bool typing = !e->text().isEmpty() && !ctrl && e->text().at(0).isPrint();

    // The goal column survives a RUN of vertical movement and nothing else.
    // Resetting it on every non-typing key looked right and was wrong: Down
    // Down through a short line then came back out at the short line's column
    // instead of where the run started, because the second Down had already
    // cleared the goal before reading it.
    const bool vertical = e->key() == Qt::Key_Up || e->key() == Qt::Key_Down
                          || e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown;
    if (!vertical) d_->goalX = -1;

    if (ctrl) {
        switch (e->key()) {
            case Qt::Key_Z:
                if (shift ? d_->undo->redo() : d_->undo->undo()) {
                    d_->cache.invalidateAll();
                    setCursor(d_->undo->lastTouched());
                }
                return;
            case Qt::Key_Y:
                redoEdit();
                return;
            case Qt::Key_A: selectAll(); return;
            // VS Code's bindings. Shift is part of them, so these are matched
            // before the plain-Ctrl cases below rather than beside them.
            // BRACE AS WELL AS BRACKET. QKeyEvent::key() carries the character
            // the layout actually produces, and Shift+[ produces '{' -- so
            // Ctrl+Shift+[ arrives as Key_BraceLeft on X11, Windows and macOS
            // alike, and a handler that only knows Key_BracketLeft never fires
            // on any of them. The menu entry works because QKeySequence
            // normalises for shortcut matching; a raw key handler does not get
            // that for free. Layouts where '{' is somewhere else entirely are
            // why both are accepted rather than only the brace.
            case Qt::Key_BracketLeft:
            case Qt::Key_BraceLeft:
                if (e->modifiers() & Qt::ShiftModifier) {
                    if (!foldRow(d_->cursor.row)) foldEnclosing();
                    return;
                }
                break;
            case Qt::Key_BracketRight:
            case Qt::Key_BraceRight:
                if (e->modifiers() & Qt::ShiftModifier) {
                    unfoldRow(d_->cursor.row);
                    return;
                }
                break;
            case Qt::Key_C: copy(); return;
            case Qt::Key_X: cut(); return;
            case Qt::Key_V: paste(); return;
            case Qt::Key_Home: moveTo({0, 0}); return;
            case Qt::Key_End: moveTo(d_->doc.end()); return;
            default: break;
        }
    }

    switch (e->key()) {
        case Qt::Key_Left:
            if (d_->cursor.column > 0) {
                int c = aced::Document::utf8Floor(d_->doc.line(d_->cursor.row),
                                                  d_->cursor.column - 1);
                moveTo({d_->cursor.row, c});
            } else if (d_->cursor.row > 0) {
                moveTo({d_->cursor.row - 1, d_->doc.lineLength(d_->cursor.row - 1)});
            }
            return;
        case Qt::Key_Right: {
            const std::string &line = d_->doc.line(d_->cursor.row);
            if (d_->cursor.column < static_cast<int>(line.size())) {
                int c = d_->cursor.column + 1;
                while (c < static_cast<int>(line.size())
                       && (static_cast<unsigned char>(line[c]) & 0xC0) == 0x80) ++c;
                moveTo({d_->cursor.row, c});
            } else if (d_->cursor.row < d_->doc.lineCount() - 1) {
                moveTo({d_->cursor.row + 1, 0});
            }
            return;
        }
        case Qt::Key_Up:       moveVertical(-1); return;
        case Qt::Key_Down:     moveVertical(1); return;
        case Qt::Key_PageUp:   moveVertical(-visibleRowCount()); return;
        case Qt::Key_PageDown: moveVertical(visibleRowCount()); return;
        case Qt::Key_Home: {
            // Start of the SCREEN line when wrapping, which is where the cursor
            // visibly is. Home jumping to the start of a paragraph three screen
            // lines further up is the behaviour nobody wants.
            if (d_->softWrap) {
                int sub = 0;
                d_->cache.xForColumn(d_->cursor.row, d_->cursor.column, &sub);
                moveTo({d_->cursor.row,
                        d_->cache.columnAtSubLineStart(d_->cursor.row, sub)});
                return;
            }
            moveTo({d_->cursor.row, 0});
            return;
        }
        case Qt::Key_End: {
            if (d_->softWrap) {
                int sub = 0;
                d_->cache.xForColumn(d_->cursor.row, d_->cursor.column, &sub);
                moveTo({d_->cursor.row,
                        d_->cache.columnAtSubLineEnd(d_->cursor.row, sub)});
                return;
            }
            moveTo({d_->cursor.row, d_->doc.lineLength(d_->cursor.row)});
            return;
        }

        case Qt::Key_Backspace:
            d_->undo->mark();
            if (!deleteSelection()) {
                if (d_->cursor.column > 0) {
                    const int c = aced::Document::utf8Floor(d_->doc.line(d_->cursor.row),
                                                            d_->cursor.column - 1);
                    d_->doc.remove({{d_->cursor.row, c}, d_->cursor});
                    d_->cursor = d_->anchor = {d_->cursor.row, c};
                } else if (d_->cursor.row > 0) {
                    const int prevLen = d_->doc.lineLength(d_->cursor.row - 1);
                    d_->doc.remove({{d_->cursor.row - 1, prevLen}, d_->cursor});
                    d_->cursor = d_->anchor = {d_->cursor.row - 1, prevLen};
                }
            }
            d_->undo->mark();
            setCursor(d_->cursor);
            return;

        case Qt::Key_Delete: {
            d_->undo->mark();
            if (!deleteSelection()) {
                const std::string &line = d_->doc.line(d_->cursor.row);
                if (d_->cursor.column < static_cast<int>(line.size())) {
                    int c = d_->cursor.column + 1;
                    while (c < static_cast<int>(line.size())
                           && (static_cast<unsigned char>(line[c]) & 0xC0) == 0x80) ++c;
                    d_->doc.remove({d_->cursor, {d_->cursor.row, c}});
                } else if (d_->cursor.row < d_->doc.lineCount() - 1) {
                    d_->doc.remove({d_->cursor, {d_->cursor.row + 1, 0}});
                }
            }
            d_->undo->mark();
            setCursor(d_->cursor);
            return;
        }

        case Qt::Key_Return:
        case Qt::Key_Enter: {
            d_->undo->mark();
            deleteSelection();
            // Carry the current line's leading whitespace onto the new line.
            const std::string &line = d_->doc.line(d_->cursor.row);
            std::string indent;
            for (char c : line) {
                if (c == ' ' || c == '\t') indent += c;
                else break;
            }
            if (static_cast<int>(indent.size()) > d_->cursor.column)
                indent.resize(d_->cursor.column);
            d_->doc.insert(d_->cursor, "\n" + indent);
            setCursor({d_->cursor.row + 1, static_cast<int>(indent.size())});
            d_->undo->mark();
            return;
        }

        case Qt::Key_Backtab:
            unindentSelection();
            return;

        case Qt::Key_Tab:
            // A selection spanning more than one line means INDENT THE BLOCK,
            // not "replace all that with a tab". Replacing is what the plain
            // insert below does, and it is right for a selection inside one
            // line -- which is a typo being overtyped, not a block.
            if (hasSelection() && selection().start.row != selection().end.row) {
                indentSelection();
                return;
            }
            d_->undo->mark();
            deleteSelection();
            if (d_->insertSpaces) {
                d_->doc.insert(d_->cursor, std::string(d_->tabWidth, ' '));
                setCursor({d_->cursor.row, d_->cursor.column + d_->tabWidth});
            } else {
                // One byte in the document. The column advances by one because
                // columns are byte offsets here; LineCache's tab stop is what
                // makes it LOOK tabWidth wide, and that is the right split.
                d_->doc.insert(d_->cursor, std::string("\t"));
                setCursor({d_->cursor.row, d_->cursor.column + 1});
            }
            d_->undo->mark();
            return;

        default: break;
    }

    if (typing) {
        const std::string s = e->text().toStdString();
        if (hasSelection()) {
            d_->undo->mark();
            deleteSelection();
        }
        d_->doc.insert(d_->cursor, s);
        const bool had = hasSelection();
        d_->cursor = d_->anchor = {d_->cursor.row,
                                   d_->cursor.column + static_cast<int>(s.size())};
        if (had) Q_EMIT selectionChanged(false);
        Q_EMIT cursorMoved(d_->cursor.row, d_->cursor.column);
        ensureCursorVisible();
        viewport()->update();
        return;
    }

    QAbstractScrollArea::keyPressEvent(e);
}

}  // namespace acedqt
