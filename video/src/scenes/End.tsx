import React from 'react';
import {AbsoluteFill} from 'remotion';
import {cream, creamDim, From} from '../components/Editorial';
import {PhotoHero} from '../components/PhotoHero';
import {CanvasScreen} from '../screens/Screens';
import {notoSans, notoSerif} from '../theme';

// Back to the room. The device is asleep and still saying something.
export const End: React.FC = () => (
  <AbsoluteFill>
    <PhotoHero zoom={1.02} zoomTo={0.98} over={150} dark>
      <CanvasScreen />
    </PhotoHero>
    <div style={{position: 'absolute', left: 120, top: 260, maxWidth: 800}}>
      <From at={6}>
        <div style={{fontFamily: notoSerif, fontWeight: 700, fontSize: 120, letterSpacing: '-0.035em', lineHeight: 1, color: cream}}>
          InkAgent
        </div>
      </From>
      <From at={26}>
        <div style={{fontFamily: notoSerif, fontSize: 38, lineHeight: 1.3, color: cream, marginTop: 30}}>
          Firmware for the Xteink X3 and X4. Open source, MIT.
        </div>
      </From>
      <From at={52}>
        <div style={{fontFamily: notoSans, fontSize: 24, color: creamDim, marginTop: 34, lineHeight: 1.7}}>
          github.com/Ash20pk/InkAgent
          <br />
          relay.inkagent.dev — or run your own
        </div>
      </From>
    </div>
  </AbsoluteFill>
);
