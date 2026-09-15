#!/usr/bin/env bash
# scripts/bundle-mac.sh
#
# Packages anyeditqt as anyedit.app, deployed with macdeployqt. The directory
# is anyedit.app, from the target's OUTPUT_NAME; Finder shows it as "AnyEdit",
# from CFBundleDisplayName.
#
#   ./scripts/bundle-mac.sh
#   ./scripts/bundle-mac.sh --qt ~/Qt/6.10.3/macos
#   ./scripts/bundle-mac.sh --arch x86_64
#   ./scripts/bundle-mac.sh --install
#   ./scripts/bundle-mac.sh --sign "Developer ID Application: ..." --dmg
#   ./scripts/bundle-mac.sh --zip
#
# WHICH Qt, echoed before the build, for the same reason bundle-linux.sh and
# bundle-windows.bat do it: a Mac with Homebrew Qt and an installed Qt will let
# find_package reach whichever comes first, and macdeployqt must then come from
# that SAME Qt. Resolution is --qt, then $CMAKE_PREFIX_PATH, then the newest
# ~/Qt/6.*/macos, then whatever find_package finds.
#
# ONE ARCHITECTURE, and by default the one this Mac is. Hardcoding arm64 built
# cleanly on an Intel Mac and then failed at the smoke test with "bad CPU type",
# which reads as a packaging bug and is not one. --arch overrides. A universal
# binary would mean building PCRE2 twice and lipo'ing, which FetchContent has no
# seam for.
#
# NOT VERIFIED ON A MAC. Written on Linux against the documented behaviour of
# macdeployqt and codesign; every step prints what it is doing so a wrong one is
# visible rather than silent. Treat the first run as the test. The Linux script
# beside this one IS verified, and the two agree on layout, so a difference in
# behaviour between them is a bug here rather than a design choice.
#
# THE ORDER MATTERS AND IS NOT OBVIOUS:
#
#   1. cmake configure and build, with MACOSX_BUNDLE on the target.
#   2. grammars.json into Contents/Resources. macdeployqt does not copy
#      application resources, only Qt's.
#   3. macdeployqt. Copies the Qt frameworks and rewrites install names.
#   4. codesign, LAST. macdeployqt rewrites binaries, which invalidates any
#      signature already on them.
#
# Contents/MacOS/<binary> resolves "../Resources/grammars.json" to
# Contents/Resources/grammars.json, which is the second candidate in
# tools/render_probe.cpp findGrammars(). The layout is not a coincidence and
# should not be changed on one platform alone.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

BUILD_DIR="build-mac"
QTPREFIX="${CMAKE_PREFIX_PATH:-}"
SIGN_ID=""
MAKE_DMG=0
MAKE_ZIP=0
INSTALL=0
TARGET="anyedit"
ARCH="$(uname -m)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --qt) QTPREFIX="$2"; shift 2 ;;
        --qt=*) QTPREFIX="${1#*=}"; shift ;;
        --target) TARGET="$2"; shift 2 ;;
        --target=*) TARGET="${1#*=}"; shift ;;
        --arch) ARCH="$2"; shift 2 ;;
        --arch=*) ARCH="${1#*=}"; shift ;;
        --sign) SIGN_ID="$2"; shift 2 ;;
        --dmg) MAKE_DMG=1; shift ;;
        --zip) MAKE_ZIP=1; shift ;;
        --install) INSTALL=1; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        -h|--help) sed -n '3,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

die() { echo "error: $*" >&2; exit 1; }
say() { echo "==> $*"; }

[[ "$(uname -s)" == "Darwin" ]] || die "this one is macOS only; see bundle-linux.sh"

