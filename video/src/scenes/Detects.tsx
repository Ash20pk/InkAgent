import React from 'react';
import {AbsoluteFill, useCurrentFrame} from 'remotion';
import {Card, cream, creamDim, Eyebrow, Head, Sub} from '../components/Editorial';
import {At} from '../components/Motion';
import {PhotoHero} from '../components/PhotoHero';
import {ReaderScreen} from '../screens/Screens';
import {notoSans, notoSerif} from '../theme';
import {PASSAGE} from './passage';

// What the reader measures from page turns alone (EpubReaderActivity.h:126):
// backward turns on a stretch, and pace against the reader's own running
// average. A session, sped up: read, read, go back, read on.
const TURNS: {at: number; page: number}[] = [
  {at: 0, page: 26},
  {at: 34, page: 27},
  {at: 66, page: 28},
  {at: 96, page: 27}, // back — the thread was lost
  {at: 122, page: 28},
  {at: 160, page: 29},
  {at: 200, page: 30},
];

const state = (frame: number) => {
  let page = 26;
  let regressions = 0;
  let forward = 0;
  let lastAt = 0;
  const gaps: number[] = [];
  for (const t of TURNS) {
    if (frame < t.at) break;
    if (t.page < page) regressions += 1;
    else if (t.at > 0) {
      forward += 1;
      gaps.push(t.at - lastAt);
    }
    page = t.page;
    lastAt = t.at;
  }
  const mean = gaps.length ? gaps.reduce((a, b) => a + b, 0) / gaps.length : 0;
  const last = gaps.length ? gaps[gaps.length - 1] : 0;
  // Pace on the latest stretch relative to the running mean — under 100 is
  // slower than usual.
  const pace = gaps.length >= 2 ? Math.round((mean / last) * 100) : -1;
  return {page, regressions, forward, pace};
};

const Readout: React.FC<{k: string; v: string; dim?: boolean}> = ({k, v, dim}) => (
  <div style={{display: 'flex', alignItems: 'baseline', gap: 22, padding: '14px 0', borderBottom: `1px solid rgba(236,231,222,0.16)`}}>
    <div style={{fontFamily: notoSans, fontSize: 15, letterSpacing: '0.22em', textTransform: 'uppercase', color: creamDim, width: 250}}>
      {k}
    </div>
    <div style={{fontFamily: notoSerif, fontWeight: 700, fontSize: 60, color: dim ? creamDim : cream, letterSpacing: '-0.02em', lineHeight: 1}}>
      {v}
    </div>
  </div>
);

export const Detects: React.FC = () => {
  const frame = useCurrentFrame();
  const local = frame - 60;
  const s = state(Math.max(0, local));

  return (
    <AbsoluteFill>
      <At from={0} to={60}>
        <Card>
          <Eyebrow>Detection</Eyebrow>
          <Head>It reads how you read.</Head>
          <Sub>From page turns alone. No camera, no eye tracking, no sensors.</Sub>
        </Card>
      </At>

      <At from={60} to={300}>
        <PhotoHero zoom={1.12} dark>
          <ReaderScreen chapter="Meditations, IV" paragraphs={PASSAGE} page={s.page} pages={41} />
        </PhotoHero>

        <div style={{position: 'absolute', left: 120, top: 200, width: 640}}>
          <div style={{fontFamily: notoSans, fontSize: 15, fontWeight: 600, letterSpacing: '0.3em', textTransform: 'uppercase', color: creamDim, marginBottom: 20}}>
            This sitting · measured on the device
          </div>
          <Readout k="Page" v={String(s.page)} />
          <Readout k="Went back" v={String(s.regressions)} />
          <Readout k="Pace vs your baseline" v={s.pace < 0 ? '—' : `${s.pace}%`} dim={s.pace < 0} />

          {local >= 100 ? (
            <div style={{fontFamily: notoSerif, fontSize: 30, lineHeight: 1.35, color: cream, marginTop: 40}}>
              Going back over a stretch is the readable sign that attention lapsed.
              <span style={{color: creamDim}}> Readers reread to recover the thread, not to admire the prose.</span>
            </div>
          ) : null}
        </div>
      </At>

      <At from={300} to={390}>
        <Card>
          <Eyebrow>What it does with that</Eyebrow>
          <Head size={84}>Two numbers steer where the question lands.</Head>
          <Sub>
            The stretch you went back over is the one you get asked about. Regressions and
            pace are the only behavioural fields on the wire — and if they were not
            measured, they are omitted, not sent as zero.
          </Sub>
        </Card>
      </At>
    </AbsoluteFill>
  );
};
