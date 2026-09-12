// Cut text to a byte budget at a UTF-8 boundary, preferring a sentence end.
// Returns { text, truncated }. Also normalises whitespace for a 30-column panel.
const ELLIPSIS = '…', ELLIPSIS_BYTES = 3;

export function normalise(text) {
  return String(text ?? '')
    .replace(/\r\n?/g, '\n')
    .replace(/[ \t]+/g, ' ')
    .replace(/ ?\n ?/g, '\n')
    .replace(/\n{3,}/g, '\n\n')
    // Markdown does not render on e-ink. Strip emphasis, code ticks, headings
    // and quote marks but leave underscores inside words (snake_case, URLs).
    .replace(/\*+|`+/g, '')
    .replace(/(^|[\s(])_+(?=\S)/g, '$1')
    .replace(/(?<=\S)_+(?=[\s).,;:!?]|$)/g, '')
    .replace(/^#{1,6}\s+/gm, '')
    .replace(/^>\s?/gm, '')
    .trim();
}

export function trimToBudget(text, budget) {
  const enc = new TextEncoder();
  const clean = normalise(text);
  if (enc.encode(clean).length <= budget) return { text: clean, truncated: false };

  // Walk code points, accumulating bytes, remembering the last sentence end
  // that still fits in full and the last code point that fits with an ellipsis.
  let bytes = 0, hardCut = 0, sentenceCut = 0, i = 0;
  for (const ch of clean) {
    const len = enc.encode(ch).length;
    if (bytes + len > budget) break;
    bytes += len;
    i += ch.length;
    if (bytes + ELLIPSIS_BYTES <= budget) hardCut = i;
    if ('.!?'.includes(ch)) sentenceCut = i;
  }
  if (sentenceCut > budget * 0.6) return { text: clean.slice(0, sentenceCut).trimEnd(), truncated: true };
  return { text: clean.slice(0, hardCut).trimEnd() + ELLIPSIS, truncated: true };
}
