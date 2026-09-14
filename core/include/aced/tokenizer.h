// src/tokenizer.h -- port of ace/src/tokenizer.js. Consumes grammars.json.
// mode/*_highlight_rules.js) as data. No JS.
//
// Structure mirrors the original exactly so the two can be diffed:
//   - per state, rules are fused into one alternation "(r1)|(r2)|...|($)"
//   - a match-offset -> rule-index map recovers which rule fired
//   - a state stack supports the push/pop ops that normalizeRules() lowered
//
// Public entry point is the same as ace's: tokenize(line, stateIn) -> tokens + stateOut.
// That signature is what QSyntaxHighlighter::highlightBlock needs, with stateIn
// from previousBlockState() and stateOut into setCurrentBlockState().

#pragma once
#include "aced/regex.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>

namespace aced {

static const int MAX_TOKEN_COUNT = 2000;

struct Token {
    std::string type;
    std::string value;
};

enum class NextOp { None, Goto, Push, Pop };

struct Rule {
    std::string regex;                  // original, pre-adjustment
    std::string token;
    std::vector<std::string> tokenArray;
    NextOp op = NextOp::None;
    std::string nextState;
    bool merge = true;
    bool consumeLineEnd = false;
    bool hasTokenFn = false;            // grammar needed real JS; we emit defaultToken
    // createKeywordMapper() lowered to a lookup table
    std::unordered_map<std::string, std::string> keywords;
    std::string keywordDefault;
    bool keywordIgnoreCase = false;
    std::unique_ptr<Regex> splitRegex;  // for tokenArray rules
};

struct State {
    std::vector<Rule> rules;
    std::string defaultToken = "text";
    Regex combined;
    // parallel arrays: matchOffset[k] -> ruleIndex[k]
    std::vector<uint32_t> matchOffset;
    std::vector<uint32_t> ruleIndex;
    bool ok = false;
    std::string error;
};

// ---- regex source rewriting, ported 1:1 from tokenizer.js -------------------

// tokenizer.js removeCapturingGroups(): turn "(" into "(?:" outside char classes
// and escapes, leaving (?: (?= (?! (?< alone.
inline std::string removeCapturingGroups(const std::string& src) {
    std::string out;
    out.reserve(src.size() + 16);
    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        if (c == '\\' && i + 1 < src.size()) { out += c; out += src[++i]; continue; }
        if (c == '[') {                                  // char class: copy verbatim
            out += c;
            for (++i; i < src.size(); ++i) {
                if (src[i] == '\\' && i + 1 < src.size()) { out += src[i]; out += src[++i]; continue; }
                out += src[i];
                if (src[i] == ']') break;
            }
            continue;
        }
        if (c == '(') {
            if (i + 1 < src.size() && src[i + 1] == '?') out += c;  // already non-capturing
            else out += "(?:";
            continue;
        }
        out += c;
    }
    return out;
}

// Offset backreferences \1 \2 ... by `base`, so a rule's groups still resolve
// once it is fused into the big alternation.
inline std::string offsetBackrefs(const std::string& src, uint32_t base) {
    std::string out;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '\\' && i + 1 < src.size() && isdigit((unsigned char)src[i + 1])) {
            size_t j = i + 1;
            std::string d;
            while (j < src.size() && isdigit((unsigned char)src[j])) d += src[j++];
            out += "\\" + std::to_string(std::stoi(d) + (int)base);
            i = j - 1;
            continue;
        }
        if (src[i] == '\\' && i + 1 < src.size()) { out += src[i]; out += src[i + 1]; ++i; continue; }
        out += src[i];
    }
    return out;
}

inline bool hasBackref(const std::string& s) {
    for (size_t i = 0; i + 1 < s.size(); ++i)
        if (s[i] == '\\' && isdigit((unsigned char)s[i + 1])) return true;
    return false;
}

