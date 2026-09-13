import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startRelay } from './helpers.js';
import { available, authorizeUrl, emailFromCode, PROVIDERS } from '../src/oauth.js';

let R;
before(async () => { R = await startRelay(); });
after(() => R.close());

const withCreds = async (fn) => {
  process.env.INK_OAUTH_GITHUB_ID = 'gh-id';
  process.env.INK_OAUTH_GITHUB_SECRET = 'gh-secret';
  try { await fn(); } finally {
    delete process.env.INK_OAUTH_GITHUB_ID;
    delete process.env.INK_OAUTH_GITHUB_SECRET;
  }
};

test('a provider is only offered once both halves of its credentials exist', async () => {
  assert.deepEqual(available(), [], 'nothing configured, nothing offered');
  process.env.INK_OAUTH_GITHUB_ID = 'only-the-id';
  assert.deepEqual(available(), [], 'half-configured is not configured');
  delete process.env.INK_OAUTH_GITHUB_ID;
  await withCreds(async () => assert.deepEqual(available(), ['github']));
});

test('the authorize URL carries the state and the callback, and never the secret', async () => {
  await withCreds(async () => {
    const u = new URL(authorizeUrl('github', { publicUrl: 'https://relay.test/', state: 'abc123' }));
    assert.equal(u.origin + u.pathname, 'https://github.com/login/oauth/authorize');
    assert.equal(u.searchParams.get('client_id'), 'gh-id');
    assert.equal(u.searchParams.get('state'), 'abc123');
    assert.equal(u.searchParams.get('redirect_uri'), 'https://relay.test/auth/callback');
    assert.equal(u.searchParams.get('response_type'), 'code');
    assert.ok(!u.toString().includes('gh-secret'), 'the secret never reaches the browser');
  });
});

test('an unverified address is not an identity', async () => {
  await withCreds(async () => {
    const fetchImpl = async (url) => {
      if (String(url).includes('access_token') || String(url).includes('/oauth/access_token')) {
        return { ok: true, json: async () => ({ access_token: 't' }) };
      }
      return { ok: true, json: async () => ([{ email: 'nope@example.com', primary: true, verified: false }]) };
    };
    const email = await emailFromCode('github', { code: 'c', publicUrl: 'https://relay.test', fetchImpl });
    assert.equal(email, null, 'GitHub address that was never verified is refused');
  });
});

test('the verified primary address becomes the identity', async () => {
  await withCreds(async () => {
    const fetchImpl = async (url) => {
      if (String(url).includes('/oauth/access_token')) return { ok: true, json: async () => ({ access_token: 't' }) };
      return { ok: true, json: async () => ([
        { email: 'other@example.com', primary: false, verified: true },
        { email: 'Real@Example.com', primary: true, verified: true },
      ]) };
    };
    const email = await emailFromCode('github', { code: 'c', publicUrl: 'https://relay.test', fetchImpl });
    assert.equal(email, 'real@example.com', 'primary, verified, and lower-cased');
  });
});

test('a provider that refuses the exchange yields nothing rather than throwing', async () => {
  await withCreds(async () => {
    const fetchImpl = async () => ({ ok: false, json: async () => ({}) });
    assert.equal(await emailFromCode('github', { code: 'bad', publicUrl: 'https://relay.test', fetchImpl }), null);
    const throwing = async () => { throw new Error('network down'); };
    assert.equal(await emailFromCode('github', { code: 'c', publicUrl: 'https://relay.test', fetchImpl: throwing }), null);
  });
});

test('a callback nobody started cannot mint a session', async () => {
  // No ink_oauth cookie: the state cannot be checked, so the request is refused
  // rather than trusted.
  const res = await fetch(R.base + '/auth/callback?p=github&code=x&state=y', { redirect: 'manual' });
  assert.equal(res.status, 400);
  assert.equal(res.headers.get('set-cookie'), null, 'no session issued');
  assert.match(await res.text(), /did not start here/);
});

test('a callback whose state does not match the cookie is refused', async () => {
  const res = await fetch(R.base + '/auth/callback?p=github&code=x&state=wrong', {
    headers: { cookie: 'ink_oauth=github:right:%2Fdevices' }, redirect: 'manual',
  });
  assert.equal(res.status, 400);
  assert.equal(res.headers.get('set-cookie'), null, 'no session issued');
  assert.match(await res.text(), /could not be verified/);
});

test('starting a sign-in sets a short-lived state cookie and leaves for the provider', async () => {
  await withCreds(async () => {
    const res = await fetch(R.base + '/auth/start?p=github', { redirect: 'manual' });
    assert.equal(res.status, 302);
    const cookie = res.headers.get('set-cookie') || '';
    assert.match(cookie, /^ink_oauth=github:/);
    assert.match(cookie, /HttpOnly/);
    assert.match(cookie, /Max-Age=600/, 'the state does not outlive the sign-in');
    const state = cookie.split('ink_oauth=')[1].split(':')[1];
    assert.equal(new URL(res.headers.get('location')).searchParams.get('state'), state,
      'the state sent to the provider is the one in the cookie');
  });
});

test('an unknown provider goes back to sign-in rather than anywhere else', async () => {
  const res = await fetch(R.base + '/auth/start?p=myspace', { redirect: 'manual' });
  assert.equal(res.status, 302);
  assert.equal(res.headers.get('location'), '/login');
});

test('with nothing configured the sign-in page says so instead of offering a way in', async () => {
  const prev = process.env.INK_DEV_LOGIN;
  delete process.env.INK_DEV_LOGIN;
  try {
    const html = await (await fetch(R.base + '/login')).text();
    assert.match(html, /No sign-in method configured/);
    assert.doesNotMatch(html, /<form method="post" action="\/login">/, 'no open email box');

    // And the endpoint itself refuses, not just the page.
    const res = await fetch(R.base + '/login', {
      method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ email: 'intruder@example.com' }), redirect: 'manual',
    });
    assert.equal(res.headers.get('set-cookie'), null, 'no session for a posted email');
  } finally {
    if (prev !== undefined) process.env.INK_DEV_LOGIN = prev;
  }
});

test('every provider declares the environment variables it needs', () => {
  for (const [key, p] of Object.entries(PROVIDERS)) {
    assert.ok(p.idEnv && p.secretEnv, `${key} names its credentials`);
    assert.ok(typeof p.email === 'function', `${key} can resolve an address`);
  }
});
