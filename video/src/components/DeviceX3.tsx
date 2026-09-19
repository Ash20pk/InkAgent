import React from 'react';
import {ink, PANEL_H, PANEL_W, shell} from '../theme';

// The Xteink X3, drawn to the proportions in firmware/docs/images: an aluminium
// face about 672 x 1049 device-units around the 528 x 792 panel, so the bezel is
// ~11% of the width at the sides, thin at the top and deep at the bottom where
// the two front keys sit. Side keys are on the edges (hasEdgeSideButtons — X3,
// X4 Pro): Up on the left, Down on the right, high up the body.

export const BODY_W = 672;
export const BODY_H = 1049;
const BEZEL_X = (BODY_W - PANEL_W) / 2; // 72
const BEZEL_TOP = 69;

export const DeviceX3: React.FC<{
  children: React.ReactNode;
  scale?: number;
  shadow?: boolean;
}> = ({children, scale = 1, shadow = true}) => (
  <div
    style={{
      width: BODY_W * scale,
      height: BODY_H * scale,
      position: 'relative',
      filter: shadow ? 'drop-shadow(0 24px 44px rgba(20,20,20,0.30))' : undefined,
    }}
  >
    <div
      style={{
        width: BODY_W,
        height: BODY_H,
        transform: `scale(${scale})`,
        transformOrigin: 'top left',
        position: 'relative',
      }}
    >
      {/* side keys, behind the body so only the tab shows past the edge */}
      <Tab side="left" top={150} />
      <Tab side="right" top={150} />

      {/* aluminium face */}
      <div
        style={{
          position: 'absolute',
          inset: 0,
          background: shell.face,
          borderRadius: 30,
          border: `1px solid ${shell.edge}`,
          boxShadow: `inset 0 1px 0 rgba(255,255,255,0.55), inset 0 -2px 4px rgba(0,0,0,0.10)`,
        }}
      />

      {/* the panel, recessed a hair below the face */}
      <div
        style={{
          position: 'absolute',
          left: BEZEL_X,
          top: BEZEL_TOP,
          width: PANEL_W,
          height: PANEL_H,
          background: ink.paper,
          boxShadow: `0 0 0 1px ${shell.deep}, inset 0 1px 3px rgba(0,0,0,0.22)`,
          overflow: 'hidden',
        }}
      >
        {children}
      </div>

      {/* the two long front keys in the bottom bezel */}
      <Key left={BEZEL_X + 6} />
      <Key left={BODY_W / 2 + 8} />
    </div>
  </div>
);

const Key: React.FC<{left: number}> = ({left}) => (
  <div
    style={{
      position: 'absolute',
      left,
      bottom: 26,
      width: PANEL_W / 2 - 14,
      height: 26,
      borderRadius: 6,
      background: shell.key,
      border: `1px solid ${shell.edge}`,
      boxShadow: 'inset 0 1px 0 rgba(255,255,255,0.6)',
    }}
  />
);

const Tab: React.FC<{side: 'left' | 'right'; top: number}> = ({side, top}) => (
  <div
    style={{
      position: 'absolute',
      top,
      [side]: -6,
      width: 10,
      height: 74,
      borderRadius: 3,
      background: shell.key,
      border: `1px solid ${shell.edge}`,
    }}
  />
);
