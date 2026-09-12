# InkAgent

An agentic e-reader. InkAgent is firmware for the Xteink X3 (and X4 family) plus
the relay it talks to. Open a book, choose **Ask the book**, and the page you are
on is explained, recapped, or translated by whatever AI you connected. Users
bring their own AI: an API key, an OpenAI-compatible endpoint such as Ollama, or
later their own agent. The device only ever holds a pairing token; keys and
traces live on the relay you run or the hosted one at `relay.inkagent.dev`.

```
firmware/       device firmware (PlatformIO, ESP32-C3), host tests under firmware/test
relay/          Node 22, zero dependencies: pairing, provider adapter, trimming, dashboard, traces
deploy/         Docker Compose + Caddy for a hosted relay with automatic HTTPS
device-sim/     simulator that speaks the exact device protocol
evals/          offline evals (protocol, fit, quality rubric, resilience); --live for a real model
PROTOCOL.md     device ↔ relay contract v1
```

## Quick start

```
# relay
cd relay && npm test && PUBLIC_URL=http://localhost:8787 node --experimental-sqlite src/server.js
# pretend to be a reader
cd device-sim && node sim.js pair && node sim.js ask --kind explain --text "..."
# evals
node --experimental-sqlite evals/run.js
```

Dashboard: sign in, claim a reader with the code it shows, add a key under
**Your AI**. Groq and Gemini free tiers work; Ollama works with an empty key.

## Firmware

Needs PlatformIO under Python ≥ 3.10.

```
cd firmware
pio run -e inkagent-check             # compile check without the IDF heap-tuning rebuild
pio run -e default -t upload          # flash the ESP32-C3 image
cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test
```

Back up the stock flash first: `esptool --port <port> read-flash 0 0x1000000 stock.bin`.

On the reader, **Settings › Ask the book** holds the relay address (default
`https://relay.inkagent.dev`), pairs or unpairs the reader, and resets the
address. The reader-menu entry is grayed out until the reader is paired.
Failures are written to `/.inkagent/inkagent.log` on the SD card, because USB
serial drops as soon as Wi-Fi starts on this board.

InkAgent-specific firmware code:

```
firmware/lib/InkAgent/                              protocol: request builder, JSON extractor, pairing parsers
firmware/src/network/InkAgentClient.*               HTTPS layer, heap gate, SD log
firmware/src/activities/agent/AskBookActivity.*     kind menu → Wi-Fi → ask → answer
firmware/src/activities/settings/InkAgentSettingsActivity.*   relay address, pair, unpair
firmware/src/activities/settings/InkAgentPairActivity.*       QR + code pairing
```

## Hosted relay

`deploy/` runs the relay behind Caddy on any Docker host; see `deploy/README.md`.
Sign-in is gated by `INK_ACCESS_CODE` until real OAuth lands.

## Evals

| Suite | What must hold |
|---|---|
| pairing | user code avoids 0/O/1/I; claim URL fits a QR; pending → ok; token cannot be replayed |
| protocol | every ask returns a session id; wire response ≤ budget + 512 B |
| fit | answer ≤ 1536 B; ≤ 2 panel pages; no markdown; no chat preamble; ends on punctuation or flagged truncated |
| quality | word cap; required names; no spoilers past the reader's position; translations in the target script |
| resilience | no provider / 500 / 429 / bad key / timeout / revoked token all yield one page of friendly text |
| latency | p95 under 500 ms offline, 15 s live |
| meta | the rubric catches a deliberately bad answer |

## Credits and license

The reader core of the firmware (EPUB rendering, fonts, dictionary, sync, web
file manager) started from CrossPoint Reader by Dave Allie and contributors,
MIT licensed; its license is preserved in `firmware/LICENSE`. The hardware
abstraction is the FreeInk SDK (`firmware/freeink-sdk`, submodule). Everything
else in this repository is MIT licensed as well.
