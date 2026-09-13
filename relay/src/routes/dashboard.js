// Dashboard: sign-in by email and password or by passkey, the claim page for
// the device flow, provider config, the device list and the app editor.
// Server-rendered HTML, no client framework and no external assets — the relay
// serves itself, and this has to stay fast on a phone held next to the reader
// during pairing. The only scripts are the two WebAuthn ceremonies, which
// cannot be done without one.
import { json, html, readBody, cookies } from '../http.js';
import { now, id, sha } from '../db.js';
import { validateManifest, SOURCES, ICONS, LIMITS } from '../manifest.js';
import { hashPassword, verifyPassword, passwordProblem } from '../password.js';
import { challenge as newChallenge, rpIdFrom, verifyRegistration, verifyAssertion } from '../webauthn.js';

// Sign-in is an email and a password, with a passkey as an alternative once
// one is registered. The box this replaces verified nothing at all — it issued
// a session for any address typed into it — behind a shared access code, which
// stops being a secret as soon as a beta has more than a few people in it.
//
// Accounts are isolated by design: readers, apps and the model key all hang off
// user_id, so a new signup sees nothing belonging to anyone else. Set
// INK_SIGNUP_CLOSED=1 once the accounts that should exist do.
const signupClosed = () => process.env.INK_SIGNUP_CLOSED === '1';

// Failed attempts, per email, in memory. Enough to make an online guessing
// attack pointless; a serious one belongs at the proxy, not here.
const attempts = new Map();
const THROTTLE_AFTER = 5, THROTTLE_MS = 5000;
function failedRecently(email) {
  const a = attempts.get(email);
  return a && a.count >= THROTTLE_AFTER && Date.now() - a.at < THROTTLE_MS;
}
function noteFailure(email) {
  const a = attempts.get(email) || { count: 0, at: 0 };
  attempts.set(email, { count: a.count + 1, at: Date.now() });
}

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

