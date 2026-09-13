import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startRelay } from './helpers.js';

let R;
before(async () => { R = await startRelay(); });
after(() => R.close());

test('pair/start returns a user code the e-ink can show and a complete verify URL', async () => {
  const r = await R.api('POST', '/v1/pair/start', { hw: 'AABBCCDDEEFF', fw: '0.1', budget: 1536 });
  assert.equal(r.status, 200);
  assert.match(r.json.user_code, /^[A-HJ-NP-Z2-9]{4}-[A-HJ-NP-Z2-9]{4}$/, 'no 0/O/1/I ambiguity');
  assert.equal(r.json.verify_url_complete, `http://relay.test/claim?code=${r.json.user_code}`);
  assert.ok(r.json.device_code.length >= 24);
  assert.equal(r.json.interval, 5);
});

test('pair/start rejects a bad hardware id and clamps the budget', async () => {
  assert.equal((await R.api('POST', '/v1/pair/start', { hw: 'nope' })).status, 400);
  const r = await R.api('POST', '/v1/pair/start', { hw: '000000000001', budget: 10 });
  const cookie = await R.login('clamp@example.com');
  await R.form(cookie, '/claim', { code: r.json.user_code });
  const poll = await R.api('POST', '/v1/pair/poll', { device_code: r.json.device_code });
  const cfg = await R.api('GET', '/v1/config', null, { 'x-ink-device': poll.json.device_token });
  assert.equal(cfg.json.budget, 256);
});

test('poll is pending until the browser claims, then hands out the token exactly once', async () => {
  const cookie = await R.login();
  const start = await R.api('POST', '/v1/pair/start', { hw: 'aabbccddeeff', fw: '0.1' });
  const p1 = await R.api('POST', '/v1/pair/poll', { device_code: start.json.device_code });
  assert.deepEqual(p1.json, { status: 'pending', interval: 5 });

  const claim = await R.form(cookie, '/claim', { code: start.json.user_code.toLowerCase(), name: 'Bedside' });
  assert.equal(claim.status, 200);
  assert.match(claim.text, /connect your ai/i, 'first-run nudge to add a provider');

  const p2 = await R.api('POST', '/v1/pair/poll', { device_code: start.json.device_code });
  assert.equal(p2.json.status, 'ok');
  assert.equal(p2.json.owner, 'ash@example.com');
  assert.ok(p2.json.device_token.length > 30);

  const p3 = await R.api('POST', '/v1/pair/poll', { device_code: start.json.device_code });
  assert.equal(p3.status, 400, 'a consumed code never yields a second token');
});

test('claiming an unknown or already-used code fails cleanly', async () => {
  const cookie = await R.login();
  const r = await R.form(cookie, '/claim', { code: 'ZZZZ-ZZZZ' });
  assert.equal(r.status, 400);
  assert.match(r.text, /not valid/);
});

test('claim page requires sign-in and keeps the code through the redirect', async () => {
  const res = await fetch(R.base + '/claim?code=ABCD-EFGH', { redirect: 'manual' });
  assert.equal(res.status, 302);
  assert.equal(res.headers.get('location'), '/login?next=%2Fclaim%3Fcode%3DABCD-EFGH');
});

test('expired pairing tells the device to restart', async () => {
  const start = await R.api('POST', '/v1/pair/start', { hw: 'aabbccddeeff' });
  R.app.db.prepare(`UPDATE pairings SET expires_at = 1 WHERE device_code = ?`).run(start.json.device_code);
  const p = await R.api('POST', '/v1/pair/poll', { device_code: start.json.device_code });
  assert.equal(p.status, 400); assert.equal(p.json.status, 'expired');
  const cookie = await R.login();
  const claim = await R.form(cookie, '/claim', { code: start.json.user_code });
  assert.equal(claim.status, 400);
});

test('revoking from the dashboard makes the device token 401 revoked', async () => {
  const cookie = await R.login('rev@example.com');
  const { token } = await R.pairDevice(cookie, { hw: '112233445566' });
  assert.equal((await R.api('GET', '/v1/config', null, { 'x-ink-device': token })).status, 200);
  const devId = R.app.db.prepare(`SELECT id FROM devices WHERE hw = '112233445566'`).get().id;
  await R.form(cookie, '/devices/revoke', { id: devId });
  const r = await R.api('GET', '/v1/config', null, { 'x-ink-device': token });
  assert.equal(r.status, 401); assert.equal(r.json.error, 'revoked');
  assert.ok(r.json.text, 'error carries screen text');
});

test('a user cannot revoke another user\'s reader', async () => {
  const a = await R.login('a@example.com'), b = await R.login('b@example.com');
  const { token } = await R.pairDevice(a, { hw: 'a1a1a1a1a1a1' });
  const devId = R.app.db.prepare(`SELECT id FROM devices WHERE hw = 'a1a1a1a1a1a1'`).get().id;
  await R.form(b, '/devices/revoke', { id: devId });
  assert.equal((await R.api('GET', '/v1/config', null, { 'x-ink-device': token })).status, 200);
});
