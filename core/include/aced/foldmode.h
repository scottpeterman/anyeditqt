// include/aced/foldmode.h
//
// Which rows can be folded, and where each fold ends.
//
// Ported from ace's src/mode/folding/. Ace is BSD-3 and this follows its
// algorithms; see THIRD_PARTY.md. What is NOT from ace is the machinery that
// hides the rows afterwards -- that is aced::LineMap, and it does not appear
// here. A fold mode answers questions about the buffer and owns no state.
//
// ROW GRANULARITY. A FoldRange carries columns because ace's do and because a
// future "show the folded text inline" placeholder will want them, but the
// widget hides whole rows: [startRow + 1, endRow]. Anything below that treats a
// column as approximate is doing so deliberately.
//
// COVERAGE. 115 of the 198 corpus modes, via three fold modes:
//   CStyle    98 modes -- brackets and block comments, plus //#region
//   Indent    15 modes -- CoffeeScript-family, indentation and # comment runs
//   Pythonic   2 modes -- a trailing ':' or bracket, then indentation
// The rest resolve to nullptr and simply have no fold widgets. Ace has 24 more
// fold modes; xml/html is the big one still missing and is the next to port.
#pragma once

#include <string>
#include <vector>

namespace aced {

class Document;
class TokenChain;
struct TokenSpan;

struct FoldRange {
    int startRow = 0;
    int startColumn = 0;
    int endRow = 0;
    int endColumn = 0;

    bool multiLine() const { return endRow > startRow; }
};

enum class FoldWidget { None, Start };

// Everything a fold mode is allowed to read. Narrow on purpose: a fold mode
// that could reach the Document could edit it.
class FoldSource {
public:
    virtual ~FoldSource() = default;
    virtual int lineCount() const = 0;
    virtual const std::string &line(int row) const = 0;
    // Byte-offset spans for `row`, empty when there is no tokenizer. A fold
    // mode must still work without them -- plain text is a mode like any
    // other -- it just cannot tell a brace in a comment from a real one.
    virtual const std::vector<TokenSpan> &tokens(int row) const = 0;
};

class FoldMode {
public:
    virtual ~FoldMode() = default;

    // Does `row` open a fold? Cheap: the gutter asks this for every visible
    // row on every paint, so implementations read line text and never tokens.
    virtual FoldWidget widget(const FoldSource &src, int row) const = 0;

    // Where that fold ends. False when `row` opens nothing, or opens something
    // that turns out to be one line long. Reads tokens, and is called on a
    // click rather than on a paint.
    virtual bool range(const FoldSource &src, int row, FoldRange *out) const = 0;
};

// Null for a mode with no fold support, which callers must handle: it is the
// normal state for 83 of the 198 modes and for plain text.
const FoldMode *foldModeFor(const std::string &mode);

// A FoldSource over a live Document, with tokens from a TokenChain. Neither is
// owned. The chain is optional: without one, tokens() is always empty and the
// fold modes fall back to plain-text reasoning, which is exactly what a buffer
// with no grammar should get.
class DocumentFoldSource : public FoldSource {
public:
    DocumentFoldSource(const Document *doc, const TokenChain *chain)
        : doc_(doc), chain_(chain) {}

    int lineCount() const override;
    const std::string &line(int row) const override;
    const std::vector<TokenSpan> &tokens(int row) const override;

private:
    const Document *doc_;
    const TokenChain *chain_;
};

// --- pieces the fold modes share, exposed because they are worth testing ----

// Byte index of the first non-whitespace character, or -1 for a blank line.
int indentOf(const std::string &line);

// Token-aware bracket matching. `column` is the byte offset of the bracket
// itself. Both return false when there is no match inside the document.
//
// Which tokens are scanned is decided by the type of the token holding the
// opening bracket, following ace: a brace in a comment matches only a brace in
// a comment, a brace in code only one in code. With no tokenizer every byte is
// eligible, which is the right answer for plain text and the wrong one for a
// language whose strings contain braces -- there is no third option.
bool findClosingBracket(const FoldSource &src, char open, int row, int column,
                        int *endRow, int *endColumn);
bool findOpeningBracket(const FoldSource &src, char close, int row, int column,
                        int *startRow, int *startColumn);

// The block of rows indented further than `row`. Blank lines do not end it;
// trailing blank lines are not part of it. From ace's fold_mode.js.
bool indentationBlock(const FoldSource &src, int row, int startColumn,
                      FoldRange *out);

}  // namespace aced
