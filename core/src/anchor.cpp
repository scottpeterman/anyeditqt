// src/anchor.cpp
#include "aced/anchor.h"

#include "aced/document.h"

namespace aced {

Anchor::Anchor(Document *doc, Position p, bool insertRight)
    : doc_(doc), pos_(doc ? doc->clamp(p) : p), insertRight_(insertRight) {
    if (doc_) doc_->attach(this);
}

Anchor::Anchor(Document *doc, int row, int column, bool insertRight)
    : Anchor(doc, Position{row, column}, insertRight) {}

Anchor::~Anchor() {
    if (doc_) doc_->detach(this);
}

void Anchor::setPosition(Position p) { pos_ = doc_ ? doc_->clamp(p) : p; }

Position Anchor::transform(const Delta &delta, Position p, bool insertRight) {
    const Position s = delta.start;
    const Position e = delta.end;

    if (delta.action == Delta::Action::Insert) {
        // Strictly before the insertion point: nothing moves.
        if (p.row < s.row) return p;
        if (p.row == s.row && p.column < s.column) return p;
        // Exactly at the insertion point: the tie-break.
        if (p == s && !insertRight) return p;

        const int addedRows = static_cast<int>(delta.lines.size()) - 1;
        if (p.row == s.row) {
            // Same row as the insert. The column moves by the width of the
            // inserted text on that row; on a multi-line insert the position
            // lands on the final inserted line, so its column is rebased.
            if (addedRows == 0) {
                p.column += static_cast<int>(delta.lines[0].size());
            } else {
                p.column = e.column + (p.column - s.column);
                p.row += addedRows;
            }
        } else {
            p.row += addedRows;
        }
        return p;
    }

    // Remove.
    // Entirely before the removed span: nothing moves.
    if (p.row < s.row) return p;
    if (p.row == s.row && p.column <= s.column) return p;

    // Inside the removed span: collapse to its start.
    if (p < e) return s;

    // After the removed span: pull back by what was taken out.
    const int removedRows = e.row - s.row;
    if (p.row == e.row) {
        p.column = s.column + (p.column - e.column);
        p.row = s.row;
    } else {
        p.row -= removedRows;
    }
    return p;
}

}  // namespace aced
