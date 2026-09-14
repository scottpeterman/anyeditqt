// include/aced/tokenchain.h
//
// Tokens for a row, on demand, with the tokenizer's start state carried down
// from the row above.
//
// acedqt::LineCache keeps the same state chain, but throws the tokens away --
// it converts them straight to QTextCharFormat runs and keeps only the end
// state. Folding needs the tokens themselves: whether a brace is a real brace
// or one inside a comment is the difference between a fold that works and one
// that swallows the rest of the file.
//
// COLD BY DESIGN. The gutter decides whether a row has a fold widget from the
// line text alone -- no tokens. Tokens are read only when a fold range is
// actually computed, which is on a click. So this chain stays empty in normal
// use and the duplication with LineCache costs nothing. If that stops being
// true, LineCache should be rebased onto this rather than the other way round:
// this half is Qt-free.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace aced {

class Document;
class Tokenizer;

struct TokenSpan {
    std::string type;
    int start = 0;   // byte offset into the row
    int length = 0;  // bytes
};

class TokenChain {
public:
    TokenChain();
    ~TokenChain();

    // Neither is owned; both must outlive the chain.
    void setDocument(const Document *doc);
    void setTokenizer(const Tokenizer *tk);

    // Rows [row, end) are gone.
    void invalidateFrom(int row);
    void invalidateAll();

    // Tokens for `row`, byte offsets into that row's text. Empty when there is
    // no tokenizer or the row is out of range -- callers must cope, because
    // plain text is a mode like any other.
    const std::vector<TokenSpan> &tokens(int row) const;

    // Type of the token covering byte `column` of `row`, or "" if there is
    // none. Column == line length asks about the token that ends there.
    std::string typeAt(int row, int column) const;

private:
    // State at the start of `row`, tokenizing whatever is needed to know it.
    void ensureStateFor(int row) const;

    struct Row {
        std::vector<TokenSpan> tokens;
        std::vector<std::string> endStack;
        std::string endState;
        bool tokensValid = false;
    };

    const Document *doc_ = nullptr;
    const Tokenizer *tk_ = nullptr;
    mutable std::unordered_map<int, Row> rows_;
    mutable int contiguousTo_ = -1;  // rows 0..contiguousTo_ have a valid end state
};

}  // namespace aced
