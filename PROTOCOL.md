# InkAgent device protocol v1

The device speaks to exactly one relay over HTTPS. Every body is compact JSON.
Responses are trimmed by the relay to the device's declared byte budget, so the
device never needs more than `budget + 512` bytes of heap for a response.

Headers on every device request:

    X-Ink-Proto: 1
    X-Ink-Device: <device_token>        (absent during pairing)
    Content-Type: application/json

## Pairing (device authorization flow, RFC 8628 shape)

    POST /v1/pair/start
      { "hw": "<hardware id, 12 hex>", "fw": "<firmware version>", "budget": 1536 }
    → 200 { "device_code": "...", "user_code": "ABCD-EFGH", "verify_url": "https://relay/claim",
            "verify_url_complete": "https://relay/claim?code=ABCD-EFGH", "interval": 5, "expires_in": 600 }

Device renders `verify_url_complete` as a QR plus `user_code` as text, then polls:

    POST /v1/pair/poll
      { "device_code": "..." }
    → 200 { "status": "pending" }
    → 200 { "status": "ok", "device_token": "...", "device_id": "...", "owner": "ash@..." }
    → 400 { "status": "expired" }        device restarts pairing
    → 400 { "status": "denied" }

User side: signed-in browser opens verify_url_complete and confirms.

## Ask the book

    POST /v1/ask
      { "kind": "explain" | "summary" | "who" | "translate" | "define",
        "book": "Title", "author": "Name", "chapter": "Chapter 3", "pct": 42,
        "text": "<selected passage or page text, ≤ 2000 bytes>",
        "arg": "<optional: target language for translate, headword for define>" }
    → 200 { "text": "<answer, ≤ budget bytes>", "sid": "<session id>", "trunc": false }
    → 402 { "error": "no_provider", "text": "<human message for the screen>" }
    → 401 { "error": "revoked" }         device wipes token, returns to pairing
    → 503 { "error": "provider", "text": "<human message>" }

`text` is always present on error responses so the device can render it without
special cases. `trunc` tells the device the answer was cut to fit; the full
answer is on the dashboard under `sid`.

## Config and briefs

    GET  /v1/config       → { "budget": 1536, "brief_interval_s": 21600, "cards": [ {id,name,kind} ... ] }
    GET  /v1/brief/next   → { "text": "...", "sid": "...", "at": <unix> } or 204

## Rules

- Relay never returns more than `budget` bytes in `text`, cut at a UTF-8 boundary
  and preferably a sentence boundary.
- Relay never returns provider error bodies to the device.
- Device stores only the device token. Provider keys never leave the relay.
