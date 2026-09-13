// Sign-in with an identity provider.
//
// The relay holds the owner's model key, so who is signed in decides who can
// read it. The previous email box verified nothing — it took any address and
// issued a session — and was fenced only by a shared access code that everyone
// in a beta ends up knowing. This replaces both.
//
// The authorization-code flow, server side: the browser never sees a token, and
// the email is read from the provider over a direct TLS call rather than taken
// from anything the browser handed us.

const REDIRECT_PATH = '/auth/callback';

export const PROVIDERS = {
  github: {
    name: 'GitHub',
    idEnv: 'INK_OAUTH_GITHUB_ID',
    secretEnv: 'INK_OAUTH_GITHUB_SECRET',
    authorize: 'https://github.com/login/oauth/authorize',
    token: 'https://github.com/login/oauth/access_token',
    scope: 'read:user user:email',
    // GitHub's /user may hide the address, so ask the addresses endpoint and
    // take the one the account actually verified.
    async email(accessToken, fetchImpl) {
      const res = await fetchImpl('https://api.github.com/user/emails', {
        headers: { authorization: `Bearer ${accessToken}`, accept: 'application/vnd.github+json', 'user-agent': 'InkAgent-Relay' },
      });
      if (!res.ok) return null;
      const list = await res.json();
      const primary = Array.isArray(list) ? list.find(e => e.primary && e.verified) : null;
      return primary ? String(primary.email).toLowerCase() : null;
    },
  },

  google: {
    name: 'Google',
    idEnv: 'INK_OAUTH_GOOGLE_ID',
    secretEnv: 'INK_OAUTH_GOOGLE_SECRET',
    authorize: 'https://accounts.google.com/o/oauth2/v2/auth',
    token: 'https://oauth2.googleapis.com/token',
    scope: 'openid email',
    async email(accessToken, fetchImpl) {
      const res = await fetchImpl('https://openidconnect.googleapis.com/v1/userinfo', {
        headers: { authorization: `Bearer ${accessToken}` },
      });
      if (!res.ok) return null;
      const info = await res.json();
      // An unverified address is not an identity: Google will hand one over for
      // accounts that never confirmed it.
      if (!info || info.email_verified !== true || !info.email) return null;
      return String(info.email).toLowerCase();
    },
  },
};

const creds = (key) => {
  const p = PROVIDERS[key];
  if (!p) return null;
  const id = process.env[p.idEnv], secret = process.env[p.secretEnv];
  return id && secret ? { id, secret } : null;
};

// Providers with both halves of their credentials present.
export function available() {
  return Object.keys(PROVIDERS).filter(k => creds(k) !== null);
}

export function redirectUri(publicUrl) {
  return `${String(publicUrl || '').replace(/\/+$/, '')}${REDIRECT_PATH}`;
}

export function authorizeUrl(key, { publicUrl, state }) {
  const p = PROVIDERS[key], c = creds(key);
  if (!p || !c) return null;
  const u = new URL(p.authorize);
  u.searchParams.set('client_id', c.id);
  u.searchParams.set('redirect_uri', redirectUri(publicUrl));
  u.searchParams.set('response_type', 'code');
  u.searchParams.set('scope', p.scope);
  u.searchParams.set('state', state);
  return u.toString();
}

// Exchanges the code and returns a verified email address, or null. Never
// throws: the caller is rendering a sign-in page.
export async function emailFromCode(key, { code, publicUrl, fetchImpl = fetch }) {
  const p = PROVIDERS[key], c = creds(key);
  if (!p || !c || !code) return null;
  try {
    const res = await fetchImpl(p.token, {
      method: 'POST',
      headers: { 'content-type': 'application/x-www-form-urlencoded', accept: 'application/json' },
      body: new URLSearchParams({
        client_id: c.id, client_secret: c.secret, code,
        redirect_uri: redirectUri(publicUrl), grant_type: 'authorization_code',
      }),
    });
    if (!res.ok) return null;
    const body = await res.json();
    if (!body || !body.access_token) return null;
    return await p.email(body.access_token, fetchImpl);
  } catch {
    return null;
  }
}
