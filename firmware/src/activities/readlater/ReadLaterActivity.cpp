#include "ReadLaterActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t NAME_BUFFER_SIZE = 256;
}

ReadLaterActivity::ReadLaterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("ReadLater", renderer, mappedInput) {}

void ReadLaterActivity::onEnter() {
  Activity::onEnter();
  // Created on first open so the folder is already there to drop files into
  // from the file transfer page — an inbox you have to make first is an inbox
  // nobody fills.
  if (!Storage.exists(FOLDER) && !Storage.mkdir(FOLDER)) {
    LOG_ERR("READLATER", "could not create %s", FOLDER);
  }
  load();
  requestUpdate();
}

void ReadLaterActivity::onExit() {
  items.clear();
  items.shrink_to_fit();
  Activity::onExit();
}

std::string ReadLaterActivity::pathFor(const int index) const {
  return std::string(FOLDER) + "/" + items[index];
}

void ReadLaterActivity::load() {
  items.clear();
  auto dir = Storage.open(FOLDER);
  if (!dir || !dir.isDirectory()) return;

  auto nameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!nameBuffer) {
    LOG_ERR("READLATER", "OOM: name buffer");
    return;
  }

  dir.rewindDirectory();
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (items.size() >= MAX_ITEMS) break;
    file.getName(nameBuffer.get(), NAME_BUFFER_SIZE);
    if (file.isDirectory() || nameBuffer[0] == '.') continue;
    const std::string_view name{nameBuffer.get()};
    // Only what the reader can actually open, so nothing in here is a dead end.
    if (FsHelpers::hasEpubExtension(name) || FsHelpers::hasTxtExtension(name) ||
        FsHelpers::hasMarkdownExtension(name) || FsHelpers::hasXtcExtension(name)) {
      items.emplace_back(nameBuffer.get());
    }
  }
  dir.close();
  std::sort(items.begin(), items.end());
  selected = std::min(selected, static_cast<int>(items.size()) - 1);
  if (selected < 0) selected = 0;
  top = std::min(top, selected);
}

int ReadLaterActivity::visibleRows() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowHeight = std::max(1, metrics.listRowHeight);
  const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  return std::max(1, (renderer.getScreenHeight() - startY - metrics.buttonHintsHeight) / rowHeight);
}

void ReadLaterActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READ_LATER));

  if (items.empty()) {
    const int y = renderer.getScreenHeight() / 2 - 30;
    UITheme::drawCenteredWrappedText(renderer,
                                     Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2,
                                          renderer.getLineHeight(UI_12_FONT_ID) * 4},
                                     UI_12_FONT_ID, tr(STR_READ_LATER_EMPTY), 4);
  } else {
    const int rowHeight = std::max(1, metrics.listRowHeight);
    const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int rows = visibleRows();

    for (int i = 0; i < rows && top + i < static_cast<int>(items.size()); i++) {
      const int rowY = startY + i * rowHeight;
      const auto lines = renderer.wrappedText(UI_12_FONT_ID, items[top + i].c_str(),
                                              pageWidth - metrics.contentSidePadding * 2, 1);
      if (!lines.empty()) {
        renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, rowY, lines.front().c_str());
      }
      if (top + i == selected) {
        renderer.fillRect(metrics.contentSidePadding, rowY + rowHeight - 6,
                          pageWidth - metrics.contentSidePadding * 2, 3);
      }
    }
  }

  const auto labels = items.empty()
                          ? mappedInput.mapLabels(tr(STR_BACK), "", "", "")
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void ReadLaterActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  const int count = static_cast<int>(items.size());
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
    onSelectBook(pathFor(selected));
    return;
  }

  // Left is "done with this one". Emptying the queue is the point, so removal
  // sits on a button rather than behind a menu — but it deletes the file, so
  // it asks first.
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    const std::string victim = pathFor(selected);
    const std::string name = items[selected];
    startActivityForResult(makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_READ_LATER_REMOVE), name),
                           [this, victim](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               if (!Storage.remove(victim.c_str())) {
                                 LOG_ERR("READLATER", "could not remove %s", victim.c_str());
                               }
                               load();
                             }
                             requestUpdate();
                           });
  }
}
