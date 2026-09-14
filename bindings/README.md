# bindings/ — planned, not built

PySide6 via shiboken6, following anytermqt exactly: a `typesystem_*.xml`, a
`*_global.h`, and scikit-build-core driving the same CMake the C++ build uses.

Two things are worth settling before writing any of it.

**The wheel must not vendor Qt.** PySide6 already ships a complete Qt, it is
imported first, and its copy wins in-process regardless of what this module
linked against. Vendoring a second one reproduces a crash on every user's
machine. The cost is an exact pin on `pyside6==shiboken6==shiboken6_generator`.
anytermqt's `pyproject.toml` has this written up; copy it.

**The core is separately bindable, and that may be the more useful artifact.**
`aced::core` has no Qt in it. A `pip install` that gives Python a syntax
tokenizer covering 198 languages, with no Qt and no Node in the dependency
tree, is useful to people who will never want the widget — Pygments' lexer
coverage without writing 198 lexers. That argues for two modules:

    anyeditqt.core     pybind11 over aced::core, no Qt, wheels everywhere
    anyeditqt.widget   shiboken over acedqt, pinned to PySide6

pybind11 rather than shiboken for the first one: shiboken exists to make C++ Qt
types work as Python Qt types, and the core has no Qt types. Using it there
would drag in the pin for nothing.
