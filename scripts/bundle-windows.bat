@echo off
REM scripts\bundle-windows.bat
REM
REM Packages anyeditqt as a self-contained folder using windeployqt.
REM
REM   scripts\bundle-windows.bat
REM   scripts\bundle-windows.bat --qt C:\Qt\6.10.3\msvc2022_64 --zip
REM
REM Run it from an "x64 Native Tools Command Prompt for VS 2022" so cl.exe is on
REM PATH. A plain cmd with cl.exe on PATH is NOT enough: %VCToolsRedistDir% is
REM what locates the MSVC runtime below, and only the Developer Command Prompt
REM sets it. The build works and the deployment silently does not.
REM
REM NOT VERIFIED ON WINDOWS. Written against the documented behaviour of
REM windeployqt; every step prints what it settled on. Treat the first run as
REM the test. bundle-linux.sh IS verified and the two agree on layout.
REM
REM   Qt          --qt <prefix>
REM               %CMAKE_PREFIX_PATH%
REM               %Qt6_DIR%  (walked back up from lib\cmake\Qt6)
REM               the newest C:\Qt\6.*\msvc*_64
REM
REM windeployqt is taken from the Qt prefix, NOT from PATH. It must come from
REM the Qt the application linked against: a windeployqt from a different Qt
REM copies DLLs that do not match what the binary asks for, and the result runs
REM on the build machine and fails on a clean one. One variable feeding both
REM makes them agree by construction rather than by luck.
REM
REM Options:
REM   --target <name>    default anyedit
REM   --zip              also write dist\anyedit-<version>-windows-x64.zip
REM                      and a matching .sha256
REM   --build-dir <dir>  default build-win
REM   --help
REM
REM WHAT windeployqt DOES: copies the Qt DLLs beside the exe, and the plugins
REM into platforms\ and friends. What it does NOT do is copy application
REM resources, which is why grammars.json is copied here into
REM Resources\grammars.json -- the layout findGrammars() looks for beside the
REM executable. Without it the editor opens and highlights nothing, which reads
REM as a tokenizer bug rather than a packaging one.
REM
REM PCRE2 is linked statically from the FetchContent build, so there is no
REM pcre2 DLL to deploy. MSVC does not export an executable's symbols by
REM default, so the symbol-collision problem bundle-linux.sh guards against
REM does not arise here.

setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
pushd "%ROOT%"

set "QTPREFIX="
set "MAKEZIP=0"
set "BUILD_DIR=build-win"
set "TARGET=anyedit"
set "STAGE=dist\anyedit"

REM --- options ---------------------------------------------------------------

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--zip" (set "MAKEZIP=1" & shift & goto parse)
if /i "%~1"=="--target" (
    if "%~2"=="" (echo error: --target needs a name 1>&2 & goto fail)
    set "TARGET=%~2" & shift & shift & goto parse
)
if /i "%~1"=="--qt" (
    if "%~2"=="" (echo error: --qt needs a path 1>&2 & goto fail)
    set "QTPREFIX=%~f2" & shift & shift & goto parse
)
if /i "%~1"=="--build-dir" (
    if "%~2"=="" (echo error: --build-dir needs a path 1>&2 & goto fail)
    set "BUILD_DIR=%~2" & shift & shift & goto parse
)
if /i "%~1"=="--help" goto usage
if /i "%~1"=="-h" goto usage
echo error: unknown option: %~1 1>&2
goto fail
:parsed

REM --- the corpus ------------------------------------------------------------

REM --- version ---------------------------------------------------------------
REM
REM Out of CMakeLists.txt, the same single source project(VERSION) and the other
REM two scripts use. The zip was called anyedit-windows-x64.zip with no version
REM in it at all, which is fine until the second release overwrites the first
REM in somebody's downloads folder.

set "VERSION="
for /f "tokens=3" %%V in ('findstr /b /c:"project(anyeditqt VERSION" CMakeLists.txt') do set "VERSION=%%V"
if "%VERSION%"=="" (
    echo error: could not read the version out of CMakeLists.txt 1>&2
    goto fail
)

if not exist "grammars\grammars.json" (
    echo error: grammars\grammars.json is missing. 1>&2
    echo     See grammars\README.md to regenerate it; it needs an ace checkout
    echo     and node, neither of which this script touches.
    goto fail
)

REM --- Qt --------------------------------------------------------------------

if "%QTPREFIX%"=="" if not "%CMAKE_PREFIX_PATH%"=="" set "QTPREFIX=%CMAKE_PREFIX_PATH%"

