# Known gaps in the exported corpus

## Rules with no token type: 1184 rules across 128 modes

`export_modes.js` captures a rule's `token` when it is a string or an array.
When ace uses an `onMatch` closure instead, the rule survives with its regex and
loses its type. The tokenizer now emits `"text"` for those bytes (ace's own
default; `tokenizer.js` initialises `mapping.defaultToken` to `"text"`).

Before that it emitted nothing, and **the bytes went with it** -- consumers
compute byte offsets by laying token values end to end, so a dropped token
shifted every format run after it one position left. `javascript`'s `no_regex`
state opens with a bare `[{}]` rule, so every brace in every JS file was
missing from the stream. `test_grammar.cpp` now asserts that token values
reconstruct the line exactly, across all 198 modes.

The visible remainder is colour, not correctness: JS and TS braces render as
plain text rather than `paren`. Folding is unaffected -- it compares token type
families, and `text` matches `text` while `comment` and `string` do not.

Worst offenders, by rules lost:

    liquid 252 · razor 176 · handlebars 85 · csound_document 81
    csound_orchestra 71 · mask 62 · coldfusion 43 · vue 29

Fixing this is the same shape of work as the `createKeywordMapper()`
monkeypatch that took fully declarative modes from 76 to 153: intercept
`onMatch` at export time where its behaviour is data.

## Conformance

The `reference.js` / `compare.js` harness needs an ace checkout with
`npm install` and a `spec.json`; the producer of `out.json` is not in the repo,
so the 78.8% baseline has NOT been re-measured since the `defaultToken` change.
The change can only add tokens where bytes were previously dropped, and a row
that dropped bytes already differed from ace in token count, so it cannot lower
the number -- but that is an argument, not a measurement.
