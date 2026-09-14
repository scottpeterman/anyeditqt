// src/foldmode.cpp
#include "aced/foldmode.h"

#include <algorithm>
#include <unordered_map>

#include "aced/document.h"
#include "aced/tokenchain.h"

namespace aced {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }
bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_';
}
bool isOpenBracket(char c) { return c == '{' || c == '[' || c == '('; }
bool isCloseBracket(char c) { return c == '}' || c == ']' || c == ')'; }
char matching(char open) {
    switch (open) {
        case '{': return '}';
        case '[': return ']';
        case '(': return ')';
        case '}': return '{';
        case ']': return '[';
        case ')': return '(';
        default: return 0;
    }
}

int lastNonSpace(const std::string &s) {
    for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i)
        if (!isSpace(s[static_cast<size_t>(i)])) return i;
    return -1;
}

bool startsWithAt(const std::string &s, int i, const char *lit) {
    const int n = static_cast<int>(std::char_traits<char>::length(lit));
    if (i < 0 || i + n > static_cast<int>(s.size())) return false;
    return s.compare(static_cast<size_t>(i), static_cast<size_t>(n), lit) == 0;
}

// ---------------------------------------------------------------------------
// Token type families.
//
// Ace builds a regex out of the token type at the opening bracket and steps
// only to tokens matching it, which keeps a brace in a comment from matching
// one in code. The transformation it applies -- lparen/rparen collapsed,
// start/begin/end made interchangeable, -open/-close paired -- is reproduced
// here as a normalisation and a comparison, which is the same thing without a
// regex compiled per click.
std::string normaliseType(const std::string &t) {
    std::string s = t;
    auto replaceAll = [&s](const std::string &from, const std::string &to) {
        if (from.empty()) return;
        size_t p = 0;
        while ((p = s.find(from, p)) != std::string::npos) {
            s.replace(p, from.size(), to);
            p += to.size();
        }
    };
    replaceAll("lparen", "paren");
    replaceAll("rparen", "paren");
    replaceAll("-open", "-x");
    replaceAll("-close", "-x");
    replaceAll("start", "*");
    replaceAll("begin", "*");
    replaceAll("end", "*");
    return s;
}

bool sameFamily(const std::string &a, const std::string &b) {
    return normaliseType(a) == normaliseType(b);
}

bool isCommentType(const std::string &t) { return t.compare(0, 7, "comment") == 0; }
bool isStringType(const std::string &t) { return t.compare(0, 6, "string") == 0; }

// Type of the token covering `column`, or "" when there are no tokens at all.
std::string typeAt(const FoldSource &src, int row, int column) {
    const std::vector<TokenSpan> &ts = src.tokens(row);
    for (const auto &t : ts)
        if (column >= t.start && column < t.start + t.length) return t.type;
    return std::string();
}

// Type of the first token on a row that is not pure whitespace.
std::string firstRealTokenType(const FoldSource &src, int row) {
    const std::vector<TokenSpan> &ts = src.tokens(row);
    const std::string &line = src.line(row);
    for (const auto &t : ts) {
        bool blank = true;
        for (int i = t.start; i < t.start + t.length && i < static_cast<int>(line.size()); ++i)
            if (!isSpace(line[static_cast<size_t>(i)])) { blank = false; break; }
        if (!blank) return t.type;
    }
    return std::string();
}

}  // namespace

// --- shared pieces ---------------------------------------------------------

int indentOf(const std::string &line) {
    for (size_t i = 0; i < line.size(); ++i)
        if (!isSpace(line[i])) return static_cast<int>(i);
    return -1;
}

