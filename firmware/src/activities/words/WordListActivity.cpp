#include "WordListActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "WordListStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

WordListActivity::WordListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("WordList", renderer, mappedInput) {}

void WordListActivity::onEnter() {
  Activity::onEnter();
  WORD_LIST.ensureLoaded();
  today = halClock.dayNumber();
  loadQueue();
  requestUpdate();
}

void WordListActivity::onExit() {
  queue.clear();
  queue.shrink_to_fit();
  Activity::onExit();
}

void WordListActivity::loadQueue() {
  queue.clear();
  for (const WordEntry* w : WORD_LIST.due(today)) queue.push_back(w->word);
  queueIndex = 0;
  if (queue.empty()) {
    // Nothing to retrieve: the list itself is still worth having, but an empty
    // queue is the good outcome, not a blank screen.
    mode = WORD_LIST.count() > 0 ? Mode::Browse : Mode::Done;
    return;
  }
  mode = Mode::Review;
  advance();
}

bool WordListActivity::advance() {
  if (queueIndex >= queue.size()) {
    mode = Mode::Done;
    return false;
  }
  currentWord = queue[queueIndex];
  currentBook.clear();
  for (const auto& w : WORD_LIST.getWords()) {
    if (w.word == currentWord) {
      currentBook = w.book;
      break;
    }
  }
  mode = Mode::Review;
  return true;
}

void WordListActivity::gradeCurrent(const bool remembered) {
  WORD_LIST.grade(currentWord, remembered, today);
  queueIndex++;
  if (!advance()) mode = Mode::Done;
  requestUpdate();
}

void WordListActivity::renderReview(const bool revealed) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WORD_LIST));

  // The word sits alone and centred: recall works when there is nothing else to
  // read off the screen.
  const int wordY = pageHeight / 2 - renderer.getLineHeight(UI_12_FONT_ID);
  UITheme::drawCenteredText(renderer, Rect{0, wordY, pageWidth, renderer.getLineHeight(UI_12_FONT_ID)}, UI_12_FONT_ID,
                            wordY, currentWord.c_str(), true, EpdFontFamily::BOLD);

  int y = wordY + renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing * 2;
  if (revealed && !currentBook.empty()) {
    UITheme::drawCenteredText(renderer, Rect{0, y, pageWidth, renderer.getLineHeight(UI_10_FONT_ID)}, UI_10_FONT_ID, y,
                              currentBook.c_str());
  } else if (!revealed) {
    UITheme::drawCenteredText(renderer, Rect{0, y, pageWidth, renderer.getLineHeight(UI_10_FONT_ID)}, UI_10_FONT_ID, y,
                              tr(STR_WORD_RECALL_PROMPT));
  }

  // A bare digit floating above the hints reads as an error code. Say what it
  // counts, and say nothing at all on the last one.
  const int left = static_cast<int>(queue.size() - queueIndex) - 1;
  char remaining[32];
  if (left > 0) {
    snprintf(remaining, sizeof(remaining), "%d %s", left, tr(STR_WORD_MORE_AFTER));
  } else {
    remaining[0] = '\0';
  }
  const int countY = pageHeight - metrics.buttonHintsHeight - renderer.getLineHeight(UI_10_FONT_ID) * 2;
  UITheme::drawCenteredText(renderer, Rect{0, countY, pageWidth, renderer.getLineHeight(UI_10_FONT_ID)}, UI_10_FONT_ID,
                            countY, remaining);

  const auto labels = revealed ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_WORD_KNEW), tr(STR_WORD_FORGOT), "")
                               : mappedInput.mapLabels(tr(STR_BACK), tr(STR_WORD_REVEAL), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void WordListActivity::renderBrowse() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& all = WORD_LIST.getWords();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WORD_LIST));

  const int rowHeight = metrics.listRowHeight;
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int visible = std::max(1, (pageHeight - top - metrics.buttonHintsHeight) / rowHeight);

  for (int i = 0; i < visible && browseTop + i < static_cast<int>(all.size()); i++) {
    const WordEntry& w = all[browseTop + i];
    const int rowY = top + i * rowHeight;
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, rowY, w.word.c_str());
    // Learned words are marked rather than hidden: seeing the pile grow is the
    // only reward this app offers.
    if (w.box >= WordListStore::kRetired) {
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, rowY + renderer.getLineHeight(UI_12_FONT_ID) - 4,
                        tr(STR_WORD_LEARNED));
    }
    if (browseTop + i == browseIndex) {
      renderer.fillRect(metrics.contentSidePadding, rowY + rowHeight - 6, pageWidth - metrics.contentSidePadding * 2,
                        3);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DELETE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void WordListActivity::renderDone() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WORD_LIST));
  const int y = pageHeight / 2;
  UITheme::drawCenteredWrappedText(renderer,
                                   Rect{metrics.contentSidePadding, y - 40, pageWidth - metrics.contentSidePadding * 2,
                                        renderer.getLineHeight(UI_12_FONT_ID) * 3},
                                   UI_12_FONT_ID,
                                   WORD_LIST.count() == 0 ? tr(STR_WORD_LIST_EMPTY) : tr(STR_WORD_ALL_DONE), 3);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), WORD_LIST.count() > 0 ? tr(STR_WORD_BROWSE) : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void WordListActivity::render(RenderLock&&) {
  switch (mode) {
    case Mode::Review:
      renderReview(false);
      break;
    case Mode::Revealed:
      renderReview(true);
      break;
    case Mode::Browse:
      renderBrowse();
      break;
    case Mode::Done:
      renderDone();
      break;
  }
}

void WordListActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  switch (mode) {
    case Mode::Review:
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        mode = Mode::Revealed;
        requestUpdate();
      }
      break;

    case Mode::Revealed:
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        gradeCurrent(true);
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        gradeCurrent(false);
      }
      break;

    case Mode::Browse: {
      const int count = WORD_LIST.count();
      if (count == 0) {
        mode = Mode::Done;
        requestUpdate();
        break;
      }
      const auto& metrics = UITheme::getInstance().getMetrics();
      const int rowHeight = std::max(1, metrics.listRowHeight);
      const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
      const int visible = std::max(1, (renderer.getScreenHeight() - top - metrics.buttonHintsHeight) / rowHeight);

      buttonNavigator.onNext([this, count, visible] {
        browseIndex = ButtonNavigator::nextIndex(browseIndex, count);
        if (browseIndex >= browseTop + visible) browseTop = browseIndex - visible + 1;
        if (browseIndex < browseTop) browseTop = browseIndex;
        requestUpdate();
      });
      buttonNavigator.onPrevious([this, count, visible] {
        browseIndex = ButtonNavigator::previousIndex(browseIndex, count);
        if (browseIndex < browseTop) browseTop = browseIndex;
        if (browseIndex >= browseTop + visible) browseTop = browseIndex - visible + 1;
        requestUpdate();
      });
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        const auto& all = WORD_LIST.getWords();
        if (browseIndex >= 0 && browseIndex < static_cast<int>(all.size())) {
          // Forgetting a word is not undoable and the list is the only copy,
          // so it asks first, like removing a book from recents does.
          const std::string victim = all[browseIndex].word;
          startActivityForResult(
              makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_WORD_FORGET_PROMPT), victim),
              [this, victim](const ActivityResult& result) {
                if (!result.isCancelled) {
                  WORD_LIST.removeWord(victim);
                  browseIndex = std::min(browseIndex, WORD_LIST.count() - 1);
                  if (browseIndex < 0) browseIndex = 0;
                  browseTop = std::min(browseTop, browseIndex);
                  if (WORD_LIST.count() == 0) mode = Mode::Done;
                }
                requestUpdate();
              });
        }
      }
      break;
    }

    case Mode::Done:
      if (WORD_LIST.count() > 0 && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        mode = Mode::Browse;
        requestUpdate();
      }
      break;
  }
}
