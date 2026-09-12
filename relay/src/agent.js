// The "ask the book" agent. One turn, no tools yet; the loop shape is here so
// briefs and prompt cards slot in without changing the device contract.
import { chat, ProviderError } from './providers/openaiCompat.js';
import { trimToBudget } from './trim.js';

export const KINDS = {
  explain:   { prompt: 'Explain the passage plainly. Assume the reader is mid-book; no spoilers beyond this point.' },
  summary:   { prompt: 'Summarise what has happened in the book up to this passage in a few sentences. No spoilers beyond it.' },
  who:       { prompt: 'Identify the characters referenced in the passage and remind the reader who they are, using only what a reader up to this point would know.' },
  translate: { prompt: 'Translate the passage into the target language. Output only the translation.' },
  define:    { prompt: 'Define the given word as used in this passage: part of speech, one-line meaning, one-line note on the usage here.' },
};

export function buildMessages(req) {
  const k = KINDS[req.kind];
  const where = [req.book && `Book: ${req.book}`, req.author && `Author: ${req.author}`,
                 req.chapter && `Chapter: ${req.chapter}`, Number.isFinite(req.pct) && `Progress: ${req.pct}%`]
                 .filter(Boolean).join('\n');
  const system = [
    'You are a reading companion shown on a 3.7 inch e-ink screen with about 30 characters per line.',
    `Hard limit: answer in at most ${Math.max(40, Math.floor(req.budget / 6))} words.`,
    'Plain text only. No markdown, no headings, no lists, no preamble.',
    k.prompt,
  ].join(' ');
  const user = [where, req.arg ? `Argument: ${req.arg}` : '', `Passage:\n${req.text}`].filter(Boolean).join('\n\n');
  return [{ role: 'system', content: system }, { role: 'user', content: user }];
}

export function validateAsk(body, budget) {
  if (!body || typeof body !== 'object') return 'bad_json';
  if (!KINDS[body.kind]) return 'bad_kind';
  if (typeof body.text !== 'string' || body.text.length === 0) return 'no_text';
  if (Buffer.byteLength(body.text, 'utf8') > 2000) return 'text_too_long';
  if (body.kind === 'translate' && !body.arg) return 'no_language';
  if (body.kind === 'define' && !body.arg) return 'no_word';
  if (!Number.isInteger(budget) || budget < 256 || budget > 8192) return 'bad_budget';
  return null;
}

export async function runAsk({ provider, req, budget, fetchImpl }) {
  const started = Date.now();
  const messages = buildMessages({ ...req, budget });
  const out = await chat({ ...provider, messages, maxTokens: Math.ceil(budget / 3), fetchImpl,
                          timeoutMs: Number(process.env.INK_PROVIDER_TIMEOUT_MS || 45000) });
  const { text, truncated } = trimToBudget(out.text, budget);
  return { text, truncated, full: out.text, model: out.model, latencyMs: Date.now() - started };
}

export { ProviderError };
