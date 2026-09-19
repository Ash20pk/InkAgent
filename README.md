# InkAgent

An agentic e-reader. InkAgent is firmware for the Xteink X3, the X4 Classic and
X4 Pro, the Seeed Sticky and the M5Stack Paper Mono, plus the relay it talks to.
X3 and X4 share one image; the other boards have their own. Within an image the
differences that matter (panel size, touch controller, a frontlight, a
gyroscope, a real-time clock) are board capabilities rather than build flags.

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

**Moves books around.** Wi-Fi, USB, Calibre, OPDS, KOReader sync — and
reader-to-reader, with no computer and no account in between. *Your library*,
below, has all six.

**Shows you what you read.** Analytics keeps the measurement the reader was
already taking. The device times a sitting anyway, to decide whether a stretch
was slower than usual for you — that total used to be used once and thrown away.
Now it is kept: a week's bar chart, because a shape shows a habit in a way a
total never can, with the week's own total over it and both axes labelled — a
day gets a bar it can be read against rather than a mark to be guessed at. Under
it the rates, the lifetime total, and the books; a book's own menu opens straight
on that book. Days in a row is reported there too, and that is the whole of it:
no goal, no target, no quota and nothing that notifies. A device that argues
reading should not be gamified cannot then award points for it.

**Says what leaves.** Settings › Configure Relay › *What leaves this device* lists the
fields actually on the wire, what never leaves, and when anything is sent. It is
written from the request builders rather than from intent.

## The reader

**Typography you set from inside the book.** The reader's Text panel is four
tabs — Font, Size, Layout, Style. Font covers the built-in Noto Serif and Sans
and anything you drop on the card; Layout covers line spacing, extra paragraph
spacing, alignment (justify, left, centre, right, or the book's own) and the
screen margin; Style covers Focus Reading, hyphenation, whether the book's
embedded CSS wins, text anti-aliasing and a darker text weight for a tired
panel. A live specimen sits across the top third of the screen and re-lays
itself as you move, so you are judging a setting against typeset text rather
than against its name.

**Focus Reading** bolds the fixation point of each word. It is a Style row
rather than a mode, because it is the kind of thing you turn on once and forget
— some readers, ADHD readers especially, stay on the line with it.
`docs/focus-reading.md` has the rule it uses.

**Getting around a book.** Contents, footnotes that return you to where you
were, bookmarks, go-to-percent, and chapter skip on a long press. Auto page turn
runs at a rate you pick when your hands are busy. Progress is written to the card
as you go, so a flat battery costs you nothing.

**An offline dictionary.** StarDict files on the SD card, no connection and no
account — and every lookup is captured into Words on the way past, which is the
whole reason the word list is never something you have to fill in.
`docs/dictionary.md` covers the format and where to get dictionaries.

## Your library

Six ways in, none of which need an account:

| | |
|---|---|
| **Wi-Fi** | a web file manager with a QR to scan; drop files from any browser |
| **USB drive** | the reader mounts as a disk (on boards with USB-OTG) |
| **Calibre** | Calibre Wireless — stock Calibre's own device protocol, no plugin |
| **OPDS** | browse and download from any catalogue — Standard Ebooks, Gutenberg, your own |
| **KOReader sync** | progress follows you between devices, against any KOSync server |
| **Reader to reader** | one device opens File Transfer, the other picks a file and sends it |

Reader-to-reader is the one worth calling out: no computer, no account, no
internet, and nothing in between. (It is not AirDrop; AirDrop is AWDL, which is
Apple's and undocumented.)

## The interface

**One theme, built for the panel.** Canvas is what ships. E-ink costs are
proportional to how many pixels change, not to how much is drawn, so selection
is a 2 px underline rather than an inverted row — about a tenth of the pixels —
rows are square and gapless so a scroll shifts whole rows with no gap to
repaint, and structure is carried by grey rules and dithered grounds rather than
black boxes, because the panel resolves four greys and may as well use them.

**A status bar you decide.** What appears in the reader's foot is yours: battery
and its percentage, the chapter or the book title, page counts, book or chapter
progress, the progress bar itself, and — on a board with a real-time clock — the
time, in 12- or 24-hour, at an offset you set or synced from NTP.

**Controls that suit the board you have.** Front buttons can be remapped and can
follow the screen's orientation; the side buttons' layout is yours; a long press
can skip chapters or rotate the screen; the power button's short click is
assignable. On touch boards, tap or swipe zones drive the reader and the menus.
On the X3, the gyroscope turns pages when you tip the reader. The reader menu
comes as a plain list or as a bottom toolbar, whichever you prefer.

**The sleep screen is a setting, not a fixed image.** Reading canvas, the book's
own cover (fit or cropped, optionally contrast-filtered or inverted), or blank.

**Screens in 33 languages,** plus Orangutan. Translations are plain YAML in
`firmware/lib/I18n/translations/`; `docs/translators.md` is the guide, and
`docs/i18n.md` covers the string table and how the build generates it. Text entry
has its own keyboard layouts to match.

**Updates.** Over the air from the project's GitHub releases, or from a `.bin`
on the SD card. A crash lands on a screen that says what happened rather than on
a blank panel.

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

**You do not need a device.** `device-sim` speaks the exact protocol in
`PROTOCOL.md` — the same bytes the firmware sends — so the relay, the agent, the
evals and the whole wire contract can be worked on with nothing but Node 22.

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

**If you do have a device**, take the prebuilt image for your board from
[Releases](https://github.com/Ash20pk/InkAgent/releases) — no toolchain needed:

```
esptool.py --chip <esp32c3|esp32s3> --port /dev/ttyUSB0 \
  write_flash 0x0 inkagent-<board>.bin
```

Then pair it against the hosted relay at `relay.inkagent.dev`, or run your own
(`deploy/`). Building from source is below.

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

**Settings › Configure Relay** holds the relay address (default
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
firmware/src/activities/stats/             Analytics
firmware/src/activities/reader/            EPUB/TXT/XTC readers, toolbar, dictionary
firmware/src/activities/settings/          every settings screen, OTA, pairing
firmware/src/components/themes/canvas/     the theme that ships
firmware/src/SettingsList.h                one table, shared by the device UI and the web API
firmware/lib/I18n/translations/            33 languages, as YAML
```

`docs/` carries the detail: `USER_GUIDE.md`, `APP_DEVELOPMENT.md`,
`file-formats.md`, `sd-card-fonts.md`, `dictionary.md`, `focus-reading.md`,
`webserver-endpoints.md`, `i18n.md` and `troubleshooting.md`.

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

## Contributing

Most of InkAgent can be worked on without owning a reader — translations, the
relay, manifest apps, the evals, and every reader internal that the host tests
cover. [`CONTRIBUTING.md`](CONTRIBUTING.md) says which is which and how to run
each suite; [`firmware/docs/contributing/`](firmware/docs/contributing/) has the
architecture and the workflow.

Before building something large, read [`SCOPE.md`](SCOPE.md) — several of the
non-goals are the point of the project rather than gaps. Open a
[Discussion](https://github.com/Ash20pk/InkAgent/discussions) if an idea is near
a line. Security reports go through [`SECURITY.md`](SECURITY.md), not the issue
tracker.

## Credits and license

The reader core of the firmware (EPUB rendering, fonts, dictionary, sync, web
file manager) started from CrossPoint Reader by Dave Allie and contributors,
MIT licensed; its license is preserved in `firmware/LICENSE`. The hardware
abstraction is the FreeInk SDK (`firmware/freeink-sdk`, submodule). Icons are
generated from [Lucide](https://lucide.dev) (ISC). Everything else in this
repository is MIT licensed as well.
