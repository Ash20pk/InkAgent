import React from 'react';
import {AbsoluteFill, Img, interpolate, staticFile, useCurrentFrame} from 'remotion';
import {PANEL_W} from '../theme';
import {matrix3d, Pt} from './homography';

// The project's own product photograph (firmware/docs/images/cover.jpg) with a
// live screen composited into the glass. The screen is authored at 528 wide and
// mapped onto the four measured corners of the panel in the photo; a multiply
// pass puts the lamp's falloff back over it so it sits in the light of the room.

const PHOTO = 2000; // square
// Panel corners in photo pixels, measured off the frame: TL, TR, BR, BL.
const QUAD: [Pt, Pt, Pt, Pt] = [[730, 518], [1262, 518], [1262, 1425], [735, 1425]];
// The visible glass is taller than 2:3, so the composited screen is too — the
// same 528 columns, more rows. Nothing is stretched.
export const HERO_H = 900;

export const PhotoHero: React.FC<{
  children: React.ReactNode;
  // 1 = the photo fills the frame width; larger pushes in on the device.
  zoom?: number;
  zoomTo?: number;
  over?: number;
  dark?: boolean;
  // Where the panel's centre lands in the frame. Right of centre by default,
  // which leaves the left third clear for type.
  panelX?: number;
}> = ({children, zoom = 1, zoomTo = zoom, over = 150, dark = false, panelX = 1270}) => {
  const frame = useCurrentFrame();
  const z = interpolate(frame, [0, over], [zoom, zoomTo], {
    extrapolateLeft: 'clamp',
    extrapolateRight: 'clamp',
  });
  const base = 1920 / PHOTO;
  const s = base * z;
  // Keep the panel's centre where it is in the frame as the shot pushes in.
  const cx = 996;
  const cy = 970;
  const ox = panelX - cx * s;
  const oy = 540 - cy * s;

  return (
    <AbsoluteFill style={{background: '#0e0d0c', overflow: 'hidden'}}>
      {/* The room, out of focus, behind the shot — so the frame is lit edge to
          edge however far the device is pushed off centre. */}
      <Img
        src={staticFile('cover.jpg')}
        style={{
          position: 'absolute',
          left: -200,
          top: -620,
          width: 2320,
          height: 2320,
          filter: 'blur(38px) brightness(0.55) saturate(0.9)',
        }}
      />
      <div
        style={{
          position: 'absolute',
          left: ox,
          top: oy,
          width: PHOTO,
          height: PHOTO,
          transform: `scale(${s})`,
          transformOrigin: 'top left',
        }}
      >
        <Img src={staticFile('cover.jpg')} style={{width: PHOTO, height: PHOTO, display: 'block'}} />

        {/* the live screen, projected onto the glass */}
        <div
          style={{
            position: 'absolute',
            left: 0,
            top: 0,
            width: PANEL_W,
            height: HERO_H,
            transform: matrix3d(PANEL_W, HERO_H, QUAD),
            transformOrigin: '0 0',
            overflow: 'hidden',
            background: '#e7e2d9',
          }}
        >
          <div
            style={{
              width: PANEL_W,
              height: HERO_H,
              // The photo's ink is a warm dark grey, not black, and the glass
              // softens edges a little.
              filter: 'sepia(0.22) brightness(0.985) contrast(0.9) blur(0.3px)',
            }}
          >
            {children}
          </div>
          {/* the lamp: bright top-left, falling off to the lower right */}
          <div
            style={{
              position: 'absolute',
              inset: 0,
              background:
                'linear-gradient(155deg, rgba(255,246,230,0.10) 0%, rgba(0,0,0,0) 35%, rgba(60,45,35,0.28) 100%)',
              mixBlendMode: 'multiply',
            }}
          />
          <div
            style={{
              position: 'absolute',
              inset: 0,
              boxShadow: 'inset 0 0 22px rgba(40,30,20,0.35)',
            }}
          />
        </div>
      </div>

      {dark ? (
        <AbsoluteFill
          style={{
            background:
              'linear-gradient(90deg, rgba(14,13,12,0.86) 0%, rgba(14,13,12,0.55) 38%, rgba(14,13,12,0) 62%)',
          }}
        />
      ) : null}
    </AbsoluteFill>
  );
};
