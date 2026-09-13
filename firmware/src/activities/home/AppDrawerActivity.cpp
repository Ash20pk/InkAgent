#include "AppDrawerActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "OpdsServerStore.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/highlights/HighlightsActivity.h"
#include "activities/stats/ReadingStatsActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "activities/network/InkAgentWebServerActivity.h"
#include "activities/network/PeerSendActivity.h"
#include "activities/readlater/ReadLaterActivity.h"
#include "activities/settings/OpdsServerListActivity.h"
#include "activities/words/WordListActivity.h"
#include "components/UITheme.h"
#include "components/icons/drawerIcons.h"
#include "components/icons/customListIcons.h"
#include "components/icons/listIcons.h"
#include "engage/ManifestActivity.h"
#include "fontIds.h"

namespace {
constexpr int ICON_SIZE = 32;
// Tile box; the grid stretches to fill the row, so these are minimums.
constexpr int MIN_TILE_WIDTH = 110;
constexpr int MAX_TILE_HEIGHT = 96;
// Below this the icon and its label stop fitting; the grid adds a column
// rather than squeezing further.
constexpr int MIN_TILE_HEIGHT = 78;

// listIcons/drawerIcons are SDK-format (1 = transparent, 0 = black), which the
// legacy GfxRenderer::drawIcon() bit layout does not share. Blit per pixel so
// the renderer still maps logical -> panel coordinates for the orientation.
void blitIcon(const GfxRenderer& renderer, const freeink::Icon& icon, const int x, const int y) {
  const int stride = (icon.w + 7) / 8;
  for (int row = 0; row < icon.h; row++) {
    const uint8_t* bits = icon.bits + row * stride;
    for (int col = 0; col < icon.w; col++) {
      const bool transparent = (bits[col / 8] >> (7 - (col % 8))) & 0x01;
      if (!transparent) renderer.drawPixel(x + col, y + row);
    }
  }
}

// A manifest names an icon as a string, resolved against this table. Unknown
// names fall back rather than failing the app: an icon is not worth refusing a
// screen over, and it keeps the format from being a place to smuggle in
// arbitrary asset paths.
const freeink::Icon& iconByName(const char* name) {
  if (name == nullptr || name[0] == '\0') return icon_apps_32;
  if (strcmp(name, "book") == 0) return icon_book_32;
  if (strcmp(name, "library") == 0) return icon_library_32;
  if (strcmp(name, "bookmark") == 0) return icon_bookmark_32;
  if (strcmp(name, "inbox") == 0) return icon_inbox_32;
  if (strcmp(name, "words") == 0) return icon_words_32;
  if (strcmp(name, "chart") == 0) return icon_chart_32;
  if (strcmp(name, "settings") == 0) return icon_settings_32;
  if (strcmp(name, "info") == 0) return icon_info_32;
  if (strcmp(name, "clock") == 0 || strcmp(name, "sun") == 0) return icon_sun_32;
  if (strcmp(name, "wifi") == 0) return icon_wifi_32;
  if (strcmp(name, "folder") == 0) return icon_folder_32;
  if (strcmp(name, "file") == 0) return icon_file_32;
  return icon_apps_32;
}

const freeink::Icon& iconFor(const int index, const StrId label) {
  (void)index;
  switch (label) {
    case StrId::STR_MENU_RECENT_BOOKS:
      return icon_book_32;
    case StrId::STR_OPDS_BROWSER:
      return icon_library_32;
    case StrId::STR_WORD_LIST:
      return icon_words_32;
    case StrId::STR_HIGHLIGHTS:
      return icon_bookmark_32;
    case StrId::STR_READING_STATS:
      return icon_chart_32;
    case StrId::STR_READ_LATER:
      return icon_inbox_32;
    case StrId::STR_BROWSE_FILES:
      return icon_folder_32;
    case StrId::STR_FILE_TRANSFER:
      return icon_upload_32;
    case StrId::STR_PEER_SEND:
      return icon_radio_tower_32;
    default:
      return icon_apps_32;
  }
}
}  // namespace

AppDrawerActivity::AppDrawerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("AppDrawer", renderer, mappedInput) {}

