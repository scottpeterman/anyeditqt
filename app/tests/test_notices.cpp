// app/tests/test_notices.cpp
//
// Licensing facts that are easy to get wrong and impossible to notice.
//
// THIRD_PARTY_NOTICES.md names versions for two dependencies that are pinned
// somewhere else entirely, in core/CMakeLists.txt. Bump a pin and the notices
// quietly describe a version that is not in the binary -- which is the one
// failure mode of a hand-maintained notices file. And the About dialog carries
// the version every bug report starts with, through a define that reaches it
// only if it is set on the right target.
#include <QFile>
#include <QRegularExpression>
#include <QString>

#include "app/aboutdialog.h"
#include "harness.h"

using anyedit::AboutDialog;

namespace {

QString slurp(const char *path) {
    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

// GIT_TAG <tag> for a FetchContent_Declare named `dep`, out of the CMake file
// that actually pins it.
QString pinnedTag(const QString &cmake, const QString &dep) {
    const int at = cmake.indexOf("FetchContent_Declare(" + dep);
    if (at < 0) return QString();
    const QRegularExpression re("GIT_TAG\\s+(\\S+)");
    const auto m = re.match(cmake, at);
    return m.hasMatch() ? m.captured(1) : QString();
}

}  // namespace

TEST(the_notices_name_the_versions_that_are_actually_pinned) {
    const QString cmake = slurp(ANYEDIT_CORE_CMAKE);
    const QString notices = slurp(ANYEDIT_NOTICES);
    CHECK(!cmake.isEmpty());
    CHECK(!notices.isEmpty());

    // pcre2-10.44 -> the notices must say 10.44; v3.11.3 -> 3.11.3.
    const QString pcre2 = pinnedTag(cmake, "pcre2");
    const QString json = pinnedTag(cmake, "nlohmann_json");
    CHECK(!pcre2.isEmpty());
    CHECK(!json.isEmpty());

    QString pcreVer = pcre2;
    pcreVer.remove("pcre2-");
    QString jsonVer = json;
    if (jsonVer.startsWith('v')) jsonVer.remove(0, 1);

    if (!notices.contains("Version " + pcreVer)) {
        std::fprintf(stderr, "    THIRD_PARTY_NOTICES.md does not name PCRE2 %s\n",
                     qPrintable(pcreVer));
        CHECK(false);
    }
    if (!notices.contains("Version " + jsonVer)) {
        std::fprintf(stderr, "    THIRD_PARTY_NOTICES.md does not name nlohmann/json %s\n",
                     qPrintable(jsonVer));
        CHECK(false);
    }
    // And the pin itself must be quoted, so the notices point at where it lives.
    CHECK(notices.contains(pcre2));
    CHECK(notices.contains(json));
}

TEST(the_notices_carry_every_component_the_binary_contains) {
    const QString n = slurp(ANYEDIT_NOTICES);
    for (const char *needle : {"Qt 6", "PCRE2", "nlohmann/json", "ace", "Scintilla"}) {
        if (n.contains(QString::fromUtf8(needle))) continue;
        std::fprintf(stderr, "    THIRD_PARTY_NOTICES.md is missing: %s\n", needle);
        CHECK(false);
    }
    // The two texts LGPLv3 requires be conveyed with a package that links Qt.
    CHECK(n.contains("licenses/LGPL-3.0.txt"));
    CHECK(n.contains("licenses/GPL-3.0.txt"));
}

TEST(the_about_dialog_reports_the_real_version) {
    // ANYEDIT_VERSION was on the anyedit EXECUTABLE, and AboutDialog lives in
    // anyedit_app. A define scoped to the executable never reaches the library:
    // the dialog compiles, falls back to its "0.0.0-dev" default, and reports a
    // version nobody set. This test links the library without the executable,
    // which is the only place that shows up.
    const QString v = AboutDialog::version();
    CHECK(v != QStringLiteral("0.0.0-dev"));
    CHECK(QRegularExpression("^\\d+\\.\\d+\\.\\d+").match(v).hasMatch());
}

TEST(the_about_text_carries_the_notices_the_licences_require) {
    const QString s = AboutDialog::summary();
    // GPLv3 s5c wants the warranty disclaimer where a user of the binary sees
    // it; the LGPL wants the use of Qt acknowledged. Both in the About box,
    // because a README reaches neither person.
    CHECK(s.contains("WITHOUT ANY WARRANTY"));
    CHECK(s.contains("GNU General Public License"));
    CHECK(s.contains("Qt"));
    CHECK(s.contains("Lesser General Public License"));
    CHECK(s.contains("ace"));
    CHECK(s.contains("Scintilla"));
    CHECK(s.contains(AboutDialog::version()));
    CHECK(s.contains("github.com/scottpeterman/anyeditqt"));
}

TEST(the_shipped_metadata_does_not_claim_a_licence_we_do_not_use) {
    // MACOSX_BUNDLE_COPYRIGHT becomes NSHumanReadableCopyright in Info.plist,
    // which Finder's Get Info panel shows -- a licence claim inside the
    // artifact, where nobody reviewing the repo would look for one. It said
    // BSD-3-Clause for as long as the licence was undecided, describing the
    // GRAMMARS' provenance rather than the application's terms.
    const QString cmake = slurp(ANYEDIT_APP_CMAKE);
    CHECK(!cmake.isEmpty());
    const int at = cmake.indexOf("MACOSX_BUNDLE_COPYRIGHT");
    CHECK(at > 0);
    const QString line = cmake.mid(at, cmake.indexOf('\n', at) - at);
    if (!line.contains("GPLv3")) {
        std::fprintf(stderr, "    bundle copyright does not name GPLv3: %s\n",
                     qPrintable(line));
        CHECK(false);
    }
    CHECK(!line.contains("BSD"));
}
