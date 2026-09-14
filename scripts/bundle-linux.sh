#!/usr/bin/env bash
# scripts/bundle-linux.sh
#
# Packages anyeditqt as a relocatable directory, then a tarball.
#
#   ./scripts/bundle-linux.sh                       # -> dist/anyedit-<ver>-linux-x86_64.tar.gz
#   ./scripts/bundle-linux.sh --qt ~/Qt/6.10.3/gcc_64
#   ./scripts/bundle-linux.sh --target render_probe # bundle a different target
#   ./scripts/bundle-linux.sh --no-tar              # leave the tree, skip the archive
#
# WHICH Qt, and it is echoed before the build. A machine with both a
# distribution Qt and an installed one -- ~/Qt/6.10.3/gcc_64 from the official
# installer, /opt/Qt, aqtinstall -- will let find_package reach whichever comes
# first, and this script then deploys the libraries THAT Qt resolves to. Linking
# one and deploying another works perfectly here and fails on a clean machine,
# which is the same failure bundle-windows.bat guards against. Resolution order:
#
#   --qt <prefix>
#   $CMAKE_PREFIX_PATH
#   $Qt6_DIR          (walked back up from lib/cmake/Qt6)
#   the newest ~/Qt/6.*/gcc_64, then /opt/Qt/6.*/gcc_64
#   whatever find_package finds on its own (usually the distribution's)
#
# NOT AN APPIMAGE, and that is a choice rather than a shortcut. An AppImage
# needs linuxdeploy and its Qt plugin downloaded at build time, which makes
# packaging depend on two more moving parts and on network access. What comes
# out of this is a directory somebody untars anywhere and runs. If an AppImage
# is wanted later, this tree is what linuxdeploy would be pointed at anyway.
#
# The layout is the one tools/render_probe.cpp findGrammars() already looks for:
#
#   anyedit/
#     bin/anyedit             the binary, plus a launcher beside it
#     lib/                    Qt and ICU
#     plugins/                Qt's platform plugin and friends
#     Resources/grammars.json found via applicationDirPath()/../Resources
#
# WHY A LAUNCHER RATHER THAN AN RPATH. Setting the rpath at link time would be
# tidier, but it has to be right for a layout that does not exist yet at
# configure time, and getting it wrong produces a binary that silently loads the
# HOST's Qt -- which works on the build machine and fails everywhere else. A
# three-line launcher is checkable by reading it.
#
# THE RESOURCE HERE IS ONE 6.7MB FILE, NOT A DIRECTORY OF THEMES, and it is not
# optional: without grammars.json the editor opens and highlights nothing, which
# looks like a tokenizer bug rather than a packaging one. That is why the smoke
# test below runs the binary with NO grammar argument -- it has to find its own.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

MAKE_TAR=1
BUILD_DIR="build-linux"
QTPREFIX="${CMAKE_PREFIX_PATH:-}"
# The editor application does not exist yet -- widget/ is headers only. Until it
# does, render_probe is a real Qt binary that links aced::core and loads the
# corpus, so it exercises every mechanism this script has: Qt libraries, the
# platform plugin, the launcher, and resource discovery. Swapping the default
# when app/ lands is a one-line change, not a rewrite.
TARGET="anyedit"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --qt) QTPREFIX="$2"; shift 2 ;;
        --qt=*) QTPREFIX="${1#*=}"; shift ;;
        --target) TARGET="$2"; shift 2 ;;
        --target=*) TARGET="${1#*=}"; shift ;;
        --no-tar) MAKE_TAR=0; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        -h|--help) sed -n '3,8p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

die() { echo "error: $*" >&2; exit 1; }
say() { echo "==> $*"; }

# --- which Qt ---------------------------------------------------------------

# Qt6_DIR points at <prefix>/lib/cmake/Qt6, so walk back up three.
if [[ -z "${QTPREFIX}" && -n "${Qt6_DIR:-}" ]]; then
    QTPREFIX="$(cd "${Qt6_DIR}/../../.." && pwd)"
