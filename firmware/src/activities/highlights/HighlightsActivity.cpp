#include "HighlightsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookmarkFile.h"

HighlightsActivity::HighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Highlights", renderer, mappedInput) {}

void HighlightsActivity::onEnter() {
  Activity::onEnter();
  load();
  requestUpdate();
}

void HighlightsActivity::onExit() {
  highlights.clear();
  highlights.shrink_to_fit();
  Activity::onExit();
}

void HighlightsActivity::load() {
  highlights.clear();
  highlights.reserve(MAX_HIGHLIGHTS);

  // Walk the recents rather than the card: a book you have not opened in ten
  // books' time is not what you are looking for, and scanning the whole SD card
  // for bookmark sidecars would cost a visible pause on entry.
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (highlights.size() >= MAX_HIGHLIGHTS) break;
    std::vector<BookmarkEntry> entries;
    if (!BookmarkFile::load(book.path, entries)) continue;
    for (const auto& e : entries) {
      if (highlights.size() >= MAX_HIGHLIGHTS) break;
      Highlight h;
      h.summary = e.summary.substr(0, 120);
      h.book = book.title;
      h.path = book.path;
      const float pct = e.percentage * 100.0f;
      h.percent = static_cast<uint8_t>(pct < 0 ? 0 : (pct > 100 ? 100 : pct));
      highlights.push_back(std::move(h));
    }
  }
  selected = 0;
  top = 0;
}

int HighlightsActivity::visibleRows() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowHeight = std::max(1, metrics.listWithSubtitleRowHeight);
  const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  return std::max(1, (renderer.getScreenHeight() - startY - metrics.buttonHintsHeight) / rowHeight);
}

void HighlightsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_HIGHLIGHTS));

  if (highlights.empty()) {
    const int y = renderer.getScreenHeight() / 2 - 20;
    UITheme::drawCenteredWrappedText(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2,
             renderer.getLineHeight(UI_12_FONT_ID) * 3},
        UI_12_FONT_ID, tr(STR_HIGHLIGHTS_EMPTY), 3);
  } else {
    const int rowHeight = std::max(1, metrics.listWithSubtitleRowHeight);
    const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int rows = visibleRows();

    for (int i = 0; i < rows && top + i < static_cast<int>(highlights.size()); i++) {
      const Highlight& h = highlights[top + i];
      const int rowY = startY + i * rowHeight;

      // The passage first, its book underneath: you recognise what you marked,
      // not which file it lives in.
      const auto lines = renderer.wrappedText(UI_12_FONT_ID, h.summary.c_str(),
                                              pageWidth - metrics.contentSidePadding * 2, 1);
      if (!lines.empty()) {
        renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, rowY, lines.front().c_str());
      }
      char sub[96];
      snprintf(sub, sizeof(sub), "%s · %u%%", h.book.c_str(), static_cast<unsigned>(h.percent));
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, rowY + renderer.getLineHeight(UI_12_FONT_ID), sub);

      if (top + i == selected) {
        renderer.fillRect(metrics.contentSidePadding, rowY + rowHeight - 8,
                          pageWidth - metrics.contentSidePadding * 2, 3);
      }
    }
  }

  const auto labels = highlights.empty()
                          ? mappedInput.mapLabels(tr(STR_BACK), "", "", "")
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void HighlightsActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  const int count = static_cast<int>(highlights.size());
  if (count == 0) return;

  const int rows = visibleRows();
  buttonNavigator.onNext([this, count, rows] {
    selected = ButtonNavigator::nextIndex(selected, count);
    if (selected >= top + rows) top = selected - rows + 1;
    if (selected < top) top = selected;
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count, rows] {
    selected = ButtonNavigator::previousIndex(selected, count);
    if (selected < top) top = selected;
    if (selected >= top + rows) top = selected - rows + 1;
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Opens the book at its own saved position, not at this bookmark: jumping
    // to a specific bookmark from outside the reader needs a position to travel
    // through goToReader, which it cannot carry yet.
    onSelectBook(highlights[selected].path);
  }
}
