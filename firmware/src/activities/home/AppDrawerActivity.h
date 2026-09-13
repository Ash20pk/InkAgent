#pragma once

#include <I18n.h>

#include <vector>

#include "activities/Activity.h"
#include "apps/InkApp.h"
#include "util/ButtonNavigator.h"

// Grid launcher for the screens that are not books: the ones Home lists plus
// Wi-Fi, Frontlight and About, which otherwise live a level or two down inside
// Settings. Rendered as icon tiles rather than a list so a whole screen of
// destinations fits without scrolling.
class AppDrawerActivity : public Activity {
 public:
  AppDrawerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Target {
    FILE_BROWSER,
    RECENTS,
    WORD_LIST,
    OPDS_BROWSER,
    FILE_TRANSFER,
    WIFI,
    FRONTLIGHT,
    SETTINGS_APP,
    USB_DRIVE,
    CALIBRE,
    FONTS,
    FIRMWARE_UPDATE,
    ABOUT,
    // Registered by an app under src/apps/ rather than built in; the AppInfo
    // on the entry says which.
    REGISTERED_APP,
  };

  struct Entry {
    Target target;
    StrId label;
    // Set only for REGISTERED_APP entries; supplies the label and icon, since
    // an out-of-tree app has no StrId.
    const inkapp::AppInfo* app = nullptr;
  };

  // Built in onEnter(): OPDS only appears when a server is configured, and
  // Frontlight only on boards that have one, so the grid has no dead tiles.
  ButtonNavigator buttonNavigator;
  std::vector<Entry> entries;
  int selectedIndex = 0;

  void activate(Target target);
  // Stacks `activity` on the drawer so its Back returns here. Null (a failed
  // allocation) is logged and ignored.
  void push(std::unique_ptr<Activity>&& activity, const char* what);
  // Grid geometry, derived once per render from the screen and the tile count.
  struct Grid {
    int originX, originY, tileWidth, tileHeight, columns;
  };
  Grid computeGrid() const;
};
