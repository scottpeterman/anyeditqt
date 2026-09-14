# Third-party notices

anyeditqt is licensed under the GNU General Public License v3.0; see `LICENSE`.
It incorporates the components below, whose licences require their copyright
notices and warranty disclaimers to be reproduced in the materials distributed
with a binary.

**Maintained by hand.** There is no module graph to enumerate here -- five
components, two of them pinned by tag in `core/CMakeLists.txt` and three
incorporated as source or as derived data. `test_notices.cpp` in the app suite
fails the build if a version named below stops matching the pin it describes,
which is the only part of this file that can drift silently.

Every licence text below was taken from the component as shipped, and the same
texts are kept verbatim under `licenses/`. The packaging scripts copy `LICENSE`,
this file and `licenses/` into every binary package.

## Dynamically linked

### Qt 6

- Copyright The Qt Company Ltd and other contributors
- GNU Lesser General Public License v3.0
- https://www.qt.io/

anyeditqt uses Qt 6 (QtCore, QtGui, QtWidgets) and links against the unmodified
Qt shared libraries dynamically. Qt is not modified, statically linked, or
otherwise incorporated into the anyeditqt binary. anyeditqt is conveyed under
GPLv3, which LGPLv3 section 3 expressly permits.

The dynamic link is what preserves the LGPL's relinking right: a recipient may
replace the Qt libraries in a package with their own build of the same Qt
version. The packaging scripts deploy Qt beside the executable rather than
rewriting it into the binary, and no Qt library is statically linked.

Full licence text: `licenses/LGPL-3.0.txt`, together with `licenses/GPL-3.0.txt`
which it incorporates by reference. Help -> About Qt in the application shows
The Qt Company's own notice.

## Statically linked

### PCRE2

- Copyright University of Cambridge, Philip Hazel and Zoltan Herczeg
- BSD-3-Clause
- https://github.com/PCRE2Project/pcre2
- Version 10.44, pinned by `GIT_TAG pcre2-10.44` in `core/CMakeLists.txt`

The regular-expression engine behind the tokenizer. Fetched and built from
source at configure time and linked statically into `aced_core`, so it is
present in every binary and absent from this source tree.

PCRE2 rather than the platform's copy because the three platforms have to agree
on one engine, and rather than RE2 or Go's `regexp` because 4,391 regexes in the
grammar corpus use lookahead, which RE2 has no form of.

```
PCRE2 LICENCE
-------------

PCRE2 is a library of functions to support regular expressions whose syntax
and semantics are as close as possible to those of the Perl 5 language.

Releases 10.00 and above of PCRE2 are distributed under the terms of the "BSD"
licence, as specified below, with one exemption for certain binary
redistributions. The documentation for PCRE2, supplied in the "doc" directory,
is distributed under the same terms as the software itself. The data in the
testdata directory is not copyrighted and is in the public domain.

The basic library functions are written in C and are freestanding. Also
included in the distribution is a just-in-time compiler that can be used to
optimize pattern matching. This is an optional feature that can be omitted when
the library is built.


THE BASIC LIBRARY FUNCTIONS
---------------------------

Written by:       Philip Hazel
Email local part: Philip.Hazel
Email domain:     gmail.com

Retired from University of Cambridge Computing Service,
Cambridge, England.

Copyright (c) 1997-2024 University of Cambridge
All rights reserved.


PCRE2 JUST-IN-TIME COMPILATION SUPPORT
--------------------------------------

Written by:       Zoltan Herczeg
Email local part: hzmester
Email domain:     freemail.hu

Copyright(c) 2010-2024 Zoltan Herczeg
All rights reserved.


STACK-LESS JUST-IN-TIME COMPILER
--------------------------------

Written by:       Zoltan Herczeg
Email local part: hzmester
Email domain:     freemail.hu

Copyright(c) 2009-2024 Zoltan Herczeg
All rights reserved.


THE "BSD" LICENCE
-----------------

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notices,
      this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notices, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    * Neither the name of the University of Cambridge nor the names of any
      contributors may be used to endorse or promote products derived from this
      software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


EXEMPTION FOR BINARY LIBRARY-LIKE PACKAGES
------------------------------------------

The second condition in the BSD licence (covering binary redistributions) does
not apply all the way down a chain of software. If binary package A includes
PCRE2, it must respect the condition, but if package B is software that
includes package A, the condition is not imposed on package B unless it uses
PCRE2 independently.

End
```

### nlohmann/json

- Copyright Niels Lohmann
- MIT
- https://github.com/nlohmann/json
- Version 3.11.3, pinned by `GIT_TAG v3.11.3` in `core/CMakeLists.txt`

Header-only, used only to read the grammar corpus, and kept PRIVATE so nothing
downstream of `aced::core` inherits it or has to agree on its version.

```
MIT License 

Copyright (c) 2013-2022 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Incorporated as source or derived data

### ace

- Copyright Ajax.org B.V.
- BSD-3-Clause
- https://github.com/ajaxorg/ace

Two things in this tree come from ace, and neither is a copy of its JavaScript.

`grammars/grammars.json` is a mechanical export of ace's 198 highlight rule sets
-- 36,136 rules -- produced by `grammars/export_modes.js` from an ace checkout.
It is derived from `src/mode/*_highlight_rules.js` and carries ace's licence.
`grammars/README.md` covers how it is produced. THE EXPORT DOES NOT RECORD THE
ACE COMMIT IT CAME FROM, so this notice cannot name one; recording it is a
change to the exporter, not to this file.

`core/src/foldmode.cpp` is a port of ace's `src/mode/folding/` -- `fold_mode.js`,
`cstyle.js`, `coffee.js` and `pythonic.js` -- to C++. `core/src/grammar.cpp` and
`core/include/aced/tokenizer.h` are a port of `src/tokenizer.js`. The code is
rewritten rather than translated line by line, but the algorithms and the
regular expressions are ace's.

```
Copyright (c) 2010, Ajax.org B.V.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Ajax.org B.V. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL AJAX.ORG B.V. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### Scintilla

- Copyright 1998-2007 Neil Hodgson
- Permissive, attribution only
- https://www.scintilla.org/

`core/include/aced/linemap.h` and `core/src/linemap.cpp` follow the design of
Scintilla's `ContractionState` -- one table for folding and wrapping together --
and the lazy step in its `Partitioning`. The code is written here, not copied,
but the algorithm is Scintilla's and the attribution is owed either way.

NOTHING IS TAKEN FROM QScintilla, whose Qt wrapper is GPLv3 or a Riverbank
commercial licence. Only `scintilla/` in that tree, which carries the licence
below, was read. The distinction matters because anyeditqt's own GPLv3 would
make a QScintilla derivation look unremarkable while still being a different
licence with different obligations.

```
License for Scintilla and SciTE

Copyright 1998-2003 by Neil Hodgson <neilh@scintilla.org>

All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation.

NEIL HODGSON DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS, IN NO EVENT SHALL NEIL HODGSON BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
OR PERFORMANCE OF THIS SOFTWARE.
```

## Not third-party

`grammars/foldtable.inc` (which ace fold mode each of the 198 corpus modes uses)
is generated from the ace tree and is a table of facts about it, not code from
it. It is listed here only so its provenance is written down somewhere.

The icons under `assets/` were produced for this project by
`scripts/make-icons.py`.
