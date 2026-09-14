# widget/

`acedqt::core` — the Qt view over `aced::core`. Builds, and `ctest` runs its
suite offscreen.

    palette.cpp       token scope -> QTextCharFormat, longest-prefix fallback
    linecache.cpp     tokenize + lay out per row, cached, invalidated by delta
    editorwidget.cpp  QAbstractScrollArea: paint, scroll, keyboard, mouse

`EditorWidget` derives from `QAbstractScrollArea` and `aced::Document` owns the
text. `QTextDocument` is not used. That costs painting, scrolling and cursor
geometry; it buys a Qt-free core that tests headlessly and no per-block
allocation, so the same widget can be pointed at a device config or at a
route table.

## Not implemented

**Soft wrap.** Rows map one-to-one onto screen lines. Adding it means
`QTextOption::WrapAtWordBoundaryOrAnywhere` in `LineCache` plus a row→screen-line
map in the widget. Nothing declares it and ignores it — there is no
`setSoftWrap()` to call.

**IME preedit.** `inputMethodEvent` commits and does not render composition.
That is also the one thing the sandbox cannot test at all, so it waits for a
real box.

Also absent: folding, multi-cursor, find/replace, word-wise Ctrl+arrow, bracket
matching, auto-indent beyond carrying leading whitespace.

## Tests

`tests/test_widget.cpp` is a real `QApplication` sending real `QKeyEvent`s under
`QT_QPA_PLATFORM=offscreen` — not mocks, because every bug in an editor's input
path is a corner case and none of them are visible by reading. 23 cases.

Two are worth keeping when this grows: the UTF-8 ones (backspace and right-arrow
must step a whole sequence, not a byte), and the block-comment one, which is the
only thing that catches `LineCache` failing to drop the tail when an edit changes
what every row below it means.