namespace {

// One pass of the bracket walk, in either direction. `step` is +1 or -1.
bool walkBrackets(const FoldSource &src, char bracket, int row, int column, int step,
                  int *outRow, int *outColumn) {
    const char other = matching(bracket);
    if (!other) return false;

    const bool haveTokens = !src.tokens(row).empty();
    const std::string anchor = haveTokens ? typeAt(src, row, column) : std::string();
    // A bracket the tokenizer put inside a string is not structure. Ace reaches
    // the same conclusion by never finding a matching token; saying so here is
    // cheaper and easier to read.
    if (haveTokens && isStringType(anchor)) return false;

    int depth = 1;
    int r = row;
    int c = column + step;
    const int maxRow = src.lineCount();

    while (r >= 0 && r < maxRow) {
        const std::string &line = src.line(r);
        const int len = static_cast<int>(line.size());
        if (step > 0 && c < 0) c = 0;
        if (step < 0 && c >= len) c = len - 1;

        while (c >= 0 && c < len) {
            const char ch = line[static_cast<size_t>(c)];
            if (ch == bracket || ch == other) {
                bool eligible = true;
                if (haveTokens) {
                    const std::string t = typeAt(src, r, c);
                    // Empty type means the tokenizer produced nothing covering
                    // that byte, which happens on a row it has no rule for.
                    // Treat it as eligible rather than dropping the bracket.
                    eligible = t.empty() || sameFamily(t, anchor);
                }
                if (eligible) {
                    if (ch == other) {
                        if (--depth == 0) {
                            *outRow = r;
                            *outColumn = c;
                            return true;
                        }
                    } else {
                        ++depth;
                    }
                }
            }
            c += step;
        }

        r += step;
        if (r < 0 || r >= maxRow) break;
        c = step > 0 ? 0 : static_cast<int>(src.line(r).size()) - 1;
        if (step < 0 && c < 0) {
            // Empty row going backwards: skip it rather than spinning.
            continue;
        }
    }
    return false;
}

}  // namespace

bool findClosingBracket(const FoldSource &src, char open, int row, int column,
                        int *endRow, int *endColumn) {
    if (!isOpenBracket(open)) return false;
    return walkBrackets(src, open, row, column, +1, endRow, endColumn);
}

bool findOpeningBracket(const FoldSource &src, char close, int row, int column,
                        int *startRow, int *startColumn) {
    if (!isCloseBracket(close)) return false;
    return walkBrackets(src, close, row, column, -1, startRow, startColumn);
}

bool indentationBlock(const FoldSource &src, int row, int startColumn, FoldRange *out) {
    const std::string &first = src.line(row);
    const int startLevel = indentOf(first);
    if (startLevel == -1) return false;

    const int maxRow = src.lineCount();
    int endRow = row;
    for (int r = row + 1; r < maxRow; ++r) {
        const int level = indentOf(src.line(r));
        if (level == -1) continue;  // blank lines do not end a block
        if (level <= startLevel) {
            // ...unless the row is inside a string, where indentation means
            // nothing. Ace checks the token at column 0 for exactly this.
            const std::string t = typeAt(src, r, 0);
            if (!isStringType(t)) break;
        }
        endRow = r;
    }
    if (endRow <= row) return false;
    out->startRow = row;
    out->startColumn = startColumn >= 0 ? startColumn : static_cast<int>(first.size());
    out->endRow = endRow;
    out->endColumn = static_cast<int>(src.line(endRow).size());
    return true;
}

// --- cstyle ----------------------------------------------------------------
//
// Ace's markers, hand-coded rather than compiled:
//
//   foldingStartMarker  /([\{\[\(])[^\}\]\)]*$|^\s*(\/\*)/
//   foldingStopMarker   /^[^\[\{\(]*([\}\]\)])|^[\s\*]*(\*\/)/
//   singleLineBlockCommentRe  /^\s*(\/\*).*\*\/\s*$/
//   tripleStarBlockCommentRe  /^\s*(\/\*\*\*).*\*\/\s*$/
//   startRegionRe             /^\s*(\/\*|\/\/)#?region\b/
//
// The gutter calls widget() for every visible row on every frame. Five PCRE2
// matches per row per frame is not a disaster, but it is five more than this
// needs, and the JS-to-PCRE2 dialect risk is not worth taking on a regex this
// small.

