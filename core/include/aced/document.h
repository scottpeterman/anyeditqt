// include/aced/document.h
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "aced/delta.h"

namespace aced {

class Anchor;

// The text. A vector of lines, one string per line, and applyDelta() as the
// only thing that changes it.
//
// There is always at least one line. An empty document is one empty line, so
// lineCount() is never 0 and every Position in [0,0]..[lineCount()-1, len] is
// addressable without a special case.
//
// Columns are byte offsets into the line, not code points. That is a deliberate
// choice and it is the same one ace makes: the tokenizer hands back byte spans,
// PCRE2 matches in bytes, and a view layer that needs grapheme boundaries
// already has to ask the shaper rather than the model. Callers converting from
// a mouse position must clamp to a UTF-8 boundary themselves; see utf8Floor().
//
// Deltas are broadcast to two kinds of observer. Anchors are registered objects
// the document shifts for you (see anchor.h). Listeners are plain callbacks for
// everything else -- undo, tokenizer invalidation, the view's dirty-row set.
// Both run inside applyDelta(), after the line store is already consistent, so
// a listener may read the document but must not mutate it.
class Document {
public:
    Document();
    explicit Document(const std::string &text);
    ~Document();

    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;

    // --- reading ---------------------------------------------------------

    int lineCount() const { return static_cast<int>(lines_.size()); }
    const std::string &line(int row) const;
    const std::vector<std::string> &lines() const { return lines_; }

    // Length of `row` in bytes. Out-of-range rows report 0.
    int lineLength(int row) const;

    // Position of the very end of the document.
    Position end() const;

    // Clamp a position into the document: row into [0, lineCount()-1], then
    // column into [0, lineLength(row)].
    Position clamp(Position p) const;

    std::string text() const;
    std::string textInRange(const Range &r) const;

    // Byte offset <-> Position. Offsets count the '\n' separators.
    int positionToOffset(Position p) const;
    Position offsetToPosition(int offset) const;

    // --- writing ---------------------------------------------------------
    //
    // These are conveniences that build a Delta and hand it to applyDelta().
    // They return the Delta so a caller that is recording history does not have
    // to reconstruct it.

    Delta insert(Position at, const std::string &text);
    Delta remove(const Range &range);
    Delta replace(const Range &range, const std::string &text);

    // The primitive. Everything above funnels here.
    void applyDelta(const Delta &delta);

    // Apply in reverse. Equivalent to applyDelta(delta.inverted()) and exists
    // so undo reads as what it is at the call site.
    void revertDelta(const Delta &delta) { applyDelta(delta.inverted()); }

    void setText(const std::string &text);

    // --- observers -------------------------------------------------------

    using Listener = std::function<void(const Delta &)>;
    using ListenerId = int;

    ListenerId addListener(Listener fn);
    void removeListener(ListenerId id);

    // --- helpers ---------------------------------------------------------

    // Move `column` back to the start of the UTF-8 sequence it lands inside.
    // A column already on a boundary, at 0, or past the end is returned as-is.
    static int utf8Floor(const std::string &line, int column);

private:
    friend class Anchor;
    void attach(Anchor *a);
    void detach(Anchor *a);

    void applyInsert(const Delta &d);
    void applyRemove(const Delta &d);

    std::vector<std::string> lines_;
    std::vector<Anchor *> anchors_;
    std::vector<std::pair<ListenerId, Listener>> listeners_;
    ListenerId nextListenerId_ = 1;
};

}  // namespace aced