// tokenizer.js createSplitterRegexp(): drop a trailing lookahead group, anchor ^...$
inline std::string makeSplitterSource(const std::string& in) {
    std::string src = in;
    if (src.find("(?=") != std::string::npos) {
        int stack = 0;
        bool inCls = false;
        long lastStart = -1, lastEnd = -1, lastStack = -1;
        for (size_t i = 0; i < src.size(); ++i) {
            char c = src[i];
            if (c == '\\') { ++i; continue; }
            if (inCls) { if (c == ']') inCls = false; continue; }
            if (c == '[') { inCls = true; continue; }
            if (c == ')') {
                if (stack == lastStack) { lastEnd = (long)i + 1; lastStack = -1; }
                stack--;
            } else if (c == '(') {
                stack++;
                bool grouped = (i + 1 < src.size() && src[i + 1] == '?'
                                && i + 2 < src.size() && (src[i + 2] == '=' || src[i + 2] == '!'));
                if (grouped) { lastStack = stack; lastStart = (long)i; }
            }
        }
        if (lastEnd >= 0) {
            bool onlyParens = true;
            for (size_t k = (size_t)lastEnd; k < src.size(); ++k)
                if (src[k] != ')') { onlyParens = false; break; }
            if (onlyParens) src = src.substr(0, (size_t)lastStart) + src.substr((size_t)lastEnd);
        }
    }
    if (src.empty() || src.front() != '^') src = "^" + src;
    if (src.empty() || src.back()  != '$') src += "$";
    return src;
}

// ---- tokenizer -------------------------------------------------------------

class Tokenizer {
public:
    // `grammar` is one mode's state map: state name -> list of rule descriptors.
    // Each descriptor: {regex, token|tokenArray|tokenFn, defaultToken, next:{op,state},
    //                   caseInsensitive, unicode, consumeLineEnd, merge}
    template <class Json>
    explicit Tokenizer(const Json& grammar) {
        for (auto it = grammar.begin(); it != grammar.end(); ++it)
            buildState(it.key(), it.value());
    }

    struct Result {
        std::vector<Token> tokens;
        std::vector<std::string> stack;  // empty => plain state in `state`
        std::string state = "start";
    };

    std::vector<std::string> badStates() const {
        std::vector<std::string> v;
        for (auto& [k, s] : states_) if (!s->ok) v.push_back(k + ": " + s->error);
        return v;
    }
    size_t stateCount() const { return states_.size(); }

