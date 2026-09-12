# InkAgent firmware

Device firmware for the Xteink X3 / X4 family (ESP32-C3). See the repository
README one level up for the full picture, build steps and the relay.

```
pio run -e inkagent-check             # compile check
pio run -e default -t upload          # flash
cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test
```

The reader core (EPUB rendering, fonts, dictionary, sync, web file manager)
started from CrossPoint Reader by Dave Allie and contributors, MIT licensed;
the license is preserved in `LICENSE`. Hardware abstraction: FreeInk SDK
(`freeink-sdk`, git submodule).
