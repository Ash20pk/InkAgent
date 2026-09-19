import React from 'react';
import {AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig} from 'remotion';
import {ink, PANEL_H, PANEL_W} from '../theme';

// E-ink cannot tween, so nothing in this video fades. Type lands in two frames
// with a flash behind it, which is both what the panel does and what gives the
// cut its snap.

export const Stamp: React.FC<{
  at?: number;
  children: React.ReactNode;
  flash?: boolean;
}> = ({at = 0, children, flash = true}) => {
  const frame = useCurrentFrame();
  if (frame < at) return null;
  const age = frame - at;
  return (
    <div style={{position: 'relative', display: 'inline-block'}}>
      {flash && age < 2 ? (
        <div
          style={{
            position: 'absolute',
            inset: '-6px -10px',
            background: age < 1 ? ink.dark : ink.light,
            zIndex: 2,
          }}
        />
      ) : null}
      <div style={{opacity: age < 1 ? 0 : 1}}>{children}</div>
    </div>
  );
};

// A full-frame refresh: invert, flash, settle. Used on every scene change.
export const CutFlash: React.FC<{at?: number[]}> = ({at = [0]}) => {
  const frame = useCurrentFrame();
  const hit = at.find((a) => frame >= a && frame < a + 3);
  if (hit === undefined) return null;
  const phase = frame - hit;
  return (
    <AbsoluteFill
      style={{background: phase < 1 ? ink.dark : phase < 2 ? ink.paper : ink.light, zIndex: 90}}
    />
  );
};

// A slow push on the device, so a static object still has somewhere to go.
export const PushIn: React.FC<{
  children: React.ReactNode;
  from?: number;
  to?: number;
  over?: number;
}> = ({children, from = 1, to = 1.06, over = 120}) => {
  const frame = useCurrentFrame();
  const s = interpolate(frame, [0, over], [from, to], {
    extrapolateLeft: 'clamp',
    extrapolateRight: 'clamp',
  });
  return (
    <div style={{transform: `scale(${s})`, transformOrigin: 'center'}}>{children}</div>
  );
};

// The device arriving: a spring on scale, because the hardware reveal is the one
// moment the piece is allowed to feel physical.
export const Arrive: React.FC<{children: React.ReactNode; at?: number}> = ({
  children,
  at = 0,
}) => {
  const frame = useCurrentFrame();
  const {fps} = useVideoConfig();
  const s = spring({
    frame: frame - at,
    fps,
    config: {damping: 200, mass: 0.7, stiffness: 90},
  });
  return (
    <div
      style={{
        transform: `scale(${interpolate(s, [0, 1], [0.82, 1])})`,
        opacity: s > 0.02 ? 1 : 0,
      }}
    >
      {children}
    </div>
  );
};

// A cropped close-up of a device screen, full-bleed. The panel is 528 wide; this
// blows a region of it up so a screen detail can carry a whole cut.
export const Crop: React.FC<{
  children: React.ReactNode;
  x: number;
  y: number;
  w: number;
  drift?: number;
}> = ({children, x, y, w, drift = 0}) => {
  const frame = useCurrentFrame();
  const k = 1920 / w;
  const dy = drift ? interpolate(frame, [0, 90], [0, drift], {extrapolateRight: 'clamp'}) : 0;
  return (
    <AbsoluteFill style={{overflow: 'hidden', background: ink.paper}}>
      <div
        style={{
          position: 'absolute',
          left: -x * k,
          top: -(y + dy) * k,
          width: PANEL_W,
          height: PANEL_H,
          transform: `scale(${k})`,
          transformOrigin: 'top left',
        }}
      >
        {children}
      </div>
    </AbsoluteFill>
  );
};

// Blocks dissolve: each one fades in over the one before it, which stays
// underneath until the fade is done. Later blocks paint on top, so a plain
// opacity ramp on the incoming block is the whole transition.
export const DISSOLVE = 14;
export const At: React.FC<{
  from: number;
  to: number;
  children: React.ReactNode;
}> = ({from, to, children}) => {
  const frame = useCurrentFrame();
  if (frame < from || frame >= to + DISSOLVE) return null;
  const opacity = interpolate(frame, [from, from + DISSOLVE], [0, 1], {
    extrapolateLeft: 'clamp',
    extrapolateRight: 'clamp',
  });
  return <AbsoluteFill style={{opacity}}>{children}</AbsoluteFill>;
};
