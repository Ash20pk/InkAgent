#pragma once

#include <I18n.h>

#include <vector>

#include "activities/Activity.h"
#include "apps/InkApp.h"
#include "engage/AppCatalog.h"
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
    RECENTS,
    OPDS_BROWSER,
    WORD_LIST,
    HIGHLIGHTS,
    READ_LATER,
    FILE_TRANSFER,
    PEER_SEND,
    // An app that is a manifest file in /Apps rather than compiled in.
    CATALOG_APP,
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
    // Index into `catalog` for CATALOG_APP entries.
    int catalogIndex = -1;
  };

  // Built in onEnter(): OPDS only appears when a server is configured, and
  // Frontlight only on boards that have one, so the grid has no dead tiles.
  ButtonNavigator buttonNavigator;
  std::vector<Entry> entries;
  // Manifests found in /Apps on this open. Held for the life of the screen so a
  // tile has a path to launch and a label to draw.
  std::vector<engage::CatalogEntry> catalog;
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
