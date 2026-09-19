import React from 'react';
import {ink, metrics, notoSans, PANEL_W, ubuntu} from '../theme';

// Chrome the firmware draws on every screen, rebuilt at device scale from the
// theme that actually ships: CanvasTheme (src/components/themes/canvas/), which
// UITheme::setTheme() constructs unconditionally.

// CanvasTheme::fillBatteryIcon + headerBatteryBarStyle: a rounded outlined track
// with a dithered level fill, no terminal nub. 26x18, sized to the header's
// clock so the glyph and the digits share a cap height. The fill is DarkGray
// dither above 20% and solid black at or below it, "because a grey sliver at
// 10% would be easy to miss".
// `nub` draws the terminal pip: the HEADER battery is the nub-less bar style
// (headerBatteryBarStyle), but the reader's status bar goes through
// drawBatteryLeft -> drawBatteryOutline, which keeps the classic outline and
// its nub and only takes Canvas's dithered fill.
export const BatteryBar: React.FC<{percent: number; nub?: boolean}> = ({percent, nub}) => {
  const track = metrics.batteryWidth;
  const maxFill = track - 5;
  const fill = Math.min(maxFill, Math.round((percent * maxFill) / 100) + 1);
  const low = percent <= 20;
  return (
    <span
      style={{
        position: 'relative',
        width: track,
        height: metrics.batteryHeight,
        border: `1px solid ${ink.dark}`,
        borderRadius: 2,
        display: 'inline-block',
        flex: 'none',
      }}
    >
      <span
        style={{
          position: 'absolute',
          left: 1,
          top: 1,
          width: fill,
          height: metrics.batteryHeight - 4,
          background: low ? ink.dark : ink.mid,
          // fillRectDither lays the level down as an ordered pattern, which is
          // what keeps it reading as grey rather than as the darkest mark.
          backgroundImage: low
            ? undefined
            : `repeating-conic-gradient(${ink.dark} 0% 25%, transparent 0% 50%)`,
          backgroundSize: '2px 2px',
        }}
      />
      {nub ? (
        <span
          style={{
            position: 'absolute',
            left: '100%',
            top: 4,
            width: 2,
            height: metrics.batteryHeight - 10,
            background: ink.dark,
          }}
        />
      ) : null}
    </span>
  );
};

// BaseTheme::drawHeader under Canvas tokens: title LEFT (headerTitleAlign = 0),
// 44 tall at topPadding 13, 16px side padding, a 1px bottom rule
// (headerUnderlineSize = 1), and the battery on the right with its percentage in
// the TITLE font (headerStatusUsesTitleFont = true), not the small one.
export const Header: React.FC<{title: string; right?: string; battery?: number}> = ({
  title,
  right,
  battery = 77,
}) => (
  <div
    style={{
      position: 'absolute',
      left: 0,
      right: 0,
      top: metrics.topPadding,
      height: metrics.headerHeight,
      borderBottom: `${metrics.headerUnderlineSize}px solid ${ink.dark}`,
      display: 'flex',
      alignItems: 'center',
      padding: `0 ${metrics.headerSidePadding}px`,
      gap: 8,
    }}
  >
    <span
      style={{
        fontFamily: ubuntu,
        fontSize: 17,
        color: ink.dark,
        flex: 1,
        whiteSpace: 'nowrap',
        overflow: 'hidden',
      }}
    >
      {title}
    </span>
    {right ? (
      <span style={{fontFamily: ubuntu, fontSize: 14, color: ink.dark}}>{right}</span>
    ) : null}
    <BatteryBar percent={battery} />
    <span style={{fontFamily: ubuntu, fontSize: 17, color: ink.dark}}>{battery}%</span>
  </div>
);

// Where a screen's content starts: under the header band, plus verticalSpacing.
// Every activity computes exactly this (HighlightsActivity.cpp:58 and friends).
export const CONTENT_TOP =
  metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