fi

# Newest first, and NOT with `sort`. That orders as text, which puts 6.9.1 above
# 6.10.3 -- and a mismatch between the Qt the build links and the Qt this script
# deploys from is precisely what the whole verify section exists to prevent.
# `sort -V` compares the parts as numbers.
if [[ -z "${QTPREFIX}" ]]; then
    for base in "${HOME}/Qt" /opt/Qt; do
        [[ -d "${base}" ]] || continue
        for ver in $(ls -1 "${base}" 2>/dev/null | grep -E '^6\.[0-9]+' | sort -Vr); do
            for abi in gcc_64 gcc_arm64; do
                if [[ -x "${base}/${ver}/${abi}/bin/qmake" || -d "${base}/${ver}/${abi}/lib/cmake/Qt6" ]]; then
                    QTPREFIX="${base}/${ver}/${abi}"
                    break 3
                fi
            done
        done
    done
    [[ -n "${QTPREFIX}" ]] && say "found Qt at ${QTPREFIX} (pass --qt to choose a different one)"
fi

if [[ -n "${QTPREFIX}" ]]; then
    [[ -d "${QTPREFIX}/lib/cmake/Qt6" ]] \
        || die "${QTPREFIX} has no lib/cmake/Qt6 in it. The prefix is the
     directory holding bin/, lib/ and include/ -- e.g. ~/Qt/6.10.3/gcc_64,
     not ~/Qt and not the lib/cmake/Qt6 inside it."
fi

VERSION="$(sed -n 's/^project(anyeditqt VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[[ -n "${VERSION}" ]] || die "could not read the version out of CMakeLists.txt"
ARCH="$(uname -m)"
STAGE="dist/anyedit"

[[ -f grammars/grammars.json ]] \
    || die "grammars/grammars.json is missing; see grammars/README.md to regenerate it"

# --- build -----------------------------------------------------------------

# A CMAKE CACHE RECORDS THE ABSOLUTE PATH IT WAS CREATED AT, and it cannot be
# relocated. Move the tree, rename it, or hand somebody a zip that happens to
# contain a build directory, and every cmake call against it dies with "is
# different than the directory ... where CMakeCache.txt was created" before it
# builds anything. The message names the ORIGINAL author's home directory, so it
# reads as a path hardcoded somewhere in this script -- there is none. Wipe it
# rather than diagnose it: the directory is worthless on this machine.
#
# Symlinked home directories can make the comparison spuriously unequal. The
# cost of that is one rebuild, which is the right way round.
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    CACHED_SRC="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt")"
    CACHED_BLD="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt")"
    HERE_SRC="$(cd "${ROOT}" && pwd -P)"
    HERE_BLD="$(cd "${BUILD_DIR}" && pwd -P)"
    if [[ -n "${CACHED_SRC}" && "${CACHED_SRC}" != "${ROOT}" && "${CACHED_SRC}" != "${HERE_SRC}" ]] \
    || [[ -n "${CACHED_BLD}" && "${CACHED_BLD}" != "${HERE_BLD}" ]]; then
        say "${BUILD_DIR} was configured under a different path:"
        echo "        cached: ${CACHED_SRC:-?} -> ${CACHED_BLD:-?}"
        echo "        here:   ${HERE_SRC} -> ${HERE_BLD}"
        echo "    a CMake cache is not relocatable; wiping it."
        rm -rf "${BUILD_DIR}"
    fi
fi

