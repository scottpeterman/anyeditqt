// Ground truth: run ace's real JS tokenizer over spec.json.
// node reference.js spec.json > ref.json
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
const o = Module._resolveFilename;
Module._resolveFilename = function (r, ...a) {
  if (r.startsWith('ace/')) r = path.join(SRC, r.slice(4));
  return o.call(this, r, ...a);
};
const { Tokenizer } = require(path.join(SRC, 'tokenizer.js'));

const spec = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const out = [];
for (const entry of spec) {
  const rec = { mode: entry.mode, rows: [] };
  let tk;
  try {
    const m = require(path.join(SRC, 'mode', entry.mode + '_highlight_rules.js'));
    const k = Object.keys(m).find(k => /HighlightRules$/.test(k));
    const inst = new m[k]();
    if (inst.normalizeRules) inst.normalizeRules();
    tk = new Tokenizer(inst.$rules);
  } catch (e) { rec.error = e.message; out.push(rec); continue; }

  let state = 'start';
  for (const line of entry.lines) {
    let r;
    try { r = tk.getLineTokens(line, state); }
    catch (e) { r = { tokens: [], state: 'start' }; }
    state = r.state;
    rec.rows.push({
      state: state,
      tokens: r.tokens.map(t => [t.type, t.value])
    });
  }
  out.push(rec);
}
process.stdout.write(JSON.stringify(out));
