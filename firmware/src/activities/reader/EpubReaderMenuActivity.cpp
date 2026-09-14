#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>
#include <InkAgentStore.h>

#include "InkAgentSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks)
    : UiListActivity("EpubReaderMenu", renderer, mappedInput),
      title(title),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
  INKAGENT_STORE.ensureLoaded();
  buildMenuItems(menuItems, hasFootnotes, hasBookmarks);
  buildMenuRowItems();
}

// Populates menuRowItems's labels/actionValue from menuItems. Called once
// here since menuItems (and thus which rows exist) never changes after
// construction; buildScreen() only touches the two rows with a live value.
void EpubReaderMenuActivity::buildMenuRowItems() {
  // A chevron rather than a word: it says the row leads somewhere without
  // claiming any of the label's width, and it lines up down the right edge
  // with the value rows, so every row ends in the same column.
  static constexpr const char* kOpensScreen = "\u203A";

  bool haveGroup = false;
  Group previousGroup = Group::GoTo;
  for (size_t i = 0; i < menuItems.size() && i < MAX_MENU_ITEMS; i++) {
    const MenuAction action = menuItems[i].action;
    fui::ListItem item;
    item.label = I18N.get(menuItems[i].labelId);
    item.actionValue = static_cast<int16_t>(i);
    if (action == MenuAction::ASK_BOOK) item.enabled = INKAGENT_STORE.isPaired();
    if (opensScreen(action)) item.value = kOpensScreen;

    // The heading belongs to the first row of each group, so a group that
    // built no rows (no footnotes, no frontlight) leaves no heading behind.
    const Group group = groupFor(action);
    if (!haveGroup || group != previousGroup) {
      item.sectionHeading = I18N.get(headingFor(group));
      previousGroup = group;
      haveGroup = true;
    }
    menuRowItems[i] = item;
  }
}

void EpubReaderMenuActivity::buildMenuItems(std::vector<MenuItem>& items, bool hasFootnotes, bool hasBookmarks) {
  items.clear();
  items.reserve(MAX_MENU_ITEMS);

  // Somewhere else in the book.
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  if (hasFootnotes) items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  if (hasBookmarks) items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});

  // How the page looks.
  items.push_back({MenuAction::TEXT_SETTINGS, StrId::STR_TEXT_SETTINGS});
  items.push_back({MenuAction::NIGHT_MODE, StrId::STR_NIGHT_MODE});
  if (Frontlight.present()) items.push_back({MenuAction::FRONTLIGHT, StrId::STR_FRONTLIGHT});
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});

  // Things done to this book.
  items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
  items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  items.push_back({MenuAction::ASK_BOOK, StrId::STR_ASK_BOOK});
  items.push_back({MenuAction::BOOK_STATS, StrId::STR_STATS_BOOK});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});

  // The device. No Go Home: the bottom-left button already is one, and a menu
  // row duplicating a button is a row to scroll past.
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
}

EpubReaderMenuActivity::Group EpubReaderMenuActivity::groupFor(const MenuAction action) {
  switch (action) {
    case MenuAction::SELECT_CHAPTER:
    case MenuAction::FOOTNOTES:
    case MenuAction::BOOKMARKS:
    case MenuAction::GO_TO_PERCENT:
      return Group::GoTo;
    case MenuAction::TEXT_SETTINGS:
    case MenuAction::NIGHT_MODE:
    case MenuAction::FRONTLIGHT:
    case MenuAction::ROTATE_SCREEN:
    case MenuAction::AUTO_PAGE_TURN:
      return Group::Text;
    case MenuAction::TOGGLE_BOOKMARK:
    case MenuAction::DICTIONARY:
    case MenuAction::ASK_BOOK:
    case MenuAction::BOOK_STATS:
    case MenuAction::SYNC:
      return Group::Book;
    default:
      return Group::System;
  }
}

StrId EpubReaderMenuActivity::headingFor(const Group group) {
  switch (group) {
    case Group::GoTo:
      return StrId::STR_MENU_GROUP_GOTO;
    case Group::Text:
      return StrId::STR_MENU_GROUP_TEXT;
    case Group::Book:
      return StrId::STR_MENU_GROUP_BOOK;
    default:
      return StrId::STR_CAT_SYSTEM;
  }
}

bool EpubReaderMenuActivity::opensScreen(const MenuAction action) {
  switch (action) {
    case MenuAction::SELECT_CHAPTER:
    case MenuAction::FOOTNOTES:
    case MenuAction::BOOKMARKS:
    case MenuAction::GO_TO_PERCENT:
    case MenuAction::TEXT_SETTINGS:
    case MenuAction::DICTIONARY:
    case MenuAction::ASK_BOOK:
    case MenuAction::BOOK_STATS:
    case MenuAction::DISPLAY_QR:
      return true;
    default:
      return false;
  }
}

void EpubReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption};
  setResult(std::move(result));
  finish();
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void EpubReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  // The activated row leaves this screen (popup or finish); a lingering flash
  // would gray an unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;

  const auto selectedAction = menuItems[index].action;
  if (index >= 0 && static_cast<size_t>(index) < MAX_MENU_ITEMS && !menuRowItems[index].enabled) {
    return;  // grayed out: Ask the book before pairing (Settings › Ask the book)
  }
  if (selectedAction == MenuAction::ROTATE_SCREEN) {
    optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                     pendingOrientation, [this](int idx) {
                       pendingOrientation = idx;
                       // Rotate the menu immediately. Only the renderer turns;
                       // SETTINGS.orientation stays unchanged so the reader's
                       // result handler still detects the change and reflows.
                       ReaderUtils::applyOrientation(renderer, pendingOrientation);
                       app.setDevice(uiTarget.deviceContext());  // hit rects follow the new frame
                       requestUpdate(true);
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
    optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                     static_cast<int>(pageTurnLabels.size()), selectedPageTurnOption, [this](int idx) {
                       selectedPageTurnOption = idx;
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::NIGHT_MODE) {
    SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::FRONTLIGHT) {
    const bool lightOn = !Frontlight.isOn();
    Frontlight.setOn(lightOn);
    SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption});
  finish();
}

bool EpubReaderMenuActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool EpubReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void EpubReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  // Progress summary where the old sub-header band sat.
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight));
  const int16_t pad = screen.theme().headerSidePadding;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), progressLine.c_str(), screen.theme().smallText);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // menuRowItems's labels/actionValue were set once in the constructor (see
  // buildMenuRowItems()); only rows with live values need refreshing here.
  for (size_t i = 0; i < menuItems.size(); i++) {
    const auto action = menuItems[i].action;
    if (action == MenuAction::ROTATE_SCREEN) {
      menuRowItems[i].value = I18N.get(orientationLabels[pendingOrientation]);
    } else if (action == MenuAction::AUTO_PAGE_TURN) {
      menuRowItems[i].value = pageTurnLabels[selectedPageTurnOption];
    } else if (action == MenuAction::NIGHT_MODE) {
      menuRowItems[i].value = I18N.get(SETTINGS.screenInverted ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    } else if (action == MenuAction::FRONTLIGHT) {
      menuRowItems[i].value = I18N.get(Frontlight.isOn() ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    }
  }

  fui::ListProps props;
  props.items = menuRowItems;
  props.count = static_cast<uint16_t>(menuItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void EpubReaderMenuActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  drawChrome();

  renderUi();

  drawFooter();
  renderer.displayBuffer();
}
