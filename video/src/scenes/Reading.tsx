import React from 'react';
import {AbsoluteFill} from 'remotion';
import {Card, Eyebrow, Head, Lower, PaperLower, Sub} from '../components/Editorial';
import {At, Crop} from '../components/Motion';
import {PhotoHero} from '../components/PhotoHero';
import {ControlsScreen, DictionaryScreen, FocusScreen, SyncScreen, TextSettingsScreen, TransferScreen} from '../screens/More';
import {PASSAGE} from './passage';

// The reading experience, one setting per shot. Each is a real entry: the Style
// tab of the reader's Text settings (TextSettingsActivity.cpp:29), the reader
// menu's Look Up row, Settings › Controls (SettingsList.h:306), and the File
// Transfer screen the web server activity draws.
export const Reading: React.FC = () => (
  <AbsoluteFill>
    <At from={0} to={60}>
      <Card>
        <Eyebrow>The reader</Eyebrow>
        <Head>Built for the page, then for the person reading it.</Head>
        <Sub>EPUB, TXT, Markdown, XTC and page images, typeset properly on a panel with four greys.</Sub>
      </Card>
    </At>

    {/* Focus Reading: off, then on, same page */}
    <At from={60} to={90}>
      <Crop x={4} y={60} w={520}>
        <FocusScreen paragraphs={PASSAGE} on={false} />
      </Crop>
    </At>
    <At from={90} to={150}>
      <Crop x={4} y={60} w={520}>
        <FocusScreen paragraphs={PASSAGE} on />
      </Crop>
      <PaperLower
        label="Reader · Text · Style"
        title="Focus Reading"
        body="Fixation points bolded into every word. Some readers — ADHD readers especially — stay on the line."
      />
    </At>

    <At from={150} to={215}>
      <PhotoHero zoom={1.14} dark>
        <DictionaryScreen />
      </PhotoHero>
      <Lower
        label="Reader menu · Look Up"
        title="Offline dictionary."
        body="StarDict files on the SD card. No connection, no account — and every lookup is saved to Words for recall."
      />
    </At>

    <At from={215} to={275}>
      <PhotoHero zoom={1.14} dark>
        <TextSettingsScreen />
      </PhotoHero>
      <Lower
        label="Reader · Text"
        title="Hyphenation, alignment, your fonts."
        body="Noto Serif and Sans built in; drop any TTF on the card for Literata, Atkinson Hyperlegible, or CJK, Greek and Arabic coverage."
      />
    </At>

    <At from={275} to={335}>
      <PhotoHero zoom={1.14} dark>
        <ControlsScreen />
      </PhotoHero>
      <Lower
        label="X3 only"
        title="Tilt to turn the page."
        body="The X3's gyroscope, plus auto page turn, chapter skip on hold, and a long-press you assign yourself."
      />
    </At>

    <At from={335} to={395}>
      <PhotoHero zoom={1.14} dark>
        <SyncScreen />
      </PhotoHero>
      <Lower
        label="Your library, wherever it is"
        title="KOReader sync. Calibre. OPDS."
        body="Progress follows you between devices. Books arrive over Wi-Fi from Calibre or any OPDS catalogue."
      />
    </At>

    <At from={395} to={460}>
      <PhotoHero zoom={1.14} zoomTo={1.2} over={65} dark>
        <TransferScreen />
      </PhotoHero>
      <Lower
        label="File transfer"
        title="Reader to reader."
        body="One device opens File transfer; the other picks a file and sends it. No computer, no account, no internet."
      />
    </At>
  </AbsoluteFill>
);
