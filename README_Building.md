# Building anyeditqt

A Qt-free C++ core, a Qt widget over it, and an editor application. Three
platforms, one CMake tree, two vendored dependencies.

If you are Claude working in a sandbox, read `README_Claude_Qt_Sandbox.md`
instead — it covers the same build plus the environment-specific traps.

---

## Prerequisites

| | |
|---|---|
| CMake | 3.21 or newer |
| Compiler | C++17: GCC 9+, Clang 10+, MSVC 2019+ |
| Qt | 6.2 or newer, Widgets module |
| Git | needed at **configure** time, not build time (see below) |

Nothing else. PCRE2 (`pcre2-10.44`) and nlohmann/json (`v3.11.3`) are pinned by
tag and cloned by FetchContent during configure, which is why git has to be on
PATH before the first `cmake -S . -B build` rather than before the first build.

They are vendored rather than found on the system on purpose: the pin is what
makes Linux, macOS and Windows agree on a regex engine. A local build that
quietly used the distribution's PCRE2 10.42 would not reproduce anywhere else.

### Linux

```bash
# Debian/Ubuntu
apt install cmake build-essential git qt6-base-dev

# Fedora
dnf install cmake gcc-c++ git qt6-qtbase-devel

# Arch
pacman -S cmake base-devel git qt6-base
```

Or point at a Qt from the official installer — see "Choosing a Qt" below.

### macOS

```bash
brew install cmake qt
```

or the official Qt installer. Xcode command line tools for the compiler.

### Windows

Visual Studio 2019 or newer with the C++ workload, plus Qt for MSVC from the
official installer. Build from an **x64 Native Tools Command Prompt** — a plain
`cmd` with `cl.exe` on PATH is not equivalent, because `%VCToolsRedistDir%` is
what the packaging script uses to find the MSVC runtime and only the Developer
Command Prompt sets it.

## Quick start

```bash
git clone <this repo> && cd anyeditqt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/app/anyedit path/to/some/file.py
```

First configure takes about 20 seconds — most of it cloning PCRE2 and
nlohmann/json. Later configures reuse them, so prefer re-running
`cmake -S . -B build` over `rm -rf build` when you only need the cache
refreshed.

## Options

| Option | Default | What it does |
|---|---|---|
| `BUILD_WIDGET` | `ON` | The Qt widget (`acedqt::core`) and the `anyedit` app |
| `BUILD_PROBES` | `OFF` | `render_probe`, the offscreen tokenize-and-paint check |
| `BUILD_BINDINGS` | `OFF` | Python bindings — not implemented, see `bindings/README.md` |
| `ACED_BUILD_TESTS` | `ON` | Both test suites |

**Pass options explicitly with `-D` rather than relying on the defaults above.**
CMake's `option()` honours whatever is already in `CMakeCache.txt`, so changing
a default in `CMakeLists.txt` has no effect on a build directory that already
exists: the new target simply never appears, and `cmake --build build --target
help` lists everything except the one you wanted. Either pass the `-D` or
`rm -rf` the build directory.

Core only, no Qt at all:

```bash
cmake -S . -B build-core -DBUILD_WIDGET=OFF
```

That is a useful thing to do — `aced::core` has no Qt in it and the 78 core
tests run headlessly with no display and no Qt installed. Find and replace lives
there too, so most of the search behaviour is covered without a widget at all.

## Choosing a Qt

A machine with both a distribution Qt and one from the official installer will
let `find_package` reach whichever comes first. Which one it picked is printed
before anything builds:

```
-- Qt 6.10.3
--   prefix:  /home/you/Qt/6.10.3/gcc_64
--   QtCore:  /home/you/Qt/6.10.3/gcc_64/lib/libQt6Core.so.6.10.3
```