REM Qt6_DIR points at <prefix>\lib\cmake\Qt6, so walk back up three.
if "%QTPREFIX%"=="" if not "%Qt6_DIR%"=="" (
    for %%P in ("%Qt6_DIR%\..\..\..") do set "QTPREFIX=%%~fP"
)

REM Newest first, and NOT with dir /o-n. That sorts as TEXT, which puts 6.8.3
REM above 6.10.3 -- and the resulting mismatch is the exact thing this script is
REM supposed to prevent, because it deploys one Qt's DLLs beside a binary linked
REM against another. PowerShell's [version] cast compares the parts as numbers,
REM which is the only correct way to order these.
if "%QTPREFIX%"=="" if exist "C:\Qt" (
    for /f "delims=" %%D in ('powershell -NoProfile -Command ^
        "Get-ChildItem 'C:\Qt' -Directory -Filter '6.*' ^| Where-Object { $_.Name -match '^^6\.' } ^| Sort-Object { [version]$_.Name } -Descending ^| Select-Object -ExpandProperty Name" 2^>nul') do (
        if "!QTPREFIX!"=="" (
            for /f "delims=" %%E in ('dir /b /ad "C:\Qt\%%D\msvc*_64" 2^>nul') do (
                if "!QTPREFIX!"=="" set "QTPREFIX=C:\Qt\%%D\%%E"
            )
        )
    )
    if not "!QTPREFIX!"=="" (
        echo ==^> guessed Qt at !QTPREFIX!
        echo     ^(pass --qt or set CMAKE_PREFIX_PATH to choose a different one^)
    )
)

if "%QTPREFIX%"=="" (
    echo error: no Qt prefix. Either:
    echo         scripts\bundle-windows.bat --qt C:\Qt\6.10.3\msvc2022_64
    echo     or  set CMAKE_PREFIX_PATH=C:\Qt\6.10.3\msvc2022_64
    echo     Nothing matching C:\Qt\6.*\msvc*_64 was found either.
    goto fail
)
if not exist "%QTPREFIX%\bin\windeployqt.exe" (
    echo error: no windeployqt.exe under %QTPREFIX%\bin 1>&2
    echo     That does not look like a Qt prefix. It should be the directory
    echo     holding bin\, lib\ and include\ -- e.g. C:\Qt\6.10.3\msvc2022_64,
    echo     not C:\Qt and not the lib\cmake\Qt6 inside it.
    goto fail
)

REM --- toolchain -------------------------------------------------------------

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo error: cl.exe not on PATH. 1>&2
    echo     Run this from an "x64 Native Tools Command Prompt for VS 2022",
    echo     or run vcvars64.bat in this shell first.
    goto fail
)

REM git is needed at CONFIGURE time, not build time: core/CMakeLists.txt pulls
REM PCRE2 and nlohmann through FetchContent. Checked here because the failure is
REM a wall of CMake FetchContent output that never says "install git".
where git.exe >nul 2>nul
if errorlevel 1 (
    echo error: git.exe not on PATH. 1>&2
    echo     core\CMakeLists.txt fetches PCRE2 and nlohmann/json at configure
    echo     time; without git the configure step fails inside FetchContent.
    goto fail
)

echo ==^> version    %VERSION%
echo ==^> Qt         %QTPREFIX%
echo ==^> target     %TARGET%
echo ==^> build dir  %BUILD_DIR%

REM --- build -----------------------------------------------------------------

REM A CMAKE CACHE RECORDS THE ABSOLUTE PATH IT WAS CREATED AT and cannot be
REM relocated. A build directory that travelled with the tree -- in a zip, or a
REM copy under a new name -- fails every cmake call with "is different than the
REM directory ... where CMakeCache.txt was created", naming somebody else's
REM folder, which reads as a path hardcoded in this script. There is none. Wipe
REM it. CMake writes forward slashes into the cache, so compare that way.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /b /c:"CMAKE_HOME_DIRECTORY:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt" > "%TEMP%\anyedit_home.txt" 2>nul
    set "CACHED_SRC="
    for /f "tokens=2 delims==" %%V in ('type "%TEMP%\anyedit_home.txt"') do set "CACHED_SRC=%%V"
    del "%TEMP%\anyedit_home.txt" >nul 2>nul
    set "HERE_SRC=%CD:\=/%"
    if not "!CACHED_SRC!"=="" if /i not "!CACHED_SRC!"=="!HERE_SRC!" (
        echo ==^> %BUILD_DIR% was configured under a different path:
        echo         cached: !CACHED_SRC!
        echo         here:   !HERE_SRC!
        echo     a CMake cache is not relocatable; wiping it.
        rmdir /s /q "%BUILD_DIR%"
    )
)

