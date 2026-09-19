import React from 'react';
import {ink, metrics, notoSerif, PANEL_W, ubuntu} from '../theme';
import {ButtonHints, CONTENT_TOP, Header, Row, ScrollTrack, StatusRow} from './Chrome';
import {Screen} from './Screens';

// The reading screens, each one following the activity that draws it.

// Focus Reading is a Style row under the reader's Text settings
// (TextSettingsActivity.cpp:29). The split below is the visible effect, not the
// firmware's exact rule.
const focus = (text: string) =>
  text.split(' ').map((w, i) => {
    const n = Math.max(1, Math.ceil(w.replace(/[^A-Za-z]/g, '').length * 0.4));
    return (
      <React.Fragment key={i}>
        <b>{w.slice(0, n)}</b>
        {w.slice(n)}{' '}
      </React.Fragment>
    );
  });

export const FocusScreen: React.FC<{paragraphs: string[]; on: boolean}> = ({
  paragraphs,
  on,
}) => (
  <Screen>
    <div style={{padding: '30px 20px 0'}}>
      {paragraphs.map((p, i) => (
        <p
          key={i}
          style={{
            fontFamily: notoSerif,
            fontSize: 16,
            lineHeight: '26px',
            textAlign: 'justify',
            color: ink.dark,
            margin: '0 0 14px',
            hyphens: 'auto',
          }}
        >
          {on ? focus(p) : p}
        </p>
      ))}
    </div>
    <StatusRow title="Meditations, IV" page={26} pages={41} />
  </Screen>
);

// DictionaryDefinitionActivity — a whole screen, not a sheet over the page. The
// headword is bold at the top left in UI_12 (no header band, no rule), the page
// counter is right-aligned beside it, and the definition runs below in the
// reader's own font. This is the same activity that shows the agent's question,
// which is why the two screens look identical.
export const DictionaryScreen: React.FC = () => (
  <Screen>
    <div
      style={{
        position: 'absolute',
        left: 20,
        right: 20,
        top: metrics.topPadding + 4,
        display: 'flex',
        alignItems: 'baseline',
        justifyContent: 'space-between',
      }}
    >
      <span style={{fontFamily: ubuntu, fontWeight: 700, fontSize: 17, color: ink.dark}}>
        impede
      </span>
      <span style={{fontFamily: ubuntu, fontSize: 13, color: ink.dark}}>1/2</span>
    </div>
    <div
      style={{
        position: 'absolute',
        left: 20,
        right: 20,
        top: metrics.topPadding + metrics.headerHeight,
        fontFamily: notoSerif,
        fontSize: 16,
        lineHeight: '26px',
        color: ink.dark,
      }}
    >
      <p style={{margin: '0 0 14px'}}>
        <i>v.t.</i> To hinder; to stop in progress; to obstruct; as, to impede the
        advance of troops.
      </p>
      <p style={{margin: '0 0 14px'}}>
        <i>v.i.</i> To be a hindrance; to obstruct the progress of.
      </p>
      <p style={{margin: 0}}>
        From the Latin <i>impedire</i>, to entangle the feet, from <i>in-</i> + <i>pes</i>,
        foot.
      </p>
    </div>
    <ButtonHints labels={['« Back', '', '<', '>']} />
  </Screen>
);

// TextSettingsActivity — a live specimen across the top (previewHeightPercent =
// 30 of the usable height, re-laid as you move: TextSettingsPreview.h), then the
// tabs Font | Size | Layout | Style with the active one filled, then that tab's
// rows. Focus Reading, Hyphenation, Embedded Style and Text AA are the Style
// rows, in that order.
const TABS = ['Font', 'Size', 'Layout', 'Style'];

// previewHeightPercent = 30 of the height left under the header.
const PREVIEW_H = 200;

export const TextSettingsScreen: React.FC<{tab?: number}> = ({tab = 3}) => (
  <Screen>
    <Header title="Text" />
    <div
      style={{
        position: 'absolute',
        left: metrics.contentSidePadding,
        right: metrics.contentSidePadding,
        top: metrics.topPadding + metrics.headerHeight + 10,
        height: PREVIEW_H,
        overflow: 'hidden',
        fontFamily: notoSerif,
        fontSize: 16,
        lineHeight: '26px',
        textAlign: 'justify',
        color: ink.dark,
      }}
    >
      <p style={{margin: 0}}>
        The impediment to action advances action. What stands in the way becomes the
        way. Our actions may be impeded, but there can be no impeding our intentions or
        dispositions, because we can accommodate and adapt.
      </p>
      <div style={{fontFamily: ubuntu, fontSize: 13, color: ink.mid, marginTop: 8}}>
        Noto Serif · 16
      </div>
    </div>
    <div
      style={{
        position: 'absolute',
        left: metrics.contentSidePadding,
        right: metrics.contentSidePadding,
        top: metrics.topPadding + metrics.headerHeight + 10 + PREVIEW_H + metrics.verticalSpacing,
        height: 34,
        display: 'flex',
        gap: 8,
      }}
    >
      {TABS.map((t, i) => (
        <div
          key={t}
          style={{
            flex: 1,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            fontFamily: ubuntu,
            fontSize: 14,
            background: i === tab ? ink.dark : 'transparent',
            color: i === tab ? ink.paper : ink.dark,
            border: i === tab ? undefined : `1px solid ${ink.light}`,
          }}
        >
          {t}
        </div>
      ))}
    </div>
    <div
      style={{
        position: 'absolute',
        left: 0,
        right: 0,
        top:
          metrics.topPadding + metrics.headerHeight + 10 + PREVIEW_H +
          metrics.verticalSpacing + 34 + metrics.verticalSpacing,
      }}
    >
      <Row label="Focus Reading" value="On" selected />
      <Row label="Hyphenation" value="On" />
      <Row label="Embedded Style" value="On" />
      <Row label="Text Anti-aliasing" value="On" />
    </div>
    <ButtonHints labels={['« Back', 'Select', 'Up', 'Down']} />
  </Screen>
);

