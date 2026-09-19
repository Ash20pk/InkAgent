import React from 'react';
import {AbsoluteFill, interpolate, useCurrentFrame} from 'remotion';
import {Card, cream, creamDim, Eyebrow, Head, Lower, Sub} from '../components/Editorial';
import {At} from '../components/Motion';
import {PhotoHero} from '../components/PhotoHero';
import {AppsScreen} from '../screens/Screens';
import {notoSans} from '../theme';

const SOURCE = `{
  "name": "Status",
  "icon": "info",
  "rows": [
    {"kind": "kv", "label": "Reading",
     "value": {"src": "reading.title"}},
    {"kind": "kv", "label": "Battery",
     "value": {"src": "device.battery"}}
  ]
}`;

// The drawer lists built-in, registered and manifest apps identically. A
// manifest is a JSON file in /Apps, and the ceiling on what it can do is the point.
export const Apps: React.FC = () => {
  const frame = useCurrentFrame();
  const typed = Math.floor(interpolate(frame, [40, 110], [0, SOURCE.length], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'}));
  return (
    <AbsoluteFill>
      <At from={0} to={40}>
        <Card>
          <Eyebrow>Apps</Eyebrow>
          <Head>An app drawer, with a ceiling.</Head>
        </Card>
      </At>

      <At from={40} to={170}>
        <PhotoHero zoom={1.14} dark>
          <AppsScreen installed={frame >= 118} selected={frame >= 118 ? 8 : undefined} />
        </PhotoHero>
        <div style={{position: 'absolute', left: 120, top: 190, width: 720}}>
          <div style={{fontFamily: notoSans, fontSize: 15, fontWeight: 600, letterSpacing: '0.3em', textTransform: 'uppercase', color: creamDim, marginBottom: 18}}>
            /Apps/status.json
          </div>
          <pre style={{fontFamily: 'SF Mono, Menlo, monospace', fontSize: 21, lineHeight: 1.5, color: cream, margin: 0, whiteSpace: 'pre-wrap', minHeight: 330, borderLeft: `2px solid rgba(236,231,222,0.35)`, paddingLeft: 22}}>
            {SOURCE.slice(0, typed)}
            {typed < SOURCE.length ? <span style={{background: cream}}>&nbsp;</span> : null}
          </pre>
        </div>
        {frame >= 118 ? (
          <Lower label="Copied to the card" title="It appears in the drawer." body="No firmware build. Or write it on the relay dashboard, validated, and sync it to every reader you own." />
        ) : null}
      </At>

      <At from={170} to={230}>
        <Card>
          <Eyebrow>The ceiling is the feature</Eyebrow>
          <Head size={84}>No if. No loop. No unread count.</Head>
          <Sub>A manifest describes a screen and nothing else, so a third-party app cannot poll you, notify you, or follow you into a book.</Sub>
        </Card>
      </At>
    </AbsoluteFill>
  );
};