# sort -V, not sort: text order puts 6.9.1 above 6.10.3, and deploying one Qt's
# frameworks over a binary linked against another is the exact failure the
# verify section below exists to catch.
if [[ -z "${QTPREFIX}" ]]; then
    for base in "${HOME}/Qt" /opt/Qt; do
        [[ -d "${base}" ]] || continue
        for ver in $(ls -1 "${base}" 2>/dev/null | grep -E '^6\.[0-9]+' | sort -Vr); do
            if [[ -d "${base}/${ver}/macos/lib/cmake/Qt6" ]]; then
                QTPREFIX="${base}/${ver}/macos"; break 2
            fi
        done
    done
    [[ -n "${QTPREFIX}" ]] && echo "==> found Qt at ${QTPREFIX} (pass --qt to choose a different one)"
fi
if [[ -n "${QTPREFIX}" ]]; then
    [[ -d "${QTPREFIX}/lib/cmake/Qt6" ]] \
        || die "${QTPREFIX} has no lib/cmake/Qt6 in it. The prefix is the
     directory holding bin/, lib/ and include/ -- e.g. ~/Qt/6.10.3/macos."
fi

VERSION="$(sed -n 's/^project(anyeditqt VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[[ -n "${VERSION}" ]] || die "could not read the version out of CMakeLists.txt"
[[ -f grammars/grammars.json ]] \
    || die "grammars/grammars.json is missing; see grammars/README.md to regenerate it"

# --- 1. build --------------------------------------------------------------

# A CMAKE CACHE RECORDS THE ABSOLUTE PATH IT WAS CREATED AT and cannot be
# relocated: a build directory that travelled with the tree fails every cmake
# call with "is different than the directory ... where CMakeCache.txt was
# created", naming somebody else's home. Wipe it. See bundle-linux.sh.
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    CACHED_SRC="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt")"
    CACHED_BLD="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt")"
    HERE_SRC="$(cd "${ROOT}" && pwd -P)"
    HERE_BLD="$(cd "${BUILD_DIR}" && pwd -P)"
    if [[ -n "${CACHED_SRC}" && "${CACHED_SRC}" != "${ROOT}" && "${CACHED_SRC}" != "${HERE_SRC}" ]] \
    || [[ -n "${CACHED_BLD}" && "${CACHED_BLD}" != "${HERE_BLD}" ]]; then
        say "${BUILD_DIR} was configured under ${CACHED_SRC:-?}, not ${HERE_SRC}; wiping it"
        rm -rf "${BUILD_DIR}"
    fi
fi

