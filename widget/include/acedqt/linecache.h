// include/acedqt/linecache.h
#pragma once

#include <QFont>
#include <QString>
#include <QTextLayout>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "aced/delta.h"
#include "acedqt/palette.h"

namespace aced {
class Document;
class Tokenizer;
}  // namespace aced

namespace acedqt {

// Tokenised, formatted, laid-out lines, cached by row.
//
// This is the piece that replaces the bulk of ace's edit_session.js. Ace had to
// compute document->screen coordinates by hand because the DOM will not lay out
// text for you; QTextLayout does, so what is left is bookkeeping: which rows are
// still valid, and what the tokenizer's end-of-line state was for each one.
//
// TOKENIZER STATE IS THE REASON THIS IS NOT A PLAIN LRU. Each row is tokenized
// starting from the state the previous row ended in, so editing row N can change
// the meaning of every row after it -- open a block comment and the rest of the
// file is a comment. invalidateFrom() therefore drops the tail, not one entry,
// but only as far as it has to: re-tokenizing stops early once a row's computed
// start state matches what it was before, because from there down nothing can
// have changed. Typing inside a function body costs one row, not the file.
class LineCache {
public:
    LineCache();
    ~LineCache();

    void setDocument(const aced::Document *doc);
    void setTokenizer(const aced::Tokenizer *tk);  // null = no highlighting
    void setPalette(const Palette &p);
    void setFont(const QFont &f);
    void setTabWidth(int columns);

    // Pixels. Zero or less means no wrapping and a row is one screen line,
    // which is what every coordinate below degenerates to. Changing it drops
    // every layout: wrapping is decided at layout time, not at paint time.
    void setWrapWidth(qreal px);
    qreal wrapWidth() const { return wrapWidth_; }
    bool wrapping() const { return wrapWidth_ > 0; }

    const QFont &font() const { return font_; }
    qreal lineHeight() const { return lineHeight_; }
    qreal charWidth() const { return charWidth_; }

    // Laid out on demand and kept. Valid until the next invalidate.
    QTextLayout *layout(int row) const;

    // How many screen lines `row` occupies. LAYS THE ROW OUT, so it is not the
    // function to call over a whole document.
    int screenLineCount(int row) const;

    // The same number for a row that has already been laid out, and -1 for one
    // that has not. THIS IS THE ONE THE WIDGET'S WRAP MAP USES: knowing how
    // many screen lines every row of a 50,000-line file occupies would mean
    // laying out all 50,000, which is the cost the cache exists to avoid. Rows
    // that have never been painted are assumed to be one screen line and
    // corrected as they are.
    int screenLineCountIfKnown(int row) const;

    // Widest laid-out row seen so far, for the horizontal scrollbar. It is a
    // running maximum over what has actually been drawn rather than a scan of
    // the document: measuring every line of a large file to size a scrollbar
    // is the kind of thing that makes opening a file feel slow.
    qreal maxWidthSeen() const { return maxWidth_; }

    // Rows [row, end) are gone. Called from the document's delta listener.
    void invalidateFrom(int row);
    void invalidateAll();

    // x offset of a byte column within a row, and the inverse. Both go through
    // the layout, so they are correct for proportional fonts and for any shaping
    // the font does -- which is the whole reason not to multiply by charWidth().
    //
    // WITH WRAPPING ON, a row occupies several screen lines and x alone no
    // longer identifies a position: the sub-line has to travel with it. The
    // column->x direction returns which sub-line it landed on through
    // `subLine`, and the x->column direction requires it.
    qreal xForColumn(int row, int column, int *subLine = nullptr) const;
    int columnForX(int row, int subLine, qreal x) const;

    // First and last byte column on a sub-line, for Home/End and for painting a
    // selection band that stops at the wrap point rather than running off.
    int columnAtSubLineStart(int row, int subLine) const;
    int columnAtSubLineEnd(int row, int subLine) const;

private:
    void remeasureFont();
    QList<QTextLayout::FormatRange> formatsFor(int row, const std::string &line) const;
    // Tokenizer state at the START of `row`. Computed by walking down from the
    // deepest known row, so the walk is bounded by how far the cache was dropped.
    const std::vector<std::string> &stackAt(int row) const;

    struct Row {
        std::unique_ptr<QTextLayout> layout;
        std::vector<std::string> endStack;  // tokenizer stack after this row
        std::string endState;
    };

    const aced::Document *doc_ = nullptr;
    const aced::Tokenizer *tk_ = nullptr;
    Palette palette_;
    QFont font_;
    qreal lineHeight_ = 16;
    qreal charWidth_ = 8;
    qreal wrapWidth_ = 0;
    int tabWidth_ = 4;
    mutable qreal maxWidth_ = 0;
    mutable std::unordered_map<int, Row> rows_;
    mutable int contiguousTo_ = -1;  // rows 0..contiguousTo_ have valid end state
};

}  // namespace acedqt
