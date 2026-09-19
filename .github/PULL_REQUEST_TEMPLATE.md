## What this changes

<!-- One paragraph. What behaviour is different after this lands. -->

## Why

<!-- The problem. If it fixes an issue, "Fixes #123". If it is a design
     decision, say what you rejected and why — that is the part review needs. -->

## How it was tested

<!-- Delete what does not apply. An honest "not verified on hardware" is fine
     and far better than an implied one. -->

- [ ] Host tests pass (`cmake --build firmware/build/test && ctest --test-dir firmware/build/test`)
- [ ] Relay tests pass (`cd relay && npm test`)
- [ ] Evals pass (`cd evals && npm run eval`)
- [ ] Ran against the simulator (`device-sim`)
- [ ] Flashed and used on hardware — board: <!-- X3 / X4 / X4C / X4 Pro / Sticky / Paper Mono -->
- [ ] Not verified on hardware, because: <!-- … -->

## Checks

- [ ] Formatted (`cd firmware && ./bin/clang-format-fix`), or the repo hooks are enabled
- [ ] Within scope — see `SCOPE.md`, particularly the non-goals
- [ ] Flash and RAM impact considered; if it is not trivial, the numbers are below
- [ ] Anything that changes what leaves the device is reflected in
      Settings › InkAgent › *What leaves this device*

## Footprint

<!-- For anything touching firmware. `pio run -e inkagent-check` prints both.
     Only needed if it moved. -->

| | before | after |
|---|---|---|
| Flash | | |
| RAM | | |
