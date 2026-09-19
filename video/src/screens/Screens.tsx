import React from 'react';
import {ink, metrics, notoSerif, PANEL_W, ubuntu} from '../theme';
import {ButtonHints, CONTENT_TOP, Header, Row, ScrollTrack, StatusRow} from './Chrome';

// Whole screens, authored at the X3's real 528x792, each one following the
// activity that draws it. Nothing here is a size the panel could not draw, and
// nothing here is a screen the firmware does not have.

export const Screen: React.FC<{children: React.ReactNode}> = ({children}) => (
  <div
    style={{
      width: PANEL_W,
      height: '100%',
      background: ink.paper,
      position: 'relative',
      overflow: 'hidden',
    }}
  >
    {children}
  </div>
);

// EpubReaderActivity::renderContents. The page is text and the status bar, and
// nothing else: no header band, no button hints (those only appear once the
// toolbar overlay is open), and no chapter heading over the body — the chapter
// name lives in the status bar, which is where drawStatusBar puts it.
export const ReaderScreen: React.FC<{
  chapter: string;
  paragraphs: string[];
  page: number;
  pages: number;
  percent?: number;
}> = ({chapter, paragraphs, page, pages, percent = 63}) => (
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
          {p}
        </p>
      ))}
    </div>
    <StatusRow title={chapter} page={page} pages={pages} percent={percent} />
  </Screen>
);

// AskBookActivity::renderKindMenu. This screen does NOT go through the themed
// list: it draws its own rows, so the selection here really is an inverted bar —
// fillRect(8, y - 4, width - 16, lineHeight), inset 8px from each edge. Rows are
// a UI_12 line height plus 8, starting at topPadding + headerHeight +
// verticalSpacing * 2. Two hints only: Back, and Ask the book.
const KIND_LABELS = [
  'Question me on this',
  'Explain this',
  'Story so far',
  'Who is this?',
  'Translate',
];

export const AskMenuScreen: React.FC<{selected: number}> = ({selected}) => {
  const rowH = 28; // getLineHeight(UI_12) + 8
  const top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  return (
    <Screen>
      <Header title="Ask the book" />
      {KIND_LABELS.map((label, i) => (
        <div
          key={label}
          style={{
            position: 'absolute',
            left: 8,
            right: 8,
            top: top + i * rowH - 4,
            height: rowH,
            background: i === selected ? ink.dark : 'transparent',
            display: 'flex',
            alignItems: 'center',
          }}
        >
          <span
            style={{
              fontFamily: ubuntu,
              fontSize: 17,
              color: i === selected ? ink.paper : ink.dark,
              paddingLeft: 12, // text is drawn at x = 20, the row starts at 8
            }}
          >
            {label}
          </span>
        </div>
      ))}
      <ButtonHints labels={['« Back', 'Ask the book', '', '']} />
    </Screen>
  );
};

// AskBookActivity::renderStatus — the header, one centred line, one hint.
export const WaitScreen: React.FC<{message?: string}> = ({message = 'Asking...'}) => (
  <Screen>
    <Header title="Ask the book" />
    <div
      style={{
        position: 'absolute',
        inset: 0,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        fontFamily: ubuntu,
        fontSize: 17,
        color: ink.dark,
      }}
    >
      {message}
    </div>
    <ButtonHints labels={['« Back', '', '', '']} />
  </Screen>
);

// What comes back is shown by DictionaryDefinitionActivity, the same activity a
// dictionary lookup uses. There is no header band: the headword is drawn bold at
// the top left in UI_12, the page counter right-aligned beside it, and the body
// below in the reader's own font. The hints are Back and the two page arrows.
export const AnswerScreen: React.FC<{
  headword: string;
  body: string;
  page?: number;
  pages?: number;
}> = ({headword, body, page = 1, pages = 2}) => (
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
        {headword}
      </span>
      {pages > 1 ? (
        <span style={{fontFamily: ubuntu, fontSize: 13, color: ink.dark}}>
          {page}/{pages}
        </span>
      ) : null}
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
      {body}
    </div>
    <ButtonHints labels={['« Back', '', '<', '>']} />
  </Screen>
);

