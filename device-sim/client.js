// Protocol client that behaves like the X3 firmware: same endpoints, same
// headers, same budget handling. Shared by the CLI simulator and the evals.
export class DeviceClient {
  constructor({ relay, hw = 'aabbccddeeff', fw = 'sim-0.1', budget = 1536, fetchImpl = fetch }) {
    Object.assign(this, { relay: relay.replace(/\/+$/, ''), hw, fw, budget, fetchImpl, token: null });
  }
  async post(path, body, withToken) {
    const headers = { 'content-type': 'application/json', 'x-ink-proto': '1' };
    if (withToken && this.token) headers['x-ink-device'] = this.token;
    const res = await this.fetchImpl(this.relay + path, { method: 'POST', headers, body: JSON.stringify(body) });
    const text = await res.text(); let json = null; try { json = JSON.parse(text); } catch {}
    return { status: res.status, json, bytes: Buffer.byteLength(text) };
  }
  async pairStart() {
    const r = await this.post('/v1/pair/start', { hw: this.hw, fw: this.fw, budget: this.budget }, false);
    if (r.status !== 200) throw new Error(`pair/start ${r.status}`);
    return r.json;
  }
  async pairPoll(deviceCode) {
    const r = await this.post('/v1/pair/poll', { device_code: deviceCode }, false);
    return r.json?.status ? r.json : { status: 'error', http: r.status };
  }
  // Mirrors AskBookActivity: returns { ok, text, sid, trunc, revoked, noProvider, status, bytes }
  async ask(req) {
    const r = await this.post('/v1/ask', req, true);
    const j = r.json || {};
    const text = typeof j.text === 'string' ? j.text : (r.status <= 0 ? 'No connection to the relay.' : 'The relay returned an error.');
    // Device-side hard cut, as InkAgentProtocol::jsonGetString does
    const enc = new TextEncoder(); let out = text, cutLocally = false;
    if (enc.encode(text).length > this.budget) {
      cutLocally = true; let bytes = 0, i = 0;
      for (const ch of text) { const n = enc.encode(ch).length; if (bytes + n > this.budget) break; bytes += n; i += ch.length; }
      out = text.slice(0, i);
    }
    return { ok: r.status >= 200 && r.status < 300 && !!text, text: out, sid: j.sid, trunc: !!j.trunc || cutLocally,
             revoked: r.status === 401 && j.error === 'revoked', noProvider: r.status === 402, status: r.status, bytes: r.bytes };
  }
}

// X3 panel is 480×800 px portrait. At the default reader font that is
// roughly 38 columns × 28 rows (~1060 chars) per page. Wrap like the firmware
// does (word wrap, hard-split overlong words) and report pages.
export const PANEL_COLS = 38, PANEL_ROWS = 28;
export function layoutForPanel(text, cols = PANEL_COLS, rows = PANEL_ROWS) {
  const lines = [];
  for (const para of text.split('\n')) {
    let line = '';
    for (const word of para.split(' ')) {
      if (!word) continue;
      if (word.length > cols) { if (line) { lines.push(line); line = ''; } for (let i = 0; i < word.length; i += cols) lines.push(word.slice(i, i + cols)); continue; }
      if ((line + ' ' + word).trim().length > cols) { lines.push(line); line = word; } else line = (line + ' ' + word).trim();
    }
    lines.push(line);
  }
  return { lines, pages: Math.max(1, Math.ceil(lines.length / rows)) };
}

export function renderPanel(lines, cols = PANEL_COLS, rows = PANEL_ROWS) {
  const top = '┌' + '─'.repeat(cols + 2) + '┐', bottom = '└' + '─'.repeat(cols + 2) + '┘';
  const body = lines.slice(0, rows).map(l => '│ ' + l.padEnd(cols) + ' │');
  while (body.length < rows) body.push('│ ' + ' '.repeat(cols) + ' │');
  return [top, ...body, bottom].join('\n');
}
