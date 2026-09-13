// Passkeys, verified here rather than trusted from the browser.
//
// Only the slice of WebAuthn this relay needs: register a credential, then
// prove possession of it at sign-in. No attestation is checked — the relay does
// not care which authenticator someone uses, only that the same one comes back
// — so registration reduces to reading a public key out of the response, and
// sign-in to one signature check.
//
// Written against node:crypto so the relay keeps its no-dependency rule. That
// means a small CBOR reader, because the two things worth having out of a
// WebAuthn response are both CBOR-encoded and nothing else here needs it.
import { createHash, createVerify, randomBytes, createPublicKey } from 'node:crypto';

export const challenge = () => randomBytes(32).toString('base64url');
const b64u = (buf) => Buffer.from(buf).toString('base64url');
const fromB64u = (s) => Buffer.from(String(s || ''), 'base64url');

// --- CBOR, only the shapes WebAuthn produces --------------------------------
// Maps, byte strings, text strings, unsigned and negative integers, arrays.
// Anything else throws, which is the right answer for input this specific.
function cborRead(buf, pos) {
  const first = buf[pos++];
  const major = first >> 5, minor = first & 0x1f;

  let len = minor;
  if (minor === 24) len = buf[pos++];
  else if (minor === 25) { len = buf.readUInt16BE(pos); pos += 2; }
  else if (minor === 26) { len = buf.readUInt32BE(pos); pos += 4; }
  else if (minor >= 28) throw new Error('cbor: unsupported length');

  switch (major) {
    case 0: return [len, pos];                       // unsigned
    case 1: return [-1 - len, pos];                  // negative
    case 2: return [buf.subarray(pos, pos + len), pos + len];       // bytes
    case 3: return [buf.toString('utf8', pos, pos + len), pos + len]; // text
    case 4: {                                        // array
      const out = [];
      for (let i = 0; i < len; i++) { const [v, np] = cborRead(buf, pos); out.push(v); pos = np; }
      return [out, pos];
    }
    case 5: {                                        // map
      const out = new Map();
      for (let i = 0; i < len; i++) {
        const [k, kp] = cborRead(buf, pos);
        const [v, vp] = cborRead(buf, kp);
        out.set(k, v); pos = vp;
      }
      return [out, pos];
    }
    default: throw new Error('cbor: unsupported major type ' + major);
  }
}
const cborDecode = (buf) => cborRead(buf, 0)[0];

// --- COSE public key -> something node can verify with ----------------------
// Only ES256 (P-256) and RS256, which is what browsers and platform
// authenticators actually produce.
function coseToKey(cose) {
  const kty = cose.get(1), alg = cose.get(3);
  if (kty === 2 && alg === -7) {
    const x = cose.get(-2), y = cose.get(-3);
    if (!x || !y || x.length !== 32 || y.length !== 32) throw new Error('bad EC key');
    // SPKI wrapper for prime256v1, with the uncompressed point appended.
    const prefix = Buffer.from('3059301306072a8648ce3d020106082a8648ce3d030107034200', 'hex');
    const der = Buffer.concat([prefix, Buffer.from([0x04]), x, y]);
    return { key: createPublicKey({ key: der, format: 'der', type: 'spki' }), alg: 'ES256' };
  }
  if (kty === 3 && alg === -257) {
    const n = cose.get(-1), e = cose.get(-2);
    if (!n || !e) throw new Error('bad RSA key');
    const int = (b) => { const t = Buffer.from(b); return t[0] & 0x80 ? Buffer.concat([Buffer.from([0]), t]) : t; };
    const tlv = (tag, body) => {
      const len = body.length;
      const head = len < 128 ? Buffer.from([tag, len])
        : len < 256 ? Buffer.from([tag, 0x81, len])
        : Buffer.from([tag, 0x82, len >> 8, len & 0xff]);
      return Buffer.concat([head, body]);
    };
    const seq = tlv(0x30, Buffer.concat([tlv(0x02, int(n)), tlv(0x02, int(e))]));
    const spki = tlv(0x30, Buffer.concat([
      Buffer.from('300d06092a864886f70d0101010500', 'hex'),
      tlv(0x03, Buffer.concat([Buffer.from([0]), seq])),
    ]));
    return { key: createPublicKey({ key: spki, format: 'der', type: 'spki' }), alg: 'RS256' };
  }
  throw new Error('unsupported key algorithm');
}

