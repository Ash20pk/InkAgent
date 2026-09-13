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

## Checklist before you submit

- [ ] `pio run` succeeds with no new warnings
- [ ] `./bin/clang-format-fix -g` is clean
- [ ] Tested on device in all four orientations
- [ ] Free heap stays healthy (`ESP.getFreeHeap()`), no leak across enter/exit
- [ ] No blocking work in `loop()`
- [ ] Back always leaves the app
