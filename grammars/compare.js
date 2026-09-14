// node compare.js ref.json out.json
const fs = require('fs');
const ref = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const out = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
const byMode = Object.fromEntries(out.map(r => [r.mode, r]));

let rows = 0, rowsExact = 0, tokens = 0, tokensExact = 0;
const perMode = [];

for (const R of ref) {
  const O = byMode[R.mode];
  if (!O || O.error) { perMode.push([R.mode, 0, R.rows.length, 'missing']); continue; }
  let mRows = 0, mExact = 0, mTok = 0, mTokExact = 0;
  const n = Math.min(R.rows.length, O.rows.length);
  for (let i = 0; i < n; i++) {
    const a = R.rows[i].tokens, b = O.rows[i].tokens;
    mRows++; rows++;
    let same = a.length === b.length;
    const k = Math.min(a.length, b.length);
    for (let j = 0; j < k; j++) {
      mTok++; tokens++;
      if (a[j][0] === b[j][0] && a[j][1] === b[j][1]) { mTokExact++; tokensExact++; }
      else same = false;
    }
    mTok += Math.abs(a.length - b.length);
    tokens += Math.abs(a.length - b.length);
    if (same) { mExact++; rowsExact++; }
  }
  perMode.push([R.mode, mExact, mRows, mTokExact, mTok]);
}

const pct = (a, b) => b ? (100 * a / b).toFixed(1) + '%' : 'n/a';
console.log(`modes compared : ${perMode.length}`);
console.log(`rows identical : ${rowsExact}/${rows}  ${pct(rowsExact, rows)}`);
console.log(`tokens identical: ${tokensExact}/${tokens}  ${pct(tokensExact, tokens)}`);

const perfect = perMode.filter(m => m[2] > 0 && m[1] === m[2]);
console.log(`\nbyte-identical modes: ${perfect.length}/${perMode.length}`);

const worst = perMode.filter(m => m[2] > 0 && m[1] !== m[2])
  .map(m => [m[0], m[1] / m[2], m[1], m[2]])
  .sort((a, b) => a[1] - b[1]);
console.log(`\nimperfect modes (worst 20 of ${worst.length}):`);
for (const [name, r, a, b] of worst.slice(0, 20))
  console.log(`  ${name.padEnd(24)} ${String(a).padStart(4)}/${String(b).padEnd(4)} rows  ${(100*r).toFixed(1)}%`);

// show a concrete first divergence for the worst mode
if (worst.length) {
  const m = worst[0][0];
  const R = ref.find(x => x.mode === m), O = byMode[m];
  for (let i = 0; i < Math.min(R.rows.length, O.rows.length); i++) {
    if (JSON.stringify(R.rows[i].tokens) !== JSON.stringify(O.rows[i].tokens)) {
      console.log(`\nfirst divergence in "${m}" at row ${i}:`);
      console.log('  js : ' + JSON.stringify(R.rows[i].tokens).slice(0, 200));
      console.log('  cpp: ' + JSON.stringify(O.rows[i].tokens).slice(0, 200));
      break;
    }
  }
}
