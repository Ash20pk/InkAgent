import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { createHash, generateKeyPairSync, createSign } from 'node:crypto';
import { startRelay } from './helpers.js';
import { hashPassword, verifyPassword, passwordProblem } from '../src/password.js';
import { verifyAssertion, verifyRegistration, rpIdFrom } from '../src/webauthn.js';

let R;
before(async () => { R = await startRelay(); });
after(() => R.close());

const post = (path, fields, cookie) => fetch(R.base + path, {
  method: 'POST',
  headers: { 'content-type': 'application/x-www-form-urlencoded', ...(cookie ? { cookie } : {}) },
  body: new URLSearchParams(fields), redirect: 'manual',
});

// --- passwords --------------------------------------------------------------

test('a stored password reveals nothing and still verifies', async () => {
  const stored = await hashPassword('correct-horse-battery');
  assert.ok(!stored.includes('correct-horse-battery'), 'the password is not in the record');
  assert.match(stored, /^scrypt\$16384\$8\$1\$/, 'parameters travel with the hash');
  assert.equal(await verifyPassword('correct-horse-battery', stored), true);
  assert.equal(await verifyPassword('correct-horse-batterY', stored), false);
});

test('two accounts with the same password get different records', async () => {
  const a = await hashPassword('same-password-here');
  const b = await hashPassword('same-password-here');
  assert.notEqual(a, b, 'salted, so one cracked hash does not crack the other');
});

test('a corrupt or empty record is a refusal, not a crash or a way in', async () => {
  for (const junk of ['', 'nonsense', 'scrypt$$$$', 'bcrypt$x$y', null, undefined]) {
    assert.equal(await verifyPassword('anything', junk), false, `refused: ${junk}`);
  }
});

test('length is what is asked for, not symbol soup', () => {
  assert.match(passwordProblem('short'), /at least 10/);
  assert.equal(passwordProblem('a reasonably long passphrase'), null);
  assert.match(passwordProblem('x'.repeat(201)), /longer than 200/);
});

// --- sign-in ----------------------------------------------------------------

test('signing up creates a session; the same address cannot be taken twice', async () => {
  const res = await post('/signup', { email: 'new@example.com', password: 'a-good-long-password' });
  assert.equal(res.status, 302);
  assert.match(res.headers.get('set-cookie') || '', /ink_session=/);

  const again = await post('/signup', { email: 'new@example.com', password: 'another-long-password' });
  assert.equal(again.status, 400);
  assert.match(await again.text(), /already an account/);
});

test('a wrong password and an unknown address fail identically', async () => {
  await post('/signup', { email: 'real@example.com', password: 'a-good-long-password' });
  const wrong = await post('/login', { email: 'real@example.com', password: 'not-the-password' });
  const missing = await post('/login', { email: 'ghost@example.com', password: 'not-the-password' });

  assert.equal(wrong.status, 400);
  assert.equal(missing.status, 400);
  // Any difference here is an oracle for which addresses have accounts.
  assert.equal(await wrong.text(), await missing.text(), 'same reply either way');
  assert.equal(wrong.headers.get('set-cookie'), null);
  assert.equal(missing.headers.get('set-cookie'), null);
});

test('a short password is refused at signup', async () => {
  const res = await post('/signup', { email: 'shorty@example.com', password: 'tiny' });
  assert.equal(res.status, 400);
  assert.match(await res.text(), /at least 10/);
  const login = await post('/login', { email: 'shorty@example.com', password: 'tiny' });
  assert.equal(login.headers.get('set-cookie'), null, 'no account was created');
});

test('repeated failures are throttled', async () => {
  await post('/signup', { email: 'target@example.com', password: 'a-good-long-password' });
  let throttled = false;
  for (let i = 0; i < 8; i++) {
    const res = await post('/login', { email: 'target@example.com', password: 'guess-' + i });
    if (/Too many attempts/.test(await res.text())) { throttled = true; break; }
  }
  assert.ok(throttled, 'online guessing is slowed down');
});

test('signup can be closed once the accounts that should exist do', async () => {
  process.env.INK_SIGNUP_CLOSED = '1';
  try {
    const page = await (await fetch(R.base + '/login')).text();
    assert.doesNotMatch(page, /Create one/, 'no invitation to sign up');
    const res = await post('/signup', { email: 'late@example.com', password: 'a-good-long-password' });
    assert.equal(res.headers.get('set-cookie'), null, 'and the endpoint refuses too');
  } finally { delete process.env.INK_SIGNUP_CLOSED; }
});

// --- passkeys ---------------------------------------------------------------
// A real authenticator, in miniature: a P-256 key that signs what a browser
// would sign, so the verifier is checked against genuine and forged responses.

const RP_ID = 'relay.test', ORIGIN = 'http://relay.test';

