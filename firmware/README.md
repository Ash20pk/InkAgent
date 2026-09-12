# InkAgent firmware

Firmware for the Xteink X3 (and X4 family) that turns the reader into a small
internet-connected agent. Open a book, pick **Ask the book**, and get an
explanation, a recap, a character reminder or a translation of the page you are
on, answered by whatever AI you connected to your relay. Pairing, keys and
traces live on the relay and its dashboard; the device only ever holds a token.

Relay, dashboard, simulator and evals: https://github.com/ash20pk/inkagent

## What is InkAgent-specific

```
lib/InkAgent/                         portable protocol (request builder, JSON extractor, pairing), host-tested
src/network/InkAgentClient.*          HTTPS layer, heap gate, SD log
src/activities/agent/AskBookActivity.*        kind menu → Wi-Fi → ask → answer
src/activities/settings/InkAgentSettingsActivity.*  relay address, pair / unpair, reset
src/activities/settings/InkAgentPairActivity.*      QR + code pairing (RFC 8628 shape)
test/ink_agent/                       gtest suite
```

Everything else is the reader: EPUB rendering, fonts, dictionary, KOReader sync,
OPDS, web file manager. That comes from InkAgent (see Credits) and is
kept close to upstream so fixes can be pulled in.

## Build and flash

Needs PlatformIO under Python ≥ 3.10.

```
pio run -e default -t upload          # ESP32-C3 image (X3 / X4)
pio run -e inkagent-check             # compile check without the IDF heap-tuning rebuild
cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test   # host tests
```

Back up the stock flash first: `esptool --port <port> read-flash 0 0x1000000 stock.bin`.

On the device: **Settings › Ask the book** sets the relay address (default
`https://relay.inkagent.dev`), pairs the reader, or unpairs it. The reader-menu
entry is grayed out until the reader is paired.

Failures write to `/.inkagent/inkagent.log` on the SD card, since USB serial
drops as soon as Wi-Fi starts on this board.

## Credits and license

InkAgent is a fork of [InkAgent](https://github.com/Ash20pk/InkAgent)
by Dave Allie and contributors, MIT licensed; the original license is in `LICENSE`
and the upstream README in `docs/UPSTREAM-CROSSPOINT-README.md`. The hardware
abstraction is the [FreeInk SDK](https://freeink.org). Settings and caches stay
under `/.inkagent/` on the SD card so a card moved from a InkAgent reader
keeps working.
