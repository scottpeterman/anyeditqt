// src/tokenchain.cpp
#include "aced/tokenchain.h"

#include <algorithm>

#include "aced/document.h"
#include "aced/tokenizer.h"

namespace aced {

namespace {
const std::vector<TokenSpan> &emptySpans() {
    static const std::vector<TokenSpan> v;
    return v;
}
const std::vector<std::string> &emptyStack() {
    static const std::vector<std::string> v;
    return v;
}
}  // namespace

TokenChain::TokenChain() = default;
TokenChain::~TokenChain() = default;

void TokenChain::setDocument(const Document *doc) {
    doc_ = doc;
    invalidateAll();
}

void TokenChain::setTokenizer(const Tokenizer *tk) {
    tk_ = tk;
    invalidateAll();
}

void TokenChain::invalidateAll() {
    rows_.clear();
    contiguousTo_ = -1;
}

void TokenChain::invalidateFrom(int row) {
    if (row <= 0) {
        invalidateAll();
        return;
    }
    for (auto it = rows_.begin(); it != rows_.end();) {
        if (it->first >= row)
            it = rows_.erase(it);
        else
            ++it;
    }
    contiguousTo_ = std::min(contiguousTo_, row - 1);
}

void TokenChain::ensureStateFor(int row) const {
    if (!tk_ || !doc_) return;
    int from = std::max(0, contiguousTo_ + 1);
    for (int r = from; r < row; ++r) {
        auto hit = rows_.find(r);
        if (hit != rows_.end() && !hit->second.endState.empty()) {
            contiguousTo_ = r;
            continue;
        }
        const std::vector<std::string> &in =
            (r == 0) ? emptyStack() : rows_[r - 1].endStack;
        const std::string inState =
            (r == 0) ? std::string("start") : rows_[r - 1].endState;
        auto res = tk_->tokenize(doc_->line(r), inState, in);
        Row &slot = rows_[r];
        slot.endStack = res.stack;
        slot.endState = res.state;
        contiguousTo_ = r;
    }
}

const std::vector<TokenSpan> &TokenChain::tokens(int row) const {
    if (!tk_ || !doc_) return emptySpans();
    if (row < 0 || row >= doc_->lineCount()) return emptySpans();

    auto hit = rows_.find(row);
    if (hit != rows_.end() && hit->second.tokensValid) return hit->second.tokens;

    ensureStateFor(row);

    const std::vector<std::string> &in =
        (row == 0) ? emptyStack() : rows_[row - 1].endStack;
    const std::string inState =
        (row == 0) ? std::string("start") : rows_[row - 1].endState;

    const std::string &text = doc_->line(row);
    auto res = tk_->tokenize(text, inState, in);

    Row &slot = rows_[row];
    slot.endStack = res.stack;
    slot.endState = res.state;
    slot.tokens.clear();
    slot.tokens.reserve(res.tokens.size());

    // Byte offsets, from the token values laid end to end. Clamped to the line
    // because a grammar with a zero-width rule can, in principle, emit more
    // than the line holds; a span running past the end would index out of the
    // string in every caller below rather than in one place here.
    int off = 0;
    const int len = static_cast<int>(text.size());
    for (const auto &t : res.tokens) {
        if (off >= len) break;
        int n = static_cast<int>(t.value.size());
        if (off + n > len) n = len - off;
        slot.tokens.push_back({t.type, off, n});
        off += n;
    }
    slot.tokensValid = true;
    if (contiguousTo_ == row - 1) contiguousTo_ = row;
    return slot.tokens;
}

std::string TokenChain::typeAt(int row, int column) const {
    const std::vector<TokenSpan> &ts = tokens(row);
    for (const auto &t : ts)
        if (column >= t.start && column < t.start + t.length) return t.type;
    if (!ts.empty() && column >= ts.back().start) return ts.back().type;
    return std::string();
}

}  // namespace aced