void AppDrawerActivity::onEnter() {
  Activity::onEnter();

  entries = {
      {Target::RECENTS, StrId::STR_MENU_RECENT_BOOKS},
      {Target::WORD_LIST, StrId::STR_WORD_LIST},
      {Target::HIGHLIGHTS, StrId::STR_HIGHLIGHTS},
      {Target::BOOK_STATS, StrId::STR_READING_STATS},
      {Target::READ_LATER, StrId::STR_READ_LATER},
      {Target::FILE_BROWSER, StrId::STR_BROWSE_FILES},
      // Not a reading screen, but it is how the reading screens get fed: the
      // way books and articles arrive belongs next to where they land.
      {Target::FILE_TRANSFER, StrId::STR_FILE_TRANSFER},
      {Target::PEER_SEND, StrId::STR_PEER_SEND},
  };
  // Only when there is somewhere to browse: a tile that opens an empty server
  // picker is a dead tile.
  if (OPDS_STORE.hasServers()) entries.push_back({Target::OPDS_BROWSER, StrId::STR_OPDS_BROWSER});

  // Apps registered under src/apps/ land after the built-ins, in link order.
  for (const inkapp::AppInfo* app = inkapp::apps(); app != nullptr; app = app->next) {
    entries.push_back({Target::REGISTERED_APP, StrId::STR_APPS, app});
  }

  // Then apps that are just files in /Apps, which need no firmware build.
  catalog = engage::scanApps();
  for (size_t i = 0; i < catalog.size(); i++) {
    entries.push_back({Target::CATALOG_APP, StrId::STR_APPS, nullptr, static_cast<int>(i)});
  }

  selectedIndex = 0;
  requestUpdate();
}

AppDrawerActivity::Grid AppDrawerActivity::computeGrid() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int usableWidth = pageWidth - metrics.contentSidePadding * 2;

  Grid grid{};
  grid.originX = metrics.contentSidePadding;
  grid.originY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  const int count = std::max(1, static_cast<int>(entries.size()));
  const int availableHeight =
      renderer.getScreenHeight() - grid.originY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // Widen the grid until the rows it needs fit the height available. Landscape
  // gets more columns than portrait for free, and a board with a short screen
  // degrades by adding columns rather than clipping the bottom row.
  const int maxColumns = std::max(2, usableWidth / MIN_TILE_WIDTH);
  grid.columns = maxColumns;
  for (int cols = 2; cols <= maxColumns; cols++) {
    const int rows = (count + cols - 1) / cols;
    if (rows * MIN_TILE_HEIGHT <= availableHeight) {
      grid.columns = cols;
      break;
    }
  }

  const int rows = (count + grid.columns - 1) / grid.columns;
  grid.tileWidth = usableWidth / grid.columns;
  grid.tileHeight = std::min(MAX_TILE_HEIGHT, std::max(MIN_TILE_HEIGHT, availableHeight / std::max(1, rows)));
  return grid;
}

void AppDrawerActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_APPS));

  const Grid grid = computeGrid();
  for (size_t i = 0; i < entries.size(); i++) {
    const int row = static_cast<int>(i) / grid.columns;
    const int col = static_cast<int>(i) % grid.columns;
    const int tileX = grid.originX + col * grid.tileWidth;
    const int tileY = grid.originY + row * grid.tileHeight;

    const freeink::Icon& icon = entries[i].app != nullptr && entries[i].app->icon != nullptr ? *entries[i].app->icon
                                : entries[i].catalogIndex >= 0
                                    ? iconByName(catalog[entries[i].catalogIndex].icon.c_str())
                                    : iconFor(static_cast<int>(i), entries[i].label);
    blitIcon(renderer, icon, tileX + (grid.tileWidth - ICON_SIZE) / 2, tileY + metrics.verticalSpacing);

    const char* label = entries[i].app != nullptr      ? entries[i].app->name
                        : entries[i].catalogIndex >= 0 ? catalog[entries[i].catalogIndex].name.c_str()
                                                       : I18n::getInstance().get(entries[i].label);
    const int labelY = tileY + metrics.verticalSpacing + ICON_SIZE + metrics.verticalSpacing;
    UITheme::drawCenteredText(renderer, Rect{tileX, labelY, grid.tileWidth, renderer.getLineHeight(UI_10_FONT_ID)},
                              UI_10_FONT_ID, labelY, label);

    // Selection is an underline under the whole tile rather than an inverted
    // block: far fewer changed pixels per move, which is what e-ink costs.
    if (static_cast<int>(i) == selectedIndex) {
      const int ruleY = labelY + renderer.getLineHeight(UI_10_FONT_ID) + 4;
      const int ruleInset = grid.tileWidth / 6;
      renderer.fillRect(tileX + ruleInset, ruleY, grid.tileWidth - ruleInset * 2, 4);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // Fast waveform throughout. A half refresh here inverts the whole panel on
  // the way in, which reads as a black flash every time the drawer opens.
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void AppDrawerActivity::loop() {
  Activity::loop();

  const int count = static_cast<int>(entries.size());
  if (count == 0) return;

  buttonNavigator.onNext([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate(entries[selectedIndex].target);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Touch: a tap inside a tile selects and launches it in one go.
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    const Grid grid = computeGrid();
    const int col = (tapX - grid.originX) / std::max(1, grid.tileWidth);
    const int row = (tapY - grid.originY) / std::max(1, grid.tileHeight);
    const int index = row * grid.columns + col;
    if (tapX >= grid.originX && col >= 0 && col < grid.columns && row >= 0 && index >= 0 && index < count) {
      selectedIndex = index;
      activate(entries[index].target);
    }
  }
}

void AppDrawerActivity::push(std::unique_ptr<Activity>&& activity, const char* what) {
  // A null activity means the allocation failed; stay on the drawer rather than
  // pushing nothing and leaving the user on a dead screen.
  if (!activity) {
    LOG_ERR("DRAWER", "OOM launching %s", what);
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
}

void AppDrawerActivity::activate(const Target target) {
  if (target == Target::REGISTERED_APP) {
    const inkapp::AppInfo* app = entries[selectedIndex].app;
    if (app == nullptr || app->create == nullptr) return;
    push(app->create(renderer, mappedInput), app->name);
    return;
  }

  switch (target) {
    // Pushed rather than routed through ActivityManager's goTo* helpers: those
    // replace the activity and clear the stack, which leaves Back falling
    // through to Home instead of returning to the drawer.
    case Target::RECENTS:
      push(makeUniqueNoThrow<RecentBooksActivity>(renderer, mappedInput), "Recents");
      break;
    case Target::OPDS_BROWSER: {
      // Mirrors goToBrowser(): a single configured server skips the picker.
      const auto& servers = OPDS_STORE.getServers();
      if (servers.size() == 1) {
        push(makeUniqueNoThrow<OpdsBookBrowserActivity>(renderer, mappedInput, servers[0]), "Library");
      } else {
        push(makeUniqueNoThrow<OpdsServerListActivity>(renderer, mappedInput, true), "Library");
      }
      break;
    }
    case Target::WORD_LIST:
      push(makeUniqueNoThrow<WordListActivity>(renderer, mappedInput), "Word List");
      break;
    case Target::HIGHLIGHTS:
      push(makeUniqueNoThrow<HighlightsActivity>(renderer, mappedInput), "Highlights");
      break;
    case Target::BOOK_STATS:
      push(makeUniqueNoThrow<ReadingStatsActivity>(renderer, mappedInput), "Reading Stats");
      break;
    case Target::READ_LATER:
      push(makeUniqueNoThrow<ReadLaterActivity>(renderer, mappedInput), "Read Later");
      break;
    case Target::FILE_BROWSER:
      push(makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, std::string{},
                                                  FileBrowserActivity::Mode::Books, /*returnOnRootBack=*/true),
           "Files");
      break;
    case Target::FILE_TRANSFER:
      push(makeUniqueNoThrow<InkAgentWebServerActivity>(renderer, mappedInput), "File Transfer");
      break;
    case Target::PEER_SEND:
      // Pick the file here, then hand the path to the sender: the browser
      // already knows how to list what the reader can open.
      startActivityForResult(makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, std::string{},
                                                                    FileBrowserActivity::Mode::PickToSend),
                             [this](const ActivityResult& result) {
                               if (result.isCancelled) {
                                 requestUpdate();
                                 return;
                               }
                               const auto& picked = std::get<FilePathResult>(result.data);
                               push(makeUniqueNoThrow<PeerSendActivity>(renderer, mappedInput, picked.path),
                                    "Send to reader");
                             });
      break;
    case Target::CATALOG_APP: {
      const int idx = entries[selectedIndex].catalogIndex;
      if (idx < 0 || idx >= static_cast<int>(catalog.size())) break;
      push(makeUniqueNoThrow<ManifestActivity>(renderer, mappedInput, catalog[idx].path, catalog[idx].name),
           catalog[idx].name.c_str());
      break;
    }
    case Target::REGISTERED_APP:
      break;  // handled above
  }
}
