// Dashboard: dev login by email (OAuth providers slot in behind the same
// session cookie later), claim page for the device flow, provider config,
// device list, session traces. Server-rendered HTML, no client framework and
// no external assets — the relay serves itself, and this has to stay fast on a
// phone held next to the reader during pairing.
import { json, html, readBody, cookies } from '../http.js';
import { now, id, sha } from '../db.js';
import { timingSafeEqual } from 'node:crypto';
import { validateManifest, SOURCES, ICONS, LIMITS } from '../manifest.js';

// INK_ACCESS_CODE gates sign-in on public deployments until real OAuth lands.
const ACCESS_CODE = process.env.INK_ACCESS_CODE || '';
const safeEqual = (a, b) => { const x = Buffer.from(a), y = Buffer.from(b); return x.length === y.length && timingSafeEqual(x, y); };

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

const STYLE = `:root{
  --bg:#fbfaf8; --fg:#1b1a17; --muted:#6b6862; --card:#fff; --line:#e6e2da;
  --accent:#2f6f4f; --accent-fg:#fff; --danger:#9c3328; --shadow:0 1px 2px rgba(0,0,0,.05);
}
@media (prefers-color-scheme:dark){:root{
  --bg:#16150f; --fg:#ece8df; --muted:#9a958a; --card:#1f1e17; --line:#332f26;
  --accent:#7fba97; --accent-fg:#11221a; --danger:#e08a7e; --shadow:none;
}}
*{box-sizing:border-box}
body{font:16px/1.55 ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif;
  background:var(--bg);color:var(--fg);margin:0;padding:0 1rem 4rem;-webkit-text-size-adjust:100%}
.wrap{max-width:42rem;margin:0 auto}
header.top{display:flex;align-items:baseline;gap:.6rem;padding:1.5rem 0 .25rem}
.brand{font-weight:640;letter-spacing:-.01em;text-decoration:none;color:var(--fg)}
.brand span{color:var(--muted);font-weight:400}
nav{display:flex;flex-wrap:wrap;gap:.25rem;border-bottom:1px solid var(--line);margin-bottom:1.5rem}
nav a{padding:.5rem .7rem;text-decoration:none;color:var(--muted);border-bottom:2px solid transparent;
  margin-bottom:-1px;border-radius:6px 6px 0 0}
nav a:hover{color:var(--fg)}
nav a[aria-current]{color:var(--fg);border-bottom-color:var(--accent);font-weight:560}
nav .spacer{flex:1}
h1{font-size:1.45rem;line-height:1.25;letter-spacing:-.015em;margin:0 0 .35rem}
h2{font-size:1rem;margin:0}
p{margin:.5rem 0}
a{color:var(--accent)}
.muted{color:var(--muted)}
.small{font-size:.875rem}
label{display:block;margin-top:1rem;font-weight:560;font-size:.9rem}
label .hint{display:block;font-weight:400;color:var(--muted);font-size:.85rem;margin-top:.15rem}
input,select,button{font:inherit;border-radius:8px;border:1px solid var(--line);
  padding:.55rem .7rem;background:var(--card);color:var(--fg);margin-top:.3rem}
input:focus-visible,select:focus-visible,button:focus-visible{outline:2px solid var(--accent);outline-offset:1px}
input[type=email],input[type=password],input[type=text],input:not([type]),select{width:100%}
button{cursor:pointer;font-weight:560;width:auto}
.btn{background:var(--accent);color:var(--accent-fg);border-color:transparent}
.btn-danger{background:transparent;color:var(--danger)}
.actions{display:flex;gap:.5rem;flex-wrap:wrap;margin-top:1rem;align-items:center}
.card{border:1px solid var(--line);border-radius:12px;padding:1rem;margin:.75rem 0;
  background:var(--card);box-shadow:var(--shadow)}
.row{display:flex;gap:.5rem;align-items:center;flex-wrap:wrap}
.row input{flex:1;min-width:9rem;margin-top:0}
.note{border-left:3px solid var(--accent);background:var(--card);border-radius:0 8px 8px 0;
  padding:.7rem .9rem;margin:1rem 0}
.note.bad{border-left-color:var(--danger)}
.empty{border:1px dashed var(--line);border-radius:12px;padding:2rem 1rem;text-align:center;color:var(--muted)}
.empty strong{display:block;font-size:1.05rem;color:var(--fg);font-weight:560;margin-bottom:.25rem}
code{background:var(--bg);border:1px solid var(--line);border-radius:5px;padding:.05rem .3rem;font-size:.9em}
pre{background:var(--bg);border:1px solid var(--line);border-radius:8px;padding:.75rem;
  white-space:pre-wrap;overflow-wrap:anywhere;margin:.4rem 0;font-size:.9rem}
.pill{display:inline-block;font-size:.75rem;font-weight:560;padding:.1rem .45rem;border-radius:999px;
  border:1px solid var(--line);color:var(--muted)}
.pill.off{color:var(--danger);border-color:var(--danger)}
.meta{color:var(--muted);font-size:.85rem;display:flex;flex-wrap:wrap;gap:.2rem .6rem;margin-top:.5rem}
.code-input{font-size:1.6rem;letter-spacing:.18em;text-transform:uppercase;text-align:center;
  font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
details>summary{cursor:pointer;color:var(--muted);font-size:.875rem;padding:.25rem 0}
.head{display:flex;justify-content:space-between;align-items:baseline;gap:1rem;flex-wrap:wrap}`;