namespace {

struct StartMatch {
    bool found = false;
    int index = 0;
    char bracket = 0;    // set for the bracket alternative
    int afterComment = 0;  // byte after "/*", set for the comment alternative
};

// The leftmost match, with the alternation resolved the way JS resolves it:
// at each position the bracket alternative is tried first, and the comment
// alternative is anchored at the start of the line.
StartMatch cstyleStart(const std::string &line, const std::string &blockStart) {
    StartMatch m;
    const int len = static_cast<int>(line.size());

    // "([{[(])[^}\])]*$" matches at i iff line[i] opens and no closer follows.
    int lastClose = -1;
    for (int i = len - 1; i >= 0; --i)
        if (isCloseBracket(line[static_cast<size_t>(i)])) { lastClose = i; break; }

    int firstBracket = -1;
    for (int i = lastClose + 1; i < len; ++i)
        if (isOpenBracket(line[static_cast<size_t>(i)])) { firstBracket = i; break; }

    if (firstBracket == 0) {
        m.found = true;
        m.index = 0;
        m.bracket = line[0];
        return m;
    }

    const int ind = indentOf(line);
    if (ind >= 0 && startsWithAt(line, ind, blockStart.c_str())) {
        m.found = true;
        m.index = ind;
        m.afterComment = ind + static_cast<int>(blockStart.size());
        return m;
    }

    if (firstBracket > 0) {
        m.found = true;
        m.index = firstBracket;
        m.bracket = line[static_cast<size_t>(firstBracket)];
    }
    return m;
}

// "^\s*(\/\*|\/\/)#?region\b" -- and the endregion form, which also allows --.
bool regionMarker(const std::string &line, bool *isEnd) {
    const int ind = indentOf(line);
    if (ind < 0) return false;
    int i = ind;
    if (startsWithAt(line, i, "/*") || startsWithAt(line, i, "//"))
        i += 2;
    else if (startsWithAt(line, i, "--"))
        i += 2;
    else
        return false;
    if (i < static_cast<int>(line.size()) && line[static_cast<size_t>(i)] == '#') ++i;
    bool end = false;
    if (startsWithAt(line, i, "end")) {
        end = true;
        i += 3;
    }
    if (!startsWithAt(line, i, "region")) return false;
    i += 6;
    if (i < static_cast<int>(line.size()) && isWordChar(line[static_cast<size_t>(i)]))
        return false;  // \b
    if (isEnd) *isEnd = end;
    return true;
}

// "^\s*(\/\*|\/\/)#?region\b" only -- the start form, which does not take --.
bool startRegion(const std::string &line) {
    const int ind = indentOf(line);
    if (ind < 0) return false;
    if (!startsWithAt(line, ind, "/*") && !startsWithAt(line, ind, "//")) return false;
    bool end = false;
    if (!regionMarker(line, &end)) return false;
    return !end;
}

bool singleLineBlockComment(const std::string &line, const std::string &bs,
                            const std::string &be) {
    const int ind = indentOf(line);
    if (ind < 0 || !startsWithAt(line, ind, bs.c_str())) return false;
    const int last = lastNonSpace(line);
    const int n = static_cast<int>(be.size());
    if (last - n + 1 < ind + static_cast<int>(bs.size())) return false;
    return startsWithAt(line, last - n + 1, be.c_str());
}

bool tripleStarComment(const std::string &line) {
    const int ind = indentOf(line);
    if (ind < 0 || !startsWithAt(line, ind, "/***")) return false;
    const int last = lastNonSpace(line);
    return last >= ind + 4 && startsWithAt(line, last - 1, "*/");
}

class CStyleFoldMode : public FoldMode {
public:
    CStyleFoldMode(std::string blockStart, std::string blockEnd)
        : bs_(std::move(blockStart)), be_(std::move(blockEnd)) {}

    FoldWidget widget(const FoldSource &src, int row) const override {
        if (row < 0 || row >= src.lineCount()) return FoldWidget::None;
        const std::string &line = src.line(row);

        if (singleLineBlockComment(line, bs_, be_)) {
            // A comment opened and closed on one line folds nothing, unless it
            // is a region marker or ace's three-star convention.
            if (!startRegion(line) && !tripleStarComment(line)) return FoldWidget::None;
        }
        if (cstyleStart(line, bs_).found) return FoldWidget::Start;
        if (startRegion(line)) return FoldWidget::Start;
        return FoldWidget::None;
    }

    bool range(const FoldSource &src, int row, FoldRange *out) const override {
        if (row < 0 || row >= src.lineCount()) return false;
        const std::string &line = src.line(row);

        if (startRegion(line)) return regionBlock(src, row, out);

        const StartMatch m = cstyleStart(line, bs_);
        if (!m.found) return false;

        if (m.bracket) return bracketBlock(src, m.bracket, row, m.index, out);
        if (singleLineBlockComment(line, bs_, be_)) return false;
        return commentBlock(src, row, m.index, out);
    }

private:
    // ace fold_mode.js openingBracketBlock
    bool bracketBlock(const FoldSource &src, char bracket, int row, int column,
                      FoldRange *out) const {
        int er = 0, ec = 0;
        if (!findClosingBracket(src, bracket, row, column, &er, &ec)) return false;
        out->startRow = row;
        out->startColumn = column + 1;
        out->endRow = er;
        out->endColumn = ec;
        // "} else {" opens a fold of its own. Stopping a row short lets the two
        // nest instead of overlapping, which is ace's rule and the reason an
        // if/else chain folds the way a reader expects.
        if (er > row && widget(src, er) == FoldWidget::Start) {
            out->endRow = er - 1;
            out->endColumn = static_cast<int>(src.line(er - 1).size());
        }
        return out->multiLine();
    }