// WordListActivity::renderReview. The word sits alone and centred at mid-screen,
// because "recall works when there is nothing else to read off the screen" —
// there is no definition on this screen, no book, and no schedule shown. What is
// under it is the prompt before you reveal, and the book after. Above the hints,
// how many are left, in words rather than as a bare digit.
export const WordsScreen: React.FC<{revealed?: boolean; remaining?: number}> = ({
  revealed,
  remaining = 3,
}) => (
  <Screen>
    <Header title="Word List" />
    <div
      style={{
        position: 'absolute',
        left: 0,
        right: 0,
        top: '50%',
        transform: 'translateY(-100%)',
        textAlign: 'center',
      }}
    >
      <div style={{fontFamily: ubuntu, fontWeight: 700, fontSize: 17, color: ink.dark}}>
        apophatic
      </div>
      <div
        style={{
          fontFamily: ubuntu,
          fontSize: 13,
          color: ink.dark,
          marginTop: metrics.verticalSpacing * 2,
        }}
      >
        {revealed ? 'The Cloud of Unknowing' : 'Can you recall what this means?'}
      </div>
    </div>
    {remaining > 0 ? (
      <div
        style={{
          position: 'absolute',
          left: 0,
          right: 0,
          bottom: metrics.buttonHintsHeight + 18,
          textAlign: 'center',
          fontFamily: ubuntu,
          fontSize: 13,
          color: ink.dark,
        }}
      >
        {remaining} more after this
      </div>
    ) : null}
    <ButtonHints
      labels={
        revealed ? ['« Back', 'Knew it', 'Forgot', ''] : ['« Back', 'Reveal', '', '']
      }
    />
  </Screen>
);

// SleepActivity::renderCanvasSleepScreen. A manifest of six rows, every one
// centred, and the whole block centred vertically: logo, title (bold), author,
// percent, "Recall: <word>" (bold), clock. Under it, if one was cached on the
// last wake, the agent's own screen — which is how the last question gets here.
// No rules, no labels, no progress bar: the manifest has no such row kinds.
export const CanvasScreen: React.FC = () => (
  <Screen>
    <div
      style={{
        position: 'absolute',
        inset: 0,
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'center',
        textAlign: 'center',
        padding: '0 30px',
        fontFamily: ubuntu,
        color: ink.dark,
      }}
    >
      <div style={{fontSize: 15, letterSpacing: '0.16em', marginBottom: 14}}>INKAGENT</div>
      <div style={{fontSize: 17, fontWeight: 700}}>Meditations</div>
      <div style={{fontSize: 15, marginTop: 2}}>Marcus Aurelius</div>
      <div style={{fontSize: 15, marginTop: 10}}>41%</div>
      <div style={{fontSize: 17, fontWeight: 700, marginTop: 6}}>Recall: apophatic</div>
      <div style={{fontSize: 15, marginTop: 6}}>21:40</div>

      <div
        style={{
          marginTop: metrics.verticalSpacing * 2,
          fontSize: 15,
          lineHeight: '24px',
          maxWidth: 420,
        }}
      >
        What is Aurelius claiming can never be impeded, and why does that follow?
      </div>
    </div>
  </Screen>
);

// AppDrawerActivity::render. Not a list: a grid of 32px icon tiles with the
// label centred underneath in UI_10, four columns at this width (usable width
// 496 / MIN_TILE_WIDTH 110). Selection is a 4px rule under the tile, inset by a
// sixth of the tile each side — "far fewer changed pixels per move, which is
// what e-ink costs". Built-in, registered and manifest apps are all tiles here,
// which is the claim the scene is making.
const ICON = 32;

// The drawer's icons are 32x32 1-bit glyphs (components/icons/drawerIcons.h).
// These are stand-ins at the same size and weight, not traced copies.
const Glyph: React.FC<{kind: string}> = ({kind}) => {
  const common = {stroke: ink.dark, strokeWidth: 2, fill: 'none'} as const;
  return (
    <svg width={ICON} height={ICON} viewBox="0 0 32 32">
      {kind === 'book' ? (
        <>
          <path d="M5 5h10a4 4 0 0 1 4 4v18a4 4 0 0 0-4-4H5z" {...common} />
          <path d="M27 5H17a4 4 0 0 0-4 4v18a4 4 0 0 1 4-4h10z" {...common} />
        </>
      ) : kind === 'words' ? (
        <>
          <path d="M4 25 12 6l8 19" {...common} />
          <path d="M7 19h10" {...common} />
          <path d="M23 12v13M23 25h5" {...common} />
        </>
      ) : kind === 'bookmark' ? (
        <path d="M8 4h16v24l-8-6-8 6z" {...common} />
      ) : kind === 'chart' ? (
        <>
          <path d="M4 28h24" {...common} />
          <rect x="7" y="17" width="5" height="11" {...common} />
          <rect x="14" y="10" width="5" height="18" {...common} />
          <rect x="21" y="20" width="5" height="8" {...common} />
        </>
      ) : kind === 'inbox' ? (
        <>
          <path d="M4 18 8 6h16l4 12v8H4z" {...common} />
          <path d="M4 18h7l2 4h6l2-4h7" {...common} />
        </>
      ) : kind === 'folder' ? (
        <path d="M4 26V7h9l3 4h12v15z" {...common} />
      ) : kind === 'transfer' ? (
        <>
          <path d="M6 12h20M22 8l4 4-4 4" {...common} />
          <path d="M26 22H6M10 18l-4 4 4 4" {...common} />
        </>
      ) : kind === 'peer' ? (
        <>
          <rect x="4" y="7" width="10" height="18" rx="2" {...common} />
          <rect x="18" y="7" width="10" height="18" rx="2" {...common} />
          <path d="M14 16h4" {...common} />
        </>
      ) : (
        <>
          <rect x="5" y="5" width="9" height="9" {...common} />
          <rect x="18" y="5" width="9" height="9" {...common} />
          <rect x="5" y="18" width="9" height="9" {...common} />
          <rect x="18" y="18" width="9" height="9" {...common} />
        </>
      )}
    </svg>
  );
};