To choose:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=~/Qt/6.10.3/gcc_64
```

The prefix is the directory holding `bin/`, `lib/` and `include/` — e.g.
`~/Qt/6.10.3/gcc_64`, not `~/Qt` and not the `lib/cmake/Qt6` inside it.

**A cached `Qt6_DIR` beats `-DCMAKE_PREFIX_PATH` and says nothing about it.**
CMake offers no way to un-cache a `find_package` result, so pointing an existing
build directory at a different Qt means deleting it. The bundle scripts detect
this and wipe for you; by hand you have to.

## Targets

```
aced_core        static lib, no Qt. Document, anchors, undo, tokenizer,
                 grammars, search.
acedqt_core      static lib, Qt Widgets. Palette, line cache, editor widget.
anyedit_app      static lib, Qt Widgets. Settings, theme, window, prefs,
                 recent files, find bar, language picker.
anyedit          the application. A thin main() over anyedit_app.
aced_tests       78 core cases, headless, no Qt.
acedqt_tests     57 widget cases, real QKeyEvents, needs a platform plugin.
anyedit_tests    73 app cases, needs a platform plugin.
render_probe     offscreen tokenize-and-paint check (BUILD_PROBES=ON).
```

`anyedit_app` is a library rather than three more files in the executable so the
window, the settings and the preferences dialog can be constructed by a test. A
window reachable only by launching the application is a window whose menus get
checked by hand or not at all.

## Running

```bash
./build/app/anyedit                  # one empty tab
./build/app/anyedit file.py other.go # a tab each, mode from the extension
./build/app/anyedit --check          # load the corpus, report, exit
```

`--check` is for packaging: it resolves the grammar corpus, prints what it
found, and exits without an event loop. It deliberately does **not** write a
settings file — a packaging smoke test should not leave a config directory
behind on a build machine.

### Keys

```
Tab / Shift+Tab     indent, unindent (Ctrl+] / Ctrl+[ do the same)
Ctrl+F / Ctrl+H     find, find and replace
F3 / Shift+F3       next, previous
Esc                 close the find bar
Ctrl+, / Ctrl+0     preferences, reset font size
Alt+Z               word wrap
```

Tab with a selection spanning more than one line indents the block; inside a
single line it replaces the selection, because that is a typo being overtyped
rather than a block. A selection ending at column 0 does not drag in the line
the cursor landed on.

The language for a tab is set from the button in the status bar or from
View → Language. The find bar's three toggles are match case, whole word and
regular expression.
Matches do not span lines: `aced::Search` runs against one line at a time, so a
pattern for a multi-line block comment will not find one.

### Configuration

```
~/.anyeditqt/settings.json    preferences: editor group and app group
~/.anyeditqt/recent.json      the last 20 files opened
```

Preferences → Settings (`Ctrl+,`) edits the first through a dialog with live
preview and a real Cancel; Preferences → Edit settings.json opens it as a tab,
and saving that tab applies the change immediately through a
`QFileSystemWatcher`. Keys a build does not recognise are preserved across a
save, so a file written by a newer build survives an older one.

`$ANYEDITQT_CONFIG_DIR` overrides the directory:

```bash
ANYEDITQT_CONFIG_DIR=/tmp/scratch ./build/app/anyedit
```

A malformed `settings.json` is reported and left alone — defaults apply, and the
offending byte offset is named so the line can be found. A malformed
`recent.json` is ignored silently. That asymmetry is deliberate: one is your
configuration, the other is disposable state.

The application needs `grammars.json` at runtime and looks for it in this order:

```
<exe>/Resources/grammars.json          shipped layout, Windows and macOS
<exe>/../Resources/grammars.json       shipped layout, Linux tree
<exe>/../../grammars/grammars.json     running from a build directory
```

Without it the editor opens and highlights nothing. It says so rather than
failing silently, but if you have moved the binary somewhere odd that is the
reason.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

`ctest` hides each suite's own summary on success. Run them directly for that:

```bash
./build/core/aced_tests
#     198 grammars built, 0 with a rejected regex
# 78 cases, 0 failures

./build/widget/acedqt_tests    # needs a display, or:
QT_QPA_PLATFORM=offscreen ./build/widget/acedqt_tests
# 57 cases, 0 failures