    Result tokenize(const std::string& line, const std::string& startState,
                    const std::vector<std::string>& startStack = {}) const
    {
        Result out;
        std::vector<std::string> stack = startStack;
        std::string currentState = startState;

        if (!stack.empty()) {
            currentState = stack.front();
            if (currentState == "#tmp") {
                stack.erase(stack.begin());
                currentState = stack.front();
                stack.erase(stack.begin());
            }
        }
        if (currentState.empty()) currentState = "start";

        const State* st = find(currentState);
        if (!st) { currentState = "start"; st = find(currentState); }
        if (!st) { out.state = "start"; return out; }

        size_t lastIndex = 0, pos = 0;
        int attempts = 0;
        Token cur; bool curSet = false;

        auto flush = [&]() { if (curSet && !cur.type.empty()) out.tokens.push_back(cur); };

        while (true) {
            Regex::Match m = st->combined.search(line, pos);
            if (!m.matched) break;

            // ace's matchMappings start at "text" and are only overridden by an
            // explicit defaultToken (tokenizer.js: {defaultToken: "text"}).
            // Ours left it empty, and the emit below drops an empty-typed
            // token -- along with the bytes it covered. That is not a missing
            // colour, it is a hole in the stream: every consumer computes byte
            // offsets by laying token values end to end, so a dropped token
            // shifts every format run after it to the left. The javascript
            // grammar's no_regex state opens with a bare "[{}]" rule, which is
            // how every brace in every JS file went missing.
            std::string type = st->defaultToken.empty() ? std::string("text")
                                                        : st->defaultToken;
            const Rule* rule = nullptr;
            const State* stBefore = st;
            size_t vs = (size_t)m.start(), ve = (size_t)m.end();
            std::string value = line.substr(vs, ve - vs);
            size_t index = ve;

            // gap between last emission and this match -> defaultToken run
            if (vs > lastIndex) {
                std::string skipped = line.substr(lastIndex, vs - lastIndex);
                if (curSet && cur.type == type) cur.value += skipped;
                else { flush(); cur = {type, skipped}; curSet = true; }
            }

            // find which rule's group fired
            for (size_t k = 0; k < st->matchOffset.size(); ++k) {
                uint32_t g = st->matchOffset[k] + 1;
                if (!m.has(g)) continue;
                rule = &st->rules[st->ruleIndex[k]];

                std::vector<Token> arrayTokens;
                bool isArray = false;

                if (!rule->tokenArray.empty() && rule->splitRegex) {
                    isArray = true;
                    if (!value.empty()) {
                        Regex::Match sm = rule->splitRegex->search(value, 0);
                        if (!sm.matched) { type = "text"; isArray = false; }
                        else {
                            for (size_t t = 0; t < rule->tokenArray.size(); ++t) {
                                if (!sm.has(t + 1)) continue;
                                long a = sm.gs(t + 1), b = sm.ge(t + 1);
                                if (b <= a) continue;
                                arrayTokens.push_back({rule->tokenArray[t],
                                                       value.substr((size_t)a, (size_t)(b - a))});
                            }
                        }
                    }
                } else if (!rule->keywords.empty()) {
                    std::string key = value;
                    if (rule->keywordIgnoreCase)
                        std::transform(key.begin(), key.end(), key.begin(),
                                       [](unsigned char c){ return std::tolower(c); });
                    auto kit = rule->keywords.find(key);
                    type = (kit != rule->keywords.end()) ? kit->second : rule->keywordDefault;
                } else if (rule->hasTokenFn) {
                    type = st->defaultToken;          // grammar wanted JS; degrade, don't guess
                } else {
                    type = rule->token;
                }

                if (rule->op != NextOp::None) {
                    std::string nx;
                    switch (rule->op) {
                        case NextOp::Goto: nx = rule->nextState; break;
                        case NextOp::Push:
                            if (currentState != "start" || !stack.empty()) {
                                stack.insert(stack.begin(), currentState);
                                stack.insert(stack.begin(), rule->nextState);
                            }
                            nx = rule->nextState;
                            break;
                        case NextOp::Pop:
                            if (!stack.empty()) { nx = stack.front(); stack.erase(stack.begin()); }
                            if (nx.empty()) nx = "start";
                            break;
                        default: break;
                    }
                    const State* ns = find(nx);
                    if (!ns) { nx = "start"; ns = find(nx); }
                    if (ns) { currentState = nx; st = ns; }
                    lastIndex = index;
                }
                if (rule->consumeLineEnd) lastIndex = index;

                if (isArray) {
                    flush();
                    cur = {}; curSet = false;
                    for (auto& t : arrayTokens) out.tokens.push_back(t);
                    value.clear();      // consumed
                }
                break;
            }

            if (!value.empty()) {
                if (type.empty()) type = "text";  // see above: never drop bytes
                {
                    if ((!rule || rule->merge) && curSet && cur.type == type) cur.value += value;
                    else { flush(); cur = {type, value}; curSet = true; }
                }
            }

            if (lastIndex == line.size()) break;
            lastIndex = index;
            // Zero-width matches are legitimate and load-bearing: several
            // grammars (javascript among them) begin with an empty-regex rule
            // whose only job is to pick a real start state. Re-scanning from
            // the same offset is correct there because the state changed, so
            // the next pass runs a different pattern. Only a zero-width match
            // that ALSO left the state alone can spin, and that one has to be
            // stepped past.
            pos = (index == vs && st == stBefore) ? index + 1 : index;
            if (pos > line.size()) break;

            if (++attempts > MAX_TOKEN_COUNT) {
                while (lastIndex < line.size()) {
                    flush();
                    size_t n = std::min<size_t>(500, line.size() - lastIndex);
                    cur = {"overflow", line.substr(lastIndex, n)};
                    curSet = true;
                    lastIndex += n;
                }
                currentState = "start"; stack.clear();
                break;
            }
        }

        flush();

        if (stack.size() > 1 && stack.front() != currentState) {
            stack.insert(stack.begin(), currentState);
            stack.insert(stack.begin(), "#tmp");
        }
        out.stack = stack;
        out.state = stack.empty() ? currentState : stack.front();
        return out;
    }

private:
    const State* find(const std::string& n) const {
        auto it = states_.find(n);
        return it == states_.end() ? nullptr : it->second.get();
    }