const STYLE = `:root{
  /* The panel resolves four levels and nothing else, so the dashboard uses
     four: paper, a rule grey, a muted grey, and ink. No colour, because the
     device has none and a green button here would be describing a machine that
     cannot show it. */
  --paper:#f5f3ed; --l2:#cdc9be; --l1:#6d695f; --ink:#17160f;
  --gap:1rem;
}
/* Night mode on the reader is inverted output polarity, not a different
   palette. Same here: the four levels swap ends. */
@media (prefers-color-scheme:dark){:root{
  --paper:#17160f; --l2:#3b382f; --l1:#9d988b; --ink:#f0ede4;
}}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
/* No transitions anywhere. A panel that takes a second to repaint does not
   ease, and the stillness is most of the character. */
*,*::before,*::after{transition:none!important;animation:none!important}
body{font:16px/1.6 ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif;
  background:var(--paper);color:var(--ink);margin:0;padding:0 var(--gap) 4rem}
.wrap{max-width:40rem;margin:0 auto}

header.top{display:flex;align-items:baseline;gap:.6rem;padding:1.5rem 0 .5rem}
.brand{font-family:ui-serif,Georgia,"Times New Roman",serif;font-size:1.05rem;
  letter-spacing:.01em;text-decoration:none;color:var(--ink)}
.brand span{color:var(--l1)}

/* Selection is an underline, as it is on the reader: a rule under the label
   rather than an inverted band. */
nav{display:flex;gap:0;border-bottom:1px solid var(--l2);margin-bottom:1.5rem;
  overflow-x:auto;scrollbar-width:none}
nav::-webkit-scrollbar{display:none}
nav a{padding:.55rem .8rem;text-decoration:none;color:var(--l1);white-space:nowrap;
  border-bottom:3px solid transparent;margin-bottom:-1px}
nav a:hover{color:var(--ink)}
nav a[aria-current]{color:var(--ink);border-bottom-color:var(--ink)}
nav .spacer{flex:1;min-width:.5rem}

h1{font-family:ui-serif,Georgia,"Times New Roman",serif;font-weight:600;
  font-size:1.6rem;line-height:1.2;letter-spacing:-.01em;margin:0}
h2{font-family:ui-serif,Georgia,"Times New Roman",serif;font-weight:600;font-size:1.05rem;margin:0}
.lead{color:var(--l1);margin:.35rem 0 1.5rem}
p{margin:.5rem 0}
a{color:var(--ink);text-underline-offset:2px}
.muted{color:var(--l1)}
.small{font-size:.875rem}

label{display:block;margin-top:1.1rem;font-weight:600;font-size:.85rem;
  letter-spacing:.04em;text-transform:uppercase;color:var(--l1)}
label .hint{display:block;font-weight:400;text-transform:none;letter-spacing:0;
  color:var(--l1);font-size:.85rem;margin-top:.2rem}

/* Square, hairline, flat. E-ink has no depth to imply. 16px on controls so iOS
   does not zoom the page on focus. */
input,select,button,textarea{font:inherit;font-size:16px;border-radius:0;
  border:1px solid var(--l1);padding:.6rem .7rem;background:var(--paper);
  color:var(--ink);margin-top:.35rem;min-height:44px;box-shadow:none;
  -webkit-appearance:none;appearance:none}
select{background-image:linear-gradient(45deg,transparent 50%,var(--ink) 50%),
  linear-gradient(135deg,var(--ink) 50%,transparent 50%);
  background-position:calc(100% - 18px) 50%,calc(100% - 13px) 50%;
  background-size:5px 5px,5px 5px;background-repeat:no-repeat;padding-right:2.2rem}
textarea{min-height:14rem;font:13px/1.6 ui-monospace,SFMono-Regular,Menlo,monospace;width:100%}
input:focus-visible,select:focus-visible,button:focus-visible,textarea:focus-visible{
  outline:2px solid var(--ink);outline-offset:1px}
input[type=email],input[type=password],input[type=text],input:not([type]),select{width:100%}
button{cursor:pointer;font-weight:600;width:auto;letter-spacing:.02em}
/* The primary action is an inverted block, which is how the reader marks the
   thing you are on. */
.btn{background:var(--ink);color:var(--paper);border-color:var(--ink)}
.btn-danger{background:transparent;color:var(--ink);border-style:dashed}
a.btn,a.btn-quiet{display:inline-flex;align-items:center;justify-content:center;
  min-height:44px;padding:.6rem 1rem;text-decoration:none;border:1px solid var(--ink)}
a.btn-quiet{color:var(--ink);background:var(--paper)}
.actions{display:flex;gap:.5rem;flex-wrap:wrap;margin-top:1.1rem;align-items:center}
.actions form{margin:0}

.card{border:1px solid var(--l2);border-radius:0;padding:1rem;margin:.9rem 0;
  background:var(--paper);box-shadow:none}
.card > :first-child{margin-top:0}
.row{display:flex;gap:.5rem;align-items:center;flex-wrap:wrap}
.row input{flex:1;min-width:8rem;margin-top:0}

/* A dithered edge instead of a coloured one: two greys at 45 degrees, which is
   how the panel fakes a tone it does not have. */
.note{border-left:6px solid var(--ink);background:var(--paper);border-radius:0;
  padding:.75rem .9rem;margin:1.1rem 0}
.note.bad{border-left:6px solid transparent;
  border-image:repeating-linear-gradient(45deg,var(--ink) 0 3px,var(--paper) 3px 6px) 6}
.note ul{margin:.4rem 0 0;padding-left:1.1rem}

.empty{border:1px dashed var(--l2);border-radius:0;padding:2.25rem 1rem;text-align:center;color:var(--l1)}
.empty strong{display:block;font-family:ui-serif,Georgia,serif;font-size:1.15rem;
  color:var(--ink);font-weight:600;margin-bottom:.35rem}

.steps{list-style:none;padding:0;margin:0}
.steps li{display:flex;gap:.7rem;align-items:flex-start;padding:.6rem 0;border-bottom:1px solid var(--l2)}
.steps li:last-child{border-bottom:0}
.steps .tick,.steps .todo{flex:0 0 1.5rem;font-weight:700;font-variant-numeric:tabular-nums}
.steps .tick{color:var(--ink)}
.steps .todo{color:var(--l1)}
.steps .done{color:var(--l1);text-decoration:line-through}

code{background:transparent;border:1px solid var(--l2);border-radius:0;padding:.05rem .3rem;
  font-size:.9em;overflow-wrap:anywhere}
pre{background:transparent;border:1px solid var(--l2);border-radius:0;padding:.8rem;
  white-space:pre-wrap;overflow-wrap:anywhere;margin:.5rem 0;font-size:.85rem}
.pill{display:inline-block;font-size:.7rem;font-weight:600;letter-spacing:.06em;
  text-transform:uppercase;padding:.2rem .5rem;border-radius:0;border:1px solid var(--l1);
  color:var(--l1);white-space:nowrap}
.pill.off{color:var(--paper);background:var(--ink);border-color:var(--ink)}
.meta{color:var(--l1);font-size:.85rem;display:flex;flex-wrap:wrap;gap:.2rem .7rem;margin-top:.6rem}
.code-input{font-size:1.6rem;letter-spacing:.2em;text-transform:uppercase;text-align:center;
  font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
details>summary{cursor:pointer;color:var(--l1);font-size:.875rem;padding:.45rem 0}
.head{display:flex;justify-content:space-between;align-items:baseline;gap:.75rem;flex-wrap:wrap}

@media (max-width:30rem){
  :root{--gap:.85rem}
  .card{padding:.85rem}
  h1{font-size:1.35rem}
  .actions{flex-direction:column;align-items:stretch}
  .actions form,.actions button,.actions a.btn,.actions a.btn-quiet{width:100%}
  .row{flex-direction:column;align-items:stretch}
  .row button{width:100%}
}`;

const NAV = [['/devices', 'Readers'], ['/apps', 'Apps'], ['/provider', 'Your AI'], ['/account', 'Account']];

