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

int drawScreenBody(GfxRenderer& renderer, const Screen& screen, const int startY) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int labelX = metrics.contentSidePadding;

  int y = startY;

  for (uint8_t i = 0; i < screen.rowCount; i++) {
    const Row& row = screen.rows[i];
    const int gap = metrics.verticalSpacing * row.gapAfter;

    switch (row.kind) {
      case RowKind::Logo:
        renderer.drawImage(Logo120, (pageWidth - LOGO_SIZE) / 2, y, LOGO_SIZE, LOGO_SIZE);
        y += LOGO_SIZE + gap;
        break;

      case RowKind::Text: {
        // A manifest cannot branch, so an optional row is expressed as a row
        // bound to a source that may resolve to nothing. Collapsing it here is
        // what keeps that honest: no conditionals in the format, no blank gap
        // on the screen.
        if (row.a[0] == '\0') break;
        constexpr int fontId = UI_12_FONT_ID;
        const int lineHeight = renderer.getLineHeight(fontId);
        const auto style = row.bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
        // The prefix rides with the value, so it disappears when the value does
        // and never labels an empty line.
        char line[kMaxTextBytes * 2];
        snprintf(line, sizeof(line), "%s%s", row.prefix, row.a);
        if (row.centered) {
          UITheme::drawCenteredText(renderer, Rect{0, y, pageWidth, lineHeight}, fontId, y, line, true, style);
        } else {
          renderer.drawText(fontId, labelX, y, line, true, style);
        }
        y += lineHeight + gap;
        break;
      }

      case RowKind::Para: {
        // The one row kind that may run to several lines. Everything else is a
        // single line by construction, which is what keeps layout predictable.
        if (row.paraLen == 0) break;  // collapsed, like an empty text row
        const char* text = screen.paraPool + row.paraOffset;
        const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
        const auto lines = renderer.wrappedText(UI_10_FONT_ID, text, pageWidth - labelX * 2, row.maxLines);
        for (const auto& line : lines) {
          if (row.centered) {
            UITheme::drawCenteredText(renderer, Rect{0, y, pageWidth, lineHeight}, UI_10_FONT_ID, y, line.c_str());
          } else {
            renderer.drawText(UI_10_FONT_ID, labelX, y, line.c_str());
          }
          y += lineHeight;
        }
        y += gap;
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

  return y;
}

int measureScreenBody(const GfxRenderer& renderer, const Screen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int height = 0;
  for (uint8_t i = 0; i < screen.rowCount; i++) {
    const Row& row = screen.rows[i];
    const int gap = metrics.verticalSpacing * row.gapAfter;
    switch (row.kind) {
      case RowKind::Logo:
        height += LOGO_SIZE + gap;
        break;
      case RowKind::Text:
        if (row.a[0] == '\0') break;  // collapsed; see drawScreenBody
        height += renderer.getLineHeight(UI_12_FONT_ID) + gap;
        break;
      case RowKind::Rule:
        height += 1 + gap;
        break;
      case RowKind::Para: {
        if (row.paraLen == 0) break;
        const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
        const auto lines = renderer.wrappedText(UI_10_FONT_ID, screen.paraPool + row.paraOffset, width, row.maxLines);
        height += static_cast<int>(lines.size()) * renderer.getLineHeight(UI_10_FONT_ID) + gap;
        break;
      }
      case RowKind::Kv:
        // Label line plus a single value line; a wrapped value only grows this.
        height += renderer.getLineHeight(UI_10_FONT_ID) + renderer.getLineHeight(UI_12_FONT_ID) + gap;
        break;
    }
  }
  return height;
}

void renderScreen(GfxRenderer& renderer, MappedInputManager& mappedInput, const Screen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, screen.title);

  drawScreenBody(renderer, screen, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // Never a half refresh: that inverts the panel and reads as a black flash.
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

}  // namespace engage