say "building ${VERSION} into ${BUILD_DIR}"
# A cached Qt6_DIR beats -DCMAKE_PREFIX_PATH and says nothing about it, so a
# build directory configured against a different Qt is wiped rather than reused.
if [[ -n "${QTPREFIX}" && -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    CACHED="$(sed -n 's/^Qt6_DIR:PATH=//p' "${BUILD_DIR}/CMakeCache.txt")"
    if [[ -n "${CACHED}" && "${CACHED}" != "${QTPREFIX}"/* ]]; then
        say "${BUILD_DIR} was configured against ${CACHED}; wiping it"
        rm -rf "${BUILD_DIR}"
    fi
fi

# Every option passed EXPLICITLY -- see the note in bundle-linux.sh. option()
# honours an existing cache entry, so a default changed in CMakeLists.txt does
# not reach a build directory that already exists.
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release -DBUILD_WIDGET=ON -DBUILD_PROBES=ON
            -DACED_BUILD_TESTS=OFF
            "-DCMAKE_OSX_ARCHITECTURES=${ARCH}")
# NOT -DCMAKE_MACOSX_BUNDLE=ON. That variable applies to every executable
# created after it is set, so render_probe -- a command line tool that takes a
# source file and writes a PNG -- became build-mac/tools/render_probe.app, and
# the search below picked THAT up instead of the editor and died with "no
# executable inside build-mac/tools/render_probe.app". anyedit carries
# MACOSX_BUNDLE as a target property in app/CMakeLists.txt, which is where the
# decision belongs; render_probe stays a plain binary and stays runnable as
# ./build-mac/tools/render_probe.
[[ -n "${QTPREFIX}" ]] && CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${QTPREFIX}")
cmake -S . -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"

# CHECKED BEFORE THE BUILD, not after. CMake says nothing about a --target that
# does not exist until it has already spent a couple of minutes on PCRE2.
# Captured to a variable first, NOT piped into `grep -q`. Under `set -o
# pipefail` grep -q exits as soon as it matches, cmake gets SIGPIPE, the
# pipeline reports failure, and `if !` turns a successful match into "no such
# target" -- a false negative that only appears once the target exists.
TARGETS="$(cmake --build "${BUILD_DIR}" --target help 2>/dev/null || true)"
if ! grep -qx "\.\.\. ${TARGET}" <<<"${TARGETS}"; then
    echo >&2
    echo "no target named '${TARGET}' in ${BUILD_DIR}." >&2
    echo "  targets in this build directory:" >&2
    grep -E '^\.\.\. [a-z_]+$' <<<"${TARGETS}" | grep -v autogen \
        | sed 's/^\.\.\. /      /' >&2
    exit 1
fi

cmake --build "${BUILD_DIR}" --target "${TARGET}" -j"$(sysctl -n hw.ncpu)"

# BY NAME, not "the first .app under the build directory". The tree can contain
# more than one, and `find | head -1` returns whichever the filesystem happens to
# hand back first -- which is how packaging the editor ended up inside
# tools/render_probe.app.
APP="$(find "${BUILD_DIR}" -maxdepth 4 -type d -name "${TARGET}.app" | head -1)"
if [[ -z "${APP}" ]]; then
    # Executability tested in the shell, not with find -perm. `+111` is BSD
    # syntax that GNU find rejects outright, so the error path itself failed
    # with "find: invalid mode" the first time it was reached -- and an error
    # path that errors is worse than no error path.
    PLAIN="$(find "${BUILD_DIR}" -type f -name "${TARGET}" | head -1)"
    if [[ -n "${PLAIN}" && -x "${PLAIN}" ]]; then
        die "${TARGET} built as a plain executable, not a .app.
     Only anyedit is bundled: it carries MACOSX_BUNDLE as a target property in
     app/CMakeLists.txt. render_probe is a command line tool and is deliberately
     not bundled -- run it directly as ${BUILD_DIR}/tools/${TARGET}.
     To bundle something else, set MACOSX_BUNDLE on that target first."
    fi
    die "no ${TARGET}.app and no ${TARGET} binary after the build"
fi
say "bundle: ${APP}"
BIN="${APP}/Contents/MacOS/$(basename "${APP}" .app)"
[[ -x "${BIN}" ]] || die "no executable inside ${APP}"

# --- 2. resources ----------------------------------------------------------
#
# Before macdeployqt, and checked here rather than at the end, so that a missing
# corpus is a CMake/copy problem and a missing framework is a macdeployqt
# problem. At the end they look alike.

say "copying the grammar corpus"
mkdir -p "${APP}/Contents/Resources"
cp grammars/grammars.json "${APP}/Contents/Resources/grammars.json"

# LICENCE TEXTS ARE A DISTRIBUTION OBLIGATION, not documentation. GPLv3 s4 wants
# a copy of the licence conveyed with the work, and LGPLv3 s4d wants the Qt
# notices and both licence texts with any package that links it. A package
# missing these is not merely undocumented, it is non-compliant -- and nothing
# about it looks wrong.
cp LICENSE "${APP}/Contents/Resources/LICENSE"
cp THIRD_PARTY_NOTICES.md "${APP}/Contents/Resources/THIRD_PARTY_NOTICES.md"
mkdir -p "${APP}/Contents/Resources/licenses"
cp -R licenses/. "${APP}/Contents/Resources/licenses/"
for f in LICENSE THIRD_PARTY_NOTICES.md licenses/GPL-3.0.txt licenses/LGPL-3.0.txt; do
    [[ -s "${APP}/Contents/Resources/${f}" ]] || die "Resources/${f} missing from the bundle;
    it is required to be distributed with the binary, not optional."
done
CORPUS="$(stat -f%z "${APP}/Contents/Resources/grammars.json")"
[[ "${CORPUS}" -ge 1000000 ]] || die "grammars.json is ${CORPUS} bytes; that is not the corpus"
say "corpus: ${CORPUS} bytes"

# --- 3. macdeployqt --------------------------------------------------------
#
# macdeployqt is not reliably idempotent: it rewrites install names in place,
# and running it twice over the same bundle can leave paths pointing at
# themselves. The build directory is therefore disposable for packaging.

# From the Qt prefix first, PATH only as a fallback. macdeployqt must come from
# the same Qt the application linked against: a Homebrew macdeployqt over an
# installer build (or the reverse) copies frameworks that do not match the
# install names in the binary, and the result runs here and fails on a clean Mac.
MACDEPLOYQT=""
[[ -n "${QTPREFIX}" && -x "${QTPREFIX}/bin/macdeployqt" ]] && MACDEPLOYQT="${QTPREFIX}/bin/macdeployqt"
[[ -z "${MACDEPLOYQT}" ]] && MACDEPLOYQT="$(command -v macdeployqt || true)"
if [[ -z "${MACDEPLOYQT}" ]]; then
    QTBIN="$(qmake6 -query QT_INSTALL_BINS 2>/dev/null || qmake -query QT_INSTALL_BINS 2>/dev/null || true)"
    [[ -n "${QTBIN}" && -x "${QTBIN}/macdeployqt" ]] \
        || die "macdeployqt not found; put your Qt's bin on PATH"
    MACDEPLOYQT="${QTBIN}/macdeployqt"
fi
# Which Qt this is, said before it matters. macdeployqt must come from the SAME
# Qt the application linked against: a Homebrew macdeployqt over an aqtinstall
# build (or the reverse) copies frameworks that do not match the install names
# in the binary, and the result runs here and fails on a clean Mac.
say "deploying with ${MACDEPLOYQT}"
otool -L "${BIN}" | awk '/QtCore/{print "    linked against: " $1}'

"${MACDEPLOYQT}" "${APP}" -verbose=1

# --- 3b. rpaths ------------------------------------------------------------
#
# @rpath IS THE MODERN LAYOUT AND IS NOT A FAULT. A Qt 6 CMake build links
# against @rpath/QtCore.framework/... and carries an LC_RPATH that says where to
# look; macdeployqt copies the frameworks in and leaves the @rpath install names
# alone. An earlier version of this script demanded @executable_path in the
# install names and failed a bundle that was entirely correct.
#
# What actually matters is the LC_RPATH list, and there the hazard is real: the
# build adds an absolute rpath to the Qt that was linked against
# (~/Qt/6.10.3/macos/lib). Leave it in and the application resolves Qt through
# it on THIS machine -- so the bundle looks perfect here and fails on any Mac
# without that directory. The copied frameworks are never even opened.
#
# So: make sure the bundle's own rpath is there, and take out any absolute one
# that can still resolve Qt.

rpathsOf() { otool -l "$1" | awk '/LC_RPATH/{getline; getline; print $2}'; }

say "normalising rpaths"
TOUCHED=0
RPATHS="$(rpathsOf "${BIN}")"
if ! grep -qx -- '@executable_path/../Frameworks' <<<"${RPATHS}"; then
    install_name_tool -add_rpath '@executable_path/../Frameworks' "${BIN}"
    TOUCHED=1
    echo "    added   @executable_path/../Frameworks"
fi
while read -r rp; do
    [[ -z "${rp}" ]] && continue
    case "${rp}" in @*) continue ;; esac
    # Absolute, and it holds Qt frameworks: this is the one that would win.
    if compgen -G "${rp}/Qt*.framework" >/dev/null; then
        install_name_tool -delete_rpath "${rp}" "${BIN}" || true
        TOUCHED=1
        # Said only if it actually went. install_name_tool can decline quietly,
        # and a script that reports a removal it did not make is worse than one
        # that reports nothing.
        if grep -qxF -- "${rp}" <<<"$(rpathsOf "${BIN}")"; then
            echo "    COULD NOT REMOVE ${rp}"
        else
            echo "    removed ${rp}"
        fi
    fi
done <<<"${RPATHS}"

# RE-SIGNED IMMEDIATELY, and not left to step 5. install_name_tool invalidates
# whatever signature is on the binary -- macdeployqt ad-hoc signs on its way
# out -- and on Apple Silicon an invalidly signed binary is not a warning, it is
# "Killed: 9" the moment it launches. The smoke test below launches it. Without
# this the smoke test fails on arm64 and the message points nowhere near the
# cause.
if [[ "${TOUCHED}" -eq 1 ]]; then
    codesign --force --sign - "${APP}" >/dev/null 2>&1 \
        || say "ad-hoc re-sign after install_name_tool failed; the smoke test may be killed"
    echo "    re-signed ad-hoc after rewriting"
fi

# --- 4. verify -------------------------------------------------------------

say "verifying"

# An install name that is an ABSOLUTE path is a real fault -- nothing resolves
# it relative to the bundle. @rpath, @executable_path and @loader_path are all
# fine and all mean "inside".
STRAY="$(otool -L "${BIN}" | awk '/Qt[A-Za-z]*\.framework/{print $1}' | grep '^/' || true)"
if [[ -n "${STRAY}" ]]; then
    echo "${STRAY}" | sed 's/^/    /' >&2
    die "the Qt frameworks above are referenced by absolute path"
fi

LEFT="$(rpathsOf "${BIN}" | grep -v '^@' || true)"
while read -r rp; do
    [[ -z "${rp}" ]] && continue
    if compgen -G "${rp}/Qt*.framework" >/dev/null; then
        die "an absolute rpath that still resolves Qt survived: ${rp}
     The bundle would load that Qt on this Mac and fail on any other."
    fi
done <<<"${LEFT}"

# The frameworks have to actually BE in the bundle. Without this the two checks
# above pass on a bundle whose Frameworks directory is empty: nothing points
# outside, and nothing points at anything.
for fw in QtCore QtGui QtWidgets; do
    [[ -d "${APP}/Contents/Frameworks/${fw}.framework" ]] \
        || die "${fw}.framework is not in the bundle; macdeployqt did not copy it"
done

# The icon and the plist keys that Finder, the Dock and Spotlight read. None of
# these stop the application running, which is exactly why they need checking:
# the failure is a generic icon and no Spotlight hit, and nothing says why.
# PARSES FIRST. Every check below reads a key out of the plist, and on an
# unparseable one they all come back empty -- so a malformed plist reported
# itself as a missing icon, which is true but points at the wrong file. A double
# hyphen inside an XML comment in Info.plist.in is enough to cause it: CMake
# substitutes it happily, it ships, and Launch Services refuses it.
plutil -lint "${APP}/Contents/Info.plist" >/dev/null \
    || die "Contents/Info.plist is not valid XML; run plutil -lint on it.
     A double hyphen inside an XML comment in app/Info.plist.in will do this."

ICON="$(plutil -extract CFBundleIconFile raw "${APP}/Contents/Info.plist" 2>/dev/null || true)"
if [[ -z "${ICON}" ]]; then
    die "Info.plist has no CFBundleIconFile; the Dock will show a generic icon.
     app/CMakeLists.txt sets MACOSX_BUNDLE_ICON_FILE."
fi
if [[ ! -f "${APP}/Contents/Resources/${ICON}" && ! -f "${APP}/Contents/Resources/${ICON}.icns" ]]; then
    die "Info.plist names ${ICON} but it is not in Contents/Resources.
     Run scripts/make-icons.py, then reconfigure: CMake does not notice a new
     asset on its own."
fi
for key in CFBundleIdentifier CFBundleDisplayName NSPrincipalClass; do
    plutil -extract "${key}" raw "${APP}/Contents/Info.plist" >/dev/null 2>&1 \
        || die "Info.plist has no ${key}. Spotlight indexes on the identifier
     and the display name; without NSPrincipalClass the app is treated as
     pre-Cocoa and never sees the system appearance, so the \"follow the
     system\" theme stays light in dark mode."
done
say "icon ${ICON} present, and Info.plist parses with the keys Spotlight uses"

say "Qt resolves through the bundle's own rpath, and the frameworks are in it"

[[ -d "${APP}/Contents/PlugIns/platforms" ]] \
    || die "no platform plugin in the bundle; it would not start on a clean Mac"
[[ -f "${APP}/Contents/PlugIns/platforms/libqcocoa.dylib" ]] \
    || die "libqcocoa.dylib is not in the bundle; the directory is there but the
     plugin that actually draws a window is not, so it would open nothing on a
     clean Mac"

# macdeployqt does not always bring the offscreen plugin, and the smoke test
# below asks for it. Checked rather than assumed: forcing QT_QPA_PLATFORM to a
# plugin that is not there turns a working bundle into "could not load the Qt
# platform plugin" and the script would blame the packaging. With a window
# server present -- and this is a Mac, so there usually is one -- cocoa answers
# the same question just as well.
QPA=""
if [[ -f "${APP}/Contents/PlugIns/platforms/libqoffscreen.dylib" ]]; then
    QPA="offscreen"
    say "offscreen plugin present; the smoke test will use it"
else
    say "no offscreen plugin in the bundle; the smoke test will use the default"
fi

# The static PCRE2 must not appear as a dynamic dependency. Unlike Linux there
# is no symbol-export hazard here -- Mach-O executables do not re-export their
# static archives -- so this is the only half of that check worth making.
if otool -L "${BIN}" | grep -q 'libpcre2'; then
    die "the binary links libpcre2 dynamically; core/CMakeLists.txt pins a
     static PCRE2, so the build is not using the pinned one"
fi

say "architecture: $(lipo -archs "${BIN}")"

# The smoke test gets NO grammar argument on purpose: this is the only run that
# proves the bundle can find its own corpus through applicationDirPath().
#
# WHICH ARGUMENTS DEPEND ON THE TARGET, and getting this wrong is silent in the
# worst way: anyedit given render_probe's arguments (a source file and a PNG
# path) opens that file in a window and enters the event loop, so the script
# hangs forever instead of failing. anyedit has --check for exactly this --
# load the corpus, print, exit, no event loop.
say "smoke test: running the bundled binary without telling it where the corpus is"
# macOS has no timeout(1) unless coreutils is installed. Without it a hang is
# forever; with it a hang is a failure. Either way the script must not depend
# on it being there.
TIMEOUT=""
command -v timeout >/dev/null 2>&1 && TIMEOUT="timeout 30"
command -v gtimeout >/dev/null 2>&1 && TIMEOUT="gtimeout 30"

# EXPORTED OR UNSET, never set to the empty string. QT_QPA_PLATFORM="" is not
# "use the default": Qt takes it as a request for a plugin whose name is the
# empty string, fails to find one, and aborts with "no Qt platform plugin could
# be initialized" -- which reads exactly like a bundle that is missing its
# plugins, and is not.
if [[ -n "${QPA}" ]]; then
    export QT_QPA_PLATFORM="${QPA}"
else
    unset QT_QPA_PLATFORM
fi
# DYLD_PRINT_LIBRARIES turns the smoke test into the one check that actually
# settles the question. Every check above reads what the binary SAYS it wants;
# this reads what dyld actually opened. A bundle carrying a stale absolute rpath
# passes all of them and still loads the developer's Qt, and the only visible
# difference is on somebody else's Mac.
export DYLD_PRINT_LIBRARIES=1
if [[ "${TARGET}" == "anyedit" ]]; then
    CHECK="$(${TIMEOUT} "${BIN}" --check 2>&1 || true)"
    EXPECT="modes:"
else
    CHECK="$(${TIMEOUT} "${BIN}" \
                 "${ROOT}/core/src/document.cpp" /tmp/anyedit-bundle-check.png 2>&1 || true)"
    EXPECT="clamped=0"
fi
unset DYLD_PRINT_LIBRARIES

# Font-fallback chatter from the offscreen plugin is noise, and so are a few
# hundred dyld lines; both bury the lines that matter.
grep -vE "OpenType support missing|dyld\[|^dyld:" <<<"${CHECK}" | sed 's/^/    /'

if grep -q "no grammars found" <<<"${CHECK}"; then
    die "the bundle did not find its own grammars.json"
fi
# Qt words this two ways depending on version and on whether a plugin was
# named; matching only one of them lets the other fall through to the far vaguer
# "did not report 'modes:'".
if grep -qiE "could not load the Qt platform plugin|no Qt platform plugin could be initialized" \
        <<<"${CHECK}"; then
    die "the bundle could not load its platform plugin; the plugins in
     ${APP}/Contents/PlugIns/platforms are what macdeployqt left there"
fi
if grep -qi "bad CPU type" <<<"${CHECK}"; then
    die "the bundle was built for ${ARCH} and will not run on this Mac.
     Pass --arch $(uname -m)."
fi
grep -q "${EXPECT}" <<<"${CHECK}" \
    || die "the bundle started but did not report '${EXPECT}'; see above"

# Which Qt dyld actually opened. SIP strips DYLD_* from some processes, so an
# empty result is "could not check" and is said as such rather than treated as a
# pass -- a check that silently does nothing is worse than no check.
APP_ABS="$(cd "${APP}" && pwd)"
LOADED="$(grep -oE '/[^ ]*Qt[A-Za-z]+\.framework/[^ ]*' <<<"${CHECK}" | sort -u || true)"
if [[ -z "${LOADED}" ]]; then
    say "dyld reported nothing; the load path could not be confirmed"
else
    OUTSIDE="$(grep -v "^${APP_ABS}/" <<<"${LOADED}" || true)"
    if [[ -n "${OUTSIDE}" ]]; then
        echo "${OUTSIDE}" | sed 's/^/    /' >&2
        die "the bundle ran, but dyld loaded the Qt frameworks above from
     OUTSIDE it. It works on this Mac and will not start on a clean one."
    fi
    say "dyld loaded $(wc -l <<<"${LOADED}" | tr -d ' ') Qt frameworks, all from inside the bundle"
fi
say "the bundle starts, loads its own Qt, and finds its own corpus"

# --- 5. sign ---------------------------------------------------------------

if [[ -n "${SIGN_ID}" ]]; then
    say "signing with ${SIGN_ID}"
    codesign --force --options runtime --timestamp \
             --sign "${SIGN_ID}" --deep "${APP}"
    codesign --verify --deep --strict --verbose=2 "${APP}"
    echo
    echo "signed, but NOT notarized. For distribution outside your own machine:"
    echo "      xcrun notarytool submit <zip> --keychain-profile <profile> --wait"
    echo "      xcrun stapler staple ${APP}"
else
    say "ad-hoc signing (local use only)"
    codesign --force --deep --sign - "${APP}"
fi

# --- 6. dmg ----------------------------------------------------------------

if [[ "${MAKE_DMG}" -eq 1 ]]; then
    mkdir -p dist
    # anyedit-<version>-<os>-<arch>.<ext>, the same shape as the linux and
    # windows artifacts. Release asset names cannot be changed once anyone has
    # linked one, and three conventions across three platforms is the kind of
    # thing that looks deliberate to nobody.
    DMG="dist/anyedit-${VERSION}-macos-${ARCH}.dmg"
    say "writing ${DMG}"
    rm -f "${DMG}"
    hdiutil create -volname "AnyEdit ${VERSION}" -srcfolder "${APP}" \
                   -ov -format UDZO "${DMG}"
    ls -lh "${DMG}"
    # Bare filename inside, so `shasum -a 256 -c` works wherever it is
    # downloaded to rather than only where it was built.
    ( cd dist && shasum -a 256 "$(basename "${DMG}")" > "$(basename "${DMG}").sha256" )
    cat "${DMG}.sha256"
fi

# --- 6b. zip ---------------------------------------------------------------
#
# ditto, NOT zip -r, and this is not a preference.
#
# A Qt framework inside a .app is symlinks all the way down -- Versions/Current,
# and the top-level QtCore pointing into it. zip -r FOLLOWS symlinks, so the
# archive carries four copies of every framework binary and unpacks to a layout
# macOS will not load. And the bundle is signed by now; part of a signature
# lives in extended attributes, which zip -r drops, so the unzipped app fails
# verification with "damaged and can't be opened" -- a message that reads as a
# corrupt download rather than as a bad archiver.
#
# ditto -c -k preserves both, and it is what Apple's own notarization
# instructions use. --sequesterRsrc keeps resource forks in the AppleDouble
# form the format expects, and --keepParent puts anyedit.app INSIDE the zip
# rather than spilling Contents/ into whatever directory it is opened in.
if [[ "${MAKE_ZIP}" -eq 1 ]]; then
    mkdir -p dist
    ZIP="dist/anyedit-${VERSION}-macos-${ARCH}.zip"
    say "writing ${ZIP}"
    rm -f "${ZIP}"
    ditto -c -k --sequesterRsrc --keepParent "${APP}" "${ZIP}"
    ls -lh "${ZIP}"
    ( cd dist && shasum -a 256 "$(basename "${ZIP}")" > "$(basename "${ZIP}").sha256" )
    cat "${ZIP}.sha256"

    # A zip that unpacks to something macOS rejects is not obviously different
    # from one that does not, so the round trip is checked rather than assumed:
    # unpack to a scratch directory and verify the signature survived.
    TMPCHECK="$(mktemp -d)"
    trap 'rm -rf "${TMPCHECK}"' EXIT
    ditto -x -k "${ZIP}" "${TMPCHECK}"
    if [[ ! -d "${TMPCHECK}/$(basename "${APP}")" ]]; then
        die "the zip did not unpack to $(basename "${APP}"); --keepParent did not take"
    fi
    if ! codesign --verify --deep --strict "${TMPCHECK}/$(basename "${APP}")" 2>/dev/null; then
        die "the unpacked app fails signature verification.
    The archive lost extended attributes or symlinks, which is what zip -r does
    and ditto is here to avoid."
    fi
    say "the zip round-trips and the unpacked bundle still verifies"
fi

# --- 7. install ------------------------------------------------------------
#
# /Applications, not ~/Applications: both are indexed by Spotlight, but only
# /Applications is searched by every user and shown in the Finder sidebar.
# Copied with ditto rather than cp, because cp -R does not preserve the
# extended attributes and resource forks that a signed bundle relies on, and a
# bundle that loses them fails signature verification on first launch.

if [[ "${INSTALL}" -eq 1 ]]; then
    DEST="/Applications/$(basename "${APP}")"
    say "installing to ${DEST}"
    if [[ -e "${DEST}" ]]; then
        # Removed rather than copied over. ditto merges, so an old build's
        # frameworks survive alongside the new ones and the app loads a mixture.
        rm -rf "${DEST}" || die "could not replace ${DEST}; is it running?"
    fi
    ditto "${APP}" "${DEST}" || die "could not copy to ${DEST}"
    # Registers the bundle with Launch Services immediately. Without it the
    # icon, the Open With entries and the Spotlight hit appear whenever the
    # system next rescans, which is not now and looks like a failure.
    LSREG="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
    [[ -x "${LSREG}" ]] && "${LSREG}" -f "${DEST}" >/dev/null 2>&1 || true
    say "installed. Spotlight should find it as AnyEdit within a few seconds."
    echo
    echo "open it with:"
    echo "      open -a AnyEdit"
else
    echo
    echo "open it with:"
    echo "      open ${APP}"
    echo
    echo "install it into /Applications, so Spotlight and Launchpad see it:"
    echo "      $0 --install"
fi