const FAVICON = "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Crect width='32' height='32' fill='%23f5f3ed'/%3E%3Crect x='6' y='5' width='20' height='22' fill='none' stroke='%2317160f' stroke-width='2'/%3E%3Crect x='10' y='11' width='12' height='2' fill='%2317160f'/%3E%3Crect x='10' y='16' width='12' height='2' fill='%2317160f'/%3E%3Crect x='10' y='21' width='7' height='2' fill='%2317160f'/%3E%3C/svg%3E";

// A signed-out page gets no nav: tabs you cannot use are noise.
//
// Every page takes a `lead` — one line saying what the page is for. Without it
// each screen opened on a different kind of content (a list here, a form there,
// a wall of JSON on the apps page) and the dashboard felt like separate tools
// rather than one.
const page = (title, body, { active = '', chrome = true, lead = '' } = {}) => `<!doctype html><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="light dark">
<link rel="icon" href="${FAVICON}">
<title>${esc(title)} · InkAgent</title>
<style>${STYLE}</style>
<div class="wrap">
<header class="top"><a class="brand" href="/devices">InkAgent <span>relay</span></a></header>
${chrome
    ? `<nav>${NAV.map(([href, label]) => `<a href="${href}"${active === href ? ' aria-current="page"' : ''}>${label}</a>`).join('')}<span class="spacer"></span><a href="/logout">Sign out</a></nav>`
    : '<div style="height:1.25rem"></div>'}
<h1>${esc(title)}</h1>${lead ? `<p class="lead">${lead}</p>` : '<div style="height:1rem"></div>'}${body}</div>`;

// Failures render inside the page that caused them, with the form still filled
// in, rather than on a dead-end page the user has to navigate back from.
const note = (text, bad = false) => `<div class="note${bad ? ' bad' : ''}">${text}</div>`;

const sessionCookie = (sid) =>
  `ink_session=${sid}; Path=/; HttpOnly; SameSite=Lax${securePart()}`;

const originOf = (publicUrl) => { try { return new URL(publicUrl).origin; } catch { return 'http://localhost'; } };

const securePart = () => (process.env.PUBLIC_URL || '').startsWith('https') ? '; Secure' : '';

const when = (ts) => ts ? new Date(ts * 1000).toLocaleString() : 'never';

// What the editor opens on, so the first thing an owner sees is a working app.
const STARTER_MANIFEST = "{\n  \"name\": \"Status\",\n  \"icon\": \"info\",\n  \"title\": \"Status\",\n  \"rows\": [\n    {\"kind\": \"kv\", \"label\": \"Reading\", \"value\": {\"src\": \"reading.title\"}},\n    {\"kind\": \"kv\", \"label\": \"Battery\", \"value\": {\"src\": \"device.battery\"}}\n  ]\n}";

// Models worth offering per preset. Not exhaustive and not authoritative — the
// point is that the common case is a choice rather than a string typed from
// memory, and "Custom" always remains for anything not listed.
export const MODELS = {
  openrouter: ['anthropic/claude-opus-5', 'anthropic/claude-sonnet-5', 'anthropic/claude-haiku-4.5',
               'openai/gpt-5-mini', 'google/gemini-2.5-flash', 'meta-llama/llama-3.3-70b-instruct'],
  groq:       ['llama-3.3-70b-versatile', 'llama-3.1-8b-instant'],
  gemini:     ['gemini-2.5-flash', 'gemini-2.5-pro'],
  openai:     ['gpt-5-mini', 'gpt-5'],
  ollama:     ['llama3.2', 'qwen2.5', 'mistral'],
};

export const PRESETS = {
  openrouter: { name: 'OpenRouter', base_url: 'https://openrouter.ai/api/v1', model: 'anthropic/claude-sonnet-5' },
  groq:       { name: 'Groq (free tier)', base_url: 'https://api.groq.com/openai/v1', model: 'llama-3.3-70b-versatile' },
  gemini:     { name: 'Google Gemini (free tier)', base_url: 'https://generativelanguage.googleapis.com/v1beta/openai', model: 'gemini-2.5-flash' },
  openai:     { name: 'OpenAI', base_url: 'https://api.openai.com/v1', model: 'gpt-5-mini' },
  ollama:     { name: 'Ollama / LM Studio / any OpenAI-compatible URL', base_url: 'http://localhost:11434/v1', model: 'llama3.2' },
};