function authenticator() {
  const { publicKey, privateKey } = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  const spki = publicKey.export({ format: 'der', type: 'spki' });
  return {
    publicKey: spki.toString('base64url'),
    sign(challenge, { rpId = RP_ID, origin = ORIGIN, counter = 1, flags = 0x05 } = {}) {
      const authData = Buffer.concat([
        createHash('sha256').update(rpId).digest(),
        Buffer.from([flags]),
        (() => { const b = Buffer.alloc(4); b.writeUInt32BE(counter); return b; })(),
      ]);
      const clientDataJSON = Buffer.from(JSON.stringify({ type: 'webauthn.get', challenge, origin }));
      const signer = createSign('SHA256');
      signer.update(Buffer.concat([authData, createHash('sha256').update(clientDataJSON).digest()]));
      signer.end();
      return {
        authenticatorData: authData.toString('base64url'),
        clientDataJSON: clientDataJSON.toString('base64url'),
        signature: signer.sign({ key: privateKey, dsaEncoding: 'der' }).toString('base64url'),
      };
    },
  };
}

const assertWith = (a, challenge, opts = {}, storedCounter = 0) =>
  verifyAssertion({ ...a.sign(challenge, opts), publicKey: a.publicKey, expectedChallenge: challenge,
    origin: ORIGIN, rpId: RP_ID, storedCounter });

test('a genuine passkey assertion verifies', () => {
  const a = authenticator();
  const out = assertWith(a, 'chal-1', { counter: 7 });
  assert.equal(out.counter, 7);
});

test('a response for a different challenge is refused', () => {
  const a = authenticator();
  assert.throws(() => verifyAssertion({ ...a.sign('the-real-challenge'), publicKey: a.publicKey,
    expectedChallenge: 'a-different-challenge', origin: ORIGIN, rpId: RP_ID, storedCounter: 0 }),
    /challenge mismatch/, 'a captured response cannot be replayed against a new challenge');
});

test('a response from another origin is refused', () => {
  const a = authenticator();
  assert.throws(() => assertWith(a, 'chal-2', { origin: 'https://evil.example' }), /origin mismatch/);
});

test('a response for another relying party is refused', () => {
  const a = authenticator();
  assert.throws(() => assertWith(a, 'chal-3', { rpId: 'evil.example' }), /wrong relying party/);
});

test('someone else\'s key does not verify', () => {
  const real = authenticator(), impostor = authenticator();
  assert.throws(() => verifyAssertion({ ...impostor.sign('chal-4'), publicKey: real.publicKey,
    expectedChallenge: 'chal-4', origin: ORIGIN, rpId: RP_ID, storedCounter: 0 }), /did not verify/);
});

test('a tampered signature does not verify', () => {
  const a = authenticator();
  const r = a.sign('chal-5');
  const sig = Buffer.from(r.signature, 'base64url');
  sig[sig.length - 1] ^= 0xff;
  assert.throws(() => verifyAssertion({ ...r, signature: sig.toString('base64url'), publicKey: a.publicKey,
    expectedChallenge: 'chal-5', origin: ORIGIN, rpId: RP_ID, storedCounter: 0 }), /did not verify/);
});

test('a counter that goes backwards suggests a clone and is refused', () => {
  const a = authenticator();
  assert.throws(() => assertWith(a, 'chal-6', { counter: 3 }, 9), /counter went backwards/);
  // Authenticators that never increment report zero, which is not evidence.
  assert.doesNotThrow(() => assertWith(a, 'chal-7', { counter: 0 }, 0));
});

test('a response with no user present is refused', () => {
  const a = authenticator();
  assert.throws(() => assertWith(a, 'chal-8', { flags: 0x00 }), /no user presence/);
});

test('registration rejects a response built for another relying party', () => {
  assert.throws(() => verifyRegistration({
    attestationObject: Buffer.from('a0', 'hex').toString('base64url'),
    clientDataJSON: Buffer.from(JSON.stringify({ type: 'webauthn.create', challenge: 'c', origin: ORIGIN })).toString('base64url'),
    expectedChallenge: 'c', origin: ORIGIN, rpId: RP_ID,
  }));
});

test('the relying party is the relay host, not whatever is asked for', () => {
  assert.equal(rpIdFrom('https://relay.inkagent.dev'), 'relay.inkagent.dev');
  assert.equal(rpIdFrom('http://127.0.0.1:8787'), '127.0.0.1');
  assert.equal(rpIdFrom('nonsense'), 'localhost');
});

test('a passkey sign-in that did not start here is refused', async () => {
  const res = await fetch(R.base + '/auth/passkey/verify', {
    method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ id: 'whatever' }), redirect: 'manual',
  });
  assert.equal(res.status, 400);
  const body = await res.json();
  assert.match(body.error, /did not start here/);
});

test('the challenge endpoint issues a short-lived, single-use cookie', async () => {
  const res = await fetch(R.base + '/auth/passkey/options');
  const body = await res.json();
  assert.ok(body.challenge && body.challenge.length >= 40);
  assert.equal(body.rpId, 'relay.test');
  const cookie = res.headers.get('set-cookie') || '';
  assert.match(cookie, /ink_pk=/);
  assert.match(cookie, /HttpOnly/);
  assert.match(cookie, /Max-Age=300/);
});

test('registering a passkey requires being signed in', async () => {
  const res = await fetch(R.base + '/auth/passkey/register-options', { redirect: 'manual' });
  assert.equal(res.status, 302);
  assert.match(res.headers.get('location'), /^\/login/);
});
