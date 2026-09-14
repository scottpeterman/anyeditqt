// app/theme.h
//
// Applies the app group's theme to QApplication -- the menu bar, the tab bar,
// the status bar, the dialogs. The editor's own colours are separate and come
// from acedqt::Palette; this is only the chrome around it.
//
// WHY A HAND-BUILT QPALETTE AND NOT QStyleHints::setColorScheme(). That call
// arrived in Qt 6.8. The sandbox builds against 6.4, the development machine
// runs 6.10, and a code path that only compiles on one of them is a code path
// that only gets tested on one of them. A QPalette is the same on every
// version back to 6.2.
//
// WHY FUSION FOR AN EXPLICIT THEME. The Windows and macOS native styles paint
// significant parts of their chrome from the system theme and ignore the
// application palette, so "dark" on a light desktop comes out half dark and
// unreadable. Fusion honours the palette everywhere. ThemeMode::System changes
// neither the style nor the palette, so a user who wants the native look
// leaves the theme on system and gets it.
#pragma once

#include "app/settings.h"

class QApplication;

namespace anyedit {

// Returns the scheme that was applied, which is what the editor palette should
// then be chosen against.
Scheme applyTheme(QApplication *app, const AppSettings &s);

// The chrome font. An empty family or a zero size leaves that half alone, so
// setting only ui_font_size enlarges the menus without changing the family.
void applyUiFont(QApplication *app, const AppSettings &s);

QPalette darkChromePalette();
QPalette lightChromePalette();

}  // namespace anyedit