    template <class JsonVal>
    void buildState(const std::string& name, const JsonVal& arr) {
        auto st = std::make_unique<State>();
        bool ci = false, uni = false;

        for (auto& rj : arr) {
            if (rj.contains("caseInsensitive")) ci = true;
            if (rj.contains("unicode")) uni = true;
        }

        std::vector<std::string> parts;
        uint32_t matchTotal = 0;

        for (auto& rj : arr) {
            if (rj.contains("defaultToken") && rj["defaultToken"].is_string())
                st->defaultToken = rj["defaultToken"].template get<std::string>();
            if (!rj.contains("regex") || !rj["regex"].is_string()) continue;

            Rule r;
            r.regex = rj["regex"].template get<std::string>();
            if (rj.contains("token") && rj["token"].is_string())
                r.token = rj["token"].template get<std::string>();
            if (rj.contains("tokenArray") && rj["tokenArray"].is_array()) {
                bool allStr = true;
                for (auto& t : rj["tokenArray"]) if (!t.is_string()) allStr = false;
                if (allStr) r.tokenArray = rj["tokenArray"].template get<std::vector<std::string>>();
                else r.hasTokenFn = true;
            }
            if (rj.contains("tokenFn")) r.hasTokenFn = true;
            if (rj.contains("keywords") && rj["keywords"].is_object()) {
                for (auto it = rj["keywords"].begin(); it != rj["keywords"].end(); ++it)
                    if (it.value().is_string())
                        r.keywords[it.key()] = it.value().template get<std::string>();
                if (rj.contains("keywordDefault") && rj["keywordDefault"].is_string())
                    r.keywordDefault = rj["keywordDefault"].template get<std::string>();
                if (rj.contains("keywordIgnoreCase")) r.keywordIgnoreCase = true;
            }
            if (rj.contains("merge")) r.merge = rj["merge"].template get<bool>();
            if (rj.contains("consumeLineEnd")) r.consumeLineEnd = true;
            if (rj.contains("next") && rj["next"].is_object()) {
                const auto& nx = rj["next"];
                std::string op = nx.contains("op") && nx["op"].is_string()
                               ? nx["op"].template get<std::string>() : "";
                std::string tgt = nx.contains("state") && nx["state"].is_string()
                                ? nx["state"].template get<std::string>() : "";
                if (op == "goto" && !tgt.empty()) { r.op = NextOp::Goto; r.nextState = tgt; }
                else if (op == "push" && !tgt.empty()) { r.op = NextOp::Push; r.nextState = tgt; }
                else if (op == "pop") { r.op = NextOp::Pop; }
            }

            // capture-group count drives everything below
            Regex probe(r.regex, ci, uni);
            if (!probe.valid()) {            // regex PCRE2 rejects: skip the rule, keep the mode
                st->error += (st->error.empty() ? "" : "; ") + probe.error();
                continue;
            }
            uint32_t groups = probe.captureCount();
            uint32_t matchcount = groups + 1;

            if (!r.tokenArray.empty()) {
                if (r.tokenArray.size() == 1 || matchcount == 1) {
                    r.token = r.tokenArray[0];
                    r.tokenArray.clear();
                } else if (matchcount - 1 != r.tokenArray.size()) {
                    r.token = r.tokenArray[0];
                    r.tokenArray.clear();
                }
            }

            std::string adjusted = r.regex;
            if (matchcount > 1) {
                if (hasBackref(r.regex)) {
                    adjusted = offsetBackrefs(r.regex, matchTotal + 1);
                } else {
                    matchcount = 1;
                    adjusted = removeCapturingGroups(r.regex);
                }
                if (!r.tokenArray.empty())
                    r.splitRegex = std::make_unique<Regex>(makeSplitterSource(r.regex), ci, uni);
            }

            st->matchOffset.push_back(matchTotal);
            st->ruleIndex.push_back((uint32_t)st->rules.size());
            matchTotal += matchcount;
            parts.push_back(adjusted);
            st->rules.push_back(std::move(r));
        }

        if (parts.empty()) {
            st->matchOffset = {0}; st->ruleIndex = {0};
            st->rules.emplace_back();
            parts.push_back("$");
        }

        std::string fused = "(";
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) fused += ")|(";
            fused += parts[i];
        }
        fused += ")|($)";

        st->combined = Regex(fused, ci, uni);
        st->ok = st->combined.valid();
        if (!st->ok) st->error = st->combined.error();
        states_[name] = std::move(st);
    }

    std::unordered_map<std::string, std::unique_ptr<State>> states_;
};

} // namespace aced