// CanvasTheme::drawButtonHints — a flat bar, not the base theme's four outlined
// boxes: a LightGray dithered ground the full width of the panel, a hairline
// along its top, and quarter-width slots with a separator drawn only between two
// populated ones. Labels are centred in UI_10 (Ubuntu 10).
export const ButtonHints: React.FC<{labels: [string, string, string, string]}> = ({
  labels,
}) => {
  const slot = PANEL_W / 4;
  return (
    <div
      style={{
        position: 'absolute',
        left: 0,
        right: 0,
        bottom: 0,
        height: metrics.buttonHintsHeight,
        borderTop: `1px solid ${ink.dark}`,
        background: ink.light,
        backgroundImage: `repeating-conic-gradient(${ink.mid} 0% 25%, transparent 0% 50%)`,
        backgroundSize: '2px 2px',
        display: 'flex',
      }}
    >
      {labels.map((label, i) => (
        <div
          key={i}
          style={{
            width: slot,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            fontFamily: ubuntu,
            fontSize: 13,
            color: ink.dark,
            // Separators between populated slots only, so a lone hint sits on
            // an open bar.
            borderLeft:
              i > 0 && label && labels[i - 1] ? `1px solid ${ink.dark}` : undefined,
            // The rule is only the middle half of the bar's height.
            borderImage:
              i > 0 && label && labels[i - 1]
                ? `linear-gradient(transparent 25%, ${ink.dark} 25% 75%, transparent 75%) 1`
                : undefined,
          }}
        >
          {label}
        </div>
      ))}
    </div>
  );
};

// A list row. Canvas rows are square, gapless and 44 tall, the label inset by
// listSidePadding, and the SELECTION IS AN UNDERLINE — a 2px rule along the
// row's bottom, inset the same amount (list.h:752). The base theme's inverted
// black row is the thing this theme exists to not do.
export const Row: React.FC<{
  label: string;
  value?: string;
  subtitle?: string;
  selected?: boolean;
}> = ({label, value, subtitle, selected}) => (
  <div
    style={{
      position: 'relative',
      height: subtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight,
      padding: `0 ${metrics.listSidePadding}px`,
      display: 'flex',
      flexDirection: 'column',
      justifyContent: 'center',
      color: ink.dark,
    }}
  >
    <div style={{display: 'flex', alignItems: 'center', justifyContent: 'space-between'}}>
      <span style={{fontFamily: ubuntu, fontSize: 17}}>{label}</span>
      {value ? (
        <span style={{fontFamily: ubuntu, fontSize: 14, color: ink.mid}}>{value}</span>
      ) : null}
    </div>
    {subtitle ? (
      <div style={{fontFamily: ubuntu, fontSize: 13, color: ink.mid, marginTop: 4}}>
        {subtitle}
      </div>
    ) : null}
    {selected ? (
      <div
        style={{
          position: 'absolute',
          left: metrics.listSidePadding,
          right: metrics.listSidePadding,
          bottom: 0,
          height: metrics.markerThickness,
          background: ink.dark,
        }}
      />
    ) : null}
  </div>
);

// A scroll indicator: 3px at the right edge, drawn only when the list overflows.
export const ScrollTrack: React.FC<{from: number; to: number}> = ({from, to}) => (
  <div
    style={{
      position: 'absolute',
      right: metrics.scrollBarRightOffset,
      top: `${from}%`,
      height: `${to - from}%`,
      width: metrics.listScrollWidth,
      background: ink.dark,
    }}
  />
);

// BaseTheme::drawStatusBar, which is the reader's foot and nothing else's.
// Left cluster: the battery icon and its percentage. Centre: the chapter title.
// Right cluster: "page/pages  percent" in one string. Under all of it, a full
// width progress bar (fillMargin = true, so it runs edge to edge). The text is
// SMALL_FONT_ID, which is Noto Sans 8 — not the Ubuntu the rest of the chrome
// uses.
export const StatusRow: React.FC<{
  title: string;
  page: number;
  pages: number;
  percent?: number;
  battery?: number;
}> = ({title, page, pages, percent = 63, battery = 77}) => (
  <>
    <div
      style={{
        position: 'absolute',
        left: metrics.statusBarHorizontalMargin + 1,
        right: metrics.statusBarHorizontalMargin,
        bottom: metrics.progressBarHeight + 8,
        display: 'flex',
        alignItems: 'center',
        fontFamily: notoSans,
        fontSize: 11,
        color: ink.dark,
      }}
    >
      <span style={{display: 'inline-flex', alignItems: 'center', gap: 4, flex: 'none'}}>
        <BatteryBar percent={battery} nub />
        <span>{battery}%</span>
      </span>
      <span style={{flex: 1, textAlign: 'center'}}>{title}</span>
      <span style={{flex: 'none'}}>
        {page}/{pages}&nbsp;&nbsp;{percent}%
      </span>
    </div>
    <div
      style={{
        position: 'absolute',
        left: 0,
        bottom: 0,
        height: metrics.progressBarHeight,
        width: `${percent}%`,
        background: ink.dark,
      }}
    />
  </>
);
