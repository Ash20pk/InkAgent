import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startRelay, startMockProvider, PASSAGE } from './helpers.js';

let R, cookie, token;
before(async () => { R = await startRelay(); cookie = await R.login(); ({ token } = await R.pairDevice(cookie)); });
after(() => R.close());
const ask = (body, tok = token) => R.api('POST', '/v1/ask', body, { 'x-ink-device': tok });

test('without a provider the device gets 402 with screen text, not a crash', async () => {
  const r = await ask({ kind: 'explain', text: PASSAGE });
  assert.equal(r.status, 402); assert.equal(r.json.error, 'no_provider'); assert.match(r.json.text, /dashboard/);
});

test('happy path: provider is called with context and the answer comes back within budget', async () => {
  const P = await startMockProvider({ reply: (m) => `Elizabeth finds Darcy's snub funny. ${m[1].content.includes('Chapter: 3') ? 'CTX_OK' : 'CTX_MISSING'}` });
  await R.setProvider(cookie, P.url);
  const r = await ask({ kind: 'explain', book: 'Pride and Prejudice', author: 'Austen', chapter: '3', pct: 8, text: PASSAGE });
  assert.equal(r.status, 200);
  assert.match(r.json.text, /CTX_OK/);
  assert.equal(r.json.trunc, false);
  assert.ok(r.json.sid);
  assert.equal(P.calls[0].auth, 'Bearer sk-test', 'key lives on the relay');
  assert.equal(P.calls[0].body.model, 'mock-1');
  assert.ok(P.calls[0].body.max_tokens <= 600);
  assert.match(P.calls[0].body.messages[0].content, /No markdown/);
  P.close();
});

test('long answers are cut to the device budget at a sentence boundary and flagged', async () => {
  const long = Array.from({ length: 80 }, (_, i) => `Sentence number ${i} says something about the passage.`).join(' ');
  const P = await startMockProvider({ reply: () => long });
  await R.setProvider(cookie, P.url);
  const r = await ask({ kind: 'summary', text: PASSAGE });
  assert.equal(r.status, 200);
  assert.ok(Buffer.byteLength(r.json.text) <= 1536, `got ${Buffer.byteLength(r.json.text)} bytes`);
  assert.equal(r.json.trunc, true);
  assert.match(r.json.text, /\.$/, 'ends on a sentence');
  const turn = R.app.db.prepare(`SELECT * FROM turns WHERE sid = ?`).get(r.json.sid);
  assert.equal(turn.full_text, long, 'full answer kept for the dashboard');
  P.close();
});

test('markdown from the model is stripped because e-ink fonts do not render it', async () => {
  const P = await startMockProvider({ reply: () => '**Darcy** is _proud_.\n\n# Heading\n- item' });
  await R.setProvider(cookie, P.url);
  const r = await ask({ kind: 'who', text: PASSAGE });
  assert.equal(r.json.text, 'Darcy is proud.\n\nHeading\n- item');
  P.close();
});

test('multibyte answers are never cut mid-character', async () => {
  const P = await startMockProvider({ reply: () => '日本語の文章です。'.repeat(200) });
  await R.setProvider(cookie, P.url);
  const r = await ask({ kind: 'translate', arg: 'Japanese', text: PASSAGE });
  assert.ok(Buffer.byteLength(r.json.text) <= 1536);
  assert.ok(!r.json.text.includes('�'));
  assert.equal(Buffer.from(r.json.text).toString(), r.json.text);
  P.close();
});

test('validation: bad kind, empty text, oversize text, translate without language', async () => {
  const P = await startMockProvider(); await R.setProvider(cookie, P.url);
  assert.equal((await ask({ kind: 'roast', text: 'x' })).json.error, 'bad_kind');
  assert.equal((await ask({ kind: 'explain', text: '' })).json.error, 'no_text');
  assert.equal((await ask({ kind: 'explain', text: 'x'.repeat(2001) })).json.error, 'text_too_long');
  assert.equal((await ask({ kind: 'translate', text: 'x' })).json.error, 'no_language');
  assert.equal((await ask({ kind: 'define', text: 'x' })).json.error, 'no_word');
  assert.equal(P.calls.length, 0, 'provider never called for invalid input');
  P.close();
});

test('provider 500 becomes a 503 with friendly screen text and no provider body leaks', async () => {
  const P = await startMockProvider({ status: 500 }); await R.setProvider(cookie, P.url);
  const r = await ask({ kind: 'explain', text: PASSAGE });
  assert.equal(r.status, 503); assert.match(r.json.text, /busy or unreachable/); assert.ok(!r.text.includes('boom'));
  P.close();
});

test('provider 401 tells the user their key is wrong', async () => {
  const P = await startMockProvider({ status: 401 }); await R.setProvider(cookie, P.url);
  const r = await ask({ kind: 'explain', text: PASSAGE });
  assert.equal(r.status, 402); assert.match(r.json.text, /key was rejected/);
  P.close();
});

test('provider unreachable is handled the same way', async () => {
  await R.setProvider(cookie, 'http://127.0.0.1:1/v1');
  const r = await ask({ kind: 'explain', text: PASSAGE });
  assert.equal(r.status, 503); assert.ok(r.json.text);
});

test('every error response carries text the device can render', async () => {
  for (const r of [await ask({ kind: 'nope', text: 'x' }), await ask({ kind: 'explain', text: PASSAGE }, 'bogus')]) {
    assert.equal(typeof r.json.text, 'string'); assert.ok(r.json.text.length > 0);
  }
});

test('traces page shows the turn with the full answer when truncated', async () => {
  const res = await fetch(R.base + '/traces', { headers: { cookie } });
  const html = await res.text();
  assert.match(html, /Sentence number 0/); assert.match(html, /cut to fit/);
});

test('provider page rejects a non-http base URL', async () => {
  const r = await R.form(cookie, '/provider', { kind: 'custom', base_url: 'file:///etc/passwd', model: 'm' });
  assert.equal(r.status, 400);
});
