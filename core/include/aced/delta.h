// include/aced/delta.h
#pragma once

#include <string>
#include <vector>

namespace aced {

struct Position {
    int row = 0;
    int column = 0;

    friend bool operator==(const Position &a, const Position &b) {
        return a.row == b.row && a.column == b.column;
    }
    friend bool operator!=(const Position &a, const Position &b) { return !(a == b); }
    friend bool operator<(const Position &a, const Position &b) {
        return a.row != b.row ? a.row < b.row : a.column < b.column;
    }
    friend bool operator<=(const Position &a, const Position &b) { return !(b < a); }
    friend bool operator>(const Position &a, const Position &b) { return b < a; }
    friend bool operator>=(const Position &a, const Position &b) { return !(a < b); }
};

struct Range {
    Position start;
    Position end;

    bool empty() const { return start == end; }
    bool contains(const Position &p) const { return start <= p && p < end; }
};

// The one mutation primitive. Every change to a Document is expressed as a
// Delta, and nothing mutates the line store except applyDelta().
//
// `end` is derivable from `start` and `lines`, and for Insert it is exactly
// that. It is stored anyway because every consumer -- anchors, the undo stack,
// the tokenizer's dirty-row tracking -- wants the affected span without having
// to walk the strings to find it.
//
// For Remove, `lines` holds the text that was there. That is what makes a Delta
// invertible in place: flip the action and it undoes itself. The undo stack is
// therefore a stack of Deltas, not a stack of document snapshots.
struct Delta {
    enum class Action { Insert, Remove };

    Action action = Action::Insert;
    Position start;
    Position end;
    std::vector<std::string> lines;  // always at least one element

    Range range() const { return {start, end}; }

    Delta inverted() const {
        Delta d = *this;
        d.action = (action == Action::Insert) ? Action::Remove : Action::Insert;
        return d;
    }
};

// Split text on \n, \r\n or \r into the line vector a Delta carries.
// "" -> {""}; "a\nb" -> {"a", "b"}; "a\n" -> {"a", ""}.
std::vector<std::string> splitLines(const std::string &text);

// Join with '\n'. Inverse of splitLines for \n input.
std::string joinLines(const std::vector<std::string> &lines);

}  // namespace aced