QT_QPA_PLATFORM=offscreen ./build/app/anyedit_tests
# 73 cases, 0 failures
```

`anyedit_tests` points `$ANYEDITQT_CONFIG_DIR` at a fresh temporary directory
per case, so it can never read or write a real `~/.anyeditqt`.

**Watch the grammars line.** A few ace regexes use JS-only features PCRE2 will
not take — variable-length lookbehind, mostly. Those rules are skipped and the
rest of the grammar still works, so the failure mode is silent.
`test_grammar.cpp` asserts the count stays under 10 rather than letting it
drift.

The widget tests send real `QKeyEvent`s to a real `EditorWidget` under the
offscreen platform. Not mocks: every bug in an editor's input path is a corner
case, and none of them are visible by reading.

Two things about them that are easy to get wrong and silent when you do.
**Paint is forced with `grab()`, never `repaint()`** — under the offscreen
platform a window is never exposed, so `repaint()` returns without delivering a
paint event, and a timing test written with it reported `0ms` for months because
nothing was being painted. **Mouse events go to `ed->viewport()`, not to `ed`**
— `QAbstractScrollArea` receives them through an event filter on its viewport,
and one sent to the scroll area itself is dropped without a word.

## Packaging

```bash
./scripts/bundle-linux.sh                          # dist/anyedit-<ver>-linux-<arch>.tar.gz
./scripts/bundle-linux.sh --qt ~/Qt/6.10.3/gcc_64
./scripts/bundle-mac.sh --sign "Developer ID Application: ..." --dmg
scripts\bundle-windows.bat --qt C:\Qt\6.10.3\msvc2022_64 --zip
```

Linux produces a relocatable directory and a tarball — not an AppImage, because
an AppImage means linuxdeploy and its Qt plugin downloaded at build time, and
the tree this produces is what linuxdeploy would be pointed at anyway. macOS
produces `build-mac/app/anyedit.app` via `macdeployqt` — the directory is
`anyedit.app`, from the target's OUTPUT_NAME; Finder shows it as "AnyEdit", from
`MACOSX_BUNDLE_BUNDLE_NAME`. Windows produces a folder via `windeployqt`.

All three verify before they archive, because this kind of packaging has two
failure modes that look perfect on the machine that built it: loading the
*host's* Qt instead of the shipped one, and not finding `grammars.json`. The
smoke test deliberately runs the staged binary **without** telling it where the
corpus is.

On macOS the Qt half of that is checked with `DYLD_PRINT_LIBRARIES` — which
frameworks dyld actually opened — rather than by reading install names. `@rpath`
in an install name is the normal Qt 6 layout and says nothing either way; the
hazard is an absolute `LC_RPATH` left over from the build, pointing at the Qt
that was linked against. Leave it and the application resolves Qt through it on
the build machine and nowhere else. The script deletes it and re-signs, because
`install_name_tool` invalidates the signature and on Apple Silicon that is
`Killed: 9` rather than a warning.

`bundle-linux.sh` is fully verified. `bundle-mac.sh` has been run on a real Mac
and the failures that found are fixed, but its `--sign` and `--dmg` paths are
still unexercised. `bundle-windows.bat` has never been run at all and says so in
its own header.

## Regenerating the grammar corpus

Only needed to change the exporter or pull newer ace grammars. Needs Node and an
ace checkout.

```bash
git clone https://github.com/ajaxorg/ace.git ../ace && (cd ../ace && npm install)
cd grammars
node export_modes.js      # ace modes -> grammars.json
node fix_surrogates.js    # JS regex dialect -> PCRE2 dialect
```

Then check conformance against ace's own tokenizer before trusting it:

```bash
node reference.js spec.json > ref.json
node compare.js ref.json out.json
```

Baseline is **7638/9699 rows identical (78.8%)** across 187 modes. A change that
moves that number down is a regression, whatever else it fixes.
`grammars/README.md` explains how the export works.

## Layout

```
core/      aced::core      no Qt. Document, Delta, Anchor, UndoManager,
                           Tokenizer, Grammar. Tests run headlessly.