REM A CACHED Qt6_DIR BEATS -DCMAKE_PREFIX_PATH, so an existing build directory
REM configured against a different Qt keeps using it and says nothing: the
REM binary links one Qt while windeployqt below deploys another. Wiping is the
REM only reliable answer -- CMake offers no way to un-cache a find_package
REM result. It also throws away the FetchContent clones, which costs a minute;
REM that is still cheaper than a package that fails on somebody else's machine.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /c:"Qt6_DIR:PATH=" "%BUILD_DIR%\CMakeCache.txt" > "%TEMP%\anyedit_qtdir.txt" 2>nul
    set "CACHED="
    for /f "tokens=2 delims==" %%V in ('type "%TEMP%\anyedit_qtdir.txt"') do set "CACHED=%%V"
    del "%TEMP%\anyedit_qtdir.txt" >nul 2>nul
    if not "!CACHED!"=="" (
        echo !CACHED! | findstr /i /c:"%QTPREFIX:\=/%" >nul
        if errorlevel 1 (
            echo ==^> %BUILD_DIR% was configured against a different Qt:
            echo         cached: !CACHED!
            echo         wanted: %QTPREFIX%
            echo     wiping it, or the build and windeployqt would disagree.
            rmdir /s /q "%BUILD_DIR%"
        )
    )
)

echo ==^> building
REM Every option EXPLICITLY, including BUILD_WIDGET -- which was missing here
REM while the other two scripts passed it. option() honours an existing cache
REM entry, so a build directory carried forward from before widget/ had sources
REM keeps BUILD_WIDGET:BOOL=OFF and the anyedit target simply never appears.
cmake -S . -B "%BUILD_DIR%" -DCMAKE_PREFIX_PATH="%QTPREFIX%" ^
      -DCMAKE_BUILD_TYPE=Release -DBUILD_WIDGET=ON -DBUILD_PROBES=ON ^
      -DACED_BUILD_TESTS=OFF
if errorlevel 1 goto fail

cmake --build "%BUILD_DIR%" --config Release --target %TARGET%
if errorlevel 1 goto fail

REM Multi-config generators (MSBuild, the default here) put it under Release\;
REM single-config ones (Ninja) do not. app\ FIRST, because that is where the
REM default target lives; tools\ was first while render_probe was the default
REM and searching in that order costs nothing but reads as though the probe
REM still is.
set "EXE=%BUILD_DIR%\app\Release\%TARGET%.exe"
if not exist "%EXE%" set "EXE=%BUILD_DIR%\app\%TARGET%.exe"
if not exist "%EXE%" set "EXE=%BUILD_DIR%\tools\Release\%TARGET%.exe"
if not exist "%EXE%" set "EXE=%BUILD_DIR%\tools\%TARGET%.exe"
if not exist "%EXE%" (
    echo error: no %TARGET%.exe after the build 1>&2
    echo     Looked under %BUILD_DIR%\tools\ and %BUILD_DIR%\app\
    goto fail
)

REM --- stage -----------------------------------------------------------------

echo ==^> staging into %STAGE%
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
mkdir "%STAGE%\Resources"

copy /y "%EXE%" "%STAGE%\%TARGET%.exe" >nul
if errorlevel 1 goto fail
copy /y "grammars\grammars.json" "%STAGE%\Resources\grammars.json" >nul
if errorlevel 1 goto fail

REM LICENCE TEXTS ARE A DISTRIBUTION OBLIGATION, not documentation. GPLv3 s4
REM wants a copy of the licence conveyed with the work, and LGPLv3 s4d wants
REM the Qt notices and both licence texts with any package that links it. A
REM package missing these is not merely undocumented, it is non-compliant --
REM and nothing about it looks wrong.
copy /y "LICENSE" "%STAGE%\Resources\LICENSE" >nul
if errorlevel 1 goto fail
copy /y "THIRD_PARTY_NOTICES.md" "%STAGE%\Resources\THIRD_PARTY_NOTICES.md" >nul
if errorlevel 1 goto fail
xcopy /e /i /y /q "licenses" "%STAGE%\Resources\licenses" >nul
if errorlevel 1 goto fail

REM --- windeployqt -----------------------------------------------------------

echo ==^> running windeployqt from %QTPREFIX%\bin
"%QTPREFIX%\bin\windeployqt.exe" --release --no-translations ^
    --no-system-d3d-compiler --compiler-runtime "%STAGE%\%TARGET%.exe"
if errorlevel 1 goto fail