const NAV = [['/devices', 'Readers'], ['/apps', 'Apps'], ['/provider', 'Your AI'], ['/traces', 'Traces']];

const FAVICON = "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Crect width='32' height='32' rx='7' fill='%232f6f4f'/%3E%3Crect x='9' y='8' width='14' height='16' rx='2' fill='%23fff'/%3E%3Crect x='12' y='12' width='8' height='1.6' fill='%232f6f4f'/%3E%3Crect x='12' y='16' width='8' height='1.6' fill='%232f6f4f'/%3E%3C/svg%3E";

// A signed-out page gets no nav: tabs you cannot use are noise, and the old
// shell showed them on the login screen.
const page = (title, body, { active = '', chrome = true } = {}) => `<!doctype html><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<link rel="icon" href="${FAVICON}">
<title>${esc(title)} · InkAgent</title>
<style>${STYLE}</style>
<div class="wrap">
<header class="top"><a class="brand" href="/devices">InkAgent <span>relay</span></a></header>
${chrome
    ? `<nav>${NAV.map(([href, label]) => `<a href="${href}"${active === href ? ' aria-current="page"' : ''}>${label}</a>`).join('')}<span class="spacer"></span><a href="/logout">Sign out</a></nav>`
    : '<div style="height:1.25rem"></div>'}
<h1>${esc(title)}</h1>${body}</div>`;

// Failures render inside the page that caused them, with the form still filled
// in, rather than on a dead-end page the user has to navigate back from.
const note = (text, bad = false) => `<div class="note${bad ? ' bad' : ''}">${text}</div>`;

const when = (ts) => ts ? new Date(ts * 1000).toLocaleString() : 'never';