widget/    acedqt::core    Qt Widgets. Palette, LineCache, EditorWidget.
app/       anyedit_app     settings, theme, main window, preferences
                           dialog, recent files. anyedit is a thin main().
tools/     render_probe    offscreen tokenize-and-paint check.
grammars/  198 languages as data, plus the exporter and the conformance harness.
scripts/   per-platform packaging.
bindings/  planned PySide6 bindings. Not implemented.
```

The split is the point. `core/` has no Qt in it:

```bash
grep -rn '#include <Q' core/include core/src    # empty
```

(`grep -r "Q[A-Z]" core/` is not the test — it matches comments in `regex.h`
explaining how the PCRE2 wrapper maps onto `QRegularExpression`.)

That is what makes the model testable without a display, and what would let the
tokenizer be bound to Python without dragging Qt along.

`EditorWidget` derives from `QAbstractScrollArea`, not `QPlainTextEdit`, and
`aced::Document` owns the text. `QTextDocument` is not used. That costs
painting, scrolling and cursor geometry; it buys the Qt-free core and removes
the per-block allocation ceiling, so the same widget can be pointed at a config
file or at a very large log.

## Troubleshooting

**`Unknown CMake command "qt_standard_project_setup"`** — that function is Qt
6.3+, and something older satisfied `find_package(Qt6 6.2)`. The tree does not
call it; if you see this, a local edit reintroduced it.

**`undefined reference to EditorWidget::textChanged()`** — AUTOMOC did not run.
It looks for a `Q_OBJECT` header beside the `.cpp` that includes it, and
`include/` and `src/` are split here, so the headers are listed as target
sources in `widget/CMakeLists.txt`. Removing them from that list produces
exactly this.

**`ctest` says `No tests were found!!!` and exits 0** — `enable_testing()` has to
be called at the top level, not only in `core/`. It is in the root
`CMakeLists.txt`; do not move it down.

**A new target does not appear** — either configure was not re-run after adding
it (CMake does not notice a new `add_executable` on its own), or an option is
cached at its old value. Check `grep BUILD_ build/CMakeCache.txt`.

**The app starts but nothing is highlighted** — it did not find `grammars.json`.
Run `anyedit --check`, which prints the path it resolved or the paths it tried.

**Settings changes do not stick** — the window writes the file on exit and the
preferences dialog writes it on OK, so a process killed rather than closed loses
whatever was only in memory. Check which file is actually in play with
`anyedit --check`, which prints the settings path it resolved.

**`redefinition of qt_static_metacall`, or `out-of-line definition ... does not
match any declaration`** — there is a second copy of a header in `widget/src/`
or `core/src/`. AUTOMOC finds it as well as the real one in `include/`, moc's
the class twice, and the errors all point at generated code. Configure now
fails with a sentence instead; delete the stray copy and wipe the build
directory, because the stale autogen tree survives on its own.

**A large paste looks truncated** — fixed, and it was not truncated. Scrollbar
ranges were computed in `resizeEvent()` only, so inserting text never widened
the vertical range: the document held every line and the scrollbar stayed at
maximum 0, which made the first screenful all you could reach.
`updateScrollRanges()` runs on every edit now. Soft wrap hid it, because
`paintEvent` recomputes the range itself when wrapping — so it only ever showed
with wrap off, which is the default.

**A tab saved over the wrong file** — fixed, and worth knowing why it happened.
The tab bar is movable and the per-tab state was indexed by tab *position*, so
dragging a tab left the paths where they were and Ctrl+S then wrote to whichever
file used to be in that slot. Per-tab state is keyed by the editor widget now.
Anything else that grows a parallel list alongside `QTabWidget` has the same
trap waiting in it.

**The packaged build works here and dies elsewhere** — that is what the verify
steps in the bundle scripts are for, and if one of them passed while the package
is still broken, the check that is missing is worth adding there rather than
fixing by hand.