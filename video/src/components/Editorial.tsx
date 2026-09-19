import React from 'react';
import {AbsoluteFill, useCurrentFrame} from 'remotion';
import {notoSans, notoSerif} from '../theme';

// The card system between shots: a warm near-black, cream type, a small-caps
// label over a rule, a headline, and one line of body. Restraint is the point;
// the product photo carries the drama.
export const cream = '#ece7de';
export const creamDim = 'rgba(236,231,222,0.62)';
export const ground = '#1a1816';

export const Card: React.FC<{children: React.ReactNode; align?: 'left' | 'center'}> = ({
  children,
  align = 'left',
}) => (
  <AbsoluteFill style={{background: ground}}>
    <AbsoluteFill
      style={{
        background:
          'radial-gradient(ellipse 80% 70% at 30% 40%, rgba(255,235,210,0.06), transparent 70%)',
      }}
    />
    <AbsoluteFill
      style={{
        padding: '0 140px',
        justifyContent: 'center',
        alignItems: align === 'center' ? 'center' : 'flex-start',
        textAlign: align,
      }}
    >
      {children}
    </AbsoluteFill>
  </AbsoluteFill>
);

export const Eyebrow: React.FC<{children: React.ReactNode}> = ({children}) => (
  <div
    style={{
      fontFamily: notoSans,
      fontSize: 17,
      fontWeight: 600,
      letterSpacing: '0.3em',
      textTransform: 'uppercase',
      color: creamDim,
      paddingBottom: 18,
      marginBottom: 34,
      borderBottom: `1px solid rgba(236,231,222,0.22)`,
      minWidth: 220,
    }}
  >
    {children}
  </div>
);

export const Head: React.FC<{children: React.ReactNode; size?: number}> = ({children, size = 92}) => (
  <div
    style={{
      fontFamily: notoSerif,
      fontWeight: 700,
      fontSize: size,
      lineHeight: 1.04,
      letterSpacing: '-0.02em',
      color: cream,
      maxWidth: 1300,
    }}
  >
    {children}
  </div>
);

export const Sub: React.FC<{children: React.ReactNode}> = ({children}) => (
  <div
    style={{
      fontFamily: notoSans,
      fontSize: 27,
      lineHeight: 1.5,
      color: creamDim,
      maxWidth: 980,
      marginTop: 30,
    }}
  >
    {children}
  </div>
);

// Reveal on a frame with a two-frame arrival — no fade.
export const From: React.FC<{at: number; children: React.ReactNode}> = ({at, children}) => {
  const frame = useCurrentFrame();
  if (frame < at) return null;
  return <div style={{opacity: frame - at < 1 ? 0.35 : 1}}>{children}</div>;
};

// Lower-third over a photo shot.
export const Lower: React.FC<{label: string; title: string; body?: string; at?: number}> = ({
  label,
  title,
  body,
  at = 0,
}) => {
  const frame = useCurrentFrame();
  if (frame < at) return null;
  return (
    <div style={{position: 'absolute', left: 120, bottom: 110, maxWidth: 720}}>
      <div
        style={{
          fontFamily: notoSans,
          fontSize: 15,
          fontWeight: 600,
          letterSpacing: '0.3em',
          textTransform: 'uppercase',
          color: creamDim,
          marginBottom: 16,
        }}
      >
        {label}
      </div>
      <div
        style={{
          fontFamily: notoSerif,
          fontWeight: 700,
          fontSize: 58,
          lineHeight: 1.06,
          letterSpacing: '-0.02em',
          color: cream,
        }}
      >
        {title}
      </div>
      {body ? (
        <div
          style={{
            fontFamily: notoSans,
            fontSize: 22,
            lineHeight: 1.5,
            color: creamDim,
            marginTop: 18,
            maxWidth: 640,
          }}
        >
          {body}
        </div>
      ) : null}
    </div>
  );
};

// Lower band for a paper close-up: a cream strip under a rule, the way the
// firmware's own status row sits under a page.
export const PaperLower: React.FC<{label: string; title: string; body?: string}> = ({label, title, body}) => (
  <div
    style={{
      position: 'absolute',
      left: 0,
      right: 0,
      bottom: 0,
      padding: '22px 120px 30px',
      background: '#eae8e3',
      borderTop: '2px solid #141414',
      display: 'flex',
      alignItems: 'baseline',
      gap: 40,
    }}
  >
    <div style={{fontFamily: notoSans, fontSize: 14, fontWeight: 600, letterSpacing: '0.3em', textTransform: 'uppercase', color: '#6a6760', whiteSpace: 'nowrap'}}>
      {label}
    </div>
    <div style={{fontFamily: notoSerif, fontWeight: 700, fontSize: 44, letterSpacing: '-0.02em', color: '#141414', whiteSpace: 'nowrap'}}>
      {title}
    </div>
    {body ? <div style={{fontFamily: notoSans, fontSize: 19, lineHeight: 1.4, color: '#6a6760'}}>{body}</div> : null}
  </div>
);
