// src/linecache.cpp
#include "acedqt/linecache.h"

#include <QFontMetricsF>
#include <cmath>
#include <QTextOption>

#include "aced/document.h"
#include "aced/tokenizer.h"

namespace acedqt {

LineCache::LineCache() {
    font_ = QFont("monospace");
    font_.setStyleHint(QFont::Monospace);
    font_.setPixelSize(14);
    remeasureFont();
}

LineCache::~LineCache() = default;

void LineCache::setDocument(const aced::Document *doc) {
    doc_ = doc;
    invalidateAll();
}

void LineCache::setTokenizer(const aced::Tokenizer *tk) {
    tk_ = tk;
    invalidateAll();
}

void LineCache::setPalette(const Palette &p) {
    palette_ = p;
    invalidateAll();
}

void LineCache::setFont(const QFont &f) {
    font_ = f;
    remeasureFont();
    invalidateAll();
}

void LineCache::setTabWidth(int columns) {
    tabWidth_ = columns > 0 ? columns : 4;
    invalidateAll();
}

void LineCache::setWrapWidth(qreal px) {
    if (px < 0) px = 0;
    // Compared with a tolerance because the caller computes this from a
    // viewport width minus a gutter measured in fractional character widths,
    // and a resize that does not move the wrap point by a whole pixel should
    // not throw away every layout in the file.
    if (qAbs(px - wrapWidth_) < 0.5) return;
    wrapWidth_ = px;
    invalidateAll();
}

void LineCache::remeasureFont() {
    const QFontMetricsF fm(font_);
    // CEILED TO A WHOLE PIXEL, deliberately. Screen lines are stacked at
    // multiples of this, and the widget clips each one to its own band while
    // painting; with a fractional height -- 16.6 for the default font -- those
    // bands land on fractional boundaries and the bottom pixel row of every
    // second line is clipped away. The visible symptom is an underscore that
    // renders on some lines and not others, which reads as a font problem
    // rather than a geometry one.
    lineHeight_ = std::ceil(fm.height());
    if (lineHeight_ < 1) lineHeight_ = 1;
    // A monospace advance measured over many characters rather than one, so
    // rounding in the font's metrics does not accumulate across a long line.
    charWidth_ = fm.horizontalAdvance(QString(64, QLatin1Char('X'))) / 64.0;
    if (charWidth_ <= 0) charWidth_ = 8;
}

void LineCache::invalidateAll() {
    rows_.clear();
    contiguousTo_ = -1;
    maxWidth_ = 0;
}

void LineCache::invalidateFrom(int row) {
    if (row <= 0) { invalidateAll(); return; }
    for (auto it = rows_.begin(); it != rows_.end();)
        it = (it->first >= row) ? rows_.erase(it) : std::next(it);
    if (contiguousTo_ >= row) contiguousTo_ = row - 1;
}

const std::vector<std::string> &LineCache::stackAt(int row) const {
    static const std::vector<std::string> kEmpty;
    if (row <= 0 || !tk_ || !doc_) return kEmpty;

    // Walk forward from the deepest contiguous row, tokenizing only what is
    // needed to know the state at `row`. Rows produced on the way are kept --
    // scrolling down then repainting would otherwise redo all of this.
    int from = contiguousTo_ + 1;
    if (from < 0) from = 0;
    for (int r = from; r < row; ++r) {
        auto hit = rows_.find(r);
        if (hit != rows_.end() && hit->second.layout) { contiguousTo_ = r; continue; }
        const std::string &text = doc_->line(r);
        const std::vector<std::string> &in = (r == 0) ? kEmpty : rows_[r - 1].endStack;
        const std::string inState = (r == 0) ? std::string("start") : rows_[r - 1].endState;
        auto res = tk_->tokenize(text, inState, in);
        Row &slot = rows_[r];
        slot.endStack = res.stack;
        slot.endState = res.state;
        contiguousTo_ = r;
    }
    auto prev = rows_.find(row - 1);
    return prev == rows_.end() ? kEmpty : prev->second.endStack;
}

QList<QTextLayout::FormatRange> LineCache::formatsFor(int row,
                                                      const std::string &line) const {
    QList<QTextLayout::FormatRange> out;
    if (!tk_) return out;

    const std::vector<std::string> &inStack = stackAt(row);
    std::string inState = "start";
    if (row > 0) {
        auto prev = rows_.find(row - 1);
        if (prev != rows_.end() && !prev->second.endState.empty())
            inState = prev->second.endState;
    }
    auto res = tk_->tokenize(line, inState, inStack);

    Row &slot = rows_[row];
    slot.endStack = res.stack;
    slot.endState = res.state;
    if (contiguousTo_ == row - 1) contiguousTo_ = row;

    // Token spans are byte offsets into a UTF-8 line; QTextLayout wants UTF-16
    // indices. Converting by re-encoding each prefix is O(n) per token and O(n^2)
    // per line in the worst case, so the common case is short-circuited: a line
    // that is pure ASCII has byte offsets and UTF-16 indices that are equal, and
    // that is almost every line of almost every source file.
    bool ascii = true;
    for (unsigned char c : line)
        if (c >= 0x80) { ascii = false; break; }

    int byteOffset = 0;
    for (const auto &t : res.tokens) {
        const int startByte = byteOffset;
        byteOffset += static_cast<int>(t.value.size());
        if (byteOffset > static_cast<int>(line.size()))
            byteOffset = static_cast<int>(line.size());

        int start, length;
        if (ascii) {
            start = startByte;
            length = byteOffset - startByte;
        } else {
            start = QString::fromUtf8(line.c_str(), startByte).size();
            length = QString::fromUtf8(line.c_str(), byteOffset).size() - start;
        }
        if (length <= 0) continue;

        QTextLayout::FormatRange fr;
        fr.start = start;
        fr.length = length;
        fr.format = palette_.formatFor(QString::fromStdString(t.type));
        out.append(fr);
    }
    return out;
}

QTextLayout *LineCache::layout(int row) const {
    if (!doc_ || row < 0 || row >= doc_->lineCount()) return nullptr;

    auto hit = rows_.find(row);
    if (hit != rows_.end() && hit->second.layout) return hit->second.layout.get();

    const std::string &line = doc_->line(row);
    auto formats = formatsFor(row, line);

    auto tl = std::make_unique<QTextLayout>(
        QString::fromUtf8(line.c_str(), static_cast<int>(line.size())), font_);
    QTextOption opt;
    // AnywhereOrWordBoundary, not WordWrap: source code is full of tokens with
    // no break opportunity in them -- a long path, a base64 blob, a minified
    // line -- and WordWrap leaves those running off the right edge, which is
    // the one thing wrapping was turned on to prevent.
    opt.setWrapMode(wrapWidth_ > 0 ? QTextOption::WrapAtWordBoundaryOrAnywhere
                                   : QTextOption::NoWrap);
    opt.setTabStopDistance(tabWidth_ * charWidth_);
    tl->setTextOption(opt);
    tl->setFormats(formats);

    tl->beginLayout();
    qreal y = 0;
    while (true) {
        QTextLine qline = tl->createLine();
        if (!qline.isValid()) break;
        // 1e9 rather than -1 for the unwrapped case: setLineWidth(-1) is not a
        // documented "no limit" and produces a zero-width line on some
        // platforms, which shows up as every row collapsing to one character.
        qline.setLineWidth(wrapWidth_ > 0 ? wrapWidth_ : 1e9);
        qline.setPosition(QPointF(0, y));
        // The uniform lineHeight_, not qline.height(). Everything above this --
        // the widget's paint loop, its scrollbar arithmetic, its hit testing --
        // assumes screen lines are on a fixed grid, and a line containing one
        // taller glyph would silently shift every row below it by a pixel or
        // two if its own height were used here.
        y += lineHeight_;
        if (qline.naturalTextWidth() > maxWidth_) maxWidth_ = qline.naturalTextWidth();
    }
    tl->endLayout();

    Row &slot = rows_[row];
    slot.layout = std::move(tl);
    return slot.layout.get();
}

int LineCache::screenLineCount(int row) const {
    QTextLayout *tl = layout(row);
    return (tl && tl->lineCount() > 0) ? tl->lineCount() : 1;
}

int LineCache::screenLineCountIfKnown(int row) const {
    auto hit = rows_.find(row);
    if (hit == rows_.end() || !hit->second.layout) return -1;
    const int n = hit->second.layout->lineCount();
    return n > 0 ? n : 1;
}

qreal LineCache::xForColumn(int row, int column, int *subLine) const {
    if (subLine) *subLine = 0;
    QTextLayout *tl = layout(row);
    if (!tl || tl->lineCount() == 0) return 0;
    const std::string &line = doc_->line(row);
    if (column < 0) column = 0;
    if (column > static_cast<int>(line.size())) column = static_cast<int>(line.size());
    const int idx = QString::fromUtf8(line.c_str(), column).size();

    QTextLine ql = tl->lineForTextPosition(idx);
    if (!ql.isValid()) ql = tl->lineAt(tl->lineCount() - 1);
    if (subLine) *subLine = ql.lineNumber();
    return ql.cursorToX(idx);
}

int LineCache::columnForX(int row, int subLine, qreal x) const {
    QTextLayout *tl = layout(row);
    if (!tl || tl->lineCount() == 0) return 0;
    if (subLine < 0) subLine = 0;
    if (subLine >= tl->lineCount()) subLine = tl->lineCount() - 1;
    const QTextLine ql = tl->lineAt(subLine);
    int idx = ql.xToCursor(x, QTextLine::CursorBetweenCharacters);
    // xToCursor past the right edge of a WRAPPED line returns the index of the
    // first character of the next one, so clicking in the empty space to the
    // right of a wrap point would put the cursor on the following screen line.
    // Clamped back to the last index that belongs to this one.
    if (subLine + 1 < tl->lineCount() && idx >= ql.textStart() + ql.textLength())
        idx = ql.textStart() + ql.textLength() - 1;
    if (idx < 0) idx = 0;
    return tl->text().left(idx).toUtf8().size();
}

int LineCache::columnAtSubLineStart(int row, int subLine) const {
    QTextLayout *tl = layout(row);
    if (!tl || tl->lineCount() == 0) return 0;
    if (subLine < 0) subLine = 0;
    if (subLine >= tl->lineCount()) subLine = tl->lineCount() - 1;
    return tl->text().left(tl->lineAt(subLine).textStart()).toUtf8().size();
}

int LineCache::columnAtSubLineEnd(int row, int subLine) const {
    QTextLayout *tl = layout(row);
    if (!tl || tl->lineCount() == 0) return 0;
    if (subLine < 0) subLine = 0;
    if (subLine >= tl->lineCount()) subLine = tl->lineCount() - 1;
    const QTextLine ql = tl->lineAt(subLine);
    return tl->text().left(ql.textStart() + ql.textLength()).toUtf8().size();
}

}  // namespace acedqt