REM --- the MSVC runtime ------------------------------------------------------
REM
REM WITHOUT THIS THE PACKAGE DIES ON A CLEAN BOX and runs fine here, because
REM this machine has the VS 2022 redistributable installed and the loader finds
REM it system-wide.
REM
REM --compiler-runtime above is asked for but not trusted. It locates the
REM redistributable through %VCINSTALLDIR%, which only a Developer Command
REM Prompt sets, and depending on the Qt version it may drop vc_redist.x64.exe
REM into the folder INSTEAD of the DLLs. An installer sitting in a directory is
REM not a deployed runtime, and the folder looks equally plausible either way.
REM
REM ucrtbase.dll is deliberately not in the list. The UCRT ships with Windows
REM itself from 10 onward, which is below anything this targets.

set "CRTOK=1"
for %%F in (VCRUNTIME140.dll VCRUNTIME140_1.dll MSVCP140.dll) do (
    if not exist "%STAGE%\%%F" set "CRTOK=0"
)

if "!CRTOK!"=="0" (
    if "%VCToolsRedistDir%"=="" (
        echo error: MSVC runtime DLLs missing from %STAGE%, and 1>&2
        echo         %%VCToolsRedistDir%% is unset so they cannot be located.
        echo     Run this from an "x64 Native Tools Command Prompt for VS 2022".
        goto fail
    )
    set "CRTDIR="
    for /f "delims=" %%D in ('dir /b /ad /o-n "%VCToolsRedistDir%x64\Microsoft.VC*.CRT" 2^>nul') do (
        if "!CRTDIR!"=="" set "CRTDIR=%VCToolsRedistDir%x64\%%D"
    )
    if "!CRTDIR!"=="" (
        REM !VAR! AND NOT %VAR%, and this is not style. Percent-expansion
        REM happens when cmd parses this whole if-block, before it runs a line
        REM of it -- so on a machine where VS lives under
        REM "C:\Program Files (x86)\..." the ")" in "(x86)" closes the block
        REM early and the rest of the path becomes a stray command. Delayed
        REM expansion happens at execution, after the parse that would break.
        echo error: no Microsoft.VC*.CRT directory under 1>&2
        echo         !VCToolsRedistDir!x64
        echo     Add "MSVC v143 - VS 2022 C++ x64/x86 Redistributable MSMs" in
        echo     the VS Installer.
        goto fail
    )
    echo ==^> copying the MSVC runtime from !CRTDIR!
    copy /y "!CRTDIR!\VCRUNTIME140.dll"   "%STAGE%\" >nul
    copy /y "!CRTDIR!\VCRUNTIME140_1.dll" "%STAGE%\" >nul
    copy /y "!CRTDIR!\MSVCP140.dll"       "%STAGE%\" >nul
)

REM Shipping an .exe that asks for elevation inside a folder meant to be
REM unzip-and-run is the wrong signal to whoever receives it.
if exist "%STAGE%\vc_redist.x64.exe" del /q "%STAGE%\vc_redist.x64.exe"

REM --- verify ----------------------------------------------------------------
REM
REM The platform plugin is the one whose absence is fatal and whose error
REM message does not name it usefully: without platforms\qwindows.dll the
REM application exits with "could not find or load the Qt platform plugin".

if not exist "%STAGE%\platforms\qwindows.dll" (
    echo error: platforms\qwindows.dll missing; the package would not start 1>&2
    goto fail
)
if not exist "%STAGE%\Qt6Widgets.dll" (
    echo error: Qt6Widgets.dll missing; windeployqt did not do its job 1>&2
    goto fail
)
if not exist "%STAGE%\Resources\grammars.json" (
    echo error: Resources\grammars.json missing; the package would open with 1>&2
    echo        no syntax highlighting at all and no message saying why.
    goto fail
)

for %%F in (LICENSE THIRD_PARTY_NOTICES.md licenses\GPL-3.0.txt licenses\LGPL-3.0.txt) do (
    if not exist "%STAGE%\Resources\%%F" (
        echo error: Resources\%%F missing. It is required to be distributed 1>&2
        echo        with the binary, not optional.
        goto fail
    )
)

for %%F in (VCRUNTIME140.dll VCRUNTIME140_1.dll MSVCP140.dll) do (
    if not exist "%STAGE%\%%F" (
        echo error: %%F missing; the package dies at launch on a machine 1>&2
        echo        without the VS 2022 redistributable, with a missing-DLL
        echo        dialog that says nothing about Qt or about anyedit.
        goto fail
    )
)