    // ace folding.js getCommentFoldRange, dir = 1, at row granularity: extend
    // while the row still opens with a comment token of the same family.
    bool commentBlock(const FoldSource &src, int row, int column, FoldRange *out) const {
        const std::string anchor = typeAt(src, row, column);
        const bool haveTokens = !src.tokens(row).empty();
        int endRow = row;
        for (int r = row + 1; r < src.lineCount(); ++r) {
            if (haveTokens) {
                const std::string t = firstRealTokenType(src, r);
                if (t.empty() || !isCommentType(t)) break;
                if (!anchor.empty() && !isCommentType(anchor)) break;
            } else {
                // No tokenizer: close on the first row carrying the block-comment
                // terminator, which is all plain text can tell us.
                endRow = r;
                if (src.line(r).find(be_) != std::string::npos) break;
                continue;
            }
            endRow = r;
        }
        if (endRow <= row) return false;
        out->startRow = row;
        out->startColumn = static_cast<int>(src.line(row).size());
        out->endRow = endRow;
        out->endColumn = static_cast<int>(src.line(endRow).size());
        return true;
    }

    // ace cstyle.js getCommentRegionBlock
    bool regionBlock(const FoldSource &src, int row, FoldRange *out) const {
        int depth = 1;
        int r = row;
        const int maxRow = src.lineCount();
        while (++r < maxRow) {
            bool end = false;
            if (!regionMarker(src.line(r), &end)) continue;
            depth += end ? -1 : 1;
            if (depth == 0) break;
        }
        if (r >= maxRow || r <= row) return false;
        const std::string &startLine = src.line(row);
        out->startRow = row;
        out->startColumn = lastNonSpace(startLine) + 1;
        out->endRow = r;
        out->endColumn = static_cast<int>(src.line(r).size());
        return true;
    }

    std::string bs_, be_;
};

// --- indentation (ace folding/coffee.js) -----------------------------------

class IndentFoldMode : public FoldMode {
public:
    FoldWidget widget(const FoldSource &src, int row) const override {
        if (row < 0 || row >= src.lineCount()) return FoldWidget::None;
        const std::string &line = src.line(row);
        const int indent = indentOf(line);
        if (indent == -1) return FoldWidget::None;

        const int next = nextNonBlank(src, row);
        // Ace decides this against the immediately following line and patches
        // its neighbours' cached answers when that line is blank. Looking past
        // the blanks directly is the same intent without the side channel, and
        // is right where ace is wrong: several blank lines before an indented
        // block left the widget on none of them.
        if (next != -1 && indentOf(src.line(next)) > indent) return FoldWidget::Start;

        // A run of comment lines at one indent folds as a block. Only its first
        // row gets the widget.
        if (line[static_cast<size_t>(indent)] == '#') {
            const int prev = prevNonBlank(src, row);
            const bool prevIsComment =
                prev != -1 && indentOf(src.line(prev)) == indent &&
                src.line(prev)[static_cast<size_t>(indent)] == '#';
            const bool nextIsComment =
                next != -1 && indentOf(src.line(next)) == indent &&
                src.line(next)[static_cast<size_t>(indent)] == '#';
            if (!prevIsComment && nextIsComment) return FoldWidget::Start;
        }
        return FoldWidget::None;
    }

    bool range(const FoldSource &src, int row, FoldRange *out) const override {
        if (row < 0 || row >= src.lineCount()) return false;
        if (indentationBlock(src, row, -1, out)) return true;
        return commentBlock(src, row, out);
    }

private:
    static int nextNonBlank(const FoldSource &src, int row) {
        for (int r = row + 1; r < src.lineCount(); ++r)
            if (indentOf(src.line(r)) != -1) return r;
        return -1;
    }
    static int prevNonBlank(const FoldSource &src, int row) {
        for (int r = row - 1; r >= 0; --r)
            if (indentOf(src.line(r)) != -1) return r;
        return -1;
    }

