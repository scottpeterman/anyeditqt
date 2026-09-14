// include/acedqt/editorwidget.h
//
// EditorWidget derives from QAbstractScrollArea, not QPlainTextEdit, and
// aced::Document owns the text. QTextDocument is not used.
//
// That costs painting, scrolling and cursor geometry. It buys a Qt-free core
// that tests headlessly, and no per-block QTextBlock allocation, so the same
// widget can be pointed at a device config or at a multi-million-line route
// table. anytermqt's TerminalWidget made the same call against the same base.
//
// SCOPE. Soft wrap and code folding are implemented, and they share one table
// (aced::LineMap): a document row occupies `height` screen lines unless it is
// hidden, in which case it occupies none. Everything that takes a row --
// scrolling, hit testing, vertical movement, Home/End -- knows the difference.
// IME preedit is still not implemented (inputMethodEvent commits only), and
// neither are multi-cursor or find/replace. Nothing below pretends otherwise.
#pragma once

#include <QAbstractScrollArea>
#include <QPointF>
#include <QFont>
#include <memory>
#include <vector>

#include "aced/delta.h"
#include "acedqt/palette.h"

namespace aced {
class Document;
class Grammar;
class UndoManager;
}  // namespace aced

namespace acedqt {

class EditorWidget : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit EditorWidget(QWidget *parent = nullptr);
    ~EditorWidget() override;

    // The widget owns its Document. Exposed because commands, a find bar and a
    // language client all need to edit it directly; routing everything through
    // the widget would make it a god object.
    aced::Document *document() const;
    aced::UndoManager *undo() const;

    QString text() const;
    void setText(const QString &text);
    bool openFile(const QString &path, QString *error = nullptr);
    bool saveFile(const QString &path, QString *error = nullptr);

    // The corpus is shared, not owned: one process, one 7MB corpus, however
    // many tabs. Must outlive the widget.
    void setGrammar(const aced::Grammar *grammar);
    void setMode(const QString &mode);
    QString mode() const;

    void setEditorFont(const QFont &font);
    // The font the text is actually laid out with. QWidget::font() is not it:
    // LineCache holds the editor font, and the two differ the moment a host
    // sets one without the other.
    QFont editorFont() const;
    void setEditorPalette(const Palette &p);
    void setShowLineNumbers(bool on);
    void setTabWidth(int columns);

    // Off by default. When on, a row too wide for the viewport continues on the
    // next screen line instead of running under the horizontal scrollbar.
    void setSoftWrap(bool on);
    bool softWrap() const;
    // Where to wrap, in characters. Zero -- the default -- wraps at whatever
    // the viewport is currently wide enough for, so it follows a resize.
    void setWrapColumn(int columns);
    int wrapColumn() const;

    // Pixel height of one screen line. A whole number: the paint loop clips
    // each screen line to its own band and fractional bands clip descenders.
    qreal lineHeight() const;

    // Screen lines, not document rows: these differ only when wrapping is on.
    // Both are what the scrollbar and PageUp/PageDown are measured in.
    int screenLineCount() const;
    int screenLineForRow(int row) const;
    // Tab inserts tabWidth spaces when true, one '\t' when false. The document
    // stores exactly what was inserted either way; this is not a display trick.
    void setInsertSpaces(bool on);
    void setHighlightCurrentLine(bool on);

    aced::Position cursor() const;
    void setCursor(aced::Position p);
    bool hasSelection() const;
    aced::Range selection() const;
    void setSelection(const aced::Range &r);
    void selectAll();
    void clearSelection();

    // Indent or unindent every line the selection touches, or the cursor's own
    // line when there is none. One undo step each, and the selection is left
    // covering the same lines so the keys repeat.
    void indentSelection();
    void unindentSelection();