REM Everything above proves files are present; this proves they are the RIGHT
REM files. A Qt6Core.dll from a different Qt than the binary was linked against
REM is present, correctly named, and wrong -- and the package still runs here,
REM because the real Qt is on PATH on this machine.
for /f "tokens=2 delims==" %%V in ('findstr /c:"Qt6_DIR:PATH=" "%BUILD_DIR%\CMakeCache.txt"') do set "USEDQT=%%V"
echo !USEDQT! | findstr /i /c:"%QTPREFIX:\=/%" >nul
if errorlevel 1 (
    echo error: the build and windeployqt used different Qt installations. 1>&2
    echo         linked against: !USEDQT!
    echo         deployed from:  %QTPREFIX%
    echo     Delete %BUILD_DIR% and run this again.
    goto fail
)

REM The smoke test gets NO grammar argument on purpose: this is the only run
REM that proves the staged folder finds its own corpus through
REM applicationDirPath(). QT_QPA_PLATFORM is not set -- windows is the only
REM platform plugin here and it works without a session.
REM
REM WHICH ARGUMENTS DEPEND ON THE TARGET. anyedit given render_probe's
REM arguments -- a source file and a PNG path -- opens that file in a window and
REM enters the event loop, so the script never returns. There is no timeout(1)
REM here to save it. anyedit has --check for exactly this: load the corpus,
REM print, exit, no event loop.
echo ==^> smoke test: running the staged exe with no grammar argument
if /i "%TARGET%"=="anyedit" (
    set "EXPECT=modes:"
    "%STAGE%\%TARGET%.exe" --check > "%TEMP%\anyedit-check.txt" 2>&1
) else (
    set "EXPECT=clamped=0"
    "%STAGE%\%TARGET%.exe" "core\src\document.cpp" "%TEMP%\anyedit-bundle-check.png" > "%TEMP%\anyedit-check.txt" 2>&1
)
type "%TEMP%\anyedit-check.txt"
findstr /c:"no grammars found" "%TEMP%\anyedit-check.txt" >nul
if not errorlevel 1 (
    echo error: the staged folder did not find its own grammars.json 1>&2
    goto fail
)
findstr /c:"!EXPECT!" "%TEMP%\anyedit-check.txt" >nul
if errorlevel 1 (
    echo error: the staged folder ran but did not report "!EXPECT!"; see above 1>&2
    goto fail
)
del "%TEMP%\anyedit-check.txt" >nul 2>nul

echo ==^> platform plugin, Qt DLLs, MSVC runtime and corpus present,
echo     one Qt throughout, and the staged tree finds its own corpus

REM --- zip -------------------------------------------------------------------

REM NOT inside a parenthesised if-block, and each powershell command on ONE
REM line. Both rules are the same bug twice over:
REM
REM   ^ is a line continuation only OUTSIDE quotes. Inside a quoted string it is
REM   an ordinary character, so a "..." that spans lines ends at the first line
REM   break and cmd tries to run the remainder as a command. That is where
REM   "-Value was unexpected at this time" came from -- AFTER every verification
REM   had passed, which makes it read as a packaging failure rather than a typo.
REM
REM   And a ( ) block is parsed in full before any of it runs, so a stray
REM   parenthesis inside it -- of which a PowerShell expression has several --
REM   can close the block early. A goto costs nothing and removes the class.

if not "%MAKEZIP%"=="1" goto :skipzip

set "ZIP=dist\anyedit-%VERSION%-windows-x64.zip"
echo ==^> writing %ZIP%
if exist "%ZIP%" del /q "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path 'dist\anyedit' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 goto fail
if not exist "%ZIP%" (
    echo error: Compress-Archive reported success but wrote no %ZIP% 1>&2
    goto fail
)

REM Bare filename inside the .sha256, so the pair verifies wherever it is
REM downloaded to rather than only where it was built. Two spaces between hash
REM and name is sha256sum's format, so one file works on all three platforms.
REM Single quotes throughout: nesting double quotes inside a cmd "..." is its
REM own category of pain and is not needed here.
powershell -NoProfile -Command "$h=(Get-FileHash '%ZIP%' -Algorithm SHA256).Hash.ToLower(); $n=Split-Path '%ZIP%' -Leaf; Set-Content -Path '%ZIP%.sha256' -Value ($h+'  '+$n) -Encoding ASCII"
if errorlevel 1 goto fail
type "%ZIP%.sha256"

:skipzip

echo.
echo run it with:
echo       %STAGE%\%TARGET%.exe
popd
exit /b 0

:usage
echo Packages anyeditqt as a self-contained folder using windeployqt.
echo.
echo   scripts\bundle-windows.bat [--qt ^<prefix^>] [--target ^<name^>]
echo                              [--zip] [--build-dir ^<dir^>]
echo.
echo Environment: CMAKE_PREFIX_PATH, Qt6_DIR
popd
exit /b 0

:fail
echo.
echo build failed
popd
exit /b 1