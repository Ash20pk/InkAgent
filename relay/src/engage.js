// Engage: the agent composes a screen, the device renders it.
//
// The device is too small to hold a model and too slow to care about
// interpreter overhead, so all the thinking happens here and what crosses the
// wire is a screen — rows and text, already sized to the panel the device
// declared.
//
// The model never emits JSON. Small models are unreliable at it, and a
// malformed screen on a device with no error surface is a blank sleep screen
// nobody can debug. The model returns prose; this file wraps it in the
// manifest. That also means the device's format cannot be widened by whatever
// a provider decides to return.
import { chat, ProviderError } from './providers/openaiCompat.js';
import { trimToBudget } from './trim.js';
import { tightenAnswer } from './agent.js';

// What the agent is allowed to do. Every one of these elicits; none of them
// tell. That is not a style preference — a pre-registered trial (Kreijkes et
// al. 2025, n=405) found LLM-alone reading produced worse comprehension and
// worse three-day retention than plain note-taking, while readers preferred
// it. Retrieval practice and self-explanation are what actually hold, so those
// are the only moves on offer.
export const APPS = {
  recall: {
    title: 'From where you stopped',
    prompt: [
      'Ask the reader exactly one short question about the passage that checks whether they followed it.',
      'Do not answer it. Do not hint at the answer. Do not preface it.',
      'One sentence, under 20 words, ending in a question mark.',
      'Ask about something stated in the passage, never about what happens next.',
    ].join(' '),
  },
  explain_back: {
    title: 'In your own words',
    prompt: [
      'Name one specific idea in the passage and ask the reader to put it in their own words.',
      'Do not explain it yourself. One sentence, under 20 words, ending in a question mark.',
    ].join(' '),
  },
};

export function validateEngage(body, budget) {
  if (!body || typeof body !== 'object') return 'bad_json';
  if (!APPS[body.app]) return 'bad_app';
  if (typeof body.text !== 'string' || body.text.length === 0) return 'no_text';
  if (Buffer.byteLength(body.text, 'utf8') > 2000) return 'text_too_long';
  if (!Number.isInteger(budget) || budget < 256 || budget > 8192) return 'bad_budget';
  return null;
}

function buildMessages(req) {
  const app = APPS[req.app];
  const where = [req.book && `Book: ${req.book}`, req.author && `Author: ${req.author}`,
                 req.chapter && `Chapter: ${req.chapter}`,
                 Number.isFinite(req.pct) && `Progress: ${req.pct}%`].filter(Boolean).join('\n');

  // Behavioural features, when the reader sent them. They are hints about how
  // this stretch went, never a verdict to repeat back: the device computes them
  // from page turns alone and the reader never sees a score.
  const f = req.features || {};
  const signals = [];
  if (Number.isFinite(f.regressions) && f.regressions >= 2) {
    signals.push('The reader went back over this stretch more than once, so favour the part most likely to have been missed.');
  }
  if (Number.isFinite(f.speedRatio) && f.speedRatio < 0.8) {
    signals.push('The reader slowed down here relative to their usual pace.');
  }

  const system = [
    'You write for a 3.7 inch e-ink screen, about 30 characters a line.',
    `Hard limit: ${Math.max(20, Math.floor(req.budget / 12))} words.`,
    'Plain text only. No markdown, no headings, no lists, no quotation marks around your output.',
    app.prompt,
    ...signals,
  ].join(' ');

  const user = [where, `Passage:\n${req.text}`].filter(Boolean).join('\n\n');
  return [{ role: 'system', content: system }, { role: 'user', content: user }];
}

// A question the model failed to make a question is worse than nothing on an
// ambient screen: it reads as a statement of fact about the book.
function looksLikeQuestion(text) {
  return /\?\s*$/.test(text.trim());
}

// The screen the device will render, in its own manifest vocabulary. Composed
// here so the wire format stays exactly what the firmware's parser accepts.
export function composeScreen({ app, text }) {
  return {
    title: APPS[app].title,
    rows: [{ kind: 'text', text, center: true }],
  };
}

export async function runEngage({ provider, req, budget, fetchImpl }) {
  const started = Date.now();
  const messages = buildMessages({ ...req, budget });
  const out = await chat({
    ...provider, messages,
    maxTokens: Math.ceil(budget / 3), fetchImpl,
    timeoutMs: Number(process.env.INK_PROVIDER_TIMEOUT_MS || 45000),
  });

  let text = tightenAnswer(out.text).replace(/^["'“‘]|["'”’]$/g, '').trim();
  if (!looksLikeQuestion(text)) {
    throw new ProviderError('not_a_question', 502, true);
  }
  const trimmed = trimToBudget(text, budget);

  return {
    screen: composeScreen({ app: req.app, text: trimmed.text }),
    text: trimmed.text,
    truncated: trimmed.truncated,
    full: out.text,
    model: out.model,
    latencyMs: Date.now() - started,
  };
}

export { ProviderError };
