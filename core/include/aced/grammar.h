// include/aced/grammar.h
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace aced {

class Tokenizer;

// Owns the exported ace grammar corpus and hands out Tokenizers for it.
//
// The corpus is one JSON file -- 198 languages, ~34k rules -- produced by
// grammars/export_modes.js from an ace checkout. It is data at runtime, not
// generated code, so adding or fixing a language is a re-export rather than a
// rebuild, and a user can drop in their own grammar without touching the
// binary.
//
// The JSON dependency stops here. nlohmann appears only in grammar.cpp, so
// nothing downstream -- the widget, the bindings, a consumer of this library --
// inherits it or has to agree on its version.
//
// Tokenizers are built on demand and cached, because building one compiles
// every regex in the grammar (5ms for javascript, once).
class Grammar {
public:
    Grammar();
    ~Grammar();

    Grammar(const Grammar &) = delete;
    Grammar &operator=(const Grammar &) = delete;

    // Both return false and set error() rather than throwing: a malformed or
    // missing grammar file is a normal runtime condition for a shipped editor,
    // not an exceptional one.
    bool loadFile(const std::string &path);
    bool loadString(const std::string &json);

    const std::string &error() const { return error_; }

    bool has(const std::string &mode) const;
    std::vector<std::string> modes() const;

    // Null if the mode is unknown. The Grammar owns the returned Tokenizer and
    // keeps it alive until the Grammar is destroyed.
    const Tokenizer *tokenizer(const std::string &mode) const;

    // Guess a mode from a filename. Extension first, then a handful of names
    // with no extension. Empty string if nothing matches -- the caller decides
    // whether that means plain text or a prompt.
    static std::string modeForFilename(const std::string &filename);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_;
};

}  // namespace aced
