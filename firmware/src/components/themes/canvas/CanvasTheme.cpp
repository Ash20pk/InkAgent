#include "CanvasTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>

#include <algorithm>
#include <string>

#include "I18n.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

void CanvasTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                  const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  constexpr int barHeight = CanvasMetrics::values.buttonHintsHeight;
  const int barTop = pageHeight - barHeight;
  constexpr int textYOffset = 7;

  // A grayscale plane pass re-runs this with the base pass's monochrome hints
  // already on screen; writing nothing keeps them intact, exactly as the base
  // theme does.
  if (renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute()) {
    renderer.setOrientation(origOrientation);
    return;
  }

  // Light grey ground with a hairline above it: enough to separate the bar from
  // the content without the four black outlines the base theme draws.
  renderer.fillRectDither(0, barTop, pageWidth, barHeight, Color::LightGray);
  renderer.drawLine(0, barTop, pageWidth - 1, barTop, true);

  const char* labels[] = {btn1, btn2, btn3, btn4};
  const int slotWidth = pageWidth / 4;
  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const int x = i * slotWidth;
    // Separators between populated slots only, so a lone hint sits on open bar.
    if (i > 0 && labels[i - 1] != nullptr && labels[i - 1][0] != '\0') {
      renderer.drawLine(x, barTop + barHeight / 4, x, barTop + (barHeight * 3) / 4, true);
    }
    drawHintLabel(renderer, UI_10_FONT_ID, labels[i], x, slotWidth, barTop, barHeight, textYOffset);
  }

  renderer.setOrientation(origOrientation);
}

void CanvasTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                      const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                      bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  // The cover is the whole card. The base theme paints the title, the author and
  // a "Continue Reading" chip on top of the artwork, which buries the one thing
  // that identifies the book at a glance - and the action already has its own
  // button, so the chip is a label for something the user cannot miss.
  //
  // Selection is a border drawn just inside the card rather than an inverted
  // fill: it changes a few hundred pixels instead of the whole cover, so moving
  // the selector on and off the book stays a cheap repaint.
  const bool hasBook = !recentBooks.empty();
  const bool bookSelected = hasBook && selectorIndex == 0;

  // Padding above the card so the border is not flush against the status bar.
  const int topPad = CanvasMetrics::values.homeCoverTopPadding;
  const int availHeight = rect.height - topPad;
  const int maxWidth = static_cast<int>(rect.width * 0.9f);

  int cardWidth = rect.width / 2;
  int cardHeight = availHeight;
  bool haveCoverFile = false;
  std::string coverPath;

  if (hasBook && !recentBooks[0].coverBmpPath.empty()) {
    coverPath = UITheme::getCoverThumbPath(recentBooks[0].coverBmpPath, CanvasMetrics::values.homeCoverHeight);
    HalFile file;
    if (Storage.openFileForRead("HOME", coverPath, file)) {
      Bitmap probe(file);
      if (probe.parseHeaders() == BmpReaderError::Ok && probe.getWidth() > 0 && probe.getHeight() > 0) {
        haveCoverFile = true;
        // Size the card to the pixels drawBitmap will actually put on screen.
        // It only ever scales DOWN (see its fitScale < 1.0f guard), so a
        // thumbnail smaller than the band is drawn at its natural size - asking
        // for the full band would leave the art floating inside a larger border.
        // Taking the same min() here, capped at 1.0, makes the border hug the
        // art exactly at whatever size it lands.
        const int imgW = probe.getWidth();
        const int imgH = probe.getHeight();
        float fit = std::min(static_cast<float>(maxWidth) / imgW, static_cast<float>(availHeight) / imgH);
        if (fit > 1.0f) fit = 1.0f;
        cardWidth = static_cast<int>(imgW * fit);
        cardHeight = static_cast<int>(imgH * fit);
      }
    }
  }

  const int cardX = rect.x + (rect.width - cardWidth) / 2;
  // Centre what is left of the band under the top padding, so a short cover sits
  // in the space rather than hanging from the top edge.
  const int cardY = rect.y + topPad + std::max(0, (availHeight - cardHeight) / 2);

  // Selection rings sit outside the frame, so they never cover the artwork.
  const auto drawSelection = [&]() {
    if (!bookSelected) return;
    renderer.drawRect(cardX - 3, cardY - 3, cardWidth + 6, cardHeight + 6);
    renderer.drawRect(cardX - 4, cardY - 4, cardWidth + 8, cardHeight + 8);
  };

  if (!hasBook) {
    renderer.drawRect(cardX, cardY, cardWidth, cardHeight);
    const int y = cardY + (cardHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_NO_OPEN_BOOK));
    return;
  }

  // First paint loads the cover from SD and stashes the framebuffer; later
  // paints restore that buffer instead, so navigating the menu never re-reads
  // the card.
  if (haveCoverFile && !coverRendered) {
    HalFile file;
    if (Storage.openFileForRead("HOME", coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        // Art at its exact size, frame drawn one pixel outside it: the border
        // neither overwrites the outer row of the art nor leaves a gap.
        renderer.drawBitmap(bitmap, cardX, cardY, cardWidth, cardHeight);
        renderer.drawRect(cardX - 1, cardY - 1, cardWidth + 2, cardHeight + 2);
        coverBufferStored = storeCoverBuffer();
        coverRendered = coverBufferStored;
        drawSelection();
        return;
      }
    }
  }

  if (bufferRestored && coverRendered) {
    drawSelection();
    return;
  }

  // No cover art: the title has to carry the card, so it is drawn on a plain
  // ground rather than over an image.
  if (!coverRendered) {
    renderer.drawRect(cardX, cardY, cardWidth, cardHeight);
    const auto lines = renderer.wrappedText(UI_12_FONT_ID, recentBooks[0].title.c_str(), cardWidth - 32, 3);
    const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
    int y = cardY + (cardHeight - lineH * static_cast<int>(lines.size())) / 2;
    for (const auto& line : lines) {
      renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str());
      y += lineH;
    }
    drawSelection();
  }
}

void CanvasTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  // Flat and grey to match the rest of the theme: a dithered level fill inside
  // the outline instead of the base theme's solid black bar, so the indicator
  // reads as chrome rather than as the darkest thing on the screen. Charging
  // inverts to solid so the state is unmistakable at a glance.
  const bool charging = gpio.isUsbConnected();
  const int maxFillWidth = rect.width - 5;
  const int fillHeight = rect.height - 4;
  if (maxFillWidth <= 0 || fillHeight <= 0) return;

  int filledWidth = percentage * maxFillWidth / 100 + 1;
  if (filledWidth > maxFillWidth) filledWidth = maxFillWidth;

  if (charging) {
    renderer.fillRect(rect.x + 2, rect.y + 2, maxFillWidth, fillHeight, true);
    return;
  }
  // Low battery stays solid black: a grey sliver at 10% would be easy to miss.
  if (percentage <= 20) {
    renderer.fillRect(rect.x + 2, rect.y + 2, filledWidth, fillHeight, true);
    return;
  }
  renderer.fillRectDither(rect.x + 2, rect.y + 2, filledWidth, fillHeight, Color::DarkGray);
}