    static bool commentBlock(const FoldSource &src, int row, FoldRange *out) {
        const std::string &line = src.line(row);
        const int level = indentOf(line);
        if (level == -1 || line[static_cast<size_t>(level)] != '#') return false;
        int endRow = row;
        for (int r = row + 1; r < src.lineCount(); ++r) {
            const std::string &l = src.line(r);
            const int lv = indentOf(l);
            if (lv == -1) continue;
            if (l[static_cast<size_t>(lv)] != '#') break;
            endRow = r;
        }
        if (endRow <= row) return false;
        out->startRow = row;
        out->startColumn = static_cast<int>(line.size());
        out->endRow = endRow;
        out->endColumn = static_cast<int>(src.line(endRow).size());
        return true;
    }
};

// --- pythonic (ace folding/pythonic.js) ------------------------------------
//
//   foldingStartMarker = /([\[{])(?:\s*)$|(\:)(?:\s*)(?:#.*)?$/
//
// A trailing bracket takes the bracket path; a trailing ':' -- optionally
// followed by a comment -- takes the indentation path.

class PythonicFoldMode : public FoldMode {
public:
    FoldWidget widget(const FoldSource &src, int row) const override {
        if (row < 0 || row >= src.lineCount()) return FoldWidget::None;
        char br = 0;
        int col = 0;
        return match(src.line(row), &br, &col) ? FoldWidget::Start : FoldWidget::None;
    }

    bool range(const FoldSource &src, int row, FoldRange *out) const override {
        if (row < 0 || row >= src.lineCount()) return false;
        char br = 0;
        int col = 0;
        if (!match(src.line(row), &br, &col)) return false;
        if (br) {
            int er = 0, ec = 0;
            if (!findClosingBracket(src, br, row, col, &er, &ec)) return false;
            out->startRow = row;
            out->startColumn = col + 1;
            out->endRow = er;
            out->endColumn = ec;
            return out->multiLine();
        }
        return indentationBlock(src, row, col + 1, out);
    }

private:
    // Sets `bracket` for the bracket alternative, or leaves it 0 and sets
    // `column` to the index of the ':' for the indentation one.
    static bool match(const std::string &line, char *bracket, int *column) {
        const int last = lastNonSpace(line);
        if (last < 0) return false;
        const char c = line[static_cast<size_t>(last)];
        if (c == '[' || c == '{') {
            *bracket = c;
            *column = last;
            return true;
        }
        if (c == ':') {
            *column = last;
            return true;
        }
        // ':' followed by a trailing comment. Ace allows "#.*" only, and only
        // where the '#' is not itself inside a string -- which it cannot check
        // from the line alone either.
        const size_t hash = line.find('#');
        if (hash == std::string::npos) return false;
        int i = static_cast<int>(hash) - 1;
        while (i >= 0 && isSpace(line[static_cast<size_t>(i)])) --i;
        if (i >= 0 && line[static_cast<size_t>(i)] == ':') {
            *column = i;
            return true;
        }
        return false;
    }
};

// --- the table -------------------------------------------------------------

enum class Kind { CStyle, Indent, Pythonic };

const FoldMode *modeFor(Kind k) {
    // ace passes {start,end} to CStyleFoldMode for three modes (perl, raku,
    // powershell). Their markers are anchored differently from "/*", so those
    // modes are mapped to plain cstyle here and get bracket folding without
    // POD/comment-block folding. Recorded rather than silently dropped.
    static const CStyleFoldMode cstyle("/*", "*/");
    static const IndentFoldMode indent;
    static const PythonicFoldMode pythonic;
    switch (k) {
        case Kind::CStyle: return &cstyle;
        case Kind::Indent: return &indent;
        case Kind::Pythonic: return &pythonic;
    }
    return nullptr;
}

const std::unordered_map<std::string, Kind> &table() {
    static const std::unordered_map<std::string, Kind> t = {
#include "foldtable.inc"
    };
    return t;
}

}  // namespace

int DocumentFoldSource::lineCount() const { return doc_ ? doc_->lineCount() : 0; }

const std::string &DocumentFoldSource::line(int row) const {
    static const std::string kEmpty;
    if (!doc_ || row < 0 || row >= doc_->lineCount()) return kEmpty;
    return doc_->line(row);
}

const std::vector<TokenSpan> &DocumentFoldSource::tokens(int row) const {
    static const std::vector<TokenSpan> kEmpty;
    return chain_ ? chain_->tokens(row) : kEmpty;
}

const FoldMode *foldModeFor(const std::string &mode) {
    const auto &t = table();
    auto hit = t.find(mode);
    return hit == t.end() ? nullptr : modeFor(hit->second);
}

}  // namespace aced
