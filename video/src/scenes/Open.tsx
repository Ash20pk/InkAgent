import React from 'react';
import {AbsoluteFill} from 'remotion';
import {cream, creamDim, From} from '../components/Editorial';
import {PhotoHero} from '../components/PhotoHero';
import {ReaderScreen} from '../screens/Screens';
import {notoSans, notoSerif} from '../theme';
import {PASSAGE} from './passage';

// The device, in the room, reading. The whole claim is in the second line: this
// is the operating system, not something installed on one.
export const Open: React.FC = () => (
  <AbsoluteFill>
    <PhotoHero zoom={1} zoomTo={1.08} over={150} dark>
      <ReaderScreen chapter="Meditations, IV" paragraphs={PASSAGE} page={26} pages={41} />
    </PhotoHero>

    <div style={{position: 'absolute', left: 120, top: 250, maxWidth: 820}}>
      <From at={8}>
        <div
          style={{
            fontFamily: notoSerif,
            fontWeight: 700,
            fontSize: 150,
            letterSpacing: '-0.035em',
            lineHeight: 1,
            color: cream,
          }}
        >
          InkAgent
        </div>
      </From>
      <From at={34}>
        <div
          style={{
            fontFamily: notoSerif,
            fontSize: 44,
            lineHeight: 1.25,
            color: cream,
            marginTop: 34,
            maxWidth: 700,
          }}
        >
          An operating system for the Xteink X3 and X4.
        </div>
      </From>
      <From at={70}>
        <div
          style={{
            fontFamily: notoSans,
            fontSize: 22,
            lineHeight: 1.55,
            color: creamDim,
            marginTop: 30,
            maxWidth: 620,
          }}
        >
          Firmware, not an app. The reader, the agent, the sync, the dictionary and the
          app drawer are one image on one ESP32-C3 — the same binary on both readers, with 380 KB of RAM between them.
        </div>
      </From>
    </div>
  </AbsoluteFill>
);
