#!/usr/bin/env python3
"""Convert a Sublime Text .sublime-syntax into an anyeditqt grammar.

    python3 grammars/import_sublime_syntax.py <file.sublime-syntax> [--name mode]
    python3 grammars/import_sublime_syntax.py <file> --merge grammars/grammars.json

WHY THIS IS POSSIBLE AT ALL. Sublime's newer YAML syntax format and the grammar
corpus exported from ace describe the same machine:

    contexts        states
    match           regex
    scope           token
    push/set/pop    next: {op, state}
    captures        tokenArray
    meta_scope      defaultToken

That is a closer match than ace's own format was before export_modes.js
flattened it. The conversion below is mechanical; what is NOT mechanical is
listed under LOSSY at the bottom, and every instance is counted and reported
rather than dropped quietly.

Scope names pass through almost unchanged. Sublime uses TextMate scope
conventions and so does ace, and acedqt::Palette walks a token name up its dots
until it finds a colour -- so `constant.numeric.ip.ipv4.address` resolves at
`constant` with no mapping table at all. Only Sublime's structural scopes, the
`text.<language>...` ones that mark regions rather than colour them, need
redirecting; they become `text`.
"""

import argparse
import json
import re
import sys
from collections import OrderedDict

# Scope roots acedqt::Palette knows how to colour. Anything else is structure
# rather than highlighting -- Sublime's `text.network.cisco.bgp` marks a region,
# it does not ask for a colour -- and becomes plain text.
HIGHLIGHT_ROOTS = {
    "comment", "constant", "entity", "invalid", "keyword", "markup", "meta",
    "punctuation", "storage", "string", "support", "variable",
}


class Lossy:
    """Counts what the conversion could not carry across, by kind."""

    def __init__(self):
        self.counts = OrderedDict()
        self.examples = {}

    def note(self, kind, detail=""):
        self.counts[kind] = self.counts.get(kind, 0) + 1
        if kind not in self.examples and detail:
            self.examples[kind] = detail

    def report(self, out=sys.stderr):
        if not self.counts:
            print("    nothing lost", file=out)
            return
        for kind, n in self.counts.items():
            ex = self.examples.get(kind, "")
            print(f"    {n:5d}  {kind}" + (f"   e.g. {ex}" if ex else ""), file=out)


def expand_variables(text, variables, lossy, depth=0):
    """Replace {{name}} recursively. Sublime variables may refer to each other."""
    if not isinstance(text, str) or "{{" not in text:
        return text
    if depth > 20:
        lossy.note("variable expansion too deep", text[:40])
        return text
    def sub(m):
        name = m.group(1)
        if name not in variables:
            lossy.note("undefined variable", name)
            return m.group(0)
        return expand_variables(variables[name], variables, lossy, depth + 1)
    return re.sub(r"\{\{([A-Za-z_][A-Za-z0-9_]*)\}\}", sub, text)


def scope_to_token(scope, lossy):
    """First scope in a space-separated list, redirected if it is structural."""
    if not isinstance(scope, str) or not scope.strip():
        return None
    first = scope.split()[0]
    root = first.split(".")[0]
    if root in HIGHLIGHT_ROOTS:
        return first
    # `cisco.scope`, `ios.ipv6.general-prefix` and the `text.*` family mark
    # regions for Sublime's own scope queries. They carry no colour intent.
    lossy.note("structural scope -> text", first)
    return "text"


def count_groups(pattern):
    """Capture groups in a regex, ignoring (?:...), (?=...), escapes and classes.

    Approximate on purpose: it decides only how long a tokenArray has to be, and
    the tokenizer falls back to a single token when the count disagrees with the
    array, so an error here degrades a colour rather than breaking a grammar.
    """
    n, i, in_class = 0, 0, False
    while i < len(pattern):
        c = pattern[i]
        if c == "\\":
            i += 2
            continue
        if in_class:
            if c == "]":
                in_class = False
            i += 1
            continue
        if c == "[":
            in_class = True
        elif c == "(":
            if pattern[i + 1 : i + 2] != "?":
                n += 1
            elif re.match(r"\(\?P?<[A-Za-z_]", pattern[i:]):
                n += 1  # named group, still a capture
        i += 1
    return n


