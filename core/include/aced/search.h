// include/aced/search.h
//
// Find and replace over a Document. No Qt, so it tests headlessly and at speed.
//
// MATCHES DO NOT SPAN LINES. The document is a vector of lines and a search is
// run against one line at a time, so a pattern cannot match across a newline
// and `.` never has to be told not to. That is a real limitation -- a regex for
// a multi-line block comment will not find one -- and it is stated here rather
// than half-supported. Every editor this is modelled on made the same choice
// first and added multi-line search later as a separate path.
//
// Columns are byte offsets, as everywhere else in aced, and PCRE2 matches in
// bytes, so the two agree without conversion. A match therefore always lands on
// a UTF-8 boundary as long as the pattern does.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "aced/delta.h"

namespace aced {

class Document;

struct SearchOptions {
    bool caseSensitive = false;
    bool wholeWord = false;
    // When false the needle is taken literally: every metacharacter is escaped
    // before it reaches PCRE2, so searching for "a.b" finds "a.b" and not "axb".
    bool regex = false;
    // next()/previous() continue from the other end of the document when they
    // run out. Off, they return nothing at the end.
    bool wrap = true;
};

class Search {
public:
    Search(const std::string &needle, const SearchOptions &opts);
    ~Search();

    Search(const Search &) = delete;
    Search &operator=(const Search &) = delete;

    // False for an empty needle or a regex that will not compile. An empty
    // needle is deliberately invalid rather than "matches everywhere": a
    // zero-width match at every position is what turns a Find Next into an
    // infinite loop.
    bool valid() const;
    const std::string &error() const;

    const std::string &needle() const { return needle_; }
    const SearchOptions &options() const { return opts_; }

    // The first match at or after `from`, and the last one strictly before it.
    // `found` says whether there was one; the Range is meaningless when false.
    Range next(const Document &doc, Position from, bool *found) const;
    Range previous(const Document &doc, Position from, bool *found) const;

    // Every match, in document order. Capped: a match list is used to paint
    // highlights, and painting is bounded by the viewport while a file is not.
    // When `total` is given the scan keeps going past the cap and counts,
    // without storing, so a caller can report the real number.
    static constexpr size_t kDefaultLimit = 20000;
    std::vector<Range> all(const Document &doc, size_t limit = kDefaultLimit,
                           size_t *total = nullptr) const;

    // Every match, uncapped, for a caller running on another thread. Checks
    // `cancel` every 1024 rows and returns an empty list when it is set, and
    // publishes the rows scanned so far to `rowsDone` for a progress bar.
    //
    // Reads the Document and nothing else: safe beside other readers, NOT
    // beside an edit. The caller has to keep the document still -- a modal
    // dialog does. Use a Search of the caller's own; one Search is not safe
    // to share between threads.
    std::vector<Range> collect(const Document &doc, const std::atomic<bool> &cancel,
                               std::atomic<int> *rowsDone = nullptr) const;

    // Replaces every match. The replacement is LITERAL -- "$1" inserts a dollar
    // and a one, it does not interpolate a capture group. Half-supporting
    // backreferences is worse than not supporting them, because the failure is
    // a wrong document rather than an error message.
    //
    // Applied back to front so that earlier matches keep the offsets they were
    // found at, and through Document::replace() so every edit produces a Delta
    // and undo sees all of them.
    int replaceAll(Document &doc, const std::string &replacement) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    std::string needle_;
    SearchOptions opts_;
};

// Exposed for testing and for anything that needs to build a literal pattern.
std::string escapeRegex(const std::string &literal);

}  // namespace aced