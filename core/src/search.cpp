// src/search.cpp
#include "aced/search.h"

#include <algorithm>
#include <limits>

#include "aced/document.h"
#include "aced/regex.h"

namespace aced {
namespace {

// Matches within one line, as byte columns.
struct LineMatch {
    int start;
    int end;
};

}  // namespace

std::string escapeRegex(const std::string &literal) {
    // Escaped one character at a time rather than wrapped in \Q...\E. \Q is
    // terminated by the first \E in the subject, so a literal needle that
    // happens to contain "\E" -- which a search through this very file would --
    // would end the quoting early and hand the rest to PCRE2 as a pattern.
    static const std::string special = "\\^$.[]|()*+?{}-/";
    std::string out;
    out.reserve(literal.size() * 2);
    for (char c : literal) {
        if (special.find(c) != std::string::npos) out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

struct Search::Impl {
    std::unique_ptr<Regex> re;
    std::string error;

    // All matches on one line, left to right.
    std::vector<LineMatch> matchesIn(const std::string &line) const {
        std::vector<LineMatch> out;
        if (!re) return out;
        size_t offset = 0;
        while (offset <= line.size()) {
            const Regex::Match m = re->search(line, offset);
            if (!m.matched) break;
            const int s = static_cast<int>(m.start());
            const int e = static_cast<int>(m.end());
            out.push_back({s, e});
            // A ZERO-LENGTH MATCH DOES NOT ADVANCE THE OFFSET, so "x*" or "^"
            // would loop here forever, silently, on a line of any length. The
            // step past it is what makes a pattern that can match nothing safe
            // to run rather than something the caller has to check for.
            offset = (e > s) ? static_cast<size_t>(e) : static_cast<size_t>(s) + 1;
            if (out.size() > 100000) break;  // one pathological line, not a hang
        }
        return out;
    }
};

Search::Search(const std::string &needle, const SearchOptions &opts)
    : d_(new Impl), needle_(needle), opts_(opts) {
    if (needle.empty()) {
        d_->error = "empty search";
        return;
    }
    std::string pattern = opts.regex ? needle : escapeRegex(needle);
    if (opts.wholeWord) {
        // Non-capturing, so a caller's own groups keep their numbers, and \b on
        // both ends rather than one.
        pattern = "\\b(?:" + pattern + ")\\b";
    }
    auto re = std::make_unique<Regex>(pattern, !opts.caseSensitive, true);
    if (!re->valid()) {
        d_->error = re->error();
        return;
    }
    d_->re = std::move(re);
}

Search::~Search() = default;

bool Search::valid() const { return d_->re != nullptr; }
const std::string &Search::error() const { return d_->error; }

Range Search::next(const Document &doc, Position from, bool *found) const {
    if (found) *found = false;
    if (!valid()) return {};
    from = doc.clamp(from);

    // The starting row twice when wrapping: once from the cursor column to the
    // end of the line, and again at the very end from column 0 up to it. A
    // single pass over rows would miss a match earlier on the cursor's own row.
    const int rows = doc.lineCount();
    for (int i = 0; i < rows; ++i) {
        const int row = from.row + i;
        if (row >= rows) break;
        const int lower = (i == 0) ? from.column : 0;
        for (const LineMatch &m : d_->matchesIn(doc.line(row))) {
            if (m.start >= lower) {
                if (found) *found = true;
                return {{row, m.start}, {row, m.end}};
            }
        }
    }
    if (!opts_.wrap) return {};
    for (int row = 0; row <= from.row && row < rows; ++row) {
        for (const LineMatch &m : d_->matchesIn(doc.line(row))) {
            // Strictly before where we started, or a wrap would hand back the
            // match the cursor is already sitting on as though it were new.
            if (row < from.row || m.start < from.column) {
                if (found) *found = true;
                return {{row, m.start}, {row, m.end}};
            }
        }
    }
    return {};
}

Range Search::previous(const Document &doc, Position from, bool *found) const {
    if (found) *found = false;
    if (!valid()) return {};
    from = doc.clamp(from);

    const int rows = doc.lineCount();
    for (int i = 0; i < rows; ++i) {
        const int row = from.row - i;
        if (row < 0) break;
        const std::vector<LineMatch> ms = d_->matchesIn(doc.line(row));
        for (auto it = ms.rbegin(); it != ms.rend(); ++it) {
            if (i > 0 || it->start < from.column) {
                if (found) *found = true;
                return {{row, it->start}, {row, it->end}};
            }
        }
    }
    if (!opts_.wrap) return {};
    for (int row = rows - 1; row >= from.row && row >= 0; --row) {
        const std::vector<LineMatch> ms = d_->matchesIn(doc.line(row));
        for (auto it = ms.rbegin(); it != ms.rend(); ++it) {
            if (row > from.row || it->start >= from.column) {
                if (found) *found = true;
                return {{row, it->start}, {row, it->end}};
            }
        }
    }
    return {};
}

std::vector<Range> Search::all(const Document &doc, size_t limit, size_t *total) const {
    std::vector<Range> out;
    size_t count = 0;
    if (total) *total = 0;
    if (!valid()) return out;
    for (int row = 0; row < doc.lineCount(); ++row) {
        const auto ms = d_->matchesIn(doc.line(row));
        if (out.size() < limit) {
            for (const LineMatch &m : ms) {
                if (out.size() >= limit) break;
                out.push_back({{row, m.start}, {row, m.end}});
            }
        } else if (!total) {
            return out;
        }
        count += ms.size();
    }
    if (total) *total = count;
    return out;
}

std::vector<Range> Search::collect(const Document &doc, const std::atomic<bool> &cancel,
                                  std::atomic<int> *rowsDone) const {
    std::vector<Range> out;
    if (!valid()) return out;
    const int n = doc.lineCount();
    for (int row = 0; row < n; ++row) {
        if ((row & 1023) == 0) {
            if (cancel.load(std::memory_order_relaxed)) return {};
            if (rowsDone) rowsDone->store(row, std::memory_order_relaxed);
        }
        for (const LineMatch &m : d_->matchesIn(doc.line(row)))
            out.push_back({{row, m.start}, {row, m.end}});
    }
    if (rowsDone) rowsDone->store(n, std::memory_order_relaxed);
    return out;
}

int Search::replaceAll(Document &doc, const std::string &replacement) const {
    if (!valid()) return 0;
    const std::vector<Range> ranges = all(doc, std::numeric_limits<size_t>::max());
    // BACK TO FRONT. Replacing left to right shifts every later match by the
    // difference in length, so the second replacement lands in the wrong place
    // and the damage compounds down the line.
    for (auto it = ranges.rbegin(); it != ranges.rend(); ++it) {
        doc.replace(*it, replacement);
    }
    return static_cast<int>(ranges.size());
}

}  // namespace aced