    // --- folding ---------------------------------------------------------
    //
    // The fold mode comes from setMode(); 115 of the 198 corpus modes have
    // one and the rest report false everywhere, which is a normal state and
    // not an error. Fold ranges are ace's; hiding the rows is aced::LineMap.
    //
    // NESTING. Collapsing an outer fold leaves the state of the folds inside
    // it alone, so reopening it restores them rather than flattening them.
    // That is why LineMap tracks `expanded` separately from `visible`.
    bool canFold(int row) const;
    bool isFolded(int row) const;
    bool foldRow(int row);
    bool unfoldRow(int row);
    bool toggleFold(int row);
    void foldAll();
    void unfoldAll();
    // Reveal `row` if a fold is hiding it, opening whatever is in the way.
    // Called for you when an edit or a cursor move lands on a hidden row.
    void revealRow(int row);
    // Fold the innermost region containing the cursor, for the keyboard case
    // where the cursor is in a body rather than on a header.
    bool foldEnclosing();
    // The row whose fold marker sits under a viewport point, or -1. The marker
    // column is the right-hand edge of the gutter.
    int foldMarkerRowAt(const QPointF &pt) const;

    // Ranges to paint as search hits, in document order. They are view state
    // only: the widget neither runs the search nor owns the term, because a
    // find bar, a "highlight all occurrences of the word under the cursor" and
    // a language server's rename preview all want to paint the same way and
    // none of them belong in here.
    void setSearchMatches(const std::vector<aced::Range> &matches);
    void setCurrentSearchMatch(const aced::Range &r);
    void clearSearchMatches();
    int searchMatchCount() const;

    void copy();
    void cut();
    void paste();

    // The menu versions of Ctrl+Z / Ctrl+Y. Not named undo() because that is
    // already the accessor for the UndoManager; these are the whole operation,
    // including dropping the cached layouts and putting the caret back where
    // the change was.
    bool undoEdit();
    bool redoEdit();
    bool canUndo() const;
    bool canRedo() const;

    // Document position under a viewport point. Folding and wrapping both make
    // this non-obvious, so a host that needs it -- a context menu deciding
    // whether the click was inside the selection, a hover tooltip -- must ask
    // rather than divide y by the line height.
    aced::Position positionForPoint(const QPointF &pt) const;

    int firstVisibleRow() const;
    int visibleRowCount() const;
    void scrollToRow(int row);
    void ensureCursorVisible();

    bool isModified() const;
    void setModified(bool m);

Q_SIGNALS:
    void textChanged();
    void cursorMoved(int row, int column);
    void selectionChanged(bool hasSelection);
    void modifiedChanged(bool modified);
    void foldsChanged();
    // Right-click, or the Menu key. Both positions, because they answer
    // different questions and deriving one from the other is where this goes
    // wrong: `viewportPos` is what positionForPoint() takes, and `globalPos`
    // is where to put the menu -- taken straight off the event rather than
    // mapped, which is what QScintilla does and what removes a whole class of
    // off-by-a-frame error. See contextMenuEvent() for why setting
    // Qt::CustomContextMenu on the viewport does not work at all.
    void contextMenuRequested(const QPoint &viewportPos, const QPoint &globalPos);

protected:
    // Tab AND Backtab have to be taken before QWidget::event() sees them. That
    // function treats both as focus navigation and only calls keyPressEvent()
    // when focusNextPrevChild() finds nowhere to go -- so a bare EditorWidget
    // indents correctly and the same widget inside a QTabWidget silently moves
    // focus to the tab bar instead. Backtab was left alone while unindent did
    // not exist, on the grounds that swallowing a key to do nothing is worse
    // than leaving it as navigation. It does something now.
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    // Screen line the cursor sits on, and the sub-line within its row.
    int cursorScreenLine(int *subLine) const;
    // Pixels available to text: viewport less gutter less a caret column.
    qreal textWidth() const;
    // Viewport point to document position, wrapping accounted for.
    aced::Position positionAt(const QPointF &pt) const;
    // Move the cursor off a row a fold has just hidden. `preferred` is the
    // header row to land on, or -1 for the nearest visible row above.
    void rescueCursor(int preferred);
    // Pointer shape for a viewport point: I-beam over text, arrow over the
    // gutter, hand over a fold marker.
    void updateHoverShape(const QPointF &pt);
    // Emit cursorMoved() if the caret is not where `before` says it was.
    void emitIfCursorMoved(aced::Position before);
    // Scoped form of the above, declared at the top of anything that can move
    // the caret. Branch-proof: keyPressEvent alone has twenty ways out.
    struct CursorWatch;
    friend struct CursorWatch;
    // Scrollbar ranges from the document's current size. Called on resize AND
    // on every edit; see the note in the .cpp.
    void updateScrollRanges();

    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace acedqt
