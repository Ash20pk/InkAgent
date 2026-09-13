#include "ScreenRenderer.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "Screen.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"

namespace engage {
namespace {
constexpr int LOGO_SIZE = 120;
}  // namespace

void renderScreen(GfxRenderer& renderer, MappedInputManager& mappedInput, const Screen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int labelX = metrics.contentSidePadding;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, screen.title);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  for (uint8_t i = 0; i < screen.rowCount; i++) {
    const Row& row = screen.rows[i];
    const int gap = metrics.verticalSpacing * row.gapAfter;

    switch (row.kind) {
      case RowKind::Logo:
        renderer.drawImage(Logo120, (pageWidth - LOGO_SIZE) / 2, y, LOGO_SIZE, LOGO_SIZE);
        y += LOGO_SIZE + gap;
        break;

      case RowKind::Text: {
        constexpr int fontId = UI_12_FONT_ID;
        const int lineHeight = renderer.getLineHeight(fontId);
        const auto style = row.bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
        if (row.centered) {
          UITheme::drawCenteredText(renderer, Rect{0, y, pageWidth, lineHeight}, fontId, y, row.a, true, style);
        } else {
          renderer.drawText(fontId, labelX, y, row.a, true, style);
        }
        y += lineHeight + gap;
        break;
      }

      case RowKind::Rule:
        renderer.fillRect(labelX, y, pageWidth - labelX * 2, 1);
        y += 1 + gap;
        break;

      case RowKind::Kv: {
        renderer.drawText(UI_10_FONT_ID, labelX, y, row.a);
        // Values wrap rather than run off the edge.
        const auto lines = renderer.wrappedText(UI_12_FONT_ID, row.b, pageWidth - labelX * 2, 2);
        int valueY = y + renderer.getLineHeight(UI_10_FONT_ID);
        for (const auto& line : lines) {
          renderer.drawText(UI_12_FONT_ID, labelX, valueY, line.c_str());
          valueY += renderer.getLineHeight(UI_12_FONT_ID);
        }
        y = valueY + gap;
        break;
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // Never a half refresh: that inverts the panel and reads as a black flash.
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

}  // namespace engage