const DRAWER = [
  {label: 'Recent Books', icon: 'book'},
  {label: 'Word List', icon: 'words'},
  {label: 'Highlights', icon: 'bookmark'},
  {label: 'Analytics', icon: 'chart'},
  {label: 'Read Later', icon: 'inbox'},
  {label: 'Browse Files', icon: 'folder'},
  {label: 'File Transfer', icon: 'transfer'},
  {label: 'Send to reader', icon: 'peer'},
];

export const AppsScreen: React.FC<{installed?: boolean; selected?: number}> = ({
  installed,
  selected,
}) => {
  const cols = 4;
  const usable = PANEL_W - metrics.contentSidePadding * 2;
  const tileW = usable / cols;
  const tileH = 88;
  const tiles = installed ? [...DRAWER, {label: 'Status', icon: 'apps'}] : DRAWER;
  return (
    <Screen>
      <Header title="Apps" battery={92} />
      <div
        style={{
          position: 'absolute',
          left: metrics.contentSidePadding,
          top: CONTENT_TOP + 10,
          width: usable,
          display: 'flex',
          flexWrap: 'wrap',
        }}
      >
        {tiles.map((t, i) => (
          <div key={t.label} style={{width: tileW, height: tileH, position: 'relative'}}>
            <div style={{display: 'flex', justifyContent: 'center', paddingTop: metrics.verticalSpacing}}>
              <Glyph kind={t.icon} />
            </div>
            <div
              style={{
                marginTop: metrics.verticalSpacing,
                textAlign: 'center',
                fontFamily: ubuntu,
                fontSize: 13,
                color: ink.dark,
              }}
            >
              {t.label}
            </div>
            {i === selected ? (
              <div
                style={{
                  position: 'absolute',
                  left: tileW / 6,
                  right: tileW / 6,
                  top: metrics.verticalSpacing + ICON + metrics.verticalSpacing + 18,
                  height: 4,
                  background: ink.dark,
                }}
              />
            ) : null}
          </div>
        ))}
      </div>
      <ButtonHints labels={['\u00ab Back', 'Select', 'Up', 'Down']} />
    </Screen>
  );
};

// EpubReaderMenuActivity — how Ask the book is actually reached. The header is
// the book, then a progress line where the old sub-header band sat, then one
// flat list of rows (label left, value right). "Ask the book" sits in it among
// Look Up, Toggle Bookmark and Analytics; it is a row in a menu, not a gesture.
export const ReaderMenuScreen: React.FC<{selected?: number}> = ({selected = 2}) => (
  <Screen>
    <Header title="Meditations" />
    <div
      style={{
        position: 'absolute',
        left: metrics.headerSidePadding,
        top: metrics.topPadding + metrics.headerHeight,
        height: 44,
        display: 'flex',
        alignItems: 'center',
        fontFamily: ubuntu,
        fontSize: 13,
        color: ink.dark,
      }}
    >
      Chapter: 26/41 pages&nbsp;&nbsp;|&nbsp;&nbsp;Book: 41%
    </div>
    <div
      style={{
        position: 'absolute',
        left: 0,
        right: 0,
        top: metrics.topPadding + metrics.headerHeight + 44 + metrics.verticalSpacing,
      }}
    >
      <Row label="Toggle Bookmark" />
      <Row label="Look Up" />
      <Row label="Ask the book" selected={selected === 2} />
      <Row label="Analytics" />
      <Row label="Sync Progress" />
    </div>
    <ScrollTrack from={30} to={80} />
    <ButtonHints labels={['« Back', 'Select', 'Up', 'Down']} />
  </Screen>
);
