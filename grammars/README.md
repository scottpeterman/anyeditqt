# grammars/

`grammars.json` is the corpus: 198 languages, ~34k rules, exported from an ace
checkout. It is data at runtime, so adding or fixing a language is a re-export,
not a rebuild.

## Regenerating

Needs an ace checkout at `../ace` with `npm install` already run.

    node export_modes.js     # ace modes -> grammars.json
    node fix_surrogates.js   # JS regex dialect -> PCRE2 dialect

`export_modes.js` does two non-obvious things, both load-bearing.

`normalizeRules()` has already flattened ace's `push:`/`pop:`/`include:` into a
flat state map by the time the rules can be observed, leaving two generated
closures plus `rule.nextState` sitting there as plain data. They are identified
by source text and re-encoded as `{op: "push"|"pop"|"goto"}`.

`createKeywordMapper()` returns a closure over a word→class table that looks
unportable. It is monkeypatched on `TextHighlightRules.prototype` before any
mode loads, so the table is captured as data. That one change took fully
declarative modes from 76 to 153.

## Conformance

`reference.js` runs ace's own JS tokenizer over the same input; `compare.js`
diffs the two. The corpus is ace's `demo/kitchen-sink/docs/`, one real sample
file per language.

Current, against 187 modes / 9699 rows:

    rows identical   : 7638/9699    78.8%
    tokens identical : 37872/51067  74.2%
    byte-identical   : 101/187 modes

The gap is one cluster, not a long tail: XML-family and template modes share a
377-instance `onMatch` closure in ace's `mode/xml_util.js` that splits a tag
into punctuation + name + punctuation, plus 151 custom `next` functions. Same
kind of work as the keyword mapper.

## Regex dialect

PCRE2 rejects 42 of 34094 regexes across 6 modes: 34 variable-length
lookbehinds (a JS-only feature, concentrated in `gdresource`), 7 invalid class
ranges, 1 `\L`-style escape. A rejected rule is skipped and the rest of the
grammar still works; `test_grammar.cpp` guards against that set growing.

It was 96 before `fix_surrogates.js`. 52 were `\uFFFE` → `\x{FFFE}`, pure
syntax. Two were grammars spelling "any astral character" as a UTF-16
surrogate-pair range, meaningless in UTF-8. That second class disappears
entirely on QString, which is UTF-16 like JS.

Go's stdlib `regexp` is RE2 and cannot run this corpus at all — 4391 regexes
use lookahead, which RE2 has no form of.
