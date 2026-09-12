// Dashboard: dev login by email (OAuth providers slot in behind the same
// session cookie later), claim page for the device flow, provider config,
// device list, session traces. Server-rendered HTML, no client framework.
import { json, html, readBody, cookies } from '../http.js';
import { now, id, sha } from '../db.js';
import { timingSafeEqual } from 'node:crypto';

// INK_ACCESS_CODE gates sign-in on public deployments until real OAuth lands.
const ACCESS_CODE = process.env.INK_ACCESS_CODE || '';
const safeEqual = (a, b) => { const x = Buffer.from(a), y = Buffer.from(b); return x.length === y.length && timingSafeEqual(x, y); };

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const page = (title, body) => `<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>${esc(title)} · InkAgent</title>
<style>body{font:16px/1.5 system-ui;max-width:40rem;margin:2rem auto;padding:0 1rem;color:#222}
input,select,button{font:inherit;padding:.5rem;margin:.25rem 0}label{display:block;margin-top:.75rem}
code,pre{background:#f3f3f3;padding:.1rem .3rem}pre{padding:.75rem;white-space:pre-wrap}
.card{border:1px solid #ddd;border-radius:8px;padding:1rem;margin:1rem 0}nav a{margin-right:1rem}.muted{color:#777}</style>
<nav><a href="/devices">Readers</a><a href="/provider">Your AI</a><a href="/traces">Traces</a><a href="/logout">Sign out</a></nav>
<h1>${esc(title)}</h1>${body}`;

export const PRESETS = {
  openrouter: { name: 'OpenRouter', base_url: 'https://openrouter.ai/api/v1', model: 'anthropic/claude-sonnet-5' },
  groq:       { name: 'Groq (free tier)', base_url: 'https://api.groq.com/openai/v1', model: 'llama-3.3-70b-versatile' },
  gemini:     { name: 'Google Gemini (free tier)', base_url: 'https://generativelanguage.googleapis.com/v1beta/openai', model: 'gemini-2.5-flash' },
  openai:     { name: 'OpenAI', base_url: 'https://api.openai.com/v1', model: 'gpt-5-mini' },
  ollama:     { name: 'Ollama / LM Studio / any OpenAI-compatible URL', base_url: 'http://localhost:11434/v1', model: 'llama3.2' },
};