// --- authenticatorData ------------------------------------------------------
// rpIdHash(32) flags(1) counter(4) [ aaguid(16) credIdLen(2) credId rest ]
function parseAuthData(authData) {
  const rpIdHash = authData.subarray(0, 32);
  const flags = authData[32];
  const counter = authData.readUInt32BE(33);
  const out = { rpIdHash, flags, counter, userPresent: !!(flags & 0x01), userVerified: !!(flags & 0x04) };
  if (flags & 0x40) {
    const credIdLen = authData.readUInt16BE(53);
    out.credId = authData.subarray(55, 55 + credIdLen);
    out.coseKey = cborDecode(authData.subarray(55 + credIdLen));
  }
  return out;
}

function checkClientData(clientDataJSON, { type, expectedChallenge, origin }) {
  const data = JSON.parse(Buffer.from(clientDataJSON).toString('utf8'));
  if (data.type !== type) throw new Error('wrong ceremony type');
  // The challenge is what makes this not a replay: it was minted here, for this
  // attempt, and is used once.
  if (data.challenge !== expectedChallenge) throw new Error('challenge mismatch');
  if (data.origin !== origin) throw new Error('origin mismatch');
  return data;
}

export function rpIdFrom(publicUrl) {
  try { return new URL(publicUrl).hostname; } catch { return 'localhost'; }
}

// Registration: pull the credential id and public key out of the response.
export function verifyRegistration({ attestationObject, clientDataJSON, expectedChallenge, origin, rpId }) {
  checkClientData(fromB64u(clientDataJSON), { type: 'webauthn.create', expectedChallenge, origin });
  const att = cborDecode(fromB64u(attestationObject));
  const parsed = parseAuthData(att.get('authData'));
  if (!parsed.credId || !parsed.coseKey) throw new Error('no credential in response');
  if (!parsed.userPresent) throw new Error('no user presence');
  if (!parsed.rpIdHash.equals(createHash('sha256').update(rpId).digest())) throw new Error('wrong relying party');
  // Stored as SPKI so sign-in does not have to understand COSE again.
  const { key } = coseToKey(parsed.coseKey);
  return {
    credId: b64u(parsed.credId),
    publicKey: key.export({ format: 'der', type: 'spki' }).toString('base64url'),
    counter: parsed.counter,
  };
}

// Sign-in: the signature covers authenticatorData followed by the hash of
// clientDataJSON, which is what ties it to this challenge and this origin.
export function verifyAssertion({ authenticatorData, clientDataJSON, signature, publicKey, expectedChallenge, origin, rpId, storedCounter }) {
  const authData = fromB64u(authenticatorData);
  const clientData = fromB64u(clientDataJSON);
  checkClientData(clientData, { type: 'webauthn.get', expectedChallenge, origin });

  const parsed = parseAuthData(authData);
  if (!parsed.userPresent) throw new Error('no user presence');
  if (!parsed.rpIdHash.equals(createHash('sha256').update(rpId).digest())) throw new Error('wrong relying party');

  const key = createPublicKey({ key: fromB64u(publicKey), format: 'der', type: 'spki' });
  const signed = Buffer.concat([authData, createHash('sha256').update(clientData).digest()]);
  const verifier = createVerify('SHA256');
  verifier.update(signed);
  verifier.end();
  const sig = fromB64u(signature);
  const ok = key.asymmetricKeyType === 'ec'
    ? verifier.verify({ key, dsaEncoding: 'der' }, sig)
    : verifier.verify(key, sig);
  if (!ok) throw new Error('signature did not verify');

  // A counter that goes backwards suggests a cloned authenticator. Many real
  // ones never increment it at all, so zero is not evidence of anything.
  if (parsed.counter !== 0 && storedCounter !== 0 && parsed.counter <= storedCounter) {
    throw new Error('authenticator counter went backwards');
  }
  return { counter: parsed.counter };
}
