// include/acedqt/palette.h
//
// Maps the token type strings aced::Tokenizer emits onto QTextCharFormat.
//
// Types are TextMate-style dotted scopes -- "storage.type", "string.quoted.
// double", "entity.name.function". Lookup falls back along the dots: an exact
// miss on "string.quoted.double" tries "string.quoted", then "string". That is
// what lets a theme define two dozen scopes and still colour all 198 grammars
// sensibly instead of needing an entry per grammar per token.
#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <QTextCharFormat>

namespace acedqt {

class Palette {
public:
    Palette();

    // Longest-prefix lookup over the dotted scope. Results are memoised: the
    // same handful of scopes recur on every repaint of every line, and the
    // fallback walk is pure string work.
    const QTextCharFormat &formatFor(const QString &tokenType) const;

    void setFormat(const QString &scope, const QTextCharFormat &fmt);
    void setColor(const QString &scope, const QColor &color);

    QColor background() const { return background_; }
    QColor foreground() const { return foreground_; }
    QColor cursorColor() const { return cursor_; }
    QColor selectionColor() const { return selection_; }
    QColor gutterBackground() const { return gutterBg_; }
    QColor gutterForeground() const { return gutterFg_; }
    QColor currentLineColor() const { return currentLine_; }
    // Every match of the current search, and the one the cursor is on. Two
    // colours because a Find Next that repaints identically tells you nothing
    // about which match you just moved to.
    QColor searchMatchColor() const { return searchMatch_; }
    QColor searchCurrentColor() const { return searchCurrent_; }

    void setBackground(const QColor &c) { background_ = c; }
    void setForeground(const QColor &c) { foreground_ = c; invalidate(); }
    void setCursorColor(const QColor &c) { cursor_ = c; }
    void setSelectionColor(const QColor &c) { selection_ = c; }
    void setGutterBackground(const QColor &c) { gutterBg_ = c; }
    void setGutterForeground(const QColor &c) { gutterFg_ = c; }
    void setCurrentLineColor(const QColor &c) { currentLine_ = c; }
    void setSearchMatchColor(const QColor &c) { searchMatch_ = c; }
    void setSearchCurrentColor(const QColor &c) { searchCurrent_ = c; }

    static Palette tomorrowNight();
    static Palette dayfold();

private:
    void invalidate() { cache_.clear(); }

    QHash<QString, QTextCharFormat> formats_;
    mutable QHash<QString, QTextCharFormat> cache_;
    QColor background_, foreground_, cursor_, selection_;
    QColor gutterBg_, gutterFg_, currentLine_;
    QColor searchMatch_, searchCurrent_;
};

}  // namespace acedqt
