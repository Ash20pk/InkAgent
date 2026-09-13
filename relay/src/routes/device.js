import { json, readBody } from '../http.js';
import { now, id, sha, userCode } from '../db.js';
import { runAsk, validateAsk, ProviderError } from '../agent.js';
import { runEngage, validateEngage, APPS as ENGAGE_APPS } from '../engage.js';
import { sha as hashOf } from '../db.js';

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
    apps: db.prepare(`SELECT id, name, icon, updated_at FROM apps WHERE user_id = ? ORDER BY name`),
    appManifest: db.prepare(`SELECT manifest FROM apps WHERE id = ? AND user_id = ?`),
    // Content is deliberately not stored. With the traces page gone there is
    // nothing to read it back, and a table holding the passages people are
    // reading is a liability rather than a feature. The row stays for the
    // operational facts — which device, which app, how long, did it truncate —
    // and the content columns are written empty.
    turn: db.prepare(`INSERT INTO turns (sid,device_id,kind,request,full_text,sent_text,truncated,model,latency_ms,created_at) VALUES (?,?,?,'','','',?,?,?,?)`),
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
      json(res, 200, { budget: d.budget, brief_interval_s: 21600, provider: p ? p.kind : null,
                       cards: Object.keys(KINDS_PUBLIC).map(k => ({ id: k, name: KINDS_PUBLIC[k] })),
                       engage: Object.keys(ENGAGE_APPS) });
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
        q.turn.run(sid, d.id, b.kind, r.truncated ? 1 : 0, r.model, r.latencyMs, now());
        json(res, 200, { text: r.text, sid, trunc: r.truncated });
      } catch (e) {
        if (!(e instanceof ProviderError)) throw e;
        q.turn.run(sid, d.id, `${b.kind}:error`, 0, p.model, 0, now());
        json(res, e.status === 401 || e.status === 403 ? 402 : 503,
             { error: 'provider', sid, text: e.status === 401 || e.status === 403
               ? 'Your AI key was rejected. Check it on the dashboard.'
               : e.retryable ? 'The AI is busy or unreachable. Try again in a moment.' : 'The AI returned nothing useful. Try again.' });
      }
    },

    // The agent composes a screen; the device renders it. Same token, same
    // budget contract and the same 401/402 semantics as /v1/ask, so the device
    // has one set of failure paths to understand rather than two.
    'POST /v1/engage': async ({ req, res }) => {
      const d = auth(req, res); if (!d) return;
      const b = await readBody(req);
      const bad = validateEngage(b, d.budget);
      if (bad) return json(res, 400, { error: bad, text: 'The reader sent a request the relay did not understand.' });
      const p = q.provider.get(d.user_id);
      if (!p) return json(res, 402, { error: 'no_provider', text: 'No AI connected yet. Open the dashboard and add a key or an endpoint.' });
      const sid = id(9);
      try {
        const r = await runEngage({ provider: { baseUrl: p.base_url, apiKey: p.api_key, model: p.model }, req: b, budget: d.budget, fetchImpl });
        q.turn.run(sid, d.id, `engage:${b.app}`, r.truncated ? 1 : 0, r.model, r.latencyMs, now());
        // `text` duplicates the row the screen carries, deliberately: the device
        // caches the screen opaquely for the renderer and shows `text` immediately,
        // and parsing a nested array on the device to recover it would be worse.
        json(res, 200, { screen: r.screen, text: r.text, sid, trunc: r.truncated });
      } catch (e) {
        if (!(e instanceof ProviderError)) throw e;
        q.turn.run(sid, d.id, `engage:${b.app}:error`, 0, p.model, 0, now());
        // No screen on failure: the device keeps whatever it cached last rather
        // than replacing a good question with an error on an ambient surface.
        json(res, e.status === 401 || e.status === 403 ? 402 : 503,
             { error: 'provider', sid, text: e.status === 401 || e.status === 403
               ? 'Your AI key was rejected. Check it on the dashboard.'
               : 'The AI is busy or unreachable. Try again in a moment.' });
      }
    },

    // The owner's installed apps, in two steps on purpose.
    //
    // Without an id this returns the index alone — no manifests. The device
    // then asks for one manifest at a time. Eight apps at four kilobytes each
    // would be a 32 KB response, and this reader has under 50 KB of contiguous
    // heap while TLS is up: a single-shot sync would work on the bench and fail
    // on a full card. Peak memory here is one manifest.
    'GET /v1/apps': async ({ req, res, query }) => {
      const d = auth(req, res); if (!d) return;

      if (query.id) {
        const row = q.appManifest.get(String(query.id), d.user_id);
        if (!row) return json(res, 404, { error: 'no_app' });
        // Served as the manifest itself, so the device can stream it to the
        // card without parsing a wrapper it would only throw away.
        const buf = Buffer.from(row.manifest);
        res.writeHead(200, { 'content-type': 'application/json', 'content-length': buf.length });
        return res.end(buf);
      }

      const rows = q.apps.all(d.user_id);
      // A stamp over the whole set, so a device already holding it writes
      // nothing. Cheaper than comparing manifests on a device with no spare heap.
      const version = hashOf(rows.map(a => `${a.id}:${a.updated_at}`).join('|')).slice(0, 16);
      json(res, 200, { version, apps: rows.map(a => ({ id: a.id, name: a.name, icon: a.icon })) });
    },

    'GET /v1/brief/next': async ({ req, res }) => {
      const d = auth(req, res); if (!d) return;
      res.writeHead(204); res.end(); // briefs land in release two
    },

    _internal: q,
  };
}

const KINDS_PUBLIC = { explain: 'Explain this', summary: 'Story so far', who: 'Who is this?', translate: 'Translate', define: 'Define word' };
