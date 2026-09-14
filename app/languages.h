// app/languages.h
//
// Turning grammar mode ids into something to put in a menu.
//
// The corpus keys are what ace calls its modes -- "c_cpp", "apache_conf",
// "objectivec" -- and none of them are what a language is called. A generic
// prettifier gets most of the way ("apache_conf" -> "Apache Conf") and is
// embarrassing for exactly the languages people use most, so the ones that
// matter are named explicitly and everything else falls through.
//
// This is presentation, which is why it is in app/ and not in core/. The corpus
// carries no display names and should not: it is ace's data, re-exported, and a
// table of English names does not belong inside it.
#pragma once

#include <QString>
#include <string>
#include <utility>
#include <vector>

namespace aced {
class Grammar;
}

namespace anyedit {

// "c_cpp" -> "C/C++". Unknown ids are prettified rather than rejected, so a
// corpus with a language this build has never heard of still gets a usable
// entry.
QString languageDisplayName(const std::string &mode);

// Every mode in the corpus as (display name, mode id), sorted by display name
// case-insensitively. Empty if the grammar failed to load.
std::vector<std::pair<QString, QString>> languageList(const aced::Grammar &g);

}  // namespace anyedit
