// include/aced/anchor.h
#pragma once

#include "aced/delta.h"

namespace aced {

class Document;

// A Position that the Document keeps correct across edits.
//
// This is the piece with no counterpart in a terminal emulator, where rows are
// append-only and an absolute index is stable forever. Here an insertion three
// lines up renumbers everything below it, so anything that remembers a place --
// a cursor, a selection end, a fold boundary, a breakpoint marker, a diagnostic
// from a language server -- has to be told. Registering an Anchor is how it
// gets told.
//
// The rule for a position sitting exactly at the edit point is genuinely
// ambiguous, so it is a per-anchor choice:
//
//   insertRight = true   text inserted at the anchor pushes the anchor along.
//                        This is what you want for a cursor: type at the caret
//                        and the caret stays after what you typed.
//   insertRight = false  text inserted at the anchor appears after it and the
//                        anchor stays put. This is what you want for the start
//                        of a marked region you do not want to grow.
//
// A removal that spans the anchor collapses it to the start of the removed
// range; there is nowhere else honest to put it.
//
// An Anchor registers with its Document on construction and unregisters on
// destruction, so it must not outlive the Document. Anchors are not copyable
// for that reason -- a copy would be a second registration the original does
// not know about.
class Anchor {
public:
    Anchor(Document *doc, Position p, bool insertRight = true);
    Anchor(Document *doc, int row, int column, bool insertRight = true);
    ~Anchor();

    Anchor(const Anchor &) = delete;
    Anchor &operator=(const Anchor &) = delete;

    Position position() const { return pos_; }
    int row() const { return pos_.row; }
    int column() const { return pos_.column; }

    bool insertRight() const { return insertRight_; }
    void setInsertRight(bool v) { insertRight_ = v; }

    // Move the anchor explicitly. The position is clamped into the document.
    void setPosition(Position p);

    Document *document() const { return doc_; }

    // The transform itself, exposed because callers frequently need to move a
    // bare Position they did not bother to register -- a search hit being
    // reported back after the buffer moved under it, for instance.
    static Position transform(const Delta &delta, Position p, bool insertRight);

private:
    friend class Document;
    void onDelta(const Delta &d) { pos_ = transform(d, pos_, insertRight_); }

    Document *doc_;
    Position pos_;
    bool insertRight_;
};

}  // namespace aced
