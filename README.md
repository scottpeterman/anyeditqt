# anyeditqt

[github.com/scottpeterman/anyeditqt](https://github.com/scottpeterman/anyeditqt)
· GPLv3 · v0.1.0

A code editor in the shape of [anytermqt](https://github.com/scottpeterman/anytermqt):
a Qt-free C++ core, a Qt widget over it, PySide6 bindings over that. No Go.

![AnyEdit](https://raw.githubusercontent.com/scottpeterman/anyeditqt/refs/heads/main/screenshots/slides.gif)

The model and the syntax engine come from [ace](https://github.com/ajaxorg/ace) —
not as a port of its code, but as a port of its architecture plus its 198
language grammars, consumed as data.

    core/      aced::core     model + tokenizer.  Builds and passes tests.
    widget/    acedqt::core   Qt view.            Builds and passes tests.
    app/       anyedit        the editor itself.  Builds and passes tests.
    bindings/  anyeditqt      PySide6.            Planned.
    grammars/  the corpus + the exporter that produces it from an ace checkout.

## Status

    core    3147 lines impl   1638 lines tests  126 cases, 0 failures
    widget  2359 lines impl   1523 lines tests   92 cases, 0 failures
    app     2764 lines impl   1646 lines tests   83 cases, 0 failures
    corpus  198 grammars, 34094 rules, 78.8% row-identical vs ace's own tokenizer

Packaged and run on all three platforms: `bundle-linux.sh`, `bundle-mac.sh` and
`bundle-windows.bat`.

`anyedit` opens files, highlights them and edits them: typing, Enter with indent
carry, Backspace/Delete joining lines, UTF-8-aware movement, Home/End,
PageUp/Down, shift-select, mouse click and drag, double-click word select,
Ctrl+Z/Y/A/C/X/V, block indent and unindent, tabs, save, soft wrap, code
folding, find and replace with literal, whole word, case and regex matching, a
language picker over the 198-grammar corpus, a recent files list, an Edit and
right-click menu, and a preferences dialog over `~/.anyeditqt/settings.json`.

Not implemented, and nothing declares them and ignores them: IME preedit
(`inputMethodEvent` commits only), multi-cursor, word-wise Ctrl+arrow, bracket
matching, xml/html folding, multi-line search — `aced::Search` runs against one
line at a time and says so — and detecting that a file changed on disk while it
is open. There is no sidebar, command palette or fuzzy file open, which is what
still separates this from the editor it is aimed at.

## Build

    cmake -S . -B build && cmake --build build && ctest --test-dir build
    ./build/app/anyedit some/file.py

`README_Building.md` covers the three platforms, the options, packaging and the
traps. `RELEASING.md` covers turning `dist/` into GitHub release assets. This
file is the design.

Core has two vendored dependencies, both pinned by tag through FetchContent:
PCRE2 and nlohmann/json. Both are PRIVATE apart from `<pcre2.h>` in `regex.h`.

## Design

The whole thing rests on one primitive:

    struct Delta {
        enum class Action { Insert, Remove };
        Action action;
        Position start, end;
        std::vector<std::string> lines;
    };

The document is `vector<string>`, one entry per line, and `applyDelta()` is the
only thing that mutates it. Everything else follows. A Delta inverts by flipping
its action, so undo is a stack of Deltas rather than snapshots. Anchors subscribe
to Deltas and move themselves. The tokenizer takes a dirty row range from them.

No rope, no piece table, no gap buffer — same as ace, and for the same reason:
edits are overwhelmingly single-line, and the single-line branch is one
`std::string::insert`.

`Anchor` is the piece with no counterpart in anytermqt. A terminal only appends,
so an absolute row index is stable forever; `RowStore::advance()` is `base_++`.
An editor mutates anywhere, so anything remembering a place — a cursor, a fold
boundary, an LSP diagnostic — has to be told when the ground moves. That is
where the bugs will be, which is why `test_anchor.cpp` is the largest test file
relative to what it covers.

## Why not QTextDocument

`EditorWidget` derives from `QAbstractScrollArea`, and `aced::Document` owns the
text. `QTextDocument` is not used at all.

It costs real work: painting, scrolling and cursor geometry become ours. It buys
a Qt-free core that tests headlessly, and it removes the per-block allocation
ceiling, so the same widget can be pointed at a device config or at a
multi-million-line route table. `TerminalWidget` made the same call.

What Qt still does for us is the part ace had to write by hand: `QTextLayout`
wraps text, which deletes most of ace's `edit_session.js` (~3,800 lines of
document→screen coordinate mapping that exists only because the DOM will not lay
out text for you).

Soft wrap is where that paid off. `LineCache` asks `QTextLayout` to wrap and
gets the screen lines back; what remains is a row→screen-line map, and the
interesting part of it is what it does NOT do. A row that has never been painted
is assumed to be one screen line tall and corrected the first time it is drawn,
because measuring all 50,000 rows of a file to size a scrollbar is the cost
`LineCache` exists to avoid — and it would be paid again on every edit. The
price is a scrollbar range that grows as wrapped rows are discovered.

## Folding

Two halves that do not know about each other.

**Where a fold starts and ends** comes from ace, ported in `core/foldmode.cpp`
from `src/mode/folding/`: cstyle covers 98 modes, an indentation mode 15,
pythonic 2. The other 83 have no fold mode, which is a normal state rather than
an error. A fold mode reads line text for the gutter marker and tokens for the
range, so a brace in a comment is not structure — that is `aced::TokenChain`,
which is separate from `LineCache` because `LineCache` keeps the tokenizer's
end-of-line state and throws the tokens away. It stays cold: the gutter decides
whether a row has a marker from the line alone, and tokens are read on a click.

**Hiding the rows** is `aced::LineMap`, and it is the same table soft wrap uses.
Height times visible gives display lines, so a folded row that was three screen
lines tall removes three, and one structure answers the scrollbar, hit testing
and Up/Down. The design is Scintilla's `ContractionState` — *"manages visibility
of lines for folding and wrapping"* — along with the lazy step in its
`Partitioning`, which turns a height or visibility change from an O(n) prefix
rebuild into O(1) plus a deferred fixup. Two maps that have to agree eventually
do not; fold a block, let a wrapped row inside it change height, and they
disagree about where everything below starts.

Collapsing an outer fold leaves the folds inside it alone, so reopening restores
them rather than flattening them — which is why `LineMap` tracks `expanded`
apart from `visible`. An edit landing on a hidden row opens the fold containing
it: a buffer whose visible text disagrees with its contents is the one failure a
folding editor cannot have.

## Why no Go

anytermqt's Go/C-ABI split earns its keep because Go owns a real domain on the
far side of a narrow boundary. An editor has no such domain: the tokenizer must
be C++ (RE2 cannot compile half the corpus), the document is coupled to the view,
and the boundary would run through per-keystroke shared mutable state.

If Go earns a place later it is as a sidecar process — an LSP supervisor, a
project indexer — talking JSON-RPC and holding no editor state. Not cgo.

## Find

`aced::Search` is in `core/`, has no Qt in it, and is tested without one. It
takes a needle and four flags — case, whole word, regex, wrap — and answers
`next`, `previous`, `all` and `replaceAll` over a `Document`. Three things in it
are not obvious and each was a bug first:

A non-regex needle is escaped character by character rather than wrapped in
`\Q...\E`, because `\Q` quoting ends at the first `\E` *in the needle* — and a
search through this repository for `\E` would find one. A zero-length match does
not advance the search offset on its own, so a pattern like `x*` loops forever
unless stepped past explicitly. And `replaceAll` applies back to front, because
replacing left to right shifts every later match by the difference in length and
the error compounds down the file.

The replacement is literal: `$1` inserts a dollar and a one. Half-supporting
backreferences fails as a wrong document rather than an error message.

`EditorWidget` paints the matches and knows nothing about where they came from,
which is why `setSearchMatches()` takes ranges rather than a term. A find bar,
"highlight every occurrence of the word under the cursor", and a language
server's rename preview all want the same painting.

## Languages

The mode is guessed from the file name and can be set by hand, from the status
bar or from View → Language. A hand-picked language is remembered as a *choice*
rather than a guess, so a Save As does not re-derive it from the new extension
— which is the one moment it matters, since the case the picker exists for is a
new buffer given a language before it has a name.

Two presentation details live in `app/languages.cpp` and not in the corpus. The
corpus keys are ace's mode ids — `c_cpp`, `apache_conf`, `objectivec` — and none
of them are what a language is called, so there is a table of about seventy
names and a generic prettifier for the long tail. And the menu is grouped A–Z:
198 items in one column is taller than a screen and unsearchable.

## Configuration

`~/.anyeditqt/settings.json`, in two groups: `editor` (font, tab width,
insert-spaces, gutter, current-line, wrap, palette per scheme), applied to every
tab; and `app` (theme — dark, light or follow the system — chrome font, window
geometry), applied to the application. Preferences → Settings edits it through a
dialog; Preferences → Edit settings.json opens the file as a tab. Neither is the
source of truth, so a change in one moves the other, and saving the file applies
it live through a `QFileSystemWatcher`.

The last 20 files opened live beside it in `~/.anyeditqt/recent.json`, kept
separate on purpose: settings are preferences, the recent list is state, and
writing the state file on every file open would otherwise churn a preferences
file the user may have open in a tab.

`$ANYEDITQT_CONFIG_DIR` overrides the directory. The test suite uses it so it can
never touch a real one.

## Licence

GPLv3; see `LICENSE`.

Qt is used under the LGPLv3, dynamically linked and unmodified, which LGPLv3
section 3 permits a GPLv3 work to do. That is not only a statement: the
packaging scripts deploy Qt beside the executable rather than linking it in, so
a recipient can replace it with their own build of the same version, and they
fail the package if `LICENSE`, `THIRD_PARTY_NOTICES.md` or `licenses/` is
missing from it. Conveying those with a binary is an obligation rather than a
courtesy, and a package without them looks no different from one with them.

Help → About carries the warranty disclaimer and the Qt acknowledgement, and
Help → About Qt shows The Qt Company's own notice.

## Credits

ace is BSD-3-Clause (Ajax.org B.V.). The grammar corpus in `grammars/` is
derived from ace's `mode/*_highlight_rules.js`, and `core/foldmode.cpp` from its
`mode/folding/`; both carry that licence. See `grammars/README.md` for how the
corpus is produced.

`core/linemap.cpp` follows the design of Scintilla's `ContractionState` and
`Partitioning`, Copyright 1998-2007 Neil Hodgson, under Scintilla's
attribution-only licence. The code is written here, not copied, but the
algorithm is Scintilla's. Nothing is taken from QScintilla's Qt wrapper, which
is GPLv3.

PCRE2 is BSD-3-Clause and nlohmann/json is MIT, both fetched at a pinned tag and
linked statically. `THIRD_PARTY_NOTICES.md` has the full texts;
`app/tests/test_notices.cpp` fails the build if the versions it names stop
matching the pins in `core/CMakeLists.txt`.
