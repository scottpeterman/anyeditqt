// src/grammar.cpp
#include "aced/grammar.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

#include "aced/tokenizer.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace aced {

struct Grammar::Impl {
    json corpus;
    // mutable: tokenizer() is logically const (it observes the corpus) but
    // populates the cache on first use.
    mutable std::unordered_map<std::string, std::unique_ptr<Tokenizer>> cache;
};

Grammar::Grammar() : impl_(std::make_unique<Impl>()) {}
Grammar::~Grammar() = default;

bool Grammar::loadFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error_ = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return loadString(ss.str());
}

bool Grammar::loadString(const std::string &text) {
    json parsed = json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error_ = "grammar corpus is not a JSON object";
        return false;
    }
    impl_->corpus = std::move(parsed);
    impl_->cache.clear();
    error_.clear();
    return true;
}

bool Grammar::has(const std::string &mode) const {
    return impl_->corpus.is_object() && impl_->corpus.contains(mode);
}

std::vector<std::string> Grammar::modes() const {
    std::vector<std::string> out;
    if (!impl_->corpus.is_object()) return out;
    out.reserve(impl_->corpus.size());
    for (auto it = impl_->corpus.begin(); it != impl_->corpus.end(); ++it)
        out.push_back(it.key());
    std::sort(out.begin(), out.end());
    return out;
}

const Tokenizer *Grammar::tokenizer(const std::string &mode) const {
    auto hit = impl_->cache.find(mode);
    if (hit != impl_->cache.end()) return hit->second.get();
    if (!has(mode)) return nullptr;

    auto tk = std::make_unique<Tokenizer>(impl_->corpus[mode]);
    const Tokenizer *raw = tk.get();
    impl_->cache.emplace(mode, std::move(tk));
    return raw;
}

std::string Grammar::modeForFilename(const std::string &filename) {
    // Strip any directory part.
    size_t slash = filename.find_last_of("/\\");
    std::string base = slash == std::string::npos ? filename : filename.substr(slash + 1);

    std::string lower = base;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Extensionless files people actually open in an editor.
    static const std::map<std::string, std::string> kByName = {
        {"makefile", "makefile"},   {"gnumakefile", "makefile"},
        {"dockerfile", "dockerfile"}, {"cmakelists.txt", "makefile"},
        {"rakefile", "ruby"},       {"gemfile", "ruby"},
        {"vagrantfile", "ruby"},    {"jenkinsfile", "groovy"},
        {"pkgbuild", "sh"},         {".bashrc", "sh"},
        {".zshrc", "sh"},           {".profile", "sh"},
        {".gitconfig", "ini"},      {".gitignore", "gitignore"},
    };
    auto byName = kByName.find(lower);
    if (byName != kByName.end()) return byName->second;

    size_t dot = lower.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= lower.size()) return {};
    std::string ext = lower.substr(dot + 1);

    // Extensions whose ace mode name is not just the extension. Anything not
    // listed falls through to the extension itself, which is right far more
    // often than it is wrong (json, xml, css, lua, sql, ada, ...).
    static const std::map<std::string, std::string> kByExt = {
        {"c", "c_cpp"},        {"h", "c_cpp"},        {"cc", "c_cpp"},
        {"cpp", "c_cpp"},      {"cxx", "c_cpp"},      {"hpp", "c_cpp"},
        {"hh", "c_cpp"},       {"js", "javascript"},  {"mjs", "javascript"},
        {"cjs", "javascript"}, {"ts", "typescript"},  {"py", "python"},
        {"pyw", "python"},     {"rb", "ruby"},        {"rs", "rust"},
        {"go", "golang"},      {"kt", "kotlin"},      {"kts", "kotlin"},
        {"cs", "csharp"},      {"fs", "fsharp"},      {"pl", "perl"},
        {"pm", "perl"},        {"yml", "yaml"},       {"md", "markdown"},
        {"markdown", "markdown"}, {"htm", "html"},    {"sh", "sh"},
        {"bash", "sh"},        {"zsh", "sh"},         {"ps1", "powershell"},
        {"bat", "batchfile"},  {"cmd", "batchfile"},  {"tf", "terraform"},
        {"tfvars", "terraform"}, {"conf", "ini"},     {"cfg", "ini"},
        {"toml", "toml"},      {"vim", "vim"},        {"ex", "elixir"},
        {"exs", "elixir"},     {"erl", "erlang"},     {"hrl", "erlang"},
        {"clj", "clojure"},    {"cljs", "clojure"},   {"m", "objectivec"},
        {"mm", "objectivec"},  {"swift", "swift"},    {"jsonc", "json"},
        {"tsx", "tsx"},        {"jsx", "jsx"},        {"vue", "vue"},
        {"proto", "protobuf"}, {"gradle", "groovy"},  {"tex", "latex"},
    };
    auto byExt = kByExt.find(ext);
    if (byExt != kByExt.end()) return byExt->second;
    return ext;
}

}  // namespace aced
