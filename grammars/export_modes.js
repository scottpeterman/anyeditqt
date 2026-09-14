// Dump ace highlight rules to declarative JSON that a non-JS tokenizer can consume.
//
// normalizeRules() has already lowered `push:`/`pop:`/`include:` into a flat state
// map by the time we see the rules; it leaves behind two generated closures
// (pushState / popState) plus `rule.nextState` as data. We identify those two by
// source and re-encode them as {op:"push"|"pop"|"goto"}. Anything else executable
// is reported, not silently dropped.

require('/home/claude/ace/node_modules/amd-loader');
const fs = require('fs'), path = require('path'), Module = require('module');

global.window = global;
global.navigator = { userAgent: 'Mozilla/5.0 (X11; Linux x86_64)', platform: 'Linux' };
const el = () => ({ style:{}, appendChild(){}, setAttribute(){}, addEventListener(){},
  classList:{add(){},remove(){}}, childNodes:[], getElementsByTagName:()=>[],
  insertBefore(){}, removeChild(){}, cloneNode(){return el();} });
global.document = { createElement:el, createElementNS:el, createTextNode:el, createComment:el,
  documentElement:el(), head:el(), body:el(), addEventListener(){},
  getElementsByTagName:()=>[el()], querySelector:()=>null, createDocumentFragment:el };
global.HTMLElement = function(){}; global.Node = function(){};

const SRC = '/home/claude/ace/src';
const orig = Module._resolveFilename;
Module._resolveFilename = function (r, ...a) {
  if (r.startsWith('ace/')) r = path.join(SRC, r.slice(4));
  return orig.call(this, r, ...a);
};

// createKeywordMapper() returns a closure over a plain word->class table. Tag it
// at the source so the table can be exported as data instead of lost as a closure.
// This is the single biggest win: nearly every real language mode uses it for its
// keyword/builtin lists.
{
  const T = require(path.join(SRC, 'mode/text_highlight_rules.js')).TextHighlightRules;
  const orig = T.prototype.createKeywordMapper;
  T.prototype.createKeywordMapper = function (map, defaultToken, ignoreCase, splitChar) {
    const table = Object.create(null);
    Object.keys(map).forEach(cls => {
      for (const w of String(map[cls]).split(splitChar || '|'))
        table[ignoreCase ? w.toLowerCase() : w] = cls;
    });
    const fn = orig.call(this, map, defaultToken, ignoreCase, splitChar);
    fn.$kwTable = table;
    fn.$kwDefault = defaultToken;
    fn.$kwIgnoreCase = !!ignoreCase;
    return fn;
  };
}

const sig = f => f.toString().replace(/\s+/g, ' ').trim();
// captured from the actual generated closures in mode/text_highlight_rules.js
const PUSH = 'function(currentState, stack) { if (currentState != "start" || stack.length) stack.unshift(this.nextState, currentState); return this.nextState; }';
const POP  = 'function(currentState, stack) { // if (stack[0] === currentState) stack.shift(); return stack.shift() || "start"; }';

function encodeNext(rule, report) {
  const n = rule.next;
  if (n == null) return null;
  if (typeof n === 'string') return { op: 'goto', state: n };
  if (typeof n === 'function') {
    const s = sig(n);
    if (s === PUSH) return { op: 'push', state: rule.nextState };
    if (s === POP)  return { op: 'pop' };
    report.customNext++;
    return null;
  }
  return null;
}

const modeDir = path.join(SRC, 'mode');
const files = fs.readdirSync(modeDir).filter(f => f.endsWith('_highlight_rules.js'));
const out = {}, summary = [];
let loadFail = [];

for (const f of files) {
  const name = f.replace('_highlight_rules.js', '');
  let inst;
  try {
    const m = require(path.join(modeDir, f));
    const k = Object.keys(m).find(k => /HighlightRules$/.test(k));
    if (!k) { loadFail.push([name, 'no export']); continue; }
    inst = new m[k]();
    if (!inst.$rules) { loadFail.push([name, 'no $rules']); continue; }
    if (inst.normalizeRules) inst.normalizeRules();
  } catch (e) { loadFail.push([name, e.message.split('\n')[0]]); continue; }

  const report = { rules: 0, customNext: 0, customToken: 0, keywordMapper: 0, backref: 0, lookahead: 0 };
  const states = {};

  for (const stName in inst.$rules) {
    const src = inst.$rules[stName];
    if (!Array.isArray(src)) continue;
    const dst = [];
    for (const rule of src) {
      const r = {};
      if (rule.defaultToken) r.defaultToken = rule.defaultToken;
      if (rule.regex != null) {
        r.regex = rule.regex instanceof RegExp
          ? rule.regex.toString().slice(1, rule.regex.toString().lastIndexOf('/'))
          : String(rule.regex);
              report.rules++;
        if (/\\[1-9]/.test(r.regex)) report.backref++;
        if (/\(\?[=!]/.test(r.regex)) report.lookahead++;
      }
      if (Array.isArray(rule.token)) r.tokenArray = rule.token.slice();
      else if (typeof rule.token === 'string') r.token = rule.token;
      else if (typeof rule.token === 'function') {
        if (rule.token.$kwTable) {
          r.keywords = rule.token.$kwTable;
          r.keywordDefault = rule.token.$kwDefault;
          if (rule.token.$kwIgnoreCase) r.keywordIgnoreCase = true;
          report.keywordMapper++;
        } else { r.tokenFn = true; report.customToken++; }
      }

      const nx = encodeNext(rule, report);
      if (nx) r.next = nx;
      if (rule.caseInsensitive) r.caseInsensitive = true;
      if (rule.unicode) r.unicode = true;
      if (rule.consumeLineEnd) r.consumeLineEnd = true;
      if (rule.merge === false) r.merge = false;
      if (r.regex != null || r.defaultToken) dst.push(r);
    }
    states[stName] = dst;
  }

  out[name] = states;
  summary.push([name, report]);
}

fs.writeFileSync('/home/claude/acegrammar/grammars.json', JSON.stringify(out));

const tot = summary.reduce((a, [, r]) => {
  for (const k in r) a[k] = (a[k] || 0) + r[k]; return a;
}, {});
const clean = summary.filter(([, r]) => !r.customNext && !r.customToken);
console.log(`modes exported     : ${summary.length}/${files.length}`);
if (loadFail.length) console.log(`load failures      : ${loadFail.map(x => x[0]).join(', ')}`);
console.log(`rules              : ${tot.rules}`);
console.log(`fully declarative  : ${clean.length} modes`);
console.log(`custom next fns    : ${tot.customNext} rules`);
console.log(`custom token fns   : ${tot.customToken} rules`);
console.log(`keyword mappers    : ${tot.keywordMapper} rules (exported as tables)`);
console.log(`backreference regex: ${tot.backref}`);
console.log(`lookahead regex    : ${tot.lookahead}`);
const dirty = summary.filter(([, r]) => r.customNext || r.customToken)
  .sort((a, b) => (b[1].customNext + b[1].customToken) - (a[1].customNext + a[1].customToken));
console.log(`\nmodes needing hand work (top 12):`);
console.log(dirty.slice(0, 12).map(([n, r]) => `  ${n.padEnd(22)} next=${r.customNext} token=${r.customToken}`).join('\n'));
