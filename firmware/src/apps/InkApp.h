#pragma once

// InkAgent app SDK
// =================
// An app is an Activity that announces itself to the firmware at link time. Drop
// a folder under src/apps/, subclass InkApp, call INKAGENT_REGISTER_APP once in
// the .cpp, and the app appears in the app drawer with no edits to any shared
// file. See docs/APP_DEVELOPMENT.md for the full guide.
//
// Registration is a static intrusive list: each app contributes one AppInfo node
// in flash plus one pointer store at startup. There is no heap allocation, no
// std::vector to grow, and no static-init-order hazard, because the list head is
// a zero-initialised pointer rather than an object with a constructor.

#include <Icon.h>
#include <Memory.h>

#include <memory>

#include "activities/Activity.h"

class InkApp : public Activity {
 public:
  InkApp(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity(std::move(name), renderer, mappedInput) {}
};

namespace inkapp {

struct AppInfo {
  // Shown under the app's tile. Not translated: an out-of-tree app has no
  // entry in the firmware's string tables, so it supplies its own text.
  const char* name;
  // 32x32 freeink::Icon, drawn as-is. Null falls back to the generic app icon.
  const freeink::Icon* icon;
  std::unique_ptr<Activity> (*create)(GfxRenderer&, MappedInputManager&);
  const AppInfo* next;
};

// Zero-initialised before any dynamic initialiser runs, so registrars in any
// translation unit can link into it safely regardless of link order.
extern const AppInfo* registryHead;

struct Registrar {
  explicit Registrar(AppInfo* info) {
    info->next = registryHead;
    registryHead = info;
  }
};

// Walk with: for (const AppInfo* a = inkapp::apps(); a != nullptr; a = a->next)
inline const AppInfo* apps() { return registryHead; }

int appCount();

}  // namespace inkapp

// Registers `Class` as an app. Place once, at file scope, in the app's .cpp.
//   INKAGENT_REGISTER_APP(HelloWorldApp, "Hello", &icon_apps_32);
// Allocation is nothrow: a failed app launch returns null and the drawer stays
// put rather than aborting the firmware.
#define INKAGENT_REGISTER_APP(Class, DisplayName, IconPtr)                                                        \
  static std::unique_ptr<Activity> Class##_inkAppCreate(GfxRenderer& renderer, MappedInputManager& mappedInput) { \
    return makeUniqueNoThrow<Class>(renderer, mappedInput);                                                       \
  }                                                                                                               \
  static inkapp::AppInfo Class##_inkAppInfo = {DisplayName, IconPtr, &Class##_inkAppCreate, nullptr};             \
  static const inkapp::Registrar Class##_inkAppRegistrar(&Class##_inkAppInfo)
