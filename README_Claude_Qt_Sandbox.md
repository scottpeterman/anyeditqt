# README_Claude_Qt_Sandbox.md

Standing anyeditqt up in a Claude sandbox from cold, so changes get **built and
tested** there rather than reasoned about from reading the source.

The omegassh version of this document is the parent. This one is shorter in the
places that made that one hard: no Go module, so the GOPROXY dance and the
revert-before-delivering rule both disappear, and no sibling checkout to clone
before anything configures. What is left is ordinary CMake plus two dependencies
FetchContent pulls from GitHub, which is on the egress allowlist.

Every command below has been run in this sandbox and every number is from that
run. Verified on Ubuntu 24.04 with CMake 3.28.3 and Qt 6.4.2. The project also
builds and runs on Qt 6.10.3 from the official installer on the development
machine; the differences between those two are §8, and they are the only part of
this document that is not sandbox-verified.

---

## 1. Toolchain

```bash
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y cmake qt6-base-dev
```

Two packages. `qt6-base-dev` brings QtCore, QtGui and QtWidgets plus the
offscreen platform plugin, which is what makes the widget testable with no
display. No Qt Creator, no full SDK, no aqtinstall.

Optional, and worth having the moment something goes wrong:

```bash
apt-get install -y gdb patchelf
```

`gdb -batch -ex run -ex bt` found an infinite constructor recursion whose only
symptom was a segfault with no message. `patchelf --set-rpath` reproduced an
installer-layout Qt bug that the distribution Qt structurally cannot produce
(§7).

Node is already present and is needed only to regenerate the grammar corpus
(§5). Building, testing and packaging do not need it.

If `apt-get update` reports a 403 on a third-party repo, ignore it. On this
image it is nodesource:

```
E: Failed to fetch https://deb.nodesource.com/node_22.x/... 403 Forbidden
```

The Ubuntu archives are what matter and they are on the allowlist.

## 2. Dependencies come over FetchContent, and that works

PCRE2 and nlohmann/json are pinned by tag in `core/CMakeLists.txt` and cloned at
configure time from `github.com`, which is allowlisted. Nothing to pre-fetch,
nothing to `-replace`.

Do not switch PCRE2 to the system `libpcre2-dev`. It happens to work on this
image, and that is the problem: the pin exists so Linux, macOS and Windows get
the same engine, and a local build that quietly used 10.42 instead of 10.44
would not reproduce anywhere else.

## 3. Configure, build, test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_WIDGET=ON -DBUILD_PROBES=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

**Pass every option explicitly.** See §7: a default changed in `CMakeLists.txt`
does not reach a build directory that already exists.

Timings from a cold run on **one** core, so close to a worst case:

```
configure (incl. FetchContent clone)   20s
build                                  38s
ctest                                   2s
bundle-linux.sh                        48s
build/ 224M    dist/anyedit 69M    tarball 26M
```

Expected:

```
1/3 Test #1: aced_core ........... Passed
2/3 Test #2: acedqt_widget ....... Passed
3/3 Test #3: anyedit_settings .... Passed
```

Run the binaries directly to see the summaries `ctest` hides on success:

```bash
./build/core/aced_tests                                # 57 cases
QT_QPA_PLATFORM=offscreen ./build/widget/acedqt_tests   # 39 cases
QT_QPA_PLATFORM=offscreen ./build/app/anyedit_tests     # 46 cases
```

`anyedit_tests` points `ANYEDITQT_CONFIG_DIR` at a fresh `QTemporaryDir` per
case, so it never reads or writes the real `~/.anyeditqt`.

`aced_tests` prints `198 grammars built, 0 with a rejected regex`. **That line is
the one to watch.** A handful of ace regexes use JS-only features PCRE2 will not
take; the rule is skipped and the rest of the grammar still works, so the failure
mode is silent. `test_grammar.cpp` asserts the count stays under 10 rather than
letting it drift.

## 4. Looking at the GUI with no display

Two levels, and the cheap one covers most of it.

**The render probe.** Tokenizes a real file with `aced::core`, turns token types
into `QTextCharFormat` runs, lays them out with `QTextLayout`, paints to a PNG:

```bash
QT_QPA_PLATFORM=offscreen \
  ./build/tools/render_probe core/src/document.cpp /tmp/shot.png
# mode=c_cpp rows=60 tokens=515 clamped=0 -> /tmp/shot.png
```

