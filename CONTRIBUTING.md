# Contributing to InkAgent

Most of InkAgent can be worked on **without owning an e-reader**. That is worth
saying first, because "needs a $90 device" is the assumption that stops people
before they start, and it is only true for part of the project.

| If you want to work on | You need |
|---|---|
| Translations (33 languages, YAML) | a text editor |
| The relay, pairing, the agent, the dashboard | Node 22 |
| Manifest apps | Node 22, or a card and a reader |
| Evals and the wire protocol | Node 22 |
| Reader internals — EPUB, typography, hyphenation, dithering | a C++ toolchain; the host tests run on your laptop |
| Anything touching the panel, touch, power, or a new board | the hardware |

---

## Five minutes, no hardware

```sh
git clone --recursive https://github.com/Ash20pk/InkAgent
cd InkAgent

# 1. Run a relay locally (Node 22.13+, zero dependencies)
cd relay && npm start          # http://localhost:8787

# 2. In another shell, pair a simulated reader and ask it something
cd device-sim
node sim.js pair               # prints the claim URL and the user code
node sim.js ask --kind question --text "…a passage…"
```

`device-sim` speaks the exact device protocol in `PROTOCOL.md` — the same bytes
the firmware sends. If a change works against the simulator, it works against
the reader, for everything above the panel.

## Running the tests

```sh
# Firmware host tests — GoogleTest, no device, ~35 suites
cmake -S firmware/test -B firmware/build/test
cmake --build firmware/build/test --parallel
ctest --test-dir firmware/build/test --output-on-failure

# Relay
cd relay && npm test

# Offline evals (protocol, fit, quality, resilience, latency, meta)
cd evals && npm run eval       # add --live for a real provider, needs a key
```

All four run in CI on every pull request, along with a compile of each board
image and a `clang-format` check.

## Building firmware

Prerequisites, and the `clang-format` 21 install if you need it, are in
[`firmware/docs/contributing/getting-started.md`](firmware/docs/contributing/getting-started.md).

Pick the environment for your board:

| Board | Build | Release build |
|---|---|---|
| Xteink X3, X4 | `default` | `gh_release` |
| Xteink X4 Classic | `x4c` | `x4c-gh_release` |
| Xteink X4 Pro | `x4pro` | `x4pro-gh_release` |
| Seeed Sticky | `sticky` | `sticky-gh_release` |
| M5Stack Paper Mono | `papermono` | `papermono-gh_release` |

```sh
cd firmware
pio run -e x4pro -t upload
```

`pio run -e inkagent-check` compiles the X3/X4 sources **without** the custom SDK
rebuild. It is the fastest way to check that firmware still builds, and it is
what CI uses; flash `default`, not this.

Enable the repo hooks once per clone — this is what keeps formatting out of
review:

```sh
cd firmware
git config core.hooksPath .githooks
chmod +x .githooks/pre-commit
```

Prebuilt binaries for every board are attached to each
[release](https://github.com/Ash20pk/InkAgent/releases), if you only want to run
it.

## Where things live

```
firmware/       device firmware (PlatformIO, ESP32-C3 / S3)
firmware/test/  host tests — these run without hardware
firmware/lib/I18n/translations/   33 languages, as YAML
firmware/examples/apps/           example manifest apps
relay/          Node 22, zero dependencies
device-sim/     simulator speaking the real device protocol
evals/          offline evals
PROTOCOL.md     the device ↔ relay contract
```

Deeper detail is in [`firmware/docs/contributing/`](firmware/docs/contributing/):
[architecture](firmware/docs/contributing/architecture.md),
[development workflow](firmware/docs/contributing/development-workflow.md),
[testing and debugging](firmware/docs/contributing/testing-debugging.md),
[touch and UI](firmware/docs/contributing/touch-and-ui.md).

---

## Good places to start

**Translations.** `firmware/lib/I18n/translations/*.yaml`, one file per
language. [`docs/i18n.md`](firmware/docs/i18n.md) and
[`docs/translators.md`](firmware/docs/translators.md) explain the format and
what a missing key does. No hardware, no build, and review is quick — which is
why it is the best first contribution.

**Manifest apps.** [`docs/APP_DEVELOPMENT.md`](firmware/docs/APP_DEVELOPMENT.md),
examples in `firmware/examples/apps/`. Declarative, no control flow, validated
by the relay.

**A board port.** Open a board-support issue first. The HAL boundary means most
of a port is a `BoardConfig` profile.

**Data.** Dictionaries, fonts, hyphenation tries — see
[`dictionary.md`](firmware/docs/dictionary.md),
[`sd-card-fonts.md`](firmware/docs/sd-card-fonts.md) and
[`hyphenation-trie-format.md`](firmware/docs/hyphenation-trie-format.md).

Issues labelled [`good first issue`](https://github.com/Ash20pk/InkAgent/labels/good%20first%20issue)
name the file to start in and how to test the result.

---

## Before you build something large

Read [`SCOPE.md`](SCOPE.md). It says what InkAgent is for and, more usefully,
what it deliberately will not have. Several of the non-goals are the *point* of
the project rather than things nobody got round to — notably that the agent
never answers its own question, and that reading is never gamified. A PR adding
streaks, badges, targets or notifications will be declined however well it is
written, and it is unfair to let you find that out at review.

If an idea is near a line, open a
[Discussion](https://github.com/Ash20pk/InkAgent/discussions) before writing the
code.

## Pull requests

- Branch off `main`. One concern per PR.
- Commit messages: `type(scope): what changed`, e.g.
  `fix(reader): hyphenation dropped the last soft break on justified lines`.
  Say what changed, in the imperative, in the subject.
- Fill in the PR template. The "how it was tested" section is the one that
  matters — **"not verified on hardware" is a perfectly good answer** and much
  better than an implied yes.
- Report footprint for firmware changes. The ESP32-C3 has ~380 KB of RAM and no
  PSRAM; flash sits around 85% full. A few KB is a real cost here.
- Expect review to ask about memory before it asks about style.

## Licence

MIT. There is no CLA — by opening a pull request you are contributing your work
under the repository's licence.

The reader core started from CrossPoint Reader by Dave Allie and contributors
(MIT, preserved in `firmware/LICENSE`); the hardware abstraction is the FreeInk
SDK; icons are generated from [Lucide](https://lucide.dev) (ISC).
