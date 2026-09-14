// include/aced/undo.h
#pragma once

#include <vector>

#include "aced/delta.h"

namespace aced {

class Document;

// Undo as a stack of Deltas rather than a stack of snapshots.
//
// A Delta already knows how to invert itself, so undoing is applying the
// inverse in reverse order and redoing is applying the originals forward. The
// memory cost is proportional to what changed, not to the size of the file,
// which is what makes undo over a large buffer cheap.
//
// Edits are grouped. A group is what one Ctrl+Z takes back, and the grouping
// policy is the caller's: mark() closes the current group. A widget typically
// calls mark() on a pause in typing, on a cursor jump, and before any command
// that is conceptually one operation however many deltas it emits. Without a
// mark, consecutive deltas coalesce into one group, so a paste that arrives as
// several deltas undoes as a unit by default.
//
// The manager attaches to a Document and records automatically. While it is
// applying its own deltas it stops recording, so undo does not push its own
// inverse back onto the stack.
class UndoManager {
public:
    explicit UndoManager(Document *doc);
    ~UndoManager();

    UndoManager(const UndoManager &) = delete;
    UndoManager &operator=(const UndoManager &) = delete;

    // Close the current group. The next delta starts a new one. Calling this
    // twice in a row, or on an empty group, does nothing.
    void mark();

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    // Apply the inverse of the newest group. Returns false if there was
    // nothing to undo. The current group is closed first, so an undo issued
    // mid-typing takes back the run of characters just typed.
    bool undo();

    // Re-apply the group most recently undone. Returns false if the redo stack
    // is empty. Any new edit clears the redo stack.
    bool redo();

    void clear();

    int undoDepth() const { return static_cast<int>(undo_.size()); }
    int redoDepth() const { return static_cast<int>(redo_.size()); }

    // Position the caller should put the cursor at after the last undo() or
    // redo(). This is the end of the affected span, which is where a human
    // expects to be looking. Undefined before the first successful call.
    Position lastTouched() const { return lastTouched_; }

private:
    using Group = std::vector<Delta>;

    void record(const Delta &d);

    Document *doc_;
    int listener_ = 0;
    bool recording_ = true;
    bool openGroup_ = false;
    std::vector<Group> undo_;
    std::vector<Group> redo_;
    Position lastTouched_;
};

}  // namespace aced
