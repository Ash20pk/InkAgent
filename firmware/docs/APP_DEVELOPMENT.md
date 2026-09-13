# Building apps for InkAgent

An InkAgent app is a screen that appears in the app drawer. It is an ordinary
`Activity` that announces itself to the firmware at link time, so adding one
touches no shared file: you create a folder under `src/apps/`, subclass
`InkApp`, and register the class once.

Apps are compiled into the firmware. There is no dynamic loading — the ESP32-C3
has ~380KB of RAM and no PSRAM, and a script interpreter plus per-app heap does
not fit alongside the EPUB engine. Shipping an app therefore means building and
flashing the firmware that contains it.

## The shortest possible app

```
src/apps/my_app/
  MyApp.h
  MyApp.cpp
```

```cpp
// MyApp.h
#pragma once
#include "apps/InkApp.h"

class MyApp : public InkApp {
 public:
  MyApp(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
```

```cpp
// MyApp.cpp
#include "MyApp.h"
#include <GfxRenderer.h>
#include "components/UITheme.h"
#include "components/icons/drawerIcons.h"
#include "fontIds.h"

MyApp::MyApp(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : InkApp("MyApp", renderer, mappedInput) {}

void MyApp::onEnter() { Activity::onEnter(); requestUpdate(); }

void MyApp::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.clearScreen();
  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 "My App");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void MyApp::loop() {
  Activity::loop();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
}

INKAGENT_REGISTER_APP(MyApp, "My App", &icon_apps_32);
```

Build with `pio run`. The app is now the last tile in the drawer.

`src/apps/hello_world/` is a working copy of the above with a tap counter. It is
compiled out by default — it is scaffolding, not a shipped feature — so enable it
while you work by adding to `platformio.local.ini`:

```ini
[env:default]
build_flags = ${base.build_flags} -DINKAGENT_EXAMPLE_APPS=1
```

Copy that folder as a starting point; your own app needs no such guard.

## `INKAGENT_REGISTER_APP(Class, DisplayName, IconPtr)`

Place it once, at file scope, in the app's `.cpp`.

- `Class` — your `InkApp` subclass. It must be constructible from
  `(GfxRenderer&, MappedInputManager&)`.
- `DisplayName` — the tile label, a plain `const char*`. It is **not**
  translated: out-of-tree apps have no entries in the firmware string tables.
  In-tree apps may use `tr(STR_…)` inside `render()` for their own body text.
- `IconPtr` — pointer to a 32x32 `freeink::Icon`, or `nullptr` for the generic
  app icon. `components/icons/drawerIcons.h` and `listIcons.h` hold the existing
  set; see "Adding an icon" below.

Registration costs one `AppInfo` node and one pointer store at startup. No heap,
no `std::vector`, and no static-init-order hazard — the list head is a
zero-initialised pointer, not an object with a constructor.

Allocation at launch is nothrow. If it fails the drawer logs and stays put
rather than aborting; this is why the factory returns `makeUniqueNoThrow`.

## The lifecycle you must respect

Activities are heap-allocated and **deleted on exit**. The rules that matter:

| Hook | Do |
| --- | --- |
| `onEnter()` | Call `Activity::onEnter()` first. Allocate buffers, start tasks. |
| `loop()` | Handle input. Do not block; the watchdog fires after ~5s. |
| `render(RenderLock&&)` | Draw, then call `renderer.displayBuffer(...)` exactly once. |
| `onExit()` | Free every buffer, `vTaskDelete()` every task, close member files. Call `Activity::onExit()`. |

Anything allocated in `onEnter()` must be released in `onExit()`, and tasks must
be deleted **before** the activity is destroyed.

## E-ink rules that will make or break your app

The panel is the slowest thing in the system and refresh cost is proportional to
changed pixels, not to how much you draw.

- **Pick the right waveform.** `FAST_REFRESH` is the default and what you want
  almost always, entry paint included. `HALF_REFRESH` inverts the whole panel on
  the way in, which the user sees as a black flash; reserve it for screens that
  genuinely need ghosting cleared, and never call it on every render.
- **Prefer small changes.** A 4px underline beats an inverted row; the drawer's
  selection works this way deliberately.
- **Never hardcode 800 or 480.** Use `renderer.getScreenWidth()` /
  `getScreenHeight()`; the panel rotates and boards differ.
- **Draw through the theme.** `GUI.drawHeader()`, `GUI.drawButtonHints()` and
  `UITheme::getInstance().getMetrics()` keep your app consistent and
  orientation-correct. Do not hardcode fonts, paddings or colours.

## Input

Use the logical buttons from `MappedInputManager::Button` — `Back`, `Confirm`,
`Up`, `Down`, `Left`, `Right` — never raw `HalGPIO::BTN_*`. The user can remap
the front buttons, and orientation flips the side pair; the logical layer
accounts for both.

`mappedInput.mapLabels(back, confirm, up, down)` produces the strings for
`GUI.drawButtonHints()` in the correct physical order.

For touch boards, `mappedInput.wasScreenTapped(x, y)` reports a tap. Always keep
a button path working: not every board has a touchscreen.

## Memory protocol

