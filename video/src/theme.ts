import {loadFont as loadSans} from '@remotion/google-fonts/NotoSans';
import {loadFont as loadSerif} from '@remotion/google-fonts/NotoSerif';
import {loadFont as loadUbuntu} from '@remotion/google-fonts/Ubuntu';

// Two font stacks, because the firmware has two.
//
// The BOOK is set in Noto Serif / Noto Sans — src/main.cpp loads 12/14/16/18pt
// of each, and SETTINGS.getReaderFontId() picks one for body text.
// The CHROME is Ubuntu: main.cpp:121-127 binds UI_10 to ubuntu_10 and UI_12 to
// ubuntu_12, and every header, list row and button hint draws in one of those.
// The status bar is the exception — SMALL_FONT_ID is notosans_8 (main.cpp:118).
export const {fontFamily: notoSerif} = loadSerif();
export const {fontFamily: notoSans} = loadSans();
export const {fontFamily: ubuntu} = loadUbuntu();

// UC8253 draws black, white and two greys. Nothing in this video is a colour the
// panel cannot produce.
export const ink = {
  paper: '#eae8e3', // e-ink white, which is really a light warm grey
  light: '#b0ada6', // Color::LightGray, as fillRectDither lays it down
  mid: '#6a6760',   // Color::DarkGray
  dark: '#141414',
} as const;

// The body is bare aluminium, lighter than the panel.
export const shell = {
  face: '#d5d4d1',
  edge: '#a9a8a5',
  key: '#c9c8c5',
  deep: '#8e8d8a',
} as const;

export const FPS = 30;
export const WIDTH = 1920;
export const HEIGHT = 1080;

// Xteink X3, UC8253, 792x528 native — held portrait, so 528 across by 792 down.
// (The X4's panel is 800x480; they are not the same shape, and the difference is
// visible.) freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkDisplay.h:81
export const PANEL_W = 528;
export const PANEL_H = 792;

// CanvasMetrics (src/components/themes/canvas/CanvasTheme.h:29).
//
// This is the theme the firmware actually runs: UITheme::setTheme() constructs a
// CanvasTheme unconditionally (src/components/UITheme.cpp:24) and there is no
// setting that swaps it. BaseTheme's Classic metrics are the inherited default
// that nothing selects, so laying these screens out against Classic — centred
// headers, inverted selection rows, four outlined hint boxes — drew a device
// that does not ship.
export const metrics = {
  batteryWidth: 26,
  batteryHeight: 18,
  topPadding: 13,
  headerHeight: 44,
  verticalSpacing: 8,
  contentSidePadding: 16,
  listRowHeight: 44,
  listWithSubtitleRowHeight: 66,
  listSidePadding: 16,
  listScrollWidth: 3,
  scrollBarRightOffset: 4,
  headerSidePadding: 16,
  headerUnderlineSize: 1,
  buttonHintsHeight: 40,
  progressBarHeight: 14,
  progressBarMarginTop: 1,
  statusBarHorizontalMargin: 5,
  statusBarVerticalMargin: 19,
  // Selection is an underline, not an inverted row: CanvasTheme sets
  // listSelectionStyle = 2 (Underline) explicitly, to change a 2px rule instead
  // of a filled 44x496 band — "roughly a tenth of the pixels" (CanvasTheme.h:13).
  markerThickness: 2,
} as const;

// 120bpm at 30fps. Every cut in the edit lands on one of these.
export const BEAT = 15;
export const beat = (n: number) => Math.round(n * BEAT);
