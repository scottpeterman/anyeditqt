// A few grammars spell "any astral character" the UTF-16 way, as a surrogate-pair
// range. That is meaningless to a UTF-8 regex engine, and lone surrogates can't
// even survive JSON. Translate to PCRE2 code-point syntax.
//
// Worth noting for the Qt port: QString is UTF-16, so QRegularExpression would
// accept the originals unchanged. This step only exists because the harness runs
// PCRE2 over UTF-8. If you go QString-native, delete it.

const fs = require('fs');
const g = JSON.parse(fs.readFileSync('grammars.json', 'utf8'));

const HI = /[\uD800-\uDBFF]/, LO = /[\uDC00-\uDFFF]/;

function fix(re) {
  // [\uD800-\uDBFF][\uDC00-\uDFFF]  ->  [\x{10000}-\x{10FFFF}]   (a full pair)
  re = re.replace(
    /\[([\uD800-\uDBFF])-([\uD800-\uDBFF])\]\[([\uDC00-\uDFFF])-([\uDC00-\uDFFF])\]/g,
    '[\\x{10000}-\\x{10FFFF}]');
  // a bare high-surrogate range inside a class -> the astral range
  re = re.replace(/[\uD800-\uDBFF]-[\uD800-\uDBFF]/g, '\\x{10000}-\\x{10FFFF}');
  // preserve well-formed pairs (real astral literals like U+1D452); strip only strays
  let out = '';
  for (let i = 0; i < re.length; i++) {
    const c = re[i], n = re[i + 1];
    if (HI.test(c)) {
      if (n && LO.test(n)) { out += c + n; i++; }   // valid pair, keep
      continue;                                      // stray high, drop
    }
    if (LO.test(c)) continue;                        // stray low, drop
    out += c;
  }
  return out;
}

// JS spells a code point \uFFFE; PCRE2 spells it \x{FFFE}. Pure syntax, no
// semantic change. 53 regexes in the corpus need it.
function uEscapes(re) {
  let out = '', i = 0;
  while (i < re.length) {
    if (re[i] === '\\' && re[i + 1] === 'u' && /^[0-9a-fA-F]{4}$/.test(re.substr(i + 2, 4))) {
      out += '\\x{' + re.substr(i + 2, 4) + '}'; i += 6; continue;
    }
    if (re[i] === '\\') { out += re[i] + (re[i + 1] || ''); i += 2; continue; }
    out += re[i++];
  }
  return out;
}

let uFixed = 0;
for (const m in g) for (const s in g[m]) for (const r of g[m][s]) {
  if (typeof r.regex !== 'string') continue;
  const f = uEscapes(r.regex);
  if (f !== r.regex) { r.regex = f; uFixed++; }
}

let touched = 0;
for (const m in g) for (const s in g[m]) for (const r of g[m][s]) {
  if (typeof r.regex !== 'string') continue;
  if (!HI.test(r.regex) && !LO.test(r.regex)) continue;
  const f = fix(r.regex);
  if (f !== r.regex) touched++;
  r.regex = f;
}

// verify
let lone = 0;
for (const m in g) for (const s in g[m]) for (const r of g[m][s]) {
  if (typeof r.regex !== 'string') continue;
  for (let i = 0; i < r.regex.length; i++) {
    const c = r.regex.charCodeAt(i);
    if (c >= 0xD800 && c <= 0xDBFF) {
      const n = r.regex.charCodeAt(i + 1);
      if (n >= 0xDC00 && n <= 0xDFFF) { i++; continue; }
      lone++;
    } else if (c >= 0xDC00 && c <= 0xDFFF) lone++;
  }
}

fs.writeFileSync('grammars.json', JSON.stringify(g));
console.log(`\\uXXXX -> \\x{XXXX}: ${uFixed}`);
console.log(`surrogate rewrites: ${touched}`);
console.log(`lone surrogates remaining: ${lone}`);
