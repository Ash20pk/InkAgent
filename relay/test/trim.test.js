import { test } from 'node:test';
import assert from 'node:assert/strict';
import { trimToBudget, normalise } from '../src/trim.js';

test('short text passes through untouched', () => {
  assert.deepEqual(trimToBudget('Hello.', 100), { text: 'Hello.', truncated: false });
});
test('prefers a sentence boundary when one sits past 60% of the budget', () => {
  const r = trimToBudget('First sentence here. Second sentence is longer than the first. Third.', 66);
  assert.equal(r.text, 'First sentence here. Second sentence is longer than the first.');
  assert.equal(r.truncated, true);
});
test('falls back to a hard cut with ellipsis when no sentence end is usable', () => {
  const r = trimToBudget('a'.repeat(500), 64);
  assert.ok(Buffer.byteLength(r.text) <= 64); assert.ok(r.text.endsWith('…'));
});
test('never splits a multibyte code point', () => {
  const r = trimToBudget('€'.repeat(100), 32);
  assert.ok(Buffer.byteLength(r.text) <= 32); assert.equal(r.text.slice(0, -1).replace(/€/g, ''), '');
});
test('normalise collapses whitespace and strips markdown marks', () => {
  assert.equal(normalise('  **bold**\r\n\r\n\r\n\ttext  '), 'bold\n\ntext');
  assert.equal(normalise('_x_ and snake_case_name and https://a.b/c_d'), 'x and snake_case_name and https://a.b/c_d');
  assert.equal(normalise('> quoted\n## Head\n`code`'), 'quoted\nHead\ncode');
});
