import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startRelay } from './helpers.js';
import { validateManifest } from '../src/manifest.js';

let R, cookie, token;
before(async () => {
  R = await startRelay();
  cookie = await R.login('apps@example.com');
  ({ token } = await R.pairDevice(cookie, { hw: 'a9a9a9a9a9a9' }));
});
after(() => R.close());

const GOOD = JSON.stringify({
  name: 'Status', icon: 'info', title: 'Status',
  rows: [{ kind: 'kv', label: 'Reading', value: { src: 'reading.title' } }],
});

test('a manifest that branches is refused: the format has no control flow', () => {
  const r = validateManifest(JSON.stringify({
    name: 'Sneaky', rows: [{ kind: 'text', text: 'hi', if: 'battery < 20' }],
  }));
  assert.equal(r.ok, false);
  assert.match(r.errors.join(' '), /do not branch or loop/);
});

test('a source the reader does not provide is named, not silently dropped', () => {
  const r = validateManifest(JSON.stringify({
    name: 'Weather', rows: [{ kind: 'text', text: { src: 'weather.today' } }],
  }));
  assert.equal(r.ok, false);
  assert.match(r.errors.join(' '), /weather\.today.*does not provide/);
});

test('an app with no name is refused', () => {
  const r = validateManifest(JSON.stringify({ rows: [] }));
  assert.equal(r.ok, false);
  assert.match(r.errors.join(' '), /name is required/);
});

test('limits that the device enforces silently are reported here instead', () => {
  const rows = Array.from({ length: 20 }, () => ({ kind: 'rule' }));
  const r = validateManifest(JSON.stringify({ name: 'Long', rows }));
  assert.equal(r.ok, false);
  assert.match(r.errors.join(' '), /beyond the first 16 are dropped/);
});

test('bad JSON says what is wrong with it', () => {
  const r = validateManifest('{"name": "Oops",}');
  assert.equal(r.ok, false);
  assert.match(r.errors[0], /Not valid JSON/);
});

test('a good manifest validates and yields tile metadata', () => {
  const r = validateManifest(GOOD);
  assert.equal(r.ok, true, r.errors.join('; '));
  assert.deepEqual(r.meta, { name: 'Status', icon: 'info' });
});

test('saving an app makes it appear on the owner\'s readers', async () => {
  const post = await R.form(cookie, '/apps', { manifest: GOOD });
  assert.equal(post.status, 302, 'a good save redirects back to the list');
  const r = await R.api('GET', '/v1/apps', null, { 'x-ink-device': token });
  assert.equal(r.status, 200);
  assert.equal(r.json.apps.length, 1);
  assert.equal(r.json.apps[0].name, 'Status');
  assert.equal(r.json.apps[0].icon, 'info');
  assert.ok(!('manifest' in r.json.apps[0]), 'the index carries no manifests: they are fetched one at a time');
  assert.ok(r.json.version, 'a version stamp so the device can skip an unchanged set');

  const one = await R.api('GET', `/v1/apps?id=${r.json.apps[0].id}`, null, { 'x-ink-device': token });
  assert.equal(one.status, 200);
  assert.equal(one.json.title, 'Status', 'the manifest is served as itself, ready to stream to the card');
});

test('a manifest belonging to someone else is not served by id', async () => {
  const mine = (await R.api('GET', '/v1/apps', null, { 'x-ink-device': token })).json.apps[0];
  const other = await R.login('thief@example.com');
  const { token: otherToken } = await R.pairDevice(other, { hw: 'c9c9c9c9c9c9' });
  const r = await R.api('GET', `/v1/apps?id=${mine.id}`, null, { 'x-ink-device': otherToken });
  assert.equal(r.status, 404);
});

test('the version stamp changes when the set changes, so a device can skip writes', async () => {
  const before = (await R.api('GET', '/v1/apps', null, { 'x-ink-device': token })).json.version;
  const again = (await R.api('GET', '/v1/apps', null, { 'x-ink-device': token })).json.version;
  assert.equal(before, again, 'stable while nothing changed');

  await R.form(cookie, '/apps', { manifest: JSON.stringify({ name: 'Second', rows: [{ kind: 'rule' }] }) });
  const after = (await R.api('GET', '/v1/apps', null, { 'x-ink-device': token })).json.version;
  assert.notEqual(before, after);
});

test('a rejected manifest is not saved and comes back as typed', async () => {
  const typed = JSON.stringify({ name: 'Broken', rows: [{ kind: 'text', text: { src: 'nope.nope' } }] });
  const beforeCount = (await R.api('GET', '/v1/apps', null, { 'x-ink-device': token })).json.apps.length;
  const post = await R.form(cookie, '/apps', { manifest: typed });
  assert.equal(post.status, 400);
  assert.match(post.text, /nope\.nope/, 'the error names the problem');
  assert.match(post.text, /Broken/, 'the manifest comes back in the form');
  const afterCount = (await R.api('GET', '/v1/apps', null, { 'x-ink-device': token })).json.apps.length;
  assert.equal(afterCount, beforeCount, 'nothing saved');
});

test('one owner cannot see or serve another owner\'s apps', async () => {
  const other = await R.login('other@example.com');
  const { token: otherToken } = await R.pairDevice(other, { hw: 'b9b9b9b9b9b9' });
  const r = await R.api('GET', '/v1/apps', null, { 'x-ink-device': otherToken });
  assert.equal(r.status, 200);
  assert.equal(r.json.apps.length, 0);
});

test('an unpaired reader gets no apps', async () => {
  const r = await R.api('GET', '/v1/apps', null, { 'x-ink-device': 'nope' });
  assert.equal(r.status, 401);
});