// What the editor opens on, so the first thing an owner sees is a working app.
const STARTER_MANIFEST = "{\n  \"name\": \"Status\",\n  \"icon\": \"info\",\n  \"title\": \"Status\",\n  \"rows\": [\n    {\"kind\": \"kv\", \"label\": \"Reading\", \"value\": {\"src\": \"reading.title\"}},\n    {\"kind\": \"kv\", \"label\": \"Battery\", \"value\": {\"src\": \"device.battery\"}}\n  ]\n}";

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
    apps: db.prepare(`SELECT * FROM apps WHERE user_id = ? ORDER BY name`),
    appGet: db.prepare(`SELECT * FROM apps WHERE id = ? AND user_id = ?`),
    appInsert: db.prepare(`INSERT INTO apps (id,user_id,name,icon,manifest,updated_at) VALUES (?,?,?,?,?,?)`),
    appUpdate: db.prepare(`UPDATE apps SET name=?, icon=?, manifest=?, updated_at=? WHERE id=? AND user_id=?`),
    appDelete: db.prepare(`DELETE FROM apps WHERE id = ? AND user_id = ?`),
  };

  const user = (req) => { const sid = cookies(req).ink_session; return sid ? q.sessUser.get(sid) : null; };
  const requireUser = (req, res, next = req.url) => {
    const u = user(req);
    if (!u) { res.writeHead(302, { location: `/login?next=${encodeURIComponent(next)}` }); res.end(); }
    return u;
  };

  // The provider form, re-rendered with whatever the user last typed so a
  // validation failure never costs them their input.
  const providerPage = (values, banner = '') => page('Your AI', `
    ${banner}
    <p class="muted">Bring your own key or point at your own endpoint. Keys stay on this relay — the reader never sees them.</p>
    <form method="post" action="/provider">
      <label>Preset
        <span class="hint">Picking one fills in the URL and model below. You can still edit them.</span>
        <select name="kind" onchange="const p=${esc(JSON.stringify(PRESETS))};const v=p[this.value];if(v){base_url.value=v.base_url;model.value=v.model}">
        ${Object.entries(PRESETS).map(([k, v]) => `<option value="${k}" ${k === values.kind ? 'selected' : ''}>${esc(v.name)}</option>`).join('')}
        </select>
      </label>
      <label>Base URL <input name="base_url" id="base_url" value="${esc(values.base_url)}" required spellcheck="false"></label>
      <label>Model <input name="model" id="model" value="${esc(values.model)}" required spellcheck="false"></label>
      <label>API key
        <span class="hint">Leave empty for a local endpoint such as Ollama.</span>
        <input name="api_key" type="password" value="${esc(values.api_key || '')}" autocomplete="off">
      </label>
      <div class="actions"><button class="btn" name="test" value="1">Save and test</button><button>Save only</button></div>
    </form>`, { active: '/provider' });

  return {
    'GET /': async ({ res }) => { res.writeHead(302, { location: '/devices' }); res.end(); },

    'GET /login': async ({ res, query }) => html(res, 200, page('Sign in', `
      <p class="muted">${ACCESS_CODE ? 'Private beta — you need the access code.' : 'Development sign-in. Google, Apple and GitHub plug in here later behind the same session.'}</p>
      <form method="post" action="/login"><input type="hidden" name="next" value="${esc(query.next || '/devices')}">
      <label>Email <input name="email" type="email" required autofocus autocomplete="email"></label>
      ${ACCESS_CODE ? '<label>Access code <input name="code" type="password" required autocomplete="one-time-code"></label>' : ''}
      <div class="actions"><button class="btn">Sign in</button></div></form>`, { chrome: false })),

    'POST /login': async ({ req, res }) => {
      const b = await readBody(req);
      const email = String(b.email || '').trim().toLowerCase();
      const nextVal = esc(b.next || '/devices');
      const retry = (msg) => page('Sign in', `
        ${note(msg, true)}
        <form method="post" action="/login"><input type="hidden" name="next" value="${nextVal}">
        <label>Email <input name="email" type="email" value="${esc(email)}" required autofocus autocomplete="email"></label>
        ${ACCESS_CODE ? '<label>Access code <input name="code" type="password" required autocomplete="one-time-code"></label>' : ''}
        <div class="actions"><button class="btn">Sign in</button></div></form>`, { chrome: false });
      if (!/^[^@\s]+@[^@\s]+$/.test(email)) return html(res, 400, retry('That does not look like an email address.'));
      if (ACCESS_CODE && !safeEqual(String(b.code || ''), ACCESS_CODE)) {
        await new Promise(r => setTimeout(r, 500)); // slow brute force a little
        return html(res, 403, retry('That access code is not right.'));
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
      html(res, 200, claimPage(String(query.code || '').toUpperCase()));
    },

    'POST /claim': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const b = await readBody(req);
      const code = String(b.code || '').toUpperCase().trim();
      const name = String(b.name || 'My X3');
      const p = q.pairByUserCode.get(code);
      if (!p || p.status !== 'pending') {
        return html(res, 400, claimPage(code, name, 'That code is not valid. Check the reader is still showing its pairing screen.'));
      }
      if (p.expires_at < now()) {
        return html(res, 400, claimPage('', name, 'That code has expired. Start pairing again on the reader.'));
      }
      const deviceId = id(), token = id(32);
      q.deviceInsert.run(deviceId, u.id, p.hw, name.slice(0, 40), sha(token), p.budget, now());
      devTokens.set(p.device_code, token);
      q.pairClaim.run(u.id, deviceId, p.device_code);
      const hasProvider = !!q.provGet.get(u.id);
      html(res, 200, page('Reader claimed', `
        ${note(`<strong>${esc(name)}</strong> is paired. It finishes on its own within a few seconds — you can put it down.`)}
        ${hasProvider
          ? '<p><a href="/devices">Go to your readers</a></p>'
          : `<div class="card"><h2>One thing left</h2><p class="muted">The reader has nothing to talk to yet. Connect a model and it is ready.</p>
             <div class="actions"><a class="btn" href="/provider" style="text-decoration:none;padding:.55rem .7rem;border-radius:8px">Connect your AI</a></div></div>`}`,
        { active: '/devices' }));
    },

    'GET /devices': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const rows = q.devices.all(u.id);
      const hasProvider = !!q.provGet.get(u.id);
      const banner = rows.length > 0 && !hasProvider
        ? note('These readers have no model connected yet. <a href="/provider">Connect your AI</a> so they have something to talk to.', true)
        : '';
      html(res, 200, page('Readers', rows.length === 0
        ? `<div class="empty"><strong>No readers yet</strong>
           <p>Open <b>Settings → InkAgent</b> on your reader to get a pairing code, then claim it here.</p>
           <div class="actions" style="justify-content:center"><a class="btn" href="/claim" style="text-decoration:none;padding:.55rem .7rem;border-radius:8px">Claim a reader</a></div></div>`
        : banner + rows.map(d => `<div class="card">
            <div class="head"><h2>${esc(d.name)}</h2>${d.revoked ? '<span class="pill off">Unpaired</span>' : '<span class="pill">Active</span>'}</div>
            <div class="meta"><span>hw <code>${esc(d.hw)}</code></span><span>budget ${d.budget} B</span><span>last seen ${esc(when(d.last_seen))}</span></div>
            ${d.revoked ? '' : `<hr>
            <form method="post" action="/devices/rename" class="row">
              <input type="hidden" name="id" value="${esc(d.id)}">
              <input name="name" value="${esc(d.name)}" aria-label="Reader name" maxlength="40">
              <button>Rename</button>
            </form>
            <form method="post" action="/devices/revoke" onsubmit="return confirm('Unpair ${esc(d.name).replace(/'/g, '')}? Its token stops working immediately.')" style="margin-top:.5rem">
              <input type="hidden" name="id" value="${esc(d.id)}"><button class="btn-danger">Unpair</button>
            </form>`}
          </div>`).join('') + '<p><a href="/claim">Claim another reader</a></p>',
        { active: '/devices' }));
    },
    'POST /devices/rename': async ({ req, res }) => { const u = requireUser(req, res); if (!u) return; const b = await readBody(req); q.rename.run(String(b.name || '').slice(0, 40) || 'Reader', String(b.id), u.id); res.writeHead(302, { location: '/devices' }); res.end(); },
    'POST /devices/revoke': async ({ req, res }) => { const u = requireUser(req, res); if (!u) return; const b = await readBody(req); q.revoke.run(String(b.id), u.id); res.writeHead(302, { location: '/devices' }); res.end(); },


    'GET /apps': async ({ req, res, query }) => {
      const u = requireUser(req, res); if (!u) return;
      const editing = query.id ? q.appGet.get(String(query.id), u.id) : null;
      html(res, 200, appsPage(q.apps.all(u.id), editing));
    },

    'POST /apps': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const b = await readBody(req);
      const text = String(b.manifest || '');
      const editingId = String(b.id || '');
      const editing = editingId ? q.appGet.get(editingId, u.id) : null;
      const result = validateManifest(text);
      if (!result.ok) {
        // The manifest comes back exactly as typed. Losing a screen someone
        // just wrote because they mistyped one source name is unforgivable.
        return html(res, 400, appsPage(q.apps.all(u.id), editing, text,
          note('<strong>Not saved.</strong><ul>' + result.errors.map(e => `<li>${esc(e)}</li>`).join('') + '</ul>', true)));
      }
      if (editing) {
        q.appUpdate.run(result.meta.name, result.meta.icon, text, now(), editing.id, u.id);
      } else {
        q.appInsert.run(id(), u.id, result.meta.name, result.meta.icon, text, now());
      }
      res.writeHead(302, { location: '/apps' }); res.end();
    },

    'POST /apps/delete': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const b = await readBody(req);
      q.appDelete.run(String(b.id), u.id);
      res.writeHead(302, { location: '/apps' }); res.end();
    },

    'GET /provider': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const p = q.provGet.get(u.id) || { kind: 'groq', ...PRESETS.groq, api_key: '' };
      html(res, 200, providerPage(p));
    },
    'POST /provider': async ({ req, res, ctx }) => {
      const u = requireUser(req, res); if (!u) return;
      const b = await readBody(req);
      const values = { kind: String(b.kind || 'custom'), base_url: String(b.base_url || ''), model: String(b.model || ''), api_key: String(b.api_key || '') };
      let base;
      try { base = new URL(values.base_url); } catch { return html(res, 400, providerPage(values, note('That base URL is not a URL. It should look like <code>https://api.groq.com/openai/v1</code>.', true))); }
      if (!['http:', 'https:'].includes(base.protocol)) {
        return html(res, 400, providerPage(values, note('The base URL has to start with <code>http://</code> or <code>https://</code>.', true)));
      }
      q.provSet.run(u.id, values.kind, base.toString().replace(/\/+$/, ''), values.api_key || null, values.model.trim(), now());
      if (b.test) {
        const { chat } = await import('../providers/openaiCompat.js');
        try {
          const r = await chat({ baseUrl: base.toString(), apiKey: values.api_key || null, model: values.model, messages: [{ role: 'user', content: 'Reply with the single word: ready' }], maxTokens: 5, timeoutMs: 20000 });
          return html(res, 200, page('Your AI', `
            ${note(`Saved and working. The model replied <code>${esc(r.text.trim())}</code>.`)}
            <p><a href="/devices">Back to your readers</a></p>`, { active: '/provider' }));
        } catch (e) {
          return html(res, 200, providerPage(values, note(`Saved, but the test call failed: <code>${esc(e.message)}</code>`, true)));
        }
      }
      res.writeHead(302, { location: '/devices' }); res.end();
    },

    'GET /traces': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const rows = q.traces.all(u.id);
      html(res, 200, page('Traces', rows.length === 0
        ? `<div class="empty"><strong>Nothing yet</strong><p>Ask a book something on your reader and the exchange shows up here.</p></div>`
        : `<p class="muted small">The last ${rows.length} exchange${rows.length === 1 ? '' : 's'}, newest first.</p>` + rows.map(t => {
          const r = JSON.parse(t.request);
          return `<div class="card">
            <div class="head"><h2>${esc(t.kind)}</h2><span class="pill">${t.latency_ms} ms</span></div>
            <div class="meta"><span>${esc(when(t.created_at))}</span><span>${esc(t.device_name)}</span>${t.model ? `<span>${esc(t.model)}</span>` : ''}${t.truncated ? '<span>cut to fit</span>' : ''}</div>
            ${r.book ? `<p class="muted small">${esc(r.book)}${r.chapter ? ' · ' + esc(r.chapter) : ''}</p>` : ''}
            <details><summary>Passage sent</summary><pre>${esc(r.text.slice(0, 400))}${r.text.length > 400 ? '…' : ''}</pre></details>
            <p class="small" style="margin-bottom:.1rem"><b>On the reader</b></p><pre>${esc(t.sent_text)}</pre>
            ${t.truncated ? `<details><summary>Full answer before trimming</summary><pre>${esc(t.full_text)}</pre></details>` : ''}
            <div class="meta"><span><code>${esc(t.sid)}</code></span></div>
          </div>`;
        }).join(''), { active: '/traces' }));
    },
    'GET /s': async ({ req, res, query }) => { // deep link from the NFC tag: /s?sid=...
      const u = requireUser(req, res); if (!u) return;
      res.writeHead(302, { location: '/traces' }); res.end();
    },
    'GET /health': async ({ res }) => json(res, 200, { ok: true }),
  };


  // The apps page: what is installed, and one editor. Kept on a single screen
  // because an owner has a handful of these, not a catalogue.
  function appsPage(apps, editing, draft = null, banner = '') {
    const value = draft !== null ? draft : (editing ? editing.manifest : STARTER_MANIFEST);
    return page('Apps', `
      ${banner}
      <p class="muted">An app is a JSON manifest. Your readers pick these up and show them in the drawer —
      no firmware build, nothing to copy onto the card by hand.</p>

      ${apps.length === 0 ? '' : apps.map(a => `<div class="card">
        <div class="head"><h2>${esc(a.name)}</h2><span class="pill">${esc(a.icon)}</span></div>
        <div class="meta"><span>updated ${esc(when(a.updated_at))}</span></div>
        <div class="actions">
          <a href="/apps?id=${esc(a.id)}">Edit</a>
          <form method="post" action="/apps/delete" onsubmit="return confirm('Remove ${esc(a.name).replace(/'/g, '')} from your readers?')" style="margin:0">
            <input type="hidden" name="id" value="${esc(a.id)}"><button class="btn-danger">Remove</button>
          </form>
        </div></div>`).join('')}

      <div class="card">
        <h2>${editing ? `Edit ${esc(editing.name)}` : 'Add an app'}</h2>
        <form method="post" action="/apps">
          ${editing ? `<input type="hidden" name="id" value="${esc(editing.id)}">` : ''}
          <label>Manifest
            <span class="hint">Up to ${LIMITS.bytes} bytes, ${LIMITS.rows} rows. Checked before it reaches your readers.</span>
            <textarea name="manifest" rows="14" spellcheck="false"
              style="width:100%;font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;border:1px solid var(--line);border-radius:8px;padding:.7rem;background:var(--card);color:var(--fg);margin-top:.3rem">${esc(value)}</textarea>
          </label>
          <div class="actions"><button class="btn">${editing ? 'Save changes' : 'Add app'}</button>
            ${editing ? '<a href="/apps">Cancel</a>' : ''}</div>
        </form>
      </div>

      <details><summary>What a manifest can say</summary>
        <p>Row kinds: <code>text</code>, <code>kv</code>, <code>rule</code>, <code>logo</code>.
        A <code>text</code> row that resolves to nothing disappears entirely, which is how an optional row works —
        there are no conditionals, no loops and no expressions.</p>
        <p><b>Values</b> are literal text or <code>{"src": "..."}</code>, from:</p>
        <p>${SOURCES.map(x => `<code>${esc(x)}</code>`).join(' ')}</p>
        <p><b>Icons</b>: ${ICONS.map(x => `<code>${esc(x)}</code>`).join(' ')}</p>
      </details>`, { active: '/apps' });
  }

  // Declared after the returned object so the claim handlers can share it.
  function claimPage(code, name = 'My X3', error = '') {
    return page('Claim a reader', `
      ${error ? note(esc(error), true) : '<p class="muted">Your reader shows a code while it waits on its pairing screen. Type it in here.</p>'}
      <form method="post" action="/claim">
        <label>Code shown on the reader
          <input name="code" class="code-input" value="${esc(code)}" pattern="[A-Za-z2-9]{4}-?[A-Za-z2-9]{4}"
            placeholder="ABCD-EFGH" required autofocus autocomplete="off" spellcheck="false"
            title="Eight characters, as shown on the reader">
        </label>
        <label>Name this reader
          <span class="hint">Only so you can tell it apart later.</span>
          <input name="name" value="${esc(name)}" maxlength="40">
        </label>
        <div class="actions"><button class="btn">Claim this reader</button></div>
      </form>`, { active: '/devices' });
  }
}