export function dashboardRoutes(db, { devTokens, publicUrl = process.env.PUBLIC_URL || '', fetchImpl = fetch }) {
  const q = {
    userByEmail: db.prepare(`SELECT * FROM users WHERE email = ?`),
    userInsert: db.prepare(`INSERT INTO users (id,email,created_at) VALUES (?,?,?)`),
    pwGet: db.prepare(`SELECT hash FROM passwords WHERE user_id = ?`),
    pwSet: db.prepare(`INSERT INTO passwords (user_id,hash,updated_at) VALUES (?,?,?)
                       ON CONFLICT(user_id) DO UPDATE SET hash=excluded.hash, updated_at=excluded.updated_at`),
    credsForUser: db.prepare(`SELECT * FROM credentials WHERE user_id = ? ORDER BY created_at`),
    credById: db.prepare(`SELECT * FROM credentials WHERE cred_id = ?`),
    credInsert: db.prepare(`INSERT INTO credentials (cred_id,user_id,public_key,label,counter,created_at) VALUES (?,?,?,?,?,?)`),
    credTouch: db.prepare(`UPDATE credentials SET counter = ?, last_used = ? WHERE cred_id = ?`),
    credDelete: db.prepare(`DELETE FROM credentials WHERE cred_id = ? AND user_id = ?`),
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
  // The provider form, re-rendered with whatever the user last typed so a
  // validation failure never costs them their input.
  const providerPage = (values, banner = '') => {
    const known = MODELS[values.kind] || [];
    const isCustom = values.model !== '' && !known.includes(values.model);
    const options = known.map(m => `<option value="${esc(m)}"${m === values.model ? ' selected' : ''}>${esc(m)}</option>`).join('') +
      `<option value="__custom__"${isCustom ? ' selected' : ''}>Custom…</option>`;

    return page('Your AI', `
      ${banner}
      <form method="post" action="/provider">
        <label>Provider
          <span class="hint">Sets the endpoint and the models offered below.</span>
          <select name="kind" id="kind">
          ${Object.entries(PRESETS).map(([k, v]) => `<option value="${k}"${k === values.kind ? ' selected' : ''}>${esc(v.name)}</option>`).join('')}
          </select>
        </label>

        <label>Model
          <select name="model_select" id="model_select">${options}</select>
        </label>
        <div id="custom_wrap"${isCustom ? '' : ' hidden'}>
          <label>Model name
            <span class="hint">Exactly as the provider expects it.</span>
            <input name="model_custom" id="model_custom" value="${isCustom ? esc(values.model) : ''}" spellcheck="false">
          </label>
        </div>

        <label>API key
          <span class="hint">Stays on this relay. The reader never sees it, and it is never sent to a device.</span>
          <input name="api_key" type="password" value="${esc(values.api_key || '')}" autocomplete="off">
        </label>

        <details>
          <summary>Endpoint</summary>
          <label>Base URL <input name="base_url" id="base_url" value="${esc(values.base_url)}" required spellcheck="false"></label>
        </details>

        <div class="actions">
          <button class="btn" name="test" value="1">Save and test</button>
          <button>Save without testing</button>
        </div>
      </form>

      <script>
      (function () {
        var PRESETS = ${JSON.stringify(PRESETS)}, MODELS = ${JSON.stringify(MODELS)};
        var kind = document.getElementById('kind'), sel = document.getElementById('model_select');
        var wrap = document.getElementById('custom_wrap'), base = document.getElementById('base_url');
        function toggleCustom() { wrap.hidden = sel.value !== '__custom__'; }
        kind.addEventListener('change', function () {
          var p = PRESETS[kind.value];
          if (p) base.value = p.base_url;
          var list = MODELS[kind.value] || [];
          sel.innerHTML = list.map(function (m) { return '<option value="' + m + '">' + m + '</option>'; }).join('')
            + '<option value="__custom__">Custom…</option>';
          toggleCustom();
        });
        sel.addEventListener('change', toggleCustom);
      })();
      </script>`,
      { active: '/provider', lead: 'Bring your own key, or point at an endpoint you run. Nothing here reaches your readers.' });
  };

  return {
    'GET /': async ({ res }) => { res.writeHead(302, { location: '/devices' }); res.end(); },

    'GET /login': async ({ res, query }) => {
      const next = esc(query.next || '/devices');
      html(res, 200, page('Sign in', `
        <form method="post" action="/login" id="pwform">
          <input type="hidden" name="next" value="${next}">
          <label>Email <input name="email" type="email" required autofocus autocomplete="username"></label>
          <label>Password <input name="password" type="password" required autocomplete="current-password"></label>
          <div class="actions"><button class="btn">Sign in</button></div>
        </form>

        <div class="actions"><button id="pk" class="btn-quiet" type="button">Use a passkey</button></div>
        <p id="pkmsg" class="muted small" hidden></p>

        ${signupClosed() ? '' : '<p class="muted small">No account? <a href="/signup">Create one</a>.</p>'}

        <script>
        (function () {
          var b = document.getElementById('pk'), msg = document.getElementById('pkmsg');
          if (!window.PublicKeyCredential) { b.hidden = true; return; }
          var u8 = function (s) { s = s.split('-').join('+').split('_').join('/');
            var raw = atob(s + '==='.slice((s.length + 3) % 4)), a = new Uint8Array(raw.length);
            for (var i = 0; i < raw.length; i++) a[i] = raw.charCodeAt(i); return a; };
          var b64 = function (buf) { var s = ''; var a = new Uint8Array(buf);
            for (var i = 0; i < a.length; i++) s += String.fromCharCode(a[i]);
            return btoa(s).split('+').join('-').split('/').join('_').replace(/=+$/,''); };
          b.addEventListener('click', async function () {
            msg.hidden = false; msg.textContent = 'Waiting for your passkey…';
            try {
              var opts = await (await fetch('/auth/passkey/options')).json();
              var cred = await navigator.credentials.get({ publicKey: {
                challenge: u8(opts.challenge), rpId: opts.rpId, userVerification: 'preferred', timeout: 60000 } });
              var r = await fetch('/auth/passkey/verify', { method: 'POST',
                headers: { 'content-type': 'application/json' },
                body: JSON.stringify({ id: cred.id,
                  authenticatorData: b64(cred.response.authenticatorData),
                  clientDataJSON: b64(cred.response.clientDataJSON),
                  signature: b64(cred.response.signature) }) });
              var out = await r.json();
              if (out.ok) { location.href = out.next || '/devices'; }
              else { msg.textContent = out.error || 'That passkey was not recognised.'; }
            } catch (e) { msg.textContent = 'Passkey sign-in was cancelled or failed.'; }
          });
        })();
        </script>`,
        { chrome: false, lead: 'Your readers and your model key live behind this account.' }));
    },

    'GET /signup': async ({ res }) => {
      if (signupClosed()) { res.writeHead(302, { location: '/login' }); return res.end(); }
      html(res, 200, page('Create an account', `
        <form method="post" action="/signup">
          <label>Email <input name="email" type="email" required autofocus autocomplete="username"></label>
          <label>Password
            <span class="hint">At least 10 characters. Length matters more than symbols.</span>
            <input name="password" type="password" required autocomplete="new-password">
          </label>
          <div class="actions"><button class="btn">Create account</button><a class="btn-quiet" href="/login">Sign in instead</a></div>
        </form>`, { chrome: false, lead: 'One account holds your readers, your apps and your model key.' }));
    },

    'POST /signup': async ({ req, res }) => {
      if (signupClosed()) { res.writeHead(302, { location: '/login' }); return res.end(); }
      const b = await readBody(req);
      const email = String(b.email || '').trim().toLowerCase();
      const password = String(b.password || '');
      const back = (why) => html(res, 400, page('Create an account',
        note(esc(why), true) + '<div class="actions"><a class="btn" href="/signup">Try again</a></div>',
        { chrome: false }));

      if (!/^[^@\s]+@[^@\s]+$/.test(email)) return back('That does not look like an email address.');
      const problem = passwordProblem(password);
      if (problem) return back(problem);
      if (q.userByEmail.get(email)) return back('There is already an account with that address. Sign in instead.');

      const u = { id: id(), email };
      q.userInsert.run(u.id, email, now());
      q.pwSet.run(u.id, await hashPassword(password), now());
      const sid = id(24); q.sessInsert.run(sid, u.id, now());
      res.writeHead(302, { location: '/devices', 'set-cookie': sessionCookie(sid) });
      res.end();
    },

    'POST /login': async ({ req, res }) => {
      const b = await readBody(req);
      const email = String(b.email || '').trim().toLowerCase();
      const password = String(b.password || '');
      // One message for every failure: which half was wrong is exactly what an
      // attacker enumerating addresses wants to learn.
      const refuse = (why = 'That email and password do not match.') => html(res, 400, page('Sign in',
        note(esc(why), true) + '<div class="actions"><a class="btn" href="/login">Try again</a></div>',
        { chrome: false }));

      if (failedRecently(email)) return refuse('Too many attempts. Wait a few seconds and try again.');

      const u = q.userByEmail.get(email);
      const row = u ? q.pwGet.get(u.id) : null;
      // Hash even when the account does not exist, so the reply takes the same
      // time either way and does not reveal which addresses are registered.
      const ok = await verifyPassword(password, row ? row.hash : 'scrypt$16384$8$1$AAAA$AAAA');
      if (!u || !row || !ok) { noteFailure(email); return refuse(); }

      attempts.delete(email);
      const sid = id(24); q.sessInsert.run(sid, u.id, now());
      const next = String(b.next || '/devices'); const safe = next.startsWith('/') ? next : '/devices';
      res.writeHead(302, { location: safe, 'set-cookie': sessionCookie(sid) });
      res.end();
    },

    // --- Passkeys -----------------------------------------------------------
    // The challenge is minted here, kept in a short-lived cookie and used once.
    // That is what stops a captured response being replayed later.

    'GET /auth/passkey/options': async ({ res }) => {
      const c = newChallenge();
      json(res, 200, { challenge: c, rpId: rpIdFrom(publicUrl) }, {
        'set-cookie': `ink_pk=${c}; Path=/; HttpOnly; SameSite=Lax; Max-Age=300${securePart()}`,
      });
    },

    'POST /auth/passkey/verify': async ({ req, res }) => {
      const expected = cookies(req).ink_pk;
      const b = await readBody(req);
      const clear = `ink_pk=; Path=/; Max-Age=0`;
      if (!expected) return json(res, 400, { error: 'That sign-in did not start here.' }, { 'set-cookie': clear });

      const cred = q.credById.get(String(b.id || ''));
      if (!cred) return json(res, 400, { error: 'That passkey is not registered here.' }, { 'set-cookie': clear });

      try {
        const { counter } = verifyAssertion({
          authenticatorData: b.authenticatorData, clientDataJSON: b.clientDataJSON, signature: b.signature,
          publicKey: cred.public_key, expectedChallenge: expected, origin: originOf(publicUrl),
          rpId: rpIdFrom(publicUrl), storedCounter: cred.counter,
        });
        q.credTouch.run(counter, now(), cred.cred_id);
        const sid = id(24); q.sessInsert.run(sid, cred.user_id, now());
        json(res, 200, { ok: true, next: '/devices' }, { 'set-cookie': [sessionCookie(sid), clear] });
      } catch (e) {
        json(res, 400, { error: 'That passkey could not be verified.' }, { 'set-cookie': clear });
      }
    },

    'GET /auth/passkey/register-options': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const c = newChallenge();
      json(res, 200, {
        challenge: c, rpId: rpIdFrom(publicUrl), rpName: 'InkAgent relay',
        user: { id: Buffer.from(u.id).toString('base64url'), name: u.email, displayName: u.email },
        exclude: q.credsForUser.all(u.id).map(r => r.cred_id),
      }, { 'set-cookie': `ink_pk=${c}; Path=/; HttpOnly; SameSite=Lax; Max-Age=300${securePart()}` });
    },

    'POST /auth/passkey/register': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const expected = cookies(req).ink_pk;
      const b = await readBody(req);
      const clear = `ink_pk=; Path=/; Max-Age=0`;
      if (!expected) return json(res, 400, { error: 'That registration did not start here.' }, { 'set-cookie': clear });
      try {
        const reg = verifyRegistration({
          attestationObject: b.attestationObject, clientDataJSON: b.clientDataJSON,
          expectedChallenge: expected, origin: originOf(publicUrl), rpId: rpIdFrom(publicUrl),
        });
        if (q.credById.get(reg.credId)) return json(res, 400, { error: 'That passkey is already registered.' }, { 'set-cookie': clear });
        const label = String(b.label || '').slice(0, 40) || 'Passkey';
        q.credInsert.run(reg.credId, u.id, reg.publicKey, label, reg.counter, now());
        json(res, 200, { ok: true }, { 'set-cookie': clear });
      } catch (e) {
        json(res, 400, { error: 'That passkey could not be registered.' }, { 'set-cookie': clear });
      }
    },

    'GET /account': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const creds = q.credsForUser.all(u.id);
      html(res, 200, page('Account', `
        <div class="card">
          <div class="head"><h2>${esc(u.email)}</h2></div>
          <form method="post" action="/account/password" style="margin-top:.5rem">
            <label>Current password <input name="current" type="password" required autocomplete="current-password"></label>
            <label>New password
              <span class="hint">At least 10 characters.</span>
              <input name="next" type="password" required autocomplete="new-password">
            </label>
            <div class="actions"><button>Change password</button></div>
          </form>
        </div>

        <h2 style="margin-top:1.5rem">Passkeys</h2>
        <p class="muted small">A passkey signs you in with the fingerprint reader or screen lock you already use,
        and cannot be phished or reused on another site.</p>

        ${creds.length === 0 ? '<div class="empty"><strong>None yet</strong><p>Add one and you can skip the password on this device.</p></div>'
          : creds.map(c => `<div class="card">
              <div class="head"><h2>${esc(c.label)}</h2><span class="pill">${esc(when(c.last_used) === 'never' ? 'unused' : 'used ' + when(c.last_used))}</span></div>
              <div class="meta"><span>added ${esc(when(c.created_at))}</span></div>
              <div class="actions">
                <form method="post" action="/account/passkey/delete" onsubmit="return confirm('Remove this passkey?')">
                  <input type="hidden" name="id" value="${esc(c.cred_id)}"><button class="btn-danger">Remove</button>
                </form>
              </div></div>`).join('')}

        <div class="actions"><button id="add" class="btn" type="button">Add a passkey</button></div>
        <p id="msg" class="muted small" hidden></p>

        <script>
        (function () {
          var b = document.getElementById('add'), msg = document.getElementById('msg');
          if (!window.PublicKeyCredential) { b.hidden = true; msg.hidden = false;
            msg.textContent = 'This browser does not support passkeys.'; return; }
          var u8 = function (s) { s = s.split('-').join('+').split('_').join('/');
            var raw = atob(s + '==='.slice((s.length + 3) % 4)), a = new Uint8Array(raw.length);
            for (var i = 0; i < raw.length; i++) a[i] = raw.charCodeAt(i); return a; };
          var b64 = function (buf) { var s = ''; var a = new Uint8Array(buf);
            for (var i = 0; i < a.length; i++) s += String.fromCharCode(a[i]);
            return btoa(s).split('+').join('-').split('/').join('_').replace(/=+$/,''); };
          b.addEventListener('click', async function () {
            msg.hidden = false; msg.textContent = 'Follow your device prompt…';
            try {
              var o = await (await fetch('/auth/passkey/register-options')).json();
              var cred = await navigator.credentials.create({ publicKey: {
                challenge: u8(o.challenge),
                rp: { id: o.rpId, name: o.rpName },
                user: { id: u8(o.user.id), name: o.user.name, displayName: o.user.displayName },
                pubKeyCredParams: [{ type: 'public-key', alg: -7 }, { type: 'public-key', alg: -257 }],
                authenticatorSelection: { residentKey: 'required', requireResidentKey: true, userVerification: 'preferred' },
                excludeCredentials: (o.exclude || []).map(function (id) { return { type: 'public-key', id: u8(id) }; }),
                timeout: 60000, attestation: 'none' } });
              var r = await fetch('/auth/passkey/register', { method: 'POST',
                headers: { 'content-type': 'application/json' },
                body: JSON.stringify({ attestationObject: b64(cred.response.attestationObject),
                  clientDataJSON: b64(cred.response.clientDataJSON),
                  label: (navigator.platform || 'Passkey') }) });
              var out = await r.json();
              if (out.ok) location.reload(); else msg.textContent = out.error || 'That did not work.';
            } catch (e) { msg.textContent = 'Passkey setup was cancelled or failed.'; }
          });
        })();
        </script>`, { active: '/account', lead: 'How you sign in to this relay.' }));
    },

    'POST /account/password': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const b = await readBody(req);
      const row = q.pwGet.get(u.id);
      const back = (why, bad = true) => html(res, bad ? 400 : 200, page('Account',
        note(esc(why), bad) + '<div class="actions"><a class="btn" href="/account">Back</a></div>',
        { active: '/account' }));

      // The current password is required even though there is already a
      // session: a borrowed unlocked browser should not be able to take the
      // account over.
      if (!row || !(await verifyPassword(String(b.current || ''), row.hash))) {
        return back('That is not your current password.');
      }
      const problem = passwordProblem(String(b.next || ''));
      if (problem) return back(problem);

      q.pwSet.run(u.id, await hashPassword(String(b.next)), now());
      back('Password changed.', false);
    },

    'POST /account/passkey/delete': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      const b = await readBody(req);
      q.credDelete.run(String(b.id || ''), u.id);
      res.writeHead(302, { location: '/account' }); res.end();
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
      const active = rows.filter(d => !d.revoked);
      const provider = q.provGet.get(u.id);
      const appCount = q.apps.all(u.id).length;

      // Setup is two steps and they have to happen in order, but the tabs give
      // no sense of that. Until both are done the page leads with what is left,
      // which is the difference between a dashboard and a pile of tabs.
      const setup = (active.length === 0 || !provider) ? `<div class="card">
        <h2>Getting started</h2>
        <ul class="steps">
          <li><span class="${active.length ? 'tick' : 'todo'}">${active.length ? '✓' : '1'}</span>
            <span class="${active.length ? 'done' : ''}">Claim a reader${active.length ? '' : ' — open Settings › Ask the book on the device to get a code'}</span></li>
          <li><span class="${provider ? 'tick' : 'todo'}">${provider ? '✓' : '2'}</span>
            <span class="${provider ? 'done' : ''}">Connect a model${provider ? '' : ' — without one the reader has nothing to ask'}</span></li>
        </ul>
        <div class="actions">
          ${active.length === 0
            ? '<a class="btn" href="/claim">Claim a reader</a>'
            : (!provider ? '<a class="btn" href="/provider">Connect your AI</a>' : '')}
        </div>
      </div>` : '';

      const list = rows.length === 0 ? '' : rows.map(d => `<div class="card">
          <div class="head"><h2>${esc(d.name)}</h2>${d.revoked ? '<span class="pill off">Unpaired</span>' : '<span class="pill">Active</span>'}</div>
          <div class="meta"><span>hw <code>${esc(d.hw)}</code></span><span>budget ${d.budget} B</span><span>last seen ${esc(when(d.last_seen))}</span></div>
          ${d.revoked ? '' : `
          <form method="post" action="/devices/rename" class="row" style="margin-top:.75rem">
            <input type="hidden" name="id" value="${esc(d.id)}">
            <input name="name" value="${esc(d.name)}" aria-label="Reader name" maxlength="40">
            <button>Rename</button>
          </form>
          <div class="actions">
            <form method="post" action="/devices/revoke" onsubmit="return confirm('Unpair ${esc(d.name).replace(/'/g, '')}? Its token stops working immediately.')">
              <input type="hidden" name="id" value="${esc(d.id)}"><button class="btn-danger">Unpair</button>
            </form>
          </div>`}
        </div>`).join('') + '<div class="actions"><a class="btn-quiet" href="/claim">Claim another reader</a></div>';

      const lead = rows.length === 0
        ? 'Readers paired to this account.'
        : `${active.length} reader${active.length === 1 ? '' : 's'} paired · ${provider ? esc(provider.kind) : 'no model yet'} · ${appCount} app${appCount === 1 ? '' : 's'}`;

      html(res, 200, page('Readers', setup + list, { active: '/devices', lead }));
    },

    'POST /devices/rename': async ({ req, res }) => { const u = requireUser(req, res); if (!u) return; const b = await readBody(req); q.rename.run(String(b.name || '').slice(0, 40) || 'Reader', String(b.id), u.id); res.writeHead(302, { location: '/devices' }); res.end(); },
    'POST /devices/revoke': async ({ req, res }) => { const u = requireUser(req, res); if (!u) return; const b = await readBody(req); q.revoke.run(String(b.id), u.id); res.writeHead(302, { location: '/devices' }); res.end(); },


    'GET /apps': async ({ req, res }) => {
      const u = requireUser(req, res); if (!u) return;
      html(res, 200, appsListPage(q.apps.all(u.id)));
    },

    'GET /apps/edit': async ({ req, res, query }) => {
      const u = requireUser(req, res); if (!u) return;
      html(res, 200, appEditorPage(query.id ? q.appGet.get(String(query.id), u.id) : null));
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
        return html(res, 400, appEditorPage(editing, text,
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
      // The model comes from the dropdown unless "Custom" was chosen, in which
      // case the free-text field wins. Keeping both named separately means a
      // rejected save can re-render either state correctly.
      // The form sends model_select (plus model_custom when "Custom" is chosen).
      // A plain `model` is still accepted: the device simulator and anything
      // scripted against this endpoint predate the dropdown.
      const picked = String(b.model_select || '');
      const model = picked === '__custom__' ? String(b.model_custom || '')
                  : picked !== ''          ? picked
                                           : String(b.model || '');
      const values = { kind: String(b.kind || 'custom'), base_url: String(b.base_url || ''), model,
                       api_key: String(b.api_key || '') };
      if (!values.model.trim()) {
        return html(res, 400, providerPage(values, note('Pick a model, or choose Custom and type one.', true)));
      }
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

    'GET /health': async ({ res }) => json(res, 200, { ok: true }),
  };


  // The apps page: what is installed, and one editor. Kept on a single screen
  // because an owner has a handful of these, not a catalogue.
  // The list. Previously this page also carried a fourteen-row textarea, so
  // "what have I installed" and "write an app" competed for the same screen.
  function appsListPage(apps) {
    const body = apps.length === 0
      ? `<div class="empty"><strong>No apps yet</strong>
         <p>An app is a small JSON file describing a screen. Your readers pick them up automatically.</p>
         <div class="actions" style="justify-content:center"><a class="btn" href="/apps/edit">Write one</a></div></div>`
      : apps.map(a => `<div class="card">
          <div class="head"><h2>${esc(a.name)}</h2><span class="pill">${esc(a.icon)}</span></div>
          <div class="meta"><span>updated ${esc(when(a.updated_at))}</span></div>
          <div class="actions">
            <a class="btn-quiet" href="/apps/edit?id=${esc(a.id)}">Edit</a>
            <form method="post" action="/apps/delete" onsubmit="return confirm('Remove ${esc(a.name).replace(/'/g, '')} from your readers?')">
              <input type="hidden" name="id" value="${esc(a.id)}"><button class="btn-danger">Remove</button>
            </form>
          </div></div>`).join('') +
        '<div class="actions"><a class="btn" href="/apps/edit">Add an app</a></div>';

    return page('Apps', body, {
      active: '/apps',
      lead: 'Screens your readers show. No firmware build, nothing to copy onto the card.',
    });
  }

  // The editor, on its own page so the manifest gets the whole screen.
  function appEditorPage(editing, draft = null, banner = '') {
    const value = draft !== null ? draft : (editing ? editing.manifest : STARTER_MANIFEST);
    return page(editing ? `Edit ${editing.name}` : 'New app', `
      ${banner}
      <form method="post" action="/apps">
        ${editing ? `<input type="hidden" name="id" value="${esc(editing.id)}">` : ''}
        <label>Manifest
          <span class="hint">Checked before it reaches your readers. Up to ${LIMITS.bytes} bytes, ${LIMITS.rows} rows.</span>
          <textarea name="manifest" rows="16" spellcheck="false" autocapitalize="off" autocorrect="off">${esc(value)}</textarea>
        </label>
        <div class="actions">
          <button class="btn">${editing ? 'Save changes' : 'Add app'}</button>
          <a class="btn-quiet" href="/apps">Cancel</a>
        </div>
      </form>

      <details><summary>What a manifest can say</summary>
        <p>Row kinds: <code>text</code>, <code>para</code>, <code>kv</code>, <code>rule</code>, <code>logo</code>.
        A row whose value resolves to nothing disappears entirely — that is how an optional row works.
        There are no conditionals, no loops and no expressions.</p>
        <p><b>Values</b> are literal text or <code>{"src": "..."}</code>, from:</p>
        <p>${SOURCES.map(x => `<code>${esc(x)}</code>`).join(' ')}</p>
        <p><b>Icons</b>: ${ICONS.map(x => `<code>${esc(x)}</code>`).join(' ')}</p>
      </details>`, { active: '/apps' });
  }

  // Declared after the returned object so the claim handlers can share it.
  function claimPage(code, name = 'My X3', error = '') {
    return page('Claim a reader', `
      ${error ? note(esc(error), true) : ''}
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
        <div class="actions"><button class="btn">Claim this reader</button><a class="btn-quiet" href="/devices">Cancel</a></div>
      </form>`, { active: '/devices',
        lead: 'Your reader shows a code while it waits on its pairing screen.' });
  }
}
