// Regex backend for the ace tokenizer port.
//
// QRegularExpression IS PCRE2 with a QString-flavoured API, so this interface is
// deliberately the intersection of the two. To build against Qt, replace the body
// of Regex with QRegularExpression + QRegularExpressionMatch; the tokenizer above
// it does not change. The flag mapping is:
//
//   PCRE2_CASELESS       -> QRegularExpression::CaseInsensitiveOption
//   PCRE2_UTF|PCRE2_UCP  -> QRegularExpression::UseUnicodePropertiesOption
//   PCRE2_DOLLAR_ENDONLY -> (no Qt equivalent; see note below)
//
// DOLLAR_ENDONLY matters: JS `$` without the /m flag matches only at end of
// subject, PCRE2 `$` also matches before a trailing newline. We tokenize a line
// at a time with newlines already stripped, so in practice this only shows up if
// a grammar line contains an embedded \n. Set it anyway.

#pragma once
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <string>
#include <vector>
#include <cstring>

class Regex {
public:
    Regex() = default;

    Regex(const std::string& pattern, bool caseInsensitive, bool unicode)
        : pattern_(pattern)
    {
        uint32_t opts = PCRE2_DOLLAR_ENDONLY;
        if (caseInsensitive) opts |= PCRE2_CASELESS;
        // \x{...} above FF, or any non-ASCII literal, requires UTF mode.
        bool needsUtf = unicode || pattern_.find("\\x{") != std::string::npos;
        if (!needsUtf)
            for (unsigned char c : pattern_) if (c >= 0x80) { needsUtf = true; break; }
        if (needsUtf) opts |= PCRE2_UTF | PCRE2_UCP;

        int errcode = 0;
        PCRE2_SIZE erroffset = 0;
        code_ = pcre2_compile(
            reinterpret_cast<PCRE2_SPTR>(pattern_.c_str()), pattern_.size(),
            opts, &errcode, &erroffset, nullptr);

        if (!code_) {
            PCRE2_UCHAR buf[256];
            pcre2_get_error_message(errcode, buf, sizeof(buf));
            error_ = std::string(reinterpret_cast<char*>(buf))
                   + " at offset " + std::to_string(erroffset);
            return;
        }
        // JIT is optional; grammars are compiled once per mode so the cost is
        // trivial and the throughput win on large files is 3-5x.
        jitOk_ = (pcre2_jit_compile(code_, PCRE2_JIT_COMPLETE) == 0);
        pcre2_pattern_info(code_, PCRE2_INFO_CAPTURECOUNT, &captureCount_);
        md_ = pcre2_match_data_create_from_pattern(code_, nullptr);
    }

    ~Regex() {
        if (md_)   pcre2_match_data_free(md_);
        if (code_) pcre2_code_free(code_);
    }
    Regex(Regex&& o) noexcept { steal(std::move(o)); }
    Regex& operator=(Regex&& o) noexcept {
        if (this != &o) { this->~Regex(); steal(std::move(o)); } return *this;
    }
    Regex(const Regex&) = delete;
    Regex& operator=(const Regex&) = delete;

    bool valid() const { return code_ != nullptr; }
    const std::string& error() const { return error_; }
    uint32_t captureCount() const { return captureCount_; }
    const std::string& pattern() const { return pattern_; }

    // Non-owning view over PCRE2's ovector. Valid until the next search() on the
    // same Regex. Materialising these as a vector<pair> cost 8x throughput: the
    // fused per-state patterns have 100+ groups and almost all are unset.
    struct Match {
        bool matched = false;
        const PCRE2_SIZE* ov = nullptr;
        uint32_t n = 0;
        long start() const { return matched ? (long)ov[0] : -1; }
        long end()   const { return matched ? (long)ov[1] : -1; }
        bool has(size_t i) const { return matched && i < n && ov[2*i] != PCRE2_UNSET; }
        long gs(size_t i) const { return (long)ov[2*i]; }
        long ge(size_t i) const { return (long)ov[2*i+1]; }
    };

    // Search from `offset`, like JS regex.lastIndex + /g exec.
    Match search(const std::string& subject, size_t offset) const {
        Match m;
        if (!code_ || offset > subject.size()) return m;
        int rc = pcre2_match(code_,
            reinterpret_cast<PCRE2_SPTR>(subject.c_str()), subject.size(),
            offset, 0, md_, nullptr);
        if (rc < 0) return m;                 // PCRE2_ERROR_NOMATCH or an error
        if (rc == 0) rc = captureCount_ + 1;  // ovector too small; shouldn't happen
        m.ov = pcre2_get_ovector_pointer(md_);
        m.n  = (uint32_t)rc;
        m.matched = true;
        return m;
    }

private:
    void steal(Regex&& o) {
        code_ = o.code_; md_ = o.md_; captureCount_ = o.captureCount_; jitOk_ = o.jitOk_;
        pattern_ = std::move(o.pattern_); error_ = std::move(o.error_);
        o.code_ = nullptr; o.md_ = nullptr;
    }
    pcre2_code* code_ = nullptr;
    pcre2_match_data* md_ = nullptr;
    uint32_t captureCount_ = 0;
public:
    bool jitOk_ = false;
    bool jit() const { return jitOk_; }
private:
    std::string pattern_, error_;
};
