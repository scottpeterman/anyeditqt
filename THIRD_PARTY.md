# Third-party code and derived work

## ace (ajaxorg/ace) — BSD 3-Clause

`grammars/grammars.json` is a mechanical export of ace's 198 highlight rule
sets, produced by `grammars/export_modes.js`. `core/src/grammar.cpp` and
`core/include/aced/tokenizer.h` are a port of ace's `tokenizer.js` to C++.

`core/src/foldmode.cpp` ports ace's `src/mode/folding/` -- `fold_mode.js`,
`cstyle.js`, `coffee.js` and `pythonic.js` -- covering 115 of the 198 corpus
modes. `core/src/foldtable.inc` records which fold mode each mode uses,
generated from the ace tree.

## Scintilla — Neil Hodgson, permissive (attribution)

`core/include/aced/linemap.h` and `core/src/linemap.cpp` follow the design of
Scintilla's `ContractionState` (one table for folding and wrapping) and the
lazy-step prefix table of its `Partitioning.h`. The code is written here, not
copied, but the algorithm is Scintilla's.

    Copyright 1998-2007 by Neil Hodgson <neilh@scintilla.org>
    Permission to use, copy, modify, and distribute this software and its
    documentation for any purpose and without fee is hereby granted, provided
    that the above copyright notice appear in all copies and that both that
    copyright notice and this permission notice appear in supporting
    documentation.

NOT used: QScintilla's Qt wrapper (`src/`, `qsci/` in the QScintilla tree),
which is GPLv3 or Riverbank commercial. Nothing from those directories has been
read into this project.

## PCRE2 — BSD 3-Clause, vendored at 10.44 by FetchContent
## nlohmann/json — MIT, vendored at 3.11.3 by FetchContent