# A CACHED Qt6_DIR BEATS -DCMAKE_PREFIX_PATH, so an existing build directory
# configured against a different Qt keeps using it and says nothing: the binary
# links one Qt while the staging below copies another. CMake offers no way to
# un-cache a find_package result, so the directory is wiped. That also throws
# away the FetchContent clones and costs a minute, which is cheaper than a
# tarball that dies on somebody else's machine.
if [[ -n "${QTPREFIX}" && -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    CACHED="$(sed -n 's/^Qt6_DIR:PATH=//p' "${BUILD_DIR}/CMakeCache.txt")"
    if [[ -n "${CACHED}" && "${CACHED}" != "${QTPREFIX}"/* ]]; then
        say "${BUILD_DIR} was configured against a different Qt:"
        echo "        cached: ${CACHED}"
        echo "        wanted: ${QTPREFIX}"
        echo "    wiping it, or the build and the staging would disagree."
        rm -rf "${BUILD_DIR}"
    fi
fi

say "building ${VERSION} into ${BUILD_DIR}"
# Every option passed EXPLICITLY, none left to its default. CMake's option()
# honours an existing cache entry, so changing a default in CMakeLists.txt has
# no effect on a build directory that already exists -- a tree carried forward
# from before widget/ had sources keeps BUILD_WIDGET:BOOL=OFF forever and the
# app target simply never appears. A -D on the command line overwrites it.
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release -DBUILD_WIDGET=ON -DBUILD_PROBES=ON
            -DACED_BUILD_TESTS=OFF)
[[ -n "${QTPREFIX}" ]] && CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${QTPREFIX}")

# tools/CMakeLists.txt prints the version, prefix and the actual libQt6Core it
# resolved. Those three lines are the whole point of this step, so they are kept
# rather than sent to /dev/null with the rest.
cmake -S . -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}" \
    | grep -E '^-- (Qt [0-9]|  (prefix|QtCore):)' || {
        cmake -S . -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"
        die "configure failed"
    }

# CHECKED BEFORE THE BUILD, not after. CMake says nothing about a --target that
# does not exist until it has already spent a couple of minutes on PCRE2, and
# "No rule to make target" then reads as a broken CMakeLists rather than as
# "that binary has not been written yet".
# Captured to a variable first, NOT piped into `grep -q`. Under `set -o
# pipefail` grep -q exits as soon as it matches, cmake gets SIGPIPE, the
# pipeline reports failure, and `if !` turns a successful match into "no such
# target" -- a false negative that only appears once the target exists.
TARGETS="$(cmake --build "${BUILD_DIR}" --target help 2>/dev/null || true)"
if ! grep -qx "\.\.\. ${TARGET}" <<<"${TARGETS}"; then
    echo >&2
    echo "no target named '${TARGET}' in ${BUILD_DIR}." >&2
    # app/ is in the tree, so a missing anyedit target is a fact about the
    # BUILD DIRECTORY, not about the source. Say which, because "that binary
    # does not exist yet" and "your cache is stale" send you to opposite ends
    # of the project.
    if [[ "${TARGET}" == "anyedit" ]] && grep -q "^BUILD_WIDGET:BOOL=OFF" \
            "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null; then
        echo "  ${BUILD_DIR} has BUILD_WIDGET:BOOL=OFF cached. CMake's option()" >&2
        echo "  honours an existing cache entry, so a build directory carried" >&2
        echo "  forward from before widget/ had sources keeps that value." >&2
        echo "      rm -rf ${BUILD_DIR} && $0" >&2
    else
        echo "  Available:" >&2
        sed -n 's/^\.\.\. /      /p' <<<"${TARGETS}" | grep -v autogen || true
    fi
    exit 1
fi

cmake --build "${BUILD_DIR}" --target "${TARGET}" -j"$(nproc)"

BIN="$(find "${BUILD_DIR}" -type f -name "${TARGET}" -perm -u+x | head -1)"
[[ -n "${BIN}" && -x "${BIN}" ]] || die "no ${TARGET} binary after the build"

# --- stage -----------------------------------------------------------------

say "staging into ${STAGE}"
rm -rf "${STAGE}"
mkdir -p "${STAGE}/bin" "${STAGE}/lib" "${STAGE}/plugins" "${STAGE}/Resources"

cp "${BIN}" "${STAGE}/bin/${TARGET}.bin"
cp grammars/grammars.json "${STAGE}/Resources/grammars.json"

# LICENCE TEXTS ARE A DISTRIBUTION OBLIGATION, not documentation. GPLv3 s4 wants
# a copy of the licence conveyed with the work, and LGPLv3 s4d wants the Qt
# notices and both licence texts with any package that links it. A package
# missing these is not merely undocumented, it is non-compliant -- and nothing
# about it looks wrong.
cp LICENSE "${STAGE}/Resources/LICENSE"
cp THIRD_PARTY_NOTICES.md "${STAGE}/Resources/THIRD_PARTY_NOTICES.md"
mkdir -p "${STAGE}/Resources/licenses"
cp -R licenses/. "${STAGE}/Resources/licenses/"

# Every non-system shared object the binary pulls in. The filter is what keeps
# this from copying libc and the graphics stack: those must come from the host,
# and shipping them is how a bundle stops working on a newer distribution than
# the one that built it.
#
# PCRE2 is deliberately absent from the filter. core/CMakeLists.txt links
# pcre2-8-static, so it is inside the binary and there is nothing to copy -- if
# a libpcre2-8.so ever shows up in this list, someone has switched the build to
# the system package and the pin in core/CMakeLists.txt has stopped meaning
# anything.
say "copying libraries"
# readelf -d, NOT ldd. ldd prints the whole transitive closure, and Qt6Core and
# Qt6Gui pull in the system libpcre2 through glib -- so `ldd | grep pcre2` is
# always non-empty and says nothing about how WE linked it. DT_NEEDED is the
# direct dependency list and is the thing to assert on.
if readelf -d "${BIN}" | grep NEEDED | grep -q 'libpcre2'; then
    die "the binary has a direct DT_NEEDED on libpcre2; core/CMakeLists.txt
     pins a static PCRE2 on purpose, so the build is not using the pinned one"
fi
# And the other half of the same problem: a static PCRE2 whose symbols are
# exported globally is worse than a dynamic one, because Qt's own pcre2 calls
# then bind to our version instead of the system's. core/CMakeLists.txt passes
# --exclude-libs,ALL to prevent it; this is the assertion that it worked.
if readelf --dyn-syms -W "${BIN}" 2>/dev/null | grep -q ' pcre2_compile_8'; then
    die "the static PCRE2's symbols are exported; Qt would resolve its own
     pcre2 calls against our copy. --exclude-libs,ALL did not take effect"
fi
ldd "${BIN}" | awk '/=> \//{print $3}' | while read -r lib; do
    case "$(basename "${lib}")" in
        libQt6*|libicu*) cp -Lu "${lib}" "${STAGE}/lib/" ;;
    esac
done

# The platform plugin is the one nobody remembers and the one whose absence is
# fatal: without libqxcb.so Qt exits with "could not load the Qt platform
# plugin", naming a plugin it did find nothing wrong with.
# Derived from the libQt6Core the binary ACTUALLY resolves to, not from qmake on
# PATH. Those are different things on a machine with two Qts, and taking the
# plugins from the wrong one produces a bundle whose platform plugin refuses to
# load against the Qt beside it -- "The plugin was built against Qt 6.4.2", from
# a tree that contains only 6.10.3.
#
# Two layouts: a distribution package puts plugins under <libdir>/qt6/plugins,
# the official installer and aqtinstall put them under <prefix>/plugins.
say "copying plugins"
QT_CORE_SO="$(ldd "${BIN}" | awk '/libQt6Core/{print $3}')"
[[ -n "${QT_CORE_SO}" ]] || die "the binary does not link libQt6Core"
QT_LIBDIR="$(dirname "${QT_CORE_SO}")"
say "linked against ${QT_CORE_SO}"

QT_PLUGIN_SRC=""
for cand in "${QT_LIBDIR}/qt6/plugins" "${QT_LIBDIR}/../plugins" "${QT_LIBDIR}/qt/plugins"; do
    if [[ -d "${cand}/platforms" ]]; then QT_PLUGIN_SRC="$(cd "${cand}" && pwd)"; break; fi
done
[[ -n "${QT_PLUGIN_SRC}" ]] \
    || die "could not find the plugin directory for ${QT_CORE_SO};
     looked in ${QT_LIBDIR}/qt6/plugins and ${QT_LIBDIR}/../plugins"
say "plugins from ${QT_PLUGIN_SRC}"

# If a Qt prefix was asked for, the plugins must have come from inside it. This
# is the assertion that the -DCMAKE_PREFIX_PATH above actually took effect,
# rather than find_package having quietly preferred something else.
if [[ -n "${QTPREFIX}" && "${QT_PLUGIN_SRC}" != "$(cd "${QTPREFIX}" && pwd)"/* ]]; then
    die "asked for Qt at ${QTPREFIX} but the binary links ${QT_CORE_SO}.
     The build used a different Qt than the one requested; wipe ${BUILD_DIR}
     and try again."
fi

for group in platforms platformthemes imageformats tls xcbglintegrations; do
    [[ -d "${QT_PLUGIN_SRC}/${group}" ]] || continue
    mkdir -p "${STAGE}/plugins/${group}"
    cp -Lu "${QT_PLUGIN_SRC}/${group}"/*.so "${STAGE}/plugins/${group}/" 2>/dev/null || true
done
[[ -f "${STAGE}/plugins/platforms/libqxcb.so" ]] \
    || die "libqxcb.so did not get copied; the bundle would not start"
# Shipped so the smoke test below can run headless on a build machine. It is
# also what a user on Wayland-only or over ssh -X falls back to, so it is not
# test-only weight.
[[ -f "${STAGE}/plugins/platforms/libqoffscreen.so" ]] \
    || die "libqoffscreen.so did not get copied; the verify step cannot run"

# Plugins brought their own Qt dependencies with them. Resolved against the
# ORIGINAL plugins, not the copies already in the stage.
#
# Qt from the official installer builds its plugins with RUNPATH
# $ORIGIN/../../lib. Staged at <stage>/plugins/platforms/, that resolves to
# <stage>/lib -- the directory being filled -- so ldd on a staged plugin reports
# the bundle'"'"'s own libraries and cp is asked to copy a file onto itself:
#
#   cp: '"'"'.../plugins/platforms/../../lib/libQt6Gui.so.6'"'"' and
#       '"'"'dist/anyedit/lib/libQt6Gui.so.6'"'"' are the same file
#
# A distribution Qt has no RUNPATH on its plugins at all, so the same loop reads
# from the system and works. The bug is invisible on a distro Qt and certain on
# an installer one.
for group in platforms platformthemes imageformats tls xcbglintegrations; do
    [[ -d "${QT_PLUGIN_SRC}/${group}" ]] || continue
    for plugin in "${QT_PLUGIN_SRC}/${group}"/*.so; do
        [[ -f "${plugin}" ]] || continue
        ldd "${plugin}" 2>/dev/null | awk '/=> \//{print $3}' | while read -r lib; do
            case "$(basename "${lib}")" in
                libQt6*|libicu*) ;;
                *) continue ;;
            esac
            # Belt and braces: never copy something that is already in the stage.
            [[ "$(readlink -f "${lib}")" == "$(readlink -f "${STAGE}")"/* ]] && continue
            cp -Lu "${lib}" "${STAGE}/lib/"
        done
    done
done

# --- icon and desktop entry ------------------------------------------------
#
# Staged so `install.sh` (and anyone unpacking the tarball) has them, NOT
# installed into the system from here: a packaging script that writes into
# ~/.local/share behind your back is a packaging script you cannot run twice
# without wondering what it did.
#
# hicolor, and one PNG per size rather than one large one. Desktop environments
# pick the size they want and scale only as a last resort; a single 512 icon is
# visibly soft in a 22px panel.

if [[ -d assets ]]; then
    say "staging the icon and desktop entry"
    for png in assets/anyedit-*.png; do
        [[ -f "${png}" ]] || continue
        sz="$(basename "${png}" .png)"; sz="${sz#anyedit-}"
        mkdir -p "${STAGE}/share/icons/hicolor/${sz}x${sz}/apps"
        cp "${png}" "${STAGE}/share/icons/hicolor/${sz}x${sz}/apps/anyedit.png"
    done
    if [[ -f assets/anyedit.desktop ]]; then
        mkdir -p "${STAGE}/share/applications"
        cp assets/anyedit.desktop "${STAGE}/share/applications/anyedit.desktop"
    fi
    ICONS="$(find "${STAGE}/share/icons" -name anyedit.png 2>/dev/null | wc -l)"
    echo "    ${ICONS} icon sizes"
else
    say "no assets/ directory; the tree will have no icon (run scripts/make-icons.py)"
fi

# --- launcher --------------------------------------------------------------

cat > "${STAGE}/bin/${TARGET}" <<LAUNCH
#!/usr/bin/env bash
# Resolves through symlinks, so this still works from a link in ~/bin.
HERE="\$(cd "\$(dirname "\$(readlink -f "\${BASH_SOURCE[0]}")")" && pwd)"
export LD_LIBRARY_PATH="\${HERE}/../lib\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}"
export QT_PLUGIN_PATH="\${HERE}/../plugins"
exec "\${HERE}/${TARGET}.bin" "\$@"
LAUNCH
chmod +x "${STAGE}/bin/${TARGET}"

# Installs into the user's own directories, not the system's. An absolute Exec
# is written at install time because the tarball can be unpacked anywhere, and a
# desktop entry with a relative Exec silently does nothing when launched from a
# menu (the working directory is not where you unpacked it).
cat > "${STAGE}/install.sh" <<'INSTALL'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor"
mkdir -p "${APPS}" "${HOME}/.local/bin"

ln -sf "${HERE}/bin/anyedit" "${HOME}/.local/bin/anyedit"
if [[ -d "${HERE}/share/icons/hicolor" ]]; then
    (cd "${HERE}/share/icons/hicolor" && find . -name anyedit.png) | while read -r rel; do
        mkdir -p "${ICONS}/$(dirname "${rel}")"
        cp "${HERE}/share/icons/hicolor/${rel}" "${ICONS}/${rel}"
    done
fi
if [[ -f "${HERE}/share/applications/anyedit.desktop" ]]; then
    sed "s|^Exec=anyedit |Exec=${HERE}/bin/anyedit |" \
        "${HERE}/share/applications/anyedit.desktop" > "${APPS}/anyedit.desktop"
fi
# Both are best effort: a minimal container has neither, and neither is needed
# for the entry to work once the session restarts.
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "${APPS}" || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t "${ICONS}" >/dev/null 2>&1 || true

echo "installed:"
echo "    ${HOME}/.local/bin/anyedit"
echo "    ${APPS}/anyedit.desktop"
echo
echo "If ~/.local/bin is not on PATH, add it. The menu entry does not need it."
INSTALL
chmod +x "${STAGE}/install.sh"

# --- verify ----------------------------------------------------------------
#
# Not decoration. This packaging has two failure modes that both look perfect on
# the build machine: loading the host's Qt instead of the shipped one, and not
# finding grammars.json. Checking here is the only place either is cheap.

say "verifying"
LIBS="$(ls "${STAGE}/lib" | wc -l)"
for f in LICENSE THIRD_PARTY_NOTICES.md licenses/GPL-3.0.txt licenses/LGPL-3.0.txt; do
    [[ -s "${STAGE}/Resources/${f}" ]] || die "Resources/${f} missing from the package;
    it is required to be distributed with the binary, not optional."
done
CORPUS="$(stat -c%s "${STAGE}/Resources/grammars.json")"
echo "    ${LIBS} libraries, corpus ${CORPUS} bytes"
[[ "${LIBS}" -ge 4 ]] || die "only ${LIBS} libraries staged; expected Qt Core, Gui, Widgets and ICU at least"
[[ "${CORPUS}" -ge 1000000 ]] || die "grammars.json is ${CORPUS} bytes; that is not the corpus"

# The binary is given NO grammar argument on purpose: this is the only run that
# proves the shipped tree can find its own corpus through applicationDirPath().
# Passing the path would test nothing that matters here.
# anyedit has --check: load the corpus, report, exit, no event loop. Anything
# else (render_probe) takes a source file and an output image. Either way the
# grammar path is NOT passed, because finding it is the thing being tested.
say "smoke test: running the staged binary without telling it where the corpus is"
if [[ "${TARGET}" == "anyedit" ]]; then
    CHECK="$(QT_QPA_PLATFORM=offscreen timeout 30 "${STAGE}/bin/${TARGET}" --check 2>&1 || true)"
    EXPECT="modes:"
else
    CHECK="$(QT_QPA_PLATFORM=offscreen timeout 30 "${STAGE}/bin/${TARGET}" \
                 "${ROOT}/core/src/document.cpp" /tmp/anyedit-bundle-check.png 2>&1 || true)"
    EXPECT="clamped=0"
fi
# Font-fallback chatter from the offscreen plugin is noise on a build machine
# with a thin font set, and it buries the three lines that matter.
grep -v "OpenType support missing" <<<"${CHECK}" | sed 's/^/    /'

if grep -q "no grammars found" <<<"${CHECK}"; then
    die "the staged tree did not find its own grammars.json"
fi
if grep -qi "could not load the Qt platform plugin" <<<"${CHECK}"; then
    die "the staged tree could not load its platform plugin"
fi
grep -q "${EXPECT}" <<<"${CHECK}" \
    || die "the staged tree started but did not report '${EXPECT}'; see above"
say "the staged tree starts, loads its own Qt, and finds its own corpus"

# The check above proves it RAN; this proves it ran on the SHIPPED Qt rather
# than the host's, which is the failure that looks like success on a build
# machine. ldd resolves against the launcher's environment, not the shell's.
HOSTQT="$(LD_LIBRARY_PATH="${PWD}/${STAGE}/lib" ldd "${STAGE}/bin/${TARGET}.bin" \
          | awk '/libQt6.*=> \//{print $3}' | grep -cv "^${PWD}/${STAGE}/lib" || true)"
if [[ "${HOSTQT}" -ne 0 ]]; then
    LD_LIBRARY_PATH="${PWD}/${STAGE}/lib" ldd "${STAGE}/bin/${TARGET}.bin" \
        | awk '/libQt6.*=> \//{print "    " $0}' >&2
    die "${HOSTQT} Qt libraries still resolve outside the bundle"
fi
say "every Qt library resolves inside the bundle"

# --- archive ---------------------------------------------------------------

if [[ "${MAKE_TAR}" -eq 1 ]]; then
    TARBALL="dist/anyedit-${VERSION}-linux-${ARCH}.tar.gz"
    say "writing ${TARBALL}"
    tar -czf "${TARBALL}" -C dist anyedit
    ls -lh "${TARBALL}"
    # Beside the archive, named after it, with a BARE FILENAME inside so
    # `sha256sum -c` works wherever the two are downloaded to. A checksum file
    # carrying "dist/anyedit-..." verifies only on the machine that built it.
    ( cd dist && sha256sum "$(basename "${TARBALL}")" > "$(basename "${TARBALL}").sha256" )
    cat "${TARBALL}.sha256"
fi

echo
echo "run it with:"
echo "      ${STAGE}/bin/${TARGET}"
