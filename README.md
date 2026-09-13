# InkAgent

An agentic e-reader. InkAgent is firmware for the Xteink X3 (and X4 family) plus
the relay it talks to.

Its premise is unusual for an AI reading tool: **the agent asks rather than
tells.** A pre-registered trial of 405 students found that reading with an LLM
produced *worse* comprehension and worse three-day retention than plain
note-taking — while readers preferred the LLM and rated it more helpful. What
does hold is retrieval practice and self-explanation. So the default action on a
passage is **Question me on this**, and the relay's agent is forbidden from
answering its own question. Explain, recap and translate are still there; they
are no longer the first thing offered.

You bring your own AI: an API key, an OpenAI-compatible endpoint such as Ollama,
or your own agent. The device only ever holds a pairing token. Keys and traces
live on the relay you run, or the hosted one at `relay.inkagent.dev`.

```
firmware/       device firmware (PlatformIO, ESP32-C3), host tests under firmware/test
firmware/examples/apps/   example manifest apps, ready to copy onto a card
relay/          Node 22, zero dependencies: pairing, agent, app hosting, auth, dashboard
deploy/         Docker Compose + Caddy for a hosted relay with automatic HTTPS
device-sim/     simulator that speaks the exact device protocol
evals/          offline evals (protocol, fit, quality rubric, resilience); --live for a real model
PROTOCOL.md     device ↔ relay contract v1
```

## What it does

**Reads books.** EPUB, TXT, Markdown and XTC, plus BMP, PNG, JPEG and Xteink's
own `.xth`/`.xtg` page images rendered in four greys. Paged, never scrolling —
the screen-versus-paper comprehension gap is largely a scrolling effect, and a
paged e-ink device is structurally exempt.

**Asks you about what you read.** Reader → Ask → *Question me on this* sends the
passage to your relay, whose agent returns one question and is refused if it
answers instead. The reader also measures two things from page turns alone — how
often you went back over a stretch, and your pace against a baseline — and those
steer which part of the passage the question targets. No sensors, and only the
two derived numbers ever leave the device.

**Remembers words for you.** Every dictionary lookup is captured with the book it
came from and scheduled for retrieval at 1, 3, 7, 21 and 60 days. Looking a word
up again resets it, since the lookup is itself evidence it did not stick. The
queue is finite and empties, and no count of it appears anywhere else.

**Gathers what you marked.** Highlights collects bookmarks from every book into
one list — they are otherwise stored per book and invisible from outside it.

**Holds a reading inbox.** Drop articles into `/ReadLater` and they wait there.
Nothing arrives on its own; removing a piece once read is a button, because a
pile that never shrinks is a different kind of guilt.

**Uses the sleep screen for something.** The one thing this panel does that
nothing else can is hold an image at zero power. *Reading canvas* spends that on
your book, your progress, a word due for recall and the agent's last question,
instead of a wallpaper. It renders from cache and cannot bring Wi-Fi up.

**Moves books around.** File transfer over Wi-Fi with a QR to scan, USB drive
mode, Calibre wireless — and reader-to-reader: one device opens File Transfer,
the other picks a file and sends it directly. No computer, no account, no
internet. (It is not AirDrop; AirDrop is AWDL, which is Apple's and
undocumented.)

**Says what leaves.** Settings › Ask the book › *What leaves this device* lists the
fields actually on the wire, what never leaves, and when anything is sent. It is
written from the request builders rather than from intent.

## Apps

The drawer lists three kinds of app, identically:

| Kind | Ships as | Needs a firmware build |
|---|---|---|
| Built-in | C++ activity | — |
| Registered | C++ under `src/apps/`, one macro | yes |
| **Manifest** | **a JSON file in `/Apps`** | **no** |

A manifest describes a screen — rows and bindings — and that is all it can do.
There is no `if`, no loop, no expression, and no way to name a data source the
firmware does not already expose. A third-party app therefore cannot poll,
notify, accumulate an unread count, or follow you into a book. The ceiling is
the feature.

```json
{
  "name": "Status", "icon": "info", "title": "Status",
  "rows": [
    {"kind": "para", "text": "Everything here is read from the device itself.", "maxLines": 3},
    {"kind": "kv", "label": "Reading", "value": {"src": "reading.title"}},
    {"kind": "kv", "label": "Battery", "value": {"src": "device.battery"}}
  ]
}
```

Copy that to `/Apps/status.json` and it appears in the drawer. Or write it on the
relay dashboard under **Apps**, where it is validated before it ships, and sync
it to your readers over the air. `firmware/docs/APP_DEVELOPMENT.md` is the guide;
`firmware/examples/apps/` has working ones.

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
./bin/clang-format-fix -g             # format changed files
```

Back up the stock flash first: `esptool --port <port> read-flash 0 0x1000000 stock.bin`.

**Settings › Ask the book** holds the relay address (default
`https://relay.inkagent.dev`), pairs or unpairs the reader, syncs apps, and shows
what leaves the device. Failures are written to `/.inkagent/inkagent.log` on the
SD card, because USB serial drops as soon as Wi-Fi starts on this board.

The device has ~380 KB of RAM and no PSRAM, which decides most of the
architecture: the relay composes screens because the device cannot run a model;
manifests are parsed into a fixed 4 KB struct and the JSON is freed before
anything is drawn; only one TLS session exists at a time, foreground or
background, because two would need ~80 KB.

```
firmware/lib/InkAgent/                     protocol: request builders, JSON extractors, pairing
firmware/src/network/InkAgentClient.*      HTTPS, CA pinning, heap gate, SD log
firmware/src/network/RelayTask.*           all relay traffic, off the UI thread
firmware/src/engage/                       manifest parser, screen model, renderer, app catalog
firmware/src/apps/                         the in-tree app SDK (INKAGENT_REGISTER_APP)
firmware/src/activities/agent/             Ask the book / Question me on this
firmware/src/activities/words/             word list and its scheduler
firmware/src/activities/highlights/        cross-book bookmarks
firmware/src/activities/readlater/         the reading inbox
firmware/src/activities/network/PeerSend*  reader-to-reader transfer
```

## Hosted relay

`deploy/` runs the relay behind Caddy on any Docker host; see `deploy/README.md`
— including why the deploy rsync must never use `--delete`, and how to take a
backup that actually contains the data (SQLite runs in WAL mode here).

Sign-in is an email and a password, hashed with scrypt, plus **passkeys** —
WebAuthn, verified on the relay, so a signed-in device needs no password at all.
Set `INK_SIGNUP_CLOSED=1` once your account exists. The device pins ISRG Root X1
and X2 rather than trusting any certificate.

Exchanges are not stored. There is no traces page: the relay keeps which device
asked, which app, how long it took and whether the answer was trimmed, and none
of the passage or the answer.

Endpoints: `/v1/pair/*`, `/v1/config`, `/v1/ask`, `/v1/engage` (the agent
composes a screen), `/v1/apps` (an index, then one manifest at a time — the
whole set inline would not fit beside a TLS session).

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

The relay's own suite (`cd relay && npm test`) additionally covers the agent
refusing to answer its own question, manifests that try to branch being
rejected, and one owner never being served another's apps.

## Credits and license

The reader core of the firmware (EPUB rendering, fonts, dictionary, sync, web
file manager) started from CrossPoint Reader by Dave Allie and contributors,
MIT licensed; its license is preserved in `firmware/LICENSE`. The hardware
abstraction is the FreeInk SDK (`firmware/freeink-sdk`, submodule). Icons are
generated from [Lucide](https://lucide.dev) (ISC). Everything else in this
repository is MIT licensed as well.