`clamped` must be **0**. Token spans are byte offsets from PCRE2; `QString`
indices are UTF-16. The probe converts by re-measuring each prefix rather than
assuming they agree, and counts any run it had to clamp. Non-zero means the
conversion has drifted, and it exits non-zero.

Run it against multibyte content before believing a change to the tokenizer.
`demo/kitchen-sink/docs/` in an ace checkout has 198 such files.

**Grabbing the widget itself.** A throwaway harness under `/tmp`, linked against
the built static libraries:

```cpp
QApplication app(argc, argv);
static aced::Grammar g;
g.loadFile("grammars/grammars.json");
acedqt::EditorWidget ed;
ed.resize(1000, 720);
ed.setGrammar(&g);
ed.openFile(argv[1]);
ed.show();
ed.setSelection({{14, 4}, {17, 20}});
qApp->processEvents();
ed.grab().save(argv[2]);
```

with `QT_QPA_PLATFORM=offscreen`. This is how the gutter width, the selection
band and the cursor position were checked; none of them are visible by reading.

**Keep throwaway harnesses out of the repo.** Write them under `/tmp`. Something
worth keeping becomes a `*_probe.cpp` in `tools/` with its own entry in
`tools/CMakeLists.txt`, as `render_probe` is.

Two things to know about offscreen. `QStandardPaths: XDG_RUNTIME_DIR not set` and
`OpenType support missing for ...` on stderr are both noise. And a wrapped
`QLabel` reports the height the layout believed before it wrapped, so overlap in
a grab is real rather than an artifact — the trap the omegassh document
describes, which applies here the moment there are dialogs.

**`repaint()` does not paint offscreen, and says nothing.** A window is never
exposed under this platform, so `repaint()` on one returns without delivering a
paint event. The 50k-line timing in `test_widget.cpp` was written with it and
reported `0ms` for months because nothing was being painted; the number only
became real when it changed to `grab()`. Any test that depends on a paint having
happened — anything reading the wrap map, `maxWidthSeen()`, or a scrollbar range
— has to use `grab()`.

**An unformatted QTextLayout is painted with whatever pen the painter holds.**
With no tokenizer there are no `QTextCharFormat` runs, and `QTextLayout::draw()`
falls back to the current pen -- which had last been set to draw the line
number, `0x4b5056` against a `0x1d1f21` background. Plain-text buffers rendered
in gutter grey on near-black. The first regression test for it passed with the
bug present, because it turned line numbers OFF and the painter default happened
to be readable; the gutter is the entire mechanism. A contrast test has to keep
the gutter on and count pixels in the palette's *own foreground*, since gutter
grey is brighter than the background and still unreadable.

**Populating a QComboBox emits `currentIndexChanged`.** The preferences dialog
connects that signal to "write the controls back to the settings", and filling
the font combos therefore wrote every spin box's *minimum* into the settings
before they had been loaded -- opening Preferences silently set the font to 6pt
and the tab width to 1. The constructor holds a loading flag across the whole of
its own construction now. Anything that builds controls and connects them in the
same function has this shape.

**Mouse events go to `ed->viewport()`, not to `ed`.** `QAbstractScrollArea`
receives them through an event filter on its viewport; one sent to the scroll
area itself is dropped silently, and the test then asserts against a cursor that
never moved.

## 5. Regenerating the grammar corpus

Only when changing the exporter or pulling newer ace grammars.

```bash
cd .. && git clone https://github.com/ajaxorg/ace.git && cd ace && npm install
cd ../anyeditqt/grammars
node export_modes.js      # ace modes -> grammars.json
node fix_surrogates.js    # JS regex dialect -> PCRE2 dialect
```

`github.com`, `codeload.github.com` and `registry.npmjs.org` are all
allowlisted, so both steps work here.

Check conformance against ace's own tokenizer before trusting the result:

```bash
node reference.js spec.json > ref.json
node compare.js ref.json out.json
```

Baseline is **7638/9699 rows identical (78.8%)** across 187 modes. A change that
moves that number **down** is a regression, whatever else it fixes.

## 6. Packaging

```bash
./scripts/bundle-linux.sh                         # dist/anyedit-<ver>-linux-x86_64.tar.gz
./scripts/bundle-linux.sh --qt ~/Qt/6.10.3/gcc_64
./scripts/bundle-linux.sh --target render_probe
```

`bundle-linux.sh` runs end to end here and is the verified one.

