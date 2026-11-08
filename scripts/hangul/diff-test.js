// NickelOS: checks nickel-hangul.js (patches/app-i18n/chromeos-hangul)
// against libhangul itself: random key strings in every layout, composed
// by both, must give the same text (libhangul's fillers aside), and the key
// counts per syllable must add up.
//   node diff-test.js DIR LHTEST LIBDIR
// DIR holds nickel-hangul.js and nickel-hangul-keyboards.js (the patched
// extension); LHTEST is lhtest.c built against libhangul
// (cmake -DENABLE_UNIT_TEST=OFF, then cc lhtest.c -Ihangul -Lbuild/hangul
// -lhangul), LIBDIR where its libhangul.so is.
const fs = require('fs');
const [dir, lhtest, libdir] = process.argv.slice(2);
const {execFileSync} = require('child_process');
const el = new EventTarget();
global.document = {getElementById: () => el};
global.fetch = async () => ({text: async () => ''});
eval(fs.readFileSync(dir + '/nickel-hangul-keyboards.js', 'utf8') +
     ';global.NICKEL_HANGUL_KEYBOARDS = NICKEL_HANGUL_KEYBOARDS;');
eval(fs.readFileSync(dir + '/nickel-hangul.js', 'utf8'));
let seed = 7;
const rand = (n) => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed % n; };
const letters = 'abcdefghijklmnopqrstuvwxyz';
// libhangul shows a syllable it could not complete as conjoining jamo with
// fillers; the port, as compatibility jamo. Compare them alike.
const compat = new Map();
for (let c = 0x3131; c <= 0x318E; c++) compat.set(String.fromCharCode(c).normalize('NFKC'), String.fromCharCode(c));
const F2I = [0x1100, 0x1101, 0, 0x1102, 0x115C, 0x115D, 0x1103, 0x1105, 0xA964, 0xA968, 0xA969, 0xA96C, 0, 0, 0x111A, 0x1106, 0x1107, 0x1121, 0x1109, 0x110A, 0x110B, 0x110C, 0x110E, 0x110F, 0x1110, 0x1111, 0x1112];
const toCompat = {};
for (let c = 0x11A8; c <= 0x11C2; c++) if (F2I[c - 0x11A8]) toCompat[c] = F2I[c - 0x11A8];
const norm = (str) => [...str].filter((ch) => ch !== '\u115F' && ch !== '\u1160').map((ch) => {
  const code = ch.charCodeAt(0);
  if (compat.has(ch)) return compat.get(ch);
  if (toCompat[code]) return compat.get(String.fromCharCode(toCompat[code])) || ch;
  return ch;
}).join('');
async function ask(layout, text) {
  return new Promise((resolve) => {
    const on = (e) => { el.removeEventListener('message', on); resolve(JSON.parse(e.data)[1][0]); };
    el.addEventListener('message', on);
    el.postMessage(JSON.stringify({keyboard: layout}));
    el.postMessage(JSON.stringify({text: text, num: 0}));
  });
}
(async () => {
  for (const [layout, n] of [['2', 3000], ['39', 3000], ['3f', 3000], ['3s', 3000], ['ahn', 3000], ['ro', 3000]]) {
    const pool = layout === '2' ? letters + 'QWERTOP' :
        (layout === 'ro' ? letters + letters + 'ABCKNX' : NICKEL_HANGUL_KEYBOARDS[layout].keys + letters);
    const inputs = [];
    for (let i = 0; i < n; i++) {
      let s = '';
      const len = 1 + rand(12);
      for (let j = 0; j < len; j++) s += pool[rand(pool.length)];
      inputs.push(s);
    }
    const ref = execFileSync(lhtest, [layout], {input: inputs.join('\n') + '\n',
        env: {LD_LIBRARY_PATH: libdir}}).toString().split('\n');
    let bad = 0, lenBad = 0;
    for (let i = 0; i < n; i++) {
      const [text, units, lengths] = await ask(layout, inputs[i]);
      if (norm(units.join('')) !== norm(ref[i])) {
        if (bad++ < 4) console.log(`  ${layout} ${JSON.stringify(inputs[i])}: js ${units.join('')} lib ${ref[i]}`);
      }
      if (lengths.reduce((a, b) => a + b, 0) !== inputs[i].length || lengths.some((l) => l < 1)) lenBad++;
    }
    console.log(`${layout}: ${n - bad}/${n} same as libhangul; lengths off: ${lenBad}`);
  }
})();
