import { test } from 'node:test';
import assert from 'node:assert/strict';
import { tightenAnswer } from '../src/agent.js';

test('drops a colon-terminated lead-in and blank lines', () => {
  assert.equal(
    tightenAnswer('Hier ist eine Übersetzung ins Deutsche der Passage:\n\nIch erzähle dir von einem Problem.'),
    'Ich erzähle dir von einem Problem.');
});
test('drops a known opener even without a colon', () => {
  assert.equal(tightenAnswer("Here's a summary so far.\n\nThe book opens with two people."),
    'The book opens with two people.');
});
test('joins paragraphs with a single newline, no blank lines', () => {
  assert.equal(tightenAnswer('One idea here.\n\n\nAnother idea here.'), 'One idea here.\nAnother idea here.');
});
test('drops a trailing sign-off', () => {
  assert.equal(tightenAnswer('The real content.\n\nLet me know if you want more detail.'), 'The real content.');
});
test('keeps a single strong paragraph untouched', () => {
  assert.equal(tightenAnswer('Darcy has just snubbed Elizabeth at the ball.'),
    'Darcy has just snubbed Elizabeth at the ball.');
});
test('does not drop the only paragraph even if it looks like a lead-in', () => {
  assert.equal(tightenAnswer('Here is the point.'), 'Here is the point.');
});
