# Security Policy

## Reporting a vulnerability

**Do not open a public issue.** Report privately through
[GitHub Security Advisories](https://github.com/Ash20pk/InkAgent/security/advisories/new),
or by email to **aswin@openbox.ai**.

Please include what you can: the affected component, the version or commit, what
an attacker gets, and how to reproduce it. A proof of concept helps but is not
required to file.

You should get an acknowledgement within 72 hours and an assessment within a
week. If a fix is warranted we will agree a disclosure date with you, and credit
you in the advisory unless you would rather we did not.

## Scope

In scope:

- **Firmware** — the TLS client and its CA pinning, the pairing flow and the
  device token, the on-device web server and file manager, OTA, manifest parsing
  and the file formats the reader accepts (EPUB, XTC, XTH/XTG, PNG, JPEG, BMP).
  A malformed book that gets code execution is the interesting case here.
- **Relay** — pairing and token issuance, authentication (scrypt passwords,
  WebAuthn passkeys), app hosting and the manifest validator, and any path that
  serves one owner's data to another.
- **Deploy** — the Compose and Caddy configuration in `deploy/`.

Out of scope:

- The FreeInk SDK (`firmware/freeink-sdk`), which is maintained separately —
  report those upstream.
- Vendor stock firmware, and physical attacks that require opening the device.
- Anything requiring an already-flashed malicious firmware image.

## What the design already assumes

Useful context for judging severity — these are deliberate, and a report that
one of them does not hold is a real finding:

- **The device holds only a pairing token.** Provider API keys live on the relay,
  never on the reader.
- **The relay does not store exchanges.** It keeps which device asked, which app,
  how long it took, and whether the answer was trimmed — not the passage and not
  the answer.
- **The device pins ISRG Root X1 and X2** rather than trusting the system trust
  store or any certificate. `INKAGENT_RELAY_INSECURE` disables this and is off by
  default; it exists for local development against a self-hosted relay.
- **Two derived integers leave the device** from behavioural measurement —
  regressions and pace against baseline. Raw timings stay on the card. What is on
  the wire is enumerated in Settings › InkAgent › *What leaves this device*, and
  that screen is written from the request builders.
- **Manifest apps have no control flow**, by design. A manifest that achieves
  branching, reaches a data source outside the whitelist, or resolves an asset
  path is a vulnerability, not a feature.

## Self-hosted relays

If you run your own relay, set `INK_SIGNUP_CLOSED=1` once your account exists.
An open relay with a provider key configured is an open proxy to your own
billing account.
