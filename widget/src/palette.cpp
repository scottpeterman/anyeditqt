// src/palette.cpp
#include "acedqt/palette.h"

namespace acedqt {

// The default IS tomorrow-night, written out here rather than delegating to
// tomorrowNight(). Delegating recursed forever -- tomorrowNight() constructs a
// Palette, whose constructor called tomorrowNight() -- and the only symptom was
// a segfault forty thousand frames deep with no message attached to it.
Palette::Palette() {
    background_  = QColor(0x1d, 0x1f, 0x21);
    foreground_  = QColor(0xc5, 0xc8, 0xc6);
    cursor_      = QColor(0xae, 0xaf, 0xad);
    selection_   = QColor(0x37, 0x3b, 0x41);
    gutterBg_    = QColor(0x1d, 0x1f, 0x21);
    gutterFg_    = QColor(0x4b, 0x50, 0x56);
    currentLine_ = QColor(0x28, 0x2a, 0x2e);
    searchMatch_ = QColor(0x3d, 0x4c, 0x3a);
    searchCurrent_ = QColor(0x6b, 0x6a, 0x2f);

    // Deliberately short. Every entry is a scope PREFIX, so "comment" covers
    // comment.line.double-slash and comment.block alike, across all 198
    // grammars. A theme needs a couple of dozen lines, not one per language.
    setColor("text",                QColor(0xc5, 0xc8, 0xc6));
    setColor("comment",             QColor(0x96, 0x98, 0x96));
    setColor("string",              QColor(0xb5, 0xbd, 0x68));
    setColor("constant",            QColor(0xde, 0x93, 0x5f));
    setColor("constant.language",   QColor(0xb2, 0x94, 0xbb));
    setColor("keyword",             QColor(0xb2, 0x94, 0xbb));
    setColor("keyword.operator",    QColor(0x8a, 0xbe, 0xb7));
    setColor("storage",             QColor(0xb2, 0x94, 0xbb));
    setColor("entity",              QColor(0x81, 0xa2, 0xbe));
    setColor("entity.name.function", QColor(0x81, 0xa2, 0xbe));
    setColor("entity.name.tag",     QColor(0xcc, 0x66, 0x66));
    setColor("variable",            QColor(0xcc, 0x66, 0x66));
    setColor("variable.parameter",  QColor(0xde, 0x93, 0x5f));
    setColor("support",             QColor(0x81, 0xa2, 0xbe));
    setColor("meta.tag",            QColor(0xcc, 0x66, 0x66));
    setColor("invalid",             QColor(0xff, 0x3b, 0x3b));
    setColor("paren",               QColor(0xc5, 0xc8, 0xc6));
    setColor("punctuation",         QColor(0xc5, 0xc8, 0xc6));
}

const QTextCharFormat &Palette::formatFor(const QString &tokenType) const {
    auto hit = cache_.constFind(tokenType);
    if (hit != cache_.constEnd()) return *hit;

    QString scope = tokenType;
    for (;;) {
        auto f = formats_.constFind(scope);
        if (f != formats_.constEnd()) return *cache_.insert(tokenType, *f);
        const int dot = scope.lastIndexOf('.');
        if (dot < 0) break;
        scope.truncate(dot);
    }
    QTextCharFormat fallback;
    fallback.setForeground(foreground_);
    return *cache_.insert(tokenType, fallback);
}

void Palette::setFormat(const QString &scope, const QTextCharFormat &fmt) {
    formats_.insert(scope, fmt);
    invalidate();
}

void Palette::setColor(const QString &scope, const QColor &color) {
    QTextCharFormat f = formats_.value(scope);
    f.setForeground(color);
    setFormat(scope, f);
}

Palette Palette::tomorrowNight() { return Palette(); }

Palette Palette::dayfold() {
    Palette p;
    p.background_  = QColor(0xff, 0xff, 0xff);
    p.foreground_  = QColor(0x30, 0x30, 0x30);
    p.cursor_      = QColor(0x20, 0x20, 0x20);
    p.selection_   = QColor(0xb5, 0xd5, 0xff);
    p.gutterBg_    = QColor(0xf4, 0xf4, 0xf4);
    p.gutterFg_    = QColor(0x9a, 0x9a, 0x9a);
    p.currentLine_ = QColor(0xf2, 0xf6, 0xfc);
    p.searchMatch_ = QColor(0xd9, 0xf2, 0xc9);
    p.searchCurrent_ = QColor(0xff, 0xe4, 0x7a);
    p.setColor("text",               QColor(0x30, 0x30, 0x30));
    p.setColor("comment",            QColor(0x23, 0x6e, 0x25));
    p.setColor("string",             QColor(0x1a, 0x1a, 0xa6));
    p.setColor("constant",           QColor(0xc5, 0x06, 0x0b));
    p.setColor("keyword",            QColor(0x95, 0x00, 0x84));
    p.setColor("keyword.operator",   QColor(0x30, 0x30, 0x30));
    p.setColor("storage",            QColor(0x95, 0x00, 0x84));
    p.setColor("entity",             QColor(0x00, 0x00, 0xa2));
    p.setColor("entity.name.function", QColor(0x00, 0x00, 0xa2));
    p.setColor("variable",           QColor(0xc5, 0x06, 0x0b));
    p.setColor("variable.parameter", QColor(0x30, 0x30, 0x30));
    p.setColor("support",            QColor(0x00, 0x00, 0xa2));
    p.setColor("paren",              QColor(0x30, 0x30, 0x30));
    p.setColor("punctuation",        QColor(0x30, 0x30, 0x30));
    return p;
}

}  // namespace acedqt
