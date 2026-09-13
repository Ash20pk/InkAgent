// Password storage.
//
// scrypt from node:crypto, so the relay keeps its no-dependency rule while
// still using a memory-hard function — the point of which is that a stolen
// database cannot be run through a GPU cheaply.
//
// Stored as one self-describing string: the parameters travel with the hash,
// so they can be raised later without stranding the accounts hashed under the
// old ones.
import { scrypt, randomBytes, timingSafeEqual } from 'node:crypto';

const N = 16384, r = 8, p = 1, KEYLEN = 64;

const derive = (password, salt, params) => new Promise((resolve, reject) => {
  // maxmem must be raised explicitly: the default is below what N=16384 needs.
  scrypt(password, salt, KEYLEN, { N: params.N, r: params.r, p: params.p, maxmem: 256 * 1024 * 1024 },
    (err, key) => (err ? reject(err) : resolve(key)));
});

export async function hashPassword(password) {
  const salt = randomBytes(16);
  const key = await derive(password, salt, { N, r, p });
  return `scrypt$${N}$${r}$${p}$${salt.toString('base64url')}$${key.toString('base64url')}`;
}

// Constant-time where it matters, and false rather than throwing on anything
// malformed: a corrupt row must not become a way in.
export async function verifyPassword(password, stored) {
  try {
    const [scheme, sN, sr, sp, salt64, key64] = String(stored || '').split('$');
    if (scheme !== 'scrypt') return false;
    const params = { N: Number(sN), r: Number(sr), p: Number(sp) };
    if (!params.N || !params.r || !params.p) return false;
    const expected = Buffer.from(key64, 'base64url');
    const actual = await derive(password, Buffer.from(salt64, 'base64url'), params);
    return expected.length === actual.length && timingSafeEqual(expected, actual);
  } catch {
    return false;
  }
}

// Long enough to be worth hashing, short enough that a paste of a password
// manager entry is not rejected. No composition rules: they push people towards
// predictable substitutions and away from length, which is what actually helps.
export function passwordProblem(password) {
  const s = String(password || '');
  if (s.length < 10) return 'Use at least 10 characters.';
  if (s.length > 200) return 'That is longer than 200 characters.';
  return null;
}