// Settings › Controls (SettingsList.h:306). Tilt Page Turn is inserted into this
// list only on a board that has the tilt sensor, which is what makes it an X3
// row rather than a universal one.
export const ControlsScreen: React.FC = () => (
  <Screen>
    <Header title="Controls" />
    <div style={{position: 'absolute', left: 0, right: 0, top: CONTENT_TOP}}>
      <Row label="Side Button Layout (reader)" value="Prev/Next" />
      <Row label="Tilt Page Turn" value="Forward" selected />
      <Row label="Show Reader Menu" value="Tap" />
      <Row label="Orient front buttons" value="On" />
      <Row label="Long-press button behavior" value="Chapter skip" />
      <Row label="Long-press Menu" value="Look Up" />
      <Row label="Short Power Button Click" value="Page Turn" />
    </div>
    <ScrollTrack from={6} to={62} />
    <ButtonHints labels={['« Back', 'Select', 'Up', 'Down']} />
  </Screen>
);

// Settings › System › KOReader Sync.
export const SyncScreen: React.FC = () => (
  <Screen>
    <Header title="KOReader Sync" />
    <div style={{position: 'absolute', left: 0, right: 0, top: CONTENT_TOP}}>
      <Row label="Sync Server URL" value="sync.koreader.rocks" />
      <Row label="KOReader Username" value="ash" />
      <Row label="Sync Behavior" value="Ask every time" selected />
      <Row label="Sync Progress" value="41% · pushed" />
    </div>
    <ButtonHints labels={['« Back', 'Select', 'Up', 'Down']} />
  </Screen>
);

// InkAgentWebServerActivity — the File Transfer screen: a centred bold prompt, a
// centred QR, the URL under it, and the .local fallback in the small font. One
// hint, and it says Exit rather than Back.
const QR_SIDE = 200;

export const TransferScreen: React.FC = () => (
  <Screen>
    <Header title="File Transfer" />
    <div
      style={{
        position: 'absolute',
        left: 0,
        right: 0,
        top: CONTENT_TOP + 16,
        textAlign: 'center',
        fontFamily: ubuntu,
        color: ink.dark,
      }}
    >
      <div style={{fontSize: 14, fontWeight: 700}}>Open this URL in your browser</div>
      <div
        style={{
          width: QR_SIDE,
          height: QR_SIDE,
          margin: '18px auto 0',
          display: 'grid',
          gridTemplateColumns: 'repeat(21, 1fr)',
          background: ink.paper,
        }}
      >
        {Array.from({length: 441}).map((_, i) => {
          const r = Math.floor(i / 21);
          const c = i % 21;
          const finder =
            (r < 7 && c < 7) || (r < 7 && c > 13) || (r > 13 && c < 7)
              ? (r % 6 === 0 || c % 6 === 0 || (r > 1 && r < 5 && c > 1 && c < 5)) &&
                !(r > 13 && c > 13)
              : (r * 7 + c * 13 + ((r * c) % 5)) % 3 === 0;
          return <div key={i} style={{background: finder ? ink.dark : 'transparent'}} />;
        })}
      </div>
      <div style={{fontSize: 14, marginTop: 16}}>http://192.168.1.42/</div>
      <div style={{fontSize: 12, marginTop: 6}}>or http://inkagent.local/</div>
    </div>
    <ButtonHints labels={['Exit', '', '', '']} />
  </Screen>
);

// HighlightsActivity — the passage first and its book underneath, "so you
// recognise what you marked, not which file it lives in". Rows are the 66px
// with-subtitle height and the selected one carries a 3px rule at its foot.
const HIGHLIGHTS: [string, string][] = [
  ['What stands in the way becomes the way.', 'Meditations · 41%'],
  ['By love may he be gotten and holden; but by thought never.', 'The Cloud of Unknowing · 12%'],
  ['It is a narrow mind which cannot look at a subject from various', 'Middlemarch · 63%'],
  ['Nothing happens to any man which he is not formed by nature to', 'Meditations · 44%'],
];

export const HighlightsScreen: React.FC<{selected?: number}> = ({selected = 1}) => (
  <Screen>
    <Header title="Highlights" />
    <div style={{position: 'absolute', left: 0, right: 0, top: CONTENT_TOP}}>
      {HIGHLIGHTS.map(([summary, book], i) => (
        <Row key={book + i} label={summary} subtitle={book} selected={i === selected} />
      ))}
    </div>
    <ButtonHints labels={['« Back', 'Select', 'Up', 'Down']} />
  </Screen>
);

// ReadLaterActivity — plain 44px rows, one wrapped line each, no reading times
// and no count in the header: the activity draws the file names and nothing else.
export const ReadLaterScreen: React.FC<{selected?: number}> = ({selected = 1}) => (
  <Screen>
    <Header title="Read Later" />
    <div style={{position: 'absolute', left: 0, right: 0, top: CONTENT_TOP}}>
      <Row label="The Tyranny of the Marginal User" selected={selected === 0} />
      <Row label="On the Shortness of Life" selected={selected === 1} />
      <Row label="How Aristotle Created the Computer" selected={selected === 2} />
    </div>
    <ButtonHints labels={['« Back', 'Select', 'Up', 'Down']} />
  </Screen>
);
