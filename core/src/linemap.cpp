// src/linemap.cpp
#include "aced/linemap.h"

#include <algorithm>

namespace aced {

LineMap::LineMap(int rowCount) { reset(rowCount); }

void LineMap::reset(int rowCount) {
    rowCount_ = std::max(0, rowCount);
    allocated_ = false;
    hidden_ = 0;
    heights_.clear();
    visible_.clear();
    expanded_.clear();
    starts_.clear();
    stepRow_ = 0;
    stepDelta_ = 0;
}

void LineMap::ensureData() {
    if (allocated_) return;
    heights_.assign(static_cast<size_t>(rowCount_), 1);
    visible_.assign(static_cast<size_t>(rowCount_), 1);
    expanded_.assign(static_cast<size_t>(rowCount_), 1);
    starts_.resize(static_cast<size_t>(rowCount_) + 1);
    for (int i = 0; i <= rowCount_; ++i) starts_[static_cast<size_t>(i)] = i;
    stepRow_ = rowCount_;
    stepDelta_ = 0;
    allocated_ = true;
}

// --- the lazy step ------------------------------------------------------
//
// starts_ is correct for rows <= stepRow_ and short by stepDelta_ for rows
// after it. An edit that changes one row's contribution therefore costs a
// single subtraction rather than a walk to the end of the document. The cost is
// paid, once, the next time a lookup crosses the step -- and the paint loop
// reads a screenful of consecutive rows, so it usually doesn't.

void LineMap::applyStep(int upTo) {
    if (!allocated_) return;
    upTo = std::clamp(upTo, 0, rowCount_);
    if (stepDelta_ != 0 && upTo > stepRow_) {
        for (int i = stepRow_ + 1; i <= upTo; ++i)
            starts_[static_cast<size_t>(i)] += stepDelta_;
    }
    if (upTo > stepRow_) stepRow_ = upTo;
    if (stepRow_ >= rowCount_) {
        stepRow_ = rowCount_;
        stepDelta_ = 0;
    }
}

void LineMap::backStep(int downTo) {
    if (!allocated_) return;
    downTo = std::clamp(downTo, 0, rowCount_);
    if (stepDelta_ != 0 && downTo < stepRow_) {
        for (int i = downTo + 1; i <= stepRow_; ++i)
            starts_[static_cast<size_t>(i)] -= stepDelta_;
    }
    stepRow_ = downTo;
}

void LineMap::addDelta(int afterRow, int delta) {
    if (delta == 0 || !allocated_) return;
    if (stepDelta_ != 0) {
        if (afterRow >= stepRow_) {
            applyStep(afterRow);
            stepDelta_ += delta;
            if (stepRow_ >= rowCount_) {
                // applyStep parked the step at the end, where a delta would
                // have nothing to apply to. Nothing after the last row.
                stepDelta_ = 0;
            }
        } else if (afterRow >= stepRow_ - rowCount_ / 10) {
            // Close behind the step: walking it back is cheaper than flushing
            // the whole tail. The 10% bound is Scintilla's.
            backStep(afterRow);
            stepDelta_ += delta;
        } else {
            flush();
            stepRow_ = afterRow;
            stepDelta_ = delta;
        }
    } else {
        stepRow_ = afterRow;
        stepDelta_ = delta;
    }
}

// --- structure ----------------------------------------------------------

void LineMap::insertRows(int row, int count) {
    if (count <= 0) return;
    row = std::clamp(row, 0, rowCount_);
    if (!allocated_) {
        rowCount_ += count;
        return;
    }
    flush();
    const int base = starts_[static_cast<size_t>(row)];
    heights_.insert(heights_.begin() + row, static_cast<size_t>(count), 1);
    visible_.insert(visible_.begin() + row, static_cast<size_t>(count), 1);
    expanded_.insert(expanded_.begin() + row, static_cast<size_t>(count), 1);
    starts_.insert(starts_.begin() + row, static_cast<size_t>(count), 0);
    for (int i = 0; i < count; ++i)
        starts_[static_cast<size_t>(row + i)] = base + i;
    rowCount_ += count;
    for (int i = row + count; i <= rowCount_; ++i)
        starts_[static_cast<size_t>(i)] += count;
    stepRow_ = rowCount_;
    stepDelta_ = 0;
}

void LineMap::removeRows(int row, int count) {
    if (count <= 0) return;
    row = std::clamp(row, 0, rowCount_);
    count = std::min(count, rowCount_ - row);
    if (count <= 0) return;
    if (!allocated_) {
        rowCount_ -= count;
        return;
    }
    flush();
    for (int i = row; i < row + count; ++i)
        if (!visible_[static_cast<size_t>(i)]) --hidden_;
    const int removed = starts_[static_cast<size_t>(row + count)] -
                        starts_[static_cast<size_t>(row)];
    heights_.erase(heights_.begin() + row, heights_.begin() + row + count);
    visible_.erase(visible_.begin() + row, visible_.begin() + row + count);
    expanded_.erase(expanded_.begin() + row, expanded_.begin() + row + count);
    starts_.erase(starts_.begin() + row, starts_.begin() + row + count);
    rowCount_ -= count;
    for (int i = row; i <= rowCount_; ++i)
        starts_[static_cast<size_t>(i)] -= removed;
    stepRow_ = rowCount_;
    stepDelta_ = 0;
}

void LineMap::forgetHeights() {
    if (!allocated_) return;
    if (hidden_ == 0 && contractedNext(0) < 0) {
        // Nothing folded and no contracted header to remember, so dropping the
        // heights drops the only reason the table exists. Back to the identity
        // and free the memory. The contracted check is not redundant: a fold
        // whose body is empty is contracted and hides nothing.
        const int n = rowCount_;
        reset(n);
        return;
    }
    flush();
    int acc = 0;
    for (int i = 0; i < rowCount_; ++i) {
        heights_[static_cast<size_t>(i)] = 1;
        starts_[static_cast<size_t>(i)] = acc;
        if (visible_[static_cast<size_t>(i)]) ++acc;
    }
    starts_[static_cast<size_t>(rowCount_)] = acc;
    stepRow_ = rowCount_;
    stepDelta_ = 0;
}

// --- wrapping -----------------------------------------------------------

int LineMap::height(int row) const {
    if (!allocated_) return 1;
    if (row < 0 || row >= rowCount_) return 1;
    return heights_[static_cast<size_t>(row)];
}

bool LineMap::setHeight(int row, int h) {
    if (row < 0 || row >= rowCount_ || h < 1) return false;
    if (!allocated_) {
        if (h == 1) return false;
        ensureData();
    }
    const int old = heights_[static_cast<size_t>(row)];
    if (old == h) return false;
    const bool vis = visible_[static_cast<size_t>(row)] != 0;
    heights_[static_cast<size_t>(row)] = h;
    if (!vis) return false;
    addDelta(row, h - old);
    return true;
}

// --- folding ------------------------------------------------------------

bool LineMap::visible(int row) const {
    if (!allocated_) return row >= 0 && row < rowCount_;
    if (row < 0 || row >= rowCount_) return false;
    return visible_[static_cast<size_t>(row)] != 0;
}

bool LineMap::setVisible(int rowStart, int rowEnd, bool vis) {
    if (!allocated_) {
        if (vis) return false;
        ensureData();
    }
    rowStart = std::max(0, rowStart);
    rowEnd = std::min(rowEnd, rowCount_ - 1);
    bool moved = false;
    for (int row = rowStart; row <= rowEnd; ++row) {
        const bool was = visible_[static_cast<size_t>(row)] != 0;
        if (was == vis) continue;
        visible_[static_cast<size_t>(row)] = vis ? 1 : 0;
        hidden_ += vis ? -1 : 1;
        const int h = heights_[static_cast<size_t>(row)];
        if (h != 0) {
            addDelta(row, vis ? h : -h);
            moved = true;
        }
    }
    return moved;
}

void LineMap::showAll() {
    if (!allocated_) return;
    setVisible(0, rowCount_ - 1, true);
    std::fill(expanded_.begin(), expanded_.end(), 1);
}

bool LineMap::expanded(int row) const {
    if (!allocated_) return true;
    if (row < 0 || row >= rowCount_) return true;
    return expanded_[static_cast<size_t>(row)] != 0;
}

bool LineMap::setExpanded(int row, bool e) {
    if (row < 0 || row >= rowCount_) return false;
    if (!allocated_) {
        if (e) return false;
        ensureData();
    }
    if ((expanded_[static_cast<size_t>(row)] != 0) == e) return false;
    expanded_[static_cast<size_t>(row)] = e ? 1 : 0;
    return true;
}

int LineMap::contractedNext(int rowStart) const {
    if (!allocated_) return -1;
    for (int row = std::max(0, rowStart); row < rowCount_; ++row)
        if (!expanded_[static_cast<size_t>(row)]) return row;
    return -1;
}

int LineMap::prevVisible(int row) const {
    if (rowCount_ == 0) return -1;
    row = std::min(row, rowCount_ - 1);
    if (!allocated_) return std::max(0, row);
    for (int r = row; r >= 0; --r)
        if (visible_[static_cast<size_t>(r)]) return r;
    return nextVisible(0);
}

int LineMap::nextVisible(int row) const {
    if (rowCount_ == 0) return -1;
    row = std::max(0, row);
    if (!allocated_) return std::min(row, rowCount_ - 1);
    for (int r = row; r < rowCount_; ++r)
        if (visible_[static_cast<size_t>(r)]) return r;
    for (int r = std::min(row, rowCount_ - 1); r >= 0; --r)
        if (visible_[static_cast<size_t>(r)]) return r;
    return -1;
}

// --- the mapping --------------------------------------------------------

int LineMap::displayFromRow(int row) const {
    if (rowCount_ == 0) return 0;
    row = std::clamp(row, 0, rowCount_);
    if (!allocated_) return row;
    int pos = starts_[static_cast<size_t>(row)];
    if (row > stepRow_) pos += stepDelta_;
    return pos;
}

int LineMap::displayLastFromRow(int row) const {
    if (!visible(row)) return displayFromRow(row);
    return displayFromRow(row) + height(row) - 1;
}

int LineMap::rowFromDisplay(int line, int *sub) const {
    if (sub) *sub = 0;
    if (rowCount_ == 0) return 0;
    if (!allocated_) return std::clamp(line, 0, rowCount_ - 1);

    const int total = displayFromRow(rowCount_);
    if (line <= 0) {
        const int r = nextVisible(0);
        return r < 0 ? 0 : r;
    }
    if (line >= total) {
        const int r = prevVisible(rowCount_ - 1);
        const int row = r < 0 ? rowCount_ - 1 : r;
        if (sub) *sub = std::max(0, height(row) - 1);
        return row;
    }

    // Largest row whose start is <= line. Hidden rows share a start with the
    // row after them, and the search rounds high, so it lands past a run of
    // them onto the next visible row -- which is the one that owns the display
    // line being asked about.
    int lower = 0, upper = rowCount_;
    while (lower < upper) {
        const int middle = (upper + lower + 1) / 2;
        int posMiddle = starts_[static_cast<size_t>(middle)];
        if (middle > stepRow_) posMiddle += stepDelta_;
        if (line < posMiddle)
            upper = middle - 1;
        else
            lower = middle;
    }
    int row = std::clamp(lower, 0, rowCount_ - 1);
    if (!visible_[static_cast<size_t>(row)]) {
        const int r = nextVisible(row);
        if (r >= 0) row = r;
    }
    if (sub) *sub = std::max(0, line - displayFromRow(row));
    return row;
}

bool LineMap::checkInvariants() const {
    if (!allocated_) return heights_.empty() && starts_.empty();
    if (static_cast<int>(heights_.size()) != rowCount_) return false;
    if (static_cast<int>(visible_.size()) != rowCount_) return false;
    if (static_cast<int>(expanded_.size()) != rowCount_) return false;
    if (static_cast<int>(starts_.size()) != rowCount_ + 1) return false;
    if (stepRow_ < 0 || stepRow_ > rowCount_) return false;
    int acc = 0, hid = 0;
    for (int i = 0; i < rowCount_; ++i) {
        if (displayFromRow(i) != acc) return false;
        if (heights_[static_cast<size_t>(i)] < 1) return false;
        if (visible_[static_cast<size_t>(i)])
            acc += heights_[static_cast<size_t>(i)];
        else
            ++hid;
    }
    if (displayFromRow(rowCount_) != acc) return false;
    return hid == hidden_;
}

}  // namespace aced
