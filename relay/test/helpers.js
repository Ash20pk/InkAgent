// Shared test rig: in-memory relay plus a scriptable mock OpenAI-compatible provider.
import { createServer } from 'node:http';
import { createApp } from '../src/server.js';

export async function startMockProvider(script = {}) {
  // script.reply(messages) -> string | script.status -> number | script.delayMs
  const calls = [];
  const srv = createServer(async (req, res) => {
    let raw = ''; for await (const c of req) raw += c;
    const body = JSON.parse(raw);
    calls.push({ url: req.url, auth: req.headers.authorization, body });
    if (script.delayMs) await new Promise(r => setTimeout(r, script.delayMs));
    if (script.status && script.status !== 200) { res.writeHead(script.status); return res.end('{"error":"boom"}'); }
    const text = script.reply ? script.reply(body.messages, body) : 'Mock answer.';
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify({ model: body.model, choices: [{ message: { role: 'assistant', content: text } }], usage: { total_tokens: 42 } }));
  });
  await new Promise(r => srv.listen(0, r));
  return { url: `http://127.0.0.1:${srv.address().port}/v1`, calls, close: () => { srv.closeAllConnections(); srv.close(); }, script };
}

export async function startRelay() {
  const app = createApp({ dbPath: ':memory:', publicUrl: 'http://relay.test' });
  await new Promise(r => app.server.listen(0, r));
  const base = `http://127.0.0.1:${app.server.address().port}`;
  const api = async (method, path, body, headers = {}) => {
    const res = await fetch(base + path, { method, headers: { 'content-type': 'application/json', 'x-ink-proto': '1', ...headers }, body: body ? JSON.stringify(body) : undefined, redirect: 'manual' });
    const text = await res.text();
    let json = null; try { json = JSON.parse(text); } catch {}
    return { status: res.status, json, text, headers: res.headers };
  };
  // Dashboard helpers acting as a signed-in browser
  const login = async (email = 'ash@example.com') => {
    const res = await fetch(base + '/login', { method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams({ email }), redirect: 'manual' });
    return res.headers.get('set-cookie').split(';')[0];
  };
  const form = async (cookie, path, fields) => {
    const res = await fetch(base + path, { method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded', cookie }, body: new URLSearchParams(fields), redirect: 'manual' });
    return { status: res.status, text: await res.text(), location: res.headers.get('location') };
  };
  // Full happy-path pairing: returns a device token
  const pairDevice = async (cookie, { hw = 'aabbccddeeff', budget = 1536 } = {}) => {
    const start = await api('POST', '/v1/pair/start', { hw, fw: 'test', budget });
    const claim = await form(cookie, '/claim', { code: start.json.user_code, name: 'Test X3' });
    if (claim.status !== 200) throw new Error('claim failed ' + claim.text);
    const poll = await api('POST', '/v1/pair/poll', { device_code: start.json.device_code });
    return { token: poll.json.device_token, start: start.json, poll: poll.json };
  };
  const setProvider = (cookie, url, extra = {}) => form(cookie, '/provider', { kind: 'custom', base_url: url, model: 'mock-1', api_key: 'sk-test', ...extra });
  const close = () => { app.server.closeAllConnections(); app.server.close(); };
  return { app, base, api, login, form, pairDevice, setProvider, close };
}

export const PASSAGE = 'Mr. Darcy walked off; and Elizabeth remained with no very cordial feelings towards him. She told the story, however, with great spirit among her friends; for she had a lively, playful disposition, which delighted in anything ridiculous.';
