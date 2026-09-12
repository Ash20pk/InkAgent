import { json, readBody } from '../http.js';
import { now, id, sha, userCode } from '../db.js';
import { runAsk, validateAsk, ProviderError } from '../agent.js';

const PAIR_TTL = 600, PAIR_INTERVAL = 5;

export function deviceRoutes(db, { publicUrl, fetchImpl }) {
  const q = {
    pairInsert: db.prepare(`INSERT INTO pairings (device_code,user_code,hw,fw,budget,status,expires_at,created_at) VALUES (?,?,?,?,?,'pending',?,?)`),
    pairGet: db.prepare(`SELECT * FROM pairings WHERE device_code = ?`),
    pairExpire: db.prepare(`UPDATE pairings SET status='expired' WHERE device_code = ?`),
    pairConsume: db.prepare(`UPDATE pairings SET status='consumed' WHERE device_code = ?`),
    deviceByToken: db.prepare(`SELECT * FROM devices WHERE token_hash = ?`),
    touch: db.prepare(`UPDATE devices SET last_seen = ? WHERE id = ?`),
    provider: db.prepare(`SELECT * FROM providers WHERE user_id = ?`),
    turn: db.prepare(`INSERT INTO turns (sid,device_id,kind,request,full_text,sent_text,truncated,model,latency_ms,created_at) VALUES (?,?,?,?,?,?,?,?,?,?)`),
    pendingToken: new Map(), // device_code -> plaintext token, handed out exactly once
  };

  function auth(req, res) {
    const tok = req.headers['x-ink-device'];
    if (!tok) { json(res, 401, { error: 'no_token', text: 'Not paired.' }); return null; }
    const d = q.deviceByToken.get(sha(tok));
    if (!d || d.revoked) { json(res, 401, { error: 'revoked', text: 'This reader was unpaired. Pair it again.' }); return null; }
    q.touch.run(now(), d.id);
    return d;
  }

  return {
    'POST /v1/pair/start': async ({ req, res }) => {
      const b = await readBody(req);
      if (!/^[0-9a-f]{12}$/i.test(b.hw || '')) return json(res, 400, { error: 'bad_hw' });
      const budget = Number.isInteger(b.budget) ? Math.min(8192, Math.max(256, b.budget)) : 1536;
      const device_code = id(24), user_code = userCode(), t = now();
      q.pairInsert.run(device_code, user_code, b.hw.toLowerCase(), String(b.fw || ''), budget, t + PAIR_TTL, t);
      json(res, 200, {
        device_code, user_code,
        verify_url: `${publicUrl}/claim`,
        verify_url_complete: `${publicUrl}/claim?code=${user_code}`,
        interval: PAIR_INTERVAL, expires_in: PAIR_TTL,
      });
    },

    'POST /v1/pair/poll': async ({ req, res }) => {
      const b = await readBody(req);
      const p = q.pairGet.get(String(b.device_code || ''));
      if (!p) return json(res, 400, { status: 'denied' });
      if (p.status === 'pending' && p.expires_at < now()) { q.pairExpire.run(p.device_code); return json(res, 400, { status: 'expired' }); }
      if (p.status === 'pending') return json(res, 200, { status: 'pending', interval: PAIR_INTERVAL });
      if (p.status === 'claimed') {
        const token = q.pendingToken.get(p.device_code);
        q.pendingToken.delete(p.device_code);
        q.pairConsume.run(p.device_code);
        const owner = db.prepare(`SELECT email FROM users WHERE id = ?`).get(p.user_id)?.email;
        return json(res, 200, { status: 'ok', device_token: token, device_id: p.device_id, owner });
      }
      return json(res, 400, { status: p.status === 'expired' ? 'expired' : 'denied' });
    },

    'GET /v1/config': async ({ req, res }) => {
      const d = auth(req, res); if (!d) return;
      const p = q.provider.get(d.user_id);
      json(res, 200, { budget: d.budget, brief_interval_s: 21600, provider: p ? p.kind : null, cards: Object.keys(KINDS_PUBLIC).map(k => ({ id: k, name: KINDS_PUBLIC[k] })) });
    },

    'POST /v1/ask': async ({ req, res }) => {
      const d = auth(req, res); if (!d) return;
      const b = await readBody(req);
      const bad = validateAsk(b, d.budget);
      if (bad) return json(res, 400, { error: bad, text: 'The reader sent a request the relay did not understand.' });
      const p = q.provider.get(d.user_id);
      if (!p) return json(res, 402, { error: 'no_provider', text: 'No AI connected yet. Open the dashboard and add a key or an endpoint.' });
      const sid = id(9);
      try {
        const r = await runAsk({ provider: { baseUrl: p.base_url, apiKey: p.api_key, model: p.model }, req: b, budget: d.budget, fetchImpl });
        q.turn.run(sid, d.id, b.kind, JSON.stringify(b), r.full, r.text, r.truncated ? 1 : 0, r.model, r.latencyMs, now());
        json(res, 200, { text: r.text, sid, trunc: r.truncated });
      } catch (e) {
        if (!(e instanceof ProviderError)) throw e;
        q.turn.run(sid, d.id, b.kind, JSON.stringify(b), '', `ERR ${e.message}`, 0, p.model, 0, now());
        json(res, e.status === 401 || e.status === 403 ? 402 : 503,
             { error: 'provider', sid, text: e.status === 401 || e.status === 403
               ? 'Your AI key was rejected. Check it on the dashboard.'
               : e.retryable ? 'The AI is busy or unreachable. Try again in a moment.' : 'The AI returned nothing useful. Try again.' });
      }
    },

    'GET /v1/brief/next': async ({ req, res }) => {
      const d = auth(req, res); if (!d) return;
      res.writeHead(204); res.end(); // briefs land in release two
    },

    _internal: q,
  };
}

const KINDS_PUBLIC = { explain: 'Explain this', summary: 'Story so far', who: 'Who is this?', translate: 'Translate', define: 'Define word' };