export function dashboardRoutes(db, { devTokens }) {
  const q = {
    userByEmail: db.prepare(`SELECT * FROM users WHERE email = ?`),
    userInsert: db.prepare(`INSERT INTO users (id,email,created_at) VALUES (?,?,?)`),
    sessInsert: db.prepare(`INSERT INTO sessions (id,user_id,created_at) VALUES (?,?,?)`),
    sessUser: db.prepare(`SELECT u.* FROM sessions s JOIN users u ON u.id = s.user_id WHERE s.id = ?`),
    pairByUserCode: db.prepare(`SELECT * FROM pairings WHERE user_code = ?`),
    pairClaim: db.prepare(`UPDATE pairings SET status='claimed', user_id=?, device_id=? WHERE device_code=?`),
    deviceInsert: db.prepare(`INSERT INTO devices (id,user_id,hw,name,token_hash,budget,created_at) VALUES (?,?,?,?,?,?,?)`),
    devices: db.prepare(`SELECT * FROM devices WHERE user_id = ? ORDER BY created_at`),
    revoke: db.prepare(`UPDATE devices SET revoked = 1 WHERE id = ? AND user_id = ?`),
    rename: db.prepare(`UPDATE devices SET name = ? WHERE id = ? AND user_id = ?`),
    provGet: db.prepare(`SELECT * FROM providers WHERE user_id = ?`),
    provSet: db.prepare(`INSERT INTO providers (user_id,kind,base_url,api_key,model,updated_at) VALUES (?,?,?,?,?,?)
                         ON CONFLICT(user_id) DO UPDATE SET kind=excluded.kind, base_url=excluded.base_url, api_key=excluded.api_key, model=excluded.model, updated_at=excluded.updated_at`),
    traces: db.prepare(`SELECT t.*, d.name AS device_name FROM turns t JOIN devices d ON d.id = t.device_id WHERE d.user_id = ? ORDER BY t.created_at DESC LIMIT 50`),
  };

  const user = (req) => { const sid = cookies(req).ink_session; return sid ? q.sessUser.get(sid) : null; };
  const requireUser = (req, res, next = req.url) => {
    const u = user(req);
    if (!u) { res.writeHead(302, { location: `/login?next=${encodeURIComponent(next)}` }); res.end(); }
    return u;
  };

  return {
    'GET /': async ({ res }) => { res.writeHead(302, { location: '/devices' }); res.end(); },

    'GET /login': async ({ res, query }) => html(res, 200, page('Sign in', `
      <p class="muted">${ACCESS_CODE ? 'Private beta: you need the access code.' : 'Development sign-in. Google, Apple and GitHub plug in here later behind the same session.'}</p>
      <form method="post" action="/login"><input type="hidden" name="next" value="${esc(query.next || '/devices')}">
      <label>Email <input name="email" type="email" required autofocus></label>
      ${ACCESS_CODE ? '<label>Access code <input name="code" type="password" required></label>' : ''}
      <button>Sign in</button></form>`)),

    'POST /login': async ({ req, res }) => {
      const b = await readBody(req);
      const email = String(b.email || '').trim().toLowerCase();
      if (!/^[^@\s]+@[^@\s]+$/.test(email)) return html(res, 400, page('Sign in', '<p>That is not an email.</p>'));
      if (ACCESS_CODE && !safeEqual(String(b.code || ''), ACCESS_CODE)) {
        await new Promise(r => setTimeout(r, 500)); // slow brute force a little
        return html(res, 403, page('Sign in', '<p>Wrong access code.</p><p><a href="/login">Try again</a></p>'));
      }
      let u = q.userByEmail.get(email);
      if (!u) { u = { id: id(), email }; q.userInsert.run(u.id, email, now()); }
      const sid = id(24); q.sessInsert.run(sid, u.id, now());
      const next = String(b.next || '/devices'); const safe = next.startsWith('/') ? next : '/devices';
      res.writeHead(302, { location: safe, 'set-cookie': `ink_session=${sid}; Path=/; HttpOnly; SameSite=Lax${process.env.PUBLIC_URL?.startsWith('https') ? '; Secure' : ''}` }); res.end();
    },
    'GET /logout': async ({ res }) => { res.writeHead(302, { location: '/login', 'set-cookie': 'ink_session=; Path=/; Max-Age=0' }); res.end(); },

    'GET /claim': async ({ req, res, query }) => {
      const u = requireUser(req, res); if (!u) return;
      const code = String(query.code || '').toUpperCase();
      html(res, 200, page('Claim a reader', `
        <form method="post" action="/claim"><label>Code shown on the reader
        <input name="code" value="${esc(code)}" pattern="[A-Z2-9]{4}-[A-Z2-9]{4}" placeholder="ABCD-EFGH" required style="font-size:1.5rem;letter-spacing:.1em;text-transform:uppercase"></label>
        <label>Name <input name="name" value="My X3"></label><button>Claim this reader</button></form>`));
    },

    'POST /claim': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const b = await readBody(req);
      const code = String(b.code || '').toUpperCase().trim();
      const p = q.pairByUserCode.get(code);
      if (!p || p.status !== 'pending') return html(res, 400, page('Claim a reader', '<p>That code is not valid. Make sure the reader is still on the pairing screen.</p><p><a href="/claim">Try again</a></p>'));
      if (p.expires_at < now()) return html(res, 400, page('Claim a reader', '<p>That code expired. Restart pairing on the reader.</p>'));
      const deviceId = id(), token = id(32);
      q.deviceInsert.run(deviceId, u.id, p.hw, String(b.name || 'My X3').slice(0, 40), sha(token), p.budget, now());
      devTokens.set(p.device_code, token);
      q.pairClaim.run(u.id, deviceId, p.device_code);
      const hasProvider = !!q.provGet.get(u.id);
      html(res, 200, page('Reader claimed', `<p>The reader will finish pairing on its own within a few seconds.</p>
        ${hasProvider ? '<p><a href="/devices">Go to your readers</a></p>' : '<p><strong>Next:</strong> <a href="/provider">connect your AI</a> so the reader has something to talk to.</p>'}`));
    },

    'GET /devices': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const rows = q.devices.all(u.id);
      html(res, 200, page('Readers', rows.length === 0 ? '<p>No readers yet. Put one on its pairing screen and <a href="/claim">claim it</a>.</p>' :
        rows.map(d => `<div class="card"><form method="post" action="/devices/rename" style="display:inline">
          <input type="hidden" name="id" value="${d.id}"><input name="name" value="${esc(d.name)}"><button>Rename</button></form>
          <div class="muted">hw ${d.hw} · budget ${d.budget} B · last seen ${d.last_seen ? new Date(d.last_seen * 1000).toLocaleString() : 'never'} ${d.revoked ? '· <b>revoked</b>' : ''}</div>
          ${d.revoked ? '' : `<form method="post" action="/devices/revoke" onsubmit="return confirm('Unpair this reader?')"><input type="hidden" name="id" value="${d.id}"><button>Unpair</button></form>`}
        </div>`).join('') + '<p><a href="/claim">Claim another reader</a></p>'));
    },
    'POST /devices/rename': async ({ req, res }) => { const u = requireUser(req, res); if (!u) return; const b = await readBody(req); q.rename.run(String(b.name || '').slice(0, 40) || 'Reader', String(b.id), u.id); res.writeHead(302, { location: '/devices' }); res.end(); },
    'POST /devices/revoke': async ({ req, res }) => { const u = requireUser(req, res); if (!u) return; const b = await readBody(req); q.revoke.run(String(b.id), u.id); res.writeHead(302, { location: '/devices' }); res.end(); },

    'GET /provider': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const p = q.provGet.get(u.id) || { kind: 'groq', ...PRESETS.groq, api_key: '' };
      html(res, 200, page('Your AI', `<p>Bring your own key or your own endpoint. Keys stay on this relay; the reader never sees them.</p>
        <form method="post" action="/provider">
        <label>Preset <select name="kind" onchange="const p=${esc(JSON.stringify(PRESETS))};const v=p[this.value];if(v){base_url.value=v.base_url;model.value=v.model}">
          ${Object.entries(PRESETS).map(([k, v]) => `<option value="${k}" ${k === p.kind ? 'selected' : ''}>${esc(v.name)}</option>`).join('')}</select></label>
        <label>Base URL <input name="base_url" id="base_url" value="${esc(p.base_url)}" required style="width:100%"></label>
        <label>Model <input name="model" id="model" value="${esc(p.model)}" required></label>
        <label>API key <input name="api_key" type="password" value="${esc(p.api_key || '')}" placeholder="leave empty for local endpoints" style="width:100%"></label>
        <button>Save</button> <button name="test" value="1">Save and test</button></form>`));
    },
    'POST /provider': async ({ req, res, ctx }) => {
      const u = requireUser(req, res); if (!u) return;
      const b = await readBody(req);
      let base; try { base = new URL(String(b.base_url)); } catch { return html(res, 400, page('Your AI', '<p>Base URL is not a URL.</p>')); }
      if (!['http:', 'https:'].includes(base.protocol)) return html(res, 400, page('Your AI', '<p>Base URL must be http or https.</p>'));
      q.provSet.run(u.id, String(b.kind || 'custom'), base.toString().replace(/\/+$/, ''), String(b.api_key || '') || null, String(b.model || '').trim(), now());
      if (b.test) {
        const { chat } = await import('../providers/openaiCompat.js');
        try {
          const r = await chat({ baseUrl: base.toString(), apiKey: b.api_key || null, model: b.model, messages: [{ role: 'user', content: 'Reply with the single word: ready' }], maxTokens: 5, timeoutMs: 20000 });
          return html(res, 200, page('Your AI', `<p>Saved. The AI answered: <code>${esc(r.text.trim())}</code></p><p><a href="/devices">Back to readers</a></p>`));
        } catch (e) { return html(res, 200, page('Your AI', `<p>Saved, but the test failed: <code>${esc(e.message)}</code></p><p><a href="/provider">Fix it</a></p>`)); }
      }
      res.writeHead(302, { location: '/devices' }); res.end();
    },

    'GET /traces': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const rows = q.traces.all(u.id);
      html(res, 200, page('Traces', rows.length === 0 ? '<p>Nothing yet. Ask the book something.</p>' : rows.map(t => {
        const r = JSON.parse(t.request);
        return `<div class="card"><div class="muted">${new Date(t.created_at * 1000).toLocaleString()} · ${esc(t.device_name)} · ${esc(t.kind)} · ${esc(t.model || '')} · ${t.latency_ms} ms ${t.truncated ? '· cut to fit' : ''} · <code>${t.sid}</code></div>
          <p class="muted">${esc(r.book || '')} ${r.chapter ? '· ' + esc(r.chapter) : ''}</p><pre>${esc(r.text.slice(0, 400))}${r.text.length > 400 ? '…' : ''}</pre>
          <p><b>On the reader</b></p><pre>${esc(t.sent_text)}</pre>${t.truncated ? `<p><b>Full answer</b></p><pre>${esc(t.full_text)}</pre>` : ''}</div>`;
      }).join('')));
    },
    'GET /s': async ({ req, res, query }) => { // deep link from the NFC tag: /s?sid=...
      const u = requireUser(req, res); if (!u) return;
      res.writeHead(302, { location: '/traces' }); res.end();
    },
    'GET /health': async ({ res }) => json(res, 200, { ok: true }),
  };
}
