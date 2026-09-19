import React from 'react';
import {AbsoluteFill} from 'remotion';
import {ink, notoSans, notoSerif} from '../theme';

export const Paper: React.FC<{children?: React.ReactNode; tone?: string}> = ({
  children,
  tone = ink.paper,
}) => (
  <AbsoluteFill style={{backgroundColor: tone}}>
    {children}
    <AbsoluteFill
      style={{
        opacity: 0.045,
        pointerEvents: 'none',
        mixBlendMode: 'multiply',
        backgroundImage: `repeating-linear-gradient(0deg, ${ink.dark} 0 1px, transparent 1px 3px), repeating-linear-gradient(90deg, ${ink.dark} 0 1px, transparent 1px 3px)`,
      }}
    />
  </AbsoluteFill>
);

// The headline voice: Noto Serif, very large, tight.
export const Slam: React.FC<{children: React.ReactNode; size?: number}> = ({
  children,
  size = 132,
}) => (
  <div
    style={{
      fontFamily: notoSerif,
      fontWeight: 700,
      fontSize: size,
      lineHeight: 0.98,
      letterSpacing: '-0.03em',
      color: ink.dark,
    }}
  >
    {children}
  </div>
);

export const Line: React.FC<{children: React.ReactNode; size?: number}> = ({
  children,
  size = 40,
}) => (
  <div
    style={{
      fontFamily: notoSerif,
      fontSize: size,
      lineHeight: 1.3,
      color: ink.dark,
      maxWidth: 1080,
    }}
  >
    {children}
  </div>
);

export const Label: React.FC<{children: React.ReactNode; tone?: string}> = ({
  children,
  tone = ink.mid,
}) => (
  <div
    style={{
      fontFamily: notoSans,
      fontSize: 19,
      fontWeight: 600,
      letterSpacing: '0.26em',
      textTransform: 'uppercase',
      color: tone,
    }}
  >
    {children}
  </div>
);

// A caption pinned to a corner, for the device scenes.
export const Caption: React.FC<{children: React.ReactNode}> = ({children}) => (
  <div
    style={{
      fontFamily: notoSans,
      fontSize: 16,
      letterSpacing: '0.22em',
      textTransform: 'uppercase',
      color: ink.mid,
    }}
  >
    {children}
  </div>
);

// The product ground. Device shots sit on a dark studio field so the aluminium
// body separates from it; the typographic cards stay on paper. Alternating the
// two is what gives the cut its rhythm.
export const Stage: React.FC<{children?: React.ReactNode}> = ({children}) => (
  <AbsoluteFill style={{background: '#2b2a28'}}>
    <AbsoluteFill
      style={{
        background:
          'radial-gradient(ellipse 70% 60% at 50% 42%, rgba(255,255,255,0.10), transparent 70%)',
      }}
    />
    {children}
  </AbsoluteFill>
);

export const StageCaption: React.FC<{children: React.ReactNode}> = ({children}) => (
  <div
    style={{
      fontFamily: notoSans,
      fontSize: 16,
      letterSpacing: '0.22em',
      textTransform: 'uppercase',
      color: 'rgba(234,232,227,0.62)',
    }}
  >
    {children}
  </div>
);

// Headline set on the dark stage.
export const StageSlam: React.FC<{children: React.ReactNode; size?: number}> = ({
  children,
  size = 96,
}) => (
  <div
    style={{
      fontFamily: notoSerif,
      fontWeight: 700,
      fontSize: size,
      lineHeight: 1.0,
      letterSpacing: '-0.03em',
      color: '#eae8e3',
    }}
  >
    {children}
  </div>
);
