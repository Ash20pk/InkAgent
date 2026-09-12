#!/usr/bin/env node
// X3 simulator: walks the exact firmware flow against a running relay.
//   node sim.js pair   [--relay http://localhost:8787]
//   node sim.js ask    [--kind explain] [--book ..] [--chapter ..] [--pct 42] [--text "..." | --file passage.txt]
//   node sim.js reset
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { DeviceClient, layoutForPanel, renderPanel } from './client.js';

const args = process.argv.slice(2);
const cmd = args.shift() || 'help';
const opt = (k, d) => { const i = args.indexOf('--' + k); return i >= 0 ? args[i + 1] : d; };
const STATE = new URL('./state.json', import.meta.url);
const state = existsSync(STATE) ? JSON.parse(readFileSync(STATE, 'utf8')) : {};
const relay = opt('relay', state.relay || process.env.INK_RELAY || 'http://localhost:8787');
const dev = new DeviceClient({ relay, hw: state.hw || 'aabbccddeeff', budget: Number(opt('budget', state.budget || 1536)) });
dev.token = state.token || null;
const save = () => writeFileSync(STATE, JSON.stringify({ ...state, relay, hw: dev.hw, budget: dev.budget, token: dev.token }, null, 2));
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

if (cmd === 'pair') {
  const p = await dev.pairStart();
  console.log(renderPanel(['      PAIR YOUR READER', '', '  [QR code: open on phone]', '', `  ${p.verify_url_complete}`.length > 30 ? '  ' + p.verify_url : `  ${p.verify_url_complete}`, '', `        ${p.user_code}`, '', '  Scan, sign in, enter code']));
  console.log(`\nOpen: ${p.verify_url_complete}\n`);
  const deadline = Date.now() + p.expires_in * 1000;
  while (Date.now() < deadline) {
    await sleep(Math.max(3000, p.interval * 1000));
    const r = await dev.pairPoll(p.device_code);
    if (r.status === 'pending' || r.status === 'error') { process.stdout.write('.'); continue; }
    if (r.status === 'ok') { dev.token = r.device_token; save(); console.log(`\nPaired with ${r.owner}. Token stored in state.json.`); process.exit(0); }
    console.log(`\nPairing ${r.status}.`); process.exit(1);
  }
  console.log('\nPairing expired.'); process.exit(1);
} else if (cmd === 'ask') {
  if (!dev.token) { console.error('Not paired. Run: node sim.js pair'); process.exit(2); }
  const text = opt('file') ? readFileSync(opt('file'), 'utf8') : opt('text', 'Mr. Darcy walked off; and Elizabeth remained with no very cordial feelings towards him.');
  const req = { kind: opt('kind', 'explain'), book: opt('book', 'Pride and Prejudice'), author: opt('author', 'Jane Austen'), chapter: opt('chapter', 'Chapter 3'), pct: Number(opt('pct', 8)), text: text.slice(0, 1800) };
  if (opt('arg')) req.arg = opt('arg');
  const t0 = Date.now();
  const r = await dev.ask(req);
  const { lines, pages } = layoutForPanel(r.text);
  console.log(renderPanel([req.kind.toUpperCase().padStart(15 + req.kind.length / 2), '', ...lines]));
  console.log(`\nHTTP ${r.status} · ${r.bytes} B on the wire · ${Buffer.byteLength(r.text)} B shown · ${pages} page(s) · ${Date.now() - t0} ms${r.trunc ? ' · CUT TO FIT' : ''}${r.sid ? ` · sid ${r.sid}` : ''}`);
  if (r.revoked) { dev.token = null; save(); console.log('Token revoked by relay; state cleared. Pair again.'); }
  process.exit(r.ok ? 0 : 1);
} else if (cmd === 'reset') {
  writeFileSync(STATE, '{}'); console.log('state cleared');
} else {
  console.log('usage: node sim.js pair|ask|reset [--relay URL] [--kind explain|summary|who|translate] [--text ..|--file ..] [--arg lang]');
}
