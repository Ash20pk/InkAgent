import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startRelay, startMockProvider, PASSAGE } from './helpers.js';

let R, cookie, token;
before(async () => { R = await startRelay(); cookie = await R.login('engage@example.com'); ({ token } = await R.pairDevice(cookie, { hw: 'e1e1e1e1e1e1' })); });
after(() => R.close());
const engage = (body, tok = token) => R.api('POST', '/v1/engage', body, { 'x-ink-device': tok });

test('an unpaired reader gets 401, not a screen', async () => {
  const r = await engage({ app: 'recall', text: PASSAGE }, 'not-a-token');
  assert.equal(r.status, 401);
  assert.ok(!r.json.screen, 'no screen on an auth failure');
});

test('without a provider the device gets 402 and keeps whatever it cached', async () => {
  const r = await engage({ app: 'recall', text: PASSAGE });
  assert.equal(r.status, 402);
  assert.equal(r.json.error, 'no_provider');
  assert.ok(!r.json.screen, 'no screen to overwrite the cache with');
});

test('an unknown app is rejected: the device cannot invent one', async () => {
  const P = await startMockProvider({ reply: () => 'What did she notice?' });
  await R.setProvider(cookie, P.url);
  const r = await engage({ app: 'summarise_everything', text: PASSAGE });
  assert.equal(r.status, 400);
  assert.equal(r.json.error, 'bad_app');
  P.close();
});

test('happy path returns a screen in the device manifest vocabulary, not prose', async () => {
  const P = await startMockProvider({ reply: () => 'What did Elizabeth overhear Darcy say about her?' });
  await R.setProvider(cookie, P.url);
  const r = await engage({ app: 'recall', book: 'Pride and Prejudice', author: 'Austen', chapter: '3', pct: 8, text: PASSAGE });
  assert.equal(r.status, 200);
  assert.ok(r.json.sid);
  const s = r.json.screen;
  assert.ok(s && Array.isArray(s.rows), 'screen with rows');
  assert.equal(s.rows.length, 1);
  assert.equal(s.rows[0].kind, 'text');
  assert.equal(s.rows[0].center, true);
  assert.match(s.rows[0].text, /\?$/, 'the row carries a question');
  assert.equal(r.json.text, s.rows[0].text, 'plain text mirrors the row, so the device need not parse the screen');
  assert.ok(!('prompt' in s.rows[0]), 'no fields the firmware parser does not know');
  P.close();
});

test('the agent elicits: the model is told to ask and never to answer', async () => {
  const P = await startMockProvider({ reply: () => 'What changed in her opinion of him?' });
  await R.setProvider(cookie, P.url);
  await engage({ app: 'recall', text: PASSAGE });
  const system = P.calls[0].body.messages[0].content;
  assert.match(system, /Do not answer it/);
  assert.match(system, /never about what happens next/, 'no spoilers past the reader');
  P.close();
});

test('behavioural features steer the question without ever being quoted back', async () => {
  const P = await startMockProvider({ reply: () => 'Which detail did she miss?' });
  await R.setProvider(cookie, P.url);
  const r = await engage({ app: 'recall', text: PASSAGE, features: { regressions: 3, speedPct: 60 } });
  assert.equal(r.status, 200);
  const system = P.calls[0].body.messages[0].content;
  assert.match(system, /went back over this stretch/);
  assert.match(system, /slowed down/);
  // The reader must never see the inference, only its effect.
  assert.doesNotMatch(r.json.screen.rows[0].text, /regression|slowed|pace/i);
  P.close();
});

test('features are optional and a reader who sends none gets no hints', async () => {
  const P = await startMockProvider({ reply: () => 'What did she decide?' });
  await R.setProvider(cookie, P.url);
  await engage({ app: 'recall', text: PASSAGE });
  const system = P.calls[0].body.messages[0].content;
  assert.doesNotMatch(system, /went back over this stretch/);
  assert.doesNotMatch(system, /slowed down/);
  P.close();
});

test('a model that answers instead of asking is refused rather than shown', async () => {
  const P = await startMockProvider({ reply: () => 'Elizabeth overheard Darcy call her tolerable.' });
  await R.setProvider(cookie, P.url);
  const r = await engage({ app: 'recall', text: PASSAGE });
  assert.equal(r.status, 503, 'a statement on an ambient screen reads as fact about the book');
  assert.ok(!r.json.screen);
  P.close();
});

test('the question is trimmed to the device budget', async () => {
  const long = 'Why did ' + 'she consider the matter at length and '.repeat(80) + 'decide?';
  const P = await startMockProvider({ reply: () => long });
  await R.setProvider(cookie, P.url);
  const r = await engage({ app: 'recall', text: PASSAGE });
  assert.equal(r.status, 200);
  assert.ok(Buffer.byteLength(r.json.screen.rows[0].text) <= 1536, 'fits the budget the device declared');
  P.close();
});

test('every turn is recorded for the dashboard, labelled by app', async () => {
  const P = await startMockProvider({ reply: () => 'What did she notice first?' });
  await R.setProvider(cookie, P.url);
  const r = await engage({ app: 'explain_back', text: PASSAGE });
  const turn = R.app.db.prepare(`SELECT * FROM turns WHERE sid = ?`).get(r.json.sid);
  assert.equal(turn.kind, 'engage:explain_back', 'labelled by app');
  assert.equal(turn.sent_text, '', 'the question itself is not stored');
  assert.equal(turn.request, '', 'nor the passage it was about');
  P.close();
});

test('config advertises the engage apps so the device does not hardcode them', async () => {
  const r = await R.api('GET', '/v1/config', null, { 'x-ink-device': token });
  assert.equal(r.status, 200);
  assert.ok(Array.isArray(r.json.engage));
  assert.ok(r.json.engage.includes('recall'));
});