The 380KB ceiling is the project's hard constraint. Apps are held to the same
rules as the firmware:

- Never use bare `new` — with `-fno-exceptions` it calls `abort()` on OOM.
  Use `makeUniqueNoThrow<T>()` from `lib/Memory/Memory.h` and null-check it.
- `LOG_ERR` before returning on any allocation failure.
- Keep locals under ~256 bytes; large buffers go on the heap.
- `.reserve()` before any `push_back` loop.
- Mark constant tables `static constexpr` so they live in flash, not DRAM.
- Prefer `std::string_view` for read-only text, but never pass `.data()` to a C
  API — it is not null-terminated.

## Adding an icon

Icons are 1-bpp `freeink::Icon` structs generated from Lucide SVGs:

```
python3 freeink-sdk/libs/assets/Icons/tools/gen_icons.py \
  --manifest icons.txt --svgdir <lucide/icons> --sizes 24,32 --out myIcons.h
```

`icons.txt` is one `alias = lucide-name` per line. Requires `rsvg-convert` and
Pillow. Keep generated headers inside your app's folder.

Note the SDK's vendored `lucide/` directory is excluded from its export and ships
empty, so fetch the SVGs you need from the upstream Lucide repository.

## Apps that are just files

Everything above compiles an app into the firmware. There is a second kind that
does not: a **manifest**, a JSON file in `/Apps` on the SD card. It appears in
the drawer on the next open, with no build and no reflash.

```json
{
  "name": "Status",
  "icon": "info",
  "title": "Status",
  "rows": [
    {"kind": "kv",   "label": "Reading",  "value": {"src": "reading.title"}},
    {"kind": "kv",   "label": "Progress", "value": {"src": "reading.percent"}},
    {"kind": "rule", "gapAfter": 2},
    {"kind": "kv",   "label": "Battery",  "value": {"src": "device.battery"}},
    {"kind": "text", "prefix": "Due to recall: ", "text": {"src": "review.word"}, "center": true}
  ]
}
```

Copy that to `/Apps/status.json` and open the drawer. `examples/apps/` has it
and one other to start from.

### What a manifest can say

| Field | Meaning |
| --- | --- |
| `name` | Tile label. A literal string, required — a tile with no label cannot be chosen deliberately. |
| `icon` | One of `book`, `library`, `bookmark`, `inbox`, `words`, `settings`, `info`, `clock`, `wifi`, `folder`, `file`. Anything else falls back to the generic app icon. |
| `title` | Header text. A literal, or `{"src": "..."}`. |
| `rows` | Up to 16 rows, drawn in order. |

Row kinds:

| `kind` | Draws |
| --- | --- |
| `text` | One line. `text` is a literal or a binding; `bold`, `center` and `prefix` are optional. |
| `kv` | A small `label` with a larger `value` beneath it. The value wraps to two lines. |
| `rule` | A horizontal divider. |
| `logo` | The product mark, centred. |

Every row takes `gapAfter`: the space below it, in multiples of the theme's
vertical spacing, 0 to 4.

A `text` row whose value resolves to nothing is **collapsed entirely** — it
takes no space and its `prefix` disappears with it. That is how an optional row
works, and it is why the format has no conditionals.

### What a manifest cannot say

There is no `if`, no loop, no expression and no arithmetic. There is no way to
name a data source the firmware does not already expose, no way to reach the
network, and no way to run for longer than the screen is on.

This is the point, not a limitation waiting to be lifted. A third-party app on
this device cannot poll, cannot notify, cannot accumulate an unread count and
cannot follow you into the book. The ceiling is what makes it safe to let
strangers ship apps for a device meant to be undistracting.

If you find yourself needing a branch, the answer is a data source, not a
conditional: compute it in C++ where the memory and the whitelist are visible,
and bind a row to the result.

### Data sources

A value is either a literal string or `{"src": "<name>"}`. The whitelist lives
in `src/engage/DataSource.cpp`:

| Source | Value |
| --- | --- |
| `device.version` | Release version, dev suffix stripped |
| `device.model` | `Xteink X3` / `Xteink X4` |
| `device.screen` | `800 x 480` |
| `device.battery` | `84%` |
| `device.freeHeap` | `116 KB` |
| `device.clock` | Current time, or empty with no RTC |
| `reading.title` | Book you are in |
| `reading.author` | Its author |
| `reading.percent` | How far in, or empty if never recorded |
| `review.word` | One word due for recall, or empty |

Adding a source is a few lines in `resolveSource()`. Keep them cheap: a source
is resolved while a screen is being built, sometimes while entering sleep, and
anything that opens a file or waits on the network does not belong here.

### Limits

A manifest over 4 KB is ignored, at most 8 apps are listed, names are cut to 24
characters and each field to 64 bytes. A broken manifest shows a message on its
own screen rather than silently missing from the drawer.

## Checklist before you submit

- [ ] `pio run` succeeds with no new warnings
- [ ] `./bin/clang-format-fix -g` is clean
- [ ] Tested on device in all four orientations
- [ ] Free heap stays healthy (`ESP.getFreeHeap()`), no leak across enter/exit
- [ ] No blocking work in `loop()`
- [ ] Back always leaves the app
