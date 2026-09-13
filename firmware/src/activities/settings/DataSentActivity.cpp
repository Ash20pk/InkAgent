#include "DataSentActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <InkAgentStore.h>

#include <algorithm>
#include <string>
#include <vector>

#include "InkAgentSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Host only. The scheme is noise here, and the whole URL rarely fits.
std::string relayHost() {
  std::string url = INKAGENT_STORE.getRelayUrl();
  const auto scheme = url.find("://");
  if (scheme != std::string::npos) url.erase(0, scheme + 3);
  const auto slash = url.find('/');
  if (slash != std::string::npos) url.erase(slash);
  return url;
}

}  // namespace

DataSentActivity::DataSentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("DataSent", renderer, mappedInput) {}

void DataSentActivity::onEnter() {
  Activity::onEnter();
  INKAGENT_STORE.ensureLoaded();
  buildBlocks();
  scrollY = 0;
  contentHeight = layout(false);  // once, so loop() never wraps text
  requestUpdate();
}

void DataSentActivity::onExit() {
  blocks.clear();
  blocks.shrink_to_fit();
  Activity::onExit();
}

void DataSentActivity::buildBlocks() {
  blocks.clear();

  // Where, and under whose account.
  if (!INKAGENT_STORE.isPaired()) {
    blocks.push_back({tr(STR_DATA_SENT_UNPAIRED), false});
  } else {
    const std::string owner = INKAGENT_STORE.getOwner();
    std::string where = std::string(tr(STR_DATA_SENT_TO)) + " " + relayHost();
    if (!owner.empty()) where += " (" + owner + ")";
    blocks.push_back({where, false});
  }

  blocks.push_back({tr(STR_DATA_SENT_WHAT), true});
  blocks.push_back({tr(STR_DATA_SENT_PASSAGE), false});
  blocks.push_back({tr(STR_DATA_SENT_BOOK), false});
  blocks.push_back({tr(STR_DATA_SENT_FEATURES), false});

  blocks.push_back({tr(STR_DATA_NEVER), true});
  blocks.push_back({tr(STR_DATA_NEVER_LIST), false});

  blocks.push_back({tr(STR_DATA_WHEN), true});
  blocks.push_back({tr(STR_DATA_WHEN_ASK), false});
  // The automatic case is described only when it is switched on: telling
  // someone about a behaviour they have not enabled is its own confusion.
  blocks.push_back(
      {SETTINGS.agentBackgroundFetch ? tr(STR_DATA_WHEN_BACKGROUND_ON) : tr(STR_DATA_WHEN_BACKGROUND_OFF), false});
}

int DataSentActivity::viewportHeight() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return renderer.getScreenHeight() - (metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing) -
         metrics.buttonHintsHeight;
}

// Lays the blocks out, drawing only what falls inside the viewport. Returns the
// total height, which is also how the scroll limit is found — the text wraps
// differently in every orientation, so the extent cannot be a constant.
int DataSentActivity::layout(const bool draw) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int left = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - left * 2;
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = top + viewportHeight();

  int y = 0;
  for (const auto& block : blocks) {
    const int fontId = block.heading ? UI_12_FONT_ID : UI_10_FONT_ID;
    const int lineHeight = renderer.getLineHeight(fontId);
    if (block.heading && y > 0) y += metrics.verticalSpacing;

    // maxLines is generous: these are fixed strings, and clipping the sentence
    // that says what is never sent would be the worst possible truncation.
    const auto lines = renderer.wrappedText(fontId, block.text.c_str(), width, 6);
    for (const auto& line : lines) {
      const int screenY = top + y - scrollY;
      if (draw && screenY >= top && screenY + lineHeight <= bottom) {
        renderer.drawText(fontId, left, screenY, line.c_str(), true,
                          block.heading ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      }
      y += lineHeight;
    }
    if (!block.heading) y += metrics.verticalSpacing / 2;
  }
  return y;
}

void DataSentActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_DATA_SENT));

  contentHeight = layout(true);
  const bool scrollable = contentHeight > viewportHeight();

  const auto labels = scrollable ? mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN))
                                 : mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void DataSentActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const int viewport = viewportHeight();
  const int maxScroll = std::max(0, contentHeight - viewport);
  if (maxScroll == 0) return;

  // A page at a time, overlapping by a line so nothing is stepped over.
  const int step = std::max(1, viewport - UITheme::getInstance().getMetrics().verticalSpacing * 2);
  buttonNavigator.onNext([this, step, maxScroll] {
    const int next = std::min(scrollY + step, maxScroll);
    if (next != scrollY) {
      scrollY = next;
      requestUpdate();
    }
  });
  buttonNavigator.onPrevious([this, step] {
    const int next = std::max(0, scrollY - step);
    if (next != scrollY) {
      scrollY = next;
      requestUpdate();
    }
  });
}
