// app/theme.cpp
#include "app/theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace anyedit {
namespace {

// Captured before anything is overridden, so switching back to "system" during
// a session restores what the desktop actually gave us rather than a guess at
// it. Function-local so it initialises on first use, which is after
// QApplication exists and therefore after the platform palette is real.
struct Original {
    QPalette palette;
    QString styleName;
    QFont font;
    bool captured = false;
};

Original &original() {
    static Original o;
    return o;
}

void captureOriginal(QApplication *app) {
    Original &o = original();
    if (o.captured) return;
    o.palette = app->palette();
    o.styleName = app->style() ? app->style()->objectName() : QString();
    o.font = app->font();
    o.captured = true;
}

}  // namespace

QPalette darkChromePalette() {
    // Tuned against acedqt::Palette::tomorrowNight() so the chrome and the text
    // area read as one application: the window colour is a shade above the
    // editor background rather than the same, which is what makes the tab strip
    // separate from the document without a border.
    const QColor window(0x2b, 0x2d, 0x30);
    const QColor base(0x1d, 0x1f, 0x21);
    const QColor text(0xc5, 0xc8, 0xc6);
    const QColor disabled(0x70, 0x73, 0x75);
    const QColor highlight(0x37, 0x4b, 0x5e);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, window);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xff, 0x55, 0x55));
    p.setColor(QPalette::Link, QColor(0x81, 0xa2, 0xbe));
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::PlaceholderText, disabled);
    // Set explicitly rather than left to the style's own dimming, which derives
    // disabled colours from a light palette's assumptions and comes out
    // brighter than the enabled text on a dark one.
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x3a, 0x3d, 0x41));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    return p;
}

QPalette lightChromePalette() {
    const QColor window(0xef, 0xef, 0xef);
    const QColor base(0xff, 0xff, 0xff);
    const QColor text(0x20, 0x20, 0x20);
    const QColor disabled(0x9a, 0x9a, 0x9a);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xc5, 0x06, 0x0b));
    p.setColor(QPalette::Link, QColor(0x00, 0x00, 0xa2));
    p.setColor(QPalette::Highlight, QColor(0x30, 0x8c, 0xc6));
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    return p;
}

Scheme applyTheme(QApplication *app, const AppSettings &s) {
    captureOriginal(app);

    if (s.theme == ThemeMode::System) {
        if (!original().styleName.isEmpty())
            app->setStyle(QStyleFactory::create(original().styleName));
        app->setPalette(original().palette);
        // Detected AFTER the platform palette is back, because on Qt 6.4 the
        // detection reads that palette. Doing it first would test whatever
        // override was in force a moment ago.
        return detectSystemScheme();
    }

    if (QStyle *fusion = QStyleFactory::create("Fusion")) app->setStyle(fusion);
    const bool dark = s.theme == ThemeMode::Dark;
    app->setPalette(dark ? darkChromePalette() : lightChromePalette());
    return dark ? Scheme::Dark : Scheme::Light;
}

void applyUiFont(QApplication *app, const AppSettings &s) {
    captureOriginal(app);
    QFont f = original().font;
    if (!s.uiFontFamily.isEmpty()) f.setFamily(s.uiFontFamily);
    if (s.uiFontSize > 0) f.setPointSize(s.uiFontSize);
    app->setFont(f);
}

}  // namespace anyedit