class Converter:
    def __init__(self, doc, lossy):
        self.lossy = lossy
        self.variables = doc.get("variables", {}) or {}
        self.contexts = doc.get("contexts", {}) or {}
        self.out = OrderedDict()
        self.anon = 0
        # Inline contexts memoised BY IDENTITY of the YAML list object. Without
        # this, a context whose inline body includes its own parent gets a fresh
        # synthetic name on every visit and the conversion never terminates --
        # the include-cycle guard does not catch it, because each name really is
        # new. cisco-ios hits this on the first context.
        self.inline_names = {}
        # Sublime applies `prototype` to every context implicitly, which ace has
        # no equivalent for: the rules have to be prepended to each state by
        # hand. Missing this is silent -- comments and `exit` stop working
        # everywhere except the top level, which reads as a broken grammar.
        self.prototype = self.contexts.get("prototype")

    def var(self, text):
        return expand_variables(text, self.variables, self.lossy)

    def flatten(self, name, seen=None):
        """A context's items with every `include:` spliced in."""
        seen = seen or set()
        if name in seen:
            self.lossy.note("include cycle", name)
            return []
        seen = seen | {name}
        items = self.contexts.get(name)
        if items is None:
            self.lossy.note("include of a missing context", name)
            return []
        flat = []
        for item in items:
            if isinstance(item, dict) and "include" in item:
                target = item["include"]
                if isinstance(target, str) and target.startswith("scope:"):
                    self.lossy.note("cross-syntax include", target)
                    continue
                flat.extend(self.flatten(target, seen))
            else:
                flat.append(item)
        return flat

    def context_ref(self, value, origin):
        """A push/set target: a name, an inline context, or a list of either."""
        if isinstance(value, str):
            if value not in self.contexts:
                self.lossy.note("branch to a missing context", value)
                return None
            self.emit(value)
            return value
        if isinstance(value, list):
            # An inline anonymous context is a list of rule dicts; a list of
            # names is a multi-push, which ace's single `next` cannot express.
            if value and all(isinstance(v, str) for v in value):
                self.lossy.note("multi-context push, kept the innermost",
                                " -> ".join(value))
                return self.context_ref(value[-1], origin)
            key = id(value)
            if key in self.inline_names:
                return self.inline_names[key]
            self.anon += 1
            name = f"{origin}__inline{self.anon}"
            self.inline_names[key] = name
            self.contexts[name] = value
            self.emit(name)
            return name
        self.lossy.note("unrecognised push/set target", repr(value)[:40])
        return None

    def emit(self, name):
        if name in self.out:
            return
        self.out[name] = []  # claim the slot first: contexts can be cyclic
        items = self.flatten(name)
        if self.prototype is not None and name != "prototype":
            # meta_include_prototype: false opts a context out. Honour it, or
            # a context that deliberately swallows everything stops doing so.
            opts_out = any(
                isinstance(i, dict) and i.get("meta_include_prototype") is False
                for i in items
            )
            if not opts_out:
                items = self.flatten("prototype") + items

        rules = []
        for item in items:
            if not isinstance(item, dict):
                continue

            # meta_scope colours everything the context matches that no rule
            # claimed, which is exactly what ace's defaultToken does.
            for key in ("meta_scope", "meta_content_scope"):
                if key in item:
                    tok = scope_to_token(item[key], self.lossy)
                    if tok:
                        rules.append({"defaultToken": tok})

            if "match" not in item:
                continue

            rule = {"regex": self.var(item["match"])}

            captures = item.get("captures")
            if isinstance(captures, dict) and captures:
                n = count_groups(rule["regex"])
                if n > 0:
                    # Ace's tokenArray is POSITIONAL over every capture group;
                    # Sublime's captures map is sparse. Fill the gaps, or the
                    # tokenizer sees a length mismatch and collapses the whole
                    # match to one colour.
                    arr = []
                    for g in range(1, n + 1):
                        s = captures.get(g) or captures.get(str(g))
                        arr.append(scope_to_token(s, self.lossy) or "text")
                    rule["tokenArray"] = arr
                else:
                    self.lossy.note("captures with no capture group",
                                    rule["regex"][:40])

            if "scope" in item and "tokenArray" not in rule:
                tok = scope_to_token(item["scope"], self.lossy)
                if tok:
                    rule["token"] = tok

            if "tokenArray" not in rule and "token" not in rule:
                rule["token"] = "text"

            if item.get("pop"):
                rule["next"] = {"op": "pop"}
            elif "set" in item:
                target = self.context_ref(item["set"], name)
                if target:
                    rule["next"] = {"op": "goto", "state": target}
            elif "push" in item:
                target = self.context_ref(item["push"], name)
                if target:
                    rule["next"] = {"op": "push", "state": target}

            if "embed" in item or "escape" in item:
                self.lossy.note("embedded syntax", item.get("embed", "")[:40])

            rules.append(rule)

        self.out[name] = rules

    def convert(self):
        # `main` is Sublime's entry context; ace's is `start`.
        self.emit("main")
        for name in list(self.contexts):
            if name != "prototype":
                self.emit(name)
        states = OrderedDict()
        for name, rules in self.out.items():
            states["start" if name == "main" else name] = rules
        return states


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("syntax", help="a .sublime-syntax file")
    ap.add_argument("--name", help="mode name (default: from the file name)")
    ap.add_argument("--merge", metavar="GRAMMARS_JSON",
                    help="add the mode to an existing corpus, in place")
    ap.add_argument("-o", "--output", help="write the mode's states to a file")
    args = ap.parse_args()

    try:
        import yaml
    except ImportError:
        sys.exit("needs PyYAML:  pip install pyyaml")

    # .sublime-syntax is YAML 1.2 with a leading %YAML directive some parsers
    # reject; safe_load handles it, and the format uses no custom tags.
    doc = yaml.safe_load(open(args.syntax, encoding="utf-8"))

    name = args.name or re.sub(r"\.sublime-syntax$", "", args.syntax.split("/")[-1])
    name = name.replace("-", "_").replace(" ", "_").lower()

    lossy = Lossy()
    states = Converter(doc, lossy).convert()

    rules = sum(len(v) for v in states.values())
    print(f"==> {args.syntax}", file=sys.stderr)
    print(f"    mode {name}: {len(states)} states, {rules} rules", file=sys.stderr)
    print(f"    source: {doc.get('name', '?')}  "
          f"extensions: {','.join(doc.get('file_extensions', []) or []) or '-'}",
          file=sys.stderr)
    lossy.report()

    if args.merge:
        corpus = json.load(open(args.merge, encoding="utf-8"))
        existed = name in corpus
        corpus[name] = states
        with open(args.merge, "w", encoding="utf-8") as f:
            json.dump(corpus, f, separators=(",", ":"), ensure_ascii=False)
        print(f"    {'replaced' if existed else 'added'} {name} in {args.merge} "
              f"({len(corpus)} modes)", file=sys.stderr)
    elif args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            json.dump({name: states}, f, indent=1, ensure_ascii=False)
        print(f"    wrote {args.output}", file=sys.stderr)
    else:
        json.dump({name: states}, sys.stdout, indent=1, ensure_ascii=False)


if __name__ == "__main__":
    main()

# LOSSY, and counted rather than hidden:
#
#   multi-context push    `push: [a, b]` pushes two contexts; ace's `next`
#                         carries one. The innermost is kept, so the outer one
#                         never pops -- affects a handful of rules.
#   cross-syntax include  `include: scope:source.python` embeds another
#                         language. Dropped; ace has no equivalent and the
#                         corpus has no cross-mode references.
#   embed / escape        same reason.
#   structural scopes     `text.network.cisco.*` mark regions for Sublime's own
#                         scope queries rather than asking for a colour.
#
# NOT lossy, despite looking it: Oniguruma and PCRE2 disagree on a few escapes,
# and a rule PCRE2 rejects is skipped by the tokenizer while the rest of the
# grammar keeps working. aced_tests reports the count, and test_grammar.cpp
# fails the build if it grows.