`bundle-mac.sh` has now been rehearsed on Linux behind stubs for the Darwin-only
commands — `uname`, `sysctl`, `stat -f%z`, `otool`, `macdeployqt`, `lipo`,
`codesign` — plus a `cmake` wrapper that fakes the one thing CMake does on macOS
and cannot do here: turn a MACOSX_BUNDLE target into a `.app`. That exercises
target lookup, the resource copy, the smoke test and every error path for real.
It does NOT exercise macdeployqt, install-name rewriting, codesign or hdiutil,
and nothing here can.

`bundle-windows.bat` **ran clean on the first attempt** on Windows with Qt
6.10.3 and VS 2022: 198 modes, one Qt throughout, the staged tree finding its
own `Resources/grammars.json` through `applicationDirPath()`. One guard fired,
and it is the one that matters --

    ==> copying the MSVC runtime from ...\Microsoft.VC143.CRT

`windeployqt --compiler-runtime` was asked for and did not deliver the CRT
DLLs. The script checks for `VCRUNTIME140.dll` rather than trusting the flag,
and went to `%VCToolsRedistDir%` itself. A script that trusted the flag would
have produced a folder that runs on the build machine -- which has the VS 2022
redistributable installed system-wide -- and dies at launch on a clean one with
a missing-DLL dialog naming neither Qt nor anyedit.

The rehearsal is worth keeping. Four bugs came out of it that a reading had
missed, and three of them only fire on the error paths — which is to say, on the
day something else is already wrong:

- `find -maxdepth 3 -type d -name "*.app" | head -1` returns whichever `.app`
  the filesystem hands back first. With `-DCMAKE_MACOSX_BUNDLE=ON` making
  *every* executable a bundle, that was `tools/render_probe.app`, and the
  script packaged the probe instead of the editor. The flag is gone — `anyedit`
  carries `MACOSX_BUNDLE` as a target property, which is where the decision
  belongs — and the search is by name.
- The smoke test passed **render_probe's** arguments to `anyedit`, which opens
  that file in a window and enters the event loop. On Linux `timeout 30` would
  have caught it; macOS has no `timeout(1)` and Windows batch has nothing, so it
  would simply have hung. Both scripts now branch on the target and use
  `--check`.
- `find -perm +111` is BSD syntax that GNU find rejects outright, so the error
  path *itself* errored. Executability is tested in the shell now.
- `QT_QPA_PLATFORM=""` is not "use the default": Qt takes it as a request for a
  plugin named `""` and aborts with a message indistinguishable from a bundle
  missing its plugins. The variable is exported or unset, never emptied.

And two more from the first real run on a Mac, which the rehearsal could not
have found because both are about macdeployqt:

- **`@rpath` in the install names is the modern layout and is not a fault.** A
  Qt 6 CMake build links against `@rpath/QtCore.framework/...` and carries an
  `LC_RPATH`; macdeployqt copies the frameworks in and leaves those install
  names alone. The verify step demanded `@executable_path` and failed a bundle
  that was entirely correct. What matters is the rpath list — the build adds an
  absolute one pointing at the Qt that was linked against, and leaving it in
  means the application resolves Qt through it *on the build machine*, so the
  bundle looks perfect there and fails everywhere else. The script now adds
  `@executable_path/../Frameworks`, deletes any absolute rpath that can still
  resolve Qt, and checks with `DYLD_PRINT_LIBRARIES` which Qt was actually
  opened rather than inferring it from what the binary says it wants.
- **`install_name_tool` invalidates the signature.** On Apple Silicon that is
  not a warning, it is `Killed: 9` on the next launch — and the next launch is
  the smoke test. Anything that rewrites a binary has to ad-hoc re-sign before
  running it.

The smoke test runs the staged binary with **no grammar argument**, because
finding `Resources/grammars.json` through `applicationDirPath()` is the thing
being tested. `anyedit --check` loads the corpus, prints three lines and exits
without an event loop, and exists for exactly this:

```
==> smoke test: running the staged binary without telling it where the corpus is
    grammars: .../dist/anyedit/Resources/grammars.json
    modes: 198
    qt: 6.4.2
==> every Qt library resolves inside the bundle
```

## 7. Traps

All of these were hit for real. Each one built cleanly, or looked like success,
and cost time.

**`enable_testing()` must be at the top level.** In `core/CMakeLists.txt` alone,
the tests build fine and `ctest` against the top build directory reports
`No tests were found!!!` and exits 0. Nothing errors.

