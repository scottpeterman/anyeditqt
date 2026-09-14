// include/aced/linemap.h
//
// Document rows to display lines, and back.
//
// ONE structure for wrapping and folding, not two. A row occupies `height`
// display lines (wrapping) unless it is hidden, in which case it occupies none
// (folding). Everything downstream -- the scrollbar, hit testing, Up/Down,
// PageUp/PageDown -- asks this one object and gets one answer.
//
// The alternative is a wrap map and a fold map that have to agree, and they
// eventually don't: fold a block, then let a wrapped row inside it change
// height, and the two disagree about where the rows below start. Scintilla has
// used a single table for both since 1998 (scintilla/src/ContractionState.h,
// "Manages visibility of lines for folding and wrapping"), and this is a port
// of that idea -- not of its code.
//
// Portions of the algorithm below (the lazy step in the prefix table) follow
// Scintilla's Partitioning.h, Copyright 1998-2007 Neil Hodgson, used under the
// Scintilla licence: permission to use, copy, modify and distribute with the
// copyright notice retained. See THIRD_PARTY.md.
//
// COST MODEL, because it is the whole point:
//   setHeight / setVisible   O(1) amortised   -- called per painted row, per
//                                                frame, and per folded row
//   displayFromRow           O(1)             -- plus a deferred O(n) fixup
//   rowFromDisplay           O(log n)
//   insertRows / removeRows  O(n) memmove     -- once per Enter/Backspace
//
// The predecessor rebuilt an O(n) prefix array on every single edit and reset
// every height below the edit to a guess. This keeps them.
#pragma once

#include <cstdint>
#include <vector>

namespace aced {

class LineMap {
public:
    explicit LineMap(int rowCount = 0);

    // All heights 1, all rows visible, all folds expanded.
    void reset(int rowCount);

    int rowCount() const { return rowCount_; }
    int displayLineCount() const { return displayFromRow(rowCount_); }

    // True while every row is one display line tall and visible, so the
    // mapping is the identity and no table has been allocated. The common
    // case: wrap off, nothing folded.
    bool isIdentity() const { return !allocated_; }

    // --- structure -------------------------------------------------------
    //
    // New rows arrive one display line tall, visible and expanded. Heights of
    // rows either side are kept: a row's wrapped height depends on its own
    // text and the wrap width, not on the rows above it, so an edit does not
    // invalidate them. (Its *colours* do depend on the rows above -- that is
    // LineCache's problem, not this one's.)
    void insertRows(int row, int count);
    void removeRows(int row, int count);

    // Every height back to 1. For a wrap-width change, where every row really
    // does have to be measured again. NOT for an edit.
    void forgetHeights();

    // --- wrapping --------------------------------------------------------

    int height(int row) const;
    // Returns true if the total number of display lines moved, which is the
    // signal to resize the scrollbar.
    bool setHeight(int row, int h);

    // --- folding ---------------------------------------------------------

    bool visible(int row) const;
    // Inclusive of both ends. Returns true if the display total moved.
    bool setVisible(int rowStart, int rowEnd, bool vis);
    bool anyHidden() const { return hidden_ > 0; }
    void showAll();

    // Whether the fold headed by `row` is open. Deliberately independent of
    // visible(): a fold nested inside a collapsed one keeps its own state, so
    // reopening the outer fold restores what was inside rather than flattening
    // it. Scintilla splits these for the same reason.
    bool expanded(int row) const;
    bool setExpanded(int row, bool e);
    // First row at or after `rowStart` with a contracted fold header. -1 when
    // there is none. Used to find the fold a hidden cursor is trapped in.
    int contractedNext(int rowStart) const;

    // Nearest visible row at or before / at or after `row`. Returns -1 only
    // when the document is empty; a fully hidden document cannot occur, since
    // a fold's header stays visible.
    int prevVisible(int row) const;
    int nextVisible(int row) const;

    // --- the mapping -----------------------------------------------------

    // First display line of `row`. Accepts rowCount() and returns the total,
    // which is what makes it usable as a half-open end.
    int displayFromRow(int row) const;
    // Last display line of `row`. Equals displayFromRow(row) for a hidden row.
    int displayLastFromRow(int row) const;
    // Inverse. Lands on a visible row. `sub` receives the display line's index
    // within that row, 0 for the first.
    int rowFromDisplay(int line, int *sub = nullptr) const;

    // Recompute the prefix table from heights and visibility and compare. For
    // tests: the lazy step is exactly the kind of thing that is subtly wrong
    // for a week.
    bool checkInvariants() const;

private:
    void ensureData();
    // Flush the pending step up to and including `upTo`.
    void applyStep(int upTo);
    void backStep(int downTo);
    // Add `delta` display lines to every row strictly after `afterRow`.
    void addDelta(int afterRow, int delta);
    void flush() { applyStep(rowCount_); }

    int rowCount_ = 0;
    bool allocated_ = false;
    int hidden_ = 0;

    std::vector<int> heights_;
    std::vector<uint8_t> visible_;
    std::vector<uint8_t> expanded_;

    // starts_[i] is the display line row i begins on, before the step is
    // applied. Size rowCount_ + 1; the last entry is the total.
    std::vector<int> starts_;
    int stepRow_ = 0;
    int stepDelta_ = 0;
};

}  // namespace aced
