import React from 'react';
import {AbsoluteFill, useCurrentFrame} from 'remotion';
import {Card, cream, creamDim, Eyebrow, Head, Lower, Sub} from '../components/Editorial';
import {At} from '../components/Motion';
import {PhotoHero} from '../components/PhotoHero';
import {HighlightsScreen, ReadLaterScreen} from '../screens/More';
import {CanvasScreen, WordsScreen} from '../screens/Screens';
import {notoSans, notoSerif} from '../theme';

// What the OS keeps for you between sittings, and what it does with the panel
// while you are not looking at it.
export const Memory: React.FC = () => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill>
      <At from={0} to={50}>
        <Card>
          <Eyebrow>Memory</Eyebrow>
          <Head>It remembers so you can.</Head>
        </Card>
      </At>

      <At from={50} to={120}>
        <PhotoHero zoom={1.14} dark>
          <WordsScreen revealed={frame >= 95} />
        </PhotoHero>
        <div style={{position: 'absolute', left: 120, top: 220, width: 700}}>
          <div style={{fontFamily: notoSans, fontSize: 15, fontWeight: 600, letterSpacing: '0.3em', textTransform: 'uppercase', color: creamDim}}>
            Words · due in
          </div>
          <div style={{display: 'flex', gap: 26, alignItems: 'baseline', marginTop: 18}}>
            {[1, 3, 7, 21, 60].map((d, i) => (
              <div key={d} style={{fontFamily: notoSerif, fontWeight: 700, fontSize: 118, letterSpacing: '-0.04em', color: frame >= 56 + i * 6 ? cream : 'rgba(236,231,222,0.18)'}}>
                {d}
              </div>
            ))}
            <div style={{fontFamily: notoSans, fontSize: 30, color: creamDim}}>days</div>
          </div>
          <div style={{fontFamily: notoSans, fontSize: 22, lineHeight: 1.5, color: creamDim, marginTop: 22, maxWidth: 600}}>
            Every dictionary lookup, scheduled for recall. The word comes back alone:
            there is nothing else on the screen to read the answer off.{' '}
            {frame >= 95 ? <span style={{color: cream}}>Reveal, then say whether you knew it. Looking a word up again resets it — the second lookup is the evidence it did not stick.</span> : null}
          </div>
        </div>
      </At>

      <At from={120} to={175}>
        <PhotoHero zoom={1.14} dark>
          <HighlightsScreen />
        </PhotoHero>
        <Lower label="Highlights" title="Every book's bookmarks, one list." body="They are otherwise stored per book and invisible from outside it." />
      </At>

      <At from={175} to={230}>
        <PhotoHero zoom={1.14} dark>
          <ReadLaterScreen />
        </PhotoHero>
        <Lower label="Read later" title="An inbox that empties." body="Drop articles in /ReadLater. Nothing arrives on its own, and removing a piece once read is a button." />
      </At>

      <At from={230} to={300}>
        <PhotoHero zoom={1.14} zoomTo={1.22} over={70} dark>
          <CanvasScreen />
        </PhotoHero>
        <Lower
          label="Reading canvas · asleep · 0 W"
          title="The sleep screen, spent on the book."
          body="The one thing this panel does that nothing else can is hold an image for free. So it holds your progress, a word due, and the last question — not a wallpaper."
        />
      </At>

      <At from={300} to={330}>
        <Card>
          <Sub>&nbsp;</Sub>
          <Head size={84}>Nothing here is a feed.</Head>
          <Sub>No count of anything appears anywhere it was not asked for.</Sub>
        </Card>
      </At>
    </AbsoluteFill>
  );
};
