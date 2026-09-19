#!/usr/bin/env node
// InkAgent local evals. Default mode is fully offline: an in-process relay, a
// scripted mock provider, and the device simulator client. `--live` swaps in a
// real OpenAI-compatible endpoint (INK_EVAL_BASE_URL, INK_EVAL_KEY, INK_EVAL_MODEL)
// and runs the same rubric against real answers.
import { readFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { createApp } from '../relay/src/server.js';
import { DeviceClient, layoutForPanel } from '../device-sim/client.js';

// The rig builds its own throwaway in-memory relay and has to create an account
// on it. INK_SIGNUP_CLOSED is an operator setting for a deployed relay — the
// README tells you to set it once your account exists — and inheriting it here
// would close signup on a database that has no accounts at all, so the evals
// would fail on exactly the machines that follow the deployment advice.
delete process.env.INK_SIGNUP_CLOSED;

const LIVE = process.argv.includes('--live');
const VERBOSE = process.argv.includes('-v');
const BUDGET = 1536, MAX_PAGES = 2, MAX_WORDS = Math.floor(BUDGET / 6);
const LATENCY_P95_MS = LIVE ? 15000 : 500;

const dataset = readFileSync(new URL('./datasets/ask.jsonl', import.meta.url), 'utf8').trim().split('\n').map(JSON.parse)
  .map(d => d.text === 'LONGFILL' ? { ...d, text: ('The committee met again on Tuesday and argued about the bridge. ').repeat(28).slice(0, 1795) } : d);

const results = []; // { suite, name, pass, detail }
let quiet = false;
const check = (suite, name, pass, detail = '') => { results.push({ suite, name, pass: !!pass, detail }); if (!quiet && (VERBOSE || !pass)) console.log(`${pass ? '  ok ' : '  FAIL'} [${suite}] ${name}${detail ? ' — ' + detail : ''}`); };

// ---------- rig ----------
async function mockProvider(behaviour) {
  const srv = createServer(async (req, res) => {
    let raw = ''; for await (const c of req) raw += c;
    const body = JSON.parse(raw);
    const r = behaviour(body);
    if (r.delayMs) await new Promise(t => setTimeout(t, r.delayMs));
    if (r.status && r.status !== 200) { res.writeHead(r.status); return res.end('{"error":"mock"}'); }
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify({ model: body.model, choices: [{ message: { role: 'assistant', content: r.text } }] }));
  });
  await new Promise(r => srv.listen(0, r));
  return { url: `http://127.0.0.1:${srv.address().port}/v1`, close: () => { srv.closeAllConnections(); srv.close(); } };
}
async function relay() {
  const app = createApp({ dbPath: ':memory:', publicUrl: 'http://relay.eval' });
  await new Promise(r => app.server.listen(0, r));
  const base = `http://127.0.0.1:${app.server.address().port}`;
  // Sign-up, not sign-in: the relay takes an email and a scrypt-hashed password
  // (routes/dashboard.js 'POST /signup'), and this rig starts on an empty
  // in-memory database, so there is no account to sign in to yet. Signing up
  // sets the session cookie in the same response a login would have.
  const login = async (email, password = 'eval-password') => {
    const r = await fetch(base + '/signup', { method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams({ email, password }), redirect: 'manual' });
    const cookie = r.headers.get('set-cookie');
    if (!cookie) throw new Error(`relay refused the eval account (${r.status}): ${(await r.text()).replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim().slice(0, 160)}`);
    return cookie.split(';')[0];
  };
  const form = (cookie, path, fields) => fetch(base + path, { method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded', cookie }, body: new URLSearchParams(fields), redirect: 'manual' });
  return { app, base, login, form, close: () => { app.server.closeAllConnections(); app.server.close(); } };
}
// Scripted model: answers from the dataset by matching the passage.
const scripted = (body) => {
  const user = body.messages.find(m => m.role === 'user').content;
  const sys = body.messages.find(m => m.role === 'system').content;
  const candidates = dataset.filter(d => user.includes(d.text.slice(0, 60)));
  // Several items share a passage; disambiguate on the translate argument and the kind prompt.
  const hit = candidates.find(d => d.arg && user.includes(`Argument: ${d.arg}`))
           || candidates.find(d => !d.arg && sys.includes(KIND_HINT[d.kind]));
  return { text: hit ? hit.mock_answer : 'No scripted answer.' };
};
const KIND_HINT = { explain: 'Explain the passage', summary: 'Summarise what has happened', who: 'Identify the characters', translate: 'Translate the passage', define: 'Define the given word' };

// ---------- rubric ----------
const hasJapanese = (s) => /[぀-ヿ一-鿿]/.test(s);
const preamble = /^(sure|certainly|of course|here is|here's|great question|as an ai)/i;
function rubric(d, r) {
  const bytes = Buffer.byteLength(r.text);
  const { pages } = layoutForPanel(r.text);
  const words = r.text.split(/\s+/).filter(Boolean).length;
  check('fit', `${d.id}: answer within ${BUDGET} B`, bytes <= BUDGET, `${bytes} B`);
  check('fit', `${d.id}: at most ${MAX_PAGES} panel pages`, pages <= MAX_PAGES, `${pages} pages`);
  check('fit', `${d.id}: no markdown marks`, !/[*#`]|^\s*[-•] /m.test(r.text));
  check('fit', `${d.id}: no chat preamble`, !preamble.test(r.text.trim()), r.text.slice(0, 40));
  check('fit', `${d.id}: ends on punctuation or is flagged truncated`, /[.!?。」…]$/.test(r.text.trim()) || r.trunc, JSON.stringify(r.text.slice(-12)));
  if (!LIVE || d.kind !== 'translate') check('quality', `${d.id}: within ${MAX_WORDS} words`, words <= MAX_WORDS, `${words} words`);
  const lower = r.text.toLowerCase();
  if (d.must_mention_any) check('quality', `${d.id}: mentions one of ${d.must_mention_any.join('/')}`, d.must_mention_any.some(k => lower.includes(k.toLowerCase())));
  if (d.must_mention_all) check('quality', `${d.id}: mentions all of ${d.must_mention_all.join(',')}`, d.must_mention_all.every(k => lower.includes(k.toLowerCase())));
  if (d.must_not_mention) { const leaked = d.must_not_mention.filter(k => lower.includes(k.toLowerCase())); check('quality', `${d.id}: no spoilers / echo`, leaked.length === 0, leaked.join(',')); }
  if (d.script === 'japanese') check('quality', `${d.id}: answer is in Japanese script`, hasJapanese(r.text));
}

// ---------- suites ----------
async function main() {
  const R = await relay();
  const cookie = await R.login('eval@example.com');
  const dev = new DeviceClient({ relay: R.base, hw: 'e0e0e0e0e0e0', budget: BUDGET });

  // E1: device pairing flow exactly as the firmware does it
  const p = await dev.pairStart();
  check('pairing', 'user code is e-ink friendly', /^[A-HJ-NP-Z2-9]{4}-[A-HJ-NP-Z2-9]{4}$/.test(p.user_code), p.user_code);
  check('pairing', 'QR payload fits a version-6 QR (≤ 134 bytes alnum-ish)', p.verify_url_complete.length <= 134, `${p.verify_url_complete.length} chars`);
  check('pairing', 'poll is pending before claim', (await dev.pairPoll(p.device_code)).status === 'pending');
  await R.form(cookie, '/claim', { code: p.user_code, name: 'Eval X3' });
  const ok = await dev.pairPoll(p.device_code);
  check('pairing', 'poll yields token after claim', ok.status === 'ok' && ok.device_token?.length > 30);
  dev.token = ok.device_token;
  check('pairing', 'second poll cannot replay the token', (await dev.pairPoll(p.device_code)).status !== 'ok');

  // E2: no provider yet → device still gets renderable text
  const np = await dev.ask({ kind: 'explain', text: 'x y z' });
  check('resilience', 'no provider → 402 with screen text', np.noProvider && np.text.length > 0 && layoutForPanel(np.text).pages === 1, np.text);

  // E3: answers — scripted or live
  let P = null;
  if (LIVE) {
    const { INK_EVAL_BASE_URL, INK_EVAL_KEY, INK_EVAL_MODEL } = process.env;
    if (!INK_EVAL_BASE_URL || !INK_EVAL_MODEL) { console.error('live mode needs INK_EVAL_BASE_URL and INK_EVAL_MODEL (INK_EVAL_KEY optional)'); process.exit(2); }
    await R.form(cookie, '/provider', { kind: 'custom', base_url: INK_EVAL_BASE_URL, model: INK_EVAL_MODEL, api_key: INK_EVAL_KEY || '' });
    console.log(`live: ${INK_EVAL_MODEL} @ ${INK_EVAL_BASE_URL}`);
  } else {
    P = await mockProvider(scripted);
    await R.form(cookie, '/provider', { kind: 'custom', base_url: P.url, model: 'scripted', api_key: 'k' });
  }
  const latencies = [];
  for (const d of dataset) {
    const req = { kind: d.kind, book: d.book, author: d.author, chapter: d.chapter, pct: d.pct, text: d.text };
    if (d.arg) req.arg = d.arg;
    const t0 = Date.now();
    const r = await dev.ask(req);
    latencies.push(Date.now() - t0);
    check('protocol', `${d.id}: HTTP 200 with sid`, r.ok && !!r.sid, `HTTP ${r.status}`);
    check('protocol', `${d.id}: wire response ≤ budget + 512 B (device heap ceiling)`, r.bytes <= BUDGET + 512, `${r.bytes} B`);
    if (r.ok) rubric(d, r);
    if (VERBOSE) console.log('      ' + r.text.replace(/\n/g, ' ').slice(0, 110));
  }
  const sorted = [...latencies].sort((a, b) => a - b);
  const p50 = sorted[Math.floor(sorted.length / 2)], p95 = sorted[Math.floor(sorted.length * 0.95)];
  check('latency', `p95 ≤ ${LATENCY_P95_MS} ms`, p95 <= LATENCY_P95_MS, `p50 ${p50} ms · p95 ${p95} ms`);
  if (P) P.close();

  // E4: relay-side trimming when the model ignores the length instruction
  const big = await mockProvider(() => ({ text: Array.from({ length: 120 }, (_, i) => `Sentence ${i} about the passage that keeps going.`).join(' ') }));
  await R.form(cookie, '/provider', { kind: 'custom', base_url: big.url, model: 'verbose', api_key: 'k' });
  const vb = await dev.ask({ kind: 'summary', text: dataset[0].text });
  check('fit', 'verbose model is cut at a sentence and flagged', vb.ok && vb.trunc && Buffer.byteLength(vb.text) <= BUDGET && /\.$/.test(vb.text), `${Buffer.byteLength(vb.text)} B`);
  check('fit', 'cut answer still ≤ 2 pages', layoutForPanel(vb.text).pages <= MAX_PAGES);
  big.close();

  // E5: provider failures never crash the device path
  for (const [label, behaviour, expect] of [
    ['provider 500', () => ({ status: 500 }), (r) => r.status === 503 && /busy|unreachable/i.test(r.text)],
    ['provider 429', () => ({ status: 429 }), (r) => r.status === 503],
    ['provider 401 (bad key)', () => ({ status: 401 }), (r) => r.status === 402 && /key/i.test(r.text)],
    ['provider timeout', () => ({ delayMs: 400, text: 'late' }), (r) => r.status === 503 && r.text.length > 0],
  ]) {
    process.env.INK_PROVIDER_TIMEOUT_MS = '150';
    const F = await mockProvider(behaviour);
    await R.form(cookie, '/provider', { kind: 'custom', base_url: F.url, model: 'm', api_key: 'k' });
    const r = await dev.ask({ kind: 'explain', text: 'abc' });
    check('resilience', `${label} → friendly text, one page`, expect(r) && layoutForPanel(r.text).pages === 1, `HTTP ${r.status}: ${r.text}`);
    F.close();
  }
  delete process.env.INK_PROVIDER_TIMEOUT_MS;

  // E6: revoke from dashboard → device wipes token on next call
  const devId = R.app.db.prepare(`SELECT id FROM devices WHERE hw = 'e0e0e0e0e0e0'`).get().id;
  await R.form(cookie, '/devices/revoke', { id: devId });
  const rv = await dev.ask({ kind: 'explain', text: 'abc' });
  check('resilience', 'revoked token → 401 revoked with screen text', rv.revoked && rv.text.length > 0, rv.text);

  // E7: rubric self-test — a deliberately bad model must fail the quality checks
  const before = results.length;
  const bad = { id: 'selftest', kind: 'explain', must_mention_any: ['Darcy'], must_not_mention: ['proposal'] };
  quiet = true;
  rubric(bad, { text: 'Sure! Here is **bold** markdown about the proposal', trunc: false });
  quiet = false;
  const selfFails = results.slice(before).filter(r => !r.pass).length;
  results.splice(before); // do not count the self-test rows themselves
  check('meta', 'rubric catches a bad answer (self-test)', selfFails >= 4, `${selfFails} checks tripped`);

  R.close();

  // ---------- report ----------
  const suites = [...new Set(results.map(r => r.suite))];
  console.log('\nSuite        Pass  Fail');
  for (const s of suites) { const rs = results.filter(r => r.suite === s); console.log(`${s.padEnd(12)} ${String(rs.filter(r => r.pass).length).padStart(4)}  ${String(rs.filter(r => !r.pass).length).padStart(4)}`); }
  const failed = results.filter(r => !r.pass).length;
  console.log(`\n${results.length - failed}/${results.length} checks passed${LIVE ? ' (live)' : ' (offline)'}`);
  process.exit(failed ? 1 : 0);
}
main().catch(e => { console.error(e); process.exit(1); });
