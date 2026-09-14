// src/undo.cpp
#include "aced/undo.h"

#include "aced/document.h"

namespace aced {

UndoManager::UndoManager(Document *doc) : doc_(doc) {
    listener_ = doc_->addListener([this](const Delta &d) { record(d); });
}

UndoManager::~UndoManager() {
    if (doc_) doc_->removeListener(listener_);
}

void UndoManager::record(const Delta &d) {
    if (!recording_) return;
    redo_.clear();
    if (!openGroup_) {
        undo_.emplace_back();
        openGroup_ = true;
    }
    undo_.back().push_back(d);
}

void UndoManager::mark() {
    // An open-but-empty group can only happen if a caller marks twice; drop it
    // so undoDepth() never counts a group that would undo nothing.
    if (openGroup_ && !undo_.empty() && undo_.back().empty()) undo_.pop_back();
    openGroup_ = false;
}

bool UndoManager::undo() {
    mark();
    if (undo_.empty()) return false;

    Group g = std::move(undo_.back());
    undo_.pop_back();

    recording_ = false;
    // Newest first: a group's deltas were applied in order, so their inverses
    // have to come off in reverse or the positions in the earlier ones will
    // refer to text that is no longer where they think it is.
    for (auto it = g.rbegin(); it != g.rend(); ++it) doc_->revertDelta(*it);
    recording_ = true;

    lastTouched_ = g.front().start;
    redo_.push_back(std::move(g));
    return true;
}

bool UndoManager::redo() {
    if (redo_.empty()) return false;

    Group g = std::move(redo_.back());
    redo_.pop_back();

    recording_ = false;
    for (const Delta &d : g) doc_->applyDelta(d);
    recording_ = true;

    lastTouched_ = g.back().end;
    undo_.push_back(std::move(g));
    openGroup_ = false;
    return true;
}

void UndoManager::clear() {
    undo_.clear();
    redo_.clear();
    openGroup_ = false;
}

}  // namespace aced