**`option()` honours an existing cache entry.** Changing a default in
`CMakeLists.txt` does not reach a build directory that already exists, so a tree
carried forward from before `widget/` had sources keeps `BUILD_WIDGET:BOOL=OFF`
and the app target never appears. Pass every option explicitly with `-D`, or
`rm -rf` the build directory. The bundle scripts do the former.

**AUTOMOC needs the `Q_OBJECT` header listed as a target source.** It looks for a
header beside the `.cpp` that includes it; with `include/` and `src/` split it
finds nothing, moc never runs, and the link fails on every signal with
`undefined reference to EditorWidget::textChanged()` — which reads as a missing
source file.

**`qt_standard_project_setup()` is Qt 6.3+.** A machine with an older system Qt
satisfies `find_package(Qt6 6.2)` and then fails with `Unknown CMake command`.
Set `CMAKE_AUTOMOC` / `AUTOUIC` / `AUTORCC` directly instead.

**`set -o pipefail` plus `grep -q` is a false negative.** `grep -q` exits on
match, the upstream command takes SIGPIPE, the pipeline reports failure, and
`if !` inverts a successful match into "not found". Capture to a variable, then
grep the variable.

**Static PCRE2 leaks into the dynamic symbol table.** Without
`-Wl,--exclude-libs,ALL`, `pcre2_compile_8` is exported globally and Qt6Core and
Qt6Gui — which pull in the *system* libpcre2 through glib — resolve against our
copy. Two PCRE2 versions in one process, and which one a call reaches depends on
link order. Assert with `readelf --dyn-syms -W <bin> | grep pcre2_compile_8`
being empty. Linux only: Mach-O and MSVC executables do not re-export their
static archives.

**Use `readelf -d`, not `ldd`, to ask how something was linked.** `ldd` prints the
transitive closure, and Qt pulls in the system libpcre2 through glib, so
`ldd | grep pcre2` is always non-empty and says nothing.

**Which Qt was found is not obvious and must be printed.** A machine with both a
distribution Qt and an installed one will let `find_package` reach whichever comes
first, and packaging then deploys the libraries *that* Qt resolves to.
`tools/CMakeLists.txt` and `widget/CMakeLists.txt` print the version, the prefix
and the resolved `libQt6Core` before anything builds.

**Sort Qt versions with `sort -V`, never `sort`.** Text order puts 6.9.1 above
6.10.3.

**Plugin RUNPATH can point into the staging directory.** Qt from the official
installer builds plugins with `RUNPATH=$ORIGIN/../../lib`; staged at
`<stage>/plugins/platforms/`, that resolves to `<stage>/lib`, so `ldd` on a staged
plugin reports the bundle's own libraries and `cp` is asked to copy a file onto
itself. A distribution Qt has no RUNPATH on its plugins at all, so the same loop
reads from the system and works. **This bug is invisible on the sandbox's Qt and
certain on an installer Qt.** Resolve plugin dependencies against the originals,
never the staged copies.

**Re-run configure after adding a source file or a target.** CMake does not notice
a new `add_executable` on its own, and the failure reads as a broken CMakeLists
rather than a stale cache.

## 8. What the sandbox cannot tell you

- **No real input.** Offscreen covers painting and layout. It cannot deliver a
  drag, a window-manager close event, focus changes, or IME composition — and IME
  is exactly where an editor's input path is hardest. Xvfb plus xdotool would
  cover the first three; nothing here covers the last.
- **No clipboard.** `QClipboard` under offscreen is process-local, so copy/paste
  round-trips within one process and tells you nothing about the platform. The
  widget tests do exactly that round-trip and are worth exactly that much.
- **One monospace family.** Anything depending on fallback for CJK, emoji or a
  missing glyph looks fine here and nowhere else. Cell-width measurement in
  particular needs a real box.
- **Distribution Qt only, 6.4.2.** The development machine runs 6.10.3 from the
  official installer, and the two differ in ways that matter for packaging (the
  plugin RUNPATH above) and possibly for `QTextLayout` cursor geometry.
- **No Windows and no macOS.** Qt's per-platform seams are untested here. All
  three bundle scripts have now produced a working package -- linux and windows
  for real, mac rehearsed behind stubs (§6) -- but every Qt behaviour they
  depend on was confirmed on the target machine, not in this sandbox.
