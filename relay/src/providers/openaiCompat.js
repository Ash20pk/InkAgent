// The one adapter for release one. Covers OpenAI, OpenRouter, Groq, Google's
// compatibility endpoint, Ollama, LM Studio, vLLM and every proxy in between.
export class ProviderError extends Error {
  constructor(message, status, retryable = false) {
    super(message); this.status = status; this.retryable = retryable;
  }
}

export async function chat({ baseUrl, apiKey, model, messages, maxTokens = 400, timeoutMs = 45000, fetchImpl = fetch }) {
  const url = baseUrl.replace(/\/+$/, '') + '/chat/completions';
  const headers = { 'content-type': 'application/json' };
  if (apiKey) headers.authorization = `Bearer ${apiKey}`;
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), timeoutMs);
  let res;
  try {
    res = await fetchImpl(url, {
      method: 'POST', headers, signal: ctrl.signal,
      body: JSON.stringify({ model, messages, max_tokens: maxTokens, temperature: 0.3, stream: false }),
    });
  } catch (e) {
    throw new ProviderError(e.name === 'AbortError' ? 'provider timeout' : `provider unreachable: ${e.message}`, 0, true);
  } finally { clearTimeout(t); }

  if (!res.ok) {
    const retryable = res.status === 429 || res.status >= 500;
    throw new ProviderError(`provider ${res.status}`, res.status, retryable);
  }
  const json = await res.json();
  const text = json?.choices?.[0]?.message?.content;
  if (typeof text !== 'string') throw new ProviderError('provider returned no text', 502, false);
  return { text, model: json.model ?? model, usage: json.usage ?? null };
}
