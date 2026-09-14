# Releasing

`dist/` is gitignored and stays that way. GitHub release **assets** are uploaded
to a release object, not committed to the repository — the two are separate
stores, and a repo carrying 26 MB binaries per version is a repo nobody wants to
clone.

So the shape is: tag the source, build on each platform, upload three files.

## 1. One version number

`project(anyeditqt VERSION x.y.z)` in the top-level `CMakeLists.txt` is the
single source. All three bundle scripts read it, the About dialog gets it
through `ANYEDIT_VERSION`, and the artifact names carry it. Bump it there and
nowhere else.

```bash
ctest --test-dir build --output-on-failure    # 126 + 92 + 88 cases
git commit -am "v0.1.0"
git tag -a v0.1.0 -m "v0.1.0"
git push origin main --tags
```

**Tag before building.** The tag is the corresponding source GPLv3 section 6
requires you to offer alongside the binaries, and "the source is in the repo" is
only true if there is a tag naming the state the binaries were built from.

## 2. Build on each platform

There is no cross-compiling here. Qt, the platform plugin and the C++ runtime all
come from the machine doing the build, so each artifact is built on its own
platform, from the tag.

```bash
git checkout v0.1.0

./scripts/bundle-linux.sh              # dist/anyedit-0.1.0-linux-x86_64.tar.gz
./scripts/bundle-mac.sh --dmg          # dist/anyedit-0.1.0-macos-arm64.dmg
scripts\bundle-windows.bat --zip       # dist\anyedit-0.1.0-windows-x64.zip
```

Each writes a matching `.sha256` beside its archive, with a bare filename inside
so `sha256sum -c` works wherever the pair is downloaded to rather than only on
the machine that built it.

Every script refuses to produce a package that is missing `LICENSE`,
`THIRD_PARTY_NOTICES.md` or `licenses/`. That is not a tidiness check: conveying
those with a binary is a GPLv3 and LGPLv3 obligation, and a non-compliant package
looks exactly like a compliant one.

## 3. Upload

With the GitHub CLI, from the machine that has all three files gathered:

```bash
gh release create v0.1.0 \
  --title "AnyEdit 0.1.0" \
  --notes-file RELEASE_NOTES.md \
  dist/anyedit-0.1.0-linux-x86_64.tar.gz \
  dist/anyedit-0.1.0-linux-x86_64.tar.gz.sha256 \
  dist/anyedit-0.1.0-macos-arm64.dmg \
  dist/anyedit-0.1.0-macos-arm64.dmg.sha256 \
  dist/anyedit-0.1.0-windows-x64.zip \
  dist/anyedit-0.1.0-windows-x64.zip.sha256
```

Or Releases → Draft a new release in the web UI, choose the tag, and drag the
files into the assets box. `gh release upload v0.1.0 <file>` adds one to an
existing release, which is what you want when the third platform is built a day
later than the first two.

GitHub attaches "Source code (zip/tar.gz)" to every release automatically, from
the tag. That is the corresponding source, and it is why tagging first matters.

**Asset names cannot be changed once anyone has linked one.** They are
`anyedit-<version>-<os>-<arch>.<ext>` across all three platforms; keep it.

## 4. What users hit on first launch

Both of these are consequences of shipping unsigned binaries, they are not bugs,
and neither error message says anything useful about the cause. Put the
workarounds in the release notes rather than in an issue tracker later.

**macOS.** A downloaded `.dmg` is quarantined, and an unsigned, un-notarized app
is refused with *"AnyEdit is damaged and can't be opened"* — which is a lie, and
reads as a corrupt download. Either right-click the app and choose Open (which
offers an override the double-click path does not), or:

```bash
xattr -dr com.apple.quarantine /Applications/AnyEdit.app
```

Signing needs a paid Apple Developer account; notarization needs that plus an
upload step. Worth doing if this gets an audience, not before.

**Windows.** SmartScreen shows *"Windows protected your PC"* for an unsigned
executable with no download reputation. More info → Run anyway. A code-signing
certificate removes it; reputation also accrues on its own with downloads.

**Linux.** Nothing. Untar and run `anyedit/bin/anyedit`. The launcher sets the
library path so the bundled Qt is used rather than any system copy.

## 5. Release notes

Keep them about what changed and what is not there yet. The gap list in
`README.md` is honest and should stay that way in the notes: IME preedit,
multi-cursor, word-wise Ctrl+arrow, bracket matching, xml/html folding,
multi-line search, and detecting that a file changed on disk.

State the platform each artifact was built on and the Qt version it carries.
"macOS 14, arm64, Qt 6.10.3" answers the first question anyone opening an issue
will be asked.
