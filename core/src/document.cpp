// src/document.cpp
#include "aced/document.h"

#include <algorithm>

#include "aced/anchor.h"

namespace aced {

std::vector<std::string> splitLines(const std::string &text) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            out.push_back(cur);
            cur.clear();
        } else if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::string joinLines(const std::vector<std::string> &lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += '\n';
        out += lines[i];
    }
    return out;
}

Document::Document() : lines_{std::string()} {}

Document::Document(const std::string &text) : lines_(splitLines(text)) {}

Document::~Document() {
    // Anchors hold a back-pointer; null it so a stray one cannot call into a
    // dead document. It is still a bug for an anchor to outlive its document,
    // but this turns it into a no-op instead of a crash.
    for (Anchor *a : anchors_) a->doc_ = nullptr;
}

const std::string &Document::line(int row) const {
    static const std::string kEmpty;
    if (row < 0 || row >= lineCount()) return kEmpty;
    return lines_[static_cast<size_t>(row)];
}

int Document::lineLength(int row) const {
    if (row < 0 || row >= lineCount()) return 0;
    return static_cast<int>(lines_[static_cast<size_t>(row)].size());
}

Position Document::end() const {
    int r = lineCount() - 1;
    return {r, lineLength(r)};
}

Position Document::clamp(Position p) const {
    if (p.row < 0) return {0, 0};
    if (p.row >= lineCount()) return end();
    int len = lineLength(p.row);
    if (p.column < 0) p.column = 0;
    if (p.column > len) p.column = len;
    return p;
}

std::string Document::text() const { return joinLines(lines_); }

std::string Document::textInRange(const Range &r) const {
    Position s = clamp(r.start), e = clamp(r.end);
    if (e < s) std::swap(s, e);
    if (s.row == e.row)
        return line(s.row).substr(static_cast<size_t>(s.column),
                                  static_cast<size_t>(e.column - s.column));

    std::string out = line(s.row).substr(static_cast<size_t>(s.column));
    for (int r2 = s.row + 1; r2 < e.row; ++r2) {
        out += '\n';
        out += line(r2);
    }
    out += '\n';
    out += line(e.row).substr(0, static_cast<size_t>(e.column));
    return out;
}

int Document::positionToOffset(Position p) const {
    p = clamp(p);
    int off = 0;
    for (int r = 0; r < p.row; ++r) off += lineLength(r) + 1;
    return off + p.column;
}

Position Document::offsetToPosition(int offset) const {
    if (offset <= 0) return {0, 0};
    int remaining = offset;
    for (int r = 0; r < lineCount(); ++r) {
        int len = lineLength(r);
        if (remaining <= len) return {r, remaining};
        remaining -= len + 1;
    }
    return end();
}

int Document::utf8Floor(const std::string &line, int column) {
    if (column <= 0 || column >= static_cast<int>(line.size())) return column;
    // Continuation bytes are 10xxxxxx; walk back off them to the lead byte.
    while (column > 0 && (static_cast<unsigned char>(line[column]) & 0xC0) == 0x80) --column;
    return column;
}

// --- writing ---------------------------------------------------------------

Delta Document::insert(Position at, const std::string &text) {
    Delta d;
    d.action = Delta::Action::Insert;
    d.start = clamp(at);
    d.lines = splitLines(text);
    // end follows from start + lines: last line of a multi-line insert starts
    // at column 0, so the tail length is the new column.
    if (d.lines.size() == 1)
        d.end = {d.start.row, d.start.column + static_cast<int>(d.lines[0].size())};
    else
        d.end = {d.start.row + static_cast<int>(d.lines.size()) - 1,
                 static_cast<int>(d.lines.back().size())};
    applyDelta(d);
    return d;
}

Delta Document::remove(const Range &range) {
    Position s = clamp(range.start), e = clamp(range.end);
    if (e < s) std::swap(s, e);
    Delta d;
    d.action = Delta::Action::Remove;
    d.start = s;
    d.end = e;
    d.lines = splitLines(textInRange({s, e}));
    applyDelta(d);
    return d;
}

Delta Document::replace(const Range &range, const std::string &text) {
    Position s = clamp(range.start), e = clamp(range.end);
    if (e < s) std::swap(s, e);
    if (s != e) remove({s, e});
    return insert(s, text);
}

void Document::setText(const std::string &text) {
    if (lineCount() > 1 || !lines_[0].empty()) remove({{0, 0}, end()});
    if (!text.empty()) insert({0, 0}, text);
}

void Document::applyDelta(const Delta &delta) {
    if (delta.lines.empty()) return;

    if (delta.action == Delta::Action::Insert)
        applyInsert(delta);
    else
        applyRemove(delta);

    // Order matters: anchors first, so a listener that reads an anchor during
    // the callback sees the post-edit position rather than a stale one.
    for (Anchor *a : anchors_) a->onDelta(delta);

    // Copy: a listener is allowed to add or drop listeners, which would
    // invalidate an iterator over the live vector.
    auto snapshot = listeners_;
    for (auto &[id, fn] : snapshot)
        if (fn) fn(delta);
}

void Document::applyInsert(const Delta &d) {
    Position at = clamp(d.start);
    const std::vector<std::string> &ins = d.lines;
    std::string &row = lines_[static_cast<size_t>(at.row)];
    size_t col = static_cast<size_t>(std::min<int>(at.column, static_cast<int>(row.size())));

    if (ins.size() == 1) {
        row.insert(col, ins[0]);
        return;
    }

    std::string tail = row.substr(col);
    row.erase(col);
    row += ins.front();

    // Splice the interior lines in, then re-attach the tail to the last one.
    lines_.insert(lines_.begin() + at.row + 1, ins.begin() + 1, ins.end());
    lines_[static_cast<size_t>(at.row) + ins.size() - 1] += tail;
}

void Document::applyRemove(const Delta &d) {
    Position s = clamp(d.start), e = clamp(d.end);
    if (e < s) std::swap(s, e);

    if (s.row == e.row) {
        std::string &row = lines_[static_cast<size_t>(s.row)];
        size_t a = static_cast<size_t>(std::min<int>(s.column, static_cast<int>(row.size())));
        size_t b = static_cast<size_t>(std::min<int>(e.column, static_cast<int>(row.size())));
        if (b > a) row.erase(a, b - a);
        return;
    }

    std::string head = line(s.row).substr(0, static_cast<size_t>(s.column));
    std::string tail = line(e.row).substr(
        static_cast<size_t>(std::min<int>(e.column, lineLength(e.row))));
    lines_.erase(lines_.begin() + s.row, lines_.begin() + e.row + 1);
    lines_.insert(lines_.begin() + s.row, head + tail);
}

// --- observers -------------------------------------------------------------

Document::ListenerId Document::addListener(Listener fn) {
    ListenerId id = nextListenerId_++;
    listeners_.emplace_back(id, std::move(fn));
    return id;
}

void Document::removeListener(ListenerId id) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [id](const auto &p) { return p.first == id; }),
                     listeners_.end());
}

void Document::attach(Anchor *a) { anchors_.push_back(a); }

void Document::detach(Anchor *a) {
    anchors_.erase(std::remove(anchors_.begin(), anchors_.end(), a), anchors_.end());
}

}  // namespace aced
