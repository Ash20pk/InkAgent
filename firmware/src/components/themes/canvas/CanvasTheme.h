#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// Canvas: the theme for app screens.
//
// Three things drive every value here, in priority order.
//
// 1. Refresh efficiency. E-ink cost is proportional to how many pixels change,
//    not to how much is drawn, so the layout is built to keep changes small and
//    local. Selection is an underline rather than an inverted row (a 4px rule
//    instead of a filled 44x488 band, roughly a tenth of the pixels), rows are
//    square and gapless so a scroll shifts whole rows with no gap to repaint,
//    and the chrome bands match Classic's heights so moving between screens
//    only ever redraws the content between them.
//
// 2. Four-grey polish. The panel resolves black / dark / light / white, so
//    structure is carried by grey rules and grounds instead of heavy black
//    boxes. Greys are laid down with fillRectDither, which renders as an
//    ordered pattern on the 1-bit path and as true grey once a screen runs the
//    grayscale planes - correct either way, with no branch at the call site.
//
// 3. A stable contract. App screens render through FreeInkUI, which reads these
//    tokens via uiThemeTokens(); an app that uses them looks native without
//    drawing any chrome itself. Values are deliberately plain data so they can
//    move to SD-card theme files later without touching app code.
namespace CanvasMetrics {
constexpr ThemeMetrics values = {  // Matched to the header's clock: a digit in ubuntu_12 (the header title font)
                                   // is 12x17, so a 12px-tall battery reads as undersized next to
                                   // the time. 17 tall makes the glyph and the digits share a cap
                                   // height; the width keeps the stock 1.25 aspect.
    .batteryWidth = 26,
    .batteryHeight = 18,
    .topPadding = 13,
    .batteryBarHeight = 24,
    .headerHeight = 44,
    .verticalSpacing = 8,
    .previewPadding = 10,
    .previewHeightPercent = 30,
    .contentSidePadding = 16,
    // Gapless square rows: a scroll moves whole rows and leaves no
    // inter-row band to repaint.
    .listRowHeight = 44,
    .listWithSubtitleRowHeight = 66,
    .listRowGap = 0,
    .listRowRadius = 0,
    .listInset = 0,
    .listSidePadding = 16,
    .listSelectionStyle = 2,  // underline: the cheapest selection to repaint
    .listScrollWidth = 3,
    .listScrollSide = 0,
    .listTitleBold = false,
    .headerSidePadding = 16,
    .headerUnderlineSize = 1,
    .headerTitleAlign = 0,  // left
    .headerBatterySide = 0,
    .headerBatteryDetached = false,
    .menuRowHeight = 44,
    .menuSpacing = 0,
    .tabSpacing = 8,
    .tabBarHeight = 44,
    .tabPillFullSlot = false,
    .scrollBarWidth = 3,
    .scrollBarRightOffset = 4,
    .homeTopPadding = 50,
    .homeCoverHeight = 300,
    .homeCoverTileHeight = 340,
    .homeRecentBooksCount = 1,
    .homeContinueReadingInMenu = false,
    .homeMenuTopOffset = 16,
    .buttonHintsHeight = 40,
    .sideButtonHintsWidth = 30,
    .progressBarHeight = 14,
    .progressBarMarginTop = 1,
    .statusBarHorizontalMargin = 5,
    .statusBarVerticalMargin = 19,
    .keyboardKeyHeight = 36,
    .keyboardKeySpacing = 8,
    .keyboardCenteredText = true,
    .keyboardVerticalOffset = 0,
    .keyboardTextFieldWidthPercent = 88,
    .keyboardWidthPercent = 96,
    // 0 centres the plate; see BaseTheme::drawPopup.
    .popupTopOffsetRatio = 0.0f,
    .popupMarginX = 18,
    .popupMarginY = 12,
    .popupFrameThickness = 1,
    .popupCornerRadius = 0,
    .popupTextBold = false,
    // The plate is filled white (popupCornerRadius 0 takes the
    // squared-off path), so the text has to be black. False here
    // painted white on white: the popup looked empty because the
    // word was there and invisible.
    .popupTextInverted = true,
    .popupTextBaselineOffsetY = -2,
    .popupProgressBarHeight = 4,
    .popupProgressDrawOutline = true,
    .popupProgressClampPercent = true,
    // Black on the white plate, for the same reason the text is:
    // these read as "inverted" because they were set for a plate
    // filled black, and on a white one they drew white on white.
    .popupProgressFillInverted = true,
    .popupProgressOutlineInverted = true,
    .optionPopupItemSpacing = 4,
    .optionPopupInnerPadding = 20,
    .optionPopupSelectionVPadding = 8,
    .optionPopupDialogSideMargin = 18,
    .textFieldHorizontalPadding = 8,
    .textFieldNormalThickness = 1,
    .textFieldCursorThickness = 2,
    .textFieldLineEndOffset = -1,
    .controlRadius = 0,
    .sheetRadius = 0,
    .capsuleRadius = 0,
    .homeHeaderShowsClock = true,
    .homeCoverTopPadding = 16,
    .headerStatusUsesTitleFont = true,
    .headerBatteryBarStyle = true};
}

class CanvasTheme : public BaseTheme {
 public:
  // Flat hint bar: a grey ground with grey separators instead of four outlined
  // boxes, so the bar reads as chrome rather than competing with the content.
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  // Flat grey battery fill, matching the theme's chrome.
  void fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const override;
  // App screens are lists of mixed content, so the type icon carries meaning.
  bool showsFileIcons() const override { return true; }
  // Cover-only home card: no title plate and no "Continue Reading" chip painted
  // over the artwork.
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
};