- **No real context menu, and no real keyboard.** Both of the folding UI's
  platform bugs were of this kind and neither could be reproduced here.
  `Qt::CustomContextMenu` on the viewport never fires, because
  `QAbstractScrollArea`'s event filter takes `QEvent::ContextMenu` off the
  viewport before `QWidget::event()` -- which is what emits that signal -- can
  see it; nothing errors, the menu simply does not appear. And
  `QKeyEvent::key()` carries the character the LAYOUT produces, so Ctrl+Shift+[
  arrives as `Key_BraceLeft`, and a handler matching `Key_BracketLeft` never
  fires while the menu entry for the same sequence keeps working. Synthetic
  `QContextMenuEvent`s and synthetic `QKeyEvent`s test the handler, never the
  routing that reaches it. `ANYEDITQT_DEBUG_EVENTS=1` prints where a context
  menu request gets to.

A change touching any of those gets built and unit-tested here, and confirmed on a
real machine before it counts as working.

## 9. Where the project stands

```
core/      aced::core     document, anchors, undo, tokenizer.   57 tests.
widget/    acedqt::core   palette, line cache, editor widget.   39 tests.
app/       anyedit_app    settings, theme, window, prefs, recent. 46 tests.
           anyedit        the executable: corpus, window, loop.
tools/     render_probe   offscreen tokenize-and-paint check.
grammars/  198 languages as data, plus the exporter.
```

`anyedit` opens files, highlights them and edits them: typing, Enter with indent
carry, Backspace/Delete joining lines, UTF-8-aware arrows, Home/End, PageUp/Down,
shift-select, mouse click and drag, double-click word select, Ctrl+Z/Y/A/C/X/V,
tabs, save.

**Soft wrap is implemented.** `LineCache` lays a row out into several
`QTextLine`s, and `WrapMap` in the widget maps document rows onto screen lines
for the scrollbar, hit testing, Up/Down and Home/End. A row that has never been
painted is assumed to be one screen line tall and corrected the first time it is
drawn — measuring all 50,000 rows of a file to size a scrollbar is exactly the
cost `LineCache` exists to avoid. The visible consequence is a scrollbar range
that grows as wrapped rows are discovered.

**Settings are implemented**, in `~/.anyeditqt/settings.json`, in two groups:
`editor` (font, tab width, insert-spaces, gutter, current-line, wrap, palette
per scheme) applied to every tab, and `app` (theme, chrome font, window
geometry) applied to the application. There are two editors of it: a
Preferences dialog (Ctrl+,) with live preview and a real Cancel, and the file
itself, which Preferences → Edit settings.json opens as a tab. Neither is the
source of truth -- `Settings` is -- so a change in one moves the other. `$ANYEDITQT_CONFIG_DIR` overrides the
directory, which is what makes the tests safe. The last 20 files opened live
beside it in `~/.anyeditqt/recent.json` and NOT inside settings.json: that file
is preferences, and rewriting it on every file open would fire the settings
watcher, reload the preferences dialog and every tab, and churn a file the user
may have open in a tab. A corrupt recent.json is ignored silently; a corrupt
settings.json is reported and left alone. The asymmetry is deliberate. Keys the running build does not
recognise are preserved across a save. Preferences → Settings opens the file as
a tab; saving it fires a `QFileSystemWatcher` and the change applies live.

**Code folding is implemented**, in two halves that do not know about each
other. Fold ranges come from ace (`core/src/foldmode.cpp`, a port of
`src/mode/folding/`): cstyle for 98 modes, an indentation mode for 15, pythonic
for 2, and nothing for the other 83, which is a normal state rather than an
error. Hiding the rows is `aced::LineMap`, the same table soft wrap uses --
height times visible gives display lines, one answer, so a folded row that was
three screen lines tall removes three. `aced::TokenChain` gives the fold modes
tokens with byte offsets, so a brace in a comment is not structure; it is
separate from `LineCache`, which keeps end states and throws tokens away, and it
stays cold because the gutter decides whether a row has a marker from the line
text alone.

Collapsing an outer fold leaves the folds inside it alone, so reopening restores
them; that is why `LineMap` tracks `expanded` apart from `visible`. An edit
landing on a hidden row opens the fold containing it -- a buffer whose visible
text disagrees with its contents is the one failure a folding editor cannot
have. Markers are painted polygons, not glyphs, because offscreen cannot tell
you whether font fallback found anything.

Not implemented, and nothing declares them and ignores them: **IME preedit**
(`inputMethodEvent` commits only), multi-cursor, word-wise Ctrl+arrow, bracket
matching, xml/html folding (the biggest gap in the fold-mode port), and fold
state surviving a file reload.
