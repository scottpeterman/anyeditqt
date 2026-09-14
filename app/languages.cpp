// app/languages.cpp
#include "app/languages.h"

#include <QHash>
#include <algorithm>

#include "aced/grammar.h"

namespace anyedit {
namespace {

// Only where the generic rule gets it wrong. Everything absent from here is
// prettified, which is correct for "ada", "haskell", "lua" and most of the
// long tail.
const QHash<QString, QString> &overrides() {
    static const QHash<QString, QString> m = {
        {"c_cpp", "C/C++"},
        {"csharp", "C#"},
        {"fsharp", "F#"},
        {"objectivec", "Objective-C"},
        {"golang", "Go"},
        {"javascript", "JavaScript"},
        {"jsx", "JSX"},
        {"tsx", "TSX"},
        {"typescript", "TypeScript"},
        {"json", "JSON"},
        {"json5", "JSON5"},
        {"jsonata", "JSONata"},
        {"html", "HTML"},
        {"html_elixir", "HTML (Elixir)"},
        {"html_ruby", "HTML (Ruby)"},
        {"xml", "XML"},
        {"yaml", "YAML"},
        {"toml", "TOML"},
        {"css", "CSS"},
        {"scss", "SCSS"},
        {"less", "Less"},
        {"sass", "Sass"},
        {"sh", "Shell"},
        {"batchfile", "Batch File"},
        {"powershell", "PowerShell"},
        {"sql", "SQL"},
        {"sqlserver", "SQL Server"},
        {"mysql", "MySQL"},
        {"pgsql", "PostgreSQL"},
        {"php", "PHP"},
        {"php_laravel_blade", "PHP (Blade)"},
        {"perl", "Perl"},
        {"ruby", "Ruby"},
        {"python", "Python"},
        {"rust", "Rust"},
        {"swift", "Swift"},
        {"kotlin", "Kotlin"},
        {"scala", "Scala"},
        {"lua", "Lua"},
        {"r", "R"},
        {"matlab", "MATLAB"},
        {"markdown", "Markdown"},
        {"latex", "LaTeX"},
        {"tex", "TeX"},
        {"bibtex", "BibTeX"},
        {"diff", "Diff"},
        {"dockerfile", "Dockerfile"},
        {"makefile", "Makefile"},
        {"cmake", "CMake"},
        {"ini", "INI"},
        {"properties", "Properties"},
        {"apache_conf", "Apache Config"},
        {"nginx", "Nginx Config"},
        {"terraform", "Terraform"},
        {"graphqlschema", "GraphQL"},
        {"protobuf", "Protocol Buffers"},
        {"text", "Plain Text"},
        {"plsql", "PL/SQL"},
        {"partiql", "PartiQL"},
        {"prql", "PRQL"},
        {"liquid", "Liquid"},
        {"asciidoc", "AsciiDoc"},
        {"vbscript", "VBScript"},
        {"vhdl", "VHDL"},
        {"verilog", "Verilog"},
        {"systemverilog", "SystemVerilog"},
        {"assembly_x86", "Assembly (x86)"},
        {"ocaml", "OCaml"},
        {"coffee", "CoffeeScript"},
        {"clojure", "Clojure"},
        {"elixir", "Elixir"},
        {"erlang", "Erlang"},
        {"fortran", "Fortran"},
        {"pascal", "Pascal"},
        {"prolog", "Prolog"},
        {"scheme", "Scheme"},
        {"smarty", "Smarty"},
        {"twig", "Twig"},
        {"handlebars", "Handlebars"},
        {"velocity", "Velocity"},
        {"gitignore", "Gitignore"},
        {"csv", "CSV"},
        {"tsv", "TSV"},
    };
    return m;
}

}  // namespace

QString languageDisplayName(const std::string &mode) {
    if (mode.empty()) return "Plain Text";
    const QString id = QString::fromStdString(mode);
    const auto hit = overrides().constFind(id);
    if (hit != overrides().constEnd()) return hit.value();

    // "apache_conf" -> "Apache Conf". Not clever: the clever cases are in the
    // table above, and a rule that tried to be clever here would be wrong in
    // ways nobody could predict from the mode id alone.
    QString out = id;
    out.replace('_', ' ');
    QStringList words = out.split(' ', Qt::SkipEmptyParts);
    for (QString &w : words) {
        if (!w.isEmpty()) w[0] = w[0].toUpper();
    }
    return words.join(' ');
}

std::vector<std::pair<QString, QString>> languageList(const aced::Grammar &g) {
    std::vector<std::pair<QString, QString>> out;
    for (const std::string &mode : g.modes()) {
        // "text" is ace's do-nothing grammar and is the same thing as no
        // grammar at all. It is offered once, at the top of the menu, rather
        // than a second time under P where it means exactly the same and looks
        // like a different choice.
        if (mode == "text") continue;
        out.emplace_back(languageDisplayName(mode), QString::fromStdString(mode));
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        const int c = QString::compare(a.first, b.first, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.second < b.second;
    });
    return out;
}

}  // namespace anyedit
